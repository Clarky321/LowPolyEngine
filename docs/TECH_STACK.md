# Technology stack and architecture decisions

**Research snapshot:** 8 November 2026. At this snapshot, SDL 3.4.20 (released 3 November 2026) and Lua 5.5.1 are current release baselines. Versions in the table remain candidate baselines; pin exact Conan package revisions and verify compatibility before implementation. <https://www.libsdl.org/release/> · <https://www.lua.org/versions.html>

## 1. Core language and build

| Technology | Decision | Role / notes |
|---|---|---|
| C++20 | Adopt | Runtime, editor core, platform/rendering and performance-critical systems. |
| CMake | Adopt | Source of truth for targets, options, tests and installation. |
| Conan 2 | Adopt | Primary third-party package manager, profiles, reproducible dependency graph and lockfile. |
| Python 3 | Adopt for tooling | Asset validation, build wrappers, code generation and helper scripts; not required by shipped games. |
| Batch (`.bat`) | Adopt for Windows convenience | Thin wrappers around Conan/CMake/CTest; never maintain a second build system in batch. |
| Ninja / Visual Studio generator | Choose per developer workflow | Keep CMake presets explicit. Visual Studio generator can be useful for native debugging; Ninja is a concise fast default when installed. |

Recommended Conan integration: call `conan install` explicitly, then configure CMake with the generated toolchain/preset. Conan documents `CMakeToolchain`, `CMakeDeps` and presets for this flow. Do not hide dependency installation inside `CMakeLists.txt`. <https://docs.conan.io/2/integrations/cmake.html>

## 2. Windowing and input: SDL3 vs GLFW vs SFML

| Library | Strengths | Limitations for this engine | Verdict |
|---|---|---|---|
| SDL3 | Window/events/input, game controllers, audio/platform facilities; supports an OpenGL context through `SDL_GL_CreateContext`; can also be used with Vulkan. | SDL_GPU is a different graphics abstraction and does not run on OpenGL. More API surface than GLFW. | **Recommended** for an engine and editor intended to support game input/controllers. Use SDL3 window/input with an OpenGL context initially, not SDL_GPU. |
| GLFW | Small and focused: window/context creation, input/events, monitor handling; supports OpenGL and Vulkan workflows. | Less broad as a general game/platform layer; audio/media facilities are outside its primary scope. | Good minimal alternative if you want a very small platform layer. |
| SFML | Convenient high-level multimedia framework, especially friendly for 2D applications. | Not the most natural foundational platform layer for a custom 3D engine with a custom graphics backend. | Not selected. I assume “SMTL” in the request meant **SFML**; if a different library was intended, re-evaluate. |

Official references:
- SDL3 OpenGL context creation: <https://wiki.libsdl.org/SDL3/SDL_GL_CreateContext>
- SDL3 graphics API FAQ: <https://wiki.libsdl.org/SDL3/FAQDevelopment>
- GLFW overview: <https://www.glfw.org/docs/latest/>

Important distinction: SDL3 can create an OpenGL context. SDL_GPU is a separate API; do not expect SDL_GPU to provide an OpenGL backend. If OpenGL is the first renderer, use `SDL_GL_*` and GLAD.

## 3. Graphics API strategy

| API | Platform scope | Advantages | Costs / caveats | Recommendation |
|---|---|---|---|---|
| OpenGL 4.5 Core | Broad desktop support | Small learning curve compared with explicit APIs; straightforward shader iteration; good for the first working renderer. The current Khronos registry lists OpenGL 4.6 as the current specification, but targeting 4.5 is a deliberate compatibility choice. | Driver behavior and implicit state model; not the best basis for every modern GPU feature; macOS support is frozen at older OpenGL versions. | **First backend.** Use a narrow RHI boundary and keep GL-specific code isolated. |
| Vulkan 1.4 | Cross-platform | Explicit control of resources, synchronization and queues; modern GPU capabilities; useful for learning modern low-level rendering. | More code, synchronization complexity, validation and resource-lifetime work. | Second backend after the engine's resource/material model is stable. |
| Direct3D 12 | Windows ecosystem | Native Windows API, explicit GPU control and strong tooling/integration on Windows. | Windows-specific; substantial API and synchronization complexity. | Third backend only if native Windows support is an explicit goal. |
| BGFX | Cross-platform abstraction | Advertises OpenGL 4.3+, Vulkan, Direct3D 11/12, Metal and other backends. | Backend implementation and parts of shader/resource strategy are delegated to the library; less direct control over API internals. | Alternative if multi-API support is a release requirement and self-implementing APIs is not the learning goal. |
| SDL_GPU | Vulkan, D3D12, Metal family | High-level cross-platform GPU abstraction with a smaller interface than native explicit APIs. | Not an OpenGL backend; incompatible with a requirement that explicitly includes OpenGL through the same backend family. | Consider only if OpenGL is dropped from requirements. |

Do not start by creating empty `VulkanRenderer` and `D3D12Renderer` classes. Define only the interfaces needed by the implemented OpenGL renderer. Keep renderer API types out of `scene`, `worldgen`, `scripting` and editor document models.

Official/reference links:
- BGFX backend list: <https://github.com/bkaradzic/bgfx/blob/master/docs/overview.rst>
- SDL3 FAQ: <https://wiki.libsdl.org/SDL3/FAQDevelopment>
- Khronos Vulkan specification: <https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html>
- OpenGL registry: <https://registry.khronos.org/OpenGL/index_gl.php>

## 4. Graphics libraries and supporting dependencies

| Library | Use | Decision |
|---|---|---|
| GLM | Vectors, matrices, quaternions | Adopt. Keep GLM types behind engine math aliases/facades if API independence becomes important. |
| spdlog | Logging | Adopt. Initialize early; route engine/editor logs through one configurable logger. |
| GLAD 2 | OpenGL function loading | Adopt for OpenGL backend. Generate against the project's target API and pin generated source/metadata. |
| EnTT | ECS | Candidate/adopt for initial scene runtime. Keep persistent IDs separate from transient EnTT entity handles. |
| Dear ImGui | Editor UI | Adopt for first editor prototype. Use official SDL3 + OpenGL/Vulkan backends where applicable. |
| Catch2 v3 | Unit/integration testing | Adopt with CTest. Use one main test framework. |
| Lua 5.5.1 | Embedded scripts | Candidate baseline; validate Conan package and toolchain across target platforms. Lua official release list identifies 5.5.1 (3 Aug 2026) as current at research time. |
| LuaBridge3 | C++ ↔ Lua bindings | Candidate. Prove compatibility with selected Lua 5.5.1 package; otherwise use a small project-owned wrapper around Lua C API. |
| stb_image / tinyobjloader / meshoptimizer | Asset import/processing | Add only when the asset pipeline milestone needs them; verify licenses and current maintenance before selection. |
| Tracy | Profiling | Optional later tool; add after there is meaningful runtime behavior to profile. |

Lua release reference: <https://www.lua.org/versions.html>  
LuaBridge3: <https://github.com/kunitoki/LuaBridge3>

## 5. Editor UI: Raven vs Dear ImGui

### Dear ImGui — default choice

Advantages: mature developer tooling ecosystem, immediate-mode workflow, low integration effort, known SDL/OpenGL/Vulkan backends, many examples for inspectors, diagnostics and gizmos. It is sufficient for the first usable editor.

Limitations: the project must build its own higher-level editor architecture (document model, hierarchy, inspector, undo/redo, content browser, selection, commands). Dear ImGui is a UI library, not a complete editor framework.

### Raven — prototype candidate

Raven describes itself as a lightweight multithreaded C GUI library for editors/tools, offers language bindings, a visual designer and viewport integration options. The public README shows viewport callbacks/native child window handles and has build options for tests/examples/tools. However, the README explicitly says it is under active development and its public API may change. Repository documentation also reports platform/testing caveats. I have inspected the public repository/README and selected documentation, but have **not built or run Raven locally**, so runtime compatibility with this engine is not established.

Recommended proof-of-concept before adopting Raven:
1. Build the smallest Raven sample with the same Windows compiler and CMake version as the engine.
2. Create a host window and a dockable editor shell.
3. Embed a real OpenGL scene viewport and verify input focus, resize and high-DPI behavior.
4. Test on at least Windows and one Linux desktop environment if both are targets.
5. Verify API stability, licensing, integration model and how undo/redo/data binding would work.

**Decision:** Dear ImGui for initial editor; Raven may replace it only after a documented prototype proves a meaningful advantage. Do not maintain two editor UI stacks in the production app.

Raven repository: <https://github.com/oscar-soirey/Raven>

## 6. Conan vs Git submodules

Prefer **Conan as the single primary dependency manager**. Use a Conan recipe and lockfile to define dependency versions and build options. Keep third-party dependencies out of manually maintained submodules unless source-level patching/forking is unavoidable.

A Git submodule is appropriate when:
- the source must be patched and the patch cannot reasonably be carried as a package patch;
- a library is not available in the chosen Conan ecosystem or its available recipe is unsuitable;
- the project needs an exact upstream commit or an internal fork.

If using a submodule, pin it to a commit/tag, document its license, and include it in the clean-checkout setup. Do not float `main` or silently update third-party code.

GLAD is generated API loader code; generated source may be vendored under a clearly marked third-party folder, with generation metadata and license notices.

## 7. Build, automation and repository tooling

- `CMakeLists.txt`: targets and feature options; no machine-specific absolute paths.
- `CMakePresets.json`: named configurations such as `windows-msvc-debug`, `windows-msvc-release`, `linux-clang-debug`.
- `conanfile.py`: dependency requirements, generators and build options.
- `conan.lock`: exact dependency graph once baseline is stable.
- `profiles/`: Conan host/build profiles for supported compiler/platform combinations.
- `scripts/python/`: asset validation, shader checks, deterministic test fixtures and build helpers.
- `scripts/windows/`: `.bat` wrappers that call Conan, CMake and CTest, passing through exit codes.

Expected command sequence (names are illustrative until created in repository):

```powershell
conan profile detect --force
conan install . -pr:h=profiles/windows-msvc -pr:b=default --build=missing
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

Use the exact profile/preset names present in the repository. CI should follow the same sequence rather than inventing a parallel build path.

## 8. Docker and GitHub Actions

Docker is useful for reproducible Linux builds, dependency/toolchain pinning and headless tests. It is not necessary to run the Windows desktop editor inside Linux containers. GPU rendering inside Docker often requires host GPU and display/device pass-through, so keep that outside the default unit-test pipeline.

Initial GitHub Actions matrix:
- Windows + MSVC: configure, build Debug/Release as needed, run headless tests.
- Ubuntu + GCC or Clang: configure/build and run tests; ASan/UBSan job where supported.
- Formatting/static checks: clang-format and clang-tidy (initially advisory, then required once clean).
- Optional nightly/self-hosted GPU job: create GL context, compile shaders and run smoke/snapshot tests where reliable.

Do not make every PR depend on screenshot equality across arbitrary GPU drivers. For render validation, prefer controlled hardware/software renderer environments and tolerant image comparisons.

## 9. Testing tools

- Catch2 + CTest: unit and integration test registration.
- clang-format: consistent formatting.
- clang-tidy: static analysis and modernization checks.
- AddressSanitizer + UndefinedBehaviorSanitizer: memory/undefined behavior detection on supported toolchains.
- ThreadSanitizer: optional Linux job for concurrency code when it exists.
- gcovr or llvm-cov: optional coverage reporting; do not optimize to a percentage at the expense of useful assertions.
- libFuzzer: later fuzzing for asset parsers and project-file readers.
- Tracy: optional runtime profiler after a measurable workload exists.

Tests should be layered: pure unit tests do not need SDL or GPU; integration tests exercise engine services; rendering tests are a separate tier.

## 10. License and due diligence

Before adopting any external code, record its license in `third_party/`/`docs/references/THIRD_PARTY_NOTICES.md`, confirm redistribution terms, and decide whether the dependency is runtime, build-time or development-only. Do not copy large portions of tutorial repositories. Use them as references and reimplement concepts, preserving attribution and license obligations where code is reused.
