#include "ScaleDialog.h"

#include "theme/Theme.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace plynth::app {

// Builds the dialog with the distance box, the pixel distance and the buttons.
ScaleDialog::ScaleDialog(double pixelDistance, std::optional<double> presetMetres, bool showKeep, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint)
{
    const Theme& t = Theme::instance();
    setWindowTitle(tr("Set scale"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(t.unit() * 5, t.unit() * 5, t.unit() * 5, t.unit() * 4);
    layout->setSpacing(t.unit() * 2);

    auto* label = new QLabel(tr("Real distance between the two points"), this);
    label->setFont(t.uiFont(t.sizeNormal()));
    layout->addWidget(label);

    distance_ = new QDoubleSpinBox(this);
    distance_->setDecimals(3);
    distance_->setRange(0.01, 1000.0);
    distance_->setSingleStep(0.1);
    distance_->setSuffix(tr(" m"));
    distance_->setFont(t.monoFont(t.sizeNormal()));
    distance_->setStyle(t.controlStyle());
    distance_->setValue(presetMetres.has_value() ? std::clamp(*presetMetres, 0.01, 1000.0) : 1.0);
    distance_->setToolTip(tr("The real length between the two points you clicked, in metres"));
    layout->addWidget(distance_);

    auto* pixels = new QLabel(tr("Measured on the image: %1 px").arg(pixelDistance, 0, 'f', 1), this);
    pixels->setFont(t.monoFont(t.sizeSmall()));
    pixels->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    pixels->setToolTip(tr("The distance between the two clicks in image pixels"));
    layout->addWidget(pixels);

    keep_ = new QCheckBox(tr("Keep marks on the image"), this);
    keep_->setStyle(t.controlStyle());
    keep_->setChecked(true);
    keep_->setVisible(showKeep);
    keep_->setToolTip(tr("On: points and walls stay on the same spot of the image. Off: they keep their real sizes in metres and move on the image."));
    layout->addWidget(keep_);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* ok = buttons->addButton(tr("Set scale"), QDialogButtonBox::AcceptRole);
    QPushButton* cancel = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    ok->setDefault(true);
    ok->setToolTip(tr("Use this distance as the scale of the plan"));
    cancel->setToolTip(tr("Close without changing the scale"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    distance_->selectAll();
    distance_->setFocus();
}

// The distance typed, in metres.
double ScaleDialog::metres() const
{
    return distance_->value();
}

// True when marks should stay on the same spot of the image.
bool ScaleDialog::keepMarksOnImage() const
{
    return keep_->isChecked();
}

}
