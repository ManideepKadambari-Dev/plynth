#pragma once

#include "Theme.h"

#include <QPushButton>

namespace plynth::app {

// Makes one flat tab button: muted text with a line under it, bright text and a blue line when checked.
inline QPushButton* makeTabButton(const QString& text, const QString& tip, QWidget* parent,
                                  int padVertical, int padHorizontal)
{
    const Theme& t = Theme::instance();
    auto* button = new QPushButton(text, parent);
    button->setCheckable(true);
    button->setFlat(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    button->setToolTip(tip);
    button->setStyleSheet(
        QStringLiteral("QPushButton { background: transparent; border: none; border-bottom: 2px solid %1; "
                       "color: %2; padding: %3px %4px; border-radius: 0px; }"
                       "QPushButton:hover { color: %5; }"
                       "QPushButton:checked { color: %5; border-bottom-color: %6; }")
            .arg(t.line().name(), t.muted().name())
            .arg(padVertical)
            .arg(padHorizontal)
            .arg(t.text().name(), t.signalBlue().name()));
    return button;
}

}
