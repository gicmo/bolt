project('bolt',
  'c',
  version: '0.1',
  license : 'LGPL-2.1+',
  meson_version: '>= 0.42.0',
  default_options: ['warning_level=1',
                    'c_std=gnu99',
                    'buildtype=debugoptimized'])


# additional compiler warnings, if supported
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


# dependencies

gnome = import('gnome')

glib  = dependency('glib-2.0')
gio   = dependency('gio-2.0')
unix  = dependency('gio-unix-2.0')
udev  = dependency('libudev')
umock = dependency('umockdev-1.0')


# configuration & well known directories

prefixdir = get_option('prefix')

bindir = join_paths(prefixdir, get_option('bindir'))
libexecdir = join_paths(prefixdir, get_option('libexecdir'))
datadir = join_paths(prefixdir, get_option('datadir'))
sysconfdir = get_option('sysconfdir')

dbdir = get_option('db-path')

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('DATADIR', datadir)
conf.set_quoted('BOLT_DBDIR', dbdir)

conf.set('_GNU_SOURCE', true)

foreach fn : [
  ['explicit_bzero', '''#include <string.h>'''],
  ['getrandom',      '''#include <sys/random.h>''']
]

  have = compiler.has_function(fn[0], prefix : fn[1])
  conf.set10('HAVE_FN_' + fn[0].to_upper(), have)
endforeach

config_h = configure_file(output: 'config.h', configuration: conf)

cargs = ['-DG_LOG_DOMAIN="Bolt"']


# common static library
#  contains code shared by daemon, command line tools
common_deps = [glib, gio, udev, unix]

common_headers = [
  'common/bolt-enums.h',
  'common/bolt-error.h'
]

common_sources = [
  'common/bolt-enums.c',
  'common/bolt-error.c',
  'common/bolt-fs.c',
  'common/bolt-io.c',
  'common/bolt-rnd.c',
  'common/bolt-str.c',
  'common/bolt-term.c'
]

common_enums = gnome.mkenums_simple('bolt-enum-types',
  sources: common_headers)

gen_sources = [common_enums[0]]
gen_headers = [common_enums[1]]

common_lib = static_library('common',
  c_args : [cargs],
  sources: common_sources + gen_sources + gen_headers,
  dependencies: common_deps,
  include_directories: [
    include_directories('.')
])

common = declare_dependency(
  sources: gen_headers,
  dependencies: common_deps,
  link_with: common_lib,
  include_directories: [
    include_directories('common')
])


# boltd - the main daemon
daemon_sources = files([
  'boltd/bolt-auth.c',
  'boltd/bolt-manager.c',
  'boltd/bolt-device.c',
  'boltd/bolt-key.c',
  'boltd/bolt-store.c'
])

daemon_sources += gnome.gdbus_codegen('bolt-gdbus',
  'dbus/org.freedesktop.Bolt.xml',
  interface_prefix: 'org.freedesktop.Bolt1.',
  namespace: 'BoltDBus')

install_data(['dbus/org.freedesktop.Bolt.xml'],
  install_dir : join_paths(datadir, 'dbus-1', 'interfaces')
)

install_data(['dbus/org.freedesktop.Bolt.conf'],
  install_dir : join_paths(sysconfdir, 'dbus-1', 'system.d')
)

daemon_deps = [
  glib,
  gio,
  unix,
  udev,
  common
]

daemon_library = static_library('daemon',
  c_args : [cargs],
  sources: daemon_sources,
	dependencies: daemon_deps,
	include_directories: [
    include_directories('boltd')
])

libdaemon = declare_dependency(
  dependencies: daemon_deps,
	link_with: [daemon_library],
	include_directories: [
    include_directories('boltd')
])


executable('boltd',
  ['boltd/bolt-daemon.c'],
  dependencies: [libdaemon],
  c_args : [
    cargs,
  ],
  install: true,
  install_dir: libexecdir)


# command line tools

executable('boltctl',
   ['cli/bolt-client.c',
    'cli/bolt-device.c',
    'cli/bolt-proxy.c',
    'cli/boltctl.c'],
  dependencies: [glib,
                 gio,
                 unix,
                 common],
  c_args : [
    cargs,
  ],
  install: true,
  install_dir: bindir)


# testing

tests = [
  ['test-common'],
  ['test-store', [libdaemon]]
]

foreach t: tests
  test_name = t.get(0)
  test_deps = [common] + t.get(1, [])
  test_srcs = ['tests/@0@.c'.format(test_name), t.get(2, [])]
  e = executable(test_name, test_srcs, dependencies: test_deps)

  test_env = environment()
  test_env.prepend('LD_PRELOAD', 'libumockdev-preload.so.0')
  test(test_name, e, env: test_env, timeout : 120)

endforeach

msg = '  database path:       @0@'.format(dbdir)
message('\n' + msg)
