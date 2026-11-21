add_library(plynth_core STATIC
    src/Version.cpp
)
add_library(plynth::core ALIAS plynth_core)

target_include_directories(plynth_core PUBLIC include)
target_compile_features(plynth_core PUBLIC cxx_std_20)
set_source_files_properties(src/Version.cpp PROPERTIES
    COMPILE_DEFINITIONS PLYNTH_VERSION_STRING="${PROJECT_VERSION}")
