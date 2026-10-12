# Lot G de l'audit des objets : synoptique et symboles (XPGAnalyser IHM 1.12.11)

Rédigé le 12/10/2026. Il reprend le plan de `docs/AUDIT-BIBLIOTHEQUE-OBJETS.md` : § 1.1 (P3 et P9), § 2.1 (le rôle
« État »), § 3.7 (Synoptique), § 3.8 (Instance de symbole), ligne G du § 4.

Ce document sert à la livraison : les notes de version et les paragraphes du guide se reprennent tels quels
(`src/help/ReleaseNotes.cpp`, `src/hmi/HmiGuideText.cpp` et les numéros de version n'ont pas été touchés). Pour
chaque point : ce qui change pour l'utilisateur, ce que Générer vérifie de plus, les essais, ce qui n'est pas fait.

La règle « ce qui démarrait en 1.12.9 démarre toujours » est tenue :
- aucun contrôle nouveau n'est une erreur bloquante (ce sont des avertissements) ;
- les propriétés nouvelles (`equipment`, `stateAspects`, `operatingMode`, `cylinderMode`) sont absentes d'un objet
  qui ne s'en sert pas : un objet neuf ne les porte pas, l'inspecteur les montre et les pose à la première saisie ;
- la ligne `liaison_equipement` de `ihm.txt` n'est écrite que si l'on a choisi un membre à la main : un projet
  1.12.9 s'enregistre octet pour octet comme avant, et se relit à l'identique ;
- un objet existant garde son dessin : le mode État ne s'applique que s'il est réglé (Contenu), ou d'office avec
  une liaison Équipement (une propriété nouvelle).

## Ce qui change pour vous (notes de version)

- **La liaison « Équipement »** (`synoptique`) : les 25 symboles du synoptique (pompe, moteur, ventilateur,
  compresseur, échangeur, chaudière, mélangeur, convoyeur, tuyauterie, flèche de flux, vanne, vanne 3 voies, clapet,
  filtre, disjoncteur, sectionneur, contacteur, lampe, transformateur, cuve, silo, trémie, bouteille de gaz, vérin,
  instrument ISA) ont une propriété « Équipement », en tête de leur section : une structure (`P1`, `Pompes[2]`,
  `$Pompe$`, le paramètre d'un symbole ou d'une popup). Ses membres se lient par leur nom :
  Valeur ← Marche / EnMarche / Running / Etat ; Mesure ou Niveau (cuve, silo, trémie, bouteille, vérin, ISA) ←
  Mesure / Niveau / Valeur / Pv ; Position (vanne 3 voies) ← Position / Voie / Etat ; Défaut ← Defaut / Fault /
  Alarme ; Ouverture (vanne) ← Ouverture / Position ; En mouvement (vannes) ← EnMouvement / Mouvement / Moving ;
  Mode ← Mode ; Libellé ← Nom / Repere / Tag. Une pompe `P1 : T_Pompe` se règle d'une case (avant : trois
  formules sur deux objets). Ce qui est réglé à la main sur l'objet gagne. L'alarme Défaut de l'objet suit.
- **L'aide de chaque case liée** (`synoptique`) : la Valeur, le Défaut, le Libellé… d'un symbole lié disent dans
  leur aide « Liaison Équipement : lue dans P1.Marche » (ou « réglée à la main : elle gagne »).
- **Le tableau de correspondance, par type** (`synoptique`) : sous la section du symbole, « Liaison Équipement
  (T_Pompe) » montre une ligne par rôle, le membre lu, et « à la main » quand l'objet règle lui-même la propriété.
  Choisir un autre membre (ou « aucun ») vaut pour toutes les structures de ce type ; « (selon le nom) » revient
  aux noms proposés. Renommer le type ou un de ses membres suit.
- **Le mode « État »** (`synoptique`, `contenu`) : l'onglet Contenu d'une pompe, d'un moteur, d'une vanne… règle
  une table valeur → aspect (Arrêt, Marche, Défaut, Maintenance), avec une couleur et « animé » ou non.
  « Depuis une énumération » la remplit : ETAT_MOTEUR (0 arrêt, 1 marche, 2 défaut, 3 maintenance) ou une
  énumération du projet, d'après les noms de ses valeurs (ARRET, STOP : arrêt ; MARCHE, RUN, AUTO : marche ;
  DEFAUT, FAULT, ALARME : défaut ; MAINT… : maintenance ; les autres : arrêt). Avant, un moteur en DÉFAUT (2)
  se lisait « différent de 0 » : vert, et il tournait. Avec la liaison Équipement, une Valeur entière ou énumérée
  prend ce mode d'office.
- **Le mode montré** (`synoptique`) : une étiquette en haut à gauche du symbole (Automatique, Manuel), quand le
  membre Mode est lié (ou la propriété « Mode » réglée : un texte, ou `{P1.Mode}`).
- **Le libellé à trous dans un symbole** (`symboles`) : `{Nom}` dans le libellé d'une pompe, le titre d'un cadre,
  le texte d'un état… d'un symbole est remplacé par l'argument de l'instance (avant : seul le Texte l'était, et
  `{Nom}` s'affichait). `{P.Nom}` se remplit en marche, `P-$N$` s'affiche sans ses `$`.
- **Les arguments d'une instance de symbole** (`objet-instance-de-symbole`) : Générer les vérifie comme ceux d'une
  popup - le type (une T_Vanne pour une T_Pompe, un REAL pour un INT ; INT → REAL passe), un calcul ou une
  constante pour un paramètre que le symbole écrit, les arguments sans nom en trop (ou au rang d'un paramètre déjà
  nommé), une constante qui ne se convertit pas (avant : collée telle quelle). Des avertissements.
- **« Créer un symbole » type ses paramètres** (`symboles`) : `Pompe := Pompes[1]` devient un paramètre
  `T_Pompe`, `Mesure := Niveau` un `REAL` (avant : ANY). Un chemin de type inconnu reste ANY.
- **Le modèle « Moteur et son état »** (`modeles-de-vues`) : il déclare `Moteur : T_Moteur` (le type Marche,
  Defaut, Nom est créé s'il manque) ; son moteur est lié par Équipement, son voyant montre `Moteur.Defaut`, son
  texte `{Moteur.Nom}`. Posé, il suffit de donner `M1` à l'instance.
- **Le vérin rentré / sorti** (`objet-verin`) : « Course » : position (comme avant, entre Minimum et Maximum) ou
  rentré / sorti - la Valeur se lit vrai / faux, un BOOL Sorti sort la tige entière (avant : 1 % de sa course).
- **La bouteille de gaz** (`synoptique`) : la couleur de l'ogive se choisit
  par gaz, couleurs EN 1089-3 : oxygène blanc, azote noir, argon vert foncé, dioxyde de carbone gris, hélium brun,
  acétylène marron, protoxyde d'azote bleu, air blanc et noir, hydrogène rouge (inflammable), et par propriété
  (inerte vert vif, oxydant bleu clair, toxique ou corrosif jaune). Une couleur se tape toujours.
- **La vanne 3 voies** (`objet-vanne-3-voies`) : les cases du mode de position qui ne sert pas sont grisées
  (A et B en mode entier ; la Position en mode deux booléens) ; Générer dit une position 1-2 fixée sur un
  boisseau en L.
- **Renommer suit** (`synoptique`) : renommer une variable réécrit l'Équipement, un Défaut ou une Valeur tapés
  sans `=`, et les libellés et titres à trous (avant : seul le Texte) ; renommer un type IHM met à jour les
  paramètres de popup et de symbole de ce type.

## Pour le guide (paragraphes courts)

- `synoptique` : « 1.12.11 : Équipement - donne au symbole sa structure (P1, Pompes[2], un paramètre). Ses membres
  se lient par leur nom : Valeur ← Marche, Défaut ← Defaut, Mode ← Mode, Libellé ← Nom (Mesure ou Niveau pour une
  cuve, Ouverture et En mouvement pour une vanne). La section Liaison Équipement montre chaque rôle et le membre
  lu ; un autre choix vaut pour tout le type. Ce que tu règles à la main sur l'objet gagne. »
- `synoptique` (mode État) : « 1.12.11 : Contenu › Mode État - la Valeur choisit un aspect : arrêt, marche,
  défaut, maintenance (une couleur, animé ou non). Depuis une énumération : ETAT_MOTEUR, ou une énumération du
  projet d'après ses noms (ARRET, MARCHE, DEFAUT, MAINT…). Un moteur ETAT_MOTEUR en Défaut (2) se dessine en
  défaut. »
- `contenu` : « 1.12.11 : une pompe, un moteur, une vanne… - Contenu règle leur mode État, une ligne par valeur. »
- `symboles` : « 1.12.11 : dans un symbole, un libellé, un titre, un texte d'état à trous ({Nom}) reçoivent
  l'argument de l'instance, comme le Texte. Créer un symbole type ses paramètres d'après les chemins choisis
  (Pompes[1] : T_Pompe). »
- `objet-instance-de-symbole` : « 1.12.11 : Générer compare chaque argument au paramètre, comme pour une popup :
  le type, un calcul pour un paramètre que le symbole écrit, les arguments sans nom en trop, une constante qui ne
  se convertit pas. Ce sont des avertissements. »
- `modeles-de-vues` : « 1.12.11 : Moteur et son état - un symbole lié par sa structure (Moteur : T_Moteur) :
  pose-le, donne-lui M1. »
- `objet-verin` : « 1.12.11 : Course - rentré / sorti : la Valeur se lit vrai / faux (un BOOL Sorti). »
- `objet-vanne-3-voies` : « 1.12.11 : les cases du mode de position qui ne sert pas sont grisées ; Générer dit
  une position 1-2 sur un boisseau en L (il ne relie que 1-3 et 2-3). »

## Ce que Générer vérifie de plus

Tous des avertissements : rien ne bloque.
- un Équipement qui n'est pas une structure (`Niveau_1`, un REAL), un chemin inconnu, un paramètre sans type ;
  une structure dont aucun membre ne se lie ; un membre choisi qui n'est plus dans le type ; un membre lié du
  mauvais type (une STRING pour un niveau, un tableau ou une structure pour un défaut, une date pour la marche) ;
- une énumération du projet sur la Valeur d'un symbole lue vrai / faux : le mode État proposé (§ 4a) ;
- une structure entière sur la Valeur : la liaison Équipement proposée (§ 4b) ;
- une ligne du mode État illisible, une couleur du mode État qui n'en est pas une (§ 4c ; les couleurs marche,
  arrêt et défaut l'étaient déjà depuis la 1.12.6, vérifié) ;
- un BOOL sur un vérin en course : « rentré / sorti » proposé ;
- une position 1-2 fixée sur une vanne 3 voies à boisseau en L ;
- les arguments des instances de symbole (le type, un calcul pour un paramètre écrit, les sans-nom en trop ou en
  double, une constante qui ne se convertit pas).
Une valeur fixe qui ressemble à une variable (§ 4d, D13) : vérifié, le lot A la lit en formule (le dessin comme
l'alarme) et Générer en vérifie la variable ; rien à ajouter.

## Les essais

`HMI_TEST_11211=1` les lance seuls, dans `hmi_test` et `hmi_editor_test`.

- `hmi_test` : `lotG11211` (ESSAIS_HMI contrôles) :
  - `lotG11211_liaison` : ce que la liaison remplit (Valeur, Défaut, Mode, Libellé par leur nom), un repère
    `$Pompes[2]$`, ce qui est réglé à la main gagne, l'objet enregistré ne change pas ; le choix d'un membre pour
    le type (deux T_Pompe le suivent, « aucun », retour aux noms), enregistré et relu, un projet sans choix
    inchangé ; le type et un membre renommés ; une variable renommée (l'équipement, un Défaut tapé sans `=`, un
    libellé à trous) ; en marche (marche, défaut, libellé P-101, mode « Manuel ») ; l'alarme Défaut de l'objet ;
    Générer sans erreur, un équipement qui n'est pas une structure dit ;
  - `lotG11211_etat` : le petit tableau (lu, écrit, l'aspect d'une valeur), ETAT_MOTEUR, les noms d'une
    énumération, le mode d'office (une énumération, un INT) ; en marche, un moteur en DEFAUT (2) en défaut, un
    INT à 3 en maintenance, le retour en marche animée ; Générer (énumération lue vrai / faux, structure entière) ;
  - `lotG11211_symbole` : dans un symbole, l'équipement reçoit l'argument, `{Repere}` et un titre à trous sont
    remplacés, en marche, et l'alarme de la pompe de l'instance ;
  - `lotG11211_popup` : une popup à paramètre typé (`Pompe : T_Pompe`) ouverte pour `P2` : sa pompe lit P2 ;
  - `lotG11211_arguments` : le type, un calcul pour un paramètre écrit, les sans-nom en trop et en double, une
    constante qui ne se convertit pas, rien sur une instance juste, rien de bloquant ;
  - `lotG11211_creer` : Créer un symbole type ses paramètres ; le modèle Moteur et son état (le type créé, le
    paramètre, la liaison, en marche avec M1, son alarme, Générer) ;
  - `lotG11211_extras` : le vérin rentré / sorti (en marche, et le BOOL en course dit), la vanne 3 voies en L, les
    gaz, la boucle ISA dupliquée ;
  - `lotG11211_generer` : les couleurs d'un symbole et de son mode État, un Défaut tapé sans `=` vérifié, un membre
    lié du mauvais type, un équipement `Pompes[1]` dupliqué suit l'indice.
- `hmi_editor_test` : `lotG11211_editeur` (la propriété Équipement, sa liste et sa place, la section Liaison
  Équipement et un choix pour le type, Ctrl+Z, « à la main » ; Contenu › Mode État : ETAT_MOTEUR, une ligne
  réglée, une variable énumérée qui devient la Valeur, décocher, Ctrl+Z ; une cuve sans mode État ; l'étiquette du
  mode dessinée ; le type renommé).
- `hmi_reperes_test` : `liaisonEquipement11211` (`$Pompes[1]$` et `Pompe $Rep$` dupliqués en `$Pompes[2]$` et
  `Pompe $P102$`, Générer sans erreur, en marche chacune lit sa pompe).

Chaque contrôle dit, dans son libellé, ce que faisait la 1.12.9. Comme pour les lots précédents, l'échec « avant »
n'est pas rejoué un par un : ces essais appellent des fonctions nouvelles (`eqlink::…`, `symargs::check`) et ne se
compilent pas contre la 1.12.9.

## Ce qui n'est pas fait, et pourquoi

- **L'éditeur ne dessine pas la liaison** : une pompe liée montre dans l'éditeur ses valeurs fixes (pas
  `{P1.Nom}` sous elle) ; la section Liaison Équipement dit ce qui sera lu. Résoudre le type à chaque image de
  l'éditeur coûterait, pour peu.
- **Les structures de l'automate (XPGAnalyser API)** : elles se lient en marche et dans Générer par les DDT que
  l'application publie (`eqlink::setPlcTypes`) ; un DDT renommé dans le programme ne déplace pas un choix fait
  pour l'ancien nom (le choix est gardé par nom de type). Non essayé avec un vrai programme (pas d'essai API ici).
- **Un paramètre de popup sans type** (ANY) ne se lie pas : son type n'est connu qu'à l'ouverture. Générer le dit
  (« donne-lui son type de structure »).
- **Le mode d'office** ne vaut qu'avec la liaison Équipement (un objet existant garde son dessin) ; une Valeur
  liée à la main à une énumération est signalée par Générer, et Contenu le règle d'un choix.
- **Le Mode** n'est montré que pour ce qui se pilote (pompe, moteur, ventilateur, compresseur, échangeur,
  chaudière, mélangeur, convoyeur, vannes, vérin) ; pas pour un tube, une flèche, un contenant ou un appareil
  électrique.
- **La Position d'une vanne 3 voies** (← Position / Voie / Etat) est un rôle ajouté à la liste de la décision
  (elle n'en avait pas) ; son mode État n'existe pas (ses codes de position restent).
- **Le vérin** se lie par Mesure / Niveau / Valeur / Pv (pas « Sorti ») ; « rentré / sorti » se choisit à la main.
- **L'air comprimé** (blanc et noir en quartiers, EN 1089-3) se dessine d'un seul gris clair : l'ogive n'a qu'une
  couleur. Les équivalents #RRGGBB des RAL sont approchés.
- **« 1$NN$ » pour la boucle ISA** : un repère ne commence pas par un chiffre ; la boucle suit la copie d'elle-même
  (+1), un repère s'y remplace comme ailleurs (`$Boucle$`).
- **Un type IHM renommé** met maintenant à jour les paramètres de popup et de symbole de ce type ; un membre
  renommé ne réécrit pas les formules qui le citent (`P1.Marche` tapé à la main) : c'est plus ancien que le lot.
- **Vérifier les arguments nommés inconnus** reste l'erreur de 1.11.22 (dite une fois) ; une constante nommée
  qui ne se convertit pas est déjà une erreur de syntaxe ou « variable inexistante » : l'avertissement nouveau ne
  la répète pas.

## Les risques pour les projets existants

- **Format** : `equipment`, `stateAspects`, `operatingMode`, `cylinderMode` (propriétés d'objet) et la ligne
  `liaison_equipement` (type, rôle, membre) de `ihm.txt` sont nouveaux, absents tant qu'on ne s'en sert pas. Une
  version 1.12.9 qui relit un fichier 1.12.11 ignore la ligne (avertissement « inconnu, ignoré ») et les
  propriétés (inconnues de l'inspecteur).
- **Ce qui change à l'écran** :
  - dans un symbole, un libellé, un titre, un texte d'état à trous qui cite un paramètre (`{Nom}`) montre
    l'argument (avant : `###` ou les accolades) ;
  - les symboles marche / arrêt (pompe, moteur, vanne…) ont l'onglet Contenu (leur mode État, vide par défaut) ;
    la bouteille de gaz montre le nom du gaz dans l'inspecteur quand sa couleur en est une ; les cases inutiles
    de la vanne 3 voies sont grisées ; une ligne « Équipement » en tête de la section d'un symbole, « Course » pour
    un vérin, « Mode État » pour les symboles marche / arrêt ;
  - Dupliquer un instrument ISA numérote sa boucle (101, 102…) au lieu de la recopier ;
  - Générer dit plus de choses, toujours en avertissements.
- **Le renommage** suit maintenant les libellés et les titres à trous (avant : seul le Texte), un Défaut ou une
  Valeur de synoptique tapés sans `=`, l'Équipement.
- **Le coût** : une vue qui a des liaisons Équipement est recomposée à chaque cycle (100 ms) comme une vue à
  jokers ; la recherche du type et des membres se fait par objet lié, non mesurée sur un gros projet.
