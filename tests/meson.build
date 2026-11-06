# Tests that link to liboac
oac_tests = [
  ['test_oac_api'],
  ['test_oac_decode', [], 120],
  ['test_oac_encode', 'oac_encode_regressions.c', 240],
  ['test_oac_extensions', [], 120],
  ['test_oac_lossless'],
  ['test_oac_padding'],
  ['test_oac_projection'],
]

if opt_dred.enabled()
  oac_tests += [['test_oac_dred', [], 60 * 20]]
endif

foreach t : oac_tests
  test_name = t.get(0)
  extra_srcs = t.get(1, [])

  test_kwargs = {}
  if t.length() > 2
    test_kwargs += {'timeout': t[2]}
  endif

  exe_kwargs = {}
  # This test uses private symbols
  if test_name == 'test_oac_projection' or test_name == 'test_oac_extensions'
    exe_kwargs = {
      'link_with': [celt_lib, silk_lib, dnn_lib],
      'objects': oac_lib.extract_all_objects(),
    }
  endif

  exe = executable(test_name, '@0@.c'.format(test_name), extra_srcs,
    include_directories: oac_includes,
    dependencies: [libm, oac_dep],
    install: false,
    kwargs: exe_kwargs)
  test(test_name, exe, kwargs: test_kwargs)
endforeach
