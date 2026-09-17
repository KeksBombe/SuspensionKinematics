#pragma once

#include <QFrame>
#include <QPointF>
#include <QString>

class QLabel;
class QLineEdit;

namespace suspkin {

/// The little field that opens over the viewport when X, Y or Z is pressed with
/// a hardpoint selected.
///
/// It holds one coordinate of one point, ready to be typed over or added to:
/// the value is already in it with the cursor at the end, so "-0.5" typed on
/// the end of it is the whole gesture for moving that point half a millimetre.
/// What may be typed is a sum (evaluateExpression()), and what that sum comes
/// to is shown under the field before Enter is pressed -- the arithmetic is
/// visible, not something the user has to trust.
///
/// It is a view, not a store: Enter reports the number and the window writes it
/// into the table through the same path the table's own cells use. Escape, or
/// clicking away, closes it and changes nothing.
class CoordinateEntry : public QFrame {
    Q_OBJECT

public:
    explicit CoordinateEntry(QWidget* parent);

    /// Open on @p axis of @p pointName, holding @p value, beside @p anchor --
    /// the marker's own position in the parent's coordinates.
    void openAt(const QPointF& anchor, const QString& pointName, int axis, double value);
    /// Close without reporting anything.
    void dismiss();

signals:
    /// Enter was pressed on a field that reads as a number: @p value is what it
    /// came to, in millimetres, for the axis the entry was opened on.
    void committed(int axis, double value);
    /// The entry closed, however it closed. The viewport takes the keyboard
    /// back on this, so the next X, Y or Z lands where the first one did.
    void closed();

protected:
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Colours out of the palette, so the box follows the application between
    /// light and dark like everything else in the window.
    void applyTheme();
    /// Show what the field comes to, or why it comes to nothing.
    void showEvaluation();
    /// Report the value and close. Does neither when the field is not a sum.
    void commit();
    /// Put the box beside @p anchor without letting it off the edge.
    void placeNear(const QPointF& anchor);

    int m_axis = 0;
    /// Set while the colours are being written on. Style sheets change the
    /// palette, which comes back as another palette change: the same guard the
    /// hardpoint panel has, and for the same reason.
    bool m_theming = false;
    QLabel* m_caption = nullptr;
    QLineEdit* m_edit = nullptr;
    QLabel* m_result = nullptr;
};

} // namespace suspkin
