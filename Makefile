VERSION := $(shell cat VERSION)
APPINDICATOR_PKG ?= $(shell pkg-config --exists ayatana-appindicator3-0.1 && echo ayatana-appindicator3-0.1 || echo appindicator3-0.1)

CC      = gcc
CFLAGS  ?= -Wall -Wextra -O2
CFLAGS  += $(shell pkg-config --cflags gtk+-3.0 $(APPINDICATOR_PKG) 2>/dev/null)
LDFLAGS += $(shell pkg-config --libs gtk+-3.0 $(APPINDICATOR_PKG) 2>/dev/null)

ifeq ($(APPINDICATOR_PKG),ayatana-appindicator3-0.1)
  CFLAGS += -DHAVE_AYATANA
endif

ifeq ($(shell pkg-config --exists libsystemd && echo yes),yes)
  CFLAGS  += $(shell pkg-config --cflags libsystemd) -DHAVE_LIBSYSTEMD
  LDFLAGS += $(shell pkg-config --libs libsystemd)
endif

ifeq ($(shell pkg-config --exists libcurl && echo yes),yes)
  CFLAGS  += $(shell pkg-config --cflags libcurl) -DHAVE_LIBCURL
  LDFLAGS += $(shell pkg-config --libs libcurl)
endif

SRCDIR  = src
SOURCES = $(SRCDIR)/main.c $(SRCDIR)/config.c $(SRCDIR)/ping.c $(SRCDIR)/http_check.c $(SRCDIR)/tray.c $(SRCDIR)/dbus_monitor.c $(SRCDIR)/settings_ui.c
TARGET  = internet-indicator

PREFIX       = /usr/local
INSTALL_BIN  = $(PREFIX)/bin
INSTALL_DATA = $(PREFIX)/share/internet-indicator

.PHONY: all clean install uninstall autostart help

help:
	@echo "Available targets:"
	@echo "  all                  - Build the application"
	@echo "  clean                - Remove build artifacts"
	@echo "  install              - Install application and systemd service"
	@echo "  uninstall            - Remove installed files"
	@echo "  autostart            - Install desktop file to ~/.config/autostart"
	@echo "  distributable        - Build standalone binary (embedded icons)"
	@echo ""
	@echo "Packaging:"
	@echo "  deb                  - Build Debian package (.deb)"
	@echo "  rpm                  - Build RPM package (.rpm)"
	@echo "  apk                  - Build Alpine package (.apk)"
	@echo "  docker               - Build minimal Docker image"
	@echo "  packages             - Build all packages"
	@echo "  release              - Create GitHub release and upload packages"
	@echo ""
	@echo "Version Bumping:"
	@echo "  bump-version-bugfix  - Bump patch version (x.y.Z+1)"
	@echo "  bump-version-minor   - Bump minor version (x.Y+1.0)"
	@echo "  bump-version-major   - Bump major version (X+1.0.0)"

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)
	rm -f $(TARGET)-standalone
	rm -f src/icons_embedded.h
	rm -rf build/

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(INSTALL_BIN)/$(TARGET)
	install -Dm644 internet-indicator.desktop $(DESTDIR)$(PREFIX)/share/applications/internet-indicator.desktop
	mkdir -p $(DESTDIR)$(PREFIX)/lib/systemd/user
	sed "s|ExecStart=.*|ExecStart=$(INSTALL_BIN)/internet-indicator|" packaging/internet-indicator.service > $(DESTDIR)$(PREFIX)/lib/systemd/user/internet-indicator.service
	install -Dm644 icons/net-good.png $(DESTDIR)$(INSTALL_DATA)/icons/net-good.png
	install -Dm644 icons/net-bad.png  $(DESTDIR)$(INSTALL_DATA)/icons/net-bad.png
	@echo "Setting CAP_NET_RAW on $(DESTDIR)$(INSTALL_BIN)/$(TARGET)..."
	sudo setcap cap_net_raw+ep $(DESTDIR)$(INSTALL_BIN)/$(TARGET) || \
		echo "Warning: could not set CAP_NET_RAW. Run with sudo or set manually."

uninstall:
	rm -f  $(DESTDIR)$(INSTALL_BIN)/$(TARGET)
	rm -f  $(DESTDIR)$(PREFIX)/share/applications/internet-indicator.desktop
	rm -f  $(DESTDIR)$(PREFIX)/lib/systemd/user/internet-indicator.service
	rm -rf $(DESTDIR)$(INSTALL_DATA)
	rm -f  $(HOME)/.config/autostart/internet-indicator.desktop

autostart:
	install -Dm644 internet-indicator.desktop $(HOME)/.config/autostart/internet-indicator.desktop
	@echo "Autostart entry installed."

distributable:
	@echo "Generating embedded icons headers..."
	xxd -i icons/net-good.png > src/icons_embedded.h
	xxd -i icons/net-bad.png >> src/icons_embedded.h
	$(CC) $(CFLAGS) -DSTANDALONE -o $(TARGET)-standalone $(SOURCES) $(LDFLAGS)
	@echo "Standalone binary created: $(TARGET)-standalone"

deb: distributable
	@echo "Building DEB..."
	mkdir -p build/deb/internet-indicator_$(VERSION)_amd64/DEBIAN
	mkdir -p build/deb/internet-indicator_$(VERSION)_amd64/usr/bin
	mkdir -p build/deb/internet-indicator_$(VERSION)_amd64/usr/share/applications
	mkdir -p build/deb/internet-indicator_$(VERSION)_amd64/usr/lib/systemd/user
	sed "s/Version: .*/Version: $(VERSION)/" packaging/internet-indicator.control > build/deb/internet-indicator_$(VERSION)_amd64/DEBIAN/control
	cp internet-indicator-standalone build/deb/internet-indicator_$(VERSION)_amd64/usr/bin/internet-indicator
	cp internet-indicator.desktop build/deb/internet-indicator_$(VERSION)_amd64/usr/share/applications/
	sed "s|ExecStart=.*|ExecStart=/usr/bin/internet-indicator|" packaging/internet-indicator.service > build/deb/internet-indicator_$(VERSION)_amd64/usr/lib/systemd/user/internet-indicator.service
	dpkg-deb --root-owner-group --build build/deb/internet-indicator_$(VERSION)_amd64
	mv build/deb/internet-indicator_$(VERSION)_amd64.deb build/

rpm: distributable
	@echo "Building RPM..."
	mkdir -p build/rpm/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	sed "s/Version:\s*.*/Version:        $(VERSION)/" packaging/internet-indicator.spec > build/rpm/SPECS/internet-indicator.spec
	rpmbuild -bb --define "_topdir $$(pwd)/build/rpm" --define "srcdir $$(pwd)" build/rpm/SPECS/internet-indicator.spec
	find build/rpm/RPMS -name "*.rpm" -exec cp {} build/ \; || true

apk:
	@echo "Building APK via Docker..."
	DOCKER_BUILDKIT=0 docker build --build-arg VERSION=$(VERSION) --target builder -t internet-indicator-apk-builder -f packaging/Dockerfile .
	mkdir -p build/apk
	docker run --rm --name temp-apk-builder -v $$(pwd)/build/apk:/out internet-indicator-apk-builder sh -c 'cp /home/builder/packages/builder/x86_64/*.apk /out/'
	cp build/apk/*.apk build/ || true

docker:
	@echo "Building minimal Docker image..."
	DOCKER_BUILDKIT=0 docker build --build-arg VERSION=$(VERSION) --target minimal -t internet-indicator:latest -f packaging/Dockerfile .

packages:
	mkdir -p build
	-$(MAKE) deb
	-$(MAKE) rpm
	-$(MAKE) apk
	-$(MAKE) docker
	@echo "Package build phase completed (see above for any specific failures)."

release: packages distributable
	@echo "Checking if working directory is clean..."
	@if [ -n "$$(git status --porcelain)" ]; then \
		echo "Error: Working directory is not clean. Commit or stash your changes first."; \
		exit 1; \
	fi
	@echo "Creating GitHub release for v$(VERSION)..."
	@if git rev-parse v$(VERSION) >/dev/null 2>&1; then \
		echo "Tag v$(VERSION) already exists. Skipping tag creation."; \
	else \
		git tag -a v$(VERSION) -m "Release v$(VERSION)"; \
		git push origin v$(VERSION); \
	fi
	@assets=""; \
	for f in internet-indicator-standalone build/*.apk build/*.deb build/*.rpm; do \
		if [ -f "$$f" ]; then assets="$$assets $$f"; fi; \
	done; \
	if [ -n "$$assets" ]; then \
		echo "Uploading assets: $$assets"; \
		gh release create v$(VERSION) $$assets --title "v$(VERSION)" --notes "Release v$(VERSION)" || \
		gh release upload v$(VERSION) $$assets --clobber; \
	else \
		echo "No assets found in build/ or root. Creating empty release."; \
		gh release create v$(VERSION) --title "v$(VERSION)" --notes "Release v$(VERSION)" || true; \
	fi
	@echo "Release process finished!"

.PHONY: bump-packaging
bump-packaging:
	@sed -i "s/Version: .*/Version: $$(cat VERSION)/" packaging/internet-indicator.control
	@sed -i "s/^Version:\s*.*/Version:        $$(cat VERSION)/" packaging/internet-indicator.spec
	@sed -i "s/^ARG VERSION=.*/ARG VERSION=$$(cat VERSION)/" packaging/Dockerfile

bump-version-bugfix:
	@awk -F. '{print $$1"."$$2"."$$3+1}' VERSION > VERSION.tmp && mv VERSION.tmp VERSION
	@$(MAKE) bump-packaging
	@echo "Bumped version to $$(cat VERSION)"

bump-version-minor:
	@awk -F. '{print $$1"."$$2+1".0"}' VERSION > VERSION.tmp && mv VERSION.tmp VERSION
	@$(MAKE) bump-packaging
	@echo "Bumped version to $$(cat VERSION)"

bump-version-major:
	@awk -F. '{print $$1+1".0.0"}' VERSION > VERSION.tmp && mv VERSION.tmp VERSION
	@$(MAKE) bump-packaging
	@echo "Bumped version to $$(cat VERSION)"
