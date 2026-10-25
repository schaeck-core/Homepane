cmake_minimum_required(VERSION 3.10)
project(lvgl C CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LvglPatches.cmake) # fixes for the lvgl submodule

# Set the correct FreeRTOS port for your system (e.g., Posix for WSL)
set(FREERTOS_PORT GCC_POSIX CACHE STRING "Port for FreeRTOS on Posix environment")
set(LWS_WITH_SSL OFF CACHE BOOL "Disable SSL for libwebsockets" FORCE)

unset(USE_FREERTOS CACHE)

option(USE_FREERTOS "Enable FreeRTOS" OFF) # Turn this on to enable FreeRTOS

if(USE_FREERTOS)
    message(STATUS "FreeRTOS is enabled")

    # FreeRTOS integration when USE_FREERTOS is enabled
    add_library(freertos_config INTERFACE)
    target_include_directories(freertos_config SYSTEM INTERFACE ${PROJECT_SOURCE_DIR}/config)
    target_compile_definitions(freertos_config INTERFACE projCOVERAGE_TEST=0)

    # Add FreeRTOS as a subdirectory
    add_subdirectory(FreeRTOS)

    # FreeRTOS-specific include directories
    include_directories(${PROJECT_SOURCE_DIR}/FreeRTOS/include)
    include_directories(${PROJECT_SOURCE_DIR}/FreeRTOS/portable/ThirdParty/GCC/Posix)
    include_directories(${PROJECT_SOURCE_DIR}/config)

    # Add FreeRTOS sources
    file(GLOB FREERTOS_SOURCES
        "${PROJECT_SOURCE_DIR}/FreeRTOS/*.c"
        "${PROJECT_SOURCE_DIR}/FreeRTOS/portable/MemMang/heap_4.c"
        "${PROJECT_SOURCE_DIR}/FreeRTOS/portable/ThirdParty/GCC/Posix/*.c"
    )
else()
    message(STATUS "FreeRTOS is disabled")
    set(FREERTOS_SOURCES "")  # No FreeRTOS sources if FreeRTOS is disabled
endif()

# Main include files of the project (shared app code)
include_directories(${PROJECT_SOURCE_DIR}/app)

# Define options for LVGL with default values (OFF)
option(LV_USE_DRAW_SDL "Use SDL draw unit" OFF)
option(LV_USE_LIBPNG "Use libpng to decode PNG" OFF)
option(LV_USE_LIBJPEG_TURBO "Use libjpeg turbo to decode JPEG" OFF)
option(LV_USE_FFMPEG "Use libffmpeg to display video using lv_ffmpeg" OFF)
option(LV_USE_FREETYPE "Use freetype library" OFF)
option(USE_GENERATED_FONTS "Use pre-generated bitmap C fonts instead of runtime tiny_ttf" ON)

# Set C and C++ standards
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the output path for the executable
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# Find and include SDL2 library
find_package(SDL2 REQUIRED)

# Find and include libwebsockets library
# find_package(libwebsockets REQUIRED)

# Remove ARM-specific compile and linker options
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")

# Add compile definitions based on the selected options
add_compile_definitions($<$<BOOL:${LV_USE_DRAW_SDL}>:LV_USE_DRAW_SDL=1>)
add_compile_definitions($<$<BOOL:${LV_USE_LIBPNG}>:LV_USE_LIBPNG=1>)
add_compile_definitions($<$<BOOL:${LV_USE_LIBJPEG_TURBO}>:LV_USE_LIBJPEG_TURBO=1>)
add_compile_definitions($<$<BOOL:${LV_USE_FFMPEG}>:LV_USE_FFMPEG=1>)


# Disable building LVGL examples and demos to speed up the build
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "Disable building lvgl/examples" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "Disable building lvgl/demos" FORCE)

# Add LVGL subdirectory
add_subdirectory(lvgl)
target_include_directories(lvgl PUBLIC ${PROJECT_SOURCE_DIR} ${SDL2_INCLUDE_DIRS})

# Find packages installed via vcpkg
find_package(libwebsockets CONFIG REQUIRED)
find_package(cJSON CONFIG REQUIRED)
find_package(CURL REQUIRED)

# libmicrohttpd only provides pkg-config, not CMake targets
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBMICROHTTPD REQUIRED IMPORTED_TARGET libmicrohttpd)

# Dashboard tiles and detail views of the design guide (app/cpp/ui/tiles, details)
file(GLOB HOMEPANE_TILE_SOURCES ${PROJECT_SOURCE_DIR}/app/cpp/ui/tiles/*.cpp ${PROJECT_SOURCE_DIR}/app/cpp/ui/details/*.cpp
     ${PROJECT_SOURCE_DIR}/app/cpp/media/*.cpp # pictures (camera tile, covers)
     ${PROJECT_SOURCE_DIR}/app/cpp/core/IsoTime.cpp ${PROJECT_SOURCE_DIR}/app/cpp/core/DateText.cpp)

# Create the main executable (Simulator build - FreeRTOS removed)
add_executable(main
    # Platform-specific (Simulator)
    ${PROJECT_SOURCE_DIR}/platforms/simulator/main.c
    ${PROJECT_SOURCE_DIR}/platforms/simulator/webserver.c
    ${PROJECT_SOURCE_DIR}/platforms/simulator/LwsTransport.cpp
    ${PROJECT_SOURCE_DIR}/platforms/simulator/SimWifiAdapter.cpp
    ${PROJECT_SOURCE_DIR}/platforms/simulator/SimUpdateAdapter.cpp
    ${PROJECT_SOURCE_DIR}/platforms/simulator/DemoHaTransport.cpp
    ${PROJECT_SOURCE_DIR}/platforms/simulator/sim_script.c
    # Shared App Code (C++ core skeleton, C++ migration phase 1/2/3)
    ${PROJECT_SOURCE_DIR}/app/cpp/app_facade.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/Application.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/AppStateController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/DashboardController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/SetupController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/PairingController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/WifiController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/UpdateController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/doorbell/DoorbellController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/eventcalendar/EventFeed.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/DoorbellOverlay.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/Screensaver.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/eventcalendar/EventFeed.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/voice/PipelineEvent.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/voice/VoiceController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/voice/CancelPhrases.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/voice/band_levels.c
    ${PROJECT_SOURCE_DIR}/app/cpp/voice/earcons.c
    ${PROJECT_SOURCE_DIR}/app/cpp/settings/SettingRules.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/mqtt/HaDiscovery.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/DeviceLink.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/mqtt/MqttController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/sensors/SimulatedSensors.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/update/UpdateManifest.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/PanelListParser.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/platform/PortableDefaults.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/core/UiDispatcher.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntityStore.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntitySubscriptionCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaCommandService.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/music_assistant/MaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/music_assistant/MaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/music_assistant/MaPlayerStore.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/music_assistant/MaController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/music/MusicPlayer.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/music/MusicQueuePage.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/music/MusicBrowsePage.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/music/MusicItemPage.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/music/MusicItems.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/music/MusicCovers.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/music/MusicPullGesture.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/music/MusicTrackPopover.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardParser.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardSession.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardLayout.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/QueryCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/WidgetFactory.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/Widget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/UnsupportedWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/ClockWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/LightWidget.cpp
    ${HOMEPANE_TILE_SOURCES}
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/WeatherWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/AttributeText.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DefaultWidgetRegistration.cpp
    # Shared App Code
    ${PROJECT_SOURCE_DIR}/app/fonts.c
    ${PROJECT_SOURCE_DIR}/app/ui_kit.c
    ${PROJECT_SOURCE_DIR}/app/i18n.c
    ${PROJECT_SOURCE_DIR}/app/generated/i18n_strings.c
    ${PROJECT_SOURCE_DIR}/app/config.c
    ${PROJECT_SOURCE_DIR}/app/network.c
    ${PROJECT_SOURCE_DIR}/app/utils.c
    ${PROJECT_SOURCE_DIR}/app/websocket_client.c
    ${PROJECT_SOURCE_DIR}/app/error_handler.c
    # Views
    ${PROJECT_SOURCE_DIR}/app/views/initialization.c
    ${PROJECT_SOURCE_DIR}/app/views/boot_screen.c
    ${PROJECT_SOURCE_DIR}/app/views/network_setup.c
    ${PROJECT_SOURCE_DIR}/app/views/setup_frame.c
    ${PROJECT_SOURCE_DIR}/app/generated/builtin_pictures.c
    ${PROJECT_SOURCE_DIR}/app/views/wifi_picker.c
    ${PROJECT_SOURCE_DIR}/app/views/setup.c
    ${PROJECT_SOURCE_DIR}/app/views/dashboard.c
    ${PROJECT_SOURCE_DIR}/app/views/settings/settings.c
    ${PROJECT_SOURCE_DIR}/app/views/settings/factory_reset.c
    ${PROJECT_SOURCE_DIR}/app/views/settings/voice.c
    ${PROJECT_SOURCE_DIR}/app/views/settings/settings_tab.c
    ${PROJECT_SOURCE_DIR}/app/views/settings/general.c
    ${PROJECT_SOURCE_DIR}/app/views/settings/clock.c
    ${PROJECT_SOURCE_DIR}/app/views/settings/screensaver.c
    ${PROJECT_SOURCE_DIR}/app/views/settings/homeassistant.c
    ${PROJECT_SOURCE_DIR}/app/views/settings/wifi.c
    ${PROJECT_SOURCE_DIR}/app/views/settings/update.c
    ${PROJECT_SOURCE_DIR}/app/views/voice_overlay.c
    ${PROJECT_SOURCE_DIR}/app/views/voice_overlay_b.c
    ${PROJECT_SOURCE_DIR}/app/views/text_fit.c
    # Addons
    ${PROJECT_SOURCE_DIR}/app/addons/addons.c
    ${PROJECT_SOURCE_DIR}/app/addons/musicassistant/addon.c
    ${PROJECT_SOURCE_DIR}/app/addons/musicassistant/settings.c
    ${PROJECT_SOURCE_DIR}/app/addons/doorbell/addon.c
    ${PROJECT_SOURCE_DIR}/app/addons/doorbell/settings.c
    # Haptic
    ${PROJECT_SOURCE_DIR}/app/haptic/haptic.c
    # Components
    ${PROJECT_SOURCE_DIR}/app/components/loading_spinner.c
    # APIs
)

# Define LVGL configuration as a simple include
target_compile_definitions(main PRIVATE LV_CONF_INCLUDE_SIMPLE
    HOMEPANE_DEMO_DIR="${PROJECT_SOURCE_DIR}/platforms/simulator/demo")
target_link_libraries(main
    lvgl
    lvgl::thorvg
    ${SDL2_LIBRARIES}
    websockets
    PkgConfig::LIBMICROHTTPD
    cjson
    CURL::libcurl
    m
    pthread
)


# Only link freertos_config if the FreeRTOS directory exists
if(USE_FREERTOS)
    target_link_libraries(main freertos_config)
endif()

# Custom target to run the executable
add_custom_target(run COMMAND ${EXECUTABLE_OUTPUT_PATH}/main DEPENDS main)

# Conditionally include and link SDL2_image if LV_USE_DRAW_SDL is enabled
if(LV_USE_DRAW_SDL)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
    find_package(SDL2_image REQUIRED)
    target_include_directories(lvgl PUBLIC ${SDL2_IMAGE_INCLUDE_DIRS})
    target_link_libraries(main ${SDL2_IMAGE_LIBRARIES})
endif()

# Conditionally include and link libpng if LV_USE_LIBPNG is enabled
if(LV_USE_LIBPNG)
    find_package(PNG REQUIRED)
    target_include_directories(lvgl PUBLIC ${PNG_INCLUDE_DIRS})
    target_link_libraries(main ${PNG_LIBRARIES})
endif()

# Conditionally include and link libjpeg-turbo if LV_USE_LIBJPEG_TURBO is enabled
if(LV_USE_LIBJPEG_TURBO)
    find_package(JPEG REQUIRED)
    target_include_directories(lvgl PUBLIC ${JPEG_INCLUDE_DIRS})
    target_link_libraries(main ${JPEG_LIBRARIES})
endif()

# Conditionally include and link FFmpeg libraries if LV_USE_FFMPEG is enabled
if(LV_USE_FFMPEG)
    target_link_libraries(main avformat avcodec avutil swscale z)
endif()

# Conditionally include and link FreeType if LV_USE_FREETYPE is enabled
if(LV_USE_FREETYPE)
    find_package(Freetype REQUIRED)
    target_include_directories(lvgl PUBLIC ${FREETYPE_INCLUDE_DIRS})
    target_link_libraries(main ${FREETYPE_LIBRARIES})
endif()

# NOTE: USE_GENERATED_FONTS is a future switch. When enabled, replace app/fonts.c
# with generated font C sources produced by lv_font_conv and update includes.
# Current build keeps runtime tiny_ttf.

if(USE_GENERATED_FONTS)
    message(STATUS "Using generated fonts")

    # Paths
    set(SCRIPTS_DIR ${PROJECT_SOURCE_DIR}/scripts)
    set(GENERATED_DIR ${PROJECT_SOURCE_DIR}/app/generated)
    file(MAKE_DIRECTORY ${GENERATED_DIR})

    # Optional manual override: provide -DLV_FONT_CONV_PATH=/absolute/path/to/lv_font_conv
    set(_FONTS_GEN_ENABLED TRUE)
    set(LV_FONT_CONV_PATH "" CACHE STRING "Manual path to lv_font_conv executable")

    # Find converter: prefer lv_font_conv in PATH, else try npx. If absent, we will NOT fail – we just skip generation and fall back.
    if(LV_FONT_CONV_PATH)
        if(EXISTS ${LV_FONT_CONV_PATH})
            set(LV_FONT_CONV_EXECUTABLE ${LV_FONT_CONV_PATH})
            message(STATUS "Using user-provided lv_font_conv: ${LV_FONT_CONV_EXECUTABLE}")
        else()
            message(WARNING "LV_FONT_CONV_PATH provided but does not exist: ${LV_FONT_CONV_PATH}")
        endif()
    endif()
    find_program(LV_FONT_CONV_EXECUTABLE NAMES lv_font_conv)
    if(NOT LV_FONT_CONV_EXECUTABLE OR LV_FONT_CONV_EXECUTABLE STREQUAL "LV_FONT_CONV_EXECUTABLE-NOTFOUND")
        find_program(NPX_EXECUTABLE NAMES npx)
        if(NPX_EXECUTABLE)
            set(LV_FONT_CONV_EXECUTABLE ${NPX_EXECUTABLE} --yes lv_font_conv)
        else()
            message(WARNING "lv_font_conv not found (and npx unavailable). Will not generate fonts; existing generated .c files (if any) will be used, else runtime fonts. To enable generation: install Node + 'npm i -g lv_font_conv'.")
            set(_FONTS_GEN_ENABLED FALSE)
        endif()
    endif()
    if(_FONTS_GEN_ENABLED)
        message(STATUS "Font generation enabled (lv_font_conv resolved: ${LV_FONT_CONV_EXECUTABLE})")
    else()
        message(STATUS "Font generation disabled (converter unavailable).")
    endif()

    if(_FONTS_GEN_ENABLED)
        # Detect node explicitly so debug/run tasks (non-interactive) still locate it even if nvm isn't sourced
        find_program(NODE_BIN NAMES node)
        if(NODE_BIN)
            get_filename_component(NODE_DIR ${NODE_BIN} DIRECTORY)
            message(STATUS "Found Node: ${NODE_BIN}")
        else()
            file(GLOB NODE_CAND "$ENV{HOME}/.nvm/versions/node/*/bin/node")
            if(NODE_CAND)
                list(SORT NODE_CAND)
                list(REVERSE NODE_CAND)
                list(GET NODE_CAND 0 NODE_BIN)
                get_filename_component(NODE_DIR ${NODE_BIN} DIRECTORY)
                message(STATUS "Using nvm Node fallback: ${NODE_BIN}")
            else()
                message(WARNING "Node executable not found (PATH + nvm); font generation may fail if lv_font_conv requires it.")
                set(NODE_DIR "")
            endif()
        endif()
    endif()

    # Prepare environment prefix for force regeneration target
    if(NODE_DIR)
        set(_ENV_PREFIX ${CMAKE_COMMAND} -E env PATH=${NODE_DIR}:$ENV{PATH})
    else()
        set(_ENV_PREFIX)
    endif()

    # Manual font generation target (no ALL): invoke when you actually need to regenerate fonts.
    # Command: cmake --build . --target generate_fonts
    if(_FONTS_GEN_ENABLED)
        # scripts/gen_fonts.py: Lucide icons (scripts/lucide_icons.txt) and Manrope text
        add_custom_target(generate_fonts
            COMMAND ${_ENV_PREFIX} python3 ${PROJECT_SOURCE_DIR}/scripts/gen_fonts.py
                    --lv-font-conv ${LV_FONT_CONV_EXECUTABLE}
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            COMMENT "Regenerating app/generated fonts"
            VERBATIM
        )
    else()
        add_custom_target(generate_fonts COMMENT "Font conversion tool missing; install Node + lv_font_conv then re-configure to enable generation.")
    endif()

    # Attaches whichever generated font .c files exist as sources of
    # ${_target} (each host test target that renders real widget text
    # needs the same real glyphs `main` uses — a test asserting on
    # lv_label_get_text() doesn't care, but anything that would actually
    # rasterize would silently fall back to tiny_ttf otherwise). Reusable
    # function instead of one inline block per target, since this needs
    # calling once for `main` and once per widget-rendering test target.
    function(attach_generated_fonts _target)
        # Lucide line icons and Manrope text: always generated (scripts/gen_fonts.py), referenced by fonts.c.
        file(GLOB _lucide_fonts ${GENERATED_DIR}/font_lucide_*.c ${GENERATED_DIR}/font_manrope_*.c)
        target_sources(${_target} PRIVATE ${_lucide_fonts})
        target_compile_definitions(${_target} PRIVATE USE_GENERATED_FONTS=1)
    endfunction()

    attach_generated_fonts(main)
endif()

# Apply additional compile options if the build type is Debug
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug mode enabled")

    target_compile_options(lvgl PRIVATE
        -pedantic-errors
        -Wall
        -Wclobbered
        -Wdeprecated
        -Wdouble-promotion
        -Wempty-body
        -Wextra
        -Wformat-security
        -Wmaybe-uninitialized
        # -Wmissing-prototypes
        -Wpointer-arith
        -Wmultichar
        -Wno-pedantic # ignored for now, we convert functions to pointers for properties table.
        -Wreturn-type
        -Wshadow
        -Wshift-negative-value
        -Wsizeof-pointer-memaccess
        -Wtype-limits
        -Wundef
        -Wuninitialized
        -Wunreachable-code
        -Wfloat-conversion
        -Wstrict-aliasing
    )

if (ASAN)
    message(STATUS "AddressSanitizer enabled")

    # Add AddressSanitizer flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
else()
    message(STATUS "AddressSanitizer disabled")
endif()
endif()

# ── Host-only unit tests (C++ migration phase 1) ─────────────
# Deliberately excludes LVGL/SDL2/websockets/vcpkg: only the C++
# core/application skeleton, so it builds and runs on any host.
add_executable(app_core_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_application_smoke.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/Application.cpp
)
target_include_directories(app_core_tests PRIVATE ${PROJECT_SOURCE_DIR})

add_executable(ui_dispatcher_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_ui_dispatcher.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/core/UiDispatcher.cpp
)
target_include_directories(ui_dispatcher_tests PRIVATE ${PROJECT_SOURCE_DIR})

add_executable(websocket_connection_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_websocket_connection.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
)
target_include_directories(websocket_connection_tests PRIVATE ${PROJECT_SOURCE_DIR})

add_executable(ha_protocol_decoder_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_ha_protocol_decoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
)
target_include_directories(ha_protocol_decoder_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(ha_protocol_decoder_tests PRIVATE cjson)

add_executable(ha_client_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_ha_client.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
)
target_include_directories(ha_client_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(ha_client_tests PRIVATE cjson)

# Music Assistant client foundation (docs/ARCHITECTURE.md §14) — no
# widget consumer yet, host tests are the only coverage.
add_executable(ma_protocol_decoder_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_ma_protocol_decoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/music_assistant/MaProtocolDecoder.cpp
)
target_include_directories(ma_protocol_decoder_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(ma_protocol_decoder_tests PRIVATE cjson)

add_executable(ma_client_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_ma_client.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/music_assistant/MaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/music_assistant/MaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
)
target_include_directories(ma_client_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(ma_client_tests PRIVATE cjson)

add_executable(ma_player_store_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_ma_player_store.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/music_assistant/MaPlayerStore.cpp
)
target_include_directories(ma_player_store_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(ma_player_store_tests PRIVATE cjson)

add_executable(ma_controller_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_ma_controller.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/music_assistant/MaController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/music_assistant/MaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/music_assistant/MaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/music_assistant/MaPlayerStore.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
)
target_include_directories(ma_controller_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(ma_controller_tests PRIVATE cjson)

add_executable(image_queue_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_image_queue.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/media/ImageLoader.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/media/ImageDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/media/FileImageCache.cpp
)
target_include_directories(image_queue_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(image_queue_tests PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(image_queue_tests PRIVATE lvgl)

add_executable(entity_store_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_entity_store.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntityStore.cpp
)
target_include_directories(entity_store_tests PRIVATE ${PROJECT_SOURCE_DIR})

add_executable(entity_subscription_coordinator_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_entity_subscription_coordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntityStore.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntitySubscriptionCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
)
target_include_directories(entity_subscription_coordinator_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(entity_subscription_coordinator_tests PRIVATE cjson)

add_executable(query_coordinator_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_query_coordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/QueryCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
)
target_include_directories(query_coordinator_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(query_coordinator_tests PRIVATE cjson)

add_executable(dashboard_parser_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_dashboard_parser.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardParser.cpp
)
target_include_directories(dashboard_parser_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(dashboard_parser_tests PRIVATE cjson)

# Pure get_panels JSON parsing (no LVGL, no network) — see PanelListParser.h.
add_executable(panel_list_parser_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_panel_list_parser.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/PanelListParser.cpp
)
target_include_directories(panel_list_parser_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(panel_list_parser_tests PRIVATE cjson)

# Phase 10 "Setup" consumer: the full real stack (fake socket only), no
# LVGL — Setup never builds a widget tree.
add_executable(setup_controller_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_setup_controller.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/SetupController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/PanelListParser.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
)
target_include_directories(setup_controller_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(setup_controller_tests PRIVATE cjson)

# Phase 10's last consumer: the OAuth device-pairing flow's WebSocket half
# (auth with a short-lived token, request auth/long_lived_access_token).
# Full real stack (fake socket only), no LVGL.
add_executable(pairing_controller_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_pairing_controller.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/PairingController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
)
target_include_directories(pairing_controller_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(pairing_controller_tests PRIVATE cjson)

# WiFi scan/connect flow shared by the setup screen and the settings
# "WiFi" tab — fake adapter, no threads, no LVGL.
add_executable(wifi_controller_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_wifi_controller.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/WifiController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/platform/PortableDefaults.cpp
)
target_include_directories(wifi_controller_tests PRIVATE ${PROJECT_SOURCE_DIR})

# Firmware update flow (Track U U1): manifest parsing, versions,
# UpdateController — fake adapter, no network/flash/LVGL.
add_executable(update_controller_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_update_controller.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/UpdateController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/update/UpdateManifest.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/platform/PortableDefaults.cpp
)
target_include_directories(update_controller_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(update_controller_tests PRIVATE cjson)

# Improv Wi-Fi serial packets (web flasher sends the Wi-Fi credentials).
add_executable(improv_serial_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_improv_serial.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/ImprovSerial.cpp
)
target_include_directories(improv_serial_tests PRIVATE ${PROJECT_SOURCE_DIR})

# Time sync: SNTP packets and HA's HTTP Date header.
add_executable(time_sync_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_time_sync.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/TimeSync.cpp
)
target_include_directories(time_sync_tests PRIVATE ${PROJECT_SOURCE_DIR})

add_executable(mqtt_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_mqtt.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/mqtt/HaDiscovery.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/mqtt/MqttController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/sensors/SimulatedSensors.cpp
)
target_include_directories(mqtt_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(mqtt_tests PRIVATE cjson)

add_executable(voice_controller_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_voice_controller.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/DeviceLink.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/voice/VoiceController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/voice/PipelineEvent.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/voice/CancelPhrases.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/voice/band_levels.c
    ${PROJECT_SOURCE_DIR}/app/cpp/voice/earcons.c
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
)
target_include_directories(voice_controller_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(voice_controller_tests PRIVATE cjson)

add_executable(setting_rules_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_setting_rules.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/settings/SettingRules.cpp
)
target_include_directories(setting_rules_tests PRIVATE ${PROJECT_SOURCE_DIR})

add_executable(voice_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_voice.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/voice/PipelineEvent.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/voice/CancelPhrases.cpp
)
target_include_directories(voice_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(voice_tests PRIVATE cjson)

# Pure grid-geometry math (no cJSON, no LVGL) — see DashboardLayout.h.
add_executable(dashboard_layout_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_dashboard_layout.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardLayout.cpp
)
target_include_directories(dashboard_layout_tests PRIVATE ${PROJECT_SOURCE_DIR})

# The one test target that links real LVGL (headless: no SDL window, no
# display hardware) to prove Widget/Factory/Session actually create and
# tear down real lv_obj_t trees, not just that the C++ compiles.
add_executable(dashboard_session_lvgl_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_dashboard_session_lvgl.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardParser.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardSession.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardLayout.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/QueryCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/WidgetFactory.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntityStore.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntitySubscriptionCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/Widget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/UnsupportedWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/ClockWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/core/DateText.cpp
    ${PROJECT_SOURCE_DIR}/app/fonts.c
    ${PROJECT_SOURCE_DIR}/app/ui_kit.c
    ${PROJECT_SOURCE_DIR}/app/i18n.c
    ${PROJECT_SOURCE_DIR}/app/generated/i18n_strings.c
)
target_include_directories(dashboard_session_lvgl_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(dashboard_session_lvgl_tests PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(dashboard_session_lvgl_tests PRIVATE lvgl lvgl::thorvg cjson)
if(COMMAND attach_generated_fonts)
    attach_generated_fonts(dashboard_session_lvgl_tests)
endif()

# Phase 7: the full real stack (fake socket only) through to real LVGL.
add_executable(sensor_widget_integration_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_sensor_widget_integration.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntityStore.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntitySubscriptionCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaCommandService.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardParser.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardSession.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardLayout.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/QueryCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/WidgetFactory.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DefaultWidgetRegistration.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/Widget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/UnsupportedWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/ClockWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/LightWidget.cpp
    ${HOMEPANE_TILE_SOURCES}
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/WeatherWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/AttributeText.cpp
    ${PROJECT_SOURCE_DIR}/app/fonts.c
    ${PROJECT_SOURCE_DIR}/app/ui_kit.c
    ${PROJECT_SOURCE_DIR}/app/i18n.c
    ${PROJECT_SOURCE_DIR}/app/generated/i18n_strings.c
    ${PROJECT_SOURCE_DIR}/app/config.c
    ${PROJECT_SOURCE_DIR}/app/haptic/haptic.c
)
target_include_directories(sensor_widget_integration_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(sensor_widget_integration_tests PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(sensor_widget_integration_tests PRIVATE lvgl lvgl::thorvg cjson)
if(COMMAND attach_generated_fonts)
    attach_generated_fonts(sensor_widget_integration_tests)
endif()

# Phase 8: first interactive widget (LightWidget) through the same real
# stack, plus simulated taps.
add_executable(light_widget_integration_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_light_widget_integration.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntityStore.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntitySubscriptionCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaCommandService.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardParser.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardSession.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardLayout.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/QueryCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/WidgetFactory.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DefaultWidgetRegistration.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/Widget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/UnsupportedWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/ClockWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/LightWidget.cpp
    ${HOMEPANE_TILE_SOURCES}
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/WeatherWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/AttributeText.cpp
    ${PROJECT_SOURCE_DIR}/app/fonts.c
    ${PROJECT_SOURCE_DIR}/app/ui_kit.c
    ${PROJECT_SOURCE_DIR}/app/i18n.c
    ${PROJECT_SOURCE_DIR}/app/generated/i18n_strings.c
    ${PROJECT_SOURCE_DIR}/app/config.c
    ${PROJECT_SOURCE_DIR}/app/haptic/haptic.c
)
target_include_directories(light_widget_integration_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(light_widget_integration_tests PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(light_widget_integration_tests PRIVATE lvgl lvgl::thorvg cjson)
if(COMMAND attach_generated_fonts)
    attach_generated_fonts(light_widget_integration_tests)
endif()

# Phase 8: second interactive widget (CoverWidget) through the same real
# stack, plus simulated taps.
add_executable(cover_widget_integration_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_cover_widget_integration.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntityStore.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntitySubscriptionCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaCommandService.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardParser.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardSession.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardLayout.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/QueryCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/WidgetFactory.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DefaultWidgetRegistration.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/Widget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/UnsupportedWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/ClockWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/LightWidget.cpp
    ${HOMEPANE_TILE_SOURCES}
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/WeatherWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/AttributeText.cpp
    ${PROJECT_SOURCE_DIR}/app/fonts.c
    ${PROJECT_SOURCE_DIR}/app/ui_kit.c
    ${PROJECT_SOURCE_DIR}/app/i18n.c
    ${PROJECT_SOURCE_DIR}/app/generated/i18n_strings.c
    ${PROJECT_SOURCE_DIR}/app/config.c
    ${PROJECT_SOURCE_DIR}/app/haptic/haptic.c
)
target_include_directories(cover_widget_integration_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(cover_widget_integration_tests PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(cover_widget_integration_tests PRIVATE lvgl lvgl::thorvg cjson)
if(COMMAND attach_generated_fonts)
    attach_generated_fonts(cover_widget_integration_tests)
endif()

# Climate tile (ClimateTile) end to end
# through the same real stack, plus simulated arc drag/release.
add_executable(thermostat_widget_integration_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_thermostat_widget_integration.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntityStore.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntitySubscriptionCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaCommandService.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardParser.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardSession.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardLayout.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/QueryCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/WidgetFactory.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DefaultWidgetRegistration.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/Widget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/UnsupportedWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/ClockWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/LightWidget.cpp
    ${HOMEPANE_TILE_SOURCES}
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/WeatherWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/AttributeText.cpp
    ${PROJECT_SOURCE_DIR}/app/fonts.c
    ${PROJECT_SOURCE_DIR}/app/ui_kit.c
    ${PROJECT_SOURCE_DIR}/app/i18n.c
    ${PROJECT_SOURCE_DIR}/app/generated/i18n_strings.c
    ${PROJECT_SOURCE_DIR}/app/config.c
    ${PROJECT_SOURCE_DIR}/app/haptic/haptic.c
)
target_include_directories(thermostat_widget_integration_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(thermostat_widget_integration_tests PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(thermostat_widget_integration_tests PRIVATE lvgl lvgl::thorvg cjson)
if(COMMAND attach_generated_fonts)
    attach_generated_fonts(thermostat_widget_integration_tests)
endif()

# Phase 9: WeatherWidget (EntityStore + QueryCoordinator's Forecast) and
# StatisticsWidget (QueryCoordinator's Statistics) through the same real
# stack, plus their own composition-root wiring (query_coordinator).
add_executable(weather_widget_integration_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_weather_widget_integration.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntityStore.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntitySubscriptionCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaCommandService.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardParser.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardSession.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardLayout.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/QueryCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/WidgetFactory.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DefaultWidgetRegistration.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/Widget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/UnsupportedWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/ClockWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/LightWidget.cpp
    ${HOMEPANE_TILE_SOURCES}
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/WeatherWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/AttributeText.cpp
    ${PROJECT_SOURCE_DIR}/app/fonts.c
    ${PROJECT_SOURCE_DIR}/app/ui_kit.c
    ${PROJECT_SOURCE_DIR}/app/i18n.c
    ${PROJECT_SOURCE_DIR}/app/generated/i18n_strings.c
    ${PROJECT_SOURCE_DIR}/app/config.c
    ${PROJECT_SOURCE_DIR}/app/haptic/haptic.c
)
target_include_directories(weather_widget_integration_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(weather_widget_integration_tests PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(weather_widget_integration_tests PRIVATE lvgl lvgl::thorvg cjson)
if(COMMAND attach_generated_fonts)
    attach_generated_fonts(weather_widget_integration_tests)
endif()

add_executable(statistics_widget_integration_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_statistics_widget_integration.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntityStore.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntitySubscriptionCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaCommandService.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardParser.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardSession.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardLayout.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/QueryCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/WidgetFactory.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DefaultWidgetRegistration.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/Widget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/UnsupportedWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/ClockWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/LightWidget.cpp
    ${HOMEPANE_TILE_SOURCES}
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/WeatherWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/AttributeText.cpp
    ${PROJECT_SOURCE_DIR}/app/fonts.c
    ${PROJECT_SOURCE_DIR}/app/ui_kit.c
    ${PROJECT_SOURCE_DIR}/app/i18n.c
    ${PROJECT_SOURCE_DIR}/app/generated/i18n_strings.c
    ${PROJECT_SOURCE_DIR}/app/config.c
    ${PROJECT_SOURCE_DIR}/app/haptic/haptic.c
)
target_include_directories(statistics_widget_integration_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(statistics_widget_integration_tests PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(statistics_widget_integration_tests PRIVATE lvgl lvgl::thorvg cjson)
if(COMMAND attach_generated_fonts)
    attach_generated_fonts(statistics_widget_integration_tests)
endif()

# What the design guide tiles show (pure parts of app/cpp/ui/tiles)
add_executable(tiles_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_tiles.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/doorbell/DoorbellController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/eventcalendar/EventFeed.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/Screensaver.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntityStore.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntitySubscriptionCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaCommandService.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardParser.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardSession.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardLayout.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/QueryCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/WidgetFactory.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DefaultWidgetRegistration.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/Widget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/UnsupportedWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/ClockWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/LightWidget.cpp
    ${HOMEPANE_TILE_SOURCES}
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/WeatherWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/AttributeText.cpp
    ${PROJECT_SOURCE_DIR}/app/fonts.c
    ${PROJECT_SOURCE_DIR}/app/ui_kit.c
    ${PROJECT_SOURCE_DIR}/app/i18n.c
    ${PROJECT_SOURCE_DIR}/app/generated/i18n_strings.c
    ${PROJECT_SOURCE_DIR}/app/config.c
    ${PROJECT_SOURCE_DIR}/app/haptic/haptic.c
)
target_include_directories(tiles_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(tiles_tests PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(tiles_tests PRIVATE lvgl lvgl::thorvg cjson)
if(COMMAND attach_generated_fonts)
    attach_generated_fonts(tiles_tests)
endif()

# The live cutover's composition root (DashboardController), through the
# same real stack down to real LVGL, using the real captured dashboard fixture.
add_executable(dashboard_controller_tests
    ${PROJECT_SOURCE_DIR}/tests/host/test_dashboard_controller.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/application/DashboardController.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/net/WebSocketConnection.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaClient.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaProtocolDecoder.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntityStore.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/EntitySubscriptionCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/HaCommandService.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardParser.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardSession.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DashboardLayout.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/home_assistant/QueryCoordinator.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/WidgetFactory.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/dashboard/DefaultWidgetRegistration.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/Widget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/UnsupportedWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/ClockWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/LightWidget.cpp
    ${HOMEPANE_TILE_SOURCES}
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/WeatherWidget.cpp
    ${PROJECT_SOURCE_DIR}/app/cpp/ui/AttributeText.cpp
    ${PROJECT_SOURCE_DIR}/app/fonts.c
    ${PROJECT_SOURCE_DIR}/app/ui_kit.c
    ${PROJECT_SOURCE_DIR}/app/i18n.c
    ${PROJECT_SOURCE_DIR}/app/generated/i18n_strings.c
    ${PROJECT_SOURCE_DIR}/app/config.c
    ${PROJECT_SOURCE_DIR}/app/haptic/haptic.c
)
target_include_directories(dashboard_controller_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(dashboard_controller_tests PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(dashboard_controller_tests PRIVATE lvgl lvgl::thorvg cjson)
if(COMMAND attach_generated_fonts)
    attach_generated_fonts(dashboard_controller_tests)
endif()

enable_testing()
add_test(NAME app_core_tests COMMAND app_core_tests)
add_test(NAME ui_dispatcher_tests COMMAND ui_dispatcher_tests)
add_test(NAME websocket_connection_tests COMMAND websocket_connection_tests)
add_test(NAME ha_protocol_decoder_tests COMMAND ha_protocol_decoder_tests
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME ha_client_tests COMMAND ha_client_tests)
add_test(NAME ma_protocol_decoder_tests COMMAND ma_protocol_decoder_tests)
add_test(NAME ma_client_tests COMMAND ma_client_tests)
add_test(NAME ma_player_store_tests COMMAND ma_player_store_tests WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME ma_controller_tests COMMAND ma_controller_tests WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME image_queue_tests COMMAND image_queue_tests WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME entity_store_tests COMMAND entity_store_tests)
add_test(NAME entity_subscription_coordinator_tests COMMAND entity_subscription_coordinator_tests)
add_test(NAME query_coordinator_tests COMMAND query_coordinator_tests)
add_test(NAME dashboard_parser_tests COMMAND dashboard_parser_tests
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME panel_list_parser_tests COMMAND panel_list_parser_tests)
add_test(NAME setup_controller_tests COMMAND setup_controller_tests)
add_test(NAME pairing_controller_tests COMMAND pairing_controller_tests)
add_test(NAME wifi_controller_tests COMMAND wifi_controller_tests)
add_test(NAME update_controller_tests COMMAND update_controller_tests)
add_test(NAME improv_serial_tests COMMAND improv_serial_tests)
add_test(NAME time_sync_tests COMMAND time_sync_tests)
add_test(NAME voice_tests COMMAND voice_tests)
add_test(NAME setting_rules_tests COMMAND setting_rules_tests)
add_test(NAME voice_controller_tests COMMAND voice_controller_tests)
add_test(NAME mqtt_tests COMMAND mqtt_tests)
# Every icon used in app/ must be in the generated fonts (scripts/gen_fonts.py fixes it).
# UI texts: app/i18n/*.lang consistent (placeholders, keys) and the generated tables current
add_test(NAME i18n_tables_check COMMAND python3 ${PROJECT_SOURCE_DIR}/scripts/gen_i18n.py --check)
add_test(NAME dashboard_layout_tests COMMAND dashboard_layout_tests)
add_test(NAME dashboard_session_lvgl_tests COMMAND dashboard_session_lvgl_tests
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME sensor_widget_integration_tests COMMAND sensor_widget_integration_tests)
add_test(NAME light_widget_integration_tests COMMAND light_widget_integration_tests)
add_test(NAME cover_widget_integration_tests COMMAND cover_widget_integration_tests)
add_test(NAME thermostat_widget_integration_tests COMMAND thermostat_widget_integration_tests)
add_test(NAME weather_widget_integration_tests COMMAND weather_widget_integration_tests)
add_test(NAME statistics_widget_integration_tests COMMAND statistics_widget_integration_tests)
add_test(NAME tiles_tests COMMAND tiles_tests WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(NAME dashboard_controller_tests COMMAND dashboard_controller_tests
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
