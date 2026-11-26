#include "StoreImports.h"

#include "catalog/FabImportDialog.h"
#include "catalog/ImportQueue.h"
#include "catalog/LibraryHost.h"
#include "materials/MaterialImportQueue.h"

#include <plynth/core/catalog/FabZip.h>
#include <plynth/core/catalog/Importer.h>
#include <plynth/core/catalog/Integrations.h>
#include <plynth/core/catalog/MaterialImporter.h>

#include <QFileDialog>
#include <QFileInfo>

namespace plynth::app {

namespace {

// Turns Qt text into core text.
std::string toCore(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

// Turns core text into Qt text.
QString fromCore(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Turns a Qt path into a standard path without losing non English letters.
std::filesystem::path toPath(const QString& text)
{
    return std::filesystem::path(text.toStdWString());
}

// The file dialog filter for the importers an integration lists, for example "Files (*.obj *.fbx)".
QString filterFor(const plynth::Integration& integration, bool material)
{
    QStringList patterns;
    const std::vector<std::string> ids = material ? integration.materialImporters() : integration.assetImporters();
    for (const std::string& id : ids) {
        std::vector<std::string> suffixes;
        if (material) {
            if (const plynth::MaterialImporter* importer = plynth::findMaterialImporter(id))
                suffixes = importer->fileSuffixes();
        } else if (const plynth::Importer* importer = plynth::findImporter(id)) {
            suffixes = importer->fileSuffixes();
        }
        for (const std::string& suffix : suffixes) {
            const QString pattern = QStringLiteral("*") + fromCore(suffix);
            if (!patterns.contains(pattern))
                patterns << pattern;
        }
    }
    if (patterns.isEmpty())
        return QObject::tr("All files (*)");
    return QObject::tr("Files (%1)").arg(patterns.join(QLatin1Char(' ')));
}

}

// Keeps the host and the two queues it hands requests to.
StoreImports::StoreImports(LibraryHost& host, ImportQueue& assets, MaterialImportQueue& materials,
                           QWidget* dialogParent, QObject* parent)
    : QObject(parent), host_(host), assets_(assets), materials_(materials), dialogParent_(dialogParent)
{
}

// Queues the item at a pasted address as an asset or as a material, read by the importer the integration
// chooses for it.
void StoreImports::importAddress(const QString& integrationId, const QString& address, bool material)
{
    const QString trimmed = address.trimmed();
    if (trimmed.isEmpty()) {
        emit notice(tr("Paste an address first."), integrationId);
        return;
    }
    if (!host_.hasLibrary()) {
        emit notice(tr("Open a library before importing."), integrationId);
        return;
    }
    const plynth::Integration* integration = plynth::findIntegration(toCore(integrationId));
    if (integration == nullptr)
        return;
    plynth::ImportSource source;
    source.url = toCore(trimmed);
    const std::string importer = plynth::integrationImporter(*integration, source, material);
    if (importer.empty()) {
        emit notice(tr("%1 cannot read this address.").arg(fromCore(integration->name())), integrationId);
        return;
    }
    if (material) {
        plynth::MaterialImportRequest request;
        request.source = source;
        request.importer = importer;
        materials_.enqueueImport(request, integrationId);
    } else {
        plynth::ImportRequest request;
        request.source = source;
        request.importer = importer;
        assets_.enqueueRequest(request, integrationId);
    }
}

// Asks for files with the endings the integration's importers read, then imports them.
void StoreImports::importFiles(const QString& integrationId, bool material)
{
    const plynth::Integration* integration = plynth::findIntegration(toCore(integrationId));
    if (integration == nullptr)
        return;
    if (isOwnFilesIntegration(*integration)) {
        if (ownFiles_)
            ownFiles_(material, integrationId);
        return;
    }
    QString title;
    QString filter;
    if (isFabIntegration(*integration)) {
        title = material ? tr("Import Fab material zips") : tr("Import Fab model zips");
        filter = tr("Zip files (*.zip)");
    } else {
        title = material ? tr("Import materials from %1").arg(fromCore(integration->name()))
                         : tr("Import assets from %1").arg(fromCore(integration->name()));
        filter = filterFor(*integration, material);
    }
    const QStringList files = QFileDialog::getOpenFileNames(dialogWindow(), title, QString(), filter);
    if (!files.isEmpty())
        importChosenFiles(integrationId, files, material);
}

// Own files go to the window's queue as its own imports do, Fab zips to the dialog, any other source's files
// to the queue with the importer that source picks for each file, so the item gets that source and options.
void StoreImports::importChosenFiles(const QString& integrationId, const QStringList& files, bool material,
                                     bool waitForDialog)
{
    const plynth::Integration* integration = plynth::findIntegration(toCore(integrationId));
    if (integration == nullptr || files.isEmpty())
        return;
    if (!host_.hasLibrary()) {
        emit notice(tr("Open a library before importing."), integrationId);
        return;
    }
    if (isOwnFilesIntegration(*integration)) {
        if (ownPaths_)
            ownPaths_(files, material, integrationId);
        return;
    }
    if (isFabIntegration(*integration)) {
        if (waitForDialog)
            importFabZips(files, material, integrationId);
        else
            openFabDialog(files, material, integrationId);
        return;
    }
    for (const QString& file : files) {
        plynth::ImportSource source;
        source.file = toPath(file);
        const std::string importer = plynth::integrationImporter(*integration, source, material);
        if (importer.empty()) {
            emit notice(tr("%1 cannot read %2.").arg(fromCore(integration->name()), QFileInfo(file).fileName()), integrationId);
            continue;
        }
        if (material) {
            plynth::MaterialImportRequest request;
            request.source = source;
            request.importer = importer;
            materials_.enqueueImport(request, integrationId);
        } else {
            plynth::ImportRequest request;
            request.source = source;
            request.importer = importer;
            assets_.enqueueRequest(request, integrationId);
        }
    }
}

// Opens the dialog for the zips and queues the pages the user kept.
void StoreImports::importFabZips(const QStringList& zips, bool material, const QString& origin)
{
    if (zips.isEmpty())
        return;
    if (!host_.hasLibrary()) {
        emit notice(tr("Open a library before importing."), origin);
        return;
    }
    FabImportDialog dialog(host_.paths(), zips, material, host_.settings().keepImportedZips,
                           std::string(plynth::kFabImporterId), dialogWindow());
    if (dialog.exec() == QDialog::Accepted)
        queuePicks(dialog, origin);
}

// Opens the dialog and returns at once. Closing it with Import queues the picks.
FabImportDialog* StoreImports::openFabDialog(const QStringList& zips, bool material, const QString& origin)
{
    if (zips.isEmpty() || !host_.hasLibrary())
        return nullptr;
    auto* dialog = new FabImportDialog(host_.paths(), zips, material, host_.settings().keepImportedZips,
                                       std::string(plynth::kFabImporterId), dialogWindow());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog, origin]() { queuePicks(*dialog, origin); });
    dialog->open();
    return dialog;
}

// Queues the pages the user kept in an accepted dialog, with the dialog's importer.
void StoreImports::queuePicks(const FabImportDialog& dialog, const QString& origin)
{
    const bool material = dialog.material();
    const bool keep = dialog.keepZips();
    for (const FabImportDialog::Pick& pick : dialog.picks()) {
        if (material) {
            plynth::MaterialImportRequest request;
            request.source.file = toPath(pick.zip);
            request.importer = dialog.importer();
            request.importerOptions = pick.options;
            request.keepOriginal = keep;
            materials_.enqueueImport(request, origin);
        } else {
            plynth::ImportRequest request;
            request.source.file = toPath(pick.zip);
            request.importer = dialog.importer();
            request.importerOptions = pick.options;
            request.keepOriginal = keep;
            assets_.enqueueRequest(request, origin);
        }
    }
}

// Opens the dialog for the file of a failed row, where core said it went. The row's own importer is kept,
// empty when the source was guessed, so the pick records the same source and license. The new import
// replaces the row.
void StoreImports::pickModel(int queueItemId)
{
    const QueueItem* item = assets_.item(queueItemId);
    if (item == nullptr)
        return;
    // Copied now: the queue's list can change while the dialog is open.
    const bool inbox = item->kind == QueueItem::Kind::Inbox;
    const plynth::ImportRequest rowRequest = item->request;
    const QString origin = item->origin;
    const std::filesystem::path file = inbox ? item->movedTo : item->request.source.file;
    const QString path = QString::fromStdWString(file.wstring());
    if (file.empty() || !QFileInfo::exists(path)) {
        emit notice(tr("The file is no longer there."), origin);
        return;
    }
    const std::string importer = rowRequest.importer;
    FabImportDialog dialog(host_.paths(), {path}, false, host_.settings().keepImportedZips, importer, dialogWindow());
    if (dialog.exec() != QDialog::Accepted)
        return;
    const std::vector<FabImportDialog::Pick> picks = dialog.picks();
    if (picks.empty())
        return;
    plynth::ImportRequest request = inbox ? plynth::ImportRequest() : rowRequest;
    request.source.file = file;
    request.importer = importer;
    request.importerOptions = picks.front().options;
    request.keepOriginal = dialog.keepZips();
    assets_.dismiss(queueItemId);
    assets_.enqueueRequest(request, origin);
}

}
