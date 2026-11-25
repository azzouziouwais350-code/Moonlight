# Moteur MOONLIGHT : une bibliotheque statique par module.
#
# Graphe de dependances (impose aussi par tools/check_layering.cmake) :
#
#     assets ---> platform ---> core
#
# Les autres dossiers (graphics, audio, input, game, localization, saves, mods)
# ne contiennent pas encore de code et n'ont donc pas encore de cible.
#
# Tous les modules s'incluent depuis la racine engine/ : #include "core/log.hpp".

set(MOONLIGHT_ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(core)
add_subdirectory(platform)

# Cible agregat conservee pour compatibilite : le code existant qui lie
# moonlight::engine continue de fonctionner et recoit tous les modules.
add_library(moonlight_engine INTERFACE)
add_library(moonlight::engine ALIAS moonlight_engine)
target_link_libraries(moonlight_engine INTERFACE
    moonlight::core
    moonlight::platform)
