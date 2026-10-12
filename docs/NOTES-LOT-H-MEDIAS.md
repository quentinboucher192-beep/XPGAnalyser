# Lot H, 2e partie : la vidéo, l'horloge, le texte défilant, le code QR (XPGAnalyser IHM 1.12.12)

Rédigé le 12/10/2026. Il reprend le lot H du plan de `docs/AUDIT-BIBLIOTHEQUE-OBJETS.md` (§ 4, et § 3.3 « Horloge »,
« Texte défilant », « Code QR », § 3.10 « Vidéo ») pour sa 2e partie : la vidéo décodée, l'horloge liée à une variable,
le texte défilant depuis une liste, le code QR au-delà de la version 10. La 1re partie (l'édition des sommets, les
points d'une source, le plan à zones, la référence XY) est un autre lot.

Ce document sert à la livraison : les notes de version et les paragraphes du guide se reprennent tels quels
(`src/help/ReleaseNotes.cpp`, `src/hmi/HmiGuideText.cpp` et les numéros de version n'ont pas été touchés). Chaque
point dit le sujet du guide où il va (sa clé dans `HmiGuideText.cpp`).

La règle « ce qui démarrait en 1.12.9 démarre toujours » est tenue : aucune propriété nouvelle n'est écrite dans un
objet qui ne s'en sert pas (un fichier 1.12.9 se relit et s'enregistre tel quel), aucun contrôle nouveau de Générer
n'est une erreur bloquante. Les changements visibles sur des objets existants sont dits plus bas (« Les risques »).

## Ce qui change pour vous (notes de version)

- **Le code QR jusqu'à la version 40** (`objet-code-qr`) : les 40 versions (de 21 x 21 à 177 x 177 modules) et les
  quatre niveaux de correction, comme le promettait le guide (avant : la version 10, 213 octets en M). Le texte prend
  le mode le plus compact : des chiffres seuls (jusqu'à 7 089 en L, 5 596 en M), l'alphanumérique - 0 à 9, A à Z,
  l'espace et $ % * + - . / : - (4 296 en L, 3 391 en M), sinon les octets du texte en UTF-8 (2 953 en L, 2 331 en
  M, 1 663 en Q, 1 273 en H) ; la version est la plus petite qui le loge.
- **La place qui reste dans un code QR** (`objet-code-qr`) : l'inspecteur montre, sous la correction, une ligne
  « Place » : « reste 2 302 octets en M, version 3 », « reste 12 chiffres en L, version 1 », ou « trop long :
  3 000 octets, 2 331 au plus en M ». Un texte calculé (à trous, une formule) se juge en marche.
- **Un code QR calculé trop long** (`objet-code-qr`) : l'objet montre « trop long » et ce que le niveau loge au plus ;
  la Console le dit une fois (un avertissement), pas à chaque cycle.
- **L'horloge liée à une variable** (`objet-horloge`) : « Heure affichée » montre l'heure d'une variable au lieu de
  celle de l'IHM - une TOD, une DT (DATE_AND_TIME), une DATE (à minuit), une STRING ISO 8601 (2026-10-12T14:30:00,
  avec ou sans fuseau Z / +02:00), un REAL en secondes (moins d'un jour : une heure du jour ; au-delà : un horodatage
  Unix), un TIME (la durée depuis minuit). Vide : l'heure de l'IHM, comme avant. Une valeur qui n'est pas une heure
  montre ###.
- **Les formats et le fuseau de l'horloge** (`objet-horloge`) : « Format de la date » (jj/mm/aaaa comme avant,
  jj/mm/aa, aaaa-mm-jj, jj mmm aaaa : 12 oct. 2026, jour jj mois aaaa : lundi 12 octobre 2026), « Heure sur » (24 h
  comme avant, ou 12 h : 02:30:05 PM ; l'horloge analogique dit AM ou PM), « Fuseau » (local comme avant, UTC, ou un
  décalage : UTC+2, UTC-5, UTC+5:30, à choisir ou à taper). Pour les deux styles, analogique et numérique.
- **Le texte défilant depuis une source** (`objet-texte-defilant`) : « Messages depuis » fait défiler, l'un après
  l'autre, les textes d'une LIST ou d'un tableau de textes (un message par élément ; le nombre suit la liste en
  marche), d'un joker du lot E (`Consignes[*].Texte`), ou les messages des « alarmes en cours » (d'un groupe
  d'alarmes, au choix). « Séparateur des messages » les sépare (par défaut, un point médian entre des espaces).
  L'éditeur montre les valeurs initiales de la liste. Vide, ou rien en ce moment : le texte, comme avant.
- **La vidéo décodée** (`objet-video`) : un AVI Motion-JPEG se décode partout (Linux, Wine, Windows) ; sous Windows,
  Media Foundation décode aussi les MP4 / H.264, WMV, MOV et les AVI d'autres codecs, image par image, sur un fil à
  part (sans tout charger). Avant : l'image d'attente et une barre qui bouclait, la vidéo jamais montrée.
- **La lecture d'une vidéo** (`objet-video`) : « Lecture » (une variable BOOL ou une formule : vraie, elle avance ;
  fausse, pause ; vide : elle joue ; une constante aussi : FAUX, NON, 0 la mettent en pause), « Départ » (à
  l'affichage, ou sur condition : elle joue tant que la condition est vraie et revient à l'image d'attente quand elle
  tombe), « Répétition » (en boucle, ou une fois : elle reste sur sa dernière image, et la Console le dit),
  « Variable de la position (s) » (la position, écrite au dixième de seconde ; une variable qui la refuse - inconnue,
  une structure - est dite une fois à la Console, pas à chaque cycle). L'image d'attente se montre jusqu'à la
  première image, et quand le format ne se décode pas sur cette plateforme ; le bandeau le dit (« MP4 : décodé sous
  Windows seulement ; ailleurs, convertir en Motion-JPEG AVI »).
- **La première image dans l'éditeur et les ressources** (`objet-video`, `ressources`) : l'éditeur et le gestionnaire
  de ressources montrent la première image d'une vidéo quand elle se décode ici (avant : l'image d'attente, ou un
  écran noir). Les ressources acceptent AVI, MOV et WMV (avant : MP4 et WEBM) ; leur fiche dit la durée, la taille et
  le codec (« Motion-JPEG (MJPG) »). Les dialogues Importer et Remplacer une ressource les proposent (leur filtre est
  fait de la liste des extensions acceptées : il ne peut plus l'oublier).
- **Les repères** (`dupliquer-reperes`) : l'heure d'une horloge (`=$Debut$`), la source d'un texte défilant
  (`$Messages$`), la Lecture et la variable de position d'une vidéo (`$Lecture$`, `$Pos$`) gardent leurs repères :
  Dupliquer les trouve et les remplace, la marche les lit sans leurs `$`. Renommer une variable fait suivre ces
  propriétés (« alarmes en cours » n'est pas un nom : il ne bouge pas).

## Pour le guide (paragraphes courts)

- `objet-code-qr` : « 1.12.12 : le code QR va jusqu'à la version 40 (177 x 177 modules) : 2 331 octets en M,
  5 596 chiffres. Le texte prend le mode le plus compact (des chiffres, des majuscules, sinon les octets en UTF-8) et
  la plus petite version qui le loge. La ligne Place de l'inspecteur dit ce qui reste ; un texte calculé trop long
  montre « trop long » sur l'objet, et la Console le dit une fois. »
- `objet-horloge` : « 1.12.12 : Heure affichée relie l'horloge à une variable - une TOD, une DT, une DATE, une STRING
  ISO 8601 ou un REAL en secondes ; vide, elle montre l'heure de l'IHM. Format de la date (12/10/2026, 2026-10-12,
  12 oct. 2026, lundi 12 octobre 2026), Heure sur 24 h ou 12 h, et Fuseau (local, UTC, UTC+2...) valent pour les deux
  styles. Une heure du mur (DT, TOD, DATE) se montre telle quelle ; l'heure de l'IHM et une heure qui dit son fuseau
  sont converties dans celui de l'horloge. »
- `objet-texte-defilant` : « 1.12.12 : Messages depuis fait défiler les textes d'une LIST ou d'un tableau de textes,
  d'un joker (Consignes[*].Texte) ou les alarmes en cours (d'un groupe), l'un après l'autre, séparés par le
  Séparateur. Le nombre de messages suit la liste en marche ; l'éditeur montre ses valeurs initiales. Sans source, ou
  une source vide : le texte de l'objet. »
- `objet-video` : « 1.12.12 : la vidéo se décode - un AVI Motion-JPEG partout, et sous Windows les MP4 (H.264), WMV,
  MOV. Lecture (une variable BOOL) la met en pause, Départ la fait partir à l'affichage ou sur condition, Répétition
  la fait boucler ou s'arrêter sur sa dernière image ; sa position peut s'écrire dans une variable. Avant la première
  image, ou si le format ne se décode pas ici, l'image d'attente se montre et le bandeau dit pourquoi : convertir en
  Motion-JPEG AVI (`ffmpeg -i film.mp4 -an -c:v mjpeg -q:v 3 film.avi`) pour qu'elle se lise partout. »
- `ressources` : « 1.12.12 : les vidéos AVI, MOV et WMV s'importent ; la fiche montre la première image quand elle se
  décode ici (un AVI Motion-JPEG partout, le reste sous Windows), sa durée, sa taille, son codec. »
- `verifier` : « 1.12.12 : Générer dit, en information, qu'une vidéo MP4, WMV, MOV ou WEBM ne se décode que sous
  Windows ; en avertissement, une horloge liée à ce qui n'est pas une heure, un fuseau illisible, la source d'un texte
  défilant qui n'est pas une liste de textes, un départ sur condition sans condition. »

## Tester vous-même (pour la livraison)

1. Posez un **code QR** et collez dans son texte 1 000 caractères : la ligne Place dit « reste 1 331 octets en M,
   version 26 » (en minuscules) ; avec des chiffres seuls, il en reste bien plus. Au-delà de 2 331 octets en M,
   Générer le dit et l'objet montre « trop long ».
2. Posez une **horloge numérique**, tapez dans Heure affichée `TOD#14:30:05`, choisissez Heure sur « 12 h » et Format
   de la date « jour jj mois aaaa » : elle montre 02:30:05 PM et la date en toutes lettres. Fuseau « UTC+9 » sans
   heure affichée : l'heure de Tokyo.
3. Créez une variable `Annonces : LIST OF STRING := ['Bienvenue', 'Casque obligatoire']`, posez un **texte défilant**
   et choisissez Messages depuis « Annonces » : l'éditeur montre « Bienvenue · Casque obligatoire ». En marche, un
   bouton `LIST_ADD(Annonces, 'Pause à 10 h')` ajoute un message. « alarmes en cours » : les messages des alarmes
   actives.
4. Convertissez une vidéo en Motion-JPEG (`ffmpeg -i film.mp4 -an -c:v mjpeg -q:v 3 film.avi`), importez-la dans les
   ressources : sa fiche montre sa première image. Posez une **vidéo**, réglez Lecture sur une variable BOOL et la
   variable de position sur un REAL : en marche, elle joue, se met en pause, et la position avance. Un MP4 se lit
   sous Windows ; sous Linux, l'image d'attente et le bandeau le disent.

## Ce que Générer vérifie de plus

Rien de bloquant (des avertissements et des informations), sauf ce qui l'était déjà :
- **le code QR** : un texte fixe trop long reste une erreur, comme avant, mais avec les 40 versions et le mode le
  plus compact (il ne se dit plus qu'au-delà de 2 331 octets en M ; avant : 213). Le texte est lu sans les `$` de ses
  repères (ce que l'objet encode). Un texte calculé (à trous, une formule) se juge en marche ;
- **l'horloge** (avertissements) : une heure fixe illisible (« 'abc' n'est pas une heure »), un fuseau illisible
  (l'heure locale sera montrée), une valeur liée qui n'est pas une heure (un BOOL, une structure, une liste) ;
- **le texte défilant** : la source de ses messages inconnue, ou qui n'est pas une liste (un REAL, un TUPLE) - des
  avertissements ; une STRING seule (un seul message), une MAP (ses valeurs), des éléments qui ne sont pas des textes,
  « alarmes en cours » dans un projet sans alarme - des informations ; un joker se vérifie comme ceux du lot E ;
- **la vidéo** : son format, quand toutes les plateformes ne le décodent pas (une information : « MP4 : décodé sous
  Windows seulement ; ailleurs, convertir en Motion-JPEG AVI », « AVI (XVID) : ... », « WEBM : ... avec les
  extensions vidéo web ») ; un départ sur condition sans condition, une Lecture ou une variable de position
  inconnues, une variable de position qui ne prend pas des secondes (un BOOL, une date) - des avertissements. La
  condition du départ se vérifie comme celle d'un GIF (une variable inconnue y est une erreur, comme avant pour toute
  condition).

## Les essais

- `hmi_qr_test` (nouveau, 117 contrôles) : les symboles de référence module pour module (le générateur de Project
  Nayuki, qrcodegen 1.8.0, sans relever le niveau), dont l'exemple de la norme (01234567 en 1-M) et ses mots de
  l'annexe I (16 mots de données, 10 mots correcteurs) ; HELLO WORLD en M et en Q ; un texte UTF-8 ; les plus longs
  textes des quatre niveaux en version 40 (octets, chiffres, alphanumérique). Puis CHAQUE VERSION À CHAQUE NIVEAU
  (160 symboles en octets, 80 en chiffres et en alphanumérique), relue par un lecteur écrit d'après la norme et non
  d'après l'encodeur : la place des motifs (les modules de données comptent les mots de la table 1 et leurs bits de
  reste), les deux copies du format et de la version (codes BCH justes), l'entrelacement des blocs de la table 9,
  les syndromes de Reed-Solomon de chaque bloc (tous nuls), le texte relu à l'identique, la capacité de la table 7 ;
  un caractère de plus passe à la version suivante. La place qui reste et le texte trop long (« reste 2 302 octets en
  M, version 3 », « trop long : 2 332 octets, 2 331 au plus en M »).
- `hmi_video_test` (nouveau, 73 contrôles) : un AVI Motion-JPEG fait dans l'essai (des images JPEG écrites par
  stb_image_write) : sa taille, sa cadence (10 images/s, 29,97), ses 3 images et leurs couleurs à la perte JPEG près,
  une image vide (la précédente), un fichier coupé (les images entières), une image sans tables de Huffman (stb_image
  seule la lit mal ; avec les tables de la norme elle se lit) ; la ressource (AVI, 0,3 s, « Motion-JPEG (MJPG) ») ; la
  source d'images (l'image à une heure, son numéro qui change avec elle) ; un MP4 sous Linux (pas de source, et
  pourquoi). LA LECTURE avec une horloge factice : à l'affichage, en boucle (le tour), pause et reprise, une fois
  (finie sur sa dernière image), sur condition (part, avance, revient à l'image d'attente, repart du début), une durée
  inconnue. Le moteur : la position écrite dans sa variable, la pause par sa Lecture (une variable, et la constante
  FAUX), le départ sur condition, la fin dite à la Console, une vidéo de la 1.12.9 (sans réglage) qui joue à
  l'affichage en boucle, une variable de position inconnue dite une seule fois en 7 cycles (avant la correction : 7
  erreurs). Générer : le conseil pour un MP4, le départ sans condition, la variable de position inconnue, rien sur
  l'AVI, rien de bloquant. LA FILE DES IMAGES D'AVANCE de Media Foundation (`HmiVideoQueue.hpp`, sans le fil, avec un
  faux décodeur) : 3 images d'avance, la première image d'un MP4 à images B (à 0,067 s) montrée à 0 s - l'aperçu de
  l'éditeur (sans cette règle : rien, 3 contrôles en échec) -, les images en retard qui défilent d'un coup, le saut en
  arrière de la boucle, une image d'avant le saut jetée.
- `hmi_test` (`HMI_TEST_11212=1` les lance seuls) : `horloge11212` (chaque source : TOD, DT et ses millisecondes,
  DATE, STRING ISO avec et sans fuseau, à la française, REAL d'un jour et horodatage Unix, TIME ; l'illisible ; les
  fuseaux ; les cinq formats de date, le 1er du mois, 12 h et 24 h ; en marche, la copie que l'écran dessine pour une
  DT, une TOD, un REAL en UTC+2 sans perte, une STRING en Z, un BOOL refusé ; Générer ; enregistré et relu, une
  horloge d'avant sans les propriétés nouvelles), `defilant11212` (l'aperçu des valeurs initiales d'une LIST et d'un
  ARRAY OF STRING, d'un repère, des alarmes d'un groupe ; en marche, une LIST qui grandit, un ARRAY, un joker
  `Consignes[*].Texte`, une STRING seule, les alarmes en cours et d'un groupe, plus d'alarme ; Générer ; le
  renommage), `qrEnMarche11212` (un code QR calculé - une formule, un texte à trous - trop long : dit une fois chacun
  à la Console, en avertissement, et « trop long » sur l'objet).
- `hmi_editor_test` (`HMI_TEST_11212=1`) : `lotH11212_editeur` (l'inspecteur d'une horloge, d'un texte défilant,
  d'une vidéo, d'un code QR - leurs lignes, leurs choix, la place en lecture seule et jamais enregistrée, les
  propriétés absentes d'un objet neuf ; le dessin de l'éditeur : l'heure fixe en 12 h et la date en toutes lettres,
  les valeurs initiales d'un texte défilant, ce qui manque à une vidéo MP4).
- `hmi_reperes_test` : `reperesLotH11212` (`=$Debut$`, `$Messages$`, `$Pos$`, `$Lecture$` trouvés par Dupliquer dans
  le mode de leur rôle, remplacés dans les copies, lus sans leurs `$` en marche ; Compiler et Générer muets).
- Essais existants changés, parce que le comportement qu'ils figeaient a changé exprès (`hmi_test`, `lot 9`) :
  `qrCapacity` vaut 2 953 / 2 331 / 1 663 / 1 273 (avant : 271 / 213 / 151 / 119) ; la grille de HELLO WORLD en M
  est celle du mode alphanumérique (avant : octets, masque 4) ; 80 chiffres en Q passent en version 4 (avant : 7) ;
  214 octets en M passent en version 11 (avant : trop long) ; le code QR trop long de Générer a 3 000 octets (avant :
  300).

## Ce qui n'est pas fait

- **Media Foundation n'a pas été lancé** : il n'y a pas de Windows ici. Le code (`HmiVideoMf.cpp`) se compile avec
  MinGW (`x86_64-w64-mingw32-g++-posix -std=c++20 -fsyntax-only`, sans avertissement), les bibliothèques se lient
  dans CMake (mfplat, mfreadwrite, mfuuid, mf) et dans le projet Visual Studio ; la file de ses images d'avance
  (`HmiVideoQueue.hpp`) est vérifiée ici avec un faux décodeur ; le reste (le lecteur de Windows, son fil, le fichier
  temporaire) est à voir sur un vrai Windows et sous Wine (Wine n'a pas tous les décodeurs H.264 : un AVI
  Motion-JPEG y est le format sûr).
- **Ajouté à l'intégration (12/10, midi) : Media Foundation se charge à la demande.** Lié à l'édition des liens,
  l'exe importait `MFPlat.DLL` et `MFReadWrite.dll` : un Windows qui ne les a pas (une édition N sans le Media
  Feature Pack, un Windows Server sans ses fonctions vidéo) n'aurait plus démarré ni XPGAnalyser IHM ni
  XPGAnalyser API. Les cinq fonctions qui servent (`MFStartup`, `MFShutdown`, `MFCreateAttributes`,
  `MFCreateMediaType`, `MFCreateSourceReaderFromURL`) se chargent maintenant depuis System32 à la première vidéo
  qui en a besoin ; sans elles, l'objet montre son image d'attente et le conseil (« Media Foundation manque à ce
  Windows… installer le Media Feature Pack ; sinon, convertir en Motion-JPEG AVI »). Les bibliothèques d'import
  restent liées pour leurs GUID : aucune fonction n'en est importée (la table d'importation de l'exe MinGW est à
  relire après sa compilation : voir `docs/LIVRAISON-1.12.12.md`).
- **Le son des vidéos** n'est pas joué (la vidéo est muette ; les propriétés `autoplay` et `muted` restent sans
  effet).
- **Le WEBM** n'est essayé que par Media Foundation sous Windows (avec les extensions vidéo web) ; ailleurs, l'image
  d'attente et le conseil.
- **La durée d'un WMV** se lit dans son en-tête ASF (lu ici, sans décodeur) ; un fichier dont l'en-tête ne la dit pas
  lit sa vidéo sans fin connue (la boucle ne repart pas). Le MOV et le MP4 la lisent dans `mvhd`.
- **Les membres publics d'une vidéo** (Vue.Objet.Position, Playing) et une action « Jouer la vidéo » n'existent pas :
  la Lecture et la variable de position en tiennent lieu.
- **Le texte défilant** : un message ne se colore pas selon la priorité de son alarme ; un message qui contient le
  séparateur n'est pas échappé.
- **L'horloge** : pas de fuseaux nommés (Europe/Paris) ni d'heure d'été calculée pour un décalage fixe (UTC+1 reste
  UTC+1 en été) - « local » suit l'heure d'été du PC.
- **Le code QR** : un seul mode par texte (pas de segments mêlés chiffres / octets), pas de mode kanji ni ECI.

## Les propriétés nouvelles et le format

Toutes absentes d'un objet qui ne s'en sert pas : l'inspecteur les montre à leur valeur par défaut et ne les écrit
qu'à la première saisie (un fichier 1.12.9 se relit tel quel, et s'enregistre tel quel).
- Horloge : `value` (« Heure affichée », rôle Date : la copie dessinée porte son texte exact), `dateFormat`,
  `hourFormat`, `timeZone` (des choix).
- Texte défilant : `messagesFrom` (« Messages depuis », un nom : une variable, un joker ou « alarmes en cours »),
  `separator` (« Séparateur des messages », un texte) et `group` (le groupe des alarmes en cours) - deux clés qui
  existaient déjà ailleurs.
- Vidéo : `playing` (« Lecture », une formule booléenne), `start`, `condition`, `play` (« Départ », sa condition,
  « Répétition » - les clés du GIF animé), `positionVariable` (« Variable de la position (s) », une formule écrite).
- Code QR : rien de nouveau dans le fichier ; la ligne « Place » de l'inspecteur est calculée.
- Ressources : les formats AVI, MOV, WMV.

## Les risques pour les projets existants

- **Le dessin de certains codes QR change** : un texte fait seulement de chiffres, ou de majuscules, chiffres, espace
  et $ % * + - . / :, prend un mode plus compact - la grille change (souvent plus petite), le texte lu par un
  lecteur reste le même. Un texte avec des minuscules ou des accents (une adresse https://..., une phrase) garde
  exactement sa grille, et un texte vide aussi.
- **Les vidéos d'un projet existant jouent** là où elles se décodent : sous Windows, un MP4 part à l'affichage, en
  boucle (les valeurs par défaut de Départ et de Répétition), muet ; l'éditeur montre sa première image au lieu de
  l'image d'attente. Ailleurs, le bandeau d'une vidéo MP4 en marche dit « MP4 : décodé sous Windows seulement ;
  ailleurs, convertir en Motion-JPEG AVI », et la barre fine suit la position de la lecture (avant : elle bouclait
  sur l'horloge). Générer ajoute une information par vidéo MP4 / WEBM.
- **Une vidéo qui joue sous Windows** écrit un fichier temporaire (le fichier de la ressource, dans le dossier TEMP,
  effacé à la fermeture de l'objet) et décode sur un fil à part : la mémoire et le processeur d'une vidéo HD.
- **Un AVI, un MOV ou un WMV importé avant comme document** (lot API 8 : « tout autre fichier ») devient une vidéo :
  le gestionnaire de ressources montre sa fiche de vidéo (sans durée ni taille, lues à l'import : réimporter le
  fichier les donne) ; inutilisé, Générer le dit en information. Aucun objet ne pouvait le citer comme vidéo avant
  (c'était une erreur) : rien ne casse.
- **Rien d'autre ne change** pour une horloge, un texte défilant ou un code QR qui ne se servent pas des propriétés
  nouvelles ; aucun contrôle nouveau n'est bloquant.
