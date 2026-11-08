# Licences — conception (préparées en 1.29.0, branchées en 1.30.0)

Ce document décrit les licences : le format des fichiers signés, le registre des éditions et fonctionnalités, l'identité d'un PC, la vérification hors ligne et ses états (dont **« Non activées »** : un programme sans clé d'éditeur n'est jamais bloqué), l'activation par fichier, les deux fichiers de clés et l'option CMake de la clé d'essai, ce que lit l'écran Licence (registre, PC liés, historique), l'outil de l'éditeur (livré à part, avec son registre des activations), l'installateur et le contrôle du paquet. Il reprend la maquette validée (onglets Studio, Admin, Client, Architecture) et les décisions prises avec toi :
- **version 1 sans service en ligne** : licences signées, vérifiées hors ligne, activation **par fichier** ;
- **rien n'est écrit en dur** : les éditions, les fonctionnalités et leurs droits viennent d'un **registre signé** et numéroté ; le code ne porte que des identifiants de fonctionnalités (`ai_generation`, `deploy_network`…) ;
- **un PC est une paire de clés** créée sur lui (Ed25519, clé secrète sous DPAPI machine) ; jamais son matériel ; la licence porte aussi l'empreinte salée de l'installation de Windows (1.32.0) ;
- **une clé d'éditeur** (Ed25519) signe licences, registres et révocations ; sa clé publique est compilée dans les logiciels clients ; sa clé privée ne quitte pas le poste Admin.

Retour au [README](../README.md). Voir aussi [DEPLOIEMENT.md](DEPLOIEMENT.md) (la clé d'éditeur du développeur, `deploy/DevKey`, qui signe les paquets : ce n'est pas la même clé).

## 1. Où on en est

| | Fait | Pas fait |
|---|---|---|
| Bibliothèque | 1.29.0 : `src/licensing/` (`xpg_licensing`) : formats, registre, licence, révocations, identité du PC, demande d'activation, import, vérification hors ligne, états, l'API de la maquette. **1.30.0** : l'état **Non activées** (aucune clé de l'éditeur : tout est ouvert, § 8 bis), ce que lit l'écran Licence (le registre, les PC liés, l'historique, le dossier, l'empreinte de ce PC, § 9), l'historique de ce PC (`historique.jsonl`), un dossier non inscriptible qui ne bloque rien | — |
| Clés de l'éditeur | **1.30.0** : deux fichiers (§ 11 bis) — `PublisherKey.hpp`, les **vraies** clés, **vide** aujourd'hui ; `TestPublisherKey.hpp`, la clé d'**essai**, compilée **seulement** avec l'option CMake `XPG_LICENCES_CLE_ESSAI` (OFF par défaut) | **la vraie clé**, à créer sur ton PC (§ 11) ; d'ici là, les programmes livrés sont « Non activées » |
| Outil Admin | **1.33.0 : l'application `XPG-License-Manager-Admin`** (ses fenêtres, § 11 septies) ; `src/license_admin/` : `XPG-License-Admin` en console (clé de l'éditeur, `exporter-cle` vide ou à plusieurs clés, signer registre / licence / révocations, vérifier). **1.30.0** : le **registre des activations** (`activations.jsonl`, `enregistrer-demande`, `liste`, `--csv`) ; **livré à part à l'éditeur** (`outils/mingw/admin_editeur.sh` : `XPG-License-Admin-<version>-EDITEUR.zip`, avec son LISEZ-MOI pas à pas). **1.31.0** : l'**archive** du poste et sa **synchro privée** (§ 11 quater : licences et registre des activations chiffrés, fusion entre postes, code de secours ; par le **service des licences** depuis la 1.35.0, un dépôt GitHub privé de la 1.31.0 à la 1.34.0). **1.32.0** : la **sauvegarde de la clé par une phrase de passe** (§ 11 quinquies), le dépôt remis en arrière refusé (§ 11 sexies) | la boutique et les commandes (un paiement), la vérification du poste Admin par défi signé |
| Interface | **1.30.0** : les Studios lient `xpg_licensing` (l'écran Licence et les verrous des entrées : `src/app/Licence*`) | — |
| Installateur | **1.30.0** : `%ProgramData%\XPG\Licences` créé pour tous les comptes, avec le droit Modifier des Utilisateurs ; gardé à la désinstallation ; le registre livré (`licences\registre.xpgreg`) s'il est dans le dossier à livrer ; le contrôle du paquet dans `package_mingw.py` **et** `package.ps1` (§ 13) | le registre réel, signé par toi (`ressources-licences/registre.xpgreg`) |
| En ligne | **1.35.0** : le service des licences (`docs/SERVICE-LICENCES.md`) ; côté PC (§ 12 bis) : la **licence en ligne** (champ `service`, aucun PC lié) valable avec l'activation de ce PC signée par le service, le **bail** et son **compteur** (l'heure retenue), l'heure signée, la licence **flottante** et son jeton, la **Découverte** enregistrée par le service | **voir chaque lancement du logiciel** (§ 12) |

## 2. Les programmes et les modules

| Où | Quoi |
|---|---|
| `src/licensing/Time` | les dates : secondes UTC, écrites `AAAA-MM-JJTHH:MM:SSZ` et rien d'autre |
| `src/licensing/SignedFile` | l'enveloppe signée (§ 3) |
| `src/licensing/Documents` | registre, licence, révocations, demande : lecture **stricte**, écriture canonique |
| `src/licensing/KeyFile` | une paire Ed25519 gardée sur ce PC (`SigningKey`) ; l'identité du PC |
| `src/licensing/Store` | les licences de ce PC : évaluer, importer, écrire la demande d'activation |
| `src/licensing/Licensing` | l'API des logiciels (§ 9) |
| `src/licensing/PublisherKey.hpp` | **généré** par l'outil Admin (`exporter-cle`) : les **vraies** clés publiques de l'éditeur (`kKeys`) — aucune aujourd'hui |
| `src/licensing/TestPublisherKey.hpp` | **généré** : la clé d'**essai** (`kTestKeys`), compilée seulement avec `XPG_LICENCES_CLE_ESSAI` (§ 11 bis) |
| `src/licensing/PublisherKeyEntry.hpp` | le format d'une ligne de ces deux listes (`KeyEntry`) ; il ne change pas |
| `src/license_admin/Admin`, `AdminMain.cpp` | l'outil de l'éditeur (cible `xpg_license_admin`, exe `XPG-License-Admin`) — **jamais dans un paquet client** ; livré à part à l'éditeur |
| `outils/mingw/admin_editeur.sh`, `LISEZ-MOI-EDITEUR.txt` | 1.30.0 : l'outil de l'éditeur construit avec MinGW et emballé à part (§ 11 ter) |
| `tests/licensing_test.cpp` | l'essai (ctest `licensing`) |
| `tests/fixtures/licences/` | le registre d'exemple (les éditions et fonctionnalités de la maquette), des paramètres et une révocation d'exemple |

`licensing/` ne dépend que de `core/` (JSON, processus) et de `deploy/Crypto` (SHA-256, Ed25519, hasard, DPAPI, écriture d'un fichier privé : rien n'est réécrit). Il n'a ni interface ni lien vers `src/app/` ; c'est `xpg_app` qui le lie (1.30.0 : `target_link_libraries(xpg_app PUBLIC xpg_licensing)` ; CMake remet `xpg_deploy` et `xpg_core` après lui sur la ligne de lien, ce que demande l'éditeur de liens de MinGW).

## 3. L'enveloppe signée

Licence (`.xpglic`), registre (`.xpgreg`), révocations (`.xpgrev`) et demande d'activation (`.xpgreq`) sont des fichiers texte faits d'un ou de plusieurs **blocs** :

```
XPG-LICENSING 1 licence 812
{ … exactement 812 octets de JSON … }
SIGNATURE ed25519 xpg-cb0d1ae560b52310 <128 hexadécimaux>
```

- Le JSON est **signé octet pour octet**, tel qu'il est dans le fichier (comme le manifeste d'un paquet de déploiement) ; il n'est jamais réécrit avant d'être vérifié. Le message signé est `"XPG-LICENSING-1\n" + genre + "\n" + JSON` : une signature de registre ne passe pas pour une licence, ni pour quoi que ce soit d'autre signé par la même clé.
- `xpg-…` : l'identifiant de la clé (les 16 premiers hexadécimaux du SHA-256 de la clé publique). Le client cherche cette clé parmi celles qu'il croit (`PublisherKey.hpp`).
- La taille dans l'en-tête fait qu'un fichier **tronqué** se voit toujours. Un fichier dont les fins de ligne ont changé (CRLF) n'est plus le même : refusé. À transmettre tel quel (pièce jointe).
- Le fichier rendu par l'Admin contient **la licence, le registre et, si on la donne, la liste de révocation** : le client n'importe qu'un fichier.

**La lecture est stricte** : JSON bien formé, un objet, **aucune clé en double** (à aucun niveau), **aucune clé inconnue** (un champ qui restreindrait une licence ne peut pas être ignoré en silence ; un champ nouveau = une version nouvelle du format), chaque champ du bon genre et dans ses bornes. Un format plus récent est refusé avec « mets à jour ce programme ».

## 4. Le registre (le « manifeste numéroté »)

```json
{
  "format": "xpg-registry", "version": 1,
  "number": 318,
  "issued_at": "2026-11-06T09:41:00Z",
  "unlicensed_edition": "FREE",
  "features": [
    { "id": "ai_generation", "product": "IA", "label": "Génération",
      "limits": [ { "id": "requests_per_day", "label": "Générations par jour" } ] },
    { "id": "api_editor", "product": "API Studio", "label": "Éditeur" }
  ],
  "editions": [
    { "id": "PROFESSIONAL", "label": "PROFESSIONAL", "type": "temporary", "max_devices": 5,
      "duration_days": 365, "offline_grace_days": 14,
      "features": [ { "id": "api_editor" }, { "id": "ai_generation" } ] }
  ]
}
```

- Une fonctionnalité : un identifiant stable (`[a-z0-9_]`), son logiciel, son libellé, ses **limites** possibles (un entier nommé : `max_views`, `requests_per_day`…). `"retired": true` : retirée — encore connue des clients (les licences déjà signées restent lisibles), plus accordée par l'Admin.
- Une édition : ses valeurs **par défaut** pour une licence neuve (type, nombre de PC, durée, grâce, fonctionnalités et limites). L'Admin s'en sert ; une licence signée porte **sa propre copie** des fonctionnalités : changer une édition ne touche pas les licences déjà signées.
- 1.37.0 : une édition peut porter `"price"` (« 490 € HT / an », 60 caractères au plus) et `"description"` (une phrase, 200 au plus) : la **boutique** des Studios (onglet Demander ma licence). Écrits seulement s'ils sont remplis : un registre sans eux reste identique octet pour octet. **Un Studio 1.36 ou plus ancien refuse un registre qui en porte** : installer la 1.37.0 avant d'en signer un.
- 1.37.0 : l'onglet Éditions de la fenêtre Licence montre, pour la fonctionnalité choisie, une capture et deux lignes livrées avec le logiciel : `resources/licences/fonctions/<id>.png` (960 × 600) et `descriptions.json` (`{"format": "xpg-fonctions-apercu/1", "fonctions": {"<id>": "texte"}}`). Une fonctionnalité sans capture le dit.
- 1.38.0 : une **réduction** — `"promo_price"` (le prix réduit, 60 caractères au plus) et `"promo_until"` (sa fin, une date ISO, exigée avec lui). La carte de la boutique barre l'ancien prix et montre le nouveau « jusqu'au … » ; passée cette date, elle reprend le prix normal d'elle-même.
- 1.38.0 : les **annonces** de l'éditeur — `"announcements"` (20 au plus) : `id` (`[A-Za-z0-9_-]`, 40 au plus, unique), `title` (120), `text` (600), `edition` (facultatif : une édition du registre), `at`, `until` (facultatif). « Prévenir mes clients » (outil de l'éditeur, page Éditions) en écrit une à la signature d'une réduction. Chaque Studio la montre **une seule fois** dans ses notifications (jamais par mail) ; un client hors connexion la reçoit à sa prochaine connexion (elle voyage dans le registre signé). Une annonce finie n'est plus montrée ; l'outil la retire à la signature suivante.
- 1.38.0 : **un Studio 1.37 ou plus ancien refuse un registre qui porte une réduction ou une annonce** (la lecture est stricte) : il garde son registre d'avant, rien ne casse, mais il ne voit pas le nouveau. Installer la 1.38.0 chez les clients avant d'en signer un.
- 1.38.0 : **les notifications des Studios** (accueil › Notifications, Aide › Notifications…, la cloche du projet). Chaque Studio retient ce qu'il a déjà vu (`vu-licence.json`, dans ses réglages) et compare à chaque changement : une offre dont les fonctionnalités changent (l'avant et l'après, ce qui est ajouté et retiré ; « ta licence » si c'est la sienne), une offre nouvelle ou retirée, les annonces, la licence installée, renouvelée, passée dans une autre offre, révoquée, finie, bientôt finie (14 jours avant). Le premier lancement retient sans rien dire (sauf les annonces en cours). Le carnet : `notifications.json` dans les réglages de chaque Studio (300 au plus).
- 1.39.0 : **l'offre modifiée, précisément**. La notification dit ce qui change en une ligne (« 1 fonctionnalité retirée : Éditeur · 1 ajoutée : … · Prix : 490 € → 390 € · PC au plus : 5 → 10 · Durée : 365 jours → sans fin ») et le montre dans **un tableau** (fonctionnalité, logiciel, avant, après ; retirée en rouge, ajoutée en vert) ; le prix, les PC et la durée qui changent sont dits aussi (ils sont retenus dans `vu-licence.json` : `postes`, `duree`, et `logiciels` pour la colonne Logiciel ; un fichier de la 1.38 ne les a pas — rien n'est inventé, ils seront dits au changement suivant). Le bouton **Voir le détail** ouvre la fenêtre « Offre « X » : avant / après » : toute l'offre ou seulement les changements, et la capture de chaque fonctionnalité (la molette zoome). Le tableau et l'avant / l'après voyagent dans la notification (`Item::data`, la clé `donnees` de `notifications.json`).
- `unlicensed_edition` : l'édition qui s'applique **sans licence** (et quand la licence ne vaut pas) — FREE dans la maquette.
- **Numéroté** : un client refuse un registre plus ancien que le plus récent qu'il a vu (à l'import, et si le fichier est remplacé sur le disque).
- Le registre d'exemple (`tests/fixtures/licences/registre-exemple.json`) reprend les 21 fonctionnalités et les 6 éditions de la maquette (FREE 4, PERSONAL 10, PROFESSIONAL 18, ENTERPRISE 21, EVALUATION 18, CUSTOM 0). Ce sont des **exemples** : tout se règle dans le registre, rien dans le code. Les deux limites déclarées (`max_views`, `requests_per_day`) servent aux essais ; aucune édition n'en fixe.

## 5. La licence

```json
{
  "format": "xpg-license", "version": 1,
  "license_id": "XPG-2026-00042",
  "customer": "Entreprise ABC",
  "edition": "PROFESSIONAL",
  "type": "temporary",
  "service": null,
  "max_devices": 5,
  "features": [ { "id": "ai_generation", "limits": { "requests_per_day": 200 } }, { "id": "api_editor" } ],
  "issued_at": "2026-04-01T08:00:00Z",
  "expires_at": "2027-03-31T00:00:00Z",
  "offline_grace_days": 14,
  "devices": [ { "key": "<clé publique du PC, 64 hexadécimaux>", "name": "Poste bureau d'études", "activated_at": "2026-04-01T08:00:00Z" } ],
  "manifest": 318,
  "nonce": "<32 hexadécimaux>"
}
```

| Champ | Sens |
|---|---|
| `type` | `perpetual` (sans fin), `temporary` (jusqu'à `expires_at`, puis la grâce), `evaluation` (jusqu'à `expires_at`, **sans** grâce), `floating` (**prévu**, refusé en version 1) |
| `service` | **prévu** : le service en ligne qui tiendrait la licence ; `null` en version 1 (sinon refusée) |
| `features` | les fonctionnalités ouvertes et leurs limites (pas de limite = pas de plafond) |
| `devices` | les PC liés : leur **clé publique** et le nom que le client leur a donné (jamais leur nom réseau) ; au plus `max_devices` |
| `offline_grace_days` | en version 1 (sans service) : les jours après `expires_at` où les fonctionnalités restent ouvertes, le temps de recevoir et d'importer le renouvellement par fichier. Avec le service, il bornera aussi le temps sans synchronisation |
| `manifest` | le numéro du registre en vigueur à l'émission |
| `nonce` | du hasard : deux émissions ne se ressemblent jamais |

**Plusieurs PC** : une licence liste ses PC. Pour en ajouter un, l'Admin ré-émet la licence (même identifiant) avec l'ancienne et la nouvelle demande (`--licence ancienne.xpglic --demande nouveau.xpgreq`) ; les PC déjà liés gardent leur fichier, le nouveau reçoit le sien. L'Admin refuse au-delà de `max_devices`. Un PC n'accepte pas une émission **plus ancienne** de la licence qu'il a déjà.

## 6. Les révocations

```json
{ "format": "xpg-revocations", "version": 1, "number": 19, "issued_at": "…",
  "revoked": [ { "license_id": "XPG-2026-00047", "revoked_at": "…", "reason": "Fin de contrat" },
               { "license_id": "XPG-2026-00044", "device": "<clé du PC>", "revoked_at": "…", "reason": "Transfert" } ] }
```

Numérotée et signée ; un client refuse une liste plus ancienne que la plus récente vue. Avec `device` : la licence n'est retirée que pour ce PC (un transfert). Une liste déjà vue qui **disparaît** du disque, ou qui est remplacée par une plus ancienne : la licence devient Invalide (pas de retour à Valide). Sans service, un PC n'apprend une révocation qu'en important un fichier qui la porte (une licence renouvelée, ou un envoi exprès). 1.35.0 : une licence EN LIGNE révoquée revient dans la réponse du renouvellement (`retirees`, raison `revoquee`) et ce PC la retire - la liste entière n'est plus envoyée ; une licence liée à des PC reçoit la liste par l'état de son poste, scellée par l'éditeur pour ce seul PC.

## 7. L'identité du PC et l'activation par fichier

1. **Sur le PC client** (License Manager, 1.30.0) : « Demander une activation ». Au premier besoin, une **paire Ed25519** est créée sur ce PC (`identite-pc.cle` ; clé secrète sous **DPAPI machine** : illisible en ouvrant le fichier, mais tout programme de ce PC peut la déchiffrer et la recopier ; 1.32.0 : la licence porte l'empreinte salée de l'installation de Windows, `station`, voir § 7 bis). La demande (`.xpgreq`) porte la **clé publique**, un **nom choisi** par l'utilisateur (facultatif), la licence demandée (facultatif), le logiciel ; elle est **signée par la clé du PC** (preuve qu'il la détient). Rien du matériel : ni nom réseau, ni adresse MAC, ni numéro de série.
2. **Sur le poste Admin** : `XPG-License-Admin signer-licence --parametres P.json --demande PC.xpgreq --registre R.xpgreg [--revocations V.xpgrev] --sortie L.xpglic`. L'outil vérifie la signature de la demande, l'édition, les fonctionnalités, le nombre de PC, la liste de révocation.
3. **Sur le PC client** : « Activer par fichier de licence » → `importLicenseFile`. Le registre et les révocations passent d'abord ; la licence n'est installée que si elle vaut **pour ce PC, maintenant** (signée par l'éditeur, liée à la clé de ce PC, pas révoquée, pas finie, horloge en ordre, pas plus ancienne que celle installée). Sinon rien ne change et la raison est dite.

**Le dossier** (`LicenseStore::defaultFolder()` : `%ProgramData%\XPG\Licences` sous Windows ; ailleurs `$XDG_DATA_HOME/xpg/licences` ou `~/.local/share/xpg/licences`) : `identite-pc.cle`, `licence.xpglic`, `registre.xpgreg`, `revocations.xpgrev`, `etat.bin` (protégé par DPAPI machine : l'heure la plus tardive vue, les numéros les plus hauts vus), `historique.jsonl` (1.30.0, § 9 bis). Copier ce dossier tel quel sur un autre PC ne sert à rien (DPAPI) ; une clé extraite puis recopiée donne « Autre PC » (l'empreinte du poste, 1.32.0).

**Qui peut y écrire** (1.30.0) : l'installation **pour tous les comptes** crée le dossier et donne au groupe Utilisateurs le droit Modifier, hérité (§ 13 bis) : chaque compte importe une licence et écrit la demande de ce PC. L'installation **pour toi seul** ne fait rien : le **repli** est le même chemin, que le programme crée au premier besoin (une demande, un import, la première évaluation) ; Windows en donne alors la propriété au compte qui l'a créé, et un **autre compte** de ce PC le lit sans pouvoir y écrire. Pour ce compte-là, l'évaluation marche quand même (elle ne fait que lire : la licence vaut, le registre et l'horloge sont vérifiés) ; `detail` le dit (« ce compte ne peut pas écrire dans le dossier des licences de ce PC (…) : l'état protégé ne s'écrit pas (…), l'historique ne s'écrit pas (…) ; une installation pour tous les comptes donne ce droit à chaque compte ») ; l'import et la demande sont refusés avec la même raison.

**Honnêtement** : DPAPI machine empêche de lire la clé en ouvrant le fichier, pas de l'extraire : tout programme de ce PC peut la déchiffrer. L'empreinte du poste (§ 7 bis) est un fil d'alarme, pas une protection forte (elle tombe si on recopie aussi l'identifiant de l'installation de Windows). Sous Linux, pas de DPAPI : fichiers lisibles par leur seul propriétaire (0600), et c'est écrit dans le fichier (`protection=fichier`).

### 7 bis. La sécurité des licences, revue en 1.32.0

Une relecture de sécurité a confirmé que la cryptographie tient (signature vérifiée avant toute lecture du JSON, genre du document dans le message signé, identifiants de clé recalculés, comparaisons à temps constant) ; elle a trouvé des faiblesses autour, corrigées :
- **L'empreinte du poste** (`station`) : la demande d'activation porte `hex(SHA-256("xpg-poste/1" ‖ MachineGuid ‖ clé publique du PC))`, 32 caractères (Linux : `/etc/machine-id`) ; l'Admin la recopie dans le PC lié de la licence. Une clé de PC recopiée sur une autre installation de Windows donne « Autre PC ». Salée par la clé du PC, elle ne relie pas deux licences entre elles et ne dit rien du matériel. Absente (demandes et licences d'avant) : pas vérifiée.
- **Révoquer une émission précise** : une révocation peut porter `issued_before` (une date) : les émissions de cette licence signées avant cette date sont révoquées. Et `etat.bin` garde, par licence, la date d'émission la plus récente installée : remettre une ancienne `.xpglic` sur le disque (après un renouvellement moins généreux) la rend Invalide, et l'importer est refusé.
- **L'état protégé perdu** : une licence présente sans `etat.bin` lisible n'est plus remise à zéro en silence : le plancher de l'horloge est reconstruit sur la date la plus récente du dossier (fichiers, émissions, historique), et l'historique le dit (« État recréé »).
- **Une liste de révocation livrée** avec les programmes (`<programme>\licences\revocations.xpgrev`, depuis `ressources-licences/revocations.xpgrev` à la construction) est adoptée si elle est plus récente, comme le registre ; sa date relève le plancher de l'horloge.
- **`XPG_LICENCES_DOSSIER`** (un autre dossier des licences, par variable d'environnement) n'existe plus que dans une construction avec la clé d'essai (`XPG_LICENCES_CLE_ESSAI`) : un utilisateur ne peut plus faire tourner le programme sur un dossier à lui.
- **Les verrous à la source** : en plus des entrées de menu, la fonctionnalité est vérifiée là où le travail se fait : le démarrage de la simulation de l'IHM (toutes ses entrées : sa barre, le clic droit, F8, une vue choisie, « Les deux »), la validation d'un import de vues (un paquet glissé ou passé en ligne de commande compris), les commandes de script `poste` et `simulation-barre hmi.start`. `licence::start()` est fait avant tout (ouverture d'un projet, poste, scripts). Le poste d'exploitation (`--ihm`) n'est toujours **jamais** arrêté par la licence du PC.
- **Le dossier des licences** : l'installateur le protège contre une jonction (§ 13 bis).
- **L'outil de l'éditeur** : `liste --csv` neutralise une cellule qui commencerait par `= + - @` (une formule Excel venue d'une demande) et une demande dont le nom de PC commence ainsi est refusée ; jusqu'à la 1.34.0, le jeton GitHub ne partait que vers `https://api.github.com` (ou la boucle locale, pour les essais) ; depuis la 1.35.0 il n'y a plus de jeton : chaque appel au service des licences est signé par la clé de l'éditeur.

**Compatibilité** : les nouveaux champs (`station`, `issued_before`) sont facultatifs et écrits seulement quand ils servent ; un client 1.30/1.31 refuse un document qui les porte (champ inconnu). Sans conséquence aujourd'hui : aucun programme livré n'a encore la clé de l'éditeur. Il faut l'outil de l'éditeur 1.32.0 pour lire une demande 1.32.0.

## 8. Les états

| État | Quand | Fonctionnalités | La phrase (exemples réels de l'essai) |
|---|---|---|---|
| **Valide** | tout est en ordre | celles de la licence | « Licence PROFESSIONAL valide jusqu'au 2027-11-06. » |
| **Grâce** | après `expires_at`, pendant `offline_grace_days` (pas pour une évaluation) | celles de la licence | « Ta licence PROFESSIONAL a pris fin le 2027-11-06 : ses fonctionnalités restent ouvertes encore 11 jours, le temps d'importer son renouvellement. Ensuite, l'édition FREE s'appliquera ; tes projets restent ouverts et enregistrables. » |
| **Expirée** | au-delà de la grâce | FREE | « Ta licence PROFESSIONAL a pris fin le 2027-11-06 : l'édition FREE s'applique. Ton projet reste ouvert ; Enregistrer et Exporter restent permis. Pour retrouver tes fonctionnalités, importe le renouvellement reçu de l'éditeur. » |
| **Révoquée** | dans la liste de révocation (toute la licence, ou ce PC) | FREE | « Ta licence XPG-… a été révoquée par l'éditeur le … (motif : Fin de contrat) : l'édition FREE s'applique. … » |
| **Invalide** | signature fausse ou clé inconnue, fichier tronqué, champ en double ou inconnu, édition ou fonctionnalité inconnue du registre, registre ou liste de révocation remplacés par des plus anciens ; 1.35.0 : une licence en ligne qui lie aussi un PC, une flottante sans service, l'activation d'une licence en ligne abîmée ou mal signée | FREE | « Le fichier de licence de ce PC n'est pas valide : l'édition FREE s'applique. … Demande un nouveau fichier à l'éditeur. » (+ `detail` : la raison précise) |
| **Autre PC** | la licence n'est pas liée à la clé de ce PC | FREE | « Cette licence (…) a été délivrée pour un autre PC : … Envoie la demande d'activation de ce PC à l'éditeur pour recevoir la tienne. » |
| **Absente** | aucune licence | FREE | « Aucune licence sur ce PC : l'édition FREE s'applique. » |
| **Horloge reculée** | licence datée, et l'heure du PC est en arrière de plus de 24 h sur l'heure la plus tardive vue | FREE | « L'horloge de ce PC indique le 2027-10-01, alors que le 2027-11-21 a déjà été vu : remets-la à l'heure. En attendant, l'édition FREE s'applique. … » |
| **Découverte** (1.34.0 ; nommée « Évaluation » jusqu'en 1.34) | aucune licence, licences actives, une édition d'évaluation au registre : pendant sa durée, depuis le premier lancement (§ 8 ter) | celles de l'édition d'évaluation | « Découverte : il te reste 18 jours (jusqu'au 07/12/2026). Pour continuer ensuite, demande une licence (menu Licence…). » |
| **Bail expiré** (1.35.0) | une licence en ligne sans activation valable sur ce PC : le bail fini à l'heure retenue, le certificat du service expiré, l'activation absente (§ 12 bis) | FREE | « Connecte ce PC à Internet pour renouveler sa licence (bail fini le 07/12/2026). En attendant, l'édition FREE s'applique. Ton projet reste ouvert ; Enregistrer et Exporter restent permis. » |
| **Flottante : aucun poste** (1.35.0) | une licence flottante activée, sans jeton valable (§ 12 bis) | FREE | « Licence flottante PROFESSIONAL : ce PC n'a pas de poste en ce moment. Connecte-le à Internet pour en prendre un ; en attendant, l'édition FREE s'applique. … » |

**L'horloge** : l'heure la plus tardive vue est gardée dans `etat.bin` ; les dates d'émission des documents signés (licence, registre, révocations) servent aussi d'ancrage. Une licence perpétuelle ne dépend pas de l'heure. **Honnêtement** : un PC sans réseau ne peut pas prouver l'heure ; ce contrôle arrête le retour en arrière ordinaire de l'horloge, pas quelqu'un qui efface l'état **et** remet une horloge postérieure aux dates des documents. Le vrai garde-temps viendra avec le service (une heure signée).

Si le registre manque (il doit être livré avec le programme, § 13), il n'y a pas d'édition FREE : `getEdition()` rend `""` et rien n'est ouvert ; la phrase le dit.

### 8 bis. Non activées (1.30.0) : un programme sans clé de l'éditeur

Tant que la vraie clé de l'éditeur n'est pas compilée (`PublisherKey.hpp` vide, et pas l'option de la clé d'essai), le programme **ne peut rien vérifier**. Il ne bloque alors **jamais** personne :

| | |
|---|---|
| État | `LicenseStatus::NotConfigured` — clé `non-configuree`, étiquette « Non activées » ; `licensing::isConfigured()` rend faux |
| Fonctionnalités | **toutes ouvertes** : `isFeatureEnabled` rend toujours vrai (même pour un identifiant inconnu) ; `LicenseState::hasFeature` aussi ; `allFeaturesOpen(status)` le dit |
| Édition | `getEdition()` rend `""` ; `getRegistry()` ne rend rien ; `getLicensedDevices()` est vide |
| La phrase | « Les licences ne sont pas encore actives dans ce programme : il ne contient pas encore la clé de l'éditeur. Toutes les fonctionnalités sont ouvertes. » |
| Import | refusé : « Ce programme ne peut pas encore vérifier une licence : il ne contient pas la clé de l'éditeur ; mets-le à jour. » (noté dans l'historique) |
| Demande d'activation | **marche** (elle ne dépend pas de la clé de l'éditeur) : le PC peut déjà envoyer sa demande |
| Fichiers | l'évaluation n'écrit rien (pas même le dossier) ; seule une demande ou un import tenté écrit (la clé du PC, l'historique) |
| `lockedMessage(id)` | « Cette fonctionnalité (id) est ouverte : les licences ne sont pas encore actives dans ce programme. » |

### 8 ter. La Découverte (1.34.0 : « Évaluation », renommée en 1.35.0)

**1.35.0** : pour l'utilisateur, l'évaluation de ce PC s'appelle la **Découverte** (l'étiquette, les phrases, l'historique, `lockedMessage`). Dans le code, l'état reste `LicenseStatus::Evaluation`, sa clé `evaluation`, les champs `evaluation*` et le genre `evaluation` des lignes de l'historique (compatibles avec la 1.34). Le service en ligne garde aussi son début (§ 12 bis) : le plus ancien gagne. Le tableau ci-dessous garde les phrases de la 1.34 ; en 1.35.0, « Évaluation » y devient « Découverte » (« Découverte : il te reste… », « Ta Découverte de 30 jours est finie depuis le… », « Découverte commencée : 30 jours… »).

Un Studio installé, licences actives (une clé de l'éditeur), **sans fichier de licence** : la période d'évaluation de ce PC commence au premier lancement qui le constate, si le registre accepté a une **édition d'évaluation** (type `evaluation`, pas retirée ; `EVALUATION` d'abord, sinon la première). Elle dure son `duration_days` (0 : 30 jours). Une notification la rappelle à chaque ouverture, avec les jours qui restent.

| | |
|---|---|
| État | `LicenseStatus::Evaluation` — clé `evaluation`, étiquette « Évaluation » ; les fonctionnalités, les limites et `maxDevices` de l'édition d'évaluation ; `getExpirationDate()` rend sa fin |
| `LicenseState` | `evaluationStartedAt`, `evaluationEndsAt`, `evaluationDaysLeft` (arrondi au jour supérieur), `evaluationOver`, `evaluationEdition` |
| La phrase | « Évaluation : il te reste 18 jours (jusqu'au 07/12/2026). Pour continuer ensuite, demande une licence (menu Licence…). » |
| Après | **Absente** (FREE) et `evaluationOver` : « Ton évaluation de 30 jours est finie depuis le 07/12/2026 : demande une licence (menu Licence…). En attendant, l'édition FREE s'applique. » ; une ligne `etat` « Évaluation → Absente » dans l'historique |
| Horloge | reculée de plus de 24 h pendant l'évaluation : **Horloge reculée** (FREE), comme une licence datée ; avant son début, elle ne commence pas tant que l'horloge est en arrière |
| Licence | une licence installée passe avant (les champs `evaluation*` restent vides) ; retirée, l'évaluation d'avant reprend avec ses dates — **jamais une nouvelle** ; expirée ou révoquée : l'état d'avant (Expirée, Révoquée) ; un PC qui a déjà eu une licence (notée dans `etat.bin`) n'en reçoit pas |
| `lockedMessage(id)` | « Cette fonctionnalité (multi_user) n'est pas incluse dans l'évaluation (EVALUATION) : demande une licence qui la contient (menu Licence…). Ton projet reste ouvert ; … » |

**Son début est gardé en plusieurs copies** : `etat.bin` (deux champs de plus, `evaluation_started` et `evaluation_days`, que la 1.33 ignore ; un `etat.bin` d'avant se lit), `evaluation.bin` (protégé comme lui, DPAPI machine ; il garde aussi l'heure la plus tardive vue), une ligne `evaluation` de l'historique (« Évaluation commencée : 30 jours, jusqu'au 07/12/2026. », avec les champs `started` et `days`) et, sous Windows et pour le dossier par défaut seulement, la valeur `evaluation` de `HKCU\Software\XPG\Licences` (elle survit à une désinstallation). Le début **le plus ancien** de toutes les copies compte, et une copie qui manque est remise : effacer `etat.bin`, `evaluation.bin` ou l'historique (un, ou deux à la fois) ne la fait pas recommencer ; `etat.bin` effacé pendant l'évaluation est recréé comme avec une licence (§ 7 bis), une ligne `controle` le dit. **Jamais prolongée** : les jours notés à son début comptent ; un registre qui en donne moins la raccourcit, un registre qui en donne plus ne change rien.

**L'option** `StoreOptions::evaluationPeriod` : vide, l'évaluation est donnée au programme qui croit ses clés **compilées** (le Studio livré), pas quand l'appelant donne ses propres clés (les essais d'avant la 1.34.0, les outils) ; un essai la met à vrai. Non activées (aucune clé) : rien ne commence, rien n'est écrit. Sans registre, ou sans édition d'évaluation : Absente, comme avant.

**Un dossier où ce compte n'écrit pas** : un début déjà noté (dans n'importe quelle copie) compte ; si aucune copie ne peut le noter, l'évaluation est donnée depuis ce lancement, et `detail` dit qu'elle n'est pas gardée d'un lancement à l'autre (sous Windows, la copie de HKCU s'écrit presque toujours).

**Honnêtement** : quelqu'un qui efface **toutes** les copies (et l'historique) recommence ; ceci arrête la réinstallation et l'effacement d'un fichier, pas davantage — comme pour l'horloge (§ 8).

## 9. L'API (`src/licensing/Licensing.hpp`)

```cpp
licensing::LicenseState licensing::initialize(licensing::StoreOptions options);   // au démarrage
licensing::LicenseState licensing::refresh();                                     // après un import, et une fois par heure
bool                    licensing::isFeatureEnabled(std::string_view featureId);
std::string             licensing::getEdition();          // "PROFESSIONAL" ; sans licence : "FREE" (le registre)
licensing::LicenseStatus licensing::getLicenseStatus();   // Valid, Grace, Expired, Revoked, Invalid, OtherDevice, Absent, ClockRolledBack, NotConfigured, Evaluation (1.34.0)
int                     licensing::getMaxDevices();       // 5
std::string             licensing::getExpirationDate();   // "2027-03-31" ; "" : pas de fin
std::string             licensing::getLicenseId();        // "XPG-2026-00042" ; "" sans licence
std::string             licensing::getStatusMessage();    // la phrase claire de l'état
std::string             licensing::getStatusLabel();      // "Valide", "Grâce", "Expirée", "Révoquée", "Invalide", "Autre PC", "Absente", "Horloge reculée", "Non activées", "Évaluation"
std::optional<std::int64_t> licensing::getFeatureLimit(std::string_view featureId, std::string_view limit);
licensing::LicenseState licensing::getLicenseState();     // tout, pour l'écran Licence
std::string             licensing::lockedMessage(std::string_view featureId);   // la phrase d'une entrée verrouillée
licensing::ImportReport licensing::importLicenseFile(const std::string& path);
bool licensing::writeActivationRequest(const std::string& path, const std::string& deviceName,
                                       const std::string& licenseId, const std::string& software, std::string* why);
// 1.30.0
bool                    licensing::isConfigured();         // faux : aucune clé de l'éditeur (Non activées, tout ouvert)
std::optional<licensing::RegistryView> licensing::getRegistry();          // le registre en vigueur ; rien : pas de registre
std::vector<licensing::DeviceView>     licensing::getLicensedDevices();   // les PC liés dans la licence installée
std::vector<licensing::HistoryEvent>   licensing::getHistory(int limit);  // le plus récent d'abord ; limit <= 0 : tout
std::string             licensing::getLicenseFolder();     // le dossier des licences de ce PC
std::string             licensing::thisDeviceFingerprint(); // lisible ; "" : la clé du PC n'existe pas encore
```

`StoreOptions` : `folder` (le dossier, § 7), `bundledRegistry` (le registre livré avec le programme : adopté s'il est plus récent ; l'installateur le pose dans `{app}\licences\registre.xpgreg`), `trustedKeys` (vide : `compiledPublisherKeys()` — les vraies clés, plus la clé d'essai avec l'option ; les essais y mettent leur clé), `compiledKeys` (1.30.0 ; faux : seulement `trustedKeys` — un essai simule ainsi un programme sans clé), `clock` (vide : l'heure du système), `clockTolerance` (24 h). Les lectures ne touchent aucun fichier (le dernier état, sous verrou : appelables de n'importe quel fil). Avant `initialize()` : Absente, rien d'ouvert.

`LicenseState` porte tout ce qu'il faut pour afficher : `status`, `edition` et `editionLabel` (en vigueur), `licensedEdition` (celle de la licence, même finie), `licenseId`, `customer`, `type`, `maxDevices`, `boundDevices`, `issuedAt`, `expiresAt`, `graceEndsAt`, `graceDaysLeft`, `features` (ouvertes, avec leurs limites), `registryNumber`, `revocationNumber`, `deviceFingerprint`, `message`, `detail`, `registryProblem`.

**Ce que lit l'écran Licence** (1.30.0) — comme le reste, sous verrou et sans relire de fichier : ce que la dernière évaluation a lu (`initialize`, `refresh`, un import, une demande) est gardé ; l'historique est relu à ces moments-là. Les dates sont écrites `AAAA-MM-JJTHH:MM:SSZ`.

```cpp
struct RegistryFeatureView { std::string id, product, label; bool retired; std::vector<std::pair<std::string, std::string>> limits; /* id, libellé */ };
struct RegistryEditionView { std::string id, label, type; /* "perpetual"… */ int maxDevices, durationDays, graceDays;
                             std::vector<std::string> features; /* triées */ bool retired; };
struct RegistryView        { std::uint64_t number; std::string issuedAt, unlicensedEdition;
                             std::vector<RegistryFeatureView> features; std::vector<RegistryEditionView> editions; };
struct DeviceView          { std::string name, fingerprint /* 4F:9A:12:34:…:AB:CD:EF:01 */, activatedAt; bool thisPc; };
struct HistoryEvent        { std::int64_t at; std::string kind; /* "import", "demande", "etat" */ bool ok; std::string text; };
```

- `getRegistry()` : le registre **accepté** à la dernière évaluation (signé par une clé connue, pas plus ancien que le plus récent vu) ; rien s'il manque, s'il est refusé, ou en Non activées.
- `getLicensedDevices()` : les PC de la licence installée **lisible** (signée par une clé connue) — aussi quand elle est finie, révoquée ou d'un autre PC ; `thisPc` : la clé de ce PC. Vide sans licence lisible.
- `thisDeviceFingerprint()` : l'empreinte courte de la clé de ce PC (la même forme que `DeviceView::fingerprint`) ; vide tant que la première demande ne l'a pas créée.

### 9 bis. L'historique de ce PC (1.30.0)

`historique.jsonl`, dans le dossier des licences : une ligne JSON par événement, que le magasin (`LicenseStore`) écrit lui-même :

| `kind` | Quand | `ok` | `text` (exemples réels de l'essai) |
|---|---|---|---|
| `import` | chaque import, accepté ou refusé | accepté | « Registre n° 318 : installé. Licence XPG-… (PROFESSIONAL, temporaire, jusqu'au 2027-11-06) : installée sur ce PC. » ; « Refusé : Ce programme ne peut pas encore vérifier une licence… » |
| `demande` | chaque demande d'activation écrite (ou pas) | écrite | « Demande d'activation écrite (pc.xpgreq, PC « Poste essai 1 », licence XPG-…) : à envoyer à l'éditeur. » |
| `etat` | un **changement d'état constaté** par une évaluation (`refresh`, un import…) ; la première évaluation sur ce PC note l'état de départ | l'état est sans anomalie (Valide, Grâce, Absente, Non activées) | « Valide → Grâce : Ta licence PROFESSIONAL a pris fin le … » |

Exemple de ligne : `{"at":"2027-11-09T00:00:00Z","kind":"etat","ok":true,"status":"grace","text":"Valide → Grâce : …"}`. **500 lignes au plus** : les plus anciennes partent ; une ligne illisible est ignorée (et ne se recopie pas) ; un champ inconnu (une version plus récente) n'empêche pas de lire la ligne. Un changement d'état n'est noté qu'une fois, même si deux programmes de ce PC (API Studio et IHM Studio) le constatent : le dernier état noté est relu dans le fichier. Pendant un import, la ligne d'état vient après celle de l'import (la cause, puis l'effet). En Non activées, l'évaluation n'écrit rien. Un dossier où ce compte n'écrit pas : rien n'est noté, `detail` le dit (§ 7).

## 10. Jamais de perte de travail (la règle du branchement des menus, 1.30.0)

- **Ouvrir, Enregistrer, Enregistrer sous, Exporter, Fermer, Quitter** ne portent **aucun** identifiant de fonctionnalité et ne demandent **jamais** `isFeatureEnabled`, quel que soit l'état (Expirée, Révoquée, Invalide, Autre PC, Absente, Horloge reculée). Les exports « avancés » éventuels (`advanced_export`) sont des entrées à part ; l'export ordinaire d'un projet reste toujours permis.
- Une licence qui prend fin pendant le travail **ne ferme rien** : le projet ouvert reste ouvert ; seules les **entrées** des fonctionnalités se ferment, au `refresh()` suivant, avec `lockedMessage(id)`.
- Une fonctionnalité fermée en cours d'usage (une génération IA lancée…) va à son terme ; c'est la suivante qui est refusée.
- Chaque entrée verrouillée garde sa place dans le menu, avec le cadenas « pas dans ta licence » (maquette) et la phrase ; jamais une entrée qui disparaît.
- 1.39.0 : **barrée**. Dans les menus (la barre du haut, les menus du clic droit de l'explorateur), l'arbre du projet (et les boutons de la rangée des outils), la palette Aller à / Faire (Ctrl+K), une entrée dont la fonctionnalité n'est pas dans la licence a son libellé barré, un cadenas et « pas dans ta licence ». Elle reste choisissable : le clic va au contrôle (`licence::gate`), qui ouvre la fenêtre **« Ta licence ne permet pas cette action »** : l'action, la fonctionnalité qu'elle demande (et son logiciel), pourquoi (la Découverte, ta licence X, pas de licence valable), sa capture, les offres qui la contiennent avec leur prix ; Voir les offres… ouvre la boutique. Le test est en ligne dans `Licence.hpp` (`licence::locked`, `lockedAction`) : les menus et les listes se compilent aussi dans des essais sans `Licence.cpp`. L'Assistant IA (ouvrir le panneau, envoyer, créer une mission) passe par le même contrôle.
- Aucun nom de l'outil Admin dans le code des clients (le contrôle du paquet le refuserait).

## 11. L'outil de l'éditeur : `XPG-License-Admin`

```
XPG-License-Admin creer-cle [--essai] [--nom N] [--dossier D]
XPG-License-Admin cle [--dossier D]
XPG-License-Admin exporter-cle --sortie src/licensing/PublisherKey.hpp [--garder] [--dossier D]
                  (une clé d'essai : --sortie src/licensing/TestPublisherKey.hpp)
XPG-License-Admin exporter-cle --sortie src/licensing/PublisherKey.hpp --vide
XPG-License-Admin signer-registre --entree registre.json --sortie registre.xpgreg [--dossier D]
XPG-License-Admin signer-revocations --entree revocations.json --sortie revocations.xpgrev [--dossier D]
XPG-License-Admin signer-licence --parametres P.json --demande PC.xpgreq [--demande …] [--licence ANCIENNE.xpglic]
                  --registre registre.xpgreg [--revocations revocations.xpgrev] --sortie L.xpglic [--dossier D]
XPG-License-Admin verifier FICHIER [--dossier D]
XPG-License-Admin enregistrer-demande PC.xpgreq [--dossier D]                              (1.30.0)
XPG-License-Admin liste [--licence ID] [--client TEXTE] [--csv F.csv] [--dossier D]         (1.30.0)
XPG-License-Admin aide | --version
```

Code de sortie : 0 fait, 1 refusé (la raison sur la sortie d'erreur), 2 mauvais usage. Le dossier par défaut de la clé : `%LOCALAPPDATA%\XPG\LicenseAdmin` (sous Linux `~/.config/xpg-license-admin`).

- **La clé de l'éditeur** (`cle-editeur-licences.cle`) : Ed25519, la partie secrète protégée par **DPAPI pour ton compte Windows** ; elle ne quitte jamais le poste. `creer-cle` refuse d'en écraser une. Une clé qui ne se relit pas n'est jamais remplacée en silence.
- **Les paramètres d'une licence** (`P.json`) : `license_id`, `customer`, `edition` ; facultatifs (sinon ceux de l'édition) : `type`, `max_devices`, `features` (des identifiants, ou `{"id", "limits"}`), `issued_at`, `expires_at` ou `duration_days`, `offline_grace_days`. Un paramètre inconnu est refusé. Exemple : `tests/fixtures/licences/parametres-exemple.json`.
- **Le registre à signer** est le JSON du § 4 sans `format`, `version`, `issued_at` (ajoutés). Le numéro (`number`) est à augmenter à chaque publication : l'outil ne garde pas d'historique.
- **La clé d'ESSAI** : `creer-cle --essai` la nomme « CLE D'ESSAI XPG-CLE-ESSAI-LICENCES - NE PAS LIVRER » ; elle ne s'exporte que vers `TestPublisherKey.hpp` (`exporter-cle` refuse de l'écrire dans un fichier nommé `PublisherKey.hpp`, et une vraie clé dans `TestPublisherKey.hpp`). La clé d'essai du dépôt (`xpg-cb0d1ae560b52310`) est dans `TestPublisherKey.hpp` ; sa partie privée n'est pas dans le dépôt.
- **`exporter-cle`** écrit l'un des deux fichiers du § 11 bis, en ASCII : `--garder` garde les clés déjà dans le fichier de sortie et ajoute celle du poste (une rotation ; elle n'y est jamais deux fois ; une clé d'essai ne se garde pas à côté de vraies clés) ; sans `--garder`, la clé du poste seule — la sortie dit quelles clés sortent du fichier (« retirée : ses licences ne seront plus reconnues ») ; `--vide` écrit `PublisherKey.hpp` sans clé (c'est ainsi qu'est fait celui du dépôt aujourd'hui). Chaque fichier se relit (l'essai vérifie que ceux du dépôt sont octet pour octet ce qu'écrit l'outil, et qu'un octet de clé changé à la main est vu : l'identifiant ne va plus).

**À faire sur ton PC (l'éditeur)** — le pas à pas est dans le LISEZ-MOI livré avec l'outil (§ 11 ter) : `creer-cle --nom "…"` ; garder une **copie chiffrée hors ligne** du fichier de la clé ; `exporter-cle --sortie PublisherKey.hpp` ; adapter puis `signer-registre --entree registre.json --sortie registre.xpgreg` ; envoyer **uniquement** `PublisherKey.hpp` et `registre.xpgreg` à l'agent de développement (jamais le fichier de la clé). Ils iront dans `src/licensing/PublisherKey.hpp` et `ressources-licences/registre.xpgreg` (§ 13). **Honnêtement**, sur la copie de secours : DPAPI lie le fichier de la clé à ton compte Windows ; la copie se relit sous ton compte, sur ce PC, ou sur un PC où ton compte revient avec ses clés DPAPI (une sauvegarde complète de Windows, un compte de domaine) — d'où, aussi, une image système. Une sauvegarde de la clé protégée par un mot de passe n'existe pas encore.

**Rotation** : `PublisherKey.hpp` peut lister plusieurs clés (`kKeys`) ; les clients croient chacune. Une nouvelle clé s'ajoute à côté de l'ancienne pendant la transition (`exporter-cle --garder --dossier NOUVEAU-DOSSIER`) ; une clé retirée s'enlève (`exporter-cle` sans `--garder` : ses licences tombent).

**Le registre des activations** (1.30.0) : « voir la liste de toutes les personnes qui ont installé le logiciel ». Sans service en ligne, l'éditeur ne connaît que les PC qui lui envoient une demande d'activation ; l'outil les note dans `activations.jsonl`, dans le dossier de la clé (`--dossier`) — une ligne JSON par événement, **ajoutée à la fin**, le fichier n'est **jamais réécrit** :

| `kind` | Écrite par | Ce qu'elle porte |
|---|---|---|
| `licence` | `signer-licence` : une ligne **par PC lié** | `license_id`, `customer`, `edition`, `type`, `expires_at`, `device_name` (le nom donné au PC par le client), `device_fingerprint` (l'empreinte courte de sa clé), `device_key`, `software` (le logiciel et sa version, tels que la demande les porte : « IHM Studio 1.30.0 »), `requested_at` (la date de la demande), `activated_at`, `signed_at`, `extension` (vrai si `--licence` étendait une licence existante ; un PC gardé de l'ancienne licence n'a pas de demande : son logiciel et sa date restent ceux déjà notés) |
| `demande` | `enregistrer-demande PC.xpgreq` : une demande reçue, **pas encore signée** (sa signature par la clé du PC est vérifiée) | le nom du PC, son empreinte, la licence demandée, le logiciel, la date |
| `revocation` | `signer-revocations` : une ligne par licence (ou PC) révoqué | `license_id`, `device_key` (vide : toute la licence), `revoked_at`, `reason` |

`liste [--licence ID] [--client TEXTE] [--csv F.csv]` : tous les PC connus, **un par ligne**, le dernier état de chacun (par la clé du PC) — PC (le nom donné), empreinte, client, licence, édition, logiciel et version, demandé le, activé le, expire le, état (`demande`, `activé`, `révoqué`, `expiré` à la date du jour) ; en bas, les totaux (PC, licences, clients ; et par état). `--licence` garde une licence ; `--client` les clients dont le nom contient le texte (sans tenir compte de la casse). `--csv` écrit le même tableau pour Excel : UTF-8 avec BOM, séparateur `;`, fins de ligne CRLF, un champ qui contient `;` ou `"` entre guillemets. Une ligne illisible du registre est ignorée, et la commande le dit.

**Rien du matériel** : la demande n'en porte pas (§ 7) ; l'Admin ne voit **que ce que les clients lui envoient**. Un PC qui a installé le logiciel sans jamais demander d'activation n'y est pas — le voir demande le service en ligne (§ 12).

### 11 bis. Deux fichiers de clés, et l'option CMake `XPG_LICENCES_CLE_ESSAI` (1.30.0)

| Fichier | Contient | Compilé |
|---|---|---|
| `src/licensing/PublisherKey.hpp` | les **vraies** clés publiques (`std::array<KeyEntry, N> kKeys`) — **aucune aujourd'hui** : l'en-tête le dit (« aucune clé : les licences ne sont pas encore actives dans ce programme ; crée la clé sur le poste Admin, puis exporter-cle vers ce fichier ») | toujours |
| `src/licensing/TestPublisherKey.hpp` | la clé d'**essai** (`kTestKeys`, `xpg-cb0d1ae560b52310`, marquée `XPG-CLE-ESSAI-LICENCES`) | **seulement** si la macro `XPG_LICENCES_CLE_ESSAI` est définie : sinon le fichier est vide pour le compilateur |

`compiledPublisherKeys()` rend les vraies clés, plus la clé d'essai avec la macro ; `testKeyCompiled()` dit si elle l'est. Une liste vide compile (`std::array<KeyEntry, 0>`) : c'est l'état **Non activées** (§ 8 bis).

**L'option CMake** `XPG_LICENCES_CLE_ESSAI` (OFF par défaut) définit la macro pour `xpg_licensing` **seulement** (PRIVATE : seul `Store.cpp` la lit ; aucun en-tête public n'en dépend). La basculer ne recompile que les 6 objets de `xpg_licensing`, puis refait les liens de ce qui le lie (vérifié sur les commandes de compilation : 1209 commandes, 6 changent). CMake le dit par un avertissement. **Une construction de captures** avec la clé d'essai :

```
cmake -S . -B build-mingw -DXPG_LICENCES_CLE_ESSAI=ON
ninja -C build-mingw xpg_ihm            # seuls xpg_licensing et les liens bougent
# … copier IHM-Studio.exe à part (captures, essais avec des licences signées par la clé d'essai) …
cmake -S . -B build-mingw -DXPG_LICENCES_CLE_ESSAI=OFF
```

Une construction **avec** l'option n'est **jamais livrée** : le contrôle du paquet trouve la marque de la clé d'essai dans l'exe et s'arrête. **Sans** l'option, la marque n'est dans aucun binaire : l'essai le vérifie sur `libxpg_licensing.a` (ce que lient les clients ; ni la marque ni l'identifiant de la clé d'essai), et `package_mingw.py --controler` passe sur la bibliothèque MinGW.

### 11 ter. L'outil livré à l'éditeur (1.30.0)

`outils/mingw/admin_editeur.sh [DOSSIER]` construit **la seule cible** `xpg_license_admin` dans `build-mingw` (une console : `-mconsole` ; le script le vérifie, et vérifie la marque de l'outil), puis prépare, **hors de `dist/`** (il refuse un dossier dans `dist/`) :

```
DOSSIER/editeur-<version>/XPG-License-Admin.exe
                         /LISEZ-MOI-EDITEUR.txt     le pas à pas, en français (UTF-8 avec BOM, CRLF) : outils/mingw/LISEZ-MOI-EDITEUR.txt
                         /registre.json             le registre d'exemple à adapter et à signer (registre-exemple.json)
                         /parametres-exemple.json
DOSSIER/XPG-License-Admin-<version>-EDITEUR.zip    le même dossier, zippé
```

Par défaut `DOSSIER` = `dist-editeur/` à la racine ; la version vient de `outils/config.ini` (`XPG_VERSION=1.30.0` la remplace) ; `XPG_NINJA_LOCK=<fichier>` partage un verrou de construction. Le LISEZ-MOI explique, sur le PC de l'éditeur : `creer-cle --nom "…"`, la copie de secours chiffrée hors ligne (et sa limite DPAPI), `exporter-cle --sortie PublisherKey.hpp`, `signer-registre` (changer d'abord les éditions et le numéro), l'envoi de **seulement** `PublisherKey.hpp` et `registre.xpgreg` à l'agent de développement ; puis, pour chaque client : recevoir son `.xpgreq` (License Manager > Demander une activation), `signer-licence`, lui renvoyer le `.xpglic` (Activer par fichier de licence) ; un PC de plus (`--licence`), les révocations, le registre des activations (`enregistrer-demande`, `liste`, `--csv`), la rotation.

Cet outil n'est **jamais** copié par `cross_mingw.sh` ni `package_mingw.py` ; le contrôle du paquet le refuserait (son nom, sa marque, même renommé).

### 11 quater. La source centrale : l'archive privée de l'éditeur (1.31.0 ; par le service des licences depuis la 1.35.0)

`src/license_admin/ArchiveSync.{hpp,cpp}`, dans l'outil de l'éditeur **seulement** (jamais dans les Studios). De la 1.31.0 à la 1.34.0, l'archive se synchronisait avec un dépôt GitHub privé (`GitHubSync`, les commandes `github-*`) ; depuis la 1.35.0, elle passe par le **service des licences** de l'éditeur (routes `/admin/archive`, [SERVICE-LICENCES.md](SERVICE-LICENCES.md)), avec les **mêmes** fichiers chiffrés et les **mêmes** règles de fusion. Chaque appel est signé par la clé de l'éditeur : il n'y a plus de jeton.

**L'archive du poste** (`<dossier de la clé>/archive/`) : `registre/registre.xpgreg`, `revocations/revocations.xpgrev`, `licences/<ID>.xpglic` — le dernier de chacun (le plus grand numéro ; pour une licence, la plus récente). `signer-registre`, `signer-revocations` et `signer-licence` y rangent ce qu'ils signent ; `archiver FICHIER...` y range les fichiers signés avant la 1.31.0 (signature vérifiée par la clé du poste ou une clé compilée ; une demande est refusée). Le registre des activations reste `activations.jsonl`.

**Sur le poste**, à côté de la clé : `service.json` (l'adresse du service) ; `cle-synchro.cle` (32 octets tirés sur ce poste, DPAPI) ; `archive-vu.json` (la dernière synchro et le plus grand compteur vu, § 11 sexies). Le **code de secours** (`XPGS1-` puis 11 groupes de 5, base32 de Crockford, la clé et 2 octets de contrôle ; minuscules, O/0 et I/L/1 tolérés) redonne la clé de synchro sur un autre poste ; une **autre** clé déjà là n'est jamais remplacée.

**Dans l'archive en ligne** :

| Fichier | En clair ? |
|---|---|
| `registre/registre.xpgreg` | oui : signé, aucun client nommé |
| `licences/<ID>.xpglic.chiffre`, `revocations/revocations.xpgrev.chiffre`, `activations/activations.jsonl.chiffre` | **non** (la liste de révocation aussi : sa raison est un texte libre, qui peut nommer un client) : XChaCha20-Poly1305 (libsodium), une clé tirée de la clé de synchro (`crypto_kdf`), le **chemin** lié au chiffré (un fichier déplacé ne se déchiffre plus) |
| `index.xpgsync` | la liste des fichiers : genre, empreinte SHA-256 du contenu **en clair** et du texte déposé, numéro ou date, identifiant de licence, nombre de lignes, un compteur ; authentifiée (BLAKE2b avec une autre clé tirée de la clé de synchro) |

Jamais : la clé de l'éditeur, la clé ou le code de synchro, un code d'activation. Les identifiants de licence restent lisibles (chemins, index) : n'y mets pas le nom du client. Un identifiant ne donne jamais `..` dans un chemin (un point qui suit un point devient `_`).

**Une synchro** (`archive-synchroniser`, ou la page Service en ligne de l'application) :
1. lire l'archive en ligne (`GET /admin/archive` : sa version et ses fichiers) et son index ; un index d'une autre clé de synchro, ou dont le code d'authentification est faux, arrête tout ;
2. **recevoir** : pour chaque fichier de l'index, refusé s'il a été changé hors de l'outil, s'il ne se déchiffre pas, ou si son empreinte n'est pas celle de l'index ; **pas reçu** s'il est signé par une clé d'éditeur que le poste ne connaît pas (quand il en connaît une) — et sa copie plus ancienne n'est alors jamais publiée par-dessus ; une entrée d'un genre inconnu (une version plus récente de l'outil) est gardée telle quelle ; sinon pris s'il est plus récent (numéro, puis date de signature) ; à égalité avec un contenu différent : le poste garde le sien, et le dit ; le registre des activations : les lignes qui manquent sont **ajoutées à la fin** (jamais réécrit), et `liste` classe les événements par leur heure ;
3. **envoyer** ce que l'archive en ligne n'a pas : `POST /admin/archive` avec la version lue (l'envoi optimiste) ; si un autre poste a envoyé entre-temps (409), l'outil relit, fusionne et recommence (quatre essais au plus) ; ce qui a été reçu reste dit, même après un refus ; le registre des activations n'est envoyé que si le poste a des lignes que l'archive n'a pas (jamais d'aller-retour sans fin).

**La clé de synchro sur un nouveau poste** : `archive-cle` sans `--code-secours` en crée une neuve (et le dit : si l'archive en ligne a déjà été synchronisée, il faut son code) ; `archive-cle --code-secours CODE --remplacer` met la clé du code à la place d'une autre — l'ancienne est gardée à côté (`cle-synchro.cle.ancienne-<date>`), jamais effacée ; un code refusé ne change rien. `archive-code-secours --afficher` remontre le code. `signer-* --publier` rend **3** quand le fichier est signé et rangé mais que la synchro a échoué (ne pas signer de nouveau).

`--recevoir-seulement` (un poste neuf) ; `--a-blanc` dit ce qui serait reçu et envoyé sans rien changer. Hors ligne, clé refusée, service mal réglé : un refus en français, jamais une réussite inventée.

**Le transport** : celui de l'Assistant IA (`app/ai/AiHttp.cpp`, compilé dans l'outil) — WinHTTP sous Windows (les proxys et certificats du poste), libcurl chargée à la demande sous Linux. La console attend la réponse ; l'application lance la synchro sur un autre fil.

**Essayé** : `admin_service_test` (ctest `adminservice`, 1.35.0) — l'outil de l'éditeur contre le **vrai** Worker lancé sur ce poste (sauté sans Node.js ni wrangler) : deux postes Admin, la synchro dans les deux sens, aucun nom de client en clair dans l'archive en ligne, un index modifié à la main refusé.

### 11 quinquies. La sauvegarde de la clé de l'éditeur, par une phrase de passe (1.32.0)

`src/license_admin/KeyBackup.{hpp,cpp}`, dans l'outil de l'éditeur seulement. Le fichier de la clé (`cle-editeur-licences.cle`) est protégé par DPAPI : il ne se relit que sous le compte Windows qui l'a créé. La **sauvegarde** (`.xpgcle`) se relit partout, avec la phrase de passe que l'éditeur choisit et tape lui-même (jamais affichée, jamais écrite) :

```
XPG-SAUVEGARDE-CLE 1
cle=xpg-…
argon2id ops=3 mem=268435456 sel=<32 hexadécimaux>
<base64 : nonce (24 octets) + chiffré XChaCha20-Poly1305>
```

- La clé de chiffrement vient de la phrase par **Argon2id** (libsodium `crypto_pwhash` : 3 passes, 256 Mio) ; les trois lignes d'en-tête sont les **données associées** : changer l'identifiant ou les paramètres rend la sauvegarde illisible. À la relecture, des paramètres plus faibles que le minimum (2 passes, 64 Mio) ou démesurés (plus de 16 passes, plus de 1 Gio) sont refusés.
- Le contenu : la clé secrète, la publique, le nom, essai ou non ; à la restauration, la paire est vérifiée (la secrète porte la publique, signe pour elle, et l'identifiant concorde).
- La phrase : 12 caractères au moins, variés, pas seulement des chiffres.
- `sauvegarder-cle --sortie F.xpgcle` (la phrase deux fois, sans écho), `verifier-sauvegarde F.xpgcle` (rien n'est écrit), `restaurer-cle --entree F.xpgcle` (la clé réécrite sous DPAPI pour ce compte ; une clé déjà là n'est jamais remplacée). `--phrase-variable NOM` lit la phrase dans une variable d'environnement (les essais).
- Le contrôle du paquet refuse un `.xpgcle` et la marque `XPG-SAUVEGARDE-CLE` dans tout dossier à livrer (§ 13).
- Essai : `key_backup_test` (ctest `clesauvegarde`, 19 contrôles) — aller-retour (la même clé, qui signe pareil), phrase fausse, identifiant ou chiffré modifiés, paramètres affaiblis ou démesurés, une clé déjà là, une phrase trop simple, deux sauvegardes différentes de la même clé.

### 11 sexies. La synchro : une archive remise en arrière est refusée (1.32.0)

L'index porte un **compteur**, augmenté à chaque envoi ; chaque poste retient le plus grand qu'il a vu (`archive-vu.json`, à côté de la clé ; `github-vu.json` jusqu'à la 1.34.0). Un index dont le compteur est plus petit (l'archive en ligne remise à un état plus ancien) arrête la synchro : rien n'est reçu ni envoyé, et c'est dit. Un poste neuf n'a encore rien vu : il reçoit l'archive en ligne telle qu'elle est.

### 11 septies. L'application de l'éditeur : `XPG-License-Manager-Admin` (1.33.0)

À la demande de l'éditeur (« je veux avoir une interface, une application »), l'outil a ses fenêtres : `XPG-License-Manager-Admin.exe`, le nom de la maquette (« XPG License Manager Admin »). La console reste, pour les scripts ; les deux travaillent dans le **même dossier** (`defaultKeyFolder()`), donc la même clé, la même archive et le même registre des activations. Un fichier signé par l'application est celui que la console aurait écrit : elles appellent les mêmes fonctions (`Admin.hpp`, `ArchiveSync.hpp` et `ServiceClient.hpp` depuis la 1.35.0, `KeyBackup.hpp`).

Trois étages (`CMakeLists.txt`) :

| Cible | Contenu |
|---|---|
| `xpg_admin_core` | `Admin`, `ArchiveSync` et `ServiceClient` (1.35.0), `KeyBackup`, le transport HTTPS (`app/ai/AiHttp`) et **`license_admin/gui/Workspace`** : le poste Admin sans écran. Chaque opération rend un `OpResult` (fait ou refusé, en français, le détail, le fichier écrit) : la clé, l'archive, l'assistant de licence (`readRequest`, `suggestLicenseId`, `preview`, `signLicence`), les révocations, le registre modifié, les activations, les clients, le tableau de bord, le service des licences (1.35.0), la sauvegarde de la clé ; `Background` fait tourner **un** travail lent (le réseau, Argon2id) sur un autre fil. |
| `xpg_admin_gui` | la coquille (`Shell` : le menu des 13 entrées de la maquette, l'état de la clé et de la synchro, le bandeau du dernier résultat, le journal de la séance, le glisser-déposer) et les pages (`Page.hpp` : chacune un `ui::Widget`, les widgets des Studios). Sans SDL : les essais les dessinent à blanc. |
| `xpg_license_manager_admin` | la fenêtre (`AdminGuiMain.cpp`, SDL3, seulement avec `XPG_WITH_SDL`) : les dialogues de fichiers du système, le glisser-déposer, `--dossier`, `--page`, `--capture` (les captures des livraisons), `--assistant`, `--cliquer`, `--taper`. Sous-système Windows GUI ; `SDL3.dll` à côté. |

Les pages : **Tableau de bord** (clients, licences actives, expirées, révoquées, PC activés, demandes en attente, celles qui finissent dans 30 jours, la répartition par édition, les dernières opérations), **Clients** (tirés des licences et des demandes : il n'y a pas encore de fichier des clients), **Licences** (la liste filtrée, le détail, Nouvelle licence…, Ajouter un PC…, Retirer un PC… — une nouvelle émission sans le PC choisi, la même fin : un PC remplacé ne reste pas dans la licence, où, révoqué, il bloquerait toute nouvelle émission ; la console : `signer-licence --licence … --retirer-pc CLÉ` —, Exporter le fichier…, Révoquer…) et son **assistant** en six étapes (les demandes des PC, le client et l'identifiant, l'édition et ses valeurs, les fonctionnalités, le récapitulatif — `preview` : rien n'est écrit —, signer puis publier), **Éditions** et **Fonctionnalités** (le registre modifié dans un brouillon partagé, jamais une suppression : une édition ou une fonctionnalité se retire ; « Signer le registre n° N+1 » ; un registre signé ailleurs entre-temps est dit, et le brouillon n'écrase rien), **Activations** (les PC connus, leur état, Exporter pour Excel), **Révocations** (la liste, Révoquer…, Retirer, Exporter la liste…), **Service en ligne** (1.35.0, à la place de Synchronisation GitHub : l'adresse du service, Vérifier, le certificat de sa clé, la synchro de l'archive ; le code de secours montré une fois, puis seulement après confirmation), **Historique** (le registre des activations, le plus récent d'abord ; une révocation répétée par les listes suivantes n'est montrée qu'une fois), **Paramètres de sécurité** (créer, sauvegarder, vérifier, restaurer la clé ; exporter la clé publique). **Licences flottantes**, **Boutique** et **Commandes** disent qu'elles demandent le service en ligne (§ 12) : rien n'y est simulé.

Les règles : le réseau et Argon2id jamais sur le fil de l'interface ; une opération qui écrit attend la fin du travail lent ; un secret tapé (phrase de passe, code de secours) est masqué, jamais dans un message ni dans le journal, effacé dès que l'opération est lancée.

Le contrôle du paquet refuse aussi le nom `XPG-License-Manager-Admin` ; l'application porte la marque de l'outil (`kAdminMarker`). `outils/mingw/admin_editeur.sh` construit les deux cibles et met dans le zip de l'éditeur l'application, `SDL3.dll` et la console.

## 12. Ce qui est prévu pour plus tard, et pourquoi il faut un service hébergé

*1.35.0 : le service est là (un Worker Cloudflare, `docs/SERVICE-LICENCES.md`) ; ce que le PC en fait est au § 12 bis. Ce paragraphe garde le raisonnement de la 1.30.*

Le format prévoit `type: "floating"` et `service` ; rien ne prétend les faire : une telle licence est lue puis refusée, avec la phrase qui le dit.

- **Les licences flottantes** (N jetons simultanés) demandent un **compteur partagé avec un verrou atomique** : deux postes qui démarrent en même temps ne doivent pas prendre le dernier jeton tous les deux. Un fichier ou un dépôt partagé n'a ni verrou atomique ni contrôle d'accès par client ; il faut une base transactionnelle derrière un service (baux de 45 min renouvelés, jeton emprunté avant de partir hors ligne, maquette).
- **L'activation en ligne** (une clé de licence tapée, sans échange de fichiers), la **désactivation** d'un PC qui libère sa place tout de suite, le **transfert** validé par l'Admin : il faut quelqu'un qui réponde, et qui tienne le compte des PC activés.
- **Une heure de confiance** : un service peut signer l'heure ; hors ligne, un PC ne prouve rien (§ 8).
- **Une révocation rapide** : sans service, une révocation n'arrive qu'avec un fichier importé.
- **La vérification du poste Admin par défi signé** (maquette : « sans réponse valide, toutes les fonctions sont bloquées ») suppose le service, qui connaît la clé publique du poste.

- **Voir chaque lancement du logiciel** (même sans activation : qui l'utilise, quelle version, quand) : le registre des activations de l'Admin (§ 11) ne voit que les demandes que les clients lui envoient. Voir chaque lancement demande le **service de licences en ligne** : à chaque lancement, le client enverrait l'identifiant de sa licence (s'il en a une), l'**empreinte de la clé du PC**, la version du logiciel et la date — **jamais le matériel** (ni nom réseau, ni adresse MAC, ni numéro de série). Il faudra aussi **informer clairement les utilisateurs** (RGPD : une mention dans les conditions d'utilisation, ce qui est envoyé, pourquoi, combien de temps c'est gardé). Prévu avec l'hébergement ; **pas fait**.

L'hébergement (un petit serveur, ou une fonction en ligne avec sa base) n'est pas décidé ; le format est prêt à l'accueillir sans changer les licences par fichier.

## 12 bis. Le service en ligne (1.35.0) : le côté du PC

Le contrat (les clés, les appels signés, les documents, les routes) est dans [SERVICE-LICENCES.md](SERVICE-LICENCES.md). Ici, ce que fait **le magasin** (`src/licensing/Store`, `Licensing.hpp`) : il ne fait **aucun réseau** ; l'application (`src/app`, sur un fil de travail) parle au service et lui passe ce qu'elle a reçu ; le magasin le **vérifie**, le **garde** et en tire l'état.

### La licence en ligne

Une licence **en ligne** a le champ `service` (l'adresse du service, `https://…`) et **aucun PC lié** ; `max_devices` est le nombre de postes que le service accorde. La lecture le vérifie (`Documents.cpp`) : sans `service`, au moins un PC lié (comme avant) ; avec, aucun (un mélange est refusé) ; `floating` seulement en ligne. Une licence en ligne ne s'importe **pas** en fichier (« c'est une licence en ligne : elle s'active avec son code d'activation (menu Licence…), pas par un fichier. ») et ne lie jamais un PC par elle-même.

Elle vaut sur ce PC avec `activation.xpgact` : le bloc `activation` (signé par le service) et le bloc `service` (le certificat, signé par l'éditeur). Tout est vérifié, à chaque évaluation :

| Contrôle | Sinon |
|---|---|
| le certificat signé par une clé de l'éditeur que ce programme croit | Invalide |
| le certificat nomme **l'adresse du service de ce programme** (`StoreOptions::serviceUrl`, par défaut `service::compiledAddress()` ; schéma, hôte et port, sans `/` final) ; un programme **sans adresse** ne croit aucun document du service (1.35.0, revue de sécurité) | Invalide |
| le certificat pas expiré à **l'heure retenue** | **Bail expiré** (« le certificat du service a expiré le … ») |
| l'activation signée par la clé que nomme le certificat, qui donne le droit `activation` | Invalide |
| la même licence, la même émission (`emission` = `issued_at` de la licence) | Bail expiré (une autre licence) ; Invalide (une autre émission) |
| la clé de **ce** PC ; l'empreinte du poste (`station`) : **obligatoire** dès que cette installation en a une (le cas normal ; une activation sans `station` vaudrait partout où la clé serait recopiée) | Autre PC |
| le type (`fixe` / `flottante`) celui de la licence | Invalide |
| le bail (`jusqu_a`) pas fini à l'heure retenue | **Bail expiré** |

Puis les règles de toute licence : jamais une émission plus ancienne, la révocation, l'horloge reculée, la fin et la grâce — pour une licence en ligne, l'horloge reculée et la fin se jugent sur le compteur et **l'heure retenue** (ci-dessous), pas sur l'horloge du PC. Une licence **liée par l'éditeur** (sans `service`) marche **exactement comme avant** : l'activation ne la regarde pas.

`installActivation(licence, activation, certificat)` (la réponse de `/v1/activer` ou `/v1/renouveler`) écrit `licence.xpglic` et `activation.xpgact` seulement si **tout** se vérifie pour ce PC et que le bail court encore ; sinon le rapport dit pourquoi (« cette activation est pour un autre PC », « … pour une autre émission de la licence … », « … pour une autre installation de Windows », « le certificat du service a expiré le 06/11/2026 », « le certificat du service ne donne pas le droit « activation » », « son bail est déjà fini (le …) : connecte ce PC à Internet pour le renouveler. »). Le premier import est noté dans l'historique ; un **renouvellement** (même licence, même émission, même numéro) ne note rien, sauf un changement d'état ; une activation rejouée plus courte que celle gardée ne la remplace pas. `dropActivation(id)` (le service a dit : libérée, retirée, révoquée) efface l'activation, le jeton et la licence en ligne de cette licence ; une ligne `service` le dit.

### La licence perdue en direct (1.39.0)

Tant qu'une licence est sur le PC, le Studio regarde `GET /v1/etat` **toutes les 2 minutes** (`kWatchPeriod` ; un appel public, rien d'écrit). Le service y dit `changements`, un compteur qui monte à chaque révocation déposée, licence déposée ou désactivée, PC retiré, état de poste déposé, et le numéro du registre. Le compteur ou le registre a bougé : la **synchronisation complète** part tout de suite (renouveler, l'état « poste », le registre). Une licence retirée de ce PC, révoquée ou désactivée y est effacée ; le Studio montre alors la fenêtre **« Ta licence n'est plus active sur ce PC »** (une fois par licence et par lancement ; Voir ma licence…) et la notification « Licence perdue sur ce PC ». Les fonctions se ferment tout de suite ; **rien ne se ferme** : le projet ouvert reste ouvert, Enregistrer et Exporter restent permis. Hors ligne, c'est au lancement suivant connecté, au plus tard à la fin du bail (30 jours). Une licence par fichier révoquée par une liste adoptée (l'état « poste », un fichier importé) : la même fenêtre. Le poste d'exploitation (`--ihm`) n'est jamais arrêté.

### L'outil de l'éditeur en direct (1.40.0)

L'application de l'éditeur, ouverte, regarde son service **toutes les 15 secondes** (`Shell::autoWatch` : `GET /admin/etat`, signé par la clé de l'éditeur) : des compteurs qui montent quand un PC est activé, **libéré par son client** (Licence › Mes appareils › Libérer ce PC, dans son Studio) ou retiré à son renouvellement, et quand une demande est déposée ou annulée. Ils ont bougé : les licences en ligne (ou les demandes) sont **relues seules** (`autoRefreshLicences`), les pages suivent, et un avis dit ce qui a changé (`Shell::describeChanges` : « Atelier … a libéré le PC « … » (licence XPG-…) : sa place est libre. »). Le haut de la fenêtre dit « Service : … · en direct ». Ce regard est court et **n'occupe pas** l'application : un travail lancé pendant qu'il tourne attend sa fin (une file d'une place) et part seul. Pas sous un dialogue ; la première panne est dite une fois. Un service d'avant la 1.40.0 (sans `/admin/etat`) : les licences sont relues toutes les 30 secondes. Environ 8 appels par minute tant que l'application est ouverte (l'état public pour l'heure, puis l'appel signé) : le plan gratuit de Cloudflare en permet 100 000 par jour.

### Le bail et son compteur (l'heure retenue)

`etat.bin` garde trois champs de plus (que la 1.34 ignore) : `service_time` (la dernière heure signée reçue du service), `service_elapsed` (le temps écoulé depuis) et `service_wall` (l'horloge du PC la plus avancée déjà comptée). Une copie : `horloge.bin` (protégé comme lui, DPAPI machine) et, sous Windows et pour le dossier par défaut, la valeur `horloge` de `HKCU\Software\XPG\Licences`. La meilleure copie gagne (l'heure signée la plus récente, puis le plus de temps écoulé) ; une copie qui manque ou en retard est remise ; `etat.bin` effacé est recréé comme avant (§ 7 bis), le compteur repris de `horloge.bin`.

- **Le temps écoulé ne fait qu'avancer**, à chaque évaluation (`refresh`, un import…) : l'horloge du PC la plus avancée déjà comptée avance jusqu'à l'heure du PC ; et, dans un même lancement, du temps mesuré par une **horloge monotone** (`std::chrono::steady_clock`, le temps où le programme tourne) — le plus grand des deux, jamais les deux additionnés. D'un lancement à l'autre, seuls les écarts positifs de l'horloge du PC comptent.
- **L'heure retenue** = max(horloge du PC, dernière heure signée + temps écoulé, **l'émission (`emis`) de l'activation et du jeton**) (`LicenseState::trustedNow`) : une date écrite dans un document signé ne s'efface pas ; une émission plus récente que le compteur le recale en avant (jamais en arrière). Le bail, le certificat, l'avertissement, **la fin et la grâce d'une licence en ligne** s'y comparent : **reculer l'horloge ne prolonge jamais un bail** ; une horloge avancée par erreur l'avance (le bail peut paraître fini) jusqu'à la connexion suivante.
- **Sans aucune copie lisible du compteur** (`etat.bin`, `horloge.bin`, HKCU effacés, ou illisibles) : une licence en ligne activée est en **Bail expiré** (« Connecte ce PC à Internet pour renouveler sa licence : l'heure du service des licences n'est plus connue sur ce PC. ») — jamais l'horloge du PC seule. La connexion suivante (l'heure signée) la rend Valide sans rien réinstaller. Une activation installée sans heure signée (`/v1/heure` n'a pas répondu) est dans le même cas.
- **L'horloge du PC en arrière** de plus que la tolérance (24 h) sur la plus avancée déjà comptée depuis la dernière heure signée : **Horloge reculée**, datée ou non (« … remets-la à l'heure, ou connecte ce PC à Internet pour la recaler. ») ; l'heure signée de la connexion suivante recale le compteur. Pas l'heure la plus tardive de toujours (`etat.bin`) : une horloge avancée une fois par erreur bloquerait sinon la licence en ligne même après la connexion.
- **`noteServiceTime(heure, nonce, certificat)`** : le bloc `heure` de `/v1/heure?n=<nonce>`, signé par la clé certifiée (droit `heure`), qui répond au nonce de la demande, dans la validité du certificat à l'heure signée. **Plus récente** que la dernière gardée : le compteur repart d'elle (temps écoulé à zéro) — elle recale tout, même vers le passé. **Pas plus récente** : ignorée (vrai, rien ne change). Un autre nonce, une autre clé, un certificat sans le droit : refusée.
- **Sept jours avant la fin** du bail : `activationWarning`, et la phrase ajoute « Connecte ce PC à Internet avant le 07/12/2026 pour renouveler sa licence. » ; sinon « Activée en ligne jusqu'au 07/12/2026 (renouvelée à chaque connexion). ».
- **Honnêtement** : hors ligne, rien ne mesure le temps où le programme ne tourne pas ; une horloge tenue juste derrière (moins que la tolérance) ou arrêtée entre deux lancements ne compte que le temps où le programme tourne. Effacer les copies du compteur ne sert plus à rien (Bail expiré) ; remettre une **ancienne copie** sauvegardée de toutes (`etat.bin`, `horloge.bin`, HKCU) avec l'horloge reculée n'est pas détectable sans réseau. Le bail borne tout : à `jusqu_a` de l'heure retenue, c'est fini.

### La licence flottante

Activée (une activation `flottante`), elle demande en plus `jeton.xpgjet` : le bloc `jeton` (droit `flottante`) pour cette licence et ce PC, et le certificat ; `installLease` le vérifie et l'écrit, `dropLease` l'efface (le poste rendu). Sans jeton valable : **Flottante : aucun poste** (FREE). Le jeton (15 min) se compte à **l'heure du service** (dernière heure signée + temps écoulé ; jamais avant son émission ni celle de l'activation) : une horloge du PC en avance ne le périme pas d'avance ; reculée ou arrêtée, elle ne le prolonge pas ; sans compteur, Bail expiré. `leaseUntil` dit sa fin.

### La Découverte enregistrée par le service

`installTrialRecord(découverte, certificat)` : le bloc `decouverte` de `/v1/decouverte` (droit `decouverte`), pour **cette machine** (`machine` = `thisMachineHash()`, l'empreinte de `docs/SERVICE-LICENCES.md` § 7) et **ce PC** ; il est gardé dans `decouverte.xpgdec` (un début plus ancien déjà gardé reste). À l'évaluation, son début est **une copie de plus** du début de la Découverte (§ 8 ter) : le **plus ancien** gagne, jamais prolongée ; la durée reste celle de l'édition d'évaluation du registre. Un début plus ancien venu du service est noté dans l'historique (« Découverte commencée le 28/10/2026 sur cette machine, selon le service des licences : … »). Effacer toutes les copies locales ne relance pas l'essai tant que `decouverte.xpgdec` est là ; et le service, lui, rend toujours le premier début connu.

### Le reste de l'API (Licensing.hpp)

- `deviceSignServiceMessage(message, sig, pub, why)` : signe le message d'un appel au service avec la clé de ce PC (créée si besoin) ; refuse tout message qui ne commence pas par `XPG-SERVICE-1\n` (la clé du PC ne signe rien d'autre pour le réseau).
- `thisMachineHash()` : l'empreinte de la machine pour `/v1/decouverte`.
- `thisStationHash()` : l'empreinte du poste de ce PC (`stationHash`, 32 hexadécimaux), à envoyer dans `/v1/renouveler`, `/v1/flottante/jeton` et `/v1/flottante/rendre` (la clé du PC créée si besoin ; vide avant `initialize()`). La demande d'activation (`.xpgreq`) la porte toujours (quand l'identifiant de l'installation se lit).
- `pendingActivationCode()` / `clearPendingActivationCode(why)` : le code laissé par l'installateur (`code-activation.txt` du dossier des licences : sa première ligne, sans espaces autour ; UTF-8 avec ou sans BOM, ou UTF-16) ; un **secret** : jamais dans un journal, un historique, un message ; effacé une fois l'activation faite. Tout compte peut créer un fichier dans ce dossier : le code n'est lu que si le fichier appartient aux **Administrateurs** (S-1-5-32-544) ou à **SYSTEM** (S-1-5-18) — l'installateur pour tous les comptes — ou à **ce compte** quand **aucune licence valable** n'est installée (une installation pour ce seul compte, ou le code posé à la main) ; ailleurs que sous Windows, root ou ce compte ; un propriétaire illisible (Wine, un volume sans droits) compte comme ce compte. Sinon il est ignoré : le fichier reste (jamais effacé s'il est à un autre compte), une ligne `controle` de l'historique le dit, sans le code (`fileOwnerOf`, `StoreOptions::fileOwner` pour les essais).
- `LicenseState` : `online`, `activationUntil`, `activationDaysLeft`, `activationWarning`, `floating`, `leaseUntil`, `lastServiceSync`, `trustedNow`.
- Chaque appel qui change quelque chose revalue l'état ; tout est sous le verrou du module. `StoreOptions::monotonicClock` remplace l'horloge monotone (les essais).

Les fichiers de plus dans le dossier des licences : `activation.xpgact`, `jeton.xpgjet`, `decouverte.xpgdec`, `horloge.bin`, `code-activation.txt` (laissé par l'installateur, effacé après).

## 13. Le contrôle du paquet

`outils/mingw/package_mingw.py` contrôle le dossier à livrer **après le staging et pour chaque zip portable** ; un seul écart arrête la construction, avec la liste. Il cherche, dans les noms et dans les octets de chaque fichier (en ASCII et en UTF-16 : les chaînes larges des exe) :
- le nom de l'outil (`XPG-License-Admin`, « XPG License Manager Admin », un nom de fichier qui le rappelle) ;
- sa marque `XPG-LICENSE-ADMIN-NE-JAMAIS-LIVRER` (dans l'exe et dans le fichier de la clé de l'éditeur) ;
- la marque d'une clé d'essai `XPG-CLE-ESSAI-LICENCES` (un client qui embarque la clé d'essai n'est pas livrable) ;
- une clé privée : un bloc PEM « PRIVATE KEY » **suivi de son contenu** (l'en-tête seul est une chaîne de mbedTLS, présente dans les exe : ce n'est pas une clé), un fichier `*.cle`, `cle_editeur.bin` ;
- un jeton GitHub (`ghp_…`, `github_pat_…` ; 1.30.0 : les bornes de mot sont écrites `(?<![A-Za-z0-9_])…(?![A-Za-z0-9_])`, pour que Python et .NET lisent le motif pareil) ;
- 1.30.0 : **le registre livré** (`licences\registre.xpgreg`, s'il y est) doit être un registre signé (un seul bloc) **par une clé de `PublisherKey.hpp`** — signé par la clé d'essai, ou par une clé que les programmes ne connaissent pas, il arrête la construction.

`python3 outils/mingw/package_mingw.py --controler <dossier>` fait le contrôle seul (l'essai s'en sert). Sur le dossier de livraison de la 1.28.0 : aucun écart. `cross_mingw.sh` ne construit pas l'outil Admin. 1.30.0 : sur un dossier fait des seuls exe clients de `build-mingw` (API-Studio, IHM-Studio, l'agent) : aucun écart ; avec `XPG-License-Admin.exe` renommé `outil.exe` à côté : arrêt (« le nom de l'outil Admin des licences ») ; la bibliothèque `xpg_licensing` MinGW et Linux : aucun écart.

**Le registre livré** (1.30.0) : `package_mingw.py` et `package.ps1` copient `ressources-licences/registre.xpgreg` (à la racine du dépôt) dans `licences\registre.xpgreg` du dossier à livrer **s'il existe**, et le disent ; **aujourd'hui il n'existe pas** (il sera signé par toi, avec ta vraie clé) : rien n'est copié, et la sortie le dit (« registre des licences : ressources-licences/registre.xpgreg absent - aucun registre livré »). L'installateur le pose dans `{app}\licences\registre.xpgreg` ; le programme le donne à `StoreOptions::bundledRegistry`.

**`outils/lib/package.ps1`** (la construction sous Windows, 1.30.0) : le **même** contrôle (`Test-XpgLivraison` : mêmes chaînes en ASCII et en UTF-16, mêmes motifs, mêmes noms, même contrôle du registre livré), une étape à part après le dossier à livrer (« Le contrôle du paquet »), qui arrête la construction (`Stop-Xpg`) avec la liste des écarts. Les octets sont lus en Latin-1 (un caractère par octet) : les chaînes se cherchent en ordinal, les motifs avec `Regex` (sensibles à la casse, comme en Python). **Pas exécuté ici** (pas de PowerShell sous Linux) : relu ligne à ligne contre `package_mingw.py`. Le jour où `PublisherKey.hpp` porterait la clé d'essai, ou une construction avec `XPG_LICENCES_CLE_ESSAI` serait empaquetée, le contrôle refuserait la livraison — c'est voulu.

### 13 bis. L'installateur (1.30.0)

`installateur/XPGAnalyser.iss`, procédure `LicencesRegler` (après la copie, comme `AgentRegler`) :
- **pour tous les comptes** : `%ProgramData%\XPG\Licences` (`{commonappdata}\XPG\Licences` : le chemin exact de `LicenseStore::defaultFolder()`) est créé ; `icacls "<dossier>" /grant *S-1-5-32-545:(OI)(CI)M` (le groupe Utilisateurs par son SID, quelle que soit la langue de Windows ; caché) lui donne le droit Modifier, hérité par ce qui y sera écrit. Le journal le dit (« Le dossier des licences de ce PC : … (chaque compte peut y importer une licence ; il est gardé à la désinstallation) »), ou dit ce qui n'a pas marché (le code d'icacls) ;
- **pour toi seul** : rien (pas de droits sur ProgramData) ; le journal dit le repli : le programme crée le dossier au premier besoin, et un autre compte le lira sans y importer (§ 7) ;
- le registre livré : le journal dit s'il est là (`{app}\licences\registre.xpgreg`) ; « Ce qui a été installé » aussi (et une rubrique LES LICENCES) ;
- **la désinstallation ne l'efface pas** : les licences de ce PC (sa clé, sa licence, son historique) survivent à une réinstallation ; le journal de la désinstallation le dit (« Les licences de ce PC sont gardées : … »).

Vérifié : ISCC 6.4.1 (sous Wine, `/O-` : le script compile, sans rien produire). Pas essayé : une installation réelle sous Windows (icacls, les droits d'un second compte).

**1.32.0 : le dossier ne peut plus être détourné.** Avant, le droit Modifier hérité permettait à un utilisateur de supprimer le dossier et de le remplacer par une **jonction** vers un dossier de programmes : la réinstallation suivante (administrateur) aurait donné le droit Modifier aux Utilisateurs sur la cible. Maintenant (`DossierProtege`) : un dossier qui est un lien est retiré (jamais sa cible) ; `XPG` puis `Licences` reçoivent le propriétaire Administrateurs, l'héritage coupé, SYSTEM et Administrateurs en contrôle total ; les Utilisateurs lisent `XPG`, et dans `Licences` ils créent des fichiers (`RX,WD,AD` sur le dossier : ils ne peuvent ni le supprimer ni le remplacer) et modifient ce qui est dedans (`(OI)(CI)(IO)M`). Le dossier commun de l'Assistant IA (`%ProgramData%\XPGAnalyser\IA`) est protégé de même, en lecture seule pour les Utilisateurs.

**1.32.0 : le contrôle du paquet** refuse aussi : dans `licences\`, tout sauf le registre, la liste de révocation et les textes de licence des bibliothèques ; partout, `*.xpglic`, `*.xpgreq`, `github*.json`, `etat.bin`, `historique.jsonl`, un dossier `archive/` (1.35.0 : aussi les fichiers du service de l'outil Admin — `service.json`, `service-releve.json`, `archive-vu.json`, `postes-publies.json`, `cle-synchro.cle` — et un dossier `demandes-en-ligne/`), un bloc de licence ou de demande dans n'importe quel fichier, une sauvegarde de clé (`*.xpgcle`, `XPG-SAUVEGARDE-CLE`), les fichiers de la synchro (`*.xpgsync`, `XPG-CHIFFRE 1 xpgs-`, `XPG-INDEX 1 xpgs-`). Avec `[Licences] obligatoires = oui` dans `outils/config.ini` (ou `--licences-obligatoires`), une construction dont `PublisherKey.hpp` n'a aucune clé est refusée (par défaut `non` : la clé de l'éditeur n'est pas encore créée).

## 14. Les essais

`licensing_test <XPG-License-Admin> <racine du dépôt> <libxpg_licensing.a>` (ctest `licensing`) : une clé d'éditeur d'essai créée dans un dossier temporaire, des PC simulés (un dossier chacun, une paire de clés chacun), l'heure donnée par l'essai.
- les formats (dates, enveloppe tronquée à six longueurs, genre inconnu, CRLF, la signature liée au genre) ;
- la clé de l'éditeur (essai marquée partout, jamais écrasée, `PublisherKey.hpp` généré en ASCII) ;
- le registre de la maquette signé, relu, réécrit à l'identique ; ses refus ;
- un PC de bout en bout : Absente sans registre, FREE sans licence, la demande (rien du matériel, clé 0600 ou DPAPI), la licence, l'API, la grâce, l'expiration, l'horloge reculée (et l'ancrage des documents quand l'état est effacé) ;
- chaque refus : signature fausse, clé inconnue, autre PC, tronquée, champ en double, champ inconnu, édition et fonctionnalité inconnues, flottante, service en ligne, registre plus ancien (import et disque), registre livré plus récent adopté, révocation (licence, un seul PC), liste plus ancienne, liste remplacée ou effacée, émission plus ancienne ;
- les fonctionnalités de chaque édition, exactement celles du registre ; les limites ; perpétuelle ; plusieurs PC (étendre, trop de PC), demande modifiée en chemin ou pour une autre licence ;
- l'outil Admin en **sous-processus**, de bout en bout : clé, export, registre, demande → licence → import → vérification → révocation ; ses refus et codes de sortie ;
- le contrôle du paquet : un dossier propre passe (avec l'en-tête PEM de mbedTLS), et chacun de neuf écarts arrête la construction (l'exe par son nom, l'exe renommé, la marque, le nom en UTF-16, une clé PEM, la clé de l'éditeur, une clé de PC, la clé d'essai embarquée, un jeton) ; 1.30.0 : un registre livré signé par une clé inconnue des programmes, un registre livré abîmé ;
- **1.30.0** : les deux fichiers de clés du dépôt, octet pour octet ce qu'écrit `exporter-cle` ; `PublisherKey.hpp` sans la marque d'essai ; les clés compilées selon l'option ; le format à 3 vraies clés (des clés publiques tirées au hasard : aucune clé privée créée) écrit en ASCII et relu, noms accentués compris ; vide ; une clé modifiée à la main refusée ; les genres jamais mélangés ; `exporter-cle` en sous-processus (clé d'essai refusée vers `PublisherKey.hpp`, `--garder` : 2 clés, puis toujours 2, sans `--garder` : 1 et l'autre dite retirée, `--vide`) ;
- **1.30.0** : un programme sans clé (Non activées : tout ouvert, même un identifiant inconnu ; rien écrit ; l'API ; l'import refusé avec sa phrase ; la demande écrite et relue ; l'empreinte après la demande ; l'historique) ;
- **1.30.0** : l'écran Licence (le registre de la maquette par `getRegistry`, une fonctionnalité et sa limite, deux éditions ; les PC liés, un seul `thisPc`, son empreinte = `thisDeviceFingerprint` ; la licence d'autres PC : Autre PC, ses PC listés, aucun n'est ce PC ; sans licence : aucun) ; l'historique (les 6 premiers événements d'un PC, Valide → Grâce et Grâce → Expirée constatés par `refresh`, un refresh sans changement n'ajoute rien ; 600 ajouts → 500 lignes, les plus anciennes parties ; des lignes illisibles ignorées puis non recopiées ; une ligne d'une version plus récente lue) ; un dossier non inscriptible (l'évaluation marche, `detail` le dit une fois) ;
- **1.30.0** : le registre des activations en sous-processus (deux licences pour deux PC, une extension, une révocation d'un PC, une demande enregistrée seule, une demande modifiée refusée ; 6 lignes, les premières jamais réécrites, pas le nom réseau ; l'état de chaque PC — activé, expiré, révoqué, demande — ; le tableau et ses totaux ; `--client`, `--licence` ; le CSV : BOM, `;`, CRLF, un champ entre guillemets ; une ligne abîmée ignorée et dite) ;
- **1.30.0** : la bibliothèque `xpg_licensing` construite sans l'option : ni la marque ni l'identifiant de la clé d'essai, rien de l'outil Admin.

`licensing_evaluation_test <racine du dépôt>` (**1.34.0**) : la période d'évaluation (§ 8 ter), avec une clé d'éditeur d'essai, le registre d'exemple et l'heure donnée par l'essai — le premier lancement (Évaluation, 30 jours, les fonctionnalités d'EVALUATION, les fichiers, l'historique), jour 12 (18 jours), le dernier jour (« 1 jour »), la fin (Absente, FREE, la phrase, « Évaluation → Absente ») ; chaque copie effacée, deux à la fois, abîmée, une copie plus tardive ; l'horloge reculée (et tolérée) ; jamais prolongée (60 jours), raccourcie (14) ; une autre édition d'évaluation, une retirée, aucune ; une licence importée puis effacée (l'évaluation d'avant reprend), révoquée, expirée, un PC qui a eu une licence d'abord ; Non activées (rien d'écrit), sans registre puis le registre importé, l'option vide ; un `etat.bin` d'avant ; un dossier non inscriptible ; l'API.

`licensing_activation_test <racine du dépôt>` (**1.35.0**) : le côté PC du service des licences (§ 12 bis), sans réseau — une clé d'éditeur et une clé de service d'essai, le certificat, les licences en ligne, les activations, les jetons, les heures et les Découvertes signés dans l'essai, l'horloge du PC et l'horloge monotone données par l'essai. Les documents (la licence en ligne se lit ; en ligne et liée, flottante sans service, une adresse en http ailleurs, ni service ni PC : refusées) ; une activation valable (Valide, en ligne, le bail, les fichiers, l'historique ; renouvelée sans bruit ; une plus courte rejouée ignorée ; la licence en ligne et le certificat refusés en fichier) ; les refus (un autre PC, une autre émission, une autre installation de Windows, une autre licence, une clé de service inconnue, un certificat d'une clé d'éditeur inconnue, expiré, sans le droit, un bail fini, le type, sans licence, une licence liée, une licence d'une autre clé ; rien d'écrit ; Non activées) ; le bail (7 jours avant : l'avertissement ; Bail expiré et sa phrase ; l'horloge reculée ne le ravive pas ; l'horloge monotone compte, l'horloge du PC arrêtée ; un autre lancement ; `horloge.bin` puis `etat.bin` effacés ; une heure signée puis le renouvellement) ; l'heure signée (une horloge avancée par erreur recalée, une plus ancienne ignorée, les deux horloges ensemble comptées une fois, un autre nonce, une autre clé, sans le droit, après le certificat) ; un certificat qui expire pendant le bail ; la flottante (sans jeton, avec, fini, rendu, d'un autre PC, d'une autre licence, une horloge 20 minutes en avance, `dropActivation`) ; la Découverte du service (une autre machine, un autre PC, sans le droit ; plus ancienne : gagne ; plus tardive : ignorée ; les copies locales effacées ; la fin) ; une licence liée par l'éditeur (comme avant : Valide, Horloge reculée) ; l'API (le code de l'installateur relu et effacé, jamais dans l'historique ; la signature ; activation, jeton, heure, Découverte, `dropActivation`). **125** contrôles sous Linux.

`github_sync_test` (ctest `githubsync`, 1.31.0 à 1.34.0) a été retiré avec la synchro GitHub en 1.35.0 ; la synchro de l'archive par le service est essayée par `admin_service_test` (ctest `adminservice`, § 11 quater).

Sous Linux : **239** contrôles (1.29.0 : 154). Sous Wine (exe MinGW) en 1.29.0 : 142 contrôles, le contrôle du paquet sauté ; en 1.30.0, `licensing_test` n'a pas été refait sous Wine (`build-mingw` est configuré sans les essais) — l'outil Admin MinGW, lui, a été essayé sous Wine (§ 11 ter : `--version`, `creer-cle --essai`, `cle`, `exporter-cle` vers les deux fichiers, `--garder`, `--vide`, `signer-registre`, `verifier`).
