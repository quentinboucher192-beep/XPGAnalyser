# XPGAnalyser 1.11.17 : refonte des scripts, lots 0 et 1

Livrée le 09/10/2026 au matin. C'est le début de la **refonte des scripts et des fonctions** (votre demande du 08/10). Comme le demande votre § 26, l'analyse du dépôt est venue d'abord : `docs/REFONTE-SCRIPTS-ANALYSE.md`, avec ses 11 points, ses 16 lots et ses 12 décisions par défaut. Viennent ensuite les deux premiers lots, chacun compilé et testé avant le suivant :
- **lot 0**, préparation et correctifs indépendants : renommer une fonction suit chaque appel ; la trace de référence ; le projet Visual Studio ;
- **lot 1**, « Compiler » compile le document affiché (votre § 13).

Rien ne change encore dans le format des projets (toujours le format IHM 22) ; un projet 1.11.16 s'ouvre tel quel.

Fichiers livrés :
- `XPGAnalyser-Setup-1.11.17.exe` (l'installateur) ;
- ce document, et l'analyse `docs/REFONTE-SCRIPTS-ANALYSE.md` ;
- les captures `11117_*`, prises sous Wine avec l'exe livré.

## 1. Ce qui change pour vous

- **Compiler (F7) dans un éditeur compile ce qu'il affiche, et lui seul** :
  - l'éditeur de scripts : le script affiché (avant : les fautes de tous les scripts du projet) ;
  - l'éditeur des fonctions : la fonction affichée (avant : il ouvrait aussi le rapport du projet) ;
  - l'éditeur des opérateurs : ceux du symbole ou du type affiché.

  Les fautes s'affichent sous le code. En erreur, les **Diagnostics** du panneau du bas s'ouvrent, filtrés sur l'élément compilé. Sans document, le bouton est grisé. Les info-bulles disent « Compiler le script actuel », « Compiler la fonction actuelle ».
- **Le projet entier reste à part** : IHM › Compiler, ou F7 hors d'un éditeur (sur l'arbre, sur une vue).
- **Un build ciblé ne garde de la validation que ce qui le touche** : une erreur ailleurs dans le projet ne fait plus échouer « Compiler le script actuel ». Démarrer, Compiler le projet et Compiler l'IHM depuis la racine de l'arbre valident tout, comme avant.
- **Renommer une fonction suit chaque appel**. Voir le § 3.
- **`IHM_JOURNAL` et `IHM_LOG` lisent les variables locales** du code qui les appelle. Avant, `IHM_JOURNAL('mini {Mini:0.0}')` écrivait `###` dans un script qui déclare `Mini`.

## 2. Lot 1 : compiler le document actif (§ 13)

| Éditeur | Le bouton Compiler (et F7) | Avant |
|---|---|---|
| Scripts généraux, scripts d'une vue, d'une popup, d'un symbole | Le script affiché, lui seul (`CompileFocus`) ; puis le build de ce script (son état dans l'arbre et la barre) | Les fautes de **tous** les scripts du projet, plus le build du script |
| Fonctions IHM, fonctions d'un symbole | La fonction affichée, avec la signature de ce qu'elle appelle ; puis son build | La fonction, **et** l'ouverture du rapport du projet (IHM › Compiler) |
| Opérateurs d'un symbole ou d'un type | Les opérateurs du porteur affiché ; puis le build du porteur | L'ouverture du rapport du projet |

Ce qui accompagne le bouton :
- **les info-bulles** commencent par « Compiler le script actuel », « Compiler la fonction actuelle », « Compiler les opérateurs affichés » ;
- **sans document** (aucun script ou aucune fonction choisis, aucun opérateur), le bouton est grisé et F7 le dit ;
- **le message** sous la barre : « Compiler le script Horloge : 1 faute (un clic sur le résultat y mène) · le projet entier : IHM > Compiler » ;
- **le document affiché est celui qui est compilé** : l'éditeur écrit chaque frappe dans le modèle, et le build part d'un instantané de ce modèle ; un autre script choisi efface les résultats du précédent ;
- **en erreur**, le build ouvre le panneau du bas sur **Diagnostics**. Sa validation étant limitée à la demande (§ 3 de l'analyse, constat 6), la liste ne contient que l'élément compilé.

**Le pipeline.** La phase E (« Validation ») passait les contrôles de Générer sur tout le projet, quelle que soit la demande. Une demande ciblée ne garde plus que les remarques de sa portée. Celles des autres éléments restent dans le cache, intactes, et celles du projet lui-même aussi. Démarrer, Compiler le projet (sans portée) et Compiler l'IHM depuis la racine de l'arbre valident tout, comme avant.

**F7.** Dans un éditeur (scripts, fonctions, opérateurs), le volet prend la touche avant l'écran : il compile le document affiché. Ailleurs, F7 ouvre IHM › Compiler, le rapport du projet entier. La table des raccourcis (F1) le dit.

**Les raccourcis Ctrl+B, Ctrl+Maj+B et F6** que propose votre § 13 ne sont pas encore là. Le clavier de l'application n'a ni la touche B ni F6 (`ui::Key`). Ils viendront avec le gestionnaire central des raccourcis (lot 14), selon le profil que vous retiendrez (décision D5 de l'analyse, à confirmer).

## 3. Lot 0 : renommer suit chaque appel

**Un seul parcours des textes du projet**, `hmi::forEachCode`. Il couvre :
- les scripts (généraux, de vue, de popup, de symbole) ;
- les fonctions (IHM et de symbole) et les redéfinitions des instances ;
- les opérateurs, de symbole et de type ;
- les actions et les propriétés des objets, lues comme les lit le renommage d'un paramètre de symbole : expressions, listes, états, cellules, arguments, cibles ;
- les titres et les paramètres des vues ;
- les alarmes : du projet, de symbole, surchargées (condition, message, consigne) ;
- les recettes, les historiques et les autorisations par expression.

Quatre commandes s'en servent : Renommer une fonction, « Appelée par », renommer une fonction de symbole, renommer une valeur ou un type d'énumération. Avant, chacune avait sa propre liste, et toutes oubliaient une partie de ces textes.

**La portée d'un symbole.** Dans un symbole, dans ses popups et dans les redéfinitions de ses instances, un appel court vise d'abord la fonction du symbole, comme à l'exécution. Renommer la fonction IHM `Calc` laisse donc intact `Calc(` dans un symbole qui a sa propre `Calc`.

**Le refus d'un détournement.** Un renommage qui changerait la cible d'un appel est refusé, avec le lieu :
- renommer `Calc` en `Calcul` alors qu'un symbole qui appelle `Calc` a déjà sa propre `Calcul` ;
- renommer une fonction de symbole du nom d'une fonction IHM qu'il appelle déjà.

**`IHM_JOURNAL` et `IHM_LOG`** remplissent leurs trous avec les variables locales du code qui les appelle : `VAR`, `VAR_TEMP`, l'indice d'une boucle, les paramètres d'une fonction. L'interpréteur expose désormais, le temps d'un appel, les locales de l'appelant (`sim::readCallerLocal`). La trace de référence a trouvé ce défaut : `Statistiques_Pression` écrivait « Pression A : mini ###, maxi ### bar ».

**La trace de référence** (CTest `hmitrace`) fait tourner `bac/Armoire_Gaz` pendant 40 s :
- l'IHM image par image ;
- l'automate simulé une seconde à la fois ;
- les 29 vues visitées ;
- les 3 scripts « Appel » appelés.

Elle compare les variables IHM, les alarmes et le journal (sans l'heure) à `tests/fixtures/trace/armoire_gaz.trace`, soit 914 lignes. Les lots suivants changent la forme des déclarations, pas l'exécution : la trace doit rester la même. Pour la refaire après un changement voulu : `XPG_TRACE_REFAIRE=1`. Elle s'exécute alors deux fois, pour vérifier que rien ne dépend de l'horloge.

**Le projet Visual Studio** (`outils\build.bat`, MSBuild) ne liait plus depuis la 1.11.3 : 21 `.cpp` et 21 en-têtes manquaient à `XpgAnalyzer.vcxproj`. Il est resynchronisé, et `outils/verifier_vcxproj.py` (CTest `vcxproj`) vérifie qu'il suit CMake (`--corriger` l'y remet). `build.sh` est déclaré non tenu à jour.

## 4. Le parcours vérifié sous Wine

Une session jouée avec l'exe livré, sur une copie d'Armoire_Gaz sans cache. Chaque contrôle échoue si le texte attendu n'y est pas ; il n'y a eu aucun échec.

| Étape | Ce qui s'est passé | Capture |
|---|---|---|
| **1.** Une faute ajoutée à deux scripts : Horloge (`Secondes_IHM := 'zz';`) et Init (`Compteur_Clics := 'abc';`). Horloge affiché, **Compiler le script actuel** | « Compiler le script Horloge : 1 faute (un clic sur le résultat y mène) · le projet entier : IHM > Compiler ». Résultats de Compiler : 1 ligne, Horloge. Le build de ce script échoue (1 erreur) et ouvre le panneau du bas sur **Diagnostics** : une seule ligne, Horloge. La faute d'Init n'y est pas | `11117_01` |
| **2.** Init choisi, **F7** | « Compiler le script Init : 1 faute ». Les Diagnostics ne montrent qu'Init, alors que l'arbre marque toujours Horloge en faute | `11117_03` |
| **3.** Fonctions IHM, Moyenne_Pression, **Compiler la fonction actuelle** | « Compiler la fonction Moyenne_Pression : aucune faute ». Le build **réussit** (1 généré, 1 compilé, 0 erreur), alors que deux scripts du projet sont en faute. Le rapport du projet ne s'ouvre pas | `11117_04` |
| **4.** Le projet entier (IHM › Compiler) | Le build échoue : 304 générés, 219 compilés, 2 erreurs. Diagnostics : 54 lignes, les deux fautes en tête, puis les avertissements du projet. L'info-bulle du bouton de la fonction est visible | `11117_05` |
| **5.** Les deux fautes retirées (Ctrl+Z), la simulation IHM, la Console filtrée sur « Pression A » | « Pression A : mini 0.0, maxi 0.0 bar (5 mesures) », puis « (10 mesures) », écrites par `Statistiques_Pression` à sa ligne 21. Avant la 1.11.17 : « mini ###, maxi ### bar » | `11117_06` |

## 5. Fichiers créés et modifiés

**Lot 0** (commit 4a90821) :
- **créés** :
  - `tests/hmi_trace_test.cpp` et `tests/fixtures/trace/armoire_gaz.trace` : la trace de référence ;
  - `outils/verifier_vcxproj.py` ;
- **modifiés** :
  - `src/hmi/HmiScript.hpp` et `.cpp` : `forEachCode`, `rewriteInText`, `renameCaptures` ; Renommer et « Appelée par » passent par `forEachCode` ;
  - `src/hmi/HmiSymbols.hpp` et `.cpp` : `renameSymbolFunction` passe par `forEachCode` ;
  - `src/hmi/HmiEnums.cpp` : le renommage d'une énumération passe par `forEachCode` ;
  - `src/sim/Interpreter.hpp` et `.cpp` : `readCallerLocal` ;
  - `src/hmi/HmiRuntime.cpp` : les trous d'`IHM_JOURNAL` et d'`IHM_LOG` ;
  - `src/hmi/HmiModel.hpp` : le commentaire mort ;
  - `src/app/hmi/HmiFunctionPanes.cpp` : le refus d'un renommage qui détourne ;
  - `XpgAnalyzer.vcxproj` et `.filters`, `build.sh`, `CMakeLists.txt` (les essais `hmitrace` et `vcxproj`) ;
  - `tests/hmi_test.cpp`.

**Lot 1** (commit 63652e7) :
- `src/hmi/HmiPipeline.cpp` : la validation limitée à la demande ;
- `src/app/hmi/HmiScriptPanes`, `HmiFunctionPanes`, `HmiOperatorPanes` : Compiler le document affiché, F7, le bouton grisé, les info-bulles ;
- `src/app/hmi/HmiPanels` : `HmiToolStrip::isEnabled` ;
- `src/app/screens/HmiBuildWorkspace.cpp` et `HmiWorkspace.cpp` : le build des opérateurs ;
- `src/app/screens/HistoryWorkspace.cpp` : le commentaire de F7 ;
- `src/app/ScriptRunner.cpp` : la commande de session `volet-message` ;
- `src/help/Shortcuts.cpp` (F7), `src/hmi/HmiGuideText.cpp` (l'aide F1), `tools/tutoriels/script-rampe.tuto` et `src/help/TutorialTexts.cpp` (le tutoriel du script) ;
- `src/help/ReleaseNotes.cpp` : 5 notes de version ;
- `tools/sessions/session-11117-compiler-actif.txt` : la session Wine ;
- `tests/hmi_build_test.cpp` et `tests/hmi_editor_test.cpp` ;
- la version 1.11.17 :
  - `src/core/Version.hpp`, `src/hmi/HmiPackage.hpp`, `src/core/CodeStats.hpp` ;
  - `CMakeLists.txt`, `installateur/XPGAnalyser.iss`, `outils/config.ini`, `resources/windows/xpg_analyzer.rc` ;
  - `dist/SHA256SUMS.txt`.

## 6. Les tests ajoutés

- **`hmi_test`** :
  - *renommerPartout1117* : un projet avec un symbole qui appelle `Calc`, un autre qui a sa propre `Calc`, leurs popups, redéfinitions, opérateurs, alarmes et surcharges, une autorisation.
    - « Appelée par » : chaque lieu, sauf ceux du second symbole ;
    - Renommer : les 13 textes qui suivent, et ceux qui ne doivent pas suivre ;
    - le refus d'un détournement, dans les deux sens ;
    - renommer une fonction de symbole : sa popup, l'alarme du projet, l'autorisation, la redéfinition (nom, retour, `SUPER`) ;
    - une valeur d'énumération dans une fonction de symbole ;
  - *journalLocales1117* : `VAR`, `VAR_TEMP`, l'indice d'une boucle et les paramètres d'une fonction dans `IHM_JOURNAL` et `IHM_LOG` ; une locale avant une variable IHM du même nom ; plus de `###`.
- **`hmi_trace_test`** (CTest `hmitrace`) : la trace de référence d'Armoire_Gaz.
- **`vcxproj`** (CTest) : le projet Visual Studio suit CMake.
- **`hmi_build_test`**, *compilerLeDocumentActif* (14 contrôles) :
  - le script seul passe malgré une fonction en faute ailleurs ;
  - le projet entier échoue ;
  - la remarque de la fonction reste au cache après un build ciblé ;
  - la fonction ciblée échoue ;
  - la racine « IHM » et Démarrer valident tout.

  Vérifié dans l'autre sens : sans le correctif, deux de ces contrôles échouent.
- **`hmi_editor_test`** :
  - l'éditeur de scripts : le bouton actif, la faute du seul script affiché, le build de ce script, le rapport du projet non ouvert, F7 sur un autre script, la faute d'une vue absente du volet mais présente dans le rapport, le bouton grisé sans script ;
  - l'éditeur des fonctions : la fonction affichée, son build, sans le rapport ; F7 ;
  - l'éditeur des opérateurs : le build du type ;
  - la table des raccourcis (63 lignes) et les notes (25 versions).

## 7. Les limites

- Rien n'a été lancé sur un vrai Windows : tout a été vérifié sous Wine.
- Ctrl+B, Ctrl+Maj+B et F6 ne sont pas encore là (voir le § 2).
- « Compiler la fonction actuelle » d'une fonction de symbole compile la fonction dans la portée du symbole (ses paramètres, ses autres fonctions). Les instances qui la redéfinissent ne sont pas recompilées : elles suivent au prochain build du projet.
- Renommer un **script** ne suit toujours pas `IHM_APPELER('Ancien')`, et l'application le dit déjà. C'est prévu au lot 10 (la navigation et le renommage).
- La trace de référence fait avancer l'automate d'une seconde par cycle. Son programme coûte 125 ms par cycle dans une construction de test non optimisée.

## 8. Tester vous-même

1. Ouvrez une copie d'un projet. Dans IHM › Programmation générale › Scripts, ajoutez une faute à deux scripts, par exemple `Compteur := 'abc';`.
2. Choisissez l'un d'eux et cliquez sur **Compiler** (ou F7). Seule sa faute s'affiche. Le panneau du bas s'ouvre sur Diagnostics avec ce seul script.
3. Choisissez l'autre et faites F7 : seule la sienne s'affiche.
4. Dans l'onglet Fonctions, choisissez une fonction sans faute et faites Compiler : le build réussit malgré les deux scripts en faute.
5. IHM › Compiler (ou F7 sur l'arbre) : le rapport du projet entier liste les deux fautes.
6. Renommez une fonction IHM appelée depuis une fonction de symbole : l'appel suit. « Appelée par », dans les propriétés de la fonction, cite ce lieu.
7. Dans un script, déclarez `VAR Mini : REAL := 4.5; END_VAR` et écrivez `IHM_JOURNAL('mini {Mini:0.0}');`. En simulation, la Console dit « mini 4.5 ».

## Installer

1. Lancez `XPGAnalyser-Setup-1.11.17.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.17.exe` (15,0 Mo, 15 758 225 octets) :
`106F76E4C2683923819C305CA4486FAB55EDBA5EEA5723BC77D06357C3A9E76C`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (61 sur 61, dont les deux nouveaux essais `hmitrace` et `vcxproj`). Elle comprend les 226 contrôles de `hmibuild` et les 5 948 de `hmieditor`.

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.17` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session du § 4 a été rejouée avec l'exe livré : 6 captures `11117_*`, aucun contrôle en échec. La capture `11117_02` reprend la `11117_01` (le build en échec ouvre déjà les Diagnostics) : elle n'est pas jointe.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé pour tous les comptes, par-dessus la 1.11.16 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.17`.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Le § 8 donne le parcours à refaire chez vous.

## La suite

**Le lot 2**, le lecteur partagé et l'extracteur de déclarations sans perte. Il fournit une seule lecture fiable des blocs `VAR` :
- toutes les formes : `VAR`, `VAR_TEMP`, `VAR_INPUT`, `VAR_IN_OUT`, `VAR_OUTPUT`, `CONSTANT`, `RETAIN` ;
- plusieurs noms par ligne, la valeur initiale ;
- les commentaires rattachés, la position de chaque nom ;
- les fonctions internes.

Elle remplacera les lectures en double, avec un essai d'égalité sur tous les projets du dépôt.

Viennent ensuite :
- **le lot 3**, les déclarations dans le modèle (format IHM 23), lues par le moteur sans le changer ;
- **le lot 4**, la migration des blocs `VAR` existants, avec son rapport, sa version de sauvegarde et Ctrl+Z.

La trace de référence d'Armoire_Gaz doit rester identique d'un bout à l'autre.

Deux décisions de l'analyse attendent votre avis : **D5**, le profil des raccourcis, et **D7**, le sens du mot « Générer ». Rien ne bouge sur ces deux points avant les lots 13 et 14.
