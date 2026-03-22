# Internet Indicator

A lightweight, minimal system tray indicator to monitor your internet connection. Written in C with GTK3 and AppIndicator.

## Features

- **Real-time monitoring**: Pings a user-defined host (default is `8.8.8.8`) securely using ICMP raw sockets (or UDP fallback).
- **Asynchronous**: Ping happens in a separate thread, meaning the GUI will never freeze or hang.
- **Configurable**: Right-click the icon to change the target address and ping interval (in seconds) on the fly.
- **Cross-compatible**: Compiles as a dynamic binary for major distributions, or fully standalone via Docker multi-stage builds.

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

## Permissions
Internet Indicator needs raw socket access for ICMP pings. If you install it via `make install` or through the provided packages, `setcap cap_net_raw+ep` is automatically applied, allowing the binary to run safely without `sudo`.

## Development

- `make build`: builds the binary dynamically linked to your local GTK/AppIndicator environment.
- `make docker`: builds an ultra-minimal Alpine container running the indicator (under `<35MB`).
- `make clean`: clears all build artifacts.
- Submitting updates? Bump versions using `make bump-version-bugfix`, `make bump-version-minor`, or `make bump-version-major`.

## License
MIT
