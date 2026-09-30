#pragma once

#include <SDK/foobar2000.h>

#include <vector>

//! Writes one lyrics text into one field per track. Every other field is left
//! as it was.
class file_info_filter_lyrics : public file_info_filter
{
public:
	struct item
	{
		metadb_handle_ptr track;
		pfc::string8 field;
		pfc::string8 text;
	};

	explicit file_info_filter_lyrics(const std::vector<item> & items);

	bool apply_filter(metadb_handle_ptr p_track, t_filestats p_stats, file_info & p_info) override;

private:
	//! Sorted by track, so apply_filter can bsearch.
	metadb_handle_list m_tracks;
	std::vector<item> m_items;
};
