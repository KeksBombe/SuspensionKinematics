#pragma once

class QDockWidget;

namespace suspkin {

/// Makes letting go of a floating @p dock's native title bar drop it where it
/// is, instead of at the next pointer move over that title bar.
///
/// A floating dock has a native frame wherever the window manager draws one --
/// Windows above all -- and a drag that starts on it is ended by QDockWidget
/// only when a later non-client move reaches the dock: on Windows the move loop
/// swallows the button-up. Qt synthesises the release when that loop ends, and
/// QDockWidget ignores it everywhere but macOS. Until another move arrives, the
/// panel floats over the gap opened for it; if the pointer leaves the title bar
/// first, or the window is moved before it does, it never docks at all. This
/// ends the drag on that release, the way Qt already does on macOS.
void endNativeDockDragOnRelease(QDockWidget* dock);

} // namespace suspkin
