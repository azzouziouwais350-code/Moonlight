# Module assets : gestion des ressources (chemins virtuels, montages, lecture sure).
# Depend de core et de platform (interfaces uniquement : il ne connait pas
# l'implementation hote). Ne contient aucune donnee du jeu.

add_library(moonlight_assets STATIC
    resource_manager.cpp)
add_library(moonlight::assets ALIAS moonlight_assets)

target_compile_features(moonlight_assets PUBLIC cxx_std_20)
target_link_libraries(moonlight_assets
    PUBLIC  moonlight::platform
    PRIVATE moonlight_warnings)
