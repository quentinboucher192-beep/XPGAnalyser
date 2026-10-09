# XPGAnalyser 1.11.24 : les repères dans les symboles

Livrée le 09/10/2026 en fin de soirée. Elle répond à votre message, avec la capture de la Console (« saisie : variable inconnue (V[0]).IN_Percent », source `Vue_Accueil/Symbole_1.InputField_1`) :
- « gros bug avec les repères : essaie de faire des symboles ou même des objets de la bibliothèque, teste chaque paramètre, chaque option avec des repères ; dans le symbole, fais tout ce qui est possible pour hériter des repères, et duplique ; vérifie tous les cas de figure, constate et corrige les erreurs en simulation » ;
- « en simulation, à droite, enlever les onglets Recettes, Alarmes, Journal » ;
- « quand tu auras trouvé tous les bugs, liste-les, dis-les-moi et corrige ».

La liste vous a été donnée dans la conversation ; elle est reprise ici (§ 1), avec ce qui a été corrigé et comment.

**Un projet de la 1.11.23 s'ouvre tel quel** : le format ne change pas.

Fichiers livrés :
- `XPGAnalyser-Setup-1.11.24.exe` (l'installateur) ;
- `XPGAnalyser-1.11.24-portable.zip` (le même programme, sans installation) ;
- ce document ;
- les captures `11124_*`, prises sous Wine avec l'exe livré.

## 1. Les bugs trouvés, tous corrigés

Une campagne d'essais automatisée (§ 2) : 258 contrôles. Sur le code de la 1.11.23, **171 échouaient** ; sur la 1.11.24, aucun.

**En simulation : ce qui ne s'écrivait pas**

1. **Votre capture.** Un argument d'instance à repère (`Cuve := $V[0]$`) était collé entre parenthèses dans le symbole : `(V[0]).IN_Percent`. Cette forme se lisait (l'affichage était juste), mais ne s'écrivait pas. Le champ de saisie, Incrémenter, Basculer, Affecter, l'interrupteur, le curseur, le clavier virtuel et Maths échouaient tous (« variable inconnue », « écriture refusée »). Même chose pour une structure IHM (`$Cuve_A$`) ou de l'automate (`$W[0]$`).
2. **L'héritage d'un repère.** Un symbole posé dans un symbole, qui reçoit `M := $Cuve$`, produisait `$($V[0]$)$`. Les `$` s'imbriquaient, l'expression devenait illisible : affichage `###`, écritures en échec.
3. **Les popups ouvertes depuis l'instance.** La popup du symbole et une popup du projet ouverte avec `C := Cuve` recevaient `(V[0])` et refusaient la référence (« `'__hmi_r.IN_Percent' cannot be written` »). Leurs champs et leurs boutons n'écrivaient rien.
4. **Un indice calculé** (`Cuve := V[$Idx$]`, ou `Tab[I]` avec `I := $Idx$`) : `V[Idx].IN_Percent` ne s'écrivait pas. Le ST, lui, calcule l'indice avant d'écrire ; une saisie, une commande ou l'action d'un bouton ne le faisaient pas.
5. **Des repères dans le symbole lui-même**, autour de ses paramètres (`$Cuve$.IN_Percent`, `$Cuve.Consigne$`, `Tab[$I$]`, `=$Cuve$.Marche`) : même échec à l'écriture.
6. **La borne max d'un champ de saisie égale à un paramètre** du symbole (`max := Haut`) : la borne restait « Haut », illisible, et elle était ignorée. Une valeur au-dessus passait.
7. **Le clavier virtuel d'une popup à paramètre** (cible `C.Niveau`, `C` en Référence) : « écriture refusée : C.Niveau ». Le clavier restait alors ouvert et prenait les frappes suivantes. Ce défaut n'est pas lié aux repères ; il est apparu pendant la campagne.

**À Compiler et à Générer : de fausses fautes**

8. **Un repère d'une seule lettre** (`$V$`, `Tab := $V$`, `Cuve := $V$[2]`, `Tab[$I$]`) était refusé : « caractère inattendu : '$' — un repère s'écrit entre deux $ ». C'est maintenant un repère. La règle « au moins deux caractères » devient « au moins deux caractères, ou une lettre seule » (le guide est à jour).
9. **Les arguments positionnels** d'une instance (`$V[3]$; 'C3'; 90.0`) : Générer disait « paramètre sans valeur ».
10. **La popup d'une instance** : une action « Ouvrir une popup » vers `Sym_0.Pop_Cuve`, ou `IHM_POPUP('Vue_Test.Sym_0.Pop_Cuve')` dans un script, étaient dites « vue introuvable » alors que le moteur les ouvre (depuis la 1.11.10).
11. **La variable d'un conteneur à onglets, d'un panneau repliable, d'un plan à zones** : « variable inexistante » dès qu'elle portait un repère (`$Reels[2]$`).
12. **Un repère autour d'un membre** (`$V[0]$.$Nom$`) : le membre était pris pour une variable (« variable inexistante : Nom »).

**Testé sans défaut trouvé** : les alarmes, les fonctions, les scripts et les redéfinitions d'un symbole ; deux et trois niveaux de symboles imbriqués ; Dupliquer une instance, et Dupliquer dans un symbole ; les popups en Copie et Les deux, avec Appliquer ; Naviguer avec des arguments ; les courbes et les tableaux ; et, objet par objet de la bibliothèque, chaque propriété avec et sans repère (§ 2).

**Votre deuxième demande** : la simulation de l'IHM n'a plus les onglets **Journal**, **Alarmes** et **Recettes** (§ 4).

## 2. La campagne d'essais

Elle est dans `tests/hmi_reperes_test.cpp` (le nouvel essai CTest `hmireperes`). L'essai construit un projet, le passe à Compiler et à Générer, puis le fait tourner dans le moteur de l'IHM, celui de la simulation. Il tape dans les champs, appuie sur les boutons, ouvre les popups et lit ce que chaque objet affiche. Chaque contrôle dit ce qu'il attendait et ce qu'il a obtenu.

**Le projet.** Un type IHM `T_Cuve` (IN_Percent, Niveau, Marche, Nom, Consigne) ; les variables IHM `V : ARRAY[0..3] OF T_Cuve`, `Cuve_A : T_Cuve` et `Idx` ; dans l'automate, `W[0..1]`, du même type. Deux symboles :
- `S_Mini (M : T_Cuve)` : un champ `M.Consigne`, un bouton +1, un texte ;
- `S_Cuve (Cuve : T_Cuve ; Titre : STRING ; Haut : REAL ; Tab : ARRAY OF T_Cuve ; I : INT)`, où chaque paramètre sert de toutes les façons possibles :
  - un texte à trous ;
  - un champ de saisie avec ses bornes (`max := Haut`) ;
  - des boutons Incrémenter, Basculer, Affecter et un script ;
  - un voyant, une barre, un interrupteur, un curseur ;
  - `Tab[I]` ;
  - une popup du symbole, et une popup du projet ouverte avec `C := Cuve` ;
  - trois `S_Mini`, posés avec `M := Cuve`, `M := $Cuve$` et `M := Tab[I]`.

**Sept instances de S_Cuve**, une par forme d'argument :

| Instance | Arguments | Ce qu'elle essaie |
|---|---|---|
| Sym_0 | `Cuve := $V[0]$; Titre := '$Nom$'; Haut := 80.0; Tab := V; I := 0` | votre cas |
| Sym_1 | `Cuve := V[1]; Tab := $V$; I := $Idx$` | un tableau entier, un indice à repère |
| Sym_2 | `Cuve := $V$[2]; Tab := V; I := 2` | un repère d'une lettre suivi d'un indice |
| Sym_3 | `$V[3]$; 'C3'; 90.0; V; 3` | les arguments positionnels |
| Sym_A | `Cuve := $Cuve_A$; …` | une structure de l'IHM |
| Sym_W | `Cuve := $W[0]$; …` | une variable de l'automate |
| Sym_X | `Cuve := V[$Idx$]; …` | un indice calculé |

Pour chacune, Compiler et Générer ne doivent rien signaler. En marche, chaque champ, chaque bouton, chaque commande et chaque popup doit écrire dans la bonne cuve, et chaque objet doit montrer la bonne valeur.

**Puis :**
- **des repères dans le symbole lui-même** (le symbole `S_Rep`) : `$Cuve$.IN_Percent`, `$Cuve.Consigne$`, `Tab[$I$]`, `{$Cuve.Nom$}` dans un texte, une variable globale entre `$` (`$Idx$`), et les valeurs par défaut des paramètres ;
- **les alarmes, les fonctions, les scripts et les redéfinitions** d'un symbole dont l'instance reçoit des repères ;
- **deux et trois niveaux** : `S_Ligne (L : ARRAY OF T_Cuve ; K : INT)` pose deux `S_Cuve`, avec `Cuve := L[K]` et `Cuve := $L[0]$`, et chaque S_Cuve pose ses S_Mini ;
- **Dupliquer** une instance, en remplaçant ses repères par d'autres cuves ; et **Dupliquer dans un symbole** : un champ qui écrit `$Cuve$.IN_Percent`, dupliqué avec Cuve → `Tab[1]`, puis `Tab[2]` ;
- **la bibliothèque, objet par objet** : chacun des 101 objets que l'on peut poser, et chacune de ses propriétés qui peut se calculer (sa variable, son texte, ses nombres, ses cases à cocher, ses couleurs : 1 620 en tout), reçoit une formule avec repère, puis la même formule sans repère. Tout ce que Compiler, Générer ou l'animation disent en plus avec les repères compte comme une faute. Ce contrôle est fait deux fois : dans une vue (`$Reels[1]$`, `$Flags[0]$`), et dans un symbole (ses paramètres `$P$` et `$B$`) ;
- **les popups et les actions** :
  - une popup en Copie et en Les deux, avec Appliquer ;
  - Naviguer avec des arguments ;
  - le clavier virtuel, Maths, une courbe, les cellules d'un tableau ;
  - la popup d'une instance, ouverte depuis la vue (`Sym_0.Pop_Cuve`) et par un script (`IHM_POPUP`) ;
- **un symbole dans une popup, et une redéfinition** : `Pop_Ligne (C : T_Cuve)` pose un `S_Mini` avec `M := $C$`, et `Sym_0` redéfinit une fonction de son symbole.

**Le résultat** : 258 contrôles. Sur le code de la 1.11.23, 171 échouaient, tous à cause des 12 bugs du § 1. Sur la 1.11.24, aucun n'échoue. Avec la variable d'environnement `HMI_REPERES_OBS=1`, l'essai écrit aussi les contrôles réussis, pas seulement les échecs.

## 3. Les corrections, dans le code

- **Le développement d'une instance** (`src/hmi/HmiSymbols.cpp`) :
  - `substituteParams` retire les `$` de l'argument avant de le coller dans le symbole. Un chemin reste un chemin, sans parenthèses : `Cuve.IN_Percent` devient `V[0].IN_Percent`, plus `(V[0]).IN_Percent`, ni `($V[0]$).IN_Percent` (bugs 1, 2, 3) ;
  - `expandInstance` retire les repères du symbole lui-même avant le remplacement : `$Cuve$.IN_Percent` devient `V[0].IN_Percent`, et `Tab[$I$]` avec `I := 3` devient `Tab[3]`, plus `Tab[$3$]` (bug 5) ;
  - `rewriteNames` remplace aussi les paramètres dans les bornes `min` et `max` d'un champ de saisie (bug 6).
- **Le moteur** (`src/hmi/HmiRuntime.cpp`) : `Env::concretePath` met un chemin « en clair » avant de le lire, de l'écrire ou de tester s'il existe. Il retire les parenthèses autour d'un chemin et calcule chaque indice qui n'est pas un nombre : `V[Idx].IN_Percent` devient `V[2].IN_Percent`, y compris dans un tableau à plusieurs dimensions. Le clavier virtuel prend sa cible en clair à l'ouverture : `C.Niveau` (le paramètre de la popup) devient `V[0].Niveau`. Avant, il cherchait `C.Niveau` à la validation, hors de la popup (bugs 4 et 7).
- **Les repères** (`src/hmi/HmiMarkers.cpp`) : une lettre seule est un repère (bug 8). Le chiffre seul (`$1$`) n'en est toujours pas un.
- **Compiler et Générer** (`src/hmi/HmiCheck.cpp`) :
  - les arguments positionnels comptent, comme dans le moteur (bug 9) ;
  - la popup d'une instance est trouvée, pour une action comme pour `IHM_POPUP` (bug 10) ;
  - la variable d'un conteneur à onglets, d'un panneau repliable ou d'un plan à zones est lue sans ses `$` (bug 11).
- **Les noms d'une expression** (`src/hmi/HmiExpr.cpp`, `scanRoots`) : un nom précédé de `.` reste un membre, même avec un `$` entre les deux, comme dans `$V[0]$.$Nom$` (bug 12).

## 4. Les onglets retirés de la simulation

Le panneau de droite de Simulation › IHM garde **Expressions, Variables IHM, Variables API, Performances, Esclaves simulés et Popups** (capture `11124_05`). Ce que faisaient les trois onglets retirés :
- **Journal** : les lignes du journal de la séance vont à la **Console** du panneau du bas, avec le niveau, la source, la recherche, les filtres et l'export. Le bouton « Vider le journal de la séance » n'existe plus. La poubelle de la Console vide la Console, et le journal de la séance repart de zéro quand l'IHM redémarre.
- **Alarmes** : le double-clic qui acquittait une alarme disparaît. Acquittez dans la vue, avec ses objets d'alarme, ou avec **Tout acquitter** (clic droit sur la barre de la simulation). La Console dit quand une alarme apparaît. Dans le journal du Centre de simulation, « aller à » sur une ligne « Alarme : … » ouvre maintenant la Console, au lieu de l'onglet retiré.
- **Recettes** : les jeux se chargent avec les objets de recettes des vues, plus depuis le panneau de droite.

## 5. Le parcours vérifié sous Wine

Une session jouée avec l'exe livré (`tools/sessions/session-11124-reperes.txt`), sur la copie d'Armoire_Gaz des livraisons précédentes (`preparer-projet-11120`, `-11121` et `-11123`). `tools/sessions/preparer-projet-11124.cpp` y ajoute le type `T_Cuve`, la variable `Cuves : ARRAY[0..3] OF T_Cuve` (ce projet a déjà une variable `V`, un tableau de vannes) et `Idx` (2), ainsi que deux symboles :
- `S_Mini (M : T_Cuve)` : un champ `M.Consigne` et un texte ;
- `S_Cuve (Cuve, Titre, Haut)` : un texte, `InputField_1` (`Cuve.IN_Percent`, de 0 à `Haut`), un bouton `Btn_Plus` (Consigne + 1), et `Mini`, un S_Mini posé avec **`M := $Cuve$`** (le repère hérité).

La vue **Vue_Reperes** pose trois S_Cuve : `Symbole_1` (`Cuve := $Cuves[0]$; Titre := '$Nom$'; Haut := 80.0` : la forme de votre capture), `Symbole_2` (`Cuve := $Cuves[1]$; … Haut := 80.0`) et `Symbole_3` (`Cuve := Cuves[$Idx$]` : un indice calculé).

| Étape | Ce qui s'est passé | Capture |
|---|---|---|
| **1.** Vue_Reperes ouverte, Symbole_1 choisi | L'explorateur d'objets montre Symbole_1, Symbole_2 et Symbole_3, chacun avec sa pastille **$**. L'inspecteur commence par la ligne « Repères » (`$Cuves[0]$ × …`). Le dessin montre les trois instances développées : « {Cuves[0].IN_Percent} », « {Cuves[Idx].IN_Percent} » | `11124_01` |
| **2.** **Compiler** | « Build réussi : 0 erreur, 53 avertissements » (339 générés, 254 compilés ; les avertissements sont ceux du projet de démonstration). Les Diagnostics, filtrés sur `S_Cuve`, n'ont aucune ligne | `11124_02` |
| **3.** Simulation › IHM, Vue_Reperes. Dans Symbole_1 : `42` dans le champ, deux clics sur Consigne + 1, `7` dans le champ de Mini. Dans Symbole_3 : `55`. Dans Symbole_2 : `95` | Symbole_1 affiche « Nom : 42.0 % · consigne 7 » et Mini « consigne 7 » ; Symbole_3, « Cuve Idx : 55.0 % » ; sous le champ de Symbole_2, en rouge, « hors bornes : de 0 à 80 » (Haut vaut 80 : le 95 est refusé). À droite, les Expressions montrent les mêmes valeurs. La Console : « saisie : Cuves[0].IN_Percent = 42 », « Cuves[0].Consigne : 0 → 1 », « 1 → 2 », « saisie : Cuves[0].Consigne = 7 », « saisie : Cuves[Idx].IN_Percent = 55 ». Session 1 : 44 lignes, **0 erreur, 0 avertissement** | `11124_03` |
| **4.** La recherche `saisie` dans la Console | Les trois saisies, chacune avec sa source (`Vue_Reperes/Symbole_1.InputField_1`, `…Symbole_1.Mini.Saisie_M`, `…Symbole_3.InputField_1`). Plus de « variable inconnue » | `11124_04` |
| **5.** La liste des onglets du panneau de droite | Expressions, Variables IHM (585), Variables API (63 983), Performances, Esclaves simulés, Popups. Plus de Journal, d'Alarmes ni de Recettes | `11124_05` |

Les contrôles de la session (`ihm-build-etat`, `ihm-sim-etat`, `ihm-console-etat`) n'ont signalé aucun échec. Le même projet a aussi été vérifié hors interface : `Cuves[0].IN_Percent` vaut 42, `Cuves[2].IN_Percent` vaut 55, et `Cuves[1].IN_Percent` est resté à 0.

**Ce que la session a montré.** À la première exécution, l'explorateur d'objets montrait « … » à la place du nom des trois instances. Le genre de l'objet, « Instance de symbole », prenait toute la place dans le panneau étroit, et le nom était coupé. C'est un défaut d'affichage plus ancien que la 1.11.24. Maintenant, le nom passe d'abord ; le genre prend la place qui reste, coupé ou omis (`src/app/hmi/HmiPanels.cpp`). L'exe a été recompilé et la session rejouée : la capture 1 montre les noms.

## 6. Fichiers modifiés

- les repères dans les symboles : `HmiSymbols.cpp`, `HmiRuntime.cpp`, `HmiMarkers.cpp` et `HmiMarkers.hpp`, `HmiCheck.cpp`, `HmiExpr.cpp` ;
- la simulation : `HmiSimulation.cpp` et `HmiSimulation.hpp` (les trois onglets retirés), `SimulationWorkspace.cpp` (le raccourci des alarmes ouvre la Console) ;
- l'explorateur d'objets : `HmiPanels.cpp` (le nom d'abord) ;
- le guide (la règle des repères, « Des repères dans les arguments et dans le symbole », les onglets de la simulation), les notes (3), la version 1.11.24 ;
- la session Wine : `tools/sessions/session-11124-reperes.txt` et `tools/sessions/preparer-projet-11124.cpp` ;
- les essais : `hmi_reperes_test.cpp` (nouveau), `hmi_test.cpp`, `hmi_editor_test.cpp`, et `CMakeLists.txt` (l'essai `hmireperes`).

## 7. Les tests ajoutés ou changés

- **`hmireperes`** (nouveau, 258 contrôles) : la campagne du § 2.
- **`hmi`** (2 de plus) : une lettre seule est un repère (`$A$`, et `$V$[2].Nom + $I$` qui se lit `V[2].Nom + I`), un chiffre seul non (`$1$`) ; Dupliquer qui remplace un repère par `A` écrit `$A$`, par `7` écrit `7`.
- **`hmieditor`** (2 de moins) : la liste des onglets de la simulation (« Expressions | Variables IHM | Variables API | Performances | Esclaves simulés | Popups ») ; plus d'onglet Alarmes ni Journal ; Vider le journal garde son effet (le journal de la séance se vide, l'historique non). Les contrôles qui cliquaient dans l'onglet Journal sont partis avec lui.

## 8. Les limites et ce qui change aussi

- **Rien n'a été lancé sur un vrai Windows** : tout a été vérifié sous Wine.
- **Un texte qui contenait `$A$`** (une lettre entre deux `$`) affiche maintenant « A », puisque c'est un repère. Pour garder « $A$ » à l'écran, écrivez `$$A$`.
- **Dupliquer** : remplacer un repère par une lettre seule (`A`) écrit maintenant `$A$`, un repère, comme pour un nom plus long ; avant, la copie écrivait `A`.
- **La Console écrit le chemin tel qu'il est écrit dans le symbole** : « saisie : Cuves[Idx].IN_Percent = 55 », pas la case réelle (`Cuves[2]`). La valeur est écrite dans la bonne case.
- **L'explorateur d'objets** : dans un panneau étroit, le genre de l'objet (« Instance de symbole ») est coupé ou omis pour laisser la place au nom.
- **Les raccourcis clavier des éditeurs** (le § 14 de la spécification de la refonte des scripts) restent à faire.
- **La source du guide Word** (`tools/guide-ihm/guide-ihm.txt`) n'a toujours pas suivi les versions depuis la 1.11.2 : l'aide F1 (le C++) a été modifiée directement.

## Tester vous-même

1. Ouvrez votre projet, celui de la capture : `Vue_Accueil`, `Symbole_1` et son `InputField_1`. Lancez **Simulation › IHM**, tapez une valeur dans le champ, puis Entrée. La Console dit « saisie : V[0].IN_Percent = … », sans erreur.
2. Dans un symbole, posez un autre symbole et donnez-lui `M := $Cuve$`, où `Cuve` est un paramètre du premier. **Compiler** ne dit rien ; en simulation, le champ du symbole intérieur écrit dans la cuve de l'instance.
3. Donnez à une instance `Cuve := V[$Idx$]`. Changez `Idx` en simulation (onglet Variables IHM) : le champ écrit dans la nouvelle case.
4. Mettez `max` d'un champ de saisie du symbole égal à un paramètre (`Haut`), puis tapez une valeur plus grande : « hors bornes ».
5. Sur un bouton de la vue, ajoutez l'action « Ouvrir une popup » vers `Symbole_1.<sa popup>`. **Générer** ne dit plus « vue introuvable ».
6. Dans le panneau de droite de la simulation, ouvrez la liste des onglets (le bouton ≡) : il n'y a plus Journal, Alarmes ni Recettes.

## Installer

1. Lancez `XPGAnalyser-Setup-1.11.24.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.24.exe` (15,3 Mo, 16 048 123 octets) :
`B8EACDB1842706C800DF69ABD2221AFA88C96838755B8C752AA8DD1B1DDA9E64`

Le zip portable `XPGAnalyser-1.11.24-portable.zip` (20,5 Mo) contient le même exe et les mêmes fichiers que l'installateur, dans un dossier `XPGAnalyser-1.11.24`. Décompressez-le, puis lancez `XpgAnalyzer.exe`. Son empreinte SHA-256 :
`9EADE84E1C9F301241EB53353DD2AA6C6129C8599103121412C0A02C3FF246B3`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (67 sur 67, avec le nouvel essai `hmireperes`). Elle comprend les 258 contrôles de `hmireperes`, les 6 612 de `hmieditor` et les 4 730 de `hmi`, ainsi que les deux traces de référence, `hmitrace` et `hmitracemig`.

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.24` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session du § 5 a été rejouée avec l'exe livré : 5 captures `11124_*`, aucun contrôle en échec.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé pour tous les comptes, par-dessus la 1.11.23, puis par-dessus lui-même après la correction de l'explorateur : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.24`.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. La section « Tester vous-même » donne le parcours à refaire chez vous.

## La suite

- les raccourcis clavier des éditeurs (le § 14 de la spécification) ;
- la Console qui écrirait la case réelle d'un indice calculé (`Cuves[2]` plutôt que `Cuves[Idx]`) ;
- les titres de domaine sur deux lignes et le pied de santé redessiné, comme dans la maquette de l'explorateur.
