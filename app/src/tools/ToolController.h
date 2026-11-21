#pragma once

#include "canvas/PlanCanvas.h"
#include "document/Selection.h"
#include "tools/Tool.h"

#include <plynth/core/Document.h>

#include <QObject>
#include <QPointer>
#include <QString>

#include <array>
#include <memory>
#include <optional>

class QWidget;

namespace plynth::app {

class DetectionSession;

// Owns the tools, says which one is active, and gives them the document, the selection and the canvas.
class ToolController : public QObject {
    Q_OBJECT
public:
    ToolController(plynth::Document& document, PlanCanvas* canvas, Selection* selection, QWidget* dialogParent,
                   QObject* parent = nullptr);
    ~ToolController() override;

    ToolId active() const { return active_; }
    // Switches tool. Does nothing when the tool is not available now.
    bool setActive(ToolId id);
    // Whether a tool can be used now, and when not, why.
    bool isAvailable(ToolId id) const;
    QString unavailableReason(ToolId id) const;
    // Re-checks availability after the plan or the read only state changed.
    void refreshAvailability();
    // The document changed. Forwards it to the active tool.
    void documentChanged(unsigned changes);

    QString hint() const;

    plynth::Document& document() { return document_; }
    PlanCanvas& canvas() { return *canvas_; }
    Selection& selection() { return *selection_; }
    QWidget* dialogParent() { return dialogParent_.data(); }
    DetectionSession& detection() { return *detection_; }

    const plynth::Floor* floor() const;
    std::optional<double> pixelsPerMetre() const;
    bool hasImage() const;
    // The snap radius in metres at the current zoom.
    double snapRadiusMetres() const;
    bool snapToGrid() const { return snapToGrid_; }
    void setSnapToGrid(bool on);
    double newWallThickness() const { return newWallThickness_; }
    void setNewWallThickness(double metres);
    // Grid step in image pixels for the canvas, or 0 when no grid should show.
    double gridStepPixels() const;

    // Runs a command on the document. A refusal is shown as a message and returned.
    plynth::Status run(std::unique_ptr<plynth::Command> command);
    void showMessage(const QString& text);
    // Shows plain news in the status bar, such as what an accept added.
    void showNotice(const QString& text);
    void requestTool(ToolId id) { setActive(id); }
    // Lets a tool say its hint changed.
    void hintChanged();

signals:
    void activeChanged(ToolId id);
    void availabilityChanged();
    void hintTextChanged(const QString& text);
    void message(const QString& text);
    void notice(const QString& text);
    void gridChanged();
    void newWallThicknessChanged(double metres);

private:
    Tool* tool(ToolId id) const;

    plynth::Document& document_;
    QPointer<PlanCanvas> canvas_;
    QPointer<Selection> selection_;
    QPointer<QWidget> dialogParent_;
    std::array<std::unique_ptr<Tool>, 5> tools_;
    DetectionSession* detection_ = nullptr;
    ToolId active_ = ToolId::Select;
    bool snapToGrid_ = true;
    double newWallThickness_ = 0.2;
};

}
