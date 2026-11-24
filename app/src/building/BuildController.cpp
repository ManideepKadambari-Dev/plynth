#include "BuildController.h"

#include <plynth/core/Blender.h>

#include <QCoreApplication>
#include <QMetaObject>

#include <algorithm>

namespace plynth::app {

namespace {

constexpr int kPreparingPercent = 3;
constexpr int kBlenderStartPercent = 5;
constexpr int kBlenderSpanPercent = 90;
constexpr int kFinishingPercent = 97;

// The sentence of a stage that has no worker step of its own.
QString stageMessage(plynth::BuildStage stage)
{
    switch (stage) {
    case plynth::BuildStage::Preparing:
        return QCoreApplication::translate("BuildController", "Checking the plan");
    case plynth::BuildStage::Blender:
        return QCoreApplication::translate("BuildController", "Starting Blender");
    case plynth::BuildStage::Finishing:
        return QCoreApplication::translate("BuildController", "Writing the report");
    case plynth::BuildStage::Done:
        break;
    }
    return QCoreApplication::translate("BuildController", "Done");
}

// Turns a stage and the worker's step into a percent for the bar.
int percentOf(const plynth::BuildProgress& step)
{
    switch (step.stage) {
    case plynth::BuildStage::Preparing:
        return kPreparingPercent;
    case plynth::BuildStage::Blender:
        if (step.blender.count < 1)
            return kBlenderStartPercent;
        return kBlenderStartPercent + kBlenderSpanPercent * std::max(0, step.blender.index - 1) / step.blender.count;
    case plynth::BuildStage::Finishing:
        return kFinishingPercent;
    case plynth::BuildStage::Done:
        break;
    }
    return 100;
}

}

BuildController::BuildController(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<plynth::BuildReport>();
}

// Stops a running build before the controller goes away.
BuildController::~BuildController()
{
    stop_.request_stop();
    worker_ = std::jthread();
}

// Starts the worker thread. Its progress and result come back as queued calls on this object.
bool BuildController::start(plynth::BuildRequest request, plynth::MachineSettings settings)
{
    if (running_)
        return false;
    worker_ = std::jthread();
    stop_ = std::stop_source();
    running_ = true;
    const std::stop_token token = stop_.get_token();
    worker_ = std::jthread([this, request = std::move(request), settings = std::move(settings), token]() mutable {
        const plynth::BlenderChoice choice = plynth::chooseBlender(std::nullopt, settings, token);
        plynth::BuildReport report;
        if (!choice.status.ok()) {
            report.status = choice.status;
            QMetaObject::invokeMethod(
                this,
                [this, report]() {
                    running_ = false;
                    emit finished(report);
                },
                Qt::QueuedConnection);
            return;
        }
        request.blender = choice.install;
        request.addonSource = plynth::blenderUserFolders(choice.install).extensions;
        report = plynth::runBuild(
            request,
            [this](const plynth::BuildProgress& step) {
                const QString message = step.stage == plynth::BuildStage::Blender && !step.blender.message.empty()
                                            ? QString::fromUtf8(step.blender.message.data(),
                                                                static_cast<qsizetype>(step.blender.message.size()))
                                            : stageMessage(step.stage);
                const int percent = percentOf(step);
                QMetaObject::invokeMethod(this, [this, message, percent]() { emit progress(message, percent); },
                                          Qt::QueuedConnection);
            },
            token);
        QMetaObject::invokeMethod(
            this,
            [this, report]() {
                running_ = false;
                emit finished(report);
            },
            Qt::QueuedConnection);
    });
    return true;
}

// Asks the worker to stop. The runner ends Blender and reports Cancelled.
void BuildController::cancel()
{
    stop_.request_stop();
}

}
