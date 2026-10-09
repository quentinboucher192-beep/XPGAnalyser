# IHM Simulation : rapport final (spécification du 08/10/2026)

Ce rapport clôt votre spécification « IHM Simulation » : génération incrémentale, compilation, diagnostics, journalisation et rémanence. Elle a été livrée en quatre lots :

| Lot | Version | Contenu |
|---|---|---|
| 1 | **1.11.13** | Le moteur de build incrémental : cache, dépendances, artefacts, 13 états et leurs icônes, Générer / Régénérer / Compiler partout, fenêtre de progression, démarrage par le build. |
| 2 | **1.11.14** | Le panneau du bas (Sorties, Console, Diagnostics) et `IHM_LOG` dans tous les scripts. |
| 3 | **1.11.15** | Le cycle de la simulation (Démarrer, Arrêter, Redémarrer, Générer et redémarrer), l'arrêt sur modification, la rémanence de simulation. |
| 4 | **1.11.16** | La rémanence d'exploitation variable par variable, la robustesse restante, les tests restants du § 19, ce rapport. |

Le détail de chaque lot est dans `docs/LIVRAISON-1.11.13.md` à `docs/LIVRAISON-1.11.16.md`. Ce document reprend les 9 points demandés à la fin de votre § 20, pour l'ensemble des quatre lots.

## Les critères d'acceptation (§ 20)

| Critère | Tenu | Comment, et où c'est vérifié |
|---|---|---|
| Le démarrage ne reconstruit pas un projet à jour | oui | « Projet à jour » : 0 tâche, tout réutilisé (`hmi_build_test` : *premiereGenerationEtDemarrageSansModification* ; session 11113). |
| Seules les modifications et leurs dépendances sont régénérées | oui | empreintes contenu / configuration / interface, dépendances d'interface ou de contenu (§ 6 ; `hmi_build_test` : un script, une variable API citée par deux vues, une fonction). |
| L'ordre API puis IHM est respecté | oui | la progression montre l'API finie avant que l'IHM commence (`hmi_build_test`). |
| Chaque script a Générer, Régénérer et Compiler | oui | la barre des éditeurs de scripts et de fonctions (F7 : Compiler), le clic droit de chaque élément de l'arbre. |
| Les menus contextuels sont présents et adaptés | oui | une entrée grisée dit pourquoi (« aucune valeur gardée pour elle », « projet jamais enregistré »…). |
| Les états et icônes changent tout de suite après une modification | oui | l'analyse est refaite 300 ms après une modification, sans rien construire (« le script modifié est Modifié dans l'arbre, tout de suite »). |
| `IHM_LOG` fonctionne dans tous les scripts | oui | généraux, de vue, d'action, fonctions IHM et de symbole (`hmi_log_test`, session 11114). |
| Les onglets Sorties et Console sont fonctionnels | oui | filtres, recherche, export texte et CSV, pause, conservation (session 11114). |
| Un double-clic sur une erreur ouvre sa source | oui | Diagnostics, Sorties et Console (`hmi_editor_test` : *sources1116* ; session 11116, étape 6 : le script ouvert à sa ligne). |
| Une modification pendant la marche arrête proprement la simulation | oui | la règle est dans le moteur (`pl::runChange`, essayée) ; la carte et ses quatre boutons (session 11115). |
| Générer et redémarrer garde la rémanence de simulation | oui | `hmi_editor_test` : *cycle1115* ; session 11115. |
| Redémarrer efface la rémanence de simulation après confirmation | oui | la question ; Annuler ne touche à rien (*cycle1115*). |
| Désactiver la rémanence réinitialise au démarrage suivant | oui | *cycle1115* ; session 11115. |
| Les variables « Rémanentes » sont sauvegardées seulement en exploitation réelle | oui | le poste d'exploitation seul ; la simulation de l'éditeur n'y écrit jamais (*remanence1116*). |
| Rémanence de simulation et d'exploitation totalement séparées | oui | deux fichiers, deux formats, deux en-têtes, deux dossiers (§ 5). |
| Un build ou une sauvegarde interrompus ne corrompent pas le dernier état valide | oui | écritures d'un bloc, ligne `fin` comptée, copie `.bak` reprise (§ 4, § 5 ; tests de coupure). |
| Aucun faux bouton, faux journal ou faux état de progression | oui | la progression compte les vraies tâches ; un élément n'est jamais marqué généré ou compilé si l'opération a échoué ; les entrées impossibles sont grisées avec leur raison. |
| Le projet compile sans erreur | oui | Linux (GCC) et Windows (MinGW-w64), sans erreur. |
| Les fonctionnalités existantes continuent de fonctionner | oui | les 59 suites de tests passent (5 933 contrôles dans `hmi_editor_test` seul). |

## 1. Les fichiers créés

**Les services :**
- `src/core/AtomicFile.hpp` et `.cpp` : l'écriture d'un fichier d'un bloc (temporaire, relu, ancienne version en `.bak`, remplacement) et la lecture d'un fichier entier.
- `src/hmi/HmiPipeline.hpp` et `.cpp` : le moteur de build (collecte des éléments, empreintes, dépendances, analyse, plan, génération de l'API puis de l'IHM, compilation, validation, cache, artefacts, verrou, règle de l'arrêt sur modification).
- `src/app/hmi/HmiBuild.hpp` et `.cpp` : le gestionnaire de build (un build à la fois dans un fil à part, sur une copie du projet ; analyse en continu ; annulation ; états de l'arbre).
- `src/app/hmi/HmiBuildPanes.hpp` et `.cpp` : la fenêtre de progression, la question avant Redémarrer, le panneau du bas (Sorties, Console, Diagnostics).
- `src/app/screens/HmiBuildWorkspace.cpp` : le branchement dans l'écran (l'arbre, les menus, les barres, le démarrage par le build, l'arrêt sur modification, les commandes de session).
- `src/hmi/HmiLog.hpp` et `.cpp` : les niveaux de `IHM_LOG` et de la Console.
- `src/app/hmi/HmiConsole.hpp` et `.cpp` : la Console (les lignes, les filtres, la conservation, l'export).
- `src/hmi/HmiSimData.hpp` et `.cpp` : la rémanence de simulation.
- `src/hmi/HmiRetain.hpp` et `.cpp` : la rémanence d'exploitation.

**Les tests :**
- `tests/hmi_build_test.cpp`, `tests/hmi_log_test.cpp`, `tests/hmi_simdata_test.cpp` et `tests/hmi_retain_test.cpp`.

**Les sessions jouées sous Wine** (`tools/sessions/`) :
- `session-11113-build-incremental.txt`, `session-11113a-demarrage-bloque.txt`, `session-11113c-fenetre-echec.txt` et `session-11113d-demarrer-reussi.txt` ;
- `session-11114-console.txt` ;
- `session-11115-cycle.txt` ;
- `session-11116-remanence.txt` et `session-11116b-relance.txt`.

**Les documents :**
- `docs/LIVRAISON-1.11.13.md` à `docs/LIVRAISON-1.11.16.md` ;
- ce rapport.

## 2. Les fichiers modifiés

| Fichier | Ce qui a changé |
|---|---|
| `src/hmi/HmiModel.hpp` | `Variable::retain` (« Rémanente ») ; les champs de journal (niveau, code, ligne, cycle, session). |
| `src/hmi/HmiStore.cpp` | la case Rémanente lue et écrite dans le projet (`remanente=`). |
| `src/hmi/HmiRuntime.hpp` et `.cpp` | `IHM_LOG` ; le journal enrichi ; la prise et le retour des données (`captureData`, `setStartData`, `applyData`, `lastRestore`). |
| `src/hmi/HmiCheck`, `HmiScriptCheck`, `HmiExprCheck`, `HmiScript.cpp` | Compiler limité à des éléments ; la vérification de `IHM_LOG` ; les niveaux ne sont pas des variables inexistantes. |
| `src/hmi/HmiGuideText.cpp`, `HmiVersions.cpp` | l'aide de `IHM_LOG` ; le dossier `.xpg/` hors des versions. |
| `src/hmi/HmiPackage.hpp` | la version qui écrit les paquets. |
| `src/app/hmi/HmiSimulation.hpp` et `.cpp` | le démarrage par le build, Démarrer / Arrêter / Redémarrer / Générer et redémarrer, la rémanence de simulation, l'arrêt sur modification et sa carte, la rémanence d'exploitation du poste (capture, écriture regroupée, verrou, reprise). |
| `src/app/hmi/HmiVariablePanes.hpp` et `.cpp` | la colonne Rémanente, la fiche « Rémanence (exploitation) », les six commandes du clic droit, l'export et l'import. |
| `src/app/hmi/HmiScriptPanes`, `HmiFunctionPanes` | Générer, Régénérer, Compiler (F7) et l'état de l'élément dans la barre ; aller à la source montre l'onglet Scripts généraux (1.11.16). |
| `src/app/hmi/HmiAssist` | l'aide à la saisie de `IHM_LOG` et de `NIVEAU_LOG#`. |
| `src/app/screens/HmiWorkspace.cpp`, `MainAnalysisScreen.cpp`, `ExplorerMenus.cpp`, `HmiRuntimeDialogs.cpp`, `HistoryWorkspace.cpp`, `Screens.hpp` | l'état de build dans l'arbre, les menus contextuels, le panneau du bas, la question avant Redémarrer, les hôtes de la rémanence. |
| `src/app/screens/StationScreen.cpp` | le stockage du poste ; la reprise d'un arrêt brutal ne fait plus reculer une variable rémanente. |
| `src/app/ScriptRunner.hpp` et `.cpp` | les commandes de session (`ihm-build…`, `ihm-sorties`, `ihm-console-etat`, `ihm-remanence`, `ihm-sim-etat`, `ihm-variable`, `ihm-carte`, `varihm remanente/remanence-…/fiche`, `ihm-variable-ecrire`, `ihm-remanence-poste`, `ihm-curseur`). |
| `src/app/ViewModels`, `src/app/tutorial/UiDriver.cpp` | l'état de build dans l'arbre ; les gestes des sessions. |
| `src/ui/widgets/DataViews` | la colonne d'état de l'arbre ; les tables du panneau du bas. |
| `src/platform/AtlasSymbols.hpp`, `FontAtlas.cpp`, `SdlRenderer.cpp`, `SdlEventPump.cpp`, `InputEvent.hpp` | les 9 glyphes des états dessinés par l'atlas ; Ctrl+J. |
| `src/help/ReleaseNotes.cpp`, `Shortcuts.cpp` | les notes des quatre versions ; le raccourci du panneau. |
| `tests/hmi_editor_test.cpp` | le cycle dans le volet, la rémanence d'exploitation dans le poste et l'éditeur, le double-clic vers la source. |
| `CMakeLists.txt`, `src/core/Version.hpp`, `src/core/CodeStats.hpp`, `resources/windows/xpg_analyzer.rc`, `installateur/XPGAnalyser.iss`, `outils/config.ini`, `dist/SHA256SUMS.txt` | les tests ajoutés, la version, l'empreinte de l'installateur. |

En tout : 83 fichiers, environ 20 300 lignes ajoutées et 1 450 retirées, pour les quatre lots.

## 3. Le rôle de chaque service

| Service | Où | Rôle |
|---|---|---|
| **Le moteur de build** (BuildService, IncrementalBuildEngine, DependencyGraph, BuildCache, ArtifactManager de la spécification) | `hmi::pipeline` | Il collecte les éléments (API puis IHM), calcule leurs empreintes et leurs dépendances, compare au cache, décide quoi refaire, génère, compile, valide, écrit le cache et les artefacts d'un bloc, tient le verrou `build.lock`, et dit la règle de l'arrêt sur modification. Il ne connaît pas l'écran. |
| **Le gestionnaire de build** (BuildManager) | `app::HmiBuildManager` | Il lance un build à la fois dans un fil à part sur une copie du projet, sans geler l'interface. Il refait l'analyse 300 ms après une modification, annule sur demande et donne l'état de chaque élément et de chaque dossier. |
| **Les diagnostics** (DiagnosticsService) | `pl::Diagnostic`, panneau Diagnostics | Chaque remarque dit son code, l'élément, son chemin, la ligne, l'étape et ce qu'il faut pour aller à la source. Celles qui bloquent empêchent le démarrage. |
| **Le journal** (LogService) | `app::HmiConsole`, `hmi::LogLevel` | Les lignes de `IHM_LOG` et du moteur IHM : niveau, source, ligne, cycle, session, filtres, conservation, export. |
| **Les Sorties** (OutputService) | `HmiBuildOutputPane` | Le récit du build, du démarrage et de l'arrêt de la simulation. |
| **La rémanence de simulation** (SimulationPersistenceService) | `hmi::simdata` | Elle garde les variables IHM et la mémoire des esclaves simulés, puis les rend par identifiant stable. |
| **La rémanence d'exploitation** (RuntimePersistenceService) | `hmi::retain` | Elle garde les variables cochées « Rémanente » sur le poste. Le module a son propre format, son verrou, son import et son export (CSV pour Excel), sa vérification d'intégrité et l'état d'une variable pour l'éditeur. |
| **Les écritures atomiques** | `core::writeFileAtomic` | Cache, artefacts, rémanences et exports sont écrits d'un bloc, avec la copie de l'ancien. |
| **Le cycle de la simulation** (SimulationController) | `app::HmiSimulationPane` | Démarrer par le build, Arrêter, Redémarrer, Générer et redémarrer, l'arrêt sur modification et sa carte ; sur le poste, la capture regroupée des variables rémanentes. |

## 4. Le format du cache de build

Le cache est dans `<projet>/.xpg/build/build-cache.txt`. C'est du texte, au format des fichiers IHM : une ligne par enregistrement, `mot clé=valeur`, les textes entre guillemets.

```
# XPGAnalyser - cache de build de l'IHM (ne pas modifier a la main)
cache_build format=1 generateur="1" ecrit="2026-10-08 18:16:20"
element cle="script:69" genre="script" chemin="IHM/Programmation générale/Scripts/Horloge" contenu=c77b6a7d4350759c config=14650fb0739d0383 dependances=8a882f59d897c78a interface=db1da8d70a082e61 echec="" valide="2026-10-08 18:16:20" genere="2026-10-08 18:16:20" compile="2026-10-08 18:16:20" generateur="1" generation=genere compilation=compile artefact="ihm/05-scripts_generaux/script_69-Horloge.txt" empreinte_artefact="24de58c024ecfbbb"
dependance de="script:69" vers="variable:67" empreinte=a3407b90546b41fb signature="Secondes_IHM : DINT"
diagnostic de="vue:107" gravite="Avertissement" code="Lisibilité" … etape="Validation" … vue=107 objet=117 … fichier="ihm/vues/0107-Vue_Supervision.vue"
…
fin
```

- **`cache_build`** : le format (1) et la version du générateur.
- **`element`** : un élément du build avec :
  - sa clé, son genre et son chemin dans l'arbre ;
  - ses quatre empreintes (FNV-1a 64 bits) : contenu, configuration, dépendances, interface ;
  - `echec` : l'empreinte du code qui a échoué, vide si aucun échec ;
  - ses dates : dernier build valide, génération, compilation ;
  - son état de génération et de compilation ;
  - son artefact et l'empreinte de celui-ci.
- **`dependance`** : ce que l'élément cite, avec l'empreinte et la signature lisible vues au dernier build. C'est ce qui permet de dire « Pression_Gaz : REAL → LREAL ».
- **`diagnostic`** : une remarque du dernier build, avec ce qu'il faut pour aller à la source (vue, objet, script, propriété, fichier, ligne).
- **`fin`** : la dernière ligne. Un cache qui ne finit pas par `fin` est refusé.

**Les artefacts.** Ils sont rangés dans `.xpg/build/api/NN-étape/` et `.xpg/build/ihm/NN-étape/<clé>-<nom>.txt`, par exemple `ihm/08-vues/vue_1-Vue_Accueil.txt`. Chacun contient la forme générée de l'élément (une vue : ses instances de symboles dépliées), avec son en-tête (clé, empreintes, dépendances), et finit par `fin`. Les noms sont en ASCII. Le dossier `.xpg/` n'entre pas dans les versions du projet.

**Comment ils sont écrits** (cache et artefacts) :
1. un fichier `.tmp` ;
2. relu et comparé ;
3. l'ancien copié en `.bak` ;
4. le `.tmp` renommé.

**Le verrou.** `build.lock` contient le PID et l'heure de l'instance qui construit. Une seconde instance ne construit pas en même temps, et le dit. Un verrou de plus de 10 minutes est repris.

## 5. Le format des fichiers de rémanence

Les deux rémanences sont **séparées** : deux dossiers, deux formats, deux en-têtes, deux diagnostics. Aucune ne lit le fichier de l'autre : chaque lecture vérifie l'en-tête.

### A. La rémanence de simulation (le développement)

- **Le fichier** : `<projet>/.xpg/simulation/remanence.txt`.
- **L'en-tête** : `xpg-simulation-remanence 1`.
- **Qui l'écrit** : l'onglet Simulation de l'éditeur, quand l'option Rémanence est cochée :
  - à Arrêter ;
  - à Générer et redémarrer ;
  - à l'arrêt sur modification ;
  - en quittant l'application en marche (les variables seulement).
- **Qui l'efface** : Redémarrer, après la question.

```
xpg-simulation-remanence 1
# XPGAnalyser : les donnees de la simulation IHM (remanence de simulation). Ecrit par l'application.
prise date="2026-10-08 20:49:37" session=4
case variable=67 nom="Secondes_IHM" declare="DINT" chemin="" type=DINT valeur="3"
case variable=618 nom="Fours" declare="ARRAY[1..4] OF T_Four" chemin="[1].Consigne" type=REAL valeur="850"
jumeau equipement=589 nom="Balance B" table=4x debut=200 valeurs="65282,16576"
fin cases=204 jumeaux=7
```

- `prise` : quand, et la session.
- `case` : une case d'une variable IHM :
  - son identifiant stable (`variable`) ;
  - son nom et son type déclaré au moment de la prise ;
  - le chemin de la case (vide pour une variable simple) ;
  - le type de la valeur et la valeur.
- `jumeau` : une plage de la mémoire d'un esclave simulé.
- `fin` : le compte des lignes. Un autre compte, ou pas de `fin`, et le fichier est refusé ; la copie `.bak` est alors reprise.

### B. La rémanence d'exploitation (le poste)

- **Le fichier** : `<projet>/ihm/historique/remanence_exploitation.txt`, à côté des historiques du poste.
- **L'en-tête** : `xpg-remanence-exploitation 1`.
- **Qui l'écrit** : le poste d'exploitation seul, pour les variables cochées « Rémanente » :
  - dès qu'une valeur change (capture toutes les 0,25 s, au plus une écriture par seconde) ;
  - à l'arrêt ;
  - en quittant le poste.

  La simulation de l'éditeur n'y écrit jamais. Une génération ou une compilation n'y touche pas.

Le fichier qu'a écrit le poste dans la session 11116 A (une copie d'Armoire_Gaz) :

```
xpg-remanence-exploitation 1
# XPGAnalyser : les variables remanentes du poste d'exploitation. Ecrit par le poste.
poste projet="Armoire_Gaz" date="2026-10-09 00:30:37"
valeur variable=67 nom="Secondes_IHM" declare="DINT" chemin="" type=DINT valeur="5" date="2026-10-09 00:30:37"
valeur variable=201 nom="Vitesse_Pompe" declare="REAL" chemin="" type=REAL valeur="72.5" date="2026-10-09 00:30:25"
valeur variable=202 nom="Consigne_Pression" declare="REAL" chemin="" type=REAL valeur="3.75" date="2026-10-09 00:30:25"
fin valeurs=3
```

Après la relance (session B), Secondes_IHM est passée à 6, et Consigne_Pression, réinitialisée, est revenue à 2.5. Vitesse_Pompe, qui n'a pas changé, a gardé sa date (00:30:25).

- `poste` : le projet et la date de la dernière écriture du fichier.
- `valeur` : une case d'une variable rémanente, comme une `case` de la simulation, plus la **date de sa dernière sauvegarde** : une valeur qui ne change pas garde sa date.
- `fin` : le compte des valeurs. Un fichier coupé ou abîmé est refusé, et sa copie `.bak` est reprise.

**Le verrou.** `remanence_exploitation.lock` est à côté du fichier. Il contient `verrou pid=… depuis="…"`. Le poste qui écrit le tient : il le rafraîchit en marche et le rend à l'arrêt. Un second poste lancé sur le même projet lit les valeurs mais ne les écrit pas. Il le dit, réessaie toutes les 30 s, et prend la relève quand le premier s'arrête. Un verrou que personne n'a rafraîchi depuis 3 minutes (un poste arrêté brutalement) est repris.

**L'export pour Excel** (clic droit, « Exporter les valeurs rémanentes… », un fichier `.csv`). Il est écrit en UTF-8 avec BOM, séparé par `;`. Les nombres à virgule y sont écrits avec une virgule. Un texte qui commencerait par `=`, `+`, `-` ou `@` est précédé d'une apostrophe, pour qu'Excel n'en fasse pas une formule.

L'export de la session B (précédé du BOM UTF-8, invisible) :

```
Variable;Chemin;Type;Valeur;Date;Type déclaré;Id
Secondes_IHM;;DINT;5;2026-10-09 00:30:37;DINT;67
Vitesse_Pompe;;REAL;72,5;2026-10-09 00:30:25;REAL;201
Consigne_Pression;;REAL;3,75;2026-10-09 00:30:25;REAL;202
```

L'import relit ce CSV même modifié dans Excel :
- les colonnes sont reconnues par leur titre, dans n'importe quel ordre ;
- le séparateur peut être `;`, `,` ou une tabulation, ou être donné par une ligne `sep=;` ;
- les nombres peuvent avoir une virgule ou un point ;
- les booléens peuvent s'écrire VRAI/FAUX, TRUE/FALSE, oui/non ou 1/0.

L'import relit aussi le format du poste. Une valeur va à sa variable par son identifiant si le nom concorde, sinon par son nom : un CSV sans identifiant, ou venu d'un autre projet, fonctionne donc. Les variables inconnues, non rémanentes ou liées sont écartées, et le compte rendu le dit.

## 6. Les règles exactes d'invalidation

### Le build (lot 1)

Chaque élément a trois **empreintes** :
- **contenu** : ce qui s'exécute ou s'affiche ;
- **configuration** : taille, fond, réglages de popup… ;
- **interface** : ce que les autres en voient (un nom et un type, une signature).

**Ce qui fait refaire un élément**, et l'état qu'il prend :
- **Modifié** : son contenu ou sa configuration a changé ;
- **Renommé** : il a été renommé ou déplacé ;
- **Génération requise** : il n'a jamais été généré, ou son artefact manque ou a été altéré ;
- **En échec** : son dernier build a échoué ;
- **Obsolète** : le générateur a changé de version.

**La documentation n'entre dans aucune empreinte** (description, commentaire de variable, dossier, grille, guides) : la changer ne refait rien. Un commentaire *dans le code* d'un script fait partie du code.

**Dépendance d'interface.** Un script cite une variable, une fonction, un type ou une variable de l'API ; une animation cite une variable ; une action vise une vue. Le dépendant n'est refait que si l'**interface** change :
- le corps d'une fonction change : la fonction seule est refaite ;
- sa signature change : ses appelants aussi ;
- une variable de l'API change d'adresse : elle seule ;
- elle change de type : ce qui la cite, et rien d'autre.

**Dépendance de contenu.** Une vue et son modèle, une vue et les symboles qu'elle pose, un type et ses membres de type IHM : tout changement du dépendu refait le dépendant.

**Un élément cité mais introuvable** (un symbole supprimé, une ressource absente) met le dépendant en **Dépendance invalide**, avec un code clair (E120 à E132). **Un élément en échec** rend ses dépendants « Dépendance invalide » jusqu'à sa correction. **Un élément supprimé** quitte le cache, et son artefact est retiré.

**Un cache absent, coupé, illisible ou d'un autre format** : tout est régénéré, et le journal dit pourquoi.

**Ce qui bloque le démarrage** :
- une erreur de génération ou de compilation ;
- une erreur de validation de structure ;
- une ressource manquante ;
- une incohérence entre l'API et l'IHM.

Le reste est dit en avertissement.

### L'arrêt sur modification (lot 3)

- Au démarrage, l'**empreinte d'exécution** de chaque élément est retenue : contenu, configuration et interface, sans ses dépendances.
- À chaque analyse pendant la marche, ce qui a changé ou disparu depuis est comparé.
- **La simulation s'arrête** si une modification du **développeur** change une empreinte ou supprime un élément.
- **Elle continue** :
  - pour une description ou un dossier : aucune empreinte ne change ;
  - pour ce que la simulation écrit elle-même (une recette, un utilisateur, un forçage de jumeau).
- La carte dit le nombre d'éléments et ce qu'ils demandent (« 2 éléments modifiés : 1 à compiler, 1 à générer »), avec 3 lignes au plus et « … et N autre(s) ».
- La règle est `pl::runChange` (1.11.16 : sortie de l'écran pour être essayée).

### Le retour des données rémanentes (lots 3 et 4)

| Le projet a changé | Ce que fait le retour |
|---|---|
| rien | toutes les valeurs reviennent, **après les valeurs initiales et avant les scripts de Démarrage** |
| une variable renommée ou déplacée | sa valeur la suit (l'identifiant compte, pas le nom) |
| une variable supprimée | sa valeur est ignorée et comptée |
| une nouvelle variable, un nouveau membre | sa valeur initiale |
| un membre retiré, un tableau raccourci | les cases disparues sont ignorées |
| un type compatible (INT → DINT, un entier → REAL) | la valeur est convertie, et c'est dit |
| un type incompatible (REAL → BOOL, simple → structure, une valeur d'énumération disparue) | la valeur initiale, et un avertissement qui nomme la variable, les deux types et l'ancienne valeur |
| exploitation : une variable décochée « Rémanente » ou liée à un équipement | sa valeur gardée est ignorée (comptée), puis retirée du stockage à l'écriture suivante |

Un script de Démarrage qui écrit une variable a le dernier mot. Par exemple, `Compteur_Clics := 0;` dans votre script `Init` remet ce compteur à zéro même s'il est rémanent.

## 7. Les tests ajoutés

| Suite (CTest) | Fichier | Contrôles | Ce qu'elle couvre |
|---|---|---|---|
| `hmibuild` | `tests/hmi_build_test.cpp` | 212 | le moteur de build : première génération, projet à jour, un script, une variable API citée par deux vues, une fonction (corps, signature, renommage par la vraie commande), `IHM_LOG` invalide qui fait échouer le build, la règle de l'arrêt sur modification, un symbole supprimé, échec puis correction, Régénérer élément / branche / tout, cache absent / coupé / d'un autre format, artefact supprimé ou altéré, générateur changé, annulation, verrou, coupure pendant une écriture, le gestionnaire et son fil |
| `hmilog` | `tests/hmi_log_test.cpp` | 40 | `IHM_LOG` : les niveaux, la vérification (niveau inconnu, message non texte, arguments), l'exécution, la Console |
| `hmisimdata` | `tests/hmi_simdata_test.cpp` | 47 | la rémanence de simulation : le format, les conversions, le retour par identifiant, le moteur |
| `hmiretain` | `tests/hmi_retain_test.cpp` | 94 | la rémanence d'exploitation : le format, le fichier et sa copie, le verrou, la prise et la fusion, l'état d'une variable, l'échange avec Excel, l'import, le projet, le moteur |
| `hmieditor` | `tests/hmi_editor_test.cpp` (*cycle1115*, *remanence1116*, *sources1116* et les autres) | 5 933 au total | le cycle dans le volet ; le poste qui garde et rend ; la reprise ; le verrou ; une écriture qui échoue ; un dossier inaccessible ; la colonne et la fiche ; les commandes ; le double-clic vers la source |

**Votre § 19, cas par cas :**

| # | Cas | Test |
|---|---|---|
| 1 | démarrage sans modification | `hmibuild` *premiereGenerationEtDemarrageSansModification* ; session 11113 |
| 2 | première génération complète | idem |
| 3 | modification d'un seul script | *unSeulScriptEtUneVue* |
| 4 | variable API utilisée par plusieurs vues | *variableApi* |
| 5 | vue sans impact sur les autres | *unSeulScriptEtUneVue* |
| 6 | renommage d'une fonction IHM | *fonctionIhm* (la vraie commande Renommer, puis le build) |
| 7 | suppression d'un symbole utilisé | *suppressionSymbole* |
| 8 | échec de compilation | *echecEtCorrection* ; sessions 11113a et 11113c |
| 9 | correction après échec | *echecEtCorrection* |
| 10 | régénération d'un seul élément | *commandes* |
| 11 | régénération d'une branche | *commandes* |
| 12 | régénération complète | *commandes* |
| 13 | cache absent | *cacheEtArtefacts* |
| 14 | cache corrompu | *cacheEtArtefacts* (coupé, cassé, d'un autre format) |
| 15 | artefact supprimé manuellement | *cacheEtArtefacts* |
| 16 | arrêt de simulation après modification | *arretSurModification* (la règle) ; `hmieditor` *cycle1115* (la carte) ; session 11115 |
| 17 | sauvegarde de la rémanence de simulation | *cycle1115* ; `hmisimdata` |
| 18 | restauration de la rémanence | *cycle1115* ; `hmisimdata` *moteur* |
| 19 | redémarrage avec effacement | *cycle1115* |
| 20 | génération sans effacement | *cycle1115* ; `hmibuild` (la phase G) ; session 11115 |
| 21 | option de rémanence désactivée | *cycle1115* ; session 11115 |
| 22 | variable IHM rémanente en exploitation | `hmiretain` ; *remanence1116* ; sessions 11116 A et B |
| 23 | variable non rémanente | `hmiretain` *priseEtFusion* ; *remanence1116* |
| 24 | changement de type compatible | `hmisimdata` *conversions* ; `hmiretain` *etats*, *moteur* ; *remanence1116* |
| 25 | changement de type incompatible | `hmisimdata` ; `hmiretain` *etats*, *moteur*, *import* |
| 26 | coupure pendant une sauvegarde | `hmibuild` *coupureSauvegarde* ; `hmiretain` *fichier* ; `hmisimdata` *format* ; *remanence1116* (écriture qui échoue) |
| 27 | appel valide à `IHM_LOG` | `hmilog` ; session 11114 |
| 28 | appel invalide à `IHM_LOG` | `hmilog` *verification* ; `hmibuild` *ihmLogInvalide* |
| 29 | navigation vers la source depuis un diagnostic | `hmieditor` *sources1116* ; session 11116 (`ihm-curseur 2 "Horloge"`) |

## 8. Les limitations restantes

- **Rien n'a été lancé sur un vrai Windows** : tout a été vérifié sous Wine (l'exe livré et son installateur).
- **Une coupure de courant réelle** n'a pas été provoquée. Les tests reproduisent ses effets : un fichier coupé, un temporaire laissé, un temporaire impossible à écrire, un dossier inaccessible.
- **Le poste écrit au plus une fois par seconde.** Une coupure tombant dans la seconde qui suit un changement perd ce changement : le fichier garde la valeur précédente, entière.
- **Les variables d'une structure ou d'un tableau** sont gardées case par case ; l'éditeur montre « N cases » sans détailler chacune.
- **La valeur actuelle** de la fiche est celle de la simulation de l'éditeur, quand elle tourne. Le poste d'exploitation ne tourne pas sous l'éditeur : sa valeur du moment est celle qu'il garde.
- **Deux instances de l'éditeur** sur le même projet peuvent écrire, l'une après l'autre, la rémanence de simulation. Chaque écriture reste entière, mais la dernière l'emporte. Le build et le poste, eux, ont leur verrou.
- **Le cas 20** (« génération sans effacement ») est vérifié dans le volet sans un vrai build incrémental derrière. La chaîne complète l'est dans la session 11115.

## 9. Tester vous-même l'ensemble

Prenez une copie d'Armoire_Gaz.

1. **Le build.**
   1. Ouvrez Simulation > IHM : la fenêtre de progression montre les phases A à G. Le premier démarrage génère tout.
   2. Arrêtez, puis redémarrez sans rien changer : « Projet à jour », rien n'est refait.
   3. Modifiez le script Horloge : il passe à ● Modifié dans l'arbre aussitôt. Démarrez : lui seul est régénéré et recompilé.
2. **Une erreur.**
   1. Ajoutez `IHM_LOG(BLABLA, 'x');` au script Horloge, puis Compiler (F7). L'erreur bloque le démarrage.
   2. Double-cliquez sur sa ligne dans Diagnostics : le script s'ouvre à la ligne 2.
   3. Remplacez par `IHM_LOG(INFO, 'tic {Secondes_IHM}');` : le build passe, et la Console montre les « tic ».
3. **Le cycle.**
   1. Cochez Rémanence dans la barre de la simulation. Laissez tourner, puis Arrêtez : les Sorties disent « données gardées ».
   2. Démarrez : Secondes_IHM reprend où il en était.
   3. Redémarrez : la question s'affiche, puis les valeurs initiales reviennent.
   4. Modifiez un script pendant la marche : la simulation s'arrête, et la carte propose Générer et redémarrer.
4. **La rémanence d'exploitation.**
   1. Dans IHM > Programmation générale > Variables IHM, mettez Rémanente à « oui » pour Consigne_Pression et Vitesse_Pompe. La fiche de droite montre « Rémanence (exploitation) ».
   2. Faites Essayer le poste. Changez Vitesse_Pompe sur une vue, attendez deux secondes, puis Quitter > Passer en conception (avec le mot de passe de sortie d'Armoire_Gaz).
   3. La fiche dit la valeur gardée (« Gardée »), sa date (« Gardée le ») et « à jour ».
   4. Quittez l'application **pendant que le poste tourne**, relancez-la, et entrez dans le poste : Vitesse_Pompe est rendue.
   5. Une variable non cochée (Mode_Pompe) repart de sa valeur initiale.
5. **Les commandes** (clic droit sur une variable) :
   1. Exporter les valeurs rémanentes… : choisissez un `.csv` et ouvrez-le dans Excel.
   2. Modifiez une valeur dans Excel, enregistrez, puis Importer les valeurs rémanentes… : le compte rendu s'affiche.
   3. Vérifier l'intégrité des données rémanentes.
   4. Réinitialiser cette variable rémanente : la question, puis la fiche dit « aucune ».
   5. Afficher l'emplacement du stockage : le chemin est copié.
6. **La robustesse.**
   1. Fermez l'application pendant que le poste tourne, puis ouvrez `ihm/historique/remanence_exploitation.txt` : il finit par `fin`.
   2. Coupez-le à la main (supprimez la fin) : au lancement suivant, le poste reprend la copie `.bak` et le journal le dit.
   3. Lancez deux postes sur le même projet : le second dit qu'un autre poste écrit déjà, puis prend la relève quand le premier s'arrête.
