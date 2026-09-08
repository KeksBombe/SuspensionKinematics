#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace suspkin {

/// The window that comes up before the application does: pick a project, make a
/// new one, or open one from anywhere on disk.
///
/// It is deliberately the only way in. The tool has no meaningful stateless
/// mode any more -- every import, edit and view belongs to a project -- so
/// there is nowhere for the main window to put anything until one is chosen.
class ProjectLauncher : public QDialog {
    Q_OBJECT

public:
    explicit ProjectLauncher(QWidget* parent = nullptr);

    /// The manifest of the project the user chose, once the dialog was accepted.
    QString chosenProject() const { return m_chosen; }

    /// Create a project by asking for a name and a folder. Also used by the main
    /// window's File menu, which is why it is not tied to this dialog's state.
    /// Returns the new project's manifest path, or an empty string.
    static QString runNewProjectDialog(QWidget* parent);
    /// Browse for an existing project. Returns its manifest path, or empty.
    static QString runOpenProjectDialog(QWidget* parent);

private slots:
    void newProject();
    void openProject();
    void openSelected();
    void removeSelected();

private:
    void reloadList();
    void updateButtons();
    void acceptProject(const QString& manifestPath);

    QListWidget* m_list = nullptr;
    QLabel* m_empty = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QString m_chosen;
};

} // namespace suspkin
