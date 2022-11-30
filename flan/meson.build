
flan_files = files('flan.c')
flan_fuse_files = files('flan_fuse.c')

fuse_deps = dependency('fuse3')
library = both_libraries('flan',
  fla_common_files, flan_files, libflexalloc_files, xnvme_env_files,
  fla_util_files,
  dependencies: xnvme_deps,
  include_directories : libflexalloc_header_dirs,
  install : true)

executable('flan_rw_test', 'rw_test.c',
  fla_common_files, flan_files, libflexalloc_files, xnvme_env_files,
  fla_util_files,
  dependencies: xnvme_deps,
  include_directories : libflexalloc_header_dirs
  )

executable('flan_bw_test', 'bw_test.c',
  fla_common_files, flan_files, libflexalloc_files, xnvme_env_files,
  fla_util_files,
  dependencies: xnvme_deps,
  include_directories : libflexalloc_header_dirs
  )

executable('flan_test', 'test.c',
  fla_common_files, flan_files, libflexalloc_files, xnvme_env_files,
  fla_util_files,
  dependencies: xnvme_deps,
  include_directories : libflexalloc_header_dirs
  )

executable('flan_fuse', flan_fuse_files,
  fla_common_files,  flan_files, libflexalloc_files, xnvme_env_files,
  fla_util_files,
  dependencies: [xnvme_deps, fuse_deps],
  include_directories : libflexalloc_header_dirs,
  override_options: ['c_std=gnu11'],
  )

install_headers('flan.h')
