#include "project/Project.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <utility>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("Project", text); }

constexpr int kFormatVersion = 1;
const char kFormatTag[] = "suspkin-project";
const char kEditsFormatTag[] = "suspkin-hardpoint-edits";

QString isoOrEmpty(const QDateTime& time)
{
    return time.isValid() ? time.toUTC().toString(Qt::ISODate) : QString();
}

QDateTime fromIso(const QJsonValue& value)
{
    return value.isString() ? QDateTime::fromString(value.toString(), Qt::ISODate) : QDateTime();
}

QJsonObject writeSweepSettings(const SweepSettings& settings)
{
    QJsonObject sweep;
    sweep.insert(QStringLiteral("bumpTravel"), settings.bumpTravel);
    sweep.insert(QStringLiteral("reboundTravel"), settings.reboundTravel);
    sweep.insert(QStringLiteral("bumpIncrement"), settings.bumpIncrement);
    sweep.insert(QStringLiteral("rollAngle"), settings.rollAngle);
    sweep.insert(QStringLiteral("rollIncrement"), settings.rollIncrement);
    sweep.insert(QStringLiteral("steerTravel"), settings.steerTravel);
    sweep.insert(QStringLiteral("steerIncrement"), settings.steerIncrement);
    sweep.insert(QStringLiteral("rack"), settings.rackTravel);
    return sweep;
}

SweepSettings readSweepSettings(const QJsonObject& sweep)
{
    SweepSettings settings;
    const auto number = [&sweep](const QString& key, double fallback) {
        return sweep.value(key).toDouble(fallback);
    };
    settings.bumpTravel = number(QStringLiteral("bumpTravel"), settings.bumpTravel);
    settings.reboundTravel = number(QStringLiteral("reboundTravel"), settings.reboundTravel);
    settings.bumpIncrement = number(QStringLiteral("bumpIncrement"), settings.bumpIncrement);
    settings.rollAngle = number(QStringLiteral("rollAngle"), settings.rollAngle);
    settings.rollIncrement = number(QStringLiteral("rollIncrement"), settings.rollIncrement);
    settings.steerTravel = number(QStringLiteral("steerTravel"), settings.steerTravel);
    settings.steerIncrement = number(QStringLiteral("steerIncrement"), settings.steerIncrement);
    settings.rackTravel = number(QStringLiteral("rack"), settings.rackTravel);
    return settings;
}

/// What a project written before the three sweeps had their own travel holds:
/// one range, in whichever unit the sweep it was left on happened to be in.
///
/// It is folded back into that kind's own numbers rather than dropped, so
/// reopening an older project lands on the travel it was last used with. The
/// other two kinds keep their defaults, which is all the file has to say about
/// them.
SweepSettings legacySweepSettings(const QJsonObject& simulation, SweepKind kind)
{
    SweepSettings settings;
    const double from = simulation.value(QStringLiteral("from")).toDouble(-25.0);
    const double to = simulation.value(QStringLiteral("to")).toDouble(25.0);
    const int steps = simulation.value(QStringLiteral("steps")).toInt(41);
    settings.rackTravel = simulation.value(QStringLiteral("rack")).toDouble(0.0);

    const double low = std::min(from, to);
    const double high = std::max(from, to);
    const double increment = steps > 1 ? (high - low) / double(steps - 1) : 1.0;
    if (!(increment > 0.0)) return settings;

    switch (kind) {
    case SweepKind::Bump:
        settings.reboundTravel = std::abs(low);
        settings.bumpTravel = std::abs(high);
        settings.bumpIncrement = increment;
        break;
    case SweepKind::Roll:
        settings.rollAngle = std::max(std::abs(low), std::abs(high));
        settings.rollIncrement = increment;
        break;
    case SweepKind::Steer:
        settings.steerTravel = std::max(std::abs(low), std::abs(high));
        settings.steerIncrement = increment;
        break;
    }
    return settings;
}

QJsonObject assetToJson(const AssetRef& asset)
{
    QJsonObject object;
    object.insert(QStringLiteral("path"), asset.relativePath);
    if (!asset.originalPath.isEmpty())
        object.insert(QStringLiteral("importedFrom"), asset.originalPath);
    if (asset.importedAt.isValid())
        object.insert(QStringLiteral("importedAt"), isoOrEmpty(asset.importedAt));
    return object;
}

AssetRef assetFromJson(const QJsonObject& object)
{
    AssetRef asset;
    asset.relativePath = object.value(QStringLiteral("path")).toString();
    asset.originalPath = object.value(QStringLiteral("importedFrom")).toString();
    asset.importedAt = fromIso(object.value(QStringLiteral("importedAt")));
    return asset;
}

QJsonArray vectorToJson(const QVector3D& vector)
{
    return QJsonArray{ double(vector.x()), double(vector.y()), double(vector.z()) };
}

QVector3D vectorFromJson(const QJsonValue& value, const QVector3D& fallback)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 3) return fallback;
    return QVector3D(float(array.at(0).toDouble()), float(array.at(1).toDouble()),
                     float(array.at(2).toDouble()));
}

QJsonObject cameraToJson(const CameraState& camera)
{
    QJsonObject object;
    object.insert(QStringLiteral("pivot"), vectorToJson(camera.pivot));
    object.insert(QStringLiteral("distance"), double(camera.distance));
    object.insert(QStringLiteral("azimuthDeg"), double(camera.azimuthDeg));
    object.insert(QStringLiteral("elevationDeg"), double(camera.elevationDeg));
    object.insert(QStringLiteral("fovYDeg"), double(camera.fovYDeg));
    object.insert(QStringLiteral("sceneRadius"), double(camera.sceneRadius));
    return object;
}

CameraState cameraFromJson(const QJsonObject& object)
{
    CameraState camera;
    camera.pivot = vectorFromJson(object.value(QStringLiteral("pivot")), camera.pivot);
    camera.distance = float(object.value(QStringLiteral("distance")).toDouble(camera.distance));
    camera.azimuthDeg =
        float(object.value(QStringLiteral("azimuthDeg")).toDouble(camera.azimuthDeg));
    camera.elevationDeg =
        float(object.value(QStringLiteral("elevationDeg")).toDouble(camera.elevationDeg));
    camera.fovYDeg = float(object.value(QStringLiteral("fovYDeg")).toDouble(camera.fovYDeg));
    camera.sceneRadius =
        float(object.value(QStringLiteral("sceneRadius")).toDouble(camera.sceneRadius));
    return camera;
}

QJsonObject mirrorToJson(const MirrorSpec& spec)
{
    QJsonObject object;
    object.insert(QStringLiteral("axis"), mirrorAxisToString(spec.axis));
    object.insert(QStringLiteral("naming"), mirrorNamingToString(spec.naming));
    object.insert(QStringLiteral("affix"), spec.affix);
    object.insert(QStringLiteral("find"), spec.findText);
    object.insert(QStringLiteral("replace"), spec.replaceText);
    object.insert(QStringLiteral("caseSensitive"), spec.caseSensitive);
    object.insert(QStringLiteral("updateExisting"), spec.updateExisting);
    object.insert(QStringLiteral("skipMirrored"), spec.skipMirrored);
    return object;
}

MirrorSpec mirrorFromJson(const QJsonObject& object)
{
    MirrorSpec spec;
    spec.axis = mirrorAxisFromString(object.value(QStringLiteral("axis")).toString(), spec.axis);
    spec.naming =
        mirrorNamingFromString(object.value(QStringLiteral("naming")).toString(), spec.naming);
    spec.affix = object.value(QStringLiteral("affix")).toString(spec.affix);
    spec.findText = object.value(QStringLiteral("find")).toString();
    spec.replaceText = object.value(QStringLiteral("replace")).toString();
    spec.caseSensitive = object.value(QStringLiteral("caseSensitive")).toBool(spec.caseSensitive);
    spec.updateExisting = object.value(QStringLiteral("updateExisting")).toBool(spec.updateExisting);
    spec.skipMirrored = object.value(QStringLiteral("skipMirrored")).toBool(spec.skipMirrored);
    return spec;
}

QJsonObject wheelSpecToJson(const WheelSpec& spec)
{
    QJsonObject object;
    QJsonObject points;
    for (const WheelCorner corner : kWheelCorners) {
        // Corners the user did not pick are left out rather than written empty,
        // so a hand-edited manifest reads as what it is.
        if (!spec.point(corner).isEmpty())
            points.insert(wheelCornerToString(corner), spec.point(corner));
    }
    object.insert(QStringLiteral("points"), points);
    object.insert(QStringLiteral("modelSide"), wheelModelSideToString(spec.modelSide));
    object.insert(QStringLiteral("alignToCenter"), spec.alignToCenter);
    return object;
}

WheelSpec wheelSpecFromJson(const QJsonObject& object)
{
    WheelSpec spec;
    const QJsonObject points = object.value(QStringLiteral("points")).toObject();
    for (const WheelCorner corner : kWheelCorners)
        spec.setPoint(corner, points.value(wheelCornerToString(corner)).toString());
    spec.modelSide = wheelModelSideFromString(object.value(QStringLiteral("modelSide")).toString(),
                                              spec.modelSide);
    spec.alignToCenter = object.value(QStringLiteral("alignToCenter")).toBool(spec.alignToCenter);
    return spec;
}

QJsonObject configToJson(const HardpointConfig& config)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), pointTypeToString(config.type));
    if (!config.part1.isEmpty()) object.insert(QStringLiteral("part1"), config.part1);
    if (!config.part2.isEmpty()) object.insert(QStringLiteral("part2"), config.part2);
    if (config.bushing != kNoBushing) object.insert(QStringLiteral("bushing"), config.bushing);
    return object;
}

HardpointConfig configFromJson(const QJsonObject& object)
{
    HardpointConfig config;
    config.type = pointTypeFromString(object.value(QStringLiteral("type")).toString());
    config.part1 = object.value(QStringLiteral("part1")).toString();
    config.part2 = object.value(QStringLiteral("part2")).toString();
    config.bushing = object.value(QStringLiteral("bushing")).toInt(kNoBushing);
    return config;
}

QJsonObject pointToJson(const Hardpoint& point)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), point.name);
    object.insert(QStringLiteral("coord"),
                  QJsonArray{ point.coord[0], point.coord[1], point.coord[2] });
    if (point.isMirrored()) object.insert(QStringLiteral("mirrorOf"), point.mirrorOf);
    return object;
}

std::optional<Hardpoint> pointFromJson(const QJsonValue& value)
{
    const QJsonObject object = value.toObject();
    const QString name = object.value(QStringLiteral("name")).toString();
    const QJsonArray coordinates = object.value(QStringLiteral("coord")).toArray();
    if (name.isEmpty() || coordinates.size() != 3) return std::nullopt;

    Hardpoint point;
    point.name = name;
    for (int axis = 0; axis < 3; ++axis) point.coord[axis] = coordinates.at(axis).toDouble();
    point.mirrorOf = object.value(QStringLiteral("mirrorOf")).toString();
    return point;
}

/// Write @p bytes to @p path through a temporary, so an interrupted save cannot
/// leave a half-written project behind.
bool writeAtomically(const QString& path, const QByteArray& bytes, QString* error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = tr("Cannot create %1.").arg(QFileInfo(path).absolutePath());
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = tr("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = tr("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Project
// ---------------------------------------------------------------------------

QString Project::manifestName() { return QStringLiteral("project.suspkin"); }
QString Project::fileExtension() { return QStringLiteral("suspkin"); }

QString Project::fileFilter()
{
    return tr("SuspensionKinematics projects (*.suspkin);;All files (*)");
}

QString Project::manifestPath() const { return QDir(m_root).filePath(manifestName()); }

QString Project::absolutePath(const QString& relativePath) const
{
    if (relativePath.isEmpty()) return {};
    return QDir(m_root).filePath(relativePath);
}

std::optional<Project> Project::create(const QString& directory, const QString& name,
                                       QString* error)
{
    const QString root = QDir::cleanPath(directory);
    if (root.isEmpty()) {
        if (error) *error = tr("A project needs a folder to live in.");
        return std::nullopt;
    }

    QDir dir(root);
    if (dir.exists(manifestName())) {
        if (error)
            *error = tr("There is already a project in %1.").arg(QDir::toNativeSeparators(root));
        return std::nullopt;
    }
    if (!QDir().mkpath(root)) {
        if (error) *error = tr("Cannot create %1.").arg(QDir::toNativeSeparators(root));
        return std::nullopt;
    }

    Project project;
    project.m_root = root;
    project.m_name = name.trimmed().isEmpty() ? dir.dirName() : name.trimmed();
    project.m_created = QDateTime::currentDateTimeUtc();
    project.m_modified = project.m_created;
    if (!project.save(error)) return std::nullopt;
    return project;
}

std::optional<Project> Project::open(const QString& manifestPath, QString* error)
{
    QFileInfo info(manifestPath);
    // Opening the directory is the same thing to a user as opening the manifest
    // inside it, so both are accepted.
    const QString file =
        info.isDir() ? QDir(info.absoluteFilePath()).filePath(manifestName()) : manifestPath;

    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Cannot open %1: %2").arg(QDir::toNativeSeparators(file), in.errorString());
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(in.readAll(), &parseError);
    if (document.isNull() || !document.isObject()) {
        if (error) {
            *error = tr("%1 is not a readable project file: %2")
                         .arg(QDir::toNativeSeparators(file), parseError.errorString());
        }
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kFormatTag)) {
        if (error)
            *error = tr("%1 is not a SuspensionKinematics project.")
                         .arg(QDir::toNativeSeparators(file));
        return std::nullopt;
    }
    if (root.value(QStringLiteral("formatVersion")).toInt(1) > kFormatVersion) {
        if (error) {
            *error = tr("%1 was written by a newer version of SuspensionKinematics and cannot be "
                        "opened safely.")
                         .arg(QDir::toNativeSeparators(file));
        }
        return std::nullopt;
    }

    Project project;
    project.m_root = QFileInfo(file).absolutePath();
    project.m_name = root.value(QStringLiteral("name")).toString(QDir(project.m_root).dirName());
    project.m_created = fromIso(root.value(QStringLiteral("created")));
    project.m_modified = fromIso(root.value(QStringLiteral("modified")));

    if (root.contains(QStringLiteral("geometry")))
        project.m_geometry = assetFromJson(root.value(QStringLiteral("geometry")).toObject());

    if (root.contains(QStringLiteral("hardpoints"))) {
        const QJsonObject hardpoints = root.value(QStringLiteral("hardpoints")).toObject();
        project.m_hardpoints.workbook =
            assetFromJson(hardpoints.value(QStringLiteral("workbook")).toObject());
        project.m_hardpoints.sheetName = hardpoints.value(QStringLiteral("sheet")).toString();
        const QJsonObject mirrored = hardpoints.value(QStringLiteral("mirrored")).toObject();
        for (auto it = mirrored.begin(); it != mirrored.end(); ++it)
            project.m_hardpoints.mirrored.insert(it.key(), it.value().toString());
        const QJsonObject config = hardpoints.value(QStringLiteral("config")).toObject();
        for (auto it = config.begin(); it != config.end(); ++it)
            project.m_hardpoints.config.insert(it.key(), configFromJson(it.value().toObject()));
    }

    if (root.contains(QStringLiteral("linkage"))) {
        const QJsonObject linkage = root.value(QStringLiteral("linkage")).toObject();
        project.m_linkageTemplate =
            assetFromJson(linkage.value(QStringLiteral("template")).toObject());
    }

    if (root.contains(QStringLiteral("wheels"))) {
        const QJsonObject wheels = root.value(QStringLiteral("wheels")).toObject();
        project.m_wheels.wheel = assetFromJson(wheels.value(QStringLiteral("wheel")).toObject());
        project.m_wheels.rim = assetFromJson(wheels.value(QStringLiteral("rim")).toObject());
        project.m_wheels.spec = wheelSpecFromJson(wheels);
    }

    const QJsonObject view = root.value(QStringLiteral("view")).toObject();
    if (view.contains(QStringLiteral("camera"))) {
        project.m_view.camera = cameraFromJson(view.value(QStringLiteral("camera")).toObject());
        project.m_view.cameraValid = true;
    }
    project.m_view.displayMode =
        view.value(QStringLiteral("displayMode")).toString() == QLatin1String("triangles")
            ? DisplayMode::Triangles
            : DisplayMode::Solid;
    project.m_view.labelsVisible = view.value(QStringLiteral("labels")).toBool(true);
    project.m_view.linksVisible = view.value(QStringLiteral("links")).toBool(true);
    project.m_view.wheelsVisible = view.value(QStringLiteral("wheels")).toBool(true);
    project.m_view.selectedHardpoint = view.value(QStringLiteral("selected")).toInt(-1);

    const QJsonObject simulation = view.value(QStringLiteral("simulation")).toObject();
    if (!simulation.isEmpty()) {
        SimulationState& state = project.m_view.simulation;
        state.active = simulation.value(QStringLiteral("active")).toBool(false);
        state.axle = simulation.value(QStringLiteral("axle")).toString();
        state.position = simulation.value(QStringLiteral("position")).toDouble(0.0);
        state.measure = simulation.value(QStringLiteral("measure")).toString();
        state.kind = sweepKindFromString(simulation.value(QStringLiteral("kind")).toString());
        const QJsonObject sweep = simulation.value(QStringLiteral("sweep")).toObject();
        state.sweep = sweep.isEmpty()
                          ? legacySweepSettings(simulation, state.kind)
                          : readSweepSettings(sweep);
        state.animating = simulation.value(QStringLiteral("animating")).toBool(false);
        state.animationSeconds =
            simulation.value(QStringLiteral("animationSeconds")).toDouble(4.0);
        state.allAxles = simulation.value(QStringLiteral("allAxles")).toBool(true);
        state.parametersOpen = simulation.value(QStringLiteral("parametersOpen")).toBool(false);
    }

    const QJsonObject window = root.value(QStringLiteral("window")).toObject();
    project.m_window.geometry =
        QByteArray::fromBase64(window.value(QStringLiteral("geometry")).toString().toLatin1());
    project.m_window.dockState =
        QByteArray::fromBase64(window.value(QStringLiteral("state")).toString().toLatin1());

    if (root.contains(QStringLiteral("mirror")))
        project.m_mirror = mirrorFromJson(root.value(QStringLiteral("mirror")).toObject());

    const QJsonObject directories = root.value(QStringLiteral("directories")).toObject();
    project.m_lastGeometryDirectory = directories.value(QStringLiteral("geometry")).toString();
    project.m_lastHardpointDirectory = directories.value(QStringLiteral("hardpoints")).toString();

    return project;
}

bool Project::save(QString* error) const
{
    m_modified = QDateTime::currentDateTimeUtc();

    QJsonObject root;
    root.insert(QStringLiteral("format"), QLatin1String(kFormatTag));
    root.insert(QStringLiteral("formatVersion"), kFormatVersion);
    root.insert(QStringLiteral("name"), m_name);
    root.insert(QStringLiteral("created"), isoOrEmpty(m_created));
    root.insert(QStringLiteral("modified"), isoOrEmpty(m_modified));

    if (!m_geometry.isEmpty()) root.insert(QStringLiteral("geometry"), assetToJson(m_geometry));

    if (!m_hardpoints.isEmpty()) {
        QJsonObject hardpoints;
        hardpoints.insert(QStringLiteral("workbook"), assetToJson(m_hardpoints.workbook));
        hardpoints.insert(QStringLiteral("sheet"), m_hardpoints.sheetName);
        if (!m_hardpoints.mirrored.isEmpty()) {
            QJsonObject mirrored;
            for (auto it = m_hardpoints.mirrored.begin(); it != m_hardpoints.mirrored.end(); ++it)
                mirrored.insert(it.key(), it.value());
            hardpoints.insert(QStringLiteral("mirrored"), mirrored);
        }
        if (!m_hardpoints.config.isEmpty()) {
            // Every entry is written, empty ones included: an entry that exists
            // and says nothing is a point the user cleared on purpose, which is
            // not the same as one nobody has got to yet.
            QJsonObject config;
            for (auto it = m_hardpoints.config.begin(); it != m_hardpoints.config.end(); ++it)
                config.insert(it.key(), configToJson(it.value()));
            hardpoints.insert(QStringLiteral("config"), config);
        }
        root.insert(QStringLiteral("hardpoints"), hardpoints);
    }

    if (!m_linkageTemplate.isEmpty()) {
        QJsonObject linkage;
        linkage.insert(QStringLiteral("template"), assetToJson(m_linkageTemplate));
        root.insert(QStringLiteral("linkage"), linkage);
    }

    // Written when there is either a model or a set of corners: a user who takes
    // the models out has not thereby said to forget which points their wheels
    // were on.
    if (!m_wheels.isEmpty() || !m_wheels.spec.isEmpty()) {
        QJsonObject wheels = wheelSpecToJson(m_wheels.spec);
        if (!m_wheels.wheel.isEmpty())
            wheels.insert(QStringLiteral("wheel"), assetToJson(m_wheels.wheel));
        if (!m_wheels.rim.isEmpty())
            wheels.insert(QStringLiteral("rim"), assetToJson(m_wheels.rim));
        root.insert(QStringLiteral("wheels"), wheels);
    }

    QJsonObject view;
    if (m_view.cameraValid) view.insert(QStringLiteral("camera"), cameraToJson(m_view.camera));
    view.insert(QStringLiteral("displayMode"), m_view.displayMode == DisplayMode::Triangles
                                                   ? QStringLiteral("triangles")
                                                   : QStringLiteral("solid"));
    view.insert(QStringLiteral("labels"), m_view.labelsVisible);
    view.insert(QStringLiteral("links"), m_view.linksVisible);
    view.insert(QStringLiteral("wheels"), m_view.wheelsVisible);
    view.insert(QStringLiteral("selected"), m_view.selectedHardpoint);

    const SimulationState& state = m_view.simulation;
    QJsonObject simulation;
    simulation.insert(QStringLiteral("active"), state.active);
    if (!state.axle.isEmpty()) simulation.insert(QStringLiteral("axle"), state.axle);
    if (!state.measure.isEmpty()) simulation.insert(QStringLiteral("measure"), state.measure);
    simulation.insert(QStringLiteral("kind"), sweepKindToString(state.kind));
    simulation.insert(QStringLiteral("sweep"), writeSweepSettings(state.sweep));
    simulation.insert(QStringLiteral("position"), state.position);
    simulation.insert(QStringLiteral("animating"), state.animating);
    simulation.insert(QStringLiteral("animationSeconds"), state.animationSeconds);
    simulation.insert(QStringLiteral("allAxles"), state.allAxles);
    simulation.insert(QStringLiteral("parametersOpen"), state.parametersOpen);
    view.insert(QStringLiteral("simulation"), simulation);

    root.insert(QStringLiteral("view"), view);

    if (!m_window.isEmpty()) {
        QJsonObject window;
        window.insert(QStringLiteral("geometry"),
                      QString::fromLatin1(m_window.geometry.toBase64()));
        window.insert(QStringLiteral("state"), QString::fromLatin1(m_window.dockState.toBase64()));
        root.insert(QStringLiteral("window"), window);
    }

    root.insert(QStringLiteral("mirror"), mirrorToJson(m_mirror));

    QJsonObject directories;
    directories.insert(QStringLiteral("geometry"), m_lastGeometryDirectory);
    directories.insert(QStringLiteral("hardpoints"), m_lastHardpointDirectory);
    root.insert(QStringLiteral("directories"), directories);

    return writeAtomically(manifestPath(), QJsonDocument(root).toJson(QJsonDocument::Indented),
                           error);
}

std::optional<AssetRef> Project::importAsset(const QString& sourcePath,
                                             const QString& subdirectory, QString* error)
{
    return importAssetAs(sourcePath, subdirectory, QString(), error);
}

std::optional<AssetRef> Project::importAssetAs(const QString& sourcePath,
                                               const QString& subdirectory,
                                               const QString& targetFileName, QString* error)
{
    const QFileInfo source(sourcePath);
    if (!source.exists() || !source.isFile()) {
        if (error)
            *error = tr("%1 is not a file that can be copied into the project.")
                         .arg(QDir::toNativeSeparators(sourcePath));
        return std::nullopt;
    }

    const QString targetDirectory = QDir(m_root).filePath(subdirectory);
    if (!QDir().mkpath(targetDirectory)) {
        if (error)
            *error = tr("Cannot create %1.").arg(QDir::toNativeSeparators(targetDirectory));
        return std::nullopt;
    }

    const QString target = QDir(targetDirectory)
                              .filePath(targetFileName.isEmpty() ? source.fileName()
                                                                 : targetFileName);

    // Importing the project's own copy again -- the recent-files menu makes that
    // easy to do -- must not delete the file and then fail to copy it back.
    if (QFileInfo(target).canonicalFilePath() != source.canonicalFilePath()) {
        if (QFile::exists(target) && !QFile::remove(target)) {
            if (error)
                *error = tr("Cannot replace %1.").arg(QDir::toNativeSeparators(target));
            return std::nullopt;
        }
        if (!QFile::copy(source.absoluteFilePath(), target)) {
            if (error) {
                *error = tr("Cannot copy %1 into the project.")
                             .arg(QDir::toNativeSeparators(sourcePath));
            }
            return std::nullopt;
        }
        // A copy inherits the source's permissions, and CAD exports off a share
        // are often read-only, which would block overwriting it later.
        QFile(target).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser
                                     | QFile::WriteUser | QFile::ReadGroup | QFile::ReadOther);
    }

    AssetRef asset;
    asset.relativePath = QDir(m_root).relativeFilePath(target);
    asset.originalPath = source.absoluteFilePath();
    asset.importedAt = QDateTime::currentDateTimeUtc();
    return asset;
}

bool Project::writeFile(const QString& relativePath, const QByteArray& bytes, QString* error) const
{
    return writeAtomically(absolutePath(relativePath), bytes, error);
}

// ---------------------------------------------------------------------------
// Hardpoint edits
// ---------------------------------------------------------------------------

HardpointEdits diffHardpoints(const HardpointTable& baseline, const HardpointTable& current)
{
    HardpointEdits edits;
    for (const Hardpoint& point : current.points) {
        const Hardpoint* original = baseline.find(point.name);
        if (!original) {
            edits.added.push_back(point);
            continue;
        }
        // Bit-exact: these numbers are only ever set by parsing text or by the
        // user typing one, so "close enough" would mean writing an edit file for
        // a project nobody touched.
        //
        // Coordinates only. Where a point came from is the project's own record
        // (HardpointRef::mirrored), not something the workbook can disagree with,
        // so it must not make a point look edited.
        const bool same = original->coord[0] == point.coord[0]
            && original->coord[1] == point.coord[1] && original->coord[2] == point.coord[2];
        if (!same) edits.changed.push_back(point);
    }
    // And the other direction: a name the workbook has that the table no longer
    // does was deleted. The workbook itself is left alone until it is written.
    for (const Hardpoint& point : baseline.points)
        if (current.indexOf(point.name) < 0) edits.removed.append(point.name);
    return edits;
}

HardpointTable applyHardpointEdits(const HardpointTable& baseline, const HardpointEdits& edits)
{
    HardpointTable table = baseline;

    // Removals first, so a name that was deleted and then re-added comes back as
    // the addition rather than being taken out again.
    if (!edits.removed.isEmpty()) {
        std::vector<Hardpoint> kept;
        kept.reserve(table.points.size());
        for (Hardpoint& point : table.points)
            if (!edits.removed.contains(point.name)) kept.push_back(std::move(point));
        table.points = std::move(kept);
    }

    for (const Hardpoint& point : edits.changed) {
        const int index = table.indexOf(point.name);
        // A point the workbook no longer has is not dropped: it comes back as an
        // added one, so re-importing a workbook that lost a row cannot silently
        // discard the work done on it.
        if (index < 0)
            table.points.push_back(point);
        else
            table.points[static_cast<std::size_t>(index)] = point;
    }
    for (const Hardpoint& point : edits.added) {
        const int index = table.indexOf(point.name);
        if (index < 0)
            table.points.push_back(point);
        else
            table.points[static_cast<std::size_t>(index)] = point;
    }
    return table;
}

bool writeHardpointEdits(const QString& path, const HardpointEdits& edits, QString* error)
{
    // Nothing pending means no file: an edits file that exists but says nothing
    // would look like unsaved work in every listing the user opens.
    if (edits.isEmpty()) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            if (error) *error = tr("Cannot remove %1.").arg(QDir::toNativeSeparators(path));
            return false;
        }
        return true;
    }

    QJsonArray changed;
    for (const Hardpoint& point : edits.changed) changed.append(pointToJson(point));
    QJsonArray added;
    for (const Hardpoint& point : edits.added) added.append(pointToJson(point));

    QJsonObject root;
    root.insert(QStringLiteral("format"), QLatin1String(kEditsFormatTag));
    root.insert(QStringLiteral("formatVersion"), kFormatVersion);
    root.insert(QStringLiteral("changed"), changed);
    root.insert(QStringLiteral("added"), added);
    if (!edits.removed.isEmpty()) {
        QJsonArray removed;
        for (const QString& name : edits.removed) removed.append(name);
        root.insert(QStringLiteral("removed"), removed);
    }

    return writeAtomically(path, QJsonDocument(root).toJson(QJsonDocument::Indented), error);
}

std::optional<HardpointEdits> readHardpointEdits(const QString& path, QString* error)
{
    if (!QFile::exists(path)) return HardpointEdits{};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Cannot read %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString());
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || !document.isObject()) {
        if (error) {
            *error = tr("The hardpoint edits in %1 are unreadable: %2")
                         .arg(QDir::toNativeSeparators(path), parseError.errorString());
        }
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kEditsFormatTag)) {
        if (error)
            *error = tr("%1 is not a hardpoint edits file.").arg(QDir::toNativeSeparators(path));
        return std::nullopt;
    }

    HardpointEdits edits;
    for (const QJsonValue& value : root.value(QStringLiteral("changed")).toArray()) {
        if (const std::optional<Hardpoint> point = pointFromJson(value))
            edits.changed.push_back(*point);
    }
    for (const QJsonValue& value : root.value(QStringLiteral("added")).toArray()) {
        if (const std::optional<Hardpoint> point = pointFromJson(value))
            edits.added.push_back(*point);
    }
    for (const QJsonValue& value : root.value(QStringLiteral("removed")).toArray()) {
        const QString name = value.toString();
        if (!name.isEmpty()) edits.removed.append(name);
    }
    return edits;
}

} // namespace suspkin
