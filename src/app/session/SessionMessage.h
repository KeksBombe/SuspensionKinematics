#pragma once

#include <QString>

namespace suspkin {

/// Something the session has to tell the user, worded for them: a title and the
/// text under it.
///
/// The session holds no widgets, so it cannot put a box in front of anybody.
/// What it can do is say what the box would say, the way MeshLoadResult::error
/// and HardpointLoadResult::error already do, and leave the showing to whoever
/// asked. A title comes with it because one call can fail more than one way --
/// a file missing is not a file unreadable -- and only the session knows which.
struct SessionMessage {
    QString title;
    QString text;

    bool isEmpty() const { return text.isEmpty(); }
};

} // namespace suspkin
