#include "stdafx.h"

#include "file_info_filter_fields.h"

file_info_filter_fields::file_info_filter_fields(const std::vector<item> & items)
{
	metadb_handle_list tracks;
	for (const item & i : items) tracks.add_item(i.track);

	pfc::array_t<t_size> order;
	order.set_size(tracks.get_count());
	order_helper::g_fill(order.get_ptr(), order.get_size());
	tracks.sort_get_permutation_t(pfc::compare_t<metadb_handle_ptr, metadb_handle_ptr>, order.get_ptr());

	m_tracks.set_count(order.get_size());
	m_items.resize(order.get_size());
	for (t_size n = 0; n < order.get_size(); n++)
	{
		m_tracks[n] = tracks[order[n]];
		m_items[n] = items[order[n]];
	}
}

bool file_info_filter_fields::apply_filter(metadb_handle_ptr p_track, t_filestats p_stats, file_info & p_info)
{
	t_size index;
	if (!m_tracks.bsearch_t(pfc::compare_t<metadb_handle_ptr, metadb_handle_ptr>, p_track, index))
		return false;

	bool changed = false;
	for (const tangotagger::tag_value & f : m_items[index].fields)
	{
		// Already exactly this: leave the field - and, if nothing else
		// changes, the file - untouched.
		const t_size count = p_info.meta_get_count_by_name(f.field.c_str());
		bool same = count == f.values.size();
		for (t_size i = 0; same && i < count; i++)
			same = f.values[i] == p_info.meta_get(f.field.c_str(), i);
		if (same) continue;

		p_info.meta_remove_field(f.field.c_str());
		for (const std::string & v : f.values) p_info.meta_add(f.field.c_str(), v.c_str());
		changed = true;
	}
	return changed;
}
