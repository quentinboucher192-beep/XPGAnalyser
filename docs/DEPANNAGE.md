# Dépannage

Retour au [README](../README.md). Compilation : [GUIDE-COMPILATION.md](GUIDE-COMPILATION.md) ; installation : [GUIDE-INSTALLATION.md](GUIDE-INSTALLATION.md).

Sous Windows, d'abord : **`collect_diagnostics.bat`**. Il crée un zip sur le Bureau avec les journaux, l'état, la vérification et la description du PC, sans tes projets. Chaque script écrit aussi son journal : `outils\journaux\` pour la compilation, `Journaux\` de l'installation pour la maintenance.

## Compilation

| Symptôme | Cause probable | Que faire |
|---|---|---|
| « Windows a protégé… » ou une question à chaque script | Le zip n'a pas été débloqué : les fichiers sont marqués « Internet » | Clic droit sur le zip › Propriétés › Débloquer, puis extraire à nouveau. |
| « … ne peut pas être chargé car l'exécution de scripts est désactivée », puis « PowerShell n'a pas pu lancer lib\… » | Une stratégie de groupe (GPO) interdit les scripts, même pour un seul processus ; le `.bat` affiche son nom | `RemoteSigned` : débloque le zip et extrais-le à nouveau. Sinon, les scripts ne la contournent pas : demande à l'administrateur du PC, ou compile sur un autre poste. |
| `check_environment` : code 10 | Pas de MSVC x64 avec MSBuild | `install_build_tools.bat`, choix 1 (ou 3 si Visual Studio 2022 est déjà installé ailleurs). |
| « Installateur refusé : signature… » | Le fichier téléchargé n'est pas signé par l'éditeur attendu (proxy, téléchargement corrompu) | Supprime `%TEMP%\xpg-outils\` et relance. Si ça persiste : le package hors ligne (choix 4). |
| Installation des Build Tools : code 1602 | Annulée dans l'installateur de Microsoft | Relance ; rien n'a été installé. |
| Code 8006 | Un programme de Visual Studio est ouvert | Ferme-le, relance. |
| Code 3010 | Un redémarrage est nécessaire | Accepte-le quand tu veux : à l'ouverture de session, la procédure reprend toute seule et relance ce que tu avais lancé (`package.bat`, `build.bat`…). |
| Package hors ligne : l'installateur de Microsoft refuse ses paquets | Les certificats de la disposition ne sont pas connus du PC hors ligne | Installe les fichiers `buildtools\certificates\*.p12` (double-clic, magasin « Ordinateur local »), puis relance le choix 4. |
| MSBuild : `SDL3 introuvable dans …` | Le dossier `third_party\SDL3` manque, ou ne contient que les en-têtes (cas du dépôt Git : `lib\x64\` n'y est pas versionné) | Extraire le zip des sources en entier, ou poser `SDL3.lib` et `SDL3.dll` du paquet officiel dans `third_party\SDL3\lib\x64\` (voir `third_party/SDL3/ORIGINE.txt`). |
| MSBuild : erreurs C2xxx dans le code | Une construction propre à MSVC dans le code nouveau de la 1.8.0 (voir le rapport d'analyse, § 10) | Envoie le journal de `build`. |
| `--version` ne répond pas après la compilation | Une DLL manque à côté de l'exe (SDL3.dll, runtime) | Le journal le dit ; `rebuild.bat`. |
| `test_build` : CMake ou Ninja manque | Composant « C++ CMake tools for Windows » absent | `install_build_tools.bat /forcer`, ou Visual Studio Installer › Modifier. |
| `package.bat` : « Le runtime Visual C++ à livrer est introuvable » | Build Tools installés sans le redistribuable | Visual Studio Installer › Modifier › Composants individuels › « MSVC v143 … redistribuable ». |
| `build_installer` : Inno Setup introuvable | Pas installé, et tu as refusé de l'installer | Relance `build_installer.bat` (ou `package.bat`) et accepte, ou `install_build_tools.bat /inno-seulement`. |
| `build.bat` / `package.bat` : « Versions incohérentes » (code 2) | Une des quatre versions diffère. Jusqu'à la 1.21.3, `resources\windows\xpg_analyzer.rc` était resté à `1.12.0` (aligné en 1.22.0) | Voir [GUIDE-COMPILATION.md § 9](GUIDE-COMPILATION.md#version) : la version doit être la même dans les quatre fichiers. |
| Linux, `cmake` : SDL3 introuvable (`find_package(SDL3 CONFIG REQUIRED)`) | Pas de SDL3 installé pour CMake | `-DXPG_WITH_SDL=OFF` (bibliothèques, essais, `--cli`), ou installer SDL3 pour une construction avec interface (non vérifiée). |
| Un exécutable affiche « this binary was compiled without SDL3 support » | Construit avec `XPG_WITH_SDL=OFF` | Normal : seuls `--cli` et `--version` fonctionnent dans cette construction. |
| Linux : l'essai `hmidocviewer` échoue | `third_party/pdfium/linux-x64/libpdfium.so` absent (hors dépôt) | `python3 outils/pdfium/obtenir_pdfium.py` (accès à PyPI). |
| La compilation s'arrête faute de mémoire sur `src/hmi/HmiGuideText.cpp` | Fichier d'aide généré très gros (environ 12 Go de mémoire d'après les notes de livraison) | Compiler avec moins de tâches en parallèle (`ninja -j 2`, `JOBS=2 outils/mingw/cross_mingw.sh`). |
| `cross_mingw.sh` : `SDL3.dll` introuvable | `third_party/SDL3/lib/x64/` n'est pas dans le dépôt (seuls les en-têtes y sont) | Y poser `SDL3.dll` et `SDL3.lib` du paquet officiel (voir `third_party/SDL3/ORIGINE.txt`). |

## Installation et utilisation

| Symptôme | Que faire |
|---|---|
| « La sauvegarde de la version installée n'a pas pu être faite » | Rien n'a été modifié. Le journal (chemin affiché) dit pourquoi : disque plein, droits. |
| « Une étape n'a pas abouti » à la fin de l'installation | Le programme est installé. Maintenance › Reprendre une installation interrompue. |
| « Windows empêche PowerShell de lancer les étapes de l'installation » | Une stratégie de groupe interdit les scripts. Le programme s'installe quand même, sans sauvegarde, reprise ni vérification. Pour les avoir : l'administrateur autorise les scripts, puis relance l'installateur. |
| L'application dit que son dossier des données est inaccessible (lecteur réseau absent, clé USB) | Elle utilise `Documents\XPGAnalyser` pour cette fois (ou, s'il est lui aussi inaccessible, `%LOCALAPPDATA%\XpgAnalyzer\Donnees`). Rebranche le lecteur, ou change le dossier : accueil › Dossiers. |
| Un projet récent n'apparaît plus | Il est peut-être sur un support absent : `verify_installation.bat` liste les projets introuvables. |
| L'application ne démarre plus | Maintenance › Réparer › Réparation automatique. |
| Retour à la version d'avant | Maintenance › Restaurer une sauvegarde (`/precedente`). |
| Une bibliothèque modifiée « a disparu » après une mise à jour | Elle n'est jamais écrasée : ta version est en place, la version livrée est à côté (`.livre-<version>`). Le rapport `migration_<date>.txt` les liste. |

## Codes de retour

Voir `GUIDE-COMPILATION.md` § 7. Pour la maintenance :

| Code | Sens |
|---|---|
| 20 | aucune installation trouvée |
| 21 | aucune sauvegarde (ou introuvable) |
| 6 | l'application est restée ouverte, rien n'a été modifié |
| 7 | l'installateur est introuvable (réinstallation) |

## Assistant IA

| Symptôme | Que faire |
|---|---|
| « Pas de connexion possible sur ce poste » (Linux) | libcurl (`libcurl.so.4`) n'est pas installée : l'Assistant la charge à la demande. |
| « http:// n'est accepté que vers ce poste… » | Utiliser `https://`. `http://` n'est accepté que vers `localhost`/`127.x`, et vers une adresse privée pour un service compatible OpenAI sans clé ([ASSISTANT-IA.md](ASSISTANT-IA.md#fournisseurs)). |
| La clé API est à ressaisir à chaque séance (Linux) | Normal : sous Linux, la clé n'est gardée qu'en mémoire. |
| « La réponse a été coupée » | Augmenter **Réponse** dans les Paramètres, ou demander en plusieurs fois ; voir [ASSISTANT-IA.md § 9](ASSISTANT-IA.md#suite). |
