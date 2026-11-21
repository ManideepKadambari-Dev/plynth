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
    add_test(
        NAME blender_python
        COMMAND "${PLYNTH_BLENDER}" --background --factory-startup -noaudio --python-exit-code 3
                --python "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(blender_python PROPERTIES LABELS blender TIMEOUT 900)
else()
    message(STATUS "Blender not found, the worker Python tests are not added")
endif()
