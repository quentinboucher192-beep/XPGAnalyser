# XPGAnalyser 1.12.6 : un rôle par propriété, les repères partout (lots B et C)

Livrée le 11/10/2026 au soir.

Elle répond à la demande du 11/10, après la 1.12.5 (le lot A) :

> « continue sur B, C stp »

Ce sont les **lots B et C** du plan de l'audit de la bibliothèque d'objets (`docs/AUDIT-BIBLIOTHEQUE-OBJETS.md`, § 2.1, 2.2, 2.4, 2.7, 2.8, 2.9) :
- **B, un rôle par propriété.** Chaque propriété de chaque objet a un rôle : un nombre, un booléen, un état, un texte, une durée, une date, une couleur, un nom, une formule, une liste. Ce rôle dit comment lire la valeur et comment l'écrire. Une seule table le donne au dessin, au moteur, à l'inspecteur et à Générer.
- **C, les repères partout.** Un seul découpeur de listes. Les noms, les listes et les états se lisent sans les `$` de leurs repères. Dupliquer lit chaque propriété dans le mode de son rôle et suit tous les indices.

Tout se passe dans **XPGAnalyser IHM**. XPGAnalyser API ne change pas, à part son numéro de version.

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.6.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.6-portable.zip` et `XPGAnalyser-IHM-1.12.6-portable.zip` : chaque application, sans installation ;
- ce document ;
- les captures `1126_ihm_*`, prises sous Wine avec l'exe livré.

## 1. Ce qui change pour vous

Le principe : **un objet lit la valeur elle-même, pas son texte**, et il écrit au type de sa variable. Avant, chaque résultat de formule passait par un texte que l'objet relisait comme il pouvait ; une valeur qui ne convenait pas devenait 0, sans un mot.

| | Avant (1.12.5) | Maintenant (1.12.6) |
|---|---|---|
| Jauge sur un `BOOL` vrai | 0 | 1 |
| Jauge sur un `TIME` | rien de lisible | des secondes (T#1.5s : 1,5) |
| Jauge sur une `STRING` `'abc'` ou sur une structure | 0, sans rien dire | hachures et `###`, la raison dans la simulation ; Générer le signale avant |
| Un `BOOL` dans un texte à trous | `TRUE` | `Vrai` |
| Un `TIME` dans un texte | `T#1500ms` | `1,5 s` |
| Une `DATE` dans un texte | `2026-10-09` | `09/10/2026` |
| Une énumération dans un texte | son numéro (1) | son texte (Automatique) |
| Une structure entière dans un texte | une erreur | ses membres |
| Multi-états sur une énumération | l'état trouvé par son numéro seulement | aussi par son nom (`Auto = …`) |
| Curseur 0..50000 sur un `INT`, au bout | −15536, replié en silence | 32767 ; la Console le dit |
| `40000` tapé dans un champ lié à un `INT` | −25536 | refusé : hors bornes |
| `Auto` tapé dans un champ lié à une énumération | 0 écrit | sa valeur (1) |
| `1 h 30` tapé dans un champ lié à un `TIME` | refusé | 90 minutes |
| Affecter `'Manuel'` à une énumération | rien d'utile | Manu (le texte d'une valeur est reconnu) |
| Interrupteur lié à une structure | « variable inconnue » | refusé, avec la vraie raison et ses membres |
| Barre de navigation dont les vues sont une formule | le clic menait à la vue fixe | à la vue calculée |
| Un clic sur un onglet piloté par une formule | la formule était effacée | le clic vaut à côté ; la formule reprend la main quand sa valeur change |
| Vue `$Vue_B$` dans une barre de navigation | « vue introuvable » | trouvée, au clic comme dans Générer |
| Positions d'un sélecteur, libellés d'une barre repérés | affichés avec leurs `$` | sans |
| Liste `'a;b';c` ou clé `M['a;b']` | coupée au mauvais endroit | `'a;b'` reste entier |
| Dupliquer sur `Cuves[1].Pompes[2]` | seul `Cuves[1]` se suivait | les deux indices |
| Dupliquer sur `M[1, 2]` | pas suivi | chaque dimension (`M[1, ·]`, `M[·, 2]`) |

## 2. Le lot B : un rôle par propriété

**B1 — La table unique.** 280 clés, chacune avec son rôle, le rôle de ses éléments (pour une liste), son unité (une durée en secondes ou en millisecondes) et son type (`Vue.Objet.Cle` le prend). La Valeur dépend du genre de l'objet : un nombre dans une jauge, un booléen dans un voyant, un état dans un sélecteur, une date dans un sélecteur de date, un texte dans un champ de saisie. Avant, trois tables disaient le type attendu, et elles ne s'accordaient pas.

**B2 — Un lecteur par rôle.** La valeur calculée est lue selon le rôle, sans passer par un texte :
- **un nombre** : un `BOOL` vaut 1 ou 0 ; un `TIME` des secondes (des millisecondes pour une période) ; une `DATE` ou une `DT` des secondes depuis 1970 ; une `TOD` des secondes depuis minuit ; une `STRING` se lit si c'est un nombre (`'12,5'`), sinon elle est refusée et dite ;
- **un booléen** : un nombre différent de 0, `TRUE`, `VRAI`, `'TRUE'` ;
- **un état** : sa valeur, et pour une énumération le nom de sa valeur ;
- **un texte** : l'affichage français (`Vrai`, `1,5 s`, `09/10/2026`, `14:30:00`) ; une énumération montre son texte ;
- **une structure, un tableau, une liste entiers** : un texte les montre ; un nombre, un booléen ou un état les refuse, en proposant leurs membres.

Une valeur refusée ne se dessine plus comme si elle était vraie : l'objet se hachure avec `###`, comme l'afficheur numérique, et la simulation dit pourquoi.

**B3 — Écrire au type de la variable.** Le curseur, le champ de saisie, Affecter, les boutons, l'interrupteur, le programmateur, les onglets et le panneau repliable convertissent ce qu'ils écrivent :
- un entier est arrondi et ramené dans les bornes de l'objet, puis dans celles de son type ; la Console le dit (« 40000 ramené à 32767 (le maximum d'un INT) ») ;
- le champ de saisie, lui, refuse une valeur hors bornes, et le dit ;
- une énumération accepte son nom, son texte, `T_MODE#Auto` ou son numéro ; une valeur inconnue est refusée, avec la liste des valeurs ;
- un `TIME` accepte `1 h 30`, `90 min`, `T#1m30s`, `1,5 s`, `250 ms`, `01:30:00` ;
- une date accepte `09/10/2026` ;
- un `BOOL` accepte vrai, faux, oui, non, on, off, 1, 0 ;
- une structure est refusée avec ses membres.

**B4 — Le moteur lit la même copie que le dessin.** Appuyer, relâcher, double-cliquer, viser une partie d'objet lisent les propriétés calculées. La barre de navigation suit donc ses vues en formule. Un geste (changer d'onglet, déplier un panneau repliable, faire défiler un panneau défilant) ne remplace plus la formule de la propriété : il vaut à côté d'elle, et la formule reprend la main dès que sa valeur change.

**B5 — Générer vérifie le type contre le rôle.** Il signale, en avertissement :
- une structure ou un tableau là où une valeur seule est attendue, dans une jauge ou une pompe (avec ses membres proposés) ;
- une `STRING` lue comme un nombre (un afficheur, une jauge) ;
- une énumération lue comme un booléen (le voyant s'allumerait hors de la première valeur) ;
- un sélecteur de date sur un `BOOL`, un programmateur dont la sortie n'est pas une `STRING` ;
- des listes qui ne s'alignent pas : 2 noms pour 3 barres, les unités, les formats, les valeurs, les libellés ;
- une couleur inconnue, aussi dans les états et les zones ;
- le format `0,0` (la virgule n'y est pas une décimale : il donne un entier ; écris `0.0`) ;
- une valeur fixe qui ressemble à une formule sans `=` (Visible tapé `Ok` : voulais-tu `=Ok` ?) ;
- des seuils dans le désordre (seuil bas 80 > seuil haut 20) ou hors de la plage (alarme haute 150 sur 0..100).

Ce sont des **avertissements**, et c'est voulu : un projet qui démarrait en 1.12.5 démarre toujours. En marche, la valeur qui ne se lit pas montre `###` et dit pourquoi. Ce que Compiler exigeait déjà reste une erreur bloquante, comme avant : `Visible` doit être un booléen, `X` un nombre, `Remplissage` une couleur.

## 3. Le lot C : les repères partout

**C1 — Un seul découpeur de listes.** Le moteur, le dessin, Générer et le renommage coupent les listes `a;b;c` de la même façon. Une chaîne `'a;b'`, une clé `M['a;b']`, un repère `$A;B$` restent entiers. Une apostrophe du français (`l'arrêt`) n'ouvre rien. Les éléments vides gardent leur rang.

**C2 — Sans les `$`, partout.** Les noms (ressource, symbole, vue après connexion, style, vues d'une barre de navigation), les listes de textes, de noms et de couleurs, les états et les textes sans trou se lisent sans les `$` de leurs repères : à l'écran, au clic et dans Générer.

**C3 — Dupliquer.** Chaque propriété est lue dans le mode de son rôle : une formule (même tapée sans `=`) comme une expression, un texte comme un texte. Tous les indices d'un chemin se suivent : `A[0].B[2]` propose `A[0]` et `A[0].B[2]` ; `M[1, 2]` propose chaque dimension.

**C4 — L'essai des repères.** `hmi_reperes_test` couvre maintenant chaque propriété de chaque objet qui peut porter un repère, 135 propriétés :
- Compiler et Générer n'en disent pas plus qu'avec les mêmes propriétés sans repère ;
- les 123 qui se montrent (textes, noms, listes, états) s'affichent sans leurs `$`, et le moteur les lit sans eux au clic ;
- Dupliquer trouve les 135 repères, chacun dans le mode de son rôle.

## Tester vous-même

1. Posez une **jauge** liée à un `BOOL` vrai : elle montre 1. Liée à un `TIME` de `T#90s`, sur 0..120 : 90.
2. Liez une jauge à une `STRING` qui vaut `'abc'` : Générer le signale (un avertissement) ; la simulation démarre, et la jauge se hachure avec `###`.
3. Mettez dans un **texte** `Mode : {Mode}`, où `Mode` est une énumération : il montre le texte de la valeur (Automatique).
4. Posez un **curseur** de 0 à 50000 lié à un `INT`, et poussez-le au bout : l'INT reçoit 32767, et la Console du panneau du bas le dit.
5. Posez un **champ de saisie** en mode texte lié à un `TIME` et tapez `1 h 30` : 90 minutes. Lié à l'énumération, tapez `Auto`.
6. Posez un **bouton** qui affecte `'Manuel'` (le texte d'une valeur) à l'énumération : elle passe à Manu.
7. Mettez une formule sur « Onglet montré » d'un **conteneur à onglets**. En simulation, cliquez un autre onglet : il se montre. Changez la variable de la formule : la formule reprend la main.
8. Posez un afficheur `=M[1, 2]` (`M` : `ARRAY[0..3, 0..3] OF INT`) et faites Ctrl+D : la fenêtre propose chaque dimension.
9. Donnez à une **liste déroulante** les éléments `$Arret$;$Auto$;Manuel` : en marche, ils s'affichent sans les `$`.

## Installer

1. Lancez `XPGAnalyser-Setup-1.12.6.exe`. Il met à jour la version installée : même identité d'installation, données reprises sans être déplacées. Il pose les deux applications et leurs deux raccourcis. Vos projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.12.6.exe` (28,3 Mo, 29 694 518 octets) :
`E14DEFAD2394780FB06CF513FCF6B726335F1EBF08C943AC8FA4123C5C5C8277`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `XPGAnalyser-API-1.12.6` ou `XPGAnalyser-IHM-1.12.6`. Décompressez, puis lancez l'exe.
- `XPGAnalyser-API-1.12.6-portable.zip` (21,2 Mo) :
  `B96FE97ECA3706D8AACFCD047B6DD8B038E5FB62CEB4CC8A85A337ACFAE747F`
- `XPGAnalyser-IHM-1.12.6-portable.zip` (21,2 Mo) :
  `6AFD7168B8CB462E92BE5EC864C03DDACD2C7A6F930F55F7A030FBEB8E3C0B26`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe, 70 sur 70. Elle comprend les 6 846 contrôles de `hmieditor`, les 5 045 de `hmi`, les 1 842 de `hminatives`, les 262 de `hmireperes` et les 133 de `hmitypes`.

Les essais des lots B et C :
- `lotBC1126` dans `hmi` (54 contrôles ; `HMI_TEST_1126=1` le lance seul) : la table, le lecteur par rôle à l'écran, l'écriture vers le type, la barre de navigation et les onglets qui suivent leur formule, Générer, le découpeur, les repères retirés, Dupliquer. Chaque contrôle dit, dans son libellé, ce que faisait la 1.12.5. Deux d'entre eux figent la règle « ce qui démarrait démarre » (voir plus bas) ;
- `chaqueProprieteParSonRole` dans `hmireperes` : les 135 propriétés à repère, comme dit au § C4.

Des essais existants ont changé, parce que le comportement qu'ils figeaient a changé exprès :
- les textes montrent `Vrai`, `Faux`, `1,5 s`, `09/10/2026` (avant : `TRUE`, `FALSE`, `T#1500ms`, `2026-10-09`) ;
- le refus d'un booléen dit « vrai ou faux attendu » ;
- Dupliquer trouve 4 indices dans un texte qui porte `M[1, 2]` (avant : 2, `M[1, 2]` n'était pas suivi) ;
- les compteurs des notes de version (39 versions, 283 lignes).

Générer, sur le projet de démonstration Armoire_Gaz, donne les mêmes 3 erreurs qu'avec la 1.12.5 (des fautes voulues du projet, qui ne bloquent pas la simulation) et aucune fausse alerte nouvelle. Vue_LotBC n'ajoute aucune erreur ; la jauge liée à la `STRING` `'abc'` y donne l'avertissement voulu.

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `XPGAnalyser API 1.12.6` et `XPGAnalyser IHM 1.12.6` ;
- `XpgAnalyzer-API.exe --cli MAST.XPG` analyse le projet d'essai (891 variables, 29 POU), comme avant.

**La session sous Wine** a tourné avec l'exe livré, sur Armoire_Gaz. Un petit outil y a ajouté la vue `Vue_LotBC` et ses huit variables. Chaque ligne de la session a réussi :
- Vue_LotBC dans l'éditeur (capture `1126_ihm_01`). On y voit ce qui est tapé : les formules, les textes à trous, les repères `$Vanne$` et `$Arret$` ;
- Ctrl+D sur l'afficheur `=Grille_BC[1, 2]` (`02`) : la fenêtre propose « Suivre Grille_BC[1, ·] » et « Suivre Grille_BC[·, 2] » ;
- la simulation : le build réussit (386 éléments générés, 312 compilés, 0 erreur, 54 avertissements dont celui de la jauge sur la STRING). La vue montre (`03`) :
  - la jauge sur le BOOL à 1, celle sur le TIME à 90 s, celle sur la STRING `'abc'` hachurée avec `###`, la raison dans le panneau Expressions (« 'abc' n'est pas un nombre ») ;
  - « Marche : Vrai », « Durée du cycle : 1 min 30 s », « Mode : Automatique », « Mise en service : 09/10/2026 » ;
  - « Vanne ouverte, P1 en marche » et la liste sur « Auto », sans leurs `$` ;
  - les onglets sur la page 2, celle de leur formule ;
- le curseur poussé au bout : `Compte_BC` vaut 32767 (`04`), et la Console dit « Compte_BC : 50000 ramené à 32767 (le maximum d'un INT) » (`08`) ;
- « Mode := 'Manuel' » : `Mode_BC` vaut 2 (Manu), et le texte montre « Manuel » (`05`) ;
- un clic sur l'onglet 3 : la page 3 se montre et `Page_BC` reste 2 (`06`). Puis « Page_BC := 1 » : la formule reprend la main, l'onglet 1 se montre (`07`).

**Ce que la session a trouvé, corrigé dans cette livraison.** La première session s'est arrêtée au démarrage de la simulation : « Démarrage bloqué : 1 erreur bloquante ». La jauge liée à la STRING bloquait tout le projet. La table des rôles typait désormais environ 70 propriétés que la 1.12.5 laissait passer : la Valeur des jauges, des voyants et des pompes, les seuils, les couleurs des états… Un projet qui démarrait en 1.12.5 aurait donc pu ne plus démarrer. Ces contrôles sont maintenant des avertissements (§ B5), et deux contrôles de `lotBC1126` figent la règle.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.5 :
- « Installation process succeeded ». Il a repris l'installation de la 1.12.5 : même dossier, et son journal de désinstallation complété ;
- avant comme après : `XpgAnalyzer-API.exe`, `XpgAnalyzer-IHM.exe`, et les raccourcis « XPGAnalyser API » et « XPGAnalyser IHM » ;
- les deux exe installés sont identiques à ceux qui ont été testés. Installés, ils répondent `XPGAnalyser API 1.12.6` et `XPGAnalyser IHM 1.12.6`.

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici). La section « Tester vous-même » donne le parcours à refaire chez vous.

**Une limite connue.** Sur un tableau à deux dimensions, Dupliquer propose chaque dimension mais n'en lit pas encore les bornes : la fenêtre dit « bornes inconnues » (capture `02`), et elle ne signale donc pas une copie qui sortirait du tableau. Pour un tableau à une dimension, rien ne change : les bornes sont lues comme avant.

## La suite

Le plan de l'audit, dans l'ordre recommandé :
- **D** : les listes en tableau dans Contenu (une ligne par élément, au lieu de `a;b;c`) et les listes à choix (groupes d'alarmes, langues, vues après connexion…) ;
- ou **E** : les tailles dynamiques (« 8 cuves, 8 pompes » : une courbe, un tableau, des onglets qui suivent un tableau de l'automate) ;
- puis **F** (plages et seuils pris à la variable) et **G** (synoptique et symboles).
