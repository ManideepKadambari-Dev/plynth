#pragma once

#include <QColor>
#include <QFont>
#include <QString>

namespace plynth::guide {

// The colors, fonts and sizes the guide widgets use. The app fills it from its theme file, so nothing here
// has a value of its own.
struct GuideStyle {
    QColor ground;
    QColor panel;
    QColor raised;
    QColor line;
    QColor text;
    QColor muted;
    QColor blue;
    QColor amber;
    QColor onBlue;
    int dimAlpha = 0; // how dark the tour layer is, 0 to 255
    QFont uiFont;
    QFont titleFont;
    QFont monoFont;
    int unit = 0;
    int radius = 0;
    int cardWidth = 0;
    int outlineWidth = 0;
    int listWidth = 0;
};

// A style sheet for the guide widgets made from the style.
QString guideStyleSheet(const GuideStyle& style);

}
