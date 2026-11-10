# Les statistiques et le globe (1.47.0) : le contrat

> **En construction pour la 1.47.0** (le service, l'Admin, ses essais). Ce document est le contrat entre le service des
> licences (`service/`, `service/src/stats.js`), le client C++ de l'Admin (`src/license_admin/Stats.hpp`,
> `gui/WorkspaceStats.cpp`) et les pages de l'Admin (la page « Statistiques », la fiche d'un client, la fiche et la vue
> d'ensemble des admins, le globe). Il suit la maquette validée `docs/maquettes/1.47.0/` (planches 00 à 05 et 08 à 11)
> et la liste `stats-admins-liste` (chaque chiffre y porte sa source).

## 1. Le principe

Chaque chiffre se regarde **sur une période** (7 j, 30 j, 90 j, 12 mois, tout, ou deux dates), **par pas** (jour,
semaine, mois) et **se compare** (à la période d'avant, ou aux mêmes dates l'an dernier, ou à rien). Trois sources :

| Marque | Source | Qui calcule |
|---|---|---|
| **[S]** | les tables du service qui existaient déjà : licences (le bloc signé de la dernière émission : client, offre, options, achats), activations, demandes, découvertes, commandes, versions publiées, logiciels vus | le service (`GET /admin/stats/…`) |
| **[N]** | ce que le service relève **en plus depuis la 1.47.0** : les faits (§ 9), le relevé de chaque jour, les compteurs du jour, la première fois qu'un PC est vu avec chaque version, la ville de la dernière connexion de chaque PC, les heures de travail des admins | le service |
| **[J]** | le journal des admins (signé, chaîné, déjà là) : les actions par type et par criticité, les jours actifs, la carte jour × heure des actions, les droits utilisés ou non, les signatures refusées, les actions depuis un autre PC, les heures inhabituelles, les pics, la chaîne vérifiée, les actions réservées de l'admin+ | **l'Admin lui-même** (`src/license_admin/JournalStats.*`) — pas ce contrat |

Ce qui a déjà une date dans les tables d'avant la 1.47 est **reconstitué** pour le passé (les licences : leur émission et
l'historique des achats de leur bloc ; les activations : `activee`, et `vue` pour un PC libéré ou retiré ; les
découvertes : `debut` ; les commandes : `paye`, `maj`) ; ce qui ne l'est pas commence au premier lancement de la 1.47
(`releves_depuis` dans chaque réponse). Avant cette date, un chiffre reconstitué peut être un peu bas (une activation
finie est effacée une semaine après la fin de son bail ; une demande close, après 7 jours) : les pages le disent
(« reconstitué avant le 10/11/2026 »).

**Les unités** : les montants en **centimes TTC** (la devise de la boutique, `eur`), les durées en **secondes**, les
jours en **jours UTC** écrits `AAAA-MM-JJ` (une période est comprise : `du` et `au` inclus), les instants en **secondes
Unix** (comme les routes de la boutique), les heures d'un admin en **heures UTC** (0 à 23 ; l'Admin les décale à l'heure
du PC pour les montrer). Un point inconnu vaut `null` (en C++ : `NaN`).

**La vie privée** : le service ne garde **jamais l'adresse IP** d'un PC, ni un historique de ses lieux : seulement la
ville de sa **dernière** connexion (§ 8). Aucun chiffre n'ajoute de donnée personnelle à ce que l'Admin a déjà (les noms
de clients sont ceux des licences et des commandes ; les admins sont nommés par l'identifiant de leur clé, `xpg-…`).

## 2. Les versions

- **Service 1.47.0, schéma 5** (`GET /v1/etat` : `version`). Le schéma 5 n'ajoute **que des tables** (§ 9) et deux
  index sur `decouvertes` ; la migration 4 → 5 est faite au premier lancement (rien n'est effacé) : elle note
  `stats_debut` (le début des relevés) et recopie `logiciels` dans `versions_vues` (la version vue de chaque clé, datée
  de sa dernière connexion : une approximation, dite par `releves_depuis`).
- **Une sauvegarde** du schéma 3 ou 4 se restaure dans un service 1.47 (ses tables de statistiques restent vides,
  `stats_debut` = la fin de la restauration) ; une sauvegarde du schéma 5 ne se restaure pas dans un service 1.46.
- **L'Admin 1.47 face à un service 1.46** : les autres pages marchent ; la page Statistiques, le globe et les chiffres
  [S]/[N] des fiches disent **« Ton service en ligne est en 1.46.0 : déploie la 1.47.0 pour les statistiques et le
  globe. »** (`stats::kServiceRequired = "1.47.0"`, `stats::serviceTooOldText`, comme `shop::kServiceRequired`) avec le
  bouton Déployer ; les chiffres [J] (le journal) restent. Une route inconnue (404 `route`) donne la même phrase.
- **Un Studio** n'a rien à changer : le service relève ses appels signés tels qu'ils sont.

## 3. Les périodes, les pas, la comparaison

- `du`, `au` : deux jours (`AAAA-MM-JJ`), `du` ≤ `au`, entre 2000-01-01 et aujourd'hui + 400 jours (une période peut
  être dans le futur : les échéances). Absents : les 30 derniers jours (aujourd'hui compris).
- `pas` : `jour` (chaque jour), `semaine` (du lundi au dimanche ; la première et la dernière semaine sont coupées à la
  période), `mois` (le mois du calendrier, coupé de même). **400 points au plus** (sinon 400 `champ` : « choisis la
  semaine ou le mois »).
- `comparer` : `avant` (la période de même longueur juste avant : `du` − n jours … `du` − 1), `an` (les mêmes dates un
  an plus tôt ; le 29 février devient le 28) ou `rien`. Les points de la comparaison sont **alignés** sur ceux de la
  période (chaque période du pas décalée de la même façon) : `avant.points[i]` se compare à `points[i]`.
- **Un flux** (des ventes, des PC activés…) : la somme de ce qui arrive pendant chaque période du pas ; `total` : sur
  toute la période. **Un niveau** (licences actives, PC actifs…) : la valeur **à la fin** de chaque période du pas (le
  relevé du jour suivant, sinon reconstitué ; la période en cours : maintenant) ; `total` : le dernier point. **Un
  ratio** (panier moyen, délais) : calculé sur chaque période du pas ; `total` : sur toute la période. **Un maximum**
  (le pic des jetons) : le plus grand des jours de la période du pas.
- `variation` (dans chaque mesure) : `(total − avant.total) / avant.total × 100`, `null` sans comparaison ou si
  `avant.total` vaut 0.

## 4. `GET /admin/stats/serie` — les courbes

Signée par l'admin+ ou un admin (comme toutes les routes `/admin`). Paramètres (dans l'adresse) :

| Paramètre | Valeurs |
|---|---|
| `mesures` | une ou plusieurs mesures du § 5, séparées par des virgules (20 au plus) |
| `du`, `au`, `pas`, `comparer` | § 3 (défauts : 30 derniers jours, `jour`, `avant`) |
| `par` | la division : `offre`, `option`, `article`, `admin`, `client`, `logiciel`, `version`, `type` (fixe, flottante) ; absent : aucune. Une mesure qui ne se divise pas ainsi rend ses points sans `parts` |
| `client` | ne compter que ce client (le `customer` des licences, la `societe` des commandes ; la casse et les espaces ne comptent pas) — la fiche d'un client |
| `admin` | ne compter que ce qu'a fait cette clé (`xpg-` + 16 hexadécimaux) — la fiche d'un admin |
| `licence` | ne compter que cette licence |
| `logiciel` | `studios` (IHM-Studio et API-Studio), `admins` (XPG-License-Admin), ou un nom de logiciel |

Réponse 200 :
```json
{
  "version": "1.47.0", "heure": 1794326400, "releves_depuis": "2026-11-10",
  "du": "2026-08-12", "au": "2026-11-10", "pas": "semaine", "par": "offre", "comparer": "an",
  "periodes": [{"du": "2026-08-12", "au": "2026-08-16"}, {"du": "2026-08-17", "au": "2026-08-23"}],
  "avant": {"du": "2025-08-12", "au": "2025-11-10",
            "periodes": [{"du": "2025-08-12", "au": "2025-08-16"}, {"du": "2025-08-17", "au": "2025-08-23"}]},
  "mesures": {
    "ventes_montant": {
      "unite": "centimes", "genre": "flux", "source": "S",
      "points": [184000, 252000], "total": 2602000,
      "parts": [{"cle": "PROFESSIONAL", "points": [98000, 147000], "total": 1225000},
                {"cle": "options", "points": [2900, 11600], "total": 260000}],
      "avant": {"points": [150000, 209000], "total": 2300000},
      "variation": 13.1
    },
    "demandes_traitees": {"refuse": "droit", "droits": ["demandes"]}
  }
}
```
- `parts` : les 50 plus grandes clés (par `total`), puis `"…"` (le reste, s'il y en a) ; la clé `""` : ce qui n'a pas
  cette dimension (une commande d'offre dans `par=option`, une licence sans signataire connu…).
- `avant` n'est là qu'avec une comparaison ; il n'a pas de `parts`.
- Une mesure qu'un **admin** n'a pas le droit de voir : `{"refuse": "droit", "droits": [...]}` (le reste répond).
- Erreurs : 400 `champ` (une mesure inconnue, un jour illisible, trop de points, une division inconnue), 401/403 comme
  les autres routes `/admin`.

## 5. Les mesures

`S` : reconstitué des tables ; `N` : relevé depuis la 1.47 ; `S+N` : relevé depuis la 1.47, reconstitué avant. Les
divisions possibles sont entre crochets. **Droit** : ce qu'il faut à un admin (l'admin+ voit tout).

### Les ventes (droit `demandes`) — la boutique en ligne

| Mesure | Unité, genre | Source | Ce que c'est |
|---|---|---|---|
| `ventes_montant` | centimes, flux | S | le montant encaissé : les commandes payées (au moment du paiement, `paye`), même remboursées ensuite [offre (l'édition, ou `options`), option, article, client, admin (qui l'a livrée)] |
| `ventes_cumul` | centimes, niveau | S | le montant encaissé depuis le 1er janvier de l'année de chaque point (« cumulé sur l'année ») [les mêmes] |
| `ventes_commandes` | nombre, flux | S | les commandes payées [les mêmes] |
| `ventes_panier` | centimes, ratio | S | le panier moyen : montant ÷ commandes |
| `ventes_remboursements` | centimes, flux | S | le montant des commandes remboursées (au remboursement) [les mêmes] |
| `ventes_remboursements_nombre` | nombre, flux | S | les commandes remboursées |
| `licences_vendues_en_ligne` | nombre, flux | S | les commandes d'une offre payées [offre, client, admin] |
| `licences_signees_main` | nombre, flux | S+N | les licences neuves qui ne viennent pas d'une commande (signées à la main) [offre, client, admin] |

### Les licences (sans droit)

| Mesure | Unité, genre | Source | Ce que c'est |
|---|---|---|---|
| `licences_actives` | nombre, niveau | S+N | actives : créées, pas désactivées, pas expirées, pas révoquées en entier [offre, client, admin (la clé qui a signé l'émission en place), type] |
| `licences_nouvelles` | nombre, flux | S+N | créées (le premier dépôt ; avant 1.47 : le premier achat « offer » de son historique, sinon son émission) [les mêmes] |
| `licences_prolongees` | nombre, flux | S+N | une émission plus récente qui finit plus tard (avant 1.47 : les achats « extension ») [les mêmes] |
| `licences_options` | nombre, flux | S+N | une émission qui ajoute une option (avant 1.47 : les achats « option ») [option, offre, client, admin] |
| `licences_expirees` | nombre, flux | S | les licences dont la fin tombe dans la période — **dans le futur aussi : les échéances** (`du` = aujourd'hui, `au` = +90 j) [offre, client, admin, type] |
| `licences_echeances_montant` | centimes, flux | S | le même compte, chaque licence comptée au prix de son offre dans le catalogue de la boutique (`ed:<offre>` ; 0 si elle n'y est pas) : « 11 960 € à renouveler » |
| `licences_revoquees` | nombre, flux | S+N | les licences révoquées en entier (avant 1.47 : la date de la liste de révocation, `revoked_at`) [offre, client, admin (qui a déposé la liste)] |

### Les demandes en ligne (droit `demandes`)

| Mesure | Unité, genre | Source | Ce que c'est |
|---|---|---|---|
| `demandes_recues` | nombre, flux | S+N | les demandes déposées par les PC (avant 1.47 : celles encore dans la table) [client (d'après les licences du PC)] |
| `demandes_traitees` | nombre, flux | N | les demandes traitées [admin, client] |
| `demandes_acceptees`, `demandes_refusees` | nombre, flux | N | les traitées dont l'Admin 1.47 a dit l'issue (`issue` de `POST /admin/demandes/traiter`) [admin] |
| `demandes_delai_median`, `demandes_delai_moyen`, `demandes_delai_max` | secondes, ratio | N | le délai entre la demande et son traitement [admin] |
| `demandes_attente` | nombre, niveau | S+N | les demandes en attente |
| `demandes_plus_ancienne` | secondes, niveau | S+N | l'âge de la plus ancienne qui attend (0 : aucune) |

### Les PC (sans droit)

| Mesure | Unité, genre | Source | Ce que c'est |
|---|---|---|---|
| `pc_actifs` | nombre, niveau | S+N | les PC actifs : une activation en cours (dans son bail, pas libérée, pas retirée, pas révoquée) — un PC actif sur deux licences compte deux fois [offre, client, admin, type, logiciel] |
| `pc_actives` | nombre, flux | S+N | les PC activés (une activation neuve, ou reprise après une libération) [les mêmes] |
| `pc_liberes` | nombre, flux | S+N | libérés par leur client [les mêmes] |
| `pc_retires` | nombre, flux | S+N | retirés par un admin, révoqués, ou repris au renouvellement (leur place donnée à un autre) [les mêmes] |
| `jetons_pic` | nombre, maximum | N | les jetons flottants pris **en même temps** (toutes les licences flottantes), le pic du jour |
| `jetons_plafond` | nombre, niveau | S+N | la somme des postes des licences flottantes actives (le plafond) |
| `licences_au_plafond` | nombre, flux | N | les (jour, licence flottante) où tous ses jetons ont été pris : « 3 jours au plafond » [licence, client] |
| `decouvertes_commencees` | nombre, flux | S | les Découvertes (l'essai) commencées |
| `decouvertes_converties` | nombre, flux | S+N | les Découvertes dont le PC a ensuite activé une licence |
| `decouvertes_en_cours` | nombre, niveau | S+N | les Découvertes commencées depuis moins de `DECOUVERTE_JOURS` jours (30), pas converties — la ligne « Découverte » des PC par offre |

### Les mises à jour (sans droit) — et `GET /admin/stats/mises-a-jour` (§ 6)

| Mesure | Unité, genre | Source | Ce que c'est |
|---|---|---|---|
| `pc_vus` | nombre, niveau | S+N | les clés (PC des Studios, Admins) vues dans les 30 jours avant chaque point [logiciel, version] |
| `pc_a_jour` | nombre, niveau | S+N | parmi elles, celles sur la dernière version publiée à ce moment (ou plus récente) [logiciel] |
| `maj_installees` | nombre, flux | N | les clés vues pour la première fois avec une version (une mise à jour, ou une installation) [version, logiciel] |

### Les admins (sans droit ; un admin ne voit que lui) — et `GET /admin/stats/admins` (§ 7)

| Mesure | Unité, genre | Source | Ce que c'est |
|---|---|---|---|
| `admin_appels` | nombre, flux | N | les appels signés au service (l'Admin ouvert regarde toutes les 15 s) [admin] |
| `admin_jours` | nombre, flux | N | les jours où il s'est connecté (au moins un appel) [admin] |

**Ce qu'un admin a produit** (la fiche, planche 04) se lit avec `admin=<id>` : `licences_nouvelles`,
`licences_prolongees`, `licences_options`, `licences_actives`, `licences_expirees` (et à venir), `licences_revoquees`,
`pc_actifs` (sur ses licences), `demandes_traitees`, `demandes_acceptees`/`_refusees`, les délais,
`ventes_montant`/`ventes_commandes`/`ventes_panier`/`ventes_remboursements` (les commandes qu'il a livrées) ; ses
5 premiers clients : `licences_nouvelles` avec `par=client`.
**La fiche d'un client** (planche 10) : `client=<nom>` sur `licences_actives`, `licences_nouvelles`,
`licences_prolongees`, `licences_options`, `licences_expirees`, `pc_actifs`, `ventes_montant`, `ventes_cumul`,
`ventes_commandes`, `demandes_recues`, `demandes_traitees`, `pc_vus` avec `par=version` (« la version de ses PC,
chaque semaine »). « Qui l'a servi » : `demandes_traitees`, `licences_nouvelles` avec `par=admin`, et le journal [J].

## 6. `GET /admin/stats/mises-a-jour` — les versions (planche 09)

Paramètres : `du`, `au`, `pas` (la part des PC à jour dans le temps), `jours` (la longueur des courbes d'adoption, 1 à
120, défaut 42), `logiciel` (`studios`, `admins`, `tous` ; défaut `tous`). Réponse 200 :
```json
{
  "version": "1.47.0", "heure": 1794326400, "releves_depuis": "2026-11-10",
  "derniere": {"version": "1.47.0", "publiee": 1793750400},
  "versions": [
    {"version": "1.47.0", "publiee": 1793750400, "studios": 168, "admins": 2},
    {"version": "1.45.2", "publiee": 1788220800, "studios": 19, "admins": 1},
    {"version": "1.43.2", "publiee": 0, "studios": 6, "admins": 0}
  ],
  "vus": {"studios": 286, "admins": 3, "depuis": "2026-07-01"},
  "a_jour": {"studios": 168, "admins": 2},
  "adoption": [
    {"version": "1.47.0", "publiee": 1793750400, "logiciel": "studios", "base": 286, "jours": [12, 45, 80, 101, 130, 150, 168]},
    {"version": "1.47.0", "publiee": 1793750400, "logiciel": "admins", "base": 3, "jours": [1, 2, 2, 2, 2, 2, 2]}
  ],
  "delais": [{"version": "1.47.0", "logiciel": "studios", "nombre": 120, "median": 172800, "moyen": 224640}],
  "periodes": [{"du": "2026-07-01", "au": "2026-07-01"}],
  "part": {"studios": {"a_jour": [170], "vus": [286]}, "admins": {"a_jour": [2], "vus": [3]}},
  "pas_a_jour": [{"cle": "<64 hexa>", "logiciel": "IHM-Studio", "version": "1.43.2", "retard": 4, "vu": 1790784000,
                  "nom": "ATELIER-PC3", "client": "Atelier Exemple", "licence": "XPG-2024-00031", "admin": ""}],
  "pas_a_jour_total": 119
}
```
- **Un PC compte pour la version de sa dernière connexion** (`logiciels`), s'il a été vu dans la période (`vus`,
  `versions`). Une version vue mais jamais publiée (une version de développement, ou publiée avant la 1.44) : `publiee`
  vaut 0. `versions` : la plus récente d'abord.
- `adoption` (pour chaque version publiée qui a commencé dans les 400 derniers jours, et chaque groupe `studios`,
  `admins`) : `jours[k]` = les clés du groupe vues avec cette version **ou une plus récente** au plus tard `k` jours
  après sa publication ; `base` = les clés du groupe vues dans les 30 jours avant sa publication, ou depuis (le 100 %).
  La courbe s'arrête à la publication de la version suivante, ou aujourd'hui, ou à `jours`.
- `delais` : pour les clés qui avaient une version plus ancienne, le temps entre la publication et la première fois
  qu'elles sont vues avec cette version (ou une plus récente) ; médiane et moyenne en secondes.
- `part` : la part des PC à jour dans le temps (les mesures `pc_a_jour` / `pc_vus`, divisées par groupe), un point par
  période du pas.
- `pas_a_jour` : les clés vues dans la période qui ne sont pas sur la dernière version, les plus en retard d'abord
  (`retard` : le nombre de versions publiées plus récentes), 500 au plus (`pas_a_jour_total` : combien en tout) ; `nom`,
  `client`, `licence` : de son activation la plus récente ; une clé d'admin : `admin` (son `xpg-…`).

## 7. `GET /admin/stats/admins` — leurs jours et leurs heures (planches 01 à 03)

Paramètres : `du`, `au`. L'admin+ reçoit tous les admins de la liste (même retirés) et les clés de l'éditeur ; un
admin, seulement lui. Réponse 200 :
```json
{
  "du": "2026-10-12", "au": "2026-11-10", "releves_depuis": "2026-11-10",
  "admins": [
    {"id": "xpg-1f2e3d4c5b6a7980", "cle": "<64 hexa>", "role": "admin", "nom": "Léa Essai", "retire": 0,
     "jours": ["2026-11-02", "2026-11-03"], "heures": [0,0,0,0,0,0,0,12,40,55,61,30,8,20,48,52,40,22,4,0,0,0,0,0],
     "appels": 392, "debut_habituel": 7, "fin_habituelle": 17, "vu": 1794320000,
     "logiciel": "XPG-License-Admin", "logiciel_version": "1.47.0", "ville": "Lyon", "pays": "FR"}
  ]
}
```
`heures` : les appels de chaque heure UTC sur la période ; `debut_habituel` / `fin_habituelle` : la médiane, sur ses
jours de connexion, de la première et de la dernière heure UTC (−1 : aucun jour). `role` : `admin+` pour une clé de
l'éditeur. `ville`, `pays` : sa dernière connexion (§ 8). `vu`, `logiciel`, `logiciel_version` : son dernier appel
(`logiciels`). Une période de plus de 731 jours : 400 `champ`.

## 8. Le globe (planche 11)

### D'où vient le lieu
Le **Worker** lit, pour chaque appel, la ville que Cloudflare donne (`request.cf` : `city`, `region`, `country`,
`latitude`, `longitude`, `colo`) et la passe à l'objet dans un en-tête interne `X-XPG-Lieu` (qu'il efface toujours de
l'appel reçu : un client ne peut pas le poser). **L'adresse IP n'est jamais gardée.** Le service ne garde, **par clé**,
que le lieu de sa **dernière** connexion signée (la table `lieux` : ville, région, pays, latitude et longitude
**arrondies au dixième de degré** (~10 km), le centre de données ; jamais le code postal, ni l'opérateur, ni un
historique) ; une clé qui n'appelle plus depuis un an est oubliée. La ligne est réécrite quand le lieu change, sinon une
fois par heure au plus (le « vu ») ; « actif maintenant » se lit en mémoire (vu dans les 15 dernières minutes : un
Studio ouvert appelle toutes les 2 minutes ; après un redémarrage du service, le compte se refait en 2 minutes).

**Les essais** : en local (`wrangler dev`), `request.cf` est un faux (Austin, Texas, de miniflare) : il est **ignoré**.
Seul l'en-tête **`X-XPG-Lieu-Essai`** place un appel, et seulement pour un appel à `127.0.0.1` ou `localhost` (comme
`STRIPE_API_BASE` et `DECALAGE_HORLOGE_ESSAI` : en ligne, l'adresse n'est jamais celle-là et l'en-tête est effacé).
Sa valeur : un formulaire d'adresse, `ville=Lyon&region=Auvergne-Rh%C3%B4ne-Alpes&pays=FR&lat=45.76&lon=4.84&colo=CDG`
(UTF-8 encodé en `%XX`).

### `GET /admin/globe` (admin+, admin)
Paramètre : `periode` (jours, 1 à 400, défaut 30 : les commandes de la période). Réponse 200 :
```json
{
  "version": "1.47.0", "heure": 1794326400, "periode": 30, "version_derniere": "1.47.0",
  "villes": [
    {"id": "FR|Lyon", "ville": "Lyon", "region": "Auvergne-Rhône-Alpes", "pays": "FR", "lat": 45.8, "lon": 4.8, "colo": "CDG",
     "clients": 9, "pc": 64, "actifs": 9, "pas_a_jour": 2, "admins": 1, "decouvertes": 1, "commandes": 3, "demandes": 1,
     "noms_clients": ["Atelier Exemple"], "postes": ["<64 hexa>"], "ids_admins": ["xpg-1f2e3d4c5b6a7980"]}
  ],
  "totaux": {"villes": 21, "pays": 9, "clients": 54, "pc": 342, "actifs": 42, "pas_a_jour": 11, "admins": 3,
             "decouvertes": 9, "commandes": 14, "demandes": 5},
  "sans_lieu": {"pc": 3, "admins": 0}
}
```
Les **calques** (chacun compté par ville) :

| Calque | Ce qui compte |
|---|---|
| `clients` | les clients distincts (le `customer` des licences) qui ont un PC activé dans cette ville |
| `pc` | les PC (clés des Studios) activés dont la dernière connexion est dans cette ville |
| `actifs` | parmi les clés des Studios de la ville, celles vues dans les 15 dernières minutes |
| `pas_a_jour` | les clients distincts qui ont, dans cette ville, un PC dont le Studio n'est pas sur la dernière version publiée |
| `admins` | les clés des admins de la liste (pas retirés) et de l'éditeur dont la dernière connexion est dans cette ville |
| `decouvertes` | les Découvertes en cours (§ 5) dont le PC est dans cette ville |
| `commandes` | les commandes payées dans la période dont le PC est dans cette ville |
| `demandes` | les demandes en attente dont le PC est dans cette ville (droit `demandes` pour un admin ; sinon 0) |

`noms_clients` (50 au plus), `postes` (200 au plus), `ids_admins` : pour « Voir les 9 clients », « Les 64 PC » et la
carte de la ville. Les villes : la plus grande (`pc` + `admins`) d'abord. `lat`/`lon` d'une ville : la moyenne de ses
clés. Une clé sans lieu (jamais connectée depuis la 1.47, ou un lieu que Cloudflare ne connaît pas) : `sans_lieu`.

### `GET /admin/globe/direct?depuis=<unix>` (admin+, admin) — « En direct »
```json
{"heure": 1794326400, "actifs": 42,
 "connexions": [{"t": 1794326390, "ville": "FR|Lyon", "genre": "studio"}, {"t": 1794326395, "ville": "FR|Paris", "genre": "admin"}]}
```
Les connexions (un appel signé d'une clé placée, une par clé et par minute au plus) depuis `depuis` (les 15 dernières
minutes au plus, 500 au plus, en mémoire seulement : rien n'est écrit) ; `actifs` : les clés des Studios vues dans les 15
dernières minutes. L'Admin le regarde toutes les 5 à 10 secondes quand « En direct » est choisi, et fait pulser
`ville`.

## 9. Ce que le service relève en plus (le schéma 5)

| Table | Une ligne | Écrite | Gardée |
|---|---|---|---|
| `stats_faits` | un fait : `t`, `genre`, `licence`, `offre`, `client`, `admin` (`xpg-…` de la clé qui l'a fait), `poste` (la clé du PC, ou l'identifiant de poste d'une demande), `valeur` (des jours, des secondes, des centimes), `detail` (un petit JSON) | au moment du fait (une ligne et son index) | 2 ans |
| `stats_quotidien` | le relevé d'un jour : les niveaux (§ 5) et leurs divisions, les versions vues par logiciel, la dernière version publiée | une fois par jour UTC (§ 10) | 2 ans |
| `stats_compteurs` | `(jour, mesure, cle) → n` : le pic des jetons (`jetons_pic`, et par licence `jetons_pic_licence`), les appels de chaque admin par heure UTC (`admin_appels`, `cle` = `<id>\|<HH>`) | tenus en mémoire, écrits une fois par minute au plus | 2 ans |
| `versions_vues` | `(cle, version) → logiciel, premiere` : la première fois qu'une clé est vue avec une version | quand la version d'une clé change | 2 ans |
| `lieux` | `cle → poste, logiciel, ville, region, pays, lat, lon, colo, vu` : le lieu de sa dernière connexion | quand il change, ou une fois par heure | un an sans appel |

Les **faits** (`genre`) : `licence_nouvelle`, `licence_prolongee` (`valeur` : les jours gagnés), `licence_option`
(`detail.options` : les identifiants ajoutés), `licence_revoquee`, `pc_revoque`, `pc_active`, `pc_libere`, `pc_retire`,
`decouverte_convertie`, `demande_recue`, `demande_traitee` (`valeur` : le délai ; `detail.issue` : `acceptee`,
`refusee`), `commande_livree` (`valeur` : le montant ; `detail.numero`). Le client et l'offre d'un fait : ceux du bloc de
la licence à ce moment.

`POST /admin/demandes/traiter` prend en 1.47 un champ facultatif **`issue`** (`"acceptee"` ou `"refusee"`) : l'Admin
1.47 le donne ; un Admin d'avant ne le donne pas (la demande compte comme traitée, ni acceptée ni refusée).

**Les écritures** (le plan gratuit compte les lignes écrites) : un fait = 2 lignes, sur des appels rares (une
activation, un dépôt de licence, une demande) ; un appel signé de routine (renouveler, jeton, messages) n'écrit rien de
plus, sauf le lieu d'une clé une fois par heure ; les compteurs, une ligne par admin actif et par minute au plus ; le
relevé, une ligne par jour. Les **ménages** (une fois par jour) effacent ce qui dépasse les durées ci-dessus.

## 10. Le relevé du jour

Le relevé de chaque jour UTC (les niveaux du § 5 à cet instant, sans filtre, divisés par offre, admin, type, logiciel,
version) est pris **une fois par jour, idempotent** (la ligne du jour existe : rien n'est refait) : par une **alarme de
l'objet** (`alarm()`, chaque jour vers 00:05 UTC, reprogrammée à chaque passage), et au premier appel du jour (le grand
ménage), au cas où l'alarme manquerait. Le point d'un niveau pour la fin d'un jour J est le relevé du jour J+1 ; sans
relevé, il est reconstitué.

`POST /admin/stats/releve` (l'admin+ seulement ; corps vide ou `{}`) : prend le relevé d'aujourd'hui s'il n'est pas
encore pris — pour les essais (avec `DECALAGE_HORLOGE_ESSAI`, « aujourd'hui » est celui du service) et pour l'Admin
(« Relever maintenant »). Réponse 200 `{"jour": "2026-11-10", "pris": true|false, "releve": {…}}` (`pris` faux : il
existait déjà ; `releve` : celui du jour).

## 11. La sauvegarde

Les cinq tables du § 9 sont dans la sauvegarde complète (`GET /admin/sauvegarde`, ses pages) et dans la restauration
(`POST /admin/restauration/lot`), avec leurs clés : `stats_faits` (`numero`), `stats_quotidien` (`jour`),
`stats_compteurs` (`jour`, `mesure`, `cle`), `versions_vues` (`cle`, `version`), `lieux` (`cle`). Ce qui n'est qu'en
mémoire (les compteurs de la dernière minute, « actif maintenant », les connexions en direct) n'y est pas.

## 12. Les droits

Toutes les routes : l'admin+ (une clé de l'éditeur) ou un admin de la liste (pas retiré), comme `GET /admin/stats`.
Un admin sans le droit `demandes` ne voit pas les ventes ni les demandes (`refuse` : `droit`), et le calque `demandes`
du globe vaut 0 pour lui ; `GET /admin/stats/admins` ne lui rend que lui-même ; `admin_appels`/`admin_jours` aussi
(`admin` forcé à sa clé). `POST /admin/stats/releve` : l'admin+ seulement.

## 13. Dans l'Admin : `src/license_admin/Stats.hpp` (sans écran)

```cpp
namespace license_admin::stats {
inline constexpr const char* kServiceRequired = "1.47.0";
bool serviceKnowsStats(const std::string& version);          // 1.47.0 ou plus
std::string serviceTooOldText(const std::string& version);   // « Ton service en ligne est en 1.46.0 : déploie la 1.47.0 pour les statistiques et le globe. »

enum class Step { Day, Week, Month };                         // "jour", "semaine", "mois"
enum class Compare { None, Previous, LastYear };              // "rien", "avant", "an"
struct Period { std::string from, to; };                      // "AAAA-MM-JJ", compris
std::string dayOf(Seconds t);                                 // le jour UTC
Period lastDays(Seconds now, int days);                       // 7 j, 30 j, 90 j (aujourd'hui compris)
Period lastMonths(Seconds now, int months);                   // 12 mois
Period nextDays(Seconds now, int days);                       // les échéances (aujourd'hui → +days)
bool isNumber(double v);                                      // faux : NaN (inconnu)

struct SeriesQuery {                                          // GET /admin/stats/serie
    std::vector<std::string> measures; Period period; Step step{Step::Day};
    std::string split; Compare compare{Compare::Previous};
    std::string client, admin, licence, software;             // les filtres
    std::string path() const;
};
struct Bucket { std::string from, to; };
struct Part { std::string key; std::vector<double> points; double total; };
struct Series {
    std::string name, unit, kind, source;                    // "centimes", "flux"…, "S", "N", "S+N"
    std::vector<double> points; double total;                 // NaN : inconnu
    std::vector<Part> parts;                                  // le plus grand d'abord ; "…" : le reste
    std::vector<double> before; double beforeTotal;           // la comparaison (vide : aucune)
    double change;                                            // la variation en % (NaN)
    bool refused; std::vector<std::string> rights;            // un droit manque
    const Part* part(const std::string& key) const;
};
struct SeriesReply {
    Period period, beforePeriod; Step step; Compare compare; std::string split;
    std::vector<Bucket> buckets, beforeBuckets;
    std::vector<Series> series; Seconds at; std::string recordedSince, serviceVersion;
    const Series* find(std::string_view name) const;
};
struct UpdatesQuery { Period period; Step step{Step::Week}; int days{42}; std::string software{"tous"}; std::string path() const; };
struct VersionRow { std::string version; Seconds published; int studios, admins; };
struct AdoptionCurve { std::string version, software; Seconds published; int base; std::vector<int> days; };
struct UpdateDelay { std::string version, software; int count; double median, mean; };   // secondes
struct BehindPc { std::string key, software, version, name, customer, licence, admin; Seconds seen; int behind; };
struct UpdatesReply {
    std::string latest; Seconds latestPublished; std::vector<VersionRow> versions;
    int seenStudios, seenAdmins, upToDateStudios, upToDateAdmins; std::string seenSince;
    std::vector<AdoptionCurve> adoption; std::vector<UpdateDelay> delays;
    std::vector<Bucket> buckets; std::vector<int> studiosUpToDate, studiosSeen, adminsUpToDate, adminsSeen;
    std::vector<BehindPc> behind; int behindTotal;
};
struct AdminUsage {
    std::string id, key, role, name; Seconds retired; std::vector<std::string> days; std::array<int, 24> hours;
    int calls, usualStart, usualEnd; Seconds lastSeen; std::string software, softwareVersion, city, country;
};
struct AdminsReply { Period period; std::string recordedSince; std::vector<AdminUsage> admins; const AdminUsage* find(const std::string& id) const; };
struct GlobeCity {
    std::string id, city, region, country, colo; double lat, lon;
    int clients, pcs, active, outdated, admins, discoveries, orders, requests;
    std::vector<std::string> customers, devices, adminIds;
    int count(std::string_view layer) const;                  // "clients", "pc", "actifs", "pas_a_jour", "admins", "decouvertes", "commandes", "demandes"
};
struct GlobeReply { Seconds at; int periodDays; std::string latestVersion; std::vector<GlobeCity> cities; GlobeCity totals; int countries, unplacedPcs, unplacedAdmins; };
struct LivePing { Seconds at; std::string city, kind; };      // "studio", "admin"
struct LiveReply { Seconds at; int active; std::vector<LivePing> pings; };
struct SnapshotReply { std::string day; bool taken; core::json::Value snapshot; };

// Lire les réponses (les essais, les pages) ; faux : `why`.
bool parseSeries(const core::json::Value&, SeriesReply&, std::string* why);
bool parseUpdates(const core::json::Value&, UpdatesReply&, std::string* why);
bool parseAdmins(const core::json::Value&, AdminsReply&, std::string* why);
bool parseGlobe(const core::json::Value&, GlobeReply&, std::string* why);
bool parseLive(const core::json::Value&, LiveReply&, std::string* why);
// Le réseau (BLOQUANT ; un client ouvert) : un service d'avant la 1.47 → refusé avec serviceTooOldText.
online::Result fetchSeries(online::Client&, const SeriesQuery&, SeriesReply&);
online::Result fetchUpdates(online::Client&, const UpdatesQuery&, UpdatesReply&);
online::Result fetchAdmins(online::Client&, const Period&, AdminsReply&);
online::Result fetchGlobe(online::Client&, int periodDays, GlobeReply&);
online::Result fetchLive(online::Client&, Seconds since, LiveReply&);
online::Result takeSnapshot(online::Client&, SnapshotReply&);   // l'admin+
// Les textes : "26 020 €" (sans centimes), "3 h 40", "+13 %", la mesure en français.
std::string euros(double cents); std::string duration(double seconds); std::string changeText(double pct);
std::string measureLabel(std::string_view name);
}
```

**Le Workspace** (`gui/WorkspaceStats.cpp`, `Workspace.hpp`) — chaque appel est **LENT** (le réseau, à lancer par
`Background`), signé par la clé qui signe (l'admin+ ou un admin) :
`statsSeries(q, out)`, `statsUpdates(q, out)`, `statsAdmins(period, out)`, `statsGlobe(periodDays, out)`,
`statsLive(since, out)`, `statsSnapshot(out)` (l'admin+) ; et sans réseau : `serviceKnowsStats()` (l'état du service
lu), `statsServiceNote()` (la phrase « déploie la 1.47.0 », vide si le service la sait).

**La console** (`XPG-License-Admin`, pour les scripts ; `--json` : la réponse du service telle quelle) :
`stats-serie --mesures a,b [--du D --au D] [--pas jour|semaine|mois] [--par X] [--comparer avant|an|rien] [--client C]
[--admin ID] [--licence L] [--logiciel S] [--json]`, `stats-mises-a-jour [--du D --au D] [--jours N] [--json]`,
`stats-admins [--du D --au D] [--json]`, `stats-releve` (l'admin+), `globe [--periode N] [--json]`,
`globe-direct [--depuis T] [--json]`.

## 14. Les planches et leurs mesures

| Planche, graphique | Ce qu'il lit |
|---|---|
| 08 Montant encaissé (par semaine, contre l'an dernier) | `ventes_montant`, `pas=semaine`, `comparer=an` ; l'infobulle : `ventes_commandes`, `ventes_panier` du même point |
| 08 Cumulé sur l'année | `ventes_cumul`, `pas=jour` ou `semaine`, `comparer=an` |
| 08 Commandes, Panier moyen | `ventes_commandes`, `ventes_panier` |
| 08 Ventes par offre | `ventes_montant` `par=offre` (la clé `options` : les options) |
| 08 Options vendues | `ventes_montant` et `ventes_commandes` `par=option` (sans la clé `""`) |
| 08 Licences vendues (en ligne, à la main) | `licences_vendues_en_ligne`, `licences_signees_main` |
| 08 Remboursements | `ventes_remboursements`, `ventes_remboursements_nombre` |
| 08 Licences actives | `licences_actives`, `comparer=an` |
| 08 Mouvements des licences | `licences_nouvelles`, `licences_prolongees` (entrées), `licences_expirees`, `licences_revoquees` (sorties) |
| 08 Échéances (90 jours à venir) | `licences_expirees` et `licences_echeances_montant` avec `du` = aujourd'hui, `au` = +90 j (`stats::nextDays`) |
| 08 Demandes, Délai de réponse | `demandes_recues`, `demandes_traitees`, `demandes_attente` ; `demandes_delai_median`, `demandes_delai_moyen` |
| 08 PC actifs par offre | `pc_actifs` `par=offre`, et `decouvertes_en_cours` (la ligne « Découverte ») |
| 08 Mouvements des PC | `pc_actives`, `pc_liberes`, `pc_retires` |
| 08 Flottantes (le pic du jour) | `jetons_pic` (`pas=jour`), `jetons_plafond`, `licences_au_plafond` |
| 08 Découvertes | `decouvertes_commencees`, `decouvertes_converties` |
| 09 tout | `GET /admin/stats/mises-a-jour` |
| 10 la fiche d'un client | § 5, `client=<nom>` ; « Ville : Lyon, FR · vue par le service » : `GET /admin/globe` (`noms_clients`) |
| 11 le globe, la petite carte du tableau de bord | `GET /admin/globe`, `GET /admin/globe/direct` |
| 01-04 les admins : jours de connexion, heures habituelles, version de son Admin, vu, ville | `GET /admin/stats/admins` |
| 01-04 ce qu'il produit, ses courbes sur 12 semaines | § 5 avec `admin=<id>` (et sans, pour les autres admins en gris : `par=admin`) |
| 01-05 actions, criticité, carte jour × heure, sécurité, qui fait quoi | **[J]** : le journal, calculé par l'Admin |
