# XPGAnalyser 1.12.5 : les défauts de l'audit des objets corrigés (lot A)

Livrée le 11/10/2026 dans l'après-midi.

Elle répond à la demande du 11/10, après l'audit de la bibliothèque d'objets (`docs/AUDIT-BIBLIOTHEQUE-OBJETS.md`) :

> « ok go »

C'est le **lot A** du plan de l'audit. Il corrige ce qui était faux sans changer la façon de régler les objets :
- les 18 défauts trouvés pendant l'audit (D1 à D18) ;
- les défauts propres aux objets, signalés objet par objet.

Les lots B et C (un rôle par propriété, les repères partout) viennent ensuite, comme recommandé.

Tout se passe dans **XPGAnalyser IHM**. XPGAnalyser API gagne seulement les entiers sur 64 bits (`LINT`, `ULINT`, `LWORD`) de son simulateur.

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.5.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.5-portable.zip` et `XPGAnalyser-IHM-1.12.5-portable.zip` : chaque application, sans installation ;
- ce document ;
- les captures `1125_ihm_*`, prises sous Wine avec l'exe livré.

## 1. Ce qui change pour vous

Le principe : **ce que l'écran montre, le clic le fait**, et chaque type en entrée est lu comme il se doit.

| | Avant (1.12.4) | Maintenant (1.12.5) |
|---|---|---|
| Sélecteur de date lié à une `DATE`, `TOD`, `DT` | des secondes écrites : une date de 1970 | des millisecondes, à l'heure du mur (comme `D#`, `TOD#`, `DT#`) |
| Bandeau, liste, résumé d'alarmes au groupe calculé (`=Zone`) | le clic et Acquitter lisaient le groupe fixe : une autre alarme | le groupe calculé, comme le dessin |
| Bouton d'export avec une formule | la valeur fixe | la formule (source, nom, format, « Demander où ») |
| Un `REAL` qui passe par un objet, une formule, une recette | 6 chiffres (1 234 570), un faux « Écart » | sans perte (1 234 567,8) |
| Numéro de boucle ISA | typé BOOL : `Vue.Objet.Loop` lu FAUX | un texte (`'101A'`) |
| Titre d'un cadre ou d'un panneau, libellé d'un symbole | les accolades affichées | des textes à trous remplis, sans les `$` de leurs repères |
| Conteneur ou groupe caché, transparent, clignotant | ses enfants restaient visibles et cliquables | ses enfants le suivent |
| Date d'une recette | enregistrée `2026-10-09`, relue 2007 | `D#2026-10-09` |
| GIF « sur condition » tapée `=Marche` | ne jouait jamais | joue |
| Compteur de production | seulement vue affichée ; INT qui déborde = remise à zéro ; un compte pour toutes les ouvertures d'une popup | en fond ; le débordement suivi ; un compte par jeu d'arguments |
| Tableau de variables, ligne `$Cuve$.Consigne` | en lecture seule, sans unité ni format | écrivable, avec son unité et son format |
| Consigne d'alarme | les alarmes d'objets « introuvables » | proposées et trouvées |
| Défaut d'un symbole tapé `P1.Defaut` (sans `=`) | l'alarme sonnait, le dessin restait sans défaut | le dessin le montre aussi |
| Courbe XY en « escalier » | tracée en linéaire | en marches |
| Incrémenter une `STRING` | elle se vidait | refusé, et dit |
| Champ de saisie en mode texte sur un nombre | 0 écrit | la valeur convertie, ou refusée et dite |
| `LINT`, `ULINT`, `LWORD` | rangés sur 32 bits | 64 bits |
| « Lignes depuis » un `TUPLE` | refusé par Générer | accepté |
| Dupliquer, valeur d'Affecter, d'Incrémenter, de Calcul | lue en texte | lue en formule |

## 2. Les défauts D1 à D18

**D1 — La date et l'heure.** Le sélecteur lié à une `DATE`, une `TIME_OF_DAY` ou une `DATE_AND_TIME` écrit et relit des millisecondes, à l'heure du mur, sans fuseau. Avant, il écrivait des secondes en heure locale : la date enregistrée tombait en 1970. On rencontre ces types dans une variable de l'automate ou d'un équipement, ou dans un paramètre de popup.

**D2 — Les alarmes au groupe calculé.** Le bandeau d'alarme, la liste (Historique) et le résumé par zone lisent leur groupe, leur mode et leur période **calculés** au clic comme au dessin. « Acquitter » acquitte donc l'alarme montrée, et un clic sur une ligne choisit celle qu'on voit. Le résumé retire les `$` de ses repères partout : dessin, clic, survol et Générer.

**D3 — Le bouton d'export.** Une formule sur la source, le nom du fichier, le format ou « Demander où enregistrer » est lue au clic. Le menu de connexion et le bouton Paramètres système lisent aussi l'onglet calculé.

**D4 — Les REAL sans perte.** Une valeur qui passe par un objet est écrite au plus court, sans perte (`1234567.8`, `0.1`). C'est le cas d'une propriété calculée (Valeur, minimum, maximum, seuils…), du pas d'Incrémenter, d'une recette (Ajouter, Lire) et d'un compteur. Les textes affichés (libellés, titres) gardent leur écriture courte.

**D5 — L'instrument ISA.** Son numéro de boucle est un texte. Une formule texte est acceptée par Générer, et `Vue.Objet.Loop` se lit `'101A'`.

**D6 — Les textes à trous.** Toutes les propriétés qui se montrent comme un texte sont des textes à trous (`Pompe {Nom}`), remplis en marche et vérifiés par Générer :
- le texte, le libellé, le titre, l'infobulle ;
- le texte vide, le texte des états (marche, arrêt), l'invite ;
- le texte inconnu, le texte du bouton, les titres des axes.

Un texte qui porte un repère sans trou (`Mode : $Maintenance$`, ce que Dupliquer écrit) s'affiche sans ses `$`.

**D7 — Conteneur et groupe.** Caché (Visible faux), un conteneur ou un groupe cache ses enfants : ils ne se voient plus et ne se cliquent plus. Transparent ou clignotant, il les entraîne, leur opacité multipliée par la sienne, comme une instance de symbole.

**D8 — Les dates des recettes.** Une `DATE`, une `TOD` ou une `DT` est rangée avec son préfixe (`D#2026-10-09`, `TOD#06:15:00`). Elle se relit et s'applique telle quelle.

**D9 — Le GIF animé.** La condition du départ « sur condition » se tape sans `=` (`Marche`) ou en formule (`=Marche`) : les deux font jouer le GIF. Générer ne dit plus « la condition est vide ».

**D10 — Le compteur de production.** Il compte en fond, que sa vue soit affichée ou non, comme les courbes depuis la 1.12.3. Une popup rouverte ne compte pas son absence. Un `INT` ou un `DINT` qui déborde (32 767 → −32 768) n'est plus pris pour une remise à zéro. Une popup ouverte avec d'autres arguments a son propre compte, que le rapport nomme.

**D11 — Le tableau de variables.** Une ligne repérée (`$Cuve$.Consigne`) reste une variable : on l'écrit d'un clic, et elle garde l'unité et le format de « Unités et formats ». Dans Contenu, retirer ou déplacer une ligne emmène son unité et son format.

**D12 — La consigne d'alarme.** Les alarmes des objets (`Vue.Objet.Alarme`) sont proposées dans sa liste, et Générer les trouve. Le nom se lit sans ses `$`.

**D13 — Le synoptique.** La Valeur, le Défaut ou le Mouvement d'un symbole tapés sans `=` (`P1.Defaut`, `Niveau_1`) sont lus comme des formules : par le dessin, comme le faisait déjà l'alarme de l'objet. Générer les vérifie : `P1.Defot` est signalé. `TRUE`, `0` et `vrai` restent des constantes.

**D14 — La courbe XY.** L'interpolation « escalier » tient chaque valeur jusqu'au point suivant.

**D15 — Les écritures selon le type.**
- Incrémenter ou Décrémenter une `STRING` est refusé : le journal le dit et Générer le signale. Avant, elle se vidait.
- En mode texte, un champ de saisie lié à une variable qui n'est pas un texte convertit ce qui est tapé : `12.5` dans un `REAL`, `vrai` dans un `BOOL`, `T#5s` dans un `TIME`. Un texte illisible est refusé avec un message ; avant, 0 était écrit sans un mot.

**D16 — Les entiers sur 64 bits.** `LINT`, `ULINT` et `LWORD` sont calculés sur 64 bits, dans les scripts, les fonctions et les formules (`5000000000 * 3` vaut 15 000 000 000). Un littéral au-delà de 32 bits est un `LINT`. Les conversions `TO_LINT`, `LINT_TO_STRING`… suivent.

**D17 — « Lignes depuis ».** Un `TUPLE` est accepté par Générer. Un tableau à deux dimensions montre une ligne par premier indice et une colonne par second. Une source repérée (`$Lignes$`) est vérifiée ; avant, Générer la sautait.

**D18 — Dupliquer.** La valeur d'Affecter, d'Incrémenter, de Décrémenter et de Calcul se lit comme une formule, comme le moteur la lit.

## 3. Les défauts propres aux objets

- **Voyant** : un nombre différent de 0 l'allume (2 aussi), comme `TRUE`, `VRAI` ou une `STRING` `'TRUE'`.
- **Barre de progression** : son texte est le pourcentage de sa plage, 500 sur 0..2000 montre 25 %. Avant, elle montrait la valeur suivie de %.
- **Interrupteur, case à cocher, bouton lumineux** liés à une `STRING` : un clic allume (`'TRUE'`), le suivant éteint (`'FALSE'`). Avant, chaque clic écrivait VRAI. L'interrupteur ne montre plus de discordance pour un `INT` de 2.
- **Panneau repliable** : sa variable se lit comme un voyant (2 aussi, `'TRUE'`), et le dessin et le clic sont d'accord. Sur une `STRING`, un clic le déplie.
- **Conteneur à onglets** : la variable suit son type.
  - Une énumération montre et écrit ses valeurs, sans décalage : Auto montre l'onglet 2, et l'onglet 3 écrit Manu.
  - Un `BOOL` : FAUX montre la page 1, VRAI la page 2.
  - Une `STRING` : le nom de l'onglet.
- **Compteur horaire** :
  - « Compte quand » se lit en formule (`=Pompe`) comme en texte (`Pompe`). Avant, la formule était ignorée et le compteur comptait toujours.
  - Le total ne repart plus de zéro. À 90 % de ce que tient son type, le journal le dit ; au maximum, il s'arrête. Un `INT` tient 9,1 h et un `TIME` 49,7 jours.
  - Générer le prévient : un `LREAL` (des heures) ou un `UDINT` (des secondes) tiennent des années.
- **Flèche de tendance** : la fenêtre et la zone morte calculées sont lues.
- **Code QR** : le guide dit les versions 1 à 10, celles que l'application calcule (271 octets en L, 213 en M, 151 en Q, 119 en H). Avant, il promettait la version 40.
- **Horloge** : « Couleur de la trotteuse » (l'inspecteur disait « Couleur du bouton »).
- **Programmateur horaire** : sa sortie repérée (`$Sortie$`) s'écrit.
- **Sélecteur** : la source des éléments ne perd que les `$` de ses repères ; les valeurs écrites suivent un renommage.
- **Guide** : « Clé » s'écrit enfin correctement dans la page du tableau.

## Tester vous-même

1. Posez un **voyant** lié à un `INT`, et forcez-le à 2 en simulation : il s'allume.
2. Posez une **barre de progression** de 0 à 2000, liée à une variable qui vaut 500 : elle montre 25 %.
3. Posez un **interrupteur** lié à une `STRING` : un clic écrit `TRUE`, le suivant `FALSE`.
4. Créez une énumération Arrêt / Auto / Manu et une variable de ce type à Auto. Posez un **conteneur à onglets** à trois onglets lié à cette variable : il montre le deuxième. Un clic sur le troisième écrit Manu.
5. Mettez dans un **cadre** le titre `Pompe {Nom}` : en simulation, il affiche le nom.
6. Posez un **conteneur** et un texte dedans. Mettez sur Visible du conteneur une formule fausse : le texte disparaît avec lui.
7. Posez un **bandeau d'alarme** dont le groupe est la formule `=Zone`, et changez `Zone` : Acquitter vise l'alarme affichée.

## Installer

1. Lancez `XPGAnalyser-Setup-1.12.5.exe`. Il met à jour la version installée : même identité d'installation, données reprises sans être déplacées. Il pose les deux applications et leurs deux raccourcis. Vos projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.12.5.exe` (28,2 Mo, 29 620 246 octets) :
`AA548D135B8BE58B9C43C3A445BDC466EB9E0790926BD8E8945D160C01353C38`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `XPGAnalyser-API-1.12.5` ou `XPGAnalyser-IHM-1.12.5`. Décompressez, puis lancez l'exe.
- `XPGAnalyser-API-1.12.5-portable.zip` (21,1 Mo) :
  `E8A0F01EA5B2F8FA73E0584F610E1AE33BC06F833C36235CFA42F08B5189894A`
- `XPGAnalyser-IHM-1.12.5-portable.zip` (21,1 Mo) :
  `8255982057F41845B3817512CFDD98457FA10CD8502E93C488502D81F70DACAC`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe, 70 sur 70. Elle comprend les 6 841 contrôles de `hmieditor`, les 4 991 de `hmi`, les 1 842 de `hminatives`, les 258 de `hmireperes` et les 133 de `hmitypes`.

Les essais du lot A :
- `lotA1125` dans `hmi` (92 contrôles ; `HMI_TEST_1125=1` le lance seul) couvre chaque défaut D1 à D18 et chaque défaut propre aux objets. Le plus souvent, il le fait au plus près de l'usage : un clic, un Valider, un Générer, une copie évaluée comme celle que l'écran dessine. Plusieurs cas portent un repère (`$Montrer$`, `$Pompe$`, `$Cuve$.Consigne`, `$Sortie$`…) ;
- `lotA1125_contenuTableauVariables` dans `hmieditor` : retirer et déplacer une ligne dans Contenu.

Chaque contrôle dit, dans son libellé, ce que faisait la 1.12.4. **L'échec « avant » n'a pas été rejoué un par un.** Ces essais appellent des fonctions ajoutées par les corrections (`interpolatedPoints`, `progressLabel`, `valueOn`…), donc ils ne se compilent pas contre la 1.12.4. L'audit promettait un essai qui échoue avant sa correction : la promesse est tenue sur le papier, pas rejouée.

Deux essais existants ont changé, parce que le comportement qu'ils figeaient était le défaut :
- `hmitypes` attendait `LINT` rangé en `DINT` ; il attend maintenant les 64 bits ;
- les compteurs des notes de version (38 versions, 278 lignes).

Générer, sur le projet de démonstration Armoire_Gaz, donne les mêmes 3 erreurs qu'avec la 1.12.4 ; ce sont des fautes voulues du projet. Vue_LotA n'en ajoute aucune.

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `XPGAnalyser API 1.12.5` et `XPGAnalyser IHM 1.12.5` ;
- `XpgAnalyzer-API.exe --cli MAST.XPG` analyse le projet d'essai (891 variables, 29 POU), comme avant.

**La session sous Wine** a tourné avec l'exe livré, sur Armoire_Gaz. Un petit outil y a ajouté la vue `Vue_LotA`, ses six variables et l'énumération `T_Mode_LotA`. Chaque ligne de la session a réussi :
- Vue_LotA dans l'éditeur (capture `1125_ihm_01`). On y voit ce qui est tapé : le titre `Pompe {Nom_Pompe} - {Volume:0} L`, le texte `Mode : $Maintenance$` ;
- la simulation : le build réussit (375 éléments générés, 302 compilés, 0 erreur). La vue montre (`02`) :
  - le titre rempli, « Pompe P1 - 500 L », et « Mode : Maintenance » sans ses `$` ;
  - la barre à 25 % (500 sur 0..2000) et le voyant allumé par `Compte = 2` ;
  - l'interrupteur « Arrêt (FALSE) » ;
  - les onglets sur « Automatique » : `Mode_Ligne` vaut Auto, la page 2 ;
  - le conteneur et son texte ;
- un clic sur l'interrupteur écrit `'TRUE'` dans la STRING (`03`), un second `'FALSE'` (`04`) ;
- « Cacher / montrer » : le conteneur disparaît avec son texte (`05`).

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.4 :
- « Installation process succeeded ». Il a repris l'installation de la 1.12.4 : même dossier, et son journal de désinstallation complété ;
- avant comme après : `XpgAnalyzer-API.exe`, `XpgAnalyzer-IHM.exe`, et les raccourcis « XPGAnalyser API » et « XPGAnalyser IHM » ;
- les deux exe installés sont identiques à ceux qui ont été testés. Installés, ils répondent `XPGAnalyser API 1.12.5` et `XPGAnalyser IHM 1.12.5`.

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici). La section « Tester vous-même » donne le parcours à refaire chez vous.

## La suite

Le plan de l'audit, dans l'ordre recommandé :
- **B et C ensemble** : un rôle par propriété, lu par un seul lecteur, et les repères partout. Le moteur lira la même copie évaluée que le dessin, et une formule ne sera plus effacée par un geste. Générer vérifiera le type contre le rôle, et Dupliquer suivra tous les index ;
- puis **D** (les listes en tableau, les choix) ou **E** (les tailles dynamiques : « 8 cuves, 8 pompes »), selon votre usage.
