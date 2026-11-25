#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;

namespace plynth::app {

// The thin bar above the central area: the Plan and 3D view tabs, the build status, the problems button, the
// buttons that open the app's own windows, the Build button and the button that opens or closes the side panel.
// It only shows state and sends requests.
class BuildBar : public QWidget {
    Q_OBJECT
public:
    enum class View { Plan, Scene3d };
    Q_ENUM(View)

    explicit BuildBar(QWidget* parent = nullptr);

    View view() const { return view_; }
    // Checks the tab of a view without sending a request.
    void setView(View view);

    // Shows a plain status text and hides the progress bar and the Cancel button.
    void setStatus(const QString& text);
    // Shows the progress bar with its message and the Cancel button. A percent below 0 means unknown.
    void setProgress(const QString& message, int percent);
    // Sets what the Cancel button's tooltip says it stops.
    void setCancelTip(const QString& tip);
    // Shows "N problems" in amber, or hides the button when there are none.
    void setProblems(int errors, int warnings);
    // Enables or disables the Build button and sets its tooltip.
    void setBuildEnabled(bool enabled, const QString& tip);
    // Adds a button that asks for one of the app's windows by its name.
    void addWindowButton(const QString& name, const QString& text, const QString& tip);
    // Checks the side panel button without sending a request.
    void setPanelOpen(bool open);

signals:
    void viewRequested(plynth::app::BuildBar::View view);
    void buildRequested();
    void cancelRequested();
    void problemsRequested();
    void windowRequested(const QString& name);
    void panelToggled(bool open);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    View view_ = View::Plan;
    QPushButton* planTab_ = nullptr;
    QPushButton* sceneTab_ = nullptr;
    QLabel* status_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* problems_ = nullptr;
    QPushButton* build_ = nullptr;
    QWidget* windows_ = nullptr;
    QPushButton* panel_ = nullptr;
};

}
