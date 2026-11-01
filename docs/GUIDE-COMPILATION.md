# Compiler API Studio et IHM Studio

Ce guide couvre les trois façons de construire la suite à partir du dépôt (version 1.21.3) et la fabrication de l'installateur. Les commandes viennent des scripts du dépôt (`CMakeLists.txt`, `outils/`). Retour au [README](../README.md).

> Mise à jour 1.21.3 : ce guide décrivait auparavant la seule chaîne Windows de la 1.11.2 (un exécutable `XpgAnalyzer.exe`, un installateur `XPGAnalyser-Setup-1.11.2.exe`). Il couvre maintenant les deux exécutables `API-Studio.exe` et `IHM-Studio.exe`, la construction Linux et la compilation croisée MinGW-w64, qui produit les exécutables livrés.

## Sommaire

1. [Les trois chaînes](#chaines)
2. [Dépendances](#dependances)
3. [Linux : bibliothèques, exécutables sans fenêtre, essais](#linux)
4. [Exécutables Windows compilés sous Linux (MinGW-w64)](#mingw)
5. [Installateur et versions portables fabriqués sous Linux](#paquet-linux)
6. [Windows : scripts `outils\` (MSVC, Visual Studio)](#windows)
7. [Codes de retour des scripts Windows](#codes)
8. [Outils de maintenance du dépôt](#outils)
9. [Avant une livraison : la version](#version)

---

<a id="chaines"></a>
## 1. Les trois chaînes

| Chaîne | Produit | Usage constaté dans les notes de livraison |
|---|---|---|
| **CMake + Ninja + GCC sous Linux** | bibliothèques, `API-Studio` et `IHM-Studio` sans interface (`--cli` seulement) si `XPG_WITH_SDL=OFF`, et les essais | les 116 suites de la 1.21.3 |
| **CMake + Ninja + MinGW-w64 (compilation croisée sous Linux)** | `API-Studio.exe`, `IHM-Studio.exe` pour Windows x64 | exécutables livrés, installateur fabriqué sous Linux avec Inno Setup sous Wine |
| **MSBuild + MSVC (Visual Studio 2022 ou Build Tools)** | `build\Release\API-Studio.exe`, `IHM-Studio.exe` ; installateur par `outils\package.bat` | liste des sources tenue à jour (essai `vcxproj`) ; l'ancienne version de ce guide indiquait que ces scripts n'avaient pas encore tourné sous Windows, aucune note de livraison ultérieure ne rapporte une compilation MSVC, et la version du fichier `.rc` est en retard (§ 9) |

Versions d'outils constatées dans l'environnement de construction utilisé pour les livraisons récentes : GCC 13.3 (Linux), MinGW-w64 GCC 13 (variante `posix`), CMake 3.28, Ninja 1.11. Minimum déclaré : CMake 3.24 (`cmake_minimum_required`), C++20.

---

<a id="dependances"></a>
## 2. Dépendances

| Dépendance | Rôle | Dans le dépôt ? |
|---|---|---|
| SDL3 3.4.12 | fenêtre, rendu, événements, presse-papiers, son | **en-têtes seulement** (`third_party/SDL3/include`). `SDL3.dll` et `SDL3.lib` (paquet officiel `SDL3-devel-3.4.12-VC.zip`, voir `third_party/SDL3/ORIGINE.txt`) doivent être posés dans `third_party/SDL3/lib/x64/` pour les constructions Windows et MinGW. Sous Linux, une construction avec interface exige un paquet SDL3 trouvé par `find_package(SDL3 CONFIG)`. |
| stb_truetype, stb_image, stb_image_write, nanosvg, minimp3, miniz | polices, images, SVG, MP3, zip | oui, `third_party/` (avec leurs licences) |
| PDFium 156.0.8076.0 | lecture des PDF (visionneuse, pièces jointes de l'Assistant) | **non** : `python3 outils/pdfium/obtenir_pdfium.py` télécharge et vérifie `win-x64/pdfium.dll` et `linux-x64/libpdfium.so` (hors dépôt, `.gitignore`). Chargé à la demande : l'application démarre sans lui. |
| WinHTTP, Crypt32 (DPAPI), Winsock, IP Helper, Media Foundation | HTTPS de l'Assistant, clés protégées, réseau, vidéo | bibliothèques du système Windows |
| libcurl (`libcurl.so.4`) | HTTPS de l'Assistant sous Linux | chargée à la demande (`dlopen`), pas de dépendance à la compilation |
| Npcap (Windows) / libpcap (Linux) | capture de l'espion Modbus | chargés à la demande s'ils sont installés ; option `XPG_SANS_NPCAP` |
| Python 3 | outils, essai `vcxproj` | outil externe |
| Inno Setup ≥ 6.3 | installateur | outil externe (`#if Ver < EncodeVer(6, 3, 0)` dans `installateur/XPGAnalyser.iss`) |

---

<a id="linux"></a>
## 3. Linux : bibliothèques, exécutables sans fenêtre, essais

```bash
python3 outils/pdfium/obtenir_pdfium.py                  # une fois (accès à PyPI)
cmake -S . -B build-linux -G Ninja -DXPG_WITH_SDL=OFF    # sans SDL3 sur la machine
ninja -C build-linux
ctest --test-dir build-linux --output-on-failure
```

- `-DXPG_WITH_SDL=OFF` : les exécutables se construisent mais refusent d'ouvrir une fenêtre (« this binary was compiled without SDL3 support… ») ; `--cli` et `--version` fonctionnent (`App::initPlatform`, `src/main.cpp`).
- Pour une construction Linux **avec** interface, il faut SDL3 installé et connu de CMake. Le script `tools/sessions/rejouer-toutes-les-sessions.sh` rejouait des séances de capture sous Linux avec SDL hors écran (`SDL_VIDEO_DRIVER=offscreen`) et vise encore l'ancien exécutable unique `build/xpg_analyzer` (avant la 1.12) : une telle construction a donc existé, mais aucune note de livraison récente n'en fait état. Pour les versions actuelles, c'est **non vérifié**.
- Options CMake : `XPG_WITH_SDL`, `XPG_BUILD_TESTS`, `XPG_WARNINGS_AS_ERRORS`, `XPG_SANS_NPCAP` ; chemins d'essai `XPG_FIXTURE`, `XHW_FIXTURE`, `XPG_LIBS_DIR` (voir [ARCHITECTURE.md](ARCHITECTURE.md#bibliotheques)).
- Mémoire : `src/hmi/HmiGuideText.cpp` (l'aide F1 générée) est compilé en `-O1` avec GCC/Clang et demande tout de même beaucoup de mémoire (environ 12 Go selon les notes de livraison 1.21.x) ; réduire le parallélisme (`ninja -C build-linux -j 2`) si besoin.
- Détail des essais : [TESTS.md](TESTS.md).

---

<a id="mingw"></a>
## 4. Exécutables Windows compilés sous Linux (MinGW-w64)

Script : `outils/mingw/cross_mingw.sh`. Prérequis indiqués dans son en-tête : `g++-mingw-w64-x86-64-posix`, `cmake`, `ninja` ; plus `third_party/SDL3/lib/x64/SDL3.dll` (voir § 2).

```bash
outils/mingw/cross_mingw.sh            # build-mingw/API-Studio.exe et build-mingw/IHM-Studio.exe
JOBS=2 outils/mingw/cross_mingw.sh     # moins de compilations en parallèle (4 par défaut)
```

Ce que fait le script :

1. fabrique la bibliothèque d'importation MinGW de `SDL3.dll` (`libSDL3.dll.a`) depuis sa table d'exportation ;
2. configure CMake avec `outils/mingw/toolchain-mingw64.cmake` (`x86_64-w64-mingw32-g++-posix`, édition des liens `-static -static-libgcc -static-libstdc++ -mwindows`), `CMAKE_BUILD_TYPE=Release`, `XPG_BUILD_TESTS=OFF`, et `outils/mingw/SDL3Config.cmake` ;
3. construit les cibles `xpg_api` et `xpg_ihm` ;
4. copie `SDL3.dll` et, s'il est présent, `pdfium.dll` à côté des exécutables ;
5. garde une copie **avec symboles** dans `build-mingw/symboles/` (jamais livrée) et retire les symboles des exécutables.

Le runtime C++ étant lié statiquement, seuls `SDL3.dll` et `pdfium.dll` accompagnent les exécutables.

---

<a id="paquet-linux"></a>
## 5. Installateur et versions portables fabriqués sous Linux

Script : `outils/mingw/package_mingw.py` (équivalent de `outils\package.bat /exe:… /chaine:mingw` sous Windows).

```bash
python3 outils/mingw/package_mingw.py --iscc /chemin/vers/ISCC.exe [--portables DOSSIER]
#   options : --exe, --exe-ihm, --sdl, --pdfium, --sans-installateur
```

1. `dist/staging/` : les deux exécutables, `SDL3.dll`, `pdfium.dll` (obligatoire : le script s'arrête s'il manque), `resources/` (`schneider_library.txt`, `plc_catalog.txt`), `libs/`, `maintenance/` (scripts et leur `config.ini`), `licences/`, `LISEZ-MOI.txt` ;
2. `manifeste.json` : chaque fichier, sa taille, son SHA-256, son rôle, et les DLL importées par les exécutables (arrêt si l'une n'est ni du système ni livrée) ;
3. Inno Setup (ISCC) **sous Wine** avec les valeurs de `outils/config.ini` : `API-IHM-Studio-Setup-<version>.exe` à la racine du dépôt (ignoré par Git) ;
4. `dist/SHA256SUMS.txt` ;
5. avec `--portables DOSSIER` : un zip par application, `API-Studio-<version>-portable.zip` et `IHM-Studio-<version>-portable.zip`.

`WINEPREFIX` et `WINEDEBUG` sont lus dans l'environnement.

---

<a id="windows"></a>
## 6. Windows : scripts `outils\` (MSVC, Visual Studio)

Tous les scripts `.bat` de `outils\` lancent leur travail dans `outils\lib\*.ps1` (Windows PowerShell 5.1, présent dans Windows 10 et 11), avec `-ExecutionPolicy Bypass` pour eux seuls ; une stratégie de groupe plus stricte l'emporte et le script le dit.

| Besoin | Script |
|---|---|
| Savoir ce qui est installé et si le compilateur marche vraiment | `check_environment.bat` |
| Installer les outils qui manquent (avec accord) | `install_build_tools.bat` |
| Préparer le projet (config.ini, versions, dossiers) | `configure.bat` |
| Compiler (Release / Debug) | `build.bat` / `build_debug.bat` |
| Tout recompiler | `rebuild.bat` (`/debug` pour Debug) |
| Effacer ce qui a été généré | `clean.bat` (`/tout` : aussi `dist\`) |
| Compiler puis lancer les essais | `test_build.bat` |
| **Tout, dans l'ordre, jusqu'à l'installateur** | **`package.bat`** |
| Refaire seulement l'installateur | `build_installer.bat` |
| Préparer les outils pour un PC sans Internet | `create_offline_toolchain.bat` |

### Avant de commencer

- SDL3 : `XpgAnalyzer.vcxproj` prend `$(ProjectDir)third_party\SDL3` (`SDL3_DIR`) ; `lib\x64\SDL3.lib` et `SDL3.dll` doivent y être (voir § 2). Une autre SDL3 : `msbuild /p:SDL3_DIR=<dossier>`.
- Débloquer une archive téléchargée avant de l'extraire (Propriétés › Débloquer) ; éviter `Program Files`.

### Outils (`check_environment.bat`, `install_build_tools.bat`)

`check_environment.bat` cherche MSVC par `vswhere`, les instances enregistrées de Visual Studio, les emplacements standards, le registre, les variables d'environnement et `config.ini` (`compilateur_manuel`, `iscc`, `vswhere`), puis compile, lie et lance un petit programme C++20 x64. Codes : 0 (tout est là), 11 (compilateur présent, autre outil manquant), 10 (aucun compilateur utilisable).

`install_build_tools.bat` propose six choix : installer automatiquement les Microsoft C++ Build Tools 2022 (composants de `config.ini` : MSVC v143 x64, SDK Windows, CMake/Ninja) puis Inno Setup pour l'utilisateur ; une autre chaîne ; un compilateur déjà installé ; un package hors ligne ; afficher les prérequis ; annuler. Les fichiers téléchargés viennent des adresses HTTPS de `config.ini`, leur signature Authenticode et, quand elle est donnée, leur empreinte SHA-256 sont vérifiées. Un redémarrage n'a jamais lieu sans confirmation ; la procédure reprend à l'ouverture de session suivante.

### Compiler (`build.bat`)

`build.bat` vérifie que la version est la même partout (`outils/config.ini`, `CMakeLists.txt`, `src/core/Version.hpp`, `resources/windows/xpg_analyzer.rc` — voir la [remarque du § 9](#version) sur ce dernier fichier), trouve MSVC, puis lance MSBuild sur `XpgAnalyzer.sln` (x64, `/m`) **deux fois** : `/p:XpgEdition=API` puis `/p:XpgEdition=IHM` (`outils/lib/build.ps1`). Résultat : `build\Release\API-Studio.exe` et `build\Release\IHM-Studio.exe`, avec `SDL3.dll` (et `pdfium.dll` s'il est présent) copiés à côté par l'événement après génération. Chaque exécutable est ensuite lancé avec `--version`, qui doit contenir la version attendue.

Dans Visual Studio : ouvrir `XpgAnalyzer.sln` ; la propriété `XpgEdition` (API par défaut, ou IHM) choisit l'exécutable produit.

### Les essais (`test_build.bat`)

1. compilation Release de l'application, comme `build.bat` ;
2. essais de `CMakeLists.txt` compilés par CMake + Ninja avec le même MSVC (environnement `vcvars64`), **sans SDL**, dans `build\cmake-tests` ;
3. `ctest --output-on-failure`, puis le bilan.

Options : `/filtre:<regex>`, `/propre`, `/sans-application`.

### L'installateur (`package.bat`, `build_installer.bat`)

`package.bat` enchaîne : vérification des versions, compilation Release, essais, dossier `dist\staging` (les deux exécutables, `SDL3.dll`, `pdfium.dll`, le runtime Visual C++ copié à côté des exécutables, `resources\`, `libs\`, `maintenance\`, `licences\`, `LISEZ-MOI.txt`), `manifeste.json`, puis `build_installer.bat` (Inno Setup avec les valeurs de `config.ini`) qui produit **`API-IHM-Studio-Setup-<version>.exe` à la racine du dossier**, et son empreinte dans `dist\SHA256SUMS.txt`.

Options de `package.bat` : `/sans-tests`, `/sans-installateur`, `/exe:<fichier> /sdl:<SDL3.dll> /pdfium:<pdfium.dll> /chaine:msvc|mingw` (empaqueter des exécutables compilés ailleurs ; `IHM-Studio.exe` est pris dans le même dossier que l'exécutable donné).

### Sans surveillance

Chaque script accepte `/non-interactif` (aucune question) et `/oui` (accepte les confirmations, y compris les conditions de licence de Microsoft). Exemple :

```bat
outils\install_build_tools.bat /choix:1 /oui /non-interactif
outils\package.bat /non-interactif
```

Chaque script tient un journal dans `outils\journaux\<script>_<date>.log` (ignoré par Git) et affiche un résumé.

---

<a id="codes"></a>
## 7. Codes de retour des scripts Windows

| Code | Sens |
|---|---|
| 0 | réussi |
| 1 | échec (le message et le journal disent lequel) |
| 2 | annulé, ou versions incohérentes |
| 3 | `config.ini` absent, paramètre `À_RENSEIGNER` nécessaire, ou valeur non prise en charge (`[Compilateur] chaine` autre que msvc, `[Projet] plateforme` autre que x64) |
| 4 | une réponse manque en mode non interactif |
| 5 | droits insuffisants, ou adresse non HTTPS |
| 10 | pas de compilateur utilisable (ou installation des outils échouée) |
| 11 | compilateur présent, autre outil manquant (Inno Setup, CMake/Ninja) |
| 3010 | redémarrage nécessaire (la procédure reprendra ensuite) |
| 9009 | PowerShell ou le script `lib\` introuvable |

Dépannage : [DEPANNAGE.md](DEPANNAGE.md). Paramètres : [CONFIG-INI.md](CONFIG-INI.md).

---

<a id="outils"></a>
## 8. Outils de maintenance du dépôt

| Commande | Rôle |
|---|---|
| `python3 outils/verifier_vcxproj.py` | Vérifie que `XpgAnalyzer.vcxproj` nomme toutes les sources que CMake ramasse (sortie 1 sinon) ; `--corriger` ajoute les manquantes. Lancé aussi par l'essai `vcxproj`. |
| `python3 tools/guide-ihm/generer_guide.py --cpp src/hmi/HmiGuideText.cpp` | Régénère l'aide F1 depuis sa source unique `tools/guide-ihm/guide-ihm.txt` (ne pas modifier `HmiGuideText.cpp` à la main). `--docx` et `--pdf` produisent le guide Word/PDF (Node et son paquet `docx`, Pillow, LibreOffice, `pdftotext`). `tools/guide-ihm/depuis_cpp.py` fait le chemin inverse ou vérifie la concordance. |
| `python3 outils/natives/generer.py` | Régénère `src/hmi/HmiNativesData.cpp` depuis `outils/natives/catalogue.py`. |
| `python3 tools/images/generer_images.py --cpp src/hmi/HmiImageLibraryData.cpp` | Régénère la bibliothèque d'images de l'IHM depuis `tools/images/images.txt` (`--verifier` : le C++ est-il à jour ?). |
| `python3 outils/compter_lignes.py` | Recompte les lignes de code et écrit `src/core/CodeStats.hpp` (affiché par l'accueil et « À propos »). |
| `python3 outils/corpus_declarations.py` | Reconstitue `tests/fixtures/decl/corpus.txt` (essai `hmidecl`). |
| `python3 outils/installateur_images.py` | Fabrique les images de l'installateur (`installateur/images/ui/`). |
| `python3 outils/pdfium/obtenir_pdfium.py [--depuis DOSSIER] [--verifier]` | Pose ou vérifie PDFium (§ 2). |
| `tools/tutoriels/generer_tutoriels.py`, `tools/sessions/` | Didacticiels et séances rejouées (`--script`) qui servent aux captures et aux vérifications. |
| `tools/help_preview.cpp`, `tools/execorder_preview.cpp`, `tools/macros/macro_form.cpp` | Programmes de prévisualisation qui font peindre de vrais widgets par le moteur SVG (non construits par `CMakeLists.txt`). |

---

<a id="version"></a>
## 9. Avant une livraison : la version

La version doit être identique dans quatre fichiers (`src/core/Version.hpp`, rappel en tête) :

- `CMakeLists.txt` : `project(xpg_analyzer VERSION 1.21.3 …)` ;
- `src/core/Version.hpp` : `XPG_ANALYZER_VERSION` ;
- `resources/windows/xpg_analyzer.rc` ;
- `outils/config.ini` : `[Produit] version`.

`outils\build.bat` et `outils\package.bat` vérifient leur concordance sous Windows (`Get-XpgVersionsDuProjet`, `outils/lib/XpgOutils.psm1`).

> **État constaté dans ce commit** : `CMakeLists.txt`, `src/core/Version.hpp` et `outils/config.ini` portent 1.21.3, mais `resources/windows/xpg_analyzer.rc` (le fichier de ressources utilisé par `XpgAnalyzer.vcxproj`) porte encore `1.12.0`. En l'état, `build.bat` et `package.bat` s'arrêteraient donc sur « Versions incohérentes » (code 2) avant de compiler. La chaîne CMake (Linux, MinGW) n'est pas concernée : elle produit sa ressource depuis `resources/windows/xpg_analyzer.rc.in` avec la version de `CMakeLists.txt`.
