#pragma once

#include <QColor>
#include <array>
#include <QFont>
#include <QString>

namespace plynth::app {

// Every color, font and spacing value the UI uses, read from one JSON file.
class Theme {
public:
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
    int toolButton() const { return toolButton_; }
    int toolStripWidth() const { return toolStripWidth_; }
    int propertiesWidth() const { return propertiesWidth_; }
    int snapRadius() const { return snapRadius_; }
    int terminalButton() const { return terminalButton_; }
    int terminalPanelWidth() const { return terminalPanelWidth_; }
    int terminalPanelHeight() const { return terminalPanelHeight_; }
    QColor terminalAnsi(int index) const { return ansi_[static_cast<size_t>(index)]; }
    QString monoFamily() const { return monoFamily_; }

    QString styleSheet() const;
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
    int toolButton_ = 32;
    int toolStripWidth_ = 44;
    int propertiesWidth_ = 280;
    int snapRadius_ = 10;
    int terminalButton_ = 44;
    int terminalPanelWidth_ = 760;
    int terminalPanelHeight_ = 420;
    std::array<QColor, 16> ansi_;
};

}
