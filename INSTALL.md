BUILDING
========

The [meson][meson] build system is used to configure and compile bolt.


    meson build           # configure bolt, use build as buildir
    ninja -C build        # compile it
    ninja -C build test   # run the tests


NB: `boltd` comes with configuration files for dbus and PolicyKit that
need to be installed to the proper locations. It is probably a good
idea to manually specify them with the correct values for the current
distribution. This can be done by passing the corresponding options
to meson:

    --sysconfdir=/etc
	--localstatedir=/var
	--sharedstatedir=/var/lib


[meson]: http://mesonbuild.com/
