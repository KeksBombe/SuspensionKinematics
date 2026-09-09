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
    <name>.xlsx        the imported workbook, copied in, kept as imported
    edits.json         hardpoint changes not yet written into that workbook
  linkage/
    template.json      which parts join which hardpoints
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
- `diffHardpoints(baseline, current)` produces `HardpointEdits` (`changed` +
  `added`), which is what `hardpoints/edits.json` stores.
  `applyHardpointEdits()` is its inverse and is how a project reopens where it was.

The workbook copy is **not** touched by editing. It changes only when the user asks:

- **Overwrite Workbook** rewrites the project's copy, then *re-reads it* so the
  baseline and the cell map are current. Skipping that re-read would append the
  mirrored rows a second time on the next overwrite — see the test named for it.
- **Export Workbook As** writes elsewhere (defaulting to `importedFrom`) and leaves
  the edits pending.

`writeHardpointsXlsx()` splices bytes rather than reserialising XML: cells that
exist are patched in place, points that do not exist in the workbook (what
mirroring produces) are appended as new rows before `</sheetData>`, and every
other ZIP member is copied through with its original compressed data. That is what
keeps number formats, other sheets, charts and vendor parts intact. Untouched
coordinates are written back with the exact text the file already held.

Mirroring itself is pure and testable: `src/model/HardpointMirror.*`. A `MirrorSpec`
(axis, naming rule, affix or find/replace) is stored in the project because it is
the user's own naming convention.

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
- A project without a template gets the built-in one written in on open
  (`installBuiltinLinkageTemplate()`), so it becomes an ordinary project file the
  user can edit. A template that fails to parse is *not* repaired by overwriting.

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
src/geom/     Aabb, TriMesh, vertex welding and edge extraction
src/model/    Hardpoint, HardpointTable, hardpoint mirroring, and the linkage
              a template resolves to
src/io/       STL and STEP readers behind importMeshFile(), a minimal ZIP
              reader/rewriter, the hardpoint workbook reader/writer, and the
              linkage template reader/writer
src/project/  the project format: manifest, assets, view state, hardpoint edits
src/update/   what a release says about itself: parsing and comparing the
              version.json a release publishes. Pure, so it is tested without
              the network
src/render/   Camera, GPU buffers, the OpenGL viewport, gizmo, mode selector
src/app/      MainWindow, AppController, the project launcher, the mirror dialog,
              the hardpoint table model and dock
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
- Warnings are on but `-Werror` is not, on purpose.
- `CMakeLists.txt` uses the range form `3.24...3.28`: this machine has both CMake
  3.28.1 (from STM32CubeCLT, first on `PATH`) and 4.4.1.
