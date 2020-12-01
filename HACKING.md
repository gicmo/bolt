Patches
=======

Patches should be submitted in the form of merge requests at
[gitlab][gitlab].


Coding style
============

The style is codified via the supplied `sscripts/uncrustify.cfg` config.
It can be automatically formatted by the `uncrustify` target, i.e. by
invoking `ninja -C <builddir> uncrustify`.
Make sure to format the source code before submitting pull requests.

Testing
=======

To run the test suite in verbose mode:

	meson test -C build --verbose

To run `boltd` from within valgrind for the integration tests set the
environment variable `VALGRIND`. A [suppression file][valgrind] can be
specified via that variable as well (n.b. for meson the path must be
relative to the build directory):

	VALGRIND=../bolt.supp meson test -C build --verbose

Coverage
--------

To analyze the current code coverage either `lcov` or `gcovr` need
to be installed. Support must also be enabled during configure time:

	meson -Db_coverage=true <buildir>

This should enable the general `coverage` target as well as the
`coverage-{text, html, xml}` targets:

	ninja -C <builddir> coverage

To manually invoke `gcovr` and exclude the `cli` directory use:

	gcovr -r <builddir> -e cli -s


Address Sanitizer
=================

The Address Sanitizer can be used to detect memory errors, i.e.
memory leaks and undefined behavior. Use `clang` for this, since
there the needed library (`libasan`) is dynamically loaded,
instead of pre-loaded via `LD_PRELOAD` when using `gcc`, which
conflicts with our pre-load needed for `umockdev`.

    env CC=clang meson -Db_sanitize=address,undefined . <builddir>
    ninja -C <builddir> test

NB: There might be a warning that `b_lundef` is needed as well.
It seems to work just fine right now without it.


Static analysis
===============

The clang static analyzer can be run locally via:

    ninja -C <buildir> scan-build

Coverity
--------

Bolt is registered with [coverity][coverity]. To submit a local build,
execute the following commands (the `cov-build` [build tool][cov-build]
must be in `PATH`) from the source directory:

	CC=gcc CXX=gcc meson -Dcoverity=true coverity
	cov-build --dir cov-int ninja -C coverity
	tar caf bolt.xz cov-int

Upload the `bolt.xz` file to coverity for analysis. Fix defects. Profit.

[gitlab]: https://gitlab.freedesktop.org/bolt/bolt
[coverity]: https://scan.coverity.com/projects/bolt
[cov-build]: https://scan.coverity.com/download
[valgrind]: https://gist.github.com/gicmo/327dad149fcb386ac7f59e279b8ba322
