#pragma once

#include <QWidget>

class QVBoxLayout;

namespace plynth::app {

class ImportQueue;

// The progress list under the asset grid: one row per queued import with its stage, a bar and Cancel,
// and failed rows with the reason, Show log and Dismiss. Hidden while the queue is empty.
class ImportQueueView : public QWidget {
    Q_OBJECT
public:
    explicit ImportQueueView(ImportQueue& queue, QWidget* parent = nullptr);

private:
    void rebuild();

    ImportQueue& queue_;
    QVBoxLayout* rows_ = nullptr;
};

}
