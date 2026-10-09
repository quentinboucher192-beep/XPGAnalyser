# XPGAnalyser 1.11.20 : les signatures (E/S, sorties, surcharges)

Livrée le 09/10/2026 en fin de matinée. Elle répond à vos deux demandes de ce matin :
- « les entrées/sorties c'est des REF_TO, et sorties c'est des paramètres aussi… il faut les compter dans paramètres » ;
- « les multi signatures sur les fonctions dans les scripts et les fonctions symboles » : c'est votre § 10, prévu au lot 8 et avancé.

**Un projet de la 1.11.19 s'ouvre tel quel**, toujours au format 23. Une surcharge s'enregistre comme toute fonction (une ligne, son identifiant). En revanche, **ne rouvrez pas en 1.11.19 un projet qui a des surcharges** : la 1.11.19 les dirait « fonction en double » et sa simulation prendrait la première.

Fichiers livrés :
- `XPGAnalyser-Setup-1.11.20.exe` (l'installateur) ;
- ce document ;
- les captures `11120_*`, prises sous Wine avec l'exe livré.

## 1. Ce qui change pour vous

- **Les paramètres E/S et sorties comptent.** Un paramètre `VAR_IN_OUT` (E/S, par référence) ou `VAR_OUTPUT` (sortie) est un paramètre comme une entrée.
  - `Random(0.0, 1.0, Graine, Tirage)` n'est plus refusé (« Random prend 2 arguments, pas 4 »).
  - La signature, la colonne et la propriété Paramètres, l'arbre, la bulle d'aide et Essayer les montrent tous, avec leur mode.
  - Dans un script, la fonction écrit bien dans `Graine` (E/S) et dans `Tirage` (sortie) : un essai le prouve.
- **Les surcharges.** Plusieurs fonctions peuvent porter le même nom si leurs paramètres diffèrent : leur nombre, leurs types ou leurs modes. Cela vaut pour les fonctions IHM, les fonctions d'un symbole et les fonctions internes d'un script (`FUNCTION … END_FUNCTION`).
  - **Nouvelle fonction** accepte un nom déjà pris.
  - Chaque appel prend la sienne : `Convertir(5)` va à la version INT, `Convertir(2.5)` à la version REAL, `Convertir('FF', 16)` à celle à deux paramètres.
  - L'éditeur, Compiler et la simulation appliquent **la même règle** : ils choisissent toujours la même surcharge.
- **Les fautes sont dites à leur place**, avec les formes possibles :
  - un argument manque : « Random : il manque RandomSeed (VAR_IN_OUT : une variable) » ;
  - une valeur calculée est passée là où il faut une variable : « RandomSeed est passé par référence (VAR_IN_OUT) : il faut une variable » ;
  - aucune surcharge ne convient : « aucune surcharge de Convertir ne prend cet appel » (le nombre d'arguments) ou « aucune surcharge de Convertir n'accepte ces types (STRING) » ; chacune dit pourquoi ;
  - l'appel est ambigu : « appel ambigu de Melanger (INT, INT) : … conviennent autant, précisez un type ».

## 2. Les paramètres E/S et sorties

| Mode | Dans l'appel | Obligatoire |
|---|---|---|
| Entrée (`VAR_INPUT`) | une valeur ou une expression | oui, sauf si elle a une valeur par défaut |
| E/S (`VAR_IN_OUT`) | une **variable** : la fonction travaille sur elle | oui |
| Sortie (`VAR_OUTPUT`) | une **variable** : la fonction l'écrit | non |

- Les arguments par position remplissent les paramètres **dans l'ordre de l'onglet Paramètres**, tous modes confondus. Les arguments par nom (`a := x`, `q => y`) remplissent leur paramètre ; `=>` est réservé aux sorties.
- **Essayer** demande une valeur pour chaque paramètre. Une E/S reçoit sa valeur de départ ; la case d'une sortie est ignorée. Après l'essai, la valeur finale des E/S et des sorties est montrée (« Paramètre rendu »).
- **Corrigé au passage** : les blocs `VAR_IN_OUT` et `VAR_OUTPUT` restaient dans le corps d'une fonction lue « à l'ancienne ». Une expression de vue qui appelait une telle fonction échouait donc. Ils sont maintenant lus comme des paramètres partout.

## 3. Les surcharges

**Deux surcharges doivent différer par leur forme** : le nombre de paramètres, leurs modes, leurs types. Ne comptent pas :
- **le type de retour** : `Calculer(INT) : INT` et `Calculer(INT) : REAL` ont la même forme ;
- **deux types que la simulation calcule de la même façon** : REAL et LREAL, INT et SINT, DINT et LINT, UINT et USINT, UDINT et ULINT.

Deux fonctions de même forme sont une faute, dite dans l'éditeur et par Compiler.

**Le choix d'une surcharge** (le module `hmi::overload`, une seule règle) :
1. **L'arité.** On garde les surcharges qui acceptent ce nombre d'arguments et ces noms. Une seule reste : c'est elle, sans regarder les types, comme une fonction non surchargée avant.
2. **La référence.** Une E/S, une sortie et un `REF_TO` veulent une variable. Une valeur calculée écarte la surcharge.
3. **Les types.** Chaque argument coûte sa conversion (la règle du lot 6) :

| Argument → paramètre | Coût |
|---|---|
| le même type ; un type inconnu ; ANY | 0 |
| un littéral entier vers son type naturel (`5` : INT ; `100000` : DINT) | 0 |
| un littéral entier vers un autre entier où il tient | 1 |
| une conversion élargie (INT → DINT, REAL → LREAL) | 1 ou 2 |
| un littéral entier vers un réel | 2 |
| avec perte possible (DINT → REAL, LREAL → REAL) ; un nombre pour un nombre, par référence | 4 |
| vers un entier plus petit (DINT → INT) | 8 |
| un réel vers un entier (la simulation arrondit à l'entier le plus proche, comme avant : 2.5 donne 3) | 16 |
| interdite (un texte pour un nombre, deux structures) | écartée |

La moins chère gagne. À égalité, l'appel est **ambigu**. Si un type inconnu cause l'égalité (une variable de l'automate non branché, par exemple), l'éditeur avertit et la simulation choisit avec le vrai type.

**Où.**
- Les fonctions IHM.
- Les fonctions d'un symbole, appelées par leur nom dans le symbole, par `SUPER.Nom`, par `Instance.Nom` ou par `Vue.Instance.Nom`.
- Les fonctions internes d'un script.
- Partout où elles s'appellent : scripts, fonctions, actions, expressions de vue.

**Renommer** une fonction renomme ses surcharges avec elle, et tous leurs appels : chaque appel garde sa cible. Renommer vers le nom d'une autre fonction reste refusé, parce que ses appels pourraient changer de cible ; créez plutôt la surcharge par **Nouvelle fonction**.

**La bulle d'aide** d'un appel montre toutes les signatures du nom. Celle qui prend l'argument en cours de frappe passe devant ; les autres sont listées dessous (« 1/3 »).

**À l'import d'un paquet**, une fonction se reconnaît à son nom **et** à sa forme. Une surcharge d'une autre forme s'ajoute. **Remplacer** ne touche que la fonction de même forme et laisse ses surcharges.

## 4. Une seule règle, au contrôle et à la simulation

- **`hmi::overload`** (`src/hmi/HmiOverload.hpp`) porte la règle : l'arité, la référence, les coûts, l'ambiguïté et la forme.
- **La simulation** déduit le type de chaque argument **sans l'évaluer** :
  - littéraux, locales, variables IHM, membres, cases, opérateurs et appels ;
  - pour une variable dont le type reste inconnu, le type de sa valeur.
  
  Elle demande ensuite la surcharge à l'IHM, qui applique `hmi::overload`. La surcharge choisie voyage sous une clé (`Convertir#615`) jusqu'à son exécution.
- **Le contrôle** (`src/hmi/HmiCallCheck.hpp`) lit le code comme la simulation : même lecteur, même déduction des types. Il juge chaque appel d'une fonction de l'utilisateur avec la même règle. Le contrôle « par mots » ne compte plus les arguments que si le code ne se lit pas (une faute de syntaxe).

## 5. Le parcours vérifié sous Wine

Une session jouée avec l'exe livré (`tools/sessions/session-11120-signatures.txt`), sur une copie d'Armoire_Gaz sans cache, celle de la 1.11.17. Avant la session, `tools/sessions/preparer-projet-11120.cpp` y ajoute :
- la fonction **Random** de votre capture : Min, Max (entrées), RandomSeed (E/S), test (sortie) ;
- trois **Convertir** : `(valeur : INT) : STRING`, `(valeur : REAL) : STRING`, `(texte : STRING; base : INT) : INT` ;
- le script **Essai_Signatures**, lancé au démarrage, qui les appelle et écrit à la Console ce que chaque appel a pris.

Les commandes de contrôle de la session (`volet-message`, `ihm-build-attendre`, `ihm-console-etat`) échouent si le résultat n'est pas celui attendu ; il n'y a eu aucun échec.

| Étape | Ce qui s'est passé | Capture |
|---|---|---|
| **1.** Fonctions IHM, **Random** | L'onglet **Paramètres 4**. L'en-tête de l'éditeur : `Random(Min : REAL; Max : REAL; VAR_IN_OUT RandomSeed : REAL; VAR_OUTPUT test : REAL) : REAL`. L'arbre montre Random et les trois Convertir avec leur signature | `11120_01` |
| **2.** **Nouvelle fonction** `Convertir`, retour REAL (son paramètre Entree est un REAL) | Elle est créée. La barre du volet : « Même forme que Convertir(REAL) : changez ses paramètres (onglet Paramètres) pour en faire une surcharge ». Diagnostics : « deux fonctions Convertir ont la même forme Convertir(REAL) » | `11120_02` |
| **3.** Onglet Paramètres, Entree passé en **BOOL** | La Signature devient `Convertir(Entree : BOOL) : REAL` ; « aucune erreur dans cette fonction ». Quatre Convertir, quatre formes | `11120_03` |
| **4.** Le script **Essai_Signatures** | « aucune erreur dans ce script » : `Random(0.0, 10.0, Graine, Tirage)` (4 arguments), `Convertir(5)`, `Convertir(2.5)`, `Convertir('FF', 16)` | `11120_04` |
| **5.** Une ligne tapée : `Texte_Entier := Convertir('texte');` | Soulignée, sa ligne en rouge. La bulle : « aucune surcharge de Convertir n'accepte ces types (STRING) - Convertir(INT) : valeur attend un INT, pas un STRING ; Convertir(REAL) : … ; Convertir(BOOL) : Entree attend un BOOL, pas un STRING ». Diagnostics : ligne 8, colonne 17. Ctrl+Z la retire | `11120_05` |
| **6.** La simulation | Le build réussit (320 éléments, 0 erreur). La Console, filtrée sur « Signatures » : « Convertir(5) = entier, Convertir(2.5) = reel, Convertir(FF, 16) = 16 » et « Random(0.0, 10.0, Graine, Tirage) : Graine 1.75, Tirage 5, rendu 5 ». Graine est passée de 1.0 à 1.75 (E/S écrite), Tirage vaut 5 (sortie écrite) | `11120_06` |

**Ce que la préparation de la session a montré.** Ma première version de l'étape 5 tapait `Convertir(Graine, 2)`. Ce n'est pas refusé : une seule surcharge prend deux arguments, et elle est prise sans regarder les types (le § 3). C'est la règle voulue, mais elle laisse passer un REAL là où un STRING est attendu : je l'ai mise dans les limites (§ 8), et le contrôle des types de tous les appels est prévu en 1.11.21.

## 6. Fichiers créés et modifiés

- **créés** :
  - `src/hmi/HmiOverload.hpp` et `.cpp` : la règle, les signatures, les formes, les surcharges mal distinguées ;
  - `src/hmi/HmiCallCheck.hpp` et `.cpp` : le contrôle des appels ;
  - `tests/hmi_overload_test.cpp` (l'essai `hmioverload`) ;
  - `tools/sessions/preparer-projet-11120.cpp` et `tools/sessions/session-11120-signatures.txt` (la session Wine) ;
- **modifiés** :
  - le moteur : `src/sim/Interpreter.hpp` et `.cpp` (le type d'un argument sans l'évaluer, les appels d'une section, les fonctions internes de même nom, le choix par l'environnement) ;
  - l'IHM : `HmiScript` (VAR_IN_OUT et VAR_OUTPUT lus comme paramètres), `HmiRuntime` (le choix, la clé, l'essai), `HmiExpr` (les expressions de vue), `HmiModel` et `HmiSymbols` (les fonctions d'un nom, par clé), `HmiScriptCheck`, `HmiScriptCheck110`, `HmiExprCheck`, `HmiCheck`, `HmiPipeline` (la compilation incrémentale), `HmiPackage` (l'import) ;
  - les écrans : `HmiFunctionPanes` (créer, renommer, virtuelle, diagnostics, Essayer), `HmiAssist` (la bulle), `HmiTreeData` (l'arbre), `HmiWorkspace` (le dialogue Essayer), `ui/widgets/Controls` (la bulle à plusieurs signatures) ;
  - le guide (`HmiGuideText.cpp` : fonctions), les notes (`ReleaseNotes.cpp`, 3 notes), la version 1.11.20 (`CMakeLists.txt`, `Version.hpp`, `HmiPackage.hpp`, `xpg_analyzer.rc`, `XPGAnalyser.iss`, `config.ini`, `CodeStats.hpp`, `dist/SHA256SUMS.txt`), le projet Visual Studio ;
  - les essais existants mis à jour : `hmi_test.cpp`, `hmi_editor_test.cpp`, `hmi_decl_test.cpp`.

## 7. Les tests ajoutés

- **`hmioverload`** (`hmi_overload_test`, 69 contrôles) :
  - les modes : `parameters()`, les blocs blanchis, VAR_IN_OUT refusé hors d'une fonction, la signature ;
  - l'arité et la référence : 4 arguments pour Random, l'E/S due, une valeur pour une E/S, trop d'arguments, par nom, `=>`, une entrée facultative, `REF_TO` ;
  - la règle des types : les coûts, le littéral entier ;
  - le choix : par le nombre, par le type, aucune, ambigu, incertain, le mode qui départage ;
  - les formes : REAL et LREAL, INT et SINT, le retour seul ;
  - **le moteur** : un script appelle `Random(0.0, 10.0, Seed, Draw)`, et `Seed` passe de 1.0 à 2.0 (E/S) et `Draw` à 5.0 (sortie). Il vérifie aussi les trois surcharges de Convertir à l'exécution, une expression de vue, deux fonctions internes `Double`, l'appel ambigu dit à l'exécution, et Essayer par identifiant (E/S et sorties rendues).
- **`hmieditor`**, *surcharges1120* (16 contrôles) :
  - la colonne et la propriété Paramètres, la Signature ;
  - Nouvelle fonction d'un nom pris ;
  - le contrôle d'un script ;
  - la bulle d'aide, l'arbre ;
  - renommer le groupe, puis Ctrl+Z ;
  - renommer vers un nom pris ;
  - deux formes identiques (INT et SINT).
- **Les essais existants mis à jour**, sans perdre ce qu'ils contrôlent :
  - le message d'arité nomme maintenant le paramètre qui manque ;
  - le doublon et le conflit d'import portent sur des formes identiques ;
  - une fonction interne appelée sans son E/S, refusée jusqu'ici par la seule simulation, est dite.

## 8. Les limites

- **Rien n'a été lancé sur un vrai Windows** : tout a été vérifié sous Wine.
- **Une seule surcharge du bon nombre d'arguments est prise sans contrôler les types**, comme une fonction non surchargée avant : `Convertir(Graine, 2)` prend `Convertir(STRING, INT)` sans rien dire, alors que Graine est un REAL. Le contrôle des conversions interdites pour tous les appels (votre § 7 : « compatibilité des arguments, conversions interdites ») viendra en 1.11.21, d'abord en avertissement, pour ne bloquer la simulation d'aucun projet existant.
- **Renommer une seule surcharge** n'est pas possible : on renomme le groupe. Séparer les appels d'une seule surcharge viendra avec le renommage symbolique (lot 10).
- **« Appelée par »** liste les appels du nom, toutes surcharges confondues.
- **Une fonction virtuelle d'un symbole ne se surcharge pas** : ses redéfinitions la désignent par son nom.
- **La bulle d'aide** des fonctions d'un symbole, dans le symbole, n'existe toujours pas (comme avant).
- **Essayer** passe des valeurs : une E/S ou une sortie de type riche (ARRAY, REF_TO, structure) ne s'essaie pas au banc. Appelez-la depuis un script.
- **Dans une expression de vue**, le type d'un paramètre de vue sans type est inconnu. Si deux surcharges conviennent alors autant, l'éditeur avertit et la simulation choisit avec la valeur.

## 9. Tester vous-même

1. Fonctions IHM : créez une fonction avec deux entrées, une E/S et une sortie (onglet Paramètres, colonne Mode). La colonne Paramètres et la Signature montrent les quatre.
2. Dans un script : `x := MaFonction(1.0, 2.0, Graine, Tirage);`. Rien n'est refusé ; en simulation, `Graine` et `Tirage` changent.
3. **Nouvelle fonction** `Convertir` (retour REAL : son paramètre Entree est un REAL), puis une seconde **Nouvelle fonction** `Convertir` : elle a la même forme, c'est dit. Passez son paramètre en INT : deux surcharges, `Convertir(5)` prend la version INT et `Convertir(2.5)` la version REAL.
4. Tapez `Convertir('texte')` : aucune des deux ne prend un texte, la faute est dite à sa place, avec la raison de chacune.
5. Tapez `Convertir(` : la bulle montre les deux signatures.

## Installer

1. Lancez `XPGAnalyser-Setup-1.11.20.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.20.exe` (15,2 Mo, 15 983 202 octets) :
`29431730470B3D531355C37ABB27B18C8D3F42885A7E1F4500C717A753393358`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (66 sur 66). Elle comprend le nouvel essai `hmioverload` (69 contrôles), les 6 521 contrôles de `hmieditor`, les 4 680 de `hmi`, les 890 de `hmidecl`, les 84 de `hmitypes`, les 67 de `hmicheckexpr` et les deux traces de référence, `hmitrace` et `hmitracemig`.

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.20` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session du § 5 a été rejouée avec l'exe livré : 6 captures `11120_*`, aucun contrôle en échec.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé pour tous les comptes, par-dessus la 1.11.19 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.20`.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Le § 9 donne le parcours à refaire chez vous.

## La suite

1. **La 1.11.21 : les éditeurs.**
   - Les bandeaux Essai et Diagnostics quittent les éditeurs : les erreurs utiles vont au panneau du bas (Diagnostics, un clic mène à la ligne), le résultat d'Essayer aux Sorties.
   - Les explorateurs (application, vue, objets) déplient :
     - un script en constantes, variables et fonctions internes ;
     - une fonction en paramètres (avec leur mode), locales et constantes ;
     - avec les signatures complètes, et les surcharges regroupées sous leur nom.
   - Le contrôle des types des arguments pour tous les appels, même quand une seule surcharge a le bon nombre d'arguments (voir les limites, § 8).
   - J'y corrigerai aussi ce que la session de la 1.11.19 a montré : changer le type d'une déclaration garde sa valeur initiale, même quand elle ne convient plus (`0` pour un `ARRAY[1..4] OF T_Four`).
2. **La 1.11.22 : les raccourcis clavier** (votre § 14). Votre profil actuel reste le défaut, et un profil « Visual Studio » est proposé à côté.
