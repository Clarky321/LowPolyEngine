# ADR 0025: Prefab model

- **Status:** Accepted
- **Date:** 2026-11-15
- **Related:** ADR 0020 (persistent identifiers), ADR 0021 (components), ADR 0022 (prefab assets), ADR 0023 (scene documents; Amendment 2), ADR 0004 (versioned JSON), `docs/milestones/m4-scene-data-model.md` M4.6 and the decisions of 2026-11-14 on `ParentComponent`

## Context

A prefab is a reusable group of entities — a house of a base, a roof and a door — saved once and placed into scenes many times. Its entities refer to each other (the roof's parent is the base), so the entity model needs a link between entities, and those links must point inside each instance. Approved on 2026-11-14:
- `ParentComponent` is a relationship only, with no transform inheritance in M4;
- an entity cannot be its own parent, links cannot form cycles, and the parent must exist in the same scene;
- destroying a parent detaches its children;
- links are checked when documents are loaded and when a prefab is instantiated.

## Decision

1. **`scene::ParentComponent { EntityId entity; }`**, a scene component like the others. `validate` rejects the null ID. The scene enforces the rest in `addComponent` and `replaceComponent`:
   - not the entity itself ("an entity cannot be its own parent");
   - a parent the scene contains ("parent … does not exist in scene 'Main'"), which also covers an ID from another scene;
   - no cycle: the scene walks up from the new parent and rejects a link that reaches the entity again.

   `destroyEntity` first removes the `ParentComponent` of every child of the destroyed entity; the children keep their IDs and other components. An ID created again does not get children back. There is no transform inheritance: every transform stays in world space.
2. **`scene::EntityComponents`** holds the plain components of one entity as `std::optional`s: name, transform, static mesh, camera, directional light and script. It excludes the links, whose meaning depends on where the entity lives. `componentsOf(scene, entity)` copies them, `addComponents(scene, entity, components)` adds them, and `validate` checks them. Scenes and prefabs exchange entities through it.
3. **`scene::Prefab`** is `{ AssetId id; std::string name; std::vector<PrefabEntity> entities; }` with `PrefabEntity { PrefabLocalId localId; std::optional<PrefabLocalId> parent; EntityComponents components; }`. `validate(prefab)` requires:
   - a non-null ID and a valid name;
   - 1 to 10 000 entities;
   - **local IDs** that are unsigned 32-bit integers of at least 1, unique within the prefab. Integers, not UUIDs: they only need to be unique in one document, and they read well in it;
   - parents that are other entities of the prefab, without a cycle;
   - components that pass their `validate`.
4. **`createPrefab(scene, entities, id, name)`** copies the given entities in the given order with local IDs 1, 2, …. Parent links inside the set become local links. A parent outside the set is an error naming both entities, and so are an empty set, a duplicate or an unknown entity. A `PrefabInstanceComponent` is not copied: there are no nested prefabs.
5. **`instantiatePrefab(scene, prefab)`** creates new entities with IDs from the scene's generator, copies every component value (nothing is shared between instances or with the prefab), maps parent links to the new entities, and adds a `PrefabInstanceComponent` to each entity. It returns `PrefabInstance { id, entities }`. All or nothing: the prefab is validated first, and on any later error the entities created so far are destroyed again. The scene's content is then as before, although its generator has moved on. Entities are created before any link is added, so parents may follow their children in the prefab.
6. **Instance link:** `scene::PrefabInstanceComponent { AssetId prefab; PrefabInstanceId instance; PrefabLocalId localId; }`. `PrefabInstanceId` is a `TypedUuid` ("prefab instance ID") drawn from the scene's generator through `Scene::generateId()`, and checked to be unused in the scene. All entities of one instance carry the same one. A scene stores each instance's full component values plus this link; there are no overrides and no live updates in M4, but a later milestone can find the instances of a prefab and their entities through it. It is never removed by the scene itself; it is only data.
7. **Prefab documents** (`engine/scene/PrefabDocument.hpp`):
   - **Format:** `{"format": "lpe.prefab", "formatVersion": 1, "id": <asset ID>, "name": …, "entities": [{"localId": n, "components": {…}}]}`.
   - **Components:** those of scene documents (ADR 0023) except `prefabInstance`, which is an unknown type here. A parent is `"parent": {"entity": <local ID>}`.
   - **Reading:** the staged, all-or-nothing rules of scene documents with paths in the errors (`entities[1].localId: 1 is also entities[0]`), then `validate(prefab)` for links and cycles. With a registry, the document's ID must be a registered Prefab asset and every asset reference must be known and of the right type.
   - **Writing:** `savePrefab` validates first and writes atomically. A document is at most 16 MiB.
   - **Loading an asset:** `loadPrefabAsset(registry, id)` resolves the asset as a Prefab, loads the file with the registry's checks, and rejects a document whose ID is not `id`.
8. **Scene documents** gain `"parent": {"entity": <entity ID>}` and `"prefabInstance": {"prefab": …, "instance": …, "localId": n}` in format version 1, which is still unreleased (ADR 0023 Amendment 2). Reading checks parent links in the document with paths (own parent, a parent not in the document, a cycle) before anything is built. It creates every entity before adding components, so a child may come before its parent. With a registry, `prefabInstance.prefab` must be a registered Prefab asset; `findAssetReferenceProblems` reports it too.
9. **Shared document code:** the JSON of the plain components moved from `SceneDocument.cpp` into the internal `engine/src/scene/ComponentJson.hpp`, used by both document kinds; scene documents are byte for byte as before (`tests/unit/scene/scene_document_test.cpp`).

## Consequences

- Hierarchies exist as data; moving a parent moves nothing else. Hierarchical transforms get a milestone of their own.
- An instance is independent of its prefab after instantiation; changing the prefab file changes no scene.
- Instance consistency (one entity per local ID per instance) is not enforced by the scene in M4; nothing updates instances yet.
- A destroyed parent silently detaches its children; an editor will want to show that, through commands (M5).

## Revisit when

- Prefab instances must follow changes of their prefab (overrides, updates): the instance link is the starting point.
- Nested prefabs or variants are needed.
- Transforms are inherited along parent links.
