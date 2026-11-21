#pragma once

#include <QObject>
#include <QString>

namespace plynth::app {

enum class SelectionKind { None, Point, Wall, Room };

// The one item the user has selected on the plan. It is app state, not plan data, and holds an id only.
class Selection : public QObject {
    Q_OBJECT
public:
    explicit Selection(QObject* parent = nullptr) : QObject(parent) {}

    SelectionKind kind() const { return kind_; }
    const QString& id() const { return id_; }
    bool isEmpty() const { return kind_ == SelectionKind::None; }

    void set(SelectionKind kind, const QString& id);
    void clear();

signals:
    void changed();

private:
    SelectionKind kind_ = SelectionKind::None;
    QString id_;
};

}
