# Vita_plex as a VitaHub module. Source list follows Vita_plex's CMakeLists.txt.

set(PLEX_SOURCES
    hub/plex_module.cpp

    src/app/application.cpp
    src/app/plex_client.cpp
    src/app/downloads_manager.cpp
    src/app/music_queue.cpp
    src/app/music_controller.cpp
    src/app/hint_icons.cpp
    src/app/synclounge_client.cpp
    src/app/synclounge_session.cpp

    src/activity/main_activity.cpp
    src/activity/login_activity.cpp
    src/activity/player_activity.cpp

    src/view/home_tab.cpp
    src/view/library_tab.cpp
    src/view/library_section_tab.cpp
    src/view/search_tab.cpp
    src/view/settings_tab.cpp
    src/view/livetv_tab.cpp
    src/view/livetv_actions.cpp
    src/view/media_detail_view.cpp
    src/view/media_item_cell.cpp
    src/view/recycling_grid.cpp
    src/view/video_view.cpp
    src/view/downloads_tab.cpp
    src/view/music_tab.cpp
    src/view/progress_dialog.cpp
    src/view/overlay_dialog.cpp
    src/view/horizontal_scroll_row.cpp
    src/view/long_press_gesture.cpp
    src/view/sidebar_editor.cpp

    src/player/mpv_player.cpp

    src/utils/app_update.cpp
    src/utils/http_client.cpp
    src/utils/http_cache.cpp
    src/utils/https_proxy.cpp
    src/utils/image_loader.cpp
    src/utils/jwt_auth.cpp
    src/utils/update_verify.cpp
    src/utils/now_playing.cpp
    src/utils/pip.cpp

    src/platform/platform_common.cpp
)

# One platform_<name>.cpp, plus each platform's extras, as in Vita_plex.
# Windows (SMTC, toasts, shell), Linux (MPRIS) and the Vita/PS4 notification
# probes are added from the top-level CMakeLists.txt, next to their probes.
if(PLATFORM_PSV)
    list(APPEND PLEX_SOURCES
        src/platform/platform_psv.cpp
        src/utils/vita_install.cpp
        src/utils/shell_integration_psv.cpp)
elseif(PLATFORM_PS4)
    list(APPEND PLEX_SOURCES
        src/platform/platform_ps4.cpp
        src/utils/ps4_install.cpp
        src/utils/shell_integration_ps4.cpp)
elseif(PLATFORM_SWITCH)
    list(APPEND PLEX_SOURCES src/platform/platform_switch.cpp)
elseif(PLATFORM_ANDROID)
    list(APPEND PLEX_SOURCES
        src/platform/platform_android.cpp
        src/platform/android_assets.cpp
        src/platform/android_mpv_surface.cpp
        src/platform/android_media_browser.cpp
        src/utils/shell_integration_android.cpp)
elseif(PLATFORM_IOS OR PLATFORM_TVOS)
    list(APPEND PLEX_SOURCES src/platform/platform_ios.mm)
else()
    list(APPEND PLEX_SOURCES src/platform/platform_desktop.cpp)
endif()

add_library(vitahub_plex STATIC ${PLEX_SOURCES})
vitahub_module_target(vitahub_plex plex)

vitahub_module_version(plex PLEX_VERSION PLEX_DISPLAY_VERSION)
target_compile_definitions(vitahub_plex PRIVATE
    VITA_PLEX_VERSION=\"${PLEX_VERSION}\"
    VITA_PLEX_DISPLAY_VERSION=\"${PLEX_DISPLAY_VERSION}\")

if(PLATFORM_SWITCH AND USE_DEKO3D)
    target_compile_definitions(vitahub_plex PRIVATE VITAPLEX_NRO_DEKO3D)
endif()
