# SuspensionKinematics

Native suspension-kinematics tool for Bremergy, running on Linux and Windows.

Import geometry and inspect it, as a shaded solid or as its triangle mesh, with a
camera that behaves the way a CAD user expects; import the suspension hardpoints
from the spreadsheet they already live in, see them in the viewport, edit them,
mirror them to the other side, and write them straight back; hang real wheels and
rims off the wheel centres. Bump/roll sweeps and camber/toe plots build on top
of it.

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
| `Ctrl+O` / `Ctrl+W` / `Ctrl+Q` | Import chassis / Remove chassis / Quit |
| Click a marker | Select that hardpoint, in the viewport and in the table |
| `Ctrl+H` | Show or hide the hardpoint table |
| `Ctrl+I` | Import hardpoints |
| `Ctrl+M` | Mirror hardpoints to the other side |
| `Ctrl+Shift+S` / `Ctrl+E` | Overwrite the workbook / Export a workbook |
| `Ctrl+L` | Show or hide the hardpoint labels |
| `Ctrl+Shift+W` | Show or hide the wheels |

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
    edits.json           your coordinate changes, until you write them into a workbook
  linkage/
    template.json        which parts join which hardpoints
  wheels/
    tyre.step            a copy of the tyre model you imported
    rim.step             and of the rim
```

Imported files are **copied into the project**, so it still opens when the
original has moved, been renamed, or lives on a share that is not mounted today.
The path it came from is remembered, but only as a note to you.

Everything else you do is kept there too, and written out on its own a moment
after you stop: which geometry and which workbook are loaded, every coordinate
you have changed, the mirroring rule you last used, the camera position, solid or
triangles, whether labels and parts are drawn, which hardpoint is selected, what
each point is for and which bushing acts at it, and the size and layout of the
window itself. Reopening a project puts you back where you were.

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
the one under the cursor and the ones that are selected. Selecting in the table and
selecting in the viewport are the same selection, and it can be more than one
point: **Ctrl+click** in the viewport adds a point to it or takes one away, and
Ctrl or Shift does the same in the table. The viewport keeps the order they were
picked in, which is the order a new part is drawn through.

### Making points here

A point does not have to come out of a workbook.

- **Hardpoints ▸ Add Point** (`Ins`) adds one next to the selected point, starting
  at its coordinates with a name that is free.
- **Delete Point** (`Del`) deletes every selected point, after asking. A point the
  workbook holds stays in it until the workbook is overwritten; until then the
  project remembers it as deleted.
- **Rename Point**, or a double-click on the name in the table, renames one. The
  name is the key the parts, the solver and the configuration find a point by, so
  its configuration and anything mirrored from it go with it. A name has to be
  free, cannot be empty, and cannot end in `_x`, `_y` or `_z`, which is how the
  workbook marks a coordinate.
- **New Hardpoint Table** starts a project that has no workbook at all. The
  project gets one of its own, `hardpoints/hardpoints.xlsx`, made from a blank
  workbook the application carries, and from then on it is an ordinary project
  with an ordinary workbook.

A new point comes back where you put it when the project reopens, rather than at
the bottom of the table.

### Generating a corner from design targets

**Hardpoints ▸ Generate from Design** (`Ctrl+G`) works out the wishbones, the
upright and the steering from vehicle targets: wheelbase and track, weight
distribution and brake bias, loaded radius, static camber and toe, caster and
kingpin inclination, scrub radius and trail, the roll centre and the front-view
swing arm, anti-dive or anti-lift and the side-view swing arm, the ball joint
heights, the planform of each wishbone leg, and the steering arm. The defaults are
the 2025 car.

The construction is ported from the team's Python geometry editor, with its bugs
fixed: the contact patch is computed down the wheel's own plane, so camber and toe
are real inputs; anti-dive is measured over the whole wheelbase; the side-view
instant centre is placed off the contact patch; and each arm's plane contains its
ball joint and both instant centres. The points are named through your linkage
template, the far side comes from your mirror rule, and the steering is written
into the template: an axle generated without a rack is not steered by one.

Generating is one-shot. Changing a target moves nothing; the dialog shows what
**Generate** would do first — which points arrive, which move and by how much, and
which of your own edits would be overwritten. It also says where the fourth chassis
pivot would have to go to share a plane with the other three and the inner tie
rod end, which is what takes the bump steer out. That is advice; it is never
applied for you. With geometry imported, the pivots can be put against it a
clearance off its surface, which is worth doing when the geometry is the chassis
on its own. The targets are kept in the project, so reopening the dialog starts
where you left it.

The rocker, pushrod inner end, damper and anti-roll bar are not generated. They
are packaging, not a consequence of vehicle targets; place them with Add Point.

### The configuration table

The hardpoint dock is where the model itself is defined. Every point carries its
number, its name and its three coordinates, and four things you set:

| Column | What it is |
|---|---|
| **Point Type** | what the solver does with the point. **To Body/Ground** holds it to the chassis, **Solved** lets the linkage work it out as the wheel moves, **Dependent** carries it along with a body that does move — a wheel centre on the upright, a sensor bracket |
| **Part 1**, **Part 2** | the two bodies that meet at that joint: the lower wishbone and the upright, a damper and the chassis |
| **Bushing** | which compliance bushing acts there, by index into your own stiffness and damping map. `—` is a rigid joint |

The dropdowns offer exactly the bodies your project's linkage template describes,
so a car that is not a pushrod double wishbone offers its own parts rather than
ours. The type is a coloured chip rather than a coloured row: blue is held to the
car, green is solved for, amber is carried along.

**None of it has to be typed in to begin with.** The template already knows which
point is a chassis pivot and which is an outer ball joint, and which parts are
drawn through each point, so a workbook opens with the table already described.
Part 1 is always the member the point belongs to and Part 2 what it is attached
to — a tie rod end reads *tie rod, upright*. A project made before any of this
existed holds a template that says nothing about roles; it is read with the
built-in ones assumed, so those tables fill in too, and the analysis dock says so.
What was worked out for you is yours from that moment: change any of it and the
change is what gets saved.

An edit that could not mean anything — the same body on both sides of one joint, a
bushing index out of range — is refused, and the reason appears under the table.
One that is merely unfinished is kept and the row is marked with a dot: a point
typed *Solved* but held to the chassis, a bushing with only one body to act
between. Hovering the row says what is wrong with it.

The number and the name stay put when you scroll sideways, the header stays put
when you scroll down, any header sorts, and the box above the table filters by
name. It follows your desktop's light or dark theme.

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

Points that are not in the workbook — the ones mirroring produced, and the ones
you added — are appended as new rows under the table, in the same two columns it
uses. A point you deleted has its name and value cells emptied, and everything
else in those rows — a unit, a note, a formula in the next column — is left
alone. A renamed point is both: its old rows emptied, its new name appended.

Coordinates are carried as doubles from the file to the viewport and back;
only the renderer sees floats.

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
first time it opens. **Linkage ▸ Import Template** takes one from anywhere (it is
copied in, like every other asset), **Reset to Built-in Template** puts the
shipped one back, and **Show Template File** opens the project's copy in whatever
edits JSON on your machine.

**Linkage ▸ New Part from Selection** draws a part through the selected points, in
the order you picked them, with a label and a kind, closed or open. It is written
into the project's template with `"perCorner": false`: it names its points
outright and is drawn exactly once, not repeated per corner or mirrored.
**Edit Parts** renames or deletes the template's parts. Both edit the file in
place — your notes, your layout and anything this version does not know about
come out the other side untouched — and renaming a part takes the configuration
rows that named it along.

> The template that ships assumes the **pushrod picks up on the upper wishbone**,
> which is what this workbook's numbers say: `PushRod_O` sits 21 mm (front) and
> 31 mm (rear) off the plane of the upper arm and 37–80 mm from its outer ball
> joint. If yours is mounted on the upright instead, delete the `pushRodPickup`
> part and add `{corner}_PushRod_O` to the upright's first chain.

## Analysis

The analysis dock (`Ctrl+K`) puts an axle through **bump**, **roll** or
**steer** and plots what it does: camber (to the body, and to the ground), toe,
caster, kingpin inclination, scrub radius, trail, track and wheelbase change,
damper travel and installation ratio, the roll centre, the anti-roll bar's twist
and Ackermann. **Curves** picks as many plots as you want. Beside it, **Both
sides / Left only / Right only** says which wheels they draw: on a symmetric car
the two are mirror images — the left wheel at +10 mm of rack is the right wheel
at −10 — so one of them is often all there is to read.

### Static camber and toe

**Linkage ▸ Static Camber and Toe** sets each axle's static camber and toe as
numbers, the way Lotus's *Set Static Angles* does. The wheel's axis is built from
them and the contact patch is computed from that — a tyre radius from the wheel
centre, straight down the wheel's own plane onto the ground — so neither has to
be placed as a hardpoint. The far side takes the same numbers, mirrored.

An axle you have not set reads its angles off its hardpoints: a
`{corner}_WheelAxis` point if the table has one, otherwise the contact patch
under the wheel centre, which gives camber and assumes zero toe. That is where
a workbook's static camber has always come from, and it is easy to miss — the
26_DY workbook's patch sits 0.8 mm outboard of its wheel centre, which is
−0.2°. The dialog shows what each axle currently reads and where it read it.

With the angles set, a contact patch in the table only says how high the ground
is (for a workbook measured from a chassis datum), and a wheel axis point is
carried on the axis the angles give. If your ground is `z = 0` you can delete the
patch points altogether.

### Signs, and comparing with Lotus

Every measure has been checked against an independent solve of the same car
(Newton on the upright as a free rigid body, sharing no code with the solver);
they agree to the fourth decimal across bump, roll and steer. When a number
disagrees with Lotus, it is almost always one of these:

| | Here | Lotus |
|---|---|---|
| Frame | ISO 8855: X forward, **Y left**, Z up | X rearward, **Y right**, Z up |
| Positive roll | right-handed about +X: the **left** side rises, body leans right | roll to the **left** is positive |
| Positive rack travel | towards **+Y**, i.e. to the left | check yours: with Y to the right it is likely the other way |
| Static camber and toe | set here, or read off the hardpoints (see above) | *Data ▸ Set Static Angles* |
| Camber in roll | **Camber** is to the body; **Camber to ground** is to the road | check which your plot shows |

Camber, toe, caster and kingpin are signed the same way in both: negative camber
leans the top in, positive toe is toe-in, positive caster leans the top
rearward, positive kingpin leans it inboard. The **installation ratio** is damper
compression per millimetre of wheel travel, so a damper that bump compresses
reads positive.

## Wheels

**Geometry ▸ Add Wheels** draws a real tyre and rim at the corners, which is
what turns a cloud of points into something recognisable as a car. One dialog:
pick the hardpoint each of the four wheels is centred on, and pick a tyre model
and a rim model — STEP or STL, whatever this build can import. Both are copied
into the project like every other asset, and both are optional: a rim on its own
is a perfectly good way to see where the wheels sit.

The four corners are guessed for you from the table when you open the dialog on a
project that has no wheels yet. Names are matched loosely — anything that reads
like a wheel centre, in English or German — and which corner each one belongs to
comes from where the point actually is, since **+X is forward and +Y is left**.
Check it and move on.

Two settings decide how a model lands on its hardpoint:

- **The models are drawn for** the left side, the right side, or neither. A rim
  is dished, so the same model cannot simply be dropped onto all four corners:
  the copies on the far side are drawn as its mirror image, in `y` as always. It
  is the corner you assigned that decides this, not the sign of the coordinate,
  so a workbook measured in somebody else's frame still comes out facing the
  right way.
- **Put the centre of each model on its hardpoint** — on for a wheel modelled on
  its own, wherever its origin happens to sit; off for one already positioned in
  vehicle coordinates, which is then placed by its own origin. The wheel and the
  rim are measured separately, so they do not have to share an origin.

Everything here lives in the project: the two models, the four hardpoint names,
how they are placed, and whether they are drawn at all. A wheel centre you edit
in the table takes its wheel with it as you type. **Geometry ▸ Remove Wheels**
deletes the copies from the project folder; the files they came from are not
touched.

## Building

Requires **Qt 6.5+**, **CMake 3.24+**, a C++20 compiler and **Python 3**. The
blank workbook a new hardpoint table starts from is generated at configure time by
`tools/make_blank_hardpoints_xlsx.py` (standard library only) rather than
committed as a binary.

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
             mirroring rules, the linkage a template resolves to, and where the
             wheel models are placed
src/io/      STL and STEP readers behind one importMeshFile() entry point,
             a minimal ZIP reader/rewriter, the hardpoint workbook reader, and
             the linkage template reader/writer
src/project/ the project format: manifest, copied assets, view state, edits
src/render/  Camera, GPU buffers, the OpenGL viewport, gizmo and mode selector
src/app/     MainWindow and the controller that owns it, the project launcher,
             the mirror and wheel dialogs, menus, the hardpoint table and dock
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

## License

Copyright 2026 Bremergy.

SuspensionKinematics is free software, released under the [GNU General Public
License v3.0](LICENSE) or, at your option, any later version. Anyone may use it,
change it and sell it. Whoever passes it on, changed or not, has to pass it on
under the same license together with its source code, so nobody can turn it
into a closed product. It comes without any warranty.

Qt and Open CASCADE, which the Windows builds bundle, keep their own licenses.
