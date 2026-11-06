# API Studio et IHM Studio 1.27.0 : les objets et les ressources dans les scripts, les fonctions sur les objets, chaque script de vue à son moment

Faite à partir des trois maquettes que tu as validées le 06/11 (« je valide ») :
- **cycle-vue** : la section Exécution des scripts d'une vue, les dix étapes du cycle, les scripts glissés à leur étape, l'OnCycle déballé ;
- **objets-fonctions** : EditObject, CopyObjectTo (`{}` : tout sauf le nom), MoveObject, ResizeObject, les listes d'objets, AlignTop, JustifyH, HideIf, ShowIf… ;
- **types-ressources** : les types Object, Font, Texture, Sound et Style, avec les pointeurs, les listes, les références, leurs attributs, GetSound et les autres recherches, l'aide à la saisie.

Elle se met à jour par-dessus la 1.26.1 et garde tes données.

## 1. Ce qui change pour toi

### Les objets et les ressources dans les scripts : cinq types
- **Object** (un objet d'une vue), **Font** (une police du projet), **Texture** (une image des ressources), **Sound** (un son des ressources ou de la bibliothèque de sons), **Style** (un style nommé).
- Ils s'emploient partout où va un type : `o : Object`, `L : LIST OF Object`, `p : POINTER TO Font`, `r : REF_TO Object`, un paramètre, un retour de fonction.
- Une valeur de ces types **désigne** la chose, elle ne la copie pas. Deux valeurs qui désignent le même objet sont égales. Une valeur neuve vaut **NULL**.
- **Leurs attributs** se lisent et s'écrivent :
  - un Object : `X`, `Y`, `Width`, `Height`, `Visible`, `Text`, `Fill`, `Font`, `Opacity`, `Layer`, `Style`… ;
  - lecture seule : `Name`, `Kind`, `View`, `Parent`, `Children` ;
  - un Sound : `Duration`, `Playing` ; une Font : `Family`, `Glyphs` ; un Style : `Users`.
- Le **sélecteur de type** les range dans « Types de l'IHM ». L'**aide à la saisie** propose leurs attributs après `o.` ou `p^.`. Natives a une sixième étagère, **Types de l'IHM**, avec une fiche par type.

```ST
VAR
    pompes : LIST OF Object;
    titre  : POINTER TO Object := GetObject('Titre_Accueil');
    bip    : POINTER TO Sound := GetSound('Bip court');
END_VAR
pompes := ObjectsOf('Pompe_*');
HideIf(pompes, NOT Mode_Auto);
AlignTop(pompes);
JustifyH(pompes, 16);
titre^.Text := CONCAT('Pompes : ', INT_TO_STRING(pompes.Count));
IF Defaut_Gaz AND bip <> NULL THEN Play(bip^); END_IF;
Voyant_Gaz.Fill := @Alarme;
```

### 38 fonctions sur les objets (Natives › Objets)
| Geste | Fonctions |
|---|---|
| Trouver | `GetObject`, `ObjectsOf('Pompe_*')` (aussi `'type:Voyant'`, `'calque:Process'`, `'Vue_A.*'`), `ObjectsIn(X, Y, W, H)`, `ObjectsOfKind` |
| Modifier, copier | `EditObject(Obj.Prop, valeur)`, `CopyObjectTo(source, cible, {Source.Width, …})` (`{}` : tout sauf le nom), `ResetObject`, `ObjectProp` |
| Position, taille | `MoveObject`, `MoveBy`, `ResizeObject`, `ResizeBy`, `PlaceObject` |
| Aligner | `AlignTop`, `AlignBottom`, `AlignLeft`, `AlignRight`, `AlignCenterH`, `AlignCenterV`, `AlignCenter` |
| Répartir | `JustifyH`, `JustifyV`, `SameWidth`, `SameHeight`, `SameSize`, `ArrangeGrid` |
| Visibilité, état | `ShowIf`, `HideIf`, `EnableIf`, `BlinkIf`, `Show`, `Hide`, `Enable`, `Disable` |
| Ordre, calques | `BringToFront`, `SendToBack`, `BringForward`, `SetLayer` |

- Chaque fonction prend **un objet, une liste** (`{Pompe_1, Pompe_2}`, `ObjectsOf(...)`, une variable `LIST OF Object`) **ou un nom** entre apostrophes.
- Elle agit sur la **vue montrée** (la simulation, le poste), comme `Vue.Objet.X := 120` : **le projet ne change jamais**. `ResetObject` ou le redémarrage de l'IHM rend l'objet tel qu'il est dessiné.
- Un **nom court** (`Pompe_1`) se cherche dans la vue du script qui tourne, sinon dans la vue montrée. **This**, dans une action d'objet, est l'objet lui-même.
- Une **fonction du projet** du même nom passe avant la native.

### 16 fonctions sur les ressources (Natives › Ressources)
- `GetResource`, `GetFont`, `GetTexture`, `GetSound`, `GetStyle` : un pointeur, NULL si rien ne porte ce nom. `Fonts`, `Textures('Pictos/*')`, `Sounds`, `Styles` : les listes.
- `TextWidth(police, texte, taille)` : la largeur d'un texte en pixels. `HasGlyph(police, 'é')` : la police dessine-t-elle ce caractère.
- `Play`, `Stop` ; `ApplyStyle(obj, style)`, `StyleValue(style, 'fill')` ; `Color(@Alarme)` : la couleur `#RRGGBB`.
- **`@Alarme` s'écrit seul** dans un script, comme une constante.

### Ce qui est vérifié
- **Générer** prévient d'un nom écrit qui n'existe pas : `GetSound('Bip cour')`, `GetObject('Pompe_9')` dans un script de vue.
- Une **formule (fx)** peut lire (`ObjectsOf('Pompe_*').Count`, `Color(@Alarme)`) mais pas agir : AlignTop, ShowIf, Play se font dans un script ou une action.

### Les scripts d'une vue : chaque script de cycle à son moment
- Dans l'onglet **Scripts** d'une vue, l'**OnCycle se déballe** : une vue peut avoir plusieurs scripts de cycle, chacun avec son nom et son moment. La liste montre OnOpen, puis OnCycle avec ses scripts dessous (« après 8 », « après 6 · 1/5 », « après 4 · si »), puis OnClose.
- Au milieu, la section **Exécution** montre les **dix étapes** du cycle de la vue, dans l'ordre où le moteur les fait :
  1. Lecture des équipements ;
  2. Entrées ;
  3. Minuteries ;
  4. Scripts généraux cycliques ;
  5. Déclencheurs des objets ;
  6. Alarmes ;
  7. Courbes, archives, compteurs ;
  8. Formules des objets (fx) ;
  9. Dessin de la vue ;
  10. Écritures envoyées.
- Chaque étape a sa **durée mesurée** à la dernière simulation. Sous chacune, les scripts qui tournent après elle.
- **Glisse un script** entre deux étapes (sa pastille, sa ligne dans la liste, ou le bouton « + Script de cycle »). La ligne pointillée dit où il va tourner et ce que ça change (« après 8 Formules, avant 9 Dessin — tes modifications d'objets seront dessinées dans ce cycle »).
- Le menu **Moment** de ses propriétés fait la même chose au clavier. **Ctrl+Z** défait.
- Ses propriétés : **Nom**, **Moment**, **Rang dans l'étape**, **Fréquence** (chaque cycle, ou 1 cycle sur N), **Condition** (une formule : le script ne tourne que si elle est vraie ; Compiler la vérifie), **Durée mesurée**.
- **Un projet existant ne change pas** : son OnCycle tourne entre 4 et 5, comme avant, et s'enregistre à l'identique.
- L'export `.xpgst` des scripts de la vue garde le moment, la fréquence et la condition de chacun.

## 2. Les captures

L'application installée a été ouverte dans une fenêtre (sous Wine), sur le projet de démonstration Armoire_Gaz, et pilotée par une session (`--script`, `--captures`). Avant le lancement, quatre lignes ont été ajoutées au script Init de la copie du projet (AlignTop, JustifyH, MoveBy, EditObject sur les boutons et le titre de Vue_Accueil). Les huit images jointes :
1. `1270_01_natives_aligntop.png` : Natives › Fonctions › Objets (Object) › Aligner, la fiche AlignTop ;
2. `1270_02_natives_copyobjectto.png` : la fiche CopyObjectTo, `{…}` la liste des propriétés ;
3. `1270_03_type_object.png` : Natives › Types › Types de l'IHM, la fiche du type Object (NULL, une poignée, pas de place Modbus, `Object*` en C et C++) ;
4. `1270_04_natives_getsound.png` : Natives › Ressources, la fiche GetSound ;
5. `1270_05_script_init.png` : le script Init et ses quatre lignes ; la barre dit « aucune erreur » ;
6. `1270_06_execution_cycle.png` : les scripts de Vue_Accueil après « + Script de cycle » : l'OnCycle déballé (Cycle, « après 4 »), la section Exécution et ses dix étapes, les propriétés (Moment, Rang, Fréquence, Condition, Durée mesurée) ;
7. `1270_07_glisser_apres_8.png` : la pastille Cycle tirée sous l'étape 8, la ligne pointillée « Déposer ici : après 8 Formules, avant 9 Dessin… » ;
8. `1270_08_depose_apres_8.png` : lâchée : Cycle tourne « après 8 », le message « tes modifications d'objets seront dessinées dans ce cycle (Ctrl+Z le remet) ».

**Ce que les captures ont trouvé, et qui est corrigé.** Au premier passage, la génération refusait les nouvelles fonctions (« fonction inconnue : ObjectsOf, MoveBy… ») : la simulation ne démarrait pas. Les essais passaient, parce qu'ils appelaient le moteur directement, sans passer par la vérification de Générer. Corrigé dans la vérification des scripts (`HmiScriptCheck.cpp`) :
- les 54 fonctions sont connues, avec leur nombre d'arguments ; une fonction du projet du même nom passe toujours avant ;
- `{a, b}` compte pour un seul argument ;
- `@Alarme` est accepté ;
- `Vue.Objet` sans propriété est accepté (c'est un Object) ;
- le nom court d'un objet est accepté dans un script de sa vue.

Un essai le vérifie maintenant comme Générer le fait (`hmi_objets_test`). La capture 5 montre le résultat.

**Pas de capture de la simulation** : sous IHM Studio seul, Armoire_Gaz ne démarre pas en simulation. Ses vues lisent des variables de l'automate (`Armoires[0]…`), inconnues sans API Studio. C'était déjà le cas avant la 1.27.0. Ce que les fonctions font en marche est vérifié par `hmi_objets_test`, sur une vue d'essai.

## 3. Les essais

Sous Linux : **137 suites d'essais sur 137** réussies (CTest). Deux suites sont nouvelles : `hmiobjets` et `hmicycle`.

Au premier passage, trois suites avaient des échecs. Ce sont les essais qui ont été corrigés, pas le logiciel :
- `hmitypes` et `hmieditor` attendaient les listes de types d'avant (sans les cinq types de l'IHM) et les dix thèmes de Natives › Fonctions (Objets et Ressources sont maintenant en tête) ;
- `hmi` a trouvé un exemple du guide écrit en ST seulement : il a maintenant ses versions C et C++, comme les autres.

- **`hmi_objets_test`** (nouveau) : 129 contrôles, sur une vue d'essai avec trois pompes :
  - les types : déclarer, NULL, égalité, pointeurs, listes ;
  - les attributs : lire (`o.Kind`, `p^.Width`), écrire (`o.Fill := ...`, `p^.Text := ...`) ;
  - chaque famille de fonctions : trouver, modifier et copier (`{}` : tout sauf le nom), placer, aligner, répartir, la grille, la visibilité, l'ordre, les calques ;
  - les ressources : GetSound (et NULL pour un son absent), GetFont, GetStyle, TextWidth, HasGlyph, ApplyStyle, Color(@Alarme) ;
  - les noms courts dans la vue du script, This, une fonction du projet qui passe avant la native ;
  - une formule (fx) qui lit sans agir, Générer qui signale un nom inconnu ;
  - la vérification de Générer (l'automate connu) qui accepte les fonctions, `{a, b}`, `@Alarme`, `Vue.Objet` et les noms courts d'une vue.
- **`hmi_cycle_test`** (nouveau) : 57 contrôles :
  - l'ordre des étapes 1 à 9, même quand la vue range ses scripts dans le désordre ;
  - ce que voit un script à chaque moment : le script général entre 3 et 4, le front entre 4 et 5, l'alarme après 6 et pas après 4, une variable Modbus après 1 ;
  - l'OnCycle d'avant : à la même place, le même nom au journal, le même fichier sur le disque et en `.xpgst` ;
  - le rang, 1 cycle sur 3 (qui repart à la réouverture de la vue), la condition (et une condition illisible, dite une fois au journal) ;
  - 8 et 9 avec l'écran, la mesure, l'aller-retour disque et `.xpgst`, Compiler et Générer.
- **`hmi_editor_test`** : 10 979 contrôles (10 914 en 1.26.1). Nouveau : la section Exécution (61 contrôles) :
  - la liste déballée, + Script de cycle, glisser une pastille après 8 (la ligne pointillée, le dépôt) ;
  - lâchée hors de la section : rien ne change ;
  - le menu Moment, le Rang, la Fréquence, la Condition, Ctrl+Z et Ctrl+Y.
- **`hmi_natives_test`** : 3 265 contrôles. Les 54 nouvelles fiches s'exécutent (leurs exemples ST, sur la vue d'essai), les étagères Objets et Ressources, les fiches des cinq types.
- **`hmi_test`** : 6 973 contrôles, sans échec.
- **`hmi_types_test`** : 157 contrôles, les cinq types dans le registre.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.27.0.exe` : l'installateur (52 654 078 octets). Il met à jour la 1.26.1 au même endroit ;
- le même installateur dans un zip ;
- `API-Studio-1.27.0-portable.zip` et `IHM-Studio-1.27.0-portable.zip` (188 fichiers chacun) ;
- les huit captures ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.27.0.exe` | `3749F217900AEEB516296361827F5E03E9B8D51B3A71FFB89E0AF2CB49E08FCF` |
| `API-Studio-1.27.0-portable.zip` | `082F2BB9870AF5881D0FD4FC13F810DD263E71B97DBA95B17A2D579B90D55FFE` |
| `IHM-Studio-1.27.0-portable.zip` | `A2C025167B56E20BF3C4551664253656C6E90BB6BE9FB6C3C773ACA1CD6963E2` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.26.1 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.27.0 » et « IHM Studio 1.27.0 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.26.1 est gardée ;
- **IHM Studio a été ouvert dans une fenêtre** (sous Wine) : les huit captures viennent de l'exécutable livré.

## 5. Limites

**Les types de l'IHM et les fonctions** :
- `IHM_POPUP` rend toujours un BOOL (ouvert ou non), pas un Object.
- `Stop(son)` arrête tous les sons en cours, pas seulement celui-là.
- Dans un script **C ou C++**, les mêmes noms sont édités et vérifiés, pas exécutés en simulation (comme tout script C et C++).
- **Générer** ne vérifie que les noms écrits en clair (`GetSound('Bip court')`) ; un nom calculé se vérifie en marche. Dans un script général, un objet se cherche dans la vue montrée : il n'est vérifié qu'en marche.
- Les attributs `Pressed`, `Focused` et `Shown` d'un Object sont en lecture seule.

**Le cycle de la vue** :
- Étape 1 : les lectures d'équipements suivent la période de leur liaison ; « après 1 » ne relit pas l'équipement.
- Étape 2 : seulement les gestes tenus (appui long, glissés) ; les clics partent à leur arrivée, entre deux cycles.
- Étape 4 : elle commence par les médias.
- Étape 10 n'est pas une étape séparée : chaque écriture part quand elle est faite. Rien ne se place après 10 ; la section affiche le nombre d'écritures du cycle.
- Étapes 8 et 9 : exactes avec l'écran de la simulation ; sans écran (les essais, les exemples de l'aide), elles suivent 7. « Après 8 » coûte un second calcul de la vue, seulement aux cycles où un tel script a tourné.
- Le total affiché compte les étapes 1 à 9 et les scripts de cycle de cette vue (pas ceux de ses popups).
- Le débogueur nomme toujours un script de cycle « Vue.OnCycle » dans la pile.
- **Compatibilité** : une version d'avant la 1.27.0 ouvre le projet mais met tous les scripts de cycle après 4. Si elle réenregistre, elle perd la fréquence et la condition.
- Les boutons « Mesurer un cycle » et « Essayer la vue » de la maquette ne sont pas faits : la durée vient de la dernière simulation.

**Le projet de démonstration** : sous IHM Studio seul, Armoire_Gaz ne démarre pas en simulation (ses variables d'automate sont inconnues sans API Studio). Ce n'est pas nouveau dans la 1.27.0.

**Les vérifications ont été faites sous Wine, pas sur un vrai Windows.**
