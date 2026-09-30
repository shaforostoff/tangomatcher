#pragma once

// Precompiled header.
//
// On Windows, foobar2000+atl.h is the SDK's umbrella: pfc, the SDK proper,
// ATL and WTL. On macOS there is no ATL, WTL or libPPUI, so it is the SDK
// alone; the Cocoa window in mac/ does not use this header.

#ifdef _WIN32
#include <helpers/foobar2000+atl.h>
#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>
#include <libPPUI/listview_helper.h>
#else
#include <SDK/foobar2000.h>
#endif

#include <algorithm>
#include <string>
#include <vector>
