#include "model/Hardpoint.h"

#include <QCoreApplication>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("Hardpoint", text); }

/// The workbook reader splits a coordinate row's name at its last two
/// characters -- one of these separators, then x, y or z -- so a point whose
/// own name ends that way is one character away from being read as somebody
/// else's coordinate.
bool endsLikeACoordinate(const QString& name)
{
    if (name.size() < 2) return false;
    const QChar separator = name.at(name.size() - 2);
    if (separator != u'_' && separator != u'.' && separator != u'-') return false;
    const char16_t axis = name.at(name.size() - 1).toLower().unicode();
    return axis == u'x' || axis == u'y' || axis == u'z';
}

} // namespace

QString hardpointNameProblem(const QString& name, const HardpointTable& table, int ignoreRow)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return tr("A point needs a name.");

    // The reader trims every name it reads, so a space at either end would come
    // back as a different point from the one that was written.
    if (trimmed != name)
        return tr("A name cannot start or end with a space: the workbook reads it back without "
                  "one.");

    for (const QChar ch : name) {
        if (ch.category() == QChar::Other_Control)
            return tr("A name cannot contain a line break or a tab.");
    }

    if (endsLikeACoordinate(name)) {
        return tr("\"%1\" ends in %2, which is how the workbook marks a coordinate: F_LCA_O_x is "
                  "the x of F_LCA_O.")
            .arg(name, name.right(2));
    }

    const int existing = table.indexOf(name);
    if (existing >= 0 && existing != ignoreRow)
        return tr("There is already a point called \"%1\".").arg(name);

    return QString();
}

} // namespace suspkin
