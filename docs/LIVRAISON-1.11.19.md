# XPGAnalyser 1.11.19 : refonte des scripts, lot 6 (le registre des types)

Livrée le 09/10/2026 à la mi-journée. Elle poursuit la **refonte des scripts et des fonctions** (votre demande du 08/10) avec le **lot 6** : votre § 7, les types. Un seul registre dit désormais quels types existent et où ils sont permis, et une seule règle dit ce qui passe d'un type à l'autre. Un sélecteur permet de choisir un type partout où l'on en écrit un.

**Un projet de la 1.11.18 s'ouvre tel quel**, toujours au format 23. Une déclaration d'un type IHM enregistre en plus la clé de son type (`type_cle`) ; la 1.11.18 relit ces fichiers et ignore la clé.

Fichiers livrés :
- `XPGAnalyser-Setup-1.11.19.exe` (l'installateur) ;
- ce document ;
- les captures `11119_*`, prises sous Wine avec l'exe livré.

## 1. Ce qui change pour vous

- **« Choisir un type… »**, au bout de chaque liste de types, ouvre le **sélecteur de types**. On le trouve :
  - dans la case Type d'une déclaration (constante, variable, paramètre, locale) ;
  - dans la case Type d'une variable IHM et d'un membre d'un type IHM ;
  - dans le type d'un paramètre de popup ou de symbole ;
  - dans le Type de retour d'une fonction.
- **Le sélecteur** offre :
  - une recherche, sans casse ni accents ;
  - les catégories : Élémentaires, Chaînes et durées, Structures IHM, Énumérations, DDT de l'API, Génériques, Sans valeur ;
  - les **Récents** ;
  - pour chaque type, sa provenance, son identifiant, sa phrase, ses membres ou ses valeurs ;
  - **Construire** un tableau (une ou deux dimensions), une référence `REF_TO` ou une `MAP`, là où l'usage les permet ;
  - **Ouvrir la définition** d'un type IHM ou d'un DDT.

  Il se tient à jour : un type créé ailleurs pendant qu'il est ouvert y apparaît, et un type actuel introuvable est signalé.
- **Les mêmes types, dans le même ordre, partout.** Toutes les listes de types viennent du registre. Le Type de retour d'une nouvelle fonction propose aussi les structures et les énumérations du projet.
- **Une seule règle de conversion** pour les paramètres de popup, les opérateurs et les valeurs gardées (rémanence) :
  - Les paramètres acceptent les mêmes types qu'avant.
  - Un opérateur aussi, mais il choisit d'abord celui qui ne perd rien ; avant, il prenait le premier trouvé.
- **Corrigé** : une locale ou un paramètre LINT, ULINT ou USINT d'une fonction était calculé comme un INT de 16 bits (100000 devenait −31072). C'était aussi le cas d'un type écrit en minuscules (`lreal`). Ils ont maintenant leur type.

## 2. Le registre des types

`hmi::typereg` (`src/hmi/HmiTypeRegistry.hpp`) est le catalogue. Il est construit à partir du projet et des DDT du programme de l'automate, puis ne change plus : le build le lit dans un autre fil.

| Ce qu'un type y a | Exemples |
|---|---|
| une **clé stable**, qui ne dépend pas de son nom affiché | `base:REAL`, `ihm:615` (un type IHM : son identifiant), `api:T_ANA` (un DDT), `any`, `void` |
| une catégorie, une provenance, une phrase | Structures IHM · Projet · Types IHM |
| les **usages** où il est permis | variable IHM, déclaration, paramètre de popup, retour, opérande |

Les usages reproduisent exactement les listes d'avant, qui ne sont plus écrites en dur :

| Usage | Les types de base | Remplace |
|---|---|---|
| variable IHM, membre d'un type | BOOL, INT, DINT, UINT, UDINT, WORD, DWORD, REAL, LREAL, STRING, TIME (une place Modbus) | `kVariableTypes` |
| déclaration (constante, variable, locale, paramètre d'un code) | les mêmes, et SINT, USINT, BYTE ; LINT et ULINT permis sans être proposés | `kLocalTypes`, `richLocalType` |
| paramètre de popup | ceux d'une variable IHM et ANY ; tout entier permis ; les DDT | `params::baseTypes`, `typeKnown` |
| retour de fonction | ceux d'une déclaration et Aucun | le dialogue Nouvelle fonction |
| opérande | les 17 types de la norme | `kBaseTypes` |

Un **type construit** (`ARRAY[0..9] OF REAL`, `MAP[STRING] OF T`, `REF_TO T`) est lu par le registre et remis en forme. Sa clé se déduit de celle de son élément (`array[0..9]:base:REAL`). Un nom inconnu y est signalé, avec sa raison.

## 3. La règle de conversion

| Passer une valeur de… vers… | Verdict | Coût (opérateurs) |
|---|---|---|
| le même type (sans casse ; STRING[n] = STRING ; EBOOL = BOOL), ANY | exacte | 0 |
| un entier vers un entier qui contient toutes ses valeurs (INT → DINT, USINT → INT, UINT → UDINT, UINT → WORD) | élargie | 1 |
| REAL → LREAL | élargie | 1 |
| un entier de 16 bits au plus → REAL ; un entier → LREAL | élargie | 2 |
| un entier → un entier au moins aussi large qui ne le contient pas (UINT → INT, INT → UDINT) ; DINT → REAL ; LREAL → REAL | avec perte possible | 4 |
| un entier → un plus petit (DINT → INT) | rétrécie : refusée (une valeur gardée passe si elle tient) | — |
| un réel → un entier ; un nombre → STRING ; deux structures, deux tableaux différents | interdite, la raison dite | — |

Qui l'applique :
- **les paramètres de popup**, en Copie : exacte ou élargie (comme avant). En Référence, une variable donnée doit être exacte (comme avant).
- **les opérateurs** : tout sauf rétrécie et interdite (les mêmes paires qu'avant), par coût croissant.
- **les valeurs gardées** (rémanence de simulation, d'exploitation, Persistante) : un entier passe dans un autre entier s'il tient, un entier devient un réel ; le reste repart de sa valeur initiale (comme avant).

**Les écarts, prouvés case par case** : l'essai `hmitypes` garde une copie figée des trois anciennes règles. Il compare chaque paire de 22 types :
- les paramètres acceptent exactement les mêmes paires qu'avant ;
- les opérateurs aussi, à deux exceptions voulues près : STRING[n] et STRING s'acceptent désormais l'un l'autre ;
- **35 coûts d'opérateur changent** : une conversion avec perte passe en dernier choix au lieu d'être à égalité. Quand deux opérateurs conviennent, celui qui ne perd rien est choisi ; avant, c'était le premier déclaré.

## 4. Le sélecteur de types

`app::HmiTypePicker` (`src/app/hmi/HmiTypePicker.hpp`) est bâti sur le modèle du sélecteur de valeurs. Une grille ou un volet le **demande** à un hôte (`app::typepicker`). L'écran principal l'ouvre, garde les récents (le réglage `hmi.types.recents`) et mène à une définition.

### Le parcours vérifié sous Wine

Une session jouée avec l'exe livré, sur une copie d'Armoire_Gaz sans cache (`tools/sessions/session-11119-registre-types.txt`), celle de la 1.11.17. Chaque commande `choix-type-…` échoue si le sélecteur n'est pas ouvert ou si son résultat n'est pas celui attendu ; il n'y a eu aucun échec.

| Étape | Ce qui s'est passé | Capture |
|---|---|---|
| **1.** Statistiques_Pression migré, une variable **Fours_Suivis** ajoutée (INT), sa case Type : **Choisir un type…** | Le sélecteur « Type de Fours_Suivis », pour : une déclaration. Le type actuel, INT, est choisi d'office et visible. Sa phrase : « Entier signé 16 bits (-32768 à 32767). Se convertit sans perte vers : DINT, LINT, REAL, LREAL ». Les puces : Élémentaires 14, Chaînes et durées 2, Structures IHM 5 | `11119_01` |
| **2.** La recherche « four » | Une seule ligne : T_Four, Structures IHM, Projet · Types IHM, `ihm:616`, avec ses membres (Temperature, Consigne, Marche, Defaut, Vannes : ARRAY[1..3] OF T_Vanne, Heures) | `11119_02` |
| **3.** Tableau, bornes `1..4` | Résultat : `ARRAY[1..4] OF T_Four`. **Ouvrir la définition** est actif (un type IHM) | `11119_03` |
| **4.** **Choisir** | La case Type de Fours_Suivis prend `ARRAY[1..4] OF T_Four` ; aucune erreur dans le script | `11119_04` |
| **5.** Le sélecteur de Mini (REAL), puce **Récents** | T_Four, choisi à l'étape 4, est en tête des récents | `11119_05` |
| **6.** Variables IHM, Temperature_Local, **Choisir un type…** | « pour : une variable IHM » : 9 élémentaires seulement (les types d'une place Modbus, ni SINT ni LINT), sans REF_TO ni MAP. REAL, son type, est choisi | `11119_06` |
| **7.** Retour au script, le sélecteur de Mini : « vanne », T_Vanne, **Ouvrir la définition** | L'onglet Types IHM s'ouvre sur T_Vanne : ses 3 membres (Ouverte, Fermee, Position), « Utilisé par T_Four.Vannes » | `11119_07` |

**Deux défauts trouvés pendant la livraison, corrigés avant l'installateur :**
- **Le type actuel sortait de la liste à l'ouverture.** La première session montrait INT choisi, mais la liste commençait à LINT : la ligne était calculée avant que la liste ait sa taille. Elle est maintenant amenée en vue à la mise en page. Un essai le vérifie pour INT, BOOL, REAL et T_Four (*selecteurTypes1119*).
- **La trace de référence dépendait de l'heure du poste.** Le programmateur hebdomadaire du projet d'essai (Prog_Chauffage, du lundi au vendredi de 7 h à 12 h et de 13 h 30 à 17 h) écrit `Chauffage_Actif` selon l'heure réelle. Lancés un vendredi à 7 h, les essais `hmitrace` et `hmitracemig` échouaient à leur ligne 164. L'essai fait maintenant vivre l'IHM un dimanche à midi, par le réglage d'heure du poste (`clockOffset`) : le résultat ne dépend plus du moment où il est lancé. Le moteur n'a pas changé.

## 5. La clé du type d'une déclaration

À l'enregistrement, une déclaration d'un type IHM (ou d'un tableau, d'une référence de lui) écrit la clé de son type, par exemple `type="T_Four" type_cle="ihm:616"`.

Au chargement :
- si le nom ne se lit plus (le type a été renommé hors de l'application, par exemple le fichier édité à la main), la clé retrouve son nom d'aujourd'hui, et le rapport de chargement le dit ;
- en mémoire, la clé n'est pas gardée : l'enregistrement la recalcule toujours du texte du type.

## 6. Fichiers créés et modifiés

**Lot 6** (commits 3b5c98d et suivants) :
- **créés** :
  - `src/hmi/HmiTypeRegistry.hpp` et `.cpp` : le registre, la lecture des types, la règle, `simTypeOf`, la clé suivie au chargement ;
  - `src/app/hmi/HmiTypePicker.hpp` et `.cpp` : le sélecteur et son hôte ;
  - `tests/hmi_types_test.cpp` (l'essai `hmitypes`) ;
  - `tools/sessions/session-11119-registre-types.txt` : la session Wine ;
- **modifiés** :
  - le registre branché :
    - `HmiPopupParams` (les types proposés, connus, la règle) ;
    - `HmiOperators` (les types de base, le coût) ;
    - `HmiSimData` (la conversion des valeurs gardées) ;
    - `HmiScript` et `HmiModel` (`kLocalTypes` et `kVariableTypes` ne sont plus) ;
    - `HmiTypes`, `HmiDeclEdit`, `HmiExprCheck`, `HmiScriptCheck`, `HmiRuntime` (le type du moteur), `HmiStore` (la clé) ;
  - les écrans :
    - `HmiDeclGrid`, `HmiVariablePanes`, `HmiParamPanes`, `HmiEditor`, `HmiFunctionPanes` (« Choisir un type… ») ;
    - `HmiEquipmentPanes`, `HmiAssist`, `HmiValueKind`, `HmiWorkspace` et `Screens.hpp` (les listes, l'hôte du sélecteur) ;
  - `src/app/ScriptRunner.cpp` : les commandes de session `choix-type-…` ;
  - `tests/hmi_trace_test.cpp` : la trace de référence ne dépend plus de l'heure du poste ;
  - `src/hmi/HmiGuideText.cpp` (le guide), `src/help/ReleaseNotes.cpp` (5 notes) ;
  - la version 1.11.19 : `CMakeLists.txt`, `src/core/Version.hpp`, `src/hmi/HmiPackage.hpp`, `resources/windows/xpg_analyzer.rc`, `installateur/XPGAnalyser.iss`, `outils/config.ini`, `src/core/CodeStats.hpp`, `dist/SHA256SUMS.txt` ; le projet Visual Studio.

## 7. Les tests ajoutés

- **`hmitypes`** (`hmi_types_test`, 84 contrôles) :
  - les listes d'avant retrouvées, usage par usage ;
  - le registre d'un projet : structures, énumérations, DDT ; une clé qui survit au renommage ; un type ajouté ensuite ;
  - la lecture des types : base, tableaux (1, 2, N dimensions), STRING[n], MAP, REF_TO, POINTER TO, MAP_ITERATOR ; les refus et leurs raisons ;
  - la règle contre la copie figée des trois anciennes ; les 35 coûts changés ;
  - la valeur qui tient ;
  - le type du moteur ; la fonction LINT (200000, plus −31072) et `lreal` ;
  - la clé écrite, relue, suivie quand le type est renommé dans le fichier.
- **`hmieditor`**, *selecteurTypes1119* (40 contrôles) :
  - la liste d'une déclaration, d'une variable IHM, d'un paramètre, d'un retour ;
  - la recherche (accents compris), les catégories, les récents ;
  - le type actuel choisi d'office (un tableau : ses bornes) ; un type introuvable dit ;
  - le type actuel visible dès l'ouverture (INT, BOOL, REAL, T_Four) ;
  - construire et vérifier ; Choisir grisé ; rafraîchi quand le projet change ; Ouvrir la définition ;
  - « Choisir un type… » dans une grille de déclarations, dans Variables IHM et dans le retour d'une fonction, par un hôte d'essai (puis Ctrl+Z) ; un type refusé, dit.

## 8. Les limites

- **Rien n'a été lancé sur un vrai Windows** : tout a été vérifié sous Wine.
- **Les symboles, les DFB et les bibliothèques ne sont pas encore des types du registre.** Une instance de symbole comme type viendra avec les références du lot 7.
- **DATE, TOD et DT** ne sont pas proposés : le moteur de l'IHM ne les calcule pas.
- **LINT et ULINT** sont permis, mais l'IHM les calcule sur 32 bits (comme DINT et UDINT) ; leur phrase le dit.
- **La clé de type** n'est enregistrée que pour les déclarations. Les variables IHM, les membres et les paramètres de popup suivent un renommage par leur nom, comme avant.
- **Les listes d'encodage** ne passent pas par le registre : types de registre Modbus, jumeau numérique, table d'adresses, mots. Ce sont des encodages d'équipement, pas des types de l'IHM.
- **Les contrôles d'appel** (arguments, retour, surcharges, récursion) appliqueront la règle aux lots 8 et 9.

## 9. Tester vous-même

1. Dans un script, onglet Variables, cliquez sur la case Type d'une variable, puis **Choisir un type…**.
2. Tapez `four` : seul T_Four reste. Écrivez des bornes (`1..4`) : le résultat devient `ARRAY[1..4] OF T_Four`. **Choisir** : la case prend ce type, et Ctrl+Z le reprend.
3. Rouvrez le sélecteur : T_Four est dans les **Récents**.
4. Dans Variables IHM, la même entrée ne propose que les types d'une place Modbus (ni SINT, ni LINT).
5. **Ouvrir la définition** d'un type IHM : l'onglet Types IHM s'ouvre sur lui.

## Installer

1. Lancez `XPGAnalyser-Setup-1.11.19.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.19.exe` (15,2 Mo, 15 908 965 octets) :
`6A730616AEBDF92664EF1FB7A0DFD2CB58854A41C4CD4AD496EDBEEFB41D4B9A`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (65 sur 65). Elle comprend le nouvel essai `hmitypes` (84 contrôles), les 6 500 contrôles de `hmieditor`, les 890 de `hmidecl`, les 37 de `hmimigrate` et les deux traces de référence, `hmitrace` et `hmitracemig`.

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.19` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session du § 4 a été rejouée avec l'exe livré : 7 captures `11119_*`, aucun contrôle en échec.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé pour tous les comptes, par-dessus la 1.11.18 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.19`.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Le § 9 donne le parcours à refaire chez vous.

## La suite

Votre message de ce matin change l'ordre :

1. **La 1.11.20 : les signatures.**
   - **Les paramètres Entrée/sortie (`VAR_IN_OUT`, par référence) et Sortie (`VAR_OUTPUT`) comptent partout comme des paramètres** : le nombre d'arguments d'un appel (« la fonction ne demande que 2 arguments » disparaît), la signature, la liste et la propriété Paramètres, l'arbre, l'aide à la saisie, Essayer. Un argument passé par référence doit être une variable, et c'est dit à la saisie. Un essai prouvera qu'un appel depuis un script écrit bien dans la variable de l'appelant.
   - **Les surcharges** (votre § 10, prévues au lot 8 et avancées à votre demande) : plusieurs fonctions du même nom si leurs paramètres diffèrent. Cela vaut pour les fonctions IHM, les fonctions internes d'un script et les fonctions de symbole. Le type de retour seul ne suffit pas à les distinguer. L'appel choisit la surcharge qui convertit le moins (la règle du § 3). Un appel ambigu, ou qu'aucune surcharge n'accepte, est signalé avec les signatures possibles. L'aide à la saisie les montre toutes.
2. **La 1.11.21 : les éditeurs.** Les bandeaux Essai et Diagnostics quittent les éditeurs : les erreurs utiles vont au panneau du bas (Diagnostics, un clic mène à la ligne), le résultat d'Essayer aux Sorties. Les explorateurs (application, vue, objets) déplient un script en constantes, variables et fonctions internes, et une fonction en paramètres (avec leur sens), locales et constantes, avec les signatures complètes. Les surcharges y sont regroupées sous leur nom.
3. **La 1.11.22 : les raccourcis clavier** (votre § 14). Un gestionnaire central et configurable : profils, accords Ctrl+M,Ctrl+M et Ctrl+K,Ctrl+C, conflits signalés, retour aux valeurs par défaut, affichage dans les menus et les info-bulles. Puis les raccourcis d'édition, de recherche, de documents et de diagnostics du § 14. Par défaut (décision D5), votre profil actuel (validé le 02/10) reste le défaut, et un profil « Visual Studio » est proposé à côté.
