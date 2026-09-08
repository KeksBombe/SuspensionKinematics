#!/usr/bin/env python3
"""Generate the STL fixtures the unit tests read.

Standard library only, no network: the files are synthesised here so the test
suite is self-contained on any machine.

Usage: make_test_stl.py [output-directory]
"""

from __future__ import annotations

import math
import pathlib
import struct
import sys

Vec = tuple[float, float, float]
Tri = tuple[Vec, Vec, Vec]

BINARY_HEADER_SIZE = 84
BINARY_FACET_SIZE = 50


# --------------------------------------------------------------------------- #
# geometry
# --------------------------------------------------------------------------- #
def cube(size: float = 10.0) -> list[Tri]:
    """Axis-aligned cube: 12 triangles, 8 unique vertices, 18 unique edges.

    12 of those edges are the cube's real 90-degree edges and 6 are coplanar
    face diagonals, which makes it a precise test of crease detection.
    """
    s = size / 2.0
    v = [
        (-s, -s, -s), (+s, -s, -s), (+s, +s, -s), (-s, +s, -s),
        (-s, -s, +s), (+s, -s, +s), (+s, +s, +s), (-s, +s, +s),
    ]
    quads = [
        (0, 3, 2, 1),  # bottom, -Z
        (4, 5, 6, 7),  # top, +Z
        (0, 1, 5, 4),  # -Y
        (2, 3, 7, 6),  # +Y
        (1, 2, 6, 5),  # +X
        (3, 0, 4, 7),  # -X
    ]
    tris: list[Tri] = []
    for a, b, c, d in quads:
        tris.append((v[a], v[b], v[c]))
        tris.append((v[a], v[c], v[d]))
    return tris


def icosphere(radius: float = 25.0, subdivisions: int = 3) -> list[Tri]:
    """Subdivided icosahedron. Smooth, so almost none of its edges are creases."""
    t = (1.0 + math.sqrt(5.0)) / 2.0
    base = [
        (-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0),
        (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
        (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1),
    ]
    faces = [
        (0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11),
        (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
        (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9),
        (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1),
    ]

    def unit(p: Vec) -> Vec:
        length = math.sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2])
        return (p[0] / length, p[1] / length, p[2] / length)

    tris = [(unit(base[a]), unit(base[b]), unit(base[c])) for a, b, c in faces]
    for _ in range(subdivisions):
        nxt: list[Tri] = []
        for a, b, c in tris:
            ab = unit(((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2))
            bc = unit(((b[0] + c[0]) / 2, (b[1] + c[1]) / 2, (b[2] + c[2]) / 2))
            ca = unit(((c[0] + a[0]) / 2, (c[1] + a[1]) / 2, (c[2] + a[2]) / 2))
            nxt += [(a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)]
        tris = nxt
    return [tuple(tuple(co * radius for co in p) for p in tri) for tri in tris]  # type: ignore


def facet_normal(tri: Tri) -> Vec:
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = tri
    ux, uy, uz = bx - ax, by - ay, bz - az
    vx, vy, vz = cx - ax, cy - ay, cz - az
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    return (0.0, 0.0, 0.0) if length == 0.0 else (nx / length, ny / length, nz / length)


# --------------------------------------------------------------------------- #
# serialisation
# --------------------------------------------------------------------------- #
def ascii_body(tris: list[Tri], zero_normals: bool = False) -> str:
    out: list[str] = []
    for tri in tris:
        n = (0.0, 0.0, 0.0) if zero_normals else facet_normal(tri)
        out.append("  facet normal %.6e %.6e %.6e\n" % n)
        out.append("    outer loop\n")
        for p in tri:
            out.append("      vertex %.6e %.6e %.6e\n" % p)
        out.append("    endloop\n")
        out.append("  endfacet\n")
    return "".join(out)


def write_ascii(path: pathlib.Path, tris: list[Tri], name: str = "test") -> None:
    path.write_text("solid %s\n%sendsolid %s\n" % (name, ascii_body(tris), name),
                    encoding="ascii")


def write_binary(path: pathlib.Path, tris: list[Tri], header: bytes,
                 claim_count: int | None = None) -> None:
    """Write a binary STL. `claim_count` overrides the header count to build a
    deliberately truncated file."""
    blob = bytearray(header[:80].ljust(80, b"\0"))
    blob += struct.pack("<I", claim_count if claim_count is not None else len(tris))
    for tri in tris:
        blob += struct.pack("<3f", *facet_normal(tri))
        for p in tri:
            blob += struct.pack("<3f", *p)
        blob += struct.pack("<H", 0)
    path.write_bytes(bytes(blob))


def write_overflow_ascii(path: pathlib.Path, tris: list[Tri]) -> bool:
    """Craft a valid ASCII STL that a 32-bit format check misidentifies as binary.

    The size test is `size == 84 + 50 * count`, with `count` read from offset 80.
    In a text file those four bytes are just characters, and 50x their value
    overflows 32 bits -- so we solve for a value whose *wrapped* product equals
    the file size exactly. A reader that widens to 64 bits sees the true product
    (~1e11 bytes), rejects it, and parses the file as the ASCII it is; a reader
    that does not will run the binary parser over text.

    The solved bytes are hidden inside the `solid` name, which is free-form, so
    the file stays a legitimate ASCII STL.
    """
    name = list("O" * 100)  # long enough that offsets 80..83 land inside the name
    prefix = "solid "
    name_offset = len(prefix)
    body = ascii_body(tris)

    inv25 = pow(25, -1, 2 ** 31)

    for pad in range(0, 6000):
        content = prefix + "".join(name) + "\n" + body + "endsolid\n" + " " * pad
        size = len(content)
        if (size - BINARY_HEADER_SIZE) % 2 != 0:
            continue
        half = (size - BINARY_HEADER_SIZE) // 2
        root = (half * inv25) % (2 ** 31)
        for candidate in (root, root + 2 ** 31):
            # Sanity: this must be a genuine 32-bit collision.
            if (BINARY_HEADER_SIZE + BINARY_FACET_SIZE * candidate) % (2 ** 32) != size:
                continue
            raw = struct.pack("<I", candidate)
            if not all(0x21 <= b <= 0x7E for b in raw):
                continue
            for i, byte in enumerate(raw):
                name[80 - name_offset + i] = chr(byte)
            final = prefix + "".join(name) + "\n" + body + "endsolid\n" + " " * pad
            assert len(final) == size, "padding solve drifted"
            path.write_text(final, encoding="ascii")
            return True
    return False


# --------------------------------------------------------------------------- #
def main() -> int:
    out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "tests/data")
    out.mkdir(parents=True, exist_ok=True)

    box = cube()
    write_ascii(out / "cube_ascii.stl", box, name="cube")
    write_binary(out / "cube_bin.stl", box, b"cube binary")

    # A binary file whose free-form header opens with "solid": the reason a
    # leading-token sniff cannot be trusted.
    write_binary(out / "solid_header_bin.stl", box,
                 b"solid this header is a lie, the payload is binary")

    write_binary(out / "sphere_bin.stl", icosphere(), b"icosphere binary")

    # Zero-area triangles alongside good ones, plus zeroed facet normals.
    degenerate: list[Tri] = list(box)
    degenerate.append(((0.0, 0.0, 20.0), (0.0, 0.0, 20.0), (5.0, 0.0, 20.0)))   # repeated vertex
    degenerate.append(((0.0, 0.0, 30.0), (5.0, 0.0, 30.0), (10.0, 0.0, 30.0)))  # collinear
    write_ascii(out / "degenerate.stl", degenerate, name="degenerate")
    # Same file again but with every stored normal zeroed, proving normals are
    # recomputed rather than trusted.
    (out / "zero_normals.stl").write_text(
        "solid zeroed\n%sendsolid zeroed\n" % ascii_body(box, zero_normals=True),
        encoding="ascii")

    # Header claims 200 triangles, only 6 are present.
    write_binary(out / "truncated.stl", box[:6], b"truncated binary", claim_count=200)

    if not write_overflow_ascii(out / "overflow_ascii.stl", box):
        print("warning: could not craft overflow_ascii.stl", file=sys.stderr)

    for f in sorted(out.glob("*.stl")):
        print("  %-24s %8d bytes" % (f.name, f.stat().st_size))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
