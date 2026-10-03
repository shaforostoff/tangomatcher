// The Cocoa windows, on the stand-in for DeaDBeeF with made-up tracks behind
// them (preview_tracks.h). Not a test: the way to look at the windows without
// the player, as tt_gtk_preview is for the GTK ones.
//
//   tt_cocoa_preview          match the tracks' lyrics and recordings and use
//                             the windows as they are; close them all to quit
//   tt_cocoa_preview <dir>    draw the lyrics window, the listening progress
//                             and the discographies window to PNGs in <dir>
//                             and exit

#import <Cocoa/Cocoa.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "fake_host.h"
#include "preview_tracks.h"
#include "tt_ui.h"

using namespace fake;

namespace
{

DB_plugin_t fake_cocoaui;
std::string out_dir;
DB_plugin_t * plugin = nullptr;

NSWindow * find_window(NSString * title)
{
	for (NSWindow * w in NSApp.windows)
		if (w.visible && [w.title isEqualToString:title]) return w;
	return nil;
}

//! The whole window, title bar and all, drawn into a bitmap rather than read
//! off the screen, which would want the screen recording permission.
void snapshot(NSWindow * window, const char * name)
{
	if (window == nil) return;
	NSView * frame = window.contentView.superview;
	NSBitmapImageRep * rep = [frame bitmapImageRepForCachingDisplayInRect:frame.bounds];
	[frame cacheDisplayInRect:frame.bounds toBitmapImageRep:rep];
	NSData * png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
	const std::string path = out_dir + "/" + name + ".png";
	[png writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES];
	std::printf("wrote %s (%.0fx%.0f)\n", path.c_str(), frame.bounds.size.width, frame.bounds.size.height);
}

void quit()
{
	[NSApp stop:nil];
	// stop: takes effect after the next event, so there has to be one.
	[NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
	                                modifierFlags:0 timestamp:0 windowNumber:0 context:nil
	                                      subtype:0 data1:0 data2:0]
	         atStart:NO];
}

void after(double seconds, void (^block)(void))
{
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (seconds * NSEC_PER_SEC)), dispatch_get_main_queue(),
	               block);
}

void run(const char * action)
{
	DB_plugin_action_t * a = find_action(plugin, action);
	a->callback2(a, DDB_ACTION_CTX_SELECTION);
}

//! Waits for the discographies window, drawing the progress on the way.
void watch_disco()
{
	static bool saw_progress = false;
	static int progress_ticks = 0;
	if (!saw_progress)
		if (NSWindow * p = find_window(@"Tango Tagger"))
			if (++progress_ticks > 4)
			{
				saw_progress = true;
				snapshot(p, "progress");
			}
	NSWindow * disco = find_window(@"Tango Tagger - Match Discographies");
	if (disco == nil)
	{
		after(0.05, ^{ watch_disco(); });
		return;
	}
	after(1.0, ^{
		snapshot(find_window(@"Tango Tagger - Match Discographies"), "disco");
		quit();
	});
}

//! Waits for the lyrics window, draws it, closes it, and goes on to the
//! discographies.
void watch_lyrics()
{
	NSWindow * lyrics = find_window(@"Tango Tagger - Lyrics");
	if (lyrics == nil)
	{
		after(0.05, ^{ watch_lyrics(); });
		return;
	}
	after(1.0, ^{
		NSWindow * w = find_window(@"Tango Tagger - Lyrics");
		snapshot(w, "lyrics");
		[w close];
		run("tangotagger_match_discographies");
		after(0.05, ^{ watch_disco(); });
	});
}

//! Until the last window is closed.
void watch_until_closed()
{
	static int idle = 0;
	bool any = false;
	for (NSWindow * w in NSApp.windows) any = any || w.visible;
	idle = any ? 0 : idle + 1;
	if (idle >= 20) quit();
	else after(0.5, ^{ watch_until_closed(); });
}

}   // namespace

int main(int argc, char ** argv)
{
	@autoreleasepool
	{
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		if (argc > 1) out_dir = argv[1];
		quiet = !out_dir.empty();
		init();

		add_preview_tracks();

		std::memset(&fake_cocoaui, 0, sizeof(fake_cocoaui));
		fake_cocoaui.type = DB_PLUGIN_GUI;
		fake_cocoaui.id = "cocoaui";
		ui_plugin = &fake_cocoaui;

		DB_functions_t api = make_api();
		plugin = ddb_tangotagger_load(&api);
		plugin->start();
		plugin->connect();

		run("tangotagger_find_lyrics");
		if (!out_dir.empty()) after(0.05, ^{ watch_lyrics(); });
		else
		{
			run("tangotagger_match_discographies");
			after(0.5, ^{ watch_until_closed(); });
		}

		[NSApp activateIgnoringOtherApps:YES];
		[NSApp run];
		plugin->stop();
	}
	return 0;
}
