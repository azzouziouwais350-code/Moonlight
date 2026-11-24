# Architecture de MOONLIGHT (version 0.1.0)

Ce document décrit ce qui **existe réellement** en 0.1.0 et les principes qui guideront la suite. Les parties marquées *prévu* ne sont pas implémentées.

## 1. Principes

1. **Moteur indépendant de la plateforme.** Le code du moteur (`engine/`) ne dépend ni d'Android, ni d'iOS, ni d'une API graphique précise. Les spécificités de plateforme vivent dans `engine/platform/` (*prévu*) et dans `android/` / `ios/`.
2. **Dépendances dirigées vers `core`.** `core` ne dépend de rien d'autre dans le projet. Les autres modules peuvent dépendre de `core`, jamais l'inverse. `game` est au sommet et peut dépendre de tous les autres.
3. **Pas de système fictif.** Un module n'obtient du code que lorsqu'il est réellement implémenté et testé. Les dossiers vides contiennent un `README.md` qui indique leur statut.
4. **Erreurs explicites.** Les échecs prévisibles sont retournés par valeur (`Status`), pas par exception.
5. **Peu de dépendances.** En 0.1.0, seule la bibliothèque standard C++20 (et les threads du système) est utilisée.
6. **Aucun contenu propriétaire.** Le jeu original est fourni par le joueur et n'est jamais versionné.

## 2. Modules

| Dossier | Rôle prévu | Statut 0.1.0 |
|---|---|---|
| `engine/core` | cycle de vie, logs, erreurs, version | **implémenté** |
| `engine/graphics` | abstraction de rendu ; Vulkan (Android), Metal (iOS) ; textures HD | non implémenté |
| `engine/audio` | sortie audio | non implémenté |
| `engine/input` | entrées, commandes tactiles | non implémenté |
| `engine/platform` | implémentations par plateforme (fichiers, fenêtre, logcat...) | non implémenté |
| `engine/game` | logique de jeu | non implémenté |
| `engine/assets` | gestion des ressources fournies par le joueur | non implémenté |
| `engine/localization` | langues sélectionnables | non implémenté |
| `engine/saves` | sauvegardes | non implémenté |
| `engine/mods` | modifications et packs de textures | non implémenté |

Graphe de dépendances visé :

```
game ──► graphics, audio, input, assets, localization, saves, mods ──► core
platform ──► core            (fournit les implémentations dépendantes de l'OS)
android/, ios/ ──► engine + platform
```

Cette séparation moteur / plateforme est inspirée de l'organisation publique de Dusklight (dossiers `src/` et `platforms/` distincts, `mods/` dédié, presets CMake, toolchain iOS séparée). Seule l'arborescence publique a été consultée : le fonctionnement interne de Dusklight n'a pas été analysé et aucun de ses codes n'est utilisé.

## 3. Module `core` (implémenté)

### 3.1 Cycle de vie : `Engine`

```
Created ──initialize()──► Initialized ──shutdown()──► ShutDown
                               ▲                          │
                               └──────initialize()────────┘   (réinitialisation permise)
```

| Opération | Comportement |
|---|---|
| `initialize()` | `InvalidState` si déjà initialisé ; `InvalidArgument` si `maxFrameDeltaSeconds` est non fini ou ≤ 0. Remet les compteurs à zéro. |
| `update(dt)` | `InvalidState` si le moteur n'est pas initialisé ; `InvalidArgument` si `dt` est négatif, NaN ou infini ; un `dt` supérieur à `maxFrameDeltaSeconds` est ramené à cette valeur. Incrémente `frameCount` et `elapsedSeconds`. |
| `shutdown()` | Idempotent : sans effet hors de l'état `Initialized`. |
| Destructeur | Si le moteur est encore initialisé : avertissement dans les logs puis `shutdown()`. |

`Engine` n'est pas thread-safe. En 0.1.0 il ne fait que compter les images et le temps simulé ; c'est le point d'accroche des futurs sous-systèmes, pas un moteur de jeu.

### 3.2 Logs : `Logger`

- Niveaux : `Trace`, `Debug`, `Info`, `Warning`, `Error`. Niveau minimal par défaut : `Info`.
- Macros `MOONLIGHT_LOG_<NIVEAU>(catégorie, format, ...)` ; le format est de type `printf` et vérifié à la compilation (GCC/Clang).
- Les messages plus longs que 1024 octets sont formatés sur le tas, sans troncature.
- Thread-safe. Le sink est appelé hors verrou : un sink peut lui-même journaliser sans interblocage.
- Destination par défaut : `stderr`, avec le temps écoulé depuis la création du logger. `Logger::setSink()` permet de rediriger les messages (c'est ainsi que les plateformes brancheront logcat sur Android, et que le test de fumée capture les logs).

### 3.3 Erreurs : `Status` et `ErrorCode`

`Status` est `[[nodiscard]]` : ignorer un échec produit un avertissement de compilation (une erreur avec `MOONLIGHT_WARNINGS_AS_ERRORS`). Les codes sont volontairement peu nombreux (`None`, `InvalidState`, `InvalidArgument`) et ne seront étendus que sur besoin réel.

### 3.4 Version

`engine/core/version.hpp.in` est transformé par CMake en `core/version.hpp` (dans le dossier de build) à partir de `project(VERSION ...)`, ce qui garde une source unique pour le numéro de version.

## 4. Système de compilation

- CMake ≥ 3.21, C++20, extensions du compilateur désactivées, Ninja via `CMakePresets.json`.
- `engine/` produit la bibliothèque statique `moonlight_engine` (alias `moonlight::engine`) ; les en-têtes s'incluent par `#include "core/engine.hpp"`.
- La cible d'interface `moonlight_warnings` centralise les avertissements (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`).
- `tools/smoke_test` produit `moonlight_smoke_test`, enregistré dans CTest.
- *Prévu :* fichiers de toolchain Android (NDK) pilotés par Gradle, toolchain iOS, intégration continue.

## 5. Tests

Le test de fumée (`tools/smoke_test/main.cpp`) vérifie le cycle de vie normal, les transitions invalides, les arguments invalides, le plafonnement du pas de temps, la réinitialisation, le comportement du destructeur, le filtrage par niveau de log, le formatage et les messages longs. Il ne couvre que `core` : aucun autre module n'existe. Il a été exécuté en Debug, Release, avec AddressSanitizer/UBSan (GCC) et avec Clang.

## 6. Hors périmètre de la 0.1.0

Lecture ou décodage des données du jeu, rendu (Vulkan/Metal), audio, entrées tactiles, localisation, sauvegardes, mods, projets Android/iOS, JNI. Rien de tout cela n'est commencé.
