#include "FurniturePicker.h"

#include "AssetPreview.h"
#include "GeneratorPreview.h"
#include "catalog/LibraryHost.h"
#include "theme/Theme.h"

#include <plynth/core/Generators.h>
#include <plynth/core/RoomTypes.h>
#include <plynth/core/catalog/AssetCatalog.h>
#include <plynth/core/catalog/ItemAssets.h>

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QListWidget>
#include <QPainter>
#include <QScrollBar>
#include <QStyledItemDelegate>

#include <algorithm>

namespace plynth::app {

namespace {

constexpr int kKeyRole = Qt::UserRole + 1;
constexpr int kSubRole = Qt::UserRole + 2;
constexpr int kLookRole = Qt::UserRole + 3;
constexpr int kBlockedRole = Qt::UserRole + 4;
constexpr int kWarnRole = Qt::UserRole + 5;
constexpr int kSubmenuRole = Qt::UserRole + 6;

constexpr int kLookPlain = 0;
constexpr int kLookAsset = 1;
constexpr int kLookBack = 2;
constexpr int kLookNote = 3;

const QString kAllRoomTypes = QStringLiteral("*all");
const QString kGeneratorsKey = QStringLiteral("*generators");
const QString kGeneratorPrefix = QStringLiteral("generator:");

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

// Draws one picker row: name, a small right hand text and a chevron, or name and size on two lines.
class RowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    // Rows are the theme row height; asset rows have a second line.
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override
    {
        const Theme& t = Theme::instance();
        const int height = t.pickerRowHeight() + (index.data(kLookRole).toInt() == kLookAsset ? t.unit() * 4 : 0);
        return QSize(t.pickerColumnWidth(), height);
    }

    // Paints the row on Raised when the mouse is over it or it is on the open path.
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const Theme& t = Theme::instance();
        const int look = index.data(kLookRole).toInt();
        const bool blocked = !index.data(kBlockedRole).toString().isEmpty();
        const bool hot = look != kLookNote && !blocked && (option.state & (QStyle::State_MouseOver | QStyle::State_Selected));
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        const QRect rect = option.rect.adjusted(t.unit() / 2, 1, -t.unit() / 2, -1);
        if (hot) {
            p->setPen(Qt::NoPen);
            p->setBrush(t.raised());
            p->drawRoundedRect(rect, t.radius(), t.radius());
        }
        const int pad = t.unit() * 3;
        const QColor nameColor = (blocked || look == kLookNote) ? t.muted() : t.text();
        const QString name = index.data(Qt::DisplayRole).toString();
        const QString sub = index.data(kSubRole).toString();
        const QColor subColor = index.data(kWarnRole).toBool() ? t.signalAmber() : t.muted();
        const bool submenu = index.data(kSubmenuRole).toBool();
        const int chevronSpace = submenu ? t.unit() * 5 : 0;
        const bool flipped = parent() != nullptr && parent()->property("flipped").toBool();
        const QRect inner = rect.adjusted(pad, 0, -pad, 0);

        if (look == kLookAsset) {
            const QRect top(inner.left(), rect.top() + t.unit(), inner.width(), t.unit() * 5);
            p->setPen(nameColor);
            p->setFont(t.uiFont(t.sizeNormal()));
            p->drawText(top, Qt::AlignLeft | Qt::AlignVCenter,
                        p->fontMetrics().elidedText(name, Qt::ElideRight, top.width()));
            p->setPen(subColor);
            p->setFont(t.monoFont(t.sizeSmall()));
            p->drawText(top.translated(0, t.unit() * 5), Qt::AlignLeft | Qt::AlignVCenter,
                        p->fontMetrics().elidedText(sub, Qt::ElideRight, top.width()));
        } else {
            // The chevron points the way the next column opens: right normally, left when the picker is flipped.
            // The back row points the other way.
            const bool chevronLeft = look == kLookBack ? !flipped : flipped;
            const bool showChevron = submenu || look == kLookBack;
            int textLeft = inner.left();
            int right = inner.right();
            if (showChevron) {
                if (chevronLeft) {
                    drawChevron(p, QPoint(inner.left() + t.unit(), rect.center().y()), false,
                                blocked ? t.line() : t.muted());
                    textLeft += t.unit() * 4;
                } else {
                    drawChevron(p, QPoint(inner.right() - t.unit(), rect.center().y()), true,
                                blocked ? t.line() : t.muted());
                    right -= chevronSpace > 0 ? chevronSpace : t.unit() * 5;
                }
            }
            p->setFont(t.monoFont(t.sizeSmall()));
            const int subWidth = sub.isEmpty() ? 0 : p->fontMetrics().horizontalAdvance(sub);
            if (!sub.isEmpty()) {
                p->setPen(subColor);
                p->drawText(QRect(right - subWidth, rect.top(), subWidth, rect.height()),
                            Qt::AlignRight | Qt::AlignVCenter, sub);
                right -= subWidth + t.unit() * 2;
            }
            const QRect nameRect(textLeft, rect.top(), qMax(0, right - textLeft), rect.height());
            p->setPen(nameColor);
            p->setFont(t.uiFont(t.sizeNormal()));
            p->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                        p->fontMetrics().elidedText(name, Qt::ElideRight, nameRect.width()));
        }
        p->restore();
    }

private:
    // A small chevron made of two lines, pointing right or left.
    static void drawChevron(QPainter* p, const QPoint& centre, bool right, const QColor& color)
    {
        const int h = Theme::instance().unit();
        const int dx = right ? -h / 2 : h / 2;
        p->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        p->drawLine(QPoint(centre.x() + dx, centre.y() - h), QPoint(centre.x() - dx, centre.y()));
        p->drawLine(QPoint(centre.x() - dx, centre.y()), QPoint(centre.x() + dx, centre.y() + h));
    }
};

}

// Builds the frame and the preview panel. Nothing shows until openAt.
FurniturePicker::FurniturePicker(LibraryHost& host, QWidget* parent) : QWidget(parent), host_(host)
{
    setToolTip(tr("Pick what to place: room type, then category, then asset, or one of the generators"));
    setProperty("tourId", QStringLiteral("furniture-picker"));
    setFocusPolicy(Qt::NoFocus);
    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(1, 1, 1, 1);
    row_->setSpacing(0);
    preview_ = new AssetPreview(this);
    row_->addWidget(preview_);
    preview_->hide();
    generatorPreview_ = new GeneratorPreview(this);
    row_->addWidget(generatorPreview_);
    generatorPreview_->hide();
    connect(&host_, &LibraryHost::generatorsChanged, this, &FurniturePicker::refreshGenerators);
    hide();
}

FurniturePicker::~FurniturePicker() = default;

// Opens with the first column for the room type, or for all room types when none is given.
void FurniturePicker::openAt(const QPoint& anchor, const QString& roomType)
{
    const Theme& t = Theme::instance();
    reset();
    anchor_ = anchor;
    roomType_ = roomType;
    filtered_ = !roomType.isEmpty();
    const int maxColumns = filtered_ ? 2 : 3;
    const int fullWidth = 2 + maxColumns * t.pickerColumnWidth() + t.previewWidth();
    const int parentWidth = parentWidget() ? parentWidget()->width() : fullWidth;
    const int onRight = parentWidth - anchor.x();
    flipped_ = onRight < fullWidth && anchor.x() > onRight;
    row_->setDirection(flipped_ ? QBoxLayout::RightToLeft : QBoxLayout::LeftToRight);

    if (filtered_) {
        QListWidget* list = addColumn(Kind::Categories, roomType);
        fillCategories(list, roomType, true);
    } else {
        QListWidget* list = addColumn(Kind::RoomTypes, QString());
        fillRoomTypes(list);
    }
    open_ = true;
    relayout();
    show();
    raise();
    qApp->installEventFilter(this);
}

// Closes the picker, forgets the hover and tells the owner.
void FurniturePicker::dismiss()
{
    if (!open_)
        return;
    open_ = false;
    qApp->removeEventFilter(this);
    setHovered(QString());
    const bool hadFocus = hasFocus() || isAncestorOf(QApplication::focusWidget());
    hide();
    if (hadFocus && parentWidget() != nullptr)
        parentWidget()->setFocus(Qt::OtherFocusReason);
    emit dismissed();
}

// Opens the path to an asset the way clicks would, then hovers it.
bool FurniturePicker::hoverAsset(const QString& id)
{
    if (id.startsWith(kGeneratorPrefix))
        return hoverGenerator(id.mid(kGeneratorPrefix.size()));
    plynth::AssetCatalog* catalog = host_.catalog();
    if (!open_ || !catalog)
        return false;
    const std::optional<plynth::AssetRecord> record = catalog->asset(toCore(id));
    if (!record || record->categories.empty())
        return false;
    if (filtered_) {
        const std::vector<plynth::CategoryEntry> own = plynth::placingCategories(*catalog, toCore(roomType_));
        for (const std::string& category : record->categories) {
            const bool found = std::any_of(own.begin(), own.end(), [&](const plynth::CategoryEntry& c) { return c.id == category; });
            if (found) {
                openPath(roomType_, fromCore(category), id, false);
                return true;
            }
        }
    }
    for (const std::string& category : record->categories) {
        for (const plynth::RoomTypeEntry& type : catalog->roomTypes()) {
            const std::vector<plynth::CategoryEntry> own = catalog->categoriesFor(type.id);
            const bool found = std::any_of(own.begin(), own.end(), [&](const plynth::CategoryEntry& c) { return c.id == category; });
            if (found) {
                openPath(fromCore(type.id), fromCore(category), id, true);
                return true;
            }
        }
    }
    openPath(QStringLiteral("other"), fromCore(record->categories.front()), id, true);
    return true;
}

// Lists the generators again after the library or the add-on check changed while the picker is open.
void FurniturePicker::refreshGenerators()
{
    if (!open_)
        return;
    const QString keep = hoveredGenerator_;
    for (const Column& column : columns_) {
        if (column.kind == Kind::Generators) {
            hoveredGenerator_.clear();
            column.list->clear();
            fillGenerators(column.list);
            relayout();
            setHoveredGenerator(keep);
        } else if (column.kind != Kind::Assets && column.list->count() > 0) {
            // The count on the Generators row of the first column.
            for (int i = 0; i < column.list->count(); ++i)
                if (column.list->item(i)->data(kKeyRole).toString() == kGeneratorsKey)
                    column.list->item(i)->setData(kSubRole, QString::number(host_.generators().generators.size()));
        }
    }
}

// Opens the path to a generator the way clicks would, then hovers it.
bool FurniturePicker::hoverGenerator(const QString& id)
{
    if (!open_ || host_.generators().find(toCore(id)) == nullptr)
        return false;
    trimColumns(0);
    if (filtered_)
        fillCategories(addColumn(Kind::Categories, roomType_), roomType_, true);
    else
        fillRoomTypes(addColumn(Kind::RoomTypes, QString()));
    selectRow(0, kGeneratorsKey);
    QListWidget* list = addColumn(Kind::Generators, kGeneratorsKey);
    fillGenerators(list);
    relayout();
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(kKeyRole).toString() == id) {
            list->setCurrentRow(i);
            list->scrollToItem(list->item(i));
        }
    setHoveredGenerator(id);
    return true;
}

// Watches the whole application while open: Escape closes, a press outside closes, a resize of the
// parent closes, and leaving the asset column clears the hover.
bool FurniturePicker::eventFilter(QObject* watched, QEvent* event)
{
    if (!open_)
        return false;
    switch (event->type()) {
    case QEvent::KeyPress:
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            dismiss();
            return true;
        }
        break;
    case QEvent::MouseButtonPress: {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && widget != this && !isAncestorOf(widget) && widget->window() == window())
            dismiss();
        break;
    }
    case QEvent::Resize:
        if (watched == parentWidget() || watched == window())
            dismiss();
        break;
    case QEvent::Leave:
        for (const Column& column : columns_)
            if (column.kind == Kind::Assets && watched == column.list->viewport())
                setHovered(QString());
            else if (column.kind == Kind::Generators && watched == column.list->viewport())
                setHoveredGenerator(QString());
        break;
    default:
        break;
    }
    return false;
}

// Paints the Panel body and the Line border.
void FurniturePicker::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(t.panel());
    p.setPen(QPen(t.line(), 1));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radius(), t.radius());
}

// Removes every column and the hover.
void FurniturePicker::reset()
{
    trimColumns(0);
    hovered_.clear();
    hoveredGenerator_.clear();
    preview_->clear();
    generatorPreview_->clear();
}

// Adds a column at the end, before the preview. The key is the room type of a categories column.
QListWidget* FurniturePicker::addColumn(Kind kind, const QString& key)
{
    const Theme& t = Theme::instance();
    auto* list = new QListWidget(this);
    list->setFrameShape(QFrame::NoFrame);
    list->setFixedWidth(t.pickerColumnWidth());
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->verticalScrollBar()->setStyleSheet(t.scrollBarStyleSheet());
    list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; border-right: 1px solid %1; "
                                       "padding: %2px 0px; outline: none; }")
                            .arg(t.line().name())
                            .arg(t.unit()));
    list->setItemDelegate(new RowDelegate(list));
    list->setMouseTracking(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setProperty("roomType", key);
    list->setProperty("flipped", flipped_);
    list->viewport()->installEventFilter(this);
    switch (kind) {
    case Kind::RoomTypes:
        list->setToolTip(tr("Room types. Pick one to see its categories"));
        list->setProperty("tourId", QStringLiteral("picker-room-types"));
        break;
    case Kind::Categories:
        list->setToolTip(tr("Categories. Pick one to see its assets"));
        list->setProperty("tourId", QStringLiteral("picker-categories"));
        break;
    case Kind::Assets:
        list->setToolTip(tr("Assets. Click one to place it, hover one to see it"));
        list->setProperty("tourId", QStringLiteral("picker-assets"));
        break;
    case Kind::Generators:
        list->setToolTip(tr("Generators. Click one to place it, hover one to see its parameters"));
        list->setProperty("tourId", QStringLiteral("picker-generators"));
        break;
    }
    const int index = static_cast<int>(columns_.size());
    row_->insertWidget(index, list);
    columns_.push_back({kind, list});
    if (open_)
        list->show();
    connect(list, &QListWidget::itemClicked, this, [this, list](QListWidgetItem* item) {
        for (size_t i = 0; i < columns_.size(); ++i)
            if (columns_[i].list == list) {
                rowClicked(static_cast<int>(i), item);
                return;
            }
    });
    if (kind == Kind::Assets)
        connect(list, &QListWidget::itemEntered, this, [this](QListWidgetItem* item) {
            setHovered(item->data(kKeyRole).toString());
        });
    if (kind == Kind::Generators)
        connect(list, &QListWidget::itemEntered, this, [this](QListWidgetItem* item) {
            setHoveredGenerator(item->data(kKeyRole).toString());
        });
    return list;
}

// Deletes the columns from index keep on. The preview hides when the asset column goes.
void FurniturePicker::trimColumns(int keep)
{
    while (static_cast<int>(columns_.size()) > keep) {
        const Column column = columns_.back();
        columns_.pop_back();
        if (column.kind == Kind::Assets)
            setHovered(QString());
        if (column.kind == Kind::Generators)
            setHoveredGenerator(QString());
        row_->removeWidget(column.list);
        column.list->hide();
        column.list->deleteLater();
    }
}

// Lists the room types in picker order.
void FurniturePicker::fillRoomTypes(QListWidget* list)
{
    for (const plynth::RoomType& type : plynth::roomTypes()) {
        auto* item = new QListWidgetItem(fromCore(type.name), list);
        item->setData(kKeyRole, fromCore(type.id));
        item->setData(kSubmenuRole, true);
        item->setData(kLookRole, kLookPlain);
    }
    addGeneratorsRow(list);
}

// Adds the last row of the first column, which opens the generators.
void FurniturePicker::addGeneratorsRow(QListWidget* list)
{
    const std::size_t count = host_.generators().generators.size();
    auto* item = new QListWidgetItem(tr("Generators"), list);
    item->setData(kKeyRole, kGeneratorsKey);
    item->setData(kSubmenuRole, true);
    item->setData(kLookRole, kLookPlain);
    item->setData(kSubRole, QString::number(count));
    if (count == 0) {
        item->setData(kBlockedRole, tr("No generators in the library yet"));
        item->setToolTip(tr("No generators in the library yet. Add the sample generators from the Catalog menu"));
    } else {
        item->setToolTip(tr("Shapes made by a script when the project is built, such as columns and railings"));
    }
}

// Lists the categories for a room type, with the asset count in mono. Empty categories are blocked.
void FurniturePicker::fillCategories(QListWidget* list, const QString& roomType, bool withAllRow)
{
    plynth::AssetCatalog* catalog = host_.catalog();
    if (withAllRow) {
        auto* back = new QListWidgetItem(tr("All room types"), list);
        back->setData(kKeyRole, kAllRoomTypes);
        back->setData(kLookRole, kLookBack);
        back->setToolTip(tr("Go back and pick another room type"));
    }
    const std::vector<plynth::CategoryEntry> entries =
        catalog ? plynth::placingCategories(*catalog, toCore(roomType)) : std::vector<plynth::CategoryEntry>{};
    for (const plynth::CategoryEntry& entry : entries) {
        auto* item = new QListWidgetItem(fromCore(entry.name), list);
        item->setData(kKeyRole, fromCore(entry.id));
        item->setData(kSubRole, QString::number(entry.assetCount));
        item->setData(kSubmenuRole, true);
        item->setData(kLookRole, kLookPlain);
        if (entry.assetCount == 0) {
            item->setData(kBlockedRole, tr("No assets in this category yet"));
            item->setToolTip(tr("No assets in this category yet"));
        }
    }
    if (entries.empty()) {
        auto* note = new QListWidgetItem(tr("No categories"), list);
        note->setData(kLookRole, kLookNote);
        note->setData(kBlockedRole, tr("There are no categories"));
    }
    addGeneratorsRow(list);
}

// Lists the assets of a category with their real sizes. Broken and unmeasured assets are blocked.
void FurniturePicker::fillAssets(QListWidget* list, const QString& category)
{
    plynth::AssetCatalog* catalog = host_.catalog();
    plynth::AssetQuery query;
    query.category = toCore(category);
    const std::vector<plynth::AssetRecord> found = catalog ? catalog->search(query) : std::vector<plynth::AssetRecord>{};
    for (const plynth::AssetRecord& record : found) {
        auto* item = new QListWidgetItem(fromCore(record.name), list);
        item->setData(kKeyRole, fromCore(record.id));
        item->setData(kLookRole, kLookAsset);
        item->setData(kSubRole, AssetPreview::sizeText(record.size));
        QString reason;
        if (record.state == plynth::AssetState::Broken)
            reason = record.problem.empty() ? tr("This asset is broken") : fromCore(record.problem);
        else if (!plynth::isMeasured(record.size))
            reason = tr("Not measured yet. Use Retry render in the Assets tab to measure it");
        if (!reason.isEmpty()) {
            item->setData(kBlockedRole, reason);
            item->setData(kWarnRole, record.state == plynth::AssetState::Broken);
            item->setToolTip(reason);
        } else {
            item->setToolTip(tr("Click to place %1").arg(fromCore(record.name)));
        }
    }
    if (found.empty()) {
        auto* note = new QListWidgetItem(tr("No assets"), list);
        note->setData(kLookRole, kLookNote);
        note->setData(kBlockedRole, tr("There are no assets here"));
    }
}

// Lists the generators with their default sizes in mono. One whose add-on is known to be off is blocked and
// says why in its tooltip.
void FurniturePicker::fillGenerators(QListWidget* list)
{
    const plynth::GeneratorLibrary& library = host_.generators();
    for (const plynth::GeneratorInfo& info : library.generators) {
        auto* item = new QListWidgetItem(fromCore(info.name), list);
        item->setData(kKeyRole, fromCore(info.id));
        item->setData(kLookRole, kLookAsset);
        const plynth::GeneratorSize size =
            plynth::generatorSize(info, plynth::defaultGeneratorParams(info, ceilingHeight_));
        item->setData(kSubRole, size.status.ok() ? GeneratorPreview::sizeText(size.size) : QString());
        const plynth::GeneratorState state = plynth::generatorState(info, host_.addonCache());
        if (state.availability == plynth::GeneratorAvailability::NeedsAddon) {
            item->setData(kBlockedRole, fromCore(state.message));
            item->setToolTip(fromCore(state.message));
        } else if (state.availability == plynth::GeneratorAvailability::Unknown) {
            item->setToolTip(tr("Click to place %1. %2").arg(fromCore(info.name), fromCore(state.message)));
        } else {
            item->setToolTip(tr("Click to place %1").arg(fromCore(info.name)));
        }
    }
    if (library.generators.empty()) {
        auto* note = new QListWidgetItem(tr("No generators"), list);
        note->setData(kLookRole, kLookNote);
        note->setData(kBlockedRole, tr("There are no generators here"));
    }
}

// Handles a click on a row: opens the next column, goes back to the room types, or chooses the asset.
void FurniturePicker::rowClicked(int column, QListWidgetItem* item)
{
    if (!item || !item->data(kBlockedRole).toString().isEmpty() || item->data(kLookRole).toInt() == kLookNote)
        return;
    const QString key = item->data(kKeyRole).toString();
    const Kind kind = columns_[static_cast<size_t>(column)].kind;
    if (kind == Kind::Assets || kind == Kind::Generators) {
        if (kind == Kind::Assets)
            emit assetChosen(key);
        else
            emit generatorChosen(key);
        dismiss();
        return;
    }
    if (key == kGeneratorsKey) {
        trimColumns(column + 1);
        fillGenerators(addColumn(Kind::Generators, key));
        selectRow(column, key);
    } else if (kind == Kind::RoomTypes) {
        trimColumns(column + 1);
        fillCategories(addColumn(Kind::Categories, key), key, false);
        selectRow(column, key);
    } else if (key == kAllRoomTypes) {
        const QString keep = roomType_;
        trimColumns(0);
        filtered_ = false;
        fillRoomTypes(addColumn(Kind::RoomTypes, QString()));
        fillCategories(addColumn(Kind::Categories, keep), keep, false);
        selectRow(0, keep);
    } else {
        trimColumns(column + 1);
        fillAssets(addColumn(Kind::Assets, key), key);
        selectRow(column, key);
    }
    relayout();
}

// Shows the preview of an asset, and tells the owner when it is one that can be placed.
void FurniturePicker::setHovered(const QString& id)
{
    if (id == hovered_)
        return;
    hovered_ = id;
    plynth::AssetCatalog* catalog = host_.catalog();
    std::optional<plynth::AssetRecord> record;
    if (!id.isEmpty() && catalog)
        record = catalog->asset(toCore(id));
    if (!record) {
        hovered_.clear();
        preview_->clear();
        emit assetHovered(QString());
        return;
    }
    QStringList names;
    for (const std::string& category : record->categories)
        names << categoryName(fromCore(category));
    QString license = fromCore(record->license);
    for (const plynth::LicenseEntry& entry : catalog->licenses())
        if (entry.id == record->license)
            license = fromCore(entry.name);
    preview_->showAsset(*record, names, license);
    const bool placeable = record->state != plynth::AssetState::Broken && plynth::isMeasured(record->size);
    emit assetHovered(placeable ? id : QString());
}

// Shows the preview of a generator, and tells the owner when it is one that can be placed.
void FurniturePicker::setHoveredGenerator(const QString& id)
{
    if (id == hoveredGenerator_)
        return;
    hoveredGenerator_ = id;
    const plynth::GeneratorInfo* info = id.isEmpty() ? nullptr : host_.generators().find(toCore(id));
    if (info == nullptr) {
        hoveredGenerator_.clear();
        generatorPreview_->clear();
        emit generatorHovered(QString());
        return;
    }
    generatorPreview_->showGenerator(*info, host_.addonCache(), ceilingHeight_);
    const bool placeable =
        plynth::generatorState(*info, host_.addonCache()).availability != plynth::GeneratorAvailability::NeedsAddon;
    emit generatorHovered(placeable ? id : QString());
}

// The readable name of a category id.
QString FurniturePicker::categoryName(const QString& id) const
{
    if (host_.catalog())
        for (const plynth::CategoryEntry& entry : host_.catalog()->categories())
            if (entry.id == toCore(id))
                return fromCore(entry.name);
    return id;
}

// Sizes and places the widget: next to the anchor, flipped near the right edge, kept inside the parent.
void FurniturePicker::relayout()
{
    const Theme& t = Theme::instance();
    QWidget* parent = parentWidget();
    const bool hasAssets = !columns_.empty() && columns_.back().kind == Kind::Assets;
    const bool hasGenerators = !columns_.empty() && columns_.back().kind == Kind::Generators;
    preview_->setVisible(hasAssets);
    generatorPreview_->setVisible(hasGenerators);
    const bool hasPreview = hasAssets || hasGenerators;
    const int previewHeight = t.thumbnailLarge() + t.unit() * 45;
    int rowsHeight = t.pickerRowHeight() * 8;
    for (const Column& column : columns_) {
        int sum = t.unit() * 2;
        for (int i = 0; i < column.list->count(); ++i)
            sum += column.list->sizeHintForRow(i) > 0 ? column.list->sizeHintForRow(i) : t.pickerRowHeight();
        rowsHeight = qMax(rowsHeight, sum);
    }
    const int margin = t.unit() * 2;
    const int wanted = qMax(rowsHeight, hasPreview ? previewHeight : 0) + 2;
    const int width = 2 + static_cast<int>(columns_.size()) * t.pickerColumnWidth() + (hasPreview ? t.previewWidth() : 0);
    const int available = parent ? parent->height() - margin * 2 : wanted;
    const int height = qMin(wanted, qMax(available, t.pickerRowHeight() * 4));
    setFixedSize(width, height);

    const int gap = t.unit() * 2;
    int x = flipped_ ? anchor_.x() - width - gap : anchor_.x() + gap;
    int y = anchor_.y();
    if (parent) {
        x = qBound(margin, x, qMax(margin, parent->width() - width - margin));
        y = qBound(margin, y, qMax(margin, parent->height() - height - margin));
    }
    move(x, y);
}

// Opens the path to an asset: room type, category and asset columns with the rows selected.
void FurniturePicker::openPath(const QString& roomType, const QString& category, const QString& asset, bool browse)
{
    trimColumns(0);
    if (browse) {
        filtered_ = false;
        fillRoomTypes(addColumn(Kind::RoomTypes, QString()));
        fillCategories(addColumn(Kind::Categories, roomType), roomType, false);
        selectRow(0, roomType);
        selectRow(1, category);
    } else {
        fillCategories(addColumn(Kind::Categories, roomType), roomType, true);
        selectRow(0, category);
    }
    QListWidget* assets = addColumn(Kind::Assets, category);
    fillAssets(assets, category);
    relayout();
    for (int i = 0; i < assets->count(); ++i) {
        if (assets->item(i)->data(kKeyRole).toString() == asset) {
            assets->setCurrentRow(i);
            assets->scrollToItem(assets->item(i));
        }
    }
    setHovered(asset);
}

// Marks the row with this key as the open one in a column.
void FurniturePicker::selectRow(int column, const QString& key)
{
    QListWidget* list = columns_[static_cast<size_t>(column)].list;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(kKeyRole).toString() == key) {
            list->setCurrentRow(i);
            list->scrollToItem(list->item(i));
            return;
        }
}

}

namespace plynth::app {

// True when the preview panel has its model drawn or none is wanted.
bool FurniturePicker::previewReady() const
{
    return preview_ == nullptr || preview_->isHidden() || preview_->pictureReady();
}

}
