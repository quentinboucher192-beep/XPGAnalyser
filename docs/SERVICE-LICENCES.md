# Le service des licences (1.35.0) : le contrat

Le service des licences est un Worker Cloudflare (`service/`, JavaScript, Durable Objects avec SQLite), mis en ligne par l'éditeur sur son compte (gratuit). Il remplace GitHub pour **tout** : les demandes des clients, l'activation par code, les licences flottantes, l'heure signée, la Découverte, et la synchro privée de l'archive de l'éditeur.

Ce document est **le contrat** entre trois programmes : le Worker (`service/`), les Studios (`src/licensing`, `src/app`) et l'outil de l'éditeur (`src/license_admin`). Les formats C++ sont dans `src/licensing/Service.hpp`.

## 1. Les clés et la confiance

- **La clé de l'éditeur** (Ed25519) reste **hors ligne**, sur le poste de l'éditeur. Les Studios ont sa partie publique compilée (`PublisherKey.hpp`). Le Worker la reçoit en variable publique `PUBLISHER_KEYS` (hexadécimal, plusieurs séparées par des virgules) : elle authentifie les appels de l'éditeur.
- **La clé de service** (Ed25519) est **créée par le Worker** lui-même, à son premier appel, et ne le quitte jamais. Elle est gardée dans le stockage du Durable Object. Elle signe les activations, les jetons des licences flottantes, l'heure et les Découvertes.
- **Le certificat de service** : un bloc signé par la clé de l'éditeur (genre `service`), qui dit « cette clé de service est la mienne, jusqu'à telle date, pour ces droits ». L'éditeur le signe dans son outil, après avoir lu la clé publique de service (`GET /admin/cle-service`), puis le dépose (`PUT /admin/certificat`). Les Studios ne croient un document du service que s'il est signé par la clé que nomme un certificat valide.
- **La clé du PC** (Ed25519, `identite-pc.cle`) authentifie chaque appel d'un Studio. Aucun jeton partagé n'est publié nulle part.

## 2. Les appels signés

Tout appel `/v1/…` qui touche à un PC, et tout appel `/admin/…`, porte quatre en-têtes :

| En-tête | Valeur |
|---|---|
| `X-XPG-Cle` | la clé publique, 64 hexadécimaux minuscules (le PC, ou l'éditeur) |
| `X-XPG-Date` | l'heure, en secondes Unix (décimal) |
| `X-XPG-Nonce` | 32 hexadécimaux minuscules, tirés au hasard |
| `X-XPG-Signature` | 128 hexadécimaux : la signature Ed25519 du message ci-dessous |

Le message signé (octets, `\n` = 0x0A) :

```
XPG-SERVICE-1\n<METHODE>\n<chemin et requête, tels qu'envoyés>\n<date>\n<nonce>\n<sha256 hexadécimal du corps>
```

Un corps vide a l'empreinte de la chaîne vide. Le Worker refuse (401) :
- une date à plus de 600 s de son heure ;
- un nonce déjà vu dans les 20 dernières minutes ;
- une signature fausse.

Pour `/admin/…`, la clé doit en plus être dans `PUBLISHER_KEYS` (sinon 403). Un Studio dont l'horloge est fausse lit d'abord l'heure du service (`GET /v1/etat`) et signe avec l'heure corrigée.

L'**identifiant d'un poste** : `deviceId(clé)` = les 32 premiers hexadécimaux de SHA-256(`"xpg-poste-passerelle/1"` ‖ clé publique), comme en 1.34.

## 3. Les documents signés par le service

Ce sont des blocs `XPG-LICENSING 1 <genre> <taille>` (voir `SignedFile.hpp`), signés par la **clé de service**. Le JSON est **exactement** celui-ci : pas d'autre champ, dans cet ordre, sans espaces ; les dates en `AAAA-MM-JJTHH:MM:SSZ`.

| Genre | Signé par | JSON |
|---|---|---|
| `service` | l'éditeur | `{"version":1,"cle":"<64 hexa>","url":"https://…","emis":"…","expire":"…","droits":["activation","flottante","heure","decouverte"]}` |
| `activation` | le service | `{"version":1,"licence":"<id>","emission":"<emission de la licence>","poste":"<64 hexa>","station":"<32 hexa>","type":"fixe"\|"flottante","numero":N,"emis":"…","jusqu_a":"…"}` (`station` absente si vide) |
| `jeton` | le service | `{"version":1,"licence":"<id>","poste":"<64 hexa>","emis":"…","jusqu_a":"…"}` |
| `heure` | le service | `{"version":1,"heure":"…","n":"<32 hexa, le nonce du client>"}` |
| `decouverte` | le service | `{"version":1,"poste":"<64 hexa>","machine":"<32 hexa>","debut":"…","jours":N}` |

Les licences, le registre, les révocations et l'état d'un PC (`poste`, 1.34) restent signés par **l'éditeur**, comme avant.

## 4. Le bail d'une activation (la désactivation même hors ligne)

- Une activation vaut **jusqu'à `jusqu_a`** : `emis` + le bail de la licence (`bail_jours`, 30 par défaut, réglé par l'éditeur).
- **À chaque connexion**, le Studio la renouvelle (`POST /v1/renouveler`) et reçoit une heure signée.
- **Un PC libéré, retiré ou révoqué** n'est plus renouvelé. S'il se connecte, le service le lui dit et il efface son activation. S'il reste hors ligne à vie, il s'arrête à `jusqu_a`.
- **Le compteur** (dans `etat.bin` et ses copies) : la dernière heure signée du service, et le temps écoulé depuis. Ce temps ne fait qu'avancer : les écarts positifs de l'horloge, et le temps où le programme tourne. L'heure retenue est le **maximum** de l'horloge du PC et de « dernière heure signée + temps écoulé ». Reculer l'horloge ne prolonge donc pas un bail. À la connexion suivante, l'heure signée recale tout.
- **Sept jours avant la fin**, le Studio prévient : « connecte ce PC à Internet avant le … ».

## 5. Les routes

Les réponses JSON ont `Content-Type: application/json`. Une erreur rend `{"erreur":"<code>","message":"<phrase en français>"}` ; jamais un secret, jamais la valeur d'un code.

### Publiques (sans signature)

| Route | Réponse |
|---|---|
| `GET /v1/etat` | `{"service":"xpg-licences","version":"1.35.0","heure":<unix>,"cle_service":"<hexa>"\|null,"certifie":true\|false,"registre":<numéro>\|0}` |
| `GET /v1/heure?n=<32 hexa>` | le bloc `heure` (texte) ; 503 sans certificat |
| `GET /v1/certificat` | le bloc `service` (texte) ; 404 sans certificat |
| `GET /v1/registre` | le bloc du registre (texte) ; 404 s'il n'y en a pas |

### Le PC (signées par la clé du PC)

| Route | Corps | Réponse |
|---|---|---|
| `POST /v1/activer` | `{"code":"<le code>","demande":"<le .xpgreq, signé par ce PC>","nom":"<nom du PC>"}` | 200 `{"licence":"<bloc>","activation":"<bloc>","certificat":"<bloc>","message":"…"}`. Erreurs : 403 `code` (inconnu ou désactivé, sans dire lequel), 409 `complet`, 410 `revoquee` ou `expiree`, 400 `demande`. Déjà activé : la même activation, renouvelée. |
| `POST /v1/renouveler` | `{}` | 200 `{"activations":[{"licence":"<bloc licence>","activation":"<bloc>"}],"retirees":["<id>",…],"certificat":"<bloc>","revocations":"<bloc>"\|null,"poste":"<bloc poste>"\|null}` |
| `POST /v1/liberer` | `{"licence":"<id>"}` | 200 `{}` : le poste est rendu, et l'activation de ce PC n'est plus renouvelée |
| `POST /v1/decouverte` | `{"machine":"<32 hexa>"}` | 200 : le bloc `decouverte` (texte). `debut` = la première fois que cette machine a été vue ; `jours` = `DECOUVERTE_JOURS` (30) |
| `POST /v1/flottante/jeton` | `{"licence":"<id>"}` | 200 `{"jeton":"<bloc>"}` (15 min) ; 409 `complet` avec `"libre_dans":<secondes>` ; 403 `activation` si ce PC n'a pas d'activation flottante pour cette licence |
| `POST /v1/flottante/rendre` | `{"licence":"<id>"}` | 200 `{}` |
| `POST /v1/demandes` | `{"cle":"<id de la clé de l'éditeur>","scelle":"<base64>"}` | 201 `{"numero":N}` ; une seule demande en attente par PC (l'ancienne passe « remplacée ») |
| `POST /v1/demandes/annuler` | `{"numero":N}` | 200 `{}` |
| `GET /v1/poste` | — | 200 `{"poste":"<bloc poste>"\|null}` |

### L'éditeur (signées par sa clé)

| Route | Corps | Réponse |
|---|---|---|
| `GET /admin/cle-service` | — | `{"cle":"<hexa>","certificat":"<bloc>"\|null}` |
| `PUT /admin/certificat` | `{"certificat":"<bloc service>"}` | 200 ; refusé si sa clé n'est pas celle du service |
| `PUT /admin/registre` | `{"registre":"<bloc>"}` | 200 ; refusé s'il n'est pas plus récent |
| `PUT /admin/revocations` | `{"revocations":"<bloc>"}` | 200. Les activations des licences révoquées ne sont plus renouvelées. La liste ne part qu'aux PC concernés, jamais en public. |
| `PUT /admin/licences` | `{"licence":"<bloc>","code_hash":"<64 hexa>"\|absent,"bail_jours":30,"active":true}` | 200 : la licence en ligne (ses postes = `max_devices`, flottante si son type l'est) |
| `GET /admin/licences` | — | `{"licences":[{"licence":"<id>","emission":"…","type":"…","postes_max":N,"bail_jours":N,"active":bool,"code":bool,"activations":[{"poste":"<64 hexa>","nom":"…","station":"…","activee":"…","vue":"…","jusqu_a":"…","etat":"active"\|"liberee"\|"retiree"\|"revoquee"}],"jetons":[{"poste":"…","jusqu_a":"…"}]}]}` |
| `POST /admin/activations/retirer` | `{"licence":"<id>","poste":"<64 hexa>"}` | 200 |
| `GET /admin/demandes?depuis=N` | — | `{"demandes":[{"numero":N,"poste":"<32 hexa>","cle":"<id>","scelle":"<base64>","etat":"attente"\|"annulee"\|"remplacee"\|"traitee","cree":"…","maj":"…"}]}` |
| `POST /admin/demandes/traiter` | `{"numero":N}` | 200 |
| `PUT /admin/postes` | `{"poste":"<32 hexa>","etat":"<bloc poste>"}` | 200 (lu par `GET /v1/poste`) |
| `GET /admin/archive` | — | `{"version":V,"fichiers":[{"chemin":"…","sha256":"…","taille":N}]}` |
| `GET /admin/archive/fichier?chemin=…` | — | le texte du fichier (chiffré par l'éditeur avec sa clé de synchro) |
| `POST /admin/archive` | `{"base":V,"fichiers":{"<chemin>":"<texte>"\|null}}` | 200 `{"version":V+1}` ; 409 `{"version":<actuelle>}` si `base` n'est pas la version actuelle |
| `GET /admin/stats` | — | les compteurs (licences, activations, demandes, appels du jour) |

### Les limites (le plan gratuit, et les abus)

- Corps : 2 Mio au plus. Un fichier de l'archive : 1,5 Mio au plus.
- Par adresse IP et par heure : `activer` 10, `decouverte` 10, `heure` 120, `demandes` 10.
- Par PC : `activer` 5 par heure, `renouveler` 30 par heure, `flottante/jeton` 60 par heure, `demandes` 5 par jour.
- Au-delà : 429 `trop` avec `"reessayer_dans":<secondes>`.
- Au plus 1 000 demandes en attente, 10 000 licences et 100 000 activations.

## 6. Le code d'activation

- Sa forme : `XPG-XXXX-XXXX-XXXX-XXXX`, 16 caractères en base32 de Crockford, soit 80 bits tirés au hasard par l'outil de l'éditeur.
- Il est lu avec souplesse : minuscules, espaces, `O` pour `0`, `I` ou `L` pour `1`.
- Le service n'en garde que l'**empreinte** : hex(SHA-256(`"xpg-code/1|"` + les 16 caractères normalisés)).
- L'éditeur voit le code une fois, à la création, et le donne à son client.

## 7. La Découverte (l'essai de 30 jours)

- Au premier lancement, le Studio commence sa Découverte sur le PC : c'est l'évaluation de la 1.34, renommée. Elle marche hors ligne.
- En ligne, il la déclare au service (`POST /v1/decouverte`) avec l'empreinte de la machine : `machine` = les 32 premiers hexadécimaux de SHA-256(`"xpg-decouverte/1|"` + l'identifiant de l'installation Windows). Le service rend le **premier début connu pour cette machine**, et le Studio retient le plus ancien des deux.
- Réinstaller Windows ou effacer ses fichiers ne relance donc pas l'essai. L'empreinte ne dit rien du matériel, mais elle reconnaît la machine : c'est dit dans la fenêtre Licence.

## 8. Ce qui reste sur le PC (le dossier des licences)

| Fichier | Contenu |
|---|---|
| `licence.xpglic` | la licence : un PC lié par l'éditeur (comme avant), ou une licence en ligne (sans PC) |
| `activation.xpgact` | le bloc `activation` de ce PC, et le bloc `service` (le certificat) |
| `jeton.xpgjet` | le jeton d'une licence flottante (15 min) |
| `decouverte.xpgdec` | le bloc `decouverte` |
| `code-activation.txt` | un code laissé par l'installateur, en attente du premier lancement en ligne (effacé après) |
| `etat.bin` | aussi le compteur du bail (§ 4) |
