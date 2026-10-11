# La bibliothèque d'objets de XPGAnalyser IHM : audit des paramètres (1.12.4)

Rédigé le 11/10/2026. Il répond à cette demande du 11/10 :

> « regarde la bibliothèque d'objets, regarde chaque paramètre, et regarde si c'est facile à configurer pour un cas pratique, liste les paramètres qui ne sont pas faciles à configurer et propose la solution pour faciliter cela, sachant qu'il faut s'adapter à n'importe quel type en entrée, liste les types autorisés pour les tailles requises dynamiques, envoie un récap de chaque objet avec les façons d'améliorer si cela peut, ne jamais oublier de gérer les repères etc »

Rien n'a été modifié dans le logiciel : ce document est un état des lieux et des propositions. Les défauts trouvés en chemin sont listés à part (§ 1.2), avec ce qui a été vérifié.

## 0. Comment ce document a été fait, et comment le lire

**Le périmètre.** La palette de XPGAnalyser IHM compte 99 objets, en 10 rubriques. S'y ajoutent l'instance de symbole (posée depuis la section Symboles) et le groupe (né de Grouper). Les deux objets « Communication » (État de la communication, Diagnostic automate) n'existent que dans XPGAnalyser API : ils sont hors sujet ici.

**La méthode.** Lecture du code, objet par objet :
- les valeurs par défaut (`makeObject`, HmiModel.cpp) ;
- le catalogue des propriétés (libellé, genre de case, aide : `keyInfos`, HmiPanels.cpp) ;
- l'inspecteur et l'onglet Contenu ;
- le moteur (ce qui est lu, converti, écrit) et le dessin ;
- les vérifications de Générer ;
- les repères `$…$` ;
- les essais existants.

Six examens en parallèle, un par famille d'objets plus un sur les mécanismes communs. Les points les plus graves ont été revérifiés ligne par ligne. Chaque défaut porte un statut :
- **vérifié** : lu et confirmé dans le code ;
- **probable** : le chemin du code y mène, mais il reste à le confirmer par un essai.

**Le vocabulaire.**
- *Valeur fixe* : ce qu'on tape dans une case de l'inspecteur.
- *Formule (fx)* : une case qui commence par `=`. Depuis la 1.10, toute case modifiable en accepte une.
- *Repère* : un morceau `$…$` d'un texte ou d'une formule. Il ne change pas le calcul ; il dit à **Dupliquer…** ce qui varie d'une copie à l'autre.
- *Contenu* : l'onglet de l'inspecteur qui édite en tableau ce qu'un objet contient (cases, plumes, états, zones, pages, vues).
- *Liste parallèle* : deux propriétés ou plus qui doivent avoir le même nombre d'éléments, dans le même ordre (par exemple `variables`, `names`, `colors`).

## 1. Ce qu'il faut retenir

### 1.1 Les neuf problèmes de fond

La plupart des objets se règlent bien pour un cas simple : une variable, deux couleurs, un libellé. Les difficultés viennent toujours des mêmes neuf mécanismes. Chacun se corrige une fois, pour tous les objets.

| # | Le problème | Ce que voit l'utilisateur | La solution proposée |
|---|---|---|---|
| P1 | **Les listes tapées `a;b;c` et les listes parallèles.** Le découpage sur `;` est naïf : il coupe dans une chaîne `'a;b'`, une clé de MAP `M['a;b']`, un repère `$A;B$`. Les longueurs ne sont vérifiées nulle part, sauf pour la consigne du radar. | Les noms, couleurs, unités et formats décalés d'un cran. Une plume coupée en deux. Rien dans Générer. | **Un seul découpeur**, qui respecte les chaînes, les crochets et les repères, utilisé partout (moteur, dessin, Générer, Dupliquer). **Un tableau (une ligne par élément)** dans Contenu pour chaque liste. **Générer vérifie l'alignement.** (§ 2.4) |
| P2 | **Pas de taille variable**, sauf « Éléments depuis » (Liste, Liste déroulante, Boutons radio, Sélecteur) et « Lignes depuis » (Tableau). | Pour 8 cuves, on tape 8 plumes, 8 noms, 8 couleurs. Un tableau `ARRAY`, une `LIST` ou une `MAP` ne peut pas nourrir une courbe, un graphique, un multi-états, une barre d'onglets. | **Une notion unique de source**, et un **joker `[*]`** (`Cuves[*].Niveau`) : une plume, une barre ou une ligne par élément. La règle des types acceptés est au § 2.3. |
| P3 | **Les types en entrée.** Chaque résultat de formule est changé en texte (les REAL avec 6 chiffres significatifs), puis relu par l'objet. Selon l'objet, BOOL, TIME, DATE, une STRING non numérique, une structure ou une liste donnent 0, FAUX, un point perdu ou `###`, sans rien dire. Une énumération montre son numéro, jamais son texte. | Une jauge à 0 sur un BOOL. Un histogramme de temps de cycle (TIME) qui ne se remplit jamais. Un moteur en DÉFAUT dessiné vert et tournant. 1 234 567,8 montré 1 234 570. | **Un adaptateur par rôle de propriété** (Nombre, Booléen, État, Texte, Durée, Date, Couleur, Nom), qui reçoit la valeur elle-même, pas son texte. **Générer prévient** quand le type ne convient pas. (§ 2.1) |
| P4 | **Écrire dans une variable.** Pas de bornes. Un texte vers un nombre donne 0. Une énumération n'est pas reconnue par son nom. Le sélecteur de date écrit des secondes dans une DATE comptée en millisecondes. | 40 000 écrit dans un INT devient −25 536. « Auto » tapé écrit 0. Une date enregistrée en 1970. | **La conversion vers le type de la variable écrite** : arrondi, bornes du type et de l'objet, nom d'énumération, durée `T#5s` ou `5 s`, date en millisecondes. Un message dans la Console quand on borne ou qu'on refuse. (§ 2.2) |
| P5 | **Ce que l'écran montre n'est pas ce que le moteur fait.** Une formule sur une propriété lue par un clic ou par le moteur ne sert qu'au dessin. Sont concernés : le groupe d'un historique ou d'un bandeau d'alarme, la source et le format d'un export, les vues d'une barre de navigation, les onglets, les langues, les réglages d'un GIF, la vue après connexion… De plus, le premier geste de l'opérateur efface la formule d'un onglet montré, d'un panneau replié ou d'un défilement. | « Acquitter » qui acquitte une autre alarme que celle affichée (probable). Un clic qui navigue vers une autre vue que le bouton dessiné. | **Une seule copie évaluée de l'objet**, lue par le dessin, le clic et le moteur. Un geste écrit dans la variable liée, jamais sur la formule. (§ 2.7) |
| P6 | **Les repères `$…$`.** Dupliquer les trouve partout, mais ils ne sont retirés que dans les formules et les textes à trous de la propriété « Texte ». | Dans les listes, libellés, titres, noms de vues, ressources, langues, groupes, chemins d'unité et de format : `vue '$Vue$' introuvable`, un libellé affiché avec ses `$`, une ligne de tableau de variables qui devient en lecture seule. | **Une règle** : toute lecture d'une propriété passe par un lecteur qui retire les repères selon le rôle de la propriété. Dupliquer lit chaque propriété dans le même mode que le moteur. Un essai par propriété. (§ 2.8) |
| P7 | **Taper des noms au lieu de les choisir.** Vues, groupes d'alarmes, alarmes d'objets, langues, boutons, vue après connexion. | Une faute de frappe, un renommage non suivi (la vue après connexion n'est pas mise à jour quand on renomme la vue). | **Des listes à choix** dans l'inspecteur et Contenu, le **renommage suivi** partout, et Générer qui nomme ce qui manque. (§ 2.5) |
| P8 | **Plages, seuils, unités : à la main, objet par objet.** Une variable a une unité et un format (Configuration › Unités et formats), mais pas de plage. | La même plage 0–10 bar retapée sur la jauge, le bargraphe, la courbe et le curseur. Un seuil hors plage qui disparaît sans rien dire. | **Min, max et seuils dans « Unités et formats »**, avec par défaut la mise à l'échelle d'une variable liée. Les objets en héritent comme de l'unité, sauf si on les règle à la main. (§ 2.6) |
| P9 | **Générer et le type attendu.** Trois tables différentes disent le type attendu d'une propriété : l'inspecteur, la vérification des formules, les vérifications au cas par cas. `value`, `fault`, `state`, `lamp`, `running`… ne sont pas typées. Les couleurs `colorOn`/`colorOff`/`colorFault` ne sont pas vues comme des couleurs. | Une STRING sur une jauge, une structure sur une pompe, un TIME sur un histogramme : Générer ne dit rien. | **Une table unique du rôle de chaque propriété**, lue par l'inspecteur, Générer et le moteur. (§ 2.9) |

### 1.2 Les défauts trouvés pendant l'audit

À corriger quel que soit le plan retenu. Ils sont classés par gravité.

| # | Défaut | Statut | Où |
|---|---|---|---|
| D1 | **Sélecteur de date et heure lié à une DATE, une TOD ou une DATE_AND_TIME :** il écrit des secondes (heure locale) dans un type compté en millisecondes (heure du mur, sans fuseau), et relit de même. La date enregistrée est fausse. | vérifié | HmiRuntimeControls.cpp:347-354, 739-751 ; HmiControls.cpp:574-584 ; Value.hpp:24-27 |
| D2 | **Bandeau d'alarme, Historique, Résumé par zone :** quand le groupe est une formule, le dessin choisit les alarmes avec le groupe calculé, mais le clic et « Acquitter » utilisent le groupe fixe. L'exemple livré utilise justement une formule (`Zone_Choisie`). Conséquence : on peut acquitter ou choisir une autre alarme que celle affichée. | probable (chemins lus des deux côtés ; à confirmer par un essai) | HmiRuntimeLot11.cpp:560-592 ; HmiLot11Painter.cpp:88 ; HmiExamples.cpp:1599 |
| D3 | **Bouton d'export :** une formule sur la source, le format ou « Demander où enregistrer » est ignorée au clic, qui lit la valeur fixe. | vérifié | HmiRuntime.cpp:3005-3006 |
| D4 | **Les REAL ne gardent que 6 chiffres significatifs** dès qu'ils passent par un objet (`%.6g`). Effets : graphiques et tableau de variables arrondis ; recettes qui enregistrent une valeur arrondie et montrent ensuite un faux « Écart » ; compteurs de production imprécis au-delà de 999 999. | vérifié | HmiExpr.cpp:380-384 |
| D5 | **Instrument ISA :** la clé `loop` (numéro de boucle) est typée BOOL. C'est un reste d'une ancienne propriété « lire en boucle » de la vidéo, qui n'existe plus. Une formule texte sur la boucle est refusée par l'inspecteur et par Générer, et `Vue.Objet.Loop` se lit FAUX. | vérifié | HmiPublicVars.hpp:553 |
| D6 | **Le titre d'un Cadre et d'un Panneau repliable** est annoncé comme un « texte à trous » (`Pompe {Nom}`), mais seule la propriété « Texte » est remplie en marche. Les accolades s'affichent ; Générer ne les vérifie pas. Même chose pour le libellé des symboles de synoptique, présenté « texte, {variable} ». | vérifié | HmiLive.cpp:82 ; HmiCheck.cpp:67 |
| D7 | **Conteneur (et Groupe) :** caché (Visible faux) ou rendu transparent, il ne cache pas ses enfants, qui restent cliquables. Le guide promet le contraire. | vérifié | HmiNavigation.cpp:585-606 ; HmiGuideText.cpp:5674 |
| D8 | **Recettes :** une DATE est enregistrée `2026-10-09`, puis relue comme un calcul (2007). Une TOD ou une DATE_AND_TIME ne se relit pas, et tout le jeu est refusé. | probable | HmiRuntimeLot11.cpp:107-115 ; HmiRuntime.cpp:3398-3430 |
| D9 | **GIF animé, départ « sur condition » :** taper `=Marche` dans la condition range une formule et laisse le texte vide. Le GIF ne joue jamais, et Générer dit « la condition est vide ». | probable | HmiRuntimeLot16.cpp:99-173 ; HmiCheck.cpp:3564-3568 |
| D10 | **Compteur de production :** il ne compte que pendant que sa vue est affichée, puis ajoute toute l'absence d'un bloc. Un INT qui repasse de 32 767 à −32 768 est pris pour une remise à zéro. Deux ouvertures d'une même popup avec d'autres arguments partagent les mêmes compteurs. | probable | HmiRuntimeLot11.cpp:420-464 |
| D11 | **Tableau de variables :** une ligne écrite avec un repère (`$Cuve$.Consigne`) devient en lecture seule et perd son unité et son format. Retirer ou déplacer une ligne dans Contenu ne déplace pas son unité ni son format. | probable | HmiRuntimeLot11.cpp:608-615 ; HmiContentPanel.cpp:641-650, 729-737 |
| D12 | **Consigne d'alarme :** une alarme d'objet (1.9) n'est pas proposée, et Générer la dit « introuvable ». | vérifié par lecture | HmiCheck.cpp:1835-1838 ; HmiPanels.cpp:2780-2785 |
| D13 | **Valeur ou Défaut d'un symbole de synoptique tapé sans `=`** (`P1.Defaut`) : le dessin le lit FAUX, mais l'alarme par défaut de l'objet le prend pour une formule et se déclenche. L'objet ne montre jamais le défaut que l'alarme annonce. | probable | HmiObjectAlarms.cpp:78-87 ; HmiSynopticPainter.cpp:94-132 |
| D14 | **Courbe XY :** « escalier » est proposé, mais tracé en linéaire. | vérifié par lecture | HmiChartsPainter.cpp:423 |
| D15 | **Incrémenter une STRING la vide.** Le mode texte d'un champ de saisie écrit 0 dans une variable numérique. | probable | HmiRuntime.cpp:1768-1817, 4655-4714 |
| D16 | **Les types LINT, ULINT et LWORD** sont rangés sur 32 bits. | vérifié | HmiTypeRegistry.cpp:640-656 |
| D17 | **« Lignes depuis » un TUPLE** marche en simulation, mais Générer le refuse (fausse erreur). | probable | HmiCheck.cpp:837-852 |
| D18 | **Dupliquer, valeur d'une action :** la valeur des actions Affecter, Incrémenter, Décrémenter et Calcul est lue en mode texte au lieu du mode formule. Le code teste `Operation::Set` (dont la valeur ne sert pas) où `Assign` semble voulu. | probable | HmiDuplicate.cpp:327-329 |

Ces défauts s'ajoutent à ceux propres à chaque objet, signalés au § 3 :
- la barre de progression affiche « % » quels que soient min et max ;
- le voyant reste éteint pour un entier 2 ;
- l'interrupteur lié à une STRING ne repasse jamais à faux ;
- le compteur horaire repart de zéro au bout de 49,7 jours en TIME ;
- le guide promet des codes QR jusqu'à la version 40, alors que le code s'arrête à la version 10 ;
- etc.

## 2. Les règles communes proposées

### 2.1 S'adapter à n'importe quel type en entrée : un adaptateur par rôle

**Aujourd'hui.** Le résultat d'une formule est changé en texte (`formatValue`), puis chaque objet relit ce texte à sa façon :
- en nombre (`parseNumber`, qui accepte la virgule) ;
- en booléen (`parseBool` : TRUE, VRAI, 1, OUI) ;
- ou tel quel.

D'où trois défauts :
- les pertes de précision ;
- les erreurs silencieuses ;
- un même type qui donne des résultats différents selon l'objet. Un BOOL vaut 0 dans une jauge, « TRUE » dans un afficheur numérique, 1 dans un graphique.

**La proposition.**
- Chaque propriété a un **rôle**. L'objet reçoit la **valeur elle-même** (`sim::Value`, ou l'objet LIST, MAP…), pas son texte.
- Un adaptateur unique par rôle la convertit, avec les règles du tableau ci-dessous.
- Quand la conversion est impossible, l'objet le montre (hachures et `###`, comme l'afficheur numérique le fait déjà), au lieu de dessiner sa valeur par défaut comme si elle était vraie.
- Générer prévient dès que le type déclaré de la variable ne convient pas au rôle.

| Type en entrée | Rôle **Nombre** (jauge, bargraphe, barre, cadran, thermomètre, niveau de cuve, afficheur, courbes, graphiques) | Rôle **Booléen** (voyant, marche/défaut des symboles, visible, clignotement, conditions, panneau replié) | Rôle **État** (multi-états, chronogramme, sélecteur, vanne 3 voies, onglet montré, image animée) | Rôle **Texte** (texte, libellés, titres, cases, défilant, QR) |
|---|---|---|---|---|
| BOOL | aujourd'hui : 0 (jauge), « TRUE » (afficheur), 1/0 (graphiques) → **1 / 0 partout** | ✓ | 0 / 1 ✓ | « TRUE » → **le texte du format** (`{B:Oui\|Non}`), « Vrai/Faux » par défaut |
| Entiers | ✓ | ≠ 0 → vrai. Aujourd'hui le voyant ne s'allume que pour 1 → **≠ 0 partout** | ✓ | ✓ |
| REAL | ✓, mais **6 chiffres significatifs** → **précision entière** (la valeur, pas son texte) | ≠ 0 | plages `a..b` ✓ | format de la variable ou de l'objet |
| STRING | `'12,5'` ✓ ; `'abc'` → 0 ou point perdu en silence → **`###` + qualité mauvaise, et Générer prévient** | aujourd'hui toujours FAUX dans une condition, et seulement TRUE / VRAI / 1 / OUI dans une propriété → **TRUE / VRAI / 1 / OUI / ON vrais partout, le reste faux**, Générer prévient | comparée au texte de l'état ✓ | ✓ |
| TIME | 0 ou point perdu → **en secondes** par défaut (ou l'unité choisie : ms, s, min, h) | lu « T#…ms » dans une propriété (donc ignoré) → **≠ 0** | ✓ | « T#1500ms » → **« 1,5 s »** ou le format `{D:t}` |
| DATE, TOD, DT | 0 ou point perdu → **secondes depuis 1970 (DATE, DT) ou depuis minuit (TOD)** pour les courbes XY et les chronogrammes | refusé (Générer) | refusé (Générer) | « 2026-10-09 14:30:00 » → **le format local** (09/10/2026 14:30) |
| Énumération du projet | son numéro ✓ | ≠ 0 : un DÉFAUT est dessiné en marche → **refusé, sauf en choisissant la valeur « vraie »** (`Etat = T_ETAT#Marche`) | par numéro seulement → **par numéro ou par nom**, et **états générés depuis l'énumération** | son numéro → **son texte affiché** (`TO_STRING`), le numéro sur option |
| Énumération native | son numéro ✓ | ≠ 0 | ✓ | son mot (pour les propriétés à mots) ✓ |
| Structure entière | erreur de marche, objet figé sans signe → **refus par Générer, avec la liste des membres proposés** ; pour un symbole de synoptique, la liaison « Équipement » (§ 3.7) | idem | idem | **« {A: 1, B: 2} »** (son affichage) |
| ARRAY entier | erreur → **refus + index proposé** ; pour un objet à plusieurs éléments : la source `[*]` (§ 2.3) | idem | idem | **la liste de ses valeurs** (« 1 ; 2 ; 3 ») |
| LIST, VECTOR, MAP, TUPLE | erreur → **refus, avec `L.Count`, `L[0]`, `M['k']` proposés** ; pour un objet à plusieurs éléments : la source | idem | idem | **`JOIN` automatique** (« a, b, c ») |

Les rôles **Couleur** (une couleur ou une formule de couleur), **Nom** (vue, ressource, groupe, langue, recette, alarme) et **Durée** (compteur horaire) suivent le même principe. Une valeur qui ne convient pas se voit à l'écran, et Générer la signale.

### 2.2 Écrire dans une variable : convertir vers son type

Aujourd'hui l'écriture passe par `assignFrom` :
- un REAL vers un entier est arrondi ;
- **un texte vers un nombre donne 0** ;
- **il n'y a pas de bornes** : 40 000 écrit dans un INT devient −25 536 ;
- une énumération n'est pas reconnue par son nom ;
- une durée est lue comme un nombre de millisecondes.

| Variable écrite | Aujourd'hui | Proposé |
|---|---|---|
| INT, DINT, UINT, WORD… | arrondi, **sans bornes** ; texte → 0 | arrondi, **ramené dans les bornes du type et de l'objet** (min, max), avec un message dans la Console ; un texte numérique est lu, un autre texte est refusé |
| REAL | ✓ | ✓ (texte numérique lu, virgule acceptée) |
| BOOL | ✓ (VRAI, ON acceptés seulement au clavier virtuel) | TRUE / VRAI / 1 / OUI / ON partout |
| STRING | `display()` (« 2.5 », « T#5ms ») | **le format de la variable** (Unités et formats) |
| TIME | un nombre = des millisecondes ; « T#5s » non lu | `T#5s`, `5 s`, `00:05`, `1 h 30` lus ; un nombre nu suit l'unité de l'objet |
| DATE, TOD, DT | **le sélecteur écrit des secondes (défaut D1)** | millisecondes, heure du mur |
| Énumération | le rang ou un nombre ; « Auto » nu écrit 0 | **le nom** (`Auto`, `T_MODE#Auto`) ou le numéro, **refusé s'il n'existe pas** |
| STRUCT, ARRAY, LIST… | refus avec « variable inconnue : X » (trompeur) | refus avec le vrai motif : « Pompe est une structure : écris un de ses membres » |

Générer compare chaque commande avec le type de sa variable. Exemples :
- un interrupteur sur une STRING ;
- un programmateur dont la variable n'est pas une STRING ;
- un sélecteur de date sur un BOOL ;
- un champ de saisie en mode texte sur un REAL.

### 2.3 Les tailles dynamiques : les types autorisés

Aujourd'hui, seuls deux réglages acceptent une **source de taille variable** :
- « Éléments depuis » (`itemsFrom`) : Liste, Liste déroulante, Boutons radio, Sélecteur ;
- « Lignes depuis » (`rowsFrom`) : Tableau.

Partout ailleurs, le nombre d'éléments se tape à la main. Concerné : plumes, barres, parts, axes, lignes d'un tableau de variables, états, zones, positions, vues, onglets, langues, groupes, postes, boutons.

**La proposition : une notion unique de source**, avec deux formes.
- **« … depuis »** sur les objets à listes. On choisit une variable, un paramètre ou une énumération dans une liste à choix.
- **Le joker `[*]`** dans une liste de formules. `Cuves[*].Niveau` donne une plume, une barre ou une ligne par cuve ; `Cuves[*].Nom` donne les noms dans la colonne correspondante.
  - La syntaxe existe déjà pour les membres internes (`[*].NOM`, 1.11.8).
  - Pour un ARRAY, la liste se fixe au build. Pour une LIST, elle se refait en marche quand le nombre change.
  - Elle se combine avec les repères : `$Cuves$[*].Niveau` (la variable varie à la duplication, le joker parcourt ses cases).

**Les types autorisés comme source**, objet par objet :
- ✓ : accepté aujourd'hui ;
- ⚠ : accepté avec un défaut ;
- → : proposé ;
- ✗ : refusé (Générer le dit).

| Type de la source | Liste, Liste déroulante, Boutons radio, Sélecteur (Éléments depuis) | Tableau (Lignes depuis) | Courbe, graphiques, Tableau de variables (proposé : Séries depuis / `[*]`) | Multi-états, Chronogramme (proposé : États depuis) | Barre de navigation, Conteneur à onglets, Résumé par zone (proposé) |
|---|---|---|---|---|---|
| **Énumération du projet** | ✓ textes affichés, son numéro écrit | → une ligne par valeur (Nom, Valeur, Texte) | → une série par valeur (comptage, chronogramme) | → **un état par valeur** (son texte, une couleur à choisir) | → un onglet par valeur ; la variable de l'onglet de ce type |
| **ARRAY[a..b] OF scalaire** | ✓ (1 D, et 2 D mis à plat) | ✓ (colonne Valeur) | → `Tab[*]` : une plume ou une barre par case | — | → `Vues[*]` (ARRAY OF STRING) |
| **ARRAY à 2 dimensions** | ✓ à plat | ⚠ **cases vides** → lignes × colonnes | → une série par ligne | — | — |
| **ARRAY OF STRUCT** | ⚠ **liste vide sans message** (probable) → choisir le membre libellé et le membre valeur | ✓ une colonne par membre (⚠ membres imbriqués vides) | → `Pompes[*].Debit`, noms `Pompes[*].Nom` | — | → `Zones[*].Nom` |
| **LIST / VECTOR OF scalaire** | ✓ | ✓ | → le nombre suit la liste en marche | — | → |
| **LIST / VECTOR OF STRUCT** | ⚠ libellé « {A: 1, …} », et ce texte est écrit → choisir les membres | ✓ une colonne par membre | → | — | → |
| **MAP[K] OF V** | ✓ ses clés (⚠ **la clé est écrite**, pas la valeur → au choix) | ✓ colonnes Clé / Valeur | → clé = libellé, valeur = barre ou part | → clé = valeur de l'état, valeur = texte | → |
| **TUPLE** | ✓ ses éléments | ⚠ marche en simulation, **Générer le refuse** (défaut D17) | → une série par élément | — | — |
| **STRUCT seule** | ✗ | → une ligne par membre | → une barre ou une ligne par membre (Tableau de variables : « toutes les variables de la structure ») | — | — |
| **Texte calculé `'a;b;c'`** | ✓ (le rang écrit) ⚠ non choisissable dans l'inspecteur, et une formule est ignorée au clic | ✗ | ✗ | ✓ (la liste d'états entière) | ✗ |
| **Paramètre objet d'une popup ou d'un symbole (1.12.4)** | ✓ | ✓ | → | — | — |
| **Membre objet (`Recette.Etapes`, 1.12.4)** | ✓ (chemin écrit en entier) | ✓ | → | — | — |
| **Dossier de variables IHM** | — | — | → Tableau de variables : « toutes les variables du dossier Ligne/Convoyeur » | — | — |
| **Variable scalaire** (INT, REAL…) | ✗ Générer : « une liste est attendue » | ✗ | ✗ | ✗ | ✗ |

**Ce qui doit suivre la source**, pour chaque élément :
- le libellé (membre choisi, clé, texte de l'énumération ou index) ;
- l'unité et le format (ceux de la variable) ;
- la couleur (une palette tournante, ou la colonne Couleur) ;
- la consigne et les seuils.

Une source vide montre « (aucun élément) », jamais une erreur.

### 2.4 Les listes et les listes parallèles : un tableau, une ligne par élément

Contenu sait déjà éditer en tableau :
- les cases du Tableau ;
- les plumes (nom, formule, couleur) de la Courbe, et les séries des graphiques à barres, camembert, radar, XY, chronogramme et tableau de variables ;
- les états du voyant et du texte multi-états, et ceux de l'image animée ;
- les zones du plan, les pages du conteneur à onglets, les vues de la barre de navigation.

**À ajouter :**
- **Sélecteur, Liste déroulante, Boutons radio, Liste** : un tableau Libellé / Valeur écrite / (proposé) Couleur, à la place de `positions`/`items` et `values` tapés en parallèle.
- **Tableau de variables** : les colonnes Unité et Format dans le tableau des variables (aujourd'hui `units` et `formats` restent dans l'inspecteur et ne suivent pas les lignes déplacées).
- **Chronogramme d'états** : le tableau d'états du multi-états (1.12.3), à la place de la ligne `stateList`.
- **Bargraphe, Cadran** : un tableau des zones De / À / Couleur. Les bornes en **% de l'échelle** ou en valeur, au choix, pour que changer min/max ne casse pas les zones.
- **Programmateur horaire** : un tableau des plages Jours / De / À, avec l'arrondi à la résolution montré.
- **Compteur de production** : les débuts de poste pris dans **un calendrier des postes du projet** (Configuration), au lieu d'une liste par compteur.
- **Résumé par zone** : les zones cochées dans la liste des groupes d'alarmes (alarmes d'objets comprises).
- **Sélecteur de langue** : les langues cochées dans celles du projet.
- **Gestion des utilisateurs** : les boutons cochés, comme la gestion de recettes.
- **Tableau** : les colonnes d'une source (`rowsFrom`) choisies, renommées et mises en forme (aujourd'hui `columns` est ignoré avec une source).

**Le stockage reste compatible.** Chaque tableau écrit la même propriété texte qu'aujourd'hui, mais :
- avec **un découpeur unique** qui respecte les chaînes `'…'` et `"…"`, les crochets, les parenthèses et les repères, et un échappement pour un `;` dans un libellé. Il est utilisé par le moteur, le dessin, Générer, Dupliquer et la traduction ; il remplace les cinq découpeurs actuels ;
- avec **Générer qui vérifie l'alignement** (« Couleurs : 3 pour 4 plumes ») et les couleurs (« Auto n'est pas une couleur »).

### 2.5 Choisir au lieu de taper

| Ce qui se tape aujourd'hui | Où | Proposé |
|---|---|---|
| Les vues | Barre de navigation (`views`, choix dans Contenu ✓, mais texte libre dans l'inspecteur), Connexion (`afterLogin`) | liste à choix partout ; **le renommage suit `afterLogin`** (aujourd'hui seulement la barre et le plan) |
| Les groupes d'alarmes | Historique, Bandeau, Compteur, Résumé, Statistiques, Plan à zones | liste à choix des groupes **et des groupes d'alarmes d'objets** (`Vue.*`, `symbole:`), que le filtre comprend déjà |
| L'alarme d'une consigne | Consigne d'alarme | liste des alarmes **et des alarmes d'objets** (défaut D12) |
| Les langues | Sélecteur de langue (`languages`) | cases à cocher (les codes du projet) |
| Les boutons | Gestion des utilisateurs (`buttons`) | cases à cocher (comme Gestion de recettes) |
| Les lettres ISA | Instrument ISA (`function`) | liste à choix (PT, TT, FT, LT, PIC, TIC…) avec l'unité proposée |
| Les codes de la vanne 3 voies | Position 0/1/2/3 ou 12/13/23 | liste à choix « fermée / 1-2 / 1-3 / 2-3 » quand la position est fixe ; les codes restent pour une variable |
| Les points | Ligne, Polygone, référence de la courbe XY, zones du plan | **édition des sommets sur le dessin** (poignées) ; un tableau X / Y sinon |
| Le nombre de l'onglet montré | Conteneur à onglets (`page`) | liste des onglets par leur nom |

### 2.6 Plages, seuils et unités : les prendre à la variable

**Aujourd'hui.** Configuration › Unités et formats donne à une variable son unité et son format, que les objets reprennent (« Format de la variable »). Il n'y a ni plage ni seuil : la plage 0–10 bar se retape sur chaque jauge, bargraphe, cadran, thermomètre, curseur, cuve et courbe.

**Proposé.**
1. Unités et formats gagne **Min, Max, Seuil bas, Seuil haut, Alarme basse, Alarme haute**.
2. Pour une variable liée à un équipement, la mise à l'échelle (`engMin`, `engMax`) sert de plage par défaut.
3. Les objets en héritent comme de l'unité, sauf ce qu'on règle à la main sur l'objet (la case « Format de la variable » devient « Unité, format et plage de la variable »).
4. Les seuils hors plage sont signalés par Générer au lieu de disparaître.
5. Min, max et seuils acceptent une variable tapée **sans `=`**, comme l'axe X de la courbe XY. Aujourd'hui, un nom de variable tapé dans un seuil est **ignoré en silence**.

### 2.7 Ce que l'écran montre = ce que le moteur fait

**Le constat.**
- Une formule sur une propriété est évaluée dans la copie dessinée. Or le moteur, les clics et les exports lisent la vue composée, sans les formules. Toute propriété lue par un clic ou par le moteur ignore donc sa formule.
- De plus, le premier geste de l'opérateur (un clic, un défilement) écrit une valeur fixe qui **efface la formule** (onglet montré, panneau replié, défilement), quand l'objet n'a pas de variable liée.

**Les propriétés concernées**, relevées dans le code :
- **groupe** de l'historique, du bandeau d'alarme et du résumé par zone ;
- **source, format et « Demander où »** du bouton d'export ;
- **vues** et **transition** de la barre de navigation ; **onglets** du conteneur ; **langues** ; **onglet ouvert** du menu de connexion et des paramètres système ;
- **lecture, nombre de tours, vitesse, départ, fin et condition** du GIF ;
- **vue après connexion** et **liste des utilisateurs** de la connexion ;
- **retour d'état**, **voyant**, **opération**, **impulsion inversée**, **écrire en glissant**, **confirmation** et **maintien** des commandes ;
- **min/max** du champ de saisie ; **positions/éléments/valeurs/éléments depuis** des listes ;
- **fenêtre** et **zone morte** de la flèche de tendance ; **condition** du compteur horaire ;
- **plumes** de la courbe et **éléments** des graphiques (la valeur est lue sur le texte fixe).

**Proposé.**
1. Le moteur lit **la même copie évaluée** que le dessin (une fonction « propriété évaluée » commune), pour toutes ces propriétés.
2. Un geste sans variable liée écrit une **valeur de marche à côté**, sans toucher à la formule. La formule reprend la main quand sa valeur change.
3. Les propriétés que le moteur ne peut vraiment pas suivre (rares) n'offrent pas `=` : la case le dit.

Parmi les propriétés lues par le moteur, les seules qui marchent déjà avec une formule sont le min, le max et le pas du curseur et du potentiomètre, et la cadence, l'objectif et la fenêtre du compteur de production.

### 2.8 Les repères `$…$` : la règle, et où la tenir

**Ce qui marche.**
- Les formules (`Expression::compile`) et les textes à trous de la propriété Texte (`TextTemplate`) retirent les `$` avant le calcul. Dupliquer les trouve dans toutes les propriétés.
- Sont aussi corrigés (1.11, REP-1) : la recette, le groupe des vues d'alarmes (au moteur), l'image du GIF (au moteur), la variable des commandes, les arguments des symboles.
- Contenu range tout dans les propriétés de l'objet (cases, plumes, états, zones, pages, vues), et Dupliquer les lit bien.

**Ce qui ne marche pas.** Aucun lecteur maison ne retire les repères. Le repère s'affiche, ou le nom n'est pas trouvé, dans :
- les listes (`positions`, `items`, `names`, `units`, `columns`, `tabs`, `views`, `labels`, `languages`, `groups`, `themeLabels`, `buttons`) ;
- les mini-langages (`stateList`, `zones`, `schedule`, `shifts`, `points`, référence XY, états d'image, zones du plan) ;
- les textes sans trou (Texte, libellés, titres, `textOn`/`textOff`, invite, titres des axes, texte inconnu, texte du défilant, texte du QR, encodé avec ses `$`) ;
- les noms (image, vidéo et affiche au dessin, symbole, vue après connexion, alarme, source d'export, style, fichier externe, sortie du programmateur) ;
- les chemins : unité et format de la variable, chemins de la liaison et marques de simulation, lignes du tableau de variables qui deviennent en lecture seule.

**Autres écarts.**
- Le dessin et Générer retirent moins que le moteur. Exemples : « (introuvable) » en rouge pour une recette qui marche, un compteur d'alarmes bloqué à 0, une fausse alerte « aucune alarme du groupe ».
- Dupliquer lit en mode texte des propriétés que le moteur lit comme des formules (`variable`, `state`, `values`, `variables`, `min`…). Un `$…$` dans une chaîne `'…'` y est donc remplacé, alors qu'il reste littéral en marche.
- Le suivi d'index ne suit que le premier index (`A[0].B[2]` → seulement `A[0]`).

**La règle proposée.**
1. **Un lecteur par rôle** (§ 2.1) : *formule* (repères retirés en mode Expression), *texte affiché* (retirés en mode Texte, `$$` → `$`), *nom* (retirés, puis cherchés), *liste* (le découpeur unique du § 2.4 garde les repères entiers, puis chaque élément est lu selon son rôle). Tout le logiciel lit une propriété par ce lecteur : moteur, dessin, Générer, Contenu, traduction, export.
2. **Dupliquer lit chaque propriété dans le mode de son rôle**, le même que le moteur. Il suit tous les index d'un chemin (`A[0].B[2]`, `M[1,2]`).
3. **Toute nouveauté de ce document garde les repères** :
   - les tableaux de Contenu les gardent case par case ;
   - le joker `[*]` se combine avec eux (`$Cuves$[*].Niveau`) ;
   - les sélecteurs (vues, groupes) acceptent un repère tapé à la main (`$Vue_Ligne$`) et le montrent en ambre comme dans l'inspecteur ;
   - la source et l'héritage de plage passent par le lecteur de nom, qui retire les repères avant de chercher la variable.
4. **Un essai par propriété** dans `hmi_reperes_test` : pour chaque objet, un repère dans chaque propriété, puis une duplication, la marche, Générer et l'affichage. Aujourd'hui seuls les trous de Texte, la variable, le retour d'état, le voyant et les expressions de couleur ou de nombre sont essayés.

### 2.9 Générer : une table unique du rôle de chaque propriété

**Aujourd'hui, trois tables disent le type attendu d'une propriété :**
- l'inspecteur : `expectedOf` ;
- la vérification des formules : `kPropTypes` et le suffixe « Color » ;
- les vérifications au cas par cas de `HmiCheck`.

`value`, `fault`, `state`, `lamp`, `running`, les seuils, la consigne, les zones et les états n'y sont pas typés. Les couleurs `colorOn`, `colorOff` et `colorFault` ne sont pas vues comme des couleurs. DATE, TOD, DT, les énumérations, les structures et les listes sont inconnues du vérificateur.

**Proposé : une seule table**, clé → rôle (§ 2.1) → type attendu → lecteur, lue par l'inspecteur (carré de légende), Générer et le moteur. Générer ajoute :
- le type de la variable contre le rôle (STRING sur une jauge, TIME sur un histogramme, structure sur une pompe, énumération en booléen) ;
- l'alignement des listes parallèles et la validité des couleurs, y compris dans les états et les zones ;
- le format (`0,0` à la française donne un entier sur deux chiffres) ;
- l'ordre des seuils (alarme basse < seuil bas < seuil haut < alarme haute) et les seuils hors plage ;
- une valeur tapée sans `=` qui ressemble à une variable (`P1.Defaut` dans Défaut) : « c'est une constante ; voulais-tu `=P1.Defaut` ? ». L'alarme d'objet doit lire la même chose que le dessin (défaut D13).

## 3. Objet par objet

Pour chaque objet :
- **Réglages** : ses paramètres propres. Les communs (position, taille, rotation, couleurs de fond et de contour, opacité, visible, police, style nommé, accès, signature) ne sont rappelés que s'ils jouent un rôle particulier.
- **Pas facile** : ce qui gêne pour un cas pratique, et la solution proposée (→).
- **Types** : ce que l'objet fait de chaque type en entrée, et ce qu'il écrit pour une commande.
- **Taille dynamique** : ce qui varie, et d'où cela peut venir.
- **Repères** : ce qui est géré, ce qui ne l'est pas.

Les numéros P1–P9 renvoient au § 1.1, D1–D18 au § 1.2, et les § aux règles communes.

### 3.1 Formes

#### Texte
- **Réglages** :
  - `text` : un texte fixe ou un texte à trous `{Variable:format}` (formats `0.00`, `000`, `0.0%`, `X4`, `t`, `Oui|Non`) ;
  - retour à la ligne, alignement.
- **Pas facile** : les formats des trous ne sont décrits que dans l'aide de la case. → Une aide à la saisie des formats à `:` (comme l'aide aux variables), avec l'aperçu de la valeur formatée.
- **Types** : BOOL → « TRUE » (sauf `{X:Oui|Non}`), TIME → « T#1500ms », énumération → son numéro, structure ou liste → non vérifié. → Rôle Texte (§ 2.1) : texte de l'énumération, durée lisible, `JOIN` d'une liste.
- **Taille dynamique** : sans objet.
- **Repères** : retirés dans les trous. **Un texte avec repère mais sans trou s'affiche avec ses `$` en marche.** C'est exactement ce que produit Dupliquer (« Mode : $Maintenance$ »). Même défaut pour le texte des boutons, interrupteurs et cases à cocher. → Lecteur « texte affiché » (§ 2.8).

#### Image
- **Réglages** :
  - `image` (liste à choix des ressources), étirement ;
  - recoloriage : couleur, mode, couleur remplacée.
- **Pas facile** : peu. « Couleur remplacée » ne sert qu'en mode « une couleur » : la case devrait se griser dans les autres modes.
- **Types** : une formule doit rendre le nom exact d'une ressource (`SEL(Ouvert, 'a.png', 'b.png')`). Sinon « (absente) ». → Pour une énumération, proposer « une image par valeur » (comme l'image animée « selon la valeur »).
- **Taille dynamique** : sans objet.
- **Repères** : retirés dans une formule. Une valeur fixe n'est pas lue par le lecteur de nom au dessin. Impact faible, puisque la case est une liste.

#### Rectangle, Ellipse
- **Réglages** : rayon des coins (rectangle). Tout se pilote par formule (couleurs vérifiées comme des couleurs).
- **Pas facile** : rien.
- **Types et repères** : ceux des formules : ✓.

#### Ligne, Polygone
- **Réglages** : `points` « x,y x,y … » dans le repère de l'objet.
- **Pas facile** :
  - pas d'édition des sommets sur le dessin ;
  - `10, 20` (espace après la virgule) est ignoré ;
  - `10,5,20` (virgule décimale) est lu x = 10, y = 5,2 ;
  - Générer ne compte que les points, pas les paires mal écrites ;
  - aucun essai n'existe pour ces deux objets.
  
  → Poignées de sommets sur le dessin (ajouter, retirer, déplacer), un tableau X / Y dans Contenu, Générer qui signale une paire illisible.
- **Types** : une formule sur `points` est dessinée, mais la sélection et le redimensionnement utilisent la valeur fixe.
- **Taille dynamique** : le nombre de points, tapé. → Proposé : `points depuis` un ARRAY OF REAL (x, y alternés), un ARRAY OF STRUCT (x, y) ou une LIST, pour tracer un profil.
- **Repères** : sans objet.

### 3.2 Commandes

#### Bouton
- **Réglages** :
  - `text` ;
  - confirmation (aucune / second clic / maintien), durée du maintien ;
  - signature électronique ;
  - son comportement : l'onglet Actions.
- **Pas facile** : une formule sur la confirmation ou le maintien est dessinée mais pas appliquée au clic (P5).
- **Types** : par les actions (§ 2.2) : une STRING incrémentée se vide (défaut D15), Affecter écrit la valeur telle quelle.
- **Taille dynamique** : sans objet.
- **Repères** : cibles et valeurs des actions ✓. Un texte sans trou affiche ses `$` (voir Texte).

#### Bouton à impulsion
- **Réglages** : `variable`, couleur appuyé, impulsion inversée.
- **Pas facile** : une formule sur « impulsion inversée » est ignorée (P5).
- **Types écrits** : vrai à l'appui, faux au relâcher.
  - BOOL ✓ ; entiers et REAL 1/0 ✓ ; STRING « TRUE »/« FALSE ».
  - TIME ou DATE : 1 ms (absurde).
  - Énumération : 1/0 quelles que soient ses valeurs.
  
  → Générer refuse une variable qui n'est ni BOOL ni entière (§ 2.2).
- **Repères** : variable ✓.

#### Interrupteur
- **Réglages** :
  - `variable` ;
  - libellés vrai / faux ;
  - « Retour d'état (expression) » ;
  - couleurs, poignée.
- **Pas facile** :
  - **Taper `=Retour` dans « Retour d'état » range une formule que le moteur ne lit pas** : il lit le texte fixe. Le libellé « (expression) » invite pourtant à le faire (P5).
  - Une discordance est montrée à tort pour un INT de 2.
  
  → Une seule copie évaluée (§ 2.7). Le retour d'état devient une vraie liaison (choix de variable, formule acceptée).
- **Types** : lit ≠ 0 ou TRUE/VRAI ; TIME → éteint.
  - **Lié à une STRING : chaque clic écrit VRAI, l'interrupteur ne repasse jamais à faux** (vérifié).
  - Énumération : 0/1.
  
  → § 2.1 et § 2.2 ; Générer refuse STRING, TIME, DATE.
- **Repères** : variable et retour d'état ✓ ; libellés vrai/faux affichés avec leurs `$`.

#### Bouton lumineux
- **Réglages** : `variable`, opération (basculer / impulsion / mettre à 1 / mettre à 0 / aucune), « Voyant (expression) », texte, couleurs.
- **Pas facile** :
  - formule sur « Voyant » ou « Opération » ignorée (P5) ;
  - en « aucune » (un simple voyant), Générer réclame quand même une variable.
- **Types** : comme l'interrupteur (même défaut STRING en « basculer »).
- **Repères** : ✓ (voyant, variable).

#### Sélecteur
C'est le modèle des objets à choix (Liste déroulante, Boutons radio, Liste).
- **Réglages** :
  - `positions` « Positions (a;b;c) » et `values` « Valeurs écrites (a;b;c) » (vide : le rang 0, 1, 2…) ;
  - `itemsFrom` « Éléments depuis » (énumérations, LIST, VECTOR, MAP, tableaux de l'IHM) ;
  - style rotatif ou boutons, retour d'état.
- **Pas facile** :
  - Deux listes parallèles à aligner à la main. Pas de tableau dans Contenu. Pas de `;` possible dans un libellé.
  - `values` n'est jamais vérifié : ni compilé, ni comparé au type de la variable.
  - Une formule sur positions, valeurs ou source : **l'écran montre une liste, le clic écrit dans une autre** (P5).
  
  → Tableau Libellé / Valeur dans Contenu (§ 2.4), vérification des valeurs contre le type, une seule copie évaluée.
- **Types écrits** :
  - Sans `values` : le rang, ou le libellé pour une STRING.
  - **Énumération : le rang 0..n-1, pas ses numéros**, sauf avec `itemsFrom` = l'énumération ou des valeurs `T_MODE#X`. **« Auto » nu écrit 0.**
  - TIME : le rang en millisecondes.
  - Une source LIST OF STRING vers un INT : 0.
  - **Affichage** : une valeur `T_MODE#Auto`, `T#5s` ou un nom de variable est bien écrite, mais jamais retrouvée. Aucune position ne s'allume, et « suivant » repart de 0.
  - **Traduction** : une STRING reçoit le libellé de la langue source ; dans une autre langue, rien n'est sélectionné.
  
  → § 2.2 (énumération par nom, refus motivé). La comparaison d'affichage passe par la valeur calculée, pas par le texte.
- **Taille dynamique** : `itemsFrom` (§ 2.3).
  - ⚠ ARRAY OF STRUCT ou ARRAY d'énumération : **liste vide sans message**.
  - ⚠ MAP : **la clé est écrite**, pas la valeur.
  - Le texte calculé `'a;b;c'` n'est pas proposé dans la liste.
  
  → Choix du membre libellé et du membre valeur ; « écrire la clé ou la valeur » pour une MAP.
- **Repères** :
  - Variable ✓. Valeurs retirées, mais après un découpage naïf sur `;`.
  - **`itemsFrom` efface tous les `$`** (pas un vrai retrait : `'…$$…'` est cassé).
  - Positions affichées avec leurs `$`.
  - `values` n'est suivi ni par les renommages ni par la substitution des arguments de symbole.

#### Curseur, Potentiomètre
- **Réglages** :
  - `variable`, min, max, pas (0 = continu) ;
  - orientation (curseur), format, unité, afficher la valeur, graduations ;
  - « Écrire en glissant », retour d'état, « Format de la variable ».
- **Pas facile** :
  - Le format et le pas ne sont pas liés : format `0` avec un pas de 0,5 montre des valeurs arrondies.
  - Min et max se retapent pour chaque objet (P8).
  - Une formule sur « Écrire en glissant » est ignorée.
  
  → Plage héritée de la variable (§ 2.6), format proposé selon le pas.
  
  Min, max et pas acceptent bien une formule : c'est l'exception qui marche.
- **Types écrits** : toujours un REAL arrondi au pas. INT ✓ (mais sans bornes : 40 000 → −25 536) ; BOOL ≠ 0 ; **TIME : le nombre en millisecondes** ; énumération : n'importe quel nombre. → § 2.2 (bornes du type, unité de durée).
- **Types lus** : BOOL, TIME, DATE ne se lisent pas : le curseur reste au minimum. → § 2.1.
- **Repères** : ✓.

#### Champ de saisie
- **Réglages** :
  - `variable`, mode (numérique / texte / mot de passe) ;
  - format, min, max, unité, longueur maximale, invite ;
  - clavier virtuel, valider en quittant.
- **Pas facile** :
  - **Min et max en formule (`=Haut`) sont ignorés**, et l'inspecteur refuse un nom tapé : on ne peut pas borner par une variable, sauf par un argument de symbole.
  - Le mode n'est pas comparé au type de la variable.
  
  → Bornes prises à la variable (§ 2.6) ou par formule, mode proposé selon le type.
- **Types écrits** :
  - Numérique : REAL ✓ ; entiers (un entier exigé) ; BOOL ≠ 0 ; STRING : le texte tapé (avec sa virgule) ; **TIME : des millisecondes** ; énumération : n'importe quel entier.
  - **Mode texte ou mot de passe vers une variable numérique : 0 en silence.**
  
  → § 2.2 (durées « 5 s », énumérations par nom, refus motivé).
- **Repères** : variable ✓.

#### Liste déroulante
- **Réglages** : `items`, `values`, `itemsFrom`, invite « (choisir) », lignes visibles, retour d'état.
- **Pas facile** :
  - Comme le Sélecteur.
  - L'invite, montrée quand rien ne correspond, **cache les erreurs de type** (une LIST OF STRING vers un INT écrit 0, puis rien n'est sélectionné).
  - Une formule sur « lignes visibles » est ignorée au clic.
- **Types, taille dynamique, repères** : comme le Sélecteur.

#### Case à cocher
- **Réglages** : `text`, `variable`, retour d'état.
- **Types** : comme l'interrupteur, **y compris le défaut STRING**.
- **Pas facile** : une formule sur le retour d'état est ignorée (P5).
- **Repères** : texte sans trou affiché avec ses `$`.

#### Boutons radio
- **Réglages** : `items`, `values`, `itemsFrom`, orientation, retour d'état.
- **Pas facile** :
  - Comme le Sélecteur.
  - La place est partagée entre les boutons : une longue liste dynamique les écrase, sans défilement.
  - La vérification de taille tactile ne voit qu'un bouton avec une source.
  
  → Défilement, ou passage automatique en liste déroulante au-delà de N éléments.
- **Types, taille dynamique, repères** : comme le Sélecteur (une MAP écrit la clé).

#### Date et heure
- **Réglages** : `variable`, champs (date et heure / date / heure), secondes.
- **Pas facile** : le lien entre les champs et le type de la variable n'est dit nulle part. Avec « date » seule, une variable TIME reçoit quand même l'heure cachée. Générer ne vérifie rien.
- **Types écrits et lus** :
  - STRING : texte ISO ✓ ; TIME : heure du jour en ms ✓ ; REAL : secondes depuis 1970 ✓ ; DINT, UDINT : secondes ✓ (dépassement en 2038).
  - INT, UINT, WORD : tronqués ; BOOL : VRAI ; énumération : un très grand nombre.
  - **DATE, TOD, DT : des secondes au lieu de millisecondes (défaut D1).**
  
  → Écrire selon le type (DATE à minuit, TOD depuis minuit, DT complet, en millisecondes, heure du mur). Générer refuse BOOL, INT et énumération, et propose les champs selon le type.
- **Repères** : variable ✓.

#### Programmateur horaire
- **Réglages** :
  - `schedule` « Plages » (`Lu-Ve=07:00-12:00,13:30-17:00; Sa=08:00-12:00`), résolution (1 h / 30 min / 15 min) ;
  - `variable` (la STRING qui garde les plages), `output` « Sortie (variable BOOL) », couleur de l'heure.
- **Pas facile** :
  - Le mini-langage : jours, plages, listes, `*`, `7h30`, `24:00`.
  - **Les plages sont arrondies à la résolution en silence** : 07:00-07:20 en « 1 h » disparaît.
  - `schedule` ne sert que tant que la variable est vide.
  - Ni le type de la variable (STRING) ni celui de la sortie (BOOL) ne sont vérifiés.
  
  → Tableau Jours / De / À dans Contenu, arrondi montré, Générer vérifie les types (§ 2.4).
- **Types** : une variable non-STRING reçoit 0 ou FAUX en silence ; la grille ne vit qu'en mémoire.
- **Repères** :
  - **`output` n'est pas retiré** : `$Sortie$` est refusé à l'écriture, et Générer le laisse passer.
  - Le cycle du programmateur ne connaît pas les paramètres de popup.
  
  → Lecteur de nom (§ 2.8).

#### Liste
- **Réglages** : `items`, `values`, `itemsFrom`, retour d'état ; la variable est facultative (liste en lecture).
- **Pas facile, types, taille dynamique, repères** : comme le Sélecteur ; elle défile.
- **Essais** : aucun dans l'éditeur.

#### Paramètres système
- **Réglages** : texte, onglet ouvert (Réglages / Diagnostic / Simulation), icône.
- **Pas facile** : une formule sur l'onglet est ignorée (P5).
- **Repères** : texte sans trou affiché avec ses `$`.

### 3.3 Affichage

**Pour tous les afficheurs.** Quand la formule de la Valeur échoue, la plupart dessinent leur valeur par défaut comme si elle était vraie : 40 % sur un bargraphe, « Arrêt » sur un multi-états, un voyant éteint. Seuls l'afficheur numérique et le 7 segments montrent l'erreur ; la simulation ajoute juste un petit coin rouge. → L'adaptateur du § 2.1 montre `###`, et l'objet est hachuré.

#### Voyant
- **Réglages** : `value`, couleurs allumé / éteint, forme.
- **Pas facile** : pas de libellé propre ; Générer ne vérifie rien pour cet objet.
- **Types** :
  - BOOL ✓ ; STRING : TRUE / VRAI / OUI.
  - **Entier : seuls 1 (et TRUE) allument.** 2, ou un mot de bits, laissent éteint en silence (vérifié). Il faut écrire `=X <> 0`.
  - REAL : seul 1 allume.
  - Énumération : comme un nombre.
  
  → Rôle Booléen : ≠ 0 partout (§ 2.1). Pour une énumération, choisir la valeur « allumée ».
- **Repères** : ✓ (formule seule).

#### Voyant multi-états, Texte multi-états
- **Réglages** :
  - `value` ;
  - `stateList` « États (valeur = texte | couleur) », édité dans Contenu › États (1.12.3) ;
  - afficher le texte, forme, couleur par défaut ; texte inconnu (texte multi-états).
- **Ce que la 1.12.3 a réglé** :
  - une ligne par état (Quand, Texte, Couleur, Clignote), la ligne « sinon », ↑ ↓ ;
  - `;` et `|` refusés tout de suite ;
  - le mode « selon des conditions ».
- **Pas facile** :
  - **Pas d'import depuis une énumération** : les valeurs doivent être des nombres. « Auto = … » ou « T_MODE#Auto = … » ne correspondent jamais, et les textes de l'énumération sont ignorés.
  - La case de l'inspecteur reste du texte libre.
  - Les couleurs ne sont pas vérifiées : « Marche | Auto » range « Auto » comme couleur.
  - Pas de détection de doublons.
  - Passer en conditions écrit `(src) = Auto` sans apostrophes pour une valeur texte.
  
  → **« États depuis l'énumération »** : un état par valeur, son texte, une couleur à choisir. Correspondance par numéro **ou par nom**. Générer vérifie les couleurs, les doublons et le type de « Quand » (§ 2.1, § 2.3).
- **Types** :
  - BOOL, entiers, REAL (plages `a..b`), STRING : ✓.
  - TIME : seulement le texte exact « T#5000ms ».
  - Énumération : par numéro seulement.
- **Repères** : **`stateList` n'est jamais retiré**. Les textes affichent leurs `$`, un repère en valeur ne correspond jamais, et le découpage coupe dans un repère. → Lecteur « liste » (§ 2.8).

#### Afficheur numérique
- **Réglages** :
  - `value`, format, unité, libellé ;
  - alarme basse, seuil bas, seuil haut, alarme haute ;
  - couleurs des seuils et des alarmes ;
  - « Format de la variable ».
- **Pas facile** :
  - **Le format n'est jamais vérifié** : `0,0` à la française donne un entier sur deux chiffres (3,5 → « 04 »), et le séparateur reste le point.
  - L'ordre des seuils n'est pas vérifié.
  - **Le libellé est annoncé « texte à trous » mais les trous ne sont pas remplis.**
  
  → Format vérifié, avec aperçu ; séparateur décimal selon la langue ; seuils hérités de la variable et ordonnés (§ 2.6) ; libellé rendu en texte à trous.
- **Types** :
  - Entiers ✓ ; REAL : 6 chiffres (D4) ; STRING : tel quel.
  - **BOOL : « TRUE », et un format `Marche|Arrêt` ne s'applique pas.**
  - TIME : « T#5000ms » ; énumération : son numéro ; structure ou liste : `###`.
  
  → § 2.1 (texte de l'énumération, durée lisible, format booléen appliqué).
- **Repères** : libellé et unité affichés avec leurs `$`.

#### Afficheur 7 segments
- **Réglages** : `value`, chiffres, décimales, zéros à gauche, couleurs.
- **Pas facile** : pas d'unité ni de format. Le reste est simple. Un refus se voit (tirets).
- **Types** : nombres ✓ ; arrondi D4 (12345,67 → 12345,70). → § 2.1.

#### Barre de progression
- **Réglages** : `value`, min, max, couleurs, orientation.
- **Pas facile** :
  - **Le texte affiche la valeur brute + « % », quels que soient min et max** : de 0 à 40, la valeur 20 montre « 20 % », barre à moitié (vérifié).
  - Min ≥ max n'est pas vérifié par Générer.
  - Pas d'unité ni de format.
  
  → Afficher le pourcentage calculé, ou la valeur avec son unité (au choix) ; vérification min < max ; plage héritée (§ 2.6).
- **Types** : tout ce qui n'est pas un nombre vaut 0 en silence (BOOL VRAI → 0, TIME → 0). → § 2.1.

#### Bargraphe
- **Réglages** :
  - `value`, min, max, orientation ;
  - `zones` « Zones (de-à = couleur) » ;
  - consigne et sa couleur, graduations, afficher la valeur, format, unité.
- **Pas facile** :
  - Les zones sont un mini-langage en texte libre, sans tableau dans Contenu.
  - **Leurs bornes sont absolues** : changer min ou max oblige à retaper les zones.
  - Une borne seule ne peut pas être une variable.
  - Avec les zones par défaut, la couleur « Remplissage » ne se voit jamais.
  - Trous et chevauchements non vérifiés.
  
  → Tableau De / À / Couleur, bornes en % de l'échelle au choix, seuils de la variable proposés en zones (§ 2.4, § 2.6).
- **Types** : non-nombre → `###` ; mais une erreur de formule montre le 40 fixe. → § 2.1.
- **Repères** : les zones ne sont pas retirées (`$Bas$-60` : « zone illisible »).

#### Jauge
- **Réglages** : `value`, min, max, unité, couleurs, « Format de la variable » (l'unité seulement).
- **Pas facile** :
  - Toujours 1 décimale (pas de format).
  - Pas de zones, de seuils ni de consigne (le cadran en a).
  - Min ≥ max non vérifié.
  
  → Format, seuils et zones comme le cadran ; plage héritée.
- **Types** : comme la barre de progression (0 en silence pour BOOL, TIME, texte).

#### Cadran à zones
- **Réglages** : `value`, min, max, unité, format, libellé, zones, graduations, couleur de l'aiguille.
- **Pas facile** : les zones comme le bargraphe ; le libellé comme l'afficheur numérique (trous non remplis) ; pas de consigne.
- **Types** : non-nombre → `###` et pas d'aiguille ; erreur → 40 fixe.

#### Thermomètre
- **Réglages** : `value`, min (−20), max (60), unité (°C), format, graduations, couleur de la colonne.
- **Pas facile** : pas de zones, de seuils ni de consigne. La couleur ne change que par une formule sur Remplissage. → Seuils hérités de la variable (§ 2.6).
- **Types** : non-nombre → `###` ; erreur → 20 °C fixe.

#### Flèche de tendance
- **Réglages** : `value`, fenêtre (s), zone morte, couleurs hausse / baisse / stable.
- **Pas facile** :
  - **Formules sur la fenêtre et la zone morte ignorées** (le moteur lit l'objet fixe).
  - Zone morte absolue (pas en %).
  - Dans une instance de symbole, probablement pas suivie.
- **Types** : BOOL, STRING, TIME ne donnent aucun échantillon : la flèche reste « stable » en silence. → § 2.1.

#### Horloge
- **Réglages** : style analogique / numérique, secondes, afficher la date. La couleur « Couleur du bouton » est en fait **la trotteuse** (libellé à corriger).
- **Pas facile** :
  - **Aucune liaison** : elle montre l'heure de l'IHM.
  - Elle ne peut pas montrer une DT ou une TOD d'une variable, un autre fuseau, ni l'heure sur 12 h.
  - Date toujours jj/mm/aaaa.
  
  → `value` facultative (TOD, DT, STRING ISO, REAL secondes) et un format de date.

#### Compteur horaire
- **Réglages** :
  - `variable` « Variable du total » ;
  - « Compte quand (expression) » (vide : toujours) ;
  - format de la durée, libellé, couleur.
- **Pas facile** :
  - **Piège de la formule** : taper `=Pompe` dans « Compte quand » range une formule que le moteur ne lit pas. Il compte alors toujours, ou n'enregistre rien. Pire, le carré de légende marque probablement fautif le bon réglage (`Pompe` sans `=`), et sa première correction propose justement la formule qui ne marche pas (lu dans le code, pas essayé).
  - **Le total repart de zéro en silence** : en INT au bout de 9,1 h, en UINT au bout de 18,2 h, **en TIME au bout de 49,7 jours**. Or le guide conseille TIME.
  - Pas de type vérifié, pas de remise à zéro intégrée.
  - Dans une instance de symbole, probablement à 0:00:00.
  
  → Une seule copie évaluée (§ 2.7), légende cohérente, total en LREAL (heures) ou UDINT (secondes) conseillé et vérifié, avertissement avant le dépassement, propriété « Remise à zéro (front) ».
- **Types** : TIME en ms, REAL en heures, entier en secondes. STRING : 0 et écrit vide.
- **Repères** : condition et variable ✓.

#### Texte défilant
- **Réglages** : `text` (texte à trous), vitesse, sens.
- **Pas facile** : un seul texte. Plusieurs messages demandent une formule (`JOIN`). → Proposé : « Messages depuis » une LIST OF STRING, ou les alarmes en cours.
- **Repères** : un texte sans trou défile avec ses `$`.

#### Code QR
- **Réglages** : `text` (texte à trous), correction d'erreurs (L/M/Q/H), marge, couleurs.
- **Pas facile** : **le guide promet les versions 1 à 40 (2 331 octets en M), alors que le code s'arrête à la version 10 (213 octets en M)**. Générer ne vérifie la longueur que pour un texte fixe. → Corriger le guide, ou étendre l'encodeur ; montrer la longueur restante.
- **Repères** : **un texte sans trou est encodé avec ses `$`.**

#### Tableau
- **Réglages** :
  - `columns` « Colonnes (a;b;c) », `rows`, largeurs (a;b) ;
  - `source` (fichier externe), `rowsFrom` « Lignes depuis » ;
  - les cases (Contenu : texte, texte à trous ou `=formule`).
- **Pas facile** :
  - **Priorité cachée** : Lignes depuis, puis fichier, puis cases. Contenu laisse éditer des cases qui ne se verront pas.
  - Avec une source, **les colonnes ne se règlent pas** (nom, ordre, format, unité) ; seules les largeurs comptent, par position.
  - Une case `=formule` n'a pas de format : un REAL s'affiche « 1.23457e+06 ».
  - Les lignes en trop sont coupées sans défilement (sauf avec une source).
  
  → Bandeau « remplacé par Lignes depuis » dans Contenu, tableau des colonnes de la source (§ 2.4), format par colonne, défilement.
- **Taille dynamique** (Lignes depuis, § 2.3) :
  - ✓ LIST et VECTOR (une colonne par membre ou Item), MAP (Clé / Valeur), ARRAY 1 D (et ARRAY OF STRUCT).
  - ⚠ **ARRAY 2 D : lignes de cases vides.** ⚠ Membres imbriqués : vides.
  - ⚠ TUPLE : refusé par Générer à tort (D17).
  - Les paramètres de popup et les membres LIST ne sont pas proposés dans la liste.
- **Repères** :
  - Cases `=` et à trous ✓ ; cases fixes et en-têtes affichés avec leurs `$`.
  - Lignes depuis retiré en marche, mais l'aperçu de l'éditeur montre « ← $X$ ».
  - **Générer saute la vérification de Lignes depuis si elle contient un `$`.**

### 3.4 Graphiques

**Pour tous les graphiques** :
- Les plumes et les séries s'éditent en tableau dans Contenu (nom, formule, couleur) ✓.
- Mais une variable ARRAY, LIST ou MAP **ne peut jamais donner N plumes, barres ou parts** : chaque élément se tape (P2).
- Les noms et couleurs ne sont pas comptés contre les formules (P1).
- Une valeur qui n'est pas un nombre est **un point perdu en silence** (courbes) ou `###` (barres) : TIME, DATE, STRING non numérique, structure.
- Une formule sur la liste entière ne crée pas une liste dynamique : les libellés suivent la formule, les valeurs restent celles du texte fixe.

→ Source et joker `[*]` (§ 2.3), adaptateur Nombre (TIME en secondes, DATE en secondes, § 2.1), alignement vérifié (§ 2.4).

#### Courbe
- **Réglages** :
  - les plumes (`variables`, `names` créé par Contenu seulement, `colors`) ;
  - mode temps réel / historique, source (fichier externe), durée affichée ;
  - échelle fixe / auto, Y min / max, interpolation, légende.
- **Pas facile** :
  - **Un seul axe Y**, pas d'unité ni de format par plume (la légende arrondit à 0,01).
  - **Pas de période d'échantillonnage** : un point par cycle (100 ms), donc des dizaines de milliers de points sur une longue fenêtre.
  - En mode historique, une plume doit être **le texte exact** de la variable archivée.
  - L'export aligne toutes les plumes sur les instants de la première : si elle ne se lit pas (un TIME), l'export est vide.
  
  → Axe Y par plume (gauche / droite), unité et format repris de la variable, période d'échantillonnage, plume historique choisie dans la liste des archivées.
- **Repères** : l'échantillonnage ✓. Mais l'historique, les colonnes de fichier externe et les annotations comparent le texte brut : un repère y casse la correspondance, et Générer dit « n'est pas archivée ».

#### Graphique en barres
- **Réglages** :
  - valeurs, noms, couleurs (6), orientation ;
  - échelle, min, max, format, unité (un seul pour toutes les barres), afficher la valeur ;
  - seuil bas / haut, couleur d'alarme, titre.
- **Pas facile** :
  - **Seuil tapé comme un nom de variable sans `=` : ignoré en silence** (dessin et Générer).
  - Unités et formats du projet non appliqués aux graphiques.
  - Une MAP ne peut pas donner clés = libellés et valeurs = barres.
  
  → § 2.3 (MAP, ARRAY OF STRUCT), § 2.6.

#### Courbe XY
- **Réglages** :
  - variable X (formule), courbes Y, noms, couleurs, X/Y min/max, titres des axes ;
  - courbe de référence `x,y x,y…`, tolérance, points gardés, remise à zéro (front).
- **Pas facile** :
  - **Un profil ARRAY (X[i], Y[i]) ne peut pas être tracé** : seulement des points accumulés dans le temps.
  - Un seul X pour toutes les courbes.
  - La référence se tape à la main.
  - **« Escalier » est tracé en linéaire** (D14).
  
  → Mode « profil » depuis deux ARRAY ou un ARRAY OF STRUCT (x, y), référence depuis une variable ou un tableau X / Y.
- **Types** : X en TIME ou DATE → aucun point. → § 2.1 (secondes).
- **Repères** : X, Y et remise à zéro ✓ ; la référence n'est pas retirée.

#### Chronogramme d'états
- **Réglages** : lignes (formules), noms, durée, `stateList` (comme le multi-états), afficher le texte, légende.
- **Pas facile** :
  - `stateList` est une ligne de texte dans l'inspecteur, alors que le multi-états a son tableau.
  - **Les valeurs d'état doivent être des nombres** : une énumération par nom ne correspond jamais.
  - Les lignes STRING sont perdues.
  - Contenu écrit des `colors` qui ne servent pas.
  
  → Le tableau d'états du multi-états, et « états depuis l'énumération » (§ 2.3).

#### Camembert
- **Réglages** : valeurs, noms, couleurs (6), trou central (%), étiquettes (pourcentage / valeur / les deux / aucune), format, unité, légende.
- **Pas facile** : listes parallèles ; une valeur négative compte 0 ; un ARRAY ou une MAP ne peut pas donner les parts. → § 2.3.

#### Radar
- **Réglages** : axes (formules), noms, couleur, min, max, consigne (valeurs a;b;c), couleur de la consigne, cercles, afficher la valeur.
- **Pas facile** : la consigne est **la seule liste dont Générer vérifie la longueur** (contre les axes). Les noms ne le sont pas. → Consigne en colonne du tableau des axes (§ 2.4).
- **Repères** : consigne compilée ✓.

#### Histogramme
- **Réglages** :
  - variable mesurée, min, max, classes, fenêtre (s) ;
  - période de mesure, mesures gardées ;
  - tolérances basse / haute (LSL / USL), afficher les statistiques (Cp, Cpk), format.
- **Pas facile** :
  - **Un temps de cycle en TIME n'est jamais compté** : il faut `TIME_TO_REAL`.
  - Une seule variable.
  - L'export lit les tolérances fixes alors que le dessin lit les formules.
  
  → § 2.1 (TIME en secondes), § 2.7.

### 3.5 Alarmes

**Pour tous les objets d'alarmes.** Le groupe se tape ou se choisit parmi les groupes du projet. Les groupes d'alarmes d'objets (`Vue.*`, `symbole:`) sont compris par le filtre, mais pas toujours proposés. Un repère dans le groupe est mal géré, et différemment selon l'objet (voir chacun). → Liste à choix complète (§ 2.5), lecteur de nom (§ 2.8), une seule copie évaluée (§ 2.7).

#### Historique
- **Réglages** :
  - source : alarmes, acquittées, historique, événements, système, mises de côté, audit ;
  - groupe : liste des groupes, plus `Vue.*` et `symbole:` ;
  - taille du texte.
- **Pas facile** :
  - **Avec un groupe en formule, un clic sur une ligne choisit une autre alarme que celle affichée** (probable, D2) : le dessin filtre avec le groupe calculé, le clic avec le groupe fixe. L'exemple livré fait précisément cela.
  - En source « historique », le filtre est une égalité exacte (pas de joker ni de liste).
  - L'export ignore le groupe.
  - **Générer ne vérifie rien pour l'historique.**
- **Repères** : le dessin ne les retire pas mais le clic si : les rangs ne correspondent plus.

#### Bandeau d'alarme
- **Réglages** :
  - zone (groupe) ;
  - alarme montrée : la plus grave, la plus récente, ou défilement (période) ;
  - bouton Acquitter, nombre des autres (+N), clignoter à acquitter, texte sans alarme.
- **Pas facile** : **avec un groupe en formule, « Acquitter » peut acquitter une autre alarme que celle affichée** (probable, D2).
- **Repères** : le choix de l'alarme les retire. Le compteur « +N » et Générer non, d'où une fausse alerte « aucune alarme du groupe ».

#### Compteur d'alarmes
- **Réglages** : groupe, compter (à acquitter / actives / en cours / mises de côté), libellé, icône, clignoter, taille.
- **Pas facile** : **un repère dans le groupe bloque le compte à 0** (le calcul est fait au dessin, avec le texte brut). Le libellé n'est pas un texte à trous.

#### Résumé par zone
- **Réglages** :
  - zones (groupes a;b;c ; vide : tous les groupes du projet) ;
  - tuiles par ligne, couleur d'une zone calme, clignoter ;
  - variable de la zone choisie (le nom du groupe est écrit).
- **Pas facile** :
  - Les zones se tapent.
  - **La variable reçoit le nom de la zone en STRING** : un INT ou une énumération reçoit 0 (probable), et Générer ne vérifie que son existence.
  - **Un repère dans les zones est cassé partout** (dessin, clic, survol, Générer).
  - Avec les zones en formule, la zone cliquée n'est pas la zone affichée.
  
  → Zones cochées (§ 2.4) ; variable STRING exigée, ou rang écrit dans un INT au choix.

#### Consigne d'alarme
- **Réglages** : alarme (liste des alarmes du projet), afficher le message, texte sans alarme.
- **Pas facile** : **une alarme d'objet n'est pas proposée, et Générer la dit introuvable** (D12).
- **Repères** : jamais retirés ; Générer le signale en erreur.

#### Statistiques d'alarmes
- **Réglages** : période (depuis le lancement / 24 h / 7 jours / tout l'historique), nombre de lignes, classer par (nombre / durée), groupe, couleur des barres.
- **Pas facile** : peu. **Un repère dans le groupe donne un écran et un export différents** (l'export le retire, le dessin non).

### 3.6 Production

#### Compteurs de production (TRS)
- **Réglages** :
  - pièces bonnes, rebuts (compteurs), en marche (formule) ;
  - cadence nominale, objectif, fenêtre de cadence ;
  - débuts de poste (hh:mm;…), seuils TRS faible / bon (%), bouton RAZ.
- **Pas facile** :
  - **Il ne compte que pendant que sa vue est affichée**, puis ajoute toute l'absence d'un bloc. Cela fausse la disponibilité.
  - Les compteurs sont gardés par objet : deux ouvertures d'une popup avec d'autres arguments partagent les mêmes.
  - Chaque objet a sa liste de postes à taper.
  - Un seuil qui n'est pas un nombre revient à 60 / 85 en silence.
  
  → Un calcul de TRS par le moteur, pour tout le projet, gardé par jeu d'arguments (comme les courbes depuis la 1.12.3). Un calendrier des postes du projet. Seuils vérifiés.
- **Types** :
  - Entiers ✓.
  - **Un INT qui repasse de 32 767 à −32 768 est pris pour une remise à zéro.**
  - REAL : 6 chiffres.
  - BOOL : 0/1, sans comptage des fronts.
  - TIME, DATE, ARRAY : la valeur reste figée à la dernière bonne lecture.
  - « En marche » en STRING : toujours FAUX.
  
  → Fronts comptés pour un BOOL, dépassement d'entier reconnu, § 2.1.
- **Repères** : ✓.

#### Tableau de variables
- **Réglages** :
  - variables (a;b), noms, unités (a;b;c), formats (a;b;c) ;
  - valeurs modifiables, format, clavier virtuel.
- **Pas facile** :
  - Quatre listes parallèles. Contenu n'édite que les variables et les noms, et **ne déplace pas les unités et formats** quand on retire ou déplace une ligne.
  - Pas de source : on tape chaque variable.
  
  → Colonnes Unité et Format dans Contenu (§ 2.4) ; source « toutes les variables de la structure » ou « du dossier », et `[*]` (§ 2.3).
- **Types** :
  - Affichage : une énumération montre son numéro, TIME « T#…ms », DATE sans préfixe.
  - **Écriture : TIME et DATE ne se modifient pas vraiment** (« T#1500ms » refusé, seules des millisecondes nues passent).
  
  → § 2.1 et § 2.2.
- **Repères** : **une ligne `$Cuve$.Consigne` devient en lecture seule** et perd unité et format (D11).

#### Éditeur de recette
- **Réglages** : recette, boutons (Enregistrer;Appliquer;Lire;Annuler;Nouveau), comparer avec l'installation, clavier.
- **Pas facile** :
  - Les champs d'une recette sont des variables simples, définies une par une. **Une structure ou un tableau ne se déplie pas en champs.**
  - **Lire enregistre une DATE comme `2026-10-09`, relue comme un calcul (2007).** TOD et DT ne se relisent pas, et tout le jeu est refusé (D8).
  - REAL à 6 chiffres : **faux « Écart »** (D4).
  
  → Champs de recette depuis une structure (un champ par membre) ; valeurs enregistrées en littéraux ST exacts (`D#2026-10-09`, `T#5s`, REAL complet).
- **Repères** : le dessin ne retire pas celui de la recette : l'opérateur lit « choisir la recette » alors que tout marche.

#### Gestion de recettes
- **Réglages** : recette, boutons (Ajouter;Modifier;Supprimer;Appliquer;Lire), réglés dans Contenu.
- **Pas facile** : comme l'éditeur pour les types (D4, D8). **Un repère dans la recette : le dessin montre « (introuvable) » en rouge** alors qu'elle marche.

#### Bouton d'export
- **Réglages** :
  - données exportées : alarmes, historique, événements, système, mesures, audit, une recette, un objet de la vue ;
  - format (CSV / Excel / PDF), nom du fichier (texte à trous), demander où enregistrer, icône.
- **Pas facile** :
  - **Une formule sur la source, le format ou « Demander où » est ignorée au clic** (D3), alors que les énumérations natives SOURCE_EXPORT et FORMAT_FICHIER sont proposées pour ces cases.
  - On ne peut pas exporter une variable (une LIST, une MAP) directement, seulement à travers un tableau.
  - Les exports d'historique ignorent leur groupe.
  
  → § 2.7 ; source « une variable » (LIST, MAP, ARRAY, structure).
- **Repères** : nom du fichier ✓ (texte à trous).

### 3.7 Synoptique

**Les 25 symboles ont les mêmes réglages de base** :
- `value` (Valeur), `fault` « Défaut (expression) » ;
- couleurs marche / arrêt / défaut ;
- libellé et sa place (dessous, dessus, aucun) ;
- animer en marche (les objets qui tournent ou qui coulent).

**Ce qui gêne, pour tous :**
- **Valeur ou Défaut tapé sans `=` (`P1.Marche`) est une constante** : le dessin le lit FAUX. Pourtant l'alarme par défaut de l'objet le prend pour une formule et se déclenche (D13). Générer ne dit rien.
- **Une énumération d'état est lue « ≠ 0 = en marche »** : un moteur en DÉFAUT (2) ou en MAINTENANCE (3) est dessiné vert et tournant. L'énumération native ETAT_MOTEUR, documentée « pour moteur, pompe, convoyeur », n'est utilisée par aucun symbole. Une seule variable d'état demande 2 ou 3 formules à la main.
- **Une structure entière (`=Pompe`) donne une erreur de marche** ; le symbole reste figé sans aucun signe. Seule une instance de symbole accepte une structure.
- **Le libellé est annoncé « texte, {variable} » mais les trous ne sont pas remplis** (D6). Un repère s'y affiche avec ses `$`.
- Les couleurs marche / arrêt / défaut ne sont pas vues comme des couleurs par Générer ; les styles nommés ne portent que marche et arrêt.

**Les propositions pour tous** :
1. **Une liaison « Équipement »** qui prend une structure (`T_Pompe`) et en lie les membres par leur nom :
   - `Marche`, `Defaut`, `Mode`, `Mesure`, `Ouverture`… ;
   - un tableau de correspondance modifiable, mémorisé par type de structure ;
   - une seule case remplit Valeur, Défaut, Ouverture, En mouvement et le libellé.
2. **Un mode « État »** :
   - Valeur liée à un entier ou une énumération ;
   - un petit tableau valeur → aspect (arrêt, marche, défaut, maintenance), rempli d'office pour ETAT_MOTEUR ou pour une énumération du projet.
3. **Le libellé en texte à trous**, retiré de ses repères : `{P.Nom}`, `P-$N$`.
4. **Générer** :
   - une valeur fixe qui ressemble à une variable ;
   - une énumération employée en booléen ;
   - une structure sans liaison Équipement ;
   - les couleurs.

#### Pompe, Moteur, Ventilateur, Compresseur, Échangeur, Chaudière, Mélangeur, Convoyeur
- **Réglages** :
  - les communs ;
  - Mélangeur : couleur du contenu (dessinée à 35 % fixes, pas un niveau) ;
  - Convoyeur : vitesse (négative = sens inverse).
- **Types** :
  - BOOL ✓ ; entier ≠ 0 = marche ; REAL arrondi (D4).
  - STRING : seules les valeurs numériques et VRAI / OUI / TRUE / 1 donnent marche ; « Marche » ou « ON » donnent arrêt.
  - TIME, DATE : arrêt.
  - Énumération : voir plus haut.
- **Cas pratique** : une pompe `P : T_Pompe` demande au moins trois formules sur deux objets :
  - `Valeur =P.Marche` ;
  - `Défaut =P.Defaut` ;
  - le mode auto / manu sur un texte multi-états à côté.
  
  → Liaison Équipement.

#### Tuyauterie, Flèche de flux
- **Réglages** :
  - Tuyauterie : forme du tube (droit, coude, té, croix), épaisseur, couleur du tube, couleur du fluide, vitesse (la valeur ≠ 0 fait couler, négative à l'envers) ;
  - Flèche de flux : couleur marche (bleue), pas de sens inverse.
- **Pas facile** :
  - Pas de réseau : chaque tronçon a sa formule.
  - Contour et épaisseur du contour n'ont pas d'effet.
  
  → « Tronçons liés » : un tronçon coule si celui d'amont coule et que la vanne entre eux est ouverte. Proposition à mûrir.
- **Générer** : seulement « épaisseur nulle ».

#### Vanne, Clapet anti-retour, Filtre, Disjoncteur, Sectionneur, Contacteur, Lampe, Transformateur
- **Réglages** :
  - les communs, sans animation ;
  - Vanne : commande (manuelle, motorisée, pneumatique, réglante), « Ouverture (%) » et « En mouvement (expression) » ;
  - Disjoncteur : le défaut dessine « déclenché ».
- **Pas facile** :
  - **Ouverture fixée de 0 à 100 %**, sans mise à l'échelle : un signal 0–1 ou 0–27648 demande une formule. Un BOOL en ouverture devient « tout ou rien » en silence.
  - « En mouvement » tapé sans `=` : la tige ne clignote jamais, mais l'alarme « course trop longue » marche.
  
  → Ouverture avec plage (héritée de la variable, § 2.6) ; « En mouvement » en vraie liaison.
- **Énumération** : un état {OUVERT, FERMÉ, DÉCLENCHÉ} dessine DÉCLENCHÉ comme fermé, sans un Défaut séparé. → Mode « État ».

#### Cuve, Silo, Trémie, Bouteille de gaz, Vérin
- **Réglages** :
  - niveau (Valeur), couleur du contenu, min, max, afficher la valeur, unité, format ;
  - alarme basse, seuil bas, seuil haut, alarme haute, et leurs couleurs ;
  - Bouteille : couleur de l'ogive (gaz) choisie à la main ;
  - Vérin : min et max seulement.
- **Pas facile** :
  - **Un seuil hors de [min, max] n'est pas dessiné, en silence**, alors que son alarme marche.
  - **Un BOOL donne 1 % de niveau.** Un vérin « sorti » (BOOL) reste presque rentré sauf avec max = 1. Rien ne prévient.
  - La couleur EN 1089-3 d'un gaz n'est qu'évoquée dans l'aide.
  
  → Seuils et plage hérités (§ 2.6) ; Vérin en mode booléen (rentré / sorti) ; couleur du gaz choisie dans une liste (azote, oxygène, argon, CO₂, hélium, hydrogène…).
- **Types** : REAL, INT, STRING numérique ✓ ; TIME → 0 ; les quatre alarmes de niveau acceptent aussi une valeur fixe non numérique (une cuve peut sonner sans jamais se remplir).

#### Instrument ISA
- **Réglages** :
  - valeur, fonction (lettres ISA, texte libre), boucle (numéro), montage (terrain, tableau, tableau arrière) ;
  - unité, format, afficher la valeur.
- **Pas facile** :
  - **La boucle est typée BOOL** par erreur : une formule texte est refusée, `.Loop` se lit FAUX (D5).
  - Les lettres ne sont pas vérifiées, ni liées à l'unité.
  - Pas de seuils.
  - **« 101 » ne peut pas suivre une duplication** : un repère ne commence pas par un chiffre, et ce n'est pas un index.
  
  → Corriger D5 ; liste des fonctions ISA avec l'unité proposée ; seuils (§ 2.6) ; numéro de boucle « 1$NN$ » ou suivi automatique (+1 par copie).

#### Vanne 3 voies
- **Réglages** :
  - position (Valeur, défaut « 2 ») ;
  - variante (mélangeuse, répartitrice), boisseau (T, L) ;
  - commande de la position (entier, deux booléens), booléens A et B ;
  - couleurs du passage, de la vanne fermée, du mouvement ; numéros des voies.
- **Pas facile** :
  - La position est un code à retenir : 1, 12 ou 21 = 1-2 ; 2, 13 ou 31 = 1-3 ; 3, 23 ou 32 = 2-3 ; autre = fermée.
  - Un boisseau en L accepte 1-2 sans avertir.
  - Les lignes du mode inactif restent visibles.
  - A et B tapés sans `=` se lisent FAUX en silence.
  
  → Liste à choix pour une position fixe ; mode « État » pour une énumération (fermée, 1-2, 1-3, 2-3) ; lignes du mode inactif grisées ; Générer : position impossible pour le boisseau.

### 3.8 Instance de symbole

- **Réglages** : symbole (liste des symboles du projet) et ses arguments.
  - **Seule et avec des paramètres** : une grille, une ligne par paramètre « Nom · TYPE ». Le défaut est montré, une constante est convertie (`Voiture` → `'Voiture'`), `=` sert pour une formule, le carré de légende est par ligne.
  - **À plusieurs, ou sans paramètre** : la case texte « Arguments » (`Armoire := Armoires[1]; Nom := 'B'`), non vérifiée à la saisie.
- **Pas facile** :
  - **Générer ne compare pas le type de l'argument à celui du paramètre**, alors que les popups le font.
  - Une constante qui ne se convertit pas est collée telle quelle.
  - Un argument calculé pour un paramètre écrit n'est pas signalé.
  - Les noms positionnels inconnus ne sont pas vus.
  - Seul le mode Référence existe.
  - « Créer un symbole » propose des paramètres sans type (ANY).
  - Le modèle « Moteur et son état » n'a ni paramètre ni liaison.
  
  → La vérification des popups (type, écrit, calcul) appliquée aux symboles ; types proposés à la création, depuis les chemins choisis ; un modèle d'équipement lié par structure (voir la liaison Équipement).
- **Types** : une structure entière passe ici (c'est la seule voie). Les paramètres objets (LIST, MAP… 1.12.4) exigent une variable.
- **Taille dynamique** : → Proposé : **« Répéter depuis »** (une instance par élément d'un ARRAY ou d'une LIST de structures), pour poser 8 cuves d'un coup, mises à jour quand la liste change.
- **Repères** :
  - ✓ Dupliquer suit les arguments (`$V[2]$`, `Pompes[2]`), retirés à l'expansion.
  - Mais un libellé `{Nom}` dans le symbole n'est pas remplacé : il faut écrire `=Nom`.

### 3.9 Navigation et structure

#### Barre de navigation
- **Réglages** :
  - vues (a;b;c ; vide : toutes les vues ordinaires), libellés (a;b;c) ;
  - orientation, style (onglets, boutons, liens), boutons Précédent / Suivant, transition ;
  - espacement, couleurs.
- **Ce qui marche** : Contenu offre la liste des vues à choisir, un libellé par ligne et l'ordre ✓. Générer signale une vue inconnue ou non ordinaire, et le renommage d'une vue suit.
- **Pas facile** :
  - Le texte brut reste modifiable dans l'inspecteur.
  - **Les libellés vont par position** : avec les vues automatiques, ajouter une vue au projet décale tous les libellés. Générer ne le voit que si les vues sont écrites.
  - Les libellés par défaut (noms des vues) ne se traduisent pas.
  - **Une formule sur les vues ou la transition** : les boutons dessinés ne sont pas ceux du clic (P5).
  
  → Libellé tenu par la vue (une propriété « libellé de navigation » de chaque vue, traduisible) plutôt que par la barre ; § 2.7 ; source « Vues depuis » une LIST OF STRING ou un dossier de vues.
- **Repères** : `$Vue_X$` donne « vue introuvable », sans indice ; les libellés s'affichent avec leurs `$`.

#### Fil d'Ariane
- **Réglages** : chemin (hiérarchie ou historique), séparateur, étapes au plus, commencer par l'accueil, couleurs.
- **Pas facile** : les étapes portent toujours le nom de la vue, sans libellé ni traduction ; la hiérarchie se règle vue par vue (Vue parente). → Le même « libellé de navigation » par vue ; un arbre des vues pour régler les parents d'un coup.

#### Conteneur à onglets
- **Réglages** :
  - onglets (a;b;c), onglet montré (numéro) ;
  - variable de l'onglet (INT), place et hauteur des onglets ;
  - pour chaque objet enfant : sa page.
- **Pas facile** :
  - **Variable de l'onglet** :
    - INT ✓ ; REAL arrondi ✓ ;
    - **BOOL : toujours la page 1, et un clic écrit VRAI (inutilisable)** ;
    - STRING : seulement un nombre (le nom d'un onglet → page 1) ;
    - TIME : page 1 ;
    - **énumération : décalée d'un cran** (valeurs 0 et 1 → page 1 ; un clic sur l'onglet 1 écrit la 2e valeur).
    
    Générer ne vérifie pas le type, malgré « (INT) » dans le libellé.
  - Une formule sur « onglet montré » désactive la variable, et le premier clic l'efface (P5).
  - Modifier `tabs` dans l'inspecteur ne déplace pas les objets des pages (Contenu le fait).
  - L'onglet montré est un numéro, pas un nom. Pas de niveau d'accès par onglet.
  
  → Onglets depuis une énumération (un onglet par valeur, la variable de ce type, sans décalage) ; STRING = nom de l'onglet ; liste à choix pour l'onglet montré ; Générer vérifie le type.
- **Repères** : variable ✓ ; les libellés d'onglets s'affichent avec leurs `$`.

#### Cadre avec titre
- **Réglages** : titre, style (bordure, bandeau), place, couleurs, rogner le contenu.
- **Pas facile** : **le titre est annoncé « texte à trous » (`Pompe {Nom}`) mais s'affiche avec ses accolades** (D6) ; il faut `=CONCAT(…)`. → Titre rendu en texte à trous, vérifié par Générer.
- **Repères** : titre affiché avec ses `$`.

#### Panneau défilant
- **Réglages** : défilement (vertical, horizontal, les deux), taille du contenu (0 = automatique), défilement X / Y, pas de la molette, barres, couleur.
- **Pas facile** : le défilement n'a pas de variable. Une formule ne va que dans un sens, et le premier défilement de l'opérateur l'efface (P5). → Variable de défilement (lue et écrite).
- **Types** : BOOL et TIME donnent 0.

#### Panneau repliable
- **Réglages** : titre, replié, variable du repli (BOOL), remonter les objets du dessous, bandeau.
- **Pas facile** :
  - Titre : comme le cadre (D6).
  - **Variable du repli** :
    - BOOL ✓ ; nombres et énumération 0/1 ✓ ;
    - **≥ 2 : dessiné déplié, mais le clic le croit replié** ;
    - **STRING : le clic écrit toujours VRAI, on ne peut plus le déplier** ;
    - TIME : jamais replié.
  - Sans variable, le premier clic efface la formule de « replié ».
  
  → Rôle Booléen (§ 2.1), Générer vérifie le type, § 2.7.

#### Plan à zones
- **Réglages** :
  - plan (image), zones (Contenu : nom, points en %, groupe d'alarmes, vue, couleur) ;
  - variable de la zone choisie ;
  - noms, nombre d'alarmes, clignotement, couleurs.
- **Pas facile** : **c'est l'objet le plus difficile de sa rubrique.**
  - Les points se tapent en % (pas d'édition sur le dessin).
  - Les groupes d'alarmes d'objets ne sont pas proposés, et Générer dit à tort « groupe sans alarme ».
  - **La variable est en écriture seule** : rien ne la relit. Un type autre que STRING reçoit 0, et Générer ne vérifie que son existence.
  - La zone choisie est partagée avec le résumé par zone.
  
  → Tracé des zones sur l'image (poignées) ; groupes complets ; variable lue et écrite (choisir une zone par programme) ; type vérifié.
- **Repères** : l'image n'est pas retirée au dessin (« (absente) ») ; noms, groupes et vues des zones jamais retirés.

#### Sélecteur de langue
- **Réglages** : langues (codes a;b;c ; vide : toutes), style (boutons ou bascule), libellé (code et nom, code, nom), orientation.
- **Pas facile** : les codes se tapent (pas de liste, pas de Contenu) ; les noms de langue sont refusés ; un code inconnu disparaît en silence ; une formule désaccorde les boutons et le clic. → Langues cochées dans celles du projet (§ 2.4).
- **Repères** : `$fr$` disparaît, avec seulement un avertissement.

#### Sélecteur de thème
- **Réglages** : libellés (jour;nuit), icône, orientation.
- **Pas facile** : rien de notable (toujours deux boutons ; les libellés se traduisent).

#### Conteneur
- **Réglages** : couleurs, rogner le contenu.
- **Pas facile** :
  - **Caché ou transparent, il ne cache pas ses enfants**, qui restent cliquables (D7), contrairement au guide.
  - Poser un objet sur un conteneur ne le met pas dedans : il faut d'abord y entrer par un double clic.
  
  → Visible et opacité transmis aux enfants (comme une instance de symbole) ; dépôt dans le conteneur survolé.

### 3.10 Médias

#### Vidéo
- **Réglages** : vidéo (ressource), image d'attente, fond.
- **Pas facile** : **la vidéo n'est pas décodée** : seule l'image d'attente et une barre de progression qui boucle sur la durée du fichier se voient. Il n'y a ni lecture / pause ni départ sur condition. → À dire dans la palette (« aperçu seulement »), ou décoder (projet à part).
- **Repères** : le nom n'est pas retiré (« (absente) » et un avertissement).

#### Image animée
- **Réglages** : états (Contenu : `condition => image1, image2 @ période`), image par défaut, période, étirement, recoloriage.
- **Pas facile** :
  - Une énumération demande une ligne par valeur (`Etat = T_ETAT#Marche => …`) : pas de mode « selon la valeur » comme le multi-états.
  - Retirer une image d'un état oblige à modifier le texte.
  
  → Mode « selon la valeur » et « états depuis l'énumération » (§ 2.3) ; images en liste dans chaque état.
- **Types** : les conditions sont des booléens (≠ 0) ; une STRING n'est jamais vraie (Générer prévient).
- **Repères** : conditions ✓ ; **les noms d'images des états ne sont pas retirés**.

#### GIF animé
- **Réglages** :
  - GIF (ressource), lecture (en boucle, une fois, N fois), nombre de tours, vitesse (%) ;
  - départ (à l'affichage, sur action, sur condition), condition, à la fin (dernière image, première image, caché), étirement.
- **Pas facile** :
  - **Le moteur du GIF lit l'objet fixe** : une formule sur lecture, tours, vitesse, départ, fin ou condition est ignorée, alors que les énumérations natives sont proposées pour ces cases.
  - **Piège** : `=Marche` dans la condition, et le GIF ne joue jamais (D9).
  - Une variable inconnue dans la condition fixe n'est pas signalée.
  
  → § 2.7 ; la condition devient une vraie liaison (choix de variable, formule acceptée).
- **Repères** : image retirée au moteur, pas au dessin (« (absent) »).

### 3.11 Utilisateurs

#### Menu de connexion
- **Réglages** : texte (à trous ✓), onglet ouvert (Connexion, Mon compte, Comptes, Accès, Journal), icône, afficher l'utilisateur.
- **Pas facile** : une formule sur l'onglet est ignorée au clic.

#### Connexion
- **Réglages** : titre, liste des utilisateurs (choisir avec < >), texte du bouton, vue après connexion, clavier virtuel.
- **Pas facile** :
  - La vue après connexion se tape, et **n'est pas mise à jour quand on renomme la vue**.
  - Une formule sur la vue ou sur la liste des utilisateurs est ignorée, ou désaccordée.
  
  → Liste à choix des vues, renommage suivi (§ 2.5).

#### Déconnexion
- **Réglages** : texte, afficher l'utilisateur, demander confirmation.
- **Pas facile** : rien de notable.

#### Utilisateur connecté
- **Réglages** : affichage (gabarit `{nom}`, `{login}`, `{groupe}`, `{niveau}`, `{depuis}`, `{reste}`), texte sans utilisateur, icône.
- **Pas facile** : pas de trous de variables dans le gabarit ; un mot inconnu s'affiche tel quel ; Générer ne vérifie rien. → Aide à la saisie des mots du gabarit, et Générer.

#### Changer le mot de passe
- **Réglages** : titre, longueur minimale, un chiffre exigé, texte du bouton, clavier.
- **Pas facile** : rien de notable (la politique du projet s'ajoute).

#### Gestion des utilisateurs
- **Réglages** : boutons (Ajouter;Modifier;Supprimer;Activer;Mot de passe), couleurs.
- **Pas facile** :
  - Les noms des boutons se tapent exactement ; ils ne se traduisent pas.
  - **Les boutons qui ne tiennent pas, et les lignes au-delà de la hauteur, disparaissent sans défilement.**
  
  → Boutons cochés (§ 2.4), défilement, libellés traduits.

### 3.12 Groupe

- **Réglages** : les communs seulement. Il naît de Grouper.
- **Pas facile** : les formules de géométrie déplacent bien ses enfants. Mais **Visible, Opacité et Clignotement n'ont aucun effet** : le groupe ne dessine rien et ne les transmet pas (une instance de symbole, elle, les transmet). → Les transmettre aux enfants, comme pour le conteneur (D7).

## 4. Le plan proposé

Sept lots, dans cet ordre. Chacun dit comment il tient les repères.

| Lot | Contenu | Taille | Les repères |
|---|---|---|---|
| **A. Les défauts** | D1 à D18. S'y ajoutent les défauts propres aux objets : barre de progression (« % »), voyant (≠ 0), interrupteur et case à cocher sur une STRING, panneau repliable sur une STRING, onglets sur une énumération ou un BOOL, compteur horaire (dépassement prévenu), flèche de tendance, guide du code QR, libellé de la trotteuse. Chaque défaut a un essai qui échoue avant sa correction. D2 est d'abord confirmé par un essai. | petit (une version) | chaque correction a son cas avec repère |
| **B. Un rôle par propriété** | La table unique (§ 2.9), l'adaptateur par rôle (§ 2.1), l'écriture vers le type (§ 2.2). Le moteur lit la même copie évaluée que le dessin (§ 2.7). Un geste ne détruit plus une formule. Générer vérifie le type contre le rôle. | grand (une à deux versions) | le lecteur de chaque rôle retire les repères selon son mode (§ 2.8, point 1) |
| **C. Les repères partout** | Le découpeur unique des listes. Les lecteurs maison remplacés par les lecteurs du lot B. Dupliquer lit chaque propriété dans le mode de son rôle et suit tous les index. Les chemins d'unité, de format et de liaison retirent les repères. Les essais de `hmi_reperes_test` couvrent chaque propriété de chaque objet. | moyen (avec B) | c'est son objet |
| **D. Les listes en tableau et les choix** | Les tableaux de Contenu du § 2.4 (sélecteurs et listes, tableau de variables, chronogramme, zones, programmateur, colonnes d'une source). Les listes à choix du § 2.5 (groupes et alarmes d'objets, langues, boutons, vues après connexion, lettres ISA, positions de la vanne 3 voies). Le calendrier des postes. L'alignement et les couleurs vérifiés. | moyen | chaque case garde ses repères ; un repère tapé dans une liste à choix est accepté et montré en ambre |
| **E. Les tailles dynamiques** | Les sources du § 2.3 et le joker `[*]` : courbe, graphiques, tableau de variables, multi-états et image animée (« états depuis l'énumération »), conteneur à onglets, barre de navigation, résumé par zone. « Répéter depuis » pour les instances de symbole. Les colonnes d'un ARRAY 2 D et d'un ARRAY OF STRUCT. | grand | `$Cuves$[*].Niveau` : le repère varie à la duplication, le joker parcourt la variable ; essais des deux ensemble |
| **F. Plages et seuils de la variable** | Unités et formats gagne min, max et les quatre seuils. Défaut tiré de la mise à l'échelle. Héritage par tous les objets à échelle. Seuils hors plage signalés. Un nom sans `=` accepté comme liaison. | moyen | le chemin de la variable est lu sans ses repères avant la recherche de son unité et de sa plage |
| **G. Synoptique et symboles** | La liaison « Équipement » par structure, le mode « État » (ETAT_MOTEUR, énumérations du projet), le libellé en texte à trous, la vérification des arguments de symbole (types, écrit, calcul), les types proposés par « Créer un symbole », un modèle d'équipement lié. | moyen | la liaison Équipement est un chemin à repères comme les autres (`$Pompes[1]$`) |
| *H. Plus tard* | L'édition des sommets sur le dessin (ligne, polygone, plan à zones, référence XY), l'horloge liée à une variable, le texte défilant depuis une liste, le code QR au-delà de la version 10, la vidéo décodée. | à chiffrer | — |

**Ma recommandation.**
1. **Le lot A d'abord.** Il corrige ce qui est faux aujourd'hui (une date enregistrée en 1970, un acquittement sur la mauvaise alarme, des valeurs arrondies dans les recettes) sans rien changer à la façon de régler les objets.
2. **Puis B et C ensemble.** Ils reposent sur le même outil (le lecteur par rôle) et suppriment les erreurs silencieuses et les repères cassés pour tous les objets d'un coup.
3. **Ensuite, selon votre usage** : D (le confort de réglage) ou E (la taille variable, le plus demandé pour un cas pratique « 8 cuves, 8 pompes »).
