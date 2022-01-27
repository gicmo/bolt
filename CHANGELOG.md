Version 0.9.2
-------------
_Please get along_
Released: 2022-01-27

* This release is compatible with umockdev >= 0.16.3; there was a change in
  umockdev that made our test fail with it, since both our test and umockev
  were trying to create the same directorires. bolt now allows for the dir
  to already exist.

* The license for `90-bolt.rules` has changed from `GPL-2.1+`, which does
  not exist and was probably was confused with `LGPL-2.1+`, to `GPL 2.0+`.

* Documentation has been updated and spelling mistakes fixed.

* Various improvements for continuous integration.

* The minimum required version of meson has been bumped to 0.46.0.


Version 0.9.1
-------------
_Unstable icy waters_
Released: 2020-11-30

* Bug fixes for integrated thunderbolt controllers:
  On Ice Lake, the Thunderbolt 3 i/o subsystem is fully integrated into the die.
  As a side effect it does not have a DROM, which means the host udev device
  does not have the device and vendor name and id attributes.
  Additionally the `unique_id` of said host controller changes with every boot,
  which breaks one of the fundamental assumptions in `boltd`. Therefore a number
  of bug fixes were necessary to properly support this new architecture:

  - Don't store domains where uuids change across reboots [!220]
  - Fixes for the journal and the domain's acl-log [!221]
  - Version the store and use that to clean up stale domains once [!226, !231]
  - Host identification for embedded thunderbolt controllers [!233]

* Various other small bug fixes and memory leak fixes.


Version 0.9
-----------
_Four comes after Three_
Released: 2020-06-15

* New Features:
  - Add 'Generation' attribute for the Manager [!197]
  - Ability to change the policy of a stored device [!202]
  - The BootACL Domain property is now writable [!184]
  - Support for systemd's service watchdog [!185]
  - Expose Link Speed sysfs attributes [!214]

* Improvements:
  - boltclt: show timestamps in 'monitor' call [!208]
  - Persist the host device [!194]

* Bug fixes:
  - Fix a flaky test [!217, #161]
  - Plug small memory leaks in error conditions [!217]
  - Ignore spurious wakeup device uevents for probing [!209]
  - Preserve keystate when updating devices [!192]


Version 0.8
-----------
_I owe it to the MM U!_
Released: 2019-06-14

* New Features:
  - **IOMMU support**: adapt behavior iommu support is present and active [#128]
    - automatically enroll new devices with the new `iommu` policy when iommu is active
    - automatically authorize devices with the `iommu` policy if iommu is active
  - `boltctl config` command to describe, get and set global, device and domain properties.
  - Chain authorization and enrollment via `boltctl {enroll, authorize} --chain` [!153, !154]
  - `bolt-mock` script for interactively testing `boltd` [!152]

* Improvements:
  - Automatically import devices that were authorized at boot [#137]
  - Make tests installable [#140]
  - Honour `STATE_DIRECTORY` [!159] and `RUNTIME_DIRECTORY` [!161]
  - Profiling support via gprof [!168]

* Bug fixes:
  - Better handling of random data generation [#132, !165]
  - Fix double free in case of client creation failure [!148]
  - Fix invalid format string in warning [!14]

* NB for packagers:
  - The dbus configuration is now installed in `$datadir/dbus-1/system.d` instead of `$sysconfdir` [!177].
  - To install tests, configure with `-Dinstall-tests=true`.


Version 0.7
-----------
_The Known Unknowns_
Released: 2019-01-01


* Features:
  - announce status to systemd via sd_notify (using a simple custom implementation) [!143]

* Bug fixes:
  - properly update global security level status [#131 via !141]
  - adapt to `systemd` 240 not sending `bind`/`unbind` uevents [#133 via !145]
  - fix compilation on musl [#126 via !140]
  - daemon: use `g_unix_signal_source…` to catch signals [#127, #129 via !138]

* Improvements
  - precondition checks cleanup and completion [#124 via !139]
  - error cleanup [#125, !142]
  - fix some leaks and issues uncovered by coverity [!144]


Version 0.6
-----------
_Make the firmware do it!_
Released: 2018-11-28

* New Features:
  - **pre-boot access control list, aka. `BootACL`** support [!119]
    - domains objects are now persistent
      - new `Uid` (dbus) / `uid` (object) property derived from the uuid of the device representing the root switch
      - `sysfs` and `id` attribute will be set/unset on connects and disconnects
      - domains are now stored in the boltd database
    - domains got the `BootACL` (dbus) / `bootacl` (object) property
      - uuids can be added, removed or set in batch
      - when domain is *online*: changes are written to the sysfs `boot_acl` attribute directly
      - when domain is *offline*: changes are written to a journal and then reapplied in order when the domain is connected
    - newly enrolled devices get added to all bootacls of all domains *if* the `policy` is `BOLT_POLICY_AUTO`
    - removed devices get deleted from all bootacls of all domains
    - `boltacl domain` command will show the bootacl slots and their content

  - `boltctl` gained the `-U, --uuid` option, to control how uuids are printed [!124]

* Improvements and fixes:
  - Testing [!127]
    - The test coverage increased to `84.80%` overall and to `90.0%` for the `boltd` source
    - Coverage is reported for merge requests via the fedora ci image [!126]
    - `boltctl` is now included in the tests [!132]
    - Fedora 29 is used for the fedora ci image

  - Bugs and robustness:
    - The device state is verified in `Device.Authorize` [!120]
    - Handle empty 'keys' sysfs device attribute [!129]
    - Properly adjust policies when enrolling already authorized devices [!136]
    - Fix potential crash when logging assertions `g_return_if_fail` [!121]


Version 0.5
-----------
_You've got the Power_
Released: 2018-09-28

* New Features:

  - Force-Power DBus API ⚡(!101)
    - A new interface to boltd to control the (force) power mechanism (#106)
    - Switch off power with a delay so we don't run into races (#104)
  - Add representation of thunderbolt domains<br>
    This is a preparation for the boot acl support
  - Authorizing devices, after upgrading from `USER` to `SECURE` security level, will lead to key upgrades (!107)
  - Connection and Authorization times are now stored (!105)
  - Systemd dependency is now optional (!106, !103)
  - Company and brand names are cleaned up for the display name (#102)


* Bug fixes and cleanups:

  - Emit proper notification for security-level property changes (!100)
  - Auto generate the object path for BoltDevice (!102)


* NB for packagers:

  - `-Ddb-path` is **DEPRECATED**, use `-Ddb-name` instead (!113)
  - meson >= 0.44.0 is required.
  - systemd unit files got updated:
    - `After=polkit.service` (!116)
    - Use systemd for runtime and state directory management (!113)
    - Sandbox is tightened (!97)


Version 0.4
-----------
_The Race Is Over_
Released: 2018-05-28

* New features:
  - auto import of devices authorized during boot [!90]
  - allow enrolling of already authorized devices, i.e. importing of devices [!86]
  - label new devices and detect duplicates [!91]

* Be more robust:
  - Handle NULL errors in logging code better [!89]
  - Properly handle empty device database entries [!87]
  - Better authentication errors and logging [!85]
  - More tests

* Internal changes:
  - Make sure we don't miss device status changes [!82]
  - Rework property change notification dispatching [!83]


Version 0.3
-----------
_Capture The Flags_
Released: 2018-05-28

* Prepare for upcoming kernel changes:
  - Support for `usbonly` (SL4) security level (#75)
  - Support for `boot` sysfs device attribute (#76)

* DBus API changes:
  - `BoltStatus` was split (#81), so that:
      - `Device.Status` does not report `authorized-xxx` anymore
      - `Device.AuthFlags` added to indicate auth details, e.g. `secure`, `nopci`, `boot`, `nokey` (#76)
  - `BoltSecurity` and thus `Manager.SecurityLevel` can report `usbonly` (#75)

* client/boltctl:
  - async versions for many function calls
  - more efficient getters, resulting in reduced allocations
  - boltctl reports `Device.AuthFlags`
  - boltctl prints more and better version info via `boltctl monitor`

* Other bugfixes and improvements include:
  - more robust flags/enum conversion


Version 0.2
-----------
_I broke the Bus_
Released: 2018-03-06

Lots of changes, the most significant:

- database location moved (now in `/var/lib/boltd`)
  - **⚠** devices enrolled with bolt 0.1 need to be re-enrolled (or the database moved from the old location)

- DBus API changed (lots of strings)

- Enums are transmitted as strings
  - `Device.Security` property is gone; replaced by `authorized-dponly` status and `Manager.SecurityLevel` ( #37, #38, #62)
  - Various timestamps got added: `Device.ConnectTime`, `Device.StoreTime` and `Device.AuthorizeTime` (#46  #57)
  - `Device.Label` (readwrite) was added so devices can be given custom names (#46)
  - `Device.Type` added, to differentiate between host and peripherals
  - `Manager.AuthMode` (readwrite) was added to control (auto) authorization (#48)

Other bugfixes and improvements include:

- Ensure we get a `DeviceAdded` signal on startup (#58)
 - Support for legacy devices that have no key sysfs attribute (#67)
 - Use structured logging and avoid printing UUIDs in non-debug log code (#36 #60)
 - Other internal restructuring for cleaner code (#43)


Version 0.1
-----------
_Accidentally Working_
Released: 2017-12-13

* functional daemon that can authorize enroll and authorize devices
* `boltctl` command to interact with the daemon
