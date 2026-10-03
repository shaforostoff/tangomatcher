#ifndef TT_UI_H
#define TT_UI_H

// The plugin's windows, as the rest of it sees them.
//
// One implementation per toolkit, chosen when the plugin is built, as in
// deadbeef_rubato:
//
//   ui_none.cpp       no windows, in a build with neither TT_DDB_GTK nor
//                     TT_DDB_COCOA. The plugin writes what it is sure of and
//                     reports the rest in the log.
//   gtk/ui_gtk.cpp    GTK 3, for DeaDBeeF's GTK 3 interface on Linux and
//                     Windows; also the lyrics panel, a design mode widget.
//   cocoa/ui_cocoa.mm Cocoa, for DeaDBeeF for Mac.
//
// Each implements the same functions against tt_review.h and nothing else.
// Every function may be called from any thread - matching finishes on a
// worker thread - and an implementation hands the work to its window
// system's own thread. None of them blocks.

#include <memory>
#include <string>

#ifndef DDB_API_LEVEL
#define DDB_API_LEVEL 10   // DeaDBeeF 1.8.0 and later
#endif
#include <deadbeef/deadbeef.h>

#include "tt_review.h"

namespace tt
{
namespace ui
{

//! Called from the plugin's connect(), once every plugin has started: the
//! point at which the implementation can see whether the interface it draws
//! into is the one DeaDBeeF is running.
void connect(DB_functions_t * api);

//! Called from the plugin's disconnect(), before any plugin stops: takes
//! back what connect() registered with the interface.
void disconnect();

//! Called from the plugin's stop(). Closes whatever is open and gives back
//! every track the windows were holding.
void shutdown();

//! Whether there is anywhere to put a window. False in a build without one,
//! and under an interface the windows do not belong to.
bool available();

//! A progress bar with a Cancel button, for listening that has just started.
//! Shown only if it is still going after a moment, and closed by itself when
//! it finishes.
void show_progress(std::shared_ptr<scan_progress> progress);

//! The results window.
void show_review(std::shared_ptr<review> r);

//! A message with an OK button: nothing matched.
void show_message(const std::string & title, const std::string & text);

}   // namespace ui
}   // namespace tt

#endif // TT_UI_H
