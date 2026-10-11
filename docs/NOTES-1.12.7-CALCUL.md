# 1.12.7 — Les valeurs calculées au démarrage (notes de travail)

## Ce qui est fait

- **Une constante calculée**, comme un `final` de Java : un code ST qui écrit la constante sous son
  nom, une fois, au démarrage. Il tourne après les valeurs de départ des variables IHM (et la
  rémanence), avant les scripts de Démarrage et la première vue.
- **La valeur initiale calculée d'une variable** (choix 4) : une variable Exécution repart de cette
  valeur à chaque exécution ; une Conservée la prend à la première ; une Persistante rendue garde
  la sienne.
- **Les locales du calcul** vivent à part (« Locales, une par ligne : `Nom : TYPE := valeur;   //
  documentation` ») : jamais de `VAR … END_VAR` dans le code. Un tel bloc est refusé :
  « VAR … END_VAR : les locales ne s'écrivent plus dans le code, elles vont dans l'onglet Locales ».
  Une boucle `FOR i := …` sans `i` déclaré reçoit un `DINT`.
- **L'ordre** : les constantes d'abord, chacune après celles qu'elle lit, puis les variables. Une
  boucle (A lit B qui lit A) est refusée par Compiler et arrête le démarrage.
- **Les constantes des autres scripts** se lisent toujours par leur nom qualifié (choix 2),
  `Reglages.Seuils`, dans les scripts, les fonctions, les calculs et les expressions des vues.
  Publiques seulement (« Reglages.Secret est privée : seul Reglages la lit (rends-la publique) »).
  Jamais écrites : `Reglages.Total := 1`, `Reglages.Seuils['x'] := 1` sont refusés (Compiler et le moteur).
- **Une erreur de calcul arrête le démarrage** (choix 1) : la Console dit le calcul et la ligne
  (« calcul Reglages.Total : ligne 1 : clé absente de la MAP »), la barre de la simulation aussi.
  Chaque valeur calculée est dite dans la Console (`Reglages.Seuils = {'bas': 10, ...}`).
- **Les contrôles** (Générer, Compiler) : le code du calcul est vérifié comme un script (noms,
  chemins, syntaxe, avec « calcul de X, ligne N »), plus ses fautes propres : constante du script
  écrite, variable du script lue par une constante, constante d'un autre script inconnue, privée ou
  écrite, paramètre de vue (il n'existe pas au démarrage), variable IHM liée à un équipement
  (avertissement : elle vaut sa valeur initiale), aucune valeur écrite (avertissement).
- **L'écran** : la grille Constantes / Variables a un bouton « ƒx Calcul… » ; la case Valeur montre
  `ƒx = [valeur]` en couleur (en rouge avec la raison quand le calcul échoue) ; une valeur écrite
  dans la case remplace le calcul (Ctrl+Z le rend) ; l'infobulle montre le code, les locales et la
  valeur.
- **La fenêtre du calcul**, comme la maquette : deux onglets, **Code** (l'éditeur ST, avec l'aide à
  la saisie Ctrl+Espace : les noms du projet, du script et des locales) et **Locales** (une grille :
  nom, type, valeur initiale, documentation ; Ajouter, Supprimer, Monter, Descendre) ; à droite, en
  direct (une demi-seconde après la dernière frappe, F5 tout de suite) : l'**Aperçu** (la valeur au
  démarrage, ou l'erreur qui arrêterait le démarrage), ce qu'il **Lit**, qui le lit (**Lu par** :
  les autres calculs, le code de son script, les scripts et fonctions qui lisent `Reglages.X`) et
  ses **Problèmes** (un clic : la ligne dans le code). « ▶ Calculer (F5) », « Revenir au calcul
  d'origine », et « Déplacer dans Locales » quand un bloc `VAR … END_VAR` est collé dans le code.
- **Le déballage** : sous la grille, la valeur à taille de la ligne choisie (LIST, VECTOR, MAP,
  ARRAY 1D/2D avec ses vrais indices, TUPLE, structure ; écrite ou calculée), un élément par ligne, en
  arbre (élément, type, valeur), chaque élément à taille se déplie.
- **Le renommage** suit les calculs (le sien aussi) et `Reglages.X` partout ; **dupliquer** une valeur
  calculée : la copie écrit son propre nom.
- **Le build incrémental** : les constantes publiques d'un script général sont son interface ; un
  script qui lit `Reglages.X` en dépend et se recompile quand elles changent.
- **Le défaut de la MAP** : une constante neuve (`0.0`) passée en MAP, LIST ou VECTOR prend `[]` ;
  une valeur simple n'y va plus (« un MAP[STRING] OF INT prend ses éléments entre crochets »).
- **Le fichier** : `calculee=oui calcul="…"` et une ligne `locale` par locale (index, vues, scripts
  exportés). Additif : le format ne change pas ; une version d'avant ignore ces champs.

## Ce qui reste (dit honnêtement)

- Le type d'une locale s'écrit dans sa case (pas encore le sélecteur de types) ; une faute de
  type est dite par l'aperçu et par Compiler.
- « Lu par » ne cherche pas dans les expressions des vues (fx) : les scripts, les fonctions et les
  calculs seulement.
