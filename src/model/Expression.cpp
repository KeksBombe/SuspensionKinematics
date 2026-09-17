#include "model/Expression.h"

#include <QLocale>

#include <cmath>

namespace suspkin {
namespace {

bool isDigit(QChar c)
{
    return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

/// Part of a number as somebody might write one: its digits, and either
/// separator, since a locale may use each of them for either job.
bool isNumberChar(QChar c)
{
    return isDigit(c) || c == QLatin1Char('.') || c == QLatin1Char(',');
}

/// One number, read the user's way first and C's way second.
///
/// The two are tried in that order because the field is normally filled in from
/// QLocale::toString(), so what is in it is already the user's convention --
/// and because "1.5" is 1.5 to everyone, while a German locale would have to
/// call it badly grouped and refuse it.
bool parseDecimal(const QString& text, double* value)
{
    bool ok = false;
    double parsed = QLocale().toDouble(text, &ok);
    if (!ok) parsed = QLocale::c().toDouble(text, &ok);
    // A comma with no full stop beside it is a decimal point whatever the
    // locale says: nothing else in this grammar can mean it.
    if (!ok && text.contains(QLatin1Char(',')) && !text.contains(QLatin1Char('.'))) {
        QString dotted = text;
        dotted.replace(QLatin1Char(','), QLatin1Char('.'));
        parsed = QLocale::c().toDouble(dotted, &ok);
    }
    if (!ok) return false;

    *value = parsed;
    return true;
}

/// A recursive-descent pass over one field's worth of text.
///
/// Each level knows its own operators and nothing else's, so precedence is the
/// shape of the call chain rather than a table that has to be kept in step with
/// the code that reads it.
class Parser {
public:
    explicit Parser(const QString& text) : m_text(text) {}

    /// The whole field: one expression, and then the end of it.
    bool parseField(double* value)
    {
        double parsed = 0.0;
        if (!parseSum(&parsed)) return false;
        skipSpace();
        if (!atEnd()) return false; // trailing junk: "1 2", "3)" -- not an answer
        if (!std::isfinite(parsed)) return false;

        *value = parsed;
        return true;
    }

private:
    bool parseSum(double* value)
    {
        double left = 0.0;
        if (!parseProduct(&left)) return false;

        while (true) {
            skipSpace();
            const bool adding = take(QLatin1Char('+'));
            if (!adding && !take(QLatin1Char('-'))) break;

            double right = 0.0;
            if (!parseProduct(&right)) return false;
            left = adding ? left + right : left - right;
        }

        *value = left;
        return true;
    }

    bool parseProduct(double* value)
    {
        double left = 0.0;
        if (!parseSigned(&left)) return false;

        while (true) {
            skipSpace();
            const bool multiplying = take(QLatin1Char('*'));
            if (!multiplying && !take(QLatin1Char('/'))) break;

            double right = 0.0;
            if (!parseSigned(&right)) return false;
            left = multiplying ? left * right : left / right;
        }

        *value = left;
        return true;
    }

    bool parseSigned(double* value)
    {
        skipSpace();
        if (take(QLatin1Char('-'))) {
            double inner = 0.0;
            if (!parseSigned(&inner)) return false;
            *value = -inner;
            return true;
        }
        if (take(QLatin1Char('+'))) return parseSigned(value);
        return parseAtom(value);
    }

    bool parseAtom(double* value)
    {
        skipSpace();
        if (take(QLatin1Char('('))) {
            double inner = 0.0;
            if (!parseSum(&inner)) return false;
            skipSpace();
            if (!take(QLatin1Char(')'))) return false;
            *value = inner;
            return true;
        }
        return parseNumber(value);
    }

    bool parseNumber(double* value)
    {
        const int start = m_pos;
        while (!atEnd() && isNumberChar(peek())) ++m_pos;
        if (m_pos == start) return false;
        takeExponent();
        return parseDecimal(m_text.mid(start, m_pos - start), value);
    }

    /// An exponent, and only a complete one: "1e" is a typo rather than a
    /// number, and leaving the 'e' where it is makes the field say so.
    void takeExponent()
    {
        if (atEnd() || (peek() != QLatin1Char('e') && peek() != QLatin1Char('E'))) return;

        int ahead = m_pos + 1;
        if (ahead < m_text.size()
            && (m_text[ahead] == QLatin1Char('+') || m_text[ahead] == QLatin1Char('-')))
            ++ahead;
        if (ahead >= m_text.size() || !isDigit(m_text[ahead])) return;

        m_pos = ahead;
        while (!atEnd() && isDigit(peek())) ++m_pos;
    }

    void skipSpace()
    {
        while (!atEnd() && peek().isSpace()) ++m_pos;
    }

    bool atEnd() const { return m_pos >= m_text.size(); }
    QChar peek() const { return m_text[m_pos]; }

    /// Consume @p c when that is what comes next. Nothing here backtracks
    /// further than one character, which is all this grammar ever needs.
    bool take(QChar c)
    {
        if (atEnd() || peek() != c) return false;
        ++m_pos;
        return true;
    }

    QString m_text;
    int m_pos = 0;
};

} // namespace

bool evaluateExpression(const QString& text, double* value)
{
    return Parser(text).parseField(value);
}

} // namespace suspkin
