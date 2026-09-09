#include "model/Mechanism.h"

#include <QCoreApplication>

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

bool MechanismTemplate::hasAntiRoll() const
{
    return hasRocker() && !antiRollRocker.isEmpty() && !antiRollArmOuter.isEmpty()
           && !antiRollArmPivot.isEmpty();
}

QStringList MechanismTemplate::allNames() const
{
    QStringList names;
    for (const QString* name : { &lowerFront, &lowerRear, &lowerOuter, &upperFront, &upperRear,
                                 &upperOuter, &tieRodInboard, &tieRodOutboard, &wheelCenter,
                                 &wheelAxis, &contactPatch, &pushrodOuter, &pushrodInner,
                                 &rockerPivot, &rockerAxis, &damperInboard, &damperOutboard,
                                 &antiRollRocker, &antiRollArmOuter, &antiRollArmPivot })
        appendIfNamed(names, *name);
    names += carried;
    return names;
}

MechanismTemplate instantiateMechanism(const MechanismTemplate& templ, const QString& cornerToken,
                                       bool mirrored, const MirrorSpec& mirror)
{
    MechanismTemplate out;
    out.pushrodMount = templ.pushrodMount;

    const auto fill = [&](const QString& pattern) {
        return instantiateName(pattern, cornerToken, mirrored, mirror);
    };

    out.lowerFront = fill(templ.lowerFront);
    out.lowerRear = fill(templ.lowerRear);
    out.lowerOuter = fill(templ.lowerOuter);
    out.upperFront = fill(templ.upperFront);
    out.upperRear = fill(templ.upperRear);
    out.upperOuter = fill(templ.upperOuter);
    out.tieRodInboard = fill(templ.tieRodInboard);
    out.tieRodOutboard = fill(templ.tieRodOutboard);
    // The rack point is a reference to a point the mechanism already names, not
    // a point of its own, which is why it is instantiated here but stays out of
    // allNames() and out of the coverage report -- counting it would report one
    // hardpoint twice.
    out.steeringRack = fill(templ.steeringRack);
    out.wheelCenter = fill(templ.wheelCenter);
    out.wheelAxis = fill(templ.wheelAxis);
    out.contactPatch = fill(templ.contactPatch);
    out.pushrodOuter = fill(templ.pushrodOuter);
    out.pushrodInner = fill(templ.pushrodInner);
    out.rockerPivot = fill(templ.rockerPivot);
    out.rockerAxis = fill(templ.rockerAxis);
    out.damperInboard = fill(templ.damperInboard);
    out.damperOutboard = fill(templ.damperOutboard);
    out.antiRollRocker = fill(templ.antiRollRocker);
    out.antiRollArmOuter = fill(templ.antiRollArmOuter);
    out.antiRollArmPivot = fill(templ.antiRollArmPivot);
    for (const QString& name : templ.carried) appendIfNamed(out.carried, fill(name));

    return out;
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
           &mechanism.damperInboard, &mechanism.damperOutboard, &mechanism.antiRollRocker,
           &mechanism.antiRollArmOuter, &mechanism.antiRollArmPivot })
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
