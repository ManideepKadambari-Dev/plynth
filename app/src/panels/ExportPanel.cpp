#include "ExportPanel.h"

#include "building/ProblemMark.h"
#include "panels/ExportGroupsList.h"
#include "theme/ElideLabel.h"
#include "theme/Theme.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileSystemWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QHeaderView>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <vector>

namespace plynth::app {

namespace {

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

// Draws the mark of a check: a blue tick in a circle for pass, an amber outline triangle for a warning, a filled
// amber triangle for a fail and a grey dash in a circle for a check that did not run.
void paintQualityMark(QPainter& p, const QRectF& box, plynth::QualityStatus status)
{
    const Theme& t = Theme::instance();
    if (status == plynth::QualityStatus::Warning || status == plynth::QualityStatus::Fail) {
        paintProblemMark(p, box, status == plynth::QualityStatus::Fail, t.signalAmber(), t.markStroke());
        return;
    }
    const bool pass = status == plynth::QualityStatus::Pass;
    const QColor color = pass ? t.signalBlue() : t.muted();
    const double w = t.markStroke();
    const QRectF r = box.adjusted(w, w, -w, -w);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, w,Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(pass ? QBrush(color) : QBrush(Qt::NoBrush));
    p.drawEllipse(r);
    p.setPen(QPen(pass ? t.onBlue() : color, w,Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (pass) {
        p.drawPolyline(QPolygonF() << QPointF(r.left() + r.width() * 0.27, r.top() + r.height() * 0.52)
                                   << QPointF(r.left() + r.width() * 0.44, r.top() + r.height() * 0.69)
                                   << QPointF(r.left() + r.width() * 0.74, r.top() + r.height() * 0.33));
    } else {
        p.drawLine(QPointF(r.left() + r.width() * 0.3, r.center().y()), QPointF(r.right() - r.width() * 0.3, r.center().y()));
    }
    p.restore();
}

// One thing a check found. A click on one that names a plan id selects it on the plan.
class FindingRow : public QWidget {
public:
    FindingRow(const plynth::QualityFinding& finding, QWidget* parent) : QWidget(parent), planId_(fromCore(finding.planId))
    {
        const Theme& t = Theme::instance();
        const bool clickable = !planId_.isEmpty();
        setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(clickable ? tr("Show this on the plan: select it and centre the plan on it")
                             : tr("This finding has no object on the plan"));
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(t.unit() * 2 + t.qualityMark() + t.unit() * 2, t.unit() / 2, t.unit() * 2, t.unit() / 2);
        row->setSpacing(t.unit() * 2);
        auto* text = new QLabel(fromCore(finding.message), this);
        text->setWordWrap(true);
        text->setFont(t.uiFont(t.sizeSmall()));
        text->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(text, 1);
        const QString name = clickable ? planId_ : fromCore(finding.object);
        if (!name.isEmpty()) {
            auto* id = new QLabel(name, this);
            id->setFont(t.monoFont(t.sizeSmall()));
            id->setStyleSheet(QStringLiteral("color: %1;").arg(clickable ? t.signalAmber().name() : t.muted().name()));
            id->setAttribute(Qt::WA_TransparentForMouseEvents);
            row->addWidget(id, 0, Qt::AlignTop);
        }
    }

    // Reports the click to the panel.
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && !planId_.isEmpty() && onClick)
            onClick(planId_);
    }

    std::function<void(const QString&)> onClick;

private:
    QString planId_;
};

// One check: the mark, the name and the message. A click opens or closes its findings.
class CheckRow : public QWidget {
public:
    CheckRow(const plynth::QualityCheckResult& check, bool open, QWidget* parent) : QWidget(parent), status_(check.status)
    {
        const Theme& t = Theme::instance();
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        header_ = new QWidget(this);
        const bool hasFindings = !check.findings.empty();
        header_->setCursor(hasFindings ? Qt::PointingHandCursor : Qt::ArrowCursor);
        header_->setToolTip(hasFindings ? tr("Show or hide what this check found") : fromCore(check.message));
        header_->setProperty("tourId", QStringLiteral("export-check-row"));
        auto* row = new QHBoxLayout(header_);
        row->setContentsMargins(t.unit() * 2 + t.qualityMark() + t.unit() * 2, t.unit(), t.unit() * 2, t.unit());
        row->setSpacing(t.unit() * 2);
        auto* texts = new QVBoxLayout();
        texts->setSpacing(0);
        auto* name = new QLabel(fromCore(check.name), header_);
        name->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* message = new QLabel(fromCore(check.message), header_);
        message->setWordWrap(true);
        message->setFont(t.uiFont(t.sizeSmall()));
        message->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
        message->setAttribute(Qt::WA_TransparentForMouseEvents);
        texts->addWidget(name);
        texts->addWidget(message);
        row->addLayout(texts, 1);
        if (check.status == plynth::QualityStatus::Fail) {
            auto* failed = new QLabel(tr("Failed"), header_);
            failed->setFont(t.uiFont(t.sizeSmall(), QFont::Bold));
            failed->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
            failed->setAttribute(Qt::WA_TransparentForMouseEvents);
            row->addWidget(failed, 0, Qt::AlignTop);
        }
        if (hasFindings) {
            count_ = new QLabel(header_);
            count_->setFont(t.monoFont(t.sizeSmall()));
            count_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
            count_->setAttribute(Qt::WA_TransparentForMouseEvents);
            row->addWidget(count_, 0, Qt::AlignTop);
        }
        column->addWidget(header_);

        findings_ = new QWidget(this);
        auto* list = new QVBoxLayout(findings_);
        list->setContentsMargins(0, 0, 0, t.unit());
        list->setSpacing(0);
        for (const plynth::QualityFinding& finding : check.findings) {
            auto* item = new FindingRow(finding, findings_);
            item->onClick = [this](const QString& id) {
                if (onFinding)
                    onFinding(id);
            };
            list->addWidget(item);
        }
        column->addWidget(findings_);
        findingCount_ = static_cast<int>(check.findings.size());
        setOpen(open && hasFindings);
        header_->installEventFilter(this);
    }

    std::function<void(const QString&)> onFinding;
    std::function<void(bool)> onToggled;

protected:
    // Draws the mark at the left, in the top line of the row.
    void paintEvent(QPaintEvent*) override
    {
        const Theme& t = Theme::instance();
        QPainter p(this);
        const int side = t.qualityMark();
        paintQualityMark(p, QRectF(t.unit() * 2, t.unit() + 1, side, side), status_);
    }

    // Turns a click on the header into opening or closing the findings.
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == header_ && event->type() == QEvent::MouseButtonRelease && findingCount_ > 0 &&
            static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            setOpen(!open_);
            if (onToggled)
                onToggled(open_);
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    // Shows or hides the findings and the count beside the message.
    void setOpen(bool open)
    {
        open_ = open;
        findings_->setVisible(open);
        if (count_ != nullptr)
            count_->setText(open ? tr("%1  -").arg(findingCount_) : tr("%1  +").arg(findingCount_));
    }

    plynth::QualityStatus status_;
    QWidget* header_ = nullptr;
    QWidget* findings_ = nullptr;
    QLabel* count_ = nullptr;
    int findingCount_ = 0;
    bool open_ = false;
};

}

// Builds the scrolling body.
ExportPanel::ExportPanel(QWidget* parent) : QWidget(parent)
{
    const Theme& t = Theme::instance();
    setProperty("tourId", QStringLiteral("export-panel"));
    setToolTip(tr("The export for Unreal: the last export, its files and the quality checks"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }") +
                          t.scrollBarStyleSheet());
    body_ = new QWidget();
    column_ = new QVBoxLayout(body_);
    column_->setContentsMargins(t.unit() * 4, t.unit() * 3, t.unit() * 4, t.unit() * 4);
    column_->setSpacing(t.unit() * 2);
    scroll->setWidget(body_);
    outer->addWidget(scroll, 1);

    groups_ = new ExportGroupsList();
    groupsTree_ = groups_->findChild<QTreeWidget*>();
    groups_->hide();
    connect(groups_, &ExportGroupsList::edited, this, [this]() {
        QTimer::singleShot(0, this, &ExportPanel::refreshRequested);
    });
    if (groupsTree_ != nullptr) {
        groupsTree_->setTextElideMode(Qt::ElideMiddle);
        groupsTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        groupsTree_->header()->setSectionResizeMode(1, QHeaderView::Interactive);
        groupsTree_->viewport()->installEventFilter(this);
        connect(groupsTree_, &QTreeWidget::itemExpanded, this, [this]() { fitGroupsTree(); });
        connect(groupsTree_, &QTreeWidget::itemCollapsed, this, [this]() { fitGroupsTree(); });
    }
    watcher_ = new QFileSystemWatcher(this);
    watchTimer_ = new QTimer(this);
    watchTimer_->setSingleShot(true);
    watchTimer_->setInterval(250);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, watchTimer_, qOverload<>(&QTimer::start));
    connect(watchTimer_, &QTimer::timeout, this, &ExportPanel::refreshRequested);
    setReport(nullptr, ExportPanelState());
}

// Keeps the file column to a share of the tree's width, so names and files both stay readable.
bool ExportPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (groupsTree_ != nullptr && watched == groupsTree_->viewport() && event->type() == QEvent::Resize)
        fitGroupsColumns();
    return QWidget::eventFilter(watched, event);
}

// Sets the width of the file column.
void ExportPanel::fitGroupsColumns()
{
    const int width = groupsTree_->viewport()->width();
    if (width > 0)
        groupsTree_->header()->resizeSection(1, width * 45 / 100);
}

// Frees the list, which is not always in the layout.
ExportPanel::~ExportPanel()
{
    delete groups_;
}

// Asks for a fresh review each time the tab is shown.
void ExportPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, &ExportPanel::refreshRequested);
}

// For checks only: keeps every group open.
void ExportPanel::setGroupsExpanded(bool expanded)
{
    groupsExpanded_ = expanded;
    groups_->setAllExpanded(expanded);
    fitGroupsTree();
}

// Gives the groups tree the height of its visible rows, so the tab scrolls instead of the tree.
void ExportPanel::fitGroupsTree()
{
    if (groupsTree_ == nullptr)
        return;
    int rows = 0;
    for (QTreeWidgetItemIterator it(groupsTree_); *it; ++it) {
        bool shown = true;
        for (QTreeWidgetItem* up = (*it)->parent(); up != nullptr; up = up->parent())
            shown = shown && up->isExpanded();
        if (shown)
            ++rows;
    }
    const int hint = groupsTree_->topLevelItemCount() > 0 ? groupsTree_->sizeHintForRow(0) : 0;
    const int rowHeight = hint > 0 ? hint : groupsTree_->fontMetrics().height() + 6;
    groupsTree_->setFixedHeight(rows * rowHeight + groupsTree_->frameWidth() * 2 + 2);
}

// Watches the project folder, so a change to export-options.json made elsewhere refreshes the list.
void ExportPanel::watchFolder(const std::filesystem::path& folder)
{
    if (folder == watched_)
        return;
    if (!watcher_->directories().isEmpty())
        watcher_->removePaths(watcher_->directories());
    watched_ = folder;
    if (!folder.empty())
        watcher_->addPath(nativePath(folder));
}

// Writes the kit switch into the project's export options and asks for a new review.
void ExportPanel::setKitEnabled(bool enabled)
{
    if (projectFolder_.empty())
        return;
    plynth::ExportOptionsRead read = plynth::readExportOptions(projectFolder_);
    plynth::Status status = read.status;
    if (status.ok() && read.options.kit.enabled != enabled) {
        read.options.kit.enabled = enabled;
        status = plynth::writeExportOptions(projectFolder_, read.options);
    }
    kitError_ = status.ok() ? QString() : fromCore(status.message);
    // The new review puts the box back to what is saved.
    emit refreshRequested();
}

// Adds the checkbox that switches the modular kit on for the next export.
void ExportPanel::addKitSwitch(const ExportPanelState& state)
{
    const Theme& t = Theme::instance();
    auto* box = new QCheckBox(tr("Also write the modular kit"), body_);
    box->setFont(t.uiFont(t.sizeNormal()));
    box->setChecked(state.kitEnabled);
    box->setEnabled(!state.projectFolder.empty() && !state.readOnly);
    box->setProperty("tourId", QStringLiteral("export-kit"));
    box->setToolTip(tr("Also write every unique wall, corner and opening piece once, with collision, and a layout file "
                       "that says where each copy goes. It needs a library."));
    connect(box, &QCheckBox::clicked, this, [this](bool on) { setKitEnabled(on); });
    column_->addWidget(box);
    if (!kitError_.isEmpty()) {
        auto* error = new QLabel(kitError_, body_);
        error->setWordWrap(true);
        error->setFont(t.uiFont(t.sizeSmall()));
        error->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
        error->setToolTip(tr("Why the modular kit switch was not saved"));
        column_->addWidget(error);
    }
}

// Adds what the last export did with the kit: its files and counts, or why it was left out.
void ExportPanel::addKitReport(const plynth::ExportReport& report)
{
    const Theme& t = Theme::instance();
    const plynth::ExportKitReport& kit = report.kit;
    if (!kit.asked)
        return;
    QWidget* section = addSection(tr("Modular kit"));
    if (kit.written) {
        addRow(section, tr("Kit"), nativePath(kit.file), true);
        addRow(section, tr("Layout"), nativePath(kit.layoutFile), true);
        addRow(section, tr("Pieces"), QLocale().toString(kit.pieces), true);
        addRow(section, tr("Placements"), QLocale().toString(kit.placements), true);
    } else if (!kit.skipped.empty()) {
        auto* line = new QLabel(fromCore(kit.skipped), body_);
        line->setWordWrap(true);
        line->setFont(t.uiFont(t.sizeNormal()));
        line->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
        line->setToolTip(tr("Why the kit was left out of the last export"));
        column_->addWidget(line);
    }
}

// Adds the Groups section for a grouped project, or the one line for a single project.
void ExportPanel::addGroups(const ExportPanelState& state)
{
    const Theme& t = Theme::instance();
    auto* heading = new QLabel(tr("Groups"), body_);
    heading->setFont(t.uiFont(t.sizeSmall(), QFont::DemiBold));
    heading->setStyleSheet(QStringLiteral("color: %1; padding-top: %2px;").arg(t.muted().name()).arg(t.unit() * 2));
    heading->setToolTip(tr("The files an export writes"));
    column_->addWidget(heading);
    if (!state.grouped) {
        auto* line = new QLabel(tr("Single FBX: one file. Switch to grouped in Properties > Project."), body_);
        line->setWordWrap(true);
        line->setFont(t.uiFont(t.sizeNormal()));
        line->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
        line->setProperty("tourId", QStringLiteral("export-groups-single"));
        line->setToolTip(tr("This project exports one FBX. Choose Grouped FBX under Export mode in Properties > Project"));
        column_->addWidget(line);
        return;
    }
    review_ = state.review != nullptr ? *state.review : plynth::ExportGroupReview();
    if (!state.built)
        review_.groups.clear();
    // The labels come from the plan the groups were made from, which review_ keeps as long as the list needs it.
    groups_->setReview(state.projectFolder, review_, state.readOnly, &review_.plan);
    groups_->setAllExpanded(groupsExpanded_);
    column_->addWidget(groups_);
    groups_->show();
    fitGroupsTree();
}

// Starts a section with a title line and returns the widget to fill.
QWidget* ExportPanel::addSection(const QString& title)
{
    const Theme& t = Theme::instance();
    auto* heading = new QLabel(title, body_);
    heading->setFont(t.uiFont(t.sizeSmall(), QFont::DemiBold));
    heading->setStyleSheet(QStringLiteral("color: %1; padding-top: %2px;").arg(t.muted().name()).arg(t.unit() * 2));
    column_->addWidget(heading);
    auto* host = new QWidget(body_);
    auto* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(t.unit() * 3);
    grid->setVerticalSpacing(t.unit());
    grid->setColumnStretch(1, 1);
    column_->addWidget(host);
    return host;
}

// Adds a name and a value in the next row of a section. Numbers and paths use the mono font.
void ExportPanel::addRow(QWidget* host, const QString& name, const QString& value, bool mono)
{
    const Theme& t = Theme::instance();
    auto* grid = static_cast<QGridLayout*>(host->layout());
    const int row = grid->rowCount();
    auto* label = new QLabel(name, host);
    label->setFont(t.uiFont(t.sizeNormal()));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    auto* shown = new ElideLabel(value, host);
    shown->setFont(mono ? t.monoFont(t.sizeNormal()) : t.uiFont(t.sizeNormal()));
    shown->setToolTip(value);
    grid->addWidget(label, row, 0);
    grid->addWidget(shown, row, 1);
}

// Adds a button to a row. The primary one is filled with Signal blue.
QPushButton* ExportPanel::addButton(QWidget* row, const QString& text, const QString& tip, const QString& tourId, bool primary)
{
    const Theme& t = Theme::instance();
    auto* button = new QPushButton(text, row);
    button->setToolTip(tip);
    button->setProperty("tourId", tourId);
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

// Adds the quality list: one row per check.
void ExportPanel::addChecks(const plynth::ExportReport& report)
{
    const Theme& t = Theme::instance();
    auto* heading = new QLabel(tr("Quality checks"), body_);
    heading->setFont(t.uiFont(t.sizeSmall(), QFont::DemiBold));
    heading->setStyleSheet(QStringLiteral("color: %1; padding-top: %2px;").arg(t.muted().name()).arg(t.unit() * 2));
    column_->addWidget(heading);
    if (report.checks.empty()) {
        auto* none = new QLabel(tr("No checks ran."), body_);
        none->setFont(t.uiFont(t.sizeNormal()));
        none->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
        column_->addWidget(none);
        return;
    }
    // Failed checks come first, then warnings, so what needs attention is at the top.
    std::vector<const plynth::QualityCheckResult*> ordered;
    for (const plynth::QualityCheckResult& check : report.checks)
        ordered.push_back(&check);
    auto rank = [](plynth::QualityStatus status) {
        return status == plynth::QualityStatus::Fail ? 0 : status == plynth::QualityStatus::Warning ? 1 : 2;
    };
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&rank](const auto* a, const auto* b) { return rank(a->status) < rank(b->status); });
    for (const plynth::QualityCheckResult* shown : ordered) {
        const plynth::QualityCheckResult& check = *shown;
        const QString id = fromCore(check.id);
        const bool open = expanded_.value(id, check.status == plynth::QualityStatus::Fail);
        auto* row = new CheckRow(check, open, body_);
        row->onFinding = [this](const QString& planId) { emit findingActivated(planId); };
        row->onToggled = [this, id](bool now) { expanded_.insert(id, now); };
        column_->addWidget(row);
    }
}

// Rebuilds the body from a report and the state around it.
void ExportPanel::setReport(const plynth::ExportReport* report, const ExportPanelState& state)
{
    const Theme& t = Theme::instance();
    while (QLayoutItem* item = column_->takeAt(0)) {
        if (item->widget() == groups_)
            groups_->hide();
        else
            delete item->widget();
        delete item;
    }
    watchFolder(state.projectFolder);
    if (projectFolder_ != state.projectFolder)
        kitError_.clear();
    projectFolder_ = state.projectFolder;

    auto* buttons = new QWidget(body_);
    auto* buttonRow = new QHBoxLayout(buttons);
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(t.unit() * 2);
    QPushButton* exportButton = addButton(buttons, tr("Export"), tr("Write the FBX and textures for Unreal (Ctrl+E)"),
                                          QStringLiteral("export-run"), true);
    QPushButton* checkButton = addButton(buttons, tr("Check"),
                                         tr("Run the quality checks and write nothing (Ctrl+Shift+E)"),
                                         QStringLiteral("export-check"), false);
    buttonRow->addStretch(1);
    exportButton->setEnabled(state.canRun && !state.running);
    checkButton->setEnabled(state.canRun && !state.running);
    connect(exportButton, &QPushButton::clicked, this, &ExportPanel::exportRequested);
    connect(checkButton, &QPushButton::clicked, this, &ExportPanel::checkRequested);
    column_->addWidget(buttons);

    auto note = [&](const QString& text, const QColor& color) {
        auto* label = new QLabel(text, body_);
        label->setWordWrap(true);
        label->setFont(t.uiFont(t.sizeNormal()));
        label->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
        column_->addWidget(label);
    };
    if (!state.notice.isEmpty()) {
        note(state.notice, t.muted());
        if (state.offerBuild) {
            auto* row = new QWidget(body_);
            auto* layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            QPushButton* build = addButton(row, tr("Build"), tr("Build the building first (Ctrl+B)"),
                                           QStringLiteral("export-build"), false);
            build->setEnabled(!state.running);
            layout->addStretch(1);
            connect(build, &QPushButton::clicked, this, &ExportPanel::buildRequested);
            column_->addWidget(row);
        }
    }
    if (!state.error.isEmpty())
        note(state.error, t.signalAmber());

    addGroups(state);
    addKitSwitch(state);

    if (report == nullptr) {
        if (state.notice.isEmpty() && state.error.isEmpty())
            note(tr("No export yet. Press Check to run the quality checks, or Export (Ctrl+E)."), t.muted());
        column_->addStretch(1);
        column_->activate();
        return;
    }

    const bool blocked = report->status.code == plynth::ErrorCode::QualityFailed || (!report->exported && report->failCount() > 0);
    if (blocked) {
        const int fails = report->failCount();
        note(fails == 1 ? tr("Export blocked: 1 check failed.") : tr("Export blocked: %1 checks failed.").arg(fails),
             t.signalAmber());
        auto* row = new QWidget(body_);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        QPushButton* anyway = addButton(row, tr("Export anyway..."),
                                        tr("Export with the failed checks left as they are. Asks first"),
                                        QStringLiteral("export-anyway"), false);
        anyway->setEnabled(state.canRun && !state.running);
        layout->addStretch(1);
        connect(anyway, &QPushButton::clicked, this, &ExportPanel::exportAnywayRequested);
        column_->addWidget(row);
    } else if (!report->status.ok() && state.error.isEmpty()) {
        note(fromCore(report->status.message), t.signalAmber());
    }

    addChecks(*report);

    const QLocale locale;
    QWidget* summary = addSection(tr("Last export"));
    const QDateTime when = QDateTime::fromSecsSinceEpoch(report->startedAt.time_since_epoch().count()).toLocalTime();
    addRow(summary, tr("At"), when.toString(QStringLiteral("yyyy-MM-dd HH:mm")), true);
    addRow(summary, tr("Took"), tr("%1 s").arg(report->elapsedMs / 1000.0, 0, 'f', 1), true);
    addRow(summary, tr("Result"),
           report->exported ? (report->forced ? tr("Exported anyway") : tr("Exported"))
           : report->checkedOnly ? tr("Checked, nothing written")
                                 : tr("Blocked, nothing written"),
           false);
    if (report->forced && !report->overridden.empty()) {
        QStringList ids;
        for (const std::string& id : report->overridden)
            ids.append(fromCore(id));
        addRow(summary, tr("Went past"), ids.join(QStringLiteral(", ")), true);
    }
    if (!report->blenderVersion.empty())
        addRow(summary, tr("Blender"), fromCore(report->blenderVersion), true);
    if (report->objects > 0) {
        addRow(summary, tr("Objects"), locale.toString(report->objects), true);
        addRow(summary, tr("Triangles"), locale.toString(report->triangles), true);
        addRow(summary, tr("Collision meshes"), locale.toString(report->collisionObjects), true);
    }
    if (report->exported) {
        addRow(summary, tr("Texture files"), locale.toString(report->textureCount), true);
    }


    addKitReport(*report);

    if (report->exported || !report->reportFile.empty()) {
        QWidget* files = addSection(tr("Files"));
        for (const std::filesystem::path& file : report->files)
            addRow(files, tr("FBX"), nativePath(file), true);
        if (!report->texturesFolder.empty())
            addRow(files, tr("Textures"), nativePath(report->texturesFolder), true);
        if (!report->reportFile.empty())
            addRow(files, tr("Report"), nativePath(report->reportFile), true);
        auto* row = new QWidget(body_);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, t.unit(), 0, 0);
        QPushButton* folder = addButton(row, tr("Open folder"), tr("Open the export folder in Explorer"),
                                        QStringLiteral("export-open-folder"), false);
        folder->setEnabled(state.folderExists);
        layout->addStretch(1);
        connect(folder, &QPushButton::clicked, this, &ExportPanel::openFolderRequested);
        column_->addWidget(row);
    }
    column_->addStretch(1);
    column_->activate();
}

}
