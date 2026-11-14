# ADR 0022: Asset identity and registry

- **Status:** Accepted
- **Date:** 2026-11-14
- **Related:** ADR 0020 (persistent identifiers), ADR 0004 (versioned JSON documents), ADR 0021 (scene components), ADR 0010 (materials; files in M6), ADR 0012 (OBJ subset), ADR 0008 (PNG), `docs/milestones/m4-scene-data-model.md` M4.3

## Context

Scenes and prefabs must refer to meshes, textures, materials, scripts and prefabs in a way that survives restarts, moving the project to another folder or machine, and switching between Windows and Linux. Up to M3 the renderer identifies resources only by runtime handles (`MeshHandle`, `TextureHandle`, `MaterialHandle`), which live as long as one renderer, and island-preview loads its files by hard-coded paths.

## Options

| Question | Options | Choice |
|---|---|---|
| Where asset IDs are stored | one manifest per project; a sidecar file next to each asset (`tree.obj.meta`) | **manifest**: one file to load, review and diff; no stray files beside assets; a moved or renamed file is one line to change. Sidecars travel with their file, but every folder gets extra files and a missing sidecar silently creates a new ID. |
| Backslashes in paths | normalize to `/`; reject | **reject**: on Linux `\` is an ordinary file name character, so a normalized `a\b` would name another file there; documents always use `/`. |
| Characters | any Unicode; printable ASCII only | **printable ASCII**: Unicode names fold case and normalize (NFC vs NFD) differently between file systems, which the engine cannot check without a Unicode library. |
| Letter case | compare exactly; compare folded and treat differences as conflicts | **conflict**: a project must behave the same on Windows (case-insensitive) and Linux (case-sensitive). |
| A missing file | an error when registering; a state | **a state**: a manifest may list files that are not there yet or any more; loading such an asset fails. |
| Format versions of asset files | in the manifest; inside each file | **inside each file** (ADR 0004 envelopes, e.g. prefab documents in M4.6): a copy in the manifest could disagree with the file. No asset metadata has a version for now. |

## Decision

1. **`assets::AssetId`** is `core::TypedUuid` with the kind "asset ID" (ADR 0020): distinct from entity and scene IDs, integers, pointers and renderer handles, with no conversion either way (checked at compile time). The null `AssetId` refers to no asset and is never registered.
2. **`assets::AssetType`:** `mesh`, `texture`, `material`, `script`, `prefab` — the names in documents. Extensions, compared ignoring case: meshes `.obj` (ADR 0012), textures `.png` (ADR 0008), scripts `.lua`. Materials and prefabs have no file format yet (M6, M4.6), so any extension; the step that defines a format adds its extension.
3. **`assets::AssetPath`**, always valid and normalized, relative to a project root that is given at runtime and never stored:
   - at most 256 bytes of printable ASCII;
   - `/` between folders; empty and `.` segments are dropped, `..` is resolved and may not leave the root;
   - rejected, each with its own message: absolute paths, drive letters, UNC paths, a leading `~`, `\`, the characters Windows forbids (`< > : " | ? *`), segments ending in `.` or a space, and Windows device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9`, `LPT1`–`LPT9`, with any extension).
4. **`assets::AssetRegistry`**, a value owned by its caller (no global), not thread-safe:
   - `registerAsset(id, type, path)` rejects, changing nothing and naming both assets: the null ID, an ID or path already registered, a path or a folder that differs from a registered one only in letter case, a path that is a registered folder or has a registered file as a folder, and an extension the type does not allow.
   - `unregisterAsset`, `contains`, `find` (a copy of `AssetMetadata`: ID, type, path, availability, and the file name as the source name), `findByPath` (exact spelling), `checkType`, `resolve(id, expectedType)` (the file for a CPU loader), `refreshAvailability`, `assets()` in path order (compared folded).
   - **Availability:** a file is present only if it exists with exactly the path's spelling: the registry walks the folders and compares names exactly, so a wrong letter case is missing on Windows as on Linux, and the message names the spelling found on disk. `resolve` checks again at the time of loading.
   - The registry loads nothing and owns no GPU resource; the chain `AssetId → registry → path → geometry::readObj / core::readPng` is the caller's, and making GPU resources stays with the renderer.
5. **Manifest** (`engine/assets/AssetManifest.hpp`), ADR 0004 rules:
   - `{"format": "lpe.assets.manifest", "formatVersion": 1, "assets": [{"id", "type", "path"}, …]}`, entries in the registry's path order, two-space indent, final newline;
   - `id`, `type` and `path` are required in every entry; a missing `assets` list means no assets;
   - unknown fields are ignored with a warning naming their path (`assets[2].name`);
   - wrong types, invalid IDs, types or paths, a newer format version, and every conflict the registry rejects are errors naming the field (`assets[1]: asset …: path 'a.obj' is already used by asset …`);
   - at most 100 000 entries and 64 MiB per file.
   - Loading is all or nothing: a new registry is built and returned only if every entry passes. Missing files are not errors.
   - nlohmann_json stays private in `engine/src/assets/AssetManifest.cpp`.
6. **`scene::StaticMeshComponent`** holds the `AssetId`s of a mesh (never null) and a material (null: the renderer's default material). `scene::findAssetReferenceProblems(scene, registry)` lists every reference to an unregistered asset or an asset of another type, in entity creation order, with the entity, component, field, asset and message. Missing files are availability, not reference problems.

## Consequences

- The scene depends on `assets` (asset IDs, the registry for reference checks); `assets` depends only on `core`. Neither includes the renderer.
- Asset file names in Cyrillic or other non-ASCII scripts are rejected; projects name files in ASCII.
- Checking availability lists the folders along each path; this is meant for loading projects, not for every frame.
- The manifest is written with an ordinary file write; atomic replacement comes with the scene documents (M4.4).

## Revisit when

- An import pipeline needs source files, import settings or caches (out of scope for M4).
- Material files (M6) or prefab documents (M4.6) define their extensions.
- A project needs non-ASCII file names: that requires a decided Unicode normalization and case folding.
