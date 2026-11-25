#include "FloorTabs.h"

#include "document/FloorFocus.h"
#include "theme/TabButton.h"
#include "theme/Theme.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPushButton>

namespace plynth::app {

// Builds the row with the plus button at its end.
FloorTabs::FloorTabs(FloorFocus& focus, QWidget* parent) : QWidget(parent), focus_(focus)
{
    const Theme& t = Theme::instance();
    setFixedHeight(t.buildBarHeight());

    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(t.unit() * 3, t.unit() * 2, t.unit() * 3, 0);
    row_->setSpacing(0);
    add_ = makeTabButton(QStringLiteral("+"), tr("Add a floor above the top floor"), this,
                         t.unit() * 2, t.unit() * 3);
    add_->setCheckable(false);
    connect(add_, &QPushButton::clicked, this, &FloorTabs::addAboveRequested);
    row_->addWidget(add_);
    row_->addStretch(1);
}

// Replaces the tabs, keeping the plus button last.
void FloorTabs::setFloors(const QList<QPair<QString, QString>>& floors)
{
    const Theme& t = Theme::instance();
    for (QPushButton* tab : tabs_) {
        row_->removeWidget(tab);
        tab->deleteLater();
    }
    tabs_.clear();
    int at = 0;
    for (const auto& [id, name] : floors) {
        QPushButton* tab = makeTabButton(name, tr("Show floor %1 (%2)").arg(name, id),
                                         this, t.unit() * 2, t.unit() * 4);
        tab->setProperty("floorId", id);
        tab->setChecked(id == shown_);
        tab->setToolTip(tr("Show floor %1 (%2). Double click to rename it, right click for more").arg(name, id));
        tab->installEventFilter(this);
        connect(tab, &QPushButton::clicked, this, [this, id]() {
            focus_.setFloorId(id);
            setShown(focus_.floorId());
        });
        row_->insertWidget(at++, tab);
        tabs_.append(tab);
    }
}

// Checks the tab of the floor in view.
void FloorTabs::setShown(const QString& floorId)
{
    shown_ = floorId;
    for (QPushButton* tab : tabs_)
        tab->setChecked(tab->property("floorId").toString() == floorId);
}

// Turns the controls that change the plan on or off.
void FloorTabs::setEditable(bool editable)
{
    editable_ = editable;
    add_->setEnabled(editable);
}

// Opens the menu of a tab, and sends what the user picks.
void FloorTabs::showMenu(const QString& floorId, const QPoint& global)
{
    QMenu menu(this);
    menu.setToolTipsVisible(true);
    QAction* rename = menu.addAction(tr("Rename..."));
    rename->setToolTip(tr("Give this floor another name"));
    QAction* below = menu.addAction(tr("Add floor below"));
    below->setToolTip(tr("Add an empty floor under this one"));
    menu.addSeparator();
    QAction* remove = menu.addAction(tr("Remove floor..."));
    remove->setToolTip(tr("Remove this floor with everything on it"));
    for (QAction* action : {rename, below, remove})
        action->setEnabled(editable_);
    remove->setEnabled(editable_ && tabs_.size() > 1);
    if (tabs_.size() <= 1)
        remove->setToolTip(tr("The only floor cannot be removed"));
    QAction* picked = menu.exec(global);
    if (picked == rename)
        emit renameRequested(floorId);
    else if (picked == below)
        emit addBelowRequested(floorId);
    else if (picked == remove)
        emit removeRequested(floorId);
}

// Handles the right click and the double click on a tab.
bool FloorTabs::eventFilter(QObject* watched, QEvent* event)
{
    auto* tab = qobject_cast<QPushButton*>(watched);
    if (tab != nullptr && tabs_.contains(tab)) {
        if (event->type() == QEvent::ContextMenu) {
            showMenu(tab->property("floorId").toString(), static_cast<QContextMenuEvent*>(event)->globalPos());
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            if (editable_)
                emit renameRequested(tab->property("floorId").toString());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// Draws the bar background and the line along its top edge.
void FloorTabs::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.fillRect(rect(), t.panel());
    p.fillRect(QRect(0, 0, width(), 1), t.line());
}

}
