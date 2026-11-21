#include "DetectionRunner.h"

#include <plynth/core/detection/Detector.h>

#include <QMetaObject>

#include <system_error>

namespace plynth::app {

namespace {

// A result that carries only an error sentence.
std::shared_ptr<const plynth::DetectionResult> failedResult(const plynth::Status& status)
{
    auto result = std::make_shared<plynth::DetectionResult>();
    result->status = status;
    return result;
}

}

// Starts the worker thread.
DetectionRunner::DetectionRunner(QObject* parent)
    : QObject(parent)
{
    thread_ = std::jthread([this](std::stop_token quit) { work(quit); });
}

// Stops the running detection, wakes the worker so it sees the quit request, and waits for it.
DetectionRunner::~DetectionRunner()
{
    {
        std::lock_guard lock(mutex_);
        running_.request_stop();
        if (pending_.has_value())
            pending_->stop.request_stop();
    }
    thread_.request_stop();
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

// Stops the one that is running, queues the new request and wakes the worker.
quint64 DetectionRunner::start(const std::filesystem::path& imageFile, double pixelsPerMetre,
                               const plynth::DetectionParams& params, const std::string& detectorId)
{
    quint64 generation = 0;
    Request request;
    request.imageFile = imageFile;
    request.pixelsPerMetre = pixelsPerMetre;
    request.params = params;
    request.detectorId = detectorId;
    {
        std::lock_guard lock(mutex_);
        running_.request_stop();
        if (pending_.has_value())
            pending_->stop.request_stop();
        generation = ++latest_;
        request.generation = generation;
        running_ = request.stop;
        pending_ = std::move(request);
    }
    wake_.notify_all();
    return generation;
}

// Stops everything and moves the generation on, so a result already on its way is dropped.
void DetectionRunner::cancel()
{
    std::lock_guard lock(mutex_);
    running_.request_stop();
    if (pending_.has_value())
        pending_->stop.request_stop();
    pending_.reset();
    ++latest_;
}

// Waits for a request, loads the image when it changed, runs the detector and hands the result back.
void DetectionRunner::work(std::stop_token quit)
{
    while (true) {
        Request request;
        {
            std::unique_lock lock(mutex_);
            if (!wake_.wait(lock, quit, [this]() { return pending_.has_value(); }))
                return;
            request = std::move(*pending_);
            pending_.reset();
        }
        if (request.stop.stop_requested())
            continue;

        std::error_code ec;
        const std::filesystem::file_time_type time = std::filesystem::last_write_time(request.imageFile, ec);
        const bool reuse = cached_.image != nullptr && cachedFile_ == request.imageFile && !ec && time == cachedTime_;
        if (!reuse) {
            cached_ = plynth::loadGrayImage(request.imageFile);
            cachedFile_ = request.imageFile;
            cachedTime_ = time;
            if (cached_.image == nullptr)
                cachedFile_.clear();
        }
        if (request.stop.stop_requested())
            continue;
        if (cached_.image == nullptr) {
            deliver(request.generation, failedResult(cached_.status));
            continue;
        }
        auto result = std::make_shared<plynth::DetectionResult>(
            plynth::runDetection(request.detectorId, *cached_.image, request.pixelsPerMetre, request.params,
                                 request.stop.get_token()));
        if (request.stop.stop_requested() || result->cancelled)
            continue;
        deliver(request.generation, std::move(result));
    }
}

// Sends the result to the owner's thread. It is dropped there when a newer request exists by then.
void DetectionRunner::deliver(quint64 generation, std::shared_ptr<const plynth::DetectionResult> result)
{
    QMetaObject::invokeMethod(
        this,
        [this, generation, result = std::move(result)]() {
            quint64 latest = 0;
            {
                std::lock_guard lock(mutex_);
                latest = latest_;
            }
            if (generation == latest)
                emit finished(result);
        },
        Qt::QueuedConnection);
}

}
