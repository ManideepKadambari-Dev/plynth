#include "MaterialsPanel.h"

#include "catalog/LibraryHost.h"
#include "materials/MaterialPreview.h"
#include "theme/PlainText.h"
#include "theme/Theme.h"

#include <plynth/core/catalog/AssetInfo.h>

#include <QComboBox>
#include <QEvent>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace plynth::app {

namespace {

constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kPixmapRole = Qt::UserRole + 2;
constexpr int kStateRole = Qt::UserRole + 3;
constexpr int kStateColorRole = Qt::UserRole + 4;
constexpr int kTileRole = Qt::UserRole + 5;
constexpr int kSearchDelayMs = 200;

const QString kAllGroups = QStringLiteral("*");
const QString kUncategorized = QStringLiteral("-");

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Turns a Qt text into a core text.
std::string toCore(const QString& text)
{
    return text.toStdString();
}

// Draws one grid tile: the thumbnail or the reason there is none, then the name and the tile size.
class TileDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    // The cell size: the picture square plus two text lines.
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        const Theme& t = Theme::instance();
        return QSize(t.materialGridThumb() + t.unit(), t.materialGridThumb() + t.unit() * 12);
    }

    // Paints the tile with the Signal blue outline when selected.
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const Theme& t = Theme::instance();
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        const int tile = t.materialGridThumb();
        const QRect cell = option.rect;
        const QRect image(cell.left() + (cell.width() - tile) / 2, cell.top() + t.unit(), tile, tile);
        const QString state = index.data(kStateRole).toString();
        if (state.isEmpty()) {
            const QPixmap pixmap = index.data(kPixmapRole).value<QPixmap>();
            const QSizeF logical = QSizeF(pixmap.size()) / pixmap.devicePixelRatio();
            p->fillRect(image, t.ground());
            p->drawPixmap(QPointF(image.left() + (tile - logical.width()) / 2.0,
                                  image.top() + (tile - logical.height()) / 2.0),
                          pixmap);
        } else {
            p->setPen(Qt::NoPen);
            p->setBrush(t.raised());
            p->drawRoundedRect(image, t.radius(), t.radius());
            p->setPen(index.data(kStateColorRole).value<QColor>());
            p->setFont(t.uiFont(t.sizeSmall()));
            p->drawText(image, Qt::AlignCenter | Qt::TextWordWrap, state);
        }
        const bool selected = option.state & QStyle::State_Selected;
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(selected ? t.signalAmber() : t.line(), selected ? 2 : 1));
        p->drawRoundedRect(QRectF(image).adjusted(0.5, 0.5, -0.5, -0.5), t.radius(), t.radius());

        const QRect nameRect(cell.left() + t.unit() / 2, image.bottom() + t.unit() * 2, cell.width() - t.unit(),
                             t.unit() * 5);
        p->setPen(t.text());
        p->setFont(t.uiFont(t.sizeNormal()));
        p->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                                nameRect.width()));
        p->setPen(t.muted());
        p->setFont(t.monoFont(t.sizeSmall()));
        p->drawText(nameRect.translated(0, t.unit() * 5), Qt::AlignLeft | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(index.data(kTileRole).toString(), Qt::ElideRight, nameRect.width()));
        p->restore();
    }
};

// Makes a muted small label.
QLabel* mutedLabel(const QString& text, QWidget* parent)
{
    const Theme& t = Theme::instance();
    auto* label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setText(text);
    label->setFont(t.uiFont(t.sizeSmall()));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    return label;
}

// The words for a map channel.
QString channelText(plynth::MapChannel channel)
{
    switch (channel) {
    case plynth::MapChannel::BaseColor: return QObject::tr("Base color");
    case plynth::MapChannel::Normal: return QObject::tr("Normal");
    case plynth::MapChannel::Roughness: return QObject::tr("Roughness");
    case plynth::MapChannel::Metallic: return QObject::tr("Metallic");
    case plynth::MapChannel::Opacity: return QObject::tr("Opacity");
    case plynth::MapChannel::Occlusion: return QObject::tr("Occlusion");
    }
    return QString();
}

// How a map was made, in plain words.
QString originText(const plynth::MaterialMap& map)
{
    switch (map.origin) {
    case plynth::MapOrigin::File: return QObject::tr("from %1").arg(fromCore(map.detail));
    case plynth::MapOrigin::Value: return QObject::tr("value %1").arg(fromCore(map.detail));
    case plynth::MapOrigin::Baked: return QObject::tr("baked");
    case plynth::MapOrigin::None: return QObject::tr("none");
    }
    return QString();
}

// A tile size spin box in metres.
QDoubleSpinBox* makeTileSpin(const QString& tip, const QString& tourId, QWidget* parent)
{
    const Theme& t = Theme::instance();
    auto* spin = new QDoubleSpinBox(parent);
    spin->setDecimals(3);
    spin->setRange(plynth::kMinTileSize, plynth::kMaxTileSize);
    spin->setSingleStep(0.05);
    spin->setSuffix(QObject::tr(" m"));
    spin->setFont(t.monoFont(t.sizeNormal()));
    spin->setKeyboardTracking(false);
    spin->setStyle(t.controlStyle());
    spin->setToolTip(tip);
    spin->setProperty("tourId", tourId);
    return spin;
}

}

// Builds the controls, the grid and the details.
MaterialsPanel::MaterialsPanel(LibraryHost& host, QWidget* parent) : QWidget(parent), host_(host)
{
    const Theme& t = Theme::instance();
    setProperty("tourId", QStringLiteral("materials-panel"));
    setToolTip(tr("The materials catalog: search, pick and edit the surface materials in the library"));

    auto* column = new QVBoxLayout(this);
    column_ = column;
    column->setContentsMargins(t.unit() * 4, t.unit() * 3, t.unit() * 4, t.unit() * 3);
    column->setSpacing(t.unit() * 2);
    buildControls(column);

    pages_ = new QStackedWidget(this);
    auto* empty = new QWidget(pages_);
    auto* emptyColumn = new QVBoxLayout(empty);
    emptyColumn->setContentsMargins(0, t.unit() * 6, 0, 0);
    emptyColumn->setSpacing(t.unit() * 3);
    emptyText_ = new QLabel(empty);
    emptyText_->setTextFormat(Qt::PlainText);
    emptyText_->setWordWrap(true);
    emptyText_->setFont(t.uiFont(t.sizeNormal()));
    emptyColumn->addWidget(emptyText_);
    emptyHelp_ = new QLabel(empty);
    emptyHelp_->setWordWrap(true);
    emptyHelp_->setFont(t.uiFont(t.sizeSmall()));
    emptyHelp_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    emptyColumn->addWidget(emptyHelp_);
    openSetup_ = new QPushButton(tr("Open setup check"), empty);
    openSetup_->setToolTip(tr("Open the setup check to choose or create the library folder"));
    openSetup_->setProperty("tourId", QStringLiteral("materials-open-setup"));
    connect(openSetup_, &QPushButton::clicked, this, &MaterialsPanel::setupRequested);
    emptyColumn->addWidget(openSetup_, 0, Qt::AlignLeft);
    samples_ = new QPushButton(tr("Add the sample materials"), empty);
    samples_->setToolTip(tr("Import the nine sample materials that come with Plynth. New projects use them by default"));
    samples_->setProperty("tourId", QStringLiteral("materials-add-samples"));
    connect(samples_, &QPushButton::clicked, this, &MaterialsPanel::samplesRequested);
    emptyColumn->addWidget(samples_, 0, Qt::AlignLeft);
    emptyColumn->addStretch(1);
    pages_->addWidget(empty);

    splitter_ = new QSplitter(Qt::Vertical, pages_);
    splitter_->setChildrenCollapsible(false);
    splitter_->setHandleWidth(t.unit());
    splitter_->setStyleSheet(QStringLiteral("QSplitter::handle { background: %1; }").arg(t.line().name()));
    auto* top = new QWidget(splitter_);
    auto* topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(t.unit() * 2);
    countLabel_ = mutedLabel(QString(), top);
    countLabel_->setToolTip(tr("How many materials match the search and the group"));
    topLayout->addWidget(countLabel_);

    model_ = new QStandardItemModel(this);
    grid_ = new QListView(top);
    grid_->setModel(model_);
    grid_->setViewMode(QListView::IconMode);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setUniformItemSizes(true);
    grid_->setWrapping(true);
    grid_->setSpacing(t.unit() / 2);
    grid_->setSelectionMode(QAbstractItemView::SingleSelection);
    grid_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    grid_->setFrameShape(QFrame::NoFrame);
    grid_->setStyleSheet(QStringLiteral("QListView { background: transparent; }"));
    grid_->setItemDelegate(new TileDelegate(grid_));
    grid_->setToolTip(tr("The materials in the library. Click one to see and edit its details"));
    grid_->setProperty("tourId", QStringLiteral("materials-grid"));
    grid_->setMinimumHeight(t.materialGridThumb() + t.unit() * 14);
    topLayout->addWidget(grid_, 1);
    splitter_->addWidget(top);

    buildDetails();
    splitter_->addWidget(detailsScroll_);
    splitter_->setStretchFactor(0, 3);
    splitter_->setStretchFactor(1, 2);
    detailsScroll_->hide();
    grid_->installEventFilter(this);
    connect(splitter_, &QSplitter::splitterMoved, this, [this]() { QTimer::singleShot(0, this, [this]() { snapSplit(); }); });
    pages_->addWidget(splitter_);
    column->addWidget(pages_, 1);

    connect(grid_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this]() {
        if (refreshing_)
            return;
        const QModelIndex index = grid_->currentIndex();
        current_.reset();
        if (index.isValid() && host_.materialCatalog())
            current_ = host_.materialCatalog()->material(toCore(index.data(kIdRole).toString()));
        showDetails();
    });
    connect(&host_, &LibraryHost::materialsChanged, this, &MaterialsPanel::refresh);
    connect(&host_, &LibraryHost::libraryChanged, this, &MaterialsPanel::refresh);
    refresh();
}

// Builds the search box, the group filter and the Rescan and Import buttons.
void MaterialsPanel::buildControls(QVBoxLayout* column)
{
    const Theme& t = Theme::instance();
    controls_ = new QWidget(this);
    auto* box = new QVBoxLayout(controls_);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(t.unit() * 2);

    search_ = new QLineEdit(controls_);
    search_->setPlaceholderText(tr("Search materials"));
    search_->setClearButtonEnabled(true);
    search_->setFont(t.uiFont(t.sizeNormal()));
    search_->setStyle(t.controlStyle());
    search_->setToolTip(tr("Type part of a name, tag or group. The grid filters as you type"));
    search_->setProperty("tourId", QStringLiteral("materials-search"));
    box->addWidget(search_);
    searchTimer_ = new QTimer(this);
    searchTimer_->setSingleShot(true);
    searchTimer_->setInterval(kSearchDelayMs);
    connect(search_, &QLineEdit::textChanged, searchTimer_, qOverload<>(&QTimer::start));
    connect(searchTimer_, &QTimer::timeout, this, &MaterialsPanel::fillGrid);

    group_ = new QComboBox(controls_);
    group_->setFont(t.uiFont(t.sizeNormal()));
    group_->setStyle(t.controlStyle());
    group_->setToolTip(tr("Show only one group, or the materials that have no group yet"));
    group_->setProperty("tourId", QStringLiteral("materials-group"));
    group_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    box->addWidget(group_);
    connect(group_, &QComboBox::activated, this, &MaterialsPanel::fillGrid);
    connect(group_, &QComboBox::currentIndexChanged, this, &MaterialsPanel::updateGroupButtons);

    auto* groupButtons = new QHBoxLayout();
    groupButtons->setSpacing(t.unit() * 2);
    addGroup_ = new QPushButton(tr("Add group"), controls_);
    addGroup_->setToolTip(tr("Make a new material group"));
    addGroup_->setProperty("tourId", QStringLiteral("materials-group-add"));
    connect(addGroup_, &QPushButton::clicked, this, &MaterialsPanel::addGroup);
    renameGroup_ = new QPushButton(tr("Rename"), controls_);
    renameGroup_->setToolTip(tr("Change the name of the group shown in the filter above"));
    renameGroup_->setProperty("tourId", QStringLiteral("materials-group-rename"));
    connect(renameGroup_, &QPushButton::clicked, this, &MaterialsPanel::renameGroup);
    removeGroup_ = new QPushButton(tr("Remove"), controls_);
    removeGroup_->setToolTip(tr("Delete the group shown in the filter above. The materials stay but lose the group"));
    removeGroup_->setProperty("tourId", QStringLiteral("materials-group-remove"));
    connect(removeGroup_, &QPushButton::clicked, this, &MaterialsPanel::removeGroup);
    groupButtons->addWidget(addGroup_);
    groupButtons->addWidget(renameGroup_);
    groupButtons->addWidget(removeGroup_);
    groupButtons->addStretch(1);
    box->addLayout(groupButtons);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(t.unit() * 2);
    rescan_ = new QPushButton(tr("Rescan"), controls_);
    rescan_->setToolTip(tr("Read the materials folder again and update the index"));
    rescan_->setProperty("tourId", QStringLiteral("materials-rescan"));
    connect(rescan_, &QPushButton::clicked, this, &MaterialsPanel::rescanRequested);
    import_ = new QPushButton(tr("Import"), controls_);
    import_->setDefault(true);
    import_->setToolTip(tr("Open the import screen for materials"));
    import_->setProperty("tourId", QStringLiteral("materials-import"));
    import_->setEnabled(false);
    connect(import_, &QPushButton::clicked, this, &MaterialsPanel::importRequested);
    buttons->addWidget(rescan_);
    buttons->addStretch(1);
    buttons->addWidget(import_);
    box->addLayout(buttons);
    column->addWidget(controls_);
}

// Builds the details area for the selected material.
void MaterialsPanel::buildDetails()
{
    const Theme& t = Theme::instance();
    detailsScroll_ = new QScrollArea(splitter_);
    detailsScroll_->setWidgetResizable(true);
    detailsScroll_->setFrameShape(QFrame::NoFrame);
    detailsScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailsScroll_->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
    detailsScroll_->verticalScrollBar()->setStyleSheet(t.scrollBarStyleSheet());
    auto* body = new QWidget();
    auto* box = new QVBoxLayout(body);
    box->setContentsMargins(0, t.unit() * 2, t.unit() * 4, 0);
    box->setSpacing(t.unit() * 2);

    thumb_ = new QLabel(body);
    thumb_->setFixedSize(t.thumbnailLarge(), t.thumbnailLarge());
    thumb_->setAlignment(Qt::AlignCenter);
    thumb_->setFont(t.uiFont(t.sizeNormal()));
    thumb_->setToolTip(tr("The thumbnail from Blender: a sphere and a flat tile at real size. A missing one reads Rendering, a failed one Render failed"));
    thumb_->setProperty("tourId", QStringLiteral("materials-thumbnail"));
    box->addWidget(thumb_, 0, Qt::AlignHCenter);

    name_ = new QLineEdit(body);
    name_->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    name_->setStyle(t.controlStyle());
    name_->setToolTip(tr("The name of the material. Press Enter to save a new name"));
    name_->setProperty("tourId", QStringLiteral("materials-name"));
    box->addWidget(name_);
    connect(name_, &QLineEdit::editingFinished, this, [this]() {
        if (refreshing_ || !current_ || name_->text().trimmed() == fromCore(current_->name))
            return;
        plynth::MaterialEdit edit;
        edit.name = toCore(name_->text().trimmed());
        applyEdit(edit);
    });

    idLabel_ = new QLabel(body);
    idLabel_->setTextFormat(Qt::PlainText);
    idLabel_->setFont(t.monoFont(t.sizeSmall()));
    idLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    idLabel_->setToolTip(tr("The id of the material, used by the command line and the plan"));
    idLabel_->setProperty("tourId", QStringLiteral("materials-id"));
    idLabel_->setTextInteractionFlags(Qt::NoTextInteraction);
    box->addWidget(idLabel_);

    box->addWidget(mutedLabel(tr("Tile size"), body));
    auto* tileRow = new QHBoxLayout();
    tileRow->setSpacing(t.unit() * 2);
    tileWidth_ = makeTileSpin(tr("Real width of one tile of the texture, along the wall or floor. Press Enter to save; the thumbnail is rendered again"),
                              QStringLiteral("materials-tile-width"), body);
    tileHeight_ = makeTileSpin(tr("Real height of one tile of the texture. Press Enter to save; the thumbnail is rendered again"),
                               QStringLiteral("materials-tile-height"), body);
    tileRow->addWidget(tileWidth_, 1);
    auto* times = mutedLabel(QStringLiteral("x"), body);
    tileRow->addWidget(times);
    tileRow->addWidget(tileHeight_, 1);
    box->addLayout(tileRow);
    const auto tileEdited = [this]() {
        if (refreshing_ || !current_)
            return;
        const plynth::TileSize size{tileWidth_->value(), tileHeight_->value()};
        if (size == current_->tileSize)
            return;
        plynth::MaterialEdit edit;
        edit.tileSize = size;
        const QString id = fromCore(current_->id);
        applyEdit(edit);
        emit retryRequested(id);
    };
    connect(tileWidth_, &QDoubleSpinBox::editingFinished, this, tileEdited);
    connect(tileHeight_, &QDoubleSpinBox::editingFinished, this, tileEdited);

    textureLabel_ = new QLabel(body);
    textureLabel_->setFont(t.monoFont(t.sizeSmall()));
    textureLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    textureLabel_->setToolTip(tr("Size in pixels of the base color image"));
    textureLabel_->setProperty("tourId", QStringLiteral("materials-texture-size"));
    box->addWidget(textureLabel_);

    box->addWidget(mutedLabel(tr("Maps"), body));
    mapsBox_ = new QWidget(body);
    mapsBox_->setToolTip(tr("The images the material is made of and how each was made: from a file, a single value, baked from Blender nodes, or none"));
    mapsBox_->setProperty("tourId", QStringLiteral("materials-maps"));
    mapsGrid_ = new QGridLayout(mapsBox_);
    mapsGrid_->setContentsMargins(0, 0, 0, 0);
    mapsGrid_->setHorizontalSpacing(t.unit() * 3);
    mapsGrid_->setVerticalSpacing(t.unit());
    mapsGrid_->setColumnStretch(1, 1);
    box->addWidget(mapsBox_);

    sourceLabel_ = new QLabel(body);
    sourceLabel_->setTextFormat(Qt::PlainText);
    sourceLabel_->setFont(t.monoFont(t.sizeSmall()));
    sourceLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    sourceLabel_->setWordWrap(true);
    sourceLabel_->setToolTip(tr("Where the material came from"));
    sourceLabel_->setProperty("tourId", QStringLiteral("materials-source"));
    box->addWidget(sourceLabel_);

    box->addWidget(mutedLabel(tr("License"), body));
    license_ = new QComboBox(body);
    license_->setFont(t.uiFont(t.sizeNormal()));
    license_->setStyle(t.controlStyle());
    license_->setToolTip(tr("The license the material came with"));
    license_->setProperty("tourId", QStringLiteral("materials-license"));
    box->addWidget(license_);
    connect(license_, &QComboBox::activated, this, [this](int index) {
        if (refreshing_ || !current_)
            return;
        plynth::MaterialEdit edit;
        edit.license = toCore(license_->itemData(index).toString());
        applyEdit(edit);
    });

    box->addWidget(mutedLabel(tr("Groups"), body));
    groups_ = new QListWidget(body);
    groups_->setFont(t.uiFont(t.sizeNormal()));
    groups_->setStyle(t.controlStyle());
    groups_->setFixedHeight(t.unit() * 40);
    groups_->setStyleSheet(t.scrollBarStyleSheet());
    groups_->setToolTip(tr("Tick every group the material belongs to"));
    groups_->setProperty("tourId", QStringLiteral("materials-groups"));
    box->addWidget(groups_);
    connect(groups_, &QListWidget::itemChanged, this, [this]() {
        if (refreshing_ || !current_)
            return;
        std::vector<std::string> ids;
        for (int i = 0; i < groups_->count(); ++i)
            if (groups_->item(i)->checkState() == Qt::Checked)
                ids.push_back(toCore(groups_->item(i)->data(kIdRole).toString()));
        plynth::MaterialEdit edit;
        edit.groups = ids;
        applyEdit(edit);
    });

    box->addWidget(mutedLabel(tr("Tags, separated by commas"), body));
    tags_ = new QLineEdit(body);
    tags_->setFont(t.uiFont(t.sizeNormal()));
    tags_->setStyle(t.controlStyle());
    tags_->setToolTip(tr("Words that help find the material. Lower case letters, digits, _ and -. Press Enter to save"));
    tags_->setProperty("tourId", QStringLiteral("materials-tags"));
    box->addWidget(tags_);
    connect(tags_, &QLineEdit::editingFinished, this, [this]() {
        if (refreshing_ || !current_)
            return;
        std::vector<std::string> tags;
        for (const QString& part : tags_->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString tag = part.trimmed().toLower();
            if (!tag.isEmpty())
                tags.push_back(toCore(tag));
        }
        std::sort(tags.begin(), tags.end());
        tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
        if (tags == current_->tags)
            return;
        plynth::MaterialEdit edit;
        edit.tags = tags;
        applyEdit(edit);
    });

    problems_ = new QLabel(body);
    problems_->setTextFormat(Qt::PlainText);
    problems_->setWordWrap(true);
    problems_->setFont(t.uiFont(t.sizeSmall()));
    problems_->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
    problems_->setToolTip(tr("Warnings from the import and the last render"));
    problems_->setProperty("tourId", QStringLiteral("materials-warnings"));
    box->addWidget(problems_);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(t.unit() * 2);
    retry_ = new QPushButton(tr("Retry render"), body);
    retry_->setToolTip(tr("Clean the maps again and render a new thumbnail"));
    retry_->setProperty("tourId", QStringLiteral("materials-retry"));
    retry_->setEnabled(false);
    connect(retry_, &QPushButton::clicked, this, [this]() {
        if (current_)
            emit retryRequested(fromCore(current_->id));
    });
    openFolder_ = new QPushButton(tr("Open folder"), body);
    openFolder_->setToolTip(tr("Open the folder of the material in Explorer"));
    openFolder_->setProperty("tourId", QStringLiteral("materials-open-folder"));
    connect(openFolder_, &QPushButton::clicked, this, [this]() {
        if (current_)
            QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(current_->folder.wstring())));
    });
    remove_ = new QPushButton(tr("Remove"), body);
    remove_->setToolTip(tr("Delete the material and its folder from the library. This cannot be undone"));
    remove_->setProperty("tourId", QStringLiteral("materials-remove"));
    connect(remove_, &QPushButton::clicked, this, &MaterialsPanel::removeSelected);
    buttons->addWidget(retry_);
    buttons->addWidget(openFolder_);
    buttons->addWidget(remove_);
    box->addLayout(buttons);
    box->addStretch(1);
    detailsScroll_->setWidget(body);
}

// Turns Import and the sample button on or off.
void MaterialsPanel::setImportEnabled(bool enabled, const QString& tip)
{
    import_->setEnabled(enabled);
    import_->setToolTip(plainToolTip(tip));
    samples_->setEnabled(enabled);
}

// Puts the import queue under the controls, so it shows over the empty state and over the grid alike.
void MaterialsPanel::setQueueView(QWidget* view)
{
    column_->insertWidget(1, view);
}

// Scrolls the details to the group list and puts the focus on it.
void MaterialsPanel::showGroups()
{
    if (!current_)
        return;
    detailsScroll_->ensureWidgetVisible(groups_);
    groups_->setFocus();
}

// Reads the catalog again and rebuilds the filter, the grid and the details.
void MaterialsPanel::refresh()
{
    const QString keep = current_ ? fromCore(current_->id) : QString();
    tileCache_.clear();
    const bool open = host_.hasLibrary() && host_.materialCatalog() != nullptr;
    controls_->setVisible(open);
    if (!open) {
        refreshing_ = true;
        emptyText_->setText(host_.libraryProblem().isEmpty() ? tr("No library folder is set.") : host_.libraryProblem());
        emptyHelp_->clear();
        emptyHelp_->hide();
        openSetup_->show();
        samples_->hide();
        pages_->setCurrentIndex(0);
        model_->clear();
        current_.reset();
        detailsScroll_->hide();
        refreshing_ = false;
        return;
    }
    if (host_.materialCatalog()->materialCount() == 0) {
        refreshing_ = true;
        emptyText_->setText(tr("No materials yet."));
        emptyHelp_->setText(tr("Materials you add to the library show up here, with their thumbnails."));
        emptyHelp_->show();
        openSetup_->hide();
        samples_->show();
        pages_->setCurrentIndex(0);
        model_->clear();
        current_.reset();
        detailsScroll_->hide();
        fillGroupFilter();
        refreshing_ = false;
        return;
    }
    pages_->setCurrentIndex(1);
    fillGroupFilter();
    current_.reset();
    if (!keep.isEmpty() && host_.materialCatalog()->material(toCore(keep)))
        current_ = host_.materialCatalog()->material(toCore(keep));
    fillGrid();
}

// Fills the group filter with the groups and their counts, keeping what was chosen when it still exists.
void MaterialsPanel::fillGroupFilter()
{
    const QVariant keep = group_->currentData();
    group_->clear();
    group_->addItem(tr("All groups"), kAllGroups);
    if (!host_.materialCatalog())
        return;
    plynth::MaterialQuery loose;
    loose.uncategorizedOnly = true;
    group_->addItem(tr("Uncategorized (%1)").arg(host_.materialCatalog()->search(loose).size()), kUncategorized);
    for (const plynth::MaterialGroupEntry& entry : host_.materialCatalog()->groups())
        group_->addItem(QStringLiteral("%1 (%2)").arg(fromCore(entry.name)).arg(entry.materialCount), fromCore(entry.id));
    const int index = group_->findData(keep);
    group_->setCurrentIndex(index >= 0 ? index : 0);
    updateGroupButtons();
}

// Turns Rename and Remove on only while a real group that is not built in is chosen in the filter.
void MaterialsPanel::updateGroupButtons()
{
    if (!group_ || !renameGroup_)
        return;
    const QString id = group_->currentData().toString();
    const bool real = !id.isEmpty() && id != kAllGroups && id != kUncategorized;
    bool builtIn = false;
    if (real && host_.materialCatalog())
        for (const plynth::MaterialGroupEntry& entry : host_.materialCatalog()->groups())
            if (fromCore(entry.id) == id)
                builtIn = entry.builtIn;
    renameGroup_->setEnabled(real && !builtIn);
    removeGroup_->setEnabled(real && !builtIn);
    const QString why = tr("Built-in groups cannot be renamed or removed.");
    renameGroup_->setToolTip(builtIn ? why : tr("Change the name of the group shown in the filter above"));
    removeGroup_->setToolTip(builtIn ? why
                                     : tr("Delete the group shown in the filter above. The materials stay but lose the group"));
}

// Asks for a name and adds a group with an id made from it.
void MaterialsPanel::addGroup()
{
    plynth::MaterialCatalog* catalog = host_.materialCatalog();
    if (!catalog)
        return;
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Add group"), tr("Name of the group"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    const std::string id = plynth::uniqueCatalogId(plynth::catalogIdFromName(name.toStdString()),
                                                   [catalog](std::string_view candidate) {
                                                       for (const plynth::MaterialGroupEntry& entry : catalog->groups())
                                                           if (entry.id == candidate)
                                                               return true;
                                                       return false;
                                                   });
    const plynth::Status status = catalog->addGroup(id, name.toStdString());
    if (!status.ok())
        emit message(fromCore(status.message));
    host_.materialsEdited();
    if (status.ok()) {
        const int index = group_->findData(fromCore(id));
        if (index >= 0) {
            group_->setCurrentIndex(index);
            fillGrid();
        }
    }
}

// Asks for a new name for the group chosen in the filter.
void MaterialsPanel::renameGroup()
{
    plynth::MaterialCatalog* catalog = host_.materialCatalog();
    if (!catalog || !renameGroup_->isEnabled())
        return;
    const QString id = group_->currentData().toString();
    QString current = group_->currentText();
    for (const plynth::MaterialGroupEntry& entry : catalog->groups())
        if (fromCore(entry.id) == id)
            current = fromCore(entry.name);
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Rename group"), tr("New name"), QLineEdit::Normal, current, &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    const plynth::Status status = catalog->renameGroup(id.toStdString(), name.toStdString());
    if (!status.ok())
        emit message(fromCore(status.message));
    host_.materialsEdited();
}

// Asks, then removes the group chosen in the filter, saying how many materials lose it.
void MaterialsPanel::removeGroup()
{
    plynth::MaterialCatalog* catalog = host_.materialCatalog();
    if (!catalog || !removeGroup_->isEnabled())
        return;
    const QString id = group_->currentData().toString();
    QString name = group_->currentText();
    int count = 0;
    for (const plynth::MaterialGroupEntry& entry : catalog->groups())
        if (fromCore(entry.id) == id) {
            name = fromCore(entry.name);
            count = entry.materialCount;
        }
    QMessageBox box(this);
    box.setIcon(QMessageBox::NoIcon);
    box.setWindowTitle(tr("Remove group"));
    box.setTextFormat(Qt::PlainText);
    box.setText(tr("Remove the group %1? %n material(s) lose it. The materials stay in the library.", nullptr, count).arg(name));
    QPushButton* yes = box.addButton(tr("Remove"), QMessageBox::DestructiveRole);
    QPushButton* no = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    yes->setToolTip(tr("Delete the group"));
    no->setToolTip(tr("Keep the group"));
    box.setDefaultButton(no);
    box.exec();
    if (box.clickedButton() != yes)
        return;
    const plynth::Status status = catalog->removeGroup(id.toStdString());
    if (!status.ok())
        emit message(fromCore(status.message));
    host_.materialsEdited();
}

// The tile picture of a material, from disk and kept until the next refresh.
QPixmap MaterialsPanel::tilePixmap(const plynth::MaterialRecord& record)
{
    const QString file = QString::fromStdWString(record.thumbFile.wstring());
    const auto found = tileCache_.constFind(file);
    if (found != tileCache_.constEnd())
        return found.value();
    QImageReader reader(file);
    const qreal ratio = devicePixelRatioF();
    const int pixels = qRound(Theme::instance().materialGridThumb() * ratio);
    if (reader.size().isValid())
        reader.setScaledSize(reader.size().scaled(pixels, pixels, Qt::KeepAspectRatio));
    const QImage image = reader.read();
    QPixmap pixmap;
    if (!image.isNull()) {
        pixmap = QPixmap::fromImage(image);
        pixmap.setDevicePixelRatio(ratio);
    }
    tileCache_.insert(file, pixmap);
    return pixmap;
}

// Runs the search with the text and the group, fills the grid and keeps the selected material selected.
void MaterialsPanel::fillGrid()
{
    if (!host_.materialCatalog())
        return;
    const Theme& t = Theme::instance();
    plynth::MaterialQuery query;
    query.text = toCore(search_->text().trimmed());
    const QString groupId = group_->currentData().toString();
    if (groupId == kUncategorized)
        query.uncategorizedOnly = true;
    else if (!groupId.isEmpty() && groupId != kAllGroups)
        query.group = toCore(groupId);

    const QString keep = current_ ? fromCore(current_->id) : QString();
    refreshing_ = true;
    model_->clear();
    const std::vector<plynth::MaterialRecord> found = host_.materialCatalog()->search(query);
    for (const plynth::MaterialRecord& record : found) {
        auto* item = new QStandardItem(fromCore(record.name));
        item->setData(fromCore(record.id), kIdRole);
        item->setData(MaterialPreview::tileText(record.tileSize), kTileRole);
        item->setToolTip(plainToolTip(tr("%1. Click to see and edit its details").arg(fromCore(record.name))));
        QString state;
        QColor color = t.muted();
        if (record.state == plynth::MaterialState::Broken) {
            state = tr("Broken");
            color = t.signalAmber();
        } else if (record.thumbnail == plynth::ThumbnailState::Failed) {
            state = tr("Render failed");
            color = t.signalAmber();
        } else if (record.thumbnail == plynth::ThumbnailState::Missing) {
            state = tr("Rendering");
        } else {
            const QPixmap pixmap = tilePixmap(record);
            if (pixmap.isNull()) {
                state = tr("Render failed");
                color = t.signalAmber();
            } else {
                item->setData(pixmap, kPixmapRole);
            }
        }
        item->setData(state, kStateRole);
        item->setData(color, kStateColorRole);
        model_->appendRow(item);
    }
    countLabel_->setText(found.size() == 1 ? tr("1 material") : tr("%1 materials").arg(found.size()));
    current_.reset();
    for (int row = 0; row < model_->rowCount() && !keep.isEmpty(); ++row) {
        const QModelIndex index = model_->index(row, 0);
        if (index.data(kIdRole).toString() == keep) {
            grid_->setCurrentIndex(index);
            current_ = host_.materialCatalog()->material(toCore(keep));
        }
    }
    refreshing_ = false;
    showDetails();
}

// Selects a material by id. Returns false when it is not in the grid.
bool MaterialsPanel::selectMaterial(const QString& id)
{
    for (int row = 0; row < model_->rowCount(); ++row) {
        const QModelIndex index = model_->index(row, 0);
        if (index.data(kIdRole).toString() != id)
            continue;
        refreshing_ = true;
        grid_->setCurrentIndex(index);
        grid_->scrollTo(index);
        refreshing_ = false;
        current_ = host_.materialCatalog() ? host_.materialCatalog()->material(toCore(id)) : std::nullopt;
        showDetails();
        return true;
    }
    return false;
}

// The id of the selected material, empty when none is.
QString MaterialsPanel::selectedId() const
{
    return current_ ? fromCore(current_->id) : QString();
}

// Redraws the split when the grid is resized or shown, so only whole rows of tiles are visible.
bool MaterialsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == grid_ && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        QTimer::singleShot(0, this, [this]() { snapSplit(); });
    return QWidget::eventFilter(watched, event);
}

// Moves the split so the grid ends on a whole row of tiles. The spare pixels go to the details.
void MaterialsPanel::snapSplit()
{
    if (!detailsScroll_->isVisible() || model_->rowCount() == 0)
        return;
    const QRect first = grid_->visualRect(model_->index(0, 0));
    int pitch = 0;
    for (int i = 1; i < model_->rowCount(); ++i) {
        const QRect r = grid_->visualRect(model_->index(i, 0));
        if (r.top() > first.top()) {
            pitch = r.top() - first.top();
            break;
        }
    }
    if (pitch <= 0)
        pitch = first.height() + Theme::instance().unit();
    const int height = grid_->viewport()->height();
    const int rows = std::max(1, height / pitch);
    const int spare = height - rows * pitch;
    QList<int> sizes = splitter_->sizes();
    if (spare <= 0 || sizes.size() < 2 || sizes[0] - spare < grid_->minimumHeight())
        return;
    sizes[0] -= spare;
    sizes[1] += spare;
    splitter_->setSizes(sizes);
}

// Fills the details from the selected material, or hides them.
void MaterialsPanel::showDetails()
{
    if (!current_ || !host_.materialCatalog()) {
        detailsScroll_->hide();
        return;
    }
    const Theme& t = Theme::instance();
    const bool was = refreshing_;
    refreshing_ = true;
    const plynth::MaterialRecord& record = *current_;
    detailsScroll_->show();

    MaterialPreview::showThumbnail(thumb_, record, t.thumbnailLarge(), devicePixelRatioF());
    name_->setText(fromCore(record.name));
    idLabel_->setText(fromCore(record.id));
    tileWidth_->setValue(record.tileSize.width);
    tileHeight_->setValue(record.tileSize.height);

    const plynth::MaterialInfoRead info = host_.materialCatalog()->info(record.id);
    if (info.status.ok()) {
        textureLabel_->setText(QStringLiteral("%1 x %2 px").arg(info.info.textureSize.width).arg(info.info.textureSize.height));
        textureLabel_->show();
    } else {
        textureLabel_->hide();
    }

    while (QLayoutItem* item = mapsGrid_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (info.status.ok()) {
        int row = 0;
        for (const plynth::MapChannel channel : plynth::allMapChannels()) {
            auto* name = mutedLabel(channelText(channel), mapsBox_);
            auto* how = new QLabel(mapsBox_);
            how->setTextFormat(Qt::PlainText);
            how->setText(originText(plynth::materialMap(info.info.maps, channel)));
            how->setFont(t.monoFont(t.sizeSmall()));
            how->setTextInteractionFlags(Qt::NoTextInteraction);
            how->setWordWrap(true);
            mapsGrid_->addWidget(name, row, 0, Qt::AlignTop);
            mapsGrid_->addWidget(how, row, 1, Qt::AlignTop);
            ++row;
        }
    }
    mapsBox_->setVisible(info.status.ok());

    sourceLabel_->setText(fromCore(record.sourceKind) +
                          (record.sourceReference.empty() ? QString() : QStringLiteral(": ") + fromCore(record.sourceReference)));

    license_->clear();
    for (const plynth::LicenseEntry& entry : host_.materialCatalog()->licenses())
        license_->addItem(fromCore(entry.name), fromCore(entry.id));
    license_->setCurrentIndex(std::max(0, license_->findData(fromCore(record.license))));

    groups_->clear();
    for (const plynth::MaterialGroupEntry& entry : host_.materialCatalog()->groups()) {
        auto* item = new QListWidgetItem(fromCore(entry.name), groups_);
        item->setData(kIdRole, fromCore(entry.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool on = std::find(record.groups.begin(), record.groups.end(), entry.id) != record.groups.end();
        item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
    if (groups_->count() > 8 && groups_->sizeHintForRow(0) > 0)
        groups_->setFixedHeight(static_cast<int>(groups_->sizeHintForRow(0) * 8.5) + 4);
    QStringList tags;
    for (const std::string& tag : record.tags)
        tags << fromCore(tag);
    tags_->setText(tags.join(QStringLiteral(", ")));

    QStringList problems;
    if (record.state == plynth::MaterialState::NeedsRefresh && record.problem.empty())
        problems << tr("Needs a refresh. Use Retry render.");
    if (!record.problem.empty())
        problems << fromCore(record.problem);
    if (!record.thumbnailError.empty())
        problems << fromCore(record.thumbnailError);
    if (info.status.ok())
        for (const plynth::BlenderWarning& warning : info.info.warnings)
            problems << fromCore(warning.message);
    problems_->setText(problems.join(QLatin1Char('\n')));
    problems_->setVisible(!problems.isEmpty());

    const bool busy = busy_.contains(fromCore(record.id));
    retry_->setEnabled(renderEnabled_ && !busy);
    retry_->setToolTip(!renderEnabled_ ? tr("A render needs a library and Blender")
                       : busy          ? tr("This material is already being cleaned and rendered")
                                       : tr("Clean the maps again and render a new thumbnail"));
    refreshing_ = was;
}

// Remembers which materials are being rendered, so Retry waits for them.
void MaterialsPanel::setBusyMaterials(const QStringList& ids)
{
    busy_ = ids;
    if (current_)
        showDetails();
}

// Turns Retry on or off.
void MaterialsPanel::setRenderEnabled(bool enabled)
{
    renderEnabled_ = enabled;
    if (current_)
        showDetails();
}

// Saves an edit of the selected material and shows the result. A refusal goes to the status bar.
void MaterialsPanel::applyEdit(const plynth::MaterialEdit& edit)
{
    if (!current_ || !host_.materialCatalog())
        return;
    const plynth::Status status = host_.materialCatalog()->editMaterial(current_->id, edit);
    if (!status.ok())
        emit message(fromCore(status.message));
    QTimer::singleShot(0, this, [this]() { host_.materialsEdited(); });
}

// Asks, then deletes the selected material with its folder. A plan that names it keeps the name and shows it as
// missing.
void MaterialsPanel::removeSelected()
{
    if (!current_ || !host_.materialCatalog())
        return;
    QMessageBox box(this);
    box.setIcon(QMessageBox::NoIcon);
    box.setWindowTitle(tr("Remove material"));
    box.setTextFormat(Qt::PlainText);
    box.setText(tr("Remove %1 and its folder from the library? This cannot be undone. Plans that use it keep its name and show it as missing.")
                    .arg(fromCore(current_->name)));
    QPushButton* yes = box.addButton(tr("Remove"), QMessageBox::DestructiveRole);
    QPushButton* no = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    yes->setToolTip(tr("Delete the material and its folder"));
    no->setToolTip(tr("Keep the material"));
    box.setDefaultButton(no);
    box.exec();
    if (box.clickedButton() != yes)
        return;
    const plynth::Status status = host_.materialCatalog()->removeMaterial(current_->id);
    if (!status.ok())
        emit message(fromCore(status.message));
    current_.reset();
    host_.materialsEdited();
}

}
