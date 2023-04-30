
flan_files = files('flan.c', 'flan_md.c')
#flan_fuse_files = files('flan_fuse.c')

library = both_libraries('flan',
  fla_common_files, flan_files, libflexalloc_files, xnvme_env_files,
  fla_util_files,
  dependencies: xnvme_deps,
  build_rpath: xnvme_lib_dir,
  install_rpath: xnvme_lib_dir,
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

executable('flan_test_write_over_obj_size', 'test_write_over_obj_size.c',
  fla_common_files, flan_files, libflexalloc_files, xnvme_env_files,
  fla_util_files,
  dependencies: xnvme_deps,
  include_directories : libflexalloc_header_dirs
  )

executable('flan_test_rename_write', 'test_rename_write.c',
  fla_common_files, flan_files, libflexalloc_files, xnvme_env_files,
  fla_util_files,
  dependencies: xnvme_deps,
  include_directories : libflexalloc_header_dirs
  )

executable('flan_test_unaligned_read', 'test_unaligned_read.c',
  fla_common_files, flan_files, libflexalloc_files, xnvme_env_files,
  fla_util_files, flexalloc_testing,
  dependencies: xnvme_deps,
  include_directories : libflexalloc_header_dirs
  )

executable('flan_print', 'flan_print.c',
  fla_common_files, flan_files, libflexalloc_files, xnvme_env_files,
  fla_util_files, flexalloc_testing,
  dependencies: xnvme_deps,
  include_directories : libflexalloc_header_dirs,
  install : true)

#fuse_deps = dependency('fuse3')
#executable('flan_fuse', flan_fuse_files,
#  fla_common_files,  flan_files, libflexalloc_files, xnvme_env_files,
#  fla_util_files,
#  dependencies: [xnvme_deps, fuse_deps],
#  include_directories : libflexalloc_header_dirs,
#  override_options: ['c_std=gnu11'],
#  )

install_headers('flan.h', 'flan_md.h')
