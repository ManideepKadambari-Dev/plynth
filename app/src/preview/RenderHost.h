#pragma once

#include <plynth/core/Render.h>

#include <stop_token>

namespace plynth::app {

// Draws one RenderShot with a hidden ModelView and BuildScene.qml, the same scene as the 3D page, and saves it
// as a PNG. Starts a QApplication only when none runs, and then keeps Qt's own messages off the console, so a
// command line answer stays the only output. Never starts Blender.
plynth::Status renderShot(const plynth::RenderShot& shot, std::stop_token stop);

}
