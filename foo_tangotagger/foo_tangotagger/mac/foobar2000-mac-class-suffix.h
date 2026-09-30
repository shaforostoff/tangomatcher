#pragma once

// Objective-C has one flat namespace for class names across every bundle the
// process has loaded. The SDK's macOS helpers suffix their class names with
// this value so two components cannot collide; they #error without it.
// This component's own classes are prefixed fooTangoTagger.
#define FOOBAR2000_MAC_CLASS_SUFFIX _foo_tangotagger
