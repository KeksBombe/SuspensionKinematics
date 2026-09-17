#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "model/EditHistory.h"

#include <QAction>
#include <QCoreApplication>
#include <QKeySequence>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("EditFeature", text); }

/// How long the status line says what a step took back or did again.
constexpr int kStepMessageMs = 5000;

/// A step's label inside an action's text, where a lone & would underline the
/// letter after it rather than be shown.
QString asActionText(QString label)
{
    return label.replace(QLatin1Char('&'), QLatin1String("&&"));
}

/// Undo and redo: stepping back and forth through what the user has done to the
/// points. The window records each edit as it makes it; this only walks the
/// history it keeps.
class EditFeature : public Feature {
public:
    explicit EditFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("edit"); }

    void registerCommands(CommandRegistry& commands) override
    {
        // First on the Hardpoints tab, which is where the edits are made. The
        // shortcuts are what most people will use, and those work on any tab.
        const QString page = QStringLiteral("hardpoints");
        const QString group = tr("Edit");

        commands.add({
            .id = QStringLiteral("edit.undo"),
            .text = tr("&Undo"),
            .icon = Icon::ArrowBackUp,
            .iconText = tr("Undo"),
            .shortcut = QKeySequence(QStringLiteral("Ctrl+Z")),
            .statusTip = tr("Take back the last change to the points: a move, a rename, an "
                            "added or deleted point, a mirror or a generated corner."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 0 } },
            .run = [this] { undo(); },
            .enabledWhen = [this] { return m_context.editHistory().canUndo(); },
            .textWhen = [this] { return undoText(); },
        });

        QAction* redoAction = commands.add({
            .id = QStringLiteral("edit.redo"),
            .text = tr("&Redo"),
            .icon = Icon::ArrowForwardUp,
            .iconText = tr("Redo"),
            .shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+Z")),
            .statusTip = tr("Make the last change that was undone again."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 5 } },
            .run = [this] { redo(); },
            .enabledWhen = [this] { return m_context.editHistory().canRedo(); },
            .textWhen = [this] { return redoText(); },
        });
        // Ctrl+Y too, which is where redo lives on Windows. Second, so the
        // tooltip goes on naming the one this application asks for.
        redoAction->setShortcuts(
            { QKeySequence(QStringLiteral("Ctrl+Shift+Z")), QKeySequence(QStringLiteral("Ctrl+Y")) });
    }

private:
    void undo()
    {
        EditHistory& history = m_context.editHistory();
        if (!history.canUndo()) return;
        const QString label = history.undoLabel();
        // Copies, not references into the history: restoring resolves the whole
        // project again, and nothing it sets off may leave either one dangling.
        const EditState from = history.current();
        const EditState to = history.undo();
        m_context.restoreEditState(from, to);
        m_context.showStatus(tr("Undone: %1").arg(label), kStepMessageMs);
    }

    void redo()
    {
        EditHistory& history = m_context.editHistory();
        if (!history.canRedo()) return;
        const QString label = history.redoLabel();
        const EditState from = history.current();
        const EditState to = history.redo();
        m_context.restoreEditState(from, to);
        m_context.showStatus(tr("Redone: %1").arg(label), kStepMessageMs);
    }

    /// "Undo Move F_UCA_IF", or plain "Undo" with nothing to take back.
    QString undoText() const
    {
        const QString label = m_context.editHistory().undoLabel();
        return label.isEmpty() ? tr("&Undo") : tr("&Undo %1").arg(asActionText(label));
    }

    QString redoText() const
    {
        const QString label = m_context.editHistory().redoLabel();
        return label.isEmpty() ? tr("&Redo") : tr("&Redo %1").arg(asActionText(label));
    }

    AppContext& m_context;
};

} // namespace

SUSPKIN_FEATURE(EditFeature)

} // namespace suspkin
