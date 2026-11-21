#pragma once

#include <QAbstractButton>
#include <QSize>
#include <QWidget>

namespace plynth::app {

// The round button in the corner of the plan area that shows or hides the terminal.
class TerminalButton : public QAbstractButton {
    Q_OBJECT
public:
    explicit TerminalButton(QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

// A small square at the top left of the panel that resizes it when dragged.
class TerminalGrip : public QWidget {
    Q_OBJECT
public:
    explicit TerminalGrip(QWidget* parent = nullptr);

signals:
    // The size the panel would have, with its bottom right corner kept in place.
    void sizeRequested(const QSize& size);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool dragging_ = false;
    QPoint startGlobal_;
    QSize startSize_;
};

// The frame around the terminal view: Panel background, Line border and rounded corners.
class TerminalPanel : public QWidget {
    Q_OBJECT
public:
    explicit TerminalPanel(QWidget* parent = nullptr);
    TerminalGrip* grip() const { return grip_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    TerminalGrip* grip_ = nullptr;
};

}
