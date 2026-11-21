#include "PropertiesPanel.h"

#include "detection/DetectionSession.h"
#include "document/Selection.h"
#include "theme/Theme.h"
#include "tools/ToolController.h"

#include <plynth/core/Document.h>
#include <plynth/core/Geometry.h>
#include <plynth/core/RoomTypes.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/WallGeometry.h>
#include <plynth/core/commands/PointCommands.h>
#include <plynth/core/commands/RoomCommands.h>
#include <plynth/core/commands/SettingsCommands.h>
#include <plynth/core/commands/WallCommands.h>
#include <plynth/core/detection/Detector.h>

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace plynth::app {

namespace {

constexpr int kBorderWidth = 1;
constexpr int kNothingPage = 0;
constexpr int kPointPage = 1;
constexpr int kWallPage = 2;
constexpr int kRoomPage = 3;
constexpr int kDetectPage = 4;

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
    buildRoomPage();
    buildDetectPage();
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
    connect(&controller_.detection(), &DetectionSession::changed, this, &PropertiesPanel::refresh);
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
    spin->setStyle(Theme::instance().controlStyle());
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

// Adds a spin row whose edits merge into one undo step for each visit to the box.
QDoubleSpinBox* PropertiesPanel::addKeyedSpinRow(QGridLayout* grid, const QString& label, double min, double max,
                                                 double step, const QString& tip, const QString& tourId)
{
    QDoubleSpinBox* spin = addSpinRow(grid, label, min, max, step, tip, tourId);
    spin->installEventFilter(this);
    if (auto* edit = spin->findChild<QLineEdit*>())
        edit->installEventFilter(this);
    keyedSpins_.push_back(spin);
    return spin;
}

// Adds a muted one line note that spans the page. Hidden until it has text.
QLabel* PropertiesPanel::addNote(QGridLayout* grid)
{
    const Theme& t = Theme::instance();
    auto* note = new QLabel(grid->parentWidget());
    note->setFont(t.uiFont(t.sizeSmall()));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    note->setVisible(false);
    grid->addWidget(note, grid->rowCount(), 0, 1, 2);
    return note;
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
    connect(gridSpin_, &QDoubleSpinBox::valueChanged, this, [this]() { gridEdited_ = !refreshing_ || gridEdited_; });
    connect(gridSpin_, &QDoubleSpinBox::editingFinished, this, [this]() {
        if (refreshing_ || !gridEdited_)
            return;
        gridEdited_ = false;
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
    cornerRadius_ = addKeyedSpinRow(grid, tr("Corner radius"), 0.0, 100.0, 0.05,
                                    tr("Rounds the corner with an arc of this radius. It applies where exactly two straight walls meet."),
                                    QStringLiteral("panel-corner-radius"));
    cornerNote_ = addNote(grid);
    connect(cornerRadius_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (refreshing_ || selection_.kind() != SelectionKind::Point)
            return;
        if (!controller_.run(std::make_unique<plynth::SetCornerRadiusCommand>(selection_.id().toStdString(), value,
                                                                              focusKey_)).ok())
            refresh();
    });
    connect(pointX_, &QDoubleSpinBox::valueChanged, this, [this]() { pointXEdited_ = !refreshing_ || pointXEdited_; });
    connect(pointY_, &QDoubleSpinBox::valueChanged, this, [this]() { pointYEdited_ = !refreshing_ || pointYEdited_; });
    // Only a box the user changed counts, so tabbing through a box never rounds the real position.
    auto move = [this]() {
        const bool xEdited = pointXEdited_;
        const bool yEdited = pointYEdited_;
        pointXEdited_ = false;
        pointYEdited_ = false;
        if (refreshing_ || (!xEdited && !yEdited) || selection_.kind() != SelectionKind::Point)
            return;
        const plynth::Floor* floor = controller_.floor();
        const plynth::Point* point = floor ? plynth::findPoint(*floor, selection_.id().toStdString()) : nullptr;
        if (point == nullptr)
            return;
        controller_.run(std::make_unique<plynth::MovePointCommand>(point->id, xEdited ? pointX_->value() : point->x,
                                                                   yEdited ? pointY_->value() : point->y));
    };
    connect(pointX_, &QDoubleSpinBox::editingFinished, this, move);
    connect(pointY_, &QDoubleSpinBox::editingFinished, this, move);
}

// The page for a selected wall: id, ends, length along the curve, thickness and bend.
void PropertiesPanel::buildWallPage()
{
    QGridLayout* grid = nullptr;
    makePage(&grid);
    wallId_ = addLabelRow(grid, tr("Wall"));
    wallFrom_ = addLabelRow(grid, tr("From"));
    wallTo_ = addLabelRow(grid, tr("To"));
    wallLength_ = addLabelRow(grid, tr("Length"));
    wallLength_->setToolTip(tr("Length of the wall along its centre line, along the arc when the wall is bent"));
    wallThickness_ = addKeyedSpinRow(grid, tr("Thickness"), 0.01, 10.0, 0.01, tr("Thickness of the wall"),
                                     QStringLiteral("panel-wall-thickness"));
    connect(wallThickness_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (refreshing_ || selection_.kind() != SelectionKind::Wall)
            return;
        controller_.run(std::make_unique<plynth::SetWallThicknessCommand>(selection_.id().toStdString(), value,
                                                                          focusKey_));
    });

    wallBend_ = addKeyedSpinRow(grid, tr("Bend"), -1000.0, 1000.0, 0.05,
                                tr("How far the middle of the wall is from the straight line between its points. Positive bends to the right of the wall's direction. 0 is straight. The most is half the straight length."),
                                QStringLiteral("panel-wall-bend"));
    connect(wallBend_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (refreshing_ || selection_.kind() != SelectionKind::Wall)
            return;
        if (!controller_.run(std::make_unique<plynth::SetWallBulgeCommand>(selection_.id().toStdString(), value,
                                                                           focusKey_)).ok())
            refresh();
    });
    straightenButton_ = new QPushButton(tr("Straighten"), grid->parentWidget());
    straightenButton_->setToolTip(tr("Make the wall straight again"));
    straightenButton_->setProperty("tourId", QStringLiteral("panel-wall-straighten"));
    grid->addWidget(straightenButton_, grid->rowCount(), 1);
    connect(straightenButton_, &QPushButton::clicked, this, [this]() {
        if (selection_.kind() == SelectionKind::Wall)
            controller_.run(std::make_unique<plynth::SetWallBulgeCommand>(selection_.id().toStdString(), 0.0));
    });
}

// The page for a selected room: id, type, floor area and number of walls.
void PropertiesPanel::buildRoomPage()
{
    const Theme& t = Theme::instance();
    QGridLayout* grid = nullptr;
    makePage(&grid);
    roomId_ = addLabelRow(grid, tr("Room"));

    auto* name = new QLabel(tr("Type"), grid->parentWidget());
    name->setFont(t.uiFont(t.sizeSmall()));
    name->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    roomType_ = new QComboBox(grid->parentWidget());
    roomType_->setFont(t.uiFont(t.sizeNormal()));
    roomType_->setStyle(t.controlStyle());
    roomType_->setToolTip(tr("What kind of room this is"));
    roomType_->setProperty("tourId", QStringLiteral("panel-room-type"));
    name->setToolTip(roomType_->toolTip());
    const int row = grid->rowCount();
    grid->addWidget(name, row, 0, Qt::AlignVCenter);
    grid->addWidget(roomType_, row, 1);
    editors_.push_back(roomType_);
    connect(roomType_, &QComboBox::activated, this, [this](int index) {
        if (refreshing_ || selection_.kind() != SelectionKind::Room)
            return;
        const QVariant data = roomType_->itemData(index);
        std::optional<std::string> type;
        if (data.isValid() && !data.isNull())
            type = data.toString().toStdString();
        const plynth::Floor* floor = controller_.floor();
        const std::string id = selection_.id().toStdString();
        if (floor != nullptr)
            for (const plynth::Room& room : floor->rooms)
                if (room.id == id && room.type == type)
                    return;
        controller_.run(std::make_unique<plynth::SetRoomTypeCommand>(id, type));
    });

    roomArea_ = addLabelRow(grid, tr("Area"));
    roomArea_->setToolTip(tr("Floor area inside the walls"));
    roomWalls_ = addLabelRow(grid, tr("Walls"));
}

// Adds a row with a slider: its name and value on one line, the slider under them.
QSlider* PropertiesPanel::addSliderRow(QGridLayout* grid, const QString& label, int min, int max, const QString& tip,
                                       const QString& tourId, QLabel** valueOut)
{
    const Theme& t = Theme::instance();
    auto* name = new QLabel(label, grid->parentWidget());
    name->setFont(t.uiFont(t.sizeSmall()));
    name->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    name->setToolTip(tip);
    auto* value = new QLabel(grid->parentWidget());
    value->setFont(t.monoFont(t.sizeNormal()));
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setToolTip(tip);
    auto* slider = new QSlider(Qt::Horizontal, grid->parentWidget());
    slider->setRange(min, max);
    slider->setStyle(t.controlStyle());
    slider->setToolTip(tip);
    slider->setProperty("tourId", tourId);
    const int row = grid->rowCount();
    grid->addWidget(name, row, 0, Qt::AlignVCenter);
    grid->addWidget(value, row, 1);
    grid->addWidget(slider, row + 1, 0, 1, 2);
    editors_.push_back(slider);
    *valueOut = value;
    return slider;
}

// The page for the Detect tool: the settings, the status line, Accept and Reset settings.
void PropertiesPanel::buildDetectPage()
{
    const Theme& t = Theme::instance();
    QGridLayout* grid = nullptr;
    QWidget* page = makePage(&grid);
    page->setProperty("tourId", QStringLiteral("panel-detect"));
    page->setToolTip(tr("Settings for finding walls in the plan image"));

    thinSlider_ = addSliderRow(grid, tr("Thinnest wall"), 2, 40,
                               tr("Dark lines thinner than this are not walls. Raise it to drop thin inner walls."),
                               QStringLiteral("panel-detect-thinnest"), &thinValue_);
    thickSlider_ = addSliderRow(grid, tr("Thickest wall"), 10, 100,
                                tr("Dark areas thicker than this are not walls, for example filled blocks."),
                                QStringLiteral("panel-detect-thickest"), &thickValue_);
    sensitivitySlider_ = addSliderRow(grid, tr("Sensitivity"), 0, 100,
                                      tr("Higher counts lighter grey as ink. Lower it for a plan with grey paper."),
                                      QStringLiteral("panel-detect-sensitivity"), &sensitivityValue_);
    lengthSpin_ = addSpinRow(grid, tr("Shortest wall"), 0.05, 5.0, 0.05,
                             tr("Wall pieces shorter than this are dropped."), QStringLiteral("panel-detect-length"));
    lengthSpin_->setDecimals(2);
    openingSpin_ = addSpinRow(grid, tr("Widest opening"), 0.0, 5.0, 0.05,
                              tr("A gap up to this wide is closed with wall and shown as an opening. 0 closes none."),
                              QStringLiteral("panel-detect-opening"));
    openingSpin_->setDecimals(2);

    const auto addCheck = [&](const QString& text, const QString& tip, const QString& tourId) {
        auto* box = new QCheckBox(text, page);
        box->setFont(t.uiFont(t.sizeNormal()));
        box->setStyle(t.controlStyle());
        box->setToolTip(tip);
        box->setProperty("tourId", tourId);
        grid->addWidget(box, grid->rowCount(), 0, 1, 2);
        editors_.push_back(box);
        return box;
    };
    roomsCheck_ = addCheck(tr("Propose rooms"), tr("Also propose a room for every closed area"),
                           QStringLiteral("panel-detect-rooms"));
    openingsCheck_ = addCheck(tr("Show openings"), tr("Show doors, windows and gaps found in the walls"),
                              QStringLiteral("panel-detect-openings"));

    detectorLabel_ = new QLabel(tr("Detector"), page);
    detectorLabel_->setFont(t.uiFont(t.sizeSmall()));
    detectorLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    detectorCombo_ = new QComboBox(page);
    detectorCombo_->setFont(t.uiFont(t.sizeNormal()));
    detectorCombo_->setStyle(t.controlStyle());
    detectorCombo_->setToolTip(tr("The way walls are found"));
    detectorCombo_->setProperty("tourId", QStringLiteral("panel-detect-detector"));
    for (const plynth::Detector* detector : plynth::allDetectors()) {
        const QString id = QString::fromUtf8(detector->id().data(), static_cast<qsizetype>(detector->id().size()));
        const QString text = QString::fromUtf8(detector->name().data(), static_cast<qsizetype>(detector->name().size()));
        detectorCombo_->addItem(text, id);
        detectorCombo_->setItemData(detectorCombo_->count() - 1,
                                    QString::fromUtf8(detector->description().data(),
                                                      static_cast<qsizetype>(detector->description().size())),
                                    Qt::ToolTipRole);
    }
    const int detectorRow = grid->rowCount();
    grid->addWidget(detectorLabel_, detectorRow, 0, Qt::AlignVCenter);
    grid->addWidget(detectorCombo_, detectorRow, 1);
    detectorLabel_->setVisible(detectorCombo_->count() > 1);
    detectorCombo_->setVisible(detectorCombo_->count() > 1);
    editors_.push_back(detectorCombo_);

    detectStatus_ = new QLabel(page);
    detectStatus_->setFont(t.uiFont(t.sizeSmall()));
    detectStatus_->setWordWrap(true);
    detectStatus_->setToolTip(tr("What detection found, or why it could not run"));
    detectStatus_->setProperty("tourId", QStringLiteral("panel-detect-status"));
    grid->addWidget(detectStatus_, grid->rowCount(), 0, 1, 2);

    auto* note = new QLabel(tr("Openings are shown for checking. They are not added yet."), page);
    note->setFont(t.uiFont(t.sizeSmall()));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    grid->addWidget(note, grid->rowCount(), 0, 1, 2);

    acceptButton_ = new QPushButton(tr("Accept"), page);
    acceptButton_->setDefault(true);
    acceptButton_->setToolTip(tr("Add the new walls and rooms to the plan (Enter)"));
    acceptButton_->setProperty("tourId", QStringLiteral("detect-accept"));
    resetButton_ = new QPushButton(tr("Reset settings"), page);
    resetButton_->setToolTip(tr("Put every detection setting back to its default"));
    resetButton_->setProperty("tourId", QStringLiteral("panel-detect-reset"));
    editors_.push_back(resetButton_);
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(t.unit() * 2);
    buttons->addWidget(acceptButton_);
    buttons->addWidget(resetButton_);
    grid->addLayout(buttons, grid->rowCount(), 0, 1, 2);

    connect(thinSlider_, &QSlider::valueChanged, this, [this]() { detectSettingsEdited(thinSlider_); });
    connect(thickSlider_, &QSlider::valueChanged, this, [this]() { detectSettingsEdited(thickSlider_); });
    connect(sensitivitySlider_, &QSlider::valueChanged, this, [this]() { detectSettingsEdited(sensitivitySlider_); });
    connect(lengthSpin_, &QDoubleSpinBox::valueChanged, this, [this]() { detectSettingsEdited(lengthSpin_); });
    connect(openingSpin_, &QDoubleSpinBox::valueChanged, this, [this]() { detectSettingsEdited(openingSpin_); });
    connect(roomsCheck_, &QCheckBox::toggled, this, [this]() { detectSettingsEdited(roomsCheck_); });
    connect(openingsCheck_, &QCheckBox::toggled, this, [this]() { detectSettingsEdited(openingsCheck_); });
    connect(detectorCombo_, &QComboBox::activated, this, [this](int index) {
        if (!refreshing_)
            controller_.detection().setDetectorId(detectorCombo_->itemData(index).toString().toStdString());
    });
    connect(acceptButton_, &QPushButton::clicked, this, [this]() { controller_.detection().accept(); });
    connect(resetButton_, &QPushButton::clicked, this, [this]() { controller_.detection().resetParams(); });
    for (QDoubleSpinBox* spin : {lengthSpin_, openingSpin_}) {
        spin->installEventFilter(this);
        if (auto* edit = spin->findChild<QLineEdit*>())
            edit->installEventFilter(this);
    }
}

// Reads the widgets into settings and gives them to the session. Keeps the thinnest wall below the thickest.
void PropertiesPanel::detectSettingsEdited(const QObject* from)
{
    if (refreshing_)
        return;
    int thin = thinSlider_->value();
    int thick = thickSlider_->value();
    if (from == thinSlider_ && thin >= thick)
        thick = std::min(thickSlider_->maximum(), thin + 1);
    else if (from == thickSlider_ && thick <= thin)
        thin = std::max(thinSlider_->minimum(), thick - 1);
    plynth::DetectionParams params = controller_.detection().params();
    params.minThickness = thin / 100.0;
    params.maxThickness = thick / 100.0;
    params.sensitivity = sensitivitySlider_->value() / 100.0;
    params.minLength = lengthSpin_->value();
    params.maxOpeningWidth = openingSpin_->value();
    params.rooms = roomsCheck_->isChecked();
    params.openings = openingsCheck_->isChecked();
    controller_.detection().setParams(params);
}

// Fills the Detect page from the session.
void PropertiesPanel::refreshDetect()
{
    const Theme& t = Theme::instance();
    const DetectionSession& session = controller_.detection();
    const plynth::DetectionParams& params = session.params();
    const auto setSlider = [](QSlider* slider, int value) {
        if (slider->value() == value)
            return;
        const bool before = slider->blockSignals(true);
        slider->setValue(value);
        slider->blockSignals(before);
    };
    const auto setCheck = [](QCheckBox* box, bool on) {
        if (box->isChecked() == on)
            return;
        const bool before = box->blockSignals(true);
        box->setChecked(on);
        box->blockSignals(before);
    };
    setSlider(thinSlider_, qRound(params.minThickness * 100.0));
    setSlider(thickSlider_, qRound(params.maxThickness * 100.0));
    setSlider(sensitivitySlider_, qRound(params.sensitivity * 100.0));
    thinValue_->setText(QStringLiteral("%1 m").arg(params.minThickness, 0, 'f', 2));
    thickValue_->setText(QStringLiteral("%1 m").arg(params.maxThickness, 0, 'f', 2));
    sensitivityValue_->setText(QStringLiteral("%1 %").arg(qRound(params.sensitivity * 100.0)));
    setSpin(lengthSpin_, params.minLength);
    setSpin(openingSpin_, params.maxOpeningWidth);
    setCheck(roomsCheck_, params.rooms);
    setCheck(openingsCheck_, params.openings);
    const int detector = detectorCombo_->findData(fromCore(session.detectorId()));
    if (detector >= 0)
        detectorCombo_->setCurrentIndex(detector);

    detectStatus_->setText(session.statusText());
    detectStatus_->setStyleSheet(QStringLiteral("color: %1;")
                                     .arg((session.statusIsRefusal() ? t.signalAmber() : t.text()).name()));
    acceptButton_->setEnabled(session.canAccept() && !document_.isReadOnly());
}

// Fills the page for a selected room.
void PropertiesPanel::refreshRoom()
{
    const plynth::Floor* floor = controller_.floor();
    const std::string id = selection_.id().toStdString();
    const plynth::Room* room = nullptr;
    if (floor != nullptr)
        for (const plynth::Room& candidate : floor->rooms)
            if (candidate.id == id)
                room = &candidate;
    if (room == nullptr)
        return;
    roomId_->setText(fromCore(room->id));

    roomType_->clear();
    if (room->type.has_value() && plynth::findRoomType(*room->type) == nullptr)
        roomType_->addItem(fromCore(*room->type), fromCore(*room->type));
    for (const plynth::RoomType& type : plynth::builtInRoomTypes())
        roomType_->addItem(QString::fromUtf8(type.name.data(), static_cast<qsizetype>(type.name.size())),
                           QString::fromUtf8(type.id.data(), static_cast<qsizetype>(type.id.size())));
    roomType_->addItem(tr("No type yet"), QVariant());
    int current = roomType_->count() - 1;
    if (room->type.has_value())
        current = roomType_->findData(fromCore(*room->type));
    roomType_->setCurrentIndex(current);

    const plynth::RoomShape shape = plynth::roomShape(*floor, *room);
    roomArea_->setText(QStringLiteral("%1 m\u00B2").arg(shape.area, 0, 'f', 2));
    roomWalls_->setText(QString::number(room->walls.size()));
}

// Gives the thickness box a new merge key each time it gets focus, so one visit is one undo step.
bool PropertiesPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn)
        for (QDoubleSpinBox* spin : keyedSpins_)
            if (watched == spin || watched->parent() == spin)
                ++focusKey_;
    if (event->type() == QEvent::ShortcutOverride) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        const bool inSpin = qobject_cast<QAbstractSpinBox*>(watched) != nullptr ||
                            qobject_cast<QAbstractSpinBox*>(watched->parent()) != nullptr;
        if (inSpin && (key == Qt::Key_Return || key == Qt::Key_Enter))
            event->accept();
    }
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
    const bool detecting = controller_.active() == ToolId::Detect;
    if (detecting) {
        page = kDetectPage;
        refreshDetect();
    }
    switch (detecting ? SelectionKind::None : selection_.kind()) {
    case SelectionKind::Point:
        page = kPointPage;
        refreshPoint();
        break;
    case SelectionKind::Wall:
        page = kWallPage;
        refreshWall();
        break;
    case SelectionKind::Room:
        page = kRoomPage;
        refreshRoom();
        break;
    default:
        if (!detecting)
            refreshNothing();
        break;
    }
    pages_->setCurrentIndex(page);
    deleteButton_->setVisible(!selection_.isEmpty() && !detecting);
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
    setSpin(cornerRadius_, point->cornerRadius);
    const plynth::CornerRounding rounding = plynth::cornerRounding(*floor, *point);
    QString note;
    if (point->cornerRadius > 0.0 && !rounding.rounded)
        note = tr("Not rounded: needs exactly two straight walls");
    else if (rounding.rounded && std::abs(rounding.radius - point->cornerRadius) > 1e-6)
        note = tr("Rounded at %1 m").arg(rounding.radius, 0, 'f', 2);
    cornerNote_->setText(note);
    cornerNote_->setVisible(!note.isEmpty());
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
    setSpin(wallBend_, wall->bulge);
    straightenButton_->setEnabled(!document_.isReadOnly() && std::abs(wall->bulge) >= plynth::kMinBulge);
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
