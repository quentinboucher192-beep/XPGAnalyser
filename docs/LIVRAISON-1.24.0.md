# API Studio et IHM Studio 1.24.0 : la bibliothèque de sons, supprimer plusieurs d'un coup, les chemins de fichiers

Faite à partir de tes demandes du 05/11. Tes mots :
- « reprends ce qu'on avait laissé de côté par rapport aux sons natifs » ;
- « dans variables, types, faire en sorte de pouvoir supprimer plusieurs variables d'un coup, plusieurs types d'un coup » ; « partout pouvoir supprimer plusieurs d'un coup (à appliquer aussi dans l'explorateur d'application, le panneau de gauche) » ;
- « à chaque fois qu'il y a un chemin vers un fichier ou un dossier, je veux pouvoir faire clic droit → "Ouvrir dans l'explorateur" ou "Aller à" ».

Elle se met à jour par-dessus la 1.23.1 et garde tes données. Tes projets se relisent tels quels.

La mise en page des rapports (tes maquettes du 05/11) n'est pas dans cette version : elle attend ta validation.

## 1. Ce qui change pour toi

### La bibliothèque de sons

Le travail commencé en 1.14.2 avait été perdu avec l'ancienne machine de construction : il a été refait entièrement.

IHM › Ressources › **Bibliothèque de sons** :
- **22 sons natifs**, fabriqués par le logiciel : rien à télécharger, rien à licencier, le même son partout. Rangés en dossiers dépliables, chacun avec un cadenas :
  - Alarmes : critique (une sirène à deux tons), haute, moyenne et basse priorité, sirène montante, klaxon, bip continu. Plus une alarme est grave, plus son son est rapide et insistant ;
  - Notifications : notification, information, succès, erreur, avertissement, acquittement ;
  - Interface : clic, bip court, bip double, touche, refus ;
  - Procédé : démarrage, arrêt, cycle terminé, carillon.
- **Écouter** (Espace, double-clic) : la fiche à droite dit à quoi sert le son, sa durée, et montre sa forme d'onde.
- **Importer dans le projet** (Entrée) : le son devient une ressource (`alarme-haute.wav`). Le même son déjà là est réutilisé ; la colonne « Dans le projet » le dit. Ctrl+Z retire l'import.
- **Chercher un son** : le nom, le dossier, la description, les mots-clés (sirène, porte, acquittement…).
- **Mes sons** : tes fichiers WAV ou MP3, par le bouton + ou lâchés sur la liste. Gardés hors du projet, dans tes réglages (à côté de Mes images) : ils te suivent d'un projet à l'autre. Retirer (Suppr) n'enlève qu'un de tes sons ; Ouvrir dans l'explorateur montre son fichier.
- **Ressources › Ma bibliothèque** ajoute aussi un son du projet à Mes sons.
- **Le son d'une priorité d'alarme** et **l'action Jouer un son** proposent, après les sons du projet, ceux de la bibliothèque (« Bibliothèque › Alarmes › Alarme critique ») : le choisir l'importe et le règle d'un seul geste.

### Supprimer plusieurs d'un coup

**Dans les listes** (Variables, Types et tous les volets à liste) :
- choisis plusieurs lignes (Ctrl+clic, Maj+clic) : le bouton dit « Supprimer les 5 » ; Suppr et le clic droit font pareil ;
- une seule question si la suppression d'une ligne en pose une, avec les noms et celles qui servent encore ;
- un seul Ctrl+Z les rend toutes ;
- une ligne encore employée ou fournie reste (un type utilisé, un style ou une couleur fournis, un groupe qui a des comptes, la langue du projet) : le compte rendu dit laquelle et pourquoi ;
- Renommer, Dupliquer et ce qui ne vaut que pour une ligne se grisent : « une à la fois ».
- Les volets : variables et leurs dossiers, types et leurs membres, fonctions, scripts, vues, alarmes, recettes (éléments, jeux), utilisateurs, groupes, rôles, ressources, fichiers externes, styles, couleurs sémantiques, formats, langues, unités de mesure, rapports, destinataires, essais et leurs pas, équipements, écrans du poste, actions et raccourcis d'un objet, popups d'un symbole, méthodes, opérateurs, valeurs d'énumération, cas de test ; côté automate : variables, tables d'animation, DDT.

**Dans l'arbre du projet** (le panneau de gauche) :
- choisis les éléments avec Ctrl+clic ou Maj+clic, même de genres différents (des vues et des scripts) ;
- clic droit « Supprimer les 5 éléments… » (ou « Supprimer les 3 vues… »), ou Suppr ;
- une seule question : quelques noms, ce qui ne part pas et pourquoi, ce que Ctrl+Z rendra ; un seul Ctrl+Z rend tout ;
- côté IHM : vues, popups, modèles, en-têtes et pieds, symboles (avec leurs popups), scripts, fonctions, types, variables, alarmes, recettes, utilisateurs, groupes vides, tes styles et couleurs sémantiques, dossiers des listes ; côté automate : sections, sous-routines, variables, DDT, DFB, unités, tables d'animation, tâches.

### Les chemins de fichiers

Partout où un chemin de fichier ou de dossier s'affiche, le clic droit propose :
- **Ouvrir dans l'explorateur** : l'Explorateur de Windows, le fichier choisi (un dossier : ouvert) ;
- **Aller à** : l'élément dans le logiciel (le projet, une vue, les dossiers Ressources, Historique, Enregistrements, Incidents : leur nœud dans l'arbre ; une ressource, un fichier externe, un enregistrement, un dossier d'incident : leur volet). Ailleurs, un fichier s'ouvre dans l'aperçu des fichiers, un dossier dans Chercher des fichiers ;
- **Copier le chemin**.

Un chemin qui n'existe pas grise les entrées, avec la raison. Où : l'inspecteur (image, vidéo, affiche, police, document, fichier externe d'un Tableau ou d'une Courbe, dossier de l'Explorateur de fichiers), les tableaux Ressources, Fichiers externes et Enregistrements, la fiche d'un enregistrement, la configuration des enregistrements, l'historique, les bibliothèques reliées, le fichier de stockage d'une variable rémanente, le fichier source de l'automate, l'action Exporter un enregistrement, le Stockage PC de la mémoire de l'IA, et des nœuds de l'arbre (vue enregistrée, macro, bibliothèque, dossiers Ressources, Historique, Enregistrements, Incidents).

## 2. Les essais

Sous Linux : **133 suites d'essais sur 133** réussies (CTest), dont une nouvelle.

Au premier passage, `hmieditor` a eu 4 échecs, tous dus aux nouveaux sujets d'aide :
- les sujets « Supprimer plusieurs éléments d'un coup », « Les chemins de fichiers » et « La bibliothèque de sons » n'avaient pas de ligne de l'arbre, alors chacun de leurs tutoriels n'avait pas de geste. Ils ont maintenant la leur : Vues pour le premier, Ressources pour les deux autres ;
- deux comptes du chapitre Démarrer (29 sujets, 15 pages) n'incluaient pas les deux nouveaux sujets : ils sont passés à 31 et 17.

Après correction, l'essai repasse, puis tout a été reconstruit et revérifié.

- **Nouvel essai : `sound_library_test`** (33 vérifications). Il couvre :
  - les 22 sons natifs : un WAV valide, une durée, un niveau ni muet ni saturé, un dossier ; toujours le même son d'une fois à l'autre ;
  - la recherche : par le début d'un mot, sans accents ni casse ;
  - Mes sons : ajouter, le même contenu déjà là, un autre contenu sous le même nom, ce qui n'est pas un son, retirer (un natif ne se retire pas) ;
  - importer : le même son réutilisé, un nom pris par un autre contenu ;
  - les choix proposés aux alarmes et à l'action Jouer un son, les durées écrites.
- **`hmi_editor_test`** : 9 972 contrôles (9 839 en 1.23.1). Ce qui est nouveau :
  - **la bibliothèque de sons** : le volet, les dossiers, la recherche, importer puis Ctrl+Z, un fichier lâché dans Mes sons, retirer, le son natif d'une priorité d'alarme puis Ctrl+Z, Ma bibliothèque ;
  - **supprimer plusieurs dans les volets à liste** : le bouton « Supprimer les N », une seule question, un seul Ctrl+Z, les lignes qui restent et pourquoi, les gestes « une à la fois » grisés ;
  - **supprimer plusieurs dans l'arbre** : des éléments de genres différents, le libellé du menu, ce qui ne part pas, un seul Ctrl+Z ;
  - **les chemins** : Ouvrir dans l'explorateur, Aller à et Copier le chemin dans l'inspecteur, les tableaux et l'arbre ; un chemin absent grisé avec sa raison.

## 3. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.24.0.exe` : l'installateur (48 899 022 octets). Il met à jour la 1.23.1 au même endroit ;
- `API-Studio-1.24.0-portable.zip` et `IHM-Studio-1.24.0-portable.zip` (167 fichiers chacun) ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.24.0.exe` | `6F9F75BD3AD48F9742AA0AE040F0EAC1F8853A423DB869AEB6F112DD7788C225` |
| `API-Studio-1.24.0-portable.zip` | `0AF8497F1C69D4BB0FD19F694338490499DD7982D4FE315529670EC6AEACF342` |
| `IHM-Studio-1.24.0-portable.zip` | `494E66E950C287DB2F104B38D9BB8B747F3A65F267F98A4C0DD51EB76C064F33` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.23.1 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.24.0 » et « IHM Studio 1.24.0 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.23.1 est gardée.

L'interface n'a pas été ouverte dans une fenêtre.

## 4. Limites

- **Les chemins** : les champs de saisie de chemin des fenêtres (avec leur bouton « … ») et les simples libellés ne sont pas couverts ; la liste des incidents et Chercher des fichiers gardent leurs propres entrées (Ouvrir le dossier, Copier le chemin).
- **Supprimer plusieurs** : les enregistrements, les incidents et les listes de l'Assistant IA se suppriment sur le disque, une ligne à la fois (leur propre confirmation, sans Ctrl+Z) ; les requêtes et les jeux de l'outil Modbus, les bibliothèques, les tâches, les modules, les sections et les blocs aussi. Les macros et les versions supprimées depuis l'arbre ne reviennent pas par Ctrl+Z (la question le dit).
- **Les volets de l'automate** (variables, tables d'animation, DDT) suppriment à plusieurs, mais n'ont pas d'essai automatique : seule leur construction est vérifiée.
- **Les équipements** supprimés à plusieurs gardent toujours leurs variables, déliées.
- **Mes sons** : un son de Mes sons se retire un à la fois.
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows.** Les nouveaux écrans n'ont été vus que dessinés hors écran par les essais : l'application n'a pas été ouverte dans une fenêtre. Les sons n'ont pas été écoutés sur une carte son : leur forme, leur durée et leur niveau sont vérifiés par les essais.
