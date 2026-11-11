# 1.48.1 : les limites de la 1.48 corrigées, des statistiques justes après chaque changement, Supprimer et Actualiser partout

Fait à partir de : « corrige les limites sauf essai complet sous windows », « dans les studios, dans notifications
ajouter un bouton 'actualiser' », « quand je supprime des clients, des pc, des demandes, des admins, des licences, des
options etc il faut que les parties statistiques se mettent à jour … mettre actualiser aussi ici », « aussi lors de
nouvelles versions etc, vérifier la cohérence », « ajouter un bouton réévaluer, et ensuite ça fait un modal avec les
modifications apportées dans le service », « dans tous les onglets avoir supprimer et que ça mette à jour partout » (et
« et actualiser »), ta capture de la cloche (« pourquoi notifications prend toute la largeur de l'écran »), et « dans une
session si quelqu'un revient au menu général ça le fait pas quitter ».

## 0. En bref

**Dans l'Admin**

- **« Demandes en ligne » n'existe plus** : les demandes de l'assistant « Demander ma licence » arrivent dans
  **Commandes et demandes**, sur une ligne « Demande de licence » (un type de plus dans la barre de filtres).
  Délivrer… et Refuser… y font exactement ce que faisait l'ancienne page (la même fenêtre, la même signature). Les
  anciens liens (tableau de bord, journal, fiche client) ouvrent cette page, filtrée. Le nombre sur l'entrée du menu
  compte les deux.
- **Rembourser un seul article** d'une commande payée : « Rembourser… » sur la ligne choisie (la question dit l'article
  et son montant TTC) ; Stripe ne rembourse que ce montant ; les autres articles ne bougent pas. « Rembourser la
  commande… » reste pour tout. Comme avant, la licence livrée n'est pas révoquée par un remboursement.
- **Des statistiques justes** :
  - **Ce qui est supprimé disparaît de partout** (un client, une licence, leurs PC) : les courbes, même des jours
    passés, le globe, « actifs maintenant », la fiche client, Mises à jour. « Restaurer » les remet.
  - **Ce qui s'est passé reste** : une licence révoquée, un PC retiré, une demande traitée, une option retirée comptent
    aujourd'hui, hier ne change pas. Le journal des admins n'est jamais réécrit.
  - **Après chaque geste**, partout où il y a des chiffres, tout se recharge tout de suite (Statistiques et ses onglets,
    le tableau de bord, le globe, la fiche client, Administrateurs).
  - **« Actualiser »** (aussi F5) et **« Réévaluer »** dans Statistiques. Réévaluer demande au service de tout
    recalculer à partir de ce qui existe, puis une fenêtre liste ce qu'il a corrigé (ou « Rien à corriger : les
    statistiques étaient cohérentes. »). Il ne touche qu'aux statistiques, jamais aux licences ni aux clients ; une ligne
    au journal des admins.
  - **Une nouvelle version publiée** met à jour la dernière version, l'adoption et qui est à jour, partout en même temps.
@@SUPPRIMER@@

**Dans les Studios**

- **Notifications › « Actualiser »** (la fenêtre, et en tête de la cloche du bandeau) : les messages de l'éditeur, les
  mises à jour et les offres relevés tout de suite auprès du service, sans attendre le relevé automatique (toutes les
  2 minutes) ; une ligne dit ce qui a été relevé, ou pourquoi rien n'a pu l'être.
- **La cloche ne prend plus toute la largeur** : les textes longs sont raccourcis avec « … », le détail passe en petit
  sous le titre sans le répéter ; le texte entier reste dans la fenêtre Notifications.
- **Sessions : revenir au menu principal demande confirmation** : « Quitter la session et revenir à l'accueil » (l'hôte :
  « Arrêter la session… », pour tous) ou « Rester dans la session » (rien ne change). Avant, l'accueil s'ouvrait et la
  session continuait derrière.
- **Mes achats** : un article remboursé seul est marqué « remboursée », les autres restent tels quels.

## 1. Ce qu'il te reste à faire

1. Installer **l'Admin 1.48.1** (par-dessus la 1.48.0), puis les **Studios 1.48.1**.
2. **Service en ligne › Déployer le service** (la 1.48.1) : le remboursement d'un article, Réévaluer, les suppressions
   côté service en ont besoin.
3. Une fois : **Statistiques › Réévaluer** — la fenêtre te montre ce qui était incohérent chez toi et l'a corrigé.

## 2. Les essais

@@ESSAIS@@

## 3. Installer

@@EMPREINTES@@

## 4. Limites

- **Pas d'essai complet sous Wine** (comme tu l'as dit) : les parcours sont essayés sous Linux contre le vrai service
  lancé sur ce poste et un faux Stripe ; sous Wine, l'installation des programmes et des captures des pages.
- **Le vrai Stripe n'a pas été appelé** (un faux, qui sait maintenant rembourser une partie) : ton remboursement d'essai
  d'un article est la vraie vérification.
- Les jours passés : la part d'une activation finie depuis plus d'une semaine (le service l'efface) ne peut plus être
  retirée d'un ancien relevé (docs/STATISTIQUES.md § 15).
- Le service ne sait pas retirer une version publiée (il refuse une version plus ancienne) : rien à mettre en cohérence
  de ce côté.
- Le retour à l'accueil dans une session : vérifié par la lecture et la construction, pas par un essai automatique (aucun
  essai ne lance le Studio entier).
- **Rien sur un vrai Windows** : sous Wine seulement.
