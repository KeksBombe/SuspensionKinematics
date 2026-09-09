#include "app/UpdateChecker.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#ifndef SUSPKIN_BUILD
#    define SUSPKIN_BUILD 0
#endif

namespace suspkin {
namespace {

// The rolling release keeps one tag, so this URL is stable forever and always
// resolves to the newest published manifest. No API call, no rate limit, no
// token: it is a plain file download.
constexpr auto kManifestUrl =
    "https://github.com/KeksBombe/SuspensionKinematics/releases/latest/download/version.json";

constexpr auto kSettingsCheckOnStartup = "updates/checkOnStartup";
constexpr auto kSettingsSkippedBuild = "updates/skippedBuild";

/// A GitHub release download answers with a redirect to a storage host, and
/// QNetworkAccessManager does not follow those unless asked.
QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("SuspensionKinematics/%1")
                          .arg(QCoreApplication::applicationVersion()));
    return request;
}

} // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this))
{
}

UpdateChecker::~UpdateChecker() = default;

int UpdateChecker::currentBuild()
{
    return SUSPKIN_BUILD;
}

InstallKind UpdateChecker::installKind()
{
#ifndef Q_OS_WIN
    // Linux ships as a pacman package; the package manager owns these files and
    // an in-application update would fight it.
    return InstallKind::Unknown;
#else
    // The installer writes its uninstaller beside the exe, and nothing else
    // does. That is the difference between an installed copy and an unzipped
    // one, and it does not depend on where the user chose to install.
    const QDir dir(QCoreApplication::applicationDirPath());
    const QStringList uninstallers =
        dir.entryList({QStringLiteral("unins*.exe")}, QDir::Files);
    if (!uninstallers.isEmpty()) return InstallKind::Installed;

    // A build tree has the deploy artefacts missing; a portable unzip does not.
    // Either way it is not something to overwrite from the inside.
    return dir.exists(QStringLiteral("qt.conf")) ? InstallKind::Portable
                                                 : InstallKind::Unknown;
#endif
}

bool UpdateChecker::checkOnStartup()
{
    return QSettings().value(QLatin1String(kSettingsCheckOnStartup), true).toBool();
}

void UpdateChecker::setCheckOnStartup(bool on)
{
    QSettings().setValue(QLatin1String(kSettingsCheckOnStartup), on);
}

void UpdateChecker::skipBuild(int build)
{
    QSettings().setValue(QLatin1String(kSettingsSkippedBuild), build);
}

bool UpdateChecker::isSkipped(int build)
{
    return QSettings().value(QLatin1String(kSettingsSkippedBuild), 0).toInt() == build;
}

void UpdateChecker::check(bool userAsked)
{
    if (!updatesSupported()) {
        if (userAsked) {
            emit failed(tr("This copy was not installed by the installer, so it cannot "
                           "update itself. Download a new build from the releases page."),
                        true);
        }
        return;
    }

    QNetworkReply* reply = m_net->get(makeRequest(QUrl(QLatin1String(kManifestUrl))));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, userAsked] { handleManifest(reply, userAsked); });
}

void UpdateChecker::handleManifest(QNetworkReply* reply, bool userAsked)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit failed(tr("Could not reach GitHub: %1").arg(reply->errorString()), userAsked);
        return;
    }

    QString error;
    const std::optional<UpdateRelease> release =
        parseUpdateManifest(reply->readAll(), &error);
    if (!release) {
        emit failed(tr("The release information could not be read (%1).").arg(error),
                    userAsked);
        return;
    }

    if (!isUpdateAvailable(currentBuild(), *release)) {
        if (userAsked) emit upToDate();
        return;
    }
    // A build the user has already declined is not raised again on its own, but
    // asking from the menu still shows it.
    if (!userAsked && isSkipped(release->build)) return;

    emit updateAvailable(*release, userAsked);
}

void UpdateChecker::downloadAndInstall(const UpdateRelease& release)
{
    if (!release.isUsable()) {
        emit failed(tr("That release is missing the information needed to verify it."), true);
        return;
    }
    QNetworkReply* reply = m_net->get(makeRequest(QUrl(release.assetUrl)));
    connect(reply, &QNetworkReply::downloadProgress, this, &UpdateChecker::downloadProgress);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, release] { handleDownload(reply, release); });
}

void UpdateChecker::handleDownload(QNetworkReply* reply, const UpdateRelease& release)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit failed(tr("The download failed: %1").arg(reply->errorString()), true);
        return;
    }

    const QByteArray payload = reply->readAll();

    // Verified before it is written anywhere it could be run from. This is what
    // separates an updater from a way to run whatever the network hands over:
    // the digest comes from the manifest, which came from the same release.
    if (payload.size() != release.assetSize) {
        emit failed(tr("The download is %1 bytes but the release says %2. It has been "
                       "discarded.")
                        .arg(payload.size())
                        .arg(release.assetSize),
                    true);
        return;
    }
    const QString digest =
        QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256)
                                .toHex());
    if (digest != release.sha256) {
        emit failed(tr("The downloaded installer does not match the checksum the release "
                       "publishes. It has been discarded."),
                    true);
        return;
    }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString name = release.assetName.isEmpty()
                             ? QStringLiteral("SuspensionKinematics-setup.exe")
                             : QFileInfo(release.assetName).fileName();
    const QString path = QDir(dir).filePath(name);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit failed(tr("Could not write the installer to %1.").arg(path), true);
        return;
    }
    if (file.write(payload) != payload.size() || !file.flush()) {
        file.close();
        file.remove();
        emit failed(tr("Could not write the installer to %1.").arg(path), true);
        return;
    }
    file.close();

    QString error;
    if (!runInstaller(path, &error)) {
        emit failed(error, true);
        return;
    }
    emit installerStarted();
}

bool UpdateChecker::runInstaller(const QString& path, QString* error)
{
    // /SILENT shows a progress bar but asks nothing; the answers are already
    // known from the install being upgraded. /RELAUNCH=1 is read by the
    // installer script's [Run] entry, which starts the application again once
    // the files are in place -- Inno skips the ordinary post-install launch on
    // a silent run, and a silent update that never comes back looks like a
    // crash.
    const QStringList args{QStringLiteral("/SILENT"), QStringLiteral("/NORESTART"),
                           QStringLiteral("/RELAUNCH=1")};
    qint64 pid = 0;
    if (!QProcess::startDetached(path, args, QString(), &pid)) {
        if (error) *error = tr("The installer could not be started.");
        return false;
    }
    return true;
}

} // namespace suspkin
