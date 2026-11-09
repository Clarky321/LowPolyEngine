# Third-party dependencies

Every dependency added to `conanfile.py` or vendored under `third_party/` is listed here (docs/TECH_STACK.md §10).

| Package | Version | License | Kind | Used by | Added in |
|---|---|---|---|---|---|
| Catch2 | 3.16.1 | BSL-1.0 | test-only | `tests/` | M0.1 |

## Developer tools (not linked, not shipped)

Pinned in `requirements-dev.txt`, installed from PyPI into `.venv` locally and in CI.

| Tool | Version | License | Platforms | Added in |
|---|---|---|---|---|
| Conan | 2.34.0 | MIT | Windows, Linux | M0.1 (pinned in M0.3) |
| clang-format (PyPI wheel of LLVM clang-format) | 22.1.3 | Apache-2.0 WITH LLVM-exception | Windows, Linux | M0.3 |
| Ninja | 1.13.2 | Apache-2.0 | Linux only | M0.3 |

## Planned

Decided in ADRs, not yet added: SDL3 3.4.16 (Zlib, ADR 0002), nlohmann_json 3.12.0 (MIT, ADR 0004).
