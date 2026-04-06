# Build stage
FROM alpine:3.18 AS builder
RUN apk add --no-cache build-base gtk+3.0-dev libayatana-appindicator-dev xxd alpine-sdk sudo

# Create a non-root user for abuild
RUN adduser -G abuild -g "Alpine Package Builder" -s /bin/sh -D builder && \
    echo "builder ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers
USER builder
WORKDIR /home/builder
RUN abuild-keygen -n -a -i

# Copy source
COPY --chown=builder:abuild . source/
WORKDIR /home/builder/source

# Build standalone binary
RUN make distributable

ARG VERSION=0.3.0
# Create APKBUILD
RUN echo 'pkgname=internet-indicator' > APKBUILD && \
    echo 'pkgver='${VERSION} >> APKBUILD && \
    echo 'pkgrel=0' >> APKBUILD && \
    echo 'pkgdesc="Internet indicator"' >> APKBUILD && \
    echo 'url="https://github.com/lsferreira42/internet-indicator"' >> APKBUILD && \
    echo 'arch="x86_64"' >> APKBUILD && \
    echo 'license="MIT"' >> APKBUILD && \
    echo 'depends="gtk+3.0 libayatana-appindicator"' >> APKBUILD && \
    echo 'makedepends="build-base gtk+3.0-dev libayatana-appindicator-dev xxd"' >> APKBUILD && \
    echo 'source=""' >> APKBUILD && \
    echo 'build() { make distributable; }' >> APKBUILD && \
    echo 'package() {' >> APKBUILD && \
    echo '    mkdir -p $pkgdir/usr/bin' >> APKBUILD && \
    echo '    mkdir -p $pkgdir/usr/share/applications' >> APKBUILD && \
    echo '    mkdir -p $pkgdir/usr/lib/systemd/user' >> APKBUILD && \
    echo '    cp internet-indicator-standalone $pkgdir/usr/bin/internet-indicator' >> APKBUILD && \
    echo '    cp internet-indicator.desktop $pkgdir/usr/share/applications/' >> APKBUILD && \
    echo '    sed "s|ExecStart=.*|ExecStart=/usr/bin/internet-indicator|" packaging/internet-indicator.service > $pkgdir/usr/lib/systemd/user/internet-indicator.service' >> APKBUILD && \
    echo '}' >> APKBUILD

# Generate APK
RUN abuild -F checksum && abuild -F rootpkg

# Minimal Runtime Stage
FROM alpine:3.18 AS minimal
RUN apk add --no-cache gtk+3.0 libayatana-appindicator
COPY --from=builder /home/builder/source/internet-indicator-standalone /usr/bin/internet-indicator
COPY --from=builder /home/builder/source/internet-indicator.desktop /usr/share/applications/
CMD ["internet-indicator"]
