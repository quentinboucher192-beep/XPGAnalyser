# Installer, mettre à jour, réparer, désinstaller API IHM Studio

Pour l'utilisateur, sous Windows. Un seul fichier : **`API-IHM-Studio-Setup-<version>.exe`** (pour cette version : `API-IHM-Studio-Setup-1.22.0.exe`). Il installe API Studio et/ou IHM Studio et apporte tout ce dont ils ont besoin, sans Internet : `SDL3.dll`, `pdfium.dll` (lecteur PDF) et, pour une version compilée avec MSVC, le runtime Visual C++ placé à côté des exécutables. Retour au [README](../README.md).

> Mise à jour 1.21.3 : ce guide a été écrit pour la 1.11.2 (installateur `XPGAnalyser-Setup-1.11.2.exe`). Les noms ont été mis à jour d'après `installateur/XPGAnalyser.iss` et `outils/config.ini` : le produit s'appelle **API IHM Studio** depuis la 1.12.20 ; la clé du registre et les dossiers techniques gardent le nom d'origine `XPGAnalyser` / `XpgAnalyzer`.

> **Vérification** : d'après les notes de livraison, l'installateur a été essayé **sous Wine** (installation silencieuse par-dessus la version précédente), **pas sur un vrai Windows**.

## Configuration requise

D'après `installateur/XPGAnalyser.iss` : Windows **10 version 1809 (10.0.17763) ou plus récent**, **x64** (`ArchitecturesAllowed=x64compatible`). L'installation « pour moi seul » ne demande pas de droits d'administrateur (`PrivilegesRequired=lowest`) ; « tous les comptes » les demande.

## 1. Installer

1. Double-clic sur `API-IHM-Studio-Setup-<version>.exe`.
   L'installateur n'est pas signé : Windows SmartScreen peut afficher « Windows a protégé votre ordinateur » ; « Informations complémentaires », puis « Exécuter quand même ». L'empreinte du fichier est dans `dist\SHA256SUMS.txt` ; pour la comparer : `certutil -hashfile API-IHM-Studio-Setup-<version>.exe SHA256`.
2. **Pour qui installer ?**
   - **Moi seul** : sans droits d'administrateur ; dossier proposé `%LOCALAPPDATA%\Programs\API IHM Studio`.
   - **Tous les comptes** : dossier proposé `C:\Program Files\API IHM Studio` ; Windows demande les droits d'administrateur.
3. **Que veux-tu installer ?** (1.14.0) :
   - **Les deux** : API Studio et IHM Studio (le choix habituel) ;
   - **API Studio seul**, ou **IHM Studio seul** : l'exécutable de l'autre application, ses raccourcis et l'ouverture de ses fichiers ne s'installent pas ;
   - **Poste d'exploitation** : IHM Studio sans l'éditeur, qui ouvre un projet en plein écran (`IHM-Studio.exe --poste <dossier du projet>`). Sa page demande le **projet** (un projet FINISH, vérifié par l'installateur), **Démarrer le poste à l'ouverture de la session Windows** (valeur « XPG Poste d'exploitation » de la clé `Run` ; `HKLM` pour tous les comptes) et **Le relancer s'il se ferme** (le lanceur `maintenance\poste.cmd` le relance 5 s après ; trois fermetures de suite moins d'une minute après le lancement l'arrêtent, noté dans `Journaux\poste.log`). Dans le menu Démarrer : seulement « Poste d'exploitation ».

   Une mise à jour reprend le choix d'avant. En changer retire ce qui ne sert plus (exécutable, raccourcis, associations) ; les données restent.
4. **Le dossier du programme** : à la première installation seulement. Une mise à jour garde le même (une installation d'avant la 1.12.20 garde donc son ancien dossier).
5. **Tes données** : où ranger projets, bibliothèque, catalogue et captures. Par défaut `Documents\XPGAnalyser` ; en installation pour tous les comptes, chaque compte a le sien, dans ses Documents. Ce dossier ne peut pas être dans celui du programme. Il se change ensuite dans l'application (accueil › **Dossiers**).
6. **Reprendre tes anciennes données** (seulement si une ancienne version est **installée** sur ce PC) :
   - une ancienne version, c'est un dossier où son programme est présent, ou l'installation du « lot 8 » connue du registre ; sur un PC vierge, cette page n'apparaît pas et le dossier des données est créé **vide, sans aucun projet** ;
   - un autre dossier peut être ajouté à la main (une version portable) ;
   - projets, bibliothèque, catalogue et captures sont **copiés** ; les originaux ne sont jamais modifiés ;
   - un fichier de bibliothèque modifié garde ta version, la version livrée est rangée à côté (`<nom>.livre-<version>`) ;
   - une case propose de désinstaller ensuite la version du lot 8 (ses fichiers de programme seulement).
7. **Ta bibliothèque** (avec API Studio) : la liste des blocs livrés (DFB, DDT, macros), par catégorie. Tu y **ajoutes les tiens** :
   - en les glissant depuis l'Explorateur (`.dfb`, `.ddt`, `.mac`, ou un dossier entier) ;
   - ou avec **Ajouter des fichiers…** et **Ajouter un dossier…** ; **Retirer** enlève les tiens ;
   - ils sont copiés dans `libs\<catégorie>\` de tes données et `libs\index.txt` les connaît ; même nom qu'un bloc déjà là : le tien le remplace, l'autre est rangé à côté.
8. **L'Assistant IA** (1.17.0, avec IHM Studio) : garder le réglage actuel (sur un PC neuf : l'IA intégrée), l'IA intégrée, Ollama sur ce PC, Anthropic (Claude), ou un service compatible OpenAI ; adresse et modèle facultatifs ; « IA intégrée d'abord ». **Aucune clé API ne passe par l'installateur** : elle se saisit dans le logiciel. Une page permet aussi de reprendre l'**apprentissage** d'un autre PC (un fichier `.xia`). Ces choix sont appliqués par IHM Studio au premier lancement de chaque compte.
9. **Options** (seules celles des applications choisies sont proposées) :
   - **sur le Bureau** : un raccourci par application (décochés) ;
   - **Épingler à la barre des tâches** (coché) : au premier lancement, l'application le demande à Windows ;
   - **les fichiers** : ouvrir les `.xihm` avec IHM Studio et les `.xapi` avec API Studio (cochées), et **« Ouvrir avec API Studio » dans le clic droit des `.XPG`** (Control Expert reste leur programme par défaut) ;
   - **le pare-feu Windows** : une règle entrante pour `IHM-Studio.exe`, pour les esclaves Modbus simulés et le poste ; seulement pour une installation pour tous les comptes ; retirée à la désinstallation ;
   - **Garder une copie de secours pour Réparer** (cochée) : la réserve où Réparer reprend un fichier abîmé sans l'installateur.
10. **Prêt à installer** : le résumé.
11. L'installation : **sauvegarde** de la version déjà installée (programme, réglages, registre) ; fermeture propre de l'application si elle est ouverte ; copie ; ajout des fichiers livrés de `resources\` et `libs\` aux données, sans rien écraser ; reprise des anciennes données ; tes blocs ajoutés ; **vérification** de chaque fichier installé (taille et SHA-256, d'après `manifeste.json`). Chaque étape est notée : une installation interrompue se reprend (voir § 4). Le panneau **Détails** montre chaque action en cours ; sans assistant (`/SILENT`, `/VERYSILENT`), les mêmes lignes vont dans le journal (`/LOG`).
12. **Ce qui a été installé** : les programmes (`API-Studio.exe`, `IHM-Studio.exe`, le désinstallateur, `SDL3.dll`, `pdfium.dll`, les scripts de maintenance), leur architecture et leur taille, puis l'arborescence (programme, données, maintenance, menu Démarrer, Bureau, barre des tâches, registre). **Copier** met le texte dans le presse-papiers ; il est aussi enregistré dans `Journaux\recapitulatif_<date>.txt`.

À la fin, un rapport liste ce qui a été repris et gardé : `Journaux\migration_<date>.txt`. Le journal indique aussi combien de **clés API** de l'Assistant ont été gardées (elles sont dans les réglages de l'utilisateur, qu'une réinstallation ne touche pas).

Dans le menu Démarrer (groupe **API IHM Studio**) : API Studio, IHM Studio, **Maintenance**, **Tes données** (installation pour moi seul) et **Désinstaller API IHM Studio**.

**Sur un PC où une stratégie de groupe interdit les scripts PowerShell**, l'installateur le voit et le dit. Il installe quand même le programme, et l'application prépare ses données au premier lancement ; mais il n'y a ni sauvegarde de la version installée, ni reprise des anciennes données, ni vérification, et la maintenance du menu Démarrer ne fonctionne pas sur ce PC.

### Sans surveillance (déploiement)

```bat
API-IHM-Studio-Setup-1.22.0.exe /VERYSILENT /SUPPRESSMSGBOXES /CURRENTUSER /LOG="C:\temp\installation.log"
API-IHM-Studio-Setup-1.22.0.exe /VERYSILENT /SUPPRESSMSGBOXES /ALLUSERS /DONNEES="D:\Donnees API IHM" /MIGRER=non
API-IHM-Studio-Setup-1.22.0.exe /VERYSILENT /ALLUSERS /PROFIL=poste /POSTE="D:\Projets\Armoire" /POSTEAUTO=oui /POSTERELANCE=oui
```

| Option | Rôle |
|---|---|
| `/CURRENTUSER` ou `/ALLUSERS` | pour qui installer |
| `/DIR="…"` | le dossier du programme |
| `/DONNEES="…"` | le dossier des données |
| `/MIGRER=oui\|non` | reprend toutes les anciennes versions trouvées, ou aucune |
| `/DESINSTALLERLOT8=non` | ne désinstalle pas la version du lot 8 |
| `/PROFIL=deux\|api\|ihm\|poste` | ce qui est installé ; sans lui : le choix de l'installation d'avant, sinon les deux |
| `/POSTE="…"` | le projet du poste d'exploitation (obligatoire avec `/PROFIL=poste`, sauf à une mise à jour) |
| `/POSTEAUTO=oui\|non` | le poste démarre à l'ouverture de la session (oui par défaut) |
| `/POSTERELANCE=oui\|non` | le poste est relancé s'il se ferme (non par défaut) |
| `/IA=integree\|ollama\|anthropic\|compatible\|garder`, `/IAADRESSE=…`, `/IAMODELE=…`, `/IADABORD=oui\|non` | réglage initial de l'Assistant IA (jamais de clé API) |
| `/APPRENTISSAGE="…\fichier.xia"` | apprentissage de l'Assistant repris d'un autre PC |
| `/COMPOSITION=non` | avec l'assistant : retire le double tampon de Windows si sa fenêtre se dessine mal |
| `/ANIMATIONS=non` | l'assistant ne s'anime pas (sinon il suit le réglage de Windows) |
| `/TASKS="…"`, `/MERGETASKS="…"` | les options : `bureauapi`, `bureauihm`, `bureauposte`, `barre`, `xihm`, `xapi`, `xpg`, `parefeu`, `reserve` ; `/MERGETASKS="!reserve"` : sans copie de secours |
| `/SUPPRIMERDONNEES=oui` | (désinstallation) envoie les données à la Corbeille |

Un paramètre faux (`/PROFIL=autre`, le poste sans son projet…) arrête l'installateur sans rien installer ; le journal (`/LOG`) dit pourquoi.

## 2. Où sont les choses

| Quoi | Où |
|---|---|
| Le programme (remplacé à chaque mise à jour) | `%LOCALAPPDATA%\Programs\API IHM Studio` ou `C:\Program Files\API IHM Studio` (ou le dossier d'une installation antérieure, gardé) |
| Tes données | `Documents\XPGAnalyser` (ou le dossier choisi) : `projets\api\`, `projets\ihm\`, `libs\`, `resources\`, `captures\` |
| Tes réglages | `%APPDATA%\XpgAnalyzer` : `XPGAnalyser.ini` (tes dossiers), `themes\`, `images\` (Mes images), `IA\` (Assistant IA) ; par application : `API\` et `IHM\` (`settings.txt`, `reprise\`, `crashs\`, `signalements\`) |
| Journaux, sauvegardes, réserve de réparation, état | `%LOCALAPPDATA%\XpgAnalyzer\` (pour toi seul) ou `%ProgramData%\XPGAnalyser\` (tous les comptes) ; la réserve : `Reserve\<version>`, celle de la version installée et celle de la précédente |
| Le poste d'exploitation | `maintenance\poste.cmd` (s'il relance le poste), la valeur « XPG Poste d'exploitation » de `HKCU` ou `HKLM\Software\Microsoft\Windows\CurrentVersion\Run`, `[Poste]` dans `installation.ini` |
| Registre | `HKCU` ou `HKLM\Software\XPGAnalyser` (`InstallDir`, `Version`, `Portee`, `DossierDonnees`, `Profil`…), et la fiche « Applications installées » |
| Les archives d'un projet | `.xihm` (IHM Studio) et `.xapi` (API Studio) : `HKCU` ou `HKLM\Software\Classes` (`APIIHMStudio.xihm`, `APIIHMStudio.xapi`) ; la désinstallation les enlève |

Dans l'application, l'accueil (lien **Dossiers**), **Projet › Dossiers de l'application…** et **À propos** montrent ces dossiers. Chacun a un bouton **Ouvrir** ; les données, les projets, la bibliothèque et les captures ont aussi **Changer…**, avec copie du contenu si demandée ; l'ancien dossier n'est jamais effacé. Le format de ces fichiers : [CONFIG-INI.md](CONFIG-INI.md) et [FORMATS.md](FORMATS.md#emplacements).

## 3. Mettre à jour

Lancer le nouvel installateur : même `AppId`, donc Windows voit une mise à jour, pas un deuxième logiciel. Le dossier, le choix « pour qui », le profil et le dossier des données sont repris. La version installée est sauvegardée d'abord, puis l'application est fermée proprement. Les fichiers livrés non modifiés sont mis à jour ; ceux que tu as modifiés sont gardés, et la nouvelle version est rangée à côté. Les 3 dernières sauvegardes sont gardées (`[Maintenance] sauvegardes_a_garder`).

## 4. En cas de problème (menu Démarrer › API IHM Studio › Maintenance)

| Raccourci (script) | Ce qu'il fait |
|---|---|
| **Vérifier l'installation** (`verify_installation.bat`) | Vérifie chaque fichier (taille, SHA-256), les DLL, le runtime, le registre, les raccourcis, les droits, la configuration, les chemins, une installation interrompue. **Rien n'est modifié.** |
| **Réparer** (`repair.bat`) | Diagnostic, puis réparation de ce qui est cassé seulement. |
| **Reprendre une installation interrompue** (`resume_installation.bat`) | Refait les étapes non terminées, ou revient à la version d'avant. `/migrer:"D:\ancien"` reprend plus tard un ancien dossier oublié. |
| **Restaurer une sauvegarde** (`restore_backup.bat`) | Remet le programme, les réglages et le registre d'une sauvegarde. L'état actuel est sauvegardé d'abord. |
| **Réinitialiser mes réglages** (`reset_user_settings.bat`) | Affichage seul, + dossiers, ou tout. Les fichiers sont mis de côté dans une sauvegarde, pas effacés. |
| **Collecter les journaux** (`collect_diagnostics.bat`) | Un zip sur le Bureau : journaux, état, vérification, description du PC. Ni tes projets, ni ta bibliothèque. Rien n'est envoyé. |

`repair.bat` propose neuf modes : diagnostic uniquement ; réparation automatique ; fichiers du programme ; raccourcis ; paramètres et chemins ; dépendances (SDL3, runtime) ; reprise d'une installation interrompue ; installation précédente ; réinstallation complète en gardant les données.

Avant de modifier quoi que ce soit : l'application est fermée proprement (forcée seulement avec ton accord) et une sauvegarde est faite. Les fichiers manquants ou abîmés sont repris dans la **réserve** (SHA-256 vérifié avant la copie). Sans réserve, la réparation le dit et donne la commande : `repair.bat /mode:reinstaller /installateur:<API-IHM-Studio-Setup-<version>.exe>`. L'exécutable d'une application non installée n'est ni vérifié ni réparé. Tes projets et tes données ne sont jamais touchés. Rapport : `Journaux\repair_<date>.txt`. Sans question : `repair.bat /mode:auto /oui /non-interactif`.

Une installation pour tous les comptes se répare en administrateur : le script propose de se relancer avec les droits, pour cette opération seulement.

## 5. Désinstaller

Taper « désinstaller » dans la recherche Windows : **Désinstaller API IHM Studio** (menu Démarrer). Ou : Paramètres › Applications › Applications installées › **API IHM Studio** › Désinstaller.

- Par défaut, tes données (projets, bibliothèque, captures), tes réglages et tes sauvegardes **restent**.
- Répondre « Non » à « Les garder ? » ouvre une confirmation : il faut taper **SUPPRIMER**. Ils partent alors dans la **Corbeille**. Un dossier réglé ailleurs (une bibliothèque sur un partage réseau) n'est jamais supprimé.
- Le programme, ses raccourcis, ses clés de registre (et les associations `.xihm`, `.xapi`, « Ouvrir avec » des `.XPG`, le démarrage du poste), toutes les copies de secours, la règle du pare-feu et l'état sont retirés.

Sans surveillance : `unins000.exe /VERYSILENT` (dans le dossier du programme) garde les données ; `/SUPPRIMERDONNEES=oui` les envoie à la Corbeille.

## 6. Versions portables

Chaque livraison fournit aussi un zip par application : `API-Studio-<version>-portable.zip` et `IHM-Studio-<version>-portable.zip` (fabriqués par `outils/mingw/package_mingw.py --portables`). Sans `installation.ini` à côté de l'exécutable, l'application range projets, bibliothèque, ressources et captures dans son **dossier de travail** (ou celui de l'exécutable s'il contient `resources\`), et ignore `XPGAnalyser.ini` (`src/app/Dossiers.hpp`, `src/main.cpp`). Les réglages restent dans `%APPDATA%\XpgAnalyzer`.

## 7. La recette à faire sur un PC Windows

Tout cela n'a été essayé que sous Wine (voir `RAPPORT-ANALYSE.md`, § 10, et les notes de livraison). Sur un PC Windows de test :

1. Sur un PC où une version précédente est installée, avec un projet : installer « pour moi seul », « Reprendre » coché ; vérifier les projets dans `Documents\XPGAnalyser\projets` et le rapport `Journaux\migration_<date>.txt`.
2. Lancer ; accueil › Dossiers : chaque dossier s'ouvre.
3. Réinstaller par-dessus (mise à jour) : la sauvegarde de la version d'avant apparaît dans `restore_backup.bat /liste`.
4. Supprimer `SDL3.dll` du dossier du programme, puis Maintenance › Réparer › Réparation automatique : l'application redémarre.
5. Désinstaller en gardant les données : `Documents\XPGAnalyser` est toujours là.
