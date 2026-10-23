# UltraAI/ui/CMakeLists.txt
# UltraAI_UI: the module's one library that draws - the "Get a local model"
# dialog every application shows the same way. It needs the local runtime
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
)
add_library(UltraAI::UI ALIAS UltraAI_UI)

target_include_directories(UltraAI_UI PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(UltraAI_UI
    PUBLIC UltraAI UltraAI_Runtime ${ULTRACANVAS_LIBRARY}
)
target_compile_features(UltraAI_UI PUBLIC cxx_std_20)
