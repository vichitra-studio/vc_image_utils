cmake_minimum_required(VERSION 3.20)
project(vc_image_utils LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------- ccache ----------
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

# ---------- warnings ----------
add_library(vc_warnings INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(vc_warnings INTERFACE
        -Wall           # common bugs: unused vars, missing return, signed/unsigned compare
        -Wextra         # extras beyond -Wall: unused params, missing field initialisers
        -Wpedantic      # strict ISO C++ only — pairs with CMAKE_CXX_EXTENSIONS OFF
        -Wshadow        # local var silently hides outer var with the same name
        -Wconversion    # implicit narrowing: double→float, int→short (critical for pixel math)
        -Wsign-conversion   # signed↔unsigned implicit conversion (common bug with size_t)
        -Wnon-virtual-dtor  # class has virtual functions but non-virtual destructor → UB/leak
        -Wold-style-cast    # forbids (int)x — forces static_cast<int>(x) to make intent explicit
        -Wdouble-promotion  # float silently promoted to double (expensive, wrong in pixel math)
        -Wimplicit-fallthrough  # switch case falls through without [[fallthrough]] annotation
        -Wnull-dereference  # potential null pointer dereference detected by flow analysis
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(vc_warnings INTERFACE /W4)
endif()

# ---------- sanitizers ----------
option(ENABLE_SANITIZERS "Enable AddressSanitizer + UBSan + float-div-by-zero" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer (mutually exclusive with ENABLE_SANITIZERS)" OFF)

if(ENABLE_SANITIZERS AND ENABLE_TSAN)
    message(FATAL_ERROR "ENABLE_SANITIZERS and ENABLE_TSAN are mutually exclusive")
endif()

if(ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "ENABLE_SANITIZERS requires clang or GCC — not supported with MSVC. Use clang-cl or switch to a POSIX toolchain.")
    endif()
    add_library(vc_sanitizers INTERFACE)
    target_compile_options(vc_sanitizers INTERFACE
        -fsanitize=address,undefined,float-divide-by-zero
        # ASan:  heap/stack buffer overflows, use-after-free, memory leaks
        # UBSan: null deref, signed overflow, misaligned pointer, invalid enum/bool, bad shift
        # float-divide-by-zero: not included in 'undefined' by default — catches div/0 in pixel math
        -fno-omit-frame-pointer  # keeps stack frames readable in ASan reports
    )
    target_link_options(vc_sanitizers INTERFACE
        -fsanitize=address,undefined,float-divide-by-zero
    )
endif()

if(ENABLE_TSAN)
    if(MSVC)
        message(FATAL_ERROR "ENABLE_TSAN requires clang or GCC — not supported with MSVC. Use clang-cl or switch to a POSIX toolchain.")
    endif()
    add_library(vc_tsan INTERFACE)
    target_compile_options(vc_tsan INTERFACE
        -fsanitize=thread,undefined,float-divide-by-zero
        # TSan:  data races between threads
        # UBSan + float-divide-by-zero: same coverage as debug-sanitizers (TSan is compatible with UBSan)
        -fno-omit-frame-pointer
    )
    target_link_options(vc_tsan INTERFACE
        -fsanitize=thread,undefined,float-divide-by-zero
    )
endif()

# ---------- library ----------
# Shared source list — reused verbatim by the benchmark-only library copy
# (vc_bench_lib, built with pinned -march) so the two never drift.
set(VC_LIB_SOURCES
    src/vc_exception.cpp
    src/vc_error_code.cpp
    src/vc_image.cpp
    src/io/vc_io_stb.cpp
    src/utils/vc_log.cpp
    src/utils/vc_image_dumper.cpp
    src/utils/vc_perf.cpp
    src/pipe/vc_pipe_contract.cpp
    src/pipe/vc_pipe_context.cpp
    src/pipe/vc_pipeline.cpp
    src/pipe/stages/vc_passthrough_stage.cpp
    src/pipe/stages/vc_grayscale_stage.cpp
    src/pipe/stages/vc_mean_brightness_stage.cpp
)

add_library(vc_image_utils_lib STATIC ${VC_LIB_SOURCES})

target_include_directories(vc_image_utils_lib PUBLIC include)
target_include_directories(vc_image_utils_lib SYSTEM PRIVATE third_party)

target_link_libraries(vc_image_utils_lib PRIVATE
    vc_warnings
    $<$<BOOL:${ENABLE_SANITIZERS}>:vc_sanitizers>
    $<$<BOOL:${ENABLE_TSAN}>:vc_tsan>
)

# ---------- app ----------
add_executable(vc_image_utils_app
    src/main.cpp
)

target_link_libraries(vc_image_utils_app PRIVATE
    vc_image_utils_lib
    vc_warnings
    $<$<BOOL:${ENABLE_SANITIZERS}>:vc_sanitizers>
    $<$<BOOL:${ENABLE_TSAN}>:vc_tsan>
)

# ---------- tests ----------
enable_testing()

add_executable(vc_image_utils_tests
    tests/test_vc_image.cpp
    tests/test_vc_pixel_buffer.cpp
    tests/test_vc_log.cpp
    tests/test_vc_image_dumper.cpp
    tests/test_vc_perf.cpp
    tests/test_vc_pipe.cpp
)

target_include_directories(vc_image_utils_tests SYSTEM PRIVATE third_party)

target_compile_definitions(vc_image_utils_tests PRIVATE
    VC_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
    VC_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(vc_image_utils_tests PRIVATE
    vc_image_utils_lib
    vc_warnings
    $<$<BOOL:${ENABLE_SANITIZERS}>:vc_sanitizers>
    $<$<BOOL:${ENABLE_TSAN}>:vc_tsan>
)

add_test(NAME vc_image_utils_tests COMMAND vc_image_utils_tests)

# ---------- benchmarks ----------
# Off by default: a normal build and the test run are unaffected. Benchmarks are
# Release-only with PINNED flags, in targets separate from the doctest test
# binary — a -O0/sanitizer build must never leak into a perf number.
# See docs/benchmarking.md Sec 4.4, Sec 8.
option(VC_BUILD_BENCHMARKS "Build performance benchmarks (nanobench, Release only)" OFF)

if(VC_BUILD_BENCHMARKS)
    # A sanitizer build is a different, slower binary — it must never be timed.
    if(ENABLE_SANITIZERS OR ENABLE_TSAN)
        message(FATAL_ERROR
            "VC_BUILD_BENCHMARKS is incompatible with ENABLE_SANITIZERS/ENABLE_TSAN: "
            "a sanitizer/-O0 build must never leak into a performance number "
            "(docs/benchmarking.md Sec 4.4). Configure benchmarks in a separate Release build dir.")
    endif()

    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        message(WARNING
            "VC_BUILD_BENCHMARKS is ON but CMAKE_BUILD_TYPE is '${CMAKE_BUILD_TYPE}', not 'Release'. "
            "Benchmark numbers are only meaningful in an optimized build.")
    endif()

    # Pinned, not left to default: two runs on the same machine diverge otherwise
    # (Sec 4.4). 'native' is fastest but its numbers are not portable across
    # machines — mitigated by host-keyed baselines (Sec 11 open question).
    set(VC_BENCH_MARCH "native" CACHE STRING "-march value for benchmark targets (pinned)")

    find_package(Threads REQUIRED)  # nanobench uses threading facilities

    # A benchmark-only COPY of the library, compiled with the pinned bench flags.
    # Header-inline code (vc_pixel_buffer::as<>, the ctor) is compiled INTO the
    # bench TU and already gets -march via vc_bench_opts — but a kernel authored
    # in a library .cpp (vc_grayscale_stage::process, etc.) is compiled in the
    # library, and would otherwise be benchmarked WITHOUT the pinned -march,
    # understating it. Building a SEPARATE target (not mutating vc_image_utils_lib
    # in place) means the app/test binaries never inherit -march=native even if
    # someone enables benchmarks in a shared build dir — no reliance on the
    # "dedicated dir" convention. Built from the shared VC_LIB_SOURCES, so it
    # cannot drift from the real library. See docs/benchmarking.md Sec 4.4.
    add_library(vc_bench_lib STATIC ${VC_LIB_SOURCES})
    target_include_directories(vc_bench_lib PUBLIC include)
    target_include_directories(vc_bench_lib SYSTEM PRIVATE third_party)
    target_compile_options(vc_bench_lib PRIVATE -O3 -march=${VC_BENCH_MARCH})
    target_link_libraries(vc_bench_lib PRIVATE vc_warnings)

    # Reproducibility metadata, injected as compile defs (the mechanism the test
    # target uses for VC_TEST_DATA_DIR). Keys every baseline to a fixed
    # (bench definition + host + compiler + flags) — never compare across them.
    cmake_host_system_information(RESULT VC_BENCH_HOST QUERY HOSTNAME)
    set(VC_BENCH_COMPILER "${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
    # Key flags stamped into every baseline. Includes -DNDEBUG because Release
    # defines it (it removes asserts, which materially affects timings) — the
    # bench targets are Release-only. Not the exhaustive compile line (the -W set
    # is omitted as perf-irrelevant); it is the reproducibility-relevant subset.
    set(VC_BENCH_FLAGS "-O3 -DNDEBUG -march=${VC_BENCH_MARCH} -std=c++${CMAKE_CXX_STANDARD}")
    execute_process(
        COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE VC_BENCH_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT VC_BENCH_GIT_SHA)
        set(VC_BENCH_GIT_SHA "nogit")
    endif()

    # Shared options carrier: pinned flags, include paths (nanobench as SYSTEM so
    # its 3.5k lines never trip our -Wconversion/-Wold-style-cast), the metadata
    # defs, and the link set. Both the support lib and the bench executables use it.
    add_library(vc_bench_opts INTERFACE)
    target_compile_options(vc_bench_opts INTERFACE -O3 -march=${VC_BENCH_MARCH})
    target_include_directories(vc_bench_opts INTERFACE bench)
    target_include_directories(vc_bench_opts SYSTEM INTERFACE third_party/nanobench)
    target_compile_definitions(vc_bench_opts INTERFACE
        VC_BENCH_HOST="${VC_BENCH_HOST}"
        VC_BENCH_COMPILER="${VC_BENCH_COMPILER}"
        VC_BENCH_MARCH="${VC_BENCH_MARCH}"
        VC_BENCH_FLAGS="${VC_BENCH_FLAGS}"
        VC_BENCH_GIT_SHA="${VC_BENCH_GIT_SHA}"
        VC_BENCH_BASELINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/bench/baselines"
    )
    target_link_libraries(vc_bench_opts INTERFACE
        vc_bench_lib
        vc_warnings
        Threads::Threads
    )

    # nanobench's single implementation TU + the shared harness, compiled once.
    add_library(vc_bench_support_lib STATIC
        bench/vc_bench_impl.cpp
        bench/vc_bench_support.cpp
    )
    target_link_libraries(vc_bench_support_lib PUBLIC vc_bench_opts)

    # One binary per category (Sec 7): the scaling mechanism, since nanobench has
    # no auto-registration / name filter — each main() forwards to run_suite().
    foreach(target vc_benchmark_harness vc_benchmark_pipe vc_benchmark_pipeline)
        add_executable(${target} bench/${target}.cpp)
        target_link_libraries(${target} PRIVATE vc_bench_support_lib vc_bench_opts)
    endforeach()
endif()
