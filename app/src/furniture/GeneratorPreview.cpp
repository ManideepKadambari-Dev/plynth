#include "GeneratorPreview.h"

#include "theme/Theme.h"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace plynth::app {

namespace {

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// A default value as shown in the list: the choice label, yes or no, none for a neutral material, or the
// number with its unit.
QString defaultText(const plynth::GeneratorParam& param, const plynth::GeneratorValue& value)
{
    if (param.type == plynth::GeneratorParamType::Boolean)
        return std::get<bool>(value) ? QObject::tr("yes") : QObject::tr("no");
    if (param.type == plynth::GeneratorParamType::Choice) {
        const std::string& id = std::get<std::string>(value);
        for (const plynth::GeneratorChoice& choice : param.choices)
            if (choice.id == id)
                return fromCore(choice.label);
        return fromCore(id);
    }
    QString text = fromCore(plynth::generatorValueText(value));
    if (param.type == plynth::GeneratorParamType::Number && param.unit == plynth::GeneratorUnit::Metres)
        text += QStringLiteral(" m");
    else if (param.type == plynth::GeneratorParamType::Number && param.unit == plynth::GeneratorUnit::Degrees)
        text += QStringLiteral(" deg");
    return text;
}

}

// Builds the labels from theme fonts and colors. The panel has the same fixed width as the asset preview.
GeneratorPreview::GeneratorPreview(QWidget* parent) : QWidget(parent)
{
    const Theme& t = Theme::instance();
    setFixedWidth(t.previewWidth());
    setToolTip(tr("The generator under the cursor: its size, parameters and the add-ons it needs"));
    setProperty("tourId", QStringLiteral("picker-generator-preview"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(t.unit() * 4, t.unit() * 4, t.unit() * 4, t.unit() * 4);
    column->setSpacing(t.unit() * 2);

    hint_ = new QLabel(tr("Hover a generator to see what it makes."), this);
    hint_->setWordWrap(true);
    hint_->setFont(t.uiFont(t.sizeNormal()));
    hint_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    column->addWidget(hint_);

    body_ = new QWidget(this);
    auto* box = new QVBoxLayout(body_);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(t.unit() * 2);

    name_ = new QLabel(body_);
    name_->setFont(t.uiFont(t.sizeLarge(), QFont::DemiBold));
    name_->setWordWrap(true);
    box->addWidget(name_);

    description_ = new QLabel(body_);
    description_->setFont(t.uiFont(t.sizeNormal()));
    description_->setWordWrap(true);
    box->addWidget(description_);

    size_ = new QLabel(body_);
    size_->setFont(t.monoFont(t.sizeNormal()));
    size_->setToolTip(tr("The size it makes with the default values"));
    box->addWidget(size_);

    rows_ = new QWidget(body_);
    grid_ = new QGridLayout(rows_);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(t.unit() * 3);
    grid_->setVerticalSpacing(t.unit());
    grid_->setColumnStretch(0, 1);
    box->addWidget(rows_);
    box->addStretch(1);

    column->addWidget(body_, 1);
    body_->hide();
}

// Fills the labels and the parameter rows from the declaration, then shows the body.
void GeneratorPreview::showGenerator(const plynth::GeneratorInfo& info, const std::optional<plynth::AddonCache>& cache,
                                     double ceilingHeight)
{
    hint_->hide();
    body_->show();
    name_->setText(fromCore(info.name));
    description_->setText(fromCore(info.description));
    description_->setVisible(!info.description.empty());

    const std::vector<plynth::GeneratorParamValue> values =
        plynth::defaultGeneratorParams(info, ceilingHeight);
    const plynth::GeneratorSize size = plynth::generatorSize(info, values);
    size_->setText(size.status.ok() ? sizeText(size.size) : QString());

    while (QLayoutItem* old = grid_->takeAt(0)) {
        delete old->widget();
        delete old;
    }
    if (!info.params.empty())
        addHeading(tr("Parameters"));
    for (const plynth::GeneratorParam& param : info.params) {
        const plynth::GeneratorValue* value = plynth::findGeneratorValue(values, param.id);
        addRow(fromCore(param.label), value != nullptr ? defaultText(param, *value) : QString());
    }
    if (!info.addons.empty())
        addHeading(tr("Add-ons"));
    for (const std::string& addon : info.addons) {
        QString state = tr("not checked");
        if (cache.has_value())
            state = plynth::addonEnabled(*cache, addon) ? tr("found") : tr("missing");
        addRow(fromCore(plynth::addonDisplayName(addon)), state);
    }
}

// Adds a small Muted heading that spans both columns.
void GeneratorPreview::addHeading(const QString& text)
{
    const Theme& t = Theme::instance();
    auto* heading = new QLabel(text, rows_);
    heading->setFont(t.uiFont(t.sizeSmall(), QFont::DemiBold));
    heading->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    grid_->addWidget(heading, grid_->rowCount(), 0, 1, 2);
    heading->show();
}

// Adds one row: a label on the left and its value in mono on the right, both Muted.
void GeneratorPreview::addRow(const QString& left, const QString& right)
{
    const Theme& t = Theme::instance();
    auto* a = new QLabel(left, rows_);
    a->setFont(t.uiFont(t.sizeSmall()));
    a->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    auto* b = new QLabel(right, rows_);
    b->setFont(t.monoFont(t.sizeSmall()));
    b->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    b->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    const int row = grid_->rowCount();
    grid_->addWidget(a, row, 0);
    grid_->addWidget(b, row, 1);
    a->show();
    b->show();
}

// Hides the details and shows the hint.
void GeneratorPreview::clear()
{
    body_->hide();
    hint_->show();
}

// Formats a size in metres as width x depth x height.
QString GeneratorPreview::sizeText(const plynth::ItemSize& size)
{
    return QStringLiteral("%1 x %2 x %3 m")
        .arg(size.width, 0, 'f', 2)
        .arg(size.depth, 0, 'f', 2)
        .arg(size.height, 0, 'f', 2);
}

}
