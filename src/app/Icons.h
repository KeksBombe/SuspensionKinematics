#pragma once

#include <QIcon>
#include <QList>
#include <QString>

namespace suspkin {

/// Every icon the application draws, by what it looks like rather than by the
/// command it is on: one drawing can serve two commands (the wheel is Add
/// Wheels and Show Wheels), and a command can change its icon without an enum
/// changing its meaning.
///
/// An enum rather than a string, so a misspelt icon is a compile error rather
/// than a blank button. Each entry has one line in the table in Icons.cpp and
/// one file in SUSPKIN_ICONS in CMakeLists.txt; test_icons checks the two agree.
enum class Icon {
    AdjustmentsHorizontal,
    Angle,
    ArrowBackUp,
    ArrowForwardUp,
    Braces,
    ChartLine,
    ChevronDown,
    ChevronUp,
    CloudDownload,
    Cube,
    DeviceFloppy,
    Edit,
    FileExport,
    FileImport,
    FileSpreadsheet,
    FileTypeCsv,
    FileX,
    FlipHorizontal,
    FocusCentered,
    FolderOpen,
    FolderPlus,
    FolderSearch,
    Forms,
    History,
    InfoCircle,
    InfoSquareRounded,
    LayoutDashboard,
    License,
    Line,
    ListDetails,
    Logout,
    Refresh,
    Restore,
    RowInsertBottom,
    RowRemove,
    Sphere,
    SteeringWheel,
    Table,
    TableExport,
    TableMinus,
    TablePlus,
    Tag,
    Template,
    Trash,
    Triangles,
    Vector,
    Wand,
    Wheel,

    Count ///< not an icon: how many there are, so a test can walk them all
};

namespace Icons {

/// The embedded SVG @p icon is drawn from, e.g. ":/icons/wheel.svg". Empty for
/// a value the table does not have.
QString resourcePath(Icon icon);

/// Every icon, in enum order.
QList<Icon> all();

/// @p icon, drawn in the colours of the application's palette at the moment it
/// is painted: black on a light desktop, white on a dark one, greyed out when
/// disabled. Switching theme needs nothing from the caller -- the next paint
/// simply draws in the new colours.
///
/// Anything not in the table logs a warning and comes back as a null icon,
/// which a button draws as no icon at all.
QIcon get(Icon icon);

} // namespace Icons
} // namespace suspkin
