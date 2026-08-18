#ifndef PBC_JSON_H
#define PBC_JSON_H

// ---------------------------------------------------------------------------
// Bundled nlohmann/json isolation wrapper
// ---------------------------------------------------------------------------
// mod-pbc ships its own copy of nlohmann/json (v3.12.0-main) to
// guarantee a fixed feature set. Because the library is header-only, simply
// including it alongside another module that bundles a different version
// (e.g. ollama-chat) reopens the same `nlohmann` namespace and causes ODR /
// symbol clashes.
//
// To stay self-contained we relocate the whole library into the
// `pbc_nlohmann` namespace by macro-renaming the `nlohmann` token for the
// duration of the include only. This is the same technique already used for
// cpp-httplib (`pbc_httplib`). The rename never leaks into other translation
// units because the macro is undefined again immediately afterwards.
//
// Note: angle-bracket #include paths are not subject to macro expansion, so
// `<nlohmann/json.hpp>` still resolves to our bundled single-header file.
//
// Usage: include "pbc_json.h" instead of <nlohmann/json.hpp> and refer to the
// JSON type as `pbc_json` (or add a local `using json = pbc_json;` alias).
// ---------------------------------------------------------------------------

#define nlohmann pbc_nlohmann
#include <nlohmann/json.hpp>
#undef nlohmann

// Convenience alias mirroring the upstream `nlohmann::json` spelling.
using pbc_json = pbc_nlohmann::json;

#endif // PBC_JSON_H
