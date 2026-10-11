# Lots D et E de l'audit des objets : les notes pour la livraison

Rédigé dans la nuit du 11 au 12/10/2026. Il reprend le plan de `docs/AUDIT-BIBLIOTHEQUE-OBJETS.md` (§ 4) :
- **D. Les listes en tableau et les choix** (§ 2.4, § 2.5) ;
- **E. Les tailles dynamiques** (§ 2.3, § 3.8).

Ce document sert à la livraison : les notes de version et les paragraphes du guide se reprennent tels quels
(`src/help/ReleaseNotes.cpp` et `src/hmi/HmiGuideText.cpp` n'ont pas été touchés, ni les numéros de version).
Pour chaque lot : ce qui change pour l'utilisateur, ce que Générer vérifie de plus, les essais, ce qui n'est pas fait.

La règle « ce qui démarrait en 1.12.6 démarre toujours » est tenue : aucun contrôle nouveau n'est une erreur
bloquante (ce sont des avertissements ou des informations), aucune propriété nouvelle n'est écrite dans un objet
qui ne s'en sert pas, et un projet sans calendrier des postes s'enregistre octet pour octet comme avant.

## Lot D : les listes en tableau et les choix

### Ce qui change pour vous (notes de version)

- **Contenu, une ligne par choix** (Objets) : le sélecteur, la liste déroulante, les boutons radio et la liste ont
  leur onglet Contenu, une ligne par choix (son libellé, la valeur qu'il écrit ; vide : son rang). Ajouter,
  retirer, monter, descendre emmènent le libellé et sa valeur ensemble (avant : `positions` ou `items` et
  `values` à aligner à la main). Un « ; » nu dans un libellé est refusé tout de suite (il couperait la liste) ; un
  repère `$Mode$` est gardé tel quel. Avec « Éléments depuis », Contenu le dit : les lignes ne servent pas.
- **Le tableau de variables** (Production) : l'unité et le format de chaque ligne se règlent dans Contenu, à côté
  de sa variable (avant : les listes `units` et `formats` de l'inspecteur).
- **Le chronogramme d'états** (Graphiques) : Contenu montre ses lignes ou ses états (« Montrer »), et ses états
  s'éditent comme ceux d'un voyant multi-états, une ligne par état (avant : la ligne `stateList` à taper).
- **Les zones d'un bargraphe et d'un cadran** (Affichage) : une ligne par zone dans Contenu (De, À, Couleur).
  Une borne peut s'écrire en % de l'échelle (`60 %`) : elle suit Min et Max quand ils changent (avant : changer
  l'échelle obligeait à retaper les zones). Une borne en valeur reste une valeur.
- **Les plages du programmateur horaire** (Commandes) : une ligne par plage dans Contenu (Jours, De, À), avec ce
  que la résolution en fait : « arrondie à 14:00-17:00 (résolution 1 h) », « disparaît : plus courte qu'une case ».
  Des jours ou une heure illisibles sont refusés à la saisie.
- **Des cases à cocher au lieu de codes à taper** (Objets) : le sélecteur de langue coche les langues du projet,
  la gestion des utilisateurs ses boutons, le résumé par zone ses zones (les groupes du projet et ceux des
  alarmes des objets). Rien de coché vaut toujours « toutes » ; décocher une case garde les autres. Ce qui avait
  été tapé et ne se propose pas (un repère `$Langue$`) reste, coché.
- **Choisir au lieu de taper** (Objets) : dans l'inspecteur, la vue après connexion se choisit dans les vues ; les
  lettres d'un instrument ISA dans une liste (PT, TT, FT, LT, PIC, TIC…) qui propose son unité (TT : °C ; une
  unité tapée à la main reste) ; la position fixe d'une vanne 3 voies dans « fermée, 1-2, 1-3, 2-3 » (le code
  0, 1, 2, 3 reste écrit, une variable garde ses codes) ; l'onglet montré d'un conteneur par son nom ; les débuts
  de poste d'un compteur peuvent suivre le calendrier du projet.
- **Une liste à choix accepte aussi un nom tapé** (Repères) : chaque liste de noms de l'inspecteur (vue, groupe
  d'alarmes, alarme, recette, ressource, style, lettres ISA…) et de Contenu (la vue d'une barre de navigation,
  le groupe et la vue d'une zone de plan, la recette d'un gestionnaire) a un dernier choix « Taper un nom ou un
  repère $…$ » : ce qu'on y tape est gardé tel quel, et un repère se voit en ambre, comme dans toute case.
- **Les groupes des alarmes des objets** (Alarmes) : la zone d'un plan propose aussi les groupes des objets
  (`Vue.*`, `Vue.Objet`, `symbole:Nom`), comme le bandeau et l'historique.
- **La vue après connexion suit le renommage** (Utilisateurs) : renommer une vue met à jour la Connexion qui y mène
  (avant : seules la barre de navigation et le plan à zones suivaient) ; un repère `$Vue_A$` garde ses `$`.
- **Les colonnes d'un tableau depuis une source** (Affichage) : un tableau dont les lignes viennent d'une variable
  (« Lignes depuis ») montre dans Contenu les colonnes de la source ; on choisit celles qui se montrent, leur ordre
  (↑ ↓), leur titre et leur format (`0.0`). Avant, `columns` était ignoré avec une source : toutes les colonnes,
  sous le nom de leur membre. Rien de choisi : toutes, comme avant (la propriété `sourceColumns` est absente).
- **Le calendrier des postes** (Production) : Configuration › Exécution › « Calendrier des postes (hh:mm;…) ». Un
  compteur de production dont les débuts de poste disent « calendrier » le suit : changer les postes du site se
  fait une fois (avant : une liste à taper dans chaque compteur).

### Pour le guide (paragraphes courts)

- *Sélecteur, liste déroulante, boutons radio, liste* : « 1.12.7 : l'onglet Contenu montre une ligne par choix :
  son libellé et la valeur qu'il écrit (vide : son rang, ou le libellé pour une STRING). Ajouter, retirer, monter
  et descendre emmènent les deux ensemble. Un repère (`$Mode$`) se garde dans la case. »
- *Tableau de variables* : « 1.12.7 : l'unité et le format d'une ligne se règlent dans Contenu, avec sa variable ;
  vides, ce sont ceux de la variable (Configuration › Unités et formats). »
- *Chronogramme d'états* : « 1.12.7 : Contenu › Montrer : les lignes, ou les états — une ligne par état (la valeur,
  le texte, la couleur), comme le voyant multi-états. »
- *Bargraphe, cadran à zones* : « 1.12.7 : les zones se règlent dans Contenu (De, À, Couleur). Une borne en % de
  l'échelle (`60 %`) suit Min et Max. Générer signale deux zones qui se chevauchent. »
- *Programmateur horaire* : « 1.12.7 : les plages se règlent dans Contenu (Jours, De, À). La ligne dit ce que la
  résolution en fait (arrondie, ou disparue) ; Générer le dit aussi. »
- *Sélecteur de langue, gestion des utilisateurs, résumé par zone* : « 1.12.7 : Contenu coche les langues du
  projet, les boutons, les zones (les groupes d'alarmes, ceux des objets compris). Rien de coché : toutes. »
- *Instrument ISA* : « 1.12.7 : Fonction se choisit dans la liste des lettres ISA ; choisie, elle propose son unité
  (PT : bar, TT : °C), sauf si l'unité a été tapée à la main. »
- *Vanne 3 voies* : « 1.12.7 : une position fixe se choisit (fermée, 1-2, 1-3, 2-3) ; une variable garde ses codes. »
- *Connexion* : « 1.12.7 : la vue après connexion se choisit dans les vues, et suit le renommage d'une vue. »
- *Tableau* : « 1.12.7 : avec Lignes depuis, Contenu montre les colonnes de la source : cochez celles qui se
  montrent, ordonnez-les, donnez-leur un titre (Débit (m³/h)) et un format (0.0). Rien de réglé : toutes. »
- *Compteurs de production* : « 1.12.7 : Débuts de poste = « calendrier » : le compteur suit le calendrier des
  postes du projet (Configuration › Exécution). »
- *Repères* : « 1.12.7 : une liste à choix accepte aussi un nom tapé (« Taper un nom ou un repère $…$ ») ; un
  repère y est gardé et montré en ambre, comme dans toute case. »

### Ce que Générer vérifie de plus (lot D)

Tous des avertissements ou des informations : rien ne bloque.
- deux choix qui écrivent la même valeur (« l'objet ne sait pas lequel montrer ») ;
- une plage du programmateur plus courte qu'une case de la résolution (avertissement : elle disparaît) ou
  arrondie (information : « 07:10-12:00 arrondie à 07:00-12:00 ») ;
- deux zones d'un bargraphe ou d'un cadran qui se chevauchent ;
- un compteur qui suit le calendrier des postes alors que le projet n'en a pas ;
- une colonne choisie d'une source de tableau que la source n'a pas (elle ne se montrerait pas) ;
- un groupe d'alarmes tapé avec un repère (`$Zone_A$`) se lit sans ses `$` (avant : la fausse alerte « aucune
  alarme du groupe $Zone_A$ ») ;
- les zones en % de l'échelle sont relues avec Min et Max (hors de l'échelle, illisibles : comme avant).

L'alignement des listes parallèles (noms, unités, formats, valeurs, libellés) et les couleurs (aussi dans les
états et les zones) étaient déjà vérifiés par la 1.12.6 (B5) ; les tableaux de Contenu écrivent des listes alignées
par construction.

### Les essais (lot D)

- `hmi_editor_test` : `lotD1127_contenu` (les choix d'un sélecteur et d'une liste déroulante, l'unité et le format
  d'un tableau de variables, les états d'un chronogramme, les zones d'un bargraphe en %, les plages d'un
  programmateur, les cases à cocher des langues, des boutons et des zones, les colonnes d'une source de tableau,
  les repères gardés case par case) et
  `lotD1127_choix` (le groupe d'un bandeau : la liste, puis « Taper un nom ou un repère » au clavier et à la souris,
  `$Zone_A$` gardé et montré en ambre ; la vue après connexion ; les lettres ISA et leur unité ; la vanne 3 voies ;
  l'onglet montré par son nom ; le calendrier). `HMI_TEST_1127=1` les lance seuls.
- `hmi_test` : `lotD1127` (les zones en %, l'arrondi du programmateur, les colonnes choisies d'une source en marche
  et leur colonne inconnue dite, le calendrier enregistré et relu, un projet
  sans calendrier inchangé, Générer : doublons, plage disparue, zones qui se chevauchent, groupe repéré, calendrier
  absent, rien de bloquant ; le compteur qui prend son poste au calendrier en marche). `HMI_TEST_1127=1` le lance seul.
- `renameplan_test` : la vue après connexion suit le renommage, un repère `$Vue_A$` aussi, `Vue_AB` ne bouge pas.

### Ce qui n'est pas fait (lot D)

- **Les colonnes d'une source de tableau** : faites (choix, ordre, titre, format), mais pas de largeur par colonne
  (les largeurs restent celles du tableau, par position) ; Contenu propose les colonnes que donne le type de la
  variable (une variable IHM) - une source qui est un paramètre de popup ou un membre se règle en tapant
  `sourceColumns` dans l'inspecteur (`Nom; Debit = Débit | 0.0`).
- **La couleur par choix** d'un sélecteur (« proposé » au § 2.4) : pas de colonne Couleur (le moteur n'a pas de
  couleur par choix).
- **Un « ; » dans un libellé** : refusé dans la case (comme les onglets et les états), pas d'échappement.
- **Les vues de la barre de navigation dans l'inspecteur** restent une case de texte (Contenu les choisit déjà) ;
  le nombre de l'onglet montré en formule garde son numéro.
- **Le tableau des sommets** (lignes, polygones, zones du plan) : c'est le lot H.
- Les listes à choix acceptent un nom tapé par le dernier choix de la liste ; elles ne filtrent pas pendant qu'on
  tape dans la liste elle-même (le champ, une fois ouvert, propose les choix qui commencent comme ce qui est tapé).

## Lot E : les tailles dynamiques

### Ce qui change pour vous (notes de version)

- **Le joker `[*]`** (Graphiques) : dans une liste de formules, `Cuves[*].Niveau` donne un élément par indice de
  `Cuves` - une plume de courbe, une barre, une part, un axe de radar (et sa consigne), une courbe Y, une ligne
  de chronogramme, une ligne de tableau de variables. La source : un tableau de l'IHM (`ARRAY[1..8]` : 1 à 8 ;
  à deux dimensions, toutes ses cases), une `LIST` ou un `VECTOR` (le nombre suit la liste en marche : une plume
  de plus au cycle suivant), une `MAP` (ses clés), une énumération du projet (ses valeurs). Les listes
  parallèles suivent : un nom à joker (`Cuves[*].Nom`) se calcule pour chaque élément, un nom simple prend le
  numéro de l'élément (« Cuve 1 »), la couleur tourne dans la palette, l'unité et le format se répètent.
  Avant : pour 8 cuves, 8 plumes, 8 noms, 8 couleurs à taper.
- **Les vues, les zones, les onglets depuis une source** (Navigation) : la barre de navigation lit ses vues dans
  une liste (`Vues[*]` : une `LIST OF STRING` ou un `ARRAY OF STRING`), le résumé par zone ses zones
  (`Zones[*].Nom`), le conteneur à onglets ses onglets (`T_MODE[*]` : un onglet par valeur de l'énumération, son
  texte). Le clic suit ce qui est montré.
- **Les états depuis une énumération** (Objets) : Contenu › « Depuis une énumération » remplit les états d'un
  voyant ou d'un texte multi-états (un état par valeur, reconnu par son nom, son texte, une couleur de la
  palette ; choisie par une variable de ce type, elle devient la Valeur), d'un chronogramme (par numéro), d'une
  image animée (une condition `Etat = T_ETAT#Marche` par valeur, les images à choisir ensuite) et les onglets
  d'un conteneur (un onglet par valeur, la variable de ce type : sans décalage). Ctrl+Z rend ce qu'il y avait.
- **« Répéter depuis »** (Symboles) : une instance de symbole se pose une fois par élément d'un tableau ou d'une
  `LIST` (`Cuves`) ; ses arguments qui citent `[*]` (`Cuve := Cuves[*]`) reçoivent l'élément ; les copies se
  suivent (horizontal ou vertical, à l'écart donné). La première garde son nom et son identifiant, les autres
  s'appellent `Cuve_2`, `Cuve_3`… Les alarmes du symbole sont générées pour chaque copie (d'un tableau de l'IHM).
  « 8 cuves, 8 pompes » : une instance et une ligne d'argument.
- **Une liste depuis un tableau de structures** (Commandes) : « Éléments depuis » `Pompes[*].Nom` montre le
  membre choisi de chaque élément et écrit son indice (avant : un `ARRAY OF STRUCT` donnait une liste vide, sans
  un mot ; Générer le dit maintenant et propose `Pompes[*].Nom`).
- **Les colonnes d'un tableau de structures imbriquées** (Affichage) : « Lignes depuis » un `ARRAY OF STRUCT` dont
  un membre est une structure ou un tableau montre une colonne par membre simple (`Pompe.Debit`, `Mesures[0]`) ;
  avant : une colonne `Pompe` aux cases vides.
- **Repères** : `$Cuves$[*].Niveau` - le repère varie à la duplication (Dupliquer remplace `$Cuves$` par
  `$Ballons$`), le joker parcourt la variable (3 barres pour les cuves, 2 pour les ballons).

### Pour le guide (paragraphes courts)

- *Courbe, graphiques, tableau de variables* : « 1.12.7 : le joker `[*]` - `Cuves[*].Niveau` donne une plume (une
  barre, une ligne) par cuve ; `Cuves[*].Nom` en Noms les nomme. Un tableau de l'IHM, une LIST (le nombre suit la
  liste en marche), une MAP (ses clés) ou une énumération. Une source inconnue ne donne rien (Générer le dit). »
- *Barre de navigation* : « 1.12.7 : Vues `Vues[*]` : les vues d'une LIST OF STRING, dans son ordre. »
- *Conteneur à onglets* : « 1.12.7 : Onglets `T_MODE[*]` : un onglet par valeur de l'énumération ; Contenu ›
  Depuis une énumération fait de même et relie la variable de ce type. »
- *Résumé par zone* : « 1.12.7 : Zones `Zones[*].Nom` : les zones lues dans un tableau de structures. »
- *Voyant et texte multi-états, image animée, chronogramme* : « 1.12.7 : Contenu › Depuis une énumération : un
  état par valeur (son texte, une couleur), la variable choisie devient la Valeur. »
- *Instance de symbole* : « 1.12.7 : Répéter depuis `Cuves` - une instance par élément ; dans les Arguments,
  `[*]` désigne l'élément (`Cuve := Cuves[*]`). Les copies se suivent (sens, écart) ; la première garde le nom de
  l'instance, les autres Nom_2, Nom_3… »
- *Liste, liste déroulante, boutons radio, sélecteur* : « 1.12.7 : Éléments depuis `Pompes[*].Nom` : le membre
  montré d'un tableau de structures ; le choix écrit l'indice. »
- *Repères* : « 1.12.7 : `$Cuves$[*].Niveau` - Dupliquer remplace le repère, le joker parcourt la variable. »

### Ce que Générer vérifie de plus (lot E)

Des avertissements :
- la source d'un joker inconnue, ou sans éléments (`Niveau[*]` sur un REAL) : « la liste sera vide » ;
- une instance répétée dont la source est inconnue, ou dont aucun argument ne cite `[*]` (toutes les copies
  montreraient la même chose) ;
- « Éléments depuis » un tableau ou une liste de structures sans membre choisi (avec la proposition
  `Pompes[*].Nom`).
Et un élément à joker se vérifie sur son premier élément (`Cuves[1].Niveau` : un membre inconnu reste dit) ;
avant, `Cuves[*].Niveau` était une expression illisible.

### Les essais (lot E)

- `hmi_test` : `lotE1127` (le joker sans moteur ; en marche, un graphique en barres sur un `ARRAY[1..3]` de
  structures et ses noms, ses valeurs ; un tableau de variables `$Cuves$[*].Niveau` et son unité ; une courbe sur
  une `LIST` qui grandit en marche, la plume de plus tracée ; une barre de navigation sur une `LIST OF STRING` et
  son clic ; des onglets depuis une énumération ; une liste depuis `Cuves[*].Nom` ; Générer sans erreur, une
  source inconnue dite), `repeterDepuis1127` (trois instances, leurs noms, leurs arguments, leur place, leurs
  valeurs, l'alarme du symbole par copie, l'avertissement sans `[*]`) et `colonnesImbriquees1127`.
  `HMI_TEST_1127=1` les lance avec ceux du lot D.
- `hmi_editor_test` : `lotE1127_editeur` (les états d'un texte multi-états, d'un chronogramme, d'une image animée
  et les onglets depuis une énumération ; « Répéter depuis » dans l'inspecteur d'une instance, un repère tapé).
- `hmi_reperes_test` : `repereEtJoker1127` (`$Cuves$[*].Niveau` dupliqué en `$Ballons$[*].Niveau`, chacun déplié
  sur sa variable en marche).

### Ce qui n'est pas fait (lot E)

- **Dans l'éditeur**, un joker n'est pas déplié (pas de moteur) : la courbe, le graphique, la barre montrent une
  seule plume ou un seul bouton au nom de la formule ; une instance répétée se voit une fois. Le dépliage se voit
  en simulation.
- **Les sources « … depuis » choisies dans une liste** (une case « Séries depuis ») : le joker se tape dans la
  formule ; il n'y a pas de case à choix dédiée pour les graphiques.
- **Une STRUCT seule comme source** (« une barre par membre ») et **un dossier de variables** (« toutes les
  variables du dossier » d'un tableau de variables) : non faits.
- **Une MAP dans un graphique** : les clés deviennent des éléments (`Stock[*]` : une barre par clé, nommée par sa
  clé si le nom le demande) ; pas de libellé automatique depuis la clé sans nom.
- **Un tableau à deux dimensions** se déplie case par case (`M[*]` : toutes les cases), pas « une série par
  ligne ».
- **« Répéter depuis » une LIST** : les copies suivent la liste en marche, mais les alarmes du symbole ne sont
  générées que pour un tableau de l'IHM (la taille d'une LIST n'est connue qu'en marche). Pas de grille (lignes et
  colonnes) : un seul axe. La ligne d'un paramètre de l'inspecteur peut prendre `Cuves[*]` pour une constante :
  écrire l'argument dans la case Arguments, ou `=Cuves[*]`.
- **Les jokers à travers un paramètre de popup** (`Cuve.Mesures[*]` dans une popup ouverte pour `Cuves[2]`) sont
  résolus par la portée de la vue ; non essayé au-delà d'un niveau.
- **Les vues, les zones, les onglets d'une source** : un texte calculé qui contient « ; » est remplacé par « , »
  (il couperait la liste).
