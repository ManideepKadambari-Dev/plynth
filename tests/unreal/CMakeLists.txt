# The headless Unreal Engine link test. It starts a real Unreal Editor and a real Blender, so it is added only
# when PLYNTH_UNREAL_TESTS is on, and never on GitHub. Its name starts with unreal_ so the GitHub test step can
# leave it out by name: the core tests tagged [unreal] carry the unreal label too and must still run there.
set(PLYNTH_UNREAL_EDITOR "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
    CACHE FILEPATH "UnrealEditor.exe used by the headless Unreal test")

if(NOT PLYNTH_UNREAL_TESTS)
    return()
endif()

if(DEFINED ENV{GITHUB_ACTIONS})
    message(STATUS "The headless Unreal test is never added on GitHub")
    return()
endif()

if(NOT EXISTS "${PLYNTH_UNREAL_EDITOR}")
    message(WARNING "PLYNTH_UNREAL_TESTS is on but ${PLYNTH_UNREAL_EDITOR} does not exist, so the Unreal test is not added")
    return()
endif()

if(NOT TARGET plynth_launcher)
    message(WARNING "The headless Unreal test needs plynth.com, which this build does not make")
    return()
endif()

find_program(PLYNTH_WINDOWS_POWERSHELL NAMES powershell.exe REQUIRED)
message(STATUS "The headless Unreal test uses ${PLYNTH_UNREAL_EDITOR}")

add_test(NAME unreal_headless
    COMMAND "${PLYNTH_WINDOWS_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/run-headless.ps1"
            -Build "${CMAKE_BINARY_DIR}" -Config "$<CONFIG>"
            -Plynth "$<TARGET_FILE:plynth_launcher>" -Editor "${PLYNTH_UNREAL_EDITOR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
# One editor at a time, and the first start compiles shaders for a long while.
set_tests_properties(unreal_headless PROPERTIES LABELS unreal TIMEOUT 14400 RUN_SERIAL TRUE)
