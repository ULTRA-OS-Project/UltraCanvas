# OS/WASM/CMakeLists.txt
# WebAssembly platform build configuration
# Version: 1.0.0
# Last Modified: 2025-01-27
# Author: UltraCanvas Framework

# Verify Emscripten is being used
if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "WASM target requires Emscripten. Use 'emcmake cmake ..'")
endif()

message(STATUS "Configuring UltraCanvas for WebAssembly/WASM platform")

# ===== WASM PLATFORM SOURCES =====

set(ULTRACANVAS_WASM_SOURCES
    UltraCanvasWASMApplication.cpp
    UltraCanvasWASMWindow.cpp
    UltraCanvasWASMRenderContext.cpp
    UltraCanvasWASMSupport.cpp
)

set(ULTRACANVAS_WASM_HEADERS
    UltraCanvasWASMApplication.h
    UltraCanvasWASMWindow.h
    UltraCanvasWASMRenderContext.h
    UltraCanvasWASMSupport.h
)

# ===== CREATE WASM LIBRARY =====

add_library(UltraCanvasWASM STATIC
    ${ULTRACANVAS_WASM_SOURCES}
    ${ULTRACANVAS_WASM_HEADERS}
)

# ===== INCLUDE DIRECTORIES =====

target_include_directories(UltraCanvasWASM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${EMSCRIPTEN_ROOT_PATH}/system/include
)

# ===== EMSCRIPTEN COMPILER FLAGS =====

target_compile_options(UltraCanvasWASM PRIVATE
    -sUSE_WEBGL2=1                  # Enable WebGL 2.0 (for future WebGL backend)
    -sALLOW_MEMORY_GROWTH=1         # Allow memory to grow dynamically
    -sMAXIMUM_MEMORY=2GB            # Maximum memory limit
    -sSTACK_SIZE=5MB                # Stack size
    -sINITIAL_MEMORY=64MB           # Initial memory allocation
    -sNO_EXIT_RUNTIME=1             # Don't exit runtime after main()
    -sASSERTIONS=0                  # Disable assertions in release
    -sENVIRONMENT='web'             # Target web browsers only
)

# ===== EMSCRIPTEN LINKER FLAGS =====

target_link_options(UltraCanvasWASM PUBLIC
    # Memory settings
    -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=2GB
    -sSTACK_SIZE=5MB
    -sINITIAL_MEMORY=64MB
    
    # Runtime settings
    -sNO_EXIT_RUNTIME=1
    -sENVIRONMENT='web'
    -sEXPORT_ES6=0
    -sMODULARIZE=0
    
    # Export functions
    -sEXPORTED_FUNCTIONS=['_main','_malloc','_free']
    -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8']
    
    # File system
    -sF ORCE_FILESYSTEM=1           # Include file system support
    -lidbfs.js                      # IndexedDB file system
    
    # Optimization
    -sASSERTIONS=0
    -sTEXTDECODER=2                # Optimized text decoder
    
    # Features
    -sUSE_WEBGL2=1
    -sALLOW_TABLE_GROWTH=1
    -sDYNAMIC_EXECUTION=0
    
    # Debugging (remove for production)
    # -sASSERTIONS=1
    # -sSTACK_OVERFLOW_CHECK=2
    # -sSAFE_HEAP=1
    # -g4
)

# ===== BUILD CONFIGURATIONS =====

# Debug build
if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(UltraCanvasWASM PRIVATE
        -g4                         # Full debug info
        -sASSERTIONS=2             # Maximum assertions
        -sSTACK_OVERFLOW_CHECK=2   # Stack overflow checking
        -sSAFE_HEAP=1              # Heap safety checks
        -sWASM_BIGINT=1            # Support for 64-bit integers
    )
    
    target_link_options(UltraCanvasWASM PUBLIC
        -g4
        -sASSERTIONS=2
        -sSTACK_OVERFLOW_CHECK=2
        -sSAFE_HEAP=1
        --source-map-base ./       # Source map location
    )
    
    message(STATUS "WASM Debug build configuration")
endif()

# Release build
if(CMAKE_BUILD_TYPE MATCHES Release)
    target_compile_options(UltraCanvasWASM PRIVATE
        -O3                        # Maximum optimization
        -flto                      # Link-time optimization
        -sASSERTIONS=0            # No assertions
    )
    
    target_link_options(UltraCanvasWASM PUBLIC
        -O3
        -flto
        --closure 1                # Closure compiler optimization
        -sMINIFY_HTML=1           # Minify generated HTML
        -sAGGRESSIVE_VARIABLE_ELIMINATION=1
    )
    
    message(STATUS "WASM Release build configuration")
endif()

# ===== INSTALL =====

install(TARGETS UltraCanvasWASM
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES ${ULTRACANVAS_WASM_HEADERS}
    DESTINATION include/UltraCanvas/OS/WASM
)

# ===== BUILD INFO =====

message(STATUS "UltraCanvas WASM Platform:")
message(STATUS "  - Canvas 2D rendering backend")
message(STATUS "  - Emscripten ${EMSCRIPTEN_VERSION}")
message(STATUS "  - WebGL 2.0 support enabled")
message(STATUS "  - IndexedDB file system support")
message(STATUS "  - Initial memory: 64MB")
message(STATUS "  - Maximum memory: 2GB")
