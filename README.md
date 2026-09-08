# SuspensionKinematics

Native suspension-kinematics tool for Bremergy, running on Linux and Windows.

Import geometry and inspect it, as a shaded solid or as its triangle mesh, with a
camera that behaves the way a CAD user expects; import the suspension hardpoints
from the spreadsheet they already live in, see them in the viewport, edit them and
write them straight back. Bump/roll sweeps and camber/toe plots build on top of it.

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
| **XLSX** (`.xlsx`) | Hardpoint coordinates. Read and written in place — see [Hardpoints](#hardpoints). |

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
| `Ctrl+O` / `Ctrl+W` / `Ctrl+Q` | Import geometry / Close model / Quit |
| Click a marker | Select that hardpoint, in the viewport and in the table |
| `Ctrl+I` | Import hardpoints |
| `Ctrl+S` / `Ctrl+Shift+S` | Save hardpoints / Save hardpoints as |
| `Ctrl+L` | Show or hide the hardpoint labels |

Display mode and the navigation gizmo also sit in the viewport's top-right corner.

Coordinate convention is **ISO 8855 / DIN 70000**: X forward, Y left, Z up,
right-handed — the convention the vehicle-dynamics literature uses, so camber and
toe signs will come out consistent when the solver lands.

## Hardpoints

Hardpoints are read from an `.xlsx` workbook laid out as a **Name / Value** table,
one coordinate per row, with the axis as a suffix on the name:

| Name | Value |
|---|---|
| `F_LCA_O_x` | -544.26 |
| `F_LCA_O_y` | 554.412 |
| `F_LCA_O_z` | 138.408 |

Those three rows become the point `F_LCA_O`. Units are millimetres, in the same
ISO 8855 frame as the geometry, so hardpoints and an imported chassis line up
without anything being moved. The reader finds the header row rather than assuming
column A and B, takes the first sheet that actually contains such a table, and
reports the rows it could not use instead of dropping them silently. Separators
`_`, `.` and `-` all work, and a `Wert` column header is recognised alongside
`Value`.

The points show up in the viewport as labelled markers, drawn at a constant size
on screen — a hardpoint is a coordinate, not an object with a size. One hidden
behind geometry still shows through, dimmed, because that is usually the moment
somebody goes looking for it. Labels that would collide are dropped, except for
the one under the cursor and the one that is selected. Selecting in the table and
selecting in the viewport are the same selection.

### Saving

**Save** (`Ctrl+S`) writes back to the workbook that was imported, after a
dialog that names the file and says it is about to be replaced. **Save As**
writes a new workbook, leaving the original alone — the way to keep a variant.

Saving rewrites the value cells and *nothing else*. The workbook is unzipped,
those cells are spliced in as bytes, and every other part is copied through with
its original compressed data untouched, so number formats, column widths, other
sheets, charts, and vendor parts a spreadsheet library would not understand all
survive exactly. A coordinate nobody edited is written back with the very text
the file already held, so saving an unmodified import is a no-op on the cells.
An edited one is written at the shortest precision that still reads back as the
same double, rather than as seventeen digits. Workbooks that carry a `calcPr`
element are marked to recalculate on load, so formulas elsewhere that read these
cells do not show stale results.

Coordinates are carried as doubles from the file to the viewport and back;
only the renderer sees floats. Editing is limited to coordinates — names are the
key the cells are addressed by, so renaming one here would either rewrite cells
nobody looked at or quietly break the round trip.

## Building

Requires **Qt 6.5+**, **CMake 3.24+** and a C++20 compiler.

An `.xlsx` is a ZIP of XML parts, so reading one needs zlib. The build uses the
system zlib when there is one — every Linux has it — and otherwise downloads and
builds the release itself, so a Windows Qt kit with no zlib in sight still
configures without anything being installed by hand.

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
src/model/   Hardpoint and HardpointTable -- the coordinates themselves
src/io/      STL and STEP readers behind one importMeshFile() entry point,
             a minimal ZIP reader/rewriter, and the hardpoint workbook reader
src/render/  Camera, GPU buffers, the OpenGL viewport, gizmo and mode selector
src/app/     MainWindow, menus, file handling, the hardpoint table and dock
tests/       Qt Test suites; STL and XLSX fixtures are generated, STEP committed
tools/       make_test_stl.py, make_test_xlsx.py -- stdlib-only fixture generators
```

`suspkin_core` is a static library with no OpenGL and no widgets, so the readers,
the mesh topology and the camera are all unit-testable headlessly.

## Development notes

- `--screenshot <file>` renders one frame and exits; used to verify the renderer
  without a human looking at the window, and usable from CI.
- `--mode solid|triangles` sets the initial display mode.
- Files on the command line are dispatched by extension, so
  `./start.sh chassis.step hardpoints.xlsx` opens both, in either order.
