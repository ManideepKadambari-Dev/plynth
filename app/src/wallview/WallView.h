#pragma once

#include <plynth/core/Plan.h>

#include <QWidget>

class QLabel;
class QPushButton;

namespace plynth {
class Document;
}

namespace plynth::app {

class Selection;
class ToolController;
class WallFaceView;

// The panel under the plan that shows the face of the selected wall, or of the wall that holds the selected
// opening, at true scale. It reads the document and the selection and draws; it never talks to the plan view.
class WallView : public QWidget {
    Q_OBJECT
public:
    WallView(plynth::Document& document, Selection& selection, ToolController& controller, QWidget* parent = nullptr);

    // Reads the document and the selection again and redraws.
    void refresh();
    // True when the selection is a wall or an opening, so there is a face to show.
    bool hasTarget() const { return hasTarget_; }
    plynth::WallSide side() const { return side_; }
    // Chooses the side the face is seen from.
    void setSide(plynth::WallSide side);
    // Shows the other side of the wall.
    void flip();

signals:
    void targetChanged(bool hasTarget);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    plynth::Document& document_;
    Selection& selection_;
    ToolController& controller_;
    WallFaceView* face_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* length_ = nullptr;
    QLabel* height_ = nullptr;
    QLabel* sideLabel_ = nullptr;
    QPushButton* flipButton_ = nullptr;
    bool hasTarget_ = false;
    plynth::WallSide side_ = plynth::WallSide::Right;
    plynth::Id sideWall_;
};

}
