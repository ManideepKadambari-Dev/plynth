#pragma once

#include <plynth/core/catalog/MaterialImport.h>

#include <QString>
#include <QStringList>
#include <QWidget>

#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace plynth::app {

class ImportQueueView;
class LibraryHost;
class MaterialImportQueue;

// The material import screen: pick folders, zips, image files or a .blend, set the fields of the material and
// queue the imports. It only builds requests; the queue runs them.
class MaterialImportScreen : public QWidget {
    Q_OBJECT
public:
    MaterialImportScreen(LibraryHost& host, MaterialImportQueue& queue, QWidget* parent = nullptr);

    // Adds files and folders to the source list. Ones no importer takes are named in a note and left out.
    void addSources(const QStringList& paths);
    // Reads the groups and licenses of the open library again.
    void refreshLists();
    // Lets the window turn Import on and off with the library.
    void setLibraryOpen(bool open);

signals:
    void backRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    QWidget* buildSources();
    QWidget* buildForm();
    void updateState();
    void chooseFolder();
    void chooseFiles();
    void importAll();
    plynth::MaterialImportRequest makeRequest(const QString& path) const;

    LibraryHost& host_;
    MaterialImportQueue& queue_;
    bool libraryOpen_ = false;
    QString lastFolder_;

    QListWidget* sources_ = nullptr;
    QLabel* note_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* clear_ = nullptr;
    QPushButton* import_ = nullptr;
    ImportQueueView* queueView_ = nullptr;

    QLineEdit* name_ = nullptr;
    QListWidget* groups_ = nullptr;
    QLineEdit* tags_ = nullptr;
    QComboBox* license_ = nullptr;
    QCheckBox* setTile_ = nullptr;
    QDoubleSpinBox* tileWidth_ = nullptr;
    QDoubleSpinBox* tileHeight_ = nullptr;
    QComboBox* normal_ = nullptr;
    QLineEdit* blendMaterial_ = nullptr;
    QComboBox* bake_ = nullptr;
    std::array<QLineEdit*, 6> channelFiles_{};
    std::array<QPushButton*, 6> channelBrowse_{};
};

}
