#pragma once

#include <QtGlobal>

class QWidget;

namespace plynth::app {

class TitleBar;

// Gives a frameless Qt window the normal Windows behavior: snap, shadow, resize edges.
namespace win {

void enableNativeFrame(QWidget& window);

bool handleNativeEvent(QWidget& window, TitleBar& titleBar, void* message, qintptr* result);

}

}
