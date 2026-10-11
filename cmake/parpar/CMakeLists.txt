# Build ParPar's GF16 and hashing engines from the fetched ParPar tree as one static library.
#
# ParPar ships no CMake of its own, so this is ours. It runs as a nested project
# through add_vendored_project(), which builds one tree per architecture for a
# macOS universal2 build: the flag probes below have to see a single target.
#
# Each group of kernels is compiled with the flags for the instruction set it
# targets when the compiler accepts them. A group whose flags are refused is
# compiled without them, and the guards in its sources leave it empty; ParPar
# picks what to use at run time from what the CPU supports.

cmake_minimum_required(VERSION 3.16)
project(parpar C CXX)

include(CheckCCompilerFlag)

if(NOT PARPAR_DIR)
    message(FATAL_ERROR "Set PARPAR_DIR to the fetched ParPar tree")
endif()

set(PARPAR_SOURCES
    gf16/controller.cpp
    gf16/controller_cpu.cpp
    gf16/gfmat_coeff.c
    gf16/gfmat_inv.cpp
    gf16/gf16pmul.cpp
    gf16/gf16mul.cpp
    gf16/gf16_lookup.c
    gf16/gf_add_generic.c
    gf16/gf16_cksum_generic.c
    hasher/crc_zeropad.c
    hasher/hasher.cpp
    hasher/hasher_scalar.cpp
    hasher/hasher_input.cpp
    hasher/hasher_md5crc.cpp
    hasher/tables.cpp
    hasher/md5-final.c
)

# Compile a group of kernels with the flags for its instruction set, GCC and
# Clang taking the first set and MSVC the second
set(PARPAR_GROUPED)
function(parpar_group gnu msvc)
    set(sources ${ARGN})
    list(TRANSFORM sources PREPEND "${PARPAR_DIR}/")

    if(MSVC)
        set(flags "${msvc}")
    else()
        set(flags "${gnu}")
    endif()

    if(flags)
        string(MAKE_C_IDENTIFIER "PARPAR_FLAGS_${flags}" check)
        check_c_compiler_flag("${flags}" ${check})
        if(${check})
            separate_arguments(options NATIVE_COMMAND "${flags}")
            set_source_files_properties(${sources} PROPERTIES COMPILE_OPTIONS "${options}")
        endif()
    endif()

    set(PARPAR_GROUPED ${PARPAR_GROUPED} ${sources} PARENT_SCOPE)
endfunction()

# x86. MSVC needs /arch only from AVX up: SSE2 is its x64 baseline, and it compiles
# the SSSE3, CLMUL and GFNI intrinsics without one.
parpar_group("-msse2" "" gf16/gf16_xor_sse2.c gf16/gf16_lookup_sse2.c gf16/gf_add_sse2.c gf16/gf16_cksum_sse2.c hasher/hasher_sse.cpp)
parpar_group("-mssse3" "" gf16/gf16_shuffle_ssse3.c)
parpar_group("-mavx" "/arch:AVX" gf16/gf16_shuffle_avx.c)
parpar_group("-mavx2" "/arch:AVX2" gf16/gf16_xor_avx2.c gf16/gf16_shuffle_avx2.c gf16/gf_add_avx2.c gf16/gf16_cksum_avx2.c hasher/hasher_avx2.cpp)
parpar_group("-mavx512vl -mavx512bw" "/arch:AVX512" gf16/gf16_xor_avx512.c gf16/gf16_shuffle_avx512.c gf16/gf_add_avx512.c gf16/gf16_cksum_avx512.c)
parpar_group("-mavx512vl -mavx512vbmi" "/arch:AVX512" gf16/gf16_shuffle_vbmi.c)
parpar_group("-mgfni -mssse3" "" gf16/gf16_affine_gfni.c)
parpar_group("-mgfni -mssse3 -mavx2" "/arch:AVX2" gf16/gf16_affine_avx2.c src/platform_warnings.c)
parpar_group("-mgfni -mssse3 -mavx512vl -mavx512bw" "/arch:AVX512" gf16/gf16_affine_avx512.c)
parpar_group("-mavx512vl -mavx512bmm" "/arch:AVX512" gf16/gf16_affine_bmm.c)
parpar_group("-mpclmul -msse4.1" "" gf16/gf16pmul_sse.c hasher/hasher_clmul.cpp)
parpar_group("-mpclmul -msse4.1 -mavx2" "/arch:AVX2" gf16/gf16pmul_avx2.c)
parpar_group("-mvpclmulqdq -mavx2" "/arch:AVX2" gf16/gf16pmul_vpclmul.c)
parpar_group("-mvpclmulqdq -mavx2 -mgfni -mssse3" "/arch:AVX2" gf16/gf16pmul_vpclgfni.c)
parpar_group("-mavx -mxop" "/arch:AVX" hasher/hasher_xop.cpp)
parpar_group("-mavx -mbmi -mpclmul" "/arch:AVX" hasher/hasher_bmi1.cpp)
parpar_group("-mavx512f" "/arch:AVX512" hasher/hasher_avx512.cpp)
parpar_group("-mavx512vl -mavx512bw -mbmi2 -mpclmul" "/arch:AVX512" hasher/hasher_avx512vl.cpp)

# ARM. NEON needs no flags on AArch64, where the ARMv7 ones are refused.
parpar_group("-mfpu=neon -march=armv7-a" "" gf16/gf16_shuffle_neon.c gf16/gf16_clmul_neon.c gf16/gf_add_neon.c gf16/gf16_cksum_neon.c gf16/gf16pmul_neon.c hasher/hasher_neon.cpp)
parpar_group("-march=armv8.2-a+sha3" "" gf16/gf16_clmul_sha3.c)
parpar_group("-march=armv8-a+sve" "" gf16/gf16_shuffle128_sve.c gf16/gf_add_sve.c gf16/gf16_cksum_sve.c)
parpar_group("-march=armv8-a+sve2" "" gf16/gf16_shuffle128_sve2.c gf16/gf16_shuffle2x128_sve2.c gf16/gf16_shuffle512_sve2.c gf16/gf16_clmul_sve2.c gf16/gf_add_sve2.c gf16/gf16pmul_sve2.c hasher/hasher_sve2.cpp)
parpar_group("-march=armv8-a+crc" "" hasher/hasher_armcrc.cpp hasher/hasher_neoncrc.cpp)

# RISC-V
parpar_group("-march=rv64gcv" "" gf16/gf16_cksum_rvv.c gf16/gf16_shuffle128_rvv.c gf16/gf_add_rvv.c)
parpar_group("-march=rv64gcv_zvbc" "" gf16/gf16_clmul_rvv.c gf16/gf16pmul_rvv.c)
parpar_group("-march=rv64gc_zbkc" "" hasher/hasher_rvzbc.cpp)

# MSVC cannot inline the XOR kernels' JIT stubs, so it takes them from assembly
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64)$")
    enable_language(ASM_MASM)
    list(APPEND PARPAR_SOURCES gf16/xor_jit_stub_masm64.asm)
endif()

list(TRANSFORM PARPAR_SOURCES PREPEND "${PARPAR_DIR}/")

add_library(parpar STATIC ${PARPAR_SOURCES} ${PARPAR_GROUPED})
target_include_directories(parpar PRIVATE "${PARPAR_DIR}/gf16" "${PARPAR_DIR}/gf16/opencl-include")
target_compile_definitions(parpar PRIVATE
    NDEBUG PARPAR_ENABLE_HASHER_MD5CRC PARPAR_INVERT_SUPPORT PARPAR_SLIM_GF16
    _DARWIN_C_SOURCE _GNU_SOURCE _DEFAULT_SOURCE)
set_target_properties(parpar PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON C_STANDARD 11)

if(MSVC)
    target_compile_options(parpar PRIVATE /W0)
else()
    target_compile_options(parpar PRIVATE -w)
endif()
