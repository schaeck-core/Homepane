# Alle C-Dateien in src/ und src/generated/ automatisch laden
file(GLOB SRC_FILES
    #"${CMAKE_CURRENT_SOURCE_DIR}/../../src/*.c"
    #"${CMAKE_CURRENT_SOURCE_DIR}/../../src/views/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../app/generated/*.c"
)

idf_component_register(
    SRCS
        # C++ core skeleton (Phase 1/2/3 of CPP_MIGRATION_PLAN.md)
        "../../../app/cpp/app_facade.cpp"
        "../../../app/cpp/application/Application.cpp"
        "../../../app/cpp/application/AppStateController.cpp"
        "../../../app/cpp/application/DashboardController.cpp"
        "../../../app/cpp/application/SetupController.cpp"
        "../../../app/cpp/application/PairingController.cpp"
        "../../../app/cpp/application/WifiController.cpp"
        "../../../app/cpp/application/UpdateController.cpp"
        "../../../app/cpp/voice/PipelineEvent.cpp"
        "../../../app/cpp/voice/VoiceController.cpp"
        "../../../app/cpp/voice/CancelPhrases.cpp"
        "../../../app/cpp/voice/band_levels.c"
        "../../../app/cpp/voice/earcons.c"
        "../../../app/cpp/settings/SettingRules.cpp"
        "../../../app/cpp/mqtt/HaDiscovery.cpp"
        "../../../app/cpp/home_assistant/DeviceLink.cpp"
        "../../../app/cpp/mqtt/MqttController.cpp"
        "../../../app/cpp/sensors/SimulatedSensors.cpp"
        "EspMqttTransport.cpp"
        "EspVoiceAudio.cpp"
        "WakeWordDetector.cpp"
        "../../../app/cpp/update/UpdateManifest.cpp"
        "../../../app/cpp/application/PanelListParser.cpp"
        "../../../app/cpp/platform/PortableDefaults.cpp"
        "../../../app/cpp/core/UiDispatcher.cpp"
        "../../../app/cpp/net/WebSocketConnection.cpp"
        "../../../app/cpp/net/ImprovSerial.cpp"
        "../../../app/cpp/home_assistant/HaProtocolDecoder.cpp"
        "../../../app/cpp/home_assistant/HaClient.cpp"
        "../../../app/cpp/home_assistant/EntityStore.cpp"
        "../../../app/cpp/home_assistant/EntitySubscriptionCoordinator.cpp"
        "../../../app/cpp/home_assistant/HaCommandService.cpp"
        "../../../app/cpp/music_assistant/MaProtocolDecoder.cpp"
        "../../../app/cpp/music_assistant/MaClient.cpp"
        "../../../app/cpp/music_assistant/MaPlayerStore.cpp"
        "../../../app/cpp/music_assistant/MaController.cpp"
        "../../../app/cpp/ui/music/MusicPlayer.cpp"
        "../../../app/cpp/ui/music/MusicQueuePage.cpp"
        "../../../app/cpp/ui/music/MusicBrowsePage.cpp"
        "../../../app/cpp/ui/music/MusicItemPage.cpp"
        "../../../app/cpp/ui/music/MusicItems.cpp"
        "../../../app/cpp/ui/music/MusicCovers.cpp"
        "../../../app/cpp/ui/music/MusicPullGesture.cpp"
        "../../../app/cpp/ui/music/MusicTrackPopover.cpp"
        "../../../app/cpp/dashboard/DashboardParser.cpp"
        "../../../app/cpp/dashboard/DashboardSession.cpp"
        "../../../app/cpp/dashboard/DashboardLayout.cpp"
        "../../../app/cpp/home_assistant/QueryCoordinator.cpp"
        "../../../app/cpp/dashboard/WidgetFactory.cpp"
        "../../../app/cpp/ui/Widget.cpp"
        "../../../app/cpp/ui/UnsupportedWidget.cpp"
        "../../../app/cpp/ui/ClockWidget.cpp"
        "../../../app/cpp/ui/AttributeText.cpp"
        "../../../app/cpp/ui/LightWidget.cpp"
        "../../../app/cpp/ui/tiles/TileWidget.cpp"
        "../../../app/cpp/ui/tiles/EntityTile.cpp"
        "../../../app/cpp/ui/tiles/CompactTile.cpp"
        "../../../app/cpp/ui/tiles/ValueTile.cpp"
        "../../../app/cpp/ui/tiles/CalendarTile.cpp"
        "../../../app/cpp/ui/tiles/TodoTile.cpp"
        "../../../app/cpp/ui/tiles/CameraTile.cpp"
        "../../../app/cpp/media/ImageLoader.cpp"
        "../../../app/cpp/media/ImageDecoder.cpp"
        "../../../app/cpp/media/FileImageCache.cpp"
        "../../../app/cpp/core/IsoTime.cpp"
        "../../../app/cpp/core/DateText.cpp"
        "../../../app/cpp/doorbell/DoorbellController.cpp"
        "../../../app/cpp/ui/DoorbellOverlay.cpp"
        "../../../app/cpp/ui/Screensaver.cpp"
        "../../../app/cpp/eventcalendar/EventFeed.cpp"
        "../../../app/cpp/ui/tiles/ClimateTile.cpp"
        "../../../app/cpp/ui/tiles/CoverTile.cpp"
        "../../../app/cpp/ui/tiles/MediaTile.cpp"
        "../../../app/cpp/ui/tiles/OptionsTile.cpp"
        "../../../app/cpp/ui/tiles/ChartTile.cpp"
        "../../../app/cpp/ui/details/LightDetail.cpp"
        "../../../app/cpp/ui/details/ClimateDetail.cpp"
        "../../../app/cpp/ui/details/CoverDetail.cpp"
        "../../../app/cpp/ui/details/AlarmDetail.cpp"
        "../../../app/cpp/ui/details/MediaDetail.cpp"
        "../../../app/cpp/ui/details/WeatherDetail.cpp"
        "../../../app/cpp/ui/details/EntityDetail.cpp"
        "../../../app/cpp/ui/WeatherWidget.cpp"
        "../../../app/cpp/dashboard/DefaultWidgetRegistration.cpp"
        "EspWebSocketTransport.cpp"
        "EspWifiAdapter.cpp"
        "EspImprovSerial.cpp"
        "EspUpdateAdapter.cpp"
        "EspHttpFetcher.cpp"
        "PsramNew.cpp"
        "LvMem.c"
        "app_main.c"
        "app_states.c"
        "slave_ota.c"
        "webserver_esp32.c"
        "error_handler_esp_stub.c"
        # Real dashboard view (replaces dashboard_render_stub.c)
        "../../../app/views/dashboard.c"
        "../../../app/config.c"
        "../../../app/network.c"
        "../../../app/fonts.c"
        "../../../app/ui_kit.c"
        "../../../app/i18n.c"
        "../../../app/utils.c"
        "../../../app/views/initialization.c"
        "../../../app/views/network_setup.c"
        "../../../app/views/setup_frame.c"
        "../../../app/views/wifi_picker.c"
        "../../../app/views/setup.c"
        "../../../app/components/loading_spinner.c"
        # Settings subsystem
        "../../../app/views/settings/settings.c"
        "../../../app/views/settings/settings_tab.c"
        "../../../app/views/settings/general.c"
        "../../../app/views/settings/clock.c"
        "../../../app/views/settings/screensaver.c"
        "../../../app/views/settings/homeassistant.c"
        "../../../app/views/settings/wifi.c"
        "../../../app/views/settings/update.c"
        "../../../app/views/voice_overlay.c"
        "../../../app/views/voice_overlay_b.c"
        "../../../app/views/text_fit.c"
        # Addons
        "../../../app/addons/addons.c"
        "../../../app/addons/musicassistant/addon.c"
        "../../../app/addons/musicassistant/settings.c"
        "../../../app/addons/doorbell/addon.c"
        "../../../app/addons/doorbell/settings.c"
        # Haptic
        "../../../app/haptic/haptic.c"
        # Boot logo (embedded SVG)
        # Voice assistant audio (docs/voice/VOICE_ASSISTANT_PLAN.md)
        "audio_hw.c"
        "stream_player.c"
        ${SRC_FILES}

    # Public half of the firmware update signing key (Track U U1); the
    # private key is a CI secret, see CPP_MIGRATION_PLAN.md §0.
    EMBED_TXTFILES "ota_signing_pubkey.pem"
    # microWakeWord model (github.com/esphome/micro-wake-word-models, Apache 2.0)
    EMBED_FILES "wakeword/okay_nabu.tflite" "wakeword/hey_jarvis.tflite"
    INCLUDE_DIRS
        "."
        "../../.."
        "../../../app"
        "../../../app/views/settings"
        "../../../app/addons"

    REQUIRES
            lvgl
            esp_lcd
            esp_timer
            littlefs
            sdmmc
            fatfs
            vfs
            esp32_p4_wifi6_touch_lcd_4b
            esp_http_server
            esp_http_client
            espcoredump
            esp_eth
            esp_event
            esp_netif
            esp_wifi
            lwip
            mbedtls
            json
            nvs_flash
            espressif__esp_wifi_remote
            espressif__esp_hosted
            espressif__esp_websocket_client
            bootloader_support
            esp_app_format
            app_update
            esp_driver_gpio
            esp_driver_ledc
            esp_driver_uart
            esp_driver_i2s
            mqtt
            espressif__esp-tflite-micro
            tflite_microfrontend
            esp_adc

)
# Audio bring-up endpoints (voice plan V0): only when the environment variable
# HOMEPANE_AUDIO_DEBUG is set at configure time (`HOMEPANE_AUDIO_DEBUG=1
# idf.py reconfigure build`). They expose the microphones on the LAN without
# authentication, so CI and release builds never include them.
if(DEFINED ENV{HOMEPANE_AUDIO_DEBUG})
    message(WARNING "HOMEPANE_AUDIO_DEBUG: building the unauthenticated audio debug server (developer build only)")
    target_sources(${COMPONENT_LIB} PRIVATE "audio_debug.c")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HOMEPANE_AUDIO_DEBUG=1)
endif()

# Suppress switch-enum exhaustiveness warning for app/ widget/component files
# that use partial switch-on-enum (they deliberately skip most LVGL event codes).
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-switch
    -Wno-format
    -Wno-misleading-indentation
    -Wno-maybe-uninitialized
)

# PsramNew.cpp only defines operator new/delete, which nothing references by
# name in libmain: pull it in so libstdc++'s versions are not used instead.
# Update source other than the release site (app/cpp/update/UpdateSource.h)
if(DEFINED ENV{HOMEPANE_OTA_MANIFEST_URL})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HOMEPANE_OTA_MANIFEST_URL="$ENV{HOMEPANE_OTA_MANIFEST_URL}")
endif()

target_link_libraries(${COMPONENT_LIB} INTERFACE "-u homepane_psram_new")
# LVGL calls these (LV_USE_CUSTOM_MALLOC): LvMem.c
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_malloc_core")
