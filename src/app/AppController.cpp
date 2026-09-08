#include "app/AppController.h"

#include "app/MainWindow.h"
#include "app/ProjectLauncher.h"
#include "project/Project.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QTimer>

#include <utility>

namespace suspkin {

AppController::AppController(QObject* parent) : QObject(parent) {}
AppController::~AppController() = default;

bool AppController::startFromLauncher()
{
    ProjectLauncher launcher;
    if (launcher.exec() != QDialog::Accepted) return false;
    return openProject(launcher.chosenProject());
}

bool AppController::openProject(const QString& manifestPath)
{
    QString error;
    std::optional<Project> project = Project::open(manifestPath, &error);
    if (!project) {
        QMessageBox::warning(
            nullptr, QCoreApplication::translate("AppController", "Cannot open the project"),
            QCoreApplication::translate("AppController", "The project could not be opened.\n\n%1")
                .arg(error));
        return false;
    }

    auto window = std::make_unique<MainWindow>(std::move(*project));

    connect(window.get(), &MainWindow::openProjectRequested, this,
            [this](const QString& path) { openProject(path); }, Qt::QueuedConnection);
    connect(window.get(), &MainWindow::projectListRequested, this, [this] { showLauncher(); },
            Qt::QueuedConnection);
    // Closing the window is how the user quits. quitOnLastWindowClosed is off,
    // because between two projects there is a moment with no window at all.
    connect(window.get(), &MainWindow::closed, this, [this] {
        if (!m_switching) QCoreApplication::quit();
    });

    window->show();
    replaceWindow(std::move(window));
    return true;
}

void AppController::replaceWindow(std::unique_ptr<MainWindow> window)
{
    if (m_window) {
        m_switching = true;
        // A real close, not a bare delete: the outgoing window writes its project
        // out in closeEvent().
        m_window->close();
        m_switching = false;
    }
    m_window = std::move(window);
}

void AppController::showLauncher()
{
    // The window goes first, so the project list is not sitting on top of a
    // project the user has just left.
    if (m_window) {
        m_switching = true;
        m_window->close();
        m_switching = false;
        m_window.reset();
    }

    if (!startFromLauncher()) QCoreApplication::quit();
}

} // namespace suspkin
