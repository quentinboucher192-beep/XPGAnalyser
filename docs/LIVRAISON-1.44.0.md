# API Studio, IHM Studio et l'Admin 1.44.0 : plusieurs admins, leurs droits, le journal, les fichiers, les mises à jour, la sauvegarde de secours

Faite à partir de la maquette de l'Admin que tu as validée (« je valide toutes les maquettes » : planches 00 à 07), de la
planche 08 (les droits des admins : « le admin+ doit pouvoir autoriser ou non certaines fonctions aux autres admins,
il peut créer des profils templates genre 'Admin LV1', 'Admin LV2'… »), de ta demande de secours (« une sauvegarde des
licences doit se faire avec le contenu exact, plus qu'à importer et toutes les configurations seront restaurées »), de
« ajoute 'client supprimé' dans les historiques, journaux sur l'admin », et de l'image des sessions pour
Multi-utilisateur.

Elle se met à jour par-dessus la 1.43.0 (les Studios) et la 1.42.0 (l'Admin), et garde tes données. **Le service en
ligne change** : il faut le redéployer (`deployer.bat`), voir § 1.

## 0. En bref

**Dans l'Admin**

- **Plusieurs admins, et toi l'admin+** (Sécurité › Administrateurs) : un PC qui veut devenir admin le demande depuis
  son Admin (« Demander à devenir admin… ») ; tu vois sa demande avec son **empreinte** (6 groupes de 4 caractères) :
  vérifie-la avec lui (par téléphone), choisis **son profil**, accepte. Il reçoit la liste signée par ta clé, la clé
  de synchro de l'archive, et signe ensuite **avec sa propre clé** (elle ne quitte jamais son PC). Les Studios
  acceptent ce qu'il signe tant que ses droits le permettent ; s'il est retiré, **ce qu'il a déjà signé reste
  valable** (Retirer… peut aussi révoquer ses licences).
- **Ajouter un admin…** (3 étapes) : son nom ; comment lui faire parvenir l'Admin (**dans les notifications de son
  Studio** : l'installateur de l'Admin, vérifié avant d'être proposé ; ou **par un lien** valable 48 h, une fois) ;
  sa demande, son empreinte, son profil.
- **Les droits** (Administrateurs › Droits des admins) : 13 fonctions (créer, prolonger une licence, ajouter une
  option, révoquer, retirer un PC, les offres, les options, les annonces, les demandes, revoir un code, supprimer un
  client, exporter les clients, écrire aux clients). Tu crées des **profils** (« Admin LV1 », « Admin LV2 »…),
  tu les **copies vers un admin**, puis tu ajustes ses fonctions une par une (ses différences avec le profil se
  voient). Ce qui est vérifié par les Studios et le service est marqué : un admin ne peut pas contourner ses droits
  en modifiant son Admin. Un bouton dont le droit manque est grisé et dit pourquoi.
- **Le journal des admins** (l'admin+ seulement) : chaque action de chaque admin, **signée par sa clé** et
  **chaînée** à la précédente sur le service (une ligne effacée ou modifiée se voit : « Chaîne vérifiée »). Filtres
  par criticité (Info, Moyenne, Haute, Critique), par admin, par période ; **Criticité des actions…** : tu règles la
  criticité de chaque type d'action et celles qui te préviennent. « Client supprimé » et « client restauré » y sont
  (et dans l'Historique, le tableau de bord).
- **Les mises à jour** (Versions) : tu publies les **deux installateurs** (Studios et Admin) ; leur version est lue
  dans le fichier, elle doit être plus récente que la dernière publiée ; ils sont signés par ta clé. Tu vois qui est
  à jour.
- **Envoyer un fichier à un PC** : un message dans ses notifications (lisible par lui seul) avec des fichiers joints.
- **La solution de secours** (Paramètres de sécurité › Sauvegarde de secours) : **chaque jour**, une sauvegarde du
  contenu **exact** du service (toutes les licences, les activations, les demandes, la liste des admins, leurs
  profils et leurs droits, le journal, les versions publiées) et de ce poste (l'archive, les réglages, ta clé
  d'éditeur, la clé de synchro), **chiffrée par ta phrase de passe** (Argon2id + XChaCha20-Poly1305 ; sans la
  phrase, elle ne s'ouvre pas). **Restaurer une sauvegarde…** remet tout, sur ce PC ou un autre, et sur le même
  service ou **un autre** (le jour où Cloudflare casse : un nouveau service, vide, et tout revient). **Les adresses
  de secours** (Service en ligne) : les Studios essaient l'adresse suivante si la première ne répond plus.

**Dans les Studios**

- **Les mises à jour arrivent seules** : quand tu publies une version, une notification « Mise à jour disponible »
  (ses nouveautés) et une fenêtre le disent. **Télécharger et installer** vérifie le fichier (signé par toi, son
  empreinte), enregistre le projet, installe et rouvre le Studio dessus ; **Plus tard** : une pastille en haut le
  rappelle. Sur le PC d'un admin, **Tout installer** met aussi l'Admin à jour.
- **Les fichiers de l'éditeur** dans les notifications (le filtre « Avec fichiers ») : vérifiés avant d'être
  proposés, refusés s'ils ne correspondent pas.
- **Les sessions par Internet** : Démarrer une session… › Où : **Internet** donne un code (« K7Q4-M2XP-T9 ») à
  transmettre ; **Rejoindre une session…** (aussi **depuis l'accueil**, sans projet ouvert) › Par Internet, ce code.
  Le service fait passer les échanges **sans pouvoir les lire** (chiffrés de bout en bout). Sur le réseau du bureau,
  les sessions trouvées sont listées ; le code n'est demandé que si la session l'exige.
- **La fenêtre Licence** : une offre choisie dans « Demander ma licence » montre aussi **ce qu'elle n'a pas, barré**
  (« 18/21 ») ; une fonctionnalité peut avoir **plusieurs captures** (les flèches) avec **des bulles au survol** qui
  disent ce qu'on y voit ; **Multi-utilisateur** montre les sessions.
- **Les onglets lisibles** : quand la place manque, chaque onglet garde le début de son titre (« Fonctionn… »),
  jamais son icône seule ; des flèches seulement en dernier recours.
- **Les notifications lues le restent** (après « Tout marquer comme lu », d'un projet à l'autre, d'un lancement à
  l'autre). **Demander ma licence** : la barre de défilement ne passe plus sur le texte du haut.

## 1. Ce qu'il te reste à faire

1. **Redéployer le service** (`deployer.bat`, comme d'habitude) : il passe en 1.44.0 (ses données restent : la mise à
   jour de sa base ne fait qu'ajouter). Tant qu'il n'est pas redéployé, l'Admin 1.44 te le dit et les nouveautés en
   ligne attendent.
2. Installer **l'Admin 1.44.0** sur ton PC (tu es l'admin+ : tu as la clé de l'éditeur), puis les **Studios 1.44.0**.
3. **La sauvegarde de secours** : Paramètres de sécurité › Sauvegarde de secours : choisis le dossier (une clé USB, un
   disque à part, un dossier synchronisé) et **ta phrase de passe** (toi seul la connais ; sans elle, la sauvegarde ne
   s'ouvre pas). Elle se fait ensuite chaque jour.
4. Pour ajouter un admin : Administrateurs › Ajouter un admin…

## 2. Les essais

@@ESSAIS@@

## 3. Installer

@@EMPREINTES@@

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows, ni sur plusieurs vrais PC** : sous Linux (le vrai service lancé sur ce
  poste) et sous Wine (deux Admin et deux IHM Studio sur ce poste, chacun ses dossiers). Le vrai Cloudflare, un vrai
  réseau entre deux bureaux (la session par Internet) et l'installation réelle d'une mise à jour restent à voir chez toi.
- **Redéploie le service avant tout le reste** : tant qu'il est en 1.43, l'Admin 1.44 te le dit et les nouveautés en
  ligne (les admins, le journal, les fichiers, les versions, le relais des sessions, la sauvegarde complète) attendent.
- **Les Studios 1.43 et avant** ne connaissent pas les admins : ils refusent une licence signée par un admin (pas par
  toi) et un registre qui porte des adresses de secours. Mets les Studios à jour (la 1.44 les avertira seule des
  suivantes) avant d'ajouter un admin ou une adresse de secours.
- **Un admin = un PC** : sa clé ne quitte jamais son PC. Nouveau PC : tu le retires, puis tu l'ajoutes à nouveau (une
  nouvelle clé, une nouvelle empreinte à vérifier).
- **La sauvegarde de secours ne contient pas** le mot de passe du courriel (SMTP) ni le jeton d'Outlook : après une
  restauration sur un autre PC, tu les retapes (Service en ligne › Le mail au client). Sans ta phrase de passe, une sauvegarde ne
  s'ouvre pas : personne (ni moi) ne peut la retrouver. Les fichiers envoyés (installateurs, pièces jointes) ne sont pas
  dans la sauvegarde : après une restauration, l'Admin liste ceux à renvoyer.
- **Les adresses de secours** : c'est à toi de déployer le second service (un autre compte Cloudflare, ou ailleurs) et
  d'y restaurer la sauvegarde ; les Studios ne font que basculer vers l'adresse suivante quand la première ne répond plus.
- **La session par Internet** passe par le service (le relais) : il ne lit rien (chiffré de bout en bout), mais il faut
  que les deux PC le joignent. 8 personnes au plus, comme sur le réseau du bureau.
- **Les mises à jour** : le Studio installe en silencieux puis se rouvre ; s'il faut les droits d'administrateur de
  Windows, Windows les demande (UAC). Sous Wine, l'essai a lancé un faux installateur (il note ses arguments) : la vraie
  installation par-dessus est celle de la chaîne (§ 2).
