# Hardpoints: create them, and generate a corner from design targets

> **Resumable working plan.** This file is the source of truth for the work in
> progress. A new session should read it top to bottom, check the **Status**
> boxes, and pick up at the first unchecked item. Update the boxes and the
> progress log as things land.
>
> Sibling document: `docs/KINEMATICS_PLAN.md`. Its **Phase D** overlaps this
> plan's Phase A; Phase A here supersedes it, and the boxes there should be
> ticked from here rather than worked twice.

## 1. What is being built, and why

Today a hardpoint can enter a project by exactly two routes: importing a
workbook, or mirroring a point that came from one. `HardpointModel` has no
`insertRows`/`removeRows`, its name column is deliberately read-only
(`src/app/HardpointModel.h:28`), and `MainWindow::loadHardpointsFromProject()`
returns `false` on its first line when the project has no workbook
(`src/app/MainWindow.cpp:539`). **A project cannot hold a point that a
spreadsheet did not give it.**

That is a real limit on the tool. It means the suspension cannot be started
here -- only inspected here, after it has been laid out somewhere else.

Two things get built, and the first is worth having on its own:

1. **Create, delete and rename points.** The missing primitive. Without it the
   generator has nowhere to put anything.
2. **Generate a corner from design targets.** Track, wheelbase, weight
   distribution, CoG, tyre and rim size, kingpin and caster, target roll centre
   height and swing-arm lengths, anti-dive, planform angles -- out the other end
   comes a full set of wishbone, upright and steering hardpoints.

### Where the second one comes from

`~/Downloads/Geometry-Editor 1/Geometry-Editor` is an abandoned Python attempt
at the same tool by the suspension side of the team. Its kinematics
(`Geo_Math/Math/move_math.py`, a sympy `nsolve` over 21 unknowns, heave only) is
strictly worse than what `SuspensionSolver` already does. Its **geometry
generation** (`Geo_Math/Math/geo_math.py`, `Geo_Math/variables.py`) has no
counterpart here at all, and it is the useful half. This plan ports the
construction, not the code -- see section 3 for what changes on the way, and
section 3.1 for the bugs that must not come with it.

Its parameter set is the 2025 car (track 1220, wheelbase 1530, CoG at 300 mm,
18" tyres), which makes `variables.py` a ready-made validation fixture.

## 2. Architecture decisions (settled -- do not re-litigate)

1. **The generator is pure, and lives in `suspkin_core`.** Parameters in, a
   `HardpointTable` out. No mesh, no widgets, no project. It is trigonometry
   over ~30 scalars and it must be testable headlessly, like everything else
   there. This is CLAUDE.md's standing rule.
2. **Doubles end to end**, per CLAUDE.md. Coordinates the user will read in
   their own workbook.
3. **Names come from the project's `mechanism` block, not from the generator.**
   The generator produces *roles* -- lower front pivot, upper outer ball joint,
   tie rod inboard -- and the names are looked up in the template that is
   already open. A team whose template says `FL_LOA_front` gets that, and the
   generator never hardcodes a vocabulary. This is the same relationship
   `CornerSolver` has with `MechanismTemplate`, run backwards.
4. **One side, one corner, then mirror.** The generator writes the corner the
   template names and the project's own `MirrorSpec` produces the far side,
   exactly as the parts and the mechanism already do. Nothing here learns a
   naming convention for left and right.
5. **A generated project is an ordinary project by the end of the action.** The
   points are written into a real `.xlsx` inside `hardpoints/` before anything
   else sees them, so the baseline/edits/workbook split (CLAUDE.md) is never
   special-cased. See Phase B: this costs one small resource file, not a second
   code path.
6. **Generation is one-shot, with a diff shown first.** The parameters persist
   in the manifest so they can be reopened and adjusted, but changing one does
   **not** silently re-position points. A live binding puts "the project owns
   all state" against "the user's hand edits are theirs to keep", and the user
   loses. Regenerating is an explicit action that says what it is about to
   overwrite.
7. **The generator does not invent the rocker group.** Pushrod inner, rocker,
   damper and anti-roll bar are packaging decisions, not consequences of vehicle
   targets. The Python tool hardcoded them (`geometry_input.py:145`) and that is
   the honest admission. The generator emits the wishbones, the upright and the
   steering; the rest is placed by hand, which Phase A now makes possible.

## 3. The construction, spelled out

Per corner, one side. ISO 8855, millimetres, `s = +1` on the left. `gamma` is
static camber, `lambda` kingpin inclination, `sigma` caster angle.

| # | Point | How |
|---|---|---|
| 1 | **Wheel centre** | `x` from the CoG and the weight distribution along the wheelbase; `y = s * track/2`; `z` = the loaded tyre radius. |
| 2 | **Contact patch** | `R = z_wc / cos(gamma)`; `y_cp = y_wc - s * R * sin(gamma)`, `z = 0`. Negative camber walks the patch outboard, which is the point of computing it rather than dropping a perpendicular. |
| 3 | **Steering axis** | Its ground pierce point is the contact patch moved forward by the mechanical trail and inboard by the scrub radius. Its direction is `z_hat` leaned top-rearward by `sigma` and top-inboard by `lambda`. |
| 4 | **Ball joints** | Both on that axis, at the heights the rim packaging allows: `h = (z_wc +/- rim_radius) / d_z`. This is what keeps the joints inside the wheel. |
| 5 | **Front-view instant centre** | From the target roll-centre height `h_rc` and the front-view swing arm length `L`: `y = y_cp - s * L`, `z = h_rc * L / |y_cp|`. Measured off the contact patch's real `y`, not off `track/2`. |
| 6 | **Side-view instant centre** | From the anti-dive (or anti-lift) target and the brake bias: `tan(theta) = (anti/100) * h_cg / (wheelbase * bias)`, then walked out from the contact patch along that line by the side-view swing arm length. |
| 7 | **Wishbone planes** | An arm's plane contains its outboard ball joint and **both** instant centres -- that is what fixes a wishbone's motion. Normal is `(IC_roll - out) x (IC_pitch - out)`. |
| 8 | **Inboard pivot rays** | In that plane, each leg leaves the ball joint at its own planform angle (four of them per axle: upper/lower x forward/rearward). Phase C stops at a given arm length; Phase E casts the ray at the chassis. |
| 9 | **Tie rod outboard** | Offset from the wheel centre by the steering arm length (sign selects front or rear steer), at a height on the rim packaging circle, `y` interpolated on the steering axis, plus the Ackermann offset. |
| 10 | **Tie rod inboard** | Three of the four wishbone inboards define a plane; the tie-rod inboard is where the line from the tie-rod outboard toward the front-view IC pierces it. `x` is then set from the outboard plus an offset. |
| 11 | **Coplanarity advice** | The fourth wishbone inboard is reported with the position that *would* put it on that plane. Not applied -- shown, as "move this point here and the bump steer goes away". |

Steps 10 and 11 are the sharpest idea in the Python tool and the reason it is
worth porting at all. Zero bump steer wants the tie rod's instantaneous axis to
share the wishbones' instant centre; four inboard points are never naturally
coplanar, so the construction picks three, and then says what the fourth would
have to be. It is an **advisory**, and the UI must present it as one.

### 3.1 Bugs in the source that must not be ported

Verified by reading `Geo_Math/Math/geo_math.py`:

- **`:381`** -- the *rear* tie-rod outboard computes its `z` from
  `Steering_Arm_length_front` while computing its `x` from `..._rear`.
  Copy-paste; with the shipped numbers (70 front, 90 rear, 90 rim) it silently
  moves the point 57 mm.
- **`:134`** -- the side-view instant centre is placed at
  `cos(alpha) * swingarm` from the **world origin**, with no contact-patch or
  wheel-centre term, while the front-view one at `:121` correctly offsets from
  the wheel centre. Anti-dive geometry is built from the contact patch.
- **`:130`** -- the anti-dive relation divides by `Wheelbase/2`. The standard
  relation is over the **whole** wheelbase; as written the line is twice as
  steep as the requested percentage.
- **`:66`** -- camber is hardcoded to `0` in the contact patch and both camber
  parameters are marked "not implemented" in `variables.py`, so the patch is
  just the wheel centre dropped to the ground. Step 2 above replaces it.
- **`:12`** -- `get_intersect_plane_ray` calls `sympy.solve` on a *linear*
  equation, once per point. It is a dot product. This is most of why the Python
  tool is unusably slow.

## 4. Work breakdown and status

Legend: `[x]` done - `[~]` in progress - `[ ]` not started

### Phase A -- creating, deleting and renaming a point

The primitive everything else needs. Useful shipped alone.

- [ ] `HardpointModel` -- `insertPoint(int row, Hardpoint)` and
      `removePoints(std::vector<int>)`, both through
      `beginInsertRows`/`beginRemoveRows` so the panel's
      `QSortFilterProxyModel` stays correct. Config entries are keyed by name
      and move with the point, not with the row.
- [ ] `HardpointModel` -- make `NameColumn` editable, guarded: a rename must be
      rejected when the new name is empty, already taken, or ends in `_x`/`_y`/
      `_z` (which would collide with the workbook's own coordinate suffixes).
      Route it through the existing `editRejected()` path.
- [ ] **The rename rule for a workbook-backed point is remove + add.** The name
      is the key `XlsxHardpointSource` writes back through, so a renamed point
      has to leave its old rows behind and arrive as a new one. Renaming a point
      that is *already* only in `edits.json` is a plain rename.
- [ ] `MainWindow` -- `Hardpoints > Add Point...` (seeded from the selection, so
      a new point starts next to the one being worked on), `Delete Point`,
      `Rename Point...`. Wire into `updateActionState()`.
- [ ] **Every add and delete goes through `setHardpointTable()`**, which is what
      calls `rebuildLinkage()`. `Linkage` holds *indices into the table*
      (CLAUDE.md), so inserting or removing a row without that rebuild leaves
      the viewport drawing parts between the wrong points. This is the single
      biggest hazard in the phase.
- [ ] `writeHardpointsXlsx()` -- a removed point's three rows have their **name
      and value cells blanked**, leaving the rest of each row intact. Deleting
      whole `<row>` elements would take neighbouring columns with them.
- [ ] Multi-select in `HardpointPanel` and ctrl-click in the viewport, so a
      delete can take more than one point.

### Phase B -- a project can hold points with no workbook

The structural blocker. `HardpointEdits::added` and `removed` already round-trip
(`src/project/Project.h:249`), so the storage exists; what is missing is a
workbook for the points to be a delta *against*.

Chosen approach: **a blank workbook shipped as a resource**, filled by the
append path that already exists.

- [ ] `tools/make_blank_hardpoints_xlsx.py` -- stdlib only, following
      `tools/make_test_xlsx.py`, emitting a one-sheet workbook whose only
      content is a `Name` / `Value` header row. Generated at configure time like
      the other fixtures; no binary in git (CLAUDE.md).
- [ ] `CMakeLists.txt:147` -- add the generated file to the existing
      `qt_add_resources(suspkin_core "templates" ...)` so it ships as
      `:/templates/blank_hardpoints.xlsx` and the tests read the same bytes the
      application does.
- [ ] `Project` -- write the resource into `hardpoints/` on demand, the way
      `installBuiltinLinkageTemplate()` installs the built-in template.
- [ ] **Fill it through the writer, not a new one.** Build an
      `XlsxHardpointSource` by hand for the blank file (`nameColumn = 1`,
      `valueColumn = 2`, `lastRow = 1`, empty `rows`) and call
      `writeHardpointsXlsx()`. Every point is then "not in the workbook" and
      goes down the existing append-before-`</sheetData>` path
      (`src/io/XlsxHardpoints.cpp:911`). Then **re-read the file** to get the
      real baseline and cell map -- the same re-read Overwrite Workbook already
      does, and for the same reason.
- [ ] Note the trap: `readHardpointsXlsx()` skips a sheet that yields no points
      (`XlsxHardpoints.cpp:832`), so the blank workbook cannot be read *before*
      it is filled. It is written into, then read. Never read first.
- [ ] `MainWindow` -- `Hardpoints > New Hardpoint Table` for a project with no
      workbook at all, so Phase A is reachable without an import.

### Phase C -- the generator (core maths)

- [ ] `src/model/DesignParameters.h` -- the parameter set of section 3, grouped
      vehicle / per-axle, with the units in the field comments and sane
      defaults. Round-trip helpers for the manifest.
- [ ] `src/model/HardpointGenerator.h` / `.cpp` -- `generateCorner(const
      DesignParameters&, Axle)` returning roles and coordinates, plus the
      warnings a set of targets can earn (a ball joint outside the rim, a
      steering arm longer than the packaging circle, an unreachable trail).
- [ ] Role-to-name binding against the open `MechanismTemplate`, so what comes
      out is a `HardpointTable` in the project's own vocabulary.
- [ ] The coplanarity advisory of step 11, returned alongside rather than
      applied.
- [ ] `CMakeLists.txt` -- added to `suspkin_core`.

### Phase D -- parameters, the dialog and the action

- [ ] `DesignParameters` stored in the manifest next to `MirrorSpec` and
      `WheelSpec`, read and written by `src/project/Project.cpp`. Per CLAUDE.md
      this is part of the feature, not a follow-up.
- [ ] `src/app/GenerateDialog.*` -- the parameters, grouped, with the corner to
      generate and the side to generate it on.
- [ ] **A preview before anything is written**: which points would be added,
      which existing ones would move and by how much, and which are hand-edited
      and therefore about to be overwritten. Decision 6 lives or dies here.
- [ ] `Hardpoints > Generate from Design...`, then mirror through the project's
      `MirrorSpec` for the far side, then `setHardpointTable()` ->
      `rebuildLinkage()` -> solvers, and `markDirty()`.
- [ ] The advisory from step 11 surfaced on the status bar and in the dialog,
      never applied silently.

### Phase E -- chassis clearance for the inboard points

Optional, and the highest-value piece after the generator itself: we already
load the chassis mesh and today do nothing with it but draw it.

- [ ] `src/geom/MeshQuery.*` -- ray/triangle intersection and unsigned
      distance-to-mesh over the imported `TriMesh`. Core library, no OpenGL.
      A BVH over the triangles; the Python tool leaned on Open3D for this and
      we are not taking that dependency.
- [ ] Inboard placement: intersect the pivot ray with the chassis, then back off
      along it until the distance to the mesh reaches the clearance parameter
      (bisection, as `geo_math.py:246` does). Report a ray that never hits
      rather than inventing `y = 200`, which is what the Python code does at
      `:280` and is the reason its output cannot be trusted unattended.

### Phase F -- tests

- [ ] `tests/test_hardpoint_generator.cpp` -- registered in
      `tests/CMakeLists.txt`. The 2025 car from `variables.py` as the fixture:
      wheel centre and contact patch at known coordinates; both ball joints
      exactly on the steering axis; the axis reproducing the requested caster
      and kingpin angles when measured back off it; the contact patch walking
      outboard as camber goes negative; each wishbone plane containing its
      outboard joint and both instant centres to 1e-9.
- [ ] **The round trip that matters**: generate a corner, feed it to
      `CornerSolver::bind()`, and check the design pose reports back the caster,
      kingpin, scrub and trail that were asked for. The generator and the solver
      are inverses; this is the test that proves it.
- [ ] `tests/test_project.cpp` -- `removed` round-trips through `edits.json`
      (already outstanding in `KINEMATICS_PLAN.md`); `DesignParameters`
      round-trips through the manifest.
- [ ] `tests/test_xlsx_hardpoints.cpp` -- the blank resource workbook, filled
      through `writeHardpointsXlsx()` and read back, yields exactly the points
      that were written; a renamed point leaves no trace of its old name; a
      deleted point's row keeps its neighbouring columns.

## 5. Verification

```bash
cmake --build --preset linux-debug && ctest --preset linux-debug

./build/linux-debug/tests/test_hardpoint_generator
./build/linux-debug/tests/test_hardpoint_generator steeringAxisReproducesItsAngles

# headless render check (no --import: it opens a modal on warnings and hangs)
QT_QPA_PLATFORM=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  ./build/linux-debug/suspkin --screenshot /tmp/frame.png <project.suspkin>

./start.sh
```

The end-to-end check for this work: **start from an empty project, generate a
front corner, and have the analysis dock sweep it** without a workbook ever
having been imported. That exercises Phases A through D and the whole existing
stack behind them in one go.

## 6. Progress log

- **2026-09-09** -- Plan written. Nothing implemented yet. Established by
  reading the code, and worth not re-deriving:
  - Points enter a project only by import or mirror.
    `loadHardpointsFromProject()` returns `false` at
    `src/app/MainWindow.cpp:539` when the manifest names no workbook, so
    `edits.json` is not even read. This, not the maths, is what makes a
    generator a structural change.
  - `HardpointEdits` already carries `added` and `removed` and already
    round-trips them, so nothing new is needed to *store* created points.
  - `writeHardpointsXlsx()` sends any point it cannot find in
    `XlsxHardpointSource::rows` down the append path, and `canAppend()` only
    wants `nameColumn`, `valueColumn` and `lastRow`. A hand-built source over a
    header-only workbook therefore fills a blank file with no new writer -- but
    `readHardpointsXlsx()` rejects a sheet with no points, so the blank file
    must be written into before it is ever read.
  - The Python tool's generator is ~400 lines of trigonometry with no real
    dependencies; the numpy and sympy in it are doing nothing that a dot product
    would not. Porting it is not a dependency decision.
