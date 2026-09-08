#include "io/StepReader.h"

#include "geom/MeshTopology.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>

#include <BRepAdaptor_Surface.hxx>
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
#include <gp.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <vector>

namespace suspkin {
namespace {

/// Chord height allowed between the true surface and its triangles, as a
/// fraction of the model diagonal. Same relative-scaling principle as the STL
/// weld tolerance: an absolute value would be wrong at one end of the range.
constexpr double kLinearDeflectionRatio = 0.0006;
/// Cap on the angle a single facet may span, so a small hole is not reduced to a
/// few segments just because it is tiny next to the rest of the model. 0.2 rad
/// puts at least ~31 segments around any full circle.
constexpr double kAngularDeflectionRad = 0.2;

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
        std::vector<QVector3D> cornerNormals;
        int meshedFaces = 0;
        // Reused per face so the allocation does not repeat across a big assembly.
        std::vector<QVector3D> nodeNormals;
        for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face face = TopoDS::Face(it.Current());
            TopLoc_Location location;
            const Handle(Poly_Triangulation) triangulation =
                BRep_Tool::Triangulation(face, location);
            if (triangulation.IsNull()) continue;
            ++meshedFaces;

            const gp_Trsf transform = location.Transformation();
            // A reversed face is the same surface seen from the other side, so
            // flip both the winding and the normal to keep the shell outward.
            const bool reversed = (face.Orientation() == TopAbs_REVERSED);

            // Evaluate the *analytic* surface normal at each node's UV parameter.
            // This is the whole point of importing STEP rather than a mesh: the
            // triangles are only an approximation, but the normals are exact, so
            // a bore shades as the cylinder it really is instead of a prism.
            const Standard_Integer nodeCount = triangulation->NbNodes();
            nodeNormals.assign(static_cast<std::size_t>(nodeCount) + 1, QVector3D());
            const bool exactNormals = triangulation->HasUVNodes();
            if (exactNormals) {
                const BRepAdaptor_Surface surface(face);
                for (Standard_Integer n = 1; n <= nodeCount; ++n) {
                    const gp_Pnt2d uv = triangulation->UVNode(n);
                    gp_Pnt position;
                    gp_Vec dU, dV;
                    surface.D1(uv.X(), uv.Y(), position, dU, dV);
                    gp_Vec normal = dU.Crossed(dV);
                    if (normal.SquareMagnitude() < gp::Resolution()) continue; // seam or pole
                    normal.Normalize();
                    if (reversed) normal.Reverse();
                    nodeNormals[static_cast<std::size_t>(n)] =
                        QVector3D(static_cast<float>(normal.X()), static_cast<float>(normal.Y()),
                                  static_cast<float>(normal.Z()));
                }
            }

            for (Standard_Integer i = 1; i <= triangulation->NbTriangles(); ++i) {
                Standard_Integer a = 0, b = 0, c = 0;
                triangulation->Triangle(i).Get(a, b, c);
                if (reversed) std::swap(a, b);

                for (const Standard_Integer node : { a, b, c }) {
                    const gp_Pnt p = triangulation->Node(node).Transformed(transform);
                    corners.emplace_back(static_cast<float>(p.X()), static_cast<float>(p.Y()),
                                         static_cast<float>(p.Z()));
                    // A zero here means a degenerate UV point (a cone apex or a
                    // sphere pole); weldSoup's geometric normal covers those.
                    cornerNormals.push_back(exactNormals
                                                ? nodeNormals[static_cast<std::size_t>(node)]
                                                : QVector3D());
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
        TriMesh mesh = weldSoup(corners, &dropped, &cornerNormals);
        // Patch any node whose analytic normal was undefined with the facet normal.
        if (mesh.hasCornerNormals()) {
            for (std::size_t t = 0; t < mesh.triangleCount(); ++t)
                for (int k = 0; k < 3; ++k) {
                    QVector3D& n = mesh.cornerNormals[3 * t + k];
                    if (n.lengthSquared() < 1e-12f) n = mesh.faceNormals[t];
                }
        }
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
