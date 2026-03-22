Name:           internet-indicator
Version:        1.0
Release:        1%{?dist}
Summary:        Internet connectivity indicator

License:        MIT
URL:            https://github.com/lsferreira42/internet-indicator

Requires:       gtk3 >= 3.0, libappindicator-gtk3

%description
System tray internet connectivity indicator.

%install
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/share/applications
install -m 755 %{srcdir}/internet-indicator-standalone %{buildroot}/usr/bin/internet-indicator
install -m 644 %{srcdir}/internet-indicator.desktop %{buildroot}/usr/share/applications/

%files
/usr/bin/internet-indicator
/usr/share/applications/internet-indicator.desktop
