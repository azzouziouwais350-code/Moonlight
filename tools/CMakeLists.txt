# Outils et points d'entree de developpement.

if(MOONLIGHT_BUILD_TESTS)
    add_executable(moonlight_smoke_test smoke_test/main.cpp)
    target_link_libraries(moonlight_smoke_test
        PRIVATE moonlight::engine moonlight_warnings)
    add_test(NAME moonlight_smoke_test COMMAND moonlight_smoke_test)

    # Separation stricte des modules (voir le script pour les regles).
    add_test(NAME moonlight_layering_check
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_layering.cmake)

    # Aucune donnee du jeu original dans le depot (filet de securite).
    add_test(NAME moonlight_no_game_data_check
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_game_data.cmake)
endif()
