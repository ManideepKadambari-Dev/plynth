#pragma once

#include <plynth/core/Document.h>

#include <QObject>
#include <QString>

namespace plynth::app {

// Turns the plain C++ document events into a Qt signal, so views can connect to it.
class DocumentBridge : public QObject {
    Q_OBJECT
public:
    explicit DocumentBridge(plynth::Document& document, QObject* parent = nullptr);
    ~DocumentBridge() override;

signals:
    // changes holds plynth::Change flags. floorId is empty when several floors or none changed.
    void changed(unsigned changes, const QString& floorId);

private:
    plynth::Document& document_;
    int listenerId_ = 0;
};

}
