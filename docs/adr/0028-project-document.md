# ADR 0028: Project folder and project document

- **Status:** Accepted (2026-11-21, M5.4). The plan asked M5.4 to decide this ADR (`docs/milestones/m5-minimal-editor.md`, M5.4, "Decisions to make", and addition 2 of the approved plan); the decisions were taken under `docs/DEVELOPMENT_WORKFLOW.md` §4.
- **Date:** 2026-11-21
- **Related:** ADR 0004 (versioned JSON documents; Amendment 2), ADR 0020 (persistent identifiers), ADR 0022 (asset paths and the manifest), ADR 0023 (scene documents and atomic file writes), M5 plan decision 5 (the project in `engine/project/`)

## Context

The editor opens and saves a project: an asset manifest (ADR 0022), a scene (ADR 0023) and the world generation settings (ADR 0004 Amendment 1), each a document of its own, plus the asset files the manifest names. Something must say which files belong together, and a runtime application will later open the same project without the editor.

Each document is written atomically on its own (`core::writeFileAtomically`), but a project is several files. A save that stops after the manifest and before the scene leaves a new manifest next to an old scene: a mixture that may not even open (a scene that refers to an asset the new manifest dropped), or opens as a state that was never saved.

## Decision

1. **The folder.** A project is a folder; it is opened by its folder, not by a file.

   ```text
   <project>/
     project.json              the project document (lpe.project)
     assets.json               the asset manifest (lpe.assets.manifest, ADR 0022)
     scenes/main.scene.json    the start scene (lpe.scene, ADR 0023)
     worlds/world.json         the world settings (lpe.worldgen.settings, ADR 0004 Amendment 1)
     models/, textures/, …     the asset files, where the manifest's paths point
   ```

   - `project.json` is the fixed name of the project document. The other paths are written in it; the names above are the defaults of a new project.
   - `project.saving.json` is reserved for the save journal (point 5), and file names ending in `.saving-` and 8 hex digits for a save's temporary files.
   - Asset folders are not created: they exist when assets are added (asset import is M6).
2. **The project document**, format `lpe.project`, version 1, in the envelope of ADR 0004:

   ```json
   {
     "format": "lpe.project",
     "formatVersion": 1,
     "name": "Sky Islands",
     "assetManifest": "assets.json",
     "startScene": "scenes/main.scene.json",
     "worldSettings": "worlds/world.json"
   }
   ```

   - **All four fields are required.**
     - `name` follows the rules of scene names (`scene::validateName`).
     - The paths follow ADR 0022's rules for asset paths: project-relative, normalized, portable; never absolute or outside the folder.
   - **The three paths must:**
     - differ from each other, also when only letter case is ignored;
     - not be `project.json` or `project.saving.json`;
     - not lie inside one another (`a` and `a/b`).
     - No asset of the manifest may have one of these paths either.
   - **Unknown fields** are ignored with a warning. A newer version is an error, and version 1 has no migration.
   - **Written:** fields in this order, two-space indent, a final newline. The same project always gives the same bytes.
   - **Only the start scene.** Version 1 names one scene. Other scene files may sit in `scenes/`, but the project neither opens nor saves them. A list of scenes can come with a later version and its migration, when the editor opens more than one scene.
   - **The world settings file is required.** A missing or broken file is an error like any other part, not a fallback to the default settings: a silent default would replace the project's world at the next save.
3. **New project:**
   - the folder is created if it does not exist;
   - an existing `project.json` or save journal is refused ("already holds a project"), and so is any of the default files: a new project never overwrites a file;
   - it then has an empty manifest, an empty scene "Main" with a new scene ID, and the default world settings;
   - it is written with the save of point 5.
4. **Open, all or nothing, read-only:**
   - The order: the save journal if there is one, the document, the manifest into a new registry for the project's folder, the start scene checked against that registry (a reference to an unknown asset, or to one of another type, is an error), the world settings.
   - Every error starts with the part's project-relative path, e.g. `scenes/main.scene.json: entities[3]...`.
   - Warnings (unknown fields) are collected with their file's path.
   - Size limits: the document 1 MiB, the others those of their formats.
   - The result is a new `Project` value; a failure gives no project at all.
   - Open writes nothing, so a project can be opened from a read-only folder.
5. **Save, consistent as a whole** (plan addition 2). Every part is written at every save, in a fixed order: the manifest, the world settings, the start scene, the project document.
   1. **Check:**
      - the document as in point 2;
      - the world settings pass `validate()`;
      - the scene has an ID;
      - every asset reference of the scene is known to the registry, with the right type.

      Every part is turned into text in memory. A failed check writes nothing, so a save never writes a project that would not open.
   2. **Finish what an earlier save left:**
      - a journal there from an interrupted save is completed (its renames, then the journal removed);
      - temporary files of earlier failed saves next to the project's files are removed;
      - if finishing fails, the save stops before writing anything.
   3. **Folders** of the parts are created.
   4. **Temporary files:**
      - every part is written next to its file as `<file>.saving-<id>`, `<id>` being 8 random hex digits, the same for all files of one save;
      - on a failure, this save's temporary files are removed, and the project is unchanged.
   5. **Commit:**
      - the journal `project.saving.json` (format `lpe.project.save`, version 1) lists each part's path and its temporary path;
      - it is written to a temporary file of its own and renamed into place, and **that rename is the commit point**: before it the project is the previous save, after it the new one;
      - on a failure before the rename, the temporary files are removed and the project is unchanged.
   6. **Put in place:**
      - every temporary file is renamed over its part, in the same order; each rename is atomic, like `core::writeFileAtomically`'s;
      - on a failure the save reports it, and the journal stays: the project opens as the new save, and the next save finishes it.
   7. **Done:** the journal is removed. If that fails, the save still succeeded, with a warning: the journal names no temporary file that still exists.

   **Opening during or after an interrupted save:**
   - With a journal, a part whose temporary file still exists is read from that file: after the commit it holds the new content, and the part itself may still hold the old.
   - So every part opens as the new save, whatever renames were done. A warning says an interrupted save was found and that the next save completes it.
   - Without a journal, temporary files (of a save that failed before its commit) are ignored: the project opens as the previous save, and the next save removes them.
   - A journal that cannot be read fails the open with its name. It is written atomically, so a broken one was changed by hand, and the project cannot know which save is whole.
   - A journal may name only the project's own parts, and only temporary files named as in step 4 next to them, so it cannot make open read another file.

   **Why this way:** writing each file atomically does not make the project atomic. Other ways to do it, and why they were not chosen:
   - Files named by save generation (`main.scene.3.json`): every save would rename every file, which is noise in version control.
   - Writing the whole folder anew and swapping folders: that copies every asset at every save.
   - A generation number inside each part: that changes the formats of ADR 0004, 0022 and 0023.

   The journal keeps the file names, costs one rename per file, and needs no change to the parts' formats.
6. **Code and dependencies** (plan decision 5):
   - The public API is in `engine/include/engine/project/`: `ProjectDocument.hpp` and `Project.hpp` (`createProject`, `openProject`, `saveProject`, the `Project` value with its registry, scene and settings).
   - The implementation is in `engine/src/project/`. It does every file operation through an internal interface (`ProjectFiles`), which the tests replace to make any operation fail.
   - The project uses core, assets, scene and worldgen only: no editor, Raven, SDL3, OpenGL, renderer, platform or UI code. A test reads the module's includes and enforces it. A runtime application can open a project with the same code.
7. **Not covered:**
   - Data is not forced to the disk (no `fsync`), as in ADR 0023: a power loss right after a save may still lose it. The protocol covers a program that fails or stops at any point.
   - Two programs saving the same project at the same time are not coordinated.
   - Changes made by hand to the project's files between open and save are overwritten by the save.

## Consequences

- A project is one folder with readable, diffable documents under fixed names; moved to another folder or machine, it opens the same (every path is project-relative).
- A save that fails or stops anywhere leaves a project that opens as the previous save or as the new one, never as a mixture. The tests check it by making every file operation of a save fail in turn, once as an error and once as a stop of everything that follows (with a partly written file).
- A program that reads the parts without the engine's open (a text editor, a script) may see a mixture while a journal is there. The project's own readers go through `openProject`.
- The editor (M5.11) gets the project's file workflow from this module; M5.4 itself has no UI.
