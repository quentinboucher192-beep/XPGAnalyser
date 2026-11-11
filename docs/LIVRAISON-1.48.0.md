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

@@ESSAIS@@

## 3. Installer

@@EMPREINTES@@

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
