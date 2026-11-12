# 1.50.0 : les méthodes des types IHM comme les fonctions, « Étirer », POINTER TO, « Dupliquer » pour 500 marques, les tableaux partout, les scripts C/C++, répondre dans le chat

Fait à partir de ta liste (12/11) et des quatre maquettes que tu as validées (tes réponses : « Elle s'écrira »
FUNCTION ; « Étirer » : une solution efficace ; POINTER TO, un vrai pointeur ; « Dupliquer » : i = la 1re profondeur,
j = la 2e).

## 0. En bref (dans les Studios)

**Les méthodes des types IHM = les fonctions des symboles** (maquette 01)

- « Ajouter une méthode… » ouvre **la même fenêtre** que « Nouvelle fonction du symbole » : Nom, Génériques, Retour,
  Unité, Portée (publique / privée), Virtuelle, Description, et « Elle s'écrira `FUNCTION PUBLIC VIRTUAL Demarrer : BOOL` ».
- L'éditeur d'une méthode a **les mêmes onglets** : Code, Paramètres, Locales, Constantes, Tests. **SELF** est la
  première ligne des Paramètres (grisée, verrouillée).
- **Les tests d'une méthode** partent d'une instance d'essai du type (« SELF avant » règle ses membres) ; « Lancer les
  tests », la colonne Tests de la liste.
- **Tes projets se convertissent seuls** à l'ouverture : les blocs VAR écrits dans le corps d'une méthode passent dans
  Locales / Constantes ; rien n'est perdu, le format du fichier ne change pas.
- L'aide à la saisie propose les méthodes après `Pompe1.` ; une méthode privée y est grisée.

**« Étirer »** (maquette 02)

- Clic droit d'un objet de la vue › **Étirer ›** (juste après Aligner), aussi dans la barre d'outils de la vue (« Étirer ▾ »)
  et le clic droit de l'explorateur : jusqu'au bord gauche / droit / haut / bas de la vue, toute la largeur, toute la
  hauteur, toute la vue ; jusqu'à l'objet voisin, au bord de la sélection, à un repère, à une colonne de la grille ;
  « Garder les marges » ; la fenêtre « Étirer… » pour régler finement.
- **L'aperçu au survol** (en pointillés turquoise, avec « + 396 px · largeur 412 → 808 ») ; rien ne bouge avant le clic ;
  **un seul Ctrl+Z** ; la barre d'état dit ce qui a changé.
- **Ctrl+Alt+Maj+flèche** : jusqu'au bord gauche / droit / haut / bas (ils étaient libres).
- Un groupe s'étire en entier ; un objet tourné, par sa boîte ; une ligne, dans son sens ; un objet verrouillé : grisé
  avec la raison. La marge gardée : celle de la grille de colonnes quand elle est affichée, sinon le pas d'aimantation
  (10 px) — la vue n'a pas de « marge » à elle.

**POINTER TO, un vrai pointeur** (maquette 03)

- **« Choisir un type »** a une ligne **Accès** : Direct (la valeur), REF_TO (une référence), **POINTER TO** (un
  pointeur), pour tout type, `Object` compris ; la ligne « S'écrira : `Cible : POINTER TO Object;` » suit en direct.
- **« Le pointeur vise : chaque élément / le tout »** pour une liste ou un tableau : `LIST OF POINTER TO Object`
  n'est pas `POINTER TO LIST OF Object`.
- « ✓ permis ici », ou **grisé avec la raison** là où un pointeur n'a pas de sens : une constante (« une constante ne
  vise rien (NULL seul) »), une variable IHM, un paramètre de popup ou de symbole, un opérande.
- **Il marche pour de vrai** : `p := ADR(x);` puis `p^` lit et écrit la chose visée ; `NULL` tant qu'il ne vise rien ;
  `p = q` compare ce qu'ils visent. La recherche comprend « pointer to obj ».
- Tu avais écrit `POINTER OF` : il est lu comme `POINTER TO`, avec un avertissement qui propose la bonne écriture.

**« Dupliquer » pour 500 repères** (maquette 04)

- **Les copies se rangent** en suite (n = 1 à N) ou en **grille i × j (× k)** : i est toujours la 1re profondeur,
  j la 2e, k la 3e (tes réponses) ; jusqu'à 500 copies, comme avant.
- **Les repères se groupent** : une ligne par profondeur d'indice (`Armoires[.]`, `…Departs[.]`), les adresses (`%MW`,
  `%M`), les textes, les variables, les noms des objets. La liste défile, rien n'est plus tronqué : la recherche, les filtres,
  « Grouper par », « Tout déplier / replier ».
- **Chaque groupe a une règle** : « fixe » (garder) ou **une formule**, le même principe que « Modifier l'adresse dans
  toutes les cases… » : `%MW(1000 + 10 * i)`, `o + j` (o : la valeur de l'original), `+ - * / MOD`, parenthèses ; dans
  un texte `{i+1}`, `{j:02}` (deux chiffres). Ne faire varier que certaines profondeurs : les autres restent « fixe ».
  Un repère peut avoir sa règle « à part ».
- **L'aperçu**, recalculé à chaque changement : les totaux, les 5 premières copies et les 3 dernières, l'avant / après
  de la ligne choisie, puis les conflits ; **l'onglet Tableau** se remplit comme un tableur (une
  ligne par copie) pour les cas à la main.
- Une adresse ou un nom déjà pris **avertit** (le nom devient `Nom_2`), un indice hors des bornes du tableau
  **bloque** avec la raison. **Un seul Ctrl+Z** retire toutes les copies.
- Sans rien régler, le résultat est exactement celui d'avant.

**Les tableaux, partout pareil** (ta question : « pourquoi à des endroits je peux faire ARRAY[0..3] OF ARRAY[0..15] OF
BOOL et pas à d'autres »)

- **La cause** : plusieurs lectures du type coexistaient ; celle des variables, des expressions et des chemins
  n'acceptait pas un tableau dans un tableau. **Il n'y en a plus qu'une**, utilisée partout : les variables, les
  paramètres, les locales, les constantes, les types IHM, les champs, « Choisir un type » (le texte tapé), les valeurs
  initiales et les expressions.
- `ARRAY[0..3] OF ARRAY[0..15] OF BOOL` **et** `ARRAY[0..3, 0..15] OF BOOL` sont acceptés tous les deux ; `M[i][j]` et
  `M[i, j]` désignent la même case ; `M[i]` d'un tableau à deux dimensions est sa ligne.
- Les fautes en clair : « la borne haute est sous la borne basse », « tableau trop grand (plus de 1 000 000 de
  cases) », « X a 2 dimensions : 2 indices… X[i, j] ou X[i][j] ».

**Les scripts C et C++ se compilent et s'exécutent**

- **La cause** : ils n'étaient jamais compilés ni lancés (le Studio n'embarque pas de compilateur, la simulation les
  sautait).
- **Maintenant** : F7 les compile, la simulation les exécute, sous Windows comme ailleurs, sans compilateur à
  installer. Ce qui est pris : les types de base, les tableaux `t[N][M]`, if / switch / while / do / for (et le for
  sur un tableau), les fonctions, les pointeurs (`POINTER TO`), `printf` vers la Console, les variables IHM par leur
  nom.
- **Refusé avec la faute à la bonne ligne** : class, template, lambda, new, malloc, goto, sizeof, le calcul sur les
  pointeurs, les structures, un case sans break. Ce qui est pris est dit dans la fenêtre « Nouveau script », le
  commentaire d'en-tête du script et le guide.

**Le chat de la session** (l'onglet Session et la fenêtre de la session)

- **Répondre** à un message (le bouton au survol, le clic droit) : « En réponse à Léa : « … » » au-dessus de la saisie
  (✕ ou Échap pour annuler) ; la réponse montre l'extrait cité ; un clic dessus ramène au message d'origine.
- **Réduire / déballer** : au-delà de 5 lignes, les 4 premières et « … voir tout » ; « Réduire » ; « Tout réduire ».
- **Copier** (Ctrl+C, le clic droit) ; « Copier avec l'auteur et l'heure » (« 14:32 Léa Essai : … »).
- Un Studio plus ancien dans la même session reçoit la citation en première ligne du message.

## 0 bis. Dans l'Admin

- **L'installateur XPG Admin** : la page « Code d'invitation » ne se chevauche plus (la consigne entière, l'étiquette
  au-dessus du champ, le message d'erreur sous le champ) ; le Récapitulatif des deux installateurs revient à la ligne au
  lieu de couper les lignes longues.
- « Repartir de zéro » est proposé au premier lancement d'un Admin+ 1.49 **ou plus récent** (si tu passes directement à
  la 1.50, tu le vois quand même, une seule fois).

## 1. Ce qu'il te reste à faire

1. Installer **l'Admin+ 1.50.0** (par-dessus la 1.49.0), puis les **Studios 1.50.0**.
2. **Service en ligne › Déployer le service** (la 1.50.0 ; rien de nouveau côté service, il suit le numéro).
3. **Versions** : publier la 1.50.0 avec ses trois fichiers (Studios, Admin+, XPG Admin).
4. Ouvrir un projet qui a des types IHM avec des méthodes : la conversion se fait seule (tu peux vérifier les onglets).

## 2. Les essais

- **Linux : 198 suites sur 198 réussies** (`ctest`, sur la version livrée ; journal `essais-1.50.0.txt`). Celles de cette version :
  - **les méthodes** (`hmieditor`, 11 341 contrôles en tout, dont la partie `methodes150`) : « Ajouter une méthode… » ouvre la
    fenêtre des fonctions, les cinq onglets, SELF en tête des Paramètres, une méthode privée / virtuelle, ses tests
    lancés sur une instance d'essai, un projet d'avant converti à l'ouverture (ses VAR dans Locales / Constantes, ses
    tests réussis avant et après, le même format de fichier, relu sans rien bouger une 2e fois), Ctrl+Z ;
  - **Étirer** (`hmieditor`, ses deux parties « Étirer - le moteur » et « l'éditeur ») : chaque cible (les quatre bords, toute la
    largeur / hauteur / vue, l'objet voisin, la sélection, un repère, une colonne), l'aperçu sans rien bouger, un seul
    Ctrl+Z, un groupe, un objet tourné, une ligne, un objet verrouillé grisé, Ctrl+Alt+Maj+flèche ;
  - **Dupliquer** (`hmidupliquer`, 121 contrôles) : les formules (i, j, k, n, o), un nom pris (avertit), un indice hors
    des bornes (bloque, en tête des conflits), sans rien régler le même résultat qu'avant, **512 repères × 31 copies** (les compteurs, les profondeurs, les formules ; ouvrir 284 ms, la grille 4 × 8
    139 ms, une règle 124 ms, Dupliquer 314 ms, dans la version de mise au point, plus lente que la livrée), l'écran
    (le texte dans son cadre, la liste et le tableau qui ne dessinent que ce qui se voit) ;
  - **POINTER TO** (`pointeurs`, 54 contrôles) : la ligne Accès de « Choisir un type », chaque élément / le
    tout, les grisés et leur raison, `ADR` et `p^` en lecture et en écriture (INT et Object), NULL, un pointeur qui ne
    vise plus rien, l'avertissement de `POINTER OF` ;
  - **les tableaux** (`tableaux`, 86 contrôles) : `ARRAY[0..3] OF ARRAY[0..15] OF BOOL` et `ARRAY[0..3, 0..15]`
    dans les variables, les paramètres, les locales, les constantes, les types IHM, les champs, « Choisir un type », les
    expressions ; `M[i][j]` = `M[i, j]` ; les fautes de bornes ;
  - **les scripts C/C++** (`scriptsc`, 76 contrôles) : compilés, exécutés, une variable IHM changée par le script,
    une faute à la bonne ligne, ce qui est refusé ;
  - **le chat** (`session`, 201 contrôles ; `sessionui`, 46) : une réponse et sa citation, un
    Studio plus ancien qui reçoit la citation en première ligne, réduire / déballer, copier (avec l'auteur et l'heure),
    le texte dans son cadre ;
  - **l'Admin+** : `adminrepartir` (85 : la proposition de « Repartir de zéro » aussi en passant directement à
    la 1.50) ;
  - après la suite complète, une ligne corrigée dans une commande des captures (`dupliquer-exemple`, prise pour une
    commande de la fenêtre Dupliquer) : les six suites qui emploient le lanceur de scripts relancées sur la version
    livrée, 6 sur 6.
- **Sous Wine** : les trois programmes construits ; les Studios installés en silencieux par-dessus la 1.49.0 (ta clé
  d'API gardée) ; l'Admin+ par-dessus la 1.49.0 ; **XPG Admin** installé en silencieux avec un code fictif.
  Captures : les pages de l'installateur XPG Admin (`1500_8_…`, la page « Code d'invitation » avec un code fictif tapé,
  jusqu'au Récapitulatif, jamais l'installation) et celles de l'Admin+ (`1500_9_…`) ; **le Studio livré, installé**,
  sur des projets fictifs créés pour l'essai : les méthodes (`1500_11` la fenêtre « Ajouter une méthode… », `1500_12` ses onglets,
  SELF en tête des Paramètres), Étirer (`1500_21` le menu et l'aperçu « + 1350 px », `1500_22` la courbe étirée,
  « largeur 520 → 1870 px (marge 10 px gardée) »), POINTER TO (`1500_31`), le tableau de tableaux accepté (`1500_33`),
  un script C compilé sans faute (`1500_32`) puis exécuté (`1500_34` : « compte = 2 » dans la Console), Dupliquer
  (`1500_41` : la grille 4 × 8, les règles, le repère à part, l'aperçu, les conflits ; `1500_42` : l'onglet Tableau).
  Dans le projet d'essai de Dupliquer, `Armoires` n'est qu'un tableau d'entiers : la fenêtre dit « Armoires[0].Nom
  n'existe pas » et grise Dupliquer — c'est le blocage voulu, pas une panne ; le Studio livré (`1500_00`) ; lancé avec une adresse locale où rien ne répond :
  **ton service n'a pas été appelé**.

## 3. Installer

**Les Studios** : lance `API-IHM-Studio-Setup-1.50.0.exe` (dans le zip) par-dessus la 1.49.0. Tes projets, tes
réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.50.0.exe` | `D3CDC36740EA5A56227E527797631EA202D18F3C5BB47C2E71C8E707F9CD4ED7` | 64 308 622 octets |
| `API-Studio-1.50.0-portable.zip` | `01FB9C35AAEEF0CCCCBED527E544CB54220D97A9677219DC0651E1D440CB3B78` | 250 fichiers |
| `IHM-Studio-1.50.0-portable.zip` | `E6C186B771883C2D8EF84E710F3AD2DF4F1A6FE33585F6DB3893F27F79966811` | 251 fichiers |

**L'Admin (pour toi seul, envoyé à part)** : lance `XPG-License-Admin-Setup-1.50.0-EDITEUR.exe` par-dessus la 1.49.0 ;
le dossier de ta clé n'est pas touché. Ne le donne à personne : pour un nouvel admin, Administrateurs › Ajouter un
admin… lui envoie l'installateur publié dans Versions (sans ta clé).

| Fichier | SHA-256 | Taille |
|---|---|---|
| `XPG-License-Admin-Setup-1.50.0-EDITEUR.exe` | `89D8CDD2FF68D51B14A58414A76433C85B58C89B68B1009B919BF0B277F101B3` | 8 888 168 octets |

**XPG Admin** (pour tes admins, à publier dans Versions) : `XPG-Admin-Setup-1.50.0.exe` (dans le zip).

| Fichier | SHA-256 | Taille |
|---|---|---|
| `XPG-Admin-Setup-1.50.0.exe` | `3594DEFDDC5BECD5BA5B169A5EFED2B8ECA20B8A019BB903B8DDD85804E88312` | 8 168 878 octets |

## 4. Limites

- **Méthodes** : pas encore d'« Essayer » ni de surcharges pour une méthode ; renommer un paramètre d'une méthode
  virtuelle du parent ne se propage pas aux méthodes qui la redéfinissent ; le grisé d'une méthode privée dans l'aide à
  la saisie ne vient qu'après un chemin de variable IHM.
- **Étirer** : « nouveau » est une note grise, pas le badge violet de la maquette ; pas de bouton « NV » dans la barre ;
  la marge gardée est celle de la grille de colonnes ou le pas d'aimantation (la vue n'a pas de marge à elle) ;
  « Garder les marges » se règle par éditeur, pas par projet.
- **Dupliquer** : la liste groupe un seul niveau ; un groupe de textes a une seule règle (« à part » pour un texte
  différent) ; Échap pendant la saisie d'une case de l'onglet Tableau n'a pas été essayé.
- **Le chat** : l'Assistant IA n'a pas changé ; répondre / réduire / copier sont essayés au niveau des messages et de
  l'affichage, pas par une vraie session entre deux PC.
- **Les installateurs** : les pages vérifiées à 96 ppp seulement (pas à 125 % ni 150 %).
- **Les tableaux** : deux dimensions au plus par niveau (`ARRAY[a, b, c]` est refusé ; la faute propose un tableau de
  tableaux) ; 1 000 000 de cases au plus.
- **Les scripts C/C++** : une partie du C/C++ (ci-dessus), traduite et exécutée par le Studio, pas un vrai compilateur ;
  le pas à pas ne les suit pas encore (ils tournent sans, le Studio le dit).
- **Rien sur un vrai Windows** : sous Wine seulement.
