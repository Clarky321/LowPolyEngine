# M2 screenshots

Real framebuffer captures made by the engine itself (`Renderer::captureFrame` → PNG, ADR 0006), not edited. Each was taken from the repository root with the Debug build of the sub-milestone listed, which renders 120 frames and saves the last one:

```powershell
build\windows-msvc-debug\bin\Debug\island-preview.exe --screenshot docs\screenshots\m2\<file>.png
```

Machine: Windows 11, NVIDIA GeForce RTX 4060, driver reporting OpenGL "4.5.0 NVIDIA 610.62", 1280×720 framebuffer, vsync on.

The sub-milestone column names the commit that added the image: `git log --grep "(M2.x)"` finds it (the image is captured before its commit exists, so no hash is written here).

| File | Sub-milestone | Shows |
|---|---|---|
| `001-materials.png` | M2.6 | The test scene drawn with materials. Shapes keep only brightness factors in their vertex colors and get their hue from a material's base color, so they look as in M1 (`docs/screenshots/m1/004-debug-overlay.png`); the ground keeps its vertex colors with a white material. New: five swatch cubes on the left share one mesh and differ only in their materials. |
| `002-textured-crate.png` | M2.7 | A textured material: the crate in front of the pyramid samples the 16×16 `assets/textures/crate.png` with nearest filtering, times the cube's gray vertex shades and a white base color. The diagonal brace runs from bottom left to top right on each face, as in the image, so the texture is not flipped. Everything else is unchanged from `001-materials.png`. |

Earlier images show the program as it was at their sub-milestone.
