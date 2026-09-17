#include "model/Expression.h"

#include <QLocale>
#include <QTest>

using namespace suspkin;

namespace {

/// The value a refused expression must be left holding, so a caller that keeps
/// its own number can tell "not a sum" from "a sum that came to zero".
constexpr double kUntouched = -12345.678;

double evaluated(const QString& text)
{
    double value = kUntouched;
    return evaluateExpression(text, &value) ? value : kUntouched;
}

bool refused(const QString& text)
{
    double value = kUntouched;
    return !evaluateExpression(text, &value) && value == kUntouched;
}

} // namespace

class TestExpression : public QObject {
    Q_OBJECT

private slots:
    void init() { QLocale::setDefault(QLocale::c()); }

    void aPlainNumberIsItsOwnValue();
    void aCorrectionTypedOnTheEndIsSubtracted();
    void multiplicationBindsTighterThanAddition();
    void bracketsAndSignsNest();
    void anExponentIsPartOfTheNumber();
    void theUsersOwnDecimalSeparatorIsRead();
    void halfTypedInputIsRefused();
    void aResultThatIsNotFiniteIsRefused();
};

void TestExpression::aPlainNumberIsItsOwnValue()
{
    QCOMPARE(evaluated(QStringLiteral("271.5")), 271.5);
    QCOMPARE(evaluated(QStringLiteral("-2068.622")), -2068.622);
    QCOMPARE(evaluated(QStringLiteral("  0 ")), 0.0);
}

void TestExpression::aCorrectionTypedOnTheEndIsSubtracted()
{
    // The gesture the whole thing exists for: the field opens holding what the
    // point is at, and the correction is typed onto the end of it.
    QCOMPARE(evaluated(QStringLiteral("271.5-0.5")), 271.0);
    QCOMPARE(evaluated(QStringLiteral("271.5 - 0.5")), 271.0);
    QCOMPARE(evaluated(QStringLiteral("-2068.622 + 12")), -2056.622);
}

void TestExpression::multiplicationBindsTighterThanAddition()
{
    QCOMPARE(evaluated(QStringLiteral("2+3*4")), 14.0);
    QCOMPARE(evaluated(QStringLiteral("10-8/2")), 6.0);
}

void TestExpression::bracketsAndSignsNest()
{
    QCOMPARE(evaluated(QStringLiteral("(2+3)*4")), 20.0);
    QCOMPARE(evaluated(QStringLiteral("-(2+3)")), -5.0);
    QCOMPARE(evaluated(QStringLiteral("--5")), 5.0);
    QCOMPARE(evaluated(QStringLiteral("3*-2")), -6.0);
}

void TestExpression::anExponentIsPartOfTheNumber()
{
    QCOMPARE(evaluated(QStringLiteral("1e3")), 1000.0);
    QCOMPARE(evaluated(QStringLiteral("1.5e-2")), 0.015);
    // An exponent with nothing after it is a typo, and the 'e' is then a letter
    // in a coordinate field.
    QVERIFY(refused(QStringLiteral("1e")));
}

void TestExpression::theUsersOwnDecimalSeparatorIsRead()
{
    // What a German Excel wrote, in a German locale -- group separators and all.
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    QCOMPARE(evaluated(QStringLiteral("271,5")), 271.5);
    QCOMPARE(evaluated(QStringLiteral("1.234,5")), 1234.5);
    // And a full stop still means a decimal point there: it is what every
    // number in this program's own files is written with.
    QCOMPARE(evaluated(QStringLiteral("271.5")), 271.5);

    // The other way round: a comma is a decimal point in a C locale too, since
    // nothing else in this grammar can mean one.
    QLocale::setDefault(QLocale::c());
    QCOMPARE(evaluated(QStringLiteral("271,5")), 271.5);
}

void TestExpression::halfTypedInputIsRefused()
{
    QVERIFY(refused(QString()));
    QVERIFY(refused(QStringLiteral("   ")));
    QVERIFY(refused(QStringLiteral("271.5-")));
    QVERIFY(refused(QStringLiteral("(2+3")));
    QVERIFY(refused(QStringLiteral("2+3)")));
    QVERIFY(refused(QStringLiteral("1..2")));
    QVERIFY(refused(QStringLiteral("271 5"))); // two numbers are not one answer
    QVERIFY(refused(QStringLiteral("abc")));
    QVERIFY(refused(QStringLiteral("271.5 mm")));
}

void TestExpression::aResultThatIsNotFiniteIsRefused()
{
    QVERIFY(refused(QStringLiteral("1/0")));
    QVERIFY(refused(QStringLiteral("0/0")));
}

QTEST_APPLESS_MAIN(TestExpression)
#include "test_expression.moc"
