# M4 — Scene document and data model

- **Branch:** `milestone/m4-scene-data-model`, created from `main` at `0befa66` (M3 merged through pull request #4).
- **Roadmap entry:** `docs/ENGINE_PLAN.md` §10, M4; data model in §7, Lua in §8.
- **Workflow:** `docs/DEVELOPMENT_WORKFLOW.md`. One task implements exactly one sub-milestone below, then stops and reports.
- **Separate experiment:** the Raven UI evaluation for M5 (`docs/experiments/raven-ui-evaluation.md`) is not part of M4, not a dependency of any M4 step and not an M4 exit criterion. It ended on 2026-11-14 with a conditional pass on Windows and the proposed ADR 0019; its prototype stays on the branch `experiment/raven-ui-evaluation`.

## Goal and exit criterion

Up to M3 the engine generates and draws a procedural world, but it has no game objects of its own and nothing to save except world generation settings. M4 adds the data model of a game world: entities with persistent identity, components, scenes that are saved and loaded, assets referenced by persistent IDs, Lua scripts that change permitted properties through a narrow API, and prefabs that are instantiated into scenes.

**Exit** (from `docs/ENGINE_PLAN.md`): save/load a scene without losing entity identity or component values; a Lua script can alter a permitted property and errors are contained. Spelled out for this plan:

1. Entities have persistent IDs that survive save → load unchanged; runtime handles never leave their subsystem.
2. Components (name, transform, static mesh, camera, directional light, script, and the parent link of M4.6) are stored, validated, serialized and restored exactly.
3. Assets (meshes, textures, materials, scripts, prefabs) are identified by persistent asset IDs, separate from GPU handles, with project-relative paths that work on another machine.
4. A Lua script changes a permitted property of an entity; syntax errors, runtime errors, bad arguments, missing entities, missing scripts and endless loops are reported and contained without stopping the engine.
5. A prefab with several entities is created, saved, loaded and instantiated twice into a scene with new, unique entity IDs and its internal links intact.

M4 has no visible change: it adds no rendering of scenes and no editor UI (M5).

## Architecture principles

- **Subsystems of their own.** Scene (entities, components, documents, prefabs), Assets (IDs, paths, registry) and Scripting (Lua runtime, bindings) are separate domains with their own folders and namespaces inside the one `engine` static library (`docs/PROJECT_STRUCTURE.md`). The library is not split.
- **No graphics or platform dependency.** None of them includes renderer, OpenGL, SDL or Dear ImGui headers, and their headless tests need no window or GPU (AGENTS.md §3, §8).
- **WorldGen stays independent.** WorldGen does not depend on Scene, EnTT or Lua; Scene does not depend on WorldGen. The archipelago keeps working unchanged (M3 golden hashes stay valid).
- **The renderer draws; it does not store the world.** Components hold persistent IDs and plain values, never `MeshHandle`, `TextureHandle`, `MaterialHandle`, `GLuint` or pointers. Mapping scene data to renderer resources is later work (M5 viewport), behind the renderer API.
- **Persistent identity vs runtime handles.** Persistent IDs (entities, scenes, assets, prefab-local IDs) are project-owned value types, serialized as strings. Runtime handles (`entt::entity`, renderer handles, Lua references) are transient and never serialized or exposed as identity (AGENTS.md §3).
- **Explicit ownership, no globals.** A `Scene` is a value owned by its caller; an asset registry and a script runtime are objects owned by their caller. No singletons, no global registries. Each is used from one thread (the thread that owns it); none is thread-safe, and that is documented.
- **Errors as values.** Public functions return `core::Result<T>` or `std::optional<core::Error>`; nothing throws across a subsystem boundary, the Lua C callbacks included (AGENTS.md §4). Messages say what happened, which object is affected and which ID or path is wrong.
- **Data is not code.** Scenes, prefabs and the asset manifest are versioned JSON documents (ADR 0004); Lua adds behavior only.
- **Validate before mutating.** Loading parses into plain data, validates everything, and only then builds the result; a broken file never damages an existing scene. Instantiation and loading are all-or-nothing.
- **Untrusted input.** Scene, prefab, manifest and script files may come from outside the project: limits on sizes and counts, no unrestricted file, process or native-module access from Lua (AGENTS.md §5).

## Technology stack and dependencies

| Technology | Status before M4 | Role in M4 |
|---|---|---|
| C++20, CMake, Conan 2, Catch2/CTest | adopted | unchanged; no C++23, no compiler extensions |
| GLM `glm/1.0.3` | adopted (ADR 0007) | transforms; M4.2 adds a quaternion alias to `engine/core/Math.hpp` |
| nlohmann_json `nlohmann_json/3.12.0` | adopted, private, only in `SettingsFile.cpp` (ADR 0004) | scene, prefab and asset manifest documents; no second JSON library |
| EnTT `entt/4.0.0` | candidate (ADR 0001) | adopted in M4.2 as the internal ECS storage, private |
| Lua `lua/5.5.0` | candidate (ADR 0001) | adopted in M4.5 as the script VM, private |
| LuaBridge3 | candidate (ADR 0001) | evaluated in M4.5 against a project-owned binding layer over the Lua C API |
| Dear ImGui, SDL3, OpenGL 4.5, GLAD | adopted | not used by M4 code; unchanged |

Package facts checked on 2026-11-14 with Conan 2.34.0 against Conan Center (`conan list`, `conan graph info` for the Windows profile); they are inputs for the ADRs of M4.2 and M4.5, which re-check and pin them:

- **`entt/4.0.0`** — the newest EnTT in Conan Center; recipe revision `ca14bea628dd1cfb1e8d444456de278c`; MIT; header-only (`header-library`, no options); the recipe requires C++20 for 4.x (`check_min_cppstd`); CMake package `EnTT`, target `EnTT::EnTT`. Compilation with MSVC and GCC 13 is proven only by the M4.2 build in CI.
- **`lua/5.5.0`** — the newest Lua in Conan Center (upstream 5.5.1 is not there, as ADR 0001 notes); recipe revision `2bac82d0b9a1744cfaf436c5c1d01bee`; MIT; static library compiled as C by default (`compile_as_cpp=False`, `with_tools=False`, `with_readline=False`); no prebuilt binary for the project's Windows profile, so `--build=missing` builds it from source (like `imgui`); on Linux the package defines `LUA_USE_DLOPEN` and `LUA_USE_POSIX` and links `dl` and `m`, so native module loading exists in the library and must stay unreachable from scripts.
- **LuaBridge3** — not in Conan Center (`luabridge*`, `luabridge3*`: no recipes). Its README (read on 2026-11-14) states MIT, header-only, C++17, tests with PUC-Lua 5.5.0, and work with or without C++ exceptions; the newest release is the pre-release `3.0-rc13`, and the only package manager it documents is vcpkg. Adopting it would mean vendoring a pinned tag under `third_party/` (`docs/TECH_STACK.md` §6).
- **sol2** `sol2/3.5.0` is in Conan Center but not a candidate of the plan; it is listed in the M4.5 ADR only as a considered alternative.
- **Lockfile:** the repository has no `conan.lock` although `docs/TECH_STACK.md` §7 and `docs/PROJECT_STRUCTURE.md` list one; CI and the wrappers run `conan install` without a lockfile. M4.5 creates it once EnTT and Lua are in (decided, see *Decisions on the open questions*).

Every new dependency follows AGENTS.md §7: purpose, exact version and recipe revision, license, platforms, why the standard library and current dependencies are not enough, a row in `docs/references/THIRD_PARTY_NOTICES.md`, and an ADR.

## Module structure

Planned layout; final file names are chosen in the step that creates them, following the existing conventions (one header per concept, `PascalCase.hpp`, namespace per folder):

```text
engine/include/engine/
  core/        Uuid-like 128-bit value type and UTF-8 checks (M4.1), atomic file write (M4.4)
  scene/       EntityId, SceneId, Scene (M4.1); components (M4.2, M4.3, M4.5, M4.6);
               scene documents (M4.4); prefabs (M4.6)
  assets/      AssetId, AssetType, AssetPath, AssetRegistry, asset manifest (M4.3)
  scripting/   script runtime facade, ScriptComponent (M4.5)
engine/src/
  scene/       EnTT used only here (M4.2); nlohmann_json in the document sources (M4.4, M4.6)
  assets/      nlohmann_json in the manifest source (M4.3)
  scripting/lua/  Lua headers used only here (M4.5)
tests/unit/scene/ · tests/unit/assets/ · tests/unit/scripting/ · tests/integration/
```

Dependency direction inside the engine (arrows: "uses"):

```text
scripting ──▶ scene ──▶ assets ──▶ core
                 │                   ▲
                 └───────────────────┘
worldgen ──▶ geometry ──▶ core          (unchanged; no scene, assets or scripting)
render, platform, app, debug            (unchanged; M4 code uses none of them)
```

- `assets` depends on `core` only. `scene` depends on `core` and `assets` (asset IDs in components, the registry when checking references). `scripting` depends on `scene`, `assets` and `core`.
- EnTT, nlohmann_json and Lua are linked `PRIVATE`; their types never appear in public headers (as for nlohmann_json in ADR 0004 rule 7).

## Persistent identity model

| Identity | Type | Persistent | Scope | Introduced |
|---|---|---|---|---|
| Entity | `EntityId` | yes, serialized | unique within a scene | M4.1 |
| Scene | `SceneId` | yes, serialized | unique within a project | M4.1 |
| Asset | `AssetId` | yes, serialized in scenes, prefabs and the manifest | unique within a project | M4.3 |
| Entity inside a prefab | prefab-local ID | yes, inside the prefab document only | unique within one prefab | M4.6 |
| ECS entity | `entt::entity` | no, internal to `engine/src/scene/` | one `Scene` object | M4.2 |
| GPU resource | `MeshHandle`, `TextureHandle`, `MaterialHandle` | no (ADR 0007, 0009, 0010) | one renderer | M1–M2 |
| Lua value | registry references, userdata | no, internal to `engine/src/scripting/lua/` | one VM | M4.5 |

EntityId, SceneId and AssetId are distinct C++ types: one cannot be passed where another is expected. None of them is derived from a world seed or from the `RandomStream`s of ADR 0003, and generating one never consumes world generation randomness.

## Status

| ID | Title | Status |
|---|---|---|
| M4.1 | Scene core and persistent identifiers | done |
| M4.2 | EnTT and entity components | done |
| M4.3 | Asset identity and asset registry | in progress |
| M4.4 | Scene documents as versioned JSON | planned |
| M4.5 | Lua runtime and script component | planned |
| M4.6 | Prefab model | planned |
| M4.7 | Integration, final verification and pull request | planned |

Status values: `planned`; `in progress` (set when work starts and committed with the change); `done` (set by a separate `docs(milestones): mark M4.x done (M4.x)` commit after the change is pushed and its CI is green, `docs/DEVELOPMENT_WORKFLOW.md` §4). `git log --grep "(M4.x)"` finds the commits.

## Rules for all of M4

- Scene, Assets and Scripting include no OpenGL, GLAD, SDL or Dear ImGui header, and no public engine header includes `entt/`, `nlohmann/` or `lua.h`. A test or a `grep` in each step's review checks it.
- WorldGen, the renderer, the platform layer, the application loop and island-preview are not changed, except where a step names a shared helper it moves (then the M3 results stay bit for bit the same and their tests prove it).
- Components contain persistent IDs and plain values only; every component type has a GPU-free `validate` with messages that name the field, and the scene only ever holds valid component values.
- Documents follow ADR 0004: an envelope with `format` and `formatVersion`, 64-bit and larger IDs as strings, floats that read back bit for bit, non-throwing parsing, errors with the field path, explicit migrations, no library types in public headers. Each document kind gets its own format id and schema decision.
- Every new public type and function gets headless unit tests for normal, boundary and failure cases; loaders get round-trip, missing-field, unknown-field, wrong-type, malformed-input, newer-version and limit tests.
- New dependencies only in the steps that name them: EnTT in M4.2, Lua (and, if chosen, LuaBridge3) in M4.5.
- Decisions about dependencies, public APIs and file formats get an ADR in the sub-milestone that makes them, numbered when written (the next free number is 0023; 0019 is the proposed editor UI decision of the Raven evaluation, 0020 to 0022 come from M4.1 to M4.3).
- The editor, the Scene → renderer bridge and everything in *Out of scope* stay out, even where an API would make them easy.
- No GPU tier is required for M4.1–M4.6 (no rendering, shader, windowing or loop code changes); the final step runs it as `docs/DEVELOPMENT_WORKFLOW.md` §8 requires. A step that unexpectedly touches such code runs it too.

## Sub-milestones

### M4.1 — Scene core and persistent identifiers

**Goal:** a scene that creates, finds and destroys entities identified by persistent IDs.

**Scope:**
- An ID format decided by an ADR after the analysis below; a value type for it in `core` (the representation shared by `EntityId` and `SceneId` now and `AssetId` in M4.3), with formatting, strict parsing, a null value and a generator.
- `EntityId` and `SceneId` in `engine/include/engine/scene/`, distinct types over that representation.
- `Scene` in `engine/include/engine/scene/Scene.hpp` and `engine/src/scene/Scene.cpp`, namespace `engine::scene`: constructed with a `SceneId` and a name (or with a generated ID); move-only or copyable as the step decides and documents; destroyed by its owner.
- Operations: create an entity with a generated ID; create an entity with a given ID (loading, later undo); destroy; `contains`; the list of entity IDs in creation order (a deterministic order, needed by documents); entity count; `clear`; the scene's ID and name.
- Storage is the simplest structure that meets the operations (for example a vector in creation order plus an ordered or hashed index by ID); M4.2 replaces it with EnTT behind the same API.
- Scene names: valid UTF-8, not empty, no control characters, with a documented byte limit; a small UTF-8 validation function in `core`, which M4.2 reuses for entity names.
- Errors through `core::Result` / `std::optional<core::Error>`, e.g. `scene 'Main': entity 3f2a…: already exists`.

**Decisions to make (ADR "Persistent identifiers"):** options to compare before choosing:

| Option | Generation | Collision risk | Text form | Remarks |
|---|---|---|---|---|
| Counter per scene | next value | none inside one scene; collisions when entities move between documents (merged scenes, prefab instances, copy and paste) | short | depends on the document it was created in |
| Random 64-bit | seeded generator | about n²/2^65: 2.7·10⁻⁸ for 10⁶ entities | 16 hex digits | adequate for one scene, weaker across a whole project |
| Random 128-bit, UUID version 4 layout (122 random bits) | seeded generator | about n²/2^123: below 10⁻²⁵ for 10⁶ entities | canonical 36-character form | recognized by tools; the plan's candidate |
| Hash of name or path | derived | changes when the object is renamed | — | not an identity; rejected |

The ADR fixes: size; generation (where the entropy comes from — never the world seed, worldgen streams or `std::hash`; an injectable generator with a fixed seed for tests); the text form and how strictly it is parsed (case, hyphens, length); the null value; uniqueness rules (a duplicate is an error naming the ID; a generated duplicate is drawn again, with a bound); whether the ID of a destroyed entity may be used again (only through explicit creation with that ID); tests.

**Lifecycle rules:** the public API identifies entities only by `EntityId`; there is no long-lived pointer or reference into scene storage, so a destroyed entity cannot be reached through stale data. Every operation on an ID the scene does not contain returns an error naming the ID; `contains` is false after `destroyEntity` and after `clear`. Data attached to an entity is removed with it (from M4.2 on: its components). `clear` keeps the scene's ID and name.

**Decisions taken while implementing** (ADR 0020):
- ID: 128-bit random IDs in the RFC 9562 version 4 layout. `engine/core/Uuid.hpp`: `core::Uuid` (`high`, `low`, ordered as one big-endian number, all zero is null), `toString` (lower case, canonical 36 characters), `parseUuid(text, kind)` (strict length, hyphens and hex digits, either case, any version; errors with the position and at most 40 quoted bytes, cut at a character boundary), `UuidGenerator` (xoshiro256** with a 256-bit state; `UuidGenerator(seed)` expands the seed with SplitMix64 for tests, `fromEntropy()` uses `std::random_device` and falls back to the clocks), `TypedUuid<Tag>` (one type per kind of object; `parse` rejects the null ID and names the kind).
- `engine/scene/EntityId.hpp` and `engine/scene/SceneId.hpp`: `EntityId` and `SceneId` as `TypedUuid`s with the kinds "entity ID" and "scene ID".
- `engine/core/Utf8.hpp`: `decodeUtf8At` and `findInvalidUtf8` (RFC 3629: no overlong forms, surrogates or values above U+10FFFF).
- `engine/scene/Scene.hpp`: `Scene::create(sceneId, name, generator)` and `Scene::create(name, generator)` (the scene's ID drawn first, the entity IDs after it from the same generator, which the scene keeps); `createEntity()`, `createEntity(id)`, `destroyEntity`, `contains`, `findEntity` (nothing, or the error naming the scene and the ID), `entityIds()` in creation order, `entityCount`, `clear`. Move-only; a moved-from scene has a null ID, an empty name and no entities. Not thread-safe.
- Storage: two ordered maps, ID → creation sequence and sequence → ID, so lookups, destruction and listing in creation order are all O(log n); an ID created again after its destruction goes to the end.
- Names: `validateName(name, what)`: not empty, at most 256 bytes, valid UTF-8, no control characters (U+0000–U+001F, U+007F–U+009F); messages start with `what` (`scene name: invalid UTF-8 at byte 2`).
- Generated IDs that are already in the scene are drawn again, at most 16 times (`kMaxIdAttempts`); a generated ID can never be null (the version bits).

Implemented tests: `tests/unit/core/uuid_test.cpp` (golden IDs of three seeds from an independent Python implementation; version and variant bits over 10 000 IDs; 200 000 unique IDs and distinct first IDs of 1000 seeds; copied generators repeat; entropy generators differ; text round trip in both cases, the null and the all-ones ID, other versions readable; big-endian order; every parse error with its message; long and multi-byte input quoted within 40 bytes as valid UTF-8; typed IDs not convertible into each other or from integers and pointers, parse rejecting null), `tests/unit/core/utf8_test.cpp` (valid text up to U+10FFFF; 15 invalid sequences at their offsets; decoding step by step), `tests/unit/scene/scene_test.cpp` (ID types; scene ID, name and generator; null scene ID; every name rule with its message; create, find, destroy; unknown and null IDs; given IDs once; a taken generated ID drawn again; a destroyed ID back only by explicit creation; creation order across destroys; `clear`; moving; the same ID in two scenes; 100 000 entities created, half destroyed in order, the rest destroyed).

**Out of scope:** components (M4.2), EnTT (M4.2), serialization (M4.4), hierarchy (M4.6), multiple scenes managed by a registry.

**Acceptance criteria:** the scene works without a GPU or window; every entity has a persistent ID; no raw pointer or memory address is used as identity; the API names no SDL, OpenGL or EnTT type; the tests below pass on all four CI jobs.

**Unit tests (headless, `tests/unit/scene/`, `tests/unit/core/`):** ID generation (fixed-seed golden values, the version and variant bits if the UUID layout is chosen); uniqueness over a large sample; text round trip, parsing errors (wrong length, characters, hyphens, null); ordering and equality; scene creation with a given and a generated ID; scene names (valid, empty, invalid UTF-8, control characters, at and above the limit); create, find, `contains`, destroy; destroying or finding an ID the scene does not contain; a duplicate ID; operations on an ID after its entity was destroyed; `clear`; creation order kept across destroys; 100 000 entities created and destroyed; a moved-from scene's documented state.

**Integration tests:** none (no subsystem boundary yet).

**Failure cases:** duplicate entity ID; unknown or destroyed entity ID; null ID; malformed ID text; invalid scene name.

**Done when:** the ADR is accepted and the tests pass in CI.

**Visual check:** none.

### M4.2 — EnTT and entity components

**Goal:** components on entities, stored in EnTT behind the scene's own API.

**Scope:**
- `entt/4.0.0` added to `conanfile.py` with a comment, `find_package(EnTT)`, linked `PRIVATE`; `docs/references/THIRD_PARTY_NOTICES.md`, `docs/TECH_STACK.md` (§4) and ADR 0001 (*Verification*) updated.
- `Scene` stores its entities in an `entt::registry` inside its implementation (pimpl or equivalent), keeping the M4.1 API and tests unchanged. The mapping `EntityId → entt::entity` is internal; `entt::entity` is never returned, serialized or used as identity.
- Component operations: add, remove, has, read, replace (validated), and the entities having given components. There is no generic component manager on top of EnTT and no public access to the registry.
- Values enter the scene only through validated operations (no mutable references that could bypass validation); reads return values or const access that cannot outlive the operation's documented validity.
- Enumeration returns entity IDs in scene order (creation order), not in EnTT pool order, so results and documents are deterministic.
- Components (plain structs, each with `validate` naming the field):
  - `NameComponent`: a UTF-8 name; not empty, valid UTF-8, no control characters, a byte limit (the M4.1 rules).
  - `TransformComponent`: position `Vec3`, rotation as a quaternion (an alias `math::Quat` added to `engine/core/Math.hpp`), scale `Vec3`; finite values; a documented quaternion policy (accept lengths within a tolerance of 1 and store normalized, reject zero or far-off lengths); positive scale components above a documented minimum (negative scale would flip triangle winding, which the renderer culls; revisited with an ADR when mirroring is needed); a position limit.
  - `CameraComponent`: vertical field of view, near and far plane, with the ranges `camera::projectionMatrix` makes usable (field of view in [1°, 179°], near ≥ 0.001, far > near), all finite; orientation comes from the transform.
  - `DirectionalLightComponent`: direction (finite, not zero, stored normalized), RGB color (finite, ≥ 0, display space as in ADR 0009/0011) and intensity (finite, ≥ 0). It maps onto `render::LightDesc` as color × intensity; the renderer has one directional light and no point lights, and no other light kinds are added.
- `StaticMeshComponent` is **not** in this step: it refers to asset IDs, which M4.3 introduces (see M4.3).

**Decisions to make (ADR "EnTT as the internal ECS"):** exact package and recipe revision; private linking and how the public API avoids EnTT types (pimpl; component functions declared in the header and instantiated for the supported component set in the implementation, or named functions per component); the component access contract; scene order for enumeration; what happens to components when an entity is destroyed; the numeric limits above.

**Decisions taken while implementing** (ADR 0021):
- Package `entt/4.0.0`, recipe revision `ca14bea628dd1cfb1e8d444456de278c`, MIT, header-only; `find_package(EnTT)`, `EnTT::EnTT` linked `PRIVATE`, included only in `engine/src/scene/Scene.cpp`.
- `Scene` keeps a pointer to a private `Scene::Storage` (the `entt::registry` and an ordered `EntityId → entt::entity` map); every EnTT entity carries an internal record with its `EntityId` and creation sequence. The M4.1 API and its tests are unchanged; `clear()` releases the storage, a moved-from scene has none.
- Components in `engine/scene/{Name,Transform,Camera,DirectionalLight}Component.hpp`: plain structs with `kTypeName`, the limits as `static constexpr` members, defaulted `==` and `validate` (in `engine/src/scene/Components.cpp`); messages `<type> component: <field> is <value>, <rule>`, numbers in plain notation where exact, otherwise the shortest exact form.
- The concept `SceneComponent` lists the four types; `addComponent`, `replaceComponent`, `removeComponent`, `hasComponent`, `component` (a copy) and `entitiesWith<T, U...>()` are templates constrained by it, explicitly instantiated in `Scene.cpp`.
- Adding a component the entity already has is an error; replacing or removing one it does not have is an error; every failure changes nothing.
- Queries collect the first type's pool, sort by creation sequence and filter by the other types: creation order, independent of EnTT's order.
- Transform: position within ±10⁵; rotation finite with a length within 10⁻³ of 1; scale in [10⁻⁴, 10⁴]. Camera: field of view in [1°, 179°], near ≥ 0.001, far > near × 1.001 (the ranges `camera::projectionMatrix` uses unchanged). Light: direction finite and not zero, color and intensity ≥ 0, color × intensity finite. Rotations and light directions are stored normalized (in double); values already within 10⁻⁶ of unit length stay bit for bit, so storing a stored value again changes nothing.
- `math::Quat` (= `glm::quat`, constructor (w, x, y, z)) added to `engine/core/Math.hpp`.

Implemented tests: `tests/unit/scene/components_test.cpp` (defaults valid, the default transform is the identity, the camera defaults equal `camera::Camera`'s; every rule of every field with NaN, ±Inf, zero, negative, below and above the limits and its message; huge finite rotations; the extreme valid lenses give exactly `glm::perspective`'s matrix through `camera::projectionMatrix`), `tests/unit/scene/scene_components_test.cpp` (for each of the four types: add, read, replace, remove; a second add, replace or remove of a missing one; invalid values rejected with the field named and the stored value unchanged; unknown, null and destroyed entities for every operation; then several components on one entity; queries by one and two components in creation order across removals, destroys and an ID created again; components gone with their entity and after `clear`; moving a scene; normalized rotations and directions over 10 000 random values each, stored again bit for bit; 100 000 entities with components created, queried and destroyed), `tests/unit/scene/public_headers_test.cpp` (no public header under `engine/include/` includes EnTT or nlohmann_json). The M4.1 tests run unchanged.

**Out of scope:** `StaticMeshComponent` (M4.3), `ScriptComponent` (M4.5), parent links (M4.6), systems and update loops, render extraction, component reflection or automatic serialization.

**Acceptance criteria:** the ECS works headless; EnTT details are isolated (no EnTT include in public headers, no `entt::` in public signatures); every component value in a scene passes its `validate`; the M4.1 tests still pass unchanged.

**Unit tests:** add, read, replace and remove each component; several components on one entity; queries by one and by two components in scene order; destroying an entity removes its components and leaves no data reachable through its ID; a destroyed and re-created ID starts without components; invalid values for every field of every component (NaN, ±Inf, zero, negative, out of range, invalid UTF-8, empty names) rejected with the field named and the stored value unchanged; quaternion normalization within tolerance; 100 000 entities with components created, queried and destroyed; the `EntityId` stays the same while components are added and removed; a public-header check that no EnTT header is included.

**Integration tests:** none.

**Failure cases:** a component operation on an unknown or destroyed entity; adding a component the entity already has (policy: error or replace, decided in the step); invalid component data.

**Done when:** components work through the scene API with EnTT inside, and CI is green on all four jobs.

**Visual check:** none.

### M4.3 — Asset identity and asset registry

**Goal:** every asset has a persistent ID that scenes and prefabs can store, separate from runtime resources.

**Scope:**
- `AssetId` in `engine/include/engine/assets/`, over the M4.1 representation, a type distinct from `EntityId`, `entt::entity`, renderer handles and pointers.
- `AssetType`: `Mesh`, `Texture`, `Material`, `Script`, `Prefab`, each with a fixed name in documents.
- Asset paths: project-relative, normalized (`/` separators, no `.` or empty segments), never absolute, never a drive letter, UNC path, user folder or temporary folder, never leaving the project root through `..`; a documented character and length policy; paths that differ only in letter case count as a conflict, so a project behaves the same on Windows and Linux.
- `AssetMetadata`: ID, type, path, source name, an import/format version where a type needs one, availability (present or missing on disk).
- `AssetRegistry`, owned by its caller, with a project root given at runtime (never serialized): register; unregister; find by ID; find by path; check the expected type; check that files exist; read metadata; reject duplicate IDs and duplicate paths with messages naming both.
- An asset manifest, so asset IDs survive restarts and moving the project: one versioned JSON document (`format` id, `formatVersion` 1, ADR 0004 rules) listing every asset's ID, type and path, saved and loaded through the registry. (Sidecar files per asset are the alternative the ADR compares.)
- `StaticMeshComponent` (moved here from M4.2): the `AssetId` of a mesh and of a material; null allowed for "not set" if the ADR says so; no GPU handle. A function reports the references of a scene that the registry does not know or that have the wrong type.
- The chain `AssetId → registry → path → existing CPU loaders` is proven in tests for meshes (`geometry::readObj`) and textures (`core::readPng`); turning loaded data into GPU resources stays with the renderer and is not part of M4.
- Material assets have an ID, a type and a path, but no file format or loader: material files belong to M6 (ADR 0010). Script and prefab files are read by M4.5 and M4.6.

**Decisions to make (ADR "Asset identity and registry"):** manifest vs sidecar files; the path normalization and rejection rules; the case-conflict policy; type checks by extension; what "missing" means (error or a state); the manifest schema and limits; the null asset ID.

**Decisions taken while implementing** (ADR 0022):
- `engine/assets/AssetId.hpp`: `assets::AssetId`, a `TypedUuid` with the kind "asset ID"; the null ID refers to no asset and is never registered.
- `engine/assets/AssetType.hpp`: `mesh`, `texture`, `material`, `script`, `prefab`; extensions compared ignoring case: `.obj`, `.png`, `.lua`; materials and prefabs any extension until their formats exist (M6, M4.6).
- `engine/assets/AssetPath.hpp`: printable ASCII only, at most 256 bytes; `/` separators, empty and `.` segments dropped, `..` resolved and never above the root; rejected with their own messages: absolute, drive-letter and UNC paths, a leading `~`, `\` (not normalized: on Linux it is a file name character), `< > : " | ? *`, segments ending in `.` or a space, Windows device names. `foldCase` (ASCII lower case) for case comparisons.
- `engine/assets/AssetRegistry.hpp`: `registerAsset`, `unregisterAsset`, `contains`, `find` (a copy of `AssetMetadata`), `findByPath`, `checkType`, `resolve`, `refreshAvailability`, `assets()` in folded path order. Conflicts: duplicate IDs and paths, files or folders that differ only in letter case, a file where a registered folder is and the reverse; all errors name both assets.
- Availability is a state (`Present`, `Missing`), set on registration and by `refreshAvailability`; a file is present only with exactly the path's spelling, checked folder by folder, so a wrong letter case is missing on Windows as on Linux. `resolve` checks again and names the spelling found on disk.
- `AssetMetadata` has the ID, type, path, availability and the file name as source name; no format version: versions live inside the asset files (ADR 0004 envelopes), so a copy in the metadata could disagree with the file.
- `engine/assets/AssetManifest.hpp`: one manifest per project (not sidecars), `lpe.assets.manifest` version 1, entries `id`, `type`, `path` (all required) in path order; unknown fields warned about; at most 100 000 entries and 64 MiB; loading is all or nothing into a new registry; missing files are not errors. nlohmann_json in `engine/src/assets/AssetManifest.cpp` only; its malformed-JSON finder and value descriptions repeat those of `worldgen/SettingsFile.cpp` (shared helpers are M4.4's).
- `engine/scene/StaticMeshComponent.hpp`: `mesh` (never null) and `material` (null: the renderer's default material); `engine/scene/AssetReferences.hpp`: `findAssetReferenceProblems(scene, registry)` lists unregistered and wrongly typed references in entity order.

Implemented tests: `tests/unit/assets/asset_id_test.cpp` (no conversion between asset IDs and renderer handles, entity or scene IDs, integers or pointers; text form; type names and extensions), `tests/unit/assets/asset_path_test.cpp` (normalization; every rejection rule with its message), `tests/unit/assets/asset_registry_test.cpp` (register and find by ID and path; duplicate IDs and paths; letter-case conflicts of files and folders, also after unregistering; extensions; unregistering; type checks; missing files, folders in place of files, refreshing; a wrong letter case on disk; two project roots; the committed lighthouse mesh and textures resolved and read by `geometry::readObj` and `core::readPng`), `tests/unit/assets/asset_manifest_test.cpp` (exact text; round trip; no list and unknown fields; envelope errors including malformed JSON with line and column and a newer version; every entry field; duplicate IDs, paths and case conflicts in the file; 100 001 entries; files saved, loaded, missing, broken and above 64 MiB), `tests/unit/scene/asset_references_test.cpp`, the static mesh component in `tests/unit/scene/components_test.cpp` and `tests/unit/scene/scene_components_test.cpp`, and the integration test `tests/integration/asset_project_test.cpp` (manifest saved, project folder moved and renamed, the same IDs resolve to the moved files, which the CPU loaders read; the manifest holds no machine path).

**Out of scope:** an asset database, import pipelines, caching, hot reload, an asset browser, glTF (M6), material files (M6), GPU resource caches.

**Acceptance criteria:** all assets have persistent IDs that can be stored in a scene; paths in the manifest contain nothing machine-specific; asset IDs and GPU handles are separate types with no conversion between them.

**Unit tests (`tests/unit/assets/`):** register and find by ID and by path; registering the same ID or path again; unregistering; a type mismatch; a missing file; invalid paths (absolute, drive letter, UNC, backslashes normalized or rejected as decided, `..` escaping the root, empty, too long, control characters); paths differing only in case; a project root moved to another folder resolves the same assets; manifest round trip, missing fields, unknown fields, wrong types, malformed JSON, newer version, duplicate IDs in the file; a static mesh referring to an unknown or wrongly typed asset is reported; mesh and texture IDs resolved and loaded through the existing CPU loaders from fixture files; `AssetId` is not constructible from or convertible to a renderer handle (compile-time checks).

**Integration tests:** a registry over a temporary project folder: manifest saved, folder moved, manifest loaded, the same IDs resolve to the moved files.

**Failure cases:** duplicate asset ID; duplicate or conflicting path; wrong asset type; missing file; invalid or non-portable path; broken manifest; reference to an unregistered asset.

**Done when:** asset IDs, paths, the registry and the manifest pass their tests in CI, and scenes can refer to meshes and materials by asset ID.

**Visual check:** none.

### M4.4 — Scene documents as versioned JSON

**Goal:** save a scene to a file and load it back logically identical.

**Scope:**
- A versioned scene document (ADR 0004 envelope with its own `format` id, `formatVersion` 1): scene ID, scene name, entities in scene order with their IDs and components, asset references by asset ID, and the metadata the ADR names. The format is documented field by field and extensible (new components and fields without breaking version 1 readers' rules).
- For each component (name, transform, static mesh, camera, directional light): its JSON schema, required fields, defaults, validation (the M4.2/M4.3 `validate`), and the handling of missing data. Serialization is written per component; nothing serializes C++ memory layout automatically. Never serialized: pointers, EnTT IDs, GPU handles, SDL or OpenGL objects, renderer state.
- `nlohmann_json` reused (no second JSON library), included only in the scene document source. The float writer that reads back bit for bit (ADR 0004 Amendment 1) is shared with the world settings file through an internal helper instead of being copied; the M3 settings files stay byte for byte the same (their tests prove it).
- Saving: valid UTF-8, the path checked, clear write errors, and no half-written document on failure as far as the file system allows: written to a temporary file in the target folder and moved over the target (a `core` helper). All persistent IDs and asset references are written.
- Loading in stages: read (with a size limit) → parse without exceptions → check the envelope and `formatVersion` → check structure, types, limits (entities, string lengths), IDs (format, duplicates), components and numbers → check asset references against a registry → build a new `Scene`. The caller's scene is replaced only by a successfully loaded one; a broken file leaves it untouched.
- Versioning: the current version; missing and unsupported versions are errors; a migration hook `vN → vN+1` applied before fields are read (ADR 0004 rule 5), with no migrations to write yet.
- Errors are returned as `core::Error` with the field path (e.g. `entities[3].components.transform.scale.y: must be > 0`) and logged by the caller.

**Decisions to make (ADR "Scene document format"):** format id and layout; ID text form in documents; unknown fields and unknown component types (warning and dropped, error, or kept — dropping loses data on the next save, so the ADR must choose consciously); unknown asset references (error, or a warning with the reference kept unchanged — never silently replaced); limits; whether components may be omitted vs written with defaults; the atomic-write behavior on Windows and Linux.

**Out of scope:** binary formats, scene streaming, linking a scene to world generation settings (M5), prefab documents (M4.6), script components (M4.5).

**Acceptance criteria:** a scene after save and load is logically identical to the original; loading never creates new IDs in place of saved ones; a failed load or save leaves the existing scene and the existing file intact.

**Unit tests:** round trip Scene → JSON → Scene comparing scene ID, entity IDs and order, entity count, component sets, transform values bit for bit, names (including non-ASCII UTF-8), asset IDs, camera and light settings; a document written twice is byte for byte the same; every failure case below with its message and path; limits at and above their value; a newer and a missing `formatVersion`; the migration hook with a test-only version.

**Integration tests (`tests/integration/`, headless):** create a scene with a registry, save to a temporary folder, destroy the runtime scene, load, compare; a save into a read-only or missing location fails and leaves the previous file unchanged; a broken file next to a loaded scene leaves that scene unchanged.

**Failure cases:** empty file; malformed JSON (line and column); wrong types; unknown version; missing required fields; duplicate entity ID; invalid transform; unknown or wrongly typed asset ID; invalid component data; a broken or non-portable path; an unreadable file; a failed write.

**Done when:** round trips and every failure case pass in CI, and the scene document format is recorded in its ADR.

**Visual check:** none.

### M4.5 — Lua runtime and script component

**Goal:** Lua scripts that change permitted entity properties through a narrow API, with every error contained.

**Scope:**
- Dependency research recorded in an ADR before code: `lua/5.5.0` from Conan Center (exact recipe revision, options, MSVC and GCC builds, license); compiled as C (errors by `longjmp`: no C++ object with a non-trivial destructor may be alive in a frame a Lua error jumps over) or with `compile_as_cpp` (errors as C++ exceptions inside Lua) — the ADR chooses and states the rule for the bindings; the Lua 5.5 C API functions the bindings use, checked against the 5.5 reference manual rather than assumed from 5.4.
- Binding layer: LuaBridge3 (vendored pinned tag, pre-release, C++17, claims 5.5.0 support) or a small project-owned layer over the official C API. The plan's default is the project-owned layer: the API is a handful of functions, it needs no vendored pre-release, and it keeps error handling explicit. The ADR records the comparison and the choice.
- `engine/include/engine/scripting/` (facade, no Lua types) and `engine/src/scripting/lua/` (all Lua code); Lua linked `PRIVATE`. One VM per script runtime object; no second VM without a concrete need.
- Runtime lifecycle: create and open the allowed libraries; load a script from a Script asset (text chunks only, never precompiled bytecode; size limit); run it; call its documented entry points; report errors; close the VM and free everything.
- Sandbox: only base functions without file loading, `string`, `table`, `math`, `utf8` (the ADR lists exactly what is open); no `io`, `os`, `package`/`require`, `debug`, `loadfile`, `dofile`, native modules or `collectgarbage` options that could stall the host; `print` goes to the engine log. Each script instance gets its own environment table.
- Limits: an instruction budget per call through a count hook, so an endless loop ends with an error; a memory limit through the VM's allocator; both values documented and tested.
- Engine API for Lua (small and documented): find an entity by its ID; read its name; read its transform; change permitted transform values (validated like any other write); the delta time of the current call; a log function. Lua receives IDs (as strings or as userdata holding the ID), never pointers, GPU handles, OpenGL functions, SDL objects, EnTT handles or other C++ objects. Every call re-checks the entity in the scene, so a destroyed entity gives a Lua error, not a crash.
- Minimal script lifecycle: a script returns a table with optional `start(entity)` and `update(entity, dt)`; the runtime runs `start` once and `update` when its owner calls it (tests call it directly). The application loop is not changed: a game update system waits until the application lifecycle for it is designed.
- `ScriptComponent`: the `AssetId` of a Script asset and `enabled`; the runtime error state lives in the runtime, keyed by entity ID, and is not serialized. The scene document (M4.4) gains the `script` component; the ADR of M4.4 records the addition (format version 1 is still unreleased while M4 is not merged).
- Error isolation: every entry from C++ into Lua is a protected call with a message handler that adds the script name and line; no C++ exception crosses into or out of Lua; a failing script instance is marked failed with its message and stops being called, while the scene, other scripts and the engine continue.
- Conan lockfile (approved 2026-11-14), after EnTT and Lua are in and the M4 dependency graph is final: create `conan.lock`; check that it serves Windows MSVC and Linux GCC, one lockfile for both profiles if Conan allows it, otherwise one per platform with the reason documented; use it in CI and in the Windows wrappers (`scripts/windows/`), so that every build resolves the same versions and recipe revisions; update `docs/TECH_STACK.md` §7, `docs/PROJECT_STRUCTURE.md`, `README.md` and the ADR.

**Decisions to make (ADR "Lua runtime and bindings"):** package, version and options; C or C++ error mechanism; binding layer; opened libraries; budgets; the Lua API surface and its error messages; the script entry points; how instances are reset or reloaded (no hot reload in M4, but restarting a runtime is supported and tested); the lockfile layout (one or per platform) and how CI and the wrappers pass it to `conan install`.

**Out of scope:** hot reload, events and subscriptions, input access from Lua, spawning or destroying entities from Lua, coroutine scheduling, a game update loop in the application, debugging tools, a second VM.

**Acceptance criteria:** a Lua script changes a permitted property of an entity; script errors stay inside the scripting subsystem and never terminate the engine; no Lua type appears in public headers; the sandbox exposes nothing outside the documented list.

**Unit tests (`tests/unit/scripting/`):** VM start and close (allocator shows all memory returned); a simple script runs; a script moves an entity (transform changed, validation applied); reading a transform and a name; delta time; syntax error with script name and line; runtime error with traceback; access to a missing entity; wrong argument types and counts; invalid values (NaN position) rejected; a missing Script asset; an endless loop stopped by the budget; a memory-hungry script stopped by the limit; forbidden libraries absent (`io`, `os`, `require`, `load` of bytecode); a failed instance does not stop others; running again after an error and after restarting the runtime; `ScriptComponent` validation and its round trip in the scene document.

**Integration tests:** a scene loaded from a document with script components, scripts loaded through the registry, `start` and several `update` calls, the resulting transforms saved and compared.

**Failure cases:** syntax error; runtime error; missing entity; destroyed entity during a call; wrong arguments; missing or wrongly typed script asset; script too large; endless execution; memory exhaustion; bytecode input.

**Done when:** the acceptance tests pass in CI on all four jobs, CI and the wrappers build from the lockfile, and the ADR records the runtime, binding and lockfile decisions.

**Visual check:** none.

### M4.6 — Prefab model

**Goal:** reusable groups of entities saved as prefab documents and instantiated into scenes.

**Scope:**
- `ParentComponent` (approved 2026-11-14), the entity reference that links inside a prefab need: the parent's `EntityId`, a relationship only. Rules: no self-parenting; no cycles; the parent exists; parent and child belong to the same scene; destroying a parent detaches its children (their `ParentComponent` is removed) and does not destroy them; the links are checked when a scene or prefab document is loaded and when a prefab is instantiated. The scene document gains the component. No transform inheritance in M4: hierarchical transforms get a milestone of their own.
- A prefab document (ADR 0004 envelope, its own `format` id, `formatVersion` 1): the prefab's asset ID (cross-checked with the registry), entities with prefab-local IDs unique within the document, their components, references to other assets by asset ID, references between its entities by local ID, minimal metadata.
- Creating a prefab from a set of scene entities: local IDs assigned, internal parent links kept, a reference to an entity outside the set rejected.
- Saving and loading prefab documents with the same rules as scene documents (staged validation, limits, atomic write).
- Instantiation into a scene: new `EntityId`s for every prefab entity, the local → new ID map applied to all internal references, components created with the same validation as everywhere else, nothing shared between instances (values are copied). All-or-nothing: on any error the scene is unchanged.
- The link from an instance to its prefab: the minimal model the ADR chooses, for example a component on each instance entity with the prefab's asset ID, an instance ID and the local ID, so a later milestone can update instances. Instances store their full component values in the scene; there are no overrides in M4.

**Decisions to make (ADR "Prefab model"):** how `ParentComponent` is stored and validated within the rules above; local ID form; the instance link; what a scene stores for an instance; limits.

**Out of scope:** a prefab editor, nested prefabs, prefab variants or inheritance, overrides for every component, live links or automatic updates of instances when the prefab changes, a visual editor.

**Acceptance criteria:** a prefab can be created, saved, loaded and instantiated into a scene without losing data or identity; two instances are independent and have unique entity IDs; internal links point inside their own instance.

**Unit tests:** create a prefab from entities with links; save and load it; instantiate; two instances: distinct IDs, independent values (changing one leaves the other), links inside each instance; ParentComponent validation (missing parent, self, cycle, a parent from another scene) and the destroy rule (children detached, kept with their IDs and components); a scene with instances round-trips with links and instance data; failure cases below; the scene unchanged after a failed instantiation.

**Integration tests:** a prefab saved to a temporary project, registered in the manifest, loaded through its asset ID and instantiated twice into a loaded scene, then the scene saved and loaded again.

**Failure cases:** missing prefab asset; broken prefab document; duplicate or unknown local IDs; a link to a local ID that does not exist; a link to an entity outside the prefab; a self-parent or a cycle in a scene or prefab document; an unknown asset referenced by a prefab entity; a prefab ID that disagrees with the registry.

**Done when:** prefabs pass their tests in CI and the ADR records the model.

**Visual check:** none.

### M4.7 — Integration, final verification and pull request

**Goal:** close M4 against its exit criterion. No new features.

**Scope:**
- An integration test with: a scene; several entities with name, transform, static mesh, camera, directional light and script components; asset references through a registry; one prefab with several entities. Steps: create the scene; add entities and components; bind assets; create and save the prefab; add a script component; save the scene; destroy the runtime scene; load it; check every ID, every component value and every asset reference; run the Lua script; instantiate the prefab; save and load again.
- Full checks in Debug and Release with warnings as errors: headless tests (unit and integration), the GPU tier as the final step requires (`docs/TESTING_AND_CI.md` §8), `format-check.bat`, `git diff --check`, every `scripts\windows\*.bat` wrapper, a fresh clone in a short path; CI green on Windows MSVC and Linux GCC, Debug and Release. Special attention to platform differences in serialization, strings, entity and asset IDs, transforms, Lua, prefabs and error messages; no test depends on non-deterministic results (generated IDs come from seeded generators in tests).
- Documentation: `docs/ENGINE_PLAN.md`, `README.md`, `docs/PROJECT_STRUCTURE.md`, `docs/TECH_STACK.md`, `docs/TESTING_AND_CI.md` (new test groups), the ADR statuses, this plan's verification section.
- Screenshots: none, unless M4 ends with a visible demonstration; then in `docs/screenshots/m4/` with a README giving the exact commands. No UI is added for screenshots.
- Pull request `M4: Scene document, ECS, asset registry and Lua scripting` from `milestone/m4-scene-data-model` to `main` with summary, architecture, sub-milestones, dependencies, tests, CI, screenshots if any, and known limitations; merged with a merge commit only after the user accepts M4.

**Acceptance criteria:** the scene, ECS, assets, serialization, Lua and prefab systems work within the scope of this plan, and every item of *Definition of done* below is checked.

**Done when:** the checks above pass, the pull request is open and the user accepts M4.

**Visual check:** none expected.

## ADRs expected in M4

| Topic | Step | Notes |
|---|---|---|
| Persistent identifiers (entities, scenes; reused by assets) | M4.1 | size, generation, text form, uniqueness, null value |
| EnTT as the internal ECS | M4.2 | package pin, isolation from public headers, access contract, component limits |
| Asset identity and registry | M4.3 | asset ID, types, path policy, manifest, missing assets |
| Scene document format | M4.4 | the scene schema ADR 0004 left to M4; amended by M4.5 and M4.6 for new components |
| Lua runtime and bindings | M4.5 | package and options, error mechanism, binding layer, sandbox, budgets, API; the Conan lockfile |
| Prefab model | M4.6 | entity references, local IDs, instance link |
| Amendments | M4.2, M4.4, M4.5 | ADR 0001 *Verification* (EnTT, Lua confirmed); ADR 0004 (shared float writer, new document kinds) |

A decision on Raven for M5 is not an M4 ADR; the separate evaluation proposed it as ADR 0019.

## Out of scope for M4

The full editor, outliner, inspector UI, scene viewport UI, undo/redo, asset browser, material editor and material files, prefab editor, character controller, NPCs, quests, inventory, physics, audio, animation, multiplayer and networking (AGENTS.md §11, `docs/ENGINE_PLAN.md` M5–M7); rendering scenes or a Scene → renderer bridge; changes to WorldGen, the OpenGL backend or the material API; Vulkan; glTF import (M6); replacing Dear ImGui.

## Definition of done for M4

M4 is done only when:

1. The scene core is implemented.
2. The entity ID model is implemented and recorded in an ADR.
3. EnTT is the internal ECS implementation and stays out of public headers.
4. The basic components work and validate their values.
5. Assets have stable asset IDs.
6. The asset registry works, with a portable manifest.
7. Scenes are saved and loaded.
8. Scene and entity IDs do not change in a round trip.
9. Components are restored exactly.
10. Asset references are correct after loading.
11. The Lua runtime runs scripts.
12. Lua has a limited, documented API.
13. Lua errors do not terminate the engine.
14. Script components are saved and loaded.
15. Prefabs can be instantiated.
16. All required unit and integration tests pass.
17. Debug and Release pass.
18. CI is green on Windows and Linux.
19. The documentation is updated.
20. The pull request is accepted by the user and merged into `main`.

The Raven evaluation is not a condition for M4.

## Findings while preparing the plan (2026-11-14)

- `docs/ENGINE_PLAN.md` and `README.md` on `main` still described M3 as waiting for acceptance; this plan's commit records it as accepted and merged through pull request #4 (`0befa66`).
- No `conan.lock` exists, although `docs/TECH_STACK.md` §7 and `docs/PROJECT_STRUCTURE.md` list one; CI caches packages by `conanfile.py`, not by a lockfile.
- `docs/PROJECT_STRUCTURE.md` lists `cmake/ProjectOptions.cmake` and `cmake/Dependencies.cmake`; only `cmake/CompilerWarnings.cmake` exists. The document is a proposed structure; not changed here.
- `docs/ENGINE_PLAN.md` §7 named `LightComponent` and a `MaterialComponent`; this plan uses `DirectionalLightComponent` (the renderer has one directional light) and a material reference on `StaticMeshComponent`; §7 is aligned with the plan in the same commit.
- The task description put `StaticMeshComponent` (with mesh and material asset IDs) into M4.2, before asset IDs exist (M4.3). Under the one-sub-milestone rule M4.2 could not be finished without part of M4.3, so the component moved to M4.3.
- Material files are deferred to M6 by ADR 0010; M4 gives materials asset IDs and paths but no file format or loader.
- Links between entities inside a prefab (task description, M4.6) need a component that refers to another entity; the M4 component set had none, hence `ParentComponent` in M4.6.
- `docs/TESTING_AND_CI.md` lists "hierarchy validation" among scene tests; with `ParentComponent` it is covered in M4.6.
- WorldGen's shortest-float JSON writer lives inside `engine/src/worldgen/SettingsFile.cpp`; scene documents need the same behavior without depending on WorldGen, hence the shared internal helper in M4.4.
- On Linux the Lua package enables `dlopen`-based module loading; the sandbox in M4.5 must keep `package` and `require` closed.

## Decisions on the open questions

Approved by the user on 2026-11-14; the details are in the steps named.

1. **Lockfile:** `conan.lock` is created in M4.5, after EnTT and Lua, and used by CI and the Windows wrappers; one lockfile for both profiles if possible, otherwise per platform with the reason documented (M4.5).
2. **`ParentComponent`** is added in M4.6 as a relationship only, with the rules in M4.6 (children are detached, not destroyed, when their parent is destroyed); no transform inheritance in M4.
3. **`StaticMeshComponent`** is implemented in M4.3 together with the asset registry, because it depends on `AssetId`; M4.2 keeps `NameComponent`, `TransformComponent`, `CameraComponent` and `DirectionalLightComponent`.
4. **Raven experiment branch** — done as proposed: the prototype is on `experiment/raven-ui-evaluation`; only its report, screenshots and ADR 0019 are on the M4 branch. ADR 0019's option 2 (Raven for the M5 editor) is accepted preliminarily, on the checks listed in the ADR; nothing of Raven enters the engine in M4, and Dear ImGui stays.

## Risks

- **EnTT 4.0 on MSVC and GCC 13** with warnings as errors: header-only templates may trigger warnings in project code; EnTT is included as a system include, and only the CI build of M4.2 proves compatibility.
- **Lua error handling vs C++:** `longjmp` over C++ frames is undefined behavior for objects with non-trivial destructors; the binding rules of M4.5 must make it impossible, and tests must exercise errors from inside bound functions.
- **Lua 5.5 C API differences from 5.4** (e.g. VM creation) and LuaBridge3 being a pre-release: checked in M4.5 before choosing.
- **Cross-platform text and paths:** UTF-8 names, case-insensitive Windows paths, `std::filesystem` behavior and atomic replacement differ between Windows and Linux; covered by tests on both CI platforms.
- **Floating-point round trips:** transforms must read back bit for bit; the shared float writer is already proven by M3.
- **Scope pressure:** scenes invite rendering and editor work; both are M5 and stay out.
