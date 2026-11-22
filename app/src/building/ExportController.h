#pragma once

#include <plynth/core/Export.h>
#include <plynth/core/Settings.h>

#include <QMetaType>
#include <QObject>
#include <QString>

#include <stop_token>
#include <thread>

namespace plynth::app {

// Runs one export or export check at a time on a worker thread, with a copy of the project in memory, and tells
// the window about progress and the finished report. It never touches the document.
class ExportController : public QObject {
    Q_OBJECT
public:
    explicit ExportController(QObject* parent = nullptr);
    ~ExportController() override;

    bool running() const { return running_; }

    // Starts an export. The worker picks the Blender from the settings, so the window never waits for the search.
    // request.blender is filled in by the worker. False when one is already running.
    bool start(plynth::ExportRequest request, plynth::MachineSettings settings);
    // Asks the running export to stop. The files of the last good export stay.
    void cancel();

signals:
    // A message for the bar and a percent from 0 to 100, or -1 when it is not known.
    void progress(const QString& message, int percent);
    // The export ended: the report holds the outcome, also for a blocked, failed or cancelled export.
    void finished(const plynth::ExportReport& report);

private:
    std::jthread worker_;
    std::stop_source stop_;
    bool running_ = false;
};

}

Q_DECLARE_METATYPE(plynth::ExportReport)
