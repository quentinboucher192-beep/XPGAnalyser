# Tests et qualité

État des essais automatiques du dépôt (version 1.21.3), établi à partir de `CMakeLists.txt` et du dossier `tests/`. Version 1.22.0. Retour au [README](../README.md).

## Sommaire

1. [Principe](#principe)
2. [Ce qui est enregistré dans CTest](#inventaire)
3. [Exécuter les essais](#executer)
4. [Catégories de suites](#categories)
5. [Données d'essai](#donnees)
6. [Plates-formes réellement essayées](#plates-formes)
7. [Scripts historiques](#historiques)
8. [Ce que les essais ne garantissent pas](#portee)

---

<a id="principe"></a>
## 1. Principe

- Chaque suite est un **exécutable autonome** (`tests/<nom>_test.cpp`) qui vérifie ses propres conditions (une fonction `check(...)` locale, sans cadriciel externe) et rend un code de sortie non nul en cas d'échec. CTest les lance.
- Les essais **n'ouvrent pas de fenêtre** : ils lient les bibliothèques sans écran (`xpg_core`, `xpg_import`, `xpg_ui`, `xpg_help`, `xpg_hmi`), des sources de `src/app` nommées explicitement, ou `xpg_app`. Dans la configuration d'essai utilisée sous Linux, SDL est désactivé (`-DXPG_WITH_SDL=OFF`).
- Le réseau et les fournisseurs d'IA sont remplacés par des **faux** : transport HTTP injectable (`setHttpFactory`, `src/app/ai/AiHttp.hpp`), faux fournisseur qui rejoue des flux enregistrés au format Anthropic (`tests/fixtures/ia/faux_fournisseur.py`), faux serveurs locaux. Une suite (`aireseau`) contient aussi un essai de bout en bout avec libcurl contre un serveur local.
- La fonction `xpg_add_test_executable` **saute** un essai dont une source manque, et `xpg_add_test` n'enregistre pas un essai dont un fichier d'entrée (chemin absolu en argument) manque ; CMake l'affiche à la configuration.

---

<a id="inventaire"></a>
## 2. Ce qui est enregistré dans CTest

Décompte fait sur `CMakeLists.txt` de ce commit :

| Élément | Nombre |
|---|---:|
| Essais enregistrés par `xpg_add_test` (sources et fichiers d'entrée présents) | 118 |
| Essai `vcxproj` (Python : `outils/verifier_vcxproj.py`, enregistré si Python 3 est trouvé) | 1 |
| **Total** | **119** |

Ce total correspond aux « 119 suites d'essais » de la note de livraison 1.22.0 (116 en 1.21.3, plus `aimissions`, `aimissionse2e`, `aimissionsui`).

Essais **déclarés mais sautés** parce que leur source n'est pas dans le dépôt (22) : `core` (`src/core/compile_check.cpp` absent), `grafcet`, `macro`, `xref`, `equipment`, `font`, `theme`, `corner`, `libhelp`, `helparticle`, `execorder`, `simorder`, `treednd`, `execwiring`, `macroxls`, `macronatives`, `libcatalog`, `macrohelp`, `libpage`, `help`, `helpsession`, `xlsm`. Pour `xlsm`, les classeurs d'essai (`tests/fixtures/simple.xlsx`) et le script `tests/make_fixtures.py` que cite `CMakeLists.txt` pour les fabriquer sont eux aussi absents.

Sources présentes dans `tests/` mais **non référencées** par `CMakeLists.txt` : `grafcet_editor_test.cpp`, `grafcet_rewrite2_test.cpp`, `xls_test.cpp`.

Conditions d'enregistrement :

- les essais qui lisent la bibliothèque (`macros2libs`, `api2libs`, `macrospecwrite`, `simdebuglibs`) ne sont enregistrés que si `libs/` existe (`XPG_LIBS_DIR`, présent dans le dépôt) ;
- `XPG_FIXTURE` et `XHW_FIXTURE` désignent `tests/fixtures/MAST.XPG` et `tests/fixtures/CONFIG.XHW` (modifiables à la configuration).

---

<a id="executer"></a>
## 3. Exécuter les essais

### Linux (référence)

Prérequis : un compilateur C++20 (les livraisons récentes utilisent GCC 13), CMake ≥ 3.24, Ninja, Python 3 ; l'accès à PyPI une fois pour PDFium.

```bash
# Une fois : PDFium pour Linux (third_party/pdfium/linux-x64/libpdfium.so, hors dépôt).
# Sans lui, l'essai hmidocviewer échoue (il le dit).
python3 outils/pdfium/obtenir_pdfium.py

# Configuration sans interface graphique, compilation, essais
cmake -S . -B build-linux -G Ninja -DXPG_WITH_SDL=OFF
ninja -C build-linux
ctest --test-dir build-linux --output-on-failure

# Une partie seulement (expression régulière sur le nom)
ctest --test-dir build-linux -R '^ai' --output-on-failure
```

`-DXPG_WITH_SDL=OFF` évite d'exiger SDL3 sur la machine (`find_package(SDL3 CONFIG REQUIRED)` sinon).

> La compilation de `src/hmi/HmiGuideText.cpp` (l'aide F1 générée, environ 2 Mo de source) demande beaucoup de mémoire : les notes de livraison 1.21.x indiquent environ 12 Go, raison pour laquelle `CMakeLists.txt` la compile en `-O1` avec GCC/Clang. Limiter le parallélisme (`ninja -j 2`) sur une machine modeste.

### Windows

`outils\test_build.bat` compile l'application avec MSBuild, puis les essais avec CMake + Ninja et MSVC (sans SDL, dans `build\cmake-tests`), et lance `ctest` (options `/filtre:<regex>`, `/propre`, `/sans-application`) — voir [GUIDE-COMPILATION.md](GUIDE-COMPILATION.md). Aucune note de livraison récente ne rapporte une exécution de ce script sur Windows.

---

<a id="categories"></a>
## 4. Catégories de suites

| Catégorie | Nombre | Suites |
|---|---:|---|
| Assistant IA | 21 | `aimissions`, `aimissionse2e`, `aimissionsui`, `aiassistant`, `aireseau`, `aisandbox`, `airegistre`, `aiintegree`, `aicompatible`, `aioptions`, `aiapprentissagefichier`, `aiapprentissage`, `aiplan`, `aidelete`, `aisouvenirs`, `aicontexte`, `ainotifications`, `aimodelesfichiers`, `aidesign`, `aiincidents`, `aimemoire` |
| IHM (modèle, éditeur, moteur, langage des scripts, communication, incidents, archives) | 37 | `hmi`, `hmieditor`, `hmicheckexpr`, `hmibuild`, `hmilog`, `hmisimdata`, `hmiretain`, `hmitrace`, `hmitracemig`, `hmimigrate`, `hmitypes`, `hmireperes`, `hminatives`, `hminatives1127`, `hminatives1216`, `hmisetring`, `hmiqr`, `hmivideo`, `hmipid`, `hmidocviewer`, `hmiedits`, `hmirecording`, `hmioverload`, `hmidecl`, `hmidebug`, `hmirelations`, `hmiincident`, `hmidiscovery`, `renamehmipaths`, `echanges`, `erreurs`, `generiques`, `testsunitaires`, `donnees`, `objets`, `unites`, `bibliotheques` |
| Automate (import, projet, édition, analyse, simulation, macros) | 33 | `import`, `hardware`, `roundtrip`, `project`, `edit`, `editor`, `library`, `sharedlib`, `delete`, `table`, `robustness`, `viewmodel`, `simulation`, `macros2`, `api2`, `api34`, `typeusage`, `versionstate`, `projecticon`, `lisible`, `membertree`, `renameplan`, `mastimport`, `projectstats`, `graphxml`, `simdebugui`, `simstatus`, `simperf`, `simdebug`, `macros2libs`, `api2libs`, `macrospecwrite`, `simdebuglibs` |
| Socle, interface, application | 27 | `menu`, `syntax`, `fold`, `layout`, `popup`, `zoom`, `settings`, `dossiers`, `keymap`, `themecatalog`, `themefile`, `dialogwindow`, `statusstrip`, `searchfocus`, `filtermemory`, `crashtrail`, `crashreport`, `coreprocess`, `topbar8`, `lot8tutorials`, `tutorial`, `fileexplorer`, `fileview`, `dropfiles`, `exportask`, `cellrule`, `edition` |
| Outillage | 1 | `vcxproj` |

Quelques suites significatives :

- `roundtrip` : import d'un export réel, réécriture, réimport, comparaison entité par entité ;
- `robustness` : fichiers malformés diagnostiqués sans dépassement ;
- `echanges` : archives `.xihm` et `.xapi` (aller-retour, archives abîmées, plus récentes, d'une autre application) ;
- `edition` : les deux applications et la migration des projets de la 1.11 ;
- `aireseau` : adresses, flux SSE, requêtes, erreurs 401/404/429/529, nouvelles tentatives, clé jamais écrite en clair, longueur des réponses et « Proposer la suite » (1.21.3) ;
- `aisandbox` : processus enfant borné du bac à sable ;
- `hmidocviewer` : visionneuse PDF (exige PDFium Linux).

---

<a id="donnees"></a>
## 5. Données d'essai

| Chemin | Contenu |
|---|---|
| `tests/fixtures/MAST.XPG`, `tests/fixtures/CONFIG.XHW` | Export de programme et de configuration matérielle d'essai |
| `tests/fixtures/ihm/` | Échantillons de ressources IHM (images PNG, JPG, BMP, GIF, SVG) |
| `tests/fixtures/decl/corpus.txt` | Corpus des blocs de déclaration (fabriqué par `outils/corpus_declarations.py`) |
| `tests/fixtures/trace/` | Trace de référence de la simulation IHM |
| `tests/fixtures/ia/faux_fournisseur.py` | Faux fournisseur d'IA local |
| `tests/fixtures/macros/`, `tests/fixtures/web/` | Un classeur `.xlsm` et un petit projet IHM ; présents, mais ils ne sont référencés ni par `CMakeLists.txt` ni par `tests/*.cpp` (recherche dans ce commit) |
| `bac/Armoire_Gaz` | Projet modèle d'avant la 1.12 (programme et IHM ensemble), utilisé par plusieurs suites |
| `projets/project` | Projet d'exemple d'avant la 1.12 (programme et IHM dans le même dossier, manifeste sans champ `edition`), utilisé par `hmidecl` |
| `libs/` | Bibliothèque livrée, lue par les suites `*libs` |
| `tools/tutoriels/` | Textes des didacticiels (suite `tutorial`) |

---

<a id="plates-formes"></a>
## 6. Plates-formes réellement essayées

D'après les notes de livraison (`docs/LIVRAISON-1.21.0.md` à `docs/LIVRAISON-1.21.3.md`) :

- **Linux** : les 119 suites CTest, toutes réussies pour la 1.22.0.
- **Windows** : les deux exécutables Windows, compilés sous Linux avec MinGW-w64, ont été **vérifiés sous Wine, pas sur un vrai Windows** : `--version`, `API-Studio.exe --cli MAST.XPG` (même résultat qu'avant : 891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB), installation silencieuse de `API-IHM-Studio-Setup-1.21.3.exe` par-dessus la 1.21.1 (clé API d'essai conservée, associations présentes), et bac à sable de l'exécutable installé (`IHM-Studio.exe --bac-a-sable`).
- **Assistant IA avec une vraie IA** : non essayé dans les sessions de la 1.21.3 et de la 1.22.0 (aucune clé disponible) ; les essais utilisent un faux fournisseur et un faux transport.

---

<a id="historiques"></a>
## 7. Scripts historiques

- `build.sh` : construction sans CMake des premiers lots ; son en-tête indique qu'il **n'est plus tenu à jour** (ni `src/hmi`, ni `src/help`, ni l'application).
- `runtests.sh` : lance les essais produits par `build.sh` dans `build/` ; il cite des essais absents du dépôt et des chemins propres à une autre machine. Ni l'un ni l'autre ne sont la référence : utiliser CMake et CTest.

---

<a id="portee"></a>
## 8. Ce que les essais ne garantissent pas

- Des essais réussis ne prouvent pas l'absence de défauts : ils vérifient les cas écrits.
- Les essais n'ouvrent pas l'interface graphique réelle ; le rendu SDL et l'interaction ne sont pas couverts par CTest.
- Les essais de communication utilisent des serveurs et des équipements simulés ; aucun automate physique n'est en jeu.
- Les services d'IA réels, le comportement de Windows hors Wine et l'installateur sur un vrai Windows ne sont pas couverts par les essais automatiques de ce dépôt.
