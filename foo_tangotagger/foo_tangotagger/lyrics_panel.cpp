#include "stdafx.h"

// The lyrics panel on Windows: a Default UI element that shows the lyrics of
// the selected track (lyrics_panel_content.h decides what that is).
//
// Drawn by hand rather than in an edit or rich edit control, so that it
// takes the Default UI's colours and font, dark mode included, and its links
// - found by tangotagger::find_links - look and act like links. The text size
// is the panel's own setting, kept in its element configuration.

#include <helpers/BumpableElem.h>
#include <libPPUI/win32_op.h>
#include <libPPUI/win32_utility.h>

#include <shellapi.h>

#include <cmath>

#include "guid.h"
#include "lyrics_panel_content.h"

namespace
{
	enum font_kind { font_heading, font_credits, font_text, font_note, font_count };

	font_kind font_for(lyrics_panel_style style)
	{
		switch (style)
		{
		case lyrics_panel_style::heading: return font_heading;
		case lyrics_panel_style::credits: return font_credits;
		case lyrics_panel_style::note:    return font_note;
		default:                          return font_text;
		}
	}

	const int min_font_size = 6;    // points
	const int max_font_size = 72;

	struct panel_settings
	{
		int font_size = 0;      //!< points; 0 follows the Default UI's font
		bool centered = true;
	};

	const t_uint32 settings_version = 1;

	ui_element_config::ptr make_config(const panel_settings & s)
	{
		ui_element_config_builder builder;
		builder << settings_version << static_cast<t_int32>(s.font_size) << static_cast<t_uint32>(s.centered ? 1 : 0);
		return builder.finish(guid_lyrics_panel);
	}

	panel_settings parse_config(ui_element_config::ptr cfg)
	{
		panel_settings s;
		try
		{
			ui_element_config_parser parser(cfg);
			t_uint32 version = 0;
			parser >> version;
			if (version < 1) return s;
			t_int32 size = 0;
			t_uint32 centered = 1;
			parser >> size >> centered;
			if (size == 0 || (size >= min_font_size && size <= max_font_size)) s.font_size = size;
			s.centered = centered != 0;
		}
		catch (const std::exception &)
		{
			// The empty default configuration, or one from a later version
			// that is shorter than expected: the defaults.
		}
		return s;
	}

	COLORREF blend(COLORREF a, COLORREF b, int percent_a)
	{
		auto mix = [percent_a](int x, int y) { return (x * percent_a + y * (100 - percent_a)) / 100; };
		return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
	}

	bool is_dark(COLORREF c)
	{
		return GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114 < 128 * 1000;
	}

	std::wstring wide(const std::string & utf8)
	{
		return std::wstring(pfc::stringcvt::string_wide_from_utf8(utf8.c_str(), utf8.size()).get_ptr());
	}

	enum menu_id
	{
		id_open_link = 1, id_copy_link, id_copy, id_larger, id_smaller, id_default_size, id_centered
	};

	class lyrics_panel : public ui_element_instance, public CWindowImpl<lyrics_panel>,
	                     private ui_selection_callback_impl_base, private metadb_io_callback_dynamic_impl_base
	{
	public:
		DECLARE_WND_CLASS_EX(L"{84ACA1A2-73A8-4D84-9011-61C730D4A808}", CS_DBLCLKS, -1);

		lyrics_panel(ui_element_config::ptr cfg, ui_element_instance_callback_ptr callback)
			: ui_selection_callback_impl_base(false), m_settings(parse_config(cfg)), m_callback(callback) {}

		void initialize_window(HWND parent)
		{
			WIN32_OP(Create(parent, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL) != NULL);
		}

		BEGIN_MSG_MAP_EX(lyrics_panel)
			MSG_WM_CREATE(OnCreate)
			MSG_WM_DESTROY(OnDestroy)
			MSG_WM_SIZE(OnSize)
			MSG_WM_ERASEBKGND(OnEraseBkgnd)
			MSG_WM_PAINT(OnPaint)
			MSG_WM_VSCROLL(OnVScroll)
			MSG_WM_MOUSEWHEEL(OnMouseWheel)
			MSG_WM_MOUSEMOVE(OnMouseMove)
			MSG_WM_MOUSELEAVE(OnMouseLeave)
			MSG_WM_SETCURSOR(OnSetCursor)
			MSG_WM_LBUTTONDOWN(OnLButtonDown)
			MSG_WM_LBUTTONUP(OnLButtonUp)
			MSG_WM_CONTEXTMENU(OnContextMenu)
			MSG_WM_KEYDOWN(OnKeyDown)
			MSG_WM_GETDLGCODE(OnGetDlgCode)
		END_MSG_MAP()

		// ui_element_instance
		HWND get_wnd() override { return *this; }
		void set_configuration(ui_element_config::ptr config) override
		{
			m_settings = parse_config(config);
			if (m_hWnd != NULL) Restyle();
		}
		ui_element_config::ptr get_configuration() override { return make_config(m_settings); }
		void notify(const GUID & what, t_size, const void *, t_size) override
		{
			if (what == ui_element_notify_colors_changed || what == ui_element_notify_font_changed) Restyle();
		}

		static GUID g_get_guid() { return guid_lyrics_panel; }
		// Beside Properties and the other views of the selection. (foobar2000
		// only adds View menu commands for utility, visualisation and library
		// elements, so the panel lives in the layout.)
		static GUID g_get_subclass() { return ui_element_subclass_selection_information; }
		static void g_get_name(pfc::string_base & out) { out = "Lyrics"; }
		static ui_element_config::ptr g_get_default_configuration() { return make_config(panel_settings()); }
		static const char * g_get_description()
		{
			return "Lyrics of the selected track: the file's own, or else the built-in tango lyrics its title "
			       "matches. Ctrl+mouse wheel changes the text size; links open in the web browser.";
		}

	private:
		struct piece
		{
			std::wstring text;
			int x = 0;
			int width = 0;
			font_kind font = font_text;
			int link = -1;          //!< index into m_urls, or -1
		};
		struct line
		{
			int y = 0;
			int height = 0;
			std::vector<piece> pieces;
		};

		// --- window ------------------------------------------------------------

		int OnCreate(LPCREATESTRUCT)
		{
			// Dark scroll bar in dark mode.
			m_dark.AddGeneric(*this);
			CreateFonts();
			ui_selection_callback_activate(true);
			metadb_handle_list selection;
			ui_selection_manager::get()->get_selection(selection);
			ShowTrack(selection.get_count() > 0 ? selection[0] : metadb_handle_ptr());
			return 0;
		}

		void OnDestroy()
		{
			ui_selection_callback_activate(false);
			SetMsgHandled(FALSE);
		}

		void OnSize(UINT, CSize)
		{
			Layout();
			Invalidate();
		}

		BOOL OnEraseBkgnd(CDCHandle) { return TRUE; }

		void OnPaint(CDCHandle)
		{
			CPaintDC paint(*this);
			CMemoryDC dc(paint, paint.m_ps.rcPaint);
			CRect client;
			GetClientRect(&client);

			const COLORREF background = m_callback->query_std_color(ui_color_background);
			const COLORREF text = m_callback->query_std_color(ui_color_text);
			const COLORREF dim = blend(text, background, 60);
			const COLORREF link = is_dark(background) ? RGB(0x6C, 0xB4, 0xFF) : RGB(0x00, 0x5A, 0xC8);
			const COLORREF link_hover = is_dark(background) ? RGB(0xA8, 0xD4, 0xFF) : RGB(0x00, 0x3A, 0x8C);
			dc.FillSolidRect(&paint.m_ps.rcPaint, background);
			dc.SetBkMode(TRANSPARENT);

			const CRect & dirty = paint.m_ps.rcPaint;
			for (const line & l : m_lines)
			{
				const int top = l.y - m_scroll;
				if (top >= dirty.bottom || top + l.height <= dirty.top) continue;
				for (const piece & p : l.pieces)
				{
					if (p.link >= 0)
					{
						dc.SelectFont(m_link_fonts[p.font]);
						dc.SetTextColor(p.link == m_hover_link ? link_hover : link);
					}
					else
					{
						dc.SelectFont(m_fonts[p.font]);
						dc.SetTextColor(p.font == font_credits || p.font == font_note ? dim : text);
					}
					::ExtTextOutW(dc, p.x, top, 0, nullptr, p.text.c_str(), static_cast<UINT>(p.text.size()), nullptr);
				}
			}
		}

		// --- content -----------------------------------------------------------

		void ShowTrack(const metadb_handle_ptr & track)
		{
			const bool same = track == m_track && m_lines.size() > 0;
			m_track = track;
			m_content = lyrics_panel_content_for(track);
			if (!same) m_scroll = 0;
			m_hover_link = -1;
			m_pressed_link = -1;
			Layout();
			Invalidate();
		}

		void on_selection_changed(metadb_handle_list_cref selection) override
		{
			if (m_hWnd == NULL) return;
			ShowTrack(selection.get_count() > 0 ? selection[0] : metadb_handle_ptr());
		}

		//! Tags edited - by Find lyrics..., say: the panel follows.
		void on_changed_sorted(metadb_handle_list_cref items, bool) override
		{
			if (m_hWnd == NULL || !m_track.is_valid()) return;
			if (items.find_item(m_track) != SIZE_MAX) ShowTrack(m_track);
		}

		// --- fonts and layout --------------------------------------------------

		//! The Default UI's font, at the panel's size.
		LOGFONT BaseFont()
		{
			LOGFONT lf = {};
			HFONT ui = m_callback->query_font_ex(ui_font_default);
			if (ui == NULL || ::GetObject(ui, sizeof(lf), &lf) == 0)
			{
				NONCLIENTMETRICS ncm = { sizeof(ncm) };
				::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
				lf = ncm.lfMessageFont;
			}
			if (m_settings.font_size > 0) lf.lfHeight = -MulDiv(m_settings.font_size, QueryScreenDPI_Y(m_hWnd), 72);
			else if (lf.lfHeight > 0) lf.lfHeight = -lf.lfHeight;
			return lf;
		}

		//! The text size in points, as the size setting or as it follows the
		//! Default UI's font.
		int FontSizePoints()
		{
			if (m_settings.font_size > 0) return m_settings.font_size;
			const LOGFONT lf = BaseFont();
			return static_cast<int>(std::lround(-lf.lfHeight * 72.0 / QueryScreenDPI_Y(m_hWnd)));
		}

		void CreateFonts()
		{
			const LOGFONT base = BaseFont();
			for (int i = 0; i < font_count; i++)
			{
				LOGFONT lf = base;
				switch (i)
				{
				case font_heading:
					lf.lfHeight = MulDiv(base.lfHeight, 13, 10);
					lf.lfWeight = FW_BOLD;
					break;
				case font_credits:
				case font_note:
					lf.lfHeight = MulDiv(base.lfHeight, 9, 10);
					break;
				}
				if (!m_fonts[i].IsNull()) m_fonts[i].DeleteObject();
				if (!m_link_fonts[i].IsNull()) m_link_fonts[i].DeleteObject();
				m_fonts[i].CreateFontIndirect(&lf);
				lf.lfUnderline = TRUE;
				m_link_fonts[i].CreateFontIndirect(&lf);
			}
		}

		//! After a change of font, colours or settings.
		void Restyle()
		{
			if (m_hWnd == NULL) return;
			// Stay at the same place in the text.
			const double at = m_content_height > 0 ? static_cast<double>(m_scroll) / m_content_height : 0;
			CreateFonts();
			Layout();
			ScrollTo(static_cast<int>(at * m_content_height));
			Invalidate();
		}

		int Margin() { return MulDiv(12, QueryScreenDPI_Y(m_hWnd), 96); }

		//! Wraps the content's lines to the window's width.
		void Layout()
		{
			if (m_hWnd == NULL) return;
			CRect client;
			GetClientRect(&client);
			const int margin = Margin();
			const int avail = (std::max)(static_cast<int>(client.Width()) - 2 * margin, 1);

			CClientDC dc(*this);
			m_lines.clear();
			m_urls.clear();
			int y = margin;

			for (const lyrics_panel_line & source : m_content.lines)
			{
				const font_kind font = font_for(source.style);
				dc.SelectFont(m_fonts[font]);
				TEXTMETRIC tm = {};
				dc.GetTextMetrics(&tm);
				const int height = tm.tmHeight + tm.tmExternalLeading;
				if (source.text.empty())
				{
					y += height;
					continue;
				}

				// The line in runs of plain text and links, then in words: a
				// word is what is kept together when the line wraps, with the
				// spaces after it.
				struct run { std::wstring text; int link; };
				std::vector<run> runs;
				std::size_t at = 0;
				for (const tangotagger::text_link & l : source.links)
				{
					if (l.begin > at) runs.push_back({ wide(source.text.substr(at, l.begin - at)), -1 });
					m_urls.push_back(wide(l.url));
					runs.push_back({ wide(source.text.substr(l.begin, l.end - l.begin)), static_cast<int>(m_urls.size() - 1) });
					at = l.end;
				}
				if (at < source.text.size()) runs.push_back({ wide(source.text.substr(at)), -1 });

				std::vector<line> wrapped(1);
				int x = 0;
				auto measure = [&](const std::wstring & s) -> int
				{
					SIZE size = {};
					::GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &size);
					return size.cx;
				};
				auto place = [&](const std::wstring & s, int link)
				{
					piece p;
					p.text = s;
					p.x = x;
					p.width = measure(s);
					p.font = font;
					p.link = link;
					x += p.width;
					wrapped.back().pieces.push_back(std::move(p));
				};
				for (const run & r : runs)
				{
					dc.SelectFont(r.link >= 0 ? m_link_fonts[font] : m_fonts[font]);
					std::size_t i = 0;
					while (i < r.text.size())
					{
						// A word and the spaces after it.
						std::size_t end = r.text.find(L' ', i);
						if (end == std::wstring::npos) end = r.text.size();
						const std::size_t word_end = end;
						while (end < r.text.size() && r.text[end] == L' ') end++;
						if (word_end == i) { place(r.text.substr(i, end - i), r.link); i = end; continue; }

						std::wstring word = r.text.substr(i, word_end - i);
						const int word_width = measure(word);
						if (x > 0 && x + word_width > avail)
						{
							wrapped.emplace_back();
							x = 0;
						}
						// A word wider than the window - a long address - is
						// broken wherever it has to be.
						while (x == 0 && measure(word) > avail && word.size() > 1)
						{
							int fit = 0;
							SIZE size = {};
							::GetTextExtentExPointW(dc, word.c_str(), static_cast<int>(word.size()), avail, &fit, nullptr, &size);
							fit = (std::max)(fit, 1);
							place(word.substr(0, fit), r.link);
							word.erase(0, fit);
							wrapped.emplace_back();
							x = 0;
						}
						place(word + r.text.substr(word_end, end - word_end), r.link);
						i = end;
					}
				}

				for (line & l : wrapped)
				{
					// Trailing spaces do not count for centring.
					while (!l.pieces.empty() && l.pieces.back().text.find_first_not_of(L' ') == std::wstring::npos)
						l.pieces.pop_back();
					if (l.pieces.empty()) continue;
					piece & last = l.pieces.back();
					const std::size_t trimmed = last.text.find_last_not_of(L' ') + 1;
					if (trimmed < last.text.size())
					{
						dc.SelectFont(last.link >= 0 ? m_link_fonts[font] : m_fonts[font]);
						last.text.erase(trimmed);
						last.width = measure(last.text);
					}
					const int width = last.x + last.width;
					const int offset = margin + (m_settings.centered ? (std::max)((avail - width) / 2, 0) : 0);
					for (piece & p : l.pieces) p.x += offset;
					l.y = y;
					l.height = height;
					y += height;
					m_lines.push_back(std::move(l));
				}
			}
			m_content_height = y + margin;
			UpdateScrollBar();
		}

		// --- scrolling ---------------------------------------------------------

		int ClientHeight()
		{
			CRect client;
			GetClientRect(&client);
			return client.Height();
		}

		int MaxScroll() { return (std::max)(m_content_height - ClientHeight(), 0); }

		int LineStep()
		{
			CClientDC dc(*this);
			dc.SelectFont(m_fonts[font_text]);
			TEXTMETRIC tm = {};
			dc.GetTextMetrics(&tm);
			return (std::max)(static_cast<int>(tm.tmHeight + tm.tmExternalLeading), 1);
		}

		void UpdateScrollBar()
		{
			// Showing or hiding the scroll bar resizes the window, and
			// OnSize lays the text out again for the new width; wrapping
			// narrower only makes it longer, so that settles at once.
			m_scroll = (std::min)(m_scroll, MaxScroll());
			SCROLLINFO si = { sizeof(si) };
			si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
			si.nMin = 0;
			si.nMax = (std::max)(m_content_height - 1, 0);
			si.nPage = static_cast<UINT>(ClientHeight());
			si.nPos = m_scroll;
			SetScrollInfo(SB_VERT, &si, TRUE);
		}

		void ScrollTo(int pos)
		{
			pos = (std::max)(0, (std::min)(pos, MaxScroll()));
			if (pos == m_scroll) return;
			m_scroll = pos;
			SetScrollPos(SB_VERT, m_scroll, TRUE);
			m_hover_link = -1;
			Invalidate();
		}

		void OnVScroll(UINT code, UINT, CScrollBar)
		{
			const int line_step = LineStep();
			const int page = (std::max)(ClientHeight() - line_step, line_step);
			switch (code)
			{
			case SB_LINEUP:   ScrollTo(m_scroll - line_step); break;
			case SB_LINEDOWN: ScrollTo(m_scroll + line_step); break;
			case SB_PAGEUP:   ScrollTo(m_scroll - page); break;
			case SB_PAGEDOWN: ScrollTo(m_scroll + page); break;
			case SB_TOP:      ScrollTo(0); break;
			case SB_BOTTOM:   ScrollTo(MaxScroll()); break;
			case SB_THUMBTRACK:
			case SB_THUMBPOSITION:
			{
				SCROLLINFO si = { sizeof(si) };
				si.fMask = SIF_TRACKPOS;
				GetScrollInfo(SB_VERT, &si);
				ScrollTo(si.nTrackPos);
				break;
			}
			}
		}

		BOOL OnMouseWheel(UINT flags, short delta, CPoint)
		{
			if (flags & MK_CONTROL)
			{
				m_wheel_zoom += delta;
				const int steps = m_wheel_zoom / WHEEL_DELTA;
				m_wheel_zoom -= steps * WHEEL_DELTA;
				if (steps != 0) Zoom(steps);
				return TRUE;
			}
			UINT lines = 3;
			::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
			if (lines == WHEEL_PAGESCROLL) lines = static_cast<UINT>((std::max)(ClientHeight() / LineStep() - 1, 1));
			ScrollTo(m_scroll - MulDiv(delta, static_cast<int>(lines) * LineStep(), WHEEL_DELTA));
			return TRUE;
		}

		// --- text size ---------------------------------------------------------

		void Zoom(int steps)
		{
			const int size = (std::max)(min_font_size, (std::min)(FontSizePoints() + steps, max_font_size));
			if (size == m_settings.font_size) return;
			m_settings.font_size = size;
			Restyle();
		}

		void ResetZoom()
		{
			if (m_settings.font_size == 0) return;
			m_settings.font_size = 0;
			Restyle();
		}

		// --- links -------------------------------------------------------------

		//! The link under a point in client coordinates, or -1.
		int LinkAt(CPoint pt)
		{
			const int y = pt.y + m_scroll;
			for (const line & l : m_lines)
			{
				if (y < l.y || y >= l.y + l.height) continue;
				for (const piece & p : l.pieces)
					if (p.link >= 0 && pt.x >= p.x && pt.x < p.x + p.width) return p.link;
				break;
			}
			return -1;
		}

		void SetHover(int link)
		{
			if (link == m_hover_link) return;
			m_hover_link = link;
			Invalidate();
		}

		void OpenLink(int link)
		{
			if (link < 0 || link >= static_cast<int>(m_urls.size())) return;
			// find_links only finds http, https and www addresses, so this
			// opens the web browser and nothing else.
			::ShellExecuteW(m_hWnd, L"open", m_urls[link].c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}

		void OnMouseMove(UINT, CPoint pt)
		{
			if (!m_tracking)
			{
				TRACKMOUSEEVENT tme = { sizeof(tme) };
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = m_hWnd;
				m_tracking = ::TrackMouseEvent(&tme) != FALSE;
			}
			SetHover(LinkAt(pt));
		}

		void OnMouseLeave()
		{
			m_tracking = false;
			SetHover(-1);
		}

		BOOL OnSetCursor(CWindow, UINT hit, UINT)
		{
			if (hit == HTCLIENT && m_hover_link >= 0)
			{
				::SetCursor(::LoadCursor(NULL, IDC_HAND));
				return TRUE;
			}
			SetMsgHandled(FALSE);
			return FALSE;
		}

		void OnLButtonDown(UINT, CPoint pt)
		{
			SetFocus();
			m_pressed_link = LinkAt(pt);
		}

		void OnLButtonUp(UINT, CPoint pt)
		{
			const int link = LinkAt(pt);
			if (link >= 0 && link == m_pressed_link) OpenLink(link);
			m_pressed_link = -1;
		}

		// --- menu and keys -----------------------------------------------------

		void OnContextMenu(CWindow, CPoint pt)
		{
			// In layout editing the host's menu applies.
			if (m_callback->is_edit_mode_enabled())
			{
				SetMsgHandled(FALSE);
				return;
			}
			int link = -1;
			if (pt.x == -1 && pt.y == -1)
			{
				// From the keyboard.
				CRect client;
				GetClientRect(&client);
				pt = client.CenterPoint();
				ClientToScreen(&pt);
			}
			else
			{
				CPoint local = pt;
				ScreenToClient(&local);
				link = LinkAt(local);
			}

			CMenu menu;
			menu.CreatePopupMenu();
			if (link >= 0)
			{
				menu.AppendMenu(MF_STRING, id_open_link, L"&Open link");
				menu.AppendMenu(MF_STRING, id_copy_link, L"Copy link &address");
				menu.AppendMenu(MF_SEPARATOR);
			}
			menu.AppendMenu(MF_STRING | (m_content.copy_text.empty() ? MF_GRAYED : 0), id_copy, L"&Copy lyrics\tCtrl+C");
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING, id_larger, L"&Larger text\tCtrl+Plus");
			menu.AppendMenu(MF_STRING, id_smaller, L"&Smaller text\tCtrl+Minus");
			menu.AppendMenu(MF_STRING | (m_settings.font_size == 0 ? MF_GRAYED : 0), id_default_size,
			                L"&Default text size\tCtrl+0");
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING | (m_settings.centered ? MF_CHECKED : 0), id_centered, L"C&entered");

			const int cmd = menu.TrackPopupMenu(TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD, pt.x, pt.y, *this);
			switch (cmd)
			{
			case id_open_link:    OpenLink(link); break;
			case id_copy_link:    uSetClipboardString(pfc::stringcvt::string_utf8_from_wide(m_urls[link].c_str())); break;
			case id_copy:         CopyLyrics(); break;
			case id_larger:       Zoom(1); break;
			case id_smaller:      Zoom(-1); break;
			case id_default_size: ResetZoom(); break;
			case id_centered:
				m_settings.centered = !m_settings.centered;
				Restyle();
				break;
			}
		}

		void CopyLyrics()
		{
			if (m_content.copy_text.empty()) return;
			// CRLF for the Windows clipboard.
			pfc::string8 text;
			for (char c : m_content.copy_text)
			{
				if (c == '\n') text += "\r\n";
				else text.add_byte(c);
			}
			uSetClipboardString(text);
		}

		void OnKeyDown(UINT key, UINT, UINT)
		{
			const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
			if (ctrl)
			{
				switch (key)
				{
				case VK_OEM_PLUS: case VK_ADD:        Zoom(1); return;
				case VK_OEM_MINUS: case VK_SUBTRACT:  Zoom(-1); return;
				case '0': case VK_NUMPAD0:            ResetZoom(); return;
				case 'C':                             CopyLyrics(); return;
				}
			}
			else
			{
				switch (key)
				{
				case VK_UP:    OnVScroll(SB_LINEUP, 0, NULL); return;
				case VK_DOWN:  OnVScroll(SB_LINEDOWN, 0, NULL); return;
				case VK_PRIOR: OnVScroll(SB_PAGEUP, 0, NULL); return;
				case VK_NEXT:  OnVScroll(SB_PAGEDOWN, 0, NULL); return;
				case VK_HOME:  ScrollTo(0); return;
				case VK_END:   ScrollTo(MaxScroll()); return;
				}
			}
			// foobar2000's own keyboard shortcuts keep working while the
			// panel has the focus.
			if (!keyboard_shortcut_manager::get()->on_keydown_auto(key)) SetMsgHandled(FALSE);
		}

		UINT OnGetDlgCode(LPMSG) { return DLGC_WANTARROWS; }

		panel_settings m_settings;
		lyrics_panel_content m_content;
		metadb_handle_ptr m_track;

		CFont m_fonts[font_count];
		CFont m_link_fonts[font_count];
		std::vector<line> m_lines;
		std::vector<std::wstring> m_urls;
		int m_content_height = 0;
		int m_scroll = 0;
		int m_wheel_zoom = 0;
		int m_hover_link = -1;
		int m_pressed_link = -1;
		bool m_tracking = false;

		fb2k::CDarkModeHooks m_dark;

	protected:
		// Protected, not private: ui_element_impl_withpopup needs it.
		const ui_element_instance_callback_ptr m_callback;
	};

	class lyrics_panel_element : public ui_element_impl_withpopup<lyrics_panel> {};

	FB2K_SERVICE_FACTORY(lyrics_panel_element);
}
