#pragma once

#include <plynth/core/Document.h>

#include <QObject>
#include <QString>

namespace plynth::app {

// Which floor of the plan the views and tools work on. This is app state, not plan data.
class FloorFocus : public QObject {
    Q_OBJECT
public:
    explicit FloorFocus(const plynth::Document& document, QObject* parent = nullptr);

    // The shown floor. Never empty while the plan has a floor.
    const plynth::Floor* floor() const;
    QString floorId() const { return id_; }
    // Position of the shown floor in the plan, 0 for the lowest.
    int index() const;

    // Shows this floor. False, and nothing changes, when the plan has no such floor.
    bool setFloorId(const QString& id);
    // Call after the plan changed: moves to a floor that still exists when the shown one is gone.
    void revalidate();

signals:
    void changed(const QString& floorId);

private:
    const plynth::Document& document_;
    QString id_;
};

}
