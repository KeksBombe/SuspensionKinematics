#include "app/Ribbon.h"

#include <QAction>
#include <QActionEvent>
#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QFontMetrics>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>

#include <algorithm>
#include <functional>

namespace suspkin {
namespace {

constexpr int kLargeIcon = 32;
constexpr int kSmallIcon = 16;
constexpr int kLargeMinWidth = 56;
constexpr int kRadius = 4;
/// Room for the small chevron that says a button has a menu.
constexpr int kArrowWidth = 7;
/// A small button's icon inset, and the gap between it and its label.
constexpr int kSmallInset = 4;
constexpr int kSmallGap = 5;
/// Where a large button's icon starts, and the gap under it.
constexpr int kLargeTop = 4;
constexpr int kLargeIconGap = 3;

/// How big everything is, from the font it is labelled in: a desktop with
/// larger text gets a taller ribbon rather than clipped labels.
struct Metrics {
    int small = 0;    ///< a small button's height, a third of a large one's
    int large = 0;
    int tabRow = 0;
    int trailing = 0; ///< an icon-only button in the tab row, square
    int caption = 0;  ///< the band under a group its caption is written in
};

QFont captionFont(const QFont& font)
{
    QFont smaller = font;
    if (smaller.pointSizeF() > 0)
        smaller.setPointSizeF(smaller.pointSizeF() * 0.86);
    else
        smaller.setPixelSize(std::max(8, qRound(smaller.pixelSize() * 0.86)));
    return smaller;
}

Metrics metricsFor(const QFont& font)
{
    const QFontMetrics fm(font);
    Metrics m;
    // A large button holds its icon and two lines of label; three small ones
    // stack beside it in the same height, each with room for its one line.
    const int largeContent = kLargeTop + kLargeIcon + kLargeIconGap + 2 * fm.lineSpacing() + 2;
    m.small = std::max({ 22, fm.height() + 6, (largeContent + 2) / 3 });
    m.large = 3 * m.small;
    m.tabRow = std::max(28, fm.height() + 12);
    m.trailing = m.tabRow - 4;
    m.caption = QFontMetrics(captionFont(font)).height() + 3;
    return m;
}

QColor mix(const QColor& a, const QColor& b, float t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QColor withAlpha(QColor colour, float alpha)
{
    colour.setAlphaF(alpha);
    return colour;
}

/// Every colour the ribbon draws, mixed from the palette the way the hardpoint
/// table mixes its own: nothing here is a colour picked for one theme.
struct Colours {
    QColor tabRow;
    QColor page;
    QColor rule;
    QColor separator;
    QColor text;
    QColor disabledText;
    QColor caption;
    QColor hover;
    QColor checked;
    QColor checkedEdge;
    QColor pressed;
    QColor accent;
    QColor accentText;
};

Colours coloursFor(const QPalette& palette)
{
    const QColor window = palette.color(QPalette::Window);
    const QColor base = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::Active, QPalette::ButtonText);
    const QColor highlight = palette.color(QPalette::Active, QPalette::Highlight);
    const bool dark = window.lightness() < 128;

    Colours c;
    c.tabRow = window;
    c.page = mix(window, base, 0.5f);
    c.rule = mix(window, palette.color(QPalette::WindowText), dark ? 0.22f : 0.15f);
    c.separator = palette.color(QPalette::Mid);
    c.text = text;
    c.disabledText = palette.color(QPalette::Disabled, QPalette::ButtonText);
    if (c.disabledText.rgba() == text.rgba()) c.disabledText = mix(text, window, 0.55f);
    c.caption = palette.color(QPalette::PlaceholderText);
    c.hover = withAlpha(text, dark ? 0.12f : 0.08f);
    c.checked = withAlpha(highlight, dark ? 0.32f : 0.20f);
    c.checkedEdge = withAlpha(highlight, dark ? 0.70f : 0.55f);
    c.pressed = withAlpha(highlight, dark ? 0.45f : 0.32f);
    c.accent = highlight;
    c.accentText = palette.color(QPalette::Active, QPalette::HighlightedText);
    return c;
}

/// "&Import Chassis..." as a person reads it: "Import Chassis".
QString plainLabel(QString text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('&') && i + 1 < text.size()) ++i;
        out += text.at(i);
    }
    if (out.endsWith(QLatin1String("..."))) out.chop(3);
    if (out.endsWith(QChar(0x2026))) out.chop(1);
    return out.trimmed();
}

void drawChevron(QPainter& painter, const QPointF& centre, const QColor& colour)
{
    const qreal half = kArrowWidth / 2.0;
    QPen pen(colour, 1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const QPointF points[] = { centre + QPointF(-half, -half / 2),
                               centre + QPointF(0.0, half / 2),
                               centre + QPointF(half, -half / 2) };
    painter.drawPolyline(points, 3);
}

// ---------------------------------------------------------------------------

/// A button on the ribbon. It paints itself -- background, icon, label and
/// menu chevron -- from the palette, because a style's own tool button differs
/// from one platform to the next in exactly the things a ribbon is made of: how
/// a checked button looks, where a split button splits, whether a label may
/// have two lines.
class RibbonButton : public QToolButton {
public:
    enum class Kind { Large, Small, Trailing, Application };

    RibbonButton(Kind kind, QWidget* parent) : QToolButton(parent), m_kind(kind)
    {
        setAutoRaise(true);
        // A click on the ribbon leaves the keyboard where it was -- in the
        // table, or on the viewport.
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover);
        switch (kind) {
        case Kind::Large:
            setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            setIconSize(QSize(kLargeIcon, kLargeIcon));
            break;
        case Kind::Small:
            setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            setIconSize(QSize(kSmallIcon, kSmallIcon));
            break;
        case Kind::Trailing:
            setToolButtonStyle(Qt::ToolButtonIconOnly);
            setIconSize(QSize(kSmallIcon, kSmallIcon));
            break;
        case Kind::Application:
            setToolButtonStyle(Qt::ToolButtonTextOnly);
            break;
        }
    }

    /// A label other than the action's own: a split button is labelled with
    /// its menu, not with what its icon half does.
    void setLabel(const QString& label)
    {
        m_label = label;
        updateGeometry();
        update();
    }

    QSize sizeHint() const override
    {
        ensurePolished();
        const QFontMetrics fm(font());
        const Metrics m = metricsFor(font());
        const QString text = label();
        const int arrow = hasMenu() ? kArrowWidth + 4 : 0;
        switch (m_kind) {
        case Kind::Large: {
            const QStringList lines = text.split(QLatin1Char('\n'));
            int widest = 0;
            for (qsizetype i = 0; i < lines.size(); ++i) {
                const int width = fm.horizontalAdvance(lines.at(i))
                                  + (i == lines.size() - 1 ? arrow : 0);
                widest = std::max(widest, width);
            }
            return { std::max(kLargeMinWidth, widest + 14), m.large };
        }
        case Kind::Small:
            return { kSmallInset + kSmallIcon + kSmallGap + fm.horizontalAdvance(text) + 8 + arrow,
                     m.small };
        case Kind::Trailing:
            return { m.trailing + (hasMenu() ? kArrowWidth + 2 : 0), m.trailing };
        case Kind::Application:
            return { fm.horizontalAdvance(text) + 26 + arrow, m.tabRow - 6 };
        }
        return QToolButton::sizeHint();
    }

    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const Colours c = coloursFor(palette());
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool enabled = isEnabled();
        const bool hovered = enabled && underMouse();
        const bool menuOpen = menu() && menu()->isVisible();
        const bool split = popupMode() == QToolButton::MenuButtonPopup && menu();

        const auto fill = [&painter](const QRectF& area, const QColor& colour) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colour);
            painter.drawRoundedRect(area, kRadius, kRadius);
        };

        if (m_kind == Kind::Application) {
            QColor colour = c.accent;
            if (isDown() || menuOpen)
                colour = colour.darker(118);
            else if (hovered)
                colour = colour.lighter(112);
            fill(frame, colour);
            const QFontMetrics fm(font());
            const QString text = label();
            const int arrow = hasMenu() ? kArrowWidth + 4 : 0;
            const int textWidth = fm.horizontalAdvance(text);
            const int left = (width() - textWidth - arrow) / 2;
            painter.setPen(c.accentText);
            painter.drawText(QRect(left, 0, textWidth, height()), Qt::AlignVCenter | Qt::AlignLeft,
                             text);
            if (arrow)
                drawChevron(painter, QPointF(left + textWidth + 4 + kArrowWidth / 2.0,
                                             height() / 2.0 + 0.5),
                            c.accentText);
            return;
        }

        // The background: checked is a wash of the highlight, hovered a wash of
        // the text colour, pressed a stronger highlight. A split button lights
        // the half under the mouse.
        if (isChecked()) {
            // Half strength when the button is disabled: it still says the
            // setting is on, without reading as something that can be pressed.
            const qreal strength = enabled ? 1.0 : 0.45;
            QColor wash = c.checked;
            QColor edge = c.checkedEdge;
            wash.setAlphaF(wash.alphaF() * float(strength));
            edge.setAlphaF(edge.alphaF() * float(strength));
            fill(frame, wash);
            painter.setPen(QPen(edge, 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(frame, kRadius, kRadius);
        }
        if (split) {
            const QRectF menuArea = QRectF(menuPart()).adjusted(0.5, 0.5, -0.5, -0.5);
            const QRectF buttonArea = QRectF(buttonPart()).adjusted(0.5, 0.5, -0.5, -0.5);
            if (hovered || menuOpen) {
                fill(frame, c.hover);
                fill(m_hoverMenuPart || menuOpen ? menuArea : buttonArea, c.hover);
                painter.setPen(QPen(c.hover, 1.0));
                const qreal y = menuArea.top();
                painter.drawLine(QPointF(frame.left() + 3, y), QPointF(frame.right() - 3, y));
            }
            if (isDown()) fill(buttonArea, c.pressed);
            if (menuOpen) fill(menuArea, c.pressed);
        } else if (isDown() || menuOpen) {
            fill(frame, c.pressed);
        } else if (hovered) {
            fill(frame, c.hover);
        }

        const QIcon::Mode mode = !enabled ? QIcon::Disabled : hovered ? QIcon::Active : QIcon::Normal;
        const QIcon::State state = isChecked() ? QIcon::On : QIcon::Off;
        const QColor textColour = enabled ? c.text : c.disabledText;
        const QFontMetrics fm(font());
        painter.setPen(textColour);

        switch (m_kind) {
        case Kind::Large: {
            const QPixmap pixmap = icon().pixmap(iconSize(), devicePixelRatioF(), mode, state);
            painter.drawPixmap(QPoint((width() - kLargeIcon) / 2, kLargeTop), pixmap);

            // Each line centred on its own, the chevron -- when there is one --
            // following the last line and centred with it.
            const QStringList lines = label().split(QLatin1Char('\n'));
            const int arrow = hasMenu() ? kArrowWidth + 4 : 0;
            int y = kLargeTop + kLargeIcon + kLargeIconGap;
            for (qsizetype i = 0; i < lines.size(); ++i) {
                const bool last = i == lines.size() - 1;
                const int lineWidth = fm.horizontalAdvance(lines.at(i));
                const int left = (width() - lineWidth - (last ? arrow : 0)) / 2;
                painter.setPen(textColour);
                painter.drawText(QPoint(left, y + fm.ascent()), lines.at(i));
                if (last && arrow)
                    drawChevron(painter,
                                QPointF(left + lineWidth + 4 + kArrowWidth / 2.0,
                                        y + fm.height() / 2.0 + 0.5),
                                textColour);
                y += fm.lineSpacing();
            }
            break;
        }
        case Kind::Small: {
            const QPixmap pixmap = icon().pixmap(iconSize(), devicePixelRatioF(), mode, state);
            painter.drawPixmap(QPoint(kSmallInset, (height() - kSmallIcon) / 2), pixmap);
            const int left = kSmallInset + kSmallIcon + kSmallGap;
            painter.drawText(QRect(left, 0, width() - left, height()),
                             Qt::AlignVCenter | Qt::AlignLeft, label());
            if (hasMenu())
                drawChevron(painter, QPointF(width() - 4 - kArrowWidth / 2.0, height() / 2.0 + 0.5),
                            textColour);
            break;
        }
        case Kind::Trailing: {
            const int arrow = hasMenu() ? kArrowWidth + 2 : 0;
            const QPixmap pixmap = icon().pixmap(iconSize(), devicePixelRatioF(), mode, state);
            const int left = (width() - arrow - kSmallIcon) / 2;
            painter.drawPixmap(QPoint(left, (height() - kSmallIcon) / 2), pixmap);
            if (arrow)
                drawChevron(painter,
                            QPointF(width() - 3 - kArrowWidth / 2.0, height() / 2.0 + 0.5),
                            textColour);
            break;
        }
        case Kind::Application:
            break;
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (popupMode() == QToolButton::MenuButtonPopup && menu()) {
            if (event->button() == Qt::LeftButton && menuPart().contains(event->position().toPoint())) {
                showMenu();
                return;
            }
            // Not QToolButton's: it asks the style where the menu half is, and
            // every style says a strip down the right-hand side.
            QAbstractButton::mousePressEvent(event);
            return;
        }
        QToolButton::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const bool over = menuPart().contains(event->position().toPoint());
        if (over != m_hoverMenuPart) {
            m_hoverMenuPart = over;
            update();
        }
        QToolButton::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        m_hoverMenuPart = false;
        update();
        QToolButton::leaveEvent(event);
    }

    void actionEvent(QActionEvent* event) override
    {
        QToolButton::actionEvent(event);
        // The label comes from the action, and may just have changed length.
        if (event->type() == QEvent::ActionChanged) {
            updateGeometry();
            update();
        }
    }

private:
    QString label() const
    {
        QString text = m_label;
        if (text.isEmpty() && defaultAction()) text = defaultAction()->iconText();
        // plainLabel, because the File button's own text carries the mnemonic
        // that opens it: Alt+F, written "&File".
        if (text.isEmpty()) text = plainLabel(QToolButton::text());
        // The break in a large button's label is a space on a small one.
        if (m_kind != Kind::Large) text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        return text;
    }

    bool hasMenu() const
    {
        return menu() != nullptr || (defaultAction() && defaultAction()->menu() != nullptr);
    }

    /// The half of a split button that opens its menu: under the icon on a
    /// large button, the way Office and Inventor split theirs, and the chevron
    /// at the end of a small one.
    QRect menuPart() const
    {
        if (popupMode() != QToolButton::MenuButtonPopup || !menu()) return {};
        if (m_kind == Kind::Large) {
            const int top = kLargeTop + kLargeIcon + kLargeIconGap - 1;
            return { 0, top, width(), height() - top };
        }
        const int strip = kArrowWidth + 8;
        return { width() - strip, 0, strip, height() };
    }

    QRect buttonPart() const
    {
        const QRect menuArea = menuPart();
        if (menuArea.isNull()) return rect();
        if (m_kind == Kind::Large) return { 0, 0, width(), menuArea.top() };
        return { 0, 0, menuArea.left(), height() };
    }

    Kind m_kind;
    QString m_label;
    bool m_hoverMenuPart = false;
};

/// The line between two groups, and between the panel toggles and Help.
class RibbonSeparator : public QWidget {
public:
    explicit RibbonSeparator(QWidget* parent) : QWidget(parent)
    {
        setFixedWidth(9);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const int x = width() / 2;
        painter.fillRect(QRect(x, 4, 1, height() - 8), coloursFor(palette()).separator);
    }
};

/// A page, scrolling sideways when the window is too narrow for it rather than
/// clipping its last groups. How tall it is -- with room for the scroll bar or
/// without -- is the ribbon's to decide, in Ribbon::fitStack().
QScrollArea* makeScrollArea(RibbonPage* page, QWidget* parent)
{
    auto* scroll = new QScrollArea(parent);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFocusPolicy(Qt::NoFocus);
    scroll->setWidget(page);
    // Transparent all the way down: the ribbon paints the page colour behind
    // every page, so there is one fill and no seams. After setWidget(), which
    // turns the page's own fill back on.
    scroll->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    page->setAutoFillBackground(false);
    return scroll;
}

} // namespace

// ---------------------------------------------------------------------------

/// Flat tabs: the current one underlined in the highlight colour, the others
/// lit only while the mouse is over them.
class RibbonTabBar : public QTabBar {
public:
    explicit RibbonTabBar(QWidget* parent) : QTabBar(parent)
    {
        setDrawBase(false);
        setExpanding(false);
        setElideMode(Qt::ElideNone);
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
    }

    /// Collapsed, no tab is drawn as the one showing, because none is.
    void setCollapsedLook(bool collapsed)
    {
        m_collapsed = collapsed;
        update();
    }

    std::function<void(int)> onDoubleClick;
    /// The tab was reached by its Alt mnemonic rather than by a click.
    std::function<void()> onKeyboardActivation;

protected:
    bool event(QEvent* event) override
    {
        // A collapsed ribbon opens when a tab is clicked; reaching one by its
        // mnemonic means the same thing.
        if (event->type() == QEvent::Shortcut && onKeyboardActivation) onKeyboardActivation();
        return QTabBar::event(event);
    }

    QSize tabSizeHint(int index) const override
    {
        const Metrics m = metricsFor(font());
        return { fontMetrics().horizontalAdvance(plainLabel(tabText(index))) + 24, m.tabRow - 2 };
    }

    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const Colours c = coloursFor(palette());
        for (int i = 0; i < count(); ++i) {
            const QRect area = tabRect(i);
            const bool current = i == currentIndex() && !m_collapsed;
            if (i == m_hover && !current) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(c.hover);
                painter.drawRoundedRect(QRectF(area).adjusted(2, 4, -2, -3), kRadius, kRadius);
            }
            painter.setPen(isEnabled() ? c.text : c.disabledText);
            // Hidden rather than underlined: the mnemonic is there for Alt,
            // and a row of underlined letters is not what a ribbon looks like.
            painter.drawText(area, Qt::AlignCenter | Qt::TextHideMnemonic, tabText(i));
            if (current) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(c.accent);
                painter.drawRoundedRect(QRectF(area.left() + 8, area.bottom() - 2.5,
                                               area.width() - 16, 3.0),
                                        1.5, 1.5);
            }
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const int hover = tabAt(event->position().toPoint());
        if (hover != m_hover) {
            m_hover = hover;
            update();
        }
        QTabBar::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        m_hover = -1;
        update();
        QTabBar::leaveEvent(event);
    }

    /// Reported here rather than through QTabBar's, which then handles the
    /// second press of the pair as a click of its own -- and a click on a
    /// collapsed ribbon expands it again straight after the double-click
    /// collapsed it.
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        const int index = tabAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && index >= 0 && onDoubleClick) onDoubleClick(index);
        event->accept();
    }

private:
    int m_hover = -1;
    bool m_collapsed = false;
};

// ---------------------------------------------------------------------------

QString commandToolTip(const QAction* action)
{
    QString tip = QStringLiteral("<b>%1</b>").arg(plainLabel(action->text()).toHtmlEscaped());
    const QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
    if (!shortcut.isEmpty()) tip += QStringLiteral("&nbsp;(%1)").arg(shortcut.toHtmlEscaped());
    if (!action->statusTip().isEmpty())
        tip += QStringLiteral("<br>%1").arg(action->statusTip().toHtmlEscaped());
    return tip;
}

// ---------------------------------------------------------------------------

RibbonGroup::RibbonGroup(const QString& caption, QWidget* parent)
    : QWidget(parent), m_caption(caption)
{
    const Metrics m = metricsFor(font());
    auto* outer = new QVBoxLayout(this);
    // The bottom margin is the band the caption is painted in.
    outer->setContentsMargins(3, 2, 3, m.caption);
    outer->setSpacing(0);
    m_row = new QHBoxLayout;
    m_row->setContentsMargins(0, 0, 0, 0);
    m_row->setSpacing(2);
    outer->addLayout(m_row);
}

QToolButton* RibbonGroup::addLarge(QAction* action)
{
    m_column = nullptr; // a large button ends the column of small ones before it
    auto* button = new RibbonButton(RibbonButton::Kind::Large, this);
    button->setDefaultAction(action);
    m_row->addWidget(button, 0, Qt::AlignTop);
    return button;
}

QToolButton* RibbonGroup::addSmall(QAction* action)
{
    if (!m_column || m_columnCount == 3) {
        m_column = new QVBoxLayout;
        m_column->setContentsMargins(0, 0, 0, 0);
        m_column->setSpacing(0);
        // Buttons go in above this, so a column of one or two sits at the top
        // rather than spreading down the group.
        m_column->addStretch(1);
        m_row->addLayout(m_column);
        m_columnCount = 0;
    }
    auto* button = new RibbonButton(RibbonButton::Kind::Small, this);
    button->setDefaultAction(action);
    // Left-aligned rather than stretched: a column of buttons of their own
    // widths reads as a list, and each lights up only as far as its label.
    m_column->insertWidget(m_column->count() - 1, button, 0, Qt::AlignLeft);
    ++m_columnCount;
    return button;
}

QToolButton* RibbonGroup::addSplit(QAction* defaultAction, QMenu* menu)
{
    m_column = nullptr;
    auto* button = new RibbonButton(RibbonButton::Kind::Large, this);
    button->setDefaultAction(defaultAction);
    button->setMenu(menu);
    button->setPopupMode(QToolButton::MenuButtonPopup);
    button->setLabel(plainLabel(menu->title()));
    m_row->addWidget(button, 0, Qt::AlignTop);
    return button;
}

int RibbonGroup::captionWidth() const
{
    return QFontMetrics(captionFont(font())).horizontalAdvance(m_caption) + 12;
}

QSize RibbonGroup::sizeHint() const
{
    QSize size = QWidget::sizeHint();
    size.setWidth(std::max(size.width(), captionWidth()));
    return size;
}

QSize RibbonGroup::minimumSizeHint() const
{
    QSize size = QWidget::minimumSizeHint();
    size.setWidth(std::max(size.width(), captionWidth()));
    return size;
}

void RibbonGroup::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const Metrics m = metricsFor(font());
    painter.setFont(captionFont(font()));
    painter.setPen(coloursFor(palette()).caption);
    painter.drawText(QRect(0, height() - m.caption, width(), m.caption - 2),
                     Qt::AlignHCenter | Qt::AlignBottom, m_caption);
}

// ---------------------------------------------------------------------------

RibbonPage::RibbonPage(const QString& key, QWidget* parent) : QWidget(parent), m_key(key)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 2, 4, 1);
    m_layout->setSpacing(0);
    m_layout->addStretch(1);
    updateHeight();
}

RibbonGroup* RibbonPage::addGroup(const QString& caption)
{
    // Each goes in before the stretch at the end, which keeps the groups
    // packed to the left however wide the window is.
    if (!m_groups.isEmpty()) m_layout->insertWidget(m_layout->count() - 1, new RibbonSeparator(this));
    auto* group = new RibbonGroup(caption, this);
    m_layout->insertWidget(m_layout->count() - 1, group);
    m_groups << group;
    return group;
}

void RibbonPage::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange) updateHeight();
}

void RibbonPage::updateHeight()
{
    const Metrics m = metricsFor(font());
    const QMargins margins = m_layout->contentsMargins();
    // Every page the same height, whatever is on it, so switching tabs never
    // moves the viewport under the ribbon.
    setFixedHeight(margins.top() + 2 + m.large + m.caption + margins.bottom());
}

// ---------------------------------------------------------------------------

Ribbon::Ribbon(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* outer = new QVBoxLayout(this);
    // One pixel at the bottom for the rule between the ribbon and whatever is
    // under it.
    outer->setContentsMargins(0, 0, 0, 1);
    outer->setSpacing(0);

    m_tabRow = new QWidget(this);
    m_tabLayout = new QHBoxLayout(m_tabRow);
    m_tabLayout->setContentsMargins(6, 0, 6, 0);
    m_tabLayout->setSpacing(4);
    m_tabBar = new RibbonTabBar(m_tabRow);
    m_tabLayout->addWidget(m_tabBar, 0, Qt::AlignBottom);
    m_tabLayout->addStretch(1);
    m_trailingLayout = new QHBoxLayout;
    m_trailingLayout->setContentsMargins(0, 0, 0, 0);
    m_trailingLayout->setSpacing(2);
    m_tabLayout->addLayout(m_trailingLayout);
    outer->addWidget(m_tabRow);

    m_stack = new QStackedWidget(this);
    m_stack->setAutoFillBackground(false);
    outer->addWidget(m_stack);

    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        if (index < 0 || index >= m_keys.size()) return;
        m_stack->setCurrentIndex(index);
        fitStack();
        emit currentPageChanged(m_keys.at(index));
    });
    // A press on a tab of a collapsed ribbon brings the page back. Remembered,
    // so a double-click that began with that press opens it rather than
    // opening and closing it again.
    connect(m_tabBar, &QTabBar::tabBarClicked, this, [this](int index) {
        m_clickExpanded = false;
        if (index < 0 || !m_collapsed) return;
        setCollapsed(false);
        m_clickExpanded = true;
    });
    m_tabBar->onKeyboardActivation = [this] { setCollapsed(false); };
    m_tabBar->onDoubleClick = [this](int) {
        const bool justExpanded = m_clickExpanded;
        m_clickExpanded = false;
        if (!justExpanded) setCollapsed(!m_collapsed);
    };

    updateMetrics();
}

void Ribbon::setApplicationMenu(QMenu* menu, const QString& text)
{
    if (m_applicationButton) {
        m_tabLayout->removeWidget(m_applicationButton);
        delete m_applicationButton;
    }
    auto* button = new RibbonButton(RibbonButton::Kind::Application, m_tabRow);
    button->setText(text);
    button->setMenu(menu);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolTip(plainLabel(menu->title()));
    m_tabLayout->insertWidget(0, button, 0, Qt::AlignVCenter);
    m_applicationButton = button;
}

RibbonPage* Ribbon::addPage(const QString& key, const QString& title)
{
    auto* page = new RibbonPage(key);
    m_stack->addWidget(makeScrollArea(page, m_stack));
    m_pages << page;
    // The key first: adding the first tab makes it current, and that is
    // reported by key.
    m_keys << key;
    m_tabBar->addTab(title);
    return page;
}

RibbonPage* Ribbon::page(const QString& key) const
{
    const qsizetype index = m_keys.indexOf(key);
    return index < 0 ? nullptr : m_pages.at(index);
}

QToolButton* Ribbon::addTrailingAction(QAction* action)
{
    auto* button = new RibbonButton(RibbonButton::Kind::Trailing, m_tabRow);
    button->setDefaultAction(action);
    m_trailingLayout->addWidget(button, 0, Qt::AlignVCenter);
    return button;
}

void Ribbon::addTrailingSeparator()
{
    auto* separator = new RibbonSeparator(m_tabRow);
    m_trailingLayout->addWidget(separator);
    m_trailingSeparators << separator;
    updateMetrics();
}

QString Ribbon::currentPage() const
{
    const int index = m_tabBar->currentIndex();
    return index >= 0 && index < m_keys.size() ? m_keys.at(index) : QString();
}

void Ribbon::setCurrentPage(const QString& key)
{
    if (m_keys.isEmpty()) return;
    const qsizetype index = m_keys.indexOf(key);
    m_tabBar->setCurrentIndex(index < 0 ? 0 : int(index));
}

void Ribbon::setCollapsed(bool collapsed)
{
    if (collapsed == m_collapsed) return;
    m_collapsed = collapsed;
    m_stack->setVisible(!collapsed);
    m_tabBar->setCollapsedLook(collapsed);
    updateGeometry();
    update();
    emit collapsedChanged(collapsed);
}

void Ribbon::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const Colours c = coloursFor(palette());
    painter.fillRect(rect(), c.tabRow);
    if (!m_collapsed) {
        const int top = m_tabRow->geometry().bottom() + 1;
        painter.fillRect(QRect(0, top, width(), height() - top), c.page);
        painter.fillRect(QRect(0, top, width(), 1), withAlpha(c.rule, 0.5f));
    }
    painter.fillRect(QRect(0, height() - 1, width(), 1), c.rule);
}

void Ribbon::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange) updateMetrics();
}

void Ribbon::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    fitStack();
}

void Ribbon::updateMetrics()
{
    const Metrics m = metricsFor(font());
    m_tabRow->setFixedHeight(m.tabRow);
    for (QWidget* separator : std::as_const(m_trailingSeparators))
        separator->setFixedHeight(m.trailing);
    fitStack();
}

void Ribbon::fitStack()
{
    const int index = m_stack->currentIndex();
    if (index < 0 || index >= m_pages.size()) return;
    // As tall as the page showing, and a scroll bar taller when the window is
    // too narrow for it -- so the bar has room of its own rather than
    // covering the captions. Every page is the same height, so this moves only
    // when a scroll bar comes or goes.
    const RibbonPage* page = m_pages.at(index);
    const auto* scroll = static_cast<const QScrollArea*>(m_stack->widget(index));
    const bool scrolls = page->minimumSizeHint().width() > width();
    const int height =
        page->minimumHeight() + (scrolls ? scroll->horizontalScrollBar()->sizeHint().height() : 0);
    if (m_stack->minimumHeight() != height || m_stack->maximumHeight() != height)
        m_stack->setFixedHeight(height);
}

} // namespace suspkin
