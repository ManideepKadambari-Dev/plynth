#include "UnrealLinkSection.h"

#include "links/LinkRunner.h"
#include "theme/ElideLabel.h"
#include "theme/PlainText.h"
#include "theme/Theme.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/ExportGroups.h>
#include <plynth/core/UnrealLink.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>
#include <system_error>
#include <utility>

namespace plynth::app {

namespace {

constexpr int kPollMs = 4000;
const std::string kLinkId(plynth::kUnrealLinkId);

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// A path in the form the owner sees in Explorer.
QString nativePath(const std::filesystem::path& path)
{
    return QDir::toNativeSeparators(QString::fromStdWString(path.wstring()));
}

// A folder in one form for comparing, so the same project folder written two ways still matches.
QString folderKey(const std::filesystem::path& folder)
{
    return QDir::cleanPath(QString::fromStdWString(folder.wstring()).replace(QLatin1Char('\\'), QLatin1Char('/'))).toLower();
}

// True when both name the same Unreal project folder, as core decides where a sync may go.
bool sameProject(const plynth::EngineTarget& a, const plynth::EngineTarget& b)
{
    return !a.projectRoot.empty() && folderKey(a.projectRoot) == folderKey(b.projectRoot);
}

// The name of an editor's project, or its folder when it gave no name.
QString projectOf(const plynth::EngineTarget& target)
{
    return target.projectName.empty() ? nativePath(target.projectRoot) : fromCore(target.projectName);
}

// "1 mesh" or "3 meshes". many holds %1 for the number.
QString countText(int n, const QString& one, const QString& many)
{
    return n == 1 ? one : many.arg(n);
}

// Says whether a command line session has the project, so the app may not write its files.
bool sessionActive(const std::filesystem::path& folder)
{
    return plynth::readSession(folder, plynth::sessionNow()).state == plynth::SessionState::Active;
}

// Says whether link_report.json of this send is on disk.
bool reportWritten(const plynth::EngineLinkReport& report)
{
    std::error_code ignored;
    return !report.reportFile.empty() && std::filesystem::exists(report.reportFile, ignored);
}

}

// Builds the empty section and its runner.
UnrealLinkSection::UnrealLinkSection(plynth::ExportEditGuard& guard, QWidget* parent) : QWidget(parent), guard_(guard)
{
    const Theme& t = Theme::instance();
    column_ = new QVBoxLayout(this);
    column_->setContentsMargins(0, t.unit() * 3, 0, 0);
    column_->setSpacing(t.unit() * 2);
    runner_ = new LinkRunner(this);
    connect(runner_, &LinkRunner::searched, this, &UnrealLinkSection::searched);
    connect(runner_, &LinkRunner::sent, this, &UnrealLinkSection::sent);
    connect(runner_, &LinkRunner::synced, this, &UnrealLinkSection::synced);
    connect(runner_, &LinkRunner::progress, this, [this](const QString& text) {
        progress_ = text;
        if (progressLabel_)
            progressLabel_->setText(text);
    });
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(kPollMs);
    connect(pollTimer_, &QTimer::timeout, this, [this]() {
        if (!runner_->running())
            lookForEditors();
    });
    rebuild();
}

// The runner stops its workers when it goes with the section.
UnrealLinkSection::~UnrealLinkSection() = default;

// Takes the project and the window state, reads the link's options and last report, and redraws.
void UnrealLinkSection::setState(const UnrealSectionState& state)
{
    if (state.projectFolder != state_.projectFolder) {
        error_.clear();
        syncLine_.clear();
        chosenId_.clear();
        fresh_.reset();
    }
    state_ = state;
    readOptions();
    readReport();
    rebuild();
}

// Greys the switches and buttons, or lets them follow the state again.
void UnrealLinkSection::setLocked(bool locked)
{
    locked_ = locked;
    updateEnabled();
}

// Keeps the function that gives the project and settings for a send.
void UnrealLinkSection::setContextSource(LinkContextSource source)
{
    context_ = std::move(source);
}

// Starts or stops looking for the editor every few seconds.
void UnrealLinkSection::setWatching(bool on)
{
    watching_ = on;
    if (on) {
        lookForEditors();
        pollTimer_->start();
    } else {
        pollTimer_->stop();
    }
}

// Starts one search, unless one runs.
void UnrealLinkSection::lookForEditors()
{
    // Without the link there is nothing to wait for, so a screenshot does not wait for an answer.
    if (!runner_->search(kLinkId) && !runner_->searching())
        searchedOnce_ = true;
}

// For screenshots: the status is known, or the section is not shown.
bool UnrealLinkSection::pictureReady() const
{
    return !watching_ || searchedOnce_;
}

// Reads the link's switches from export-options.json, or why it cannot be read.
void UnrealLinkSection::readOptions()
{
    options_ = plynth::EngineLinkOptions();
    optionsProblem_.clear();
    if (state_.projectFolder.empty())
        return;
    const plynth::ExportOptionsRead read = plynth::readExportOptions(state_.projectFolder);
    if (!read.status.ok()) {
        optionsProblem_ = fromCore(read.status.message);
        return;
    }
    options_ = plynth::engineLinkOptions(read.options, plynth::kUnrealLinkId);
}

// Reads build/links/unreal/link_report.json. The send of this session is kept when it is the one on disk, as only
// it still has the quality checks.
void UnrealLinkSection::readReport()
{
    report_.reset();
    if (state_.projectFolder.empty())
        return;
    const plynth::EngineLinkReportRead read = plynth::readEngineLinkReport(state_.projectFolder, plynth::kUnrealLinkId);
    if (!read.status.ok())
        return;
    if (fresh_ && fresh_->jobId == read.report.jobId && fresh_->startedAt == read.report.startedAt)
        report_ = fresh_;
    else
        report_ = read.report;
}

// The editors that have a project open, the only ones a send can go into.
std::vector<plynth::EngineEditor> UnrealLinkSection::openEditors() const
{
    std::vector<plynth::EngineEditor> out;
    if (!search_)
        return out;
    for (const plynth::EngineEditor& editor : search_->editors)
        if (!editor.target.projectName.empty() || !editor.target.projectRoot.empty())
            out.push_back(editor);
    return out;
}

// The editor a send goes into, chosen the way core chooses: the linked project when it is open, else the only
// one, else the one picked in the list.
std::optional<plynth::EngineEditor> UnrealLinkSection::chosenEditor() const
{
    const std::vector<plynth::EngineEditor> open = openEditors();
    if (options_.target)
        for (const plynth::EngineEditor& editor : open)
            if (sameProject(*options_.target, editor.target))
                return editor;
    if (open.size() == 1)
        return open.front();
    for (const plynth::EngineEditor& editor : open)
        if (editor.id == chosenId_)
            return editor;
    if (!open.empty())
        return open.front();
    return std::nullopt;
}

// True when several projects are open and none is the linked one, so the user picks.
bool UnrealLinkSection::needsPicker() const
{
    const std::vector<plynth::EngineEditor> open = openEditors();
    if (open.size() < 2)
        return false;
    if (options_.target)
        for (const plynth::EngineEditor& editor : open)
            if (sameProject(*options_.target, editor.target))
                return false;
    return true;
}

// Takes the editors found. The section only redraws when they differ, so an open list is not closed every poll.
void UnrealLinkSection::searched(const plynth::EngineEditorSearch& search)
{
    searchedOnce_ = true;
    const bool same = search_ && search_->status.code == search.status.code &&
                      search_->status.message == search.status.message && search_->editors == search.editors;
    search_ = search;
    if (!same)
        rebuild();
}

// Hides and frees every widget of the section. Deleting later keeps a button that is being clicked alive.
void UnrealLinkSection::clear()
{
    while (QLayoutItem* item = column_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    switches_.clear();
    progressLabel_ = nullptr;
    sendButton_ = nullptr;
    anywayButton_ = nullptr;
    unlinkButton_ = nullptr;
    stopButton_ = nullptr;
    picker_ = nullptr;
}

// Draws the whole section from what it knows.
void UnrealLinkSection::rebuild()
{
    clear();
    addHeading();
    addEditorStatus();
    addTargetLine();
    addSwitches();
    addButtons();
    const Theme& t = Theme::instance();
    if (!state_.notice.isEmpty())
        addLine(state_.notice, t.muted(), false, tr("Why Send to Unreal cannot start yet"));
    if (!optionsProblem_.isEmpty())
        addLine(optionsProblem_, t.signalAmber(), false, tr("Why the link's switches cannot be read"));
    progressLabel_ = addLine(progress_, t.muted(), true, tr("What the send or the update after a build is doing"));
    progressLabel_->setVisible(runner_->running());
    if (!error_.isEmpty())
        addLine(error_, t.signalAmber(), true, tr("Why the last change, send or unlink did not work"));
    if (!syncLine_.isEmpty())
        addLine(tr("After the last build: %1").arg(syncLine_), syncRefusal_ ? t.signalAmber() : t.muted(), true,
                tr("What the update after the last good build did"));
    addLastSend();
    updateEnabled();
}

// Adds a line of plain text in a theme color.
QLabel* UnrealLinkSection::addLine(const QString& text, const QColor& color, bool smallText, const QString& tip)
{
    const Theme& t = Theme::instance();
    auto* label = new QLabel(this);
    label->setTextFormat(Qt::PlainText);
    label->setText(text);
    label->setWordWrap(true);
    label->setFont(t.uiFont(smallText ? t.sizeSmall() : t.sizeNormal()));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
    label->setToolTip(tip);
    column_->addWidget(label);
    return label;
}

// Adds the divider, the title and the line that says what the link does.
void UnrealLinkSection::addHeading()
{
    const Theme& t = Theme::instance();
    auto* rule = new QFrame(this);
    rule->setFixedHeight(1);
    rule->setStyleSheet(QStringLiteral("background: %1;").arg(t.line().name()));
    column_->addWidget(rule);
    auto* title = new QLabel(tr("Unreal Engine"), this);
    title->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color: %1; padding-top: %2px;").arg(t.text().name()).arg(t.unit()));
    title->setToolTip(tr("The Unreal Engine link: sends the building into the open Unreal project and keeps it up to date"));
    column_->addWidget(title);
    const QString building = fromCore(plynth::unrealBuildingName(state_.projectName));
    addLine(tr("Puts the building into the open Unreal project under Content/Plynth/%1/, with Unreal names and "
               "collision, and updates it there after every good build.")
                .arg(building),
            t.muted(), true, tr("What the Unreal Engine link does"));
}

// Adds whether Unreal Engine is open and with which project, or how to make it reachable.
void UnrealLinkSection::addEditorStatus()
{
    const Theme& t = Theme::instance();
    if (!search_) {
        addLine(tr("Looking for Unreal Engine."), t.muted(), false, tr("Plynth asks the Unreal editors on this computer"));
        return;
    }
    if (!search_->status.ok()) {
        addLine(fromCore(search_->status.message), t.signalAmber(), false, tr("Why Plynth could not look for Unreal Engine"));
        return;
    }
    const std::vector<plynth::EngineEditor> open = openEditors();
    const QString howTo = tr("In Unreal, turn on the Python Editor Script Plugin under Edit > Plugins, then tick Enable "
                             "Remote Execution under Project Settings > Plugins > Python.");
    if (open.empty()) {
        auto* line = addLine(search_->editors.empty() ? tr("Unreal Engine is not open") : tr("Unreal Engine is open without a project"),
                             t.text(), false, tr("No Unreal editor with a project answered. Plynth looks again every few seconds"));
        line->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
        addLine(search_->editors.empty() ? howTo : tr("Open the project the building should go into."), t.muted(), true,
                tr("What Unreal needs so Plynth can reach it. Plynth installs nothing into Unreal"));
        return;
    }
    const std::optional<plynth::EngineEditor> chosen = chosenEditor();
    if (!needsPicker() && chosen) {
        auto* line = addLine(tr("Unreal Engine is open: %1, version %2")
                                 .arg(projectOf(chosen->target), fromCore(chosen->target.engineVersion)),
                             t.signalBlue(), false, plainToolTip(nativePath(chosen->target.projectRoot)));
        line->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
        return;
    }
    auto* line = addLine(tr("Unreal Engine is open with %1 projects").arg(open.size()), t.signalBlue(), false,
                         tr("Several Unreal editors answered. Pick the project to send to"));
    line->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(t.unit() * 2);
    auto* label = new QLabel(tr("Send to"), row);
    label->setFont(t.uiFont(t.sizeNormal()));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    layout->addWidget(label);
    auto* picker = new QComboBox(row);
    picker_ = picker;
    picker->setFont(t.uiFont(t.sizeNormal()));
    picker->setToolTip(tr("The open Unreal project the first send goes into. Later builds update only that project"));
    for (const plynth::EngineEditor& editor : open) {
        picker->addItem(tr("%1, version %2").arg(projectOf(editor.target), fromCore(editor.target.engineVersion)),
                        fromCore(editor.id));
        if (chosen && editor.id == chosen->id)
            picker->setCurrentIndex(picker->count() - 1);
    }
    // Connected after the list is filled, as filling it changes the current entry too.
    connect(picker, &QComboBox::currentIndexChanged, this, [this, picker](int index) {
        if (index < 0)
            return;
        chosenId_ = picker->itemData(index).toString().toStdString();
        QTimer::singleShot(0, this, &UnrealLinkSection::rebuild);
    });
    layout->addWidget(picker, 1);
    switches_.append(picker);
    column_->addWidget(row);
}

// Adds where the building goes: the project of the first send, or the linked project.
void UnrealLinkSection::addTargetLine()
{
    const Theme& t = Theme::instance();
    const QString building = fromCore(plynth::unrealBuildingName(state_.projectName));
    const std::optional<plynth::EngineEditor> chosen = chosenEditor();
    if (options_.target) {
        const QString linked = projectOf(*options_.target);
        if (chosen && !sameProject(*options_.target, chosen->target)) {
            addLine(tr("Linked to %1, but Unreal Engine has %2 open. Send puts the building into %2 and links it there.")
                        .arg(linked, projectOf(chosen->target)),
                    t.signalAmber(), true, tr("Builds update only the linked project, and only while it is open"));
            return;
        }
        addLine(tr("Linked to %1. Every good build updates Content/Plynth/%2/ while it is open.").arg(linked, building),
                t.text(), true, tr("Builds update only the linked project. Unlink stops that"));
        return;
    }
    if (chosen) {
        addLine(tr("Sends to %1, into Content/Plynth/%2/.").arg(projectOf(chosen->target), building), t.text(), true,
                tr("The first send links the building to this project, so later builds update it"));
        return;
    }
    addLine(tr("Not linked yet. The first send puts the building into Content/Plynth/%1/ of the open project.").arg(building),
            t.muted(), true, tr("Nothing is sent until you press Send to Unreal"));
}

// Adds the two choices of the link and the modular kit and gameplay data switches.
void UnrealLinkSection::addSwitches()
{
    const Theme& t = Theme::instance();
    auto addBox = [&](const QString& text, bool on, const QString& tip, std::function<void(bool)> onClick) {
        auto* box = new QCheckBox(text, this);
        box->setFont(t.uiFont(t.sizeNormal()));
        box->setChecked(on);
        box->setToolTip(tip);
        connect(box, &QCheckBox::clicked, this, [onClick](bool now) { onClick(now); }, Qt::QueuedConnection);
        column_->addWidget(box);
        switches_.append(box);
    };
    addBox(tr("Pack with materials"), options_.packMaterials,
           tr("On: the materials and textures go to Unreal as ready material assets on the meshes. Off: meshes only, "
              "with Unreal's default material in each named slot"),
           [this](bool on) { saveChoice(true, on); });
    addBox(tr("Grouped"), options_.grouped.value_or(state_.projectGrouped),
           tr("On: furniture and the other groups arrive as separate assets placed in the level, so they can be moved "
              "in Unreal. Off: one building asset. It starts from the project's export mode"),
           [this](bool on) { saveChoice(false, on); });
    addBox(tr("Also send the modular kit"), options_.kit,
           tr("Also send every unique wall, corner and opening piece once, with collision, and a layout file saying "
              "where each goes"),
           [this](bool on) { setKitEnabled(on); });
    addBox(tr("Also send gameplay data"), options_.gameplay,
           tr("Also send a data file with room volumes, door positions and light positions, for audio zones, "
              "navigation and spawn points"),
           [this](bool on) { setGameplayEnabled(on); });
}

// Makes a button in a row. The primary one is filled with Signal blue.
QPushButton* UnrealLinkSection::makeButton(QWidget* row, const QString& text, const QString& tip, bool primary)
{
    const Theme& t = Theme::instance();
    auto* button = new QPushButton(text, row);
    button->setToolTip(tip);
    if (primary) {
        button->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
        button->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %1; "
                                             "border-radius: %3px; padding: %4px %5px; }"
                                             "QPushButton:disabled { background: %6; color: %7; border-color: %8; }")
                                  .arg(t.signalBlue().name(), t.onBlue().name())
                                  .arg(t.radius())
                                  .arg(t.unit())
                                  .arg(t.unit() * 5)
                                  .arg(t.raised().name(), t.muted().name(), t.line().name()));
    }
    row->layout()->addWidget(button);
    return button;
}

// Adds Send to Unreal, Send anyway after a blocked send, Unlink, Look again and Stop while a send runs.
void UnrealLinkSection::addButtons()
{
    const Theme& t = Theme::instance();
    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(t.unit() * 2);
    const std::optional<plynth::EngineEditor> chosen = chosenEditor();
    const QString building = fromCore(plynth::unrealBuildingName(state_.projectName));
    const QString sendTip = chosen ? tr("Put the building into %1 under Content/Plynth/%2/ and link it, so every good "
                                        "build updates it")
                                         .arg(projectOf(chosen->target), building)
                                   : tr("Put the building into the open Unreal project. Open Unreal first");
    sendButton_ = makeButton(row, tr("Send to Unreal"), sendTip, true);
    connect(sendButton_, &QPushButton::clicked, this, [this]() { send(false); }, Qt::QueuedConnection);
    if (report_ && report_->status.code == plynth::ErrorCode::QualityFailed) {
        anywayButton_ = makeButton(row, tr("Send anyway..."),
                                   tr("Send with the failed checks left as they are. Asks first"), false);
        connect(anywayButton_, &QPushButton::clicked, this, &UnrealLinkSection::sendAnyway, Qt::QueuedConnection);
    }
    unlinkButton_ = makeButton(row, tr("Unlink"),
                               tr("Forget the linked Unreal project, so builds no longer update it. What is already in "
                                  "Unreal stays as it is"),
                               false);
    connect(unlinkButton_, &QPushButton::clicked, this, &UnrealLinkSection::unlink, Qt::QueuedConnection);
    QPushButton* again = makeButton(row, tr("Look again"), tr("Ask the Unreal editors on this computer again"), false);
    connect(again, &QPushButton::clicked, this, &UnrealLinkSection::lookForEditors, Qt::QueuedConnection);
    stopButton_ = makeButton(row, tr("Stop"), tr("Stop the send. What is already in Unreal stays as it was"), false);
    connect(stopButton_, &QPushButton::clicked, runner_, &LinkRunner::cancel);
    layout->addStretch(1);
    column_->addWidget(row);
}

// Adds what the last send did: when, the result, where it went, what it sent and the checks.
void UnrealLinkSection::addLastSend()
{
    if (!report_)
        return;
    const Theme& t = Theme::instance();
    const plynth::EngineLinkReport& r = *report_;
    QWidget* grid = addGrid(tr("Last send"));
    const QDateTime when = QDateTime::fromSecsSinceEpoch(r.startedAt.time_since_epoch().count()).toLocalTime();
    addRow(grid, tr("At"), when.toString(QStringLiteral("yyyy-MM-dd HH:mm")), true);
    addRow(grid, tr("Took"), tr("%1 s").arg(r.elapsedMs / 1000.0, 0, 'f', 1), true);
    const bool blocked = r.status.code == plynth::ErrorCode::QualityFailed;
    addRow(grid, tr("Result"),
           r.status.ok()  ? (r.exported.forced ? tr("Sent past failed checks") : tr("Sent"))
           : blocked      ? tr("Blocked, nothing sent")
                          : fromCore(r.status.message),
           false);
    if (r.target)
        addRow(grid, tr("Project"),
               tr("%1, version %2").arg(projectOf(*r.target), fromCore(r.target->engineVersion)), false);
    if (!r.contentPath.empty())
        addRow(grid, tr("Folder"), fromCore(r.contentPath), true);
    addRow(grid, tr("Choices"),
           QStringList{r.packMaterials ? tr("Pack with materials") : tr("Meshes only"),
                       r.grouped ? tr("Grouped") : tr("One building asset")}
               .join(QStringLiteral(", ")),
           false);

    int meshes = 0, textures = 0, materials = 0, files = 0;
    int created = 0, updated = 0, unchanged = 0, removed = 0;
    for (const plynth::EngineAssetResult& asset : r.assets) {
        if (asset.action == "removed") {
            ++removed;
            continue;
        }
        if (asset.action == "created")
            ++created;
        else if (asset.action == "updated")
            ++updated;
        else
            ++unchanged;
        if (asset.kind == "static_mesh")
            ++meshes;
        else if (asset.kind == "texture")
            ++textures;
        else if (asset.kind == "material" || asset.kind == "material_instance")
            ++materials;
        else
            ++files;
    }
    if (!r.assets.empty()) {
        QStringList sentParts{countText(meshes, tr("1 mesh"), tr("%1 meshes"))};
        if (textures > 0)
            sentParts.append(countText(textures, tr("1 texture"), tr("%1 textures")));
        if (materials > 0)
            sentParts.append(countText(materials, tr("1 material"), tr("%1 materials")));
        if (files > 0)
            sentParts.append(countText(files, tr("1 data file"), tr("%1 data files")));
        addRow(grid, tr("Sent"), sentParts.join(QStringLiteral(", ")), false);
        QStringList changes;
        if (created > 0)
            changes.append(tr("%1 new").arg(created));
        if (updated > 0)
            changes.append(tr("%1 updated in place").arg(updated));
        if (unchanged > 0)
            changes.append(tr("%1 unchanged").arg(unchanged));
        if (removed > 0)
            changes.append(tr("%1 removed").arg(removed));
        addRow(grid, tr("Assets"), changes.join(QStringLiteral(", ")), false);
    }
    if (!r.actors.empty()) {
        int spawned = 0, kept = 0, gone = 0;
        for (const plynth::EngineActorResult& actor : r.actors) {
            if (actor.action == "spawned")
                ++spawned;
            else if (actor.action == "kept")
                ++kept;
            else
                ++gone;
        }
        QStringList parts;
        if (spawned > 0)
            parts.append(tr("%1 placed").arg(spawned));
        if (kept > 0)
            parts.append(tr("%1 kept where they were").arg(kept));
        if (gone > 0)
            parts.append(tr("%1 removed").arg(gone));
        addRow(grid, tr("In the level"), parts.join(QStringLiteral(", ")), false);
    }
    if (!r.exported.checks.empty()) {
        int pass = 0, warn = 0, failed = 0;
        for (const plynth::QualityCheckResult& check : r.exported.checks) {
            if (check.status == plynth::QualityStatus::Pass)
                ++pass;
            else if (check.status == plynth::QualityStatus::Warning)
                ++warn;
            else if (check.status == plynth::QualityStatus::Fail)
                ++failed;
        }
        addRow(grid, tr("Checks"),
               tr("%1 passed, %2, %3")
                   .arg(pass)
                   .arg(countText(warn, tr("1 warning"), tr("%1 warnings")), tr("%1 failed").arg(failed)),
               false);
    }
    if (!r.checks.empty()) {
        int pass = 0;
        for (const plynth::EngineCheckResult& check : r.checks)
            if (check.status == "pass")
                ++pass;
        addRow(grid, tr("Unreal checks"), tr("%1 of %2 passed").arg(pass).arg(r.checks.size()), false);
    }
    if (!r.reportFile.empty())
        addRow(grid, tr("Report"), nativePath(r.reportFile), true);
    if (blocked) {
        QStringList failedNames;
        for (const plynth::QualityCheckResult& check : r.exported.checks)
            if (check.status == plynth::QualityStatus::Fail)
                failedNames.append(fromCore(check.name));
        addLine(failedNames.isEmpty() ? tr("Some quality checks failed, so nothing was sent. Press Check above to see them.")
                                      : tr("Not sent because these checks failed: %1.").arg(failedNames.join(QStringLiteral(", "))),
                t.signalAmber(), true, tr("The same quality checks as the plain export, with the Unreal rules"));
    }
}

// Starts a titled grid of names and values and returns it.
QWidget* UnrealLinkSection::addGrid(const QString& title)
{
    const Theme& t = Theme::instance();
    auto* heading = new QLabel(title, this);
    heading->setFont(t.uiFont(t.sizeSmall(), QFont::DemiBold));
    heading->setStyleSheet(QStringLiteral("color: %1; padding-top: %2px;").arg(t.muted().name()).arg(t.unit() * 2));
    heading->setToolTip(tr("What the last send to Unreal did"));
    column_->addWidget(heading);
    auto* host = new QWidget(this);
    auto* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(t.unit() * 3);
    grid->setVerticalSpacing(t.unit());
    grid->setColumnStretch(1, 1);
    column_->addWidget(host);
    return host;
}

// Adds a name and a value in the next row of a grid. Numbers and paths use the mono font.
void UnrealLinkSection::addRow(QWidget* host, const QString& name, const QString& value, bool mono)
{
    const Theme& t = Theme::instance();
    auto* grid = static_cast<QGridLayout*>(host->layout());
    const int row = grid->rowCount();
    auto* label = new QLabel(name, host);
    label->setFont(t.uiFont(t.sizeNormal()));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    auto* shown = new ElideLabel(value, host);
    shown->setFont(mono ? t.monoFont(t.sizeNormal()) : t.uiFont(t.sizeNormal()));
    shown->setToolTip(plainToolTip(value));
    grid->addWidget(label, row, 0);
    grid->addWidget(shown, row, 1);
}

// Lets each switch and button follow the lock, the window's work, the link's own run and the editor search.
void UnrealLinkSection::updateEnabled()
{
    const bool running = runner_->running();
    const bool usable = !locked_ && !state_.projectFolder.empty() && optionsProblem_.isEmpty();
    for (const QPointer<QWidget>& widget : switches_)
        if (widget)
            widget->setEnabled(usable && !running);
    const bool canSend = usable && !running && !state_.running && state_.notice.isEmpty() && chosenEditor().has_value();
    if (sendButton_)
        sendButton_->setEnabled(canSend);
    if (anywayButton_)
        anywayButton_->setEnabled(canSend);
    if (unlinkButton_)
        unlinkButton_->setEnabled(usable && !running && options_.target.has_value());
    if (stopButton_)
        stopButton_->setVisible(running);
    if (progressLabel_)
        progressLabel_->setVisible(running && !progress_.isEmpty());
}

// Writes Pack with materials or Grouped through core, unless a command line session has the project.
void UnrealLinkSection::saveChoice(bool packMaterials, bool on)
{
    if (state_.projectFolder.empty())
        return;
    if (sessionActive(state_.projectFolder)) {
        error_ = fromCore(std::string(plynth::kExportSessionLockMessage));
        emit changed();
        return;
    }
    plynth::ExportOptionsRead read = plynth::readExportOptions(state_.projectFolder);
    if (!read.status.ok()) {
        error_ = fromCore(read.status.message);
        emit changed();
        return;
    }
    plynth::EngineLinkOptions link = plynth::engineLinkOptions(read.options, plynth::kUnrealLinkId);
    if (packMaterials)
        link.packMaterials = on;
    else
        link.grouped = on;
    read.options.links[kLinkId] = link;
    const plynth::Status written = plynth::writeExportOptions(state_.projectFolder, read.options);
    error_ = written.ok() ? QString() : fromCore(written.message);
    emit changed();
}

// Takes the guard's answer to a kit or gameplay switch: a refusal or a failed write is shown in the section.
void UnrealLinkSection::tookGuardResult(const plynth::ExportEditResult& result, const QString& what)
{
    if (result.decision != plynth::ExportEditDecision::Allowed) {
        const std::string reason = guard_.message();
        error_ = tr("%1 was not saved: %2")
                     .arg(what, fromCore(reason.empty() ? std::string(plynth::kExportSessionLockMessage) : reason));
    } else if (!result.status.ok()) {
        error_ = tr("%1 was not saved: %2").arg(what, fromCore(result.status.message));
    } else {
        error_.clear();
    }
    emit changed();
}

// Switches the modular kit of the link through the guard.
void UnrealLinkSection::setKitEnabled(bool enabled)
{
    tookGuardResult(guard_.setKit(plynth::sessionNow(), enabled), tr("The modular kit switch"));
}

// Switches the gameplay data of the link through the guard.
void UnrealLinkSection::setGameplayEnabled(bool enabled)
{
    tookGuardResult(guard_.setGameplay(plynth::sessionNow(), enabled), tr("The gameplay data switch"));
}

// Forgets the linked project through core. The assets in Unreal stay.
void UnrealLinkSection::unlink()
{
    if (state_.projectFolder.empty())
        return;
    if (sessionActive(state_.projectFolder)) {
        error_ = fromCore(std::string(plynth::kExportSessionLockMessage));
        emit changed();
        return;
    }
    const plynth::Status done = plynth::unlinkEngine(state_.projectFolder, plynth::kUnrealLinkId);
    error_ = done.ok() ? QString() : fromCore(done.message);
    emit message(done.ok() ? tr("Unlinked from Unreal Engine. Builds no longer update it.") : error_, !done.ok());
    emit changed();
}

// Starts a send of the project as it is in memory.
void UnrealLinkSection::send(bool force)
{
    if (!context_ || runner_->running())
        return;
    LinkContext context = context_();
    if (context.projectFolder.empty())
        return;
    if (sessionActive(context.projectFolder)) {
        error_ = fromCore(std::string(plynth::kExportSessionLockMessage));
        rebuild();
        return;
    }
    plynth::EngineLinkRequest request;
    request.projectFolder = context.projectFolder;
    request.project = context.project;
    request.link = kLinkId;
    request.library = context.library;
    request.settings.write = true;
    request.settings.force = force;
    if (needsPicker())
        if (const std::optional<plynth::EngineEditor> chosen = chosenEditor())
            request.editor = chosen->id;
    error_.clear();
    progress_ = tr("Starting");
    runner_->send(std::move(request), std::move(context.settings));
    rebuild();
}

// Asks first, then sends with the failed checks of the last send left as they are.
void UnrealLinkSection::sendAnyway()
{
    if (!report_ || runner_->running())
        return;
    QStringList failed;
    for (const plynth::QualityCheckResult& check : report_->exported.checks)
        if (check.status == plynth::QualityStatus::Fail)
            failed.append(fromCore(check.name));
    const QString which = failed.isEmpty() ? tr("Some quality checks failed.")
                                           : tr("These checks failed: %1.").arg(failed.join(QStringLiteral(", ")));
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Send anyway"),
        tr("%1\n\nThe send will go past them and the report will say so. Send to Unreal anyway?").arg(which),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Yes)
        send(true);
}

// Takes the end of a send: keeps its report, says how it went in the status bar and looks for the editor again.
void UnrealLinkSection::sent(const plynth::EngineLinkReport& report)
{
    progress_.clear();
    if (reportWritten(report)) {
        fresh_ = report;
        error_.clear();
    } else if (!report.status.ok()) {
        error_ = fromCore(report.status.message);
    }
    if (report.status.ok()) {
        const QString project = report.target ? projectOf(*report.target) : tr("Unreal Engine");
        emit message(tr("Sent to Unreal: %1").arg(project), false);
    } else if (report.status.code == plynth::ErrorCode::QualityFailed) {
        const int fails = report.exported.failCount();
        emit message(fails == 1 ? tr("Not sent to Unreal: 1 quality check failed.")
                                : tr("Not sent to Unreal: %1 quality checks failed.").arg(fails),
                     true);
    } else {
        emit message(fromCore(report.status.message), report.status.code != plynth::ErrorCode::Cancelled);
    }
    readOptions();
    readReport();
    rebuild();
    emit changed();
    lookForEditors();
}

// Starts the update of the linked project after a good build. Nothing runs when the building was never sent.
void UnrealLinkSection::syncAfterBuild()
{
    if (!context_ || runner_->running())
        return;
    LinkContext context = context_();
    if (context.projectFolder.empty())
        return;
    const plynth::ExportOptionsRead read = plynth::readExportOptions(context.projectFolder);
    if (!read.status.ok())
        return;
    const plynth::EngineLinkOptions options = plynth::engineLinkOptions(read.options, plynth::kUnrealLinkId);
    if (!options.syncAfterBuild || !options.target)
        return;
    plynth::EngineLinkRequest base;
    base.projectFolder = context.projectFolder;
    base.project = context.project;
    base.library = context.library;
    base.settings.write = true;
    progress_ = tr("Updating in Unreal: %1").arg(projectOf(*options.target));
    runner_->sync(std::move(base), std::move(context.settings));
    rebuild();
}

// Takes the end of the sync after a build and puts one line in the status bar and in the section.
void UnrealLinkSection::synced(const std::vector<plynth::EngineLinkReport>& reports)
{
    progress_.clear();
    for (const plynth::EngineLinkReport& report : reports) {
        if (report.link != kLinkId)
            continue;
        QString text;
        bool refusal = false;
        if (report.skipped) {
            text = fromCore(report.skippedReason);
        } else if (report.status.ok()) {
            text = tr("Updated in Unreal: %1").arg(report.target ? projectOf(*report.target) : tr("Unreal Engine"));
        } else if (report.status.code == plynth::ErrorCode::QualityFailed) {
            const int fails = report.exported.failCount();
            text = fails == 1 ? tr("Not updated in Unreal: 1 quality check failed.")
                              : tr("Not updated in Unreal: %1 quality checks failed.").arg(fails);
            refusal = true;
        } else {
            text = tr("Not updated in Unreal: %1").arg(fromCore(report.status.message));
            refusal = true;
        }
        if (!report.skipped && reportWritten(report))
            fresh_ = report;
        syncLine_ = text;
        syncRefusal_ = refusal;
        emit message(text, refusal);
    }
    readOptions();
    readReport();
    rebuild();
    emit changed();
}

}
