#pragma once

#include <plynth/core/Export.h>
#include <plynth/core/ExportEditGuard.h>
#include <plynth/core/ExportGroups.h>

#include <QHash>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <filesystem>

class QCheckBox;
class QFileSystemWatcher;
class QLabel;
class QPushButton;
class QShowEvent;
class QTimer;
class QTreeWidget;
class QVBoxLayout;

namespace plynth::app {

class ExportGroupsList;

// What the Export tab needs to know besides the report.
struct ExportPanelState {
    bool running = false;      // an export or a build is running, so the buttons wait
    bool canRun = false;       // there is a build and setup is fine
    QString notice;            // why it cannot run, shown above the report; empty when it can
    bool offerBuild = false;   // show the Build button beside the notice
    QString error;             // the sentence of the last export that failed before it had a report
    bool folderExists = false; // the export folder is there to open
    bool grouped = false;      // the project exports one FBX for each group
    bool built = false;        // the project has a build, so its groups can be shown
    QString optionsProblem;    // why export-options.json cannot be read; empty when it can
    bool optionsRepairable = false; // the bad file may be replaced; false for one written by a newer Plynth
    bool kitEnabled = false;   // export-options.json switches the modular kit on
    bool gameplayEnabled = false; // export-options.json switches the gameplay data file on
    QString gameplayFile;      // where the gameplay data file goes, export/<Stem>_gameplay.json; empty without a folder
    std::filesystem::path projectFolder;               // empty when the project is not saved
    const plynth::Project* project = nullptr;          // used only during setReport; null when there is no project
    const plynth::ExportGroupReview* review = nullptr; // as core gave it, in every mode; null without a project folder
};

// The Export tab: the Export and Check buttons, the last export and the list of quality checks.
class ExportPanel : public QWidget {
    Q_OBJECT
public:
    explicit ExportPanel(QWidget* parent = nullptr);
    ~ExportPanel() override;

    // Shows a report, or the empty text when it is null.
    void setReport(const plynth::ExportReport* report, const ExportPanelState& state);
    // For checks only: opens every group of the list, now and after each refresh.
    void setGroupsExpanded(bool expanded);
    // For checks only: switches the modular kit on in the project's export options, as the checkbox does.
    void setKitEnabled(bool enabled);
    // For checks only: switches the gameplay data file on in the project's export options, as the checkbox does.
    void setGameplayEnabled(bool enabled);

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void exportRequested();
    void checkRequested();
    void exportAnywayRequested();
    void buildRequested();
    void openFolderRequested();
    // A finding was clicked: the plan id of its object.
    void findingActivated(const QString& planId);
    // The groups were edited, an options file appeared or changed, or the tab was shown: ask for a new review.
    void refreshRequested();

private:
    QWidget* addSection(const QString& title);
    void addRow(QWidget* host, const QString& name, const QString& value, bool mono);
    QPushButton* addButton(QWidget* row, const QString& text, const QString& tip, const QString& tourId, bool primary);
    void addChecks(const plynth::ExportReport& report);
    void addGroups(const ExportPanelState& state);
    void addKitSwitch(const ExportPanelState& state);
    void addGameplaySwitch(const ExportPanelState& state);
    void addGameplayReport(const plynth::ExportReport& report);
    void addLockLine();
    void addOptionsProblem(const ExportPanelState& state);
    void resetOptions();
    void updateLock();
    void lookAgain();
    void askForReview();
    void afterWrite();
    void addKitReport(const plynth::ExportReport& report);
    void fitGroupsTree();
    void fitGroupsColumns();
    void watchFolder(const std::filesystem::path& folder);

    QVBoxLayout* column_ = nullptr;
    QWidget* body_ = nullptr;
    QHash<QString, bool> expanded_;
    plynth::ExportEditGuard guard_; // the one judge of every write to export-options.json
    ExportGroupsList* groups_ = nullptr;
    bool stale_ = false;        // the last review was out of date, so the old body is shown greyed
    bool refreshDue_ = false;   // a refresh waits for the name editor to close
    bool refreshAsked_ = false; // a request for a new review is on its way
    QPointer<QCheckBox> kitBox_;
    QPointer<QCheckBox> gameplayBox_;
    QPointer<QPushButton> repairButton_;
    QPointer<QLabel> lockLine_; // the guard's message where the list is not shown
    QTreeWidget* groupsTree_ = nullptr;
    plynth::ExportGroupReview review_;
    bool groupsExpanded_ = false;
    bool groupsExpandedDone_ = false;
    QFileSystemWatcher* watcher_ = nullptr;
    QTimer* watchTimer_ = nullptr;
    QTimer* lockTimer_ = nullptr; // looks again while something is locked
    std::filesystem::path watched_;
    std::filesystem::path projectFolder_;
    QString kitError_; // why the last change of the kit switch was not saved; empty when it was
    QString gameplayError_; // the same for the gameplay data switch
};

}
