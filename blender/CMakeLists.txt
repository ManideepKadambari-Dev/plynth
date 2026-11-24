# Python tests for the Blender worker. They run inside the Blender found on this machine and are skipped
# when none is found.
set(PLYNTH_BLENDER "" CACHE FILEPATH "Blender executable used by the worker tests")

if(NOT PLYNTH_BLENDER AND DEFINED ENV{PLYNTH_BLENDER})
    set(PLYNTH_BLENDER "$ENV{PLYNTH_BLENDER}")
endif()

if(NOT PLYNTH_BLENDER)
    set(_plynth_roots "$ENV{ProgramFiles}/Blender Foundation" "$ENV{LOCALAPPDATA}/Programs/Blender Foundation")
    set(_plynth_found "")
    foreach(_root IN LISTS _plynth_roots)
        file(GLOB _exes "${_root}/*/blender.exe")
        list(APPEND _plynth_found ${_exes})
    endforeach()
    if(_plynth_found)
        list(SORT _plynth_found COMPARE NATURAL ORDER DESCENDING)
        list(GET _plynth_found 0 PLYNTH_BLENDER)
    endif()
endif()

if(PLYNTH_BLENDER AND EXISTS "${PLYNTH_BLENDER}")
    message(STATUS "Blender worker tests use ${PLYNTH_BLENDER}")
    enable_testing()
    # The add-on suite runs only the add-on tests, with the add-ons copied from the folder PLYNTH_TEST_ADDONS
    # names. It fails when that folder is missing or lacks one of the known add-ons, and when any test skips.
    add_test(
        NAME blender_python
        COMMAND "${PLYNTH_BLENDER}" --background --factory-startup -noaudio -t 6 --offline-mode
                --python-exit-code 3 --python "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(
        NAME blender_addons
        COMMAND "${PLYNTH_BLENDER}" --background --factory-startup -noaudio -t 6 --offline-mode
                --python-exit-code 3 --python "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py" -- --addons
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    # Factory startup does not protect the user's folders, so each variable that names one points inside the
    # build folder. Blender ignores a variable whose folder is missing and falls back to the user's own folder.
    # A setup test makes the folders every time a suite runs, so a deleted build folder cannot send Blender
    # anywhere else.
    set(_plynth_private "${CMAKE_BINARY_DIR}/blender-private")
    set(_plynth_private_folders "${_plynth_private}" "${_plynth_private}/config" "${_plynth_private}/scripts"
        "${_plynth_private}/extensions" "${_plynth_private}/datafiles" "${_plynth_private}/appdata"
        "${_plynth_private}/local" "${_plynth_private}/home" "${_plynth_private}/xdg"
        "${_plynth_private}/plynth-private" "${_plynth_private}/plynth-config")
    set(_plynth_private_env
        "BLENDER_USER_RESOURCES=${_plynth_private}"
        "BLENDER_USER_CONFIG=${_plynth_private}/config"
        "BLENDER_USER_SCRIPTS=${_plynth_private}/scripts"
        "BLENDER_USER_EXTENSIONS=${_plynth_private}/extensions"
        "BLENDER_USER_DATAFILES=${_plynth_private}/datafiles"
        "APPDATA=${_plynth_private}/appdata"
        "LOCALAPPDATA=${_plynth_private}/local"
        "USERPROFILE=${_plynth_private}/home"
        "HOME=${_plynth_private}/home"
        "XDG_DATA_HOME=${_plynth_private}/xdg"
        "PLYNTH_BLENDER_PRIVATE=${_plynth_private}/plynth-private"
        "PLYNTH_CONFIG_DIR=${_plynth_private}/plynth-config")
    add_test(NAME blender_private_folders
        COMMAND "${CMAKE_COMMAND}" -E make_directory ${_plynth_private_folders})
    set_tests_properties(blender_private_folders PROPERTIES LABELS blender FIXTURES_SETUP blender_private)
    set_tests_properties(blender_python PROPERTIES LABELS blender TIMEOUT 1800 FIXTURES_REQUIRED blender_private
        ENVIRONMENT "${_plynth_private_env}")
    set_tests_properties(blender_addons PROPERTIES LABELS "blender;addons" TIMEOUT 1800
        FIXTURES_REQUIRED blender_private ENVIRONMENT "${_plynth_private_env}")
else()
    message(STATUS "Blender not found, the worker Python tests are not added")
endif()

# The sweep mesh helper and the support finder need no Blender, so their tests run in plain Python on every
# machine and on GitHub.
if(PLYNTH_BUILD_TESTS)
    find_package(Python3 3.10 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        enable_testing()
        add_test(NAME python_plain_sweep
            COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/plain/test_sweep_mesh.py"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests/plain")
        set_tests_properties(python_plain_sweep PROPERTIES TIMEOUT 120)
        add_test(NAME python_plain_supports
            COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/plain/test_support_finder.py"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests/plain")
        set_tests_properties(python_plain_supports PROPERTIES TIMEOUT 120)
    else()
        message(STATUS "Python 3.10 or newer not found, the plain Python sweep and support tests are not added")
    endif()
endif()
