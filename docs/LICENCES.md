# Licences — conception (préparées en 1.29.0, branchées en 1.30.0)

Ce document décrit les licences : le format des fichiers signés, le registre des éditions et fonctionnalités, l'identité d'un PC, la vérification hors ligne et ses états (dont **« Non activées »** : un programme sans clé d'éditeur n'est jamais bloqué), l'activation par fichier, les deux fichiers de clés et l'option CMake de la clé d'essai, ce que lit l'écran Licence (registre, PC liés, historique), l'outil de l'éditeur (livré à part, avec son registre des activations), l'installateur et le contrôle du paquet. Il reprend la maquette validée (onglets Studio, Admin, Client, Architecture) et les décisions prises avec toi :
- **version 1 sans service en ligne** : licences signées, vérifiées hors ligne, activation **par fichier** ;
- **rien n'est écrit en dur** : les éditions, les fonctionnalités et leurs droits viennent d'un **registre signé** et numéroté ; le code ne porte que des identifiants de fonctionnalités (`ai_generation`, `deploy_network`…) ;
- **un PC est une paire de clés** créée sur lui (Ed25519, clé secrète sous DPAPI machine) ; jamais son matériel ;
- **une clé d'éditeur** (Ed25519) signe licences, registres et révocations ; sa clé publique est compilée dans les logiciels clients ; sa clé privée ne quitte pas le poste Admin.

Retour au [README](../README.md). Voir aussi [DEPLOIEMENT.md](DEPLOIEMENT.md) (la clé d'éditeur du développeur, `deploy/DevKey`, qui signe les paquets : ce n'est pas la même clé).

## 1. Où on en est

| | Fait | Pas fait |
|---|---|---|
| Bibliothèque | 1.29.0 : `src/licensing/` (`xpg_licensing`) : formats, registre, licence, révocations, identité du PC, demande d'activation, import, vérification hors ligne, états, l'API de la maquette. **1.30.0** : l'état **Non activées** (aucune clé de l'éditeur : tout est ouvert, § 8 bis), ce que lit l'écran Licence (le registre, les PC liés, l'historique, le dossier, l'empreinte de ce PC, § 9), l'historique de ce PC (`historique.jsonl`), un dossier non inscriptible qui ne bloque rien | — |
| Clés de l'éditeur | **1.30.0** : deux fichiers (§ 11 bis) — `PublisherKey.hpp`, les **vraies** clés, **vide** aujourd'hui ; `TestPublisherKey.hpp`, la clé d'**essai**, compilée **seulement** avec l'option CMake `XPG_LICENCES_CLE_ESSAI` (OFF par défaut) | **la vraie clé**, à créer sur ton PC (§ 11) ; d'ici là, les programmes livrés sont « Non activées » |
| Outil Admin | `src/license_admin/` : `XPG-License-Admin` en console (clé de l'éditeur, `exporter-cle` vide ou à plusieurs clés, signer registre / licence / révocations, vérifier). **1.30.0** : le **registre des activations** (`activations.jsonl`, `enregistrer-demande`, `liste`, `--csv`) ; **livré à part à l'éditeur** (`outils/mingw/admin_editeur.sh` : `XPG-License-Admin-<version>-EDITEUR.zip`, avec son LISEZ-MOI pas à pas) | l'interface graphique de l'Admin (maquette : tableau de bord, clients, boutique…), la vérification du poste Admin par défi signé, la publication dans le dépôt GitHub, une sauvegarde de la clé protégée par un mot de passe |
| Interface | **1.30.0** : les Studios lient `xpg_licensing` (l'écran Licence et les verrous des entrées : `src/app/Licence*`) | — |
| Installateur | **1.30.0** : `%ProgramData%\XPG\Licences` créé pour tous les comptes, avec le droit Modifier des Utilisateurs ; gardé à la désinstallation ; le registre livré (`licences\registre.xpgreg`) s'il est dans le dossier à livrer ; le contrôle du paquet dans `package_mingw.py` **et** `package.ps1` (§ 13) | le registre réel, signé par toi (`ressources-licences/registre.xpgreg`) |
| En ligne | le format le prévoit (`type: floating`, champ `service`) ; une licence qui les emploie est **refusée** avec une phrase qui le dit | l'activation en ligne, les licences flottantes, la désactivation et le transfert en ligne, l'heure signée, **voir chaque lancement du logiciel** (§ 12) |

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

**Le dossier** (`LicenseStore::defaultFolder()` : `%ProgramData%\XPG\Licences` sous Windows ; ailleurs `$XDG_DATA_HOME/xpg/licences` ou `~/.local/share/xpg/licences`) : `identite-pc.cle`, `licence.xpglic`, `registre.xpgreg`, `revocations.xpgrev`, `etat.bin` (protégé par DPAPI machine : l'heure la plus tardive vue, les numéros les plus hauts vus), `historique.jsonl` (1.30.0, § 9 bis). Copier ce dossier sur un autre PC ne sert à rien : la clé du PC ne s'y relit pas (DPAPI) et la licence n'est pas liée à la clé de l'autre PC.

**Qui peut y écrire** (1.30.0) : l'installation **pour tous les comptes** crée le dossier et donne au groupe Utilisateurs le droit Modifier, hérité (§ 13 bis) : chaque compte importe une licence et écrit la demande de ce PC. L'installation **pour toi seul** ne fait rien : le **repli** est le même chemin, que le programme crée au premier besoin (une demande, un import, la première évaluation) ; Windows en donne alors la propriété au compte qui l'a créé, et un **autre compte** de ce PC le lit sans pouvoir y écrire. Pour ce compte-là, l'évaluation marche quand même (elle ne fait que lire : la licence vaut, le registre et l'horloge sont vérifiés) ; `detail` le dit (« ce compte ne peut pas écrire dans le dossier des licences de ce PC (…) : l'état protégé ne s'écrit pas (…), l'historique ne s'écrit pas (…) ; une installation pour tous les comptes donne ce droit à chaque compte ») ; l'import et la demande sont refusés avec la même raison.

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
## 9. L'API (`src/licensing/Licensing.hpp`)

```cpp
licensing::LicenseState licensing::initialize(licensing::StoreOptions options);   // au démarrage
licensing::LicenseState licensing::refresh();                                     // après un import, et une fois par heure
bool                    licensing::isFeatureEnabled(std::string_view featureId);
std::string             licensing::getEdition();          // "PROFESSIONAL" ; sans licence : "FREE" (le registre)
licensing::LicenseStatus licensing::getLicenseStatus();   // Valid, Grace, Expired, Revoked, Invalid, OtherDevice, Absent, ClockRolledBack, NotConfigured
int                     licensing::getMaxDevices();       // 5
std::string             licensing::getExpirationDate();   // "2027-03-31" ; "" : pas de fin
std::string             licensing::getLicenseId();        // "XPG-2026-00042" ; "" sans licence
std::string             licensing::getStatusMessage();    // la phrase claire de l'état
std::string             licensing::getStatusLabel();      // "Valide", "Grâce", "Expirée", "Révoquée", "Invalide", "Autre PC", "Absente", "Horloge reculée", "Non activées"
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

## 12. Ce qui est prévu pour plus tard, et pourquoi il faut un service hébergé

Le format prévoit `type: "floating"` et `service` ; rien ne prétend les faire : une telle licence est lue puis refusée, avec la phrase qui le dit.

- **Les licences flottantes** (N jetons simultanés) demandent un **compteur partagé avec un verrou atomique** : deux postes qui démarrent en même temps ne doivent pas prendre le dernier jeton tous les deux. Un fichier ou un dépôt GitHub n'a ni verrou atomique ni contrôle d'accès par client ; il faut une base transactionnelle derrière un service (baux de 45 min renouvelés, jeton emprunté avant de partir hors ligne, maquette).
- **L'activation en ligne** (une clé de licence tapée, sans échange de fichiers), la **désactivation** d'un PC qui libère sa place tout de suite, le **transfert** validé par l'Admin : il faut quelqu'un qui réponde, et qui tienne le compte des PC activés.
- **Une heure de confiance** : un service peut signer l'heure ; hors ligne, un PC ne prouve rien (§ 8).
- **Une révocation rapide** : sans service, une révocation n'arrive qu'avec un fichier importé.
- **La vérification du poste Admin par défi signé** (maquette : « sans réponse valide, toutes les fonctions sont bloquées ») suppose le service, qui connaît la clé publique du poste.

- **Voir chaque lancement du logiciel** (même sans activation : qui l'utilise, quelle version, quand) : le registre des activations de l'Admin (§ 11) ne voit que les demandes que les clients lui envoient. Voir chaque lancement demande le **service de licences en ligne** : à chaque lancement, le client enverrait l'identifiant de sa licence (s'il en a une), l'**empreinte de la clé du PC**, la version du logiciel et la date — **jamais le matériel** (ni nom réseau, ni adresse MAC, ni numéro de série). Il faudra aussi **informer clairement les utilisateurs** (RGPD : une mention dans les conditions d'utilisation, ce qui est envoyé, pourquoi, combien de temps c'est gardé). Prévu avec l'hébergement ; **pas fait**.

L'hébergement (un petit serveur, ou une fonction en ligne avec sa base) n'est pas décidé ; le format est prêt à l'accueillir sans changer les licences par fichier.

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

Sous Linux : **239** contrôles (1.29.0 : 154). Sous Wine (exe MinGW) en 1.29.0 : 142 contrôles, le contrôle du paquet sauté ; en 1.30.0, `licensing_test` n'a pas été refait sous Wine (`build-mingw` est configuré sans les essais) — l'outil Admin MinGW, lui, a été essayé sous Wine (§ 11 ter : `--version`, `creer-cle --essai`, `cle`, `exporter-cle` vers les deux fichiers, `--garder`, `--vide`, `signer-registre`, `verifier`).
