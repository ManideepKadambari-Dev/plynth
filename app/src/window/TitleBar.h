#pragma once

#include <QAbstractButton>
#include <QPixmap>
#include <QWidget>

class QLabel;
class QMenu;
class QMenuBar;

namespace plynth::app {

// One of the three flat buttons at the right of the title bar, drawn with lines.
class TitleButton : public QAbstractButton {
    Q_OBJECT
public:
    enum class Kind { Minimize, Maximize, Restore, Close };

    explicit TitleButton(Kind kind, QWidget* parent = nullptr);

    void setKind(Kind kind);
    void setHovered(bool hovered);
    void setPressedLook(bool pressed);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    Kind kind_;
    bool hovered_ = false;
    bool pressedLook_ = false;
};

// The window title bar: logo, menus, project name and the window buttons.
class TitleBar : public QWidget {
    Q_OBJECT
public:
    enum class Hit { Caption, Client, MaximizeButton };

    explicit TitleBar(QWidget* parent = nullptr);

    QMenuBar* menuBar() const { return menuBar_; }
    QMenu* fileMenu() const { return fileMenu_; }
    QMenu* editMenu() const { return editMenu_; }
    QMenu* helpMenu() const { return helpMenu_; }
    QMenu* viewMenu() const { return viewMenu_; }
    QMenu* catalogMenu() const { return catalogMenu_; }
    TitleButton* maximizeButton() const { return maximizeButton_; }

    void setProjectName(const QString& name);
    void setMaximized(bool maximized);

    Hit hitTest(const QPoint& localPos) const;

signals:
    void minimizeRequested();
    void maximizeToggleRequested();
    void closeRequested();

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void updateLogo();

    QLabel* logo_ = nullptr;
    QMenuBar* menuBar_ = nullptr;
    QMenu* fileMenu_ = nullptr;
    QMenu* editMenu_ = nullptr;
    QMenu* helpMenu_ = nullptr;
    QMenu* viewMenu_ = nullptr;
    QMenu* catalogMenu_ = nullptr;
    TitleButton* minimizeButton_ = nullptr;
    TitleButton* maximizeButton_ = nullptr;
    TitleButton* closeButton_ = nullptr;
    QString projectName_;
};

}
