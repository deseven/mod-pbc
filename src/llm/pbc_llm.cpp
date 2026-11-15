#include "pbc_llm.h"
#include "pbc_config.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "pbc_log.h"

#include <algorithm>
#include <cctype>

// Bundled llm-retry, relocated into the pbc_llm namespace for the duration of
// the include (same technique as pbc_json.h) so it cannot clash with another
// module that ships its own copy.
#define LLM_RETRY_IMPLEMENTATION
#define llm pbc_llm
#include <llm_retry.hpp>
#undef llm

using json = pbc_json;

// ---------------------------------------------------------------------------
// Token estimator (rough: 1 token ≈ 4 characters)
// ---------------------------------------------------------------------------
int PBC_EstimateTokens(const std::string& text)
{
    return static_cast<int>(text.size()) / 4 + 1;
}

// ---------------------------------------------------------------------------
// Internal trim
// ---------------------------------------------------------------------------
static std::string Trim(std::string s)
{
    auto notSpace = [](unsigned char c){ return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

// ---------------------------------------------------------------------------
// Case-insensitive string comparison
// ---------------------------------------------------------------------------
static bool IEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// PBC_CallLLMWithConfig  – universal synchronous LLM call
//   openai:     POST {baseUrl}/chat/completions  (OpenAI-compatible)
//   anthropic:  POST {baseUrl}/messages          (Anthropic Messages API)
//   ollama:     POST {baseUrl}/api/chat          (Ollama native)
// Safe to call from any thread; does not touch game objects.
// ---------------------------------------------------------------------------
PBC_LLMResult PBC_CallLLMWithConfig(const PBC_APIConfig& cfg,
                                     const std::string& systemPrompt,
                                     const std::string& userPrompt,
                                     bool preserveNewlines)
{
    PBC_LLMResult result{ false, "", 0 };

    // Clean any unknown {token} placeholders and log warnings.
    std::string sysPrompt  = systemPrompt;
    std::string usrPrompt  = userPrompt;
    PBC_CleanUnknownTokens(sysPrompt);
    PBC_CleanUnknownTokens(usrPrompt);

    const bool isAnthropic = IEquals(cfg.apiType, "anthropic");
    const bool isOllama     = IEquals(cfg.apiType, "ollama");

    // --- Build URL --------------------------------------------------------
    std::string url = cfg.baseUrl;
    if (!url.empty() && url.back() == '/')
        url.pop_back();

    if (isAnthropic)
        url += "/messages";
    else if (isOllama)
        url += "/api/chat";
    else
        url += "/chat/completions";

    // --- Build request body -----------------------------------------------
    json body;
    body["model"] = cfg.model;

    if (isAnthropic)
    {
        // Anthropic Messages API format
        // system is a top-level field, not part of the messages array
        if (!sysPrompt.empty())
            body["system"] = sysPrompt;

        json messages = json::array();
        messages.push_back({ {"role", "user"}, {"content", usrPrompt} });
        body["messages"] = messages;

        // max_tokens is required for Anthropic — unlike OpenAI, it cannot be omitted.
        // If the connection's requestParameters does not include max_tokens, default
        // to a generous limit so the model isn't truncated mid-output.
        if (!cfg.requestParameters.contains("max_tokens"))
            body["max_tokens"] = 4096;
    }
    else if (isOllama)
    {
        // Ollama native /api/chat format
        json messages = json::array();
        if (!sysPrompt.empty())
            messages.push_back({ {"role", "system"}, {"content", sysPrompt} });
        messages.push_back({ {"role", "user"}, {"content", usrPrompt} });
        body["messages"] = messages;
    }
    else
    {
        // OpenAI-compatible /chat/completions format
        json messages = json::array();
        if (!sysPrompt.empty())
            messages.push_back({ {"role", "system"}, {"content", sysPrompt} });
        messages.push_back({ {"role", "user"}, {"content", usrPrompt} });
        body["messages"] = messages;
    }

    // Many endpoints stream by default. The module does not support streaming,
    // so explicitly disable it for every connection type.
    body["stream"] = false;

    // Merge provider-specific extra parameters from the connection file
    // into the request body. Keys in requestParameters override the defaults
    // set above (e.g. max_tokens, temperature, stream).
    if (cfg.requestParameters.is_object())
        body.update(cfg.requestParameters, /*merge_objects=*/true);

    std::string bodyStr = body.dump();

    // --- Build headers ----------------------------------------------------
    std::vector<std::pair<std::string, std::string>> headers;

    if (isAnthropic)
    {
        // Anthropic uses x-api-key header and requires anthropic-version
        if (!cfg.apiKey.empty())
            headers.emplace_back("x-api-key", cfg.apiKey);
        headers.emplace_back("anthropic-version", "2023-06-01");
    }
    else if (isOllama)
    {
        // Ollama supports an optional Bearer token (OLLAMA_API_KEY).
        if (!cfg.apiKey.empty())
            headers.emplace_back("Authorization", "Bearer " + cfg.apiKey);
    }
    else
    {
        // OpenAI-compatible uses Authorization: Bearer
        if (!cfg.apiKey.empty())
            headers.emplace_back("Authorization", "Bearer " + cfg.apiKey);
    }

    if (g_PBC_DebugShowFullRequest)
    {
        PBC_Log(PBC_LogLevel::PBC_DEBUG, "LLM request body:\n{}", PBC_SanitizeForFmt(PBC_TruncateForDebug(bodyStr)));
    }

    // --- Execute request --------------------------------------------------
    // Transient failures (no response, HTTP 408/429/5xx, an error or
    // malformed body) are retried with exponential backoff (1 s, then 2 s),
    // or after the server's Retry-After when it sends one. Other 4xx answers
    // (bad key, unknown model) fail at once: repeating them cannot succeed.
    pbc_llm::RetryConfig retryCfg;
    retryCfg.max_attempts  = 3;
    retryCfg.base_delay_ms = 1000.0;
    retryCfg.max_delay_ms  = 10000.0;
    // Longest Retry-After worth blocking this thread for; beyond it, give up.
    constexpr double MAX_RETRY_AFTER_SEC = 30.0;

    const int MAX_ATTEMPTS = retryCfg.max_attempts;
    int attempt = 0;

    auto attemptOnce = [&]() -> PBC_LLMResult
    {
        ++attempt;
        if (attempt > 1)
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "LLM: retrying (attempt {}/{})...", attempt, MAX_ATTEMPTS);

        PBC_HttpClient http;
        http.SetTimeoutSeconds(cfg.requestTimeoutSec);
        PBC_HttpStatus status;
        std::string responseBody = http.Post(url, bodyStr, headers, &status);

        if (responseBody.empty())
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "LLM: empty response from API (attempt {}/{}).", attempt, MAX_ATTEMPTS);
            // An empty 200 is retried like a dropped connection (code 0).
            int code = status.code == 200 ? 0 : status.code;
            bool retryable = status.retryAfterSec <= MAX_RETRY_AFTER_SEC;
            if (!retryable)
                PBC_Log(PBC_LogLevel::PBC_ERROR, "LLM: server asked to retry after {}s, giving up.", status.retryAfterSec);
            throw pbc_llm::LLMError{ code, "empty response", retryable, status.retryAfterSec };
        }

        if (g_PBC_DebugShowFullRequest)
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "LLM response body:\n{}", PBC_SanitizeForFmt(PBC_TruncateForDebug(responseBody)));
        }

        // Errors in a 200 body (error object, unexpected format, bad JSON)
        // are retried as before; they are reported with code 0.
        std::string failure;
        try
        {
            json resp = json::parse(responseBody);

            if (resp.contains("error"))
            {
                std::string errMsg;
                if (resp["error"].is_object() && resp["error"].contains("message"))
                    errMsg = resp["error"]["message"].get<std::string>();
                else
                    errMsg = responseBody;
                PBC_Log(PBC_LogLevel::PBC_ERROR, "LLM API error (attempt {}/{}): {}",
                          attempt, MAX_ATTEMPTS, errMsg);
                throw pbc_llm::LLMError{ 0, "API error", true };
            }

            std::string text;
            int tokensUsed = 0;

            if (isAnthropic)
            {
                // Anthropic response: content[0].text
                if (!resp.contains("content") || !resp["content"].is_array() || resp["content"].empty())
                {
                    PBC_Log(PBC_LogLevel::PBC_ERROR, "LLM: unexpected Anthropic response format (attempt {}/{}).", attempt, MAX_ATTEMPTS);
                    throw pbc_llm::LLMError{ 0, "unexpected response format", true };
                }
                text = resp["content"][0]["text"].get<std::string>();

                // Usage: input_tokens + output_tokens
                if (resp.contains("usage"))
                {
                    int inputTokens  = resp["usage"].value("input_tokens", 0);
                    int outputTokens = resp["usage"].value("output_tokens", 0);
                    tokensUsed = inputTokens + outputTokens;
                }
            }
            else if (isOllama)
            {
                // Ollama response: message.content (top-level)
                if (!resp.contains("message") || !resp["message"].contains("content"))
                {
                    PBC_Log(PBC_LogLevel::PBC_ERROR, "LLM: unexpected Ollama response format (attempt {}/{}).", attempt, MAX_ATTEMPTS);
                    throw pbc_llm::LLMError{ 0, "unexpected response format", true };
                }
                text = resp["message"]["content"].get<std::string>();

                // Usage: prompt_eval_count (input) + eval_count (output).
                // These fields are often absent for some models — treat missing as zero.
                int inputTokens  = resp.value("prompt_eval_count", 0);
                int outputTokens = resp.value("eval_count", 0);
                tokensUsed = inputTokens + outputTokens;
            }
            else
            {
                // OpenAI response: choices[0].message.content
                text = resp["choices"][0]["message"]["content"].get<std::string>();

                if (resp.contains("usage") && resp["usage"].contains("total_tokens"))
                    tokensUsed = resp["usage"]["total_tokens"].get<int>();
            }

            text = Trim(text);
            // Strip surrounding quotes added by some models
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
                text = text.substr(1, text.size() - 2);
            // Replace newlines with spaces so the reply fits on a single chat line
            // (unless the caller explicitly needs them, e.g. condensation output)
            if (!preserveNewlines)
                for (char& c : text)
                    if (c == '\n' || c == '\r') c = ' ';

            PBC_LLMResult ok{ true, text, tokensUsed };

            PBC_Log(PBC_LogLevel::PBC_DEBUG, "LLM reply ({} tokens): {}", tokensUsed, PBC_SanitizeForFmt(text));

            return ok;
        }
        catch (const std::exception& ex)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "JSON parse error (attempt {}/{}): {}", attempt, MAX_ATTEMPTS, ex.what());
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "Raw response: {}", PBC_SanitizeForFmt(responseBody));
            failure = ex.what();
        }
        throw pbc_llm::LLMError{ 0, failure, true };
    };

    try
    {
        return pbc_llm::with_retry<PBC_LLMResult>(attemptOnce, retryCfg).value;
    }
    catch (const pbc_llm::LLMError&)
    {
        // Every failed attempt has already been logged above.
    }

    return result;
}

// ---------------------------------------------------------------------------
// Convenience wrapper — uses the "default" connection from the registry.
// ---------------------------------------------------------------------------
PBC_LLMResult PBC_CallLLM(const std::string& systemPrompt,
                           const std::string& userPrompt,
                           bool preserveNewlines)
{
    const PBC_APIConfig* cfg = PBC_GetConnection("default");
    if (!cfg)
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "PBC_CallLLM: no default connection configured.");
        return PBC_LLMResult{ false, "", 0 };
    }
    return PBC_CallLLMWithConfig(*cfg, systemPrompt, userPrompt, preserveNewlines);
}
