#include "RightPanel.h"

#include "theme/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace plynth::app {

namespace {

constexpr int kBorderWidth = 1;

}

// Builds the header with the panel name and the close button over the Properties.
RightPanel::RightPanel(QWidget* properties, QWidget* parent) : QWidget(parent)
{
    const Theme& t = Theme::instance();
    setMinimumWidth(t.sidePanelMinWidth());
    setMaximumWidth(t.sidePanelMaxWidth());
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(kBorderWidth, 0, 0, 0);
    column->setSpacing(0);

    auto* head = new QHBoxLayout();
    head->setContentsMargins(t.unit() * 4, t.unit() * 2, t.unit() * 2, t.unit() * 2);
    head->setSpacing(t.unit() * 2);
    auto* title = new QLabel(tr("Properties"), this);
    title->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color: %1;").arg(t.text().name()));
    title->setToolTip(tr("Properties of the selected item, or of the plan"));
    head->addWidget(title, 1);
    close_ = new QPushButton(tr("Hide"), this);
    close_->setFlat(true);
    close_->setFocusPolicy(Qt::NoFocus);
    close_->setFont(t.uiFont(t.sizeSmall()));
    close_->setToolTip(tr("Close the side panel. Open it again with View > Properties panel (Ctrl+Shift+P)"));
    close_->setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; color: %1; padding: 0px %2px; }"
                                         "QPushButton:hover { color: %3; }")
                              .arg(t.muted().name())
                              .arg(t.unit() * 2)
                              .arg(t.text().name()));
    head->addWidget(close_);
    column->addLayout(head);

    properties->setParent(this);
    column->addWidget(properties, 1);
    connect(close_, &QPushButton::clicked, this, &RightPanel::closeRequested);
}

// Paints the Panel background with the Line border on its left edge.
void RightPanel::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.fillRect(rect(), t.panel());
    p.fillRect(QRect(0, 0, kBorderWidth, height()), t.line());
}

}
