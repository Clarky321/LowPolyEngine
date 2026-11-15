# M5 screenshots

Real framebuffer captures made by the editor itself (`lpe-editor --screenshot`: `Renderer::captureFrame` → PNG, ADR 0006), not edited. Each was taken from the repository root with the Debug build of the sub-milestone listed; without `--frames`, the editor renders 30 frames and saves the last one:

```powershell
build\windows-msvc-debug\bin\Debug\lpe-editor.exe --screenshot docs\screenshots\m5\<file>.png
```

Machine: Windows 11, NVIDIA GeForce RTX 4060, driver reporting OpenGL "4.5.0 NVIDIA 610.88" (hardware OpenGL), display scale 100 %, 1280×760 framebuffer, vsync on.

The sub-milestone column names the commit that added the image: `git log --grep "(M5.x)"` finds it (the image is captured before its commit exists, so no hash is written here).

| File | Sub-milestone | Shows |
|---|---|---|
| `001-editor-shell.png` | M5.2 | The empty editor shell, drawn by Raven 0.2.3 through the engine's OpenGL backend on the engine's SDL3 window (ADR 0027). A toolbar strip at the top (empty), then Raven's DockArea with four titled panels: Hierarchy on the left (a fifth of the width), the Viewport in the middle (black: nothing is drawn into it before M5.3), the Inspector on the right and the Console below the viewport. Every panel is empty. The run had 0 OpenGL errors, 0 OpenGL state mismatches, 0 Raven errors and 0 Raven allocations after shutdown. |

Earlier images show the program as it was at their sub-milestone.
