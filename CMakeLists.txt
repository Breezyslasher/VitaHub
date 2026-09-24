cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# VitaHub: Vita_plex, Vita_abs, Vita_Suwayomi and Vita-Music-Assistant in one
# app. Each service is a module under modules/<id>/ built as a static library;
# hub/ is the shell (main loop, hub UI, navigation between services).
#
#   cmake -B build -DPLATFORM_PSV=ON       # VitaHub.vpk
#   cmake -B build -DPLATFORM_DESKTOP=ON   # Linux/macOS dev build
#
# Services can be left out with -DVITAHUB_MODULE_<ID>=OFF.
# ---------------------------------------------------------------------------

option(PLATFORM_PSV     "Build for PlayStation Vita"              OFF)
option(PLATFORM_DESKTOP "Build for Desktop (Linux/macOS)"         OFF)

option(VITAHUB_MODULE_PLEX     "Include Plex (Vita_plex)"                          ON)
option(VITAHUB_MODULE_ABS      "Include Audiobookshelf (Vita_abs)"                 ON)
option(VITAHUB_MODULE_SUWAYOMI "Include Suwayomi (Vita_Suwayomi)"                  ON)
option(VITAHUB_MODULE_MUSIC    "Include Music Assistant (Vita-Music-Assistant)"   ON)

foreach(_P SWITCH PS4 ANDROID IOS TVOS)
    if(PLATFORM_${_P})
        message(FATAL_ERROR
            "PLATFORM_${_P}: VitaHub currently builds for PS Vita and desktop only. "
            "The standalone apps still build for ${_P}.")
    endif()
endforeach()
if(PLATFORM_PSV AND PLATFORM_DESKTOP)
    message(FATAL_ERROR "Set only one of PLATFORM_PSV / PLATFORM_DESKTOP.")
elseif(NOT PLATFORM_PSV AND NOT PLATFORM_DESKTOP)
    message(STATUS "No platform selected, defaulting to PLATFORM_DESKTOP=ON")
    set(PLATFORM_DESKTOP ON CACHE BOOL "Build for Desktop" FORCE)
endif()

# ---------------------------------------------------------------------------
# Borealis
# ---------------------------------------------------------------------------
set(BOREALIS_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

foreach(_P DESKTOP SWITCH PSV PS4 IOS TVOS ANDROID)
    set(PLATFORM_${_P} ${PLATFORM_${_P}} CACHE BOOL "" FORCE)
endforeach()

if(PLATFORM_PSV)
    set(BOREALIS_USE_GXM ON  CACHE BOOL "Use GXM renderer for PS Vita" FORCE)
    set(USE_VITA_SHARK  OFF  CACHE BOOL "Use VitaShark for shader compilation" FORCE)
endif()
set(USE_LIBROMFS OFF CACHE BOOL "Use libromfs")

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(VitaHub VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -frtti -fexceptions")

# ---------------------------------------------------------------------------
# Versions: VitaHub's own, plus the upstream version each module was imported
# at (modules/<id>/VERSION, written by tools/import_modules.py).
# ---------------------------------------------------------------------------
if(NOT DEFINED APP_VERSION OR APP_VERSION STREQUAL "")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" APP_VERSION)
    string(STRIP "${APP_VERSION}" APP_VERSION)
endif()
if(NOT DEFINED APP_DISPLAY_VERSION OR APP_DISPLAY_VERSION STREQUAL "")
    set(APP_DISPLAY_VERSION "${APP_VERSION}")
endif()
# The SFO wants "XX.YY".
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" _sfo "${APP_VERSION}")
if(_sfo)
    set(_major "${CMAKE_MATCH_1}")
    set(_minor "${CMAKE_MATCH_2}")
else()
    set(_major 0)
    set(_minor 1)
endif()
if(_major LESS 10)
    set(_major "0${_major}")
endif()
if(_minor LESS 10)
    set(_minor "0${_minor}")
endif()
message(STATUS "VitaHub version: ${APP_VERSION} (${APP_DISPLAY_VERSION})")

if(PLATFORM_PSV)
    set(VITA_APP_NAME "VitaHub")
    set(VITA_TITLEID  "VHUB00001")
    set(VITA_VERSION  "${_major}.${_minor}")
    set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1 -d ATTRIBUTE2=12")
endif()

# ---------------------------------------------------------------------------
# Borealis patches (patches/borealis/, see patches/borealis/README.md). These
# are the Vita_plex set, which is a superset of what the other apps carried.
# ---------------------------------------------------------------------------
function(vitahub_apply_patch patch target)
    if(NOT EXISTS ${patch})
        message(FATAL_ERROR "Patch file not found: ${patch}")
    endif()
    configure_file(${patch} ${target} COPYONLY)
endfunction()

set(_bp ${CMAKE_CURRENT_SOURCE_DIR}/patches/borealis)
if(PLATFORM_PSV)
    vitahub_apply_patch(${_bp}/psv_platform.cpp ${BOREALIS_LIBRARY}/lib/platforms/psv/psv_platform.cpp)
endif()
vitahub_apply_patch(${_bp}/fontstash.h           ${BOREALIS_LIBRARY}/include/borealis/extern/nanovg/fontstash.h)
vitahub_apply_patch(${_bp}/nanovg.c              ${BOREALIS_LIBRARY}/lib/extern/nanovg/nanovg.c)
vitahub_apply_patch(${_bp}/brls_application.hpp  ${BOREALIS_LIBRARY}/include/borealis/core/application.hpp)
vitahub_apply_patch(${_bp}/brls_application.cpp  ${BOREALIS_LIBRARY}/lib/core/application.cpp)
# Vita_abs: adds the "password" hint EditTextDialog shows.
vitahub_apply_patch(${_bp}/hints.json            ${BOREALIS_DIR}/resources/i18n/en-US/hints.json)
if(PLATFORM_DESKTOP)
    # Desktop builds read resources/ from the source tree; borealis' own
    # strings (hint bar labels) live in the submodule, so stage them there.
    configure_file(${BOREALIS_DIR}/resources/i18n/en-US/hints.json
                   ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/en-US/hints.json COPYONLY)
    vitahub_apply_patch(${_bp}/glfw_input.cpp    ${BOREALIS_LIBRARY}/lib/platforms/glfw/glfw_input.cpp)
    vitahub_apply_patch(${_bp}/glfw_platform.cpp ${BOREALIS_LIBRARY}/lib/platforms/glfw/glfw_platform.cpp)
endif()

add_subdirectory(${BOREALIS_LIBRARY})

# ---------------------------------------------------------------------------
# Settings shared by every module library
# ---------------------------------------------------------------------------
if(PLATFORM_PSV)
    set(VITAHUB_RESOURCE_PREFIX "app0:resources/")
else()
    set(VITAHUB_RESOURCE_PREFIX "resources/")
endif()

# Links, include paths and definitions every module needs. Module code is
# compiled exactly as in its standalone repo, plus VITAHUB=1, which gates the
# few hand edits that hook it into the hub (tools/patches/<id>.patch).
function(vitahub_module_target target id)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/hub/include
        ${BOREALIS_LIBRARY}/include
        ${BOREALIS_LIBRARY}/include/borealis/extern
        ${BOREALIS_LIBRARY}/lib/extern/yoga)
    target_compile_definitions(${target} PRIVATE
        VITAHUB=1
        RESOURCE_PREFIX=\"${VITAHUB_RESOURCE_PREFIX}\")
    target_link_libraries(${target} PUBLIC borealis)
    if(PLATFORM_PSV)
        target_compile_definitions(${target} PRIVATE MINIZ_NO_STDIO MINIZ_NO_TIME)
        target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/lib/miniz)
    endif()
    set_property(GLOBAL APPEND PROPERTY VITAHUB_MODULES ${id})
endfunction()

# Version of the upstream app a module was imported from (modules/<id>/VERSION,
# e.g. "Beta 1.3.9"): numeric part for API headers, full string for display.
function(vitahub_module_version id out_numeric out_display)
    set(_v "0.0.0")
    if(EXISTS ${PROJECT_SOURCE_DIR}/modules/${id}/VERSION)
        file(READ ${PROJECT_SOURCE_DIR}/modules/${id}/VERSION _v)
        string(STRIP "${_v}" _v)
    endif()
    string(REGEX MATCH "[0-9]+(\\.[0-9]+)*" _num "${_v}")
    if(NOT _num)
        set(_num "0.0.0")
    endif()
    set(${out_numeric} "${_num}" PARENT_SCOPE)
    set(${out_display} "${_v}" PARENT_SCOPE)
endfunction()

# Optional platform features, probed once for every module.
if(PLATFORM_PSV)
    # SceNotificationUtil download notifications (Vita_plex). Compiled, not
    # linked: a cross toolchain can't produce an executable for a check.
    include(CheckCXXSourceCompiles)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    check_cxx_source_compiles("
#include <psp2/notificationutil.h>
#include <psp2/sysmodule.h>
int main() {
    SceNotificationUtilProgressInitParam   a = {};
    SceNotificationUtilProgressUpdateParam b = {};
    SceNotificationUtilProgressFinishParam c = {};
    b.targetProgress = 0.5f;
    sceNotificationUtilProgressBegin(&a);
    sceNotificationUtilProgressUpdate(&b);
    sceNotificationUtilProgressFinish(&c);
    sceNotificationUtilSendNotification((const SceWChar16*)0);
    return SCE_SYSMODULE_NOTIFICATION_UTIL;
}" VITAPLEX_HAVE_PSV_NOTIFY)
    unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
elseif(PLATFORM_DESKTOP AND NOT APPLE)
    # MPRIS media controls over D-Bus (Vita_plex).
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(DBUS IMPORTED_TARGET dbus-1)
        if(DBUS_FOUND)
            set(VITAPLEX_MPRIS ON)
        endif()
    endif()
endif()

set(VITAHUB_MODULE_TARGETS)
foreach(_m PLEX ABS SUWAYOMI MUSIC)
    string(TOLOWER ${_m} _id)
    if(VITAHUB_MODULE_${_m})
        add_subdirectory(modules/${_id})
        list(APPEND VITAHUB_MODULE_TARGETS vitahub_${_id})
    endif()
endforeach()

# ---------------------------------------------------------------------------
# The hub executable
# ---------------------------------------------------------------------------
set(HUB_SOURCES
    hub/src/main.cpp
    hub/src/hub.cpp
    hub/src/hub_settings.cpp
    hub/src/modules.cpp
    hub/src/theme.cpp
)
if(PLATFORM_PSV)
    # pthread stack floor + stdio lock stubs, shared by every module. miniz is
    # used by the Plex/ABS/Suwayomi update installers (utils/vita_install).
    list(APPEND HUB_SOURCES hub/src/vita_stubs.c lib/miniz/miniz.c)
    set_source_files_properties(lib/miniz/miniz.c PROPERTIES COMPILE_DEFINITIONS "MINIZ_NO_STDIO;MINIZ_NO_TIME")
endif()

add_executable(${PROJECT_NAME} ${HUB_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE
    hub/include
    ${BOREALIS_LIBRARY}/include
    ${BOREALIS_LIBRARY}/include/borealis/extern)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VITAHUB=1
    VITAHUB_VERSION=\"${APP_VERSION}\"
    VITAHUB_DISPLAY_VERSION=\"${APP_DISPLAY_VERSION}\"
    RESOURCE_PREFIX=\"${VITAHUB_RESOURCE_PREFIX}\")
foreach(_m PLEX ABS SUWAYOMI MUSIC)
    if(VITAHUB_MODULE_${_m})
        target_compile_definitions(${PROJECT_NAME} PRIVATE VITAHUB_HAVE_${_m}=1)
    endif()
endforeach()

# Module libraries reference each other's nothing, but they all reference the
# hub (vitahub::...) and the hub references them: one link group.
target_link_libraries(${PROJECT_NAME}
    -Wl,--start-group ${VITAHUB_MODULE_TARGETS} -Wl,--end-group
    borealis
    ${APP_PLATFORM_LIB})

# ---------------------------------------------------------------------------
# Third-party libraries (union of what the four apps link)
# ---------------------------------------------------------------------------
set(APP_FFMPEG_LIBS mpv avfilter avformat avcodec swresample swscale avutil)
if(APPLE)
    set(APP_FFMPEG_LINK_BLOCK ${APP_FFMPEG_LIBS})
else()
    set(APP_FFMPEG_LINK_BLOCK -Wl,--start-group ${APP_FFMPEG_LIBS} -Wl,--end-group)
endif()

target_link_libraries(${PROJECT_NAME}
    curl mbedtls mbedx509 mbedcrypto
    ${APP_FFMPEG_LINK_BLOCK}
    ass freetype fribidi png bz2 webp z zstd harfbuzz mp3lame)

if(PLATFORM_PSV)
    set(VITAHUB_HEAP_MB 172 CACHE STRING "newlib heap size in MB (Vita)")
    target_compile_definitions(${PROJECT_NAME} PRIVATE VITAHUB_HEAP_MB=${VITAHUB_HEAP_MB})
    target_link_options(${PROJECT_NAME} PRIVATE
        "-Wl,--wrap=pthread_create"
        # See Vita_plex: keeps room at the end of segment 0 for vita-elf-create.
        "-Wl,-z,noseparate-code")
    target_link_libraries(${PROJECT_NAME}
        archive zstd c vita2d
        SceNet_stub SceNetCtl_stub SceHttp_stub SceSsl_stub ScePgf_stub SceRtc_stub
        SceShellSvc_stub SceAppMgr_stub SceTouch_stub SceAudiodec_stub SceVideodec_stub
        SceAvcodec_stub SceCodecEngine_stub SceAudio_stub SceVshBridge_stub
        ScePromoterUtil_stub SceSysmodule_stub SceCamera_stub ScePower_stub
        SceLibKernel_stub)
    if(VITAPLEX_HAVE_PSV_NOTIFY)
        target_link_libraries(${PROJECT_NAME} SceNotificationUtil_stub)
    endif()
elseif(PLATFORM_DESKTOP)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
    if(VITAPLEX_MPRIS)
        target_link_libraries(${PROJECT_NAME} PkgConfig::DBUS)
    endif()
endif()

# ---------------------------------------------------------------------------
# Packaging
# ---------------------------------------------------------------------------
if(PLATFORM_PSV)
    vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE NOASLR)

    # Globbed so a new icon or layout can never be silently left out.
    # Vita only needs the PSV button art from images/.
    file(GLOB_RECURSE VITA_PACKED_RESOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/*.png
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/hub/*.png
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/xml/*.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/images/PSV/*.png)
    file(GLOB VITA_ROOT_IMAGES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/images/*.png)
    list(APPEND VITA_PACKED_RESOURCES ${VITA_ROOT_IMAGES})
    set(VITA_RES_FILE_ARGS)
    foreach(res IN LISTS VITA_PACKED_RESOURCES)
        list(APPEND VITA_RES_FILE_ARGS FILE ${res} ${res})
    endforeach()
    file(GLOB BRLS_SYS_IMAGES RELATIVE ${BOREALIS_DIR}/resources ${BOREALIS_DIR}/resources/img/sys/*.png)
    foreach(res IN LISTS BRLS_SYS_IMAGES)
        list(APPEND VITA_RES_FILE_ARGS FILE ${BOREALIS_DIR}/resources/${res} resources/${res})
    endforeach()

    vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
        VERSION ${VITA_VERSION}
        NAME ${VITA_APP_NAME}
        FILE app/platform/psv/sce_sys/icon0.png                      sce_sys/icon0.png
        FILE app/platform/psv/sce_sys/pic0.png                       sce_sys/pic0.png
        FILE app/platform/psv/sce_sys/livearea/contents/bg.png       sce_sys/livearea/contents/bg.png
        FILE app/platform/psv/sce_sys/livearea/contents/startup.png  sce_sys/livearea/contents/startup.png
        FILE app/platform/psv/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
        FILE ${BOREALIS_DIR}/resources/i18n/en-US/hints.json resources/i18n/en-US/hints.json
        FILE resources/font/font.ttf                      resources/font/font.ttf
        FILE resources/font/icon.ttf                      resources/font/icon.ttf
        FILE resources/font/switch_font.ttf               resources/font/switch_font.ttf
        FILE resources/font/switch_icons.ttf              resources/font/switch_icons.ttf
        FILE resources/material/MaterialIcons-Regular.ttf resources/material/MaterialIcons-Regular.ttf
        FILE resources/cacert.pem                         resources/cacert.pem
        ${VITA_RES_FILE_ARGS}
    )
endif()
