#pragma once

#include <plynth/core/Pack.h>

#include <QDialog>

#include <filesystem>
#include <vector>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace plynth::app {

// Shows what a pack holds, or what unpacking it would do: every item with its kind, id, name and what happens to
// it (copied, only named and why, missing, added, already there, a conflict, to download with its command).
// After a pack it offers Open folder. Before an unpack it offers Unpack, which keeps the library's items when
// there is a conflict, and Cancel. The dialog only shows; the window runs the pack and the unpack through core.
class PackDialog : public QDialog {
    Q_OBJECT
public:
    // The result of a pack that was written.
    PackDialog(const plynth::PackReport& report, QWidget* parent = nullptr);
    // What an unpack would do. Accepting the dialog means "unpack".
    PackDialog(const plynth::UnpackPlan& plan, QWidget* parent = nullptr);

    // True when the unpack plan has a conflict, so accepting it keeps the library's items.
    bool keepsLibrary() const { return conflicts_ > 0; }

private:
    void build(const QString& title, const QString& tourId);
    void addRow(const plynth::PackEntry& entry, const QString& what, const QString& detail, bool warn);

    QLabel* summary_ = nullptr;
    QTreeWidget* list_ = nullptr;
    int conflicts_ = 0;
    std::filesystem::path folder_;
};

}
