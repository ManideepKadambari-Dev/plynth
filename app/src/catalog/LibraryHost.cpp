#include "LibraryHost.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/OpeningShapes.h>
#include <plynth/core/RoomTypes.h>

#include <QDir>
#include <QStandardPaths>

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
        emit libraryChanged();
        emit roomTypesChanged();
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
    emit libraryChanged();
    emit roomTypesChanged();
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
