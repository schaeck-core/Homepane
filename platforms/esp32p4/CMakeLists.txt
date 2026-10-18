cmake_minimum_required(VERSION 3.16)

# Add LVGL as an extra component from the project root (lvgl/ is two levels up
# from platforms/esp32p4/)
set(EXTRA_COMPONENT_DIRS "../../lvgl")
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/LvglPatches.cmake) # fixes for the lvgl submodule

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_NAME "lvgl_homeassistant")

project(${PROJECT_NAME})

# ESP32-C6 co-processor firmware (platforms/esp32c6_coprocessor/build.sh):
# flashed into the c6fw_0 partition together with the app, so a plain
# `idf.py flash` (and the web flasher, via build/flash_args) delivers it. The
# P4 then pushes it to the C6 over SDIO when it is newer than what the C6
# runs (main/slave_ota.c).
set(C6_FW_BIN "${CMAKE_CURRENT_SOURCE_DIR}/../esp32c6_coprocessor/out/c6fw.bin")
if(EXISTS "${C6_FW_BIN}")
    # A fresh flash boots ota_0, whose C6 image lives in c6fw_0; c6fw_1 is
    # filled by OTA updates into ota_1.
    esptool_py_flash_to_partition(flash "c6fw_0" "${C6_FW_BIN}")
else()
    message(WARNING "No ESP32-C6 firmware at ${C6_FW_BIN} — run platforms/esp32c6_coprocessor/build.sh to include it (c6fw stays as it is on the device)")
endif()
