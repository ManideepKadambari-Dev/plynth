#pragma once

#include <QtGlobal>

class QWidget;

namespace plynth::app {

class MainWindow;
class TitleBar;

// Gives a frameless Qt window the normal Windows behavior: snap, shadow, resize edges.
namespace win {

void enableNativeFrame(QWidget& window);

bool handleNativeEvent(MainWindow& window, TitleBar& titleBar, void* message, qintptr* result);

}

}
