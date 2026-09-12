#include "model/DesignParameters.h"

#include <QCoreApplication>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("DesignParameters", text); }

struct PivotName {
    DesignPivot pivot;
    const char* text;
};

constexpr PivotName kPivotNames[] = {
    { DesignPivot::LowerFront, "lowerFront" },
    { DesignPivot::LowerRear, "lowerRear" },
    { DesignPivot::UpperFront, "upperFront" },
    { DesignPivot::UpperRear, "upperRear" },
};

/// One number per key, named the way the struct names it, so a manifest can be
/// read against the header without a table in between.
struct AxleField {
    const char* key;
    double AxleDesign::*member;
};

constexpr AxleField kAxleFields[] = {
    { "track", &AxleDesign::track },
    { "camber", &AxleDesign::camber },
    { "toe", &AxleDesign::toe },
    { "caster", &AxleDesign::caster },
    { "kingpinInclination", &AxleDesign::kingpinInclination },
    { "scrubRadius", &AxleDesign::scrubRadius },
    { "mechanicalTrail", &AxleDesign::mechanicalTrail },
    { "rollCentreHeight", &AxleDesign::rollCentreHeight },
    { "frontViewSwingArm", &AxleDesign::frontViewSwingArm },
    { "antiPercent", &AxleDesign::antiPercent },
    { "sideViewSwingArm", &AxleDesign::sideViewSwingArm },
    { "upperJointHeight", &AxleDesign::upperJointHeight },
    { "lowerJointDrop", &AxleDesign::lowerJointDrop },
    { "upperPivotY", &AxleDesign::upperPivotY },
    { "lowerPivotY", &AxleDesign::lowerPivotY },
    { "upperForwardAngle", &AxleDesign::upperForwardAngle },
    { "upperRearwardAngle", &AxleDesign::upperRearwardAngle },
    { "lowerForwardAngle", &AxleDesign::lowerForwardAngle },
    { "lowerRearwardAngle", &AxleDesign::lowerRearwardAngle },
    { "steeringArm", &AxleDesign::steeringArm },
    { "ackermann", &AxleDesign::ackermann },
    { "tieRodInboardOffsetX", &AxleDesign::tieRodInboardOffsetX },
};

struct CarField {
    const char* key;
    double DesignParameters::*member;
};

constexpr CarField kCarFields[] = {
    { "wheelbase", &DesignParameters::wheelbase },
    { "cogX", &DesignParameters::cogX },
    { "cogHeight", &DesignParameters::cogHeight },
    { "frontWeight", &DesignParameters::frontWeight },
    { "frontBrakeBias", &DesignParameters::frontBrakeBias },
    { "loadedRadius", &DesignParameters::loadedRadius },
    { "rimRadius", &DesignParameters::rimRadius },
    { "chassisClearance", &DesignParameters::chassisClearance },
};

QJsonObject axleToJson(const AxleDesign& axle)
{
    QJsonObject object;
    object.insert(QStringLiteral("generate"), axle.generate);
    object.insert(QStringLiteral("corner"), axle.corner);
    object.insert(QStringLiteral("steered"), axle.steered);
    for (const AxleField& field : kAxleFields) object.insert(QLatin1String(field.key), axle.*field.member);
    object.insert(QStringLiteral("advisedPivot"), designPivotToString(axle.advisedPivot));
    return object;
}

AxleDesign axleFromJson(const QJsonObject& object, const AxleDesign& defaults)
{
    AxleDesign axle = defaults;
    axle.generate = object.value(QStringLiteral("generate")).toBool(defaults.generate);
    axle.corner = object.value(QStringLiteral("corner")).toString(defaults.corner);
    axle.steered = object.value(QStringLiteral("steered")).toBool(defaults.steered);
    for (const AxleField& field : kAxleFields)
        axle.*field.member = object.value(QLatin1String(field.key)).toDouble(defaults.*field.member);
    axle.advisedPivot = designPivotFromString(object.value(QStringLiteral("advisedPivot")).toString(),
                                              defaults.advisedPivot);
    return axle;
}

} // namespace

AxleDesign DesignParameters::frontDefaults()
{
    // The member initialisers are the front axle of the 2025 car.
    AxleDesign front;
    front.corner = QStringLiteral("F");
    return front;
}

AxleDesign DesignParameters::rearDefaults()
{
    // The rear axle of the same car, from the same parameter set. It has no
    // rack: a toe link, bolted to the chassis.
    AxleDesign rear;
    rear.corner = QStringLiteral("R");
    rear.steered = false;
    rear.caster = 0.0;
    rear.kingpinInclination = 0.0;
    rear.scrubRadius = 100.0;
    rear.mechanicalTrail = 0.0;
    rear.frontViewSwingArm = 2500.0;
    rear.antiPercent = 25.0;
    rear.upperForwardAngle = 40.0;
    rear.upperRearwardAngle = 30.0;
    rear.lowerForwardAngle = 30.0;
    rear.lowerRearwardAngle = 35.0;
    rear.steeringArm = 90.0;
    rear.ackermann = 0.0;
    return rear;
}

QJsonObject designParametersToJson(const DesignParameters& parameters)
{
    QJsonObject object;
    for (const CarField& field : kCarFields)
        object.insert(QLatin1String(field.key), parameters.*field.member);
    object.insert(QStringLiteral("useChassis"), parameters.useChassis);
    object.insert(QStringLiteral("side"), parameters.side == DesignSide::Left
                                              ? QStringLiteral("left")
                                              : QStringLiteral("right"));
    object.insert(QStringLiteral("front"), axleToJson(parameters.front));
    object.insert(QStringLiteral("rear"), axleToJson(parameters.rear));
    return object;
}

DesignParameters designParametersFromJson(const QJsonObject& object)
{
    DesignParameters parameters;
    for (const CarField& field : kCarFields) {
        parameters.*field.member =
            object.value(QLatin1String(field.key)).toDouble(parameters.*field.member);
    }
    parameters.useChassis = object.value(QStringLiteral("useChassis")).toBool(parameters.useChassis);
    parameters.side = object.value(QStringLiteral("side")).toString() == QLatin1String("right")
                          ? DesignSide::Right
                          : DesignSide::Left;
    parameters.front = axleFromJson(object.value(QStringLiteral("front")).toObject(),
                                    DesignParameters::frontDefaults());
    parameters.rear = axleFromJson(object.value(QStringLiteral("rear")).toObject(),
                                   DesignParameters::rearDefaults());
    return parameters;
}

QString axlePositionLabel(AxlePosition position)
{
    return position == AxlePosition::Front ? tr("Front") : tr("Rear");
}

QString designPivotToString(DesignPivot pivot)
{
    for (const PivotName& entry : kPivotNames)
        if (entry.pivot == pivot) return QLatin1String(entry.text);
    return QStringLiteral("lowerFront");
}

DesignPivot designPivotFromString(const QString& text, DesignPivot fallback)
{
    for (const PivotName& entry : kPivotNames)
        if (text == QLatin1String(entry.text)) return entry.pivot;
    return fallback;
}

} // namespace suspkin
