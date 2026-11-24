#pragma once

#include <plynth/core/Plan.h>

#include <QPoint>
#include <QString>
#include <QWidget>

class QListWidget;
class QListWidgetItem;

namespace plynth::app {

class AssetPreview;
class LibraryHost;

// What an opening gets in place of a door or window: a fitting catalog asset picked by the build, the fill
// Plynth builds, or one named catalog asset.
enum class OpeningFrameKind { Automatic, BuiltIn, Asset };

// The dropdown for choosing the door or window of an opening: Automatic, Built by Plynth, then every asset of
// the opening's category (door or window) with the asset preview beside the list on hover. It is one widget
// drawn over the window, not a menu, so it follows the theme and shows up in screenshots. It only reports the
// choice; the caller runs the command.
class OpeningAssetPicker : public QWidget {
    Q_OBJECT
public:
    OpeningAssetPicker(LibraryHost& host, QWidget* parent);
    ~OpeningAssetPicker() override;

    // Opens beside a point in the parent's coordinates for a door or a window, with the current choice marked.
    void openAt(const QPoint& anchor, plynth::OpeningType type, OpeningFrameKind current, const QString& currentAsset);
    // Closes the picker without choosing.
    void dismiss();
    bool isOpen() const { return open_; }
    // Hovers an asset row as the mouse would. False when it is not listed.
    bool hoverAsset(const QString& id);
    // The catalog category an opening type takes its assets from: "door" or "window".
    static QString categoryFor(plynth::OpeningType type);

signals:
    // asset is empty unless kind is Asset.
    void chosen(plynth::app::OpeningFrameKind kind, const QString& asset);
    void dismissed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void fill(plynth::OpeningType type, OpeningFrameKind current, const QString& currentAsset);
    void rowClicked(QListWidgetItem* item);
    void setHovered(const QString& id);
    void relayout();

    LibraryHost& host_;
    QListWidget* list_ = nullptr;
    AssetPreview* preview_ = nullptr;
    QPoint anchor_;
    QString hovered_;
    bool open_ = false;
};

}
