#pragma once

#include <QWidget>

class QPushButton;

namespace plynth::app {

// The right side of the main window: the Properties of what is selected or in use, under a header with a
// button that closes the panel. It draws the panel background and the border on its left edge.
class RightPanel : public QWidget {
    Q_OBJECT
public:
    RightPanel(QWidget* properties, QWidget* parent = nullptr);
    QSize minimumSizeHint() const override;

signals:
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPushButton* close_ = nullptr;
};

}
