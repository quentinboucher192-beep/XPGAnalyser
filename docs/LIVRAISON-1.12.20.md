# API Studio et IHM Studio 1.12.20 : les nouveaux noms, et la fin de la maquette des rappels et génériques

Livrée le 21/10/2026. Elle suit la 1.12.19 du même jour (`docs/LIVRAISON-1.12.19.md`).

Elle répond à trois demandes :

> « continue » : finir la maquette des rappels et génériques (https://claude.ai/artifact/FkuyTs9y8wsHHU4NgQVyoe), ce que la partie 7 de la 1.12.19 disait qu'il restait.

> « peux tu ajouter static_cast dynamic_cast en ST, C+++, C stp »

> « Peux tu renommer l'application aussi stp avec un nom plus cohérent, refaire le logo pour avoir un logo pour API, et un logo pour IHM » ; « pour les noms il faut se séparer de XPG surtout » ; « avec studio à la fin en 2 mots »

Les deux applications changent de nom. **IHM Studio** change le plus ; **API Studio** prend son nom et son logo.

## 1. Les noms et les logos

- **XPGAnalyser API devient API Studio** (`API-Studio.exe`) ; **XPGAnalyser IHM devient IHM Studio** (`IHM-Studio.exe`). Les deux ensemble s'appellent **API IHM Studio** : l'installateur, le menu Démarrer.
- Le nom est écrit à un seul endroit, et l'interface le lit partout : le titre de la fenêtre, À propos, l'accueil, `--version`, les fenêtres de plantage et de blocage, les rapports, la fiche des raccourcis, les documents produits (dossier, PDF, impressions), la Console, l'en-tête des fichiers écrits, `SYS.AppName`, l'identification Modbus, les notifications.
- **Deux logos de la même famille** (le cadre arrondi, les traits clairs, le point jaune) :
  - API Studio : un barreau de schéma à contacts, un contact et sa bobine, en bleu ;
  - IHM Studio : un écran de supervision, un cadran et son aiguille, en sarcelle.

  Chaque exe a son icône ; l'installateur a la sienne (les deux logos) et ses images d'assistant refaites.
- **Ce qui garde l'ancien nom, pour ne rien perdre** : la clé du registre `Software\XPGAnalyser`, le dossier des réglages `%APPDATA%\XpgAnalyzer`, `XPGAnalyser.ini`, le dossier `Documents\XPGAnalyser`, et les extensions `.xpg…` des projets. Les projets, les réglages, les récents et les licences se retrouvent tels quels. Les fichiers `.XPG` de Control Expert restent lus.

## 2. STATIC_CAST et DYNAMIC_CAST

- **`STATIC_CAST<T>(x)`** convertit comme `static_cast` du C++, sans vérifier :
  - un réel est tronqué vers zéro ;
  - un entier qui déborde boucle : `STATIC_CAST<INT>(40000)` vaut `-25536` ;
  - un nombre devient un BOOL (non nul : TRUE) ou une valeur d'énumération, sans contrôle.
- **`DYNAMIC_CAST<T>(x)`** vérifie que la valeur tient exactement : les bornes du type, pas de décimales, 0 ou 1 pour un BOOL, une valeur de l'énumération. Sinon, une erreur que `TRY … CATCH` attrape, avec la raison : « 40000 ne tient pas dans un INT (-32768 … 32767) », « n'est pas une valeur de T_MODE (Arret = 0, Auto = 1, Manu = 2) ».
- **T** : un type numérique, BOOL, une énumération du projet, ou le T d'une fonction TEMPLATE. Un texte est refusé, et STRING_TO_INT et STRING_TO_REAL sont proposés ; un type mal écrit, avec le bon (« UINTT : UINT ? »).
- Leurs fiches donnent le C (`(T)x`) et le C++ (`static_cast`, `std::in_range`).

## 3. Les finitions des rappels

- **Une expression de propriété ne modifie rien.** Une LAMBDA d'une fx qui écrit une variable, appelle une fonction qui en écrit une, ou appelle une fonction IHM_ qui agit (IHM_LOG, IHM_NAVIGUER…) est refusée, avec la raison. Dans un script ou une action, elle reste permise.
- **Un nom de fonction en texte** là où va un rappel (`'Valider'`, `LIST_TRANSFORM(S, D, 'Carre')`, `IHM_APRES(T#1s, 'Fermer')`) : un avertissement, et **sa correction d'un clic** (Diagnostics, Ctrl+.). Ctrl+Z la défait.
- Une variable rappel rémanente à laquelle un code donne une LAMBDA : un avertissement (elle ne se garde pas, la valeur initiale revient).
- Un rappel de popup non donné et sans défaut : un avertissement, il vaut NULL. Avant : une erreur.
- Deux défauts de la 1.12.19 corrigés : Générer prenait les paramètres et les types d'une LAMBDA d'une fx pour des variables inexistantes ; Générer et Compiler disaient « fonction inconnue : LAMBDA » dans un script qui passe une LAMBDA en argument.

## 4. Les types rappel nommés : T_Filtre

- **Types IHM › Nouveau type ▾ › Rappel…** crée `T_Rappel` (`CALLBACK (x : REAL) : BOOL`), à renommer. Sa page remplace les membres : la signature (double-clic pour la changer), ses entrées, son retour, et les fonctions du projet qui conviennent, ou pourquoi les autres non.
- **Il s'emploie comme sa forme** : une variable IHM, un paramètre de popup ou de symbole, une locale, un paramètre de fonction. Générer vérifie ce qu'on y range ; le forçage en simulation aussi. Il ne se lie pas à un équipement.

## 5. Les dernières natives à rappel

- **`MAP_REMOVE_IF(m, f)`** retire les paires pour lesquelles `f(clé, valeur)` rend TRUE.
- **Les plis** prennent un rappel `f(a, b)` à la place de l'OPÉRATION : `ACCUMULATE` et `REDUCE (T, init, f)`, `PARTIAL_SUM` et `INCLUSIVE_SCAN (T, f)`, `EXCLUSIVE_SCAN (T, init, f)` ; `TRANSFORM_REDUCE` et les deux `TRANSFORM_…_SCAN` en prennent un pour l'opération, la transformation, ou les deux. Avec un rappel, les éléments peuvent être des structures : `ACCUMULATE(Lots, 0.0, LAMBDA (s : REAL; l : T_Lot) : REAL => s + l.Poids)`.
- **Les clés** : `LIST_SORT_BY`, `LIST_MIN_INDEX` et `LIST_MAX_INDEX` prennent un rappel qui donne la clé (les mesures les plus proches de la consigne d'abord).
- **`IHM_SUR_CHANGEMENT(x, f)`** appelle `f`, ou `f(nouvelle valeur)`, chaque fois que x change. x est relu à chaque tour de l'IHM, ses indices figés à l'inscription ; une variable du script ou une structure entière est refusée, avec la raison. `IHM_ANNULER` l'arrête, l'arrêt de l'IHM aussi.
- Les formes d'avant (`COMPARAISON#`, `OPERATION#`, `TRANSFORMATION#`, `'Poids'`) restent.

## 6. Le C et le C++ : la vérification et l'en-tête du projet

- **La vérification** d'un script C ou C++ lit aussi :
  - les chevrons d'un template (`std::function<BOOL(REAL) f;` : « il manque le > qui ferme std::function<… ») ;
  - la forme d'une lambda (`[&](REAL x) { … }`) ;
  - le C++ écrit dans un script C (`std::`, `template`, `nullptr`, une classe, une lambda), avec ce qu'il faut en C ;
  - un nom de fonction du projet mal écrit, en avertissement : « Arreter_Pomp : veux-tu dire Arreter_Pompe ? ».
- **Le bouton En-tête du projet** d'un script C ou C++ montre, en lecture seule et dans la langue du script, ce que le script peut employer : les types IHM, les rappels, les fonctions IHM et celles de la vue, les variables IHM. **Copier** le met dans le presse-papiers. Ce qui n'a pas d'équivalent en C (une LIST, une MAP) est dit en commentaire.
- Le C et le C++ restent vérifiés, pas exécutés : seul le ST tourne en simulation.

## 7. Les structures, les symboles et les popups génériques

- **Une structure générique** : Types IHM › la structure › **Génériques** (`T`, `T : ANY_NUM`, `K, V`). Ses membres peuvent être de type T (`Valeur : T`, `Historique : LIST OF T`). La liste des types la montre `T_Mesure<T>`.
- **Elle s'emploie avec ses types partout où va un type** : une variable IHM `Debit : T_Mesure<REAL>` (ses membres de base ont leur adresse, comme ceux d'une structure), `LIST OF T_Mesure<REAL>`, `MAP[STRING] OF T_Mesure<BOOL>`, un tableau, une locale ou un paramètre du ST. Le sélecteur de types la propose avec un type par générique (`T_Mesure<REAL>`), à changer.
- **Les contraintes se vérifient partout.** `T_Num<STRING>` est refusé : « T doit être un ANY_NUM ; STRING n'en est pas un ». Seule (`T_Mesure`), elle est refusée avec l'exemple. Deux types pour un T, ou des chevrons sur une structure ordinaire, aussi.
- **Une fonction TEMPLATE déduit T d'une instance** : `FUNCTION Valeur_De(m : T_Mesure<T>) : T` appelée avec un `T_Mesure<REAL>` rend un REAL.
- **Un symbole ou une popup générique** : Paramètres › **Génériques**. Un paramètre peut être de type T (`Valeur : T`, `Format : CALLBACK (v : T) : STRING`).
  - Chaque instance **déduit T de ce qu'elle lie** (`Valeur := PT1`, un REAL : le rappel `Format` attend alors `(v : REAL)`), ou **le donne** (`T := REAL`).
  - L'inspecteur de l'instance a une ligne **T** : vide, elle montre ce qui est déduit (« REAL · déduit de Valeur ») ; un type la fixe. Les arguments d'une ouverture de popup ont la même ligne.
  - Générer refuse un T hors contrainte ou deux T différents, avec ce qui le lève (« donne-le : T := REAL »).
- **L'en-tête du projet** l'écrit en C++ (`template <typename T> struct T_Mesure`) et en C (une macro, `T_Mesure(float) debit;`).
- **Le fichier du projet** passe au format 28 seulement s'il a des génériques ; sans eux, il garde son format, et la 1.12.19 l'ouvre.

## 8. Ce qui reste de la maquette

Les douze propositions sont faites, sauf la onzième qui était un refus (pas de débogueur pas à pas pour l'IHM). Deux détails de la planche 6 ne sont pas faits :
- l'onglet Usages d'une structure générique ne range pas encore ses emplois par T (`T_Mesure<REAL> · Debit, Historique`) ;
- le titre de l'instance d'un symbole générique reste le nom du symbole : la ligne T dit le type.

## 9. Les essais

77 suites sur 77. Les essais nouveaux de cette version :
- `hmi` (5712 contrôles) :
  - `generiques1220` (29 contrôles) : le modèle (lire et écrire les génériques, découper `T_Paire<STRING, T_Mesure<BOOL>>`) ; l'instance `T_Mesure<REAL>` faite une fois ; cinq emplois permis et quatre refus avec leur raison ; le registre des types (`LIST OF T_Mesure<REAL>` et sa clé, les refus) ; un symbole générique (T déduit, hors contrainte, donné ; le rappel de l'instance) ; une popup générique ; Générer et Compiler (l'instance hors contrainte refusée, rien d'autre) ; le moteur (une variable IHM `T_Mesure<REAL>`, des locales, une fonction TEMPLATE qui déduit T : 35 et 8) ; l'en-tête C et C++ ; l'enregistrement (format 28) ;
  - `scriptsC1220` : la vérification des scripts C et C++ et l'en-tête du projet ;
  - `typesRappel1220` et `finitions1220` : les types rappel nommés, les finitions des rappels.
- `hminatives` (2577 contrôles) : `rappels1220`, les dernières natives à rappel et `IHM_SUR_CHANGEMENT`.
- `hminatives1127` (297 contrôles) : STATIC_CAST et DYNAMIC_CAST, leurs refus.
- `hmieditor` (7558 contrôles) : l'écran des types rappel ; les notes (377) ; chaque exemple du guide en ST, C et C++.

## 10. Les captures

Prises avec l'exe Windows livré, lancé sous Wine, sur le projet de démonstration Armoire_Gaz :
- `1220_01_accueil.png` : l'accueil d'IHM Studio, son nom et son logo (l'écran de supervision et son cadran), la version 1.12.20 ; en bas, « Nouveautés de la 1.12.20 ».
- `1220_02_guide_rappels.png` : le guide, Rappels et génériques : CALLBACK, LAMBDA, TEMPLATE. « Sur cette page » montre les sujets de la 1.12.20 : le type rappel nommé, les natives à rappel de plus, la structure, le symbole et la popup génériques, STATIC_CAST et DYNAMIC_CAST.
- `1220_03_guide_scripts.png` : le guide, Scripts généraux et scripts de vue : la vérification C et C++ et l'en-tête du projet, avec son exemple en ST (les onglets C et C++ donnent l'en-tête).

Cette session a trouvé un défaut, corrigé avant la livraison : **deux puces du guide montraient leurs astérisques** (« bouton \*En-tête du projet\* », « \*Génériques\* ») : le guide met en gras, pas en italique. Elles sont en gras.

## 11. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.12.20.exe` : l'installateur, qui pose les deux applications ;
- `API-Studio-1.12.20-portable.zip` et `IHM-Studio-1.12.20-portable.zip` : chaque application, sans installation ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.12.20.exe`. Il met à jour la version installée, la 1.12.19 ou une plus ancienne, en place : même identité d'installation, même dossier (`C:\Program Files\XPGAnalyser` reste le dossier d'une mise à jour ; une première installation va dans `C:\Program Files\API IHM Studio`), données reprises sans être déplacées. Il retire les anciens exe (`XpgAnalyzer-API.exe`, `XpgAnalyzer-IHM.exe`), l'ancien dossier du menu Démarrer et ses raccourcis, puis pose **API Studio** et **IHM Studio** dans le dossier **API IHM Studio** du menu Démarrer. Les projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.12.20.exe` (31,5 Mo, 33 013 108 octets) :
`BBDCBDE31C51A99DA55CFD4A103FCD1D0190548581135D54B26827257620587A`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `API-Studio-1.12.20` ou `IHM-Studio-1.12.20`. Décompresse, puis lance l'exe.
- `API-Studio-1.12.20-portable.zip` (23,5 Mo) :
  `A9D64ACC18964A27F913F537930BF15C3C378371E352A537BB3524648AE05DC5`
- `IHM-Studio-1.12.20-portable.zip` (23,5 Mo) :
  `4426B17554A3FAEF49A58E6F42B65B8C41520BE43C8B5D65E7ABA49F1396DFD6`

## 12. Les vérifications sous Windows

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `API Studio 1.12.20` et `IHM Studio 1.12.20` ;
- ils importent exactement les mêmes DLL que ceux de la 1.12.19 (ADVAPI32, IPHLPAPI, KERNEL32, SDL3, SHELL32, USER32, WS2_32, msvcrt, ole32) ;
- `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- une session scriptée sur le projet de démonstration : les trois captures ci-dessus.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.19 :
- avant, la 1.12.19 était installée (`XPGAnalyser IHM 1.12.19`), avec son dossier XPGAnalyser dans le menu Démarrer ;
- « Installation process succeeded ». Il a repris l'installation en place : même dossier, journal de désinstallation complété ;
- le dossier d'installation ne garde que `API-Studio.exe`, `IHM-Studio.exe` et le désinstalleur : les anciens exe sont partis ;
- le menu Démarrer a le dossier **API IHM Studio** (API Studio, IHM Studio, Maintenance) ; l'ancien dossier XPGAnalyser est parti ;
- les deux exe installés sont identiques à ceux qui ont été testés. Installés, ils répondent `API Studio 1.12.20` et `IHM Studio 1.12.20`, et l'API installée analyse `MAST.XPG` comme avant.

**Les zips portables** ont chacun 151 fichiers, dont l'exe testé.

**Le projet Visual Studio** a les mêmes 467 fichiers .cpp et 422 en-têtes que CMake (`outils/verifier_vcxproj.py`).

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici).

## 13. La suite

- Essayer sur un vrai Windows, en particulier la mise à jour par-dessus la 1.12.19 (les nouveaux noms dans le menu Démarrer).
- Les deux détails de la partie 8, si tu les veux.
