#include "app/DockTitleButtons.h"

#include "app/Icons.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDockWidget>
#include <QPainter>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOptionToolButton>

namespace suspkin {
namespace {

// The names QDockWidget gives its two title bar buttons. They are how the
// buttons are found, and how each is told which icon it draws.
constexpr auto kFloatButtonName = "qt_dockwidget_floatbutton";
constexpr auto kCloseButtonName = "qt_dockwidget_closebutton";

/// How far the hover and pressed panels are mixed from the window's colour
/// toward its text: enough to see on either theme, not enough to shout.
constexpr qreal kHoverShade = 0.16;
constexpr qreal kPressedShade = 0.30;
constexpr qreal kPanelRadiusPx = 3.0;

QColor blend(const QColor& from, const QColor& to, qreal amount)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount,
                            from.greenF() + (to.greenF() - from.greenF()) * amount,
                            from.blueF() + (to.blueF() - from.blueF()) * amount);
}

/// The platform style, with the painting of a dock's title bar buttons taken
/// over. Set on the buttons alone, never on the dock: the dock's title bar, and
/// everything about dragging it, stays the platform's.
///
/// The icon is chosen by the button's name rather than taken from the button,
/// because QDockWidget puts the style's standard icon back on it whenever the
/// dock floats, docks or changes style.
class DockTitleButtonStyle final : public QProxyStyle {
public:
    DockTitleButtonStyle()
        : QProxyStyle(QStyleFactory::create(QApplication::style()->name())),
          m_floatIcon(Icons::get(Icon::AppWindow)),
          m_closeIcon(Icons::get(Icon::X))
    {
    }

    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* returnData) const override
    {
        // No frame: the panel is drawn here, and a framed button would also
        // shrink its icon to ten pixels.
        if (hint == SH_DockWidget_ButtonsHaveFrame) return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget) const override
    {
        const auto* button = qstyleoption_cast<const QStyleOptionToolButton*>(option);
        if (control != CC_ToolButton || !button || !widget) {
            QProxyStyle::drawComplexControl(control, option, painter, widget);
            return;
        }
        drawPanel(*button, painter);
        drawIcon(*button, painter, iconFor(*widget, button->icon));
    }

private:
    QIcon iconFor(const QWidget& widget, const QIcon& fallback) const
    {
        if (widget.objectName() == QLatin1String(kFloatButtonName)) return m_floatIcon;
        if (widget.objectName() == QLatin1String(kCloseButtonName)) return m_closeIcon;
        return fallback;
    }

    static void drawPanel(const QStyleOptionToolButton& option, QPainter* painter)
    {
        const bool enabled = option.state.testFlag(State_Enabled);
        const bool pressed = option.state.testFlag(State_Sunken);
        const bool hovered = option.state.testFlag(State_MouseOver);
        if (!enabled || !(pressed || hovered)) return;

        const QColor window = option.palette.color(QPalette::Window);
        const QColor text = option.palette.color(QPalette::WindowText);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(blend(window, text, pressed ? kPressedShade : kHoverShade));
        painter->drawRoundedRect(QRectF(option.rect), kPanelRadiusPx, kPanelRadiusPx);
        painter->restore();
    }

    static void drawIcon(const QStyleOptionToolButton& option, QPainter* painter,
                         const QIcon& icon)
    {
        QIcon::Mode mode = QIcon::Normal;
        if (!option.state.testFlag(State_Enabled))
            mode = QIcon::Disabled;
        else if (option.state.testFlag(State_MouseOver))
            mode = QIcon::Active;
        QRect target(QPoint(), option.iconSize);
        target.moveCenter(option.rect.center());
        icon.paint(painter, target, Qt::AlignCenter, mode);
    }

    QIcon m_floatIcon;
    QIcon m_closeIcon;
};

} // namespace

void themeDockTitleButtons(QDockWidget* dock)
{
    // One style per dock, owned by it, so it goes when the buttons do.
    auto* style = new DockTitleButtonStyle;
    style->setParent(dock);
    for (const char* name : { kFloatButtonName, kCloseButtonName })
        if (auto* button = dock->findChild<QAbstractButton*>(QLatin1String(name),
                                                             Qt::FindDirectChildrenOnly))
            button->setStyle(style);
}

} // namespace suspkin
