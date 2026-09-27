#include "model/Mechanism.h"

#include <QCoreApplication>

#include <array>

namespace suspkin {
namespace {

const QString kCornerToken = QStringLiteral("{corner}");

/// One name with its corner filled in and, for the far side, put through the
/// mirror rule. Empty in stays empty out, so an optional role nobody named does
/// not turn into a mirrored empty string.
QString instantiateName(const QString& pattern, const QString& corner, bool mirrored,
                        const MirrorSpec& mirror)
{
    if (pattern.isEmpty()) return QString();
    QString name = pattern;
    name.replace(kCornerToken, corner);
    if (!mirrored) return name;
    return mirroredName(name, mirror);
}

void appendIfNamed(QStringList& list, const QString& name)
{
    if (!name.isEmpty()) list.append(name);
}

using Role = QString MechanismTemplate::*;

/// Every role that names one point of its own, in the order the fields are
/// declared, so that listing them and filling them in cannot disagree about
/// which there are. Not the steering rack, which names a point another role
/// already names, and not the carried points, which are a list.
const std::array<Role, 21>& namedRoles()
{
    static const std::array<Role, 21> roles = {
        &MechanismTemplate::lowerFront,     &MechanismTemplate::lowerRear,
        &MechanismTemplate::lowerOuter,     &MechanismTemplate::upperFront,
        &MechanismTemplate::upperRear,      &MechanismTemplate::upperOuter,
        &MechanismTemplate::tieRodInboard,  &MechanismTemplate::tieRodOutboard,
        &MechanismTemplate::wheelCenter,    &MechanismTemplate::wheelAxis,
        &MechanismTemplate::contactPatch,   &MechanismTemplate::pushrodOuter,
        &MechanismTemplate::pushrodInner,   &MechanismTemplate::rockerPivot,
        &MechanismTemplate::rockerAxis,     &MechanismTemplate::damperInboard,
        &MechanismTemplate::damperOutboard, &MechanismTemplate::antiRollDropLinkOuter,
        &MechanismTemplate::antiRollArmEnd, &MechanismTemplate::antiRollArmRoot,
        &MechanismTemplate::antiRollBearing,
    };
    return roles;
}

/// @p templ with every name put through @p fill, and everything that is not a
/// name carried across as it is.
template <typename Fill>
MechanismTemplate instantiateWith(const MechanismTemplate& templ, Fill fill)
{
    MechanismTemplate out;
    out.pushrodMount = templ.pushrodMount;
    out.antiRollMount = templ.antiRollMount;
    out.formerNames = templ.formerNames;

    for (Role role : namedRoles()) out.*role = fill(templ.*role);
    // The rack point is a reference to a point the mechanism already names, not
    // a point of its own, which is why it is instantiated here but stays out of
    // allNames() and out of the coverage report -- counting it would report one
    // hardpoint twice.
    out.steeringRack = fill(templ.steeringRack);
    for (const QString& name : templ.carried) appendIfNamed(out.carried, fill(name));
    return out;
}

} // namespace

QString pushrodMountToString(PushrodMount mount)
{
    switch (mount) {
    case PushrodMount::UpperArm: return QStringLiteral("upperArm");
    case PushrodMount::LowerArm: return QStringLiteral("lowerArm");
    case PushrodMount::Upright: return QStringLiteral("upright");
    }
    return QStringLiteral("upperArm");
}

PushrodMount pushrodMountFromString(const QString& text, PushrodMount fallback)
{
    const QString key = text.trimmed().toLower();
    if (key == QLatin1String("upperarm") || key == QLatin1String("upper")
        || key == QLatin1String("upperwishbone"))
        return PushrodMount::UpperArm;
    if (key == QLatin1String("lowerarm") || key == QLatin1String("lower")
        || key == QLatin1String("lowerwishbone"))
        return PushrodMount::LowerArm;
    if (key == QLatin1String("upright") || key == QLatin1String("knuckle"))
        return PushrodMount::Upright;
    return fallback;
}

bool MechanismTemplate::isEmpty() const
{
    return lowerFront.isEmpty() || lowerRear.isEmpty() || lowerOuter.isEmpty()
           || upperFront.isEmpty() || upperRear.isEmpty() || upperOuter.isEmpty();
}

bool MechanismTemplate::hasRocker() const
{
    return !pushrodOuter.isEmpty() && !pushrodInner.isEmpty() && !rockerPivot.isEmpty()
           && !rockerAxis.isEmpty();
}

QString dropLinkMountToString(DropLinkMount mount)
{
    switch (mount) {
    case DropLinkMount::Rocker: return QStringLiteral("rocker");
    case DropLinkMount::UpperArm: return QStringLiteral("upperArm");
    case DropLinkMount::LowerArm: return QStringLiteral("lowerArm");
    case DropLinkMount::Upright: return QStringLiteral("upright");
    }
    return QStringLiteral("rocker");
}

DropLinkMount dropLinkMountFromString(const QString& text, DropLinkMount fallback)
{
    // The wishbones and the upright are spelled the way a pushrod mount is, so
    // one word means one body wherever it is written.
    const QString key = text.trimmed().toLower();
    if (key == QLatin1String("rocker") || key == QLatin1String("bellcrank"))
        return DropLinkMount::Rocker;
    if (key == QLatin1String("upperarm") || key == QLatin1String("upper")
        || key == QLatin1String("upperwishbone"))
        return DropLinkMount::UpperArm;
    if (key == QLatin1String("lowerarm") || key == QLatin1String("lower")
        || key == QLatin1String("lowerwishbone"))
        return DropLinkMount::LowerArm;
    if (key == QLatin1String("upright") || key == QLatin1String("knuckle"))
        return DropLinkMount::Upright;
    return fallback;
}

bool MechanismTemplate::hasAntiRoll() const
{
    // A drop link on the rocker needs a rocker to be on; one on the wheel's own
    // members does not.
    const bool mounted = antiRollMount != DropLinkMount::Rocker || hasRocker();
    return mounted && !antiRollDropLinkOuter.isEmpty() && !antiRollArmEnd.isEmpty()
           && !antiRollArmRoot.isEmpty();
}

QStringList MechanismTemplate::allNames() const
{
    QStringList names;
    for (Role role : namedRoles()) appendIfNamed(names, this->*role);
    names += carried;
    return names;
}

MechanismTemplate instantiateMechanism(const MechanismTemplate& templ, const QString& cornerToken,
                                       bool mirrored, const MirrorSpec& mirror)
{
    return instantiateWith(templ, [&](const QString& pattern) {
        return instantiateName(pattern, cornerToken, mirrored, mirror);
    });
}

MechanismTemplate instantiateMechanism(const MechanismTemplate& templ, const QString& cornerToken,
                                       bool mirrored, const MirrorSpec& mirror,
                                       const HardpointTable& table)
{
    return instantiateWith(templ, [&](const QString& pattern) {
        return resolvePointName(pattern, cornerToken, mirrored, mirror, templ.formerNames, table);
    });
}

QString resolvePointName(const QString& pattern, const QString& cornerToken, bool mirrored,
                         const MirrorSpec& mirror, const FormerNames& formerNames,
                         const HardpointTable& table)
{
    const QString name = instantiateName(pattern, cornerToken, mirrored, mirror);
    if (name.isEmpty() || table.indexOf(name) >= 0) return name;

    const QString former = formerNames.value(pattern);
    if (former.isEmpty()) return name;
    const QString formerName = instantiateName(former, cornerToken, mirrored, mirror);
    // A point in neither spelling is reported under the name it has now, which
    // is the one the user should give it.
    return !formerName.isEmpty() && table.indexOf(formerName) >= 0 ? formerName : name;
}

MechanismCoverage coverMechanism(const MechanismTemplate& mechanism, const HardpointTable& table)
{
    MechanismCoverage coverage;

    int found = 0;
    const auto check = [&](const QString& name, QStringList& missing) {
        if (name.isEmpty()) return false;
        if (table.indexOf(name) >= 0) {
            ++found;
            return true;
        }
        missing.append(name);
        return false;
    };

    // The wishbones and the upright are what a solve is; without all of them
    // there is no mechanism, only a scattering of points.
    for (const QString* name : { &mechanism.lowerFront, &mechanism.lowerRear, &mechanism.lowerOuter,
                                 &mechanism.upperFront, &mechanism.upperRear, &mechanism.upperOuter,
                                 &mechanism.tieRodInboard, &mechanism.tieRodOutboard,
                                 &mechanism.wheelCenter })
        check(*name, coverage.missingRequired);

    for (const QString* name :
         { &mechanism.wheelAxis, &mechanism.contactPatch, &mechanism.pushrodOuter,
           &mechanism.pushrodInner, &mechanism.rockerPivot, &mechanism.rockerAxis,
           &mechanism.damperInboard, &mechanism.damperOutboard, &mechanism.antiRollDropLinkOuter,
           &mechanism.antiRollArmEnd, &mechanism.antiRollArmRoot, &mechanism.antiRollBearing })
        check(*name, coverage.missingOptional);

    for (const QString& name : mechanism.carried) check(name, coverage.missingOptional);

    // Not one point of this corner is in the table. A workbook may hold one
    // axle, or may not have been mirrored yet; saying so would be noise, and
    // the noise would bury the corners that really are half missing.
    if (found == 0) {
        coverage.absent = true;
        coverage.missingRequired.clear();
        coverage.missingOptional.clear();
    }
    return coverage;
}

} // namespace suspkin
