# API Studio et IHM Studio 1.28.0 : le déploiement réseau, première étape — le package signé, l'agent du PC de maintenance, la recherche et l'appairage

Faite à partir de tes réponses du 06/11 :
- « ok pour le tls » : mbedTLS ;
- « déployer une appli ça veut dire envoyer le poste d'exploitation sur l'autre pc, quand tu prends un projet tu génères un lanceur (déjà codé ça) et en gros tu l'envoies sur l'autre pc ».

C'est la première des étapes prévues (`docs/DEPLOIEMENT.md`) :
- **cette version** : le package du poste, l'agent sur le PC de maintenance, la recherche et l'appairage des machines ;
- **la 1.29.0** : l'envoi du package à la machine, son installation (versions côte à côte, retour en arrière) et le lancement du poste là-bas.

Elle se met à jour par-dessus la 1.27.0 et garde tes données.

## 1. Ce qui change pour toi

### Le volet Déploiement réseau
Configuration › **Poste d'exploitation**, outil **Déploiement réseau** (ou Aller à, Ctrl+K : « Déploiement réseau »).

| Outil | Ce qu'il fait |
|---|---|
| Créer un package | le poste du projet (FINISH) dans un fichier `.xpgdeploy` signé, dans `exports/` |
| Rechercher | les PC de maintenance du réseau local répondent (UDP 47862) |
| Ajouter par adresse | une machine par son adresse (un autre sous-réseau) |
| Appairer | le code à 6 chiffres que l'agent affiche |
| État | la machine : système, version de l'agent, en marche depuis, place libre, PC appairés |
| Journal | les dernières opérations de son agent |
| Oublier | ce PC ne la croit plus, et lui demande de l'oublier aussi |

À droite : la machine choisie, et **Ce PC** — l'empreinte de son certificat et celle de ta clé d'éditeur.

Le réseau ne bloque jamais l'écran : chaque geste part à côté, la barre d'état dit où il en est.

### Le package (`.xpgdeploy`)
- Un projet **FINISH** seulement (comme le poste).
- Dedans :
  - les fichiers du projet que lit le poste : `project.xpgproj`, `ihm/` et `donnees/` ;
  - pas les versions, ni les exports, ni la corbeille, ni l'historique, ni le programme de l'automate ;
  - et le lanceur `lancer-poste.cmd`.
- `manifest.json` liste chaque fichier, avec sa taille et son SHA-256. `manifest.sig` en est la signature Ed25519 par **ta clé d'éditeur**.
- Ta clé d'éditeur est créée sur ton PC à la première utilisation. Sa partie secrète reste sur ce PC, protégée par Windows (DPAPI).
- Après l'écriture, le package est relu comme le fera l'agent : signature, chemins, chaque empreinte.
- Il est refusé, avec la raison, s'il y a :
  - un fichier changé, en plus ou en moins ;
  - une signature fausse ;
  - un chemin dangereux (`..`, un chemin absolu, `CON`, `COM1`…) ;
  - un dépassement des limites.

### L'agent du PC de maintenance
`XPG-Deployment-Agent.exe` s'installe avec IHM Studio et avec le profil Poste d'exploitation. En 1.28.0, il se lance à la main, dans une console :

```
XPG-Deployment-Agent.exe --servir       il écoute : TCP 47861 (TLS 1.3), UDP 47862 (la recherche)
XPG-Deployment-Agent.exe --appairer     un code à 6 chiffres, valable 5 minutes, une fois, 3 essais
XPG-Deployment-Agent.exe --etat         ce qu'il fait, son empreinte, les PC appairés
```

- Son dossier : `C:\ProgramData\XPG\Agent` (son certificat, protégé pour la machine ; les PC appairés ; son journal, une ligne par opération).
- Un port déjà pris : il le dit, et il n'écoute pas.
- **Il refuse** :
  - tout PC non appairé (sauf se présenter et s'appairer) ;
  - toute commande rejouée ;
  - une version de protocole inconnue.
- Il ne lance aucune commande reçue du réseau et n'utilise jamais Internet.

### L'appairage
- **Le code ne passe jamais sur le réseau.** Les deux machines font un échange de clés authentifié par ce code (CPace, sur ristretto255), lié aux deux certificats de la connexion TLS. Un intermédiaire qui présenterait ses propres certificats ferait échouer l'appairage, sans rien apprendre qui lui permette de deviner le code.
- Après l'appairage, chaque machine garde l'empreinte du certificat de l'autre ; l'agent garde aussi ta clé d'éditeur.
- Si une machine change de certificat, elle est refusée : « ce n'est plus la même machine, ou quelqu'un s'interpose ».

### Ce que dit l'agent (copié de sa console, sous Wine, pendant les captures)

```
> XPG-Deployment-Agent.exe --appairer
Code d'appairage : 752 376
Valable jusqu'à 14:32:31 (5 minutes), une seule fois, 3 essais au plus.
Tape-le dans IHM Studio > Déploiement réseau > Appairer, sur le PC de développement.
Vérifie que le Studio affiche bien cette machine : M-F3CE, empreinte F3:CE:17:A5:…:9E:41:26:BE

> XPG-Deployment-Agent.exe --etat          (après l'appairage)
Identité : M-F3CE, empreinte F3:CE:17:A5:…:9E:41:26:BE
État : en marche depuis le 06/11/2026 14:27:23, TCP 47861, UDP 47862, 0 connexion(s) ouverte(s)
PC appairés : 1
  vm  empreinte 5F:C7:09:BD:…:6A:3F:76:AA  éditeur 8bf79ff23995db9a...  depuis le 06/11/2026 14:27
Code d'appairage : aucun

journal\2026-11-06.log
14:27:38 op-9741e1cb IDENTIFY vm 127.0.0.1 5F:C7:…:76:AA proto 1
14:27:39 op-ba7d3ad8 PAIR_START essai 1/3 vm 127.0.0.1 5F:C7:…:76:AA
14:27:39 op-c6780737 PAIR_FINISH appairé : vm 127.0.0.1 5F:C7:…:76:AA éditeur 8bf79ff23995db9a
14:27:39 op-09d04c84 STATUS ok vm 127.0.0.1 5F:C7:…:76:AA
14:27:39 op-640d9b4e GET_LOGS 9 lignes vm 127.0.0.1 5F:C7:…:76:AA
```
L'empreinte du Studio (5F:C7:…:76:AA) est celle que montre le volet à droite (Ce PC) ; celle de l'agent (F3:CE:…:26:BE) est celle que le volet a gardée.

## 2. Les captures

Tout a été fait sur une seule machine, sous Wine :
- l'agent Windows installé (`XPG-Deployment-Agent.exe --servir`) tournait sous Wine ;
- `--appairer` a donné le code ;
- IHM Studio installé, ouvert dans une fenêtre sur le projet de démonstration Armoire_Gaz (FINISH), a été piloté par une session (`--script`, `--captures`).

Le code a été tapé dans le vrai dialogue. Les neuf images jointes :
1. `1280_01_poste_outil.png` : Configuration › Poste d'exploitation, le nouvel outil Déploiement réseau ;
2. `1280_02_volet.png` : le volet ; à droite, Ce PC (l'empreinte de son certificat, ta clé d'éditeur), aucune machine ;
3. `1280_03_rechercher.png` : Rechercher - l'agent a répondu à la diffusion UDP (« 1 machine a répondu »), « Découverte » ;
4. `1280_04_dialogue_adresse.png` : Ajouter par adresse (127.0.0.1) ;
5. `1280_05_dialogue_code.png` : Appairer VM, le code tapé ;
6. `1280_06_appairee.png` : « Appairée » ; « son empreinte … est gardée ici ; la tienne et ta clé d'éditeur là-bas » ;
7. `1280_07_etat.png` : État, lu par le réseau : Windows 10 Pro (ce que Wine annonce), agent 1.0.0, en marche depuis 15 s, place libre, 1 PC appairé ;
8. `1280_08_journal.png` : le journal de l'agent, lu à distance (IDENTIFY, PAIR_START, PAIR_FINISH, STATUS) ;
9. `1280_09_package.png` : Créer un package : `exports\Projet-V2.xpgdeploy`, 80 fichiers, 449,5 Kio, créé puis vérifié (signature et empreintes).

## 3. Les essais

Sous Linux : **141 suites d'essais sur 141** réussies (CTest), dont quatre nouvelles :
- **`deploycrypto`** (18 contrôles) : libsodium et mbedTLS intégrés - SHA-256 sur les vecteurs de la norme, Ed25519 sur le vecteur de la RFC 8032, une signature fausse refusée, la protection locale.
- **`deploypackage`** (191 contrôles) :
  - la clé d'éditeur : créée, relue, jamais remplacée si elle est abîmée ;
  - la collecte (exactement les bons fichiers) ;
  - la construction et la relecture ;
  - les refus : un octet changé, un fichier en plus ou en moins, une signature fausse, une autre clé, le manifeste modifié, 18 chemins dangereux, les limites, un projet DEV, LOCK ou d'API Studio.
  - L'essai a aussi été compilé pour Windows et lancé sous Wine : 186 contrôles, avec la vraie protection DPAPI de Wine.
- **`deploynet`** (116 contrôles), de vraies sockets et de vraies poignées de main TLS 1.3 :
  - IDENTIFY et une version incompatible ;
  - un PC non appairé refusé ;
  - l'appairage : code faux, 3 échecs, code expiré, un tiers qui s'interpose avec ses propres certificats (refusé) ;
  - le rejeu ;
  - les trames trop grosses, 8 connexions au plus ;
  - un port pris, un certificat qui change ;
  - la découverte UDP.
  - Cinq passes de suite, toutes vertes. Les détecteurs d'erreurs mémoire, de comportement indéfini et d'accès concurrents (ASan, UBSan, TSan) n'ont rien signalé sur le code du déploiement.
- **`deployui`** (30 contrôles) : le volet de bout en bout avec un vrai agent - ajouter, un code faux refusé, appairer, rechercher, état, journal, créer un package puis le relire, un projet DEV refusé, oublier.

**L'essai croisé** : l'agent Windows sous Wine et un client Linux - découverte, TLS 1.3, appairage, reconnexion épinglée, état, journal ; puis, pour les captures, IHM Studio Windows et l'agent Windows, tous deux sous Wine.

Au premier passage :
- l'essai de l'éditeur ne se liait plus (il compile le volet : il lui fallait la bibliothèque du déploiement) ;
- la suite `aiincidents` avait disparu : elle citait l'ancien emplacement du JSON, déplacé dans `core/` pour servir aussi au déploiement ;
- un exemple du guide était un bloc de commandes sans langage.

Les trois sont corrigés. **`hmi_editor_test`** : 10 982 contrôles.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.28.0.exe` : l'installateur (54 204 724 octets). Il met à jour la 1.27.0 au même endroit ;
- le même installateur dans un zip ;
- `API-Studio-1.28.0-portable.zip` (190 fichiers) et `IHM-Studio-1.28.0-portable.zip` (191 fichiers, l'agent en plus) ;
- les neuf captures ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.28.0.exe` | `FC191A49CCC3B36D54F0E4830B43B723DEF06E229F84B96C5979263E2A4D9769` |
| `API-Studio-1.28.0-portable.zip` | `D278A2C0B80BC58B90E32BB2DC1034DE31CDFE5895607B31B8AD4392CFABC1F0` |
| `IHM-Studio-1.28.0-portable.zip` | `8E1556DBE9E8178674E5B366FAC2C0FDB550443E801A9B21EEC60634FE81003C` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.27.0 livrée :
- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les trois exécutables installés sont identiques à ceux construits ;
- `--version` répond :
  - « API Studio 1.28.0 » ;
  - « IHM Studio 1.28.0 » ;
  - « XPG-Deployment-Agent 1.0.0 (protocole 1, TLS 1.3 : mbedTLS 3.6.2, libsodium 1.0.20) » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.27.0 est gardée ;
- **IHM Studio et l'agent ont tourné ensemble** (sous Wine) : les neuf captures viennent des exécutables livrés.

Les licences de mbedTLS (Apache-2.0) et de libsodium (ISC) sont dans le dossier `licences` du programme.

## 5. Limites

- **L'envoi et l'installation ne sont pas encore faits** (1.29.0) : un package se copie à la main pour l'instant.
- **L'agent n'est pas encore un service Windows**, et l'installateur ne crée pas encore de règle de pare-feu (1.29.0). Au premier `--servir`, Windows peut demander d'autoriser l'agent sur le réseau.
- **Le lanceur du package** pointe vers le programme de ton PC (son chemin). Si IHM Studio est installé ailleurs sur le PC de maintenance, le lanceur est à corriger ; à partir de la 1.29.0, l'agent le réécrit.
- **Le package ne prend que le dossier du projet** : un fichier externe en chemin absolu, ou une bibliothèque reliée hors du projet, ne part pas.
- **La recherche (UDP)** ne traverse pas les routeurs : dans un autre sous-réseau, Ajouter par adresse.
- **Le volet est dans IHM Studio** : le License Manager, où la maquette le place, n'existe pas encore (étape des licences).
- **Sous Linux**, la clé d'éditeur est dans un fichier lisible par toi seul, sans chiffrement (DPAPI est propre à Windows).
- **Les essais sur deux vrais PC de ton réseau restent à faire par toi.** Tout a été vérifié sur une seule machine : sous Linux, et sous Wine pour les exécutables Windows. Rien n'a été vérifié sur un vrai Windows.
