# ---------------------------------------------------------------------------
# lzma_sdk.cmake
#
# 7-Zip's LZMA codec, from Igor Pavlov's public domain LZMA SDK, as two static
# libraries:
#
#   lzma_dec   LzmaDec.c alone - what the component carries to unpack the
#              embedded lyrics. A few KB.
#   lzma_enc   the encoder, single-threaded (Z7_ST), for tools/pack_lyrics,
#              which runs at build time and is never shipped.
#
# scripts/get_sdk.ps1 (or get_sdk.sh) fetches the SDK into external/lzma_sdk.
# ---------------------------------------------------------------------------

set(_lzma_c "${LZMA_SDK_DIR}/C")
if(NOT EXISTS "${_lzma_c}/LzmaDec.c")
    message(FATAL_ERROR "LZMA_SDK_DIR does not look like an LZMA SDK: ${LZMA_SDK_DIR}")
endif()

add_library(lzma_dec STATIC "${_lzma_c}/LzmaDec.c")
target_include_directories(lzma_dec SYSTEM PUBLIC "${_lzma_c}")

add_library(lzma_enc STATIC
    "${_lzma_c}/LzmaEnc.c"
    "${_lzma_c}/LzFind.c"
    "${_lzma_c}/LzFindOpt.c"
    "${_lzma_c}/CpuArch.c")
target_include_directories(lzma_enc SYSTEM PUBLIC "${_lzma_c}")
# No threads: the multi-threaded match finder would pull in LzFindMt.c and
# Threads.c for a payload that compresses in a fraction of a second anyway.
target_compile_definitions(lzma_enc PUBLIC Z7_ST)

set_target_properties(lzma_dec lzma_enc PROPERTIES FOLDER "LZMA SDK")
foreach(_t lzma_dec lzma_enc)
    if(MSVC)
        target_compile_options(${_t} PRIVATE /W3)
    else()
        target_compile_options(${_t} PRIVATE -w)
    endif()
endforeach()
