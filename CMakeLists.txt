cmake_minimum_required(VERSION 3.21)

project(MOONLIGHT
    VERSION 0.1.0
    DESCRIPTION "Independent engine reimplementation project (C++20) - foundation release"
    LANGUAGES CXX)

# --- Garde-fous ---------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "Les builds in-source sont interdits. Utilisez par exemple : "
        "cmake --preset debug  (ou cmake -S . -B build -G Ninja)")
endif()

# --- Options ------------------------------------------------------------------
option(MOONLIGHT_BUILD_TESTS "Compile le test de fumee et l'enregistre dans CTest" ON)
option(MOONLIGHT_WARNINGS_AS_ERRORS "Traite les avertissements du compilateur comme des erreurs" OFF)

# --- Configuration C++ globale -------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Type de build" FORCE)
endif()

# Cible d'interface qui porte les avertissements ; chaque module la lie en PRIVATE.
add_library(moonlight_warnings INTERFACE)
if(MSVC)
    target_compile_options(moonlight_warnings INTERFACE /W4)
    if(MOONLIGHT_WARNINGS_AS_ERRORS)
        target_compile_options(moonlight_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(moonlight_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    if(MOONLIGHT_WARNINGS_AS_ERRORS)
        target_compile_options(moonlight_warnings INTERFACE -Werror)
    endif()
endif()

# --- Sous-projets -------------------------------------------------------------
if(MOONLIGHT_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(engine)
add_subdirectory(tools)
if(MOONLIGHT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
