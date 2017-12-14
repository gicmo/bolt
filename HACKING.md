Patches
=======

Patches should be submitted in the form of pull requests at
[github][github].


Coding style
============

Run `contrib/uncrustify.sh` to format the source code before submitting
pull requests.


Static analysis
===============

The clang static analyzer can be run locally via:

    ninja -C <buildir> scan-build


[github]: https://github.com/gicmo/bolt
