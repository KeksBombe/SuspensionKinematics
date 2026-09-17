#pragma once

#include "model/Hardpoint.h"
#include "model/HardpointConfig.h"
#include "model/SuspensionSolver.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <vector>

namespace suspkin {

/// Everything an edit of the points can change, as one value.
///
/// The table carries the names, the coordinates and where each point was
/// mirrored from; the configuration is what each point is for. The static
/// angles are here because Generate from Design writes them in the same act as
/// the points it makes, and an undo that put the points back and left the
/// angles would leave a car nobody ever had.
///
/// Nothing else is. The workbook, the template, the wheel models, the design
/// targets and the view are not edits of the points, so stepping back through
/// the points leaves them where they are.
struct EditState {
    HardpointTable table;
    HardpointConfigMap config;
    QHash<QString, StaticAlignment> alignment;

    bool operator==(const EditState& other) const;
    bool operator!=(const EditState& other) const { return !(*this == other); }
};

/// The states the user's editing has passed through, and which of them the
/// project is in.
///
/// Whole states rather than the edits between them. An edit has side effects
/// -- a rename moves configuration and provenance, a delete takes both, a
/// generate writes angles -- and an inverse that missed one would put back a
/// project that never existed. A state cannot be applied the wrong way round:
/// what an undo puts back is exactly what was there. It is cheap, too: the
/// names and the configuration are implicitly shared between states, so a step
/// costs one copy of the rows -- a few kilobytes for a whole car.
///
/// The first state is the one the project opened in. It has no label, and
/// nothing undoes past it.
class EditHistory {
public:
    /// Far more steps than anyone takes back, and a few megabytes at most.
    static constexpr int kDefaultLimit = 500;

    explicit EditHistory(int limit = kDefaultLimit);

    /// Start again from @p state, with nothing to undo or redo: a project
    /// opened, or its points came in from a workbook. The steps that led to
    /// the old points do not lead anywhere in the new ones.
    void reset(EditState state);

    /// The project now holds @p state, which doing @p label produced. Whatever
    /// could have been redone is gone -- that was a different future. Returns
    /// false, recording nothing, when @p state is the one already current: an
    /// edit that changed nothing is not a step anyone wants to take back.
    bool record(const QString& label, EditState state);

    bool canUndo() const { return m_at > 0; }
    bool canRedo() const { return m_at + 1 < m_steps.size(); }
    int undoCount() const { return static_cast<int>(m_at); }
    int redoCount() const { return static_cast<int>(m_steps.size() - m_at - 1); }

    /// What undo() would take back, and what redo() would do again. Empty when
    /// there is nothing to take back or do again.
    QString undoLabel() const;
    QString redoLabel() const;

    /// Step back over one edit and return the state that leaves the project
    /// in. Only when canUndo().
    const EditState& undo();
    /// Step forward over one edit again. Only when canRedo().
    const EditState& redo();

    const EditState& current() const { return m_steps[m_at].state; }

    /// Rename body @p from to @p to in every state held. A part relabelled in
    /// the template renames the body its rows name; that is not an edit of the
    /// points, so it is not a step -- it is true of every state, all of which
    /// describe the same part. Without it an undo would put back rows naming a
    /// body the template no longer has.
    void renameBody(const QString& from, const QString& to);

private:
    struct Step {
        QString label; ///< the edit that produced this state; empty for the first
        EditState state;
    };

    /// Drop the oldest steps beyond the limit. The oldest state left becomes
    /// the one nothing undoes past.
    void trimToLimit();

    std::vector<Step> m_steps;
    std::size_t m_at = 0; ///< the state the project is in
    std::size_t m_limit;
};

/// The points a step from @p before to @p after renamed, old name to new.
///
/// A rename changes a row's name and nothing else, so it is read off a row
/// that is in the same place, at the same coordinates, under a different name
/// -- which says "rename" only when both tables have the same rows. Anything
/// else outside the states that names a point, the wheels, follows through
/// this.
QHash<QString, QString> renamedPoints(const HardpointTable& before, const HardpointTable& after);

/// The points of @p after that a step from @p before touched: added, moved,
/// renamed, mirrored or configured differently. In table order. What an undo
/// or a redo selects, so the user sees what it put back.
QStringList touchedPoints(const EditState& before, const EditState& after);

} // namespace suspkin
