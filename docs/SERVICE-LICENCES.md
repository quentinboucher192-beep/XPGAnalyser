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
| `activation` | le service | `{"version":1,"licence":"<id>","emission":"<emission de la licence>","poste":"<64 hexa>","station":"<32 hexa>","type":"fixe"\|"flottante","numero":N,"emis":"…","jusqu_a":"…"}` (`station` : toujours là, le service refuse une demande sans elle) |
| `jeton` | le service | `{"version":1,"licence":"<id>","poste":"<64 hexa>","emis":"…","jusqu_a":"…"}` |
| `heure` | le service | `{"version":1,"heure":"…","n":"<32 hexa, le nonce du client>"}` |
| `decouverte` | le service | `{"version":1,"poste":"<64 hexa>","machine":"<32 hexa>","debut":"…","jours":N}` |

Les licences, le registre, les révocations et l'état d'un PC (`poste`, 1.34) restent signés par **l'éditeur**, comme avant.

## 4. Le bail d'une activation (la désactivation même hors ligne)

- Une activation vaut **jusqu'à `jusqu_a`** : `emis` + le bail de la licence (`bail_jours`, 30 par défaut, réglé par l'éditeur).
- **À chaque connexion**, le Studio la renouvelle (`POST /v1/renouveler`) et reçoit une heure signée.
- **Un PC libéré, retiré ou révoqué** n'est plus renouvelé. S'il se connecte, le service le lui dit et il efface son activation. S'il reste hors ligne à vie, il s'arrête à `jusqu_a`.
- **Une licence finie** (`expire` passé) n'est plus renouvelée : le PC la garde, et la dit finie (avec sa grâce) d'après ses propres dates. Une émission plus récente, déposée par l'éditeur, la prolonge.
- **La place d'un PC libéré** (§ 4 bis) revient tout de suite dans les changements de PC permis ; au-delà, elle reste prise jusqu'à son `jusqu_a` : son activation, gardée hors ligne, vaut jusque-là.
- **Le compteur** (dans `etat.bin` et ses copies) : la dernière heure signée du service, et le temps écoulé depuis. Ce temps ne fait qu'avancer : les écarts positifs de l'horloge, et le temps où le programme tourne. L'heure retenue est le **maximum** de l'horloge du PC et de « dernière heure signée + temps écoulé ». Reculer l'horloge ne prolonge donc pas un bail. À la connexion suivante, l'heure signée recale tout.
- **Sept jours avant la fin**, le Studio prévient : « connecte ce PC à Internet avant le … ».

## 4 bis. La station et les changements de PC

- **La station** : l'empreinte de l'installation de Windows du PC (`stationHash` : l'identifiant de l'installation et la clé du PC, 32 hexadécimaux). Le `.xpgreq` envoyé à `/v1/activer` la porte (sinon : 400 `demande`) ; le service la garde avec l'activation. `/v1/renouveler`, `/v1/flottante/jeton` et `/v1/flottante/rendre` la portent aussi (`"station"`, obligatoire) ; `/v1/liberer` peut la porter. Une autre station que celle de l'activation : rien n'est renouvelé (motif `autre_poste`), aucun jeton n'est donné ni rendu (403 `activation`, « cette activation est pour un autre poste »). Les jetons flottants sont tenus par (licence, clé, station). Une clé de PC recopiée sur d'autres machines n'y sert donc à rien : chaque copie est une autre station.
- **Activer avec la clé d'un PC déjà actif pour cette licence, depuis une autre station**, pendant son bail : 403 `activation` (la copie ne prend pas la place de l'original).
- **Les changements de PC** (une licence fixe) : un PC qui se libère lui-même (`/v1/liberer`) rend sa place tout de suite au plus **max(2, postes)** fois par licence sur **30 jours glissants**. Au-delà, sa place reste prise jusqu'à la fin de son bail (`place_jusqu_a` dans `GET /admin/licences`) ; le refus d'un autre PC (409 `complet`) le dit, avec `"libre_le"`. Une place retirée par l'éditeur (`/admin/activations/retirer`), ou d'une licence qu'il désactive, revient **tout de suite** (sa décision). Une licence flottante n'a pas de place à garder (son jeton est rendu).
- **Le ménage** (une fois par jour) : une activation libérée ou retirée est effacée 7 jours après son `jusqu_a`, une activation active 90 jours après (un PC parti sans se libérer : revenu, il s'active de nouveau avec son code) ; le Studio efface alors l'activation dont le service ne dit plus rien. Une licence a au plus **10 × postes + 10** lignes d'activation (flottante : **max(50, 20 × postes)**) : au-delà, 409 `limite` pour CETTE licence seulement.

## 5. Les routes

Les réponses JSON ont `Content-Type: application/json`. Une erreur rend `{"erreur":"<code>","message":"<phrase en français>"}` ; jamais un secret, jamais la valeur d'un code.

### Publiques (sans signature)

| Route | Réponse |
|---|---|
| `GET /v1/etat` | `{"service":"xpg-licences","version":"1.38.0","heure":<unix>,"cle_service":"<hexa>"\|null,"certifie":true\|false,"registre":<numéro>\|0}` |
| `GET /v1/heure?n=<32 hexa>` | le bloc `heure` (texte) ; 503 sans certificat |
| `GET /v1/certificat` | le bloc `service` (texte) ; 404 sans certificat |
| `GET /v1/registre` | le bloc du registre (texte) ; 404 s'il n'y en a pas |

### Le PC (signées par la clé du PC)

| Route | Corps | Réponse |
|---|---|---|
| `POST /v1/activer` | `{"code":"<le code>","demande":"<le .xpgreq, signé par ce PC, avec sa station>","nom":"<nom du PC>"}` | 200 `{"licence":"<bloc>","activation":"<bloc>","certificat":"<bloc>","message":"…"}`. Erreurs : 403 `code` (inconnu ou désactivé, sans dire lequel), 409 `complet` (avec `"libre_le":"<date>"` quand une place gardée par un PC libéré revient), 409 `limite` (les lignes d'activation de cette licence, § 4 bis), 410 `revoquee` ou `expiree`, 400 `demande` (illisible, d'un autre PC, ou sans station), 403 `activation` (la clé de ce PC, active pour cette licence sur une autre station). Déjà activé : la même activation, renouvelée. |
| `POST /v1/renouveler` | `{"station":"<32 hexa>"}` | 200 `{"activations":[{"licence":"<bloc licence>","activation":"<bloc>"}],"retirees":["<id>",…],"motifs":{"<id>":"liberee"\|"retiree"\|"revoquee"\|"desactivee"\|"expiree"\|"autre_poste"\|"inconnue"},"certificat":"<bloc>","poste":"<bloc poste>"\|null,"postes":{"<id>":{"max":N,"type":"<type de la licence>","utilises":N,"total":N,"liste":[{"nom":"…","cle":"<64 hexa>","activee":"<date>","vue":"<date>","jusqu_a":"<date>","etat":"active"\|"liberee","ce_pc":true\|false,"jeton":true\|false}]}}}`. La liste de révocation de l'éditeur n'y est jamais : une révocation est un motif. **`postes` (1.38.0, « Mes appareils » dans le Studio, qui renouvelle à l'ouverture de l'onglet quand sa liste a plus de 10 minutes)** : pour chaque licence renouvelée par cet appel, et elles seules (jamais une licence où ce PC n'est pas actif, ni une licence retirée), les PC qui y tiennent une place, c'est-à-dire ceux que le service compte pour refuser un PC de trop : les activations actives dans leur bail, et les PC libérés au-delà des changements permis, jusqu'à la fin de leur bail (`etat` `liberee`, `jusqu_a` = la fin de la place gardée, § 4 bis). Jamais un PC révoqué, jamais la station ni le numéro d'un autre PC. Ce PC en tête (`ce_pc` : `true` sur lui seul), puis les autres, les plus récemment activés d'abord ; **500 au plus** (`total` : leur nombre sans cette borne). `utilises` : les places prises d'une licence fixe ; pour une flottante, les **jetons en cours** (c'est eux qui la limitent : elle s'active sur autant de PC qu'on veut), et `jeton` (flottante seulement) dit quel PC en tient un. « 3 sur 20 utilisés » = `utilises` sur `max`. Un Studio d'avant ignore ce champ. 400 `champ` sans station. |
| `POST /v1/liberer` | `{"licence":"<id>","station":"<32 hexa>"}` (station facultative) | 200 `{"place":"rendue"\|"gardee","jusqu_a":"<date>"(gardee),"transferts":N,"transferts_permis":M,"message":"…"}` : l'activation de ce PC n'est plus renouvelée ; sa place est rendue tout de suite, ou gardée jusqu'à la fin de son bail (§ 4 bis). 403 `activation` : une autre station. |
| `POST /v1/decouverte` | `{"machine":"<32 hexa>"}` | 200 : le bloc `decouverte` (texte). `debut` = la première fois que cette machine a été vue ; `jours` = `DECOUVERTE_JOURS` (30) |
| `POST /v1/flottante/jeton` | `{"licence":"<id>","station":"<32 hexa>"}` | 200 `{"jeton":"<bloc>"}` (15 min) ; 409 `complet` avec `"libre_dans":<secondes>` ; 403 `activation` si ce PC n'a pas d'activation flottante pour cette licence, ou depuis une autre station ; 400 `champ` sans station |
| `POST /v1/flottante/rendre` | `{"licence":"<id>","station":"<32 hexa>"}` | 200 `{}` ; 403 `activation` depuis une autre station |
| `POST /v1/demandes` | `{"cle":"<id de la clé de l'éditeur>","scelle":"<base64, 64 Kio au plus>"}` | 201 `{"numero":N}` ; une seule demande en attente par PC (l'ancienne passe « remplacée ») ; 507 `limite` quand le service garde déjà 20 Mio de demandes |
| `POST /v1/demandes/annuler` | `{"numero":N}` | 200 `{}` |
| `GET /v1/poste` | — | 200 `{"poste":"<bloc poste>"\|null}` |

### L'éditeur (signées par sa clé)

| Route | Corps | Réponse |
|---|---|---|
| `GET /admin/cle-service` | — | `{"cle":"<hexa>","certificat":"<bloc>"\|null}` |
| `PUT /admin/certificat` | `{"certificat":"<bloc service>"}` | 200 ; refusé si sa clé n'est pas celle du service |
| `PUT /admin/registre` | `{"registre":"<bloc>"}` | 200 ; refusé s'il n'est pas plus récent |
| `PUT /admin/revocations` | `{"revocations":"<bloc>"}` | 200. Les activations des licences révoquées ne sont plus renouvelées (motif `revoquee`). La liste ne sort jamais du service : un PC lié par l'éditeur reçoit sa révocation dans son état « poste », scellé pour lui. |
| `PUT /admin/licences` | `{"licence":"<bloc>","code_hash":"<64 hexa>"\|absent,"bail_jours":30,"active":true}` | 200 : la licence en ligne (ses postes = `max_devices`, flottante si son type l'est). Désactivée : les places gardées par des PC libérés reviennent. |
| `GET /admin/licences` | — | `{"licences":[{"licence":"<id>","emission":"…","type":"…","postes_max":N,"bail_jours":N,"active":bool,"code":bool,"activations":[{"poste":"<64 hexa>","nom":"…","station":"…","activee":"…","vue":"…","jusqu_a":"…","etat":"active"\|"liberee"\|"retiree"\|"revoquee","place_jusqu_a":"…"\|null}],"jetons":[{"poste":"…","jusqu_a":"…"}]}]}` (`place_jusqu_a` : un PC libéré au-delà des changements permis garde sa place jusque-là) |
| `POST /admin/activations/retirer` | `{"licence":"<id>","poste":"<64 hexa>"}` | 200 : la place revient tout de suite, même gardée |
| `GET /admin/demandes?depuis=N` | — | `{"demandes":[{"numero":N,"poste":"<32 hexa>","cle":"<id>","scelle":"<base64>","etat":"attente"\|"annulee"\|"remplacee"\|"traitee","cree":"…","maj":"…"}]}` |
| `POST /admin/demandes/traiter` | `{"numero":N}` | 200 |
| `PUT /admin/postes` | `{"poste":"<32 hexa>","etat":"<bloc poste>"}` | 200 (lu par `GET /v1/poste`) |
| `GET /admin/archive` | — | `{"version":V,"fichiers":[{"chemin":"…","sha256":"…","taille":N}]}` |
| `GET /admin/archive/fichier?chemin=…` | — | le texte du fichier (chiffré par l'éditeur avec sa clé de synchro) |
| `POST /admin/archive` | `{"base":V,"fichiers":{"<chemin>":"<texte>"\|null}}` | 200 `{"version":V+1}` ; 409 `{"version":<actuelle>}` si `base` n'est pas la version actuelle |
| `GET /admin/stats` | — | les compteurs (licences, activations et leurs `lignes`, demandes et leurs `octets`, Découvertes, postes, appels du jour) |

### Les limites (le plan gratuit, et les abus)

- Corps : 2 Mio au plus. Un fichier de l'archive : 1,5 Mio au plus.
- Devant le service (le limiteur de Cloudflare, `[[ratelimits]]` de `wrangler.toml`), par adresse IP et par minute : les routes publiques (`etat`, `heure`, `certificat`, `registre`) 60, les autres routes `/v1` 120. Un appel refusé là ne coûte rien au service.
- Par adresse IP et par heure : `activer` 10, `decouverte` 10, `demandes` 10.
- Par PC : `activer` 5 par heure, `renouveler` 30 par heure, `flottante/jeton` 60 par heure, `demandes` 5 par jour (`renouveler` et `jeton` sont comptés en mémoire : aucune écriture).
- Au-delà : 429 `trop` avec `"reessayer_dans":<secondes>`.
- Au plus 1 000 demandes en attente et 20 Mio de demandes gardées (une demande close est effacée après 7 jours), 10 000 licences, 100 000 activations (et par licence, § 4 bis).
- **Les lignes écrites** (le plan gratuit : 100 000 par jour) : une route publique n'écrit rien ; `renouveler` 3 (le nonce, le bail, le numéro ; +2 par licence de plus), `flottante/jeton` 2, `activer` 9 (un PC neuf) ; un appel refusé (signature, limite) : 0. Les appels du jour sont comptés en mémoire et écrits une fois par minute au plus. Sur 127.0.0.1 seulement, la réponse le dit : `X-XPG-Lignes-Ecrites`.

## 6. Le code d'activation

- Sa forme : `XPG-XXXX-XXXX-XXXX-XXXX`, 16 caractères en base32 de Crockford, soit 80 bits tirés au hasard par l'outil de l'éditeur.
- Il est lu avec souplesse : minuscules, espaces, `O` pour `0`, `I` ou `L` pour `1`.
- Le service n'en garde que l'**empreinte** : hex(SHA-256(`"xpg-code/1|"` + les 16 caractères normalisés)).
- L'éditeur voit le code à la création. 1.37.0 : son outil le garde **chiffré** (DPAPI, pour son compte Windows) dans `codes-activation/` pour le revoir (Licences › Voir le code…, `code-licence`) ; le service, lui, n'en a toujours que l'empreinte.
- 1.37.0 : en réponse à une demande, le code part aussi **scellé pour le PC qui l'a demandée**, dans son état « poste » (les champs `code`, `licence_en_ligne`) : son Studio l'ouvre avec sa clé et s'active seul (`POST /v1/activer`). Le service ne voit qu'un bloc scellé.

## 7. La Découverte (l'essai de 30 jours)

- Au premier lancement, le Studio commence sa Découverte sur le PC : c'est l'évaluation de la 1.34, renommée. Elle marche hors ligne.
- En ligne, il la déclare au service (`POST /v1/decouverte`) avec l'empreinte de la machine : `machine` = les 32 premiers hexadécimaux de SHA-256(`"xpg-decouverte/1|"` + l'identifiant de l'installation Windows). Le service rend le **premier début connu pour cette machine**, et le Studio retient le plus ancien des deux.
- Effacer les fichiers de la Découverte ne relance donc pas l'essai tant que le PC est en ligne. L'empreinte ne dit rien du matériel, mais elle reconnaît l'installation : c'est dit dans la fenêtre Licence.
- **Ses limites, honnêtement** : l'identifiant de l'installation Windows (`MachineGuid`) change à une réinstallation de Windows, et un administrateur du PC peut le changer ; un PC qui reste hors ligne compte seulement sur ses copies locales. Quelqu'un qui réinstalle Windows, ou change cet identifiant et efface les copies, recommence donc un essai. La Découverte freine l'essai répété ; elle ne l'empêche pas.

## 7 bis. La demande en ligne, le chemin principal (1.36.0)

Avec un service compilé (`ServiceAddress.hpp`), un client sans licence n'a ni fichier ni code à manier : il **demande sa licence**, l'éditeur l'**accepte**, elle s'installe d'elle-même.

1. **Le Studio** : l'accueil (« Demander ma licence… »), la fenêtre « Licence nécessaire » et la fenêtre Licence (onglet **Demander ma licence**, ouvert d'office sans licence valable) mènent au même formulaire : la société (obligatoire), un contact, le nom du PC, une licence existante, un mot. La demande part scellée pour la clé de l'éditeur (`POST /v1/demandes`).
2. **En attendant** : la demande reste visible (son numéro, sa date, son état « en attente de l'éditeur », puis « reçue par l'éditeur ») sur l'accueil, dans « Licence nécessaire » et dans l'onglet, avec **Annuler la demande** et **Vérifier maintenant**. Le Studio regarde lui-même la réponse (`GET /v1/poste`) **toutes les minutes pendant deux heures, puis toutes les 5 minutes** (`requestCheckDelay`) ; fermé, il regarde à son prochain lancement. Aucun projet ne s'ouvre en attendant.
3. **L'outil de l'éditeur** : son application relève les demandes **toute seule** (au lancement, puis toutes les 2 minutes, sans bruit) ; une nouvelle demande : un avis (« Nouvelle demande de licence : le client (PC « … ») — Demandes en ligne › Accepter ») et leur nombre sur **Demandes en ligne**. **Accepter (licence liée)…** signe la licence pour ce PC et la publie (`PUT /admin/postes`) ; **Refuser…** demande la raison (scellée pour ce PC). La console : `service-demandes`, `signer-licence --demande …` puis `service-publier-licence`, ou `service-refuser`.
4. **La réponse** : à la vérification suivante, la licence s'installe et **une fenêtre le confirme** : l'édition, la licence, le client, jusqu'à quand (et les jours qui restent), le nombre de PC, ce qu'elle ouvre (par logiciel), le mot de l'éditeur ; **Voir ma licence**. Refusée : la fenêtre donne la raison, et **Nouvelle demande…**.

Le poids sur le plan gratuit : une vérification fait au plus quatre appels (`/v1/etat`, le certificat s'il n'est pas déjà lu, `/v1/heure`, `/v1/poste`) ; une demande qui attend deux heures en fait environ 480, puis une cinquantaine par heure. Le formulaire et le fichier restent : **Demande par fichier (sans Internet)…** et **Fichier de licence…** (en bas de la fenêtre), pour un PC qui n'a pas Internet ; l'activation par code aussi (onglet Activation, et la page Licence de l'installateur).

### 1.37.0 : la boutique, le profil, le code livré, le mail

- **La boutique** : une édition du registre peut porter `price` (60 caractères au plus, tel que l'éditeur l'écrit) et `description` (200 au plus), écrits seulement s'ils sont remplis. Le Studio montre les éditions en cartes (prix, phrase, PC, durée, nombre de fonctions) ; celle choisie part dans la demande (`edition`, scellée). Aucun paiement en ligne. **Un registre avec un prix ou une description est refusé par les Studios 1.36 et plus anciens** (leur lecture est stricte) : installer la 1.37.0 avant d'en signer un.
- **Mon profil** (Studio, facultatif) : courriel, nom, société, dans `profil.json` du dossier des réglages (commun aux deux Studios, en clair : rien de secret). Il remplit la demande ; le courriel part dans la demande (`courriel`, scellé).
- **Pendant l'attente**, l'onglet rappelle ce que contiendra la licence de l'édition choisie (PC, durée, fonctions).
- **Une seule acceptation** : dans l'outil, une demande est « acceptée ici » dès que la licence est signée (`reponse` dans sa fiche locale) ; on ne l'accepte ni ne la refuse plus, on **renvoie la réponse** si elle n'est pas partie.
- **Le mail** : l'application de l'éditeur l'envoie à l'acceptation, par SMTP depuis sa boîte (STARTTLS ou TLS, mbedTLS ; un mot de passe d'application gardé par DPAPI), **avant** de publier la réponse, dont l'état « poste » dit alors `mail` = `envoye` ou `echec` (et `mail_a`). Pas parti : le Studio le dit (un avis), montre le code dans la fenêtre de confirmation (en mémoire seulement, jamais écrit) et demande de vérifier l'adresse (Mon profil). Pas de mail du tout (rien de réglé, pas de courriel) : la fenêtre montre aussi le code.
- **Le Worker ne change pas** : ces champs vivent dans les blocs scellés et le registre ; le service n'en lit rien.

**Une copie d'essai n'appelle jamais le vrai service** : dans une construction avec la clé d'essai (`XPG_LICENCES_CLE_ESSAI`), l'adresse compilée est ignorée ; seule la variable `XPG_SERVICE` (le service des essais, sur ce poste) en donne une.

## 8. Ce qui reste sur le PC (le dossier des licences)

| Fichier | Contenu |
|---|---|
| `licence.xpglic` | la licence : un PC lié par l'éditeur (comme avant), ou une licence en ligne (sans PC) |
| `activation.xpgact` | le bloc `activation` de ce PC, et le bloc `service` (le certificat) |
| `jeton.xpgjet` | le jeton d'une licence flottante (15 min) |
| `decouverte.xpgdec` | le bloc `decouverte` |
| `code-activation.txt` | un code laissé par l'installateur, en attente du premier lancement en ligne (effacé après) |
| `etat.bin` | aussi le compteur du bail (§ 4) |
