cc = meson.get_compiler('c')
rt_dep = cc.find_library('rt', required: true)

flexallocfioe = shared_module(
  meson.project_name() + '-fio-engine',
  ['flexalloc.c'],
  include_directories: [libflexalloc_header_dirs],
  dependencies: [rt_dep, fio_dep],
  link_with: library.get_static_lib(),
  install: true,
  c_args: [
    '-std=gnu11',
    '-g',
    '-Wall',
    '-D_GNU_SOURCE',
    '-include', 'config-host.h'
  ]
)

pconf.generate(
  libraries: [ flexallocfioe ],
  version: meson.project_version(),
  variables: [
    'datadir=' + get_option('prefix') / get_option('datadir') / meson.project_name()
  ],
  name: 'flexalloc-fioe',
  filebase: meson.project_name() + '-fio-engine',
  description : 'flexalloc fio I/O engine'
)
