#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"
#include "render/ViewportWidget.h"

#include <QActionGroup>
#include <QCoreApplication>
#include <QKeySequence>
#include <QMenu>
#include <QWidget>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("ViewFeature", text); }

/// Where the camera is looking and how the geometry is drawn.
///
/// Wholly its own: every command here is a call into the viewport, so nothing
/// of it is left in the window.
class ViewFeature : public Feature {
public:
    explicit ViewFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("view"); }

    void registerCommands(CommandRegistry& commands) override
    {
        registerNavigationCommands(commands);
        registerDisplayModeCommands(commands);
    }

    void windowReady() override
    {
        // The viewport's own overlay buttons and these commands are two views
        // of one mode.
        QObject::connect(m_context.viewport(), &ViewportWidget::displayModeChanged,
                         m_context.window(), [this](DisplayMode mode) {
                             (mode == DisplayMode::Solid ? m_solid : m_triangles)
                                 ->setChecked(true);
                         });
    }

private:
    void registerNavigationCommands(CommandRegistry& commands)
    {
        commands.add({
            .id = QStringLiteral("view.fit"),
            .text = tr("&Fit to view"),
            .icon = Icon::FocusCentered,
            .iconText = tr("Fit\nto View"),
            .shortcut = QKeySequence(QStringLiteral("F")),
            .statusTip = tr("Frame everything the project holds in the viewport."),
            .ribbon = { { .page = QStringLiteral("view"),
                          .group = tr("Navigate"),
                          .button = RibbonButton::Large,
                          .order = 10 } },
            .run = [this] { m_context.viewport()->fitToView(); },
        });

        registerViewPresets(commands);
    }

    /// The seven preset views. Numbers 1-7 for these; Ctrl+1..3 for the display
    /// modes, so the two sets never collide.
    ///
    /// Only the isometric one is a button: it is the split button's own half,
    /// and the arrow under it lists the rest.
    void registerViewPresets(CommandRegistry& commands)
    {
        struct PresetSpec {
            ViewPreset preset;
            const char* label;
            const char* shortcut;
        };
        static constexpr PresetSpec kPresets[] = {
            { ViewPreset::Front, QT_TR_NOOP("&Front"), "1" },
            { ViewPreset::Rear, QT_TR_NOOP("&Rear"), "2" },
            { ViewPreset::Left, QT_TR_NOOP("&Left"), "3" },
            { ViewPreset::Right, QT_TR_NOOP("Rig&ht"), "4" },
            { ViewPreset::Top, QT_TR_NOOP("&Top"), "5" },
            { ViewPreset::Bottom, QT_TR_NOOP("&Bottom"), "6" },
            { ViewPreset::Isometric, QT_TR_NOOP("&Isometric"), "7" },
        };

        m_viewsMenu = new QMenu(tr("&Views"), m_context.window());
        for (const PresetSpec& spec : kPresets) {
            const bool isDefault = spec.preset == ViewPreset::Isometric;
            const ViewPreset preset = spec.preset;
            CommandSpec command{
                .id = QStringLiteral("view.preset.") + QString::number(int(preset)),
                .text = tr(spec.label),
                .icon = isDefault ? Icon::Cube : Icon::Count,
                .shortcut = QKeySequence(QLatin1String(spec.shortcut)),
                .run = [this, preset] { m_context.viewport()->applyPreset(preset); },
            };
            if (isDefault) {
                command.statusTip = tr("Look at the car from the front, above and to one side. "
                                       "The arrow under it lists every other view.");
                command.ribbon = { { .page = QStringLiteral("view"),
                                     .group = tr("Navigate"),
                                     .button = RibbonButton::Split,
                                     .menu = m_viewsMenu,
                                     .order = 20 } };
            }
            m_viewsMenu->addAction(commands.add(std::move(command)));
        }
    }

    /// Icons that match what the viewport's own mode selector draws: a shaded
    /// ball, and a meshed one. Exclusive, because it is in one mode or the
    /// other.
    void registerDisplayModeCommands(CommandRegistry& commands)
    {
        m_modes = new QActionGroup(m_context.window());
        m_modes->setExclusive(true);

        const auto addMode = [this, &commands](const QString& id, const QString& text,
                                               const QString& shortcut, Icon icon,
                                               DisplayMode mode, const QString& tip, int order) {
            QAction* action = commands.add({
                .id = id,
                .text = text,
                .icon = icon,
                .shortcut = QKeySequence(shortcut),
                .statusTip = tip,
                .checkable = true,
                .ribbon = { { .page = QStringLiteral("view"),
                              .group = tr("Display"),
                              .order = order } },
                // triggered, not toggled: pressing the mode already showing is
                // still the user choosing it, and setting the default below
                // must not read as a choice at all.
                .run = [this, mode] { m_context.viewport()->setDisplayMode(mode); },
            });
            m_modes->addAction(action);
            return action;
        };

        m_solid = addMode(QStringLiteral("view.solid"), tr("&Solid"), QStringLiteral("Ctrl+1"),
                          Icon::Sphere, DisplayMode::Solid, tr("Draw the geometry shaded."), 30);
        m_triangles = addMode(
            QStringLiteral("view.triangles"), tr("&Triangles"), QStringLiteral("Ctrl+2"),
            Icon::Triangles, DisplayMode::Triangles,
            tr("Draw the geometry's triangles, to see how it was tessellated."), 40);
        m_solid->setChecked(true); // matches ViewportWidget's default
    }

    AppContext& m_context;
    QMenu* m_viewsMenu = nullptr;
    QActionGroup* m_modes = nullptr;
    QAction* m_solid = nullptr;
    QAction* m_triangles = nullptr;
};

} // namespace

SUSPKIN_FEATURE(ViewFeature)

} // namespace suspkin
