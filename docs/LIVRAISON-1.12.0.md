# XPGAnalyser 1.12.0 : deux applications, et les Natives

Livrée le 10/10/2026 dans la nuit. Elle répond à vos demandes :
- une branche **Natives**, verrouillée, sous IHM › Programmation générale. Elle donne toutes les fonctions utilisables dans les scripts et les fonctions, rangées par catégorie, avec un exemple et une explication. Elle donne aussi tous les types natifs (MIN, MAX, taille…), leur disponibilité en ST, C et C++ (une colonne par langage) et leur syntaxe ;
- des fonctions de **couleurs**, des fonctions **aléatoires**, et la liste des **énumérations natives** ;
- **deux exe** : le premier avec l'API, le second avec seulement l'IHM. « retirer toute la partie API dans l'IHM et avoir l'API seul de son côté, tout mettre à jour dans tous les menus etc ».

Et à vos réponses : rien de l'automate dans les variables de l'exe IHM ; la simulation de l'IHM seule ; deux choses complètement indépendantes (`projets\api`, `projets\ihm`) ; un installateur pour les deux exe, et un zip portable par exe.

**Vos projets de la 1.11 ne sont pas modifiés.** Au premier lancement, chaque application en recopie sa moitié dans son propre dossier (§ 2).

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.0.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.0-portable.zip` et `XPGAnalyser-IHM-1.12.0-portable.zip` : chaque application, sans installation ;
- ce document ;
- les captures `1200_ihm_*` et `1200_api_*`, prises sous Wine avec les exe livrés.

## 1. Ce qui change pour vous

| | XPGAnalyser API | XPGAnalyser IHM |
|---|---|---|
| Le programme | `XpgAnalyzer-API.exe` | `XpgAnalyzer-IHM.exe` |
| Le raccourci | « XPGAnalyser API » | « XPGAnalyser IHM » |
| Ses projets | `<vos données>\projets\api` | `<vos données>\projets\ihm` |
| Ses réglages | `%APPDATA%\XpgAnalyzer\API` | `%APPDATA%\XpgAnalyzer\IHM` |
| L'arbre | API, Simulation (l'automate), Versions | IHM, Simulation (l'IHM), Versions |
| La simulation | l'automate seul | l'IHM seule et ses équipements simulés |

Les deux applications viennent du même code et s'installent ensemble, dans le même dossier. Elles partagent le dossier des données (bibliothèque, catalogue, captures), les thèmes et `XPGAnalyser.ini`. Chacune a ses réglages, ses projets récents et son instance : les deux peuvent tourner en même temps.

**Tout a été mis à jour, partout**, pour chaque application :
- l'accueil, l'arbre et son rail, la barre du haut et ses menus, la bande d'état ;
- les raccourcis, Aller à… et sa palette, les menus du clic droit ;
- le centre d'aide (ses chapitres), les tutoriels et les parcours, les notes de version, le titre de la fenêtre.

## 2. Les projets : deux rangements indépendants

- **XPGAnalyser API** range ses projets dans `projets\api`, **XPGAnalyser IHM** dans `projets\ihm`. Le manifeste de chaque projet dit son application (`edition = api` ou `ihm`). Un projet de l'autre application est refusé à l'ouverture, avec la raison.
- **Vos projets de la 1.11** (`projets\<Nom>`, l'automate et son IHM ensemble) sont recopiés une fois, au premier lancement de chaque application, chacun pour sa moitié :
  - XPGAnalyser API : tout, sauf l'IHM (`ihm\`) ;
  - XPGAnalyser IHM : le manifeste, `ihm\`, `donnees\` et `versions\`. L'IHM vide que la 1.11 créait pour chaque projet n'est pas recopiée.
- **Les originaux ne bougent pas.** Les projets récents désignent les copies. Le fichier `.depuis-1.11.txt` du rangement liste ce qui a été recopié. Une notification le dit au lancement, avec un bouton qui ouvre le dossier.
- **Nouveau projet** dans XPGAnalyser IHM : un dossier et une vue de démarrage, sans automate.
- **Versions** : chaque application ne voit et ne restaure que sa moitié.

## 3. XPGAnalyser IHM : l'IHM sans automate

**À la recopie, l'IHM d'un projet de la 1.11 devient autonome.**
- Les globales de l'automate qu'elle lisait deviennent ses variables IHM, dans le dossier **Automate**, avec le même nom, le même type et la même valeur initiale. Un DDT devient un type IHM ; un tableau reste un tableau (`ARRAY[a..b] OF …`) ; une instance de DFB devient un type fait de ses entrées et de ses sorties.
- Si l'automate était relié par Modbus TCP, il devient l'équipement **« Automate »**, et ses variables situées (%M, %MW, ou les adresses de sa table) y sont liées.
- Sur `bac\Armoire_Gaz`, l'IHM sans automate donnait 149 erreurs ; après la conversion, il en reste 8. Ce sont des noms qui n'existent ni dans l'IHM ni dans l'automate (`Debit_Entree`, `Capteur_PT9`, et les `Equipement.*` d'une popup).

**Ce qui n'existe plus dans XPGAnalyser IHM :**
- `API.` et les noms de l'automate : la vérification les dit inconnus (« variable inexistante dans l'IHM »). L'aide à la saisie et le sélecteur de valeurs ne les proposent plus ;
- dans la simulation : le simulateur de l'automate, Démarrer l'API, Les deux, l'onglet Variables API, « suit l'automate ». Les équipements simulés répondent toujours ;
- SYS.Plc et SYS.Comm, les objets de diagnostic de l'automate, la phase API du build ;
- dans Configuration, les variables du programme ; dans Communication, l'automate du projet (sa table, son essai, sa liaison) ;
- le poste ne lance ni simulateur, ni liaison vers l'automate du projet, ni serveur de démonstration.

L'IHM s'ouvre sur sa vue de démarrage.

## 4. XPGAnalyser API : l'automate sans IHM

- Les variables n'ont plus ni colonne ni filtre IHM ; les tables d'animation, plus de « + Variable IHM » ; les statistiques ne comptent plus l'IHM.
- La vue d'ensemble de la simulation ne garde que les cartes, la chaîne et la frise de l'automate.
- L'accueil, l'arbre, la barre du haut, les raccourcis et Aller à… ne parlent que de l'automate.

## 5. Les Natives

**IHM › Programmation générale › Natives**, le dernier dossier, verrouillé (un cadenas, l'étiquette « verrouillées »). Il contient 513 entrées :

| Rubrique | Contenu |
|---|---|
| **Fonctions** (391) | 20 catégories et 119 fonctions : Mathématiques (15), Sélection et bornes (5), Chaînes de caractères (11), Bits et décalages (4), Conversions (16), Tableaux (3), MAP et itérateurs (9), Références et pointeurs (2), Navigation et vues (5), Popups (7), Journal et Console (2), Scripts et temps (2), Utilisateurs (6), Alarmes (3), Affichage et son (4), GIF animés (4), Équipements et réseau (3), Exports (1), **Couleurs (13)**, **Aléatoire (4)**. Et **Conversions X_TO_Y** : 272, rangées par type de départ (`INT_TO_…`, 16 chacune) |
| **Types** (17) | les types de base : MIN, MAX, taille, place dans un équipement Modbus, écriture en C et en C++, valeur par défaut, littéraux ; et les types construits |
| **Opérateurs** (11) | du plus fort au plus faible |
| **Instructions** (9) | IF, CASE, FOR, FOR EACH, WHILE, REPEAT, EXIT, RETURN… |
| **Énumérations** (7, 34 valeurs) | TRANSITION, POSITION_POPUP, NIVEAU_LOG, THEME_IHM, SOURCE_EXPORT, ONGLET_CONNEXION, PRIORITE_ALARME |

**La fiche d'une fonction** (un clic dans l'arbre, dans l'onglet « IHM · Natives ») donne :
- sa signature ;
- un tableau « Dans chaque langage » : ST (exécuté en simulation), C et C++, avec ✓ (même nom), ~ (autre écriture) ou — (aucune) et ce qui s'écrit ;
- son exemple dans la notation choisie, avec le sélecteur **ST | C | C++**. Le choix est gardé pour toutes les fiches et pour l'aide ;
- où l'écrire : script, fonction IHM, expression fx d'une propriété, texte à trous, condition d'alarme ;
- ses paramètres, avec leur type et ceux qui sont facultatifs ;
- un exemple et son résultat, **vérifié dans le moteur de l'IHM** (l'essai `hminatives` exécute chaque exemple) ;
- ce qu'il faut savoir, et les fonctions voisines.

En C, 38 fonctions ont le même nom, 58 une autre écriture, 23 aucune ; en C++ : 37, 72 et 10.

**Les couleurs** : `RGB`, `RGBA`, `HSL`, `COULEUR_MELANGER`, `COULEUR_DEGRADE`, `COULEUR_ECLAIRCIR`, `COULEUR_ASSOMBRIR`, `COULEUR_OPACITE`, `COULEUR_CONTRASTE`, et les composantes `COULEUR_ROUGE`, `COULEUR_VERT`, `COULEUR_BLEU`, `COULEUR_ALPHA`. **L'aléatoire** : `RANDOM`, `RANDOM_INT`, `RANDOM_REAL`, `RANDOM_SEED`. **Les énumérations** : `TRANSITION#Fondu` vaut un nombre (un DINT), et la fonction IHM_ qui l'attend reçoit le mot qu'elle lisait déjà. La vérification connaît ces valeurs : une valeur mal écrite est une erreur.

**Une fonction du projet de même nom passe avant la native**, partout : à l'exécution, à la vérification et dans l'aide à la saisie.

**Autour des fiches :**
- **Insérer dans le script** (le bouton du volet, ou le clic droit dans l'arbre) pose l'appel (`LIMIT()`), la valeur (`TRANSITION#Fondu`) ou le mot dans le script ou la fonction en cours d'édition. **Copier** le met dans le presse-papiers ;
- **F1** sur un nom dans un script ouvre sa fiche ;
- **Précédente** revient à la fiche d'avant ;
- les liens d'une fiche mènent aux autres, et l'arbre suit ;
- au clic droit, Renommer et Supprimer sont grisés, avec « natif : verrouillé » ;
- « Tout déplier » laisse les natives repliées ;
- le filtre de l'arbre les trouve (`LIMIT`, `COULEUR`…).

## 6. Le parcours vérifié sous Wine

Deux sessions ont été jouées avec les exe livrés : `tools/sessions/session-1200-ihm.txt` et `session-1200-api.txt`. Chacune part d'un premier lancement de la 1.12.0 :
- ses réglages de la 1.11 sont présents, ses propres réglages absents ;
- `Armoire_Gaz` (la copie de démonstration des livraisons précédentes, l'automate et son IHM ensemble) est posé dans `projets\`.

**XPGAnalyser IHM** (`XpgAnalyzer-IHM.exe`)

| Étape | Ce qui s'est passé | Capture |
|---|---|---|
| **1.** L'accueil | « XPGAnalyser IHM », Nouveau projet IHM, Ouvrir un dossier de projet IHM, Découvrir l'IHM. En bas : « XPGAnalyser IHM · XpgAnalyzer-IHM.exe » | `1200_ihm_01` |
| **2.** La recopie, puis le projet ouvert | `projets\ihm\Armoire_Gaz` a été créé au lancement. L'arbre a IHM, Simulation (Vue d'ensemble, IHM, Équipements, Forçages, Courbes, Journal) et Versions : ni API, ni simulation de l'automate | `1200_ihm_02` |
| **3.** Programmation générale › Natives | Le sommaire : 391 fonctions, 17 types, et le reste ; le cadenas et « verrouillées » dans l'arbre | `1200_ihm_03` |
| **4.** Natives › Fonctions › Sélection et bornes › LIMIT | La fiche : signature, tableau ST / C / C++, exemple en ST ; dans l'arbre, `LIMIT(MN, IN, MX) : comme IN` | `1200_ihm_04` |
| **5.** Le sélecteur **C++** | L'exemple passe en C++ (`std::clamp`) ; la barre d'état dit que le choix est gardé pour toutes les fiches | `1200_ihm_05` |
| **6 à 11.** Les fiches de `COULEUR_MELANGER`, `RANDOM_INT`, `INT_TO_REAL`, `INT`, `TRANSITION`, des opérateurs | Chacune s'ouvre dans le même onglet, et l'arbre suit | `1200_ihm_06` à `_11` |
| **12.** Le script `Init` : une ligne neuve `Compteur_Clics := ;`, puis **Insérer** depuis la fiche de LIMIT, puis les arguments tapés | `Compteur_Clics := LIMIT(0, Compteur_Clics, 100);`. Le curseur est arrivé entre les parenthèses, la bulle de signature est affichée, « aucune erreur » | `1200_ihm_12` |
| **13.** **F1**, le curseur dans l'appel | La fiche de LIMIT revient au premier plan | `1200_ihm_13` |
| **14.** Variables IHM › Automate | Les 10 variables venues de l'automate : `Armoires` (ARRAY[0..1] OF armoire), `ConfigsGaz`, `NomArmoireModifs`, trois `CAPTEUR`, et `AUTOMATE_EN_RUN`, `i`, `j`, `UDINT_1174`, liées à l'équipement **Automate** (%M101, %MW2076, %MW2077, %MW1174) | `1200_ihm_14` |
| **15.** Simulation › IHM | « Build réussi : 0 erreur » ; l'IHM est en marche sur Vue_Supervision, ses esclaves simulés lus ; la barre n'a ni pastille ni bouton de l'API | `1200_ihm_15` |

**Ce que la session a trouvé, et qui est corrigé dans les exe livrés :**
- La première exécution a refusé de démarrer la simulation : « 1 erreur bloquante ». `Vue_Communication` affiche `UDINT_1174`, une variable de l'automate, dans la propriété « variable » d'un objet. Seul Générer contrôle cette propriété, et la conversion ne reprenait que les noms demandés par Compiler. Elle reprend maintenant aussi ceux de Générer.
- F1, le curseur entre les parenthèses de `LIMIT(…)`, ouvrait l'aide des scripts. Il ouvre maintenant la fiche de la fonction appelée. Insérer place aussi le curseur entre les parenthèses.
- La barre de la simulation montrait encore « API : absente » et son bouton. La barre d'outils remettait visibles les éléments masqués ; elle les garde maintenant cachés.
- Dans Récents, l'étiquette « verrouillées » des Natives chevauchait le nom du domaine.

_(XPGAnalyser API : à compléter)_

## 7. Fichiers modifiés

- les deux applications : `CMakeLists.txt` (`xpg_app` en bibliothèque d'objets, `xpg_api` et `xpg_ihm`), `src/main.cpp`, `src/core/Edition.*` (nouveau), `resources/windows/xpg_analyzer.rc(.in)`, `outils/mingw/cross_mingw.sh` ;
- les projets : `src/project/ProjectStore.*`, `src/app/EditionMigration.*` (nouveau), `src/hmi/HmiVersions.cpp`, `src/app/App.*`, `src/app/Settings.*` ;
- l'IHM sans automate : `src/hmi/HmiStandalone.*` (nouveau), `HmiCheck`, `HmiPipeline`, `HmiExprCheck`, `HmiScriptCheck`, `HmiPublicVars.hpp`, et dans `src/app/hmi/` : `HmiSimulation`, `HmiAssist`, `HmiValueKind`, `HmiValuePicker`, `HmiPanes`, `HmiCommPanes`, `HmiTwinPanes`, `HmiTwinValues`, `HmiPanels`, `HmiStationPanes`, `HmiEditor` ;
- l'API sans IHM : `VariablesPane`, `StatisticsPane`, `AnimationTablesPane`, `SimCenterOverview`, `SimStatus`, `HistoryPanel` ;
- l'interface de chaque application : `StartPage`, `StartScreen`, `ApiTrails`, `ViewModels`, `TreeWorkspace`, `TopBar`, `TopBarLot8`, `ApiWorkspace`, `StatusStripWorkspace`, `HistoryWorkspace`, `GoToWorkspace`, `MainAnalysisScreen`, `HmiWorkspace`, `StationScreen`, `ui/widgets/Containers` (un onglet caché) ;
- l'aide : `help/CenterIndex.cpp`, `help/CenterView.cpp`, `help/Shortcuts.cpp`, `help/CenterPages.cpp`, `help/ReleaseNotes.cpp`, `app/tutorial/TutorialApp.cpp` ;
- les natives :
  - le catalogue `outils/natives/catalogue.py`, qui génère `src/hmi/HmiNativesData.cpp` ;
  - `src/hmi/HmiNatives.*` et le moteur (`HmiRuntime`, `HmiExpr`, `HmiExprCheck`, `HmiScriptCheck`) ;
  - les fiches et le volet `src/app/hmi/HmiNativesCards.*` et `HmiNativesPane.*` (nouveaux) ;
  - l'arbre `ViewModels.*`, le menu `ExplorerMenus.cpp`, `ScriptRunner.cpp` (les commandes `natives…`) ;
- les paquets :
  - `installateur/XPGAnalyser.iss` (deux exe, deux raccourcis, l'exe de la 1.11 retiré) ;
  - `outils/mingw/package_mingw.py` (deux zips portables), `outils/config.ini` (`exe`, `exe_ihm`) ;
  - `XpgAnalyzer.vcxproj` (`XpgEdition = API | IHM`), `outils/lib/build.ps1`, `package.ps1`, `build_installer.ps1` ;
- les sessions Wine : `tools/sessions/session-1200-ihm.txt` et `session-1200-api.txt`.

## 8. Les tests ajoutés ou changés

- **`edition`** (nouveau, 39 contrôles) :
  - les trois éditions, leurs noms et ce que chacune a ;
  - la recopie des projets de la 1.11 : chaque moitié, les originaux intacts, une seule fois, les récents ;
  - un dossier de la 1.11 ouvert puis enregistré par XPGAnalyser IHM, sans que le programme soit touché ;
  - les versions et Dupliquer, par moitié ;
  - la conversion de `bac\Armoire_Gaz` : 149 erreurs sans automate, 8 après.
- **`hminatives`** : le catalogue (chaque fonction que Compiler accepte y est), chaque exemple des fiches exécuté dans le moteur, les 272 conversions, les littéraux de chaque type, les couleurs et l'aléatoire, les énumérations.
- **`hmieditor`** (30 contrôles de plus) : l'arbre des natives (513 nœuds, chacun avec sa clé et sa fiche, et la clé qui ramène au nœud), les 1 539 fiches composées et mises en page dans les trois notations, F1, Insérer, le volet (une fiche, un lien, la notation gardée, Précédente). Les autres changements suivent la version : Programmation générale a 8 dossiers, et il y a 33 versions de notes, dont la 1.12.0.
- La suite complète : **69 essais, 0 échec**.

## 9. Les limites

- **Rien n'a été lancé sur un vrai Windows** : tout a été vérifié sous Wine. Le projet Visual Studio (`XpgEdition`) et `build.bat` n'ont pas pu être essayés ici, faute de MSVC ; les exe livrés sont ceux de MinGW.
- **Un projet de la 1.11 ouvert directement** (sans la recopie) reste lisible par les deux applications. Chacune n'en lit et n'en écrit que sa moitié.
- **Les 8 erreurs restantes** d'Armoire_Gaz dans XPGAnalyser IHM viennent du projet lui-même (§ 3).
- **Les exemples C et C++** des fiches montrent l'écriture équivalente. Seul le ST s'exécute en simulation, comme avant.
- **Les raccourcis clavier des éditeurs** (le § 14 de la spécification de la refonte des scripts) restent à faire.
- **La source du guide Word** (`tools/guide-ihm/guide-ihm.txt`) n'a toujours pas suivi les versions depuis la 1.11.2.

## Tester vous-même

1. Installez, puis ouvrez le menu Démarrer : « XPGAnalyser API » et « XPGAnalyser IHM ». L'ancien raccourci « XPGAnalyser » et `XpgAnalyzer.exe` ont disparu.
2. Lancez **XPGAnalyser IHM**. Une notification dit combien de vos projets de la 1.11 ont été recopiés dans `projets\ihm`. Ouvrez-en un : l'arbre n'a ni API ni simulation de l'automate. **Programmation générale › Variables IHM › Automate** contient les variables venues de l'automate. **Compiler** ne parle plus de l'automate.
3. **Programmation générale › Natives › Fonctions › Sélection et bornes › LIMIT** : sa fiche. Cliquez **C++** au-dessus de l'exemple : toutes les fiches passent en C++, et l'aide aussi.
4. Ouvrez un script, puis la fiche de `RANDOM_INT` : **Insérer dans le script** pose `RANDOM_INT()` dans le script. Placez le curseur sur un nom de native, puis appuyez sur **F1** : sa fiche s'ouvre.
5. Dans un script, écrivez `Milieu := COULEUR_MELANGER('#000000', '#FFFFFF', 0.5);` (`Milieu` vaut `'#808080'`) et `IHM_NAVIGUER('Vue_Accueil', TRANSITION#Fondu);`. **Compiler** les accepte ; `TRANSITION#Fondue`, mal écrit, est une erreur.
6. **Simulation › IHM** : il n'y a ni « Démarrer l'API » ni « Les deux ». Vos équipements simulés répondent.
7. Lancez **XPGAnalyser API** (les deux peuvent tourner en même temps) : ses projets sont dans `projets\api`. Les variables n'ont plus de colonne IHM, et l'arbre n'a plus de dossier IHM. Le centre d'aide (F1) n'a que les chapitres de l'automate.

## Installer

_(à compléter)_

## Vérifications

_(à compléter)_
