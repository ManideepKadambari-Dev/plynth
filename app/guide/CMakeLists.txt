find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)

# The Help and About widgets. They include nothing from app/src and get every
# color, font and size through GuideStyle.
add_library(plynth_guide_ui STATIC
    GuideStyle.h
    GuideStyle.cpp
    AboutPage.h
    AboutPage.cpp
    HelpRender.h
    HelpRender.cpp
    HelpWindow.h
    HelpWindow.cpp
)
add_library(plynth::guide_ui ALIAS plynth_guide_ui)
target_include_directories(plynth_guide_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(plynth_guide_ui PUBLIC Qt6::Widgets plynth::core)

if(PLYNTH_BUILD_TESTS)
    find_package(Catch2 CONFIG REQUIRED)
    include(Catch)
    enable_testing()
    add_executable(plynth_help_view_tests
        ${PROJECT_SOURCE_DIR}/tests/core/TestSetup.cpp
        tests/HelpViewTests.cpp
    )
    target_link_libraries(plynth_help_view_tests PRIVATE plynth::guide_ui Qt6::Widgets Catch2::Catch2WithMain)
    target_compile_definitions(plynth_help_view_tests PRIVATE
        PLYNTH_DOCS_DIR="${PROJECT_SOURCE_DIR}/docs"
        PLYNTH_LICENSES_DIR="${PROJECT_SOURCE_DIR}/licenses")

    # Copy the Qt DLLs and the offscreen platform plugin next to the test program.
    get_target_property(PLYNTH_HELP_QMAKE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(PLYNTH_HELP_QT_BIN "${PLYNTH_HELP_QMAKE}" DIRECTORY)
    find_program(PLYNTH_HELP_WINDEPLOYQT windeployqt HINTS "${PLYNTH_HELP_QT_BIN}" NO_DEFAULT_PATH)
    if(PLYNTH_HELP_WINDEPLOYQT)
        add_custom_command(TARGET plynth_help_view_tests POST_BUILD
            COMMAND "${PLYNTH_HELP_WINDEPLOYQT}"
                $<IF:$<CONFIG:Debug>,--debug,--release>
                --no-translations --no-compiler-runtime --no-system-d3d-compiler
                --no-opengl-sw --dir "$<TARGET_FILE_DIR:plynth_help_view_tests>"
                "$<TARGET_FILE:plynth_help_view_tests>"
            COMMENT "Running windeployqt for the help view tests")
        add_custom_command(TARGET plynth_help_view_tests POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${PLYNTH_HELP_QT_BIN}/../plugins/platforms/$<IF:$<CONFIG:Debug>,qoffscreend.dll,qoffscreen.dll>"
                "$<TARGET_FILE_DIR:plynth_help_view_tests>/platforms/"
            COMMENT "Copying the offscreen platform plugin")
    endif()
    catch_discover_tests(plynth_help_view_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
