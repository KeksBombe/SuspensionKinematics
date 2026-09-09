# Kinematics: simulate and display a pushrod double-wishbone corner

> **Resumable working plan.** This file is the source of truth for the work in
> progress. A new session should read it top to bottom, check the **Status**
> table, and pick up at the first unchecked item. Update the status table and the
> progress log as things land.

## 1. What is being built, and why

The tool today is a viewer and editor of hardpoints: it reads a workbook, draws
markers, resolves `linkage/template.json` into parts, and writes coordinates
back. Nothing moves. `README.md` and `src/render/Camera.h` both say "when the
solver lands"; CLAUDE.md calls bump/roll sweeps "the intended next layer".

The request:

- **Front axle** — double wishbone, **pushrod picking up on the upper A-arm**,
  bellcrank (rocker) to a spring/damper, **tie rod behind** the wheel centre.
- **Rear axle** — the same, plus a **U-bar** (anti-roll bar) driven off the rocker.
- Both **simulated** (the mechanism moves through its travel) and **displayed**
  (you can watch it move and read the curves off it).
- **Create and edit points, and connect them to other points.**

The shipped template `resources/templates/double_wishbone_pushrod.json` already
*draws* exactly this suspension — corners `F` and `R`, pushrod on the upper
wishbone, anti-roll arm and drop link both present and optional. The hardpoint
vocabulary is settled and does not change:

```
{corner}_LCA_IF  _LCA_IR  _LCA_O      lower wishbone: two pivots, one ball joint
{corner}_UCA_IF  _UCA_IR  _UCA_O      upper wishbone: the same
{corner}_TieRod_I  _TieRod_O          steering / toe link
{corner}_PushRod_O  _PushRod_I        pushrod, outer end on the UPPER arm
{corner}_Rocker_Center  _Rocker_AxisPoint
{corner}_Damper_O  _Damper_I
{corner}_AntiRoll_O  _AntiRoll_I  _AntiRoll_Center
{corner}_WheelCenter  _WheelAxis  _ContactPatch   axis point new; patch optional
```

So the missing pieces are: the *semantics* a solver needs on top of those names,
the solver itself, the display of what it produces, and editing that goes past
typing a number into a cell.

## 2. Architecture decisions (settled — do not re-litigate)

1. **All the maths lives in `suspkin_core`.** No OpenGL, no widgets, so every
   part of the solve is unit-testable headlessly. This is CLAUDE.md's standing
   rule and the solver is the strongest case for it yet.
2. **Doubles end to end.** `src/geom/Vec3.h` is a double-precision vector.
   Circle-sphere intersections near a tangency lose most of a float's mantissa
   exactly where the answer matters. Only the draw call sees `QVector3D`.
3. **Closed-form sequential solve, not a Newton solver over a constraint set.**
   A double wishbone decomposes into a chain of exact geometric primitives
   (circle-sphere, trilateration, rigid transform from three points). It is
   faster, it cannot fail to converge, and each step is separately testable.
   Branch selection is by **continuation** — nearest to the previous pose —
   never by a rule about signs.
4. **The mechanism is data, like the parts are.** A new `"mechanism"` block in
   `linkage/template.json` says which hardpoint plays which role. The template
   file stays the one place that describes what kind of car this is.
5. **Posing never dirties the project.** The solver writes into a *separate*
   posed table handed to the viewport. `HardpointModel::table()` — the design
   geometry — is untouched, so no sweep can ever leak into `edits.json` or a
   workbook.
6. **The project owns all state** (CLAUDE.md's rule that shapes everything):
   the sweep settings, the current travel position, which plots are open and
   whether simulation is on all persist in the manifest.

## 3. The solve, spelled out

Everything below is per corner, per side. `θ` is the lower wishbone's rotation
about its own pivot axis, measured from the design position, and is the
mechanism's single degree of freedom.

| # | Step | Primitive |
|---|---|---|
| 1 | `LCA_O(θ)` = lower outer ball joint rotated about the `LCA_IF→LCA_IR` axis | `rotateAbout` |
| 2 | `UCA_O` — on its own circle about `UCA_IF→UCA_IR`, and a fixed distance from `LCA_O(θ)` (the upright is rigid) | `intersectCircleSphere` |
| 3 | `TieRod_O` — fixed distances from `LCA_O`, `UCA_O` and the (possibly steered) `TieRod_I` | `trilaterate` |
| 4 | **Upright pose** — the rigid motion taking design `(LCA_O, UCA_O, TieRod_O)` onto the solved triple. Carries `WheelCenter`, `WheelAxis` and anything else named as carried — and the wheel *model*, through `CornerPose::uprightMotion`. | `rigidFromTriangle` |
| 5 | **Upper-arm angle** `θ_u` — read off where `UCA_O` landed on its circle. `PushRod_O` is rigid with the **upper wishbone**, so it rotates by `θ_u` about the upper pivot axis. (`pushrodMount` selects upper arm / lower arm / upright.) | `Circle::angleOf`, `rotateAbout` |
| 6 | **Rocker angle** `θ_r` — `PushRod_I` rides the rocker's circle about `Rocker_Center→Rocker_AxisPoint` and stays a pushrod-length from `PushRod_O`. `Damper_O` and `AntiRoll_O` then rotate by `θ_r`. | `intersectCircleSphere` |
| 7 | **Anti-roll arm angle** — `AntiRoll_I` rides a circle about the **bar axis** and stays a drop-link length from `AntiRoll_O`. The bar axis runs through `AntiRoll_Center` and its mirror on the far side; with no far side it falls back to the Y direction, which is what a transverse U-bar is. | `intersectCircleSphere` |

**Driving it by wheel travel rather than by `θ`**: `wheelCentreZ(θ)` is smooth and
monotonic over any sane travel, so a secant iteration with a bisection fallback
converts a requested travel into `θ`. Same routine, different target, for
"contact patch rises by *h*", which is what a roll sweep needs.

### Measures read off a pose

| Measure | How |
|---|---|
| Camber, toe | The upright rotation applied to the design spin axis. That axis is `normalize(WheelAxis − WC)`, oriented outboard, which is static camber *and* static toe straight out of the table. With no axis point named it falls back to `normalize(cross(WC − CP, x̂))`, which captures camber only and assumes zero toe -- which is why toe is also reported as **change from design** (that curve being the bump steer) alongside the absolute number. |
| Caster, KPI | The steering axis `LCA_O → UCA_O`, in side view and front view. |
| Scrub radius, mechanical trail | Where that axis pierces the ground plane through the contact patch, against the contact patch itself. The patch is computed per pose -- a tyre radius from the wheel centre, straight down the wheel's own plane -- so it walks round the tyre as the wheel leans instead of being carried rigidly by the upright. |
| Track / half-track change, wheelbase change | The contact patch's `y` and `x` against design. |
| Front-view instant centre | Each arm's pivot axis is crossed with the transverse plane through the wheel centre to give its front-view pivot; the two arm lines are then intersected in the `YZ` view. |
| Roll centre | Per axle: the two `contact patch → instant centre` lines intersected. Falls back to the centre plane `y = 0` with one side only. |
| Damper length, motion ratio | `|Damper_O − Damper_I|`; the ratio is the central difference of damper length against wheel travel across the sweep. |
| Anti-roll bar | Each arm's angle from design. Bodily rotation is their mean, **twist** is their difference — so pure bump shows the bar doing nothing and roll shows it working, which is the honest kinematic answer without inventing a stiffness model. |

## 4. Work breakdown and status

Legend: `[x]` done · `[~]` in progress · `[ ]` not started

### Phase A — core maths (`suspkin_core`)

- [x] `src/geom/Vec3.h` — double-precision vector.
- [x] `src/model/GeomSolve.h` / `.cpp` — `Axis`, `Circle`, `Rigid`,
      `rotateAbout`, `intersectCircleSphere`, `trilaterate`,
      `rigidFromTriangle`, `nearestTo`, `linePlaneCrossing`, `intersectLines2D`.
- [x] `src/model/Mechanism.h` / `.cpp` — `MechanismTemplate` (roles by name,
      still carrying `{corner}`), `PushrodMount`, instantiation per corner and
      side through the project's `MirrorSpec` (reuse `mirroredName()`).
- [x] `src/model/SuspensionSolver.h` / `.cpp` — `CornerSolver::bind()`,
      `poseAtArmAngle()`, `poseAtWheelTravel()`, `poseAtContactPatchRise()`,
      `CornerPose` with the measures above.
- [x] `src/model/Sweep.h` / `.cpp` — `AxleSolver` (base + mirrored corner),
      `SweepSpec` (bump / roll / steer, range, steps, held rack travel) and the
      `SweepSettings` it is derived from (see Phase C3),
      `SweepResult`, roll-centre construction, motion ratio, `sweepToCsv()`.
- [x] `CMakeLists.txt` — added to `suspkin_core`.

### Phase B — the template carries the mechanism

- [x] `resources/templates/double_wishbone_pushrod.json` — added the `"mechanism"`
      block, and a note explaining it, next to the existing `"parts"`.
- [x] `src/io/LinkageTemplate.cpp` — reads and writes `"mechanism"`. Keep
      `formatVersion: 1`; the block is additive and its absence is not an error.
- [x] A template with no `"mechanism"` falls back to the built-in one's block,
      with a warning on the status bar. Projects that predate this then simulate
      without anyone hand-editing a file.

### Phase C — display

- [x] `ViewportWidget::setHardpointPositions(const std::vector<QVector3D>&)` —
      batch pose update that keeps the linkage indices valid (unlike
      `setHardpoints()`, which drops the linkage by design).
- [x] `src/app/AnalysisPanel.*` — dock: axle picker, **travel slider** that poses
      the model live, sweep kind, Export CSV. Range, increments and rack travel
      moved to their own window in Phase C3.
- [x] `src/app/PlotWidget.*` — a `QPainter` XY plot (axes, grid, curves, hover
      readout). **No Qt Charts**: it is not currently a dependency and adding one
      would land on both packaging paths.
- [x] Folded into `AnalysisPanel`: a curve selector over every measure, plus a
      live readout table of the eleven that matter, rather than a second dock.
- [x] `MainWindow` — an `&Analysis` menu, the two docks, and the wiring that
      re-solves when a hardpoint moves.
- [x] `ViewState` / manifest — sweep settings, current travel, simulation on/off,
      which curve is shown. Add to `collectViewState()` and `applyViewState()`.

### Phase C2 — animation (asked for mid-build)

- [x] Play/Pause in the analysis dock: a triangle-wave walk of the sweep range,
      seconds-per-cycle adjustable, seeded from wherever the model is standing
      so pressing play does not jump it to one end first.
- [x] **All axles** switch: every axle poses at the same input, so the car heaves
      and rolls as a car. Steering is excluded — a rack belongs to one axle, and
      driving a rear toe link with it would invent a rear-steer.
- [x] Position changes during animation do **not** `markDirty()`; stopping saves
      where it stopped. `animating` and `allAxles` both persist.

### Phase C3 — the parameters window (asked for mid-build)

- [x] `SweepSettings` in `src/model/Sweep.h` — bump travel, rebound travel and
      their increment; roll angle and its increment; steer travel and its
      increment; the held rack. `specFor(kind)` derives the `SweepSpec` that is
      actually solved. A `SweepSpec` is no longer edited by hand.
      **Why**: one shared range is one unit, so switching from a +-25 mm bump
      sweep to roll asked for 25 degrees of body roll. Each kind now keeps its
      own travel in its own unit, stated as travel + increment the way a damper
      and a test sheet state it rather than as start / end / count.
- [x] `src/app/SweepParametersDialog.*` — all three travels, the increments, the
      held rack, the animation speed and the all-axles switch, in a non-modal
      window that applies as it is typed. Under each block, the range and step
      count its numbers come to. The live kind is marked, not enforced: setting
      up a roll sweep before switching to it is the normal way round.
- [x] `AnalysisPanel` keeps only what is touched continuously — axle, Simulate,
      the position slider, sweep kind, Play, the curve — plus `Parameters...`.
      `Analysis ▸ Sweep Parameters...` (Ctrl+Shift+P) opens the same window.
- [x] Switching kind puts the position back to the design position when it falls
      outside the new range, rather than to whichever end it was nearest.
- [x] `SimulationState` carries `kind`, the whole `SweepSettings`,
      `animationSeconds` and `parametersOpen`. A manifest written with the old
      single `from`/`to`/`steps` is folded into that kind's own travel on open,
      so an older project lands where its owner left it.

### Phase D — creating, editing and connecting points

- [x] `HardpointEdits` gains `removed` (names), round-tripped through
      `edits.json`; `applyHardpointEdits()` drops them, removals first so a
      delete-then-re-add comes back as the addition.
- [ ] `HardpointModel` — insert / remove rows, and an editable name column,
      guarded so a rename of a workbook-backed point is recorded as
      remove + add.
- [ ] `MainWindow` — `Hardpoints ▸ Add Point…` (seeded from the selection),
      `Delete Point`, `Rename Point…`.
- [ ] `writeHardpointsXlsx()` — a removed point's three rows have their **name
      and value cells blanked**, leaving everything else in those rows intact.
      (Deleting whole `<row>` elements would take neighbouring columns with them.)
- [ ] Multi-select in `HardpointPanel` and in the viewport (ctrl-click).
- [ ] `Parts ▸ New Part from Selection…` — writes a part into the project's
      `linkage/template.json`. Needs a per-part `"perCorner": false` so a part
      naming literal points is instantiated once rather than once per corner.
- [ ] `Parts ▸ Edit Parts…` — list, rename, delete.

### Phase E — tests

- [x] The primitives, tested inside `tests/test_kinematics.cpp` rather than a
      file of their own: a circle meeting a sphere twice / once / not at all,
      trilateration against a known tetrahedron, a rigid transform recovered
      from a rotated triangle, parallel lines refusing to cross.
- [x] `tests/test_kinematics.cpp` — a synthetic front left corner: the design
      position is a fixed point to 1e-9; every link length holds to a micron
      across a +-40 mm sweep; travel is reached to the micron; the shorter upper
      arm gains negative camber in bump and camber runs one way with no step in
      it; steering the rack turns the wheel and nothing else; the rocker, damper
      and bar arm all move and reverse together; a metre of bump is refused.
- [x] `tests/test_linkage.cpp` — `"mechanism"` block round-trips; a template
      without one still loads; every name the mechanism asks for is one the
      parts already draw.
- [ ] `tests/test_project.cpp` — `removed` round-trips through `edits.json`.
- [x] `tests/test_kinematics.cpp` — a travel and an increment become a range and
      a step count; bump and rebound are not assumed equal; each kind keeps its
      own travel in its own unit; an increment of nothing is bounded rather than
      obeyed.
- [x] `tests/test_project.cpp` — every sweep's travel round-trips, not just the
      one being swept; an older project's single range becomes that kind's own
      travel and leaves the other two at their defaults.
- [x] `tests/CMakeLists.txt` — `test_kinematics` registered.

## 5. Verification

```bash
cmake --build --preset linux-debug && ctest --preset linux-debug

# one binary, or one case
./build/linux-debug/tests/test_kinematics
./build/linux-debug/tests/test_kinematics designPositionIsAFixedPoint

# headless render check (no --import: it opens a modal on warnings and hangs)
QT_QPA_PLATFORM=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  ./build/linux-debug/suspkin --screenshot /tmp/frame.png <project.suspkin>

./start.sh   # rebuild and launch
```

The invariant that matters most in testing: **link lengths do not change**.
Every rod in the mechanism has a fixed length by construction, so a sweep that
holds all of them to within a micron over its whole range has almost certainly
solved the right branch at every step.

## 6. Progress log

- **2026-09-09** — Phase A: `Vec3.h`, `GeomSolve.*`, `Mechanism.*`,
  `SuspensionSolver.*` written, added to `suspkin_core`, building clean under
  `-Wall -Wextra -Wpedantic -Wshadow -Wdouble-promotion`.
  `tests/test_kinematics.cpp` added and registered: **16 cases, all passing**.
  Two bugs found and fixed on the way, both worth remembering:
  - `intersectCircleSphere` reported a tangency as two coincident roots when the
    sphere reached the *far* side of the circle (spread of pi, not of zero). The
    branch picker would then have been choosing between a point and itself.
  - The first test fixture gave the rocker a 20 mm arm against a 336 mm pushrod,
    which puts it at the end of its travel at the design position. A rocker has
    to be proportioned like a real one or nothing either side of design solves.
    Worth knowing when a user's own numbers refuse to sweep.

- **2026-09-09** — Phases A, B and C landed. **31 kinematics cases green, all
  nine test binaries pass.**
  - Sweep layer: `AxleSolver` (both sides of one axle, sorted by where the wheel
    centre actually is), bump/roll/steer sweeps solved outward from the design
    position, roll-centre construction, installation ratio by central difference,
    anti-roll twist, CSV export, and a `SweepMeasure` API so the widgets stay
    dumb.
  - The shipped template now carries a `mechanism` block, and
    `theShippedTemplateSolvesTheCornerItDescribes` ties that JSON to the solver.
  - UI: `PlotWidget` (QPainter, no Qt Charts), `AnalysisPanel` dock, an
    `&Analysis` menu, `Ctrl+K`, CSV export, and the whole thing persisted under
    `view.simulation` in the manifest.
  - Two more bugs worth remembering:
    - The two sides measured their anti-roll arm angle about **opposite**
      directions, so a pure bump read as the bar twisting. Both arms are on one
      straight bar and must share one direction: `setAntiRollAxis(direction)`.
    - Solvers were bound from `setHardpointTable()`, but a project reads its
      points **before** its template, so they bound against an empty template and
      the dock silently did nothing. `rebuildLinkage()` now owns the whole chain
      (parts, solvers, sweep, pose), which removes the ordering hazard for good.
  - Verified end to end headless: a generated 76-point car workbook imports, the
    manifest round-trips `view.simulation`, and `--screenshot` at +30 / 0 / -30 mm
    renders three different frames.

- **2026-09-09** — Animation added on request ("I want to see the actual geometry
  animate ... to SEE the results"). Verified by rendering 36 headless frames of a
  +-30 mm bump sweep into a GIF: both corners move, the rockers turn, the dampers
  shorten. Also: a full 76-point two-axle workbook generator now lives in the
  scratchpad (`make_car.py`) built on `tools/make_test_xlsx.py`.

- **2026-09-09** — **The wheel's own axis**, asked for after watching the wheels
  slide about the car on steering lock without ever turning. Two things were
  wrong and they were the same thing: a wheel *model* was placed by translation
  only, and the wheel's attitude was inferred from the contact patch, which can
  carry camber but never toe.
  - `mechanism.upright.wheelAxis` (`{corner}_WheelAxis`) — a second point on the
    axle line, rigid with the upright, either side of the wheel centre. It is
    the design spin axis, so camber *and* static toe now come out of the table.
    The old contact-patch inference stays as the fallback, so nothing about a
    project without the point moves.
  - **The contact patch is computed**, per pose: `contactPatchFor()`, a tyre
    radius from the wheel centre straight down the wheel's own plane. It walks
    round the tyre as the wheel leans instead of being carried rigidly by the
    upright, which is what a tyre does and what makes scrub radius and track
    honest under camber. A patch named in the workbook now supplies the *ground
    height*, not the patch position. `mechanism.upright.contactPatch` is
    optional from here.
  - `CornerPose::uprightMotion` + `wheelCenterName` published, and
    `WheelPlacement` grew a rotation: `orientWheels()` puts each upright's turn
    on the wheel model bolted to it. The mirror is applied *before* the rotation
    in `wheelTransform()` — the far side's turn is that corner's own, measured
    on the car, and mirroring it steers the wheel the wrong way.
  - Ten new cases across `test_kinematics`, `test_wheels`, `test_linkage` and
    `test_hardpoint_config`; all eleven binaries green. Verified end to end on a
    real project (`LeonsScheis`): headless top-view frames at 0 and +25 mm of
    rack show the front wheels turned and the rear pair untouched.

- **2026-09-09** — **Which axle has a steering rack**, after a steer sweep on the
  rear axle walked `R_TieRod_I` out of the car. The sweep was doing exactly what
  it says — the steer input *is* lateral movement of the inboard tie rod ends —
  but nothing in a project said which axle has a rack, so every axle had one.
  - `corners[].steering` in the template names the hardpoint that axle's rack
    drives; an axle that names none is not steered. The rule that keeps old
    projects working: **a template that says nothing anywhere leaves every axle
    steered**, one that says something is taken literally
    (`LinkageTemplate::steeringDeclared()`, `CornerSpec::steeringStated`).
  - `AxleSolver::build()` injects the corner's answer into the mechanism before
    instantiating, so the rack point goes through `{corner}` and the mirror rule
    like every other role; `CornerSolver` gates rack travel on it and reports
    `isSteered()`. A rack named on any other point is a steering linkage this
    solve has no body for: warning, and the axle is treated as unsteered.
  - The dock greys out Steer for an axle with no rack and steps off it if that is
    where it was standing. The ordering hazard worth remembering:
    `applyViewState()` restores the *kind* before the *axle*, so the fallback has
    to run inside `setAxle()` — otherwise a project saved mid rear-steer reopens
    still steering.
  - Parts > Steering… writes it, by **patching** the template rather than
    reserialising it — same principle as the workbook writer. A project whose
    template predates the role and is recognisably the built-in one has the
    answer written in on open.
  - Verified on `LeonsScheis`: opening it migrates the template (front named,
    rear not), the stored `kind: steer` on the rear axle comes back as `bump`,
    and a rear steer pose renders pixel-identical to the bump pose it fell back
    to. The front axle still steers.
