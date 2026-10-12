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
  de plus au cycle suivant), une `MAP` (ses clés : une barre ou une part par clé, nommée par sa clé), une
  énumération du projet (ses valeurs), un membre objet (`Recette.Durees[*]`), le paramètre d'une popup
  (`Cuve.Mesures[*]` dans une popup ouverte pour `Cuves[2]` ; un paramètre objet reçu en valeur, `L := [5, 6, 7]` :
  `L[*]`). Les listes parallèles suivent : un nom à joker
  (`Cuves[*].Nom`) se calcule pour chaque élément, un nom simple prend le numéro de l'élément (« Cuve 1 »), la
  couleur tourne dans la palette, l'unité et le format se répètent. Avant : pour 8 cuves, 8 plumes, 8 noms,
  8 couleurs à taper.
- **Toute une structure, tout un dossier** (Graphiques, Production) : `Pompe.*` donne un élément par membre simple
  de la structure (`Pompe.Marche`, `Pompe.Debit`, `Pompe.Moteur.Courant`, `Pompe.Mesures[0]`…), nommé par son
  membre ; `Moteurs[*].*` chaque membre de chaque moteur d'un tableau de structures (« 1 Marche, 1 Debit, 2
  Marche… ») ; `Mesure.*` un élément par élément d'un `TUPLE` (`Mesure.Item1`, `Mesure.Item2`…) ; `Lignes[*].*`
  sur une `LIST` de structures, les membres de son type ; `Ligne/Convoyeur/*` un élément par variable simple
  rangée dans ce dossier de variables ou dessous,
  dans l'ordre du projet (sans casse ; les structures et les tableaux n'y sont pas). Pour « toutes les variables de
  la pompe » ou « toutes celles du convoyeur » dans un tableau de variables, une ligne suffit.
- **Contenu propose les sources** (Graphiques, Production) : la courbe, les graphiques et le tableau de variables ont
  dans Contenu « Ajouter depuis une source » : les jokers que le projet offre (`Cuves[*].Niveau` pour chaque membre
  simple d'un tableau de structures, `Bacs[*]`, `Stock[*]`, `Pompe.*`, `Ligne/Convoyeur/*`, `Cuve.*` pour le
  paramètre structure d'une popup) ; un chemin ou un repère `$Cuves$[*].Niveau` se tape aussi. Chaque ligne à
  joker dit ce qu'elle deviendra : « [*] : 8 éléments de Cuves », « .* : 2 membres de Pompe », « [*] : les
  éléments de Bacs (leur nombre suit la source en marche) », ou ce qui ne va pas avec sa source. La barre de
  navigation a le sien : les `LIST OF STRING` du projet (`Vues[*]`) et ses dossiers de vues (`Process/*`) ; le
  résumé par zone aussi (`Zones[*].Nom`, la case de la source dit ce qu'elle deviendra).
- **L'éditeur montre le dépliage** (Graphiques, Symboles) : un objet à joker se dessine comme il démarrera - une
  barre par cuve d'un `ARRAY`, une plume par élément de la valeur initiale d'une `LIST`, un bouton par vue d'une
  `LIST OF STRING`, une part par clé initiale d'une `MAP` ; une instance répétée montre ses copies à la suite,
  estompées (seule la première se choisit). Avant : une seule barre au nom de la formule.
- **La courbe XY en profil** (Graphiques) : une Variable X à joker (`Profil[*].X`) trace, à chaque échantillon,
  tous les points (`Profil[i].X`, `Profil[i].Y`) de la source - un profil de four, une courbe de pompe relevée,
  un tableau X / Y ; ses courbes Y citent la même source (`Profil[*].Y`, ou `Debits[*]` pour un second tableau
  de même taille). Avant : seulement des points accumulés dans le temps. Sans joker en X : comme avant.
- **Les vues, les zones, les onglets depuis une source** (Navigation) : la barre de navigation lit ses vues dans
  une liste (`Vues[*]` : une `LIST OF STRING` ou un `ARRAY OF STRING`) ou dans un dossier de vues (`Process/*` :
  les vues ordinaires du dossier et de ses sous-dossiers), le résumé par zone ses zones (`Zones[*].Nom`), le
  conteneur à onglets ses onglets (`T_MODE[*]` : un onglet par valeur de l'énumération, son texte). Le clic suit
  ce qui est montré.
- **Les états depuis une énumération** (Objets) : Contenu › « Depuis une énumération » remplit les états d'un
  voyant ou d'un texte multi-états (un état par valeur, reconnu par son nom, son texte, une couleur de la
  palette ; choisie par une variable de ce type, elle devient la Valeur), d'un chronogramme (par numéro), d'une
  image animée (une condition `Etat = T_ETAT#Marche` par valeur, les images à choisir ensuite) et les onglets
  d'un conteneur (un onglet par valeur, la variable de ce type : sans décalage). Ctrl+Z rend ce qu'il y avait.
- **« Répéter depuis »** (Symboles) : une instance de symbole se pose une fois par élément d'un tableau ou d'une
  `LIST` (`Cuves`) ; ses arguments qui citent `[*]` (`Cuve := Cuves[*]`) reçoivent l'élément ; les copies se
  suivent (horizontal ou vertical, à l'écart donné ; « par rangée » : 8 cuves, 4 par rangée, deux rangées). La
  première garde son nom et son identifiant, les autres s'appellent `Cuve_2`, `Cuve_3`… Les alarmes du symbole
  sont générées pour chaque copie (d'un tableau de l'IHM). « 8 cuves, 8 pompes » : une instance et une ligne
  d'argument.
- **Une liste depuis un tableau de structures** (Commandes) : « Éléments depuis » `Pompes[*].Nom` montre le
  membre choisi de chaque élément et écrit son indice (avant : un `ARRAY OF STRUCT` donnait une liste vide, sans
  un mot ; Générer le dit maintenant et propose `Pompes[*].Nom`).
- **Les colonnes d'un tableau de structures imbriquées** (Affichage) : « Lignes depuis » un `ARRAY OF STRUCT` dont
  un membre est une structure ou un tableau montre une colonne par membre simple (`Pompe.Debit`, `Mesures[0]`) ;
  avant : une colonne `Pompe` aux cases vides. Le parcours des membres est celui de `Pompe.*`, et l'aperçu de
  l'éditeur, Contenu (les colonnes à choisir) et Générer voient les mêmes colonnes que la marche.
- **Un tableau depuis une énumération ou une structure** (Affichage) : « Lignes depuis » `T_MODE` donne une ligne par
  valeur (Nom, Valeur, Texte) ; « Lignes depuis » `Pompe` (une structure seule) une ligne par membre simple
  (Membre, Valeur). Avant : Générer refusait l'un et l'autre. Leurs colonnes se choisissent dans Contenu comme
  celles d'une liste.
- **Repères** : `$Cuves$[*].Niveau` - le repère varie à la duplication (Dupliquer remplace `$Cuves$` par
  `$Ballons$`), le joker parcourt la variable (3 barres pour les cuves, 2 pour les ballons).
- **Les renommages suivent** (Renommer) : renommer une variable IHM réécrit ses jokers partout où ils se
  déplient - les plumes et les valeurs (`Cuves[*].Niveau`), les noms calculés (`Cuves[*].Nom`), les vues d'une
  barre de navigation (`Vues[*]`), les zones d'un résumé, les onglets, « Répéter depuis » et ses arguments ;
  renommer une énumération réécrit `T_MODE[*]` et « Éléments depuis » / « Lignes depuis » `T_MODE` ; renommer
  (ou glisser ailleurs) un dossier de variables ou de vues réécrit `Ligne/Convoyeur/*` et `Process/*`. Un nom simple (« Cuves » écrit
  comme libellé) reste un texte. Dans un symbole, les mêmes listes reçoivent les arguments de l'instance.

### Pour le guide (paragraphes courts)

- *Courbe, graphiques, tableau de variables* : « 1.12.7 : le joker `[*]` - `Cuves[*].Niveau` donne une plume (une
  barre, une ligne) par cuve ; `Cuves[*].Nom` en Noms les nomme. Un tableau de l'IHM, une LIST (le nombre suit la
  liste en marche), une MAP (ses clés) ou une énumération. `Pompe.*` : un élément par membre de la structure ;
  `Ligne/Convoyeur/*` : un par variable du dossier. Contenu › Ajouter depuis une source les propose, et chaque
  ligne dit ce qu'elle deviendra. Une source inconnue ne donne rien (Générer le dit). »
- *Barre de navigation* : « 1.12.7 : Vues `Vues[*]` : les vues d'une LIST OF STRING, dans son ordre ; `Process/*` :
  les vues ordinaires du dossier de vues Process. »
- *Conteneur à onglets* : « 1.12.7 : Onglets `T_MODE[*]` : un onglet par valeur de l'énumération ; Contenu ›
  Depuis une énumération fait de même et relie la variable de ce type. »
- *Résumé par zone* : « 1.12.7 : Zones `Zones[*].Nom` : les zones lues dans un tableau de structures. »
- *Courbe XY* : « 1.12.7 : Variable X `Profil[*].X` et courbe Y `Profil[*].Y` : le profil entier, redessiné à chaque
  échantillon (un tableau de points, une LIST qui change). »
- *Tableau* : « 1.12.7 : Lignes depuis une énumération (une ligne par valeur : Nom, Valeur, Texte) ou une
  structure (une ligne par membre : Membre, Valeur) ; un tableau de structures imbriquées : une colonne par
  membre simple (Pompe.Debit). »
- *Voyant et texte multi-états, image animée, chronogramme* : « 1.12.7 : Contenu › Depuis une énumération : un
  état par valeur (son texte, une couleur), la variable choisie devient la Valeur. »
- *Instance de symbole* : « 1.12.7 : Répéter depuis `Cuves` - une instance par élément ; dans les Arguments,
  `[*]` désigne l'élément (`Cuve := Cuves[*]`). Les copies se suivent (sens, écart, combien par rangée) ; la
  première garde le nom de l'instance, les autres Nom_2, Nom_3… L'éditeur les montre estompées. »
- *Liste, liste déroulante, boutons radio, sélecteur* : « 1.12.7 : Éléments depuis `Pompes[*].Nom` : le membre
  montré d'un tableau de structures ; le choix écrit l'indice. »
- *Repères* : « 1.12.7 : `$Cuves$[*].Niveau` - Dupliquer remplace le repère, le joker parcourt la variable. »

### Ce que Générer vérifie de plus (lot E)

Des avertissements :
- la source d'un joker inconnue, ou sans éléments (`Niveau[*]` sur un REAL) : « la liste sera vide » (dans une
  courbe aussi) ;
- `X.*` sur ce qui n'est pas une structure, un dossier de variables sans variable simple, un dossier de vues sans
  vue ordinaire : « aucun élément » ;
- une instance répétée dont la source est inconnue, ou dont aucun argument ne cite `[*]` (toutes les copies
  montreraient la même chose) ;
- « Éléments depuis » un tableau ou une liste de structures sans membre choisi (avec la proposition
  `Pompes[*].Nom`).
La Variable X d'un profil (`Profil[*].X`) se vérifie sur son premier élément ; un joker sur le paramètre d'un
symbole ou d'une popup se lit sur la borne basse que donne le type du paramètre (`Cuve.Mesures[*]`, Mesures en
`ARRAY[1..3]` : `Mesures[1]` ; avant cette correction, la fausse erreur « l'indice 0 est hors des bornes »).
« Lignes depuis » une structure seule ou une énumération est accepté (avant : une erreur) ; une colonne choisie se
vérifie sur les colonnes que la marche montrera (`Pompe.Debit` n'est plus dite absente).
Et un élément à joker se vérifie sur son premier élément (`Cuves[1].Niveau`, `Pompe.Marche` : un membre inconnu
reste dit) ; avant, `Cuves[*].Niveau` était une expression illisible (une erreur bloquante). Le paramètre
structure d'une popup (`Cuve.*`) se vérifie sur son type ; un paramètre sans type, en marche.

### Les essais (lot E)

- `hmi_test` : `lotE1127` (le joker sans moteur ; en marche, un graphique en barres sur un `ARRAY[1..3]` de
  structures et ses noms, ses valeurs ; un tableau de variables `$Cuves$[*].Niveau` et son unité ; une courbe sur
  une `LIST` qui grandit en marche, la plume de plus tracée ; une barre de navigation sur une `LIST OF STRING` et
  son clic ; des onglets depuis une énumération ; une liste depuis `Cuves[*].Nom` ; un camembert sur une `MAP` ;
  ce que Contenu dit et propose ; l'aperçu de l'éditeur ; Générer sans erreur, une source inconnue dite),
  `repeterDepuis1127` (trois instances, leurs noms, leurs arguments, leur place, leurs valeurs, l'alarme du
  symbole par copie, l'aperçu de l'éditeur, deux par rangée, l'avertissement sans `[*]`),
  `colonnesImbriquees1127` (et les mêmes colonnes dans l'aperçu, Contenu et Générer ; un chemin
  `Lignes[2].Mesures` et ses bornes), `membresStruct1127` (`Pompe.*` dans un tableau de variables, un graphique,
  une courbe ; `Moteurs_M[*].*` ; `ligne/convoyeur/*` ; `Niveau.*` et un dossier vide dits), `sourcesObjets1127` (`Cuve.Mesures[*]` et
  `Cuve.*` dans une popup ouverte pour `Cuves[2]`, `Recette.Durees[*]`, `L[*]` d'un paramètre objet reçu en
  valeur), `vuesDossier1127` (`Process/*` dans
  une barre de navigation, son clic, son aperçu, un dossier vide dit) et `lignesDepuis1127` (un tableau depuis une
  énumération et depuis une structure seule, ses colonnes choisies, Générer sans erreur) et `xyProfil1127` (une
  courbe XY en profil, ses trois points refaits à chaque échantillon ; un joker dans un symbole sur un tableau
  qui part de 1, sans fausse erreur de bornes). `HMI_TEST_1127=1` les lance avec ceux du
  lot D.
- `hmi_editor_test` : `lotE1127_editeur` (les états d'un texte multi-états, d'un chronogramme, d'une image animée
  et les onglets depuis une énumération ; « Répéter depuis » et « par rangée » dans l'inspecteur d'une instance,
  un repère tapé ; « Ajouter depuis une source » et le détail d'une ligne à joker dans Contenu ; l'aperçu d'un
  graphique `Cuves[*]` dessiné avec ses 8 barres ; les vues d'une barre de navigation depuis une `LIST OF STRING` ;
  les zones d'un résumé depuis `Zones_E[*].Nom`).
- `hmi_reperes_test` : `repereEtJoker1127` (`$Cuves$[*].Niveau` dupliqué en `$Ballons$[*].Niveau`, chacun déplié
  sur sa variable en marche).
- `renameplan_test` : « 8. les jokers suivent le renommage » (renommer `Cuves` : la plume, le nom calculé, les
  zones d'un résumé, « Répéter depuis » et son argument ; renommer `Vues` : la barre de navigation). Et, dans
  `hmi_test` et `hmi_editor_test` : une énumération renommée (les onglets `T_Mode_E7[*]`), un dossier de vues
  renommé (`Process/*`), un dossier de variables renommé puis glissé dans un autre par l'écran des variables
  (`Ligne/Convoyeur/*`, puis `Usine/Atelier/Convoyeur/*`).

### Ce qui n'est pas fait (lot E)

- **Dans l'éditeur**, l'aperçu suit le projet, pas la marche : une `LIST` montre sa valeur initiale, un nom calculé
  (`Cuves[*].Nom`) le numéro de l'élément ; une copie d'instance répétée ne se choisit pas (la première seule).
  Contenu montre les lignes telles qu'écrites (une ligne `Cuves[*].Niveau`, avec ce qu'elle deviendra).
- **« Ajouter depuis une source »** existe pour la courbe, les graphiques, le tableau de variables, la barre de
  navigation et le résumé par zone ; le conteneur à onglets prend son joker tapé (ou « Depuis une énumération »).
- **Lignes depuis un paramètre de popup structure** (`Cuve`) : une structure seule se lit sur une variable du projet
  ou un chemin (`Lignes[2].Pompe`), pas à travers un paramètre.
- **Un joker et des membres ensemble** (`Moteurs[*].*`) : sur un tableau de l'IHM ; pas sur une `LIST` de
  structures (les membres d'un élément de LIST ne se connaissent qu'en marche : la ligne ne donne rien).
- **Un tableau à deux dimensions** se déplie case par case (`M[*]` : toutes les cases), pas « une série par
  ligne » ; un `TUPLE` se parcourt par `Mesure.*` (pas par `[*]`).
- **La courbe XY en profil** relit toute la source à chaque échantillon (100 ms) : pour des milliers de points,
  la période d'échantillonnage du graphique n'existe pas encore (§ 3.4, une autre demande). Le point « du
  moment » mis en avant est le dernier de la source.
- **Une MAP comme états d'un multi-états** (« clé = valeur de l'état, valeur = texte ») : non ; « Depuis une
  énumération » seulement.
- **« Répéter depuis » une LIST** : les copies suivent la liste en marche, mais les alarmes du symbole ne sont
  générées que pour un tableau de l'IHM (la taille d'une LIST n'est connue qu'en marche). Dans la ligne d'un
  paramètre de l'inspecteur, `Cuves[*]` sur une variable du projet reste une formule ; un repère (`$Cuves$[*]`)
  tapé sans fx pour un paramètre simple (STRING, INT…) peut être pris pour une constante : le taper avec fx
  (`=$Cuves$[*]`) ou dans la case Arguments.
- **Les jokers à travers un paramètre de popup** (`Cuve.Mesures[*]`, `Cuve.*`) sont résolus par la portée de la
  vue ; essayés sur un niveau.
- **Les vues, les zones, les onglets d'une source** : un texte calculé qui contient « ; » est remplacé par « , »
  (il couperait la liste).

### Les risques pour les projets existants (lots D et E)

Aucun format ne change : les propriétés nouvelles (`sourceColumns`, `repeatFrom`, `repeatAxis`, `repeatGap`,
`repeatPerRow`, le champ `postes` de la configuration) sont absentes tant qu'elles ne servent pas, et un fichier
1.12.6 se relit tel quel. Les seuls changements de comportement visibles :
- une valeur qui ressemble à un joker est maintenant dépliée : un nom de vue, d'onglet ou de groupe écrit
  `Chemin[*]`, `Chemin.*` ou `Dossier/*` (aucun n'était valable avant : Générer les refusait ou ils ne
  menaient nulle part) ; un libellé « Toutes [*] » ou un calcul `SEL(...)` ne le sont pas ;
- un tableau « Lignes depuis » un `ARRAY OF STRUCT` à membres imbriqués montre plus de colonnes (une par membre
  simple, au lieu d'une colonne vide) ;
- l'onglet Contenu apparaît pour plus d'objets (sélecteur, liste déroulante, boutons radio, liste, bargraphe,
  cadran, programmateur, sélecteur de langue, gestion des utilisateurs, résumé par zone, tableau à source) ;
- Générer dit plus de choses, toujours en avertissements ou en informations.
