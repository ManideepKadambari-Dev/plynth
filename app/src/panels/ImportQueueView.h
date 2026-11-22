#pragma once

#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

class QVBoxLayout;

namespace plynth::app {

class ImportQueue;
class MaterialImportQueue;

// The progress list under the asset grid: one row per queued import with its stage, a bar and Cancel,
// and failed rows with the reason, Show log and Dismiss. Hidden while the queue is empty.
class ImportQueueView : public QWidget {
    Q_OBJECT
public:
    // One row as the view draws it, whichever queue it came from.
    struct Row {
        int id = 0;
        QString title;
        QString stage;
        int percent = 0;
        bool failed = false;
        bool plainTitle = false; // a render, shown in the normal font instead of mono
        QString message;
        QString workFolder;
    };

    explicit ImportQueueView(ImportQueue& queue, QWidget* parent = nullptr);
    explicit ImportQueueView(MaterialImportQueue& queue, QWidget* parent = nullptr);

private:
    void start();
    void rebuild();

    std::function<std::vector<Row>()> rows_fn_;
    std::function<void(int)> cancel_;
    QVBoxLayout* rows_ = nullptr;
};

}
