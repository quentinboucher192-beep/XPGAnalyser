# XPGAnalyser 1.12.8 et 1.12.9 : les listes en tableau, les choix, les tailles dynamiques (lots D et E)

Livrées le 12/10/2026 au matin, juste après la 1.12.7 de la nuit (`docs/LIVRAISON-1.12.7.md`).

Elles répondent à la fin de la demande du 11/10 au soir :

> « ok fait tout ça et enchaine sur les lots D E le tout pour demain matin »

Ce sont les **lots D et E** du plan de l'audit de la bibliothèque d'objets (`docs/AUDIT-BIBLIOTHEQUE-OBJETS.md`, § 4) :
- **1.12.8, le lot D : les listes en tableau et les choix** (§ 2.4, § 2.5). Une ligne par élément dans Contenu, au lieu des listes `a;b;c` tapées en parallèle. Des cases à cocher et des listes à choix au lieu de codes à taper.
- **1.12.9, le lot E : les tailles dynamiques** (§ 2.3, § 3.8). « 8 cuves, 8 pompes » s'écrit une fois : `Cuves[*].Niveau`.

Un seul installateur les porte toutes les deux : la 1.12.9. Tout se passe dans **XPGAnalyser IHM**. XPGAnalyser API ne change pas, à part son numéro de version.

Le détail objet par objet, ce que Générer vérifie, les essais et les limites sont dans `docs/NOTES-LOTS-D-E.md`.

## 1. La 1.12.8 : les listes en tableau et les choix

**Contenu, une ligne par élément.** Le sélecteur, la liste déroulante, les boutons radio et la liste ont leur onglet Contenu, avec une ligne par choix : son libellé, et la valeur qu'il écrit (vide : son rang).
- Ajouter, retirer, monter et descendre emmènent le libellé et sa valeur ensemble. Avant, il fallait aligner à la main `positions` (ou `items`) et `values`.
- Un « ; » nu dans un libellé est refusé tout de suite. Un repère `$Mode$` est gardé tel quel.

Le même tableau sert aussi à quatre autres objets :
- les zones d'un bargraphe et d'un cadran (De, À, Couleur). Une borne en % de l'échelle (`60 %`) suit Min et Max quand ils changent ;
- les plages du programmateur horaire (Jours, De, À), avec ce que la résolution en fait : « arrondie à 14:00-17:00 », ou « disparaît : plus courte qu'une case » ;
- l'unité et le format de chaque ligne d'un tableau de variables ;
- les lignes ou les états d'un chronogramme.

**Des cases à cocher au lieu de codes à taper** :
- le sélecteur de langue coche les langues du projet ;
- la gestion des utilisateurs coche ses boutons ;
- le résumé par zone coche ses zones, y compris les groupes des alarmes des objets.

Rien de coché vaut toujours « toutes ».

**Choisir au lieu de taper**, dans l'inspecteur :
- la vue après connexion se choisit parmi les vues, et suit le renommage d'une vue ;
- les lettres d'un instrument ISA se choisissent dans une liste qui propose leur unité (TT : °C) ;
- la position fixe d'une vanne 3 voies : fermée, 1-2, 1-3, 2-3 ;
- l'onglet montré d'un conteneur, par son nom.

Chaque liste à choix accepte aussi un nom tapé : son dernier choix est « Taper un nom ou un repère $…$ ». Un repère s'y voit en ambre.

**Les colonnes d'un tableau depuis une source.** Avec « Lignes depuis », Contenu montre les colonnes de la source. On choisit celles qui se montrent, leur ordre, leur titre et leur format.

**Le calendrier des postes** : Configuration › Exécution. Un compteur de production dont les débuts de poste disent « calendrier » le suit.

**Générer** dit de nouvelles choses, toujours en avertissement ou en information, sans rien bloquer :
- deux choix qui écrivent la même valeur ;
- une plage du programmateur qui disparaît ou qui est arrondie ;
- deux zones qui se chevauchent ;
- un calendrier absent ;
- une colonne choisie que la source n'a pas.

## 2. La 1.12.9 : les tailles dynamiques

**Le joker `[*]`.** Dans une liste de formules, `Cuves[*].Niveau` donne un élément par indice de `Cuves`. Cela vaut pour une plume de courbe, une barre, une part, un axe de radar, une courbe Y, une ligne de chronogramme ou de tableau de variables.

La source peut être :
- un tableau de l'IHM (`ARRAY[1..8]` : 1 à 8) ;
- une LIST ou un VECTOR : le nombre suit la liste en marche, avec une plume de plus au cycle suivant ;
- une MAP : une barre ou une part par clé, nommée par sa clé ;
- une énumération ;
- un membre ;
- le paramètre d'une popup.

Les listes parallèles suivent : `Cuves[*].Nom` nomme chaque élément, la couleur tourne dans la palette, l'unité et le format se répètent.

**Toute une structure, tout un dossier.** `Pompe.*` donne un élément par membre simple de la structure. Les mêmes formes marchent pour :
- `Moteurs[*].*` : chaque membre de chaque moteur ;
- `Mesure.*` : chaque élément d'un TUPLE ;
- `Ligne/Convoyeur/*` : chaque variable simple du dossier.

**Contenu propose les sources** (« Ajouter depuis une source »). Chaque ligne à joker dit ce qu'elle deviendra : « [*] : 8 éléments de Cuves ». L'éditeur dessine l'objet comme il démarrera, par exemple une barre par cuve.

**La courbe XY en profil.** Une Variable X à joker (`Profil[*].X`) trace, à chaque échantillon, tous les points de la source. La courbe de référence se lit aussi dans une source.

**Depuis une source** :
- la barre de navigation lit ses vues dans `Vues[*]`, ou dans un dossier de vues `Process/*` ;
- le résumé par zone lit ses zones dans `Zones[*].Nom` ;
- les onglets suivent une énumération : `T_MODE[*]` ;
- « États depuis » : un multi-états ou un chronogramme lit ses états en marche dans une MAP, une LIST, un tableau ou une énumération.

**« Répéter depuis »** pose une instance de symbole une fois par élément d'un tableau ou d'une LIST :
- ses arguments qui citent `[*]` (`Cuve := Cuves[*]`) reçoivent l'élément ;
- les copies se suivent et portent les noms `Cuve_2`, `Cuve_3`… ;
- les alarmes du symbole sont générées pour chaque copie.

**Des tableaux et des listes depuis des structures** :
- « Éléments depuis » `Pompes[*].Nom` montre le membre choisi de chaque élément et écrit son indice ;
- « Lignes depuis » un tableau de structures imbriquées montre une colonne par membre simple.

**Les renommages suivent.** Renommer une variable, une énumération ou un dossier réécrit ses jokers partout.

**Générer** vérifie un joker sur son premier élément ; avant, `Cuves[*].Niveau` était une erreur bloquante. Une source inconnue ou vide est dite, en avertissement.

## Tester vous-même

1. Posez un **sélecteur** et ouvrez Contenu : une ligne par choix, avec son libellé et sa valeur. Montez un choix : sa valeur suit.
2. Posez un **bargraphe**, réglez ses zones dans Contenu avec une borne à `60 %`, puis passez Max de 100 à 200 : la zone suit (120).
3. Posez un **programmateur horaire** en résolution 1 h, avec une plage 13:30-17:00 : Contenu dit « arrondie à 14:00-17:00 ».
4. Créez un type `T_Cuve` (Nom, Niveau) et une variable `Cuves : ARRAY[1..3] OF T_Cuve`. Posez un **graphique en barres** avec Variables `Cuves[*].Niveau` et Noms `Cuves[*].Nom` : trois barres, nommées par leurs cuves.
5. Posez une **courbe** sur une `LIST OF REAL` avec `Bacs[*]` : une plume par bac. En marche, ajoutez un bac (`LIST_ADD`) : une plume de plus.
6. Posez un **camembert** sur une `MAP[STRING] OF INT` avec `Stock[*]` : une part par clé.

## Installer

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.9.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.9-portable.zip` et `XPGAnalyser-IHM-1.12.9-portable.zip` : chaque application, sans installation ;
- ce document ;
- les captures `1129_ihm_*`, prises sous Wine avec l'exe livré.

1. Lancez `XPGAnalyser-Setup-1.12.9.exe`. Il met à jour la version installée, la 1.12.7 de la nuit ou une plus ancienne : même identité d'installation, données reprises sans être déplacées. Il pose les deux applications et leurs deux raccourcis. Vos projets s'ouvrent tels quels : aucun format ne change.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.12.9.exe` (29,0 Mo, 30 392 727 octets) :
`5C052292C86371BD4FFAEC7D2EE6B62971C54FD2F0088E090AE2B5E2609032EA`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `XPGAnalyser-API-1.12.9` ou `XPGAnalyser-IHM-1.12.9`. Décompressez, puis lancez l'exe.
- `XPGAnalyser-API-1.12.9-portable.zip` (21,7 Mo) :
  `D2C44DB54F87D46D9E7BF2254DB7738E55EC2C96DCB4F8A05A0E417C08FA5769`
- `XPGAnalyser-IHM-1.12.9-portable.zip` (21,7 Mo) :
  `9B9F653D3502F299186D34ACE4BE561EDCEADA3A162AFBD11A1C37E54E4B45A6`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe, 72 sur 72. Elle comprend :
- 7 073 contrôles dans `hmieditor` (6 927 à la 1.12.7) et 5 172 dans `hmi` (5 045) ;
- 268 dans `hmireperes` et 37 dans `renameplan` ;
- ceux de la 1.12.7, qui passent toujours : 2 177 dans `hminatives`, 966 dans `hmidecl`, 269 dans `hminatives1127`, 126 dans `hmisetring`.

Ce que couvrent les essais des lots D et E (`HMI_TEST_1127=1` les lance seuls) :
- **le lot D** (`lotD1127_contenu` et `lotD1127_choix` dans `hmieditor`, `lotD1127` dans `hmi`) :
  - Contenu : les choix d'un sélecteur et d'une liste déroulante, l'unité et le format d'un tableau de variables, les états d'un chronogramme, les zones en %, les plages et leur arrondi, les cases à cocher des langues, des boutons et des zones, les colonnes d'une source ; les repères gardés case par case ;
  - les listes à choix : la liste, puis « Taper un nom ou un repère » au clavier et à la souris ; `$Zone_A$` gardé et montré en ambre ; la vue après connexion et son renommage ; les lettres ISA et leur unité ; la vanne 3 voies ; l'onglet montré ;
  - le calendrier des postes : enregistré et relu ; un projet sans calendrier reste inchangé ; le compteur prend son poste au calendrier, en marche ;
  - Générer : les doublons, la plage disparue, les zones qui se chevauchent, le calendrier absent. Rien de bloquant ;
- **le lot E** (dix essais dans `hmi`, `lotE1127_editeur` dans `hmieditor`, `repereEtJoker1127` dans `hmireperes`) :
  - en marche : un graphique en barres sur un `ARRAY[1..3]` de structures, ses noms et ses valeurs ; une courbe sur une `LIST` qui grandit, la plume de plus tracée ; un camembert sur une `MAP` ; une barre de navigation sur une `LIST OF STRING`, et son clic ; des onglets depuis une énumération ; un multi-états depuis une MAP qui grandit ;
  - `Pompe.*`, `Moteurs[*].*`, un `TUPLE`, les dossiers `Ligne/Convoyeur/*` et `Process/*` ;
  - « Répéter depuis » : trois instances, leurs noms, leurs arguments, leur place, leurs valeurs, l'alarme du symbole pour chaque copie ;
  - la courbe XY en profil, ses points refaits à chaque échantillon ;
  - l'éditeur : l'aperçu d'un graphique `Cuves[*]` dessiné avec ses 8 barres, Contenu et « Ajouter depuis une source » ;
  - les propriétés nouvelles enregistrées et relues ; un objet qui ne s'en sert pas ne les porte pas ;
  - `$Cuves$[*].Niveau` dupliqué en `$Ballons$[*].Niveau`, chacun déplié sur sa variable en marche ;
  - les renommages qui réécrivent les jokers : une variable, une énumération, un dossier de vues, un dossier de variables glissé dans un autre.

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `XPGAnalyser API 1.12.9` et `XPGAnalyser IHM 1.12.9` ;
- `XpgAnalyzer-API.exe --cli MAST.XPG` analyse le projet d'essai (891 variables, 29 POU), comme avant.

**La session sous Wine** (`tools/sessions/session-1129-ihm.txt`) a tourné avec l'exe livré, sur Armoire_Gaz. Un petit outil y a ajouté :
- le type `T_Cuve_DE` (Nom, Niveau) ;
- les variables `Mode_DE`, `Pression_DE` et `Cuves_DE : ARRAY[1..3] OF T_Cuve_DE` ;
- `Bacs_DE : LIST OF REAL` (`[1.5, 2.5, 3.5]`) et `Stock_DE : MAP[STRING] OF INT` (vis 30, ecrou 50, rondelle 20) ;
- le script `Init_LotDE`, au démarrage, qui nomme et remplit les trois cuves ;
- la vue `Vue_LotDE` : un sélecteur, un bargraphe à zones, un programmateur, puis un graphique en barres, un tableau de variables, un camembert et une courbe, chacun sur un joker.

Chaque ligne de la session a réussi :
- le sélecteur (capture `1129_ihm_01`) : Contenu montre Arrêt, Auto et Manu, une ligne par choix. Le premier écrit 0 ;
- le bargraphe (`02`) : ses trois zones, 0…60, 60…80 et 80…100. La première est verte (`#2ECC71`) ;
- le programmateur (`03`) : ses trois plages, Lu-Ve, Lu-Ve et Sa, en résolution 1 h. La deuxième, 13:30-17:00, dit « En marche : arrondie à 14:00-… » (la case étroite coupe la fin) ;
- la simulation (`04`) : le build réussit (402 éléments générés, 326 compilés, 0 erreur). Dans `Vue_LotDE` :
  - `Cuves_DE[*].Niveau` donne trois barres, 4.2, 7.5 et 2.8, nommées Cuve A, Cuve B et Cuve C par `Cuves_DE[*].Nom` ;
  - le tableau de variables a trois lignes : 4.20, 7.50 et 2.80 m ;
  - `Stock_DE[*]` donne une part par clé : ecrou 50, rondelle 20, vis 30 ;
  - `Bacs_DE[*]` donne une plume par bac : 1.5, 2.5 et 3.5.

**La session de la 1.12.7** (`session-1127-ihm.txt`) a tourné de nouveau avec l'exe 1.12.9 : ses huit captures, le build réussi (392 éléments générés, 317 compilés, 0 erreur) et `Total_Lu = 40`. Les valeurs calculées n'ont pas bougé avec les lots D et E.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.7 :
- « Installation process succeeded ». Il a repris l'installation de la 1.12.7 : même dossier, et son journal de désinstallation complété ;
- avant comme après : `XpgAnalyzer-API.exe`, `XpgAnalyzer-IHM.exe`, et les raccourcis « XPGAnalyser API » et « XPGAnalyser IHM » ;
- les deux exe installés sont identiques à ceux qui ont été testés. Installés, ils répondent `XPGAnalyser API 1.12.9` et `XPGAnalyser IHM 1.12.9`, et l'API installée analyse `MAST.XPG` comme avant.

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici). La section « Tester vous-même » donne le parcours à refaire chez vous.

## Limites connues

Le détail est dans `docs/NOTES-LOTS-D-E.md`, aux sections « Ce qui n'est pas fait ». L'essentiel :

**Trouvé en préparant la session, plus ancien que les lots D et E, pas corrigé :**
- La valeur initiale d'une variable IHM `ARRAY OF STRUCT` écrite en littéral (`[(Nom := 'Cuve A', Niveau := 4.2), …]`) est ignorée en simulation : les membres partent de 0 et de ''. Générer ne le dit pas. La session remplit donc ses cuves par un script au démarrage.

**Lot D :**
- Les colonnes d'une source de tableau n'ont pas de largeur propre : elles gardent celles du tableau, par position. Contenu ne propose les colonnes que pour une variable IHM ; pour un paramètre de popup ou un membre, on tape `sourceColumns` dans l'inspecteur.
- Un choix de sélecteur n'a pas de couleur : le moteur n'en a pas.
- Un « ; » dans un libellé est refusé, sans échappement.
- Dans l'inspecteur, les vues de la barre de navigation restent une case de texte. Contenu les choisit déjà.
- Une liste à choix ne filtre pas pendant qu'on tape dans la liste elle-même.
- Le tableau des sommets (lignes, polygones, zones du plan) relève du lot H.

**Lot E :**
- « Lignes depuis » et « Éléments depuis » ne donnent rien en marche à travers un paramètre de popup (`Cuve`, `Cuve.Mesures` dans une popup ouverte pour `Cuves[2]`). Le joker `Cuve.Mesures[*]` des graphiques marche, et une LIST passée en paramètre aussi.
- L'éditeur dessine un objet à joker d'après le projet, pas d'après la marche : une LIST montre sa valeur initiale, et un nom calculé montre le numéro de l'élément.
- Un tableau à deux dimensions se déplie case par case, pas en une série par ligne.
- « Répéter depuis » une LIST : les copies suivent la liste en marche, mais les alarmes du symbole ne sont générées que pour un tableau de l'IHM.
- `Lignes[*].*` sur une LIST de structures n'est essayé que sans moteur.
- Un conteneur à onglets à joker range ses objets par numéro de page, comme avant. « Ajouter depuis une source » ne lui est pas proposé : son joker se tape, ou vient de « Depuis une énumération ».
- La courbe XY en profil relit toute sa source à chaque échantillon (100 ms). Il n'y a pas encore de période d'échantillonnage pour des milliers de points.
- Les lignes et les polygones n'ont pas encore de « points depuis ».
- Une vue qui contient des jokers est recomposée toutes les 100 ms ; ce coût n'est pas mesuré sur un gros projet.

## La suite

Les lots restants du plan de l'audit (§ 4) :
- **F. Plages et seuils de la variable** : Unités et formats gagne min, max et les quatre seuils, et chaque objet à échelle en hérite ;
- **G. Synoptique et symboles** : la liaison « Équipement » par structure, le mode « État », la vérification des arguments de symbole, les types proposés par « Créer un symbole » ;
- **H. Plus tard** : l'édition des sommets sur le dessin, l'horloge liée à une variable, le texte défilant depuis une liste…

S'y ajoute la valeur initiale d'un `ARRAY OF STRUCT` : la lire, ou que Générer dise qu'elle n'est pas lue.
