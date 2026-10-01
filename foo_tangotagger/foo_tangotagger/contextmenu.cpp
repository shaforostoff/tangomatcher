#include "stdafx.h"

#include "guid.h"
#include "lyrics_rows.h"
#include "tangotagger_ui.h"
#include "version.h"

namespace
{
	contextmenu_group_popup_factory g_group(guid_tangotagger_context_group, contextmenu_groups::root,
	                                        FOO_TANGOTAGGER_NAME, 0);

	class tangotagger_contextmenu : public contextmenu_item_simple
	{
	public:
		GUID get_parent() override { return guid_tangotagger_context_group; }
		enum { item_lyrics, item_discographies, item_count };

		unsigned get_num_items() override { return item_count; }

		void get_item_name(unsigned p_index, pfc::string_base & p_out) override
		{
			p_out = p_index == item_lyrics ? "Find lyrics..." : "Match discographies...";
		}

		GUID get_item_guid(unsigned p_index) override
		{
			return p_index == item_lyrics ? guid_tangotagger_find_lyrics : guid_tangotagger_match_discographies;
		}

		bool get_item_description(unsigned p_index, pfc::string_base & p_out) override
		{
			if (p_index == item_lyrics)
				p_out = "Match the selected tracks' titles against the tango lyrics built into the component "
				        "and choose which to write into the files.";
			else
				p_out = "Find the selected tracks' recordings in the orchestra discographies built into the "
				        "component and fix their title, artist, date and genre tags.";
			return true;
		}

		void context_command(unsigned p_index, metadb_handle_list_cref p_data, const GUID & p_caller) override
		{
			if (p_index == item_discographies)
			{
				disco_matches matches = find_disco_matches(p_data);
				if (matches.tracks_matched == 0)
				{
					pfc::string_formatter msg;
					if (matches.tracks_examined == 1)
						msg << "The selected track was not found in the discographies.";
					else
						msg << "None of the " << matches.tracks_examined
						    << " selected tracks was found in the discographies.";
					msg << "\n\nA track is matched by its title, and its orchestra has to be named somewhere "
					       "on it: artist, album artist, conductor, the file name or its folder.";
					popup_message::g_show(msg, FOO_TANGOTAGGER_NAME);
					return;
				}
				show_disco_matches(std::move(matches));
				return;
			}

			lyrics_matches matches = find_lyrics_matches(p_data);
			if (matches.tracks_matched == 0)
			{
				pfc::string_formatter msg;
				if (matches.tracks_examined == 1)
					msg << "No lyrics match the title of the selected track.";
				else
					msg << "No lyrics match the titles of the " << matches.tracks_examined
					    << " selected tracks.";
				popup_message::g_show(msg, FOO_TANGOTAGGER_NAME);
				return;
			}
			show_lyrics_matches(std::move(matches));
		}
	};

	contextmenu_item_factory_t<tangotagger_contextmenu> g_contextmenu;
}
