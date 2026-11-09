# Les sessions : travailler à plusieurs sur le même projet (1.43.0, réseau du bureau)

La maquette validée est `docs/maquettes/1.42.0/` (planches 07 à 11). Ce document dit comment c'est fait : `src/session/` (le protocole, le lien, les changements ; sans écran, `session_test`) et `src/app/SessionService`, `SessionDialogs`, `SessionMarks` (IHM Studio).

## 1. Ce que fait la 1.43.0 (et ce qui vient après)

- **IHM Studio, sur le réseau du bureau.** Un PC **démarre** une session sur le projet ouvert (il en est l'**hôte** : son PC garde le projet et l'enregistre) ; les autres la **rejoignent** : au bureau, elle apparaît seule dans « Rejoindre » ; l'hôte accepte chaque entrée, ou exige le code de la session.
- Le projet arrive une fois chez l'équipier (dans un dossier de session, à part de ses projets), puis **seulement les changements**, objet par objet.
- **Chacun voit ses équipiers** : leurs pastilles en haut, la vue où ils sont, les objets qu'ils ont **réservés** (contour à leur couleur, leur nom) ; « Suivre » ouvre la vue d'un équipier ; une discussion.
- **Chacun son objet** : un objet choisi par un équipier est réservé pour lui tant qu'il le garde choisi ; les autres ne peuvent pas le choisir (ils voient par qui il est pris).
- **Ctrl+Z annule ses propres gestes**, pas ceux des autres. **L'hôte enregistre** pour tout le monde ; **l'historique des objets** signe chaque objet par le PC de celui qui l'a modifié (son nom, son identifiant stable « M-7F21 », la session).
- **Plus tard** : API Studio en session (ses commandes d'édition annulent par troncature : à reprendre d'abord), Internet (par le service, chiffré de bout en bout), « Regarder seulement » changé en direct, la reprise après une coupure longue.

## 2. La licence

- **Démarrer** une session demande la fonctionnalité `multi_user` (« Travail à plusieurs ») : l'offre ENTERPRISE, ou l'option « Travail à plusieurs ».
- **Rejoindre** demande seulement sa propre licence d'IHM Studio.

## 3. Le réseau

| | |
|---|---|
| Découverte | UDP **47864**, en diffusion sur le réseau du bureau (`{"xpgs":1,"op":"CHERCHE","nonce":…}` → `{"xpgs":1,"op":"ICI",…}` : le nom de la session, l'hôte, son PC, le logiciel, le projet, le port, l'empreinte de son certificat, le nombre de membres, si un code est demandé). Ports à part de ceux du déploiement (47861, 47862) : un agent sur le même PC ne gêne pas. |
| Le lien | TCP **47863**, **TLS 1.3** (la bibliothèque du déploiement réseau : mbedTLS), les deux côtés présentent leur certificat (le certificat du Studio, `Deploiement/studio.cer`, créé une fois sur chaque PC). L'équipier épingle l'empreinte annoncée par l'hôte ; l'hôte voit celle de l'équipier (son identifiant « M-7F21 », dans la fenêtre « Léa veut entrer »). |
| Le code | Avec « Il faut le code » : 6 chiffres (montrés « 482 913 »), nouveaux à chaque session ; **jamais transmis**, même haché : un échange CPace (celui de l'appairage du déploiement, lié aux deux empreintes TLS) prouve que les deux le connaissent. L'échange est aussi lié à la session (SHA-256 de « XPG-SESSION-v1\|identifiant ») : un code de session ne sert ni à un appairage de déploiement, ni à une autre session. Un intermédiaire qui présente ses propres certificats fait échouer l'échange sans rien apprendre du code. 3 essais par équipier (son empreinte) : l'essai compte dès que l'hôte répond. |
| Les trames | Celles du déploiement : longueur, en-tête JSON, données ; 2 Mio au plus (le projet part en morceaux de 1 Mio). |

## 4. Les messages

| De → à | `op` | Ce qu'il porte |
|---|---|---|
| équipier → hôte | `BONJOUR` | son nom (Mon profil), son PC, son identifiant, son logiciel et sa version, l'identifiant de la session ; avec un code : `CODE_DEMANDE`, puis l'échange CPace (`CODE_DEBUT` / `CODE_REPONSE` / `CODE_FIN`) |
| hôte → équipier | `LIEN_OK` | la poignée de main est faite (le code prouvé s'il le fallait) : l'hôte décide maintenant |
| hôte → équipier | `ATTENTE`, `ENTREE`, `REFUS` | en attente de l'hôte ; accepté (son numéro de membre, sa couleur, son rôle, son bloc d'identifiants, les membres) ; refusé (la raison) |
| hôte → équipier | `PROJET` ×n, `PROJET_FIN` | les fichiers du projet (`ihm/…`), en morceaux |
| les deux | `VUE` | les changements d'une vue : ses objets changés (leur texte, comme dans le fichier de la vue), ceux supprimés, l'ordre de dessin, la vue elle-même si ses propriétés ont changé |
| les deux | `FICHIER` | un fichier du projet hors des vues (les variables, les scripts, les réglages…), entier |
| les deux | `PRESENCE` | la vue où il est, ses objets choisis |
| équipier → hôte | `RESERVE` | les objets qu'il choisit ; l'hôte répond par `RESERVATIONS` (qui tient quoi, pour tous) |
| les deux | `MESSAGE` | la discussion |
| hôte → tous | `MEMBRES` | qui est là, où, avec quel rôle |
| les deux | `AUREVOIR` | il part ; l'hôte arrête : la session se ferme pour tous |
| hôte → équipier | `REFUSE_CHANGEMENT` | un changement refusé (l'objet est pris, il regarde seulement) ; l'état juste est reparti avant (`VUE` ou `FICHIERS`) |
| hôte → tous | `ETAT` | toutes les 4 s, quand le projet a changé : l'empreinte (SHA-256) de chaque vue et de chaque fichier hors des vues |
| équipier → hôte | `RESYNC` | au calme (rien à envoyer, rien reçu depuis un moment), ce qui diffère de `ETAT` : l'hôte renvoie la vue entière ou le fichier (3 fois au plus par élément : un écart qui ne se rattrape pas ne boucle pas) |

**L'hôte fait foi** : un changement d'un équipier lui arrive d'abord ; il le refuse s'il touche un objet réservé par un autre (ou si l'équipier ne peut que regarder) et renvoie alors l'état juste ; sinon il l'applique et le renvoie à tous, dans l'ordre. Chacun applique les changements des autres **objet par objet** : deux équipiers sur deux objets de la même vue ne s'écrasent pas.

**Les identifiants** : chaque membre crée ses objets (et ses vues, ses variables…) dans **son bloc** d'identifiants (le membre n, à partir de n × 2^28 ; une session d'avant a pu y laisser des objets : le premier libre) : deux objets créés en même temps sur deux PC n'ont jamais le même. Le compteur reste à chacun : il ne passe pas dans les fichiers.

**Le reste du projet** (`ihm.txt`, les scripts, les ressources…) passe en **différences de lignes avec leur contexte** : deux équipiers qui ajoutent chacun une variable ne s'écrasent pas. Deux ajouts au même endroit peuvent se ranger dans un ordre différent chez l'un et l'autre : les empreintes le voient, l'équipier reprend le fichier de l'hôte. Une vue neuve passe entière avec l'index qui la cite ; une vue retirée par un équipier part dans la corbeille de l'hôte.

## 5. Ce qui reste local

- La pile d'annulation : Ctrl+Z annule ses gestes (le changement part aux autres comme un geste de plus). En session (`hmi::Document::shared`), annuler ne remet que les objets que le geste a touchés, par-dessus l'état courant : ce qu'un équipier a fait depuis reste ; le reste du projet est reporté ligne par ligne (`session::mergeShell`).
- L'enregistrement : seul l'hôte enregistre le projet. À la fin, un équipier peut **garder une copie** (Enregistrer sous).
- La simulation, les essais, le poste d'exploitation : chacun les siens.

## 6. Dans IHM Studio

- **Projet › Démarrer une session…** : le nom, qui peut entrer (j'accepte chaque entrée / il faut le code), ce qu'ils font en entrant (modifient / regardent seulement). Le projet est enregistré avant ; un projet verrouillé (LOCK) ne se partage pas. La licence : `multi_user` (la table des points d'entrée : l'action fermée propose l'option).
- **Projet › Rejoindre une session…** : celles du réseau du bureau (et de ce PC), ou l'adresse du PC de l'hôte ; le code si elle en demande un. Le projet arrive dans `<dossier commun>/Sessions/<identifiant>` (pas dans les projets récents).
- **La pastille « Session · 3 »** en haut (à côté de la licence ; orange quand quelqu'un frappe) : l'équipe (où chacun est, combien d'objets il tient, Suivre, Regarder seulement / Laisser modifier pour l'hôte), ceux qui veulent entrer (Faire entrer / Refuser), le code (Copier le code), la discussion, Arrêter / Quitter.
- **Les objets tenus** : un contour à la couleur de l'équipier et son nom au-dessus ; un objet tenu ne se choisit pas (la barre d'état dit par qui il est pris).
- **Enregistrer** : l'hôte ; chez l'équipier, Ctrl+S le rappelle. À la fin, le projet reste ouvert chez l'équipier (une copie) : Enregistrer sous… pour le garder.
- **Les essais** : `session_test` (le protocole, le miroir, Ctrl+Z en session, un vrai lien TLS sur la boucle locale) ; les commandes de script `session-*` ; l'essai de bout en bout sous Wine (deux IHM Studio, une licence ENTERPRISE d'essai). `XPG_DOSSIER_COMMUN` impose le dossier commun (deux Studios côte à côte, chacun le sien).
