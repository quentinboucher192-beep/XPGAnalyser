# API Studio

Référence fonctionnelle d'**API Studio** (exécutable `API-Studio.exe`, édition `core::Edition::Api`), version 1.35.0. Chaque section indique où se trouve la fonctionnalité dans le logiciel, comment elle fonctionne, et le code qui l'implémente. Le mode d'emploi pas à pas est dans l'aide F1 de l'application. Retour au [README](../README.md).

## Sommaire

1. [Rôle](#role)
2. [Projets](#projets)
3. [Import d'un export Control Expert](#import)
4. [L'écran du projet](#ecran)
5. [Variables](#variables)
6. [Types dérivés, blocs DFB et bibliothèque partagée](#types)
7. [Configuration matérielle, voies et adresses](#materiel)
8. [Programmation : sections, unités de programme, ordre d'exécution](#programmation)
9. [GRAFCET](#grafcet)
10. [Analyse et diagnostics](#analyse)
11. [Simulation de l'automate](#simulation)
12. [Macros et import de classeurs](#macros)
13. [Exports](#exports)
14. [Ligne de commande](#cli)
15. [Limitations](#limitations)

---

<a id="role"></a>
## 1. Rôle

API Studio travaille sur le **programme d'un automate Schneider Electric programmé avec EcoStruxure Control Expert** : il lit les exports d'échange de Control Expert (`.XPG` pour le programme, `.XHW` pour la configuration matérielle), les range dans un dossier de projet modifiable, les analyse, permet de les modifier et de les simuler, puis régénère des fichiers `.XPG`/`.XHW` à réimporter dans Control Expert.

L'en-tête de `src/import/XpgParser.cpp` décrit le schéma lu (export Control Expert V15.3, DTD 41). API Studio ne remplace pas Control Expert : il ne lit pas le fichier de travail binaire `.STU` (refusé avec une explication, `src/import/ProjectImporter.cpp`) et ne charge rien dans un automate.

---

<a id="projets"></a>
## 2. Projets

**Où** : accueil (**Nouveau projet**, Ctrl+N ; **Ouvrir…**, Ctrl+O ; projets récents ; glisser-déposer), menu **Projet**.

**Format** : un projet est un **dossier** (`src/project/ProjectStore.hpp`) :

```text
MonProjet/
  project.xpgproj          manifeste : nom, version, état, CPU, dates, edition = api
  project.lock             présent seulement à l'état LOCK (sel + empreintes)
  config/hardware.txt      racks, modules, voies, plan mémoire
  vars/globals.txt         dictionnaire des variables globales
  ddt/<Nom>.ddt            un fichier par type dérivé
  dfb/<Nom>/interface.txt  paramètres d'un bloc DFB ; dfb/<Nom>/code/<Section>.st
  units/<Nom>/...          unités de programme (interface et code)
  sections/index.txt       tâche, ordre, condition d'activation de chaque section
  sections/<Nom>.st        code des sections de tâche
  src/MAST.XPG, src/CONFIG.XHW   générés à l'export (supprimables sans perte)
```

Le code est en fichiers `.st` (un par section) et les déclarations en tableaux texte, pour qu'un éditeur externe, un `diff` ou un gestionnaire de versions puissent les lire.

- **Rangement** : `<dossier des données>/projets/api/<Nom>` (`App::projectsRoot`). Un projet d'IHM Studio (`edition = ihm`) est refusé à l'ouverture.
- **Nouveau projet** : un projet vide avec une tâche MAST (`ProjectStore::createEmpty`).
- **Enregistrer** : Ctrl+S.
- **États** DEV, FINISH, LOCK (menu **Version** de la barre du haut : Terminer, Livrer et verrouiller). LOCK bloque l'ouverture **dans l'application** derrière un mot de passe (sel + 200 000 itérations de SHA-256) ; le code précise que c'est un contrôle d'accès, pas un chiffrement : les fichiers restent lisibles en texte (`src/project/Security.hpp`).
- **Versions du projet** (dossier **Versions** de l'arbre) : photos du projet stockées dans `versions/` par empreinte de contenu, comparaison élément par élément, restauration précédée d'une version « Avant restauration » (`src/hmi/HmiVersions.hpp`).
- **Archive** `.xapi` : tout le dossier du projet dans un zip vérifié (voir [FORMATS.md](FORMATS.md)).
- **Plusieurs fenêtres** : Ctrl+Maj+N ouvre une autre fenêtre pour un autre projet.

---

<a id="import"></a>
## 3. Import d'un export Control Expert

**Où** : accueil ou **Projet › Ouvrir…** sur un `.XPG` ; **Ctrl+Maj+O** importe la configuration matérielle (`.XHW`) dans le projet ouvert ; glisser-déposer d'un fichier sur la fenêtre.

**Fonctionnement** (`src/import/`) :

1. le format est reconnu d'après le contenu (`PGMExchangeFile` pour un `.XPG`, `IOExchangeFile`/`HWExchangeFile` pour un `.XHW`) ;
2. un lecteur XML en flux remplit le modèle `domain::Project` : en-tête, CPU (`resource/@resIdent`), tâches et ordre des sections avec leurs conditions d'activation, types dérivés (DDT), blocs DFB, unités de programme, sections, tables d'animation, variables globales et adresses topologiques ;
3. l'analyse (`ProjectAnalyzer`) calcule l'index des références et les constats (§ 10) ;
4. le projet n'est publié à l'interface qu'une fois complet.

Après l'import d'un `.XPG`, la fenêtre **« Créer un projet à partir de cet export »** propose un nom et un dossier ; **Annuler** permet de parcourir l'export en lecture seule (`App::offerProjectFolder`).

**Remplacer le programme d'un projet existant** par un nouvel export (`src/project/MastImport.hpp`) : un plan indique avant l'import ce qui est gardé, changé, supprimé, nouveau, et les liens perdus ; le projet garde ce que le `.XPG` ne contient pas (versions, réglages, configuration matérielle sauf si un `.XHW` accompagne l'export). L'import est une commande annulable.

**Ce qui n'est pas importé** : voir [§ 15](#limitations).

---

<a id="ecran"></a>
## 4. L'écran du projet

- **Arbre du projet** à gauche : dossier **API** (tableau de bord, Configuration, Types dérivés, Blocs DFB, Unités de programme, Ordre d'exécution, Tâches, Variables, Sous-routines, Macros, Tables d'animation), **Simulation**, **Versions** (`src/app/ViewModels.cpp`, `src/app/ApiPanes.hpp`).
- **Onglets** au centre ; un clic dans l'arbre ouvre l'onglet de l'élément. Ctrl+W ferme, Ctrl+Maj+T rouvre le dernier onglet fermé.
- **Barre du haut** : Projet, Version, Annuler, Rétablir, Historique, Nouveau (section, unité, bloc DFB, type dérivé, variable, rack, module, macro), Simuler (F5), Arrêter (Maj+F5), Un cycle, Vers Control Expert, Affichage, Aide, Enregistrer, Notifications, Tâches de fond (`src/app/TopBar.cpp`).
- **Aller à…** (Ctrl+K), filtre de l'arbre (Ctrl+Maj+F), historique de la séance (Ctrl+H), recherche de fichiers sur le PC (Ctrl+Maj+P). La table complète des raccourcis est dans `src/help/Shortcuts.cpp` et dans l'aide.

---

<a id="variables"></a>
## 5. Variables

**Où** : API › Variables (`src/app/VariablesPane.hpp`).

- Variables globales rangées par genre (instances de DFB, blocs standard, instances de types dérivés, variables situées, autres), filtres (situées, instances de DFB, non utilisées), recherche.
- Pour chaque variable : les sections qui la nomment, les tables d'animation qui la montrent ; dépliage des membres sans limite de profondeur (champs d'un DDT, broches d'une instance de bloc, éléments d'un tableau).
- Actions : **+ Variable**, **Renommer** (le code et les tables suivent), **Supprimer**, ajouter à une table d'animation, voir les usages, **Exporter CSV**.
- **Renommer** calcule d'abord un plan de tout ce qui suit (code, tables d'animation, paramètres d'unités) et l'applique en une seule commande (`src/project/RenamePlan.hpp`). Le plan sait renommer : variable globale, variable d'unité, type dérivé, bloc DFB, unité de programme, section, sous-routine, table d'animation.
- **Supprimer** retire une entité et peut la remettre à l'identique par Ctrl+Z (`src/project/DeleteCommands.hpp`).
- Références croisées : où une variable est **écrite** et où elle est **lue** (insensible à la casse, comme le ST) — `src/project/CrossReference.hpp`.

---

<a id="types"></a>
## 6. Types dérivés, blocs DFB et bibliothèque partagée

- **Types dérivés (DDT)** et **Blocs DFB** : onglets de l'arbre ; pour chacun, qui s'en sert (variables, unités, autres DDT, instances, sections appelantes) — `src/project/TypeUsage.hpp`.
- **Bibliothèque partagée** `libs/` (`src/project/SharedLibrary.hpp`) : un dossier par catégorie, un fichier par bloc (`.dfb`, `.ddt`) et un index `libs/index.txt` (genre, catégorie, nom, version, auteur, commentaire). Le dépôt livre 41 DFB, 41 DDT et 31 macros (catégories Alarms, Client, Comm, Control, Equipment, IO, Macros, Measure, System).
- **Les versions sont signalées, jamais appliquées d'office** : un projet garde la copie qu'il a importée tant que l'utilisateur ne décide pas de la mettre à jour.
- L'aide d'un bloc est écrite **dans** son fichier (lignes `#! summary`, `#! usage`, `#! param`…) ; format décrit dans `src/project/FORMAT-AIDE.md`.
- Catalogue des blocs Control Expert pour l'aide à la saisie : `resources/schneider_library.txt`.

---

<a id="materiel"></a>
## 7. Configuration matérielle, voies et adresses

- **Configuration** : racks, modules et voies lus dans le `.XHW` (`src/import/XhwParser.cpp`), vue des racks (`src/app/RackView.cpp`). Un `.XPG` seul ne contient que l'identité du CPU : l'écran le dit et propose d'importer le `.XHW`.
- **Catalogue matériel** `resources/plc_catalog.txt` (stations d'E/S locales Modicon M340, transcrit du catalogue de Control Expert) : il complète ce que l'export ne donne pas (nombre d'emplacements, répartition entrées/sorties, descriptions).
- **Voies et adresses** (`src/project/IoCheck.hpp`) : chaque adresse topologique (`%I0.5.3`, `%Q0.8.4`, `%IW0.10.1`…) nommée par le programme est confrontée au module de son emplacement : absence de module, module dans le mauvais sens, voie au-delà du module ; voies employées par module.
- **Plan mémoire** : variables situées en `%MW`, `%MD`, `%MF`, chevauchements.

---

<a id="programmation"></a>
## 8. Programmation : sections, unités de programme, ordre d'exécution

- **Sections ST** : éditeur de code avec coloration syntaxique (`src/ui/Syntax.*`), repli, recherche, profil de raccourcis de type Visual Studio par défaut (`src/ui/KeyMap.*`, réglage `editeur.raccourcis`), aide à la saisie des noms du programme. Chaque modification est une commande annulable.
- **Unités de programme** (sections multiples, paramètres), **sous-routines** (créées à la main ou par les macros `CreerSR`, `AppelerSR`).
- **Ordre d'exécution** : l'ordre des sections de chaque tâche et leurs conditions d'activation, réordonnables par glisser-déposer (`src/app/ExecutionOrderWiring.*`, `src/domain/ExecutionOrder.*`).
- **Sections LD, FBD, SFC** : conservées telles que Control Expert les a écrites et réexportées à l'identique ; elles sont **lisibles en texte** (une ligne par rangée de ladder, les blocs et broches d'un FBD, les étapes et transitions d'un SFC) mais **ne se modifient pas** dans l'outil (`src/project/GraphListing.hpp`).
- **Comparer des sections** : deux sections choisies (Ctrl+clic), lignes alignées, mots différents repérés, équivalences de noms entre sections jumelles (`src/project/SectionCompare.hpp`).
- **Tables d'animation** : créer (« + Nouvelle table »), renommer, y glisser des variables ; pendant la simulation, valeur en direct, forçage et écriture d'une nouvelle valeur (qui ne touchent que la simulation, pas le projet), courbe de la ligne choisie (`src/app/AnimationTablesPane.hpp`).

---

<a id="grafcet"></a>
## 9. GRAFCET

Les grafcets de ce type de projet **ne sont pas des sections SFC** : chacun est construit en ST (des appels `Builder` qui remplissent des tableaux d'étapes, de transitions et d'actions) et exécuté par une instance du bloc **`DFB_GRAFCETENGINE`** de la bibliothèque (`src/project/Grafcet.hpp`, `libs/Client/LISEZ-MOI-GRAFCET.md`).

- **Où** : double-clic sur une instance du bloc (dans l'arbre, les variables ou le code).
- **Éditeur** (`src/app/GrafcetView.*`, `GrafcetPanels.*`, `GrafcetCodePanel.*`) : le grafcet est relu depuis le ST et dessiné ; outils Sélection, Étape, Transition, Action, Relier, Supprimer ; volet **Code** pour la réceptivité ou le corps d'action, avec raccourcis (`ACTIF(X3)`, `DUREE(X1)`, `FIN(A2)`, `FINI(Autre)`) développés dans le programme.
- Chaque modification **réécrit les sections du programme** et s'annule par Ctrl+Z ; les onglets ST ouverts suivent.
- Disposition manuelle des étapes enregistrée dans un fichier `<projet>.xpglayout` posé à côté du dossier du projet.
- **Contrôles** : pas d'étape initiale, étape sans sortie non finale, étape jamais atteinte, numéros en double, variable inexistante dans une réceptivité… (`src/project/GrafcetCheck.*`).
- **En simulation** : étapes actives et leur durée, transitions validées, actions en cours ; commandes du moteur (initialiser, forcer une étape) ; historique des franchissements.

**1.51.0 — les GRAFCET natifs de Control Expert (sections SFC)** se dessinent, se modifient et se simulent (`src/project/Sfc.hpp`, `SfcEdit.hpp`, `SfcDiagram.hpp`, `src/sim/SfcRun.hpp`, `src/app/SfcPane.hpp`) :

- **Où** : ouvrir la section SFC ; le bouton **Schéma** de sa barre montre le dessin (enfoncé, par défaut) ou la lecture en texte (relâché). « Aller à » et les références croisées choisissent l'élément dans le dessin.
- **Dessin** : la grille de Control Expert (chaque objet dans sa case) peinte par la même vue que les grafcets en ST : étapes (initiale : double carré ; macro-étape), transitions et réceptivités (variable, section ST, section LD), divergences et convergences en OU (trait simple) et en ET (double trait), sauts, liens (et leurs coudes), textes, actions à droite de l'étape (qualificatif N, S, R, L, D, P, P1, P0, DS…, durée, variable ou section) ; zoom, défilement, mini-carte, chercher (Ctrl+F), export PNG.
- **Modifier** : + Étape (une transition et une étape sous la sélection, ce qui est dessous descend), Divergence OU, Divergence ET, Saut, Réceptivité (une variable, `NOT` variable, ou une expression qui part dans une section de transition ST), Action, Étape… (nom, initiale, temps min / max, retard, commentaire), Supprimer (Suppr), glisser une étape dans une autre case ; volet **Code** : la section de transition ou d'action ST se modifie, une section LD se lit. Chaque geste réécrit le XML de la section (seuls les éléments touchés changent) et s'annule d'un Ctrl+Z. **Nouvelle section › SFC** crée un GRAFCET à une étape initiale.
- **Simulation** (F5) : sémantique de Control Expert — étapes initiales au premier cycle, une évolution par cycle, priorité à gauche dans une divergence en OU, `Étape.x`, `Étape.t`, `Étape.tminErr`, `Étape.tmaxErr` lisibles en ST et en LD, actions selon leur qualificatif, retard d'étape ; réceptivités en ST et en LD. Sur le dessin : étapes actives et leur temps, transitions validées et vraies (1) ou fausses (0), actions en cours, supervision en défaut ; Initialiser, Activer / Désactiver l'étape choisie.

---

<a id="analyse"></a>
## 10. Analyse et diagnostics

- **Tableau de bord de l'API** (un clic sur « API ») et sa liste « À regarder » : versions des blocs face à la bibliothèque, variables non utilisées par le programme, etc. (`src/project/ApiChecks.hpp`).
- **Constats de l'analyse** (`importer::FindingKind`, `src/import/ProjectAnalyzer.hpp`) : variable globale ou locale inutilisée, type dérivé ou DFB inutilisé, section jamais planifiée ni appelée, section vide, section sans condition d'activation, adresses en double, type non défini, E/S situées sans commentaire, section trop longue. Gravités : information, avertissement, erreur.
- **Statistiques** du projet ouvert, recalculées après chaque commande : lignes par section et par langage, etc. (`src/project/ProjectStats.hpp`, `src/app/StatisticsPane.*`).
- **Conditions d'activation manquantes** des projets importés avant la 1.8.0 : détectées et signalées (`src/project/ActivationConditions.hpp`).

---

<a id="simulation"></a>
## 11. Simulation de l'automate

**Où** : bouton **Simuler** (F5), **Arrêter** (Maj+F5), **Un cycle** ; dossier **Simulation** de l'arbre : Vue d'ensemble, Débogage, Forçages, Courbes, Journal (`src/app/SimCenter.hpp`, `src/app/SimDebugPane.hpp`).

- **Interpréteur ST** (`src/sim/Interpreter.hpp`) : affectations, IF/ELSIF/ELSE, CASE, FOR, WHILE, REPEAT, EXIT, RETURN, arithmétique et comparaisons, opérateurs logiques, MOD, appels de fonctions et d'instances de blocs (paramètres nommés), tableaux, membres de structures, adresses directes (`%M`, `%MW`, `%I`, `%Q`), littéraux typés. Toute autre construction est **refusée** avec un diagnostic qui nomme la ligne, et la section s'arrête ; chaque boucle et chaque cycle ont un budget d'instructions.
- **Débogage** : points d'arrêt conditionnels, espions, pile d'appels, « qui a écrit ? », temps passé par section, trace du cycle ; Continuer (F5), Section suivante (F10), Cycle suivant (F11), Exécuter jusqu'à la ligne (Ctrl+F10).
- **Forçages**, **courbes** (huit variables au plus, export CSV), **journal** des événements de simulation.
- Pendant la simulation, les valeurs se lisent au survol (infobulle du bouton Simuler, `src/app/TopBar.cpp`).
- La simulation appartient à l'application : elle continue quand on change d'onglet ou d'écran (`src/app/SimulationHost.hpp`).
- Seules les sections ST sont simulées ; les sections LD, FBD, IL et SFC ne sont pas compilables ici.
- 1.51.0 : les sections **SFC** (GRAFCET natifs) sont simulées, leurs réceptivités en ST et en LD comprises (§ 9).

---

<a id="macros"></a>
## 12. Macros et import de classeurs

- Une **macro** est écrite en **ST**, analysée et exécutée par le même interpréteur que la simulation ; elle pilote l'éditeur (créer une section, une variable, une instance, importer un bloc…) — `src/project/Macro.hpp`. Fichiers `.mac` dans `libs/Macros/` (31 livrées : `ImportES`, `ImporterAlarmes`, `ImporterEquipements`, `ImporterReports`, `ImporterGrafcet`, `GenererGrafcet`, `GenererModes`, `GenererSimulation`, `VerifierBibliotheque`…). Formulaires de saisie et onglet Macros (`src/app/MacrosPane.*`, `MacroFormView.*`).
- **Classeurs** : lecture de `.xlsx`/`.xlsm` (`src/xls/`) et de CSV exportés d'Excel, avec détection de la ligne d'en-tête et du séparateur (`src/project/Table.hpp`). Les colonnes attendues de chaque format (entrées/sorties TOR et analogiques `es.di`, `es.do`, `es.ai`, `es.ao`, équipements, reports…) sont déclarées une seule fois dans `src/project/ImportSchema.hpp` et servent à la fois à vérifier le fichier, à afficher l'aide et à produire un fichier d'exemple.
- Le dossier `docs/` contient un classeur d'accompagnement (`automation.xlsx`, `automation.xlsm`, module VBA `Automation.bas`, `make_workbook.py`, guide `GUIDE.txt`) qui tient E/S, équipements et reports dans un seul fichier.

---

<a id="exports"></a>
## 13. Exports

| Export | Où | Code |
|---|---|---|
| **Vers Control Expert** : `<projet>/src/MAST.XPG` et `<projet>/src/CONFIG.XHW` | barre du haut | `src/export/XpgWriter.*`, `ProjectStore::exportSources` |
| **Programme lisible** : texte, Excel (`.xlsx`), PDF, HTML ; tout le programme, une unité ou des sections choisies, dans l'ordre d'exécution | **Projet › Exporter le programme lisible…** | `src/export/ProgramBook*.cpp` |
| **Archive `.xapi`** : tout le projet, manifeste et empreintes, au choix les versions et les macros | **Projet › Exporter en archive .xapi…** ; relecture par **Projet › Importer une archive…** ou Ouvrir | `src/project/ApiArchive.*` |
| Variables en CSV ; courbes de simulation en CSV | volets correspondants | `VariablesPane`, `SimCenter` |

L'écrivain `.XPG` respecte l'ordre imposé par la DTD 41 et écrit les sommes de contrôle des DDT à zéro (Control Expert les recalcule à l'import) ; l'aller-retour import → écriture → réimport est vérifié par l'essai `roundtrip` (`tests/roundtrip_test.cpp`).

---

<a id="cli"></a>
## 14. Ligne de commande

```text
API-Studio.exe --cli <fichier.XPG> [autres fichiers…]
```

Analyse sans fenêtre et écrit un résumé (projet, CPU, nombres de variables, POU, sections, DDT, DFB, lignes, constats). Code de sortie : 0 sans constat de gravité « erreur », 1 sinon, 2 en cas d'échec de lecture (`runCli`, `src/main.cpp`). Les autres options communes sont dans le [README](../README.md#cli).

---

<a id="limitations"></a>
## 15. Limitations

Vérifiées dans le code de ce commit :

- **`.XEF` / `.ZEF`** : reconnus par le détecteur de format, mais **aucun lecteur ne les accepte** ; leur import échoue (« no parser recognises this file »), bien que le dialogue d'ouverture les propose dans son filtre.
- **`.XDB`** : lecteur déclaré qui renvoie « non implémenté ».
- **`.STU`** (fichier de travail binaire) : refusé, avec la consigne d'exporter en `.XEF` ou `.XPG`.
- **LD, FBD, SFC** : conservés et lisibles, ni modifiables ni simulés.
- 1.51.0 : les **SFC** se modifient et se simulent (§ 9) ; une macro-étape s'y simule comme une étape (son contenu n'est pas déroulé), une réceptivité ou une action en FBD ou IL n'est pas simulée (fausse, et dit).
- La simulation ne couvre que le sous-ensemble du ST listé au § 11.
- Le verrou LOCK n'est pas un chiffrement ; la clé maîtresse de récupération est dérivée d'une adresse MAC et n'est pas une frontière de sécurité (le code le dit, `src/project/Security.hpp`).
- L'Assistant IA n'existe pas dans API Studio.
