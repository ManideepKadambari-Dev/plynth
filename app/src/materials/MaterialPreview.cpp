#include "MaterialPreview.h"

#include "furniture/AssetPreview.h"
#include "theme/Theme.h"

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
MaterialPreview::MaterialPreview(QWidget* parent) : QWidget(parent)
{
    const Theme& t = Theme::instance();
    setFixedWidth(t.previewWidth());
    setToolTip(tr("The material under the cursor: thumbnail, tile size, groups and license"));
    setProperty("tourId", QStringLiteral("material-picker-preview"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(t.unit() * 4, t.unit() * 4, t.unit() * 4, t.unit() * 4);
    column->setSpacing(t.unit() * 2);

    hint_ = new QLabel(tr("Hover a material to see it."), this);
    hint_->setWordWrap(true);
    hint_->setFont(t.uiFont(t.sizeNormal()));
    hint_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    column->addWidget(hint_);

    body_ = new QWidget(this);
    auto* box = new QVBoxLayout(body_);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(t.unit() * 2);

    thumb_ = new QLabel(body_);
    thumb_->setFixedSize(t.thumbnailLarge(), t.thumbnailLarge());
    thumb_->setAlignment(Qt::AlignCenter);
    thumb_->setFont(t.uiFont(t.sizeNormal()));
    box->addWidget(thumb_, 0, Qt::AlignHCenter);

    name_ = new QLabel(body_);
    name_->setFont(t.uiFont(t.sizeLarge(), QFont::DemiBold));
    name_->setWordWrap(true);
    box->addWidget(name_);

    tile_ = new QLabel(body_);
    tile_->setFont(t.monoFont(t.sizeNormal()));
    tile_->setToolTip(tr("The real size of one tile of the texture"));
    box->addWidget(tile_);

    groups_ = new QLabel(body_);
    groups_->setFont(t.uiFont(t.sizeSmall()));
    groups_->setWordWrap(true);
    groups_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    box->addWidget(groups_);

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
void MaterialPreview::showMaterial(const plynth::MaterialRecord& record, const QStringList& groupNames,
                                   const QString& licenseName)
{
    hint_->hide();
    body_->show();
    showThumbnail(thumb_, record, Theme::instance().thumbnailLarge(), devicePixelRatioF());
    name_->setText(fromCore(record.name));
    tile_->setText(tileText(record.tileSize));
    groups_->setText(groupNames.isEmpty() ? tr("Uncategorized") : groupNames.join(QStringLiteral(", ")));
    license_->setText(licenseName);
}

// Hides the details and shows the hint.
void MaterialPreview::clear()
{
    body_->hide();
    hint_->show();
}

// The two sides of a tile in metres, width first.
QString MaterialPreview::tileText(const plynth::TileSize& size)
{
    return QStringLiteral("%1 x %2 m").arg(size.width, 0, 'f', 2).arg(size.height, 0, 'f', 2);
}

// Draws the thumbnail file, or says why there is none.
void MaterialPreview::showThumbnail(QLabel* label, const plynth::MaterialRecord& record, int side, qreal ratio)
{
    const Theme& t = Theme::instance();
    QPixmap pixmap;
    if (record.state != plynth::MaterialState::Broken && record.thumbnail == plynth::ThumbnailState::Ok)
        pixmap = AssetPreview::loadPixmap(QString::fromStdWString(record.thumbFile.wstring()), side, ratio);
    if (!pixmap.isNull()) {
        label->setText(QString());
        label->setStyleSheet(QStringLiteral("background: %1;").arg(t.ground().name()));
        label->setPixmap(pixmap);
        return;
    }
    QString text = QObject::tr("Rendering");
    QColor color = t.muted();
    if (record.state == plynth::MaterialState::Broken || record.thumbnail != plynth::ThumbnailState::Missing) {
        text = record.state == plynth::MaterialState::Broken ? QObject::tr("Broken") : QObject::tr("Render failed");
        color = t.signalAmber();
    }
    label->setPixmap(QPixmap());
    label->setText(text);
    label->setStyleSheet(QStringLiteral("background: %1; color: %2; border-radius: %3px;")
                             .arg(t.raised().name(), color.name())
                             .arg(t.radius()));
}

}
