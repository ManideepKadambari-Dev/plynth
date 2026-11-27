#include "OpeningAssetPicker.h"

#include "catalog/LibraryHost.h"
#include "furniture/AssetPreview.h"
#include "theme/Theme.h"

#include <plynth/core/OpeningAssets.h>
#include <plynth/core/catalog/AssetCatalog.h>
#include <plynth/core/catalog/CatalogChecks.h>

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QListWidget>
#include <QPainter>
#include <QStyledItemDelegate>

namespace plynth::app {

namespace {

constexpr int kKindRole = Qt::UserRole + 1;
constexpr int kAssetRole = Qt::UserRole + 2;
constexpr int kSubRole = Qt::UserRole + 3;
constexpr int kBlockedRole = Qt::UserRole + 4;

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Draws one row: the name on the left and a small mono text on the right, on Raised when hovered or marked.
class RowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    // Rows are the theme row height.
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        const Theme& t = Theme::instance();
        return QSize(t.pickerColumnWidth(), t.pickerRowHeight());
    }

    // Paints the row. A row that cannot be chosen is muted and its note amber.
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const Theme& t = Theme::instance();
        const bool blocked = index.data(kBlockedRole).toBool();
        const bool hot = !blocked && (option.state & (QStyle::State_MouseOver | QStyle::State_Selected));
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        const QRect rect = option.rect.adjusted(t.unit() / 2, 1, -t.unit() / 2, -1);
        if (hot) {
            p->setPen(Qt::NoPen);
            p->setBrush(t.raised());
            p->drawRoundedRect(rect, t.radius(), t.radius());
        }
        const int pad = t.unit() * 3;
        QRect inner = rect.adjusted(pad, 0, -pad, 0);
        const QString sub = index.data(kSubRole).toString();
        p->setFont(t.monoFont(t.sizeSmall()));
        if (!sub.isEmpty()) {
            const int width = p->fontMetrics().horizontalAdvance(sub);
            p->setPen(blocked ? t.signalAmber() : t.muted());
            p->drawText(QRect(inner.right() - width, rect.top(), width, rect.height()), Qt::AlignRight | Qt::AlignVCenter, sub);
            inner.setRight(inner.right() - width - t.unit() * 2);
        }
        p->setPen(blocked ? t.muted() : t.text());
        p->setFont(t.uiFont(t.sizeNormal()));
        p->drawText(inner, Qt::AlignLeft | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, inner.width()));
        p->restore();
    }
};

}

// Builds the list and the preview. Nothing shows until openAt.
OpeningAssetPicker::OpeningAssetPicker(LibraryHost& host, QWidget* parent) : QWidget(parent), host_(host)
{
    const Theme& t = Theme::instance();
    setFocusPolicy(Qt::NoFocus);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(1, 1, 1, 1);
    row->setSpacing(0);
    list_ = new QListWidget(this);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setFixedWidth(t.pickerColumnWidth());
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; border-right: 1px solid %1; "
                                        "padding: %2px 0px; outline: none; }")
                             .arg(t.line().name())
                             .arg(t.unit()));
    list_->setItemDelegate(new RowDelegate(list_));
    list_->setMouseTracking(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setToolTip(tr("Automatic lets the build pick a fitting asset. Hover an asset to see it, click to use it."));
    list_->viewport()->installEventFilter(this);
    row->addWidget(list_);
    preview_ = new AssetPreview(this);
    row->addWidget(preview_);
    connect(list_, &QListWidget::itemClicked, this, &OpeningAssetPicker::rowClicked);
    connect(list_, &QListWidget::itemEntered, this, [this](QListWidgetItem* item) {
        setHovered(item->data(kAssetRole).toString());
    });
    hide();
}

OpeningAssetPicker::~OpeningAssetPicker() = default;

// The category name of the assets a door or a window can take.
QString OpeningAssetPicker::categoryFor(plynth::OpeningType type)
{
    const std::string_view category = type == plynth::OpeningType::Window ? plynth::kWindowCategory : plynth::kDoorCategory;
    return QString::fromUtf8(category.data(), static_cast<qsizetype>(category.size()));
}

// Fills the list, marks the current choice and shows the picker beside the anchor.
void OpeningAssetPicker::openAt(const QPoint& anchor, plynth::OpeningType type, OpeningFrameKind current,
                                const QString& currentAsset)
{
    anchor_ = anchor;
    hovered_.clear();
    preview_->clear();
    fill(type, current, currentAsset);
    open_ = true;
    relayout();
    show();
    raise();
    qApp->installEventFilter(this);
}

// Lists the two fixed choices, then the assets of the category with their sizes. An asset that core's frame check
// refuses, as "plynth opening set --frame" would, is listed but cannot be chosen, with the reason as its tip.
void OpeningAssetPicker::fill(plynth::OpeningType type, OpeningFrameKind current, const QString& currentAsset)
{
    list_->clear();
    const auto add = [this](const QString& name, OpeningFrameKind kind, const QString& asset, const QString& sub,
                            bool blocked) {
        auto* item = new QListWidgetItem(name, list_);
        item->setData(kKindRole, static_cast<int>(kind));
        item->setData(kAssetRole, asset);
        item->setData(kSubRole, sub);
        item->setData(kBlockedRole, blocked);
        return item;
    };
    QListWidgetItem* marked = nullptr;
    QListWidgetItem* automatic = add(tr("Automatic"), OpeningFrameKind::Automatic, QString(), tr("fits"), false);
    automatic->setToolTip(tr("The build picks a catalog asset that needs at most 10 percent stretch, or builds its own"));
    QListWidgetItem* builtIn = add(tr("Built by Plynth"), OpeningFrameKind::BuiltIn, QString(), QString(), false);
    builtIn->setToolTip(tr("Always build the plain frame, leaf or panes Plynth makes"));
    if (current == OpeningFrameKind::Automatic)
        marked = automatic;
    if (current == OpeningFrameKind::BuiltIn)
        marked = builtIn;

    plynth::AssetCatalog* catalog = host_.catalog();
    std::vector<plynth::AssetRecord> assets;
    if (catalog != nullptr) {
        plynth::AssetQuery query;
        query.category = categoryFor(type).toStdString();
        assets = catalog->search(query);
    }
    for (const plynth::AssetRecord& record : assets) {
        const plynth::Status check = plynth::checkFrameAsset(*catalog, record.id, type);
        const bool usable = check.ok();
        const QString sub = usable ? QStringLiteral("%1 x %2").arg(record.size.width, 0, 'f', 2).arg(record.size.height, 0, 'f', 2)
                                   : record.state == plynth::AssetState::Broken ? tr("broken")
                                                                                 : tr("not measured");
        QListWidgetItem* item = add(fromCore(record.name), OpeningFrameKind::Asset, fromCore(record.id), sub, !usable);
        item->setToolTip(usable ? fromCore(record.id) : fromCore(check.message));
        if (current == OpeningFrameKind::Asset && fromCore(record.id) == currentAsset)
            marked = item;
    }
    if (current == OpeningFrameKind::Asset && marked == nullptr && !currentAsset.isEmpty()) {
        marked = add(currentAsset, OpeningFrameKind::Asset, currentAsset, tr("not in the library"), true);
        marked->setToolTip(tr("The asset this opening names is not in the library. The build will use its own fill."));
    }
    if (assets.empty()) {
        QListWidgetItem* note = add(type == plynth::OpeningType::Window ? tr("No window assets") : tr("No door assets"),
                                    OpeningFrameKind::BuiltIn, QString(), QString(), true);
        note->setToolTip(tr("Give an asset the category %1 in the Assets window to list it here").arg(categoryFor(type)));
    }
    if (marked != nullptr)
        list_->setCurrentItem(marked);
}

// Closes the picker, forgets the hover and tells the owner.
void OpeningAssetPicker::dismiss()
{
    if (!open_)
        return;
    open_ = false;
    qApp->removeEventFilter(this);
    setHovered(QString());
    hide();
    emit dismissed();
}

// Hovers the row of an asset.
bool OpeningAssetPicker::hoverAsset(const QString& id)
{
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(kAssetRole).toString() == id && !id.isEmpty()) {
            list_->scrollToItem(list_->item(i));
            setHovered(id);
            return true;
        }
    return false;
}

// Reports the clicked row as the choice and closes.
void OpeningAssetPicker::rowClicked(QListWidgetItem* item)
{
    if (item == nullptr || item->data(kBlockedRole).toBool())
        return;
    const auto kind = static_cast<OpeningFrameKind>(item->data(kKindRole).toInt());
    const QString asset = item->data(kAssetRole).toString();
    dismiss();
    emit chosen(kind, asset);
}

// Shows an asset in the preview, or the hint when there is none.
void OpeningAssetPicker::setHovered(const QString& id)
{
    if (id == hovered_)
        return;
    hovered_ = id;
    plynth::AssetCatalog* catalog = host_.catalog();
    std::optional<plynth::AssetRecord> record;
    if (!id.isEmpty() && catalog != nullptr)
        record = catalog->asset(id.toStdString());
    if (!record) {
        hovered_.clear();
        preview_->clear();
        return;
    }
    QStringList names;
    for (const plynth::CategoryEntry& entry : catalog->categories())
        for (const std::string& category : record->categories)
            if (entry.id == category)
                names << fromCore(entry.name);
    QString license = fromCore(record->license);
    for (const plynth::LicenseEntry& entry : catalog->licenses())
        if (entry.id == record->license)
            license = fromCore(entry.name);
    preview_->showAsset(*record, names, license);
}

// Watches the whole application while open: Escape closes, a press outside closes, a resize of the parent closes.
bool OpeningAssetPicker::eventFilter(QObject* watched, QEvent* event)
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
        if (watched == list_->viewport())
            setHovered(QString());
        break;
    default:
        break;
    }
    return false;
}

// Paints the Panel body and the Line border.
void OpeningAssetPicker::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(t.panel());
    p.setPen(QPen(t.line(), 1));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radius(), t.radius());
}

// Sizes the picker and places it to the left of the anchor, kept inside the parent.
void OpeningAssetPicker::relayout()
{
    const Theme& t = Theme::instance();
    QWidget* parent = parentWidget();
    int rows = t.unit() * 2;
    for (int i = 0; i < list_->count(); ++i)
        rows += t.pickerRowHeight();
    const int margin = t.unit() * 2;
    const int wanted = qMax(qMax(rows, t.pickerRowHeight() * 6), t.thumbnailLarge() + t.unit() * 35) + 2;
    const int width = 2 + t.pickerColumnWidth() + t.previewWidth();
    const int available = parent ? parent->height() - margin * 2 : wanted;
    const int height = qMin(wanted, qMax(available, t.pickerRowHeight() * 4));
    setFixedSize(width, height);
    int x = anchor_.x() - width - t.unit() * 2;
    int y = anchor_.y();
    if (parent) {
        x = qBound(margin, x, qMax(margin, parent->width() - width - margin));
        y = qBound(margin, y, qMax(margin, parent->height() - height - margin));
    }
    move(x, y);
}

}
