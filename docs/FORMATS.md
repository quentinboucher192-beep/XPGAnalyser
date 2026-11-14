# Formats, données et emplacements

Référence des fichiers lus et écrits par API Studio et IHM Studio (version 1.22.0), d'après le code. Retour au [README](../README.md).

## Sommaire

1. [Extensions de fichiers](#extensions)
2. [Le dossier d'un projet](#projet)
3. [Format du projet IHM et numéros de format](#format-ihm)
4. [Archives `.xapi` et `.xihm`](#archives)
5. [Versions, historiques, corbeille](#versions)
6. [Migrations](#migrations)
7. [Réglages et emplacements sur le poste](#emplacements)
8. [Sauvegardes, réserve de réparation, reprise](#sauvegardes)
9. [Données de l'Assistant IA](#ia)
10. [Confidentialité des données stockées](#confidentialite)

---

<a id="extensions"></a>
## 1. Extensions de fichiers

La liste des fichiers « de l'application » que reconnaît la recherche de fichiers (Ctrl+Maj+P) est déclarée dans `src/app/FileFinder.cpp` ; les autres lignes viennent des modules cités.

### Projets et archives

| Extension | Rôle | Application | Lecture / écriture | Code |
|---|---|---|---|---|
| `project.xpgproj` | Manifeste d'un dossier de projet (nom, version, état, CPU, dates, `edition = api` ou `ihm`) | les deux | lu et écrit | `src/project/ProjectStore.*` |
| `.xapi` | Archive d'un projet d'API Studio (zip + manifeste) | API Studio | lu et écrit | `src/project/ApiArchive.*` |
| `.xihm` | Archive d'un projet d'IHM Studio (zip + manifeste) | IHM Studio | lu et écrit | `src/hmi/HmiArchive.*` |
| `.zip` | Archive de l'IHM des versions 1.x | IHM Studio | lu (importer) | `src/hmi/HmiArchive.hpp` |
| `.st` | Code d'une section, d'un corps de DFB ou d'unité dans le dossier du projet | API Studio | lu et écrit | `ProjectStore` |
| `.vue` | Une vue IHM dans `ihm/vues/` | IHM Studio | lu et écrit | `src/hmi/HmiStore.*` |
| `.xpglayout` | Disposition manuelle des grafcets, posée à côté du dossier du projet | API Studio | lu et écrit | `src/app/GrafcetView.hpp` |

### Exports de Schneider Electric Control Expert

| Extension | Contenu | État dans ce commit |
|---|---|---|
| `.XPG` | Programme (`PGMExchangeFile`, DTD 41) | **Lu** (`XpgParser`) et **écrit** (`XpgWriter`, `<projet>/src/MAST.XPG`) |
| `.XHW` | Configuration matérielle (`IOExchangeFile` / `HWExchangeFile`) | **Lu** (`XhwParser`) et **écrit** (`<projet>/src/CONFIG.XHW`) |
| `.XDB` | Dictionnaire de variables | Lecteur déclaré, renvoie « non implémenté » |
| `.XEF`, `.ZEF` | Projet complet | Reconnus par le détecteur de format, **aucun lecteur ne les accepte** : import en échec |
| `.STU` | Fichier de travail binaire | Refusé avec une explication |

### Bibliothèque et paquets

| Extension | Rôle | Application |
|---|---|---|
| `.dfb`, `.ddt` | Bloc fonction dérivé, type dérivé de la bibliothèque `libs/` (avec son aide intégrée) | API Studio |
| `.mac` | Macro (ST) | API Studio |
| `libs/index.txt` | Index de la bibliothèque : genre, catégorie, nom, version, auteur, commentaire | API Studio |
| `.xpgvues`, `.xpgsymboles`, `.xpgtypes`, `.xpgfonctions`, `.xpgscripts`, `.xpgst`, `.xpgmodele`, `.xpgbib` | Paquets d'export de l'IHM : vues, symboles, types IHM, fonctions IHM, scripts, opérateurs/scripts ST, modèle de vue, bibliothèque IHM | IHM Studio |

### Autres fichiers

| Extension | Rôle | Code |
|---|---|---|
| `.xrec` | Enregistrement de l'IHM en marche (zip ; en-tête lisible, piste et, depuis la 1.20.2, copie chiffrée du projet) | `src/hmi/HmiRecording.hpp` |
| `.xia` | Apprentissage de l'Assistant IA (JSON versionné ; jamais de clé API ni de mot de passe) | `src/app/ai/AiLearningFile.hpp` |
| `.xpgtheme` | Thème de l'interface créé par l'utilisateur | `src/ui/ThemeFile.cpp` |
| `.tuto` | Texte d'un didacticiel | `src/help/Tutorial*.cpp`, `tools/tutoriels/` |
| `.xlsx`, `.xlsm`, `.csv` | Classeurs d'entrée (macros d'import, fichiers externes de l'IHM, pièces jointes de l'Assistant) ; les macros VBA d'un `.xlsm` ne sont jamais exécutées | `src/xls/`, `src/project/Table.hpp` |
| `.csv`, `.xlsx`, `.pdf`, `.html`, `.txt` | Exports : programme lisible, variables, courbes | `src/export/ProgramBook*` |
| `.docx`, `.pdf` | Dossier de l'IHM (`dossier_IHM_<projet>_<date>`, dans `exports/` du projet) | `src/hmi/HmiDossier.hpp`, `src/app/hmi/HmiExchangePanes.cpp` |

Formats acceptés en pièce jointe par l'Assistant IA : voir [ASSISTANT-IA.md](ASSISTANT-IA.md#pieces).

---

<a id="projet"></a>
## 2. Le dossier d'un projet

Un projet est un **dossier**, pas un fichier unique (`src/project/ProjectStore.hpp`). Depuis la 1.12.0, chaque application n'écrit que sa partie : un projet d'IHM Studio n'a que son manifeste, son verrou éventuel et `ihm/`.

```text
<Projet>/
  project.xpgproj        manifeste (formatVersion = 1)
  project.lock           état LOCK seulement
  config/  vars/  ddt/  dfb/  units/  sections/  tables/     programme automate (API Studio)
  src/                   MAST.XPG, CONFIG.XHW générés (Vers Control Expert)
  ihm/                   projet IHM (IHM Studio)
  donnees/               données du projet
  versions/              versions du projet
  exports/               exports (programme lisible, dossier de l'IHM…)
  .xpg/                  cache de construction de l'IHM, rémanence de simulation
```

Dans `ihm/` (`src/hmi/HmiStore.hpp`, `HmiEditHistory.hpp`, `HmiIncident.hpp`) : `ihm.txt` (index et configuration), `vues/NNNN-<nom>.vue`, `scripts/`, `fonctions/`, `ressources/` (fichiers importés tels quels, dossiers d'incident sous `ressources/enregistrements/`), `historique/` (alarmes, mesures, audit en exploitation), `historique-edition/` (historique des modifications des objets), `corbeille/`.

Règles d'écriture :

- chaque fichier est écrit à côté (`.tmp`) puis remplace l'ancien d'un seul renommage (`core::writeFileAtomic`, `src/core/AtomicFile.hpp`) ;
- l'index `ihm.txt` est écrit en dernier, pour ne jamais désigner une vue absente ;
- chaque fichier IHM se termine par la ligne `fin` : un fichier coupé est refusé au chargement ;
- une clé inconnue est signalée dans le rapport de chargement (`LoadReport::warnings`).

1.51.0 : une section **SFC** (GRAFCET natif) reste rangée en `<section>.xml`, le `<SFCProgram>` de Control Expert tel quel (`src/project/Sfc.hpp`). L'éditeur de GRAFCET d'API Studio y écrit le même XML (étapes, transitions, divergences, sauts, liens, actions, `transitionSource`/`actionSource`) : seuls les éléments modifiés sont réécrits, le reste garde ses octets. Une section SFC **neuve** n'est plus le texte `(* nom *)` mais un `<SFCProgram>` à une étape initiale. **Le format ne change pas** : une version d'avant lit, conserve et réexporte ces sections comme tout GRAFCET de Control Expert (lecture en texte seulement).

1.51.0 : une section **LD** (ladder) reste rangée en `<section>.xml`, le `<LDSource>` de Control Expert tel quel (`src/project/Ladder.hpp`). L'éditeur de ladder d'API Studio y écrit le même XML : seuls les **réseaux** (`networkLD`) modifiés sont réécrits, aux conventions du fichier lu (fins de ligne, tabulations ; contacts, bobines, `HLink`/`emptyCell` regroupés, `shortCircuit` et `VLink` pour les liaisons verticales, `FFBBlock` dans la suite de sa rangée avec `EN`/`ENO` écrits, `control` `jumpCoil`/`retCoil`, `labelCell`, `compareBlock`/`operateBlock`, `textBox`) ; un attribut ou un élément inconnu est gardé tel quel, un réseau non touché garde ses octets. Une section LD **neuve** n'est plus le texte `(* nom *)` mais un `<LDSource nbColumns="11">` à un réseau vide. **Le format ne change pas** : une version d'avant lit, conserve et réexporte ces sections comme tout ladder de Control Expert (lecture en texte seulement).

---

<a id="format-ihm"></a>
## 3. Format du projet IHM et numéros de format

- Le numéro de format courant est **`hmi::kFormatVersion = 33`** (`src/hmi/HmiStore.hpp`). La note de livraison 1.21.3 confirme que le format des projets ne change pas dans cette version.
- **Écriture « en escalier »** : un projet est écrit au plus petit format capable de porter ce qu'il contient (un projet sans espace de noms ni bibliothèque reliée s'écrit au format 32, et ainsi de suite).
- **Un outil plus ancien refuse un format plus récent** plutôt que de le relire en perdant ce qu'il ne connaît pas.
- Jalons principaux (commentaires de `HmiStore.hpp`) : 2 ressources ; 3 programmation générale ; 4 alarmes, recettes, sécurité ; 13 communication Modbus TCP, poste d'exploitation, accès web ; 14 équipements du réseau ; 15 types IHM ; 16 jumeaux simulés ; 19 dossiers ; 21 opérateurs et énumérations ; 22 groupes d'alarmes ; 23 déclarations des scripts ; 24 raccourcis des vues ; 25 gestes sur un tableau ; 26 constantes et fonctions des vues ; 27 types rappel ; 28 et 30 génériques ; 29 tests unitaires ; 31 types objets ; 32 unités de mesure ; 33 espaces de noms et bibliothèques reliées.
- 1.50.0 : les méthodes des types IHM ont les onglets des fonctions (Paramètres, Locales, Constantes, Tests). À l'ouverture, les blocs `VAR … END_VAR` encore écrits dans le corps d'une méthode passent dans ses déclarations (lignes `declaration` sous `methode_type`, lues depuis le format 31, comme ses cas de test) : **le format ne change pas** et une version d'avant relit le projet ; un corps illisible reste tel quel (`hmi::migrate::migrateMethods`).
- 1.50.1 : **les marges d'une vue** (`View::margins`, ce que garde « Étirer › Garder les marges ») s'écrivent sur la ligne `vue` du fichier de la vue — `marge_haut=16 marge_bas=16 marge_gauche=16 marge_droite=16`, en pixels de la vue — **seulement si la vue en a** (une au moins non nulle). Sans ces champs (un projet d'avant) : 0 partout, la vue n'a pas de marges à elle et Étirer garde, comme en 1.50.0, la marge de la grille de colonnes montrée, sinon le pas d'aimantation ; un tel projet se réécrit à l'identique. Une vue ou une popup neuve (Nouvelle vue, Nouvelle popup, la `Vue_Accueil` d'une IHM neuve) a 16 px de chaque côté, comme la maquette. **Le format ne change pas** : une version d'avant ignore ces champs sans rien dire (la vue s'y relit sans marges ; les marges sont perdues si elle réenregistre). « Garder les marges » n'est pas dans le projet : c'est un réglage de l'éditeur retenu par projet dans les réglages de l'utilisateur (`filtres.<empreinte du projet>.hmi.etirer = sans-marges` quand elle est décochée), comme les règles (`hmi.regles`).
- 1.50.1 : **les surcharges des méthodes** — plusieurs lignes `methode_type` du même nom sous un même `type` (chacune son `id`, ses lignes `declaration` : leurs paramètres les distinguent), dans l'ordre de la liste. **Le format ne change pas** : une version d'avant relit ces méthodes telles quelles, mais Générer y dit « méthode en double » et un appel y prend la première de ce nom. Les noms internes du moteur ne sont pas écrits (`METHODE_T_POMPE_REGLER`, puis `METHODE_T_POMPE_REGLER__<id>` pour une autre surcharge, calculés à la lecture).
- 1.51.0 : **les types IHM d'un symbole** (`HmiType::symbol`, `src/hmi/HmiSymbolTypes.hpp`) restent dans la liste des types de l'index (`ihm.txt`), sur une ligne `type_ihm` qui porte `espace=<nom du symbole>` et **`symbole=<id de la vue du symbole>`** (absent : un type du projet), suivie comme les autres de ses membres, valeurs, méthodes, opérateurs. Leur nom complet est `MonSymbole.T_Etat`. Un symbole qui en porte s'écrit au format 33 (celui des espaces de noms). **Le format ne change pas** : une version d'avant ignore `symbole=` et relit un type de l'espace de noms `MonSymbole` (rien ne se perd : les vues, les instances, les emplois `MonSymbole.T_Etat` se relisent ; sa Générer peut dire que `MonSymbole` est à la fois un espace et une vue, et le lien au symbole est perdu si elle réenregistre). À la lecture, un type dont le symbole manque (son fichier de vue absent) reste un type du projet de cet espace, avec un avertissement. Les paquets (`.xpgsymboles`, `.xpgvues`, le presse-papiers entre fenêtres) emportent les types d'un symbole avec lui ; la corbeille les met dans l'élément du symbole.
- Le manifeste `project.xpgproj` porte son propre `formatVersion` (valeur `1`).

---

<a id="archives"></a>
## 4. Archives `.xapi` et `.xihm`

`src/project/ProjectArchive.hpp` (base commune), `src/project/ApiArchive.hpp`, `src/hmi/HmiArchive.hpp` :

- un **zip** ouvrable par n'importe quel outil, avec en tête un `manifeste.txt` : genre, outil et version, projet, comptes par section, et pour chaque fichier sa taille, son empreinte **SHA-256** et sa date ;
- contenu : tout le dossier du projet enregistré ; fichiers externes liés hors du dossier (`_externes/`), disposition des grafcets posée à côté (`_a-cote/`), macros de la bibliothèque au choix (`_macros/`) ; cache de construction de l'IHM ;
- exclus : artefacts du build, `exports/`, `ihm/corbeille/`, fichiers temporaires, archives posées dans le dossier ; au choix : `versions/` et l'historique de marche ;
- **ouverture** : déballage dans un dossier neuf seulement, chemins sortant du dossier refusés, contrôle de cohérence (chaque fichier présent et intact, chaque compte du manifeste retrouvé dans le projet relu) ;
- sous Windows, l'installateur peut associer `.xihm` à IHM Studio et `.xapi` à API Studio (`HKCU` ou `HKLM\Software\Classes`, identifiants `APIIHMStudio.xihm`, `APIIHMStudio.xapi`, `installateur/XPGAnalyser.iss`).

---

<a id="versions"></a>
## 5. Versions, historiques, corbeille

| Mécanisme | Stockage | Code |
|---|---|---|
| Versions du projet (photos nommées, comparaison, restauration) | `versions/index.txt`, `versions/V0005.txt`, `versions/objets/xx/…` (contenu stocké une fois par empreinte SHA-256) | `src/hmi/HmiVersions.hpp` |
| Historique de la séance (annuler/rétablir, Ctrl+H) | en mémoire | `src/core/Command.hpp` |
| Historique des modifications des objets IHM (100 entrées par objet, signées) | `ihm/historique-edition/<id>.txt` | `src/hmi/HmiEditHistory.hpp` |
| Corbeille de l'IHM (vues et ressources retirées de l'index) | `ihm/corbeille/` | `src/hmi/HmiStore.hpp` |
| Corbeille du projet IHM (1.26.1 : ce que l'utilisateur supprime — vues, ressources et leurs octets, scripts, fonctions, variables, types, alarmes, recettes, styles, fichiers externes ; montrée par Projet › Corbeille, purgée après 7 jours) | `ihm/supprimes/corbeille.txt` (une ligne `element` par élément, `fin`) et `ihm/supprimes/NNNN/` (le contenu : un petit projet, `ihm.txt`, `vues/`, `ressources/`…) ; hors de `ihm.txt` (format inchangé), hors des versions | `src/hmi/HmiTrash.hpp`, `src/hmi/HmiStore.hpp` |
| Journaux d'exploitation de l'IHM (alarmes, mesures, audit) | `ihm/historique/` | moteur `src/hmi/HmiRuntime*` |

---

<a id="migrations"></a>
## 6. Migrations

| Migration | Ce qui se passe | Code |
|---|---|---|
| Projets de la 1.11 (programme et IHM ensemble) → 1.12 | Au premier lancement de chaque application, ses projets de la 1.11 sont **recopiés** dans `projets\api` ou `projets\ihm` (sa moitié seulement) ; les originaux ne bougent pas ; `.depuis-1.11.txt` liste ce qui a été copié. IHM Studio rend chaque IHM autonome (les noms de l'automate deviennent des variables IHM). | `src/app/EditionMigration.hpp`, `src/hmi/HmiStandalone.*` |
| Réglages de la 1.11 → un fichier par application | Le premier lancement d'une édition copie l'ancien `settings.txt` partagé. | `Settings::defaultPath()` |
| Blocs `VAR … END_VAR` des scripts → déclarations du modèle | Lus sans perte, convertis, retirés du code ; ce qui ne migre pas est expliqué. | `src/hmi/HmiMigrate.hpp` |
| Projets IHM d'anciens formats | Relus par le chargeur, puis réécrits au format nécessaire à l'enregistrement. Le projet modèle `bac/Armoire_Gaz` du dépôt est au format 19 et sert à plusieurs essais (`hmitrace`, `hmimigrate`, `edition`…) | `src/hmi/HmiStore.*` |
| Clé API d'avant 1.18 (`IA/cle.bin`) | Reprise dans le gestionnaire des clés au premier lancement | `src/app/ai/AiKeys.hpp` |
| Connaissances de l'IA format 1 → 2 (1.21.0) | Relues comme confirmées, réécrites au format 2 après une sauvegarde de toute la mémoire | `src/app/ai/AiKnowledge.hpp` |

---

<a id="emplacements"></a>
## 7. Réglages et emplacements sur le poste

| Quoi | Windows | Linux | Code |
|---|---|---|---|
| Réglages partagés (thèmes `themes/`, Mes images `images/`, Assistant IA `IA/`, `XPGAnalyser.ini`) | `%APPDATA%\XpgAnalyzer` | `$XDG_CONFIG_HOME/xpg-analyzer`, sinon `~/.config/xpg-analyzer` | `Settings::sharedFolder()` |
| Réglages d'une application (`settings.txt`, `crashs/`, `signalements/`, `reprise/`, `modeles-vues/`) | `%APPDATA%\XpgAnalyzer\API` ou `\IHM` | `~/.config/xpg-analyzer/API` ou `/IHM` | `Settings::defaultPath()`, `src/app/AboutDialog.cpp` |
| Dossier des données (installation) : `projets/` (`api/`, `ihm/`), `libs/`, `resources/`, `captures/` | `Documents\XPGAnalyser` par défaut, modifiable ; repli `%LOCALAPPDATA%\XpgAnalyzer\Donnees` si inaccessible | — | `src/app/Dossiers.*` |
| Version non installée (développement, portable) | dossier de travail : `projets/`, `libs/`, `resources/`, `captures/` | idem | `src/app/Dossiers.hpp` |
| Programme installé | `%LOCALAPPDATA%\Programs\API IHM Studio` (pour l'utilisateur) ou `Program Files\API IHM Studio` (tous les comptes) par défaut ; une mise à jour garde le dossier existant | — | `DossierProgrammeDefaut`, `installateur/XPGAnalyser.iss` |
| État, journaux, sauvegardes, réserve de l'installation | `%LOCALAPPDATA%\XpgAnalyzer` (pour l'utilisateur) ou `%ProgramData%\XPGAnalyser` (tous les comptes) | — | `DossierEtat`, `XPGAnalyser.iss` |
| Registre | `HKCU` ou `HKLM\Software\XPGAnalyser` (`InstallDir`, `Version`, `Portee`, `DossierDonnees`, `Profil`…) | — | `[Registry]` de `XPGAnalyser.iss` |

- `installation.ini`, posé à côté de l'exécutable par l'installateur, signale une version installée et donne les dossiers par défaut ; `XPGAnalyser.ini` (réglages partagés) les remplace clé par clé. Sans `installation.ini`, les dossiers suivent le dossier de travail. Détail : [CONFIG-INI.md](CONFIG-INI.md).
- Tous ces dossiers sont listés dans l'application (accueil › **Dossiers** ; **Projet › Dossiers de l'application…** ; **À propos**).
- 1.51.0, l'Admin (`%LOCALAPPDATA%\XPG\LicenseAdmin`, détail : [ADMINS.md](ADMINS.md) § 4 et 12) : `invitations-admins.json` (l'Admin+) gagne `droits` (les droits choisis à l'invitation), `admis` et `admis_le` (la clé signée d'office) ; une 1.50 les ignore (et les perd si elle réécrit le fichier : l'Admin+ retombe alors sur le profil par son nom). XPG Admin écrit `inscription-admin.json` (`xpg-inscription-admin/1` : le nom, le courriel, le profil de l'invitation, son id, l'heure) à la place de `demande-admin.json` (encore lu pour le courriel). Le service : `admins_invitations` gagne `par` (la clé de l'éditeur qui a créé l'invitation) ; la table `admins_demandes` garde son nom et ses colonnes (une sauvegarde d'avant se restaure telle quelle) et porte les inscriptions par code.
- 1.52.0, la dérogation (détail : [ADMINS.md](ADMINS.md) § 11) : le document signé par la clé de l'éditeur passe au **format 2** — `{"format":2,"id","droit":"tout","admin","nom","duree","valable","emise"}` (la durée, de 10 min à 72 h ; `valable` : l'heure limite pour taper le code, émise + 1 h), toujours précédé de `XPG-DEROGATION-1\n` ; **ni le code ni son empreinte n'y sont**. L'Admin+ envoie à côté `code` (l'empreinte du code) ; le service la garde dans la ligne `derogation` de sa table `config` (schéma 5 inchangé) et ne la rend jamais, la sauvegarde comprise (une dérogation restaurée en attente du code ne s'active plus). Le format 1 (un Admin+ d'avant) est toujours reçu. Un Admin 1.51 lit une dérogation en attente ou annulée sans planter : `debut` = `fin` (jamais « en cours ») ; un Admin+ 1.51 ne vérifie pas le document de format 2 (« ignorée »). Rien sur le poste ne change de format : le code n'est jamais écrit (l'Admin+ le retire de sa clé à chaque fois).

---

<a id="sauvegardes"></a>
## 8. Sauvegardes, réserve de réparation, reprise

| Mécanisme | Emplacement | Source |
|---|---|---|
| Sauvegarde de la version installée avant une mise à jour (3 gardées) | dossier d'état de l'installation | `outils/config.ini` (`[Maintenance] sauvegardes_a_garder = 3`), [GUIDE-INSTALLATION.md](GUIDE-INSTALLATION.md) |
| Réserve de réparation (copie des fichiers installés, option « Garder une copie de secours pour Réparer ») | `<état>\Reserve\<version>` ; celle de la version installée et de la précédente | `DossierReserve`, `XPGAnalyser.iss` |
| Reprise après arrêt brutal (modifications non enregistrées toutes les 2 min ; état du poste toutes les 10 s) | `<réglages de l'application>/reprise/` | `src/app/Recovery.hpp` |
| Cache de construction de l'IHM (copie `.bak` gardée) | `<projet>/.xpg/build/` | `src/hmi/HmiPipeline.hpp` |
| Sauvegardes de la mémoire de l'Assistant (10 gardées) | `IA/sauvegardes/` | `src/app/ai/AiMemoryManager.hpp` |
| Version « Avant restauration » créée avant toute restauration de version | `versions/` du projet | `src/hmi/HmiVersions.hpp` |

---

<a id="ia"></a>
## 9. Données de l'Assistant IA

Dossier `IA/` des réglages partagés : `parametres.json`, `conversations/`, `missions/` (1.22.0 : une mission IA par fichier, format 1, écrit d'un bloc avec une copie `.bak`), `pieces-jointes/`, `connaissances/`, `cles/` (`index.json` non secret, `<id>.bin` protégé par DPAPI sous Windows), `projets.json`, `notifications.json`, `usage.json`, `memoire.json`, `activite.jsonl`, `sauvegardes/`, `index/`, `journaux/`, `cache/`. Détail et règles : [ASSISTANT-IA.md](ASSISTANT-IA.md#donnees).

Les fichiers JSON de l'Assistant conservent l'ordre et les champs inconnus (`src/app/ai/AiJson.*`), portent un champ `format` pour les migrations, et sont écrits de façon atomique.

1.50.1 : un message de `conversations/<id>.json` peut porter **`reponseA`** {`id`, `auteur`, `extrait`} — la demande répond à ce message (« Répondre », `src/app/ai/AiChatReply.hpp`). Champ **facultatif** : un Studio 1.50.0 ou plus ancien le garde tel quel (champ inconnu) et montre la demande sans la citation ; le texte de la demande n'en contient pas de copie.

---

<a id="confidentialite"></a>
## 10. Confidentialité des données stockées

- **Clé API** : jamais en clair (ni dans `parametres.json`, ni dans un journal, ni dans un projet) ; protégée par DPAPI sous Windows ; gardée en mémoire seulement sous Linux (`src/app/ai/AiSecret.hpp`, `AiKeys.hpp`).
- **Mots de passe des projets et des utilisateurs de l'IHM** : empreintes salées, jamais en clair (`src/project/Security.hpp`, `src/hmi/HmiCrypto.*`). Le verrou LOCK n'est **pas** un chiffrement du dossier.
- **Dossier de l'IHM** (Word/PDF) et export `.xia` : n'incluent ni mots de passe, ni codes, ni badges, ni clé API.
- **Rapports de plantage et signalements** : écrits localement ; rien n'est envoyé par le réseau.
