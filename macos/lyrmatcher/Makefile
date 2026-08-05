CONFIG ?= release
APP    := LyrMatcher.app
BIN    := .build/$(CONFIG)/LyrMatcher

.PHONY: all build app run test clean

all: app

build:
	swift build -c $(CONFIG)

## Assemble the .app bundle SwiftPM cannot produce on its own.
app: build
	rm -rf $(APP)
	mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Resources
	cp $(BIN) $(APP)/Contents/MacOS/LyrMatcher
	cp Resources/Info.plist $(APP)/Contents/Info.plist
	# Ad-hoc signature, so macOS keeps the folder permissions it grants the app
	# instead of re-asking after every rebuild.
	codesign --force --sign - $(APP)
	@echo "Built $(APP)"

run: app
	open $(APP)

test:
	swift test

clean:
	swift package clean
	rm -rf $(APP)
