# Screenshots of the Raven final validation

Frames saved by the prototype itself (`renderer.captureFrame()` after Raven's and Dear ImGui's drawing), not screen captures. Report: `../raven-final-validation.md`.

| File | What | Machine | Command (repository root) |
|---|---|---|---|
| `001-windows-150-imgui.png` | The editor shell at a display scale of 150 % set in Windows, with Dear ImGui's debug UI next to Raven; saved at the first phase of the real input check | Windows 11, MSVC 19.51, RTX 4060, driver 610.88, display 150 % | `.venv\Scripts\python sandbox\raven-evaluation\input_driver.py build\raven-eval-release\bin\Release\raven-evaluation.exe --with-imgui --screenshot docs\experiments\raven-final-validation\001-windows-150-imgui.png` |
| `002-windows-150-editor.png` | The editor shell at 150 %, without Dear ImGui (the inspector's fields run past the narrow panel: the prototype's layout, see the report) | as above | `build\raven-eval-release\bin\Release\raven-evaluation.exe --frames 90 --screenshot docs\experiments\raven-final-validation\002-windows-150-editor.png` |
| `003-linux-150-imgui.png` | The same on Linux at 150 % (Xvfb, Mesa llvmpipe, no GPU; the tooltip is the pointer XTEST left over the viewport) | GitHub Actions `ubuntu-24.04`, GCC 13.3, Mesa llvmpipe | `.github/workflows/raven-validation.yml`, step "Real input at 150 %", artifact `raven-linux-release` (`raven-logs/x11-150.png`) |

The statistics in the toolbar are those of the first second (shader compilation, the first layouts), not steady frames.
