// No windows: the build with neither TT_DDB_GTK nor TT_DDB_COCOA. See tt_ui.h.

#include "tt_ui.h"

namespace tt
{
namespace ui
{

void connect(DB_functions_t *) {}
void disconnect() {}
void shutdown() {}
bool available() { return false; }
void show_progress(std::shared_ptr<scan_progress>) {}
void show_review(std::shared_ptr<review>) {}
void show_message(const std::string &, const std::string &) {}

}   // namespace ui
}   // namespace tt
