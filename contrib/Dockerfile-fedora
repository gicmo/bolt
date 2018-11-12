## -*- mode: dockerfile -*-
FROM fedora:29
ENV LANG en_US.UTF-8
ENV LANGUAGE en_US:en
ENV LC_ALL en_US.UTF-8
RUN dnf --enablerepo=updates-testing -y update
RUN dnf --enablerepo=updates-testing -y install \
    gcc \
    glib2-devel \
    gtk-doc \
    lcov \
    libgudev-devel \
    meson \
    polkit-devel \
    python3 \
    python3-dbus \
    python3-dbusmock \
    python3-gobject \
    rpm-build \
    redhat-rpm-config \
    systemd-devel \
    umockdev-devel

RUN mkdir /src
WORKDIR /src
