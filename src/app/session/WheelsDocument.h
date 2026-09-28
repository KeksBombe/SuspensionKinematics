#pragma once

#include "app/session/SessionMessage.h"
#include "geom/MeshTopology.h"
#include "geom/TriMesh.h"
#include "model/Wheels.h"

#include <QHash>
#include <QList>
#include <QString>

#include <optional>
#include <vector>

namespace suspkin {

class ProjectSession;
struct HardpointTable;
struct SimulationPose;

/// The tyre and rim models, read and ready for the viewport. Either may be
/// empty: a wheel can be drawn as a tyre alone.
struct WheelModels {
    TriMesh tyre;
    EdgeSet tyreEdges;
    TriMesh rim;
    EdgeSet rimEdges;
};

/// Where the wheel models are and where they are drawn.
///
/// The project's WheelsRef names hardpoints, not coordinates, so the placements
/// are resolved against the table -- posed, when the car is being simulated --
/// and kept here because they are also what the status line counts.
class WheelsDocument {
public:
    explicit WheelsDocument(ProjectSession& session);

    /// Read the models the project holds. Nothing when it holds none, which
    /// also drops the placements. @p problem says what could not be read; a
    /// model whose copy has gone is forgotten.
    std::optional<WheelModels> open(SessionMessage* problem);
    /// Take what the wheel dialog came back with: import whichever models
    /// changed, drop whichever were cleared, and keep the rest. False, with
    /// nothing changed, when a new model cannot be read; each model that read
    /// but could not be copied in adds to @p problems and the rest still apply.
    bool apply(const WheelSpec& spec, const QString& tyrePath, const QString& rimPath,
               SessionMessage* failure, QList<SessionMessage>* problems);
    /// Delete the copies of the models and forget the wheels.
    void remove();

    /// Place the models on @p table as @p pose stands it, each first turned
    /// to the static camber and toe @p designAttitudes gives its wheel centre.
    void place(const HardpointTable& table, const WheelRotations& designAttitudes,
               const SimulationPose& pose);
    const std::vector<WheelPlacement>& placements() const { return m_placements; }

    /// Point every wheel centred on a renamed point at its new name. @p renamed
    /// is old name to new.
    void followRenames(const QHash<QString, QString>& renamed);

private:
    ProjectSession& m_session;
    std::vector<WheelPlacement> m_placements;
};

} // namespace suspkin
