# API Studio et IHM Studio 1.26.0 : les polices (bibliothèque, partout, éditeur), la Corbeille, la bibliothèque d'images, les limites corrigées

Faite à partir de tes demandes du 05/11. Tes mots :
- « corrige les limites restantes » ;
- « fait une corbeille bien rangée avec recherche possible, ajouter des TTF dans les ressources de base + onglet Polices dans bibliothèques éditeur de vue, pouvoir changer de police partout, thème, styles, boutons, objets de la bibliothèque, avoir aperçu des TTF, pouvoir éditer des TTF en live sur le logiciel, en créer, en dupliquer à partir de » ;
- « fait un menu comme ça pour bibliothèques d'images aussi » (le volet de la bibliothèque de sons).

Les maquettes ont été validées (« c'est banger »). Elle se met à jour par-dessus la 1.25.0 et garde tes données.

## 1. Ce qui change pour toi

### Les polices

**La bibliothèque de polices** (ta maquette 2) : IHM › Ressources › **Bibliothèque de polices**.
- **16 polices libres livrées avec le logiciel**, en 9 familles et 3 dossiers, chacune avec un cadenas :
  - **Sans empattement** : Liberation Sans (les chasses d'Arial), DejaVu Sans (très complète), Carlito (celles de Calibri) ;
  - **Avec empattement** : Liberation Serif (celles de Times New Roman), DejaVu Serif, Caladea (celles de Cambria) ;
  - **Chasse fixe** : Liberation Mono (celles de Courier New), DejaVu Sans Mono, JetBrains Mono.
- Leurs licences (SIL Open Font License, licence DejaVu) sont livrées avec elles, dans `resources/polices/licences`.
- **Une ligne par famille**, écrite dans sa propre police, avec ton texte d'aperçu, sa taille, Normal ou Gras. « dans le projet » quand elle est déjà importée.
- **L'aperçu** :
  - « Aa Bb Éé 0123 » en grand, puis les tailles 10, 14, 20, 30 ;
  - la police **sur le fond d'une vue** ;
  - la grille des glyphes ;
  - le fichier, la licence (elle s'ouvre), où le projet l'emploie.
- **Importer dans le projet** (Entrée) : le même contenu déjà là est réutilisé. Ctrl+Z.
- **Mes polices** : tes TTF et OTF, gardées hors du projet, pour tous tes projets. Ajouter à Mes polices…, ou lâche un fichier. Retirer : plusieurs d'un coup, un seul Ctrl+Z.
- **Nouvelle police**, **Dupliquer à partir de…** et **Modifier** ouvrent l'éditeur de polices. Un double-clic sur un glyphe de l'aperçu l'ouvre sur ce glyphe.
- **Dans l'arbre** : Bibliothèque de polices [N] se déplie en Mes polices, les 3 dossiers et leurs familles ; un clic ouvre le volet sur la police.

**La police partout** (ta maquette 3) :
- **Le sous-onglet Polices** de la BIBLIOTHÈQUE de l'éditeur de vues (Objets | Images | Sons | **Polices** | Variables) :
  - « Du projet » d'abord, avec le nombre d'objets qui emploient chaque police, puis toute la bibliothèque ;
  - **glisse une police sur un objet** (ou clique dessus) : il la prend, importée si besoin, un seul Ctrl+Z ;
  - un double-clic : la police va à la sélection ;
  - la recherche de la bibliothèque cherche aussi les polices.
- **La case Police** des objets propose, dans l'ordre :
  - **Police par défaut (X)** et **Police des valeurs (Y)** ;
  - les polices du projet ;
  - la bibliothèque (« Bibliothèque › Sans empattement › Carlito »). Choisir une police de la bibliothèque l'importe et la règle d'un seul geste (un seul Ctrl+Z).
- **Les styles** : la même case Police.
- **Le thème** : Configuration › Affichage, **Police par défaut** et **Police des valeurs**.
  - Tout objet sur « Police par défaut » la prend : la changer change toute l'IHM. Ceux sur « Police des valeurs » (anciennement Mono) prennent la seconde.
  - Les objets de la bibliothèque (boutons, voyants, afficheurs…) arrivent sur « Police par défaut ».
  - Tant que tu ne choisis rien, rien ne change d'aspect dans tes projets.

### L'éditeur de polices (ta maquette 4)

IHM › **Éditeur de polices**, ouvert depuis la bibliothèque de polices (Nouvelle police, Dupliquer à partir de…, Modifier).

- **Créer, dupliquer** :
  - **Nouvelle police** : les lettres, les chiffres, les signes, les accentuées et l'euro, vides, à dessiner ;
  - **Dupliquer à partir de…** : une copie de n'importe quelle police TrueType (fournie, de Mes polices, du projet), sous un autre nom ;
  - une police **fournie** ne change jamais : « Modifier » en ouvre une copie.
- **Les noms réservés** : la licence d'une police copiée suit la copie. Une copie ne peut pas porter les noms que cette licence réserve (Liberation, Carlito ; Bitstream et Vera pour DejaVu), ni le nom de sa source. L'éditeur refuse ces noms et dit pourquoi.
- **À gauche, les glyphes** : la grille, chaque glyphe dessiné avec son code. On cherche un caractère (é), plusieurs, ou un code (U+00E9). Les filtres : Tout, **Modifiés** (un point orange), A–Z, a–z, 0–9, Accents, Symboles.
- **Au milieu, le dessin** :
  - les points **sur la courbe** sont des carrés, ceux **hors de la courbe** des ronds ;
  - les lignes sont tracées : ascendante, capitales, hauteur d'x, ligne de base, descendante, la chasse et les approches ;
  - **Choisir** (V) : clic, Maj+clic, un cadre ; glisser ; les flèches (1 unité, Maj : 10) ; Suppr. Un double-clic sur un point le fait passer sur ou hors de la courbe ; sur le trait, il ajoute un point ;
  - **la plume** (P), **ajouter un point** sans changer la forme (A), **rectangle** (R), **ellipse** (E) ;
  - la **chasse** se glisse aussi ; la molette zoome ;
  - un **composé** (é = e + l'accent) suit son « e » ; **Décomposer** en fait un glyphe à dessiner ;
  - **Copier le glyphe de…** une autre police (mis à l'échelle), **Rétablir l'original**.
- **À droite, les réglages** :
  - le glyphe : sa chasse, ses approches ;
  - le point choisi : x, y, sur ou hors de la courbe ;
  - la police : famille, style, version, hauteurs, interligne ;
  - son origine et sa licence.
- **En bas, l'aperçu en direct** : il se refait à chaque geste, avec ton texte, en trois tailles. Une police du projet ouverte dans l'éditeur se dessine **dans les vues telle qu'elle est dans l'éditeur**, avant même d'être enregistrée (« Les vues suivent »).
- **Ctrl+Z, Ctrl+Y** : chaque geste ; un glissement compte pour un geste.
- **Enregistrer** :
  - **Enregistrer (Mes polices)** (Ctrl+S) ;
  - **Enregistrer dans le projet** : la ressource police est ajoutée ou remplacée, et les objets suivent ;
  - **Exporter en .ttf…** : un fichier que Windows installe ;
  - **Abandonner** revient à la police enregistrée.
- Le fichier écrit est une vraie police TrueType. Ses tables de crénage et de substitution sont gardées. Les 16 polices fournies se relisent à l'identique après écriture, et un contrôle externe les valide (fontTools).

### La Corbeille (ta maquette 1)

- **Où la trouver** : dans l'arbre du projet, **Corbeille [N]** (après Versions). Un clic l'ouvre ; le clic droit propose **Vider la corbeille…**. Ou le menu **Projet › Corbeille…**.
- **Rangée par genre**, chaque dossier avec son nombre :
  - Enregistrements, Incidents, Versions, Macros ;
  - Conversations IA, Pièces jointes IA, Connaissances et souvenirs IA.
- **Les colonnes** : Nom, Supprimé le, Origine (un chemin : le clic droit l'ouvre dans l'explorateur), Taille.
- **Chercher dans la corbeille…** : chaque mot commence un mot du nom, du genre ou de l'origine, sans casse ni accents.
- **Restaurer** (Entrée) remet les éléments choisis à leur place, sans question. Un seul Ctrl+Z les renvoie dans la corbeille.
- **Supprimer définitivement** (Suppr) et **Vider la corbeille** demandent confirmation.
- Ce qui a été supprimé il y a plus de 7 jours part à l'ouverture suivante. Les macros restent jusqu'à Supprimer définitivement.

### La bibliothèque d'images a son volet

IHM › Ressources › **Bibliothèque d'images** : son volet, le jumeau de celui des sons. L'arbre l'ouvre sur le dossier ou l'image cliqués.
- **La liste** :
  - les dossiers et leur nombre, Mes images d'abord ;
  - une vignette devant chaque image, avec un cadenas (native) ou une personne (une des tiennes) ;
  - les colonnes Taille, Format, **Dans le projet**, Mots-clés.
- **Chercher une image…** : la même recherche que le sous-onglet Images.
- **À droite**, le grand aperçu (fond en damier, clair ou sombre) et sa fiche.
- **Importer dans le projet** (Entrée ; Ctrl+Z la retire). L'image déjà importée, dans n'importe quelle couleur, est reconnue et réutilisée.
- **Ajouter à Mes images…** (ou lâche un fichier).
- **Retirer** : plusieurs d'un coup, un seul Ctrl+Z ; une native se garde.
- **Ouvrir dans l'éditeur de vues** (ou un double-clic) : le sous-onglet Images, l'image montrée.

### Les rapports : les limites de la 1.25.0

- **Une police TrueType des ressources dans le PDF** : la police d'une mise en page peut être une police du projet. Elle est intégrée au PDF (ses chasses, ses accents).
- **Les courbes des mesures dans Excel** : une feuille « Courbes des mesures », avec l'heure et une colonne par mesure.
- **Les titres de Standard comme avant** : en noir. La case « Titres en couleur d'accent » les colore, et elle est cochée dans tes mises en page.
- **L'aperçu** garde la largeur que le texte aura dans le PDF.

### Les limites de la 1.25.0, corrigées

**Les chemins** : le clic droit propose maintenant Ouvrir dans l'explorateur, Aller à et Copier le chemin aux endroits suivants :
- les cartes des projets récents de l'accueil ;
- le rapport de plantage (le rapport et le vidage mémoire) ;
- un message de la barre d'état qui contient un chemin ;
- le centre d'aide, Signaler un problème ;
- les fichiers d'une archive (Échanges) ;
- les fichiers déposés ;
- Chercher des fichiers.

**Supprimer plusieurs, un seul Ctrl+Z** :
- l'outil Modbus : plusieurs requêtes de la lecture cyclique ; **Supprimer des jeux…** ;
- les **macros et les versions** de l'arbre et du volet Versions reviennent par Ctrl+Z ;
- **plusieurs équipements** : le même choix que pour un seul (garder ou supprimer leurs variables IHM, leurs esclaves simulés) ;
- **les incidents** et **les listes de l'Assistant IA** vont dans la Corbeille, et un seul Ctrl+Z les rend.

## 2. Les captures

**De vraies captures, cette fois** : l'application a été ouverte dans une fenêtre (sous Wine), sur le projet de démonstration Armoire_Gaz, et pilotée par une session (`--script`, `--captures`). Les six images jointes :
1. `1260_01_corbeille.png` : la Corbeille, la recherche « pompe » ;
2. `1260_02_bibliotheque_polices.png` : la bibliothèque de polices, chaque famille dans sa police, l'aperçu ;
3. `1260_03_editeur_polices.png` : l'éditeur de polices, une copie de Liberation Sans, le glyphe « a » et ses points, la grille, l'aperçu en direct ;
4. `1260_04_bibliotheque_images.png` : le volet de la bibliothèque d'images ;
5. `1260_05_sous_onglet_polices.png` : le sous-onglet Polices de l'éditeur de vues ;
6. `1260_06_case_police.png` : la case Police d'un objet (police par défaut, des valeurs, du projet, puis la bibliothèque).

**Elles ont trouvé deux défauts, corrigés avant la livraison** :
- dans l'application installée, la bibliothèque de polices était vide : les polices fournies étaient cherchées dans le dossier de travail, pas à côté du programme ;
- dans l'éditeur de polices, la grille des glyphes et l'aperçu en direct étaient des pavés pleins, et les filtres ne tenaient pas dans la colonne. Les glyphes sont maintenant dessinés en images transparentes, et les filtres sont dans une liste déroulante.

## 3. Les essais

Sous Linux : **135 suites d'essais sur 135** réussies (CTest), dont une nouvelle.

Au premier passage, `api2` et `api2libs` avaient un échec chacun : l'essai qui compte les entrées des menus ne connaissait pas la nouvelle entrée Projet › Corbeille… (54 → 55). C'est l'essai qui a été corrigé, pas le logiciel.

- **Nouvel essai : `truetype`** (280 contrôles) :
  - les 16 polices fournies se lisent, s'écrivent et se relisent à l'identique ;
  - le fichier écrit se dessine avec les mêmes chasses ;
  - une nouvelle police, une copie (les noms réservés refusés), les composés qui suivent leur lettre ;
  - ajouter un point sans changer la forme, rectangle, ellipse, le remplissage ;
  - les fichiers refusés (OTF à contours CFF, TTC, tronqué).
  - En plus, les 17 fichiers écrits ont été relus par un outil externe (fontTools), sommes de contrôle comprises : tous valides.
- **`hmi_editor_test`** : 10 846 contrôles (10 552 en 1.25.0). Ce qui est nouveau :
  - **l'éditeur de polices** (51) : nouvelle police, rectangle, ellipse, la plume, ajouter un point, déplacer à la souris (un seul Ctrl+Z), les réglages, chercher et filtrer, l'aperçu (un vrai .ttf), enregistrer dans le projet (les vues suivent en direct), Mes polices, exporter, dupliquer Carlito (noms réservés), décomposer, copier un glyphe, rétablir ;
  - **la bibliothèque de polices et la police partout** (57) : le volet, le sous-onglet Polices, la case Police (importer et choisir en un seul Ctrl+Z), le thème, l'arbre, les objets de la bibliothèque sur « Police par défaut » ;
  - **la Corbeille** (41) ; **le volet de la bibliothèque d'images** ; **les limites** (chemins, Modbus, macros, versions, équipements).
- **`hmi_test`** : 6 965 contrôles (6 912 en 1.25.0) : la bibliothèque de polices (les fournies, Mes polices, l'import, les choix, le thème stocké et relu) ; les rapports (titres de Standard, les courbes dans Excel, une police TrueType intégrée au PDF).
- **`apiplusieurs`** : 84 contrôles (65 en 1.25.0) ; `hmiincident`, `crashreport` et les suites de l'Assistant IA : la Corbeille et les chemins.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.26.0.exe` : l'installateur (52 166 890 octets). Il met à jour la 1.25.0 au même endroit ;
- `API-Studio-1.26.0-portable.zip` et `IHM-Studio-1.26.0-portable.zip` (188 fichiers chacun, dont les 16 polices et leurs licences) ;
- les six captures ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.26.0.exe` | `4C3BBB06F482A3A0EF07B72EC11C81EE60BBDECB7401D46BD69EBA3B1CB11082` |
| `API-Studio-1.26.0-portable.zip` | `BEF039B07397D5C30BE1F9551FB37EDF540A1B2C35CB78D4C3CFE9DBD348EA65` |
| `IHM-Studio-1.26.0-portable.zip` | `AC73164B88FCF7C8ECF54E0FC332383498559BD3D963CA941295DBDDDB7D67DA` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.25.0 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.26.0 » et « IHM Studio 1.26.0 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.25.0 est gardée ;
- **IHM Studio a été ouvert dans une fenêtre** (sous Wine) : les six captures viennent de l'exécutable livré.

## 5. Limites

- **La Corbeille** montre ce qui est supprimé sur le disque : enregistrements, incidents, versions, macros, éléments de l'Assistant IA.
  - Les vues, objets, variables et le reste du projet supprimés n'y sont pas : ils reviennent par **Ctrl+Z**. Ta maquette les montrait dans la Corbeille ; c'est la principale différence.
  - Mes sons et Mes images n'y vont pas non plus : leur retrait est annulable par Ctrl+Z.
  - Les macros ne sont jamais purgées automatiquement.
- **Les polices** :
  - **les OTF à contours CFF** s'emploient, mais ne se modifient pas dans l'éditeur, qui lit les TrueType. Une collection TTC est refusée ;
  - **la case Police** : la liste déroulante est en texte simple, pas écrite dans chaque police (la liste du volet et le sous-onglet, eux, le sont) ;
  - **le menu de la case Police** n'a pas les entrées « Ouvrir la bibliothèque de polices… » et « Modifier cette police… ». « Modifier la police… » est dans le clic droit du sous-onglet Polices ;
  - **le tableau des styles** n'a pas de colonne Police : la police d'un style se règle dans sa fiche ;
  - **l'éditeur de polices** :
    - il ne garde pas les instructions de lissage (hinting) d'une police modifiée. L'IHM et le PDF dessinent sans elles ; sous Windows, une police installée depuis l'export peut paraître un peu moins nette aux petites tailles ;
    - les noms des glyphes ne sont pas écrits dans le fichier (table post au format 3) ;
  - **la police du logiciel lui-même** (menus, arbres) ne change pas : tu ne m'as pas encore répondu sur ce point.
- **Les chemins** : les colonnes « Chemin » des variables publiques et des références croisées ne sont pas couvertes ; ce sont des noms, pas des fichiers.
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows.**
