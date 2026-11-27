#pragma once

#include <memory>
#include <string>

namespace plynth::app {

// For screenshots only: a stand in for an open Unreal Editor with remote execution on. It answers the Unreal
// Engine link's search with one project, on this computer only and on its own multicast group, so the found
// state can be shown without Unreal. It runs no commands.
class CheckEditor {
public:
    CheckEditor(const std::string& group, int port, std::string projectName);
    ~CheckEditor();

    CheckEditor(const CheckEditor&) = delete;
    CheckEditor& operator=(const CheckEditor&) = delete;

    // True when its socket is open and it answers.
    bool ready() const;

private:
    struct State;
    std::unique_ptr<State> d_;
};

}
