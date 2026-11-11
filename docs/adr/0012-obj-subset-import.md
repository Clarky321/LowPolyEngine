# ADR 0012: OBJ subset import with an own parser

- **Status:** Accepted
- **Date:** 2026-11-11
- **Related:** ADR 0007 (`MeshData`, UV convention, normal tolerance), ADR 0011 (flat shading), `docs/milestones/m2-renderer-materials.md` M2.9 and M2.10, `docs/ENGINE_PLAN.md` M2 and M6, `docs/TECH_STACK.md` §4, AGENTS.md §5 and §7

## Context

M2's exit asks for a basic model import proof: a simple model made in an external tool, drawn with a texture (M2.10). Wavefront OBJ is the simplest widely exported text format. The import must produce a flat-shaded `MeshData` that passes `geometry::validate()`, and imported files are untrusted input (AGENTS.md §5): bounded memory, no crashes, clear errors. glTF 2.0 remains the candidate for the real content pipeline in M6.

## Options

1. **tinyobjloader** (MIT, header-only; Conan Center has 1.0.6, 1.0.7 and the release candidate 2.0.0-rc10). It reads far more of OBJ than M2 needs (materials, groups, lines, points) and is lenient: it continues past many problems with warnings. It has no limits for untrusted input; the engine would still have to convert its indexed attributes into per-face vertices, check index ranges, compute missing normals, enforce limits and phrase errors. That conversion is most of the work.
2. **An own parser for a documented subset**: four statements (`v`, `vt`, `vn`, `f`), strict errors with line numbers, limits checked before anything grows, no new dependency.

## Decision

Option 2: `engine::geometry::parseObj(text, limits)` and `readObj(path, limits)` in `engine/include/engine/geometry/ObjImport.hpp`. The module boundary stays geometry (CPU data, no GPU): there is no asset registry yet, and AGENTS.md §3 asks not to add one before a feature needs it. M6 may wrap or replace this behind an asset import pipeline.

- **Subset:** `v x y z [w]`, `vt u [v [w]]`, `vn x y z`, `f` with 3 or more corners of the forms `v`, `v/vt`, `v//vn`, `v/vt/vn` (one form per face). 1-based indices and negative ones relative to the latest element; an index must refer to an element defined earlier in the file. `#` comments, blank lines, spaces and tabs, LF and CRLF. Every other statement (`o`, `g`, `s`, `usemtl`, `mtllib`, `l`, …) is skipped, counted and listed (at most 16 keywords of at most 32 characters); `readObj` logs one warning with them.
- **Geometry:** polygons are fan-triangulated from their first corner, which is correct for convex polygons; convexity is not checked. Every face gets vertices of its own (flat shading, ADR 0011). Given normals are normalized on import and used as they are (smooth normals stay possible); a face without normals gets its flat normal from the new `geometry::faceNormal`, which the primitive builders now share. UVs follow ADR 0007 (V up), so OBJ UVs need no flip. Vertex colors are white; color comes from the material.
- **Strictness:** every malformed statement is an error naming its line: wrong number counts, tokens that are not numbers or indices, non-finite numbers or numbers outside the float range, index 0, indices out of range, corners of mixed forms in one face, faces with and without UVs in one file, a zero normal, a degenerate face without normals. Input without faces is an error. Tokens are quoted shortened and with non-printable bytes replaced, so garbage input cannot flood the log.
- **Limits** (`ObjLimits`, overridable per call): 64 MiB of text (checked on the file size before reading), 1,000,000 elements of each kind and 1,000,000 face corners (= mesh vertices). Memory stays below about 100 MB. Running out of memory returns an error instead of throwing.
- **Degenerate faces:** `faceNormal` rejects a polygon when twice its area is at most 1e-10 of its squared size (`kDegenerateFaceTolerance`). The threshold is relative, so tiny and huge models behave alike, and it only rejects corners that are numerically on one line or point.

## Not supported in M2

Materials (`mtllib`/`usemtl`), several objects or groups as separate meshes, smoothing groups, vertex colors (`v x y z r g b`), lines, points, curves, line continuation with `\`, CR-only line endings. These are reported as skipped statements or as errors. Concave polygons are the exception: they are fan-triangulated like convex ones, without a warning.

## Consequences

- No new dependency; about 500 lines of parser (`engine/src/geometry/ObjImport.cpp`), covered by headless tests with inline fixtures (`tests/unit/geometry/obj_import_test.cpp`).
- Models must be exported as triangles or convex polygons with one UV map or none; M2.10 documents the export settings of its model.
- If later milestones need materials or broader OBJ compatibility, the decision is revisited; for M6 the plan is glTF 2.0 rather than extending OBJ.
