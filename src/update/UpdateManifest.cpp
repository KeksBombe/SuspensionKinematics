#include "update/UpdateManifest.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace suspkin {
namespace {

/// The digest has to be hex and the right length before it is worth keeping:
/// a malformed one would otherwise only be caught after a 30 MB download.
bool looksLikeSha256(const QString& s)
{
    if (s.size() != 64) return false;
    for (const QChar c : s) {
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f'))) return false;
    }
    return true;
}

} // namespace

std::optional<UpdateRelease> parseUpdateManifest(const QByteArray& json, QString* error)
{
    const auto fail = [error](const QString& message) -> std::optional<UpdateRelease> {
        if (error) *error = message;
        return std::nullopt;
    };

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(QStringLiteral("not JSON: %1").arg(parseError.errorString()));
    if (!doc.isObject()) return fail(QStringLiteral("not a JSON object"));

    const QJsonObject root = doc.object();
    UpdateRelease release;
    release.build = root.value(QStringLiteral("build")).toInt();
    release.version = root.value(QStringLiteral("version")).toString();
    release.commit = root.value(QStringLiteral("commit")).toString();
    release.releaseUrl = root.value(QStringLiteral("releaseUrl")).toString();

    if (release.build <= 0)
        return fail(QStringLiteral("no usable build number"));

    const QJsonObject windows = root.value(QStringLiteral("windows")).toObject();
    if (windows.isEmpty())
        return fail(QStringLiteral("no windows section"));

    release.assetName = windows.value(QStringLiteral("name")).toString();
    release.assetUrl = windows.value(QStringLiteral("url")).toString();
    release.sha256 = windows.value(QStringLiteral("sha256")).toString().toLower();
    // toInteger rather than toInt: an installer is comfortably past 2 GB one day,
    // and a silently truncated size would fail the download check for no reason.
    release.assetSize = windows.value(QStringLiteral("size")).toInteger();

    if (release.assetUrl.isEmpty())
        return fail(QStringLiteral("no download URL"));
    // Anything but HTTPS from GitHub is not something to fetch an executable
    // from, however well-formed the rest of the document is.
    if (!release.assetUrl.startsWith(QLatin1String("https://github.com/"))
        && !release.assetUrl.startsWith(QLatin1String("https://objects.githubusercontent.com/")))
        return fail(QStringLiteral("download URL is not a GitHub HTTPS URL"));
    if (!looksLikeSha256(release.sha256))
        return fail(QStringLiteral("missing or malformed sha256"));
    if (release.assetSize <= 0)
        return fail(QStringLiteral("missing asset size"));

    return release;
}

bool isUpdateAvailable(int currentBuild, const UpdateRelease& available)
{
    if (currentBuild <= 0) return false;
    if (!available.isUsable()) return false;
    return available.build > currentBuild;
}

} // namespace suspkin
