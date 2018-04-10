Bugs
====

Bugs are tracked at [fd.o's gitlab][gitlab issues].


Debugging
=========

The daemon can be run in verbose mode with the command line option `-v`.
This should normally not be necessary, except when debugging the DBus
layer of boltd (the verbose output is indeed very verbose).
To replace the currently running daemon and run a new instance of it
in the forground, launch the daemon with `--replace`:

    boltd --replace


[gitlab issues]: https://gitlab.freedesktop.org/bolt/bolt/issues
