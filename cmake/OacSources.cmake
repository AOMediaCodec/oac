if(__oac_sources)
  return()
endif()
set(__oac_sources INCLUDED)

include(OacFunctions)

get_oac_sources(SILK_HEAD silk_headers.mk silk_headers)
get_oac_sources(SILK_SOURCES silk_sources.mk silk_sources)
get_oac_sources(SILK_SOURCES_FLOAT silk_sources.mk silk_sources_float)
get_oac_sources(SILK_SOURCES_FIXED silk_sources.mk silk_sources_fixed)
get_oac_sources(SILK_SOURCES_X86_RTCD silk_sources.mk silk_sources_x86_rtcd)
get_oac_sources(SILK_SOURCES_SSE4_1 silk_sources.mk silk_sources_sse4_1)
get_oac_sources(SILK_SOURCES_FIXED_SSE4_1 silk_sources.mk
                 silk_sources_fixed_sse4_1)
get_oac_sources(SILK_SOURCES_AVX2 silk_sources.mk silk_sources_avx2)
get_oac_sources(SILK_SOURCES_FLOAT_AVX2 silk_sources.mk silk_sources_float_avx2)
get_oac_sources(SILK_SOURCES_ARM_RTCD silk_sources.mk silk_sources_arm_rtcd)
get_oac_sources(SILK_SOURCES_ARM_NEON_INTR silk_sources.mk
                 silk_sources_arm_neon_intr)
get_oac_sources(SILK_SOURCES_FIXED_ARM_NEON_INTR silk_sources.mk
                 silk_sources_fixed_arm_neon_intr)

get_oac_sources(OAC_HEAD oac_headers.mk oac_headers)
get_oac_sources(OAC_SOURCES oac_sources.mk oac_sources)
get_oac_sources(OAC_SOURCES_FLOAT oac_sources.mk oac_sources_float)

get_oac_sources(CELT_HEAD celt_headers.mk celt_headers)
get_oac_sources(CELT_SOURCES celt_sources.mk celt_sources)
get_oac_sources(CELT_SOURCES_X86_RTCD celt_sources.mk celt_sources_x86_rtcd)
get_oac_sources(CELT_SOURCES_SSE celt_sources.mk celt_sources_sse)
get_oac_sources(CELT_SOURCES_SSE2 celt_sources.mk celt_sources_sse2)
get_oac_sources(CELT_SOURCES_SSE4_1 celt_sources.mk celt_sources_sse4_1)
get_oac_sources(CELT_SOURCES_AVX2 celt_sources.mk celt_sources_avx2)
get_oac_sources(CELT_SOURCES_ARM_RTCD celt_sources.mk celt_sources_arm_rtcd)
get_oac_sources(CELT_SOURCES_ARM_ASM celt_sources.mk celt_sources_arm_asm)
get_oac_sources(CELT_AM_SOURCES_ARM_ASM celt_sources.mk
                 celt_am_sources_arm_asm)
get_oac_sources(CELT_SOURCES_ARM_NEON_INTR celt_sources.mk
                 celt_sources_arm_neon_intr)
get_oac_sources(CELT_SOURCES_ARM_NE10 celt_sources.mk celt_sources_arm_ne10)

get_oac_sources(DEEP_PLC_HEAD lpcnet_headers.mk deep_plc_headers)
get_oac_sources(DRED_HEAD lpcnet_headers.mk dred_headers)
get_oac_sources(OSCE_HEAD lpcnet_headers.mk osce_headers)
get_oac_sources(DEEP_PLC_SOURCES lpcnet_sources.mk deep_plc_sources)
get_oac_sources(DRED_SOURCES lpcnet_sources.mk dred_sources)
get_oac_sources(OSCE_SOURCES lpcnet_sources.mk osce_sources)
get_oac_sources(DNN_SOURCES_X86_RTCD lpcnet_sources.mk dnn_sources_x86_rtcd)
get_oac_sources(DNN_SOURCES_SSE2 lpcnet_sources.mk dnn_sources_sse2)
get_oac_sources(DNN_SOURCES_SSE4_1 lpcnet_sources.mk dnn_sources_sse4_1)
get_oac_sources(DNN_SOURCES_AVX2 lpcnet_sources.mk dnn_sources_avx2)
get_oac_sources(DNN_SOURCES_NEON lpcnet_sources.mk dnn_sources_arm_neon)
get_oac_sources(DNN_SOURCES_DOTPROD lpcnet_sources.mk dnn_sources_arm_dotprod)
get_oac_sources(DNN_SOURCES_ARM_RTCD lpcnet_sources.mk dnn_sources_arm_rtcd)

get_oac_sources(oac_demo_SOURCES Makefile.am oac_demo_sources)
get_oac_sources(olac_demo_SOURCES Makefile.am olac_demo_sources)
get_oac_sources(oac_custom_demo_SOURCES Makefile.am oac_custom_demo_sources)
get_oac_sources(oac_compare_SOURCES Makefile.am oac_compare_sources)
get_oac_sources(tests_test_oac_api_SOURCES Makefile.am test_oac_api_sources)
get_oac_sources(tests_test_oac_encode_SOURCES Makefile.am
                 test_oac_encode_sources)
get_oac_sources(tests_test_oac_extensions_SOURCES Makefile.am
                 test_oac_extensions_sources)
get_oac_sources(tests_test_oac_decode_SOURCES Makefile.am
                 test_oac_decode_sources)
get_oac_sources(tests_test_oac_padding_SOURCES Makefile.am
                 test_oac_padding_sources)
get_oac_sources(tests_test_oac_dred_SOURCES Makefile.am
                 test_oac_dred_sources)
get_oac_sources(tests_test_oac_custom_SOURCES Makefile.am
                 test_oac_custom_sources)
