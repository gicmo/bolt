bolt
====

Userspace system daemon to enable security levels for *Thunderbolt™*
on GNU/Linux®.

Introduction
------------

Thunderbolt™ is the brand name of a hardware interface developed by
Intel® that allows the connection of external peripherals to a
computer.

Devices connected via Thunderbolt can be DMA masters and thus read
system memory without interference of the operating system (or even
the CPU). Version 3 of the interface introduced 5 different security
levels, in order to mitigate the aforementioned security risk that
connected devices pose to the system. The security level is set by the
system firmware.

The five security levels are:

 * `none`:    Security disabled, all devices will fully functional
              on connect.
 * `dponly`:  Only pass the display-port stream through to the
              connected device.
 * `user`:    Connected devices need to be manually authorized by
              the user.
 * `secure`:  As 'user', but also challenge the device with a secret
              key to verify its identity.
 * `usbonly`: One PCIe tunnel is created to a usb controller in a
              thunderbolt dock; no other downstream PCIe tunnels are
              authorized (needs 4.17 kernel and recent hardware).

The Linux kernel, starting with version 4.13, provides an interface via
sysfs that enables userspace query the security level, the status of
connected devices and, most importantly, to authorize devices, if the
security level demands it.

boltd - the system daemon
-------------------------

The core of bolt is a system daemon (`boltd`) that interfaces with
sysfs and exposes devices via D-Bus to clients. It also has a database
of previously authorized devices (and their keys) and will, depending
on the policy set for the individual devices, automatically authorize
newly connected devices without user interaction. The daemon supports
syncing the devices database with the pre-boot access control list
firmware feature. It also adapts its behavior when iommu support is
detected.

boltctl - command line client
-----------------------------
The `boltctl` command line can be used to manage thunderbolt devices
via `boltd`.  It can list devices, monitor changes and initiate
authorization of devices.


Installation
============

The [meson][meson] build system is used to configure and compile bolt.


    meson build           # configure bolt, use build as buildir
    ninja -C build        # compile it
    ninja -C build test   # run the tests

See [INSTALL][install] for more information, [BUGS][bugs] for how to
file issues and [HACKING][hacking] how to contribute.


[meson]: http://mesonbuild.com/
[install]: INSTALL.md
[bugs]: BUGS.md
[hacking]: HACKING.md
