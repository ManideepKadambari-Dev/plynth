#include "RoomTypesDialog.h"

#include "catalog/LibraryHost.h"
#include "theme/Theme.h"

#include <plynth/core/catalog/AssetInfo.h>

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace plynth::app {

namespace {

constexpr int kIdRole = Qt::UserRole + 1;

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Makes a heading label.
QLabel* heading(const QString& text, QWidget* parent)
{
    const Theme& t = Theme::instance();
    auto* label = new QLabel(text, parent);
    label->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    return label;
}

}

// Builds the room type list, its category links and the category list.
RoomTypesDialog::RoomTypesDialog(LibraryHost& host, QWidget* parent) : QDialog(parent), host_(host)
{
    const Theme& t = Theme::instance();
    setWindowTitle(tr("Room types and categories"));
    setProperty("tourId", QStringLiteral("room-types-dialog"));
    setToolTip(tr("Edit the room types and the categories of the assets catalog"));
    resize(t.unit() * 160, t.unit() * 130);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(t.dialogMargin(), t.dialogMargin(), t.dialogMargin(), t.dialogMargin());
    outer->setSpacing(t.unit() * 3);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(t.dialogMargin());
    grid->setVerticalSpacing(t.unit() * 2);
    grid->addWidget(heading(tr("Room types"), this), 0, 0);
    grid->addWidget(heading(tr("Categories"), this), 0, 1);

    rooms_ = new QListWidget(this);
    rooms_->setStyle(t.controlStyle());
    rooms_->setToolTip(tr("The kinds of room, in the order the room picker shows them"));
    rooms_->setProperty("tourId", QStringLiteral("room-types-list"));
    grid->addWidget(rooms_, 1, 0);
    categories_ = new QListWidget(this);
    categories_->setStyle(t.controlStyle());
    categories_->setToolTip(tr("The categories an asset can have"));
    categories_->setProperty("tourId", QStringLiteral("room-types-categories"));
    grid->addWidget(categories_, 1, 1);

    auto* roomButtons = new QHBoxLayout();
    roomButtons->setSpacing(t.unit() * 2);
    QPushButton* add = addButton(this, tr("Add"), tr("Add a room type"), QStringLiteral("room-types-add"));
    renameRoom_ = addButton(this, tr("Rename"), tr("Change the name of the selected room type"),
                            QStringLiteral("room-types-rename"));
    removeRoom_ = addButton(this, tr("Remove"), tr("Remove the selected room type. Plans that use it keep its id"),
                            QStringLiteral("room-types-remove"));
    up_ = addButton(this, tr("Up"), tr("Move the selected room type up in the picker"), QStringLiteral("room-types-up"));
    down_ = addButton(this, tr("Down"), tr("Move the selected room type down in the picker"),
                      QStringLiteral("room-types-down"));
    for (QPushButton* b : {add, renameRoom_, removeRoom_, up_, down_})
        roomButtons->addWidget(b);
    grid->addLayout(roomButtons, 2, 0);

    auto* categoryButtons = new QHBoxLayout();
    categoryButtons->setSpacing(t.unit() * 2);
    QPushButton* addCat = addButton(this, tr("Add"), tr("Add a category"), QStringLiteral("categories-add"));
    renameCategory_ = addButton(this, tr("Rename"), tr("Change the name of the selected category"),
                                QStringLiteral("categories-rename"));
    removeCategory_ = addButton(this, tr("Remove"), tr("Remove the selected category from every asset and room type"),
                                QStringLiteral("categories-remove"));
    for (QPushButton* b : {addCat, renameCategory_, removeCategory_})
        categoryButtons->addWidget(b);
    categoryButtons->addStretch(1);
    grid->addLayout(categoryButtons, 2, 1);

    linksTitle_ = heading(QString(), this);
    grid->addWidget(linksTitle_, 3, 0, 1, 2);
    links_ = new QListWidget(this);
    links_->setStyle(t.controlStyle());
    links_->setToolTip(tr("Tick the categories that belong in the selected room type"));
    links_->setProperty("tourId", QStringLiteral("room-types-links"));
    grid->addWidget(links_, 4, 0, 1, 2);
    grid->setRowStretch(1, 3);
    grid->setRowStretch(4, 2);
    outer->addLayout(grid, 1);

    problem_ = new QLabel(this);
    problem_->setWordWrap(true);
    problem_->setFont(t.uiFont(t.sizeSmall()));
    problem_->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
    problem_->hide();
    outer->addWidget(problem_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setToolTip(tr("Close this dialog. Changes are already saved"));
    buttons->button(QDialogButtonBox::Close)->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    outer->addWidget(buttons);

    connect(add, &QPushButton::clicked, this, &RoomTypesDialog::addRoom);
    connect(renameRoom_, &QPushButton::clicked, this, &RoomTypesDialog::renameRoom);
    connect(removeRoom_, &QPushButton::clicked, this, &RoomTypesDialog::removeRoom);
    connect(up_, &QPushButton::clicked, this, [this]() { moveRoom(-1); });
    connect(down_, &QPushButton::clicked, this, [this]() { moveRoom(1); });
    connect(addCat, &QPushButton::clicked, this, &RoomTypesDialog::addCategory);
    connect(renameCategory_, &QPushButton::clicked, this, &RoomTypesDialog::renameCategory);
    connect(removeCategory_, &QPushButton::clicked, this, &RoomTypesDialog::removeCategory);
    connect(rooms_, &QListWidget::currentRowChanged, this, [this]() {
        if (filling_)
            return;
        fillLinks();
        updateButtons();
    });
    connect(categories_, &QListWidget::currentRowChanged, this, &RoomTypesDialog::updateButtons);
    connect(links_, &QListWidget::itemChanged, this, &RoomTypesDialog::linksEdited);

    fillRooms(QString());
    fillCategories(QString());
}

// Makes one dialog button with its tooltip and tour id.
QPushButton* RoomTypesDialog::addButton(QWidget* parent, const QString& text, const QString& tip, const QString& tourId)
{
    auto* button = new QPushButton(text, parent);
    button->setToolTip(tip);
    button->setProperty("tourId", tourId);
    return button;
}

// Fills the room type list and selects one.
void RoomTypesDialog::fillRooms(const QString& select)
{
    filling_ = true;
    rooms_->clear();
    int row = 0;
    if (host_.catalog()) {
        for (const plynth::RoomTypeEntry& entry : host_.catalog()->roomTypes()) {
            auto* item = new QListWidgetItem(fromCore(entry.name), rooms_);
            item->setData(kIdRole, fromCore(entry.id));
            if (fromCore(entry.id) == select)
                row = rooms_->count() - 1;
        }
    }
    rooms_->setCurrentRow(row);
    filling_ = false;
    fillLinks();
    updateButtons();
}

// Fills the category list and selects one.
void RoomTypesDialog::fillCategories(const QString& select)
{
    filling_ = true;
    categories_->clear();
    int row = 0;
    if (host_.catalog()) {
        for (const plynth::CategoryEntry& entry : host_.catalog()->categories()) {
            auto* item = new QListWidgetItem(fromCore(entry.name), categories_);
            item->setData(kIdRole, fromCore(entry.id));
            if (fromCore(entry.id) == select)
                row = categories_->count() - 1;
        }
    }
    categories_->setCurrentRow(row);
    filling_ = false;
    fillLinks();
    updateButtons();
}

// Fills the check list of categories for the selected room type.
void RoomTypesDialog::fillLinks()
{
    filling_ = true;
    links_->clear();
    const QListWidgetItem* room = rooms_->currentItem();
    if (!host_.catalog() || room == nullptr) {
        linksTitle_->setText(tr("Categories in this room type"));
        links_->setEnabled(false);
        filling_ = false;
        return;
    }
    linksTitle_->setText(tr("Categories in %1").arg(room->text()));
    links_->setEnabled(true);
    std::vector<std::string> linked;
    for (const plynth::RoomTypeEntry& entry : host_.catalog()->roomTypes())
        if (fromCore(entry.id) == room->data(kIdRole).toString())
            linked = entry.categories;
    for (const plynth::CategoryEntry& entry : host_.catalog()->categories()) {
        auto* item = new QListWidgetItem(fromCore(entry.name), links_);
        item->setData(kIdRole, fromCore(entry.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool on = std::find(linked.begin(), linked.end(), entry.id) != linked.end();
        item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
    filling_ = false;
}

// Turns the buttons that need a selection on or off.
void RoomTypesDialog::updateButtons()
{
    const int row = rooms_->currentRow();
    const bool room = row >= 0;
    renameRoom_->setEnabled(room);
    removeRoom_->setEnabled(room);
    up_->setEnabled(room && row > 0);
    down_->setEnabled(room && row < rooms_->count() - 1);
    const bool category = categories_->currentRow() >= 0;
    renameCategory_->setEnabled(category);
    removeCategory_->setEnabled(category);
}

// Shows a refusal in amber under the lists.
void RoomTypesDialog::report(const QString& text)
{
    problem_->setText(text);
    problem_->setVisible(!text.isEmpty());
}

// Asks for a name. Sets ok false when the user cancels.
QString RoomTypesDialog::askName(const QString& title, const QString& label, const QString& start, bool* ok)
{
    return QInputDialog::getText(this, title, label, QLineEdit::Normal, start, ok).trimmed();
}

// Adds a room type named by the user, with an id made from the name.
void RoomTypesDialog::addRoom()
{
    if (!host_.catalog())
        return;
    bool ok = false;
    const QString name = askName(tr("Add room type"), tr("Name of the room type"), QString(), &ok);
    if (!ok || name.isEmpty())
        return;
    const std::string id = plynth::uniqueCatalogId(plynth::catalogIdFromName(name.toStdString()),
                                                   [this](std::string_view candidate) {
                                                       for (const plynth::RoomTypeEntry& entry : host_.catalog()->roomTypes())
                                                           if (entry.id == candidate)
                                                               return true;
                                                       return false;
                                                   });
    const plynth::Status status = host_.catalog()->addRoomType(id, name.toStdString());
    report(status.ok() ? QString() : fromCore(status.message));
    host_.roomTypesEdited();
    fillRooms(fromCore(id));
}

// Renames the selected room type.
void RoomTypesDialog::renameRoom()
{
    const QListWidgetItem* item = rooms_->currentItem();
    if (!host_.catalog() || item == nullptr)
        return;
    bool ok = false;
    const QString name = askName(tr("Rename room type"), tr("New name"), item->text(), &ok);
    if (!ok || name.isEmpty())
        return;
    const QString id = item->data(kIdRole).toString();
    const plynth::Status status = host_.catalog()->renameRoomType(id.toStdString(), name.toStdString());
    report(status.ok() ? QString() : fromCore(status.message));
    host_.roomTypesEdited();
    fillRooms(id);
}

// Removes the selected room type after asking.
void RoomTypesDialog::removeRoom()
{
    const QListWidgetItem* item = rooms_->currentItem();
    if (!host_.catalog() || item == nullptr)
        return;
    const auto answer = QMessageBox::question(
        this, tr("Remove room type"),
        tr("Remove %1? Plans that use it keep its id and show the id as the label.").arg(item->text()));
    if (answer != QMessageBox::Yes)
        return;
    const plynth::Status status = host_.catalog()->removeRoomType(item->data(kIdRole).toString().toStdString());
    report(status.ok() ? QString() : fromCore(status.message));
    host_.roomTypesEdited();
    fillRooms(QString());
}

// Moves the selected room type up or down in the picker order.
void RoomTypesDialog::moveRoom(int step)
{
    const QListWidgetItem* item = rooms_->currentItem();
    if (!host_.catalog() || item == nullptr)
        return;
    const QString id = item->data(kIdRole).toString();
    const plynth::Status status = host_.catalog()->moveRoomType(id.toStdString(), rooms_->currentRow() + step);
    report(status.ok() ? QString() : fromCore(status.message));
    host_.roomTypesEdited();
    fillRooms(id);
}

// Adds a category named by the user.
void RoomTypesDialog::addCategory()
{
    if (!host_.catalog())
        return;
    bool ok = false;
    const QString name = askName(tr("Add category"), tr("Name of the category"), QString(), &ok);
    if (!ok || name.isEmpty())
        return;
    const std::string id = plynth::uniqueCatalogId(plynth::catalogIdFromName(name.toStdString()),
                                                   [this](std::string_view candidate) {
                                                       for (const plynth::CategoryEntry& entry : host_.catalog()->categories())
                                                           if (entry.id == candidate)
                                                               return true;
                                                       return false;
                                                   });
    const plynth::Status status = host_.catalog()->addCategory(id, name.toStdString());
    report(status.ok() ? QString() : fromCore(status.message));
    host_.assetsEdited();
    fillCategories(fromCore(id));
}

// Renames the selected category.
void RoomTypesDialog::renameCategory()
{
    const QListWidgetItem* item = categories_->currentItem();
    if (!host_.catalog() || item == nullptr)
        return;
    bool ok = false;
    const QString name = askName(tr("Rename category"), tr("New name"), item->text(), &ok);
    if (!ok || name.isEmpty())
        return;
    const QString id = item->data(kIdRole).toString();
    const plynth::Status status = host_.catalog()->renameCategory(id.toStdString(), name.toStdString());
    report(status.ok() ? QString() : fromCore(status.message));
    host_.assetsEdited();
    fillCategories(id);
}

// Removes the selected category after asking.
void RoomTypesDialog::removeCategory()
{
    const QListWidgetItem* item = categories_->currentItem();
    if (!host_.catalog() || item == nullptr)
        return;
    const auto answer = QMessageBox::question(this, tr("Remove category"),
                                              tr("Remove %1? Every asset that has it loses it.").arg(item->text()));
    if (answer != QMessageBox::Yes)
        return;
    const plynth::Status status = host_.catalog()->removeCategory(item->data(kIdRole).toString().toStdString());
    report(status.ok() ? QString() : fromCore(status.message));
    host_.roomTypesEdited();
    host_.assetsEdited();
    fillCategories(QString());
}

// Saves the ticked categories of the selected room type.
void RoomTypesDialog::linksEdited()
{
    const QListWidgetItem* room = rooms_->currentItem();
    if (filling_ || !host_.catalog() || room == nullptr)
        return;
    std::vector<std::string> ids;
    for (int i = 0; i < links_->count(); ++i)
        if (links_->item(i)->checkState() == Qt::Checked)
            ids.push_back(links_->item(i)->data(kIdRole).toString().toStdString());
    const plynth::Status status =
        host_.catalog()->setRoomTypeCategories(room->data(kIdRole).toString().toStdString(), ids);
    report(status.ok() ? QString() : fromCore(status.message));
    host_.roomTypesEdited();
}

}
