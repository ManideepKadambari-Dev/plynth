find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)

# The Help and About widgets. They include nothing from app/src and get every
# color, font and size through GuideStyle.
add_library(plynth_guide_ui STATIC
    GuideStyle.h
    GuideStyle.cpp
    AboutPage.h
    AboutPage.cpp
    HelpWindow.h
    HelpWindow.cpp
)
add_library(plynth::guide_ui ALIAS plynth_guide_ui)
target_include_directories(plynth_guide_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(plynth_guide_ui PUBLIC Qt6::Widgets plynth::core)
