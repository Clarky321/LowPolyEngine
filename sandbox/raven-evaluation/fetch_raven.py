"""Fetches Raven at the commit the evaluation pins, for sandbox/raven-evaluation.

Usage: python sandbox/raven-evaluation/fetch_raven.py [DESTINATION]

DESTINATION defaults to build/external/raven (ignored by Git). The script only
fetches and checks out the pinned commit; it builds nothing and runs nothing
from the checkout. An existing checkout is reused if it is at that commit.
See docs/experiments/raven-ui-evaluation.md.
"""

import pathlib
import subprocess
import sys

REPOSITORY = "https://github.com/oscar-soirey/Raven"
COMMIT = "7ab270df3b618de3d11af6033912374b6cd621b7"


def git(folder: pathlib.Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", *arguments], cwd=folder, check=True, capture_output=True, text=True
    )
    return result.stdout.strip()


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[2]
    destination = (
        pathlib.Path(sys.argv[1]).resolve()
        if len(sys.argv) > 1
        else root / "build" / "external" / "raven"
    )
    if (destination / ".git").is_dir():
        current = git(destination, "rev-parse", "HEAD")
        if current == COMMIT:
            print(f"Raven {COMMIT} already checked out in {destination}")
            return 0
        print(f"{destination} holds {current}, not {COMMIT}; remove it first", file=sys.stderr)
        return 1
    if destination.exists() and any(destination.iterdir()):
        print(f"{destination} exists and is not empty", file=sys.stderr)
        return 1
    destination.mkdir(parents=True, exist_ok=True)
    git(destination, "init", "--quiet")
    git(destination, "remote", "add", "origin", REPOSITORY)
    git(destination, "fetch", "--quiet", "--depth", "1", "origin", COMMIT)
    git(destination, "-c", "advice.detachedHead=false", "checkout", "--quiet", "FETCH_HEAD")
    current = git(destination, "rev-parse", "HEAD")
    if current != COMMIT:
        print(f"fetched {current}, expected {COMMIT}", file=sys.stderr)
        return 1
    print(f"Raven {COMMIT} checked out in {destination}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
