#include "BuildPanel.h"

#include "ProblemMark.h"
#include "theme/ElideLabel.h"
#include "theme/Theme.h"

#include <QDateTime>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <functional>

namespace plynth::app {

namespace {

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// One problem: the amber mark, the sentence and the id of the object. A click activates it.
class ProblemRow : public QWidget {
public:
    ProblemRow(const plynth::BuildProblem& problem, QWidget* parent) : QWidget(parent), problem_(problem)
    {
        const Theme& t = Theme::instance();
        setCursor(problem.object.empty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
        setToolTip(problem.object.empty() ? tr("This problem has no object on the plan")
                                          : tr("Show this on the plan: select it and centre the plan on it"));
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(t.unit() * 2 + t.problemMarker() + t.unit() * 2, t.unit(), t.unit() * 2, t.unit());
        row->setSpacing(t.unit() * 2);
        auto* text = new QLabel(fromCore(problem.message), this);
        text->setWordWrap(true);
        text->setFont(t.uiFont(t.sizeNormal()));
        text->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(text, 1);
        if (!problem.object.empty()) {
            auto* id = new QLabel(fromCore(problem.object), this);
            id->setFont(t.monoFont(t.sizeSmall()));
            id->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
            id->setAttribute(Qt::WA_TransparentForMouseEvents);
            row->addWidget(id, 0, Qt::AlignTop);
        }
    }

    // The object this row points at.
    QString object() const { return fromCore(problem_.object); }

protected:
    // Draws the mark at the left, in the top line of the row.
    void paintEvent(QPaintEvent*) override
    {
        const Theme& t = Theme::instance();
        QPainter p(this);
        const int side = t.problemMarker();
        paintProblemMark(p, QRectF(t.unit() * 2, t.unit() + 1, side, side), problem_.severity == plynth::BuildSeverity::Error,
                         t.signalAmber(), 1.4);
    }
    // Reports the click to the panel through the parent chain.
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && !problem_.object.empty() && onClick)
            onClick(object());
    }

public:
    std::function<void(const QString&)> onClick;

private:
    plynth::BuildProblem problem_;
};

}

// Builds the scrolling body and shows the empty text.
BuildPanel::BuildPanel(QWidget* parent) : QWidget(parent)
{
    const Theme& t = Theme::instance();
    setProperty("tourId", QStringLiteral("build-panel"));
    setToolTip(tr("The last build: summary, files, steps and problems"));
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

    problemsHost_ = new QWidget(this);
    auto* problemOuter = new QVBoxLayout(problemsHost_);
    problemOuter->setContentsMargins(t.unit() * 4, 0, 0, 0);
    problemOuter->setSpacing(0);
    problemsHeading_ = new QLabel(problemsHost_);
    problemsHeading_->setFont(t.uiFont(t.sizeSmall(), QFont::DemiBold));
    problemsHeading_->setStyleSheet(QStringLiteral("color: %1; padding: %2px 0;").arg(t.muted().name()).arg(t.unit() * 2));
    problemOuter->addWidget(problemsHeading_);
    auto* problemScroll = new QScrollArea(problemsHost_);
    problemScroll->setWidgetResizable(true);
    problemScroll->setFrameShape(QFrame::NoFrame);
    problemScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    problemScroll->setMinimumHeight(t.unit() * 36);
    problemScroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }") +
                                 t.scrollBarStyleSheet());
    problemBody_ = new QWidget();
    problemColumn_ = new QVBoxLayout(problemBody_);
    problemColumn_->setContentsMargins(0, 0, 0, t.unit() * 2);
    problemColumn_->setSpacing(0);
    problemColumn_->addStretch(1);
    problemScroll->setWidget(problemBody_);
    problemOuter->addWidget(problemScroll, 1);
    outer->addWidget(problemsHost_, 1);
    outer->setStretch(0, 2);
    setReport(nullptr, false);
}

// Starts a section with a title line and returns the widget to fill.
QWidget* BuildPanel::addSection(const QString& title)
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
void BuildPanel::addRow(QWidget* host, const QString& name, const QString& value, bool mono)
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

// Rebuilds the body from a report.
void BuildPanel::setReport(const plynth::BuildReport* report, bool stale)
{
    const Theme& t = Theme::instance();
    while (QLayoutItem* item = column_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    empty_ = nullptr;
    while (problemColumn_->count() > 1) {
        QLayoutItem* item = problemColumn_->takeAt(0);
        delete item->widget();
        delete item;
    }
    problemsHost_->setVisible(report != nullptr);
    if (report == nullptr) {
        empty_ = new QLabel(tr("No build yet. Press Build (Ctrl+B)."), body_);
        empty_->setWordWrap(true);
        empty_->setFont(t.uiFont(t.sizeNormal()));
        empty_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
        column_->addWidget(empty_);
        column_->addStretch(1);
        return;
    }

    if (!report->status.ok()) {
        auto* failed = new QLabel(fromCore(report->status.message), body_);
        failed->setWordWrap(true);
        failed->setFont(t.uiFont(t.sizeNormal()));
        failed->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
        column_->addWidget(failed);
    }
    if (stale) {
        auto* note = new QLabel(tr("The plan changed since this build."), body_);
        note->setFont(t.uiFont(t.sizeNormal()));
        note->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
        column_->addWidget(note);
    }

    const QLocale locale;
    QWidget* summary = addSection(tr("Summary"));
    const QDateTime when = QDateTime::fromSecsSinceEpoch(report->startedAt.time_since_epoch().count()).toLocalTime();
    addRow(summary, tr("Built at"), when.toString(QStringLiteral("yyyy-MM-dd HH:mm")), true);
    addRow(summary, tr("Took"), tr("%1 s").arg(report->elapsedMs / 1000.0, 0, 'f', 1), true);
    if (!report->blenderVersion.empty())
        addRow(summary, tr("Blender"), fromCore(report->blenderVersion), true);
    if (report->status.ok()) {
        addRow(summary, tr("Objects"), locale.toString(report->objects), true);
        addRow(summary, tr("Triangles"), locale.toString(report->triangles), true);
        addRow(summary, tr("Openings cut"), QString::number(report->counts.openingsCut), true);
        addRow(summary, tr("Doors and windows"), QString::number(report->counts.fills), true);
        addRow(summary, tr("Rooms"), QString::number(report->counts.rooms), true);
        addRow(summary, tr("Items"), QString::number(report->counts.items), true);
    }

    if (!report->blendFile.empty() || !report->fbxFile.empty()) {
        QWidget* files = addSection(tr("Files"));
        if (!report->blendFile.empty())
            addRow(files, tr("Blender"), QDir::toNativeSeparators(QString::fromStdWString(report->blendFile.wstring())), true);
        if (!report->fbxFile.empty())
            addRow(files, tr("FBX"), QDir::toNativeSeparators(QString::fromStdWString(report->fbxFile.wstring())), true);
        auto* buttons = new QWidget(body_);
        auto* row = new QHBoxLayout(buttons);
        row->setContentsMargins(0, t.unit(), 0, 0);
        auto* folder = new QPushButton(tr("Open folder"), buttons);
        folder->setToolTip(tr("Open the build folder in Explorer"));
        folder->setProperty("tourId", QStringLiteral("build-open-folder"));
        auto* blender = new QPushButton(tr("Open in Blender"), buttons);
        blender->setToolTip(tr("Open building.blend in Blender"));
        blender->setProperty("tourId", QStringLiteral("build-open-blender"));
        blender->setEnabled(!report->blendFile.empty());
        row->addWidget(folder);
        row->addWidget(blender);
        row->addStretch(1);
        column_->addWidget(buttons);
        connect(folder, &QPushButton::clicked, this, &BuildPanel::openFolderRequested);
        connect(blender, &QPushButton::clicked, this, &BuildPanel::openBlenderRequested);
    }

    if (!report->steps.empty()) {
        QWidget* steps = addSection(tr("Steps"));
        for (const plynth::BuildStepTime& step : report->steps)
            addRow(steps, fromCore(step.step), tr("%1 ms").arg(step.ms), true);
    }

    problemsHeading_->setText(report->problems.empty() ? tr("Problems: none") : tr("Problems"));
    problemsHost_->findChild<QScrollArea*>()->setVisible(!report->problems.empty());
    int at = 0;
    for (const plynth::BuildProblem& problem : report->problems) {
        auto* row = new ProblemRow(problem, problemBody_);
        row->onClick = [this](const QString& object) { emit problemActivated(object); };
        problemColumn_->insertWidget(at++, row);
    }
    column_->addStretch(1);
}

}
