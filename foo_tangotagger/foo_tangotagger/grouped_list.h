#pragma once

#include <SDK/foobar2000.h>
#include <libPPUI/CListControlOwnerData.h>

#include <algorithm>
#include <functional>
#include <vector>

//! The owner-data list with its alternating row shading following groups of
//! rows rather than single rows: every row of one track has the same shade,
//! the next track's rows the other, so a track's candidates read as one
//! block.
//!
//! libPPUI picks the shade from the parity of the index it passes to
//! CListControlImpl::RenderItemBackground; this passes the row's group
//! instead. With the default, delimited style the header implementation
//! paints cell by cell, looking the spans up by row, so that loop is redone
//! here with the group handed to the painter and the row to the spans.
class grouped_list : public CListControlOwnerDataCells
{
public:
	grouped_list(IListControlOwnerDataSource * source, IListControlOwnerDataCells * cells,
	             std::function<size_t(size_t)> group_of)
		: CListControlOwnerDataCells(source, cells), m_group_of(std::move(group_of)) {}

	//! The row height, which is also the width of a checkbox cell's box.
	int item_height() const { return GetItemHeight(); }

	void RenderItemBackground(CDCHandle dc, const CRect & rect, size_t item, uint32_t color) override
	{
		if (!m_group_of)
		{
			CListControlOwnerDataCells::RenderItemBackground(dc, rect, item, color);
			return;
		}
		const size_t shade = block_of(item);
		if (!DelimitColumns())
		{
			CListControlImpl::RenderItemBackground(dc, rect, shade, color);
			return;
		}
		const size_t count = GetColumnCount();
		const std::vector<int> order = GetColumnOrderArray();
		LONG x = rect.left;
		for (size_t walk = 0; walk < count;)
		{
			const size_t sub = static_cast<size_t>(order[walk]);
			const size_t span = (std::max)(GetSubItemSpan(item, sub), size_t(1));
			LONG width = 0;
			for (size_t s = 0; s < span; ++s) width += static_cast<LONG>(GetSubItemWidth(sub + s));
			CRect cell = rect;
			cell.left = x;
			x += width;
			cell.right = x;
			CRect visible;
			if (visible.IntersectRect(cell, rect))
			{
				const int saved = dc.SaveDC();
				if (dc.IntersectClipRect(cell) != NULLREGION)
					CListControlImpl::RenderItemBackground(dc, cell, shade, color);
				dc.RestoreDC(saved);
			}
			walk += span;
		}
	}

private:
	//! The row's block counted from the top - not its group, whose numbers
	//! need not run in list order: tracks nothing matched are listed last.
	size_t block_of(size_t item)
	{
		const size_t count = GetItemCount();
		if (m_blocks.size() != count)
		{
			m_blocks.resize(count);
			for (size_t i = 0; i < count; i++)
				m_blocks[i] = i == 0 ? 0 : m_blocks[i - 1] + (m_group_of(i) != m_group_of(i - 1) ? 1 : 0);
		}
		return item < count ? m_blocks[item] : item;
	}

	std::function<size_t(size_t)> m_group_of;
	std::vector<size_t> m_blocks;
};
