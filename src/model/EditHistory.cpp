#include "model/EditHistory.h"

#include <algorithm>
#include <utility>

namespace suspkin {
namespace {

bool samePoint(const Hardpoint& a, const Hardpoint& b)
{
    return a.name == b.name && a.mirrorOf == b.mirrorOf
           && std::equal(std::begin(a.coord), std::end(a.coord), std::begin(b.coord));
}

bool samePlace(const Hardpoint& a, const Hardpoint& b)
{
    return std::equal(std::begin(a.coord), std::end(a.coord), std::begin(b.coord));
}

/// Whether @p point is in @p before exactly as it is now, configuration and all.
bool untouched(const Hardpoint& point, const EditState& before, const EditState& after)
{
    const Hardpoint* was = before.table.find(point.name);
    return was && samePoint(*was, point)
           && before.config.value(point.name) == after.config.value(point.name);
}

} // namespace

bool EditState::operator==(const EditState& other) const
{
    return std::equal(table.points.begin(), table.points.end(), other.table.points.begin(),
                      other.table.points.end(), samePoint)
           && config == other.config && alignment == other.alignment;
}

EditHistory::EditHistory(int limit) : m_limit(static_cast<std::size_t>(std::max(limit, 1)))
{
    m_steps.push_back(Step{});
}

void EditHistory::reset(EditState state)
{
    m_steps.clear();
    m_steps.push_back(Step{ QString(), std::move(state) });
    m_at = 0;
}

bool EditHistory::record(const QString& label, EditState state)
{
    if (state == current()) return false;

    m_steps.erase(m_steps.begin() + static_cast<std::ptrdiff_t>(m_at) + 1, m_steps.end());
    m_steps.push_back(Step{ label, std::move(state) });
    m_at = m_steps.size() - 1;
    trimToLimit();
    return true;
}

QString EditHistory::undoLabel() const
{
    return canUndo() ? m_steps[m_at].label : QString();
}

QString EditHistory::redoLabel() const
{
    return canRedo() ? m_steps[m_at + 1].label : QString();
}

const EditState& EditHistory::undo()
{
    if (canUndo()) --m_at;
    return current();
}

const EditState& EditHistory::redo()
{
    if (canRedo()) ++m_at;
    return current();
}

void EditHistory::renameBody(const QString& from, const QString& to)
{
    for (Step& step : m_steps) suspkin::renameBody(step.state.config, from, to);
}

void EditHistory::trimToLimit()
{
    // Steps, not states: the limit counts what can be undone, and the state
    // nothing undoes past is one more than that.
    const std::size_t states = m_limit + 1;
    if (m_steps.size() <= states) return;

    const std::size_t excess = m_steps.size() - states;
    m_steps.erase(m_steps.begin(), m_steps.begin() + static_cast<std::ptrdiff_t>(excess));
    m_at -= excess;
    m_steps.front().label.clear();
}

QHash<QString, QString> renamedPoints(const HardpointTable& before, const HardpointTable& after)
{
    QHash<QString, QString> renamed;
    if (before.size() != after.size()) return renamed;

    for (std::size_t row = 0; row < before.size(); ++row) {
        const Hardpoint& was = before.points[row];
        const Hardpoint& now = after.points[row];
        if (was.name != now.name && samePlace(was, now)) renamed.insert(was.name, now.name);
    }
    return renamed;
}

QStringList touchedPoints(const EditState& before, const EditState& after)
{
    QStringList touched;
    for (const Hardpoint& point : after.table.points)
        if (!untouched(point, before, after)) touched << point.name;
    return touched;
}

} // namespace suspkin
