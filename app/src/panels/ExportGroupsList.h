#pragma once

#include <QWidget>

namespace plynth::app {

// The list of export groups shown in the Export tab, with their members and edit actions.
class ExportGroupsList : public QWidget {
    Q_OBJECT
public:
    explicit ExportGroupsList(QWidget* parent = nullptr);
};

} // namespace plynth::app
