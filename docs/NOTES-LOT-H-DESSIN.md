# Lot H de l'audit des objets, 1re partie : les sommets sur le dessin (1.12.12)

Rédigé le 12/10/2026. Il reprend le plan de `docs/AUDIT-BIBLIOTHEQUE-OBJETS.md` (§ 3.1, § 3.4, § 3.9) :
la ligne, le polygone, le plan à zones et la courbe XY se règlent sur le dessin, plus seulement en tapant des
coordonnées.

Ce document sert à la livraison : les notes de version et les paragraphes du guide se reprennent tels quels
(`src/help/ReleaseNotes.cpp`, `src/hmi/HmiGuideText.cpp` et les numéros de version n'ont pas été touchés). Chaque
point donne l'identifiant de la fiche du guide qu'il complète.

La règle « ce qui démarrait en 1.12.9 démarre toujours » est tenue : les contrôles nouveaux de Générer sont des
avertissements, jamais des erreurs ; les cinq propriétés nouvelles (`pointsFrom`, `pointsXMin`, `pointsXMax`,
`pointsYMin`, `pointsYMax`) ne sont écrites que dans une ligne ou un polygone qui s'en sert : un projet qui ne s'en
sert pas s'enregistre comme avant. Un objet existant garde son dessin tant qu'on ne le change pas (seule exception
voulue : `10, 20` se lit désormais, voir plus bas).

## Ce qui change pour vous (notes de version)

- **Les sommets sur le dessin** (Dessin ; guide : `editeur`, `objet-ligne`, `objet-polygone`) : une ligne ou un
  polygone choisi seul montre une poignée ronde sur chaque sommet. Glisser une poignée déplace le sommet (au pas
  de la grille, aimanté aux guides, aux bords des objets et aux autres sommets ; Alt : librement) ; un clic choisit
  le sommet (la barre d'état dit où il est), que les flèches déplacent alors d'un pixel (Maj : du pas de la grille)
  au lieu de l'objet ; un double-clic sur un segment ajoute un sommet à cet endroit ; Suppr,
  ou le menu du clic droit « Retirer le sommet », retire le sommet choisi. Une ligne garde au moins 2 sommets, un
  polygone 3 (Suppr le dit et ne retire rien). Chaque geste s'annule d'un seul Ctrl+Z. Le cadre de l'objet suit
  ses points : il reste le plus petit qui les tient, le dessin ne bouge pas à l'écran (rotation et miroirs
  compris). Plusieurs objets choisis : le cadre commun, comme avant ; Échap oublie le sommet choisi.
- **Le tableau des sommets** (Dessin ; guide : `contenu`, `objet-ligne`, `objet-polygone`) : la ligne et le
  polygone ont un onglet Contenu, une ligne par sommet (X, Y). Ajouter pose un sommet au bout (sur le dernier côté
  d'un polygone, dans le prolongement d'une ligne), Retirer, ↑ ↓ changent l'ordre. Une case accepte un nombre
  (`12,5` tapé à la française s'écrit `12.5`) ou un repère `$Cote_X$`, gardé case par case ; un blanc ou une virgule
  hors d'un nombre sont refusés (ils couperaient la liste). Le sommet choisi dans Contenu est celui du dessin, et
  l'inverse. Un morceau du texte que le dessin ne lit pas a sa ligne, marquée « (illisible) ».
- **`10, 20` se lit** (Dessin ; guide : `objet-ligne`, `objet-polygone`) : une espace après (ou avant) la virgule
  ne fait plus perdre le point - avant, il était ignoré sans un mot. Un projet qui avait écrit `10, 20` voit donc ce
  point apparaître.
- **Points depuis une source** (Dessin ; guide : `objet-ligne`, `objet-polygone`) : « Points depuis » trace une
  ligne ou un polygone d'après une variable, en marche (dans l'éditeur : ses valeurs initiales) - un tableau ou une
  LIST de nombres, x et y alternés (`Courbe`), ou de structures (`Profil[*].X, Profil[*].Y`, les conventions de la
  courbe XY en profil). Les points se placent dans le cadre de l'objet selon une échelle X et une échelle Y
  (« Points depuis : X min, X max, Y min, Y max » ; vides : celles des données) : X min à gauche, Y min en bas,
  comme un graphique ; hors de l'échelle, au bord. La liste propose les variables qui conviennent ; un repère
  `$Profil$` suit Dupliquer.
- **Les zones d'un plan se tracent** (Alarmes ; guide : `objet-plan-a-zones`) : la zone choisie dans Contenu montre
  ses sommets sur le plan et se règle comme un polygone (glisser, double-clic pour ajouter, Suppr pour retirer ;
  3 sommets au moins) - les points restent en % du plan. « Ajouter » (Contenu), ou « Tracer une zone » (clic droit
  sur le plan), trace une zone à la souris : un clic par point ; un double-clic, ou un clic sur le premier point, la
  ferme ; Échap l'abandonne. La zone tracée s'appelle « Zone N » et se choisit dans Contenu.
- **La variable du plan à zones se lit et s'écrit** (Alarmes ; guide : `objet-plan-a-zones`) : un clic y écrit le
  **nom** de la zone (une STRING) ou son **numéro** (un entier : 1 pour la première) ; un programme qui l'écrit
  choisit la zone (le nom, ou son groupe d'alarmes ; le numéro ; 0 ou un nom inconnu : plus de zone choisie), comme
  un clic sans ouvrir de vue. Avant, la variable recevait le groupe d'alarmes en texte, et un entier restait à 0.
- **La courbe de référence d'une courbe XY se règle sur le dessin** (Graphiques ; guide : `objet-courbe-xy`,
  `contenu`) : avec une échelle fixe, ses points ont des poignées dans la zone de tracé - glisser (le point reste
  entre ses voisins : la courbe reste y(x)), double-clic sur la courbe pour en ajouter un, Suppr pour le retirer
  (le dessin en garde 2). Contenu › Montrer : « la référence » la montre en tableau (X, Y dans les unités des axes,
  rangés par x croissant ; Ajouter après le dernier ; Contenu peut la vider). Une échelle auto, une référence en
  formule ou depuis une source n'ont pas de poignées.

## Pour le guide (paragraphes courts)

- *Ligne, polygone* (`objet-ligne`, `objet-polygone`) : « 1.12.12 : choisi seul, l'objet montre ses sommets.
  Glissez une poignée pour déplacer un sommet, double-cliquez sur un segment pour en ajouter un, Suppr retire le
  sommet choisi (une ligne en garde 2, un polygone 3). Le cadre suit les points ; l'onglet Contenu les montre en
  tableau (X, Y), un repère `$Cote_X$` y est gardé. »
- *Ligne, polygone : Points depuis* (`objet-ligne`, `objet-polygone`) : « 1.12.12 : Points depuis trace l'objet
  d'après une variable : des nombres x et y alternés (`Courbe`), ou les membres X et Y d'un tableau de structures
  (`Profil[*].X, Profil[*].Y`). Ils se placent dans le cadre selon l'échelle X et Y (vide : celle des données),
  Y min en bas. Dans l'éditeur, le dessin montre les valeurs initiales. »
- *Plan à zones : tracer* (`objet-plan-a-zones`) : « 1.12.12 : la zone choisie dans Contenu se règle sur le plan,
  comme un polygone. Ajouter (ou clic droit › Tracer une zone) trace une nouvelle zone : un clic par point, un
  double-clic ou un clic sur le premier point la ferme, Échap l'abandonne. »
- *Plan à zones : la variable* (`objet-plan-a-zones`) : « 1.12.12 : la variable de la zone choisie reçoit le nom
  de la zone (STRING) ou son numéro (entier, 1 pour la première). Écrite par le programme, elle choisit la zone
  (0 : aucune). Générer signale une variable d'un autre type. » (Remplace, dans le tableau des paramètres,
  « Une variable STRING qui reçoit le nom du groupe de la zone cliquée ».)
- *Courbe XY* (`objet-courbe-xy`) : « 1.12.12 : avec une échelle fixe, les points de la courbe de référence se
  règlent sur le dessin (glisser, double-clic, Suppr) ou dans Contenu › Montrer : la référence. Un point glissé ne
  dépasse pas ses voisins. »
- *Contenu* (`contenu`) : « 1.12.12 : la ligne et le polygone ont leur onglet Contenu (un sommet par ligne) ; la
  courbe XY y montre ses courbes ou sa courbe de référence. Le sommet choisi est le même dans Contenu et sur le
  dessin. »
- *L'éditeur de vues* (`editeur`) : « 1.12.12 : les sommets d'une ligne, d'un polygone, d'une zone de plan et de
  la référence d'une courbe XY ont des poignées rondes : glisser (Alt : sans magnétisme), double-clic sur un
  segment pour ajouter, clic droit › Retirer le sommet ou Suppr. Chaque geste s'annule d'un Ctrl+Z. »

## Ce que Générer vérifie de plus

Tous des avertissements : rien ne bloque (guide : `verifier`).
- un morceau du texte des points d'une ligne, d'un polygone qui ne donne pas un point net, dit à sa place :
  « point 3 (7) : un nombre seul (il faut x,y) : ignoré », « point 2 (10,5,20) : trois nombres, lu x 10 ; y 5.2 -
  une virgule décimale ? écris les décimales avec un point (10.5,20) », « point 4 (abc) : illisible » - avant, ces
  morceaux étaient ignorés en silence (seul « pas assez de points » était dit, et il reste une erreur comme avant) ;
- « Points depuis » : une source qui ne se lit pas, inconnue ou sans éléments (comme les sources du lot E), une
  variable inconnue, une source qui n'est pas faite de nombres, une échelle dont le minimum n'est pas sous le
  maximum ; « pas assez de points » ne s'applique plus à un objet qui prend ses points d'une source ;
- le plan à zones : une variable de la zone choisie qui n'est ni une STRING ni un entier (« elle ne se relira pas ») ;
- le plan à zones : « aucune alarme du groupe » ne se dit plus à tort quand le groupe est celui d'alarmes d'objets
  (`Vue.*`, `Vue.Objet`, `symbole:Nom`) ou s'écrit avec un repère (`$Zone_A$`).

## Les propriétés nouvelles (format de fichier)

Toutes facultatives, absentes d'un objet qui ne s'en sert pas (un fichier 1.12.9 se relit tel quel ; un fichier
qui les porte, relu par une 1.12.9, garde les points écrits) :
- `pointsFrom` (ligne, polygone) : la source des points, `Courbe` ou `Profil[*].X, Profil[*].Y` ; vide : les
  points écrits ;
- `pointsXMin`, `pointsXMax`, `pointsYMin`, `pointsYMax` (ligne, polygone) : l'échelle des points d'une source (un
  nombre, une formule ou un repère ; vide : celle des données). Elles ne se proposent qu'une fois la source réglée.

Les propriétés existantes `points`, `mapZones` et `reference` gardent leur forme ; les gestes les réécrivent
(`x,y x,y`, un nombre tel qu'il était écrit tant qu'il ne bouge pas, un repère gardé) ; la référence d'une courbe
XY réécrite est rangée par x croissant.

## Les essais

Les 72 essais passent (`ctest` : « 100% tests passed, 0 tests failed out of 72 »). Les nouveaux :
- `hmi_test` : `lotH11212_points` (le lecteur : `10, 20`, `10 ,20`, `10,5,20`, un nombre seul, un mot ; le dessin,
  le choix à la souris et Générer lisent pareil ; Générer dit chaque morceau à sa place, rien de bloquant ; les
  gestes du modèle : déplacer, insérer, retirer, le minimum, le cadre qui suit, la rotation),
  `lotH11212_pointsDepuis` (un ARRAY OF REAL alterné, un ARRAY OF STRUCT `Profil[*].X, Profil[*].Y`, une LIST OF
  REAL qui grandit en marche ; l'aperçu de l'éditeur, la marche cycle après cycle ; l'échelle écrite et celle des
  données ; Générer : source inconnue, un REAL seul, échelle à l'envers, rien de bloquant, « pas assez de points »
  ne s'applique plus ; enregistré et relu, absent d'une ligne qui ne s'en sert pas), `lotH11212_planZones` (la
  variable écrite au clic : le nom dans une STRING, le numéro dans un INT ; ce que le clic a écrit n'est pas relu
  comme un choix ; le programme qui choisit la zone par son numéro, par son nom, 0 pour aucune ; Générer : une
  variable REAL signalée, les groupes des alarmes d'objets et un repère sans fausse information),
  `lotH11212_reference` (la référence d'une courbe XY lue comme le dessin la lit, « ; » compris ; la zone de tracé ;
  glisser, le voisin qu'on ne dépasse pas, insérer, retirer jusqu'à 2 ; échelle auto, source, texte illisible : pas
  sur le dessin ; vide : elle se commence dans Contenu). `HMI_TEST_11212H1=1` les lance seuls.
- `hmi_editor_test` : `lotH11212_dessin` (un polygone : glisser un sommet au pas de la grille, Alt sans grille, un
  seul Ctrl+Z, double-clic sur un segment, le clic droit « Retirer le sommet », Suppr, le minimum, un clic sur le
  corps ; une ligne et ses 2 sommets, les flèches sur un sommet, la sélection multiple ; le tableau X / Y de
  Contenu : Ajouter, une case, `12,5`, un repère gardé case par case, l'ordre, le même sommet choisi des deux
  côtés ; une zone de plan glissée en %, tracée à la souris, fermée sur le premier point ou d'un double-clic, Échap,
  le clic droit « Tracer une zone » ; la référence d'une courbe XY : glisser, un voisin qu'on ne dépasse pas,
  double-clic, Suppr, Contenu › Montrer la référence, le rangement par x, un repère refusé, Ajouter, vider,
  l'échelle auto et la source sans poignées). `HMI_TEST_11212H1=1` le lance seul.
- `hmi_reperes_test` : `pointsDepuisRepere11212` (Dupliquer `$Profil_A$` → `$Profil_B$` dans Points depuis et
  `$Xmax_A$` → `$Xmax_B$` dans la formule de l'échelle X, Générer muet, l'original et la copie tracés en marche
  chacun d'après sa source et son échelle ; un repère dans le texte des points gardé quand une autre case change).

## Ce qui n'est pas fait, et pourquoi

- **La référence d'une courbe XY en échelle auto** n'a pas de poignées : l'échelle bougerait avec le point qu'on
  glisse. Elle se règle dans Contenu (Montrer : la référence), qui marche dans les deux échelles.
- **Un repère dans la référence d'une courbe XY** est refusé dans Contenu : le dessin et le moteur ne liraient
  plus la courbe (un repère se garde dans les sommets d'une ligne ou d'un polygone).
- **Un repère dans une case de sommet** garde son texte (Dupliquer le remplace), mais le texte des points ne se
  calcule pas : tant que la case n'est pas un nombre, le sommet ne se dessine pas ; la ligne de Contenu le dit
  (« illisible ») et Générer aussi.
- **L'aperçu de Points depuis** dans l'éditeur montre les valeurs initiales : un ARRAY OF STRUCT n'a pas de valeur
  initiale par élément (limite connue de la 1.12.9) - tous ses éléments ont celles du type, et l'aperçu peut se
  réduire à un point ; une LIST montre les éléments de son littéral.
- **Le magnétisme d'un sommet** suit celui des objets (grille, guides, bords, centres, autres sommets) ; pas
  d'angle contraint (Maj à 45°) pour un sommet.
- **Le tracé d'une zone** se lance depuis Contenu › Ajouter ou le clic droit sur le plan (pas de bouton dans la
  barre d'outils du dessin).

## Les risques

- **`10, 20` se lit maintenant** : une ligne ou un polygone qui portait ce texte prend un point qu'il n'avait pas
  (c'est la correction demandée ; Générer le disait illisible en silence avant, il ne dit plus rien).
- **La variable du plan à zones reçoit le nom de la zone**, plus son groupe d'alarmes : identique quand la zone n'a
  pas de groupe (le cas courant), différent sinon - un programme qui comparait la variable au groupe doit comparer
  au nom (la lecture accepte les deux). Un entier reçoit maintenant le numéro (avant : rien d'utile) et, s'il
  vaut déjà un numéro de zone au démarrage, choisit cette zone.
- **Les clics sur une ligne, un polygone, une courbe XY choisis** : un clic sur une poignée de sommet prend le
  sommet au lieu de déplacer l'objet ; un double-clic sur un segment ajoute un sommet. Le corps de l'objet se
  déplace comme avant.
- **L'onglet Contenu** apparaît pour la ligne et le polygone (avant : pas d'onglet) ; la courbe XY garde ses
  courbes par défaut.
- **Fichiers partagés** : `HmiCanvas`, `HmiContentPanel`, `HmiEditor`, `HmiPanels`, `HmiPainter`, `HmiCheck`,
  `HmiSources`, `HmiEdit`, `HmiRuntime` (et `Lot11`, `Lot12`), `HmiRoles.hpp` et les trois fichiers d'essais sont
  modifiés par ajouts locaux ; le nouveau module `src/hmi/HmiVertices.cpp/.hpp` porte le reste (ajouté au
  `.vcxproj` et à ses filtres).
