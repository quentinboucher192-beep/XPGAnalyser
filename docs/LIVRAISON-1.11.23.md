# XPGAnalyser 1.11.23 : la souris, le clavier, les raccourcis, la fin de l'explorateur

Livrée le 09/10/2026 en soirée. Elle termine votre liste du 09/10 :
- « les variables SYS de la souris et du clavier » ;
- « dans les vues et les popups, une section Raccourcis qui crée des raccourcis liés à des actions, avec leur type de déclenchement : durée, front montant, front descendant » ;
- « modernise l'explorateur d'application » : ce que la 1.11.22 n'avait pas fait de la maquette validée (les puces de filtre, les titres de domaine, la légende) ;
- et le piège trouvé en 1.11.22 : le bouton **Valider** du popup « Saisie d'une consigne », qui fermait le popup sans écrire la saisie.

**Un projet de la 1.11.22 s'ouvre tel quel.** Un projet enregistré par la 1.11.23 reste lisible par la 1.11.22 tant qu'aucune vue n'a de raccourci. Dès qu'une vue en a un, le projet passe au format 24, que la 1.11.22 refuse d'ouvrir (avec un message qui le dit) plutôt que de perdre les raccourcis.

Fichiers livrés :
- `XPGAnalyser-Setup-1.11.23.exe` (l'installateur) ;
- `XPGAnalyser-1.11.23-portable.zip` (le même programme, sans installation) ;
- ce document ;
- les captures `11123_*`, prises sous Wine avec l'exe livré.

## 1. Ce qui change pour vous

- **La souris et le clavier dans les variables système.** Un domaine nouveau, **Souris et clavier**, dans IHM › Programmation générale › Variables système :
  - la souris : `SYS.MouseX`, `SYS.MouseY` (en pixels de la vue, pas de l'écran), `SYS.MouseView` (la vue ou le popup sous la souris), `SYS.MouseObject` (« Vue.Objet »), `SYS.MouseInside`, les boutons (`SYS.MouseLeft`, `SYS.MouseRight`, `SYS.MouseMiddle`, et `SYS.MouseButtons` : 1 gauche + 2 droit + 4 milieu), la molette (`SYS.MouseWheel`, en crans) ;
  - le clavier : `SYS.KeyLast` (la dernière touche, avec Ctrl, Maj, Alt : « Ctrl+F5 »), `SYS.KeysDown` (les touches tenues : « F5;Haut »), `SYS.KeyDownCount`, `SYS.KeyAnyDown`, `SYS.KeyCtrl`, `SYS.KeyShift`, `SYS.KeyAlt`, `SYS.KeyHoldTime` (depuis quand la dernière touche est tenue, un TIME), `SYS.KeyPresses` ;
  - les raccourcis : `SYS.ShortcutLast` (« Vue · touche ») et `SYS.ShortcutCount` ;
  - et **`SYS.Key.<touche>`**, vraie tant que la touche est tenue : `SYS.Key.F5`, `SYS.Key.Up`, `SYS.Key.A`. Les chiffres s'écrivent `SYS.Key.Digit1` (`SYS.Key.1` se lirait comme le bit 1). Une touche inconnue est une faute de compilation, qui donne la liste des touches.

  Elles se lisent partout où une variable système se lit : un texte à trous (`{SYS.MouseX:0}`), une expression, une condition, un script.
- **Les raccourcis d'une vue ou d'un popup.** Dans l'éditeur d'une vue ou d'un popup, rien de choisi, l'inspecteur a un troisième onglet, **Raccourcis** (à côté de Propriétés et Actions) :
  - **Ajouter** crée un raccourci sur la première touche libre de F2 à F12, qui journalise « Raccourci F2 ». Vous réglez sa **Touche**, son **Déclencheur**, puis son **Opération**, comme pour une action : écrire une variable, basculer, incrémenter, naviguer, ouvrir un popup, lancer un script, etc. ;
  - la touche s'écrit comme on la lit sur le clavier, sans casse ni accents : `F5`, `Ctrl+S`, `Maj+Entrée`, `Alt+Haut`, `A`, `1`. Ctrl, Maj et Alt comptent : `Ctrl+F5` n'est pas `F5` ;
  - les quatre déclencheurs :

    | Déclencheur | Quand l'action part |
    |---|---|
    | **Front montant** (touche enfoncée) | à l'appui, une fois ; la répétition du clavier ne compte pas |
    | **Front descendant** (touche relâchée) | au relâchement, dans la vue qui a pris la touche, même si elle s'est fermée entre-temps |
    | **Durée** (touche maintenue) | une fois, quand la touche a été tenue sa durée (1000 ms par défaut) |
    | **Répétition** (tant qu'elle est tenue) | toutes les N ms (200 par défaut), tant que la touche est tenue et la vue montrée |

  - une **Condition** facultative retient le raccourci tant qu'elle est fausse (`SYS.UserLevel >= 2`) ;
  - la liste de l'onglet montre la touche, le déclencheur et l'opération ; l'onglet porte le nombre de raccourcis ; Ctrl+Z retire ce que vous venez de faire.
- **Qui prend la touche, en marche.**
  - Les popups ouverts d'abord, de celui du dessus vers le dessous, puis la vue (avec son modèle, son en-tête et son pied) : le premier qui a un raccourci de cette touche le fait partir. Un popup modal qui n'en a pas arrête la recherche.
  - Échap seul, un popup ouvert : son raccourci s'il en a un, sinon le popup se ferme, comme avant.
  - Un champ de saisie qui a le clavier garde ses touches : seules F1 à F12 y partent en raccourci. Il en va de même pendant le menu de connexion, la signature, le clavier virtuel d'une action, et pour les chiffres du lecteur de badge.
  - Une touche prise par un raccourci ne va nulle part ailleurs : dans la simulation de l'éditeur, ni le F5 ni le Ctrl+S de l'application ne partent. L'éditeur garde F8, Maj+F8, F11 et Ctrl+Alt+S ; le poste d'exploitation garde F1, F11, F12, Ctrl+Alt+Q et Ctrl+Alt+S.
  - Un raccourci est un geste de l'opérateur : sous sécurité, sa permission est demandée (Piloter pour écrire…), l'audit le voit, et le journal dit chaque raccourci parti (type « Raccourci »).
  - **Compiler** signale une touche illisible, une touche que le poste garde, un raccourci posé sur un objet (il ne partirait jamais) ou dans un symbole.
- **Valider les saisies.** Une opération nouvelle, **Valider les saisies**, écrit la saisie en cours de la vue (ou du popup), comme Entrée. Le clic sur le bouton qui la porte ne fait plus perdre la saisie au champ. Refusée (hors bornes, illisible), elle arrête les actions suivantes du même bouton : un « Fermer le popup » placé après elle attend une saisie juste. Le bouton **Valider** du modèle « Saisie d'une consigne » s'en sert désormais. Pour vos popups déjà créés à partir de ce modèle, ajoutez « Valider les saisies » en tête des actions de leur bouton Valider (voir « Tester vous-même »).
- **L'explorateur, la suite de la maquette.**
  - Sous le champ de filtre, quatre **puces** : **Modifiés**, **En faute**, **À générer** et **Épinglés**, chacune avec son nombre, toujours visible. Un clic ne montre que leurs lignes (et leurs dossiers, dépliés) ; un second clic rend tout l'arbre ;
  - les **titres de domaine** disent leur état en gris après leur nom : « 29 POU · 891 variables » pour l'API, « 40 vues · 327 à générer » pour l'IHM, « arrêtée · cycle 0 » pour la Simulation, la version en cours et ses changements pour les Versions ;
  - au survol d'un titre, **son action** : Réimporter et réanalyser (API), Générer l'IHM, Démarrer ou Arrêter la simulation, Créer une version ; puis Plus… (le menu du clic droit) ;
  - le bouton **?** du rail, à gauche de l'arbre, ouvre **la légende** : une page du guide qui dit chaque marque (la colonne d'état, le compteur, le point orange, les étiquettes, les couleurs, les puces, les titres).

## 2. La souris et le clavier dans les variables système

- **Le domaine** « Souris et clavier » (`hmi::pub::kInputDomain`, `src/hmi/HmiPublicVars.hpp`) : 21 variables en lecture seule, chacune avec son type exact (REAL pour la position, STRING pour les noms, BOOL, INT, DINT, TIME). La page « Variables système » du guide les liste, avec ce qu'elles disent.
- **`SYS.Key.<touche>`** n'est pas une variable de la liste : c'est un chemin que la compilation résout (`resolve`), touche par touche. Les jetons sont ceux de `src/hmi/HmiKeys.hpp` : A à Z, Digit0 à Digit9, F1 à F12, Enter, Escape, Space, Tab, Backspace, Delete, Insert, Home, End, PageUp, PageDown, Up, Down, Left, Right. `SYS.Key` seul, ou une touche inconnue, est une faute de compilation qui le dit.
- **Le moteur** (`src/hmi/HmiRuntimeInput.cpp`, nouveau) reçoit la souris et le clavier de la vue en marche : la position et l'objet sous la souris (en pixels de la vue, à travers le zoom et les popups), les boutons, la molette, chaque touche enfoncée et relâchée. La simulation de l'éditeur et le poste d'exploitation les lui donnent.
- **Le clavier va à l'IHM qui a le focus** : dans la simulation de l'éditeur, un clic dans la vue le lui donne. Quand elle le perd (un clic dans un autre onglet), les touches tenues sont relâchées, et leur front descendant part.

## 3. Les raccourcis des vues et des popups

- **Le modèle** (`src/hmi/HmiModel.hpp`) : un raccourci est une **action de la vue** dont le déclencheur est une touche (`Trigger::KeyPress`, `KeyRelease`, `KeyHold`, `KeyRepeat`), avec sa touche (`Action::key`, enregistrée sous une forme stable, « Ctrl+Shift+Enter ») et sa durée ou sa période (`delayMs`). Toutes les opérations des actions sont permises.
- **L'enregistrement** (`src/hmi/HmiStore.cpp`) : la ligne de l'action gagne `touche="F5"`. Le format passe à 24 **seulement si** une vue a un raccourci ; sinon le projet reste au format 23, lisible par la 1.11.22.
- **L'éditeur** (`src/app/hmi/HmiActionsPanel.cpp`) : le volet des actions sert aussi à l'onglet Raccourcis, en ne montrant que les actions à touche. Sa catégorie « Raccourci » remplace « Déclencheur » : la Touche (lue et refusée si elle est illisible, avec la raison), le Déclencheur, sa Durée ou sa Période, la Condition. L'onglet Actions, lui, ne montre plus ces actions-là.
- **En marche** (`Runtime::keyDown`, `keyUp`, `keysTick`) :
  - à l'appui, la vue qui prend la touche est cherchée une fois (les popups du dessus vers le dessous, puis la vue) ; la touche reste à elle jusqu'au relâchement ;
  - la Durée part une fois, quand la touche a été tenue assez longtemps ; la Répétition part toutes les périodes, sans prendre plus de dix retards à la fois ; les deux seulement tant que la vue est montrée ;
  - le front descendant part dans la vue qui a pris la touche, telle qu'elle est si elle est encore montrée, sinon telle qu'elle était à l'appui, avec ses paramètres d'alors ;
  - une touche qu'aucun raccourci ne prend est rendue à l'application (F5 de la simulation, Ctrl+S…).
- **Compiler** (`src/hmi/HmiCheck.cpp`, catégorie « Raccourci ») : une touche vide ou illisible est une erreur ; une touche que le poste garde (F1, F11, F12, Tab, Ctrl+Alt+Q, Ctrl+Alt+S, Alt+F4), un raccourci posé sur un objet ou dans un symbole sont des avertissements.

## 4. Valider les saisies

- L'opération `Operation::SubmitInputs` (« Valider les saisies », famille Piloter) écrit la saisie du champ de saisie qui a le clavier dans la vue, ou dans le popup, comme Entrée. Si aucun champ n'a le clavier, elle ne fait rien.
- Le bouton qui la porte ne prend plus le clavier au champ : la saisie reste en attente jusqu'à l'action.
- Refusée (hors bornes, illisible), elle arrête les actions suivantes du même geste : un « Fermer le popup » placé après elle ne part pas, et le champ garde sa saisie à corriger.
- Le modèle « Saisie d'une consigne » (`src/hmi/HmiDesign.cpp`) met « Valider les saisies » avant « Fermer le popup » sur son bouton Valider. Les popups déjà créés à partir de ce modèle ne changent pas : leur bouton garde ses actions d'avant.

## 5. L'explorateur : les puces, les titres de domaine, la légende

- **Les puces** (`TreeChips`, `src/app/screens/TreeWorkspace.cpp`) : Modifiés, En faute et À générer sont des mots réservés du filtre de l'arbre (`@modifies`, `@en-faute`, `@a-generer`). Le filtre garde ce qui les porte, avec leurs dossiers dépliés, et rend l'arbre comme avant quand la puce s'éteint. Épinglés allume la portée Épinglés et récents du rail. Les nombres (`ProjectTreeModel::quickCounts`, `src/app/ViewModels.cpp`) sont recalculés avec le rail et le pied :
  - **Modifiés** : les dossiers changés depuis la dernière version (le point orange) ;
  - **En faute** : les éléments de l'IHM dont le build a échoué ou dont le code ne compile pas, et les sections de l'automate en erreur ;
  - **À générer** : ce qui a changé depuis le dernier build, ou n'a jamais été généré ni compilé.
- **Les titres de domaine** (`ProjectTreeModel::domainStatus`) : l'API compte ses POU et ses variables (et ses mises à jour de bibliothèque) ; l'IHM, ses vues et son état de build (en faute, à générer, à jour) ; la Simulation reçoit sa ligne de l'écran (arrêtée ou en marche, le nombre de cycles) ; les Versions disent la version en cours et ses changements. L'action au survol (`MainAnalysisScreen::treeHeadAction`) fait ce que fait le menu du clic droit.
- **La légende** : un sujet du guide, « La légende de l'explorateur » (chapitre Démarrer), ouvert par le **?** du rail. Avant, le **?** ne montrait qu'une infobulle.

## 6. Le parcours vérifié sous Wine

Une session jouée avec l'exe livré (`tools/sessions/session-11123-clavier-explorateur.txt`), sur une copie d'Armoire_Gaz sans cache, celle de la 1.11.17, préparée comme pour la 1.11.22 (`preparer-projet-11120`, puis `preparer-projet-11121`), puis par `preparer-projet-11123`. Ce dernier ajoute la vue **Vue_Clavier** : des textes qui montrent les variables de la souris et du clavier, une zone à cliquer, les variables Compteur_F5, Lampe_F6 et Niveau, et trois raccourcis : **F5** (front montant : Compteur_F5 + 1), **F6** (durée : tenue une seconde, Lampe_F6 bascule), **Haut** (répétition : Niveau + 1 toutes les 200 ms).

Les commandes de contrôle de la session (`ihm-sim-etat`, `ihm-variable`) échouent si le résultat n'est pas celui attendu ; il n'y a eu aucun échec.

| Étape | Ce qui s'est passé | Capture |
|---|---|---|
| **1.** Le projet ouvert, l'explorateur | Sous le filtre, les puces : **Modifiés 9**, **En faute**, **À générer 327**, **Épinglés** (une puce à zéro ne montre pas de nombre). Les titres : API « 29 POU · 891 variables », IHM « 40 vues · 327 à générer » (aucun build depuis l'ouverture), Simulation « arrêtée · cycle 0 ». Le pied : « Simulation arrêtée · V4 modifié » | `11123_01` |
| **2.** La puce **À générer**, puis la même encore | Le champ montre le mot réservé `@a-generer`, la ligne d'information « À générer ou à compiler : 275 ligne(s), avec leurs dossiers — la puce, encore : tout » ; l'arbre, déplié, ne garde que ces lignes (Configuration › Alarmes, Recettes, Vues…). Le second clic rend l'arbre entier | `11123_02` |
| **3.** Le **?** du rail | La page « La légende de l'explorateur » du guide (chapitre Démarrer) : à droite l'état puis le compteur (le tableau des marques), après le nom, les couleurs, les puces, les titres de domaine. Échap revient à l'écran d'analyse | `11123_03` |
| **4.** Vue_Clavier ouverte, rien de choisi, onglet **Raccourcis** ; **Ajouter**, Touche `Maj+Entree`, Déclencheur « Front descendant » | Ajouter a pris F2, la première touche libre (F5 et F6 sont pris). La touche tapée sans accent s'écrit **Maj+Entrée** ; le déclencheur « Front descendant (touche relâchée) ». L'onglet porte **4** ; la liste (# · Touche · Déclencheur) montre « 3 Haut Répétition » et « 4 Maj+Entrée Front descendant » | `11123_04` |
| **5.** Simulation › IHM (le build d'abord : 327 générés, 243 compilés, 0 erreur ; l'IHM en marche) ; Vue_Clavier ; un clic sur la zone ; **F5** trois fois ; **F6** tenue deux secondes | La vue en marche : « Souris : X = 630 Y = 260 », « Sous la souris : Vue_Clavier.Zone_Clic », « sur l'IHM : TRUE » ; « Dernière touche : F6 », « Touches tenues : F6 (1) », « Tenue depuis : T#1267ms », « SYS.Key.F6 : TRUE », « Dernier raccourci : Vue_Clavier · F6 (4) » ; à droite « Compteur_F5 = 3 », « Lampe_F6 = TRUE ». Les contrôles : Compteur_F5 vaut 3, Lampe_F6 vaut TRUE | `11123_05` |
| **6.** F6 relâchée, **Haut** tenue un peu plus d'une seconde, puis **Maj+Entrée** | « Dernière touche : Maj+Entrée », « Touches tenues : (0) », « SYS.Key.F6 : FALSE », « Touches enfoncées : 6 », « Niveau = 3 », « **Dernier raccourci : Vue_Clavier · Maj+Entrée (8)** » : le front descendant de Maj+Entrée est parti | `11123_06` |
| **7.** IHM › Programmation générale › Variables système, domaine **Souris et clavier** | Les valeurs de l'IHM en marche : `SYS.KeyLast` « Maj+Entrée », `SYS.KeyPresses` 6, `SYS.KeyDownCount` 0, `SYS.KeyHoldTime` T#0ms, `SYS.ShortcutLast` « Vue_Clavier · Maj+Entrée », `SYS.ShortcutCount` 8 ; chaque variable avec son type (BOOL, INT, DINT, STRING, TIME) et sa description | `11123_07` |

**Ce que la session a montré.**
- **À la première exécution, Maj+Entrée n'est pas parti** : « Dernier raccourci » restait à « Haut (7) ». Armoire_Gaz a la **connexion par badge** (`badge=1`). Le moteur réservait au lecteur de badge les chiffres et Entrée, **avec ou sans Maj** : Maj+Entrée était donc pris pour la fin d'un badge. Un lecteur tape ses chiffres (avec Maj sur un clavier AZERTY), puis **Entrée seule**. La règle est corrigée : Entrée seule reste au lecteur, Maj+Entrée part en raccourci. Un essai le vérifie (*clavierSouris11123*), l'exe a été recompilé et la session rejouée : le raccourci est parti (capture 6 : « Maj+Entrée (8) »).
- **Niveau = 3** pour Haut tenue un peu plus d'une seconde. La répétition suit l'horloge de l'IHM. Dans cette session sous Wine, cette horloge a avancé moins vite que celle du poste : au bout de deux secondes réelles, la durée affichée était de 1,267 s (capture 5). Les essais vérifient le rythme exact : 3 répétitions en 350 ms, à 100 ms de période.
- **La liste de l'onglet Raccourcis est basse** dans cette disposition (la bibliothèque occupe le bas du panneau de droite) : on n'y voit qu'une ligne et demie, et la colonne Action est hors de vue. Il faut faire défiler la liste. C'est la même disposition que l'onglet Actions ; elle n'a pas été changée.

## 7. Fichiers modifiés

- la souris et le clavier : `HmiKeys.hpp` (nouveau : les touches, leur lecture, leur nom en français, celles que le poste garde), `HmiRuntimeInput.cpp` (nouveau : la souris, les touches, les raccourcis en marche), `HmiRuntime`, `HmiRuntimePublic` (les variables du domaine, `SYS.Key.<touche>`), `HmiPublicVars.hpp` (le domaine, ses 21 variables), `InputEvent.hpp` et `SdlEventPump.cpp` (les touches qui manquaient : B, E, G, I, M, R, T, U, les chiffres, F4, F6…) ;
- les raccourcis : `HmiModel` (les quatre déclencheurs, la touche d'une action), `HmiStore` (la touche enregistrée, le format 24 seulement s'il y a des raccourcis), `HmiActionsPanel` (l'onglet Raccourcis), `HmiEditor` (l'onglet, son nombre), `HmiSimulation` (la souris et le clavier de la vue en marche vers le moteur), `HmiCheck` (Compiler les vérifie), `HmiTreeData.hpp` (le texte d'un raccourci), `Icons` (l'icône Clavier) ;
- Valider les saisies : `HmiModel`, `HmiRuntime` (l'opération, l'arrêt du geste si elle est refusée), `HmiActionKinds` (son aide), `HmiDesign` (le bouton Valider du modèle), `HmiSimulation` (le bouton ne prend plus le clavier au champ) ;
- l'explorateur : `TreeWorkspace` (les puces, l'action des titres, le ? du rail), `ViewModels` (les nombres des puces, les lignes d'état des domaines), `DataViews` (la ligne d'état et l'action au survol des titres), `HmiBuildState` et `HmiBuildWorkspace` (les totaux du build pour l'IHM), `MainAnalysisScreen` ;
- les commandes de session : `ScriptRunner` (`arbre-puce`, `arbre-legende`, `touche-enfoncer`, `touche-relacher`), `UiDriver` (les touches nouvelles) ;
- le guide (« Les raccourcis d'une vue », « La légende de l'explorateur », Variables système, Champ de saisie), les notes (4), la version 1.11.23 ;
- la session Wine : `tools/sessions/session-11123-clavier-explorateur.txt` et `tools/sessions/preparer-projet-11123.cpp` ;
- les essais : `hmi_test`, `hmi_editor_test`, `hmi_decl_test`.

## 8. Les tests ajoutés

- **`hmi`**, *clavierSouris11123* (38 contrôles) :
  - les touches : « ctrl + maj + entree » se lit `Ctrl+Shift+Enter` et se montre `Ctrl+Maj+Entrée` ; F5, 1 (Digit1), Échap, Haut, Pg suiv ; « Ctrl+ » seul, « Hyper+A » et « Pomme » refusés, avec leur raison ; `Ctrl+F5` n'est pas `F5` ; F1 et Ctrl+Alt+Q gardées, F2 libre ;
  - en marche : le front montant (une fois : ni la répétition du clavier ni la touche tenue ne le relancent), le front descendant, `H` seule qui n'est pas `Ctrl+H`, la durée (rien avant 500 ms, puis une seule fois), la répétition (rien avant la première période, 3 fois en 350 ms à 100 ms, plus rien une fois relâchée) ;
  - qui prend la touche : la popup modale qui a F5 le prend avant la vue, et arrête la recherche pour Haut, qu'elle n'a pas ; relâchée après la fermeture de la popup, son front descendant part quand même ; une popup non modale sans F5 laisse la vue le prendre ; Échap seul n'est à personne (la popup se ferme) ; un champ de saisie qui a le clavier garde A mais laisse partir F2 ; le clavier perdu relâche F5 (son front descendant part) ;
  - les variables : `SYS.Key.F5`, `SYS.KeyDownCount`, `SYS.KeyLast`, `SYS.KeyCtrl`, `SYS.KeyHoldTime` (un TIME), `SYS.ShortcutCount`, `SYS.ShortcutLast` et la ligne du journal (Raccourci) ; `SYS.MouseX`, `SYS.MouseY`, `SYS.MouseView`, `SYS.MouseObject`, `SYS.MouseInside`, les boutons (gauche + milieu : 5), la molette ; la souris sortie (dehors, boutons relâchés) ; l'IHM redémarrée repart de zéro ;
  - Compiler : une touche illisible (erreur), un raccourci sur un objet et F1 (avertissements) ;
  - l'enregistrement : le format 24, et le raccourci relu (Touche maintenue Ctrl+H, 500 ms) ;
  - la connexion par badge : Entrée seule reste au lecteur, Maj+Entrée part en raccourci (ajouté après la session Wine, § 6).
- **`hmi`**, *validerSaisies11123* (5 contrôles) : le modèle « Saisie d'une consigne » met Valider les saisies puis Fermer le popup sur son bouton Valider ; le clic sur Valider ne fait pas perdre la saisie ; Valider écrit la consigne (12) et ferme le popup ; 999, hors bornes, est refusé : le popup reste ouvert et le champ garde le clavier ; Fermer abandonne la saisie.
- **`hmieditor`**, *raccourcis1123* (12 contrôles) : Ajouter crée F2 (front montant), que l'onglet Actions ne montre pas ; la grille a Touche et Déclencheur, et Durée (ms) seulement pour une touche maintenue ; aller à un raccourci ouvre l'onglet Raccourcis sur sa ligne ; la touche tapée devient `Ctrl+Shift+F5` ; Ctrl+Z ; un symbole n'a pas d'onglet Raccourcis.
- **`hmieditor`**, *explorateur1123* (8 contrôles) : les lignes d'état des titres (« 2 vues · pas encore générée », « 0 POU · 0 variables », « V5 en cours · 48 changements », « 2 vues · 1 en faute · 1 à générer ») et leurs actions (Démarrer, puis Arrêter quand la simulation tourne) ; les nombres des puces (1 en faute, 1 à générer, rien de modifié) et les marques de chaque ligne.
- **`hmidecl`** : le format refusé comme « plus récent » suit la version du format (25 ; le 24 est celui des raccourcis).

## 9. Les limites

- **Rien n'a été lancé sur un vrai Windows** : tout a été vérifié sous Wine.
- **Le clavier et la souris** ne viennent que de la vue en marche : la simulation de l'éditeur (après un clic dans la vue) et le poste d'exploitation. Hors de l'IHM, `SYS.MouseInside` est faux et les touches ne lui arrivent pas.
- **Renommer une vue ou un objet** ne change pas les textes qui comparent `SYS.MouseObject` ou `SYS.MouseView` à un nom (`SYS.MouseObject = 'Vue_Four.Btn_Marche'`) : ce sont des chaînes.
- **L'aide à la saisie** propose les variables du domaine (`SYS.MouseX`…), mais pas encore les touches après `SYS.Key.` : la page « Variables système » du guide les liste.
- **Un symbole n'a pas de raccourcis** : son éditeur n'a pas l'onglet Raccourcis, et Compiler avertit si une action à touche y est posée (elle ne partirait pas). Mettez le raccourci dans la vue qui contient l'instance.
- **L'explorateur** : les titres de domaine restent sur une ligne (la maquette mettait l'état sur une seconde ligne, sous le nom) ; le pied de santé garde son dessin ; le titre collant, en haut de l'arbre quand on défile, garde l'ancien dessin. La puce Modifiés compte des dossiers entiers (ceux qui ont changé depuis la dernière version), pas les éléments un par un.
- **Les raccourcis clavier des éditeurs** (le § 14 de la spécification de la refonte des scripts) restent à faire : ce sont ceux de l'application, pas ceux des vues.
- **La source du guide Word** (`tools/guide-ihm/guide-ihm.txt`) n'a toujours pas suivi les versions depuis la 1.11.2 : l'aide F1 (le C++) a été modifiée directement.

## Tester vous-même

1. Ouvrez une vue, sans rien choisir : l'inspecteur a l'onglet **Raccourcis**. **Ajouter**, puis Touche `F5`, Déclencheur « Front montant », Opération « Incrémenter » sur une variable INT montrée dans la vue.
2. Ajoutez `F6`, Déclencheur « Durée (touche maintenue) », 1000 ms, « Basculer » sur un BOOL ; puis `Haut`, « Répétition », 200 ms, « Incrémenter ».
3. Mettez dans la vue un texte `{SYS.KeyLast} · {SYS.KeysDown} · {SYS.KeyHoldTime} · {SYS.MouseObject}`.
4. **Simuler l'IHM**, allez à la vue, **cliquez dedans** (elle prend le clavier), puis F5, F6 tenue, Haut tenue : les valeurs suivent.
5. Ouvrez une popup « Saisie d'une consigne » créée avec cette version, tapez une valeur et cliquez **Valider** sans appuyer sur Entrée : la valeur est écrite. Pour une popup créée avant, ajoutez « Valider les saisies » en tête des actions de son bouton Valider.
6. Dans l'explorateur, cliquez la puce **À générer**, puis encore : l'arbre filtre, puis revient. Survolez le titre IHM : « Générer l'IHM ». Le **?** du rail ouvre la légende.

## Installer

1. Lancez `XPGAnalyser-Setup-1.11.23.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.23.exe` (15,3 Mo, 16 049 262 octets) :
`A9AC10BE569E1D5DAED8AA2AE16B72F94C0B2A2484B22F3712C9EC168031C385`

Le zip portable `XPGAnalyser-1.11.23-portable.zip` (20,5 Mo) contient le même exe et les mêmes fichiers que l'installateur, dans un dossier `XPGAnalyser-1.11.23`. Décompressez-le, puis lancez `XpgAnalyzer.exe`. Son empreinte SHA-256 :
`90419DB695A723993FC010746A8A3DD468B23FF1B38309D0002A50843DFE1A3F`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (66 sur 66). Elle comprend les 6 614 contrôles de `hmieditor` (31 de plus qu'en 1.11.22) et les 4 728 de `hmi` (44 de plus), ainsi que les deux traces de référence, `hmitrace` et `hmitracemig`.

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.23` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session du § 6 a été rejouée avec l'exe livré : 7 captures `11123_*`, aucun contrôle en échec.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé pour tous les comptes, par-dessus la 1.11.22 (puis par-dessus lui-même, après la correction du badge) : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.23`.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. La section « Tester vous-même » donne le parcours à refaire chez vous.

## La suite

- les raccourcis clavier des éditeurs (le § 14 de la spécification) ;
- les titres de domaine sur deux lignes et le pied de santé redessiné, comme dans la maquette ;
- l'aide à la saisie après `SYS.Key.`, et le renommage qui suit les noms comparés à `SYS.MouseObject`.
