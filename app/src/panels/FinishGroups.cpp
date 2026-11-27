#include "FinishGroups.h"

#include "catalog/LibraryHost.h"
#include "theme/Theme.h"
#include "tools/ToolController.h"

#include <plynth/core/BuildCheck.h>
#include <plynth/core/Document.h>
#include <plynth/core/TrimProfiles.h>
#include <plynth/core/commands/FinishCommands.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <memory>

namespace plynth::app {

namespace {

// Turns a core text into Qt text.
QString fromCore(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

constexpr int kPreviewLines = 12;

// The word a preview line uses for what a seed changed.
QString targetWord(plynth::VariationTarget target)
{
    switch (target) {
    case plynth::VariationTarget::Item:
        return QObject::tr("furniture");
    case plynth::VariationTarget::RoomFloor:
        return QObject::tr("floor");
    case plynth::VariationTarget::RoomWall:
        return QObject::tr("room walls");
    case plynth::VariationTarget::WallInside:
        return QObject::tr("inside wall");
    case plynth::VariationTarget::Outside:
        return QObject::tr("outside");
    }
    return QString();
}

// Adds a muted title that spans the page, above a group of rows.
QLabel* addTitle(QGridLayout* grid, const QString& title)
{
    const Theme& t = Theme::instance();
    auto* label = new QLabel(title, grid->parentWidget());
    label->setFont(t.uiFont(t.sizeSmall(), QFont::DemiBold));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    const int row = grid->rowCount();
    grid->addWidget(label, row, 0, 1, 2, Qt::AlignBottom);
    grid->setRowMinimumHeight(row, t.unit() * 6);
    return label;
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

// A metre spin box in JetBrains Mono that sends its value only when an edit is finished or a step is taken.
QDoubleSpinBox* makeSpin(QWidget* parent, double min, double max, double step, const QString& tip)
{
    const Theme& t = Theme::instance();
    auto* spin = new QDoubleSpinBox(parent);
    spin->setDecimals(3);
    spin->setRange(min, max);
    spin->setSingleStep(step);
    spin->setSuffix(QObject::tr(" m"));
    spin->setFont(t.monoFont(t.sizeNormal()));
    spin->setKeyboardTracking(false);
    spin->setStyle(t.controlStyle());
    spin->setToolTip(tip);
    return spin;
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

// Adds the Trim title, one block of rows for each kind and the railing height.
TrimGroup::TrimGroup(plynth::Document& document, ToolController& controller, QGridLayout* grid, QObject* parent)
    : QObject(parent), document_(document), controller_(controller)
{
    title_ = addTitle(grid, tr("Trim"));
    addKind(grid, plynth::TrimKind::Skirting, tr("Skirting"), tr("Skirting boards along the bottom of the walls of every room"));
    addKind(grid, plynth::TrimKind::Cornice, tr("Cornice"), tr("Cornices where the walls of a room meet the ceiling"));
    addKind(grid, plynth::TrimKind::Casing, tr("Casing"), tr("Casings round both faces of every door"));
    addKind(grid, plynth::TrimKind::Sill, tr("Sill"), tr("Sills under both faces of every window with a straight bottom"));
    addKind(grid, plynth::TrimKind::Railing, tr("Railing"), tr("Railings on the open sides of stairs and guards round stair holes"));

    const QString tip = tr("Height of the top of a stair railing above the steps");
    QLabel* name = makeName(grid, tr("Railing height"), tip);
    railingHeight_ = makeSpin(grid->parentWidget(), plynth::kMinRailingHeight, plynth::kMaxRailingHeight, 0.05, tip);
    const int row = grid->rowCount();
    grid->addWidget(name, row, 0, Qt::AlignVCenter);
    grid->addWidget(railingHeight_, row, 1);
    editors_.push_back(railingHeight_);
    connect(railingHeight_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (refreshing_)
            return;
        plynth::TrimSettings trim = document_.project().settings.trim;
        trim.railingHeight = value;
        if (!controller_.run(std::make_unique<plynth::SetTrimCommand>(trim)).ok())
            refresh();
    });
    refresh();
}

// Adds the rows of one kind: its switch with the profile beside it, then its height and depth.
void TrimGroup::addKind(QGridLayout* grid, plynth::TrimKind kind, const QString& name, const QString& tip)
{
    const Theme& t = Theme::instance();
    const QString id = fromCore(plynth::trimKindName(kind));
    KindRow row;
    row.kind = kind;
    row.on = new QCheckBox(name, grid->parentWidget());
    row.on->setFont(t.uiFont(t.sizeSmall()));
    row.on->setToolTip(tip + tr(". Untick to leave it out of the build."));
    row.profile = new QComboBox(grid->parentWidget());
    row.profile->setFont(t.uiFont(t.sizeNormal()));
    row.profile->setStyle(t.controlStyle());
    row.profile->setToolTip(tr("The cross section of the %1. Profiles in the library's shapes folder are listed too.")
                                .arg(name.toLower()));
    int at = grid->rowCount();
    grid->addWidget(row.on, at, 0, Qt::AlignVCenter);
    grid->addWidget(row.profile, at, 1);

    const bool casing = kind == plynth::TrimKind::Casing;
    row.height = makeSpin(grid->parentWidget(), plynth::kMinTrimSize, plynth::kMaxTrimSize, 0.005,
                          casing ? tr("Width of the casing across the wall face") : tr("Size of the profile up the wall"));
    row.depth = makeSpin(grid->parentWidget(), plynth::kMinTrimSize, plynth::kMaxTrimSize, 0.005,
                         tr("How far it stands out from the wall"));
    auto* sizes = new QWidget(grid->parentWidget());
    auto* line = new QHBoxLayout(sizes);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(t.unit() * 2);
    line->addWidget(row.height, 1);
    line->addWidget(row.depth, 1);
    at = grid->rowCount();
    grid->addWidget(makeName(grid, casing ? tr("Width, depth") : tr("Height, depth"),
                             tr("The size of the profile: across, then out from the wall")),
                    at, 0, Qt::AlignVCenter);
    grid->addWidget(sizes, at, 1);

    for (QWidget* widget : {static_cast<QWidget*>(row.on), static_cast<QWidget*>(row.profile),
                            static_cast<QWidget*>(row.height), static_cast<QWidget*>(row.depth)})
        editors_.push_back(widget);
    rows_.push_back(row);
    const size_t index = rows_.size() - 1;
    connect(row.on, &QCheckBox::clicked, this, [this, index]() { edit(rows_[index]); });
    connect(row.profile, &QComboBox::activated, this, [this, index]() { edit(rows_[index]); });
    connect(row.height, &QDoubleSpinBox::valueChanged, this, [this, index]() { edit(rows_[index]); });
    connect(row.depth, &QDoubleSpinBox::valueChanged, this, [this, index]() { edit(rows_[index]); });
}

// Runs SetTrimCommand with one kind as its rows show it. The rows go back to the project on a refusal.
void TrimGroup::edit(const KindRow& row)
{
    if (refreshing_)
        return;
    plynth::TrimSettings trim = document_.project().settings.trim;
    plynth::TrimStyle& style = plynth::trimStyle(trim, row.kind);
    style.enabled = row.on->isChecked();
    style.profile = row.profile->currentData().toString().toStdString();
    style.height = row.height->value();
    style.depth = row.depth->value();
    if (trim == document_.project().settings.trim)
        return;
    if (!controller_.run(std::make_unique<plynth::SetTrimCommand>(trim)).ok())
        refresh();
}

// Fills every row from the project. A profile the registry does not know stays listed so the row shows it.
void TrimGroup::refresh()
{
    refreshing_ = true;
    const plynth::TrimSettings& trim = document_.project().settings.trim;
    const bool editable = !document_.isReadOnly();
    const std::vector<const plynth::TrimProfile*> profiles = plynth::allTrimProfiles();
    for (const KindRow& row : rows_) {
        const plynth::TrimStyle& style = plynth::trimStyle(trim, row.kind);
        {
            const QSignalBlocker blocker(row.profile);
            row.profile->clear();
            for (const plynth::TrimProfile* profile : profiles)
                row.profile->addItem(fromCore(profile->name()), fromCore(profile->id()));
            const QString current = QString::fromStdString(style.profile);
            int index = row.profile->findData(current);
            if (index < 0) {
                row.profile->addItem(tr("%1 (not found)").arg(current), current);
                index = row.profile->count() - 1;
            }
            row.profile->setCurrentIndex(index);
        }
        {
            const QSignalBlocker blocker(row.on);
            row.on->setChecked(style.enabled);
        }
        setSpin(row.height, style.height);
        setSpin(row.depth, style.depth);
        row.on->setEnabled(editable);
        row.profile->setEnabled(editable && style.enabled);
        row.height->setEnabled(editable && style.enabled);
        row.depth->setEnabled(editable && style.enabled);
    }
    setSpin(railingHeight_, trim.railingHeight);
    railingHeight_->setEnabled(editable && trim.railing.enabled);
    refreshing_ = false;
}

// Adds the Variation title, the seed with Randomize beside it, the two switches and a note.
VariationGroup::VariationGroup(plynth::Document& document, ToolController& controller, QGridLayout* grid,
                               QObject* parent)
    : QObject(parent), document_(document), controller_(controller)
{
    const Theme& t = Theme::instance();
    title_ = addTitle(grid, tr("Variation"));

    const QString seedTip = tr("The seed of this build's variation. The same seed always gives the same building. "
                               "0 builds the plan exactly as marked.");
    seed_ = new QSpinBox(grid->parentWidget());
    seed_->setRange(0, INT_MAX);
    seed_->setFont(t.monoFont(t.sizeNormal()));
    seed_->setKeyboardTracking(false);
    seed_->setStyle(t.controlStyle());
    seed_->setToolTip(seedTip);
    randomize_ = new QPushButton(tr("Randomize"), grid->parentWidget());
    randomize_->setToolTip(tr("Pick a new random seed"));
    auto* seedRow = new QWidget(grid->parentWidget());
    auto* line = new QHBoxLayout(seedRow);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(t.unit() * 2);
    line->addWidget(seed_, 1);
    line->addWidget(randomize_);
    const int at = grid->rowCount();
    grid->addWidget(makeName(grid, tr("Seed"), seedTip), at, 0, Qt::AlignVCenter);
    grid->addWidget(seedRow, at, 1);

    furniture_ = new QCheckBox(tr("Vary furniture"), grid->parentWidget());
    furniture_->setFont(t.uiFont(t.sizeNormal()));
    furniture_->setToolTip(tr("Let the seed swap furniture for other measured assets of the same category and a similar size"));
    grid->addWidget(furniture_, grid->rowCount(), 1);
    materials_ = new QCheckBox(tr("Vary materials"), grid->parentWidget());
    materials_->setFont(t.uiFont(t.sizeNormal()));
    materials_->setToolTip(tr("Let the seed pick other floor, inside wall and outside materials of the same group"));
    grid->addWidget(materials_, grid->rowCount(), 1);

    note_ = new QLabel(grid->parentWidget());
    note_->setFont(t.uiFont(t.sizeSmall()));
    note_->setWordWrap(true);
    note_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    grid->addWidget(note_, grid->rowCount(), 0, 1, 2);

    const QString previewTip = tr("List what this seed picks without building: the furniture and materials it swaps, "
                                  "the catalog doors and windows and the props of each room");
    previewButton_ = new QPushButton(tr("Preview"), grid->parentWidget());
    previewButton_->setToolTip(previewTip);
    const int previewRow = grid->rowCount();
    grid->addWidget(makeName(grid, tr("This seed"), previewTip), previewRow, 0, Qt::AlignVCenter);
    grid->addWidget(previewButton_, previewRow, 1, Qt::AlignLeft);
    previewText_ = new QLabel(grid->parentWidget());
    previewText_->setTextFormat(Qt::PlainText);
    previewText_->setFont(t.monoFont(t.sizeSmall()));
    previewText_->setWordWrap(true);
    previewText_->setToolTip(tr("What the seed picks, as \"plynth variation preview\" lists it. Nothing is built."));
    previewText_->hide();
    grid->addWidget(previewText_, grid->rowCount(), 0, 1, 2);
    connect(previewButton_, &QPushButton::clicked, this, &VariationGroup::preview);

    editors_ = {seed_, randomize_, furniture_, materials_};
    connect(seed_, &QSpinBox::valueChanged, this, [this](int value) {
        plynth::VariationSettings settings = document_.project().settings.variation;
        settings.seed = static_cast<std::uint32_t>(value);
        edit(settings);
    });
    connect(randomize_, &QPushButton::clicked, this, [this]() {
        plynth::VariationSettings settings = document_.project().settings.variation;
        settings.seed = static_cast<std::uint32_t>(QRandomGenerator::global()->bounded(1, INT_MAX));
        edit(settings);
    });
    connect(furniture_, &QCheckBox::clicked, this, [this](bool on) {
        plynth::VariationSettings settings = document_.project().settings.variation;
        settings.furniture = on;
        edit(settings);
    });
    connect(materials_, &QCheckBox::clicked, this, [this](bool on) {
        plynth::VariationSettings settings = document_.project().settings.variation;
        settings.materials = on;
        edit(settings);
    });
    refresh();
}

// Runs SetVariationCommand when the settings changed. The controls go back to the project on a refusal.
void VariationGroup::edit(plynth::VariationSettings settings)
{
    if (refreshing_ || settings == document_.project().settings.variation)
        return;
    if (!controller_.run(std::make_unique<plynth::SetVariationCommand>(settings)).ok())
        refresh();
}

// Fills the controls from the project.
void VariationGroup::refresh()
{
    refreshing_ = true;
    const plynth::VariationSettings& variation = document_.project().settings.variation;
    {
        const QSignalBlocker blocker(seed_);
        seed_->setValue(static_cast<int>(std::min<std::uint32_t>(variation.seed, INT_MAX)));
    }
    {
        const QSignalBlocker blocker(furniture_);
        furniture_->setChecked(variation.furniture);
    }
    {
        const QSignalBlocker blocker(materials_);
        materials_->setChecked(variation.materials);
    }
    note_->setText(variation.seed == 0 ? tr("Seed 0 builds the plan exactly as marked. Set dressing still adds props.")
                                       : tr("The build varies the plan with this seed. Set it to 0 for the plan as marked."));
    if (previewed_ && *previewed_ != document_.project()) {
        const Theme& t = Theme::instance();
        previewed_.reset();
        previewText_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
        previewText_->setText(tr("The project changed since this preview. Press Preview again."));
    }
    refreshing_ = false;
}

// Runs core's previewVariation for the project's seed with the open library and lists what it picks, a few
// lines of each kind at most.
void VariationGroup::preview()
{
    const Theme& t = Theme::instance();
    LibraryHost* host = controller_.library();
    std::optional<plynth::LibraryPaths> library;
    if (host != nullptr && host->hasLibrary())
        library = host->paths();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const plynth::VariationPreview result = plynth::previewVariation(document_.project(), std::nullopt, library);
    QApplication::restoreOverrideCursor();
    const plynth::ResolvedBuild& resolved = result.resolved;

    QStringList lines;
    for (const plynth::VariationChange& change : resolved.changes) {
        if (change.target == plynth::VariationTarget::Outside)
            lines << tr("Outside: %1 to %2").arg(fromCore(change.from), fromCore(change.to));
        else
            lines << tr("%1 %2: %3 to %4").arg(fromCore(change.object), targetWord(change.target), fromCore(change.from),
                                               fromCore(change.to));
    }
    for (const plynth::PickedFrame& frame : resolved.frames)
        lines << tr("%1 frame: %2").arg(fromCore(frame.opening), fromCore(frame.asset));
    int props = 0;
    for (const plynth::Floor& floor : resolved.project.plan.floors) {
        const plynth::FloorDressing* dressing = nullptr;
        for (const plynth::FloorDressing& entry : resolved.dressing)
            if (entry.floor == floor.id)
                dressing = &entry;
        if (dressing == nullptr)
            continue;
        for (const plynth::Room& room : floor.rooms) {
            int count = 0;
            for (const plynth::DressingPlacement& prop : dressing->props)
                count += prop.room == room.id ? 1 : 0;
            props += count;
            if (count > 0)
                lines << tr("%1: %n props", nullptr, count).arg(fromCore(room.id));
        }
    }

    QStringList shown;
    const std::uint32_t seed = resolved.project.settings.variation.seed;
    if (lines.isEmpty())
        shown << tr("Seed %1 changes nothing and adds no props.").arg(seed);
    else
        shown << tr("Seed %1: %2 changes, %3 catalog doors and windows, %4 props.")
                     .arg(seed)
                     .arg(resolved.changes.size())
                     .arg(resolved.frames.size())
                     .arg(props);
    for (qsizetype i = 0; i < lines.size() && i < kPreviewLines; ++i)
        shown << lines[i];
    if (lines.size() > kPreviewLines)
        shown << tr("and %1 more").arg(lines.size() - kPreviewLines);
    if (!result.library)
        shown << tr("No library is open, so nothing is picked from the catalog.");
    previewText_->setStyleSheet(QStringLiteral("color: %1;").arg(t.text().name()));
    previewText_->setText(shown.join(QLatin1Char('\n')));
    previewText_->show();
    previewed_ = document_.project();
}

}
