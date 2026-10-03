// The plugin's windows in Cocoa, for DeaDBeeF on macOS. See tt_ui.h for what
// they are and tt_review.h for everything they show; this file lays them out
// and passes clicks back, as gtk/ui_gtk.cpp does in GTK 3, and decides nothing
// that one does not. The results window is foo_tangotagger's two macOS windows
// (mac/lyrics_window_mac.mm, mac/disco_window_mac.mm) made one, drawing
// whichever review it is given.
//
// Everything here runs on the main thread, which is AppKit's. The entry
// points may be called from any thread, and hand their work over with
// dispatch_async.
//
// Built with ARC. The classes carry a DdbTangoTagger prefix: Objective-C class
// names share one namespace across every library in the process, whatever the
// linker exports - foo_tangotagger's own classes are fooTangoTagger, and
// another plugin's "ReviewWindow" would be taken for ours.

#import <Cocoa/Cocoa.h>

#include "tt_ui.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{

//! DeaDBeeF's Cocoa interface, which the windows belong in.
const char * const cocoaui_id = "cocoaui";

std::atomic<bool> ready(false);

void on_main_thread(std::function<void()> fn)
{
	dispatch_async(dispatch_get_main_queue(), ^{
		if (ready.load()) fn();
	});
}

//! A tag read off a file is text that arrives from outside, and
//! stringWithUTF8String: answers nil for one that is not UTF-8. An empty cell
//! is better than a nil where AppKit wants a string.
NSString * ns(const std::string & s)
{
	NSString * r = [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
	return r != nil ? r : @"";
}

NSTextField * line_label(const std::string & text, NSLineBreakMode truncation)
{
	NSTextField * label = [NSTextField labelWithString:ns(text)];
	label.translatesAutoresizingMaskIntoConstraints = NO;
	label.lineBreakMode = truncation;
	// A truncating label gives way to the window rather than widening it.
	[label setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
	                                forOrientation:NSLayoutConstraintOrientationHorizontal];
	return label;
}

NSButton * button(NSString * title, id target, SEL action)
{
	NSButton * b = [NSButton buttonWithTitle:title target:target action:action];
	b.translatesAutoresizingMaskIntoConstraints = NO;
	return b;
}

}   // namespace

// --- every window ------------------------------------------------------------

//! Owns one window, and is kept alive by the set of open ones until it closes:
//! nothing else holds on to it. The window's delegate, for that.
@interface DdbTangoTaggerWindow : NSObject <NSWindowDelegate>
@property (nonatomic, strong) NSWindow * window;
- (NSWindow *)makeWindowTitled:(NSString *)title size:(NSSize)size resizable:(BOOL)resizable;
- (void)show;
- (void)close;
//! After the window has closed, for the subclass to let go of what it holds.
- (void)didClose;
@end

namespace
{

//! Every window this file has open, for shutdown to close.
NSMutableSet<DdbTangoTaggerWindow *> * open_windows = nil;

}   // namespace

@implementation DdbTangoTaggerWindow

- (NSWindow *)makeWindowTitled:(NSString *)title size:(NSSize)size resizable:(BOOL)resizable
{
	NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
	if (resizable) style |= NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;
	NSWindow * w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, size.width, size.height)
	                                           styleMask:style
	                                             backing:NSBackingStoreBuffered
	                                               defer:YES];
	w.title = title;
	// The set above owns the window's owner, and ARC the window.
	w.releasedWhenClosed = NO;
	w.delegate = self;
	self.window = w;
	return w;
}

- (void)show
{
	NSWindow * w = self.window;
	[w layoutIfNeeded];
	// Over the middle of DeaDBeeF's own window; the middle of the screen when
	// there is none to go over.
	NSWindow * parent = NSApp.mainWindow;
	if (parent != nil && parent != w && parent.visible)
	{
		const NSRect p = parent.frame;
		const NSSize s = w.frame.size;
		[w setFrameOrigin:NSMakePoint(round(NSMidX(p) - s.width / 2), round(NSMidY(p) - s.height / 2))];
	}
	else
	{
		[w center];
	}
	if (open_windows == nil) open_windows = [NSMutableSet new];
	[open_windows addObject:self];
	[w makeKeyAndOrderFront:nil];
}

- (void)close
{
	[self.window close];
}

- (void)windowWillClose:(NSNotification *)notification
{
	self.window.delegate = nil;
	[self didClose];
	// Last: this may be the only reference left to self.
	[open_windows removeObject:self];
}

- (void)didClose {}

@end

// --- the results window ------------------------------------------------------

@interface DdbTangoTaggerReview : DdbTangoTaggerWindow <NSTableViewDataSource, NSTableViewDelegate>
- (instancetype)initWithReview:(std::shared_ptr<tt::review>)review;
@end

@implementation DdbTangoTaggerReview
{
	std::shared_ptr<tt::review> _review;
	std::vector<tt::review_column> _columns;
	NSTableView * _table;
	NSTextView * _preview;
	NSButton * _write;
	//! The option controls, by option: a popup or a checkbox.
	NSMutableArray<NSControl *> * _options;
}

- (instancetype)initWithReview:(std::shared_ptr<tt::review>)review
{
	self = [super init];
	if (self == nil) return nil;
	_review = std::move(review);
	_columns = _review->columns();

	NSWindow * w = [self makeWindowTitled:ns(_review->window_title()) size:NSMakeSize(1000, 640) resizable:YES];
	w.minSize = NSMakeSize(640, 420);
	[self buildContent];
	return self;
}

- (NSString *)identifierFor:(std::size_t)column
{
	return [NSString stringWithFormat:@"c%zu", column];
}

- (std::size_t)columnFor:(NSTableColumn *)column
{
	return (std::size_t) [[column.identifier substringFromIndex:1] integerValue];
}

//! A column as wide as the widest of its cells and its heading, label padding
//! included - the spacing between columns lies outside it - that keeps its
//! width when the window is resized: the free space goes to the fill columns.
- (CGFloat)fittingWidthOf:(std::size_t)column
{
	CGFloat width = [NSTextField labelWithString:ns(_columns[column].heading)].fittingSize.width;
	NSTextField * probe = [NSTextField labelWithString:@""];
	for (std::size_t row = 0; row < _review->size(); row++)
	{
		probe.stringValue = ns(_review->cell(row, column));
		width = MAX(width, probe.fittingSize.width);
	}
	return ceil(width);
}

- (void)addColumns
{
	for (std::size_t c = 0; c < _columns.size(); c++)
	{
		const tt::review_column & rc = _columns[c];
		NSTableColumn * column = [[NSTableColumn alloc] initWithIdentifier:[self identifierFor:c]];
		column.title = ns(rc.heading);
		switch (rc.kind)
		{
		case tt::review_column::check:
		{
			// Room for the checkbox and a candidate number beside it.
			const CGFloat width = ceil([NSButton checkboxWithTitle:@" 88" target:nil action:nil].fittingSize.width);
			column.width = column.minWidth = column.maxWidth = width;
			column.resizingMask = NSTableColumnNoResizing;
			break;
		}
		case tt::review_column::fit:
			column.width = column.minWidth = [self fittingWidthOf:c];
			column.resizingMask = NSTableColumnUserResizingMask;
			break;
		default:
			column.width = rc.width;
			column.minWidth = MIN(rc.width, 40);
			column.resizingMask = NSTableColumnUserResizingMask | NSTableColumnAutoresizingMask;
			break;
		}
		[_table addTableColumn:column];
	}
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
	[self addColumns];

	NSScrollView * tableScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
	tableScroll.documentView = _table;
	tableScroll.hasVerticalScroller = YES;
	tableScroll.autohidesScrollers = YES;
	tableScroll.borderType = NSBezelBorder;
	tableScroll.translatesAutoresizingMaskIntoConstraints = NO;

	// Built by hand: +[NSTextView scrollableTextView] is macOS 10.14, and
	// DeaDBeeF for Mac runs on 10.13.
	NSScrollView * previewScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 400, 200)];
	previewScroll.hasVerticalScroller = YES;
	previewScroll.borderType = NSBezelBorder;
	previewScroll.translatesAutoresizingMaskIntoConstraints = NO;
	_preview = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, previewScroll.contentSize.width,
	                                                        previewScroll.contentSize.height)];
	_preview.minSize = NSMakeSize(0, 0);
	_preview.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
	_preview.verticallyResizable = YES;
	_preview.horizontallyResizable = NO;
	_preview.autoresizingMask = NSViewWidthSizable;
	_preview.textContainer.widthTracksTextView = YES;
	previewScroll.documentView = _preview;
	_preview.editable = NO;
	_preview.selectable = YES;
	_preview.font = [NSFont systemFontOfSize:NSFont.systemFontSize];
	_preview.textContainerInset = NSMakeSize(4, 4);

	NSTextField * status = [NSTextField wrappingLabelWithString:ns(_review->status())];
	status.textColor = NSColor.secondaryLabelColor;
	status.selectable = NO;
	status.translatesAutoresizingMaskIntoConstraints = NO;

	// The options: a popup with its label before it, or a checkbox; a "Write:"
	// before the first checkbox that follows a popup, as foo_tangotagger has it.
	_options = [NSMutableArray array];
	NSMutableArray<NSView *> * optionViews = [NSMutableArray array];
	const std::vector<tt::review_option> options = _review->options();
	bool after_choice = false;
	for (std::size_t i = 0; i < options.size(); i++)
	{
		const tt::review_option & o = options[i];
		NSControl * control;
		if (o.kind == tt::review_option::choice)
		{
			NSPopUpButton * popup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
			for (const std::string & choice : o.choices) [popup addItemWithTitle:ns(choice)];
			[popup selectItemAtIndex:o.value];
			[optionViews addObject:[NSTextField labelWithString:ns(o.label)]];
			control = popup;
			after_choice = true;
		}
		else
		{
			if (after_choice) [optionViews addObject:[NSTextField labelWithString:@"   Write:"]];
			after_choice = false;
			NSButton * box = [NSButton checkboxWithTitle:ns(o.label) target:nil action:nil];
			box.state = o.value ? NSControlStateValueOn : NSControlStateValueOff;
			control = box;
		}
		control.tag = (NSInteger) i;
		control.target = self;
		control.action = @selector(onOption:);
		[_options addObject:control];
		[optionViews addObject:control];
	}
	NSStackView * optionRow = [NSStackView stackViewWithViews:optionViews];
	optionRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
	optionRow.spacing = 8;
	optionRow.translatesAutoresizingMaskIntoConstraints = NO;

	NSButton * checkAll = [NSButton buttonWithTitle:@"Check All" target:self action:@selector(onCheckAll:)];
	NSButton * checkNone = [NSButton buttonWithTitle:@"Check None" target:self action:@selector(onCheckNone:)];
	NSButton * cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(onCancel:)];
	cancel.keyEquivalent = @"\033";
	_write = [NSButton buttonWithTitle:@"Write" target:self action:@selector(onWrite:)];
	_write.keyEquivalent = @"\r";

	NSView * spacer = [[NSView alloc] initWithFrame:NSZeroRect];
	[spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
	NSStackView * buttonRow = [NSStackView stackViewWithViews:@[ checkAll, checkNone, spacer, cancel, _write ]];
	buttonRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
	buttonRow.spacing = 8;
	buttonRow.translatesAutoresizingMaskIntoConstraints = NO;

	for (NSView * v in @[ tableScroll, previewScroll, status, optionRow, buttonRow ]) [root addSubview:v];
	const CGFloat m = 20;
	[NSLayoutConstraint activateConstraints:@[
		[tableScroll.topAnchor constraintEqualToAnchor:root.topAnchor constant:m],
		[tableScroll.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[tableScroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[tableScroll.heightAnchor constraintEqualToAnchor:root.heightAnchor multiplier:0.5],

		[previewScroll.topAnchor constraintEqualToAnchor:tableScroll.bottomAnchor constant:8],
		[previewScroll.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[previewScroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],

		[status.topAnchor constraintEqualToAnchor:previewScroll.bottomAnchor constant:8],
		[status.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[status.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],

		[optionRow.topAnchor constraintEqualToAnchor:status.bottomAnchor constant:12],
		[optionRow.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[optionRow.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-m],

		[buttonRow.topAnchor constraintEqualToAnchor:optionRow.bottomAnchor constant:12],
		[buttonRow.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[buttonRow.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[buttonRow.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-m],
	]];

	[self relabelWriteButton];
	if (_review->size() > 0)
	{
		[_table selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
		[self showPreviewForRow:0];
	}
}

- (void)didClose
{
	// The review holds the tracks; the window is done with them.
	_table.dataSource = nil;
	_table.delegate = nil;
	_review.reset();
}

// --- the cells -------------------------------------------------------------

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
	return _review ? (NSInteger) _review->size() : 0;
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
	const std::size_t r = (std::size_t) row;
	const std::size_t c = [self columnFor:tableColumn];
	if (_columns[c].kind == tt::review_column::check)
	{
		if (!_review->checkable(r)) return nil;
		NSButton * box = [tableView makeViewWithIdentifier:tableColumn.identifier owner:self];
		if (box == nil)
		{
			box = [NSButton checkboxWithTitle:@"" target:self action:@selector(onToggle:)];
			box.identifier = tableColumn.identifier;
		}
		box.tag = row;
		// The candidate's number, when the track has several to choose from.
		const std::string number = _review->cell(r, c);
		box.title = number.empty() ? @"" : ns(" " + number);
		box.state = _review->checked(r) ? NSControlStateValueOn : NSControlStateValueOff;
		return box;
	}

	NSTextField * cell = [tableView makeViewWithIdentifier:tableColumn.identifier owner:self];
	if (cell == nil)
	{
		cell = [NSTextField labelWithString:@""];
		cell.identifier = tableColumn.identifier;
		cell.lineBreakMode = NSLineBreakByTruncatingTail;
	}
	cell.stringValue = ns(_review->cell(r, c));
	cell.textColor = _review->checkable(r) ? NSColor.labelColor : NSColor.secondaryLabelColor;
	return cell;
}

//! Every row of one track in the same shade, the next track's in the other,
//! so a track's candidates read as one block.
- (void)tableView:(NSTableView *)tableView didAddRowView:(NSTableRowView *)rowView forRow:(NSInteger)row
{
	if (!_review || row < 0 || (std::size_t) row >= _review->size()) return;
	NSArray<NSColor *> * shades;
	if (@available(macOS 10.14, *)) shades = NSColor.alternatingContentBackgroundColors;
	else shades = NSColor.controlAlternatingRowBackgroundColors;
	rowView.backgroundColor = shades[_review->block((std::size_t) row) % shades.count];
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
	[self showPreviewForRow:_table.selectedRow];
}

- (void)showPreviewForRow:(NSInteger)row
{
	NSString * text = @"";
	if (_review && row >= 0 && (std::size_t) row < _review->size()) text = ns(_review->preview((std::size_t) row));
	_preview.string = text;
	[_preview scrollRangeToVisible:NSMakeRange(0, 0)];
}

// --- the controls ----------------------------------------------------------

- (void)reloadChecks
{
	const NSInteger check = [_table columnWithIdentifier:[self identifierFor:0]];
	[_table reloadDataForRowIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, _review->size())]
	                  columnIndexes:[NSIndexSet indexSetWithIndex:check >= 0 ? check : 0]];
	[self relabelWriteButton];
}

- (void)onToggle:(NSButton *)sender
{
	// Checking one candidate of a track unchecks the others.
	_review->set_checked((std::size_t) sender.tag, sender.state == NSControlStateValueOn);
	[self reloadChecks];
}

- (void)onCheckAll:(id)sender
{
	_review->check_all();
	[self reloadChecks];
}

- (void)onCheckNone:(id)sender
{
	_review->check_none();
	[self reloadChecks];
}

//! An option changed: kept, and the cells and the preview follow, since what
//! each row would write may have changed with it.
- (void)onOption:(NSControl *)sender
{
	int value = 0;
	if ([sender isKindOfClass:[NSPopUpButton class]]) value = (int) ((NSPopUpButton *) sender).indexOfSelectedItem;
	else value = ((NSButton *) sender).state == NSControlStateValueOn ? 1 : 0;
	_review->set_option((std::size_t) sender.tag, value);
	const NSInteger selected = _table.selectedRow;
	// A cell that fitted may not any more: "same" can become "same, no links".
	for (std::size_t c = 0; c < _columns.size(); c++)
	{
		if (_columns[c].kind != tt::review_column::fit) continue;
		NSTableColumn * column = [_table tableColumnWithIdentifier:[self identifierFor:c]];
		const CGFloat width = [self fittingWidthOf:c];
		column.minWidth = width;
		if (column.width < width) column.width = width;
	}
	[_table reloadData];
	if (selected >= 0) [_table selectRowIndexes:[NSIndexSet indexSetWithIndex:selected] byExtendingSelection:NO];
	[self showPreviewForRow:selected];
}

- (void)relabelWriteButton
{
	// "Write 12 Files": the review's words, in a Mac button's title case.
	_write.title = ns(_review->commit_label()).capitalizedString;
	_write.enabled = _review->count_checked() > 0;
}

- (void)onWrite:(id)sender
{
	_review->commit();
	[self close];
}

- (void)onCancel:(id)sender
{
	[self close];
}

@end

// --- the progress window -----------------------------------------------------

//! Owned by its timer, which outlives the window: a Cancel closes the window
//! at once, and the timer goes on until the listening has noticed and stopped.
@interface DdbTangoTaggerProgress : DdbTangoTaggerWindow
- (instancetype)initWithProgress:(std::shared_ptr<tt::scan_progress>)progress;
@end

@implementation DdbTangoTaggerProgress
{
	std::shared_ptr<tt::scan_progress> _progress;
	std::chrono::steady_clock::time_point _started;
	NSTimer * _timer;
	NSTextField * _count;
	NSTextField * _items;
	NSProgressIndicator * _bar;
	BOOL _dismissed;   //!< cancelled, or closed from outside
}

- (instancetype)initWithProgress:(std::shared_ptr<tt::scan_progress>)progress
{
	self = [super init];
	if (self == nil) return nil;
	_progress = std::move(progress);
	_started = std::chrono::steady_clock::now();
	// The timer holds on to self until it is invalidated. In the common modes,
	// so that it goes on ticking while a menu is open.
	_timer = [NSTimer timerWithTimeInterval:0.1 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
	[[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
	return self;
}

- (void)build
{
	NSWindow * w = [self makeWindowTitled:@"Tango Tagger" size:NSMakeSize(460, 120) resizable:NO];
	NSView * root = w.contentView;

	_count = line_label(_progress->title(), NSLineBreakByTruncatingTail);
	_items = line_label("", NSLineBreakByTruncatingMiddle);
	_items.textColor = NSColor.secondaryLabelColor;
	_bar = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
	_bar.style = NSProgressIndicatorStyleBar;
	_bar.indeterminate = NO;
	_bar.minValue = 0;
	_bar.maxValue = 1;
	_bar.translatesAutoresizingMaskIntoConstraints = NO;
	// Stops listening; the results window still opens with what was found.
	NSButton * stop = button(@"Stop", self, @selector(onStop:));
	stop.keyEquivalent = @"\033";

	for (NSView * v in @[ _count, _bar, _items, stop ]) [root addSubview:v];
	const CGFloat m = 20;
	[NSLayoutConstraint activateConstraints:@[
		[root.widthAnchor constraintEqualToConstant:460],
		[_count.topAnchor constraintEqualToAnchor:root.topAnchor constant:m],
		[_count.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[_count.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[_bar.topAnchor constraintEqualToAnchor:_count.bottomAnchor constant:8],
		[_bar.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[_bar.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[_items.topAnchor constraintEqualToAnchor:_bar.bottomAnchor constant:8],
		[_items.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:m],
		[_items.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[stop.topAnchor constraintEqualToAnchor:_items.bottomAnchor constant:12],
		[stop.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-m],
		[stop.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-m],
	]];
	[self show];
}

- (void)tick:(NSTimer *)timer
{
	const tt::scan_progress::snapshot s = _progress->read();

	if (s.finished || !ready.load())
	{
		_dismissed = YES;
		[_timer invalidate];   // the last reference to self, but for the stack's
		_timer = nil;
		[self close];
		return;
	}
	if (_dismissed) return;

	// Not at all for listening over before it would have been read.
	if (self.window == nil)
	{
		if (std::chrono::steady_clock::now() - _started < std::chrono::milliseconds(600)) return;
		[self build];
	}

	_items.stringValue = ns(s.in_flight);
	_bar.doubleValue = s.fraction;
}

- (void)onStop:(id)sender
{
	[self close];
}

- (void)didClose
{
	if (!_dismissed)
	{
		// Stop, Escape, the close button or shutdown: whichever, listening
		// stops, and the window opens with what there is.
		_dismissed = YES;
		_progress->cancel();
	}
}

@end

// --- tt_ui.h -------------------------------------------------------------------

void tt::ui::connect(DB_functions_t * api)
{
	// These windows are Cocoa, and belong only in DeaDBeeF's Cocoa interface,
	// which is the only one DeaDBeeF for Mac has - but a player built from
	// source with another, or with none, goes on without them.
	ready = api->plug_get_for_id(cocoaui_id) != nullptr;
}

void tt::ui::disconnect() {}

void tt::ui::shutdown()
{
	if (!ready.exchange(false)) return;
	// Closing a window gives back its tracks. DeaDBeeF for Mac stops its
	// plugins on the main thread, as the application quits; anywhere else,
	// the windows are closed as soon as the main thread gets to them.
	void (^close_all)(void) = ^{
		// A copy, because each window takes itself out of the set as it goes.
		for (DdbTangoTaggerWindow * w in [open_windows allObjects]) [w close];
	};
	if ([NSThread isMainThread]) close_all();
	else dispatch_async(dispatch_get_main_queue(), close_all);
}

bool tt::ui::available()
{
	return ready.load();
}

void tt::ui::show_progress(std::shared_ptr<scan_progress> progress)
{
	on_main_thread([progress]() {
		// Kept alive by its timer.
		(void) [[DdbTangoTaggerProgress alloc] initWithProgress:progress];
	});
}

void tt::ui::show_review(std::shared_ptr<review> r)
{
	on_main_thread([r]() {
		[[[DdbTangoTaggerReview alloc] initWithReview:r] show];
	});
}

void tt::ui::show_message(const std::string & title, const std::string & text)
{
	on_main_thread([title, text]() {
		NSAlert * alert = [NSAlert new];
		alert.messageText = ns(title);
		alert.informativeText = ns(text);
		[alert addButtonWithTitle:@"OK"];
		NSWindow * parent = NSApp.mainWindow;
		if (parent != nil && parent.visible) [alert beginSheetModalForWindow:parent completionHandler:nil];
		else [alert runModal];
	});
}
