# Ribbon toolbar and icons

> **Resumable working plan.** This file is the source of truth for the work in
> progress. A new session should read it top to bottom, check the **Status**
> boxes, and pick up at the first unchecked item. Update the boxes and the
> progress log as things land.

## 1. What is being built, and why

Today every command lives in the menu bar (File, Geometry, Hardpoints, Linkage,
Analysis, View, Help). There is no toolbar. Apart from the viewport's painted
overlays (`ModeSelector`, `NavGizmo`) and the play button in the analysis panel,
there are no icons either. Opening or closing a panel means going through
**View**. The Analysis dock has `Ctrl+K`; the Hardpoints dock has no shortcut
at all.

The request: modern icons, and an Inventor-style toolbar for getting at
commands, and especially **panels**, faster.

Docks already float out onto other monitors, and `saveState()` already puts
them back when the project reopens. That is **not** part of this plan.

What gets built:

1. **An icon for every command**, from one consistent set, following the
   application between light and dark.
2. **A ribbon**: a File button, tabs that match the existing menus, and groups
   of large and small buttons under each tab, the way Inventor lays it out.
3. **Panel toggles that are always on screen**, whichever tab is showing. This
   is the part that answers "open and close the panels I need more quickly".
4. **The ribbon's own state in the project**: which tab, whether it is
   collapsed, whether the menu bar is shown. Per CLAUDE.md, that is part of the
   feature, not a follow-up.

```
+-------------------------------------------------------------------------------------+
| File  Geometry  Hardpoints  Linkage  Analysis  View  Help                           |
+-------------------------------------------------------------------------------------+
| [File v]  Geometry | Hardpoints | Linkage | Analysis | View    [#] [~] [=] | [?v] [^] |
+-------------------------------------------------------------------------------------+
|  +--------+  [] Overwrite     |  +--------+   |  [] Labels                           |
|  |  icon  |  [] Export As     |  |  icon  |   |                                      |
|  | Import |  [] Remove        |  | Mirror |   |                                      |
|  +--------+                   |  +--------+   |                                      |
|          Workbook             |     Edit      |      Show                            |
+-------------------------------------------------------------------------------------+
   [#] Hardpoints panel   [~] Analysis panel   [=] Sweep parameters   [^] collapse
```

## 2. Architecture decisions (settled -- do not re-litigate)

1. **Every button is an existing `QAction`.** A ribbon button is a
   `QToolButton` with `setDefaultAction()` set to the same `QAction` the menu
   holds. Enabled state (`updateActionState()`), checked state, shortcut and
   status tip therefore cannot disagree between the menu and the ribbon. It is
   the same rule as "the overlay buttons and the menu entries are two views of
   one mode". No ribbon button gets a slot of its own.

2. **A ribbon written here, not a library.** About thirty commands in five
   tabs. SARibbon and QtitanRibbon bring their own theming and frameless-window
   handling, plus a new dependency on both packaging paths (the Arch PKGBUILD
   and the Windows installer), for features this app does not need:
   customisation, KeyTips, contextual tabs. A `QTabBar`, a `QStackedWidget` and
   a group widget come to a few hundred lines, and they take their colours from
   the palette like the rest of the app.

3. **Icons come from Tabler Icons (MIT), pinned at `v3.46.0` and committed as
   SVG.** Every icon sits on the same 24 px grid with a 2 px stroke, which reads
   well at both 16 and 32 px. The set has icons for this app's own domain:
   `steering-wheel`, `wheel`, `car-suspension`, `file-spreadsheet`,
   `table-export`, `flip-horizontal`. The SVGs are committed under
   `resources/icons/`, not fetched at build time: the Arch build runs in a
   chroot with no network. The STEP fixtures set the same precedent, since a
   file the build cannot make is committed.

4. **Icons are recoloured from the palette by a custom `QIconEngine`.** Tabler
   draws with `stroke="currentColor"`, and QtSvg resolves `currentColor` to
   black when nothing sets `color`. A plain `QIcon(":/icons/x.svg")` is
   therefore black on a dark desktop. `ThemedIconEngine` writes the palette's
   colour into the SVG bytes before rendering, per mode and state, and caches
   by `QPalette::cacheKey()`, so switching between light and dark simply misses
   the cache. `HardpointDelegates` already works this way: colours come from
   the palette, and there is no theme of its own.

5. **`Qt6::Svg` becomes a dependency of the app only.** `suspkin_core` stays
   Core + Gui. The app renders through `QSvgRenderer` directly, so nothing
   depends on the `qsvgicon` plugin being deployed. `windeployqt` picks up
   `Qt6Svg.dll` because the exe links it. On Arch, `qt6-svg` joins `depends` in
   the PKGBUILD and the CI container's `pacman` line. It is already installed
   on this machine (6.11.1).

6. **The menu bar stays, above the ribbon, and is shown by default.** The
   window's menu widget is a container holding the `QMenuBar` above the ribbon
   (`setMenuWidget(container)`). The File button and the Help button open the
   **same `QMenu` objects** the menu bar holds, so Open Recent is still
   refreshed by its single `aboutToShow` connection. **View > Show Menu Bar**
   hides the bar for anyone who wants the ribbon on its own, and the project
   remembers the choice.
   - **Pitfall:** once `setMenuWidget()` has been called, **never call
     `QMainWindow::menuBar()` again**. When the menu widget is not a `QMenuBar`,
     `menuBar()` creates one and installs it through `setMenuWidget()`, which
     `deleteLater()`s whatever was there, including the ribbon. Keep the bar
     in `m_menuBar` and use that.

7. **Every command action is added to the window itself (`addAction()`).** A
   `QAction`'s shortcut only fires while at least one widget it is on is
   visible. Once the user hides the menu bar, and the button is on a ribbon tab
   that is not showing, `Ctrl+I` would go dead. The code already does this for the view
   presets and the three show toggles; now it applies to every command.

8. **Panel toggles are not `toggleViewAction()`.** When one dock is tabified
   behind another, its `toggleViewAction()` is still checked, so clicking it
   *closes* the panel the user was trying to reach. `PanelAction` behaves like
   this instead:
   - closed: show it and `raise()` it;
   - open but not on screen (tabbed behind another dock): `raise()` it;
   - open and on screen: close it.

   The action's checked state follows the dock's own
   `toggleViewAction()->toggled`. It is re-synced *after* acting, because a
   checkable `QAction` has already flipped itself by the time `triggered`
   fires. The Sweep Parameters window gets the same treatment through a
   visibility signal on `AnalysisPanel`.

9. **The ribbon's state is project state.** It lives in `WindowState`, beside
   the window geometry and dock state, because it is window layout. The active
   tab is stored **by key, not by index**, so a tab added in a later release
   does not change which tab an older project opens on. That is the same
   reasoning as hardpoint config being keyed by name. It is restored under
   `m_loading`, and any change calls `markDirty()`.

## 3. Where the code goes

```
src/app/Icons.h/.cpp          enum class Icon, one table Icon -> resource path,
                              ThemedIconEngine, Icons::get(Icon) -> QIcon
src/app/Ribbon.h/.cpp         Ribbon (tab row + pages), RibbonPage, RibbonGroup
src/app/PanelAction.h/.cpp    the raise-or-toggle action for a dock or a window
resources/icons/*.svg         Tabler outline SVGs, their header comment stripped
resources/icons/LICENSE-tabler.txt   the MIT licence, with the tag they came from
tools/fetch_icons.py          stdlib only; fetches the names it is given at the
                              pinned tag into resources/icons/
tests/test_icons.cpp          every Icon resolves; recolouring follows the palette
tests/test_panel_action.cpp   the raise-or-toggle rule against real docks
```

An `enum class Icon` rather than string names: a typo in `MainWindow` becomes a
compile error instead of a blank button, and the test walks the one table to
prove every entry is embedded and valid. The CMake list of files and that table
are the only two lists, and the test ties them together.

## 4. The command map

Size **L** is a 32 px icon with its label underneath. **S** is a 16 px icon with
its label beside it, stacked three to a column. Shortcuts are the existing ones
unless marked *new*.

**File button** (the File `QMenu`, contents unchanged): New Project
`folder-plus`, Open Project `folder-open`, Open Recent `history`, Save Project
`device-floppy`, Project List `list-details`, Show Project Folder
`folder-search`, Quit `logout`.

**Geometry**

| Group  | Command         | Action                 | Icon          | Size | Key    |
|--------|-----------------|------------------------|---------------|------|--------|
| Model  | Import Chassis  | `m_importAction`       | `file-import` | L    | Ctrl+O |
| Model  | Remove Chassis  | `m_closeAction`        | `file-x`      | S    | Ctrl+W |
| Wheels | Add Wheels      | `m_addWheelsAction`    | `wheel`       | L    |        |
| Wheels | Remove Wheels   | `m_removeWheelsAction` | `trash`       | S    |        |

**Hardpoints**

| Group    | Command            | Action                      | Icon               | Size | Key          |
|----------|--------------------|-----------------------------|--------------------|------|--------------|
| Workbook | Import Hardpoints  | `m_importHardpointsAction`  | `file-spreadsheet` | L    | Ctrl+I       |
| Workbook | Overwrite Workbook | `m_overwriteWorkbookAction` | `table-export`     | S    | Ctrl+Shift+S |
| Workbook | Export Workbook As | `m_exportWorkbookAction`    | `file-export`      | S    | Ctrl+E       |
| Workbook | Remove Hardpoints  | `m_closeHardpointsAction`   | `table-minus`      | S    |              |
| Edit     | Mirror Hardpoints  | `m_mirrorAction`            | `flip-horizontal`  | L    | Ctrl+M       |
| Show     | Show Labels        | `m_labelsAction`            | `tag`              | S    | Ctrl+L       |

`flip-horizontal` has the vertical mirror line, which is how mirroring across
the car's centre plane (negating Y) looks in a top view.

**Linkage** (the menu called Parts until 2026-09-11)

| Group    | Command                    | Action                          | Icon             | Size | Key    |
|----------|----------------------------|---------------------------------|------------------|------|--------|
| Show     | Show Parts                 | `m_linksAction`                 | `vector`         | L    | Ctrl+P |
| Template | Import Template            | `m_importLinkageAction`         | `template`       | S    |        |
| Template | Reset to Built-in Template | `m_resetLinkageAction`          | `restore`        | S    |        |
| Template | Show Template File         | *new member* `m_revealTemplateAction` | `braces`   | S    |        |
| Steering | Steering Rack              | `m_steeringAction`              | `steering-wheel` | L    |        |

**Analysis**

| Group | Command             | Action                                | Icon                     | Size | Key          |
|-------|---------------------|---------------------------------------|--------------------------|------|--------------|
| Panel | Analysis            | *new* `m_analysisPanelAction`         | `chart-line`             | L    | Ctrl+K       |
| Sweep | Sweep Parameters    | *new* `m_parametersPanelAction`       | `adjustments-horizontal` | L    | Ctrl+Shift+P |
| Sweep | Export Sweep as CSV | *new member* `m_exportSweepAction`    | `file-type-csv`          | S    |              |

**View**

| Group    | Command                     | Action                                           | Icon               | Size | Key                    |
|----------|-----------------------------|--------------------------------------------------|--------------------|------|------------------------|
| Navigate | Fit to View                 | `m_fitAction`                                    | `focus-centered`   | L    | F                      |
| Navigate | Views (split button)        | *new members* `m_presetActions`, default Isometric | `cube`           | L    | 1-7                    |
| Display  | Solid                       | `m_solidAction`                                  | `sphere`           | S    | Ctrl+1                 |
| Display  | Triangles                   | `m_trianglesAction`                              | `triangles`        | S    | Ctrl+2                 |
| Show     | Labels / Parts / Wheels     | `m_labelsAction` / `m_linksAction` / `m_wheelsAction` | `tag` / `vector` / `wheel` | S | Ctrl+L / Ctrl+P / Ctrl+Shift+W |
| Window   | Show Menu Bar               | *new* `m_menuBarAction`                          | `menu-2`           | S    |                        |
| Window   | Reset Panel Layout          | *new* `m_resetLayoutAction`                      | `layout-dashboard` | S    |                        |

`sphere` and `triangles` match what the viewport's own mode selector already
draws: a shaded ball and a meshed one.

**Tab row, right-hand end, on every tab** (icon only, checked = open):

| Command          | Action                              | Icon                     | Key              |
|------------------|-------------------------------------|--------------------------|------------------|
| Hardpoints panel | *new* `m_hardpointsPanelAction`     | `table`                  | Ctrl+H           |
| Analysis panel   | `m_analysisPanelAction`             | `chart-line`             | Ctrl+K           |
| Sweep Parameters | `m_parametersPanelAction`           | `adjustments-horizontal` | Ctrl+Shift+P     |
| Help             | the Help `QMenu`                    | `help-circle`            |                  |
| Collapse ribbon  | *new* `m_collapseRibbonAction`      | `chevron-up` / `chevron-down` | Ctrl+F1 *new* |

In the Help menu: Check for Updates `cloud-download`, About `info-circle`.
Check on Startup is a checkbox and gets no icon.

All of these names were confirmed to exist at the pinned tag.

## 5. Status

### Phase A -- Actions become the single source (no visible change)

- [ ] **A1** Move the actions `buildMenus()` creates inline into members built
  in `buildActions()`: Show Project Folder, Show Template File, Sweep
  Parameters, Export Sweep as CSV, the seven view presets, About.
- [ ] **A2** Call `addAction()` on the window for every command action
  (decision 7), not only the four that do it today.
- [ ] **A3** Give the ribbon shorter labels through `setIconText()`, which
  leaves the menu text alone: "Overwrite", "Export As", "Reset Template",
  "Import\nGeometry". A `QToolButton` sizes and draws multi-line text, so an
  explicit `\n` is how a large button gets its two lines; the menu never sees
  it.
- [ ] **A4** A helper that sets each action's tooltip to "*Label* (*shortcut
  in native text*)" plus its status tip. A tool button shows the tooltip, and
  Qt does not add the shortcut to it by itself.
- [ ] Verify: build, `ctest`, every menu and shortcut behaves exactly as
  before.

### Phase B -- Icons

- [ ] **B1** `tools/fetch_icons.py <name>...`: stdlib `urllib`, fetches
  `icons/outline/<name>.svg` at tag `v3.46.0`, strips the leading `<!-- -->`
  header, and writes `resources/icons/<name>.svg`. It writes
  `LICENSE-tabler.txt` once, naming the tag. Run it once with every name in
  section 4 and commit the output.
- [ ] **B2** CMake: add `Svg` to `find_package(Qt6 ...)` and link `Qt6::Svg`
  to `suspkin` only. Add `set(SUSPKIN_ICONS ...)` and
  `qt_add_resources(suspkin "icons" PREFIX "/icons" BASE "resources/icons"
  FILES ${SUSPKIN_ICONS})`. Add `qt6-svg` to the PKGBUILD `depends` and to the
  Arch job's `pacman` line in `release.yml`. For Windows, check that the first
  CI configure finds Qt6Svg in the `install-qt-action` kit. It is expected to
  be in the default install; if not, `find_package` fails loudly and the fix is
  a `modules:` line.
- [ ] **B3** `ThemedIconEngine` in `src/app/Icons.cpp`:
  - It holds the SVG bytes. `paint()` picks a colour from
    `QGuiApplication::palette()` for the mode: Normal and Active use
    `ButtonText`, Disabled uses the disabled `ButtonText`, Selected uses
    `HighlightedText`. It then replaces `currentColor` with that colour's hex
    and renders through `QSvgRenderer`.
  - At 28 px and up it also swaps `stroke-width="2"` for `1.5`. Otherwise the
    32 px icons come out visibly heavier than the 16 px ones next to them.
  - `pixmap()` renders into a `QImage` and caches by
    `(icon, pixel size, mode, state, palette.cacheKey())`. Implement `clone()`
    and `key()`.
  - `Icons::get(Icon)` builds the `QIcon`. An unknown entry logs a `qWarning`
    and returns an empty icon; it never crashes.
- [ ] **B4** Set an icon on every action as in section 4. Menus show them
  automatically.
- [ ] **B5** `tests/test_icons.cpp` (`QTEST_MAIN`):
  - every `Icon` resolves to an embedded resource that `QSvgRenderer` accepts;
  - rendered under a light palette, the opaque pixels are dark, and under a
    dark palette they are light;
  - Disabled differs from Normal;
  - changing the palette changes the pixmap, i.e. the cache is not stale.

  It compiles `../src/app/Icons.cpp` directly, the way `test_hardpoint_model`
  compiles `HardpointModel.cpp`, and embeds the same `${SUSPKIN_ICONS}`. Set
  `ENVIRONMENT QT_QPA_PLATFORM=offscreen` on it so it never opens a window on a
  developer's desktop.
- [ ] Verify at 100 %, 150 % and 200 % scaling (`QT_SCALE_FACTOR`) that menu
  icons are crisp. The pixmap path must be asked for device pixels rather than
  upscaling a 1x image.

### Phase C -- The ribbon widget

- [ ] **C1** `Ribbon`: a top row of [File button | `QTabBar` | stretch |
  trailing buttons], with a `QStackedWidget` of pages below it. API:
  ```cpp
  void setApplicationMenu(QMenu* menu, const QString& text);  // File button
  RibbonPage* addPage(const QString& key, const QString& title);
  void addTrailingAction(QAction* action);                    // icon only
  void addTrailingMenu(QMenu* menu, const QIcon& icon);       // Help
  QString currentPage() const;   void setCurrentPage(const QString& key);
  bool collapsed() const;        void setCollapsed(bool collapsed);
  // signals: currentPageChanged(const QString& key), collapsedChanged(bool)

  RibbonGroup* RibbonPage::addGroup(const QString& caption);
  void RibbonGroup::addLarge(QAction* action);
  void RibbonGroup::addSmall(QAction* action);   // stacked three to a column
  void RibbonGroup::addSplit(QAction* defaultAction, QMenu* menu);
  ```
- [ ] **C2** Buttons:
  - Large: `ToolButtonTextUnderIcon`, 32 px icon, `autoRaise`, at least 56 px
    wide.
  - Small: `ToolButtonTextBesideIcon`, 16 px icon, one third of the large
    button's height. Add them to their column with `Qt::AlignLeft` instead of
    stretching them: that way a `QToolButton`'s centred contents still read as
    a left-aligned list.
  - Split: `MenuButtonPopup`.
  - Each group has a caption underneath in a smaller font, using the
    `PlaceholderText` colour, and a thin `Mid`-coloured separator between
    groups.
- [ ] **C3** Colours come only from the palette, re-read on
  `QEvent::PaletteChange` the way `HardpointPanel::changeEvent()` does it. The
  tab row uses `Window`, and the pages a shade between `Window` and `Base`. A
  checked button gets a `Highlight` fill at low alpha. The File button is the
  one accent: `Highlight` behind `HighlightedText`.
- [ ] **C4** Collapse: double-clicking a tab, the chevron, or `Ctrl+F1` hides
  the page stack. Clicking a tab while collapsed expands the ribbon again. v1
  has no Office-style popup-over-the-viewport.
- [ ] **C5** Narrow windows: each page sits in a frameless `QScrollArea` that
  scrolls horizontally, with its scrollbar shown only when needed. A 1024 px
  window then scrolls instead of clipping. Groups do not collapse into
  dropdowns at small widths; this app is not used at widths where that would
  matter.

### Phase D -- Wiring it into `MainWindow`

- [ ] **D1** `buildMenus()` creates `m_menuBar` explicitly and never calls
  `menuBar()` (decision 6). Keep `m_fileMenu` and `m_helpMenu` as members. The
  new `buildRibbon()` runs after it, builds the container (`m_menuBar` above
  `m_ribbon`) and calls `setMenuWidget(container)`.
- [ ] **D2** Build the pages and groups in section 4.
- [ ] **D3** `PanelAction` for the Hardpoints dock, the Analysis dock and the
  Sweep Parameters window. It replaces `toggleViewAction()` in the View and
  Analysis menus as well as appearing in the ribbon.
  - **Take `Ctrl+K` off `m_analysisDock->toggleViewAction()`, and `Ctrl+H` off
    `m_hardpointDock->toggleViewAction()`.** If two actions
    share a shortcut, Qt reports an ambiguous overload and fires neither.
  - The existing inline Sweep Parameters action and its `Ctrl+Shift+P` become
    `m_parametersPanelAction`.
  - `AnalysisPanel` gains `parametersVisibilityChanged(bool)`, emitted from
    `showParameters()`, `setParametersVisible()` and `closedByUser`, so that
    toggle's checked state has something to follow.
- [ ] **D4** Reset Panel Layout: capture `m_defaultDockState = saveState()` at
  the end of construction, before `openProjectContents()` restores the
  project's own layout. Reset does `restoreState(m_defaultDockState)`, then
  applies the rule `openProjectContents()` already uses (the Hardpoints dock is
  shown when hardpoints are loaded), then calls `markDirty()`. This is what
  rescues a panel that was floated onto a monitor that is not there today.
- [ ] **D5** Show Menu Bar: a checkable action, checked by default, that sets
  `m_menuBar`'s visibility.
- [ ] **D6** `tests/test_panel_action.cpp` (`QTEST_MAIN`, offscreen): a
  `QMainWindow` with two tabified docks. Triggering the one behind brings it
  forward and leaves it open. Triggering again closes it. Triggering once more
  shows it. The checked state matches after every step.

### Phase E -- Persistence

- [ ] **E1** Add `QString ribbonPage`, `bool ribbonCollapsed = false` and
  `bool menuBarVisible = true` to `WindowState` in `Project.h`, and include
  them in `isEmpty()`. Otherwise a state holding only these would not be
  written. `Project.cpp` reads and writes `ribbonPage`, `ribbonCollapsed` and
  `menuBar` under `"window"`. A missing key reads as the default, so an older
  project opens on the first tab, expanded, with its menu bar where it always
  was.
- [ ] **E2** `collectViewState()` writes the three fields.
  `openProjectContents()` restores them under `m_loading`, straight after
  `restoreState()`. A key that names no page, such as a tab renamed in a later
  release, falls back to the first page without a message.
- [ ] **E3** Connect `currentPageChanged`, `collapsedChanged` and the menu-bar
  toggle to `markDirty()`.
- [ ] **E4** `tests/test_project.cpp`: extend the window round-trip case with
  the three fields, and add a case showing that a manifest without them reads
  as the defaults.

### Phase F -- Checking it, and saying so

- [ ] **F1** Add `--screenshot-window <png>` to `main.cpp`. It grabs the whole
  window rather than only the viewport, so the ribbon can be checked headlessly
  the same way the renderer is. Same `offscreen` + `LIBGL_ALWAYS_SOFTWARE`
  recipe as in CLAUDE.md.
- [ ] **F2** By hand, on Linux light and dark, and on Windows 11 at 100 / 150 /
  200 %:
  - [ ] every tab, every button;
  - [ ] each button's enabled state matches its menu entry, with and without
    hardpoints loaded;
  - [ ] `Ctrl+I` works with the menu bar hidden and the View tab showing;
  - [ ] a tabified Analysis dock comes to the front when its toggle is
    clicked;
  - [ ] a panel floated to the second monitor comes back there when the
    project reopens;
  - [ ] Reset Panel Layout docks everything again.
- [ ] **F3** CLAUDE.md gets a short "Commands, the ribbon and icons" section
  covering: actions as the single source; `addAction()` on the window; never
  `menuBar()` after `setMenuWidget()`; icons only through `Icons::get(Icon::...)`,
  with new ones fetched by `tools/fetch_icons.py`; ribbon state in
  `WindowState`. Add `src/app/Icons`, `Ribbon` and `PanelAction` to the layout
  block. README's shortcut table gains `Ctrl+H` and `Ctrl+F1`.

## 6. Settled with the user

- **The menu bar is shown by default**, above the ribbon, and can be hidden
  from View > Show Menu Bar.
- **Single-colour line icons that recolour with the theme.** Inventor's own
  icons are multi-coloured; matching that would mean drawing a set by hand,
  which is a much bigger job.
- **Height:** about 24 px of menu bar, 28 px of tabs and 90 px of groups.
  Collapsing the ribbon (`Ctrl+F1`) takes it down to the tab row; hiding the
  menu bar saves the rest.

## 7. Not in scope

- Floating and popping panels out: it already works.
- Ribbon customisation, a Quick Access Toolbar, KeyTips, and contextual tabs
  (e.g. a Simulation tab that appears while simulating). The `Ribbon` API
  leaves room for them.
- Moving Play and Simulate out of the analysis panel into the ribbon. They are
  widgets in the panel, not actions; they would need to become actions first.

## 8. Progress log

- **2026-09-10** Plan written and reviewed. The menu bar stays shown by
  default; everything else was accepted as drafted. No code yet.
- **2026-09-11** Still no ribbon code, but user feedback renamed things this
  plan maps, and section 4 now uses the new names: **Parts** is the
  **Linkage** menu (it holds the template and the steering rack, and people
  read "parts" as the CAD bodies under Geometry), **Import/Remove Geometry**
  is **Import/Remove Chassis**, and **Steering** is **Steering Rack**. The
  Hardpoints dock's own `toggleViewAction()` is now **Show Hardpoint Table**,
  first in the Hardpoints menu, with `Ctrl+H`. A table closed with its X could
  not be found again under View's bare "Hardpoints". D3 has to move that
  shortcut to the `PanelAction`, the same as `Ctrl+K`.
