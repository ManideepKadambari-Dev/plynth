#pragma once

#include <QScrollArea>
#include <QScrollBar>

#include <algorithm>

namespace plynth::app {

// The width a scrolling pane needs so nothing in it is cut off: the smallest width of what it shows, plus the
// scroll bar and the frame. Long ids and paths in it must be elided or wrapped, or they set this width.
inline int paneNeededWidth(const QScrollArea* pane)
{
    const QWidget* body = pane->widget();
    const int content = body != nullptr ? body->minimumSizeHint().width() : 0;
    return content + pane->verticalScrollBar()->sizeHint().width() + 2 * pane->frameWidth();
}

// Makes a scrolling pane at least as wide as its content needs and never narrower than low. high is the widest
// a person can drag it, unless the content needs more. A splitter or layout then opens the pane at that width
// and a saved narrower width cannot cut it off.
inline void fitPaneWidth(QScrollArea* pane, int low, int high)
{
    const int needed = std::max(low, paneNeededWidth(pane));
    pane->setMinimumWidth(needed);
    pane->setMaximumWidth(std::max(high, needed));
}

}
