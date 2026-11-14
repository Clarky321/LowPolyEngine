# ADR 0023: Scene document format

- **Status:** Accepted; amended for M4.5 (Amendment 1)
- **Date:** 2026-11-14
- **Related:** ADR 0004 (JSON documents; Amendment 2), ADR 0020 (persistent identifiers), ADR 0021 (components), ADR 0022 (asset references), ADR 0024 (the script component, Amendment 1), `docs/milestones/m4-scene-data-model.md` M4.4

## Context

A scene must be saved to a file and loaded back logically identical: the same scene and entity IDs in the same order, every component value bit for bit, the asset references unchanged. ADR 0004 chose JSON with nlohmann_json and left the scene schema to M4. Scene files are authored data that may be edited by hand or come from outside the project.

## Decision

1. **Envelope:** `{"format": "lpe.scene", "formatVersion": 1, "id": <scene ID>, "name": <scene name>, "entities": [...]}`. IDs are the canonical 36-character strings of ADR 0020. There is no other metadata in version 1: an engine version in the file would make the same scene give different bytes in different builds.
2. **Entities** are written in creation order as `{"id": <entity ID>, "components": {...}}`; loading creates them in the same order with the same IDs and never makes an ID of its own. The `components` object has one key per component the entity has, in the order below; an entity without components has an empty object. An absent key means the entity does not have that component.
3. **Components**, each an object whose fields have the C++ member names, every field written:

   | Key | Fields | Required when reading |
   |---|---|---|
   | `name` | `value` (string) | `value` |
   | `transform` | `position` `{x, y, z}`, `rotation` `{w, x, y, z}`, `scale` `{x, y, z}` | none |
   | `staticMesh` | `mesh` (asset ID), `material` (asset ID or `null` for the default material) | `mesh` |
   | `camera` | `verticalFovRadians`, `nearPlane`, `farPlane` | none |
   | `directionalLight` | `direction` `{x, y, z}`, `color` `{r, g, b}`, `intensity` | none |

   Vectors are objects with named members, so a message can name `scale.y`, and a quaternion cannot be read in the wrong order. A missing field (or vector member) takes the C++ default of the component (ADR 0004 rule 4); fields without a valid default (`name.value`, `staticMesh.mesh`) are required. Every component is checked with its `validate` (ADR 0021, ADR 0022) after reading.
4. **Floats** are written with the shared writer of ADR 0004 Amendment 1 (fewest digits that read back bit for bit; `-0.0` and subnormals included) and read as JSON numbers converted to float; a number outside the float range is an error. Rotations and light directions are stored normalized and stay bit for bit when stored again (ADR 0021), so save → load → save gives the same bytes.
5. **Unknown fields** at any level are ignored with a warning naming their path (ADR 0004 rule 4). **An unknown component type is an error**, not a warning: dropping it would lose the component the next time the scene is saved. A writer that adds a component type or a field raises `formatVersion` and adds a migration, so a v1 reader meets unknown names only in hand-edited files.
6. **Asset references:** with an asset registry, a reference to an asset the registry does not know, or to an asset of another type, is an error naming the field (`entities[3].components.staticMesh.material: asset …: 'bark.png' is a texture, expected a material`). Without a registry the references are read unchecked and kept as they are (a tool can list them with `findAssetReferenceProblems`); a reference is never replaced silently.
7. **Loading in stages:** size (at most 64 MiB) → syntax without exceptions (line and column) → envelope and version → migrations → every field, type, ID (format, null, duplicates naming both entries), component and asset reference, with at most 100 000 entities and names of at most 256 bytes → a new `Scene` built from the checked data. The result is a new scene; the caller replaces its own only with a successfully loaded one, so a broken file changes nothing. Errors are `core::Error` values with the field path, e.g. `entities[3].components.transform: scale.y is 0, must be in [0.0001, 10000]` (the component's `validate` message without its "… component:" prefix).
8. **Versions and migrations:** a missing, non-integer, zero or newer `formatVersion` is an error. `core::migrateDocument` (internal, `engine/src/core/DocumentMigration.hpp`) applies the migrations `vN → vN+1` in order before any field is read; version 1 has none.
9. **Saving** writes through `core::writeFileAtomically`: the text goes to a temporary file in the target's folder (`<name>.tmp-<random>`), which is renamed over the target — `rename(2)` on Linux, `MoveFileEx` with `MOVEFILE_REPLACE_EXISTING` on Windows. The folder must exist; on any failure the old file stays as it was and the temporary file is removed. The data is not forced to the disk, so a power loss right after saving may still lose it. A moved-from scene (null ID) is not saved. Scene names are valid UTF-8, so the document is.

## Consequences

- nlohmann_json is included by `engine/src/core/JsonSupport.hpp` and the document sources only; public headers stay free of it (`tests/unit/scene/public_headers_test.cpp`).
- Adding a component type later (M4.5 scripts, M4.6 parent links) amends this ADR and raises `formatVersion` with a migration from the older versions (which may change nothing), so an older reader reports a newer version instead of meeting an unknown component.
- Documents are verbose (named vector members) but diff and review well.

## Revisit when

- Scenes grow so large that JSON loading is slow: a binary cache beside the JSON source (ADR 0004).
- Scenes link to world generation settings (M5) or contain prefab instances (M4.6).

## Amendment 1: the script component (M4.5, 2026-11-15)

Format version 1 gains the component key `script` (ADR 0024): `{"asset": <asset ID>, "enabled": true}`, written after `directionalLight`. `asset` is required; `enabled` defaults to true. With a registry, an asset that is not registered or not a Script asset is an error naming `entities[i].components.script.asset`. The version stays 1: no document of version 1 has been released while M4 is not merged into `main`, so no reader exists that this could surprise. From the merge on, a new component type raises the version as decision 5 says.
