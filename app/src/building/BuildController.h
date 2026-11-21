#pragma once

#include <plynth/core/Build.h>
#include <plynth/core/Settings.h>

#include <QMetaType>
#include <QObject>
#include <QString>

#include <atomic>
#include <filesystem>
#include <stop_token>
#include <thread>

namespace plynth::app {

// Runs one build at a time on a worker thread, with a copy of the project in memory, and tells the window about
// progress and the finished report. It never touches the document.
class BuildController : public QObject {
    Q_OBJECT
public:
    explicit BuildController(QObject* parent = nullptr);
    ~BuildController() override;

    bool running() const { return running_; }

    // Starts a build. The worker picks the Blender from the settings, so the window never waits for the search.
    // request.blender is filled in by the worker. False when a build is already running.
    bool start(plynth::BuildRequest request, plynth::MachineSettings settings);
    // Asks the running build to stop. The files of the last good build stay.
    void cancel();

signals:
    // A message for the bar and a percent from 0 to 100, or -1 when it is not known.
    void progress(const QString& message, int percent);
    // The build ended: the report holds the outcome, also for a failed or cancelled build.
    void finished(const plynth::BuildReport& report);

private:
    std::jthread worker_;
    std::stop_source stop_;
    bool running_ = false;
};

}

Q_DECLARE_METATYPE(plynth::BuildReport)
