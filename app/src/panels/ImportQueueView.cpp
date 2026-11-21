#include "ImportQueueView.h"

#include "catalog/ImportQueue.h"
#include "theme/Theme.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace plynth::app {

// Builds an empty list that shows itself when the queue has items.
ImportQueueView::ImportQueueView(ImportQueue& queue, QWidget* parent) : QWidget(parent), queue_(queue)
{
    setProperty("tourId", QStringLiteral("catalog-queue"));
    setToolTip(tr("The imports that are running or waiting"));
    rows_ = new QVBoxLayout(this);
    rows_->setContentsMargins(0, 0, 0, 0);
    rows_->setSpacing(Theme::instance().unit() * 2);
    connect(&queue_, &ImportQueue::itemsChanged, this, &ImportQueueView::rebuild);
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
    const std::vector<QueueItem>& items = queue_.items();
    setVisible(!items.empty());
    for (const QueueItem& item : items) {
        const bool failed = item.state == QueueItem::State::Failed;
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
        auto* name = new QLabel(item.title, row);
        name->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
        name->setToolTip(item.title);
        top->addWidget(name, 1);
        auto* stage = new QLabel(item.stage, row);
        stage->setFont(t.uiFont(t.sizeSmall()));
        stage->setStyleSheet(QStringLiteral("color: %1;").arg((failed ? t.signalAmber() : t.muted()).name()));
        stage->setToolTip(tr("What the import is doing now"));
        top->addWidget(stage);
        box->addLayout(top);

        if (failed) {
            auto* why = new QLabel(item.message, row);
            why->setWordWrap(true);
            why->setFont(t.uiFont(t.sizeSmall()));
            why->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
            why->setToolTip(tr("Why the import failed"));
            box->addWidget(why);
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
        if (failed && !item.workFolder.isEmpty()) {
            auto* log = new QPushButton(tr("Show log"), row);
            log->setToolTip(tr("Open the Blender log of this import"));
            log->setProperty("tourId", QStringLiteral("queue-show-log"));
            const QString folder = item.workFolder;
            connect(log, &QPushButton::clicked, this, [folder]() {
                const QString file = QDir(folder).filePath(QStringLiteral("blender.log"));
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo::exists(file) ? file : folder));
            });
            buttons->addWidget(log);
        }
        auto* action = new QPushButton(failed ? tr("Dismiss") : tr("Cancel"), row);
        action->setToolTip(failed ? tr("Remove this line from the list") : tr("Stop this import. Nothing is kept"));
        action->setProperty("tourId", failed ? QStringLiteral("queue-dismiss") : QStringLiteral("queue-cancel"));
        connect(action, &QPushButton::clicked, this, [this, id]() { queue_.cancel(id); });
        buttons->addWidget(action);
        box->addLayout(buttons);
        rows_->addWidget(row);
    }
}

}
