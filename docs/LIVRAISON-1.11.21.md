# XPGAnalyser 1.11.21 : les éditeurs sans bandeaux, les explorateurs dépliés

Livrée le 09/10/2026 dans l'après-midi. Elle répond à vos demandes de ce matin :
- « supprimer les bandeaux essais et diagnostics et intégrer les erreurs détectées si nécessaires et utiles dans le bandeau bas de compilation » ;
- « refaire le déballage dans les explorateurs d'appli et de vue et d'objets, mettre les signatures, variables, fonctions internes » ;
- et à deux points restés ouverts : le contrôle des types de tous les appels (la limite de la 1.11.20), et la valeur initiale gardée quand le type d'une déclaration change (vu dans la session de la 1.11.19).

**Un projet de la 1.11.20 s'ouvre tel quel** : le format ne change pas.

Fichiers livrés :
- `XPGAnalyser-Setup-1.11.21.exe` (l'installateur) ;
- `XPGAnalyser-1.11.21-portable.zip` (le même programme, sans installation) ;
- ce document ;
- les captures `11121_*`, prises sous Wine avec l'exe livré.

## 1. Ce qui change pour vous

- **Plus de bandeaux Diagnostics et Essai dans les éditeurs.** L'éditeur d'un script, d'une fonction (IHM ou d'un symbole) ou d'opérateurs prend toute la hauteur.
  - Les fautes restent **soulignées** dans le code, leur ligne marquée, leur message en infobulle.
  - Elles vont **en direct au panneau du bas**, onglet **Diagnostics**, en tête, à l'étape **Saisie**. Un **double-clic** y ramène le curseur.
  - La barre du volet les compte : « 1 erreur(s), soulignée(s) pendant que tu tapes — la liste : panneau du bas, Diagnostics (clic ici) ». Un clic sur elle ouvre le panneau.
  - Le résultat d'**Essayer** va aux **Sorties** du panneau du bas (catégorie Essai), qui s'ouvre. Sa dernière ligne dit ce qui a été lu de l'automate : la simulation de l'API et son état, par exemple « arrêtée : ses valeurs initiales ». L'ancien tableau ESSAI disait « en marche » dès que la simulation était chargée, même arrêtée.
- **Les explorateurs déplient les codes.**
  - Dans l'arbre, une **fonction** montre ses **Paramètres** (avec leur mode : « Graine : REAL  (E/S) », « Tirage : REAL  (sortie) »), ses **Variables locales** et ses **Constantes**.
  - Un **script** montre ses **Constantes**, ses **Variables** (« (conservée) » si elle l'est) et ses **Fonctions internes** : leur signature, puis leurs paramètres et leurs locales.
  - Les **surcharges** sont regroupées sous leur nom : « Convertir · 3 surcharges », chacune avec sa signature. De même pour les fonctions d'un symbole.
  - Un **clic** ouvre le code : sur un groupe, son onglet ; sur une déclaration, sa ligne dans son onglet ; sur une fonction interne, ou sur l'un de ses paramètres et de ses locales, la ligne de la fonction dans le code.
  - L'**explorateur d'objets** d'une vue montre les **Fonctions** d'une instance de symbole, avec leur signature (« redéfinie ici » si l'instance la redéfinit).
- **Les types des arguments de tous les appels sont contrôlés.** Quand une seule surcharge a le bon nombre d'arguments, elle était prise sans regarder les types : `Convertir(Graine, 2)` passait. Maintenant :
  - une **E/S d'un autre type** est une **faute**, car la simulation refuserait l'appel ;
  - une autre **conversion interdite** est un **avertissement**, car la simulation la fait sans rien dire : 'a' passé pour un REAL donne 0. L'avertissement ne bloque la simulation d'aucun projet existant.
- **La valeur d'une déclaration suit son type.**
  - Changer le type retire la valeur initiale qui ne lui convient plus : `0` pour un `ARRAY[1..4] OF T_Four`, `FALSE` pour un INT, `'abc'` pour un REAL. La barre le dit, et un seul Ctrl+Z rend le tout.
  - Une **constante** prend la valeur nulle de son nouveau type : 0, 0.0, FALSE, '', T#0s.
  - Une valeur qui ne convient pas au type est **refusée**, avec sa raison : un texte pour un INT, 40000 pour un INT, une valeur pour une structure, un nom qui n'est pas dans l'énumération.

## 2. Les diagnostics en direct, au panneau du bas

- **D'où ils viennent.** Ce sont les contrôles que l'éditeur faisait déjà pendant la frappe, les mêmes que Compiler : syntaxe, noms, membres, appels, écritures interdites, types. Seul l'endroit où ils s'affichent change.
- **Comment le panneau les reçoit** (`src/app/hmi/HmiLive.hpp`).
  - Chaque volet de code se présente comme une « source en direct » : la clé de build du document montré (`script:12`, `fonction:7`…), ses diagnostics et un compteur de révisions.
  - L'écran lit, à chaque image, le volet de code montré sur l'onglet courant. Si le document ou ses fautes ont changé, le panneau reprend la liste.
- **Ce que montre l'onglet Diagnostics.**
  - Le direct d'abord, à l'étape **Saisie**, puis le dernier build.
  - Les diagnostics du build à l'étape **Compilation** pour le même document sont cachés, car la saisie les recalcule. Ses autres étapes restent.
  - Un autre document remplace le direct. Un onglet sans code le vide.
  - La barre du panneau le résume : « Saisie : 1 erreur, 0 avertissement dans le document montré · … ».
- **Un double-clic sur une ligne en direct** ramène le volet à la faute : son curseur dessus, ou la déclaration dans son onglet. Si le document n'est plus montré, la ligne s'ouvre comme un diagnostic de build.
- **Compiler (F7)** dans l'éditeur ne change pas : le build du document montré. Son résultat va aux Diagnostics du panneau, qui s'ouvre en cas d'erreur.
- **« Ajouter les valeurs manquantes »** (d'un CASE sur une énumération) reste à l'outil du volet. Il corrige la faute de la ligne du curseur, sinon la première qui en a une. Le panneau la signale dans sa colonne Suggestion.
- **Le dialogue d'une action Script** garde sa boîte de fautes : c'est une fenêtre modale, et le panneau du bas n'y est pas accessible.

## 3. Les explorateurs dépliés

- **La règle unique** (`hmitree::outlineOf`) lit le code **comme le moteur le lit** : le modèle de ses onglets, puis les blocs restés dans son texte. Elle donne les groupes, dans l'ordre :
  - **Paramètres** (une fonction ; avec leur mode) ;
  - **Constantes** ;
  - **Variables** (les **Variables locales** d'une fonction) ;
  - **Fonctions internes**.

  Un groupe vide n'apparaît pas. Un script C ou C++ ne se déplie pas.
- **L'arbre de l'application** (`ProjectTreeModel`).
  - Il déplie les scripts généraux et de vue, les fonctions IHM et les fonctions des symboles.
  - Le contenu de chaque code est gardé, puis oublié au premier changement du document : il n'est pas recalculé à chaque dessin.
  - Les **surcharges** sont regroupées sous un nœud par nom, dans l'ordre de la première, aussi bien dans le dossier Fonctions IHM que dans la partie Fonctions d'un symbole. Une fonction seule reste un nœud direct, comme avant.
- **La vue.** Il n'y a pas d'explorateur de vue séparé : c'est la branche IHM › Vues › la vue de l'arbre. Ses scripts et, pour un symbole, ses fonctions s'y déplient.
- **L'explorateur d'objets** d'une vue montre, sous une instance de symbole, une famille **Fonctions (n)** après ses Opérateurs : la signature de chaque fonction, « redéfinie ici » ou « virtuelle ».

## 4. Les types des arguments, la valeur d'une déclaration

- **Les types des arguments** (`hmi::overload::typeMisfits`, appelé par le contrôle des appels).
  - Une fois la surcharge choisie, chaque argument dont la conversion est interdite est signalé.
  - Une **E/S d'un autre type** est une faute (ni le même type, ni un nombre pour un nombre). Le reste est un avertissement.
  - Un type inconnu ne déclenche rien, par exemple une variable de l'automate non branché.
  - Dans une expression de vue, un avertissement d'appel reste un avertissement. En 1.11.20, il devenait une erreur bloquante : c'était le cas des appels « incertains », et c'est corrigé.
- **La valeur d'une déclaration** (`hmi::decledit::valueMisfit`).
  - Une **structure**, ou un tableau de structures, ne prend pas de valeur : elle a les valeurs initiales de son type.
  - Une **énumération** prend l'une de ses valeurs.
  - Un **littéral** est jugé par sa conversion vers le type, et un entier par ses bornes. Les littéraux reconnus : TRUE, 5, 16#FF, 2.5, 'texte', T#5s, INT#3.
  - Un **nom** ou une **expression** n'est pas jugé.
  - La règle vaut au changement de type (la valeur est retirée) et à la saisie d'une valeur (elle est refusée).

**Corrigé au passage dans le document de la 1.11.20** : son tableau des coûts disait que la simulation **tronque** un réel vers un entier. Elle **arrondit** à l'entier le plus proche : 2.5 donne 3, -2.7 donne -3. Le document de `docs/` est corrigé.

## 5. Le parcours vérifié sous Wine

Une session jouée avec l'exe livré (`tools/sessions/session-11121-editeurs-explorateurs.txt`), sur une copie d'Armoire_Gaz sans cache, celle de la 1.11.17. Avant la session, `preparer-projet-11120` y ajoute Random, les trois Convertir et le script Essai_Signatures, comme pour la 1.11.20. Puis `tools/sessions/preparer-projet-11121.cpp` ajoute :
- le script **Essai_Explorateur** : une constante `Seuil_Haut` (REAL, 80.0, documentée), deux variables (`Nb_Appels`, INT, conservée ; `Ecart_Max`, REAL) et une fonction interne `Ecart(a, b)` avec sa locale `d` ;
- la variable IHM **Ecart_Releve**, qu'il écrit.

Les commandes de contrôle de la session (`ihm-diagnostics`, `ihm-sorties-contient`, `volet-message`) échouent si le résultat n'est pas celui attendu ; il n'y a eu aucun échec.

| Étape | Ce qui s'est passé | Capture |
|---|---|---|
| **1.** L'arbre : la branche API repliée ; IHM › Fonctions › **Random** › **Paramètres** dépliés, puis un clic sur Paramètres | Sous Random : « Paramètres (4) » › `Min : REAL`, `Max : REAL`, `RandomSeed : REAL  (E/S)`, `test : REAL  (sortie)`. En dessous, un seul nœud « Convertir · 3 surcharges », et les trois signatures. Le clic a ouvert Random sur son onglet **Paramètres 4** | `11121_01` |
| **2.** L'arbre : Scripts › **Essai_Explorateur** déplié (Constantes, Variables, Fonctions internes, Ecart), puis un clic sur `d`, la locale d'Ecart | « Constantes (1) » › `Seuil_Haut : REAL = 80.0   // le seuil d'alarme (bar)` ; « Variables (2) » › `Nb_Appels : INT := 0  (conservée)`, `Ecart_Max : REAL` ; « Fonctions internes (1) » › `Ecart(a : REAL, b : REAL) : REAL` › `a`, `b`, `d`. Le clic a ouvert le script : son éditeur prend toute la hauteur, sans bandeau. La carte du nœud, au survol, donne son chemin | `11121_02` |
| **3.** Une ligne tapée à la fin du script : `Texte_Entier := Convertir(Graine, 2);` | Soulignée en orange : un avertissement. La barre du volet : « 1 avertissement(s), souligné(s) — la liste : panneau du bas, Diagnostics (clic ici) ». Le panneau du bas, onglet **Diagnostics (1)** : à l'étape **Saisie**, « Convertir(STRING, INT) : texte attend un STRING, pas un REAL (une conversion interdite, que la simulation fait sans rien dire) ». Sa barre : « Saisie : 0 erreur, 1 avertissement dans le document montré ». Ctrl+Z retire la ligne, et le direct se vide (0 diagnostic) | `11121_03` |
| **4.** Fonctions › Random, **Essayer** avec Min 0.0, Max 10.0, RandomSeed 1.0 | Le panneau du bas s'ouvre sur les **Sorties**, catégorie Essai : « Essai de Random(Min := 0.0, Max := 10.0, RandomSeed := 1.0) = 5.0 (REAL) », « Paramètre rendu : RandomSeed (E/S) = 1.75 », « Paramètre rendu : test (sortie) = 5.0 », « Automate : lu dans la simulation de l'API (arrêtée : ses valeurs initiales, sans y écrire) ». La barre du volet reprend le résultat. Le volet n'a plus de tableau ESSAI | `11121_04` |
| **5.** Essai_Explorateur, onglet **Variables** : le type de `Nb_Appels` passé de INT à **STRING** | La valeur 0 est retirée, et la barre le dit : « Nb_Appels : valeur 0 retirée : un INT (0) ne va pas dans un STRING (Ctrl+Z la rend) ». L'arbre suit : « Nb_Appels : STRING  (conservée) » | `11121_05` |

**Ce que la préparation de la session a montré.**
- La première exécution a échoué à l'étape 5 : la variable s'appelait `Compteur`, et la commande `cellule` de la session (elle cherche le nom dans la première colonne de chaque tableau de la page) a trouvé avant elle la variable IHM `Compteur_Clics`, dans le tableau VARIABLES IHM, où son type ne se change pas. La variable du script s'appelle maintenant `Nb_Appels`. Le programme n'était pas en cause.
- La capture 4 a montré une ligne fausse, présente depuis l'ancien tableau ESSAI : « Automate : lu dans la simulation en marche », alors que la simulation de l'API était arrêtée. L'essai lit la simulation dès qu'elle est chargée, même arrêtée. La ligne dit maintenant son état (§ 1), et la session a été rejouée avec l'exe corrigé.

## 6. Fichiers créés et modifiés

- **créés** :
  - `src/app/hmi/HmiLive.hpp` : la source en direct d'un volet de code ;
  - `tools/sessions/preparer-projet-11121.cpp` et `tools/sessions/session-11121-editeurs-explorateurs.txt` : la session Wine.
- **modifiés** :
  - les volets : `HmiScriptPanes`, `HmiFunctionPanes`, `HmiOperatorPanes` (sans bandeaux, leurs diagnostics en direct, la barre, l'essai aux Sorties et l'état de la simulation qu'il lit) ;
  - le panneau du bas : `HmiBuildPanes` (le direct, ce qu'il cache, sa barre, son double-clic) ;
  - l'écran : `HmiBuildWorkspace`, `HmiWorkspace`, `Screens.hpp` (la lecture à chaque image, l'ouverture d'un nœud de code, l'essai aux Sorties) ;
  - l'arbre : `ViewModels` (cinq genres de nœuds, le contenu des codes, les surcharges regroupées) et `HmiTreeData.hpp` (`outlineOf`) ;
  - l'explorateur d'objets : `HmiPanels` (les fonctions d'une instance) ;
  - le contrôle : `HmiOverload` (`typeMisfits`), `HmiCallCheck`, `HmiExprCheck` (la gravité d'un problème), `HmiCheck` (un avertissement d'appel reste un avertissement), `HmiExprBench` et `HmiValueKind` ;
  - les déclarations : `HmiDeclEdit` (`valueMisfit`, la valeur retirée ou refusée) et `HmiDeclGrid` (la barre le dit) ;
  - les sessions : `ScriptRunner` (`ihm-diagnostics`, `ihm-sorties-contient`) ;
  - le tutoriel « script-rampe » (la source `.tuto` régénérée : les résultats sont au panneau du bas), le guide (les éditeurs, Essayer, l'arbre), les notes (4), la version 1.11.21, le projet Visual Studio ;
  - les essais : `hmi_overload_test`, `hmi_decl_test`, `hmi_editor_test`, `hmi_test`.

## 7. Les tests ajoutés

- **`hmioverload`**, *les types des arguments* (14 contrôles) :
  - `typeMisfits` : un STRING pour un INT, un littéral pour un STRING, une E/S STRING pour un REAL (une faute), un nombre pour un nombre (permis), un type inconnu (rien) ;
  - le contrôle d'un code : `Convertir(Graine, 2)` en avertissement à sa colonne, l'E/S d'un autre type en faute, les bons appels sans rien ;
  - une expression de vue : l'appel dit en avertissement.
- **`hmidecl`**, *la valeur suit le type* (15 contrôles) : BOOL → INT (FALSE retiré), 40000 refusé pour un INT, INT → DINT → REAL (7 gardé), REAL → structure (retiré), une valeur refusée pour une structure et pour un tableau de structures, STRING → REAL ('abc' retiré), une expression gardée, 16#FF, une constante (la valeur nulle de son type), une énumération.
- **`hmieditor`** :
  - *direct1121* (8 contrôles) : le panneau du bas, le direct en tête, ce qu'il cache et ce qu'il garde, sa barre, le double-clic, le retour du build ;
  - *explorateurs1121* (16 contrôles) : les surcharges regroupées, Random déplié (ses paramètres et leur mode), le script déplié (constante, variable, fonction interne), la cible d'un clic, le contenu qui suit un changement, la partie Fonctions d'un symbole, l'explorateur d'objets ;
  - *valeurGrille1121* (5 contrôles) : dans la grille Variables d'un script, INT → STRING retire la valeur 0 et la barre le dit ; un seul Ctrl+Z rend le type et la valeur ; `'abc'` est refusé pour un INT, avec sa raison ; 7 est pris.
- **Les essais existants mis à jour**, sans perdre ce qu'ils contrôlent : les tableaux des bandeaux remplacés par les diagnostics en direct, par la barre du volet et par les lignes de l'essai ; les notes (29 versions) ; le guide (le sujet Fonctions change en 1.11.21).

## 8. Les limites

- **Rien n'a été lancé sur un vrai Windows** : tout a été vérifié sous Wine.
- **Le direct suit l'onglet du centre.** Un onglet détaché dans sa propre fenêtre n'alimente pas le panneau du bas. Ses fautes restent soulignées, et Compiler (F7) les y met.
- **Les déclarations d'une redéfinition** de fonction dans une instance ne se déplient pas dans l'arbre : le nœud de l'instance mène à son inspecteur, comme avant.
- **L'arbre ne montre pas de groupe Diagnostics** sous un code. Les pastilles de build disent déjà un élément en faute, et le panneau du bas les liste.
- **La source du guide Word** (`tools/guide-ihm/guide-ihm.txt`) n'a pas suivi les versions depuis la 1.11.2 : l'aide F1 (le C++) a été modifiée directement, de version en version. Cette version suit la même pratique. Le guide Word devra être resynchronisé avant d'être régénéré.
- **L'essai `crashtrail`** (un blocage simulé, mesuré au chronomètre) a échoué une fois sous la charge des essais en parallèle, puis a passé trois fois sur trois, seul. Il ne touche rien de cette version.

## Tester vous-même

1. Ouvrez une fonction et tapez une faute : elle est soulignée, et le panneau du bas (Diagnostics) la liste en tête, à l'étape Saisie. Double-cliquez dessus : le curseur y va.
2. **Essayer** une fonction : le résultat est aux Sorties du panneau du bas.
3. Dans l'arbre, dépliez une fonction : ses Paramètres (avec leur mode), ses Variables locales. Dépliez un script qui a une `FUNCTION … END_FUNCTION` : sa signature, ses paramètres. Deux fonctions du même nom : un seul nœud « · 2 surcharges ».
4. Dans un script, avec `Convertir(valeur : INT)` et `Convertir(texte : STRING; base : INT)`, tapez `Convertir(1.5, 2)` : un avertissement (un REAL pour un STRING).
5. Dans l'onglet Variables d'un script, passez une variable INT de valeur 0 en STRING : la valeur est retirée, la barre le dit ; Ctrl+Z la rend.

## Installer

1. Lancez `XPGAnalyser-Setup-1.11.21.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.21.exe` (15,3 Mo, 15 995 646 octets) :
`D9B3CEE327B46A85E1F9DD79CF558DD2113A28B38DA730E3D18A60B5C30CE861`

Le zip portable `XPGAnalyser-1.11.21-portable.zip` (20,4 Mo) contient le même exe et les mêmes fichiers que l'installateur, dans un dossier `XPGAnalyser-1.11.21`. Décompressez-le, puis lancez `XpgAnalyzer.exe`. Son empreinte SHA-256 :
`8FD92260E71A73B071742528BBDC2DF0DED20B8EF6A5E506C36DD35687051EF2`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (66 sur 66). Elle comprend `hmioverload` (83 contrôles, dont 14 nouveaux), les 6 555 contrôles de `hmieditor` (dont 29 nouveaux), les 905 de `hmidecl` (dont 15 nouveaux), les 4 680 de `hmi`, les 84 de `hmitypes`, les 67 de `hmicheckexpr` et les deux traces de référence, `hmitrace` et `hmitracemig`.

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.21` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session du § 5 a été rejouée avec l'exe livré : 5 captures `11121_*`, aucun contrôle en échec.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé pour tous les comptes, par-dessus la 1.11.20 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.21`.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. La section « Tester vous-même » donne le parcours à refaire chez vous.

## La suite

**La 1.11.22 : les raccourcis clavier** (votre § 14). Votre profil actuel reste le défaut, et un profil « Visual Studio » est proposé à côté.
