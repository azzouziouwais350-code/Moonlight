# engine/platform

**Statut (0.2.0) :** implémenté pour l'hôte (testé sous Linux x86-64 uniquement).

- `filesystem.hpp` : interface `IFileSystem`, indépendante du système (chemins UTF-8 avec `/`, `std::byte`).
- `clock.hpp` : interface `IClock`.
- `host_filesystem.*`, `host_clock.*` : implémentations sur la bibliothèque standard.

Règle : les **en-têtes** de ce dossier ne contiennent aucune API spécifique à un système ; seuls les `.cpp` d'implémentation peuvent s'appuyer sur le système (vérifié par `tools/check_layering.cmake`). Les futures implémentations Android et iOS s'ajouteront ici, derrière les mêmes interfaces.

Non implémenté : toute implémentation Android ou iOS.
