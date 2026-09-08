#pragma once

#include "model/Hardpoint.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <vector>

namespace suspkin {

/// Where one imported point's three value cells sit, and what they held.
struct XlsxCellRow {
    QString name;                ///< the hardpoint these three cells describe
    std::array<QString, 3> ref;  ///< the value cell per axis, e.g. {"B2","B3","B4"}
    std::array<QString, 3> text; ///< each value's text exactly as it stood in the file
};

/// Everything needed to write the points back into the workbook they came from:
/// the original file byte for byte, and which cell each coordinate was read out
/// of. Saving rewrites exactly those cells and copies the rest through, so
/// column widths, number formats, other sheets and anything else the author put
/// in the workbook survive a round trip untouched.
struct XlsxHardpointSource {
    QByteArray workbook;            ///< the .xlsx exactly as it was imported
    QString sheetPart;              ///< archive member, e.g. "xl/worksheets/sheet1.xml"
    QString sheetName;              ///< the sheet's name as shown in Excel
    QString workbookPart;           ///< usually "xl/workbook.xml"
    std::vector<XlsxCellRow> rows;  ///< in import order, one entry per imported point

    /// Where the table lives, so points that were not in the file -- mirrored
    /// ones -- can be appended underneath it in the same two columns.
    int nameColumn = 0;  ///< 1-based, 0 when unknown
    int valueColumn = 0; ///< 1-based, 0 when unknown
    int lastRow = 0;     ///< the last row the sheet uses at all

    bool isValid() const { return !workbook.isEmpty() && !sheetPart.isEmpty(); }
    /// True when a point that is not in the workbook can be written into it.
    bool canAppend() const { return nameColumn > 0 && valueColumn > 0 && lastRow > 0; }

    const XlsxCellRow* find(const QString& name) const;
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
///
/// Points that were not in the workbook -- the ones mirroring produced -- are
/// appended as three new `name`/`value` rows below the existing table, in the
/// same two columns it used. Everything else in the file is copied through byte
/// for byte.
QString writeHardpointsXlsx(const QString& path, const HardpointTable& table,
                            const XlsxHardpointSource& source);

/// Name filter for the hardpoint file dialogs.
QString hardpointFileFilter();

} // namespace suspkin
