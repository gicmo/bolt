common_deps = [glib, gio, gudev]

common_headers = [
  'device.h',
  'store.h',
  'manager.h'
]

common_sources = [
  'device.c',
  'store.c',
  'manager.c'
]

common_lib = static_library('tbt',
			    sources: common_sources,
			    dependencies: common_deps,
			    include_directories: [include_directories('.')])

tbt = declare_dependency(dependencies: common_deps,
			 link_with: common_lib,
			 include_directories: include_directories('.'))
