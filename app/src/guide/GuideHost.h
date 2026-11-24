#pragma once

#include <TourHost.h>

#include <plynth/core/Tour.h>

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>

namespace plynth::guide {
class HelpWindow;
class TourController;
class WhatIsThis;
}

namespace plynth::app {

class MainWindow;

// Joins the guide widgets to the main window: it finds controls by their tour id, switches screens, fills
// the guide style from the theme and owns the Help window, the tour and What is this?.
class GuideHost : public QObject, public plynth::guide::TourHost {
    Q_OBJECT
public:
    explicit GuideHost(MainWindow* window);
    ~GuideHost() override;

    bool showScreen(const QString& screen) override;
    QString currentScreen() const override;
    QWidget* window() const override;
    QWidget* findControl(const QString& id) const override;
    QString shortcutText(const QString& id) const override;

    // Opens the Help window at a page, scrolled to an anchor when one is given.
    void openHelp(const QString& page = QStringLiteral("getting-started"), const QString& anchor = {});
    // Opens the Help window with the hits of a search.
    void openSearch(const QString& text);
    // Opens the Help window at About.
    void openAbout();
    // Starts a tour at a step counted from 0, or the last step for -1. False when it cannot start.
    bool startTour(const QString& tourId, int step = 0);
    // Turns What is this? on or off.
    void setWhatIsThis(bool on);
    // Turns What is this? on and explains one control. False when the control is not there.
    bool explain(const QString& controlId);
    // In screenshot mode the Help window takes the main window's size, so its picture fits the checks.
    void setScreenshotMode(bool on) { screenshotMode_ = on; }
    // Says whether the Help window is shown, and draws it for a screenshot.
    bool helpShown() const;
    QImage helpPicture() const;
    // False when the tour file could not be read; the message says why.
    bool tourLoaded() const { return tourLoaded_; }
    QString tourProblem() const { return tourProblem_; }
    // The tour text as read.
    const plynth::TourData& tourData() const { return data_; }

signals:
    void whatIsThisChanged(bool on);
    void tourFinished(const QString& tourId);
    void setupCheckRequested();

private:
    plynth::guide::HelpWindow* help();

    MainWindow* window_ = nullptr;
    plynth::TourData data_;
    bool tourLoaded_ = false;
    QString tourProblem_;
    bool screenshotMode_ = false;
    plynth::guide::TourController* tour_ = nullptr;
    plynth::guide::WhatIsThis* whatIsThis_ = nullptr;
    QPointer<plynth::guide::HelpWindow> help_;
};

}
