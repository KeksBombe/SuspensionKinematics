# Take MainWindow apart: a session layer, and the end of the command migration

## Context

`src/app/MainWindow.cpp` is 2356 lines and `MainWindow.h` is 369. The class is
three jobs wearing one hat:

1. **The window** — the viewport, two docks, the ribbon, the File menu, the
   status line. About 380 lines, and genuinely its own.
2. **The live state a project is unfolded into** — the hardpoint baseline and
   its workbook, the linkage template and the parts resolved from it, the bound
   axles and their sweep, the wheel placements, the chassis mesh. About 900
   lines, held in 30 members that every cluster reaches into.
3. **Twenty-four command bodies** — the dialogs behind Import, Mirror,
   Overwrite, Generate, Steering Rack, Add Wheels and the rest. About 1000
   lines, and they are already listed, by name, in
   `src/app/framework/WindowActions.h` as work waiting to move:

   > It only ever shrinks: moving a body into its feature deletes a line from
   > here. When it is empty, delete the file -- that is the end of the
   > migration.

Job 3 has a destination (`src/app/features/`) and cannot reach it, because every
one of those bodies reads or writes job 2's members, which are private to the
window. So the migration stalled at 24 forwarding calls through `WindowActions`.

This plan gives job 2 a home of its own — `src/app/session/` — and then finishes
job 3. `WindowActions.h` and `.cpp` are deleted at the end, and MainWindow is a
window again.

**Behaviour does not change.** Every step is a move, and CLAUDE.md's rule holds:
refactor and behaviour changes go in separate commits.

## The shape

```
src/app/
  session/     NEW -- the live state a project is unfolded into.
               No widgets, no dialogs: failures come back as a QString the
               caller puts in front of the user, the way MeshLoadResult::error
               and HardpointLoadResult already do. Each one is therefore
               testable the way HardpointModel is -- compiled straight into a
               test binary, no window.

    ProjectSession    the Project itself, the debounced save, and the cascade
                      that resolves everything again when the table moves
    ChassisDocument   the imported mesh, its copy in the project, the MeshQuery
                      the generator casts rays at
    HardpointDocument the baseline, the workbook it came from, the edits
                      between them, and the configuration
    LinkageDocument   the template, the steering answer, and the parts
                      resolved against the table
    SimulationRunner  the bound axles, the sweep and the pose
    WheelsDocument    where the wheel models are placed

  framework/   the layer features register into. AppContext now hands out the
               session instead of a flat getter per field.
    WindowChrome      NEW -- the File menu, the recent list, the ribbon, and
                      giving every command its tooltip and its shortcut
    WindowActions.h/.cpp   DELETED at the end of the migration

  features/    the 24 command bodies land here, one per area, and each feature
               collects and restores its own slice of the view state

  PointEditController.h/.cpp  NEW -- the two ways a point is moved in the
               viewport: the arrows, and the field X, Y and Z open

  MainWindow.h/.cpp  the window: build, wire, chrome, AppContext
```

## Why the state is widget-free

Every command body today pops its own `QMessageBox` and `QFileDialog`. Moving
them as they stand would meet the line target and teach us nothing: the state
would still need a window to run.

Instead, a document takes plain data and reports failure the way the rest of
this codebase already does:

```cpp
// session/HardpointDocument.h -- nothing from QtWidgets
bool overwriteWorkbook(QString* error);
bool adoptNewWorkbook(const HardpointTable& table, QString* error);
```

```cpp
// features/HardpointsFeature.cpp -- the dialog, and only the dialog
QString error;
if (!doc.overwriteWorkbook(&error))
    QMessageBox::warning(m_context.window(), tr("Overwrite Workbook"), error);
```

That is CLAUDE.md's own convention ("Failures come back as messages ... never as
exceptions, so the caller can put the text straight in front of the user"), and
it is what makes the new test binaries possible.

## The classes

### `session/ProjectSession`

Owns the `Project`, the save timer, the loading flag, and the five documents.
Its one job beyond that is the cascade: when the table moves, everything
resolved against the table is resolved again.

```cpp
class ProjectSession : public QObject {
    Q_OBJECT
public:
    explicit ProjectSession(Project project, QObject* parent = nullptr);

    Project& project();
    const Project& project() const;

    ChassisDocument&   chassis();
    HardpointDocument& hardpoints();
    LinkageDocument&   linkage();
    WheelsDocument&    wheels();
    SimulationRunner&  simulation();

    /// Note that something worth persisting changed, and schedule a save.
    void markDirty();
    /// Write the project out now: the manifest, and the pending edits.
    bool save(QString* error);

    /// Set while a project is being opened, so restoring a saved value does not
    /// read back as a change the user made.
    bool loading() const;
    void setLoading(bool loading);

    /// Everything that is resolved against the table, resolved again: the
    /// parts, the configuration, the bound axles, the sweep, the pose, the
    /// wheels. What syncTableToViewport() used to do the model half of.
    void resolveFromTable();
    /// Only the half the analysis panel drives: the sweep and the pose.
    void resolveSimulation();

signals:
    void dirtied();
    /// The parts, the pose or the placements are new. The window pushes them.
    void derivedChanged();
};
```

`resolveFromTable()` is the orchestration that `rebuildLinkage()`,
`rebuildSolvers()`, `refreshSweep()`, `applySimulation()` and `rebuildWheels()`
call each other through today. It stays one method in one place, and it stays
readable, because each step is one call into one document.

### `session/HardpointDocument`

The three-way split CLAUDE.md warns is "the part most likely to be broken by a
careless change" — baseline, edits, workbook — behind one object.

Holds `m_baseline`, the `XlsxHardpointSource`, and the `HardpointModel` itself —
which is a `QAbstractTableModel` over core types with no widget in it, so it
belongs to the document rather than to the dock that displays it. It stays in
`src/app/HardpointModel.*`, where `test_hardpoint_model` already compiles it;
only its owner changes. `AppContext::hardpoints()` becomes
`session().hardpoints().model()`, so the nine features see no difference.

That settles the construction order in MainWindow: the session first, then the
docks built against `session->hardpoints().model()`, then the commands, then the
ribbon, then `openProjectContents()`.

Absorbs: `loadHardpointsFromProject`, `loadHardpointFile`,
`writeHardpointsTo`, `overwriteWorkbook`, `exportWorkbookAs`,
`adoptNewWorkbook`, `removeHardpoints`, `pendingEdits`, `applyMirrorProvenance`,
`captureMirrorProvenance`, `refreshHardpointConfig`, `captureHardpointConfig`.

The rules that must survive the move, verbatim from CLAUDE.md:
- Overwrite Workbook rewrites the project's copy **and then re-reads it**, or
  mirrored rows are appended twice on the next overwrite.
- `captureHardpointConfig()` comes **before** the sync, never after.
- A new workbook is written from the blank resource and **read back** as the
  baseline; the blank one is never read first.

### `session/LinkageDocument`

Holds `m_linkageTemplate`, `m_linkage` and `m_steeringNote`. Absorbs:
`loadLinkageTemplateFromProject`, `installBuiltinLinkageTemplate`,
`adoptTemplateSteering`, `patchLinkageTemplate`, `rebuildLinkage`, and the
template edits behind New Part, Edit Parts, Steering Rack, Import Template and
Reset Template — the *patching*, that is; the dialogs stay in `LinkageFeature`.

`patchLinkageTemplate()` keeps reading through `Project::readFile()`, which has
closed the file again by the time it returns. That is not tidiness: Windows
refuses to replace a file this process still has open.

### `session/SimulationRunner`

Holds `m_simulation`, `m_sweep` and `m_pose`. The panel is a widget, so the
runner does not see it: it takes a request of plain data and the window reads
that off the panel.

```cpp
struct SimulationRequest {
    QString axle;
    SweepSpec spec;
    double position = 0.0;
    bool moveAllAxles = false;
    bool simulating = false;
    /// The sweep is the most expensive thing here and there is nothing to draw
    /// a curve on while the dock is shut.
    bool sweepWanted = true;
};
```

```cpp
void setRequest(const SimulationRequest& request);
void rebind(const LinkageTemplate&, const HardpointTable&, const MirrorSpec&,
            const QHash<QString, StaticAlignment>&, const QString& steeringNote);
void run(const HardpointTable& table);        // the sweep, then the pose
const Simulation& simulation() const;
const SweepResult& sweep() const;
const SimulationPose& pose() const;
QList<AxleEntry> axleEntries() const;         // what the panel's box offers
QStringList status() const;                   // the note, then the warnings
```

### `session/WheelsDocument` and `session/ChassisDocument`

`WheelsDocument` holds `m_wheelPlacements` and absorbs `loadWheelsFromProject`,
`rebuildWheels`, `applyWheels` and `removeWheels`. The mirror still comes before
the rotation in `wheelTransform()`, and the upright's turn before the body's.

`ChassisDocument` holds the mesh, the `MeshQuery` built on first use, and
absorbs `loadGeometryFromProject`, `loadFile` and `removeChassis`. The mesh
itself is handed to the window to upload; the document keeps what rays are cast
at.

### `framework/WindowChrome`

`buildFileMenu()`, `refreshRecentProjectsMenu()`, `buildRibbon()` and
`finishCommands()` are the window's furniture rather than any feature's, and
they are the same four every session. A free-function header beside
`RibbonPages.h`:

```cpp
QMenu* buildFileMenu(QMainWindow* window, const CommandRegistry& commands);
void   buildRibbon(QMainWindow* window, Ribbon* ribbon, const CommandRegistry&);
void   finishCommands(QMainWindow* window, const CommandRegistry& commands);
```

`setMenuWidget(m_ribbon)` still happens here and `QMainWindow::menuBar()` is
still never called — on a window whose menu widget is not a `QMenuBar` it makes
one and `deleteLater()`s the ribbon.

### `PointEditController`

`buildPointEditing()`, `openCoordinateEntry()`, `moveHardpointCoordinate()` and
`showDragPosition()` — the arrows and the field, both ending in the same
`HardpointModel::setData()` so a dragged point and a typed point are one edit.
Owns the `CoordinateEntry` and the row it is open on. Takes the viewport, the
model and a status-line callback.

### View state moves to the features

`collectViewState()` and `applyViewState()` are 83 lines of MainWindow reading
and writing widgets that belong to features. Two new hooks on `Feature`:

```cpp
virtual void collectViewState(ViewState& view) const {}
virtual void applyViewState(const ViewState& view) {}
```

`ViewFeature` takes the camera, the display mode and the three visibility
toggles; `AnalysisFeature` takes the whole `SimulationState`; `HardpointsFeature`
takes the selection. This is CLAUDE.md's rule stated as code: *"If you add a
feature that has any state at all, persisting it in the project is part of that
feature, not a follow-up."*

`WindowState` — the geometry, the dock state, the ribbon's page and whether it
is collapsed — stays on the window, because it **is** the window.

## What AppContext becomes

The flat getters (`linkageTemplate()`, `linkage()`, `simulation()`,
`wheelPlacements()`, `workbookWritable()`) were there because the state was
private to the window. They collapse into one:

```cpp
virtual ProjectSession& session() = 0;
```

`project()`, `markDirty()` and `saveProject()` stay as they are — they are how
features already say "this is the project's" and there is no reason to make nine
files say `session().project()` instead. `window()`, `viewport()`,
`hardpoints()`, `showStatus()`, the two docks, `analysisPanel()` and `ribbon()`
stay: they are what is on screen. `syncTableToViewport(bool)` and
`refreshCommands()` stay, because they are the window's answer to "something
changed".

`windowActions()` goes when `WindowActions` does.

Each of the nine features needs a small edit for this — mostly
`m_context.linkage()` becoming `m_context.session().linkage().parts()` in an
`enabledWhen` predicate.

## What is left on MainWindow

- Build the viewport, the status labels, the two docks and their panels.
- Own the `ProjectSession`, the `CommandRegistry`, the features and the
  `PointEditController`.
- Wire the model, the panel and the session together, and push the session's
  derived state at the viewport and the panel when it says so.
- `AppContext`, nearly all of it inline in the header.
- Its own chrome: the title, the hardpoint status label, `updateChrome()`.
- `openProjectContents()`, `closeEvent()`, and the `WindowState` half of the
  view state.
- The five methods `src/main.cpp` calls — `loadFile`, `loadHardpointFile`,
  `setDisplayMode`, `captureViewport`, `captureWindow` — the first two now thin
  delegations into the documents.

Estimated: **MainWindow.cpp ~400 lines, MainWindow.h ~120.**

Where the 2352 lines go:

| Goes to | Lines today |
|---|---|
| `session/HardpointDocument` | ~430 |
| `session/LinkageDocument` | ~330 |
| `session/WheelsDocument` | ~230 |
| `session/SimulationRunner` | ~110 |
| `session/ChassisDocument` | ~130 |
| `session/ProjectSession` | ~90 |
| `features/*` (the dialogs and their messages) | ~430 |
| `framework/WindowChrome` | ~76 |
| `PointEditController` | ~100 |
| view state onto `Feature` | ~83 |
| dead code, deleted | ~20 |
| **stays on MainWindow** | **~370** |

The two columns do not add to 2352 exactly, because a body that splits between a
document and a feature is counted once in each place it mostly lands.

## Dead code to drop on the way

Found while mapping, all confirmed unreferenced:

- `kUpdateCheckDelayMs` (MainWindow.cpp:87) — `HelpFeature.cpp:23` has the one
  that is used.
- `PresetSpec` and `kPresets` (MainWindow.cpp:89–105) — `ViewFeature.cpp:72–88`
  already has its own copy.
- `m_viewsMenu` (MainWindow.h:305) — never read or written.
- `#include "app/UpdateChecker.h"` and the `class UpdateChecker;` forward
  declaration — update checking moved into `HelpFeature` entirely.
- The `private slots:` on the 24 `WindowActions` overrides is vestigial: only
  `exportSweepCsv` is a real signal target (`AnalysisPanel::exportCsvRequested`).

## Rules the move must not break

Each of these is stated in CLAUDE.md, each is invisible until it is wrong, and
each crosses a seam this refactor introduces. They belong in the review of the
commits that touch them.

- **`setHardpoints()` drops the linkage**, because parts are indices into the
  table. So when the window pushes `derivedChanged()` at the viewport, the
  points go first and the parts after — never the other way round.
- **`captureHardpointConfig()` before `syncTableToViewport()`**, or resolving
  the linkage refills the model's configuration from the project's copy and
  silently undoes a rename or a delete that is not folded in yet.
- **Overwrite Workbook re-reads what it wrote.** Skipping it appends the
  mirrored rows a second time on the next overwrite.
- **A new workbook is written, then read back** as the baseline. The blank
  resource is never read first: `readHardpointsXlsx()` refuses a sheet with no
  points.
- **The viewport reports how far, not where to.** `PointEditController` adds the
  drag distance to the table's own `double`; sending a position back would round
  the two coordinates the drag never touched.
- **The drag plane is the one captured when the arrow was taken hold of**, not
  recomputed per pointer position.
- **`patchLinkageTemplate()` reads through `Project::readFile()`**, which has
  closed the file by the time it returns — Windows will not replace a file this
  process still has open.
- **`markDirty()` stays inert while loading.** `ProjectSession::loading()` is
  what `MainWindow::m_loading` was, and every restore path has to run under it.
- **The save on `aboutToQuit` and in `closeEvent()` both survive**, or a run that
  ends without a window close (`--screenshot`, a session manager) loses the
  project.

## Keeping the features from becoming the new MainWindow

A feature is a class in an anonymous namespace with a file-local
`tr()` over `QCoreApplication::translate`, and nothing outside its own `.cpp`
names it. Two consequences worth stating before 1000 lines of command bodies
move into nine files:

- **The heavy half of each body goes to a document, not to the feature.**
  `overwriteWorkbook()` is 67 lines today; about 40 of them are the workbook
  write and the re-read, which are `HardpointDocument`'s, and about 20 are the
  confirmation and the failure message, which are the feature's. Most bodies
  split that way, because most already delegate the dialog itself to
  `PointDialog`, `MirrorDialog`, `GenerateDialog`, `EditPartsDialog`,
  `WheelDialog`, `SteeringDialog` or `StaticAnglesDialog`.
- **No feature file goes over about 400 lines.** If one would — `HardpointsFeature`
  is the risk, at 209 today plus nine bodies — it splits into two features in
  two files rather than growing. Nothing forbids it: a feature registers itself,
  so `HardpointsWorkbookFeature` and `HardpointsPointsFeature` can sit side by
  side, and CLAUDE.md's "adding a feature touches no file that already exists"
  is exactly what makes that cheap. `RibbonSlot::order` already decides where
  their buttons land, so splitting the file does not move anything on screen.

A feature is not a `QObject`, so where one needs to receive a signal it connects
with a context object — `connect(sender, &T::sig, m_context.window(), lambda)` —
which is also what ties the connection's lifetime to the window.

## Before the first commit

The working tree is not clean, and most of the architecture this plan builds on
is **untracked**: `src/app/framework/`, `src/app/features/`, `MoveGizmo`,
`Expression`, `Simulation`, `CoordinateEntry` and four test files are all new and
uncommitted, and eleven tracked files are modified. `tests/test_viewport_editing.cpp`
is newer than all of it, so that work is still moving.

Land that work first, or at least agree on what is in it. Otherwise "each commit
builds and passes ctest" cannot be checked against anything, and a bisect through
this refactor would land on a tree that never compiled.

## The commit sequence

Each one builds and passes `ctest --preset linux-debug` on its own. None of them
changes behaviour.

1. **Drop the dead constants, the dead member and the dead include.** Small,
   and it stops them being carried into a new file.
2. **`session/ProjectSession`, with the Project, the save timer and the loading
   flag only.** MainWindow's `m_project`, `m_saveTimer` and `m_loading` go;
   `markDirty()` and `saveProject()` delegate. `AppContext` gains `session()`.
3. **`session/ChassisDocument`.** The smallest document, and the one with the
   fewest callers — it proves the shape before the hard ones.
4. **`session/LinkageDocument`.** Template, steering note, resolved parts,
   `patchLinkageTemplate`.
5. **`session/HardpointDocument`.** Baseline, workbook, edits, configuration,
   mirror provenance. The one to review most carefully.
6. **`session/SimulationRunner` and `session/WheelsDocument`**, and
   `ProjectSession::resolveFromTable()` / `resolveSimulation()` written out of
   the cascade the five `rebuild*` methods form today.
7. **`PointEditController`** and **`framework/WindowChrome`**. Independent of
   the session work; either could land earlier if it helps.
8. **View state onto `Feature`.** The two hooks, and the three features that
   use them.
9. **The 24 command bodies into their features**, one feature per commit:
   Project, Geometry, Wheels, Hardpoints, Linkage, Analysis, Panels. Each commit
   deletes its lines from `WindowActions.h`.
10. **Delete `WindowActions.h` and `.cpp`**, the `#include`, and the
    `windowActions()` member of `AppContext`. The migration the header describes
    is over.
11. **CLAUDE.md**: a "The session layer" section under "Layout and layering",
    and the `src/app/` block updated. `docs/RIBBON_PLAN.md` gets a progress-log
    line saying the migration it started finished here.

CMake source lists are explicit and not globbed, so every new file is added to
`qt_add_executable(suspkin ...)` in `CMakeLists.txt` in its own commit.

## Verification

The refactor is guarded by tests that already exist, and by new ones the session
layer makes possible for the first time.

**Existing, must stay green at every commit:**

```bash
cmake --build --preset linux-debug
ctest --preset linux-debug
```

`test_project` covers the manifest round trip including the window and ribbon
state; `test_xlsx_hardpoints` covers the splice-don't-reserialise writer and the
overwrite-twice case CLAUDE.md names; `test_linkage`, `test_simulation`,
`test_kinematics`, `test_hardpoint_generator` and `test_commands` cover
everything the documents wrap.

**New, headless, one per document** — following `test_hardpoint_model`, which
compiles `../src/app/HardpointModel.cpp` straight into the binary:

```cmake
qt_add_executable(test_hardpoint_document
    test_hardpoint_document.cpp
    ../src/app/session/HardpointDocument.cpp
    ../src/app/HardpointModel.cpp)
target_link_libraries(test_hardpoint_document PRIVATE suspkin_core Qt6::Test)
target_include_directories(test_hardpoint_document PRIVATE ../src)
add_test(NAME hardpoint_document COMMAND test_hardpoint_document)
```

Worth writing, because each is a rule CLAUDE.md states and nothing currently
checks at this level:

- Overwrite Workbook re-reads, so overwriting twice does not append the mirrored
  rows a second time.
- A rename is a removal plus an addition, and the configuration and every
  `mirrorOf` follow the point.
- `adoptNewWorkbook()` writes the blank workbook filled and reads it back;
  reading the blank one first is refused.
- `LinkageDocument` gives a template with no `mechanism` block the built-in one
  and sets `mechanismAssumed`.
- `SimulationRunner` skips the sweep when `sweepWanted` is false and still
  poses.

**By hand, because no test opens a window:**

```bash
QT_QPA_PLATFORM=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  ./build/linux-debug/suspkin --screenshot-window /tmp/after.png <project.suspkin>
```

Take the same shot before the first commit and after the last and compare them:
the ribbon, both docks and the viewport should be pixel-identical.

Then open the 26_DY project in the real application and walk the commands that
moved: Import Chassis, Import Hardpoints, Mirror, Overwrite Workbook, Export
Workbook As, Add Point, Rename, Delete, Generate from Design, New Part from
Selection, Edit Parts, Steering Rack, Static Camber and Toe, Add Wheels, Export
Sweep as CSV, Reset Panel Layout. Check that the analysis plots read the same
numbers as before, that closing and reopening the project comes back to the same
camera, tab, docks, selection and sweep, and that the workbook written out is
byte-identical to one written before the refactor.
