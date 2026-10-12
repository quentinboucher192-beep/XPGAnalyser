# 1.12.7 — SET OF T et RING[N] OF T (le choix 5 de la maquette v3)

Ce document dit ce qui a été fait pour les deux nouveaux types de conteneurs que la maquette
1.12.7 (version 3, `docs/maquettes/maquette-constantes-numeric.html`, le tableau `NTYPES` et la
démonstration `renderTdemo`) marque « Proposé » : **`SET OF T`** (std::set) et **`RING[N] OF T`**
(un tampon circulaire). Il dit ce que l'utilisateur voit (pour les notes de version), les contrôles
de Compiler, les essais et les limites connues. Il ne touche ni à la version ni aux notes de version
(`src/help/ReleaseNotes.cpp`) : le coordinateur s'en charge.

## Le choix de construction

Un SET et un RING sont des **listes du moteur** (`sim::TypeDesc::Kind::List`) avec une forme :
`name` vaut `"SET"` ou `"RING"`, et `capacity` garde le N d'un RING (`sim::setType`,
`sim::ringType`, `sim::isSetType`, `sim::isRingType`, `src/sim/Rich.hpp`). Tout ce qui marche sur
une liste marche donc sur eux sans rien changer : l'affichage (`[..]`), le texte d'un littéral,
FOR EACH, `.Count`, `.First`, `.Last`, `S[i]` en lecture, les natives LIST_ qui lisent, les copies,
la comparaison `=`, le déballage de la grille, la simulation, une variable IHM objet.

La forme se garde à **chaque écriture** par une seule fonction, `sim::normalizeList(Obj&)` : un SET
se range (l'ordre de `<` des valeurs simples, comme std::set ; les textes octet par octet, donc
`'B' < 'a'`) et perd ses doublons (la première reste) ; un RING garde ses N dernières valeurs (les
plus anciennes, au début, partent). Elle est appelée :

- quand une valeur entre d'un coup : un littéral, une affectation d'une LIST, d'un ARRAY, d'un autre
  SET ou RING (`assignObj`, `fromSequence`), `S + [x]` ;
- après une native qui écrit (LIST_ADD, LIST_INSERT, LIST_APPEND… : un garde-fou en fin de native,
  et le résultat rendu tient compte de la forme : `LIST_ADD` sur un RING plein rend N).

## Ce qui est fait

### Le moteur (`src/sim`)

- **Les types** : `SET OF T` et `RING[N] OF T` se lisent dans une déclaration (VAR, VAR_TEMP,
  CONSTANT, un paramètre, un retour, un membre, une variable IHM). N est une constante entière de 1
  à **100000** (`sim::kRingMax` ; une constante déclarée plus haut aussi : `RING[N_MAX] OF REAL`).
  Les fautes sont dites en français à la lecture du script : « RING : sa capacité est attendue, le
  nombre de valeurs gardées — RING[100] OF REAL », « RING[0] : la capacité va de 1 à 100000 »,
  « RING[N] : N est une constante entière ('i' n'est pas une constante entière déclarée plus
  haut) ». Un SET range des **valeurs simples** (nombres, textes, durées, dates, une énumération) :
  « SET OF T_Lot : un SET range des valeurs simples (…) — pour des structures, une LIST et
  LIST_SORT_BY ».
- **Un nom seul reste un nom** : `SET` et `RING` ne sont des types que suivis de `OF` (SET) ou de
  `[` (RING). Une variable nommée `Set` ou `Ring`, un type IHM nommé `SET` gardent leur sens.
- **Les natives** (la table de `src/sim/DialectLib.cpp`, groupes `"set"` et `"ring"`) :

  | Native | Rend | Ce qu'elle fait |
  |---|---|---|
  | `SET_ADD(S, x)` | BOOL | range x à sa place ; TRUE si elle est nouvelle, FALSE (rien ne change) si elle y était |
  | `SET_REMOVE(S, x)` | BOOL | retire x ; TRUE si elle y était |
  | `SET_HAS(S, x)` | BOOL | x est dans le SET |
  | `SET_UNION(A, B)` | SET OF T | un SET neuf (std::set_union) |
  | `SET_INTERSECTION(A, B)` | SET OF T | un SET neuf (std::set_intersection) |
  | `SET_DIFFERENCE(A, B)` | SET OF T | un SET neuf : A sans B (std::set_difference) |
  | `RING_ADD(R, x)` | DINT | ajoute x à la fin ; plein, la plus ancienne part ; rend le nombre de valeurs (au plus N) |

  La valeur est d'abord mise dans le type des éléments, comme une valeur écrite (« SET_ADD : 'abc'
  n'est pas un INT », « SET_ADD : 5 n'est pas une STRING : écris '5' », « RING_ADD : 2.5 n'est pas
  un entier (INT) »). SET_ADD, SET_REMOVE, SET_HAS veulent un SET (« SET_ADD : un SET est attendu
  (SET OF T), pas une LIST — LIST_CONTAINS, LIST_ADD, LIST_REMOVE pour une liste ») ; RING_ADD un
  RING (« … pas une LIST — LIST_ADD ajoute à une liste »). A et B de SET_UNION… peuvent être des
  SET, ou une LIST, un RING, un ARRAY (rangés, sans doublon ; les valeurs de B prennent le type de
  celles de A). Une fonction du projet nommée SET_ADD passe avant la native (comme GCD, IOTA).
- **Les membres** : `.Count` (SET et RING), `.Capacity` (N) et `.Full` (BOOL) d'un RING ; `.First`,
  `.Last` (la plus petite et la plus grande d'un SET ; la plus ancienne et la plus récente d'un
  RING). `S.Capacity`, `L.Full` d'une LIST : « 'Capacity' : un SET OF STRING n'a pas ce membre ».
- **FOR EACH** parcourt un SET dans l'ordre, un RING de la plus ancienne à la plus récente
  (`FOR EACH i, x IN R` : l'indice 0 est la plus ancienne).
- **LIST_ADD (et VECTOR_PUSH)** : sur un RING, comme RING_ADD (rend le nombre, au plus N) ; sur un
  SET, la valeur se range à sa place, une seule fois (rend le nombre) — un code qui faisait
  `IF NOT LIST_CONTAINS(L, x) THEN LIST_ADD(L, x); END_IF` marche tel quel si L devient un SET.
- **Les natives LIST_ qui lisent** (LIST_AVG, LIST_MIN, LIST_MAX, LIST_SUM, LIST_CONTAINS,
  LIST_INDEX_OF, LIST_GET, LIST_FIRST, LIST_LAST, LIST_COUNT, JOIN, LIST_COUNT_IF, LIST_FIND_IF,
  LIST_IS_SORTED, LIST_SLICE, TO_LIST, ACCUMULATE, PARTIAL_SUM…) lisent un SET et un RING. Celles
  qui rendent une suite neuve rendent une LIST (PARTIAL_SUM d'un SET : une LIST, ses sommes ne sont
  ni rangées ni uniques).
- **Ce qu'un SET refuse** (une erreur, la phrase dit quoi faire) :
  - écrire un élément en place : `S[0] := x`, `S.First := x`, la variable d'un `FOR EACH x IN S`,
    `ADR(S[0])`, `S[0]` passé à un VAR_IN_OUT : « un élément d'un SET ne s'écrit pas en place —
    SET_REMOVE(S, ancienne) puis SET_ADD(S, nouvelle) : le SET reste rangé, chaque valeur une
    fois » (S[i] se **lit** : la i-ème plus petite valeur) ;
  - les natives qui écrivent à une place ou réordonnent : LIST_INSERT, VECTOR_INSERT, LIST_SORT,
    LIST_REVERSE, VECTOR_RESIZE, LIST_SET, LIST_FILL, LIST_INSERT_SORTED, LIST_SORT_BY, LIST_SWAP,
    LIST_MOVE, LIST_ROTATE, IOTA, et LIST_COPY vers un SET : « LIST_SET : un SET se range tout seul
    (chaque valeur une fois, dans l'ordre) — SET_ADD et SET_REMOVE l'écrivent ».
  - Restent permises (la forme se garde) : LIST_REMOVE, LIST_REMOVE_AT, LIST_CLEAR, LIST_POP_FIRST,
    VECTOR_POP, LIST_REMOVE_ALL, LIST_REMOVE_IF, LIST_DISTINCT, LIST_APPEND (l'union).
- **Un RING** s'écrit comme une liste (S[i] := x, LIST_SET, LIST_SORT, LIST_SWAP…) ; ce qui
  l'allonge le ramène à N (LIST_INSERT en tête d'un RING plein : la valeur part aussitôt ;
  LIST_INSERT_SORTED rend l'indice après coup) ; `VECTOR_RESIZE(R, n)` au-delà de N est refusé
  (« VECTOR_RESIZE : un RING[3] OF INT garde au plus 3 valeurs »).
- **Les affectations** : une LIST (ou un ARRAY, un littéral) dans un SET se range et perd ses
  doublons ; dans un RING, ses N dernières valeurs ; un SET ou un RING dans une LIST en fait une
  LIST ordinaire ; d'un RING à l'autre, la capacité de la destination.
- **Les constantes** : SET_ADD, SET_REMOVE, RING_ADD, LIST_ADD… sur une `VAR CONSTANT` : « SET_ADD :
  une constante ne s'écrit pas » ; SET_HAS, JOIN… la lisent.

### Compiler (`src/hmi/HmiScriptCheck.cpp`)

| Contrôle | Gravité |
|---|---|
| Le nombre d'arguments (« SET_ADD prend 2 arguments, pas 3 ») | erreur (une nouvelle native) |
| SET_ADD / SET_REMOVE / SET_HAS sur un nom dont le type est connu et n'est pas un SET (« SET_ADD : un SET est attendu (SET OF T), pas une LIST OF STRING ») ; RING_ADD sur autre chose qu'un RING | erreur |
| Un littéral du mauvais type écrit dans un SET ou un RING de type connu (« SET_ADD : 'abc' n'est pas un INT », « SET_ADD : 5 n'est pas une STRING : écris '5' ») | erreur |
| Une native qui écrit à une place sur un SET (LIST_INSERT, LIST_SORT, LIST_SET…, LIST_COPY vers un SET) | erreur |
| `S[0] := x`, `S.First := x` d'un SET (une locale, une variable IHM, `Recette.Postes[0]`) ; la variable d'un `FOR EACH x IN S` écrite (S un nom de type SET) | erreur |
| La capacité d'un RING déclaré (« RING[0] : la capacité va de 1 à 100000 », « RING : sa capacité est attendue… », « RING[N] : N est une constante entière ('i' …) ») | erreur |
| `SET OF T_Lot` (une structure), `SET OF TUPLE(…)`, `SET OF LIST OF…` | erreur |
| Une constante écrite par SET_ADD, SET_REMOVE, RING_ADD (« Cs est une constante : SET_ADD ne peut pas la changer ») | erreur |
| Dans une expression fx : SET_ADD, SET_REMOVE, RING_ADD changent leur objet (une expression lit une copie) | comme les autres natives qui écrivent |

Toutes ces fautes portent sur des constructions nouvelles (SET, RING) : ce sont des erreurs. Rien
de ce qui compilait en 1.12.6 ne change : un nom `Set`, `Ring` reste une variable, une fonction du
projet nommée SET_ADD passe avant la native.

### Les types IHM, les créateurs de variables, le sélecteur de types

- **Le registre des types** (`src/hmi/HmiTypeRegistry.cpp`) lit `SET OF T` et `RING[N] OF T`
  (clés `set:…`, `ring[N]:…`) : de taille variable et objet, comme une LIST — une variable IHM de
  ce type vit dans la mémoire de l'IHM (**Équipement et Adresse sont verrouillés**, la règle des
  types de taille variable). `RING[0]`, `RING OF REAL`, `SET OF T_Lot` y sont refusés avec la
  phrase du moteur.
- **Les conversions du registre** (`typereg::conversion`, le contrôle des appels) : un SET, un RING et
  une LIST (ou un VECTOR) de même élément s'échangent par valeur — une fonction qui attend une
  `LIST OF INT` reçoit un `SET OF INT` (une LIST vers un SET : « rangée, sans doublon » ; vers un
  RING : « ses dernières valeurs seulement »). Une LIST et un VECTOR restent comme avant.
- **Les formes** (`src/hmi/HmiTypeForms.cpp`) : deux de plus, **Ensemble (SET)** et **Tampon
  (RING)** (sa capacité en paramètre, 100 par défaut) — dans le sélecteur de types, la case Type,
  « Forme du type » (le texte de ce dialogue cite aussi SET et RING).
- Ce qui reconnaissait une LIST par son texte reconnaît aussi un SET et un RING : `hmi::types::isRich`,
  la raison dite quand un objet est lié à une telle variable (c'est « une liste » : S[0] ou S.Count),
  les lignes d'un tableau (« lignes depuis » une variable SET ou RING), la valeur
  nulle d'une constante (`[]`), le modèle de départ d'un calcul (`IF SET_ADD(S, 0) THEN … END_IF`,
  `RING_ADD(R, 0);`).

### La grille des déclarations et le tableau des éléments

- **Le déballage** (`HmiDeclGrid::refreshUnfold`) montre un SET rangé, un RING avec sa capacité
  (« 3 éléments sur 5 [..] », le type `RING[5] OF REAL`).
- **Éléments…** (`HmiItemsDialog`) : une ligne par valeur, comme une liste ; un SET se lit rangé et
  sans doublon (les nombres comme des nombres : 9, 10, 100), et ni Monter ni Descendre (l'ordre est
  le sien) ; un RING montre ses N dernières valeurs, « Valeur (INT, 3 au plus) » ; le littéral
  rendu a la même forme.

### Les fiches (branche Natives) et le guide

- `outils/natives/catalogue.py` : une catégorie de plus, **« Ensembles et tampons (SET, RING) »**
  (groupe « Le dialecte de l'IHM »), une fiche par native (SET_ADD, SET_REMOVE, SET_HAS, SET_UNION,
  SET_INTERSECTION, SET_DIFFERENCE, RING_ADD : ST, C, C++, un essai exécuté par `hmi_natives_test`),
  deux types construits (`SET OF`, `RING`), et une note « un SET et un RING aussi » sur les fiches des
  natives LIST_ qui les lisent (LIST_CONTAINS, LIST_INDEX_OF, LIST_COUNT, LIST_FIRST, LIST_LAST,
  LIST_GET, LIST_SUM, LIST_MIN, LIST_MAX, LIST_AVG, JOIN) et sur LIST_ADD. `src/hmi/HmiNativesData.cpp`
  régénéré (227 fonctions).
- **L'aide à la saisie** : les 7 natives (signature et phrase, de la table du dialecte) ; les
  modèles `SET OF STRING` et `RING[100] OF REAL` ; après `Dernieres.` (un RING, une locale ou une
  variable IHM) : Count, Capacity, Full, First, Last ; après `Postes.` (un SET) : Count, First, Last.
- **Le guide** (`src/hmi/HmiGuideText.cpp`, le sujet « MAP et itérateurs : FOR EACH ») : six blocs
  marqués 1.12.7 à la fin du sujet — ce que sont un SET et un RING, un exemple ST, C et C++, ce qu'un
  SET refuse, les affectations, une variable IHM SET ou RING ; et leurs mots-clés (SET, RING, SET_ADD…)
  pour la recherche. Sans intertitre : le tutoriel déduit de ce sujet (ses étapes : les lignes de son
  tableau) ne change pas.

## Pour les notes de version (proposées)

1. **Deux nouveaux types de conteneurs, SET et RING.** `SET OF T` est un ensemble (std::set) : chaque
   valeur une seule fois, rangée — `SET_ADD` rend TRUE si la valeur est nouvelle (déjà là, rien ne
   change), `SET_REMOVE`, `SET_HAS`, et `SET_UNION`, `SET_INTERSECTION`, `SET_DIFFERENCE` qui rendent
   un SET neuf ; plus besoin de LIST_CONTAINS avant chaque LIST_ADD. `RING[N] OF T` est un tampon
   circulaire : les N dernières valeurs — `RING_ADD` (ou LIST_ADD) ajoute à la fin et, plein, fait
   partir la plus ancienne ; `.Count`, `.Capacity`, `.Full`, et LIST_AVG, LIST_MIN, LIST_MAX le
   lisent (une moyenne glissante sur les 100 dernières mesures : `LIST_AVG(Dernieres)`).
2. **Ils s'emploient comme une liste** : FOR EACH (un SET dans l'ordre, un RING de la plus ancienne à
   la plus récente), `S[0]`, `.First`, `.Last`, JOIN, LIST_GET, les littéraux (`['P3', 'P1', 'P3']`
   devient `['P1', 'P3']` dans un SET), les affectations depuis une LIST. Une variable IHM peut être
   un SET ou un RING (Ensemble (SET), Tampon (RING) dans le sélecteur de types) : elle vit dans la
   mémoire de l'IHM ; la grille la déballe, Éléments… la montre rangée ou bornée.
3. **Un SET se range tout seul, Compiler le dit** : écrire un élément en place (`S[0] := x`) ou
   l'écrire à une place (LIST_INSERT, LIST_SORT, LIST_SET…) est refusé avec ce qu'il faut faire
   (SET_REMOVE puis SET_ADD) ; une capacité de RING hors de 1..100000 ou qui n'est pas une constante,
   un SET de structures, un SET_ADD sur une LIST sont des erreurs de Compiler. Chaque native a sa
   fiche (ST, C, C++) dans la nouvelle catégorie « Ensembles et tampons (SET, RING) ».

## Les essais

- `tests/hmi_setring_test.cpp` (nouveau, `ctest -R hmisetring`, 120 contrôles) : les déclarations et
  les littéraux (le SET rangé sans doublon, le RING des N dernières, RING[N] d'une constante),
  chaque native avec ses bords (valeur déjà là, absente, SET vide, RING plein) et ses fautes dites en
  français, les membres, l'ordre de FOR EACH, LIST_ADD et VECTOR_PUSH sur un RING et un SET, les
  natives LIST_ qui lisent, PARTIAL_SUM / LIST_SLICE / TO_LIST d'un SET (des LIST), ce qu'un SET
  refuse (S[0] :=, FOR EACH, S.First :=, ADR(S[0]), 13 natives), les affectations entre LIST, SET,
  RING et ARRAY, les constantes (6 natives qui écrivent ; la lecture), une variable IHM SET et RING
  (valeur initiale mise en forme, écrite en place par SET_ADD et RING_ADD, son littéral), des
  variables nommées Set et Ring, une fonction du projet nommée SET_ADD, Compiler (un script sans
  erreur ; le nombre d'arguments ; SET_ADD sur une LIST, RING_ADD sur une LIST, SET_HAS sur un RING ;
  ce qu'un SET refuse, la variable d'un FOR EACH sur un SET comprise ; un littéral du mauvais type ;
  une constante ; la capacité d'un RING ; SET OF T_Lot ; un SET passé par valeur à un paramètre
  LIST OF INT, en marche aussi ; un membre SET et un membre RING d'un type IHM, par leur chemin, en
  marche aussi), un projet entier (`compileWith` : des variables IHM SET et RING et des locales SET
  et RING, sans erreur), le registre des types (une variable IHM SET, RING ; RING[0], RING sans
  capacité refusés ; les conversions).
- `tests/hmi_editor_test.cpp` (`setRing1127`, aussi par `HMI_TEST_NATIVES=1`) : l'aide à la saisie
  (SET_ADD, RING_ADD, SET_INTERSECTION proposées ; les membres d'un RING et d'un SET locaux, d'une
  variable IHM RING), le tableau des éléments (un SET rangé, sans doublon, les nombres rangés comme
  des nombres ; un RING[3] et ses 3 dernières valeurs).
- `tests/hmi_types_test.cpp` : 9 formes (7 + SET et RING), `compose(decompose(…))` de `SET OF STRING`,
  `RING[100] OF REAL`.
- `tests/hmi_natives_test.cpp` exécute l'essai des 7 nouvelles fiches.

## Limites connues, écarts avec la maquette

- **RING_ADD rend le nombre de valeurs** (DINT, au plus N), comme LIST_ADD ; la maquette ne dit pas ce
  qu'il rend.
- **SET_UNION, SET_INTERSECTION, SET_DIFFERENCE acceptent aussi une LIST, un RING ou un ARRAY** (rangés,
  sans doublon) ; SET_ADD, SET_REMOVE et SET_HAS, eux, veulent un SET (LIST_CONTAINS lit déjà une
  liste).
- **Un SET range des valeurs simples** (l'ordre de `<`) ; un SET de structures ou de TUPLE est refusé
  (une LIST et LIST_SORT_BY). Les textes sont rangés octet par octet, comme std::set<std::string> :
  `'B'` avant `'a'`, et sans casse ni accent particuliers.
- **La capacité maximale d'un RING est 100000** (choisie : un RING garde chaque valeur comme un
  objet du moteur ; les 100 dernières mesures d'une courbe en sont loin).
- **Compiler connaît le type d'un SET ou d'un RING désigné** : une locale, une variable IHM, le membre
  d'une variable IHM par son chemin (`SET_ADD(Recette.Postes, 5)`, `Recette.Postes[0] := x` sont
  dits) ; une valeur calculée, le membre d'une structure locale, et `FOR EACH x IN Recette.Postes DO
  x := …` (la variable d'un FOR EACH sur un chemin) ne sont dits qu'en marche (refusés). Un
  `POINTER TO` vers un élément de SET n'est pas possible (ADR(S[0]) est refusé), mais un pointeur
  vers le SET entier l'écrit par ses natives, qui gardent sa forme.
- **Le tableau des éléments** range un SET sur le texte des valeurs (les nombres comme des nombres,
  les textes sans leurs apostrophes) ; le moteur, qui relit le littéral, a le dernier mot (une valeur
  écrite `'1'` dans un SET OF INT est refusée à la vérification de la valeur, comme pour une LIST).
- **Le C et le C++** : les fiches donnent l'équivalent (std::set, std::set_union…, std::deque bornée ou
  boost::circular_buffer) ; un script C ou C++ de l'IHM n'est pas exécuté (comme avant).
- Les variables IHM SET et RING suivent les règles des LIST (de taille variable : ni équipement ni
  adresse).
