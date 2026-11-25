#include "HomeScreen.h"

#include "catalog/LibraryHost.h"
#include "furniture/AssetPreview.h"
#include "home/ProjectThumbnail.h"
#include "theme/ElideLabel.h"
#include "preview/GraphicsProbe.h"
#include "theme/Theme.h"

#include <plynth/core/HelpPages.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Resources.h>
#include <plynth/core/Samples.h>
#include <plynth/core/SetupCheck.h>
#include <plynth/core/Settings.h>
#include <plynth/core/Version.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QScrollArea>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

namespace plynth::app {

namespace {

constexpr int kThumbWidth = 96;
constexpr int kThumbHeight = 64;

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// The plans folder of the samples core finds. Empty when there is none.
QString samplePlansFolder()
{
    const std::filesystem::path samples = plynth::resourceFolder(plynth::ResourceKind::Samples);
    if (samples.empty())
        return QString();
    const QString plans = QString::fromStdWString((samples / L"plans").wstring());
    return QFileInfo(plans).isDir() ? plans : QString();
}

// A short readable time: the time today, "Yesterday", or the date.
QString whenText(const plynth::SessionTime& time)
{
    const QDateTime stamp = QDateTime::fromSecsSinceEpoch(time.time_since_epoch().count());
    const QDate today = QDate::currentDate();
    if (stamp.date() == today)
        return QObject::tr("Today %1").arg(stamp.toString(QStringLiteral("HH:mm")));
    if (stamp.date() == today.addDays(-1))
        return QObject::tr("Yesterday %1").arg(stamp.toString(QStringLiteral("HH:mm")));
    return stamp.toString(QStringLiteral("yyyy-MM-dd"));
}

// One row of the recent list. A click anywhere on it, except on its buttons, runs onClick.
class RecentRow : public QFrame {
public:
    RecentRow(bool clickable, QWidget* parent) : QFrame(parent), clickable_(clickable)
    {
        setMouseTracking(true);
        if (clickable)
            setCursor(Qt::PointingHandCursor);
    }

    std::function<void()> onClick;

protected:
    // Paints Raised under the pointer.
    void paintEvent(QPaintEvent*) override
    {
        const Theme& t = Theme::instance();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (hover_ && clickable_) {
            p.setPen(Qt::NoPen);
            p.setBrush(t.raised());
            p.drawRoundedRect(rect(), t.radius(), t.radius());
        }
    }
    // Starts the hover look.
    void enterEvent(QEnterEvent*) override
    {
        hover_ = true;
        update();
    }
    // Ends the hover look.
    void leaveEvent(QEvent*) override
    {
        hover_ = false;
        update();
    }
    // A release inside the row counts as a click.
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (clickable_ && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && onClick)
            onClick();
    }

private:
    bool clickable_ = true;
    bool hover_ = false;
};

// Makes a small muted label.
QLabel* mutedLabel(const QString& text, QWidget* parent, bool mono = false)
{
    const Theme& t = Theme::instance();
    auto* label = new ElideLabel(text, parent);
    label->setFont(mono ? t.monoFont(t.sizeSmall()) : t.uiFont(t.sizeSmall()));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    return label;
}

// Makes a section title for the left column.
QLabel* sectionTitle(const QString& text, QWidget* parent)
{
    const Theme& t = Theme::instance();
    auto* label = new QLabel(text, parent);
    label->setFont(t.uiFont(t.sizeSmall(), QFont::DemiBold));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    return label;
}

}

// Builds the two columns. Nothing is filled until refresh.
HomeScreen::HomeScreen(LibraryHost& host, QWidget* parent) : QWidget(parent), host_(host)
{
    setToolTip(tr("The home screen: recent projects, new project and help"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(buildSide());
    row->addWidget(buildRecent(), 1);
    connect(&host_, &LibraryHost::recentChanged, this, &HomeScreen::refresh);
    connect(&host_, &LibraryHost::libraryChanged, this, &HomeScreen::refresh);
    refresh();
}

// Paints the Ground background.
void HomeScreen::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), Theme::instance().ground());
}

// A flat link style button in the left column.
QPushButton* HomeScreen::addLink(QVBoxLayout* column, const QString& text, const QString& tip)
{
    const Theme& t = Theme::instance();
    auto* button = new QPushButton(text, column->parentWidget());
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFont(t.uiFont(t.sizeNormal()));
    button->setStyleSheet(QStringLiteral("QPushButton { border: none; background: transparent; text-align: left; padding: %1px 0px; color: %2; }"
                                         "QPushButton:hover { color: %3; }")
                              .arg(t.unit())
                              .arg(t.text().name(), t.signalBlue().name()));
    button->setToolTip(tip);
    column->addWidget(button);
    return button;
}

// The left column: name, the three buttons, help, samples, setup status and the projects folder.
QWidget* HomeScreen::buildSide()
{
    const Theme& t = Theme::instance();
    auto* side = new QFrame(this);
    side->setFixedWidth(t.unit() * 80);
    side->setStyleSheet(QStringLiteral("QFrame { background: %1; border-right: 1px solid %2; }"
                                       "QLabel { border: none; background: transparent; }")
                            .arg(t.panel().name(), t.line().name()));
    auto* column = new QVBoxLayout(side);
    column->setContentsMargins(t.unit() * 6, t.unit() * 6, t.unit() * 6, t.unit() * 5);
    column->setSpacing(t.unit() * 2);

    auto* head = new QHBoxLayout();
    head->setSpacing(t.unit() * 3);
    auto* logo = new QLabel(side);
    logo->setPixmap(QPixmap(QStringLiteral(":/logo.png")).scaled(t.unit() * 10, t.unit() * 10, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    head->addWidget(logo);
    auto* names = new QVBoxLayout();
    names->setSpacing(0);
    auto* name = new QLabel(tr("Plynth"), side);
    name->setFont(t.uiFont(t.sizeLarge() + 4, QFont::DemiBold));
    versionLabel_ = new QLabel(QString::fromUtf8(plynth::version().data(), static_cast<qsizetype>(plynth::version().size())), side);
    versionLabel_->setFont(t.monoFont(t.sizeSmall()));
    versionLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    names->addWidget(name);
    names->addWidget(versionLabel_);
    head->addLayout(names, 1);
    column->addLayout(head);
    column->addSpacing(t.unit() * 3);

    auto* create = new QPushButton(tr("New project"), side);
    create->setDefault(true);
    create->setToolTip(tr("Start a new empty project (Ctrl+N)"));
    connect(create, &QPushButton::clicked, this, &HomeScreen::newProjectRequested);
    column->addWidget(create);
    auto* fromImage = new QPushButton(tr("New project from plan image"), side);
    fromImage->setToolTip(tr("Start a new project from a plan image (Ctrl+Shift+N)"));
    connect(fromImage, &QPushButton::clicked, this, &HomeScreen::newFromImageRequested);
    column->addWidget(fromImage);
    auto* open = new QPushButton(tr("Open project"), side);
    open->setToolTip(tr("Open a project folder (Ctrl+O)"));
    connect(open, &QPushButton::clicked, this, &HomeScreen::openProjectRequested);
    column->addWidget(open);
    column->addSpacing(t.unit() * 3);

    helpBox_ = new QWidget(side);
    helpColumn_ = new QVBoxLayout(helpBox_);
    helpColumn_->setContentsMargins(0, 0, 0, 0);
    helpColumn_->setSpacing(0);
    column->addWidget(helpBox_);
    samplesBox_ = new QWidget(side);
    samplesColumn_ = new QVBoxLayout(samplesBox_);
    samplesColumn_->setContentsMargins(0, 0, 0, 0);
    samplesColumn_->setSpacing(0);
    column->addWidget(samplesBox_);
    column->addStretch(1);

    setupStatus_ = new QLabel(side);
    setupStatus_->setFont(t.uiFont(t.sizeSmall()));
    setupStatus_->setWordWrap(true);
    setupStatus_->setToolTip(tr("Whether Blender, the library folder and the add-ons are ready"));
    column->addWidget(setupStatus_);
    setupPath_ = new ElideLabel(side);
    setupPath_->setFont(t.monoFont(t.sizeSmall()));
    setupPath_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    setupPath_->hide();
    column->addWidget(setupPath_);
    setupFix_ = new QPushButton(tr("Fix"), side);
    setupFix_->setToolTip(tr("Open the setup check"));
    connect(setupFix_, &QPushButton::clicked, this, &HomeScreen::setupRequested);
    column->addWidget(setupFix_, 0, Qt::AlignLeft);

    column->addWidget(sectionTitle(tr("Projects folder"), side));
    projectsLink_ = new QPushButton(side);
    projectsLink_->setFlat(true);
    projectsLink_->setCursor(Qt::PointingHandCursor);
    projectsLink_->setFont(t.monoFont(t.sizeSmall()));
    projectsLink_->setStyleSheet(QStringLiteral("QPushButton { border: none; background: transparent; text-align: left; padding: %1px 0px; color: %2; }"
                                                "QPushButton:hover { color: %3; }")
                                     .arg(t.unit() / 2)
                                     .arg(t.text().name(), t.signalBlue().name()));
    projectsLink_->setToolTip(tr("Open the folder where new projects go"));
    connect(projectsLink_, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(host_.projectsFolder()));
    });
    column->addWidget(projectsLink_);
    return side;
}

// The right column: the title with the search box, then the list of recent projects.
QWidget* HomeScreen::buildRecent()
{
    const Theme& t = Theme::instance();
    auto* page = new QWidget(this);
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(t.unit() * 8, t.unit() * 6, t.unit() * 8, t.unit() * 5);
    column->setSpacing(t.unit() * 3);

    auto* head = new QHBoxLayout();
    head->setSpacing(t.unit() * 4);
    auto* title = new QLabel(tr("Recent projects"), page);
    title->setFont(t.uiFont(t.sizeLarge() + 2, QFont::DemiBold));
    head->addWidget(title);
    head->addStretch(1);
    search_ = new QLineEdit(page);
    search_->setPlaceholderText(tr("Search projects"));
    search_->setClearButtonEnabled(true);
    search_->setFont(t.uiFont(t.sizeNormal()));
    search_->setStyle(t.controlStyle());
    search_->setFixedWidth(t.unit() * 70);
    search_->setToolTip(tr("Type part of a project name or folder to filter the list"));
    connect(search_, &QLineEdit::textChanged, this, &HomeScreen::fillRecent);
    head->addWidget(search_);
    column->addLayout(head);

    back_ = new QPushButton(page);
    back_->setToolTip(tr("Go back to the project that is open"));
    back_->hide();
    connect(back_, &QPushButton::clicked, this, &HomeScreen::backRequested);
    column->addWidget(back_, 0, Qt::AlignLeft);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->verticalScrollBar()->setStyleSheet(t.scrollBarStyleSheet());
    scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
    recentBody_ = new QWidget(scroll);
    recentColumn_ = new QVBoxLayout(recentBody_);
    recentColumn_->setContentsMargins(0, 0, t.unit() * 2, 0);
    recentColumn_->setSpacing(t.unit());
    scroll->setWidget(recentBody_);
    scroll->setToolTip(tr("Projects you opened or saved lately. Click one to open it"));
    column->addWidget(scroll, 1);
    return page;
}

// Reads everything shown from the settings and the setup check.
void HomeScreen::refresh()
{
    fillSide();
    fillRecent();
}

// Names the open project and shows or hides the way back to it.
void HomeScreen::setOpenProject(const QString& name)
{
    back_->setText(tr("Back to %1").arg(name));
    back_->setVisible(!name.isEmpty());
}

// Fills the help links, the sample list, the setup line and the projects folder.
void HomeScreen::fillSide()
{
    const Theme& t = Theme::instance();
    const auto clear = [](QVBoxLayout* column) {
        while (QLayoutItem* item = column->takeAt(0)) {
            if (QWidget* w = item->widget())
                w->deleteLater();
            delete item;
        }
    };
    clear(helpColumn_);
    clear(samplesColumn_);

    const std::filesystem::path docs = plynth::resourceFolder(plynth::ResourceKind::Docs);
    if (!docs.empty()) {
        bool any = false;
        for (const plynth::HelpPageEntry& page : plynth::helpPages()) {
            if (!page.listed || page.kind != plynth::HelpPageKind::File || page.id == "generators")
                continue;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(docs / plynth::pathFromUtf8(page.file), ec))
                continue;
            if (!any) {
                helpColumn_->addWidget(sectionTitle(tr("Help"), helpBox_));
                any = true;
            }
            const QString id = fromCore(page.id);
            QPushButton* link = addLink(helpColumn_, fromCore(page.title), tr("Open this help page in the Help window"));
            connect(link, &QPushButton::clicked, this, [this, id]() { emit helpPageRequested(id); });
        }
    }
    helpBox_->setVisible(helpColumn_->count() > 0);

    const QString samples = samplePlansFolder();
    if (!samples.isEmpty()) {
        const QFileInfoList images = QDir(samples).entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
        if (!images.isEmpty())
            samplesColumn_->addWidget(sectionTitle(tr("Sample plans"), samplesBox_));
        for (const QFileInfo& image : images) {
            QPushButton* link = addLink(samplesColumn_, image.completeBaseName(),
                                        tr("Start a new project from this sample plan image"));
            const QString file = image.absoluteFilePath();
            connect(link, &QPushButton::clicked, this, [this, file]() { emit openSampleRequested(file); });
        }
    }
    const std::filesystem::path sampleRoot = plynth::resourceFolder(plynth::ResourceKind::Samples);
    const plynth::SamplesRead index = sampleRoot.empty() ? plynth::SamplesRead{} : plynth::readSamples(sampleRoot);
    if (index.status.ok() && !index.index.projects.empty()) {
        samplesColumn_->addWidget(sectionTitle(tr("Sample projects"), samplesBox_));
        for (const plynth::SampleProject& project : index.index.projects) {
            QPushButton* link = addLink(samplesColumn_, fromCore(project.name),
                                        tr("Open a copy of this sample project that you can change. %1").arg(fromCore(project.description)));
            const QPixmap picture(QString::fromStdWString((sampleRoot / plynth::pathFromUtf8(project.image)).wstring()));
            if (!picture.isNull()) {
                link->setIcon(QIcon(picture.scaled(t.unit() * 12, t.unit() * 8, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                link->setIconSize(QSize(t.unit() * 12, t.unit() * 8));
            }
            const QString id = fromCore(project.id);
            connect(link, &QPushButton::clicked, this, [this, id]() { emit openSampleProjectRequested(id); });
        }
    }
    samplesBox_->setVisible(samplesColumn_->count() > 0);

    const plynth::SetupReport quick = plynth::quickSetupCheck(host_.settings(), host_.settingsExisted(), appGraphics());
    QString problem;
    QString problemPath;
    QString problemDetail;
    for (const plynth::SetupItem& item : quick.items) {
        if (item.state == plynth::CheckState::Missing || item.state == plynth::CheckState::TooOld ||
            (item.state == plynth::CheckState::NeedsAttention && item.required)) {
            problem = tr("%1 needs attention.").arg(fromCore(item.name));
            problemPath = QDir::toNativeSeparators(QString::fromStdWString(item.path.wstring()));
            problemDetail = fromCore(item.detail);
            break;
        }
    }
    const bool attention = !problem.isEmpty() || quick.firstRun;
    if (problem.isEmpty() && quick.firstRun)
        problem = tr("Setup has not been checked yet.");
    setupStatus_->setText(attention ? problem : tr("Setup is ready."));
    setupStatus_->setToolTip(problemDetail.isEmpty() ? tr("Whether Blender, the library folder and the add-ons are ready") : problemDetail);
    setupPath_->setText(problemPath);
    setupPath_->setToolTip(problemPath);
    setupPath_->setVisible(!problemPath.isEmpty());
    setupStatus_->setStyleSheet(QStringLiteral("color: %1;").arg((attention ? t.signalAmber() : t.muted()).name()));
    setupFix_->setVisible(attention);
    const QString folder = QDir::toNativeSeparators(host_.projectsFolder());
    projectsLink_->setText(QFontMetrics(projectsLink_->font()).elidedText(folder, Qt::ElideMiddle, t.unit() * 80 - t.unit() * 12));
    projectsLink_->setToolTip(tr("Open the folder where new projects go: %1").arg(folder));
}

// Fills the recent list, filtered by the search text. Folders that are gone show as missing.
void HomeScreen::fillRecent()
{
    const Theme& t = Theme::instance();
    while (QLayoutItem* item = recentColumn_->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    const QString filter = search_->text().trimmed();
    int shown = 0;
    QStringList missingThumbs;
    for (const plynth::RecentProject& recent : host_.settings().recentProjects) {
        const QString folder = QString::fromStdWString(recent.path.wstring());
        const QString name = fromCore(recent.name);
        if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive) && !folder.contains(filter, Qt::CaseInsensitive))
            continue;
        ++shown;
        const bool missing = !std::filesystem::is_directory(recent.path);
        auto* row = new RecentRow(!missing, recentBody_);
        row->setToolTip(missing ? tr("This project folder is gone. Remove it from the list")
                                : tr("Open %1").arg(name));
        row->setFixedHeight(t.unit() * 20);
        auto* box = new QHBoxLayout(row);
        box->setContentsMargins(t.unit() * 2, t.unit() * 2, t.unit() * 2, t.unit() * 2);
        box->setSpacing(t.unit() * 4);

        auto* thumb = new QLabel(row);
        thumb->setFixedSize(kThumbWidth, kThumbHeight);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        thumb->setFont(t.uiFont(t.sizeSmall()));
        const QPixmap picture = AssetPreview::loadPixmap(
            QString::fromStdWString(plynth::recentThumbnailPath(recent.path).wstring()), kThumbWidth, devicePixelRatioF());
        if (picture.isNull() && !missing && !triedThumbs_.contains(folder)) {
            triedThumbs_.insert(folder);
            missingThumbs << folder;
        }
        if (!picture.isNull() && !missing) {
            thumb->setStyleSheet(QStringLiteral("background: %1; border-radius: %2px;").arg(t.ground().name()).arg(t.radius()));
            thumb->setPixmap(picture);
        } else {
            thumb->setStyleSheet(QStringLiteral("background: %1; color: %2; border-radius: %3px;")
                                     .arg(t.raised().name(), t.muted().name())
                                     .arg(t.radius()));
            thumb->setText(missing ? tr("Missing") : tr("No preview"));
        }
        box->addWidget(thumb);

        auto* texts = new QVBoxLayout();
        texts->setSpacing(t.unit() / 2);
        texts->addStretch(1);
        auto* title = new ElideLabel(name, row);
        title->setFont(t.uiFont(t.sizeNormal() + 1, QFont::DemiBold));
        title->setStyleSheet(QStringLiteral("color: %1;").arg((missing ? t.muted() : t.text()).name()));
        title->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        texts->addWidget(title);
        auto* path = mutedLabel(QDir::toNativeSeparators(folder), row, true);
        path->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        texts->addWidget(path);
        QLabel* when = nullptr;
        if (missing) {
            when = new QLabel(tr("Folder not found"), row);
            when->setFont(t.uiFont(t.sizeSmall()));
            when->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
        } else {
            when = mutedLabel(tr("Opened %1").arg(whenText(recent.lastOpened)), row, true);
        }
        when->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        texts->addWidget(when);
        texts->addStretch(1);
        box->addLayout(texts, 1);

        const auto addAction = [&](const QString& text, const QString& tip) {
            auto* button = new QPushButton(text, row);
            button->setToolTip(tip);
            button->setFont(t.uiFont(t.sizeSmall()));
            box->addWidget(button, 0, Qt::AlignVCenter);
            return button;
        };
        if (!missing) {
            QPushButton* show = addAction(tr("Show folder"), tr("Open the project folder in the file explorer"));
            connect(show, &QPushButton::clicked, this, [folder]() { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
        }
        QPushButton* remove = addAction(tr("Remove"), tr("Take this project off the list. Nothing on disk is deleted"));
        const std::filesystem::path path2 = recent.path;
        connect(remove, &QPushButton::clicked, this, [this, path2]() { host_.forgetProject(path2); });
        row->onClick = [this, folder]() { emit openRecentRequested(folder); };
        recentColumn_->addWidget(row);
    }
    if (!missingThumbs.isEmpty()) {
        QTimer::singleShot(0, this, [this, missingThumbs]() {
            bool made = false;
            for (const QString& folder : missingThumbs)
                made = writeRecentThumbnail(std::filesystem::path(folder.toStdWString())) || made;
            if (made)
                fillRecent();
        });
    }
    if (shown == 0) {
        auto* note = new QLabel(host_.settings().recentProjects.empty()
                                    ? tr("No recent projects yet. Start a new project or open one. Projects you open or save will show up here.")
                                    : tr("No project matches the search."),
                                recentBody_);
        note->setWordWrap(true);
        note->setFont(t.uiFont(t.sizeNormal()));
        note->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
        recentColumn_->addWidget(note);
    }
    recentColumn_->addStretch(1);
}

}
