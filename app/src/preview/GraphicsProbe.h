#pragma once

#include <plynth/core/SetupCheck.h>

namespace plynth::app {

// Starts the graphics the 3D preview draws with and reports the interface and the card. Runs once per call.
plynth::GraphicsProbe probeGraphics();

// The probe made at start and kept for the whole run.
const plynth::GraphicsProbe& appGraphics();

}
