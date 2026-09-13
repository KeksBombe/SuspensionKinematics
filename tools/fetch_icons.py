#!/usr/bin/env python3
"""Fetch Tabler icons into resources/icons/.

    tools/fetch_icons.py wheel steering-wheel table-export ...

Each name is fetched from icons/outline/<name>.svg at the pinned tag and
written to resources/icons/<name>.svg with its leading comment -- the tags and
category Tabler's own site searches by -- stripped. The licence is written once,
beside them, as LICENSE-tabler.txt, naming the tag the icons came from.

The icons are committed rather than fetched by the build: the Arch package is
built in a chroot with no network. So this runs by hand, when an icon is added,
and its output is what gets committed. A new icon also needs its name in
SUSPKIN_ICONS in CMakeLists.txt and an entry in the table in src/app/Icons.cpp;
test_icons fails until the two agree.

Standard library only, like the other generators in this directory.
"""

import pathlib
import re
import sys
import urllib.error
import urllib.request

# Pinned: every icon on the same 24 px grid and 2 px stroke, and a name that
# means the same drawing every time this is run.
TAG = "v3.46.0"
BASE = f"https://raw.githubusercontent.com/tabler/tabler-icons/{TAG}"

ROOT = pathlib.Path(__file__).resolve().parent.parent
TARGET = ROOT / "resources" / "icons"

_HEADER = re.compile(rb"\A\s*<!--.*?-->\s*", re.DOTALL)
_NAME = re.compile(r"\A[a-z0-9]+(?:-[a-z0-9]+)*\Z")


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=30) as response:
        return response.read()


def fetch_icon(name: str) -> bytes:
    svg = fetch(f"{BASE}/icons/outline/{name}.svg")
    svg = _HEADER.sub(b"", svg, count=1)
    if not svg.lstrip().startswith(b"<svg"):
        raise ValueError(f"{name}: not an SVG after the header was stripped")
    # The recolouring in Icons.cpp works by replacing this, so an icon that
    # does not draw with it would stay black on a dark desktop.
    if b"currentColor" not in svg:
        raise ValueError(f"{name}: does not draw with currentColor")
    return svg if svg.endswith(b"\n") else svg + b"\n"


def write_licence() -> None:
    licence = TARGET / "LICENSE-tabler.txt"
    if licence.exists():
        return
    text = fetch(f"{BASE}/LICENSE").decode("utf-8")
    header = (
        f"The SVG files in this directory are Tabler Icons, {TAG},\n"
        "https://github.com/tabler/tabler-icons, fetched by tools/fetch_icons.py\n"
        "with their leading comment removed and otherwise unchanged.\n\n"
    )
    licence.write_text(header + text, encoding="utf-8", newline="\n")


def main(argv: list[str]) -> int:
    names = argv[1:]
    if not names:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    bad = [name for name in names if not _NAME.match(name)]
    if bad:
        print(f"not an icon name: {', '.join(bad)}", file=sys.stderr)
        return 2

    TARGET.mkdir(parents=True, exist_ok=True)
    failed = []
    for name in names:
        try:
            svg = fetch_icon(name)
        except urllib.error.HTTPError as error:
            failed.append(f"{name}: HTTP {error.code} (no such icon at {TAG}?)")
            continue
        except (urllib.error.URLError, ValueError, TimeoutError) as error:
            failed.append(f"{name}: {error}")
            continue
        (TARGET / f"{name}.svg").write_bytes(svg)
        print(f"  {name}.svg")

    try:
        write_licence()
    except (urllib.error.URLError, TimeoutError) as error:
        failed.append(f"LICENSE: {error}")

    for line in failed:
        print(line, file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
