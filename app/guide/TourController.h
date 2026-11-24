#pragma once

#include "GuideStyle.h"

#include <plynth/core/Tour.h>

#include <QObject>
#include <QPointer>
#include <QString>

#include <optional>

namespace plynth::guide {

class TourHost;
class TourOverlay;

// Runs one tour at a time: picks the steps with core's tourStep, switches screens through the host and
// shows the overlay. It never changes the project, and it puts the screen back when the tour ends.
class TourController : public QObject {
    Q_OBJECT
public:
    TourController(const plynth::TourData& data, TourHost* host, const GuideStyle& style, QObject* parent = nullptr);
    ~TourController() override;

    // Starts a tour at a step: 0 for the first, -1 for the last, any other number for that step or the
    // nearest present one after it. False when the tour is unknown or none of its controls is there.
    bool start(const QString& tourId, int step = 0);

    // Says whether a tour is shown now.
    bool running() const;

    // The index of the step shown now, or -1.
    int currentStep() const;

public slots:
    void next();
    void back();
    void skip();

signals:
    void finished(const QString& tourId);

private:
    // Says whether the control of a step can be shown, switching to its screen.
    bool present(const plynth::TourStep& step) const;
    // Shows one step on the overlay.
    void show(std::size_t index);
    // Ends the tour and gives the screen back.
    void finish();

    plynth::TourData data_;
    TourHost* host_ = nullptr;
    GuideStyle style_;
    QPointer<TourOverlay> overlay_;
    const plynth::TourInfo* tour_ = nullptr;
    std::optional<std::size_t> step_;
    QString startScreen_;
};

}
