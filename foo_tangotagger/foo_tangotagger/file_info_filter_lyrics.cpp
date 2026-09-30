#include "stdafx.h"

#include "file_info_filter_lyrics.h"

file_info_filter_lyrics::file_info_filter_lyrics(const std::vector<item> & items)
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

bool file_info_filter_lyrics::apply_filter(metadb_handle_ptr p_track, t_filestats p_stats, file_info & p_info)
{
	t_size index;
	if (!m_tracks.bsearch_t(pfc::compare_t<metadb_handle_ptr, metadb_handle_ptr>, p_track, index))
		return false;

	const item & i = m_items[index];
	const char * existing = p_info.meta_get(i.field, 0);
	if (existing != nullptr && strcmp(existing, i.text) == 0 && p_info.meta_get_count_by_name(i.field) == 1)
		return false;   // already there; leave the file untouched

	p_info.meta_set(i.field, i.text);
	return true;
}
