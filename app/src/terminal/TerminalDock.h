#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QSize>
#include <QString>

#include <memory>

class QLabel;
class QWidget;

namespace plynth::terminal {
class TerminalSession;
class TerminalView;
}

namespace plynth::app {

class TerminalButton;
class TerminalPanel;

// The built-in terminal of the window: a round button and a panel over the plan area, with the
// shell session behind them. The panel hides without ending the shell. Ending the project does.
class TerminalDock : public QObject {
    Q_OBJECT
public:
    // host holds the button and the panel. area is the widget they sit over, usually the canvas.
    TerminalDock(QWidget* host, QWidget* area, QObject* parent = nullptr);
    ~TerminalDock() override;

    bool isOpen() const;
    // Shows the panel, starting the shell on first use, or hides it.
    void toggle();
    void open();
    void close();

    // The folder the next shell starts in. Empty means the Documents folder.
    void setWorkingFolder(const QString& folder);
    // True while something besides the shell is running in the terminal.
    bool hasRunningProgram() const;
    // Ends the shell and hides the panel, so the next opening starts a fresh one.
    void shutdown();

    // For checks only: types text and Enter once the shell has printed its first output.
    void typeWhenReady(const QString& text);
    bool startFailed() const { return startFailed_; }
    // True once the typed text had time to show its result: output quiet for 1.5 s, or 15 s passed.
    bool pictureReady() const;

signals:
    void openChanged(bool open);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void ensureCreated();
    bool startShell();
    void layoutOverlay();
    void onFinished();
    void setPanelSize(const QSize& size);

    QWidget* host_;
    QWidget* area_;
    TerminalButton* button_ = nullptr;
    TerminalPanel* panel_ = nullptr;
    plynth::terminal::TerminalSession* session_ = nullptr;
    plynth::terminal::TerminalView* view_ = nullptr;
    QLabel* endedLabel_ = nullptr;
    QString folder_;
    QSize panelSize_;
    bool started_ = false;
    bool startFailed_ = false;

    QString pendingText_;
    bool typed_ = false;
    bool outputSinceTyped_ = false;
    bool waitingFirstOutput_ = false;
    QElapsedTimer sinceOutput_;
    QElapsedTimer sinceStart_;
};

}
