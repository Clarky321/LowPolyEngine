"""Regenerate the vendored GLAD 2 OpenGL loader in third_party/glad/.

Run from any folder with the project's virtual environment:
    .venv\\Scripts\\python scripts\\python\\generate_glad.py      (Windows)
    .venv/bin/python scripts/python/generate_glad.py            (Linux)

Why vendored instead of the Conan package: see docs/adr/0005-vendored-glad.md.
The generator version is pinned in requirements-dev.txt. "--reproducible" makes
glad use the Khronos XML bundled with that version instead of downloading the
latest one, so a given generator version always produces the same files.
"""

import shutil
import subprocess
import sys
from importlib import metadata
from pathlib import Path

GLAD_VERSION = "2.0.8"
API = "gl:core=4.5"
# OpenGL 4.5 core already contains everything the renderer uses, including the
# debug-output functions (core since 4.3), so no extensions are generated.
EXTENSIONS = ""

GENERATED_DIRS = ("include", "src")


def main() -> int:
    try:
        installed = metadata.version("glad2")
    except metadata.PackageNotFoundError:
        print("error: glad2 is not installed; run: pip install -r requirements-dev.txt",
              file=sys.stderr)
        return 1
    if installed != GLAD_VERSION:
        print(f"error: glad2 {GLAD_VERSION} required, found {installed}", file=sys.stderr)
        return 1

    out_dir = Path(__file__).resolve().parents[2] / "third_party" / "glad"
    # Remove only generated folders; README.md, LICENSE and CMakeLists.txt are hand-written.
    for name in GENERATED_DIRS:
        shutil.rmtree(out_dir / name, ignore_errors=True)

    command = [
        sys.executable, "-m", "glad",
        "--quiet",
        "--reproducible",
        f"--api={API}",
        f"--extensions={EXTENSIONS}",
        f"--out-path={out_dir}",
        "c",
    ]
    result = subprocess.run(command, check=False)
    if result.returncode != 0:
        print(f"error: glad exited with code {result.returncode}", file=sys.stderr)
        return result.returncode

    print(f"generated GLAD {GLAD_VERSION} ({API}, extensions: none) in {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
