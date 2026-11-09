# API Studio, IHM Studio et l'Admin 1.45.0 : retirer quelqu'un d'une session, partir, l'onglet Session, le journal, les licences de ce PC, Déployer le service

Faite à partir de tes retours sur la 1.44 : « ajoute un bouton pour déployer dans 'service en ligne' », « pouvoir virer
qlq dans une session », « on ne voit pas les gens dans l'explorateur d'application », « à côté de sorties,
performances, erreur etc, ajouter 'chat de session' que quand une session est active », « dans demander ma licence,
il faut voir toutes les licences actives sur le poste, avec un bouton libérer/regarder », « quand je zoom sur les
images d'exemple je vois pas les tooltips », « il faut faire plusieurs images pour chaque fonctionnalité », « les
images zoomées sont pixelisées », « il doit appuyer 2 fois sur la croix pour fermer le modal », « où se situe
l'historique dans les sessions des choses modifiées par tout le monde », « quand quelqu'un quitte la session… si
c'est l'hôte ça coupe tout le monde », « quand elle part les données de l'appli ne sont pas sauvegardées sur son PC »,
et du message « cette adresse n'existe pas sur le service des licences » (ton service était encore en 1.43).

Elle se met à jour par-dessus la 1.44.0 (les Studios et l'Admin) et garde tes données. **Le service en ligne ne change
pas** (il reste en 1.44.0) : s'il est déjà redéployé, rien à faire ; sinon, l'Admin te le dit maintenant et le
redéploie d'un bouton (§ 1).

## 0. En bref

**Dans les Studios : les sessions**

- **Retirer quelqu'un** : dans le panneau Session, chaque équipier a « Retirer de la session… » (avec une question).
  Il est coupé, revient à l'accueil (« Camille t'a retiré de la session »), et les autres le lisent. S'il frappe de
  nouveau, **même avec le code, c'est toi qui décides** (« Léa (retirée tout à l'heure) veut revenir »).
- **Partir** : un équipier qui part (Quitter la session, fermer le projet ou le Studio, une coupure) **revient à
  l'accueil**, et les autres sont prévenus (« Léa a quitté la session », dans la discussion, la barre d'état et les
  notifications). Quand **l'hôte arrête** (le bouton, fermer le projet ou le Studio), **tout le monde revient à
  l'accueil** (« Camille a arrêté la session ») ; l'hôte garde son projet.
- **Rien ne reste chez l'équipier** : sa copie du projet (reçue de l'hôte) est **fermée et effacée** dès qu'il part ;
  elle n'entre pas dans ses projets récents, n'a pas de copie de secours ; ce qu'un plantage aurait laissé est effacé
  au lancement suivant. Pendant la session, Enregistrer sous, Dupliquer, Exporter l'archive ou les sources, Ouvrir
  le dossier du projet… sont refusés (grisés : « En session, le projet reste chez l'hôte »).
- **L'onglet Session** : à côté de Sorties, Console, Diagnostics, Performances, un onglet **« Session »** apparaît
  **seulement pendant une session** : la discussion et le journal mêlés dans l'ordre (filtres Tout, Discussion,
  Journal), un message avec Entrée, une pastille pour les messages non lus.
- **Le journal de la session** (l'historique de ce que chacun a changé) : « 23:29 Léa : Vue_Accueil › Texte_Etat
  déplacé » - les objets (ajoutés, déplacés, redimensionnés, renommés, modifiés, supprimés), les vues, les variables,
  les scripts, les fonctions, **les ressources**. L'hôte le tient et l'envoie à tous (celui qui arrive en retard le
  reçoit en entier). **À la fin, il est enregistré avec le projet** : `ihm/historique/sessions/` (un fichier CSV par
  session, qui s'ouvre dans Excel) ; l'hôte peut aussi l'exporter pendant la session (Exporter le journal).
- **Les gens dans l'explorateur** : pendant une session, l'arbre du projet commence par **« Session »** : chaque membre
  (ses initiales dans sa couleur, hôte ou équipier, son PC, sa vue, Modifier ou Regarder) ; un clic le suit. Dans
  l'explorateur d'objets de la vue, un objet tenu par quelqu'un montre ses initiales (« tenu par Léa »).
- **Les ressources** (ta question) : **oui**, un équipier peut ajouter des fichiers aux Ressources ; ils arrivent chez
  l'hôte et chez les autres à l'identique (vérifiés par leur empreinte), jusqu'à 32 Mo par fichier par Internet ; le
  journal le note (« ressource logo.png ajoutée »).
- **Un service pas à jour** : si le service des licences n'est pas encore en 1.44, Démarrer ou Rejoindre une session
  par Internet le dit (« Le service des licences n'est pas encore en 1.44 : l'éditeur doit le redéployer (Admin ›
  Service en ligne › Déployer) ») au lieu de « cette adresse n'existe pas ».

**Dans les Studios : la fenêtre Licence**

- **Les licences de ce PC** : Demander ma licence montre d'abord les licences actives sur ce PC, avec **Regarder**
  (l'onglet qui la montre) et **Libérer** (rendre la place de ce PC, après une question qui dit ce qui se passera).
- **Les images d'exemple** : **2 ou 3 par fonctionnalité**, refaites **en pleine résolution** (de 1000 à 1600 pixels de
  large, sans réduction : nettes une fois agrandies). En grand : les flèches (ou ← →) passent de l'une à l'autre,
  avec leur légende et **leurs bulles au survol** (elles manquaient dans le zoom).
- **Une seule fenêtre Licence** : quand une licence se valide, la notification ne rouvre plus la fenêtre par-dessus
  celle qui est ouverte (**une seule croix** pour fermer) ; redemandée, elle se relit et montre l'onglet voulu.

**Dans l'Admin**

- **Déployer le service** (Service en ligne, et le tableau de bord quand il le faut) : un bouton qui lance le kit du
  service (installé avec l'Admin) sans fenêtre noire, montre ce qui se passe étape par étape (Node.js, wrangler, l'envoi
  à Cloudflare, la vérification), cache les secrets, puis relit la version en ligne. S'il manque quelque chose, il dit
  quoi faire : installer Node.js (le bouton ouvre nodejs.org), **Se connecter à Cloudflare…** (la première fois).
- **Le service pas à jour** : l'Admin compare la version de ton service en ligne à celle qu'il lui faut (1.44.0) ; s'il
  est plus ancien, **un avis au lancement, la pastille du haut (« à mettre à jour »), un bandeau** dans Service en
  ligne et le tableau de bord, avec le bouton Déployer. Attention : ton service actuel, s'il n'a pas été redéployé
  depuis la 1.43, s'annonce en **« 1.40.0 »** (le numéro qu'il portait jusque-là).
- En console : `XPG-License-Admin service-deployer` (avec `--essai` : tout sauf l'envoi à Cloudflare).

## 1. Ce qu'il te reste à faire

1. Installer **l'Admin 1.45.0** sur ton PC (par-dessus la 1.44.0), puis les **Studios 1.45.0**.
2. Si l'Admin te dit que le service est à mettre à jour : **Service en ligne › Déployer le service**. La première fois
   sur ce PC, il peut te demander d'installer Node.js (LTS) puis de te connecter à Cloudflare (ton navigateur s'ouvre :
   tu acceptes) ; ensuite c'est un clic. `deployer.bat` marche toujours, comme avant.
3. Une fois le service en 1.44.0, les sessions par Internet s'ouvrent.

## 2. Les essais

- **Linux : 171 suites sur 171 réussies** (`ctest`, sur la version livrée). Celles que cette version ajoute ou touche :
  - `session` (168 contrôles : les phrases du journal, le regroupement des gestes, la limite de 2000, le fichier
    enregistré ; la copie effacée et le nettoyage au lancement ; **de vrais liens chiffrés sur ce poste** : retirer
    quelqu'un (la raison, les autres prévenus, son retour qui attend l'hôte), un équipier qui part, l'hôte qui arrête ;
    **une image ajoutée aux Ressources par un équipier, arrivée à l'identique** chez l'hôte et chez un autre équipier ;
    le service d'avant la 1.44, à l'ouverture et pour rejoindre), `sessionrelais`, `hmieditor` (l'onglet Session :
    seulement pendant une session, les filtres, l'envoi, la pastille ; le nœud Session de l'arbre et Suivre ; « tenu
    par Léa » ; les actions refusées à l'équipier), `licenceui` (les licences de ce PC, Regarder et Libérer, une seule
    fenêtre Licence, les captures en grand avec leurs flèches et leurs bulles), `admindeploy` (122 contrôles : la
    version comparée à celle qu'il faut, la sortie du kit nettoyée et ses secrets cachés, chaque échec et ce qu'il dit,
    l'annulation, un seul déploiement à la fois, la connexion à Cloudflare, le bandeau et la pastille avec un service en
    1.43, le dialogue avec un faux déployeur).
- **Sous Wine, de bout en bout** : les programmes Windows (l'Admin installé par son installateur, deux IHM Studio côte
  à côte), le **vrai service** lancé sur ce poste (`workerd`) et, pour le message, **le vrai code du service d'avant la
  1.44** lancé à côté. Journal : `essai-1.45.0-wine.txt` (codes masqués) ; captures : `captures-1.45.0.zip`. Les Studios
  sont des copies avec la clé d'essai, jamais livrées.
  - **Les licences de ce PC** (`1450_01`, `1450_12` chez Léa) : la licence ENTERPRISE de l'essai, Regarder, Libérer.
    **Une capture en grand avec sa bulle** (`1450_02`), puis la suivante avec la flèche (`1450_03`, nette).
  - **La session par Internet** (Camille l'hôte, Léa entre avec le code) : **l'onglet Session** chez les deux
    (`1450_04`, `1450_04b`), **l'équipe en tête de l'arbre** et **Texte_Etat « tenu par Léa »** (`1450_05`) ; Léa déplace
    le texte : le journal le note chez tous (« 23:33 Léa Essai : Vue_Accueil › Texte_Etat déplacé »).
  - **Camille retire Léa** : Léa revient à l'accueil (`1450_07` : « Camille Essai a retiré Léa Essai de la session »),
    **sa copie du projet est effacée** ; Camille le lit (`1450_06`). Léa frappe de nouveau avec le code : **elle attend**
    (« retirée tout à l'heure, veut revenir : à toi de décider », `1450_06b`) ; Camille l'accepte.
  - **Léa part d'elle-même** : elle revient à l'accueil (`1450_09`), sa copie effacée ; Camille le lit (« Léa Essai a
    quitté la session », `1450_08`).
  - **Léa revient, Camille arrête** : Léa revient à l'accueil (`1450_11`), sa copie effacée ; chez Camille, « La session
    est arrêtée… Son journal : ihm/historique/sessions/… » (`1450_10`) : le fichier
    `2026-11-09_23h32_Mise en service Armoire_Gaz.csv` est dans son projet. Sur le PC de Léa, après la session : **plus
    rien du projet** (le dossier des sessions est vide).
  - **Le service d'avant la 1.44** (son vrai code, lancé à côté ; il s'annonce « 1.40.0 », le relais répond 404) : un
    Studio neuf s'y active (une licence ENTERPRISE déposée par l'Admin 1.45), ouvre le projet, et **Démarrer une session
    par Internet dit « Le service des licences n'est pas encore en 1.44 : l'éditeur doit le redéployer (Admin › Service
    en ligne › Déployer) »** (`1450_13`). L'Admin, relié à ce service : la pastille « à mettre à jour », **le bandeau
    « Ton service en ligne est en 1.40.0 : déploie la 1.44.0… »** au tableau de bord et dans Service en ligne (`1450_20`,
    `1450_21`) ; **Déployer le service** ouvre le dialogue, qui s'arrête à l'étape 1 : « Node.js n'est pas installé sur
    ce PC » avec **Ouvrir nodejs.org** (`1450_22`) ; rien n'est parti vers Cloudflare. En console, `service-deployer
    --essai` trouve le kit installé avec l'Admin (service 1.44.0) et dit la même chose.
  - **Le service à jour** (1.44.0) : plus de bandeau, la pastille « en direct » (`1450_23`, `1450_24`).
- **Les Studios** : l'installation en silencieux par-dessus la 1.44.0 passe, ta clé d'API est gardée, les programmes
  installés sont ceux construits (1.45.0). Le Studio livré (ton service compilé) s'ouvre (`1450_00`), lancé avec une
  adresse locale où rien ne répond : **ton service n'a pas été appelé**.
- **L'Admin** : son installateur, en silencieux par-dessus la 1.44.0 : les programmes et le kit du service identiques à
  ceux construits, une mise à jour, une désinstallation (le dossier de ta clé intact), une réinstallation ; ses pages
  d'installation (`page-1` à `page-7`).
- **Les images d'exemple** : 54 images pour les 21 fonctionnalités (2 ou 3 chacune), toutes vérifiées à l'œil avec leurs
  bulles ; celles de Multi-utilisateur viennent de cet essai (`1450_04`, `1450_04b`, `1450_06b`).
- Corrigé en route : rien dans les programmes ; mon script d'essai, lui, arrêtait la session trop tôt au premier
  passage (il attendait « au moins 1 personne »), et activait la licence du Studio C sur le nouveau service avant de le
  brancher sur l'ancien.

## 3. Installer

**Les Studios** : lance `API-IHM-Studio-Setup-1.45.0.exe` (dans le zip) par-dessus la 1.44.0. Tes projets, tes
réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.45.0.exe` | `5FAC2DB0F7B1AF33FAA2B4CBFB61FB4C3664F7C900BF18B00AA963ABFA024466` | 63 417 526 octets |
| `API-Studio-1.45.0-portable.zip` | `A018B4C184C6467D1A8BCC5FD544C7FDB593086B1EF50815AE426EA3E119C301` | 250 fichiers |
| `IHM-Studio-1.45.0-portable.zip` | `E1C2CF931374497EB01DFC92894A0EC34F81FF8792F27A33C7BA0FB4AE222E37` | 251 fichiers |

**L'Admin (pour toi seul, envoyé à part)** : lance `XPG-License-Admin-Setup-1.45.0-EDITEUR.exe` par-dessus la 1.44.0 ;
le dossier de ta clé n'est pas touché. Ne le donne à personne : pour un nouvel admin, Administrateurs › Ajouter un
admin… lui envoie l'installateur publié dans Versions (sans ta clé).

| Fichier | SHA-256 | Taille |
|---|---|---|
| `XPG-License-Admin-Setup-1.45.0-EDITEUR.exe` | `F03B38DC08AFE63D0E0E6AFA86FCDF4F5CE26B9C1AF97C1563FBBD8D23E8B2E7` | 7 218 227 octets |

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows, ni sur plusieurs vrais PC** : sous Linux et sous Wine (deux IHM Studio et
  deux services lancés sur ce poste). Une session entre deux vrais bureaux, et **le vrai déploiement vers Cloudflare
  depuis le bouton** (Node.js, la connexion à ton compte, l'envoi) restent à voir chez toi : sous Wine il n'y a pas de
  Node.js, le dialogue s'est arrêté à « Node.js n'est pas installé » (§ 2).
- **Retirer quelqu'un** le coupe et l'empêche de rentrer seul **pendant cette session** (tu décides s'il frappe) ; ce
  n'est pas une interdiction pour les sessions suivantes.
- **Le journal de la session** est tenu par l'hôte : un équipier le voit et le reçoit, mais seul l'hôte l'exporte et
  l'enregistre (avec le projet). Les arrivées et les départs sont dans la discussion, pas dans le journal. Il garde les
  2000 derniers changements ; un même geste répété (un glisser) ne fait qu'une ligne (×N).
- **La copie de l'équipier effacée** : en partant, il envoie d'abord à l'hôte ce qui restait (Quitter, fermer) ;
  seule une coupure brutale (le réseau, un plantage) peut perdre ses tout derniers gestes. Le projet, lui, reste entier
  chez l'hôte.
- **Les Studios 1.44 et avant** dans une session 1.45 : ils marchent, mais sans l'onglet Session ni le journal (ils
  ignorent ces nouveaux messages) ; mets tout le monde en 1.45.
