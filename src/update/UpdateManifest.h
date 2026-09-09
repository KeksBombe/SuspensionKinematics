#pragma once

#include <QString>

#include <optional>

namespace suspkin {

/// What the release publishes about itself, read from the `version.json` asset
/// that the release workflow writes beside the binaries.
///
/// The releases are *rolling*: the tag is always `latest` and the project
/// version in CMakeLists barely moves, so neither of those can answer "is this
/// newer than what I am". The build number can -- it is the workflow's run
/// number, which only ever goes up -- so that is what the comparison is on, and
/// what the running application carries as SUSPKIN_BUILD.
struct UpdateRelease {
    int build = 0;         ///< monotonic; 0 means "no build number", never an update
    QString version;       ///< the project version, for showing the user
    QString commit;        ///< the commit the release was built from
    QString assetName;     ///< file name of the Windows installer in the release
    QString assetUrl;      ///< direct download for that installer
    QString sha256;        ///< lowercase hex digest of the installer
    qint64 assetSize = 0;  ///< bytes, cross-checked against what arrives
    QString releaseUrl;    ///< the human-readable release page

    /// A release is only usable if it can be both compared and verified. An
    /// installer with no digest is not something to download and execute.
    bool isUsable() const
    {
        return build > 0 && !assetUrl.isEmpty() && sha256.size() == 64 && assetSize > 0;
    }
};

/// Parse the `version.json` payload. Returns nothing and sets `error` if the
/// document is not the manifest we expect -- which includes a GitHub error page
/// served with a 200, so the caller never treats junk as a release.
std::optional<UpdateRelease> parseUpdateManifest(const QByteArray& json, QString* error);

/// Whether `available` is something `current` should be offered.
///
/// A build of 0 on either side means the running binary was not built by the
/// release workflow -- a local developer build -- and is never "out of date"
/// against it, because replacing it with a release build would silently discard
/// whatever the developer built.
bool isUpdateAvailable(int currentBuild, const UpdateRelease& available);

} // namespace suspkin
