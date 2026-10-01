#include "stdafx.h"

#include "disco_dialog.h"
#include "tangotagger_ui.h"

#include <libPPUI/CListControl-Cells.h>

namespace
{
	enum column { col_check, col_title, col_artist, col_recording, col_orchestra, col_vocal, col_date, col_match, col_count };

	const struct { int id; bool tangotagger::tag_options::*field; } field_boxes[] = {
		{ IDC_FIELD_TITLE, &tangotagger::tag_options::title },
		{ IDC_FIELD_ARTIST, &tangotagger::tag_options::artist },
		{ IDC_FIELD_ALBUM_ARTIST, &tangotagger::tag_options::album_artist },
		{ IDC_FIELD_DATE, &tangotagger::tag_options::date },
		{ IDC_FIELD_GENRE, &tangotagger::tag_options::genre },
	};
}

BOOL disco_dialog::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	m_list.CreateInDialog(*this, IDC_DISCO_LIST);
	m_dark.AddDialogWithControls(*this);

	m_list.SetSelectionModeSingle();

	const CSize dpi = m_list.GetDPI();
	// The checkbox, and beside it the option number when a track has
	// several recordings to choose from.
	m_list.AddColumn("", m_list.item_height() + m_list.GetOptimalColumnWidthFixed(" 8"));
	m_list.AddColumnAutoWidth("Track title");
	m_list.AddColumnAutoWidth("Artist");
	m_list.AddColumnAutoWidth("Recording");
	m_list.AddColumnAutoWidth("Orchestra");
	m_list.AddColumnAutoWidth("Singer");
	m_list.AddColumn("Date", m_list.GetOptimalColumnWidthFixed("1940-12-31"));
	m_list.AddColumn("Match", (std::max)(m_list.GetOptimalColumnWidthFixed("confident"),
	                                     m_list.GetOptimalColumnWidthFixed("no match")));

	const tangotagger::tag_options options = disco_tag_options();
	CComboBox scheme(GetDlgItem(IDC_DISCO_SCHEME));
	for (int s = 0; s < static_cast<int>(tangotagger::artist_scheme::count); s++)
	{
		const auto a = static_cast<tangotagger::artist_scheme>(s);
		pfc::string_formatter label;
		label << tangotagger::artist_scheme_name(a) << "  -  " << tangotagger::artist_scheme_example(a).c_str();
		scheme.AddString(pfc::stringcvt::string_wide_from_utf8(label));
	}
	scheme.SetCurSel(static_cast<int>(options.scheme));
	for (const auto & b : field_boxes)
		CheckDlgButton(b.id, options.*(b.field) ? BST_CHECKED : BST_UNCHECKED);

	uSetDlgItemText(m_hWnd, IDC_DISCO_STATUS, disco_status_text(m_matches));
	LabelWriteButton();

	if (!m_matches.rows.empty())
	{
		m_list.SelectSingle(0);
		ShowPreview(0);
	}
	m_list.SetFocus();
	return FALSE;
}

void disco_dialog::OnOK(UINT, int, CWindow)
{
	write_checked_disco_tags(m_matches.rows);
	DestroyWindow();
}

void disco_dialog::OnCancel(UINT, int, CWindow)
{
	DestroyWindow();
}

void disco_dialog::OnClose()
{
	DestroyWindow();
}

void disco_dialog::OnFinalMessage(HWND)
{
	delete this;
}

void disco_dialog::OnCheckAll(UINT, int, CWindow)
{
	check_all_disco_rows(m_matches.rows);
	ChecksChanged();
}

void disco_dialog::OnCheckNone(UINT, int, CWindow)
{
	for (disco_row & r : m_matches.rows) r.checked = false;
	ChecksChanged();
}

void disco_dialog::OnOptions(UINT, int, CWindow)
{
	tangotagger::tag_options options = disco_tag_options();
	const int scheme = CComboBox(GetDlgItem(IDC_DISCO_SCHEME)).GetCurSel();
	if (scheme >= 0) options.scheme = static_cast<tangotagger::artist_scheme>(scheme);
	for (const auto & b : field_boxes)
		options.*(b.field) = IsDlgButtonChecked(b.id) == BST_CHECKED;
	set_disco_tag_options(options);
	const size_t sel = m_list.GetSingleSel();
	if (sel != SIZE_MAX) ShowPreview(sel);
}

pfc::string8 disco_dialog::listGetSubItemText(ctx_t, size_t item, size_t subItem)
{
	const disco_row & r = m_matches.rows[item];
	switch (subItem)
	{
	case col_check:
		if (r.versions < 2) return "";
		return pfc::string8(" ") + pfc::format_int(r.version).c_str();
	case col_title:
		// A track's further candidates are indented under its first.
		if (r.version > 1) return "";
		return r.title;
	case col_artist:
		return r.version > 1 ? pfc::string8() : r.artist;
	case col_recording:
		return r.matched() ? pfc::string8(tangotagger::main_title(row_recording(r).name).c_str()) : pfc::string8();
	case col_orchestra:
		return r.matched() ? pfc::string8(row_orchestra(r).c_str()) : pfc::string8();
	case col_vocal:
		return r.matched() ? pfc::string8(row_recording(r).vocal.c_str()) : pfc::string8();
	case col_date:
		return r.matched() ? pfc::string8(row_recording(r).date.c_str()) : pfc::string8();
	case col_match:
		return disco_match_label(r);
	default:
		return "";
	}
}

CListControl::cellType_t disco_dialog::listCellType(cellsCtx_t, size_t item, size_t subItem)
{
	if (subItem == col_check && m_matches.rows[item].matched())
		return &PFC_SINGLETON(CListCell_Checkbox);
	return &PFC_SINGLETON(CListCell_Text);
}

bool disco_dialog::listCellCheckState(cellsCtx_t, size_t item, size_t subItem)
{
	return subItem == col_check && m_matches.rows[item].checked;
}

void disco_dialog::listCellSetCheckState(cellsCtx_t, size_t item, size_t subItem, bool state)
{
	if (subItem != col_check) return;
	set_disco_row_checked(m_matches.rows, item, state);
	ChecksChanged();
}

void disco_dialog::listItemAction(ctx_t, size_t item)
{
	if (item >= m_matches.rows.size() || !m_matches.rows[item].matched()) return;
	set_disco_row_checked(m_matches.rows, item, !m_matches.rows[item].checked);
	ChecksChanged();
}

void disco_dialog::listSelChanged(ctx_t)
{
	const size_t sel = m_list.GetSingleSel();
	if (sel != SIZE_MAX) ShowPreview(sel);
}

void disco_dialog::ChecksChanged()
{
	m_list.Invalidate();
	LabelWriteButton();
}

void disco_dialog::ShowPreview(size_t row)
{
	pfc::string8 text;
	if (row < m_matches.rows.size())
		text = disco_preview_text(m_matches.rows[row], "\r\n");
	uSetDlgItemText(m_hWnd, IDC_DISCO_PREVIEW, text);
}

void disco_dialog::LabelWriteButton()
{
	const std::size_t n = count_checked(m_matches.rows);
	pfc::string_formatter label;
	if (n == 1) label << "&Write 1 file";
	else label << "&Write " << n << " files";
	uSetDlgItemText(m_hWnd, IDOK, label);
	GetDlgItem(IDOK).EnableWindow(n > 0);
}

bool disco_dialog::pretranslate_message(MSG * p_msg)
{
	return m_hWnd != NULL && IsDialogMessage(p_msg);
}

/***** tangotagger_ui.h *****/

void show_disco_matches(disco_matches && matches)
{
	disco_dialog * dialog = new disco_dialog(std::move(matches));
	if (dialog->Create(core_api::get_main_window()) == NULL)
	{
		delete dialog;
		return;
	}
	dialog->ShowWindow(SW_SHOWNORMAL);
}
