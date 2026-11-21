#include "PropertiesPanel.h"

#include "document/Selection.h"
#include "theme/Theme.h"
#include "tools/ToolController.h"

#include <plynth/core/Document.h>
#include <plynth/core/Geometry.h>
#include <plynth/core/commands/PointCommands.h>
#include <plynth/core/commands/SettingsCommands.h>
#include <plynth/core/commands/WallCommands.h>

#include <QDoubleSpinBox>
#include <QEvent>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cmath>

namespace plynth::app {

namespace {

constexpr int kBorderWidth = 1;
constexpr int kNothingPage = 0;
constexpr int kPointPage = 1;
constexpr int kWallPage = 2;

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}

// Builds the title, the pages for each kind of selection and the Delete button.
PropertiesPanel::PropertiesPanel(plynth::Document& document, Selection& selection, ToolController& controller,
                                 QWidget* parent)
    : QWidget(parent), document_(document), selection_(selection), controller_(controller)
{
    const Theme& t = Theme::instance();
    setFixedWidth(t.propertiesWidth());
    setProperty("tourId", QStringLiteral("panel-properties"));
    setToolTip(tr("Properties of the selected item, or of the plan when nothing is selected"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(t.unit() * 4, t.unit() * 3, t.unit() * 4 + kBorderWidth, t.unit() * 3);
    column->setSpacing(t.unit() * 3);

    auto* title = new QLabel(tr("Properties"), this);
    title->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    column->addWidget(title);

    pages_ = new QStackedWidget(this);
    buildNothingPage();
    buildPointPage();
    buildWallPage();
    column->addWidget(pages_);
    column->addStretch(1);

    deleteButton_ = new QPushButton(tr("Delete"), this);
    deleteButton_->setToolTip(tr("Delete the selected item and what depends on it (Delete)"));
    deleteButton_->setProperty("tourId", QStringLiteral("panel-delete"));
    connect(deleteButton_, &QPushButton::clicked, this, &PropertiesPanel::deleteRequested);
    column->addWidget(deleteButton_);
    editors_.push_back(deleteButton_);

    connect(&selection_, &Selection::changed, this, &PropertiesPanel::refresh);
    connect(&controller_, &ToolController::activeChanged, this, &PropertiesPanel::refresh);
    refresh();
}

// A page with a two column grid: a muted label on the left, the value on the right.
QWidget* PropertiesPanel::makePage(QGridLayout** grid)
{
    const Theme& t = Theme::instance();
    auto* page = new QWidget(pages_);
    *grid = new QGridLayout(page);
    (*grid)->setContentsMargins(0, 0, 0, 0);
    (*grid)->setHorizontalSpacing(t.unit() * 3);
    (*grid)->setVerticalSpacing(t.unit() * 2);
    (*grid)->setColumnStretch(1, 1);
    pages_->addWidget(page);
    return page;
}

// Adds a row with a text value, in JetBrains Mono unless mono is false.
QLabel* PropertiesPanel::addLabelRow(QGridLayout* grid, const QString& label, bool mono)
{
    const Theme& t = Theme::instance();
    auto* name = new QLabel(label, grid->parentWidget());
    name->setFont(t.uiFont(t.sizeSmall()));
    name->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    auto* value = new QLabel(grid->parentWidget());
    value->setFont(mono ? t.monoFont(t.sizeNormal()) : t.uiFont(t.sizeNormal()));
    value->setTextInteractionFlags(Qt::NoTextInteraction);
    value->setWordWrap(true);
    const int row = grid->rowCount();
    grid->addWidget(name, row, 0, Qt::AlignTop);
    grid->addWidget(value, row, 1);
    return value;
}

// Adds a row with a metre spin box in JetBrains Mono.
QDoubleSpinBox* PropertiesPanel::addSpinRow(QGridLayout* grid, const QString& label, double min, double max,
                                            double step, const QString& tip, const QString& tourId,
                                            QLabel** labelOut)
{
    const Theme& t = Theme::instance();
    auto* name = new QLabel(label, grid->parentWidget());
    name->setFont(t.uiFont(t.sizeSmall()));
    name->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    auto* spin = new QDoubleSpinBox(grid->parentWidget());
    spin->setDecimals(3);
    spin->setRange(min, max);
    spin->setSingleStep(step);
    spin->setSuffix(tr(" m"));
    spin->setFont(t.monoFont(t.sizeNormal()));
    spin->setKeyboardTracking(false);
    spin->setToolTip(tip);
    spin->setProperty("tourId", tourId);
    name->setToolTip(tip);
    const int row = grid->rowCount();
    grid->addWidget(name, row, 0, Qt::AlignVCenter);
    grid->addWidget(spin, row, 1);
    editors_.push_back(spin);
    if (labelOut != nullptr)
        *labelOut = name;
    return spin;
}

// The page for nothing selected: scale, grid, counts and the thickness for new walls.
void PropertiesPanel::buildNothingPage()
{
    QGridLayout* grid = nullptr;
    makePage(&grid);
    scaleValue_ = addLabelRow(grid, tr("Scale"));
    scaleValue_->setToolTip(tr("Pixels of the plan image per metre. Set it with the Scale tool (S)."));
    scaleHint_ = new QLabel(grid->parentWidget());
    grid->addWidget(scaleHint_, grid->rowCount(), 0, 1, 2);
    scaleHint_->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::instance().muted().name()));
    scaleHint_->setFont(Theme::instance().uiFont(Theme::instance().sizeSmall()));

    gridSpin_ = addSpinRow(grid, tr("Grid size"), 0.01, 100.0, 0.05,
                           tr("Distance between grid lines. Walls snap to the grid."), QStringLiteral("panel-grid-size"));
    connect(gridSpin_, &QDoubleSpinBox::editingFinished, this, [this]() {
        if (refreshing_)
            return;
        if (std::abs(gridSpin_->value() - document_.project().settings.gridSize) < 1e-9)
            return;
        controller_.run(std::make_unique<plynth::SetGridSizeCommand>(gridSpin_->value()));
    });

    pointCount_ = addLabelRow(grid, tr("Points"));
    wallCount_ = addLabelRow(grid, tr("Walls"));
    roomCount_ = addLabelRow(grid, tr("Rooms"));

    newWallSpin_ = addSpinRow(grid, tr("New walls"), 0.01, 10.0, 0.01,
                              tr("Thickness of the walls the Wall tool draws"), QStringLiteral("panel-new-wall-thickness"),
                              &newWallLabel_);
    newWallSpin_->setValue(controller_.newWallThickness());
    connect(newWallSpin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!refreshing_)
            controller_.setNewWallThickness(value);
    });
}

// The page for a selected point: id, position and the walls that use it.
void PropertiesPanel::buildPointPage()
{
    QGridLayout* grid = nullptr;
    makePage(&grid);
    pointId_ = addLabelRow(grid, tr("Point"));
    pointX_ = addSpinRow(grid, tr("X"), -100000.0, 100000.0, 0.05, tr("Position of the point, from the left edge of the image"),
                         QStringLiteral("panel-point-x"));
    pointY_ = addSpinRow(grid, tr("Y"), -100000.0, 100000.0, 0.05, tr("Position of the point, from the top edge of the image"),
                         QStringLiteral("panel-point-y"));
    pointWalls_ = addLabelRow(grid, tr("Walls"));
    auto move = [this]() {
        if (refreshing_ || selection_.kind() != SelectionKind::Point)
            return;
        const plynth::Floor* floor = controller_.floor();
        const plynth::Point* point = floor ? plynth::findPoint(*floor, selection_.id().toStdString()) : nullptr;
        if (point == nullptr)
            return;
        if (std::abs(point->x - pointX_->value()) < 1e-9 && std::abs(point->y - pointY_->value()) < 1e-9)
            return;
        controller_.run(std::make_unique<plynth::MovePointCommand>(point->id, pointX_->value(), pointY_->value()));
    };
    connect(pointX_, &QDoubleSpinBox::editingFinished, this, move);
    connect(pointY_, &QDoubleSpinBox::editingFinished, this, move);
}

// The page for a selected wall: id, ends, length and thickness.
void PropertiesPanel::buildWallPage()
{
    QGridLayout* grid = nullptr;
    makePage(&grid);
    wallId_ = addLabelRow(grid, tr("Wall"));
    wallFrom_ = addLabelRow(grid, tr("From"));
    wallTo_ = addLabelRow(grid, tr("To"));
    wallLength_ = addLabelRow(grid, tr("Length"));
    wallLength_->setToolTip(tr("Length of the wall between its two points"));
    wallThickness_ = addSpinRow(grid, tr("Thickness"), 0.01, 10.0, 0.01, tr("Thickness of the wall"),
                                QStringLiteral("panel-wall-thickness"));
    wallThickness_->installEventFilter(this);
    connect(wallThickness_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (refreshing_ || selection_.kind() != SelectionKind::Wall)
            return;
        controller_.run(std::make_unique<plynth::SetWallThicknessCommand>(selection_.id().toStdString(), value,
                                                                          thicknessKey_));
    });
}

// Gives the thickness box a new merge key each time it gets focus, so one visit is one undo step.
bool PropertiesPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == wallThickness_ && event->type() == QEvent::FocusIn)
        ++thicknessKey_;
    return QWidget::eventFilter(watched, event);
}

// Sets a spin box without sending its signals, and without touching it when it already shows the value.
void PropertiesPanel::setSpin(QDoubleSpinBox* spin, double value)
{
    if (std::abs(spin->value() - value) < 1e-9)
        return;
    const bool before = spin->blockSignals(true);
    spin->setValue(value);
    spin->blockSignals(before);
}

// Shows the page for the current selection and fills it from the document.
void PropertiesPanel::refresh()
{
    refreshing_ = true;
    const bool readOnly = document_.isReadOnly();
    int page = kNothingPage;
    switch (selection_.kind()) {
    case SelectionKind::Point:
        page = kPointPage;
        refreshPoint();
        break;
    case SelectionKind::Wall:
        page = kWallPage;
        refreshWall();
        break;
    default:
        refreshNothing();
        break;
    }
    pages_->setCurrentIndex(page);
    deleteButton_->setVisible(!selection_.isEmpty());
    for (QWidget* editor : editors_)
        editor->setEnabled(!readOnly);
    refreshing_ = false;
}

// Fills the page for nothing selected.
void PropertiesPanel::refreshNothing()
{
    const plynth::Plan& plan = document_.plan();
    if (plan.scale.pixelsPerMetre.has_value()) {
        scaleValue_->setText(tr("%1 px per m").arg(*plan.scale.pixelsPerMetre, 0, 'f', 1));
        scaleHint_->setText(QString());
    } else {
        scaleValue_->setText(tr("Not set"));
        scaleHint_->setText(tr("Use the Scale tool (S)"));
    }
    scaleHint_->setVisible(!scaleHint_->text().isEmpty());
    setSpin(gridSpin_, document_.project().settings.gridSize);
    const plynth::Floor* floor = controller_.floor();
    pointCount_->setText(QString::number(floor ? floor->points.size() : 0));
    wallCount_->setText(QString::number(floor ? floor->walls.size() : 0));
    roomCount_->setText(QString::number(floor ? floor->rooms.size() : 0));
    setSpin(newWallSpin_, controller_.newWallThickness());
    const bool wallTool = controller_.active() == ToolId::Wall;
    newWallSpin_->setVisible(wallTool);
    newWallLabel_->setVisible(wallTool);
}

// Fills the page for a selected point.
void PropertiesPanel::refreshPoint()
{
    const plynth::Floor* floor = controller_.floor();
    const plynth::Point* point = floor ? plynth::findPoint(*floor, selection_.id().toStdString()) : nullptr;
    if (point == nullptr)
        return;
    pointId_->setText(fromCore(point->id));
    setSpin(pointX_, point->x);
    setSpin(pointY_, point->y);
    QStringList walls;
    for (const plynth::Wall& wall : floor->walls)
        if (wall.from == point->id || wall.to == point->id)
            walls << fromCore(wall.id);
    pointWalls_->setText(walls.isEmpty() ? tr("None") : walls.join(QStringLiteral(", ")));
}

// Fills the page for a selected wall.
void PropertiesPanel::refreshWall()
{
    const plynth::Floor* floor = controller_.floor();
    const std::string id = selection_.id().toStdString();
    const plynth::Wall* wall = nullptr;
    if (floor != nullptr)
        for (const plynth::Wall& candidate : floor->walls)
            if (candidate.id == id)
                wall = &candidate;
    if (wall == nullptr)
        return;
    wallId_->setText(fromCore(wall->id));
    wallFrom_->setText(fromCore(wall->from));
    wallTo_->setText(fromCore(wall->to));
    wallLength_->setText(tr("%1 m").arg(plynth::wallLength(*floor, *wall), 0, 'f', 3));
    setSpin(wallThickness_, wall->thickness);
}

// Paints the Panel background and the Line border on the left.
void PropertiesPanel::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.fillRect(rect(), t.panel());
    p.fillRect(QRect(0, 0, kBorderWidth, height()), t.line());
}

}
