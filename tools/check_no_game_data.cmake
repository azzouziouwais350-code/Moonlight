# Garde-fou : aucune donnee du jeu original ne doit etre ajoutee au depot.
#
# Usage : cmake -DSOURCE_DIR=<racine du depot> -P tools/check_no_game_data.cmake
#
# Refuse les extensions de dumps/archives de la Wii courantes et tout fichier
# de plus de 10 Mio (hors dossiers build/ et .git/). Ce controle est un filet de
# securite contre un ajout accidentel ; il ne remplace pas la vigilance.

cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR n'est pas defini")
endif()

set(FORBIDDEN_EXTENSIONS iso wbfs rvz gcm ciso wad dol rel arc szs bnr nfs tpl)
set(MAX_FILE_BYTES 10485760)

file(GLOB_RECURSE all_files LIST_DIRECTORIES false "${SOURCE_DIR}/*")

set(violations "")
set(files_checked 0)
foreach(file IN LISTS all_files)
    if(file MATCHES "/(build|\\.git|\\.cache)/")
        continue()
    endif()
    math(EXPR files_checked "${files_checked} + 1")
    file(RELATIVE_PATH rel "${SOURCE_DIR}" "${file}")

    get_filename_component(extension "${file}" LAST_EXT)
    string(TOLOWER "${extension}" extension)
    string(REGEX REPLACE "^\\." "" extension "${extension}")
    if(extension IN_LIST FORBIDDEN_EXTENSIONS)
        list(APPEND violations "${rel}: extension interdite '.${extension}' (donnee de jeu ?)")
    endif()

    file(SIZE "${file}" size)
    if(size GREATER MAX_FILE_BYTES)
        list(APPEND violations "${rel}: fichier de ${size} octets (> 10 Mio)")
    endif()
endforeach()

if(violations)
    list(JOIN violations "\n  " text)
    message(FATAL_ERROR "Fichiers suspects (donnees du jeu original ?) :\n  ${text}")
endif()

message(STATUS "no_game_data_check : ${files_checked} fichier(s) verifie(s), aucun suspect")
