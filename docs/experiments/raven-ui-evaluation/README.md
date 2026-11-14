# Raven UI evaluation — screenshots

Frames saved by the prototype itself (`renderer.captureFrame()` after Raven's frame, as island-preview does, ADR 0006), Release build of `sandbox/raven-evaluation/` with Raven at commit `7ab270df3b618de3d11af6033912374b6cd621b7`.

Machine: Windows 11 (10.0.26100), MSVC 19.51 (Visual Studio 2026), NVIDIA GeForce RTX 4060, driver 610.88, display scale 100 %. The statistics in the toolbar of each image are those of the first half second (shader compilation and the first layouts), not of steady frames; steady numbers are in the report's *Performance* section.

Commands, from the repository root (build as in `sandbox/raven-evaluation/README.md`):

```powershell
build\raven-eval-release\bin\Release\raven-evaluation.exe --frames 90 --screenshot docs/experiments/raven-ui-evaluation/001-editor-shell.png
build\raven-eval-release\bin\Release\raven-evaluation.exe --frames 90 --texture-mode --screenshot docs/experiments/raven-ui-evaluation/002-cpu-read-back-viewport.png
build\raven-eval-release\bin\Release\raven-evaluation.exe --frames 90 --look glass --screenshot docs/experiments/raven-ui-evaluation/003-glass-look.png
build\raven-eval-release\bin\Release\raven-evaluation.exe --self-test --screenshot docs/experiments/raven-ui-evaluation/004-inspector-docked-by-drag.png
build\raven-eval-release\bin\Release\raven-evaluation.exe --frames 90 --ui-scale 1.5 --size 1600x950 --screenshot docs/experiments/raven-ui-evaluation/005-ui-scale-150.png
```

| File | Shows |
|---|---|
| `001-editor-shell.png` | The editor shell from `editor.xml` at 1280×760: toolbar, hierarchy (TreeView), the generated archipelago (seed 1) drawn by the engine's renderer from Raven's viewport draw callback, inspector (TextInput, Label, Button), console (TabView, ListView) in a DockArea, Raven's dark look. |
| `002-cpu-read-back-viewport.png` | The same view with the viewport as a Raven texture filled from a CPU read-back of the engine's rendering: identical content and orientation. |
| `003-glass-look.png` | Raven's glass look through the prototype's backend: backdrop copies and their blur for the glass primitives. |
| `004-inspector-docked-by-drag.png` | After the self-test (window resized to 1080×680): the Inspector panel was dragged by its tab to the left edge of the DockArea with input pushed through SDL; the camera moved by the test's keys and mouse. |
| `005-ui-scale-150.png` | Raven's UI scale 1.5 at 1600×950: glyphs rasterized at the larger size, layout in logical units. |
