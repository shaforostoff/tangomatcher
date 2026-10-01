#pragma once

#include <SDK/foobar2000.h>

#include <vector>

#include "disco_tags.h"

//! Sets whole fields on each of a set of tracks - every value of the field
//! replaced - leaving the rest of the tags alone.
class file_info_filter_fields : public file_info_filter
{
public:
	struct item
	{
		metadb_handle_ptr track;
		std::vector<tangotagger::tag_value> fields;
	};

	explicit file_info_filter_fields(const std::vector<item> & items);

	bool apply_filter(metadb_handle_ptr p_track, t_filestats p_stats, file_info & p_info) override;

private:
	metadb_handle_list m_tracks;   //!< sorted, for bsearch
	std::vector<item> m_items;     //!< in m_tracks' order
};
