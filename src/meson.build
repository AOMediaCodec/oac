oac_sources = sources['OAC_SOURCES']

oac_sources_float = sources['OAC_SOURCES_FLOAT']

if not disable_float_api
  oac_sources += oac_sources_float
endif

oac_lib_c_args = []
if host_machine.system() == 'windows'
  oac_lib_c_args += ['-DDLL_EXPORT']
endif

oac_lib = library('oac',
  oac_sources,
  version: libversion,
  darwin_versions: macosversion,
  c_args: oac_lib_c_args,
  include_directories: oac_includes,
  link_whole: [celt_lib, silk_lib, dnn_lib],
  dependencies: libm,
  install: true)

oac_dep = declare_dependency(link_with: oac_lib,
  include_directories: oac_public_includes)

# Extra uninstalled Oac programs
if not extra_programs.disabled()
  foreach prog : ['oac_compare', 'oac_demo', 'repacketizer_demo']
    executable(prog, '@0@.c'.format(prog),
               include_directories: oac_includes,
               link_with: oac_lib,
               dependencies: libm,
               install: false)
  endforeach

  executable('olac_demo',
             ['../olac/olac_demo.c',
              '../olac/olac.c',
              '../olac/olac_encoder.c',
              '../olac/olac_decoder.c',
              '../olac/olac_lpc.c',
              '../celt/entenc.c',
              '../celt/entdec.c',
              '../celt/entcode.c'],
             include_directories: oac_includes,
             dependencies: libm,
             install: false)

  if opt_custom_modes
    executable('oac_custom_demo', '../celt/oac_custom_demo.c',
               include_directories: oac_includes,
               link_with: oac_lib,
               dependencies: libm,
               install: false)
  endif

endif
