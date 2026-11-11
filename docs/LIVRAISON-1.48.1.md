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
- **Supprimer et Actualiser dans les onglets** :
  - **Supprimer** (un ou plusieurs, la barre et le clic droit ; la confirmation dit ce qui part avec) :
    **Licences** (révoquée ou finie seulement ; une licence en ligne est aussi retirée du service, ses PC libérés ; sa
    révocation reste dans la liste signée), **Activations** (un PC qui ne sert plus ; un PC actif : « retire-le de sa
    licence ou révoque-le d'abord »), **Fonctionnalités** (si aucune offre, option ou licence ne la porte), **Commandes et
    demandes** (une commande sans argent, une demande close). Déjà là : Clients, Offres, Options ; « Retirer » pour les
    admins, les PC des flottantes, la révocation.
  - **Grisé avec sa raison** là où supprimer casserait quelque chose : une licence en vigueur, une commande payée (la
    comptabilité), Historique et le Journal des admins (l'histoire, signée), Versions (les Studios s'appuient dessus).
  - **Actualiser** : Licences, Activations, Offres, Options, Fonctionnalités, Révocations, Historique, Journal des admins,
    Versions, Licences flottantes, Demandes d'admin (« Relever » y devient « Actualiser »), Statistiques.
  - **Les incohérences trouvées et corrigées** : une licence supprimée revenait dans le nombre de licences du client
    (par ses activations) ; les PC d'une licence ou d'un client supprimés restaient dans Activations, le tableau de bord
    et les nombres du client ; les licences en ligne supprimées comptaient encore (tableau de bord, flottantes).

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

- **Linux : 191 suites sur 191 réussies** (`ctest`, sur la version livrée ; journal `essais-1.48.1.txt`). Celles de cette version :
  - **le service** (`node --test`, 232 sur 232), dont **la cohérence** : une histoire fictive passée par les routes
    normales, puis, l'une après l'autre, un PC retiré, une licence supprimée, un client supprimé (un admin sans le droit :
    403), une option retirée, un admin sorti de la liste, une demande traitée, une version publiée (une plus ancienne
    refusée), Réévaluer deux fois (rien la seconde), le client restauré ; après chaque étape, les courbes, le globe,
    « actifs maintenant », Mises à jour et les chiffres des admins d'accord entre eux et avec ce qui existe (aujourd'hui et
    hier) ; puis une sauvegarde volontairement incohérente : Réévaluer liste les corrections, un second appel n'en trouve
    plus. Et : rembourser une ligne (Stripe : le montant de la ligne seulement ; deux fois : refusé), supprimer une
    commande sans argent (une payée : refusé), une demande close, une licence ;
  - **l'Admin contre le vrai service local** : `adminguicoherence` (458 contrôles : une suppression de chaque sorte, puis
    chaque liste, ses nombres « N sur N », le tableau de bord, la fiche client), `adminstats` (73) et `adminguistats`
    (261 : Actualiser, Réévaluer et sa fenêtre, « Rien à corriger », le rechargement après un changement),
    `adminboutique` (123) et `adminguiboutique` (127 : une demande de licence délivrée et refusée depuis Commandes et
    demandes, un article remboursé seul, Supprimer) ;
  - **les Studios** : `service_boutique` (50 : « Actualiser » des notifications contre le vrai service local, Mes achats
    avec une ligne remboursée), `topbar8` (la cloche : Actualiser en tête, une largeur raisonnable avec un détail très long,
    le détail sous le titre sans le répéter).
- **Sous Wine** : l'Admin 1.48.1 avec le vrai service local et le faux Stripe (`1481_01` à `1481_03`) : Commandes et
  demandes avec deux demandes de licence, une commande, un panier payé et une demande du panier ; « Rembourser… » sur un
  article (la question dit l'article et son montant) ; l'article remboursé seul (« remboursée »), le reste de la commande
  inchangé ; le pied de l'Admin dit 1.48.1.
- **Les Studios** : l'installation en silencieux par-dessus la 1.48.0, ta clé d'API gardée ; le Studio livré s'ouvre
  (`1481_00`), lancé avec une adresse locale où rien ne répond : **ton service n'a pas été appelé**.
- **L'Admin** : son installateur en silencieux par-dessus la 1.48.0, une désinstallation (le dossier de ta clé intact),
  une réinstallation ; ses pages, de Bienvenue au Récapitulatif (`1481_9_…page-1` à `page-5`, jamais l'installation).
- Corrigé en route : les pages de l'installateur n'avançaient pas en 1.48.0 (la touche Entrée n'allait pas à la fenêtre).

## 3. Installer

**Les Studios** : lance `API-IHM-Studio-Setup-1.48.1.exe` (dans le zip) par-dessus la 1.48.0. Tes projets, tes
réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.48.1.exe` | `AB49A6B1AED053288C2EBAF6914946358AFE69377CC0F833005105E9789EA198` | 63 812 435 octets |
| `API-Studio-1.48.1-portable.zip` | `A2F40D0F3F8172949F7998EF94D02F9A358AF38D98B16860A7EED929107A2D90` | 250 fichiers |
| `IHM-Studio-1.48.1-portable.zip` | `1E2F2F8664680541BA28416C85F87113BA1AA6CD33AB53AABE09A20158F7A207` | 251 fichiers |

**L'Admin (pour toi seul, envoyé à part)** : lance `XPG-License-Admin-Setup-1.48.1-EDITEUR.exe` par-dessus la 1.48.0 ;
le dossier de ta clé n'est pas touché. Ne le donne à personne : pour un nouvel admin, Administrateurs › Ajouter un
admin… lui envoie l'installateur publié dans Versions (sans ta clé).

| Fichier | SHA-256 | Taille |
|---|---|---|
| `XPG-License-Admin-Setup-1.48.1-EDITEUR.exe` | `7AA53B439322D26B2759BEC9B2B5F274ADE0894D6B9F366FF2C92FEE12A96D24` | 8 837 641 octets |

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
- Pas encore faits : supprimer plusieurs clients, offres ou options d'un coup (un par un) ; Supprimer et Actualiser
  dans la fiche client ; Actualiser dans Mon compte, Service en ligne, Sécurité ; dans Administrateurs, le bouton garde
  le nom « Relever » (« Actualiser », plus long, cachait les dates).
- Un PC supprimé l'est dans l'Admin seulement (il revient s'il redemande une licence) ; le fil « activité récente » du
  tableau de bord et la corbeille des clients gardent l'histoire.
- **Rien sur un vrai Windows** : sous Wine seulement.
