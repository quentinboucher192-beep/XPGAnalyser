# XPGAnalyser 1.12.1 : les énumérations des objets, de nouveaux opérateurs, instructions et types

Livrée le 10/10/2026 en fin de nuit. Elle répond à votre demande :

> « Fait les enumerations (pour savoir ce que tu as besoin, prend chaque objet de la biblio, regarde chaque paramètre, si un parametre peut correspondre à une potentielle enum, tu fais une nouvelle enum), les operateurs instructions, les types, les nouvelles enums doivent etre utilisables dans les paramètres des objets qui ont des paramètres qui repondnet à une enumerations »

Elle suit la liste envoyée avant de coder, avec les écarts expliqués au § 2.

Tout se passe dans **XPGAnalyser IHM** : les scripts, les fonctions, les cases ƒ et les textes à trous. Le ST de l'automate (XPGAnalyser API) ne change pas.

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.1.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.1-portable.zip` et `XPGAnalyser-IHM-1.12.1-portable.zip` : chaque application, sans installation ;
- ce document ;
- les captures `1121_ihm_*`, prises sous Wine avec l'exe livré.

## 1. Ce qui change pour vous

| | Avant (1.12.0) | Maintenant (1.12.1) |
|---|---|---|
| Énumérations natives | 7 | 59 : 52 nouvelles |
| Une propriété à liste (Alignement, Forme, Style…) pilotée par ƒ | un texte : `'centre'` | aussi `ALIGNEMENT#Centre`, et Compiler vérifie la valeur |
| Opérateurs | `:= + - * / MOD ** = <> < <= > >= AND OR XOR NOT ^` | en plus : `? :`, `??`, `IN [ ]`, `ENTRE … ET`, `&` |
| Instructions | `IF CASE FOR FOR EACH WHILE REPEAT EXIT RETURN` | en plus : `CONTINUE`, `TRY … CATCH … END_TRY`, `ASSERT`, `CASE` sur un texte ou une plage négative |
| Types de base | 17 | 22 : `CHAR`, `WSTRING`, `DATE`, `TIME_OF_DAY`, `DATE_AND_TIME` |
| Fonctions natives | 119 | 123 : `MAINTENANT`, `DT_TO_DATE`, `DT_TO_TOD`, `CONCAT_DATE_TOD` |

Tout se retrouve dans **IHM › Programmation générale › Natives**, avec une fiche par élément. **F1** sur un nom ouvre sa fiche.

## 2. Les énumérations des objets

**L'inventaire.** J'ai relu les 103 objets de la bibliothèque et toutes leurs propriétés. J'ai gardé chaque propriété dont l'inspecteur propose une liste fixe de mots. Les listes qui dépendent du projet (recettes, symboles, alarmes, styles nommés, images, vues…) restent des noms du projet, pas des énumérations.

**Le résultat.** 52 nouvelles énumérations : 47 tirées des objets et 5 générales. Avec les 7 de la 1.12.0, cela fait 59. Un essai pose chacun des 101 genres d'objets et lit son inspecteur : sur 160 listes, 145 ont leur énumération, avec exactement les mêmes mots.

**Dans une propriété.** Dans la case ƒ d'une propriété à liste, l'expression rend une valeur de l'énumération. L'objet reçoit le mot de la liste qu'il lisait déjà, donc vos projets ne bougent pas :

```
Alignement ƒ = SEL(Defaut, ALIGNEMENT#Gauche, ALIGNEMENT#Centre)
Forme ƒ      = Etat = 2 ? FORME_VOYANT#Carre : FORME_VOYANT#Rond
```

- **Compiler** et la pastille ƒ rouge de l'inspecteur refusent la valeur d'une autre énumération (`TRANSITION#Fondu` dans un alignement) et un nombre hors de l'énumération (`7` dans le style d'un sélecteur). Le message liste les valeurs attendues.
- **L'aide à la saisie** de la case ƒ propose les valeurs dès le `=`. Dans un script, `ALIG` propose `ALIGNEMENT#`, puis ses valeurs.
- **La fiche** de chaque énumération dit **où elle sert** : la liste des objets et des propriétés.
- Un mot entre apostrophes (`'centre'`) marche toujours.

**Les énumérations ajoutées.**

| Famille | Énumérations |
|---|---|
| Texte et affichage | `POLICE`, `ALIGNEMENT`, `ORIENTATION`, `ETIREMENT`, `RECOLORATION`, `SENS_DEFILEMENT`, `CORRECTION_QR` |
| Commandes et saisie | `TYPE_SAISIE`, `CLAVIER_VIRTUEL`, `CONFIRMATION`, `OPERATION_BOUTON`, `STYLE_SELECTEUR`, `CHAMPS_DATE`, `RESOLUTION_PLANNING`, `NIVEAU_ACCES`, `SIGNATURE` |
| Voyants, mesures, horloges | `FORME_VOYANT`, `STYLE_HORLOGE`, `FORMAT_DUREE` |
| Courbes et graphiques | `MODE_COURBE`, `ECHELLE`, `INTERPOLATION`, `ETIQUETTES_PARTS` |
| Alarmes, historiques, exports | `SOURCE_HISTORIQUE`, `ALARME_MONTREE`, `ALARMES_COMPTEES`, `PERIODE_STATS`, `CLASSEMENT`, `FORMAT_FICHIER` |
| Synoptique | `PLACE_LIBELLE`, `COMMANDE_VANNE`, `FORME_TUBE`, `MONTAGE_ISA`, `VANNE3_VARIANTE`, `VANNE3_BOISSEAU`, `VANNE3_COMMANDE` |
| Navigation et structure | `STYLE_NAVIGATION`, `CHEMIN_ARIANE`, `PLACE_ONGLETS`, `STYLE_TITRE`, `DEFILEMENT`, `STYLE_LANGUE`, `LIBELLE_LANGUE`, `ONGLET_PARAMETRES` |
| GIF animé | `LECTURE_GIF`, `DEPART_GIF`, `FIN_GIF` |
| Générales (scripts) | `JOUR`, `MOIS`, `QUALITE`, `ETAT_MOTEUR`, `MODE_MARCHE` |

Trois énumérations de la 1.12.0 servent aussi aux objets : `TRANSITION` (barre de navigation), `ONGLET_CONNEXION` (menu de connexion, qui gagne la valeur `Connexion`) et `SOURCE_EXPORT` (bouton d'export, qui gagne `Audit`). Leurs nombres ne changent pas.

**Les écarts avec la liste envoyée.**
- **C (`DIRECTION_TRANSITION`, `COURBE_TRANSITION`) : retirées.** Les paramètres d'une action (une transition, une popup, le clavier, les paramètres système) se choisissent dans la fenêtre de l'action : ce ne sont pas des expressions, donc une énumération n'y servirait pas. `POSITION_POPUP`, `CLAVIER_VIRTUEL` et `ONGLET_PARAMETRES` restent utilisables dans les scripts (`IHM_POPUP`) et dans les propriétés des objets.
- **D, les énumérations générales : 5 sur 11.** `NIVEAU_UTILISATEUR` est couvert par `NIVEAU_ACCES`. `TYPE_MESSAGE`, `ETAT_EQUIPEMENT`, `ORDRE_OCTETS`, `UNITE` et `CURSEUR` sont retirées : aucune fonction ni aucune propriété ne s'en servirait encore.

## 3. Les opérateurs

| Opérateur | Ce qu'il fait | Exemple |
|---|---|---|
| `c ? a : b` | `a` si `c` est vrai, sinon `b`. Seul le côté choisi est lu. | `Ratio := Debit <> 0 ? Volume / Debit : 0.0;` |
| `a ?? b` | `a`, ou `b` quand `a` ne se lit pas (division par zéro, case hors du tableau, clé absente d'une MAP, NULL) | `Texte := Recettes['B'] ?? 'aucune';` |
| `x IN [ … ]` | vrai si `x` est une des valeurs ou tombe dans une des plages | `Arret := Etat IN [0, 3, 7..9];` |
| `x ENTRE a ET b` | vrai si `a <= x <= b` | `Normal := Pression ENTRE 0.5 ET 3.5;` |
| `&` | une autre écriture de `AND` | `Pret := Marche & Ok;` |

`IN` accepte des nombres, des textes et des valeurs d'énumération, même sans le préfixe (`Mode IN [Auto, Manu]`). `? :` est le plus faible des opérateurs et se lit de droite à gauche. `ENTRE` est au rang des comparaisons.

`&` était déjà annoncé dans la fiche des opérateurs de la 1.12.0, mais le moteur ne le lisait pas. L'essai des fiches l'a trouvé ; c'est corrigé.

## 4. Les instructions

- **`CONTINUE`** passe au tour suivant de la boucle (FOR, FOR EACH, WHILE, REPEAT). Hors d'une boucle, c'est une erreur.
- **`TRY … CATCH Erreur … END_TRY`** : une erreur du bloc n'arrête plus le script. Le CATCH s'exécute, et le message, en français, va dans la variable STRING nommée après CATCH (facultative). Une boucle sans fin ou un cycle trop long arrête toujours tout, comme avant.

  ```
  TRY
      Ratio := Volume / Debit;
  CATCH Message
      Ratio := 0.0;
      IHM_LOG(NIVEAU_LOG#WARNING, Message);   (* « division par zéro » *)
  END_TRY
  ```
- **`ASSERT(condition, 'message')`** : si la condition est fausse, le script s'arrête et la Console et les Diagnostics donnent le message. Dans un TRY, c'est son CATCH qui s'exécute.
- **`CASE`** sur un texte (`'Auto':`) et sur des nombres négatifs (`-5..-1:`). Les plages (`4..9:`) existaient déjà.

## 5. Les types

| Type | Littéraux | Ce qu'on en fait |
|---|---|---|
| `CHAR` | `'A'` | un texte d'un caractère ; vers CHAR, seul le premier caractère est gardé |
| `WSTRING` | `'été'` | un texte ; l'IHM écrit tout en UTF-8, donc WSTRING et STRING s'échangent |
| `DATE` | `D#2026-10-09` | `DATE - DATE` donne une durée |
| `TIME_OF_DAY` (`TOD`) | `TOD#14:30:00`, `TOD#06:00` | `TOD + TIME` tourne sur 24 h ; `TOD - TOD` donne une durée |
| `DATE_AND_TIME` (`DT`) | `DT#2026-10-09-14:30:00` | `DT + TIME`, `DT - TIME`, `DT - DT` (une durée) |

Les nouvelles fonctions : `MAINTENANT()` (la date et l'heure du poste ; en simulation, celles du PC), `DT_TO_DATE`, `DT_TO_TOD` et `CONCAT_DATE_TOD`. Dans une case ƒ ou un texte à trous, une date s'affiche lisiblement : `{MAINTENANT()}` donne `2026-10-09 14:30:00`.

Ces types valent pour les locales, les paramètres, les retours de fonction et les opérandes, comme `SINT` ou `BYTE` avant eux. Ce ne sont pas encore des variables IHM : il leur manque une place Modbus (§ 9).

**Les génériques** `ANY`, `ANY_ELEMENTARY`, `ANY_NUM`, `ANY_INT`, `ANY_REAL`, `ANY_BIT`, `ANY_STRING` et `ANY_DATE` sont expliqués dans la fiche des types. **F1** sur leur nom l'ouvre. Ils ne se déclarent pas : ils disent, dans la signature d'une native, ce qu'un paramètre accepte (`ABS(IN : ANY_NUM)`).

## 6. Le parcours vérifié sous Wine

Une session a été jouée avec l'exe livré : `tools/sessions/session-1121-ihm.txt`. Elle part d'un premier lancement de **XPGAnalyser IHM** 1.12.1, avec `Armoire_Gaz` (la copie de démonstration des livraisons précédentes) posé dans `projets\` et recopié dans `projets\ihm` au lancement.

| Étape | Ce qui s'est passé | Capture |
|---|---|---|
| **1.** Natives › Énumérations › ALIGNEMENT | Les 3 valeurs, leur nombre et le mot reçu ; « Où elle sert » : tout objet qui a Alignement (`align`) ou Place du titre (`titleAlign`) ; « pour les objets » dans l'en-tête | `1121_ihm_01` |
| **2.** Le sommaire des énumérations | Les 59, avec ce qu'elles servent : une fonction IHM_ ou des objets et leurs propriétés | `1121_ihm_02` |
| **3.** L'opérateur `? :` | Sa phrase, ST / C / C++, l'exemple exécuté : `x := Compteur > 5 ? Niveau * 2.0 : 0.0;` donne 85 | `1121_ihm_03` |
| **4.** L'opérateur `IN [ ]` | `Compteur IN [1, 3, 5..8]` donne TRUE | `1121_ihm_04` |
| **5.** L'instruction `TRY` | Sa forme et l'exemple dans chaque langage | `1121_ihm_05` |
| **6.** Le type `DATE_AND_TIME` | MIN (`DT#1970-01-01-00:00:00`), MAX (`DT#9999-12-31-23:59:59`), 64 bits, en C (`time_t`) et en C++, ses littéraux | `1121_ihm_06` |
| **7.** Le sommaire des types | 22 types de base ; « Sur cette page » mène aux génériques (ANY_…) | `1121_ihm_07` |
| **8.** La fonction `MAINTENANT` | Sa fiche, dans la catégorie Dates et heures : `MAINTENANT() : DATE_AND_TIME`, exécutée en simulation, son équivalent en C et en C++ | `1121_ihm_08` |
| **9.** Le script `Init`, en fin de script : `IF Compteur_Clics ENTRE 0 ET 1000`, `? :`, `TRY … CATCH … END_TRY` avec une division par zéro, `DT_TO_TOD(MAINTENANT()) > TOD#12:00:00` | « aucune erreur », « Saisie : aucune faute dans le document montré » ; TRY et CATCH se replient comme IF | `1121_ihm_09` |
| **10.** Simulation › IHM | « Build terminé : 0 erreur » ; l'IHM est en marche, le script Init s'est exécuté sans erreur (la division par zéro est attrapée par son CATCH) | `1121_ihm_10` |

**Ce que la session a trouvé, et qui est corrigé dans l'exe livré :**
- La catégorie des types textes et durées s'appelait « Chaînes et durées ». Elle s'appelle maintenant « Textes, durées et dates ».
- Une fiche d'opérateur ou d'instruction demandée par son nom (`instruction:TRY`, `operateur:? :`) ouvrait celle du rang 0 (`IF`, `:=`). Les clés de l'arbre sont des rangs ; le nom est maintenant accepté aussi.
- La forme de `TRY` et d'`ASSERT` était une longue phrase, que le bouton Copier chevauchait. Elle est raccourcie.

Aucun contrôle de la session n'a échoué.

## 7. Fichiers modifiés

- **Le catalogue** : `outils/natives/catalogue.py` (les énumérations et leurs propriétés, les opérateurs, les instructions, les fonctions de dates, les types, les génériques), `generer.py` et `src/hmi/HmiNativesData.cpp` (généré).
- **Les énumérations dans les objets** : `src/hmi/HmiNatives.*` (`propertyEnum`, `propertyWord`, `enumForWords`, `propertyProblem`, `enumLiteralsIn`), `HmiLive.cpp` (la traduction en marche), `HmiCheck.cpp` (Compiler), `src/app/hmi/HmiPanels.cpp` (la pastille ƒ, la clé de chaque ligne), `HmiAssist.cpp` (l'aide à la saisie), `HmiNativesCards.cpp` (« Où elle sert », les génériques), `src/ui/widgets/DataViews.hpp` (`Property::key`).
- **Le langage** : `src/sim/Interpreter.*` (le lexer, le parseur et le moteur du dialecte de l'IHM : les opérateurs, les instructions, les littéraux de dates ; `caughtMessage`), `src/sim/Value.*` (DATE, TOD, DT, le calendrier), `src/hmi/HmiRuntime.cpp`, `HmiExpr.cpp` (l'affichage des dates, le `:` d'une heure dans un texte à trous), `HmiScript.cpp`, `HmiScriptCheck*.cpp`, les listes de mots-clés (`HmiViewPaths`, `HmiComm`, `HmiEnums`, `HmiFunctionPanes`, `HmiTreeData`, `HelpTryIt`, `ui/Syntax`).
- **Les types** : `src/hmi/HmiTypeRegistry.*` (les 5 types, la famille Date, la règle de conversion), `HmiDeclEdit.cpp`.
- **La version** : `CMakeLists.txt`, `src/core/Version.hpp`, `src/hmi/HmiPackage.hpp`, `src/help/ReleaseNotes.cpp` (4 notes).

## 8. Les tests ajoutés ou changés

- `hmi_test` : `enumsProprietes1121` (le catalogue, la traduction en marche par le vrai moteur, les erreurs de Compiler).
- `hmi_editor_test` : `enumsInspecteur1121`. Il pose les 101 genres d'objets et compare chaque liste de l'inspecteur à son énumération ; il teste aussi l'aide à la saisie de la case ƒ, la pastille rouge et la fiche.
- `hmi_natives_test` : `langage1121` (les opérateurs, les instructions, le ST de l'automate qui les refuse) et `types1121` (les littéraux, le calcul, Compiler, le registre, l'affichage). Chaque exemple des fiches est exécuté en script, en case ƒ et en texte à trous : 1 731 contrôles.
- `hmi_types_test` et les essais des notes de version : les listes des 22 types, et 34 versions.

## 9. Les limites

- `DATE`, `TIME_OF_DAY`, `DATE_AND_TIME`, `CHAR` et `WSTRING` ne sont pas encore des **variables IHM** : il faudrait choisir leur place Modbus (Schneider range DT en BCD sur 4 mots).
- `? :`, `??`, `IN`, `ENTRE`, `TRY`, `CONTINUE`, `ASSERT` et les dates n'existent que dans le langage de l'IHM, pas dans le ST de l'automate.
- Les scripts C et C++ sont édités et vérifiés, pas exécutés (comme avant). Les fiches donnent l'équivalent dans chaque langage.
- Une propriété pilotée par ƒ n'est vérifiée que si son expression est une constante (`TRANSITION#Fondu`, `7`). Une expression calculée n'est jugée qu'en marche.

## Tester vous-même

1. Lancez **XPGAnalyser IHM** et ouvrez un projet. Allez dans **Programmation générale › Natives › Énumérations › ALIGNEMENT** : la section « Où elle sert » liste les objets et la propriété Alignement.
2. Posez un **Texte**, puis dans l'inspecteur cliquez sur ƒ à côté d'**Alignement** et tapez `=` : `ALIGNEMENT#Gauche`, `#Centre` et `#Droite` sont proposés. Choisissez `ALIGNEMENT#Centre`, puis lancez la simulation : le texte est centré.
3. Remplacez par `=TRANSITION#Fondu` : la pastille ƒ devient rouge, et **Compiler** dit « n'est pas une valeur de ALIGNEMENT ».
4. Dans un script :
   ```
   IF Compteur ENTRE 0 ET 10 THEN
       Compteur := Compteur > 5 ? 0 : Compteur + 1;
   END_IF
   TRY
       Compteur := 100 / (Compteur - Compteur);
   CATCH
       Compteur := 99;
   END_TRY
   ```
   Compiler l'accepte ; en simulation, `Compteur` vaut 99 et la Console ne signale aucune erreur.
5. Dans un texte à trous : `Il est {DT_TO_TOD(MAINTENANT())}` affiche l'heure du PC.

## Installer

1. Lancez `XPGAnalyser-Setup-1.12.1.exe`. Il met à jour la version installée (même identité d'installation, données reprises sans être déplacées) et pose les deux applications avec leurs deux raccourcis, comme la 1.12.0. Vos projets ne changent pas : aucun format de fichier ne change.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.12.1.exe` (27,8 Mo, 29 155 416 octets) :
`DEFEA1192724952A926F80BD89C3E56784340A85FC46F1669453D2B5728CB552`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `XPGAnalyser-API-1.12.1` ou `XPGAnalyser-IHM-1.12.1`. Décompressez, puis lancez l'exe.
- `XPGAnalyser-API-1.12.1-portable.zip` (20,8 Mo) :
  `A63A92B5C8AFCBDFED187335D0265C5384B21986606961196153D7077322E8D0`
- `XPGAnalyser-IHM-1.12.1-portable.zip` (20,8 Mo) :
  `D67EF4F559BC4FB6E2F5DBC8C3EE1ED4188452E7012345B308A21ED6AC94D102`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe, 69 sur 69. Elle comprend les 6 665 contrôles de `hmieditor`, les 4 750 de `hmi`, les 1 731 de `hminatives` et les 258 de `hmireperes`. Après les corrections trouvées sous Wine (§ 6), les essais touchés (`hmieditor`, `hminatives`, `hmitypes`) ont été relancés et passent.

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `XPGAnalyser API 1.12.1` et `XPGAnalyser IHM 1.12.1` ;
- `XpgAnalyzer-API.exe --cli MAST.XPG` analyse le projet d'essai (891 variables, 29 POU), comme avant : le ST de l'automate n'a pas changé ;
- la session du § 6 a été rejouée avec l'exe livré : 10 captures `1121_ihm_*`, aucun contrôle en échec.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.0 :
- « Installation process succeeded » ;
- avant comme après : `XpgAnalyzer-API.exe`, `XpgAnalyzer-IHM.exe`, et les raccourcis « XPGAnalyser API » et « XPGAnalyser IHM » ;
- les deux exe installés sont identiques à ceux qui ont été testés.

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici). La section « Tester vous-même » donne le parcours à refaire chez vous.

## La suite

- `DATE`, `TOD`, `DT`, `CHAR` et `WSTRING` comme variables IHM (avec leur place Modbus) ;
- les raccourcis clavier des éditeurs (le § 14 de la spécification de la refonte des scripts) ;
- la source du guide Word, à remettre à jour depuis la 1.11.2.
