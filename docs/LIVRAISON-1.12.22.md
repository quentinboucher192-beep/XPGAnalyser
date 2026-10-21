# API Studio et IHM Studio 1.12.22 : changer les génériques fait suivre leurs emplois

Livrée le 21/10/2026. Elle suit la 1.12.21 du même jour (`docs/LIVRAISON-1.12.21.md`).

Elle répond à une demande :

> « attention si je supprime les T, K et que j'avais fait des variables, il faut modifier les types tout seul coté variables tout seul »

## 1. Retirer T ou K : les variables suivent toutes seules

Dans **Types IHM** › une structure générique › la case **Génériques** de l'inspecteur. Avant, retirer K de `T_Extractor<T, K>` laissait les variables écrites `T_Extractor<REAL, STRING>`, un type qui n'existait plus. Maintenant chaque emploi est réécrit, partout :
- les variables IHM : `T_Extractor<REAL, STRING>` devient `T_Extractor<REAL>`, `LIST OF T_Extractor<INT, STRING>` devient `LIST OF T_Extractor<INT>` ;
- les membres des autres types (`Ex : T_Extractor<INT, STRING>` dans une autre structure) ;
- les déclarations des scripts, des fonctions et des vues, les paramètres de popup et de symbole, les retours de fonction, les signatures des rappels et le code.

**Dans les membres de la structure**, le générique retiré prend le type que lui donnaient ses emplois : `Valeurs : MAP[K] OF T` devient `MAP[STRING] OF T`, puisque les variables mettaient STRING à la place de K. Les variables gardent ainsi les mêmes membres, du même type. Si les emplois ne s'accordent pas (STRING ici, INT là), le plus fréquent l'emporte et le message le dit ; sans aucun emploi, K prend un type de sa contrainte (INT sans contrainte).

Les autres changements de la case suivent aussi :
- **Renommer** T en U : les membres suivent (`Valeur : T` devient `Valeur : U`) ; les emplois ne changent pas.
- **Déplacer** (`A, B` devient `B, A`) : les arguments de chaque emploi suivent (`T_Paire<INT, STRING>` devient `T_Paire<STRING, INT>`).
- **Ajouter** un générique : chaque emploi reçoit un type de sa contrainte (INT sans contrainte), à changer ensuite.
- **Tout retirer** : la structure n'est plus générique, ses emplois s'écrivent `T_Extractor` tout court.

Les génériques se reconnaissent par leur nom. Ceux qui ne se retrouvent pas sont pris dans l'ordre pour des renommages : `T, K` devenu `U, K`, c'est T renommé U.

**Un symbole, une popup générique** (la case **Génériques** de ses paramètres) : ses paramètres et ses déclarations suivent de même. Un T retiré prend le type que lui donnaient ses instances ou ceux qui l'ouvrent (`Valeur : T` devient `Valeur : REAL`) ; les arguments `T := REAL` des instances et des ouvertures sont retirés, ou renommés (`U := REAL`) quand T devient U.

**Tout d'un coup, et tout se reprend.** La case essaie d'abord sur une copie du projet : si un membre ne passait plus, le changement est refusé avec la raison, et rien n'est touché. Sinon tout se fait en une seule commande. Le message du bas dit ce qui a suivi (« Génériques de T_Extractor : T - 2 emplois suivent (Variables IHM / Extr1, Variables IHM / Extr2) ; K devient STRING dans les membres de T_Extractor (le type que lui donnaient ses emplois) ») et **Ctrl+Z reprend tout** : les génériques, les membres et chaque emploi.

Un emploi qui n'avait pas le bon nombre de types (déjà faux avant) reste tel quel.

Un défaut corrigé en passant : à l'instanciation d'une structure, le nom du paramètre d'un rappel était pris pour le générique. Le membre `Filtre : CALLBACK (t : T) : BOOL` de `T_Extractor<REAL>` devenait `CALLBACK (REAL : REAL) : BOOL` ; il devient `CALLBACK (t : REAL) : BOOL`.

Le code : un module commun, `hmi/HmiGenericEdit` (comparer deux listes de génériques, réécrire un type, appliquer à une structure ou à une vue), appelé par la case Génériques des Types IHM et par celle des paramètres d'un symbole ou d'une popup.

## 2. Les essais

77 suites sur 77. Les essais nouveaux de cette version :
- `hmi` (5945 contrôles) : `generiquesSuivent11221` :
  - la comparaison (K retiré, T renommé U, T et K déplacés) et la réécriture d'un type (`LIST OF T_Extractor<REAL, STRING>` devient `LIST OF T_Extractor<REAL>`) ;
  - un projet où `T_Extractor<T, K>` est employé par trois variables, le membre d'une autre structure, le retour et un paramètre d'une fonction : K retiré (tout suit, `Valeurs : MAP[STRING] OF T`, le message), puis T retiré (la structure n'est plus générique, T prend REAL), puis T rajouté (chaque emploi reçoit INT) ;
  - `T_Paire<A, B>` : un renommage (les emplois ne changent pas), un déplacement (les arguments suivent) ;
  - un symbole générique `Afficheur<T>` et son instance : T renommé U (`U := REAL` dans l'instance), puis retiré (`Valeur : REAL`, l'argument part de l'instance).
- `hmieditor` (8054 contrôles) : `generiquesSuiventEcran11221` : la case Génériques de Types IHM. K retiré : les deux variables suivent, et le message le dit. Ctrl+Z : tout revient. Aucun générique : `T_Extractor` tout court partout.
- Mis à jour : les notes (380) et les versions (55) dans `hmieditor`.

## 3. Les captures

Prises avec l'exe Windows livré, lancé sous Wine, sur une copie du projet de démonstration Armoire_Gaz à laquelle la session ajoute la structure `T_Extractor<T, K>` (membres `Name : STRING` et `Valeurs : MAP[K] OF T`) et deux variables du dossier Extractions, `Extr1 : T_Extractor<REAL, STRING>` et `Extr2 : LIST OF T_Extractor<INT, STRING>` :
- `1222_01_accueil.png` : l'accueil d'IHM Studio, la version 1.12.22.
- `1222_02_avant.png` : Types IHM › T_Extractor : Génériques `T, K`, le membre `Valeurs : MAP[K] OF T`, « Utilisé par Extr1, Extr2 ».
- `1222_03_K_retire.png` : la case Génériques réduite à `T` (comme à la main) : `Valeurs` est devenu `MAP[STRING] OF T`, et le message du bas dit « 2 emplois suivent (Variables IHM / Extr1, Variables IHM / Extr2) ; K devient STRING dans les membres de T_Extractor ».
- `1222_04_variables_suivent.png` : Variables IHM, le dossier Extractions : `Extr1` est maintenant `T_Extractor<REAL>`, `Extr2` une `LIST OF T_Extractor…` (la colonne coupe le reste, `<INT>`).

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.12.22.exe` : l'installateur, qui pose les deux applications ;
- `API-Studio-1.12.22-portable.zip` et `IHM-Studio-1.12.22-portable.zip` : chaque application, sans installation ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.12.22.exe`. Il met à jour la version installée (la 1.12.21 ou une plus ancienne) en place : même identité d'installation, même dossier, données reprises sans être déplacées. Les projets s'ouvrent tels quels : le format des fichiers ne change pas.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.12.22.exe` (31,6 Mo, 33 097 811 octets) :
`92A5C3F0707C01EEFC0D04AA45E44F7EC8724EBB1B4D15F50A6F6E2B5110E79A`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `API-Studio-1.12.22` ou `IHM-Studio-1.12.22`. Décompresse, puis lance l'exe.
- `API-Studio-1.12.22-portable.zip` (23,6 Mo) :
  `8C48E2B3C88838E58BE93BC3B0B25A0A4AA373AB60130176C91FCCE93CDAA38C`
- `IHM-Studio-1.12.22-portable.zip` (23,6 Mo) :
  `835E9913E96EFD6FC85B89C734B3A8CF71A5C9BC1EBFD4156C7FFED7C228673B`

## 5. Les vérifications sous Windows

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `API Studio 1.12.22` et `IHM Studio 1.12.22` ;
- ils importent exactement les mêmes DLL que ceux de la 1.12.21 (ADVAPI32, IPHLPAPI, KERNEL32, SDL3, SHELL32, USER32, WS2_32, msvcrt, ole32) ;
- `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- une session scriptée sur le projet de démonstration : les quatre captures ci-dessus. K y est retiré par la case Génériques de l'inspecteur, et les deux variables suivent.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.21 :
- avant, la 1.12.21 était installée (`IHM Studio 1.12.21`) ;
- « Installation process succeeded ». Il a repris l'installation en place : même dossier, journal de désinstallation complété ;
- le dossier d'installation a `API-Studio.exe`, `IHM-Studio.exe` et le désinstalleur ; le menu Démarrer garde son dossier **API IHM Studio** (API Studio, IHM Studio, Maintenance) ;
- les deux exe installés sont identiques à ceux qui ont été testés. Installés, ils répondent `API Studio 1.12.22` et `IHM Studio 1.12.22`, et l'API installée analyse `MAST.XPG` comme avant.

**Les zips portables** ont chacun 151 fichiers, dont l'exe testé.

**Le projet Visual Studio** a les mêmes 469 fichiers .cpp et 424 en-têtes que CMake (`outils/verifier_vcxproj.py`) : `hmi/HmiGenericEdit` y est ajouté.

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici).

## 6. La suite

- Essayer sur un vrai Windows : retirer un générique d'une structure employée par des variables, puis Ctrl+Z ; la mise à jour par-dessus la 1.12.21.
- Le sélecteur ne propose pas encore les génériques d'une fonction TEMPLATE dans la grille de ses déclarations (on y écrit `T` à la main, comme avant).
