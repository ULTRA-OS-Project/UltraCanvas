# UltraAI/ui/CMakeLists.txt
# UltraAI_UI: the module's one library that draws - the "Get a local model"
# dialog every application shows the same way - and its one bridge into the
# UltraCanvas core: UltraAI as FileLoader's text recognizer
# (UltraAITextRecognition.h) and speech transcriber (UltraAISpeechTranscription.h). It needs the local runtime
# and the UltraCanvas core, so it is built in-tree only; the dashboard and
# Texter link it by this name.

if(NOT TARGET UltraAI_Runtime)
    message(STATUS "UltraAI: UI library skipped — the local runtime is not in the build.")
    return()
endif()
if(NOT ULTRACANVAS_LIBRARY OR NOT TARGET ${ULTRACANVAS_LIBRARY})
    message(STATUS "UltraAI: UI library skipped — the UltraCanvas core is not in the build.")
    return()
endif()

add_library(UltraAI_UI STATIC
    src/LocalModelDialog.cpp
    # UltraAI as UltraCanvasFileLoader's text recognizer (OCR through a
    # vision provider, the framework's own as the fallback).
    src/TextRecognition.cpp
    # ... and as its speech transcriber (UltraAISpeechTranscription.h).
    src/SpeechTranscription.cpp
)
add_library(UltraAI::UI ALIAS UltraAI_UI)

target_include_directories(UltraAI_UI PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
# UltraAI_Core: the text recognizer walks the provider registry and the
# default route (CreateVisionAnalyzer, ResolveProviderOrder).
target_link_libraries(UltraAI_UI
    PUBLIC UltraAI UltraAI_Runtime UltraAI_Core ${ULTRACANVAS_LIBRARY}
)
target_compile_features(UltraAI_UI PUBLIC cxx_std_20)
# The speech transcriber takes the sound out of a video (and out of a sound
# file the provider does not take) with VideoFX, where it is in the build.
if(TARGET VideoFX)
    target_link_libraries(UltraAI_UI PRIVATE VideoFX::VideoFX)
    target_compile_definitions(UltraAI_UI PRIVATE ULTRAAI_UI_HAS_VIDEOFX=1)
endif()
