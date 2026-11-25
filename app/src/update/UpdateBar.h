#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace plynth::app {

class UpdateController;

// The notice under the title bar that a newer signed release is ready, with Update and Later. It shows only
// while the controller says so, which never happens in a copy that is not installed.
class UpdateBar : public QWidget {
    Q_OBJECT
public:
    explicit UpdateBar(UpdateController& updates, QWidget* parent = nullptr);

signals:
    void updateRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void refresh();

    UpdateController& updates_;
    QLabel* label_ = nullptr;
    QLabel* versions_ = nullptr;
    QPushButton* update_ = nullptr;
    QPushButton* later_ = nullptr;
};

}
