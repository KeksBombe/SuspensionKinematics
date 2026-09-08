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
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/make_test_stl.py"
            "${SUSPKIN_TEST_DATA_DIR}"
    RESULT_VARIABLE _gen_result
    OUTPUT_VARIABLE _gen_output
    ERROR_VARIABLE _gen_output)
if(NOT _gen_result EQUAL 0)
    message(FATAL_ERROR "STL fixture generation failed:\n${_gen_output}")
endif()
message(STATUS "STL test fixtures generated in ${SUSPKIN_TEST_DATA_DIR}")

qt_add_executable(test_stl_reader test_stl_reader.cpp)
target_link_libraries(test_stl_reader PRIVATE suspkin_core Qt6::Test)
target_compile_definitions(test_stl_reader PRIVATE
    SUSPKIN_TEST_DATA_DIR="${SUSPKIN_TEST_DATA_DIR}")

add_test(NAME stl_reader COMMAND test_stl_reader)

qt_add_executable(test_camera test_camera.cpp)
target_link_libraries(test_camera PRIVATE suspkin_core Qt6::Test)
add_test(NAME camera COMMAND test_camera)

# STEP fixtures are committed rather than generated: producing one needs Open
# CASCADE, which not every build has.
if(SUSPKIN_STEP_ENABLED)
    qt_add_executable(test_step_reader test_step_reader.cpp)
    target_link_libraries(test_step_reader PRIVATE suspkin_core Qt6::Test)
    target_compile_definitions(test_step_reader PRIVATE
        SUSPKIN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_test(NAME step_reader COMMAND test_step_reader)
endif()
