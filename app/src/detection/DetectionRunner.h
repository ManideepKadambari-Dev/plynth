#pragma once

#include <plynth/core/detection/Detection.h>
#include <plynth/core/detection/GrayImage.h>

#include <QObject>
#include <QtGlobal>

#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

namespace plynth::app {

// Runs detection on one long lived worker thread, so the window never waits for it. A new request stops
// the one that is running. Results that come back for an old request are dropped.
class DetectionRunner : public QObject {
    Q_OBJECT
public:
    explicit DetectionRunner(QObject* parent = nullptr);
    // Stops the running detection and joins the worker thread.
    ~DetectionRunner() override;

    // Asks for a detection and returns its generation number.
    quint64 start(const std::filesystem::path& imageFile, double pixelsPerMetre,
                  const plynth::DetectionParams& params, const std::string& detectorId);
    // Stops the running detection and makes sure its result is never delivered.
    void cancel();

signals:
    // The result of the latest request. Sent on the thread that owns the runner.
    void finished(std::shared_ptr<const plynth::DetectionResult> result);

private:
    struct Request {
        std::filesystem::path imageFile;
        double pixelsPerMetre = 0.0;
        plynth::DetectionParams params;
        std::string detectorId;
        quint64 generation = 0;
        std::stop_source stop;
    };

    void work(std::stop_token quit);
    void deliver(quint64 generation, std::shared_ptr<const plynth::DetectionResult> result);

    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::optional<Request> pending_;
    std::stop_source running_;
    quint64 latest_ = 0;

    // Only the worker thread touches these.
    std::filesystem::path cachedFile_;
    std::filesystem::file_time_type cachedTime_;
    plynth::GrayImageLoad cached_;

    std::jthread thread_;
};

}
