#include "app/LicensesDialog.h"

#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QString>
#include <QTest>
#include <QTextBrowser>

using namespace suspkin;

namespace {

QString read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}

} // namespace

/// The licences are an obligation, not a feature: Qt and Open CASCADE are LGPL
/// and the icons are MIT, and each asks that its text go with every copy of the
/// program. So what is checked here is that the texts are really in the binary,
/// that the dialog offers every one of them, and that each is the licence it
/// says it is rather than an empty file with the right name.
class TestLicenses : public QObject {
    Q_OBJECT

private slots:
    /// Neither list may run ahead of the other: a text compiled in with no
    /// entry would never be shown, and an entry with no text is dropped
    /// silently by the dialog -- which is exactly how a licence goes missing
    /// without anyone noticing.
    void everyEmbeddedLicenceIsOfferedAndEveryOfferHasAText()
    {
        const QStringList embedded = QDir(QStringLiteral(":/licenses")).entryList(QDir::Files);
        QVERIFY(!embedded.isEmpty());

        LicensesDialog dialog;
        auto* list = dialog.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), embedded.size());

        for (int row = 0; row < list->count(); ++row) {
            const QString path = list->item(row)->data(Qt::UserRole).toString();
            QVERIFY2(path.startsWith(QStringLiteral(":/licenses/")), qPrintable(path));
            QVERIFY2(read(path).size() > 200, qPrintable(path));
        }
    }

    /// Each text is the licence it is named for. A truncated or mis-copied file
    /// still opens and still fills the dialog, so the file name is not evidence.
    void eachTextIsTheLicenceItClaimsToBe()
    {
        QVERIFY(read(QStringLiteral(":/licenses/LICENSE"))
                    .contains(QStringLiteral("GNU GENERAL PUBLIC LICENSE")));
        QVERIFY(read(QStringLiteral(":/licenses/LGPL-3.0.txt"))
                    .contains(QStringLiteral("GNU LESSER GENERAL PUBLIC LICENSE")));
        QVERIFY(read(QStringLiteral(":/licenses/LGPL-2.1.txt"))
                    .contains(QStringLiteral("GNU LESSER GENERAL PUBLIC LICENSE")));
        QVERIFY(read(QStringLiteral(":/licenses/OCCT-exception.txt"))
                    .contains(QStringLiteral("Open CASCADE exception")));
        QVERIFY(read(QStringLiteral(":/licenses/LICENSE-tabler.txt"))
                    .contains(QStringLiteral("MIT License")));
        QVERIFY(read(QStringLiteral(":/licenses/zlib.txt"))
                    .contains(QStringLiteral("Jean-loup Gailly")));
        QVERIFY(read(QStringLiteral(":/licenses/mesa-llvmpipe.txt"))
                    .contains(QStringLiteral("University of Illinois")));
    }

    /// The two statements that have to be made in words rather than by quoting
    /// a licence: the Independent JPEG Group's, which Qt's JPEG plugin carries,
    /// and FreeType's.
    void theAttributionsThatAreSentencesAreThere()
    {
        const QString qt = read(QStringLiteral(":/licenses/qt-third-party.txt"));
        QVERIFY(qt.contains(QStringLiteral("Independent JPEG Group")));
        QVERIFY(qt.contains(QStringLiteral("FreeType Project")));
    }

    /// Picking an entry shows it. The dialog reads its text on selection, so an
    /// entry that lists but cannot be displayed would otherwise look fine.
    void pickingAnEntryShowsItsText()
    {
        LicensesDialog dialog;
        auto* list = dialog.findChild<QListWidget*>();
        auto* browser = dialog.findChild<QTextBrowser*>();
        QVERIFY(list != nullptr);
        QVERIFY(browser != nullptr);

        for (int row = 0; row < list->count(); ++row) {
            list->setCurrentRow(row);
            QVERIFY2(browser->toPlainText().size() > 200,
                     qPrintable(list->item(row)->text()));
        }
    }
};

QTEST_MAIN(TestLicenses)
#include "test_licenses.moc"
