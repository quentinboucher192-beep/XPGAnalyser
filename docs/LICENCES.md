# Licences — conception (préparées en 1.29.0, pour la 1.30.0)

Ce document décrit le socle des licences : le format des fichiers signés, le registre des éditions et fonctionnalités, l'identité d'un PC, la vérification hors ligne et ses états, l'activation par fichier, l'outil de l'éditeur et le contrôle du paquet. Il reprend la maquette validée (onglets Studio, Admin, Client, Architecture) et les décisions prises avec toi :
- **version 1 sans service en ligne** : licences signées, vérifiées hors ligne, activation **par fichier** ;
- **rien n'est écrit en dur** : les éditions, les fonctionnalités et leurs droits viennent d'un **registre signé** et numéroté ; le code ne porte que des identifiants de fonctionnalités (`ai_generation`, `deploy_network`…) ;
- **un PC est une paire de clés** créée sur lui (Ed25519, clé secrète sous DPAPI machine) ; jamais son matériel ;
- **une clé d'éditeur** (Ed25519) signe licences, registres et révocations ; sa clé publique est compilée dans les logiciels clients ; sa clé privée ne quitte pas le poste Admin.

Retour au [README](../README.md). Voir aussi [DEPLOIEMENT.md](DEPLOIEMENT.md) (la clé d'éditeur du développeur, `deploy/DevKey`, qui signe les paquets : ce n'est pas la même clé).

## 1. Où on en est

| | Fait (1.29.0, **non livré**, branché nulle part) | Pas fait |
|---|---|---|
| Bibliothèque | `src/licensing/` (`xpg_licensing`) : formats, registre, licence, révocations, identité du PC, demande d'activation, import, vérification hors ligne, états, l'API de la maquette | — |
| Outil Admin | `src/license_admin/` : `XPG-License-Admin` en console (clé de l'éditeur, signer registre / licence / révocations, vérifier) | l'interface graphique de l'Admin (maquette : tableau de bord, clients, boutique…), la vérification du poste Admin par défi signé, la publication dans le dépôt GitHub |
| Clé de l'éditeur | une **clé d'ESSAI** dans `src/licensing/PublisherKey.hpp` | **la vraie clé**, à créer sur ton poste Admin (§ 11) |
| Interface | — | le License Manager (client), l'écran Licence, les verrous des menus, l'import dans les Studios (1.30.0, par toi) |
| Installateur | le contrôle du paquet dans `outils/mingw/package_mingw.py` | le même contrôle dans `outils/lib/package.ps1` (Windows) ; le dossier `%ProgramData%\XPG\Licences` et ses droits ; le registre livré avec le programme |
| En ligne | le format le prévoit (`type: floating`, champ `service`) ; une licence qui les emploie est **refusée** avec une phrase qui le dit | l'activation en ligne, les licences flottantes, la désactivation et le transfert en ligne, l'heure signée (§ 12) |

## 2. Les programmes et les modules

| Où | Quoi |
|---|---|
| `src/licensing/Time` | les dates : secondes UTC, écrites `AAAA-MM-JJTHH:MM:SSZ` et rien d'autre |
| `src/licensing/SignedFile` | l'enveloppe signée (§ 3) |
| `src/licensing/Documents` | registre, licence, révocations, demande : lecture **stricte**, écriture canonique |
| `src/licensing/KeyFile` | une paire Ed25519 gardée sur ce PC (`SigningKey`) ; l'identité du PC |
| `src/licensing/Store` | les licences de ce PC : évaluer, importer, écrire la demande d'activation |
| `src/licensing/Licensing` | l'API des logiciels (§ 9) |
| `src/licensing/PublisherKey.hpp` | **généré** par l'outil Admin : la ou les clés publiques de l'éditeur |
| `src/license_admin/Admin`, `AdminMain.cpp` | l'outil de l'éditeur (cible `xpg_license_admin`, exe `XPG-License-Admin`) — **jamais livré** |
| `tests/licensing_test.cpp` | l'essai (ctest `licensing`) |
| `tests/fixtures/licences/` | le registre d'exemple (les éditions et fonctionnalités de la maquette), des paramètres et une révocation d'exemple |

`licensing/` ne dépend que de `core/` (JSON, processus) et de `deploy/Crypto` (SHA-256, Ed25519, hasard, DPAPI, écriture d'un fichier privé : rien n'est réécrit). Il n'a ni interface ni lien vers `src/app/`.

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

Numérotée et signée ; un client refuse une liste plus ancienne que la plus récente vue. Avec `device` : la licence n'est retirée que pour ce PC (un transfert). Une liste déjà vue qui **disparaît** du disque, ou qui est remplacée par une plus ancienne : la licence devient Invalide (pas de retour à Valide). Sans service, un PC n'apprend une révocation qu'en important un fichier qui la porte (le renouvellement suivant de quiconque en reçoit une, ou un envoi exprès).

## 7. L'identité du PC et l'activation par fichier

1. **Sur le PC client** (License Manager, 1.30.0) : « Demander une activation ». Au premier besoin, une **paire Ed25519** est créée sur ce PC (`identite-pc.cle` ; clé secrète sous **DPAPI machine** : tous les comptes de ce PC, aucun autre PC). La demande (`.xpgreq`) porte la **clé publique**, un **nom choisi** par l'utilisateur (facultatif), la licence demandée (facultatif), le logiciel ; elle est **signée par la clé du PC** (preuve qu'il la détient). Rien du matériel : ni nom réseau, ni adresse MAC, ni numéro de série.
2. **Sur le poste Admin** : `XPG-License-Admin signer-licence --parametres P.json --demande PC.xpgreq --registre R.xpgreg [--revocations V.xpgrev] --sortie L.xpglic`. L'outil vérifie la signature de la demande, l'édition, les fonctionnalités, le nombre de PC, la liste de révocation.
3. **Sur le PC client** : « Activer par fichier de licence » → `importLicenseFile`. Le registre et les révocations passent d'abord ; la licence n'est installée que si elle vaut **pour ce PC, maintenant** (signée par l'éditeur, liée à la clé de ce PC, pas révoquée, pas finie, horloge en ordre, pas plus ancienne que celle installée). Sinon rien ne change et la raison est dite.

**Le dossier** (`LicenseStore::defaultFolder()` : `%ProgramData%\XPG\Licences` sous Windows) : `identite-pc.cle`, `licence.xpglic`, `registre.xpgreg`, `revocations.xpgrev`, `etat.bin` (protégé par DPAPI machine : l'heure la plus tardive vue, les numéros les plus hauts vus). Copier ce dossier sur un autre PC ne sert à rien : la clé du PC ne s'y relit pas (DPAPI) et la licence n'est pas liée à la clé de l'autre PC.

**Honnêtement** : DPAPI machine empêche de recopier la clé ailleurs et de la lire en ouvrant le fichier ; un programme qui tourne sur **ce** PC peut s'en servir (c'est le principe). Sous Linux, pas de DPAPI : fichiers lisibles par leur seul propriétaire (0600), et c'est écrit dans le fichier (`protection=fichier`).

## 8. Les états

| État | Quand | Fonctionnalités | La phrase (exemples réels de l'essai) |
|---|---|---|---|
| **Valide** | tout est en ordre | celles de la licence | « Licence PROFESSIONAL valide jusqu'au 2027-11-06. » |
| **Grâce** | après `expires_at`, pendant `offline_grace_days` (pas pour une évaluation) | celles de la licence | « Ta licence PROFESSIONAL a pris fin le 2027-11-06 : ses fonctionnalités restent ouvertes encore 11 jours, le temps d'importer son renouvellement. Ensuite, l'édition FREE s'appliquera ; tes projets restent ouverts et enregistrables. » |
| **Expirée** | au-delà de la grâce | FREE | « Ta licence PROFESSIONAL a pris fin le 2027-11-06 : l'édition FREE s'applique. Ton projet reste ouvert ; Enregistrer et Exporter restent permis. Pour retrouver tes fonctionnalités, importe le renouvellement reçu de l'éditeur. » |
| **Révoquée** | dans la liste de révocation (toute la licence, ou ce PC) | FREE | « Ta licence XPG-… a été révoquée par l'éditeur le … (motif : Fin de contrat) : l'édition FREE s'applique. … » |
| **Invalide** | signature fausse ou clé inconnue, fichier tronqué, champ en double ou inconnu, édition ou fonctionnalité inconnue du registre, registre ou liste de révocation remplacés par des plus anciens, licence flottante ou liée à un service | FREE | « Le fichier de licence de ce PC n'est pas valide : l'édition FREE s'applique. … Demande un nouveau fichier à l'éditeur. » (+ `detail` : la raison précise) |
| **Autre PC** | la licence n'est pas liée à la clé de ce PC | FREE | « Cette licence (…) a été délivrée pour un autre PC : … Envoie la demande d'activation de ce PC à l'éditeur pour recevoir la tienne. » |
| **Absente** | aucune licence | FREE | « Aucune licence sur ce PC : l'édition FREE s'applique. » |
| **Horloge reculée** | licence datée, et l'heure du PC est en arrière de plus de 24 h sur l'heure la plus tardive vue | FREE | « L'horloge de ce PC indique le 2027-10-01, alors que le 2027-11-21 a déjà été vu : remets-la à l'heure. En attendant, l'édition FREE s'applique. … » |

**L'horloge** : l'heure la plus tardive vue est gardée dans `etat.bin` ; les dates d'émission des documents signés (licence, registre, révocations) servent aussi d'ancrage. Une licence perpétuelle ne dépend pas de l'heure. **Honnêtement** : un PC sans réseau ne peut pas prouver l'heure ; ce contrôle arrête le retour en arrière ordinaire de l'horloge, pas quelqu'un qui efface l'état **et** remet une horloge postérieure aux dates des documents. Le vrai garde-temps viendra avec le service (une heure signée).

Si le registre manque (il doit être livré avec le programme, § 13), il n'y a pas d'édition FREE : `getEdition()` rend `""` et rien n'est ouvert ; la phrase le dit.

## 9. L'API (`src/licensing/Licensing.hpp`)

```cpp
licensing::LicenseState licensing::initialize(licensing::StoreOptions options);   // au démarrage
licensing::LicenseState licensing::refresh();                                     // après un import, et une fois par heure
bool                    licensing::isFeatureEnabled(std::string_view featureId);
std::string             licensing::getEdition();          // "PROFESSIONAL" ; sans licence : "FREE" (le registre)
licensing::LicenseStatus licensing::getLicenseStatus();   // Valid, Grace, Expired, Revoked, Invalid, OtherDevice, Absent, ClockRolledBack
int                     licensing::getMaxDevices();       // 5
std::string             licensing::getExpirationDate();   // "2027-03-31" ; "" : pas de fin
std::string             licensing::getLicenseId();        // "XPG-2026-00042" ; "" sans licence
std::string             licensing::getStatusMessage();    // la phrase claire de l'état
std::string             licensing::getStatusLabel();      // "Valide", "Grâce", "Expirée", "Révoquée", "Invalide", "Autre PC", "Absente", "Horloge reculée"
std::optional<std::int64_t> licensing::getFeatureLimit(std::string_view featureId, std::string_view limit);
licensing::LicenseState licensing::getLicenseState();     // tout, pour l'écran Licence
std::string             licensing::lockedMessage(std::string_view featureId);   // la phrase d'une entrée verrouillée
licensing::ImportReport licensing::importLicenseFile(const std::string& path);
bool licensing::writeActivationRequest(const std::string& path, const std::string& deviceName,
                                       const std::string& licenseId, const std::string& software, std::string* why);
```

`StoreOptions` : `folder` (le dossier, § 7), `bundledRegistry` (le registre livré avec le programme : adopté s'il est plus récent), `trustedKeys` (vide : les clés de `PublisherKey.hpp` ; les essais y mettent leur clé), `clock` (vide : l'heure du système), `clockTolerance` (24 h). Les lectures ne touchent aucun fichier (le dernier état, sous verrou : appelables de n'importe quel fil). Avant `initialize()` : Absente, rien d'ouvert.

`LicenseState` porte tout ce qu'il faut pour afficher : `status`, `edition` et `editionLabel` (en vigueur), `licensedEdition` (celle de la licence, même finie), `licenseId`, `customer`, `type`, `maxDevices`, `boundDevices`, `issuedAt`, `expiresAt`, `graceEndsAt`, `graceDaysLeft`, `features` (ouvertes, avec leurs limites), `registryNumber`, `revocationNumber`, `deviceFingerprint`, `message`, `detail`, `registryProblem`.

## 10. Jamais de perte de travail (la règle du branchement des menus, 1.30.0)

- **Ouvrir, Enregistrer, Enregistrer sous, Exporter, Fermer, Quitter** ne portent **aucun** identifiant de fonctionnalité et ne demandent **jamais** `isFeatureEnabled`, quel que soit l'état (Expirée, Révoquée, Invalide, Autre PC, Absente, Horloge reculée). Les exports « avancés » éventuels (`advanced_export`) sont des entrées à part ; l'export ordinaire d'un projet reste toujours permis.
- Une licence qui prend fin pendant le travail **ne ferme rien** : le projet ouvert reste ouvert ; seules les **entrées** des fonctionnalités se ferment, au `refresh()` suivant, avec `lockedMessage(id)`.
- Une fonctionnalité fermée en cours d'usage (une génération IA lancée…) va à son terme ; c'est la suivante qui est refusée.
- Chaque entrée verrouillée garde sa place dans le menu, avec le cadenas « pas dans ta licence » (maquette) et la phrase ; jamais une entrée qui disparaît.
- Aucun nom de l'outil Admin dans le code des clients (le contrôle du paquet le refuserait).

## 11. L'outil de l'éditeur : `XPG-License-Admin`

```
XPG-License-Admin creer-cle [--essai] [--nom N] [--dossier D]
XPG-License-Admin cle [--dossier D]
XPG-License-Admin exporter-cle --sortie src/licensing/PublisherKey.hpp [--dossier D]
XPG-License-Admin signer-registre --entree registre.json --sortie registre.xpgreg [--dossier D]
XPG-License-Admin signer-revocations --entree revocations.json --sortie revocations.xpgrev [--dossier D]
XPG-License-Admin signer-licence --parametres P.json --demande PC.xpgreq [--demande …] [--licence ANCIENNE.xpglic]
                  --registre registre.xpgreg [--revocations revocations.xpgrev] --sortie L.xpglic [--dossier D]
XPG-License-Admin verifier FICHIER [--dossier D]
XPG-License-Admin --version
```

Code de sortie : 0 fait, 1 refusé (la raison sur la sortie d'erreur), 2 mauvais usage. Le dossier par défaut de la clé : `%LOCALAPPDATA%\XPG\LicenseAdmin` (sous Linux `~/.config/xpg-license-admin`).

- **La clé de l'éditeur** (`cle-editeur-licences.cle`) : Ed25519, la partie secrète protégée par **DPAPI pour ton compte Windows** ; elle ne quitte jamais le poste. `creer-cle` refuse d'en écraser une. Une clé qui ne se relit pas n'est jamais remplacée en silence.
- **Les paramètres d'une licence** (`P.json`) : `license_id`, `customer`, `edition` ; facultatifs (sinon ceux de l'édition) : `type`, `max_devices`, `features` (des identifiants, ou `{"id", "limits"}`), `issued_at`, `expires_at` ou `duration_days`, `offline_grace_days`. Un paramètre inconnu est refusé. Exemple : `tests/fixtures/licences/parametres-exemple.json`.
- **Le registre à signer** est le JSON du § 4 sans `format`, `version`, `issued_at` (ajoutés). Le numéro (`number`) est à augmenter à chaque publication : l'outil ne garde pas d'historique.
- **La clé d'ESSAI** : `creer-cle --essai` la nomme « CLE D'ESSAI XPG-CLE-ESSAI-LICENCES - NE PAS LIVRER » ; `PublisherKey.hpp` la marque (`test = true`) ; le contrôle du paquet refuse un programme qui l'embarque. La clé actuelle de `PublisherKey.hpp` (`xpg-cb0d1ae560b52310`) en est une ; sa partie privée n'est pas dans le dépôt.

**À faire sur ton poste Admin, avant la 1.30.0** : construire `xpg_license_admin` (sur ton PC seulement), puis `XPG-License-Admin creer-cle --nom "…"` ; garder une **copie chiffrée hors ligne** du fichier de la clé (sans elle, plus aucune licence ne peut être signée) ; `XPG-License-Admin exporter-cle --sortie src/licensing/PublisherKey.hpp` ; signer le registre réel ; committer `PublisherKey.hpp` (la clé publique seulement).

**Rotation** : `PublisherKey.hpp` peut lister plusieurs clés (`kKeys`) ; les clients croient chacune. Une nouvelle clé s'ajoute à côté de l'ancienne pendant la transition ; une clé retirée s'enlève (ses licences tombent).

## 12. Ce qui est prévu pour plus tard, et pourquoi il faut un service hébergé

Le format prévoit `type: "floating"` et `service` ; rien ne prétend les faire : une telle licence est lue puis refusée, avec la phrase qui le dit.

- **Les licences flottantes** (N jetons simultanés) demandent un **compteur partagé avec un verrou atomique** : deux postes qui démarrent en même temps ne doivent pas prendre le dernier jeton tous les deux. Un fichier ou un dépôt GitHub n'a ni verrou atomique ni contrôle d'accès par client ; il faut une base transactionnelle derrière un service (baux de 45 min renouvelés, jeton emprunté avant de partir hors ligne, maquette).
- **L'activation en ligne** (une clé de licence tapée, sans échange de fichiers), la **désactivation** d'un PC qui libère sa place tout de suite, le **transfert** validé par l'Admin : il faut quelqu'un qui réponde, et qui tienne le compte des PC activés.
- **Une heure de confiance** : un service peut signer l'heure ; hors ligne, un PC ne prouve rien (§ 8).
- **Une révocation rapide** : sans service, une révocation n'arrive qu'avec un fichier importé.
- **La vérification du poste Admin par défi signé** (maquette : « sans réponse valide, toutes les fonctions sont bloquées ») suppose le service, qui connaît la clé publique du poste.

L'hébergement (un petit serveur, ou une fonction en ligne avec sa base) n'est pas décidé ; le format est prêt à l'accueillir sans changer les licences par fichier.

## 13. Le contrôle du paquet

`outils/mingw/package_mingw.py` contrôle le dossier à livrer **après le staging et pour chaque zip portable** ; un seul écart arrête la construction, avec la liste. Il cherche, dans les noms et dans les octets de chaque fichier (en ASCII et en UTF-16 : les chaînes larges des exe) :
- le nom de l'outil (`XPG-License-Admin`, « XPG License Manager Admin », un nom de fichier qui le rappelle) ;
- sa marque `XPG-LICENSE-ADMIN-NE-JAMAIS-LIVRER` (dans l'exe et dans le fichier de la clé de l'éditeur) ;
- la marque d'une clé d'essai `XPG-CLE-ESSAI-LICENCES` (un client qui embarque la clé d'essai n'est pas livrable) ;
- une clé privée : un bloc PEM « PRIVATE KEY » **suivi de son contenu** (l'en-tête seul est une chaîne de mbedTLS, présente dans les exe : ce n'est pas une clé), un fichier `*.cle`, `cle_editeur.bin` ;
- un jeton GitHub (`ghp_…`, `github_pat_…`).

`python3 outils/mingw/package_mingw.py --controler <dossier>` fait le contrôle seul (l'essai s'en sert). Sur le dossier de livraison de la 1.28.0 : aucun écart. `cross_mingw.sh` ne construit pas l'outil Admin.

**Pour la 1.30.0** : le même contrôle dans `outils/lib/package.ps1` (la construction sous Windows) ; quand les Studios embarqueront `licensing/`, le contrôle refusera la livraison tant que `PublisherKey.hpp` porte la clé d'essai — c'est voulu.

## 14. Les essais

`licensing_test <XPG-License-Admin> <racine du dépôt>` (ctest `licensing`) : une clé d'éditeur d'essai créée dans un dossier temporaire, des PC simulés (un dossier chacun, une paire de clés chacun), l'heure donnée par l'essai.
- les formats (dates, enveloppe tronquée à six longueurs, genre inconnu, CRLF, la signature liée au genre) ;
- la clé de l'éditeur (essai marquée partout, jamais écrasée, `PublisherKey.hpp` généré en ASCII) ;
- le registre de la maquette signé, relu, réécrit à l'identique ; ses refus ;
- un PC de bout en bout : Absente sans registre, FREE sans licence, la demande (rien du matériel, clé 0600 ou DPAPI), la licence, l'API, la grâce, l'expiration, l'horloge reculée (et l'ancrage des documents quand l'état est effacé) ;
- chaque refus : signature fausse, clé inconnue, autre PC, tronquée, champ en double, champ inconnu, édition et fonctionnalité inconnues, flottante, service en ligne, registre plus ancien (import et disque), registre livré plus récent adopté, révocation (licence, un seul PC), liste plus ancienne, liste remplacée ou effacée, émission plus ancienne ;
- les fonctionnalités de chaque édition, exactement celles du registre ; les limites ; perpétuelle ; plusieurs PC (étendre, trop de PC), demande modifiée en chemin ou pour une autre licence ;
- l'outil Admin en **sous-processus**, de bout en bout : clé, export, registre, demande → licence → import → vérification → révocation ; ses refus et codes de sortie ;
- le contrôle du paquet : un dossier propre passe (avec l'en-tête PEM de mbedTLS), et chacun de neuf écarts arrête la construction (l'exe par son nom, l'exe renommé, la marque, le nom en UTF-16, une clé PEM, la clé de l'éditeur, une clé de PC, la clé d'essai embarquée, un jeton).

Sous Linux : 154 contrôles. Sous Wine (exe MinGW, DPAPI machine et compte réels) : 142 contrôles, le contrôle du paquet sauté (pas de python3 dans Wine) — l'essai le dit (« SAUTE »), il ne le compte pas comme réussi.
