find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
find_package(unofficial-libvterm CONFIG REQUIRED)

set(CMAKE_AUTOMOC ON)

add_library(plynth_terminal STATIC
    ConPty_win.h
    ConPty_win.cpp
    TerminalScreen.h
    TerminalScreen.cpp
    TerminalSession.h
    TerminalSession.cpp
    TerminalView.h
    TerminalView.cpp
)
add_library(plynth::terminal ALIAS plynth_terminal)
target_include_directories(plynth_terminal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(plynth_terminal PUBLIC Qt6::Widgets PRIVATE unofficial::libvterm::libvterm)

if(PLYNTH_BUILD_TESTS)
    find_package(Catch2 CONFIG REQUIRED)
    include(Catch)
    enable_testing()
    add_executable(plynth_terminal_tests
        tests/TerminalScreenTests.cpp
        tests/ConPtyTests.cpp
        tests/TerminalViewTests.cpp
        tests/TestSetup.cpp
    )
    target_link_libraries(plynth_terminal_tests PRIVATE plynth::terminal Qt6::Widgets Catch2::Catch2WithMain
        unofficial::libvterm::libvterm)
    target_include_directories(plynth_terminal_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    # Copy the Qt DLLs and the offscreen platform plugin next to the test program.
    get_target_property(PLYNTH_TERM_QMAKE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(PLYNTH_TERM_QT_BIN "${PLYNTH_TERM_QMAKE}" DIRECTORY)
    find_program(PLYNTH_TERM_WINDEPLOYQT windeployqt HINTS "${PLYNTH_TERM_QT_BIN}" NO_DEFAULT_PATH)
    if(PLYNTH_TERM_WINDEPLOYQT)
        add_custom_command(TARGET plynth_terminal_tests POST_BUILD
            COMMAND "${PLYNTH_TERM_WINDEPLOYQT}"
                $<IF:$<CONFIG:Debug>,--debug,--release>
                --no-translations --no-compiler-runtime --no-system-d3d-compiler
                --no-opengl-sw --dir "$<TARGET_FILE_DIR:plynth_terminal_tests>"
                "$<TARGET_FILE:plynth_terminal_tests>"
            COMMENT "Running windeployqt for the terminal tests")
        add_custom_command(TARGET plynth_terminal_tests POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${PLYNTH_TERM_QT_BIN}/../plugins/platforms/$<IF:$<CONFIG:Debug>,qoffscreend.dll,qoffscreen.dll>"
                "$<TARGET_FILE_DIR:plynth_terminal_tests>/platforms/"
            COMMENT "Copying the offscreen platform plugin")
    endif()
    catch_discover_tests(plynth_terminal_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
