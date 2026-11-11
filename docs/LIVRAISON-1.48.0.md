# 1.48.0 : une seule boutique avec un panier (Licence, Options, Panier) ; chaque article validé dans l'Admin

Fait à partir de : « la gestion des paiements des options fonctionne pas comme les paiements de licence, ça arrive dans
les demandes (ça ne doit pas), on doit avoir un bouton ACHETER comme dans les licences / … plusieurs onglets 'Licence'
'Options' et on a 'Panier' … payer tout ce qu'il y a dans le panier / vider le panier / retirer des articles, quand on
paie on a un récap, les demandes en ligne doivent fonctionner aussi mais on doit valider chaque article ».

## 0. En bref

- **Studios, fenêtre Licence › Boutique : trois onglets, Licence, Options et Panier (n)** (`1480_01`).
  - **Acheter** sur une offre ou sur une durée d'option la met au panier. **Une option achetée ne devient plus jamais
    une demande** : elle se paie comme une licence. Une option se prend pour ta licence actuelle ou pour une licence
    déjà dans le panier.
  - « ou la demander sans paiement » met l'article au panier comme demande (affiché « sur demande »).
  - Le panier : ses lignes (article, ce qu'il donne, pour quelle licence, prix TTC), **Retirer**, **Vider le panier**,
    le total TTC. Il est gardé dans le dossier des licences : fermer la fenêtre ne le perd pas.
  - **Payer — total TTC** : un **récapitulatif** (chaque ligne, le total, ta société, ton nom, ton courriel, la phrase
    sur la carte jamais gardée) (`1480_02`), puis **une seule page sécurisée de Stripe** pour toutes les lignes payantes.
  - **Envoyer la demande** envoie les lignes sans paiement ; **Tout demander sans paiement** envoie tout le panier
    comme demande.
  - **Mes achats** montre chaque commande avec ses articles et l'état de chacun.
- **Le service** : une commande a maintenant des **lignes** (jusqu'à 20). Chaque ligne a son état : à payer, payée,
  à valider, livrée, refusée, remboursée. Un paiement = une seule page Stripe avec plusieurs articles (tes prix Stripe
  de la 1.46.1). Livrés dans le bon ordre : la licence d'abord, puis les options du même panier sur cette licence.
  Les Studios 1.46 et 1.47 marchent toujours (leur achat devient une commande d'une ligne).
- **L'Admin : une seule page « Commandes et demandes »** (Vente en ligne) (`1480_03`). Une commande par ligne du
  tableau, dépliée en ses articles. **Chaque article se valide à part** : **Délivrer** ou **Refuser** (avec une
  raison), qu'il soit payé ou demandé. « Livrer seul les commandes payées » livre toujours seul les articles payés.
  La barre de filtres, le clic droit dans le même ordre, le volet de droite, Rembourser (la commande payée).

## 1. Ce qu'il te reste à faire

1. Installer **l'Admin 1.48.0** (par-dessus la 1.47.0), puis les **Studios 1.48.0**.
2. **Service en ligne › Déployer le service** (la 1.48.0) **avant** d'installer les Studios 1.48 chez tes clients : le
   panier a besoin du service 1.48.0.
3. Refaire un achat d'essai depuis un Studio : une licence + une option dans le panier, Payer, la carte de test de
   Stripe (4242 4242 4242 4242), puis les deux articles dans Commandes et demandes.

## 2. Les essais

- **Linux : 190 suites sur 190 réussies** (`ctest`, sur la version livrée). Le journal `essais-1.48.0.txt` donne le détail de ceux du
  panier :
  - **le service** (`node --test`, 30 sur 30 pour la boutique) : les lignes vérifiées (un article deux fois, une
    option sans licence : refusés) ; un panier payé = **une** page de Stripe avec une ligne d'article par article,
    chaque ligne payée puis livrée une à une ; une demande sans paiement = la même commande « à valider », refusée ou
    livrée ligne par ligne ; une commande d'avant la 1.48 (un article) devient une commande d'une ligne ;
  - **le Studio contre le vrai service local** (`service_boutique`, 45 contrôles) : une commande de plusieurs lignes,
    une page de paiement, payée par le faux Stripe ; une demande sans paiement, chaque ligne validée ;
  - **l'Admin contre le vrai service local** (`adminboutique`, 115 contrôles) : un panier payé (une licence et son
    option : l'option posée sur la licence qui vient d'être livrée) ; une demande validée ligne par ligne ;
    `adminguiboutique` (111) et `licenceui` (266 : les onglets, le panier, Retirer, Vider, le récapitulatif).
- **Sous Wine** : le panier avec trois articles (`1480_01`), le récapitulatif avant Stripe (`1480_02`), la page
  Commandes et demandes de l'Admin (`1480_03`), photographiés sur les programmes Windows.
- **Les Studios** : l'installation en silencieux par-dessus la 1.47.0, ta clé d'API gardée ; le Studio livré s'ouvre (`1480_00`),
  lancé avec une adresse locale où rien ne répond : **ton service n'a pas été appelé**.
- **L'Admin** : son installateur en silencieux par-dessus la 1.47.0, une désinstallation (le dossier de ta clé intact),
  une réinstallation ; sa première page (`1480_9`, « Version 1.48.0 · pour l'éditeur »).
- Corrigé en route : cinq essais attendaient encore le service en 1.47.0 et l'ancien nom « Commandes » dans la barre
  de l'Admin.

## 3. Installer

**Les Studios** : lance `API-IHM-Studio-Setup-1.48.0.exe` (dans le zip) par-dessus la 1.47.0. Tes projets, tes
réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.48.0.exe` | `959432FF2A5C2BA4C7BC6D77CAA040ABC3BED8844BCDC8BF7BE49F447CA14876` | 63 798 145 octets |
| `API-Studio-1.48.0-portable.zip` | `8393E13F0719323165D0EB6FC7B9F2150B7530850FA8EA361E51F39DDDB1D665` | 250 fichiers |
| `IHM-Studio-1.48.0-portable.zip` | `95B81AADBB762003BA0031CE9A85AA67552638A05B0ECFAAB551E724CF9EAC3A` | 251 fichiers |

**L'Admin (pour toi seul, envoyé à part)** : lance `XPG-License-Admin-Setup-1.48.0-EDITEUR.exe` par-dessus la 1.47.0 ;
le dossier de ta clé n'est pas touché. Ne le donne à personne : pour un nouvel admin, Administrateurs › Ajouter un
admin… lui envoie l'installateur publié dans Versions (sans ta clé).

| Fichier | SHA-256 | Taille |
|---|---|---|
| `XPG-License-Admin-Setup-1.48.0-EDITEUR.exe` | `FDED0DAD8F29415E3088B439C36BF9F758F7426FE0C824A8CDD003EC13A1C448` | 8 736 629 octets |

## 4. Limites

- **Le vrai Stripe n'a pas été appelé** : un faux Stripe sur ce poste (plusieurs articles par page de paiement). Ton
  achat d'essai (§ 1) est la vraie vérification.
- **Pas de long essai de bout en bout sous Wine cette fois** (pour livrer vite) : un panier payé de plusieurs articles,
  jusqu'à la livraison, est couvert par les essais Linux contre le vrai service local, pas par un Studio et un Admin
  Windows ensemble. Les captures sous Wine montrent le panier, le récapitulatif et la page de l'Admin.
- **« Demandes en ligne » reste dans l'Admin** (Pilotage) pour les demandes de l'assistant « Demander ma licence »
  (le formulaire avec les PC et la durée, qui n'est pas la boutique). Les demandes faites depuis le panier arrivent dans
  Commandes et demandes. Réunir aussi ces deux-là est pour la 1.49 si tu le veux.
- **Rembourser** rembourse la commande payée entière (pas un seul article).
- Sur les captures, le bas de l'Admin dit encore 1.47.0 : elles ont été prises juste avant le passage du numéro de
  version.
- **Rien sur un vrai Windows** : sous Wine seulement.
