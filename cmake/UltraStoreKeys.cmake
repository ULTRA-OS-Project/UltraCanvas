# cmake/UltraStoreKeys.cmake
# ultrastore_builtin_keys(<out var> [EXTRA <hex key>...])
#
# The public keys a store build trusts, as an initializer list of string
# literals for UltraStoreBuiltin.h: one per *.pub file under
# Apps/UltraStore/keys/ (the first line of 64 hex digits), lower-case,
# sorted, plus the EXTRA keys given (a CI build of the web store adds its
# test fixture's key this way; a release never does). Both the native store
# (Apps/UltraStore) and the web store (Apps/UltraStore/web) read the same
# folder, so they trust the same catalogs.
#
# Author: UltraCanvas Framework / ULTRA OS

include_guard(GLOBAL)

function(ultrastore_builtin_keys out)
    cmake_parse_arguments(UK "" "" "EXTRA" ${ARGN})
    set(keys_dir "${CMAKE_SOURCE_DIR}/Apps/UltraStore/keys")
    file(GLOB key_files CONFIGURE_DEPENDS "${keys_dir}/*.pub")
    list(SORT key_files)
    set(init "")
    foreach(key_file IN LISTS key_files)
        file(STRINGS "${key_file}" key_lines REGEX "^[0-9a-fA-F]+$")
        list(LENGTH key_lines key_count)
        if(key_count EQUAL 0)
            message(FATAL_ERROR "UltraStore: ${key_file} holds no hex key line")
        endif()
        list(GET key_lines 0 key_hex)
        string(TOLOWER "${key_hex}" key_hex)
        string(LENGTH "${key_hex}" key_len)
        if(NOT key_len EQUAL 64)
            message(FATAL_ERROR "UltraStore: ${key_file} is not a 64-digit hex public key")
        endif()
        string(APPEND init "\"${key_hex}\", ")
    endforeach()
    foreach(key_hex IN LISTS UK_EXTRA)
        string(TOLOWER "${key_hex}" key_hex)
        string(LENGTH "${key_hex}" key_len)
        if(NOT key_len EQUAL 64 OR NOT key_hex MATCHES "^[0-9a-f]+$")
            message(FATAL_ERROR "UltraStore: the extra key '${key_hex}' is not a 64-digit hex public key")
        endif()
        string(APPEND init "\"${key_hex}\", ")
    endforeach()
    set(${out} "${init}" PARENT_SCOPE)
endfunction()
