#include "DetectTool.h"

#include "detection/DetectionSession.h"
#include "tools/ToolController.h"

#include <QKeyEvent>

namespace plynth::app {

// The status bar hint for this tool.
QString DetectTool::hint() const
{
    return QObject::tr("Move the sliders to tune the result. Enter accepts. Leave the tool to discard.");
}

// Starts detection on the floor image.
void DetectTool::activate()
{
    host_.detection().setActive(true);
}

// Throws the proposals away.
void DetectTool::deactivate()
{
    host_.detection().setActive(false);
}

// Enter accepts the proposals.
bool DetectTool::keyPressed(QKeyEvent* event)
{
    if (event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter)
        return false;
    host_.detection().accept();
    return true;
}

// Passes the change on, so the proposals are checked against the new plan.
void DetectTool::documentChanged(unsigned changes)
{
    host_.detection().documentChanged(changes);
}

}
