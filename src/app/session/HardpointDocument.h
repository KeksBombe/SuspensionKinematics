#pragma once

#include "app/HardpointModel.h"
#include "app/session/SessionMessage.h"
#include "io/XlsxHardpoints.h"
#include "project/Project.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace suspkin {

class ProjectSession;
struct LinkageTemplate;

/// A workbook read into the project: its points, and what the reader had to
/// say about the rows it skipped.
struct HardpointImport {
    QStringList warnings;
    qint64 elapsedMs = 0;
};

/// The points, and the three-way split they live in: the workbook, the baseline
/// read out of it, and the user's edits between that and the table.
///
/// The model is here rather than with the dock that shows it: it is a
/// QAbstractTableModel over core types with no widget in it, and it is the live
/// table everything else is resolved against.
///
/// The rules this class exists to keep in one place:
/// - Overwriting the workbook re-reads it, or the next overwrite appends the
///   mirrored rows a second time.
/// - A new workbook is written from the blank one and read back; the blank one
///   is never read first.
/// - The configuration is captured into the project before anything is
///   resolved again, never after.
class HardpointDocument {
public:
    explicit HardpointDocument(ProjectSession& session);

    HardpointModel& model() { return m_model; }
    const HardpointModel& model() const { return m_model; }

    /// The table exactly as the workbook holds it.
    const HardpointTable& baseline() const { return m_baseline; }
    /// The project's workbook was read whole, so the table can be written back
    /// through it.
    bool workbookWritable() const { return m_source.isValid(); }
    /// Where the project's own copy of the workbook is. Empty without one.
    QString workbookPath() const;
    /// The edits the project is holding that are not in the workbook yet.
    HardpointEdits pendingEdits() const;
    /// Write those edits beside the workbook. Nothing to write, and true,
    /// without a workbook.
    bool writePendingEdits(QString* error) const;

    /// Read the project's workbook and its edits file. The table to show --
    /// the baseline with the edits applied -- or nothing when there is none or
    /// it cannot be read. @p problem can be said even when a table comes back:
    /// a workbook whose edits cannot be read still opens, as it was imported.
    std::optional<HardpointTable> open(SessionMessage* problem);
    /// Read @p path and copy it into the project as its workbook, with nothing
    /// pending against it. The model is left for the caller to fill with
    /// baseline(). @p problem can be said on success too: an old edits file
    /// that could not be cleared.
    std::optional<HardpointImport> import(const QString& path, SessionMessage* problem);
    /// Write the table into @p path through the workbook the points came from.
    bool writeTo(const QString& path, SessionMessage* problem) const;
    /// Write the table into the project's own copy of the workbook, then read
    /// that back as the baseline. False, with @p problem said, when either
    /// half fails; the edits then stay pending.
    bool overwriteWorkbook(SessionMessage* problem);
    /// Give a project with no workbook one of its own, filled with @p table,
    /// and read it back as the baseline.
    bool adoptNewWorkbook(const HardpointTable& table, SessionMessage* problem);
    /// Delete the workbook copy and the edits file, and empty the model.
    void remove();

    /// Stamp each point with where it was mirrored from, out of the project's
    /// own record. A workbook cannot carry that, so it is restored after
    /// anything that comes back out of one.
    void applyMirrorProvenance(HardpointTable& table) const;
    /// The inverse: fold the table's provenance back into the project.
    void captureMirrorProvenance();

    /// Hand the model the bodies @p templ offers, and fill in what it implies
    /// for any point that has no configuration yet. Returns how many were
    /// filled; those are captured into the project already.
    int refreshConfig(const LinkageTemplate& templ);
    /// Fold what the model holds back into the project, which is what is saved.
    void captureConfig();

private:
    /// Take @p result as the workbook's baseline and cell map.
    void adoptBaseline(HardpointLoadResult& result);
    /// Empty the edits file: nothing is pending against a workbook just read.
    bool clearEditsFile(QString* error) const;
    QString editsPath() const;

    ProjectSession& m_session;
    HardpointModel m_model;
    /// The workbook the points came from, kept whole so saving can rewrite the
    /// value cells and copy every other byte through unchanged.
    XlsxHardpointSource m_source;
    /// The table exactly as the workbook holds it. Everything the user changes
    /// is a difference from this, and that difference is what the project keeps
    /// in its edits file until the workbook is overwritten or exported.
    HardpointTable m_baseline;
};

} // namespace suspkin
