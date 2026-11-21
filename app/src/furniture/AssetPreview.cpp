#include "AssetPreview.h"

#include "preview/AssetStage.h"
#include "theme/Theme.h"

#include <QImageReader>
#include <QLabel>
#include <QVBoxLayout>

namespace plynth::app {

namespace {

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}

// Builds the labels from theme fonts and colors. The panel has a fixed width from the theme.
AssetPreview::AssetPreview(QWidget* parent) : QWidget(parent)
{
    const Theme& t = Theme::instance();
    setFixedWidth(t.previewWidth());
    setToolTip(tr("The asset under the cursor: thumbnail, real size, categories and license"));
    setProperty("tourId", QStringLiteral("picker-preview"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(t.unit() * 4, t.unit() * 4, t.unit() * 4, t.unit() * 4);
    column->setSpacing(t.unit() * 2);

    hint_ = new QLabel(tr("Hover an asset to see it."), this);
    hint_->setWordWrap(true);
    hint_->setFont(t.uiFont(t.sizeNormal()));
    hint_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    column->addWidget(hint_);

    body_ = new QWidget(this);
    auto* box = new QVBoxLayout(body_);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(t.unit() * 2);

    stage_ = new AssetStage(false, t.thumbnailLarge(), body_);
    thumb_ = stage_->thumbnail();
    thumb_->setFont(t.uiFont(t.sizeNormal()));
    box->addWidget(stage_, 0, Qt::AlignHCenter);

    name_ = new QLabel(body_);
    name_->setFont(t.uiFont(t.sizeLarge(), QFont::DemiBold));
    name_->setWordWrap(true);
    box->addWidget(name_);

    size_ = new QLabel(body_);
    size_->setFont(t.monoFont(t.sizeNormal()));
    box->addWidget(size_);

    categories_ = new QLabel(body_);
    categories_->setFont(t.uiFont(t.sizeSmall()));
    categories_->setWordWrap(true);
    categories_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    box->addWidget(categories_);

    license_ = new QLabel(body_);
    license_->setFont(t.uiFont(t.sizeSmall()));
    license_->setWordWrap(true);
    license_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    box->addWidget(license_);
    box->addStretch(1);

    column->addWidget(body_, 1);
    body_->hide();
}

// Fills the labels from the record and shows the body.
void AssetPreview::showAsset(const plynth::AssetRecord& record, const QStringList& categoryNames,
                             const QString& licenseName)
{
    const Theme& t = Theme::instance();
    hint_->hide();
    body_->show();

    QPixmap pixmap;
    if (record.state != plynth::AssetState::Broken && record.thumbnail == plynth::ThumbnailState::Ok)
        pixmap = loadPixmap(QString::fromStdWString(record.thumbFile.wstring()), t.thumbnailLarge(), devicePixelRatioF());
    if (!pixmap.isNull()) {
        thumb_->setText(QString());
        thumb_->setStyleSheet(QStringLiteral("background: %1;").arg(t.ground().name()));
        thumb_->setPixmap(pixmap);
    } else {
        QString text = tr("Rendering");
        QColor color = t.muted();
        if (record.state == plynth::AssetState::Broken || record.thumbnail != plynth::ThumbnailState::Missing) {
            text = record.state == plynth::AssetState::Broken ? tr("Broken") : tr("Render failed");
            color = t.signalAmber();
        }
        thumb_->setPixmap(QPixmap());
        thumb_->setText(text);
        thumb_->setStyleSheet(QStringLiteral("background: %1; color: %2; border-radius: %3px;")
                                  .arg(t.raised().name(), color.name())
                                  .arg(t.radius()));
    }
    stage_->showModel(record);
    name_->setText(fromCore(record.name));
    size_->setText(sizeText(record.size));
    categories_->setText(categoryNames.isEmpty() ? tr("Uncategorized") : categoryNames.join(QStringLiteral(", ")));
    license_->setText(licenseName);
}

// True when the model is drawn or none is wanted.
bool AssetPreview::pictureReady() const
{
    return stage_->pictureReady();
}

// Hides the details and shows the hint.
void AssetPreview::clear()
{
    stage_->clearModel();
    body_->hide();
    hint_->show();
}

// The size as three numbers in metres, or "not measured" for an asset Blender has not measured.
QString AssetPreview::sizeText(const plynth::AssetSize& size)
{
    if (!plynth::isMeasured(size))
        return tr("not measured");
    return QStringLiteral("%1 x %2 x %3 m")
        .arg(size.width, 0, 'f', 2)
        .arg(size.depth, 0, 'f', 2)
        .arg(size.height, 0, 'f', 2);
}

// Reads the file at the size it will be drawn, so a large thumbnail is not kept in memory.
QPixmap AssetPreview::loadPixmap(const QString& file, int side, qreal ratio)
{
    QImageReader reader(file);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    const int pixels = qRound(side * ratio);
    if (original.isValid())
        reader.setScaledSize(original.scaled(pixels, pixels, Qt::KeepAspectRatio));
    const QImage image = reader.read();
    if (image.isNull())
        return QPixmap();
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}

}
