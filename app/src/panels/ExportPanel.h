#pragma once

#include <plynth/core/Export.h>
#include <plynth/core/ExportEditGuard.h>
#include <plynth/core/ExportGroups.h>

#include "links/UnrealLinkSection.h"

#include <QHash>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <filesystem>

class QCheckBox;
class QFileSystemWatcher;
class QHideEvent;
class QLabel;
class QPushButton;
class QShowEvent;
class QTimer;
class QTreeWidget;
class QVBoxLayout;

namespace plynth::app {

class ExportGroupsList;

// What the Export window needs to know besides the report.
struct ExportPanelState {
    bool running = false;      // an export or a build is running, so the buttons wait
    bool canRun = false;       // there is a build and setup is fine
    QString notice;            // why it cannot run, shown above the report; empty when it can
    bool offerBuild = false;   // show the Build button beside the notice
    QString error;             // the sentence of the last export that failed before it had a report
    bool folderExists = false; // the export folder is there to open
    bool grouped = false;      // the project exports one FBX for each group
    bool built = false;        // the project has a build, so its groups can be shown
    bool reportOutOfDate = false; // the last export was made from an older build than the one there now
    QString optionsProblem;    // why export-options.json cannot be read; empty when it can
    bool optionsRepairable = false; // the bad file may be replaced; false for one written by a newer Plynth
    bool licensesEnabled = false; // export-options.json switches the license report on
    QString licensesFile;      // where the report goes, export/<Stem>_licenses.json; empty without a folder
    std::filesystem::path projectFolder;               // empty when the project is not saved
    const plynth::Project* project = nullptr;          // used only during setReport; null when there is no project
    const plynth::ExportGroupReview* review = nullptr; // as core gave it, in every mode; null without a project folder
};

// The Export window. Its first part is the plain FBX export for any tool: the Export and Check buttons, the
// groups, the last export and the list of quality checks. The Unreal Engine link section goes below it.
class ExportPanel : public QWidget {
    Q_OBJECT
public:
    explicit ExportPanel(QWidget* parent = nullptr);
    ~ExportPanel() override;

    // Shows a report, or the empty text when it is null.
    void setReport(const plynth::ExportReport* report, const ExportPanelState& state);
    // For checks only: opens every group of the list, now and after each refresh.
    void setGroupsExpanded(bool expanded);
    // Switches the license report on or off in the project's export options, as the checkbox does.
    void setLicensesEnabled(bool enabled);
    // Switches the modular kit or the gameplay data of the Unreal Engine link, as its boxes do.
    void setKitEnabled(bool enabled);
    void setGameplayEnabled(bool enabled);
    // Gives the Unreal Engine section the window's project and settings for a send or the sync after a build.
    void setLinkContext(LinkContextSource source);
    // Updates the linked Unreal project after a good build of the app.
    void syncLinksAfterBuild();
    // For screenshots: the Unreal Engine section knows whether the editor is open.
    bool linkPictureReady() const;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void exportRequested();
    void checkRequested();
    void exportAnywayRequested();
    void buildRequested();
    void openFolderRequested();
    // A finding was clicked: the plan id of its object.
    void findingActivated(const QString& planId);
    // The groups were edited, an options file appeared or changed, or the window was shown: ask for a new review.
    void refreshRequested();
    // A line from the Unreal Engine link for the status bar. refusal shows it in amber.
    void linkMessage(const QString& text, bool refusal);

private:
    QWidget* addSection(const QString& title);
    void addRow(QWidget* host, const QString& name, const QString& value, bool mono);
    QPushButton* addButton(QWidget* row, const QString& text, const QString& tip, bool primary);
    void addIntro();
    void addPlainExport(const plynth::ExportReport* report, const ExportPanelState& state);
    void addUnrealLink(const ExportPanelState& state);
    void addChecks(const plynth::ExportReport& report);
    void addGroups(const ExportPanelState& state);
    void addLicensesSwitch(const ExportPanelState& state);
    void addLicensesReport(const plynth::ExportReport& report);
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
    UnrealLinkSection* unreal_ = nullptr; // kept across redraws, like the groups list
    bool stale_ = false;        // the last review was out of date, so the old body is shown greyed
    bool refreshDue_ = false;   // a refresh waits for the name editor to close
    bool refreshAsked_ = false; // a request for a new review is on its way
    QPointer<QCheckBox> licensesBox_;
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
    QString licensesError_; // why the last change of the license report switch was not saved; empty when it was
};

}
