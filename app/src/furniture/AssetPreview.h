#pragma once

#include <plynth/core/catalog/AssetCatalog.h>

#include <QPixmap>
#include <QString>
#include <QWidget>

class QLabel;

namespace plynth::app {

class AssetStage;

// The panel that shows one asset: its thumbnail, name, real size, categories and license. It never shows a
// placeholder picture: a missing thumbnail reads "Rendering" and a failed one "Render failed".
class AssetPreview : public QWidget {
    Q_OBJECT
public:
    explicit AssetPreview(QWidget* parent = nullptr);

    // Shows an asset. categoryNames and licenseName are the readable names for the details lines.
    void showAsset(const plynth::AssetRecord& record, const QStringList& categoryNames, const QString& licenseName);
    // True when the shown asset has its model drawn, or none is wanted.
    bool pictureReady() const;
    // Shows the hint to hover an asset.
    void clear();

    // A size as "2.00 x 0.90 x 0.85 m", or "not measured" when Blender has not measured the asset.
    static QString sizeText(const plynth::AssetSize& size);
    // Loads a picture file scaled to fit a square. Null when the file cannot be read.
    static QPixmap loadPixmap(const QString& file, int side, qreal ratio);

private:
    AssetStage* stage_ = nullptr;
    QLabel* thumb_ = nullptr;
    QLabel* name_ = nullptr;
    QLabel* size_ = nullptr;
    QLabel* categories_ = nullptr;
    QLabel* license_ = nullptr;
    QLabel* hint_ = nullptr;
    QWidget* body_ = nullptr;
};

}
