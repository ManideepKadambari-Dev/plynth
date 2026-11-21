#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace plynth::terminal {

struct ConPtyOptions {
    std::wstring commandLine;
    std::wstring workingDir;
    // Extra variables added on top of the current environment. Names match without regard to case.
    std::vector<std::pair<std::wstring, std::wstring>> environment;
    short columns = 80;
    short rows = 24;
};

// A program running inside a Windows pseudo console, kept in a job object that kills it on close.
class ConPty {
public:
    ConPty();
    ~ConPty();
    ConPty(const ConPty&) = delete;
    ConPty& operator=(const ConPty&) = delete;

    bool start(const ConPtyOptions& options, std::wstring* error = nullptr);
    bool isOpen() const { return open_.load(); }

    // Blocks until output arrives. Returns the byte count, or 0 when the console is closed.
    std::size_t read(char* buffer, std::size_t size);
    bool write(const char* data, std::size_t size);
    bool resize(short columns, short rows);

    // Number of live processes in the job, the shell included. Zero when closed.
    int processCount() const;
    bool hasExited() const;
    unsigned long exitCode() const;

    // Kills the job and closes the console while another thread keeps draining read(), then wakes read().
    // Call it before joining the reading thread. The destructor frees the remaining handles.
    void close();

private:
    struct Impl;
    Impl* impl_;
    std::atomic<bool> open_{false};
};

}  // namespace plynth::terminal
