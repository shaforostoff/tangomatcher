// The lyrics panel, macOS side: a layout element, "tango-lyrics", showing the
// lyrics of the selected track. The Windows counterpart is lyrics_panel.cpp.
//
// What the two share is lyrics_panel_content.h, which decides what the panel
// says and where its links are. Here the text goes into a read-only
// NSTextView, which draws the links, gives them the pointing hand and opens
// them in the default browser by itself.
//
// The text size and alignment are settings of the component, not of each
// panel: a macOS layout element has no configuration of its own to keep them
// in.

#import <Cocoa/Cocoa.h>

#include <SDK/foobar2000.h>

#include <algorithm>
#include <cstring>
#include <memory>

#include "../guid.h"
#include "../lyrics_panel_content.h"

namespace
{
	const int min_font_size = 6;    // points
	const int max_font_size = 72;

	cfg_int cfg_font_size(guid_cfg_panel_font_size, 0);     // 0: the system font size
	cfg_bool cfg_centered(guid_cfg_panel_centered, true);

	NSString * str(const std::string & utf8)
	{
		NSString * s = [[NSString alloc] initWithBytes:utf8.data() length:utf8.size() encoding:NSUTF8StringEncoding];
		return s != nil ? s : @"";
	}

	CGFloat font_size()
	{
		const int64_t size = cfg_font_size.get();
		return size > 0 ? static_cast<CGFloat>(size) : NSFont.systemFontSize;
	}
}

@class fooTangoLyricsPanel;

namespace
{
	//! Selection changes and tag edits, passed on to the panel.
	class panel_callbacks : public ui_selection_callback_impl_base, public metadb_io_callback_dynamic_impl_base
	{
	public:
		explicit panel_callbacks(fooTangoLyricsPanel * owner) : m_owner(owner) {}

		void on_selection_changed(metadb_handle_list_cref selection) override;
		void on_changed_sorted(metadb_handle_list_cref items, bool) override;

	private:
		__weak fooTangoLyricsPanel * m_owner;
	};
}


//! The text view, for the keys and gestures that change the text size.
@interface fooTangoLyricsTextView : NSTextView
@property (nonatomic, weak) fooTangoLyricsPanel * panel;
@end


@interface fooTangoLyricsPanel : NSViewController <NSTextViewDelegate>
- (void)showTrack:(metadb_handle_ptr)track;
- (void)trackChanged:(metadb_handle_list_cref)items;
- (void)zoom:(int)steps;
- (IBAction)largerText:(id)sender;
- (IBAction)smallerText:(id)sender;
- (IBAction)defaultTextSize:(id)sender;
- (IBAction)toggleCentered:(id)sender;
- (IBAction)copyLyrics:(id)sender;
@end


@implementation fooTangoLyricsTextView
{
	CGFloat _magnification;
}

- (BOOL)performKeyEquivalent:(NSEvent *)event
{
	// Cmd+Plus, Cmd+Minus and Cmd+0 while the panel has the focus.
	const NSEventModifierFlags mods = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
	if (self.window.firstResponder == self && (mods & ~NSEventModifierFlagShift) == NSEventModifierFlagCommand)
	{
		NSString * key = event.charactersIgnoringModifiers;
		if ([key isEqualToString:@"+"] || [key isEqualToString:@"="]) { [self.panel zoom:1]; return YES; }
		if ([key isEqualToString:@"-"]) { [self.panel zoom:-1]; return YES; }
		if ([key isEqualToString:@"0"]) { [self.panel defaultTextSize:nil]; return YES; }
	}
	return [super performKeyEquivalent:event];
}

- (void)magnifyWithEvent:(NSEvent *)event
{
	// Pinch: a step of text size per tenth of magnification.
	_magnification += event.magnification;
	const int steps = static_cast<int>(_magnification * 10);
	if (steps != 0)
	{
		_magnification -= steps / 10.0;
		[self.panel zoom:steps];
	}
	if (event.phase == NSEventPhaseEnded || event.phase == NSEventPhaseCancelled) _magnification = 0;
}

@end


@implementation fooTangoLyricsPanel
{
	std::unique_ptr<panel_callbacks> _callbacks;
	metadb_handle_ptr _track;
	lyrics_panel_content _content;
	fooTangoLyricsTextView * _text;
}

- (void)loadView
{
	NSScrollView * scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 300, 400)];
	scroll.hasVerticalScroller = YES;
	scroll.autohidesScrollers = YES;
	scroll.borderType = NSNoBorder;
	scroll.drawsBackground = YES;

	_text = [[fooTangoLyricsTextView alloc] initWithFrame:scroll.contentView.bounds];
	_text.panel = self;
	_text.delegate = self;
	_text.editable = NO;
	_text.selectable = YES;
	_text.richText = YES;
	_text.drawsBackground = YES;
	_text.backgroundColor = NSColor.textBackgroundColor;
	_text.textContainerInset = NSMakeSize(12, 12);
	_text.autoresizingMask = NSViewWidthSizable;
	_text.verticallyResizable = YES;
	_text.horizontallyResizable = NO;
	_text.textContainer.widthTracksTextView = YES;
	_text.displaysLinkToolTips = YES;
	_text.linkTextAttributes = @{
		NSForegroundColorAttributeName: NSColor.linkColor,
		NSUnderlineStyleAttributeName: @(NSUnderlineStyleSingle),
		NSCursorAttributeName: NSCursor.pointingHandCursor,
	};
	scroll.documentView = _text;
	self.view = scroll;

	_callbacks = std::make_unique<panel_callbacks>(self);
	metadb_handle_list selection;
	ui_selection_manager::get()->get_selection(selection);
	[self showTrack:selection.get_count() > 0 ? selection[0] : metadb_handle_ptr()];
}

- (void)dealloc
{
	_callbacks.reset();
}

// --- content ---------------------------------------------------------------

- (void)showTrack:(metadb_handle_ptr)track
{
	const bool same = track == _track && !_content.lines.empty();
	_track = track;
	_content = lyrics_panel_content_for(track);
	[self render];
	if (!same) [_text scrollPoint:NSZeroPoint];
}

- (void)trackChanged:(metadb_handle_list_cref)items
{
	// Tags edited - by Find lyrics..., say: the panel follows.
	if (_track.is_valid() && items.find_item(_track) != SIZE_MAX) [self showTrack:_track];
}

- (void)render
{
	const CGFloat size = font_size();
	NSFont * text_font = [NSFont systemFontOfSize:size];
	NSFont * small_font = [NSFont systemFontOfSize:size * 0.9];
	NSFont * heading_font = [NSFont boldSystemFontOfSize:size * 1.3];

	NSMutableParagraphStyle * paragraph = [NSMutableParagraphStyle new];
	paragraph.alignment = cfg_centered.get() ? NSTextAlignmentCenter : NSTextAlignmentLeft;

	NSMutableAttributedString * out = [NSMutableAttributedString new];
	for (const lyrics_panel_line & line : _content.lines)
	{
		NSFont * font = text_font;
		NSColor * color = NSColor.labelColor;
		switch (line.style)
		{
		case lyrics_panel_style::heading: font = heading_font; break;
		case lyrics_panel_style::credits:
		case lyrics_panel_style::note:    font = small_font; color = NSColor.secondaryLabelColor; break;
		default: break;
		}
		NSDictionary * attributes = @{
			NSFontAttributeName: font,
			NSForegroundColorAttributeName: color,
			NSParagraphStyleAttributeName: paragraph,
		};
		const NSUInteger start = out.length;
		[out appendAttributedString:[[NSAttributedString alloc] initWithString:[str(line.text) stringByAppendingString:@"\n"]
		                                                            attributes:attributes]];
		// The links' byte offsets, as UTF-16 ones.
		for (const tangotagger::text_link & l : line.links)
		{
			NSURL * url = [NSURL URLWithString:str(l.url)];
			if (url == nil) continue;
			const NSUInteger begin = str(line.text.substr(0, l.begin)).length;
			const NSUInteger length = str(line.text.substr(l.begin, l.end - l.begin)).length;
			[out addAttribute:NSLinkAttributeName value:url range:NSMakeRange(start + begin, length)];
		}
	}
	[_text.textStorage setAttributedString:out];
}

// --- text size and alignment -----------------------------------------------

- (void)zoom:(int)steps
{
	const int current = static_cast<int>(font_size() + 0.5);
	const int size = std::max(min_font_size, std::min(current + steps, max_font_size));
	cfg_font_size = size;
	[self render];
}

- (IBAction)largerText:(id)sender { [self zoom:1]; }
- (IBAction)smallerText:(id)sender { [self zoom:-1]; }

- (IBAction)defaultTextSize:(id)sender
{
	cfg_font_size = 0;
	[self render];
}

- (IBAction)toggleCentered:(id)sender
{
	cfg_centered = !cfg_centered.get();
	[self render];
}

- (IBAction)copyLyrics:(id)sender
{
	if (_content.copy_text.empty()) return;
	NSPasteboard * pasteboard = NSPasteboard.generalPasteboard;
	[pasteboard clearContents];
	[pasteboard setString:str(_content.copy_text) forType:NSPasteboardTypeString];
}

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
	if (item.action == @selector(copyLyrics:)) return !_content.copy_text.empty();
	if (item.action == @selector(defaultTextSize:)) return cfg_font_size.get() != 0;
	if (item.action == @selector(toggleCentered:)) item.state = cfg_centered.get() ? NSControlStateValueOn : NSControlStateValueOff;
	return YES;
}

// NSTextViewDelegate: the text view's own menu - Copy, Look Up, and for a
// link Open Link and Copy Link - with the panel's commands after it.
- (NSMenu *)textView:(NSTextView *)view menu:(NSMenu *)menu forEvent:(NSEvent *)event atIndex:(NSUInteger)charIndex
{
	[menu addItem:NSMenuItem.separatorItem];
	auto add = [&](NSString * title, SEL action)
	{
		NSMenuItem * item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""];
		item.target = self;
		[menu addItem:item];
	};
	add(@"Copy Lyrics", @selector(copyLyrics:));
	[menu addItem:NSMenuItem.separatorItem];
	add(@"Larger Text", @selector(largerText:));
	add(@"Smaller Text", @selector(smallerText:));
	add(@"Default Text Size", @selector(defaultTextSize:));
	[menu addItem:NSMenuItem.separatorItem];
	add(@"Centered", @selector(toggleCentered:));
	return menu;
}

@end


namespace
{
	void panel_callbacks::on_selection_changed(metadb_handle_list_cref selection)
	{
		[m_owner showTrack:selection.get_count() > 0 ? selection[0] : metadb_handle_ptr()];
	}

	void panel_callbacks::on_changed_sorted(metadb_handle_list_cref items, bool)
	{
		[m_owner trackChanged:items];
	}

	class lyrics_panel_element : public ui_element_mac
	{
	public:
		service_ptr instantiate(service_ptr arg) override
		{
			@autoreleasepool
			{
				return fb2k::wrapNSObject([fooTangoLyricsPanel new]);
			}
		}
		bool match_name(const char * name) override
		{
			return strcmp(name, "tango-lyrics") == 0 || strcmp(name, "Lyrics") == 0;
		}
		fb2k::stringRef get_name() override { return fb2k::makeString("Lyrics"); }
		GUID get_guid() override { return guid_lyrics_panel_mac; }
	};

	FB2K_SERVICE_FACTORY(lyrics_panel_element);
}
