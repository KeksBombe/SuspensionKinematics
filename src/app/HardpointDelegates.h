#pragma once

#include "model/HardpointConfig.h"

#include <QColor>
#include <QStyledItemDelegate>

namespace suspkin {

/// The colour a warning or an error is drawn in, out of @p palette so it holds
/// up in both themes. Shared so the dot on a row and the line under the table
/// cannot disagree about what "needs attention" looks like.
QColor issueColor(ConfigIssueLevel level, const QPalette& palette);

/// The spin box a plain item delegate builds runs 0 to 99.99 with two decimals,
/// which would silently clamp a coordinate like -2068.622 to 0 the first time
/// anybody edited it. This is the whole reason the delegate exists.
class CoordinateDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
};

/// The identifier column: the name, and a dot when the row has something wrong
/// with it.
///
/// The dot is drawn rather than coloured into the cell on purpose. A row that is
/// half-configured is extremely common on the way to a finished car, and a wall
/// of tinted cells would say "everything is broken" when what is meant is "this
/// one is not finished yet".
class PointNameDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

/// The solver constraint, as a pill badge.
///
/// Colour carries the meaning here -- blue is held still, green is solved for,
/// amber is carried along -- but only inside the badge, so a table of forty
/// points reads as a list rather than as a heat map.
class PointTypeDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override;
};

/// A body column: a dropdown of exactly the bodies this project's linkage
/// describes, which the model hands over through its ChoicesRole.
///
/// Restricting the editor to the catalogue is the first half of the validation
/// boundary -- it makes most bad input unreachable rather than rejected -- and
/// the model's own check is the half that cannot be got round.
class BodyDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override;
};

/// The bushing index: a spin box whose zero reads as "rigid" rather than as 0.
class BushingDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
};

} // namespace suspkin
