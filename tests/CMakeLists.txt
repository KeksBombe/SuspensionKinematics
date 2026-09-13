find_package(Qt6 6.5 QUIET COMPONENTS Test)
find_package(Python3 QUIET COMPONENTS Interpreter)

if(NOT Qt6Test_FOUND)
    message(STATUS "Qt6::Test not found -- skipping tests")
    return()
endif()
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "python3 not found -- skipping tests (needed to generate STL fixtures)")
    return()
endif()

# Generate the STL fixtures at configure time so the test binary always has
# matching data and nothing needs to be committed or downloaded.
set(SUSPKIN_TEST_DATA_DIR "${CMAKE_CURRENT_BINARY_DIR}/data")
foreach(_gen make_test_stl make_test_xlsx)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/${_gen}.py"
                "${SUSPKIN_TEST_DATA_DIR}"
        RESULT_VARIABLE _gen_result
        OUTPUT_VARIABLE _gen_output
        ERROR_VARIABLE _gen_output)
    if(NOT _gen_result EQUAL 0)
        message(FATAL_ERROR "${_gen} fixture generation failed:\n${_gen_output}")
    endif()
    # A new fixture added to a generator reconfigures, and so is written, rather
    # than the test looking for a file the last configure never made.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/../tools/${_gen}.py")
endforeach()
message(STATUS "Test fixtures generated in ${SUSPKIN_TEST_DATA_DIR}")

qt_add_executable(test_stl_reader test_stl_reader.cpp)
target_link_libraries(test_stl_reader PRIVATE suspkin_core Qt6::Test)
target_compile_definitions(test_stl_reader PRIVATE
    SUSPKIN_TEST_DATA_DIR="${SUSPKIN_TEST_DATA_DIR}")

add_test(NAME stl_reader COMMAND test_stl_reader)

qt_add_executable(test_xlsx_hardpoints test_xlsx_hardpoints.cpp)
target_link_libraries(test_xlsx_hardpoints PRIVATE suspkin_core Qt6::Test)
target_compile_definitions(test_xlsx_hardpoints PRIVATE
    SUSPKIN_TEST_DATA_DIR="${SUSPKIN_TEST_DATA_DIR}")
add_test(NAME xlsx_hardpoints COMMAND test_xlsx_hardpoints)

qt_add_executable(test_hardpoint_mirror test_hardpoint_mirror.cpp)
target_link_libraries(test_hardpoint_mirror PRIVATE suspkin_core Qt6::Test)
add_test(NAME hardpoint_mirror COMMAND test_hardpoint_mirror)

qt_add_executable(test_wheels test_wheels.cpp)
target_link_libraries(test_wheels PRIVATE suspkin_core Qt6::Test)
add_test(NAME wheels COMMAND test_wheels)

qt_add_executable(test_hardpoint_config test_hardpoint_config.cpp)
target_link_libraries(test_hardpoint_config PRIVATE suspkin_core Qt6::Test)
add_test(NAME hardpoint_config COMMAND test_hardpoint_config)

# The table model is application code, but it holds no widgets -- it is a
# QAbstractTableModel over the core types -- so the store and its validation
# boundary are testable the same way everything under it is.
qt_add_executable(test_hardpoint_model
    test_hardpoint_model.cpp
    ../src/app/HardpointModel.cpp)
target_link_libraries(test_hardpoint_model PRIVATE suspkin_core Qt6::Test)
target_include_directories(test_hardpoint_model PRIVATE ../src)
add_test(NAME hardpoint_model COMMAND test_hardpoint_model)

# The icons are application code too, but they need a palette and QtSvg, not a
# window. The test embeds the same SUSPKIN_ICONS list the application does,
# which is what ties that list to the table in Icons.cpp. Offscreen, so it never
# opens anything on a developer's desktop -- and so a build chroot with no
# display can run it at all.
qt_add_executable(test_icons
    test_icons.cpp
    ../src/app/Icons.cpp)
target_link_libraries(test_icons PRIVATE Qt6::Gui Qt6::Svg Qt6::Test)
target_include_directories(test_icons PRIVATE ../src)
qt_add_resources(test_icons "icons"
    PREFIX "/icons"
    BASE "${SUSPKIN_ICONS_DIR}"
    FILES ${SUSPKIN_ICONS})
target_compile_definitions(test_icons PRIVATE
    SUSPKIN_ICONS_DIR="${SUSPKIN_ICONS_DIR}")
add_test(NAME icons COMMAND test_icons)
set_tests_properties(icons PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

# The licence texts, read out of the binary exactly as the dialog reads them:
# the same three qt_add_resources calls as in the top-level CMakeLists, so what
# this test walks is what the application ships. A licence missing here means a
# licence missing from every copy handed out.
qt_add_executable(test_licenses
    test_licenses.cpp
    ../src/app/LicensesDialog.cpp)
target_link_libraries(test_licenses PRIVATE Qt6::Widgets Qt6::Test)
target_include_directories(test_licenses PRIVATE ../src)
qt_add_resources(test_licenses "license_own"
    PREFIX "/licenses"
    BASE "${CMAKE_SOURCE_DIR}"
    FILES "${CMAKE_SOURCE_DIR}/LICENSE")
qt_add_resources(test_licenses "license_texts"
    PREFIX "/licenses"
    BASE "${CMAKE_SOURCE_DIR}/licenses"
    FILES
        "${CMAKE_SOURCE_DIR}/licenses/THIRD-PARTY-NOTICES.md"
        "${CMAKE_SOURCE_DIR}/licenses/LGPL-3.0.txt"
        "${CMAKE_SOURCE_DIR}/licenses/LGPL-2.1.txt"
        "${CMAKE_SOURCE_DIR}/licenses/OCCT-exception.txt"
        "${CMAKE_SOURCE_DIR}/licenses/qt-third-party.txt"
        "${CMAKE_SOURCE_DIR}/licenses/zlib.txt"
        "${CMAKE_SOURCE_DIR}/licenses/mesa-llvmpipe.txt")
qt_add_resources(test_licenses "license_icons"
    PREFIX "/licenses"
    BASE "${SUSPKIN_ICONS_DIR}"
    FILES "${SUSPKIN_ICONS_DIR}/LICENSE-tabler.txt")
add_test(NAME licenses COMMAND test_licenses)
set_tests_properties(licenses PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

# Real docks in a real main window, because what it tests is how Qt reports a
# dock tabbed behind another -- which no mock would say anything true about.
qt_add_executable(test_panel_action
    test_panel_action.cpp
    ../src/app/PanelAction.cpp)
target_link_libraries(test_panel_action PRIVATE Qt6::Widgets Qt6::Test)
target_include_directories(test_panel_action PRIVATE ../src)
add_test(NAME panel_action COMMAND test_panel_action)
set_tests_properties(panel_action PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

qt_add_executable(test_linkage test_linkage.cpp)
target_link_libraries(test_linkage PRIVATE suspkin_core Qt6::Test)
add_test(NAME linkage COMMAND test_linkage)

qt_add_executable(test_kinematics test_kinematics.cpp)
target_link_libraries(test_kinematics PRIVATE suspkin_core Qt6::Test)
add_test(NAME kinematics COMMAND test_kinematics)

qt_add_executable(test_hardpoint_generator test_hardpoint_generator.cpp)
target_link_libraries(test_hardpoint_generator PRIVATE suspkin_core Qt6::Test)
add_test(NAME hardpoint_generator COMMAND test_hardpoint_generator)

qt_add_executable(test_mesh_query test_mesh_query.cpp)
target_link_libraries(test_mesh_query PRIVATE suspkin_core Qt6::Test)
add_test(NAME mesh_query COMMAND test_mesh_query)

qt_add_executable(test_project test_project.cpp)
target_link_libraries(test_project PRIVATE suspkin_core Qt6::Test)
add_test(NAME project COMMAND test_project)

qt_add_executable(test_camera test_camera.cpp)
target_link_libraries(test_camera PRIVATE suspkin_core Qt6::Test)
add_test(NAME camera COMMAND test_camera)

qt_add_executable(test_update test_update.cpp)
target_link_libraries(test_update PRIVATE suspkin_core Qt6::Test)
add_test(NAME update COMMAND test_update)

# STEP fixtures are committed rather than generated: producing one needs Open
# CASCADE, which not every build has.
if(SUSPKIN_STEP_ENABLED)
    qt_add_executable(test_step_reader test_step_reader.cpp)
    target_link_libraries(test_step_reader PRIVATE suspkin_core Qt6::Test)
    target_compile_definitions(test_step_reader PRIVATE
        SUSPKIN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_test(NAME step_reader COMMAND test_step_reader)
endif()
