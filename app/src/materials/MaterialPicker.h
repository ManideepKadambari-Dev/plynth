#pragma once

#include <QPoint>
#include <QString>
#include <QWidget>

#include <vector>

class QHBoxLayout;
class QListWidget;
class QListWidgetItem;

namespace plynth::app {

class LibraryHost;
class MaterialPreview;

// The cascading dropdown for choosing a material: a column of groups, a column of materials opening beside
// it, and a preview panel beside the materials. The first row clears the setting. It is one widget drawn over
// the window, not a menu, so it follows the theme and shows up in screenshots. It only reports choices; the
// caller runs the command. It lists materials only, never assets.
class MaterialPicker : public QWidget {
    Q_OBJECT
public:
    MaterialPicker(LibraryHost& host, QWidget* parent);
    ~MaterialPicker() override;

    // Opens beside a point in the parent's coordinates. clearText is the first row, for example "Use the
    // default". currentId, when it is in the catalog, has its group opened and its row marked.
    void openAt(const QPoint& anchor, const QString& clearText, const QString& currentId);
    // Closes the picker without choosing.
    void dismiss();
    bool isOpen() const { return open_; }
    // Opens the path to a material and hovers it, as the mouse would. False when it is not in the catalog.
    bool hoverMaterial(const QString& id);
    const QString& hoveredMaterial() const { return hovered_; }
    // Chooses a material as a click on its row would. For checks only.
    void choose(const QString& id);

signals:
    void materialChosen(const QString& id);
    // The first row was clicked.
    void cleared();
    void dismissed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    enum class Kind { Groups, Materials };
    struct Column {
        Kind kind = Kind::Groups;
        QListWidget* list = nullptr;
    };

    void reset();
    QListWidget* addColumn(Kind kind);
    void trimColumns(int keep);
    void fillGroups(QListWidget* list);
    void fillMaterials(QListWidget* list, const QString& group);
    void rowClicked(int column, QListWidgetItem* item);
    void setHovered(const QString& id);
    void relayout();
    void openGroup(const QString& group, const QString& material);
    void selectRow(int column, const QString& key);

    LibraryHost& host_;
    QHBoxLayout* row_ = nullptr;
    MaterialPreview* preview_ = nullptr;
    std::vector<Column> columns_;
    QPoint anchor_;
    QString clearText_;
    QString current_;
    QString hovered_;
    bool flipped_ = false;
    bool open_ = false;
};

}
