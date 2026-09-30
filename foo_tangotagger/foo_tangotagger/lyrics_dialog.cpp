#include "stdafx.h"

#include "lyrics_dialog.h"
#include "tangotagger_ui.h"

#include <libPPUI/CListControl-Cells.h>

namespace
{
	enum column { col_check, col_title, col_artist, col_song, col_match, col_existing, col_count };
}

BOOL lyrics_dialog::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	// Replaces the list view placeholder in the template; the dark mode hooks
	// come after, so that they see the list control and not the placeholder.
	m_list.CreateInDialog(*this, IDC_LYRICS_LIST);
	m_dark.AddDialogWithControls(*this);

	m_list.SetSelectionModeSingle();

	// The fixed columns sized to what they hold; the three text columns share
	// the rest.
	const CSize dpi = m_list.GetDPI();
	m_list.AddColumn("", MulDiv(24, dpi.cx, 96));
	m_list.AddColumnAutoWidth("Track title");
	m_list.AddColumnAutoWidth("Artist");
	m_list.AddColumnAutoWidth("Lyrics of");
	m_list.AddColumn("Match", (std::max)(m_list.GetOptimalColumnWidthFixed("similar 2/5"),
	                                     m_list.GetOptimalColumnWidthFixed("no match")));
	m_list.AddColumn("Existing lyrics", m_list.GetOptimalColumnWidthFixed("Existing lyrics"));

	uSetDlgItemText(m_hWnd, IDC_LYRICS_STATUS, lyrics_status_text(m_matches));
	LabelWriteButton();

	if (!m_matches.rows.empty())
	{
		m_list.SelectSingle(0);
		ShowPreview(0);
	}
	// The list, not the preview, whose text the dialog would otherwise
	// select in full.
	m_list.SetFocus();
	return FALSE;
}

void lyrics_dialog::OnOK(UINT, int, CWindow)
{
	write_checked_lyrics(m_matches.rows);
	DestroyWindow();
}

void lyrics_dialog::OnCancel(UINT, int, CWindow)
{
	DestroyWindow();
}

void lyrics_dialog::OnClose()
{
	DestroyWindow();
}

void lyrics_dialog::OnFinalMessage(HWND)
{
	delete this;
}

//! Every track gets a song: the one already checked in its group, or else
//! the group's first, which is the one its credits favour.
void lyrics_dialog::OnCheckAll(UINT, int, CWindow)
{
	std::vector<lyrics_row> & rows = m_matches.rows;
	for (std::size_t i = 0; i < rows.size(); i++)
	{
		if (rows[i].version != 1 || !rows[i].matched()) continue;
		bool any = false;
		for (std::size_t j = i; j < rows.size() && rows[j].group == rows[i].group; j++)
			any = any || rows[j].checked;
		if (!any) set_row_checked(rows, i, true);
	}
	ChecksChanged();
}

void lyrics_dialog::OnCheckNone(UINT, int, CWindow)
{
	for (lyrics_row & r : m_matches.rows) r.checked = false;
	ChecksChanged();
}

pfc::string8 lyrics_dialog::listGetSubItemText(ctx_t, size_t item, size_t subItem)
{
	const lyrics_row & r = m_matches.rows[item];
	switch (subItem)
	{
	case col_title:
		// A title's further candidates are indented under its first, so the
		// group reads as one choice.
		if (r.version > 1) return pfc::string8("      ") + r.title;
		return r.title;
	case col_artist:
		return r.artist;
	case col_song:
		return r.matched() ? pfc::string8(row_song(r).name.c_str()) : pfc::string8();
	case col_match:
		return match_label(r);
	case col_existing:
		return r.matched() ? pfc::string8(existing_label(r.existing)) : pfc::string8();
	default:
		return "";
	}
}

CListControl::cellType_t lyrics_dialog::listCellType(cellsCtx_t, size_t item, size_t subItem)
{
	// A track nothing matched has nothing to write: no checkbox.
	if (subItem == col_check && m_matches.rows[item].matched())
		return &PFC_SINGLETON(CListCell_Checkbox);
	return &PFC_SINGLETON(CListCell_Text);
}

bool lyrics_dialog::listCellCheckState(cellsCtx_t, size_t item, size_t subItem)
{
	return subItem == col_check && m_matches.rows[item].checked;
}

void lyrics_dialog::listCellSetCheckState(cellsCtx_t, size_t item, size_t subItem, bool state)
{
	if (subItem != col_check) return;
	// Checking one song of a title unchecks the others.
	set_row_checked(m_matches.rows, item, state);
	ChecksChanged();
}

//! Double click or Enter on a row toggles it, like clicking its checkbox.
void lyrics_dialog::listItemAction(ctx_t, size_t item)
{
	if (item >= m_matches.rows.size() || !m_matches.rows[item].matched()) return;
	set_row_checked(m_matches.rows, item, !m_matches.rows[item].checked);
	ChecksChanged();
}

void lyrics_dialog::listSelChanged(ctx_t)
{
	const size_t sel = m_list.GetSingleSel();
	if (sel != SIZE_MAX) ShowPreview(sel);
}

void lyrics_dialog::ChecksChanged()
{
	// The list asks for check states as it paints, so a repaint is all it
	// takes to show the other rows of a group unchecked.
	m_list.Invalidate();
	LabelWriteButton();
}

void lyrics_dialog::ShowPreview(size_t row)
{
	pfc::string8 text;
	if (row < m_matches.rows.size())
		text = lyrics_preview_text(m_matches.rows[row], "\r\n");
	uSetDlgItemText(m_hWnd, IDC_LYRICS_PREVIEW, text);
}

void lyrics_dialog::LabelWriteButton()
{
	const std::size_t n = count_checked(m_matches.rows);
	pfc::string_formatter label;
	if (n == 1) label << "&Write 1 file";
	else label << "&Write " << n << " files";
	uSetDlgItemText(m_hWnd, IDOK, label);
	GetDlgItem(IDOK).EnableWindow(n > 0);
}

bool lyrics_dialog::pretranslate_message(MSG * p_msg)
{
	return m_hWnd != NULL && IsDialogMessage(p_msg);
}

/***** tangotagger_ui.h *****/

void show_lyrics_matches(lyrics_matches && matches)
{
	// Deletes itself in OnFinalMessage.
	lyrics_dialog * dialog = new lyrics_dialog(std::move(matches));
	if (dialog->Create(core_api::get_main_window()) == NULL)
	{
		delete dialog;
		return;
	}
	dialog->ShowWindow(SW_SHOWNORMAL);
}
