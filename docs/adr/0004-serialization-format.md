# ADR 0004: Serialization format for world generation settings

- **Status:** Accepted
- **Date:** 2026-11-09
- **Related:** ADR 0003, `docs/ENGINE_PLAN.md` M3/M4

## Context

M3 must save and load the master seed and world generation settings. M4 will add scene and prefab documents. Files are authored data (AGENTS.md §2): they must be versioned, readable in code review and diffable in Git.

## Problem

Choose a file format and a C++ library for it that is available in Conan Center, works with C++20 (the project does not use C++23), and keeps parsing errors from crashing the application.

## Options

Checked in Conan Center on 2026-11-09:

| Option | Version | License | Notes |
|---|---|---|---|
| JSON + `nlohmann_json` | 3.12.0 | MIT | Header-only, C++11+, mature, simple DOM API, non-throwing parse mode. Slower than alternatives; irrelevant for settings-sized files. |
| JSON + `glaze` | 9.1.0 | MIT | Very fast, reflection-based. **Recipe requires C++23** — incompatible with the C++20 baseline. |
| JSON + `rapidjson` | cci.20250205 | MIT | Fast, but no tagged release for years (only snapshot packages); older C++-style API. |
| JSON + `yyjson` | 0.12.0 | MIT | Fast C library; C API needs a wrapper. |
| TOML + `tomlplusplus` | 3.4.0 | MIT | Pleasant for hand-edited config; weaker fit for nested scene/prefab data later. |
| Custom binary | — | — | Not readable or diffable; premature. |

## Decision

Use **JSON** with **`nlohmann_json/3.12.0`** for world generation settings (M3). Scene and prefab documents (M4) are expected to use the same library, but their schema gets its own decision when M4 starts.

Rules:

1. **Envelope.** Every file has a format id and version:
   ```json
   {
     "format": "lpe.worldgen.settings",
     "formatVersion": 1,
     "generatorVersion": 1,
     "seed": "0x9E3779B97F4A7C15",
     "islands": { "count": 4 },
     "noise": { "scale": 0.012, "octaves": 5 }
   }
   ```
2. **64-bit integers as strings.** `seed` and future stable IDs are stored as hex strings. Many JSON tools read numbers as doubles and silently lose precision above 2^53.
3. **Floats** are written with round-trip precision (nlohmann default), so save → load gives identical values.
4. **Missing fields** take documented defaults for that `formatVersion`. **Unknown fields** are ignored with a logged warning. A `formatVersion` newer than the reader supports is an error.
5. **Migrations** are explicit functions `vN → vN+1`, applied on load before reading fields.
6. **Errors do not throw across the subsystem boundary.** Parsing uses the non-throwing mode (`json::parse(text, nullptr, false)`) and checked field access; the loader returns a project `Result`-type (value or error with a message and field path). C++20 has no `std::expected`, so the project defines its own small result type.
7. **No library types in public headers.** `nlohmann::json` stays inside serialization `.cpp` files; public APIs take/return engine types (`WorldGenSettings`) and `std::string`/paths.
8. Values are validated after loading with the same rules the UI uses (ranges, clamping policy), so a hand-edited file cannot produce invalid generator input.

## Consequences

- One new header-only dependency in M3, MIT license, recorded in `docs/references/THIRD_PARTY_NOTICES.md`.
- Tests required: round-trip, missing fields, unknown fields, wrong types, malformed JSON, newer `formatVersion`, seed above 2^53.
- If profiling later shows JSON loading is a bottleneck for large scenes, a binary cache can be added next to the JSON source of truth; this ADR does not change.
- Revisit if the project moves to C++23 (glaze becomes an option) or if M4 scene requirements do not fit this approach.
