# engine/assets

**Statut (0.2.0) :** `ResourceManager` implémenté et testé (Linux x86-64).

- Chemins **virtuels** (`textures/hd/menu.png`) résolus dans des dossiers **montés** avec priorité.
- Un chemin virtuel ne peut pas sortir d'un montage : `..`, chemins absolus, `\`, `:` et liens symboliques sortants sont refusés.
- Lecture bornée en taille ; erreurs distinctes (introuvable, illisible, trop gros, accès refusé), jamais de repli silencieux sur un autre montage en cas d'erreur.
- Dépend des **interfaces** de `platform` uniquement.

**Ne fait pas :** aucune donnée du jeu n'est incluse ni interprétée ; aucun format du jeu n'est lu ; pas de cache ; pas d'intégration dans `Engine` (prévue pour la 0.3.0).
