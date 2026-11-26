# Architecture de MOONLIGHT (version 0.2.0)

Ce document décrit ce qui **existe réellement** en 0.2.0, les décisions d'architecture et leurs raisons, et les limites connues. Les parties marquées *prévu* ne sont pas implémentées.

## 1. Principes

1. **Moteur indépendant de la plateforme.** Le code du moteur ne dépend ni d'Android, ni d'iOS, ni d'une API graphique. Tout ce qui dépend du système vit dans les `.cpp` d'implémentation de `engine/platform/` (et, plus tard, dans `android/` et `ios/`).
2. **Séparation stricte des couches, vérifiée automatiquement.** `core` ← `platform` ← `assets`. Le test CTest `moonlight_layering_check` (`tools/check_layering.cmake`) échoue si un module en inclut un autre hors de ces règles, ou si un en-tête (ou un `.cpp` hors `platform/`) inclut une API système (`<filesystem>`, `<unistd.h>`, `<sys/...>`, `<windows.h>`, `<android/...>`, `<jni.h>`...).
3. **Pas de système fictif.** Un module n'obtient du code que lorsqu'il est implémenté et testé. Les dossiers non implémentés contiennent seulement un `README.md` qui l'indique.
4. **Erreurs explicites, sans exceptions.** Les échecs prévisibles sont retournés par valeur (`Status`, `Result<T>`).
5. **Peu de dépendances.** Uniquement la bibliothèque standard C++20 et les threads du système. Aucune bibliothèque externe, y compris pour les tests.
6. **Aucun contenu propriétaire.** Le jeu original est fourni par le joueur et n'est jamais versionné ; `tools/check_no_game_data.cmake` (CTest) refuse les extensions de dumps courantes et les fichiers de plus de 10 Mio.

## 2. Modules

| Dossier | Rôle | Statut 0.2.0 |
|---|---|---|
| `engine/core` | cycle de vie, logs, erreurs, configuration, version | **implémenté** |
| `engine/platform` | interfaces système (fichiers, horloge) + implémentation hôte | **implémenté** (hôte Linux) |
| `engine/assets` | gestion des ressources (chemins virtuels, montages) | **implémenté** |
| `engine/graphics` | abstraction de rendu ; Vulkan (Android), Metal (iOS) ; textures HD | non implémenté |
| `engine/audio` | sortie audio | non implémenté |
| `engine/input` | entrées, commandes tactiles | non implémenté |
| `engine/game` | logique de jeu | non implémenté |
| `engine/localization` | langues sélectionnables | non implémenté |
| `engine/saves` | sauvegardes | non implémenté |
| `engine/mods` | modifications et packs de textures | non implémenté |

Dépendances réelles (une bibliothèque statique par module) :

```
assets ──► platform ──► core
moonlight::engine = cible INTERFACE qui agrège core + platform + assets
```

Dépendances visées pour la suite (*prévu*) : `game` au sommet, pouvant dépendre de tous les autres ; `graphics`, `audio`, `input`, `localization`, `saves`, `mods` dépendant de `core` (et de `platform`/`assets` au besoin) ; `android/` et `ios/` dépendant du moteur et fournissant leurs implémentations des interfaces de `platform`.

Cette séparation moteur / plateforme est inspirée de l'organisation publique de Dusklight (dossiers `src/` et `platforms/` distincts, `mods/` dédié, presets CMake, toolchain iOS séparée). Seule l'arborescence publique a été consultée : le fonctionnement interne de Dusklight n'a pas été analysé et aucun de ses codes n'est utilisé.

## 3. Module `core`

### 3.1 Cycle de vie : `Engine` (inchangé depuis la 0.1.0)

```
Created ──initialize()──► Initialized ──shutdown()──► ShutDown
                               ▲                          │
                               └──────initialize()────────┘   (réinitialisation permise)
```

`initialize()` échoue (`InvalidState`) s'il est déjà initialisé et (`InvalidArgument`) si `maxFrameDeltaSeconds` est non fini ou ≤ 0 ; `update(dt)` exige un moteur initialisé et un `dt` fini ≥ 0, et ramène un `dt` trop grand à `maxFrameDeltaSeconds` ; `shutdown()` est idempotent. `Engine` n'est pas thread-safe et **n'utilise pas encore** la configuration ni le gestionnaire de ressources : c'est le point d'accroche des futurs sous-systèmes (intégration prévue en 0.3.0).

### 3.2 Erreurs : `Status`, `ErrorCode`, `Result<T>`

`Status` et `Result<T>` sont `[[nodiscard]]`. `Result<T>` contient soit une valeur, soit un `Status` d'erreur ; lui donner un `Status` « ok » produit une erreur `InvalidState` plutôt qu'un résultat vide. Codes : `None`, `InvalidState`, `InvalidArgument` (0.1.0) puis `NotFound`, `IoError`, `ParseError`, `AccessDenied`, `TooLarge` (ajoutés en 0.2.0, à la fin de l'énumération).

### 3.3 Logs : `Logger`

- Niveaux `Trace`, `Debug`, `Info`, `Warning`, `Error` ; niveau minimal par défaut `Info` ; thread-safe ; le sink est appelé hors verrou (un sink peut journaliser sans interblocage).
- **Messages classiques** : `MOONLIGHT_LOG_<NIVEAU>(catégorie, format, ...)`, format `printf` vérifié à la compilation. Inchangé.
- **Messages structurés** (0.2.0) : `MOONLIGHT_LOG_FIELDS(niveau, catégorie, "message", {"clé", "valeur"}, ...)`. Les champs ne sont évalués que si le niveau est actif. `LogRecord` porte `fields` et `uptimeSeconds`.
- **Formats** : `Text` (`[  12.345] [INFO ] [catégorie] message clé=valeur`, valeurs entre guillemets si nécessaire) et `Json` (JSON Lines, échappement RFC 8259). Les octets non ASCII sont recopiés tels quels : fournir de l'UTF-8.
- **Sorties** : `stderr` (défaut, format identique à la 0.1.0 pour les messages sans champs, vérifié par un test), sink fichier en ajout (une ligne écrite d'un bloc sous verrou : pas d'entremêlement entre threads), ou sink personnalisé via `setSink()` (point d'entrée prévu pour logcat sur Android).

### 3.4 Configuration

Structure typée `Config` = `LogConfig` + `PathsConfig` + `EngineSettings`, avec valeurs par défaut. `paths.game_data_dir` est **vide par défaut** : les données du jeu sont fournies par l'utilisateur.

Format texte de type INI, analyseur écrit à la main et **strict** : toute clé ou section inconnue, tout doublon (insensible à la casse), toute valeur invalide, toute erreur de guillemets est une erreur `ParseError` avec **numéro de ligne** ; aucune configuration partielle n'est jamais retournée. Pas de commentaire en fin de ligne (une ligne de commentaire commence par `#` ou `;`). `serializeConfig()` produit un texte que `parseConfig()` relit à l'identique (y compris les nombres flottants).

| Fonction | Rôle |
|---|---|
| `validateConfig` | plages (`0 < max_frame_delta_seconds ≤ 10`), chemins (≤ 4096 octets, sans caractère de contrôle, `mods_dir`/`saves_dir` non vides), énumérations |
| `parseConfig` / `serializeConfig` | texte ⇄ `Config` (module pur, sans accès disque) |
| `makeEngineConfig` | `EngineConfig` déduit de la configuration |
| `applyLogConfig` | applique niveau/format/destination au `Logger` ; **tout ou rien** (si le fichier de log ne s'ouvre pas, le `Logger` reste inchangé) |

Le chargement et la sauvegarde sont dans `platform/config_io.hpp` : `loadConfig`, `loadConfigOrDefault` (seul un fichier **absent** donne les valeurs par défaut ; un fichier présent mais invalide ou illisible est une erreur) et `saveConfig` (validation, écriture dans `<fichier>.tmp` puis renommage ; en cas d'échec l'ancien fichier reste intact).

### 3.5 Utilitaires internes

`core/path_util.hpp` (manipulation **textuelle** de chemins : `joinPath`, `parentPath`, `isStrictlyInside` par composantes entières) et `core/text_util.hpp` (ASCII indépendant de la locale). `core/version.hpp` est généré par CMake à partir de `project(VERSION ...)`.

## 4. Module `platform`

### 4.1 Interfaces indépendantes du système

`IFileSystem` et `IClock` n'utilisent que des types standard portables : chemins **UTF-8 avec `/`** (`std::string_view`), `std::byte`, `std::span`, `std::chrono`. Aucun `std::filesystem::path`, descripteur ou type système n'apparaît dans les en-têtes.

`IFileSystem` : `fileType` (un élément absent est la *valeur* `NotFound`), `readFile` (borné par `maxBytes`, `TooLarge` sans lire le contenu), `writeFile`, `createDirectories`, `renameEntry` (remplace la destination), `removeFile`, `canonicalPath` (chemin absolu, liens résolus ; sert à vérifier qu'un chemin ne sort pas d'une zone). Codes d'erreur communs documentés dans l'en-tête. `IClock` : `monotonicNow()` et `systemNow()`.

### 4.2 Implémentation hôte

`HostFileSystem` et `HostClock` reposent sur la bibliothèque standard (`<filesystem>` avec `error_code`, `<cstdio>`), sans exceptions. **Testées uniquement sous Linux x86-64.** Les erreurs du système sont classées (`NotFound`, `AccessDenied`, `InvalidArgument`, `IoError`).

### 4.3 Anticipation d'Android et d'iOS

- Le moteur n'accède aux fichiers **que** par `IFileSystem` : un portage fournit sa propre implémentation (dossiers de l'application, `Context.getFilesDir`, bac à sable iOS...) sans modifier `core` ni `assets`.
- Les sorties de log passent par un `LogSink` : Android branchera logcat via `Logger::setSink()`.
- Les chemins sont des chaînes UTF-8 en `/` : pas de dépendance au format de chemin de l'hôte dans les interfaces.
- **Point ouvert, non résolu en 0.2.0 :** les assets d'un APK Android ne sont pas des fichiers du système de fichiers et n'ont pas de chemin canonique. `ResourceManager::mountDirectory` suppose un dossier avec `canonicalPath`. Pour y lire des assets d'APK il faudra soit une implémentation d'`IFileSystem` adaptée, soit une abstraction de « source de montage » ; ce choix est reporté tant qu'aucun code Android n'existe.
- Aucun code Android ou iOS n'a été ajouté.

## 5. Module `assets` : `ResourceManager`

Le code du moteur demande des ressources par **chemin virtuel** (`textures/hd/menu.png`) ; elles sont résolues dans des dossiers **montés** explicitement (`mountDirectory(nom, dossier, priorité)`), consultés par priorité décroissante (à priorité égale : premier monté). Un montage prioritaire masque les fichiers de même chemin des autres (base pour les mods et les textures HD).

**Modèle de sécurité :**

1. Le chemin virtuel est validé **avant tout accès disque** : refus des chemins absolus, des composantes `.` et `..`, de `//` et du `/` final, de `\`, de `:`, des caractères de contrôle et des chemins de plus de 1024 octets.
2. Le chemin résolu est canonisé (liens symboliques résolus) et doit rester **strictement à l'intérieur** de la racine canonique du montage, par comparaison de composantes entières (`/data/game-evil` n'est pas dans `/data/game`). Un lien sortant donne `AccessDenied` et un avertissement structuré. Les liens qui restent dans le montage sont autorisés.
3. **Échec fermé :** toute erreur autre que « absent » est retournée, jamais contournée en passant au montage suivant (un fichier illisible dans un mod ne fait pas servir la version de base ; une évasion détectée n'est pas « corrigée » par un autre fichier). `exists()` ne transforme pas une erreur en « faux ».
4. Lecture bornée (`maxResourceBytes`, 256 Mio par défaut).

Ce module ne contient aucune donnée du jeu et n'interprète aucun format : il localise et lit des octets. Pas de cache en 0.2.0.

## 6. Utilisation (exemple compilé et exécuté par la CI)

`tools/examples/compose_modules.cpp` compose les modules au niveau de l'application : configuration (`loadConfigOrDefault`) → `applyLogConfig` → montage de `paths.game_data_dir` s'il est renseigné → `Engine`. C'est une référence d'usage, pas un jeu ; `Engine` et `ResourceManager` y sont créés côte à côte (leur intégration est prévue pour la 0.3.0).

## 7. Système de compilation

- CMake ≥ 3.21, C++20, extensions du compilateur désactivées, Ninja via `CMakePresets.json` (`debug`, `release`, `debug-sanitize`).
- Une bibliothèque statique par module : `moonlight_core`, `moonlight_platform`, `moonlight_assets` (alias `moonlight::core`, `moonlight::platform`, `moonlight::assets`). **`moonlight::engine` est conservée** comme cible `INTERFACE` agrégeant les trois : le code qui la liait en 0.1.0 compile sans changement. Les en-têtes s'incluent par `#include "core/engine.hpp"`, `"platform/filesystem.hpp"`, `"assets/resource_manager.hpp"`.
- La cible d'interface `moonlight_warnings` centralise les avertissements (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`, plus `-Werror` dans les presets).
- CI GitHub Actions (`.github/workflows/ci.yml`) : GCC 13/14 et Clang 18, Debug/Release, plus ASan/UBSan, sur Ubuntu 24.04 x86-64 uniquement. Elle exécute `ctest`, donc tous les tests ci-dessous.
- *Prévu :* toolchain Android (NDK) pilotée par Gradle, toolchain iOS, CI ARM64 et Android.

## 8. Tests

Aucune dépendance de test externe : `tests/support/check.hpp` fournit `CHECK`, `CHECK_OK`, `CHECK_CODE`, `SKIP_TEST`. Un échec est rapporté sans interrompre le test.

| Test CTest | Couvre |
|---|---|
| `moonlight_smoke_test` | test de fumée de la 0.1.0 (`Engine`, `Logger`), **inchangé** |
| `moonlight_core_test` | `Status`, `ErrorCode`, `Result<T>`, utilitaires de chemins |
| `moonlight_logger_test` | niveaux, champs structurés, formats texte/JSON et échappements, équivalence avec le format 0.1.0, sink fichier, threads |
| `moonlight_platform_test` | le **même contrat** exécuté sur `HostFileSystem` (vrai disque) et `MemoryFileSystem` ; liens symboliques, permissions, fichiers spéciaux, horloges |
| `moonlight_config_test` | défauts, analyse, erreurs avec numéros de ligne, validation, aller-retour, application au logger, chargement/sauvegarde (mémoire et disque), sauvegarde sûre |
| `moonlight_resource_test` | syntaxe des chemins, montages, priorités, échec fermé, traversée de chemins, évasions par liens symboliques, comparaison de préfixes, disque réel |
| `moonlight_layering_check` | règles de couches et en-têtes système interdits |
| `moonlight_no_game_data_check` | absence de dumps/archives et de gros fichiers |
| `moonlight_example_compose` | l'exemple de composition s'exécute |

Les doubles de test (`MemoryFileSystem` : liens symboliques, fichiers illisibles et pannes d'écriture simulables ; `ManualClock`) vivent dans `tests/support/` et **ne sont pas livrés** avec le moteur. Certains tests de permissions sont sautés (et comptés comme tels) quand ils s'exécutent en root, où les permissions ne s'appliquent pas ; les runners GitHub standard n'exécutent pas les jobs en root, mais cela n'a pas encore été constaté pour ces tests. Ils ont été exécutés ici sous un utilisateur non privilégié (aucun test sauté).

## 9. Décisions d'architecture

| # | Décision | Raison | Contrepartie |
|---|---|---|---|
| D1 | Configuration INI avec analyseur écrit à la main | zéro dépendance, format lisible et éditable, contrôle total des erreurs | sous-ensemble volontairement limité (pas d'imbrication, de listes ni de commentaires en fin de ligne) |
| D2 | Une bibliothèque par module, agrégat `moonlight::engine` conservé | rend les couches visibles dans le build et vérifiables ; compatibilité 0.1.0 | plus de fichiers CMake |
| D3 | Harnais `CHECK` maison | aucune dépendance, build rapide | pas de découverte automatique des tests ni de rapport détaillé |
| D4 | `Status` / `Result<T>` plutôt que des exceptions | cohérent avec la 0.1.0 ; prévisible sur mobile | l'appelant doit tester les résultats (`[[nodiscard]]` aide) |
| D5 | Chemins `std::string_view` UTF-8 en `/` dans les interfaces | aucune fuite de `std::filesystem` ni de type système ; portable | conversions dans les implémentations ; Windows non géré |
| D6 | Échec fermé du `ResourceManager` | une erreur de sécurité ou d'E/S ne doit jamais être masquée par un repli silencieux | un mod défectueux bloque la ressource au lieu d'être ignoré |
| D7 | Évolutions du `Logger` strictement additives | compatibilité de l'API publique | `LogRecord` grossit de deux champs |
| D8 | Configuration pure dans `core`, E/S dans `platform` | `core` reste sans dépendance ; tests sans disque | deux endroits à connaître |
| D9 | Doubles de test hors du moteur | le moteur ne livre pas de code de test | quelques fichiers dans `tests/support/` |

## 10. Compatibilité avec la 0.1.x

Aucune API publique de la 0.1.x n'est retirée ni modifiée : `Engine`, `EngineConfig`, `Status`, `Logger`, les macros `MOONLIGHT_LOG_*`, `LogRecord{niveau, catégorie, message}` et la cible `moonlight::engine` sont conservés, et le test de fumée de la 0.1.0 est inchangé et passe. Changements à connaître, tous justifiés ci-dessus :

- `ErrorCode` reçoit cinq nouvelles valeurs ; un `switch` du code appelant sur `ErrorCode` sans `default` produira un avertissement `-Wswitch` à recompiler.
- `LogRecord` reçoit `fields` et `uptimeSeconds` avec valeurs par défaut.
- `moonlight_engine` n'est plus une bibliothèque statique mais une cible `INTERFACE` ; le code qui la *lie* n'est pas affecté.
- La sortie `stderr` par défaut est produite par le nouveau formateur ; elle est identique à celle de la 0.1.0 pour les messages sans champs.
- La version du projet passe à `0.2.0` (`MOONLIGHT_VERSION_STRING`).

## 11. Limitations techniques connues (0.2.0)

- **Plateformes :** testé uniquement sous Ubuntu 24.04 x86-64. ARM64, Codespaces, Android, iOS, Windows et macOS ne sont pas vérifiés.
- **Course contrôle/usage (TOCTOU) :** `ResourceManager` vérifie le chemin canonique puis lit le fichier en deux opérations. Un attaquant qui peut modifier le contenu du dossier monté **pendant** l'exécution pourrait remplacer un fichier par un lien entre les deux. Ce n'est pas éliminé (cela demanderait des API de type `openat`/`O_NOFOLLOW` hors de l'interface portable) ; les dossiers montés doivent être des dossiers de confiance de l'utilisateur.
- **Casse et normalisation :** les noms de fichiers sont comparés tels quels ; les systèmes insensibles à la casse (macOS, Windows) ne sont pas traités spécifiquement.
- **UTF-8 non validé** dans les chemins et les logs : les octets sont transmis tels quels.
- **`HostFileSystem` :** pas de verrouillage de fichier, pas d'écriture atomique intrinsèque (l'atomicité de `saveConfig` repose sur `renameEntry`), `canonicalPath` exige que l'élément existe.
- **`Logger` :** singleton global ; le sink fichier ignore les erreurs d'écriture ultérieures ; la rotation de fichiers n'existe pas.
- **Configuration :** pas de rechargement à chaud ; les clés inconnues sont des erreurs (une configuration écrite par une version plus récente sera rejetée par une version plus ancienne) ; pas de paramètres de rendu, d'audio, d'entrées ni de langue (ces systèmes n'existent pas).
- **`ResourceManager` :** pas de cache, pas de lecture partielle ni asynchrone, pas de liste des fichiers d'un montage, un seul type de montage (dossier).
- **`Engine`** n'utilise ni la configuration ni les ressources (0.3.0).
- Le moteur n'est pas thread-safe, hors `Logger` ; `ResourceManager::resolve/exists/readAll` sont `const` mais les montages ne doivent pas changer pendant leur usage.
- Aucune compatibilité d'API ou d'ABI n'est promise avant la 1.0.

## 12. Hors périmètre de la 0.2.0

Lecture ou décodage des données du jeu, rendu (Vulkan/Metal), audio, entrées tactiles, localisation, sauvegardes de jeu, système de mods, projets Android/iOS, JNI, intégration du `ResourceManager` dans `Engine`. Rien de tout cela n'est commencé.
