# Model Connections

Each LLM connection is defined in a standalone JSONC file (JSON with comments). A minimal connection file specifies the API type, base URL, model, and any extra request parameters.

## Getting Started

Example templates for each supported API type (and a couple of tested providers) live in the [`connections/`](../connections/) directory, using the `.jsonc.example` extension:

| File | API type | Provider |
|---|---|---|
| [`openai.jsonc.example`](../connections/openai.jsonc.example) | `openai` | Generic OpenAI-compatible |
| [`anthropic.jsonc.example`](../connections/anthropic.jsonc.example) | `anthropic` | Anthropic |
| [`ollama.jsonc.example`](../connections/ollama.jsonc.example) | `ollama` | Ollama (local) |
| [`deepseek.jsonc.example`](../connections/deepseek.jsonc.example) | `openai` | DeepSeek (tested) |
| [`glm.jsonc.example`](../connections/glm.jsonc.example) | `openai` | GLM / Zhipu AI (tested) |

These `.example` files are read-only templates — **do not edit them directly**. To set up a connection, pick the example that matches your provider and copy it to a new file **without** the `.example` suffix:

```sh
cd modules/mod-pbc/connections
cp openai.jsonc.example my-connection.jsonc
```

Then edit your new file, filling in `baseUrl`, `apiKey` and `model` as needed, and point the relevant config parameter at it. Files in `connections/` that don't end in `.example` are git-ignored, so your keys stay local.

## Supported API Types

The module supports three API types via the `apiType` field:

- **`openai`** — OpenAI-compatible `/chat/completions` endpoint. The module appends `/chat/completions` to `baseUrl` and sends the API key via `Authorization: Bearer` header.
- **`anthropic`** — Anthropic Messages API `/messages` endpoint. The module appends `/messages` to `baseUrl` and sends the API key via `x-api-key` header with the `anthropic-version: 2023-06-01` header.
- **`ollama`** — Ollama native `/api/chat` endpoint. The module appends `/api/chat` to `baseUrl`.

All model-specific parameters (`temperature`, `frequency_penalty`, etc.) live inside the connection file's `requestParameters` object and are merged into every request body.

## Config Parameters

Four config parameters point to connection files, each relative to the server working directory (`env/dist/bin/`):

| Config parameter | Purpose |
|---|---|
| `PBC.DefaultConnection` | Used for character responses and as the fallback for any task without its own connection. **Required.** |
| `PBC.UtilityConnection` | Used for utility requests (combat summaries, quest summaries). Falls back to default if empty. |
| `PBC.CondensationConnection` | Used for condensation (extracting memories from history). Falls back to default if empty. |
| `PBC.RelationshipUpdateConnection` | Used for relationship updates. Falls back to default if empty. |

After configuring, use `.chars connection-test` (optionally with a connection name like `utility`) to verify that a connection is working.

> [!IMPORTANT]
> Due to the complexity and length of the prompts, locally-run models on average home hardware will generally struggle and produce low-quality output as context grows. A cloud-based model with a large context window is recommended. Make sure to also adjust `PBC.MaxHistoryCtx` accordingly — a good starting point is around 25% of the model's total context window. Aim for at least 32k in general, anything less could lead to poor efficiency of character relationship tracking and memory extraction.

## Choosing a Model

Choosing the right model can be tricky. Two tested configurations are listed below — copy the matching `.example` file (as described above) and adjust the fields as needed.

### DeepSeek

DeepSeek offers a reasonable cost/capabilities compromise and can be considered the cheapest viable option. Expect to spend under $1 for several hours of play. See [`deepseek.jsonc.example`](../connections/deepseek.jsonc.example).

### GLM (Zhipu AI)

GLM 5 handles the required tasks impressively well, though the cost adds up fairly quickly. Expect to spend around $2 per long game session with a full party. See [`glm.jsonc.example`](../connections/glm.jsonc.example).

### Other Models

Any OpenAI-compatible API should work with `apiType: "openai"` — just set `baseUrl` to the endpoint (the module appends `/chat/completions` automatically), `model` to the model identifier, and `apiKey` to your bearer token. If the endpoint doesn't require authentication (typically locally-run backends), leave `apiKey` empty.

Use `requestParameters` to inject provider-specific JSON into the request body. Make sure to use parameter names that are valid for your chosen API type (e.g. `top_p` and `top_k` for Anthropic, `frequency_penalty` and `presence_penalty` for OpenAI-compatible providers). For Ollama, provider-specific options nest under `options`.

When switching providers, pay attention to `temperature` — acceptable ranges vary between models. Check the provider's documentation for the recommended value.

## Per-task Connections

Condensation and relationship updates are critical — their output becomes permanent parts of the character's context. You can route these tasks to a more capable model while keeping the main chat on a cheaper/faster one by pointing `PBC.CondensationConnection` and `PBC.RelationshipUpdateConnection` to separate connection files. Any task-specific connection left empty falls back to `PBC.DefaultConnection`.

You can also enable thinking mode (if the model supports it of course) to improve the results further — just add `"thinking": { "type": "enabled" }` to the condensation/relationship connection's `requestParameters`.
