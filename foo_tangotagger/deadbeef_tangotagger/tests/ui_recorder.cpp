// A window system that records what it is asked to show. See ui_recorder.h.

#include "ui_recorder.h"

namespace recorder
{
	bool available = false;
	std::mutex lock;
	std::vector<std::shared_ptr<tt::scan_progress>> progress;
	std::vector<std::shared_ptr<tt::review>> reviews;
	std::vector<std::string> messages;
}

namespace tt
{
namespace ui
{

void connect(DB_functions_t *) {}
void disconnect() {}

void shutdown()
{
	std::lock_guard<std::mutex> guard(recorder::lock);
	recorder::reviews.clear();
	recorder::progress.clear();
}

bool available() { return recorder::available; }

void show_progress(std::shared_ptr<scan_progress> p)
{
	std::lock_guard<std::mutex> guard(recorder::lock);
	recorder::progress.push_back(std::move(p));
}

void show_review(std::shared_ptr<review> r)
{
	std::lock_guard<std::mutex> guard(recorder::lock);
	recorder::reviews.push_back(std::move(r));
}

void show_message(const std::string & title, const std::string & text)
{
	std::lock_guard<std::mutex> guard(recorder::lock);
	recorder::messages.push_back(title + ": " + text);
}

}   // namespace ui
}   // namespace tt
