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

(à compléter à la fin du lot E)
