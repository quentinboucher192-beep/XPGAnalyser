# API Studio et IHM Studio 1.12.24 : le Réglage PID, les registres, le plein écran qui remplit l'écran

Livrée le 22/10/2026. Elle suit la 1.12.23 du 21/10 (`docs/LIVRAISON-1.12.23.md`).

Elle répond à :

> « là tu as des objets dans la biblio vannes vannes 3 voies, ajoute des objets registres, dans equipements y'a un petit probleme, les simulés et les ports se chevauchent, teste en faisant plusieurs esclaves simulés tu verras, en mode plein écran, ca ne remplit pas le plein ecran ca met le plein ecran mais ca peind au dimensions de l'appli donc ca remplit pas tout, proposer une solution pour corriger ca »

> « sur l'appli tu n'as pas utilisé les objets spécifique d'apprentissage PID si ? il faut »

## 1. Ce qui change

### Le Réglage PID (bibliothèque › Régulation)

Un objet nouveau, codé d'après la maquette que tu as validée. Il regarde une boucle : la consigne, la mesure, la sortie, AUTO / MANU, et le Kp, Ti, Td du bloc.

- **En marche, il lit la boucle à sa période** (100 ms à 10 s), que sa vue soit affichée ou non. Il apprend le procédé sur les échelons de consigne et les mouvements en MANU : le gain, la constante de temps, le retard, et la qualité de ce qu'il a appris. Un échelon sur trois est gardé pour vérifier le modèle.
- **Il tient ses statistiques** : temps dans la tolérance, écart, dépassement, saturation, oscillation, sur 30 minutes, 8 heures, 24 heures ou 7 jours.
- **Il propose un réglage** Robuste, Équilibré ou Rapide, avec la réponse prévue.
- **La carte** dit l'état de la boucle (Saine, Oscille, Lente, Saturée, En MANU, Mesure figée), ses trois valeurs et ses 30 dernières minutes. Un clic ouvre la **fenêtre à quatre onglets** : Statistiques, Ce qu'il a appris, Réglage proposé, Journal. Une butée qui dure quand le poste est relancé compte depuis son début ; en MANU, une sortie en butée est celle de l'opérateur, pas une saturation.
- **Appliquer** écrit le réglage dans le bloc après confirmation, avec le niveau d'accès et la signature de l'objet. Le Journal le garde, **Revenir** remet l'ancien. Aussi : **Copier**, **Oublier et réapprendre**, **Exporter** en CSV.
- **Sa mémoire reste dans l'historique du projet** (`ihm/historique/pid/<Vue.Objet>.mem`) : relancé, il reprend ce qu'il savait.
- **Le bloc** : formes standard, parallèle et série ; Ti et Td en s, min, ms ou dixièmes de seconde ; Kp physique ou normalisé sur l'étendue de la mesure (le PID_INT de Control Expert).
- Dans l'éditeur, ses cases sont rangées en six sections : Boucle, Paramètres du bloc PID, Échelles, Apprentissage, Réglage, Carte. Générer vérifie ses liaisons.

### Les registres du synoptique

Trois objets à côté des vannes :
- le **registre** (une lame, papillon) ;
- le **registre multi-lames** (lames parallèles ou opposées) ;
- le **clapet coupe-feu**.

Ils prennent une ouverture de 0 à 100 % (avec sa plage, comme la vanne) ou ouvert / fermé, le mouvement, le défaut, l'actionneur (servomoteur, vérin, manuel) et les fins de course ouvert et fermé. Le clapet prend aussi son déclenchement et son réarmement (fusible, électrique).

Leurs alarmes d'objet : discordance des fins de course, course trop longue, déclenchement du clapet. Chacun a son exemple animé et sa page du guide.

### Le plein écran remplit l'écran

F11 mettait la fenêtre en plein écran, mais la vue gardait les proportions de la fenêtre de l'appli et laissait des bandes vides. Maintenant la vue se met à l'échelle de l'écran. Trois choix dans la barre flottante, et le choix est retenu :
- **Remplir l'écran** (au départ) : la vue est étirée jusqu'aux bords, avec une échelle par axe. Les textes gardent leur taille.
- **Garder les proportions** : la vue entière, avec des bandes à sa couleur de fond.
- **Taille réelle (100 %)** : la vue centrée.

Le poste d'exploitation a le même réglage, ses écrans secondaires aussi (Configuration › Poste d'exploitation › Mise à l'échelle). Les clics restent justes dans les trois cas.

### Équipements, le réseau du PC avec plusieurs esclaves simulés

Avant, avec plusieurs esclaves simulés, les cartes sortaient du cadre du PC et les prises des ports tombaient contre elles : un esclave semblait branché à un port.

Maintenant :
- les cartes sont rangées dans le cadre du PC, qui grandit avec elles ;
- chaque fil de port part entre deux cartes ;
- un port à deux adresses est en face de son premier réseau ;
- le schéma défile jusqu'au dernier équipement, et Maj+molette le fait défiler de côté ;
- les noms longs et l'encadré Hors réseau tiennent dans un panneau étroit.

### Aussi dans cette version

- **La fenêtre Nouvelle fonction**, refaite comme le créateur de variable : la case Retour ouvre le sélecteur de types ; une vue ou une popup a sa portée, un symbole sa fonction virtuelle ; un aperçu montre la déclaration et les appels.
- **API Studio garde les ladders et les GRAFCET tels quels** : une section LD, FBD ou SFC d'un .XPG est réexportée à l'identique, lisible dans son onglet, et les références croisées y trouvent les variables (affaire 2025_06_118).

## 2. L'affaire 2025_06_118 : la Vue_PID

La Vue_PID du projet IHM de l'affaire a maintenant **un Réglage PID par boucle** (AMF Nord, AMF Sud, AMF complet, LUNA). Chaque objet est lié à SP_x, PV_x, à la sortie et à l'état de SUIVI_REG[k], et à KP_x, TI_x, TD_x. Le bloc est un PID_INT : Kp normalisé sur 0..255 mmCE, Ti en s, période 1 s. La tolérance est celle des statistiques de l'automate (SUIVI_REG[k].Tol).

Appliquer, Revenir et Oublier demandent le niveau Chef de quart. Kp, Ti et Td restent réglables à la main sous chaque carte. Une boucle hors service (AMF complet quand Nord et Sud régulent) a sa carte voilée. Les registres des synoptiques AMF et LUNA sont des objets Registre.

**La démonstration** (`2025_06_118_IHM_DEMO`) joue maintenant ses boucles : sept jours simulés seconde par seconde (les extracteurs, la gaine, la sonde pilote, le PID de l'automate), puis le script `Demo_Boucles`, qui continue en marche avec les mêmes équations. Les archives, les sondes, les vitesses, le suivi de l'automate (SUIVI_REG) et la mémoire des Réglages PID en viennent : tout se tient.
- **AMF Nord** a un Ti trop court pour sa gaine lente : la boucle **oscille**, et le réglage proposé allonge Ti.
- **AMF Sud** est **saine**, un peu prudente.
- **LUNA** est passée en MANU il y a deux jours. Son Kp est passé de 1,8 à 2,0 il y a 26 h (le Journal le montre). Depuis le défaut de VE260, il y a 9 h, ses deux extracteurs à 100 % ne tiennent plus la consigne : la boucle est **saturée**.
- Changer CONS_CP1 dans la Vue_Regulation fait un échelon que le Réglage PID apprend.

## 3. Les essais

79 suites sur 79 (deux nouvelles). Les essais nouveaux de cette version :
- `hmipid` (92 contrôles), nouvelle : le moteur du Réglage PID sur sept jours d'un four simulé, seconde par seconde. Il apprend le gain, la constante de temps et le retard sur les échelons de consigne et les mouvements en MANU, garde un échelon sur trois pour vérifier, tient ses statistiques et propose un réglage ; les incidents : un trou dans les mesures, une mesure figée, une saturation ; la mémoire enregistrée puis relue. Ajouté en dernier : une butée qui dure quand le poste est relancé compte depuis son début (un seul événement dans le Journal, rouvert) ; passée en MANU, la sortie tenue à 100 % n'est plus une saturation (En MANU).
- `graphxml`, nouvelle : un ladder et un GRAFCET gardés tels quels. Chaque schéma d'un XPG est lu en entier et réexporté à l'identique ; le GRAFCET est une seule section SFC ; les références croisées y trouvent les variables.
- `hmi` (5765 contrôles) :
  - `registres11224` : les trois registres, leur ouverture et sa plage, les fins de course et la discordance, le clapet déclenché, leurs alarmes d'objet ;
  - `reglagePid11224` : une vue avec un Réglage PID en marche. Il lit la boucle, apprend deux échelons et propose un réglage ; Appliquer écrit KP et TI dans le bloc et le Journal le garde ; Revenir remet 2 et 8 ; la mémoire reste dans l'historique et une nouvelle marche la reprend ; Oublier ; l'export CSV.
- `hmieditor` (7666 contrôles) :
  - `schemaSimules11224` : quatre esclaves simulés et deux ports, rien ne se chevauche ;
  - le plein écran : Remplir l'écran, Garder les proportions, Taille réelle (l'échelle, les clics justes, le choix retenu), et la fiche du poste d'exploitation ;
  - `nouvelleFonction11224` : la case Retour ouvre le sélecteur de types ;
  - le Réglage PID dans l'éditeur (ses six sections), la bibliothèque (28 tuiles), les notes (387) et les versions (57).

## 4. Les captures

(À compléter.)

## 5. Installer

(À compléter.)

## 6. Les vérifications sous Windows

(À compléter.)

## 7. La suite

- Essayer sur un vrai Windows : le plein écran sur un écran d'une autre taille que la fenêtre, plusieurs esclaves simulés, la Vue_PID de la démonstration.
- À la mise en service de l'affaire : laisser le Réglage PID apprendre sur quelques échelons avant d'appliquer un réglage, et vérifier que l'automate accepte l'écriture de KP_x et TI_x.
- L'historique des modifications des objets (dates, PC, ce qui a changé, au clic droit) : la maquette est envoyée à part, elle attend ton avis.
- En attente : les fonctions IHM génériques (TEMPLATE) partout ; les raccourcis clavier avancés des éditeurs.
