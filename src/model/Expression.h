#pragma once

#include <QString>

namespace suspkin {

/// Arithmetic in a coordinate field: + - * / and brackets over decimal numbers,
/// with the usual precedence and unary signs.
///
/// It exists so a coordinate can be *nudged*. The field opens holding what the
/// point is at, so typing "-0.5" after it is the whole gesture for moving that
/// point half a millimetre inboard. Nothing else about it is clever: there are
/// no variables, no functions and no units, because a hardpoint field is not a
/// spreadsheet, and half a typed name should read as a mistake rather than as
/// zero.
///
/// Numbers are read the way the user's own locale writes them -- "1.234,5"
/// where that is what their Excel produced -- and then the way C does, so a
/// field filled in from QLocale::toString() comes back as the number that went
/// into it whichever machine it was filled in on.
///
/// Returns false, leaving @p value untouched, for anything that is not a whole
/// expression: an empty field, a trailing operator, an unclosed bracket, a
/// stray letter, or a result that is not finite -- which is what a division by
/// zero produces.
bool evaluateExpression(const QString& text, double* value);

} // namespace suspkin
