# M5 screenshots

Real framebuffer captures made by the editor itself (`lpe-editor --screenshot`: `Renderer::captureFrame` → PNG, ADR 0006), not edited. Each was taken from the repository root with the Debug build of the sub-milestone listed; without `--frames`, the editor renders 30 frames and saves the last one:

```powershell
build\windows-msvc-debug\bin\Debug\lpe-editor.exe --screenshot docs\screenshots\m5\<file>.png
```

From M5.6 on, an image of a project names the project folder first, e.g. for `003-scene-entities.png`:

```powershell
build\windows-msvc-debug\bin\Debug\lpe-editor.exe tests\projects\lighthouses --screenshot docs\screenshots\m5\003-scene-entities.png
```

From M5.7 on, the console scrolls smoothly to each new line, so the run is longer and the console has settled when the frame is saved, e.g. for `004-hierarchy-console.png`:

```powershell
build\windows-msvc-debug\bin\Debug\lpe-editor.exe tests\projects\lighthouses --frames 90 --screenshot docs\screenshots\m5\004-hierarchy-console.png
```

`005-inspector.png` needs an entity selected, and the editor has no option for that (no UI is added just for screenshots): while it ran, one real left click on the row "Floating lighthouse" (client point 92, 205), sent with Windows' `SendInput`, selected it:

```powershell
build\windows-msvc-debug\bin\Debug\lpe-editor.exe tests\projects\lighthouses --frames 480 --screenshot docs\screenshots\m5\005-inspector.png
```

Machine: Windows 11, NVIDIA GeForce RTX 4060, driver reporting OpenGL "4.5.0 NVIDIA 610.88" (hardware OpenGL), display scale 100 %, 1280×760 framebuffer, vsync on.

The sub-milestone column names the commit that added the image: `git log --grep "(M5.x)"` finds it (the image is captured before its commit exists, so no hash is written here).

| File | Sub-milestone | Shows |
|---|---|---|
| `001-editor-shell.png` | M5.2 | The empty editor shell, drawn by Raven 0.2.3 through the engine's OpenGL backend on the engine's SDL3 window (ADR 0027). A toolbar strip at the top (empty), then Raven's DockArea with four titled panels: Hierarchy on the left (a fifth of the width), the Viewport in the middle (black: nothing is drawn into it before M5.3), the Inspector on the right and the Console below the viewport. Every panel is empty. The run had 0 OpenGL errors, 0 OpenGL state mismatches, 0 Raven errors and 0 Raven allocations after shutdown. |
| `002-world-viewport.png` | M5.3 | The generated archipelago of the default world settings (seed 1: 12 of 12 islands, 172 264 triangles, 20 cloud clusters) in the editor's viewport, under the sky dome, seen from the overview camera, as island-preview shows it. The engine's renderer draws it from the viewport's draw callback inside Raven's frame (`engine/world/`, ADR 0027), clipped to the viewport: the hierarchy, inspector and console around it stay as Raven draws them. The viewport is 790×497 pixels at (203, 71). The run had 0 OpenGL errors, 0 OpenGL state mismatches, 0 Raven errors and 0 Raven allocations after shutdown. |
| `003-scene-entities.png` | M5.6 | The test project `tests/projects/lighthouses` opened by `lpe-editor` at start: its world settings (the default ones, so the same archipelago and overview as `002`) and its scene's five mesh entities, drawn by `engine/world`'s scene view inside the world (ADR 0027 Amendment 2). Four lighthouses stand on the tops of islands, at scales 4 and 5: on the large purple island in front, on the high teal island, on the far purple island and on the island at the left. The fifth floats between the islands, tilted 25 degrees and stretched to twice its height, its top in a cloud. All five share one mesh, the engine's lighthouse, drawn with the default material (white, lit) in the scene's evening sun; the islands keep the archipelago's sun. The camera entity draws nothing. The run read the scene once, uploaded one mesh, and had 0 OpenGL errors, 0 OpenGL state mismatches, 0 Raven errors and 0 Raven allocations after shutdown. |
| `004-hierarchy-console.png` | M5.7 | The test project `tests/projects/lighthouses` in the editor with its hierarchy and console filled (ADR 0030), the viewport as in `003`. The hierarchy lists the scene's seven entities in creation order: the evening sun, the lighthouse on the near island with the harbour camera under it, the lighthouse on the high island with the floating lighthouse under it, and the lighthouses on the far and the west island; the two parents have their branch arrows open. Create is enabled, Delete disabled since nothing is selected. The console shows the end of the run's log, which the editor received from its first line: the archipelago, the scene's five drawn lighthouses, the viewport's controls, and three debug lines of the OpenGL driver, dimmed (a Debug build). Its checkboxes count the lines by level: no error, no warning, 7 info lines, 3 debug lines. The run had 0 OpenGL errors, 0 OpenGL state mismatches, 0 Raven errors and 0 Raven allocations after shutdown. |
| `005-inspector.png` | M5.8 | The test project with its floating lighthouse selected by a real click on its row in the hierarchy, which shows the row selected. The inspector shows the entity's ID, then a section per component: its name in a text field; its transform, the position (60, 60, 70), the rotation as Euler angles in degrees (pitch 0, yaw 0, roll 25: the 25 degrees about Z the scene stores as a quaternion) and the scale (3, 6, 3), each vector as three spin boxes; its static mesh, `models/lighthouse.obj` with the default material, chosen from lists; its parent, the lighthouse on the high island, shown only. Each editable section has its Remove button; the Add list offers the components it lacks, Camera first. The run had 0 OpenGL errors, 0 OpenGL state mismatches, 0 Raven errors and 0 Raven allocations after shutdown. |

Earlier images show the program as it was at their sub-milestone.
