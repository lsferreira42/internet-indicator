CC      = gcc
CFLAGS  = -Wall -Wextra -O2 \
          $(shell pkg-config --cflags gtk+-3.0 appindicator3-0.1)
LDFLAGS = $(shell pkg-config --libs gtk+-3.0 appindicator3-0.1)

SRCDIR  = src
SOURCES = $(SRCDIR)/main.c $(SRCDIR)/config.c $(SRCDIR)/ping.c $(SRCDIR)/tray.c
TARGET  = internet-indicator

PREFIX       = /usr/local
INSTALL_BIN  = $(PREFIX)/bin
INSTALL_DATA = $(PREFIX)/share/internet-indicator

.PHONY: all clean install uninstall autostart

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(INSTALL_BIN)/$(TARGET)
	install -Dm644 icons/net-good.png $(DESTDIR)$(INSTALL_DATA)/icons/net-good.png
	install -Dm644 icons/net-bad.png  $(DESTDIR)$(INSTALL_DATA)/icons/net-bad.png
	@echo "Setting CAP_NET_RAW on $(DESTDIR)$(INSTALL_BIN)/$(TARGET)..."
	sudo setcap cap_net_raw+ep $(DESTDIR)$(INSTALL_BIN)/$(TARGET) || \
		echo "Warning: could not set CAP_NET_RAW. Run with sudo or set manually."

uninstall:
	rm -f  $(DESTDIR)$(INSTALL_BIN)/$(TARGET)
	rm -rf $(DESTDIR)$(INSTALL_DATA)
	rm -f  $(HOME)/.config/autostart/internet-indicator.desktop

autostart:
	install -Dm644 internet-indicator.desktop $(HOME)/.config/autostart/internet-indicator.desktop
	@echo "Autostart entry installed."
