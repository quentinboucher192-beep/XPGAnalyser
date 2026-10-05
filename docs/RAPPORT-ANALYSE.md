# Rapport d'analyse — XPGAnalyser 1.8.0

La chaîne de compilation, d'installation, de mise à jour, de réparation et de désinstallation (29/09/2026).

Ce rapport décrit le projet **tel qu'il est** (l'archive du lot API 8), puis ce qui a été retenu pour le compiler sans Visual Studio, l'installer sur un PC vierge et le maintenir. Il répond à la section 1 du cahier des charges. Les autres documents sont `docs\GUIDE-COMPILATION.md`, `docs\GUIDE-INSTALLATION.md`, `docs\CONFIG-INI.md` et `docs\DEPANNAGE.md`.

---

## 1. L'architecture actuelle

| Élément | Constat |
|---|---|
| Nature | Application de bureau **C++20**. La fenêtre, le rendu, les événements, le presse-papiers et le son passent par **SDL3**. L'interface est entièrement dessinée par le programme : ni Qt, ni MFC, ni .NET, ni boîte de dialogue Win32 (sauf l'explorateur de fichiers du système, en option). |
| Taille | 305 fichiers `.cpp` et 265 en-têtes dans `src\`, environ 324 000 lignes avec les tests. |
| Modules (`src\`) | `core` (résultats, signaux, commandes), `domain` (le modèle d'un projet Control Expert), `import` (.XPG, .XHW, .XDB, .XEF), `project` (dossier de projet, versions, bibliothèque partagée), `sim` (simulation du programme), `hmi` (IHM, Modbus, poste d'exploitation), `export`, `xls` (lecture/écriture Excel), `help`, `ui` (widgets dessinés), `menu` (pile de menus et dialogues), `platform` (SDL3, polices), `app` (les écrans), `main.cpp`. |
| Deux modes | L'application graphique, et `--cli` : analyse d'un export sans fenêtre (code de retour 0 si aucune erreur). |
| Projet Visual Studio | `XpgAnalyzer.sln` + `XpgAnalyzer.vcxproj` : VS 2022, outils **v143**, **x64** seulement, Debug et Release, `stdcpp20`, `/permissive- /W4 /utf-8 /Zc:__cplusplus /Zc:preprocessor`, jeu de caractères Unicode. Sortie : `build\<Configuration>\XpgAnalyzer.exe`, objets dans `build\obj\`. SDL3 est pris dans `third_party\SDL3` (aucun chemin absolu) et un événement après génération copie `SDL3.dll` à côté de l'exe. Sous-système **Console**. Runtime : `/MD` en Release, `/MDd` en Debug. `WindowsTargetPlatformVersion` vaut `10.0`, c'est-à-dire le SDK Windows 10/11 le plus récent installé. |
| CMake | `CMakeLists.txt` (CMake 3.24 ou plus) : les bibliothèques `xpg_core`, `xpg_xls`, `xpg_import`, `xpg_ui`, `xpg_help`, `xpg_hmi`, l'exe `xpg_analyzer` et **72 tests** (ctest ; 73 en 1.8.0, avec celui des dossiers). Options : `XPG_WITH_SDL`, `XPG_BUILD_TESTS`, `XPG_WARNINGS_AS_ERRORS`, `XPG_SANS_NPCAP`. Sous Windows, `resources\windows\xpg_analyzer.rc.in` ajoute l'icône et la version. |
| Scripts existants | `build.sh` et `runtests.sh`, pour Linux : c'est ainsi que j'ai vérifié chaque lot. Il n'y avait aucun script Windows. |
| Architecture ciblée | **Windows 64 bits (x64)** seulement : le paquet SDL3 fourni est x64. |
| Compilateur utilisé | **MSVC v143** (Visual Studio 2022) chez toi : ton raccourci lance `build\Debug\XpgAnalyzer.exe`. De mon côté, l'installateur du lot 8 a été fabriqué avec MinGW-w64 (GCC), faute de Windows ici. |
| SDK Windows | Tout SDK Windows 10/11 (10.0.x). Le code n'utilise que des API Win32 classiques : dossiers connus (shell32), registre (advapi32), réseau (ws2_32, iphlpapi), COM de base (ole32). |

## 2. Les dépendances détectées

| Dépendance | Version / licence | Comment | Livrée ? |
|---|---|---|---|
| SDL3 | 3.4.12 · zlib | DLL : `third_party\SDL3` (paquet officiel VC, SHA-256 dans `ORIGINE.txt`) | Oui, `SDL3.dll` à côté de l'exe |
| miniz | MIT | compilé dans l'exe | — |
| nanosvg, nanosvgrast | zlib | compilés | — |
| stb_image, stb_image_write, stb_truetype | domaine public / MIT | compilés | — |
| minimp3 | CC0 | compilé | — |
| Runtime Visual C++ 2015-2022 | Microsoft (redistribuable) | exigé par une compilation MSVC `/MD` | **Oui, déploiement local** : les DLL de `Microsoft.VC143.CRT` à côté de l'exe (voir § 6) |
| Universal CRT (ucrtbase.dll) | Windows | fait partie de Windows 10/11 | Non (déjà présente) |
| DLL système | Windows | kernel32, user32, gdi32, shell32, ole32, oleaut32, uuid, advapi32, comdlg32, ws2_32, iphlpapi | Non (Windows) |
| Npcap | facultatif | la capture réseau de l'espion Modbus ; chargé seulement s'il est installé | Non, jamais livré |
| Polices | Windows | Segoe UI, Consolas (`resources\fonts\` en secours, facultatif) | Non |

**Absent du projet** : Qt, .NET, MFC/ATL, LLVM, toute bibliothèque précompilée autre que SDL3, et tout SDK industriel ou propriétaire. Les exports Control Expert sont lus comme des fichiers ; aucune DLL Schneider n'est chargée.

## 3. Ressources, icônes, configuration

- `resources\schneider_library.txt` (les blocs Schneider), `resources\plc_catalog.txt` (les références d'automates), `resources\logo\` (le SVG source, les PNG de 16 à 512 pixels, `xpg_analyzer.ico` de 16 à 256).
- `resources\windows\xpg_analyzer.rc` : l'icône (ressource 1) et la `VERSIONINFO`, restée à **1.0.0**. Elle passe à 1.8.0.
- `libs\` : la bibliothèque partagée, soit 123 fichiers (`index.txt`, puis `Alarms`, `Client`, `Comm`, `Control`, `Equipment`, `IO`, `Macros`, `Measure`, `System` : les `.dfb`, `.ddt` et `.mac`). L'application **y écrit** : macros enregistrées, blocs ajoutés.
- Ton raccourci `XpgAnalyzer.lnk` pointe vers une icône de ta machine (`…\Downloads\Designer(1).ico`). L'installateur prend l'icône intégrée à l'exe.
- Réglages : `%APPDATA%\XpgAnalyzer\settings.txt` (`clé = valeur`), `themes\` (`.xpgtheme`), `reprise\` (la reprise après plantage), `modeles-vues\`.

## 4. Où l'application écrit aujourd'hui

| Quoi | Où | Remarque |
|---|---|---|
| Projets | `projets\` du **dossier de travail** | |
| Bibliothèque | `libs\` du dossier de travail | écrite par les macros et les blocs |
| Catalogue, bibliothèque Schneider | `resources\` du dossier de travail | lus, et le catalogue se complète |
| Captures F12 | `captures\` du dossier de travail | |
| Réglages, thèmes, reprise, modèles de vues | `%APPDATA%\XpgAnalyzer\` | hors du dossier du programme : déjà bien |
| Démarrage du poste d'exploitation | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, valeur « XPG Poste d'exploitation » | écrite par l'application, avec le chemin de l'exe |
| Installateur du lot 8 | `HKCU\Software\XpgAnalyzer\InstallDir`, `HKCU\…\Uninstall\XpgAnalyzer` | |

Le **dossier de travail** est celui d'où l'application est lancée. Depuis le lot 8, s'il ne contient pas `resources\`, l'application se place dans le dossier de son exe. Les projets, la bibliothèque et les captures finissent donc **dans le dossier du programme**. Dans `Program Files`, un utilisateur sans droits d'administrateur ne peut pas y écrire, et le cahier des charges l'interdit. C'est la seule chose à corriger dans le logiciel pour pouvoir l'installer (§ 8).

**Formats de projets utilisateur** : un dossier de projet (`project.xpgproj`, les versions, `sections\`, `tables\`, `config\`, `exports\`), les imports `.XPG` / `.XHW` / `.XDB` / `.XEF`, et les fichiers de l'application `.xpgvues`, `.xpgmodele`, `.xpglayout`, `.xpgtheme`. Rien n'en change.

## 5. Les anciennes versions à reprendre

| Ancienne version | Où | Ce qui s'y trouve |
|---|---|---|
| Ton dossier de développement | `C:\Program Files (x86)\XpgAnalyzer` d'après ton raccourci, et tout autre dossier que tu indiques à l'installateur | `projets\`, `libs\`, `resources\` à la racine. Si le raccourci démarre dans `build\Debug`, il peut aussi y avoir `build\Debug\projets\`. |
| L'installateur du lot 8 (NSIS) | `%LOCALAPPDATA%\Programs\XpgAnalyzer`, trouvé par `HKCU\Software\XpgAnalyzer\InstallDir` | `xpg_analyzer.exe`, `projets\`, `libs\`, `resources\` |
| La version portable du lot 8 | là où tu l'as extraite (l'installateur demande le dossier) | idem |
| Les réglages | `%APPDATA%\XpgAnalyzer\` | gardés sur place, sauvegardés avant toute retouche |
| Le démarrage du poste d'exploitation | clé Run | réécrit vers le nouvel exe s'il visait un ancien |

**Règles de la migration** :

- On **copie** puis on vérifie (tailles et SHA-256). On ne déplace rien et on ne supprime rien dans un ancien dossier.
- Ton dossier de développement n'est **jamais modifié**, puisque ce sont tes sources.
- Si un même fichier de bibliothèque diffère entre l'ancien dossier et la version livrée :
  - s'il a exactement le contenu d'un fichier livré par un lot précédent (une liste d'empreintes est fournie), il est remplacé par la version 1.8.0 ;
  - sinon, c'est **ta** version : elle est gardée, et la version livrée est rangée à côté sous le nom `<nom>.livre-1.11.2`.
- L'installation du lot 8 est désinstallée **après** la reprise. Seuls ses fichiers de programme partent (son désinstallateur garde déjà tes données).

## 6. La chaîne retenue, et pourquoi

| Étape | Outil | Raison |
|---|---|---|
| Compiler | **MSVC v143 (Microsoft C++ Build Tools 2022) + MSBuild** sur `XpgAnalyzer.sln`, sans l'interface de Visual Studio | Le même compilateur, la même ABI et le même runtime qu'aujourd'hui. Le paquet SDL3 fourni est compilé pour MSVC (`SDL3.lib`). Aucun risque de changer le comportement ou le format des binaires. |
| Tester | **CMake + Ninja** avec le même MSVC | Les 73 tests sont décrits dans `CMakeLists.txt`. CMake et Ninja sont livrés avec les Build Tools (composant « C++ CMake tools »), donc rien de plus à installer. |
| Fabriquer l'installateur | **Inno Setup 6** | Un vrai `.exe` graphique : choix « pour moi seul / pour tous », mise à jour sur place (même `AppId`), désinstallation, sans technologie en plus. |
| Automatiser | Des **`.bat`** qui lancent **PowerShell 5.1** | Les `.bat` restent en ASCII, lançables par double-clic ou depuis un terminal. PowerShell 5.1 est présent sur tout Windows 10/11 : registre, vswhere, SHA-256, signatures Authenticode, journaux en UTF-8. |

**Composants des Build Tools installés** (ni l'interface de Visual Studio, ni le reste) : `Microsoft.VisualStudio.Workload.VCTools`, `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` (MSVC v143), `Microsoft.VisualStudio.Component.Windows11SDK.22621` (SDK Windows) et `Microsoft.VisualStudio.Component.VC.CMake.Project` (CMake et Ninja). MSBuild fait partie des Build Tools. La liste est modifiable dans `outils\config.ini`.

**Les autres chaînes** :

- **MinGW-w64 (GCC)** a déjà servi à l'installateur du lot 8 (voir « Ce qui n'a pas pu être fait ici »). Mais c'est une autre ABI : SDL3 doit être pris dans sa version MinGW, et le runtime C++ n'est pas le même. `install_build_tools.bat` la présente en choix 2 **sans l'automatiser** : il explique ces écarts et, si MinGW est déjà là, fait l'essai de base (compiler, lier et lancer un petit programme x64). Le projet lui-même resterait à valider : compilation complète, démarrage, ouverture d'un `.XPG`.
- **clang-cl** garde l'ABI Microsoft mais demande quand même le SDK et l'éditeur de liens de Microsoft : il n'apporte rien ici.

**Le runtime Visual C++ sur un PC vierge** : un exe MSVC `/MD` a besoin de `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`, etc. `package.bat` copie les DLL de `VC\Redist\MSVC\<version>\x64\Microsoft.VC143.CRT\`, présentes dans les Build Tools, **à côté de l'exe**. C'est le déploiement local, que Microsoft autorise. Il ne demande **ni droits d'administrateur ni Internet**, et marche aussi en installation « pour moi seul ». En contrepartie, ces copies ne sont pas mises à jour par Windows Update, mais chaque version de XPGAnalyser apporte les siennes.

## 7. Les incompatibilités potentielles

1. **L'exe de ce zip est fabriqué avec MinGW**, parce que je n'ai pas Windows : `XPGAnalyser-Setup-1.11.2.exe` a été construit ici comme au lot 8. Le tien, fabriqué par `package.bat`, sera compilé par MSVC. Le code est le même, mais l'équivalence n'est pas démontrée (arrondis d'affichage, par exemple).
2. **Le nom de l'exe** : MSBuild produit `XpgAnalyzer.exe` et CMake `xpg_analyzer.exe`. L'installateur livre `XpgAnalyzer.exe` dans les deux cas.
3. **Le sous-système** : en Release, l'exe passe du sous-système Console au sous-système Windows, donc plus de fenêtre noire. `--cli` et `--version` écrivent toujours dans le terminal qui les lance. La version Debug garde sa console.
4. **La page de code** : le projet Visual Studio n'avait pas le manifeste UTF-8 que l'exe MinGW du lot 8 avait déjà. Je l'ajoute, pour que les chemins accentués (`C:\Users\Opérateur`) marchent partout, même par les API « A ».
5. **Les données dans le dossier du programme** : c'est incompatible avec `Program Files`. Voir le § 8.
6. **Le dossier de développement dans `Program Files (x86)`** : sans droits d'administrateur, l'application ne peut pas y écrire (un exe 64 bits n'a pas la virtualisation de fichiers de Windows). La migration lit ce dossier sans rien y écrire.
7. **Les tests** : ils ne sont compilés que par GCC ici. Leur compilation par MSVC sera faite pour la première fois par `test_build.bat` chez toi.
8. **Windows** : Windows 10 64 bits à partir de la 1809 (donc aussi la LTSC 2019, fréquente sur les PC industriels), ou Windows 11. En 1809, la page de code UTF-8 du manifeste est ignorée : un chemin accentué peut y poser problème ; à partir de la 1903, non. ARM64 n'est pas couvert.
9. **Stratégies de sécurité** : les scripts demandent `-ExecutionPolicy Bypass` pour **leur seul processus**, ce qui ne change rien sur la machine. Si une stratégie de groupe (GPO) impose autre chose, elle l'emporte et PowerShell refuse de lancer le script : le `.bat` s'en aperçoit (le script n'a pas démarré), nomme la stratégie et dit quoi faire, sans la contourner.
10. **Pas de certificat** : Windows SmartScreen avertira au premier lancement de `Setup.exe` (« Informations complémentaires », puis « Exécuter quand même »). Un zip téléchargé marque ses fichiers : débloque-le avant de l'extraire (Propriétés › Débloquer). `check_environment.bat` le signale.

## 8. Ce qui change dans le logiciel (le strict nécessaire)

1. **Le dossier des données.** À côté de l'exe installé, un fichier `installation.ini` donne le dossier des données. Par défaut, c'est `Documents\XPGAnalyser` ; il est choisi pendant l'installation. Au démarrage, l'application en fait son dossier de travail et le prépare au premier lancement (copie de `resources\` et `libs\` livrés). Tout le reste du code (`projets\`, `libs\`, `captures\`, `resources\` relatifs) ne change pas. **Sans `installation.ini`** (ta version de développement, la version portable, les sessions rejouées), l'application fait exactement comme aujourd'hui.
2. **Les dossiers, réglables et ouvrables.** C'est ta demande de 23 h 18. `%APPDATA%\XpgAnalyzer\XPGAnalyser.ini` peut changer le dossier des données, les projets, la bibliothèque et les captures. L'accueil (lien **Dossiers**) et le menu **Projet › Dossiers de l'application…** les montrent, les ouvrent dans l'Explorateur, les changent (avec la copie du contenu si tu le demandes ; l'ancien dossier n'est jamais effacé), et ouvrent le `.ini`.
3. **`--version`** : affiche `XPGAnalyser 1.11.2` et sort. Les scripts s'en servent pour vérifier que l'exe installé démarre.
4. **Release sans console**, voir le § 7.3.
5. **La version 1.8.0** partout : `CMakeLists.txt`, `.rc`, `src\core\Version.hpp` et `outils\config.ini`. `package.bat` vérifie qu'elles concordent.

## 9. Les paramètres restant à renseigner

Tout est dans **`outils\config.ini`**. Ce qui manque y est marqué `À_RENSEIGNER`, et les scripts s'arrêtent avec un message clair tant que c'est nécessaire.

| Paramètre | Valeur | État |
|---|---|---|
| `nom` (Applications installées), `editeur` | XPGAnalyser | donné le 29/09 |
| `version` | 1.8.0 | donné le 29/09 |
| `app_id` | un GUID fixé une fois pour toutes | fait. **Ne jamais le changer** : c'est lui qui fait d'une nouvelle version une mise à jour. |
| `certificat` | aucun | donné (« pas de certificat ») : signature désactivée |
| `url_support` | vide | facultatif (le lien « Aide » dans Applications installées) |
| `[Outils] buildtools_sha256` | vide | volontaire : Microsoft change ce fichier à chaque version, c'est sa signature Authenticode qui est vérifiée |
| `[Outils] innosetup_sha256` | l'empreinte de `innosetup-6.7.2.exe` | constatée le 29/09/2026 sur la publication officielle |
| `[HorsLigne] dossier` | `À_RENSEIGNER` | seulement si tu utilises un package hors ligne |
| La suite du cahier des charges (après « Dans » de la section 9) | — | **non reçue** : la chaîne couvre ce que la liste du début annonçait (installation, mise à jour, migration, réparation, reprise, restauration, désinstallation, documentation) |

## 10. Les risques identifiés

| Risque | Effet | Parade |
|---|---|---|
| **Aucune compilation MSVC n'a pu être faite ici** (pas de Windows) | Une erreur propre à MSVC dans le peu de code nouveau n'apparaîtra qu'à ton premier `build.bat` | Code nouveau court, C++20 standard. `build.bat` s'arrête au premier échec avec le journal. |
| Les scripts PowerShell ne sont **pas exécutés sous Windows** ici | Registre, vswhere, installation des Build Tools et raccourcis tournent pour la première fois chez toi | Analyse syntaxique par PowerShell ; logique commune essayée ici ; chaque étape vérifie son code de retour et le dit. |
| Inno Setup essayé **sous wine**, pas sous Windows | Écarts d'affichage ou de comportement possibles | Sous wine : l'assistant page par page, l'installation, la mise à jour par-dessus, le lancement et la désinstallation. Sous wine, `powershell.exe` n'est qu'un bouchon : la sauvegarde, la reprise, la vérification et la réparation ont été essayées à part (PowerShell 7 sous Linux, installation simulée). À refaire chez toi (`docs\GUIDE-INSTALLATION.md`, « Recette »). |
| Téléchargement des Build Tools : 2 à 3 Go, 20 à 60 min | Longue première installation | Package hors ligne (`create_offline_toolchain.bat`), reprise après coupure. |
| Installateur non signé | SmartScreen, antivirus méfiants | Procédure documentée ; le SHA-256 de `Setup.exe` est publié dans `dist\SHA256SUMS.txt`. |
| Déploiement local du runtime VC++ | Pas de correctifs par Windows Update | Chaque version apporte ses DLL ; `repair.bat` les vérifie. |
| Conflits dans la bibliothèque | Deux versions d'un même bloc | Rien n'est écrasé sans sauvegarde : ta version reste, la livrée est rangée à côté ; le rapport de migration les liste. |
| Dossier des données sur un lecteur réseau absent | L'application ne trouve pas ses projets | Au démarrage : repli sur `Documents\XPGAnalyser` avec un message ; `repair.bat` le signale. |
| PC où une stratégie de groupe interdit les scripts PowerShell | Pas de sauvegarde, de reprise, de vérification ni de maintenance | L'installateur le détecte (le script n'a pas démarré), le dit et installe quand même le programme ; l'application prépare ses données au premier lancement. |
