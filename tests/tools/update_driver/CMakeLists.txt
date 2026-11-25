add_executable(plynth_update_driver main.cpp)
target_include_directories(plynth_update_driver PRIVATE ${PROJECT_SOURCE_DIR}/core/src/update ${PROJECT_SOURCE_DIR}/core/src)
target_link_libraries(plynth_update_driver PRIVATE plynth::core unofficial-sodium::sodium httplib::httplib
    nlohmann_json::nlohmann_json ws2_32)
set_target_properties(plynth_update_driver PROPERTIES OUTPUT_NAME update_driver)
