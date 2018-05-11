Patches
=======

Patches should be submitted in the form of pull requests at
[github][github].


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

Static analysis
===============

The clang static analyzer can be run locally via:

    ninja -C <buildir> scan-build

Coverity
--------

Bolt is registerd with [coverity][coverity]. To submit a local build,
execute the following commands (the `cov-build` [build tool][cov-build]
must be in `PATH`) from the source directory:

	meson coverity
	cov-build --dir cov-int ninja -C coverity
	tar caf bolt.xz cov-int

Upload the `bolt.xz` file to coverity for analysis. Fix defects. Profit.

[github]: https://github.com/gicmo/bolt
[coverity]: https://scan.coverity.com/projects/bolt
[cov-build]: https://scan.coverity.com/download
[valgrind]: https://gist.github.com/gicmo/327dad149fcb386ac7f59e279b8ba322
