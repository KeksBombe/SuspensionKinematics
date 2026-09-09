#include "update/UpdateManifest.h"

#include <QTest>

using namespace suspkin;

// Declared before the fixtures below: moc reads this file itself, and the JSON
// documents are raw string literals it does not parse the way a compiler does.
// With the class after them, moc reaches the end having found no QObject and
// writes an empty .moc, which fails at link time with missing metaObject().
class TestUpdate : public QObject {
    Q_OBJECT

private slots:
    void readsAWellFormedManifest();
    void rejectsJunk();
    void rejectsAManifestMissingItsDigest();
    void rejectsADownloadUrlThatIsNotGitHub();
    void offersOnlyNewerBuilds();
    void aLocalBuildIsNeverOutOfDate();
};

namespace {

/// A manifest with the shape the release workflow writes, so each test changes
/// one thing rather than restating the whole document.
QByteArray goodManifest()
{
    return QByteArrayLiteral(
        "{\n"
        "  \"version\": \"0.1.0\",\n"
        "  \"build\": 42,\n"
        "  \"commit\": \"abc123\",\n"
        "  \"releaseUrl\": \"https://github.com/KeksBombe/SuspensionKinematics/releases/tag/latest\",\n"
        "  \"windows\": {\n"
        "    \"name\": \"SuspensionKinematics-windows-x64-setup.exe\",\n"
        "    \"url\": \"https://github.com/KeksBombe/SuspensionKinematics/releases/download/latest/setup.exe\",\n"
        "    \"sha256\": \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\n"
        "    \"size\": 34103730\n"
        "  }\n"
        "}\n");
}

/// The same document with the `windows` section replaced wholesale.
QByteArray withWindowsSection(const QByteArray& section)
{
    return QByteArrayLiteral("{\"version\":\"0.1.0\",\"build\":42,\"windows\":") + section
           + QByteArrayLiteral("}");
}

} // namespace

void TestUpdate::readsAWellFormedManifest()
{
    QString error;
    const auto release = parseUpdateManifest(goodManifest(), &error);
    QVERIFY2(release.has_value(), qPrintable(error));
    QCOMPARE(release->build, 42);
    QCOMPARE(release->version, QStringLiteral("0.1.0"));
    QCOMPARE(release->assetSize, Q_INT64_C(34103730));
    QVERIFY(release->isUsable());
}

void TestUpdate::rejectsJunk()
{
    QString error;
    // GitHub answers an unknown path with HTML, and a 200 carrying a login page
    // is exactly the sort of thing that must not parse as a release.
    QVERIFY(!parseUpdateManifest("<!DOCTYPE html><html>404</html>", &error).has_value());
    QVERIFY(!error.isEmpty());
    QVERIFY(!parseUpdateManifest("", &error).has_value());
    QVERIFY(!parseUpdateManifest("[1,2,3]", &error).has_value());
    // A document that parses but has no build number cannot be compared against.
    QVERIFY(!parseUpdateManifest("{\"version\":\"0.1.0\"}", &error).has_value());
}

void TestUpdate::rejectsAManifestMissingItsDigest()
{
    QString error;
    // No sha256: nothing to verify the installer against before running it.
    QVERIFY(!parseUpdateManifest(
                 withWindowsSection("{\"url\":\"https://github.com/a/b/releases/download/"
                                    "latest/s.exe\",\"size\":100}"),
                 &error)
                 .has_value());

    // A digest of the wrong length is not usable either, and catching it here
    // saves discovering it only after a 30 MB download.
    QVERIFY(!parseUpdateManifest(
                 withWindowsSection("{\"url\":\"https://github.com/a/b/releases/download/"
                                    "latest/s.exe\",\"sha256\":\"abc\",\"size\":100}"),
                 &error)
                 .has_value());

    // Hex only -- "z" is not a digest however long the string is.
    const QByteArray notHex =
        "{\"url\":\"https://github.com/a/b/releases/download/latest/s.exe\",\"sha256\":\""
        + QByteArray(64, 'z') + "\",\"size\":100}";
    QVERIFY(!parseUpdateManifest(withWindowsSection(notHex), &error).has_value());
}

void TestUpdate::rejectsADownloadUrlThatIsNotGitHub()
{
    // The updater downloads and executes whatever this URL points at, so a
    // manifest that aims it elsewhere is refused rather than followed.
    QString error;
    QVERIFY(!parseUpdateManifest(
                 withWindowsSection(
                     "{\"url\":\"https://example.com/evil.exe\",\"sha256\":\""
                     "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
                     "\",\"size\":100}"),
                 &error)
                 .has_value());
    QVERIFY2(error.contains(QStringLiteral("GitHub")), qPrintable(error));

    // Plain HTTP from the right host is still refused.
    QVERIFY(!parseUpdateManifest(
                 withWindowsSection(
                     "{\"url\":\"http://github.com/a/b/s.exe\",\"sha256\":\""
                     "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
                     "\",\"size\":100}"),
                 &error)
                 .has_value());
}

void TestUpdate::offersOnlyNewerBuilds()
{
    const auto release = parseUpdateManifest(goodManifest(), nullptr);
    QVERIFY(release.has_value());

    QVERIFY(isUpdateAvailable(41, *release));
    QVERIFY(!isUpdateAvailable(42, *release));
    // A build number ahead of the release means this copy is newer than what is
    // published, which is not an update either.
    QVERIFY(!isUpdateAvailable(43, *release));
}

void TestUpdate::aLocalBuildIsNeverOutOfDate()
{
    const auto release = parseUpdateManifest(goodManifest(), nullptr);
    QVERIFY(release.has_value());
    // Build 0 is a developer's own build. Offering to replace it with a release
    // build would quietly throw away whatever they had just compiled.
    QVERIFY(!isUpdateAvailable(0, *release));
}

QTEST_APPLESS_MAIN(TestUpdate)
#include "test_update.moc"
