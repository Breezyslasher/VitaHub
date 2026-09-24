# Vita_abs as a VitaHub module. Source list follows Vita_abs's CMakeLists.txt.

set(ABS_SOURCES
    hub/abs_module.cpp

    src/app/application.cpp
    src/app/audiobookshelf_client.cpp
    src/app/downloads_manager.cpp
    src/app/hint_icons.cpp

    src/activity/main_activity.cpp
    src/activity/login_activity.cpp
    src/activity/player_activity.cpp

    src/view/home_tab.cpp
    src/view/library_section_tab.cpp
    src/view/search_tab.cpp
    src/view/settings_tab.cpp
    src/view/media_detail_view.cpp
    src/view/media_item_cell.cpp
    src/view/recycling_grid.cpp
    src/view/horizontal_scroll_row.cpp
    src/view/downloads_tab.cpp
    src/view/progress_dialog.cpp
    src/view/podcast_search_tab.cpp

    src/player/mpv_player.cpp

    src/utils/http_client.cpp
    src/utils/image_loader.cpp
    src/utils/audio_utils.cpp
    src/utils/app_update.cpp
    src/utils/update_verify.cpp
)

# One platform_<name>.cpp, plus each platform's extras, as upstream.
if(PLATFORM_PSV)
    list(APPEND ABS_SOURCES src/platform/platform_vita.cpp src/utils/vita_install.cpp)
elseif(PLATFORM_PS4)
    list(APPEND ABS_SOURCES src/platform/platform_ps4.cpp src/utils/ps4_install.cpp)
elseif(PLATFORM_SWITCH)
    list(APPEND ABS_SOURCES src/platform/platform_switch.cpp)
elseif(PLATFORM_ANDROID)
    list(APPEND ABS_SOURCES src/platform/platform_android.cpp src/platform/android_assets.cpp)
else()
    list(APPEND ABS_SOURCES src/platform/platform_desktop.cpp)
endif()

add_library(vitahub_abs STATIC ${ABS_SOURCES})
vitahub_module_target(vitahub_abs abs)

vitahub_module_version(abs ABS_VERSION ABS_DISPLAY_VERSION)
target_compile_definitions(vitahub_abs PRIVATE
    VITAABS_VERSION=\"${ABS_VERSION}\"
    VITAABS_DISPLAY_VERSION=\"${ABS_DISPLAY_VERSION}\")

if(WIN32)
    # As upstream: the Windows build ships mpv without FFmpeg headers.
    target_compile_definitions(vitahub_abs PRIVATE VitaABS_NO_FFMPEG)
endif()
if(PLATFORM_SWITCH AND USE_DEKO3D)
    target_compile_definitions(vitahub_abs PRIVATE VITAABS_NRO_DEKO3D)
endif()
