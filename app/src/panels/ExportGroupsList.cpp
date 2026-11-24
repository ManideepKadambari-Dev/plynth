#include "ExportGroupsList.h"

#include "theme/Theme.h"

#include <plynth/core/EditSession.h>

#include <QAbstractItemDelegate>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>
#include <vector>

namespace plynth::app {

namespace {

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
    // Makes the tree, with the guard that gives a drag its ticket.
    GroupsTree(const plynth::ExportEditGuard& guard, QWidget* parent) : QTreeWidget(parent), guard_(guard) {}

    std::function<void(const plynth::ExportMember&, const std::string&, plynth::ExportEditTicket)> onDropMember;
    std::function<void(QTreeWidgetItem*)> onEditorOpened;
    std::function<void()> onEditorClosed;

    // True while a name is being typed in a row.
    bool editing() const { return state() == QAbstractItemView::EditingState; }

protected:
    // Reports an editor the moment it opens, from F2, a double click or the menu.
    bool edit(const QModelIndex& index, EditTrigger trigger, QEvent* event) override
    {
        const bool wasEditing = editing();
        const bool handled = QTreeWidget::edit(index, trigger, event);
        if (!wasEditing && editing() && onEditorOpened)
            onEditorOpened(itemFromIndex(index));
        return handled;
    }

    // Reports every close. The view also closes an editor by itself when another row is picked.
    void closeEditor(QWidget* editor, QAbstractItemDelegate::EndEditHint hint) override
    {
        QTreeWidget::closeEditor(editor, hint);
        if (onEditorClosed)
            onEditorClosed();
    }

    // Keeps the member and the ticket for as long as the drag lasts, so a rebuilt tree cannot swap them.
    void startDrag(Qt::DropActions actions) override
    {
        if (!isMember(currentItem()))
            return;
        drag_ = Drag{memberOf(currentItem()), guard_.ticket()};
        QTreeWidget::startDrag(actions);
        drag_.reset();
    }

    // Accepts a dragged member over any row.
    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (event->source() == this && drag_ && itemAt(event->position().toPoint()))
            event->acceptProposedAction();
        else
            event->ignore();
    }

    // Reports the member that was picked up, its ticket and the group it landed on.
    void dropEvent(QDropEvent* event) override
    {
        QTreeWidgetItem* target = itemAt(event->position().toPoint());
        event->ignore();
        if (event->source() == this && drag_ && target && onDropMember)
            onDropMember(drag_->member, groupIdOf(target), drag_->ticket);
    }

private:
    // What is being dragged.
    struct Drag {
        plynth::ExportMember member;
        plynth::ExportEditTicket ticket;
    };

    const plynth::ExportEditGuard& guard_;
    std::optional<Drag> drag_;
};

// Builds the list: notes, the tree, the message line and the two buttons.
ExportGroupsList::ExportGroupsList(plynth::ExportEditGuard& guard, QWidget* parent) : QWidget(parent), guard_(guard)
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

    auto* tree = new GroupsTree(guard_, this);
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
    message_->setToolTip(tr("Why editing is locked, or why the last edit was not saved"));
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
    connect(newButton_, &QPushButton::clicked, this, &ExportGroupsList::newGroup);
    connect(resetButton_, &QPushButton::clicked, this, &ExportGroupsList::resetGroups);
    tree->onEditorOpened = [this](QTreeWidgetItem* item) { editorOpened(item); };
    tree->onEditorClosed = [this]() { QTimer::singleShot(0, this, &ExportGroupsList::editorClosed); };
    tree->onDropMember = [this](const plynth::ExportMember& member, const std::string& to,
                                plynth::ExportEditTicket ticket) { moveMember(member, to, ticket); };

    rebuild();
}

// True while a group name is being typed.
bool ExportGroupsList::editing() const
{
    return tree_->editing();
}

// True when no group here may be edited.
bool ExportGroupsList::locked() const
{
    return stale_ || guard_.state() != plynth::ExportEditDecision::Allowed;
}

// Shows a review. The tree is rebuilt only when something it shows changed, so open groups and the selection stay.
// The plan only gives the members their plain labels.
void ExportGroupsList::setReview(const std::filesystem::path& projectFolder, const plynth::ExportGroupReview& review,
                                 const plynth::Plan* plan)
{
    std::map<std::pair<std::string, std::string>, QString> labels;
    const plynth::Plan none;
    for (const auto& group : review.groups)
        for (const auto& member : group.members)
            labels[{member.kind, member.id}] = fromCore(plynth::exportMemberLabel(plan ? *plan : none, member));
    if (editing())
        cancelEditing();
    const bool same = projectFolder == folder_ && review.groups == review_.groups && review.notes == review_.notes &&
                      review.options == review_.options && labels == labels_;
    folder_ = projectFolder;
    review_ = review;
    labels_ = std::move(labels);
    stale_ = false;
    updateMessage();
    if (!same || needsRebuild_ || locked() != shownLocked_)
        rebuild();
}

// Closes the name editor when the edit it would save is no longer allowed.
void ExportGroupsList::closeDeadEditor()
{
    if (editing() && (!rename_ || !guard_.holds(rename_->ticket)))
        cancelEditing();
}

// Closes an open name editor without saving. This comes before anything is hidden, so leaving the row cannot save it.
void ExportGroupsList::cancelEditing()
{
    rename_.reset();
    // An editor that closed a moment ago is still there until Qt deletes it, so each one is told.
    for (QLineEdit* editor : tree_->viewport()->findChildren<QLineEdit*>())
        emit tree_->itemDelegate()->closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
}

// Follows the guard. The rows are left alone while a name is typed, because changing them resets the typed text.
void ExportGroupsList::updateLock(bool stale)
{
    closeDeadEditor();
    stale_ = stale;
    updateMessage();
    updateButtons();
    if (!editing() && (needsRebuild_ || locked() != shownLocked_))
        rebuild();
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
    const bool closed = locked();
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
        if (closed) {
            row->setForeground(0, muted);
            row->setForeground(2, muted);
        }
        row->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
        if (!closed)
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
            if (!closed)
                childFlags |= Qt::ItemIsDragEnabled;
            child->setFlags(childFlags);
            child->setToolTip(0, tr("Right click to move it to another group, or drag it onto a group"));
        }
        if (group.rest)
            addNote(row, tr("Also takes anything added by hand"));
        row->setExpanded(expanded_.count(group.id) > 0);
    }
    tree_->setDragEnabled(!closed);
    shownLocked_ = closed;
    needsRebuild_ = false;
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

// Shows why editing is locked or why an edit was not saved.
void ExportGroupsList::updateMessage()
{
    const QString reason = fromCore(guard_.message());
    message_->setText(reason);
    message_->setVisible(!reason.isEmpty());
}

// Says whether a box or editor may open for this ticket. If not, the guard refuses the edit and gives the reason.
bool ExportGroupsList::mayOpen(plynth::ExportEditTicket ticket)
{
    if (guard_.holds(ticket))
        return true;
    // The guard turns the ticket down before it runs the change, and this change refuses too, so nothing is written.
    writeEdit(ticket, [](plynth::ExportGroupEdits&, const std::vector<plynth::ExportGroup>&) {
        return plynth::fail(plynth::ErrorCode::Locked, std::string());
    });
    return false;
}

// Hands one edit to the guard. The tree follows with the new review, after the event that made the edit.
void ExportGroupsList::writeEdit(plynth::ExportEditTicket ticket, const plynth::ExportGroupChange& change)
{
    // The result is not kept: the guard's message already has the sentence of a refused or failed edit.
    guard_.edit(ticket, plynth::sessionNow(), change);
    updateMessage();
    emit editTried();
}

// Keeps the group and the ticket of the name editor that opened.
void ExportGroupsList::editorOpened(QTreeWidgetItem* item)
{
    rename_.reset();
    if (item)
        rename_ = Rename{groupIdOf(item), menuTicket_ ? *menuTicket_ : guard_.ticket()};
}

// Starts typing a new name for a group, with the ticket of the menu it came from.
void ExportGroupsList::startRename(const std::string& group, plynth::ExportEditTicket ticket)
{
    if (!mayOpen(ticket))
        return;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = tree_->topLevelItem(i);
        if (groupIdOf(row) == group) {
            menuTicket_ = ticket;
            tree_->editItem(row, 0);
            menuTicket_.reset();
            return;
        }
    }
}

// Applies the name typed in a group row. Without an editor opened for that group the ticket is one that is never good.
void ExportGroupsList::renameTo(QTreeWidgetItem* item)
{
    const std::string group = groupIdOf(item);
    const std::string name = toCore(item->text(0));
    const plynth::ExportEditTicket ticket =
        rename_ && rename_->group == group ? rename_->ticket : plynth::ExportEditTicket{};
    needsRebuild_ = true;
    writeEdit(ticket, [&group, &name](plynth::ExportGroupEdits& edits, const std::vector<plynth::ExportGroup>& current) {
        return plynth::renameExportGroup(edits, current, group, name);
    });
}

// Merges a group into another one.
void ExportGroupsList::mergeGroup(const std::string& group, const std::string& into, plynth::ExportEditTicket ticket)
{
    writeEdit(ticket, [&group, &into](plynth::ExportGroupEdits& edits, const std::vector<plynth::ExportGroup>& current) {
        return plynth::mergeExportGroups(edits, current, group, into);
    });
}

// Moves a member to a group.
void ExportGroupsList::moveMember(const plynth::ExportMember& member, const std::string& to,
                                  plynth::ExportEditTicket ticket)
{
    writeEdit(ticket, [&member, &to](plynth::ExportGroupEdits& edits, const std::vector<plynth::ExportGroup>& current) {
        return plynth::moveExportMember(edits, current, member, to);
    });
}

// Asks for a name, adds a group and moves the member into it.
void ExportGroupsList::moveToNewGroup(plynth::ExportMember member, plynth::ExportEditTicket ticket)
{
    if (!mayOpen(ticket))
        return;
    bool ok = false;
    const QString typed = QInputDialog::getText(this, tr("New group"), newGroupPrompt(), QLineEdit::Normal,
                                                QString(), &ok)
                              .trimmed();
    if (!ok)
        return;
    const std::string name = toCore(typed);
    writeEdit(ticket, [&member, &name](plynth::ExportGroupEdits& edits, const std::vector<plynth::ExportGroup>& current) {
        std::string id;
        plynth::Status status = plynth::addExportGroup(edits, current, name, id);
        if (!status.ok())
            return status;
        // The new group is not in the current list yet, so the move is checked against a list that has it.
        std::vector<plynth::ExportGroup> next = current;
        plynth::ExportGroup added;
        added.id = id;
        added.name = name;
        next.push_back(added);
        return plynth::moveExportMember(edits, next, member, id);
    });
}

// Asks for a name and adds an empty group.
void ExportGroupsList::newGroup()
{
    const plynth::ExportEditTicket ticket = guard_.ticket();
    if (!mayOpen(ticket))
        return;
    bool ok = false;
    const QString typed = QInputDialog::getText(this, tr("New group"), newGroupPrompt(), QLineEdit::Normal,
                                                QString(), &ok)
                              .trimmed();
    if (!ok)
        return;
    const std::string name = toCore(typed);
    writeEdit(ticket, [&name](plynth::ExportGroupEdits& edits, const std::vector<plynth::ExportGroup>& current) {
        std::string id;
        return plynth::addExportGroup(edits, current, name, id);
    });
}

// Throws away every group edit after asking.
void ExportGroupsList::resetGroups()
{
    const plynth::ExportEditTicket ticket = guard_.ticket();
    if (!mayOpen(ticket))
        return;
    const auto answer = QMessageBox::question(this, tr("Reset groups"),
                                              tr("Throw away every group edit and go back to the automatic groups?"),
                                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;
    writeEdit(ticket, [](plynth::ExportGroupEdits& edits, const std::vector<plynth::ExportGroup>&) {
        edits = plynth::ExportGroupEdits{};
        return plynth::Status{};
    });
}

// Shows the menu of a row: rename and merge for a group, move for a member. Every action keeps the ids and the
// ticket from when the menu was built.
void ExportGroupsList::showMenu(const QPoint& at)
{
    QTreeWidgetItem* item = tree_->itemAt(at);
    if (!item || locked())
        return;
    const plynth::ExportEditTicket ticket = guard_.ticket();
    QMenu menu(this);
    if (isMember(item)) {
        const plynth::ExportMember member = memberOf(item);
        const std::string from = groupIdOf(item);
        QMenu* move = menu.addMenu(tr("Move to"));
        for (const auto& group : review_.groups) {
            if (group.id == from)
                continue;
            const std::string target = group.id;
            move->addAction(fromCore(group.name), this,
                            [this, member, target, ticket] { moveMember(member, target, ticket); });
        }
        move->addSeparator();
        move->addAction(tr("New group..."), this, [this, member, ticket] { moveToNewGroup(member, ticket); });
    } else {
        const std::string id = groupIdOf(item);
        menu.addAction(tr("Rename"), this, [this, id, ticket] { startRename(id, ticket); });
        QMenu* merge = menu.addMenu(tr("Merge into"));
        for (const auto& group : review_.groups) {
            if (group.id == id)
                continue;
            const std::string target = group.id;
            merge->addAction(fromCore(group.name), this, [this, id, target, ticket] { mergeGroup(id, target, ticket); });
        }
        merge->setEnabled(!merge->actions().isEmpty());
    }
    menu.exec(tree_->viewport()->mapToGlobal(at));
}

} // namespace plynth::app
