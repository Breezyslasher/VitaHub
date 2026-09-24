# Script mode: give the private (WebRTC) mbedTLS its own symbol namespace.
#
#   cmake -DNM=... -DOBJCOPY=... -DMAP=out.syms -DLIB_DIR=<prefix>/lib -P this
#
# Collects every global symbol defined by libmbedtls/libmbedx509/libmbedcrypto
# in LIB_DIR, writes an objcopy --redefine-syms map (sym -> vhrtc_sym), and
# writes renamed copies libvhrtc_<name>.a. modules/music/CMakeLists.txt applies
# the same map to libdatachannel and libjuice.

set(_libs mbedtls mbedx509 mbedcrypto)
set(_syms "")
foreach(_l IN LISTS _libs)
    execute_process(
        COMMAND ${NM} -g --defined-only --format=posix ${LIB_DIR}/lib${_l}.a
        OUTPUT_VARIABLE _out
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "nm failed on ${LIB_DIR}/lib${_l}.a")
    endif()
    string(REPLACE "\n" ";" _lines "${_out}")
    foreach(_line IN LISTS _lines)
        # "<symbol> <type> [value size]"; archive member headers end in ':'.
        if(_line MATCHES "^([A-Za-z_][A-Za-z0-9_.$]*) [A-Z]")
            list(APPEND _syms ${CMAKE_MATCH_1})
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _syms)
list(LENGTH _syms _count)
if(_count EQUAL 0)
    message(FATAL_ERROR "no symbols found in the private mbedTLS")
endif()

set(_map "")
foreach(_s IN LISTS _syms)
    string(APPEND _map "${_s} vhrtc_${_s}\n")
endforeach()
file(WRITE ${MAP} "${_map}")

foreach(_l IN LISTS _libs)
    execute_process(
        COMMAND ${OBJCOPY} --redefine-syms=${MAP} ${LIB_DIR}/lib${_l}.a ${LIB_DIR}/libvhrtc_${_l}.a
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "objcopy failed on lib${_l}.a")
    endif()
endforeach()
message(STATUS "Renamed ${_count} mbedTLS symbols for the WebRTC stack")
