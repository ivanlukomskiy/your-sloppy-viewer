PREFIX    ?= $(HOME)/.local
BUILD_DIR ?= build

BINDIR  := $(PREFIX)/bin
DATADIR := $(PREFIX)/share
ICONDIR := $(DATADIR)/icons/hicolor

.PHONY: all build install uninstall clean

all: build

build:
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) --parallel

install: build
	install -Dm755 $(BUILD_DIR)/your-sloppy-viewer $(BINDIR)/your-sloppy-viewer
	install -Dm644 data/your-sloppy-viewer.png $(ICONDIR)/512x512/apps/your-sloppy-viewer.png
	install -Dm644 data/your-sloppy-viewer.xml $(DATADIR)/mime/packages/your-sloppy-viewer.xml
	install -d $(DATADIR)/applications
	sed 's|@BINDIR@|$(BINDIR)|' data/your-sloppy-viewer.desktop.in > $(DATADIR)/applications/your-sloppy-viewer.desktop
	update-mime-database $(DATADIR)/mime
	update-desktop-database $(DATADIR)/applications
	-gtk-update-icon-cache -q -f -t $(ICONDIR)
	xdg-mime default your-sloppy-viewer.desktop model/step

uninstall:
	rm -f $(BINDIR)/your-sloppy-viewer
	rm -f $(ICONDIR)/512x512/apps/your-sloppy-viewer.png
	rm -f $(DATADIR)/mime/packages/your-sloppy-viewer.xml
	rm -f $(DATADIR)/applications/your-sloppy-viewer.desktop
	-update-mime-database $(DATADIR)/mime
	-update-desktop-database $(DATADIR)/applications
	-gtk-update-icon-cache -q -f -t $(ICONDIR)

clean:
	rm -rf $(BUILD_DIR)
