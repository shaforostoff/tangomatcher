// The GTK windows and the lyrics panel, on the stand-in for DeaDBeeF with
// made-up tracks behind them (preview_tracks.h). Not a test: the way to look
// at them without the player, as tt_cocoa_preview is for the Cocoa windows.
//
//   tt_gtk_preview            match the tracks' lyrics and recordings and use
//                             the windows as they are; close them all to quit
//   tt_gtk_preview <dir>      draw the lyrics window, the listening progress,
//                             the discographies window and the lyrics panel to
//                             PNGs in <dir> and exit

#include <gtk/gtk.h>

#include <cstdio>
#include <string>

#include "fake_host.h"
#include "preview_tracks.h"
#include "tt_ui.h"

// After deadbeef.h, which it builds on.
#include <deadbeef/gtkui_api.h>

using namespace fake;

namespace
{

ddb_gtkui_t fake_gtkui;
std::string out_dir;
DB_plugin_t * plugin = nullptr;
ddb_gtkui_widget_t * (*panel_create)(void) = nullptr;

GtkWidget * fake_mainwin() { return nullptr; }
void fake_reg_widget(const char *, uint32_t, ddb_gtkui_widget_t * (*create)(void), ...) { panel_create = create; }
void fake_unreg_widget(const char *) {}
void fake_override_signals(GtkWidget *, gpointer) {}

GtkWidget * find_window(const char * title)
{
	GtkWidget * found = nullptr;
	GList * all = gtk_window_list_toplevels();
	for (GList * l = all; l != nullptr; l = l->next)
	{
		GtkWindow * w = GTK_WINDOW(l->data);
		const char * t = gtk_window_get_title(w);
		if (t != nullptr && std::string(t) == title && gtk_widget_get_mapped(GTK_WIDGET(w))) found = GTK_WIDGET(w);
	}
	g_list_free(all);
	return found;
}

//! The window's pixels as the display has them, read back rather than
//! redrawn, so what is saved is what would be seen.
void snapshot(GtkWidget * window, const char * name)
{
	if (window == nullptr) return;
	GdkWindow * gdk = gtk_widget_get_window(window);
	const int w = gdk_window_get_width(gdk);
	const int h = gdk_window_get_height(gdk);
	GdkPixbuf * pixels = gdk_pixbuf_get_from_window(gdk, 0, 0, w, h);
	const std::string path = out_dir + "/" + name + ".png";
	if (pixels != nullptr)
	{
		gdk_pixbuf_save(pixels, path.c_str(), "png", nullptr, nullptr);
		g_object_unref(pixels);
	}
	std::printf("wrote %s (%dx%d)\n", path.c_str(), w, h);
}

void run(const char * action)
{
	DB_plugin_action_t * a = find_action(plugin, action);
	a->callback2(a, DDB_ACTION_CTX_SELECTION);
}

//! The panel in a window of its own, showing the track under the cursor.
GtkWidget * show_panel()
{
	if (panel_create == nullptr) return nullptr;
	// The panel follows the cursor, which the stand-in puts on the first
	// selected track: Yira, yira, whose lyrics are only built in.
	for (fake_track * t : playlist) t->selected = get(*t, "title") == "Yira, yira";
	ddb_gtkui_widget_t * w = panel_create();
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "Tango Lyrics");
	gtk_window_set_default_size(GTK_WINDOW(window), 420, 560);
	gtk_container_add(GTK_CONTAINER(window), w->widget);
	g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer data) {
		ddb_gtkui_widget_t * w = static_cast<ddb_gtkui_widget_t *>(data);
		w->destroy(w);
		free(w);
	}), w);
	gtk_widget_show_all(window);
	return window;
}

gboolean watch_disco(gpointer)
{
	static bool saw_progress = false;
	static int progress_frames = 0;
	if (!saw_progress)
		if (GtkWidget * p = find_window("Tango Tagger"))
			if (++progress_frames > 4)
			{
				saw_progress = true;
				snapshot(p, "progress");
			}
	if (find_window("Tango Tagger - Match Discographies") == nullptr) return G_SOURCE_CONTINUE;
	g_timeout_add(1500, [](gpointer) -> gboolean {
		snapshot(find_window("Tango Tagger - Match Discographies"), "disco");
		show_panel();
		g_timeout_add(1500, [](gpointer) -> gboolean {
			snapshot(find_window("Tango Lyrics"), "panel");
			gtk_main_quit();
			return G_SOURCE_REMOVE;
		}, nullptr);
		return G_SOURCE_REMOVE;
	}, nullptr);
	return G_SOURCE_REMOVE;
}

gboolean watch_lyrics(gpointer)
{
	if (find_window("Tango Tagger - Lyrics") == nullptr) return G_SOURCE_CONTINUE;
	g_timeout_add(1500, [](gpointer) -> gboolean {
		GtkWidget * w = find_window("Tango Tagger - Lyrics");
		snapshot(w, "lyrics");
		gtk_widget_destroy(w);
		run("tangotagger_match_discographies");
		g_timeout_add(50, watch_disco, nullptr);
		return G_SOURCE_REMOVE;
	}, nullptr);
	return G_SOURCE_REMOVE;
}

}   // namespace

int main(int argc, char ** argv)
{
	gtk_init(&argc, &argv);
	if (argc > 1) out_dir = argv[1];
	quiet = !out_dir.empty();
	init();

	add_preview_tracks();

	fake_gtkui.gui.plugin.id = DDB_GTKUI_PLUGIN_ID;
	fake_gtkui.gui.plugin.version_major = DDB_GTKUI_API_VERSION_MAJOR;
	fake_gtkui.get_mainwin = fake_mainwin;
	fake_gtkui.w_reg_widget = fake_reg_widget;
	fake_gtkui.w_unreg_widget = fake_unreg_widget;
	fake_gtkui.w_override_signals = fake_override_signals;
	ui_plugin = &fake_gtkui.gui.plugin;

	DB_functions_t api = make_api();
	plugin = ddb_tangotagger_load(&api);
	plugin->start();
	plugin->connect();

	run("tangotagger_find_lyrics");
	if (!out_dir.empty())
	{
		g_timeout_add(50, watch_lyrics, nullptr);
	}
	else
	{
		run("tangotagger_match_discographies");
		show_panel();
		// Until the last window is closed.
		g_timeout_add(500, [](gpointer) -> gboolean {
			GList * all = gtk_window_list_toplevels();
			bool any = false;
			for (GList * l = all; l != nullptr; l = l->next) any = any || gtk_widget_get_visible(GTK_WIDGET(l->data));
			g_list_free(all);
			static int idle = 0;
			idle = any ? 0 : idle + 1;
			if (idle < 20) return G_SOURCE_CONTINUE;
			gtk_main_quit();
			return G_SOURCE_REMOVE;
		}, nullptr);
	}
	gtk_main();
	plugin->disconnect();
	plugin->stop();
	return 0;
}
