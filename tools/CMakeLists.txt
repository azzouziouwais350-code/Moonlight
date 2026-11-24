# Outils et points d'entree de developpement.

if(MOONLIGHT_BUILD_TESTS)
    add_executable(moonlight_smoke_test smoke_test/main.cpp)
    target_link_libraries(moonlight_smoke_test
        PRIVATE moonlight::engine moonlight_warnings)
    add_test(NAME moonlight_smoke_test COMMAND moonlight_smoke_test)
endif()
