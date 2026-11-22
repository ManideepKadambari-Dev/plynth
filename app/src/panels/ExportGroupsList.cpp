#include "ExportGroupsList.h"

#include "theme/Theme.h"

#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QAbstractItemDelegate>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace plynth::app {

namespace {

// True when two group lists mean the same: ids, names, members and rest, in the same order. The file names are left
// out, because they depend on the project name.
bool sameGroups(const std::vector<plynth::ExportGroup>& a, const std::vector<plynth::ExportGroup>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || a[i].name != b[i].name || a[i].rest != b[i].rest || a[i].members != b[i].members)
            return false;
    return true;
}

constexpr int kRoleGroup = Qt::UserRole;      // the group id, on group and member rows
constexpr int kRoleKind = Qt::UserRole + 1;   // the member kind; empty on group rows
constexpr int kRoleMember = Qt::UserRole + 2; // the member id

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Turns Qt text into a core text.
std::string toCore(const QString& text)
{
    return text.toStdString();
}

// True for a row that stands for one member.
bool isMember(const QTreeWidgetItem* item)
{
    return item && item->parent() && !item->data(0, kRoleKind).toString().isEmpty();
}

// The member a member row stands for.
plynth::ExportMember memberOf(const QTreeWidgetItem* item)
{
    plynth::ExportMember member;
    member.kind = toCore(item->data(0, kRoleKind).toString());
    member.id = toCore(item->data(0, kRoleMember).toString());
    return member;
}

// The group id of a group row or of the row of one of its members.
std::string groupIdOf(const QTreeWidgetItem* item)
{
    return toCore(item->data(0, kRoleGroup).toString());
}

} // namespace

// The tree of groups. A member dragged onto a group is reported, never moved by the tree itself.
class GroupsTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

    std::function<void(const plynth::ExportMember&, const std::string&)> onDropMember;

    // True while a name is being typed in a row.
    bool editing() const { return state() == QAbstractItemView::EditingState; }

protected:
    // Accepts a dragged member over any row.
    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (event->source() == this && isMember(currentItem()) && itemAt(event->position().toPoint()))
            event->acceptProposedAction();
        else
            event->ignore();
    }

    // Reports the member and the group it landed on.
    void dropEvent(QDropEvent* event) override
    {
        QTreeWidgetItem* source = currentItem();
        QTreeWidgetItem* target = itemAt(event->position().toPoint());
        event->ignore();
        if (event->source() == this && isMember(source) && target && onDropMember)
            onDropMember(memberOf(source), groupIdOf(target));
    }
};

// Builds the list: notes, the tree, the message line and the two buttons.
ExportGroupsList::ExportGroupsList(QWidget* parent) : QWidget(parent)
{
    const Theme& t = Theme::instance();
    setProperty("tourId", QStringLiteral("export-groups"));
    setToolTip(tr("The files a grouped export writes. Rename a group, merge groups, or move a member to another group."));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(t.unit());

    notes_ = new QLabel(this);
    notes_->setWordWrap(true);
    notes_->setFont(t.uiFont(t.sizeSmall()));
    notes_->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
    notes_->setToolTip(tr("Edits that no longer apply to the plan and were skipped"));
    column->addWidget(notes_);

    empty_ = new QLabel(tr("Build the project to see its groups."), this);
    empty_->setWordWrap(true);
    empty_->setFont(t.uiFont(t.sizeNormal()));
    empty_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    column->addWidget(empty_);

    auto* tree = new GroupsTree(this);
    tree_ = tree;
    tree_->setColumnCount(3);
    tree_->setHeaderHidden(true);
    tree_->setRootIsDecorated(true);
    tree_->setUniformRowHeights(true);
    tree_->setIndentation(t.unit() * 4);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::DoubleClicked);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setDragEnabled(true);
    tree_->setAcceptDrops(true);
    tree_->setDropIndicatorShown(true);
    tree_->setDragDropMode(QAbstractItemView::DragDrop);
    tree_->setDefaultDropAction(Qt::IgnoreAction);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    tree_->setFont(t.uiFont(t.sizeNormal()));
    tree_->setToolTip(tr("One row for each file the export writes. Double click or press F2 to rename a group. "
                         "Right click for Merge into and Move to, or drag a member onto a group."));
    column->addWidget(tree_);

    message_ = new QLabel(this);
    message_->setWordWrap(true);
    message_->setFont(t.uiFont(t.sizeSmall()));
    message_->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
    message_->setToolTip(tr("Why the last edit was refused"));
    message_->hide();
    column->addWidget(message_);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(t.unit() * 2);
    newButton_ = new QPushButton(tr("New group..."), this);
    newButton_->setToolTip(tr("Add an empty group. Move members into it to make a file"));
    newButton_->setProperty("tourId", QStringLiteral("export-groups-new"));
    resetButton_ = new QPushButton(tr("Reset groups"), this);
    resetButton_->setToolTip(tr("Throw away every group edit and go back to the automatic groups"));
    resetButton_->setProperty("tourId", QStringLiteral("export-groups-reset"));
    buttons->addWidget(newButton_);
    buttons->addWidget(resetButton_);
    buttons->addStretch(1);
    column->addLayout(buttons);

    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &ExportGroupsList::showMenu);
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int changed) {
        if (!rebuilding_ && changed == 0 && !item->parent())
            renameTo(item);
    });
    connect(tree_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        if (!rebuilding_)
            expanded_.insert(groupIdOf(item));
    });
    connect(tree_, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
        if (!rebuilding_)
            expanded_.erase(groupIdOf(item));
    });
    connect(tree_->itemDelegate(), &QAbstractItemDelegate::closeEditor, this,
            [this]() { QTimer::singleShot(0, this, [this]() { emit editorClosed(); }); });
    connect(newButton_, &QPushButton::clicked, this, &ExportGroupsList::newGroup);
    connect(resetButton_, &QPushButton::clicked, this, &ExportGroupsList::resetGroups);
    tree->onDropMember = [this](const plynth::ExportMember& member, const std::string& to) {
        if (!locked())
            moveMember(member, to);
    };

    rebuild();
}

// True while a group name is being typed.
bool ExportGroupsList::editing() const
{
    return static_cast<GroupsTree*>(tree_)->editing();
}

// Shows a review. The plan only gives the members their plain labels. A name still being typed is dropped, because
// the panel keeps the editor open only when nothing here changes.
void ExportGroupsList::setReview(const std::filesystem::path& projectFolder, const plynth::ExportGroupReview& review,
                                 bool readOnly, const plynth::Plan* plan)
{
    std::map<std::pair<std::string, std::string>, QString> labels;
    const plynth::Plan none;
    for (const auto& group : review.groups)
        for (const auto& member : group.members)
            labels[{member.kind, member.id}] = fromCore(plynth::exportMemberLabel(plan ? *plan : none, member));
    if (editing())
        cancelEditing();
    applyReview(projectFolder, review, readOnly, labels);
}

// Takes a review. The tree is rebuilt only when something it shows changed, so open groups and the selection stay.
// A review is a reload, so it ends a lock that came from a change outside.
void ExportGroupsList::applyReview(const std::filesystem::path& projectFolder, const plynth::ExportGroupReview& review,
                                   bool readOnly,
                                   const std::map<std::pair<std::string, std::string>, QString>& labels)
{
    const bool wasLocked = outsideLocked_;
    outsideLocked_ = false;
    const bool same = projectFolder == folder_ && readOnly == shownLocked_ && review.groups == review_.groups &&
                      review.notes == review_.notes && review.options == review_.options && labels == labels_;
    folder_ = projectFolder;
    review_ = review;
    readOnly_ = readOnly;
    labels_ = labels;
    expected_ = review.groups;
    if (readOnly)
        showMessage(lockText());
    else if (wasLocked || !same)
        message_->hide();
    if (same)
        return;
    rebuild();
}

// True when showing this review would lock the list.
bool ExportGroupsList::mustCloseEditor(bool readOnly, const plynth::ExportGroupReview* review) const
{
    return readOnly || review == nullptr || !sameGroups(review->groups, expected_);
}

// Closes an open name editor without saving.
void ExportGroupsList::cancelEditing()
{
    if (auto* editor = tree_->viewport()->findChild<QLineEdit*>())
        emit tree_->itemDelegate()->closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
}

// Compares the groups in the options file now with the ones shown.
void ExportGroupsList::checkOutside()
{
    if (locked() || folder_.empty())
        return;
    const plynth::ExportOptionsRead read = plynth::readExportOptions(folder_);
    if (!read.status.ok()) {
        lockOutside();
        return;
    }
    const plynth::ExportGroupsApplied now = plynth::applyExportGroupEdits(review_.automatic, read.options.groups, "");
    if (!sameGroups(now.groups, expected_))
        lockOutside();
}

// Locks editing because the groups changed outside the app, and asks for the review again.
void ExportGroupsList::lockOutside()
{
    outsideLocked_ = true;
    cancelEditing();
    showMessage(lockText());
    QTimer::singleShot(0, this, [this]() { rebuild(); });
    emit outsideChanged();
}

// Why editing is locked, in plain words.
QString ExportGroupsList::lockText() const
{
    return outsideLocked_ ? tr("The groups were changed outside the app. Editing is locked until the list reloads.")
                          : tr("The project is being edited from the command line. Editing is locked until it ends.");
}

// Opens or closes every group.
void ExportGroupsList::setAllExpanded(bool expanded)
{
    expanded_.clear();
    if (expanded)
        for (const auto& group : review_.groups)
            expanded_.insert(group.id);
    rebuilding_ = true;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        tree_->topLevelItem(i)->setExpanded(expanded);
    rebuilding_ = false;
}

// Fills the tree from the review.
void ExportGroupsList::rebuild()
{
    const Theme& t = Theme::instance();
    rebuilding_ = true;
    tree_->clear();

    const bool has = !review_.groups.empty();
    empty_->setVisible(!has);
    tree_->setVisible(has);
    QStringList notes;
    for (const auto& note : review_.notes)
        notes << fromCore(note);
    notes_->setText(notes.join(QLatin1Char('\n')));
    notes_->setVisible(!notes.isEmpty());

    const QColor muted = t.muted();
    for (const auto& group : review_.groups) {
        auto* row = new QTreeWidgetItem(tree_);
        row->setText(0, fromCore(group.name));
        row->setText(1, fromCore(group.file));
        row->setText(2, QString::number(group.members.size()));
        row->setData(0, kRoleGroup, fromCore(group.id));
        row->setFont(0, t.uiFont(t.sizeNormal(), QFont::DemiBold));
        row->setFont(1, t.monoFont(t.sizeSmall()));
        row->setFont(2, t.monoFont(t.sizeSmall()));
        row->setForeground(1, muted);
        row->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
        if (!locked())
            flags |= Qt::ItemIsEditable;
        row->setFlags(flags);
        row->setToolTip(0, tr("%1, written as %2").arg(fromCore(group.name), fromCore(group.file)));
        row->setToolTip(1, tr("The file this group is written to"));
        row->setToolTip(2, tr("How many members the group has"));

        if (group.members.empty() && !group.rest)
            addNote(row, tr("Not written: nothing in it"));
        for (const auto& member : group.members) {
            auto* child = new QTreeWidgetItem(row);
            child->setText(0, labels_[{member.kind, member.id}]);
            child->setText(1, fromCore(member.kind));
            child->setData(0, kRoleGroup, fromCore(group.id));
            child->setData(0, kRoleKind, fromCore(member.kind));
            child->setData(0, kRoleMember, fromCore(member.id));
            child->setFont(0, t.uiFont(t.sizeSmall()));
            child->setFont(1, t.monoFont(t.sizeSmall()));
            child->setForeground(0, muted);
            child->setForeground(1, muted);
            Qt::ItemFlags childFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
            if (!locked())
                childFlags |= Qt::ItemIsDragEnabled;
            child->setFlags(childFlags);
            child->setToolTip(0, tr("Right click to move it to another group, or drag it onto a group"));
        }
        if (group.rest)
            addNote(row, tr("Also takes anything added by hand"));
        row->setExpanded(expanded_.count(group.id) > 0);
    }
    tree_->setDragEnabled(!locked());
    shownLocked_ = locked();
    rebuilding_ = false;
    updateButtons();
}

// Adds a muted line under a group that is not a member.
void ExportGroupsList::addNote(QTreeWidgetItem* row, const QString& text)
{
    const Theme& t = Theme::instance();
    auto* note = new QTreeWidgetItem(row);
    note->setText(0, text);
    note->setFont(0, t.uiFont(t.sizeSmall()));
    note->setForeground(0, t.muted());
    note->setFlags(Qt::ItemIsEnabled);
    note->setData(0, kRoleGroup, row->data(0, kRoleGroup));
}

// Turns the buttons on or off.
void ExportGroupsList::updateButtons()
{
    const bool edits = !review_.options.groups.empty();
    newButton_->setEnabled(!locked() && !review_.groups.empty());
    resetButton_->setEnabled(!locked() && edits);
}

// The text above the box that asks for a group name, with the characters a name may use.
QString ExportGroupsList::newGroupPrompt() const
{
    return tr("Name of the new group.\nLetters A to Z, digits, spaces, hyphens and underscores, up to 64 characters.");
}

// Shows why an edit was refused.
void ExportGroupsList::showMessage(const QString& text)
{
    message_->setText(text);
    message_->setVisible(!text.isEmpty());
}

// Reads the options file again just before an edit and works out the groups they give now. When those are not the
// groups shown, nothing is written: the list locks and reloads. Otherwise the fresh options are kept, so a change
// that did not touch the groups (the kit switch) is not lost, and current is what the edit is checked against.
bool ExportGroupsList::freshOptions(plynth::ExportOptions& options, std::vector<plynth::ExportGroup>& current)
{
    auto refuse = [this](const QString& text) {
        showMessage(text);
        QTimer::singleShot(0, this, [this]() { rebuild(); });
        return false;
    };
    if (locked())
        return refuse(lockText());
    const plynth::ExportOptionsRead read = plynth::readExportOptions(folder_);
    if (!read.status.ok())
        return refuse(fromCore(read.status.message));
    plynth::ExportGroupsApplied now = plynth::applyExportGroupEdits(review_.automatic, read.options.groups, "");
    if (!sameGroups(now.groups, expected_)) {
        lockOutside();
        return false;
    }
    options = read.options;
    current = std::move(now.groups);
    return true;
}

// Writes the options after an edit that was accepted, or shows the refusal.
bool ExportGroupsList::store(const plynth::ExportOptions& options, const plynth::Status& status)
{
    // The tree is put back to what is saved after the current event, not inside it.
    auto refuse = [this](const QString& text) {
        showMessage(text);
        QTimer::singleShot(0, this, [this]() { rebuild(); });
        return false;
    };
    if (locked())
        return refuse(lockText());
    if (!status.ok())
        return refuse(fromCore(status.message));
    const plynth::Status written = plynth::writeExportOptions(folder_, options);
    if (!written.ok())
        return refuse(fromCore(written.message));
    expected_ = plynth::applyExportGroupEdits(review_.automatic, options.groups, "").groups;
    showMessage(QString());
    emit edited();
    return true;
}

// Starts typing a new name for a group.
void ExportGroupsList::startRename(const std::string& group)
{
    if (locked())
        return;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = tree_->topLevelItem(i);
        if (groupIdOf(row) == group) {
            tree_->editItem(row, 0);
            return;
        }
    }
}

// Applies the name typed in a group row.
void ExportGroupsList::renameTo(QTreeWidgetItem* item)
{
    plynth::ExportOptions options;
    std::vector<plynth::ExportGroup> current;
    if (!freshOptions(options, current))
        return;
    const plynth::Status status = plynth::renameExportGroup(options.groups, current, groupIdOf(item),
                                                            toCore(item->text(0)));
    store(options, status);
}

// Merges a group into another one.
void ExportGroupsList::mergeGroup(const std::string& group, const std::string& into)
{
    plynth::ExportOptions options;
    std::vector<plynth::ExportGroup> current;
    if (!freshOptions(options, current))
        return;
    const plynth::Status status = plynth::mergeExportGroups(options.groups, current, group, into);
    store(options, status);
}

// Moves a member to a group.
void ExportGroupsList::moveMember(const plynth::ExportMember& member, const std::string& to)
{
    plynth::ExportOptions options;
    std::vector<plynth::ExportGroup> current;
    if (!freshOptions(options, current))
        return;
    const plynth::Status status = plynth::moveExportMember(options.groups, current, member, to);
    store(options, status);
}

// Asks for a name, adds a group and moves the member into it.
void ExportGroupsList::moveToNewGroup(const plynth::ExportMember& member)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New group"), newGroupPrompt(), QLineEdit::Normal,
                                               QString(), &ok)
                             .trimmed();
    if (!ok)
        return;
    plynth::ExportOptions options;
    std::vector<plynth::ExportGroup> current;
    if (!freshOptions(options, current))
        return;
    std::string id;
    plynth::Status status = plynth::addExportGroup(options.groups, current, toCore(name), id);
    if (status.ok()) {
        // The new group is not in the current list yet, so the move is checked against a list that has it.
        std::vector<plynth::ExportGroup> next = current;
        plynth::ExportGroup added;
        added.id = id;
        added.name = toCore(name);
        next.push_back(added);
        status = plynth::moveExportMember(options.groups, next, member, id);
    }
    store(options, status);
}

// Asks for a name and adds an empty group.
void ExportGroupsList::newGroup()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New group"), newGroupPrompt(), QLineEdit::Normal,
                                               QString(), &ok)
                             .trimmed();
    if (!ok)
        return;
    plynth::ExportOptions options;
    std::vector<plynth::ExportGroup> current;
    if (!freshOptions(options, current))
        return;
    std::string id;
    const plynth::Status status = plynth::addExportGroup(options.groups, current, toCore(name), id);
    store(options, status);
}

// Throws away every group edit after asking.
void ExportGroupsList::resetGroups()
{
    const auto answer = QMessageBox::question(this, tr("Reset groups"),
                                              tr("Throw away every group edit and go back to the automatic groups?"),
                                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;
    plynth::ExportOptions options;
    std::vector<plynth::ExportGroup> current;
    if (!freshOptions(options, current))
        return;
    options.groups = plynth::ExportGroupEdits{};
    store(options, plynth::Status{});
}

// Shows the menu of a row: rename and merge for a group, move for a member.
void ExportGroupsList::showMenu(const QPoint& at)
{
    QTreeWidgetItem* item = tree_->itemAt(at);
    if (!item || locked())
        return;
    QMenu menu(this);
    if (isMember(item)) {
        const plynth::ExportMember member = memberOf(item);
        const std::string from = groupIdOf(item);
        QMenu* move = menu.addMenu(tr("Move to"));
        for (const auto& group : review_.groups) {
            if (group.id == from)
                continue;
            const std::string target = group.id;
            move->addAction(fromCore(group.name), this, [this, member, target] { moveMember(member, target); });
        }
        move->addSeparator();
        move->addAction(tr("New group..."), this, [this, member] { moveToNewGroup(member); });
    } else {
        const std::string id = groupIdOf(item);
        menu.addAction(tr("Rename"), this, [this, id] { startRename(id); });
        QMenu* merge = menu.addMenu(tr("Merge into"));
        for (const auto& group : review_.groups) {
            if (group.id == id)
                continue;
            const std::string target = group.id;
            merge->addAction(fromCore(group.name), this, [this, id, target] { mergeGroup(id, target); });
        }
        merge->setEnabled(!merge->actions().isEmpty());
    }
    menu.exec(tree_->viewport()->mapToGlobal(at));
}

} // namespace plynth::app
