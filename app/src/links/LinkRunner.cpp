#include "LinkRunner.h"

#include <plynth/core/Blender.h>

#include <QCoreApplication>
#include <QMetaObject>

#include <chrono>
#include <utility>

namespace plynth::app {

namespace {

// How long the window's search waits for editors to answer. Shorter than a send, as it runs every few seconds.
constexpr std::chrono::milliseconds kSearchWait{800};

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// The sentence for one step of a link run.
QString progressText(const plynth::EngineLinkProgress& step)
{
    if (step.stage == plynth::EngineLinkStage::Preparing && !step.blender.message.empty())
        return fromCore(step.message) + QStringLiteral(" ") + fromCore(step.blender.message);
    return fromCore(step.message);
}

}

// Makes a runner with nothing running.
LinkRunner::LinkRunner(QObject* parent) : QObject(parent) {}

// Stops a running send or sync and waits for both workers before the runner goes away.
LinkRunner::~LinkRunner()
{
    runStop_.request_stop();
    runWorker_ = std::jthread();
    searchWorker_ = std::jthread();
}

// Starts a search on its own thread and hands the editors back when it ends.
bool LinkRunner::search(const std::string& linkId)
{
    const plynth::EngineLink* link = plynth::findEngineLink(linkId);
    if (searching_ || link == nullptr)
        return false;
    searchWorker_ = std::jthread();
    searching_ = true;
    searchWorker_ = std::jthread([this, link](std::stop_token stop) {
        const plynth::EngineEditorSearch found = link->findEditors(kSearchWait, stop);
        QMetaObject::invokeMethod(
            this,
            [this, found]() {
                searching_ = false;
                emit searched(found);
            },
            Qt::QueuedConnection);
    });
    return true;
}

// Starts a send on its own thread. Its steps and its report come back as queued calls.
bool LinkRunner::send(plynth::EngineLinkRequest request, plynth::MachineSettings settings)
{
    if (running_)
        return false;
    runWorker_ = std::jthread();
    runStop_ = std::stop_source();
    running_ = true;
    const std::stop_token token = runStop_.get_token();
    runWorker_ = std::jthread([this, request = std::move(request), settings = std::move(settings), token]() mutable {
        const auto onStep = [this](const plynth::EngineLinkProgress& step) {
            const QString text = progressText(step);
            QMetaObject::invokeMethod(this, [this, text]() { emit progress(text); }, Qt::QueuedConnection);
        };
        plynth::EngineLinkReport report;
        const plynth::BlenderChoice choice = plynth::chooseBlender(std::nullopt, settings, token);
        if (!choice.status.ok()) {
            report.link = request.link;
            report.status = choice.status;
        } else {
            request.blender = choice.install;
            report = plynth::runEngineLink(request, onStep, token);
        }
        QMetaObject::invokeMethod(
            this,
            [this, report]() {
                running_ = false;
                emit sent(report);
            },
            Qt::QueuedConnection);
    });
    return true;
}

// Starts the sync after a build on its own thread. Without a usable Blender there is nothing to update, so it
// ends with no reports.
bool LinkRunner::sync(plynth::EngineLinkRequest base, plynth::MachineSettings settings)
{
    if (running_)
        return false;
    runWorker_ = std::jthread();
    runStop_ = std::stop_source();
    running_ = true;
    const std::stop_token token = runStop_.get_token();
    runWorker_ = std::jthread([this, base = std::move(base), settings = std::move(settings), token]() mutable {
        const auto onStep = [this](const plynth::EngineLinkProgress& step) {
            const QString text = progressText(step);
            QMetaObject::invokeMethod(this, [this, text]() { emit progress(text); }, Qt::QueuedConnection);
        };
        std::vector<plynth::EngineLinkReport> reports;
        const plynth::BlenderChoice choice = plynth::chooseBlender(std::nullopt, settings, token);
        if (choice.status.ok()) {
            base.blender = choice.install;
            reports = plynth::syncEngineLinks(base, onStep, token);
        }
        QMetaObject::invokeMethod(
            this,
            [this, reports]() {
                running_ = false;
                emit synced(reports);
            },
            Qt::QueuedConnection);
    });
    return true;
}

// Asks the worker to stop. Core ends Blender or the editor command and reports Cancelled.
void LinkRunner::cancel()
{
    runStop_.request_stop();
}

}
