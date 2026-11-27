#include "CanopyGroup.h"

#include "catalog/LibraryHost.h"
#include "document/Selection.h"
#include "furniture/AssetPreview.h"
#include "materials/MaterialPicker.h"
#include "materials/MaterialRow.h"
#include "theme/Theme.h"
#include "tools/ToolController.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/Canopies.h>
#include <plynth/core/Document.h>
#include <plynth/core/Geometry.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/catalog/MaterialCatalog.h>
#include <plynth/core/commands/CanopyCommands.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QStringList>
#include <QTimer>

#include <cmath>
#include <memory>

namespace plynth::app {

namespace {

// How long after the last change the build problems are worked out again, in milliseconds.
constexpr int kProblemDelay = 250;

// Turns a core text into Qt text.
QString fromCore(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Adds a muted title that spans the page, above a group of rows.
void addTitle(QGridLayout* grid, const QString& title)
{
    const Theme& t = Theme::instance();
    auto* label = new QLabel(title, grid->parentWidget());
    label->setFont(t.uiFont(t.sizeSmall(), QFont::DemiBold));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    const int row = grid->rowCount();
    grid->addWidget(label, row, 0, 1, 2, Qt::AlignBottom);
    grid->setRowMinimumHeight(row, t.unit() * 6);
}

// A muted name for the left column.
QLabel* makeName(QGridLayout* grid, const QString& text, const QString& tip)
{
    const Theme& t = Theme::instance();
    auto* name = new QLabel(text, grid->parentWidget());
    name->setFont(t.uiFont(t.sizeSmall()));
    name->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    name->setToolTip(tip);
    return name;
}

// Adds a row with a value in JetBrains Mono.
QLabel* addValue(QGridLayout* grid, const QString& label, const QString& tip)
{
    const Theme& t = Theme::instance();
    auto* value = new QLabel(grid->parentWidget());
    value->setTextFormat(Qt::PlainText);
    value->setFont(t.monoFont(t.sizeNormal()));
    value->setToolTip(tip);
    const int row = grid->rowCount();
    grid->addWidget(makeName(grid, label, tip), row, 0, Qt::AlignTop);
    grid->addWidget(value, row, 1, Qt::AlignTop);
    return value;
}

// Sets a spin box without sending its signals.
void setSpin(QDoubleSpinBox* spin, double value)
{
    if (std::abs(spin->value() - value) < 1e-9)
        return;
    const QSignalBlocker blocker(spin);
    spin->setValue(value);
}

}

// Adds the Canopy title, the canopy's own rows, the Roof rows, the materials and the problem line.
CanopyGroup::CanopyGroup(plynth::Document& document, ToolController& controller, Selection& selection,
                         QGridLayout* grid, QObject* parent)
    : QObject(parent), document_(document), controller_(controller), selection_(selection)
{
    const Theme& t = Theme::instance();
    addTitle(grid, tr("Canopy"));
    id_ = addValue(grid, tr("Canopy"), tr("The id of this canopy, as the command line names it"));
    area_ = addValue(grid, tr("Area"), tr("The plan area inside the outline as drawn"));
    corners_ = addValue(grid, tr("Corners"), tr("How many corners the outline has. Drag a corner on the plan to move it."));
    height_ = addSpin(grid, tr("Height"), 1.0, 20.0, 0.05, tr("Height of the underside of the canopy above the floor"));
    thickness_ = addSpin(grid, tr("Thickness"), 0.05, 1.0, 0.01,
                         tr("How deep the slab of the canopy is, from its underside to the base of its roof"));

    addTitle(grid, tr("Roof"));
    type_ = new QComboBox(grid->parentWidget());
    type_->setFont(t.uiFont(t.sizeNormal()));
    type_->setStyle(t.controlStyle());
    const QString typeTip = tr("The shape of the roof on top of this canopy, from the roof types Plynth knows");
    type_->setToolTip(typeTip);
    for (const plynth::RoofType* type : plynth::allRoofTypes())
        type_->addItem(fromCore(type->name()), fromCore(type->id()));
    int row = grid->rowCount();
    grid->addWidget(makeName(grid, tr("Type"), typeTip), row, 0, Qt::AlignVCenter);
    grid->addWidget(type_, row, 1);
    editors_.push_back(type_);

    pitch_ = addSpin(grid, tr("Pitch"), plynth::kMinPitchedRoofPitch, plynth::kMaxRoofPitch, 1.0,
                     tr("How steep the roof slopes, in degrees from level"), &pitchLabel_);
    secondPitch_ = addSpin(grid, tr("Second pitch"), 0.0, plynth::kMaxRoofPitch, 1.0,
                           tr("The upper pitch of a gambrel or mansard roof, or the eaves pitch of a bonnet roof"),
                           &secondPitchLabel_);
    for (QDoubleSpinBox* spin : {pitch_, secondPitch_}) {
        spin->setDecimals(1);
        spin->setSuffix(QStringLiteral("°"));
    }
    breakRatio_ = addSpin(grid, tr("Break"), 0.1, 0.9, 0.05, tr("The share of the run the lower slope covers"),
                          &breakRatioLabel_);
    ridgeOffset_ = addSpin(grid, tr("Ridge offset"), -0.45, 0.45, 0.05,
                           tr("Moves the ridge or the valley across the canopy, as a share of its width. 0 is the middle."),
                           &ridgeOffsetLabel_);
    for (QDoubleSpinBox* spin : {breakRatio_, ridgeOffset_}) {
        spin->setDecimals(2);
        spin->setSuffix(QString());
    }
    rise_ = addSpin(grid, tr("Rise"), 0.1, 10.0, 0.05,
                    tr("The height of a barrel, dome or bow roof, or of the glazed band of a clerestory"), &riseLabel_);

    autoDirection_ = new QCheckBox(tr("Automatic direction"), grid->parentWidget());
    autoDirection_->setFont(t.uiFont(t.sizeNormal()));
    autoDirection_->setStyle(t.controlStyle());
    autoDirection_->setToolTip(tr("When on, the ridge runs along the longer side of the canopy. Turn it off to set the direction."));
    grid->addWidget(autoDirection_, grid->rowCount(), 0, 1, 2);
    editors_.push_back(autoDirection_);
    direction_ = addSpin(grid, tr("Direction"), 0.0, 359.9, 5.0,
                         tr("The way the ridge runs, or the way a shed roof falls, in degrees clockwise on the plan from the right"),
                         &directionLabel_);
    direction_->setDecimals(1);
    direction_->setSuffix(QStringLiteral("°"));
    overhang_ = addSpin(grid, tr("Overhang"), 0.0, plynth::kMaxRoofOverhang, 0.05,
                        tr("How far the roof reaches past the outline of the canopy"), &overhangLabel_);
    parapet_ = addSpin(grid, tr("Parapet"), 0.0, plynth::kMaxParapet, 0.05,
                       tr("Height of the low wall around a flat roof. Zero for a plain slab with a fascia band."),
                       &parapetLabel_);

    addTitle(grid, tr("Materials"));
    covering_ = addMaterial(grid, tr("Covering"),
                            tr("The material of the roof covering of this canopy. Without one, the project default shows"),
                            Slot::Covering);
    underside_ = addMaterial(grid, tr("Underside"), tr("The material of the soffit under this canopy"), Slot::Underside);
    fascia_ = addMaterial(grid, tr("Fascia"), tr("The material of the fascia round the edges of this canopy"), Slot::Fascia);

    problems_ = new QLabel(grid->parentWidget());
    problems_->setTextFormat(Qt::PlainText);
    problems_->setFont(t.uiFont(t.sizeSmall()));
    problems_->setWordWrap(true);
    problems_->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
    problems_->setToolTip(tr("What the build would report about this canopy now"));
    problems_->setVisible(false);
    grid->addWidget(problems_, grid->rowCount(), 0, 1, 2);

    problemTimer_ = new QTimer(this);
    problemTimer_->setSingleShot(true);
    problemTimer_->setInterval(kProblemDelay);
    connect(problemTimer_, &QTimer::timeout, this, &CanopyGroup::refreshProblems);

    connect(height_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        edit([value](plynth::Canopy& canopy) { canopy.height = value; });
    });
    connect(thickness_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        edit([value](plynth::Canopy& canopy) { canopy.thickness = value; });
    });
    connect(type_, &QComboBox::activated, this, [this](int) { editRoof(); });
    for (QDoubleSpinBox* spin : {pitch_, secondPitch_, breakRatio_, ridgeOffset_, rise_, direction_, overhang_, parapet_})
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this](double) { editRoof(); });
    connect(autoDirection_, &QCheckBox::clicked, this, [this](bool) { editRoof(); });
}

// Adds a row with a spin box in JetBrains Mono, in metres unless the caller changes the suffix.
QDoubleSpinBox* CanopyGroup::addSpin(QGridLayout* grid, const QString& label, double min, double max, double step,
                                     const QString& tip, QLabel** labelOut)
{
    const Theme& t = Theme::instance();
    auto* spin = new QDoubleSpinBox(grid->parentWidget());
    spin->setDecimals(3);
    spin->setRange(min, max);
    spin->setSingleStep(step);
    spin->setSuffix(tr(" m"));
    spin->setFont(t.monoFont(t.sizeNormal()));
    spin->setKeyboardTracking(false);
    spin->setStyle(t.controlStyle());
    spin->setToolTip(tip);
    QLabel* name = makeName(grid, label, tip);
    const int row = grid->rowCount();
    grid->addWidget(name, row, 0, Qt::AlignVCenter);
    grid->addWidget(spin, row, 1);
    editors_.push_back(spin);
    if (labelOut != nullptr)
        *labelOut = name;
    return spin;
}

// Adds a material row that opens the picker for one of the canopy's materials.
MaterialRow* CanopyGroup::addMaterial(QGridLayout* grid, const QString& label, const QString& tip, Slot slot)
{
    auto* row = new MaterialRow(grid->parentWidget());
    row->setToolTip(tip);
    const int index = grid->rowCount();
    grid->addWidget(makeName(grid, label, tip), index, 0, Qt::AlignVCenter);
    grid->addWidget(row, index, 1);
    editors_.push_back(row);
    connect(row, &QAbstractButton::clicked, this, [this, slot, row]() { openPicker(slot, row); });
    return row;
}

// The selected canopy on the shown floor, or null.
const plynth::Canopy* CanopyGroup::selected() const
{
    const plynth::Floor* floor = controller_.floor();
    if (floor == nullptr || selection_.kind() != SelectionKind::Canopy)
        return nullptr;
    return plynth::findCanopy(*floor, selection_.id().toStdString());
}

// Runs SetCanopyCommand with the selected canopy after a change. The boxes go back to the real values on a refusal.
void CanopyGroup::edit(const std::function<void(plynth::Canopy&)>& change)
{
    if (refreshing_)
        return;
    const plynth::Canopy* canopy = selected();
    if (canopy == nullptr)
        return;
    plynth::Canopy values = *canopy;
    change(values);
    if (values == *canopy)
        return;
    if (!controller_.run(std::make_unique<plynth::SetCanopyCommand>(canopy->id, values)).ok())
        refresh();
}

// Reads the roof boxes over the canopy's roof, so fields the shown type does not use keep their values.
void CanopyGroup::editRoof()
{
    if (refreshing_)
        return;
    const std::string type = type_->currentData().toString().toStdString();
    const double pitch = pitch_->value();
    const double secondPitch = secondPitch_->value();
    const double breakRatio = breakRatio_->value();
    const double ridgeOffset = ridgeOffset_->value();
    const double rise = rise_->value();
    const double overhang = overhang_->value();
    const double parapet = parapet_->value();
    const std::optional<double> direction =
        autoDirection_->isChecked() ? std::nullopt : std::optional<double>(direction_->value());
    edit([&](plynth::Canopy& canopy) {
        canopy.roof.type = type;
        canopy.roof.pitch = pitch;
        canopy.roof.secondPitch = secondPitch;
        canopy.roof.breakRatio = breakRatio;
        canopy.roof.ridgeOffset = ridgeOffset;
        canopy.roof.rise = rise;
        canopy.roof.overhang = overhang;
        canopy.roof.parapet = parapet;
        canopy.roof.direction = direction;
    });
}

// Shows what one material row names: the material from the library with its swatch, or the default.
void CanopyGroup::showMaterial(MaterialRow* row, const std::optional<std::string>& material)
{
    if (!material.has_value()) {
        row->setContent(QPixmap(), tr("Default"), tr("follows the project"), QString());
        return;
    }
    const Theme& t = Theme::instance();
    plynth::MaterialCatalog* catalog = controller_.library() ? controller_.library()->materialCatalog() : nullptr;
    const std::optional<plynth::MaterialRecord> record = catalog ? catalog->material(*material) : std::nullopt;
    if (!record) {
        row->setContent(QPixmap(), fromCore(*material), QString(), tr("not in the library"));
        return;
    }
    QPixmap swatch;
    if (record->state != plynth::MaterialState::Broken && record->thumbnail == plynth::ThumbnailState::Ok)
        swatch = AssetPreview::loadPixmap(QString::fromStdWString(record->thumbFile.wstring()), t.materialSwatch(),
                                          row->devicePixelRatioF());
    row->setContent(swatch, fromCore(record->name), tr("set on this canopy"),
                    record->state == plynth::MaterialState::Broken ? tr("broken") : QString());
}

// Opens the material picker beside a row, with the slot's material marked and the first row going back to the
// default.
void CanopyGroup::openPicker(Slot slot, MaterialRow* row)
{
    LibraryHost* host = controller_.library();
    const plynth::Canopy* canopy = selected();
    if (host == nullptr || canopy == nullptr)
        return;
    if (!host->materialCatalog()) {
        controller_.showMessage(tr("There is no library with materials. Open the setup check to choose one."));
        return;
    }
    if (picker_ == nullptr) {
        picker_ = new MaterialPicker(*host, row->window());
        connect(picker_, &MaterialPicker::materialChosen, this,
                [this](const QString& id) { setMaterial(pickSlot_, id.toStdString()); });
        connect(picker_, &MaterialPicker::cleared, this, [this]() { setMaterial(pickSlot_, std::nullopt); });
    }
    pickSlot_ = slot;
    const std::optional<std::string>& current = slot == Slot::Covering    ? canopy->material
                                                : slot == Slot::Underside ? canopy->undersideMaterial
                                                                          : canopy->fasciaMaterial;
    const QPoint anchor(row->parentWidget()->mapTo(row->window(), QPoint(0, 0)).x(),
                        row->mapTo(row->window(), QPoint(0, 0)).y());
    picker_->openAt(anchor, tr("Use the default"), current ? fromCore(*current) : QString());
}

// Checks a chosen material with the library, then sets it on the canopy.
void CanopyGroup::setMaterial(Slot slot, const std::optional<std::string>& material)
{
    if (material.has_value()) {
        LibraryHost* host = controller_.library();
        if (host == nullptr || host->materialCatalog() == nullptr) {
            controller_.showMessage(tr("There is no library with materials."));
            return;
        }
        const plynth::MaterialForUse use = plynth::materialForUse(*host->materialCatalog(), *material);
        if (!use.status.ok()) {
            controller_.showMessage(fromCore(use.status.message));
            return;
        }
    }
    edit([slot, &material](plynth::Canopy& canopy) {
        if (slot == Slot::Covering)
            canopy.material = material;
        else if (slot == Slot::Underside)
            canopy.undersideMaterial = material;
        else
            canopy.fasciaMaterial = material;
    });
}

// Fills the rows from the selected canopy and hides the roof fields its type does not use.
void CanopyGroup::refresh()
{
    const plynth::Canopy* canopy = selected();
    if (canopy == nullptr)
        return;
    refreshing_ = true;
    id_->setText(fromCore(canopy->id));
    std::vector<plynth::Vec2> ring;
    for (const plynth::PlanXY& corner : canopy->outline)
        ring.push_back(plynth::Vec2{corner.x, corner.y});
    area_->setText(QStringLiteral("%1 m²").arg(std::abs(plynth::signedArea(ring)), 0, 'f', 2));
    corners_->setText(QString::number(canopy->outline.size()));
    setSpin(height_, canopy->height);
    setSpin(thickness_, canopy->thickness);

    const plynth::RoofSettings& roof = canopy->roof;
    int index = type_->findData(fromCore(roof.type));
    if (index < 0) {
        type_->addItem(fromCore(roof.type), fromCore(roof.type));
        index = type_->count() - 1;
    }
    type_->setCurrentIndex(index);
    setSpin(pitch_, roof.pitch);
    setSpin(secondPitch_, roof.secondPitch);
    setSpin(breakRatio_, roof.breakRatio);
    setSpin(ridgeOffset_, roof.ridgeOffset);
    setSpin(rise_, roof.rise);
    setSpin(overhang_, roof.overhang);
    setSpin(parapet_, roof.parapet);
    setSpin(direction_, roof.direction.value_or(direction_->value()));
    {
        const QSignalBlocker blocker(autoDirection_);
        autoDirection_->setChecked(!roof.direction.has_value());
    }
    const plynth::RoofType* type = plynth::findRoofType(roof.type);
    const auto show = [](QWidget* label, QWidget* box, bool on) {
        label->setVisible(on);
        box->setVisible(on);
    };
    const bool parapet = type != nullptr && type->usesParapet();
    const bool direction = type != nullptr && type->usesDirection();
    show(pitchLabel_, pitch_, type != nullptr && type->usesPitch());
    show(secondPitchLabel_, secondPitch_, type != nullptr && type->usesSecondPitch());
    show(breakRatioLabel_, breakRatio_, type != nullptr && type->usesBreakRatio());
    show(ridgeOffsetLabel_, ridgeOffset_, type != nullptr && type->usesRidgeOffset());
    show(riseLabel_, rise_, type != nullptr && type->usesRise());
    autoDirection_->setVisible(direction);
    show(directionLabel_, direction_, direction && roof.direction.has_value());
    show(overhangLabel_, overhang_, type != nullptr && type->usesOverhang() && !(parapet && roof.parapet > 0.0));
    show(parapetLabel_, parapet_, parapet);

    showMaterial(covering_, canopy->material);
    showMaterial(underside_, canopy->undersideMaterial);
    showMaterial(fascia_, canopy->fasciaMaterial);

    // Another canopy's problems must not show while this one's are worked out.
    if (fromCore(canopy->id) != shownId_) {
        shownId_ = fromCore(canopy->id);
        problems_->clear();
        problems_->setVisible(false);
    }
    refreshing_ = false;
    problemTimer_->start();
}

// Lists the problems the build would report about the selected canopy, worked out by core without the library.
void CanopyGroup::refreshProblems()
{
    const plynth::Canopy* canopy = selected();
    if (canopy == nullptr)
        return;
    QStringList lines;
    const plynth::Status check = plynth::checkCanopy(*canopy);
    if (!check.ok())
        lines << fromCore(check.message);
    for (const plynth::BuildProblem& problem : plynth::makeBuildPlan(document_.project(), {}).problems)
        if (problem.object == canopy->id)
            lines << fromCore(problem.message);
    problems_->setText(lines.join(QLatin1Char('\n')));
    problems_->setVisible(!lines.isEmpty());
}

}
