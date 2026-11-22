#pragma once

#include "canvas/PlanCanvas.h"

#include <plynth/core/detection/Detection.h>

#include <QObject>
#include <QString>

#include <memory>
#include <string>

class QTimer;

namespace plynth::app {

class DetectionRunner;
class ToolController;

// The state of the Detect tool: the settings, the running detection, the proposals and the status text.
// Views read it and redraw when changed() is sent. Settings are kept while the app runs.
class DetectionSession : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Running, Done, Failed };

    explicit DetectionSession(ToolController& host, QObject* parent = nullptr);
    ~DetectionSession() override;

    // The Detect tool is on: detection starts, and the proposals are shown until it is turned off.
    void setActive(bool active);
    bool active() const { return active_; }

    const plynth::DetectionParams& params() const { return params_; }
    void setParams(const plynth::DetectionParams& params);
    void resetParams();
    const std::string& detectorId() const { return detectorId_; }
    void setDetectorId(const std::string& id);

    State state() const { return state_; }
    // True when no detection is running or waiting to start.
    bool settled() const { return state_ != State::Running; }
    // The status line of the panel, and whether it is a refusal.
    QString statusText() const { return statusText_; }
    bool statusIsRefusal() const { return state_ == State::Failed; }
    // Whether there is something new to accept.
    bool canAccept() const;
    // Adds the new proposals to the plan as one undo step.
    bool accept();
    // What the canvas draws.
    ProposalOverlay overlay() const;

    // Another floor is shown: detection runs again on its image.
    void floorChanged();

    // The plan changed. A new image or scale runs detection again, other edits only re-check what exists.
    void documentChanged(unsigned changes);

signals:
    void changed();

private:
    void restart(int delayMs);
    void launch();
    void onFinished(std::shared_ptr<const plynth::DetectionResult> result);
    void remark();
    void clearResult();

    ToolController& host_;
    DetectionRunner* runner_ = nullptr;
    QTimer* timer_ = nullptr;
    plynth::DetectionParams params_;
    std::string detectorId_;
    bool active_ = false;
    State state_ = State::Idle;
    QString statusText_;
    std::shared_ptr<const plynth::DetectionResult> raw_;
    plynth::DetectionResult marked_;
};

}
