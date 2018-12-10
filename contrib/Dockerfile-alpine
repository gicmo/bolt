## -*- mode: dockerfile -*-
FROM alpine:latest
ENV LANG en_US.UTF-8
ENV LANGUAGE en_US:en
ENV LC_ALL en_US.UTF-8
RUN apk add --no-cache bash dbus dbus-dev eudev-dev gcc glib-dev libc-dev meson ninja polkit-dev udev
RUN mkdir /src
WORKDIR /src
