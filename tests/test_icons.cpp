#include "app/Icons.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPalette>
#include <QPixmap>
#include <QRegularExpression>
#include <QSet>
#include <QSvgRenderer>
#include <QTest>

using namespace suspkin;

namespace {

QPalette makePalette(const QColor& text, const QColor& button, const QColor& disabledText)
{
    QPalette palette;
    for (const QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive }) {
        palette.setColor(group, QPalette::ButtonText, text);
        palette.setColor(group, QPalette::Button, button);
        palette.setColor(group, QPalette::Window, button);
        palette.setColor(group, QPalette::HighlightedText, QColor(255, 255, 255));
    }
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Button, button);
    return palette;
}

QPalette lightPalette()
{
    return makePalette(QColor(20, 20, 20), QColor(240, 240, 240), QColor(160, 160, 160));
}

QPalette darkPalette()
{
    return makePalette(QColor(235, 235, 235), QColor(45, 45, 48), QColor(110, 110, 110));
}

/// The mean lightness of the pixels an icon actually draws, weighted by how
/// much of each it covers. -1 when it draws nothing.
double inkLightness(const QPixmap& pixmap)
{
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    double sum = 0.0;
    double weight = 0.0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() < 128) continue;
            sum += pixel.lightness() * pixel.alphaF();
            weight += pixel.alphaF();
        }
    return weight > 0.0 ? sum / weight : -1.0;
}

} // namespace

class TestIcons : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { QGuiApplication::setPalette(QPalette()); }

    void everyIconIsEmbeddedAndValid();
    void everyEmbeddedFileIsInTheTable();
    void theInkFollowsThePalette();
    void disabledIsNotNormal();
    void aPaletteWithoutADisabledColourStillGreysOut();
    void changingThePaletteIsNotServedFromTheCache();
    void aHighDensityScreenGetsDevicePixels();
    void anUnknownValueIsANullIconNotACrash();
};

void TestIcons::everyIconIsEmbeddedAndValid()
{
    const QList<Icon> icons = Icons::all();
    QCOMPARE(icons.size(), int(Icon::Count));
    for (const Icon icon : icons) {
        const QString path = Icons::resourcePath(icon);
        QVERIFY2(!path.isEmpty(), qPrintable(QStringLiteral("Icon %1 is not in the table")
                                                 .arg(int(icon))));
        QVERIFY2(QFile::exists(path), qPrintable(path + QStringLiteral(" is not embedded")));

        QSvgRenderer renderer(path);
        QVERIFY2(renderer.isValid(), qPrintable(path));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        // The recolouring works by replacing this; an icon without it would stay
        // black on a dark desktop.
        QVERIFY2(file.readAll().contains("currentColor"), qPrintable(path));

        QVERIFY(!Icons::get(icon).isNull());
    }
}

void TestIcons::everyEmbeddedFileIsInTheTable()
{
    QSet<QString> named;
    for (const Icon icon : Icons::all()) {
        const QString path = Icons::resourcePath(icon);
        QVERIFY2(!named.contains(path), qPrintable(path + QStringLiteral(" is in the table twice")));
        named.insert(path);
    }

    // The CMake list, as embedded, is exactly the table: nothing listed there
    // that nothing can ask for...
    const QStringList embedded = QDir(QStringLiteral(":/icons")).entryList({ QStringLiteral("*.svg") });
    QVERIFY(!embedded.isEmpty());
    for (const QString& file : embedded) {
        const QString path = QStringLiteral(":/icons/") + file;
        QVERIFY2(named.contains(path),
                 qPrintable(path + QStringLiteral(" is embedded but not in Icons.cpp")));
    }
    // ...and nothing committed that the build does not embed.
    const QStringList committed =
        QDir(QStringLiteral(SUSPKIN_ICONS_DIR)).entryList({ QStringLiteral("*.svg") });
    QCOMPARE(committed.size(), embedded.size());
}

void TestIcons::theInkFollowsThePalette()
{
    const QSize size(32, 32);

    QGuiApplication::setPalette(lightPalette());
    for (const Icon icon : Icons::all()) {
        const double ink = inkLightness(Icons::get(icon).pixmap(size));
        QVERIFY2(ink >= 0.0, qPrintable(Icons::resourcePath(icon) + QStringLiteral(" drew nothing")));
        QVERIFY2(ink < 90.0, qPrintable(Icons::resourcePath(icon)
                                        + QStringLiteral(" is not dark on a light palette: %1")
                                              .arg(ink)));
    }

    QGuiApplication::setPalette(darkPalette());
    for (const Icon icon : Icons::all()) {
        const double ink = inkLightness(Icons::get(icon).pixmap(size));
        QVERIFY2(ink > 165.0, qPrintable(Icons::resourcePath(icon)
                                         + QStringLiteral(" is not light on a dark palette: %1")
                                               .arg(ink)));
    }
}

void TestIcons::disabledIsNotNormal()
{
    QGuiApplication::setPalette(lightPalette());
    const QIcon icon = Icons::get(Icon::Wheel);
    const QImage normal = icon.pixmap(QSize(16, 16), QIcon::Normal).toImage();
    const QImage disabled = icon.pixmap(QSize(16, 16), QIcon::Disabled).toImage();
    QVERIFY(normal != disabled);
    QVERIFY(inkLightness(QPixmap::fromImage(disabled)) > inkLightness(QPixmap::fromImage(normal)));
}

void TestIcons::aPaletteWithoutADisabledColourStillGreysOut()
{
    // Some themes give disabled text the same colour as enabled text. A
    // disabled button still has to look like one.
    QGuiApplication::setPalette(
        makePalette(QColor(20, 20, 20), QColor(240, 240, 240), QColor(20, 20, 20)));
    const QIcon icon = Icons::get(Icon::Table);
    QVERIFY(icon.pixmap(QSize(16, 16), QIcon::Normal).toImage()
            != icon.pixmap(QSize(16, 16), QIcon::Disabled).toImage());
}

void TestIcons::changingThePaletteIsNotServedFromTheCache()
{
    const QIcon icon = Icons::get(Icon::SteeringWheel);

    QGuiApplication::setPalette(lightPalette());
    const QImage light = icon.pixmap(QSize(24, 24)).toImage();
    // The same icon object, asked again: whatever was cached for the light
    // palette must not be what the dark one gets.
    QGuiApplication::setPalette(darkPalette());
    const QImage dark = icon.pixmap(QSize(24, 24)).toImage();
    QVERIFY(light != dark);
    QVERIFY(inkLightness(QPixmap::fromImage(dark)) > inkLightness(QPixmap::fromImage(light)));

    // And back again, from the cache or not, it is the light one.
    QGuiApplication::setPalette(lightPalette());
    QCOMPARE(icon.pixmap(QSize(24, 24)).toImage(), light);
}

void TestIcons::aHighDensityScreenGetsDevicePixels()
{
    QGuiApplication::setPalette(lightPalette());
    const QIcon icon = Icons::get(Icon::Cube);
    for (const qreal ratio : { 1.0, 1.5, 2.0 }) {
        const QPixmap pixmap = icon.pixmap(QSize(16, 16), ratio);
        // Rendered at the density asked for, not a 16 px image stretched.
        QCOMPARE(pixmap.width(), qRound(16 * ratio));
        QCOMPARE(pixmap.height(), qRound(16 * ratio));
        QCOMPARE(pixmap.devicePixelRatio(), ratio);
    }
}

void TestIcons::anUnknownValueIsANullIconNotACrash()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no icon for value")));
    QVERIFY(Icons::get(Icon::Count).isNull());
    QVERIFY(Icons::resourcePath(Icon::Count).isEmpty());
}

QTEST_MAIN(TestIcons)
#include "test_icons.moc"
