// The Match discographies window, macOS side. The Windows counterpart is
// disco_dialog.cpp; what the two share is disco_rows.h, so only the drawing
// is written twice. Built like lyrics_window_mac.mm.

#import <Cocoa/Cocoa.h>

#include <SDK/foobar2000.h>

#include <memory>
#include <vector>

#include "../disco_rows.h"
#include "../tangotagger_ui.h"

namespace
{
	NSString * str(const char * utf8)
	{
		if (utf8 == nullptr) return @"";
		NSString * s = [NSString stringWithUTF8String:utf8];
		return s != nil ? s : @"";
	}

	NSString * const col_check     = @"check";
	NSString * const col_title     = @"title";
	NSString * const col_artist    = @"artist";
	NSString * const col_recording = @"recording";
	NSString * const col_orchestra = @"orchestra";
	NSString * const col_vocal     = @"vocal";
	NSString * const col_date      = @"date";
	NSString * const col_match     = @"match";

	//! The field checkboxes, by tag.
	bool tangotagger::tag_options::* const field_members[] = {
		&tangotagger::tag_options::title,
		&tangotagger::tag_options::artist,
		&tangotagger::tag_options::album_artist,
		&tangotagger::tag_options::date,
		&tangotagger::tag_options::genre,
	};
	NSString * const field_titles[] = { @"Title", @"Artist", @"Album artist", @"Date", @"Genre" };
}


@interface fooTangoTaggerDiscoWindow : NSWindowController
                                      <NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate>
- (instancetype)initWithMatches:(std::shared_ptr<disco_matches>)matches;
@end


@implementation fooTangoTaggerDiscoWindow
{
	std::shared_ptr<disco_matches> _matches;
	//! Each row's block of one track, counted from the top: the shading.
	std::vector<std::size_t> _blocks;
	NSTableView   * _table;
	NSTextView    * _preview;
	NSPopUpButton * _scheme;
	NSButton      * _write;
}

//! Open windows, kept alive until windowWillClose:.
static NSMutableArray<fooTangoTaggerDiscoWindow *> * g_openDiscoWindows = nil;

// --- construction ----------------------------------------------------------

- (instancetype)initWithMatches:(std::shared_ptr<disco_matches>)matches
{
	NSWindow * window =
		[[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1000, 640)
		                            styleMask:NSWindowStyleMaskTitled |
		                                      NSWindowStyleMaskClosable |
		                                      NSWindowStyleMaskResizable
		                              backing:NSBackingStoreBuffered
		                                defer:NO];
	window.title = @"Tango Tagger - Match Discographies";
	window.releasedWhenClosed = NO;
	window.minSize = NSMakeSize(700, 440);
	[window center];

	self = [super initWithWindow:window];
	if (self == nil) return nil;

	_matches = matches;
	for (std::size_t i = 0; i < _matches->rows.size(); i++)
		_blocks.push_back(i == 0 ? 0 : _blocks.back() + (_matches->rows[i].group != _matches->rows[i - 1].group ? 1 : 0));
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

//! A column as wide as the widest of the samples, label padding included
//! (the spacing between columns lies outside it), that keeps its width
//! when the window is resized: the free space goes to the name columns.
- (NSTableColumn *)addColumn:(NSString *)identifier title:(NSString *)title fitting:(NSArray<NSString *> *)samples
{
	CGFloat width = 0;
	for (NSString * s in [samples arrayByAddingObject:title])
		width = MAX(width, [NSTextField labelWithString:s].fittingSize.width);
	width = ceil(width);
	NSTableColumn * column = [self addColumn:identifier title:title width:width];
	column.minWidth = width;
	column.resizingMask = NSTableColumnUserResizingMask;
	return column;
}

- (void)buildContent
{
	NSView * root = self.window.contentView;

	_table = [[NSTableView alloc] initWithFrame:NSZeroRect];
	// Shaded by track, not by row: see tableView:didAddRowView:forRow:.
	_table.usesAlternatingRowBackgroundColors = NO;
	_table.allowsMultipleSelection = NO;
	_table.dataSource = self;
	_table.delegate = self;
	_table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;

	NSTableColumn * check = [self addColumn:col_check title:@"" width:36];
	check.resizingMask = NSTableColumnNoResizing;
	[self addColumn:col_title title:@"Track title" width:170];
	[self addColumn:col_artist title:@"Artist" width:130];
	[self addColumn:col_recording title:@"Recording" width:170];
	[self addColumn:col_orchestra title:@"Orchestra" width:140];
	[self addColumn:col_vocal title:@"Singer" width:130];
	// The dates this window shows, not a made-up widest one: the system
	// font's digits differ in width, so 8888-88-88 is far wider than 1942-07-21.
	NSMutableArray<NSString *> * dates = [NSMutableArray array];
	for (std::size_t i = 0; i < _matches->rows.size(); i++)
		[dates addObject:[self textForRow:(NSInteger) i column:col_date]];
	[self addColumn:col_date title:@"Date" fitting:dates];
	[self addColumn:col_match title:@"Match" fitting:@[ @"confident", @"by sound", @"no match" ]];

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

	NSTextField * status = [NSTextField wrappingLabelWithString:str(disco_status_text(*_matches).get_ptr())];
	status.textColor = NSColor.secondaryLabelColor;
	status.translatesAutoresizingMaskIntoConstraints = NO;

	// The artist scheme and the fields to write.
	const tangotagger::tag_options options = disco_tag_options();
	_scheme = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
	for (int s = 0; s < static_cast<int>(tangotagger::artist_scheme::count); s++)
	{
		const auto a = static_cast<tangotagger::artist_scheme>(s);
		NSString * label = [NSString stringWithFormat:@"%@  —  %@", str(tangotagger::artist_scheme_name(a)),
		                                              str(tangotagger::artist_scheme_example(a).c_str())];
		[_scheme addItemWithTitle:label];
	}
	[_scheme selectItemAtIndex:static_cast<NSInteger>(options.scheme)];
	_scheme.target = self;
	_scheme.action = @selector(onOptions:);
	NSMutableArray<NSView *> * optionViews = [NSMutableArray arrayWithObjects:
		[NSTextField labelWithString:@"Artist scheme:"], _scheme, [NSTextField labelWithString:@"   Write:"], nil];
	for (std::size_t f = 0; f < sizeof field_members / sizeof field_members[0]; f++)
	{
		NSButton * box = [NSButton checkboxWithTitle:field_titles[f] target:self action:@selector(onOptions:)];
		box.tag = (NSInteger) f;
		box.state = options.*(field_members[f]) ? NSControlStateValueOn : NSControlStateValueOff;
		[optionViews addObject:box];
	}
	NSStackView * optionRow = [NSStackView stackViewWithViews:optionViews];
	optionRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
	optionRow.spacing = 8;
	optionRow.translatesAutoresizingMaskIntoConstraints = NO;

	NSButton * checkAll  = [NSButton buttonWithTitle:@"Check All"  target:self action:@selector(onCheckAll:)];
	NSButton * checkNone = [NSButton buttonWithTitle:@"Check None" target:self action:@selector(onCheckNone:)];
	NSButton * cancel    = [NSButton buttonWithTitle:@"Cancel"     target:self action:@selector(onCancel:)];
	cancel.keyEquivalent = @"\033";
	_write = [NSButton buttonWithTitle:@"Write" target:self action:@selector(onWrite:)];
	_write.keyEquivalent = @"\r";

	NSView * spacer = [[NSView alloc] initWithFrame:NSZeroRect];
	[spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow
	                   forOrientation:NSLayoutConstraintOrientationHorizontal];
	NSStackView * buttonRow = [NSStackView stackViewWithViews:@[ checkAll, checkNone, spacer, cancel, _write ]];
	buttonRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
	buttonRow.spacing = 8;
	buttonRow.translatesAutoresizingMaskIntoConstraints = NO;

	[root addSubview:tableScroll];
	[root addSubview:previewScroll];
	[root addSubview:status];
	[root addSubview:optionRow];
	[root addSubview:buttonRow];

	[NSLayoutConstraint activateConstraints:@[
		[tableScroll.topAnchor      constraintEqualToAnchor:root.topAnchor      constant:20],
		[tableScroll.leadingAnchor  constraintEqualToAnchor:root.leadingAnchor  constant:20],
		[tableScroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-20],
		[tableScroll.heightAnchor   constraintEqualToAnchor:root.heightAnchor multiplier:0.5],

		[previewScroll.topAnchor      constraintEqualToAnchor:tableScroll.bottomAnchor constant:8],
		[previewScroll.leadingAnchor  constraintEqualToAnchor:root.leadingAnchor  constant:20],
		[previewScroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-20],

		[status.topAnchor      constraintEqualToAnchor:previewScroll.bottomAnchor constant:8],
		[status.leadingAnchor  constraintEqualToAnchor:root.leadingAnchor  constant:20],
		[status.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-20],

		[optionRow.topAnchor      constraintEqualToAnchor:status.bottomAnchor constant:12],
		[optionRow.leadingAnchor  constraintEqualToAnchor:root.leadingAnchor  constant:20],
		[optionRow.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-20],

		[buttonRow.topAnchor      constraintEqualToAnchor:optionRow.bottomAnchor constant:12],
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
	const std::vector<disco_row> & rows = _matches->rows;
	if (row < 0 || (std::size_t) row >= rows.size()) return @"";
	const disco_row & r = rows[(std::size_t) row];

	// A track's further candidates sit under its first, without repeating it.
	if ([identifier isEqualToString:col_title])  return r.version > 1 ? @"" : str(r.title.get_ptr());
	if ([identifier isEqualToString:col_artist]) return r.version > 1 ? @"" : str(r.artist.get_ptr());
	if ([identifier isEqualToString:col_match])  return str(disco_match_label(r).get_ptr());
	if (!r.matched()) return @"";
	const tangotagger::recording & rec = row_recording(r);
	if ([identifier isEqualToString:col_recording]) return str(tangotagger::main_title(rec.name).c_str());
	if ([identifier isEqualToString:col_orchestra]) return str(row_orchestra(r).c_str());
	if ([identifier isEqualToString:col_vocal])     return str(rec.vocal.c_str());
	if ([identifier isEqualToString:col_date])      return str(rec.date.c_str());
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
		if (!matched) return nil;
		NSButton * box = [tableView makeViewWithIdentifier:col_check owner:self];
		if (box == nil)
		{
			box = [NSButton checkboxWithTitle:@"" target:self action:@selector(onToggle:)];
			box.identifier = col_check;
		}
		box.tag = row;
		// The option number, when the track has several to choose from.
		const auto & r = _matches->rows[(std::size_t) row];
		box.title = r.versions > 1 ? [NSString stringWithFormat:@" %d", r.version] : @"";
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

//! Every row of one track in the same shade, the next track's in the other,
//! so a track's candidates read as one block.
- (void)tableView:(NSTableView *)tableView didAddRowView:(NSTableRowView *)rowView forRow:(NSInteger)row
{
	if (row < 0 || (std::size_t) row >= _matches->rows.size()) return;
	NSArray<NSColor *> * shades = NSColor.alternatingContentBackgroundColors;
	rowView.backgroundColor = shades[_blocks[(std::size_t) row] % shades.count];
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
	[self showPreviewForRow:_table.selectedRow];
}

- (void)showPreviewForRow:(NSInteger)row
{
	NSString * text = @"";
	if (row >= 0 && (std::size_t) row < _matches->rows.size())
		text = str(disco_preview_text(_matches->rows[(std::size_t) row], "\n").get_ptr());
	_preview.string = text;
	[_preview scrollRangeToVisible:NSMakeRange(0, 0)];
}

// --- the controls ----------------------------------------------------------

- (void)reloadChecks
{
	[_table reloadDataForRowIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, _matches->rows.size())]
	                  columnIndexes:[NSIndexSet indexSetWithIndex:[_table columnWithIdentifier:col_check]]];
	[self relabelWriteButton];
}

- (IBAction)onToggle:(NSButton *)sender
{
	set_disco_row_checked(_matches->rows, (std::size_t) sender.tag, sender.state == NSControlStateValueOn);
	[self reloadChecks];
}

- (IBAction)onCheckAll:(id)sender
{
	check_all_disco_rows(_matches->rows);
	[self reloadChecks];
}

- (IBAction)onCheckNone:(id)sender
{
	for (disco_row & r : _matches->rows) r.checked = false;
	[self reloadChecks];
}

//! The scheme or a field checkbox changed: kept, and the preview follows.
- (IBAction)onOptions:(id)sender
{
	tangotagger::tag_options options = disco_tag_options();
	if (_scheme.indexOfSelectedItem >= 0)
		options.scheme = static_cast<tangotagger::artist_scheme>(_scheme.indexOfSelectedItem);
	if ([sender isKindOfClass:[NSButton class]])
	{
		NSButton * box = (NSButton *) sender;
		if (box.tag >= 0 && (std::size_t) box.tag < sizeof field_members / sizeof field_members[0])
			options.*(field_members[box.tag]) = box.state == NSControlStateValueOn;
	}
	set_disco_tag_options(options);
	if (_table.selectedRow >= 0) [self showPreviewForRow:_table.selectedRow];
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
	write_checked_disco_tags(_matches->rows);
	[self close];
}

- (IBAction)onCancel:(id)sender
{
	[self close];
}

- (void)windowWillClose:(NSNotification *)notification
{
	[g_openDiscoWindows removeObject:self];
}

@end


/***** tangotagger_ui.h *****/

void show_disco_matches(disco_matches && matches)
{
	auto shared = std::make_shared<disco_matches>(std::move(matches));

	if (g_openDiscoWindows == nil) g_openDiscoWindows = [NSMutableArray new];

	fooTangoTaggerDiscoWindow * window = [[fooTangoTaggerDiscoWindow alloc] initWithMatches:shared];
	[g_openDiscoWindows addObject:window];
	[window showWindow:nil];
	[window.window makeKeyAndOrderFront:nil];
}
