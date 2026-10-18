# Local fixes for the lvgl submodule (patches/lvgl/*.patch, in name
# order), applied to its working tree at configure time unless they are
# in already. Included by the simulator and the ESP32-P4 build.
get_filename_component(HOMEPANE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(GLOB HOMEPANE_LVGL_PATCHES "${HOMEPANE_ROOT}/patches/lvgl/*.patch")
list(SORT HOMEPANE_LVGL_PATCHES)
foreach(patch ${HOMEPANE_LVGL_PATCHES})
    execute_process(COMMAND git -C "${HOMEPANE_ROOT}/lvgl" apply --reverse --check "${patch}"
                    RESULT_VARIABLE already OUTPUT_QUIET ERROR_QUIET)
    if(NOT already EQUAL 0)
        execute_process(COMMAND git -C "${HOMEPANE_ROOT}/lvgl" apply "${patch}" RESULT_VARIABLE failed)
        if(failed)
            message(FATAL_ERROR "lvgl patch does not apply: ${patch}")
        endif()
        message(STATUS "lvgl patch applied: ${patch}")
    endif()
endforeach()
