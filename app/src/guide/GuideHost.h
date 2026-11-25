#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>

namespace plynth::guide {
class HelpWindow;
}

namespace plynth::app {

class MainWindow;

// Joins the Help window to the main window: it fills the guide style from the theme and owns the Help window.
class GuideHost : public QObject {
    Q_OBJECT
public:
    explicit GuideHost(MainWindow* window);
    ~GuideHost() override;

    // Opens the Help window at a page, scrolled to an anchor when one is given.
    void openHelp(const QString& page = QStringLiteral("getting-started"), const QString& anchor = {});
    // Opens the Help window with the hits of a search.
    void openSearch(const QString& text);
    // Opens the Help window at About.
    void openAbout();
    // In screenshot mode the Help window takes the main window's size, so its picture fits the checks.
    void setScreenshotMode(bool on) { screenshotMode_ = on; }
    // Says whether the Help window is shown, and draws it for a screenshot.
    bool helpShown() const;
    QImage helpPicture() const;

signals:
    void setupCheckRequested();

private:
    plynth::guide::HelpWindow* help();

    MainWindow* window_ = nullptr;
    bool screenshotMode_ = false;
    QPointer<plynth::guide::HelpWindow> help_;
};

}
