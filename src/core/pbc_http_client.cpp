#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "pbc_http.h"
#include "pbc_log.h"
#include "pbc_utils.h"

// Rename httplib namespace to avoid ODR violations with other modules.
#define httplib pbc_httplib
#include <httplib.h>
#include <cstdlib>
#include <regex>
#include <vector>
#include <utility>

PBC_HttpClient::PBC_HttpClient() : m_timeoutSec(120) {}

void PBC_HttpClient::SetTimeoutSeconds(int seconds)
{
    m_timeoutSec = seconds;
}

std::string PBC_HttpClient::Post(const std::string& url,
                                 const std::string& jsonData,
                                 const std::vector<std::pair<std::string, std::string>>& extraHeaders,
                                 PBC_HttpStatus* status)
{
    try
    {
        // Parse URL  e.g. https://api.deepseek.com/v1/chat/completions
        static const std::regex urlRe(R"(^(https?)://([^:/]+)(?::(\d+))?(/.*)?$)");
        std::smatch m;
        if (!std::regex_match(url, m, urlRe))
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "Invalid URL: {}", url);
            return "";
        }

        std::string proto  = m[1].str();
        std::string host   = m[2].str();
        std::string path   = m[4].matched ? m[4].str() : "/";
        int         port   = proto == "https" ? 443 : 80;
        if (m[3].matched) port = std::stoi(m[3].str());

        PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP {} {}:{}{}", proto, host, port, path);

        httplib::Headers headers = {
            {"Accept",       "application/json"},
            {"User-Agent",   "AzerothCore-PBC/1.0"}
        };
        for (const auto& hdr : extraHeaders)
            headers.emplace(hdr.first, hdr.second);

        httplib::Result res;

        if (proto == "https")
        {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient cli(host, port);
            cli.enable_server_certificate_verification(false);
            cli.set_connection_timeout(m_timeoutSec);
            cli.set_read_timeout(m_timeoutSec);
            cli.set_write_timeout(m_timeoutSec);
            res = cli.Post(path, headers, jsonData, "application/json");
#else
            PBC_Log(PBC_LogLevel::PBC_ERROR, "HTTPS requested but OpenSSL not compiled in.");
            return "";
#endif
        }
        else
        {
            httplib::Client cli(host, port);
            cli.set_connection_timeout(m_timeoutSec);
            cli.set_read_timeout(m_timeoutSec);
            cli.set_write_timeout(m_timeoutSec);
            res = cli.Post(path, headers, jsonData, "application/json");
        }

        if (!res)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "HTTP request failed (no response) for {}:{}{}", host, port, path);
            return "";
        }
        if (status)
        {
            status->code = res->status;
            // Only the delta-seconds form ("Retry-After: 20") is used; an
            // HTTP-date is ignored and the caller falls back to its own backoff.
            if (res->has_header("Retry-After"))
            {
                const std::string value = res->get_header_value("Retry-After");
                char* end = nullptr;
                double secs = std::strtod(value.c_str(), &end);
                if (end != value.c_str() && secs >= 0.0)
                    status->retryAfterSec = secs;
            }
        }
        if (res->status != 200)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "HTTP {} from {}:{}{}", res->status, host, port, path);
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "Response body: {}", PBC_SanitizeForFmt(res->body));
            return "";
        }

        PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP OK, body length={}", res->body.size());

        return res->body;
    }
    catch (const std::exception& ex)
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "HTTP exception: {}", ex.what());
        return "";
    }
}
