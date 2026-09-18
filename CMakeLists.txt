cmake_minimum_required(VERSION 3.20)
project(multitini_host_checks LANGUAGES C CXX)

option(MULTITINI_SANITIZE "Enable address and undefined-behavior sanitizers" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

add_library(multitini_common STATIC
    firmware/common/ak4493.c
    firmware/common/clock.c
    firmware/common/c64_boot.c
    firmware/common/c64_content.c
    firmware/common/c64_native_load.c
    firmware/common/c64_d64.c
    firmware/common/c64_g64.c
    firmware/common/c64_rom_files.c
    firmware/common/c64_hid_input.c
    firmware/common/c64_xinput.c
    firmware/common/content_type.c
    firmware/common/core_frontend.c
    firmware/common/core_package.c
    firmware/common/core_link.c
    firmware/common/fatfs_time.c
    firmware/common/gameboy_boot.c
    firmware/common/gameboy_hid_input.c
    firmware/common/vectrex_boot.c
    firmware/common/vectrex_hid_input.c
    firmware/common/vectrex_overlay.c
    firmware/common/menu.c
    firmware/common/media_browser.c
    firmware/common/supervisor.c
    firmware/common/system_frontend.c
    firmware/common/text_reader.c
    firmware/common/tmp102.c
    firmware/common/ui.c
    firmware/common/video_effects.c
    firmware/common/video_effects_frontend.c
    firmware/common/video_frame.c
    firmware/common/xmodem.c
)
target_include_directories(multitini_common PUBLIC firmware/include)
if(NOT MSVC AND NOT CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_link_libraries(multitini_common PUBLIC m)
endif()

enable_testing()
foreach(component clock text_reader system_frontend menu_services video_effects video_effects_frontend video_frame)
    add_executable(test_${component} tests/host/test_${component}.c)
    target_link_libraries(test_${component} PRIVATE multitini_common)
    add_test(NAME ${component} COMMAND test_${component})
endforeach()
add_executable(test_text_document tests/host/test_text_document.c
    firmware/platform/xilinx/text_document.c)
target_include_directories(test_text_document PRIVATE firmware/platform/xilinx)
target_link_libraries(test_text_document PRIVATE multitini_common)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_text_document PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes)
endif()
add_test(NAME text_document COMMAND test_text_document)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME usb1_phy_startup COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_usb1_phy_startup.py)
set_tests_properties(usb1_phy_startup PROPERTIES ENVIRONMENT "CC=${CMAKE_C_COMPILER}")
add_subdirectory(firmware/vectrex32)
add_library(xilinx_vectrex_services_compile OBJECT
    firmware/platform/xilinx/vectrex_content_service.c
    firmware/platform/xilinx/vectrex32_service.c
)
target_include_directories(xilinx_vectrex_services_compile PRIVATE firmware/include)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(xilinx_vectrex_services_compile PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes)
endif()

add_library(xilinx_main_compile OBJECT firmware/platform/xilinx/main.c)
target_include_directories(xilinx_main_compile PRIVATE
    tests/xilinx_stubs
    firmware/include
)

add_library(xilinx_sd_storage_host STATIC
    firmware/platform/xilinx/sd_storage.c
)
target_include_directories(xilinx_sd_storage_host PUBLIC
    tests/xilinx_stubs
    firmware/platform/xilinx
)

add_library(xilinx_persistent_save_host STATIC
    firmware/platform/xilinx/persistent_save.c
)
target_include_directories(xilinx_persistent_save_host PUBLIC
    firmware/platform/xilinx
)

add_library(xilinx_supervisor_settings_host STATIC
    firmware/platform/xilinx/supervisor_settings.c
)
target_include_directories(xilinx_supervisor_settings_host PUBLIC
    firmware/include
    firmware/platform/xilinx
)
target_link_libraries(xilinx_supervisor_settings_host PUBLIC multitini_common)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(multitini_common PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wshadow -Wstrict-prototypes
    )
    target_compile_options(xilinx_main_compile PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wshadow -Wstrict-prototypes
    )
    target_compile_options(xilinx_sd_storage_host PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wshadow -Wstrict-prototypes
    )
    target_compile_options(xilinx_persistent_save_host PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wshadow -Wstrict-prototypes
    )
    target_compile_options(xilinx_supervisor_settings_host PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wshadow -Wstrict-prototypes
    )
    if(MULTITINI_SANITIZE)
        target_compile_options(multitini_common PUBLIC
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
        )
        target_link_options(multitini_common PUBLIC -fsanitize=address,undefined)
        target_compile_options(xilinx_supervisor_settings_host PUBLIC
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
        )
        target_link_options(xilinx_supervisor_settings_host PUBLIC
            -fsanitize=address,undefined
        )
    endif()
endif()

add_executable(test_vectrex_firmware tests/host/test_vectrex_firmware.c
    firmware/platform/xilinx/vectrex_content_service.c
    firmware/platform/xilinx/vectrex32_service.c)
target_include_directories(test_vectrex_firmware PRIVATE firmware/platform/xilinx)
target_link_libraries(test_vectrex_firmware PRIVATE multitini_common multitini_vectrex32)
add_test(NAME vectrex_firmware COMMAND test_vectrex_firmware)

add_executable(test_video_effect_service tests/host/test_video_effect_service.c
    firmware/platform/xilinx/video_effect_service.c
    firmware/platform/xilinx/video_frame_service.c)
target_include_directories(test_video_effect_service PRIVATE firmware/platform/xilinx)
target_link_libraries(test_video_effect_service PRIVATE multitini_common)
add_test(NAME video_effect_service COMMAND test_video_effect_service)

add_executable(test_media_browser tests/host/test_media_browser.c)
target_link_libraries(test_media_browser PRIVATE multitini_common)
add_test(NAME media_browser COMMAND test_media_browser)

add_executable(test_core_link tests/host/test_core_link.c)
target_link_libraries(test_core_link PRIVATE multitini_common)
add_test(NAME core_link COMMAND test_core_link)

add_executable(test_vectrex_overlay tests/host/test_vectrex_overlay.c)
target_link_libraries(test_vectrex_overlay PRIVATE multitini_common)
add_test(NAME vectrex_overlay COMMAND test_vectrex_overlay)

add_executable(test_core_package tests/host/test_core_package.c)
target_link_libraries(test_core_package PRIVATE multitini_common)
add_test(NAME core_package COMMAND test_core_package)

add_executable(test_supervisor tests/host/test_supervisor.c)
target_link_libraries(test_supervisor PRIVATE multitini_common)
add_test(NAME supervisor COMMAND test_supervisor)

add_executable(test_core_catalog_sd
    tests/host/test_core_catalog_sd.c
    firmware/platform/xilinx/core_catalog_sd.c
)
target_include_directories(test_core_catalog_sd PRIVATE
    firmware/platform/xilinx
)
target_link_libraries(test_core_catalog_sd PRIVATE multitini_common)
add_test(NAME core_catalog_sd COMMAND test_core_catalog_sd)

add_executable(test_ui tests/host/test_ui.c)
target_link_libraries(test_ui PRIVATE multitini_common)
add_test(NAME ui COMMAND test_ui)

add_executable(test_menu tests/host/test_menu.c)
target_link_libraries(test_menu PRIVATE multitini_common)
add_test(NAME menu COMMAND test_menu)

# The board-handler fixture uses GCC whole-program LTO to discard unrelated
# bare-metal startup while executing the real MMIO and settings handlers.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME video_settings_runtime
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/scripts/test_video_settings_runtime.py"
            --cc "${CMAKE_C_COMPILER}"
            --build-dir "${CMAKE_BINARY_DIR}/video-settings-runtime")
endif()

add_executable(test_core_frontend tests/host/test_core_frontend.c)
target_link_libraries(test_core_frontend PRIVATE multitini_common)
add_test(NAME core_frontend COMMAND test_core_frontend)

add_executable(menu_preview tools/menu_preview.c)
target_link_libraries(menu_preview PRIVATE multitini_common)

add_executable(test_content_type tests/host/test_content_type.c)
target_link_libraries(test_content_type PRIVATE multitini_common)
add_test(NAME content_type COMMAND test_content_type)

add_executable(test_fatfs_time tests/host/test_fatfs_time.c)
target_link_libraries(test_fatfs_time PRIVATE multitini_common)
add_test(NAME fatfs_time COMMAND test_fatfs_time)

add_executable(test_c64_boot tests/host/test_c64_boot.c)
target_link_libraries(test_c64_boot PRIVATE multitini_common)
add_test(NAME c64_boot COMMAND test_c64_boot)

add_executable(test_c64_content tests/host/test_c64_content.c)
target_link_libraries(test_c64_content PRIVATE multitini_common)
add_test(NAME c64_content COMMAND test_c64_content)

add_executable(test_c64_native_load tests/host/test_c64_native_load.c)
target_link_libraries(test_c64_native_load PRIVATE multitini_common)
add_test(NAME c64_native_load COMMAND test_c64_native_load)

add_executable(test_c64_content_service
    tests/host/test_c64_content_service.c
    firmware/platform/xilinx/c64_content_service.c
)
target_include_directories(test_c64_content_service PRIVATE
    firmware/platform/xilinx
)
target_link_libraries(test_c64_content_service PRIVATE multitini_common)
add_test(NAME c64_content_service COMMAND test_c64_content_service)

add_executable(test_c64_d64 tests/host/test_c64_d64.c)
target_link_libraries(test_c64_d64 PRIVATE multitini_common)
add_test(NAME c64_d64 COMMAND test_c64_d64)

add_executable(test_c64_g64 tests/host/test_c64_g64.c)
target_link_libraries(test_c64_g64 PRIVATE multitini_common)
add_test(NAME c64_g64 COMMAND test_c64_g64)

add_executable(test_c64_d64_service
    tests/host/test_c64_d64_service.c
    firmware/platform/xilinx/c64_d64_service.c
)
target_include_directories(test_c64_d64_service PRIVATE
    tests/xilinx_stubs
    firmware/platform/xilinx
)
target_link_libraries(test_c64_d64_service PRIVATE multitini_common)
add_test(NAME c64_d64_service COMMAND test_c64_d64_service)

add_executable(test_c64_hid_input tests/host/test_c64_hid_input.c)
target_link_libraries(test_c64_hid_input PRIVATE multitini_common)
add_test(NAME c64_hid_input COMMAND test_c64_hid_input)

add_executable(test_c64_xinput tests/host/test_c64_xinput.c)
target_link_libraries(test_c64_xinput PRIVATE multitini_common)
add_test(NAME c64_xinput COMMAND test_c64_xinput)

add_executable(test_gameboy_boot tests/host/test_gameboy_boot.c)
target_link_libraries(test_gameboy_boot PRIVATE multitini_common)
add_test(NAME gameboy_boot COMMAND test_gameboy_boot)

add_executable(test_gameboy_content_service
    tests/host/test_gameboy_content_service.c
    firmware/platform/xilinx/gameboy_content_service.c
)
target_include_directories(test_gameboy_content_service PRIVATE
    firmware/platform/xilinx
)
target_link_libraries(test_gameboy_content_service PRIVATE
    multitini_common
    xilinx_persistent_save_host
)
add_test(NAME gameboy_content_service COMMAND test_gameboy_content_service)

add_executable(test_gameboy_hid_input tests/host/test_gameboy_hid_input.c)
target_link_libraries(test_gameboy_hid_input PRIVATE multitini_common)
add_test(NAME gameboy_hid_input COMMAND test_gameboy_hid_input)

add_executable(test_ak4493 tests/host/test_ak4493.c)
target_link_libraries(test_ak4493 PRIVATE multitini_common)
add_test(NAME ak4493 COMMAND test_ak4493)

add_executable(test_content_fuzz tests/host/test_content_fuzz.c)
target_link_libraries(test_content_fuzz PRIVATE multitini_common)
add_test(NAME content_fuzz COMMAND test_content_fuzz)

add_executable(test_tmp102 tests/host/test_tmp102.c)
target_link_libraries(test_tmp102 PRIVATE multitini_common)
add_test(NAME tmp102 COMMAND test_tmp102)

add_executable(test_xmodem tests/host/test_xmodem.c)
target_link_libraries(test_xmodem PRIVATE multitini_common)
add_test(NAME xmodem COMMAND test_xmodem)

add_executable(test_cherryusb_ulpi tests/host/test_cherryusb_ulpi.c)
target_include_directories(test_cherryusb_ulpi PRIVATE
    firmware/platform/xilinx
)
add_test(NAME cherryusb_ulpi COMMAND test_cherryusb_ulpi)

add_executable(test_sd_storage tests/host/test_sd_storage.c)
target_link_libraries(test_sd_storage PRIVATE
    xilinx_supervisor_settings_host xilinx_sd_storage_host)
add_test(NAME sd_storage COMMAND test_sd_storage)

add_executable(test_persistent_save tests/host/test_persistent_save.c)
target_link_libraries(test_persistent_save PRIVATE
    xilinx_persistent_save_host
)
add_test(NAME persistent_save COMMAND test_persistent_save)

add_executable(test_supervisor_settings
    tests/host/test_supervisor_settings.c
)
target_link_libraries(test_supervisor_settings PRIVATE
    xilinx_supervisor_settings_host
)
add_test(NAME supervisor_settings COMMAND test_supervisor_settings)
