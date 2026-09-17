#pragma once

#include <QDoubleSpinBox>

namespace suspkin {

/// A coordinate field that does arithmetic.
///
/// It is an ordinary spin box in every other way -- same value, same range, same
/// arrows -- but what is typed into it may be a sum: "-2068.622 - 0.5" moves a
/// point half a millimetre without the user working out what that comes to.
/// That is the whole reason it exists, so the field opens holding the current
/// value with the cursor at the end of it and a correction can simply be typed
/// on.
///
/// The arithmetic is evaluateExpression()'s, which is in the core and tested
/// there; all this adds is the two hooks a spin box offers for reading its own
/// text. Half-typed input -- "1 +", "(2" -- is Intermediate rather than
/// Invalid, because a field that refuses the keystroke before the second
/// operand cannot be typed into at all.
class ExpressionSpinBox : public QDoubleSpinBox {
    Q_OBJECT

public:
    explicit ExpressionSpinBox(QWidget* parent = nullptr);

    QValidator::State validate(QString& input, int& pos) const override;
    double valueFromText(const QString& text) const override;

private:
    /// @p text without the prefix and suffix the spin box adds -- " mm" on the
    /// new-point dialog's fields. They are the spin box's own decoration, and
    /// the arithmetic must not have to know about them.
    QString withoutAffixes(const QString& text) const;
};

} // namespace suspkin
