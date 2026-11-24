#include "WallView.h"

#include "catalog/LibraryHost.h"
#include "document/Selection.h"
#include "theme/Theme.h"
#include "tools/ToolController.h"
#include "wallview/WallFaceView.h"
#include "wallview/WallViewData.h"

#include <plynth/core/Document.h>
#include <plynth/core/Items.h>
#include <plynth/core/WallGeometry.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace plynth::app {

namespace {

// Text with a number in mono and the words around it in muted UI text.
QString measured(const QString& number, const QString& words)
{
    const Theme& t = Theme::instance();
    return QStringLiteral("<span style=\"font-family:'%1'; color:%2\">%3</span> <span style=\"color:%4\">%5</span>")
        .arg(t.monoFamily(), t.text().name(), number, t.muted().name(), words);
}

}

// Builds the header line with the Flip button and the face below it.
WallView::WallView(plynth::Document& document, Selection& selection, ToolController& controller, QWidget* parent)
    : QWidget(parent), document_(document), selection_(selection), controller_(controller)
{
    const Theme& t = Theme::instance();
    setProperty("tourId", QStringLiteral("wall-view"));
    setToolTip(tr("The face of the selected wall at true scale. Drag openings to move them, drag their edges to resize."));
    setMinimumHeight(t.wallViewMinHeight());

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    auto* header = new QWidget(this);
    auto* row = new QHBoxLayout(header);
    row->setContentsMargins(t.unit() * 4, t.unit() * 2, t.unit() * 4, t.unit() * 2);
    row->setSpacing(t.unit() * 4);

    title_ = new QLabel(header);
    title_->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    title_->setToolTip(tr("The wall shown"));
    length_ = new QLabel(header);
    length_->setTextFormat(Qt::RichText);
    length_->setFont(t.uiFont(t.sizeNormal()));
    length_->setToolTip(tr("Length of the wall along its centre line"));
    height_ = new QLabel(header);
    height_->setTextFormat(Qt::RichText);
    height_->setFont(t.uiFont(t.sizeNormal()));
    height_->setToolTip(tr("Height of the wall"));
    sideLabel_ = new QLabel(header);
    sideLabel_->setFont(t.uiFont(t.sizeNormal()));
    sideLabel_->setToolTip(tr("Which side of the wall the picture shows"));
    flipButton_ = new QPushButton(tr("Flip"), header);
    flipButton_->setToolTip(tr("See the other side of the wall (F)"));
    flipButton_->setProperty("tourId", QStringLiteral("wall-view-flip"));
    flipButton_->setFocusPolicy(Qt::NoFocus);
    row->addWidget(title_);
    row->addWidget(length_);
    row->addWidget(height_);
    row->addWidget(sideLabel_);
    row->addStretch(1);
    row->addWidget(flipButton_);
    column->addWidget(header);

    face_ = new WallFaceView(&controller_, this);
    column->addWidget(face_, 1);

    connect(flipButton_, &QPushButton::clicked, this, &WallView::flip);
    connect(&selection_, &Selection::changed, this, &WallView::refresh);
    if (LibraryHost* library = controller_.library())
        connect(library, &LibraryHost::generatorsChanged, this, &WallView::refresh);
    refresh();
}

// Paints the Panel background behind the header.
void WallView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), Theme::instance().panel());
}

// Chooses the side the face is seen from and redraws.
void WallView::setSide(plynth::WallSide side)
{
    side_ = side;
    refresh();
}

// Shows the other side of the wall.
void WallView::flip()
{
    side_ = plynth::otherSide(side_);
    refresh();
}

// Finds the wall to show from the selection, works out the side, and redraws the header and the face.
void WallView::refresh()
{
    const plynth::Floor* floor = controller_.floor();
    const plynth::Wall* wall = nullptr;
    QString selectedOpening;
    QString selectedItem;
    if (floor != nullptr) {
        const std::string id = selection_.id().toStdString();
        if (selection_.kind() == SelectionKind::Wall) {
            wall = plynth::findWall(*floor, id);
        } else if (selection_.kind() == SelectionKind::Opening) {
            if (const plynth::Opening* opening = plynth::findOpening(*floor, id)) {
                wall = plynth::findWall(*floor, opening->wall);
                selectedOpening = selection_.id();
            }
        } else if (selection_.kind() == SelectionKind::Item) {
            if (const plynth::Item* item = plynth::findItem(*floor, id)) {
                if (item->wall)
                    wall = plynth::findWall(*floor, *item->wall);
                selectedItem = selection_.id();
                if (wall != nullptr && wall->id != sideWall_) {
                    const plynth::WallItemSpot spot = plynth::wallItemSpot(*floor, *item);
                    if (spot.valid) {
                        sideWall_ = wall->id;
                        side_ = spot.side;
                    }
                }
            }
        }
    }

    const bool had = hasTarget_;
    hasTarget_ = wall != nullptr;
    if (!hasTarget_) {
        face_->setData(WallViewData{});
        if (had)
            emit targetChanged(false);
        return;
    }

    if (wall->id != sideWall_) {
        sideWall_ = wall->id;
        side_ = plynth::WallSide::Right;
        if (selection_.clickAt().has_value())
            side_ = plynth::wallSideOf(*floor, *wall,
                                       plynth::Vec2{selection_.clickAt()->x(), selection_.clickAt()->y()});
    }

    const LibraryHost* library = controller_.library();
    const WallViewData view =
        buildWallViewData(*floor, *wall, side_, selectedOpening, selectedItem, controller_.catalog(),
                          library != nullptr ? &library->generators() : nullptr,
                          library != nullptr ? library->addonCache() : std::optional<plynth::AddonCache>{});
    face_->setData(view);
    title_->setText(tr("Wall %1").arg(view.wallId));
    length_->setText(measured(QString::number(view.length, 'f', 2) + QStringLiteral(" m"), tr("long")));
    height_->setText(measured(QString::number(view.height, 'f', 2) + QStringLiteral(" m"), tr("high")));
    sideLabel_->setText(QStringLiteral("<span style=\"color:%1\">%2</span>")
                            .arg(Theme::instance().muted().name(),
                                 side_ == plynth::WallSide::Right ? tr("seen from the right") : tr("seen from the left")));
    sideLabel_->setTextFormat(Qt::RichText);
    if (!had)
        emit targetChanged(true);
}

}
