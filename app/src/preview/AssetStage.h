#pragma once

#include <plynth/core/catalog/AssetCatalog.h>

#include <QWidget>

class QLabel;

namespace plynth::app {

class ModelView;

// A square place that shows an asset: its thumbnail label on top, and the live 3D model behind it. The label
// goes away once the model is drawn, and stays when the asset has no asset.fbx or the load fails.
class AssetStage : public QWidget {
    Q_OBJECT
public:
    // interactive lets the mouse orbit and zoom the model. side is the width and height in pixels.
    AssetStage(bool interactive, int side, QWidget* parent = nullptr);

    // The label the host fills with the thumbnail or the reason there is none.
    QLabel* thumbnail() const { return thumb_; }
    // Loads the model of the record behind the thumbnail, or keeps only the thumbnail.
    void showModel(const plynth::AssetRecord& record);
    // Drops the model.
    void clearModel();
    // True when nothing is loading or the model has been drawn.
    bool pictureReady() const;

private:
    void ensureView();

    bool interactive_;
    QLabel* thumb_ = nullptr;
    ModelView* view_ = nullptr;
};

}
