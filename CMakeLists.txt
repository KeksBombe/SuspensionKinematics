# Range form on purpose. The STM32CubeCLT toolchain puts CMake 3.28.1 on PATH
# ahead of the system's 4.4.1, so both are in daily use in this tree; pinning the
# policy maximum makes them produce identical builds.
cmake_minimum_required(VERSION 3.24...3.28)

project(SuspensionKinematics
    VERSION 0.1.0
    DESCRIPTION "Suspension kinematics tool for Bremergy"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
qt_standard_project_setup()

# Warnings that catch real portability bugs. Not -Werror: GCC 16 is new enough
# that a compiler update should not break the team's build.
add_library(suspkin_warnings INTERFACE)
if(MSVC)
    # /utf-8      source and literals are UTF-8 -- without it "Radtraeger Ø" is mangled
    # /permissive- standard name lookup, so template code stays GCC-compatible
    # /Zc:__cplusplus  otherwise __cplusplus reports C++98 forever
    target_compile_options(suspkin_warnings INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus)
    target_compile_definitions(suspkin_warnings INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(suspkin_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wdouble-promotion)
endif()

# STEP is a boundary representation, so importing it needs a geometry kernel to
# tessellate. Open CASCADE is that kernel. It is optional: without it the build
# still succeeds and STEP files report a clear message instead of loading.
option(SUSPKIN_WITH_STEP "Enable STEP import via Open CASCADE" ON)
set(SUSPKIN_STEP_ENABLED OFF)
if(SUSPKIN_WITH_STEP)
    # No version argument: OCCT ships an ExactVersion config file, so asking for
    # a minimum rejects every release that is not literally that number.
    find_package(OpenCASCADE CONFIG QUIET)
    if(OpenCASCADE_FOUND AND OpenCASCADE_VERSION VERSION_LESS 7.5)
        message(STATUS "STEP import: DISABLED -- Open CASCADE ${OpenCASCADE_VERSION} is too old (need 7.5+).")
    elseif(OpenCASCADE_FOUND)
        set(SUSPKIN_STEP_ENABLED ON)
        message(STATUS "STEP import: enabled (Open CASCADE ${OpenCASCADE_VERSION})")
    else()
        message(STATUS "STEP import: DISABLED -- Open CASCADE not found. "
                       "Set OpenCASCADE_DIR to an OCCT install to enable it.")
    endif()
endif()

# An .xlsx workbook is a ZIP of XML parts, so reading hardpoints means DEFLATE.
# zlib is the obvious answer and is already present on every Linux this builds
# on; a Windows Qt kit ships none, so the build falls back to compiling the
# release from source rather than making the developer install one by hand.
find_package(ZLIB QUIET)
if(TARGET ZLIB::ZLIB)
    set(SUSPKIN_ZLIB ZLIB::ZLIB)
    message(STATUS "zlib: system (${ZLIB_VERSION_STRING})")
else()
    include(FetchContent)
    # zlib's own install rules would otherwise land in our install tree.
    set(SKIP_INSTALL_ALL ON)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(zlib
        URL      https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23)
    FetchContent_MakeAvailable(zlib)
    # Stated explicitly because zlib's own CMakeLists has not always exported
    # them, and a duplicate include directory costs nothing.
    target_include_directories(zlibstatic PUBLIC "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
    set(SUSPKIN_ZLIB zlibstatic)
    message(STATUS "zlib: built from source (no system zlib found)")
endif()

# ---------------------------------------------------------------------------
# suspkin_core -- mesh IO and geometry. No OpenGL, no widgets, so it can be
# unit tested without a graphics context.
# ---------------------------------------------------------------------------
add_library(suspkin_core STATIC
    src/geom/Aabb.h
    src/geom/TriMesh.h
    src/geom/MeshTopology.h
    src/geom/MeshTopology.cpp
    src/io/MeshLoadResult.h
    src/io/MeshImport.h
    src/io/MeshImport.cpp
    src/io/StlReader.h
    src/io/StlReader.cpp
    src/io/Zip.h
    src/io/Zip.cpp
    src/model/Hardpoint.h
    src/io/XlsxHardpoints.h
    src/io/XlsxHardpoints.cpp
    # Camera is pure math with no GL dependency, so it lives here where the
    # tests can reach it.
    src/render/Camera.h
    src/render/Camera.cpp
)
target_include_directories(suspkin_core PUBLIC src)
target_link_libraries(suspkin_core PUBLIC Qt6::Core Qt6::Gui)

# MSVC needs both of these; setting them now costs nothing and saves a confusing
# first Windows build later.
target_link_libraries(suspkin_core PRIVATE suspkin_warnings ${SUSPKIN_ZLIB})

if(SUSPKIN_STEP_ENABLED)
    target_sources(suspkin_core PRIVATE src/io/StepReader.h src/io/StepReader.cpp)
    # SYSTEM so OCCT's own headers do not drown the build in warnings.
    target_include_directories(suspkin_core SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(suspkin_core PRIVATE
        TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo TKBRep TKTopAlgo
        TKShHealing TKMesh TKXSBase TKDESTEP)
    target_compile_definitions(suspkin_core PRIVATE SUSPKIN_HAVE_OCCT)
endif()

# ---------------------------------------------------------------------------
# suspkin -- the application
# ---------------------------------------------------------------------------
qt_add_executable(suspkin
    src/main.cpp
    src/app/MainWindow.h
    src/app/MainWindow.cpp
    src/app/HardpointModel.h
    src/app/HardpointModel.cpp
    src/app/HardpointPanel.h
    src/app/HardpointPanel.cpp
    src/render/DisplayMode.h
    src/render/GpuMesh.h
    src/render/GpuMesh.cpp
    src/render/GpuPoints.h
    src/render/GpuPoints.cpp
    src/render/NavGizmo.h
    src/render/NavGizmo.cpp
    src/render/ModeSelector.h
    src/render/ModeSelector.cpp
    src/render/ViewportWidget.h
    src/render/ViewportWidget.cpp
)
target_link_libraries(suspkin PRIVATE
    suspkin_core
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    suspkin_warnings
)
target_compile_definitions(suspkin PRIVATE SUSPKIN_VERSION="${PROJECT_VERSION}")

# Shaders are embedded rather than shipped loose: one self-contained binary, no
# runtime path resolution, and it still works from any working directory.
qt_add_resources(suspkin "shaders"
    PREFIX "/shaders"
    BASE "resources/shaders"
    FILES
        resources/shaders/solid.vert
        resources/shaders/solid.frag
        resources/shaders/line.vert
        resources/shaders/line.frag
        resources/shaders/point.vert
        resources/shaders/point.frag
)

set_target_properties(suspkin PROPERTIES WIN32_EXECUTABLE ON)

# ---------------------------------------------------------------------------
# Install / deploy
#
# Enough for both packaging paths: `cmake --install` produces a runnable tree on
# Windows (windeployqt is invoked by the generated deploy script) and a normal
# /usr layout on Linux for the PKGBUILD to hand to makepkg.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS suspkin
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION .)

if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/suspkin.desktop
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
endif()

if(WIN32)
    # Collects the Qt DLLs and plugins next to the exe. OCCT is not Qt's to
    # know about, so its DLLs are copied by the packaging workflow instead.
    qt_generate_deploy_app_script(
        TARGET suspkin
        OUTPUT_SCRIPT SUSPKIN_DEPLOY_SCRIPT
        NO_TRANSLATIONS
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${SUSPKIN_DEPLOY_SCRIPT})
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(SUSPKIN_BUILD_TESTS "Build unit tests" ON)
if(SUSPKIN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
