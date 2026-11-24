#pragma once

#include <plynth/core/ExportEditGuard.h>
#include <plynth/core/ExportGroups.h>

#include <QString>
#include <QWidget>

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

class QLabel;
class QPushButton;
class QTreeWidgetItem;

namespace plynth::app {

class GroupsTree;

// The list of export groups shown in the Export tab, with their members and edit actions. Every edit goes through
// its owner's guard, with the ticket taken when the menu, box, name editor or drag opened.
class ExportGroupsList : public QWidget {
    Q_OBJECT
public:
    explicit ExportGroupsList(plynth::ExportEditGuard& guard, QWidget* parent = nullptr);

    // Shows a review that the guard took. The plan, when given, gives members their labels.
    void setReview(const std::filesystem::path& projectFolder, const plynth::ExportGroupReview& review,
                   const plynth::Plan* plan = nullptr);
    // True while a group name is being typed.
    bool editing() const;
    // Closes the name editor without saving when its ticket is no longer good.
    void closeDeadEditor();
    // Follows the guard: the message, the buttons and the grey rows. With stale set, the list is out of date and
    // is greyed too.
    void updateLock(bool stale);
    // Opens every group (for screenshots) or closes them all.
    void setAllExpanded(bool expanded);

signals:
    // An edit was written or refused; ask for the review again.
    void editTried();
    // The name editor was closed, by Enter, Escape, leaving the row or a lock.
    void editorClosed();

private:
    // The group a name editor renames and the ticket it opened with.
    struct Rename {
        std::string group;
        plynth::ExportEditTicket ticket;
    };

    bool locked() const;
    void cancelEditing();
    void rebuild();
    void showMenu(const QPoint& at);
    void editorOpened(QTreeWidgetItem* item);
    void startRename(const std::string& group, plynth::ExportEditTicket ticket);
    void renameTo(QTreeWidgetItem* item);
    void mergeGroup(const std::string& group, const std::string& into, plynth::ExportEditTicket ticket);
    void moveMember(const plynth::ExportMember& member, const std::string& to, plynth::ExportEditTicket ticket);
    void moveToNewGroup(plynth::ExportMember member, plynth::ExportEditTicket ticket);
    void newGroup();
    void resetGroups();
    bool mayOpen(plynth::ExportEditTicket ticket);
    void writeEdit(plynth::ExportEditTicket ticket, const plynth::ExportGroupChange& change);
    void updateMessage();
    QString newGroupPrompt() const;
    void addNote(QTreeWidgetItem* row, const QString& text);
    void updateButtons();

    plynth::ExportEditGuard& guard_;
    std::filesystem::path folder_;
    plynth::ExportGroupReview review_;
    bool stale_ = false;        // the list is out of date and waits for a new review
    bool shownLocked_ = false;  // the locked state the tree was last built with
    bool needsRebuild_ = false; // a row holds typed text that may not be what is saved
    bool rebuilding_ = false;
    std::optional<Rename> rename_;
    std::optional<plynth::ExportEditTicket> menuTicket_; // set while Rename of a menu opens the editor
    std::set<std::string> expanded_;
    std::map<std::pair<std::string, std::string>, QString> labels_;
    GroupsTree* tree_ = nullptr;
    QLabel* notes_ = nullptr;
    QLabel* empty_ = nullptr;
    QLabel* message_ = nullptr;
    QPushButton* newButton_ = nullptr;
    QPushButton* resetButton_ = nullptr;
};

} // namespace plynth::app
