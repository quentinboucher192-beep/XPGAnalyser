# 1.52.0 : XPG Admin ne montre que la gestion ; la dérogation donne tout, elle commence quand l'admin tape le code que tu vois sur ton PC, pour la durée que tu choisis

Fait à partir de ta demande du 14/11 : « l'admin classique ne doit pas voir les onglets 'paramètres de sécurité',
'services', 'administrateurs' en gros il ne peut faire que de la gestion, lorsqu'il a une dérogation il voit tout comme
l'admin+, quand il a une dérog il reçoit une notif et il doit mettre un code que l'admin+ voit sur son pc, et ça reste
actif pdt une durée choisissable par l'admin+ lorsqu'il l'envoie ».

## 0. En bref

**XPG Admin sans dérogation : la gestion seulement**

- Le menu de XPG Admin n'a plus que : Tableau de bord, Statistiques, Licences, Clients, Activations, Offres, Options,
  Fonctionnalités, Licences flottantes, Boutique, Commandes et demandes, Mon compte, Révocations, Historique.
- **Cachés** : Paramètres de sécurité, Service en ligne, Administrateurs (et, comme avant, Journal des admins, Droits,
  Versions). Un lien vers l'une de ces pages (une autre page, `--page`) ne l'ouvre pas et le dit : « « Service en ligne »
  s'ouvre avec une dérogation de l'admin+. ». Un groupe du menu sans page visible perd son titre.
- **Rien n'est perdu pour l'admin**, ce qu'il employait dans ces pages est passé dans **Mon compte** :
  - **son code d'invitation** (la carte « Ton code d'invitation » : le taper, le retaper après un refus ; l'installateur
    l'y dépose sans l'afficher) et **« Ton ajout comme admin »** avec « Vérifier maintenant » ;
  - **Mon compte › Ce PC** (nouveau) : la connexion au service (« Utiliser une connexion… »), son adresse et
    « Vérifier », l'archive (la clé de synchro reçue, Synchroniser, Recevoir seulement), le mail aux clients. Rien qui
    gère le service n'y est (ni Déployer, ni Certifier, ni les adresses de secours).
  - La pastille de la clé, celle du service, la carte « Le service » du menu et les boutons du tableau de bord y mènent ;
    chaque message qui disait « (Administrateurs) » dit maintenant « (Mon compte) ».
- **Ton Admin+ ne change pas** : tu vois tout, comme avant.

**La dérogation : tout, à partir du code**

- **Tu l'envoies** (Service en ligne › « Accorder une dérogation… », ou la fiche de l'admin › « Dérogation… ») : l'admin,
  puis **la durée** — 30 min, 1 h, 4 h, 24 h, 72 h ou **« Autre durée »** (heures et minutes, de 10 min à 72 h ; jamais
  permanente). « Elle commence quand il tape le code. Le code est valable 1 h. »
- **Tu vois le code sur ton PC** : 8 chiffres, en grand, avec Copier, et « Donne-lui ce code (de vive voix, au
  téléphone) : il le tape dans XPG Admin. Valable jusqu'à 10:58. ». Tant qu'il ne l'a pas tapé, ton Admin+ le remontre
  (Service en ligne, sa fiche dans Administrateurs, Mon compte). Le code n'est gardé nulle part : ton Admin+ le
  recalcule avec ta clé ; le service n'en a qu'une empreinte, qu'il ne rend jamais, et il n'est dans aucun journal.
- **Il est prévenu** (au plus 15 s après, XPG Admin ouvert ; sinon à son prochain lancement) : un avis « L'admin+ t'a
  envoyé une dérogation (2 h 30) : tape le code qu'il voit sur son PC. », **la fenêtre du code qui s'ouvre seule**, la
  pastille ambre « Dérogation reçue : taper le code » en haut, la carte dans Mon compte ; sous Windows, le bouton de la
  barre des tâches clignote.
- **Il tape le code** : un faux → « Code faux : il te reste 4 essais. » ; **au 5e, la dérogation est annulée** ; passé
  1 h, elle l'est aussi (« Le code n'est plus valable (il se tape dans l'heure qui suit l'envoi) : demande à l'admin+
  une nouvelle dérogation. »).
- **Le bon code** : « Dérogation active : tu vois tout comme l'admin+ pendant 2 h 30 (jusqu'à 12:30). » **La durée
  part de là.** Son menu devient le tien à l'instant, la pastille compte à rebours (« Dérogation : 2 h 29 »). Toi :
  « Léa Essai a tapé le code : sa dérogation court jusqu'à 12:30. » et l'alerte du journal.
- **Ce qu'il peut faire** : tout ce que le service arbitre — gérer le service (comme la dérogation d'avant), lire le
  Journal des admins et les Versions, voir les Droits, Administrateurs, Paramètres de sécurité. **Ce qui doit être signé
  par ta clé d'éditeur reste à toi**, les boutons sont gris et disent « Signé par la clé de l'éditeur : l'admin+ seul,
  même sous dérogation. » : ajouter / retirer un admin, les droits et profils, publier une version, envoyer un fichier
  ou un message, certifier, accorder une dérogation, Stripe et rembourser, la sauvegarde complète et la restauration,
  repartir de zéro. Ta clé ne quitte jamais ton PC.
- **La fin** (la durée écoulée, même hors ligne, ou « Révoquer ») : XPG Admin revient seul à la gestion ; s'il était sur
  une page de la dérogation, il retourne sur Mon compte (« Ta dérogation a pris fin : retour à la gestion. » ou
  « révoquée par l'admin+ »). Une seule dérogation à la fois (une nouvelle remplace l'ancienne) ; tu peux révoquer
  aussi une dérogation qui attend son code.
- **Le journal des admins** : « accordée » (avec la durée), « code refusé » (chaque essai faux), « activée » (Critique :
  tu es prévenu), « annulée », « utilisée » (une ligne par geste ; une lecture, une seule ligne par page), « révoquée »,
  « finie ».
- **La console** : `derogation-accorder --cle … --duree 2h30` (ou `--minutes`, `--heures`) affiche le code sur ton
  écran ; `derogation-etat` le remontre tant qu'il attend.

## 1. Ce qu'il te reste à faire

1. Installer **l'Admin+ 1.52.0** (par-dessus la 1.51.0), puis les **Studios 1.52.0**.
2. **Service en ligne › Déployer le service** : **à faire**, le service a changé (la dérogation par code, les lectures
   qu'elle ouvre). Tant qu'il n'est pas redéployé, envoyer une dérogation 1.52.0 est refusé par le service.
3. **Versions** : publier la 1.52.0 avec ses trois fichiers (Studios, Admin+, XPG Admin). **Tes admins doivent passer
   à XPG Admin 1.52.0** : c'est lui qui cache les pages (un XPG Admin 1.51.0 les montre encore, en lecture ; le service,
   lui, refuse déjà tout ce qui ne leur est pas permis).
4. Pour une dérogation : l'envoyer, puis lui **dire le code de vive voix** (téléphone, en personne) — jamais par mail.

## 2. Les essais

@@ESSAIS@@

## 3. Installer

@@INSTALLER@@

## 4. Limites

- **Sous dérogation**, ce qui doit être signé par ta clé d'éditeur reste à toi (la liste au § 0) : l'admin le voit,
  grisé, avec la raison. Si tu veux qu'il puisse aussi le faire (par exemple : il prépare, ton Admin+ ouvert signe
  seul), dis-le-moi.
- **XPG Admin 1.51.0** (pas encore mis à jour) montre encore les pages cachées, en lecture, et ne sait pas recevoir une
  dérogation par code : publie la 1.52.0 (§ 1).
- **`--page` d'une page cachée au lancement** est refusé même avec une dérogation : XPG Admin ne la connaît qu'au
  premier regard sur le service (15 s) ; ensuite le menu l'ouvre.
- **Le clignotement de la barre des tâches** n'a pas d'effet visible sous Wine : non vérifié.
- **Le code est valable 1 h** : passé ce temps, envoie une nouvelle dérogation.
- **Rien sur un vrai Windows** : sous Wine seulement.
