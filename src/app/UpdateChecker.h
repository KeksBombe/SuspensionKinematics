#pragma once

#include "update/UpdateManifest.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace suspkin {

/// How this copy of the application got onto the machine, which decides whether
/// it may update itself at all.
enum class InstallKind {
    Installed, ///< put there by the Inno Setup installer, which can replace it
    Portable,  ///< unpacked from the zip; Windows holds its own files open, so
               ///< nothing can overwrite them from inside, and there is no
               ///< uninstaller to repair a half-finished swap
    Unknown,   ///< a developer's build tree, or a platform that packages itself
};

/// Checks GitHub for a newer release and, if the user agrees, installs it.
///
/// Only ever does anything for an Installed copy. A portable unzip is left
/// alone entirely -- it does not even ask the network -- because updating it
/// would mean overwriting files that are open, and a failure there leaves no
/// working application and no uninstaller to fall back on. On Linux the package
/// manager owns the files and this never runs.
class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    /// Where the running binary came from. Public so the About box can say so.
    static InstallKind installKind();

    /// The build number compiled into this binary; 0 for a local build.
    static int currentBuild();

    /// True when this copy is one that may update itself.
    static bool updatesSupported() { return installKind() == InstallKind::Installed; }

    /// Ask GitHub what the latest release is. Does nothing at all unless
    /// updatesSupported(). `userAsked` suppresses the "skipped build" setting
    /// and makes the no-update case worth reporting, so the Help menu item
    /// always answers.
    void check(bool userAsked);

    /// Download the installer for `release`, verify it, and run it. The
    /// application is expected to quit when installerStarted() arrives.
    void downloadAndInstall(const UpdateRelease& release);

    /// Remember that the user does not want to be asked about this build again.
    static void skipBuild(int build);
    static bool isSkipped(int build);

    /// Whether the startup check runs at all. User-settable, default on.
    static bool checkOnStartup();
    static void setCheckOnStartup(bool on);

signals:
    /// A newer release exists. `userAsked` is echoed back so the window knows
    /// whether this was a background check or the menu item.
    void updateAvailable(const suspkin::UpdateRelease& release, bool userAsked);
    /// Nothing newer. Only emitted when the user asked, so a background check
    /// stays silent.
    void upToDate();
    /// The check or the download failed. Only shown when the user asked; a
    /// background check on a machine with no network says nothing.
    void failed(const QString& message, bool userAsked);
    void downloadProgress(qint64 received, qint64 total);
    /// The installer has been verified and launched. Quit now: it is waiting
    /// for this process to exit before it can replace the files.
    void installerStarted();

private:
    void handleManifest(QNetworkReply* reply, bool userAsked);
    void handleDownload(QNetworkReply* reply, const UpdateRelease& release);
    bool runInstaller(const QString& path, QString* error);

    QNetworkAccessManager* m_net = nullptr;
};

} // namespace suspkin
