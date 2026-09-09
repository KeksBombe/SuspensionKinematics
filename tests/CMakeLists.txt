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

qt_add_executable(test_linkage test_linkage.cpp)
target_link_libraries(test_linkage PRIVATE suspkin_core Qt6::Test)
add_test(NAME linkage COMMAND test_linkage)

qt_add_executable(test_kinematics test_kinematics.cpp)
target_link_libraries(test_kinematics PRIVATE suspkin_core Qt6::Test)
add_test(NAME kinematics COMMAND test_kinematics)

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
