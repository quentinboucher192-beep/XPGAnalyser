# outils\config.ini, clé par clé

Un seul fichier de paramètres pour toute la chaîne. Il est lu par tous les scripts de `outils\` et par `build_installer.bat`, qui passe ses valeurs à Inno Setup. `package.bat` en tire `maintenance\config.ini` pour les scripts installés avec le programme.

**Règles du fichier**

- **Valeurs** :
  - une valeur marquée **`À_RENSEIGNER`** manque : le script qui en a besoin s'arrête (code 3) et dit laquelle ;
  - une valeur vide veut dire la valeur par défaut, ou « sans ».
- **Chemins** :
  - absolus, ou relatifs au dossier du projet ;
  - jetons acceptés, sans tenir compte des majuscules : `{Documents}`, `{LocalAppData}`, `{AppData}`, `{ProgramData}`, `{ProgramFiles}`, `{ProgramFiles(x86)}`, `{pf}`, `{pf32}`, `{Racine}` (le dossier du projet), et les `%VARIABLES%` de Windows ;
  - deux clés sont lues par l'installateur, sur le PC où l'on installe : `{Racine}` n'y a pas de sens. `dossier_donnees_par_defaut` ne prend que les jetons que l'application comprend (`{Documents}`, `{AppData}`, `{LocalAppData}`, `{ProgramData}` et les `%VARIABLES%`). `anciens_dossiers` prend aussi les constantes d'Inno Setup (`{localappdata}`…) ; un dossier au jeton inconnu y est ignoré, et le journal de l'installation le dit.
- **Syntaxe** :
  - un commentaire en fin de ligne commence par « ` ;` », avec un espace avant ;
  - encodage UTF-8.

## [Produit]

| Clé | Valeur livrée | Rôle |
|---|---|---|
| `nom` | XPGAnalyser | Le nom dans « Applications installées », le menu Démarrer, l'installateur (ta demande du 29/09). |
| `editeur` | XPGAnalyser | L'éditeur de la fiche « Applications installées ». |
| `version` | 1.11.2 | Doit être la même que dans `CMakeLists.txt`, `src\core\Version.hpp` et `resources\windows\xpg_analyzer.rc` ; `build.bat` et `package.bat` le vérifient. |
| `app_id` | {68A57092-…} | L'identité de l'installation pour Windows. **Ne jamais la changer** : c'est elle qui fait d'une nouvelle version une mise à jour. |
| `exe` | XpgAnalyzer.exe | Le nom de l'exe installé (celui que produit MSBuild). |
| `description` | … | Le commentaire des raccourcis et de la fiche. |
| `url_support` | vide | Facultatif : le lien « Aide » de la fiche. |

## [Projet]

| Clé | Rôle |
|---|---|
| `solution` | Ce que MSBuild compile (`XpgAnalyzer.sln`). |
| `vcxproj` | Le projet de la solution ; `configure.bat` vérifie qu'il est là. |
| `plateforme` | `x64`, la seule possible (le SDL3 fourni est x64) : `build.bat` s'arrête sur une autre valeur. |
| `cmake_tests` | Le dossier de construction des tests (`build\cmake-tests`). |

## [Compilateur]

| Clé | Rôle |
|---|---|
| `chaine` | `msvc` : la chaîne du projet (rapport d'analyse, § 6), la seule automatisée : `build.bat` s'arrête sur une autre valeur. |
| `compilateur_manuel` | Le dossier d'un Visual Studio 2022 ou des Build Tools, choisi à la main (choix 3 de `install_build_tools.bat`, qui l'écrit ici). Il passe avant les autres. |
| `msvc_version_min` | 14.30 (v143). Une version plus ancienne est signalée. |

## [Outils]

| Clé | Rôle |
|---|---|
| `vswhere`, `iscc` | Un vswhere.exe ou un ISCC.exe précis. Vide : trouvés tout seuls. |
| `buildtools_url` | La source officielle des Build Tools (HTTPS obligatoire). |
| `buildtools_editeur` | L'éditeur attendu dans la signature Authenticode (Microsoft Corporation). |
| `buildtools_sha256` | Vide exprès : Microsoft change ce fichier à chaque version ; c'est la signature qui est vérifiée. |
| `buildtools_licence` | L'adresse des conditions de Microsoft, affichée avant l'installation. |
| `buildtools_composants` | Les composants, et eux seuls, séparés par `;`. |
| `buildtools_dossier` | Vide : le dossier par défaut de Microsoft. |
| `buildtools_espace_go` | L'espace à prévoir sur C:. |
| `innosetup_version`, `innosetup_url` | Inno Setup 6.7.2, depuis la publication officielle (dépôt jrsoftware/issrc sur GitHub). |
| `innosetup_editeur`, `innosetup_sha256` | Pyrsys B.V. et l'empreinte du fichier, constatées le 29/09/2026. Si tu changes de version, mets à jour les trois clés. |

## [HorsLigne]

| Clé | Rôle |
|---|---|
| `dossier` | Le package fait par `create_offline_toolchain.bat` (qui propose de l'écrire ici). `À_RENSEIGNER` tant que tu ne t'en sers pas : ce n'est demandé que pour le choix 4. Sans Internet, Inno Setup y est aussi pris quand le compilateur est déjà là. |

## [Installateur]

| Clé | Rôle |
|---|---|
| `sortie` | `dist` : le dossier de travail de l'installateur, avec `staging\` (ce qui est installé) et `SHA256SUMS.txt`. L'installateur lui-même, `XPGAnalyser-Setup-<version>.exe`, est posé à la racine du dossier ; celui d'une autre version y est rangé dans `dist\anciens\`. |
| `dossier_donnees_par_defaut` | `{Documents}\XPGAnalyser` : proposé pendant l'installation (chaque compte a le sien). |
| `anciens_dossiers` | Où chercher les anciennes versions à reprendre (séparés par `;`). L'installation du lot 8 est aussi trouvée par le registre. |
| `certificat` | Vide : pas de signature (ta réponse du 29/09). Windows SmartScreen avertit au premier lancement. La signature n'est pas automatisée dans cette version : une valeur ici est seulement signalée par `build_installer.bat`, l'installateur n'est pas signé. |

## [Maintenance]

| Clé | Rôle |
|---|---|
| `sauvegardes_a_garder` | 3 : les plus anciennes sauvegardes partent au-delà. |

## Les deux autres .ini (côté utilisateur)

- **`installation.ini`**, à côté du programme installé, est écrit par l'installateur ; ne pas le modifier.
  - Il donne la version, la portée et la date.
  - Il donne le dossier des données par défaut (`[Dossiers] donnees`).
  - Il donne les dossiers de la maintenance : état, journaux, sauvegardes, réserve.
  - Sa présence dit à l'application qu'elle est installée. Sans lui (ta version de développement, la version portable), l'application fait exactement comme avant.
- **`%APPDATA%\XpgAnalyzer\XPGAnalyser.ini`** contient tes dossiers : `[Dossiers]` `donnees`, `projets`, `bibliotheque`, `captures`.
  - Il est écrit par la fenêtre **Dossiers** (accueil, ou menu Projet).
  - Il se modifie aussi à la main, l'application fermée.
  - Une valeur vide veut dire le défaut.
  - Il accepte les jetons `{Documents}`, `{AppData}`, `{LocalAppData}`, `{ProgramData}`, `{Programme}`, `{Donnees}`.
  - L'ancien fichier est gardé en `XPGAnalyser.ini.precedent`.
