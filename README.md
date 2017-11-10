bolt
====

Userpsace system daemon to enable security levels for *Thunderbolt™ 3*
on GNU/Linux.

Introduction
------------

Thunderbolt™ is the brand name of a hardware interface developed by
Intel that allows the connection of external peripherals to a
computer.

Devices connected via Thunderbolt can be DMA masters and thus read
system memory without interference of the operating system (or even
the CPU).  Version 3 of the interface provides 4 different security
levels, in order to mitigate the aforementioned security risk that
connected devices pose to the system. The security level is set by the
system firmware.

The four security levels are:

 * `none`:   Security disabled, all devices will fully functional
             on connect.
 * `dponly`: Only pass the display-port stream through to the
             connected device.
 * `user`:   Connected devices need to be manually authorized by
             the user.
 * `secure`: As 'user', but also challenge the device with a secret
             key to verify its identity.

The Linux kernel, starting with version 4.13, provides a interface via
sysfs that enables userspace query the security level, the status of
connected devices and, most importantly, to authorize devices, if the
security level demands it.

boltd - the system daemon
-------------------------

The core of bolt is a system daemon (`boltd`) that interfaces with
sysfs and exposes devices via D-Bus to clients. It also has a database
of previously authorized devices (and their keys) and will, depending
on the policy set for the individual devices, automatically authorize
newly connected devices without user interaction.

boltctl - command line client
-----------------------------
The `boltctl` command line can be used to manage thunderbolt devices
via `boltd`.  It can list devices, monitor changes and initiate
auhtorization of devices.
