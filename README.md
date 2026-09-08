# SuspensionKinematics

Native suspension-kinematics tool for Bremergy, running on Linux and Windows.

Import geometry and inspect it, as a shaded solid or as its triangle mesh, with a
camera that behaves the way a CAD user expects; import the suspension hardpoints
from the spreadsheet they already live in, see them in the viewport, edit them,
mirror them to the other side, and write them straight back. Bump/roll sweeps and
camber/toe plots build on top of it.

Work lives in a **project**: the tool opens with a list of them, and everything
you do from then on — the files you import, the coordinates you change, the view
you left it in — is kept there. See [Projects](#projects).

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
./start.sh                                  # rebuild and launch the project list
./start.sh ~/projects/Bremergy26            # rebuild and open a project
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
| `Ctrl+N` / `Ctrl+Shift+O` | New project / Open project |
| `Ctrl+S` | Save the project now (it also saves itself) |
| `Ctrl+O` / `Ctrl+W` / `Ctrl+Q` | Import geometry / Remove geometry / Quit |
| Click a marker | Select that hardpoint, in the viewport and in the table |
| `Ctrl+I` | Import hardpoints |
| `Ctrl+M` | Mirror hardpoints to the other side |
| `Ctrl+Shift+S` / `Ctrl+E` | Overwrite the workbook / Export a workbook |
| `Ctrl+L` | Show or hide the hardpoint labels |

Display mode and the navigation gizmo also sit in the viewport's top-right corner.

Coordinate convention is **ISO 8855 / DIN 70000**: X forward, Y left, Z up,
right-handed — the convention the vehicle-dynamics literature uses, so camber and
toe signs will come out consistent when the solver lands.

## Projects

The tool opens with a list of your projects. Pick one, create one — you name it
and browse to where it should go — or open one from anywhere on disk.

A project is a folder:

```
Bremergy26/
  project.suspkin        what the project is, as readable JSON
  geometry/
    upright.step         a copy of the geometry you imported
  hardpoints/
    hardpoints.xlsx      a copy of the workbook you imported, as imported
    edits.json           your changes, until you write them into a workbook
  linkage/
    template.json        which parts join which hardpoints
```

Imported files are **copied into the project**, so it still opens when the
original has moved, been renamed, or lives on a share that is not mounted today.
The path it came from is remembered, but only as a note to you.

Everything else you do is kept there too, and written out on its own a moment
after you stop: which geometry and which workbook are loaded, every coordinate
you have changed, the mirroring rule you last used, the camera position, solid or
triangles, whether labels and parts are drawn, which hardpoint is selected, and
the size and layout of the window itself. Reopening a project puts you back where you were.

There is no *Save* prompt on the way out, because there is nothing unsaved.
`Ctrl+S` is there for when you want to be sure, and **File ▸ Show Project Folder**
opens it in your file manager.

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

### Mirroring

**Mirror Hardpoints** (`Ctrl+M`) copies points to the other side of the car. The
mirror plane is `y = 0`, because the frame is ISO 8855 — X and Z come through
untouched, so a mirrored corner keeps its station and its ride height. X and Z
mirrors are there as well for the rarer cases.

There is no universal convention for naming the two sides, so the rule is yours
to pick: add a suffix (`F_LCA_O` → `F_LCA_O_R`), add a prefix, or replace text
inside the name (`L_UCA_O` → `R_UCA_O`). The dialog previews the names it would
produce before anything happens, mirrors either the whole table or just the
selected point, and will not make mirrors of mirrors. The rule you used is
remembered in the project, because it is your convention and you will want it
again.

Mirrored points are ordinary hardpoints afterwards — editable, selectable, saved
in the project. They are also written into the workbook when you ask for one, as
new `name`/`value` rows below the existing table.

### Writing a workbook

Your edits sit in the project's `edits.json` until you say otherwise. The
workbook the project holds stays exactly as it was imported, so there is always a
known-good original to fall back on and to compare against. The window title and
the status bar say how many changes are waiting.

**Overwrite Workbook** (`Ctrl+Shift+S`) writes them into the project's own copy,
after a dialog that names the file. **Export Workbook As** (`Ctrl+E`) writes a new
workbook anywhere — starting from where the original came from, so pushing your
changes back to the shared file is one click, and keeping a variant is just as
easy.

Either way, writing rewrites the value cells and *nothing else*. The workbook is
unzipped, those cells are spliced in as bytes, and every other part is copied through with
its original compressed data untouched, so number formats, column widths, other
sheets, charts, and vendor parts a spreadsheet library would not understand all
survive exactly. A coordinate nobody edited is written back with the very text
the file already held, so saving an unmodified import is a no-op on the cells.
An edited one is written at the shortest precision that still reads back as the
same double, rather than as seventeen digits. Workbooks that carry a `calcPr`
element are marked to recalculate on load, so formulas elsewhere that read these
cells do not show stale results.

Points that are not in the workbook — the ones mirroring produced — are appended
as new rows under the table, in the same two columns it uses.

Coordinates are carried as doubles from the file to the viewport and back;
only the renderer sees floats. Editing is limited to coordinates — names are the
key the cells are addressed by, so renaming one here would either rewrite cells
nobody looked at or quietly break the round trip.

## Parts

A list of coordinates is hard to read as a suspension, so the tool draws the
parts between them: wishbones as closed A-arms, the pushrod, tie rod and drop
link as the two-force members they are, the upright as the triangle its three
joints make with the wheel hanging off the kingpin, the rocker as its spokes
about its pivot axis, the damper, the anti-roll bar and the wheel. Each kind has
its own colour, and what the geometry hides is drawn dimmed so a wishbone inside
a chassis panel is still findable. **Ctrl+P** turns them off.

What joins what is not hard-coded. It comes from a **linkage template**, a small
JSON file the project owns a copy of at `linkage/template.json` — an ordinary
project file, next to the workbook, that you can open and edit:

```json
{
    "id": "lowerWishbone",
    "label": "{corner} {side} lower wishbone",
    "kind": "wishbone",
    "points": ["{corner}_LCA_IF", "{corner}_LCA_O", "{corner}_LCA_IR"],
    "closed": true
}
```

A part is one or more *chains* of hardpoints, each drawn as a polyline;
`"closed": true` joins the last point back to the first, which is what makes a
wishbone an A-arm rather than two loose legs. `{corner}` stands for each entry
under `corners` — `F` and `R` in the template that ships — so a part is written
once and instantiated per axle. The other side of the car is never written out:
each part is built a second time with every name put through **your** mirror rule
(Hardpoints ▸ Mirror), so whatever convention your workbook uses is the one used
here, and `{side}` in a label becomes `left` or `right`.

`"optional": true` on a part or a chain means *say nothing when these points are
absent* — an anti-roll bar, a rocker axis point. A corner or a side with not one
of its points in the table is skipped silently, so a workbook holding one axle,
or one that has not been mirrored yet, draws exactly what it holds. Anything else
that is missing is reported once, by name, and the rest of the part is still
drawn.

A project that does not have a template gets the built-in one written into it the
first time it opens. **Parts ▸ Import Template** takes one from anywhere (it is
copied in, like every other asset), **Reset to Built-in Template** puts the
shipped one back, and **Show Template File** opens the project's copy in whatever
edits JSON on your machine.

> The template that ships assumes the **pushrod picks up on the upper wishbone**,
> which is what this workbook's numbers say: `PushRod_O` sits 21 mm (front) and
> 31 mm (rear) off the plane of the upper arm and 37–80 mm from its outer ball
> joint. If yours is mounted on the upright instead, delete the `pushRodPickup`
> part and add `{corner}_PushRod_O` to the upright's first chain.

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
src/model/   Hardpoint and HardpointTable -- the coordinates themselves -- the
             mirroring rules, and the linkage a template resolves to
src/io/      STL and STEP readers behind one importMeshFile() entry point,
             a minimal ZIP reader/rewriter, the hardpoint workbook reader, and
             the linkage template reader/writer
src/project/ the project format: manifest, copied assets, view state, edits
src/render/  Camera, GPU buffers, the OpenGL viewport, gizmo and mode selector
src/app/     MainWindow and the controller that owns it, the project launcher,
             the mirror dialog, menus, the hardpoint table and dock
tests/       Qt Test suites; STL and XLSX fixtures are generated, STEP committed
tools/       make_test_stl.py, make_test_xlsx.py -- stdlib-only fixture generators
```

`suspkin_core` is a static library with no OpenGL and no widgets, so the readers,
the mesh topology, the camera, the mirroring and the project format are all
unit-testable headlessly.

## Development notes

- A project on the command line opens it directly, skipping the project list.
  Its `project.suspkin` or the folder holding it both work.
- `--screenshot <file>` renders one frame and exits; used to verify the renderer
  without a human looking at the window, and usable from CI.
- `--mode solid|triangles` sets the initial display mode.
- `--import <file>` imports into the open project on startup, dispatched by
  extension, so `--import chassis.step --import hardpoints.xlsx` does both.
