#pragma once

#include <QDateTime>
#include <QString>

#include <vector>

namespace suspkin {

/// One row of the recent list: enough to draw it without touching the disk more
/// than once per entry.
struct RecentProject {
    QString name;
    QString manifestPath; ///< the project.suspkin inside the project directory
    QDateTime openedAt;

    QString directory() const;
    bool exists() const;
};

/// The list of projects the user has opened, newest first.
///
/// This is the one piece of state that cannot live in a project, since it is
/// what the user picks a project from. It goes in QSettings, per user, and
/// holds nothing but paths.
class RecentProjects {
public:
    static std::vector<RecentProject> load();
    static void remember(const QString& manifestPath, const QString& name);
    static void forget(const QString& manifestPath);
    static void clear();

    /// Where the New and Open dialogs should start, remembered across projects
    /// because it is about the user's disk, not about any one project.
    static QString lastBrowseDirectory();
    static void setLastBrowseDirectory(const QString& path);
};

} // namespace suspkin
