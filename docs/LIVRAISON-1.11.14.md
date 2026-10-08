# XPGAnalyser 1.11.14 — le panneau du bas et IHM_LOG (lot 2)

Livrée le 08/10/2026 dans la soirée. C'est le **lot 2** de votre spécification du 08/10 : la fonction native `IHM_LOG` (§ 9 de votre texte) et le panneau inférieur de diagnostics (§ 10). Le lot 1 (1.11.13, la génération incrémentale) reste tel quel ; ses sorties passent dans ce panneau.

| Votre demande | Où dans la 1.11.14 |
|---|---|
| `IHM_LOG(niveau, 'message')` dans tous les scripts | les scripts généraux, de vue (OnOpen, OnCycle, OnClose), d'action, les fonctions IHM et celles des symboles (§ 2) |
| L'énumération TRACE, DEBUG, INFO, SUCCESS, WARNING, ERROR, CRITICAL | `INFO` seul, ou le littéral `NIVEAU_LOG#INFO` (§ 2) |
| Reconnue par l'analyse, l'autocomplétion, l'aide contextuelle ; le type du niveau et le message vérifiés | Compiler, l'aide à la saisie, F1 (§ 2) |
| Messages dynamiques `{Temperature:0.0}` | les trous des textes de l'IHM, formats compris (§ 2) |
| Horodatage, niveau, catégorie, source, script, vue ou objet, ligne, cycle ou session, lien vers la source | chaque ligne de la Console (§ 3) |
| Un panneau en bas, redimensionnable, Sorties et Console | sous les onglets du centre : Sorties, Console, Diagnostics ; Ctrl+J (§ 3) |
| Filtres par niveau, recherche, effacement, copie, export, pause, retour en bas, compteurs, double-clic, menu contextuel, historique configurable, session | la barre du panneau, le clic droit de la Console (§ 3) |
| Les couleurs des niveaux en thème sombre | gris, bleu, clair, vert, orange, rouge, rouge intense (§ 2) |

Fichiers livrés : `XPGAnalyser-Setup-1.11.14.exe` (l'installateur), ce document et les captures `11114_*` prises sous Wine avec l'exe livré.

Les lots suivants : **3** le cycle de la simulation (arrêt sur modification, Arrêter, Redémarrer, Générer et redémarrer) et la rémanence de simulation ; **4** la rémanence d'exploitation par variable, la robustesse restante, les tests restants et le rapport final en 9 parties.

## 1. Ce qui change pour vous

- **Un panneau en bas**, sous les onglets du centre, avec trois onglets :
  - **Sorties** : le déroulement des builds de l'IHM (lot 1), le démarrage et l'arrêt de la simulation (« Simulation démarrée (session 2) ») ;
  - **Console** : ce que disent les scripts (`IHM_LOG`), les erreurs d'exécution, un démarrage bloqué, les actions, la navigation, le démarrage et l'arrêt de l'IHM (« IHM arrêtée (bouton Arrêter l'IHM) ») et le moteur de l'IHM ;
  - **Diagnostics** : les erreurs et avertissements du dernier build.
- **Ctrl+J** montre ou replie ce panneau. On peut aussi passer par Affichage › Panneaux, case « Panneau du bas », ou par le bouton **Replier** du panneau. On le redimensionne en tirant son bord. Le choix et la taille sont retenus.
- L'onglet **IHM · Sorties** du lot 1 disparaît : son contenu est dans le panneau. « Voir les sorties » (fenêtre du build, clic droit de l'arbre, barre des éditeurs) ouvre le panneau. Un build en échec l'ouvre sur ses Diagnostics.
- **`IHM_LOG(niveau, 'message')`** s'écrit dans tous les scripts de l'IHM. Chaque appel met une ligne dans la Console : l'heure, le niveau, la source (le script, la vue, l'objet), la ligne du code et la session. Un double-clic sur la ligne ouvre le script, le curseur sur cette ligne.

## 2. IHM_LOG

```
IHM_LOG(INFO, 'Ouverture de la vue principale');
IHM_LOG(SUCCESS, 'Recette chargée avec succès');
IHM_LOG(WARNING, 'Valeur proche de la limite');
IHM_LOG(ERROR, 'Impossible de charger la ressource');
IHM_LOG(CRITICAL, 'Perte de communication avec l$'API');
IHM_LOG(INFO, 'Température actuelle : {Temperature:0.0} °C');
IHM_LOG(NIVEAU_LOG#DEBUG, 'Consigne {Consigne} ; mesure {Mesure:0.00}');
```

**Le niveau** est une valeur de l'énumération native `NIVEAU_LOG`. On l'écrit seul (`INFO`) ou en littéral (`NIVEAU_LOG#INFO`), sans distinction de casse, comme en ST :

| Niveau | Couleur dans la Console | Pour |
|---|---|---|
| TRACE | gris | le détail du déroulement |
| DEBUG | bleu | la mise au point |
| INFO | clair | ce qui se passe |
| SUCCESS | vert | ce qui a réussi |
| WARNING | orange | ce qui mérite attention |
| ERROR | rouge (ligne teintée) | ce qui a échoué |
| CRITICAL | rouge intense, en gras (ligne teintée plus fort) | ce qui met l'installation en cause |

**Le message** est un texte, ou une variable STRING. Ses trous `{Variable}` et `{Variable:format}` prennent la valeur du moment, avec les formats des textes de l'IHM (`0.0`, `0.##`, `0.0%`, `X4`, `t`, `Oui|Non`…). `{{` écrit une accolade.

**Où** : dans les scripts généraux, les scripts de vue (OnOpen, OnCycle, OnClose), les scripts d'action et les fonctions IHM, y compris celles des symboles. Dans une fonction appelée par une expression de vue, `IHM_LOG` est refusé, comme `IHM_JOURNAL` : une expression lit, elle n'écrit pas.

**Compiler vérifie** :
- qu'il y a 2 arguments (« IHM_LOG prend 2 arguments, pas 1 ») ;
- que le premier est un niveau. Sinon : « IHM_LOG : le premier argument est le niveau — TRACE, DEBUG, INFO, SUCCESS, WARNING, ERROR ou CRITICAL (trouvé : BLABLA) ». Un texte (`'INFO'`) n'est pas un niveau ;
- que le message n'est pas un nombre ni un booléen, et que les noms qu'il cite existent ;
- les trous du message. Un `{` sans `}` donne un avertissement : il s'écrirait tel quel.

Une variable qui porte le nom d'un niveau (une variable `Debug`) passerait avant lui, comme pour une valeur d'énumération. Compiler le signale et propose `NIVEAU_LOG#DEBUG`.

**L'aide à la saisie** :
- `IHM_LOG` figure dans la liste des fonctions, avec sa signature ;
- choisir `IHM_LOG` dans la liste écrit `IHM_LOG(` et ouvre aussitôt celle des sept niveaux, du plus bavard au plus grave ; si vous tapez `IHM_LOG(` vous-même, Ctrl+Espace l'ouvre ;
- choisir un niveau écrit `INFO, '` et place le curseur dans le message ;
- après `NIVEAU_LOG#`, la liste propose les niveaux ;
- dans le message, `{` propose les variables.

**F1** sur `IHM_LOG` ou `NIVEAU_LOG` ouvre la référence des fonctions IHM_, où `IHM_LOG` a sa ligne.

## 3. Le panneau du bas

La barre du panneau suit l'onglet choisi :

| Commande | Sorties | Console | Diagnostics |
|---|---|---|---|
| Filtres par niveau | Informations, Succès, Avertissements, Erreurs | TRACE … CRITICAL (sept boutons) | Informations … Erreurs |
| Recherche (le champ à droite) | oui | oui (message, source, catégorie) | oui |
| Effacer | les lignes | la Console | — |
| Copier | les lignes montrées | les lignes montrées | les diagnostics montrés |
| Exporter | un fichier texte | un fichier texte (le clic droit : aussi en CSV) | un fichier texte |
| Pause / En bas | le défilement automatique | le défilement automatique | — |
| Garder N lignes | — | 500, 1 000, 5 000 (par défaut), 20 000, 100 000 | — |
| Replier | le panneau (Ctrl+J le rouvre) | | |

- **La Console**, colonne par colonne : l'heure, le niveau (son icône et sa couleur), la source, la ligne du code, le message, la catégorie (IHM_LOG, Erreur, Action, Navigation, Journal, Simulation…), puis la session et le cycle (« S2 · c140 »). L'infobulle d'une ligne reprend tout, avec la date.
- **Les compteurs** : l'onglet Console porte les erreurs et les avertissements (« ✕ 2 ⚠ 5 »). La barre d'état du panneau dit la session, le nombre de lignes, ce qui est montré, ce qui est tombé, la conservation et si le défilement est en pause.
- **Le double-clic** sur une ligne de la Console ouvre sa source :
  - un script, le curseur sur sa ligne ;
  - une fonction IHM, le curseur sur sa ligne, ou le symbole et ses Fonctions ;
  - l'objet dans sa vue (une action) ;
  - la vue.

  Une erreur d'exécution garde aussi sa ligne (« ligne 2 : … »).
- **Le clic droit** de la Console : Aller à la source, Filtrer sur cette source, Copier la ligne (texte), Exporter en texte, Exporter en CSV (Excel), Effacer la console, puis les entrées habituelles (Copier, Tout choisir…).
- **La conservation** : au-delà de N lignes, les plus anciennes tombent. Le compte de celles qui sont tombées est gardé, et l'export le dit.
- **La session** : chaque démarrage de la simulation porte un numéro (1, 2, 3… depuis l'ouverture de l'application). Chaque ligne le garde, l'export sépare les sessions.

## 4. L'export

Dans `exports/` du projet, sous un nom daté : `console_2026-10-08_19-12-30.txt` ou `.csv`, `sorties_….txt`, `diagnostics_….txt`.

Le texte (celui de la session du § 5, abrégé) :

```
# XPGAnalyser - Console de la simulation IHM (2026-10-08)
# 49 ligne(s) gardée(s) ; tous les niveaux
# --- session 1 ---
19:54:53.838  INFO     operateur                    utilisateur de départ : operateur
19:54:53.839  INFO                                  IHM démarrée : 89 variable(s) IHM, 14 script(s) généraux
19:54:53.840  TRACE    script Init:5                Init : les variables sont prêtes
19:54:53.840  INFO     script Init:6                Ouverture de la vue principale
19:54:53.840  SUCCESS  script Init:7                Initialisation terminée
19:54:53.840  WARNING  script Init:8                Valeur proche de la limite
19:54:53.840  ERROR    script Init:9                Impossible de charger la ressource
19:54:53.840  CRITICAL script Init:10               Perte de communication avec l'API
…
19:54:57.516  DEBUG    script Horloge:2             Horloge : 2 s
```

Le CSV, séparé par des points-virgules et encodé en UTF-8 avec BOM, s'ouvre tel quel dans Excel. Ses colonnes : `Date;Heure;Niveau;Catégorie;Source;Ligne;Session;Cycle;Message`.

## 5. Le parcours vérifié sous Wine

Session `tools/sessions/session-11114-console.txt`, sur une copie d'Armoire_Gaz corrigée comme pour la 1.11.13, sans cache, avec l'exe livré :

| Étape | Ce qui s'est passé | Capture |
|---|---|---|
| 1. Le projet ouvert | le panneau du bas, sous les onglets du centre : Sorties, Console, Diagnostics | `11114_01` |
| 2. Sept `IHM_LOG` ajoutés au script Init (un par niveau, le dernier en `NIVEAU_LOG#CRITICAL`) et un dans Horloge (`'Horloge : {Secondes_IHM} s'`, une seconde sur deux) | Démarrer : le build les compile sans erreur, la simulation démarre (session 1) | — |
| 3. L'onglet Console | 44 lignes, dont 2 erreurs (ERROR et CRITICAL) et 1 avertissement, toutes de la session 1 ; chaque ligne a son heure, son niveau en couleur, sa source (« script Init », « script Horloge »), sa ligne (5 à 10 pour Init, 2 pour Horloge) | `11114_02` |
| 4. TRACE, DEBUG, INFO, SUCCESS et WARNING décochés | restent les deux lignes ERROR et CRITICAL ; la barre d'état : « (2 montrées) » | `11114_03` |
| 5. Double-clic sur « Valeur proche de la limite » | le script Init s'ouvre, le curseur sur la ligne 8 ; la barre d'état : « WARNING · script Init, ligne 8 : Valeur proche de la limite » | `11114_04` |
| 6. Clic droit sur une ligne | Aller à la source, Filtrer sur « Init », Copier la ligne, Exporter en texte, Exporter en CSV, Effacer la console | `11114_05` |
| 7. L'onglet Sorties | le build, puis « Simulation démarrée (session 1). » | `11114_06` |
| 8. Arrêter l'IHM, `IHM_LOG(BLABLA, 'x');` dans Horloge, Démarrer | 1 erreur : « IHM_LOG : le premier argument est le niveau — TRACE, DEBUG, INFO… (trouvé : BLABLA) » ; démarrage bloqué, le panneau s'ouvre sur ses Diagnostics ; la Console compte le démarrage bloqué parmi ses erreurs (✕ 3) | `11114_07` |
| 9. Ctrl+Z, puis Exporter dans la Console | `exports/console_2026-10-08_….txt` est écrit ; la barre d'état du panneau donne le chemin | `11114_08` |
| 10. Ctrl+J, puis Ctrl+J | le panneau se replie (« Panneau du bas replié (Ctrl+J le rouvre) »), puis revient | `11114_09` |
| 11. Dans Init, `IHM_L`, puis Entrée | `IHM_LOG(` est écrit, la liste des sept niveaux s'ouvre, la signature au-dessus | `11114_10` |

Le fichier exporté est celui du § 4.

## 6. Fichiers créés et modifiés

**Créés :**
- `src/hmi/HmiLog.hpp` et `.cpp` : l'énumération `NIVEAU_LOG`, ses noms, ses libellés et sa lecture (`INFO`, `NIVEAU_LOG#INFO`, sans casse) ;
- `src/app/hmi/HmiConsole.hpp` et `.cpp` : le modèle de la Console (les lignes, la conservation, les compteurs, le filtre, la recherche, l'export texte et CSV) ;
- `tests/hmi_log_test.cpp` : 40 contrôles ;
- `tools/sessions/session-11114-console.txt` : la session Wine du § 5 ;
- ce document.

**Modifiés :**
- `src/hmi/HmiRuntime` : `IHM_LOG` ; chaque ligne du journal garde son niveau, son code, sa vue, son objet, son script, sa fonction, sa ligne, son cycle et sa session ; une erreur d'exécution garde sa ligne ; l'arrêt dit sa raison dans une seule ligne ;
- `src/hmi/HmiScriptCheck.cpp` : Compiler vérifie les appels à `IHM_LOG` ;
- `src/hmi/HmiScript.cpp` : les niveaux ne sont pas pris pour des variables (la validation les signalait comme « variable inexistante ») ;
- `src/hmi/HmiGuideText.cpp` : la ligne d'`IHM_LOG` dans la référence, F1 ;
- `src/app/hmi/HmiAssist` : l'aide à la saisie (la signature, les niveaux après `IHM_LOG(` et après `NIVEAU_LOG#`) ;
- `src/app/hmi/HmiBuildPanes` : le panneau du bas (onglets, barre, Console, export) remplace l'onglet IHM · Sorties ;
- `src/app/hmi/HmiSimulation` : la Console reçoit les lignes du moteur ; le début et la fin de chaque session vont dans les Sorties ; un démarrage bloqué est une erreur de la Console ;
- `src/app/screens/MainAnalysisScreen.cpp`, `HmiBuildWorkspace.cpp`, `HmiWorkspace.cpp`, `ExplorerMenus.cpp`, `HistoryWorkspace.cpp` et `Screens.hpp` : la colonne du centre et son panneau, Ctrl+J, le double-clic vers la source ;
- `src/ui/widgets/DataViews` : un tableau peut ajouter ses entrées en tête de son clic droit ;
- `src/platform/InputEvent.hpp`, `SdlEventPump.cpp` et `src/app/tutorial/UiDriver.cpp` : la touche J ;
- `src/app/ScriptRunner.cpp` : les commandes de session `ihm-sorties console` et `ihm-console-etat` ; `outil` et `ligne` cherchent aussi dans le panneau ;
- `src/help/Shortcuts.cpp` : Ctrl+J dans la liste des raccourcis ;
- la version et ses fichiers : `src/core/Version.hpp`, `src/hmi/HmiPackage.hpp`, `src/help/ReleaseNotes.cpp`, `src/core/CodeStats.hpp`, `CMakeLists.txt`, `installateur/XPGAnalyser.iss`, `outils/config.ini`, `resources/windows/xpg_analyzer.rc`, `tests/hmi_editor_test.cpp` et `dist/SHA256SUMS.txt`.

## 7. Les tests ajoutés

`tests/hmi_log_test.cpp` (test CTest `hmilog`) : **40 contrôles, 0 échec**.

| Votre cas | Le test |
|---|---|
| l'énumération : les sept niveaux, leurs noms, `NIVEAU_LOG#X`, sans casse | `niveaux` |
| un appel valide à `IHM_LOG` ; un appel invalide (niveau inconnu, texte au lieu du niveau, nombre au lieu du message, un argument de moins, un `{` sans `}`, un nom inconnu dans le message, une variable qui porte le nom d'un niveau) | `verification` |
| Générer et compiler un projet qui appelle `IHM_LOG` : pas de fausse erreur | `verification` |
| `IHM_LOG` dans un script de vue, un script général et une fonction IHM : le niveau, le message et ses trous, la source, la ligne, la session | `execution` |
| une erreur d'exécution garde sa ligne ; la Console reçoit tout ce que reçoit le journal | `execution` |
| la conservation, les compteurs, le filtre, la recherche, l'export texte et CSV, Effacer | `console` |

`tests/hmi_editor_test.cpp` vérifie aussi l'aide à la saisie : `IHM_LOG` dans la liste des fonctions, les sept niveaux dans l'ordre après `IHM_LOG(`, puis après `NIVEAU_LOG#`.

La suite complète passe : **57 tests sur 57** (Linux, GCC 13).

## 8. Les limites de ce lot

- **Le panneau ne garde pas ses lignes d'une ouverture de l'application à l'autre** : la Console vit le temps de l'application. Pour garder une trace, utilisez Exporter.
- **Une expression de vue ne peut pas appeler `IHM_LOG`**, comme `IHM_JOURNAL` : une expression lit, elle n'écrit pas.
- **Une modification pendant que la simulation tourne** ne l'arrête pas encore, et la rémanence n'est pas encore là : ce sont les lots 3 et 4. Le panneau est prêt à recevoir leurs lignes : arrêt, sauvegarde et restauration de la rémanence.
- Rien n'a encore été lancé sur un **vrai Windows** : tout a été vérifié sous Wine.

## 9. Tester vous-même

1. Installez la 1.11.14, puis ouvrez une **copie** d'Armoire_Gaz (corrigée comme au § 6 de la livraison 1.11.13, ou avec vos corrections).
2. Le panneau du bas est là, sous les onglets du centre. Ctrl+J le replie, puis le rouvre. Tirez son bord pour le grandir.
3. Dans le script général Init, ajoutez :
   `IHM_LOG(INFO, 'Ouverture de la vue principale');`
   `IHM_LOG(WARNING, 'Valeur proche de la limite');`
   Tapez `IHM_L`, puis Entrée : la liste propose les sept niveaux.
4. Dans un script cyclique, ajoutez `IHM_LOG(DEBUG, 'Horloge : {Secondes_IHM} s');`
5. Démarrer l'IHM, puis l'onglet **Console** : les lignes arrivent, avec l'heure, le niveau en couleur, la source (« script Init »), la ligne du code et la session (« S1 »).
6. Cliquez sur **TRACE**, **DEBUG**, **INFO**, **SUCCESS** et **WARNING** dans la barre : il ne reste que les erreurs. Recliquez-les.
7. Double-cliquez sur « Valeur proche de la limite » : le script Init s'ouvre, le curseur sur la ligne de cet appel.
8. Clic droit sur une ligne : Filtrer sur cette source, puis Exporter en CSV (Excel). Le fichier est dans `exports/` du projet.
9. Arrêtez l'IHM, écrivez `IHM_LOG(BLABLA, 'x');` dans un script, puis Démarrer : le démarrage est bloqué, l'onglet Diagnostics dit pourquoi. Ctrl+Z, puis Démarrer : la simulation repart (session 2).
10. L'onglet **Sorties** : le build, puis « Simulation démarrée (session 2) ».

## Installer

1. Lancez `XPGAnalyser-Setup-1.11.14.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.14.exe` (14,9 Mo, 15 649 526 octets) :
`B20EDD5DD4E87534C3F473F025C359D3EBD71E5C010F1D9AECF6B0746CE5DC6E`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (57 sur 57), dont les 40 contrôles de `hmilog`.

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.14` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session du § 5 a été rejouée avec l'exe livré, sur une copie d'Armoire_Gaz sans cache. Ce sont les 10 captures `11114_*.png`.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé pour tous les comptes, par-dessus la 1.11.13 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.14`.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Le § 9 donne le parcours à refaire chez vous.

## La suite

- **Lot 3** : le cycle de la simulation (arrêt sur modification, Arrêter, Redémarrer, Générer et redémarrer) et la rémanence de simulation, avec son option et sa confirmation. Ses lignes iront dans les Sorties (arrêt, sauvegarde et restauration de la rémanence).
- **Lot 4** : la rémanence d'exploitation par variable, la robustesse restante, les tests restants de votre § 19 et le rapport final en 9 parties.
