# M3 screenshots

Real framebuffer captures made by the engine itself (`Renderer::captureFrame` → PNG, ADR 0006), not edited. Each was taken from the repository root with the Debug build of the sub-milestone listed, which renders 120 frames and saves the last one:

```powershell
build\windows-msvc-debug\bin\Debug\island-preview.exe --screenshot docs\screenshots\m3\<file>.png
build\windows-msvc-debug\bin\Debug\island-preview.exe --seed 42 --screenshot docs\screenshots\m3\<file>.png
```

Machine: Windows 11, NVIDIA GeForce RTX 4060, driver reporting OpenGL "4.5.0 NVIDIA 610.88", 1280×720 framebuffer, vsync on.

The sub-milestone column names the commit that added the image: `git log --grep "(M3.x)"` finds it (the image is captured before its commit exists, so no hash is written here).

| File | Sub-milestone | Shows |
|---|---|---|
| `001-floating-archipelago.png` | M3.8 | The first generated world: the default settings (seed 1), 12 of 12 islands, 163 584 triangles, generated in about 420 ms in the Debug build. The start camera overlooks the archipelago from above and to the side. Islands float at different heights with free space between them; each has an irregular outline, a top with hills, ridges or terraces in its own palette (Meadow, Teal moss, Lavender dusk, Autumn, Sandstone), a rocky rim and a dark stone underside in strata. The sky is a dome with a gradient from data: blue at the zenith, a warm horizon, a lavender haze and a deep blue abyss below; there is no ground and no sea. A warm sun from above and to the side, cool ambient light. |
| `002-seed-42.png` | M3.8 | The same program with `--seed 42`: another layout (islands further apart and smaller on average), other palettes (more Autumn and Sandstone), same sky and light. |

Earlier images show the program as it was at their sub-milestone.
