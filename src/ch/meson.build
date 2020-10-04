ch_driver_sources = [
  'ch_conf.c',
  'ch_conf.h',
  'ch_domain.c',
  'ch_domain.h',
  'ch_driver.c',
  'ch_driver.h',
  'ch_monitor.c',
  'ch_monitor.h',
  'ch_process.c',
  'ch_process.h',
]

driver_source_files += files(ch_driver_sources)

stateful_driver_source_files += files(ch_driver_sources)

if conf.has('WITH_CH')
  ch_driver_impl = static_library(
    'virt_driver_ch_impl',
    [
      ch_driver_sources,
    ],
    dependencies: [
      access_dep,
      curl_dep,
      log_dep,
      src_dep,
    ],
    include_directories: [
      conf_inc_dir,
    ],
    link_with: [
      virt_util_lib,
    ]
  )

  virt_modules += {
    'name': 'virt_driver_ch',
    'link_whole': [
      ch_driver_impl,
    ],
    'link_args': [
      libvirt_no_undefined,
    ],
  }
endif
