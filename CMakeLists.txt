cmake_minimum_required(VERSION 3.25)

project(Plynth VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
endif()

option(PLYNTH_BUILD_TESTS "Build the unit tests" ON)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/core/CMakeLists.txt")
    add_subdirectory(core)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cli/CMakeLists.txt")
    add_subdirectory(cli)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/blender/CMakeLists.txt")
    add_subdirectory(blender)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/terminal/CMakeLists.txt")
    add_subdirectory(app/terminal)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/guide/CMakeLists.txt")
    add_subdirectory(app/guide)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt")
    add_subdirectory(app)
endif()

if(PLYNTH_BUILD_TESTS)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()
