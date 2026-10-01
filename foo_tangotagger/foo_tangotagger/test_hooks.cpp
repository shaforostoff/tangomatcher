// Test builds only (FOO_TANGOTAGGER_TEST_HOOKS=ON): opens a results window
// on start-up, so the windows can be checked in a portable foobar2000
// without driving its menus.
//
//     TANGOTAGGER_TEST_DISCO=<folder>    Match discographies on its files
//     TANGOTAGGER_TEST_LYRICS=<folder>   Find lyrics on its files

#include "stdafx.h"

#include "tangotagger_ui.h"

#include <cstdlib>
#include <filesystem>

namespace
{
	metadb_handle_list files_in(const char * folder)
	{
		metadb_handle_list out;
		std::error_code ec;
		for (const auto & e : std::filesystem::recursive_directory_iterator(std::filesystem::u8path(folder), ec))
		{
			if (!e.is_regular_file()) continue;
			pfc::string8 path;
			filesystem::g_get_canonical_path(e.path().u8string().c_str(), path);
			out.add_item(metadb::get()->handle_create(path, 0));
		}
		return out;
	}

	class test_hooks : public initquit
	{
	public:
		void on_init() override
		{
			const bool disco = std::getenv("TANGOTAGGER_TEST_DISCO") != nullptr;
			const char * folder = std::getenv(disco ? "TANGOTAGGER_TEST_DISCO" : "TANGOTAGGER_TEST_LYRICS");
			if (folder == nullptr) return;
			const metadb_handle_list tracks = files_in(folder);
			auto done = fb2k::makeCompletionNotify([tracks, disco](unsigned)
			{
				if (disco) show_disco_matches(find_disco_matches(tracks));
				else show_lyrics_matches(find_lyrics_matches(tracks));
			});
			metadb_io_v2::get()->load_info_async(tracks, metadb_io::load_info_default, core_api::get_main_window(),
			                                     metadb_io_v2::op_flag_delay_ui, done);
		}
	};

	FB2K_SERVICE_FACTORY(test_hooks);
}
