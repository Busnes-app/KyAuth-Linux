# libFuzzer targets. Off by default; needs clang:
#
#   CC=clang CXX=clang++ cmake -B build-fuzz -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug \
#     -DKYAUTH_TESTS=OFF -DKYAUTH_FUZZ=ON
#   cmake --build build-fuzz --target scan_fuzzer
#   build-fuzz/fuzz/scan_fuzzer -max_total_time=60 -dict=fuzz/scan.dict fuzz/corpus/scan
#
# The parser sources are compiled into the fuzzer rather than linked from
# kyauthcore, so they carry the coverage instrumentation.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "KYAUTH_FUZZ needs clang (libFuzzer); set CXX=clang++")
endif()

add_executable(scan_fuzzer scan_fuzzer.cpp
    ${PROJECT_SOURCE_DIR}/src/core/protocol.cpp
    ${PROJECT_SOURCE_DIR}/src/core/totp.cpp)
target_include_directories(scan_fuzzer PRIVATE ${PROJECT_SOURCE_DIR}/src/core)
target_link_libraries(scan_fuzzer PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_compile_options(scan_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined
    -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
target_link_options(scan_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
