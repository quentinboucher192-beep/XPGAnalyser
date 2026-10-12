# XPGAnalyser 1.12.10, 1.12.11 et 1.12.12 : les plages de la variable, le synoptique lié, les sommets et les médias (lots F, G et H)

Livrées le 12/10/2026 en début d'après-midi, après la 1.12.9 du matin (`docs/LIVRAISON-1.12.9.md`).

Elles répondent à la demande du 12/10 :

> « fait la suite fait une maquette interactive pour montrer ce que ca ferait et code FGH d'un coup stp »

La maquette interactive est dans `docs/maquettes/maquette-lots-fgh.html`. Elle a été envoyée avant le code, et montre chaque geste des trois lots.

Ce sont les trois derniers lots du plan de l'audit de la bibliothèque d'objets (`docs/AUDIT-BIBLIOTHEQUE-OBJETS.md`, § 4) :
- **1.12.10, le lot F : les plages et les seuils de la variable** (§ 1.1 P8, § 2.6). La plage 0..10 bar se règle une fois, sur la variable, et non plus sur chaque objet.
- **1.12.11, le lot G : le synoptique et les symboles** (§ 3.7, § 3.8). Une pompe se lie à sa structure en une case, et un moteur en défaut se dessine en défaut.
- **1.12.12, le lot H : les sommets, l'horloge, le texte défilant, le code QR, la vidéo** (§ 3.1, § 3.3, § 3.10). Ce sont les points que le plan avait laissés « pour plus tard ».

Avec eux, les huit lots du plan (A à H) sont livrés.

Un seul installateur les porte toutes les trois : la 1.12.12. Tout se passe dans **XPGAnalyser IHM**. XPGAnalyser API ne change pas, à part son numéro de version.

Le détail objet par objet, ce que Générer vérifie, les essais et les limites sont dans les notes de chaque lot :
- `docs/NOTES-LOT-F.md` ;
- `docs/NOTES-LOT-G.md` ;
- `docs/NOTES-LOT-H-DESSIN.md` (les sommets, les points depuis une source, le plan à zones, la référence d'une courbe XY) ;
- `docs/NOTES-LOT-H-MEDIAS.md` (l'horloge, le texte défilant, le code QR, la vidéo).

## 1. La 1.12.10 : les plages et les seuils de la variable

**Unités et formats gagne six colonnes.** Configuration › Unités et formats a maintenant, pour chaque variable :
- Min et Max ;
- Seuil bas et Seuil haut ;
- Alarme basse et Alarme haute.

Elles se rangent dans l'ordre alarme basse < seuil bas < seuil haut < alarme haute. Une case vide n'est pas réglée. Une valeur est un nombre, ou le nom d'une variable tapé sans `=` (`Consigne_Max`). Elles se collent depuis Excel, comme l'unité et le format.

Pour une variable liée à un équipement avec une mise à l'échelle (0..27648 → 0..10), la ligne montre en gris « 0 (mise à l'échelle) » et « 10 (mise à l'échelle) » tant qu'on n'a rien tapé.

**Les objets en héritent.** Une borne ou un seuil laissé vide sur l'objet prend la valeur de sa variable. S'il n'y en a pas, il prend celle d'un objet neuf de son genre (0..100). Cela vaut pour :
- la barre de progression, le bargraphe, le cadran, le curseur, le potentiomètre, le champ de saisie et le vérin (Min et Max) ;
- la jauge, le thermomètre, la cuve, le silo, la trémie et la bouteille (Min, Max et leurs seuils) ;
- l'afficheur numérique et l'instrument ISA (leurs seuils) ;
- la courbe : son axe Y prend l'union des plages de ses plumes ;
- la vanne : la plage de son ouverture.

Les règles :
- ce qui est réglé à la main sur l'objet l'emporte ;
- un objet posé depuis la palette naît avec Min et Max vides : il suit sa variable ;
- un objet d'un projet existant garde les valeurs écrites dans son fichier : il ne change pas ;
- vider la case dans l'inspecteur remet l'objet sur sa variable. Avant, vider Min le remettait à 0.

**L'inspecteur montre d'où vient la valeur.** Une case vide montre en gris ce que l'objet en fera : « 10 (de Pression_Ligne) », « 0 (mise à l'échelle de Debit) », « 100 (par défaut) ». La case « Format de la variable » devient « Unité, format et plage de la variable ».

**Des seuils qui se voient** :
- la jauge, le thermomètre et l'instrument ISA ont quatre seuils, vides par défaut (ceux de la variable). Au-delà d'un seuil, la valeur prend la couleur des seuils ; au-delà d'une alarme, celle des alarmes ;
- les zones vides d'un bargraphe ou d'un cadran suivent les seuils de la variable : les alarmes en rouge, les seuils en jaune, le vert entre les seuils ;
- sur une cuve, un silo, une trémie ou une bouteille, un seuil hors de la plage se dessine au bord, marqué d'une flèche. Avant, il disparaissait alors que son alarme marchait ;
- la vanne gagne « Ouverture : min » et « Ouverture : max » : un signal 0..27648 se montre de 0 à 100 % sans formule.

**Un nom de variable sans `=`.** Min, Max et les seuils acceptent `Consigne_Max` tapé sans `=`, lu comme la variable. C'est le cas aussi de l'axe Y d'une courbe, de la consigne d'un bargraphe, de la plage de l'ouverture d'une vanne, des seuils d'un graphique en barres et des tolérances d'un histogramme. Un repère s'y garde (`$Cuve$.Seuil`), et Dupliquer le remplace. Avant :
- l'inspecteur le refusait ;
- Générer en faisait une erreur bloquante sur un curseur, un afficheur ou une cuve ;
- sur un graphique, le nom était ignoré.

**La valeur initiale d'une structure** (vue en préparant la 1.12.9) :
- `Pompe : T_Pompe` accepte `(Marche := TRUE, Consigne := 12.5)`. Les membres non nommés gardent la valeur de leur type ;
- un tableau de structures accepte un littéral par élément : `Cuves : ARRAY[1..3] OF T_Cuve := [(Nom := 'Cuve A', Niveau := 4.2), (Nom := 'Cuve B', Niveau := 7.5), (Nom := 'Cuve C', Niveau := 2.8)]`. Les éléments en moins gardent les valeurs du type ;
- la simulation montre ces valeurs dès le démarrage, sans script ;
- « Éléments… » s'ouvre aussi pour un tableau de structures : une ligne par élément, avec son littéral.

Avant, l'écran des variables refusait ce littéral, et un littéral écrit par un autre outil était ignoré sans un mot.

**Générer** dit de nouvelles choses, toujours en avertissement ou en information, sans rien bloquer :
- un Min ≥ Max ;
- un seuil hors de la plage, ou des seuils dans le désordre ;
- une variable inconnue, sur la ligne d'Unités et formats comme sur les objets qui la prennent ;
- en information, un objet écrit 0..100 qui montre une variable de 0 à 10 : « videz Min et Max pour qu'il la suive » ;
- un littéral de structure qui ne s'applique pas : un membre inconnu, un texte pour un REAL, trop d'éléments.

## 2. La 1.12.11 : le synoptique et les symboles

**La liaison « Équipement ».** Les 25 symboles du synoptique (pompe, moteur, vanne, cuve, vérin, instrument ISA…) ont une propriété « Équipement », en tête de leur section. Elle prend une structure : `P1`, `Pompes[2]`, `$Pompe$`, ou le paramètre d'un symbole ou d'une popup. Ses membres se lient par leur nom :
- Valeur ← Marche, EnMarche, Running ou Etat ;
- Mesure ou Niveau (cuve, silo, trémie, bouteille, vérin, ISA) ← Mesure, Niveau, Valeur ou Pv ;
- Défaut ← Defaut, Fault ou Alarme ;
- Ouverture et En mouvement, pour une vanne ;
- Mode ← Mode : une étiquette Automatique ou Manuel en haut à gauche du symbole ;
- Libellé ← Nom, Repere ou Tag.

Une pompe `P1 : T_Pompe` se règle ainsi d'une case. Avant, il fallait trois formules sur deux objets.
- Ce qui est réglé à la main sur l'objet gagne.
- L'éditeur dessine déjà la liaison, par exemple le libellé `{P1.Nom}` sous la pompe.
- L'alarme Défaut de l'objet suit.

Sous la section du symbole, « Liaison Équipement (T_Pompe) » montre une ligne par rôle, avec le membre lu. Un autre choix vaut pour toutes les structures de ce type. Renommer le type ou un de ses membres suit.

**Le mode « État ».** L'onglet Contenu d'une pompe, d'un moteur ou d'une vanne règle une table valeur → aspect : arrêt, marche, défaut ou maintenance, avec une couleur, animé ou non. « Depuis une énumération » la remplit :
- avec ETAT_MOTEUR : 0 arrêt, 1 marche, 2 défaut, 3 maintenance ;
- ou avec une énumération du projet, d'après les noms de ses valeurs : ARRET, MARCHE, DEFAUT, MAINT…

Avant, un moteur en DÉFAUT (2) se lisait « différent de 0 » : il était vert, et il tournait. Avec la liaison Équipement, une Valeur entière ou énumérée prend ce mode d'office.

**Les symboles** :
- dans un symbole, un libellé, un titre ou un texte d'état à trous (`{Nom}`) reçoit l'argument de l'instance. Avant, seul le Texte le recevait ;
- Générer vérifie les arguments d'une instance de symbole comme ceux d'une popup : le type, un calcul pour un paramètre que le symbole écrit, les arguments sans nom en trop, une constante qui ne se convertit pas. Ce sont des avertissements ;
- « Créer un symbole » type ses paramètres d'après les chemins choisis : `Pompes[1]` donne `T_Pompe`. Avant, ils étaient ANY.

**Les petits plus** :
- le modèle « Moteur et son état » se lie par sa structure (`Moteur : T_Moteur`) : posé, il suffit de donner `M1` à l'instance ;
- le vérin se règle « rentré / sorti » : un BOOL sort toute la tige. Avant, il la sortait de 1 % de sa course ;
- la bouteille prend la couleur EN 1089-3 de son gaz ;
- la vanne 3 voies grise le mode de position qui ne sert pas. Générer dit une position 1-2 sur un boisseau en L ;
- la boucle d'un instrument ISA suit la duplication : 101, 102, 103.

## 3. La 1.12.12 : les sommets, l'horloge, le texte défilant, le code QR, la vidéo

**Les sommets sur le dessin.** Une ligne ou un polygone choisi seul montre une poignée sur chaque sommet :
- glisser déplace le sommet, au pas de la grille (Alt : librement) ;
- un double-clic sur un segment ajoute un sommet ;
- Suppr, ou le clic droit « Retirer le sommet », le retire. Une ligne en garde 2, un polygone 3 ;
- les flèches déplacent le sommet choisi ;
- chaque geste s'annule d'un Ctrl+Z, et le cadre suit les points.

L'onglet Contenu montre les sommets en tableau (X, Y), un repère gardé case par case.

**D'autres tracés à la souris** :
- **points depuis une source** : une ligne ou un polygone se trace en marche d'après une variable, par exemple les membres X et Y d'un tableau de structures (`Profil[*].X, Profil[*].Y`) ;
- **le plan à zones** se trace sur l'image : la zone choisie se règle à la souris comme un polygone, et « Tracer une zone » en ajoute une, un clic par point. La variable de la zone choisie est lue et écrite ;
- **la référence d'une courbe XY**, avec une échelle fixe, se règle à la souris : on glisse, on ajoute ou on retire un point.

**L'horloge liée à une variable.** « Heure affichée » montre une TOD, une DT, une DATE, un texte ISO 8601 ou des secondes, au lieu de l'heure de l'IHM. Trois réglages s'y ajoutent :
- le format de la date : 12/10/2026, 2026-10-12, 12 oct. 2026, lundi 12 octobre 2026 ;
- l'heure sur 24 h ou 12 h ;
- le fuseau : local, UTC, UTC+2…

**Le texte défilant depuis une source.** « Messages depuis » fait défiler l'un après l'autre :
- les textes d'une LIST ou d'un tableau ;
- ceux d'un joker (`Consignes[*].Texte`) ;
- ou les alarmes en cours, éventuellement d'un seul groupe.

Le nombre de messages suit la liste en marche.

**Le code QR jusqu'à la version 40.** Il prend les 40 versions et les quatre niveaux de correction, comme le promettait le guide. Avant, il s'arrêtait à la version 10 : 213 octets en M. Il va maintenant jusqu'à 2 331 octets en M, ou 5 596 chiffres. La ligne « Place » de l'inspecteur dit ce qui reste.

**La vidéo décodée.**
- Un AVI Motion-JPEG se décode partout. Sous Windows, Media Foundation décode aussi les MP4 (H.264), WMV et MOV.
- « Lecture » (une variable BOOL) met la vidéo en pause. « Départ » la fait partir à l'affichage ou sur une condition. « Répétition » la fait boucler, ou s'arrêter sur sa dernière image.
- Sa position peut s'écrire dans une variable.
- L'éditeur et les ressources montrent la première image.

Avant, seule l'image d'attente s'affichait.

## 4. Trois corrections faites en assemblant les lots

**Un Windows sans Media Foundation démarre.** Le lot H liait Media Foundation à l'édition des liens : les deux exe importaient `MFPlat.DLL` et `MFReadWrite.dll`. Or ces deux bibliothèques manquent :
- aux éditions N de Windows (vendues en Europe) sans le Media Feature Pack ;
- à certains Windows Server, sans leurs fonctions vidéo.

Sur ces postes, ni XPGAnalyser IHM ni XPGAnalyser API ne se seraient plus lancés, faute de DLL. Media Foundation se charge maintenant à la demande, depuis le dossier système, à la première vidéo qui en a besoin. Sans lui, la vidéo montre son image d'attente et le conseil : installer le Media Feature Pack, ou convertir la vidéo en Motion-JPEG AVI. C'est vérifié sur les exe livrés : ils importent exactement les mêmes DLL que ceux de la 1.12.9, et aucune de Media Foundation.

**L'éditeur dessine une jauge neuve à la valeur initiale de sa variable.** Une jauge posée depuis la palette suit la plage de sa variable, par exemple 0..10. Mais l'éditeur la dessinait à sa valeur fixe d'objet neuf, 40 : collée au bout, dans la couleur des alarmes. Elle se dessine maintenant à la valeur initiale de la variable, comme les jokers et « Points depuis » le font déjà. Cela ne vaut que quand Min ou Max vient de la variable : un objet aux bornes écrites garde son dessin.

**La vanne compte parmi les objets qui montrent une variable.** Avec le lot F, une vanne prend la plage de son ouverture dans Unités et formats. Pourtant, la colonne « montrée par » ne la citait pas, et Générer disait « aucun objet ne la montre encore ». Un essai le vérifie maintenant.

## Tester vous-même

1. **Lot F.** Dans Configuration › Unités et formats, donnez à une variable `Pression_Ligne` Min 0, Max 10, Seuil haut 7 et Alarme haute 9. Posez une **jauge** depuis la palette, avec Valeur `=Pression_Ligne`. Elle va de 0 à 10, et l'inspecteur montre « 10 (de Pression_Ligne) » en gris. En marche, à 7,6, la valeur prend la couleur des seuils.
2. Créez un type `T_Cuve` (Nom, Niveau) et une variable `Cuves : ARRAY[1..3] OF T_Cuve` avec la valeur initiale `[(Nom := 'Cuve A', Niveau := 4.2), (Nom := 'Cuve B', Niveau := 7.5), (Nom := 'Cuve C', Niveau := 2.8)]`. Un **graphique en barres** `Cuves[*].Niveau`, nommé par `Cuves[*].Nom`, montre trois barres dès le démarrage, sans script.
3. **Lot G.** Créez un type `T_Pompe` (Nom, Marche, Defaut, Mode) et une variable `P1 : T_Pompe`. Posez une **pompe** et réglez Équipement à `P1`. La section « Liaison Équipement (T_Pompe) » montre Valeur ← Marche, Défaut ← Defaut, Mode ← Mode et Libellé ← Nom.
4. Posez un **moteur** dont la Valeur est une variable INT à 2. Dans Contenu, choisissez Mode État › Depuis une énumération › ETAT_MOTEUR : le moteur se dessine en défaut.
5. **Lot H.** Posez un **polygone** et choisissez-le seul. Glissez un sommet, double-cliquez un segment pour en ajouter un, puis retirez-le avec Suppr. Contenu montre les sommets, une ligne par sommet.
6. Posez un **code QR** avec un texte d'un millier de caractères : la ligne Place dit la version choisie et ce qui reste.
7. Ajoutez une vidéo AVI Motion-JPEG aux ressources, puis posez un objet **Vidéo** qui la montre. En simulation, elle joue. Avec Lecture liée à une variable BOOL, la passer à FALSE met la vidéo en pause. Essayez aussi un MP4 : il passe par Media Foundation, qui n'a pas encore été essayé sur un vrai Windows (voir « Limites connues »).

## Installer

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.12.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.12-portable.zip` et `XPGAnalyser-IHM-1.12.12-portable.zip` : chaque application, sans installation ;
- ce document ;
- les captures `1212_ihm_*`, prises sous Wine avec l'exe livré.

1. Lancez `XPGAnalyser-Setup-1.12.12.exe`. Il met à jour la version installée, la 1.12.9 du matin ou une plus ancienne : même identité d'installation, données reprises sans être déplacées. Il pose les deux applications et leurs deux raccourcis. Vos projets s'ouvrent tels quels : une ligne d'Unités et formats sans plage s'enregistre comme en 1.12.9.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.12.12.exe` (29,4 Mo, 30 849 943 octets) :
`876AC96EC0A4A9E9A97C4725DF59414E509DECB967F3533DE651C76FC9DF4F57`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `XPGAnalyser-API-1.12.12` ou `XPGAnalyser-IHM-1.12.12`. Décompressez, puis lancez l'exe.
- `XPGAnalyser-API-1.12.12-portable.zip` (22,0 Mo) :
  `C13B507CBA8534D1AC8C94107237A57560C3825F5908406146BB8EB392442305`
- `XPGAnalyser-IHM-1.12.12-portable.zip` (22,0 Mo) :
  `4339A877C34EE2186F3B8F6CEF8BD846A86EBD0E986E63E0B9235FB4EC11A250`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe, 74 sur 74. Les deux nouvelles suites viennent du lot H : `hmiqr` (le code QR) et `hmivideo` (le décodeur Motion-JPEG, la file des images d'avance). Elle comprend :
- 7 268 contrôles dans `hmieditor` (7 073 à la 1.12.9), 5 463 dans `hmi` (5 172) et 304 dans `hmireperes` (268) ;
- 117 dans `hmiqr` et 73 dans `hmivideo`, les deux nouvelles suites ;
- ceux d'avant, qui passent toujours : 2 177 dans `hminatives`, 966 dans `hmidecl`, 269 dans `hminatives1127`, 126 dans `hmisetring`.

Un premier passage, mené pendant la compilation Windows, a vu échouer une fois `crashtrail`. Cet essai compte les lectures faites pendant une écriture, et il dépend de la charge de la machine. Relancé seul, puis avec toute la suite, il passe.

Ce que couvrent les essais des trois lots (le détail est dans les notes de chaque lot) :
- **le lot F** (`HMI_TEST_11210=1` les lance seuls) :
  - la ligne d'Unités et formats, enregistrée et relue. Une ligne sans plage s'écrit comme en 1.12.9 ;
  - l'héritage, objet par objet. Une jauge neuve prend 0..10, une jauge existante reste à 0..100, la mise à l'échelle sert, et un seuil réglé sur l'objet l'emporte ;
  - en marche : un curseur poussé au bout écrit 10, une vanne 0..27648 à 13824 s'ouvre à 50 %, et le littéral `Cuves` donne 4.2, 7.5 et 2.8 dès le démarrage ;
  - Générer, et `$Cuve_A$.Seuil` dupliqué en `$Cuve_B$.Seuil` ;
- **le lot G** (`HMI_TEST_11211=1`) :
  - la liaison Équipement et son tableau par type, enregistré et relu, avec les renommages ;
  - en marche : une pompe P-101 en mode Manuel, un moteur ETAT_MOTEUR en défaut puis en maintenance ;
  - un symbole et une popup à paramètre typé ;
  - les arguments vérifiés, « Créer un symbole », le modèle « Moteur et son état » ;
- **le lot H** (`HMI_TEST_11212H1=1` et `HMI_TEST_11212=1`) :
  - les sommets à la souris et au clavier, chaque geste et son Ctrl+Z ;
  - « Points depuis », le plan à zones tracé, la référence d'une courbe XY ;
  - l'horloge sur chaque source, le texte défilant et les alarmes en cours ;
  - le code QR : des symboles de référence comparés module pour module, puis chaque version de 1 à 40, à chaque niveau, relue par un lecteur écrit d'après la norme ;
  - un AVI Motion-JPEG décodé image par image, la lecture, la boucle et la position écrite.

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `XPGAnalyser API 1.12.12` et `XPGAnalyser IHM 1.12.12` ;
- ils importent exactement les mêmes DLL que ceux de la 1.12.9 (ADVAPI32, IPHLPAPI, KERNEL32, SDL3, SHELL32, USER32, WS2_32, msvcrt, ole32), et aucune de Media Foundation.

**La session sous Wine** (`tools/sessions/session-1212-ihm.txt`) a tourné avec l'exe livré, sur Armoire_Gaz. Un petit outil y a ajouté :
- les types `T_Pompe_FGH` (Nom, Marche, Defaut, Mode, Debit), `T_Point_FGH` (X, Y) et `T_Cuve_FGH` (Nom, Niveau) ;
- trois lignes d'Unités et formats. `Pression_FGH` va de 0 à 10 bar, avec les seuils 1, 2, 7 et 9. `Niveau_FGH` va de 0 à 100 %, et son alarme haute (110) est hors de la plage. `Ouverture_FGH` va de 0 à 27648 ;
- `Cuves_FGH` et `Profil_FGH` (11 points), initialisés par un littéral, sans script ;
- la vue `Vue_LotFGH` (21 objets), et `Vue_MP4_FGH`, une vidéo MP4.

Chaque ligne de la session a réussi. La simulation se construit (427 éléments générés, 347 compilés, 0 erreur), et la Console ne note aucune erreur. Les captures :
- `1212_ihm_01`, l'éditeur. La jauge, le bargraphe et la cuve sont sur la plage de leur variable, à sa valeur initiale. La jauge et le bargraphe sont à 7,6 bar, dans la couleur des seuils. La cuve est à 62 %, et son alarme haute, hors de la plage, est marquée au bord. On voit aussi la pompe liée et son libellé `{P_FGH.Nom}`, le polygone, le profil tracé d'après son littéral, l'horloge, le code QR, les messages de la liste et la première image de la vidéo ;
- `02`, Unités et formats : les colonnes Min, Max et seuils, et le volet « Plage et seuils » de Pression_FGH, « montrée par » la jauge ;
- `03`, la jauge : Minimum et Maximum vides, avec « 0 (de Pression_FGH) » et « 10 (de Pression_FGH) » en gris ;
- `04`, la pompe : la section « Liaison Équipement (T_Pompe_FGH) », avec Valeur ← Marche, Défaut ← Defaut et Mode ← Mode ;
- `05`, le moteur : Contenu, son mode État en quatre lignes ;
- `06`, le polygone : ses cinq poignées, et Contenu, une ligne par sommet ;
- `07`, le code QR : un texte de 392 octets, au-delà de l'ancienne limite (213 octets en M). La ligne Place dit « reste 1 939 octets » ;
- `08`, la simulation. La jauge et l'instrument ISA sont à 7,6 bar, dans la couleur des seuils. La cuve est à 62 % et la vanne à 50 %. Les cuves A, B et C sont à 4,2, 7,5 et 2,8 dès le démarrage, la B en rouge au-dessus du seuil 7. La pompe P-101 est en Manuel, le moteur en défaut et le vérin sorti. L'horloge marque 02:30:05 PM, lundi 12 octobre 2026. Les messages défilent, et la vidéo Motion-JPEG joue ;
- `09`, trois secondes plus tard : la vidéo et le texte défilant ont avancé ;
- `10`, la vidéo MP4 confiée à Media Foundation, sous Wine. Le Wine de cette machine a Media Foundation, mais pas les greffons GStreamer avec lesquels il décode le H.264. La vidéo n'y montre aucune image : elle garde son image d'attente et sa fiche (convoyeur.mp4, 320 × 180, 4,0 s), et l'application ne plante pas. Le décodage lui-même reste à voir sur un vrai Windows.

**Les sessions des versions d'avant**, rejouées avec l'exe 1.12.12, donnent les mêmes résultats qu'avant :
- `session-1129-ihm.txt` (lots D et E) : ses quatre captures, le build réussi (402 éléments générés, 326 compilés, 0 erreur), les cuves à 4.2, 7.5 et 2.8, et les 57 avertissements de la 1.12.9 ;
- `session-1127-ihm.txt` : ses huit captures, le build réussi (392 éléments générés, 317 compilés, 0 erreur), et `Total_Lu = 40`.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.9 :
- « Installation process succeeded ». Il a repris l'installation de la 1.12.9 : même dossier, et son journal de désinstallation complété ;
- avant comme après : `XpgAnalyzer-API.exe`, `XpgAnalyzer-IHM.exe`, et les raccourcis « XPGAnalyser API » et « XPGAnalyser IHM » ;
- les deux exe installés sont identiques à ceux qui ont été testés. Installés, ils répondent `XPGAnalyser API 1.12.12` et `XPGAnalyser IHM 1.12.12`, et l'API installée analyse `MAST.XPG` comme avant (891 variables, 29 POU).

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici). La section « Tester vous-même » donne le parcours à refaire chez vous ; son étape 7, la vidéo MP4, est la plus importante.

## Limites connues

Le détail est dans les notes de chaque lot, aux sections « Ce qui n'est pas fait ». L'essentiel :

**Ce qui n'a pas pu être essayé ici :**
- **Media Foundation n'a décodé aucune vidéo ici.** Il n'y a pas de Windows, et sous Wine l'appel pour le MP4 de la session n'a rien décodé (ce Wine n'a pas les greffons GStreamer du H.264), sans planter. Le décodeur Motion-JPEG intégré, lui, est essayé sous Linux et sous Wine. Un MP4, un WMV ou un MOV sont donc à essayer sur un vrai Windows.
- Ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé : il n'y a pas de MSVC ici.

**Lot F :**
- Les zones d'un bargraphe ou d'un cadran ne suivent les seuils que si elles sont vides (un objet neuf, ou des zones effacées) et si les seuils sont des nombres. Des zones écrites ne changent jamais.
- La barre de progression n'a pas de seuils. La jauge et le thermomètre n'ont pas de zones : leurs seuils sont des traits et une couleur.
- Le graphique en barres, l'histogramme et la courbe XY ne prennent pas leurs bornes à la variable. Ils acceptent seulement un nom sans `=`.
- Une borne héritée qui est un nom (`Max = Consigne_Max` dans Unités et formats) se lit en marche. L'éditeur dessine alors l'objet avec la valeur d'un objet neuf, et Générer ne compare pas un seuil à cette borne.
- Le volet Unités et formats ne propose pas les variables liées avec une mise à l'échelle.
- Le tableau « Éléments… » d'un tableau de structures montre le littéral de chaque élément dans une seule case, pas une colonne par membre.
- Le littéral positionnel `('Cuve A', 4.2)`, sans les noms des membres, n'est pas lu : il est refusé avec « écris Membre := valeur ».
- Une vue qui a un objet à plage héritée est recomposée à chaque cycle, comme une vue à joker. Pour 300 objets dans un projet de 500 lignes, cela coûte 14 à 16 ms par cycle, mesuré sur la construction de mise au point.

**Lot G :**
- Les paquets de symboles (`.xpgsymboles`, `.xpgvues`) emportent la propriété Équipement, mais pas le tableau de correspondance du projet. Dans le projet qui les reçoit, les membres se lient par leur nom.
- Les structures de l'automate (DDT) se lient par les types que publie XPGAnalyser API. Cela n'a pas été essayé avec un vrai programme.
- Un paramètre de popup sans type (ANY) ne se lie pas. Générer le dit.
- Le mode État d'office ne vaut qu'avec la liaison Équipement : un objet existant garde son dessin.
- Un membre renommé ne réécrit pas les formules tapées à la main qui le citent (`P1.Marche`). Ce n'est pas nouveau.

**Lot H :**
- La référence d'une courbe XY en échelle automatique n'a pas de poignées : elle se règle dans Contenu.
- L'aperçu de « Points depuis » dans l'éditeur montre les valeurs initiales. Depuis la 1.12.10, un tableau de structures peut en avoir par élément.
- Le son des vidéos n'est pas joué.
- Le WEBM n'est lu que par Media Foundation.
- L'horloge n'a pas de fuseaux nommés (Europe/Paris). « local » suit l'heure d'été du PC, mais un décalage fixe comme UTC+1 ne la suit pas.
- Le code QR n'a qu'un mode par texte (pas de segments mêlés), et ni kanji ni ECI.
- Le texte défilant ne colore pas un message d'alarme selon sa priorité.

## La suite

Le plan de l'audit des objets est terminé. Ce qui reste :
- **Essayer sur un vrai Windows** : la vidéo par Media Foundation (MP4, WMV, MOV), les deux exe et l'installateur. Le parcours « Tester vous-même » sert de recette.
- Les limites ci-dessus, si l'une d'elles vous gêne. Les plus demandées seraient sans doute une colonne par membre dans « Éléments… », le son des vidéos et les fuseaux nommés de l'horloge.
