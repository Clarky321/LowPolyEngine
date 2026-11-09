# GLAD 2 — generated OpenGL loader

Generated code. Do not edit `include/` or `src/` by hand; regenerate instead.

| | |
|---|---|
| Generator | glad2 2.0.8 (pinned in `requirements-dev.txt`) |
| API | `gl:core=4.5` |
| Extensions | none |
| Options | C generator, no internal loader (SDL3 provides `SDL_GL_GetProcAddress`), no MX, no debug wrappers |
| Spec | Khronos XML bundled with glad2 2.0.8 (`--reproducible`) |
| License | generated code: WTFPL or CC0-1.0; Khronos specification: Apache-2.0; `khrplatform.h`: Khronos MIT-style (see file header). See `LICENSE`. |

Regenerate:

```powershell
.venv\Scripts\python scripts\python\generate_glad.py
```

Why it is vendored instead of taken from Conan: [ADR 0005](../../docs/adr/0005-vendored-glad.md).
