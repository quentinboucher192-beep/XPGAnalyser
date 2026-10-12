# XPGAnalyser 1.12.7 : les valeurs calculées au démarrage, les bibliothèques du C++ en ST

Livrée le 12/10/2026 dans la nuit.

Elle répond à la demande du 11/10 au soir, sur la maquette `docs/maquettes/maquette-constantes-numeric.html` (versions 1 à 3) :

> « ok fait tout ça et enchaine sur les lots D E le tout pour demain matin »

« Tout ça », c'est la maquette 1.12.7 avec vos choix :
- **choix 1** : une erreur pendant un calcul au démarrage arrête le démarrage, et la ligne s'ouvre d'un double-clic ;
- **choix 2** : une constante d'un autre script se lit toujours par son nom qualifié, `Reglages.Coef_Ligne` ;
- **choix 3** : les natives s'écrivent comme les autres (`ACCUMULATE`, comme `LIST_SUM`), et le type d'un template entre `< >` (`NUMERIC_LIMITS<UINT>.MAX()`) ;
- **choix 4** : une variable a aussi une valeur initiale calculée ;
- **choix 5** : deux types de plus, SET et RING. La maquette les plaçait après les lots D et E ; ils ont été faits en parallèle, et ils sont prêts dans cette version ;
- **choix 6** : les fonctions de `<bit>` sur les WORD et les DWORD ;
- **choix 7** : les fonctions LIST_ qui ne changent pas la taille acceptent un ARRAY.

Tout se passe dans **XPGAnalyser IHM**. XPGAnalyser API ne change pas, à part son numéro de version.

## 1. Les valeurs calculées au démarrage

Une constante d'un script peut venir d'un **code ST qui l'écrit une fois, au démarrage**, comme un `final` de Java. Une variable peut avoir de même sa **valeur initiale calculée**.

Ce code tourne après les valeurs de départ des variables IHM et la rémanence, avant les scripts de Démarrage et la première vue :
- une variable **Exécution** repart de cette valeur à chaque exécution ;
- une **Conservée** la prend à la première exécution ;
- une **Persistante** rendue garde la sienne.

**L'ordre.** Les constantes passent d'abord, chacune après celles qu'elle lit, puis les variables. Une boucle (A lit B, qui lit A) est refusée par Compiler et arrête le démarrage.

**Une erreur arrête le démarrage** (choix 1). La Console dit le calcul et sa ligne : « calcul Reglages.Total : ligne 1 : clé absente de la MAP ». La barre de la simulation le dit aussi. Un double-clic sur la ligne de la Console ouvre le script, l'onglet et la ligne de la déclaration, puis la fenêtre du calcul à la ligne de la faute. Les fautes d'un calcul dans Compiler et Générer y mènent de la même façon. Chaque valeur calculée est écrite dans la Console (`Reglages.Seuils = {'bas': 10, …}`).

**La grille.** Dans les onglets Constantes et Variables :
- le bouton **ƒx Calcul…** ouvre la fenêtre du calcul ;
- la case Valeur montre `ƒx = {'bas': 10, 'haut': 30}`, en couleur, ou en rouge avec la raison si le calcul échoue ;
- une valeur écrite dans la case remplace le calcul (Ctrl+Z le rend) ;
- l'infobulle montre le code, les locales et la valeur.

**La fenêtre du calcul** suit la maquette :
- l'onglet **Code** : l'éditeur ST, avec l'aide à la saisie (Ctrl+Espace). Elle connaît les noms du projet, ceux du script et les locales ;
- l'onglet **Locales** : une grille avec le nom, le type, la valeur initiale et la documentation. Il n'y a **jamais de `VAR … END_VAR`** dans le code. Un bloc collé dans le code est signalé, et « Déplacer dans Locales » range ses déclarations dans la grille ;
- à droite, en direct, une demi-seconde après la dernière frappe (F5 : tout de suite) :
  - l'**Aperçu** : la valeur au démarrage, ou l'erreur qui l'arrêterait ;
  - **Lit** : ce que le calcul lit ;
  - **Lu par** : les autres calculs, le code de son script, et les scripts et fonctions qui lisent `Reglages.X` ;
  - les **Problèmes** : un clic mène à la ligne ;
- « ▶ Calculer (F5) » et « Revenir au calcul d'origine ».

**Les constantes des autres scripts, par leur nom qualifié** (choix 2). `Reglages.Seuils`, `Reglages.Seuils['bas']` et `MAP_SIZE(Reglages.Seuils)` se lisent dans les scripts, les fonctions, les calculs et les expressions des vues :
- seules les constantes **publiques** d'un script général se lisent ainsi : « Reglages.Secret est privée : seul Reglages la lit (rends-la publique) » ;
- **aucune ne s'écrit**. `Reglages.Total := 1`, `Reglages.Seuils['x'] := 1` et `MAP_REMOVE(Reglages.Seuils, 'bas')` sont refusés, par Compiler et par le moteur ;
- après `Reglages.`, l'aide à la saisie propose ses constantes ;
- renommer une constante renomme `Reglages.X` partout ;
- un script qui lit `Reglages.X` est recompilé quand Reglages change (le build incrémental).

**Le déballage et le tableau des Éléments.** Sous les grilles, la valeur à taille de la ligne choisie se lit élément par élément, en arbre (élément, type, valeur). C'est vrai pour LIST, VECTOR, MAP, ARRAY à une ou deux dimensions (avec ses vrais indices), TUPLE et structure, que la valeur soit écrite ou calculée. **Éléments…** ouvre un tableau à une ligne par élément, à la place du texte « un par ligne ».

**Le défaut de votre capture.** Une constante neuve passée en MAP, LIST ou VECTOR prend `[]` ; avant, elle gardait `0.0`. Une valeur simple n'y va plus : « un MAP[STRING] OF INT prend ses éléments entre crochets ».

**Le fichier.** `calculee=oui`, `calcul="…"` et une ligne `locale` par locale. C'est un ajout : le format ne change pas, et une version d'avant ignore ces champs.

## 2. Les natives : les conteneurs, std::numeric, les templates, `<bit>`

- **43 fonctions de plus pour les conteneurs** :
  - lire et écrire sans erreur : `LIST_SET`, `MAP_SET`, `TUPLE_GET`, `TUPLE_SET` ;
  - chercher, compter et retirer sous condition, avec l'énumération `COMPARAISON` : `LIST_FIND_IF`, `LIST_COUNT_IF`, `LIST_ALL`, `LIST_ANY`, `LIST_NONE`, `LIST_REMOVE_IF`, `MAP_REMOVE_IF` ;
  - trier une liste de structures par un membre : `LIST_SORT_BY`, un tri stable ;
  - déplacer, échanger, faire tourner, extraire, concaténer ; copier vers un ARRAY (`LIST_COPY`) ;
  - `MAP_INCREMENT`, `MAP_FLOOR_KEY` (une table de paliers), `MAP_ITEMS`, `TUPLE_UNPACK`, `TUPLE_TO_STRING`…
- **Les fonctions LIST_ qui ne changent pas la taille lisent aussi un ARRAY**, à ses indices (choix 7). Une fonction qui changerait la taille dit pourquoi : « LIST_ADD : un ARRAY a une taille fixe (5 cases) — une LIST ou un VECTOR est attendu ».
- **std::numeric en ST** : les 19 fonctions, jusqu'au C++26. Ce sont `ACCUMULATE`, `REDUCE`, `INNER_PRODUCT`, les `SCAN`, `ADJACENT_DIFFERENCE`, `IOTA`, `GCD`, `LCM`, `MIDPOINT` et le calcul saturé (`ADD_SAT`, `SUB_SAT`, `MUL_SAT`, `DIV_SAT`, `SATURATE_CAST<INT>(x)`). Elles s'accompagnent des énumérations `OPERATION` et `TRANSFORMATION`.
- **Les templates** :
  - `NUMERIC_LIMITS<UINT>.MAX()` et ses 32 membres, pour 15 types ;
  - `NUMBERS.PI`, `NUMBERS<REAL>.SQRT2` ;
  - l'aide à la saisie propose les types, puis les membres et leur valeur. Dans un script C++ aussi : `std::numeric_limits<int16_t>::max()`, `std::numbers::pi` ;
  - Compiler dit les fautes : « UINTT n'est pas un type — veux-tu dire UINT ? », « MIN est une fonction : NUMERIC_LIMITS<UINT>.MIN() ».
- **Les bits d'un mot** (choix 6) : `BIT_COUNT` (std::popcount), `BIT_FIRST` (std::countr_zero ; −1 s'il n'y a aucun bit), `BIT_TEST`, `BIT_SET`, `BIT_RESET`. Elles marchent sur BYTE, WORD, DWORD, LWORD et les entiers.
- **La branche Natives** a un groupe « Bibliothèques » : std::numeric, std::numeric_limits, std::numbers. Chaque nouvelle fonction a sa fiche, avec les onglets ST, C, C++ et un essai qui s'exécute.
- **Corrigé.** `MAP_REMOVE` et `MAP_CLEAR` changeaient une MAP déclarée constante sans un mot. Elles la refusent maintenant, comme toutes les natives qui écrivent. La fiche `MAP_GET` dit vrai : lire une clé absente arrête le script.

Une fonction du projet qui porte le nom d'une nouvelle native (GCD, IOTA…) passe avant elle. **Ce qui démarrait en 1.12.6 démarre toujours** : une faute dans du code d'avant est un avertissement ; une faute avec une nouvelle native ou un template est une erreur.

## 3. Deux types de plus : SET et RING (choix 5)

- **`SET OF T`** est un ensemble, comme `std::set` : chaque valeur y est une seule fois, et rangée.
  - `SET_ADD` rend TRUE si la valeur est nouvelle ; si elle y est déjà, rien ne change et il rend FALSE. Plus besoin de `LIST_CONTAINS` avant chaque `LIST_ADD`.
  - `SET_REMOVE`, `SET_HAS`.
  - `SET_UNION`, `SET_INTERSECTION`, `SET_DIFFERENCE` rendent un SET neuf.
  - Un littéral `['P3', 'P1', 'P3']` devient `['P1', 'P3']`.
- **`RING[N] OF T`** est un tampon circulaire : il garde les N dernières valeurs. N est une constante entière de 1 à 100 000.
  - `RING_ADD` (ou `LIST_ADD`) ajoute à la fin ; quand le tampon est plein, la plus ancienne valeur part.
  - `.Count`, `.Capacity`, `.Full`.
  - `LIST_AVG`, `LIST_MIN` et `LIST_MAX` le lisent : `LIST_AVG(Dernieres)` donne une moyenne glissante sur les 100 dernières mesures.
- **Ils s'emploient comme une liste** :
  - `FOR EACH` parcourt un SET dans l'ordre, et un RING de la plus ancienne valeur à la plus récente ;
  - `S[0]`, `.First`, `.Last`, `JOIN`, `LIST_GET` ;
  - les affectations depuis une LIST.
- **Une variable IHM** peut être un SET ou un RING. Le sélecteur de types propose les formes Ensemble (SET) et Tampon (RING). Comme une LIST, elle vit dans la mémoire de l'IHM. La grille la déballe (« 3 éléments sur 5 » pour un RING), et Éléments… la montre rangée (un SET) ou bornée (un RING).
- **Un SET se range tout seul.** Écrire un élément en place (`S[0] := x`), ou l'écrire à une place (`LIST_INSERT`, `LIST_SORT`, `LIST_SET`…), est refusé, et le message dit quoi faire : `SET_REMOVE`, puis `SET_ADD`.
- **Compiler signale comme erreurs** :
  - une capacité de RING hors de 1..100 000, ou qui n'est pas une constante ;
  - un SET de structures ;
  - un `SET_ADD` sur une LIST.
- **Les fiches.** Chaque native a sa fiche (ST, C, C++) dans une nouvelle catégorie de la branche Natives, « Ensembles et tampons (SET, RING) ».
- **Ce qui démarrait démarre toujours** : une variable nommée `Set` ou `Ring`, ou une fonction du projet nommée `SET_ADD`, garde son sens.

## Tester vous-même

1. Ouvrez un script général et passez dans l'onglet **Constantes**. Ajoutez `Seuils : MAP[STRING] OF REAL`, choisissez la ligne, puis **ƒx Calcul…**. Écrivez :
   `FOR i := 0 TO 2 DO Seuils[Noms[i]] := Base * (i + 1); END_FOR;`
   Dans l'onglet **Locales**, ajoutez `Noms : ARRAY[0..2] OF STRING := ['bas', 'moyen', 'haut']`. L'Aperçu montre la MAP. Appliquez : la case Valeur montre `ƒx = {…}`, et le déballage montre une ligne par clé.
2. Dans un autre script, écrivez `x := Reglages.Seuils['bas'];`. Après `Reglages.`, l'aide propose les constantes. Écrivez ensuite `Reglages.Base := 2;` : Compiler le refuse.
3. Rendez le calcul faux (`Seuils['x'] := Seuils['absent'];`) et lancez la simulation. Elle s'arrête, et la Console dit le calcul et la ligne. Un double-clic sur la ligne ouvre la fenêtre du calcul à cette ligne.
4. Dans un script, écrivez `n := NUMERIC_LIMITS<UINT>.MAX();` (l'aide propose les types puis les membres), `g := GCD(12, 18);` et `k := BIT_COUNT(Defauts);`.
5. Choisissez un tableau dans une grille : **Éléments…** ouvre une ligne par case.
6. Déclarez `Postes : SET OF STRING := ['P3', 'P1', 'P3'];` et `Dernieres : RING[3] OF REAL;`. Faites `SET_ADD(Postes, 'P2')` puis quatre `RING_ADD(Dernieres, x)` : `Postes` vaut `['P1', 'P2', 'P3']`, et `Dernieres` garde les trois dernières valeurs. Écrivez ensuite `Postes[0] := 'Z';` : Compiler le refuse et dit quoi faire.
