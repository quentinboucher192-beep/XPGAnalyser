# Déploiement réseau — conception (1.28.0 et suivantes)

Ce document décrit comment un projet IHM part du PC de développement vers un PC de maintenance. Il fixe le format du paquet, le protocole, l'appairage et le rôle de chaque programme. Il reprend la maquette (onglet Architecture) et les décisions prises avec toi le 06/11 :
- **TLS : mbedTLS**, embarqué, le même code sous Windows et sous Linux ;
- **déployer une application = envoyer le poste d'exploitation** d'un projet sur l'autre PC : le projet (FINISH) et son lanceur (`lancer-poste.cmd`, déjà codé : `src/app/hmi/HmiStationLaunch.hpp`), que le PC cible lance avec son propre programme.

Retour au [README](../README.md).

## 1. Les étapes

| Version | Contenu |
|---|---|
| **1.28.0** | mbedTLS et libsodium intégrés ; le paquet `.xpgdeploy` (manifeste SHA-256, signature Ed25519, vérification) et IHM Studio › Créer un package de déploiement ; l'agent `XPG-Deployment-Agent` en console (TLS 1.3 des deux côtés, IDENTIFY, STATUS, GET_LOGS, journal, `--etat`) ; la découverte UDP, l'ajout par IP, l'appairage par code à 6 chiffres ; la fenêtre Déploiement réseau d'IHM Studio (machines, appairage, état, journal). |
| 1.29.0 | L'envoi par blocs avec reprise, VERIFY_PACKAGE, l'installation (versions côte à côte, `active.json`, retour en arrière), le lanceur écrit sur le PC cible, le lancement et l'arrêt du poste ; l'agent démarré avec la session de l'opérateur (voir § 8), les règles de pare-feu à l'installation ; dans le Studio : Déployer, la progression, l'historique, le retour en arrière. |
| 1.30.0 | Les licences, sans service en ligne : l'Admin qui signe, le License Manager, les éditions et fonctionnalités, l'activation par fichier ; les licences Runtime vérifiées par l'agent. |
| ensuite | L'activation en ligne et les licences flottantes (il faut héberger le service de licences) ; l'onglet Déploiement réseau rejoint le License Manager Client. |

## 2. Les programmes et les modules

| Où | Quoi |
|---|---|
| `third_party/mbedtls`, `third_party/libsodium` | les sources réduites des deux bibliothèques (`ORIGINE.txt` : version, adresse, SHA-256 de l'archive) ; cibles CMake `xpg_mbedtls`, `xpg_sodium` |
| `src/deploy/` (bibliothèque `xpg_deploy`, sans interface) | `Crypto` (SHA-256, Ed25519, hasard, DPAPI) ; `Package` (le paquet) ; `DevKey` (la clé d'éditeur du développeur) ; `Tls` et `TlsIdentity` (le TLS et le certificat de la machine) ; `Frame`, `Protocol` (les trames) ; `Pairing` (l'appairage) ; `Discovery` (UDP) ; `Agent` (le cœur de l'agent) ; `Client` (le côté Studio) ; `Journal` |
| `src/agent/AgentMain.cpp` | l'exécutable `XPG-Deployment-Agent(.exe)` (cible `xpg_deploy_agent`) : sans SDL, sans éditeur |
| `src/app/` | IHM Studio : Créer un package de déploiement, la fenêtre Déploiement réseau |

`deploy/` ne dépend que de `core/`, de `xls/` (miniz, pour le zip) et des deux bibliothèques. Les dossiers où il écrit lui sont donnés en paramètre.

## 3. Le paquet `.xpgdeploy`

Un zip (miniz) :

```
manifest.json      le manifeste (UTF-8, signé tel quel, octet pour octet)
manifest.sig       la signature Ed25519 du manifeste (128 caractères hexadécimaux)
app/...            le poste : les fichiers du projet utiles au poste, et le lanceur
```

`manifest.json` :

```json
{
  "format": "xpgdeploy", "version": 1,
  "application": "Armoire_Gaz", "appVersion": "V3",
  "createdAt": "2026-11-06T13:40:00Z", "createdOn": "DEV-LUCAS",
  "runtime": { "program": "IHM-Studio.exe", "minVersion": "1.28.0", "mode": "--ihm" },
  "launcher": "app/lancer-poste.cmd",
  "publisher": { "name": "DEV-LUCAS", "key": "<clé publique Ed25519, hexadécimal>" },
  "files": [ { "path": "app/ihm/ihm.txt", "size": 12034, "sha256": "…" } ],
  "totalSize": 18432011
}
```

**Vérifier un paquet** (dans l'ordre, au premier écart : refusé, avec la raison) :
1. le zip se lit ; `manifest.json` et `manifest.sig` y sont ; tailles et nombre de fichiers dans les limites (20 000 fichiers, 2 Gio par fichier, 4 Gio au total) ;
2. la signature est bonne pour la clé `publisher.key` ; cette clé est **approuvée** (l'agent : celle reçue à l'appairage du PC développeur ; à l'ouverture dans le Studio : « signature valide, éditeur … » sans approbation) ;
3. chaque chemin est sûr : relatif, sous `app/`, ni `..`, ni `/` ou `\` au début, ni lettre de lecteur, ni `:`, ni nom réservé Windows (`CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9`, `LPT1`–`LPT9`) dans aucun segment, ni point ou espace en fin de segment, 240 caractères au plus ; pas de lien symbolique ;
4. les fichiers du zip et ceux du manifeste sont **exactement** les mêmes (aucun en plus, aucun en moins) ; chaque taille et chaque SHA-256 concorde.

**Ce qui part** : seul un projet **FINISH** se met en paquet (comme le poste). Les fichiers du dossier du projet que le poste lit ; pas les versions, ni les exports, ni la corbeille, ni les fichiers temporaires. Le lanceur est écrit pour l'emplacement d'installation standard du programme ; à partir de la 1.29.0, l'agent le réécrit pour le programme du PC cible.

**La clé d'éditeur** (`DevKey`) : une paire Ed25519 créée sur le PC du développeur à la première utilisation. La clé privée est gardée chiffrée par DPAPI (le compte Windows) ; sous Linux, dans un fichier lisible par son seul propriétaire. Elle ne quitte jamais le PC. La clé publique part avec chaque paquet et à chaque appairage.

## 4. Le réseau

**Ports** : TCP **47861** (l'agent), UDP **47862** (la découverte), réglables. L'agent vérifie au démarrage qu'ils sont libres ; sinon il le dit et n'écoute pas.

**TLS 1.3 seulement**, certificats des deux côtés. Chaque machine a son certificat (ECDSA P-256, auto-signé, créé au premier lancement ; la clé privée sous DPAPI : la machine pour l'agent, l'utilisateur pour le Studio). Le certificat n'est pas jugé par une autorité : **son empreinte SHA-256 est épinglée** à l'appairage. Un pair inconnu ne peut faire que IDENTIFY et l'appairage.

**Les trames** : une longueur sur 4 octets (gros-boutiste ; elle compte tout ce qui suit : 4 + en-tête + données ; 2 Mio au plus en 1.28), la longueur de l'en-tête sur 4 octets, l'en-tête JSON, puis les données. L'en-tête d'une demande : `{"proto":1,"op":"STATUS","id":"<identifiant d'opération>","seq":7,"ts":<ms depuis 1970>}` ; d'une réponse : `{"proto":1,"op":"STATUS","re":"<id>","seq":7,"ok":true}` ou `"ok":false,"error":"<code>","message":"<phrase>"`.

**Contre le rejeu** : `seq` croît strictement sur une connexion ; `ts` à moins de 5 minutes de l'horloge de l'agent ; un identifiant d'opération déjà vu (pour ce même pair) rend la même réponse sans rien refaire (`"replay":true`) ; un identifiant réemployé pour une autre opération est refusé (`rejeu`). Le cache garde les 512 dernières réponses, en mémoire.

**Les commandes en 1.28** : `IDENTIFY` (les versions de protocole acceptées, le nom de la machine, son identifiant, la version de l'agent ; une version commune est choisie, sinon « incompatible » — jamais forcé), `PAIR_START` / `PAIR_FINISH`, `STATUS`, `GET_LOGS`, `UNPAIR`. Toute autre commande : refusée (`op-inconnue`). `IDENTIFY` vient d'abord, sinon `identifier-d-abord`. Les codes d'erreur de l'agent : `incompatible`, `op-inconnue`, `non-appaire`, `identifier-d-abord`, `en-tete`, `proto`, `rejeu`, `horloge`, `trame-trop-grosse`, `arguments`, `pas-de-code`, `code-faux`, `appairage`, `interne` ; ceux du Studio : `reseau`, `empreinte`, `reponse`. L'agent accepte 8 connexions à la fois ; la neuvième est fermée avant la poignée de main.

**La découverte** (UDP) : le Studio envoie `{"xpgd":1,"op":"DISCOVER","nonce":"…"}` en diffusion (et en direct aux adresses qu'on lui donne) ; chaque agent répond `{"xpgd":1,"op":"HERE","nonce":"…","machine":"PC-ATELIER-1","id":"M-7F21","version":"1.0.0","protos":[1],"port":47861,"fp":"<empreinte>"}`. Une réponse ne prouve rien : c'est une adresse, rien de plus, tant que la machine n'est pas appairée.

## 5. L'appairage

Sur le PC de maintenance, `XPG-Deployment-Agent --appairer` affiche un **code à 6 chiffres**, valable 5 minutes, utilisable une fois, 3 essais au plus. Le développeur le tape dans le Studio.

Six chiffres se devinent hors ligne si on les envoie, même hachés. Le code n'est donc jamais transmis : les deux machines font un **échange de clés authentifié par mot de passe (CPace sur ristretto255, libsodium)**, lié aux deux certificats de la connexion TLS :

1. `G = ristretto255_from_hash(SHA-512("XPG-PAIR-v1" ‖ code ‖ sid ‖ empreinte Studio ‖ empreinte agent))`, `sid` : 16 octets tirés par le Studio ;
2. le Studio tire `ys`, envoie `Ys = ys·G` (`PAIR_START`, avec son nom et sa clé d'éditeur) ; l'agent tire `ya`, renvoie `Ya = ya·G` et sa confirmation ;
3. chacun calcule `K = y·Y(autre)`, puis la clé de session `ISK = SHA-512("XPG-PAIR-ISK" ‖ K ‖ Ys ‖ Ya ‖ sid)` ; chacun prouve la connaître (HMAC-SHA-256 sur toute la conversation, l'agent d'abord) ;
4. `PAIR_FINISH` porte la confirmation du Studio (ou un abandon, si celle de l'agent est fausse). Réussi : l'agent garde l'empreinte du Studio, son nom et sa clé d'éditeur ; le Studio garde l'empreinte de l'agent, son nom, son identifiant et son adresse. Le code est consommé. Un essai est compté à `PAIR_START`, s'il y a un code en cours.

Un intermédiaire qui présente ses propres certificats obtient un `G` différent de chaque côté : l'échange échoue, et il n'apprend rien qui lui permette de deviner le code hors ligne. Il n'a droit qu'à un essai par connexion, et le code s'efface après 3 échecs.

## 6. L'agent

```
XPG-Deployment-Agent --servir [--dossier D] [--port 47861] [--port-decouverte 47862]
XPG-Deployment-Agent --etat [--dossier D]        ce que fait l'agent (sans réseau)
XPG-Deployment-Agent --appairer [--dossier D]    un code à 6 chiffres
XPG-Deployment-Agent --demarrer-avec-session oui|non   (1.29.0) l'agent démarre avec la session de l'opérateur
XPG-Deployment-Agent --version                    « XPG-Deployment-Agent 1.2.0 (protocoles 1 et 2, …) » en 1.32.0 (1.1.0 de la 1.29.0 à la 1.31.0)
```

`--demarrer-avec-session oui` écrit la valeur `Run` « XPG Deployment Agent » de **ton** compte (HKCU ; Linux : un `.desktop` dans `~/.config/autostart`) - sauf si l'installateur l'a déjà posée pour tous les comptes (HKLM, lue dans les vues 64 et 32 bits) : alors il retire la sienne et le dit (deux agents au démarrage : le second trouverait le port pris). Lancé ainsi, sans terminal, l'agent cache sa console. `--etat` dit s'il démarre avec la session, et où.

Son dossier (par défaut **`%LOCALAPPDATA%\XPG\Agent`** sous Windows depuis la 1.32.0 ; avant : `C:\ProgramData\XPG\Agent`, voir § 6.1) : `identite.cer` et `identite.cle` (la clé protégée pour **ce compte**, 1.32.0 ; avant : pour la machine), `appaires.json` (les PC appairés), `appairage.code` (le code en cours, protégé), `etat.json` (écrit toutes les 5 s pendant qu'il tourne), `journal\AAAA-MM-JJ.log` (une ligne par opération). Ctrl+C l'arrête proprement. Il ne lance **aucune** commande reçue du réseau ; il n'utilise jamais Internet.

### 6.1 La sécurité de l'agent (1.32.0)

Une relecture de sécurité a trouvé qu'un **utilisateur local sans droits** pouvait prendre la main sur l'agent : son dossier sous `C:\ProgramData` héritait des droits de ProgramData (tout compte peut y créer des fichiers), et ses secrets étaient protégés pour la **machine** (tout programme du PC peut les ouvrir). Depuis la 1.32.0 :
- **Le dossier est dans le profil de l'opérateur** (`%LOCALAPPDATA%\XPG\Agent` ; l'agent tourne dans sa session), avec, à chaque démarrage, un accès **réservé** à SYSTEM, aux Administrateurs et à ce compte (héritage du parent coupé ; sous Linux : 0700). La clé d'identité et le code d'appairage sont protégés pour **ce compte**.
- **La reprise du dossier de la 1.29/1.31**, une fois : si `C:\ProgramData\XPG\Agent` existe, n'est pas un lien et appartient à ce compte, aux Administrateurs ou au système, il est déplacé (sinon recopié, avec une note dans l'ancien) ; ce qui y appartenait à un autre compte (ou était un lien) est effacé et dit ; les secrets sont reprotégés pour ce compte. S'il appartient à quelqu'un d'autre, il n'est **pas** repris : l'agent repart d'un dossier neuf et le dit (réappairer les PC d'IHM Studio). L'entrée du poste qui démarre avec la session est refaite si elle désignait l'ancien dossier.
- **Les fichiers temporaires** ont un nom imprévisible et sont créés en exclusivité (`crypto::replaceFile`) : un fichier ou un lien posé d'avance fait échouer l'écriture au lieu d'être suivi. Un dossier `.extraction` laissé là est effacé sans suivre les liens, sinon l'installation s'arrête ; aucun lien ni jonction n'est suivi sur le chemin d'une installation, d'un nettoyage ou d'un effacement de version.
- **`arret.json`** est toujours celui du dossier de l'agent : le chemin écrit dans `poste.json` n'est plus jamais relu.
- **Le journal** ne peut plus remplir le disque depuis le réseau : IDENTIFY refuse plus de 16 versions de protocole ; chaque champ venu du réseau est coupé à 256 octets, chaque ligne à 1 024 ; 10 Mio par jour au plus (puis une ligne « journal plein »), 30 jours gardés ; tous les caractères de contrôle (C0, C1, séparateurs Unicode, contrôles bidirectionnels) deviennent des espaces ; les refus de connexion et les échecs TLS : une ligne par raison toutes les 10 s.
- **Les connexions** : 2 au plus par adresse IP ; 2 des 8 places réservées aux PC appairés ; un PC non appairé est déconnecté après 15 s de silence (120 s pour un PC appairé) et après un PAIR_FINISH raté ; PAIR_START une fois par seconde par adresse. Les réponses aux PC non appairés ne vont plus dans le cache de rejeu ; les identifiants d'opération d'IHM Studio font 128 bits (les anciens, plus courts, restent acceptés).
- **Le disque** : PREPARE refuse une taille annoncée au-delà de la limite du paquet (4 Gio), calcule la place nécessaire sans débordement, en comptant ce que les transferts des autres PC vont encore écrire ; un seul transfert en cours par PC (un nouveau PREPARE du même PC remplace le précédent).
- **Le pare-feu** (l'installateur) : les règles de l'agent n'acceptent plus que le **sous-réseau local** (`remoteip=localsubnet`), réseaux domaine et privé.
- **Les DLL** : l'agent, comme les Studios, ne charge une DLL par son nom que depuis son dossier ou System32 (`SetDefaultDllDirectories`), jamais depuis le dossier courant.

## 7. Ce qui n'est pas simulé

Les essais passent par de vraies sockets (TCP et UDP sur la boucle locale) et de vraies poignées de main TLS. Les essais sur deux PC de ton réseau restent à faire par toi : ils ne sont annoncés que lorsqu'ils ont eu lieu.

## 8. Pourquoi l'agent tourne dans la session de l'opérateur (1.29.0)

La maquette en faisait un **service Windows**. Un service tourne dans la « session 0 » de Windows, isolée : il ne peut ouvrir **aucune fenêtre** sur le bureau de l'opérateur. Il ne pourrait donc ni lancer le poste d'exploitation (une application plein écran), ni lui demander de s'arrêter proprement.

En 1.29.0, l'agent est un programme sans fenêtre **démarré avec la session de l'opérateur** (la clé `Run` de l'utilisateur, comme le poste lui-même) : il lance le poste dans cette session, et lui parle par un canal local. Le pare-feu reçoit ses deux règles à l'installation (profils Domaine et Privé). Un vrai service, s'il le faut plus tard, devra être doublé d'un petit programme dans la session, qui lance le poste pour lui.

## 9. L'envoi et l'installation (1.29.0)

- **PREPARE** : le nom, la version, la taille et le SHA-256 du paquet ; l'agent vérifie la place libre et répond où reprendre (0, ou les octets déjà reçus d'un envoi coupé).
- **SEND_CHUNK** : des blocs de 1 Mio, chacun avec son SHA-256 ; l'agent les écrit dans `incoming\`. Une coupure reprend au dernier bloc reçu.
- **VERIFY_PACKAGE** : le SHA-256 du paquet entier, puis la vérification complète du § 3 avec les clés d'éditeur des PC appairés.
- **INSTALL** : le contenu de `app\` est extrait dans `Applications\<application>\versions\<version>\` ; le lanceur est réécrit pour le programme de ce PC ; `active.json` est remplacé en une seule opération. Rien de l'ancienne version n'est écrasé ; trois versions sont gardées (réglable).
- **STOP** / **START** : l'arrêt est demandé au poste en marche (il prévient l'opérateur et enregistre son état), puis la nouvelle version est lancée ; si elle ne tient pas quelques secondes, l'agent revient à la précédente.
- **ROLLBACK** : revenir à la version précédente.
- **LIST_APPS** : les applications installées, leurs versions, la version active, si le poste tourne.
- **GET_HISTORY** : l'historique des déploiements.
- L'agent ne lance qu'**IHM-Studio.exe**, posé à côté de lui, avec `--ihm "<dossier de la version>"` : jamais une commande reçue du réseau.

### 9.1 La version 2 du protocole

L'agent 1.1.0 (IHM Studio 1.29.0) - et l'agent 1.2.0 (1.32.0, même protocole, durci) - accepte les versions **1 et 2** ; IHM Studio 1.29.0 offre 1 et 2, la plus haute commune est choisie (`IDENTIFY`). Un Studio 1.28 (version 1) garde IDENTIFY, l'appairage, STATUS (qui liste maintenant les applications), GET_LOGS et UNPAIR ; les nouvelles commandes lui sont refusées (code `proto`, « mets à jour IHM Studio pour déployer sur ce PC »). Un agent 1.28 face à un Studio 1.29 : Déployer dit « l'agent … ne sait pas encore installer : mets-le à jour ».

| Commande | Arguments | Réponse |
|---|---|---|
| `PREPARE` | `application, version, size, sha256, contentSize` | `transfer, offset, chunkSize, diskFree, installed, version` - place exigée : le reste du paquet + son contenu + 64 Mio |
| `SEND_CHUNK` | l'en-tête porte `"chunk":{transfer, offset, size, sha256}` ; les données de la trame sont les octets bruts (1 Mio au plus, pas du JSON) | `received, complete` |
| `VERIFY_PACKAGE` | `transfer` | `application, version, files, bytes, publisher` (le nom et la version doivent être ceux de PREPARE) |
| `INSTALL` | `transfer, autostart?, application?` | `version, label, previous, already, removed` |
| `STOP` | `raison?, delai?, force?` | `wasRunning, pid, forced, waitedMs` |
| `START` | `application?, version?, check?, autostart?` | `pid, version, rolledBack…` |
| `ROLLBACK` | `application?, version?, delai?, force?, start?` | `from, to, restarted, pid` |
| `LIST_APPS` | - | `apps:[…], station:{…}, program` |
| `GET_HISTORY` | `limit?` | `entries:[…]` |

Une réponse d'erreur de la version 2 peut porter des données (le détail d'un retour automatique). Les codes nouveaux : `place`, `transfert`, `bloc-abime`, `paquet-abime`, `paquet-refuse`, `installation`, `application-inconnue`, `version-absente`, `occupe`, `programme-absent`, `arret-delai`, `demarrage` ; côté Studio seulement : `annule`, `paquet-local`.

**Déployer** (`deploy::Client::deploy`) enchaîne : la vérification locale du paquet, la connexion épinglée, PREPARE, SEND_CHUNK (une coupure : reconnexion et reprise là où l'agent en est, 3 fois au plus ; un envoi annulé ou coupé reprend au déploiement suivant, même après un redémarrage de l'agent), VERIFY_PACKAGE, INSTALL, STOP (si un poste tourne), START (le poste doit tenir 8 s). L'annulation compte jusqu'au début d'INSTALL ; ensuite, Déployer va au bout.

### 9.2 Les fichiers de l'agent (1.29.0)

```
incoming\<sha256>.part | .json            la réception en cours (la reprise survit au redémarrage de l'agent)
Applications\<application>\active.json    {"format":"xpg-application-active","version":1,"application","active","previous","activatedAt","autostart"}
Applications\<application>\versions\<id>\       le contenu de app\ ; le lanceur réécrit pour ce PC (.cmd ; .sh sous Linux)
Applications\<application>\versions\<id>.json   la fiche de la version (paquet, SHA-256, date, PC d'origine, éditeur)
historique.jsonl   poste.json   arret.json   reprise-poste.txt   poste-sortie.log (Linux)
```

`<id>` est la version du paquet (« V3 ») ; le même nom avec un autre contenu devient « V3-2 », « V3-3 » : rien n'est jamais écrasé. Une version identique déjà installée n'est ni renvoyée ni réextraite. Trois versions sont gardées ; l'active, la précédente et celle du poste en marche ne sont jamais effacées.

### 9.3 Le poste et l'agent

L'agent lance le poste avec deux variables d'environnement :

- `XPG_ARRET_FICHIER=<dossier de l'agent>\arret.json` (absent au lancement). Pour l'arrêt, l'agent y écrit d'un coup `{"raison": "<phrase>", "delai": 60}`. Le poste affiche un bandeau orange (« Mise à jour du poste (raison) : il se ferme dans N s ; son état est gardé. »), écrit son état et quitte normalement à la fin du délai. L'agent attend `delai` + 30 s ; le poste encore là : échec `arret-delai`, sans le tuer - sauf `force` : il est terminé, et le journal le dit.
- `XPG_REPRISE_FICHIER=<dossier de l'agent>
eprise-poste.txt`. À l'arrêt, le poste y écrit son état (vue, variables IHM, alarmes en cours) ; au lancement suivant (la nouvelle version, ou la précédente au retour), il le reprend **sans question** - l'arrêt était voulu - puis l'efface. L'agent ne l'efface jamais entre l'arrêt d'une version et le lancement de la suivante ; seulement s'il a plus de 10 minutes au moment d'un lancement.

Un poste lancé par la **session** (la valeur `Run` « XPG Poste d'exploitation ») n'a pas ces variables : quand son dossier est une version installée par l'agent (`<agent>\Applications\<application>\versions\<version>`), il les déduit (`<agent>\arret.json`, `<agent>\reprise-poste.txt`). L'agent le retrouve au démarrage (`poste.json` : pid et heure de création ; ou la liste des processus) et peut donc l'arrêter proprement lui aussi.

### 9.4 L'installateur (1.29.0)

Profil **Poste d'exploitation**, case **Recevoir les déploiements** (décochée au départ) : la valeur `Run` « XPG Deployment Agent » (`"<programme>\XPG-Deployment-Agent.exe" --servir`, HKLM pour une installation pour tous les comptes) ; pour tous les comptes seulement, deux règles entrantes du pare-feu limitées au programme de l'agent - TCP 47861 et UDP 47862, profils **domaine et privé** (pas public) ; l'agent démarre à la fin de l'installation, dans le compte de l'opérateur. Une mise à jour l'arrête (`taskkill`) le temps de la copie. Décochée, ou à la désinstallation : la valeur et les règles posées par l'installateur partent.

