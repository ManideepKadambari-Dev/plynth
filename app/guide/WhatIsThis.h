#pragma once

#include "GuideStyle.h"

#include <plynth/core/Tour.h>

#include <QObject>
#include <QPointer>
#include <QRect>

class QWidget;

namespace plynth::guide {

class TourHost;
class TourCard;

// The "What is this?" mode. While it is on, a click on any control, also a disabled one or a menu entry,
// shows its name and explanation and never runs the control. Esc ends it.
class WhatIsThis : public QObject {
    Q_OBJECT
public:
    WhatIsThis(const plynth::TourData& data, TourHost* host, const GuideStyle& style, QObject* parent = nullptr);
    ~WhatIsThis() override;

    void setActive(bool active);
    bool isActive() const { return active_; }

    // Explains the control with this id, as a click on it would. False when the control is not there.
    bool explain(const QString& controlId);

signals:
    void activeChanged(bool active);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Shows the card for a tour id next to a rectangle given in the window's coordinates.
    void showCard(const QString& id, const QRect& area);
    // The tour id of a widget or the nearest parent that has one.
    static QString idOf(QWidget* widget);
    // Handles a click at a point of the window.
    void clickAt(const QPoint& windowPoint);

    plynth::TourData data_;
    TourHost* host_ = nullptr;
    GuideStyle style_;
    bool active_ = false;
    QPointer<QWidget> catcher_;
    QPointer<TourCard> card_;
    QRect outline_;
};

}
