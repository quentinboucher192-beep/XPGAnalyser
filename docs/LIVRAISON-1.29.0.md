# API Studio et IHM Studio 1.29.0 : le déploiement réseau, deuxième étape — Déployer, le poste lancé et arrêté sur le PC de maintenance, le retour en arrière

Faite à partir de ta demande du 06/11 : « continue avec la 1.29 ».

C'est la deuxième des étapes prévues (`docs/DEPLOIEMENT.md`) :
- **la 1.28.0** : le package signé, l'agent sur le PC de maintenance, la recherche et l'appairage ;
- **cette version** : envoyer le package, l'installer à côté de la version en place, arrêter proprement le poste en marche, lancer la nouvelle version, revenir en arrière ; l'agent avec la session, le pare-feu ;
- **la 1.30.0** : les licences, sans service en ligne. Le socle est déjà écrit et essayé dans cette version, mais il n'est branché nulle part (voir § 5).

Elle se met à jour par-dessus la 1.28.0 et garde tes données.

## 1. Ce qui change pour toi

### Déployer (IHM › Configuration › Poste d'exploitation › Déploiement réseau)

Choisis une machine **appairée**, puis **Déployer**. Le dialogue demande trois réglages :
- démarrer le poste **avec la session** (oui au départ) ;
- le **délai** laissé au poste en marche avant de se fermer (60 s) ;
- ce qu'on fait **s'il ne se ferme pas** : attendre, ou l'arrêter quand même.

Ensuite, tout s'enchaîne et la barre d'état suit chaque étape :
1. le package est créé et signé (comme Créer un package) ;
2. il part par blocs de 1 Mio. Après une coupure, ou si tu cliques **Annuler**, le prochain Déployer reprend où il en était, même si l'agent a redémarré entre-temps ;
3. l'agent le vérifie : la signature de ta clé d'éditeur, l'empreinte de chaque fichier, les chemins. Un paquet faux est refusé et rien n'est installé ;
4. il l'installe **à côté** de la version en place, un dossier par version, et réécrit le lanceur pour ce PC ;
5. le poste en marche affiche un **bandeau orange** : « Mise à jour du poste (…) : il se ferme dans N s ; son état est gardé ». Il garde son état puis se ferme ;
6. la nouvelle version démarre et **reprend cet état sans question** : la vue, les variables IHM, les alarmes en cours. Si elle ne tient pas 8 s, l'agent **revient seul** à la précédente et le dit.

À droite du volet, chaque étape est détaillée (fait, sans objet, échec), avec ce qui a été installé et la version d'avant.

### Revenir à la précédente, Applications, Historique

- **Revenir à la précédente** remet la version gardée sur la machine. Le poste en marche est prévenu, puis fermé, et la précédente redémarre.
- **Applications** montre, pour la machine choisie :
  - la version active ;
  - les versions gardées (trois au plus ; l'active, la précédente et celle en marche ne sont jamais effacées) ;
  - le poste, en marche ou non ;
  - s'il démarre avec la session.
- **Historique** montre chaque installation, lancement, arrêt et retour : quand, depuis quel PC, quelle version, le résultat.
- Un projet qui a changé **sans nouvelle version** s'installe à côté sous « V2-2 » : rien n'est jamais écrasé.

### Sur le PC de maintenance

- **L'installateur, profil Poste d'exploitation** a une nouvelle case **Recevoir les déploiements** (décochée au départ). Cochée :
  - l'agent démarre avec la session de l'opérateur (valeur `Run` « XPG Deployment Agent »). Il travaille sans fenêtre et c'est lui qui lance le poste dans cette session ;
  - pour une installation pour tous les comptes, deux règles du pare-feu sont posées, limitées au programme de l'agent : TCP 47861 et UDP 47862, réseaux **domaine et privé**, pas public ;
  - l'agent démarre tout de suite à la fin de l'installation ;
  - une mise à jour l'arrête le temps de la copie ;
  - décocher la case, ou désinstaller, retire la valeur et les règles.
- **Sans l'installateur** : `XPG-Deployment-Agent.exe --demarrer-avec-session oui`. Si l'installateur a déjà posé la valeur pour tous les comptes, l'agent n'en ajoute pas une seconde : deux agents au démarrage, c'est un port pris.
- **L'agent passe en 1.1.0** et parle les versions **1 et 2** du protocole.
  - Un Studio 1.28 garde l'appairage, l'état et le journal ; s'il veut déployer, l'agent lui demande de se mettre à jour.
  - Un agent 1.28 face au Studio 1.29 : « il ne sait pas encore installer : mets-le à jour ».
- **L'agent ne lance que le poste** : IHM-Studio.exe posé à côté de lui, avec `--ihm "<dossier de la version>"`. Il n'exécute jamais une commande reçue du réseau.
- **Un poste lancé par la session**, sans l'agent, est retrouvé par l'agent. Il est prévenu de la même façon : il déduit ses fichiers d'échange de son dossier.

### Ce que dit l'agent (copié de son `--etat` et de son journal, sous Wine, pendant les captures)

```
XPG-Deployment-Agent 1.1.0 - dossier : Z:/…/capt/agent1290
Démarre avec la session : oui (HKLM\Software\Microsoft\Windows\CurrentVersion\Run — XPG Deployment Agent)
Poste : Projet V2 (pid 1204)
Programme du poste : C:\Program Files\API IHM Studio\IHM-Studio.exe
Applications installées : 1
  Projet  active : V2  (2 version(s))  en marche

19:00:46 PREPARE ok Projet V2 449,5 Kio
19:00:46 VERIFY_PACKAGE ok Paquet valide : Projet V2, signé par « vm » (95:FA:DD:86:…:CA:5A:32:E1) ; 80 fichiers, 1,0 Mio.
19:00:46 INSTALL ok Projet V2 installée et active
19:00:46 POSTE lancé : Projet V2 pid 352 (C:\Program Files\API IHM Studio\IHM-Studio.exe --ihm)
19:00:54 START ok Projet V2 pid 352 tient 8,0 s
19:01:06 INSTALL ok Projet V2-2 installée et active (avant : V2)
19:01:06 POSTE arrêt demandé : Projet V2 pid 352, délai 20 s (Mise à jour vers Projet V2 demandée depuis vm)
19:01:34 STOP ok Projet V2 pid 352 a quitté en 28 s
19:01:42 START ok Projet V2-2 pid 692 tient 8,0 s
19:01:44 POSTE arrêt demandé : Projet V2-2 pid 692, délai 60 s (Retour à la version V2 demandé depuis vm)
19:02:59 ROLLBACK ok Projet : retour de V2-2 à V2, relancée (pid 1204)
```

## 2. Les captures

**Deux PC sous Wine, sur une seule machine Linux.** Ce n'est pas un vrai réseau : les deux « PC » se parlent par la boucle locale.
- **Le PC du Studio** : un préfixe Wine, son écran virtuel. IHM Studio 1.29.0 y est installé par l'installateur livré.
- **Le PC de maintenance** : un autre préfixe Wine, son propre écran virtuel. L'installateur livré y a posé le profil Poste d'exploitation avec la case Recevoir les déploiements. L'agent installé y sert et lance le **vrai** poste (IHM-Studio.exe installé).

Le Studio a trouvé l'agent par la diffusion UDP, s'est appairé avec son code, puis a déployé le projet de démonstration Armoire_Gaz (FINISH). Il l'a déployé une seconde fois, après un changement de commentaire, puis il est revenu à la précédente. Le PC de maintenance a été photographié toutes les 3 s : ce sont les trois dernières images.

1. `1290_01_appairee_outils.png` : la machine appairée ; les nouveaux outils Déployer, Revenir à la précédente, Annuler, et les onglets Applications et Historique. Une seconde ligne « VM » (M-F3CE) vient des captures de la 1.28.0, faites sur le même préfixe Wine ;
2. `1290_02_dialogue_deployer.png` : le dialogue Déployer (avec la session, délai, si le poste ne se ferme pas) ;
3. `1290_03_progression.png` : pendant l'envoi, la barre d'état suit l'étape, ici « Lancement du poste - VM » ;
4. `1290_04_deploye.png` : « Déployé sur VM : Projet V2, 449,5 Kio envoyés ; le poste tourne », avec à droite chaque étape ;
5. `1290_05_applications.png` : Applications : Projet, active V2, le poste en marche ;
6. `1290_06_historique.png` : l'installation et le lancement ;
7. `1290_07_journal.png` : le journal de l'agent, lu à distance ;
8. `1290_08_second_deploye.png` : le second Déployer (délai 20 s) : « Projet V2-2 … ; l'ancien poste s'est fermé ; le poste tourne » ;
9. `1290_09_applications_deux.png` : active V2-2, V2 gardée ;
10. `1290_10_dialogue_retour.png` : le dialogue Revenir à la version précédente ;
11. `1290_11_retour.png` : « Revenu à Projet V2 sur VM (depuis V2-2) ; le poste a été relancé » ;
12. `1290_12_historique.png` : les 8 opérations ;
13. `1290_13_poste_bandeau_mise_a_jour.png` : **l'écran du PC de maintenance** : le bandeau orange du poste en marche, « il se ferme dans 15 s ; son état est gardé » ;
14. `1290_14_poste_nouvelle_version_etat_recharge.png` : la nouvelle version (V2-2) en marche. Son journal dit « État d'avant l'arrêt rechargé » : l'état a bien passé d'une version à l'autre ;
15. `1290_15_poste_bandeau_retour.png` : le bandeau au retour en arrière (« Retour à la version V2 demandé depuis vm », 60 s).

Sur l'écran du PC de maintenance, le projet de démonstration ouvre trois écrans. Ils se superposent sur le seul écran virtuel : c'est attendu. L'alarme « Variateur injoignable » vient de l'équipement Modbus absent.

## 3. Les essais

Sous Linux : **143 suites d'essais sur 143** réussies (CTest), dont deux nouvelles.
- **`deployinstall`** (89 contrôles) : un vrai agent et un faux poste.
  - L'envoi par blocs et sa reprise, même après un redémarrage de l'agent ; un bloc abîmé et un paquet faux refusés ; le disque plein simulé.
  - L'installation par versions (V3-2), le lanceur réécrit.
  - L'arrêt demandé (le poste quitte, ou il ignore et l'arrêt est forcé), le contrôle de démarrage et le retour automatique, le retour en arrière.
  - L'état repris d'une version à l'autre ; l'agent qui redémarre et retrouve son poste ; un poste lancé hors de l'agent ; un Studio 1.28 refusé avec une phrase claire.
- **`licensing`** (154 contrôles) : le socle des licences, voir § 5.
- **`deployui`** passe de 30 à **48 contrôles**. Le volet du Studio, avec un vrai agent, fait :
  - Déployer, où la progression s'est vue à l'écran ;
  - Applications et Historique ;
  - un second Déployer : l'ancien poste prévenu, son état repris par le suivant ;
  - Revenir à la précédente ;
  - un projet DEV refusé.
- **`deploynet`** (116) est mis à jour pour la version 2 du protocole ; **`deploypackage`** (191) et **`deploycrypto`** (18) passent.
- **`hmi_editor_test`** passe toujours avec le guide à jour.

**L'essai croisé** : l'agent Windows sous Wine, piloté par un client Linux. Il a fait l'appairage, V1 envoyée et lancée, V2 qui reprend l'état de V1, V3 qui s'arrête tout de suite (retour automatique à V2), le retour à V1, la liste et l'historique, `--demarrer-avec-session`. Deux défauts propres à Windows ont été trouvés et corrigés :
- l'heure de création d'un processus perdait sa précision ;
- Wine 9 ne connaît pas une classe de `NtQueryInformationProcess`, d'où un repli.

**Pendant la livraison**, l'installation silencieuse du profil Poste d'exploitation restait bloquée sur une boîte « Ce dossier n'existe pas ». C'est corrigé :
- en silencieux, plus aucune boîte ne bloque ;
- le dossier du poste peut ne pas exister encore, puisque l'agent y déploiera le projet ; le journal le dit ;
- `/TASKS` connaît la nouvelle case.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.29.0.exe` : l'installateur (54 413 894 octets). Il met à jour la 1.28.0 au même endroit ;
- le même installateur dans un zip ;
- `API-Studio-1.29.0-portable.zip` (190 fichiers) et `IHM-Studio-1.29.0-portable.zip` (191 fichiers, l'agent en plus) ;
- les quinze captures ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.29.0.exe` | `4C75E694FE075060BBFDB2260B0371F829C5E5DEE02B65ACE6C3C72B90F5BFD7` |
| `API-Studio-1.29.0-portable.zip` | `CB129793EE05F75B85898B7EFE9657DCB227B743FDB7F48F3D0CD67E42962CAC` |
| `IHM-Studio-1.29.0-portable.zip` | `F7EF02D1D0414D3FA0F4AA8A3B1C76C332DA588083B2C9B01896CA28F6435F00` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux.

**Par-dessus la 1.28.0 livrée :**
- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les exécutables installés sont identiques à ceux construits ;
- `--version` répond :
  - « API Studio 1.29.0 » ;
  - « IHM Studio 1.29.0 » ;
  - « XPG-Deployment-Agent 1.1.0 (protocoles 1 et 2, TLS 1.3 : mbedTLS 3.6.2, libsodium 1.0.20) » ;
- `--cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable fait l'analyse, la compilation et l'exécution ;
- les associations sont en place ;
- les copies de secours de la 1.28.0 et de la 1.29.0 sont gardées.

**Sur un PC neuf, profil Poste d'exploitation** (`/PROFIL=poste /MERGETASKS=agent`) :
- l'installation réussit ;
- l'agent installé est identique à celui construit ;
- la valeur `Run` « XPG Deployment Agent » est dans HKLM ;
- `netsh` a répondu 0 aux deux règles du pare-feu. **Mais Wine n'a pas de vrai pare-feu** : que les règles existent et laissent passer n'est vérifiable que sur un vrai Windows.

**Le contrôle du paquet** a passé sur le dossier d'installation et sur chaque portable : rien de l'outil d'administration des licences, aucune clé privée, aucun jeton.

## 5. Le socle des licences (pour la 1.30.0, branché nulle part)

Il a été écrit en parallèle, comme convenu, pour que la 1.30.0 n'ait plus que l'interface à faire.
- **`src/licensing`** contient :
  - les documents signés Ed25519 : la licence, le registre des éditions et fonctionnalités, la liste de révocation, la demande d'activation ;
  - le magasin local (état protégé par Windows) ;
  - les questions que poseront les menus : `isFeatureEnabled`, `getEdition`, `getLicenseStatus`…, avec une phrase claire par état.
- **« Jamais de perte de travail »** est une règle écrite : Ouvrir, Enregistrer, Exporter, Fermer et Quitter ne sont jamais verrouillés.
- **`XPG-License-Admin`** est l'outil qui crée ta clé d'éditeur et signe les licences. Il **n'est jamais livré** : la construction s'arrête si la moindre trace de lui, d'une clé privée ou de la clé d'essai se trouve dans ce qui part chez un client.
- **La clé de l'éditeur** est pour l'instant une **clé d'essai**, marquée comme telle. Sa partie privée n'est pas dans le dépôt. La vraie sera créée sur ton PC, à la 1.30.0.
- Voir `docs/LICENCES.md`.

Rappel : l'activation en ligne et les licences flottantes attendent ta décision sur l'hébergement du service de licences.

## 6. Limites

- **Rien n'a été essayé sur un vrai Windows**, ni entre deux vrais PC de ton réseau : UAC, Defender, le vrai pare-feu, une vraie session au démarrage. Tout a été fait sous Linux, et sous Wine pour les exécutables Windows. Ces essais restent à faire par toi.
- **L'agent est un programme de console.** Lancé par la session, il cache sa console au démarrage, mais une fenêtre peut apparaître un instant à l'ouverture de la session.
- **Un seul poste par PC** : Déployer arrête le poste en marche, quelle que soit son application.
- **Revenir à la précédente** prend la plus récente installée avant l'active, par date d'installation : deux retours de suite ne font pas un aller-retour.
- **L'historique de l'agent** (`historique.jsonl`) grandit sans limite.
- **Sous Windows, ce que le poste écrit sur sa sortie n'est pas gardé** par l'agent (c'est le cas sous Linux).
- **La recherche (UDP)** ne traverse pas les routeurs : dans un autre sous-réseau, Ajouter par adresse.
