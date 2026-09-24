# Vita-Music-Assistant as a VitaHub module. Source list and the
# libdatachannel setup follow Vita-Music-Assistant's CMakeLists.txt.

set(PATCHES ${PROJECT_SOURCE_DIR}/patches/deps)
set(RTC_DIR ${PROJECT_SOURCE_DIR}/lib/libdatachannel)
set(VITA_RTC_COMPAT ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/vita/compat)

# ---------------------------------------------------------------------------
# Private mbedTLS for WebRTC (Vita)
#
# Music Assistant's remote access needs DTLS-SRTP, and the upstream app
# rebuilds the whole SDK mbedTLS with MBEDTLS_SSL_DTLS_SRTP (plus threading).
# VitaHub cannot do that: the prebuilt FFmpeg that Plex/ABS/Suwayomi play
# through embeds mbedtls_ssl_context / mbedtls_ssl_config in its own structs,
# and DTLS-SRTP adds fields in the middle of both, so one shared mbedTLS
# would corrupt FFmpeg's TLS state.
#
# Instead, libdatachannel gets its own mbedTLS, built with the upstream app's
# recipe, and every symbol it defines is renamed (mbedtls_x -> vhrtc_mbedtls_x)
# in that copy and in libdatachannel/libjuice. The two copies then cannot see
# each other: FFmpeg and curl keep the SDK's mbedTLS, WebRTC uses its own.
# ---------------------------------------------------------------------------
if(PLATFORM_PSV)
    include(ExternalProject)
    set(RTC_MBEDTLS_PREFIX ${CMAKE_BINARY_DIR}/rtc-mbedtls)
    set(RTC_MBEDTLS_RAW
        ${RTC_MBEDTLS_PREFIX}/lib/libmbedtls.a
        ${RTC_MBEDTLS_PREFIX}/lib/libmbedx509.a
        ${RTC_MBEDTLS_PREFIX}/lib/libmbedcrypto.a)
    ExternalProject_Add(rtc_mbedtls
        URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
        URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PATCH_COMMAND patch -p1 -i ${PATCHES}/mbedtls_vitasdk.patch
            COMMAND python3 scripts/config.py set MBEDTLS_THREADING_C
            COMMAND python3 scripts/config.py set MBEDTLS_THREADING_PTHREAD
            COMMAND python3 scripts/config.py set MBEDTLS_SSL_DTLS_SRTP
        CMAKE_ARGS
            -DCMAKE_TOOLCHAIN_FILE=$ENV{VITASDK}/share/vita.toolchain.cmake
            -DCMAKE_INSTALL_PREFIX=${RTC_MBEDTLS_PREFIX}
            -DCMAKE_BUILD_TYPE=Release
            -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DMBEDTLS_FATAL_WARNINGS=OFF
        BUILD_BYPRODUCTS ${RTC_MBEDTLS_RAW})

    # Rename every global symbol the private mbedTLS defines.
    set(RTC_SYMS ${CMAKE_BINARY_DIR}/rtc-mbedtls/redefine.syms)
    set(RTC_MBEDTLS_LIBS
        ${RTC_MBEDTLS_PREFIX}/lib/libvhrtc_mbedtls.a
        ${RTC_MBEDTLS_PREFIX}/lib/libvhrtc_mbedx509.a
        ${RTC_MBEDTLS_PREFIX}/lib/libvhrtc_mbedcrypto.a)
    add_custom_command(
        OUTPUT ${RTC_SYMS} ${RTC_MBEDTLS_LIBS}
        COMMAND ${CMAKE_COMMAND}
            -DNM=${CMAKE_NM} -DOBJCOPY=${CMAKE_OBJCOPY} -DMAP=${RTC_SYMS}
            -DLIB_DIR=${RTC_MBEDTLS_PREFIX}/lib
            -P ${PROJECT_SOURCE_DIR}/cmake/rtc_mbedtls_rename.cmake
        DEPENDS rtc_mbedtls ${PROJECT_SOURCE_DIR}/cmake/rtc_mbedtls_rename.cmake
        COMMENT "Isolating the WebRTC mbedTLS (vhrtc_ symbol prefix)")
    add_custom_target(rtc_mbedtls_renamed DEPENDS ${RTC_SYMS} ${RTC_MBEDTLS_LIBS})

    # libdatachannel skips find_package(MbedTLS) when this target exists. It
    # only carries the private headers; the renamed libraries are linked to
    # the module below.
    file(MAKE_DIRECTORY ${RTC_MBEDTLS_PREFIX}/include)
    add_library(MbedTLS::MbedTLS INTERFACE IMPORTED GLOBAL)
    set_target_properties(MbedTLS::MbedTLS PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${RTC_MBEDTLS_PREFIX}/include)
endif()

# ---------------------------------------------------------------------------
# libdatachannel: WebRTC data channels (ICE via libjuice, SCTP via usrsctp)
# ---------------------------------------------------------------------------
set(NO_MEDIA ON CACHE BOOL "" FORCE)
set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
if(PLATFORM_PSV)
    set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
endif()
set(sctp_inet OFF CACHE BOOL "" FORCE)
set(sctp_inet6 OFF CACHE BOOL "" FORCE)
set(sctp_build_programs OFF CACHE BOOL "" FORCE)
set(sctp_build_shared_lib OFF CACHE BOOL "" FORCE)
set(sctp_werror OFF CACHE BOOL "" FORCE)

if(PLATFORM_PSV)
    # Upstream patches, copied into the submodule (see each file's header).
    vitahub_apply_patch(${PATCHES}/usrsctp_usrsctp_h.h          ${RTC_DIR}/deps/usrsctp/usrsctplib/usrsctp.h)
    vitahub_apply_patch(${PATCHES}/usrsctp_user_environment.c   ${RTC_DIR}/deps/usrsctp/usrsctplib/user_environment.c)
    vitahub_apply_patch(${PATCHES}/libdatachannel_dtlstransport.cpp ${RTC_DIR}/src/impl/dtlstransport.cpp)
    vitahub_apply_patch(${PATCHES}/libdatachannel_certificate.cpp   ${RTC_DIR}/src/impl/certificate.cpp)
    vitahub_apply_patch(${PATCHES}/libdatachannel_sctptransport.cpp ${RTC_DIR}/src/impl/sctptransport.cpp)
    vitahub_apply_patch(${PATCHES}/libjuice_random.c     ${RTC_DIR}/deps/libjuice/src/random.c)
    vitahub_apply_patch(${PATCHES}/libjuice_udp.c        ${RTC_DIR}/deps/libjuice/src/udp.c)
    vitahub_apply_patch(${PATCHES}/libjuice_conn_poll.c  ${RTC_DIR}/deps/libjuice/src/conn_poll.c)
    vitahub_apply_patch(${PATCHES}/libjuice_conn_mux.c   ${RTC_DIR}/deps/libjuice/src/conn_mux.c)
    vitahub_apply_patch(${PATCHES}/libjuice_conn_thread.c ${RTC_DIR}/deps/libjuice/src/conn_thread.c)
endif()

add_subdirectory(${RTC_DIR} ${CMAKE_BINARY_DIR}/libdatachannel EXCLUDE_FROM_ALL)

if(PLATFORM_PSV)
    add_dependencies(datachannel-static rtc_mbedtls)
    if(TARGET juice-static)
        target_include_directories(juice-static BEFORE PRIVATE ${VITA_RTC_COMPAT})
        target_compile_options(juice-static PRIVATE -include ${VITA_RTC_COMPAT}/vita_juice_compat.h)
        target_compile_definitions(juice-static PRIVATE ICE_MAX_CANDIDATES_COUNT=128)
    endif()
    if(TARGET usrsctp)
        target_compile_options(usrsctp PRIVATE -include ${VITA_RTC_COMPAT}/vita_bsd_types.h)
        target_include_directories(usrsctp BEFORE PRIVATE ${VITA_RTC_COMPAT})
        target_compile_definitions(usrsctp PRIVATE HAVE_SCONN_LEN)
    endif()
    target_include_directories(datachannel-static BEFORE PRIVATE ${VITA_RTC_COMPAT})
    target_compile_options(datachannel-static PRIVATE -include ${VITA_RTC_COMPAT}/vita_datachannel_compat.h)
    target_compile_definitions(datachannel-static PRIVATE HAVE_SCONN_LEN)

    # vitasdk links libpthread whole-archive already; the deps' -pthread would
    # define every pthread symbol twice. Vita homebrew is also never PIC.
    foreach(_t IN ITEMS datachannel-static juice-static usrsctp)
        if(TARGET ${_t})
            foreach(_prop IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
                get_target_property(_libs ${_t} ${_prop})
                if(_libs)
                    list(REMOVE_ITEM _libs Threads::Threads -pthread pthread)
                    string(REPLACE "$<LINK_ONLY:Threads::Threads>" "" _libs "${_libs}")
                    set_target_properties(${_t} PROPERTIES ${_prop} "${_libs}")
                endif()
            endforeach()
            set_target_properties(${_t} PROPERTIES POSITION_INDEPENDENT_CODE OFF)
        endif()
    endforeach()

    # Renamed copies of libdatachannel and libjuice that reference the
    # vhrtc_ mbedTLS symbols; the module links these instead of the targets.
    set(RTC_RENAMED_DIR ${CMAKE_BINARY_DIR}/rtc-renamed)
    set(RTC_DC_LIB    ${RTC_RENAMED_DIR}/libvhrtc_datachannel.a)
    set(RTC_JUICE_LIB ${RTC_RENAMED_DIR}/libvhrtc_juice.a)
    add_custom_command(
        OUTPUT ${RTC_DC_LIB} ${RTC_JUICE_LIB}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RTC_RENAMED_DIR}
        COMMAND ${CMAKE_OBJCOPY} --redefine-syms=${RTC_SYMS} $<TARGET_FILE:datachannel-static> ${RTC_DC_LIB}
        COMMAND ${CMAKE_OBJCOPY} --redefine-syms=${RTC_SYMS} $<TARGET_FILE:juice-static> ${RTC_JUICE_LIB}
        DEPENDS datachannel-static juice-static ${RTC_SYMS}
        COMMENT "Pointing libdatachannel/libjuice at the WebRTC mbedTLS")
    add_custom_target(rtc_renamed DEPENDS ${RTC_DC_LIB} ${RTC_JUICE_LIB})
endif()

# ---------------------------------------------------------------------------
# The module
# ---------------------------------------------------------------------------
set(MUSIC_SOURCES
    hub/music_module.cpp

    src/app/application.cpp
    src/app/ma_client.cpp
    src/app/sendspin_client.cpp
    src/app/websocket_client.cpp
    src/app/webrtc_client.cpp
    src/app/music_queue.cpp

    src/activity/main_activity.cpp
    src/activity/login_activity.cpp
    src/activity/player_activity.cpp
    src/activity/qr_scanner_activity.cpp

    src/view/home_tab.cpp
    src/view/library_tab.cpp
    src/view/search_tab.cpp
    src/view/settings_tab.cpp
    src/view/debug_tab.cpp
    src/view/media_detail_view.cpp
    src/view/media_item_cell.cpp
    src/view/recycling_grid.cpp
    src/view/progress_dialog.cpp
    src/view/overlay_dialog.cpp
    src/view/horizontal_scroll_row.cpp

    src/player/native_audio_player.cpp

    src/utils/http_client.cpp
    src/utils/image_loader.cpp

    # quirc QR decoder (Remote ID scanning)
    ${PROJECT_SOURCE_DIR}/lib/quirc/quirc.c
    ${PROJECT_SOURCE_DIR}/lib/quirc/decode.c
    ${PROJECT_SOURCE_DIR}/lib/quirc/identify.c
    ${PROJECT_SOURCE_DIR}/lib/quirc/version_db.c
)

add_library(vitahub_music STATIC ${MUSIC_SOURCES})
vitahub_module_target(vitahub_music music)
target_include_directories(vitahub_music PRIVATE
    ${PROJECT_SOURCE_DIR}/lib/quirc
    ${PROJECT_SOURCE_DIR}/lib/dr_libs)
if(PLATFORM_PSV)
    target_include_directories(vitahub_music PRIVATE
        $<TARGET_PROPERTY:datachannel-static,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(vitahub_music PRIVATE
        $<TARGET_PROPERTY:datachannel-static,INTERFACE_COMPILE_DEFINITIONS>)
    # getifaddrs/ioctl shims for libjuice and libdatachannel: a library of
    # its own so it lands after them on the link line.
    add_library(vitahub_music_compat STATIC src/platform/vita/compat/vita_ifaddrs.c)
    target_include_directories(vitahub_music_compat PRIVATE ${VITA_RTC_COMPAT})
    target_link_libraries(vitahub_music PUBLIC
        ${RTC_DC_LIB} ${RTC_JUICE_LIB} usrsctp ${RTC_MBEDTLS_LIBS} vitahub_music_compat)
    add_dependencies(vitahub_music rtc_renamed rtc_mbedtls_renamed)
else()
    target_link_libraries(vitahub_music PUBLIC datachannel-static)
endif()
