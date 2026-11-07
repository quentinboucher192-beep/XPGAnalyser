# API Studio et IHM Studio 1.32.0 : la sécurité partout, et la liste des limites

Faite à partir de tes demandes du 07/11 : « continue avec la 1.32 » et « rend tout plus sécurisé en plus stp, en plus de la suite (liste des limites) ».

Elle se met à jour par-dessus la 1.31.0 et garde tes données.

## 0. En bref

- **Quatre relectures de sécurité**, une par partie exposée du logiciel : l'agent de déploiement, les licences et l'outil de l'éditeur, l'Assistant IA, les fichiers venus d'ailleurs et les scripts de l'IHM. **Tout ce qui était critique ou élevé est corrigé**, avec un essai pour chaque correction (§ 3).
- **Ce qui te demande un geste** (§ 2) :
  - un script qui écrit **hors du projet** (`C:\Donnees\…`, un partage réseau) : autorise ce dossier sur le PC ;
  - **l'accès web** de l'IHM : coche la case « permis sur ce poste » sur le PC qui le sert ;
  - **ta clé de l'éditeur** : fais-en une sauvegarde avec une phrase de passe (`sauvegarder-cle`).
- **Les limites listées dans les livraisons précédentes**, corrigées (§ 4) :
  - la sauvegarde de ta clé par une phrase de passe ;
  - un dépôt GitHub remis à un état plus ancien est refusé ;
  - « Revenir à la précédente » fait l'aller-retour entre les deux dernières versions ;
  - l'historique de l'agent est borné ;
  - sous Windows, ce que le poste écrit est gardé.
- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux, et sous Wine pour les programmes Windows.

## 1. Comment la sécurité a été revue

Quatre relectures indépendantes du code, en lecture seule, chacune sur une partie du logiciel :
- l'**agent de déploiement**, ouvert au réseau ;
- les **licences** et l'outil de l'éditeur ;
- l'**Assistant IA**, les secrets et le bac à sable ;
- les **fichiers venus d'ailleurs** (projets, archives, images, polices, Excel) et les **scripts de l'IHM**.

Chacune a classé ce qu'elle a trouvé (critique, élevé, moyen, faible) avec un scénario concret. Tout ce qui est critique ou élevé est corrigé ; le reste l'est presque entièrement (§ 3, et § 7 pour ce qui reste). Chaque correction a son essai.

Une **cinquième relecture**, faite après les corrections, a cherché ce qu'elles pourraient casser dans un usage normal, ou laisser ouvert. Ce qu'elle a trouvé est corrigé :
- **un projet rangé sur un partage réseau** (`\\serveur\partage\Projet`) aurait perdu ses enregistrements, ses fichiers externes et l'écriture de ses scripts. Un chemin sous le dossier du projet lui-même est maintenant permis : c'est le même serveur, rien de nouveau n'est contacté ;
- **un chemin réseau autorisé** échappait aux gardes des fichiers du projet (`ihm/`, `project.xpgproj`) et du dossier permis lui-même. Il passe maintenant par les mêmes gardes qu'un chemin local ;
- **sous Windows, une jonction** sous un dossier permis laissait sortir un script. Chaque élément du chemin sous le dossier permis est maintenant vérifié ;
- **l'ancien dossier de l'agent** :
  - l'installateur lui garde les droits de l'opérateur, pour que l'agent puisse le déplacer ;
  - après une copie, sa clé n'y reste pas ;
  - le compte système (une installation par SCCM ou Intune) ne le prend jamais à l'opérateur ;
- **un PC dont l'identifiant de Windows ne se lit pas** ne perd plus sa licence valide ;
- **dans le bac à sable de l'IA**, copier un fichier vers un dossier marche de nouveau.

## 2. Ce qui change pour toi

| Quoi | Avant | Maintenant | Ce que tu fais |
|---|---|---|---|
| **Fichiers des scripts** (`FILE_*`, `DIR_*`, `RESOURCE_EXTRACT`, `IHM_ENREG_EXPORTER/IMPORTER`) | un chemin complet allait où il disait | seulement dans `donnees/`, `exports/` et les **dossiers autorisés sur ce PC** ; ailleurs, la native rend FAUX et `FILE_ERROR()` dit quel dossier autoriser | sur chaque PC concerné (le tien et les postes d'exploitation) : **Configuration › Poste d'exploitation**, partie *Ce PC*, **Dossiers autorisés aux scripts** (séparés par `;`). Sur un poste d'exploitation sans l'éditeur, dans le compte de l'opérateur : `IHM-Studio.exe --autoriser-dossier-scripts "C:\Donnees"`. Ce réglage reste sur le PC : il ne part jamais avec le projet |
| **Chemins réseau** venus d'un projet (`\\serveur\partage`) : fichiers externes, documents, explorateur, enregistrements | ouverts | refusés sans rien ouvrir, sauf dans un dossier autorisé | autorise le partage s'il en faut un |
| **Accès web** de l'IHM | « Actif » dans le projet suffisait | il faut aussi cocher **Accès web de l'IHM permis sur ce poste** (Configuration › Accès web, partie *Ce poste*) | coche-le sur le PC qui sert les navigateurs ; sur un poste sans l'éditeur : `IHM-Studio.exe --acces-web-sur-ce-poste oui` |
| Accès web par un **nom à points** (`poste.usine.local`) ou derrière un **proxy** | marchait | la page s'affiche, mais la connexion et les clics sont refusés | appelle le poste par son adresse IP ou son nom court |
| Accès web **réseau sans connexion** | les clics passaient | lecture seule, et c'est dit | demande la connexion pour commander |
| **Polices** `.otf` (CFF) et collections `.ttc` | lues | dessinées avec la police par défaut ; « Générer » le signale | convertis-les en `.ttf` |
| **Assistant IA : ta clé d'API** | partait vers l'adresse réglée | ne part que vers les serveurs où elle a été testée | après un changement d'adresse, clique **Tester** (le premier envoi le rappelle) |
| **Assistant IA : mode Direct** | tout s'appliquait d'office | une proposition sensible (fichiers, scripts de démarrage ou cycliques, C/C++, équipements, adresses) ou issue d'un échange avec une pièce jointe passe toujours par l'aperçu | rien |
| **Assistant IA : fichier `.xia` importé** | pouvait changer le fournisseur et l'adresse | ne les change jamais (l'aperçu dit qu'ils sont ignorés) | rien |
| **Agent de déploiement** | données dans `C:\ProgramData\XPG\Agent`, lisibles par tous les comptes | dans `%LOCALAPPDATA%\XPG\Agent` du compte qui le lance, réservées à ce compte ; l'ancien dossier est repris seul au premier lancement | rien. Si d'autres personnes ouvrent une session sur ce PC de maintenance, lis § 7 |
| **Licences signées par l'outil 1.32** | — | elles portent l'empreinte du poste et ne se lisent qu'avec des Studios 1.32 | mets les PC des clients en 1.32 avant de leur envoyer une licence signée par le nouvel outil |
| **Ta clé de l'éditeur** | liée à ton compte Windows : une panne du PC et elle était perdue | une sauvegarde chiffrée par une phrase de passe, relisible sur n'importe quel PC | **fais-la maintenant** (ci-dessous) |

### La sauvegarde de ta clé (outil de l'éditeur, pour toi seul)

```
XPG-License-Admin.exe sauvegarder-cle --sortie D:\coffre\cle-editeur.xpgcle
XPG-License-Admin.exe verifier-sauvegarde D:\coffre\cle-editeur.xpgcle
```

- Tu choisis la phrase de passe et tu la tapes deux fois ; elle ne s'affiche pas et n'est écrite nulle part. Il faut 12 caractères au moins : une phrase de plusieurs mots est le plus simple. **Je ne la connais pas et je ne peux pas la retrouver** : note-la hors du PC (coffre, gestionnaire de mots de passe).
- La phrase est étirée par Argon2id (3 passes, 256 Mio), et la clé est chiffrée par XChaCha20-Poly1305 : sans la phrase, le fichier ne sert à rien.
- Garde le `.xpgcle` hors du PC : clé USB au coffre, ou ton stockage en ligne.
- Sur un PC neuf : `XPG-License-Admin.exe restaurer-cle --entree D:\coffre\cle-editeur.xpgcle`. Une clé déjà présente n'est jamais remplacée.
- Le pas à pas est dans `LISEZ-MOI-EDITEUR.txt`, étape 2.

## 3. Les failles corrigées

Pour chaque relecture : ce qui pouvait arriver, puis ce qui le bloque maintenant.

### Fichiers venus d'ailleurs et scripts de l'IHM

- **Critique : un projet reçu pouvait écrire ou effacer n'importe où sur le PC**, par ses scripts (`FILE_WRITE`, `FILE_DELETE`, `DIR_*`…), dès l'ouverture de la simulation ou du poste.
  - Maintenant, les scripts sont confinés à `donnees/`, `exports/` et aux dossiers autorisés sur ce PC.
  - La comparaison se fait après avoir résolu les liens. Un dossier autorisé ne se supprime ni ne se déplace lui-même.
  - Les noms réservés de Windows (`CON`, `NUL`, `COM1`…), les noms finissant par un point ou une espace, et le « : » sont refusés partout.
- **Élevé : une icône `.ico` forgée pouvait écrire hors de sa mémoire.** Chaque dimension, palette et écriture est maintenant bornée.
- **Élevé : un chemin réseau mis dans un projet faisait se connecter le PC à un serveur choisi** (et lui envoyer l'empreinte du mot de passe Windows). Un seul contrôle (`hmi::projectPathAllowed`) les refuse maintenant sans rien ouvrir.
- **Élevé : l'accès web** pouvait être ouvert par un projet reçu, et un site visité par l'opérateur pouvait cliquer dans la vue (*DNS rebinding*, requête d'une autre origine).
  - Il faut maintenant le réglage du poste.
  - L'adresse appelée et l'origine sont vérifiées.
  - Un jeton par session est exigé pour cliquer, et un POST ne crée plus de session.
- **Moyen : des archives piégées pouvaient remplir la mémoire ou le disque** (projets, vues, Excel, bombe zip, taille mentie d'un xlsx). Une même limite sert maintenant à tous les lecteurs : 256 Mio par entrée, 1 Gio au total, 20 000 entrées.
- **Moyen : une police forgée était dessinée directement.** Les OTF et TTC sont refusées ; une TTF est relue, puis dessinée depuis une copie réécrite.
- **Moyen : un XML très imbriqué pouvait faire déborder la pile.** La lecture est maintenant une boucle.
- **Moyen : une archive pouvait déposer des fichiers « à côté » du projet.** Seul `_a-cote/<nom>.xpglayout` est accepté.
- **Moyen : « Ouvrir » un fichier passait par le shell** et pouvait lancer un programme. `core::openWithSystem` n'utilise pas le shell et refuse les exécutables.
- **Moyen : les quatre programmes cherchaient leurs DLL dans le dossier courant.** Ils ne les cherchent plus que dans leur propre dossier et dans System32.

### Agent de déploiement (ouvert au réseau)

- **Critique : la clé d'identité de l'agent et ses appairages étaient lisibles par tous les comptes du PC** (dossier commun, protection « machine »).
  - Ils sont maintenant dans le profil du compte qui lance l'agent, protégés pour ce compte seul (DPAPI), avec un accès réservé au dossier.
  - L'ancien dossier est repris seulement s'il n'est pas un lien et s'il t'appartient, ou aux administrateurs ou à SYSTEM. Sinon l'agent repart à neuf et le dit.
- **Élevé : un dossier d'extraction laissé ou un lien pouvait faire écrire l'installation ailleurs.** Ce dossier est supprimé sans suivre les liens, puis recréé ; un lien sur le chemin est refusé.
- **Élevé : un PC du réseau pouvait remplir le disque par le journal**, ou y glisser de fausses lignes.
  - Chaque champ venu du réseau est coupé à 256 octets, et les caractères de contrôle sont remplacés.
  - Le journal est limité à 10 Mio par jour et à 30 fichiers.
- **Moyen :**
  - fichiers temporaires au nom prévisible : nom aléatoire, création exclusive ;
  - `arret.json` lu à un chemin venu de `poste.json` : il est toujours dans le dossier de l'agent ;
  - un seul PC pouvait occuper toutes les connexions : 2 par adresse, 2 places réservées aux PC appairés, 15 s pour un PC non appairé ;
  - une taille annoncée démesurée : refusée, un transfert par PC.
- **Faible :**
  - les réponses aux PC non appairés ne sont plus rejouées ;
  - une demande d'appairage par seconde et par adresse ;
  - les identifiants d'opération ont 128 bits.

### Licences et outil de l'éditeur

- **Élevé : les fonctionnalités n'étaient fermées que dans les menus.** La simulation de l'IHM, l'import de vues et les commandes de script vérifient maintenant la licence eux-mêmes.
- **Élevé : une licence recopiée avec la clé d'un PC marchait sur un autre.**
  - La demande et la licence portent maintenant l'empreinte salée de l'installation de Windows (jamais le matériel en clair).
  - Une licence sur le mauvais PC est « Autre PC ».
- **Moyen :**
  - une variable d'environnement pouvait désigner un autre dossier de licences : seulement dans la version d'essai ;
  - effacer l'état local ne remet plus le compteur à zéro : il est recréé sur la date la plus récente ;
  - une liste de révocation livrée avec le programme est prise en compte ;
  - une ancienne émission d'une licence remise sur le disque : l'éditeur peut révoquer une émission précise (`issued_before`), et la plus récente est retenue ;
  - un nom de PC piégé pour Excel (`=`, `+`, `-`, `@`) : refusé dans la demande, neutralisé dans l'export CSV.
- **Faible : le jeton GitHub ne part que vers `https://api.github.com`.**
- **Le paquet livré est contrôlé.** `licences\` n'accepte que le registre, la liste de révocation et les textes de licence. Aucune licence, demande, sauvegarde de clé ni fichier de la synchro ne peut partir avec l'installateur.
- **L'installateur :**
  - les dossiers communs sous ProgramData ne sont jamais un lien, appartiennent aux administrateurs et ne reçoivent plus de droits hérités ;
  - dans `Licences`, les utilisateurs créent et modifient des fichiers, mais ne suppriment ni ne remplacent le dossier ;
  - dans le dossier commun de l'IA, seuls les administrateurs écrivent ;
  - les règles du pare-feu ne s'ouvrent qu'au **sous-réseau local**.

### Assistant IA, secrets et bac à sable

- **Élevé : une conversation modifiée pouvait faire lire, envoyer ou supprimer un fichier hors des pièces jointes.** Les identifiants et les chemins sont vérifiés à la relecture, chaque chemin résolu reste sous `pieces-jointes/`, et un lien est retiré sans être suivi.
- **Élevé : un `.xia` ou un `installation.json` déposé par un autre compte pouvait envoyer tes demandes, et ta clé, à un autre serveur.**
  - Un `.xia` n'apporte plus ni fournisseur, ni adresse, ni chemin.
  - `installation.json` n'est cru que s'il appartient aux administrateurs, à SYSTEM ou à ton compte.
  - Ta clé est liée aux serveurs où elle a été testée.
- **Élevé : le mode Direct appliquait d'office des propositions sensibles.** Elles demandent maintenant toujours ton accord.
- **Moyen :**
  - le programme lancé par le bac à sable héritait des handles du Studio (sockets, fichiers) : il n'hérite plus que de ses trois tuyaux ;
  - la copie du projet qu'il reçoit n'a plus les mots de passe, sels, secrets TOTP, badges, identifiants SMTP ni l'adresse SMS ;
  - une adresse « locale » trompeuse (`127.exemple.com`, `127.1`, `0127.0.0.1`) n'est plus locale ;
  - les redirections HTTP ne sont plus suivies : ta clé ne peut pas être renvoyée ailleurs.

## 4. Les limites de la liste

Ce que les livraisons précédentes listaient comme limites, et qui est corrigé :

| Limite | Livrée en | Maintenant |
|---|---|---|
| La sauvegarde de ta clé de l'éditeur est liée à ton compte Windows (DPAPI) | 1.30.0, 1.31.0 | `sauvegarder-cle`, `verifier-sauvegarde`, `restaurer-cle` : une sauvegarde chiffrée par ta phrase de passe, relisible partout (§ 2) |
| Un dépôt GitHub remis à un état plus ancien (par quelqu'un qui a le jeton) aurait été accepté | 1.31.0 (relecture) | l'index porte un compteur ; chaque PC Admin retient le plus haut vu (`github-vu.json`) et refuse un dépôt remis en arrière, en le disant |
| « Revenir à la précédente » : deux retours de suite ne faisaient pas un aller-retour | 1.29.0 | il revient à la version active juste avant : deux retours font l'aller-retour |
| L'historique de l'agent grandit sans limite | 1.29.0 | 1 Mio, puis il passe à un fichier ancien (2 × 1 Mio gardés) |
| Sous Windows, ce que le poste écrit sur sa sortie n'est pas gardé | 1.29.0 | gardé, comme sous Linux (seul ce fichier est hérité par le poste) |
| Le lanceur du poste pointe vers l'ancien dossier quand le programme a bougé | 1.28.0 | l'entrée du démarrage avec la session est refaite si son dossier n'existe plus |

Restent de ces listes : la console de l'agent qui peut apparaître un instant à l'ouverture de session, la recherche UDP qui ne traverse pas les routeurs, un seul poste par PC, `package.ps1` non exécuté ici (pas de PowerShell), et aucun essai sur un vrai Windows (§ 7).

## 5. Les essais

- **Linux : 146 suites sur 146 réussies** (`ctest`, sur la version livrée), dont une nouvelle, `clesauvegarde` (la sauvegarde de la clé de l'éditeur). Chaque correction de sécurité a son essai dans la suite de sa partie :
  - **fichiers et scripts** (`hminatives`, `hmi`, `hmieditor`, `hmirecording`, `echanges`, `robustness`) :
    - le confinement, les liens, les noms réservés, un projet sur un partage ;
    - des icônes, des polices, des archives et des classeurs forgés ;
    - un XML d'un million de commentaires ;
    - l'accès web (adresse appelée, origine, jeton) ;
  - **agent de déploiement** (`deploynet`, `deployinstall`, `deployui`, `deploycrypto`, `deploypackage`) :
    - un journal attaqué (caractères de contrôle, 5 000 protocoles, plafond du jour) ;
    - les connexions par adresse et les places réservées ;
    - une taille démesurée, un dossier d'extraction piégé, des liens ;
    - l'aller-retour « Revenir à la précédente », l'historique borné ;
  - **licences** (`licensing`, `licenceui`, `githubsync`, `clesauvegarde`) :
    - les verrous à la source, l'empreinte du poste, une émission plus ancienne, l'état local effacé ;
    - un CSV piégé, l'anti-retour du dépôt ;
    - la sauvegarde de la clé : phrase fausse, en-tête ou paramètres modifiés ;
  - **Assistant IA** (31 suites, et `coreprocess`) :
    - des pièces jointes aux chemins hostiles ;
    - un `.xia` qui voudrait changer l'adresse ;
    - la clé liée au serveur, les adresses locales trompeuses ;
    - le mode Direct, les secrets retirés du bac à sable ;
    - aucun descripteur hérité par un processus enfant.
- **Sous Wine, avec les programmes Windows livrés :**
  - **l'installation par-dessus la 1.31.0** :
    - elle passe, et ta clé d'API est gardée ;
    - les programmes installés sont ceux construits : versions 1.32.0, agent 1.2.0 ;
    - `--cli`, le bac à sable, les associations de fichiers et les copies de secours marchent ;
    - rien de l'outil de l'éditeur n'est installé ;
    - le journal montre les droits posés sur les dossiers communs, dans l'ordre : l'ancien dossier de l'agent d'abord ;
  - **l'agent 1.2.0 reprend le dossier de l'agent 1.1.0** (créé pour l'essai sous `C:\ProgramData\XPG\Agent`) :
    - il est déplacé dans le profil, et l'ancien n'existe plus ;
    - **l'empreinte de son identité est la même avant et après** : les PC appairés n'ont rien à refaire ;
    - journal joint : `essai-agent-et-poste-wine.txt` ;
  - **les réglages du poste sans fenêtre** (`--autoriser-dossier-scripts`, `--acces-web-sur-ce-poste`…) :
    - un chemin relatif est refusé ;
    - le même dossier écrit autrement (casse, barre finale) n'est pas ajouté deux fois ;
  - **l'outil de l'éditeur** (WinHTTP et DPAPI de Wine, contre le faux GitHub local) :
    - 17 commandes réussies et les 5 refus attendus ;
    - la sauvegarde de la clé : écrite, vérifiée, une phrase fausse refusée, restaurée sur un « autre PC » avec le même identifiant, et jamais par-dessus une clé déjà là ;
    - une adresse d'API autre que GitHub est refusée ;
    - journal joint : `essai-outil-wine.txt`, codes de secours masqués.
- **Une cinquième relecture**, faite après les corrections (§ 1), a trouvé six problèmes. Ils sont corrigés et couverts par les essais.

## 6. Installer

Comme d'habitude : lance `API-IHM-Studio-Setup-1.32.0.exe` (dans le zip) par-dessus la 1.31.0. Tes projets, tes réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.32.0.exe` | `603D9517DE2281D390B0691A1A82BA6020502653E11E7277E8402FB87475FBD8` | 54 855 623 octets |
| `API-Studio-1.32.0-portable.zip` | `66C67AF032A28A90FAB0D53B4972524826C30ED51322042720E45DD900F04B74` | 190 fichiers |
| `IHM-Studio-1.32.0-portable.zip` | `FECF3E7AE39ED5BC81CA810EF641BB5CC1D15F323378851FE5B3E7E40346151D` | 191 fichiers |
| `XPG-License-Admin-1.32.0-EDITEUR.zip` (**à part, pour toi seul**) | `4002D22C79411A663F1C517D4B7C400588A240F4B81C77444687D98677EF8CDB` | 1 328 609 octets |

**L'outil de l'éditeur** remplace celui de la 1.31.0 : mets le nouveau `XPG-License-Admin.exe` à la place de l'ancien. Ta clé, ton archive et ta synchro ne bougent pas (`%LOCALAPPDATA%\XPG\LicenseAdmin`). Ne le mets jamais avec l'installateur. **Première chose à faire avec lui : `sauvegarder-cle`** (§ 2).

Après l'installation, sur les PC concernés :
- un script écrit hors du projet : autorise son dossier (§ 2) ;
- l'accès web : coche « permis sur ce poste » ;
- l'agent de déploiement : il reprend seul son ancien dossier à son prochain démarrage, rien à faire.

## 7. Ce qui reste

- **Rien n'a été essayé sur un vrai Windows.** Plusieurs corrections n'ont tourné que sous Wine, ou n'ont été que compilées :
  - les droits du dossier de l'agent ;
  - le déplacement de son ancien dossier ;
  - la liste des handles transmis au bac à sable ;
  - le refus des redirections par WinHTTP ;
  - le propriétaire de `installation.json`.

  Les essais sur un vrai Windows restent à faire par toi.
- **L'agent de déploiement est maintenant lié au compte Windows qui le lance.**
  - Son ancienne clé d'identité était lisible par les autres comptes du PC.
  - Si d'autres personnes ouvrent une session sur ce PC de maintenance, elles ont pu la copier. Dans ce cas : arrête l'agent, efface `identite.cer` et `identite.cle` dans `%LOCALAPPDATA%\XPG\Agent`, relance-le (il crée une nouvelle identité), puis refais l'appairage depuis ton Studio.
  - Une coupure réseau peut laisser des connexions à moitié ouvertes. Elles comptent dans la limite de 2 par adresse pendant 2 minutes au plus.
- **Licences :**
  - effacer à la fois l'état local et la liste de révocation fait oublier une révocation, sauf si une liste est livrée avec le programme ;
  - le dossier commun est trouvé par la variable `%ProgramData%`, qu'un utilisateur peut changer pour sa session ;
  - l'empreinte du poste se contourne en clonant l'identifiant de Windows : c'est un signal d'alarme, pas une protection forte ;
  - les licences signées par l'outil 1.32 ne se lisent pas avec des Studios 1.30 ou 1.31.
- **D'autres connexions réseau restent héritables** par un programme lancé : la recherche de l'agent, l'espion réseau et l'instance unique. Le bac à sable, lui, est protégé.
- **Non couverts :**
  - les macros d'une bibliothèque `.xapi` peuvent encore écrire un chemin relatif dans leur dossier `Macros` ;
  - la police d'interface choisie par l'utilisateur n'est pas relue comme les polices des projets.
- **L'outil de l'éditeur reste en ligne de commande.** Son application avec des fenêtres (ta demande de ce matin) est la **1.33**, commencée dès cette livraison : la clé, signer une licence en glissant la demande, la liste des activations, les révocations, et un assistant pour GitHub.
- **Reportés :** les licences du poste d'exploitation (Runtime, la maquette) et les limites de la 1.27.0 (`Stop(son)`…).
