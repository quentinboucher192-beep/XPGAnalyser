# Compiler XPGAnalyser sans Visual Studio

Ce guide va d'un PC vierge (Windows 10 ou 11, 64 bits) jusqu'à l'installateur `XPGAnalyser-Setup-1.11.2.exe`. Tout se fait avec les scripts de `outils\`, par double-clic ou depuis un terminal ; l'interface de Visual Studio n'est jamais ouverte.

## En bref

| Tu veux… | Lance |
|---|---|
| savoir ce qui est installé, et si le compilateur marche vraiment | `check_environment.bat` |
| installer les outils qui manquent (avec ton accord) | `install_build_tools.bat` |
| préparer le projet (config.ini, versions, dossiers) | `configure.bat` |
| compiler (Release / Debug) | `build.bat` / `build_debug.bat` |
| tout recompiler | `rebuild.bat` (`/debug` pour Debug) |
| effacer ce qui a été généré | `clean.bat` (`/tout` : aussi `dist\` ; l'installateur de la racine reste) |
| compiler puis lancer les tests | `test_build.bat` |
| **tout, dans l'ordre, jusqu'à l'installateur** | **`package.bat`** |
| refaire seulement l'installateur | `build_installer.bat` |
| préparer les outils pour un PC sans Internet | `create_offline_toolchain.bat` |

Sur un PC vierge, un seul geste suffit : **double-clic sur `outils\package.bat`**. Il voit qu'il n'y a pas de compilateur, te pose la question du cahier des charges (§ 4) avec ses six choix, installe ce que tu acceptes, puis reprend tout seul : compilation, tests, installateur.

## 1. Avant de commencer

- **Débloque le zip** avant de l'extraire : clic droit sur le zip › Propriétés › cocher « Débloquer » › OK. Sinon Windows marque chaque fichier « venu d'Internet » et peut demander une confirmation à chaque script. `check_environment.bat` le signale.
- Extrais-le où tu veux. Les chemins avec des espaces ou des accents sont acceptés. Évite seulement `Program Files`, où il faut être administrateur pour écrire.
- PowerShell 5.1 fait partie de Windows 10 et 11 : il n'y a rien à installer. Les scripts le lancent pour eux seuls avec `-ExecutionPolicy Bypass`, ce qui ne change rien au réglage de la machine. Une stratégie de groupe (GPO) plus stricte l'emporte : PowerShell refuse alors le script, et le `.bat` le dit (« PowerShell n'a pas pu lancer… », avec le nom de la stratégie) sans la contourner.

## 2. Les outils (`check_environment.bat`, `install_build_tools.bat`)

**`check_environment.bat`** ne se contente pas du PATH. Il cherche aussi avec `vswhere`, dans les instances enregistrées de Visual Studio (`C:\ProgramData\Microsoft\VisualStudio\Packages\_Instances`), aux emplacements standards, dans le registre (SDK Windows, Inno Setup), dans les variables d'environnement et dans `config.ini` (`compilateur_manuel`, `iscc`, `vswhere`). Il affiche le compilateur, sa version, les architectures disponibles, l'emplacement, CMake, Ninja, MSBuild, le SDK, les dépendances présentes et manquantes, la compatibilité et la chaîne retenue.

Ensuite il fait un **vrai essai** : un petit programme C++20 est compilé, lié et lancé ; son code de retour (42) et son architecture (x64, lue dans l'en-tête PE) sont vérifiés ; les fichiers temporaires sont supprimés.

Codes de retour :

- 0 : tout est là ;
- 11 : le compilateur marche, mais il manque autre chose (Inno Setup, CMake/Ninja, un fichier du projet) ;
- 10 : aucun compilateur utilisable.

**`install_build_tools.bat`** affiche le message exact du cahier des charges et ses six choix :

1. **Installer automatiquement le compilateur recommandé** : les Microsoft C++ Build Tools 2022, avec seulement ces composants : MSVC v143 x64, SDK Windows, CMake/Ninja. MSBuild fait partie des Build Tools. Inno Setup est installé ensuite, pour toi seul et sans droits.
   - Avant d'installer : le nom, l'éditeur, les composants, la raison, les droits, Internet, l'espace disque, les conditions et les autres solutions. Rien n'est lancé sans ton accord.
   - Les fichiers viennent des sources de `config.ini`, en HTTPS. Leur signature Authenticode est vérifiée (Microsoft Corporation ; Pyrsys B.V. pour Inno Setup), ainsi que leur SHA-256 quand `config.ini` en donne un. Un fichier refusé n'est jamais lancé.
   - Les droits d'administrateur sont demandés au moment de l'installation des Build Tools, pour elle seule.
   - Si Windows doit redémarrer, l'état est enregistré et la question est posée ; il n'y a jamais de redémarrage sans confirmation. La procédure reprend toute seule à l'ouverture de session suivante (clé RunOnce), puis relance ce que tu avais lancé : `package.bat`, `build.bat` ou `test_build.bat`.
   - Après l'installation : environnement relu, nouvelle détection, versions, SDK, essai (compiler, lier, lancer), résultat enregistré dans `outils\etat\compilateur.json`, puis la compilation reprend.
2. **Choisir une autre chaîne compatible** : explique pourquoi MinGW-w64 et LLVM/Clang ne sont pas automatisés pour ce projet (rapport d'analyse, § 6), et essaie MinGW s'il est présent.
3. **Sélectionner manuellement un compilateur déjà installé** : tu donnes le dossier d'un Visual Studio 2022 ou des Build Tools. Il est essayé, puis retenu dans `config.ini` (`compilateur_manuel`).
4. **Utiliser un package hors ligne** : le dossier fait par `create_offline_toolchain.bat` (empreintes revérifiées), ou un `vs_BuildTools.exe` que tu as déjà. Sans Internet, Inno Setup est pris dans ce package, même si le compilateur est déjà là : indique-le dans `config.ini` (`[HorsLigne] dossier`) ou par `/dossier:<package>`.
5. **Afficher les prérequis** : la liste exacte des composants, l'espace, les adresses.
6. **Annuler** : rien n'est installé (code 2).

Sans Internet : c'est détecté en 8 secondes au plus, puis le choix 4 et la liste des composants sont proposés.

## 3. Compiler (`build.bat`)

`build.bat` vérifie que la version est la même partout (config.ini, CMakeLists.txt, `src\core\Version.hpp`, le `.rc`), trouve MSVC, puis lance MSBuild sur `XpgAnalyzer.sln` (x64, `/m`). Le projet et les réglages sont ceux que tu as dans Visual Studio. Le résultat est `build\Release\XpgAnalyzer.exe`, avec `SDL3.dll` à côté.

L'exe est ensuite vérifié : `XpgAnalyzer.exe --version` doit répondre `XPGAnalyser 1.11.2`.

Sans compilateur, `build.bat` lance lui-même l'installation assistée, puis reprend.

## 4. Les tests (`test_build.bat`)

1. La compilation Release de l'application, comme `build.bat`.
2. Les tests de `CMakeLists.txt`, compilés par CMake + Ninja avec le même MSVC. L'environnement vient de `vcvars64` ; les tests sont compilés sans SDL, car ils n'ouvrent pas de fenêtre. Le dossier est `build\cmake-tests`.
3. `ctest --output-on-failure`, puis le bilan, sous la forme « 73 sur 73 réussis » (la 1.8.0 a 73 tests ; ici, sous Linux avec GCC, ils passent tous).

Options :

- `/filtre:<regex>` : ne lance que certains tests ;
- `/propre` : repart d'un dossier vide ;
- `/sans-application` : saute l'étape 1.

## 5. L'installateur (`package.bat`, `build_installer.bat`)

`package.bat` fait tout, dans l'ordre :

1. les versions ;
2. la compilation Release ;
3. les tests ;
4. `dist\staging`, le dossier à livrer :
   - l'exe, `SDL3.dll` et le **runtime Visual C++** (les DLL de `Microsoft.VC143.CRT`, copiées à côté de l'exe : un PC vierge n'a rien à installer) ;
   - `resources\`, `libs\`, `maintenance\` (les six scripts d'après l'installation), `licences\`, `LISEZ-MOI.txt` ;
5. `manifeste.json` : chaque fichier, sa taille, son SHA-256, son rôle, et les DLL que l'exe importe (lues dans sa table d'importation ; `package.bat` s'arrête si l'une d'elles manque) ;
6. `build_installer.bat` : Inno Setup, avec les valeurs de `config.ini`, produit **`XPGAnalyser-Setup-1.11.2.exe` à la racine du dossier**, le seul fichier à lancer pour installer ; son empreinte va dans `dist\SHA256SUMS.txt`.

Les questions viennent au début : sans compilateur, celle des six choix ; avec un compilateur mais sans Inno Setup, `package.bat` propose d'installer Inno Setup (pour toi seul, sans droits) avant de compiler. `build_installer.bat` seul fait de même.

Options de `package.bat` :

- `/sans-tests` ;
- `/sans-installateur` ;
- `/exe:<fichier> /sdl:<SDL3.dll> /chaine:mingw` : empaqueter un exe compilé ailleurs. C'est ainsi que l'installateur de ce zip a été fabriqué.

## 6. Sans surveillance

Chaque script accepte `/non-interactif` : aucune question, les valeurs par défaut, ou un arrêt propre qui dit quelle option manque. `/oui` accepte les confirmations, y compris les conditions de licence de Microsoft : c'est alors toi qui les acceptes d'avance. Exemple pour un PC de fabrication :

```bat
outils\install_build_tools.bat /choix:1 /oui /non-interactif
outils\package.bat /non-interactif
```

Chaque script tient un journal dans `outils\journaux\<script>_<date>.log` : chaque étape, chaque commande, sa sortie, son code. Il affiche aussi un **résumé** à la fin. S'il a été lancé par un double-clic, la fenêtre attend Entrée avant de se fermer.

## 7. Les codes de retour

| Code | Sens |
|---|---|
| 0 | réussi |
| 1 | échec (le message et le journal disent lequel) |
| 2 | annulé, ou versions incohérentes |
| 3 | `config.ini` absent, paramètre `À_RENSEIGNER` nécessaire, ou valeur non prise en charge (`[Compilateur] chaine` autre que msvc, `[Projet] plateforme` autre que x64) |
| 4 | une réponse manque en mode non interactif |
| 5 | droits insuffisants, ou adresse non HTTPS |
| 10 | pas de compilateur utilisable (ou installation des outils échouée) |
| 11 | compilateur OK, autre outil manquant (Inno Setup, CMake/Ninja) ; `package.bat` le dit dès le début si Inno Setup n'a pas pu être installé |
| 3010 | redémarrage nécessaire (la procédure reprendra ensuite) |
| 9009 | PowerShell ou le script `lib\` introuvable |

## 8. Ce que je n'ai pas pu faire ici

Je n'ai pas de Windows. Aucune compilation MSVC n'a donc été faite de mon côté, et ces scripts n'ont pas encore tourné sous Windows. Ils sont analysés par PowerShell 7 (syntaxe, règles de compatibilité 5.1) et la mise en paquet a tourné sous Linux. Ton premier `package.bat` sera leur première exécution réelle. Si une étape échoue, son journal dit laquelle et pourquoi : envoie-le moi (`collect_diagnostics.bat` le rassemble).
