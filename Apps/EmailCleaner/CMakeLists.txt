# Apps/EmailCleaner/CMakeLists.txt
# EmailCleaner — loads several mail accounts into an analysis database and maps
# who sends what, when. Builds the headless analysis engine library and, on top
# of it, the GUI executable (sender map, timetable, sender detail).
# Version: 0.2.0
# Author: UltraCanvas Framework / ULTRA OS

# The engine analyses mail UltraMail has fetched and cached, so it needs the
# UltraMail engine (account list + body cache layout) and UltraDatabase.
if(NOT TARGET UltraDatabase)
    message(STATUS "  [−] EmailCleaner engine - SKIPPED (UltraDatabase not built)")
    return()
endif()
if(NOT TARGET UltraMailEngine)
    message(STATUS "  [−] EmailCleaner engine - SKIPPED (UltraMailEngine not built)")
    return()
endif()

set(EMAILCLEANER_ENGINE_SOURCES
    engine/EmailCleanerTypes.cpp
    engine/EmailCleanerText.cpp
    engine/EmailCleanerRules.cpp
    engine/EmailCleanerClassifier.cpp
    engine/EmailCleanerStore.cpp
    engine/EmailCleanerIngest.cpp
    engine/EmailCleanerAnalytics.cpp
    engine/EmailCleanerUnsubscribe.cpp
    engine/EmailCleanerActions.cpp
    engine/EmailCleanerMailBackend.cpp
    engine/EmailCleanerAttachments.cpp
)

add_library(EmailCleanerEngine STATIC ${EMAILCLEANER_ENGINE_SOURCES})
set_target_properties(EmailCleanerEngine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "emailcleaner_engine")
target_compile_features(EmailCleanerEngine PUBLIC cxx_std_20)
target_include_directories(EmailCleanerEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine)
# UltraMailEngine carries UltraDatabase and the UltraNet MIME helpers (directly
# for a static UltraCanvas, through libultracanvas for a shared one) as PUBLIC
# dependencies, and expresses the link order those need — inherit both.
target_link_libraries(EmailCleanerEngine PUBLIC UltraMailEngine)

message(STATUS "  [✓] EmailCleaner engine - ENABLED (target: EmailCleanerEngine)")

# ── EmailCleaner GUI application ─────────────────────────────────────────────
# The UI executable on top of the engine. Needs the UltraCanvas UI library,
# built by add_subdirectory(UltraCanvas) before this directory, so
# ${ULTRACANVAS_LIBRARY} / ${ULTRACANVAS_INCLUDE_DIRS} are already in scope.
if(NOT ULTRACANVAS_LIBRARY)
    message(STATUS "  [−] EmailCleaner GUI app - SKIPPED (UltraCanvas UI library not built)")
    return()
endif()

set(EMAILCLEANER_UI_SOURCES
    ui/EmailCleanerApp.cpp
    ui/EmailCleanerMapView.cpp
    ui/EmailCleanerTimetableView.cpp
    ui/EmailCleanerDetailView.cpp
    ui/EmailCleanerAccountBar.cpp
    ui/EmailCleanerActionsPanel.cpp
    ui/EmailCleanerRulesDialog.cpp
)

add_executable(EmailCleaner main.cpp ${EMAILCLEANER_UI_SOURCES})
# Plugins (the IMAP DSO the account sync loads) resolve symbols back from the
# executable at runtime.
set_target_properties(EmailCleaner PROPERTIES ENABLE_EXPORTS ON)
target_include_directories(EmailCleaner PRIVATE
    ${ULTRACANVAS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui)
# Engine archives before the core: they pull UltraNet_* from the shared
# libultracanvas import lib, and GNU ld resolves left-to-right, so the provider
# must follow the consumers on the link line.
target_link_libraries(EmailCleaner PRIVATE
    EmailCleanerEngine UltraMailEngine ${ULTRACANVAS_LIBRARY})
if(TARGET fmt::fmt)
    target_link_libraries(EmailCleaner PRIVATE fmt::fmt)
endif()
target_compile_features(EmailCleaner PRIVATE cxx_std_20)
message(STATUS "  [✓] EmailCleaner GUI app - ENABLED (target: EmailCleaner)")
