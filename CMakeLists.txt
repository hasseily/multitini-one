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
    firmware/common/display.c
    firmware/common/content_type.c
    firmware/common/controls.c
    firmware/common/screenshot.c
    firmware/common/core_frontend.c
    firmware/common/core_package.c
    firmware/common/core_link.c
    firmware/common/fatfs_time.c
    firmware/common/gamepad.c
    firmware/common/gameboy_boot.c
    firmware/common/gameboy_hid_input.c
    firmware/common/genesis_boot.c
    firmware/common/genesis_content.c
    firmware/common/genesis_hid_input.c
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
add_executable(test_gamepad tests/host/test_gamepad.c)
target_link_libraries(test_gamepad PRIVATE multitini_common)
add_test(NAME gamepad COMMAND test_gamepad)

add_executable(test_controls tests/host/test_controls.c)
target_link_libraries(test_controls PRIVATE multitini_common)
add_test(NAME controls COMMAND test_controls)

add_executable(test_screenshot tests/host/test_screenshot.c)
target_link_libraries(test_screenshot PRIVATE multitini_common)
add_test(NAME screenshot COMMAND test_screenshot)

add_executable(test_screenshot_service tests/host/test_screenshot_service.c)
target_include_directories(test_screenshot_service PRIVATE firmware/platform/xilinx)
target_link_libraries(test_screenshot_service PRIVATE multitini_common)
add_test(NAME screenshot_service COMMAND test_screenshot_service)

# Exercise the actual USB drivers against host-controller fault fixtures.
# Initialize the pinned submodule before running the complete host suite.
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/CherryUSB/core/usbh_core.h")
    add_library(usb_test_headers INTERFACE)
    target_include_directories(usb_test_headers INTERFACE
        firmware/include firmware/platform/xilinx
        third_party/CherryUSB/common third_party/CherryUSB/core
        third_party/CherryUSB/class/hub third_party/CherryUSB/class/hid
        third_party/CherryUSB/class/vendor/xbox third_party/CherryUSB/port/ehci)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(usb_test_headers INTERFACE -Wall -Wextra -Werror)
    endif()
    if(APPLE)
        target_compile_options(usb_test_headers INTERFACE
            -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
            -include "${CMAKE_SOURCE_DIR}/tests/host/usb_macho_sections.h")
    endif()
    foreach(driver usb_ps4 usb_ehci_completion usb_xbox_one)
        add_executable(test_${driver} tests/host/test_${driver}.c)
        target_link_libraries(test_${driver} PRIVATE usb_test_headers)
        add_test(NAME ${driver} COMMAND test_${driver})
    endforeach()
    add_executable(test_usb_hid_runtime tests/host/test_usb_hid_runtime.c)
    target_include_directories(test_usb_hid_runtime PRIVATE tests/xilinx_stubs)
    target_link_libraries(test_usb_hid_runtime PRIVATE usb_test_headers multitini_common)
    add_test(NAME usb_hid_runtime COMMAND test_usb_hid_runtime)
    target_sources(test_usb_xbox_one PRIVATE firmware/platform/xilinx/usb_xbox_one.c)
    add_library(usb_hid_service_compile OBJECT firmware/platform/xilinx/usb_hid_service.c)
    target_include_directories(usb_hid_service_compile PRIVATE tests/xilinx_stubs)
    target_link_libraries(usb_hid_service_compile PRIVATE usb_test_headers)
else()
    message(FATAL_ERROR "Run git submodule update --init third_party/CherryUSB for USB tests")
endif()
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
add_test(NAME screenshot_png COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/scripts/test_screenshot_png.py
    --helper $<TARGET_FILE:test_screenshot>)
add_test(NAME image_manifest COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/scripts/test_image_manifest.py)
add_test(NAME usb1_phy_startup COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_usb1_phy_startup.py)
add_test(NAME multiboot_manifest COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_multiboot_manifest.py)
add_test(NAME multiboot_switch COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_multiboot_switch.py)
add_test(NAME golden_serial_selection COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_golden_serial_selection.py)
set_tests_properties(golden_serial_selection PROPERTIES ENVIRONMENT "CC=${CMAKE_C_COMPILER}")
add_test(NAME multiboot_bundle COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_multiboot_bundle.py)
add_test(NAME multiboot_status_cache COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_multiboot_status_cache.py)
set_tests_properties(multiboot_status_cache PROPERTIES ENVIRONMENT "CC=${CMAKE_C_COMPILER}")
add_test(NAME qspi_performance COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_qspi_performance.py)
set_tests_properties(qspi_performance PROPERTIES ENVIRONMENT "CC=${CMAKE_C_COMPILER}")
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

foreach(component genesis_boot genesis_content genesis_hid_input)
    add_executable(test_${component} tests/host/test_${component}.c)
    target_link_libraries(test_${component} PRIVATE multitini_common)
    add_test(NAME ${component} COMMAND test_${component})
endforeach()
add_executable(test_genesis_content_service
    tests/host/test_genesis_content_service.c
    firmware/platform/xilinx/genesis_content_service.c)
target_include_directories(test_genesis_content_service PRIVATE firmware/platform/xilinx)
target_link_libraries(test_genesis_content_service PRIVATE
    multitini_common xilinx_persistent_save_host)
add_test(NAME genesis_content_service COMMAND test_genesis_content_service)

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

# Compile every imported recovery source against the BSP API fixtures, and run
# the production self-update engine with a page/sector fault-injection NOR.
file(GLOB GOLDEN_BOOT_SOURCES CONFIGURE_DEPENDS firmware/golden/bootloader/*.c)
add_library(golden_compile OBJECT ${GOLDEN_BOOT_SOURCES}
    firmware/golden/lib/qspi_nor.c firmware/golden/lib/uart.c)
target_include_directories(golden_compile PRIVATE tests/xilinx_stubs)
add_executable(test_golden_self_update tests/host/test_golden_self_update.c
    firmware/golden/lib/golden_self_update.c firmware/golden/lib/crc32.c
    firmware/golden/lib/multiboot.c)
target_include_directories(test_golden_self_update PRIVATE
    tests/xilinx_stubs firmware/golden/lib)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_golden_self_update PRIVATE -Wall -Wextra -Werror)
    if(MULTITINI_SANITIZE)
        target_compile_options(test_golden_self_update PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(test_golden_self_update PRIVATE -fsanitize=address,undefined)
    endif()
endif()
add_test(NAME golden_self_update COMMAND test_golden_self_update)
add_executable(test_multiboot_updater tests/host/test_multiboot_updater.c
    firmware/golden/bootloader/updater.c firmware/golden/bootloader/updater_layout.c
    firmware/golden/lib/multiboot.c firmware/golden/lib/crc32.c)
target_include_directories(test_multiboot_updater PRIVATE tests/xilinx_stubs
    firmware/golden/lib firmware/golden/bootloader)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_multiboot_updater PRIVATE -Wall -Wextra -Werror)
    if(MULTITINI_SANITIZE)
        target_compile_options(test_multiboot_updater PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(test_multiboot_updater PRIVATE -fsanitize=address,undefined)
    endif()
endif()
add_test(NAME multiboot_updater COMMAND test_multiboot_updater)
add_executable(test_crc32 tests/host/test_crc32.c firmware/golden/lib/crc32.c)
target_include_directories(test_crc32 PRIVATE firmware/golden/lib)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_crc32 PRIVATE -Wall -Wextra -Werror)
    if(MULTITINI_SANITIZE)
        target_compile_options(test_crc32 PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(test_crc32 PRIVATE -fsanitize=address,undefined)
    endif()
endif()
add_test(NAME crc32 COMMAND test_crc32)
add_library(multiboot_host OBJECT firmware/golden/lib/multiboot.c)
target_include_directories(multiboot_host PRIVATE firmware/golden/lib)
add_executable(test_multiboot tests/host/test_multiboot.c
    $<TARGET_OBJECTS:multiboot_host> firmware/golden/lib/crc32.c)
target_include_directories(test_multiboot PRIVATE firmware/golden/lib)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_multiboot PRIVATE -Wall -Wextra -Werror)
    target_compile_options(multiboot_host PRIVATE -Wall -Wextra -Werror)
    if(MULTITINI_SANITIZE)
        target_compile_options(test_multiboot PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_compile_options(multiboot_host PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(test_multiboot PRIVATE -fsanitize=address,undefined)
    endif()
endif()
add_test(NAME multiboot COMMAND test_multiboot)
add_test(NAME multiboot_marker_identity COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/scripts/test_multiboot_marker_identity.py
    $<TARGET_OBJECTS:multiboot_host>)
add_executable(test_video_worker_reset tests/host/test_video_worker_reset.c)
target_include_directories(test_video_worker_reset PRIVATE firmware/include
    firmware/platform/xilinx tests/fixtures/video_worker_reset tests/xilinx_stubs)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_video_worker_reset PRIVATE -Wall -Wextra -Werror)
    if(MULTITINI_SANITIZE)
        target_compile_options(test_video_worker_reset PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(test_video_worker_reset PRIVATE -fsanitize=address,undefined)
    endif()
endif()
add_test(NAME video_worker_reset COMMAND test_video_worker_reset)
add_test(NAME video_worker_handoff_order COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/scripts/test_video_worker_handoff_order.py)
add_executable(test_display tests/host/test_display.c)
target_link_libraries(test_display PRIVATE multitini_common)
add_test(NAME display COMMAND test_display)
