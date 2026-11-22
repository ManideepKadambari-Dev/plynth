#include "ExportController.h"

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
QString stageMessage(plynth::ExportStage stage)
{
    switch (stage) {
    case plynth::ExportStage::Preparing:
        return QCoreApplication::translate("ExportController", "Preparing the export");
    case plynth::ExportStage::Blender:
        return QCoreApplication::translate("ExportController", "Starting Blender");
    case plynth::ExportStage::Finishing:
        return QCoreApplication::translate("ExportController", "Writing the report");
    case plynth::ExportStage::Done:
        break;
    }
    return QCoreApplication::translate("ExportController", "Done");
}

// Turns a stage and the worker's step into a percent for the bar.
int percentOf(const plynth::ExportProgress& step)
{
    switch (step.stage) {
    case plynth::ExportStage::Preparing:
        return kPreparingPercent;
    case plynth::ExportStage::Blender:
        if (step.blender.count < 1)
            return kBlenderStartPercent;
        return kBlenderStartPercent + kBlenderSpanPercent * std::max(0, step.blender.index - 1) / step.blender.count;
    case plynth::ExportStage::Finishing:
        return kFinishingPercent;
    case plynth::ExportStage::Done:
        break;
    }
    return 100;
}

}

ExportController::ExportController(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<plynth::ExportReport>();
}

// Stops a running export before the controller goes away.
ExportController::~ExportController()
{
    stop_.request_stop();
    worker_ = std::jthread();
}

// Starts the worker thread. Its progress and result come back as queued calls on this object.
bool ExportController::start(plynth::ExportRequest request, plynth::MachineSettings settings)
{
    if (running_)
        return false;
    worker_ = std::jthread();
    stop_ = std::stop_source();
    running_ = true;
    const std::stop_token token = stop_.get_token();
    worker_ = std::jthread([this, request = std::move(request), settings = std::move(settings), token]() mutable {
        const plynth::BlenderChoice choice = plynth::chooseBlender(std::nullopt, settings, token);
        plynth::ExportReport report;
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
        report = plynth::runExport(
            request,
            [this](const plynth::ExportProgress& step) {
                const QString message = step.stage == plynth::ExportStage::Blender && !step.blender.message.empty()
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
void ExportController::cancel()
{
    stop_.request_stop();
}

}
