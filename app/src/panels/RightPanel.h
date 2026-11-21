#pragma once

#include <QWidget>

class QPushButton;
class QStackedWidget;

namespace plynth::app {

// The right side of the window: a tab for Properties, one for Assets and one for the last Build. It draws the panel background
// and the border, and changes its width with the tab.
class RightPanel : public QWidget {
    Q_OBJECT
public:
    RightPanel(QWidget* properties, QWidget* assets, QWidget* build, QWidget* parent = nullptr);

    enum class Tab { Properties, Assets, Build };
    void showTab(Tab tab);
    Tab tab() const { return tab_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Tab tab_ = Tab::Properties;
    QPushButton* propertiesButton_ = nullptr;
    QPushButton* assetsButton_ = nullptr;
    QPushButton* buildButton_ = nullptr;
    QStackedWidget* pages_ = nullptr;
};

}
