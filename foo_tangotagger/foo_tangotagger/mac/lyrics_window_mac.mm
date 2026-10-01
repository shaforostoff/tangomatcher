// The results window, macOS side. The Windows counterpart is lyrics_dialog.cpp,
// built on a dialog template and a list view control.
//
// What the two share is lyrics_rows.h, which settles the rows, which of them
// start checked, what the preview says and what reaches the files - so only
// the drawing is written twice.

#import <Cocoa/Cocoa.h>

#include <SDK/foobar2000.h>

#include <memory>
#include <vector>

#include "../lyrics_rows.h"
#include "../tangotagger_ui.h"

namespace
{
	NSString * str(const char * utf8)
	{
		if (utf8 == nullptr) return @"";
		NSString * s = [NSString stringWithUTF8String:utf8];
		return s != nil ? s : @"";
	}

	NSString * const col_check    = @"check";
	NSString * const col_title    = @"title";
	NSString * const col_artist   = @"artist";
	NSString * const col_song     = @"song";
	NSString * const col_match    = @"match";
	NSString * const col_existing = @"existing";
}


@interface fooTangoTaggerLyricsWindow : NSWindowController
                                       <NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate>
- (instancetype)initWithMatches:(std::shared_ptr<lyrics_matches>)matches;
@end


@implementation fooTangoTaggerLyricsWindow
{
	std::shared_ptr<lyrics_matches> _matches;
	NSTableView * _table;
	NSTextView  * _preview;
	NSButton    * _write;
}

//! Open windows, so that one stays alive after the function that made it has
//! returned. Released again in windowWillClose:.
static NSMutableArray<fooTangoTaggerLyricsWindow *> * g_openWindows = nil;

// --- construction ----------------------------------------------------------

- (instancetype)initWithMatches:(std::shared_ptr<lyrics_matches>)matches
{
	NSWindow * window =
		[[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 820, 600)
		                            styleMask:NSWindowStyleMaskTitled |
		                                      NSWindowStyleMaskClosable |
		                                      NSWindowStyleMaskResizable
		                              backing:NSBackingStoreBuffered
		                                defer:NO];
	window.title = @"Tango Tagger - Lyrics";
	window.releasedWhenClosed = NO;   // the array above owns it
	window.minSize = NSMakeSize(560, 400);
	[window center];

	self = [super initWithWindow:window];
	if (self == nil) return nil;

	_matches = matches;
	[self buildContent];
	window.delegate = self;
	return self;
}

- (NSTableColumn *)addColumn:(NSString *)identifier title:(NSString *)title width:(CGFloat)width
{
	NSTableColumn * column = [[NSTableColumn alloc] initWithIdentifier:identifier];
	column.title = title;
	column.width = width;
	column.minWidth = MIN(width, 40);
	column.resizingMask = NSTableColumnUserResizingMask | NSTableColumnAutoresizingMask;
	[_table addTableColumn:column];
	return column;
}

- (void)buildContent
{
	NSView * root = self.window.contentView;

	_table = [[NSTableView alloc] initWithFrame:NSZeroRect];
	_table.usesAlternatingRowBackgroundColors = YES;
	_table.allowsMultipleSelection = NO;
	_table.dataSource = self;
	_table.delegate = self;
	_table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;

	NSTableColumn * check = [self addColumn:col_check title:@"" width:24];
	check.resizingMask = NSTableColumnNoResizing;
	[self addColumn:col_title title:@"Track title" width:220];
	[self addColumn:col_artist title:@"Artist" width:140];
	[self addColumn:col_song title:@"Lyrics of" width:200];
	[self addColumn:col_match title:@"Match" width:80];
	[self addColumn:col_existing title:@"Existing lyrics" width:100];

	NSScrollView * tableScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
	tableScroll.documentView = _table;
	tableScroll.hasVerticalScroller = YES;
	tableScroll.autohidesScrollers = YES;
	tableScroll.borderType = NSBezelBorder;
	tableScroll.translatesAutoresizingMaskIntoConstraints = NO;

	NSScrollView * previewScroll = [NSTextView scrollableTextView];
	previewScroll.borderType = NSBezelBorder;
	previewScroll.translatesAutoresizingMaskIntoConstraints = NO;
	_preview = (NSTextView *) previewScroll.documentView;
	_preview.editable = NO;
	_preview.selectable = YES;
	_preview.font = [NSFont systemFontOfSize:NSFont.systemFontSize];
	_preview.textContainerInset = NSMakeSize(4, 4);

	NSTextField * status = [NSTextField wrappingLabelWithString:str(lyrics_status_text(*_matches).get_ptr())];
	status.textColor = NSColor.secondaryLabelColor;
	status.translatesAutoresizingMaskIntoConstraints = NO;

	NSButton * checkAll  = [NSButton buttonWithTitle:@"Check All"  target:self action:@selector(onCheckAll:)];
	NSButton * checkNone = [NSButton buttonWithTitle:@"Check None" target:self action:@selector(onCheckNone:)];
	NSButton * links = [NSButton checkboxWithTitle:@"Add links to translations" target:self
	                                        action:@selector(onTranslationLinks:)];
	links.state = translation_links_enabled() ? NSControlStateValueOn : NSControlStateValueOff;
	NSButton * cancel    = [NSButton buttonWithTitle:@"Cancel"     target:self action:@selector(onCancel:)];
	cancel.keyEquivalent = @"\033";   // Escape
	_write = [NSButton buttonWithTitle:@"Write" target:self action:@selector(onWrite:)];
	_write.keyEquivalent = @"\r";     // the default button

	NSView * spacer = [[NSView alloc] initWithFrame:NSZeroRect];
	[spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow
	                   forOrientation:NSLayoutConstraintOrientationHorizontal];
	NSStackView * buttonRow = [NSStackView stackViewWithViews:@[ checkAll, checkNone, links, spacer, cancel, _write ]];
	buttonRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
	buttonRow.spacing = 8;
	buttonRow.translatesAutoresizingMaskIntoConstraints = NO;

	[root addSubview:tableScroll];
	[root addSubview:previewScroll];
	[root addSubview:status];
	[root addSubview:buttonRow];

	[NSLayoutConstraint activateConstraints:@[
		[tableScroll.topAnchor      constraintEqualToAnchor:root.topAnchor      constant:20],
		[tableScroll.leadingAnchor  constraintEqualToAnchor:root.leadingAnchor  constant:20],
		[tableScroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-20],
		[tableScroll.heightAnchor   constraintEqualToAnchor:root.heightAnchor multiplier:0.45],

		[previewScroll.topAnchor      constraintEqualToAnchor:tableScroll.bottomAnchor constant:8],
		[previewScroll.leadingAnchor  constraintEqualToAnchor:root.leadingAnchor  constant:20],
		[previewScroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-20],

		[status.topAnchor      constraintEqualToAnchor:previewScroll.bottomAnchor constant:8],
		[status.leadingAnchor  constraintEqualToAnchor:root.leadingAnchor  constant:20],
		[status.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-20],

		[buttonRow.topAnchor      constraintEqualToAnchor:status.bottomAnchor constant:12],
		[buttonRow.leadingAnchor  constraintEqualToAnchor:root.leadingAnchor  constant:20],
		[buttonRow.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-20],
		[buttonRow.bottomAnchor   constraintEqualToAnchor:root.bottomAnchor   constant:-20],
	]];

	[self relabelWriteButton];
	if (!_matches->rows.empty())
	{
		[_table selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
		[self showPreviewForRow:0];
	}
}

// --- the cells -------------------------------------------------------------

- (NSString *)textForRow:(NSInteger)row column:(NSString *)identifier
{
	const std::vector<lyrics_row> & rows = _matches->rows;
	if (row < 0 || (std::size_t) row >= rows.size()) return @"";
	const lyrics_row & r = rows[(std::size_t) row];

	if ([identifier isEqualToString:col_title])
	{
		// A title's further candidates are indented under its first, so the
		// group reads as one choice.
		NSString * title = str(r.title.get_ptr());
		return r.version > 1 ? [@"      " stringByAppendingString:title] : title;
	}
	if ([identifier isEqualToString:col_artist])   return str(r.artist.get_ptr());
	if ([identifier isEqualToString:col_match])    return str(match_label(r).get_ptr());
	if (!r.matched()) return @"";
	if ([identifier isEqualToString:col_song])     return str(row_song(r).name.c_str());
	if ([identifier isEqualToString:col_existing]) return str(existing_label(r.existing()));
	return @"";
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
	return (NSInteger) _matches->rows.size();
}

- (NSView *)tableView:(NSTableView *)tableView
   viewForTableColumn:(NSTableColumn *)tableColumn
                  row:(NSInteger)row
{
	const bool matched = _matches->rows[(std::size_t) row].matched();
	if ([tableColumn.identifier isEqualToString:col_check])
	{
		// A track nothing matched has nothing to write: no checkbox.
		if (!matched) return nil;
		NSButton * box = [tableView makeViewWithIdentifier:col_check owner:self];
		if (box == nil)
		{
			box = [NSButton checkboxWithTitle:@"" target:self action:@selector(onToggle:)];
			box.identifier = col_check;
		}
		box.tag = row;
		box.state = _matches->rows[(std::size_t) row].checked ? NSControlStateValueOn : NSControlStateValueOff;
		return box;
	}

	NSTextField * cell = [tableView makeViewWithIdentifier:tableColumn.identifier owner:self];
	if (cell == nil)
	{
		cell = [NSTextField labelWithString:@""];
		cell.identifier = tableColumn.identifier;
		cell.lineBreakMode = NSLineBreakByTruncatingTail;
	}
	cell.stringValue = [self textForRow:row column:tableColumn.identifier];
	cell.textColor = matched ? NSColor.labelColor : NSColor.secondaryLabelColor;
	return cell;
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
	[self showPreviewForRow:_table.selectedRow];
}

- (void)showPreviewForRow:(NSInteger)row
{
	NSString * text = @"";
	if (row >= 0 && (std::size_t) row < _matches->rows.size())
		text = str(lyrics_preview_text(_matches->rows[(std::size_t) row], "\n").get_ptr());
	_preview.string = text;
	[_preview scrollRangeToVisible:NSMakeRange(0, 0)];
}

// --- the checkboxes and buttons --------------------------------------------

- (void)reloadChecks
{
	[_table reloadDataForRowIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, _matches->rows.size())]
	                  columnIndexes:[NSIndexSet indexSetWithIndex:[_table columnWithIdentifier:col_check]]];
	[self relabelWriteButton];
}

- (IBAction)onToggle:(NSButton *)sender
{
	// Checking one song of a title unchecks the others.
	set_row_checked(_matches->rows, (std::size_t) sender.tag, sender.state == NSControlStateValueOn);
	[self reloadChecks];
}

//! Every track gets a song: the one already checked in its group, or else
//! the group's first, which is the one its credits favour.
- (IBAction)onCheckAll:(id)sender
{
	std::vector<lyrics_row> & rows = _matches->rows;
	for (std::size_t i = 0; i < rows.size(); i++)
	{
		if (rows[i].version != 1) continue;
		bool any = false;
		for (std::size_t j = i; j < rows.size() && rows[j].group == rows[i].group; j++)
			any = any || rows[j].checked;
		if (!any) set_row_checked(rows, i, true);
	}
	[self reloadChecks];
}

- (IBAction)onCheckNone:(id)sender
{
	for (lyrics_row & r : _matches->rows) r.checked = false;
	[self reloadChecks];
}

- (void)relabelWriteButton
{
	const std::size_t n = count_checked(_matches->rows);
	pfc::string_formatter label;
	if (n == 1) label << "Write 1 File";
	else label << "Write " << n << " Files";
	_write.title = str(label.get_ptr());
	_write.enabled = n > 0;
}

- (IBAction)onWrite:(id)sender
{
	write_checked_lyrics(_matches->rows);
	[self close];
}

//! What is written changes, and with it what the files already having it
//! means: the Existing lyrics column and the preview follow.
- (IBAction)onTranslationLinks:(NSButton *)sender
{
	set_translation_links_enabled(sender.state == NSControlStateValueOn);
	[_table reloadData];
	if (_table.selectedRow >= 0) [self showPreviewForRow:_table.selectedRow];
	// The links are at the end: show the end, where the change is.
	[_preview scrollRangeToVisible:NSMakeRange(_preview.string.length, 0)];
}

- (IBAction)onCancel:(id)sender
{
	[self close];
}

- (void)windowWillClose:(NSNotification *)notification
{
	[g_openWindows removeObject:self];
}

@end


/***** tangotagger_ui.h *****/

void show_lyrics_matches(lyrics_matches && matches)
{
	auto shared = std::make_shared<lyrics_matches>(std::move(matches));

	if (g_openWindows == nil) g_openWindows = [NSMutableArray new];

	fooTangoTaggerLyricsWindow * window = [[fooTangoTaggerLyricsWindow alloc] initWithMatches:shared];
	[g_openWindows addObject:window];
	[window showWindow:nil];
	[window.window makeKeyAndOrderFront:nil];
}
