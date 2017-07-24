project('tbt', 'c')

libexecdir = join_paths(get_option('prefix'), get_option('libexecdir'))
udevrulesdir = '/etc/udev/rules.d'

compiler = meson.get_compiler('c')

glib  = dependency('glib-2.0')
gio   = dependency('gio-2.0')
gudev = dependency('gudev-1.0')

common_deps = [glib, gio, gudev]

common_sources = [
  'lib/device.h', 'lib/device.c',
  'lib/store.h', 'lib/store.c',
  'lib/manager.h', 'lib/manager.c'
]

tests = [
  ['test_store', common_sources]
]

executable('tbtctl',
	   sources: [
	     common_sources,
	     'ctl.c'
	   ],
	   include_directories: [include_directories('lib')],
	   dependencies: common_deps,
	   install: true)


executable('tbtauth',
	   sources: [
	     common_sources,
	     'auth.c'
	   ],
	   include_directories: [include_directories('lib')],
	   dependencies: common_deps,
	   install: true,
	   install_dir: libexecdir)

executable('tbtdb',
	   sources: [
	     common_sources,
	     'db.c'
	   ],
	   include_directories: [include_directories('lib')],
	   dependencies: common_deps,
	   install: true,
	   install_dir: libexecdir)

substs = configuration_data()
substs.set('libexecdir', libexecdir)

gen = configure_file(input: 'tbt.rules.in',
		     output: 'tbt.rules',
		     configuration: substs)

install_data(gen, install_dir: udevrulesdir)


foreach t: tests
  test_name = t.get(0)
  test_srcs = ['tests/@0@.c'.format(test_name), t.get(1, [])]
  executable(test_name, test_srcs,
	     include_directories: [include_directories('lib')],
             dependencies: common_deps)
endforeach
