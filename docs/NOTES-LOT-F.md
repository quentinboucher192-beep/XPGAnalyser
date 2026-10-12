# Lot F de l'audit des objets : les notes pour la livraison (1.12.10)

Rédigé le 12/10/2026. Il reprend le plan de `docs/AUDIT-BIBLIOTHEQUE-OBJETS.md` (§ 1.1 P8, § 2.6, § 3, ligne F du § 4) :
**F. Plages et seuils de la variable**, et, dans le même lot, **la valeur initiale d'une structure et d'un
tableau de structures** (vue en préparant la 1.12.9 : `docs/LIVRAISON-1.12.9.md`, « Limites connues »).

Ce document sert à la livraison : les notes de version et les paragraphes du guide se reprennent tels quels
(`src/help/ReleaseNotes.cpp`, `src/hmi/HmiGuideText.cpp` et les numéros de version n'ont pas été touchés).
Chaque puce dit, entre crochets, la fiche du guide à laquelle elle se rattache.

La règle « ce qui démarrait en 1.12.9 démarre toujours » est tenue : aucun contrôle nouveau n'est une erreur
bloquante (des avertissements) ; deux erreurs d'avant deviennent des acceptations (un nom de variable tapé dans
une borne ou un seuil) ; un fichier 1.12.9 se relit tel quel et s'enregistre octet pour octet comme avant tant
qu'on ne règle pas de plage ; un objet d'un projet existant garde son aspect (voir « Les risques »).

## La règle de l'héritage (décision 3)

- Une borne ou un seuil **laissé vide** sur l'objet prend la valeur de sa variable (Unités et formats, sinon la
  mise à l'échelle) ; s'il n'y en a pas, la valeur d'un objet neuf de son genre (0..100, -20..60 pour le
  thermomètre, 0..200 pour la bouteille). **Ce qui est réglé à la main sur l'objet l'emporte.**
- Un objet **posé depuis la palette** naît avec Min et Max vides : il suit sa variable. Un objet d'un projet
  existant a ses valeurs écrites dans son fichier (0, 100…) : il ne change pas. Vider la case dans l'inspecteur
  remet l'objet sur sa variable (avant : vider Min le remettait à 0).
- Les seuils (vides par défaut) des afficheurs et des contenants suivent la variable dès qu'on lui en donne
  dans Unités et formats (un geste de l'utilisateur) ; les bornes vides d'un champ de saisie aussi.
- **La mise à l'échelle ne remplit que les cases qui avaient une valeur par défaut** (Min, Max d'une jauge,
  d'un bargraphe, d'un curseur, d'une cuve, d'une courbe…). Les bornes vides d'un champ de saisie (« sans
  borne ») et la plage de l'ouverture d'une vanne ne suivent qu'une ligne d'Unités et formats : sinon, un projet
  existant aurait vu ses champs bornés et ses vannes rééchelonnées sans que l'utilisateur ne touche à rien.
- La case « Unité, format et plage de la variable » (l'ancienne « Format de la variable ») décochée : l'objet
  garde tout ce qui est à lui (ses cases vides prennent alors la valeur d'un objet neuf).
- Où : le moteur compose la vue (`Runtime::viewOf`) puis `hmi::ranges::apply` (`src/hmi/HmiRanges.cpp`) ; le
  dessin, le clic et le moteur lisent donc les mêmes bornes (un curseur écrit dans la plage qu'il montre).
  L'éditeur dessine l'objet de la même façon ; l'inspecteur montre la valeur héritée en gris, avec son origine.

## Ce qui change pour vous (notes de version)

- **La plage et les seuils de la variable** [unites-formats] : Configuration › Unités et formats gagne six
  colonnes, Min, Max, Seuil bas, Seuil haut, Alarme basse, Alarme haute (dans l'ordre alarme basse < seuil bas <
  seuil haut < alarme haute). Vide : non réglé. Une valeur est un nombre ou le nom d'une variable tapé sans =
  (`Consigne_Max`). Ils se collent depuis Excel comme l'unité et le format. Avant : la plage 0..10 bar se
  retapait sur chaque jauge, bargraphe, curseur, cuve et courbe.
- **La mise à l'échelle donne Min et Max** [variables-liees] : pour une variable liée à un équipement avec une
  mise à l'échelle (0..27648 → 0..10), la ligne montre en gris « 0 (mise à l'échelle) » et « 10 (mise à
  l'échelle) » tant qu'on n'a rien tapé ; les objets à échelle neufs les prennent.
- **Les objets en héritent** [objet-jauge, objet-bargraphe, objet-cuve, objet-curseur, objet-champ-de-saisie,
  objet-courbe…] : la barre de progression, le bargraphe, le cadran (et ses zones vides), le curseur, le
  potentiomètre, le champ de saisie, le vérin (Min et Max) ; la jauge, le thermomètre, la cuve, le silo, la
  trémie, la bouteille (Min, Max et leurs seuils) ; l'afficheur numérique, l'instrument ISA (leurs seuils) ; la
  courbe (son axe Y : l'union des plages de ses plumes) ; la vanne (la plage de son ouverture). Ce qui est réglé sur l'objet l'emporte ;
  un objet posé depuis la palette naît avec Min et Max vides ; un objet existant ne change pas.
- **L'inspecteur le montre** [proprietes] : une case vide montre en gris ce que l'objet en fera, « 10 (de
  Pression_Ligne) », « 0 (mise à l'échelle de Debit) », « 100 (par défaut) ». La case « Format de la variable »
  devient « Unité, format et plage de la variable », et apparaît aussi sur la barre de progression et la courbe.
  Vider Min ou Max, ou « Revenir à la valeur par défaut », rend la case à la variable (avant : 0 et 100).
- **Un objet d'avant qui ne suit pas sa variable** [unites-formats] : Générer dit, en information, qu'une jauge
  écrite 0..100 montre une variable qui va de 0 à 10 (sa ligne, ou sa mise à l'échelle) et propose de vider Min et
  Max pour qu'elle la suive. Rien ne change sans ce geste.
- **Un nom de variable sans =** [objet-cuve, objet-curseur…] : Min, Max, les quatre seuils, l'axe Y d'une courbe,
  la consigne d'un bargraphe et la plage de l'ouverture d'une vanne acceptent `Consigne_Max` tapé sans =, lu
  comme la variable, comme l'axe X de la courbe XY. Avant : refusé par l'inspecteur, et une erreur bloquante de
  Générer (« n'est pas un nombre ») sur un curseur, un afficheur ou une cuve. Un repère s'y garde
  (`$Cuve$.Seuil`) : Dupliquer le remplace, le moteur le lit sans ses `$`. Renommer la variable suit.
- **Le graphique en barres, l'histogramme : un nom dans un seuil** [objet-graphique-en-barres, objet-histogramme] :
  Min, Max, le seuil bas et le seuil haut d'un graphique en barres, les tolérances d'un histogramme acceptent aussi
  une variable tapée sans = (`Seuil_Haut`), lue comme la variable. Avant : ignorée par le dessin (pas de ligne de
  seuil, pas de barre en rouge), et Générer disait « voulais-tu =Seuil_Haut ? ». Leur échelle reste la leur
  (plusieurs variables, une tolérance : rien n'est pris à Unités et formats ; un graphique posé depuis la palette
  garde 0..100).
- **Un seuil hors de la plage se voit** [objet-cuve] : sur une cuve, un silo, une trémie, une bouteille, un seuil
  au-delà de Min ou de Max se dessine au bord, son étiquette marquée d'une flèche vers l'extérieur. Avant, il
  disparaissait en silence alors que son alarme marchait. Générer dit, pour chaque objet, ce qu'il en dessine :
  « il se dessine au bord, marqué d'une flèche » (un contenant), « il se dessine au bout de l'échelle » (une jauge,
  un thermomètre), « il ne se verra pas » (un graphique en barres, un histogramme : comme avant).
- **La vanne : l'ouverture dans sa plage** [objet-vanne] : « Ouverture : min » et « Ouverture : max » (vides :
  ceux de la variable de l'ouverture dans Unités et formats) - un signal 0..27648 ou 0..1 se montre de 0 à 100 %
  sans formule. Sans plage, comme avant.
- **L'instrument ISA : ses seuils** [objet-instrument-isa] : alarme basse, seuil bas, seuil haut, alarme haute
  (vides : ceux de sa variable) ; au-delà d'un seuil, la bulle et la valeur prennent la couleur des seuils, au-delà
  d'une alarme celle des alarmes.
- **La jauge et le thermomètre : leurs seuils** [objet-jauge, objet-thermometre] : les mêmes quatre seuils (vides :
  ceux de la variable), chacun un trait à son niveau (au bord de l'arc, à gauche du tube ; au bout s'il sort de la
  plage) ; au-delà, l'arc de la jauge et la valeur prennent la couleur des seuils ou des alarmes. Avant : ni seuil
  ni zone.
- **Les zones d'un bargraphe, d'un cadran : les seuils de la variable** [objet-bargraphe, objet-cadran-a-zones] :
  posés depuis la palette, leurs zones sont vides - elles suivent les seuils de la variable (sous l'alarme basse et
  au-dessus de l'alarme haute la couleur des alarmes, entre une alarme et son seuil celle des seuils, entre les
  seuils le vert) ; sans seuil, celles d'un objet neuf, en % de l'échelle (`0 %..60 %`), qui restent à leur place
  sur une plage prise à la variable (avant, 0-60 couvrait toute une échelle 0..10). L'inspecteur et Contenu disent
  d'où elles viennent (« les seuils de Pression_Ligne »). Des zones écrites (un objet existant, ou Contenu) restent
  les leurs.
- **La valeur initiale d'une structure** [variables-ihm, types-ihm] : une variable `Pompe : T_Pompe` accepte
  `(Marche := TRUE, Consigne := 12.5)` - les membres nommés, les autres gardent la valeur de leur type. Un tableau
  de structures accepte un littéral par élément : `Cuves : ARRAY[1..3] OF T_Cuve := [(Nom := 'Cuve A', Niveau :=
  4.2), (Nom := 'Cuve B', Niveau := 7.5), (Nom := 'Cuve C', Niveau := 2.8)]` (des éléments en moins gardent les
  valeurs du type). Le moteur l'applique case par case (la place en mémoire et les adresses ne changent pas), la
  simulation montre les valeurs dès le démarrage. Avant : l'écran des variables le refusait, et un littéral écrit
  par un autre outil était ignoré sans un mot. Un membre structure d'un type IHM accepte le même littéral.
- **Le tableau des éléments** [variables-ihm] : « Éléments… » s'ouvre aussi pour un tableau de structures - une
  ligne par élément, son littéral `(Nom := 'Cuve A', Niveau := 4.2)`.
- **Le dossier du projet** [dossier] : le tableau des unités et des formats montre la plage et les seuils quand
  une ligne en a.

## Pour le guide (paragraphes courts)

- *unites-formats* : « 1.12.10 : chaque ligne a aussi Min, Max, Seuil bas, Seuil haut, Alarme basse, Alarme haute
  (un nombre, ou une variable tapée sans = : `Consigne_Max`). Les objets qui montrent la variable les prennent
  pour les cases qu'ils laissent vides ; ce qui est réglé sur l'objet l'emporte. Pour une variable liée à un
  équipement, la mise à l'échelle donne Min et Max (en gris) tant qu'on ne les tape pas. »
- *unites-formats* (Générer) : « 1.12.10 : Générer prévient d'un Min ≥ Max, d'un seuil hors de la plage, de seuils
  dans le désordre et d'une variable inconnue, sur la ligne comme sur les objets qui la prennent. »
- *objet-jauge, objet-barre-de-progression, objet-bargraphe, objet-cadran-a-zones, objet-thermometre,
  objet-curseur, objet-potentiometre, objet-verin* : « 1.12.10 : Min et Max vides prennent la plage de la
  variable (Configuration › Unités et formats, ou sa mise à l'échelle) ; l'inspecteur la montre en gris. Un
  objet posé depuis la palette naît ainsi ; une valeur tapée l'emporte. Min et Max acceptent aussi une variable
  tapée sans = (`Consigne_Max`). »
- *objet-bargraphe, objet-cadran-a-zones* : « 1.12.10 : des zones vides (un objet posé depuis la palette) suivent
  les seuils de la variable - alarmes en rouge, seuils en jaune, le vert entre les seuils ; sans seuil, celles d'un
  objet neuf en % de l'échelle. Des zones écrites dans Contenu restent les leurs. »
- *objet-cuve, objet-silo, objet-tremie, objet-bouteille-de-gaz* : « 1.12.10 : Min, Max et les quatre seuils
  laissés vides viennent de la variable. Un seuil hors de Min..Max se dessine au bord, marqué d'une flèche. »
- *objet-afficheur-numerique* : « 1.12.10 : les seuils laissés vides sont ceux de la variable (Unités et
  formats) ; un seuil peut être une variable tapée sans =. »
- *objet-champ-de-saisie* : « 1.12.10 : Min et Max vides sont ceux de la ligne de la variable dans Unités et
  formats (pas sa mise à l'échelle : vide reste « sans borne » tant que la ligne ne dit rien). »
- *objet-courbe* : « 1.12.10 : Axe Y min et max vides : l'union des plages des plumes (Unités et formats). »
- *objet-graphique-en-barres, objet-histogramme* : « 1.12.10 : Min, Max, le seuil bas et le seuil haut (les tolérances
  de l'histogramme) acceptent une variable tapée sans = (`Seuil_Haut`), lue comme la variable. L'échelle reste celle
  de l'objet : elle ne vient pas d'Unités et formats. »
- *objet-vanne* : « 1.12.10 : Ouverture : min et max - la valeur qui vaut 0 % et celle qui vaut 100 % (27648, 1) ;
  vides, la plage de la variable de l'ouverture dans Unités et formats, sinon 0..100. »
- *objet-instrument-isa* : « 1.12.10 : quatre seuils (vides : ceux de la variable) ; au-delà, la bulle et la
  valeur prennent la couleur des seuils ou des alarmes. »
- *objet-jauge, objet-thermometre* : « 1.12.10 : quatre seuils (alarme basse, seuil bas, seuil haut, alarme haute ;
  vides : ceux de la variable), marqués d'un trait ; au-delà, la valeur (et l'arc de la jauge) prend la couleur des
  seuils ou des alarmes. »
- *proprietes* : « 1.12.10 : la case « Unité, format et plage de la variable » : cochée (par défaut), l'objet
  prend l'unité, le format, la plage et les seuils de sa variable pour ce qu'il laisse vide. »
- *variables-ihm, types-ihm* : « 1.12.10 : une structure prend une valeur initiale `(Marche := TRUE, Consigne :=
  12.5)` ; un tableau de structures, une par élément `[(Nom := 'Cuve A'), (Nom := 'Cuve B')]`. Les membres non
  nommés et les éléments en moins gardent les valeurs du type. Ce qui ne peut pas s'appliquer (un membre
  inconnu, un texte pour un REAL, trop d'éléments) est refusé à la saisie, et dit par Générer et la Console. »

## Ce que Générer vérifie de plus

Tous des avertissements (rien ne bloque) :
- dans Unités et formats : Min ≥ Max (la mise à l'échelle comptée), un seuil hors de Min..Max, des seuils dans le
  désordre (alarme basse ≤ seuil bas ≤ seuil haut ≤ alarme haute), une valeur qui n'est ni un nombre ni une
  variable, une variable inconnue ;
- en information : un objet dont Min et Max écrits diffèrent de la plage de sa variable (« videz Min et Max pour
  qu'il la suive ») ;
- sur un objet, avec ce qu'il prendra à sa variable : Min ≥ Max (et, même écrits sur l'objet, sur une jauge et une
  barre de progression : rien ne le disait), un seuil hors de la plage, des seuils dans le désordre (dès qu'une
  valeur vient de la variable ; les valeurs écrites sur l'objet étaient déjà vérifiées par la 1.12.6) ;
- un nom tapé sans = dans Min, Max, un seuil, l'axe Y, la consigne : une variable inconnue (avant : « n'est pas
  un nombre », une **erreur** bloquante, sur un curseur, un afficheur, une cuve) ; de même sur un graphique en
  barres et un histogramme (avant : « voulais-tu =X ? », et le nom ignoré) ;
- la valeur initiale d'une structure, d'un tableau de structures, d'un membre structure d'un type IHM : un membre
  inconnu, un texte pour un nombre, une valeur d'énumération inconnue, trop d'éléments, un littéral mal formé.
Le message d'un seuil hors de la plage écrite sur l'objet (1.12.6) dit maintenant ce que l'objet en dessine : « il
se dessine au bord, marqué d'une flèche » (cuve, silo, trémie, bouteille), « il se dessine au bout de l'échelle »
(jauge, thermomètre) ; « il ne se verra pas » reste pour les graphiques (avant : « il ne se verra pas » partout).

## Les essais

`HMI_TEST_11210=1` les lance seuls (dans `hmi`, `hmieditor`).
- `hmi_test` : `lotF11210` - la ligne enregistrée et relue (une ligne sans plage s'écrit comme en 1.12.9) ;
  l'héritage (une jauge neuve 0..10, une jauge existante 0..100 inchangée, une variable sans plage, la mise à
  l'échelle, la case décochée, les quatre seuils d'une cuve, un seuil réglé sur l'objet qui l'emporte, un
  afficheur, une courbe et l'union de ses plumes, une bouteille neuve à 0..200 ; les zones d'un bargraphe neuf
  tirées des seuils, celles d'un cadran neuf en %, celles d'un bargraphe existant inchangées) ; un nom tapé sans = devenu
  formule (Max d'un curseur, consigne d'un bargraphe) ; l'aide grise de l'inspecteur ; en marche, le dessin (la
  jauge sur 0..10, la consigne à 8), un curseur poussé au bout qui écrit 10 puis 8, un champ de saisie qui refuse
  12 ; Générer (Min ≥ Max, un nom inconnu, le désordre, une alarme hors plage dans Unités et formats ; un seuil
  tapé en nom accepté ; un Max inconnu ; une jauge 10..5 ; un seuil hors de la plage héritée ; rien de bloquant).
  `structuresF11210` - le littéral d'une structure, d'un tableau de structures appliqué en marche (Cuves à 4.2,
  7.5, 2.8 ; Pompe ; des éléments en moins) ; un graphique en barres `Cuves[*].Niveau` nommé par `Cuves[*].Nom`
  qui montre 4.2, 7.5, 2.8 et Cuve A, B, C dès le démarrage, sans script ; un texte pour un REAL, un membre
  inconnu, trop d'éléments dits par la Console et par Générer, sans rien bloquer ; le littéral relu.
- `hmi_editor_test` : `lotF11210_editeur` - Unités et formats (les six colonnes, une valeur refusée, la mise à
  l'échelle en gris, une valeur tapée qui l'emporte, Ctrl+Z, le collage depuis Excel d'une plage et d'une alarme) ;
  l'inspecteur (une jauge posée depuis la palette avec Min et Max vides, l'aide grise « 10 (de Pression_Ligne) »,
  le nouveau libellé, un nom accepté, vider rend à la variable, « Revenir à la valeur par défaut » aussi, un texte
  refusé) ; le dessin (l'étiquette HH d'une alarme hors plage, une vanne 0..27648 à 50 %, un instrument ISA en
  couleur d'alarme puis de seuil) ; l'écran des variables (un littéral par élément accepté, une structure, un
  membre inconnu, un texte pour un REAL et trop d'éléments refusés avec leur raison) ; le tableau « Éléments »
  d'un tableau de structures (une ligne par élément, son littéral, rendu tel quel). L'essai du lot 13 sur le volet lit
  l'exemple et « montrée par » à leurs nouvelles colonnes (9 et 10).
- `hmi_reperes_test` : `repereSeuil11210` - `$Cuve_A$.Seuil` dans un seuil et `$Cuve_A$.Maxi` dans un Max :
  Dupliquer les trouve et les remplace (`$Cuve_B$`), en marche chaque copie lit sa variable ; la plage prise à la
  variable d'une valeur à repère (`=$Cuve_A$.Niveau`) ; `$Cuve_A$.Seuil` dans le seuil haut d'un graphique en
  barres et dans l'alarme haute d'un instrument ISA, `$Cuve_A$.Maxi` dans « Ouverture : max » d'une vanne : trouvés,
  remplacés, lus en marche (70 puis 30 pour la copie ; la vanne ouverte à 25 % puis 50 %).
- Dans `lotF11210` aussi : la vanne (une ouverture 0..27648 à 13824 : 50 % ; une variable 0..1 d'Unités et formats :
  25 % sans formule ; une vanne sans plage inchangée), l'instrument ISA qui prend les seuils de sa variable,
  l'alarme « niveau très haut » d'une cuve qui prend l'alarme haute de sa variable ; dans `lotF11210_editeur`, le
  dessin d'une cuve dont l'alarme haute (150) dépasse la plage 0..100 : son étiquette HH se dessine (avant : rien) ;
  une jauge qui prend les quatre seuils de sa variable ; dans `lotF11210_editeur`, une jauge au-delà de son alarme
  haute et un thermomètre au-delà de son seuil haut dessinent leur valeur dans la couleur voulue.
- Dans `lotF11210` encore : un graphique en barres dont le seuil haut et le Max sont tapés en nom (`Seuil_Haut`,
  `Echelle_Max`) les lit en marche (7.5, 12), un histogramme sa tolérance haute ; un graphique posé depuis la palette
  garde 0..100 ; un seuil inconnu dit par Générer, plus de « voulais-tu = » ; le message d'un seuil hors de la plage
  selon l'objet (une cuve : la flèche ; une jauge : le bout de l'échelle ; un graphique en barres : « il ne se verra
  pas »). Dans `lotF11210_editeur` : un graphique en barres posé depuis la palette garde 0..100, son seuil haut
  accepte un nom, vider son Min ou « Revenir à la valeur par défaut » le rend à 0 ou 100 comme avant.

Trois essais existants ont changé, parce que le comportement qu'ils figeaient a changé exprès :
- `hmi` (lot 9) : une borne `abc` sur un curseur était « n'est pas un nombre » (une erreur) ; `abc` est maintenant un
  nom de variable. L'essai garde son intention avec `12 bar` (ni un nombre ni un nom : toujours l'erreur) ;
- `hmi` (lot 11) : de même, le seuil illisible d'une cuve passe de `x` (un nom) à `8 %` ;
- `hmieditor` (1.10.4, K3) : la section Vanne de l'inspecteur montre trois cases de plus (« Unité, format et plage
  de la variable », « Ouverture : min », « Ouverture : max ») ; et l'essai du lot 13 sur Unités et formats lit
  l'exemple et « montrée par » aux colonnes 9 et 10 (six colonnes de plage avant elles).

## Ce qui n'est pas fait

- **Les zones d'un bargraphe, d'un cadran à partir des seuils** : seulement pour des zones vides (un objet neuf, ou
  des zones effacées) et des seuils en nombres (un seuil qui est une variable n'y compte pas) ; des zones écrites
  ne changent jamais.
- **La barre de progression n'a pas de seuils** ; la jauge et le thermomètre n'ont pas de zones (leurs seuils sont
  des traits et une couleur). Le graphique en barres, l'histogramme, la courbe XY ne prennent pas leurs bornes à la
  variable (plusieurs variables, ou une autre sémantique : la tolérance d'un histogramme, le TRS d'un compteur) ; le
  graphique en barres et l'histogramme acceptent seulement un nom sans = dans Min, Max et leurs seuils. La courbe
  prend l'union des plages de ses plumes.
- **Le dessin hors plage** : seuls les contenants (cuve, silo, trémie, bouteille) dessinent un seuil hors de la plage
  au bord, avec une flèche ; la consigne d'un bargraphe hors plage reste ramenée au bord comme avant ; un
  afficheur numérique n'a pas de plage.
- **Une borne héritée qui est un nom** (`Max = Consigne_Max` dans Unités et formats) se lit en marche ; l'éditeur
  dessine alors l'objet avec la valeur d'un objet neuf (il ne calcule pas les variables), et Générer ne compare
  pas un seuil à une borne qui est une variable.
- **Le volet Unités et formats ne propose pas** les variables liées avec une mise à l'échelle (Proposer ne relit
  que les unités et les formats des objets) ; la plage d'une telle variable sert aux objets même sans ligne.
- **Les éléments d'un tableau de structures** : le tableau « Éléments… » montre une ligne par élément et son
  littéral entier `(Nom := 'Cuve A', Niveau := 4.2)` dans une seule case (pas une colonne par membre) ; une
  structure seule se règle dans la case Valeur initiale (le tableau ne s'ouvre pas pour elle).
- **Le littéral positionnel** `('Cuve A', 4.2)` (sans les noms) n'est pas lu : il est refusé avec « écris
  Membre := valeur ». Les chaînes entre guillemets doubles (WSTRING) ne protègent pas une virgule.
- **La structure liée à un équipement** : sa mise à l'échelle sert à tous ses membres simples (la racine du chemin)
  - comme le moteur l'applique ; une mise à l'échelle par membre n'existe pas.

## Les risques pour les projets existants

- **Les formats** : une ligne d'Unités et formats sans plage s'enregistre exactement comme en 1.12.9 (les six
  champs `min`, `max`, `seuil_bas`, `seuil_haut`, `alarme_basse`, `alarme_haute` sont absents tant qu'ils sont
  vides) ; un fichier 1.12.10 avec une plage, relu par une 1.12.9, perd seulement la plage (les champs inconnus sont
  ignorés). Les valeurs initiales en littéral sont du texte, déjà enregistré tel quel.
- **L'aspect des objets existants ne change pas**, sauf dans ces cas, tous d'un geste de l'utilisateur ou d'un
  défaut corrigé :
  - un champ de saisie aux bornes vides se borne à la plage de sa variable dès que sa ligne d'Unités et formats a
    Min ou Max (avant : sans borne) ;
  - les afficheurs, les cuves et les instruments ISA aux seuils vides prennent ceux de leur variable dès que sa
    ligne en a ; les alarmes de niveau d'une cuve aussi (elles étaient « sans objet », seuil vide) ;
  - une vanne sans plage d'ouverture prend celle de la ligne de sa variable (son ouverture lue en %) ;
  - une valeur à repère (`=$Cuve$.Niveau`) trouve maintenant l'unité, le format et la plage de sa variable
    (avant : le repère la laissait sans unité) ;
  - une borne ou un seuil tapé en nom (`abc`, `Seuil`) était une erreur bloquante sur un curseur, un afficheur,
    une cuve (refusé par l'inspecteur, de toute façon) - c'est maintenant la variable ; sur un graphique en barres
    ou un histogramme, il était ignoré (Générer : « voulais-tu = ? ») - la ligne du seuil se dessine maintenant à la
    valeur de la variable ;
  - un tableau de structures ou une structure dont la valeur initiale avait été écrite par un autre outil voit ce
    littéral appliqué au démarrage (avant : ignoré) ; ce qui ne s'applique pas garde la valeur du type et est dit.
- **Les seuils hors plage d'une cuve** se dessinent maintenant au bord (avant : invisibles) ; Générer le disait
  déjà (1.12.6), son message change.
- **Un objet posé depuis la palette** (jauge, barre, bargraphe, cadran, thermomètre, curseur, potentiomètre,
  cuve, silo, trémie, bouteille, vérin, courbe) a Min et Max vides dans son fichier (la valeur de sa variable, ou
  celle d'un objet neuf), et un bargraphe ou un cadran ses zones vides ; un objet créé autrement (modèles,
  exemples, Dupliquer, copier-coller) garde ses valeurs. Ouvert dans une 1.12.9, un tel objet se dessinerait sur
  la plage par défaut de son dessin (0..100), sans zone ni seuil hérité.
- **Le moteur compose une vue de plus** : une vue qui a un objet à plage héritée (ou un nom dans une borne) est
  recomposée à chaque cycle (100 ms), comme une vue à joker (lot E) ; le coût n'est pas mesuré sur un gros projet.
- **Les rôles** : Min, Max, les seuils (et l'axe Y, la consigne, la plage de l'ouverture) d'un objet à échelle, d'un
  graphique en barres et d'un histogramme sont des formules sans = (comme les bornes du champ de saisie depuis la
  1.11.24) : Dupliquer les lit en mode formule, l'avertissement « tapé sans = : voulais-tu =X ? » ne s'y applique
  plus (le nom est la variable).
