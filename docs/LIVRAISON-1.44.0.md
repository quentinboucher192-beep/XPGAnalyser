# API Studio, IHM Studio et l'Admin 1.44.0 : plusieurs admins, leurs droits, le journal, les fichiers, les mises à jour, la sauvegarde de secours

Faite à partir de la maquette de l'Admin que tu as validée (« je valide toutes les maquettes » : planches 00 à 07), de la
planche 08 (les droits des admins : « le admin+ doit pouvoir autoriser ou non certaines fonctions aux autres admins,
il peut créer des profils templates genre 'Admin LV1', 'Admin LV2'… »), de ta demande de secours (« une sauvegarde des
licences doit se faire avec le contenu exact, plus qu'à importer et toutes les configurations seront restaurées »), de
« ajoute 'client supprimé' dans les historiques, journaux sur l'admin », et de l'image des sessions pour
Multi-utilisateur.

Elle se met à jour par-dessus la 1.43.0 (les Studios) et la 1.42.0 (l'Admin), et garde tes données. Elle remplace la
livraison intermédiaire de cet après-midi (même numéro : installe simplement celle-ci par-dessus). **Le service en
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

- **Linux : 170 suites sur 170 réussies** (`ctest`, sur la version livrée). Celles que cette version ajoute ou touche :
  - `admins` (le cœur : la liste des admins signée par ta clé, les droits, ce qui reste valable après un retrait,
    le journal chaîné), `adminsadmin` (contre le vrai service lancé sur ce poste : la demande d'un admin, son
    empreinte, l'acceptation avec un profil, une licence signée par l'admin installée chez un client, ses droits
    refusés par le service, le retrait), `adminsfichiers` (un fichier de 3,5 Mio envoyé par morceaux et repris
    après une coupure, un lien à usage unique, un message lisible par un seul PC, une version publiée et une plus
    ancienne refusée, **une sauvegarde restaurée sur un second service vide : les mêmes licences, la même liste des
    admins, le même journal**), `service_fichiers` (les fichiers et les mises à jour côté Studio), `sessionrelais` (la
    session par Internet : le code, le relais qui ne lit rien), `adminguiadmins` et `adminguiversions` (les pages
    de l'Admin), et le service lui-même (`service_worker` : ses essais `npm test` contre `workerd`).
- **Sous Wine, de bout en bout** : les programmes Windows (l'Admin installé par son installateur, deux IHM Studio
  côte à côte), le **vrai service** lancé sur ce poste (`workerd`), deux Admin (deux dossiers : Camille, l'admin+
  avec la clé d'essai de l'éditeur ; Léa, un PC qui devient admin). Journal : `essai-1.44.0-wine.txt` (codes
  masqués) ; captures : `captures-1.44.0.zip`. Les Studios sont des copies avec la clé d'essai, jamais livrées.
  - **Léa demande à devenir admin** (son poste en attente, `1440_20`) ; Camille voit sa demande avec **la même
    empreinte** (`1440_21`, `1440_23` : Ajouter un admin en 3 étapes) et l'accepte avec le profil « Admin LV1 »
    (4 droits sur 13). Léa reçoit la liste, la clé de synchro, l'archive (le registre).
  - **Léa signe une licence en ligne ENTERPRISE avec sa clé d'admin** : les deux Studios s'activent avec son code,
    **« Valide / ENTERPRISE »** (`1440_02`) ; son archive arrive chez Camille.
  - **Camille publie la version 1.44.1** (deux faux installateurs de 1.44.1 : leur version lue dans le fichier) :
    le Studio la voit (la notification et la bulle, `1440_05`), **Plus tard** met la pastille en haut (`1440_06`),
    puis **Télécharger et installer** télécharge, vérifie (signé par l'éditeur, son empreinte), enregistre le projet
    et lance l'installateur en silencieux avec la relance du Studio sur le projet (le faux installateur a noté :
    `/SILENT … /RELANCER=…IHM-Studio.exe /RELANCERPROJET=…Armoire_Gaz`).
  - **Un message avec un fichier, puis l'installateur de l'Admin** envoyés par Camille au PC B : ils arrivent dans
    ses notifications (le filtre « Avec fichiers », `1440_09`) ; le fichier est **téléchargé et vérifié**.
  - **La session par Internet** : A démarre (Où : Internet, `1440_07`) et reçoit le code (`1440_08`) ; B, **depuis
    l'accueil** (sans projet ouvert), choisit « Par Internet » (`1440_10`) et entre (`1440_11`) ; la discussion
    passe dans les deux sens (`1440_12`). Le relais du service ne voit que des échanges chiffrés.
  - **Les notifications lues le restent** : Tout marquer comme lu, le projet fermé puis rouvert : 0 non lue (`1440_13`).
  - **La fenêtre Licence** : une capture de Multi-utilisateur et **sa bulle au survol** (`1440_03`) ; PROFESSIONAL
    choisie dans Demander ma licence : « 18/21 », ce qu'elle n'a pas **barré** (`1440_04`).
  - **Le journal des admins** relevé par Camille : chaque action, son admin, sa criticité (l'ajout de Léa et son
    retrait « Critique », sa licence « Moyenne », la version « Haute »), **« Chaîne vérifiée »** (`1440_25`, `1440_26`).
  - **La sauvegarde de secours** : réglée avec une phrase de passe (lue dans un fichier, jamais affichée ni
    écrite), une sauvegarde chiffrée (ni la phrase ni une licence en clair dedans) ; **restaurée sur un second
    service vide** : 20 tables, 52 lignes remises, puis le poste restauré relié au second service : la même licence (XPG-2026-00440), la même liste des admins (n° 1, Léa), le journal qui continue (n° 8 la sauvegarde, n° 9 la restauration, Critique) ; les mêmes licences, la même liste des admins ; une mauvaise phrase est refusée.
  - **Camille retire Léa** : sa clé ne peut plus rien faire sur le service (Léa le voit) ; la licence qu'elle a
    signée reste sur le service (active) ; le journal le note (Critique).
  - Les pages de l'Admin : Administrateurs (`1440_22`, `1440_32` : Léa retirée), Droits des admins (`1440_24`),
    Versions (`1440_27`), Sauvegarde de secours (`1440_28`), Service en ligne (`1440_29`), le tableau de bord
    (`1440_30`), l'Historique (`1440_31`), le poste de Léa admin (`1440_21b`).
- **Les Studios** : l'installation en silencieux par-dessus la 1.43.0 passe, ta clé d'API est gardée, les programmes
  installés sont ceux construits (1.44.0), le code laissé par `/CODEFICHIER=` n'est dans aucun journal. Le Studio livré
  (ton service compilé) s'ouvre (`1440_00`), lancé avec une adresse locale où rien ne répond : **ton service n'a pas
  été appelé**.
- **L'Admin** : son installateur, en silencieux par-dessus la 1.42.0 : les programmes identiques à ceux construits,
  le dossier de ta clé intact (avant, après une mise à jour, après une désinstallation), le menu Démarrer, les
  fichiers d'un double-clic ; ses pages (`page-1` à `page-7`).
- Corrigé en route : le texte tronqué coupait un caractère accentué en deux (« Service : non r▯… ») — 18 endroits,
  Admin et Studios ; les tableaux ne barraient pas ce qui n'est pas dans l'offre ou la licence (seules les listes le
  faisaient) ; « Léa est retiré » (le message ne s'accorde plus : « n'est plus dans la liste des admins ») ; en console,
  la synchronisation de l'archive chez l'admin+ refusait une licence signée par un admin ; « LES NOUVEAUTéS » dans la
  fenêtre de mise à jour ; le détail d'une ligne du journal des admins (« Cle », « Retire » et une date en secondes).

## 3. Installer

**Les Studios** : lance `API-IHM-Studio-Setup-1.44.0.exe` (dans le zip) par-dessus la 1.43.0 (ou l'intermédiaire). Tes
projets, tes réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.44.0.exe` | `58B0856EEADA752DB4F111857CBE5A5425449DEB5C4037C188F70091DED12F89` | 58 138 794 octets |
| `API-Studio-1.44.0-portable.zip` | `3263C761EB92B792FCD117E654FA0B5885F7061C209A8ED7D79B6AF0DF56904E` | 218 fichiers |
| `IHM-Studio-1.44.0-portable.zip` | `B941FCAD0DEB40EC690B85F0E269442B9CA60CA94850EC731D986D9871705D44` | 219 fichiers |

**L'Admin (pour toi seul, envoyé à part)** : lance `XPG-License-Admin-Setup-1.44.0-EDITEUR.exe` par-dessus la 1.42.0 ;
le dossier de ta clé n'est pas touché. Ne le donne à personne : pour un nouvel admin, Administrateurs › Ajouter un
admin… lui envoie l'installateur publié dans Versions (sans ta clé).

| Fichier | SHA-256 | Taille |
|---|---|---|
| `XPG-License-Admin-Setup-1.44.0-EDITEUR.exe` | `E818112B394875F367C89206C9ADB984841E46D9B46EF381248FA56B5F405DF7` | 7 149 366 octets |

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
