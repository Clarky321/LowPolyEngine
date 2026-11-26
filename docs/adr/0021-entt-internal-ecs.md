# ADR 0021: EnTT as the internal ECS

- **Status:** Accepted; amended for M5.5 (Amendment 1)
- **Date:** 2026-11-14
- **Related:** ADR 0001 (EnTT as a candidate), ADR 0020 (persistent identifiers), ADR 0004 (private dependencies, no library types in public headers), ADR 0011 (one directional light), AGENTS.md §3 (EnTT handles are not identity), `docs/milestones/m4-scene-data-model.md` M4.2

## Context

M4.2 gives the entities of a `scene::Scene` components: a name, a transform, a camera and a directional light now; a static mesh (M4.3), a script (M4.5) and a parent link (M4.6) later. Components must be stored per entity, found by entity ID, listed by type in a deterministic order (documents, M4.4) and validated before they enter the scene. The scene API of M4.1 (entities identified only by `EntityId`, no pointers into storage) stays as it is.

## Options

| Option | For | Against | Verdict |
|---|---|---|---|
| A `std::map` per component type in `Scene` | no dependency; enough for four components | a hand-made component store that each new component and every later system (viewport extraction, scripting) has to extend; no packed iteration | rejected |
| EnTT behind the scene's own API | the ECS candidate of ADR 0001; header-only; packed sparse-set pools per type, cheap membership tests, entity recycling with versions | a large template library; its handles must not leak | **chosen** |
| EnTT's registry exposed to callers | no wrapper code | `entt::` types in public headers, no validation, handles used as identity | rejected (AGENTS.md §3, ADR 0004 rule 7) |
| Named functions per component (`addTransform`, `transform`, …) | no templates in the API | five functions for every component type; the API grows with each one | rejected |

## Decision

1. **Package:** `entt/4.0.0` from Conan Center, recipe revision `ca14bea628dd1cfb1e8d444456de278c` (package `da39a3ee5e6b4b0d3255bfef95601890afd80709`, revision `bdd6b51bb3ea0f2ecf3c40414b227aca`); MIT; header-only, no options; the recipe requires C++20; CMake package `EnTT`, target `EnTT::EnTT`. Compiled with warnings as errors by MSVC 19.51 and GCC 13 in the CI of M4.2 (Windows and Linux, Debug and Release). The Conan lockfile that pins the revision comes with M4.5.
2. **Isolation:** `EnTT::EnTT` is linked `PRIVATE` and included only in `engine/src/scene/Scene.cpp`. `Scene` keeps its storage behind a pointer to a private `Scene::Storage` (the `entt::registry` and the `EntityId → entt::entity` index). No public header includes an EnTT header, which `tests/unit/scene/public_headers_test.cpp` checks for every header under `engine/include/` (together with nlohmann_json).
3. **Component types:** plain structs in `engine/include/engine/scene/` — `NameComponent`, `TransformComponent`, `CameraComponent`, `DirectionalLightComponent` — each with `kTypeName`, defaulted `==` and a GPU-free `validate` whose message names the component and the field. The concept `scene::SceneComponent` lists exactly the stored types; the component functions of `Scene` are templates constrained by it, defined in `Scene.cpp` and explicitly instantiated there for each listed type. A new component type is added to the concept and to the instantiation list.
4. **Access contract:**
   - `addComponent<T>(entity, value)` adds a component the entity does not have; adding one it already has is an **error** (no silent replacement, because a document that gives an entity the same component twice is broken).
   - `replaceComponent<T>(entity, value)` changes a component the entity has; `removeComponent<T>(entity)` removes one; both fail if it has none.
   - `hasComponent<T>(entity)` is false for unknown entities.
   - `component<T>(entity)` returns a **copy**, so no reference into a pool can outlive a later change.
   - Each failing operation changes nothing and names the scene, the entity and the component type, e.g. `scene 'Main': entity …: already has a camera component`. Validation runs before anything is stored, so every component in a scene passes its `validate`.
5. **Scene order:** every EnTT entity carries an internal record with its `EntityId` and its creation sequence. `entitiesWith<T, U...>()` collects the entities of the first type's pool, sorts them by sequence and keeps those that have the other types, so results are in creation order, independent of EnTT's pool order. `entityIds()` sorts the same way.
6. **Lifetime:** destroying an entity destroys its EnTT entity and with it all its components; an ID created again gets a new EnTT entity without components. EnTT recycles its handles with a new version, but they never leave `Scene.cpp` and are never serialized. `clear()` releases the whole storage; a moved-from scene has none until an entity is created.
7. **Stored form:** a transform's rotation and a light's direction are stored normalized. Normalization runs in double; a value whose length is already within 10⁻⁶ of 1 is stored unchanged, so storing a stored value again keeps every bit (a saved scene reads back the same on any platform).
8. **Limits:**

   | Component | Field | Rule |
   |---|---|---|
   | Name | `value` | the scene's name rules (`validateName`): not empty, at most 256 bytes, valid UTF-8, no control characters |
   | Transform | `position` | finite, each coordinate within ±10⁵ (100 km; floats there are 8 mm apart) |
   | Transform | `rotation` | finite, length within 10⁻³ of 1 (a hand-written 0.7071 is accepted), stored normalized |
   | Transform | `scale` | finite, each component in [10⁻⁴, 10⁴]; zero and negative scales rejected (a negative scale flips the winding the renderer culls; mirroring needs its own decision) |
   | Camera | `verticalFovRadians` | finite, in [1°, 179°] |
   | Camera | `nearPlane` | finite, ≥ 0.001 |
   | Camera | `farPlane` | finite, > `nearPlane` × 1.001 |
   | Directional light | `direction` | finite, not zero, stored normalized |
   | Directional light | `color`, `intensity` | finite, ≥ 0, and color × intensity finite (the renderer's `LightDesc` gets the product) |

   The camera ranges are those `camera::projectionMatrix` uses without clamping or replacing a value, which a test checks at the extremes. Messages print numbers in plain notation where that is exact, otherwise in the shortest exact form.
9. **`math::Quat`:** `engine/core/Math.hpp` gains the alias `math::Quat = glm::quat` for rotations (ADR 0007); its constructor takes (w, x, y, z).

## Consequences

- The engine builds EnTT's templates only in `Scene.cpp`; other sources do not see them.
- Component reads copy the value; components are small (the largest is a name of at most 256 bytes).
- A query costs O(m log m) for m entities in the first type's pool plus a lookup per further type; deterministic order matters more than speed here.
- A scene is not thread-safe (as in M4.1).
- There are no systems, update loops, render extraction, reflection or automatic serialization of components; documents (M4.4) write each component type explicitly.

## Revisit when

- Systems need EnTT views or groups for speed: they stay inside `engine/src/scene/` behind scene functions.
- A component refers to other entities (M4.6 `ParentComponent`): its rules are checked by the scene, not by the component.
- A scene is used from several threads.

## Amendment 1: an entity's place in the creation order (M5.5, 2026-11-26)

- The scene keeps every entity's place in the creation order (a sequence number that is never given out twice), which `entityIds()` and the scene document follow (ADR 0023).
- **New functions:**
  - `Scene::placeOf(entity)` returns that place as an `EntityPlace`;
  - `Scene::restoreEntity(id, place)` creates an entity with the given ID at a free place the scene gave out before: the place it had before it was destroyed. It fails for a place the scene never gave out or another entity holds.
- **Why:** the editor's undo of a delete (ADR 0029) must put the entity back where it was, or the scene document's bytes would change. `createEntity(id)` still puts a re-created entity at the end.
- A place is only meaningful for the scene that gave it out; clearing the scene starts the places over.
