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
   tie rod inboard, **the point on the wheel's own axis** -- and the names are
   looked up in the template that is already open. A role the template does not
   name is simply not written: a project whose template has no `contactPatch` --
   which is now the sensible way to write one -- gets no contact patch point,
   because the solver computes that from the wheel axis anyway. A team whose template says `FL_LOA_front` gets that, and the
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
| 2 | **Wheel axis** | A point an axle's length outboard of the wheel centre along `n = (sin(tau)*cos(gamma), s*cos(tau)*cos(gamma), -sin(gamma))` -- static toe `tau` and camber `gamma` in one direction. This is what the solver reads the wheel's attitude off (`mechanism.upright.wheelAxis`), it is the only way a generated table can state **toe** at all, and it is what turns the wheel model with the steering. |
| 3 | **Contact patch** | `contactPatchFor(wc, n, R)` with `R = tireRadiusToGround(wc, n, 0)` -- the solver's own functions from `SuspensionSolver.h`, not a second derivation, so generator and solver cannot drift apart. `R = z_wc / cos(gamma)` is that same drop with toe left out. Steps 4, 6 and 7 build off the patch; it is only *written into the table* when the template names a `contactPatch`, because with a wheel axis the solver computes it. Negative camber walks it outboard, which is the point of computing it rather than dropping a perpendicular. |
| 4 | **Steering axis** | Its ground pierce point is the contact patch moved forward by the mechanical trail and inboard by the scrub radius. Its direction is `z_hat` leaned top-rearward by `sigma` and top-inboard by `lambda`. |
| 5 | **Ball joints** | Both on that axis, at the heights the rim packaging allows: `h = (z_wc +/- rim_radius) / d_z`. This is what keeps the joints inside the wheel. |
| 6 | **Front-view instant centre** | From the target roll-centre height `h_rc` and the front-view swing arm length `L`: `y = y_cp - s * L`, `z = h_rc * L / abs(y_cp)`. Measured off the contact patch's real `y`, not off `track/2`. |
| 7 | **Side-view instant centre** | From the anti-dive (or anti-lift) target and the brake bias: `tan(theta) = (anti/100) * h_cg / (wheelbase * bias)`, then walked out from the contact patch along that line by the side-view swing arm length. |
| 8 | **Wishbone planes** | An arm's plane contains its outboard ball joint and **both** instant centres -- that is what fixes a wishbone's motion. Normal is `(IC_roll - out) x (IC_pitch - out)`. |
| 9 | **Inboard pivot rays** | In that plane, each leg leaves the ball joint at its own planform angle (four of them per axle: upper/lower x forward/rearward). Phase C stops at a given arm length; Phase E casts the ray at the chassis. |
| 10 | **Tie rod outboard** | Offset from the wheel centre by the steering arm length (sign selects front or rear steer), at a height on the rim packaging circle, `y` interpolated on the steering axis, plus the Ackermann offset. |
| 11 | **Tie rod inboard** | Three of the four wishbone inboards define a plane; the tie-rod inboard is where the line from the tie-rod outboard toward the front-view IC pierces it. `x` is then set from the outboard plus an offset. |
| 12 | **Coplanarity advice** | The fourth wishbone inboard is reported with the position that *would* put it on that plane. Not applied -- shown, as "move this point here and the bump steer goes away". |

Steps 11 and 12 are the sharpest idea in the Python tool and the reason it is
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
  just the wheel centre dropped to the ground. Steps 2 and 3 above replace it:
  the attitude is stated by the axis point, and the patch falls out of it.
- **`:12`** -- `get_intersect_plane_ray` calls `sympy.solve` on a *linear*
  equation, once per point. It is a dot product. This is most of why the Python
  tool is unusably slow.

Two more, found while porting:

- **`:201`/`:203`** -- a leg's planform is laid out as `sin(alpha) * 100` along
  the car per 100 mm across it, so a 30 degree leg comes out at 26.6 degrees. The
  angle a leg makes in top view is its tangent.
- **`:132`** -- the *rear* anti-lift angle is divided by the *front* brake share.
  Anti-lift is measured against the braking the rear axle does, which is
  `1 - bias`.

## 4. Work breakdown and status

Legend: `[x]` done - `[~]` in progress - `[ ]` not started

### Phase A -- creating, deleting and renaming a point

The primitive everything else needs. Useful shipped alone.

- [x] `HardpointModel` -- `insertPoint(int row, Hardpoint)` and
      `removePoints(std::vector<int>)`, both through
      `beginInsertRows`/`beginRemoveRows` so the panel's
      `QSortFilterProxyModel` stays correct. Config entries are keyed by name
      and move with the point, not with the row.
- [x] `HardpointModel` -- make `NameColumn` editable, guarded: a rename must be
      rejected when the new name is empty, already taken, or ends in `_x`/`_y`/
      `_z` (which would collide with the workbook's own coordinate suffixes).
      Route it through the existing `editRejected()` path.
- [x] **The rename rule for a workbook-backed point is remove + add.** The name
      is the key `XlsxHardpointSource` writes back through, so a renamed point
      has to leave its old rows behind and arrive as a new one. Renaming a point
      that is *already* only in `edits.json` is a plain rename.
- [x] `MainWindow` -- `Hardpoints > Add Point...` (seeded from the selection, so
      a new point starts next to the one being worked on), `Delete Point`,
      `Rename Point...`. Wire into `updateActionState()`.
- [x] **Every add and delete goes through `setHardpointTable()`**, which is what
      calls `rebuildLinkage()`. `Linkage` holds *indices into the table*
      (CLAUDE.md), so inserting or removing a row without that rebuild leaves
      the viewport drawing parts between the wrong points. This is the single
      biggest hazard in the phase.
- [x] `writeHardpointsXlsx()` -- a removed point's three rows have their **name
      and value cells blanked**, leaving the rest of each row intact. Deleting
      whole `<row>` elements would take neighbouring columns with them.
- [x] Multi-select in `HardpointPanel` and ctrl-click in the viewport, so a
      delete can take more than one point.

### Phase B -- a project can hold points with no workbook

The structural blocker. `HardpointEdits::added` and `removed` already round-trip
(`src/project/Project.h:249`), so the storage exists; what is missing is a
workbook for the points to be a delta *against*.

Chosen approach: **a blank workbook shipped as a resource**, filled by the
append path that already exists.

- [x] `tools/make_blank_hardpoints_xlsx.py` -- stdlib only, following
      `tools/make_test_xlsx.py`, emitting a one-sheet workbook whose only
      content is a `Name` / `Value` header row. Generated at configure time like
      the other fixtures; no binary in git (CLAUDE.md).
- [x] `CMakeLists.txt:147` -- add the generated file to the existing
      `qt_add_resources(suspkin_core "templates" ...)` so it ships as
      `:/templates/blank_hardpoints.xlsx` and the tests read the same bytes the
      application does.
- [x] `Project` -- write the resource into `hardpoints/` on demand, the way
      `installBuiltinLinkageTemplate()` installs the built-in template.
- [x] **Fill it through the writer, not a new one.** Build an
      `XlsxHardpointSource` by hand for the blank file (`nameColumn = 1`,
      `valueColumn = 2`, `lastRow = 1`, empty `rows`) and call
      `writeHardpointsXlsx()`. Every point is then "not in the workbook" and
      goes down the existing append-before-`</sheetData>` path
      (`src/io/XlsxHardpoints.cpp:911`). Then **re-read the file** to get the
      real baseline and cell map -- the same re-read Overwrite Workbook already
      does, and for the same reason.
- [x] Note the trap: `readHardpointsXlsx()` skips a sheet that yields no points
      (`XlsxHardpoints.cpp:832`), so the blank workbook cannot be read *before*
      it is filled. It is written into, then read. Never read first.
- [x] `MainWindow` -- `Hardpoints > New Hardpoint Table` for a project with no
      workbook at all, so Phase A is reachable without an import.

### Phase C -- the generator (core maths)

- [x] `src/model/DesignParameters.h` -- the parameter set of section 3, grouped
      vehicle / per-axle, with the units in the field comments and sane
      defaults. Round-trip helpers for the manifest. **Static camber and static
      toe are both real parameters here**, not the "not implemented" they are in
      the Python tool: the wheel axis point is what carries them into the table,
      and toe has nowhere else to live.
- [x] `src/model/HardpointGenerator.h` / `.cpp` -- `generateCorner(const
      DesignParameters&, Axle)` returning roles and coordinates, plus the
      warnings a set of targets can earn (a ball joint outside the rim, a
      steering arm longer than the packaging circle, an unreachable trail).
- [x] The **wheel axis** role among them, and the contact patch built with the
      solver's own `contactPatchFor()` / `tireRadiusToGround()` rather than a
      second copy of that trigonometry. The two must not be able to drift.
- [x] A generated corner writes the **steering** role on the axle it generates a
      rack for, and explicitly none on the other (`CornerSpec::steeringRack` /
      `steeringStated`). A generator that leaves it silent hands back a car whose
      rear axle steers.
- [x] Role-to-name binding against the open `MechanismTemplate`, so what comes
      out is a `HardpointTable` in the project's own vocabulary.
- [x] The coplanarity advisory of step 12, returned alongside rather than
      applied.
- [x] `CMakeLists.txt` -- added to `suspkin_core`.

### Phase D -- parameters, the dialog and the action

- [x] `DesignParameters` stored in the manifest next to `MirrorSpec` and
      `WheelSpec`, read and written by `src/project/Project.cpp`. Per CLAUDE.md
      this is part of the feature, not a follow-up.
- [x] `src/app/GenerateDialog.*` -- the parameters, grouped, with the corner to
      generate and the side to generate it on.
- [x] **A preview before anything is written**: which points would be added,
      which existing ones would move and by how much, and which are hand-edited
      and therefore about to be overwritten. Decision 6 lives or dies here.
- [x] `Hardpoints > Generate from Design...`, then mirror through the project's
      `MirrorSpec` for the far side, then `setHardpointTable()` ->
      `rebuildLinkage()` -> solvers, and `markDirty()`.
- [x] The advisory from step 12 surfaced on the status bar and in the dialog,
      never applied silently.

### Phase E -- chassis clearance for the inboard points

Optional, and the highest-value piece after the generator itself: we already
load the chassis mesh and today do nothing with it but draw it.

- [x] `src/geom/MeshQuery.*` -- ray/triangle intersection and unsigned
      distance-to-mesh over the imported `TriMesh`. Core library, no OpenGL.
      A BVH over the triangles; the Python tool leaned on Open3D for this and
      we are not taking that dependency.
- [x] Inboard placement: intersect the pivot ray with the chassis, then back off
      along it until the distance to the mesh reaches the clearance parameter
      (bisection, as `geo_math.py:246` does). Report a ray that never hits
      rather than inventing `y = 200`, which is what the Python code does at
      `:280` and is the reason its output cannot be trusted unattended.

### Phase F -- tests

- [x] `tests/test_hardpoint_generator.cpp` -- registered in
      `tests/CMakeLists.txt`. The 2025 car from `variables.py` as the fixture:
      wheel centre and contact patch at known coordinates; both ball joints
      exactly on the steering axis; the axis reproducing the requested caster
      and kingpin angles when measured back off it; the contact patch walking
      outboard as camber goes negative; the wheel axis point reproducing the
      requested camber **and toe**; each wishbone plane containing its outboard
      joint and both instant centres to 1e-9.
- [x] **The round trip that matters**: generate a corner, feed it to
      `CornerSolver::bind()`, and check the design pose reports back the camber,
      toe, caster, kingpin, scrub and trail that were asked for. The generator
      and the solver are inverses; this is the test that proves it. Camber and
      toe only close this loop because of the wheel axis point -- before it,
      toe went in and nothing came back out.
- [x] `tests/test_project.cpp` -- `removed` round-trips through `edits.json`
      (already outstanding in `KINEMATICS_PLAN.md`); `DesignParameters`
      round-trips through the manifest.
- [x] `tests/test_xlsx_hardpoints.cpp` -- the blank resource workbook, filled
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

- **2026-09-11** -- **All six phases landed.** All 14 test binaries green, two
  of them new: `test_hardpoint_generator` (23 cases) and `test_mesh_query`.
  Verified in the real application headless: a project made by the generator
  with no workbook ever imported opens, draws both axles and both sides, and the
  analysis poses it -- the end-to-end check of section 5. Where the build
  differs from the text above, and why:
  - **Track is measured between the contact patches**, not at the wheel centres
    (step 1): the wheel is moved across until its computed patch is where the
    track says. It is where every rulebook and every data sheet measures it, and
    under camber the two differ.
  - **Phase C's "given arm length" is a pivot line**: each wishbone's chassis
    pivots go on a line at a stated distance from the centreline
    (`upperPivotY`, `lowerPivotY`, default 200 mm -- the Python tool's own
    fallback). Both legs of an arm then end at one chassis pickup line, which is
    how a frame is built; a length would put two legs of different sweep at two
    different widths. With both lines at one distance the four pivots share a
    vertical plane and step 12 has nothing to advise, which is the correct
    answer, not a missing one.
  - **The two bugs added to section 3.1** -- planform by tangent, rear anti-lift
    against the rear's own braking.
  - "Every add and delete goes through `setHardpointTable()`" is kept in
    substance: row-level changes use the model's insert and remove signals and
    then `syncTableToViewport()`, which is the half of `setHardpointTable()` that
    re-resolves the parts. **`captureHardpointConfig()` has to run before it**,
    or the refill from the project undoes the model's move -- found by reading,
    written into CLAUDE.md.
  - Row order survives a reopen: `HardpointEdits::addedAfter` records the point
    above each added one, so a point added next to its neighbour, or a renamed
    workbook point, comes back where it was rather than at the bottom.
  - The writer's `lastRow` now counts every `<row>` element, not only rows with
    values. An empty formatted row below the table, or a deleted point's
    emptied cells, used to be a row number an append could collide with.
  - The planner writes steering only where it changes what a corner already
    means, and states the other corners of a template that said nothing, so
    generating the rear alone does not stop the front steering.
  - **Open question -- the front roll centre.** The generator places the
    front-view instant centre where the two arm planes cross the transverse plane
    through the wheel centre (the RCVD construction). The solver reads its front
    view off the ball joints themselves (`KINEMATICS_PLAN.md`, "Front-view
    instant centre"), which does not see a caster's worth of fore-aft offset. For
    the 2025 car the analysis therefore reports **26.8 mm against the 30 mm
    asked for** on the front axle; the rear, with no caster or trail, agrees to
    the micron. Changing the solver to take its front view from the arm planes
    would make them agree -- and would move every existing project's roll centre
    curve slightly -- so it is left for a decision rather than made here.
    `aGeneratedCarSweepsWithoutAWorkbook` pins both numbers.
    **Decided the same day: the arm planes.** The solver now builds its front
    view the way this generator does, and the 2025 car's front reads 30 mm to
    1e-6. What settled it was a real workbook designed to 10 mm that the old
    construction read as 6.3; see `KINEMATICS_PLAN.md`'s progress log.

- **2026-09-09** -- The **wheel's own axis** landed in the solver ahead of this
  plan (`mechanism.upright.wheelAxis`, `{corner}_WheelAxis`), so section 3 has
  been renumbered around it. What changes for this work:
  - The generator emits a **wheel axis point**, and static **toe** becomes a
    real parameter -- there was previously nowhere in a hardpoint table to put
    it, which is why the Python tool left camber and toe "not implemented".
  - The contact patch is no longer necessarily a generated point. The solver
    computes it (`contactPatchFor()`, a tyre radius down the wheel's own plane
    onto the ground), and the generator should call those same functions for its
    internal patch rather than re-deriving them.
  - The Phase F round trip gets sharper: camber and toe now come back out of
    `CornerSolver` as well as going in.
