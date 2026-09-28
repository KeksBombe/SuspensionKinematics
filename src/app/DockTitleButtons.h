#pragma once

class QDockWidget;

namespace suspkin {

/// Draws @p dock's float and close buttons with the application's own icons, in
/// the palette's colours, over a panel that shows when the pointer is on one.
///
/// The platform style's title bar icons are its own greys: on Windows 11 in
/// dark mode they are dark grey on dark grey. Only the two buttons are
/// restyled -- the title bar itself, and with it dragging the dock out,
/// dragging it back and double-clicking it home, stays Qt's own.
void themeDockTitleButtons(QDockWidget* dock);

} // namespace suspkin
