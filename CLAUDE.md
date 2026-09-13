# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A native (Qt 6 / OpenGL 3.3) suspension-kinematics tool for Bremergy, on Linux and
Windows. It imports CAD geometry and a hardpoint workbook, shows both in a 3D
viewport, and lets the hardpoints be edited and written back. Bump/roll sweeps and
camber/toe plots are the intended next layer on top.

## Build, test, run

```bash
cmake --preset linux-debug        # configure (first time only)
cmake --build --preset linux-debug
ctest --preset linux-debug
./start.sh                        # rebuild and launch; args pass through
SUSPKIN_PRESET=linux-release ./start.sh
```

Presets: `linux-debug`, `linux-release`, `windows-msvc`, `windows-ninja`.

Run one test binary, or one case inside it:

```bash
ctest --preset linux-debug -R project            # by test name
./build/linux-debug/tests/test_project           # the whole binary
./build/linux-debug/tests/test_project editsRoundTripThroughTheirFile
```

Headless verification, which is how a change to the renderer or to project
loading gets checked without a human at the window:

```bash
QT_QPA_PLATFORM=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  ./build/linux-debug/suspkin --screenshot /tmp/frame.png <project.suspkin>
```

`--import <file>` imports geometry or an `.xlsx` on startup (dispatched by
extension). Note that an import which produces warnings opens a modal dialog, so
`--import` plus `--screenshot` will hang under `offscreen`.

STL and XLSX fixtures are generated at configure time by `tools/make_test_*.py`
(stdlib only). STEP fixtures are committed, because making one needs Open CASCADE.
The blank workbook a new hardpoint table starts from is generated the same way,
by `tools/make_blank_hardpoints_xlsx.py`, but for the *application*: it is
embedded in `suspkin_core` as `:/templates/blank_hardpoints.xlsx`, so python3 is a
build dependency and not only a test one. Every timestamp in it is fixed, so the
bytes compiled in do not change from build to build.

## The rule that shapes everything: the project owns all state

The application is **never stateless**. It opens with a project chooser and always
has exactly one project open; there is no meaningful mode without one. **Anything
the user does must end up in the project** — imports, hardpoint edits, mirroring
rules, camera position, display mode, label visibility, selection, window geometry
and dock layout. If you add a feature that has any state at all, persisting it in
the project is part of that feature, not a follow-up.

The mechanics are already in place, so this costs almost nothing:

- `MainWindow::markDirty()` starts a debounced timer (`kAutoSaveDelayMs`) that
  calls `saveProject()`. Call it from anything that changes state.
- View state is gathered in `MainWindow::collectViewState()` and restored in
  `applyViewState()`; add new fields to `ViewState` in `src/project/Project.h` and
  to both of those.
- `MainWindow::m_loading` is set while a project is being opened, so restoring a
  saved value does not read back as a change the user made.
- Saving also happens in `closeEvent()` and on `aboutToQuit`, so a run that ends
  without a window close (`--screenshot`, a session manager) still persists.

There is deliberately **no "unsaved document" dialog**. Nothing is unsaved, and
asking would imply otherwise.

## Project format

A project is a *directory*, not a file:

```
MyCar/
  project.suspkin      the manifest, JSON, read/written by src/project/Project.cpp
  geometry/            the imported STL/STEP, copied in
  hardpoints/
    <name>.xlsx        the imported workbook, copied in, kept as imported --
                       or hardpoints.xlsx, made here from the blank workbook
    edits.json         hardpoint changes not yet written into that workbook
  linkage/
    template.json      which parts join which hardpoints
  wheels/
    tyre.step          the tyre model, copied in (wheel.step in a project
                       made before a wheel meant tyre and rim together)
    rim.step           the rim model, copied in
```

Imported files are **copied into the project** (`Project::importAsset`) so a
project stays openable when the original moves or its share is unmounted. The
manifest keeps `importedFrom` for reference only — never load from it.

The one piece of state that cannot live in a project is the list of projects, in
`src/app/RecentProjects.*` (QSettings, paths only). The updater's two settings --
whether to check on startup, and which build the user said to stop asking about --
are in QSettings for the same reason: they are about this installation, not about
any project, and they have to outlive every project the user opens.

## Hardpoints: baseline, edits, workbook

This three-way split is the part most likely to be broken by a careless change:

- `MainWindow::m_baseline` is the table **exactly as the workbook holds it**, read
  from the project's own copy on open.
- The live table is `HardpointModel::table()` — the baseline with the user's work
  applied.
- `diffHardpoints(baseline, current)` produces `HardpointEdits` (`changed`,
  `added`, `removed`, and `addedAfter` -- the name of the point above each added
  one), which is what `hardpoints/edits.json` stores. `applyHardpointEdits()` is
  its inverse and is how a project reopens where it was, rows in the order they
  were left in. An added point with no `after` is from an older edits file and
  goes at the end, where it always went.
- **A rename of a workbook point is a removal plus an addition.** The name is the
  key the workbook is written back through, so nothing special-cases it: the diff
  sees one name gone and one arrived, and the writer empties the old rows and
  appends the new ones. A point that exists only in `edits.json` renames as
  itself.

The workbook copy is **not** touched by editing. It changes only when the user asks:

- **Overwrite Workbook** rewrites the project's copy, then *re-reads it* so the
  baseline and the cell map are current. Skipping that re-read would append the
  mirrored rows a second time on the next overwrite — see the test named for it.
- **Export Workbook As** writes elsewhere (defaulting to `importedFrom`) and leaves
  the edits pending.

`writeHardpointsXlsx()` splices bytes rather than reserialising XML: cells that
exist are patched in place, points that do not exist in the workbook (what
mirroring and Add Point produce) are appended as new rows before `</sheetData>`,
points the workbook has and the table does not have their **name and value cells
emptied** -- never the whole `<row>`, which would take a unit or a note in the
next column with it -- and every other ZIP member is copied through with its
original compressed data. That is what keeps number formats, other sheets, charts
and vendor parts intact. Untouched coordinates are written back with the exact
text the file already held. `XlsxHardpointSource::lastRow` is the last `<row>`
the sheet has an element for, not the last with a value: a formatted empty row, or
a deleted point's emptied cells, is still a row an appended one must not collide
with.

**A project can hold points without an imported workbook.** Hardpoints > New
Hardpoint Table and Generate from Design both go through
`Project::createHardpointWorkbook()`, which writes the blank resource workbook
filled with the points (`writeNewHardpointsXlsx()`, the ordinary append path
over a hand-built `blankHardpointSource()`), and the window then **reads it back**
as the baseline, exactly as Overwrite Workbook does. Never read the blank one
first: `readHardpointsXlsx()` refuses a sheet with no points in it, which is why
there has to be at least one point before there is a workbook -- and why
Overwrite Workbook refuses a table with every point deleted.

Mirroring itself is pure and testable: `src/model/HardpointMirror.*`. A `MirrorSpec`
(axis, naming rule, affix or find/replace) is stored in the project because it is
the user's own naming convention.

## Hardpoints: what each point is *for*

Beside its coordinates, each point carries a `HardpointConfig`
(`src/model/HardpointConfig.*`): a `PointType` -- the solver constraint -- the two
bodies that meet there, and a bushing index. The configuration table in the
hardpoint dock is the one place all of that is edited.

- It is stored **by name** in the manifest, under `hardpoints.config`. Not in the
  workbook, which has nowhere to put it, and not in `edits.json`, where it would
  read as a coordinate change the user never made. By name and not by row because
  mirroring appends rows and a reimported workbook may be in a different order.
- The bodies a Part column may name come from the project's own linkage template
  (`bodyCatalog()`), so a project describing a different car offers that car's
  bodies. The ground body is a fixed, untranslated string: it is written into
  project files.
- `inferHardpointConfig()` fills a table in from the template -- types from the
  `mechanism` block, bodies from the parts the template actually draws through
  each point -- so the configuration and the linkage cannot disagree about what a
  corner is made of. On a solved joint, Part 1 is the member the point belongs to
  (the body it shares with the far end of its own link) and Part 2 is what that
  member is attached to, so a tie rod end reads "tie rod, upright" rather than
  whichever way round the template happened to list its parts.
- `fillMissingConfig()` never touches an entry that already exists, *including an
  empty one*: that is how "the user cleared this row" is told apart from "nobody
  has got to it yet".
- `validateHardpointConfig()` is pure and lives in the core. `HardpointModel::setData()`
  refuses an **error** before the store changes and reports it through
  `editRejected()`; a **warning** is stored and shown as a dot on the row, because
  half-finished is the normal state of a table on its way somewhere. Anything
  inference produces has to pass without a warning -- there is a test that says so.
- Nothing in the solver reads this yet. `CornerSolver` takes its roles from the
  same `mechanism` block the inference does, which is why the two agree; when the
  solver does start reading it, that is the seam.

## Hardpoints: adding, deleting, renaming

- `HardpointModel::insertPoint()` and `removePoints()` go through
  `beginInsertRows`/`beginRemoveRows`, not a reset, so the panel's sort and filter
  proxy keeps its place. `renamePoint()` (also the Name column's `setData()`) is
  guarded by `hardpointNameProblem()` in the core -- empty, taken, padded with
  spaces the reader trims, or ending in `_x`/`_y`/`_z` -- and refuses through
  `editRejected()`. It moves the point's configuration and every `mirrorOf` that
  named it; the window moves the `WheelSpec` corner that named it.
- **Every add, delete and rename ends in `syncTableToViewport()`** (the half of
  `setHardpointTable()` after the model): parts are indices into the table, so
  skipping it draws them between the wrong points.
- **`captureHardpointConfig()` comes before that sync, not after.** Resolving the
  linkage again refills the model's configuration from the *project's* copy
  (`refreshHardpointConfig()`), so a rename or delete not yet folded into the
  project is silently undone by the refresh.
- The selection is a list, in picking order: Ctrl+click in the viewport,
  Ctrl/Shift in the table (`ExtendedSelection`). `ViewState::selection` persists
  it next to `selectedHardpoint`, and an older manifest's single `selected` reads
  back as a selection of one. The name column is edited in the *frozen* view --
  `FrozenColumnView::edit()` forwards there, because an editor opened on the main
  view's name cell sits underneath the frozen overlay, out of sight.

## Generating a corner from design targets

`src/model/HardpointGenerator.*` is section 3 of
`docs/HARDPOINT_GENERATOR_PLAN.md`: vehicle targets (`DesignParameters`, stored in
the manifest under `design`) in, roles out. Pure and in the core.

- **It produces roles, never names.** `bindGeneratedCorner()` names them through
  the project's own `MechanismTemplate`, and a role the template does not name is
  not written. `planDesign()` does the rest: every axle asked for, the far side
  through the project's `MirrorSpec`, and a `DesignChange` per point -- added,
  moved by how much, and whether that overwrites the user's own edit (it differs
  from the baseline and is not simply the mirror of its source).
- The contact patch comes from the solver's own `contactPatchFor()` and
  `tireRadiusToGround()`, and `test_hardpoint_generator` feeds generated corners
  back through `CornerSolver`: camber, toe, caster, kingpin, scrub and trail come
  back to 1e-9. Keep it that way -- the generator and the solver are inverses.
- **So is the roll centre**, because both take the front-view instant centre
  the RCVD way: the line the two wishbone *planes* share (each through its pivot
  axis and its ball joint), pierced by the transverse plane through the wheel
  centre -- `planesCrossing()` in `CornerSolver::measure()`. Do not go back to
  drawing each arm from its pivot to its ball joint and flattening that: it
  ignores a caster's worth of fore-aft offset, and on an inclined pivot axis it
  read a real car's front roll centre as 6.3 mm where its workbook said 10.
- **Generation is one-shot.** The dialog previews; nothing moves until Generate.
  The targets are saved even on Cancel -- they are the user's work -- but
  changing one never re-positions a point by itself.
- It writes each generated axle's steering into the template, and only when that
  changes what the corner already means; a template that said nothing gets the
  other corners stated as steered, so they keep the steering they always had.
- The coplanarity advice is advice. It is shown and put on the status bar, never
  applied. With both pivot lines at one distance from the centreline the four
  pivots are already in one plane, and there is no advice to give.
- A generated axle that has **stated static angles** gets the camber and toe it
  was generated with written over them, before the points go in. Otherwise the
  stated angles would outrank the wheel axis just placed, and the scrub, trail
  and roll centre built off the generator's camber would come out wrong. An axle
  with none keeps reading its angles off the new points.
- The rocker group is not generated: packaging, placed by hand.
- `src/geom/MeshQuery.*` (a BVH, ray casting and unsigned distance) is what puts
  inboard pivots on the imported chassis. A ball joint already inside the
  geometry -- an upright in the model -- is detected and those pivots go on their
  lines with a warning. A ray that misses says so; nothing is invented.

The table itself (`src/app/HardpointPanel.*`, `HardpointModel.*`,
`HardpointDelegates.*`) draws its colours from the palette rather than carrying a
theme, freezes the number and name columns behind a second view of the same
model, and puts every chip and dropdown in a **delegate** rather than a widget in
a cell -- which is what keeps the view virtualised. A row is a fixed height for
the same reason. Do not reach for `setIndexWidget()` here.

## Parts: the linkage template

What is drawn between the hardpoints is data, not code. `linkage/template.json`
inside the project describes parts as chains of hardpoint names; the built-in one
is `resources/templates/double_wishbone_pushrod.json`, embedded in `suspkin_core`
as `:/templates/...` so the tests read the same bytes the application ships.

- `src/io/LinkageTemplate.*` reads and writes the file. `src/model/Linkage.*`
  resolves a template against a `HardpointTable` into a `Linkage` of parts, each
  a list of chains of **indices into that table** -- which is also the order the
  viewport holds its markers in, so `moveHardpoint()` keeps the parts attached
  without re-resolving.
- Because they are indices, `ViewportWidget::setHardpoints()` drops the linkage;
  whoever sets the points sets the parts again. That is `rebuildLinkage()`, and
  it is called from `setHardpointTable()` for exactly that reason.
- A template is written once per corner (`{corner}`) and for one side. The far
  side comes from the project's own `MirrorSpec` via `mirroredName()`, so a
  template never encodes a naming convention for left and right.
- A corner or a side with **no** points in the table is skipped silently: a
  workbook may hold one axle, or may not have been mirrored yet. Only a corner
  that partly resolves produces warnings. Do not "fix" that into warning about
  everything -- the noise buries the real misses.
- A part with `"perCorner": false` names its points literally and is drawn
  exactly once -- no `{corner}`, no mirror. That is what Linkage > New Part from
  Selection writes. It belongs to no corner, so the silence above does not cover
  it: a literal part that has lost a point says so.
- Linkage > New Part from Selection and Edit Parts edit the template through
  `addTemplatePart()`, `removeTemplatePart()` and `setTemplatePartLabel()`: text
  patches, each checked by parsing the result against what a rewrite would say,
  with the rewrite as the fallback -- the same safety net `setTemplateSteering()`
  has. A relabelled part renames its body in the configuration too
  (`renameBody()`), or every row naming it would turn red.
- A project without a template gets the built-in one written in on open
  (`installBuiltinLinkageTemplate()`), so it becomes an ordinary project file the
  user can edit. A template that fails to parse is *not* repaired by overwriting.
- `mechanism.upright.wheelAxis` is a second point on the **wheel's own axis of
  rotation** (`{corner}_WheelAxis`), rigid with the upright. It is what says
  which way the wheel points -- static toe, which no other hardpoint in a table
  can state, and static camber with it -- and it is what the wheel *model* is
  turned by. With it named, `mechanism.upright.contactPatch` is optional: the
  patch is **computed**, a tyre radius from the wheel centre straight down the
  wheel's own plane (`contactPatchFor()`), and it is recomputed at every pose
  rather than carried rigidly, because a tyre stays on the road while the wheel
  leans. What a named patch supplies is the **height of the ground**, for a
  workbook measured from a chassis datum; without one the ground is `z = 0`.
  With no axis point named, the old rule stands -- the axis is inferred from the
  patch under the wheel centre, zero toe assumed -- and the computed patch comes
  out exactly where the workbook put it, so nothing about an older project moves.
  Static angles the project states outrank both; see "Static camber and toe".
- **Which axle has a steering rack is the corner's business, not a point's.**
  A `corners` entry may name `"steering"`: the hardpoint that axle's rack drives
  (the inboard tie rod end, with `{corner}` still in it). An axle that names none
  is not steered -- rack travel leaves its toe link alone and the analysis dock
  does not offer it a steer sweep. The compatibility rule is the whole of it:
  **a template that says nothing anywhere leaves every axle steered**
  (`LinkageTemplate::steeringDeclared()`), which is what every project written
  before the role existed relies on; one that says something is taken literally.
  `CornerSpec::steeringStated` is why "no axle on this car steers" survives a
  reload instead of reading as a template that was never asked.
  This is not a `PointType`: both ends of a toe link are bolted to something on
  either axle, and what tells a rack from a bracket is the car, not the joint.
  `AxleSolver::build()` puts the corner's answer into `MechanismTemplate::
  steeringRack` before instantiating, so it goes through the same `{corner}`
  substitution and the same mirror rule as every other role.
- **Linkage > Steering Rack...** edits it, and the template is *patched* rather
  than rewritten (`setTemplateSteering()`) for the same reason
  `writeHardpointsXlsx()` splices a workbook: the file is the user's, and notes,
  parts and keys a later release adds have to come out the other side. A project
  whose template predates the role and is recognisably the built-in one gets the
  answer written in on open (`MainWindow::adoptTemplateSteering()`); one that is
  somebody's own is left alone and said so about in the status line. The dialog
  asks it the way the car is described -- where is the rack attached -- not in
  terms of what the solver does with the answer.
- **A project file that is about to be replaced is read through
  `Project::readFile()`**, which has closed it again by the time it returns.
  `writeFile()` replaces a file by renaming a temporary over it, and Windows
  refuses to replace a file any handle has open, this process's own included. A
  `QFile` still open in the scope of the write is what made the steering fail
  there with "Access is denied" while Linux let it through.
- A template with **no `mechanism` block** -- one written before the solver
  existed, which is what older projects still hold -- is read with the built-in
  mechanism assumed and `LinkageTemplate::mechanismAssumed` set. Without it the
  parts draw while nothing else works: no corner solves, and every row of the
  hardpoint table reads "unassigned". The fallback lives in the *reader* so that
  every consumer sees the same roles; `builtinLinkageTemplate()` is read with it
  disabled, so it can never be asked to fall back to itself.

## Wheels

**Geometry > Add Wheels** puts a copy of one tyre model and one rim model at
each of four hardpoints the user picks -- a wheel being the two together. The
placement is pure and testable: `src/model/Wheels.*`.

- The `WheelSpec` the project stores names *hardpoints*, not coordinates, so a
  wheel centre that gets edited in the table takes its wheel with it
  (`rebuildWheels()` is called from the coordinate-edit path for that reason).
- **A wheel is bolted to its upright, so it turns with it.** `WheelPlacement`
  carries a rotation as well as a centre: `MainWindow::wheelRotations()` reads
  `CornerPose::uprightMotion` out of the current poses, keyed by
  `CornerPose::wheelCenterName`, and `orientWheels()` puts it on the placements.
  Identity when nothing is being simulated, which is the model as its CAD file
  drew it. Without this the models slide about the car on steering lock without
  ever pointing anywhere -- which is what they used to do.
- The mirror comes *before* the rotation in `wheelTransform()`. The rotation is
  a real one, measured on that corner of the car; mirroring it would steer the
  far wheel the wrong way.
- **In a roll the body rolls, and takes the wheels with it.** A roll sweep is
  solved in the car's own coordinates with the road tilted under it, which is
  where its numbers belong, and they stay there. The viewport draws the other
  picture: a level road, and the monocoque turned about the roll axis through
  the axles' design roll centres (`rollAxisThrough()`, `bodyRollMotion()`),
  because turning about the roll centres is what leaves the contact patches
  where they were. `MainWindow::m_bodyMotion` moves every point in
  `posedTable()`, the geometry through `ViewportWidget::setMeshTransform()`, and
  each wheel after its upright's own turn. Only when every axle is posed: a body
  cannot roll with one left behind, so with "Move all axles" off the chassis
  stays put.
- **The corner decides which side a copy is on, not the sign of y.** The user
  says which point is the front left one. `WheelModelSide` says which side the
  models were drawn for, and the copies on the other side are mirrored in Y --
  a rim is dished, so one model cannot simply be dropped onto all four centres.
- The anchor that lands on the hardpoint is the centre of the model's *own*
  bounding box, computed per body, because the wheel and the rim are two
  different boxes. `alignToCenter = false` places by the model's own origin
  instead, for a model already positioned in vehicle coordinates.
- Both models are copied into `wheels/` under fixed names (`tyre.<ext>`,
  `rim.<ext>`) through `Project::importAssetAs`, because two files that happen
  to be called the same thing would otherwise be one file. `WheelsRef::tyre` is
  written under `"tyre"`; a manifest from before that has it under `"wheel"`,
  pointing at `wheel.<ext>`, and still reads. Nothing on disk is renamed.
- The viewport keeps the models and the placements apart: `setWheelModels()`
  re-uploads meshes, `setWheelPlacements()` is a handful of matrices. One mesh
  is uploaded once and drawn per corner with its own model matrix, so four
  wheels cost four draw calls rather than four buffers.

## The analysis plots

`src/app/PlotWidget.*` draws one measure of a sweep; `AnalysisPanel` holds as many
as the Curves menu has ticked, in a grid that reflows with the dock's width and
scrolls when they do not fit.

- **Colours come from the palette**, mixed from `Base` and `Text`, the same rule
  as the hardpoint table. The hover readout is `Window` and `WindowText`: a fixed
  white box written in the palette's text colour was white on white on a dark
  desktop.
- **A curve is never stretched past `sweepMeasureResolution()`** -- a tenth of a
  millimetre, a hundredth of a degree. Less movement than that over the whole
  sweep is drawn flat in a band that wide. Without it a roll centre sitting on
  the centreline through a bump filled the plot with the solver's rounding,
  under an axis that read "0.000" at every tick. Readouts go through
  `sweepValueText()`, which never prints "-0.000".
- Which curves are plotted is `SimulationState::measures`, by key, in menu
  order. `"measure"` is still written beside `"measures"` -- the first of them --
  the way `"selected"` sits beside `"selection"`, and an older manifest's one
  `"measure"` reads back as a list of one.
- Which wheels they draw is `SimulationState::sides` (`SweepSides`: both, left,
  right), next to the Curves button. It reaches every plot (`setSides()`) and the
  readout, which hides the other column and writes the axle-wide rows into the
  one left showing. Axle-wide measures are one curve whatever it says.
- **Camber** is to the body; **Camber to ground** (`CornerPose::camberToGround`)
  is to the road. They are one number except in a roll sweep, where the road is
  tilted under the car (`z = -y tan(roll)`) and they part by the whole roll
  angle. `measure()` sets it equal to camber; `poseFor()` overwrites it in roll,
  from the tilted road's normal -- the corner does not know the roll angle.
- **The installation ratio is compression per bump**, positive for a damper
  bump compresses, the way a motion ratio is quoted. Until 2026-09-12 it was the
  length change, so the user's car read -1.04; the tests used to check only its
  magnitude, which is how that went unnoticed. They check the sign now.

## Static camber and toe

**Linkage > Static Camber and Toe** is Lotus's Set Static Angles: per axle, two
numbers (`StaticAlignment`, degrees, signed like the measures), stored in the
manifest under `alignment`, keyed by corner token. Not the template, which says
what the car is made of, and not the workbook, which holds points.

- **Precedence in `CornerSolver::bind()`**: stated angles, else the table's
  `{corner}_WheelAxis` point, else the contact patch under the wheel centre
  (camber only, zero toe), else an upright wheel. `CornerSolver::wheelAttitude()`
  says which; the dialog shows it, so an unticked axle tells the user where its
  numbers came from. An axle not in `alignment` behaves exactly as before.
- `spinAxisFor()` builds the axis so the measures read the angles straight back
  -- camber is the axis's inclination, toe its plan angle -- and the far side
  gets the same pair through `AxleSolver::build()`, which is its mirror image.
- With angles stated, the contact patch is computed (as it already was with a
  wheel axis point) and a patch *point* supplies only the ground height; a wheel
  axis point is re-placed on the stated axis at its own distance from the
  centre, so the posed marker is the axis actually used.
- The dialog builds each axle once **without** the project's angles to find
  what the hardpoints say; a newly ticked axle starts from there, not from zero.
  OK replaces the angles of every axle it showed and keeps those of an axle that
  does not solve today.
- The static camber the team's workbook carries is easy to miss: a patch 0.8 mm
  outboard of a wheel centre 228.6 mm up is -0.2°. That, not the solver, is what
  made the camber curves disagree with Lotus.

## Comparing with Lotus

Every measure was checked on 2026-09-12 against an independent solve of the
26_DY car (Newton on the upright as a free rigid body, no shared code): 146 CSV
columns, bump, roll and steer, both axles, with and without stated angles, all
within the CSV's 5e-5 rounding. A disagreement with Lotus is a convention, not
arithmetic: Lotus's frame is X rearward and **Y right**, its positive roll leans
the body **left** (ours, right-handed about +X forward, lifts the left side), its
rack travel is likely signed the other way, its static angles come from Set
Static Angles, and in roll it may be plotting camber to the ground. Camber, toe,
caster and kingpin are signed the same in both. README's Analysis section has
the table.

## Updating itself

An installed copy checks GitHub on startup and offers to replace itself. Three
pieces have to agree, and they are in three different files:

- The exe carries `SUSPKIN_BUILD`, the release workflow's run number, passed in
  as `-DSUSPKIN_BUILD_NUMBER=`. It is **0** for a local build, and build 0 is
  never out of date — otherwise a developer's own build would offer to overwrite
  itself with a release one.
- The release publishes `version.json` beside the binaries, written by the
  workflow's "Write the update manifest" step. It is fetched from
  `releases/latest/download/version.json`, which is a fixed URL because the tag
  is rolling. No API call, so no token and no rate limit.
- `packaging/windows/suspkin.iss` has a `[Run]` entry gated on `/RELAUNCH=1`.
  The updater runs the installer with `/SILENT`, which makes Inno skip the
  ordinary post-install launch, and a silent update that never came back would
  look like a crash. Nothing else passes that switch, so an unattended install
  still starts nothing.

The build number is what is compared, not the version: the tag is always
`latest` and `VERSION` in CMakeLists barely moves, so neither can answer "is
this newer than what I am".

**The installer is downloaded and executed, so it is verified first.** The
manifest carries a SHA-256 and a size; `UpdateChecker` refuses anything that
does not match, and `parseUpdateManifest()` refuses a manifest whose URL is not
HTTPS on a GitHub host. Do not relax either — they are the whole reason this is
an updater rather than a way to run what the network hands over.

Only `InstallKind::Installed` ever updates, detected by the `unins*.exe` the
installer leaves beside the exe. A portable unzip never even asks the network:
Windows holds its own files open, so nothing can overwrite them from inside, and
there is no uninstaller to repair a half-finished swap. Linux is packaged by
pacman, which owns those files.

## Layout and layering

```
src/geom/     Aabb, TriMesh, vertex welding and edge extraction, and MeshQuery:
              rays and distances against an imported mesh
src/model/    Hardpoint, HardpointTable, hardpoint mirroring, what each point
              is for and the rules that check it, the linkage a template
              resolves to, where the wheel models are placed, the solver and
              its sweeps, and the generator that runs it the other way:
              design targets to hardpoints
src/io/       STL and STEP readers behind importMeshFile(), a minimal ZIP
              reader/rewriter, the hardpoint workbook reader/writer, and the
              linkage template reader/writer
src/project/  the project format: manifest, assets, view state, hardpoint edits
src/update/   what a release says about itself: parsing and comparing the
              version.json a release publishes. Pure, so it is tested without
              the network
src/render/   Camera, GPU buffers, the OpenGL viewport, gizmo, mode selector
src/app/      MainWindow, AppController, the project launcher, the mirror dialog,
              the hardpoint configuration table -- its model, its delegates and
              its dock -- and the dialogs for a new point, a generated corner
              and the template's parts
```

`suspkin_core` is a static library with **no OpenGL and no widgets** — geometry,
IO, the project format and the camera all live there so they can be tested
headlessly. `Camera` and `DisplayMode` are in `src/render/` but belong to the core
library for exactly that reason. Keep it that way: if something new is testable
without a graphics context, it goes in the core.

`AppController` owns the single `MainWindow` and swaps it out wholesale when the
user changes project, so nothing survives from one project into the next. Its
signals from `MainWindow` are queued, because a window cannot be destroyed from
inside its own menu handler. `QApplication::setQuitOnLastWindowClosed(false)` is
set in `main()` because there is a moment between two projects with no window.

## Conventions worth knowing

- Coordinate frame is **ISO 8855 / DIN 70000**: X forward, Y left, Z up,
  right-handed. Mirroring "to the other side" therefore negates **Y**.
- Hardpoint coordinates are `double` end to end; only the renderer sees `float`.
  Rounding a value the user never touched would show up in their file as a change
  they did not make.
- Failures come back as messages (`MeshLoadResult::error`, `HardpointLoadResult`,
  a returned `QString`), never as exceptions, so the caller can put the text
  straight in front of the user.
- The camera is a turntable with a fixed world up, not an arcball — "up" is what
  camber and caster are read against.
- **A path handed to Open CASCADE is UTF-8**, not the local 8-bit encoding:
  `path.toUtf8()`, never `QFile::encodeName()`, whose Windows encoding is still
  the ANSI codepage. One umlaut in the user's own home directory was enough to
  make every STEP import there report an unreadable header.
- Warnings are on but `-Werror` is not, on purpose.
- `CMakeLists.txt` uses the range form `3.24...3.28`: this machine has both CMake
  3.28.1 (from STM32CubeCLT, first on `PATH`) and 4.4.1.
