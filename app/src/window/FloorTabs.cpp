#include "FloorTabs.h"

#include "document/FloorFocus.h"
#include "theme/TabButton.h"
#include "theme/Theme.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>

namespace plynth::app {

// Builds the row with the disabled plus button at its end.
FloorTabs::FloorTabs(FloorFocus& focus, QWidget* parent) : QWidget(parent), focus_(focus)
{
    const Theme& t = Theme::instance();
    setFixedHeight(t.buildBarHeight());
    setProperty("tourId", QStringLiteral("floor-tabs"));
    setToolTip(tr("The floors of the building, lowest first. Click one to work on it"));

    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(t.unit() * 3, t.unit() * 2, t.unit() * 3, 0);
    row_->setSpacing(0);
    add_ = makeTabButton(QStringLiteral("+"), tr("Add a floor (not available yet)"), QStringLiteral("floor-add"), this,
                         t.unit() * 2, t.unit() * 3);
    add_->setCheckable(false);
    add_->setEnabled(false);
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
                                         QStringLiteral("floor-tab"), this, t.unit() * 2, t.unit() * 4);
        tab->setProperty("floorId", id);
        tab->setChecked(id == shown_);
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

// Draws the bar background and its bottom line.
void FloorTabs::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.fillRect(rect(), t.panel());
    p.fillRect(QRect(0, height() - 1, width(), 1), t.line());
}

}
