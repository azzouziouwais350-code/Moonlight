# Moonlight

[![CI](https://github.com/azzouziouwais350-code/Moonlight/actions/workflows/ci.yml/badge.svg)](https://github.com/azzouziouwais350-code/Moonlight/actions/workflows/ci.yml)

**MOONLIGHT** est un projet de réimplémentation indépendante de *Zero: Tsukihami no Kamen* (Project Zero 4, version japonaise Wii, identifiant `R4ZJ01`), destiné à devenir une application mobile Android puis iOS, avec un moteur C++ réimplémenté.

> **Version 0.2.0 : architecture centrale (fondations techniques).**
> Ce dépôt contient la configuration, un gestionnaire de ressources, une couche d'abstraction de plateforme, un logger structuré et un squelette de moteur, avec leurs tests.
> **Le jeu n'est pas jouable et aucun système de jeu n'existe** : ni rendu, ni audio, ni entrées, ni lecture ou décodage des données du jeu. Le projet n'est pas compatible avec le jeu original.

## Aspects légaux

- MOONLIGHT ne contient et ne distribue **aucun fichier du jeu original** ni aucune ressource protégée.
- Le joueur devra fournir **sa propre copie** du jeu (dump personnel). Les extensions de données courantes sont ignorées par `.gitignore`, et un test CTest (`moonlight_no_game_data_check`) refuse les dumps et les gros fichiers pour éviter tout ajout accidentel.
- Projet indépendant, sans lien avec les ayants droit du jeu. [Dusklight](https://github.com/TwilitRealm/dusklight) sert uniquement d'inspiration architecturale ; aucun code n'en est repris.

## Prérequis

| Outil | Version testée |
|---|---|
| Compilateur C++20 (GCC ou Clang) | GCC 13.3 et 14.2, Clang 18.1 |
| CMake | ≥ 3.21 (testé avec 3.28 et 3.31) |
| Ninja | 1.11 et 1.13 |

Sur Ubuntu / Debian (y compris GitHub Codespaces) :

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build
```

## Compiler et tester

Avec les presets fournis (recommandé) :

```bash
cmake --preset debug          # configure avec Ninja dans build/debug
cmake --build --preset debug  # compile
ctest --preset debug          # lance tous les tests
```

Presets disponibles : `debug`, `release`, `debug-sanitize` (AddressSanitizer + UBSan). Les presets activent `MOONLIGHT_WARNINGS_AS_ERRORS`.

Sans preset :

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests enregistrés dans CTest (9) : `moonlight_smoke_test`, `moonlight_core_test`, `moonlight_logger_test`, `moonlight_platform_test`, `moonlight_config_test`, `moonlight_resource_test`, `moonlight_layering_check`, `moonlight_no_game_data_check`, `moonlight_example_compose`. Détail de ce que chacun couvre dans [`docs/architecture.md`](docs/architecture.md#8-tests).

Exécuter directement le test de fumée (hérité de la 0.1.0, inchangé) :

```bash
./build/debug/tools/moonlight_smoke_test
```

```
MOONLIGHT 0.2.0 - smoke test
42 check(s), 0 failure(s)
RESULT: PASS
```

Exemple de composition des modules (configuration → logs → montage des données de l'utilisateur → moteur) :

```bash
./build/debug/tools/moonlight_example_compose [chemin/vers/moonlight.ini]
```

### Options CMake

| Option | Défaut | Effet |
|---|---|---|
| `MOONLIGHT_BUILD_TESTS` | `ON` | Compile les tests, l'exemple et les contrôles, et les enregistre dans CTest |
| `MOONLIGHT_WARNINGS_AS_ERRORS` | `OFF` (`ON` dans les presets) | Les avertissements deviennent des erreurs |

Les builds in-source sont refusés par CMake.

## Configuration

Fichier texte de type INI, strict (une clé inconnue, un doublon ou une valeur invalide est une erreur avec numéro de ligne). Toutes les clés sont optionnelles :

```ini
[log]
level = info          ; trace | debug | info | warning | error
format = text         ; text | json
file =                ; vide = stderr

[paths]
game_data_dir =       ; dossier de VOS données de jeu (vide = non configuré)
mods_dir = mods
saves_dir = saves

[engine]
max_frame_delta_seconds = 0.25
```

Un fichier absent donne les valeurs par défaut ; un fichier présent mais invalide n'est **jamais** remplacé silencieusement par les valeurs par défaut. Voir `engine/core/config.hpp`.

## Intégration continue (CI)

Le workflow [`.github/workflows/ci.yml`](.github/workflows/ci.yml) s'exécute sur **chaque Pull Request** et **chaque push sur `main`** (et manuellement via *workflow_dispatch*). Le badge en tête de ce fichier reflète l'état de la dernière exécution sur `main`.

Toutes les configurations utilisent CMake + Ninja via les presets du dépôt, avec les **avertissements traités comme erreurs**, puis exécutent les tests CTest. Elles tournent sur `ubuntu-24.04` (x86-64).

| Job | Compilateurs | Configurations |
|---|---|---|
| `build-test` | GCC 13, GCC 14, Clang 18 | Debug et Release (6 combinaisons) |
| `sanitize` | GCC 13, Clang 18 | Debug + AddressSanitizer + UBSan (UBSan arrête le test à la première erreur) |

Chaque combinaison est indépendante (`fail-fast: false`) : un échec n'en masque pas un autre.

### Ce que la CI ne valide pas

- **Pas d'ARM64 :** aucun runner ARM64 n'est utilisé ; la compatibilité ARM64 n'est ni testée ni garantie.
- **Pas d'Android ni d'iOS :** la CI ne compile ni projet Gradle, ni NDK, ni JNI, ni Vulkan, ni Metal (ils n'existent pas encore). Un CI vert ne dit **rien** sur Android.
- **Pas de test de jeu :** le jeu n'existe pas ; seuls les modules de base (`core`, `platform`, `assets`) sont testés.
- **Pas de Windows ni de macOS.**
- GCC 14 et Clang 18 sont les versions fournies par l'image `ubuntu-24.04` au moment de l'écriture ; si GitHub change cette image, la matrice devra être ajustée.

Pour reproduire une combinaison localement, utilisez les mêmes commandes que la CI, par exemple :

```bash
CC=clang-18 CXX=clang++-18 cmake --preset release
cmake --build --preset release
ctest --preset release
```

(Les presets écrivent dans `build/<preset>` : supprimez ce dossier entre deux compilateurs différents. Le job Clang + sanitizers demande le paquet `libclang-rt-18-dev`.)

## Structure du dépôt

```
engine/        moteur C++ : une bibliothèque statique par module
  core/        cycle de vie (Engine), logs (Logger), erreurs (Status, Result), configuration
  platform/    interfaces système (IFileSystem, IClock) + implémentation hôte + config_io
  assets/      ResourceManager (chemins virtuels, montages, résolution sûre)
  graphics/ audio/ input/ game/ localization/ saves/ mods/
               pas encore implémentés : un README.md par dossier indique son statut
android/       projet Android (non démarré)
ios/           projet iOS (non démarré)
tests/         tests unitaires et aides de test (harnais CHECK, système de fichiers en mémoire)
tools/         test de fumée, exemple de composition, contrôles (couches, données de jeu)
docs/          documentation (architecture.md)
```

Dépendances entre modules : `assets` → `platform` → `core`, vérifiées automatiquement par `moonlight_layering_check`. Détails, décisions d'architecture et sécurité des chemins dans [`docs/architecture.md`](docs/architecture.md).

## Ce qui est implémenté en 0.2.0

- **Configuration :** structure typée, valeurs par défaut, analyseur INI strict, validation, sérialisation exacte, chargement et sauvegarde sûre (écriture temporaire puis renommage).
- **Resource Manager :** chemins virtuels, dossiers montés avec priorité, refus de la traversée de chemins (`..`, chemins absolus) et des évasions par liens symboliques, échec fermé, lecture bornée.
- **Platform Abstraction Layer :** `IFileSystem` et `IClock` sans aucune API système dans les interfaces ; implémentation hôte testée sous Linux ; doubles de test (système de fichiers en mémoire, horloge manuelle).
- **Logger :** niveaux, messages structurés (clés/valeurs), formats texte et JSON Lines, sorties `stderr`, fichier ou personnalisée.
- Hérité de la 0.1.0 et inchangé : `Engine` (`initialize()`, `update(dt)`, `shutdown()`), `Status`/`ErrorCode`, CI GitHub Actions. Aucune API publique de la 0.1.x n'est retirée (voir « Compatibilité » dans l'architecture).

## Limitations connues (0.2.0)

- **Aucun jeu :** rien du jeu original n'est lu, décodé ni exécuté. Le `ResourceManager` ne fait que localiser et lire des octets dans des dossiers fournis par l'utilisateur ; aucun format du jeu n'est interprété.
- **`Engine` n'utilise pas encore** la configuration ni le gestionnaire de ressources (intégration prévue pour la 0.3.0).
- **Pas de rendu, d'audio, d'entrées, de sauvegardes de jeu, de langues ni de mods :** les dossiers correspondants sont des emplacements documentés, sans code.
- **Android et iOS non démarrés :** aucun projet Gradle, aucun code JNI, aucun backend Vulkan ou Metal. La compatibilité Vulkan reste à vérifier. Les interfaces de `platform` sont conçues pour accueillir ces portages, mais **la lecture des assets d'un APK n'est pas résolue** (voir l'architecture).
- **Plateformes vérifiées :** Ubuntu 24.04 sur x86-64. **Non vérifié :** Linux ARM64, GitHub Codespaces lui-même, Windows, macOS.
- **Sécurité des chemins :** le `ResourceManager` ne protège pas contre une modification **concurrente** du contenu d'un dossier monté entre le contrôle et la lecture (course TOCTOU) ; les dossiers montés doivent être de confiance.
- Le moteur n'est pas thread-safe (seul `Logger` l'est). Pas de cache de ressources, pas de rechargement à chaud de la configuration.
- Aucune compatibilité d'API ou d'ABI n'est promise avant la 1.0.

Liste complète des limitations dans [`docs/architecture.md`](docs/architecture.md#11-limitations-techniques-connues-020).
