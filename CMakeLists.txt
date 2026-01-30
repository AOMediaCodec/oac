cmake_minimum_required(VERSION 3.16)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(OacPackageVersion)
get_package_version(PACKAGE_VERSION PROJECT_VERSION)

project(Oac LANGUAGES C VERSION ${PROJECT_VERSION})

include(OacFunctions)
include(OacBuildtype)
include(OacConfig)
include(OacSources)
include(GNUInstallDirs)
include(CMakeDependentOption)
include(FeatureSummary)

# Detect clang-cl
set(MSVC_CLANG_CL false BOOL)
if (MSVC)
    if("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
        set(MSVC_CLANG_CL true BOOL)
    endif()
endif()

# Workaround: MSVC's cl (not clang-cl) struggles compiling generated DNN sources (large initializers and some constructs).
# Disable DNN-related features on MSVC as a temporary mitigation until the generated sources are made MSVC-friendly.
if(MSVC AND NOT MSVC_CLANG_CL)
  if(OAC_DNN)
    message(WARNING "Disabling OAC_DNN on MSVC because generated DNN sources may be incompatible with MSVC.")
    set(OAC_DNN FALSE CACHE BOOL "Disable DNN on MSVC" FORCE)
  endif()
  if(OAC_DRED)
    message(WARNING "Disabling OAC_DRED on MSVC because generated DNN sources may be incompatible with MSVC.")
    set(OAC_DRED FALSE CACHE BOOL "Disable DRED on MSVC" FORCE)
  endif()
  if(OAC_OSCE)
    message(WARNING "Disabling OAC_OSCE on MSVC because generated DNN sources may be incompatible with MSVC.")
    set(OAC_OSCE FALSE CACHE BOOL "Disable OSCE on MSVC" FORCE)
  endif()
endif()

set(OAC_BUILD_SHARED_LIBRARY_HELP_STR "build shared library.")
option(OAC_BUILD_SHARED_LIBRARY ${OAC_BUILD_SHARED_LIBRARY_HELP_STR} OFF)
if(OAC_BUILD_SHARED_LIBRARY OR BUILD_SHARED_LIBS OR OAC_BUILD_FRAMEWORK)
  # Global flag to cause add_library() to create shared libraries if on.
  set(BUILD_SHARED_LIBS ON)
  set(OAC_BUILD_SHARED_LIBRARY ON)
endif()
add_feature_info(OAC_BUILD_SHARED_LIBRARY OAC_BUILD_SHARED_LIBRARY ${OAC_BUILD_SHARED_LIBRARY_HELP_STR})

set(OAC_BUILD_TESTING_HELP_STR "build tests.")
option(OAC_BUILD_TESTING ${OAC_BUILD_TESTING_HELP_STR} OFF)
if(OAC_BUILD_TESTING OR BUILD_TESTING)
  set(OAC_BUILD_TESTING ON)
  set(BUILD_TESTING ON)
endif()
add_feature_info(OAC_BUILD_TESTING OAC_BUILD_TESTING ${OAC_BUILD_TESTING_HELP_STR})

set(OAC_CUSTOM_MODES_HELP_STR "enable non-Oac modes, e.g. 44.1 kHz & 2^n frames.")
option(OAC_CUSTOM_MODES ${OAC_CUSTOM_MODES_HELP_STR} OFF)
add_feature_info(OAC_CUSTOM_MODES OAC_CUSTOM_MODES ${OAC_CUSTOM_MODES_HELP_STR})

set(OAC_BUILD_PROGRAMS_HELP_STR "build programs.")
option(OAC_BUILD_PROGRAMS ${OAC_BUILD_PROGRAMS_HELP_STR} OFF)
add_feature_info(OAC_BUILD_PROGRAMS OAC_BUILD_PROGRAMS ${OAC_BUILD_PROGRAMS_HELP_STR})

set(OAC_DISABLE_INTRINSICS_HELP_STR "disable all intrinsics optimizations.")
option(OAC_DISABLE_INTRINSICS ${OAC_DISABLE_INTRINSICS_HELP_STR} OFF)
add_feature_info(OAC_DISABLE_INTRINSICS OAC_DISABLE_INTRINSICS ${OAC_DISABLE_INTRINSICS_HELP_STR})

set(OAC_FIXED_POINT_HELP_STR "compile as fixed-point (for machines without a fast enough FPU).")
option(OAC_FIXED_POINT ${OAC_FIXED_POINT_HELP_STR} OFF)
add_feature_info(OAC_FIXED_POINT OAC_FIXED_POINT ${OAC_FIXED_POINT_HELP_STR})

set(OAC_ENABLE_FLOAT_API_HELP_STR "compile with the floating point API (for machines with float library).")
option(OAC_ENABLE_FLOAT_API ${OAC_ENABLE_FLOAT_API_HELP_STR} ON)
add_feature_info(OAC_ENABLE_FLOAT_API OAC_ENABLE_FLOAT_API ${OAC_ENABLE_FLOAT_API_HELP_STR})

set(OAC_FLOAT_APPROX_HELP_STR "enable floating point approximations (Ensure your platform supports IEEE 754 before enabling).")
option(OAC_FLOAT_APPROX ${OAC_FLOAT_APPROX_HELP_STR} OFF)
add_feature_info(OAC_FLOAT_APPROX OAC_FLOAT_APPROX ${OAC_FLOAT_APPROX_HELP_STR})

set(OAC_ASSERTIONS_HELP_STR "additional software error checking.")
option(OAC_ASSERTIONS ${OAC_ASSERTIONS_HELP_STR} OFF)
add_feature_info(OAC_ASSERTIONS OAC_ASSERTIONS ${OAC_ASSERTIONS_HELP_STR})

set(OAC_HARDENING_HELP_STR "run-time checks that are cheap and safe for use in production.")
option(OAC_HARDENING ${OAC_HARDENING_HELP_STR} ON)
add_feature_info(OAC_HARDENING OAC_HARDENING ${OAC_HARDENING_HELP_STR})

set(OAC_FUZZING_HELP_STR "causes the encoder to make random decisions (do not use in production).")
option(OAC_FUZZING ${OAC_FUZZING_HELP_STR} OFF)
add_feature_info(OAC_FUZZING OAC_FUZZING ${OAC_FUZZING_HELP_STR})

set(OAC_CHECK_ASM_HELP_STR "enable bit-exactness checks between optimized and c implementations.")
option(OAC_CHECK_ASM ${OAC_CHECK_ASM_HELP_STR} OFF)
add_feature_info(OAC_CHECK_ASM OAC_CHECK_ASM ${OAC_CHECK_ASM_HELP_STR})

set(OAC_DNN_FLOAT_DEBUG_HELP_STR "Run DNN computations as float for debugging purposes.")
option(OAC_DNN_FLOAT_DEBUG ${OAC_DNN_FLOAT_DEBUG_HELP_STR} OFF)
add_feature_info(OAC_DNN_FLOAT_DEBUG OAC_DNN_FLOAT_DEBUG ${OAC_DNN_FLOAT_DEBUG_HELP_STR})

set(OAC_INSTALL_PKG_CONFIG_MODULE_HELP_STR "install pkg-config module.")
option(OAC_INSTALL_PKG_CONFIG_MODULE ${OAC_INSTALL_PKG_CONFIG_MODULE_HELP_STR} ON)
add_feature_info(OAC_INSTALL_PKG_CONFIG_MODULE OAC_INSTALL_PKG_CONFIG_MODULE ${OAC_INSTALL_PKG_CONFIG_MODULE_HELP_STR})

set(OAC_INSTALL_CMAKE_CONFIG_MODULE_HELP_STR "install CMake package config module.")
option(OAC_INSTALL_CMAKE_CONFIG_MODULE ${OAC_INSTALL_CMAKE_CONFIG_MODULE_HELP_STR} ON)
add_feature_info(OAC_INSTALL_CMAKE_CONFIG_MODULE OAC_INSTALL_CMAKE_CONFIG_MODULE ${OAC_INSTALL_CMAKE_CONFIG_MODULE_HELP_STR})

set(OAC_DRED_HELP_STR "enable DRED.")
option(OAC_DRED ${OAC_DRED_HELP_STR} OFF)
add_feature_info(OAC_DRED OAC_DRED ${OAC_DRED_HELP_STR})

set(OAC_OSCE_HELP_STR "enable OSCE.")
option(OAC_OSCE ${OAC_OSCE_HELP_STR} OFF)
add_feature_info(OAC_OSCE OAC_OSCE ${OAC_OSCE_HELP_STR})

if(APPLE)
  set(OAC_BUILD_FRAMEWORK_HELP_STR "build Framework bundle for Apple systems.")
  option(OAC_BUILD_FRAMEWORK ${OAC_BUILD_FRAMEWORK_HELP_STR} OFF)
  add_feature_info(OAC_BUILD_FRAMEWORK OAC_BUILD_FRAMEWORK ${OAC_BUILD_FRAMEWORK_HELP_STR})
endif()

if(MSVC)
  set(OAC_STATIC_RUNTIME_HELP_STR "build with static runtime library.")
  option(OAC_STATIC_RUNTIME ${OAC_STATIC_RUNTIME_HELP_STR} OFF)
  add_feature_info(OAC_STATIC_RUNTIME OAC_STATIC_RUNTIME ${OAC_STATIC_RUNTIME_HELP_STR})
endif()

set(OAC_FIXED_POINT_DEBUG_HELP_STR "debug fixed-point implementation.")
cmake_dependent_option(OAC_FIXED_POINT_DEBUG
                      ${OAC_FIXED_POINT_DEBUG_HELP_STR}
                      ON
                      "OAC_FIXED_POINT; OAC_FIXED_POINT_DEBUG"
                      OFF)
add_feature_info(OAC_FIXED_POINT_DEBUG OAC_FIXED_POINT_DEBUG ${OAC_FIXED_POINT_DEBUG_HELP_STR})

set(OAC_VAR_ARRAYS_HELP_STR "use variable length arrays for stack arrays.")
cmake_dependent_option(OAC_VAR_ARRAYS
                      ${OAC_VAR_ARRAYS_HELP_STR}
                      ON
                      "VLA_SUPPORTED; NOT OAC_USE_ALLOCA; NOT OAC_NONTHREADSAFE_PSEUDOSTACK"
                      OFF)
add_feature_info(OAC_VAR_ARRAYS OAC_VAR_ARRAYS ${OAC_VAR_ARRAYS_HELP_STR})

set(OAC_USE_ALLOCA_HELP_STR "use alloca for stack arrays (on non-C99 compilers).")
cmake_dependent_option(OAC_USE_ALLOCA
                       ${OAC_USE_ALLOCA_HELP_STR}
                       ON
                       "USE_ALLOCA_SUPPORTED; NOT OAC_VAR_ARRAYS; NOT OAC_NONTHREADSAFE_PSEUDOSTACK"
                       OFF)
add_feature_info(OAC_USE_ALLOCA OAC_USE_ALLOCA ${OAC_USE_ALLOCA_HELP_STR})

set(OAC_NONTHREADSAFE_PSEUDOSTACK_HELP_STR "use a non threadsafe pseudostack when neither variable length arrays or alloca is supported.")
cmake_dependent_option(OAC_NONTHREADSAFE_PSEUDOSTACK
                       ${OAC_NONTHREADSAFE_PSEUDOSTACK_HELP_STR}
                       ON
                       "NOT OAC_VAR_ARRAYS; NOT OAC_USE_ALLOCA"
                       OFF)
add_feature_info(OAC_NONTHREADSAFE_PSEUDOSTACK OAC_NONTHREADSAFE_PSEUDOSTACK ${OAC_NONTHREADSAFE_PSEUDOSTACK_HELP_STR})

set(OAC_FAST_MATH_HELP_STR "enable fast math (unsupported and discouraged use, as code is not well tested with this build option).")
cmake_dependent_option(OAC_FAST_MATH
                       ${OAC_FAST_MATH_HELP_STR}
                       ON
                       "OAC_FLOAT_APPROX; OAC_FAST_MATH; FAST_MATH_SUPPORTED"
                       OFF)
add_feature_info(OAC_FAST_MATH OAC_FAST_MATH ${OAC_FAST_MATH_HELP_STR})

set(OAC_STACK_PROTECTOR_HELP_STR "use stack protection.")
cmake_dependent_option(OAC_STACK_PROTECTOR
                       ${OAC_STACK_PROTECTOR_HELP_STR}
                       ON
                       "STACK_PROTECTOR_SUPPORTED"
                       OFF)
add_feature_info(OAC_STACK_PROTECTOR OAC_STACK_PROTECTOR ${OAC_STACK_PROTECTOR_HELP_STR})

if(NOT MSVC)
  set(OAC_FORTIFY_SOURCE_HELP_STR "add protection against buffer overflows.")
  cmake_dependent_option(OAC_FORTIFY_SOURCE
                         ${OAC_FORTIFY_SOURCE_HELP_STR}
                         ON
                         "FORTIFY_SOURCE_SUPPORTED"
                         OFF)
  add_feature_info(OAC_FORTIFY_SOURCE OAC_FORTIFY_SOURCE ${OAC_FORTIFY_SOURCE_HELP_STR})
endif()

if(MINGW AND (OAC_FORTIFY_SOURCE OR OAC_STACK_PROTECTOR))
  # ssp lib is needed for security features for MINGW
  list(APPEND OAC_REQUIRED_LIBRARIES ssp)
endif()

if(OAC_CPU_X86 OR OAC_CPU_X64)
  set(OAC_X86_MAY_HAVE_SSE_HELP_STR "does runtime check for SSE1 support.")
  cmake_dependent_option(OAC_X86_MAY_HAVE_SSE
                         ${OAC_X86_MAY_HAVE_SSE_HELP_STR}
                         ON
                         "SSE1_SUPPORTED; NOT OAC_DISABLE_INTRINSICS"
                         OFF)
  add_feature_info(OAC_X86_MAY_HAVE_SSE OAC_X86_MAY_HAVE_SSE ${OAC_X86_MAY_HAVE_SSE_HELP_STR})

  set(OAC_X86_MAY_HAVE_SSE2_HELP_STR "does runtime check for SSE2 support.")
  cmake_dependent_option(OAC_X86_MAY_HAVE_SSE2
                         ${OAC_X86_MAY_HAVE_SSE2_HELP_STR}
                         ON
                         "SSE2_SUPPORTED; NOT OAC_DISABLE_INTRINSICS"
                         OFF)
  add_feature_info(OAC_X86_MAY_HAVE_SSE2 OAC_X86_MAY_HAVE_SSE2 ${OAC_X86_MAY_HAVE_SSE2_HELP_STR})

  set(OAC_X86_MAY_HAVE_SSE4_1_HELP_STR "does runtime check for SSE4.1 support.")
  cmake_dependent_option(OAC_X86_MAY_HAVE_SSE4_1
                         ${OAC_X86_MAY_HAVE_SSE4_1_HELP_STR}
                         ON
                         "SSE4_1_SUPPORTED; NOT OAC_DISABLE_INTRINSICS"
                         OFF)
  add_feature_info(OAC_X86_MAY_HAVE_SSE4_1 OAC_X86_MAY_HAVE_SSE4_1 ${OAC_X86_MAY_HAVE_SSE4_1_HELP_STR})

  set(OAC_X86_MAY_HAVE_AVX2_HELP_STR "does runtime check for AVX FMA AVX2 support.")
  cmake_dependent_option(OAC_X86_MAY_HAVE_AVX2
                         ${OAC_X86_MAY_HAVE_AVX2_HELP_STR}
                         ON
                         "AVX2_SUPPORTED; NOT OAC_DISABLE_INTRINSICS"
                         OFF)
  add_feature_info(OAC_X86_MAY_HAVE_AVX2 OAC_X86_MAY_HAVE_AVX2 ${OAC_X86_MAY_HAVE_AVX2_HELP_STR})

  # PRESUME depends on MAY HAVE, but PRESUME will override runtime detection
  set(OAC_X86_PRESUME_SSE_HELP_STR "assume target CPU has SSE1 support (override runtime check).")
  set(OAC_X86_PRESUME_SSE2_HELP_STR "assume target CPU has SSE2 support (override runtime check).")
  if(OAC_CPU_X64) # Assume x86_64 has up to SSE2 support
    cmake_dependent_option(OAC_X86_PRESUME_SSE
                           ${OAC_X86_PRESUME_SSE_HELP_STR}
                           ON
                           "OAC_X86_MAY_HAVE_SSE; NOT OAC_DISABLE_INTRINSICS"
                           OFF)

    cmake_dependent_option(OAC_X86_PRESUME_SSE2
                           ${OAC_X86_PRESUME_SSE2_HELP_STR}
                           ON
                           "OAC_X86_MAY_HAVE_SSE2; NOT OAC_DISABLE_INTRINSICS"
                           OFF)
  else()
    cmake_dependent_option(OAC_X86_PRESUME_SSE
                           ${OAC_X86_PRESUME_SSE_HELP_STR}
                           OFF
                           "OAC_X86_MAY_HAVE_SSE; NOT OAC_DISABLE_INTRINSICS"
                           OFF)

    cmake_dependent_option(OAC_X86_PRESUME_SSE2
                           ${OAC_X86_PRESUME_SSE2_HELP_STR}
                           OFF
                           "OAC_X86_MAY_HAVE_SSE2; NOT OAC_DISABLE_INTRINSICS"
                           OFF)
  endif()
  add_feature_info(OAC_X86_PRESUME_SSE OAC_X86_PRESUME_SSE ${OAC_X86_PRESUME_SSE_HELP_STR})
  add_feature_info(OAC_X86_PRESUME_SSE2 OAC_X86_PRESUME_SSE2 ${OAC_X86_PRESUME_SSE2_HELP_STR})

  set(OAC_X86_PRESUME_SSE4_1_HELP_STR "assume target CPU has SSE4.1 support (override runtime check).")
  cmake_dependent_option(OAC_X86_PRESUME_SSE4_1
                         ${OAC_X86_PRESUME_SSE4_1_HELP_STR}
                         OFF
                         "OAC_X86_MAY_HAVE_SSE4_1; NOT OAC_DISABLE_INTRINSICS"
                         OFF)
  add_feature_info(OAC_X86_PRESUME_SSE4_1 OAC_X86_PRESUME_SSE4_1 ${OAC_X86_PRESUME_SSE4_1_HELP_STR})

  set(OAC_X86_PRESUME_AVX2_HELP_STR "assume target CPU has AVX FMA AVX2 support (override runtime check).")
  cmake_dependent_option(OAC_X86_PRESUME_AVX2
                         ${OAC_X86_PRESUME_AVX2_HELP_STR}
                         OFF
                         "OAC_X86_MAY_HAVE_AVX2; NOT OAC_DISABLE_INTRINSICS"
                         OFF)
  add_feature_info(OAC_X86_PRESUME_AVX2 OAC_X86_PRESUME_AVX2 ${OAC_X86_PRESUME_AVX2_HELP_STR})
endif()

feature_summary(WHAT ALL)

set_package_properties(Git
                       PROPERTIES
                       TYPE
                       REQUIRED
                       DESCRIPTION
                       "fast, scalable, distributed revision control system"
                       URL
                       "https://git-scm.com/"
                       PURPOSE
                       "required to set up package version")

set(Oac_PUBLIC_HEADER
    ${CMAKE_CURRENT_SOURCE_DIR}/include/oac.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/oac_defines.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/oac_multistream.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/oac_projection.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/oac_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/opus_types.h)

if(OAC_CUSTOM_MODES)
  list(APPEND Oac_PUBLIC_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/include/oac_custom.h)
endif()

if(MSVC)
  if(OAC_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  endif()
endif()

add_library(oac ${oac_headers} ${oac_sources} ${oac_sources_float} ${Oac_PUBLIC_HEADER})
add_library(Oac::oac ALIAS oac)

get_library_version(OAC_LIBRARY_VERSION OAC_LIBRARY_VERSION_MAJOR)
message(DEBUG "Oac library version: ${OAC_LIBRARY_VERSION}")

set_target_properties(oac
                      PROPERTIES SOVERSION
                                 ${OAC_LIBRARY_VERSION_MAJOR}
                                 VERSION
                                 ${OAC_LIBRARY_VERSION}
                                 PUBLIC_HEADER
                                 "${Oac_PUBLIC_HEADER}")

target_include_directories(
  oac
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/oac>
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
          ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_SOURCE_DIR}/dnn
          celt
          silk)

target_link_libraries(oac PRIVATE ${OAC_REQUIRED_LIBRARIES})
target_compile_definitions(oac PRIVATE OAC_BUILD)

if(OAC_FIXED_POINT_DEBUG)
  target_compile_definitions(oac PRIVATE FIXED_DEBUG)
endif()

if(OAC_FORTIFY_SOURCE AND NOT MSVC)
  target_compile_definitions(oac PRIVATE
                             $<$<NOT:$<CONFIG:debug>>:_FORTIFY_SOURCE=2>)
endif()

if(OAC_FLOAT_APPROX)
  target_compile_definitions(oac PRIVATE FLOAT_APPROX)
endif()

if(OAC_ASSERTIONS)
  target_compile_definitions(oac PRIVATE ENABLE_ASSERTIONS)
endif()

if(OAC_HARDENING)
  target_compile_definitions(oac PRIVATE ENABLE_HARDENING)
endif()

if(OAC_FUZZING)
  target_compile_definitions(oac PRIVATE FUZZING)
endif()

if(OAC_CHECK_ASM)
  target_compile_definitions(oac PRIVATE OAC_CHECK_ASM)
endif()

if(NOT OAC_DNN_FLOAT_DEBUG)
  target_compile_definitions(oac PRIVATE DISABLE_DEBUG_FLOAT)
endif()

if(OAC_VAR_ARRAYS)
  target_compile_definitions(oac PRIVATE VAR_ARRAYS)
elseif(OAC_USE_ALLOCA)
  target_compile_definitions(oac PRIVATE USE_ALLOCA)
elseif(OAC_NONTHREADSAFE_PSEUDOSTACK)
  target_compile_definitions(oac PRIVATE NONTHREADSAFE_PSEUDOSTACK)
else()
  message(ERROR "Need to set a define for stack allocation")
endif()

if(OAC_CUSTOM_MODES)
  target_compile_definitions(oac PRIVATE CUSTOM_MODES)
endif()

if(OAC_FAST_MATH)
  if(MSVC)
    target_compile_options(oac PRIVATE /fp:fast)
  else()
    target_compile_options(oac PRIVATE -ffast-math)
  endif()
endif()

if(OAC_STACK_PROTECTOR)
  if(MSVC)
    target_compile_options(oac PRIVATE /GS)
  else()
    target_compile_options(oac PRIVATE -fstack-protector-strong)
  endif()
elseif(STACK_PROTECTOR_DISABLED_SUPPORTED)
  target_compile_options(oac PRIVATE /GS-)
endif()

if(BUILD_SHARED_LIBS)
  if(WIN32)
    target_compile_definitions(oac PRIVATE DLL_EXPORT)
  elseif(HIDDEN_VISIBILITY_SUPPORTED)
    set_target_properties(oac PROPERTIES C_VISIBILITY_PRESET hidden)
  endif()
endif()

add_sources_group(oac silk ${silk_headers} ${silk_sources})
add_sources_group(oac celt ${celt_headers} ${celt_sources})

if(OAC_FIXED_POINT)
  add_sources_group(oac silk ${silk_sources_fixed})
  target_include_directories(oac PRIVATE silk/fixed)
  target_compile_definitions(oac PRIVATE FIXED_POINT=1)
else()
  add_sources_group(oac silk ${silk_sources_float})
  target_include_directories(oac PRIVATE silk/float)
endif()

if(NOT OAC_ENABLE_FLOAT_API)
  target_compile_definitions(oac PRIVATE DISABLE_FLOAT_API)
endif()

if (OAC_DEEP_PLC OR OAC_DRED OR OAC_OSCE)
  add_sources_group(oac lpcnet ${deep_plc_headers} ${deep_plc_sources})
  set(OAC_DNN TRUE)
else()
  set(OAC_DNN FALSE)
endif()

if (OAC_DNN)
  add_sources_group(oac lpcnet ${deep_plc_headers} ${deep_plc_sources})
  target_compile_definitions(oac PRIVATE ENABLE_DEEP_PLC)
endif()

if (OAC_DRED)
  add_sources_group(oac lpcnet ${dred_headers} ${dred_sources})
  target_compile_definitions(oac PRIVATE ENABLE_DRED)
endif()

if (OAC_OSCE)
  add_sources_group(oac lpcnet ${osce_headers} ${osce_sources})
  target_compile_definitions(oac PRIVATE ENABLE_OSCE)
endif()

if(NOT OAC_DISABLE_INTRINSICS)
  if(((OAC_X86_MAY_HAVE_SSE AND NOT OAC_X86_PRESUME_SSE) OR
     (OAC_X86_MAY_HAVE_SSE2 AND NOT OAC_X86_PRESUME_SSE2) OR
     (OAC_X86_MAY_HAVE_SSE4_1 AND NOT OAC_X86_PRESUME_SSE4_1) OR
     (OAC_X86_MAY_HAVE_AVX2 AND NOT OAC_X86_PRESUME_AVX2)) AND
      RUNTIME_CPU_CAPABILITY_DETECTION)
    target_compile_definitions(oac PRIVATE OAC_HAVE_RTCD)
    if(NOT MSVC)
      if(CPU_INFO_BY_ASM_SUPPORTED)
        target_compile_definitions(oac PRIVATE CPU_INFO_BY_ASM)
      elseif(CPU_INFO_BY_C_SUPPORTED)
        target_compile_definitions(oac PRIVATE CPU_INFO_BY_C)
      else()
        message(ERROR "Runtime cpu capability detection is enabled while CPU_INFO is not supported")
      endif()
    endif()
    add_sources_group(oac celt ${celt_sources_x86_rtcd})
    add_sources_group(oac silk ${silk_sources_x86_rtcd})
    if (OAC_DNN)
      add_sources_group(oac lpcnet ${dnn_sources_x86_rtcd})
    endif()
  endif()

  if(SSE1_SUPPORTED)
    if(OAC_X86_MAY_HAVE_SSE)
      add_sources_group(oac celt ${celt_sources_sse})
      target_compile_definitions(oac PRIVATE OAC_X86_MAY_HAVE_SSE)
      if(NOT MSVC)
        set_source_files_properties(${celt_sources_sse} PROPERTIES COMPILE_FLAGS -msse)
      endif()
    endif()
    if(OAC_X86_PRESUME_SSE)
      target_compile_definitions(oac PRIVATE OAC_X86_PRESUME_SSE)
      if(NOT MSVC)
        target_compile_options(oac PRIVATE -msse)
      endif()
    endif()
  endif()

  if(SSE2_SUPPORTED)
    if(OAC_X86_MAY_HAVE_SSE2)
      add_sources_group(oac celt ${celt_sources_sse2})
      if (OAC_DNN)
        add_sources_group(oac lpcnet ${dnn_sources_sse2})
      endif()
      target_compile_definitions(oac PRIVATE OAC_X86_MAY_HAVE_SSE2)
      if(NOT MSVC)
        set_source_files_properties(${celt_sources_sse2} ${dnn_sources_sse2} PROPERTIES COMPILE_FLAGS -msse2)
      endif()
    endif()
    if(OAC_X86_PRESUME_SSE2)
      target_compile_definitions(oac PRIVATE OAC_X86_PRESUME_SSE2)
      if(NOT MSVC)
        target_compile_options(oac PRIVATE -msse2)
      endif()
    endif()
  endif()

  if(SSE4_1_SUPPORTED)
    if(OAC_X86_MAY_HAVE_SSE4_1)
      add_sources_group(oac celt ${celt_sources_sse4_1})
      add_sources_group(oac silk ${silk_sources_sse4_1})
      if (OAC_DNN)
        add_sources_group(oac lpcnet ${dnn_sources_sse4_1})
      endif()
      target_compile_definitions(oac PRIVATE OAC_X86_MAY_HAVE_SSE4_1)
      if(NOT MSVC OR MSVC_CLANG_CL)
        set_source_files_properties(${celt_sources_sse4_1} ${silk_sources_sse4_1} ${dnn_sources_sse4_1} PROPERTIES COMPILE_FLAGS -msse4.1)
      endif()

      if(OAC_FIXED_POINT)
        add_sources_group(oac silk ${silk_sources_fixed_sse4_1})
        if(NOT MSVC OR MSVC_CLANG_CL)
          set_source_files_properties(${silk_sources_fixed_sse4_1} PROPERTIES COMPILE_FLAGS -msse4.1)
        endif()
      endif()
    endif()
    if(OAC_X86_PRESUME_SSE4_1)
      target_compile_definitions(oac PRIVATE OAC_X86_PRESUME_SSE4_1)
      if(NOT MSVC OR MSVC_CLANG_CL)
        target_compile_options(oac PRIVATE -msse4.1)
      endif()
    endif()
  endif()

  if(AVX2_SUPPORTED)
    if(OAC_X86_MAY_HAVE_AVX2)
      add_sources_group(oac celt ${celt_sources_avx2})
      add_sources_group(oac silk ${silk_sources_avx2})
      if (NOT OAC_FIXED_POINT)
        add_sources_group(oac silk ${silk_sources_float_avx2})
      endif()
      if (OAC_DNN)
        add_sources_group(oac lpcnet ${dnn_sources_avx2})
      endif()
      target_compile_definitions(oac PRIVATE OAC_X86_MAY_HAVE_AVX2)
      if(MSVC)
        set(AVX2_FLAGS "${AVX2_FLAGS} /arch:AVX2")
      else()
        set(AVX2_FLAGS "${AVX2_FLAGS} -mavx2 -mfma -mavx")
      endif()
      set_source_files_properties(${celt_sources_avx2} PROPERTIES COMPILE_FLAGS ${AVX2_FLAGS})
      set_source_files_properties(${silk_sources_avx2} PROPERTIES COMPILE_FLAGS ${AVX2_FLAGS})
      if (NOT OAC_FIXED_POINT)
        set_source_files_properties(${silk_sources_float_avx2} PROPERTIES COMPILE_FLAGS ${AVX2_FLAGS})
      endif()
      set_source_files_properties(${dnn_sources_avx2} PROPERTIES COMPILE_FLAGS ${AVX2_FLAGS})
    endif()
    if(OAC_X86_PRESUME_AVX2)
      target_compile_definitions(oac PRIVATE OAC_X86_PRESUME_AVX2)
      target_compile_definitions(oac PRIVATE OAC_X86_PRESUME_SSE4_1)
      if(NOT MSVC)
        target_compile_options(oac PRIVATE -mavx2 -mfma -mavx)
      endif()
    endif()
  endif()

  if(MSVC)
    if(AVX2_SUPPORTED AND OAC_X86_PRESUME_AVX2) # on 64 bit and 32 bits
      add_definitions(/arch:AVX2)
    elseif(OAC_CPU_X86) # if AVX not supported then set SSE flag
      if((SSE4_1_SUPPORTED AND OAC_X86_PRESUME_SSE4_1)
         OR (SSE2_SUPPORTED AND OAC_X86_PRESUME_SSE2))
        target_compile_definitions(oac PRIVATE /arch:SSE2)
      elseif(SSE1_SUPPORTED AND OAC_X86_PRESUME_SSE)
        target_compile_definitions(oac PRIVATE /arch:SSE)
      endif()
    endif()
  endif()

  if(COMPILER_SUPPORT_NEON)
    if(OAC_MAY_HAVE_NEON)
      if(RUNTIME_CPU_CAPABILITY_DETECTION)
        message(STATUS "OAC_MAY_HAVE_NEON enabling runtime detection")
        target_compile_definitions(oac PRIVATE OAC_HAVE_RTCD)
        add_sources_group(oac celt ${celt_sources_arm_rtcd})
        add_sources_group(oac silk ${silk_sources_arm_rtcd})
        add_sources_group(oac lpcnet ${dnn_sources_arm_rtcd})
      else()
        message(ERROR "Runtime cpu capability detection needed for MAY_HAVE_NEON")
      endif()
      # Do runtime check for NEON
      target_compile_definitions(oac
                                 PRIVATE
                                 OAC_ARM_MAY_HAVE_NEON
                                 OAC_ARM_MAY_HAVE_NEON_INTR)
    endif()

    add_sources_group(oac celt ${celt_sources_arm_neon_intr})
    add_sources_group(oac silk ${silk_sources_arm_neon_intr})
    if (OAC_DNN)
      add_sources_group(oac lpcnet ${dnn_sources_arm_neon})
    endif()

    # silk arm neon depends on main_Fix.h
    target_include_directories(oac PRIVATE silk/fixed)

    if(OAC_FIXED_POINT)
      add_sources_group(oac silk ${silk_sources_fixed_arm_neon_intr})
    endif()

    if(OAC_PRESUME_NEON)
      target_compile_definitions(oac
                                 PRIVATE
                                 OAC_ARM_PRESUME_NEON
                                 OAC_ARM_PRESUME_NEON_INTR)
    endif()
  endif()
endif()

target_compile_definitions(oac
                           PRIVATE
                           $<$<BOOL:${HAVE_LRINT}>:HAVE_LRINT>
                           $<$<BOOL:${HAVE_LRINTF}>:HAVE_LRINTF>
                           $<$<BOOL:${HAVE_ELF_AUX_INFO}>:HAVE_ELF_AUX_INFO>)

if(OAC_BUILD_FRAMEWORK)
  set_target_properties(oac PROPERTIES
                        FRAMEWORK TRUE
                        FRAMEWORK_VERSION ${PROJECT_VERSION}
                        MACOSX_FRAMEWORK_IDENTIFIER org.xiph.oac
                        MACOSX_FRAMEWORK_SHORT_VERSION_STRING ${PROJECT_VERSION}
                        MACOSX_FRAMEWORK_BUNDLE_VERSION ${PROJECT_VERSION}
                        XCODE_ATTRIBUTE_INSTALL_PATH "@rpath"
                        OUTPUT_NAME Oac)
endif()

install(TARGETS oac
        EXPORT OacTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        FRAMEWORK DESTINATION ${CMAKE_INSTALL_PREFIX}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/oac)

if(OAC_INSTALL_PKG_CONFIG_MODULE)
  set(prefix ${CMAKE_INSTALL_PREFIX})
  set(exec_prefix ${CMAKE_INSTALL_PREFIX})
  set(libdir ${CMAKE_INSTALL_FULL_LIBDIR})
  set(includedir ${CMAKE_INSTALL_FULL_INCLUDEDIR})
  set(VERSION ${PACKAGE_VERSION})
  if(HAVE_LIBM)
    set(LIBM "-lm")
  endif()
  configure_file(oac.pc.in oac.pc)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/oac.pc
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

if(OAC_INSTALL_CMAKE_CONFIG_MODULE)
  set(CPACK_GENERATOR TGZ)
  include(CPack)
  set(CMAKE_INSTALL_PACKAGEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
  install(EXPORT OacTargets
          NAMESPACE Oac::
          DESTINATION ${CMAKE_INSTALL_PACKAGEDIR})

  include(CMakePackageConfigHelpers)

  set(INCLUDE_INSTALL_DIR ${CMAKE_INSTALL_INCLUDEDIR})
  configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/OacConfig.cmake.in
                                OacConfig.cmake
                                INSTALL_DESTINATION
                                ${CMAKE_INSTALL_PACKAGEDIR}
                                PATH_VARS
                                INCLUDE_INSTALL_DIR
                                INSTALL_PREFIX
                                ${CMAKE_INSTALL_PREFIX})
  write_basic_package_version_file(OacConfigVersion.cmake
                                   VERSION ${PROJECT_VERSION}
                                   COMPATIBILITY SameMajorVersion)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/OacConfig.cmake
                ${CMAKE_CURRENT_BINARY_DIR}/OacConfigVersion.cmake
          DESTINATION ${CMAKE_INSTALL_PACKAGEDIR})
endif()

if(OAC_BUILD_PROGRAMS)
  # demo
  if(OAC_CUSTOM_MODES)
    add_executable(oac_custom_demo ${oac_custom_demo_sources})
    target_include_directories(oac_custom_demo
                               PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(oac_custom_demo PRIVATE oac)
    target_compile_definitions(oac_custom_demo PRIVATE OAC_BUILD)
  endif()

  add_executable(oac_demo ${oac_demo_sources})
  target_include_directories(oac_demo PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_include_directories(oac_demo PRIVATE silk) # debug.h
  target_include_directories(oac_demo PRIVATE celt) # arch.h
  target_include_directories(oac_demo PRIVATE dnn)
  target_link_libraries(oac_demo PRIVATE oac ${OAC_REQUIRED_LIBRARIES})
  target_compile_definitions(oac_demo PRIVATE OAC_BUILD)

  # compare
  add_executable(oac_compare ${oac_compare_sources})
  target_include_directories(oac_compare PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(oac_compare PRIVATE oac ${OAC_REQUIRED_LIBRARIES})
endif()

if(BUILD_TESTING AND NOT BUILD_SHARED_LIBS)
  enable_testing()

  # tests
  add_executable(test_oac_decode ${test_oac_decode_sources})
  target_include_directories(test_oac_decode
                             PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(test_oac_decode PRIVATE oac)
  target_compile_definitions(test_oac_decode PRIVATE OAC_BUILD)
  if(OAC_FIXED_POINT)
    target_compile_definitions(test_oac_decode PRIVATE DISABLE_FLOAT_API)
  endif()
  add_test(NAME test_oac_decode COMMAND ${CMAKE_COMMAND}
           -DTEST_EXECUTABLE=$<TARGET_FILE:test_oac_decode>
           -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
           -P "${PROJECT_SOURCE_DIR}/cmake/RunTest.cmake")

  add_executable(test_oac_padding ${test_oac_padding_sources})
  target_include_directories(test_oac_padding
                             PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(test_oac_padding PRIVATE oac)
  add_test(NAME test_oac_padding COMMAND ${CMAKE_COMMAND}
           -DTEST_EXECUTABLE=$<TARGET_FILE:test_oac_padding>
           -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
           -P "${PROJECT_SOURCE_DIR}/cmake/RunTest.cmake")

  add_executable(test_oac_api ${test_oac_api_sources})
  target_include_directories(test_oac_api
                            PRIVATE ${CMAKE_CURRENT_BINARY_DIR} celt)
  target_link_libraries(test_oac_api PRIVATE oac)
  target_compile_definitions(test_oac_api PRIVATE OAC_BUILD)
  if(OAC_FIXED_POINT)
    target_compile_definitions(test_oac_api PRIVATE DISABLE_FLOAT_API)
  endif()
  add_test(NAME test_oac_api COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:test_oac_api>
        -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -P "${PROJECT_SOURCE_DIR}/cmake/RunTest.cmake")

  add_executable(test_oac_encode ${test_oac_encode_sources})
  target_include_directories(test_oac_encode
                            PRIVATE ${CMAKE_CURRENT_BINARY_DIR} celt dnn)
  target_link_libraries(test_oac_encode PRIVATE oac)
  target_compile_definitions(test_oac_encode PRIVATE OAC_BUILD)
  add_test(NAME test_oac_encode COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:test_oac_encode>
        -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -P "${PROJECT_SOURCE_DIR}/cmake/RunTest.cmake")

  add_executable(test_oac_extensions ${test_oac_extensions_sources})
  target_include_directories(test_oac_extensions
                            PRIVATE ${CMAKE_CURRENT_BINARY_DIR} celt dnn)
  target_link_libraries(test_oac_extensions PRIVATE oac)
  target_compile_definitions(test_oac_extensions PRIVATE OAC_BUILD)
  add_test(NAME test_oac_extensions COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:test_oac_extensions>
        -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -P "${PROJECT_SOURCE_DIR}/cmake/RunTest.cmake")
  if(OAC_DRED)
    add_executable(test_oac_dred ${test_oac_dred_sources})
    target_include_directories(test_oac_dred
                              PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(test_oac_dred PRIVATE oac)
    target_compile_definitions(test_oac_dred PRIVATE OAC_BUILD)
    add_test(NAME test_oac_dred COMMAND ${CMAKE_COMMAND}
          -DTEST_EXECUTABLE=$<TARGET_FILE:test_oac_dred>
          -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
          -P "${PROJECT_SOURCE_DIR}/cmake/RunTest.cmake")
  endif()
  if(OAC_CUSTOM_MODES)
    add_executable(test_oac_custom ${test_oac_custom_sources})
    target_include_directories(test_oac_custom
                              PRIVATE ${CMAKE_CURRENT_BINARY_DIR} celt dnn)
    target_link_libraries(test_oac_custom PRIVATE oac)
    target_compile_definitions(test_oac_custom PRIVATE OAC_BUILD)
  endif()
endif()
