#include "app/ExpressionSpinBox.h"

#include "model/Expression.h"

namespace suspkin {
namespace {

/// Everything a sum can be built out of. Nothing else is worth letting into the
/// field: a letter in a coordinate is a typo whatever comes after it.
bool isExpressionChar(QChar c)
{
    static const QString allowed = QStringLiteral("0123456789.,+-*/() eE");
    return allowed.contains(c);
}

bool couldBecomeExpression(const QString& text)
{
    for (const QChar c : text)
        if (!isExpressionChar(c)) return false;
    return true;
}

} // namespace

ExpressionSpinBox::ExpressionSpinBox(QWidget* parent) : QDoubleSpinBox(parent)
{
    // Without this the value would follow every keystroke, so "12-0.5" would be
    // read as 12 on its way to being typed and the point would jump there.
    setKeyboardTracking(false);
    setAccelerated(true);
}

QString ExpressionSpinBox::withoutAffixes(const QString& text) const
{
    QString bare = text;
    if (!suffix().isEmpty() && bare.endsWith(suffix())) bare.chop(suffix().size());
    if (!prefix().isEmpty() && bare.startsWith(prefix())) bare = bare.mid(prefix().size());
    return bare;
}

QValidator::State ExpressionSpinBox::validate(QString& input, int& pos) const
{
    const QString sum = withoutAffixes(input);

    double value = 0.0;
    if (evaluateExpression(sum, &value)) {
        // Out of range is still on its way somewhere: "-20" is a legitimate
        // prefix of "-2068.622". Clamping is interpretText()'s job, not this.
        return (value >= minimum() && value <= maximum()) ? QValidator::Acceptable
                                                          : QValidator::Intermediate;
    }
    if (couldBecomeExpression(sum)) return QValidator::Intermediate;

    return QDoubleSpinBox::validate(input, pos);
}

double ExpressionSpinBox::valueFromText(const QString& text) const
{
    double value = 0.0;
    if (evaluateExpression(withoutAffixes(text), &value)) return value;

    // Not a sum: let the spin box read it the way it always has, which is what
    // keeps a plain number in an unusual locale behaving exactly as before.
    return QDoubleSpinBox::valueFromText(text);
}

} // namespace suspkin
