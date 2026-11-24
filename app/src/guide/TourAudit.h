#pragma once

#include <QString>

namespace plynth::app {

class MainWindow;

// The exit code of an audit that found controls without a tour entry.
inline constexpr int kExitTourAuditFailed = 5;

// Shows every screen of the window in turn, lists the tour id of every visible control and menu entry, and
// compares the list with the tour text through core's checkTourEntries. Writes the result as JSON to outFile
// and returns 0 when every id has an entry, kExitTourAuditFailed when one has none, 1 when the file cannot be
// written. It starts no process, no build and no Blender, and leaves the project as it was.
int runTourAudit(MainWindow& window, const QString& outFile);

}
