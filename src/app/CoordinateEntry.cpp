#include "app/CoordinateEntry.h"

#include "app/HardpointDelegates.h"
#include "model/Expression.h"
#include "render/MoveGizmo.h"

#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QVBoxLayout>

#include <algorithm>

namespace suspkin {
namespace {

constexpr int kBoxWidth = 208;
/// How far the box sits from the marker, so the hub and the arrows stay visible.
constexpr int kAnchorGap = 18;
/// What the answer is shown to, the same as the table's own columns.
constexpr int kResultDecimals = 3;

/// @p value written out so that reading it back gives exactly @p value again.
///
/// The field is filled in with this, so a coordinate the user opens and leaves
/// alone is written back as the number that was already there. Formatting it to
/// a fixed number of decimals would quietly round a point nobody moved, and
/// that would turn up in the workbook as an edit the user never made.
QString roundTripText(double value)
{
    for (int digits = 1; digits <= 15; ++digits) {
        const QString candidate = QLocale().toString(value, 'g', digits);
        double back = 0.0;
        if (evaluateExpression(candidate, &back) && back == value) return candidate;
    }
    return QLocale().toString(value, 'g', 17);
}

} // namespace

CoordinateEntry::CoordinateEntry(QWidget* parent) : QFrame(parent)
{
    setAutoFillBackground(true);
    setFixedWidth(kBoxWidth);
    hide();

    m_caption = new QLabel(this);
    m_edit = new QLineEdit(this);
    m_result = new QLabel(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(4);
    layout->addWidget(m_caption);
    layout->addWidget(m_edit);
    layout->addWidget(m_result);

    connect(m_edit, &QLineEdit::textChanged, this, [this] { showEvaluation(); });
    connect(m_edit, &QLineEdit::returnPressed, this, [this] { commit(); });
    // Clicking anywhere else is the other way out of it, and means the same as
    // Escape: the point stays where it is.
    m_edit->installEventFilter(this);

    applyTheme();
}

void CoordinateEntry::openAt(const QPointF& anchor, const QString& pointName, int axis,
                             double value)
{
    m_axis = axis;
    m_caption->setText(tr("%1  ·  %2").arg(pointName, MoveGizmo::axisLabel(axis)));

    // Filled in and not selected: the point of the field is that a correction
    // can be typed straight onto the end of what is already there.
    m_edit->setText(roundTripText(value));
    m_edit->setCursorPosition(m_edit->text().size());

    applyTheme(); // the caption carries the axis colour, which has just changed
    placeNear(anchor);
    show();
    raise();
    m_edit->setFocus(Qt::OtherFocusReason);
}

void CoordinateEntry::dismiss()
{
    if (!isVisible()) return;
    hide();
    emit closed();
}

void CoordinateEntry::commit()
{
    double value = 0.0;
    if (!evaluateExpression(m_edit->text(), &value)) return; // the field already says why

    hide();
    emit committed(m_axis, value);
    emit closed();
}

void CoordinateEntry::showEvaluation()
{
    double value = 0.0;
    const bool readable = evaluateExpression(m_edit->text(), &value);
    m_result->setText(readable ? tr("= %1 mm").arg(QLocale().toString(value, 'f', kResultDecimals))
                               : tr("Not a number yet"));

    const QColor colour = readable ? palette().color(QPalette::WindowText)
                                   : issueColor(ConfigIssueLevel::Error, palette());
    m_result->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(colour.name()));
}

void CoordinateEntry::placeNear(const QPointF& anchor)
{
    if (!parentWidget()) return;

    const QSize size = sizeHint();
    const QRect room = parentWidget()->rect();
    const int x = static_cast<int>(anchor.x()) + kAnchorGap;
    const int y = static_cast<int>(anchor.y()) + kAnchorGap;
    move(std::clamp(x, room.left() + 4, std::max(room.left() + 4, room.right() - size.width() - 4)),
         std::clamp(y, room.top() + 4, std::max(room.top() + 4, room.bottom() - size.height() - 4)));
    resize(size);
}

void CoordinateEntry::applyTheme()
{
    m_theming = true;
    const QPalette pal = palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor text = pal.color(QPalette::WindowText);
    // Mixed rather than fixed, the same rule the table and the plots follow: a
    // line the panel's own background can always be told apart from.
    const QColor line = QColor::fromRgbF(0.5 * window.redF() + 0.5 * text.redF(),
                                         0.5 * window.greenF() + 0.5 * text.greenF(),
                                         0.5 * window.blueF() + 0.5 * text.blueF());

    setStyleSheet(QStringLiteral("QFrame { background-color: %1; border: 1px solid %2;"
                                 " border-radius: 6px; }"
                                 "QLineEdit { border: 1px solid %2; border-radius: 4px;"
                                 " padding: 4px 6px; }")
                      .arg(window.name(), line.name()));

    QFont small = font();
    small.setPointSizeF(std::max(7.5, small.pointSizeF() - 1.0));
    m_caption->setFont(small);
    m_result->setFont(small);
    // The caption carries the arrow's own colour, which is what ties the field
    // to the arm it is about -- darkened on a light background, where the green
    // of Y would otherwise be too pale to read.
    QColor axis = MoveGizmo::axisColor(m_axis);
    if (window.lightness() >= 128) axis = axis.darker(150);
    m_caption->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(axis.name()));
    // The result's own colour says whether the field reads as a number, so it
    // is written by the one place that knows.
    showEvaluation();
    m_theming = false;
}

void CoordinateEntry::changeEvent(QEvent* event)
{
    QFrame::changeEvent(event);
    if (m_theming) return;
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::ThemeChange)
        applyTheme();
}

void CoordinateEntry::keyPressEvent(QKeyEvent* event)
{
    // The line edit does not use Escape, so it arrives here.
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

bool CoordinateEntry::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_edit && event->type() == QEvent::FocusOut) dismiss();
    return QFrame::eventFilter(watched, event);
}

} // namespace suspkin
