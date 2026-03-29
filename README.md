# Internet Indicator

A lightweight, minimal system tray indicator to monitor your internet connection. Written in C with GTK3 and AppIndicator.

## Features

- Pings a specified host (default `8.8.8.8`) using ICMP (raw or datagram sockets).
- Pauses checks during sleep or screen lock (requires `libsystemd`).
- Runs in a separate thread to keep the GUI responsive.
- Settings menu to configure address, interval, and logging.
- Connection state logging (can be viewed via journalctl).

## Installation

### Distro Packages
You can build native packages for your system. We currently support DEB, RPM, and Alpine APKs.

```bash
# To build a Debian package (Ubuntu, Mint, Debian)
make deb

# To build an RPM package (Fedora, RHEL, openSUSE)
make rpm

# To build an Alpine package via a reproducible Docker container
make apk
```

The generated packages will be placed inside the `build/` directory. You can install them locally via `dpkg -i`, `rpm -i`, or `apk add --allow-untrusted`.

### Building from Source
If you just want a portable, standalone binary to run without polluting your package manager:

```bash
# Compiles a standalone executable with icons embedded as C arrays
make distributable

# Run it directly
./internet-indicator-standalone
```

### Autostart
To ensure the indicator boots up automatically with your desktop environment:
```bash
make autostart
```

### Systemd Service (Background Daemon)
The application includes native integration as a graphical user service. If you install via packages or `make install`, the service file is placed automatically.

Enable and start the service tied to your user session (no `sudo` required):
```bash
systemctl --user daemon-reload
systemctl --user enable --now internet-indicator.service

# View connection state logs (if log_enabled=true in settings)
journalctl --user -u internet-indicator.service -f
```

## Permissions
Internet Indicator needs raw socket access for ICMP pings. If you install it via `make install` or through the provided packages, `setcap cap_net_raw+ep` is automatically applied, allowing the binary to run safely without `sudo`.

## Development

- `make build`: builds the binary dynamically linked to your local GTK/AppIndicator environment.
- `make docker`: builds an ultra-minimal Alpine container running the indicator (under `<35MB`).
- `make clean`: clears all build artifacts.
- Submitting updates? Bump versions using `make bump-version-bugfix`, `make bump-version-minor`, or `make bump-version-major`.

## License
MIT
