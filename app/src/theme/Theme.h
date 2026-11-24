#pragma once

#include <QColor>
#include <array>
#include <QFont>
#include <QString>

class QStyle;

namespace plynth::app {

// Every color, font and spacing value the UI uses, read from one JSON file.
class Theme {
public:
    // The style for inputs (spin boxes, combo boxes, check boxes). It draws from the palette, so they follow the theme.
    QStyle* controlStyle() const;

    static Theme& instance();

    bool load(const QString& resourcePath);
    void loadBundledFonts();

    QColor ground() const { return ground_; }
    QColor panel() const { return panel_; }
    QColor raised() const { return raised_; }
    QColor line() const { return line_; }
    QColor text() const { return text_; }
    QColor muted() const { return muted_; }
    QColor signalBlue() const { return signalBlue_; }
    QColor signalAmber() const { return signalAmber_; }
    QColor onBlue() const { return onBlue_; }

    QFont uiFont(int pixelSize = 0, int weight = QFont::Normal) const;
    QFont monoFont(int pixelSize = 0) const;
    int sizeSmall() const { return sizeSmall_; }
    int sizeNormal() const { return sizeNormal_; }
    int sizeLarge() const { return sizeLarge_; }

    int unit() const { return unit_; }
    int titleBarHeight() const { return titleBarHeight_; }
    int titleButtonWidth() const { return titleButtonWidth_; }
    int resizeBorder() const { return resizeBorder_; }
    int radius() const { return radius_; }
    int logoSize() const { return logoSize_; }
    int sheetPadding() const { return sheetPadding_; }
    int pointDot() const { return pointDot_; }
    int overlayAlpha() const { return overlayAlpha_; }
    int readOnlyDim() const { return readOnlyDim_; }
    int terminalSelectionAlpha() const { return terminalSelectionAlpha_; }
    int roomFillAlpha() const { return roomFillAlpha_; }
    int proposalFillAlpha() const { return proposalFillAlpha_; }
    int proposalLineAlpha() const { return proposalLineAlpha_; }
    int proposalDash() const { return proposalDash_; }
    int toolButton() const { return toolButton_; }
    int toolStripWidth() const { return toolStripWidth_; }
    int propertiesWidth() const { return propertiesWidth_; }
    int assetsWidth() const { return assetsWidth_; }
    int sidePanelWidth() const { return sidePanelWidth_; }
    int sidePanelMinWidth() const { return sidePanelMinWidth_; }
    int sidePanelMaxWidth() const { return sidePanelMaxWidth_; }
    int toolWindowWidth() const { return toolWindowWidth_; }
    int toolWindowHeight() const { return toolWindowHeight_; }
    int thumbnailTile() const { return thumbnailTile_; }
    int thumbnailLarge() const { return thumbnailLarge_; }
    int setupRowGap() const { return setupRowGap_; }
    int dialogMargin() const { return dialogMargin_; }
    int snapRadius() const { return snapRadius_; }
    int wallViewMinHeight() const { return wallViewMinHeight_; }
    int wallViewMargin() const { return wallViewMargin_; }
    int handleSize() const { return handleSize_; }
    int bendHandle() const { return bendHandle_; }
    int pickerColumnWidth() const { return pickerColumnWidth_; }
    int pickerRowHeight() const { return pickerRowHeight_; }
    int materialSwatch() const { return materialSwatch_; }
    int materialGridThumb() const { return materialGridThumb_; }
    int previewWidth() const { return previewWidth_; }
    int rotationHandle() const { return rotationHandle_; }
    int itemLabelMinPixels() const { return itemLabelMinPixels_; }
    int itemFillAlpha() const { return itemFillAlpha_; }
    int terminalButton() const { return terminalButton_; }
    int terminalPanelWidth() const { return terminalPanelWidth_; }
    int terminalPanelHeight() const { return terminalPanelHeight_; }
    int buildBarHeight() const { return buildBarHeight_; }
    int problemMarker() const { return problemMarker_; }
    int lightMarker() const { return lightMarker_; }
    int qualityMark() const { return qualityMark_; }
    double markStroke() const { return markStroke_; }
    int previewButton() const { return previewButton_; }
    int helpListWidth() const { return helpListWidth_; }
    double overlay3dOpacity() const { return overlay3dOpacity_; }
    double floorBelowOpacity() const { return floorBelowOpacity_; }
    double roofAreaOpacity() const { return roofAreaOpacity_; }
    double keyLightBrightness() const { return keyLightBrightness_; }
    double probeExposure() const { return probeExposure_; }
    int aoStrength() const { return aoStrength_; }
    QColor lightColor() const { return lightColor_; }
    QColor skyTopColor() const { return skyTop_; }
    QColor skyHorizonColor() const { return skyHorizon_; }
    QColor groundHorizonColor() const { return groundHorizon_; }
    QColor groundBottomColor() const { return groundBottom_; }
    QColor terminalAnsi(int index) const { return ansi_[static_cast<size_t>(index)]; }
    QString monoFamily() const { return monoFamily_; }

    QString styleSheet() const;
    // Style sheet for one scroll bar, from theme colors. Applied to the scroll bars that need it.
    QString scrollBarStyleSheet() const;
    // Rich text for a sentence, with every file name and path set in the mono font.
    QString withMonoPaths(const QString& text) const;
    void apply() const;

private:
    Theme();

    QColor ground_, panel_, raised_, line_, text_, muted_;
    QColor signalBlue_, signalAmber_, onBlue_;
    QString uiFamily_, monoFamily_;
    QString fallbackUi_, fallbackMono_;
    int sizeSmall_ = 11, sizeNormal_ = 13, sizeLarge_ = 16;
    int unit_ = 4, titleBarHeight_ = 36, titleButtonWidth_ = 46;
    int resizeBorder_ = 6, radius_ = 4, logoSize_ = 20;
    int sheetPadding_ = 24;
    int pointDot_ = 9;
    int overlayAlpha_ = 150;
    int readOnlyDim_ = 150;
    int terminalSelectionAlpha_ = 80;
    int roomFillAlpha_ = 36;
    int proposalFillAlpha_ = 40;
    int proposalLineAlpha_ = 220;
    int proposalDash_ = 4;
    int toolButton_ = 32;
    int toolStripWidth_ = 44;
    int propertiesWidth_ = 280;
    int assetsWidth_ = 372;
    int sidePanelWidth_ = 372;
    int sidePanelMinWidth_ = 280;
    int sidePanelMaxWidth_ = 640;
    int toolWindowWidth_ = 960;
    int toolWindowHeight_ = 720;
    int thumbnailTile_ = 128;
    int thumbnailLarge_ = 256;
    int setupRowGap_ = 12;
    int dialogMargin_ = 20;
    int snapRadius_ = 10;
    int wallViewMinHeight_ = 160;
    int wallViewMargin_ = 24;
    int handleSize_ = 8;
    int bendHandle_ = 10;
    int pickerColumnWidth_ = 220;
    int pickerRowHeight_ = 28;
    int materialSwatch_ = 20;
    int materialGridThumb_ = 96;
    int previewWidth_ = 288;
    int rotationHandle_ = 10;
    int itemLabelMinPixels_ = 48;
    int itemFillAlpha_ = 153;
    int terminalButton_ = 44;
    int terminalPanelWidth_ = 760;
    int terminalPanelHeight_ = 420;
    int buildBarHeight_ = 40;
    int problemMarker_ = 12;
    int lightMarker_ = 14;
    int qualityMark_ = 14;
    int previewButton_ = 28;
    int helpListWidth_ = 240;
    double overlay3dOpacity_ = 0.85;
    double floorBelowOpacity_ = 0.35;
    double roofAreaOpacity_ = 0.15;
    double markStroke_ = 1.4;
    double keyLightBrightness_ = 1.0;
    double probeExposure_ = 1.0;
    int aoStrength_ = 60;
    QColor lightColor_ = Qt::white;
    QColor skyTop_ = Qt::lightGray, skyHorizon_ = Qt::lightGray;
    QColor groundHorizon_ = Qt::gray, groundBottom_ = Qt::gray;
    std::array<QColor, 16> ansi_;
};

}
