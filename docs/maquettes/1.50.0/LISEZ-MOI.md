# Maquette 1.50.0 : méthodes des types IHM, Étirer, POINTER TO, Dupliquer avec 500 repères (proposition)

Quatre demandes du retour 1.50 (les 1, 3, 4 et 6), en maquette d'abord : tu valides, puis on intègre. Rien n'est changé
dans le produit. Les planches sont dans le thème clair de l'IHM Studio (`Theme::xpg`, accent turquoise 0F766E, Inter et
JetBrains Mono) et reprennent les mots d'aujourd'hui (les titres, les boutons, les onglets, les colonnes, lus dans le code).
Les noms sont fictifs (T_Pompe, Vue_Ligne2, API.Armoires…).

Les pages HTML font 1600 px de large ; les PNG sont dans `hd/` (@2x, 3200 px). Le rendu se refait avec
`node rendre.js <dossier> 2` (`1` pour une vérification dans `v/`). Toutes partagent `planches.css`.

## Les planches

- `01` **Les méthodes des types IHM deviennent des fonctions comme les autres.** En haut, ce qui se passe aujourd'hui :
  la fenêtre « Nouvelle fonction du symbole Carte » (ta capture) et ses onglets, à côté de « Nouvelle méthode de T_Pompe »
  (Nom, Retour, une ligne « PARAMÈTRES (nom : type, séparés par ;) », Privée, Redéfinissable) et le corps seul.
  En bas, la proposition : « Ajouter une méthode… » ouvre **la même fenêtre** que les fonctions (Nom, Génériques, Retour,
  Unité, Portée, Virtuelle, Description, « Elle s'écrira »), et l'éditeur a **les mêmes cinq onglets** : Code,
  Paramètres, Locales, Constantes, Tests. SELF est la première ligne des Paramètres, grisée et verrouillée. Les tests d'une
  méthode partent d'une instance d'essai du type (« SELF avant »). Un tableau compare les cinq sortes de fonctions.
- `02` **Étirer, dans le clic droit.** Le vrai menu d'un objet de la vue (Couper… Ordre › Aligner › Transformer ›
  Style › Calque › …) avec **Étirer ›** juste après Aligner ›. Le sous-menu : jusqu'au bord gauche, droit, haut ou bas
  de la vue, toute la largeur, toute la hauteur, toute la vue ; jusqu'à l'objet voisin, au bord de la sélection
  (deux objets ou plus), à un repère, à une colonne de la grille ; « Garder les marges » ; « Étirer… » (la fenêtre).
  Au survol, l'aperçu en pointillés sur la vue (+ 396 px, la marge de 16 px). À côté : un dessin par entrée, la fenêtre
  Étirer…, les règles (groupe, objet tourné, ligne, verrouillé, un seul Ctrl+Z).
- `03` **« Choisir un type » : POINTER TO et REF_TO.** Aujourd'hui le sélecteur n'a qu'une case « REF_TO (une
  référence) ». Proposé : une ligne **Accès** (Direct, REF_TO, POINTER TO) sous la Forme ; le résultat
  `POINTER TO Object` vérifié en direct ; avec une forme, « le pointeur vise » chaque élément
  (`LIST OF POINTER TO Object`) ou le tout (`POINTER TO LIST OF Object`). La recherche comprend un type écrit en entier
  (« pointer to obj »). Un tableau dit où c'est permis et pourquoi c'est grisé ailleurs, et un bout de code montre le ST
  qui en sort (ADR, ^, NULL) et ce que le Studio vérifie.
- `04` **Dupliquer… avec 500 repères.** Aujourd'hui : une ligne par repère et par indice (coupées), une colonne et une case
  à cocher par repère, un seul pas pour tout. Proposé, sur le principe des formules d'adresses
  (`%MW(1000 + 10 * i)`) : des **compteurs** (une suite n, ou une grille i × j : 4 armoires × 8 départs = 31 copies),
  **une règle par profondeur** d'indice (Armoires[·] = i, Departs[·] = j, Voies[·] fixe…), les adresses
  (`%MW(1000 + 10 * i + j)`, `%M(…)`), les textes (`'Départ {i+1}.{j+1}'`) et les noms ; les **512 repères en liste
  groupée** par profondeur, comptée, repliable, cherchable, filtrable, qui défile (virtuelle) ; un repère « à part » peut
  avoir sa propre règle ; l'**aperçu** des 5 premières et 3 dernières copies avec le total (copies, repères,
  remplacements) ; les **conflits** en tête (hors des bornes : bloque ; adresse ou nom déjà pris : avertit). L'onglet
  « Tableau (Excel) » garde la grille d'aujourd'hui, avec la première colonne figée et le défilement.

## Ce qui existe déjà dans le code (lu pour les planches)

- Fonctions : `HmiNewFunctionDialog` (propriétaires Projet, Vue, Popup, Symbole) et `HmiFunctionsPane` avec les onglets
  Code, Paramètres, Locales, Constantes (`HmiCodeTabs`) et Tests (`HmiFunctionTestsPage`).
- Méthodes : `HmiMethodDialog` et `HmiMethodsPane` (une liste, des propriétés, le corps) ; le modèle est déjà le même
  (`HmiType::methods` est une liste de `HmiFunction`) : surtout de l'écran à unifier.
- Clic droit de la vue : `HmiEditor` (Ordre, Aligner, Transformer, Style, Calque) ; pas de geste qui redimensionne.
- Types : le registre (`HmiTypeRegistry`) lit `REF_TO`, `REFERENCE TO` et `POINTER TO` tapés ; l'interpréteur a ADR,
  REF, `^`, NULL ; le sélecteur (`HmiTypePicker`) n'offre que la case REF_TO, permise pour une déclaration et un retour.
  `Object` est un des « Types de l'IHM » (une poignée vers un objet d'une vue).
- Dupliquer : `HmiDuplicateDialog` / `hmi::dup` (repères `$…$`, indices suivis à toutes les profondeurs depuis 1.12.6) ;
  les formules d'adresses : `hmi::addr` et `hmi::types::formulaAddress` (i, j, + - * / MOD).

## Questions pour toi

1. Méthodes : « METHOD » ou « FUNCTION » dans « Elle s'écrira » ? Des génériques par méthode, ou ceux du type suffisent ?
   Une méthode privée : cachée de l'aide à la saisie hors du type, ou grisée ?
2. Étirer : une seule fois (la taille change), ou aussi un **ancrage** qui suit la vue quand elle change de taille ?
   Les raccourcis Ctrl+Alt+Maj+flèche ? La marge d'office : celle de la vue (16 px) ou 0 ?
3. POINTER TO Object : pour toi « un objet ou NULL » (REF_TO suffirait, Object étant déjà une poignée) ou un vrai pointeur
   vers une variable qui tient un objet ? REF_TO / POINTER TO dans les paramètres de popup et de symbole ? Corriger tout
   seul « POINTER OF » en « POINTER TO » (avec un avertissement) ?
4. Dupliquer : i est-il toujours la profondeur 1 et j la 2, ou les relier toi-même ? L'écriture `{i+1}`, `{j:02}` dans
   les textes, ou celle de la Série (`V1{n}`) ? Une adresse déjà prise : avertir (comme « Modifier l'adresse… ») ou
   bloquer ? « Une suite » par défaut quand il n'y a qu'une profondeur ?
