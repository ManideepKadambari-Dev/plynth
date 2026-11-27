#pragma once

#include <plynth/core/EngineLink.h>
#include <plynth/core/Settings.h>

#include <QObject>
#include <QString>

#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace plynth::app {

// Runs the slow parts of an engine link on worker threads: the search for open editors, a send by hand and the
// sync after a build. Results come back on the window's thread as signals. It never touches the document.
class LinkRunner : public QObject {
    Q_OBJECT
public:
    explicit LinkRunner(QObject* parent = nullptr);
    ~LinkRunner() override;

    bool searching() const { return searching_; }
    // A send or a sync runs.
    bool running() const { return running_; }

    // Looks for open editors of one link. False when a search already runs or there is no such link.
    bool search(const std::string& linkId);
    // Sends the building with one link. The worker picks the Blender from the settings. False when a run is going.
    bool send(plynth::EngineLinkRequest request, plynth::MachineSettings settings);
    // Updates every linked editor after a good build. False when a run is going.
    bool sync(plynth::EngineLinkRequest base, plynth::MachineSettings settings);
    // Asks the running send or sync to stop.
    void cancel();

signals:
    void searched(const plynth::EngineEditorSearch& search);
    // One plain sentence on where the send or sync is.
    void progress(const QString& message);
    void sent(const plynth::EngineLinkReport& report);
    void synced(const std::vector<plynth::EngineLinkReport>& reports);

private:
    std::jthread searchWorker_;
    std::jthread runWorker_;
    std::stop_source runStop_;
    bool searching_ = false;
    bool running_ = false;
};

}
