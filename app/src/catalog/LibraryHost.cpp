#include "LibraryHost.h"

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

// Saves the settings, then opens the library they name.
plynth::Status LibraryHost::apply(const plynth::MachineSettings& settings)
{
    const plynth::Status status = plynth::saveSettings(settings);
    if (!status.ok())
        return status;
    settings_ = settings;
    settingsExisted_ = true;
    settingsProblem_.clear();
    reopen();
    return status;
}

// Opens assets.db of the chosen library, hands its room types to core and registers its shapes once.
void LibraryHost::reopen()
{
    catalog_.reset();
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

// Tells the views to read the assets again.
void LibraryHost::assetsEdited()
{
    emit assetsChanged();
}

}
