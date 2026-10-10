# XPGAnalyser 1.12.3 : Projet › Disposition, les états en table, plusieurs objets choisis, la simulation et le poste

Livrée le 10/10/2026 au soir. Elle termine la liste du 10/10 : ce que la 1.12.2 annonçait pour la 1.12.3 est là, sans reste.

Elle répond à ces demandes du 10/10 :

> « ajouter dans le menu déroulant du haut, là où y'a Enregistrer, “Disposition” : un menu de configuration où je choisis quels sous-onglets afficher, quand, comment, où ; en persisté »

> « rendre plus pratique l'éditeur des formules multi-états (texte multi-états, voyant multi-états…) »

> « pouvoir sélectionner plusieurs objets dans l'explorateur d'objets et modifier les paramètres communs »

> « règle la partie simulation et poste d'exploitation » — et « y'avait un bug dans les courbes, j'étais obligé d'aller au moins une fois sur la page pour que ça commence à enregistrer »

Tout se passe dans **XPGAnalyser IHM**. XPGAnalyser API gagne seulement la fenêtre Disposition (ses panneaux et le panneau du bas).

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.3.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.3-portable.zip` et `XPGAnalyser-IHM-1.12.3-portable.zip` : chaque application, sans installation ;
- ce document ;
- les captures `1123_ihm_*`, prises sous Wine avec l'exe livré.

## 1. Ce qui change pour vous

| | Avant (1.12.2) | Maintenant (1.12.3) |
|---|---|---|
| Choisir ce que montre la fenêtre | Affichage › Panneaux à afficher… : des cases pour les grands panneaux | **Projet › Disposition…** (Ctrl+Maj+K) : les panneaux, les sous-onglets et les pages du centre ; quand, où, au départ ; gardé |
| Les états d'un voyant ou d'un texte multi-états | une ligne `0 = Arrêt \| gris; 1 = …` à écrire juste | **Contenu › États** : une ligne par état, selon la valeur ou selon des conditions |
| Plusieurs objets choisis | l'inspecteur du premier | leurs **propriétés communes** ; une saisie part sur tous, en une annulation |
| Une courbe | n'enregistrait qu'une fois sa vue affichée | enregistre **dès le démarrage** de la simulation |
| Une écriture sur une variable forcée | disparaissait sans rien dire | la Console le dit, l'action aussi |
| Deux alarmes au même moment | un son coupait l'autre | les deux sons **se mélangent** |
| Le poste redémarre | revient au premier onglet, tout déplié | garde l'onglet, les panneaux repliés, le défilement |

## 2. Projet › Disposition…

Dans le menu du projet (celui d'Enregistrer), **Disposition…** (Ctrl+Maj+K). **Affichage › Panneaux à afficher…** ouvre la même fenêtre.

Elle liste, groupe par groupe :
- **la fenêtre** : l'explorateur du projet, les documents ouverts, le panneau du bas (Sorties, Console, Diagnostics), la bande d'état, les lignes alternées ;
- **les sous-onglets** du panneau du bas, de l'inspecteur de l'éditeur de vue (Propriétés, Actions, Contenu, Raccourcis), de la simulation (Expressions, Variables IHM, Performances, Esclaves simulés, Popups), de la programmation générale et des équipements ;
- **les pages du centre** : les vues, la simulation, la programmation générale, le poste d'exploitation.

Pour chacun :
- **quand** : toujours, en édition, en simulation, ou caché. Un élément « en simulation » apparaît au démarrage de la simulation et repart à l'arrêt. Une page : à la demande, à l'ouverture du projet, ou au démarrage de la simulation ;
- **où** : l'explorateur à gauche ou à droite ; le panneau du bas sous l'éditeur ou à sa droite ; une page dans un onglet, côte à côte ou dans une fenêtre détachée ;
- **au départ** : le panneau du bas ouvert ou replié (Ctrl+J le rouvre) ; le sous-onglet choisi à l'ouverture de chaque page.

En haut, des **dispositions toutes faites** : Par défaut, Dessin des vues, Mise au point, Écran large, et **Ma disposition** (« Garder comme Ma disposition »). En bas, ce que la disposition cache et ce qui n'est pas encore appliqué, puis **Rétablir la disposition d'origine**, Annuler, **Appliquer** (on voit tout de suite) et OK.

Tout se garde dans les réglages de l'édition (les clés `disposition.*` ; API et IHM ont chacune les leurs), relus au lancement. « Rétablir » les efface. La première fois, la fenêtre reprend ce que montraient les cases d'Affichage › Panneaux à afficher.

Le raccourci de la maquette, Ctrl+Maj+D, était déjà « Dupliquer tel quel » dans l'éditeur de vue : la fenêtre a pris **Ctrl+Maj+K**.

## 3. Les états en table

Un **voyant multi-états** ou un **texte multi-états** choisi : l'onglet **Contenu › États**. Une ligne par état : quand (la valeur, ou une condition), le texte, la couleur (à la palette), clignote. Ajouter, Retirer, ↑ ↓ pour l'ordre, et la ligne « sinon ». Le bouton **…** de la ligne États de l'inspecteur y mène.

L'état se choisit **selon la valeur** (`0`, `1`, `10..20`, `'Auto'`) ou **selon des conditions** (`Defaut`, `Marche AND NOT Defaut`) : la première vraie gagne. Pour les conditions, la Valeur de l'objet est écrite pour elles (`(Defaut) ? 1 : (Marche) ? 2 : 0`), et relue à la réouverture. Un « ; » ou un « | » dans un texte est refusé tout de suite (ils séparent les états).

## 4. Plusieurs objets choisis

Choisissez plusieurs objets (un cadre dans la vue, Ctrl+clic, **Ctrl+A** dans l'explorateur d'objets, **Maj+clic** depuis le dernier objet cliqué). L'inspecteur montre ce qu'ils ont **en commun** :
- le Type dit leurs genres ; une valeur qui diffère (le calque, Verrouillé aussi) se lit « (plusieurs valeurs) » ; une case à cocher qui diffère est en « – » ; des formules (fx) différentes se lisent « (plusieurs formules) » ;
- une saisie part sur **tous**, en une seule annulation (Ctrl+Z les remet tous) ;
- l'onglet **Actions** liste les objets choisis et leurs actions : une action est à un objet, un clic sur une ligne le choisit seul.

## 5. Les courbes enregistrent dès le démarrage

Une courbe en temps réel ne s'échantillonnait que si sa vue était affichée : une vue jamais vue n'avait rien, une vue quittée montrait un trou au retour. Maintenant, toutes les vues où l'on peut naviguer, les popups sans paramètre et ce qui est ouvert s'échantillonnent dès le démarrage (les graphiques XY, chronogrammes et histogrammes aussi). Une plume qui cite un paramètre de sa vue (`Cuve.Niveau`) s'enregistre, et une popup à paramètres garde **une série par jeu d'arguments** : Cuves[2] et Cuves[3] ne se mélangent plus.

## 6. La simulation et le poste d'exploitation

- **Une écriture sur une variable forcée** ne disparaît plus sans rien dire : la Console l'écrit une fois par variable (« Niveau est forcée : écriture ignorée (45) »), et l'action dit « ignorée (forcée) ». Après Défaire le forçage, elle se redit.
- **La vraie case dans la Console** : `Cuves[2].Consigne := 75 (Cuves[Idx].Consigne)`, pour Affecter, Mettre à 1 / à 0, Basculer, Incrémenter et les commandes.
- **Les sons se mélangent** : deux alarmes qui apparaissent ensemble s'entendent ensemble (un même son ne part qu'une fois) ; la répétition reprend le son du groupe de l'alarme la plus grave ; **Silence** et l'arrêt coupent ce qui joue.
- **Le poste garde** l'onglet choisi, les panneaux repliés et le défilement d'un lancement à l'autre (une option du poste, cochée), y compris dans un symbole, où ces clics ne prenaient pas.
- **Les popups de symbole** ne fabriquent plus d'alarmes d'objets dont la condition cite un paramètre du symbole (« condition illisible » au démarrage).
- **La mémoire de la marche** (le journal, les événements, les alarmes closes) suit le réglage des Historiques (Entrées par liste, 2 000 par défaut, 100 au moins) au lieu de 500 lignes fixes ; `SYS.ErrorCount` compte toutes les erreurs depuis le démarrage.
- **L'audit des scripts** : « Tracer aussi les écritures des scripts » (Configuration › Historiques) garde au journal d'audit une valeur changée par un script ou une fonction qui tourne seul, avec le script pour source. Sans elle, rien ne change.

## Tester vous-même

1. Ouvrez un projet dans **XPGAnalyser IHM**. **Projet › Disposition…** : choisissez « Écran large », puis **Appliquer** : le panneau du bas passe à droite de l'éditeur. **Rétablir la disposition d'origine** le remet dessous.
2. Dans la fenêtre Disposition, mettez **Simulation · IHM › Performances** sur « En simulation », OK. Lancez la simulation : l'onglet Performances apparaît ; arrêtez-la : il repart.
3. Posez un **voyant multi-états**. **Contenu › États** : passez sur « selon des conditions », écrivez `Defaut` puis `Marche` : la Valeur de l'objet devient `(Defaut) ? 1 : (Marche) ? 2 : 0`.
4. Choisissez trois voyants (un cadre). Dans l'inspecteur, **Forme** : « carré » : les trois changent ; Ctrl+Z les remet.
5. Une vue avec une courbe : lancez la simulation **sans aller sur cette vue**, attendez, puis allez-y : la courbe a déjà ses points.
6. Forcez une variable, cliquez un bouton qui l'écrit : la Console le dit.

## Installer

1. Lancez `XPGAnalyser-Setup-1.12.3.exe`. Il met à jour la version installée : même identité d'installation, données reprises sans être déplacées. Il pose les deux applications et leurs deux raccourcis. Vos projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.12.3.exe` (28,2 Mo, 29 549 470 octets) :
`CB58BEF655E52FB62B2AC5E0B90CCA0E4BC0C1E2F209D0F01F430C1AF66F44EA`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `XPGAnalyser-API-1.12.3` ou `XPGAnalyser-IHM-1.12.3`. Décompressez, puis lancez l'exe.
- `XPGAnalyser-API-1.12.3-portable.zip` (21,1 Mo) :
  `77FBE3D033BDF5E6E1F143F51AEE54480A735F12B502CEAA23D5B20BE4738107`
- `XPGAnalyser-IHM-1.12.3-portable.zip` (21,1 Mo) :
  `2C9DF477A06C53A6082A4B76AAD0E6EFAD173E217B99DFE6CAD91B5B73989DA8`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe, 70 sur 70. Elle comprend les 6 785 contrôles de `hmieditor`, les 4 851 de `hmi`, les 1 842 de `hminatives` et les 258 de `hmireperes`. Chaque nouveauté a ses essais :
- la fenêtre Disposition (`disposition1123` : le modèle, l'écriture dans les réglages, les dispositions toutes faites, un sous-onglet caché puis rendu, « en simulation ») ;
- les états en table (`etats1123` : selon la valeur, selon des conditions, la Valeur écrite puis relue, « ; » refusé) ;
- plusieurs objets choisis (`communes1123` : « (plusieurs valeurs) », la case en « – », « (plusieurs formules) », le Type qui dit les genres, une saisie sur tous en une annulation, l'onglet Actions, Ctrl+A, Maj+clic) ;
- les courbes dès le démarrage (`courbesDemarrage1123`) et une série par jeu d'arguments (`courbesParArguments1123`) ;
- l'écriture sur une variable forcée (`ecritureForcee1123`), la vraie case dans la Console (`vraieCase1123`), la mémoire de la marche (`memoireJournal1123`), l'audit des scripts (`auditScripts1123`) ;
- les sons mélangés, Silence et l'arrêt (`sonsMelanges1123`, `voixDesSons1123`) ;
- le poste qui garde ses onglets, ses panneaux repliés et son défilement, symboles compris (`etatPoste1123`) ;
- les popups de symbole sans alarme illisible (`popupSymboleAlarmes1123`).

Les essais des popups de symbole et des clics dans un symbole échouent sans leur correction : ils ont été lancés avant elle.

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `XPGAnalyser API 1.12.3` et `XPGAnalyser IHM 1.12.3` ;
- `XpgAnalyzer-API.exe --cli MAST.XPG` analyse le projet d'essai (891 variables, 29 POU), comme avant ;
- une session sous Wine avec l'exe livré, sur Armoire_Gaz et sa vue Vue_Listes : Projet › Disposition… s'ouvre (capture `1123_ihm_01`) ; dans l'explorateur d'objets, un clic sur Liste_Recettes puis Maj+clic sur Table_Lots choisit les quatre objets, l'inspecteur montre leurs propriétés communes (`02`) et l'onglet Actions les liste (`03`) ; la simulation démarre (build réussi, 360 éléments générés, 0 erreur) et montre Vue_Listes en marche (`04`). Chaque ligne de la session a réussi.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.2 :
- « Installation process succeeded » ; il a repris l'installation de la 1.12.2 (même dossier, son journal de désinstallation complété) ;
- avant comme après : `XpgAnalyzer-API.exe`, `XpgAnalyzer-IHM.exe`, et les raccourcis « XPGAnalyser API » et « XPGAnalyser IHM » ;
- les deux exe installés sont identiques à ceux qui ont été testés ; installés, ils répondent `XPGAnalyser API 1.12.3` et `XPGAnalyser IHM 1.12.3`.

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici). La section « Tester vous-même » donne le parcours à refaire chez vous.

## La suite

- `DATE`, `TOD`, `DT`, `CHAR` et `WSTRING` comme variables IHM, avec leur place Modbus ;
- la source du guide Word, à remettre à jour depuis la 1.11.2.
