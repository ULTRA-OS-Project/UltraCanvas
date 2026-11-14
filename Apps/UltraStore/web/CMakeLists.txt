# Apps/UltraStore/web/CMakeLists.txt
# The UltraStore web store (Docs/UltraStore/WebstoreBrief.md): the shop's
# pages as an UltraCanvas application.
#
#   under emcmake   ultrastore-web.wasm, a side module for the UltraCanvas
#                   runtime (cmake/UltraCanvasWasmApp.cmake): this code and
#                   the catalog's reader only, nothing of the framework. And
#                   site/ - what the admin backend's publish lays beside
#                   catalog.json: the module, the loader, the runtime under
#                   runtime/, site.json naming the runtime's version, the
#                   store's mark. Needs ULTRACANVAS_WASM_DYNAMIC (the root
#                   build adds this directory with BUILD_WASM_DEMOS).
#   natively        ultrastore-web, the same pages over a publish folder, to
#                   look at them before the wasm pipeline runs:
#                   ultrastore-web <publish folder> [page]
#
# The keys the module trusts are the store's (Apps/UltraStore/keys/*.pub,
# cmake/UltraStoreKeys.cmake); ULTRASTORE_WEB_TEST_KEYS adds hex keys for a
# test build (the browser test's fixture catalog is signed with one), never
# for a release.
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS

include(${CMAKE_SOURCE_DIR}/cmake/UltraStoreKeys.cmake)

set(ULTRASTORE_WEB_TEST_KEYS "" CACHE STRING
    "Extra hex public keys the web store trusts (a test build only), semicolon-separated")
ultrastore_builtin_keys(ULTRASTORE_BUILTIN_KEYS_INIT EXTRA ${ULTRASTORE_WEB_TEST_KEYS})
set(ULTRASTORE_BUILTIN_SERVERS_INIT "")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../core/UltraStoreBuiltin.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/UltraStoreBuiltin.h" @ONLY)

set(_web_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/WebPlatform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/WebStore.cpp)

if(EMSCRIPTEN)
    include(${CMAKE_SOURCE_DIR}/cmake/UltraCanvasWasmApp.cmake)
    # The catalog's reader and its types come along; the verifier does not
    # (it is UltraCrypt's, which the runtime does not carry): the browser's
    # own WebCrypto checks the signature (WebPlatform.cpp).
    ultracanvas_wasm_app(ultrastore-web ${_web_sources}
        ${CMAKE_CURRENT_SOURCE_DIR}/../core/Catalog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../core/UltraStoreTypes.cpp)
    # With UltraCrypt on libsodium in this build (the sysroot's), the module
    # verifies the way the native store does, through the runtime's
    # UltraCrypt (the runtime carries it whole); WebCrypto stays the
    # fallback, and the only way without it.
    if(TARGET uc-crypt)
        get_target_property(_web_crypt_defs uc-crypt INTERFACE_COMPILE_DEFINITIONS)
        if(_web_crypt_defs MATCHES "ULTRACRYPT_HAVE_SODIUM")
            target_sources(ultrastore-web PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/Verifier.cpp)
            target_compile_definitions(ultrastore-web PRIVATE ULTRASTORE_WEB_ULTRACRYPT=1 ULTRACRYPT_HAVE_SODIUM=1)
            message(STATUS "  [✓] UltraStore web store verifies through UltraCrypt (libsodium), WebCrypto as the fallback")
        else()
            message(STATUS "  [−] UltraStore web store verifies through WebCrypto only (no libsodium in the sysroot)")
        endif()
    endif()
    target_include_directories(ultrastore-web PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../core
        ${CMAKE_CURRENT_SOURCE_DIR}/../ui
        ${CMAKE_SOURCE_DIR}/UltraCanvas          # OS/WASM/UltraCanvasWASMSupport.h
        "${CMAKE_CURRENT_BINARY_DIR}/generated")

    # site/: what publish copies beside the catalog (CatalogPublisher's
    # web.site_dir). The runtime files are the runtime build's (UltraCanvas/
    # OS/WASM/runtime, added by the root build); the loader is the
    # runtime's own, unchanged, so the extension path stays as it is.
    set(_web_site ${CMAKE_CURRENT_BINARY_DIR}/site)
    set(_runtime_dir ${CMAKE_BINARY_DIR}/UltraCanvas/OS/WASM/runtime)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/site.json.in ${CMAKE_CURRENT_BINARY_DIR}/site.json @ONLY)
    # The translation catalogues the store's chrome follows the visitor's
    # language with: the store's own set and the standard set per language,
    # staged at configure, under site/locale/<lang>/. The module fetches the
    # visitor's at start (WebStore.cpp, LoadLocale).
    set(_web_locale ${CMAKE_CURRENT_BINARY_DIR}/locale-stage)
    file(REMOVE_RECURSE ${_web_locale})
    file(GLOB _web_catalogues CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/media/locale/*/UltraStore.json")
    foreach(_web_catalogue IN LISTS _web_catalogues)
        get_filename_component(_web_lang_dir ${_web_catalogue} DIRECTORY)
        get_filename_component(_web_lang ${_web_lang_dir} NAME)
        file(COPY ${_web_catalogue} ${_web_lang_dir}/Standard.json DESTINATION ${_web_locale}/${_web_lang})
    endforeach()
    add_custom_target(ultrastore-web-site ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_web_site}/runtime
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${_web_locale} ${_web_site}/locale
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ultrastore-web> ${_web_site}/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/UltraCanvas/OS/WASM/runtime/site/ultracanvas-loader.js ${_web_site}/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/site.json ${_web_site}/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/media/appicon/UltraStore.png ${_web_site}/ultrastore.png
        COMMAND ${CMAKE_COMMAND} -E copy
            ${_runtime_dir}/ultracanvas-runtime.js
            ${_runtime_dir}/ultracanvas-runtime.wasm
            ${_runtime_dir}/ultracanvas-runtime.data
            ${_web_site}/runtime/
        DEPENDS ultrastore-web ultracanvas-runtime
        COMMENT "UltraStore web store: the site folder")
    message(STATUS "  [✓] UltraStore web store (ultrastore-web.wasm, site/) - ENABLED")
else()
    if(NOT TARGET UltraStoreCore)
        message(STATUS "  [−] UltraStore web store - SKIPPED (UltraStoreCore not built)")
        return()
    endif()
    add_executable(ultrastore-web ${_web_sources})
    target_include_directories(ultrastore-web PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../core
        ${CMAKE_CURRENT_SOURCE_DIR}/../ui
        ${ULTRACANVAS_INCLUDE_DIRS}
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(ultrastore-web PRIVATE UltraStoreCore ${ULTRACANVAS_LIBRARY})
    if(ULTRACANVAS_PLUGIN_TARGETS)
        target_link_libraries(ultrastore-web PRIVATE ${ULTRACANVAS_PLUGIN_TARGETS})
    endif()
    target_compile_features(ultrastore-web PRIVATE cxx_std_20)
    message(STATUS "  [✓] UltraStore web store (ultrastore-web, desktop) - ENABLED")
endif()
