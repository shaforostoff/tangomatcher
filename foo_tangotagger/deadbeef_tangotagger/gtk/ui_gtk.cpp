// The plugin's windows in GTK 3, and the lyrics panel. See tt_ui.h for what
// they are and tt_review.h / tt_lyrics.h for everything they show; this file
// lays them out and passes clicks back, as cocoa/ui_cocoa.mm does in Cocoa,
// and decides nothing that one does not.
//
// Everything here runs on GTK's thread. The entry points may be called from
// any thread, and hand their work over with g_idle_add.
//
// GTK 3.10 is what DeaDBeeF's plugin builder compiles against, so nothing
// newer is used without a GTK_CHECK_VERSION.

#include <gtk/gtk.h>

#include "tt_lyrics.h"
#include "tt_plugin.h"
#include "tt_ui.h"

#include <deadbeef/gtkui_api.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{

DB_functions_t * deadbeef = nullptr;
ddb_gtkui_t * gtkui = nullptr;
std::atomic<bool> ready(false);

//! Every window this file has open, for shutdown to close.
std::set<GtkWidget *> open_windows;

void on_gtk_thread(std::function<void()> fn)
{
	g_idle_add([](gpointer data) -> gboolean {
		std::unique_ptr<std::function<void()>> f(static_cast<std::function<void()> *>(data));
		if (ready.load()) (*f)();
		return G_SOURCE_REMOVE;
	}, new std::function<void()>(std::move(fn)));
}

GtkWindow * main_window()
{
	GtkWidget * w = gtkui != nullptr ? gtkui->get_mainwin() : nullptr;
	return w != nullptr ? GTK_WINDOW(w) : nullptr;
}

//! A top-level window over DeaDBeeF's own, closed by Escape.
GtkWidget * new_window(const std::string & title, int width, int height)
{
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), title.c_str());
	gtk_window_set_default_size(GTK_WINDOW(window), width, height);
	if (GtkWindow * parent = main_window())
	{
		gtk_window_set_transient_for(GTK_WINDOW(window), parent);
		gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ON_PARENT);
	}
	gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_container_set_border_width(GTK_CONTAINER(window), 12);
	open_windows.insert(window);
	g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget * w, gpointer) {
		open_windows.erase(w);
	}), nullptr);
	g_signal_connect(window, "key-press-event", G_CALLBACK(+[](GtkWidget * w, GdkEventKey * e, gpointer) -> gboolean {
		if (e->keyval != GDK_KEY_Escape) return FALSE;
		gtk_widget_destroy(w);
		return TRUE;
	}), nullptr);
	return window;
}

void align_left(GtkWidget * label)
{
	// gtk_label_set_xalign is GTK 3.16; GtkMisc's alignment does the same in
	// every GTK 3, only deprecated.
#if GTK_CHECK_VERSION(3, 16, 0)
	gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
#else
	gtk_misc_set_alignment(GTK_MISC(label), 0.0f, 0.5f);
#endif
}

GtkWidget * left_label(const std::string & text)
{
	GtkWidget * label = gtk_label_new(text.c_str());
	align_left(label);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	// A wrapping label asks for the width of its whole text on one line
	// unless told otherwise.
	gtk_label_set_max_width_chars(GTK_LABEL(label), 80);
	return label;
}

// --- the results window ----------------------------------------------------
//
// The list store has, per row: whether it is checked, whether it can be, the
// shade of its track's block, then one string per column of the review - the
// check column's being the candidate's number.

enum { store_checked, store_checkable, store_shade, store_text };

struct review_window
{
	std::shared_ptr<tt::review> model;
	std::vector<tt::review_column> columns;
	GtkWidget * window = nullptr;
	GtkWidget * view = nullptr;
	GtkListStore * store = nullptr;
	GtkTextBuffer * preview = nullptr;
	GtkWidget * write = nullptr;
};

//! A faint grey over whatever the theme's background is, for every other
//! track: light and dark themes alike.
const GdkRGBA odd_block = { 0.5, 0.5, 0.5, 0.10 };
const GdkRGBA even_block = { 0, 0, 0, 0 };

void refresh_row(review_window & w, std::size_t row)
{
	GtkTreeIter iter;
	if (!gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(w.store), &iter, nullptr, static_cast<gint>(row))) return;
	gtk_list_store_set(w.store, &iter,
	                   store_checked, w.model->checked(row) ? TRUE : FALSE,
	                   store_checkable, w.model->checkable(row) ? TRUE : FALSE,
	                   store_shade, w.model->block(row) % 2 ? &odd_block : &even_block,
	                   -1);
	for (std::size_t c = 0; c < w.columns.size(); c++)
		gtk_list_store_set(w.store, &iter, static_cast<gint>(store_text + c), w.model->cell(row, c).c_str(), -1);
}

void relabel_write(review_window & w)
{
	gtk_button_set_label(GTK_BUTTON(w.write), w.model->commit_label().c_str());
	gtk_widget_set_sensitive(w.write, w.model->count_checked() > 0);
}

void refresh_all(review_window & w)
{
	for (std::size_t row = 0; row < w.model->size(); row++) refresh_row(w, row);
	relabel_write(w);
}

int selected_row(review_window & w)
{
	GtkTreeModel * model = nullptr;
	GtkTreeIter iter;
	GtkTreeSelection * selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(w.view));
	if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return -1;
	GtkTreePath * path = gtk_tree_model_get_path(model, &iter);
	const int row = gtk_tree_path_get_indices(path)[0];
	gtk_tree_path_free(path);
	return row;
}

void show_preview(review_window & w)
{
	const int row = selected_row(w);
	const std::string text = row >= 0 ? w.model->preview(static_cast<std::size_t>(row)) : std::string();
	gtk_text_buffer_set_text(w.preview, text.c_str(), -1);
}

GtkWidget * build_options(review_window * w)
{
	GtkWidget * row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	const std::vector<tt::review_option> options = w->model->options();
	bool after_choice = false;
	for (std::size_t i = 0; i < options.size(); i++)
	{
		const tt::review_option & o = options[i];
		GtkWidget * control;
		if (o.kind == tt::review_option::choice)
		{
			gtk_box_pack_start(GTK_BOX(row), gtk_label_new(o.label.c_str()), FALSE, FALSE, 0);
			control = gtk_combo_box_text_new();
			for (const std::string & c : o.choices)
				gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(control), c.c_str());
			gtk_combo_box_set_active(GTK_COMBO_BOX(control), o.value);
			g_signal_connect(control, "changed", G_CALLBACK(+[](GtkComboBox * box, gpointer data) {
				review_window & w = *static_cast<review_window *>(data);
				const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(box), "tt-option"));
				w.model->set_option(index, gtk_combo_box_get_active(box));
				refresh_all(w);
				show_preview(w);
			}), w);
			after_choice = true;
		}
		else
		{
			if (after_choice)
				gtk_box_pack_start(GTK_BOX(row), gtk_label_new("   Write:"), FALSE, FALSE, 0);
			after_choice = false;
			control = gtk_check_button_new_with_label(o.label.c_str());
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control), o.value != 0);
			g_signal_connect(control, "toggled", G_CALLBACK(+[](GtkToggleButton * box, gpointer data) {
				review_window & w = *static_cast<review_window *>(data);
				const std::size_t index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(box), "tt-option"));
				w.model->set_option(index, gtk_toggle_button_get_active(box) ? 1 : 0);
				refresh_all(w);
				show_preview(w);
			}), w);
		}
		g_object_set_data(G_OBJECT(control), "tt-option", GSIZE_TO_POINTER(i));
		gtk_box_pack_start(GTK_BOX(row), control, FALSE, FALSE, 0);
	}
	return row;
}

void open_review(std::shared_ptr<tt::review> model)
{
	review_window * w = new review_window();
	w->model = std::move(model);
	w->columns = w->model->columns();

	w->window = new_window(w->model->window_title(), 1000, 640);
	g_signal_connect(w->window, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer data) {
		delete static_cast<review_window *>(data);
	}), w);

	GtkWidget * vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_add(GTK_CONTAINER(w->window), vbox);

	std::vector<GType> types = { G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, GDK_TYPE_RGBA };
	types.resize(store_text + w->columns.size(), G_TYPE_STRING);
	w->store = gtk_list_store_newv(static_cast<gint>(types.size()), types.data());
	for (std::size_t row = 0; row < w->model->size(); row++)
	{
		GtkTreeIter iter;
		gtk_list_store_append(w->store, &iter);
	}

	w->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(w->store));
	g_object_unref(w->store);   // the view holds it now
	for (std::size_t c = 0; c < w->columns.size(); c++)
	{
		const tt::review_column & rc = w->columns[c];
		const gint text = static_cast<gint>(store_text + c);
		GtkTreeViewColumn * column = gtk_tree_view_column_new();
		gtk_tree_view_column_set_title(column, rc.heading.c_str());
		if (rc.kind == tt::review_column::check)
		{
			// The checkbox, and the candidate's number beside it.
			GtkCellRenderer * toggle = gtk_cell_renderer_toggle_new();
			gtk_tree_view_column_pack_start(column, toggle, FALSE);
			gtk_tree_view_column_set_attributes(column, toggle, "active", store_checked, "visible", store_checkable,
			                                    "activatable", store_checkable, "cell-background-rgba", store_shade,
			                                    nullptr);
			g_signal_connect(toggle, "toggled", G_CALLBACK(+[](GtkCellRendererToggle *, gchar * path, gpointer data) {
				review_window & w = *static_cast<review_window *>(data);
				const std::size_t row = static_cast<std::size_t>(std::atoi(path));
				// Checking one candidate of a track unchecks the others.
				w.model->set_checked(row, !w.model->checked(row));
				refresh_all(w);
			}), w);
		}
		GtkCellRenderer * renderer = gtk_cell_renderer_text_new();
		gtk_tree_view_column_pack_start(column, renderer, TRUE);
		gtk_tree_view_column_set_attributes(column, renderer, "text", text, "sensitive", store_checkable,
		                                    "cell-background-rgba", store_shade, nullptr);
		gtk_tree_view_column_set_resizable(column, rc.kind != tt::review_column::check);
		if (rc.kind == tt::review_column::fill)
		{
			// Takes a share of what the others leave, and is cut short with
			// an ellipsis rather than pushing them out of the window.
			g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, nullptr);
			gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
			gtk_tree_view_column_set_fixed_width(column, rc.width);
			gtk_tree_view_column_set_expand(column, TRUE);
		}
		else
		{
			// As wide as its widest cell, and no wider.
			gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
		}
		gtk_tree_view_append_column(GTK_TREE_VIEW(w->view), column);
	}
	GtkTreeSelection * selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(w->view));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);
	g_signal_connect(selection, "changed", G_CALLBACK(+[](GtkTreeSelection *, gpointer data) {
		show_preview(*static_cast<review_window *>(data));
	}), w);

	GtkWidget * list_scroller = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(list_scroller), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(list_scroller), w->view);

	GtkWidget * text = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text), 4);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text), 4);
	w->preview = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
	GtkWidget * preview_scroller = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(preview_scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(preview_scroller), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(preview_scroller), text);

	// The list and the preview share the height, the divider where the user
	// wants it.
	GtkWidget * paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	gtk_paned_pack1(GTK_PANED(paned), list_scroller, TRUE, FALSE);
	gtk_paned_pack2(GTK_PANED(paned), preview_scroller, TRUE, FALSE);
	gtk_paned_set_position(GTK_PANED(paned), 300);
	gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(vbox), left_label(w->model->status()), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), build_options(w), FALSE, FALSE, 4);

	GtkWidget * buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_pack_start(GTK_BOX(vbox), buttons, FALSE, FALSE, 0);
	GtkWidget * check_all = gtk_button_new_with_mnemonic("Check _All");
	GtkWidget * check_none = gtk_button_new_with_mnemonic("Check _None");
	gtk_box_pack_start(GTK_BOX(buttons), check_all, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(buttons), check_none, FALSE, FALSE, 0);
	g_signal_connect(check_all, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
		review_window & w = *static_cast<review_window *>(data);
		w.model->check_all();
		refresh_all(w);
	}), w);
	g_signal_connect(check_none, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
		review_window & w = *static_cast<review_window *>(data);
		w.model->check_none();
		refresh_all(w);
	}), w);

	w->write = gtk_button_new_with_label("");
	GtkWidget * cancel = gtk_button_new_with_mnemonic("_Cancel");
	gtk_box_pack_end(GTK_BOX(buttons), w->write, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(buttons), cancel, FALSE, FALSE, 0);
	g_signal_connect(w->write, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
		review_window & w = *static_cast<review_window *>(data);
		w.model->commit();
		gtk_widget_destroy(w.window);
	}), w);
	g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_widget_destroy), w->window);
	gtk_widget_set_can_default(w->write, TRUE);
	gtk_window_set_default(GTK_WINDOW(w->window), w->write);

	refresh_all(*w);
	gtk_widget_show_all(w->window);
	if (w->model->size() > 0)
	{
		GtkTreePath * first = gtk_tree_path_new_first();
		gtk_tree_selection_select_path(selection, first);
		gtk_tree_path_free(first);
	}
	gtk_widget_grab_focus(w->view);
}

// --- the progress window ---------------------------------------------------

//! Owned by its timer, which outlives the window: Stop closes the window at
//! once, and the timer goes on until the listening has noticed and stopped.
struct progress_window
{
	std::shared_ptr<tt::scan_progress> progress;
	std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
	GtkWidget * window = nullptr;
	GtkWidget * items = nullptr;
	GtkWidget * bar = nullptr;
	bool dismissed = false;   //!< stopped, or closed from outside
};

void build_progress(progress_window & p)
{
	p.window = new_window("Tango Tagger", 460, -1);
	gtk_window_set_resizable(GTK_WINDOW(p.window), FALSE);
	g_signal_connect(p.window, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer data) {
		progress_window & p = *static_cast<progress_window *>(data);
		p.window = nullptr;
		if (!p.dismissed)
		{
			// Stop, Escape, the window manager or shutdown: whichever,
			// listening stops, and the results window opens with what there is.
			p.dismissed = true;
			p.progress->cancel();
		}
	}), &p);

	GtkWidget * vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_add(GTK_CONTAINER(p.window), vbox);
	p.items = left_label("");
	gtk_label_set_ellipsize(GTK_LABEL(p.items), PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_line_wrap(GTK_LABEL(p.items), FALSE);
	p.bar = gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(vbox), left_label(p.progress->title()), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), p.bar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), p.items, FALSE, FALSE, 0);

	GtkWidget * buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget * stop = gtk_button_new_with_mnemonic("_Stop");
	gtk_box_pack_end(GTK_BOX(buttons), stop, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), buttons, FALSE, FALSE, 0);
	g_signal_connect_swapped(stop, "clicked", G_CALLBACK(gtk_widget_destroy), p.window);

	gtk_widget_show_all(p.window);
}

gboolean progress_tick(gpointer data)
{
	progress_window * p = static_cast<progress_window *>(data);
	const tt::scan_progress::snapshot s = p->progress->read();

	if (s.finished || !ready.load())
	{
		p->dismissed = true;
		if (p->window != nullptr) gtk_widget_destroy(p->window);
		delete p;
		return G_SOURCE_REMOVE;
	}
	if (p->dismissed) return G_SOURCE_CONTINUE;

	// Not at all for listening over before it would have been read.
	if (p->window == nullptr)
	{
		if (std::chrono::steady_clock::now() - p->started < std::chrono::milliseconds(600)) return G_SOURCE_CONTINUE;
		build_progress(*p);
	}
	gtk_label_set_text(GTK_LABEL(p->items), s.in_flight.c_str());
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(p->bar), s.fraction);
	return G_SOURCE_CONTINUE;
}

// --- the lyrics panel --------------------------------------------------------
//
// A design mode widget, "Tango Lyrics": the lyrics of the track under the
// cursor, or of the one playing when there is none - the file's own, or the
// built-in song its title matches, marked as not in the file. Web addresses
// are links.

struct panel_state
{
	GtkWidget * view = nullptr;
	GtkTextBuffer * buffer = nullptr;
	//! An update is waiting for GTK's thread. Set on DeaDBeeF's message
	//! thread, which is where widgets are sent events.
	std::atomic<bool> queued{false};
};

struct w_panel_t
{
	ddb_gtkui_widget_t base;
	panel_state * state;
};

//! The panels alive, so an update queued for one that has gone since is
//! dropped.
std::set<w_panel_t *> live_panels;

//! The track the panel is about, with a reference held; null for none.
DB_playItem_t * panel_track()
{
	DB_playItem_t * track = nullptr;
	if (ddb_playlist_t * plt = deadbeef->plt_get_curr())
	{
		const int cursor = deadbeef->plt_get_cursor(plt, PL_MAIN);
		if (cursor >= 0) track = deadbeef->plt_get_item_for_idx(plt, cursor, PL_MAIN);
		deadbeef->plt_unref(plt);
	}
	if (track == nullptr) track = deadbeef->streamer_get_playing_track();
	return track;
}

void panel_update(w_panel_t * w)
{
	panel_state & s = *w->state;
	s.queued = false;

	tt::panel_content content;
	if (DB_playItem_t * track = panel_track())
	{
		const tt::track_meta meta = tt::snapshot(track);
		deadbeef->pl_item_unref(track);
		content = tt::panel_content_for(&meta);
	}
	else
	{
		content = tt::panel_content_for(nullptr);
	}

	GtkTextBuffer * b = s.buffer;
	gtk_text_buffer_set_text(b, "", -1);
	GtkTextIter end;
	for (std::size_t i = 0; i < content.lines.size(); i++)
	{
		const tt::panel_line & line = content.lines[i];
		const char * style = line.style == tt::panel_style::heading ? "heading"
		                   : line.style == tt::panel_style::credits ? "credits"
		                   : line.style == tt::panel_style::note    ? "note"
		                                                            : "text";
		GtkTextTag * style_tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(b), style);
		std::size_t at = 0;
		for (const tangotagger::text_link & link : line.links)
		{
			gtk_text_buffer_get_end_iter(b, &end);
			gtk_text_buffer_insert_with_tags(b, &end, line.text.c_str() + at, static_cast<gint>(link.begin - at),
			                                 style_tag, nullptr);
			// One tag per link, carrying where it goes.
			GtkTextTag * link_tag = gtk_text_buffer_create_tag(b, nullptr, "foreground", "#3584e4",
			                                                   "underline", PANGO_UNDERLINE_SINGLE, nullptr);
			g_object_set_data_full(G_OBJECT(link_tag), "tt-url", g_strdup(link.url.c_str()), g_free);
			gtk_text_buffer_get_end_iter(b, &end);
			gtk_text_buffer_insert_with_tags(b, &end, line.text.c_str() + link.begin,
			                                 static_cast<gint>(link.end - link.begin), style_tag, link_tag, nullptr);
			at = link.end;
		}
		gtk_text_buffer_get_end_iter(b, &end);
		gtk_text_buffer_insert_with_tags(b, &end, line.text.c_str() + at, -1, style_tag, nullptr);
		if (i + 1 < content.lines.size())
		{
			gtk_text_buffer_get_end_iter(b, &end);
			gtk_text_buffer_insert(b, &end, "\n", 1);
		}
	}
	GtkTextIter start;
	gtk_text_buffer_get_start_iter(b, &start);
	gtk_text_buffer_place_cursor(b, &start);
	gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(s.view), &start, 0, FALSE, 0, 0);
}

void panel_queue_update(w_panel_t * w)
{
	g_idle_add([](gpointer data) -> gboolean {
		w_panel_t * w = static_cast<w_panel_t *>(data);
		if (live_panels.count(w) != 0 && w->state->queued) panel_update(w);
		return G_SOURCE_REMOVE;
	}, w);
}

int panel_message(ddb_gtkui_widget_t * base, uint32_t id, uintptr_t, uint32_t, uint32_t)
{
	switch (id)
	{
	case DB_EV_CURSOR_MOVED:
	case DB_EV_PLAYLISTSWITCHED:
	case DB_EV_PLAYLISTCHANGED:
	case DB_EV_TRACKINFOCHANGED:
	case DB_EV_SONGSTARTED:
	case DB_EV_STOP:
		break;
	default:
		return 0;
	}
	// Several events arrive for one change; one update answers them all.
	w_panel_t * w = reinterpret_cast<w_panel_t *>(base);
	if (!w->state->queued.exchange(true)) panel_queue_update(w);
	return 0;
}

void panel_destroy(ddb_gtkui_widget_t * base)
{
	w_panel_t * w = reinterpret_cast<w_panel_t *>(base);
	live_panels.erase(w);
	delete w->state;
	w->state = nullptr;
}

//! A click on a link opens it in the web browser. On the release, and only
//! if nothing was selected by dragging.
gboolean panel_clicked(GtkWidget * view, GdkEventButton * e, gpointer)
{
	if (e->type != GDK_BUTTON_RELEASE || e->button != 1) return FALSE;
	GtkTextBuffer * buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	GtkTextIter a, b;
	if (gtk_text_buffer_get_selection_bounds(buffer, &a, &b)) return FALSE;
	gint x = 0, y = 0;
	gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_WIDGET,
	                                      static_cast<gint>(e->x), static_cast<gint>(e->y), &x, &y);
	GtkTextIter at;
	gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(view), &at, x, y);
	GSList * tags = gtk_text_iter_get_tags(&at);
	const char * url = nullptr;
	for (GSList * t = tags; t != nullptr && url == nullptr; t = t->next)
		url = static_cast<const char *>(g_object_get_data(G_OBJECT(t->data), "tt-url"));
	g_slist_free(tags);
	// find_links only ever finds http, https and www addresses, so this
	// cannot start a program or open a local file.
	if (url != nullptr)
	{
#if GTK_CHECK_VERSION(3, 22, 0)
		gtk_show_uri_on_window(main_window(), url, e->time, nullptr);
#else
		gtk_show_uri(gtk_widget_get_screen(view), url, e->time, nullptr);
#endif
	}
	return FALSE;
}

ddb_gtkui_widget_t * panel_create()
{
	// gtkui frees the widget struct itself, with free(): calloc'd, and the
	// C++ state kept behind a pointer it does not know about.
	w_panel_t * w = static_cast<w_panel_t *>(std::calloc(1, sizeof(w_panel_t)));
	w->state = new panel_state();
	w->base.widget = gtk_event_box_new();
	w->base.message = panel_message;
	w->base.destroy = panel_destroy;
	gtk_widget_set_can_focus(w->base.widget, FALSE);

	GtkWidget * scroll = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_widget_set_can_focus(scroll, FALSE);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_ETCHED_IN);
	gtk_container_add(GTK_CONTAINER(w->base.widget), scroll);

	GtkWidget * view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD);
	gtk_text_view_set_justification(GTK_TEXT_VIEW(view), GTK_JUSTIFY_CENTER);
	gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(view), 2);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 8);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 8);
	gtk_container_set_border_width(GTK_CONTAINER(view), 8);
	gtk_container_add(GTK_CONTAINER(scroll), view);
	g_signal_connect(view, "button-release-event", G_CALLBACK(panel_clicked), nullptr);

	GtkTextBuffer * buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	gtk_text_buffer_create_tag(buffer, "heading", "weight", PANGO_WEIGHT_BOLD, "scale", 1.4, nullptr);
	gtk_text_buffer_create_tag(buffer, "credits", "style", PANGO_STYLE_ITALIC, nullptr);
	gtk_text_buffer_create_tag(buffer, "text", nullptr);
	gtk_text_buffer_create_tag(buffer, "note", "scale", 0.85, "foreground", "#888888", nullptr);
	w->state->view = view;
	w->state->buffer = buffer;

	gtk_widget_show_all(w->base.widget);
	gtkui->w_override_signals(w->base.widget, w);
	live_panels.insert(w);
	panel_update(w);
	return &w->base;
}

const char * const panel_type = "tangotagger_lyrics";

}   // namespace

// --- tt_ui.h -----------------------------------------------------------------

void tt::ui::connect(DB_functions_t * api)
{
	deadbeef = api;
	// These windows are GTK 3, and belong only in DeaDBeeF's GTK 3 interface.
	// Under any other - GTK 2, or none - the plugin goes on without them.
	gtkui = reinterpret_cast<ddb_gtkui_t *>(deadbeef->plug_get_for_id(DDB_GTKUI_PLUGIN_ID));
	ready = gtkui != nullptr && gtkui->gui.plugin.version_major == DDB_GTKUI_API_VERSION_MAJOR;
	if (ready.load()) gtkui->w_reg_widget("Tango Lyrics", 0, panel_create, panel_type, nullptr);
}

void tt::ui::disconnect()
{
	if (ready.load() && gtkui != nullptr) gtkui->w_unreg_widget(panel_type);
}

void tt::ui::shutdown()
{
	if (!ready.exchange(false)) return;
	// Closing a window gives back its tracks. A copy, because each window
	// takes itself out of the set as it goes.
	const std::set<GtkWidget *> windows = open_windows;
	for (GtkWidget * w : windows) gtk_widget_destroy(w);
}

bool tt::ui::available()
{
	return ready.load();
}

void tt::ui::show_progress(std::shared_ptr<scan_progress> progress)
{
	on_gtk_thread([progress]() {
		progress_window * p = new progress_window();
		p->progress = progress;
		g_timeout_add(100, progress_tick, p);
	});
}

void tt::ui::show_review(std::shared_ptr<review> r)
{
	on_gtk_thread([r]() { open_review(r); });
}

void tt::ui::show_message(const std::string & title, const std::string & text)
{
	on_gtk_thread([title, text]() {
		GtkWidget * dialog = gtk_message_dialog_new(main_window(), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO,
		                                            GTK_BUTTONS_OK, "%s", text.c_str());
		gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());
		g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), nullptr);
		gtk_widget_show(dialog);
	});
}
