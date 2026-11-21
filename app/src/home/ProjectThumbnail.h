#pragma once

#include <plynth/core/Plan.h>

#include <QImage>

namespace plynth::app {

// Draws a small picture of a plan for the home screen: the plan image with the rooms and walls over it.
// The longest side is maxSide pixels. A null image gives a null result.
QImage renderProjectThumbnail(const plynth::Plan& plan, const QImage& planImage, int maxSide);

}
