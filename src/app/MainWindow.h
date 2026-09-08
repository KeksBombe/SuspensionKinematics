#pragma once

#include "io/XlsxHardpoints.h"
#include "render/Camera.h"
#include "render/ViewportWidget.h"

#include <QMainWindow>
#include <QString>
#include <QStringList>

class QAction;
class QActionGroup;
class QDockWidget;
class QLabel;
class QMenu;

namespace suspkin {

class HardpointModel;
class HardpointPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    /// Load an STL, replacing whatever is on screen. On failure the current mesh
    /// is left untouched and the reason is shown in a dialog.
    void loadFile(const QString& path);

    /// Import hardpoints from an .xlsx workbook, replacing the current set. The
    /// workbook is remembered so the points can be written straight back to it.
    void loadHardpointFile(const QString& path);

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
    void importHardpointsDialog();
    bool saveHardpoints();
    bool saveHardpointsAs();
    void closeHardpoints();

private:
    void buildActions();
    void buildMenus();
    void buildHardpointDock();
    void restoreSession();
    void rememberRecentFile(const QString& path);
    void refreshRecentMenu();
    void updateWindowTitle();
    void updateHardpointStatus();
    /// Write the current table to @p path and adopt it as the save target.
    bool writeHardpointsTo(const QString& path);
    /// Offer to save unsaved hardpoint edits. False means the user cancelled and
    /// whatever was about to happen must not.
    bool confirmDiscardHardpoints();

    ViewportWidget* m_viewport = nullptr;
    QLabel* m_meshLabel = nullptr;
    QLabel* m_hardpointLabel = nullptr;
    QLabel* m_glLabel = nullptr;

    QAction* m_importAction = nullptr;
    QAction* m_closeAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_solidAction = nullptr;
    QAction* m_trianglesAction = nullptr;
    QAction* m_fitAction = nullptr;
    QAction* m_importHardpointsAction = nullptr;
    QAction* m_saveHardpointsAction = nullptr;
    QAction* m_saveHardpointsAsAction = nullptr;
    QAction* m_closeHardpointsAction = nullptr;
    QAction* m_labelsAction = nullptr;
    QActionGroup* m_modeGroup = nullptr;
    QMenu* m_recentMenu = nullptr;

    HardpointModel* m_hardpointModel = nullptr;
    HardpointPanel* m_hardpointPanel = nullptr;
    QDockWidget* m_hardpointDock = nullptr;

    /// The workbook the points came from, kept whole so saving can rewrite the
    /// value cells and copy every other byte through unchanged.
    XlsxHardpointSource m_hardpointSource;
    QString m_hardpointPath;
    bool m_hardpointsDirty = false;

    QString m_meshPath;
    QString m_lastDirectory;
    QString m_lastHardpointDirectory;
    QStringList m_recentFiles;
};

} // namespace suspkin
