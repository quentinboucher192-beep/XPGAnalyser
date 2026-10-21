# API Studio et IHM Studio 1.12.23 : la plume lit sa formule, les carrés du Contenu s'ouvrent

Livrée le 21/10/2026. Elle suit la 1.12.22 du même jour (`docs/LIVRAISON-1.12.22.md`).

Elle répond à des captures (une courbe dont la plume vaut `SPPV_TELUNA[0].SP`, refusée ; la structure `SPPV<K>` dans Types IHM) :

> « bug »

> « d'ailleurs si j'appuie sur le 'C' ca ouvre pas le selecteur de constantes, variables ihm, systemes etc »

> « pourquoi ces bugs ? tu as corrigé ça ? »

## 1. Pourquoi ces bugs

Les deux premiers viennent de la 1.12.19 (la règle des cases). Elle a posé, dans les onglets Contenu, Actions et Raccourcis, le carré au bout de chaque case et la raison en rouge sous une case fautive, comme dans l'inspecteur. Deux choses y manquaient :

- **La plume était lue comme une valeur fixe.** La case Expression d'une plume n'attend qu'une formule : elle la garde telle quelle, sans `=`. Le contrôle la lisait pourtant comme une valeur fixe, qui devait être un nombre : toute variable y donnait « n'est pas un nombre », `SPPV_TELUNA[0].SP` comme `Niveau`. Les autres cases faites pareil avaient le même défaut : la condition d'une alarme (`Pression > 10` refusée), celle d'un état d'un objet multi-états, le Min et le Max d'une saisie au clavier.
- **Le clic sur le carré n'était branché que dans l'onglet Propriétés.** Dans les autres onglets, le carré (C pour constante, fx, I, S…) se voyait mais ne répondait pas.

Le troisième, « selon T » pour `SPPV<K>`, était écrit tel quel dans le code, pour toutes les structures génériques.

## 2. Ce qui change

- **La case Expression d'une plume se lit en formule** : `SPPV_TELUNA[0].SP` est une variable IHM, un INT (`SPPV<INT>`, son membre `SP : K` avec K = INT), carré I, sans faute. `Niveau * 2` est un calcul (fx) ; `12.5` reste une constante (C) ; une formule qui ne se lit pas (`Niveau +`) est toujours en rouge, avec la raison.
- **La même règle pour toutes les cases qui n'attendent qu'une formule** : la condition d'une alarme (`Pression > 10`), la condition d'un état d'un objet multi-états (`Niveau > 80`), le Min et le Max d'une saisie au clavier (`Consigne_Min`). Ce qui n'est pas une constante du type attendu se lit en formule ; `TRUE` et `12.5` restent des constantes.
- **Le carré répond au clic dans les onglets Contenu, Actions et Raccourcis**, comme dans l'inspecteur : la liste dit ce que vaut la case et ce que veut dire chaque carré ; **Ouvrir le sélecteur…** ouvre le sélecteur des constantes, des variables IHM et des variables système ; le choix s'écrit dans la case, et Ctrl+Z le reprend.
- **Types IHM, colonne Mots** : `SPPV<K>` compte ses mots « selon K », `T_Paire<A, B>` « selon A, B ».

## 3. Les essais

77 suites sur 77. L'essai nouveau de cette version :
- `hmieditor` (8070 contrôles) : `plumeCarres11223` :
  - la structure `SPPV<K>` (SP, PV : K), la variable `SPPV_TELUNA : ARRAY[0..8] OF SPPV<INT>` et une courbe : la plume `SPPV_TELUNA[0].SP` est sans faute, carré I ; `Niveau * 2` carré fx ; `Niveau +` en faute ; `12.5` carré C ;
  - le carré du Contenu : sans sélecteur il ne répond pas ; avec, ceux du Contenu et des Actions répondent ; un clic ouvre la liste, Ouvrir le sélecteur… demande la case de la plume (sa formule, un nombre attendu), le choix (`Niveau`) s'écrit dans la plume ; une grille inconnue n'écrit rien ;
  - Types IHM : `SPPV<K>` « selon K » ;
  - la règle seule : une condition `Niveau > 10` se lit en formule, `TRUE` reste une constante.
- Mis à jour : les notes (381) et les versions (56) dans `hmieditor`. `hmi` : 5945 contrôles, inchangé.

## 4. Les captures

Prises avec l'exe Windows livré, lancé sous Wine, sur une copie du projet de démonstration Armoire_Gaz à laquelle la session ajoute ta structure `SPPV<K>` (membres `SP : K`, `PV : K`) et la variable `SPPV_TELUNA : ARRAY[0..8] OF SPPV<INT>` ; la première plume de la courbe `Courbe_Pressions` (Vue_Supervision) vaut `SPPV_TELUNA[0].SP` :
- `1223_01_accueil.png` : l'accueil d'IHM Studio, la version 1.12.23.
- `1223_02_plume.png` : Vue_Supervision › Courbe_Pressions › onglet Contenu, la plume 1 : son Expression `SPPV_TELUNA[0].SP` est sans faute, avec le carré I (variable IHM).
- `1223_03_carre.png` : un clic sur ce carré : la liste. En tête, ce que vaut la case (« Variable IHM : SPPV_TELUNA[0].SP · INT · Initiale 0 · Les boucles TELUNA ») ; Variable IHM est cochée (« ✓ ici ») ; en bas, Ouvrir le sélecteur….
- `1223_04_selecteur.png` : Ouvrir le sélecteur… : « Choisir la valeur de « Expression » », attendu : nombre ; `SPPV_TELUNA[0].SP` y est un INT de l'IHM (« La consigne »), « Convient à nombre ».

La colonne Mots de Types IHM, trop étroite sur la capture, coupait « selon K » : l'essai le vérifie, pas une capture.

## 5. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.12.23.exe` : l'installateur, qui pose les deux applications ;
- `API-Studio-1.12.23-portable.zip` et `IHM-Studio-1.12.23-portable.zip` : chaque application, sans installation ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.12.23.exe`. Il met à jour la version installée (la 1.12.22 ou une plus ancienne) en place : même identité d'installation, même dossier, données reprises sans être déplacées. Les projets s'ouvrent tels quels : le format des fichiers ne change pas.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.12.23.exe` (31,6 Mo, 33 089 958 octets) :
`7489A3D31D468615F2476117EEE2F3BF8306DCB8666FB4CDA129E9063B91C9CE`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `API-Studio-1.12.23` ou `IHM-Studio-1.12.23`. Décompresse, puis lance l'exe.
- `API-Studio-1.12.23-portable.zip` (23,6 Mo) :
  `F31D2B71C3EA54250450EA814CC47D966FB6793404CB851681CADF4D9761F094`
- `IHM-Studio-1.12.23-portable.zip` (23,6 Mo) :
  `0725A5BCA7B114910CF32099A892C83D1B6259D15A08949CFF5126CB43E2F851`

## 6. Les vérifications sous Windows

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `API Studio 1.12.23` et `IHM Studio 1.12.23` ;
- ils importent exactement les mêmes DLL que ceux de la 1.12.22 (ADVAPI32, IPHLPAPI, KERNEL32, SDL3, SHELL32, USER32, WS2_32, msvcrt, ole32) ;
- `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- une session scriptée sur le projet de démonstration : les captures ci-dessus. Le carré y est cliqué comme à la souris, dans l'onglet Contenu.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.22 :
- avant, la 1.12.22 était installée (`IHM Studio 1.12.22`) ;
- « Installation process succeeded ». Il a repris l'installation en place : même dossier, journal de désinstallation complété ;
- le dossier d'installation a `API-Studio.exe`, `IHM-Studio.exe` et le désinstalleur ; le menu Démarrer garde son dossier **API IHM Studio** (API Studio, IHM Studio, Maintenance) ;
- les deux exe installés sont identiques à ceux qui ont été testés. Installés, ils répondent `API Studio 1.12.23` et `IHM Studio 1.12.23`, et l'API installée analyse `MAST.XPG` comme avant.

**Les zips portables** ont chacun 151 fichiers, dont l'exe testé.

**Le projet Visual Studio** a les mêmes 469 fichiers .cpp et 424 en-têtes que CMake (`outils/verifier_vcxproj.py`) : aucun fichier nouveau cette fois.

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici).

## 7. La suite

- Essayer sur un vrai Windows : la plume de ta courbe, un clic sur un carré du Contenu.
- Vu en écrivant l'essai : un nom inconnu tapé dans une case (`Nivau` pour `Niveau`) n'est signalé qu'à Générer, pas sous la case, tant qu'aucun projet d'automate n'est ouvert. C'est une autre règle (un nom inconnu peut venir de l'automate) ; à revoir pour IHM Studio, qui n'en a plus.
- La maquette du Réglage PID attend ton avis avant d'être codée.
