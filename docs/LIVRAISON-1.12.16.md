# XPGAnalyser 1.12.16 : les constantes et fonctions des vues, « Choisir une fonction… », la valeur initiale en brut ou en code, 39 std::, et les demandes du 19/10

Livrée le 19/10/2026. Elle suit la 1.12.15 du 16/10 (`docs/LIVRAISON-1.12.15.md`).

Elle répond à deux demandes. La première, après la maquette validée (« ok go ») :

> « ajoute les constantes et fonctions sur les VUES/POPUP stp / fait un helper modal pour choisir les fonctions parmis les natives dans tous les fichiers ST, tous les endroits avec aide à la saisie, dans les completions ajouter 'Choisir...' et ca ouvre ca / grandement améliorer l'aide aux valeurs d'inits des constantes, car il faut pouvoir ecrire du code ou en brut, la partie avec du code ca appelrait un callback qui renverait le meme type que la variable modifié, optimisé pour faciliter la compréhension de chaque type spé / proposer d'ajouter des std:: utiles, propose / ajouter une sorte d'exemple et proposer maquette pour tout ca »

La seconde, sur une capture de la fenêtre Dupliquer :

> « dans duppliquer ajouter decalage X et Y pas seulement XY sur une variable / quand on modifie dans un tableau une adresse lié %MW510 par exemple pour surcharger la suite logique 501-502-503-504, il faut pouvoir retirer la surcharge et revenir à la valeur normal / dans Courbe (conenu en manière générale, actions aussi) pouvoir duppliquer une plume, action c'est bien en gardant le meme principe que duppliquer un symbole / sur les courbes faire en sorte de pouvoir faire pause/continuer et avoir une option 'Pilotable' dans Propriétés / pouvoir ajouter une condition dynamique pour afficher/cacher une plume »

Tout se passe dans **XPGAnalyser IHM**. XPGAnalyser API ne change pas, à part son numéro de version.

## 1. Les constantes et les fonctions d'une vue (et d'une popup)

Une vue ou une popup a maintenant ses propres constantes et ses propres fonctions, comme un symbole avait déjà les siennes.

**Où :** ouvrez la vue. Au-dessus du dessin, trois sous-onglets : **Dessin**, **Constantes** et **Fonctions**. Dans l'arbre, la vue les montre sous son nom.

**Les constantes** (la grille des déclarations, comme celle d'un script) :
- un nom, un type, une valeur. La case Valeur ouvre « Valeur initiale… » (voir § 3) ;
- **calculée au démarrage** (d'office) ou **à chaque ouverture** de la vue (la case de la ligne). Une constante d'une popup qui lit un des paramètres de la popup se calcule d'office à chaque ouverture ;
- la portée : **publique** (d'office) ou **privée**.

**Les fonctions** : un nom, un type de retour, des paramètres, un code qui rend sa valeur par `RETURN`. Portée publique (d'office) ou privée.

**Les employer :**
- dans la vue, **par leur nom** : une fx `=Pourcent(Temperature)`, un texte à trous `{Seuil_Haut:0} °C`, un script, une action `Consigne := Borne(Consigne + 10.0)`, une autre fonction ;
- ailleurs, **`Vue_Four.Seuil_Haut`** et **`Vue_Four.Pourcent(...)`** - les publiques seulement ;
- **l'aide à la saisie** les propose : par leur nom dans la vue (les privées aussi), après `Vue_Four.` ailleurs ; la bulle de signature de `Pourcent(` et l'infobulle disent leur type et d'où elles viennent ;
- **Générer** refuse, chacun à sa ligne : une privée lue d'ailleurs, une constante écrite (`Seuil_Haut := 2.0`), un nom déjà pris dans la vue (un objet, un paramètre), une constante du démarrage qui lit une constante de l'ouverture.

Le fichier du projet passe au format 26 quand une vue a des constantes ou des fonctions. Une 1.12.15 refuse ce fichier, plutôt que de les perdre. Un projet sans elles s'écrit comme avant.

## 2. « Choisir une fonction… »

Chaque liste de l'aide à la saisie (les éditeurs de code, les cases fx) finit par la ligne **Choisir une fonction…**. Elle ouvre un sélecteur des fonctions :
- il part du **type attendu** au curseur : après `Total := ` (Total, une variable REAL), il montre d'abord ce qui rend un REAL ; dans l'argument d'une fonction, le type de ce paramètre ;
- il range par famille les natives (avec leur fiche), les fonctions de la vue éditée et celles du projet ;
- l'appel choisi s'insère à la place du mot commencé, le curseur sur le premier argument.

**Raccourcis :** **Ctrl+I** ouvre le sélecteur directement. **Ctrl+Espace** montre la liste de l'aide à la saisie. Ctrl+J reste le panneau du bas, et Ctrl+Maj+Espace l'aide des paramètres.

## 3. La valeur initiale, en brut ou en code

« Éléments… » et « ƒx Calcul… » sont remplacés par une seule fenêtre, **Valeur initiale…**, pour une constante (d'un script, d'une vue, d'une popup) et une variable. Elle a deux façons d'écrire.

**En brut**, un éditeur fait pour le type :
- une ligne par entrée d'une MAP, par élément d'une liste ou d'un tableau ;
- une fiche par élément structure, un membre par ligne ;
- les valeurs d'une énumération ; une durée en heures, minutes, secondes et millisecondes ; un tuple ;
- la case vide montre la valeur du type en gris. Le littéral enregistré et ses fautes se lisent en direct ; une faute grise Appliquer ;
- des aides : Ranger par clé, Tout à 0, Suite, les doublons d'un SET, ce qui partira d'un RING.

**En code** : l'en-tête `FUNCTION Init_<Nom> : <type>` est fixe, et le code **rend la valeur** par `RETURN`. Le style de la 1.12.7 (`Nom := …`) tourne tel quel.
- un mauvais type rendu est refusé, à sa ligne ;
- des exemples faits du type ; « Passer en code » part de la valeur brute ;
- les Locales, l'aperçu, et « calculée au démarrage / à chaque ouverture ».

**À droite**, le type est expliqué : son arbre, comment le lire, son équivalent C++, les natives qui vont avec.

## 4. 39 std:: recommandées

| Famille | Natives |
| --- | --- |
| Maths | CEIL, FLOOR, FMOD, HYPOT, ATAN2, LERP, ISNAN, ISFINITE |
| Textes | STARTS_WITH, ENDS_WITH, CONTAINS, TRIM, TRIM_LEFT, TRIM_RIGHT, FORMAT |
| Dates | YEAR, MONTH, DAY, HOUR, MINUTE, SECOND, WEEKDAY, ADD_DAYS, DAYS_BETWEEN, FORMAT_DT |
| Listes | LIST_LOWER_BOUND, LIST_BINARY_SEARCH, LIST_TRANSFORM, LIST_MEDIAN, LIST_VARIANCE, LIST_STDDEV, INTERPOLER_TABLE |
| Ensembles | SET_SYMMETRIC_DIFFERENCE, SET_INCLUDES |
| Hasard | RANDOM_NORMAL |
| Bits | BYTESWAP, WORDSWAP |
| Échanges | SWAP, EXCHANGE |

Chacune a sa fiche, ses équivalents C et C++, et un exemple joué par le moteur. Générer et Compiler les connaissent, les fx aussi.

## 5. L'exemple

- **L'exemple animé** : la page d'aide « Les constantes et fonctions d'une vue » (F1 dans les sous-onglets Constantes et Fonctions) montre en haut une vue qui tourne. Seuil_Haut et Tiede s'écrivent, Rampe se calcule au démarrage. Pourcent() et Couleur() (privée) remplissent et colorent la barre. Le bouton « +15 °C » appelle Borne().
- **Le modèle de vue « Constantes et fonctions »** (Nouvelle vue › Modèle) en pose un complet :
  - la vue du four : trois constantes, trois fonctions, une barre, deux textes, -10 / +10 et Régler… ;
  - sa popup de réglage `Pop_Reglage` : son paramètre Consigne, Pas (écrite), Maxi (lit `Vue_Four.Seuil_Haut`, une constante publique d'une autre vue), Depart (lit le paramètre, donc calculée à chaque ouverture), Monter() et Descendre() ; Annuler remet la consigne d'à l'ouverture ;
  - les variables `Four_Temperature` et `Four_Consigne`, créées si elles manquent.

## 6. Dupliquer : X et Y séparés ; « Dupliquer… » d'une plume, d'une ligne, d'une action

**Dupliquer un objet** (Ctrl+D), onglet La disposition : **Espacement X** et **Espacement Y**, chacun le sien.
- Sur X lit l'espacement X, Sur Y l'espacement Y, la grille les deux (entre deux colonnes, entre deux lignes) ;
- « Ajuster l'espacement » règle chacun au plus grand qui fait tout tenir ;
- dans un script de capture : `dupliquer-pose grille 3 30 60` (colonnes, X, Y).

**Dupliquer…** fait maintenant aussi N copies d'**un élément**, avec le même principe que pour un symbole :
- dans **Contenu** : une plume d'une courbe (aussi une barre, une part, une ligne d'un tableau de variables), une ligne d'un tableau à cases fixes, un choix, un état ;
- dans **Actions** : une action, un raccourci.

La fenêtre :
- les indices suivis : `Cuves[0].Niveau` donne `Cuves[1].Niveau`, `Cuves[2].Niveau`… Chaque indice a sa case **Suivre**, décochée d'office quand les copies sortiraient de ses bornes connues (un avertissement le dit) ;
- le nom numéroté : Plume 1 → Plume 2, `Mode 1` → `Mode 2`, la touche F2 → F3 ;
- la couleur prise dans la palette ;
- un tableau des copies : l'original en gris, une ligne par copie. Chaque case se corrige, et une case corrigée le reste quand on change le nombre de copies.

« Dupliquer (N copies) » les insère après l'original, en une commande : Ctrl+Z retire tout.

## 7. Revenir à l'adresse calculée

Dans **Variables IHM**, un membre dont l'adresse est corrigée à la main (`%MW510` au milieu d'une suite `%MW501`, `%MW502`…) peut reprendre sa place :
- sa case **Adresse** porte un bouton de retour, qui dit l'adresse de la suite logique ;
- le **clic droit** propose « Revenir à l'adresse calculée » pour les membres choisis, ou « Retirer les adresses corrigées » pour tous ;
- c'est une commande : Ctrl+Z rend la correction.

## 8. Les courbes : Pilotable, Visible si

- **Pilotable** (Propriétés de la courbe) : en marche (simulation, poste d'exploitation), un bouton en haut à droite de la courbe. **Pause** fige l'affichage pour lire un instant précis, et l'axe du temps dit « pause ». Les mesures continuent d'être prises. **Continuer** reprend le temps réel. Le journal dit les deux.
- **Visible si** (Contenu › la plume) : une condition lue en marche. Fausse, la plume et sa légende se cachent ; ses mesures continuent. Une plume à joker (`Cuves[*].Niveau`) prend `Cuves[*].Actif`, une condition par cuve. Générer vérifie chaque condition.

## Tester vous-même

1. Créez une vue avec le modèle **Constantes et fonctions**. Ouvrez ses sous-onglets Constantes et Fonctions, puis Simulation : la barre suit la température, +10 et -10 bornent la consigne, Régler… ouvre la popup (+ Pas s'arrête à Maxi, Annuler revient à la consigne d'ouverture).
2. Dans un script, tapez `Total := ` (Total, une variable REAL) puis **Ctrl+I** : le sélecteur montre d'abord ce qui rend un REAL.
3. Sur une constante MAP, la case Valeur ouvre « Valeur initiale… » : une ligne par clé en brut ; « Passer en code » donne la fonction `Init_<Nom>` qui la rend.
4. Une courbe à deux plumes : Contenu › la plume 2 › **Visible si** `Debit > 5` ; Propriétés › **Pilotable** ; en simulation, le bouton Pause fige, Continuer reprend.
5. Contenu › une plume `Cuves[0].Niveau` › **Dupliquer…** › 3 copies : `Cuves[1]` à `Cuves[3]`, leurs noms et leurs couleurs.
6. Variables IHM : corrigez l'adresse d'un élément de tableau en `%MW510`, puis le bouton de retour de sa case Adresse : il reprend sa place.

## Installer

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.16.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.16-portable.zip` et `XPGAnalyser-IHM-1.12.16-portable.zip` : chaque application, sans installation ;
- ce document.

1. Lancez `XPGAnalyser-Setup-1.12.16.exe`. Il met à jour la version installée, la 1.12.15 ou une plus ancienne : même identité d'installation, données reprises sans être déplacées. Il pose les deux applications et leurs deux raccourcis. Vos projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.12.16.exe` (30,3 Mo, 31 742 366 octets) :
`D1177194C24D02BE7099EB71707A669DCDD4BFF452E525ED4AD44E75E5FAD32D`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `XPGAnalyser-API-1.12.16` ou `XPGAnalyser-IHM-1.12.16`. Décompressez, puis lancez l'exe.
- `XPGAnalyser-API-1.12.16-portable.zip` (22,6 Mo) :
  `75398245DAE44C8A9C24736D5A816BE027AAA55BC7BE9DE3E605F5C7B5485DC8`
- `XPGAnalyser-IHM-1.12.16-portable.zip` (22,6 Mo) :
  `B97C13C0DA7DFA9562BA3CD27DC734FB4622806E06D319336BF5B970A2B46F3A`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe, 75 sur 75 (une de plus qu'à la 1.12.15 : `hminatives1216`). Elle comprend :
- 7 638 contrôles dans `hmieditor` (7 363 à la 1.12.15), 5 706 dans `hmi` (5 656), 1 025 dans `hmidecl`, 380 dans `hminatives1216` et 157 dans `hmitypes` ;
- ceux d'avant, qui passent toujours.

Les nouveaux essais couvrent :
- **les constantes et fonctions d'une vue** :
  - le moteur : par leur nom dans la vue, par `Vue_Four.Seuil_Haut` et `Vue_Four.Pourcent(...)` ailleurs ; une constante calculée au démarrage, une autre à l'ouverture d'une popup (elle lit son paramètre) ;
  - Générer refuse une privée lue d'ailleurs, une constante écrite (dans la vue ou d'ailleurs), un nom déjà pris par un objet, une constante du démarrage qui lit une de l'ouverture, et dit un membre inconnu ;
  - un projet sans elles garde le format d'avant : une 1.12.15 l'ouvre ;
- **les sous-onglets** : la grille des constantes ; renommer `Seuil_Haut` en `Seuil_Max` ou `Pourcent` en `Ratio` renomme leurs emplois ; un nom déjà pris est refusé ; un symbole garde ses six sous-onglets ;
- **l'aide à la saisie** : dans la vue, les constantes et fonctions par leur nom ; ailleurs, seulement après `Vue_Four.` ; la bulle de `Vue_Four.Pourcent(` ;
- **Choisir une fonction…** :
  - le type attendu : après `Pourcent(1.0, `, un REAL ; après `LEFT('abc', `, un INT ; rien d'attendu après `LIMIT(` ;
  - la recherche (`LIMI` met LIMIT en tête), la famille du projet, l'appel inséré à la place du mot commencé ;
  - Ctrl+Espace, Ctrl+I, rien dans un commentaire ; la dernière ligne dans une case fx et dans une case de grille ;
- **la valeur initiale** :
  - en brut : une MAP (une valeur qui n'est pas du type est une faute, Ranger par clé), une liste de structures (une fiche par élément, un membre faux), une structure, une énumération, une durée, un tuple, les doublons d'un SET, ce qui partira d'un RING ;
  - en code : `RETURN`, `Init_Nom :=` et le style de la 1.12.7 ; un mauvais type rendu est refusé à sa ligne ;
- **les 39 std::** : chacune a sa fiche et son exemple ; le moteur les calcule (`RANDOM_NORMAL(10, 2)` sur 20 000 tirages) ; Compiler dit un mauvais nombre d'arguments, une constante passée à EXCHANGE, une fonction de LIST_TRANSFORM mal écrite (« veux-tu dire VersKelvin ? ») ; dans une fx, SWAP n'écrit rien ;
- **Dupliquer** : 16 px en X et 30 en Y dans une grille ; sans espacement Y, celui de X (les anciens plans) ; Ajuster sur Y règle l'espacement Y seul ;
- **Dupliquer… d'un élément** : une plume (`Cuves[0]`, bornes 0..7, et `Actifs[0]`, bornes inconnues, suivis d'office ; 9 copies : `Cuves[9]` sort des bornes, c'est dit), une case corrigée qui le reste, la ligne d'un tableau à cases fixes, un choix (Mode 1 → Mode 2), un état (le « sinon » ne se duplique pas et reste le dernier), une action, un raccourci (Ctrl+F5 → Ctrl+F6, Ctrl+F7) ; Ctrl+Z retire tout ;
- **l'adresse calculée** : celle de `V[2]` sans la correction, « Revenir à l'adresse calculée », tout retirer en une commande, puis plus rien à retirer ;
- **les courbes** :
  - Pilotable est décochée sur une courbe neuve ; sans elle, ni bouton ni pause ;
  - le bouton fige la courbe à l'instant de la pause (le journal le dit) et les mesures continuent ; Continuer reprend le temps réel ; un nouveau lancement repart sans pause ;
  - `Debit > 5` faux cache la plume, qui se mesure quand même ; `Debit = 7` la montre ;
  - un joker donne une condition par cuve (`Cuves[1].Actif`, `Cuves[2].Actif`) ;
  - Générer dit, à sa plume, une condition qui lit une variable inconnue ;
- **l'exemple et le modèle** : l'exemple animé se compile et tourne sans faute (+15 °C : `Borne(45 + 15)` = 60, puis 75) ; le modèle « Constantes et fonctions » se génère et se compile sans erreur ; en marche, +10 donne 70, « + Pas » 75, la consigne s'arrête à Maxi = 100, Annuler revient à 70 ; un second modèle dans le même projet reprend les variables et nomme autrement sa popup.

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `XPGAnalyser API 1.12.16` et `XPGAnalyser IHM 1.12.16` ;
- ils importent exactement les mêmes DLL que ceux de la 1.12.15 (ADVAPI32, IPHLPAPI, KERNEL32, SDL3, SHELL32, USER32, WS2_32, msvcrt, ole32).

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.15 :
- avant, la 1.12.15 livrée le 16/10 était installée (`XPGAnalyser IHM 1.12.15`) ;
- « Installation process succeeded ». Il a repris l'installation en place : même dossier, les deux raccourcis et le dossier Maintenance du menu Démarrer gardés, et son journal de désinstallation complété ;
- les deux exe installés sont identiques à ceux qui ont été testés. Installés, ils répondent `XPGAnalyser API 1.12.16` et `XPGAnalyser IHM 1.12.16`, et l'API installée analyse `MAST.XPG` comme avant (891 variables, 29 POU).

**Les zips portables** ont chacun 151 fichiers, dont l'exe testé et le manifeste de la 1.12.16.

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici). Pas de session Wine avec captures pour cette version : la section « Tester vous-même » donne le parcours.

## Limites connues

- **Une constante à chaque ouverture** se calcule quand la vue s'ouvre ; une vue déjà ouverte ne la recalcule pas si ce qu'elle lit change ensuite.
- **Dupliquer…** d'un élément copie un élément à la fois (pas une sélection de plusieurs plumes). Les zones, les onglets, les vues d'une barre de navigation, les colonnes dynamiques et les sommets n'ont pas encore « Dupliquer… ».
- **Pause** fige l'affichage de la courbe de la vue ouverte ; la reprise après une nouvelle ouverture de la vue repart en temps réel.
- **Visible si** se lit sur une courbe ; les graphiques (barres, parts, radar, XY, chronogramme) n'ont pas encore la condition par élément.

## La suite

- Essayer sur un vrai Windows, avec la section « Tester vous-même ».
