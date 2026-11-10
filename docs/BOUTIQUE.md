# La boutique et les commandes (1.46.0) : le contrat

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
  local (variable `STRIPE_API_BASE`, que le Worker n'écoute que pour une adresse `127.0.0.1` ou `localhost`, comme
  `XPG_DECALAGE_HORLOGE_ESSAI`).

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

`GET /admin/boutique` (admin+, admin) : le même JSON, plus `"stripe": {"branche": bool, "mode": "test"|"reel"|null,
"webhook": bool, "dernier_evenement": T|null}` (jamais la clé).

`GET /v1/boutique` (public) : `{"active": bool, "devise": "eur", "mode": "test"|"reel"|null, "articles": [{"id",
"genre", "edition"|"option", "jours"?, "montant", "libelle"}]}` - seulement si Stripe est branché ; sinon `"active":
false` et pas d'articles.

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
| `POST /v1/boutique/commande` | `{"article":"<id>","societe","nom","courriel","licence_existante"?,"cle":"<id clé éditeur>","scelle":"<base64>","scelles"?:[…]}` | 201 `{"numero":N,"url":"<page de paiement Stripe>","expire":T,"montant":49000,"devise":"eur","mode":"test"\|"reel"}`. 409 `boutique` (fermée, Stripe pas branché), 404 `article`, 400 `champ` (courriel invalide, société vide), 409 `en_cours` (ce PC a déjà une commande `a_payer` de moins de 30 minutes : sa `url` est rendue avec), 502 `stripe` (Stripe refuse ou ne répond pas : le texte de Stripe, sans secret). |
| `GET /v1/boutique/commandes` | — | 200 `{"commandes":[{"numero","article","libelle","montant","devise","etat","cree","maj","licence"?,"recu_url"?,"url"?(a_payer)}]}` : celles de CE PC (50 au plus, les plus récentes d'abord). |
| `POST /v1/boutique/annuler` | `{"numero":N}` | 200 : une commande `a_payer` de ce PC passe `annulee` (la session Stripe est expirée chez Stripe). 409 `etat` sinon. |

Le Studio ne connaît pas le résultat du paiement par le navigateur : il regarde `GET /v1/boutique/commandes` toutes les
10 secondes pendant 30 minutes après un achat, puis toutes les 5 minutes tant qu'une commande est `payee`. La licence
livrée arrive comme la réponse à une demande (le code scellé dans l'état « poste », `GET /v1/poste`).

**La page de paiement** (`POST https://api.stripe.com/v1/checkout/sessions`, en `application/x-www-form-urlencoded`,
l'en-tête `Authorization: Bearer <STRIPE_SECRET_KEY>` et `Idempotency-Key: commande-<numero>`) : `mode=payment`,
`line_items[0][price_data][currency]`, `[unit_amount]`, `[product_data][name]` (le libellé), `[quantity]=1`,
`payment_method_types[0]=card`, `customer_email`, `client_reference_id=<numero>`, `metadata[commande]=<numero>`,
`invoice_creation[enabled]=true` (une facture envoyée par Stripe), `locale=fr`, `expires_at` (+30 min, le minimum de
Stripe), `success_url` et `cancel_url` (deux petites pages du service : `GET /v1/boutique/merci?c=N` « Paiement reçu :
retourne dans ton Studio, ta licence arrive » et `GET /v1/boutique/annule?c=N`). **Jamais** `customer`,
`customer_creation=always`, `setup_future_usage`, `saved_payment_method_options`, ni `subscription`.

### Le webhook de Stripe

`POST /v1/boutique/stripe` (public, sans signature XPG ; signé par Stripe) : l'en-tête `Stripe-Signature`
(`t=<unix>,v1=<hexa>[,v1=…]`) ; `v1` = HMAC-SHA256(`STRIPE_WEBHOOK_SECRET`, `<t>.<corps brut>`), comparé en temps
constant ; refusé (400) si aucune signature ne va, ou si `t` a plus de 5 minutes d'écart. Chaque événement n'est traité
qu'une fois (son `id` gardé 30 jours). Le corps : 256 Kio au plus.
- `checkout.session.completed` avec `payment_status` = `paid`, ou `checkout.session.async_payment_succeeded` →
  `payee` (le montant payé doit être celui de la commande, sinon `a_revoir`) ; `stripe_paiement`, `recu_url` si présent.
- `checkout.session.async_payment_failed` → `echec_paiement` ; `checkout.session.expired` → `expiree` (si `a_payer`).
- `charge.refunded` (remboursée en entier) → `remboursee` (la licence livrée n'est pas révoquée d'office : l'Admin le
  signale, l'éditeur décide).
La réponse est toujours 200 pour un événement bien signé (même inconnu), pour que Stripe ne le renvoie pas.

### L'éditeur et les admins (signées)

| Route | Qui | Corps | Réponse |
|---|---|---|---|
| `GET /admin/commandes?depuis=<maj>` | admin+, admin (droit `demandes`) | — | `{"commandes":[{…toute la commande, avec le scellé de la clé qui appelle…}],"numero":N}` |
| `POST /admin/commandes/livree` | idem | `{"numero":N,"licence":"<id>"}` | 200 : `payee` → `livree` (409 `etat` sinon) |
| `POST /admin/commandes/a-revoir` | idem | `{"numero":N,"erreur":"<texte>"}` | 200 |
| `POST /admin/commandes/rembourser` | admin+ | `{"numero":N}` | 200 : le service demande le remboursement à Stripe (`POST /v1/refunds`, `payment_intent`) ; la commande passe `remboursee` au webhook |

`GET /admin/etat` gagne `commandes` (un compteur qui monte à chaque changement d'une commande) et `a_livrer` (les
commandes `payee`) ; `GET /v1/etat` gagne `boutique` (`true` si des articles sont achetables). La version du service
passe à **1.46.0** ; l'Admin 1.46 demande un service 1.46.0 pour la boutique (sinon : « déploie la 1.46.0 pour la
boutique »), et marche avec un service 1.44 pour tout le reste.

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
- puis `POST /admin/commandes/livree`. En cas d'échec (scellé illisible, licence existante introuvable, droit
  manquant) : `a-revoir` avec la raison, une notification dans l'Admin.
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
  courriel, le PC, la licence livrée, les identifiants Stripe, le reçu), **Délivrer maintenant** (payée), **Voir dans
  Stripe**, **Rembourser…** (admin+, avec une question), **Revoir** (a_revoir). Le tableau de bord montre « à livrer ».

## 7. Le Studio

- Les cartes des offres (Demander ma licence) : une offre en vente en ligne a **Acheter — 490,00 € TTC** (et toujours
  « Demander », sans paiement) ; une option : **Acheter** pour chacune de ses durées en vente.
- Acheter : un récapitulatif (l'offre, ce qu'elle donne, le montant TTC, la société et le courriel du profil, « Tu
  paies sur la page sécurisée de Stripe : ta carte n'est ni vue ni gardée par XPG ; elle est redemandée à chaque
  achat. »), puis la page de paiement dans le navigateur. Une fenêtre « Commande n° N » suit l'état (en attente du
  paiement, Rouvrir la page de paiement, Annuler la commande ; payée ; livrée).
- **Mes achats** : les commandes de ce PC (date, article, montant, état, le reçu Stripe).
- Le mode test se voit (« Paiement d'essai : aucune carte réelle n'est débitée »).
