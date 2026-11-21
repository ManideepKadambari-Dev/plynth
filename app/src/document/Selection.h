#pragma once

#include <QObject>
#include <QPointF>
#include <QString>

#include <optional>

namespace plynth::app {

enum class SelectionKind { None, Point, Wall, Room, Opening, Item };

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

    // Where on the plan, in metres, the click was that picked the item. The wall view uses it to choose
    // the side of the wall it shows. Set it just before set(). Empty when the selection did not come from a click.
    void setClickAt(double x, double y) { clickAt_ = QPointF(x, y); }
    std::optional<QPointF> clickAt() const { return clickAt_; }

signals:
    void changed();

private:
    SelectionKind kind_ = SelectionKind::None;
    QString id_;
    std::optional<QPointF> clickAt_;
};

}
