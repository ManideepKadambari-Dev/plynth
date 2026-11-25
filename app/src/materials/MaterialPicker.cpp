#include "MaterialPicker.h"

#include "MaterialPreview.h"
#include "catalog/LibraryHost.h"
#include "theme/PlainText.h"
#include "theme/Theme.h"

#include <plynth/core/catalog/MaterialCatalog.h>

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
constexpr int kLookMaterial = 1;
constexpr int kLookClear = 2;
constexpr int kLookNote = 3;

const QString kClearKey = QStringLiteral("*clear");
const QString kUncategorizedKey = QStringLiteral("*uncategorized");

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

// Draws one picker row: a name with a small right hand text and a chevron, or a name over the tile size.
class RowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    // Rows are the theme row height; material rows have a second line.
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override
    {
        const Theme& t = Theme::instance();
        const int height = t.pickerRowHeight() + (index.data(kLookRole).toInt() == kLookMaterial ? t.unit() * 4 : 0);
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
        const bool flipped = parent() != nullptr && parent()->property("flipped").toBool();
        const QRect inner = rect.adjusted(pad, 0, -pad, 0);

        if (look == kLookMaterial) {
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
            int textLeft = inner.left();
            int right = inner.right();
            if (submenu) {
                if (flipped) {
                    drawChevron(p, QPoint(inner.left() + t.unit(), rect.center().y()), false, t.muted());
                    textLeft += t.unit() * 4;
                } else {
                    drawChevron(p, QPoint(inner.right() - t.unit(), rect.center().y()), true, t.muted());
                    right -= t.unit() * 5;
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
MaterialPicker::MaterialPicker(LibraryHost& host, QWidget* parent) : QWidget(parent), host_(host)
{
    setToolTip(tr("Pick a material: group, then material"));
    setFocusPolicy(Qt::NoFocus);
    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(1, 1, 1, 1);
    row_->setSpacing(0);
    preview_ = new MaterialPreview(this);
    row_->addWidget(preview_);
    preview_->hide();
    hide();
}

MaterialPicker::~MaterialPicker() = default;

// Opens with the group column, and with the current material's group open when it is known.
void MaterialPicker::openAt(const QPoint& anchor, const QString& clearText, const QString& currentId)
{
    const Theme& t = Theme::instance();
    reset();
    anchor_ = anchor;
    clearText_ = clearText;
    current_ = currentId;
    const int fullWidth = 2 + 2 * t.pickerColumnWidth() + t.previewWidth();
    const int parentWidth = parentWidget() ? parentWidget()->width() : fullWidth;
    const int onRight = parentWidth - anchor.x();
    flipped_ = onRight < fullWidth && anchor.x() > onRight;
    row_->setDirection(flipped_ ? QBoxLayout::RightToLeft : QBoxLayout::LeftToRight);
    fillGroups(addColumn(Kind::Groups));
    if (!currentId.isEmpty() && host_.materialCatalog()) {
        const std::optional<plynth::MaterialRecord> record = host_.materialCatalog()->material(toCore(currentId));
        if (record)
            openGroup(record->groups.empty() ? kUncategorizedKey : fromCore(record->groups.front()), currentId);
        setHovered(currentId);
    }
    open_ = true;
    relayout();
    show();
    raise();
    qApp->installEventFilter(this);
}

// Closes the picker, forgets the hover and tells the owner.
void MaterialPicker::dismiss()
{
    if (!open_)
        return;
    open_ = false;
    qApp->removeEventFilter(this);
    setHovered(QString());
    hide();
    emit dismissed();
}

// Reports the material as chosen and closes.
void MaterialPicker::choose(const QString& id)
{
    if (!open_)
        return;
    emit materialChosen(id);
    dismiss();
}

// Opens the path to a material the way clicks would, then hovers it.
bool MaterialPicker::hoverMaterial(const QString& id)
{
    plynth::MaterialCatalog* catalog = host_.materialCatalog();
    if (!open_ || !catalog)
        return false;
    const std::optional<plynth::MaterialRecord> record = catalog->material(toCore(id));
    if (!record)
        return false;
    openGroup(record->groups.empty() ? kUncategorizedKey : fromCore(record->groups.front()), id);
    setHovered(id);
    relayout();
    return true;
}

// Watches the whole application while open: Escape closes, a press outside closes, a resize of the parent
// closes, and leaving the material column clears the hover.
bool MaterialPicker::eventFilter(QObject* watched, QEvent* event)
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
            if (column.kind == Kind::Materials && watched == column.list->viewport())
                setHovered(QString());
        break;
    default:
        break;
    }
    return false;
}

// Paints the Panel body and the Line border.
void MaterialPicker::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(t.panel());
    p.setPen(QPen(t.line(), 1));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radius(), t.radius());
}

// Removes every column and the hover.
void MaterialPicker::reset()
{
    trimColumns(0);
    hovered_.clear();
    preview_->clear();
}

// Adds a column at the end, before the preview.
QListWidget* MaterialPicker::addColumn(Kind kind)
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
    list->setProperty("flipped", flipped_);
    list->viewport()->installEventFilter(this);
    if (kind == Kind::Groups) {
        list->setToolTip(tr("Material groups. Pick one to see its materials"));
    } else {
        list->setToolTip(tr("Materials. Click one to use it, hover one to see it"));
    }
    row_->insertWidget(static_cast<int>(columns_.size()), list);
    columns_.push_back({kind, list});
    connect(list, &QListWidget::itemClicked, this, [this, list](QListWidgetItem* item) {
        for (size_t i = 0; i < columns_.size(); ++i)
            if (columns_[i].list == list) {
                rowClicked(static_cast<int>(i), item);
                return;
            }
    });
    if (kind == Kind::Materials)
        connect(list, &QListWidget::itemEntered, this, [this](QListWidgetItem* item) {
            setHovered(item->data(kKeyRole).toString());
        });
    return list;
}

// Deletes the columns from index keep on. The preview hides when the material column goes.
void MaterialPicker::trimColumns(int keep)
{
    while (static_cast<int>(columns_.size()) > keep) {
        const Column column = columns_.back();
        columns_.pop_back();
        if (column.kind == Kind::Materials)
            setHovered(QString());
        row_->removeWidget(column.list);
        column.list->hide();
        column.list->deleteLater();
    }
}

// Lists the clear row, then the groups with their material counts, then Uncategorized when it has materials.
void MaterialPicker::fillGroups(QListWidget* list)
{
    auto* clear = new QListWidgetItem(clearText_, list);
    clear->setData(kKeyRole, kClearKey);
    clear->setData(kLookRole, kLookClear);
    clear->setToolTip(tr("Take the material off this surface"));

    plynth::MaterialCatalog* catalog = host_.materialCatalog();
    if (!catalog) {
        auto* note = new QListWidgetItem(tr("No library"), list);
        note->setData(kLookRole, kLookNote);
        note->setData(kBlockedRole, tr("There is no library"));
        return;
    }
    for (const plynth::MaterialGroupEntry& entry : catalog->groups()) {
        auto* item = new QListWidgetItem(fromCore(entry.name), list);
        item->setData(kKeyRole, fromCore(entry.id));
        item->setData(kSubRole, QString::number(entry.materialCount));
        item->setData(kSubmenuRole, true);
        item->setData(kLookRole, kLookPlain);
        if (entry.materialCount == 0) {
            item->setData(kBlockedRole, tr("No materials in this group yet"));
            item->setToolTip(tr("No materials in this group yet"));
        }
    }
    plynth::MaterialQuery loose;
    loose.uncategorizedOnly = true;
    const size_t looseCount = catalog->search(loose).size();
    if (looseCount > 0) {
        auto* item = new QListWidgetItem(tr("Uncategorized"), list);
        item->setData(kKeyRole, kUncategorizedKey);
        item->setData(kSubRole, QString::number(looseCount));
        item->setData(kSubmenuRole, true);
        item->setData(kLookRole, kLookPlain);
    }
    if (list->count() == 1) {
        auto* note = new QListWidgetItem(tr("No materials yet"), list);
        note->setData(kLookRole, kLookNote);
        note->setData(kBlockedRole, tr("There are no materials in the library yet"));
    }
}

// Lists the materials of a group with their tile sizes. Broken materials are blocked.
void MaterialPicker::fillMaterials(QListWidget* list, const QString& group)
{
    plynth::MaterialCatalog* catalog = host_.materialCatalog();
    plynth::MaterialQuery query;
    if (group == kUncategorizedKey)
        query.uncategorizedOnly = true;
    else
        query.group = toCore(group);
    const std::vector<plynth::MaterialRecord> found = catalog ? catalog->search(query) : std::vector<plynth::MaterialRecord>{};
    for (const plynth::MaterialRecord& record : found) {
        auto* item = new QListWidgetItem(fromCore(record.name), list);
        item->setData(kKeyRole, fromCore(record.id));
        item->setData(kLookRole, kLookMaterial);
        item->setData(kSubRole, MaterialPreview::tileText(record.tileSize));
        if (record.state == plynth::MaterialState::Broken) {
            const QString reason = record.problem.empty() ? tr("This material is broken") : fromCore(record.problem);
            item->setData(kBlockedRole, reason);
            item->setData(kWarnRole, true);
            item->setToolTip(plainToolTip(reason));
        } else {
            item->setToolTip(plainToolTip(tr("Click to use %1").arg(fromCore(record.name))));
        }
    }
    if (found.empty()) {
        auto* note = new QListWidgetItem(tr("No materials"), list);
        note->setData(kLookRole, kLookNote);
        note->setData(kBlockedRole, tr("There are no materials here"));
    }
}

// Handles a click on a row: clears, opens the material column, or chooses the material.
void MaterialPicker::rowClicked(int column, QListWidgetItem* item)
{
    if (!item || !item->data(kBlockedRole).toString().isEmpty() || item->data(kLookRole).toInt() == kLookNote)
        return;
    const QString key = item->data(kKeyRole).toString();
    if (key == kClearKey) {
        emit cleared();
        dismiss();
        return;
    }
    if (columns_[static_cast<size_t>(column)].kind == Kind::Materials) {
        emit materialChosen(key);
        dismiss();
        return;
    }
    openGroup(key, QString());
    relayout();
}

// Shows the preview of a material.
void MaterialPicker::setHovered(const QString& id)
{
    if (id == hovered_)
        return;
    hovered_ = id;
    plynth::MaterialCatalog* catalog = host_.materialCatalog();
    std::optional<plynth::MaterialRecord> record;
    if (!id.isEmpty() && catalog)
        record = catalog->material(toCore(id));
    if (!record) {
        hovered_.clear();
        preview_->clear();
        return;
    }
    QStringList names;
    for (const plynth::MaterialGroupEntry& group : catalog->groups())
        if (std::find(record->groups.begin(), record->groups.end(), group.id) != record->groups.end())
            names << fromCore(group.name);
    QString license = fromCore(record->license);
    for (const plynth::LicenseEntry& entry : catalog->licenses())
        if (entry.id == record->license)
            license = fromCore(entry.name);
    preview_->showMaterial(*record, names, license);
}

// Sizes and places the widget: next to the anchor, flipped near the right edge, kept inside the parent.
void MaterialPicker::relayout()
{
    const Theme& t = Theme::instance();
    QWidget* parent = parentWidget();
    const bool hasMaterials = columns_.size() > 1;
    preview_->setVisible(hasMaterials);
    const int previewHeight = t.thumbnailLarge() + t.unit() * 35;
    int rowsHeight = t.pickerRowHeight() * 8;
    for (const Column& column : columns_) {
        int sum = t.unit() * 2;
        for (int i = 0; i < column.list->count(); ++i)
            sum += column.list->sizeHintForRow(i) > 0 ? column.list->sizeHintForRow(i) : t.pickerRowHeight();
        rowsHeight = qMax(rowsHeight, sum);
    }
    const int margin = t.unit() * 2;
    const int wanted = qMax(rowsHeight, hasMaterials ? previewHeight : 0) + 2;
    const int width = 2 + static_cast<int>(columns_.size()) * t.pickerColumnWidth() + (hasMaterials ? t.previewWidth() : 0);
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

// Opens the material column of a group, with the given material's row marked.
void MaterialPicker::openGroup(const QString& group, const QString& material)
{
    trimColumns(1);
    QListWidget* list = addColumn(Kind::Materials);
    fillMaterials(list, group);
    selectRow(0, group);
    for (int i = 0; i < list->count(); ++i)
        if (!material.isEmpty() && list->item(i)->data(kKeyRole).toString() == material) {
            list->setCurrentRow(i);
            list->scrollToItem(list->item(i));
        }
}

// Marks the row with this key as the open one in a column.
void MaterialPicker::selectRow(int column, const QString& key)
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
