# Déploiement réseau — conception (1.28.0 et suivantes)

Ce document décrit comment un projet IHM part du PC de développement vers un PC de maintenance. Il fixe le format du paquet, le protocole, l'appairage et le rôle de chaque programme. Il reprend la maquette (onglet Architecture) et les décisions prises avec toi le 06/11 :
- **TLS : mbedTLS**, embarqué, le même code sous Windows et sous Linux ;
- **déployer une application = envoyer le poste d'exploitation** d'un projet sur l'autre PC : le projet (FINISH) et son lanceur (`lancer-poste.cmd`, déjà codé : `src/app/hmi/HmiStationLaunch.hpp`), que le PC cible lance avec son propre programme.

Retour au [README](../README.md).

## 1. Les étapes

| Version | Contenu |
|---|---|
| **1.28.0** | mbedTLS et libsodium intégrés ; le paquet `.xpgdeploy` (manifeste SHA-256, signature Ed25519, vérification) et IHM Studio › Créer un package de déploiement ; l'agent `XPG-Deployment-Agent` en console (TLS 1.3 des deux côtés, IDENTIFY, STATUS, GET_LOGS, journal, `--etat`) ; la découverte UDP, l'ajout par IP, l'appairage par code à 6 chiffres ; la fenêtre Déploiement réseau d'IHM Studio (machines, appairage, état, journal). |
| 1.29.0 | L'envoi par blocs avec reprise, VERIFY_PACKAGE, l'installation (versions côte à côte, `active.json`, retour en arrière), le lanceur écrit sur le PC cible, démarrer avec la session, l'arrêt demandé au poste ; le service Windows, le profil d'installation « PC de maintenance », les règles de pare-feu. |
| ensuite | Les licences (Admin, License Manager, éditions, fonctionnalités, licences Runtime) ; l'onglet Déploiement réseau rejoint le License Manager Client. |

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

**Les trames** : une longueur totale sur 4 octets (gros-boutiste ; 2 Mio au plus en 1.28), la longueur de l'en-tête sur 4 octets, l'en-tête JSON, puis les données. L'en-tête d'une demande : `{"proto":1,"op":"STATUS","id":"<identifiant d'opération>","seq":7,"ts":<ms depuis 1970>}` ; d'une réponse : `{"proto":1,"op":"STATUS","re":"<id>","seq":7,"ok":true}` ou `"ok":false,"error":"<code>","message":"<phrase>"`.

**Contre le rejeu** : `seq` croît strictement sur une connexion ; `ts` à moins de 5 minutes de l'horloge de l'agent ; un identifiant d'opération déjà vu rend la même réponse sans rien refaire.

**Les commandes en 1.28** : `IDENTIFY` (les versions de protocole acceptées, le nom de la machine, son identifiant, la version de l'agent ; une version commune est choisie, sinon « incompatible » — jamais forcé), `PAIR_START` / `PAIR_FINISH`, `STATUS`, `GET_LOGS`, `UNPAIR`. Toute autre commande : refusée (`op-inconnue`).

**La découverte** (UDP) : le Studio envoie `{"xpgd":1,"op":"DISCOVER","nonce":"…"}` en diffusion (et en direct aux adresses qu'on lui donne) ; chaque agent répond `{"xpgd":1,"op":"HERE","nonce":"…","machine":"PC-ATELIER-1","id":"M-7F21","version":"1.0.0","protos":[1],"port":47861,"fp":"<empreinte>"}`. Une réponse ne prouve rien : c'est une adresse, rien de plus, tant que la machine n'est pas appairée.

## 5. L'appairage

Sur le PC de maintenance, `XPG-Deployment-Agent --appairer` affiche un **code à 6 chiffres**, valable 5 minutes, utilisable une fois, 3 essais au plus. Le développeur le tape dans le Studio.

Six chiffres se devinent hors ligne si on les envoie, même hachés. Le code n'est donc jamais transmis : les deux machines font un **échange de clés authentifié par mot de passe (CPace sur ristretto255, libsodium)**, lié aux deux certificats de la connexion TLS :

1. `G = ristretto255_from_hash(SHA-512("XPG-PAIR-v1" ‖ code ‖ sid ‖ empreinte Studio ‖ empreinte agent))`, `sid` : 16 octets tirés par le Studio ;
2. le Studio tire `ys`, envoie `Ys = ys·G` (`PAIR_START`, avec son nom et sa clé d'éditeur) ; l'agent tire `ya`, renvoie `Ya = ya·G` et sa confirmation ;
3. chacun calcule `K = y·Y(autre)`, puis la clé de session `ISK = SHA-512("XPG-PAIR-ISK" ‖ K ‖ Ys ‖ Ya ‖ sid)` ; chacun prouve la connaître (HMAC-SHA-256 sur toute la conversation, l'agent d'abord) ;
4. réussi : l'agent garde l'empreinte du Studio, son nom et sa clé d'éditeur ; le Studio garde l'empreinte de l'agent, son nom, son identifiant et son adresse. Le code est consommé.

Un intermédiaire qui présente ses propres certificats obtient un `G` différent de chaque côté : l'échange échoue, et il n'apprend rien qui lui permette de deviner le code hors ligne. Il n'a droit qu'à un essai par connexion, et le code s'efface après 3 échecs.

## 6. L'agent

```
XPG-Deployment-Agent --servir [--dossier D] [--port 47861] [--port-decouverte 47862]
XPG-Deployment-Agent --etat [--dossier D]        ce que fait l'agent (sans réseau)
XPG-Deployment-Agent --appairer [--dossier D]    un code à 6 chiffres
XPG-Deployment-Agent --version
```

Son dossier (par défaut `C:\ProgramData\XPG\Agent` sous Windows) : son identité (protégée), les PC appairés, le code d'appairage en cours (protégé), `etat.json` (écrit toutes les 5 s pendant qu'il tourne), `journal\AAAA-MM-JJ.log` (une ligne par opération). Il ne lance **aucune** commande reçue du réseau ; il n'utilise jamais Internet.

## 7. Ce qui n'est pas simulé

Les essais passent par de vraies sockets (TCP et UDP sur la boucle locale) et de vraies poignées de main TLS. Les essais sur deux PC de ton réseau restent à faire par toi : ils ne sont annoncés que lorsqu'ils ont eu lieu.
