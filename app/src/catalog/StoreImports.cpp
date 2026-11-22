#include "StoreImports.h"

#include "catalog/FabImportDialog.h"
#include "catalog/ImportQueue.h"
#include "catalog/LibraryHost.h"
#include "materials/MaterialImportQueue.h"

#include <plynth/core/catalog/FabZip.h>
#include <plynth/core/catalog/Integrations.h>

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>

namespace plynth::app {

namespace {

std::string toCore(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

std::filesystem::path toPath(const QString& text)
{
    return std::filesystem::path(text.toStdWString());
}

// The first importer an integration names for assets or for materials, or empty.
std::string firstImporter(const QString& integrationId, bool material)
{
    const plynth::Integration* integration = plynth::findIntegration(toCore(integrationId));
    if (integration == nullptr)
        return {};
    const std::vector<std::string> ids = material ? integration->materialImporters() : integration->assetImporters();
    return ids.empty() ? std::string() : ids.front();
}

// The zip of a failed inbox row: the inbox moved it to failed/, under a numbered name if the name was taken.
QString failedInboxZip(const plynth::LibraryPaths& library, const std::filesystem::path& entry)
{
    const QDir folder(QString::fromStdWString(library.assetInboxFailed.wstring()));
    const QString name = QString::fromStdWString(entry.filename().wstring());
    const QFileInfo base(name);
    QStringList filters{name, base.completeBaseName() + QStringLiteral(" (*).") + base.suffix()};
    QString best;
    QDateTime bestTime;
    for (const QFileInfo& info : folder.entryInfoList(filters, QDir::Files)) {
        if (!bestTime.isValid() || info.lastModified() >= bestTime) {
            best = info.absoluteFilePath();
            bestTime = info.lastModified();
        }
    }
    return best;
}

}

// Keeps the host and the two queues it hands requests to.
StoreImports::StoreImports(LibraryHost& host, ImportQueue& assets, MaterialImportQueue& materials,
                           QWidget* dialogParent, QObject* parent)
    : QObject(parent), host_(host), assets_(assets), materials_(materials), dialogParent_(dialogParent)
{
}

// Queues the item at a pasted address as an asset or as a material.
void StoreImports::importAddress(const QString& integrationId, const QString& address, bool material)
{
    const QString trimmed = address.trimmed();
    if (trimmed.isEmpty()) {
        emit notice(tr("Paste an address first."));
        return;
    }
    if (!host_.hasLibrary()) {
        emit notice(tr("Open a library before importing."));
        return;
    }
    if (material) {
        plynth::MaterialImportRequest request;
        request.source.url = toCore(trimmed);
        request.importer = firstImporter(integrationId, true);
        materials_.enqueueImport(request);
    } else {
        plynth::ImportRequest request;
        request.source.url = toCore(trimmed);
        request.importer = firstImporter(integrationId, false);
        assets_.enqueueRequest(request);
    }
}

// Own files use the window's actions. Fab asks for zips and opens the dialog.
void StoreImports::importFiles(const QString& integrationId, bool material)
{
    if (integrationId == QLatin1String(plynth::kFabIntegrationId)) {
        const QStringList zips = QFileDialog::getOpenFileNames(
            dialogParent_, material ? tr("Import Fab material zips") : tr("Import Fab model zips"), QString(),
            tr("Zip files (*.zip)"));
        if (!zips.isEmpty())
            importFabZips(zips, material);
        return;
    }
    if (ownFiles_)
        ownFiles_(material);
}

// Opens the dialog for the zips and queues the pages the user kept.
void StoreImports::importFabZips(const QStringList& zips, bool material)
{
    if (zips.isEmpty())
        return;
    if (!host_.hasLibrary()) {
        emit notice(tr("Open a library before importing."));
        return;
    }
    FabImportDialog dialog(host_.paths(), zips, material, host_.settings().keepImportedZips, dialogParent_);
    if (dialog.exec() == QDialog::Accepted)
        queuePicks(dialog);
}

// Opens the dialog and returns at once. Closing it with Import queues the picks.
FabImportDialog* StoreImports::openFabDialog(const QStringList& zips, bool material)
{
    if (zips.isEmpty() || !host_.hasLibrary())
        return nullptr;
    auto* dialog = new FabImportDialog(host_.paths(), zips, material, host_.settings().keepImportedZips, dialogParent_);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog]() { queuePicks(*dialog); });
    dialog->open();
    return dialog;
}

// Queues the pages the user kept in an accepted dialog.
void StoreImports::queuePicks(const FabImportDialog& dialog)
{
    const bool material = dialog.material();
    const bool keep = dialog.keepZips();
    for (const FabImportDialog::Pick& pick : dialog.picks()) {
        if (material) {
            plynth::MaterialImportRequest request;
            request.source.file = toPath(pick.zip);
            request.importer = std::string(plynth::kFabImporterId);
            request.importerOptions = pick.options;
            request.keepOriginal = keep;
            materials_.enqueueImport(request);
        } else {
            plynth::ImportRequest request;
            request.source.file = toPath(pick.zip);
            request.importer = std::string(plynth::kFabImporterId);
            request.importerOptions = pick.options;
            request.keepOriginal = keep;
            assets_.enqueueRequest(request);
        }
    }
}

// Opens the dialog for the zip of a failed row. The new import replaces the row.
void StoreImports::pickModel(int queueItemId)
{
    const QueueItem* item = assets_.item(queueItemId);
    if (item == nullptr)
        return;
    QString zip;
    if (item->kind == QueueItem::Kind::Inbox)
        zip = failedInboxZip(host_.paths(), item->entry);
    else if (!item->request.source.file.empty())
        zip = QString::fromStdWString(item->request.source.file.wstring());
    if (zip.isEmpty() || !QFileInfo::exists(zip)) {
        emit notice(tr("The zip is no longer there."));
        return;
    }
    FabImportDialog dialog(host_.paths(), {zip}, false, host_.settings().keepImportedZips, dialogParent_);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const std::vector<FabImportDialog::Pick> picks = dialog.picks();
    if (picks.empty())
        return;
    plynth::ImportRequest request = item->kind == QueueItem::Kind::Inbox ? plynth::ImportRequest() : item->request;
    request.source.file = toPath(zip);
    request.importer = std::string(plynth::kFabImporterId);
    request.importerOptions = picks.front().options;
    request.keepOriginal = dialog.keepZips();
    assets_.dismiss(queueItemId);
    assets_.enqueueRequest(request);
}

}
