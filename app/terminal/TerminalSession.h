#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "TerminalScreen.h"

namespace plynth::terminal {

class ConPty;

struct SessionOptions {
    // Empty means the user's shell chosen by chooseShell().
    QString commandLine;
    QString workingDir;
    // Put first on PATH for the program, so tools next to the app are found. Empty leaves PATH alone.
    QString pathPrefix;
    int columns = 80;
    int rows = 24;
};

// One running program in a pseudo console, with its screen. Lives on the GUI thread.
class TerminalSession : public QObject {
    Q_OBJECT
public:
    explicit TerminalSession(int rows = 24, int columns = 80, QObject* parent = nullptr);
    ~TerminalSession() override;

    // The command line of the shell: pwsh, then Windows PowerShell, then the command prompt.
    static QString chooseShell();

    bool start(const SessionOptions& options, QString* error = nullptr);
    bool isRunning() const;
    // True once the first program has ended. The console stays open until close().
    bool hasExited() const;
    int processCount() const;
    void write(const QByteArray& bytes);
    void resize(int columns, int rows);
    void close();

    TerminalScreen& screen() { return screen_; }
    const TerminalScreen& screen() const { return screen_; }

    // Gives output to the screen as if the program wrote it. Used by the reader and by tests.
    void feedOutput(const QByteArray& bytes);

signals:
    void screenDamaged(int firstRow, int lastRow);
    void outputReceived();
    void finished(int exitCode);

private:
    void readerLoop();
    void checkExit();

    TerminalScreen screen_;
    std::unique_ptr<ConPty> pty_;
    std::thread reader_;
    std::mutex mutex_;
    QByteArray pending_;
    bool deliveryQueued_ = false;
    std::atomic<bool> stopping_{false};
    QTimer exitTimer_;
    bool finishedSent_ = false;
};

}  // namespace plynth::terminal
