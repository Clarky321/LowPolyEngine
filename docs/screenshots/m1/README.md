# M1 screenshots

Real framebuffer captures made by the engine itself (`Renderer::captureFrame` → PNG, ADR 0006), not edited. Each was taken from the repository root with the Debug build of the commit listed, which renders 120 frames and saves the last one:

```powershell
build\windows-msvc-debug\bin\Debug\island-preview.exe --screenshot docs\screenshots\m1\<file>.png
```

Machine: Windows 11, NVIDIA GeForce RTX 4060, driver reporting OpenGL "4.5.0 NVIDIA 610.62", 1280×720 framebuffer, vsync on (120 Hz display).

| File | Commit | Shows |
|---|---|---|
| `001-engine-window.png` | `703fdff` (M1.3) | The M0 test triangle drawn through the new application loop (`engine::app::run`); proves the lifecycle and the capture path. |
| `002-triangle.png` | `74275c8` (M1.5) | The same triangle and an indexed quad, both built as `geometry::MeshData` and uploaded through the renderer mesh API. The quad is drawn after the triangle but stays behind it: depth test. |
| `003-camera.png` | `1c56fdc` (M1.6) | The static test scene through the perspective free-fly camera at its start pose: checker ground, pyramid, boxes (one spinning), corner pillars, back-face culling. |
| `004-debug-overlay.png` | `f96ff78` (M1.7) | Final M1 state: the scene plus the debug overlay (title, FPS/frame time, renderer, GL version, GPU, window size, camera pose, draw stats, controls). |

Earlier images show the program as it was at their commit; the current `island-preview` renders what `004` shows.

Not captured automatically: interactive flight. Driving the window with injected OS input was tried, but Windows does not let a background process bring the window to the foreground, so no input was sent. Keyboard/mouse handling is covered by integration tests (SDL events → `InputState`) and unit tests (key mapping → camera movement); flying with WASD and the mouse is a manual check.
