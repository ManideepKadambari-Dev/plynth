#pragma once

#include <QAbstractScrollArea>
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QScrollBar>
#include <QWidget>

namespace plynth::app {

// Keeps the mouse wheel from changing a drop down or number field. The wheel scrolls the page around it
// instead, so moving across a screen never saves or picks anything. Clicks and keys still change the value.
class WheelGuard : public QObject {
public:
    // Guards one control. The guard lives as long as the control.
    static void guard(QWidget* control)
    {
        if (control == nullptr)
            return;
        control->setFocusPolicy(Qt::StrongFocus);
        control->installEventFilter(new WheelGuard(control));
    }

protected:
    // Hands every wheel turn over the control to the nearest scrolling page, or drops it.
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::Wheel)
            return QObject::eventFilter(watched, event);
        auto* widget = qobject_cast<QWidget*>(watched);
        for (QWidget* up = widget != nullptr ? widget->parentWidget() : nullptr; up != nullptr; up = up->parentWidget())
            if (auto* area = qobject_cast<QAbstractScrollArea*>(up)) {
                QCoreApplication::sendEvent(area->verticalScrollBar(), event);
                break;
            }
        return true;
    }

private:
    explicit WheelGuard(QObject* parent) : QObject(parent) {}
};

}
