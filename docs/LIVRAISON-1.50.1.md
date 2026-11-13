# 1.50.1 : les limites de la 1.50.0 corrigées (méthodes, Étirer, Dupliquer, le chat de l'Assistant IA, les tableaux à N dimensions, les scripts C/C++ au débogueur, les installateurs à 125 / 150 %)

Fait à partir de ta demande du 13/11 : « corrige les limites sauf essai complet sous windows ». Chaque limite listée au
§ 4 de la note de la 1.50.0 est reprise ci-dessous ; ce qui reste est au § 4.

## 0. En bref (dans les Studios)

**Les méthodes des types IHM**

- **Essayer** (barre des méthodes) : une fenêtre demande un argument par paramètre, puis la valeur de chaque membre de
  SELF avant l'appel (vide : sa valeur initiale). La méthode tourne sur une instance d'essai (une méthode privée aussi) ;
  les Sorties du panneau du bas montrent le résultat, SELF après l'appel, **ce qui a changé** (`SELF.Debit : 0.0 → 4.0`),
  le journal. Le projet n'est jamais touché.
- **Les surcharges**, avec les règles des fonctions : un nom déjà pris dans « Nouvelle méthode » fait une surcharge (dit
  sous le nom), ou **Nouvelle surcharge** dans la barre ; la liste les montre (Genre « surcharge »). Chaque appel prend
  la sienne par ses arguments, sur un objet comme par son seul nom dans une méthode ; Compiler dit un appel ambigu ou
  qu'aucune ne prend, et deux surcharges de la même forme. Une méthode virtuelle ne se surcharge pas ; renommer renomme
  toutes les surcharges ensemble.
- **La signature d'une méthode virtuelle suit dans ses redéfinitions** : renommer, retyper, ajouter, retirer ou
  déplacer un paramètre, changer le retour. Une seule question nomme toutes les redéfinitions touchées, à tous les
  niveaux ; puis un seul Ctrl+Z annule tout. Un paramètre renommé l'est aussi dans le code des redéfinitions, et
  `SUPER.M(...)` suit. Un nom déjà employé par une redéfinition (une locale) est refusé avant la question.
- **Une méthode privée grisée partout** dans l'aide à la saisie, avec la raison, après tout chemin vers une instance :
  variable IHM, locale, paramètre, membre d'un autre type, `a[i].`, `p^.`, `F().`. Pas dans les méthodes du type ni de
  ses sous-types (`SELF.`, `THIS.`, le nom seul).

**Étirer**

- Le **badge violet « NOUVEAU »** dans le menu (comme la maquette, avec la barre violette à gauche de l'entrée) et
  **« NV »** sur le bouton « Étirer ▾ » de la barre (un point violet si la barre est trop étroite pour les libellés).
- **Les marges de la vue** : propriétés de la vue › section **Marges de la vue** (En haut, En bas, À gauche, À droite ;
  chaque changement, un Ctrl+Z). Une nouvelle vue (et la vue d'accueil d'une nouvelle IHM) a 16 px de chaque côté ; une
  vue existante n'en a pas : elle s'étire exactement comme en 1.50.0. Le cadre des marges se dessine en pointillés
  (caché avec les repères). « Garder les marges » s'arrête à elles et le dit : « Garder les marges (16 px) », ou
  « (haut 16 · bas 16 · gauche 16 · droite 40 px) ».
- **« Garder les marges » est retenu par projet** (comme les règles de l'éditeur) : tous les éditeurs de vues du projet
  le partagent ; ce n'est pas un changement du projet (pas de Ctrl+Z).

**Dupliquer**

- **Les regroupements à plusieurs niveaux** : « Grouper par » (une liste : Profondeur, Profondeur › Objet, Objet,
  Objet › Profondeur, Rien). Par profondeur, un vrai arbre comme la maquette : Armoires[·] (468) › .Departs[·] (448) ›
  .Voies[·] (320) / .Mesures[·] (128), le nombre à chaque niveau, même replié. « Tout déplier / Tout replier » vont à
  tous les niveaux. La liste ne dessine toujours que ce qui se voit (≈ 80 lignes sur 512 repères).
- **Une règle par repère dans son groupe** : double-clic ou F2 sur la règle d'un repère ; il reste dans son groupe, sa
  règle en couleur. Trois textes d'un même groupe, trois règles. Aussi pour les variables, les adresses et les repères
  indexés. L'en-tête du groupe dit « N avec leur règle ».
- **Échap dans l'onglet « Tableau (Excel) »** : il y avait une panne — Échap enregistrait ce qui avait été tapé. Échap
  annule maintenant la case (l'ancienne valeur revient), un second Échap ferme la fenêtre sans rien appliquer ; Entrée
  enregistre et ouvre la case du dessous ; Ctrl+Entrée enregistre la case ouverte avant de dupliquer (elle était perdue).
- **L'exemple des captures** déclare de vrais types IHM fictifs (T_Armoire, T_Depart, T_Voie, T_Mesure) : Dupliquer
  n'y est plus grisé.
- Un changement à connaître : **les tableaux à l'intérieur d'un type IHM ont leurs bornes** dans Dupliquer (avant :
  « inconnues ») ; un indice hors bornes dans un tableau imbriqué bloque donc Dupliquer, comme pour un tableau simple.

**Le chat de l'Assistant IA**

- Les gestes du chat de la session : **Répondre** et **Copier** au survol d'un message ; au clic droit : Répondre,
  Copier (Ctrl+C), Copier avec l'auteur et l'heure, Copier le code (d'un bloc de code, sans les ```), Déballer /
  Réduire, Tout réduire.
- **Répondre** : « En réponse à Assistant : « … » » au-dessus de la saisie (✕ ou Échap l'annule) ; la citation part avec
  ta demande (ses 12 premières lignes) et s'affiche dans ta bulle ; un clic dessus remonte au message cité.
- **Réduire** : un message de plus de 5 lignes montre 4 lignes et « … voir tout » ; un bloc de code n'est jamais coupé ;
  le dernier échange et la réponse en cours restent dépliés.

**Les tableaux**

- **Autant de dimensions que tu veux** par niveau : `ARRAY[0..3, 0..7, 0..15] OF BOOL`, partout (variables IHM,
  types IHM, paramètres, locales, constantes, scripts, en-tête C : `bool Cube[4][8][16]`). « Choisir un type » :
  « Tableau à N dimensions », **+ Dimension** et **−**. Moins d'indices que de dimensions donne une partie :
  `Matrice[1]` est une ligne, `Cube[1]` une couche. La faute dit « Cube a 3 dimensions : 3 indices entre crochets ».
- **Plus de limite d'un million de cases** : un grand tableau (65 536 cases et plus) est rangé en un bloc compact
  (BOOL 1 bit, INT 2 octets, REAL 8…). Mesuré : 10 millions de BOOL tiennent en 1,2 Mo (avant : refusé, ≈ 4 Go) ; la
  copie et la comparaison de tout le tableau, 2 ms. Seule la mémoire compte : au-delà de 1 Go estimé par variable, le
  tableau est refusé avec l'estimation (« environ 1,2 Go en mémoire (1,0 Go au plus par variable) »).
- Au-delà de 100 000 cases, une variable IHM vit dans la mémoire de l'IHM : elle ne se lie pas à un équipement (la
  raison est dite) ; une valeur initiale unique (`FALSE`, `0`) remplit toutes ses cases.

**Les scripts C/C++**

- **Au débogueur, comme les scripts ST** : points d'arrêt dans la marge, F10, F11, Maj+F11, la ligne en cours dans le
  code C ; la pile sous les noms C (`somme`, `Pompe::demarrer`) ; les variables sous leurs noms et types C (les membres
  d'une structure, les cases, `*p`, `this`, une valeur d'enum par son nom) ; conditions d'arrêt et espions écrits en C
  (`i == 2`, `p->valeur * 2`) ; `IHM_POINT_ARRET` aussi.
- **Le C/C++ accepté s'élargit** : struct (imbriquées, `.` et `->`), enum et enum class, typedef et using, const,
  sizeof ; initialiseurs entre accolades et désignés ; macros à paramètres, `#if` / `#ifdef` / `#else` ; switch sans
  break ; `++` et affectations au milieu d'une expression ; pointeurs de fonction ; **pointeurs dans un tableau** (`p + i`,
  `p++`, `p[i]`, `p - q` ; une case hors du tableau est une faute à sa ligne) ; **classes** (constructeurs, `this`,
  public / private, membres static, héritage simple et méthodes virtuelles, destructeurs virtuels) ; **new / delete**,
  malloc / free (un double delete, un objet employé après delete : une faute claire à la bonne ligne) ; lambdas ;
  `std::array`, `std::vector`, `std::string`, `std::cout`.
- Ça reste **un traducteur, pas un vrai compilateur** (pourquoi : le pas à pas au milieu d'une ligne à travers le cycle de
  l'IHM, les boucles sans fin arrêtées, les fautes à la bonne ligne en français, rien à livrer de plus).

**Les installateurs à 125 % et 150 %**

- L'installateur des Studios : une description sur deux lignes ne touche plus le trait turquoise de l'en-tête ; les
  choix de « Que veux-tu installer ? » ne se touchent plus ; le Récapitulatif revient à la ligne au lieu de déborder à
  droite ; la page de fin fait place à une ligne de plus si besoin. À 100 %, les pages sont identiques à avant.
- L'Admin+ et XPG Admin : rien à corriger à ces tailles (toutes leurs pages vues, avec et sans ta clé, avec un code
  d'invitation fictif et son message d'erreur).

## 1. Ce qu'il te reste à faire

1. Installer **l'Admin+ 1.50.1** (par-dessus la 1.50.0), puis les **Studios 1.50.1**.
2. **Service en ligne › Déployer le service** (la 1.50.1 ; rien de nouveau côté service, il suit le numéro).
3. **Versions** : publier la 1.50.1 avec ses trois fichiers (Studios, Admin+, XPG Admin).

## 2. Les essais

@@ESSAIS@@

## 3. Installer

@@EMPREINTES@@

**XPG Admin** (pour tes admins, à publier dans Versions) : `XPG-Admin-Setup-1.50.1.exe` (dans le zip).

| Fichier | SHA-256 | Taille |
|---|---|---|
@@XPGADMIN@@

## 4. Limites

- **Méthodes** : un sous-type ne peut pas ajouter une surcharge d'une méthode de son parent ; retirer un paramètre d'une
  méthode virtuelle laisse ses emplois dans le code des redéfinitions (Compiler les signale, comme dans la méthode de
  base) ; avec des surcharges privées et publiques d'un même nom, le contrôle avant l'exécution laisse passer l'appel
  et c'est l'exécution qui refuse la surcharge privée.
- **Étirer** : pas d'aimantation sur les marges de la vue ; une version d'avant la 1.50.1 qui réenregistre le projet
  perd les marges des vues.
- **Le chat** : l'essai de bout en bout se fait avec deux Studios sur le même PC (par la boucle locale), pas entre deux
  PC.
- **Les installateurs** : sous Wine seulement ; les pages Préparation, Installation et Terminé ne s'atteignent qu'en
  installant (relues dans le code, pas vues) ; à 125 % seulement, Wine coupe de quelques pixels le texte de la fenêtre
  « pour qui ? » et des boîtes Oui / Non (dessinées par Wine, pas par l'installateur) ; un petit écran (fenêtre
  réduite) n'a pas été essayé.
- **Les tableaux** : un tableau lié à un équipement : 100 000 cases au plus ; la simulation d'automate : 100 000
  éléments par variable ; un script ne parcourt pas 10 millions de cases en un seul passage (5 millions d'instructions
  par exécution) ; au-delà de 100 000 cases, un tableau s'affiche par ses 16 premières cases et la rémanence de la
  simulation ne le garde pas.
- **Les scripts C/C++** : un objet local dont la classe a un destructeur est refusé (le créer avec `new`) ; pas de
  `delete[]` d'objets à destructeur, de caractère d'une chaîne (`s[i]`), de `dynamic_cast`, de capture par référence ;
  refusés avec la faute à la ligne : goto, templates, exceptions, héritage multiple, surcharge d'opérateurs, union,
  realloc, calcul sur un pointeur qui ne vise pas un tableau.
- **Rien sur un vrai Windows** : sous Wine seulement.
