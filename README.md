# SuspensionKinematics

Native suspension-kinematics tool for Bremergy, running on Linux and Windows.

This milestone is the 3D viewport foundation: import geometry and inspect it, as a
shaded solid or as its triangle mesh, with a camera that behaves the way a CAD user
expects. Hardpoint editing, bump/roll sweeps and camber/toe plots build on top of it.

## Download

Every push to `main` republishes the [`latest` release](https://github.com/KeksBombe/SuspensionKinematics/releases/tag/latest).
The tag is moved rather than accumulated, so these links always point at the
current build.

| Platform | File | Install |
|---|---|---|
| Windows 10/11 x64 | `SuspensionKinematics-windows-x64-setup.exe` | Run it. Qt and Open CASCADE are bundled. |
| Windows 10/11 x64 | `SuspensionKinematics-windows-x64.zip` | Portable — unzip and run `suspkin.exe`. |
| Arch Linux x86_64 | `suspensionkinematics-*-x86_64.pkg.tar.zst` | `sudo pacman -U <file>` |

Packaging lives in `packaging/` and is driven by `.github/workflows/release.yml`.

## Quick start (Linux)

```bash
./start.sh                                  # rebuild and launch
./start.sh tests/fixtures/plate_with_hole.step
SUSPKIN_PRESET=linux-release ./start.sh     # optimised build
```

## Supported formats

| Format | Notes |
|---|---|
| **STL**, binary and ASCII | Auto-detected from the file size, not the leading `solid` token, which is unreliable in both directions. Facet normals in the file are ignored and recomputed from the winding. |
| **STEP** (`.step`, `.stp`) | ISO 10303 boundary representation. Read and tessellated with Open CASCADE; the chord tolerance scales with the model, so a 10 mm bracket and a 3 m chassis both come out sensibly. The **exact analytic surface normal** is evaluated at every vertex from the B-Rep, so curved faces shade smoothly rather than as flat facets. Optional at build time. |

Both formats end up in the same triangle mesh for rendering — a GPU draws nothing
else. The difference that matters is the normals: an STL is genuinely faceted and
is shaded flat, while a STEP body is analytic surfaces that merely got tessellated,
so its normals come from the real surface and a bore shades as the cylinder it is.

## Controls

| Input | Action |
|---|---|
| Left-drag | Orbit |
| Middle-drag, or Shift+left-drag | Pan |
| Wheel | Zoom |
| Click an axis ball on the gizmo | Snap to that view |
| Drag the gizmo | Orbit |
| `1`–`7` | Front, Rear, Left, Right, Top, Bottom, Isometric |
| `F` | Fit to view |
| `Ctrl+1` / `Ctrl+2` | Solid / Triangles |
| `Ctrl+O` / `Ctrl+W` / `Ctrl+Q` | Import / Close model / Quit |

Display mode and the navigation gizmo also sit in the viewport's top-right corner.

Coordinate convention is **ISO 8855 / DIN 70000**: X forward, Y left, Z up,
right-handed — the convention the vehicle-dynamics literature uses, so camber and
toe signs will come out consistent when the solver lands.

## Building

Requires **Qt 6.5+**, **CMake 3.24+** and a C++20 compiler.

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

### STEP support (optional)

STEP is a boundary representation — trimmed NURBS surfaces, not triangles — so
reading it needs a geometry kernel. The build uses Open CASCADE 7.5+ when it can
find one, and otherwise still builds and runs, reporting a clear message if you
open a STEP file.

The Linux presets look for a repo-local kernel at `.nix/occt` first, then fall
back to anything on `CMAKE_PREFIX_PATH`. To provision it without root, matching
the nix pattern already used elsewhere in this tree:

```bash
nix build --out-link .nix/occt nixpkgs#opencascade-occt
```

Or install it system-wide (`pacman -S opencascade`) and pass
`-DOpenCASCADE_DIR=/usr/lib/cmake/opencascade`.

> Note: Open CASCADE ships an *exact*-match version config, so `find_package`
> must not be given a minimum version — the build checks the version afterwards
> instead.

### Windows

Install Visual Studio 2022 (Desktop C++ workload) and Qt 6 `msvc2022_64`, set
`QT_ROOT_DIR` to the Qt kit, then:

```
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
```

For STEP on Windows, point `OpenCASCADE_DIR` at an OCCT install.

There is also a `windows-ninja` preset, which builds with Ninja from a
"x64 Native Tools" prompt instead of generating a Visual Studio solution. It is
what CI uses, because it does not pin a Visual Studio version the way the
`windows-msvc` generator string does.

Packaging is handled by `.github/workflows/release.yml`; see **Download** above.

### A note on CMake on this machine

The STM32CubeCLT toolchain puts **CMake 3.28.1** on `PATH` ahead of the system's
**4.4.1**. `CMakeLists.txt` uses the range form `3.24...3.28` so both produce
identical builds.

## Layout

```
src/geom/    Aabb, TriMesh, vertex welding and edge extraction
src/io/      STL and STEP readers behind one importMeshFile() entry point
src/render/  Camera, GPU buffers, the OpenGL viewport, gizmo and mode selector
src/app/     MainWindow, menus, file handling
tests/       Qt Test suites; STL fixtures are generated, STEP ones committed
tools/       make_test_stl.py -- stdlib-only STL fixture generator
```

`suspkin_core` is a static library with no OpenGL and no widgets, so the readers,
the mesh topology and the camera are all unit-testable headlessly.

## Development notes

- `--screenshot <file>` renders one frame and exits; used to verify the renderer
  without a human looking at the window, and usable from CI.
- `--mode solid|triangles` sets the initial display mode.
