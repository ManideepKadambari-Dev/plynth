#include "DocumentBridge.h"

namespace plynth::app {

// Starts listening to the document.
DocumentBridge::DocumentBridge(plynth::Document& document, QObject* parent)
    : QObject(parent), document_(document)
{
    listenerId_ = document_.subscribe([this](const plynth::DocumentEvent& event) {
        emit changed(event.changes, QString::fromStdString(event.floorId));
    });
}

// Stops listening, so the document never calls a deleted bridge.
DocumentBridge::~DocumentBridge()
{
    document_.unsubscribe(listenerId_);
}

}
