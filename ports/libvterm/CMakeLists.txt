cmake_minimum_required(VERSION 3.25)
project(libvterm VERSION 0.3.3 LANGUAGES C)

set(VTERM_SOURCES
    src/encoding.c
    src/keyboard.c
    src/mouse.c
    src/parser.c
    src/pen.c
    src/screen.c
    src/state.c
    src/unicode.c
    src/vterm.c
)

add_library(libvterm STATIC ${VTERM_SOURCES})
set_target_properties(libvterm PROPERTIES OUTPUT_NAME vterm)
target_include_directories(libvterm
    PRIVATE src
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
)
if(MSVC)
    target_compile_options(libvterm PRIVATE /utf-8 /wd4244 /wd4267 /wd4996)
    target_compile_definitions(libvterm PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

install(TARGETS libvterm EXPORT unofficial-libvterm-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES include/vterm.h include/vterm_keycodes.h DESTINATION include)
install(EXPORT unofficial-libvterm-targets
    NAMESPACE unofficial::libvterm::
    FILE unofficial-libvterm-targets.cmake
    DESTINATION share/unofficial-libvterm
)
install(FILES unofficial-libvterm-config.cmake.in
    DESTINATION share/unofficial-libvterm
    RENAME unofficial-libvterm-config.cmake
)
