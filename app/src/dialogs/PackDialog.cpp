#include "PackDialog.h"

#include "theme/Theme.h"

#include <plynth/core/ProjectFiles.h>

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace plynth::app {

namespace {

// Turns a core text into Qt text.
QString fromCore(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Why an item is only named or missing, in plain words.
QString reasonText(plynth::PackReason reason)
{
    switch (reason) {
    case plynth::PackReason::Store: return QObject::tr("from a store, get it there");
    case plynth::PackReason::License: return QObject::tr("its license does not allow sharing");
    case plynth::PackReason::UnknownOrigin: return QObject::tr("where it came from is not known");
    case plynth::PackReason::Script: return QObject::tr("generator scripts are never copied");
    case plynth::PackReason::NotInLibrary: return QObject::tr("not in the library");
    case plynth::PackReason::Broken: return QObject::tr("broken in the library");
    case plynth::PackReason::None: break;
    }
    return QString();
}

}

// Lists a written pack.
PackDialog::PackDialog(const plynth::PackReport& report, QWidget* parent) : QDialog(parent), folder_(report.folder)
{
    build(tr("Pack project"));
    int copied = 0, named = 0, missing = 0;
    for (const plynth::PackEntry& entry : report.manifest.entries) {
        if (entry.state == plynth::PackState::Copied) {
            ++copied;
            addRow(entry, tr("Copied"), QString(), false);
        } else if (entry.state == plynth::PackState::Reference) {
            ++named;
            addRow(entry, tr("Only named"), reasonText(entry.reason), false);
        } else {
            ++missing;
            addRow(entry, tr("Missing"), reasonText(entry.reason), true);
        }
    }
    summary_->setText(tr("%1 copied, %2 only named, %3 missing. %4 MB in %5. Do not share the build and export "
                         "folders: they can hold store files.")
                          .arg(copied)
                          .arg(named)
                          .arg(missing)
                          .arg(report.bytes / 1048576.0, 0, 'f', 1)
                          .arg(QString::fromStdWString(report.folder.wstring())));
    auto* buttons = new QDialogButtonBox(this);
    QPushButton* open = buttons->addButton(tr("Open folder"), QDialogButtonBox::ActionRole);
    open->setToolTip(tr("Open the pack folder in Explorer"));
    connect(open, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(folder_.wstring())));
    });
    QPushButton* close = buttons->addButton(QDialogButtonBox::Close);
    close->setToolTip(tr("Close this window"));
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    layout()->addWidget(buttons);
}

// Lists what an unpack would do and asks to go on.
PackDialog::PackDialog(const plynth::UnpackPlan& plan, QWidget* parent) : QDialog(parent)
{
    build(tr("Unpack into library"));
    int adds = 0, present = 0, download = 0, missing = 0;
    for (const plynth::UnpackEntry& entry : plan.entries) {
        switch (entry.action) {
        case plynth::UnpackAction::Add:
            ++adds;
            addRow(entry.entry, tr("Add"), QString(), false);
            break;
        case plynth::UnpackAction::Present:
            ++present;
            addRow(entry.entry, tr("Already there"), QString(), false);
            break;
        case plynth::UnpackAction::Conflict:
            ++conflicts_;
            addRow(entry.entry, tr("Conflict"), tr("the library has a different item with this id"), true);
            break;
        case plynth::UnpackAction::NeedsDownload:
            ++download;
            addRow(entry.entry, tr("To get"), fromCore(entry.command), true);
            break;
        case plynth::UnpackAction::Missing:
            ++missing;
            addRow(entry.entry, tr("Missing"), reasonText(entry.entry.reason), true);
            break;
        }
    }
    summary_->setText(tr("%1 to add, %2 already there, %3 conflicts, %4 to get from their source, %5 missing.")
                          .arg(adds)
                          .arg(present)
                          .arg(conflicts_)
                          .arg(download)
                          .arg(missing));
    auto* buttons = new QDialogButtonBox(this);
    QPushButton* go = buttons->addButton(conflicts_ > 0 ? tr("Keep the library's items and unpack") : tr("Unpack"),
                                         QDialogButtonBox::AcceptRole);
    go->setToolTip(conflicts_ > 0 ? tr("Add the new items and leave the library's own items with the same ids as they are")
                                  : tr("Add the new items to the library"));
    go->setEnabled(adds > 0 || conflicts_ > 0);
    QPushButton* cancel = buttons->addButton(QDialogButtonBox::Cancel);
    cancel->setToolTip(tr("Close without changing the library"));
    connect(go, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    layout()->addWidget(buttons);
}

// Makes the summary line and the list.
void PackDialog::build(const QString& title)
{
    const Theme& t = Theme::instance();
    setWindowTitle(title);
    resize(t.unit() * 180, t.unit() * 120);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(t.dialogMargin(), t.dialogMargin(), t.dialogMargin(), t.dialogMargin());
    outer->setSpacing(t.unit() * 3);
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    summary_->setFont(t.uiFont(t.sizeNormal()));
    outer->addWidget(summary_);
    list_ = new QTreeWidget(this);
    list_->setStyle(t.controlStyle());
    list_->setRootIsDecorated(false);
    list_->setHeaderLabels({tr("Kind"), tr("Id"), tr("Name"), tr("What happens"), tr("Why")});
    list_->header()->setStretchLastSection(true);
    list_->setToolTip(tr("Every library item the project uses"));
    outer->addWidget(list_, 1);
}

// Adds one item. Ids and commands are in the mono font; rows that need the user are amber.
void PackDialog::addRow(const plynth::PackEntry& entry, const QString& what, const QString& detail, bool warn)
{
    const Theme& t = Theme::instance();
    auto* row = new QTreeWidgetItem(list_);
    row->setText(0, fromCore(plynth::packKindName(entry.kind)).replace(QLatin1Char('_'), QLatin1Char(' ')));
    row->setText(1, fromCore(entry.id));
    row->setFont(1, t.monoFont(t.sizeNormal()));
    row->setText(2, fromCore(entry.name));
    row->setText(3, what);
    row->setText(4, detail);
    if (detail.startsWith(QStringLiteral("plynth ")))
        row->setFont(4, t.monoFont(t.sizeNormal()));
    if (warn)
        row->setForeground(3, t.signalAmber());
}

}
