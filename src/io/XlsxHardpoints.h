#pragma once

#include "model/Hardpoint.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <vector>

namespace suspkin {

/// Everything needed to write the points back into the workbook they came from:
/// the original file byte for byte, and which cell each coordinate was read out
/// of. Saving rewrites exactly those cells and copies the rest through, so
/// column widths, number formats, other sheets and anything else the author put
/// in the workbook survive a round trip untouched.
struct XlsxHardpointSource {
    QByteArray workbook;                       ///< the .xlsx exactly as it was imported
    QString sheetPart;                         ///< archive member, e.g. "xl/worksheets/sheet1.xml"
    QString sheetName;                         ///< the sheet's name as shown in Excel
    QString workbookPart;                      ///< usually "xl/workbook.xml"
    std::vector<std::array<QString, 3>> cells; ///< parallel to the table: {"B2","B3","B4"}
    std::vector<std::array<QString, 3>> text;  ///< each value's text as it stood in the file

    bool isValid() const { return !workbook.isEmpty() && !sheetPart.isEmpty(); }
};

/// Outcome of reading hardpoints from a workbook. Like MeshLoadResult, failures
/// come back as a message rather than an exception, so the caller can put it
/// straight in front of the user.
struct HardpointLoadResult {
    std::optional<HardpointTable> table;
    XlsxHardpointSource source;
    QStringList warnings;   ///< rows that were skipped, and why
    QString error;          ///< empty on success
    qint64 elapsedMs = 0;

    bool ok() const { return table.has_value(); }
};

/// Read hardpoints from an .xlsx workbook laid out as a Name/Value table, where
/// each row holds one coordinate and the name carries an `_x`, `_y` or `_z`
/// suffix -- so `F_LCA_O_x`, `F_LCA_O_y` and `F_LCA_O_z` become the point
/// `F_LCA_O`.
HardpointLoadResult readHardpointsXlsx(const QString& path);

/// Write @p table to @p path, using @p source as the template. Returns an empty
/// string on success, otherwise the reason it failed.
///
/// Coordinates the user did not edit are written back with the exact text the
/// workbook already held, so saving an unmodified import leaves the cells alone.
QString writeHardpointsXlsx(const QString& path, const HardpointTable& table,
                            const XlsxHardpointSource& source);

/// Name filter for the hardpoint file dialogs.
QString hardpointFileFilter();

} // namespace suspkin
