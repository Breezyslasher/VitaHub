# Vita_Suwayomi as a VitaHub module. Source list follows Vita_Suwayomi's
# CMakeLists.txt.

set(SUWAYOMI_SOURCES
    hub/suwayomi_module.cpp

    src/app/application.cpp
    src/app/suwayomi_client.cpp
    src/app/downloads_manager.cpp

    src/activity/main_activity.cpp
    src/activity/login_activity.cpp
    src/activity/reader_activity.cpp

    src/view/library_section_tab.cpp
    src/view/options_popover.cpp
    src/view/history_tab.cpp
    src/view/search_tab.cpp
    src/view/settings_tab.cpp
    src/view/media_detail_view.cpp
    src/view/recycling_grid.cpp
    src/view/manga_item_cell.cpp
    src/view/rotatable_box.cpp
    src/view/rotatable_image.cpp
    src/view/rotatable_label.cpp
    src/view/webtoon_scroll_view.cpp
    src/view/downloads_tab.cpp
    src/view/extensions_tab.cpp
    src/view/source_browse_tab.cpp
    src/view/horizontal_scroll_row.cpp
    src/view/tracking_search_view.cpp
    src/view/migrate_search_view.cpp
    src/view/storage_view.cpp
    src/view/long_press_gesture.cpp
    src/view/pinch_gesture.cpp

    src/utils/http_client.cpp
    src/utils/app_update.cpp
    src/utils/update_verify.cpp
    src/utils/image_loader.cpp
    src/utils/library_cache.cpp
    src/utils/perf_overlay.cpp
)

# One platform_<name>.cpp, plus each platform's extras, as upstream.
if(PLATFORM_PSV)
    list(APPEND SUWAYOMI_SOURCES src/platform/platform_vita.cpp src/utils/vita_install.cpp)
elseif(PLATFORM_PS4)
    list(APPEND SUWAYOMI_SOURCES src/platform/platform_ps4.cpp src/utils/ps4_install.cpp)
elseif(PLATFORM_SWITCH)
    list(APPEND SUWAYOMI_SOURCES src/platform/platform_switch.cpp)
elseif(PLATFORM_ANDROID)
    list(APPEND SUWAYOMI_SOURCES src/platform/platform_android.cpp src/platform/android_assets.cpp)
else()
    list(APPEND SUWAYOMI_SOURCES src/platform/platform_desktop.cpp)
endif()

add_library(vitahub_suwayomi STATIC ${SUWAYOMI_SOURCES})
vitahub_module_target(vitahub_suwayomi suwayomi)

vitahub_module_version(suwayomi SUWAYOMI_VERSION SUWAYOMI_DISPLAY_VERSION)
target_compile_definitions(vitahub_suwayomi PRIVATE
    VITA_SUWAYOMI_VERSION=\"${SUWAYOMI_VERSION}\"
    VITA_SUWAYOMI_DISPLAY_VERSION=\"${SUWAYOMI_DISPLAY_VERSION}\")

if(PLATFORM_DESKTOP AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # As upstream: FFmpeg is only used for rare AVIF/HEIF pages, and Linux
    # distros ship different FFmpeg versions, so the Linux build goes without.
    target_compile_definitions(vitahub_suwayomi PRIVATE VITASUWAYOMI_NO_FFMPEG)
endif()
if(PLATFORM_SWITCH AND USE_DEKO3D)
    target_compile_definitions(vitahub_suwayomi PRIVATE VITASUWAYOMI_NRO_DEKO3D)
endif()
