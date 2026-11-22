#include <algorithm>
#include "Theme.h"

#include <QApplication>
#include <QDirIterator>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>

namespace plynth::app {

// Returns the one shared theme.
Theme& Theme::instance()
{
    static Theme theme;
    return theme;
}

// One shared Fusion style for inputs. Other widgets keep the menu look from milestone 2.
QStyle* Theme::controlStyle() const
{
    static QStyle* style = QStyleFactory::create(QStringLiteral("Fusion"));
    return style;
}

// Plain neutral defaults, used only when the theme file cannot be read.
Theme::Theme()
    : ground_(Qt::black), panel_(Qt::darkGray), raised_(Qt::darkGray), line_(Qt::gray),
      text_(Qt::white), muted_(Qt::lightGray), signalBlue_(Qt::lightGray),
      signalAmber_(Qt::gray), onBlue_(Qt::black),
      uiFamily_("Segoe UI"), monoFamily_("Consolas"),
      fallbackUi_("Segoe UI"), fallbackMono_("Consolas")
{
    ansi_.fill(Qt::gray);
}

// Reads colors, fonts and spacing from the JSON file in the resources.
bool Theme::load(const QString& resourcePath)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.isEmpty())
        return false;

    const QJsonObject colors = root["colors"].toObject();
    auto pick = [&](const char* key, QColor& target) {
        const QColor c(colors[key].toString());
        if (c.isValid())
            target = c;
    };
    pick("ground", ground_);
    pick("panel", panel_);
    pick("raised", raised_);
    pick("line", line_);
    pick("text", text_);
    pick("muted", muted_);
    pick("signalBlue", signalBlue_);
    pick("signalAmber", signalAmber_);
    pick("onBlue", onBlue_);

    const QJsonObject fonts = root["fonts"].toObject();
    uiFamily_ = fonts["ui"].toString(uiFamily_);
    monoFamily_ = fonts["mono"].toString(monoFamily_);
    fallbackUi_ = fonts["uiFallback"].toString(fallbackUi_);
    fallbackMono_ = fonts["monoFallback"].toString(fallbackMono_);
    sizeSmall_ = fonts["sizeSmall"].toInt(sizeSmall_);
    sizeNormal_ = fonts["sizeNormal"].toInt(sizeNormal_);
    sizeLarge_ = fonts["sizeLarge"].toInt(sizeLarge_);

    const QJsonObject spacing = root["spacing"].toObject();
    unit_ = spacing["unit"].toInt(unit_);
    titleBarHeight_ = spacing["titleBarHeight"].toInt(titleBarHeight_);
    titleButtonWidth_ = spacing["titleButtonWidth"].toInt(titleButtonWidth_);
    resizeBorder_ = spacing["resizeBorder"].toInt(resizeBorder_);
    radius_ = spacing["radius"].toInt(radius_);
    logoSize_ = spacing["logoSize"].toInt(logoSize_);
    sheetPadding_ = spacing["sheetPadding"].toInt(sheetPadding_);
    pointDot_ = spacing["pointDot"].toInt(pointDot_);
    terminalButton_ = spacing["terminalButton"].toInt(terminalButton_);
    terminalPanelWidth_ = spacing["terminalPanelWidth"].toInt(terminalPanelWidth_);
    terminalPanelHeight_ = spacing["terminalPanelHeight"].toInt(terminalPanelHeight_);
    toolButton_ = spacing["toolButton"].toInt(toolButton_);
    toolStripWidth_ = spacing["toolStripWidth"].toInt(toolStripWidth_);
    propertiesWidth_ = spacing["propertiesWidth"].toInt(propertiesWidth_);
    assetsWidth_ = spacing["assetsWidth"].toInt(assetsWidth_);
    thumbnailTile_ = spacing["thumbnailTile"].toInt(thumbnailTile_);
    thumbnailLarge_ = spacing["thumbnailLarge"].toInt(thumbnailLarge_);
    setupRowGap_ = spacing["setupRowGap"].toInt(setupRowGap_);
    dialogMargin_ = spacing["dialogMargin"].toInt(dialogMargin_);
    snapRadius_ = spacing["snapRadius"].toInt(snapRadius_);
    wallViewMinHeight_ = spacing["wallViewMinHeight"].toInt(wallViewMinHeight_);
    wallViewMargin_ = spacing["wallViewMargin"].toInt(wallViewMargin_);
    handleSize_ = spacing["handleSize"].toInt(handleSize_);
    bendHandle_ = spacing["bendHandle"].toInt(bendHandle_);
    proposalDash_ = spacing["proposalDash"].toInt(proposalDash_);
    pickerColumnWidth_ = spacing["pickerColumnWidth"].toInt(pickerColumnWidth_);
    pickerRowHeight_ = spacing["pickerRowHeight"].toInt(pickerRowHeight_);
    materialSwatch_ = spacing["materialSwatch"].toInt(materialSwatch_);
    materialGridThumb_ = spacing["materialGridThumb"].toInt(materialGridThumb_);
    previewWidth_ = spacing["previewWidth"].toInt(previewWidth_);
    rotationHandle_ = spacing["rotationHandle"].toInt(rotationHandle_);
    itemLabelMinPixels_ = spacing["itemLabelMinPixels"].toInt(itemLabelMinPixels_);
    buildBarHeight_ = spacing["buildBarHeight"].toInt(buildBarHeight_);
    problemMarker_ = spacing["problemMarker"].toInt(problemMarker_);
    qualityMark_ = spacing["qualityMark"].toInt(qualityMark_);
    markStroke_ = spacing["markStroke"].toDouble(markStroke_);
    previewButton_ = spacing["previewButton"].toInt(previewButton_);

    const QJsonObject opacity = root["opacity"].toObject();
    overlay3dOpacity_ = opacity["overlay3d"].toDouble(overlay3dOpacity_);
    floorBelowOpacity_ = opacity["floorBelow"].toDouble(floorBelowOpacity_);
    roofAreaOpacity_ = opacity["roofArea"].toDouble(roofAreaOpacity_);
    const QJsonObject preview = root["preview"].toObject();
    const QColor light(preview["lightColor"].toString());
    if (light.isValid())
        lightColor_ = light;
    const auto readColor = [&](const char* key, QColor& target) {
        const QColor c(preview[key].toString());
        if (c.isValid())
            target = c;
    };
    readColor("skyTopColor", skyTop_);
    readColor("skyHorizonColor", skyHorizon_);
    readColor("groundHorizonColor", groundHorizon_);
    readColor("groundBottomColor", groundBottom_);
    keyLightBrightness_ = preview["keyLightBrightness"].toDouble(keyLightBrightness_);
    probeExposure_ = preview["probeExposure"].toDouble(probeExposure_);
    aoStrength_ = preview["aoStrength"].toInt(aoStrength_);

    const QJsonObject effects = root["effects"].toObject();
    overlayAlpha_ = effects["overlayAlpha"].toInt(overlayAlpha_);
    readOnlyDim_ = effects["readOnlyDim"].toInt(readOnlyDim_);
    terminalSelectionAlpha_ = effects["terminalSelectionAlpha"].toInt(terminalSelectionAlpha_);
    roomFillAlpha_ = effects["roomFillAlpha"].toInt(roomFillAlpha_);
    proposalFillAlpha_ = effects["proposalFillAlpha"].toInt(proposalFillAlpha_);
    proposalLineAlpha_ = effects["proposalLineAlpha"].toInt(proposalLineAlpha_);
    itemFillAlpha_ = effects["itemFillAlpha"].toInt(itemFillAlpha_);

    const QJsonArray ansi = root["terminal"].toObject()["ansi"].toArray();
    for (int i = 0; i < 16 && i < ansi.size(); ++i) {
        const QColor c(ansi[i].toString());
        if (c.isValid())
            ansi_[static_cast<size_t>(i)] = c;
    }
    return true;
}

// Registers the bundled font files. If a family did not load, its fallback name is used.
void Theme::loadBundledFonts()
{
    QDirIterator it(":/fonts", {"*.ttf"}, QDir::Files);
    while (it.hasNext())
        QFontDatabase::addApplicationFont(it.next());

    const QStringList families = QFontDatabase::families();
    if (!families.contains(uiFamily_))
        uiFamily_ = fallbackUi_;
    if (!families.contains(monoFamily_))
        monoFamily_ = fallbackMono_;
}

// Builds a font in the UI family at a pixel size.
QFont Theme::uiFont(int pixelSize, int weight) const
{
    QFont f(uiFamily_);
    f.setPixelSize(pixelSize > 0 ? pixelSize : sizeNormal_);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

// Builds a font in the monospace family for numbers and measurements.
QFont Theme::monoFont(int pixelSize) const
{
    QFont f(monoFamily_);
    f.setPixelSize(pixelSize > 0 ? pixelSize : sizeNormal_);
    return f;
}

// Style sheet for the whole app, made only from theme values.
QString Theme::styleSheet() const
{
    const QString sheet = QStringLiteral(R"(
        QWidget { color: %1; }
        QMainWindow, QDialog { background: %2; }
        QMenuBar { background: transparent; color: %1; }
        QMenuBar::item { background: transparent; padding: %9px %10px; border-radius: %11px; }
        QMenuBar::item:selected, QMenuBar::item:pressed { background: %4; }
        QMenu { background: %3; color: %1; border: 1px solid %5; padding: %9px 0px; }
        QMenu::item { padding: %9px %12px; }
        QMenu::item:selected { background: %4; }
        QMenu::item:disabled { color: %6; }
        QMenu::separator { height: 1px; background: %5; margin: %9px 0px; }
        QToolTip { background: %4; color: %1; border: 1px solid %5; padding: %9px; }
        QPushButton { background: %4; color: %1; border: 1px solid %5; border-radius: %11px; padding: %9px %10px; }
        QPushButton:hover { border-color: %6; }
        QPushButton:default { background: %7; color: %8; border-color: %7; }
        QLabel { background: transparent; }
        QPushButton:disabled, QAbstractSpinBox:disabled, QComboBox:disabled, QCheckBox:disabled { color: %6; }
        QPushButton:disabled { border-color: %5; background: %3; }
        QCheckBox::indicator, QListView::indicator, QTreeView::indicator {
            width: %13px; height: %13px; border: 1px solid %6; border-radius: %14px; background: %2;
        }
        QCheckBox::indicator:hover, QListView::indicator:hover, QTreeView::indicator:hover { border-color: %1; }
        QCheckBox::indicator:checked, QListView::indicator:checked, QTreeView::indicator:checked {
            background: %7; border-color: %7;
        }
        QCheckBox::indicator:disabled, QListView::indicator:disabled, QTreeView::indicator:disabled { border-color: %5; }
        QCheckBox { spacing: %9px; }
    )");
    return sheet
        .arg(text_.name(), ground_.name(), panel_.name(), raised_.name(), line_.name(),
             muted_.name(), signalBlue_.name(), onBlue_.name())
        .arg(unit_)
        .arg(unit_ * 3)
        .arg(radius_)
        .arg(unit_ * 4)
        .arg(unit_ * 3)
        .arg(std::max(1, radius_ / 2));
}

// Style sheet for a vertical or horizontal scroll bar: a thin track with a Line colored handle.
QString Theme::scrollBarStyleSheet() const
{
    const QString sheet = QStringLiteral(R"(
        QScrollBar:vertical { background: %1; width: %4px; margin: 0px; border: none; }
        QScrollBar:horizontal { background: %1; height: %4px; margin: 0px; border: none; }
        QScrollBar::handle { background: %2; border-radius: %5px; min-height: %6px; min-width: %6px; }
        QScrollBar::handle:hover { background: %3; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; background: none; border: none; }
        QScrollBar::add-page, QScrollBar::sub-page { background: none; }
    )");
    return sheet.arg(ground_.name(), line_.name(), muted_.name()).arg(unit_ * 2).arg(unit_).arg(unit_ * 6);
}

// Escapes the sentence and wraps every path or file name in a span that uses the mono font.
QString Theme::withMonoPaths(const QString& text) const
{
    static const QRegularExpression pathPattern(
        QStringLiteral(R"re((?:\.[A-Za-z0-9_-]+[\\/][^\s"'<>|]+|[A-Za-z]:[\\/][^\s"'<>|]+|\\\\[^\s"'<>|]+|[^\s"'<>|\\/]+(?:[\\/][^\s"'<>|\\/]+)*\.(?:png|jpe?g|bmp|tif|tiff|blend|fbx|obj|gltf|glb|zip|json|log|db)))re"),
        QRegularExpression::CaseInsensitiveOption);
    const QString open = QStringLiteral("<span style=\"font-family:'%1';\">").arg(monoFamily_);
    QString out;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = pathPattern.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        qsizetype end = m.capturedEnd();
        QString found = m.captured();
        while (!found.isEmpty() && QStringLiteral(".,;:)").contains(found.back())) {
            found.chop(1);
            --end;
        }
        if (found.isEmpty())
            continue;
        out += text.mid(last, m.capturedStart() - last).toHtmlEscaped();
        out += open + found.toHtmlEscaped() + QStringLiteral("</span>");
        last = end;
    }
    out += text.mid(last).toHtmlEscaped();
    return out;
}

// Sets the palette, default font and style sheet on the running application.
void Theme::apply() const
{
    QPalette p;
    p.setColor(QPalette::Window, ground_);
    p.setColor(QPalette::Base, ground_);
    p.setColor(QPalette::AlternateBase, raised_);
    p.setColor(QPalette::Button, raised_);
    p.setColor(QPalette::WindowText, text_);
    p.setColor(QPalette::Text, text_);
    p.setColor(QPalette::ButtonText, text_);
    p.setColor(QPalette::PlaceholderText, muted_);
    p.setColor(QPalette::ToolTipBase, raised_);
    p.setColor(QPalette::ToolTipText, text_);
    p.setColor(QPalette::Highlight, signalBlue_);
    p.setColor(QPalette::HighlightedText, onBlue_);
    p.setColor(QPalette::Disabled, QPalette::WindowText, muted_);
    p.setColor(QPalette::Disabled, QPalette::Text, muted_);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, muted_);
    QApplication::setPalette(p);
    QApplication::setFont(uiFont());
    qApp->setStyleSheet(styleSheet());
}

}
