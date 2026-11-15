# ADR 0024: Lua runtime and bindings

- **Status:** Accepted
- **Date:** 2026-11-15
- **Related:** ADR 0001 (Lua 5.5 and LuaBridge3 as candidates), AGENTS.md §2, §4, §5 (Lua boundary and safety), ADR 0021 (components), ADR 0022 (script assets), ADR 0023 (scene documents; amended for the script component), `docs/milestones/m4-scene-data-model.md` M4.5

## Context

M4 needs scripts that change permitted properties of entities through a narrow API, with every error contained: syntax and runtime errors, wrong arguments, missing or destroyed entities, endless loops and memory exhaustion must not stop the engine. Scripts are project content but may come from outside the project, so they get no file, process or native-module access. The M4 dependency graph is final with Lua, so the Conan lockfile is created now.

## Package

`lua/5.5.0` from Conan Center, recipe revision `2bac82d0b9a1744cfaf436c5c1d01bee`; MIT; a static library built from the lua.org sources (`--build=missing`; Conan Center has no binary for these options), linked `PRIVATE` and included only in `engine/src/scripting/lua/`. Options: `compile_as_cpp=True`, `shared=False`, `with_tools=False`, `with_readline=False`. Built with MSVC 19.51 and GCC 13 in the CI of M4.5 (Debug and Release). Lua 5.5.1 is upstream but not in Conan Center (ADR 0001).

The C API used was checked against the 5.5 headers and the 5.5 reference manual, not assumed from 5.4: `lua_newstate(allocator, userData, seed)` has a third parameter, the string-hash seed; `luaL_openselectedlibs(L, load, preload)` opens libraries by the masks `LUA_GLIBK`, `LUA_STRLIBK`, …; the count hook runs only while Lua code runs; `luaL_loadbufferx` takes the mode `"t"` for text only; `string.dump` still makes binary chunks.

## Options

| Question | Options | Choice |
|---|---|---|
| Error mechanism | Lua as C: errors by `longjmp`, so no C++ object with a non-trivial destructor may be alive in a frame an error jumps over; Lua as C++ (`compile_as_cpp`): errors are C++ exceptions of Lua's own | **C++**: a Lua error unwinds the bindings' frames properly, so they may use `std::string` and other RAII values. The headers then have C++ linkage: `lua.h` is included directly (`lua.hpp` does not exist in this build); a mismatch would fail at link time. |
| Binding layer | LuaBridge3 (not in Conan Center; the newest release is the pre-release 3.0-rc13, to be vendored); a project-owned layer over the C API | **project-owned**: the API is nine functions and `print`; no vendored pre-release; error handling stays explicit. |
| Identity in Lua | entity IDs as strings; userdata holding an ID | **strings**, the canonical text of ADR 0020: nothing for Lua to keep alive or to dangle. |

## Decision

1. **Layout:** `engine/include/engine/scripting/ScriptRuntime.hpp` is the facade, with no Lua type. `engine/src/scripting/lua/LuaVm.hpp` is one VM with its allocator, budget and sandbox, and `ScriptRuntime.cpp` holds the instances and the engine API. `scene::ScriptComponent` lives in `engine/scene/` with the other components, because the scene lists them and scripting depends on the scene, not the reverse. One VM per `ScriptRuntime`; no second VM.
2. **Protected entry:** every call from C++ into Lua goes through `LuaVm::protect`: the work runs in `lua_pcall` with a message handler that adds a traceback (`luaL_traceback`). This covers loading, calling `start` and `update`, releasing a script, and even opening the libraries, so no Lua error — a memory error included — reaches `lua_atpanic`. A C++ exception from the work is caught there and becomes a Lua error; Lua's own exceptions are not `std::exception`s and pass through. No C++ exception leaves the scripting subsystem.
3. **Sandbox:** libraries `base`, `string`, `table`, `math` and `utf8` are opened; `string.dump` is removed. The string metatable gets `__metatable = false`, so scripts cannot reach the shared string table. Each script instance gets its own environment table (set as the chunk's `_ENV`) with:
   - `assert`, `error`, `getmetatable`, `ipairs`, `next`, `pairs`, `pcall`, `rawequal`, `rawget`, `rawlen`, `rawset`, `select`, `setmetatable`, `tonumber`, `tostring`, `type`, `xpcall`, `_VERSION`, `_G` (the environment itself);
   - copies of `string`, `table`, `math` and `utf8`, so one script changing `string.format` changes nothing for another;
   - its own `engine` table and `print`.

   Not available: `collectgarbage`, `dofile`, `load`, `loadfile`, `require`, `warn`, `coroutine`, `io`, `os`, `package`, `debug`, native modules. Chunks load as text only (mode `"t"`); bytecode is refused (`attempt to load a binary chunk (mode is 't')`). The string-hash seed is fixed, so a script iterates `pairs` the same way in every run.
4. **Limits** (`scripting::ScriptLimits`, checked by `validate`):
   - **source size:** at most 256 KiB by default (1 B to 16 MiB allowed);
   - **instructions:** at most 10 000 000 per call by default (at least 1000). A count hook checks every 1000 instructions. When the budget is used up, the hook switches to every instruction, so a script that catches the error with `pcall` cannot go on. Time spent inside one library function is not counted;
   - **memory:** 64 MiB by default (at least 256 KiB) for the whole VM, enforced by the VM's allocator. A request beyond it fails, and Lua reports "not enough memory".

   After `restart()` the closed VM must have returned every byte, which is checked.
5. **Script lifecycle:** a script returns a table with the optional functions `start(entity)` and `update(entity, dt)`.
   - `addScript(entity, name, source)` loads and runs the chunk;
   - `loadScripts(scene, registry)` does the same for every enabled `ScriptComponent`, resolving its Script asset and reading the file;
   - `start(scene)` calls `start` once per loaded instance;
   - `update(scene, dt)` calls `update` per started instance whose component is enabled. Both run in the scene's entity order.

   The application loop does not call scripts yet. There is no hot reload; `restart()` closes the VM and opens a new one, and scripts are loaded again.
6. **Failures:** a script whose loading, `start` or `update` fails is marked `Failed` with its message and is not called again until the runtime restarts. The message names the script and entity, e.g. `script 'scripts/door.lua' (entity …): update: scripts/door.lua:3: door jammed`, followed by a traceback. Its table is released. The pass goes on with the other scripts and returns the errors; the scene and the engine continue. An instance whose entity left the scene fails the same way. Failure state is runtime state, keyed by entity ID, and is never saved.
7. **Engine API** (an `engine` table of C closures; their only upvalue is the runtime):
   - `exists(id)`, `name(id)`;
   - `position(id)`, `setPosition(id, x, y, z)`, `rotation(id)`, `setRotation(id, w, x, y, z)`, `scale(id)`, `setScale(id, x, y, z)`;
   - `deltaTime()`.

   Each function checks the exact argument count ("engine.setPosition expects 4 arguments (id, x, y, z), got 3"). Argument types use Lua's own messages ("bad argument #2 to 'setPosition' (number expected, got string)"). Numbers outside the float range are rejected before conversion. IDs are parsed strictly.

   Every call looks the entity up in the scene again, so a missing or destroyed entity is a Lua error naming it. Writes go through `Scene::replaceComponent` and its validation, so a NaN or a zero scale is an error that changes nothing. The API exists only while `start` or `update` runs. `print` joins its arguments with tabs and goes to a handler, by default the engine log (category `script`).
8. **`ScriptComponent`:** `asset` (a Script asset, never null) and `enabled` (default true). In scene documents it is `"script": {"asset": <asset ID>, "enabled": true}`. This is added to format version 1, which is not released while M4 is unmerged (ADR 0023 Amendment 1). `findAssetReferenceProblems` and the scene document's registry check include it.
9. **Lockfile:**
   - One `conan.lock` at the repository root serves both profiles. It was created with `conan lock create` for Windows MSVC and Linux GCC, Debug and Release, each run adding to the same file; Conan computes the Linux graph on Windows without building anything.
   - It pins the versions and recipe revisions of every requirement and build requirement. Linux-only packages are `xorg/system`, `pkgconf`, `meson` and `ninja`.
   - `scripts/windows/configure.bat` and the Linux CI job pass `--lockfile=conan.lock` to `conan install`. A requirement missing from the lockfile is an error.
   - The CI caches are keyed by the lockfile too. README.md lists the commands that create it again.

## Consequences

- Lua is built from source once per configuration (cached by Conan and in CI).
- The unit tests link Lua to test the internal VM directly; public headers never include Lua headers (`tests/unit/scene/public_headers_test.cpp`).
- Scripts change transforms only; events, input, spawning or destroying entities, coroutines and a game update loop are later work.
- A pathological string pattern can still take long inside one `string.find` call; the budget does not cover time inside library functions.

## Revisit when

- Scripts need more of the engine (events, input, spawning): each addition extends the API and this ADR.
- Hot reload is designed: instances must be replaced without leaving a half-updated scene.
- Lua 5.5.1 or a newer recipe revision comes to Conan Center: update the requirement and the lockfile together.
