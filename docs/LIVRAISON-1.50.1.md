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
  (`i == 2`, `p->valeur * 2`) ; `IHM_POINT_ARRET` aussi. La ligne d'arrêt est amenée en vue dans le volet du code
  (aussi pour un script ST plus long que le volet) ; sur la ligne d'arrêt, un pointeur dit sa case
  (« p = -> case 2 sur 4 »).
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

- **Linux : 199 suites sur 199 réussies** (`ctest`, sur la version livrée, passé par paquets de 12 programmes d'essai faute de place sur
  le disque ; journal `essais-1.50.1.txt`). Celles de cette version :
  - **les méthodes** (`hmieditor`, 11 509 contrôles en tout, dont la partie `methodes1501`, 79) : Essayer (la fenêtre
    des arguments et de SELF avant, le résultat, SELF après, ce qui a changé, une méthode privée), les surcharges (la
    fenêtre, Nouvelle surcharge, la liste, l'appel qui prend la sienne, Compiler : ambigu, aucune, même forme ; renommer
    toutes ensemble), la signature d'une virtuelle suivie à tous les niveaux (une question, un Ctrl+Z, refuser ne change
    rien), le grisé d'une méthode privée après chaque sorte de chemin, le privé de surcharges mêlées ;
  - **Étirer** (`hmieditor`, 259 contrôles pour Étirer seul ; `popup`) : le badge violet du menu et de la barre (son
    contraste dans les 44 thèmes), les marges de la vue (enregistrées, relues, un fichier d'avant sans elles, une
    nouvelle vue à 16 px), Étirer jusqu'à elles (le moteur, l'éditeur, le raccourci, la fenêtre), leurs propriétés et
    Ctrl+Z, « Garder les marges » retenu par projet (rouvert, partagé entre éditeurs, à part pour un autre projet) ;
  - **Dupliquer** (`hmidupliquer`, 248 contrôles) : l'arbre à plusieurs niveaux et ses comptes, déplier / replier, une
    règle par repère dans son groupe, Échap / Entrée / Ctrl+Entrée dans l'onglet Tableau (les trois essais échouaient
    sur la 1.50.0), l'exemple des captures valide, et toujours 512 repères dans les temps ;
  - **les tableaux** (`tableaux`, 259 contrôles ; `hmi`, `hmitypes`) : 3 et 4 dimensions partout, les parties
    (`Matrice[1]`), les messages, `+ Dimension`, l'en-tête C, un tableau de 10 millions de BOOL (1,2 Mo, sa copie et
    sa comparaison), le garde-fou de 1 Go, et les essais du moteur relancés avec tous les tableaux en bloc compact ;
  - **les scripts C/C++** (`scriptsc`, 303 contrôles ; `hmidebug`, 366) : chaque nouvelle construction traduite,
    exécutée, ses valeurs vérifiées, puis passée par F7 et le contrôle du projet ; les refus à leur ligne ; au
    débogueur : un point d'arrêt sur une ligne C, Into / Over / Out, une condition et un espion en C, les noms C dans
    les variables, le pas dans un appel virtuel ;
  - **le chat de l'Assistant IA** (`aiassistant`, 351 contrôles ; `sessionui`, 58 : le chat dans son cadre de 280 à
    460 px) : répondre et la citation envoyée, réduire / déballer, les quatre copies, Ctrl+C ;
  - **deux Studios en session sur ce PC** (`sessione2e`, 25 contrôles, nouveau) : deux vrais IHM Studio (chacun son
    dossier, la boucle locale, une adresse de licences où rien ne répond), l'hôte démarre la session et accepte
    l'équipier ; l'équipier écrit un message de 9 lignes, l'hôte le voit réduit et y répond ; l'équipier voit la
    citation (un clic remonte au message), réduit / déballe / tout réduit, copie (seul, avec l'auteur et l'heure,
    Ctrl+C, relus dans le presse-papiers), puis quitte. Aucun code de session n'est créé (la session « accepter »).
    Pour cet essai, la version Linux d'essai (sans fenêtre) a un mode « sans écran » ; il n'existe pas dans les
    versions livrées.
  - après la suite complète, deux corrections du débogueur vues sur les captures (la ligne d'arrêt amenée en vue dans le volet du code ; la valeur d'un pointeur C sur la ligne d'arrêt, « ? » avant) : les suites qui touchent au moteur et au débogueur relancées (`scriptsc` avec 2 contrôles de plus, `hmidebug`, `hmi`, `simdebug`, `simdebugui`, `pointeurs`, `tableaux`, `hmieditor`, `testsunitaires` : 9 sur 9), puis la chaîne Windows refaite ;
- **Sous Wine** : les trois programmes construits ; les Studios installés en silencieux par-dessus la 1.50.0 ; l'Admin+
  par-dessus la 1.50.0 ; **XPG Admin** installé en silencieux avec un code fictif.
  **Les installateurs à 125 % et 150 %** (`1501_E2_…`, à 120 et 144 ppp, chaque page jusqu'au Récapitulatif, jamais
  l'installation) : les Studios (nouvelle installation et par-dessus une ancienne : la note ambre, Reprendre tes
  anciennes données, le poste d'exploitation), l'Admin+ (avec et sans ta clé), XPG Admin (un code d'invitation fictif
  tapé, et son message d'erreur). Les pages de l'Admin+ (`1501_9_…`) et de XPG Admin (`1501_8_…`) à 100 %.
  **Le Studio livré, installé**, sur des projets fictifs créés pour l'essai : les méthodes (`1501_A1` la fenêtre Essayer : l'argument et SELF avant ; `1501_A2` le résultat, SELF après et ce qui a changé ; `1501_A3` deux Regler, Genre « surcharge »), Étirer (`1501_B1` les marges de la vue dans ses propriétés ; `1501_B2` le badge NOUVEAU, « Garder les marges (32 px) » et l'aperçu ; `1501_B3` la courbe étirée, « largeur 520 → 1788 px (marge 32 px gardée) »), Dupliquer (`1501_C1` l'arbre à plusieurs niveaux et la règle propre d'un repère ; `1501_C2` trois textes, trois règles ; Dupliquer n'est plus grisé, un seul avertissement voulu), les tableaux (`1501_D1`-`D2` « Tableau à N dimensions » : `Cube : ARRAY[0..3, 0..7, 0..15] OF BOOL` ; `1501_D3` `ARRAY[0..999, 0..9999] OF BOOL` accepté ; `1501_D4` le script compilé, Grand : Length 10 000 000 ; `1501_D5` la Console : une ligne de 16 cases, une couche de 128, Grand : 10 000 000 de cases), le débogueur C (`1501_D21` le script C avec sa struct et ses pointeurs, compilé sans faute ; `1501_D22` l'arrêt à la ligne 8 sur la condition C `p - mesures == 2`, « total = 5.5 » sur la ligne ; `1501_D23` l'espion `p->valeur * 2` = 9 ; `1501_D24` Suivant : ligne 9, « p = -> case 2 sur 4 »), le chat de l'Assistant IA (`1501_E1_1` la barre « En réponse à Assistant : « … » » ; `1501_E1_2` la demande envoyée avec sa citation ; `1501_E1_3` une réponse réduite ; `1501_E1_4` le clic droit (Répondre, Copier, Copier avec l'auteur et l'heure, Déballer, Tout réduire) ; `1501_E1_5` la réponse déballée). Le Studio livré (`1501_00`) ;
  lancé avec une adresse locale où rien ne répond : **ton service n'a pas été appelé**.

## 3. Installer

**Les Studios** : lance `API-IHM-Studio-Setup-1.50.1.exe` (dans le zip) par-dessus la 1.50.0. Tes projets, tes
réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.50.1.exe` | `FD1D2D4E5F675C2132B6F39167D43E1C877DDBC6E7EB44F7FC55E58308654F6F` | 64 849 769 octets |
| `API-Studio-1.50.1-portable.zip` | `2BFBB1C9771595FE770A98FF87A3BCCDDD2D4D8C710A9E1861C30D824127B812` | 250 fichiers |
| `IHM-Studio-1.50.1-portable.zip` | `BCFCEB83E36FEA642CD6B70241514DF671BFCE19ADABFD56E0EF31DE73D2CD4B` | 251 fichiers |

**L'Admin (pour toi seul, envoyé à part)** : lance `XPG-License-Admin-Setup-1.50.1-EDITEUR.exe` par-dessus la 1.50.0 ;
le dossier de ta clé n'est pas touché. Ne le donne à personne : pour un nouvel admin, Administrateurs › Ajouter un
admin… lui envoie l'installateur publié dans Versions (sans ta clé).

| Fichier | SHA-256 | Taille |
|---|---|---|
| `XPG-License-Admin-Setup-1.50.1-EDITEUR.exe` | `D2090D7E8247A275D014CE8224CD33FCD7822D0796613FAA4C780333557F524C` | 8 886 261 octets |

**XPG Admin** (pour tes admins, à publier dans Versions) : `XPG-Admin-Setup-1.50.1.exe` (dans le zip).

| Fichier | SHA-256 | Taille |
|---|---|---|
| `XPG-Admin-Setup-1.50.1.exe` | `FD23F209F17A100C5200AEB78D56A39CEA1DE2AA2377F630A43A68450993E4BD` | 8 170 864 octets |

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
