#include "StatusBar.h"

#include "theme/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>

namespace plynth::app {

namespace {

constexpr int kBorderWidth = 1;

}

// Builds the labels with theme fonts and the Muted color.
StatusBar::StatusBar(QWidget* parent)
    : QWidget(parent)
{
    const Theme& t = Theme::instance();
    setFixedHeight(t.unit() * 7);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(t.unit() * 3, kBorderWidth, t.unit() * 3, 0);
    layout->setSpacing(t.unit() * 2);

    const QString mutedStyle = QStringLiteral("color: %1;").arg(t.muted().name());

    nameLabel_ = new QLabel(this);
    nameLabel_->setFont(t.uiFont(t.sizeSmall()));
    nameLabel_->setStyleSheet(mutedStyle);
    nameLabel_->setToolTip(tr("Name of the plan image that is open"));
    layout->addWidget(nameLabel_);

    sizeLabel_ = new QLabel(this);
    sizeLabel_->setFont(t.monoFont(t.sizeSmall()));
    sizeLabel_->setStyleSheet(mutedStyle);
    sizeLabel_->setToolTip(tr("Size of the plan image in pixels"));
    layout->addWidget(sizeLabel_);

    layout->addStretch(1);

    zoomLabel_ = new QLabel(this);
    zoomLabel_->setFont(t.monoFont(t.sizeSmall()));
    zoomLabel_->setStyleSheet(mutedStyle);
    zoomLabel_->setToolTip(tr("Zoom level of the plan sheet"));
    layout->addWidget(zoomLabel_);

    setProperty("tourId", QStringLiteral("status-bar"));
    setToolTip(tr("Status bar: image name, image size and zoom"));
}

// Shows the file name and pixel size.
void StatusBar::setImageInfo(const QString& fileName, int width, int height)
{
    nameLabel_->setText(fileName);
    sizeLabel_->setText(QStringLiteral("%1 x %2 px").arg(width).arg(height));
}

// Empties the name and size.
void StatusBar::clearImageInfo()
{
    nameLabel_->clear();
    sizeLabel_->clear();
}

// Shows the zoom as a whole percent.
void StatusBar::setZoom(double zoom)
{
    zoomLabel_->setText(QStringLiteral("%1%").arg(qRound(zoom * 100.0)));
}

// Hides the zoom readout.
void StatusBar::clearZoom()
{
    zoomLabel_->clear();
}

// Paints the Panel background and the Line border on top.
void StatusBar::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.fillRect(rect(), t.panel());
    p.fillRect(QRect(0, 0, width(), kBorderWidth), t.line());
}

}
