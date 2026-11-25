# Moonlight

[![CI](https://github.com/azzouziouwais350-code/Moonlight/actions/workflows/ci.yml/badge.svg)](https://github.com/azzouziouwais350-code/Moonlight/actions/workflows/ci.yml)

**MOONLIGHT** est un projet de réimplémentation indépendante de *Zero: Tsukihami no Kamen* (Project Zero 4, version japonaise Wii, identifiant `R4ZJ01`), destiné à devenir une application mobile Android puis iOS, avec un moteur C++ réimplémenté.

> **Version 0.1.0 : fondation technique uniquement.**
> Ce dépôt contient un squelette de moteur (cycle de vie, logs, gestion d'erreurs) et un test de fumée.
> **Le jeu n'est pas jouable et aucun système de jeu n'existe encore** (ni rendu, ni audio, ni entrées, ni chargement de ressources).

## Aspects légaux

- MOONLIGHT ne contient et ne distribue **aucun fichier du jeu original** ni aucune ressource protégée.
- Le joueur devra fournir **sa propre copie** du jeu (dump personnel). Les extensions de données courantes sont ignorées par `.gitignore` pour éviter tout ajout accidentel.
- Projet indépendant, sans lien avec les ayants droit du jeu. [Dusklight](https://github.com/TwilitRealm/dusklight) sert uniquement d'inspiration architecturale ; aucun code n'en est repris.

## Prérequis

| Outil | Version testée |
|---|---|
| Compilateur C++20 (GCC ou Clang) | GCC 13.3, Clang 18.1 |
| CMake | ≥ 3.21 (testé avec 3.28) |
| Ninja | 1.11 |

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
ctest --preset debug          # lance le test de fumée via CTest
```

Presets disponibles : `debug`, `release`, `debug-sanitize` (AddressSanitizer + UBSan). Les presets activent `MOONLIGHT_WARNINGS_AS_ERRORS`.

Sans preset :

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Exécuter directement le test de fumée :

```bash
./build/debug/tools/moonlight_smoke_test
```

Sortie attendue (les logs vont sur stderr) :

```
MOONLIGHT 0.1.0 - smoke test
42 check(s), 0 failure(s)
RESULT: PASS
```

### Options CMake

| Option | Défaut | Effet |
|---|---|---|
| `MOONLIGHT_BUILD_TESTS` | `ON` | Compile le test de fumée et l'enregistre dans CTest |
| `MOONLIGHT_WARNINGS_AS_ERRORS` | `OFF` (`ON` dans les presets) | Les avertissements deviennent des erreurs |

Les builds in-source sont refusés par CMake.

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
- **Pas de test de jeu :** seul le squelette du moteur (`engine/core`) est testé, via le test de fumée.
- **Pas de Windows ni de macOS.**
- GCC 14 et Clang 18 sont les versions fournies par l'image `ubuntu-24.04` au moment de l'écriture ; si GitHub change cette image, la matrice devra être ajustée.

Pour reproduire une combinaison localement, utilisez les mêmes commandes que la CI, par exemple :

```bash
CC=clang-18 CXX=clang++-18 cmake --preset release
cmake --build --preset release
ctest --preset release
```

(Les presets écrivent dans `build/<preset>` : supprimez ce dossier entre deux compilateurs différents.)

## Structure du dépôt

```
engine/        moteur C++ (seul engine/core contient du code en 0.1.0)
  core/        cycle de vie (Engine), logs (Logger), erreurs (Status), version
  graphics/ audio/ input/ platform/ game/ assets/ localization/ saves/ mods/
               pas encore implémentés : un README.md par dossier indique son statut
android/       projet Android (non démarré)
ios/           projet iOS (non démarré)
tools/         outils de développement ; contient le test de fumée
docs/          documentation (architecture.md)
```

Détails dans [`docs/architecture.md`](docs/architecture.md).

## Ce qui est implémenté en 0.1.0

- Configuration CMake (C++20, Ninja, presets, avertissements stricts, garde-fou in-source).
- `Engine` : `initialize()`, `update(dt)`, `shutdown()` avec validation des transitions d'état et des arguments.
- `Logger` : niveaux, catégories, formatage `printf` vérifié par le compilateur, thread-safe, destination remplaçable (sink).
- `Status` / `ErrorCode` : erreurs explicites, sans exceptions.
- Test de fumée (42 vérifications) enregistré dans CTest.

## Limitations connues (0.1.0)

- **Aucun jeu :** rien du jeu original n'est lu, décodé ni exécuté. Aucune ressource n'est chargée.
- **Pas de rendu, d'audio, d'entrées, de sauvegardes, de langues ni de mods :** les dossiers correspondants sont des emplacements documentés, sans code.
- **Android et iOS non démarrés :** aucun projet Gradle, aucun code JNI, aucun backend Vulkan ou Metal. La compatibilité Vulkan reste à vérifier.
- **Plateformes vérifiées :** Ubuntu 24.04 sur x86-64 (GCC 13/14 et Clang 18, Debug, Release, ASan/UBSan), en local et par la CI (voir plus haut). **Non vérifié** : Linux ARM64, et GitHub Codespaces lui-même (même famille d'OS, mais l'exécution n'y a pas été testée).
- **La CI ne couvre que x86-64 et le moteur de base** (voir « Ce que la CI ne valide pas »).
- Le moteur n'est pas thread-safe (seul `Logger` l'est).
- Aucune compatibilité d'API ou d'ABI n'est promise avant la 1.0.
