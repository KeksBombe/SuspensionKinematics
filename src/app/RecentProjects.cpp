#include "app/RecentProjects.h"

#include "project/Project.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace suspkin {
namespace {

constexpr int kMaxRecent = 20;
const char kRecentKey[] = "projects/recent";
const char kBrowseKey[] = "projects/lastBrowseDirectory";

/// Stored as one flat string list of triples rather than as nested groups: a
/// QSettings array is far more code for a list this small, and a flat list is
/// readable in the .ini when something needs debugging.
constexpr int kFieldsPerEntry = 3;

} // namespace

QString RecentProject::directory() const
{
    return manifestPath.isEmpty() ? QString() : QFileInfo(manifestPath).absolutePath();
}

bool RecentProject::exists() const
{
    return !manifestPath.isEmpty() && QFileInfo::exists(manifestPath);
}

std::vector<RecentProject> RecentProjects::load()
{
    const QStringList flat = QSettings().value(QLatin1String(kRecentKey)).toStringList();

    std::vector<RecentProject> projects;
    projects.reserve(static_cast<std::size_t>(flat.size() / kFieldsPerEntry));
    for (qsizetype i = 0; i + kFieldsPerEntry <= flat.size(); i += kFieldsPerEntry) {
        RecentProject project;
        project.manifestPath = flat.at(i);
        project.name = flat.at(i + 1);
        project.openedAt = QDateTime::fromString(flat.at(i + 2), Qt::ISODate);
        if (project.manifestPath.isEmpty()) continue;
        if (project.name.isEmpty()) project.name = QDir(project.directory()).dirName();
        projects.push_back(std::move(project));
    }
    return projects;
}

void RecentProjects::remember(const QString& manifestPath, const QString& name)
{
    if (manifestPath.isEmpty()) return;
    const QString canonical = QFileInfo(manifestPath).absoluteFilePath();

    std::vector<RecentProject> projects = load();
    // Same project, opened again: it moves to the top rather than appearing twice.
    projects.erase(std::remove_if(projects.begin(), projects.end(),
                                  [&canonical](const RecentProject& project) {
                                      return QFileInfo(project.manifestPath).absoluteFilePath()
                                          == canonical;
                                  }),
                   projects.end());

    RecentProject entry;
    entry.manifestPath = canonical;
    entry.name = name.isEmpty() ? QFileInfo(canonical).dir().dirName() : name;
    entry.openedAt = QDateTime::currentDateTimeUtc();
    projects.insert(projects.begin(), std::move(entry));
    if (projects.size() > kMaxRecent) projects.resize(kMaxRecent);

    QStringList flat;
    flat.reserve(static_cast<qsizetype>(projects.size()) * kFieldsPerEntry);
    for (const RecentProject& project : projects) {
        flat << project.manifestPath << project.name
             << project.openedAt.toUTC().toString(Qt::ISODate);
    }
    QSettings().setValue(QLatin1String(kRecentKey), flat);
}

void RecentProjects::forget(const QString& manifestPath)
{
    const QString canonical = QFileInfo(manifestPath).absoluteFilePath();
    std::vector<RecentProject> projects = load();

    QStringList flat;
    for (const RecentProject& project : projects) {
        if (QFileInfo(project.manifestPath).absoluteFilePath() == canonical) continue;
        flat << project.manifestPath << project.name
             << project.openedAt.toUTC().toString(Qt::ISODate);
    }
    QSettings().setValue(QLatin1String(kRecentKey), flat);
}

void RecentProjects::clear()
{
    QSettings().remove(QLatin1String(kRecentKey));
}

QString RecentProjects::lastBrowseDirectory()
{
    const QString stored = QSettings().value(QLatin1String(kBrowseKey)).toString();
    if (!stored.isEmpty() && QFileInfo(stored).isDir()) return stored;
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void RecentProjects::setLastBrowseDirectory(const QString& path)
{
    if (!path.isEmpty()) QSettings().setValue(QLatin1String(kBrowseKey), path);
}

} // namespace suspkin
