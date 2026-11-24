#include "TourController.h"

#include "TourCard.h"
#include "TourHost.h"
#include "TourOverlay.h"

#include <QWidget>

namespace plynth::guide {

namespace {

// Text from core as a Qt string.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}

// Keeps a copy of the tour data.
TourController::TourController(const plynth::TourData& data, TourHost* host, const GuideStyle& style, QObject* parent)
    : QObject(parent), data_(data), host_(host), style_(style)
{
}

// Removes the overlay if a tour is still shown.
TourController::~TourController()
{
    if (overlay_) overlay_->deleteLater();
}

// Finds the tour and its first present step at or after the one asked for.
bool TourController::start(const QString& tourId, int step)
{
    if (running()) finish();
    tour_ = plynth::findTour(data_, tourId.toStdString());
    if (!tour_ || !host_ || !host_->window()) {
        tour_ = nullptr;
        return false;
    }
    startScreen_ = host_->currentScreen();
    const auto isPresent = [this](const plynth::TourStep& s) { return present(s); };
    std::optional<std::size_t> first;
    if (step < 0) {
        first = plynth::tourStep(*tour_, std::nullopt, -1, isPresent);
    } else {
        const std::optional<std::size_t> from =
            step == 0 ? std::nullopt : std::optional<std::size_t>(static_cast<std::size_t>(step) - 1);
        first = plynth::tourStep(*tour_, from, 1, isPresent);
    }
    if (!first) {
        host_->showScreen(startScreen_);
        tour_ = nullptr;
        return false;
    }
    auto* overlay = new TourOverlay(style_, host_->window());
    connect(overlay, &TourOverlay::nextRequested, this, &TourController::next);
    connect(overlay, &TourOverlay::backRequested, this, &TourController::back);
    connect(overlay, &TourOverlay::skipRequested, this, &TourController::skip);
    overlay_ = overlay;
    overlay->show();
    show(*first);
    return true;
}

// A tour is running while its overlay exists.
bool TourController::running() const
{
    return tour_ != nullptr && overlay_;
}

// The step on screen.
int TourController::currentStep() const
{
    return step_ ? static_cast<int>(*step_) : -1;
}

// Goes to the next present step, or ends the tour after the last one.
void TourController::next()
{
    if (!running()) return;
    const auto isPresent = [this](const plynth::TourStep& s) { return present(s); };
    const std::optional<std::size_t> to = plynth::tourStep(*tour_, step_, 1, isPresent);
    if (!to) {
        finish();
        return;
    }
    show(*to);
}

// Goes to the step before, staying put on the first one.
void TourController::back()
{
    if (!running() || !step_) return;
    const auto isPresent = [this](const plynth::TourStep& s) { return present(s); };
    const std::optional<std::size_t> to = plynth::tourStep(*tour_, step_, -1, isPresent);
    show(to ? *to : *step_);
}

// Ends the tour at once.
void TourController::skip()
{
    if (running()) finish();
}

// A step counts when its screen can be shown and its control is visible there.
bool TourController::present(const plynth::TourStep& step) const
{
    if (!host_->showScreen(fromCore(step.screen))) return false;
    QWidget* control = host_->findControl(fromCore(step.control));
    return control && control->isVisible();
}

// Switches to the step's screen, outlines its control and fills the card.
void TourController::show(std::size_t index)
{
    const plynth::TourStep& step = tour_->steps[index];
    host_->showScreen(fromCore(step.screen));
    const QString id = fromCore(step.control);
    const plynth::TourEntry* entry = plynth::tourEntry(data_, step.control);
    step_ = index;
    const bool isLast = index + 1 == tour_->steps.size();
    overlay_->card()->setContent(entry ? fromCore(entry->name) : id, entry ? fromCore(entry->text) : QString(),
                                 host_->shortcutText(id));
    overlay_->card()->setStep(static_cast<int>(index) + 1, static_cast<int>(tour_->steps.size()), index > 0, isLast);
    overlay_->setTarget(host_->findControl(id));
    overlay_->setFocus();
}

// Removes the overlay, gives the screen back and says which tour ended.
void TourController::finish()
{
    const QString id = tour_ ? fromCore(tour_->id) : QString();
    if (overlay_) {
        overlay_->hide();
        overlay_->deleteLater();
    }
    overlay_ = nullptr;
    tour_ = nullptr;
    step_.reset();
    if (host_ && !startScreen_.isEmpty()) host_->showScreen(startScreen_);
    emit finished(id);
}

}
