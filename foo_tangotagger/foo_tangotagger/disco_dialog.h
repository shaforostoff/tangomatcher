#pragma once

#include <SDK/foobar2000.h>
#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>
#include <libPPUI/CListControlOwnerData.h>

#include "grouped_list.h"

#include "disco_rows.h"
#include "resource.h"

//! The Match discographies window on Windows: the candidate recordings in a
//! checkbox list, the artist scheme and the fields to write below it, and
//! what writing would change on the selected row's file. Modeless; deletes
//! itself when closed. Built like lyrics_dialog.
class disco_dialog : public CDialogImpl<disco_dialog>, private message_filter_impl_base,
                     private IListControlOwnerDataSource, private IListControlOwnerDataCells
{
public:
	enum { IDD = IDD_DISCO_DIALOG };

	BEGIN_MSG_MAP_EX(disco_dialog)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnOK)
		COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnCancel)
		COMMAND_HANDLER_EX(IDC_CHECK_ALL, BN_CLICKED, OnCheckAll)
		COMMAND_HANDLER_EX(IDC_CHECK_NONE, BN_CLICKED, OnCheckNone)
		COMMAND_HANDLER_EX(IDC_DISCO_SCHEME, CBN_SELCHANGE, OnOptions)
		COMMAND_HANDLER_EX(IDC_FIELD_TITLE, BN_CLICKED, OnOptions)
		COMMAND_HANDLER_EX(IDC_FIELD_ARTIST, BN_CLICKED, OnOptions)
		COMMAND_HANDLER_EX(IDC_FIELD_ALBUM_ARTIST, BN_CLICKED, OnOptions)
		COMMAND_HANDLER_EX(IDC_FIELD_DATE, BN_CLICKED, OnOptions)
		COMMAND_HANDLER_EX(IDC_FIELD_GENRE, BN_CLICKED, OnOptions)
		MSG_WM_CLOSE(OnClose)
	END_MSG_MAP()

	explicit disco_dialog(disco_matches && matches) : m_matches(std::move(matches)),
		m_list(this, this, [this](size_t row) { return row < m_matches.rows.size() ? m_matches.rows[row].group : row; }) {}

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnOK(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnCheckAll(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnCheckNone(UINT uNotifyCode, int nID, CWindow wndCtl);
	//! The scheme or a field checkbox changed: kept, and the preview follows.
	void OnOptions(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnClose();
	void OnFinalMessage(HWND) override;

	bool pretranslate_message(MSG * p_msg) override;

	// IListControlOwnerDataSource
	size_t listGetItemCount(ctx_t) override { return m_matches.rows.size(); }
	pfc::string8 listGetSubItemText(ctx_t, size_t item, size_t subItem) override;
	bool listIsSubItemGrayed(ctx_t, size_t item, size_t) override { return !m_matches.rows[item].matched(); }
	void listSelChanged(ctx_t) override;
	void listItemAction(ctx_t, size_t item) override;

	// IListControlOwnerDataCells
	CListControl::cellType_t listCellType(cellsCtx_t, size_t item, size_t subItem) override;
	bool listCellCheckState(cellsCtx_t, size_t item, size_t subItem) override;
	void listCellSetCheckState(cellsCtx_t, size_t item, size_t subItem, bool state) override;

	void ChecksChanged();
	void ShowPreview(size_t row);
	void LabelWriteButton();

	disco_matches m_matches;
	grouped_list m_list;   //!< shaded by track
	fb2k::CDarkModeHooks m_dark;
};
