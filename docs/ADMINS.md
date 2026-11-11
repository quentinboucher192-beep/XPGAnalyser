# Plusieurs admins, le journal, les fichiers, les mises à jour (1.44.0) : le contrat

Maquette validée : `docs/maquettes/1.43.0` (planches 00 à 08 ; la 08, les droits, ajoutée à la demande de l'éditeur).
Ce document est le contrat entre l'Admin, le service (`service/`) et les Studios. Ce qui n'y est pas ne change pas
(`docs/SERVICE-LICENCES.md` reste vrai).

## 1. Les rôles

- **L'admin+** : le PC qui a **la clé de l'éditeur** (`cle-editeur-licences.cle`). Il n'y en a qu'un. Il fait tout.
- **Un admin** : un Admin qui a **sa propre clé** (`cle-admin.cle`, Ed25519, née sur son PC, protégée par DPAPI comme
  celle de l'éditeur ; elle n'en sort jamais) et qui figure, non retiré, dans **la liste des admins** signée par la clé de
  l'éditeur. Il signe avec sa clé ce que ses **droits** permettent.
- **Pas encore admin** : un Admin sans aucune des deux clés. Il peut **créer la clé de l'éditeur** (le premier PC,
  comme avant) ou **demander à devenir admin** (§ 4).

Réservé à l'admin+, jamais donné : ajouter / retirer un admin, leurs droits et les profils ; le journal des admins et
sa criticité ; publier une version ; envoyer un fichier ; le certificat du service. **Gérer le service** (1.47.0, § 11 :
le mettre en ligne ou à jour, son adresse, ses adresses de secours, le relevé du jour) est aussi à l'admin+ seul, mais il
peut le confier à **un** admin pour un temps court (une dérogation : 72 h au plus, jamais permanente).

## 2. Les droits (13 fonctions)

| Identifiant | Fonction | Qui le vérifie |
|---|---|---|
| `licence_creer` | Créer et signer une licence | service (licence nouvelle) ; Studios (genre licence) |
| `licence_prolonger` | Prolonger une licence | service (licence déjà là) ; Studios (genre licence) |
| `licence_option` | Ajouter une option à une licence | service (licence déjà là) ; Studios (genre licence) |
| `licence_revoquer` | Révoquer une licence | service, Studios (genre revocations) |
| `poste_retirer` | Retirer un PC d'une licence | service |
| `offres` | Modifier les offres (prix, fonctions) | service (partie `editions` du registre) ; Studios (genre registre) |
| `options` | Créer, modifier une option | service (partie `options`) ; Studios (genre registre) |
| `annonces` | Annonces (notifications générales) | service (partie `annonces`) ; Studios (genre registre) |
| `demandes` | Accepter, refuser une demande | service, Studios (genre poste) |
| `codes` | Revoir le code d'activation | l'Admin |
| `client_supprimer` | Supprimer un client | l'Admin |
| `clients_exporter` | Exporter les clients (CSV) | l'Admin |
| `courriels` | Écrire aux clients (courriel) | l'Admin |

**Les Studios vérifient par genre de document** (ils ne voient pas forcément chaque version d'un document) :
une licence signée par un admin demande l'un de `licence_creer`, `licence_prolonger`, `licence_option` ; un registre,
l'un de `offres`, `options`, `annonces` ; une liste de révocation, `licence_revoquer` ; un état « poste », l'un de
`demandes`, `licence_creer`, `licence_prolonger`, `licence_option`, `licence_revoquer`, `poste_retirer`. Tout autre genre
(certificat du service, liste des admins, version, message, fichier) : la clé de l'éditeur seulement.

**Le service vérifie au plus près** (il voit chaque dépôt) : une licence d'un identifiant inconnu demande
`licence_creer`, une licence déjà déposée `licence_prolonger` ou `licence_option` (ou `licence_creer`) ; un registre,
le droit de chaque partie qui change (`editions` : `offres`, `options` : `options`, `announcements` : `annonces`,
comparées au registre en place ; une autre partie qui change : `offres` ; rien ne change : l'un des trois) ; etc. (§ 6).

**Les profils** (« Admin LV1 », « Admin LV2 ») sont une commodité de l'admin+ : une liste de droits nommée. Copier un
profil vers un admin **copie** ses droits (pas un lien) ; l'admin+ ajuste ensuite cet admin seul. Les profils et
l'origine de chaque admin vivent chez l'admin+ (`profils-admins.json`, partagé par l'archive) ; la liste signée ne porte
que le nom du profil d'origine (pour l'affichage) et les droits effectifs. Le profil de départ, « Tous les droits »,
ne se modifie pas.

## 3. La liste des admins (genre `admins`, signée par la clé de l'éditeur)

Un bloc `XPG-LICENSING 1 admins <taille>` (l'enveloppe de `SignedFile.hpp`), au plus 64 Kio :

```json
{"format":1,"numero":7,"emise":1762700000,
 "admins":[
  {"id":"xpg-a41c0e7b92d45f18","cle":"<64 hexa>","nom":"Léa","pc":"PC-COMMERCIAL-02","poste":"<32 hexa>",
   "depuis":1757800000,"profil":"Admin LV2","droits":["licence_creer","demandes"],
   "retire":0,"gardes":[]}
 ]}
```

- `numero` monte à chaque changement (anti-retour : un Studio, le service, l'Admin gardent le plus grand vu).
- `id` = `keyIdOf(cle)` ; `poste` = l'identifiant de la machine de l'admin (`systemMachineId`, 32 hexa).
- **Retirer** un admin : `retire` = l'heure, `droits` vidés ; il reste dans la liste (ses anciennes signatures se
  reconnaissent). `gardes` : les empreintes (SHA-256 hexa du JSON signé, octet pour octet) des documents qu'il a signés et
  qui **restent valides** (ses licences, sauf si l'admin+ choisit de les révoquer aussi). Un document signé par un admin
  retiré n'est valide que si son empreinte est dans `gardes`. Un document gardé vaut aussi quand l'admin, toujours là,
  a perdu le droit qui le permettait : **retirer un droit à un admin ne casse pas ce qu'il a déjà signé** (l'Admin de
  l'admin+ met dans `gardes` ce que l'admin a signé et qui court encore).
- Au retrait, l'admin+ **re-signe avec sa clé** le registre et la liste de révocation en place s'ils étaient signés par
  l'admin retiré (numéro + 1), pour qu'ils restent valides.
- Au plus 32 admins, 512 empreintes gardées par admin.

**Où les Studios la trouvent** : `GET /v1/admins` (publique) ; `GET /v1/etat` donne son numéro (`admins`) ; un fichier de
licence exporté par un admin porte deux blocs (la liste, puis la licence) et l'import garde la liste si elle est plus
récente. Elle est gardée dans le dossier des licences (`admins.xpgadm`), comme le registre.

## 4. Devenir admin (planche 02)

1. **L'admin+** ouvre « Ajouter un admin… » : son nom, son courriel, comment lui envoyer l'Admin (**dans les
   notifications de son Studio** — un PC choisi parmi les activations — ou **par un lien** valable 48 h, une seule fois),
   son profil. L'invitation est gardée chez l'admin+ (`invitations-admins.json`).
2. **Le futur admin** installe l'Admin. Il démarre **connecté** au service de l'éditeur (1.47.0, § 11 : la connexion
   signée qui voyage avec l'installateur, ou que le Studio de son PC a déjà) ; aucune adresse à taper.
   Sans clé, l'Admin propose « Demander à devenir admin » : son nom, son courriel ;
   il crée sa clé (`cle-admin.cle`) et dépose sa **demande d'admin** : `POST /v1/admins/demande`, signée par **sa clé**
   (les en-têtes habituels), corps `{"nom","courriel","pc","poste","logiciel"}`. Le service la garde (une par clé ; au
   plus 20 en attente) et l'écrit au journal. L'Admin montre **l'empreinte** : les 24 premiers hexadécimaux en
   majuscules du SHA-256 de la clé publique, en 6 groupes de 4 (`B7E2 48A9 1D3C 90F6 5A2E C471`).
3. **L'admin+** voit la demande (« En attente d'acceptation » dans Administrateurs, `GET /admin/admins/demandes`) ;
   ils lisent l'empreinte **au téléphone**, il coche « J'ai vérifié l'empreinte », accepte : la clé de l'éditeur signe
   la nouvelle liste (numéro + 1, avec ses droits), `PUT /admin/admins` `{"admins":"<bloc>"}` la publie ; **le trousseau** (§ 5) lui est
   déposé ; la demande est close. Refuser : `POST /admin/admins/demande/refuser`.
4. L'Admin du nouvel admin relève `GET /v1/admins` (toutes les 15 s tant qu'il attend) : sa clé y est → il est admin.
   Il prend le trousseau, la clé de synchro, et relève l'archive.

**Une ou plusieurs demandes à la fois (1.47.0).** Les demandes ont leur liste, comme les demandes de licence :
**Sécurité › Demandes d'admin** (l'admin+ seul ; le nombre en attente sur l'entrée du menu). Elle relève
`GET /admin/admins/demandes?toutes=1` : celles en attente **et** celles closes depuis 7 jours, chacune avec son `etat`
(`attente`, `acceptee`, `refusee`) et `maj` (sans `?toutes=1`, la réponse reste celle d'avant : en attente seulement).
Colonnes : reçue le, nom, courriel, PC, logiciel et sa version, l'empreinte courte, **l'invitation** qui va avec (le même
courriel, sinon le même nom, dans `invitations-admins.json` : « invité le … (profil) » / « sans invitation »), l'état ;
la barre de filtres, le clic droit (Ouvrir / Accepter… / Aller à › / Filtrer sur › / Copier › l'empreinte / Exporter… /
Refuser… en rouge), la ligne dépliée (l'empreinte en grand, la demande, l'invitation).

- **Accepter plusieurs** : choisir les demandes → « Accepter 3 demandes… » ouvre **une** fenêtre qui les liste, chacune
  avec son empreinte en 6 groupes, sa case **« J'ai vérifié l'empreinte (lue au téléphone) »** et son profil (celui de
  l'invitation, sinon « Admin LV1 », sinon aucun droit). Une case vide : le bouton reste grisé — on la coche, ou on retire
  la demande du lot. Confirmer : la clé de l'éditeur signe **une seule** nouvelle liste (numéro + 1) avec tous ;
  `PUT /admin/admins` la publie, et le service **close d'un coup toutes les demandes dont la clé est dans la liste**
  (`demandes_acceptees`) ; le trousseau est déposé pour chacun ; **une ligne `admin_ajouter` au journal par nouvel admin**
  (son détail dit la liste et la taille du lot). L'Admin le refait de son côté : une case non cochée, ou une demande qui
  n'attend plus, et rien n'est signé (`Workspace::acceptAdmins`).
- **Refuser plusieurs** : « Refuser 2 demandes… », une raison facultative ; `POST /admin/admins/demande/refuser` pour
  chacune, une ligne `demande_refuser` chacune (la raison y est ; le demandeur voit seulement « refusée »).
- Aucune route ni table de plus côté service : la limite de 20 demandes en attente reste.

## 5. Le trousseau (partager la clé de synchro)

L'archive partagée (`ArchiveSync`) est scellée par la clé de synchro. L'admin+ la donne à chaque admin, **scellée pour sa
clé** (la clé Ed25519 de l'admin convertie en X25519, `crypto_box_seal`) : `PUT /admin/trousseau`
`{"cle":"<cle de l'admin>","scelle":"<base64>"}` ; l'admin la lit par `GET /admin/trousseau` (signé par sa clé : le
service rend le scellé de cette clé). Retirer un admin efface son scellé. (La clé de synchro ne change pas au retrait :
l'admin retiré a pu lire l'archive jusque-là ; un « Changer la clé de synchro » existe déjà côté admin+.)

L'archive accepte désormais les documents signés par **la clé de l'éditeur ou un admin de la liste** (plus seulement la
clé de ce PC). Les profils, les courriels des admins, les invitations, la criticité vivent dans l'archive (scellés).

## 6. Le service

### L'authentification des appels d'admin

Les routes `/admin/*` acceptent la clé de l'éditeur (`PUBLISHER_KEYS`, rôle « admin+ ») **ou** une clé d'admin de la
liste en place, non retiré (rôle « admin », avec ses droits). Chaque route dit le droit qu'elle demande ; « admin+ » :
la clé de l'éditeur seulement. Une clé d'admin **retiré** : 403 `retire`, et une ligne au journal (« signature
refusée », écrite par le service). Les documents déposés (licence, registre, révocations, poste) : leur signature doit
être celle de **la clé qui appelle** (un admin dépose ce qu'il a signé), avec le droit qu'il faut (§ 2).

| Route | Qui | Droit |
|---|---|---|
| `PUT /admin/certificat`, `PUT /admin/admins`, `GET /admin/admins/demandes`, `POST /admin/admins/demande/refuser`, `PUT /admin/trousseau`, `GET /admin/journal`, `POST /admin/fichiers`, `PUT /admin/fichiers/morceau`, `PUT /admin/versions`, `PUT /admin/messages`, `GET /admin/versions` | admin+ | — |
| `PUT /admin/registre` | admin+ ou admin | la partie qui change |
| `PUT /admin/revocations` | admin+ ou admin | `licence_revoquer` |
| `PUT /admin/licences` | admin+ ou admin | `licence_creer` (nouvelle) ; sinon l'un des trois |
| `POST /admin/activations/retirer` | admin+ ou admin | `poste_retirer` |
| `GET /admin/demandes`, `POST /admin/demandes/traiter`, `PUT /admin/postes` | admin+ ou admin | `demandes` (ou un droit de licence pour un poste) |
| `GET /admin/licences`, `GET /admin/etat`, `GET /admin/stats`, `GET /admin/archive*`, `POST /admin/archive`, `GET /admin/trousseau`, `POST /admin/journal`, `GET /admin/admins` | admin+ ou admin | — |
| 1.46.0 (la boutique, `docs/BOUTIQUE.md`) : `PUT /admin/boutique`, `POST /admin/commandes/rembourser` | admin+ | — |
| `GET /admin/boutique` | admin+ ou admin | — |
| `GET /admin/commandes`, `POST /admin/commandes/prendre`, `POST /admin/commandes/livree`, `POST /admin/commandes/a-revoir` | admin+ ou admin | `demandes` |
| 1.47.0 (§ 11) : `PUT /admin/derogation`, `POST /admin/derogation/revoquer` | admin+ | — |
| `GET /admin/derogation` | admin+ (celle en place) ou admin (la sienne) | — |
| `POST /admin/derogation/utiliser` | admin (sa dérogation en cours) | — |
| `POST /admin/stats/releve` | admin+, ou l'admin dont la dérogation court | — |
| 1.49.0 (§ 12) : `POST /admin/admins/invitation` | admin+ | — |

**La boutique (1.46.0)** : livrer une commande payée demande le droit `demandes` (la commande, son scellé) **et** le
droit de la licence qu'elle signe : `licence_creer` pour une offre, `licence_option` pour une option (le dépôt de la
licence, `PUT /admin/licences`, le vérifie) ; la livraison automatique ne tourne que dans un Admin qui les a (l'admin+,
ou un admin avec `demandes` et `licence_creer`), un admin sans `licence_option` laisse les options à un autre. Régler la
boutique, Brancher Stripe et rembourser : l'admin+ seul. Les actions du journal : `boutique`, `stripe_brancher`,
`commande_livrer`, `commande_revoir`, `commande_rembourser`.

### La demande en ligne scellée pour plusieurs clés

`POST /v1/demandes` accepte, en plus de `cle`/`scelle`, `scelles` : `[{"cle","scelle"}]` (une par admin qui a le droit
`demandes`, et la clé de l'éditeur). `GET /admin/demandes` rend à chaque clé **son** scellé (sans scellé pour elle : la
demande est listée sans contenu, « scellée pour un autre admin »). Un Studio 1.43 ou avant ne scelle que pour l'éditeur.

### Le journal des admins

Chaque action d'un admin (et de l'admin+) part au service : `POST /admin/journal` `{"entree":"<JSON>","signature":"<128 hexa>"}`
où la signature (de la clé qui appelle) porte sur `"XPG-JOURNAL-1\n" + entree`. `entree` :

```json
{"quand":1762700000,"action":"licence_creer","objet":"XPG-2026-00412","resume":"a signé la licence XPG-2026-00412",
 "detail":{"client":"Atelier Meyer","offre":"PROFESSIONAL 1 an"},"avant":{},"apres":{},
 "pc":"PC-COMMERCIAL-02","poste":"<32 hexa>","logiciel":"1.44.0","nonce":"<32 hexa>"}
```

Le service l'ajoute à la chaîne : `numero` (1, 2, 3…), `precedent` (SHA-256 hexa de la ligne d'avant, `""` pour la
première), `recu` (son heure), `cle` (qui), et garde la ligne `{"numero","precedent","recu","cle","entree","signature"}`
(son empreinte = SHA-256 de ce texte, octet pour octet). Il écrit lui-même (signées par **la clé du service**) les
lignes `signature_refusee` (une clé retirée), `autre_pc` (une entrée dont `poste` n'est pas celui de l'admin dans la
liste) et `demande_admin` (une demande d'admin reçue). `GET /admin/journal?depuis=N` (admin+) rend les lignes après N
(500 au plus). `GET /admin/etat` rend `journal` (le dernier numéro).

L'admin+ vérifie chaque ligne (la signature de l'entrée par la clé de cet admin, d'après toutes les listes vues ; la
chaîne) et la garde (`journal-admins.jsonl`). **Une ligne manquante ou modifiée se voit** : il garde le dernier numéro
et son empreinte.

**La criticité** (Info, Moyenne, Haute, Critique) est un réglage de l'admin+ (par type d'action, « Me prévenir tout de
suite », « Aussi par courriel » ; et une criticité changée à la main par ligne, avec qui et quand) : chez lui, partagé
par l'archive. Les lignes signées ne changent jamais.

Types d'action : `licence_creer`, `licence_prolonger`, `licence_option`, `licence_revoquer`, `poste_retirer`,
`offres`, `options`, `annonces`, `demande_accepter`, `demande_refuser`, `code_revoir`, `client_supprimer`,
`client_restaurer`, `clients_exporter`, `courriel`, `admin_ajouter`, `admin_inviter` (1.49.0), `admin_retirer`, `admin_droits`, `admin_profil`, `version_publier`,
`fichier_envoyer`, `criticite`, `connexion`, et ceux du service (`signature_refusee`, `autre_pc`, `demande_admin`).

### Les fichiers

Un fichier est déposé en morceaux (1 Mio au plus) par l'admin+ : `POST /admin/fichiers` `{"manifeste":"<bloc fichier>"}`
→ `{"id"}` ; puis `PUT /admin/fichiers/morceau?id=…&n=…` (`n` compte depuis 0 ; le corps : les octets, 1 Mio pile sauf le dernier) ; le service le marque complet quand
tous les morceaux sont là. Le **manifeste** (genre `fichier`, signé par la clé de l'éditeur) :
`{"format":1,"id":"<32 hexa>","nom":"XPG-License-Admin-Setup-1.44.0.exe","taille":6595615,"sha256":"<64 hexa>",
"sorte":"admin|studios|autre","version":"1.44.0","acces":"public|admins|jeton","morceau":1048576,"emis":…}`.

Lire : `GET /v1/fichiers/manifeste?id=…`, `GET /v1/fichiers/morceau?id=…&n=…` ; l'accès `public` : tout le monde ;
`admins` : un appel signé par une clé d'admin (ou de l'éditeur), ou l'en-tête `X-XPG-Jeton` (le bloc `jetonadmin` en base64) d'un jeton d'admin (§ 7) ;
`jeton` : `?jeton=…` (un jeton tiré par l'admin+, gardé haché ; 30 jours au plus — un fichier joint à un message ; « une
seule fois » : 48 h au plus, utilisable pour un seul téléchargement complet — un lien dans un courriel). `GET /v1/fichiers/lien?jeton=…` rend le fichier entier (un navigateur). **Le Studio vérifie
toujours** la taille et le SHA-256 contre le manifeste signé ; un fichier qui ne correspond pas est **refusé**
(jamais proposé, ni enregistré).

Les fichiers d'une version gardent leurs morceaux tant que la version est l'une des 3 dernières ; un fichier joint à un
message, 30 jours.

### Les messages à un PC

`PUT /admin/messages` (admin+) `{"poste":"<deviceId>","bloc":"<bloc message>"}` ; le Studio relève
`GET /v1/messages?depuis=N` (signé par la clé du PC) : ses messages, numérotés par le service. Le bloc (genre `message`,
signé par la clé de l'éditeur) : `{"format":1,"poste":"<deviceId>","titre","de","emis","scelle":"<base64>"}` ; le scellé
(pour la clé du PC) : `{"texte","fichiers":[{"id","nom","taille","sha256","version","jeton"}],"action":"admin"}`. Le
`jeton` d'un fichier joint vaut 30 jours (`POST /admin/fichiers/jeton`), comme le fichier lui-même.

### Les versions

`PUT /admin/versions` (admin+) `{"bloc":"<bloc version>"}` ; `GET /v1/version` (publique) : la plus récente ;
`GET /v1/etat` rend `version_publiee` (« 1.44.1 » ; `version` y est déjà celle du service). Le bloc (genre `version`, signé par la clé de l'éditeur) :

```json
{"format":1,"version":"1.44.1","publiee":1762700000,"importante":false,"notes":["Exports PDF : …"],
 "fichiers":[{"id","nom","taille","sha256","sorte":"studios","pour":"tous"},
             {"id","nom","taille","sha256","sorte":"admin","pour":"admins"}]}
```

Les Studios et les Admins disent leur version dans l'en-tête `X-XPG-Logiciel` (`IHM-Studio/1.44.0`,
`API-Studio/1.44.0`, `XPG-License-Admin/1.44.0`) de leurs appels signés ; le service garde, par clé, le logiciel, la
version et l'heure (`logiciels`). `GET /admin/versions` (admin+) : les versions publiées, et combien de PC sont à jour.

## 7. Le Studio

- **La liste des admins** : relevée avec le registre (§ 3) ; `verifyTrusted` accepte une clé d'admin de la liste
  (non retiré, avec le droit du genre ; retiré : son empreinte gardée).
- **Les messages** (planche 05) : relevés au lancement et toutes les 2 minutes (avec l'état) ; une notification « avec
  fichiers » : le fichier se vérifie, **Installer** le lance, **Enregistrer…** le copie où on veut ; un fichier qui ne
  correspond pas : « Fichier refusé ».
- **Les mises à jour** (planche 06) : `version` de l'état plus récente que la sienne → la notification « Mise à jour
  X disponible » (une fois par version ; « importante » : à chaque lancement), le toast, la pastille en haut ;
  **Télécharger et installer** : le téléchargement vérifié, le projet enregistré, l'installateur lancé en silencieux
  (`/RELANCER=` : il rouvre le Studio sur le projet), le Studio se ferme.
- **Sur le PC d'un admin** : l'Admin laisse un **jeton d'admin** (`jeton-admin.xpgjet`, genre `jetonadmin`, signé par sa clé :
  `{"cle","poste","expire"}`, 30 jours) dans son dossier ; le Studio du même compte le lit et l'envoie
  (`X-XPG-Jeton`) pour télécharger aussi l'installateur de l'Admin : « Ton Admin 1.44.0 n'est plus à jour ».

## 8. L'Admin

Pages nouvelles (Sécurité) : **Administrateurs** (+ **Droits** ; en lecture seule pour un admin), **Journal des
admins** (admin+), **Versions** (admin+). Un admin n'a pas les entrées réservées. Les actions qu'il n'a pas le droit
de faire sont grisées (le bouton dit pourquoi). Chaque action écrit sa ligne au journal (hors ligne : gardée, envoyée
au retour).

**Mon compte (1.47.0)** — la première entrée de Sécurité, pour tous les rôles (admin+, admin, admin retiré, en attente,
pas encore de clé) ; le bloc d'identité en haut à gauche (« Camille · admin+ ») y mène aussi. Tout se lit sur le poste,
sans réseau :

- **Qui** : le nom (la liste, sinon la clé), le courriel (celui de sa demande d'admin), le rôle.
- **L'identifiant** : l'identifiant de la clé (`xpg-…`), l'empreinte en 6 groupes (celle qui se lit au téléphone),
  « Copier » l'un et l'autre.
- **L'état du compte** : actif depuis, ajouté par l'admin+ dans la liste signée n° (la première liste vue sur ce poste
  qui porte sa clé), la liste en vigueur ici ; en attente (la demande envoyée le …) ; retiré le … par l'admin+ ; le
  certificat du service (s'il a été lu) ; ce PC (et celui que la liste lui connaît) ; la version de l'Admin et celle du
  service ; **Gérer le service** : « l'admin+ seul gère le service », ou la dérogation en cours et ce qu'il en reste (§ 11).
- **Les pouvoirs** : les 13 droits et la boutique (livrer, revoir une commande : « Accepter une demande » et « Créer et
  signer »), permis ou non, ce que chacun permet, sa criticité (celle en vigueur), le profil (LV1, LV2…) et les écarts au
  profil (« en plus du profil », « retiré du profil »), la dernière fois qu'il a servi (le journal des admins de ce
  poste : chez l'admin+ ; un admin lit « il est chez l'admin+ »). L'admin+ : les 13, et ce qu'il fait seul (§ 1).
- **L'histoire des droits** : les listes signées vues sur le poste (`admins-vus.jsonl`), l'une après l'autre : ce qui a
  été ajouté ou retiré, par l'admin+.
- Les gestes qui existent : Copier ; « Sauvegarder ma clé… » (l'admin+ : Paramètres de sécurité) ; « Demander à devenir
  admin… » et « Créer la clé de l'éditeur… » (sans clé) ; « Voir ma demande » (en attente : l'écran d'attente).

**Demandes d'admin (1.47.0)** — la liste des demandes pour devenir admin, l'admin+ seul (§ 4). Les invitations de
« Ajouter un admin… » sont notées sur le poste de l'admin+ (`invitations-admins.json` : `{"format":"xpg-invitations-admins/1",
"invitations":[{"nom","courriel","profil","par","le"}]}`, les 100 dernières).

## 9. La sauvegarde de secours (si le service tombe)

**Une sauvegarde complète, au contenu exact** : de quoi tout remettre ailleurs — l'admin+, les admins, leurs droits,
les profils, le journal, les licences, les PC activés, les demandes, les réglages. Faite par **l'Admin de l'admin+**,
chaque jour d'elle-même (et « Sauvegarder maintenant »), dans un dossier qu'il choisit ; les 30 dernières sont gardées.

Le fichier `sauvegarde-licences-AAAA-MM-JJ-HHMM.xpgsauve` :

```
XPG-SAUVEGARDE 1
cle=<identifiant xpg-... de la clé de l'éditeur>
creee=<heure> service=<adresse>
argon2id ops=<n> mem=<octets> sel=<32 hexadécimaux>
<le base64 : nonce (24 octets) + chiffré XChaCha20-Poly1305 du contenu>
```

Comme la sauvegarde de la clé (`KeyBackup.hpp`, 1.32.0) : la clé de chiffrement vient d'**une phrase de passe que
l'éditeur choisit et tape lui-même** (Argon2id) ; les lignes d'en-tête sont liées au chiffré. Pour la sauvegarde de
chaque jour sans retaper la phrase, la clé dérivée (et son sel) est gardée sur le PC de l'admin+, protégée par DPAPI
(`sauvegarde.cle`) ; ailleurs, la phrase suffit (le sel est dans l'en-tête).

Le contenu (JSON) : `{"format":1, "cree":…, "logiciel":…, "service_url":…, "service":{"manifeste":…, "tables":{"<table>":[lignes…]}},
"poste":{"<chemin relatif>":"<base64>"…}, "cle_editeur":{"format":"xpg-sauvegarde-cle/1","nom","essai","publique","secrete"},
"cle_synchro":"XPGS1-…", "codes":{"<licence>":"<code>"}}` (1.44.0, tel qu'écrit par l'Admin : la sauvegarde de chaque
jour se fait sans la phrase, la clé de l'éditeur y est donc mise telle quelle — la même charge utile que le `.xpgcle` —
**dans** le contenu chiffré par la clé tirée de la phrase ; jamais ailleurs en clair) :
- **le service** : tout ce que rend `GET /admin/sauvegarde` (table par table, page par page, les lignes telles quelles :
  la clé du service et son certificat, les licences, les PC, les demandes, la liste des admins, le trousseau, le
  journal, les messages, les versions et le manifeste des fichiers — pas leurs octets, ni les compteurs anti-abus) ;
- **le dossier du poste** : l'archive, les profils, la criticité, les invitations, `service.json`, `activations.jsonl`,
  les listes des admins vues, le journal vérifié — pas les clés DPAPI de ce PC (`cle-synchro.cle` y est sous forme
  scellée par la phrase) ;
- **la clé de l'éditeur**, la clé de synchro et les codes d'activation gardés sur le poste (dans le contenu chiffré) ;
  le mot de passe du mail n'y est pas (à retaper).

Les lignes du service ne sont jamais relues ni réécrites par l'Admin : le texte JSON de chaque ligne passe tel quel de
`GET /admin/sauvegarde` à `POST /admin/restauration/lot` (des lots de 2 Mio au plus ; une ligne plus grosse part seule).

**Restaurer** (« Restaurer une sauvegarde… », dans un Admin neuf, ou Paramètres de sécurité) : le fichier, la phrase ;
le dossier du poste revient, la clé de l'éditeur est réécrite pour CE compte (DPAPI). Option **« Remettre aussi le
service »** : l'adresse du service à remplir (un nouveau déploiement : un autre compte Cloudflare, ou le même programme
hébergé ailleurs — `outils/service/`), puis `POST /admin/restauration/debut`, `…/lot` (table par table), `…/fin` : il
retrouve **la même clé et le même certificat**, les Studios le reconnaissent. Les installateurs des versions publiées
ne sont pas dans la sauvegarde (trop gros) : la page Versions les redemande (`fichiers_a_renvoyer`). Restaurer à une
autre adresse que celle de la sauvegarde : le certificat de la même clé de service est signé de nouveau pour elle. Le poste
de l'admin+ intact peut ne remettre **que le service** (son dossier, plus récent que la sauvegarde, ne recule pas).

## 10. Les adresses de secours

Le registre peut porter `fallback_services` : jusqu'à 3 adresses (https) de services de secours, signées avec le reste.
Un Studio qui **ne joint pas** l'adresse principale (une panne réseau, pas une réponse d'erreur) essaie celles-ci, dans
l'ordre ; un document de ce service n'est cru que si son certificat (signé par l'éditeur) nomme cette adresse. Une
adresse de secours se règle dans Service en ligne (admin+ ; 1.47.0 : ou l'admin qui a le droit `offres` pendant sa
dérogation, § 11) ; son service se certifie comme le principal.

## 11. Gérer le service : l'admin+ seul, la dérogation courte ; la connexion qui voyage avec l'installateur (1.47.0)

### Le constat (10/11)

L'admin+ envoyait l'Admin à un futur admin (« Ajouter un admin… » : les notifications de son Studio, ou un lien de
48 h) ; installé sur l'autre PC, l'Admin n'avait **pas l'adresse du service** (`service.json` absent) : « Envoyer ma
demande » restait gris (« Il faut d'abord l'adresse du service… »), Service en ligne disait « pas encore réglé », la
pastille « Service : non réglé » ; il fallait taper une adresse à la main, et la croire sur parole. La console créait
même la clé d'admin avant de voir qu'il n'y avait pas de service (le poste se disait « en attente » sans rien avoir
envoyé). Le kit du service était installé, mais sans `cle-publique.txt`.

### La connexion au service (`connexion-service.xpgsvc`)

C'est **le certificat du service** lui-même : le bloc `service` signé par la clé de l'éditeur (l'adresse, la clé du
service, ses dates ; `licensing/Service.hpp`, `ServiceCert`). Aucun format neuf, rien de secret (`GET /v1/certificat`
le donne à tous) : jamais un jeton Cloudflare, une clé Stripe, une clé privée. Un fichier `connexion-service.xpgsvc`
= ce bloc (`license_admin/ServiceConnection.hpp`).

L'Admin la cherche au lancement, **seulement s'il n'a pas d'adresse et n'a pas la clé de l'éditeur** (l'admin+ règle
son service lui-même), dans l'ordre :

1. `--connexion F.xpgsvc` (la ligne de commande) ;
2. **à côté du programme** : `connexion-service*.xpgsvc` (un navigateur peut l'avoir renommé « connexion-service (1)
   .xpgsvc »). L'installateur l'y copie : celle qu'il **porte** (construit avec `ressources-licences/connexion-service.xpgsvc`,
   `outils/mingw/admin_editeur.sh`), puis celle **posée à côté de lui** (`{src}`, la plus précise : elle remplace l'autre) ;
3. **le dossier des licences du Studio de ce PC** (`%ProgramData%\XPG\Licences`) : `activation.xpgact`,
   `decouverte.xpgdec`, `jeton.xpgjet` portent ce certificat dès que le Studio a parlé au service. C'est le cas du PC qui
   reçoit l'Admin **dans les notifications de son Studio** : aucun Studio n'a à changer (un Studio 1.35 ou plus suffit).

La première **valable** est prise : signée par une clé de l'éditeur que cet Admin croit (celle compilée, `PublisherKey.hpp`,
et celles retenues du service), l'adresse valable. Elle devient `service.json` (l'adresse) et `connexion-service.json`
(d'où elle vient, la clé du service attendue, la date). **Refusées, et dites** (Service en ligne › « La connexion au
service », en rouge ; la console `service-connexion`) : une adresse ou une clé modifiée (la signature ne va plus), un
fichier sans signature, une clé inconnue. « Vérifier » compare ensuite la clé du service à celle que nommait la connexion.

Pourquoi ces trois chemins : le Studio du PC couvre la notification et l'installateur lancé à la main sur un PC qui a un
Studio, **sans changer les Studios** ; le fichier à côté de l'installateur couvre tout le reste (le lien de 48 h, une
clé USB) ; l'installateur publié la porte quand l'éditeur l'a mise dans `ressources-licences/` (« Préparer les fichiers
pour les Studios » / `kit-studios` l'y écrit, ou Service en ligne › « Exporter la connexion… »). Sans rien de tout cela :
« Aucune connexion signée sur ce PC », « Utiliser une connexion… » (le fichier envoyé par l'admin+), ou l'adresse tapée
à la main (un poste sans rôle ou en attente seulement ; elle est dite « tapée ici, sans connexion signée »).
L'admin+ : Service en ligne › « Exporter la connexion… » (le certificat vérifié par « Vérifier », gardé dans son dossier).
Les installateurs (`installateur/XPG-License-Admin.iss`) : **le kit du service est toujours installé** (le composant
« service » est fixe : une dérogation sert sans réinstaller) ; la connexion portée ou posée à côté va dans `{app}`.

### Gérer le service : ce qui est à l'admin+

| Geste | Où | Qui |
|---|---|---|
| Mettre le service en ligne ou à jour (le Worker) | Service en ligne › Déployer le service ; `service-deployer` | admin+, ou l'admin sous dérogation |
| Changer l'adresse du service | Service en ligne › Adresse ; « Utiliser une connexion… » | admin+, ou l'admin sous dérogation |
| Les adresses de secours (le registre) | Service en ligne | admin+, ou l'admin sous dérogation qui a `offres` |
| `cle-publique.txt` du kit | Service en ligne | admin+ (sa clé) ; l'admin sous dérogation : les clés de l'éditeur que le service croit (`GET /admin/admins`, `editeurs`), jamais la sienne |
| Le relevé du jour (`POST /admin/stats/releve`) | (l'alarme, ou à la main) | admin+, ou l'admin sous dérogation |
| Certifier la clé du service | Service en ligne | admin+ seul (sa clé signe) |
| Brancher Stripe, régler la boutique, rembourser | Boutique | admin+ seul (sa clé Stripe ne se partage pas) |
| La sauvegarde complète, la restauration | Paramètres de sécurité | admin+ seul (sa clé d'éditeur y est) |

Un admin voit l'état du service **en lecture seule** : une carte « L'admin+ seul gère le service (dérogation
possible) », l'adresse non modifiable, les boutons gris (ils disent pourquoi) ; « Vérifier » et les relevés restent.

### La dérogation « Gérer le service »

- **L'admin+ l'accorde** à UN admin de la liste (Service en ligne › « Accorder une dérogation… », ou la fiche de
  l'admin › « Dérogation… », ou `derogation-accorder --cle HEX --heures N`) : **1 h, 4 h, 24 h ou 72 h**, jamais plus,
  jamais permanente. Une nouvelle remplace celle en cours.
- **Le document**, signé par la clé de l'éditeur (la signature porte sur `"XPG-DEROGATION-1\n" + texte`) :
  `{"format":1,"id":"<32 hexa>","droit":"service","admin":"<64 hexa>","nom","debut","fin","emise"}` ; `debut` = l'heure
  du service (à 10 min près), `fin - debut` entre 10 min et 72 h. `PUT /admin/derogation` `{"derogation","signature"}` ;
  le service la garde dans `config` (une ligne `derogation` : elle part avec la sauvegarde ; pas de table neuve,
  schéma 5 inchangé).
- **Le service en est l'arbitre, à SON heure** : `POST /admin/derogation/utiliser` `{"action","detail"}` (`deployer`,
  `connexion_cloudflare`, `adresse`, `secours`, `releve`, `verifier`) — l'Admin le demande **avant** chaque geste ; 200
  tant qu'elle court, sinon 403 `derogation` et la phrase (« Ta dérogation … a pris fin », « … a été révoquée », « L'admin+
  seul gère le service : tu n'as pas de dérogation … en cours »). Une route réservée à l'admin+ marquée « dérogation »
  (`POST /admin/stats/releve`) s'ouvre aussi à lui. `GET /admin/derogation` (l'admin+ : celle en place ; un admin : la
  sienne, avec son document signé, que l'Admin vérifie) ; `GET /admin/etat` dit `derogation` (sans le document) à chaque
  regard (15 s). `POST /admin/derogation/revoquer` `{"id"}` (admin+) : finie tout de suite.
- **L'Admin** : la carte « Ta dérogation « Gérer le service » : il reste 52 min (jusqu'à 15:42) », les boutons rouverts ;
  le compte à rebours se fait à l'heure du service (l'écart vu à la lecture) ; **à la fin, la page repasse d'elle-même en
  lecture seule** (même hors ligne), et le regard des 15 s le confirme (accordée, finie, révoquée : un avis).
  `Workspace::mayManageService()`, `whyNotService()`, `derogation()` (`gui/Derogation.hpp` : `activeAt`, `remaining`,
  `derogationText`) — Mon compte les montre.
- **Le journal** : `derogation_accorder` et `derogation_revoquer` (l'Admin de l'admin+, signées par sa clé),
  `derogation_utilisee` (le service, à chaque geste ; 200 lignes au plus par dérogation) et `derogation_expiree` (le
  service, une fois, au premier appel qui la voit finie). `derogation_utilisee` est « Critique, me prévenir tout de suite » :
  **l'admin+ est prévenu** (l'alerte) dès que son Admin relève le journal.
- **Cloudflare** : l'admin sous dérogation met le Worker en ligne **avec son propre compte Cloudflare, sur son PC** —
  l'admin+ l'invite comme **membre** de son compte Cloudflare (tableau de bord › Gérer le compte › Membres) ; **jamais un
  jeton partagé**, jamais le mot de passe de l'admin+. Le kit reçoit les clés publiques de l'éditeur que le service croit
  (`cle-publique.txt`, `PUBLISHER_KEYS`), jamais la sienne ; une mise en ligne qui donne une autre adresse (un autre
  compte Cloudflare) est signalée : ce n'est pas le service de l'éditeur.

## 12. Trois exécutables : Studios, Admin+, Admin ; l'invitation par codes (1.49.0)

### Les trois exécutables

| | Programme | Installateur | Pour qui |
|---|---|---|---|
| **Studios** | `API-Studio.exe`, `IHM-Studio.exe` | `API-IHM-Studio-Setup-<v>.exe` | les clients (inchangé) |
| **Admin+** | `XPG-License-Manager-Admin.exe` (cible `xpg_license_manager_admin`) et la console `XPG-License-Admin.exe` | `XPG-License-Admin-Setup-<v>-EDITEUR.exe` | l'admin+ seul (inchangé) |
| **Admin** (« XPG Admin ») | `XPG-Admin.exe` (cible `xpg_admin_classique`, pas de console) | `XPG-Admin-Setup-<v>.exe` | les admins, reçu avec une invitation |

XPG Admin est le **même code** que l'Admin+ (les bibliothèques `xpg_admin_core`, `xpg_admin_gui` ; le même
`AdminGuiMain.cpp`, compilé avec `XPG_ADMIN_CLASSIQUE`) : son `main` appelle `license_admin::setEdition(Edition::Classique)`
au lancement (`license_admin/Edition.hpp`). Son nom (« XPG Admin » : la fenêtre, le menu, `gui::appName()`), son icône
(`resources/logo/xpg_admin.ico` : la clé sur une tuile violette) et sa ressource de version
(`resources/windows/xpg_admin.rc.in`) sont à lui ; ses données restent dans `%LOCALAPPDATA%\XPG\LicenseAdmin`, comme
avant : un admin qui avait l'Admin d'avant (1.48) garde sa clé d'admin et son archive, rien à refaire.

### Ce que XPG Admin n'offre jamais (le contrôle à l'exécution)

La vraie protection reste la clé de l'éditeur (le service n'accepte que ses signatures pour ce qui est à l'admin+) ;
XPG Admin, lui, **n'offre simplement jamais** ce qui est à l'admin+ (`license_admin::classique()`) :

- **la clé de l'éditeur** : il ne la lit jamais, même posée dans son dossier (`Workspace::loadKey`, `detail::loadCaller`) —
  son rôle n'est donc jamais « admin+ » ; « Créer la clé », « Restaurer une sauvegarde » (de la clé) et « Restaurer une
  sauvegarde complète » ne sont pas dans Paramètres de sécurité ; `createKey`, `restoreKey`, `restoreBackup` refusent ;
- **les pages de l'admin+** (Journal des admins, Droits, Versions, Demandes d'admin) ne sont pas dans le menu ; un lien
  vers l'une d'elles (une autre page, `--page`) est refusé et dit : « « Versions » n'est pas dans XPG Admin : c'est à
  l'admin+ (XPG License Manager Admin). » ;
- **les gestes de l'admin+** : Ajouter / retirer un admin, les droits et profils, publier une version, envoyer un
  fichier ou un message, « Certifier la clé du service » (absent de Service en ligne), accorder une dérogation, Gérer le
  service hors dérogation ; atteint autrement (un essai, un lien), le geste refuse avec « XPG Admin ne fait pas cela :
  c'est à l'admin+ (XPG License Manager Admin, sur le poste qui a la clé de l'éditeur) » (`kClassicRefusal`,
  `detail::callerDenied`) ;
- il **garde** ce que ses droits permettent (§ 2) et la dérogation courte « Gérer le service » (§ 11 ; le kit du service
  est installé avec lui) ;
- **sans clé d'admin** : seulement le chemin de l'admin (l'invitation, ci-dessous) — jamais « Créer la clé de
  l'éditeur… » ni « Restaurer… ».

**L'Admin+ lancé sans la clé de l'éditeur** : seulement « Créer la clé de l'éditeur… » (le premier PC) et « Restaurer la
clé de l'éditeur… », avec la phrase « Ce programme est celui de l'admin+. Un admin installe XPG Admin, reçu avec son
invitation. » (le tableau de bord, Administrateurs, Mon compte) ; **jamais « Demander à devenir admin »**.

Les essais : `admin_gui_edition_test` (ctest `adminguiedition`) — XPG Admin construit dans le même processus
(`setEdition`) sur un poste qui a la clé de l'éditeur, puis chez une admin de la liste (Camille Essai) ; l'Admin+ sans clé.

### Les installateurs

`outils/mingw/admin_editeur.sh` fait **les deux** dans le même dossier (avec `ISCC=…`) :
`XPG-License-Admin-Setup-<v>-EDITEUR.exe` (inchangé) et `XPG-Admin-Setup-<v>.exe` (`installateur/XPG-Admin.iss`, à partir
du dossier `admin-<v>/` : `XPG-Admin.exe`, `SDL3.dll`, la police, le kit du service, `connexion-service.xpgsvc`).

XPG-Admin-Setup a son `AppId`, son nom (« XPG Admin »), son dossier (`%LOCALAPPDATA%\Programs\XPG Admin` ; pour tous :
Program Files), l'habillage de celui de l'éditeur (la colonne des étapes, Barlow) ; pas de console, pas de page de
composants. Il porte la connexion au service (§ 11) comme celui de l'éditeur. Ses étapes : Bienvenue, **Code
d'invitation**, Emplacement, Les options, Ta clé d'admin, Récapitulatif, Installation, Terminé.

- **La page « Code d'invitation »** : obligatoire en mode normal ; le format est vérifié (`INV-` puis 8 caractères de
  l'alphabet Crockford sans I L O U ; tirets, espaces, minuscules tolérés ; remis en forme `INV-XXXX-XXXX`) — faux, la page
  le dit en rouge et ne passe pas. En silencieux : `/INVITATION=INV-XXXX-XXXX` (facultatif ; faux : rien n'est écrit, XPG
  Admin le demandera).
- Le code va dans `%LOCALAPPDATA%\XPG\LicenseAdmin\invitation.txt` (une ligne), réservé au compte (`icacls
  /inheritance:r /grant:r <compte>:F`). Rien d'autre du dossier n'est touché ; la désinstallation ne l'efface pas.

### Les versions : trois sortes

La page **Versions** de l'Admin+ a trois emplacements : **les Studios** (`API-IHM-Studio-Setup-…`), **l'Admin+**
(`XPG-License-Admin-Setup-…-EDITEUR`, facultatif) et **l'Admin** (`XPG-Admin-Setup-…`) ; un `.exe` ou un `.zip` qui le
porte, glissé (le nom décide de l'emplacement) ou choisi ; les trois de la même version. `Workspace::publishVersion(studios,
admin, notes, importante, avancement, adminPlus)`.

| Sorte (`sorte` / `pour`) | Le fichier | Le service le donne à | Proposé par le Studio |
|---|---|---|---|
| `studios` / `tous` | les Studios | tout le monde | à chaque PC pas à jour |
| `admin` / `admins` | XPG Admin | un appel signé par un admin ou l'éditeur, le jeton d'un admin | aux PC des admins (leur jeton d'admin) |
| `admin_plus` / `admin_plus` | l'Admin+ | **la clé de l'éditeur seule** : un appel signé par elle, ou le jeton d'admin qu'elle a signé (le Studio du PC de l'admin+) ; jamais un lien (`POST /admin/fichiers/jeton` : 403), un admin, un client | au PC de l'admin+ seul |

- **Le bloc de version** : les fichiers `studios` et `admin` restent dans `fichiers` ; ceux de l'Admin+ vont dans
  **`fichiers_admin_plus`** (`[{"id","nom","taille","sha256","sorte":"admin_plus","pour":"admin_plus"}]`). Pourquoi : un
  Studio d'avant (1.44 à 1.48) refuse toute la version si `fichiers` porte une sorte qu'il ne connaît pas — il ne verrait
  plus aucune mise à jour. Le service et l'Admin les relisent ensemble (`GET /admin/versions` les montre tous). Un bloc
  d'avant (sorte `admin` seule) reste valable.
- **Le Studio** (`app::updates::Offer`) : un client, les Studios ; le PC d'un admin (son jeton d'admin), les Studios et
  XPG Admin (`adminKind` « admin ») ; le PC de l'admin+ (le jeton signé par une clé de l'éditeur), les Studios et
  l'Admin+ (« admin_plus »), jamais XPG Admin. Un admin qui a l'ancien Admin (1.48) reçoit l'offre « admin » : XPG Admin
  s'installe à côté, avec sa clé et ses données (le même dossier).
- « Ajouter un admin… » envoie le fichier `admin` de la dernière version publiée — jamais un `XPG-License-Admin-Setup`
  (l'Admin d'avant, celui de l'admin+).

Les essais : `service/test/versions149.test.js` (les droits du fichier `admin_plus`, le bloc à trois sortes, un bloc
d'avant), `service_fichiers` (`licence_fichiers_test` : le Studio du PC de l'admin+ reçoit l'Admin+, un client les
Studios seuls), `adminguiversions` (les trois emplacements, les `.zip`, la publication), `adminsfichiers`.

### L'invitation par codes

Deux codes : un **code d'invitation** (l'admin+ le crée, le futur admin le saisit dans l'installateur XPG Admin) et un
**code de confirmation** à 6 chiffres (l'Admin du futur admin le montre, l'admin+ le tape). Ils remplacent « J'ai vérifié
l'empreinte » pour les admins invités en 1.49 ; une demande **sans invitation** (un Admin d'avant la 1.49, une demande
déjà en attente) garde l'empreinte et sa case.

1. **L'admin+, « Ajouter un admin… »** : à l'envoi de l'installateur (« Envoyer l'Admin » ou « Créer le lien »), l'Admin
   crée d'abord l'invitation : `POST /admin/admins/invitation` (admin+ seul, jamais par une dérogation)
   `{"nom","courriel","profil"}` → 201 `{"id":"inv-<16 hexa>","code":"INV-XXXX-XXXX","expire":<48 h>}`. Le code :
   8 caractères tirés au hasard dans l'alphabet Crockford sans I L O U (`0-9 A-H J K M N P-T V-Z`, 40 bits), valable
   **48 h**, **une seule fois**. Le dialogue le montre **une fois**, en grand, avec « Copier » et « valable 48 h, une seule
   fois » ; il part dans le message : la notification du Studio (« Installer XPG Admin ») ou le texte copié avec le lien
   de 48 h — « Ton code d'invitation : INV-XXXX-XXXX (à saisir dans l'installateur, valable 48 h) ». L'installateur envoyé
   est celui de la sorte `admin` (XPG Admin) de la dernière version publiée ; sinon le fichier choisi
   (`XPG-Admin-Setup-….exe`). L'invitation est notée dans `invitations-admins.json` (son `id`, `expire` ; **jamais le
   code**) et une ligne **`admin_inviter`** part au journal (détail : nom, courriel, profil, invitation, expire ; jamais le
   code).
2. **Le service** ne garde que `SHA-256(code)` (hexa) : la ligne de config **`admins_invitations`** (un tableau JSON, 100
   au plus : `id`, `hash`, `nom`, `courriel`, `profil`, `cree`, `expire`, `servie`, `demande` — la clé qui l'a servie —,
   `servie_le`) ; elle part avec la sauvegarde (la table `config`). Une invitation finie est oubliée 30 jours après son
   expiration, sauf si sa demande attend encore. **« Repartir de zéro » l'efface** (comme les demandes d'admin).
3. **XPG Admin, premier lancement sans clé d'admin** (`license_admin::classique()`) : il lit
   `%LOCALAPPDATA%\XPG\LicenseAdmin\invitation.txt` (une ligne, posée par l'installateur ; minuscules, espaces, tirets
   tolérés) ; absent, la carte « Demander à devenir admin » a le champ **« Code d'invitation »**. Il crée sa clé, envoie
   `POST /v1/admins/demande` (signée par sa clé) avec `"invitation":"INV-…"` en plus. Le service vérifie : connu, pas servi
   (par une autre clé : la même clé peut renvoyer sa demande), pas expiré → marque servie, lie la demande, répond
   201 `{"etat","empreinte","invitation":{"id","nom","profil"}}` ; sinon **403** `{"erreur":"invitation","message":…}` :
   « Ce code d'invitation est inconnu. », « Ce code d'invitation a expiré (48 h). », « Ce code d'invitation a déjà
   servi. ». `invitation.txt` est **effacé dès que le service a répondu** (accepté ou refusé) ; un code refusé se
   **retape** dans le champ (la phrase du service est dite). Accepté : l'écran d'attente montre en grand
   « Dis ce code à ton admin+ : 123 456 » et relève `GET /v1/admins` comme avant. L'Admin+ ne montre jamais ce chemin.
4. **Le code de confirmation** : `n` = l'entier big-endian (64 bits) des 8 premiers octets de
   `SHA-256("xpg-confirmation-1" || clé publique brute, 32 octets)` ; code = `n % 1000000`, sur 6 chiffres, affiché
   « 123 456 ». Il ne passe **jamais** par le service : chaque Admin le calcule de la clé (`licensing::admins::
   confirmationCode`, la même formule des deux côtés ; valeur connue : la clé `00 01 02 … 1f` → « 763 090 », vérifiée par
   `admin_invitation_test` et `service/test/invitations.test.js`).
5. **L'admin+, Demandes d'admin** : `GET /admin/admins/demandes` rend, pour une demande qui a servi une invitation,
   `"invitation":{"id","nom","courriel","profil","invitee","expire","servie"}` (jamais l'empreinte du code). La liste montre
   son id exact ; « Accepter… » demande de **taper les 6 chiffres** (« 123 456 » ou « 123456 ») à la place de la case ;
   faux : **rien n'est signé**, la phrase le dit (« Ce code ne correspond pas à sa clé : rien n'est signé… »). Accepter
   plusieurs : un champ par demande avec invitation, la case de l'empreinte pour les autres ; un seul code faux et rien
   n'est signé. Le reste ne change pas (une liste signée, le trousseau, une ligne `admin_ajouter` par admin, son détail
   dit l'invitation). `Workspace::acceptAdmin` / `acceptAdmins` le refont : une demande avec invitation exige ses 6 chiffres
   (la case ne suffit pas).

Les essais : `service/test/invitations.test.js` (créer : l'admin+ seul ; le servir, la liste ; inconnu, déjà servi,
expiré à 49 h ; une demande sans invitation ; le code jamais gardé) ; `admin_invitation_test` (contre le service local :
l'invitation, XPG Admin qui lit `invitation.txt`, les 6 chiffres, un faux code puis le bon, l'admin se voit admin ;
inconnu puis retapé, déjà servi, expiré).
