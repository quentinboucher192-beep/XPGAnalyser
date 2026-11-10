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

@@ESSAIS@@

## 3. Installer

@@EMPREINTES@@

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
- **Rembourser** ne révoque pas la licence : à toi de le faire si tu le veux (la question le rappelle).
- Sans courriel de l'éditeur réglé, la fenêtre « Ta commande est livrée » montre le code d'activation au client (comme
  pour une demande acceptée).
