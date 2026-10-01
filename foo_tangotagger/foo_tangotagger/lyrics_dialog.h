#pragma once

#include <SDK/foobar2000.h>
#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>
#include <libPPUI/CListControlOwnerData.h>

#include "lyrics_rows.h"
#include "resource.h"

//! The results window on Windows: the matches in a checkbox list, the
//! selected row's lyrics below it. Modeless; deletes itself when closed.
//!
//! The list is libPPUI's CListControl rather than a list view: it takes its
//! cell types row by row, so a track nothing matched is listed without a
//! checkbox, and it follows foobar2000's dark mode. (A list view would be
//! swapped for one anyway - the dark mode hooks do that - and the swapped-in
//! control puts a checkbox on every row.)
class lyrics_dialog : public CDialogImpl<lyrics_dialog>, private message_filter_impl_base,
                      private IListControlOwnerDataSource, private IListControlOwnerDataCells
{
public:
	enum { IDD = IDD_LYRICS_DIALOG };

	BEGIN_MSG_MAP_EX(lyrics_dialog)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnOK)
		COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnCancel)
		COMMAND_HANDLER_EX(IDC_CHECK_ALL, BN_CLICKED, OnCheckAll)
		COMMAND_HANDLER_EX(IDC_CHECK_NONE, BN_CLICKED, OnCheckNone)
		COMMAND_HANDLER_EX(IDC_TRANSLATION_LINKS, BN_CLICKED, OnTranslationLinks)
		MSG_WM_CLOSE(OnClose)
	END_MSG_MAP()

	explicit lyrics_dialog(lyrics_matches && matches) : m_matches(std::move(matches)), m_list(this, this) {}

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnOK(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnCheckAll(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnCheckNone(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnTranslationLinks(UINT uNotifyCode, int nID, CWindow wndCtl);
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

	//! Repaints the checkboxes - checking one row can uncheck others of its
	//! group - and relabels the button.
	void ChecksChanged();
	void ShowPreview(size_t row);
	//! "Write 12 files", disabled at none.
	void LabelWriteButton();

	lyrics_matches m_matches;
	CListControlOwnerDataCells m_list;
	fb2k::CDarkModeHooks m_dark;
};
