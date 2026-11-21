#pragma once

#include <QPoint>
#include <QString>
#include <QWidget>

#include <vector>

class QHBoxLayout;
class QListWidget;
class QListWidgetItem;

namespace plynth::app {

class AssetPreview;
class LibraryHost;

// The cascading dropdown for placing furniture: a column per level (room type, category, asset) opening
// beside the one before, and a preview panel beside the asset column. It is one widget drawn over the plan,
// not a menu, so it follows the theme and shows up in screenshots. It only reports choices; the tool places.
class FurniturePicker : public QWidget {
    Q_OBJECT
public:
    FurniturePicker(LibraryHost& host, QWidget* parent);
    ~FurniturePicker() override;

    // Opens near a point in the parent's coordinates. With a room type the first column lists that type's
    // categories; with an empty one it lists the room types.
    void openAt(const QPoint& anchor, const QString& roomType);
    // Closes the picker without choosing.
    void dismiss();
    bool isOpen() const { return open_; }
    // Opens the path to an asset and hovers it, as the mouse would. False when it is not in the catalog or
    // has no category to find it by.
    bool hoverAsset(const QString& id);
    // The asset under the cursor, empty when none.
    const QString& hoveredAsset() const { return hovered_; }

signals:
    void assetChosen(const QString& id);
    // The id of the asset under the cursor, or empty when the cursor left the asset column.
    void assetHovered(const QString& id);
    void dismissed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    enum class Kind { RoomTypes, Categories, Assets };
    struct Column {
        Kind kind = Kind::RoomTypes;
        QListWidget* list = nullptr;
    };

    void reset();
    QListWidget* addColumn(Kind kind, const QString& key);
    void trimColumns(int keep);
    void fillRoomTypes(QListWidget* list);
    void fillCategories(QListWidget* list, const QString& roomType, bool withAllRow);
    void fillAssets(QListWidget* list, const QString& category);
    void rowClicked(int column, QListWidgetItem* item);
    void setHovered(const QString& id);
    void relayout();
    void openPath(const QString& roomType, const QString& category, const QString& asset, bool browse);
    void selectRow(int column, const QString& key);
    QString categoryName(const QString& id) const;

    LibraryHost& host_;
    QHBoxLayout* row_ = nullptr;
    AssetPreview* preview_ = nullptr;
    std::vector<Column> columns_;
    QPoint anchor_;
    QString roomType_;
    QString hovered_;
    bool filtered_ = false;
    bool flipped_ = false;
    bool open_ = false;
};

}
