#include "ImportQueueView.h"

#include "catalog/ImportQueue.h"
#include "materials/MaterialImportQueue.h"
#include "theme/ElideLabel.h"
#include "theme/PlainText.h"
#include "theme/Theme.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QUrl>
#include <QVBoxLayout>

namespace plynth::app {

// Shows the asset import queue.
ImportQueueView::ImportQueueView(ImportQueue& queue, QWidget* parent) : QWidget(parent)
{
    rows_fn_ = [&queue]() {
        std::vector<Row> rows;
        for (const QueueItem& item : queue.items())
            rows.push_back({item.id, item.title, item.stage, item.percent, item.state == QueueItem::State::Failed,
                            item.kind == QueueItem::Kind::Refresh, item.message, item.workFolder, item.pickable});
        return rows;
    };
    cancel_ = [&queue](int id) { queue.cancel(id); };
    connect(&queue, &ImportQueue::itemsChanged, this, &ImportQueueView::rebuild);
    start();
}

// Shows the material import queue.
ImportQueueView::ImportQueueView(MaterialImportQueue& queue, QWidget* parent) : QWidget(parent)
{
    rows_fn_ = [&queue]() {
        std::vector<Row> rows;
        for (const MaterialQueueItem& item : queue.items())
            rows.push_back({item.id,
                            item.title.isEmpty() ? QString::fromStdString(item.request.source.url) : item.title,
                            item.stage, item.percent, item.state == MaterialQueueItem::State::Failed, false,
                            item.message, item.workFolder, false});
        return rows;
    };
    cancel_ = [&queue](int id) { queue.cancel(id); };
    connect(&queue, &MaterialImportQueue::itemsChanged, this, &ImportQueueView::rebuild);
    start();
}

// Builds an empty list that shows itself when the queue has items.
void ImportQueueView::start()
{
    rows_ = new QVBoxLayout(this);
    rows_->setContentsMargins(0, 0, 0, 0);
    rows_->setSpacing(Theme::instance().unit() * 2);
    rebuild();
}

// Rebuilds the rows from the queue. The list is short, so it is made again on every change.
void ImportQueueView::rebuild()
{
    const Theme& t = Theme::instance();
    while (QLayoutItem* item = rows_->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    const std::vector<Row> items = rows_fn_();
    setVisible(!items.empty());
    // The parent lays out again at once, so a picture taken right after the last row left shows no gap.
    if (parentWidget() != nullptr && parentWidget()->layout() != nullptr)
        parentWidget()->layout()->activate();
    for (const Row& item : items) {
        const bool failed = item.failed;
        auto* row = new QFrame(this);
        row->setObjectName(QStringLiteral("queueRow"));
        row->setStyleSheet(QStringLiteral("QFrame#queueRow { background: %1; border: 1px solid %2; border-radius: %3px; }")
                               .arg(t.raised().name(), t.line().name())
                               .arg(t.radius()));
        auto* box = new QVBoxLayout(row);
        box->setContentsMargins(t.unit() * 2, t.unit() * 2, t.unit() * 2, t.unit() * 2);
        box->setSpacing(t.unit());

        auto* top = new QHBoxLayout();
        top->setSpacing(t.unit() * 2);
        auto* name = new QLabel(row);
        name->setTextFormat(Qt::PlainText);
        name->setText(item.title);
        if (item.plainTitle) {
            name->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
        } else {
            name->setFont(t.monoFont(t.sizeNormal()));
            name->setMinimumWidth(0);
            name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        }
        name->setToolTip(plainToolTip(item.title));
        top->addWidget(name, 1);
        auto* stage = new QLabel(row);
        stage->setTextFormat(Qt::PlainText);
        stage->setText(item.stage);
        stage->setFont(t.uiFont(t.sizeSmall()));
        stage->setStyleSheet(QStringLiteral("color: %1;").arg((failed ? t.signalAmber() : t.muted()).name()));
        stage->setToolTip(tr("What the import is doing now"));
        top->addWidget(stage);
        box->addLayout(top);

        if (failed) {
            // The work folder path is long and the Show log button opens it, so the row keeps it on one mono line.
            QString shown = item.message;
            const QString folder = QDir::toNativeSeparators(item.workFolder);
            const QString slashed = QDir::fromNativeSeparators(item.workFolder);
            if (!folder.isEmpty()) {
                shown.replace(folder, QString());
                shown.replace(slashed, QString());
            }
            shown = shown.trimmed();
            auto* why = new QLabel(row);
            why->setTextFormat(Qt::PlainText);
            why->setText(shown);
            why->setWordWrap(true);
            why->setFont(t.uiFont(t.sizeSmall()));
            why->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
            why->setToolTip(tr("Why the import failed"));
            box->addWidget(why);
            if (!folder.isEmpty()) {
                auto* path = new ElideLabel(row);
                path->setTextFormat(Qt::PlainText);
                path->setText(folder);
                path->setFont(t.monoFont(t.sizeSmall()));
                path->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
                path->setToolTip(plainToolTip(folder));
                box->addWidget(path);
            }
        } else {
            auto* bar = new QProgressBar(row);
            bar->setRange(0, 100);
            bar->setValue(item.percent);
            bar->setTextVisible(false);
            bar->setFixedHeight(t.unit() * 2);
            bar->setStyleSheet(QStringLiteral("QProgressBar { background: %1; border: none; border-radius: %3px; }"
                                              "QProgressBar::chunk { background: %2; border-radius: %3px; }")
                                   .arg(t.ground().name(), t.signalBlue().name())
                                   .arg(t.radius() / 2));
            bar->setToolTip(tr("How far the import has come"));
            box->addWidget(bar);
        }

        auto* buttons = new QHBoxLayout();
        buttons->setSpacing(t.unit() * 2);
        buttons->addStretch(1);
        const int id = item.id;
        if (item.canPick) {
            auto* pick = new QPushButton(tr("Pick the model..."), row);
            pick->setToolTip(tr("Choose which model of the file to import"));
            const int pickId = item.id;
            connect(pick, &QPushButton::clicked, this, [this, pickId]() { emit pickModelRequested(pickId); });
            buttons->addWidget(pick);
        }
        if (failed && !item.workFolder.isEmpty()) {
            auto* log = new QPushButton(tr("Show log"), row);
            log->setToolTip(tr("Open the Blender log of this import"));
            const QString folder = item.workFolder;
            connect(log, &QPushButton::clicked, this, [folder]() {
                const QString file = QDir(folder).filePath(QStringLiteral("blender.log"));
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo::exists(file) ? file : folder));
            });
            buttons->addWidget(log);
        }
        auto* action = new QPushButton(failed ? tr("Dismiss") : tr("Cancel"), row);
        action->setToolTip(failed ? tr("Remove this line from the list") : tr("Stop this import. Nothing is kept"));
        connect(action, &QPushButton::clicked, this, [this, id]() { cancel_(id); });
        buttons->addWidget(action);
        box->addLayout(buttons);
        rows_->addWidget(row);
    }
}

}
