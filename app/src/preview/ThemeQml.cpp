#include "ThemeQml.h"

#include "theme/Theme.h"

namespace plynth::app {

// The Ground color, the background of the 3D scene.
QColor ThemeQml::ground() const { return Theme::instance().ground(); }

// The Muted color, for the empty state text.
QColor ThemeQml::muted() const { return Theme::instance().muted(); }

// The Signal amber color, for load failures.
QColor ThemeQml::signalAmber() const { return Theme::instance().signalAmber(); }

// The white of the scene lights.
QColor ThemeQml::lightColor() const { return Theme::instance().lightColor(); }

// The skyTopColor of the light probe sky.
QColor ThemeQml::skyTopColor() const { return Theme::instance().skyTopColor(); }

// The skyHorizonColor of the light probe sky.
QColor ThemeQml::skyHorizonColor() const { return Theme::instance().skyHorizonColor(); }

// The groundHorizonColor of the light probe sky.
QColor ThemeQml::groundHorizonColor() const { return Theme::instance().groundHorizonColor(); }

// The groundBottomColor of the light probe sky.
QColor ThemeQml::groundBottomColor() const { return Theme::instance().groundBottomColor(); }

// How bright the key light is.
double ThemeQml::keyLightBrightness() const { return Theme::instance().keyLightBrightness(); }

// How bright the light probe is.
double ThemeQml::probeExposure() const { return Theme::instance().probeExposure(); }

// How strong the ambient occlusion is.
double ThemeQml::aoStrength() const { return Theme::instance().aoStrength(); }

// The UI font family name.
QString ThemeQml::uiFamily() const { return Theme::instance().uiFont().family(); }

// The normal text size in pixels.
int ThemeQml::sizeNormal() const { return Theme::instance().sizeNormal(); }

// The base spacing unit in pixels.
int ThemeQml::unit() const { return Theme::instance().unit(); }

}
