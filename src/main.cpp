#include "app/MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QImage>
#include <QSurfaceFormat>
#include <QTimer>

int main(int argc, char* argv[])
{
    // The default format has to be set before any QOpenGLWidget is constructed,
    // so it happens here rather than in the widget.
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4); // MSAA is only a hint, but it transforms line quality
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Bremergy"));
    QApplication::setApplicationName(QStringLiteral("SuspensionKinematics"));
    QApplication::setApplicationVersion(QStringLiteral(SUSPKIN_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Suspension kinematics tool - 3D viewport for STL and STEP geometry"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("files"),
        QStringLiteral("Files to open on startup: STL or STEP geometry, and/or an .xlsx "
                       "hardpoint workbook."),
        QStringLiteral("[files...]"));

    const QCommandLineOption modeOption(
        QStringLiteral("mode"),
        QStringLiteral("Initial display mode: solid or triangles (default solid)."),
        QStringLiteral("mode"));
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Render one frame to this PNG and exit. For verification and CI."),
        QStringLiteral("path"));
    parser.addOption(modeOption);
    parser.addOption(screenshotOption);
    parser.process(app);

    suspkin::MainWindow window;
    window.show();

    if (parser.isSet(modeOption)) {
        const QString mode = parser.value(modeOption).toLower();
        if (mode == QLatin1String("solid"))
            window.setDisplayMode(suspkin::DisplayMode::Solid);
        else if (mode == QLatin1String("triangles"))
            window.setDisplayMode(suspkin::DisplayMode::Triangles);
        else
            qWarning("Unknown --mode '%s'; keeping the default.", qPrintable(mode));
    }

    // Dispatch on the extension rather than on argument order, so geometry and
    // hardpoints can be given in either order, or one without the other.
    for (const QString& file : parser.positionalArguments()) {
        if (QFileInfo(file).suffix().compare(QLatin1String("xlsx"), Qt::CaseInsensitive) == 0)
            window.loadHardpointFile(file);
        else
            window.loadFile(file);
    }

    if (parser.isSet(screenshotOption)) {
        const QString path = parser.value(screenshotOption);
        // Queued so the widget has a live context and one painted frame first.
        QTimer::singleShot(0, &window, [&window, path] {
            const QImage frame = window.captureViewport();
            if (frame.isNull() || !frame.save(path)) {
                qCritical("Could not write screenshot to %s", qPrintable(path));
                QCoreApplication::exit(1);
                return;
            }
            qInfo("Wrote %dx%d screenshot to %s", frame.width(), frame.height(),
                  qPrintable(path));
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
