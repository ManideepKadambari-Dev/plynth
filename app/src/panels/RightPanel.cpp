#include "RightPanel.h"

#include "theme/TabButton.h"
#include "theme/Theme.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace plynth::app {

namespace {

constexpr int kBorderWidth = 1;

// Makes one flat tab button of the right panel.
QPushButton* makeTab(const QString& text, const QString& tip, const QString& tourId, QWidget* parent)
{
    const Theme& t = Theme::instance();
    return makeTabButton(text, tip, tourId, parent, t.unit() * 2, t.unit() * 2);
}

}

// Builds the tabs over a stack holding their pages.
RightPanel::RightPanel(QWidget* properties, QWidget* assets, QWidget* materials, QWidget* build, QWidget* parent) : QWidget(parent)
{
    const Theme& t = Theme::instance();
    setProperty("tourId", QStringLiteral("panel-right"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(kBorderWidth, 0, 0, 0);
    column->setSpacing(0);

    auto* tabs = new QHBoxLayout();
    tabs->setContentsMargins(t.unit() * 2, t.unit() * 2, t.unit() * 2, 0);
    tabs->setSpacing(0);
    propertiesButton_ = makeTab(tr("Properties"), tr("Properties of the selected item, or of the plan"),
                                QStringLiteral("tab-properties"), this);
    assetsButton_ = makeTab(tr("Assets"), tr("The assets catalog (Ctrl+Shift+A)"), QStringLiteral("tab-assets"), this);
    materialsButton_ = makeTab(tr("Materials"), tr("The materials catalog (Ctrl+Shift+M)"), QStringLiteral("tab-materials"), this);
    buildButton_ = makeTab(tr("Build"), tr("The last build: summary, files, steps and problems"),
                           QStringLiteral("tab-build"), this);
    tabs->addWidget(propertiesButton_, 1);
    tabs->addWidget(assetsButton_, 1);
    tabs->addWidget(materialsButton_, 1);
    tabs->addWidget(buildButton_, 1);
    column->addLayout(tabs);

    pages_ = new QStackedWidget(this);
    properties->setParent(pages_);
    assets->setParent(pages_);
    materials->setParent(pages_);
    build->setParent(pages_);
    pages_->addWidget(properties);
    pages_->addWidget(assets);
    pages_->addWidget(materials);
    pages_->addWidget(build);
    column->addWidget(pages_, 1);

    connect(propertiesButton_, &QPushButton::clicked, this, [this]() { showTab(Tab::Properties); });
    connect(assetsButton_, &QPushButton::clicked, this, [this]() { showTab(Tab::Assets); });
    connect(materialsButton_, &QPushButton::clicked, this, [this]() { showTab(Tab::Materials); });
    connect(buildButton_, &QPushButton::clicked, this, [this]() { showTab(Tab::Build); });
    showTab(Tab::Properties);
}

// Shows one tab and sets the width of the panel for it.
void RightPanel::showTab(Tab tab)
{
    const Theme& t = Theme::instance();
    tab_ = tab;
    propertiesButton_->setChecked(tab == Tab::Properties);
    assetsButton_->setChecked(tab == Tab::Assets);
    materialsButton_->setChecked(tab == Tab::Materials);
    buildButton_->setChecked(tab == Tab::Build);
    pages_->setCurrentIndex(static_cast<int>(tab));
    setFixedWidth(tab == Tab::Properties ? t.propertiesWidth() : t.assetsWidth());
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
