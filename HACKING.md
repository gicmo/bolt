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
