#include "app/AppController.h"
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
    // Between one project and the next there is a moment with no window open,
    // and Qt would take that for the end of the run.
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Suspension kinematics tool - 3D viewport for STL and STEP geometry"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("project"),
        QStringLiteral("A project to open (its project.suspkin, or the folder holding it). "
                       "Without one, the project list comes up first."),
        QStringLiteral("[project]"));

    const QCommandLineOption importOption(
        QStringLiteral("import"),
        QStringLiteral("Import a file into the project on startup: STL or STEP geometry, or an "
                       ".xlsx hardpoint workbook. May be given more than once."),
        QStringLiteral("path"));
    const QCommandLineOption modeOption(
        QStringLiteral("mode"),
        QStringLiteral("Initial display mode: solid or triangles (default solid)."),
        QStringLiteral("mode"));
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Render one frame to this PNG and exit. For verification and CI."),
        QStringLiteral("path"));
    const QCommandLineOption windowScreenshotOption(
        QStringLiteral("screenshot-window"),
        QStringLiteral("Render the whole window -- menu bar, ribbon, docks and viewport -- to this "
                       "PNG and exit. For checking the window's chrome the way --screenshot "
                       "checks the renderer."),
        QStringLiteral("path"));
    parser.addOption(importOption);
    parser.addOption(modeOption);
    parser.addOption(screenshotOption);
    parser.addOption(windowScreenshotOption);
    parser.process(app);

    suspkin::AppController controller;
    const QStringList positional = parser.positionalArguments();
    const bool opened = positional.isEmpty() ? controller.startFromLauncher()
                                             : controller.openProject(positional.first());
    if (!opened) return 0;

    suspkin::MainWindow* window = controller.window();

    if (parser.isSet(modeOption)) {
        const QString mode = parser.value(modeOption).toLower();
        if (mode == QLatin1String("solid"))
            window->setDisplayMode(suspkin::DisplayMode::Solid);
        else if (mode == QLatin1String("triangles"))
            window->setDisplayMode(suspkin::DisplayMode::Triangles);
        else
            qWarning("Unknown --mode '%s'; keeping the default.", qPrintable(mode));
    }

    // Dispatch on the extension rather than on argument order, so geometry and
    // hardpoints can be given in either order, or one without the other.
    for (const QString& file : parser.values(importOption)) {
        if (QFileInfo(file).suffix().compare(QLatin1String("xlsx"), Qt::CaseInsensitive) == 0)
            window->loadHardpointFile(file);
        else
            window->loadFile(file);
    }

    if (parser.isSet(screenshotOption) || parser.isSet(windowScreenshotOption)) {
        const QString viewportPath = parser.value(screenshotOption);
        const QString windowPath = parser.value(windowScreenshotOption);
        // Queued so the widget has a live context and one painted frame first.
        QTimer::singleShot(0, window, [window, viewportPath, windowPath] {
            const auto write = [](const QImage& image, const QString& path) {
                if (image.isNull() || !image.save(path)) {
                    qCritical("Could not write screenshot to %s", qPrintable(path));
                    return false;
                }
                qInfo("Wrote %dx%d screenshot to %s", image.width(), image.height(),
                      qPrintable(path));
                return true;
            };
            bool ok = true;
            if (!viewportPath.isEmpty()) ok = write(window->captureViewport(), viewportPath) && ok;
            if (!windowPath.isEmpty()) ok = write(window->captureWindow(), windowPath) && ok;
            if (ok)
                QCoreApplication::quit();
            else
                QCoreApplication::exit(1);
        });
    }

    return app.exec();
}
