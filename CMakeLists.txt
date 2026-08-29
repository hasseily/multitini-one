cmake_minimum_required(VERSION 3.20)
project(multitini_host_checks LANGUAGES C)

option(MULTITINI_SANITIZE "Enable address and undefined-behavior sanitizers" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(multitini_common STATIC
    firmware/common/ak4493.c
    firmware/common/c64_boot.c
    firmware/common/content_type.c
    firmware/common/core_link.c
    firmware/common/tmp102.c
)
target_include_directories(multitini_common PUBLIC firmware/include)

add_library(xilinx_main_compile OBJECT firmware/platform/xilinx/main.c)
target_include_directories(xilinx_main_compile PRIVATE
    tests/xilinx_stubs
    firmware/include
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(multitini_common PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wshadow -Wstrict-prototypes
    )
    target_compile_options(xilinx_main_compile PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wshadow -Wstrict-prototypes
    )
    if(MULTITINI_SANITIZE)
        target_compile_options(multitini_common PUBLIC
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(multitini_common PUBLIC -fsanitize=address,undefined)
    endif()
endif()

enable_testing()

add_executable(test_core_link tests/host/test_core_link.c)
target_link_libraries(test_core_link PRIVATE multitini_common)
add_test(NAME core_link COMMAND test_core_link)

add_executable(test_content_type tests/host/test_content_type.c)
target_link_libraries(test_content_type PRIVATE multitini_common)
add_test(NAME content_type COMMAND test_content_type)

add_executable(test_c64_boot tests/host/test_c64_boot.c)
target_link_libraries(test_c64_boot PRIVATE multitini_common)
add_test(NAME c64_boot COMMAND test_c64_boot)

add_executable(test_ak4493 tests/host/test_ak4493.c)
target_link_libraries(test_ak4493 PRIVATE multitini_common)
add_test(NAME ak4493 COMMAND test_ak4493)

add_executable(test_content_fuzz tests/host/test_content_fuzz.c)
target_link_libraries(test_content_fuzz PRIVATE multitini_common)
add_test(NAME content_fuzz COMMAND test_content_fuzz)

add_executable(test_tmp102 tests/host/test_tmp102.c)
target_link_libraries(test_tmp102 PRIVATE multitini_common)
add_test(NAME tmp102 COMMAND test_tmp102)
