project('tbt', 'c')

libexecdir = join_paths(get_option('prefix'), get_option('libexecdir'))
udevrulesdir = '/etc/udev/rules.d'

gnome = import('gnome')

compiler = meson.get_compiler('c')

glib  = dependency('glib-2.0')
gio   = dependency('gio-2.0')
gudev = dependency('gudev-1.0')

tests = [
  ['test_store']
]

subdir('lib')

executable('tbtctl',
	   sources: [
	     'ctl.c'
	   ],
	   dependencies: tbt,
	   install: true)


executable('tbtauth',
	   sources: [
	     'auth.c'
	   ],
	   dependencies: tbt,
	   install: true,
	   install_dir: libexecdir)

executable('tbtdb',
	   sources: [
	     'db.c'
	   ],
	   dependencies: tbt,
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
	     dependencies: tbt)
endforeach
