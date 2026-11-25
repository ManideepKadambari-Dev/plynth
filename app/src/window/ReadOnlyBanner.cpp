#include "ReadOnlyBanner.h"

#include "theme/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>

namespace plynth::app {

namespace {

constexpr int kEdgeWidth = 3;
constexpr int kBorderWidth = 1;

}

// Builds the text and the Unlock now button.
ReadOnlyBanner::ReadOnlyBanner(QWidget* parent)
    : QWidget(parent)
{
    const Theme& t = Theme::instance();
    setFixedHeight(t.unit() * 10);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(t.unit() * 4 + kEdgeWidth, 0, t.unit() * 3, kBorderWidth);
    layout->setSpacing(t.unit() * 3);

    label_ = new QLabel(this);
    label_->setFont(t.uiFont(t.sizeNormal()));
    label_->setToolTip(tr("The project is read only while the command line edits it"));
    layout->addWidget(label_, 1);

    button_ = new QPushButton(tr("Unlock now"), this);
    button_->setFont(t.uiFont(t.sizeNormal()));
    button_->setCursor(Qt::PointingHandCursor);
    button_->setToolTip(tr("End the command line session and make the project editable"));
    layout->addWidget(button_);
    connect(button_, &QPushButton::clicked, this, &ReadOnlyBanner::unlockRequested);

    setTool(QString());
}

// Names the tool that holds the session.
void ReadOnlyBanner::setTool(const QString& tool)
{
    label_->setText(tr("Being edited from the command line by %1. Changes appear here as they are saved.")
                        .arg(tool.isEmpty() ? tr("a script") : tool));
}

// Paints the Panel background, the amber left edge and a Line border below.
void ReadOnlyBanner::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.fillRect(rect(), t.panel());
    p.fillRect(QRect(0, height() - kBorderWidth, width(), kBorderWidth), t.line());
    p.fillRect(QRect(0, 0, kEdgeWidth, height()), t.signalAmber());
}

}
