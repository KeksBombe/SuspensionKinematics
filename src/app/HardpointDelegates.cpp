#include "app/HardpointDelegates.h"

#include "app/ExpressionSpinBox.h"
#include "app/HardpointModel.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QPainter>
#include <QSpinBox>

#include <iterator>
#include <optional>

namespace suspkin {
namespace {

/// Millimetres, comfortably past anything a vehicle-sized model reaches.
constexpr double kCoordinateLimit = 1.0e7;
constexpr int kEditDecimals = 4;

/// The pill's own padding, and the room the status dot needs beside a name.
constexpr int kChipPaddingX = 9;
constexpr int kChipPaddingY = 3;
constexpr int kDotDiameter = 7;
constexpr int kDotSpace = 16;

const QChar kEmDash(0x2014);

/// Everything here reads its colours out of the palette it is handed, which is
/// what makes the table follow the application between light and dark without
/// a second set of constants to keep in step.
bool isDark(const QPalette& palette)
{
    return palette.color(QPalette::Base).lightness() < 128;
}

struct ChipColors {
    QColor background;
    QColor border;
    QColor foreground;
};

/// The hue each constraint is read by. They are far enough apart to tell at a
/// glance and in the same family of saturation, so no one of them shouts.
int hueFor(PointType type)
{
    switch (type) {
    case PointType::ToBody: return 212;    // blue: pinned to the car
    case PointType::Solved: return 146;    // green: the solver works this one out
    case PointType::Dependent: return 32;  // amber: carried along by a body
    case PointType::Unassigned: return -1; // no colour, on purpose
    }
    return -1;
}

ChipColors chipColorsFor(PointType type, const QPalette& palette)
{
    const bool dark = isDark(palette);
    const int hue = hueFor(type);
    ChipColors colors;

    // Nothing has been decided about this point yet, and a colour would be
    // claiming otherwise.
    if (hue < 0) {
        const QColor text = palette.color(QPalette::Text);
        colors.background = QColor(text.red(), text.green(), text.blue(), dark ? 30 : 20);
        colors.border = QColor(text.red(), text.green(), text.blue(), dark ? 70 : 55);
        colors.foreground = palette.color(QPalette::Disabled, QPalette::Text);
        return colors;
    }

    if (dark) {
        colors.background = QColor::fromHsl(hue, 110, 58);
        colors.border = QColor::fromHsl(hue, 110, 92);
        colors.foreground = QColor::fromHsl(hue, 150, 200);
    } else {
        colors.background = QColor::fromHsl(hue, 165, 234);
        colors.border = QColor::fromHsl(hue, 130, 203);
        colors.foreground = QColor::fromHsl(hue, 145, 76);
    }
    return colors;
}

/// The row's worst issue, or nothing when there is none. An invalid variant is
/// read as "clean" rather than as level zero, which would be a warning.
std::optional<ConfigIssueLevel> issueLevelOf(const QModelIndex& index)
{
    const QVariant value = index.data(HardpointModel::IssueLevelRole);
    if (!value.isValid()) return std::nullopt;
    const int level = value.toInt();
    if (level < 0) return std::nullopt;
    return static_cast<ConfigIssueLevel>(level);
}

/// Draw this cell's text at the strength of something that is not there. Both
/// the ordinary and the selected colour, so a selected row does not undo it.
void mute(QPalette* palette)
{
    const QColor faded = palette->color(QPalette::Disabled, QPalette::Text);
    palette->setColor(QPalette::Text, faded);
    palette->setColor(QPalette::HighlightedText, faded);
}

QStyle* styleOf(const QStyleOptionViewItem& option)
{
    return option.widget ? option.widget->style() : QApplication::style();
}

/// A dropdown that commits as soon as something is picked. In a table, having
/// to pick and then press Enter reads as the click not having worked.
QComboBox* makeCommittingCombo(QWidget* parent, const QStyledItemDelegate* delegate)
{
    auto* combo = new QComboBox(parent);
    auto* self = const_cast<QStyledItemDelegate*>(delegate);
    QObject::connect(combo, &QComboBox::activated, self, [self, combo] {
        emit self->commitData(combo);
        emit self->closeEditor(combo);
    });
    return combo;
}

} // namespace

QColor issueColor(ConfigIssueLevel level, const QPalette& palette)
{
    const int hue = level == ConfigIssueLevel::Error ? 4 : 36;
    return isDark(palette) ? QColor::fromHsl(hue, 155, 148) : QColor::fromHsl(hue, 165, 118);
}

// ---------------------------------------------------------------------------
// Coordinates
// ---------------------------------------------------------------------------

QWidget* CoordinateDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                          const QModelIndex& index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);

    // Built here rather than asked for, because what a coordinate cell needs is
    // not the default spin box: the range has to reach a chassis datum, and
    // what is typed may be a sum -- "-2068.622-0.5" -- so a point can be nudged
    // from the table the same way it can from the viewport.
    auto* spin = new ExpressionSpinBox(parent);
    spin->setRange(-kCoordinateLimit, kCoordinateLimit);
    spin->setDecimals(kEditDecimals);
    return spin;
}

// ---------------------------------------------------------------------------
// The identifier column
// ---------------------------------------------------------------------------

void PointNameDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const
{
    const std::optional<ConfigIssueLevel> level = issueLevelOf(index);

    QStyleOptionViewItem opt = option;
    // Clear the room for the dot before the text is laid out, so a long name is
    // elided rather than drawn underneath it.
    if (level) opt.rect.adjust(0, 0, -kDotSpace, 0);
    QStyledItemDelegate::paint(painter, opt, index);
    if (!level) return;

    const QRect area(option.rect.right() - kDotSpace, option.rect.top(), kDotSpace,
                     option.rect.height());
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(issueColor(*level, option.palette));
    painter->drawEllipse(QPointF(area.center()) + QPointF(0.5, 0.5), kDotDiameter / 2.0,
                         kDotDiameter / 2.0);
    painter->restore();
}

QSize PointNameDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setWidth(size.width() + kDotSpace);
    return size;
}

// ---------------------------------------------------------------------------
// The constraint chip
// ---------------------------------------------------------------------------

void PointTypeDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const QString text = opt.text;
    opt.text.clear(); // the chip draws it, not the style

    QStyle* style = styleOf(opt);
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const auto type = static_cast<PointType>(index.data(HardpointModel::PointTypeRole).toInt());
    const ChipColors colors = chipColorsFor(type, opt.palette);

    const QRect content = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
    const QFontMetrics metrics(opt.font);
    const int height = qMin(content.height() - 2, metrics.height() + 2 * kChipPaddingY);
    const int width =
        qMin(content.width(), metrics.horizontalAdvance(text) + 2 * kChipPaddingX + 2);
    if (height <= 0 || width <= 0) return;

    const QRect chip(content.left(), content.center().y() - height / 2 + 1, width, height);
    const qreal radius = chip.height() / 2.0;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(colors.border, 1.0));
    painter->setBrush(colors.background);
    painter->drawRoundedRect(QRectF(chip).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    painter->setPen(colors.foreground);
    painter->setFont(opt.font);
    painter->drawText(chip, Qt::AlignCenter,
                      metrics.elidedText(text, Qt::ElideRight, chip.width() - kChipPaddingX));
    painter->restore();
}

QSize PointTypeDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setWidth(size.width() + 2 * kChipPaddingX);
    size.setHeight(size.height() + kChipPaddingY);
    return size;
}

QWidget* PointTypeDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                         const QModelIndex& index) const
{
    Q_UNUSED(option);
    QComboBox* combo = makeCommittingCombo(parent, this);
    const QStringList labels = index.data(HardpointModel::ChoicesRole).toStringList();
    // The label is what the user picks; the value stored is the type itself, so
    // the two can never drift apart through a reordered list.
    for (int i = 0; i < labels.size() && i < int(std::size(kPointTypes)); ++i)
        combo->addItem(labels.at(i), static_cast<int>(kPointTypes[i]));
    return combo;
}

void PointTypeDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto* combo = qobject_cast<QComboBox*>(editor);
    if (!combo) return;
    const int found = combo->findData(index.data(Qt::EditRole).toInt());
    combo->setCurrentIndex(found < 0 ? 0 : found);
}

void PointTypeDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                     const QModelIndex& index) const
{
    auto* combo = qobject_cast<QComboBox*>(editor);
    if (!combo) return;
    model->setData(index, combo->currentData().toInt(), Qt::EditRole);
}

// ---------------------------------------------------------------------------
// The body columns
// ---------------------------------------------------------------------------

void BodyDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                         const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    // An unnamed body is shown as a dash, and the dash is not information: it
    // should not read as loudly as a body that is actually named. Both roles,
    // or the dash would come back to full strength on the selected row.
    if (index.data(Qt::EditRole).toString().isEmpty()) mute(&opt.palette);
    QStyledItemDelegate::paint(painter, opt, index);
}

QWidget* BodyDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const
{
    Q_UNUSED(option);
    QComboBox* combo = makeCommittingCombo(parent, this);
    // Clearing a cell has to be reachable: a point may be described from one
    // side before the other is known.
    combo->addItem(QString(kEmDash), QString());
    for (const QString& body : index.data(HardpointModel::ChoicesRole).toStringList())
        combo->addItem(body, body);
    return combo;
}

void BodyDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto* combo = qobject_cast<QComboBox*>(editor);
    if (!combo) return;
    const QString body = index.data(Qt::EditRole).toString();
    const int found = combo->findData(body);
    // A body the template no longer describes is still shown, so the cell that
    // has to be corrected says what it currently holds.
    if (found < 0 && !body.isEmpty()) combo->addItem(body, body);
    combo->setCurrentIndex(found < 0 ? combo->count() - 1 : found);
}

void BodyDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                const QModelIndex& index) const
{
    auto* combo = qobject_cast<QComboBox*>(editor);
    if (!combo) return;
    model->setData(index, combo->currentData().toString(), Qt::EditRole);
}

// ---------------------------------------------------------------------------
// The bushing column
// ---------------------------------------------------------------------------

void BushingDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    if (index.data(Qt::EditRole).toInt() == kNoBushing) mute(&opt.palette);
    QStyledItemDelegate::paint(painter, opt, index);
}

QWidget* BushingDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                       const QModelIndex& index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    auto* spin = new QSpinBox(parent);
    spin->setRange(kNoBushing, kMaxBushingIndex);
    // The minimum is "no bushing", and reading it as a dash is the same thing
    // the cell says when it is not being edited.
    spin->setSpecialValueText(QString(kEmDash));
    spin->setAlignment(Qt::AlignCenter);
    spin->setKeyboardTracking(false);
    return spin;
}

} // namespace suspkin
