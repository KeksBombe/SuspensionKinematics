#include "io/MeshImport.h"

#include "io/StlReader.h"

#ifdef SUSPKIN_HAVE_OCCT
#    include "io/StepReader.h"
#endif

#include <QCoreApplication>
#include <QFileInfo>

namespace suspkin {

bool stepImportSupported()
{
#ifdef SUSPKIN_HAVE_OCCT
    return true;
#else
    return false;
#endif
}

MeshLoadResult importMeshFile(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();

    if (suffix == QLatin1String("step") || suffix == QLatin1String("stp")) {
#ifdef SUSPKIN_HAVE_OCCT
        return readStep(path);
#else
        MeshLoadResult result;
        result.formatName = QStringLiteral("STEP");
        result.error = QCoreApplication::translate(
            "MeshImport",
            "This build has no STEP support. It needs Open CASCADE at build time; "
            "see the README for how to enable it.");
        return result;
#endif
    }

    return readStl(path);
}

QString importFileFilter()
{
    if (stepImportSupported()) {
        return QCoreApplication::translate(
            "MeshImport",
            "CAD and mesh files (*.stl *.STL *.step *.STEP *.stp *.STP);;"
            "STL meshes (*.stl *.STL);;"
            "STEP files (*.step *.STEP *.stp *.STP);;"
            "All files (*)");
    }
    return QCoreApplication::translate("MeshImport",
                                       "STL meshes (*.stl *.STL);;All files (*)");
}

} // namespace suspkin
