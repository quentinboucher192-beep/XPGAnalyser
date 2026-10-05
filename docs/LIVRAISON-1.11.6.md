# XPGAnalyser 1.11.6 — la simulation : sur la vue actuelle, le clic droit, Expressions en arbre, le forçage par type et bornes

Livrée le 05/10/2026. Elle reprend les quatre demandes faites après la 1.11.5 :

| Votre demande | Où |
|---|---|
| « dans Expressions, être en mode treeview aussi » | Simulation IHM › Expressions |
| « ajouter forçage par borne et type dans Variables IHM et API » | Simulation IHM › Variables IHM, Variables API |
| « ajouter une option "Sur la vue actuelle" en repérant les profondeurs des symboles d'instances » | Esclaves simulés, Variables IHM, Variables API |
| « dans ces 3 onglets, ajouter clic droit avec tout déplier, déplier, replier, tout replier, déforcer, forcer » | Esclaves simulés, Variables IHM, Variables API |

Les livraisons suivantes sont décalées d'un numéro :

| Version | Contenu |
|---|---|
| 1.11.7 | les actions : le script dans une petite fenêtre, le clavier virtuel, Maths, les actions rangées en arbre |
| 1.11.8 | les popups et les fonctions propres à un symbole |

Fichiers livrés : `XPGAnalyser-Setup-1.11.6.exe` (l'installateur), ce document et 9 captures.

## 1. « Sur la vue actuelle »

Une case à côté de la recherche, dans **Esclaves simulés**, **Variables IHM** et **Variables API**. Cochée, elle ne garde que les variables que lit **la vue montrée en simulation et ses popups ouvertes**.

**Ce qui compte comme « lu par la vue » :**
- les propriétés pilotées par une formule ;
- les textes à trous (`T = {Four1.Temperature:0.0}`) ;
- les cases des tableaux ;
- les listes des graphiques et des courbes ;
- les états des images animées ;
- la variable des commandes et des champs ;
- les actions (cible, valeur, condition, expression surveillée) ;
- les scripts de la vue.

**Les symboles, à toute profondeur.** Une instance est dépliée, et le paramètre du symbole se lit dans la variable qu'on lui a donnée. Un exemple avec le test :
- le symbole S_Aff affiche `X + Compteur` ;
- l'instance Aff_1 lui donne `X := Four1.Vannes[2].Position` ;
- la vue lit donc `Four1.Vannes[2].Position` et `Compteur`.

Un symbole dans un symbole suit de la même façon. Par exemple :
- la ligne S_Ligne passe `W` à son vanne S_Vanne (`V := W`) ;
- l'instance Ligne_1 donne `W := Vannes[5]` ;
- la vue lit donc `Vannes[5].Pos`.

**Les autres cas :**
- **Un paramètre de popup** se lit dans sa variable : une popup ouverte pour `Vannes[7]` lit `Vannes[7].Pos`, pas `M.Pos`.
- **Un index calculé** (`V[i].Pos`) couvre toutes les cases : `V[0].Pos`, `V[3].Pos`…
- **Dans Esclaves simulés**, une ligne reste si sa variable est lue. Un registre sans variable est caché.

Le compte en haut le dit : « 3 sur 228 variables ». Quand la vue ne lit rien de l'onglet, la liste le dit aussi : « La vue Vue_STEST ne lit aucune variable API. »

Captures :
- `1116_03_sur_la_vue_actuelle_ihm.png` : Vue_STEST ;
- `1116_07_esclaves_sur_la_vue.png` : Vue_Equipements.

## 2. Le clic droit dans les trois onglets

Sur une ligne, dans Esclaves simulés, Variables IHM et Variables API :

| Entrée | Sur un nœud (Four1, un esclave) | Sur une variable |
|---|---|---|
| **Tout déplier** | tout l'arbre | tout l'arbre |
| **Déplier** | ce nœud | (grisé) |
| **Replier** | ce nœud | son nœud |
| **Tout replier** | tout l'arbre | tout l'arbre |
| **Forcer** | toutes ses valeurs encore libres, à leur valeur du moment (« Forcer les 5 libres ») | elle, à sa valeur |
| **Déforcer** | toutes ses valeurs forcées (« Déforcer les 2 ») | elle |

- Le menu nomme ce sur quoi on a cliqué et combien de valeurs il contient. Une entrée qui ne peut rien faire est grisée, et dit pourquoi.
- **Pour un esclave**, Forcer ou Déforcer un nœud ne fait qu'une seule commande : un seul Ctrl+Z revient en arrière.
- **Dans l'automate**, qui a des dizaines de milliers de cases, Forcer refuse un nœud de plus de 2 000 valeurs : forcer un nœud plus petit.

Captures : `1116_02_clic_droit_variables_ihm.png`, `1116_08_clic_droit_esclaves.png`.

## 3. Le forçage par type et bornes (Variables IHM et Variables API)

Comme pour un esclave simulé, une variable peut suivre un **mouvement** entre deux **bornes**. Elle est tenue (forcée) à la valeur du mouvement à chaque cycle de la simulation. Une nouvelle colonne **Mouvement** apparaît :

- **un clic** sur la cellule ouvre la liste des types :

  | Type | Ce qu'il fait |
  |---|---|
  | constante | une valeur fixe |
  | sinus | va et vient entre min et max sur une période |
  | rampe | monte de min à max sur une période, puis repart |
  | compteur | part d'une valeur et ajoute un pas à chaque période |
  | clignote | passe à 1 pendant un temps, à chaque période |
  | aléatoire | une nouvelle valeur entre min et max à chaque période |
  | étapes | passe d'une valeur de la liste à la suivante à chaque période |

  Un BOOL n'a que constante, clignote et étapes. Le type choisi part de la valeur du moment, à ±20 % autour d'elle.
- **un double-clic** ouvre le champ des bornes. Entrée applique, Échap annule :

  | Mouvement | On tape | Exemple |
  |---|---|---|
  | sinus, rampe, aléatoire | min ; max ; période (s) | `20 ; 80 ; 10` (la période peut manquer) |
  | compteur | départ ; pas ; période | `0 ; 1 ; 1` |
  | clignote | à 1 pendant ; période | `1,5 ; 4` |
  | constante | la valeur | `50` |
  | étapes | les valeurs | `1; 5; 3` |
- la cellule montre le mouvement : « sinus 0,5 → 3 · 8 s » ;
- **décocher Forcer**, choisir « Aucun » ou Déforcer au clic droit arrête le mouvement : la variable redevient libre.

Les formules sont celles des esclaves simulés : un sinus 20 → 80 sur 4 s vaut 80 à 1 s et 20 à 3 s.

Captures :
- `1116_04_mouvement_types.png` : la liste des types ;
- `1116_05_mouvement_bornes.png` : `0,5 ; 3 ; 8` tapé sur gCoef ;
- `1116_06_mouvement_en_marche.png` : gCoef en sinus fait monter et descendre STEST_1, dont Y vaut `gCoef * 100` ;
- `1116_09_api_rampe.png` : une rampe sur `Armoires[0].ana.PT1.mes`.

## 4. L'onglet Expressions en arbre

- **Chaque objet** est un nœud, avec ses propriétés dessous : la propriété, l'expression, la valeur.
- **Une instance de symbole** range les objets de son symbole sous elle, à toute profondeur. Par exemple STEST_1 › Rect › fill.
- **Un nœud** dit combien d'expressions il porte, et combien sont en erreur, en rouge.
- **La recherche** en haut cherche dans l'objet, la propriété, l'expression et la valeur. Elle garde ce qu'elle trouve et ouvre ses nœuds.
- **Le clic droit** propose Tout déplier et Tout replier. Un double-clic sur un nœud le replie ; sur une propriété, il force sa variable, comme avant.

Capture : `1116_01_expressions_arbre.png`.

## Installer

1. Lancer `XPGAnalyser-Setup-1.11.6.exe`. Il met à jour la 1.11.5 installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisir « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.6.exe` (TAILLE) :
`EMPREINTE`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (55 sur 55). Les nouveaux contrôles :

| Test | Ce qu'il vérifie |
|---|---|
| `surLaVueActuelle1116` (hmi_test) | les chemins d'un code (`V[i].Pos` → `V[*].Pos`, ni les appels, ni les textes entre apostrophes), d'un texte à trous, d'une adresse `%MW100` ; le filtre (un préfixe, un joker, `API.`) ; une vue avec un symbole et un symbole dans un symbole ; une popup ouverte pour `Vannes[7]` |
| `surLaVueClicDroit1116` (hmi_editor_test) | la case cochée : 3 valeurs sur 11, dont la variable passée au symbole ; le menu et ses six entrées dans l'ordre ; Tout déplier, Tout replier, Déplier, Replier sur une variable ; le vrai clic droit à la souris ; Forcer et Déforcer un nœud ; dans Esclaves simulés, Forcer un nœud de 5 valeurs puis un seul Ctrl+Z ; « Sur la vue actuelle » côté esclaves ; l'onglet Expressions en arbre (l'instance, son objet, l'expression dépliée, Tout replier, la recherche) |
| `forcageMouvement1116` (hmi_editor_test) | les formules, le champ des bornes (`20 ; 80 ; 4`, `90 .. 10`, une borne seule refusée) ; en marche, Niveau en sinus reste entre 20 et 80 et parcourt la plage ; une écriture ne passe pas ; un BOOL refuse le sinus et clignote ; libérer arrête le mouvement ; les 7 types d'un INT ; côté automate, une rampe 0 → 10 qui tourne, puis libérée |

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.6` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session `session-1116-simulation.txt` a été rejouée sur une copie d'Armoire_Gaz : ce sont les 9 captures.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé en silencieux, pour tous les comptes, par-dessus la 1.11.5 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.6`.
- Seuls les raccourcis du menu Démarrer dont le nom a un accent n'ont pas pu être créés. C'est la limite de Wine déjà vue aux livraisons précédentes, sans rapport avec Windows.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Merci de vérifier en ouvrant un projet. En cas de souci, renvoyez-moi le résultat de `collect_diagnostics.bat`.

Les sources sont dans le dépôt GitHub, branche `claude/hopeful-goodall-h0srq0`, avec la session de captures `tools/sessions/session-1116-simulation.txt`. Son projet est celui de la 1.11.5 (`tools/sessions/preparer-projet-1115.cpp`).
