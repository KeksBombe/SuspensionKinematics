#include "app/LicensesDialog.h"
#include "app/UpdateChecker.h"
#include "app/framework/AppContext.h"
#include "app/framework/CommandRegistry.h"
#include "app/framework/Feature.h"
#include "app/framework/FeatureRegistry.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

namespace suspkin {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("HelpFeature", text); }

// Long enough that the window is up and the project loaded before the network
// is touched, short enough that the answer arrives while the user is still at
// the start of a session.
constexpr int kUpdateCheckDelayMs = 2500;

/// What this copy is, what it is built on, and whether there is a newer one.
///
/// It owns the update checker outright: nothing else in the application has
/// anything to do with updating, so nothing else should be holding it.
class HelpFeature : public Feature {
public:
    explicit HelpFeature(AppContext& context) : m_context(context) {}

    QString id() const override { return QStringLiteral("help"); }

    void registerCommands(CommandRegistry& commands) override
    {
        registerUpdateCommands(commands);
        registerAboutCommands(commands);
    }

    void windowReady() override { startUpdateChecker(); }

private:
    void registerUpdateCommands(CommandRegistry& commands)
    {
        const bool supported = UpdateChecker::updatesSupported();
        const QString page = QStringLiteral("help");
        const QString group = tr("Updates");

        // Disabled rather than hidden for a portable or developer build, so the
        // absence is visible and the tip can say why.
        QAction* check = commands.add({
            .id = QStringLiteral("help.checkUpdates"),
            .text = tr("Check for &Updates..."),
            .icon = Icon::CloudDownload,
            .iconText = tr("Check for\nUpdates"),
            .statusTip =
                supported
                    ? tr("Ask GitHub whether there is a newer build, and offer to install it.")
                    : tr("Only a copy put here by the installer can update itself."),
            .ribbon = { { page, group, RibbonButton::Large, nullptr, 10 } },
            .run = [this] { if (m_updates) m_updates->check(/*userAsked=*/true); },
        });
        check->setEnabled(supported);

        QAction* automatic = commands.add({
            .id = QStringLiteral("help.autoUpdate"),
            .text = tr("Check for Updates on Start&up"),
            .icon = Icon::Refresh,
            .iconText = tr("On Startup"),
            .statusTip =
                tr("Ask once, shortly after the window opens, whether there is a newer build."),
            .checkable = true,
            .checkedByDefault = UpdateChecker::checkOnStartup(),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 20 } },
            .onToggled = [](bool on) { UpdateChecker::setCheckOnStartup(on); },
        });
        automatic->setEnabled(supported);
    }

    void registerAboutCommands(CommandRegistry& commands)
    {
        const QString page = QStringLiteral("help");
        const QString group = tr("Program");

        commands.add({
            .id = QStringLiteral("help.about"),
            .text = tr("&About SuspensionKinematics"),
            .icon = Icon::InfoCircle,
            .iconText = tr("About"),
            .ribbon = { { page, group, RibbonButton::Large, nullptr, 30 } },
            .run = [this] { showAboutBox(); },
        });

        // Not a courtesy: Qt and Open CASCADE are LGPL and the icons are MIT,
        // and all three ask that their licence travel with every copy of the
        // program. This is how it travels -- the texts are compiled into the
        // binary, so a portable unzip carries them whether or not anyone kept
        // the folder.
        commands.add({
            .id = QStringLiteral("help.licenses"),
            .text = tr("&Licenses..."),
            .icon = Icon::License,
            .statusTip = tr("The licence of this program and of everything it is built on."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 40 } },
            .run = [this] { showLicenses(); },
        });

        // Qt's own box, which states the Qt version and its licence in Qt's
        // words.
        commands.add({
            .id = QStringLiteral("help.aboutQt"),
            .text = tr("About &Qt"),
            .icon = Icon::InfoSquareRounded,
            .statusTip = tr("The version of Qt this copy was built against."),
            .ribbon = { { page, group, RibbonButton::Small, nullptr, 50 } },
            .run = [this] { QMessageBox::aboutQt(m_context.window()); },
        });
    }

    /// Wire the checker up and, unless the user has turned it off, ask GitHub
    /// once shortly after the window is up.
    void startUpdateChecker()
    {
        if (!UpdateChecker::updatesSupported()) return;

        QWidget* window = m_context.window();
        m_updates = new UpdateChecker(window);
        QObject::connect(m_updates, &UpdateChecker::updateAvailable, window,
                         [this](const UpdateRelease& release, bool userAsked) {
                             offerUpdate(release, userAsked);
                         });
        QObject::connect(m_updates, &UpdateChecker::upToDate, window, [window] {
            QMessageBox::information(window, tr("No update"), tr("This is the newest build."));
        });
        QObject::connect(m_updates, &UpdateChecker::failed, window,
                         [window](const QString& message, bool userAsked) {
                             // A background check on a machine that is offline,
                             // or behind a proxy that blocks GitHub, must not
                             // put a dialog in front of someone who never asked
                             // about updates.
                             if (userAsked) QMessageBox::warning(window, tr("Update"), message);
                             else qInfo("Update check: %s", qPrintable(message));
                         });

        if (!UpdateChecker::checkOnStartup()) return;
        // Not during construction: the window should be up and usable first, and
        // a project still opening should not compete with the network.
        QTimer::singleShot(kUpdateCheckDelayMs, window,
                           [this] { m_updates->check(/*userAsked=*/false); });
    }

    /// Offer @p release, and install it if the user accepts. @p userAsked
    /// distinguishes the startup check from the command.
    void offerUpdate(const UpdateRelease& release, bool userAsked)
    {
        QWidget* window = m_context.window();
        QMessageBox box(window);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("Update available"));
        box.setText(tr("<b>Build %1 is available.</b>").arg(release.build));
        box.setInformativeText(
            tr("This copy is build %1. The update is about %2 MB and installs itself; "
               "the application restarts when it is done.")
                .arg(UpdateChecker::currentBuild())
                .arg(QString::number(release.assetSize / (1024.0 * 1024.0), 'f', 1)));

        QPushButton* update = box.addButton(tr("Update Now"), QMessageBox::AcceptRole);
        box.addButton(tr("Not Now"), QMessageBox::RejectRole);
        // Only worth offering when the application raised this by itself; asking
        // from the command and being told "never mind this one" would be odd.
        QPushButton* skip = userAsked
                                ? nullptr
                                : box.addButton(tr("Skip This Build"),
                                                QMessageBox::DestructiveRole);
        box.setDefaultButton(update);
        box.exec();

        if (box.clickedButton() == skip) {
            UpdateChecker::skipBuild(release.build);
            return;
        }
        if (box.clickedButton() != update) return;

        // The project is written out before anything replaces the binary, so an
        // update can never be what loses someone's work.
        m_context.saveProject();
        downloadAndInstall(release);
    }

    void downloadAndInstall(const UpdateRelease& release)
    {
        QWidget* window = m_context.window();
        auto* progress =
            new QProgressDialog(tr("Downloading the update..."), tr("Cancel"), 0, 100, window);
        progress->setWindowTitle(tr("Update"));
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(0);
        progress->setAutoClose(false);
        progress->setAutoReset(false);
        progress->setValue(0);

        QObject::connect(m_updates, &UpdateChecker::downloadProgress, progress,
                         [progress](qint64 received, qint64 total) {
                             if (total <= 0) return;
                             progress->setMaximum(100);
                             progress->setValue(static_cast<int>(received * 100 / total));
                         });
        QObject::connect(m_updates, &UpdateChecker::installerStarted, progress, [progress] {
            progress->close();
            progress->deleteLater();
            // The installer is waiting for this process to let go of its files.
            QCoreApplication::quit();
        });
        QObject::connect(m_updates, &UpdateChecker::failed, progress, [progress] {
            progress->close();
            progress->deleteLater();
        });
        QObject::connect(progress, &QProgressDialog::canceled, progress, &QProgressDialog::close);

        m_updates->downloadAndInstall(release);
    }

    void showAboutBox()
    {
        const int build = UpdateChecker::currentBuild();
        const QString version = build > 0
                                    ? tr("Version %1 (build %2)")
                                          .arg(QCoreApplication::applicationVersion())
                                          .arg(build)
                                    : tr("Version %1 (local build)")
                                          .arg(QCoreApplication::applicationVersion());
        QMessageBox::about(
            m_context.window(), tr("About SuspensionKinematics"),
            tr("<h3>SuspensionKinematics</h3><p>%1</p>"
               "<p>Suspension kinematics for Bremergy: CAD geometry and a hardpoint "
               "workbook in one 3D viewport.</p>"
               "<p>Copyright &copy; 2026 Bremergy. Free software under the "
               "<b>GNU General Public License, version 3 or later</b>: anyone may use it, "
               "change it and pass it on, under the same licence and with its source. "
               "It comes with <b>absolutely no warranty</b>.</p>"
               "<p>It is built on Qt 6, used under the GNU Lesser General Public License "
               "v3, and on Open CASCADE Technology, used under the GNU LGPL v2.1 with the "
               "Open CASCADE exception; both remain the copyright of their own authors. "
               "<b>Help &gt; Licenses</b> has every licence text in full and says where "
               "the source of each can be had.</p>")
                .arg(version));
    }

    void showLicenses()
    {
        LicensesDialog dialog(m_context.window());
        dialog.exec();
    }

    AppContext& m_context;
    UpdateChecker* m_updates = nullptr;
};

} // namespace

SUSPKIN_FEATURE(HelpFeature)

} // namespace suspkin
