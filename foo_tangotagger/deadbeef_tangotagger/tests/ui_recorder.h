#ifndef TT_UI_RECORDER_H
#define TT_UI_RECORDER_H

// What the plugin asked the window system to show, for the host test: a
// tt_ui.h implementation that keeps everything instead of drawing it.

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "tt_ui.h"

namespace recorder
{
	//! What ui::available() answers.
	extern bool available;
	extern std::mutex lock;
	extern std::vector<std::shared_ptr<tt::scan_progress>> progress;
	//! Every results window asked for, in order. Clear it to give the tracks
	//! back.
	extern std::vector<std::shared_ptr<tt::review>> reviews;
	extern std::vector<std::string> messages;
}

#endif // TT_UI_RECORDER_H
