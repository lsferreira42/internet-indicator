# Internet Indicator 🌐

A blazing-fast, ultra-lightweight system tray app written in pure C (GTK3 / _AppIndicator_) that constantly monitors your internet connection. 

I built this because I was tired of second-guessing if my ISP was dropping packets or if my VPN was acting up, and I needed hard proof without keeping a terminal open running `ping` all day. It sits quietly in your tray, tracks your real-time latency, and logs every dropout directly into a searchable UI.

### Why this over a bash script?
- **Because I wanted to** its a good exercise to learn C and GTK3.
- **Zero bloat.** It runs on negligible RAM footprint.
- **Multithreading done right.** Network requests run in a detached thread behind a `GMutex` lock. The UI never freezes, even if DNS resolution hangs.
- **No false positives.** Hooks into Linux D-Bus (`logind`) to automatically pause checks while your system sleeps or is screen-locked.
- **Embedded Assets.** Can compile the icons directly into the C binary — single standalone executable.

---

## 🚀 Key Features

- **Dual Checking Engines:**
  - **ICMP Mode:** Smart datagram ping (`SOCK_DGRAM`) with full hostname resolution (`getaddrinfo`) and strict sequence matching. No root/`sudo` needed if CAP_NET_RAW is applied!
  - **HTTP Mode:** Powered by `libcurl`. Want to ping a captive portal or a localized intranet endpoint? Configure custom headers, methods (GET/HEAD/POST), toggle SSL verification, and specify accepted status codes (e.g., `200,301`).
- **Live Latency Tooltips:** Hover your tray to instantly see your latency via `CLOCK_MONOTONIC` metrics (e.g., `✓ Connected — ping.example.com (14ms)`). 
- **Desktop Notifications:** Native `libnotify` integration. Get an immediate system popup the second your network drops or recovers.
- **Auditable Connection Logs:** Built-in log rotation (configure size in KB). The Settings dialog contains a **Logs Tab** with a fully searchable UI (`GtkTreeView`) so you can pinpoint exactly when your connection failed to complain to your ISP.
- **Robust Error Diagnostics:** Any socket, HTTP, or DNS failures aren't just thrown to `stderr`, they're piped directly into your UI tray as an error row.

---

## 📦 Installation

### Distro Packages
We currently support DEB, RPM, and Alpine APK packaging right out of the box via the `Makefile`.

```bash
# Ubuntu, Mint, Debian
make deb

# Fedora, RHEL, openSUSE
make rpm

# Alpine (via reproducible Docker container)
make apk
```
The generated packages are tossed into the `build/` directory.

### Building from Source
Just want a portable binary to throw in your `~/bin` folder without polluting your package manager?

```bash
# Compiles a standalone executable with icons embedded directly as C arrays
make distributable

# Run it directly
./internet-indicator-standalone
```

### System Integration
Install globally to applications menu and systemd background service:

```bash
sudo make install
```
*(This automatically adds the `.desktop` file to your app launcher and sets `CAP_NET_RAW` permissions).*

To ensure the indicator boots up automatically on startup:
```bash
make autostart
```

---

## 🛠️ Development & Tooling

This project ships with heavy Makefile automation tailored for CICD and package maintainers:

- `make docker`: Builds an ultra-minimal Alpine container running the indicator (under `<35MB`).
- `make clean`: Clears all build artifacts.
- `make bump-version-[bugfix|minor|major]`: Automatically bumps the `VERSION` file and securely updates the `.control`, `.spec`, and Dockerfile packaging metadata.

---

## License
MIT. PRs and forks are extremely welcome.
