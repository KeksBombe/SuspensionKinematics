#include "io/StepReader.h"

#include "geom/MeshTopology.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>

#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <cmath>
#include <vector>

namespace suspkin {
namespace {

/// Chord height allowed between the true surface and its triangles, as a
/// fraction of the model diagonal. Same relative-scaling principle as the STL
/// weld tolerance: an absolute value would be wrong at one end of the range.
constexpr double kLinearDeflectionRatio = 0.0015;
/// Cap on the angle between adjacent facet normals, so small holes and fillets
/// stay round even when they are tiny next to the overall model.
constexpr double kAngularDeflectionRad = 0.35;

} // namespace

MeshLoadResult readStep(const QString& path)
{
    MeshLoadResult result;
    result.formatName = QStringLiteral("STEP");

    QElapsedTimer timer;
    timer.start();

    if (!QFile::exists(path)) {
        result.error = QCoreApplication::translate("StepReader", "File does not exist.");
        return result;
    }

    try {
        STEPControl_Reader reader;
        const QByteArray localPath = QFile::encodeName(path);
        if (reader.ReadFile(localPath.constData()) != IFSelect_RetDone) {
            result.error = QCoreApplication::translate(
                "StepReader", "Not a readable STEP file (the header could not be parsed).");
            return result;
        }

        if (reader.NbRootsForTransfer() < 1) {
            result.error = QCoreApplication::translate(
                "StepReader", "The STEP file contains no transferable geometry.");
            return result;
        }
        reader.TransferRoots();

        const TopoDS_Shape shape = reader.OneShape();
        if (shape.IsNull()) {
            result.error = QCoreApplication::translate(
                "StepReader", "The STEP file produced no usable shape.");
            return result;
        }

        // Measure the shape from its exact geometry (no triangulation exists yet)
        // so the meshing tolerance can be scaled to it.
        Bnd_Box box;
        BRepBndLib::Add(shape, box, Standard_False);
        double diagonal = 1.0;
        if (!box.IsVoid()) {
            Standard_Real xMin, yMin, zMin, xMax, yMax, zMax;
            box.Get(xMin, yMin, zMin, xMax, yMax, zMax);
            const double dx = xMax - xMin;
            const double dy = yMax - yMin;
            const double dz = zMax - zMin;
            diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        const double deflection = std::max(kLinearDeflectionRatio * diagonal, 1e-6);

        BRepMesh_IncrementalMesh mesher(shape, deflection, Standard_False,
                                        kAngularDeflectionRad, Standard_True);
        Q_UNUSED(mesher);

        std::vector<QVector3D> corners;
        int meshedFaces = 0;
        for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face face = TopoDS::Face(it.Current());
            TopLoc_Location location;
            const Handle(Poly_Triangulation) triangulation =
                BRep_Tool::Triangulation(face, location);
            if (triangulation.IsNull()) continue;
            ++meshedFaces;

            const gp_Trsf transform = location.Transformation();
            // A reversed face is the same surface seen from the other side, so
            // flip the winding to keep the whole shell consistently outward.
            const bool reversed = (face.Orientation() == TopAbs_REVERSED);

            for (Standard_Integer i = 1; i <= triangulation->NbTriangles(); ++i) {
                Standard_Integer a = 0, b = 0, c = 0;
                triangulation->Triangle(i).Get(a, b, c);
                if (reversed) std::swap(a, b);

                for (const Standard_Integer node : { a, b, c }) {
                    const gp_Pnt p = triangulation->Node(node).Transformed(transform);
                    corners.emplace_back(static_cast<float>(p.X()), static_cast<float>(p.Y()),
                                         static_cast<float>(p.Z()));
                }
            }
        }

        if (corners.empty()) {
            result.error = QCoreApplication::translate(
                "StepReader",
                "The STEP file has no meshable faces (%1 face(s) found, none tessellated).")
                               .arg(meshedFaces);
            return result;
        }

        int dropped = 0;
        TriMesh mesh = weldSoup(corners, &dropped);
        if (mesh.isEmpty()) {
            result.error = QCoreApplication::translate(
                "StepReader", "Tessellation produced no usable triangles.");
            return result;
        }

        result.skippedDegenerate = dropped;
        result.mesh = std::move(mesh);
        result.elapsedMs = timer.elapsed();
        return result;
    } catch (const Standard_Failure& failure) {
        // Open CASCADE reports problems by throwing; a malformed file must not
        // take the application down with it.
        const Standard_CString message = failure.GetMessageString();
        result.error = QCoreApplication::translate("StepReader", "Open CASCADE failed: %1")
                           .arg(QString::fromLocal8Bit(message ? message : "unknown error"));
        return result;
    }
}

} // namespace suspkin
