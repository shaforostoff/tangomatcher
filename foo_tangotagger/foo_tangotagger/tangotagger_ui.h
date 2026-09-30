#pragma once

// The results window, one per platform: lyrics_dialog.cpp on Windows,
// mac/lyrics_window_mac.mm on macOS. Nothing outside those two knows which.

#include "lyrics_rows.h"

//! Shows the matches, non-modally. Only called with at least one row.
void show_lyrics_matches(lyrics_matches && matches);
