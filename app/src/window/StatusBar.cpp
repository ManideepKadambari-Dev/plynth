#include "StatusBar.h"

#include "theme/PlainText.h"
#include "theme/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTimer>

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
    nameLabel_->setTextFormat(Qt::PlainText);
    nameLabel_->setFont(t.monoFont(t.sizeSmall()));
    nameLabel_->setStyleSheet(mutedStyle);
    nameLabel_->setToolTip(tr("Name of the plan image that is open"));
    layout->addWidget(nameLabel_);

    sizeLabel_ = new QLabel(this);
    sizeLabel_->setFont(t.monoFont(t.sizeSmall()));
    sizeLabel_->setStyleSheet(mutedStyle);
    sizeLabel_->setToolTip(tr("Size of the plan image in pixels"));
    layout->addWidget(sizeLabel_);

    hintLabel_ = new QLabel(this);
    hintLabel_->setTextFormat(Qt::PlainText);
    hintLabel_->setFont(t.uiFont(t.sizeSmall()));
    hintLabel_->setStyleSheet(mutedStyle);
    hintLabel_->setToolTip(tr("What the active tool does with a click"));
    layout->addSpacing(t.unit() * 4);
    layout->addWidget(hintLabel_);

    messageLabel_ = new QLabel(this);
    messageLabel_->setFont(t.uiFont(t.sizeSmall()));
    messageLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
    messageLabel_->setToolTip(tr("The reason the last action was refused"));
    messageLabel_->hide();
    layout->addWidget(messageLabel_);

    layout->addStretch(1);

    cursorLabel_ = new QLabel(this);
    cursorLabel_->setFont(t.monoFont(t.sizeSmall()));
    cursorLabel_->setStyleSheet(mutedStyle);
    cursorLabel_->setToolTip(tr("Position of the cursor on the plan in metres"));
    layout->addWidget(cursorLabel_);

    messageTimer_ = new QTimer(this);
    messageTimer_->setSingleShot(true);
    messageTimer_->setInterval(4000);
    connect(messageTimer_, &QTimer::timeout, this, [this]() {
        messageLabel_->hide();
        hintLabel_->show();
    });

    zoomLabel_ = new QLabel(this);
    zoomLabel_->setFont(t.monoFont(t.sizeSmall()));
    zoomLabel_->setStyleSheet(mutedStyle);
    zoomLabel_->setToolTip(tr("Zoom level of the plan sheet"));
    layout->addWidget(zoomLabel_);
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

// Shows the cursor position text.
void StatusBar::setCursorText(const QString& text)
{
    cursorLabel_->setText(text);
}

// Shows the hint of the active tool.
void StatusBar::setHint(const QString& text)
{
    hintLabel_->setText(text);
}

// Shows a message over the hint for four seconds, in amber when it is a refusal.
void StatusBar::showMessage(const QString& text, bool refusal)
{
    const Theme& t = Theme::instance();
    messageLabel_->setStyleSheet(QStringLiteral("color: %1;").arg((refusal ? t.signalAmber() : t.text()).name()));
    messageLabel_->setTextFormat(Qt::RichText);
    messageLabel_->setText(t.withMonoPaths(text));
    messageLabel_->setToolTip(plainToolTip(text));
    messageLabel_->show();
    hintLabel_->hide();
    messageLabel_->adjustSize();
    layout()->invalidate();
    layout()->activate();
    messageTimer_->start();
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
