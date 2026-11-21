#pragma once

#include <QApplication>
#include <QtGlobal>

// One shared application object for all terminal tests, drawn offscreen so no window appears.
inline QApplication& testApp() {
    static int argc = 1;
    static char name[] = "plynth_terminal_tests";
    static char* argv[] = {name, nullptr};
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    static QApplication instance(argc, argv);
    return instance;
}
