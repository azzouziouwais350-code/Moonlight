# Verifie la separation stricte des modules du moteur.
#
# Usage : cmake -DSOURCE_DIR=<racine du depot> -P tools/check_layering.cmake
#
# Regles :
#   1. Dependances autorisees entre modules (toute autre inclusion est refusee) :
#        core     -> core
#        platform -> core, platform
#        assets   -> core, platform, assets
#      Les autres modules (graphics, audio, input, game, localization, saves,
#      mods) n'ont pas de code ; s'ils en recoivent, ils devront etre declares ici.
#   2. Les en-tetes (.hpp) ne doivent inclure AUCUN en-tete specifique a un
#      systeme (<filesystem>, <unistd.h>, <sys/...>, <windows.h>, <android/...>,
#      <jni.h>...) : les interfaces restent independantes de la plateforme.
#   3. Seuls les .cpp de engine/platform peuvent inclure de tels en-tetes.

cmake_minimum_required(VERSION 3.21)  # active les politiques modernes (IN_LIST...)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR n'est pas defini")
endif()

set(ALL_MODULES core platform assets graphics audio input game localization saves mods)
set(ALLOWED_core core)
set(ALLOWED_platform core platform)
set(ALLOWED_assets core platform assets)

set(OS_HEADER_REGEX
    "<(filesystem|unistd\\.h|fcntl\\.h|dirent\\.h|pthread\\.h|windows\\.h|jni\\.h|TargetConditionals\\.h|android/[^>]*|sys/[^>]*)>")

set(violations "")
set(files_checked 0)

foreach(module IN LISTS ALL_MODULES)
    file(GLOB_RECURSE files
        "${SOURCE_DIR}/engine/${module}/*.hpp"
        "${SOURCE_DIR}/engine/${module}/*.cpp")
    foreach(file IN LISTS files)
        math(EXPR files_checked "${files_checked} + 1")
        file(RELATIVE_PATH rel "${SOURCE_DIR}" "${file}")
        file(STRINGS "${file}" include_lines REGEX "^[ \t]*#[ \t]*include")

        foreach(line IN LISTS include_lines)
            # Regle 1 : inclusions "module/fichier" entre modules du projet.
            if(line MATCHES "\"([a-z]+)/[^\"]+\"")
                set(target_module "${CMAKE_MATCH_1}")
                if(target_module IN_LIST ALL_MODULES)
                    if(NOT DEFINED ALLOWED_${module})
                        list(APPEND violations
                            "${rel}: le module '${module}' ne devrait pas contenir de code ni inclure ${line}")
                    elseif(NOT target_module IN_LIST ALLOWED_${module})
                        list(APPEND violations
                            "${rel}: dependance interdite ${module} -> ${target_module} (${line})")
                    endif()
                endif()
            endif()

            # Regles 2 et 3 : en-tetes systeme.
            if(line MATCHES "${OS_HEADER_REGEX}")
                set(is_header FALSE)
                if(file MATCHES "\\.hpp$")
                    set(is_header TRUE)
                endif()
                if(is_header OR NOT module STREQUAL "platform")
                    list(APPEND violations
                        "${rel}: en-tete specifique au systeme hors de platform/*.cpp (${line})")
                endif()
            endif()
        endforeach()
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n  " text)
    message(FATAL_ERROR "Violations de l'architecture en couches :\n  ${text}")
endif()

message(STATUS "layering_check : ${files_checked} fichier(s) verifie(s), aucune violation")
