#include "SetupCheckDialog.h"

#include "catalog/LibraryHost.h"
#include "preview/GraphicsProbe.h"
#include "theme/PlainText.h"
#include "theme/Theme.h"
#include "update/UpdateController.h"

#include <plynth/core/Library.h>
#include <plynth/core/Version.h>

#include <QCheckBox>
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
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>

namespace plynth::app {

namespace {

constexpr int kDialogWidth = 760;
constexpr int kDialogHeight = 600;
constexpr double kMegabyte = 1024.0 * 1024.0;
constexpr int kProgressSteps = 1000;
constexpr auto kProgressEvery = std::chrono::milliseconds(100);
constexpr long long kCannedZipBytes = 401653760;
constexpr long long kCannedDoneBytes = 168820736;

// The made up state the next dialog shows, empty for a real one.
QString& cannedDownloadName()
{
    static QString name;
    return name;
}

// A size in megabytes with one decimal.
QString megabytes(long long bytes)
{
    return QString::number(static_cast<double>(bytes) / kMegabyte, 'f', 1);
}

// The Blender series Plynth downloads, for example "5.2".
QString testedSeries()
{
    return QStringLiteral("%1.%2").arg(plynth::kTestedBlenderVersion.major).arg(plynth::kTestedBlenderVersion.minor);
}

// The offer shown in a check, shaped like one blender.org gives.
plynth::BlenderOffer cannedOffer()
{
    plynth::BlenderOffer offer;
    offer.version = {plynth::kTestedBlenderVersion.major, plynth::kTestedBlenderVersion.minor, 3};
    const std::string name = "blender-" + plynth::formatBlenderVersion(offer.version) + "-windows-x64.zip";
    offer.zipUrl = std::string(plynth::kBlenderReleaseFolder) + "Blender" + testedSeries().toStdString() + "/" + name;
    offer.zipName = name;
    offer.sha256 = std::string(64, '0');
    offer.size = static_cast<std::uint64_t>(kCannedZipBytes);
    return offer;
}

// Gives a button the blue look of the main action, from the theme.
void makePrimary(QPushButton* button)
{
    const Theme& t = Theme::instance();
    button->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border-color: %1; }")
                              .arg(t.signalBlue().name(), t.onBlue().name()));
}

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
SetupCheckDialog::SetupCheckDialog(LibraryHost& host, QWidget* parent, UpdateController* updates)
    : QDialog(parent), host_(host), updates_(updates), canned_(cannedDownloadName())
{
    const Theme& t = Theme::instance();
    setWindowTitle(tr("Setup check"));
    setModal(true);
    if (canned_ == QStringLiteral("looking")) {
        getStep_ = GetStep::Looking;
    } else if (canned_ == QStringLiteral("question")) {
        offer_ = cannedOffer();
        getStep_ = GetStep::Question;
    } else if (canned_ == QStringLiteral("downloading") || canned_ == QStringLiteral("checking")) {
        offer_ = cannedOffer();
        progress_.stage = canned_ == QStringLiteral("checking") ? plynth::BlenderDownloadStage::Checking
                                                                : plynth::BlenderDownloadStage::Downloading;
        progress_.done = canned_ == QStringLiteral("checking") ? kCannedZipBytes : kCannedDoneBytes;
        progress_.total = kCannedZipBytes;
        getStep_ = GetStep::Downloading;
    } else if (canned_ == QStringLiteral("failed")) {
        offer_ = cannedOffer();
        showGetFailure(plynth::fail(plynth::ErrorCode::NetworkFailed,
                                    "The connection to download.blender.org was lost."), false);
    } else if (canned_ == QStringLiteral("checksum")) {
        offer_ = cannedOffer();
        showGetFailure(plynth::fail(plynth::ErrorCode::ChecksumFailed,
                                    "The SHA-256 of the download is not the one blender.org publishes."), false);
    } else if (canned_ == QStringLiteral("stopped")) {
        offer_ = cannedOffer();
        getStep_ = GetStep::Stopped;
    }

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

// Stops a running check and a running download before the dialog goes away.
SetupCheckDialog::~SetupCheckDialog()
{
    stop_.request_stop();
    downloadStop_.request_stop();
    worker_ = std::jthread();
    downloadWorker_ = std::jthread();
}

// For checks only: makes the dialogs opened from now on show a made up "Get Blender for me" state.
void SetupCheckDialog::useCannedDownload(const QString& name)
{
    cannedDownloadName() = name;
}

// The made up states a check can ask for.
QStringList SetupCheckDialog::cannedDownloadNames()
{
    return {QStringLiteral("offer"),    QStringLiteral("looking"),  QStringLiteral("question"),
            QStringLiteral("downloading"), QStringLiteral("checking"), QStringLiteral("failed"),
            QStringLiteral("checksum"), QStringLiteral("stopped")};
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
    const bool canned = !canned_.isEmpty();
    if (canned) {
        // An empty path means no Blender is looked for or started, which is the made up missing Blender.
        options.blender = std::filesystem::path();
        options.probeAddons = false;
    }
    const std::stop_token token = stop_.get_token();
    worker_ = std::jthread([this, generation, settings, options, token, canned]() {
        plynth::SetupReport report = plynth::runSetupCheck(settings, options, token);
        if (canned)
            for (plynth::SetupItem& item : report.items)
                if (item.id == plynth::kCheckBlender) {
                    item.detail = "No Blender was found on this computer.";
                    item.offerDownload = true;
                }
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
    rebuildRows();
    updateSummary();
}

// Builds the rows again from the shown report, for a new report or a new step of "Get Blender for me".
void SetupCheckDialog::rebuildRows()
{
    if (!report_)
        return;
    bar_ = nullptr;
    progressLabel_ = nullptr;
    while (QLayoutItem* item = rows_->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    for (const plynth::SetupItem& item : report_->items)
        addRow(item);
    addUpdateRow();
    rows_->addStretch(1);
}

// Sets the summary line, the note under it and the button states.
void SetupCheckDialog::updateSummary()
{
    const Theme& t = Theme::instance();
    // A check during a download could see a half unpacked copy, so it waits.
    again_->setEnabled(!checking_ && !downloadBusy());
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
    if (!getDone_.isEmpty())
        note += (note.isEmpty() ? QString() : QStringLiteral(" ")) + getDone_;
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
        if (item.offerDownload && getStep_ == GetStep::Idle) {
            QPushButton* getForMe = addButton(
                tr("Get Blender for me"),
                tr("Download the official portable Blender %1 from blender.org after asking you, check it and use it")
                    .arg(testedSeries()));
            getForMe->setAutoDefault(false);
            makePrimary(getForMe);
            connect(getForMe, &QPushButton::clicked, this, &SetupCheckDialog::lookForOffer);
        }
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
    if (item.id == plynth::kCheckBlender && item.offerDownload && getStep_ != GetStep::Idle)
        addDownloadPanel(box, row);
    rows_->addWidget(row);
}

// Adds the box of "Get Blender for me" under the Blender buttons: the question, the progress or the result.
void SetupCheckDialog::addDownloadPanel(QVBoxLayout* box, QWidget* row)
{
    const Theme& t = Theme::instance();
    auto* panel = new QFrame(row);
    panel->setObjectName(QStringLiteral("getBlender"));
    panel->setStyleSheet(QStringLiteral("QFrame#getBlender { background: %1; border: 1px solid %2; border-radius: %3px; }")
                             .arg(t.ground().name(), t.line().name())
                             .arg(t.radius()));
    auto* inner = new QVBoxLayout(panel);
    inner->setContentsMargins(t.unit() * 3, t.unit() * 3, t.unit() * 3, t.unit() * 3);
    inner->setSpacing(t.unit() * 2);

    const QString version = offer_ ? fromCore(plynth::formatBlenderVersion(offer_->version)) : testedSeries();
    const auto addTitle = [&](const QString& text, const QColor& color) {
        QLabel* title = textLabel(text, t.uiFont(t.sizeNormal(), QFont::DemiBold), color, panel);
        inner->addWidget(title);
    };
    const auto addSmall = [&](const QString& text, const QFont& font, const QColor& color) {
        QLabel* label = textLabel(text, font, color, panel);
        inner->addWidget(label);
        return label;
    };
    const auto addBar = [&]() {
        auto* bar = new QProgressBar(panel);
        bar->setTextVisible(false);
        bar->setFixedHeight(t.unit() * 2);
        bar->setToolTip(tr("How far the download has come"));
        bar->setStyleSheet(QStringLiteral("QProgressBar { background: %1; border: none; border-radius: %3px; }"
                                          "QProgressBar::chunk { background: %2; border-radius: %3px; }")
                               .arg(t.raised().name(), t.signalBlue().name())
                               .arg(t.radius() / 2));
        inner->addWidget(bar);
        return bar;
    };

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(t.unit() * 2);
    buttons->addStretch(1);
    const auto addButton = [&](const QString& text, const QString& tip, bool primary) {
        auto* button = new QPushButton(text, panel);
        button->setToolTip(tip);
        button->setAutoDefault(false);
        if (primary)
            makePrimary(button);
        buttons->addWidget(button);
        return button;
    };
    const auto addNotNow = [&]() {
        connect(addButton(tr("Not now"), tr("Close this box. Nothing is downloaded"), false), &QPushButton::clicked,
                this, [this]() { setGetStep(GetStep::Idle); });
    };
    const auto addTryAgain = [&]() {
        connect(addButton(tr("Try again"), tr("Ask blender.org again for the newest Blender %1").arg(testedSeries()), true),
                &QPushButton::clicked, this, &SetupCheckDialog::lookForOffer);
    };

    bar_ = nullptr;
    progressLabel_ = nullptr;
    switch (getStep_) {
    case GetStep::Idle:
        break;
    case GetStep::Looking:
        addTitle(tr("Asking blender.org for the newest Blender %1").arg(testedSeries()), t.text());
        bar_ = addBar();
        bar_->setRange(0, 0);
        connect(addButton(tr("Stop"), tr("Stop asking blender.org. Nothing is downloaded"), false),
                &QPushButton::clicked, this, &SetupCheckDialog::stopDownload);
        break;
    case GetStep::Question: {
        addTitle(tr("Download Blender %1 from blender.org?").arg(version), t.text());
        QString facts = offer_ ? fromCore(offer_->zipName) : QString();
        facts += QStringLiteral("   ") + (offer_ && offer_->size > 0 ? tr("%1 MB").arg(megabytes(static_cast<long long>(offer_->size)))
                                                                    : tr("size not given"));
        facts += QStringLiteral("   ") + QString::fromUtf8(plynth::kBlenderDownloadHost.data());
        addSmall(facts, t.monoFont(t.sizeSmall()), t.text())
            ->setToolTip(tr("The file, its download size and the server it comes from"));
        addSmall(tr("This is the official portable Blender for Windows. Plynth checks the download against the "
                    "SHA-256 checksum blender.org publishes, unpacks it into %1 and selects it in the settings. "
                    "Nothing else on this computer changes.")
                     .arg(displayPath(plynth::downloadedBlenderRoot())),
                 t.uiFont(t.sizeSmall()), t.muted());
        addNotNow();
        connect(addButton(tr("Download"), tr("Download Blender %1 from blender.org, check it and use it").arg(version), true),
                &QPushButton::clicked, this, &SetupCheckDialog::startDownload);
        break;
    }
    case GetStep::Downloading:
        addTitle(tr("Downloading Blender %1 from blender.org").arg(version), t.text());
        bar_ = addBar();
        progressLabel_ = addSmall(QString(), t.monoFont(t.sizeSmall()), t.muted());
        progressLabel_->setToolTip(tr("What the download is doing now"));
        refreshProgress();
        connect(addButton(tr("Stop"), tr("Stop the download. Nothing is kept"), false), &QPushButton::clicked, this,
                &SetupCheckDialog::stopDownload);
        break;
    case GetStep::Failed:
        addTitle(getProblem_, t.signalAmber());
        if (!getDetail_.isEmpty())
            addSmall(getDetail_, t.uiFont(t.sizeSmall()), t.muted());
        addSmall(tr("You can try again, use Get Blender to install it yourself, or use Browse to pick blender.exe."),
                 t.uiFont(t.sizeSmall()), t.muted());
        addNotNow();
        addTryAgain();
        break;
    case GetStep::Stopped:
        addTitle(tr("The download was stopped. Nothing was kept."), t.text());
        addNotNow();
        addTryAgain();
        break;
    }
    inner->addLayout(buttons);
    box->addWidget(panel);
}

// Moves "Get Blender for me" to another step and shows it.
void SetupCheckDialog::setGetStep(GetStep step)
{
    getStep_ = step;
    if (again_ == nullptr)
        return;
    rebuildRows();
    updateSummary();
}

// Asks blender.org for the newest portable Blender of the tested series on the download thread.
void SetupCheckDialog::lookForOffer()
{
    if (downloadBusy())
        return;
    getDone_.clear();
    if (!canned_.isEmpty()) {
        offer_ = cannedOffer();
        setGetStep(GetStep::Question);
        return;
    }
    downloadStop_ = std::stop_source();
    const std::stop_token token = downloadStop_.get_token();
    setGetStep(GetStep::Looking);
    downloadWorker_ = std::jthread([this, token]() {
        plynth::BlenderOfferCheck check = plynth::findBlenderOffer(token);
        QMetaObject::invokeMethod(this, [this, check]() { offerFound(check); }, Qt::QueuedConnection);
    });
}

// Shows the question when blender.org named a Blender, or why it did not.
void SetupCheckDialog::offerFound(const plynth::BlenderOfferCheck& check)
{
    if (getStep_ != GetStep::Looking)
        return;
    if (check.status.code == plynth::ErrorCode::Cancelled || downloadStop_.stop_requested()) {
        setGetStep(GetStep::Idle);
        return;
    }
    if (!check.status.ok() || !check.offer) {
        showGetFailure(check.status.ok() ? plynth::fail(plynth::ErrorCode::NetworkFailed,
                                                        "blender.org lists no portable Blender for Windows.")
                                         : check.status,
                       true);
        return;
    }
    offer_ = check.offer;
    setGetStep(GetStep::Question);
}

// Downloads, checks and unpacks the offer on the download thread, passing on its progress.
void SetupCheckDialog::startDownload()
{
    if (downloadBusy() || !offer_)
        return;
    getDone_.clear();
    progress_ = plynth::BlenderDownloadProgress();
    progress_.total = static_cast<long long>(offer_->size);
    if (!canned_.isEmpty()) {
        progress_.done = kCannedDoneBytes;
        setGetStep(GetStep::Downloading);
        return;
    }
    downloadStop_ = std::stop_source();
    const std::stop_token token = downloadStop_.get_token();
    setGetStep(GetStep::Downloading);
    const plynth::BlenderOffer offer = *offer_;
    downloadWorker_ = std::jthread([this, offer, token]() {
        std::optional<plynth::BlenderDownloadStage> lastStage;
        std::chrono::steady_clock::time_point lastPost;
        // Passes on a new stage at once and the bytes at most ten times a second.
        const auto onProgress = [&](const plynth::BlenderDownloadProgress& progress) {
            const auto now = std::chrono::steady_clock::now();
            if (lastStage == progress.stage && now - lastPost < kProgressEvery)
                return;
            lastStage = progress.stage;
            lastPost = now;
            QMetaObject::invokeMethod(this, [this, progress]() { showDownloadProgress(progress); }, Qt::QueuedConnection);
        };
        plynth::BlenderDownload result = plynth::downloadBlender(offer, onProgress, token);
        QMetaObject::invokeMethod(this, [this, result]() { downloadFinished(result); }, Qt::QueuedConnection);
    });
}

// Stops asking blender.org or stops the download. The download thread then reports that it stopped.
void SetupCheckDialog::stopDownload()
{
    if (!canned_.isEmpty()) {
        setGetStep(getStep_ == GetStep::Looking ? GetStep::Idle : GetStep::Stopped);
        return;
    }
    downloadStop_.request_stop();
}

// Takes new progress from the download thread.
void SetupCheckDialog::showDownloadProgress(const plynth::BlenderDownloadProgress& progress)
{
    if (getStep_ != GetStep::Downloading)
        return;
    progress_ = progress;
    refreshProgress();
}

// Shows the progress in the bar and the line under it.
void SetupCheckDialog::refreshProgress()
{
    const Theme& t = Theme::instance();
    const bool bytes = progress_.stage == plynth::BlenderDownloadStage::Downloading;
    if (bar_) {
        if (bytes && progress_.total > 0) {
            bar_->setRange(0, kProgressSteps);
            bar_->setValue(static_cast<int>(std::clamp(progress_.done * kProgressSteps / progress_.total, 0LL,
                                                       static_cast<long long>(kProgressSteps))));
        } else {
            bar_->setRange(0, 0);
        }
    }
    if (!progressLabel_)
        return;
    QString text;
    switch (progress_.stage) {
    case plynth::BlenderDownloadStage::Downloading:
        text = progress_.total > 0
                   ? tr("%1 of %2 MB").arg(megabytes(progress_.done), megabytes(progress_.total))
                   : tr("%1 MB").arg(megabytes(progress_.done));
        break;
    case plynth::BlenderDownloadStage::Checking:
        text = tr("Checking it against the SHA-256 checksum blender.org publishes");
        break;
    case plynth::BlenderDownloadStage::Unpacking:
        text = tr("Unpacking it into the Plynth Blender folder");
        break;
    case plynth::BlenderDownloadStage::Inspecting:
        text = tr("Starting it once to read its version");
        break;
    }
    progressLabel_->setFont(bytes ? t.monoFont(t.sizeSmall()) : t.uiFont(t.sizeSmall()));
    progressLabel_->setText(text);
}

// Selects the new Blender in the settings and checks again, or says why the download failed.
void SetupCheckDialog::downloadFinished(const plynth::BlenderDownload& result)
{
    if (getStep_ != GetStep::Downloading)
        return;
    if (result.status.code == plynth::ErrorCode::Cancelled || (!result.status.ok() && downloadStop_.stop_requested())) {
        setGetStep(GetStep::Stopped);
        return;
    }
    if (!result.status.ok() || !result.install || !result.install->usable()) {
        plynth::Status status = result.status;
        if (status.ok())
            status = result.install ? result.install->status
                                    : plynth::fail(plynth::ErrorCode::BlenderFailed, "No Blender came out of the download.");
        if (status.ok())
            status = plynth::fail(plynth::ErrorCode::BlenderFailed, "The downloaded Blender cannot be used.");
        showGetFailure(status, false);
        return;
    }
    const plynth::BlenderInstall install = *result.install;
    const QString version = install.version ? fromCore(plynth::formatBlenderVersion(*install.version)) : QString();
    offer_.reset();
    getStep_ = GetStep::Idle;
    const plynth::Status saved = host_.apply([chosen = install.executable](plynth::MachineSettings& s) { s.blender = chosen; });
    if (saved.ok()) {
        getDone_ = tr("Blender %1 was downloaded from blender.org, matched its checksum and is now the Blender Plynth uses.")
                       .arg(version);
        problem_->hide();
    } else {
        getDone_.clear();
        problem_->setText(tr("Blender %1 was downloaded to %2 but could not be selected: %3")
                              .arg(version, displayPath(install.executable), fromCore(saved.message)));
        problem_->show();
    }
    recheck();
}

// Says why "Get Blender for me" failed, in words for the kind of failure.
void SetupCheckDialog::showGetFailure(const plynth::Status& status, bool whileLooking)
{
    QString title;
    switch (status.code) {
    case plynth::ErrorCode::ChecksumFailed:
        title = tr("The download did not match the checksum blender.org publishes, so it was deleted. Nothing was installed.");
        break;
    case plynth::ErrorCode::CannotWrite:
        title = tr("Blender could not be saved on this computer. Check the free space, then try again.");
        break;
    case plynth::ErrorCode::BadFile:
        title = tr("The downloaded file could not be used, so it was deleted. Nothing was installed.");
        break;
    case plynth::ErrorCode::BlenderFailed:
        title = tr("The downloaded Blender did not start.");
        break;
    case plynth::ErrorCode::NetworkFailed:
        title = whileLooking ? tr("Plynth could not read the list of Blender downloads on blender.org. Check the "
                                  "internet connection, then try again.")
                             : tr("The download from blender.org failed. Nothing was installed. Check the internet "
                                  "connection, then try again.");
        break;
    default:
        title = tr("Getting Blender failed. Nothing was installed.");
        break;
    }
    getProblem_ = title;
    getDetail_ = fromCore(status.message);
    setGetStep(GetStep::Failed);
}

// Adds the row with the switch "Check for updates at start". It never blocks anything.
void SetupCheckDialog::addUpdateRow()
{
    if (updates_ == nullptr)
        return;
    const Theme& t = Theme::instance();
    auto* row = new QFrame(rowsHost_);
    row->setObjectName(QStringLiteral("setupRow"));
    row->setStyleSheet(QStringLiteral("QFrame#setupRow { background: %1; border: 1px solid %2; border-radius: %3px; }")
                           .arg(t.panel().name(), t.line().name())
                           .arg(t.radius()));
    auto* box = new QVBoxLayout(row);
    box->setContentsMargins(t.unit() * 3, t.unit() * 3, t.unit() * 3, t.unit() * 3);
    box->setSpacing(t.unit() * 2);

    auto* head = new QHBoxLayout();
    head->setSpacing(t.unit() * 3);
    auto* name = new QLabel(tr("Updates (optional)"), row);
    name->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    head->addWidget(name, 1);
    const bool on = updates_->checkAtStart();
    auto* state = new QLabel(on ? tr("On") : tr("Off"), row);
    state->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    state->setStyleSheet(QStringLiteral("color: %1;").arg((on ? t.signalBlue() : t.muted()).name()));
    head->addWidget(state);
    box->addLayout(head);

    box->addWidget(textLabel(tr("Once per start, Plynth asks GitHub whether a newer signed release exists and shows a "
                                "notice. GitHub sees your IP address and the time. Nothing about you, this computer "
                                "or your projects is sent. Updating always waits for you."),
                             t.uiFont(t.sizeSmall()), t.muted(), row));
    if (!updates_->installed())
        box->addWidget(textLabel(tr("This copy of Plynth was not installed, so it never checks. The switch is kept "
                                    "for an installed copy."),
                                 t.uiFont(t.sizeSmall()), t.muted(), row));

    auto* check = new QCheckBox(tr("Check for updates at start"), row);
    check->setFont(t.uiFont(t.sizeNormal()));
    check->setStyle(t.controlStyle());
    check->setChecked(on);
    check->setToolTip(tr("Once per start, ask GitHub for a newer signed release and show a notice"));
    connect(check, &QCheckBox::toggled, this, [this, state](bool checked) {
        const plynth::Status status = updates_->setCheckAtStart(checked);
        problem_->setText(status.ok() ? QString() : fromCore(status.message));
        problem_->setVisible(!status.ok());
        const Theme& theme = Theme::instance();
        const bool now = updates_->checkAtStart();
        state->setText(now ? tr("On") : tr("Off"));
        state->setStyleSheet(QStringLiteral("color: %1;").arg((now ? theme.signalBlue() : theme.muted()).name()));
    });
    box->addWidget(check);
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
