#include "app/ProjectLauncher.h"

#include "app/RecentProjects.h"
#include "project/Project.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace suspkin {
namespace {

/// The role the manifest path is stashed in, so the list item can stay a plain
/// two-line label.
constexpr int kPathRole = Qt::UserRole + 1;

/// Characters a project name may not contain, because it becomes a directory
/// name on both platforms this ships on.
bool isUsableName(const QString& name)
{
    static const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    if (name.trimmed().isEmpty()) return false;
    for (const QChar ch : name)
        if (forbidden.contains(ch)) return false;
    return true;
}

/// Ask for a name and a parent folder, and show exactly what will be created.
class NewProjectDialog : public QDialog {
public:
    explicit NewProjectDialog(QWidget* parent) : QDialog(parent)
    {
        setWindowTitle(tr("New project"));

        m_name = new QLineEdit(tr("Untitled"), this);
        m_name->selectAll();

        m_location = new QLineEdit(RecentProjects::lastBrowseDirectory(), this);
        auto* browse = new QPushButton(tr("Browse..."), this);

        auto* locationRow = new QWidget(this);
        auto* locationLayout = new QHBoxLayout(locationRow);
        locationLayout->setContentsMargins(0, 0, 0, 0);
        locationLayout->addWidget(m_location, 1);
        locationLayout->addWidget(browse);

        m_preview = new QLabel(this);
        m_preview->setWordWrap(true);
        QFont previewFont = m_preview->font();
        previewFont.setPointSizeF(previewFont.pointSizeF() * 0.9);
        m_preview->setFont(previewFont);

        m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));

        auto* form = new QFormLayout;
        form->addRow(tr("Project name:"), m_name);
        form->addRow(tr("Location:"), locationRow);

        auto* layout = new QVBoxLayout(this);
        layout->addLayout(form);
        layout->addWidget(m_preview);
        layout->addStretch(1);
        layout->addWidget(m_buttons);

        connect(browse, &QPushButton::clicked, this, [this] {
            const QString directory = QFileDialog::getExistingDirectory(
                this, tr("Where should the project folder go?"), m_location->text());
            if (!directory.isEmpty()) m_location->setText(directory);
        });
        connect(m_name, &QLineEdit::textChanged, this, [this] { refresh(); });
        connect(m_location, &QLineEdit::textChanged, this, [this] { refresh(); });
        connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        refresh();
        resize(560, sizeHint().height());
    }

    QString projectName() const { return m_name->text().trimmed(); }
    QString projectRoot() const
    {
        return QDir(m_location->text().trimmed()).filePath(projectName());
    }

private:
    void refresh()
    {
        const bool nameOk = isUsableName(m_name->text());
        const bool locationOk = !m_location->text().trimmed().isEmpty();

        if (!nameOk) {
            m_preview->setText(tr("A project name cannot be empty or contain / \\ : * ? \" < > |"));
        } else if (!locationOk) {
            m_preview->setText(tr("Choose a folder to create the project in."));
        } else if (QFileInfo::exists(QDir(projectRoot()).filePath(Project::manifestName()))) {
            m_preview->setText(tr("There is already a project in %1.")
                                   .arg(QDir::toNativeSeparators(projectRoot())));
        } else {
            m_preview->setText(tr("Creates %1")
                                   .arg(QDir::toNativeSeparators(
                                       QDir(projectRoot()).filePath(Project::manifestName()))));
        }
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(nameOk && locationOk);
    }

    QLineEdit* m_name = nullptr;
    QLineEdit* m_location = nullptr;
    QLabel* m_preview = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

QString describe(const RecentProject& project)
{
    QString detail = QDir::toNativeSeparators(project.directory());
    if (!project.exists()) return detail + QCoreApplication::translate("ProjectLauncher", "   (missing)");
    if (project.openedAt.isValid()) {
        detail += QStringLiteral("   ")
            + QLocale().toString(project.openedAt.toLocalTime(), QLocale::ShortFormat);
    }
    return detail;
}

} // namespace

ProjectLauncher::ProjectLauncher(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("SuspensionKinematics"));

    auto* heading = new QLabel(tr("Open a project"), this);
    QFont headingFont = heading->font();
    headingFont.setPointSizeF(headingFont.pointSizeF() * 1.4);
    headingFont.setBold(true);
    heading->setFont(headingFont);

    auto* subheading = new QLabel(
        tr("Every geometry import, hardpoint edit and view setting is kept in the project "
           "you choose here."),
        this);
    subheading->setWordWrap(true);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(false);

    m_empty = new QLabel(tr("No projects yet. Create one to get started."), this);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setEnabled(false);

    auto* newButton = new QPushButton(tr("&New Project..."), this);
    auto* browseButton = new QPushButton(tr("&Open Project..."), this);
    m_openButton = new QPushButton(tr("Open"), this);
    m_openButton->setDefault(true);
    m_removeButton = new QPushButton(tr("Remove from list"), this);
    auto* quitButton = new QPushButton(tr("Quit"), this);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(newButton);
    buttons->addWidget(browseButton);
    buttons->addStretch(1);
    buttons->addWidget(m_removeButton);
    buttons->addWidget(m_openButton);
    buttons->addWidget(quitButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);
    layout->addWidget(heading);
    layout->addWidget(subheading);
    layout->addSpacing(6);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_empty, 1);
    layout->addLayout(buttons);

    connect(newButton, &QPushButton::clicked, this, &ProjectLauncher::newProject);
    connect(browseButton, &QPushButton::clicked, this, &ProjectLauncher::openProject);
    connect(m_openButton, &QPushButton::clicked, this, &ProjectLauncher::openSelected);
    connect(m_removeButton, &QPushButton::clicked, this, &ProjectLauncher::removeSelected);
    connect(quitButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &ProjectLauncher::openSelected);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &ProjectLauncher::updateButtons);

    reloadList();
    resize(640, 460);
}

void ProjectLauncher::reloadList()
{
    m_list->clear();
    const std::vector<RecentProject> projects = RecentProjects::load();
    for (const RecentProject& project : projects) {
        auto* item = new QListWidgetItem(m_list);
        item->setText(project.name + QLatin1Char('\n') + describe(project));
        item->setData(kPathRole, project.manifestPath);
        item->setToolTip(QDir::toNativeSeparators(project.manifestPath));
        // A project whose folder is gone stays listed rather than vanishing:
        // seeing it and being told why is more use than it silently disappearing.
        if (!project.exists()) item->setForeground(palette().brush(QPalette::Disabled,
                                                                   QPalette::Text));
    }

    const bool empty = m_list->count() == 0;
    m_list->setVisible(!empty);
    m_empty->setVisible(empty);
    if (!empty) m_list->setCurrentRow(0);
    updateButtons();
}

void ProjectLauncher::updateButtons()
{
    const bool hasSelection = m_list->currentItem() != nullptr && m_list->isVisible();
    m_openButton->setEnabled(hasSelection);
    m_removeButton->setEnabled(hasSelection);
}

void ProjectLauncher::acceptProject(const QString& manifestPath)
{
    m_chosen = manifestPath;
    QDialog::accept();
}

void ProjectLauncher::newProject()
{
    const QString path = runNewProjectDialog(this);
    if (!path.isEmpty()) acceptProject(path);
}

void ProjectLauncher::openProject()
{
    const QString path = runOpenProjectDialog(this);
    if (!path.isEmpty()) acceptProject(path);
}

void ProjectLauncher::openSelected()
{
    const QListWidgetItem* item = m_list->currentItem();
    if (!item) return;

    const QString path = item->data(kPathRole).toString();
    if (!QFileInfo::exists(path)) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Project not found"),
            tr("%1 is no longer there.\n\nRemove it from the list?")
                .arg(QDir::toNativeSeparators(path)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes) {
            RecentProjects::forget(path);
            reloadList();
        }
        return;
    }
    acceptProject(path);
}

void ProjectLauncher::removeSelected()
{
    const QListWidgetItem* item = m_list->currentItem();
    if (!item) return;
    // Only the list entry: the project on disk is the user's, and a launcher is
    // not where anyone expects files to be deleted.
    RecentProjects::forget(item->data(kPathRole).toString());
    reloadList();
}

QString ProjectLauncher::runNewProjectDialog(QWidget* parent)
{
    NewProjectDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted) return {};

    const QString root = dialog.projectRoot();
    QString error;
    const std::optional<Project> project = Project::create(root, dialog.projectName(), &error);
    if (!project) {
        QMessageBox::warning(parent, tr("Cannot create the project"),
                             tr("The project could not be created.\n\n%1").arg(error));
        return {};
    }

    RecentProjects::setLastBrowseDirectory(QFileInfo(root).absolutePath());
    RecentProjects::remember(project->manifestPath(), project->name());
    return project->manifestPath();
}

QString ProjectLauncher::runOpenProjectDialog(QWidget* parent)
{
    const QString path = QFileDialog::getOpenFileName(
        parent, tr("Open project"), RecentProjects::lastBrowseDirectory(), Project::fileFilter());
    if (path.isEmpty()) return {};

    QString error;
    const std::optional<Project> project = Project::open(path, &error);
    if (!project) {
        QMessageBox::warning(parent, tr("Cannot open the project"),
                             tr("The project could not be opened.\n\n%1").arg(error));
        return {};
    }

    RecentProjects::setLastBrowseDirectory(QFileInfo(project->rootPath()).absolutePath());
    RecentProjects::remember(project->manifestPath(), project->name());
    return project->manifestPath();
}

} // namespace suspkin
