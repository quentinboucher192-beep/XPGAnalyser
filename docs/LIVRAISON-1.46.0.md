# API Studio, IHM Studio et l'Admin 1.46.0 : la boutique et les commandes (Stripe), la fenêtre Licence avec un fil d'Ariane, Mon profil à part, les images en grand, les barres de défilement

Faite à partir de tes retours sur la 1.45 : « le bouton annuler chevauche, je ne peux pas écrire dans Notes », « ces
scrollbars ne bougent pas avec la souris », « quand on est dans les fonctionnalités zoomées on peut quand même changer
de fonctionnalités et aussi on peut voir les captures de celles barrées », « fais la partie boutique et commandes »,
« dans la partie licence fais plutôt une bande d'Ariane pour les onglets, ça sera mieux et moins un paquet empilé
incompréhensible », « mon profil, mets-le ailleurs », « pour les achats ne jamais enregistrer les cartes des clients,
les redemander à chaque achat » ; et de tes deux choix : **Stripe** pour le paiement, la licence livrée **seule, par
ton Admin ouvert**.

Elle se met à jour par-dessus la 1.45.0 (les Studios et l'Admin) et garde tes données. **Le service en ligne passe en
1.46.0** (la boutique) : l'Admin te le dit et le redéploie d'un bouton (§ 1). Tant qu'il est en 1.44, tout marche comme
avant, sans boutique.

## 0. En bref

**Dans les Studios : acheter en ligne**

- **Acheter** : dans Licence › Demander ma licence, une offre en vente en ligne a **« Acheter — 468,00 € TTC »** (et
  toujours « ou la demander sans paiement ») ; dans Options, **Acheter** pour la durée choisie. Le récapitulatif dit
  l'offre, ce qu'elle donne (« 5 PC · 1 an · 18/21 fonctionnalités »), le montant TTC, la société, le nom et le
  courriel du profil (« Modifier mon profil… » à côté ; sans société ni courriel, Payer est grisé et le dit).
- **Payer** : la **page sécurisée de Stripe** s'ouvre dans le navigateur. **La carte n'est ni vue ni gardée** par le
  Studio, le service ou l'Admin, et **elle est redemandée à chaque achat** : le service ne crée jamais de « client »
  Stripe, ne demande jamais à Stripe de garder la carte, et n'accepte que la carte (sans Link, qui propose de
  l'enregistrer). En mode test, une bande le dit partout : « Paiement d'essai : aucune carte réelle n'est débitée ».
- **La commande** : une fenêtre « Commande n° N » la suit : à payer (Rouvrir la page de paiement, Annuler la commande),
  payée (« Paiement reçu : ta licence arrive dès que l'éditeur la délivre »), **livrée** : la licence **s'active toute
  seule** sur le PC (« Ta commande est livrée ») ; une option s'ajoute à sa licence de la même façon. Expirée, annulée,
  paiement refusé, remboursée, en revue : un texte clair chaque fois.
- **Mes achats** : « Mes commandes en ligne » (n°, date, article, montant TTC, état, **le reçu de Stripe**) ; l'accueil
  de la fenêtre Licence rappelle une commande en cours (« paiement en attente », « payée : licence en route »).
- Si quelque chose empêche de commencer (pas de réseau, boutique fermée, article retiré, Stripe qui ne répond pas) :
  « Commande non passée… rien n'a été débité ». Un service plus ancien ou une boutique fermée : pas de bouton Acheter,
  sans un mot (Demander reste).

**Dans les Studios : la fenêtre Licence**

- **Plus d'onglets empilés** : elle s'ouvre sur un **accueil** (ton résumé, puis les rubriques en quatre groupes : Ma
  licence, Obtenir ou étendre, Découvrir, Réglages) ; chaque rubrique a son **fil d'Ariane** (« Licence › Demander ma
  licence » : un clic sur Licence revient à l'accueil, la flèche montre les rubriques voisines ; Retour arrière ou Alt+←).
- **Mon profil a sa propre fenêtre** : l'accueil du Studio (Mon profil…), Aide › Mon profil…, et « Modifier mon
  profil… » à côté du formulaire de Demander ma licence (qui se met à jour).
- **Les images en grand** : on **change de fonctionnalité sans fermer** (‹ la liste ›, Page préc. et Page suiv.) ; une
  fonctionnalité **barrée** (pas dans ta licence, ou pas dans l'offre choisie) montre aussi ses captures, avec « pas
  dans ta licence ».
- **Les barres de défilement se tirent à la souris** (un clic dans la piste avance d'une page) là où seule la molette
  marchait : Demander ma licence, la fenêtre de mise à jour, la liste des styles de l'IHM, la visionneuse de fichiers,
  les suggestions d'un champ. Les cartes des offres ne coupent plus leur prix ni leur réduction ; l'onglet Éditions
  retrouve ses colonnes.

**Dans l'Admin**

- **Vente en ligne › Boutique** : l'état de Stripe (branché, mode test ou réel, le dernier événement reçu),
  **Brancher Stripe…** (tu colles ta clé secrète dans un champ masqué : l'Admin crée lui-même chez Stripe l'adresse où
  Stripe prévient ton service, puis passe la clé et le secret de cette adresse **à ton service sur Cloudflare** ; ils ne
  sont gardés **nulle part ailleurs** : ni dans l'Admin, ni dans un journal, ni dans une sauvegarde), Rebrancher,
  Ouvrir mon tableau de bord Stripe ; **Vendre en ligne** (marche/arrêt) ; **Livrer seul les commandes payées** ; le
  **catalogue** : pour chaque offre et chaque durée d'option, en vente ou non, le montant encaissé en € TTC, le libellé
  de la facture ; un écart avec le prix affiché du registre est signalé.
- **Vente en ligne › Commandes** : le tableau (n°, date, société, article, montant, état ; filtres par état), le détail
  (courriel, le PC, la licence livrée, le reçu), **Délivrer maintenant**, **Revoir…** (avec la raison), **Rembourser…**
  (admin+ ; la licence n'est pas révoquée d'office, la question le rappelle), Voir dans Stripe, Ouvrir le reçu.
- **La livraison seule** : ton Admin ouvert (ou à sa prochaine ouverture) voit chaque commande payée et **signe la
  licence lui-même** (comme une demande acceptée), la dépose pour le PC, la note au journal des admins (« commande n° N
  livrée ») ; une commande n'est livrée qu'une fois, même avec deux Admin ouverts ; une panne passagère est reprise
  toutes les 5 minutes sans jamais signer deux licences ; ce qui ne peut pas être livré passe « à revoir » avec la
  raison. Le tableau de bord montre « Commandes à livrer ».
- **Versions** : le bouton Annuler ne chevauche plus les notes, et **on écrit dans Notes** ; les barres de défilement
  des pages se tirent à la souris.
- En console : `boutique-etat`, `boutique-config`, `stripe-brancher --cle-variable NOM` (la clé lue dans une variable,
  jamais affichée), `commandes`, `commande-livrer`, `commande-revoir`, `commande-rembourser`.

## 1. Ce qu'il te reste à faire

1. Installer **l'Admin 1.46.0** sur ton PC (par-dessus la 1.45.0), puis les **Studios 1.46.0**.
2. **Service en ligne › Déployer le service** : l'Admin dit qu'il faut la 1.46.0 pour la boutique (le reste marche
   déjà en 1.44).
3. **Stripe, d'abord en mode test** : dans ton tableau de bord Stripe (Développeurs › Clés API), copie la **clé
   secrète de test** (`sk_test_…`), puis dans l'Admin : **Vente en ligne › Boutique › Brancher Stripe…**, colle-la. Il
   faut être connecté à Cloudflare sur ce PC (comme pour Déployer). Une clé restreinte (`rk_…`) marche aussi, si elle
   peut écrire les Webhook Endpoints, les Checkout Sessions et les Refunds, et lire les Invoices.
4. Dans le **catalogue**, mets en vente les offres et les durées d'option voulues avec leur **montant TTC**, coche
   **Vendre en ligne**, Enregistrer.
5. **Essaie un achat** depuis un Studio, avec ta carte de test Stripe (`4242 4242 4242 4242`, une date future, un code
   quelconque), l'Admin ouvert : la commande doit passer payée puis livrée, et la licence s'activer sur le PC.
6. Pour vendre pour de vrai : ton compte Stripe activé (c'est chez Stripe), puis **Rebrancher Stripe…** avec ta clé
   **réelle** (`sk_live_…`) ; la bande « Paiement d'essai » disparaît.

## 2. Les essais

- **Linux : 175 suites sur 175 réussies** (`ctest`, sur la version livrée). Celles que cette version ajoute ou touche :
  - `service_boutique` (40 contrôles, le **vrai service** lancé sur ce poste et un **faux Stripe**) : la boutique
    fermée puis ouverte ; la commande d'un Studio (sa demande scellée, que seul l'éditeur ouvre) ; une commande déjà
    en cours ; l'annulation ; Stripe qui ne répond pas (rien n'est gardé, rien n'est débité) ; le paiement et son
    reçu ; **la livraison d'une offre** (le Studio s'active seul, une seule fois) et **d'une option** (sa licence
    renouvelée avec l'option) ; hors ligne.
  - `adminboutique` (l'Admin contre le même vrai service et le faux Stripe) : la boutique réglée, une commande payée,
    **livrée seule** (la licence signée, déposée, son code scellé pour le PC, qui s'active avec lui ; le journal),
    **une seconde livraison refusée**, une option ajoutée à une licence, une commande à revoir, « Livrer seul »
    décoché, le remboursement ; **Brancher Stripe** avec un faux kit et un **faux wrangler** : le point d'arrivée créé
    chez le faux Stripe, les deux secrets posés, la clé reçue seulement sur l'entrée standard, rien d'écrit ni
    d'affiché ; la console.
  - `adminguiboutique` (86 contrôles : les pages Boutique et Commandes et le dialogue Brancher Stripe, à la taille de
    ta fenêtre et en 1920 × 1080 : aucune colonne hors de la vue ; un service 1.44 : un seul avertissement ; la clé dans
    un champ masqué, vidé dès le clic, jamais dans un message), `adminguiversions` (61 : Annuler ne chevauche plus,
    on écrit dans Notes), `scrollbars` (72 : tirer la barre, un clic dans la piste), `licenceui` (262 : l'accueil et
    le fil d'Ariane, Mon profil, les images en grand, **les boutons Acheter** - seulement sur ce qui est en vente,
    absents avec un service ancien ou une boutique fermée -, le récapitulatif et la phrase sur la carte, chaque état
    d'une commande, Mes achats) ; les essais du service (`npm test`) : 176 sur 176.
- **Sous Wine, de bout en bout** : les programmes Windows (l'Admin installé par son installateur, un IHM Studio neuf),
  le **vrai service 1.46.0** lancé sur ce poste (`workerd`, avec des secrets d'essai) et le **faux Stripe**. Journal :
  `essai-1.46.0-wine.txt` (codes masqués) ; captures : `captures-1.46.0.zip`. Le Studio est une copie avec la clé
  d'essai, jamais livrée.
  - **La fenêtre Licence** (un PC neuf, en Découverte) : l'accueil et ses rubriques (`1460_10`), Demander ma licence
    avec **Acheter** sur chaque offre en vente et « ou la demander sans paiement » (`1460_11`), **le fil d'Ariane**
    ouvert sur les rubriques voisines (`1460_12`), **une fonctionnalité barrée en grand**, avec ses captures et « pas
    dans ta licence » (`1460_13`), puis **la suivante sans fermer** (`1460_14`), **Mon profil** dans sa fenêtre
    (`1460_15`).
  - **L'achat** : PROFESSIONAL, le récapitulatif (`1460_16` : 468,00 € TTC, la société et le courriel du profil, la
    bande d'essai, la phrase sur la carte), **Payer** : la commande n° 1 « en attente du paiement » (`1460_17`) ; la
    page de paiement demandée à Stripe est écrite dans un journal (jamais ouverte : il n'y a pas de vrai Stripe).
  - **Le paiement** : le faux Stripe prévient le service par un événement signé ; le Studio passe à « Payée »
    (`1460_18`).
  - **La livraison seule** : l'Admin de Camille (la clé d'essai de l'éditeur), **simplement ouvert** sur Commandes,
    voit la commande payée et la livre (`1460_30` : « livrée ») ; **le Studio s'active seul** : « Ta commande est
    livrée » (`1460_19`, le code d'activation masqué sur la capture), sa licence PROFESSIONAL valide ; Mes achats
    (`1460_20`) : la commande, son reçu, l'achat. Le journal des admins : « commande n° 1 livrée (licence
    XPG-2026-00001, Client d'essai) ».
  - **Une option** commandée puis annulée avant de payer : « Commande n° 2 annulée : rien n'a été débité ».
  - **Ce que le service a demandé à Stripe** pour la page de paiement (relevé chez le faux Stripe) : pas de client
    Stripe (`customer`), pas d'enregistrement de la carte (`setup_future_usage`, `saved_payment_method`), pas
    d'abonnement ; la carte seulement (`payment_method_types`), une facture.
  - **Les pages de l'Admin** à la taille de ta fenêtre (1373 × 917) : Boutique (`1460_31` : Stripe en mode test, Vendre
    en ligne, Livrer seul, le catalogue avec ses montants TTC ; l'écart signalé sur PROFESSIONAL vient de sa réduction,
    voir § 4), **Rebrancher Stripe** (`1460_32`), Commandes et le détail (`1460_33`), le tableau de bord (`1460_34`).
    Versions (`1460_35`, sous Linux) : la publication avec ses notes, Annuler à sa place.
- **Les Studios** : l'installation en silencieux par-dessus la 1.45.0 passe, ta clé d'API est gardée, les programmes
  installés sont ceux construits (1.46.0). Le Studio livré (ton service compilé) s'ouvre (`1460_00`), lancé avec une
  adresse locale où rien ne répond : **ton service n'a pas été appelé**.
- **L'Admin** : son installateur, en silencieux par-dessus la 1.45.0 : les programmes et le kit du service identiques à
  ceux construits, une mise à jour, une désinstallation (le dossier de ta clé intact), une réinstallation ; ses pages
  d'installation (`page-1` à `page-7`).
- Corrigé en route : dans les programmes, un message du journal de session (« aucun changement depuis le début de la
  session ») s'affichait mal ; dans mon essai, au premier passage, l'Admin ouvert n'a pas pu s'afficher (deux écrans
  virtuels mêlés sous Wine) et mon script a cru la commande livrée alors qu'elle était « payée, à livrer » : les deux
  corrigés, le second passage est celui décrit ci-dessus.

## 3. Installer

**Les Studios** : lance `API-IHM-Studio-Setup-1.46.0.exe` (dans le zip) par-dessus la 1.45.0. Tes projets, tes
réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.46.0.exe` | `ED701EDD1883680039997A807235A1F11DDB2589F4737F8C843A2FE21D0FA02B` | 63 646 654 octets |
| `API-Studio-1.46.0-portable.zip` | `9DE115E9BA173DB5C8931D65EE13599417E83479A727DA03CA8A42E5CF955A58` | 250 fichiers |
| `IHM-Studio-1.46.0-portable.zip` | `03F57163B0D02C5C1BDD49A071EB1CFEA0C3024E48F603D1F3996809A32C70BB` | 251 fichiers |

**L'Admin (pour toi seul, envoyé à part)** : lance `XPG-License-Admin-Setup-1.46.0-EDITEUR.exe` par-dessus la 1.45.0 ;
le dossier de ta clé n'est pas touché. Ne le donne à personne : pour un nouvel admin, Administrateurs › Ajouter un
admin… lui envoie l'installateur publié dans Versions (sans ta clé).

| Fichier | SHA-256 | Taille |
|---|---|---|
| `XPG-License-Admin-Setup-1.46.0-EDITEUR.exe` | `C4A1E7ED4A4323CAB3B756360C021A990A3AD549AAC210CB08559D34A2D44983` | 7 348 253 octets |

## 4. Limites

- **Le vrai Stripe n'a jamais été appelé** : tous les essais passent par un **faux Stripe** lancé sur ce poste, qui
  imite son API (la page de paiement, l'événement signé, les remboursements) ; il n'a pas de page où taper une carte.
  La vraie page de paiement, le vrai événement et un vrai remboursement restent à voir chez toi, **en mode test
  d'abord** (§ 1).
- **Rien n'a été essayé sur un vrai Windows** : sous Linux et sous Wine. **Brancher Stripe** (qui passe la clé à
  Cloudflare par le kit du service, comme Déployer) n'a été essayé que sous Linux, avec un faux wrangler et le faux
  Stripe ; dans l'essai sous Wine, les secrets (des valeurs d'essai) sont donnés au service local à son lancement.
- **La licence n'arrive que si un Admin est ouvert** (le tien, ou celui d'un admin qui a les droits) : sinon, à sa
  prochaine ouverture ; le client voit « payée : licence en route ».
- **La TVA** : les montants sont ceux que tu mets (TTC) ; Stripe fait la facture, mais ne calcule pas la TVA (pas de
  Stripe Tax).
- **Une option « D'autres dates… »** ne s'achète pas en ligne (seulement les durées du catalogue) ; elle se demande.
- **L'écart dans le catalogue** compare le montant au prix affiché **sans sa réduction** : une offre en réduction
  (PROFESSIONAL dans l'essai : 490 € HT, réduite à 390 €) est signalée « ≠ affiché (588,00 € TTC) » alors que son
  montant (468,00 € TTC) suit bien la réduction. C'est un avertissement seulement ; il sera corrigé dans la 1.47.
- **Rembourser** ne révoque pas la licence : à toi de le faire si tu le veux (la question le rappelle).
- Sans courriel de l'éditeur réglé, la fenêtre « Ta commande est livrée » montre le code d'activation au client (comme
  pour une demande acceptée).
