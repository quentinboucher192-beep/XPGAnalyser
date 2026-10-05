# XPGAnalyser 1.11.4 — première des quatre livraisons du 05/10

Livrée le 05/10/2026. Les demandes du 05/10 arrivent en quatre livraisons, point par point :

| Version | Contenu |
|---|---|
| **1.11.4** (celle-ci) | la position, la taille, la rotation et les miroirs calculés en simulation ; les repères dans les paramètres ; Équipements sans « Variables liées » ; les barres de défilement qu'on tire |
| 1.11.5 | les esclaves simulés : arbre de profondeur illimitée, onglets Variables IHM et Variables API, recherche, bornes faciles à éditer |
| 1.11.6 | les actions : le script dans une petite fenêtre, le clavier virtuel, Maths, les actions rangées en arbre |
| 1.11.7 | les popups et les fonctions propres à un symbole |

Fichiers livrés : `XPGAnalyser-Setup-1.11.4.exe` (l'installateur) et ce document.

## 1. Une formule sur la position d'une instance la déplace en simulation

**Le défaut (votre capture).** X de STEST_1 = `=$UINTS[0]$`. En simulation, l'instance restait à sa place de conception.

**Pourquoi.** L'onglet Expressions calculait bien X. Mais en marche, une instance ne se dessine pas elle-même : ce sont les objets de son symbole qu'on voit. Ils étaient posés une fois, à la place fixe de l'instance, avant le calcul des formules, et ne suivaient pas.

Les `$` du repère n'y étaient pour rien : ils sont transparents pour le calcul, `$UINTS[0]$` se calcule `UINTS[0]`.

**Maintenant**, après le calcul des formules, tout ce qui en dépend est reposé :

| Où est la formule | Ce qui se passe en simulation |
|---|---|
| X, Y d'une instance | ses objets la suivent |
| largeur, hauteur d'une instance | ses objets sont mis à l'échelle, comme en conception |
| rotation d'une instance | ses objets tournent autour de son pivot |
| miroir X, miroir Y d'une instance | ses objets se retournent |
| un objet **du symbole** qui a sa propre formule de position | la valeur se lit dans le repère du symbole (X = 50 : 50 px à droite du bord de l'instance), puis l'objet est posé dans l'instance |
| un **groupe** (X, Y, taille, rotation, miroirs) | tout ce qu'il contient suit |
| un symbole **dans** un symbole | l'objet le plus profond suit l'instance extérieure |
| un objet simple | déjà juste avant ; vérifié pour les sept propriétés |

Vérifié par des tests sur les sept propriétés (X, Y, largeur, hauteur, rotation, miroir X, miroir Y), pour une instance, un groupe, un objet du symbole avec sa propre formule et un symbole imbriqué. **Sans la correction, 21 de ces contrôles échouent.**

## 2. Les repères et le fx dans les paramètres d'une instance

| On tape | Paramètre | Ce qui est gardé | En marche |
|---|---|---|---|
| `=$UINTS$` (fx) | Value · ARRAY[0..9] OF UINT (reçoit une variable) | `Value := $UINTS$` | la variable UINTS |
| `$UINTS$` (sans fx) | Value | `Value := $UINTS$` : un nom de variable reste la variable | la variable UINTS |
| `$Nom$` (sans fx) | Name · STRING | `Name := '$Nom$'` | le texte `Nom` |

- **Le défaut corrigé.** `$Nom$` tapé sans fx dans un STRING devenait `'$$Nom$$'` (le `$` doublé du ST) : le repère était perdu. Ses `$` sont maintenant gardés.
- Dans l'inspecteur, la case montre le carré **$**, et elle compte dans le filtre « $ Repères ».
- **Dupliquer…** voit les deux repères : `$UINTS$` (une variable) et `$Nom$` (un texte).
- En marche, une action qui écrit `Value[1]` écrit `UINTS[1]`. Une formule `Value[1]` sur une position se lit `UINTS[1]`.

## 3. IHM › Équipements sans l'onglet « Variables liées »

- Une variable IHM liée à un équipement se choisit et se règle dans le **Plan d'adressage**. Le formulaire de droite est le même qu'avant : équipement, adresse (Schneider ou Modicon), type, lecture seule, mise à l'échelle, place.
- **Lier une variable** est dans la barre de l'onglet Équipements et du Plan d'adressage. Il ouvre le Plan d'adressage sur la ligne de la variable.
- Les Variables IHM, avec le bouton « Voir dans Équipements », mènent aussi au Plan d'adressage, sur la ligne.

## 4. Les barres de défilement se tirent à la souris

**La règle, partout :**
- le pouce se prend et se tire ; le glisser continue même si la souris sort du volet ;
- un clic dans la gouttière avance d'une page vers le clic ;
- le pouce s'éclaire au survol.

| Barre | Avant | Maintenant |
|---|---|---|
| **L'arbre du projet** (l'explorateur, à gauche) | pas de barre | une barre ; les compteurs au bout des lignes restent lisibles |
| **L'explorateur d'objets** de l'éditeur IHM | pas de barre | une barre ; l'œil et le cadenas restent cliquables |
| Les listes, les tableaux (deux barres), l'inspecteur | pas de barre | une barre ; l'inspecteur la place dans sa marge, sans couvrir le carré de légende ni la croix |
| L'éditeur de scripts | dessinée ; un appui dessus plaçait le curseur | elle se tire (les deux barres) |
| Les dialogues (panneaux qui défilent) | dessinée seulement | elle se tire |
| L'historique, le choix d'une variable, les macros, la galerie et l'éditeur de thèmes, le rack, Aller à, le centre de simulation, l'aide et ses articles, les nouveautés, le sélecteur de valeur, la carte mémoire, les valeurs simulées | dessinée seulement, 4 à 5 px | 10 px, elle se tire |
| La liste ouverte d'un choix | un clic sautait à la position | le pouce se tire, un clic avance d'une page |

**Pas encore :** quelques volets défilent toujours à la molette seulement, sans barre. C'est le cas de la Bibliothèque de l'éditeur IHM, dont la mise en page des tuiles dépend du défilement. Dites-moi si l'un d'eux vous gêne.

## Installer

1. Lancer `XPGAnalyser-Setup-1.11.4.exe`. Il met à jour la 1.11.3 installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisir « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.4.exe` (15,2 Mo, 15 195 945 octets) :
`5B993981C5985A1C34C39470739DBC46ABB709CB3D4A7DAC3DB564327DF7AC70`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (55 sur 55). Les nouveaux contrôles :

| Test | Ce qu'il vérifie |
|---|---|
| `geometrieEnMarche1114` (hmi_test) | les sept propriétés calculées pour une instance, un groupe, un objet du symbole avec sa formule, un symbole imbriqué, un objet simple |
| `parametresReperesEnMarche1114` (hmi_test) | `Name := '$Nom$'` se lit `'Nom'` ; `Value := $UINTS$` pilote un texte (`{Value[0]}`) et une position (`x = Value[1]`) |
| `symParametresReperes1114` (hmi_editor_test) | `=$UINTS$` et `$UINTS$` dans un paramètre variable, `$Nom$` dans un STRING, le carré $, Dupliquer |
| `barresDefilement1114` (hmi_editor_test) | tirer la barre d'une liste, d'un arbre, d'un tableau (deux axes), de l'inspecteur et d'un panneau, jusqu'en bas, la souris sortie du widget ; le clic dans la gouttière ; la souris rendue au relâchement ; le carré de légende hors de la barre |
| la fiche d'un équipement (hmi_editor_test) | une variable liée choisie dans le Plan d'adressage ; plus d'onglet « Variables liées » |

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.4` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- la session `session-1114-geometrie.txt` a été rejouée sur une copie d'Armoire_Gaz :
  - en simulation, STEST_1 (X = `$UINTS[0]$`, Y = `gCoef * 100`) va en haut à gauche ;
  - STEST_2 tourne de 30° et se retourne ;
  - Groupe_geo avance de 150 et tourne avec ce qu'il contient ;
  - Équipements a 9 onglets, sans « Variables liées » ;
  - les barres de défilement de l'arbre et de l'inspecteur sont visibles.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé en silencieux, pour tous les comptes, par-dessus la 1.11.3 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.4`.
- Seuls les raccourcis du menu Démarrer dont le nom a un accent n'ont pas pu être créés. C'est la limite de Wine déjà vue aux livraisons précédentes, sans rapport avec Windows.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Merci de vérifier en ouvrant un projet. En cas de souci, renvoyez-moi le résultat de `collect_diagnostics.bat`.

Les sources sont dans le dépôt GitHub, branche `claude/hopeful-goodall-h0srq0`, avec la session de captures `tools/sessions/session-1114-geometrie.txt` et son projet (`tools/sessions/preparer-projet-1114.cpp`).
