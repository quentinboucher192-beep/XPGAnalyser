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
sa criticité ; publier une version ; envoyer un fichier ; le certificat du service.

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
le droit de chaque partie qui change (`editions`, `options`, `annonces` comparées au registre en place ; une autre
partie qui change : `offres`) ; etc. (§ 6).

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
  retiré n'est valide que si son empreinte est dans `gardes`.
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
2. **Le futur admin** installe l'Admin. Sans clé, l'Admin propose « Demander à devenir admin » : son nom, son courriel ;
   il crée sa clé (`cle-admin.cle`) et dépose sa **demande d'admin** : `POST /v1/admins/demande`, signée par **sa clé**
   (les en-têtes habituels), corps `{"nom","courriel","pc","poste","logiciel"}`. Le service la garde (une par clé ; au
   plus 20 en attente) et l'écrit au journal. L'Admin montre **l'empreinte** : les 24 premiers hexadécimaux en
   majuscules du SHA-256 de la clé publique, en 6 groupes de 4 (`B7E2 48A9 1D3C 90F6 5A2E C471`).
3. **L'admin+** voit la demande (« En attente d'acceptation » dans Administrateurs, `GET /admin/admins/demandes`) ;
   ils lisent l'empreinte **au téléphone**, il coche « J'ai vérifié l'empreinte », accepte : la clé de l'éditeur signe
   la nouvelle liste (numéro + 1, avec ses droits), `PUT /admin/admins` la publie ; **le trousseau** (§ 5) lui est
   déposé ; la demande est close. Refuser : `POST /admin/admins/demande/refuser`.
4. L'Admin du nouvel admin relève `GET /v1/admins` (toutes les 15 s tant qu'il attend) : sa clé y est → il est admin.
   Il prend le trousseau, la clé de synchro, et relève l'archive.

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
`clients_exporter`, `courriel`, `admin_ajouter`, `admin_retirer`, `admin_droits`, `admin_profil`, `version_publier`,
`fichier_envoyer`, `criticite`, `connexion`, et ceux du service (`signature_refusee`, `autre_pc`, `demande_admin`).

### Les fichiers

Un fichier est déposé en morceaux (1 Mio au plus) par l'admin+ : `POST /admin/fichiers` `{"manifeste":"<bloc fichier>"}`
→ `{"id"}` ; puis `PUT /admin/fichiers/morceau?id=…&n=…` (le corps : les octets) ; le service le marque complet quand
tous les morceaux sont là. Le **manifeste** (genre `fichier`, signé par la clé de l'éditeur) :
`{"format":1,"id":"<32 hexa>","nom":"XPG-License-Admin-Setup-1.44.0.exe","taille":6595615,"sha256":"<64 hexa>",
"sorte":"admin|studios|autre","version":"1.44.0","acces":"public|admins|jeton","morceau":1048576,"emis":…}`.

Lire : `GET /v1/fichiers/manifeste?id=…`, `GET /v1/fichiers/morceau?id=…&n=…` ; l'accès `public` : tout le monde ;
`admins` : un appel signé par une clé d'admin (ou de l'éditeur), ou l'en-tête `X-XPG-Jeton` d'un jeton d'admin (§ 8) ;
`jeton` : `?jeton=…` (un jeton tiré par l'admin+, gardé haché ; 48 h ; « une seule fois » : utilisable pour un seul
téléchargement complet). `GET /v1/fichiers/lien?jeton=…` rend le fichier entier (un navigateur). **Le Studio vérifie
toujours** la taille et le SHA-256 contre le manifeste signé ; un fichier qui ne correspond pas est **refusé**
(jamais proposé, ni enregistré).

Les fichiers d'une version gardent leurs morceaux tant que la version est l'une des 3 dernières ; un fichier joint à un
message, 30 jours.

### Les messages à un PC

`PUT /admin/messages` (admin+) `{"poste":"<deviceId>","bloc":"<bloc message>"}` ; le Studio relève
`GET /v1/messages?depuis=N` (signé par la clé du PC) : ses messages, numérotés par le service. Le bloc (genre `message`,
signé par la clé de l'éditeur) : `{"format":1,"poste":"<deviceId>","titre","de","emis","scelle":"<base64>"}` ; le scellé
(pour la clé du PC) : `{"texte","fichiers":[{"id","nom","taille","sha256","version","jeton"}],"action":"admin"}`.

### Les versions

`PUT /admin/versions` (admin+) `{"bloc":"<bloc version>"}` ; `GET /v1/version` (publique) : la plus récente ;
`GET /v1/etat` rend `version` (« 1.44.1 »). Le bloc (genre `version`, signé par la clé de l'éditeur) :

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
