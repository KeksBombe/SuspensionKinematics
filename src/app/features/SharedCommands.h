#pragma once

#include <QString>

namespace suspkin {

class AppContext;
class Project;
struct HardpointTable;

/// What more than one feature -- or the command line -- reaches in another
/// feature. Each is defined in the feature it belongs to.

/// Import the chassis at @p path into the project and draw it. What --import
/// does with anything that is not a workbook. GeometryFeature.
void importChassisFile(AppContext& context, const QString& path);

/// Import the workbook at @p path into the project as its hardpoints. What
/// --import does with an .xlsx. HardpointWorkbookFeature.
void importHardpointFile(AppContext& context, const QString& path);

/// Give a project with no workbook one of its own, filled with @p table, read
/// it back as the baseline and show it -- so from here on it is an ordinary
/// project with an ordinary workbook, not a special case. What adding the first
/// point and generating a corner into an empty project both come to.
/// HardpointWorkbookFeature.
bool adoptNewWorkbook(AppContext& context, const HardpointTable& table);

/// Where a file dialog for geometry starts: the last place geometry came from,
/// or the documents folder. GeometryFeature.
QString geometryDialogDirectory(const Project& project);

/// Where a file dialog for a workbook starts: the last place one came from,
/// then where geometry came from, then the documents folder.
/// HardpointWorkbookFeature.
QString hardpointDialogDirectory(const Project& project);

} // namespace suspkin
