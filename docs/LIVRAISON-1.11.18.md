# XPGAnalyser 1.11.18 : refonte des scripts, lots 2 à 5

Livrée le 09/10/2026 dans la matinée. Elle poursuit la **refonte des scripts et des fonctions**, commencée avec la 1.11.17 (votre demande du 08/10). Les déclarations sortent du code et entrent dans des onglets (votre § 2). La refonte avance en quatre lots, chacun compilé et testé avant le suivant :
- **lot 2** : une seule lecture, sans perte, des blocs `VAR … END_VAR` ;
- **lot 3** : les déclarations dans le modèle du projet (le format IHM 23), et un pont qui les fait lire au moteur sans le changer ;
- **lot 4** : la migration des blocs `VAR` existants ;
- **lot 5** : les onglets Constantes, Variables, Paramètres et Locales, avec le collage depuis Excel et le stockage Persistante.

**Un projet d'avant s'ouvre et tourne tel quel.** Ses blocs `VAR` restent lus. Il ne passe au format 23 que lorsqu'il a des déclarations dans ses onglets : la 1.11.17 refuse alors de l'ouvrir, plutôt que de perdre ses déclarations.

Fichiers livrés :
- `XPGAnalyser-Setup-1.11.18.exe` (l'installateur) ;
- ce document ;
- les captures `11118_*`, prises sous Wine avec l'exe livré.

## 1. Ce qui change pour vous

- **L'éditeur d'un code a des onglets** :

  | Éditeur | Onglets |
  |---|---|
  | un script (général, de vue, de popup, de symbole, d'écran modèle) | Code, Constantes, Variables |
  | une fonction (IHM ou de symbole) | Code, Paramètres, Locales, Constantes |
  | un opérateur (de type IHM ou de symbole) | Code, Locales, Constantes |

  Le code ne garde que sa logique. Le titre d'un onglet compte ses déclarations ; il est rouge si l'une est fautive.
- **On déclare dans une grille**, comme dans un tableur : on écrit dans la case, on choisit dans une liste, on ajoute, supprime, duplique et déplace au clavier. Chaque geste s'annule (Ctrl+Z). Une erreur est refusée tout de suite, et la barre dit pourquoi.
- **Renommer une déclaration renomme ses utilisations** dans le code, y compris les trous `{Nom}` des textes d'`IHM_JOURNAL` et d'`IHM_LOG`.
- **Excel dans les deux sens** : Ctrl+C copie les lignes avec leurs titres ; Ctrl+V colle un tableau d'Excel, même écrit à la française (`2,5`, `VRAI`).
- **Le stockage dit la vie d'une variable** :
  - Exécution : remise à sa valeur initiale à chaque exécution (l'ancien `VAR_TEMP`) ;
  - Conservée : gardée d'une exécution à l'autre (l'ancien `VAR`) ;
  - Persistante, nouveau : gardée aussi d'un lancement à l'autre, en simulation comme sur le poste d'exploitation.
- **Migrer d'un clic** :
  - le bandeau « Migrer ce code » au-dessus d'un code qui déclare encore ses variables dans son texte ;
  - pour tout le projet, IHM › Compiler › « Migrer les déclarations… ».

  Une version de sauvegarde est créée d'abord, et un seul Ctrl+Z reprend la migration.
- **Une nouvelle fonction n'a plus de bloc VAR** : son paramètre d'exemple et sa locale sont dans ses onglets. Le retour d'une procédure s'appelle « Aucun ».

## 2. Lot 5 : les onglets de déclarations

### La grille

Une grille par onglet, la même partout (`HmiDeclGrid`). Ses colonnes :

| Onglet | Colonnes |
|---|---|
| Constantes | Nom, Type, Valeur, Visibilité (un script), Utilisations, Documentation |
| Variables (Locales d'une fonction ou d'un opérateur) | Nom, Type, Initiale, Stockage et Visibilité (un script), Utilisations, Documentation |
| Paramètres | Nom, Type, Mode, Défaut, Utilisations, Documentation |

Ce qu'elle sait faire :
- **écrire dans la case** : double-clic ou F2 ;
- **choisir dans une liste** :
  - le Type (les types de base, puis les structures et énumérations du projet ; « Autre type… » ouvre un champ libre pour `ARRAY`, `REF_TO`, `MAP`) ;
  - le Stockage (une fonction ne propose qu'Exécution : elle n'a pas de mémoire) ;
  - le Mode (Entrée, Entrée/sortie, Sortie) ;
  - la Visibilité (Public, Privé) ;
- **la barre** : Ajouter, Supprimer, Dupliquer, monter, descendre, Utilisations, et un champ Rechercher qui filtre les lignes ;
- **le clavier** : Inser ajoute (sa case Nom s'ouvre aussitôt), Suppr supprime, Ctrl+D duplique (`Nom_copie`), Alt+Haut et Alt+Bas déplacent. L'ordre des paramètres est la signature : leur grille ne se trie pas ;
- **refuser avant d'agir** : un nom pris, réservé ou illisible, un type inconnu, une constante sans valeur, une valeur que le simulateur ne lirait pas, une locale de fonction Conservée. La barre du volet dit pourquoi, et rien ne change ;
- **les fautes en rouge** : une déclaration fautive (par exemple un nom aussi déclaré dans un bloc `VAR` du code) a son nom en rouge, la raison en infobulle. Un clic sur sa faute dans les Diagnostics ouvre son onglet sur sa ligne ;
- **Utilisations** : l'onglet Code s'ouvre sur l'utilisation suivante (« Utilisation 2 sur 5 de Compteur (ligne 12) »). La colonne Utilisations les compte ; à 0, le nombre est grisé.

### Renommer

Renommer une déclaration (sa case Nom) renomme ses utilisations :
- dans son code, mais ni dans les chaînes, ni dans les commentaires, ni dans un membre `x.Nom`, ni dans un argument nommé `F(Nom := 1)` ;
- dans les trous `{Nom}` et `{Nom:0.0}` des textes d'`IHM_JOURNAL` et d'`IHM_LOG` ;
- dans les valeurs des autres déclarations ;
- pour un paramètre d'une fonction de symbole, dans les redéfinitions de ses instances.

Un nom que le code emploie déjà pour autre chose (une variable IHM, une fonction) est refusé pour une déclaration employée : les deux se confondraient. Une déclaration jamais employée peut le prendre : c'est la façon de déclarer un nom que le code utilise déjà.

### Excel

- **Ctrl+C** copie les lignes choisies avec leurs titres, séparées par des tabulations : Excel les colle en cases.
- **Ctrl+V** colle un tableau d'Excel :
  - les colonnes sont reconnues par leur titre (Nom, Type, Valeur, Initiale, Défaut, Stockage, Mode, Visibilité, Documentation, en français ou en anglais), dans n'importe quel ordre ;
  - un nom qui existe est mis à jour, un nom nouveau est créé ; Ctrl+Maj+V colle en nouvelles lignes ;
  - les valeurs écrites à la française sont relues : `2,5` devient `2.5`, `1 234,5` devient `1234.5`, `VRAI` devient `TRUE` ;
  - une case refusée est marquée en rouge, avec sa raison, et les autres sont collées ;
  - le collage entier s'annule d'un seul Ctrl+Z.

### Le stockage Persistante

Une variable de script au stockage **Persistante** garde sa valeur d'un lancement à l'autre :
- en simulation, avec la rémanence de simulation (`.xpg/simulation`) ;
- sur le poste d'exploitation, avec ses variables rémanentes (`ihm/historique`).

Sa valeur est rendue à la première exécution de son script. Un script qui ne tourne pas pendant une séance garde la valeur qui lui avait été rendue. Si son type a changé entre-temps, la valeur est convertie quand les types sont compatibles (INT vers DINT, INT vers REAL) ; sinon la variable repart de sa valeur initiale, et le journal le dit.

### La migration, au quotidien

- **Le bandeau** : au-dessus du code d'un script, d'une fonction ou d'un opérateur qui déclare encore ses variables dans son texte, il dit combien de noms passeraient dans les onglets. « Migrer ce code » le fait en une commande ; Ctrl+Z rend le code d'avant à l'octet près. Une fonction de symbole migre avec ses redéfinitions.
- **Le projet entier** : IHM › Compiler montre « Migrer les déclarations… » tant qu'un code a un bloc `VAR`. Le dialogue :
  - donne le rapport : combien de codes, de déclarations, de points d'attention ;
  - liste chaque code à migrer, coché, avec ses déclarations ; vous décochez ceux à laisser ;
  - dit ceux qui restent tels quels, et pourquoi.

  « Migrer » enregistre d'abord le projet, crée la version « Avant migration des déclarations », puis migre en une seule commande.

### Le reste

- **Une nouvelle fonction** reçoit son paramètre d'exemple (`Entree`, du type du retour) et sa locale `Resultat` dans ses onglets. Son code n'a plus de bloc `VAR`.
- **Le retour « Aucun »** remplace « (aucun) » ; l'ancien mot se relit.
- **Une redéfinition neuve** d'une fonction de symbole part du corps du symbole et copie aussi ses locales et ses constantes, que ce corps lit. Son éditeur les contrôle.
- **Le guide** (F1) : les articles « Variables locales » et « Fonctions IHM » décrivent les onglets, le stockage, Excel et la migration.

## 3. Lot 4 : la migration des blocs VAR

`hmi::migrate` (`src/hmi/HmiMigrate.hpp`) fait passer les déclarations écrites dans le code dans le modèle. Elles sont lues sans perte par le lot 2, converties, puis retirées du code. Le moteur lit les mêmes déclarations, reconstruites : l'exécution ne change pas.

| Bloc écrit | Dans un script | Dans une fonction, une redéfinition, un opérateur |
|---|---|---|
| `VAR` | Variable Conservée | Variable Exécution (une fonction n'a pas de mémoire) |
| `VAR_TEMP` | Variable Exécution | Variable Exécution |
| `VAR CONSTANT` | Constante | Constante |
| `VAR RETAIN` | Variable Conservée, avec un point d'attention | Variable Exécution |
| `VAR_INPUT`, `VAR_IN_OUT`, `VAR_OUTPUT` | non migré (le script ne tourne pas aujourd'hui) | Paramètre Entrée, Entrée/sortie, Sortie |

- **Le commentaire d'une déclaration** devient sa documentation. Ceux qui ne sont rattachés à aucune déclaration restent dans le code, à la place du bloc.
- **Une déclaration migrée est Privée** (décision D11) : elle n'était vue que de son code.
- **Ne migre pas**, et le rapport dit pourquoi :
  - un code dont un bloc est illisible ;
  - un script ou un opérateur qui déclare des paramètres ;
  - une constante sans valeur ;
  - un nom déjà dans le modèle ;
  - une redéfinition dont les paramètres diffèrent de ceux de sa fonction.

  Une fonction de symbole et ses redéfinitions migrent ensemble, ou pas du tout. Le bloc de paramètres d'une redéfinition, identique à celui de sa fonction, s'en va : elle les reçoit de sa fonction, et le rapport le dit (« ses paramètres (Motif) : ceux de S_Vanne.Ouvrir »).
- **Les fonctions internes** (`FUNCTION … END_FUNCTION`) restent dans le code, avec leurs blocs. Elles deviendront des fonctions du script au lot 8 (décision D2).
- **La trace de référence** (`hmitracemig`) le prouve : Armoire_Gaz migré entièrement produit la même trace de 914 lignes que l'original.

## 4. Lot 3 : les déclarations dans le modèle, le format 23, le pont

- **Le modèle** : une déclaration (`hmi::Declaration`) s'attache à un script, une fonction, une redéfinition ou un opérateur. Elle a :
  - un identifiant stable, donné par le projet et jamais réutilisé ;
  - un genre (constante, variable, paramètre), un nom, un type, une valeur et sa documentation ;
  - un stockage (Exécution, Conservée, Persistante) pour une variable, un mode (Entrée, Entrée/sortie, Sortie) pour un paramètre ;
  - une visibilité (Public, Privé).
- **Le format IHM 23** écrit une ligne `declaration` par déclaration, juste après la ligne de son porteur. Il n'est écrit que si le projet a des déclarations dans le modèle ; sinon le projet reste au format 22 et la 1.11.17 l'ouvre. Un identifiant absent, en double ou venu d'un autre projet est renouvelé, au chargement et à chaque commande.
- **Le pont** (voie A, décision D1) : le moteur et les contrôles lisent les déclarations du modèle comme des blocs `VAR` reconstruits sur la ligne 1 du corps. Les lignes du corps ne bougent pas, et une faute qui tombe dans une déclaration la nomme. Passent par lui : l'exécution, Compiler et la vérification pendant la frappe, la génération incrémentale, la signature, l'aide à la saisie.
- **Les fautes des déclarations elles-mêmes** sont dites sans le moteur : un nom vide, illisible, réservé ou en double, un type manquant ou non pris en charge, une constante sans valeur, un paramètre là où il n'en faut pas.
- **Les déclarations suivent** :
  - les renommages (fonction, valeur d'énumération, variable IHM, type) ;
  - Rechercher et Remplacer ;
  - « Appelée par » ;
  - les paquets d'export et d'import ;
  - les fichiers `.xpgst` : une ligne `(*# declaration … *)` par déclaration, au format 2 seulement s'il y en a.

## 5. Lot 2 : les déclarations lues sans perte

- **Une seule lecture, partagée** (`hmi::decl`) :
  - un lexer de surface qui garde les commentaires, les octets, les lignes et les colonnes ;
  - `extract` lit les cinq sortes de blocs et leurs qualificatifs : plusieurs noms par ligne, le type et la valeur en texte, le commentaire rattaché, la place de chaque nom, les fonctions internes ;
  - `compose` réécrit un bloc, relu à l'identique.
- **L'empreinte d'interface d'une fonction** la lit. Ajouter un paramètre `VAR_IN_OUT` ou `VAR_OUTPUT` fait recompiler les appelants (avant, ce changement était ignoré).
- **L'arbre du projet et l'import** la lisent. Un `VAR` écrit dans une chaîne, dans un membre ou dans un repère n'ouvre plus de bloc.
- **L'égalité** avec la lecture d'avant est prouvée sur 157 codes (222 déclarations) du corpus et des deux projets du dépôt.

## 6. Le parcours vérifié sous Wine

Une session jouée avec l'exe livré, sur une copie d'Armoire_Gaz sans cache (`tools/sessions/session-11118-onglets-declarations.txt`), celle de la 1.11.17 : ses scripts et ses fonctions déclarent encore leurs variables dans leur texte. Chaque contrôle échoue si le texte attendu n'y est pas ; il n'y a eu aucun échec.

| Étape | Ce qui s'est passé | Capture |
|---|---|---|
| **1.** Scripts généraux, Statistiques_Pression | Au-dessus du code, le bandeau « Ancien format : ce code déclare encore 5 noms dans son texte (VAR … END_VAR) » et son bouton **Migrer ce code**. L'éditeur a ses onglets Code, Constantes, Variables | `11118_01` |
| **2.** **Migrer ce code** | « 1 code migré, 5 déclarations (Ctrl+Z reprend la migration) ». Le code commence par sa logique (`P := Armoires[0].ana.PT1.mes;`). L'onglet Variables compte 5 : Mini (1000.0), Maxi, Tours et Ecart en **Conservée** (l'ancien `VAR` d'un script), P en **Exécution** (l'ancien `VAR_TEMP`), toutes Privées | `11118_02`, `11118_03` |
| **3.** La grille défilée vers la droite | La colonne Utilisations (Mini 3, Maxi 3, Tours 4, Ecart 0, P 6) et la Documentation : les commentaires du bloc, « VAR : gardée d'une exécution à l'autre » et « VAR_TEMP : repart à chaque exécution » | `11118_04` |
| **4.** Tours passée en **Persistante**, puis renommée **Mesures** dans sa case Nom | « Tours renommée en Mesures - le code suit (4 utilisations) ». Dans le code : `Mesures := Mesures + 1;`, `IF Mesures MOD 5 = 0`, et le trou `({Mesures} mesures)` du texte de l'`IHM_JOURNAL` | `11118_05`, `11118_06` |
| **5.** Onglet Constantes : **Ajouter** (Seuil_Haut, 30.0, sa documentation), puis un tableau d'Excel collé par Ctrl+V : Pression_Max `40.0`, Pression_Min `2,5` | « Collé : 2 constantes créées, 0 mises à jour — Ctrl+Z pour tout annuler ». `2,5` est devenu `2.5` ; les lignes collées sont marquées | `11118_07` |
| **6.** Fonctions IHM, Moyenne_Pression, **Migrer ce code**, onglet Paramètres | « 1 code migré, 4 déclarations ». A, B et Poids_A (défaut 0.5) en Entrée, dans l'ordre de la signature ; Somme dans Locales | `11118_08` |
| **7.** IHM › Compiler › **Migrer les déclarations…** | « 7 codes à migrer, 9 déclarations », chacun coché avec ses noms : le script Surveillance_Fours, les fonctions Tracer_Evenement, Journaliser, Bonus, les fonctions Ouvrir et GetActiveCount du symbole S_Vanne, et la redéfinition de Vanne_3 (« ses paramètres (Motif) : ceux de S_Vanne.Ouvrir »). **Migrer 7 codes** : « Migré : 7 codes migrés, 9 déclarations · version V4 · Avant migration des déclarations ». Compiler : 0 erreur, 0 avertissement ; « Migrer les déclarations… » n'est plus proposé | `11118_09`, `11118_10` |
| **8.** La simulation IHM, le projet entièrement migré | Le build réussit (308 générés, 225 compilés, 0 erreur). La Console, filtrée sur « Pression A » : « Pression A : mini 0.0, maxi 0.0 bar (5 mesures) », puis « (10 mesures) ». Ce sont les lignes de la 1.11.17, écrites par Statistiques_Pression à sa ligne 12 (la ligne 21 avant la migration : les 9 lignes des blocs sont parties) | `11118_11` |

## 7. Fichiers créés et modifiés

**Lot 2** (commit d475d9f) :
- **créés** : `src/hmi/HmiDecl.hpp` et `.cpp` ; `tests/hmi_decl_test.cpp` ; `outils/corpus_declarations.py` et `tests/fixtures/decl/corpus.txt` ;
- **modifiés** : `src/hmi/HmiPipeline.cpp` (l'empreinte d'interface), `src/app/hmi/HmiTreeData.hpp` (l'arbre), `tests/hmi_build_test.cpp`, `CMakeLists.txt`.

**Lot 3** (commit 3446484) :
- `src/hmi/HmiModel.hpp` et `.cpp` : `Declaration`, ses clés, `uniqueDeclarationIds` ;
- `src/hmi/HmiStore.cpp` : le format 23 ;
- `src/hmi/HmiDecl.cpp` : le pont (`composeCode`, `codeOf`, `checkDeclarations`) ;
- le moteur et les contrôles : `HmiRuntime`, `HmiScript`, `HmiScriptCheck`, `HmiCheck`, `HmiPipeline`, `HmiOperators`, `HmiSymbols`, `HmiExprCheck`, `HmiBuildState`, `HmiCommands` ;
- le suivi : `HmiRenameRefs`, `HmiPopupParams`, `HmiDesign`, `HmiPackage`, `HmiScriptFile` ;
- l'application : `HmiAssist`, `HmiScriptPanes`, `HmiFunctionPanes`, `HmiOperatorPanes`, `HmiVariablePanes`, `RenameDialog`, `HmiWorkspace`, `ImportWorkspace`, `TutorialApp` ;
- `tests/hmi_decl_test.cpp` et `tests/hmi_test.cpp`.

**Lot 4** (commit 1f07406) :
- **créés** : `src/hmi/HmiMigrate.hpp` et `.cpp` ; `tests/hmi_migrate_test.cpp` ;
- **modifiés** : `tests/hmi_trace_test.cpp` (l'option `--migrer`), `CMakeLists.txt` (les essais `hmimigrate` et `hmitracemig`), le projet Visual Studio.

**Lot 5** (commits 68ce4f7, 5cdd4f1 et c43162e) :
- **créés** :
  - `src/hmi/HmiDeclEdit.hpp` et `.cpp` : les gestes des grilles, validés avant d'agir ;
  - `src/app/hmi/HmiDeclGrid.hpp` et `.cpp` : la grille, les onglets (`HmiCodeTabs`), le bandeau, la migration d'un code ;
- **modifiés** :
  - `src/hmi/HmiDecl` (les fautes par déclaration), `HmiScript` (le modèle de fonction, la lecture d'une valeur), `HmiRuntime` et `HmiSimData` (Persistante) ;
  - `src/app/hmi/HmiScriptPanes`, `HmiFunctionPanes`, `HmiOperatorPanes` : les onglets ; `HmiPanes` (Migrer les déclarations…) ; `HmiEditor` et `HmiActionDialogs` (les redéfinitions) ;
  - `src/app/screens/HmiWorkspace.cpp` et `Screens.hpp` : la migration du projet ; `src/hmi/HmiMigrate` : le résumé avant d'agir (« 7 codes à migrer »), la note des paramètres d'une redéfinition ;
  - `src/app/ScriptRunner.cpp` : la commande de session `sous-onglet` cherche d'abord le titre exact (« Variables » ne prend plus « Variables IHM ») ;
  - `tools/sessions/session-11118-onglets-declarations.txt` : la session Wine du § 6 ;
  - `src/hmi/HmiGuideText.cpp` (le guide), `src/help/ReleaseNotes.cpp` (8 notes) ;
  - `tests/hmi_decl_test.cpp`, `tests/hmi_editor_test.cpp`, `tests/hmi_test.cpp` ;
  - la version 1.11.18 : `src/core/Version.hpp`, `src/hmi/HmiPackage.hpp`, `src/core/CodeStats.hpp`, `CMakeLists.txt`, `installateur/XPGAnalyser.iss`, `outils/config.ini`, `resources/windows/xpg_analyzer.rc`, `dist/SHA256SUMS.txt`, le projet Visual Studio.

## 8. Les tests ajoutés

- **`hmidecl`** (`hmi_decl_test`, 890 contrôles) :
  - le lexer, chaque forme de bloc, les commentaires, les fonctions internes, les fautes, `compose`, la signature (lot 2) ;
  - le modèle et le format 23 : l'aller-retour des sept porteurs, les identifiants, le pont, « la même faute à la même ligne » et « la même exécution » avec des déclarations du modèle ou des blocs écrits (lot 3) ;
  - les gestes des grilles sur les sept porteurs, les refus, le renommage (code, trous, valeurs, redéfinitions), les libellés relus, les valeurs d'Excel, dupliquer, déplacer, supprimer, les fautes ligne à ligne, cinq gestes égalent cinq commandes et le code tourne (lot 5) ;
  - Persistante : trois appels, l'arrêt, la relance (la valeur continue), Conservée qui repart, un type converti ou refusé, un script qui n'a pas tourné, le stockage du poste.
- **`hmimigrate`** (37 contrôles) et **`hmitracemig`** : la migration, et la même trace de référence après elle ; la note des paramètres d'une redéfinition, le résumé avant et après.
- **`hmieditor`** (6 455 contrôles), *onglets1118* :
  - les onglets des trois éditeurs ;
  - Ajouter et sa case Nom ouverte ; les listes ; « Autre type… » ;
  - un refus dit dans la barre ; renommer, et le code suit ; Utilisations ;
  - le collage d'Excel (`12,5` relu `12.5`, un nom créé, un autre mis à jour, un seul Ctrl+Z) et Ctrl+C ;
  - une faute en rouge ; le bandeau et « Migrer ce code » ; un script C ;
  - le modèle de fonction sans `VAR`, le retour Aucun, l'ordre des paramètres ;
  - les noms réservés d'un opérateur.
- **`hmi`** (4 675 contrôles) : le modèle d'une nouvelle fonction ; et un défaut ancien de l'essai `symbolesLot10` corrigé. Il gardait un pointeur sur une vue après en avoir ajouté une autre ; ce pointeur, perdu, faisait planter l'essai de temps en temps. C'était le plantage intermittent noté pendant le lot 3.

## 9. Les limites

- **Rien n'a été lancé sur un vrai Windows** : tout a été vérifié sous Wine.
- **La visibilité** (Public, Privé) s'enregistre et s'édite, sans effet jusqu'aux noms qualifiés (lot 7). Une déclaration n'est vue que de son code.
- **Renommer un paramètre ne renomme pas encore les arguments nommés de ses appels** (`F(Ancien := 1)`). Le renommage symbolique du lot 10 le fera.
- **Persistante ne garde que les types simples et les énumérations.** Un tableau ou une structure Persistants repartent de leur valeur initiale, et le journal le dit. Un script de symbole garde une valeur par script, pas une par instance.
- **Les locales d'une redéfinition** se copient de celles du symbole et migrent avec elle, mais ne s'éditent pas encore dans une grille : son éditeur reste celui du code. Le lot 8 (fonctions et surcharges) reprendra les redéfinitions.
- **Une valeur initiale** se contrôle à la saisie pour sa syntaxe, et à l'exécution pour son sens (un nom inconnu). Le résolveur du lot 7 contrôlera le sens.
- **Un tableau ne prend pas de liste** `[1, 2, 3]` comme valeur initiale : le simulateur ne la lit pas, et la grille la refuse. Une seule valeur remplit toutes ses cases.
- **Les références externes, les fonctions de script, la valeur calculée d'une constante et la feuille .xlsx** viennent aux lots 7, 8 et 12.

## 10. Tester vous-même

1. Ouvrez une copie d'un projet. Dans IHM › Programmation générale › Scripts, choisissez un script qui déclare ses variables dans son texte (`VAR … END_VAR`). Le bandeau « Ancien format » s'affiche au-dessus du code.
2. Cliquez sur **Migrer ce code**. Les blocs disparaissent du code ; l'onglet Variables compte ses variables, avec leurs commentaires en documentation. Ctrl+Z rend le code d'avant.
3. Dans l'onglet Variables, changez le Stockage d'une variable en **Persistante**, puis renommez-la dans sa case Nom : le code suit, et les trous `{Nom}` de ses `IHM_JOURNAL` aussi.
4. Dans Excel, préparez un tableau Nom, Type, Valeur, avec un nombre à virgule (`2,5`). Copiez-le, cliquez dans l'onglet Constantes et faites Ctrl+V : les lignes sont créées, `2,5` devient `2.5`. Un Ctrl+Z reprend le collage entier.
5. Dans l'onglet Fonctions, créez une fonction : son code n'a pas de bloc VAR, ses onglets Paramètres et Locales ont `Entree` et `Resultat`.
6. IHM › Compiler › **Migrer les déclarations…** : le rapport, les codes cochés. Migrez : la version « Avant migration des déclarations » apparaît dans les versions du projet.
7. Lancez la simulation, arrêtez-la, relancez-la : la variable Persistante reprend sa valeur.

## Installer

1. Lancez `XPGAnalyser-Setup-1.11.18.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.18.exe` (15,1 Mo, 15 868 955 octets) :
`31252ED2FCB37B273F87BAF3B237A09912FA9D0A8122D76B0DD878F28507C5E8`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (64 sur 64, dont les nouveaux essais `hmidecl`, `hmimigrate` et `hmitracemig`). Elle comprend les 890 contrôles de `hmidecl`, les 37 de `hmimigrate`, les 6 455 de `hmieditor` et les 4 675 de `hmi`.

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.18` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session du § 6 a été rejouée avec l'exe livré : 11 captures `11118_*`, aucun contrôle en échec.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé pour tous les comptes, par-dessus la 1.11.17 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.18`.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Le § 10 donne le parcours à refaire chez vous.

## La suite

**Le lot 6**, le registre des types : un seul catalogue des types (de base, IHM, de l'automate, des bibliothèques), que les grilles, l'aide à la saisie et les contrôles liront.

Viennent ensuite :
- **le lot 7**, le résolveur et les noms qualifiés : `Script.Constante`, `Instance.Script.Variable`, la visibilité qui prend effet, les références externes ;
- **le lot 8**, les fonctions et les surcharges (les fonctions internes d'un script, les redéfinitions).

Deux décisions de l'analyse attendent toujours votre avis : **D5**, le profil des raccourcis, et **D7**, le sens du mot « Générer ». Rien ne bouge sur ces deux points avant les lots 13 et 14.
