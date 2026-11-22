#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QPushButton;

namespace plynth::app {

class FloorFocus;

// One tab for each floor, lowest first, with the shown floor in blue. Clicking a tab shows that floor. The
// plus button is shown but disabled until floors can be added from here.
class FloorTabs : public QWidget {
    Q_OBJECT
public:
    FloorTabs(FloorFocus& focus, QWidget* parent = nullptr);

    // Rebuilds the tabs from the floors of the plan: id and display name, lowest first.
    void setFloors(const QList<QPair<QString, QString>>& floors);
    // Checks the tab of the shown floor.
    void setShown(const QString& floorId);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    FloorFocus& focus_;
    QHBoxLayout* row_ = nullptr;
    QList<QPushButton*> tabs_;
    QPushButton* add_ = nullptr;
    QString shown_;
};

}
