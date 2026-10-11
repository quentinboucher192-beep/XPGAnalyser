# 1.12.7 — Les natives des conteneurs, std::numeric, les templates et `<bit>`

Ce document dit ce qui a été fait pour la partie « natives » de la 1.12.7 (la maquette 1.12.7,
versions 2 et 3 : `v3_data.js`, `v3_cont.js`, `v2_num.js`, `v2_templ.js`, `v2_analyse.js`,
`v3_types.js`), ce que l'utilisateur voit (pour les notes de version), les contrôles de
Compiler et les limites connues. Il ne touche ni aux constantes calculées au démarrage ni
aux grilles de déclaration (un autre chantier de la 1.12.7).

## Ce qui est fait

### Le moteur (le dialecte de l'IHM)

- **`src/sim/DialectLib.hpp/.cpp` (nouveau)** : la bibliothèque du dialecte, lue au même
  endroit par l'interpréteur, Compiler et l'aide à la saisie :
  - les **templates** : `NUMERIC_LIMITS<T>.MEMBRE` (15 types : SINT INT DINT LINT USINT UINT
    UDINT ULINT BYTE WORD DWORD LWORD REAL LREAL BOOL ; 32 membres : 9 fonctions appelées avec
    `()` — MIN MAX LOWEST EPSILON ROUND_ERROR INFINITY QUIET_NAN SIGNALING_NAN DENORM_MIN — et
    23 constantes sans parenthèses ; leurs valeurs, celles du C++ libstdc++ x86, comme la
    maquette `limVal`), `NUMBERS[<T>].CONSTANTE` (REAL ou LREAL, LREAL sans `<T>` ; 13
    constantes), `SATURATE_CAST<T>(x)` (T obligatoire, un entier), `ADD_SAT`, `SUB_SAT`,
    `MUL_SAT`, `DIV_SAT`, `MIDPOINT` avec un `<T>` facultatif (sinon le type de `a`) ;
  - les **énumérations** `COMPARAISON` (Egal, Different, Inferieur, InferieurOuEgal,
    Superieur, SuperieurOuEgal), `OPERATION` (Somme, Produit, Min, Max, Et, Ou),
    `TRANSFORMATION` (Identite, Abs, Carre, Racine, Neg, Inverse) — `NOM#Valeur` vaut le rang
    de la valeur (un DINT), lu par l'environnement de l'IHM (le catalogue) ou, à défaut, par
    l'interpréteur lui-même ;
  - la **table des natives de la 1.12.7** (67) : leur nombre d'arguments, leur signature, leur
    phrase, si elles écrivent leur premier argument, si elles changent sa taille (un ARRAY est
    alors refusé), si elles lisent un ARRAY ;
  - le calcul entier **exact** des saturations et de `MIDPOINT` sans `__int128` (le projet se
    construit aussi sous MSVC) ;
  - « veux-tu dire … ? » (la distance d'édition de la maquette).
- **`src/sim/Interpreter.cpp`** :
  - `NOM<TYPE>` se lit **seulement** pour les noms des templates et seulement sous la forme
    `NOM < IDENTIFIANT >` : `a < b`, `Compteur<10` ne changent pas. Le type refusé, le membre
    inconnu, les parenthèses en trop ou en moins sont dits à la lecture du script (donc par
    Compiler et au démarrage) ;
  - les **43 natives des conteneurs** de la maquette v3 : LIST_SET, MAP_SET, TUPLE_GET,
    TUPLE_SET, MAP_FIRST_KEY, MAP_LAST_KEY, TUPLE_SIZE, MAP_INSERT, LIST_INSERT_SORTED,
    LIST_POP_FIRST, LIST_REMOVE_ALL, LIST_REMOVE_IF, MAP_REMOVE_IF, LIST_DISTINCT,
    MAP_INCREMENT, LIST_APPEND, MAP_MERGE, LIST_FILL, LIST_LAST_INDEX_OF, MAP_KEY_OF,
    LIST_COUNT_OF, LIST_FIND_IF, LIST_COUNT_IF, LIST_ALL, LIST_ANY, LIST_NONE, LIST_MIN_INDEX,
    LIST_MAX_INDEX, MAP_FLOOR_KEY, MAP_CEIL_KEY, LIST_SORT_BY, LIST_IS_SORTED, LIST_SWAP,
    LIST_MOVE, LIST_ROTATE, LIST_SLICE, LIST_CONCAT, LIST_COPY, TO_LIST, MAP_VALUES, MAP_ITEMS,
    TUPLE_TO_STRING, TUPLE_UNPACK. Les fonctions qui écrivent le font **en place**, comme
    LIST_ADD : une variable du script, une variable IHM objet (LIST, MAP), un tableau IHM (copié
    puis recopié après l'écriture) ; une constante est refusée. L'argument `membre` ('Poids')
    trie ou compare un membre d'une liste de structures ; 'Item2', un champ d'une liste de
    TUPLE. La valeur cherchée ou comparée (LIST_COUNT_OF, LIST_LAST_INDEX_OF, LIST_REMOVE_ALL,
    MAP_KEY_OF, MAP_REMOVE_IF…) est d'abord mise dans le type des éléments, comme une valeur
    écrite : `LIST_COUNT_OF(Mesures, 8)` compte les 8.0, `LIST_COUNT_OF(Mesures, 'abc')` s'arrête
    sur « 'abc' n'est pas un REAL » ;
  - `LIST_SORT` d'une liste de structures dit quoi faire : « LIST_SORT : des T_Lot —
    LIST_SORT_BY(L, 'Poids') trie par un membre » (avant : « une liste de valeurs simples est
    attendue ») ;
  - les **10 natives LIST_ d'avant** (LIST_GET, LIST_CONTAINS, LIST_INDEX_OF, LIST_MIN,
    LIST_MAX, LIST_SUM, LIST_AVG, LIST_SORT, LIST_REVERSE, JOIN) lisent aussi un **ARRAY**, à ses
    indices (LIST_GET(Tab, 1, 0) sur un ARRAY[1..5] lit la première case) ; LIST_SORT et
    LIST_REVERSE l'écrivent en place ;
  - une native qui change la taille reçoit un ARRAY : « LIST_ADD : un ARRAY a une taille fixe
    (5 cases) — une LIST ou un VECTOR est attendu » (avant : « une liste ou un vecteur est
    attendu… ») ;
  - `MAP_GET(m, clé)` sans défaut : une clé absente est une erreur (« MAP_GET : clé absente :
    'clou' ») ;
  - les **19 fonctions de std::numeric** : ACCUMULATE, REDUCE, INNER_PRODUCT,
    TRANSFORM_REDUCE, PARTIAL_SUM, INCLUSIVE_SCAN, EXCLUSIVE_SCAN, TRANSFORM_INCLUSIVE_SCAN,
    TRANSFORM_EXCLUSIVE_SCAN, ADJACENT_DIFFERENCE, IOTA, GCD, LCM, MIDPOINT, ADD_SAT, SUB_SAT,
    MUL_SAT, DIV_SAT, SATURATE_CAST<T> (SATURATE_TO_INT, d'un brouillon, n'existe pas) ;
  - **`<bit>`** : BIT_COUNT (std::popcount), BIT_FIRST (std::countr_zero, −1 si aucun bit),
    BIT_TEST, BIT_SET, BIT_RESET (des fonctions pures : elles rendent la nouvelle valeur) — sur
    BYTE, WORD, DWORD, LWORD et les entiers (une SINT déclarée garde ses 8 bits) ;
  - une **fonction du projet** nommée comme une nouvelle native (GCD, IOTA…) passe avant elle :
    ce qui tournait en 1.12.6 tourne pareil ;
  - **les constantes** : chaque native qui écrit son premier argument (ou, pour LIST_COPY, sa
    destination ; pour TUPLE_UNPACK, ses variables) refuse une constante : « MAP_SET : une
    constante ne s'écrit pas ». **MAP_REMOVE et MAP_CLEAR** aussi : avant, leur premier argument
    perdait le drapeau « constante » de son emplacement, et `MAP_REMOVE(m, 'k')` changeait une
    `VAR CONSTANT m : MAP…` sans un mot. Une constante qu'un environnement donnera en place (un
    emplacement constant) sera refusée de même : le point de branchement est `Runner::locate`
    (`src/sim/Interpreter.cpp`, cas `Expr::Kind::Reference`), qui construit le `Place` d'un nom de
    l'environnement avec `constant = false` — y lire `Environment::constantVariable(nom)` suffit,
    toutes les natives qui écrivent lisent ce drapeau.

Toutes ces natives marchent aussi dans une expression de vue (fx) et un texte à trous (`{GCD(a,
b)}`, `{NUMBERS.PI}`) : une expression lit une copie des objets, une native qui écrit n'y change
rien (Compiler le dit).

### Les fiches (branche Natives)

- `outils/natives/catalogue.py` : une fiche par nouvelle native (69 : 43 conteneurs, 19
  std::numeric, NUMERIC_LIMITS, NUMBERS, 5 de `<bit>`) — la phrase, les paramètres, les notes, ST,
  C, C++, un essai qui s'exécute dans le moteur. Les conteneurs dans leurs catégories (« Listes,
  vecteurs et tuples » pour les LIST_ et les TUPLE_, « MAP et itérateurs ») ; un nouveau groupe
  **« Bibliothèques »** avec trois catégories : std::numeric, std::numeric_limits, std::numbers ;
  les fonctions de `<bit>` dans « Bits et décalages ». Les énumérations COMPARAISON, OPERATION,
  TRANSFORMATION.
- Les 10 fiches LIST_ étendues disent l'ARRAY (« LIST, VECTOR, ARRAY », à ses indices).
- **La fiche MAP_GET est corrigée** : elle disait « m['cle'] en lecture crée la clé » ; le
  moteur, lui, s'arrête sur « clé absente de la MAP ». Elle dit maintenant : lire `m['cle']` ne
  crée pas la clé (une clé absente arrête le script ; MAP_HAS teste, MAP_GET rend le défaut) ;
  écrire `m['cle'] := v` la crée.
- `generer.py` : `expr=False` (IOTA écrit son argument : pas dans une expression fx).
  `src/hmi/HmiNativesData.cpp` régénéré (220 fonctions, 62 énumérations).

### L'aide à la saisie (éditeur ST et champs)

- Les nouvelles natives sont proposées avec leur signature et leur phrase (le dialecte :
  `lang110::builtins()`), dans un champ fx celles qui s'écrivent dans une expression.
- Après `NUMERIC_LIMITS<` : les 15 types (leur nom en C, ce qu'ils sont) ; le choix pose
  `UINT>.` et la liste se rouvre sur les membres. Après `NUMERIC_LIMITS<UINT>.` : les 32 membres
  avec leur valeur (« = 65535 »), leur phrase et leur C++ ; une fonction se pose avec ses `()`.
  Après `NUMBERS.` ou `NUMBERS<REAL>.` : les 13 constantes et leur valeur (LREAL ou REAL).
  Après `SATURATE_CAST<` : les 12 entiers (le choix pose `INT>(`) ; `NUMBERS<` : REAL, LREAL.
- Choisir NUMERIC_LIMITS pose `NUMERIC_LIMITS<` (puis les types), NUMBERS pose `NUMBERS.`,
  SATURATE_CAST pose `SATURATE_CAST<`.
- **Dans un script C++** (la même aide, les formes `std::`) : après `std::numeric_limits<`, les
  types (le choix pose `UINT>::` et rouvre la liste) ; après `std::numeric_limits<UINT>::`, les
  32 membres sous leur nom C++ (`max()`, `digits`) avec leur valeur pour ce type —
  `std::numeric_limits<int16_t>::` se lit comme INT ; après `std::numbers::`, les 13 constantes
  (`pi`, `e`, `sqrt2`…) et leur valeur en double.

## Pour les notes de version (ce que l'utilisateur voit)

- **43 natives de plus pour les conteneurs** : lire et écrire sans erreur (LIST_SET, MAP_SET,
  TUPLE_GET, TUPLE_SET), chercher, compter et retirer sous condition avec l'énumération
  COMPARAISON (LIST_FIND_IF, LIST_COUNT_IF, LIST_ALL, LIST_ANY, LIST_NONE, LIST_REMOVE_IF,
  MAP_REMOVE_IF), trier une liste de structures par un membre (LIST_SORT_BY, stable),
  déplacer, échanger, faire tourner, extraire, copier vers un ARRAY (LIST_COPY), MAP_INCREMENT,
  MAP_FLOOR_KEY (une table de paliers), MAP_ITEMS, TUPLE_UNPACK, TUPLE_TO_STRING…
- **Les fonctions LIST_ qui ne changent pas la taille lisent aussi un ARRAY**, à ses indices.
- **std::numeric en ST** (19 fonctions jusqu'au C++26 : ACCUMULATE, REDUCE, les SCAN, IOTA,
  GCD, LCM, MIDPOINT, le calcul saturé ADD_SAT… SATURATE_CAST<T>), avec les énumérations
  OPERATION et TRANSFORMATION.
- **Les templates** : `NUMERIC_LIMITS<UINT>.MAX()`, `NUMBERS.PI`, `NUMBERS<REAL>.PI`,
  `SATURATE_CAST<INT>(x)`, `ADD_SAT<INT>(a, b)`, avec l'aide à la saisie des types puis des
  membres et de leur valeur, et leurs fautes dites par Compiler.
- **Les bits d'un mot** : BIT_COUNT, BIT_FIRST, BIT_TEST, BIT_SET, BIT_RESET.
- **La fiche MAP_GET dit vrai** : lire une clé absente arrête le script ; `MAP_GET(m, clé)` sans
  défaut aussi.
- **Une constante ne s'écrit plus par une native** : MAP_REMOVE et MAP_CLEAR changeaient une MAP
  déclarée VAR CONSTANT ; elles la refusent, comme LIST_ADD et toutes les nouvelles natives qui
  écrivent. Compiler le dit avant la marche (« Seuils est une constante : LIST_SET ne peut pas la
  changer »).
- La branche Natives a un groupe « Bibliothèques » (std::numeric, std::numeric_limits,
  std::numbers) et une fiche par nouvelle fonction (ST, C, C++, un essai).

## Les contrôles (Compiler, l'éditeur)

La règle de la 1.12.6 est tenue : **ce qui compilait et démarrait en 1.12.6 démarre toujours**.
Une faute dans du code d'avant est un **avertissement** ; une faute avec une nouvelle native ou un
template, une **erreur**.

| Contrôle | Nouvelle native / template | Native d'avant |
|---|---|---|
| Nombre d'arguments (« LIST_SET prend 3 arguments, pas 2 ») | erreur | avertissement (LIST_, VECTOR_, JOIN, SPLIT, MAP_GET) |
| Un ARRAY donné à une native qui change la taille (« … un ARRAY a une taille fixe (5 cases) — une LIST ou un VECTOR est attendu ») | erreur (LIST_INSERT_SORTED, LIST_POP_FIRST, LIST_REMOVE_ALL, LIST_REMOVE_IF, LIST_DISTINCT, LIST_APPEND) | avertissement (LIST_ADD, LIST_INSERT, LIST_REMOVE…, VECTOR_…) |
| Le membre d'un tri ou d'une comparaison, le type de la liste connu (« T_Lot n'a pas de membre Poid — veux-tu dire Poids ? », « un TUPLE(STRING, REAL) a Item1 à Item2, pas 'Item3' », « des REAL n'ont pas de membres ») | erreur | — |
| TUPLE_GET / TUPLE_SET, un numéro constant hors des champs (« TUPLE_GET : TUPLE(STRING, REAL) a 2 champs, pas 3 ») | erreur | — |
| Le type d'un template (« NUMERIC_LIMITS<STRING> : un type numérique est attendu (SINT … ULINT, BYTE … LWORD, REAL, LREAL, BOOL) » ; un nom qui n'est pas un type, `NUMERIC_LIMITS<UINTT>` : « UINTT n'est pas un type — veux-tu dire UINT ? »), un membre inconnu (« NUMERIC_LIMITS<UINT> n'a pas de membre MAXX — veux-tu dire MAX ? »), les parenthèses (« DIGITS est une constante : sans parenthèses », « MIN est une fonction : NUMERIC_LIMITS<UINT>.MIN() ») | erreur (à la lecture du script) | — |
| Le `>` oublié devant un nom de type (« NUMERIC_LIMITS : le type, puis > — NUMERIC_LIMITS<UINT>.MAX() », « SATURATE_CAST : le type, puis > — SATURATE_CAST<INT>(x) ») ; une variable nommée comme un template (`Midpoint < 5`) reste une comparaison | erreur | — |
| Le type oublié (« SATURATE_CAST attend son type : SATURATE_CAST<INT>(x) », « NUMERIC_LIMITS attend son type : NUMERIC_LIMITS<UINT>.MAX() »), une constante inconnue de NUMBERS | erreur | — |
| Une valeur d'énumération inconnue (« COMPARAISON#Plus n'existe pas ») | erreur | — |
| Une constante (VAR CONSTANT) qu'une native écrirait — son premier argument, la destination de LIST_COPY, les variables de TUPLE_UNPACK (« Seuils est une constante : LIST_SET ne peut pas la changer », la maquette) ; en marche, le moteur s'arrête (« LIST_SET : une constante ne s'écrit pas ») | erreur | avertissement (LIST_ADD, LIST_INSERT, LIST_REMOVE…, LIST_SORT, LIST_REVERSE, VECTOR_…, MAP_REMOVE, MAP_CLEAR) |

Dans une expression fx, une nouvelle native qui écrit son argument est signalée (« change son
objet : une expression en lit une copie… »), comme LIST_ADD avant. Les templates et
`NUMBERS.PI` ne sont pas des « variables inexistantes » (ni pour Compiler, ni pour les racines
d'une expression).

## Les essais

- `tests/hmi_natives1127_test.cpp` (nouveau, `ctest -R hminatives1127`) : chaque native dans un
  script du moteur de l'IHM — le cas ordinaire, le bord (liste vide, indice hors de la liste,
  bornes), la faute dite comme la maquette ; les 10 LIST_ sur un ARRAY et sur un tableau IHM
  écrit en place ; std::numeric ; les templates et leurs fautes ; `<bit>` ; les énumérations ; une
  fonction du projet nommée GCD passe avant la native ; Compiler (erreurs et avertissements) ; un
  projet entier (`compileWith`) avec une structure T_Lot, LIST_SORT_BY par 'Poids' en marche ;
  les constantes refusées par les 25 natives qui écrivent (en marche et par Compiler). 269 contrôles.
- `tests/hmi_natives_test.cpp` : chaque essai des 69 nouvelles fiches s'exécute (script, et pour
  les fonctions pures : expression fx et texte à trous) — 2150 contrôles.
- `tests/hmi_editor_test.cpp` (`saisieNatives1127`, aussi par `HMI_TEST_NATIVES=1`) : l'aide à la
  saisie des templates et des nouvelles natives, les formes C++ `std::numeric_limits<T>::` et
  `std::numbers::`, les valeurs de `COMPARAISON#` et `OPERATION#`.

## Limites connues, écarts avec la maquette

- **C et C++** : l'aide à la saisie de l'IHM est la même pour les trois langages (elle n'a pas
  de liste par langage). Elle reconnaît en plus les formes C++ `std::numeric_limits<T>::` et
  `std::numbers::` (voir plus haut) ; les autres équivalents `std::` / C (`std::gcd`,
  `std::accumulate`, `INT16_MAX`, `M_PI`…) sont dans les fiches (onglets C et C++) et dans le
  détail des membres de NUMERIC_LIMITS / NUMBERS, pas proposés à part. `std::numbers::pi_v<float>`
  n'est pas proposé (`NUMBERS<REAL>.PI` en ST).
- **ACCUMULATE plie dans le type de INIT**, comme le C++ : `ACCUMULATE(Mesures, 0)` plie en
  entiers (chaque pas arrondi comme une affectation ST) ; la fiche le dit (0.0 pour des réels).
- **IOTA** rend le nombre de cases remplies (DINT) ; la maquette dit seulement « (remplit T) ».
- **Les messages d'un objet** disent « un VECTOR » là où la maquette écrit « une VECTOR ».
- **Les types refusés d'un template** ne sont pas proposés (grisés dans la maquette) : la liste
  ne montre que les types acceptés.
- **Un nom qui n'est pas un type dans `<…>`** : « X n'est pas un type — veux-tu dire Y ? » quand un
  type accepté en est proche (la même distance que la maquette : `UNIT` propose INT, le premier à
  deux lettres) ; sans type proche, le type attendu est dit (« NUMERIC_LIMITS<FOO> : un type
  numérique est attendu… ») — la lecture du script ne connaît pas les types du projet, et
  `SATURATE_CAST<T_LOT>` est bien un type, refusé.
- **Les essais de la maquette** (les 153 appels du bac d'essai des conteneurs, `tries` de
  `v3_data.js`) ont été passés dans le moteur et comparés : mêmes résultats, mêmes variables
  changées, mêmes erreurs, sauf `Tab[7]` (un message d'avant la 1.12.7 : « hors des bornes 0..4 »,
  la maquette écrit « [0..4] »).
- **Un ARRAY à deux dimensions** : les fonctions qui prennent ou rendent un indice (LIST_SET,
  LIST_FIND_IF, LIST_SWAP, LIST_SLICE…) le refusent (« un ARRAY à une dimension est attendu ») ;
  celles qui lisent les valeurs (LIST_SUM, LIST_CONTAINS, TO_LIST — ligne par ligne —, LIST_FILL)
  le prennent.
- **Compiler ne connaît le type d'une liste** que si elle est un nom (une locale, une variable
  IHM) : `LIST_SORT_BY(Recette.Lots, 'Poid')` n'est dit qu'en marche.
- **INFINITY, QUIET_NAN, SIGNALING_NAN** rendent l'infini et des NaN du double ; l'IHM les écrit
  « inf », « nan » (l'aide à la saisie montre « +INF », « NAN », « SNAN » comme la maquette).
- **SET et RING** (les types proposés de la maquette v3) ne font pas partie de ce lot.
