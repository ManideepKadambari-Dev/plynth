#pragma once

#include <plynth/core/Library.h>
#include <plynth/core/Settings.h>
#include <plynth/core/catalog/AssetCatalog.h>

#include <QObject>
#include <QString>

#include <memory>

namespace plynth::app {

// Holds the machine settings and the open library for the whole window. Views ask it for the catalog and
// listen for its signals, so nothing else opens a database or writes the settings file.
class LibraryHost : public QObject {
    Q_OBJECT
public:
    explicit LibraryHost(QObject* parent = nullptr);

    const plynth::MachineSettings& settings() const { return settings_; }
    // True when settings.json was there at start. False means this is the first run.
    bool settingsExisted() const { return settingsExisted_; }
    // Text of the problem when settings.json could not be read, empty otherwise.
    const QString& settingsProblem() const { return settingsProblem_; }

    // Saves new settings and opens the library they name. Returns the save refusal, if any.
    plynth::Status apply(const plynth::MachineSettings& settings);

    bool hasLibrary() const { return catalog_ != nullptr; }
    plynth::AssetCatalog* catalog() const { return catalog_.get(); }
    const plynth::LibraryPaths& paths() const { return paths_; }
    // Why no library is open, as one plain sentence. Empty while one is open.
    const QString& libraryProblem() const { return libraryProblem_; }

    // A sentence about a custom shape file that was refused when the library opened, or empty.
    const QString& shapeNotice() const { return shapeNotice_; }

    // The folder New project and the terminal start in: the projects setting, else Documents.
    QString projectsFolder() const;

    // Hands the catalog's room types to core after the user edited them and tells the views.
    void roomTypesEdited();
    // Tells the views that assets were added, changed or removed.
    void assetsEdited();
    // Puts a project at the top of the recent list and saves the settings. Only the app calls this, when it opens
    // or saves a project.
    void recordProject(const std::filesystem::path& folder, const std::string& name);
    // Takes a project off the recent list and saves the settings. Nothing on disk is deleted.
    void forgetProject(const std::filesystem::path& folder);
    // Opens the library again, for example after the folder was created or the index rescanned.
    void reopen();

signals:
    void libraryChanged();
    void roomTypesChanged();
    void assetsChanged();
    void recentChanged();

private:
    plynth::MachineSettings settings_;
    bool settingsExisted_ = false;
    QString settingsProblem_;
    std::unique_ptr<plynth::AssetCatalog> catalog_;
    plynth::LibraryPaths paths_;
    QString libraryProblem_;
    QString shapeNotice_;
    std::filesystem::path shapesRegistered_;
};

}
