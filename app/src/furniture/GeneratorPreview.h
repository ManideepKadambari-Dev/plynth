#pragma once

#include <plynth/core/Addons.h>
#include <plynth/core/Generators.h>

#include <QWidget>

#include <optional>

class QLabel;
class QGridLayout;

namespace plynth::app {

// The panel that describes one generator: name, description, default size, parameters with their defaults
// and the add-ons it needs. It never shows a picture, because the result is only made when the project is
// built.
class GeneratorPreview : public QWidget {
    Q_OBJECT
public:
    explicit GeneratorPreview(QWidget* parent = nullptr);

    // Shows a generator. cache is the add-on check result for the Blender in use, empty when there is none.
    // The size and the default values are worked out for a floor with this ceiling height in metres.
    void showGenerator(const plynth::GeneratorInfo& info, const std::optional<plynth::AddonCache>& cache,
                       double ceilingHeight);
    // Shows the hint to hover a generator.
    void clear();

    // A size as "0.30 x 0.30 x 3.00 m".
    static QString sizeText(const plynth::ItemSize& size);

private:
    void addRow(const QString& left, const QString& right);
    void addHeading(const QString& text);

    QLabel* hint_ = nullptr;
    QWidget* body_ = nullptr;
    QLabel* name_ = nullptr;
    QLabel* description_ = nullptr;
    QLabel* size_ = nullptr;
    QWidget* rows_ = nullptr;
    QGridLayout* grid_ = nullptr;
};

}
