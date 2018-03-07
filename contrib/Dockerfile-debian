## -*- mode: dockerfile -*-
FROM debian:sid
RUN apt-get update -q && apt-get install -y \
    asciidoc \
    git \
    gobject-introspection \
    libgirepository1.0-dev \
    libpolkit-gobject-1-dev \
    locales \
    libudev-dev \
    libumockdev-dev \
    libsystemd-dev \
    meson \
    pkg-config \
    policykit-1 \
    python3-dbus \
    python3-dbusmock \
    udev \
    umockdev \
    systemd \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir /src
WORKDIR /src
