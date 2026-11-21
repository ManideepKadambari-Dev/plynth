find_package(nlohmann_json CONFIG REQUIRED)

add_library(plynth_core STATIC
    src/Version.cpp
    src/PlanImage.cpp
    src/Status.cpp
    src/Plan.cpp
    src/Project.cpp
    src/ProjectJson.cpp
)
add_library(plynth::core ALIAS plynth_core)

target_include_directories(plynth_core PUBLIC include)
target_compile_features(plynth_core PUBLIC cxx_std_20)
target_link_libraries(plynth_core PUBLIC nlohmann_json::nlohmann_json)
set_source_files_properties(src/Version.cpp PROPERTIES
    COMPILE_DEFINITIONS PLYNTH_VERSION_STRING="${PROJECT_VERSION}")
