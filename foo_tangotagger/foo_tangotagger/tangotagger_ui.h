#pragma once

// The results windows, one per platform: lyrics_dialog.cpp and
// disco_dialog.cpp on Windows, mac/lyrics_window_mac.mm and
// mac/disco_window_mac.mm on macOS. Nothing outside those knows which.

#include "disco_rows.h"
#include "lyrics_rows.h"

//! Shows the matches, non-modally. Only called with at least one row.
void show_lyrics_matches(lyrics_matches && matches);

//! Shows the discography matches, non-modally. Only called with at least one
//! matched track.
void show_disco_matches(disco_matches && matches);
