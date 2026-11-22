#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QPushButton;

namespace plynth::app {

class FloorFocus;

// One tab for each floor, lowest first, with the shown floor in blue. Clicking a tab shows that floor. The
// plus button adds a floor above the top one. The context menu of a tab renames it, adds a floor below it or
// removes it, and a double click renames. The tabs only ask: the window runs the commands.
class FloorTabs : public QWidget {
    Q_OBJECT
public:
    FloorTabs(FloorFocus& focus, QWidget* parent = nullptr);

    // Rebuilds the tabs from the floors of the plan: id and display name, lowest first.
    void setFloors(const QList<QPair<QString, QString>>& floors);
    // Checks the tab of the shown floor.
    void setShown(const QString& floorId);
    // False while the project is read only: the plus button and the menu entries that change the plan are off.
    void setEditable(bool editable);

signals:
    void addAboveRequested();
    void addBelowRequested(const QString& floorId);
    void renameRequested(const QString& floorId);
    void removeRequested(const QString& floorId);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showMenu(const QString& floorId, const QPoint& global);

    FloorFocus& focus_;
    QHBoxLayout* row_ = nullptr;
    QList<QPushButton*> tabs_;
    QPushButton* add_ = nullptr;
    QString shown_;
    bool editable_ = true;
};

}
