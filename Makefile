PREFIX    ?= $(HOME)/.local
BUILD_DIR ?= build

APP_ID  := io.github.ivanlukomskiy.YourSloppyViewer
BINDIR  := $(PREFIX)/bin
DATADIR := $(PREFIX)/share
ICONDIR := $(DATADIR)/icons/hicolor/512x512/apps

.PHONY: all build test install uninstall clean

all: build

build:
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) --parallel

test: build
	cmake --build $(BUILD_DIR) --parallel --target camera_test
	$(BUILD_DIR)/camera_test

install: build
	rm -f $(ICONDIR)/your-sloppy-viewer.png
	rm -f $(DATADIR)/mime/packages/your-sloppy-viewer.xml
	rm -f $(DATADIR)/applications/your-sloppy-viewer.desktop
	install -Dm755 $(BUILD_DIR)/your-sloppy-viewer $(BINDIR)/your-sloppy-viewer
	install -Dm644 data/$(APP_ID).png $(ICONDIR)/$(APP_ID).png
	install -Dm644 data/$(APP_ID).xml $(DATADIR)/mime/packages/$(APP_ID).xml
	install -d $(DATADIR)/applications
	sed 's|@BINDIR@|$(BINDIR)|' data/$(APP_ID).desktop.in > $(DATADIR)/applications/$(APP_ID).desktop
	update-mime-database $(DATADIR)/mime
	update-desktop-database $(DATADIR)/applications
	-gtk-update-icon-cache -q -f -t $(DATADIR)/icons/hicolor
	xdg-mime default $(APP_ID).desktop model/step image/vnd.dxf

uninstall:
	rm -f $(BINDIR)/your-sloppy-viewer
	rm -f $(ICONDIR)/$(APP_ID).png
	rm -f $(DATADIR)/mime/packages/$(APP_ID).xml
	rm -f $(DATADIR)/applications/$(APP_ID).desktop
	-update-mime-database $(DATADIR)/mime
	-update-desktop-database $(DATADIR)/applications
	-gtk-update-icon-cache -q -f -t $(DATADIR)/icons/hicolor

clean:
	rm -rf $(BUILD_DIR)
