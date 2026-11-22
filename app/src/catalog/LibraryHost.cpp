#include "LibraryHost.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/OpeningShapes.h>
#include <plynth/core/RoomTypes.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QStandardPaths>
#include <QTimer>

#include <plynth/core/Blender.h>

namespace plynth::app {

namespace {

// Turns a core sentence into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}

// Reads the machine settings and opens the library they name.
LibraryHost::LibraryHost(QObject* parent) : QObject(parent)
{
    const plynth::SettingsLoad load = plynth::loadSettings();
    settings_ = load.settings;
    settingsExisted_ = load.exists;
    if (!load.status.ok())
        settingsProblem_ = fromCore(load.status.message);
    reopen();
}

// Loads the settings fresh, applies the change, saves, then opens the library they name. A file that is
// missing is a first run and starts from defaults. A file that cannot be read is never overwritten.
plynth::Status LibraryHost::apply(const std::function<void(plynth::MachineSettings&)>& change)
{
    plynth::SettingsLoad load = plynth::loadSettings();
    if (!load.status.ok())
        return load.status;
    change(load.settings);
    const plynth::Status status = plynth::saveSettings(load.settings);
    if (!status.ok())
        return status;
    settings_ = load.settings;
    settingsExisted_ = true;
    settingsProblem_.clear();
    reopen();
    return status;
}

// Opens assets.db of the chosen library, hands its room types to core and registers its shapes once.
void LibraryHost::reopen()
{
    catalog_.reset();
    materials_.reset();
    libraryProblem_.clear();
    const plynth::LibraryChoice choice = plynth::chooseLibrary(std::nullopt, settings_);
    if (!choice.status.ok()) {
        libraryProblem_ = fromCore(choice.status.message);
        plynth::useRoomTypes({});
        generators_ = {};
        emit libraryChanged();
        emit roomTypesChanged();
        emit generatorsChanged();
        return;
    }
    plynth::Status status;
    catalog_ = plynth::AssetCatalog::open(choice.paths, status);
    if (!catalog_) {
        libraryProblem_ = fromCore(status.message);
        plynth::useRoomTypes({});
    } else {
        paths_ = choice.paths;
        openMaterials();
        plynth::useRoomTypes(catalog_->roomTypeList());
        if (shapesRegistered_ != paths_.shapes) {
            shapesRegistered_ = paths_.shapes;
            const plynth::ShapeFolderLoad load = plynth::registerShapeFolder(paths_.shapes);
            shapeNotice_.clear();
            if (!load.refused.empty())
                shapeNotice_ = QObject::tr("The shape file %1 was not used. %2")
                                   .arg(QString::fromStdWString(load.refused.front().first.filename().wstring()),
                                        fromCore(load.refused.front().second.message));
        }
    }
    if (catalog_) {
        generators_ = plynth::loadGeneratorFolder(paths_.generators);
        watchGenerators();
        readAddonCacheFile();
    } else {
        generators_ = {};
    }
    emit libraryChanged();
    emit roomTypesChanged();
    emit generatorsChanged();
    if (catalog_)
        probeIfNeeded();
}

// Watches the generators folder and reads it again two quiet seconds after a change. The folder is made when
// missing so the watch has something to follow.
void LibraryHost::watchGenerators()
{
    if (!generatorWatcher_) {
        generatorWatcher_ = new QFileSystemWatcher(this);
        generatorTimer_ = new QTimer(this);
        generatorTimer_->setSingleShot(true);
        generatorTimer_->setInterval(1000);
        connect(generatorTimer_, &QTimer::timeout, this, &LibraryHost::reloadGenerators);
        connect(generatorWatcher_, &QFileSystemWatcher::directoryChanged, this, [this]() { generatorTimer_->start(); });
        connect(generatorWatcher_, &QFileSystemWatcher::fileChanged, this, [this]() { generatorTimer_->start(); });
    }
    if (!generatorWatcher_->directories().isEmpty())
        generatorWatcher_->removePaths(generatorWatcher_->directories());
    if (!generatorWatcher_->files().isEmpty())
        generatorWatcher_->removePaths(generatorWatcher_->files());
    std::error_code ignored;
    std::filesystem::create_directories(paths_.generators, ignored);
    generatorWatcher_->addPath(QString::fromStdWString(paths_.generators.wstring()));
    for (const plynth::GeneratorInfo& info : generators_.generators)
        generatorWatcher_->addPath(QString::fromStdWString(info.file.wstring()));
}

// Reads the generators folder again and tells the views when the list differs.
void LibraryHost::reloadGenerators()
{
    if (!catalog_)
        return;
    plynth::GeneratorLibrary fresh = plynth::loadGeneratorFolder(paths_.generators);
    const bool same = fresh.generators == generators_.generators && fresh.refused.size() == generators_.refused.size();
    generators_ = std::move(fresh);
    watchGenerators();
    if (!same)
        emit generatorsChanged();
}

// Reads the add-on cache file. Without a running Blender check it is trusted as it is.
void LibraryHost::readAddonCacheFile()
{
    addonCache_.reset();
    const plynth::AddonCacheRead read = plynth::readAddonCache(plynth::addonCacheFile());
    if (read.status.ok() && read.cache)
        addonCache_ = read.cache;
}

// Starts the background check when the cache is missing or was made by another Blender.
void LibraryHost::probeIfNeeded()
{
    if (!autoProbe_ || probeRunning_)
        return;
    const std::optional<plynth::AddonCache> cached = addonCache_;
    const plynth::MachineSettings settings = settings_;
    probeRunning_ = true;
    probe_ = std::jthread([this, cached, settings](std::stop_token stop) {
        std::optional<plynth::AddonCache> result;
        const plynth::BlenderChoice choice = plynth::chooseBlender(std::nullopt, settings, stop);
        if (!choice.status.ok()) {
            result = std::nullopt;
        } else if (cached && plynth::addonCacheMatches(*cached, choice.install)) {
            result = cached;
        } else {
            const plynth::AddonProbe probe = plynth::probeAddons(choice.install, plynth::addonCacheFile(), {true, plynth::kAddonProbeTimeout, {}}, stop);
            if (probe.status.ok())
                result = probe.cache;
        }
        QMetaObject::invokeMethod(this, [this, result]() {
            probeRunning_ = false;
            addonCache_ = result;
            emit generatorsChanged();
        }, Qt::QueuedConnection);
    });
}

// Runs the add-on check now, even when the cache looks right.
void LibraryHost::checkAddons()
{
    if (probeRunning_)
        return;
    const plynth::MachineSettings settings = settings_;
    probeRunning_ = true;
    emit generatorsChanged();
    probe_ = std::jthread([this, settings](std::stop_token stop) {
        QString text;
        std::optional<plynth::AddonCache> result;
        const plynth::BlenderChoice choice = plynth::chooseBlender(std::nullopt, settings, stop);
        if (!choice.status.ok()) {
            text = fromCore(choice.status.message);
        } else {
            const plynth::AddonProbe probe = plynth::probeAddons(choice.install, plynth::addonCacheFile(), {true, plynth::kAddonProbeTimeout, {}}, stop);
            if (probe.status.ok()) {
                result = probe.cache;
                text = QObject::tr("Checked the add-ons: %1 enabled in Blender.").arg(probe.cache.enabled.size());
            } else {
                text = fromCore(probe.status.message);
            }
        }
        QMetaObject::invokeMethod(this, [this, result, text]() {
            probeRunning_ = false;
            if (result)
                addonCache_ = result;
            emit generatorsChanged();
            emit addonCheckFinished(text);
        }, Qt::QueuedConnection);
    });
}

// Copies the sample generators from the samples folder next to the program (or up the tree) into the library.
QString LibraryHost::addSampleGenerators()
{
    if (!catalog_)
        return tr("Open a library first.");
    QDir dir(QCoreApplication::applicationDirPath());
    QString from;
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo(dir.filePath(QStringLiteral("samples/generators"))).isDir()) {
            from = dir.filePath(QStringLiteral("samples/generators"));
            break;
        }
        if (!dir.cdUp())
            break;
    }
    if (from.isEmpty())
        return tr("The sample generators folder was not found.");
    const plynth::SampleGeneratorsCopy copy =
        plynth::addSampleGenerators(paths_.generators, std::filesystem::path(from.toStdWString()));
    reloadGenerators();
    if (!copy.status.ok())
        return fromCore(copy.status.message);
    return tr("Added %1 sample generators, %2 were already there.").arg(copy.copied.size()).arg(copy.skipped.size());
}

// Copies the sample shapes into the library, registers the folder again (shapes already known are skipped
// by core) and tells the views so the opening shape lists fill again.
QString LibraryHost::addSampleShapes()
{
    if (!hasLibrary())
        return QObject::tr("Open a library first.");
    const std::filesystem::path from = plynth::sampleShapesFolder();
    if (from.empty())
        return QObject::tr("The sample shapes folder was not found next to the program.");
    const plynth::SampleShapesCopy copy = plynth::addSampleShapes(paths_, from);
    if (!copy.status.ok())
        return fromCore(copy.status.message);
    plynth::registerShapeFolder(paths_.shapes);
    emit libraryChanged();
    if (copy.copied.empty())
        return QObject::tr("All %n sample shape(s) are already in the library.", nullptr, int(copy.skipped.size()));
    return QObject::tr("Added %n sample shape(s). %1 already there.", nullptr, int(copy.copied.size()))
        .arg(copy.skipped.size());
}

// Opens materials.db. An empty index next to folders in materials/items is rebuilt once, so a library filled
// by hand or by an older run shows its materials.
void LibraryHost::openMaterials()
{
    plynth::Status status;
    materials_ = plynth::MaterialCatalog::open(paths_, status);
    if (!materials_)
        return;
    if (materials_->materialCount() == 0) {
        std::error_code ignored;
        for (const auto& entry : std::filesystem::directory_iterator(paths_.materialItems, ignored)) {
            if (entry.is_directory(ignored) && !entry.path().filename().string().starts_with('.')) {
                materials_->rescan();
                break;
            }
        }
    }
}

// Reads the settings file again, changes only the recent list with the given function, saves, and copies the
// recent list back. Nothing happens when the file could not be read at start, so it is never overwritten with
// defaults. Settings changed by the command line since the start are kept.
bool LibraryHost::editRecent(const std::function<bool(plynth::MachineSettings&)>& change)
{
    if (!settingsProblem_.isEmpty())
        return false;
    plynth::SettingsLoad load = plynth::loadSettings();
    if (!load.status.ok() || !load.exists)
        return false;
    if (!change(load.settings))
        return false;
    if (!plynth::saveSettings(load.settings).ok())
        return false;
    settings_.recentProjects = load.settings.recentProjects;
    emit recentChanged();
    return true;
}

// Adds the project to the recent list and writes the settings file.
void LibraryHost::recordProject(const std::filesystem::path& folder, const std::string& name)
{
    editRecent([&](plynth::MachineSettings& settings) {
        plynth::addRecentProject(settings, folder, name, plynth::sessionNow());
        return true;
    });
}

// Removes the project from the recent list and writes the settings file.
void LibraryHost::forgetProject(const std::filesystem::path& folder)
{
    editRecent([&](plynth::MachineSettings& settings) { return plynth::removeRecentProject(settings, folder); });
}

// The projects folder from the settings, or Documents when none is set.
QString LibraryHost::projectsFolder() const
{
    if (settings_.projects.has_value() && std::filesystem::is_directory(*settings_.projects))
        return QDir::toNativeSeparators(QString::fromStdWString(settings_.projects->wstring()));
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

// Gives core the catalog's room types again, then tells the views to redraw labels and pickers.
void LibraryHost::roomTypesEdited()
{
    if (catalog_)
        plynth::useRoomTypes(catalog_->roomTypeList());
    emit roomTypesChanged();
}

// Tells the views to read the materials again.
void LibraryHost::materialsEdited()
{
    emit materialsChanged();
}

// Tells the views to read the assets again.
void LibraryHost::assetsEdited()
{
    emit assetsChanged();
}

}
