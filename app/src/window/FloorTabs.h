#pragma once

#include <QList>
#include <QMenu>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QPushButton;

namespace plynth::app {

class FloorFocus;

// One tab for each floor, lowest first, with the shown floor in blue. Clicking a tab shows that floor. The
// plus button adds a floor above the top one. The context menu of a tab sets or replaces its plan image,
// renames it, adds a floor below it or removes it, and a double click renames. The tabs only ask: the window
// runs the commands.
class FloorTabs : public QWidget {
    Q_OBJECT
public:
    FloorTabs(FloorFocus& focus, QWidget* parent = nullptr);

    // Rebuilds the tabs from the floors of the plan: id and display name, lowest first.
    void setFloors(const QList<QPair<QString, QString>>& floors);
    // Checks the tab of the shown floor.
    void setShown(const QString& floorId);
    // Names the floors that have a plan image, so their menu offers to replace it instead of setting one.
    void setImageFloors(const QSet<QString>& floorIds);
    // For checks only: opens the menu of a floor's tab without waiting, so a picture can show it. Null for an
    // unknown floor.
    QMenu* popupMenuForCheck(const QString& floorId);
    // For checks only: the menu popupMenuForCheck opened, while it is open.
    QMenu* checkMenu() const { return checkMenu_; }
    // False while the project is read only: the plus button and the menu entries that change the plan are off.
    void setEditable(bool editable);

signals:
    void planImageRequested(const QString& floorId);
    void addAboveRequested();
    void addBelowRequested(const QString& floorId);
    void renameRequested(const QString& floorId);
    void removeRequested(const QString& floorId);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QMenu* makeMenu(const QString& floorId);
    void showMenu(const QString& floorId, const QPoint& global);

    FloorFocus& focus_;
    QHBoxLayout* row_ = nullptr;
    QList<QPushButton*> tabs_;
    QPushButton* add_ = nullptr;
    QString shown_;
    QSet<QString> imageFloors_;
    QPointer<QMenu> checkMenu_;
    bool editable_ = true;
};

}
