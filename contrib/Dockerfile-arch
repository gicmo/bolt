## -*- mode: dockerfile -*-
FROM archlinux/base
ENV LANG en_US.UTF-8
ENV LC_ALL en_US.UTF-8
RUN rm /usr/share/libalpm/hooks/package-cleanup.hook
RUN pacman -Syu --noconfirm
RUN pacman -S --noconfirm base-devel
RUN pacman -S --noconfirm \
    asciidoc \
    dbus-glib \
    git \
    gobject-introspection \
    gtk-doc \
    meson \
    perl-sgmls \
    polkit \
    python-dbus \
    python-gobject \
    python-pip \
    umockdev \
    valgrind

RUN pip3 install python-dbusmock

RUN mkdir /src
WORKDIR /src
