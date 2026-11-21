#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <functional>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QString>

#include "TerminalSession.h"
#include "TestApp.h"

using namespace plynth::terminal;

namespace {

bool waitFor(const std::function<bool()>& done, int milliseconds) {
    testApp();
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < milliseconds) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

bool screenContains(const TerminalScreen& screen, const std::string& text) {
    for (int r = 0; r < screen.rows(); ++r) {
        if (screen.lineText(r).find(text) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("a command runs through the pseudo console", "[terminal][conpty]") {
    testApp();
    TerminalSession session;
    SessionOptions options;
    options.commandLine = QStringLiteral("cmd.exe /c echo plynth-pty-ok");
    QString error;
    REQUIRE(session.start(options, &error));
    CHECK(waitFor([&] { return screenContains(session.screen(), "plynth-pty-ok"); }, 10000));
}

TEST_CASE("a running shell can be resized and typed into", "[terminal][conpty]") {
    testApp();
    TerminalSession session;
    SessionOptions options;
    options.commandLine = QStringLiteral("cmd.exe");
    REQUIRE(session.start(options));
    CHECK(waitFor([&] { return session.screen().cell(0, 0).text.size() > 0; }, 10000));
    session.resize(100, 30);
    CHECK(session.screen().columns() == 100);
    CHECK(session.screen().rows() == 30);
    session.write("echo typed-ok\r");
    CHECK(waitFor([&] { return screenContains(session.screen(), "typed-ok"); }, 10000));
    session.resize(60, 20);
    CHECK(session.screen().columns() == 60);
    CHECK(session.isRunning());
}

TEST_CASE("closing a session ends the shell and its children", "[terminal][conpty]") {
    testApp();
    TerminalSession session;
    SessionOptions options;
    options.commandLine = QStringLiteral("cmd.exe /c ping -n 30 127.0.0.1");
    REQUIRE(session.start(options));
    CHECK(waitFor([&] { return session.processCount() >= 2; }, 10000));

    QElapsedTimer timer;
    timer.start();
    session.close();
    CHECK(timer.elapsed() < 5000);
    CHECK(session.processCount() == 0);
    CHECK_FALSE(session.isRunning());
}

TEST_CASE("the shell choice is usable", "[terminal][conpty]") {
    QString shell = TerminalSession::chooseShell();
    CHECK_FALSE(shell.isEmpty());
}

TEST_CASE("closing during heavy output does not hang", "[terminal][conpty]") {
    testApp();
    TerminalSession session;
    SessionOptions options;
    options.commandLine = QStringLiteral("cmd.exe /c dir /s C:\\Windows");
    REQUIRE(session.start(options));
    CHECK(waitFor([&] { return session.screen().scrollbackLines() > 50; }, 10000));

    // The GUI thread handles no events here, so the reader is busy when close starts.
    QElapsedTimer timer;
    timer.start();
    session.close();
    CHECK(timer.elapsed() < 5000);
    CHECK(session.processCount() == 0);
}
