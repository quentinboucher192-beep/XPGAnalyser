# XPGAnalyser 1.11.15 — le cycle de la simulation et la rémanence de simulation (lot 3)

Livrée le 08/10/2026 dans la nuit. C'est le **lot 3** de votre spécification du 08/10. Il couvre le comportement en cas de modification pendant la simulation (§ 11), la rémanence de simulation (§ 12), Démarrer, Arrêter, Redémarrer et Générer et redémarrer (§ 13) et la séparation des deux rémanences, côté simulation (§ 15 A). Les lots 1 (1.11.13, la génération incrémentale) et 2 (1.11.14, le panneau du bas et `IHM_LOG`) restent tels quels.

| Votre demande | Où dans la 1.11.15 |
|---|---|
| L'option globale « Conserver les données de simulation entre les démarrages » | le bouton **Rémanence** de la barre de l'onglet Simulation · IHM ; le réglage est retenu (§ 3) |
| Sauvegarder à l'arrêt, restaurer au démarrage suivant, garder lors d'une génération, d'une compilation, d'un arrêt sur modification, de Générer et redémarrer | l'instantané de `.xpg/simulation/` (§ 3) |
| Option désactivée : l'état précédent ignoré, les valeurs initiales | (§ 3) |
| Restaurer par identifiant stable, convertir les types compatibles, ignorer les variables supprimées, initialiser les nouvelles, avertir d'un type incompatible, jamais dans une mauvaise variable | (§ 3) |
| Démarrer, Arrêter, Redémarrer, Générer et redémarrer, distincts | la barre de l'onglet, la carte au milieu de la vue (§ 2) |
| La confirmation avant un redémarrage destructif, « Ne plus demander pour cette session » | (§ 2) |
| Une modification pendant la simulation : marquée, invalidée, arrêt propre, état sauvegardé, journaux gardés, le message, les actions proposées | (§ 4) |
| Ne pas arrêter pour une modification sans effet sur l'exécution ; distinguer exécution, génération, compilation, documentation | (§ 4) |
| La rémanence de simulation séparée de celle d'exploitation | son propre fichier, dans l'espace du développement (§ 3) ; la rémanence d'exploitation vient au lot 4 |

Fichiers livrés : `XPGAnalyser-Setup-1.11.15.exe` (l'installateur), ce document et les captures `11115_*` prises sous Wine avec l'exe livré.

Le lot suivant, **4** : la rémanence d'exploitation par variable (la colonne Rémanente), la robustesse restante, les tests restants de votre § 19 et le rapport final en 9 parties.

## 1. Ce qui change pour vous

- La barre de l'onglet **Simulation · IHM** a deux boutons de plus :
  - **Générer et redémarrer** : les données de simulation sont gardées, seul ce qui a changé est refait, la simulation repart avec ses données ;
  - **Rémanence** : l'option « Conserver les données de simulation entre les démarrages ».
- **Redémarrer l'IHM** devient un vrai redémarrage propre : les données de simulation et l'état gardé sont effacés, après une question.
- **Arrêter l'IHM** garde les données quand la rémanence est cochée.
- **Une modification du projet IHM pendant la marche arrête la simulation proprement.** La carte au milieu de la vue dit pourquoi et ce qui a changé. Elle propose : Générer et redémarrer, Régénérer et redémarrer, Compiler, Annuler le redémarrage.
- Les **Sorties** disent tout cela : données sauvegardées, restaurées, effacées, l'arrêt et sa cause. La **Console** aussi : la restauration et ses avertissements, l'arrêt sur modification.

## 2. Démarrer, Arrêter, Redémarrer, Générer et redémarrer

| Commande | Ce qu'elle fait | Les données de simulation |
|---|---|---|
| **Démarrer l'IHM** | build de ce qui a changé (lot 1), puis la simulation | rendues si la rémanence est cochée |
| **Arrêter l'IHM** | arrêt propre | gardées si la rémanence est cochée ; rien n'est effacé |
| **Redémarrer l'IHM** | arrêt, état gardé effacé, variables à leur valeur initiale, esclaves simulés à leur départ, démarrage propre | effacées |
| **Générer et redémarrer** | données gardées, build de ce qui a changé, la simulation repart | gardées et rendues : pas de remise à zéro |

**La question avant Redémarrer** : « Le redémarrage va réinitialiser les données de simulation et supprimer l'état rémanent courant. Continuer ? ». La case « Ne plus demander pour cette session » la fait taire jusqu'à la fermeture de l'application. Échap répond non. Entrée ne répond pas d'office, puisque la question efface des données. Sans rien à perdre (la simulation arrêtée, aucun état gardé), elle n'est pas posée.

Les données rendues le sont **après les valeurs initiales et avant les scripts de Démarrage**, comme la reprise à chaud d'un automate. Un script de Démarrage qui remet une variable à zéro (`Compteur_Clics := 0;` dans votre Init) la remet donc à zéro : c'est lui qui décide.

## 3. La rémanence de simulation

**L'option** : le bouton **Rémanence** de la barre. C'est un réglage de l'application, retenu d'une ouverture à l'autre, que l'on change quand on veut.

**Ce qui est gardé** :
- les variables IHM, case par case : une structure membre par membre, un tableau case par case, une énumération par son nombre ;
- la mémoire des esclaves simulés (leurs registres et leurs bits), par l'identifiant de leur équipement.

Une variable IHM **liée à un équipement** n'est pas gardée à part : sa valeur vient de l'équipement, donc de la mémoire de l'esclave simulé, gardée avec lui.

**Quand** :
- à **Arrêter** ;
- à **Générer et redémarrer** ;
- à l'**arrêt sur modification** ;
- en **quittant l'application** pendant la marche, pour les variables seulement : l'application qui se ferme ne garantit plus les esclaves simulés.

**Où** : `<projet>/.xpg/simulation/remanence.txt`. Ce dossier est hors des versions du projet (lot 1), et c'est l'espace du développement : jamais le stockage du poste d'exploitation, qui aura le sien au lot 4. Le fichier est écrit d'un bloc, avec la règle des écritures atomiques du lot 1 :
1. un fichier temporaire ;
2. relu ;
3. l'ancien gardé en `.bak` ;
4. puis le remplacement.

Sa dernière ligne compte celles qui précèdent : un fichier coupé est refusé, et la copie `.bak` est reprise.

**Le retour, compatible avec les modifications du projet** :

| Le projet a changé | Ce que fait le retour |
|---|---|
| rien | toutes les valeurs reviennent |
| une variable renommée, déplacée dans la liste | sa valeur la suit : c'est son identifiant qui compte, pas son nom ni son rang |
| une variable supprimée | sa valeur est ignorée (comptée) |
| une nouvelle variable, un nouveau membre de structure | sa valeur initiale |
| un membre de structure retiré, un tableau raccourci | les cases disparues sont ignorées |
| un type compatible : un entier vers un entier plus large (INT vers DINT), ou plus étroit si la valeur y tient, un entier vers un REAL | la valeur convertie (comptée, dite dans les Sorties) |
| un type incompatible : un REAL devenu BOOL, une STRING devenue INT, une valeur qui ne tient pas dans le nouvel entier | la valeur initiale, et un avertissement qui nomme la variable, l'ancien type, le nouveau et l'ancienne valeur |
| une variable devenue une structure, ou l'inverse | toute la variable reprend sa valeur initiale (avertissement) |
| une énumération qui a perdu la valeur gardée | la valeur initiale (avertissement) |

Les Sorties disent ce que le retour a fait, par exemple « Données de simulation restaurées (gardées le 2026-10-08 22:10:05) : 85 valeurs rendues · 2 esclaves simulés rendus ». Chaque avertissement suit sur sa ligne, et aussi dans la Console.

**L'option décochée** : le démarrage suivant repart des valeurs initiales, et l'état gardé est ignoré (il n'est pas effacé : Redémarrer le fait).

**Dans la fenêtre du build**, la phase G (« Restauration des données rémanentes ») dit ce qui sera rendu au démarrage, par exemple « données du 2026-10-08 22:10:05 : rendues au démarrage ». Si rien n'est gardé, elle dit « rien à restaurer » ; si l'option est décochée, « rémanence désactivée ».

**Le format de l'instantané** : du texte, une ligne par donnée, au format des fichiers du projet IHM (`mot cle=valeur`). Voici le début de celui que la session du § 5 a écrit :

```
xpg-simulation-remanence 1
# XPGAnalyser : les donnees de la simulation IHM (remanence de simulation). Ecrit par l'application.
prise date="2026-10-08 20:49:37" session=4
case variable=64 nom="Compteur_Clics" declare="INT" chemin="" type=INT valeur="0"
case variable=67 nom="Secondes_IHM" declare="DINT" chemin="" type=DINT valeur="3"
case variable=618 nom="Fours" declare="ARRAY[1..4] OF T_Four" chemin="[1].Consigne" type=REAL valeur="850"
case variable=618 nom="Fours" declare="ARRAY[1..4] OF T_Four" chemin="[1].Marche" type=BOOL valeur="FALSE"
…
jumeau equipement=584 nom="Variateur ATV320" table=4x debut=8504 valeurs="32"
jumeau equipement=589 nom="Balance B" table=4x debut=200 valeurs="65282,16576"
jumeau equipement=754 nom="Esclave virtuel 1" table=4x debut=0 valeurs=""
fin cases=204 jumeaux=7
```

- `prise` : quand, et la session ;
- `case` : l'identifiant stable de la variable (`variable`), son nom et son type déclaré à la prise (pour le dire), le chemin de la case dans la variable (`chemin` : vide pour une variable simple, `.Temperature`, `[3]`), le type de la valeur et la valeur ;
- `jumeau` : un esclave simulé (l'identifiant de son équipement), une plage de sa mémoire : la table, le début, les valeurs. Une mémoire toute à zéro a une ligne vide : au retour, la mémoire est remise à zéro ;
- `fin` : le compte des lignes `case` et `jumeau`. Sans elle, ou avec un autre compte, le fichier est refusé.

## 4. L'arrêt sur modification

Quand la simulation démarre, chaque élément du projet a son empreinte : son contenu, sa configuration, son interface (celles du build du lot 1). Après chaque modification, l'analyse du lot 1 repart, 300 ms plus tard. Si une empreinte a changé, ou si un élément est apparu ou a disparu, alors :
1. l'élément passe à « ● Modifié » dans l'arbre (lot 1), et ce qui en dépend est à refaire ;
2. la simulation s'arrête proprement ;
3. les données sont gardées si la rémanence est cochée ;
4. les journaux (Console, Sorties) et les diagnostics restent ;
5. la carte au milieu de la vue dit : **« La simulation a été arrêtée car le projet IHM a été modifié. »** Puis ce qui a changé et ce que cela demande, par exemple « 1 élément modifié : 1 à compiler · Horloge (Script général, à compiler) » ;
6. elle propose **Générer et redémarrer**, **Régénérer et redémarrer**, **Compiler** et **Annuler le redémarrage**. Les Sorties le disent aussi, avec le chemin de chaque élément.

| La modification | La simulation |
|---|---|
| du code (un script, une fonction, une expression, une action) | s'arrête : « à compiler » |
| une vue, un style, une ressource, une alarme, une variable… | s'arrête : « à générer » |
| une description, un dossier, la grille ou les guides de l'éditeur | continue ; les Sorties : « Modification sans effet sur l'exécution… : la simulation continue » |
| ce que la simulation écrit elle-même dans le projet : une recette enregistrée, un utilisateur, une ressource, un forçage ou une animation de jumeau | continue : ce sont ses données, pas du code |
| sélectionner, déplacer une fenêtre, changer d'onglet | rien ne change dans le projet : rien ne se passe |

**Les quatre boutons** :
- **Générer et redémarrer** : seul ce qui a changé est refait, puis la simulation repart avec ses données ;
- **Régénérer et redémarrer** : tout est refait, puis la simulation repart avec ses données ;
- **Compiler** : compile ce qui doit l'être, sans redémarrer ; la carte reste ;
- **Annuler le redémarrage** : la simulation reste arrêtée, la carte ordinaire revient (Démarrer l'IHM la relance).

## 5. Le parcours vérifié sous Wine

Session `tools/sessions/session-11115-cycle.txt`, sur une copie d'Armoire_Gaz corrigée comme pour la 1.11.13, sans cache, avec l'exe livré. `Secondes_IHM` est compté par votre script Horloge (une seconde, un de plus) ; votre script Init ne le remet pas à zéro.

| Étape | Ce qui s'est passé | Capture |
|---|---|---|
| 1. Simulation › IHM, puis la case Rémanence | le build complet (308 générés, 225 compilés), la simulation démarre (session 1) ; les Sorties : « Rémanence de simulation activée » | `11115_01` |
| 2. 5 s de marche, puis Arrêter l'IHM | `Secondes_IHM` = 7 ; « Données de simulation sauvegardées (bouton Arrêter l'IHM) : 204 valeurs, 5 esclaves simulés » | `11115_02` |
| 3. Démarrer l'IHM | « Projet à jour », session 2 ; « Données de simulation restaurées (gardées le 2026-10-08 20:49:28) : 204 valeurs rendues · 5 esclaves simulés rendus » ; `Secondes_IHM` = 7 | `11115_03` |
| 4. Redémarrer l'IHM | la question ; puis Redémarrer : « État rémanent de simulation supprimé », les valeurs initiales (session 3) ; `Secondes_IHM` = 0 | `11115_04`, `11115_05` |
| 5. 4 s de marche, puis Générer et redémarrer | `Secondes_IHM` = 3 avant, 3 après : sauvegardées, rendues, pas de remise à zéro (session 4) | `11115_06` |
| 6. Une ligne `IHM_LOG(DEBUG, 'tic {Secondes_IHM}');` ajoutée au script Horloge pendant la marche | 0,3 s plus tard, l'arrêt ; la carte : « La simulation a été arrêtée car le projet IHM a été modifié. 1 élément modifié : 1 à compiler · Horloge (Script général, à compiler). Les données de simulation sont gardées ; les journaux et les diagnostics restent. », et ses quatre boutons ; la Console : l'avertissement | `11115_07`, `11115_08` |
| 7. Générer et redémarrer, sur la carte | seul Horloge est refait (1 généré, 1 compilé, 307 réutilisés), session 5 ; `Secondes_IHM` = 3, rendu | `11115_09` |
| 8. Rémanence décochée, Arrêter, Démarrer | « Rémanence de simulation désactivée… » ; la session 6 démarre sans rien rendre ; `Secondes_IHM` = 0 | `11115_10` |

Dès la deuxième capture, le panneau du bas est filtré sur « simulation » (sa recherche) : on n'y voit que les lignes de la simulation.

L'instantané écrit pendant cette session est celui du § 3 (204 cases, 7 lignes de jumeaux pour 5 esclaves simulés).

## 6. Fichiers créés et modifiés

**Créés :**
- `src/hmi/HmiSimData.hpp` et `.cpp` : la rémanence de simulation (l'instantané, son format, la prise des variables, le retour par identifiant stable, les conversions) ;
- `tests/hmi_simdata_test.cpp` : 47 contrôles ;
- `tools/sessions/session-11115-cycle.txt` : la session Wine du § 5 ;
- ce document.

**Modifiés :**
- `src/hmi/HmiRuntime` : la prise des variables IHM (`captureData`), leur retour au démarrage avant les scripts de Démarrage (`setStartData`, `lastRestore`) ;
- `src/app/hmi/HmiSimulation` : les commandes (Redémarrer, Générer et redémarrer, l'option Rémanence), la sauvegarde et le retour des données, l'arrêt sur modification et sa carte à quatre boutons ;
- `src/hmi/HmiPipeline` : la phase G d'un Démarrer dit ce que la rémanence rendra (ou « rémanence désactivée ») ;
- `src/app/hmi/HmiBuildPanes` : la question avant Redémarrer ;
- `src/app/screens/HmiBuildWorkspace.cpp`, `HmiWorkspace.cpp`, `HmiRuntimeDialogs.cpp` et `Screens.hpp` : les empreintes au démarrage, la comparaison après chaque analyse, ce qui vient de la simulation elle-même, la question, Régénérer et redémarrer, Compiler ;
- `src/app/ScriptRunner.cpp` : les commandes de session `ihm-remanence`, `ihm-sim-etat`, `ihm-variable` et `ihm-carte` ;
- `tests/hmi_editor_test.cpp` : le cycle dans le volet (§ 7) ;
- `tests/hmi_build_test.cpp` : la phase G (§ 7) ;
- la version et ses fichiers : `src/core/Version.hpp`, `src/hmi/HmiPackage.hpp`, `src/help/ReleaseNotes.cpp`, `src/core/CodeStats.hpp`, `CMakeLists.txt`, `installateur/XPGAnalyser.iss`, `outils/config.ini`, `resources/windows/xpg_analyzer.rc` et `dist/SHA256SUMS.txt`.

## 7. Les tests ajoutés

`tests/hmi_simdata_test.cpp` (test CTest `hmisimdata`) : **47 contrôles, 0 échec**.

| Votre cas | Le test |
|---|---|
| l'instantané : chaque type revient à l'identique (BOOL, entiers, REAL avec tous ses chiffres, TIME, STRING avec guillemets et retours, membres, cases de tableau, mémoire des esclaves simulés) ; un fichier coupé, une ligne perdue, une valeur abîmée, un autre fichier : refusés | `format` |
| les conversions : seulement les types compatibles, rien de tronqué en cachette | `conversions` |
| restaurer par identifiant stable ; un renommage ; un nouvel ordre ; une variable supprimée ; une nouvelle ; un type compatible (INT vers DINT) ; un type incompatible (REAL vers BOOL, avec l'avertissement) ; un membre ajouté, un membre retiré ; une énumération qui perd une valeur ; un type simple devenu structure ; une variable liée à un équipement | `captureEtRetour` |
| le moteur : rendues avant les scripts de Démarrage, une seule fois, dites dans le journal | `moteur` |

`tests/hmi_editor_test.cpp`, `cycle1115` : le cycle dans le volet de la simulation, avec un hôte d'essai :
- Arrêter garde les données (le fichier, les Sorties) ;
- Démarrer les rend ;
- Redémarrer pose la question : Annuler ne change rien ; Redémarrer remet les valeurs initiales et supprime l'état gardé ;
- Générer et redémarrer garde les données ;
- l'arrêt sur modification : sa carte, ses quatre boutons (Compiler et Régénérer et redémarrer appellent le build de l'hôte, Annuler le redémarrage rend la carte ordinaire), les données gardées à cet arrêt qui reviennent ;
- l'option décochée : les valeurs initiales ;
- un instantané coupé : refusé, sa copie de secours reprise, jamais la valeur du fichier coupé ;
- la relance du poste d'exploitation et des didacticiels : les valeurs initiales, sans question, sans effacer l'état gardé.

`tests/hmi_build_test.cpp` : la phase G dit « données du … : rendues au démarrage » quand un instantané sera rendu, et « rémanence désactivée » quand l'option est décochée.

Les tests du lot 1 vérifient déjà qu'une description ne change aucune empreinte : c'est ce qui laisse tourner la simulation pour une modification de documentation.

La suite complète passe : **58 tests sur 58** (Linux, GCC 13).

## 8. Les limites de ce lot

- **En quittant l'application pendant la marche**, seules les variables IHM sont gardées. La mémoire des esclaves simulés l'est à Arrêter, à Générer et redémarrer et à l'arrêt sur modification.
- **Les variables locales des scripts** (les `VAR` gardées d'un script d'un appel à l'autre) ne sont pas dans l'instantané : seules les variables IHM le sont, avec la mémoire des esclaves simulés.
- **Le programme de l'automate simulé (l'API)** a son propre cycle (Démarrer l'API, Arrêter l'API) : ses variables ne sont pas dans la rémanence de simulation de l'IHM.
- **Ce que la simulation écrit elle-même dans le projet** (une recette, un utilisateur, un jumeau) ne l'arrête pas. Mais une modification du développeur faite dans la même fraction de seconde serait vue avec elle : elle arrêterait la simulation, comme il se doit.
- Rien n'a encore été lancé sur un **vrai Windows** : tout a été vérifié sous Wine.

## 9. Tester vous-même

1. Installez la 1.11.15, puis ouvrez une **copie** d'Armoire_Gaz (corrigée comme au § 6 de la livraison 1.11.13, ou avec vos corrections).
2. Ouvrez Simulation › IHM : la simulation démarre. Cochez **Rémanence** dans la barre de l'onglet.
3. Laissez tourner quelques secondes (le script Horloge compte `Secondes_IHM`), puis **Arrêter l'IHM**. Les Sorties : « Données de simulation sauvegardées… ».
4. **Démarrer l'IHM** : `Secondes_IHM` reprend où il en était. Les Sorties : « Données de simulation restaurées… ».
5. **Redémarrer l'IHM** : la question. Annuler : rien ne change. Redémarrer : `Secondes_IHM` repart de 0, l'état gardé est supprimé.
6. Laissez tourner, puis **Générer et redémarrer** : `Secondes_IHM` continue, rien n'est remis à zéro.
7. Pendant la marche, ajoutez une ligne au script Horloge : la simulation s'arrête 0,3 s plus tard, la carte dit pourquoi. **Générer et redémarrer** sur la carte : seul Horloge est refait, et `Secondes_IHM` revient.
8. Pendant la marche, changez la description d'un script : la simulation continue (les Sorties le disent).
9. Décochez Rémanence, Arrêter, Démarrer : les valeurs initiales.
10. Renommez une variable IHM ou changez son type (INT en DINT), puis Démarrer avec la rémanence cochée : la valeur suit la variable renommée, convertie. Un type incompatible (REAL en BOOL) : la valeur initiale, et un avertissement dans les Sorties et la Console.

## Installer

1. Lancez `XPGAnalyser-Setup-1.11.15.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.15.exe` (15,0 Mo, 15 693 974 octets) :
`99961AA2AF65563D8D377EC43974795098EA3684EE0A72C9AA518D95FFB0516F`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (58 sur 58), dont les 47 contrôles de `hmisimdata`, le cycle dans le volet (`hmieditor`) et la phase G (`hmibuild`).

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.15` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session du § 5 a été rejouée avec l'exe livré, sur une copie d'Armoire_Gaz sans cache. Ce sont les 10 captures `11115_*.png`.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé pour tous les comptes, par-dessus la 1.11.14 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.15`.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Le § 9 donne le parcours à refaire chez vous.

## La suite

- **Lot 4** : la rémanence d'exploitation par variable (la colonne Rémanente, son stockage à part, ses propriétés et ses commandes : réinitialiser, exporter, importer, l'emplacement, l'intégrité), la robustesse restante, les tests restants de votre § 19 et le rapport final en 9 parties.
