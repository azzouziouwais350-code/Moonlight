# Module platform : interfaces independantes du systeme + implementation hote.
# Depend uniquement de core. Les en-tetes ne doivent contenir aucune API
# systeme (verifie par tools/check_layering.cmake) ; seuls les .cpp d'implementation
# peuvent s'appuyer sur le systeme.

add_library(moonlight_platform STATIC
    file_util.cpp
    host_clock.cpp
    host_filesystem.cpp)
add_library(moonlight::platform ALIAS moonlight_platform)

target_compile_features(moonlight_platform PUBLIC cxx_std_20)
target_link_libraries(moonlight_platform
    PUBLIC  moonlight::core
    PRIVATE moonlight_warnings)
