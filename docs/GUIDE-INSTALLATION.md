# Installer, mettre à jour, réparer, désinstaller XPGAnalyser

Pour l'utilisateur, sur un PC vierge ou non. Un seul fichier, à la racine du dossier : **`XPGAnalyser-Setup-1.11.2.exe`**. Il fait tout et apporte tout ce dont le programme a besoin, sans Internet : SDL3 et, pour une version compilée avec MSVC, le runtime Visual C++, placé à côté de l'exe.

## 1. Installer

1. Double-clic sur `XPGAnalyser-Setup-1.11.2.exe`.
   L'installateur n'est pas signé : Windows SmartScreen peut afficher « Windows a protégé votre ordinateur ». Clique sur « Informations complémentaires », puis sur « Exécuter quand même ». L'empreinte du fichier est dans `dist\SHA256SUMS.txt` ; pour la comparer : `certutil -hashfile XPGAnalyser-Setup-1.11.2.exe SHA256`.
2. **Pour qui installer ?**
   - **Moi seul** : sans droits d'administrateur, dans `%LOCALAPPDATA%\Programs\XPGAnalyser`.
   - **Tous les comptes** : dans `C:\Program Files\XPGAnalyser` ; Windows demande les droits d'administrateur.
3. **Le dossier du programme** : à la première installation seulement. Une mise à jour garde le même.
4. **Tes données** : où ranger tes projets, ta bibliothèque, ton catalogue et tes captures. Par défaut `Documents\XPGAnalyser` ; en installation pour tous les comptes, chaque compte a le sien, dans ses Documents. Ce dossier ne peut pas être dans celui du programme. Il se change ensuite dans l'application (accueil › **Dossiers**).
5. **Reprendre tes anciennes données** (seulement si une ancienne version est **installée** sur ce PC) :
   - une ancienne version, c'est un dossier où son programme (`XpgAnalyzer.exe`) est là, ou l'installation du lot 8 connue du registre. Un dossier qui n'a que des projets (une copie des sources, un dossier `build\`) n'est pas proposé : sur un PC vierge, cette page n'apparaît pas et ton dossier des données est créé **vide, sans aucun projet** ;
   - tu peux toujours ajouter un autre dossier à la main (une version portable) ;
   - leurs projets, leur bibliothèque, leur catalogue et leurs captures sont **copiés** ; les originaux ne sont jamais modifiés ;
   - un fichier de bibliothèque que tu as modifié garde ta version, et la version livrée est rangée à côté (`<nom>.livre-1.11.2`) ;
   - une case propose de désinstaller ensuite la version du lot 8 : seulement ses fichiers de programme, son désinstallateur garde tes données.
6. **Ta bibliothèque** : la liste Windows des blocs livrés (DFB, DDT, macros), rangés par catégorie. Tu y **ajoutes les tiens** :
   - en les glissant depuis l'Explorateur (des fichiers `.dfb`, `.ddt`, `.mac`, ou un dossier entier, sous-dossiers compris) ;
   - ou avec **Ajouter des fichiers…** et **Ajouter un dossier…** ; **Retirer** enlève les tiens (les blocs livrés restent) ;
   - « Tes blocs sont rangés » : d'après leur dossier (un dossier `Control` va dans Control ; sinon `Perso`), ou dans la catégorie choisie ;
   - à l'installation, ils sont copiés dans `libs\<catégorie>\` de tes données et `libs\index.txt` les connaît. Le nom et la version sont lus dans le fichier (`name =`, `version =`). Même nom qu'un bloc déjà là : **le tien le remplace**, l'autre est rangé à côté (`.livre-1.11.2`).
7. **Raccourcis** : un raccourci sur le Bureau (décoché), et **Épingler XPGAnalyser à la barre des tâches** (coché). Windows interdit à un installateur d'épingler un programme : au premier lancement, XPGAnalyser le demande à Windows, qui affiche sa confirmation ; si Windows ne le permet pas, XPGAnalyser te montre le geste (clic droit sur son icône dans la barre des tâches › Épingler). Une seule fois par compte.
8. **Prêt à installer** : le résumé (pour qui, où, tes données, ce qui est repris, ta bibliothèque, les raccourcis).
9. L'installation :
   - une **sauvegarde** de la version déjà installée (programme, réglages, registre) ;
   - la fermeture propre de l'application si elle est ouverte ;
   - la copie ;
   - tes données : les fichiers livrés de `resources\` et `libs\` sont ajoutés sans rien écraser ;
   - la reprise des anciennes données ;
   - tes blocs ajoutés (page Ta bibliothèque) ;
   - la **vérification** de chaque fichier installé (taille et SHA-256, d'après `manifeste.json`).

   Chaque étape est notée : si le PC s'éteint en plein milieu, l'installation se reprend (voir § 4).

10. **Ce qui a été installé** : les programmes (`XpgAnalyzer.exe`, le désinstallateur, `SDL3.dll`, les scripts de maintenance), leur **architecture** (64 bits x64, 32 bits x86) et leur taille, puis l'arborescence : le programme, tes données, la maintenance, le menu Démarrer, le Bureau, la barre des tâches, le registre. La zone défile ; **Copier** met le texte dans le presse-papiers ; **Suivant** mène à la fin. Le même texte est enregistré : `Journaux\recapitulatif_<date>.txt` (aussi en installation sans surveillance).

À la fin, un rapport liste ce qui a été repris et gardé : `Journaux\migration_<date>.txt`.

Dans le menu Démarrer, **Désinstaller XPGAnalyser** est à côté de XPGAnalyser : la recherche Windows le trouve dès que tu tapes « désinstaller ».

**Sur un PC où une stratégie de groupe interdit les scripts PowerShell**, l'installateur le voit et le dit. Il installe quand même le programme, et l'application prépare elle-même tes données au premier lancement. Mais il n'y a ni sauvegarde de la version installée, ni reprise des anciennes données, ni vérification ; la maintenance du menu Démarrer ne marche pas non plus sur ce PC. Rien n'est contourné : c'est à l'administrateur d'autoriser les scripts.

### Sans surveillance (déploiement)

```bat
XPGAnalyser-Setup-1.11.2.exe /VERYSILENT /SUPPRESSMSGBOXES /CURRENTUSER /LOG="C:\temp\xpg.log"
XPGAnalyser-Setup-1.11.2.exe /VERYSILENT /SUPPRESSMSGBOXES /ALLUSERS /DONNEES="D:\Données XPG" /MIGRER=non
```

| Option | Rôle |
|---|---|
| `/CURRENTUSER` ou `/ALLUSERS` | pour qui installer |
| `/DIR="…"` | le dossier du programme |
| `/DONNEES="…"` | le dossier des données |
| `/MIGRER=oui` | reprend toutes les anciennes versions trouvées ; `non` n'en reprend aucune |
| `/DESINSTALLERLOT8=non` | ne désinstalle pas la version du lot 8 |

## 2. Où sont les choses

| Quoi | Où |
|---|---|
| Le programme (remplacé à chaque mise à jour) | `%LOCALAPPDATA%\Programs\XPGAnalyser` ou `C:\Program Files\XPGAnalyser` |
| Tes données | `Documents\XPGAnalyser` (ou le dossier choisi) : `projets\`, `libs\`, `resources\`, `captures\` |
| Tes réglages | `%APPDATA%\XpgAnalyzer` : `settings.txt`, `XPGAnalyser.ini` (tes dossiers), `themes\`, `reprise\` |
| Journaux, sauvegardes, réserve de réparation, état | `%LOCALAPPDATA%\XpgAnalyzer\` (pour toi seul) ou `%ProgramData%\XPGAnalyser\` (tous les comptes) |
| Registre | `HKCU` ou `HKLM\Software\XPGAnalyser`, et la fiche « Applications installées » |

Dans l'application, l'accueil (lien **Dossiers**) et le menu **Projet › Dossiers de l'application…** montrent tous ces dossiers. Chacun a un bouton **Ouvrir** (l'Explorateur). Les données, les projets, la bibliothèque et les captures ont aussi **Changer…**, avec la copie du contenu si tu la demandes ; l'ancien dossier n'est jamais effacé. Le bouton **Ouvrir XPGAnalyser.ini** permet de modifier le fichier à la main. Les projets et les captures changent tout de suite ; les données et la bibliothèque, au prochain démarrage.

## 3. Mettre à jour

Lance le nouvel installateur : même `AppId`, donc Windows voit une mise à jour, pas un deuxième logiciel. Le dossier, le choix « pour qui » et le dossier des données sont repris. La version installée est sauvegardée d'abord, puis l'application fermée proprement. Les fichiers livrés que tu n'as pas modifiés sont mis à jour ; ceux que tu as modifiés sont gardés, et la nouvelle version est rangée à côté. Les 3 dernières sauvegardes sont gardées.

## 4. En cas de problème (menu Démarrer › XPGAnalyser › Maintenance)

| Raccourci (script) | Ce qu'il fait |
|---|---|
| **Vérifier l'installation** (`verify_installation.bat`) | Vérifie chaque fichier (taille, SHA-256), les DLL, le runtime, le registre, les raccourcis, les droits, la configuration, les chemins, une installation interrompue. **Rien n'est modifié.** |
| **Réparer** (`repair.bat`) | Diagnostic, puis réparation de ce qui est cassé seulement (voir ci-dessous). |
| **Reprendre une installation interrompue** (`resume_installation.bat`) | Refait les étapes non terminées, ou revient à la version d'avant. `/migrer:"D:\ancien"` reprend plus tard un ancien dossier oublié. |
| **Restaurer une sauvegarde** (`restore_backup.bat`) | Remet le programme, les réglages et le registre d'une sauvegarde. L'état actuel est sauvegardé d'abord. |
| **Réinitialiser mes réglages** (`reset_user_settings.bat`) | Affichage seul, + dossiers, ou tout. Les fichiers sont mis de côté dans une sauvegarde, pas effacés. |
| **Collecter les journaux** (`collect_diagnostics.bat`) | Un zip sur le Bureau : journaux, état, vérification, description du PC. Ni tes projets, ni ta bibliothèque. Rien n'est envoyé. |

**`repair.bat`** propose ses neuf modes :

1. diagnostic uniquement ;
2. réparation automatique ;
3. fichiers du programme ;
4. raccourcis ;
5. paramètres et chemins ;
6. dépendances (SDL3, runtime) ;
7. reprise d'une installation interrompue ;
8. installation précédente ;
9. réinstallation complète en gardant tes données.

Avant de modifier quoi que ce soit : l'application fermée proprement (forcée seulement avec ton accord) et une sauvegarde. Les fichiers manquants ou abîmés sont repris dans la **réserve** laissée par l'installateur (leur SHA-256 est vérifié avant la copie). Les raccourcis et le registre sont refaits. Tes projets et tes données ne sont jamais touchés. Un rapport est écrit : `Journaux\repair_<date>.txt`. En mode automatique, sans question : `repair.bat /mode:auto /oui /non-interactif`.

Une installation pour tous les comptes se répare en administrateur : le script propose de se relancer avec les droits, pour cette opération seulement.

## 5. Désinstaller

Tape « désinstaller » dans la recherche Windows : **Désinstaller XPGAnalyser** (menu Démarrer › XPGAnalyser). Ou : Paramètres › Applications › Applications installées › **XPGAnalyser** › Désinstaller.

- Par défaut, tes données (projets, bibliothèque, captures), tes réglages et tes sauvegardes **restent**.
- Répondre « Non » à « Les garder ? » ouvre une confirmation : il faut taper **SUPPRIMER**. Ils partent alors dans la **Corbeille**, pas directement à la poubelle. Un dossier réglé ailleurs (une bibliothèque sur un partage réseau) n'est jamais supprimé.
- Le programme, ses raccourcis, ses clés de registre, la réserve et l'état sont retirés.

Sans surveillance : `unins000.exe /VERYSILENT` (dans le dossier du programme) garde les données ; `/SUPPRIMERDONNEES=oui` les envoie à la Corbeille.

## 6. La recette à faire chez toi

Tout cela n'a été essayé ici que sous Linux (wine) ; voir `RAPPORT-ANALYSE.md`, § 10. Sur un PC Windows de test :

1. Sur un PC où la version du lot 8 est installée, avec un projet : installer « pour moi seul », « Reprendre » coché. Le projet est dans `Documents\XPGAnalyser\projets`, la version du lot 8 a quitté les Applications installées, et `Journaux\migration_<date>.txt` le raconte.
2. Lancer ; accueil › Dossiers : chaque dossier s'ouvre.
3. Réinstaller par-dessus (mise à jour) : la sauvegarde `avant-1.11.2` apparaît dans `restore_backup.bat /liste`.
4. Supprimer `SDL3.dll` du dossier du programme, puis Maintenance › Réparer › Réparation automatique : l'application redémarre.
5. Désinstaller en gardant les données : `Documents\XPGAnalyser` est toujours là.
