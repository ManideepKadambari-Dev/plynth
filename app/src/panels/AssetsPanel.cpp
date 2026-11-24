#include "AssetsPanel.h"

#include "catalog/LibraryHost.h"
#include "furniture/AssetPreview.h"
#include "preview/AssetStage.h"
#include "theme/PlainText.h"
#include "theme/Theme.h"

#include <plynth/core/RoomTypes.h>
#include <plynth/core/catalog/AssetInfo.h>
#include <plynth/core/ProjectFiles.h>

#include <QComboBox>
#include <QDesktopServices>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
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
constexpr int kSizeRole = Qt::UserRole + 5;
constexpr int kSearchDelayMs = 200;

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

// The MIME type of a dragged asset tile. It holds the asset id as plain bytes.
const QString kAssetMime = QStringLiteral("application/x-plynth-asset");

// A model that hands the asset id to a drag, so the plan and the wall view can place it.
class AssetModel : public QStandardItemModel {
public:
    using QStandardItemModel::QStandardItemModel;

    // The one type a tile drag carries.
    QStringList mimeTypes() const override { return {kAssetMime}; }

    // Packs the id of the first dragged tile.
    QMimeData* mimeData(const QModelIndexList& indexes) const override
    {
        if (indexes.isEmpty())
            return nullptr;
        auto* data = new QMimeData();
        data->setData(kAssetMime, indexes.first().data(kIdRole).toString().toUtf8());
        return data;
    }
};

// Loads an image file scaled to fit a square, keeping its shape. Null when the file cannot be read.
QPixmap loadScaled(const QString& file, int side, qreal ratio)
{
    QImageReader reader(file);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    const int pixels = qRound(side * ratio);
    if (original.isValid())
        reader.setScaledSize(original.scaled(pixels, pixels, Qt::KeepAspectRatio));
    const QImage image = reader.read();
    if (image.isNull())
        return QPixmap();
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}

// Draws one grid tile: the thumbnail or the reason there is none, then the name and the size.
class TileDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    // The cell size: the picture square plus two text lines.
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        const Theme& t = Theme::instance();
        return QSize(t.thumbnailTile() + t.unit() * 6, t.thumbnailTile() + t.unit() * 12);
    }

    // Paints the tile with the Signal blue outline when selected.
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const Theme& t = Theme::instance();
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        const int tile = t.thumbnailTile();
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
            p->setFont(t.uiFont(t.sizeNormal()));
            p->drawText(image, Qt::AlignCenter | Qt::TextWordWrap, state);
        }
        const bool selected = option.state & QStyle::State_Selected;
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(selected ? t.signalAmber() : t.line(), selected ? 2 : 1));
        p->drawRoundedRect(QRectF(image).adjusted(0.5, 0.5, -0.5, -0.5), t.radius(), t.radius());

        const QRect nameRect(cell.left() + t.unit(), image.bottom() + t.unit() * 2, cell.width() - t.unit() * 2,
                             t.unit() * 5);
        p->setPen(t.text());
        p->setFont(t.uiFont(t.sizeNormal()));
        p->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                                nameRect.width()));
        p->setPen(t.muted());
        p->setFont(t.monoFont(t.sizeSmall()));
        p->drawText(nameRect.translated(0, t.unit() * 5), Qt::AlignLeft | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(index.data(kSizeRole).toString(), Qt::ElideRight,
                                                nameRect.width()));
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

}

// Builds the controls, the grid and the details.
AssetsPanel::AssetsPanel(LibraryHost& host, QWidget* parent) : QWidget(parent), host_(host)
{
    const Theme& t = Theme::instance();
    setProperty("tourId", QStringLiteral("catalog-assets"));
    setToolTip(tr("The assets catalog: search, pick and edit the models you imported"));
    setAcceptDrops(true);

    auto* column = new QVBoxLayout(this);
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
    emptyText_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    emptyColumn->addWidget(emptyText_);
    auto* setup = new QPushButton(tr("Open setup check"), empty);
    setup->setToolTip(tr("Open the setup check to choose or create the library folder"));
    setup->setProperty("tourId", QStringLiteral("catalog-open-setup"));
    connect(setup, &QPushButton::clicked, this, &AssetsPanel::setupRequested);
    emptyColumn->addWidget(setup, 0, Qt::AlignLeft);
    emptyColumn->addStretch(1);
    pages_->addWidget(empty);

    splitter_ = new QSplitter(Qt::Vertical, pages_);
    splitter_->setChildrenCollapsible(false);
    splitter_->setHandleWidth(t.unit());
    splitter_->setStyleSheet(QStringLiteral("QSplitter::handle { background: %1; }").arg(t.line().name()));
    top_ = new QWidget(splitter_);
    topLayout_ = new QVBoxLayout(top_);
    topLayout_->setContentsMargins(0, 0, 0, 0);
    topLayout_->setSpacing(t.unit() * 2);
    countLabel_ = mutedLabel(QString(), top_);
    countLabel_->setToolTip(tr("How many assets match the search and the filters"));
    topLayout_->addWidget(countLabel_);

    model_ = new AssetModel(this);
    grid_ = new QListView(top_);
    grid_->setModel(model_);
    grid_->setViewMode(QListView::IconMode);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setUniformItemSizes(true);
    grid_->setWrapping(true);
    grid_->setSpacing(t.unit());
    grid_->setSelectionMode(QAbstractItemView::SingleSelection);
    grid_->setDragEnabled(true);
    grid_->setDragDropMode(QAbstractItemView::DragOnly);
    grid_->setDefaultDropAction(Qt::CopyAction);
    grid_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    grid_->setFrameShape(QFrame::NoFrame);
    grid_->setStyleSheet(QStringLiteral("QListView { background: transparent; }"));
    grid_->setItemDelegate(new TileDelegate(grid_));
    grid_->setToolTip(tr("The assets in the library. Click one to see and edit its details"));
    grid_->setProperty("tourId", QStringLiteral("catalog-grid"));
    grid_->setMinimumHeight(t.thumbnailTile() + t.unit() * 14);
    topLayout_->addWidget(grid_, 1);
    splitter_->addWidget(top_);

    buildDetails();
    splitter_->addWidget(detailsScroll_);
    splitter_->setStretchFactor(0, 3);
    splitter_->setStretchFactor(1, 2);
    detailsScroll_->hide();
    pages_->addWidget(splitter_);
    column->addWidget(pages_, 1);

    connect(grid_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this]() {
        if (refreshing_)
            return;
        const QModelIndex index = grid_->currentIndex();
        current_.reset();
        if (index.isValid() && host_.catalog())
            current_ = host_.catalog()->asset(toCore(index.data(kIdRole).toString()));
        showDetails();
    });
    connect(&host_, &LibraryHost::assetsChanged, this, &AssetsPanel::refresh);
    connect(&host_, &LibraryHost::libraryChanged, this, &AssetsPanel::refresh);
    connect(&host_, &LibraryHost::roomTypesChanged, this, &AssetsPanel::refresh);
    refresh();
}

// Builds the search box, the two filters and the Rescan and Import buttons.
void AssetsPanel::buildControls(QVBoxLayout* column)
{
    const Theme& t = Theme::instance();
    controls_ = new QWidget(this);
    auto* box = new QVBoxLayout(controls_);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(t.unit() * 2);

    search_ = new QLineEdit(controls_);
    search_->setPlaceholderText(tr("Search assets"));
    search_->setClearButtonEnabled(true);
    search_->setFont(t.uiFont(t.sizeNormal()));
    search_->setStyle(t.controlStyle());
    search_->setToolTip(tr("Type part of a name, tag or category. The grid filters as you type"));
    search_->setProperty("tourId", QStringLiteral("catalog-search"));
    box->addWidget(search_);
    searchTimer_ = new QTimer(this);
    searchTimer_->setSingleShot(true);
    searchTimer_->setInterval(kSearchDelayMs);
    connect(search_, &QLineEdit::textChanged, searchTimer_, qOverload<>(&QTimer::start));
    connect(searchTimer_, &QTimer::timeout, this, &AssetsPanel::fillGrid);

    auto* filters = new QHBoxLayout();
    filters->setSpacing(t.unit() * 2);
    room_ = new QComboBox(controls_);
    room_->setFont(t.uiFont(t.sizeNormal()));
    room_->setStyle(t.controlStyle());
    room_->setToolTip(tr("Show only the assets that belong in this kind of room"));
    room_->setProperty("tourId", QStringLiteral("catalog-room"));
    category_ = new QComboBox(controls_);
    category_->setFont(t.uiFont(t.sizeNormal()));
    category_->setStyle(t.controlStyle());
    category_->setToolTip(tr("Show only one category, or the assets that have no category yet"));
    category_->setProperty("tourId", QStringLiteral("catalog-category"));
    room_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    category_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    filters->addWidget(room_, 1);
    filters->addWidget(category_, 1);
    box->addLayout(filters);
    connect(room_, &QComboBox::activated, this, [this]() {
        fillCategoryFilter();
        fillGrid();
    });
    connect(category_, &QComboBox::activated, this, &AssetsPanel::fillGrid);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(t.unit() * 2);
    rescan_ = new QPushButton(tr("Rescan"), controls_);
    rescan_->setToolTip(tr("Read the library folder again and update the index"));
    rescan_->setProperty("tourId", QStringLiteral("catalog-rescan"));
    connect(rescan_, &QPushButton::clicked, this, &AssetsPanel::rescanRequested);
    import_ = new QPushButton(tr("Import"), controls_);
    import_->setDefault(true);
    import_->setToolTip(tr("Import arrives with the import queue"));
    import_->setProperty("tourId", QStringLiteral("catalog-import"));
    import_->setEnabled(false);
    connect(import_, &QPushButton::clicked, this, &AssetsPanel::importRequested);
    buttons->addWidget(rescan_);
    buttons->addStretch(1);
    buttons->addWidget(import_);
    box->addLayout(buttons);
    column->addWidget(controls_);
}

// Builds the details area for the selected asset.
void AssetsPanel::buildDetails()
{
    const Theme& t = Theme::instance();
    detailsScroll_ = new QScrollArea(splitter_);
    detailsScroll_->setWidgetResizable(true);
    detailsScroll_->setFrameShape(QFrame::NoFrame);
    detailsScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailsScroll_->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
    auto* body = new QWidget();
    auto* box = new QVBoxLayout(body);
    box->setContentsMargins(0, t.unit() * 2, t.unit() * 4, 0);
    box->setSpacing(t.unit() * 2);

    stage_ = new AssetStage(true, t.thumbnailLarge(), body);
    stage_->setToolTip(tr("The asset in 3D. Drag to orbit, wheel to zoom. Until it is drawn, the thumbnail from Blender shows"));
    stage_->setProperty("tourId", QStringLiteral("catalog-thumbnail"));
    thumb_ = stage_->thumbnail();
    box->addWidget(stage_, 0, Qt::AlignHCenter);

    name_ = new QLineEdit(body);
    name_->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    name_->setStyle(t.controlStyle());
    name_->setToolTip(tr("The name of the asset. Press Enter to save a new name"));
    name_->setProperty("tourId", QStringLiteral("catalog-name"));
    box->addWidget(name_);
    connect(name_, &QLineEdit::editingFinished, this, [this]() {
        if (refreshing_ || !current_ || name_->text().trimmed() == fromCore(current_->name))
            return;
        plynth::AssetEdit edit;
        edit.name = toCore(name_->text().trimmed());
        applyEdit(edit);
    });

    auto addMono = [&](const QString& tip, const QString& tour) {
        auto* label = new QLabel(body);
        label->setTextFormat(Qt::PlainText);
        label->setFont(t.monoFont(t.sizeSmall()));
        label->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
        label->setToolTip(tip);
        label->setProperty("tourId", tour);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::NoTextInteraction);
        box->addWidget(label);
        return label;
    };
    idLabel_ = addMono(tr("The id of the asset, used by the command line"), QStringLiteral("catalog-id"));
    sizeLabel_ = addMono(tr("Width, depth and height in metres"), QStringLiteral("catalog-size"));
    trianglesLabel_ = addMono(tr("How many triangles the model has"), QStringLiteral("catalog-triangles"));
    sourceLabel_ = addMono(tr("Where the asset came from"), QStringLiteral("catalog-source"));

    box->addWidget(mutedLabel(tr("License"), body));
    license_ = new QComboBox(body);
    license_->setFont(t.uiFont(t.sizeNormal()));
    license_->setStyle(t.controlStyle());
    license_->setToolTip(tr("The license the asset came with"));
    license_->setProperty("tourId", QStringLiteral("catalog-license"));
    box->addWidget(license_);
    connect(license_, &QComboBox::activated, this, [this](int index) {
        if (refreshing_ || !current_)
            return;
        plynth::AssetEdit edit;
        edit.license = toCore(license_->itemData(index).toString());
        applyEdit(edit);
    });

    box->addWidget(mutedLabel(tr("Author"), body));
    author_ = new QLineEdit(body);
    author_->setFont(t.uiFont(t.sizeNormal()));
    author_->setStyle(t.controlStyle());
    author_->setMaxLength(static_cast<int>(plynth::kMaxAuthorLength));
    author_->setPlaceholderText(tr("Unknown"));
    author_->setToolTip(tr("Who made the asset, shown in the license report. Press Enter to save"));
    author_->setProperty("tourId", QStringLiteral("catalog-author"));
    box->addWidget(author_);
    connect(author_, &QLineEdit::editingFinished, this, [this]() {
        if (refreshing_ || !current_ || author_->text().trimmed() == shownAuthor_)
            return;
        plynth::AssetEdit edit;
        edit.author = toCore(author_->text().trimmed());
        applyEdit(edit);
    });

    box->addWidget(mutedLabel(tr("Categories"), body));
    categories_ = new QListWidget(body);
    categories_->setFont(t.uiFont(t.sizeNormal()));
    categories_->setStyle(t.controlStyle());
    categories_->setFixedHeight(t.unit() * 40);
    categories_->setStyleSheet(t.scrollBarStyleSheet());
    categories_->setToolTip(tr("Tick every category the asset belongs to"));
    categories_->setProperty("tourId", QStringLiteral("catalog-categories"));
    box->addWidget(categories_);
    connect(categories_, &QListWidget::itemChanged, this, [this]() {
        if (refreshing_ || !current_)
            return;
        std::vector<std::string> ids;
        for (int i = 0; i < categories_->count(); ++i)
            if (categories_->item(i)->checkState() == Qt::Checked)
                ids.push_back(toCore(categories_->item(i)->data(kIdRole).toString()));
        plynth::AssetEdit edit;
        edit.categories = ids;
        applyEdit(edit);
    });

    box->addWidget(mutedLabel(tr("Tags, separated by commas"), body));
    tags_ = new QLineEdit(body);
    tags_->setFont(t.uiFont(t.sizeNormal()));
    tags_->setStyle(t.controlStyle());
    tags_->setToolTip(tr("Words that help find the asset. Lower case letters, digits, _ and -. Press Enter to save"));
    tags_->setProperty("tourId", QStringLiteral("catalog-tags"));
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
        plynth::AssetEdit edit;
        edit.tags = tags;
        applyEdit(edit);
    });

    problems_ = new QLabel(body);
    problems_->setTextFormat(Qt::PlainText);
    problems_->setWordWrap(true);
    problems_->setFont(t.uiFont(t.sizeSmall()));
    problems_->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
    problems_->setToolTip(tr("Warnings from the import and the last render"));
    problems_->setProperty("tourId", QStringLiteral("catalog-warnings"));
    box->addWidget(problems_);

    materialsTitle_ = mutedLabel(tr("Materials"), body);
    box->addWidget(materialsTitle_);
    materialsBox_ = new QWidget(body);
    materialsBox_->setToolTip(tr("The materials inside the asset. Each can be added to the materials catalog"));
    materialsBox_->setProperty("tourId", QStringLiteral("catalog-materials"));
    materialsColumn_ = new QVBoxLayout(materialsBox_);
    materialsColumn_->setContentsMargins(0, 0, 0, 0);
    materialsColumn_->setSpacing(t.unit());
    box->addWidget(materialsBox_);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(t.unit() * 2);
    retry_ = new QPushButton(tr("Retry render"), body);
    retry_->setToolTip(tr("Measure the asset again and render a new thumbnail"));
    retry_->setProperty("tourId", QStringLiteral("catalog-retry"));
    retry_->setEnabled(false);
    connect(retry_, &QPushButton::clicked, this, [this]() {
        if (current_)
            emit retryRequested(fromCore(current_->id));
    });
    openFolder_ = new QPushButton(tr("Open folder"), body);
    openFolder_->setToolTip(tr("Open the folder of the asset in Explorer"));
    openFolder_->setProperty("tourId", QStringLiteral("catalog-open-folder"));
    connect(openFolder_, &QPushButton::clicked, this, [this]() {
        if (current_)
            QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(current_->folder.wstring())));
    });
    remove_ = new QPushButton(tr("Remove"), body);
    remove_->setToolTip(tr("Delete the asset and its folder from the library. This cannot be undone"));
    remove_->setProperty("tourId", QStringLiteral("catalog-remove"));
    connect(remove_, &QPushButton::clicked, this, &AssetsPanel::removeSelected);
    buttons->addWidget(retry_);
    buttons->addWidget(openFolder_);
    buttons->addWidget(remove_);
    box->addLayout(buttons);
    box->addStretch(1);
    detailsScroll_->setWidget(body);
}

// Reads the catalog again and rebuilds the filters, the grid and the details.
void AssetsPanel::refresh()
{
    const QString keep = current_ ? fromCore(current_->id) : QString();
    tileCache_.clear();
    const bool open = host_.hasLibrary();
    controls_->setVisible(open);
    if (!open) {
        refreshing_ = true;
        emptyText_->setText(host_.libraryProblem().isEmpty() ? tr("No library folder is set.")
                                                             : host_.libraryProblem());
        pages_->setCurrentIndex(0);
        model_->clear();
        current_.reset();
        detailsScroll_->hide();
        refreshing_ = false;
        return;
    }
    pages_->setCurrentIndex(1);
    fillFilters();
    current_.reset();
    if (!keep.isEmpty() && host_.catalog()->asset(toCore(keep)))
        current_ = host_.catalog()->asset(toCore(keep));
    fillGrid();
}

// Fills the room and category filters, keeping what was chosen when it still exists.
void AssetsPanel::fillFilters()
{
    const QVariant roomKeep = room_->currentData();
    room_->clear();
    room_->addItem(tr("All rooms"), QString());
    for (const plynth::RoomType& type : plynth::roomTypes())
        room_->addItem(fromCore(type.name), fromCore(type.id));
    const int roomIndex = room_->findData(roomKeep);
    room_->setCurrentIndex(roomIndex >= 0 ? roomIndex : 0);
    fillCategoryFilter();
}

// Fills the category filter with the categories of the chosen room, or all of them.
void AssetsPanel::fillCategoryFilter()
{
    const QVariant keep = category_->currentData();
    category_->clear();
    category_->addItem(tr("All categories"), QStringLiteral("*"));
    category_->addItem(tr("Uncategorized"), QStringLiteral("-"));
    if (!host_.catalog())
        return;
    const std::string roomId = toCore(room_->currentData().toString());
    const std::vector<plynth::CategoryEntry> list =
        roomId.empty() ? host_.catalog()->categories() : host_.catalog()->categoriesFor(roomId);
    for (const plynth::CategoryEntry& entry : list)
        category_->addItem(fromCore(entry.name), fromCore(entry.id));
    const int index = category_->findData(keep);
    category_->setCurrentIndex(index >= 0 ? index : 0);
}

// The tile picture of an asset, from disk and kept until the next refresh.
QPixmap AssetsPanel::tilePixmap(const plynth::AssetRecord& record)
{
    const QString file = QString::fromStdWString(record.thumbFile.wstring());
    const auto found = tileCache_.constFind(file);
    if (found != tileCache_.constEnd())
        return found.value();
    const QPixmap pixmap = loadScaled(file, Theme::instance().thumbnailTile(), devicePixelRatioF());
    tileCache_.insert(file, pixmap);
    return pixmap;
}

// The big picture of the details.
QPixmap AssetsPanel::largePixmap(const plynth::AssetRecord& record)
{
    return loadScaled(QString::fromStdWString(record.thumbFile.wstring()), Theme::instance().thumbnailLarge(),
                      devicePixelRatioF());
}

// Runs the search with the text and the filters, fills the grid and keeps the selected asset selected.
void AssetsPanel::fillGrid()
{
    if (!host_.catalog())
        return;
    const Theme& t = Theme::instance();
    plynth::AssetQuery query;
    query.text = toCore(search_->text().trimmed());
    const QString roomId = room_->currentData().toString();
    if (!roomId.isEmpty())
        query.roomType = toCore(roomId);
    const QString categoryId = category_->currentData().toString();
    if (categoryId == QStringLiteral("-"))
        query.uncategorizedOnly = true;
    else if (!categoryId.isEmpty() && categoryId != QStringLiteral("*"))
        query.category = toCore(categoryId);

    const QString keep = current_ ? fromCore(current_->id) : QString();
    refreshing_ = true;
    model_->clear();
    const std::vector<plynth::AssetRecord> found = host_.catalog()->search(query);
    for (const plynth::AssetRecord& record : found) {
        auto* item = new QStandardItem(fromCore(record.name));
        item->setData(fromCore(record.id), kIdRole);
        item->setData(AssetPreview::sizeText(record.size), kSizeRole);
        item->setToolTip(plainToolTip(tr("%1. Click to see and edit its details").arg(fromCore(record.name))));
        QString state;
        QColor color = t.muted();
        if (record.state == plynth::AssetState::Broken) {
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
        const bool placeable = record.state != plynth::AssetState::Broken && plynth::isMeasured(record.size);
        if (placeable) {
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
            item->setToolTip(plainToolTip(tr("%1. Click to see and edit its details. Drag it onto the plan to place it")
                                              .arg(fromCore(record.name))));
        } else {
            item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
            if (record.state != plynth::AssetState::Broken)
                item->setToolTip(plainToolTip(tr("%1 is not measured yet, so it cannot be placed. Use Retry render in the details")
                                                  .arg(fromCore(record.name))));
        }
        model_->appendRow(item);
    }
    countLabel_->setText(found.size() == 1 ? tr("1 asset") : tr("%1 assets").arg(found.size()));
    current_.reset();
    for (int row = 0; row < model_->rowCount() && !keep.isEmpty(); ++row) {
        const QModelIndex index = model_->index(row, 0);
        if (index.data(kIdRole).toString() == keep) {
            grid_->setCurrentIndex(index);
            current_ = host_.catalog()->asset(toCore(keep));
        }
    }
    refreshing_ = false;
    showDetails();
}

// Selects an asset by id. Returns false when it is not in the grid.
bool AssetsPanel::selectAsset(const QString& id)
{
    for (int row = 0; row < model_->rowCount(); ++row) {
        const QModelIndex index = model_->index(row, 0);
        if (index.data(kIdRole).toString() != id)
            continue;
        refreshing_ = true;
        grid_->setCurrentIndex(index);
        grid_->scrollTo(index);
        refreshing_ = false;
        current_ = host_.catalog() ? host_.catalog()->asset(toCore(id)) : std::nullopt;
        showDetails();
        return true;
    }
    return false;
}

// True when the details hold no model or the model is drawn.
bool AssetsPanel::pictureReady() const
{
    return detailsScroll_ == nullptr || detailsScroll_->isHidden() || stage_->pictureReady();
}

// The id of the selected asset, empty when none is.
QString AssetsPanel::selectedId() const
{
    return current_ ? fromCore(current_->id) : QString();
}

// Fills the details from the selected asset, or hides them.
void AssetsPanel::showDetails()
{
    if (!current_ || !host_.catalog()) {
        stage_->clearModel();
        detailsScroll_->hide();
        return;
    }
    const Theme& t = Theme::instance();
    const bool was = refreshing_;
    refreshing_ = true;
    const plynth::AssetRecord& record = *current_;
    detailsScroll_->show();

    const QPixmap pixmap = record.thumbnail == plynth::ThumbnailState::Ok ? largePixmap(record) : QPixmap();
    if (!pixmap.isNull()) {
        thumb_->setText(QString());
        thumb_->setStyleSheet(QStringLiteral("background: %1;").arg(t.ground().name()));
        thumb_->setPixmap(pixmap);
    } else {
        QString text = tr("Rendering");
        QColor color = t.muted();
        if (record.state == plynth::AssetState::Broken || record.thumbnail != plynth::ThumbnailState::Missing) {
            text = record.state == plynth::AssetState::Broken ? tr("Broken") : tr("Render failed");
            color = t.signalAmber();
        }
        thumb_->setPixmap(QPixmap());
        thumb_->setText(text);
        thumb_->setStyleSheet(QStringLiteral("background: %1; color: %2; border-radius: %3px;")
                                  .arg(t.raised().name(), color.name())
                                  .arg(t.radius()));
    }
    stage_->showModel(record);
    name_->setText(fromCore(record.name));
    idLabel_->setText(fromCore(record.id));
    sizeLabel_->setText(AssetPreview::sizeText(record.size));
    trianglesLabel_->setText(tr("%1 triangles").arg(record.triangles));
    sourceLabel_->setText(fromCore(record.sourceKind) +
                          (record.sourceReference.empty() ? QString() : QStringLiteral(": ") + fromCore(record.sourceReference)));

    license_->clear();
    for (const plynth::LicenseEntry& entry : host_.catalog()->licenses())
        license_->addItem(fromCore(entry.name), fromCore(entry.id));
    license_->setCurrentIndex(std::max(0, license_->findData(fromCore(record.license))));
    const plynth::AssetInfoRead info = plynth::readAssetInfo(record.folder / plynth::pathFromUtf8(plynth::kAssetInfoFileName), record.id);
    shownAuthor_ = info.status.ok() ? fromCore(info.info.author) : QString();
    author_->setText(shownAuthor_);

    categories_->clear();
    for (const plynth::CategoryEntry& entry : host_.catalog()->categories()) {
        auto* item = new QListWidgetItem(fromCore(entry.name), categories_);
        item->setData(kIdRole, fromCore(entry.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool on = std::find(record.categories.begin(), record.categories.end(), entry.id) != record.categories.end();
        item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
    // Ending on half a row tells the user that the list scrolls.
    if (categories_->count() > 9 && categories_->sizeHintForRow(0) > 0)
        categories_->setFixedHeight(static_cast<int>(categories_->sizeHintForRow(0) * 9.5) + 4);
    QStringList tags;
    for (const std::string& tag : record.tags)
        tags << fromCore(tag);
    tags_->setText(tags.join(QStringLiteral(", ")));

    QStringList problems;
    if (!record.problem.empty())
        problems << fromCore(record.problem);
    if (!record.thumbnailError.empty())
        problems << fromCore(record.thumbnailError);
    if (info.status.ok())
        for (const plynth::BlenderWarning& warning : info.info.warnings)
            problems << fromCore(warning.message);
    problems_->setText(problems.join(QLatin1Char('\n')));
    problems_->setVisible(!problems.isEmpty());
    fillMaterials(record, info.status.ok() ? info.info.materials : std::vector<std::string>());

    const bool busy = busy_.contains(fromCore(record.id));
    retry_->setEnabled(retryEnabled_ && !busy);
    retry_->setToolTip(!retryEnabled_ ? tr("Retry arrives with the import queue")
                       : busy         ? tr("A render of this asset is already running")
                                      : tr("Measure the asset again and render a new thumbnail"));
    refreshing_ = was;
}

// Lists the materials inside the asset, each with a button that adds it to the materials catalog.
void AssetsPanel::fillMaterials(const plynth::AssetRecord& record, const std::vector<std::string>& names)
{
    const Theme& t = Theme::instance();
    while (QLayoutItem* item = materialsColumn_->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    materialsTitle_->setVisible(!names.empty());
    materialsBox_->setVisible(!names.empty());
    const QString assetId = fromCore(record.id);
    for (const std::string& raw : names) {
        const QString name = fromCore(raw);
        auto* row = new QWidget(materialsBox_);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(t.unit() * 2);
        auto* label = new QLabel(row);
        label->setTextFormat(Qt::PlainText);
        label->setText(name);
        label->setFont(t.monoFont(t.sizeSmall()));
        label->setMinimumWidth(0);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        label->setToolTip(plainToolTip(name));
        line->addWidget(label, 1);
        auto* add = new QPushButton(tr("Add to materials catalog"), row);
        add->setEnabled(importMaterialsEnabled_);
        add->setToolTip(importMaterialsEnabled_ ? tr("Copy this material into the materials catalog as its own material. The asset keeps its own")
                                                : tr("Set a library folder first"));
        add->setProperty("tourId", QStringLiteral("catalog-add-material"));
        connect(add, &QPushButton::clicked, this, [this, assetId, name]() { emit addMaterialRequested(assetId, name); });
        line->addWidget(add);
        materialsColumn_->addWidget(row);
    }
}

// Turns the Add to materials catalog buttons on or off.
void AssetsPanel::setMaterialImportEnabled(bool enabled)
{
    importMaterialsEnabled_ = enabled;
    if (current_)
        showDetails();
}

// Scrolls the details to the category list and puts the focus on it.
void AssetsPanel::showCategories()
{
    if (!current_)
        return;
    detailsScroll_->ensureWidgetVisible(categories_);
    categories_->setFocus();
}

// Puts the import queue between the grid and the details.
void AssetsPanel::setQueueView(QWidget* view)
{
    topLayout_->addWidget(view);
}

// Remembers which assets are being measured, so Retry waits for them.
void AssetsPanel::setBusyAssets(const QStringList& ids)
{
    busy_ = ids;
    if (current_)
        showDetails();
}

// Turns Import and Retry on or off, with the Import tooltip.
void AssetsPanel::setImportEnabled(bool enabled, const QString& tip)
{
    import_->setEnabled(enabled);
    import_->setToolTip(plainToolTip(tip));
    retryEnabled_ = enabled;
    if (current_)
        showDetails();
}

// Saves an edit of the selected asset and shows the result. A refusal goes to the status bar.
void AssetsPanel::applyEdit(const plynth::AssetEdit& edit)
{
    if (!current_ || !host_.catalog())
        return;
    const std::string id = current_->id;
    const plynth::Status status = host_.catalog()->editAsset(id, edit);
    if (!status.ok())
        emit message(fromCore(status.message));
    QTimer::singleShot(0, this, [this]() { host_.assetsEdited(); });
}

// Asks, then deletes the selected asset with its folder.
void AssetsPanel::removeSelected()
{
    if (!current_ || !host_.catalog())
        return;
    QMessageBox box(this);
    box.setIcon(QMessageBox::NoIcon);
    box.setWindowTitle(tr("Remove asset"));
    box.setTextFormat(Qt::PlainText);
    box.setText(tr("Remove %1 and its folder from the library? This cannot be undone.").arg(fromCore(current_->name)));
    QPushButton* yes = box.addButton(tr("Remove"), QMessageBox::DestructiveRole);
    QPushButton* no = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    yes->setToolTip(tr("Delete the asset and its folder"));
    no->setToolTip(tr("Keep the asset"));
    box.setDefaultButton(no);
    box.exec();
    if (box.clickedButton() != yes)
        return;
    const plynth::Status status = host_.catalog()->removeAsset(current_->id);
    if (!status.ok())
        emit message(fromCore(status.message));
    current_.reset();
    host_.assetsEdited();
}

// Accepts files and folders dragged onto the tab.
void AssetsPanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls() && host_.hasLibrary() && retryEnabled_)
        event->acceptProposedAction();
    else
        event->ignore();
}

// Hands the dropped files and folders to the window to import.
void AssetsPanel::dropEvent(QDropEvent* event)
{
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    if (paths.isEmpty())
        return;
    event->acceptProposedAction();
    emit filesDropped(paths);
}

}
