# Module core : cycle de vie du moteur, logs, erreurs.
# Ne depend d'aucun autre module du projet ni d'aucune API systeme.

find_package(Threads REQUIRED)

# Genere <build>/engine/core/generated/core/version.hpp a partir de la version du projet.
configure_file(
    version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/core/version.hpp
    @ONLY)

add_library(moonlight_core STATIC
    engine.cpp
    log.cpp)
add_library(moonlight::core ALIAS moonlight_core)

target_include_directories(moonlight_core PUBLIC
    $<BUILD_INTERFACE:${MOONLIGHT_ENGINE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>)

target_compile_features(moonlight_core PUBLIC cxx_std_20)
target_link_libraries(moonlight_core
    PUBLIC  Threads::Threads
    PRIVATE moonlight_warnings)
