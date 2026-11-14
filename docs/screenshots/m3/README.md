# M3 screenshots

Real framebuffer captures made by the engine itself (`Renderer::captureFrame` → PNG, ADR 0006), not edited. Each was taken from the repository root with the Debug build of the sub-milestone listed, which renders 120 frames and saves the last one:

```powershell
build\windows-msvc-debug\bin\Debug\island-preview.exe --screenshot docs\screenshots\m3\<file>.png
build\windows-msvc-debug\bin\Debug\island-preview.exe --seed 42 --screenshot docs\screenshots\m3\<file>.png
build\windows-msvc-debug\bin\Debug\island-preview.exe --seed 19 --island 9 --screenshot docs\screenshots\m3\<file>.png
build\windows-msvc-debug\bin\Debug\island-preview.exe --panel --screenshot docs\screenshots\m3\<file>.png
```

From M3.10 on, the clouds drift during the 120 frames (about 1.2 s at the 100 frames per second of the Debug build), a metre or two along the wind.

Machine: Windows 11, NVIDIA GeForce RTX 4060, driver reporting OpenGL "4.5.0 NVIDIA 610.88", 1280×720 framebuffer, vsync on.

The sub-milestone column names the commit that added the image: `git log --grep "(M3.x)"` finds it (the image is captured before its commit exists, so no hash is written here).

| File | Sub-milestone | Shows |
|---|---|---|
| `001-floating-archipelago.png` | M3.8 | The first generated world: the default settings (seed 1), 12 of 12 islands, 163 584 triangles, generated in about 420 ms in the Debug build. The start camera overlooks the archipelago from above and to the side. Islands float at different heights with free space between them; each has an irregular outline, a top with hills, ridges or terraces in its own palette (Meadow, Teal moss, Lavender dusk, Autumn, Sandstone), a rocky rim and a dark stone underside in strata. The sky is a dome with a gradient from data: blue at the zenith, a warm horizon, a lavender haze and a deep blue abyss below; there is no ground and no sea. A warm sun from above and to the side, cool ambient light. |
| `002-seed-42.png` | M3.8 | The same program with `--seed 42`: another layout (islands further apart and smaller on average), other palettes (more Autumn and Sandstone), same sky and light. |
| `003-island-water.png` | M3.9 | Local water, `--seed 19 --island 9`: the start camera beside one island's waterfall. On its Autumn top a lake lies in a hollow and a stream runs from a spring on the higher ground to the rim, where it falls as a waterfall: a light ribbon that curves away from the island along the path of falling water and fades out far below the underside. The water is translucent and drawn after the islands; lakes are lighter where they are shallow. Neighbouring islands, one with water of its own, float around it. The world has 17 lakes and ponds, 8 streams and 5 waterfalls on 12 islands. |
| `004-meadow-waterfall.png` | M3.9 | `--seed 15 --island 9`: a Meadow island with water in hollows of its top, a stream to the rim and its waterfall; no ocean or water plane anywhere, only water that belongs to an island. The world has 9 lakes and ponds, 6 streams and 3 waterfalls. |
| `005-clouds.png` | M3.10 | The default world (seed 1) as in `001`, now with 20 low-poly cloud clusters around and between the islands at different heights, generated in about 530 ms in the Debug build, 172 264 triangles. Each cluster is a few flat-bottomed puffs, stretched along its own axis, white where its faces look up and lavender blue underneath; small clusters far away read as wisps, the large one in front of the middle islands as a cumulus. The clouds keep their distance from the islands and drift slowly along the wind. |
| `006-clouds-seed-42.png` | M3.10 | `--seed 42`, like `002`, with its clouds: clusters fill the air between the islands at many heights; two of the five waterfalls of this world hang below the central islands. |
| `007-worldgen-panel.png` | M3.12 | `--panel`: the default world with the WorldGen panel (Dear ImGui, F2) at the right: the seed with steps, Regenerate, Revert (off while nothing has changed) and Defaults, the settings in nine collapsible groups with the Islands group open (count, altitude and radius ranges, spacings as drag boxes within their limits), and the settings file to save and load. Every field of the world settings can be edited; Regenerate builds the world again in place. The panel is drawn over the frame, so it is in the capture. |
| `008-seed-7.png` | M3.13 | The finished program with `--seed 7`: twelve islands spread wide through the air, a large Autumn island on the left, Meadow, Teal moss and Sandstone tops elsewhere, a waterfall at the right edge, clouds of every size between them; 15 lakes and ponds, 6 streams, 3 waterfalls, 172 954 triangles. |
| `009-seed-2026.png` | M3.13 | `--seed 2026`: a different layout with islands stacked above each other (a Teal moss island floating over an Autumn one on the left, Sandstone over Lavender dusk in front) and a waterfall on the island at the upper left; 15 lakes and ponds, 6 streams, 2 waterfalls, 173 022 triangles. Seeds 1 (`005`), 42 (`006`), 7 and 2026 together show how far worlds differ. |

Earlier images show the program as it was at their sub-milestone.
