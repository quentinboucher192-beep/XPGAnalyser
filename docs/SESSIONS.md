# Les sessions : travailler à plusieurs sur le même projet (1.43.0, réseau du bureau)

La maquette validée est `docs/maquettes/1.42.0/` (planches 07 à 11). Ce document dit comment c'est fait.

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
| Le code | Avec « Il faut le code » : 6 chiffres, nouveaux à chaque session ; **jamais transmis**, même haché : un échange CPace (celui de l'appairage du déploiement, lié aux deux empreintes TLS) prouve que les deux le connaissent. Un intermédiaire qui présente ses propres certificats fait échouer l'échange sans rien apprendre du code. 3 essais par équipier. |
| Les trames | Celles du déploiement : longueur, en-tête JSON, données ; 2 Mio au plus (le projet part en morceaux de 1 Mio). |

## 4. Les messages

| De → à | `op` | Ce qu'il porte |
|---|---|---|
| équipier → hôte | `BONJOUR` | son nom (Mon profil), son PC, son identifiant, son logiciel et sa version ; avec un code : l'échange CPace (`CODE_DEBUT` / `CODE_REPONSE` / `CODE_FIN`) |
| hôte → équipier | `ATTENTE`, `ENTREE`, `REFUS` | en attente de l'hôte ; accepté (son numéro de membre, sa couleur, son rôle, son bloc d'identifiants, les membres) ; refusé (la raison) |
| hôte → équipier | `PROJET` ×n, `PROJET_FIN` | les fichiers du projet (`ihm/…`), en morceaux |
| les deux | `VUE` | les changements d'une vue : ses objets changés (leur texte, comme dans le fichier de la vue), ceux supprimés, l'ordre de dessin, la vue elle-même si ses propriétés ont changé |
| les deux | `FICHIER` | un fichier du projet hors des vues (les variables, les scripts, les réglages…), entier |
| les deux | `PRESENCE` | la vue où il est, ses objets choisis |
| équipier → hôte | `RESERVE` | les objets qu'il choisit ; l'hôte répond par `RESERVATIONS` (qui tient quoi, pour tous) |
| les deux | `MESSAGE` | la discussion |
| hôte → tous | `MEMBRES` | qui est là, où, avec quel rôle |
| les deux | `AUREVOIR` | il part ; l'hôte arrête : la session se ferme pour tous |

**L'hôte fait foi** : un changement d'un équipier lui arrive d'abord ; il le refuse s'il touche un objet réservé par un autre (ou si l'équipier ne peut que regarder) et renvoie alors l'état juste ; sinon il l'applique et le renvoie à tous, dans l'ordre. Chacun applique les changements des autres **objet par objet** : deux équipiers sur deux objets de la même vue ne s'écrasent pas.

**Les identifiants** : chaque membre crée ses objets (et ses vues, ses variables…) dans **son bloc** d'identifiants (le membre n, à partir de n × 2^28) : deux objets créés en même temps sur deux PC n'ont jamais le même.

## 5. Ce qui reste local

- La pile d'annulation : Ctrl+Z annule ses gestes (le changement part aux autres comme un geste de plus).
- L'enregistrement : seul l'hôte enregistre le projet. À la fin, un équipier peut **garder une copie** (Enregistrer sous).
- La simulation, les essais, le poste d'exploitation : chacun les siens.
