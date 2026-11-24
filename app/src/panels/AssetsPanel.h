#pragma once

#include <plynth/core/catalog/AssetCatalog.h>

#include <QHash>
#include <QPixmap>
#include <QWidget>

#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QPushButton;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QStandardItemModel;
class QTimer;
class QVBoxLayout;

namespace plynth::app {

class AssetStage;
class LibraryHost;

// The Assets tab: search, room and category filters, the thumbnail grid and the details of the selected
// asset. It reads the catalog through LibraryHost and changes assets only with catalog calls.
class AssetsPanel : public QWidget {
    Q_OBJECT
public:
    AssetsPanel(LibraryHost& host, QWidget* parent = nullptr);

    // Reads the catalog again and rebuilds the filters, the grid and the details.
    void refresh();
    // Selects an asset by id. Returns false when it is not in the grid.
    bool selectAsset(const QString& id);
    // For checks only: true when the details show no 3D model, or it has been drawn.
    bool pictureReady() const;
    QString selectedId() const;
    // Scrolls the details to the category list and puts the focus on it.
    void showCategories();
    // Puts a widget (the import queue) between the grid and the details.
    void setQueueView(QWidget* view);
    // Tells the panel whether a render of this asset is already running, so Retry waits.
    void setBusyAssets(const QStringList& ids);
    // Lets the window turn the buttons on and off while no import can start.
    void setImportEnabled(bool enabled, const QString& tip);
    // Lets the window turn "Add to materials catalog" on and off with the library.
    void setMaterialImportEnabled(bool enabled);

signals:
    void importRequested();
    void rescanRequested();
    void setupRequested();
    void retryRequested(const QString& id);
    void filesDropped(const QStringList& paths);
    // Take the material of this asset into the materials catalog.
    void addMaterialRequested(const QString& assetId, const QString& materialName);
    void message(const QString& text);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildControls(QVBoxLayout* column);
    void buildDetails();
    void fillFilters();
    void fillCategoryFilter();
    void fillGrid();
    void showDetails();
    void fillMaterials(const plynth::AssetRecord& record, const std::vector<std::string>& names);
    void applyEdit(const plynth::AssetEdit& edit);
    void removeSelected();
    QPixmap tilePixmap(const plynth::AssetRecord& record);
    QPixmap largePixmap(const plynth::AssetRecord& record);

    LibraryHost& host_;
    bool refreshing_ = false;
    bool retryEnabled_ = false; // imports and renders are available (set by the window)
    std::optional<plynth::AssetRecord> current_;
    QStringList busy_;
    QHash<QString, QPixmap> tileCache_;

    QLineEdit* search_ = nullptr;
    QTimer* searchTimer_ = nullptr;
    QComboBox* room_ = nullptr;
    QComboBox* category_ = nullptr;
    QPushButton* rescan_ = nullptr;
    QPushButton* import_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QLabel* emptyText_ = nullptr;
    QWidget* controls_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QWidget* top_ = nullptr;
    QVBoxLayout* topLayout_ = nullptr;
    QLabel* countLabel_ = nullptr;
    QListView* grid_ = nullptr;
    QStandardItemModel* model_ = nullptr;
    QScrollArea* detailsScroll_ = nullptr;

    AssetStage* stage_ = nullptr;
    QLabel* thumb_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLabel* idLabel_ = nullptr;
    QLabel* sizeLabel_ = nullptr;
    QLabel* trianglesLabel_ = nullptr;
    QLabel* sourceLabel_ = nullptr;
    QComboBox* license_ = nullptr;
    QLineEdit* author_ = nullptr;
    QString shownAuthor_; // the author of the shown asset as saved
    QListWidget* categories_ = nullptr;
    QLineEdit* tags_ = nullptr;
    QLabel* problems_ = nullptr;
    QLabel* materialsTitle_ = nullptr;
    QWidget* materialsBox_ = nullptr;
    QVBoxLayout* materialsColumn_ = nullptr;
    bool importMaterialsEnabled_ = false;
    QPushButton* retry_ = nullptr;
    QPushButton* openFolder_ = nullptr;
    QPushButton* remove_ = nullptr;
};

}
