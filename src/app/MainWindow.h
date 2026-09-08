#pragma once

#include "render/Camera.h"
#include "render/ViewportWidget.h"

#include <QMainWindow>
#include <QString>
#include <QStringList>

class QAction;
class QActionGroup;
class QLabel;
class QMenu;

namespace suspkin {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    /// Load an STL, replacing whatever is on screen. On failure the current mesh
    /// is left untouched and the reason is shown in a dialog.
    void loadFile(const QString& path);

    /// Set the display mode and keep the menu and toolbar in step.
    void setDisplayMode(DisplayMode mode);

    /// Render one frame and return it. Used by --screenshot to verify the
    /// renderer without a human looking at the window.
    QImage captureViewport();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void importFileDialog();
    void closeModel();

private:
    void buildActions();
    void buildMenus();
    void restoreSession();
    void rememberRecentFile(const QString& path);
    void refreshRecentMenu();

    ViewportWidget* m_viewport = nullptr;
    QLabel* m_meshLabel = nullptr;
    QLabel* m_glLabel = nullptr;

    QAction* m_importAction = nullptr;
    QAction* m_closeAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_solidAction = nullptr;
    QAction* m_trianglesAction = nullptr;
    QAction* m_fitAction = nullptr;
    QActionGroup* m_modeGroup = nullptr;
    QMenu* m_recentMenu = nullptr;

    QString m_lastDirectory;
    QStringList m_recentFiles;
};

} // namespace suspkin
