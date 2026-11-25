#include "SetupCheckDialog.h"

#include "catalog/LibraryHost.h"
#include "preview/GraphicsProbe.h"
#include "theme/PlainText.h"
#include "theme/Theme.h"

#include <plynth/core/Library.h>
#include <plynth/core/Version.h>

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace plynth::app {

namespace {

constexpr int kDialogWidth = 760;
constexpr int kDialogHeight = 600;

// Turns a core sentence into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// A path for display, with a break allowed after each separator so long paths wrap.
QString displayPath(const std::filesystem::path& path)
{
    QString text = QDir::toNativeSeparators(QString::fromStdWString(path.wstring()));
    text.replace(QLatin1Char('\\'), QStringLiteral("\\​"));
    return text;
}

// The word shown for a state.
QString stateWord(plynth::CheckState state)
{
    switch (state) {
    case plynth::CheckState::Found:
        return QObject::tr("Found");
    case plynth::CheckState::Missing:
        return QObject::tr("Missing");
    case plynth::CheckState::TooOld:
        return QObject::tr("Too old");
    case plynth::CheckState::NeedsAttention:
        return QObject::tr("Needs attention");
    case plynth::CheckState::NotChecked:
        return QObject::tr("Not checked");
    }
    return QString();
}

// The theme color of a state word: blue when found, amber when the user should act, muted when not checked.
QColor stateColor(plynth::CheckState state)
{
    const Theme& t = Theme::instance();
    switch (state) {
    case plynth::CheckState::Found:
        return t.signalBlue();
    case plynth::CheckState::NotChecked:
        return t.muted();
    default:
        return t.signalAmber();
    }
}

// Makes a wrapped label with a color and font.
QLabel* textLabel(const QString& text, const QFont& font, const QColor& color, QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setText(text);
    label->setWordWrap(true);
    label->setFont(font);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    return label;
}

}

// Builds the summary, the scrolling list of rows and the two buttons, then starts the first check.
SetupCheckDialog::SetupCheckDialog(LibraryHost& host, QWidget* parent) : QDialog(parent), host_(host)
{
    const Theme& t = Theme::instance();
    setWindowTitle(tr("Setup check"));
    setModal(true);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(t.dialogMargin(), t.dialogMargin(), t.dialogMargin(), t.dialogMargin());
    outer->setSpacing(t.unit() * 3);

    summary_ = new QLabel(this);
    summary_->setFont(t.uiFont(t.sizeLarge(), QFont::DemiBold));
    summary_->setToolTip(tr("The result of the check"));
    outer->addWidget(summary_);
    note_ = textLabel(QString(), t.uiFont(t.sizeNormal()), t.muted(), this);
    note_->hide();
    outer->addWidget(note_);

    scroll_ = new QScrollArea(this);
    QScrollArea* scroll = scroll_;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
    rowsHost_ = new QWidget();
    rows_ = new QVBoxLayout(rowsHost_);
    rows_->setContentsMargins(0, 0, t.unit() * 2, 0);
    rows_->setSpacing(t.setupRowGap());
    scroll->setWidget(rowsHost_);
    outer->addWidget(scroll, 1);

    problem_ = textLabel(QString(), t.uiFont(t.sizeSmall()), t.signalAmber(), this);
    problem_->hide();
    outer->addWidget(problem_);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(t.unit() * 3);
    again_ = new QPushButton(tr("Check again"), this);
    again_->setToolTip(tr("Run every check again without restarting Plynth"));
    connect(again_, &QPushButton::clicked, this, &SetupCheckDialog::recheck);
    done_ = new QPushButton(tr("Done"), this);
    done_->setDefault(true);
    done_->setToolTip(tr("Close the setup check. When everything required is found, it will not open by itself again"));
    connect(done_, &QPushButton::clicked, this, &SetupCheckDialog::finish);
    buttons->addWidget(again_);
    buttons->addStretch(1);
    buttons->addWidget(done_);
    outer->addLayout(buttons);

    const QSize room = parent != nullptr ? parent->size() - QSize(t.dialogMargin() * 2, t.dialogMargin() * 2)
                                         : QSize(kDialogWidth, kDialogHeight);
    resize(std::min(kDialogWidth, std::max(room.width(), 480)), std::min(kDialogHeight, std::max(room.height(), 320)));
    recheck();
}

// Stops a running check before the dialog goes away.
SetupCheckDialog::~SetupCheckDialog()
{
    stop_.request_stop();
    worker_ = std::jthread();
}

// Runs the setup check on the worker thread and shows the result when it arrives.
void SetupCheckDialog::recheck()
{
    stop_.request_stop();
    stop_ = std::stop_source();
    const int generation = ++generation_;
    checking_ = true;
    updateSummary();
    const plynth::MachineSettings settings = host_.settings();
    plynth::SetupCheckOptions options;
    options.settingsExist = host_.settingsExisted();
    options.graphics = appGraphics();
    const std::stop_token token = stop_.get_token();
    worker_ = std::jthread([this, generation, settings, options, token]() {
        plynth::SetupReport report = plynth::runSetupCheck(settings, options, token);
        QMetaObject::invokeMethod(
            this,
            [this, generation, report]() {
                if (generation == generation_)
                    showReport(report);
            },
            Qt::QueuedConnection);
    });
}

// Shows a finished report.
void SetupCheckDialog::showReport(const plynth::SetupReport& report)
{
    report_ = report;
    checking_ = false;
    while (QLayoutItem* item = rows_->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    for (const plynth::SetupItem& item : report.items)
        addRow(item);
    rows_->addStretch(1);
    updateSummary();
}

// Sets the summary line, the note under it and the button states.
void SetupCheckDialog::updateSummary()
{
    const Theme& t = Theme::instance();
    again_->setEnabled(!checking_);
    if (checking_ || !report_) {
        summary_->setText(tr("Checking..."));
        summary_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
        return;
    }
    long long blocking = 0;
    for (const plynth::SetupItem& item : report_->items)
        if (item.blocks())
            ++blocking;
    if (blocking == 0) {
        summary_->setText(tr("Ready"));
        summary_->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalBlue().name()));
    } else {
        summary_->setText(blocking == 1 ? tr("1 item needs you") : tr("%1 items need you").arg(blocking));
        summary_->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
    }
    QString note;
    if (report_->firstRun)
        note = tr("This is the first run. Choose or create the folders, then press Done.");
    else if (report_->updated)
        note = tr("Plynth was updated. Check that everything still works, then press Done.");
    note_->setText(note);
    note_->setVisible(!note.isEmpty());
}

// Adds the row of one item with the buttons it needs.
void SetupCheckDialog::addRow(const plynth::SetupItem& item)
{
    const Theme& t = Theme::instance();
    auto* row = new QFrame(rowsHost_);
    row->setObjectName(QStringLiteral("setupRow"));
    row->setStyleSheet(QStringLiteral("QFrame#setupRow { background: %1; border: 1px solid %2; border-radius: %3px; }")
                           .arg(t.panel().name(), t.line().name())
                           .arg(t.radius()));
    row->setToolTip(plainToolTip(fromCore(item.name)));
    auto* box = new QVBoxLayout(row);
    box->setContentsMargins(t.unit() * 3, t.unit() * 3, t.unit() * 3, t.unit() * 3);
    box->setSpacing(t.unit() * 2);

    auto* head = new QHBoxLayout();
    head->setSpacing(t.unit() * 3);
    auto* name = new QLabel(row);
    name->setTextFormat(Qt::PlainText);
    name->setText(fromCore(item.name) + (item.required ? QString() : tr(" (optional)")));
    name->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    head->addWidget(name, 1);
    auto* state = new QLabel(stateWord(item.state), row);
    state->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    state->setStyleSheet(QStringLiteral("color: %1;").arg(stateColor(item.state).name()));
    state->setToolTip(tr("The state of this item"));
    head->addWidget(state);
    box->addLayout(head);

    QString facts = fromCore(item.version);
    if (!item.path.empty())
        facts += (facts.isEmpty() ? QString() : QStringLiteral("   ")) + displayPath(item.path);
    if (!facts.isEmpty()) {
        QLabel* mono = textLabel(facts, t.monoFont(t.sizeSmall()), t.text(), row);
        mono->setToolTip(tr("The version and the place that was found"));
        box->addWidget(mono);
    }
    if (!item.detail.empty())
        box->addWidget(textLabel(fromCore(item.detail), t.uiFont(t.sizeSmall()), t.muted(), row));
    if (!item.action.empty())
        box->addWidget(textLabel(fromCore(item.action), t.uiFont(t.sizeSmall()), t.muted(), row));
    if (!item.affects.empty()) {
        QStringList affects;
        for (const std::string& what : item.affects)
            affects << fromCore(what);
        box->addWidget(textLabel(tr("Stays off without it: %1").arg(affects.join(QStringLiteral(", "))),
                                 t.uiFont(t.sizeSmall()), t.muted(), row));
    }

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(t.unit() * 2);
    bool any = false;
    const auto addButton = [&](const QString& text, const QString& tip) {
        auto* button = new QPushButton(text, row);
        button->setToolTip(tip);
        buttons->addWidget(button);
        any = true;
        return button;
    };

    if (item.id == plynth::kCheckBlender) {
        if (report_ && !report_->blenderInstalls.empty()) {
            auto* combo = new QComboBox(row);
            combo->setFont(t.monoFont(t.sizeSmall()));
            combo->setStyle(t.controlStyle());
            combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            combo->setToolTip(tr("Every Blender found on this computer. Pick the one Plynth should use"));
            for (std::size_t i = 0; i < report_->blenderInstalls.size(); ++i) {
                const plynth::BlenderInstall& install = report_->blenderInstalls[i];
                QStringList where;
                for (plynth::BlenderSource source : install.sources)
                    where << QString::fromUtf8(plynth::blenderSourceName(source).data());
                const QString version = install.version ? fromCore(plynth::formatBlenderVersion(*install.version))
                                                        : tr("unknown");
                combo->addItem(QStringLiteral("%1  (%2)%3")
                                   .arg(version, where.join(QStringLiteral(", ")),
                                        install.usable() ? QString() : tr("  cannot be used")),
                               QString::fromStdWString(install.executable.wstring()));
                combo->setItemData(static_cast<int>(i), plainToolTip(displayPath(install.executable)), Qt::ToolTipRole);
            }
            if (report_->selectedBlender)
                combo->setCurrentIndex(static_cast<int>(*report_->selectedBlender));
            connect(combo, &QComboBox::activated, this, [this, combo](int index) {
                const std::filesystem::path chosen(combo->itemData(index).toString().toStdWString());
                applySettings([chosen](plynth::MachineSettings& s) { s.blender = chosen; });
            });
            buttons->addWidget(combo, 1);
            any = true;
        } else {
            buttons->addStretch(1);
        }
        connect(addButton(tr("Browse"), tr("Pick blender.exe by hand")),
                &QPushButton::clicked, this, &SetupCheckDialog::browseBlender);
        connect(addButton(tr("Get Blender"), tr("Open the official Blender download page in your browser")),
                &QPushButton::clicked, this, [this]() { openLink(QString::fromUtf8(plynth::kBlenderDownloadUrl.data())); });
    } else if (item.id == plynth::kCheckLibrary || item.id == plynth::kCheckProjects) {
        const bool library = item.id == plynth::kCheckLibrary;
        buttons->addStretch(1);
        connect(addButton(tr("Choose folder"),
                          library ? tr("Choose the folder that holds the library") : tr("Choose the folder for your projects")),
                &QPushButton::clicked, this, [this, library]() { chooseFolder(library); });
        if (item.state == plynth::CheckState::Missing)
            connect(addButton(tr("Create"),
                              library ? tr("Create the library folder and its databases")
                                      : tr("Create the projects folder")),
                    &QPushButton::clicked, this, [this, library]() { createFolder(library); });
    } else if (!item.link.empty()) {
        buttons->addStretch(1);
        const QString link = fromCore(item.link);
        connect(addButton(tr("Open page"), tr("Open the official page in your browser")),
                &QPushButton::clicked, this, [this, link]() { openLink(link); });
    }
    if (any)
        box->addLayout(buttons);
    else
        delete buttons;
    rows_->addWidget(row);
}

// Saves new settings, shows a refusal, and checks again.
void SetupCheckDialog::applySettings(const std::function<void(plynth::MachineSettings&)>& change)
{
    const plynth::Status status = host_.apply(change);
    problem_->setText(status.ok() ? QString() : fromCore(status.message));
    problem_->setVisible(!status.ok());
    recheck();
}

// Asks for a folder and makes it the library or the projects folder.
void SetupCheckDialog::chooseFolder(bool library)
{
    plynth::MachineSettings s = host_.settings();
    const std::optional<std::filesystem::path>& current = library ? s.library : s.projects;
    const std::filesystem::path suggested = library ? plynth::defaultLibraryFolder() : plynth::defaultProjectsFolder();
    QString start = QString::fromStdWString((current ? *current : suggested).wstring());
    while (!start.isEmpty() && !QDir(start).exists())
        start = QFileInfo(start).absolutePath() == start ? QString() : QFileInfo(start).absolutePath();
    const QString folder = QFileDialog::getExistingDirectory(
        this, library ? tr("Choose the library folder") : tr("Choose the projects folder"), start);
    if (folder.isEmpty())
        return;
    const std::filesystem::path chosen(folder.toStdWString());
    applySettings([library, chosen](plynth::MachineSettings& settings) {
        (library ? settings.library : settings.projects) = chosen;
    });
}

// Creates the library or projects folder named in the settings, or the suggested one on first run.
void SetupCheckDialog::createFolder(bool library)
{
    plynth::MachineSettings s = host_.settings();
    std::optional<std::filesystem::path>& target = library ? s.library : s.projects;
    if (!target)
        target = library ? plynth::defaultLibraryFolder() : plynth::defaultProjectsFolder();
    plynth::Status status;
    if (library) {
        status = plynth::initLibrary(*target);
    } else {
        std::error_code error;
        std::filesystem::create_directories(*target, error);
        if (error)
            status = plynth::fail(plynth::ErrorCode::CannotWrite, "The folder could not be created: " + error.message());
    }
    if (!status.ok()) {
        problem_->setText(fromCore(status.message));
        problem_->show();
        return;
    }
    applySettings([library, folder = *target](plynth::MachineSettings& settings) {
        (library ? settings.library : settings.projects) = folder;
    });
}

// Asks for blender.exe and makes it the chosen Blender.
void SetupCheckDialog::browseBlender()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose blender.exe"), QString(),
                                                      tr("Blender (blender.exe);;Programs (*.exe)"));
    if (file.isEmpty())
        return;
    const std::filesystem::path chosen(file.toStdWString());
    applySettings([chosen](plynth::MachineSettings& s) { s.blender = chosen; });
}

// Saves the version that passed when everything required is found, then closes.
void SetupCheckDialog::finish()
{
    if (ready()) {
        const plynth::Status status = host_.apply(
            [](plynth::MachineSettings& s) { s.setupCheckedVersion = std::string(plynth::version()); });
        if (!status.ok()) {
            problem_->setText(fromCore(status.message));
            problem_->show();
            return;
        }
    }
    accept();
}

// Opens an official page in the browser.
void SetupCheckDialog::openLink(const QString& link)
{
    QDesktopServices::openUrl(QUrl(link));
}

}

namespace plynth::app {

// For checks only: scrolls the list until the row whose title starts with this text is in view.
void SetupCheckDialog::scrollToItem(const QString& title)
{
    const QList<QLabel*> labels = rowsHost_->findChildren<QLabel*>();
    for (QLabel* label : labels)
        if (label->text().startsWith(title)) {
            scroll_->ensureWidgetVisible(label);
            return;
        }
}

}
