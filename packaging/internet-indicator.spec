Name:           internet-indicator
Version:        0.6.3
Release:        1%{?dist}
Summary:        Internet connectivity indicator

License:        MIT
URL:            https://github.com/lsferreira42/internet-indicator

BuildRequires:  make, gcc, gtk3-devel, libappindicator-gtk3-devel, systemd-devel, libcurl-devel, libnotify-devel
Requires:       gtk3 >= 3.0, libappindicator-gtk3, libnotify, libcurl, systemd-libs

%description
System tray internet connectivity indicator.

%install
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/share/applications
mkdir -p %{buildroot}/usr/lib/systemd/user
install -m 755 %{srcdir}/internet-indicator-standalone %{buildroot}/usr/bin/internet-indicator
install -m 644 %{srcdir}/internet-indicator.desktop %{buildroot}/usr/share/applications/
sed "s|ExecStart=.*|ExecStart=/usr/bin/internet-indicator|" %{srcdir}/packaging/internet-indicator.service > %{buildroot}/usr/lib/systemd/user/internet-indicator.service

%files
/usr/bin/internet-indicator
/usr/share/applications/internet-indicator.desktop
/usr/lib/systemd/user/internet-indicator.service

%changelog
* Mon Mar 30 2026 lsferreira42 <lsferreira42@example.com> - 0.2.0-1
- Initial changelog
