#include "UpdateBar.h"

#include "UpdateController.h"
#include "theme/ElideLabel.h"
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

// Builds the sentence, the two versions and the Update and Later buttons.
UpdateBar::UpdateBar(UpdateController& updates, QWidget* parent) : QWidget(parent), updates_(updates)
{
    const Theme& t = Theme::instance();
    setFixedHeight(t.unit() * 10);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(t.unit() * 4 + kEdgeWidth, 0, t.unit() * 3, kBorderWidth);
    layout->setSpacing(t.unit() * 3);

    label_ = new ElideLabel(this);
    label_->setFont(t.uiFont(t.sizeNormal()));
    layout->addWidget(label_, 1);

    versions_ = new QLabel(this);
    versions_->setTextFormat(Qt::PlainText);
    versions_->setFont(t.monoFont(t.sizeSmall()));
    versions_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    layout->addWidget(versions_);

    update_ = new QPushButton(tr("Update"), this);
    update_->setFont(t.uiFont(t.sizeNormal()));
    update_->setDefault(true);
    update_->setCursor(Qt::PointingHandCursor);
    update_->setToolTip(tr("Download and verify the new version. You can keep working while it downloads"));
    layout->addWidget(update_);
    connect(update_, &QPushButton::clicked, this, &UpdateBar::updateRequested);

    later_ = new QPushButton(tr("Later"), this);
    later_->setFont(t.uiFont(t.sizeNormal()));
    later_->setCursor(Qt::PointingHandCursor);
    later_->setToolTip(tr("Hide this notice until Plynth starts again. Help, Update still works"));
    layout->addWidget(later_);
    connect(later_, &QPushButton::clicked, &updates_, &UpdateController::dismissNotice);

    connect(&updates_, &UpdateController::changed, this, &UpdateBar::refresh);
    refresh();
}

// Shows or hides the bar and fills in the versions of the offer.
void UpdateBar::refresh()
{
    const bool shown = updates_.noticeShown() && updates_.offer().has_value() && updates_.installed();
    setVisible(shown);
    if (!shown)
        return;
    const QString version = updates_.offerVersion();
    label_->setText(updates_.offer()->earlyPreview
                        ? tr("Plynth %1 is available, an early preview.").arg(version)
                        : tr("Plynth %1 is available.").arg(version));
    versions_->setText(tr("%1 to %2").arg(updates_.currentVersion(), version));
}

// Paints the Panel background, the blue left edge and a Line border below.
void UpdateBar::paintEvent(QPaintEvent*)
{
    const Theme& t = Theme::instance();
    QPainter p(this);
    p.fillRect(rect(), t.panel());
    p.fillRect(QRect(0, height() - kBorderWidth, width(), kBorderWidth), t.line());
    p.fillRect(QRect(0, 0, kEdgeWidth, height()), t.signalBlue());
}

}
