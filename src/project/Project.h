#pragma once

#include "model/Hardpoint.h"
#include "model/HardpointConfig.h"
#include "model/HardpointMirror.h"
#include "model/Sweep.h"
#include "model/Wheels.h"
#include "render/Camera.h"
#include "render/DisplayMode.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace suspkin {

/// Where the solver has the suspension standing, and what it is being asked.
///
/// This is view state rather than model state on purpose. Posing the mechanism
/// changes nothing about the hardpoints -- the table keeps the coordinates the
/// workbook gave it -- so keeping the pose here is what stops it leaking into
/// the edits file as changes the user never made.
struct SimulationState {
    /// Whether the viewport is showing the mechanism posed rather than at the
    /// coordinates the table holds.
    bool active = false;
    QString axle;                       ///< the corner token being swept, e.g. "F"
    SweepKind kind = SweepKind::Bump;   ///< which of the three is being swept
    /// The travel and increment of all three kinds, not just the one being
    /// swept: a project that is left in roll and reopened in bump has to come
    /// back to the wheel travel it was given, not to the roll angle.
    SweepSettings sweep;
    double position = 0.0; ///< where along that sweep the model stands
    QString measure;       ///< which curve the plot is showing
    /// Whether it is running through its travel on its own, how long one run of
    /// the travel takes, and whether every axle comes along or only the one the
    /// curve belongs to.
    bool animating = false;
    double animationSeconds = 4.0;
    bool allAxles = true;
    /// Whether the parameters window was left open. A window the user arranged
    /// is state like any other, so it comes back the way they left it.
    bool parametersOpen = false;
};

/// What the viewport looked like when the project was last saved. Restored
/// wholesale on open, so reopening a project puts the user back in front of the
/// same view they left, down to which marker was selected.
struct ViewState {
    CameraState camera;
    /// False until a view has actually been framed once, which is the signal to
    /// fit the camera to the geometry instead of restoring a meaningless one.
    bool cameraValid = false;
    DisplayMode displayMode = DisplayMode::Solid;
    bool labelsVisible = true;
    /// Whether the parts the linkage template describes are drawn between the
    /// markers. On by default: a table of points with nothing joining them is
    /// hard to read as a suspension.
    bool linksVisible = true;
    /// Whether the wheel and rim models are drawn at the wheel centres. On by
    /// default, for the same reason: they were imported to be looked at.
    bool wheelsVisible = true;
    int selectedHardpoint = -1;
    /// Where the solver had the suspension standing, so reopening a project puts
    /// the user back mid-travel if that is where they left it.
    SimulationState simulation;
};

/// The window's own layout: its size and position, and where the docks sit.
/// Opaque Qt blobs, stored per project because a project the user works on in a
/// particular arrangement should come back in that arrangement.
struct WindowState {
    QByteArray geometry;
    QByteArray dockState;

    bool isEmpty() const { return geometry.isEmpty() && dockState.isEmpty(); }
};

/// An asset the project owns: a file that was imported from somewhere on disk
/// and then copied inside, so the project keeps working when the original moves.
struct AssetRef {
    QString relativePath; ///< inside the project, e.g. "geometry/upright.step"
    QString originalPath; ///< where it was imported from, kept for reference only
    QDateTime importedAt;

    bool isEmpty() const { return relativePath.isEmpty(); }
};

/// The hardpoint side of a project: the workbook copied in, plus the sheet the
/// points were read from.
struct HardpointRef {
    AssetRef workbook;
    QString sheetName;

    /// Which points are mirrors of which, keyed by the mirrored point's name.
    ///
    /// A spreadsheet has nowhere to record this, so the project has to. Without
    /// it, mirroring a table and then writing it into the workbook would leave
    /// the tool unable to tell a mirrored point from an original one, and a
    /// second mirror pass would start producing mirrors of mirrors.
    QHash<QString, QString> mirrored;

    /// What each point is for, keyed by point name: its solver constraint, the
    /// two bodies that meet at it, the bushing that acts there.
    ///
    /// It lives here rather than in the workbook for the same reason the edits
    /// do: the workbook is the user's file and stays exactly as it was
    /// imported. It is keyed by name rather than by row because mirroring
    /// appends rows and a reimported workbook may be in a different order.
    HardpointConfigMap config;

    bool isEmpty() const { return workbook.isEmpty(); }
};

/// The wheels a project draws: the two models copied in, and the rule for where
/// the copies of them go.
///
/// Either model may be absent -- a rim on its own is a perfectly good way to see
/// where the wheels sit -- so what makes this empty is having neither.
struct WheelsRef {
    AssetRef wheel; ///< the wheel or tyre model, copied in
    AssetRef rim;   ///< the rim model, copied in
    WheelSpec spec;

    bool isEmpty() const { return wheel.isEmpty() && rim.isEmpty(); }
    bool hasModels() const { return !isEmpty(); }
};

/// A project on disk.
///
/// A project is a directory, not a single file:
///
///     MyCar/
///       project.suspkin      the manifest below, as JSON
///       geometry/            imported geometry, copied in
///       hardpoints/          the imported workbook, copied in
///         edits.json         hardpoint edits not yet written to the workbook
///       linkage/
///         template.json      which parts join which hardpoints
///       wheels/
///         wheel.step         the wheel model, copied in
///         rim.step           the rim model, copied in
///
/// Geometry and workbooks are copied rather than referenced because the whole
/// point is that a project stays openable: the file it was imported from may be
/// on a share that is not mounted, or may have moved on since.
///
/// Hardpoint edits are deliberately kept out of the workbook copy. The workbook
/// stays exactly as it was imported until the user asks for it to be
/// overwritten or exported, so there is always a known-good original to compare
/// against and to fall back to.
class Project {
public:
    /// The manifest's file name inside a project directory.
    static QString manifestName();
    /// The extension the manifest carries, for file dialogs.
    static QString fileExtension();
    static QString fileFilter();

    /// Create a project directory at @p directory and write an empty manifest.
    /// Fails rather than overwriting if a manifest is already there.
    static std::optional<Project> create(const QString& directory, const QString& name,
                                         QString* error);
    /// Open the project whose manifest is at @p manifestPath. The path to the
    /// directory is also accepted, since that is what a user thinks of as "the
    /// project".
    static std::optional<Project> open(const QString& manifestPath, QString* error);

    /// Write the manifest. Everything else in the project -- assets, the edits
    /// file -- is written as it changes, so this is the whole of "saving".
    bool save(QString* error) const;

    QString rootPath() const { return m_root; }
    QString manifestPath() const;
    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    /// Absolute path of a project-relative path, empty in, empty out.
    QString absolutePath(const QString& relativePath) const;

    /// Copy @p sourcePath into the project's @p subdirectory, replacing whatever
    /// asset was there before. Returns the new reference, or nothing on failure.
    std::optional<AssetRef> importAsset(const QString& sourcePath, const QString& subdirectory,
                                        QString* error);
    /// The same, but the copy is called @p targetFileName inside the project
    /// instead of keeping the source's own name. For assets that share a
    /// subdirectory and would otherwise collide when two files happen to be
    /// called the same thing.
    std::optional<AssetRef> importAssetAs(const QString& sourcePath, const QString& subdirectory,
                                          const QString& targetFileName, QString* error);
    /// Write @p bytes into the project as @p relativePath, creating directories.
    bool writeFile(const QString& relativePath, const QByteArray& bytes, QString* error) const;

    const AssetRef& geometry() const { return m_geometry; }
    void setGeometry(const AssetRef& asset) { m_geometry = asset; }
    void clearGeometry() { m_geometry = AssetRef{}; }

    const HardpointRef& hardpoints() const { return m_hardpoints; }
    void setHardpoints(const HardpointRef& reference) { m_hardpoints = reference; }
    void clearHardpoints() { m_hardpoints = HardpointRef{}; }

    /// The linkage template: which parts are drawn between which hardpoints.
    /// A copy inside the project like every other asset, because it is the
    /// user's to edit and a project has to keep working when the original moves.
    const AssetRef& linkageTemplate() const { return m_linkageTemplate; }
    void setLinkageTemplate(const AssetRef& asset) { m_linkageTemplate = asset; }
    void clearLinkageTemplate() { m_linkageTemplate = AssetRef{}; }

    /// The wheel and rim models, and the hardpoints they are centred on.
    const WheelsRef& wheels() const { return m_wheels; }
    void setWheels(const WheelsRef& wheels) { m_wheels = wheels; }
    void clearWheels() { m_wheels = WheelsRef{}; }

    const ViewState& view() const { return m_view; }
    void setView(const ViewState& view) { m_view = view; }

    const WindowState& window() const { return m_window; }
    void setWindow(const WindowState& window) { m_window = window; }

    const MirrorSpec& mirror() const { return m_mirror; }
    void setMirror(const MirrorSpec& spec) { m_mirror = spec; }

    /// Where the file dialogs last pointed, so a project remembers the folders
    /// its owner actually imports from.
    const QString& lastGeometryDirectory() const { return m_lastGeometryDirectory; }
    void setLastGeometryDirectory(const QString& path) { m_lastGeometryDirectory = path; }
    const QString& lastHardpointDirectory() const { return m_lastHardpointDirectory; }
    void setLastHardpointDirectory(const QString& path) { m_lastHardpointDirectory = path; }

    QDateTime created() const { return m_created; }
    QDateTime modified() const { return m_modified; }

private:
    QString m_root;
    QString m_name;
    QDateTime m_created;
    mutable QDateTime m_modified;

    AssetRef m_geometry;
    HardpointRef m_hardpoints;
    AssetRef m_linkageTemplate;
    WheelsRef m_wheels;
    ViewState m_view;
    WindowState m_window;
    MirrorSpec m_mirror;
    QString m_lastGeometryDirectory;
    QString m_lastHardpointDirectory;
};

/// The hardpoint edits a project is holding that have not been written into a
/// workbook: coordinates changed by hand, and points that were not in the
/// workbook at all because mirroring produced them.
struct HardpointEdits {
    std::vector<Hardpoint> changed; ///< in the workbook, but with different numbers
    std::vector<Hardpoint> added;   ///< not in the workbook at all
    /// In the workbook, and deleted since. Names rather than points: there is
    /// nothing left of them but which rows to leave out.
    QStringList removed;

    bool isEmpty() const { return changed.empty() && added.empty() && removed.isEmpty(); }
    int count() const
    {
        return static_cast<int>(changed.size() + added.size()) + int(removed.size());
    }
};

/// The difference between @p current and the @p baseline the workbook holds.
HardpointEdits diffHardpoints(const HardpointTable& baseline, const HardpointTable& current);

/// @p baseline with @p edits applied: changed coordinates overwritten, added
/// points appended in the order the edits file lists them.
HardpointTable applyHardpointEdits(const HardpointTable& baseline, const HardpointEdits& edits);

/// Read and write the edits file. Reading a file that is not there is not an
/// error -- it simply means nothing has been edited yet.
bool writeHardpointEdits(const QString& path, const HardpointEdits& edits, QString* error);
std::optional<HardpointEdits> readHardpointEdits(const QString& path, QString* error);

} // namespace suspkin
