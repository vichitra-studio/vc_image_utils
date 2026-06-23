cmake_minimum_required(VERSION 3.20)
project(vc_image_utils LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
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
add_library(vc_image_utils_lib STATIC
    src/placeholder.cpp
)

target_include_directories(vc_image_utils_lib
    PUBLIC  include        # public API headers — visible to lib consumers
    PRIVATE third_party   # vendored impl deps — not part of the public interface
)

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
    tests/test_placeholder.cpp
)

target_include_directories(vc_image_utils_tests PRIVATE third_party)

target_link_libraries(vc_image_utils_tests PRIVATE
    vc_image_utils_lib
    vc_warnings
    $<$<BOOL:${ENABLE_SANITIZERS}>:vc_sanitizers>
    $<$<BOOL:${ENABLE_TSAN}>:vc_tsan>
)

add_test(NAME vc_image_utils_tests COMMAND vc_image_utils_tests)
