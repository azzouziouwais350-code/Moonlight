# Bibliotheque du moteur MOONLIGHT.
#
# En 0.1.0 seul le module "core" existe reellement. Les autres dossiers
# (graphics, audio, input, ...) ne contiennent pas encore de code ; ils seront
# ajoutes a cette bibliotheque (ou a leurs propres cibles) au moment ou ils
# seront reellement implementes.

find_package(Threads REQUIRED)

# Genere <build>/generated/core/version.hpp a partir de la version du projet.
configure_file(
    core/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/core/version.hpp
    @ONLY)

add_library(moonlight_engine STATIC
    core/engine.cpp
    core/log.cpp)
add_library(moonlight::engine ALIAS moonlight_engine)

target_include_directories(moonlight_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>)

target_compile_features(moonlight_engine PUBLIC cxx_std_20)
target_link_libraries(moonlight_engine
    PUBLIC  Threads::Threads
    PRIVATE moonlight_warnings)
