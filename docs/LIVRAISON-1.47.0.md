# API Studio, IHM Studio et l'Admin 1.47.0 : les statistiques des admins et de l'admin+, les courbes, le globe, les listes (filtrer, clic droit, déplier), Mon compte, les demandes d'admin en liste, le service qui voyage avec l'Admin

Faite à partir de la maquette 1.47.0 que tu as validée (« je valide la maquette, intègre-la dans la 1.47 ») et de tes
retours : « j'ai besoin de beaucoup beaucoup plus de statistiques sur les admin et admin+ (plus de déballage, clic droit
menu contextuel cohérent en fonction des listes) », « des courbes sur les ventes, sur les MAJ faites, sur les PC, des
courbes générales et par client », le globe qui tourne (« avec la possibilité de voir les clients/admins/gens
actifs/clients pas à jour »), « dans toutes les listes/tableaux avec beaucoup d'éléments toujours pouvoir mettre des
filtres/recherches », le panneau Session (« le texte sort du cadre, attention au redimensionnement »), « ajouter un
onglet 'mon compte' avec la liste des pouvoirs, l'état du compte, le ID », « dans les demandes pour passer admin
comment faire si plusieurs d'un coup ? », et « quand j'envoie le admin.exe vers une demande, la partie service n'est
pas active quand il l'installe… y'a que l'admin+ qui peut gérer la partie service sauf sur dérogation… la partie
service doit voyager avec l'installateur ». Elle contient aussi le correctif 1.46.1 de la boutique.

Elle se met à jour par-dessus la 1.46.0 (les Studios) et la 1.46.1 (l'Admin) et garde tes données. **Le service passe en
1.47.0** (les statistiques, le globe, la dérogation) : l'Admin te le dit et le redéploie d'un bouton.

## 0. En bref

**Dans l'Admin : les statistiques** (une nouvelle page sous Pilotage)
- **Statistiques** : une période (7 j, 30 j, 90 j, 12 mois, tout, deux dates), une comparaison (la période d'avant, l'an
  dernier) et un pas (jour, semaine, mois) communs à 17 courbes : les ventes (montant, cumul de l'année, commandes, panier
  moyen, par offre, options, en ligne ou à la main, remboursements), les licences (actives, mouvements, échéances), les
  demandes (reçues, traitées, délai), les PC (actifs par offre, mouvements, flottantes au plafond, Découvertes). Une bulle
  au survol ; **un clic sur un point ouvre la liste correspondante, filtrée**. Des onglets Ventes, Licences, PC, Mises à
  jour, Demandes, Admins (les heures à l'heure de Paris), Globe ; Exporter (CSV) ; Relever maintenant (l'admin+).
- **Mises à jour** : l'adoption de chaque version jour après jour depuis sa publication, la part des PC à jour (Studios et
  Admins), le délai avant la mise à jour, les PC pas à jour.
- **La fiche d'un client et ses courbes** (Clients › « Sa fiche et ses courbes ») : ses licences et leurs échéances, ses PC
  actifs dans le temps, ses mises à jour, ses achats et leur cumul, ses demandes, qui l'a servi, ses PC.
- **Le globe** (Statistiques › Le globe, et en petit sur le tableau de bord) : il tourne seul, se tourne à la souris,
  zoome à la molette, s'arrête au survol ; des calques à cocher (clients, PC actifs maintenant, clients pas à jour,
  admins, Découvertes, commandes, demandes), la liste des villes (un clic y tourne le globe), la carte d'une ville (Voir
  les clients, Les PC), **En direct** (un point pulse à chaque connexion), plein écran. Le service ne garde que la ville
  de la dernière connexion de chaque PC (arrondie à 10 km), **jamais son adresse IP**, oubliée après un an sans appel.
- **Le tableau de bord** : 4 courbes (montant encaissé, licences actives, PC actifs, demandes) affichées tout de suite, et
  la petite carte du globe.

**Dans l'Admin : les admins et l'admin+**
- **Administrateurs › Vue d'ensemble** : la période comparée à celle d'avant, les tuiles (admins, actions, demandes en
  attente et délai, licences signées par les admins et par toi, montant encaissé, alertes), le classement trié par
  colonne, **une ligne qui se déplie** (ses chiffres, sa courbe sur la période, ses 5 dernières actions, ses droits), les
  alertes chacune avec son geste (inactif depuis 30 jours, droit jamais utilisé, Admin pas à jour, signature refusée,
  action critique, heure inhabituelle).
- **La fiche d'un admin** : qui et où, 8 tuiles avec leur courbe sur 12 semaines, ses actions par type et par criticité,
  sa semaine jour × heure (les heures inhabituelles signalées), ses jours et sa série, le début et la fin de ses journées,
  ses droits utilisés ou non (« Retirer ce droit ? »), sa sécurité, ce qu'il produit (licences, clients, demandes,
  boutique). Un admin ne voit que la sienne (« Ma fiche »).
- **Qui fait quoi** : la part de chacun par type d'action, l'écart à la période d'avant, qui est en tête, cette période et
  celle d'avant par admin, et tes actions réservées d'admin+.
- **Mon compte** (en tête de Sécurité, et un clic sur ton nom en haut à gauche) : ton **ID** (la clé et son empreinte en
  6 groupes, à copier), **l'état de ton compte** (actif depuis, ajouté par l'admin+ dans la liste n°, en attente, retiré,
  le certificat du service, ce PC, les versions), **tes pouvoirs** (les 13 droits et la boutique : accordé ou non, ce qu'il
  permet, sa criticité, ton profil et ses écarts, sa dernière utilisation, l'histoire de tes droits) et la gestion du
  service (dérogation en cours ou non).
- **Demandes d'admin** (pour toi, l'admin+) : la liste des demandes pour devenir admin, comme les demandes de licence
  (filtres, clic droit, dépliage, l'invitation correspondante). **Plusieurs d'un coup** : coche-les, « Accepter les 3… » :
  une seule fenêtre montre chaque empreinte avec sa case « J'ai vérifié l'empreinte (lue au téléphone) » et son profil ;
  tant qu'une case est vide, rien n'est signé (ou retire-la du lot) ; puis **une seule liste des admins signée** les
  ajoute tous, chacun reçoit la clé de synchro. « Refuser 2 demandes… » avec une raison.

**Dans l'Admin : toutes les listes** (Licences, Clients, Demandes en ligne, Activations, Commandes, Journal des admins,
Offres, Options, Fonctionnalités, Licences flottantes, Révocations, Versions, Historique, Administrateurs, Demandes
d'admin)
- **Filtrer et chercher** au-dessus de chaque liste : chercher (Ctrl+F, dans toutes les colonnes, les trouvailles
  surlignées), une puce par colonne utile (des cases à cocher et leurs nombres ; les dates en périodes), « + Filtre »,
  « 12 sur 128 », Effacer les filtres, **Mes filtres** (enregistrés par liste), l'entonnoir dans l'en-tête filtré.
- **Le clic droit dans le même ordre partout** : Ouvrir / Déplier ; les actions ; Aller à › ; Filtrer sur › ; Copier › ;
  Exporter… ; à part, en rouge, avec « … » : Retirer, Révoquer, Supprimer. Un droit qui manque : grisé avec sa raison ;
  plusieurs lignes choisies : seulement ce qui s'applique à toutes (« Exporter 12 lignes »).
- **Les lignes se déplient** (→ / ←, ou un clic) : le résumé, les liens, l'avant et l'après, qui l'a fait.
- **Aucun texte coupé** : un texte trop long finit par « … » et se lit en entier dans une bulle ; les erreurs du service
  tiennent en une phrase courte, le détail dans la bulle.

**Dans l'Admin : le service**
- **Il voyage avec l'Admin** : un nouvel Admin arrive relié à ton service (son adresse et son identité, **signées par ta
  clé d'éditeur** ; une adresse modifiée ou non signée est refusée). Il la trouve dans le dossier de licence du Studio du
  même PC (le cas quand tu l'envoies par une demande), à côté de l'installateur, ou par un second lien de 48 h envoyé
  avec celui de l'installateur. Le kit de mise en ligne est toujours installé. « Service en ligne › Exporter la
  connexion… » la donne aussi.
- **Seul l'admin+ gère le service** ; les admins le voient sans y toucher (« l'admin+ seul gère le service »).
- **La dérogation** : tu accordes « Gérer le service » à un admin pour **1, 4, 24 ou 72 heures** (jamais plus), depuis
  Service en ligne ou sa fiche ; le service la vérifie avec sa propre horloge, elle s'arrête seule, tu peux la révoquer ;
  chaque usage t'envoie une alerte et va au journal. Elle permet de déployer le service, changer son adresse ou ses
  adresses de secours, prendre le relevé du jour ; certifier, Stripe, la sauvegarde et la restauration restent à toi.
  L'admin dérogé déploie avec **sa propre connexion Cloudflare** (membre de ton compte), jamais un jeton partagé.

**Dans les Studios**
- **Le panneau Session** (l'onglet et la fenêtre) : les textes passent à la ligne dans leur cadre, les noms trop longs
  finissent par « … » (en entier dans une bulle), les boutons ne se chevauchent plus, à toutes les tailles ; la fenêtre
  se met sur deux colonnes ou une selon la place ; pareil pour Démarrer, Rejoindre, Retirer quelqu'un.
- **L'accueil** : « Mon profil… » toujours en entier dans l'encart de la licence.
- **Demander ma licence** : une erreur de paiement passe à la ligne dans sa bande au lieu d'être coupée.
- **Acheter** suit la réduction en cours (le correctif 1.46.1, côté service).

## 1. Ce qu'il te reste à faire

1. Installer **l'Admin 1.47.0** (par-dessus la 1.46.1 ou la 1.46.0), puis les **Studios 1.47.0**.
2. **Service en ligne › Déployer le service** (la 1.47.0) ; puis **Boutique › Enregistrer** une fois si tu ne l'as pas
   fait en 1.46.1.
3. Les statistiques se remplissent dès le premier jour (ce qui est avant la 1.47 est reconstitué depuis les tables du
   service, la page le dit).

## 2. Les essais

- **Linux : 190 suites sur 190 réussies** (`ctest`, sur la version livrée). Celles que cette version ajoute ou touche, entre autres :
  `charts` (217 contrôles : échelles, formats en français, couleurs stables, survol, clic, cas limites) et
  `chartsgallery` (56) ; `globe` (139 : le masque des terres, la projection, la rotation, les groupes de villes, le coût
  quand il ne tourne pas) et `adminguiglobe` (99) ; `listes` (103 : la barre de filtres, Mes filtres, le menu dans le
  même ordre, les lignes dépliées) et `adminguilistes` / `adminguilistes2` (les 14 listes) ; `adminguistats` (247 : les
  17 courbes, la période, la comparaison, un clic qui filtre, aucun texte coupé à 1373 × 917 ni à 1100 × 700) ;
  `adminguiadminsvue` (109) et `adminjournalstats` (67 : les chiffres du journal, l'heure de Paris, le changement
  d'heure) ; `adminguicompte` (123 : Mon compte pour chaque rôle ; contre le vrai service local, 2 demandes d'admin
  acceptées d'un coup dans une seule liste signée, une case vide qui bloque tout) ; `adminderogation` (68 : la connexion
  signée acceptée, modifiée ou non signée refusée ; la dérogation accordée, utilisée, révoquée, finie à l'heure du
  service) et `adminguiservice` (37) ; `adminstats` (62, contre le vrai service local) ; `sessionui` (le panneau Session
  de 200 × 60 à 2400 × 600, aucun texte hors de son cadre) ; les essais du service (`npm test`, dont 18 sur les
  statistiques et 7 sur la dérogation).
- **Sous Wine, de bout en bout** : le **vrai service 1.47.0** lancé sur ce poste et un **faux Stripe** ; **six mois
  d'histoire fictive** passés par les routes normales du service, l'horloge du service reculée semaine après semaine
  (1 216 gestes : Camille l'admin+, Léa, Hugo et Nina Essai, des licences, des PC dans 21 villes, des demandes, des
  commandes payées et une remboursée, des Découvertes, les versions 1.44 à 1.47 publiées et installées) ; puis l'Admin
  Windows (`XPG-License-Manager-Admin.exe`) photographié page par page. **69 contrôles réussis, 0 échec** (journal
  `essai-1.47.0-wine.txt`, captures `captures-1.47.0.zip`, `1470_01` à `1470_46`) : le service rend chaque mesure
  (17 licences créées, 32 PC actifs, 10 demandes, 6 commandes payées, 13 Découvertes dont 4 converties, 170 mises à
  jour…) ; le tableau de bord, Statistiques et ses onglets, Mises à jour, le globe (Compte, une ville, En direct), la
  fiche d'un client, toutes les listes avec leur barre, Administrateurs (vue d'ensemble, qui fait quoi), la fiche de Léa,
  Mon compte (Camille, Léa, Léa sous dérogation), **Demandes d'admin** (deux demandes, les deux acceptées d'un coup),
  **Service en ligne** (Camille ; la dérogation de 4 h accordée à Léa : sa page s'ouvre, puis révoquée : de nouveau en
  lecture seule). Aucun code ni jeton en clair dans les journaux. Les versions publiées dans l'essai sont de petits
  installateurs factices (ils portent un numéro, n'installent rien).
- **Le service qui voyage avec l'Admin** (un essai à part, sous Wine) : le lien de l'installateur et de la connexion
  (48 h, une seule fois) ; l'Admin installé démarre relié au service ; une connexion modifiée est refusée ; la demande
  de Léa, acceptée ; sa page du service en lecture seule ; une dérogation d'1 h : la page s'ouvre, `service-deployer
  --essai` passe et va au journal ; l'horloge du service avancée d'1 h 02 : « a pris fin », la page repasse en lecture
  seule. Sous Linux, un admin sous dérogation a lancé le vrai `wrangler deploy --dry-run` (rien n'est parti vers
  Cloudflare).
- **Les Studios** : l'installation en silencieux par-dessus la 1.46.0 passe, ta clé d'API est gardée, les programmes
  installés sont ceux construits (1.47.0). Le Studio livré (ton service compilé) s'ouvre (`1470_00`), lancé avec une
  adresse locale où rien ne répond : **ton service n'a pas été appelé**.
- **L'Admin** : son installateur, en silencieux par-dessus la 1.46.1 : la mise à jour, une désinstallation (le dossier
  de ta clé intact), une réinstallation ; le kit du service toujours installé ; ses pages d'installation (`page-1` à
  `page-7`).
- **Aucun texte coupé** : un outil a mesuré chaque texte de chaque page de l'Admin (et ses onglets) à 1373 × 917 et
  1100 × 700 : de 65 et 83 textes coupés ou chevauchés au départ à 6 et 13, tous des cellules raccourcies avec
  « … » dont la bulle donne le texte entier.
- Corrigé en route : un caractère (⇅) que la police de Windows n'a pas ; la liste des fichiers du projet Visual Studio ;
  dans mon essai, le disque plein puis l'écran virtuel arrêté (la chaîne a été relancée sans rien reconstruire).

## 3. Installer

**Les Studios** : lance `API-IHM-Studio-Setup-1.47.0.exe` (dans le zip) par-dessus la 1.46.0. Tes projets, tes
réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.47.0.exe` | `09D089FEAB0DCD0EF2DD3411E595C1141B0FD043DA954C57AF03E44A0E40A9AF` | 63 767 131 octets |
| `API-Studio-1.47.0-portable.zip` | `0BD518F0204BF996A8333871FDBAC192036A33138C81E18D356B0AA554029605` | 250 fichiers |
| `IHM-Studio-1.47.0-portable.zip` | `1C5D573D9598D7406978BD1C3379AA1709B070263C04062E003B94CD1619F03D` | 251 fichiers |

**L'Admin (pour toi seul, envoyé à part)** : lance `XPG-License-Admin-Setup-1.47.0-EDITEUR.exe` par-dessus la 1.46.1 ;
le dossier de ta clé n'est pas touché. Ne le donne à personne : pour un nouvel admin, Administrateurs › Ajouter un
admin… lui envoie l'installateur publié dans Versions (sans ta clé).

| Fichier | SHA-256 | Taille |
|---|---|---|
| `XPG-License-Admin-Setup-1.47.0-EDITEUR.exe` | `718729D209DCFB88A14B23148196C5EAD52670852575D431843817304FCE0BE8` | 8 719 288 octets |

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows** : sous Linux et sous Wine. **Le vrai Stripe et le vrai Cloudflare n'ont pas
  été appelés** par les essais (des faux sur ce poste).
- **Ce qui est avant la 1.47** est reconstitué depuis les tables du service : un peu bas quand d'anciennes activations
  ou demandes ont été effacées. Les demandes acceptées ou refusées ne se distinguent qu'à partir de l'Admin 1.47.
- **Le globe** : la ville vient de Cloudflare pour chaque appel ; un PC derrière un réseau d'entreprise peut apparaître
  dans la ville de son fournisseur.
- **La fiche d'un admin** : pas de courriel ni de « Lui écrire… » (l'Admin ne garde le courriel d'aucun admin) ; le
  délai de réponse est donné en moyenne et médiane (pas de répartition). La comparaison des admins se fait avec la
  période d'avant, pas avec l'an dernier. **La dernière utilisation d'un droit** n'est connue que sur ton PC (le journal
  des admins y est). Un refus pour droit manquant n'est pas encore inscrit au journal (dis-moi si tu le veux).
- **Les clics sur les courbes** : une barre de plusieurs jours filtre « depuis le … » ; Échéances ouvre les licences
  actives. Pas encore d'« Écrire aux clients… ».
