# Maquette 1.47.0 : les statistiques des admins et de l'admin+, le dépliage, le clic droit, des courbes partout, le globe, les filtres (proposition)

La demande : « j'ai besoin de beaucoup beaucoup plus de statistiques sur les admin et admin+ (plus de déballage, clic droit
menu contextuel cohérent en fonction des listes) propose en priorité la liste des informations supplémentaires à mettre et
fait une maquette », puis « des courbes sur les ventes, sur les MAJ faites, sur les PC, des courbes générales et par client »
et le globe qui tourne, puis « dans toutes les listes/tableaux avec beaucoup d'éléments toujours pouvoir mettre des
filtres/recherches dans admin.exe ». La liste proposée est la planche 00 ; chaque élément dit sa source ([J] le journal des admins,
[S] les tables du service, [N] nouveau) et la planche où le voir.

Pages HTML (1600 x 1000). Les sources sont les `src-NN-*.html` : `python3 prep.py` en fait les `NN-*.html` (il ajoute la
barre latérale de l'Admin `<!--SIDE:cle-->` : « Statistiques » (nouveau) sous Pilotage, Boutique et Commandes sous Vente en
ligne, la version 1.47.0 en bas ; et, pour les planches 00 à 07, les graphiques `<!--G:nom-->` et les valeurs `<!--V:nom-->`
que `graphes.py` dessine en SVG depuis ses données d'essai, les mêmes partout). Les planches 08 à 11 ont leurs graphiques
dans `stats.js` / `globe.js` (et `stats.css`, `globe.css`) ; toutes partagent `planches.css` (dont la barre de filtres
`.fb`, la même partout).
Rendu en PNG : `node rendre.js <dossier> 1` (dans `v/`, pour vérifier) et `node rendre.js <dossier> 2` (dans `hd/`, à
livrer). Une seule page autonome : `python3 assembler.py <dossier> maquette-1.47.0.html` (styles, scripts et icônes
dedans ; le globe y tourne).

Les couleurs des graphiques sont en tête de `extra.css` : une couleur par admin (Camille Essai, Léa Essai, Hugo Essai,
Nina Essai, toujours dans cet ordre), par offre, la criticité, la comparaison en gris, les rampes séquentielle, ordinale
(les versions) et divergente ; les palettes ont passé le validateur (méthode dataviz : un seul axe par graphique, traits
fins, légende dès 2 séries, peu d'étiquettes, les couleurs d'état réservées aux alertes). Rien de client : des prénoms
« Essai », des sociétés « Exemple » ou « d'essai ».

- `00` **la liste** des informations proposées, en une page : la fiche d'un admin (qui et où, son activité, ce qu'il
  produit, sa sécurité), la vue d'ensemble, le dépliage, le clic droit, les courbes, le globe, les filtres ; [J] [S] [N] et
  la planche ;
- `01` **Administrateurs, la vue d'ensemble** (l'admin+) : la période commune (7 j, 30 j, 90 j, 12 mois, tout, deux dates)
  comparée à la période d'avant, les tuiles (admins actifs / en attente / retirés, actions, demandes en attente et délai,
  licences signées par les admins et par toi, montant encaissé, alertes), la barre de filtres, le classement trié par
  colonne avec Léa dépliée
  (ses chiffres, sa courbe sur 30 jours, ses 5 dernières actions, ses droits), les alertes chacune avec son geste ;
- `02` **la fiche d'un admin, vue d'ensemble** (Léa Essai) : qui et où (PC, empreinte, clé, profil et écarts, versions,
  connexions, heures habituelles), 8 tuiles avec leur courbe sur 12 semaines, ses actions par jour (contre la période
  d'avant), par type (les 13 droits puis la boutique, toujours dans cet ordre), par criticité, sa part parmi les admins ;
- `03` **la fiche, activité et sécurité** : la carte jour × heure (les heures inhabituelles signalées), ses jours (série,
  pics), le début et la fin de ses journées, ses droits utilisés ou non (« Retirer ce droit ? »), les signatures refusées,
  depuis un autre PC, le détail de ce qui compte, ses droits changés (qui, quand), la chaîne vérifiée ;
- `04` **la fiche, ce qu'elle produit** : ses licences (état, par offre, PC, prolongations, options, à renouveler), ses
  clients (les 5 premiers), ses demandes (le taux, le délai en distribution : médiane, moyenne, max ; la plus ancienne qui
  attend), la boutique (commandes, montant, panier, remboursements, par article), le reste ; et **ses courbes** sur 12
  semaines, les mêmes que les statistiques, limitées à elle (elle en couleur, les autres admins en gris) ;
- `05` **qui fait quoi** : la part de chacun par type d'action (une barre à 100 % par type, filtrable, l'écart à la période d'avant,
  qui est en tête), cette période et la période d'avant par admin (4 petits graphiques), et toi l'admin+ : versions,
  sauvegardes et restaurations, admins ajoutés et retirés, droits, criticités, Stripe, le certificat du service ;
- `06` **le clic droit, la règle** : le même ordre partout (Ouvrir / Déplier ; les actions ; Aller à › ; Filtrer sur › ;
  Copier › ; Exporter… ; à part, en rouge, avec « … »), un droit qui manque grisé avec sa raison, plusieurs lignes choisies ;
  puis 6 vrais menus sur leurs listes : Administrateurs (« Aller à » ouvert), Journal des admins (« Filtrer sur » ouvert),
  Licences (vue par Léa : Prolonger grisé), Clients, Commandes, Demandes en ligne (« Copier » ouvert) ;
- `07` **le dépliage partout** : Journal (l'objet touché avec ses liens, l'avant et l'après, la signature), Licences (le
  résumé, son histoire, « signée et livrée par Léa Essai »), Commandes, Demandes, Clients, Activations, chacune avec sa
  barre de filtres ;
- `08` **Statistiques, la vue générale** (nouvelle page, sous Pilotage) : une période, une comparaison (la période d'avant,
  l'an dernier) et un pas (jour, semaine, mois) communs ; les ventes (montant, cumul sur l'année, commandes, panier moyen,
  par offre, options, en ligne ou à la main, remboursements), les licences (actives, mouvements, échéances des 90 jours),
  les demandes (reçues, traitées, délai), les PC (actifs par offre, mouvements, flottantes au plafond, Découvertes) ; un
  clic sur un point filtre la liste (une infobulle en exemple) ;
- `09` **Statistiques, les mises à jour** : l'adoption de chaque version (une courbe par version, alignées sur leur jour de
  publication), la part des PC à jour dans le temps (Studios et Admins), le délai avant la mise à jour, les versions
  publiées, les PC pas à jour ;
- `10` **la fiche d'un client et ses courbes** (Atelier Exemple) : ses licences et leurs échéances, ses PC actifs dans le
  temps, ses mises à jour, ses achats et le montant cumulé (deux graphiques, jamais deux axes), ses demandes, qui l'a servi,
  la liste de ses PC (avec sa barre de filtres) ;
- `11` **le globe** (dans Statistiques, et en petit sur le Tableau de bord) : un globe pointillé qui tourne seul, se tourne
  à la souris et s'arrête au survol ; des calques à cocher (clients, PC actifs maintenant, pas à jour, admins, Découvertes,
  commandes, demandes), la carte d'une ville, la liste des villes (un clic tourne le globe vers elle), Compte / En direct,
  plein écran. La source : la ville et le pays que Cloudflare donne pour chaque appel (`request.cf`) ; le service ne garde
  que la ville de la dernière connexion de chaque PC, jamais son adresse IP. Le PNG montre une image figée (sur l'Europe,
  Lyon choisie) ; la page HTML tourne (`#anime` force l'animation, `#fige` la fige) ;
- `12` **filtrer et chercher, dans toutes les listes** (ajout du 10/11) : la même barre au-dessus de chaque longue liste —
  chercher (Ctrl+F, dans toutes les colonnes visibles, surligné), une puce par colonne utile (un petit menu, des cases et
  leurs nombres), « + Filtre » sur n'importe quelle colonne, « 12 sur 128 », « Effacer les filtres », « Mes filtres »
  (« Commandes à livrer »), l'entonnoir des en-têtes ; le clic droit « Filtrer sur › » met la même puce. Montrée sur
  Commandes, Activations, Journal des admins et Licences, avec la liste de toutes les listes de l'Admin qui l'ont et
  leurs puces toutes prêtes ; la même barre est sur les planches 01, 05, 07 et 10.

`maquette-1.47.0.html` : toutes les planches dans une seule page autonome.
