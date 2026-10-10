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
	install -Dm755 $(BUILD_DIR)/stepview $(BINDIR)/stepview
	install -Dm644 data/stepview.png $(ICONDIR)/512x512/apps/stepview.png
	install -Dm644 data/stepview.xml $(DATADIR)/mime/packages/stepview.xml
	install -d $(DATADIR)/applications
	sed 's|@BINDIR@|$(BINDIR)|' data/stepview.desktop.in > $(DATADIR)/applications/stepview.desktop
	update-mime-database $(DATADIR)/mime
	update-desktop-database $(DATADIR)/applications
	-gtk-update-icon-cache -q -f -t $(ICONDIR)
	xdg-mime default stepview.desktop model/step

uninstall:
	rm -f $(BINDIR)/stepview
	rm -f $(ICONDIR)/512x512/apps/stepview.png
	rm -f $(DATADIR)/mime/packages/stepview.xml
	rm -f $(DATADIR)/applications/stepview.desktop
	-update-mime-database $(DATADIR)/mime
	-update-desktop-database $(DATADIR)/applications
	-gtk-update-icon-cache -q -f -t $(ICONDIR)

clean:
	rm -rf $(BUILD_DIR)
