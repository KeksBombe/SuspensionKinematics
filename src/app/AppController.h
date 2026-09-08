#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace suspkin {

class MainWindow;

/// Owns the one window there is, and the project it has open.
///
/// Switching projects means tearing the window down and building a new one, so
/// nothing from the previous project can survive into the next by accident.
/// That is what this class is for: the window asks for a different project, and
/// the request is honoured once the window has finished handling the event it
/// asked from.
class AppController : public QObject {
    Q_OBJECT

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    /// Show the project list and open whatever the user picks. False means they
    /// chose to quit instead, and there is nothing to run.
    bool startFromLauncher();
    /// Open a project straight away, for the command line. False on failure,
    /// with the reason already in front of the user.
    bool openProject(const QString& manifestPath);

    MainWindow* window() const { return m_window.get(); }

private:
    /// Back to the project list; quits the application if the user cancels it.
    void showLauncher();
    void replaceWindow(std::unique_ptr<MainWindow> window);

    std::unique_ptr<MainWindow> m_window;
    /// Set while one window is being swapped for another, so the outgoing
    /// window's close is not read as the user quitting.
    bool m_switching = false;
};

} // namespace suspkin
