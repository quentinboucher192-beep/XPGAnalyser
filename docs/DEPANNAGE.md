# Dépannage

D'abord : **`collect_diagnostics.bat`**. Il crée un zip sur le Bureau avec les journaux, l'état, la vérification et la description du PC, sans tes projets. Chaque script écrit aussi son journal : `outils\journaux\` pour la compilation, `Journaux\` de l'installation pour la maintenance.

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
| MSBuild : `SDL3 introuvable dans …` | Le dossier `third_party\SDL3` manque | Il est dans le zip : extrais-le en entier. |
| MSBuild : erreurs C2xxx dans le code | Une construction propre à MSVC dans le code nouveau de la 1.8.0 (voir le rapport d'analyse, § 10) | Envoie le journal de `build`. |
| `--version` ne répond pas après la compilation | Une DLL manque à côté de l'exe (SDL3.dll, runtime) | Le journal le dit ; `rebuild.bat`. |
| `test_build` : CMake ou Ninja manque | Composant « C++ CMake tools for Windows » absent | `install_build_tools.bat /forcer`, ou Visual Studio Installer › Modifier. |
| `package.bat` : « Le runtime Visual C++ à livrer est introuvable » | Build Tools installés sans le redistribuable | Visual Studio Installer › Modifier › Composants individuels › « MSVC v143 … redistribuable ». |
| `build_installer` : Inno Setup introuvable | Pas installé, et tu as refusé de l'installer | Relance `build_installer.bat` (ou `package.bat`) et accepte, ou `install_build_tools.bat /inno-seulement`. |

## Installation et utilisation

| Symptôme | Que faire |
|---|---|
| « La sauvegarde de la version installée n'a pas pu être faite » | Rien n'a été modifié. Le journal (chemin affiché) dit pourquoi : disque plein, droits. |
| « Une étape n'a pas abouti » à la fin de l'installation | Le programme est installé. Maintenance › Reprendre une installation interrompue. |
| « Windows empêche PowerShell de lancer les étapes de l'installation » | Une stratégie de groupe interdit les scripts. Le programme s'installe quand même, sans sauvegarde, reprise ni vérification. Pour les avoir : l'administrateur autorise les scripts, puis relance l'installateur. |
| L'application dit que son dossier des données est inaccessible (lecteur réseau absent, clé USB) | Elle utilise `Documents\XPGAnalyser` pour cette fois. Rebranche le lecteur, ou change le dossier : accueil › Dossiers. |
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
