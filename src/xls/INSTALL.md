# Lire des .xlsx et des .xlsm

## Il n'y a aucune commande d'installation

Vous en avez demandé une. Il n'y en a pas, et c'est délibéré.

Un `.xlsx` est une archive ZIP de fichiers XML. La seule chose qui manquait au
projet était donc un **décompresseur**. Trois routes existaient :

| | |
|---|---|
| `libzip`, `zlib` | `apt install`, `vcpkg install`, une DLL à livrer, une version à accorder entre les postes |
| écrire un inflate | plusieurs centaines de lignes de code qu'il faudrait tester soi-même |
| **miniz, intégré au dépôt** | deux fichiers posés dans `third_party/`, compilés avec le reste |

C'est la troisième. `third_party/miniz.c` et `miniz.h` sont là, domaine public
(voir `miniz.LICENSE`), version 3.0.2 amalgamée. Le projet garde la propriété
qu'il annonce depuis le début : **rien à installer hors SDL**. La même décision
que pour `stb_truetype.h`, pour la même raison.

## Un seul drapeau compte

```sh
g++ -x c++ -std=c++20 -O2 -DMINIZ_NO_DEFLATE_APIS -c third_party/miniz.c -o miniz.o
```

`MINIZ_NO_DEFLATE_APIS` n'est pas une optimisation : **sans lui, miniz ne
compile pas en C++**. `s_tdefl_num_probes` y est défini deux fois, ce que le C
tolère — ce sont des définitions tentatives — et que le C++ refuse.

Le drapeau retire la partie *compression*, dont un lecteur de classeur n'a aucun
besoin. L'objet fait 48 Ko et ne sait plus qu'ouvrir, ce qui est exactement ce
qu'on veut.

`-x c++` force la compilation en C++ ; sans lui `g++` compilerait en C d'après
l'extension, et l'éditeur de liens chercherait des symboles décorés autrement.

## Les fichiers à ajouter

```
third_party/miniz.c          décompresseur, domaine public
third_party/miniz.h
third_party/miniz.LICENSE
src/xls/Workbook.hpp         le lecteur
src/xls/Workbook.cpp
tests/xls_test.cpp
```

## Les fichiers à modifier

**`src/project/Table.hpp`** et **`Table.cpp`** — une option de plus,
`stopAfterBlankRows`, par défaut à `0`, donc sans effet sur ce qui existait.
Elle sert à écarter la note de bas de page qu'une feuille porte presque toujours
sous ses données, en première colonne — c'est-à-dire dans une colonne d'ancrage.
Sans elle, cette phrase devient une ligne de données.

**`build.sh`** — une règle pour miniz, et l'édition de liens du test :

```sh
XLS=src/xls/Workbook.cpp
MINIZ_O=$OUT/obj/third_party_miniz.o
if [ ! -f $MINIZ_O ] || [ third_party/miniz.c -nt $MINIZ_O ]; then
    g++ -x c++ -std=c++20 -O2 -DMINIZ_NO_DEFLATE_APIS -c third_party/miniz.c -o $MINIZ_O
fi
ALL="$CORE $IMPORT $UI $PLATFORM $XLS $APP $(ls tests/*_test.cpp)"
link xls  $OUT/obj/src_xls_Workbook.o $OUT/obj/src_project_Table.o \
          $OUT/obj/src_import_XmlReader.o $MINIZ_O
```

**`runtests.sh`** :

```sh
run xls  build/xls_test /chemin/vers/un/classeur.xlsm
```

Sans argument, le test vérifie quand même tout ce qui ne demande pas de fichier
réel : l'arithmétique des références, les cellules éparses, le choix d'onglet,
les formules. Il ne saute rien en silence.

**`CMakeLists.txt`** — `miniz.c` dans la bibliothèque, avec sa définition :

```cmake
add_library(xpg_xls STATIC src/xls/Workbook.cpp third_party/miniz.c)
set_source_files_properties(third_party/miniz.c PROPERTIES LANGUAGE CXX)
target_compile_definitions(xpg_xls PRIVATE MINIZ_NO_DEFLATE_APIS)
target_link_libraries(xpg_xls PUBLIC xpg_project xpg_import)
```

## Ce que ça change pour les macros

Rien, et c'est le point.

`xls::read` rend un **`project::Table`** — exactement celui que `Table::parse`
rend pour un CSV. `OpenTable`, `ImportTable`, `RowCount`, `Cell`, `HasColumn`
continuent de fonctionner sans qu'une ligne change. Un second type de tableau
aurait doublé chaque chemin de code qui en consomme un.

```cpp
xls::ReadOptions o;
o.sheet = "Entrees TOR";              // vide : le premier onglet VISIBLE
o.table.anchors = {"Carte", "Designation", "Adresse"};
o.table.descriptionRows = 1;

auto r = xls::read("config_es.xlsm", o);
if (r.ok) {
    for (std::size_t i = 0; i < r.table.rowCount(); ++i)
        creerVoie(r.table.cell(i, "Designation"), r.table.cell(i, "Adresse"));
}
for (const auto& w : r.warnings) signaler(w);
```

`xls::inspect` donne la liste des onglets sans lire une seule feuille — de quoi
la montrer avant de demander lequel ouvrir. Demander « quel onglet ? » sans
pouvoir montrer la liste revient à demander de deviner.
