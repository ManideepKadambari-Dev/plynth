#include "AssetStage.h"

#include "GraphicsProbe.h"
#include "ModelView.h"

#include <QLabel>
#include <QUrl>

#include <filesystem>

namespace plynth::app {

// Makes the label that covers the square. The 3D view is made when the first model is asked for.
AssetStage::AssetStage(bool interactive, int side, QWidget* parent) : QWidget(parent), interactive_(interactive)
{
    setFixedSize(side, side);
    thumb_ = new QLabel(this);
    thumb_->setFixedSize(side, side);
    thumb_->setAlignment(Qt::AlignCenter);
}

// Makes the 3D view behind the label, once.
void AssetStage::ensureView()
{
    if (view_ != nullptr || !appGraphics().ok)
        return;
    view_ = new ModelView(interactive_, false, this);
    view_->setFixedSize(size());
    view_->move(0, 0);
    view_->hide();
    thumb_->raise();
    connect(view_, &ModelView::pictureDrawn, this, [this]() {
        if (view_->state() == ModelView::State::Ready)
            thumb_->hide();
    });
}

// Loads asset.fbx of the record, sized from the catalog's measurements (pivot at the bottom centre).
void AssetStage::showModel(const plynth::AssetRecord& record)
{
    thumb_->show();
    std::error_code ignored;
    const bool usable = record.state != plynth::AssetState::Broken && !record.fbxFile.empty() &&
                        std::filesystem::exists(record.fbxFile, ignored) && plynth::isMeasured(record.size);
    if (!usable) {
        clearModel();
        return;
    }
    ensureView();
    if (view_ == nullptr)
        return;
    const float halfW = static_cast<float>(record.size.width / 2);
    const float halfD = static_cast<float>(record.size.depth / 2);
    view_->setModel(QUrl::fromLocalFile(QString::fromStdWString(record.fbxFile.wstring())),
                    QVector3D(-halfW, 0.0f, -halfD),
                    QVector3D(halfW, static_cast<float>(record.size.height), halfD));
    view_->setPreset(ModelView::Preset::ThreeQuarter);
    view_->show();
}

// Hides the 3D view and shows the label.
void AssetStage::clearModel()
{
    thumb_->show();
    if (view_ != nullptr) {
        view_->clearModel(QString());
        view_->hide();
    }
}

// True when the model is drawn, or none is wanted.
bool AssetStage::pictureReady() const
{
    if (view_ == nullptr || view_->isHidden())
        return true;
    return view_->pictureReady();
}

}
