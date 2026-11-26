# Tests unitaires de MOONLIGHT.
#
# Un executable par module, enregistre dans CTest. Le test de fumee
# historique (tools/smoke_test) est conserve tel quel.
# Aucune dependance externe : l'en-tete tests/support/check.hpp suffit.

# En-tetes d'aide aux tests (CHECK, capture de logs, dossier temporaire).
add_library(moonlight_test_support INTERFACE)
target_include_directories(moonlight_test_support INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_link_libraries(moonlight_test_support INTERFACE moonlight::core)

# moonlight_add_test(<nom> <source> [bibliotheques...])
function(moonlight_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE
        moonlight_test_support moonlight_warnings ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Doubles de test des interfaces de platform (systeme de fichiers en memoire).
add_library(moonlight_test_doubles STATIC support/memory_filesystem.cpp)
target_include_directories(moonlight_test_doubles PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_link_libraries(moonlight_test_doubles
    PUBLIC  moonlight::platform
    PRIVATE moonlight_warnings)

moonlight_add_test(moonlight_core_test core/core_test.cpp moonlight::core)
moonlight_add_test(moonlight_logger_test core/logger_test.cpp moonlight::core)
moonlight_add_test(moonlight_platform_test platform/platform_test.cpp
    moonlight::platform moonlight_test_doubles)
moonlight_add_test(moonlight_config_test core/config_test.cpp
    moonlight::platform moonlight_test_doubles)
