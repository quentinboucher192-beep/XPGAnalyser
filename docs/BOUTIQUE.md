# La boutique et les commandes (1.46.0) : le contrat

> **Construit en 1.46.0** (le service, l'Admin, ses essais). Ce qui a été précisé en construisant, par rapport à la
> première version de ce contrat, est dit au § 8 ; le texte ci-dessous en tient déjà compte.

Demandé par l'éditeur : « fait la partie boutique et commandes » ; « pour les achats ne jamais enregistrer les cartes
des clients, les redemander à chaque achat ». Ses choix : **Stripe** (la page de paiement hébergée par Stripe) et
**la licence part seule quand son Admin est ouvert** (l'Admin signe les commandes payées ; sa clé ne quitte pas son PC).

Ce document est le contrat entre le service des licences (`service/`), l'Admin (`src/license_admin/`) et les Studios
(`src/app/`). Il complète [SERVICE-LICENCES.md](SERVICE-LICENCES.md) (les appels signés, les blocs, le poste) et
[LICENCES.md](LICENCES.md) (le registre, les éditions, les options).

## 1. Le principe

1. **Le Studio** (Licence › Demander ma licence, ou une option) : le client choisit une offre (ou une option et sa
   durée) en vente en ligne, son profil remplit la société, le nom, le courriel ; **Acheter — 490,00 € TTC** crée une
   commande sur le service, qui demande à Stripe une **page de paiement** (Checkout) et rend son adresse. Le Studio
   l'ouvre dans le navigateur.
2. **Le client paie sur la page de Stripe** : il tape sa carte (ou Apple Pay, Google Pay). **Ni le Studio, ni le
   service, ni l'Admin ne voient la carte.** Rien n'est enregistré pour un achat suivant : le service ne crée jamais de
   « client » Stripe réutilisable, ne demande jamais `setup_future_usage`, et limite les moyens à `card` (sans Link, qui
   propose d'enregistrer la carte). La carte est redemandée à chaque achat.
3. **Stripe prévient le service** (le webhook, signé par Stripe) : la commande passe **payée**.
4. **L'Admin de l'éditeur**, dès qu'il est ouvert (ou à sa prochaine ouverture), voit la commande payée et, si
   « Livrer seul les commandes payées » est coché (par défaut), **signe la licence** (comme une demande acceptée : la
   demande scellée de la commande dit le PC) : une licence en ligne de l'offre achetée, déposée sur le service, son code
   scellé pour le PC (son état « poste ») ; pour une option : la licence du client émise de nouveau avec l'option. La
   commande passe **livrée** (avec l'identifiant de la licence). Sinon, elle attend un clic « Délivrer » dans Commandes.
5. **Le Studio** regarde sa commande : payée → « Paiement reçu : ta licence arrive dès que l'éditeur la délivre » ;
   livrée → il s'active seul (le code scellé, comme la réponse à une demande) et une fenêtre le confirme.

## 2. Les secrets de Stripe (jamais ailleurs que dans le service)

- `STRIPE_SECRET_KEY` (clé secrète `sk_live_…`, `sk_test_…` ou restreinte `rk_…`) et `STRIPE_WEBHOOK_SECRET`
  (`whsec_…`) sont des **secrets du Worker** (`wrangler secret put`). Ils ne sont jamais écrits dans un fichier de
  l'Admin, ni dans un journal, ni dans une sauvegarde de secours, ni renvoyés par une route.
- **Brancher Stripe…** (Admin › Boutique) : l'éditeur colle sa clé secrète (champ masqué, jamais gardé) ; l'Admin crée
  le point d'arrivée du webhook chez Stripe (`POST https://api.stripe.com/v1/webhook_endpoints`, `url` = l'adresse du
  service + `/v1/boutique/stripe`, `enabled_events` : `checkout.session.completed`,
  `checkout.session.async_payment_succeeded`, `checkout.session.async_payment_failed`, `checkout.session.expired`,
  `charge.refunded`), reçoit son secret `whsec_…`, puis passe les deux au Worker par `wrangler secret put` (l'entrée
  standard, le kit du service installé avec l'Admin, comme Déployer). Le mode (test ou réel) se lit du préfixe de la
  clé et se montre partout (« Stripe en mode test »).
- **Les essais** n'utilisent jamais de vraie clé : des valeurs qui commencent par `essai_jeton_`, et un faux Stripe
  local (`tests/outils/faux_stripe.mjs` ; la variable `STRIPE_API_BASE`, que le Worker n'écoute que pour un appel à
  `127.0.0.1` ou `localhost` et seulement vers `http://127.0.0.1:PORT`, comme `DECALAGE_HORLOGE_ESSAI` ;
  `outils/service/lancer-local.sh` la passe avec `XPG_STRIPE_SECRET_KEY` et `XPG_STRIPE_WEBHOOK_SECRET`, qui doivent
  commencer par `essai_jeton_`). Côté Admin, l'adresse de l'API de Stripe ne se remplace que par une adresse de ce poste
  (`XPG_STRIPE_API_ESSAI`, ou `shop::setStripeApiBaseForTests`).
- **Le mode** : `sk_live_…` / `rk_live_…` → `"reel"` ; toute autre clé (`sk_test_…`, `rk_test_…`, `essai_jeton_…`) →
  `"test"`. Une clé restreinte (`rk_…`) doit pouvoir écrire les Webhook Endpoints, les Checkout Sessions et les Refunds,
  et lire les Invoices.
- **Brancher de nouveau** (une autre clé, un autre compte) : l'Admin efface d'abord chez Stripe les points d'arrivée qui
  ont la même adresse (`GET` puis `DELETE /v1/webhook_endpoints/<id>`), sinon chaque événement arriverait deux fois, l'un
  signé par un secret que le service n'a plus.

## 3. Le catalogue de la boutique (sur le service, pas dans le registre)

Le registre signé garde le **prix affiché** (`price`, un texte) ; le **montant encaissé** est dans le catalogue de la
boutique, que l'éditeur règle dans l'Admin et que le service garde (le registre n'a pas de nouveau champ : les Studios
1.45 et avant le lisent toujours).

`PUT /admin/boutique` (admin+) :
```json
{"active": true, "livraison_auto": true, "devise": "eur",
 "articles": [
   {"id": "ed:PROFESSIONAL", "genre": "edition", "edition": "PROFESSIONAL", "montant": 49000, "libelle": "Licence PROFESSIONAL - 1 an"},
   {"id": "opt:ai_advanced:365", "genre": "option", "option": "ai_advanced", "jours": 365, "montant": 9900, "libelle": "Option Assistant IA avancé - 1 an"}
 ]}
```
`montant` en centimes, TTC (ce que la carte paie) ; 50 centimes au moins, 1 000 000 € au plus ; 200 articles au plus.
Une offre ou une option du registre absente du catalogue (ou `active: false`) n'est pas achetable en ligne : le
Studio garde « Demander » (la demande sans paiement). Réponse 200 `{"numero":N}` (monte à chaque changement).

`id` vaut `ed:<EDITION>` ou `opt:<option>:<jours>` (sinon 400 `champ`) ; `jours` de 0 (jusqu'à la fin de la licence) à
3660 ; `libelle` une ligne de 1 à 120 caractères ; `devise` 3 lettres minuscules (`eur` par défaut) ; `livraison_auto`
vrai par défaut. Le service ne relit pas le registre : l'Admin ne propose que ses offres et ses options.

`GET /admin/boutique` (admin+, admin, sans droit) : le même JSON (avec `numero`), plus `"stripe": {"branche": bool,
"mode": "test"|"reel"|null, "webhook": bool, "dernier_evenement": T|null}` (jamais la clé) et `"ouverte": bool`.

**La boutique est ouverte** quand le réglage est `active`, que les DEUX secrets de Stripe sont posés (la clé et le secret
du webhook) et qu'il y a au moins un article.

`GET /v1/boutique` (public) : `{"active": bool, "devise": "eur", "mode": "test"|"reel"|null, "articles": [{"id",
"genre", "edition"|"option", "jours"?, "montant", "libelle"}]}` - ouverte seulement ; fermée : `"active": false` et
`"articles": []`.

**Les dates** de la boutique (`cree`, `maj`, `expire`, `paye`, `prise`, `dernier_evenement`) sont des **secondes Unix**,
comme les routes de la 1.44.

## 4. Les commandes

Une commande : `numero` (entier croissant), `poste` (le `deviceId` de la clé du PC qui commande), `cle` (sa clé
publique), `article` (l'id), son `genre`, `edition` ou `option`+`jours`, `montant`, `devise`, `societe`, `nom`,
`courriel`, `licence_existante` (pour une option : l'identifiant de la licence du client), `scelle` (la demande du PC,
scellée pour la clé de l'éditeur et des admins qui ont le droit `demandes`, comme `POST /v1/demandes`), `etat`,
`cree`, `maj`, `stripe_session`, `stripe_paiement` (l'id du PaymentIntent), `recu_url` (le reçu Stripe, si Stripe le
donne), `licence` (l'identifiant livré), `erreur` (le texte d'un échec de livraison).

**Les états** : `a_payer` (créée, la page de paiement ouverte) → `payee` (le webhook) → `livree` (l'Admin) ; ou
`expiree` (la page de paiement a expiré, 30 minutes après la création, ou abandonnée), `annulee` (par le client avant
de payer), `echec_paiement` (un paiement différé refusé), `remboursee` (Stripe : remboursée en entier), `a_revoir`
(livraison impossible : l'Admin dit pourquoi, l'éditeur décide).

### Le PC (signées par la clé du PC, comme les autres routes `/v1/…`)

| Route | Corps | Réponse |
|---|---|---|
| `POST /v1/boutique/commande` | `{"article":"<id>","societe","nom"?,"courriel","licence_existante"?,"cle":"<id clé éditeur>","scelle":"<base64>","scelles"?:[{"cle","scelle"}]}` | 201 `{"numero":N,"url":"<page de paiement Stripe>","expire":T,"montant":49000,"devise":"eur","mode":"test"\|"reel"}`. 409 `boutique` (fermée : pas de réglage actif, Stripe pas branché, aucun article), 404 `article`, 400 `champ` (courriel illisible, société vide, une option sans `licence_existante`), 400 `cle` (une clé de scellement inconnue), 409 `en_cours` (ce PC a déjà une commande `a_payer` dont la page n'a pas expiré : `numero`, `url`, `expire` rendus avec), 502 `stripe` (Stripe refuse ou ne répond pas : le texte de Stripe, sans secret ; la commande n'est pas gardée), 507 `limite`. |
| `GET /v1/boutique/commandes` | — | 200 `{"commandes":[{"numero","article","libelle","montant","devise","etat","mode","cree","maj","licence"?,"recu_url"?,"url"?,"expire"?}]}` : celles de CE PC (50 au plus, les plus récentes d'abord) ; `url` et `expire` pour une commande `a_payer`. |
| `POST /v1/boutique/annuler` | `{"numero":N}` | 200 : une commande `a_payer` de ce PC passe `annulee` (la session est expirée chez Stripe ; si Stripe refuse — déjà expirée ou payée —, annulée quand même : un paiement fait juste avant arrive par le webhook et la fait passer `payee`) ; déjà `annulee` : 200. 404 `commande` (pas de ce PC), 409 `etat` sinon, 502 `stripe` (Stripe ne répond pas : rien n'est changé). |

Le Studio ne connaît pas le résultat du paiement par le navigateur : il regarde `GET /v1/boutique/commandes` toutes les
10 secondes pendant 30 minutes après un achat, puis toutes les 5 minutes tant qu'une commande est `payee`. La licence
livrée arrive comme la réponse à une demande (le code scellé dans l'état « poste », `GET /v1/poste`).

**La page de paiement** (`POST https://api.stripe.com/v1/checkout/sessions`, en `application/x-www-form-urlencoded`,
l'en-tête `Authorization: Bearer <STRIPE_SECRET_KEY>` et `Idempotency-Key: commande-<numero>`) : `mode=payment`,
`line_items[0][price_data][currency]`, `[unit_amount]`, `[product_data][name]` (le libellé), `[quantity]=1`,
`payment_method_types[0]=card`, `customer_email`, `client_reference_id=<numero>`, `metadata[commande]=<numero>`,
`invoice_creation[enabled]=true` (une facture envoyée par Stripe), `locale=fr`, `expires_at` (+31 min : les 30 minutes
minimum de Stripe et une de marge), `success_url` et `cancel_url` (deux petites pages du service : `GET /v1/boutique/merci?c=N` « Paiement reçu :
retourne dans ton Studio, ta licence arrive » et `GET /v1/boutique/annule?c=N`). **Jamais** `customer`,
`customer_creation=always`, `setup_future_usage`, `saved_payment_method_options`, ni `subscription`. Une page finie
depuis 5 minutes sans nouvelle de Stripe : le service passe la commande `expiree` lui-même (son ménage, toutes les 5
minutes).

### Le webhook de Stripe

`POST /v1/boutique/stripe` (public, sans signature XPG ; signé par Stripe) : l'en-tête `Stripe-Signature`
(`t=<unix>,v1=<hexa>[,v1=…]`) ; `v1` = HMAC-SHA256(`STRIPE_WEBHOOK_SECRET`, `<t>.<corps brut>`), comparé en temps
constant ; refusé (400) si aucune signature ne va, ou si `t` a plus de 5 minutes d'écart. Chaque événement n'est traité
qu'une fois (son `id` gardé 30 jours). Le corps : 256 Kio au plus.
- `checkout.session.completed` avec `payment_status` = `paid`, ou `checkout.session.async_payment_succeeded` →
  `payee` (le montant ET la devise payés doivent être ceux de la commande, sinon `a_revoir` avec la raison) ;
  `stripe_paiement` (le `payment_intent`) ; `recu_url` = la facture de Stripe (`hosted_invoice_url`, lue par
  `GET /v1/invoices/<id>` au paiement ; absente si elle ne se lit pas). La commande est retrouvée par
  `metadata[commande]` (ou `client_reference_id`) ET l'identifiant de la session. Une commande `expiree`, `annulee` ou
  `echec_paiement` payée quand même passe `payee`.
- `checkout.session.async_payment_failed` → `echec_paiement` ; `checkout.session.expired` → `expiree` (si `a_payer`).
- `charge.refunded` (remboursée en entier : `refunded` vrai, ou `amount_refunded` ≥ `amount`) → `remboursee`, retrouvée
  par son `payment_intent` (la licence livrée n'est pas révoquée d'office : l'Admin le signale, l'éditeur décide). Un
  remboursement partiel ne change rien.
La réponse est toujours 200 pour un événement bien signé (même inconnu), pour que Stripe ne le renvoie pas.

### L'éditeur et les admins (signées)

| Route | Qui | Corps | Réponse |
|---|---|---|---|
| `GET /admin/commandes?depuis=<maj>` | admin+, admin (droit `demandes`) | — | `{"commandes":[…],"suite":bool,"numero":N,"a_livrer":n}` : les commandes dont `maj` > `depuis` (toutes sans lui), dans l'ordre de `maj`, 500 au plus (`suite` : il y en a d'autres). Chaque commande : `numero`, `poste`, `cle_pc` (la clé publique du PC, 64 hexa), `article`, `genre`, `edition` ou `option`+`jours`+`licence_existante`, `montant`, `devise`, `libelle`, `societe`, `nom`, `courriel`, `etat`, `mode`, `cree`, `maj`, `expire`, `paye`, `stripe_session`, `stripe_paiement`, `stripe_remboursement`, `recu_url`, `licence`, `erreur`, `prise` (l'heure où un Admin l'a prise, s'il y a moins de 5 minutes ; sinon null), `cle` (l'identifiant de la clé pour qui le scellé rendu est fait) et `scelle` — comme `/admin/demandes` : l'admin+ reçoit celui de l'éditeur ; un admin le sien, sinon `"scelle":null,"pour_un_autre":true`. `numero` : le compteur des changements ; `a_livrer` : les `payee`. |
| `POST /admin/commandes/prendre` | idem | `{"numero":N}` | 200 `{"numero","prise","etat"}` si `payee` (ou `a_revoir` : « Délivrer maintenant » après revue) et pas prise par un AUTRE Admin depuis moins de 5 minutes ; 409 `prise` (un autre Admin la livre), 409 `etat` (`{"etat","licence"}`) sinon |
| `POST /admin/commandes/livree` | idem | `{"numero":N,"licence":"<id>"}` | 200 : `payee` ou `a_revoir` → `livree` ; 409 `etat` (avec `{"etat","licence"}` : une seconde livraison est refusée), 409 `prise` (prise par un autre Admin) |
| `POST /admin/commandes/a-revoir` | idem | `{"numero":N,"erreur":"<texte, 500 au plus>"}` | 200 : `payee` ou `a_revoir` → `a_revoir` (la prise est levée) ; 409 `etat` sinon |
| `POST /admin/commandes/rembourser` | admin+ | `{"numero":N}` | 200 `{"numero","remboursement":"re_…","etat"}` : le service demande le remboursement à Stripe (`POST /v1/refunds`, `payment_intent`, `Idempotency-Key: remboursement-<numero>`) pour une commande `payee`, `livree` ou `a_revoir` ; elle passe `remboursee` au webhook. 409 `etat` sinon, 502 `stripe`. |

`GET /admin/etat` gagne `commandes` (un compteur qui monte à chaque changement d'une commande) et `a_livrer` (les
commandes `payee`) ; `GET /v1/etat` gagne `boutique` (`true` si la boutique est ouverte). La version du service
passe à **1.46.0** ; l'Admin 1.46 demande un service 1.46.0 pour la boutique (sinon : « Ton service en ligne est en
1.44.0 : déploie la 1.46.0 pour la boutique », avec Déployer le service), et marche avec un service 1.44 pour tout le
reste (`deployer::kServiceRequired` reste 1.44.0 ; `shop::kServiceRequired` = 1.46.0).

**Les limites** : `POST /v1/boutique/commande` 10 par heure et par adresse, 10 par jour et par PC ; `GET
/v1/boutique/commandes` 400 par heure et par PC (en mémoire : aucune écriture) ; `/v1/boutique`, `/merci`, `/annule`
sous le limiteur des routes publiques (60 par minute et par adresse), le webhook sous celui des PC (120). 100 000
commandes, 1 000 `a_payer` à la fois, 50 Mio de scellés. **Le ménage** (une fois par jour) : une commande `expiree`,
`annulee` ou `echec_paiement` est effacée 30 jours après son dernier changement ; le scellé d'une commande `livree` ou
`remboursee` est effacé après 7 jours (la commande reste) ; un événement de Stripe est oublié après 30 jours.

**Les tables** (schéma 4 du Durable Object) : `boutique` (le réglage, le dernier événement), `commandes`,
`commandes_scelles`, `stripe_evenements` — toutes dans la sauvegarde de secours (`GET /admin/sauvegarde`), jamais les
secrets de Stripe (des secrets du Worker, dans aucune table). Un service 1.46 restaure une sauvegarde du schéma 3 (une
1.44 : ses tables de la boutique restent vides) ; une sauvegarde du schéma 4 dans un service 1.44 : 409 `schema`.

## 5. La livraison par l'Admin

L'Admin ouvert regarde `GET /admin/etat` (toutes les 15 secondes, comme aujourd'hui) ; `commandes` a bougé : il relit
les commandes. Pour chaque commande `payee`, si la livraison automatique est cochée (et que cet Admin peut signer :
l'admin+, ou un admin avec les droits `demandes` et `licence_creer` ; `licence_option` pour une option) :
- il ouvre le scellé (sa clé), vérifie qu'il est du PC de la commande ;
- **une offre** : une licence en ligne de l'offre (ses PC, sa durée, ses fonctionnalités, comme le registre les dit),
  le client = la société, un identifiant neuf (`XPG-AAAA-NNNNN`), le bail habituel ; déposée (`PUT /admin/licences`),
  le code scellé pour le PC (son état « poste »), une ligne au registre des activations, à l'archive, au journal des
  admins (« commande n° N livrée », Moyenne) ;
- **une option** : la licence existante du client (vérifiée : elle est bien liée à ce PC) émise de nouveau avec
  l'option pour sa durée (comme une demande d'option acceptée en 1.42) ;
- puis `POST /admin/commandes/livree`. En cas d'échec (scellé illisible, licence existante introuvable ou pas liée à ce
  PC, une offre ou une option qui n'est plus au registre) : `a-revoir` avec la raison, une notification dans l'Admin.
  Une panne qui passera seule (le service ou le réseau) ne met pas la commande à revoir : elle reste `payee`, l'Admin
  reprend toutes les 5 minutes. **Une reprise ne signe jamais une seconde licence** : les livraisons commencées sont
  notées sur le poste (`boutique-livraisons.json` : la commande → la licence signée pour elle ; son code est gardé
  chiffré dès la signature) et la même licence est déposée de nouveau.
- **La réponse au PC** (son état « poste », comme pour une demande acceptée) : `acceptee`, `requestNonce` = le nonce de
  la demande d'activation scellée avec la commande, le contenu scellé pour le PC : `onlineLicense` (la licence),
  `code` (une offre : le code d'activation ; le mail de l'éditeur s'il est réglé, comme une demande acceptée), et un
  mot (`message`). Une option sur une licence en ligne : `acceptee` sans code (le Studio renouvelle et reçoit la
  nouvelle émission) ; sur une licence liée à des PC : la nouvelle émission part à chacun de ses PC (`mise_a_jour`).
- Le journal des admins : `commande_livrer` (« commande n° N livrée (licence X, société) », Moyenne), et aussi
  `commande_revoir` (Haute), `commande_rembourser` (Haute), `boutique` (le réglage, Haute) et `stripe_brancher`
  (Critique) — de nouvelles actions que le service 1.46 accepte.
Une commande n'est livrée qu'une fois, même si deux Admin sont ouverts (le service refuse la seconde `livree` ; l'Admin
qui perd retire ce qu'il a déposé ou ne dépose qu'après avoir « pris » la commande : `POST /admin/commandes/prendre`
`{"numero":N}` → 200 si `payee` et non prise depuis 5 minutes, 409 sinon).

## 6. Les pages de l'Admin

- **Boutique** (Vente en ligne) : l'état de Stripe (branché, mode test ou réel, le webhook reçu pour la dernière fois
  le…), **Brancher Stripe…**, **Ouvrir mon tableau de bord Stripe** ; « Vendre en ligne » (marche/arrêt) ; « Livrer
  seul les commandes payées » ; le **catalogue** : chaque offre du registre (en vente, son prix affiché, le montant
  encaissé en € TTC, le libellé de la facture), chaque option et chacune de ses durées ; un écart entre le prix affiché
  et le montant est signalé. Enregistrer → `PUT /admin/boutique`.
- **Commandes** : le tableau (n°, date, client, société, article, montant, état ; filtres par état), le détail (le
  courriel, le PC, la licence livrée, les identifiants Stripe, le reçu), **Délivrer maintenant** (payée, ou à revoir
  après revue), **Voir dans Stripe**, **Ouvrir le reçu**, **Revoir…** (une payée : la mettre à revoir, avec la raison),
  **Rembourser…** (admin+, avec une question qui rappelle que la licence n'est pas révoquée d'office). Le tableau de
  bord montre « N commandes payées à livrer » ; le menu de gauche compte les commandes à livrer (Commandes).
- **La console** (`XPG-License-Admin`) : `boutique-etat`, `boutique-config --parametres F.json` (le JSON de `PUT
  /admin/boutique`), `stripe-brancher --cle-variable NOM [--dossier-kit D]` (la clé lue dans une variable
  d'environnement, jamais sur la ligne de commande ni affichée), `commandes [--etat E]`, `commande-livrer [--numero N]`,
  `commande-revoir --numero N --raison T`, `commande-rembourser --numero N`.

## 7. Le Studio

- Les cartes des offres (Demander ma licence) : une offre en vente en ligne a **Acheter — 490,00 € TTC** (et toujours
  « Demander », sans paiement) ; une option : **Acheter** pour chacune de ses durées en vente.
- Acheter : un récapitulatif (l'offre, ce qu'elle donne, le montant TTC, la société et le courriel du profil, « Tu
  paies sur la page sécurisée de Stripe : ta carte n'est ni vue ni gardée par XPG ; elle est redemandée à chaque
  achat. »), puis la page de paiement dans le navigateur. Une fenêtre « Commande n° N » suit l'état (en attente du
  paiement, Rouvrir la page de paiement, Annuler la commande ; payée ; livrée).
- **Mes achats** : les commandes de ce PC (date, article, montant, état, le reçu Stripe).
- Le mode test se voit (« Paiement d'essai : aucune carte réelle n'est débitée »).

## 8. Ce qui a été précisé en construisant (1.46.0)

Pour qui construit le Studio d'après ce contrat (la première version de ce document disait moins, ou autrement) :

1. **Les dates** de la boutique sont des secondes Unix (`cree`, `maj`, `expire`, `paye`, `prise`, `dernier_evenement`).
2. **`GET /admin/commandes`** : `cle` est l'identifiant de la clé pour qui le scellé rendu est fait (comme
   `/admin/demandes`), la clé publique du PC s'appelle **`cle_pc`** ; la réponse porte aussi `a_livrer` et `suite` ;
   `depuis` compare `maj`. Un admin sans scellé pour lui : `"scelle":null,"pour_un_autre":true`.
3. **`prendre` et `livree`** acceptent aussi une commande `a_revoir` (« Délivrer maintenant » après revue) ; `livree`
   refuse (409 `prise`) une commande prise par un AUTRE Admin depuis moins de 5 minutes ; les 409 `etat` rendent
   `etat` et `licence`. **`a-revoir`** : depuis `payee` ou `a_revoir`, la prise levée.
4. **La boutique ouverte** demande aussi le secret du webhook (pas seulement la clé) et un article ; `GET
   /admin/boutique` dit `ouverte`. Fermée, `GET /v1/boutique` rend `"articles": []` (le tableau est toujours là).
5. **`expires_at`** = +31 minutes (une minute de marge sur le minimum de Stripe) ; une page finie depuis 5 minutes
   sans événement de Stripe : `expiree` par le service lui-même.
6. **Le reçu** (`recu_url`) est la facture hébergée de Stripe (`hosted_invoice_url`, lue au paiement).
7. **Une commande expirée, annulée ou refusée puis payée quand même** (le client a payé juste avant) passe `payee`.
   Le montant **et la devise** payés sont vérifiés.
8. **Annuler** : Stripe refuse d'expirer la page (déjà expirée, payée) → annulée quand même ; Stripe ne répond pas →
   502 `stripe`, rien n'est changé.
9. **409 `en_cours`** rend `numero`, `url` et `expire` ; une commande refusée par Stripe (502) n'est pas gardée.
10. **Les limites, le ménage, les tables, le schéma 4** : § 4 (fin). La sauvegarde de secours porte les tables de la
    boutique ; un service 1.46 restaure une sauvegarde 1.44 (schéma 3).
11. **La réponse au PC** d'une commande livrée (§ 5) : l'état « poste » `acceptee`, `requestNonce` = le nonce de la
    demande scellée avec la commande, `onlineLicense`, `code` (une offre ; vide pour une option) et un mot.
12. **Le contenu scellé** d'une commande est exactement celui d'une demande en ligne (`gateway::OnlineRequest` : la
    demande d'activation `.xpgreq` signée par le PC, `customer`, `email`, `contact`, et `edition`, ou `option` et
    `option_jours`) ; le service ne le lit pas, l'Admin vérifie que la clé du PC qui l'a signé est `cle_pc` et que son
    `deviceId` est `poste`. Le client de la licence livrée est la `societe` de la commande.
13. **Le journal des admins** gagne `boutique`, `stripe_brancher`, `commande_livrer`, `commande_revoir`,
    `commande_rembourser`.
14. **Brancher de nouveau** efface d'abord les anciens points d'arrivée de la même adresse chez Stripe ; le mode se
    lit `reel` pour `sk_live_`/`rk_live_`, `test` sinon.
15. **Les limites** : la commande 10 par heure et par adresse, 10 par jour et par PC ; la liste des commandes d'un PC
    400 par heure (le Studio la regarde toutes les 10 s pendant 30 minutes : 180 appels).
