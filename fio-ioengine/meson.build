cc = meson.get_compiler('c')
rt_dep = cc.find_library('rt', required: true)
pconf = import('pkgconfig')
fs = import('fs')

fio_dir = get_option('fio_source_dir')
if not fs.exists(fio_dir)
    error('fio source directory does not exist!')
elif not fs.is_dir(fio_dir)
    error('fio_source_dir does not point to a directory!')
elif not fs.exists(fio_dir / 'config-host.h')
    error('no `config-host.h` file in fio_source_directory, run `./configure` in fio dir and re-try!')
endif

flexallocfioe = shared_module(
  meson.project_name() + '-fio-engine',
  ['flexalloc.c'],
  override_options: ['c_std=gnu11'],
  include_directories: [
      libflexalloc_header_dirs,
      include_directories(fio_dir),
      ],
  dependencies: [rt_dep],
  link_with: library.get_static_lib(),
  install: true,
  c_args: [
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