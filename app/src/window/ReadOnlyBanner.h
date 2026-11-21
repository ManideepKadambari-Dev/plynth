#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace plynth::app {

// The strip under the title bar that says the project is being edited from the command line.
class ReadOnlyBanner : public QWidget {
    Q_OBJECT
public:
    explicit ReadOnlyBanner(QWidget* parent = nullptr);

    void setTool(const QString& tool);

signals:
    void unlockRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* label_ = nullptr;
    QPushButton* button_ = nullptr;
};

}
