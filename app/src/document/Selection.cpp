#include "Selection.h"

namespace plynth::app {

// Selects one item. Selecting nothing or an empty id clears the selection.
void Selection::set(SelectionKind kind, const QString& id)
{
    if (kind == SelectionKind::None || id.isEmpty()) {
        clear();
        return;
    }
    if (kind == kind_ && id == id_)
        return;
    kind_ = kind;
    id_ = id;
    emit changed();
}

// Drops the selection.
void Selection::clear()
{
    if (kind_ == SelectionKind::None)
        return;
    kind_ = SelectionKind::None;
    id_.clear();
    clickAt_.reset();
    emit changed();
}

}
