#pragma once

#include <plynth/core/catalog/MaterialCatalog.h>

#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;

namespace plynth::app {

// The panel that shows one material: its thumbnail, name, tile size, groups and license. It never shows a
// placeholder picture: a missing thumbnail reads "Rendering" and a failed one "Render failed".
class MaterialPreview : public QWidget {
    Q_OBJECT
public:
    explicit MaterialPreview(QWidget* parent = nullptr);

    // Shows a material. groupNames and licenseName are the readable names for the details lines.
    void showMaterial(const plynth::MaterialRecord& record, const QStringList& groupNames, const QString& licenseName);
    // Shows the hint to hover a material.
    void clear();

    // A tile size as "1.20 x 0.60 m".
    static QString tileText(const plynth::TileSize& size);
    // Sets a label to a thumbnail scaled to a square, or to the reason there is none.
    static void showThumbnail(QLabel* label, const plynth::MaterialRecord& record, int side, qreal ratio);

private:
    QLabel* thumb_ = nullptr;
    QLabel* name_ = nullptr;
    QLabel* tile_ = nullptr;
    QLabel* groups_ = nullptr;
    QLabel* license_ = nullptr;
    QLabel* hint_ = nullptr;
    QWidget* body_ = nullptr;
};

}
