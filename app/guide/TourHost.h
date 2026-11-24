#pragma once

#include <QString>

class QWidget;

namespace plynth::guide {

// What the guide widgets need from the app. The app implements it, so the guide never includes app code.
class TourHost {
public:
    virtual ~TourHost() = default;

    // Shows one screen: home, plan, wall, build, export, assets or materials. False when it cannot be shown.
    virtual bool showScreen(const QString& screen) = 0;

    // The screen shown now.
    virtual QString currentScreen() const = 0;

    // The main window the tour draws over.
    virtual QWidget* window() const = 0;

    // The visible widget with this tour id, or the widget that shows a menu action with it. Null when absent.
    virtual QWidget* findControl(const QString& id) const = 0;

    // The keyboard shortcut of the control as the user sees it, such as "Ctrl+B". Empty when it has none.
    virtual QString shortcutText(const QString& id) const
    {
        (void)id;
        return {};
    }
};

}
