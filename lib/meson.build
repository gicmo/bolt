common_deps = [glib, gio, gudev, unix]

common_headers = [
  'ioutils.h',
  'device.h',
  'store.h',
  'manager.h'
]

common_sources = [
  'ioutils.c',
  'device.c',
  'store.c',
  'manager.c'
]

common_enums = gnome.mkenums('enums',
			     sources: common_headers,
			     c_template: 'enums.c.tpl',
			     h_template: 'enums.h.tpl')

gen_sources = [common_enums[0]]
gen_headers = [common_enums[1]]

common_lib = static_library('tbt',
			    sources: common_sources + gen_sources + gen_headers,
			    dependencies: common_deps,
			    include_directories: [include_directories('.')])

tbt = declare_dependency(sources: gen_headers,
			 dependencies: common_deps,
			 link_with: common_lib,
			 include_directories: include_directories('.'))
