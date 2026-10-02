# Fingerprint experiment: the feature extractor.
#
# Builds against bpmcore from the foo_bpm checkout, so the features are the
# ones a foobar2000 component would compute:
#
#   cmake -S fingerprint_lab -B fingerprint_lab/build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build fingerprint_lab/build
#
# (from a Visual Studio developer prompt, or with -A x64 and no -G for the
# Visual Studio generator).

cmake_minimum_required(VERSION 3.21)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
project(fingerprint_lab LANGUAGES C CXX)

set(FOO_BPM_DIR "C:/Dev/foo_bpm" CACHE PATH "Checkout of foo_bpm, for bpmcore")

include("${FOO_BPM_DIR}/cmake/fft_backend.cmake")
if(BPMCORE_FFT_BACKEND STREQUAL "kiss")
    add_subdirectory("${FOO_BPM_DIR}/kiss_fft" kiss_fft)
else()
    add_subdirectory("${FOO_BPM_DIR}/pffft" pffft)
endif()
add_subdirectory("${FOO_BPM_DIR}/bpmcore" bpmcore)

add_executable(fp_extract fp_extract.cpp)
target_link_libraries(fp_extract PRIVATE bpmcore)
target_compile_features(fp_extract PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(fp_extract PRIVATE /W4 /utf-8)
    target_compile_definitions(fp_extract PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# foo_tangotagger's fingerprint matching, run over the extracted features.
add_executable(fp_eval fp_eval.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_tangotagger/core/fingerprint.cpp")
target_include_directories(fp_eval PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../foo_tangotagger/core")
target_compile_features(fp_eval PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(fp_eval PRIVATE /W4 /utf-8 /O2)
    target_compile_definitions(fp_eval PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
