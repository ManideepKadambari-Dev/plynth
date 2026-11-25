#pragma once

#include <plynth/core/catalog/MaterialCatalog.h>

#include <QHash>
#include <QPixmap>
#include <QStringList>
#include <QWidget>

#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
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

class LibraryHost;

// The Materials window: search, a group filter, the thumbnail grid and the details of the selected material. It
// reads the catalog through LibraryHost and changes materials only with catalog calls. A new tile size or Retry
// asks the window to clean the material again.
class MaterialsPanel : public QWidget {
    Q_OBJECT
public:
    MaterialsPanel(LibraryHost& host, QWidget* parent = nullptr);

    // Reads the catalog again and rebuilds the filter, the grid and the details.
    void refresh();
    // Selects a material by id. Returns false when it is not in the grid.
    bool selectMaterial(const QString& id);
    QString selectedId() const;
    // Tells the panel which materials are being cleaned and rendered, so Retry waits for them.
    void setBusyMaterials(const QStringList& ids);
    // Lets the window turn Retry on and off while no Blender job can start.
    void setRenderEnabled(bool enabled);
    // Lets the window turn Import and the sample button on and off with the library.
    void setImportEnabled(bool enabled, const QString& tip);
    // Puts a widget (the import queue) between the grid and the details, and under the empty state text.
    void setQueueView(QWidget* view);
    // Scrolls the details to the group list and puts the focus on it.
    void showGroups();

signals:
    void importRequested();
    void samplesRequested();
    void rescanRequested();
    void setupRequested();
    void retryRequested(const QString& id);
    void message(const QString& text);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void snapSplit();
    void buildControls(QVBoxLayout* column);
    void buildDetails();
    void fillGroupFilter();
    void fillGrid();
    void showDetails();
    void fitDetails();
    void applyEdit(const plynth::MaterialEdit& edit);
    void removeSelected();
    void addGroup();
    void renameGroup();
    void removeGroup();
    void updateGroupButtons();
    QPixmap tilePixmap(const plynth::MaterialRecord& record);

    LibraryHost& host_;
    bool refreshing_ = false;
    bool renderEnabled_ = false;
    std::optional<plynth::MaterialRecord> current_;
    QStringList busy_;
    QHash<QString, QPixmap> tileCache_;

    QLineEdit* search_ = nullptr;
    QTimer* searchTimer_ = nullptr;
    QComboBox* group_ = nullptr;
    QPushButton* rescan_ = nullptr;
    QPushButton* addGroup_ = nullptr;
    QPushButton* renameGroup_ = nullptr;
    QPushButton* removeGroup_ = nullptr;
    QPushButton* import_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QLabel* emptyText_ = nullptr;
    QLabel* emptyHelp_ = nullptr;
    QPushButton* openSetup_ = nullptr;
    QPushButton* samples_ = nullptr;
    QVBoxLayout* column_ = nullptr;
    QWidget* controls_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QLabel* countLabel_ = nullptr;
    QListView* grid_ = nullptr;
    QStandardItemModel* model_ = nullptr;
    QScrollArea* detailsScroll_ = nullptr;

    QLabel* thumb_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLabel* idLabel_ = nullptr;
    QDoubleSpinBox* tileWidth_ = nullptr;
    QDoubleSpinBox* tileHeight_ = nullptr;
    QLabel* textureLabel_ = nullptr;
    QGridLayout* mapsGrid_ = nullptr;
    QWidget* mapsBox_ = nullptr;
    QLabel* sourceLabel_ = nullptr;
    QComboBox* license_ = nullptr;
    QLineEdit* author_ = nullptr;
    QString shownAuthor_; // the author of the shown material as saved
    QListWidget* groups_ = nullptr;
    QLineEdit* tags_ = nullptr;
    QLabel* problems_ = nullptr;
    QPushButton* retry_ = nullptr;
    QPushButton* openFolder_ = nullptr;
    QPushButton* remove_ = nullptr;
};

}
