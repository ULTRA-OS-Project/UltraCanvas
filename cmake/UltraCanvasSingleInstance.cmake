# cmake/UltraCanvasSingleInstance.cmake
# Reusable CMake function that lets an application run one copy per user
# (include/UltraCanvasSingleInstance.h): a second start brings the running
# copy forward and quits.
#
#   ultracanvas_single_instance(DeviceExplorer)
#
# Compiles core/AppEntry/UltraCanvasSingleInstance.cpp into the executable -
# never into the core library, which does not link UltraMessage - and links
# UltraMessage where the build has it, defining
# ULTRACANVAS_SINGLE_INSTANCE_HAVE_ULTRAMESSAGE so the hand-off is compiled
# in. Without UltraMessage the instance lock still stops a second copy.

set(_ULTRACANVAS_SINGLE_INSTANCE_SOURCE
    "${CMAKE_CURRENT_LIST_DIR}/../UltraCanvas/core/AppEntry/UltraCanvasSingleInstance.cpp")

function(ultracanvas_single_instance TARGET_NAME)
    target_sources(${TARGET_NAME} PRIVATE "${_ULTRACANVAS_SINGLE_INSTANCE_SOURCE}")
    # Generator expressions, so the answer does not depend on whether the
    # module was configured before or after the application.
    target_link_libraries(${TARGET_NAME} PRIVATE
        "$<$<TARGET_EXISTS:UltraMessage>:UltraMessage>")
    target_compile_definitions(${TARGET_NAME} PRIVATE
        "$<$<TARGET_EXISTS:UltraMessage>:ULTRACANVAS_SINGLE_INSTANCE_HAVE_ULTRAMESSAGE=1>")
endfunction()
