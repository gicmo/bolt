project('tbt', 'c')

# get suported warning flags
test_args = [
  '-fstack-protector-strong',
  '-Waggregate-return',
  '-Wunused',
  '-Warray-bounds',
  '-Wcast-align',
  '-Wclobbered',
  '-Wdeclaration-after-statement',
  '-Wempty-body',
  '-Wformat=2',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wformat-signedness',
  '-Wignored-qualifiers',
  '-Wimplicit-function-declaration',
  '-Winit-self',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wno-discarded-qualifiers',
  '-Wno-missing-field-initializers',
  '-Wno-strict-aliasing',
  '-Wno-suggest-attribute=format',
  '-Wno-unused-parameter',
  '-Wold-style-definition',
  '-Woverride-init',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wreturn-type',
  '-Wshadow',
  '-Wsign-compare',
  '-Wstrict-aliasing',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wtype-limits',
  '-Wundef',
  '-Wuninitialized',
  '-Wunused-but-set-variable',
  '-Wwrite-strings'
]

compiler = meson.get_compiler('c')

foreach arg: test_args
  if compiler.has_argument(arg)
    add_project_arguments(arg, language : 'c')
  endif
endforeach

libexecdir = join_paths(get_option('prefix'), get_option('libexecdir'))
udevrulesdir = '/etc/udev/rules.d'

gnome = import('gnome')



glib  = dependency('glib-2.0')
gio   = dependency('gio-2.0')
unix  = dependency('gio-unix-2.0')
gudev = dependency('gudev-1.0')
umock = dependency('umockdev-1.0')

tests = [
  ['test_lib']
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
	     dependencies: [tbt, umock])
endforeach
