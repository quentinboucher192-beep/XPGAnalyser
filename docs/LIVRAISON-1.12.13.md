# XPGAnalyser 1.12.13 : le Tableau v2 — un mode à la fois, des colonnes liées à la source, R/W, des gestes qui passent la ligne

Livrée le 16/10/2026 au matin. Elle a été codée le 12/10, après la 1.12.12 de l'après-midi (`docs/LIVRAISON-1.12.12.md`).

Elle répond à la demande du 12/10 :

> « ok code ca dans la prochaine livraison pour le moment que sur 'Tableau' donne un exemple dynamique pour LIST, ARRAY, MAP, STRUCT, plusieurs MAP etc »

Elle code la maquette validée `docs/maquettes/maquette-tableau.html` (la v2), et seulement sur l'objet **Tableau**. Tout se passe dans **XPGAnalyser IHM**. XPGAnalyser API ne change pas, à part son numéro de version.

Les questions laissées ouvertes par la maquette ont pris les réponses qu'elle proposait :
- les réglages des autres modes restent **en réserve**, et « effacer » les vide ;
- en cases fixes, **R/W se règle par colonne** ;
- les paramètres reçus portent les noms proposés : Ligne, Index, Cle, Membre, Element, Colonne, Valeur, Valeurs, Ancienne, Nouvelle, Titre ;
- tout est livré d'un coup.

Trois écarts avec la maquette :
- **changer de mode ne demande pas de confirmation** : rien ne se perd (les réglages restent en réserve), la barre d'état le dit, et Ctrl+Z le reprend ;
- **un tableau en mode fichier externe n'a pas de gestes** : le moteur n'a pas les lignes du fichier sous la main ;
- **le niveau pour écrire** se règle par colonne en lignes dynamiques seulement ; une case fixe prend le niveau de l'objet.

## 1. Un mode à la fois

Dans l'inspecteur, section Tableau, « **Le tableau montre** » coche un mode : **cases fixes**, **lignes dynamiques** ou **fichier externe**. Le tableau ne montre que celui-là.

Les deux autres disent « en réserve, non utilisé » s'ils ont des réglages (avec ce qu'ils contiennent), ou « vide ». Ils ne se perdent pas : un clic sur « effacer » les vide. C'était la question du 12/10 : « comment bien différencier si j'utilise cases fixes, mode dynamique ou fichier externe si j'ai rempli les 3 onglets ? ».

Un tableau d'avant garde le mode qu'il avait : Lignes depuis passe avant le fichier, et le fichier avant les cases. Il se dessine comme avant.

## 2. Les lignes dynamiques : des colonnes liées à la source

**Lignes depuis** prend une variable ARRAY, LIST, VECTOR ou MAP, une structure (une ligne par membre) ou une énumération (une ligne par valeur). Le tableau a autant de lignes qu'elle a d'éléments, en marche.

L'onglet **Contenu** règle les colonnes, une par ligne : titre, contenu, format, unité, largeur, alignement, R/W, niveau, couleur. Le type attendu et le type reçu se voient sur chaque ligne. Une faute, par exemple une structure entière dans une case, se corrige d'un clic. « Une colonne par membre » les crée toutes.

| Contenu | Ce que la case montre |
|---|---|
| `[*].Niveau` | le membre de l'élément de la ligne (`Cuves[2].Niveau`) |
| `[#]` | le rang de l'élément (1, 2…) |
| `[*]` | l'élément lui-même (une LIST de nombres) |
| `[*].Cle`, `[*].Valeur` | la clé et la valeur d'une MAP |
| `[*].Membre`, `[*].Valeur` | le nom et la valeur d'un membre (une structure) |
| `[*].Nom`, `[*].Valeur`, `[*].Texte` | une valeur d'énumération |
| `Debits[*]` | une autre liste, élément par élément |
| `Prix[[*].Cle]` | une autre MAP, lue par la clé de la ligne |
| `=[*].Niveau * 100 / [*].Capacite` | une formule |
| `{[*].Vanne.Ouverture:0} %` | un texte à trous |

La **couleur** d'une colonne peut suivre les **seuils** de la variable (Unités et formats), ou une **formule** : `=SEL([*].Etat = T_Etat#DEFAUT, '#2EA043', '#E5534B')`, avec SEL(G, si faux, si vrai) comme dans l'application. Un nombre se cale à droite et un texte à gauche, sauf si un alignement est choisi.

Sans Lignes depuis, chaque colonne nomme sa liste (`Lignes[*]`, `Cadences[*]`) : une liste par colonne.

## 3. « ... » là où une liste s'arrête

La plus longue liste fait les lignes. Une case sans valeur montre « ... » en gris : une liste plus courte, ou une clé absente d'une autre MAP. Dans la demande : « la plus longue et laisse juste des cases vides avec '...' ».

## 4. R/W par colonne

En marche, un clic sur une case R/W ouvre le **clavier virtuel** :
- la saisie se fait dans la plage de la variable (Unités et formats), sinon dans celle de son type ;
- une énumération se tape par son nom ;
- la valeur s'écrit à la validation, même dans une MAP ou une LIST ;
- le niveau de la colonne protège l'écriture, sous sécurité.

En **cases fixes**, R/W se règle par colonne dans Contenu (R;W;R). Une case qui n'a qu'une variable s'écrit : `{PA:0.0} bar`, ou `=Cuves[2].Consigne`.

## 5. Les gestes et ce que l'action reçoit

L'onglet **Actions** d'un tableau propose six déclencheurs de plus : Clic sur une case, Double clic sur une case, Appui long sur une case, Case saisie, Ligne choisie, Clic sur un en-tête. Chacun vaut pour une colonne (par son titre) ou pour toutes.

L'action reste une opération ordinaire de l'application (Affecter, Exécuter un script, Ouvrir une popup…). Elle reçoit des paramètres, et l'onglet Actions dit lesquels :

| Paramètre | Ce qu'il vaut |
|---|---|
| Ligne, Colonne | la ligne montrée et la colonne (1, 2…) — des copies |
| Valeur, Valeurs | le texte de la case ; ceux de la ligne (`Valeurs[1]`…) |
| Index, Cle, Membre | l'indice (ARRAY, LIST), la clé (MAP), le membre (structure) |
| Element | l'élément de la ligne, **par référence** : `Element.Consigne := 5` écrit la cuve (`Element^` aussi) |
| Ancienne, Nouvelle | la valeur avant et après la saisie (Case saisie) |
| Titre | le titre de la colonne (Clic sur un en-tête) |

Un projet qui se sert de ces gestes s'enregistre au **format 25**, qu'une 1.12.12 refuse : elle perdrait ces actions. Un projet qui ne s'en sert pas s'enregistre comme avant.

## 6. La ligne choisie et le tri

- **Ligne choisie →** : la variable qu'un clic écrit, le numéro de l'élément ou, pour une MAP, sa clé. La ligne choisie se surligne.
- **Trier par** et l'**ordre** rangent les lignes : les nombres comme des nombres, « ... » à la fin. Une formule les pilote : `Tri := Titre` sur Clic sur un en-tête trie d'un clic.
- **Sans ligne** : le texte d'une source vide (« Aucune alarme »).

## 7. Générer et Dupliquer

**Générer** dit :
- le type de chaque colonne, attendu et reçu ;
- `[*]` seul sur une structure, avec sa correction (`[*].Nom`) ;
- une formule mise en R/W ;
- une variable inconnue ;
- une action sur une colonne qui n'existe pas ;
- les réglages en réserve (une information).

Les paramètres reçus (Element, Index, Titre…) sont connus des actions du tableau. Une énumération n'est plus signalée « aucun membre ». Les réglages du tableau ont leur rôle pour les repères : **Dupliquer** suit `$Src$` dans Lignes depuis et dans les colonnes.

## 8. Les exemples dynamiques

**Nouvelle vue › modèle « Tableaux dynamiques »** pose une vue de sept tableaux, un par genre de source. Ses types et ses variables sont créés s'ils manquent. Si le projet en a déjà d'un autre type, le modèle crée `Cuves_Ex`, `T_Cuve_Ex`… et s'y lie. Ses scripts de vue donnent les valeurs de départ et font vivre les cuves : le niveau suit la consigne.

| Tableau | Source | Ce qu'il montre |
|---|---|---|
| Cuves | `ARRAY[1..4] OF T_Cuve` | `[#]`, Nom, Niveau (seuils), `%` (formule), État (formule de couleur), Consigne (R/W, de 0 à 10), Vanne (texte à trous), Débit (`Debits[*]` : « ... » sur la 4e cuve). Un clic choisit la ligne (`Cuve_Choisie`) ; un double clic met la consigne à mi-capacité (`Element.Consigne := Element.Capacite / 2`, par référence) ; un clic sur un en-tête trie, un second inverse |
| Recette | `T_Recette` (une structure) | une ligne par membre : Membre, Valeur (R/W, dans le type de chaque membre) |
| Alarmes | `LIST OF T_Alarme` | une ligne par alarme ; **+ Alarme** (`LIST_ADD`) et **− Alarme** la font grandir et rétrécir ; « Aucune alarme » sans ligne ; un clic sur Active la bascule |
| Stock | `MAP[STRING] OF INT`, avec `Prix` et `Seuils` | **plusieurs MAP par la clé** : Article (`[*].Cle`), Qté (R/W, rouge sous le seuil), Seuil (`Seuils[[*].Cle]`), Prix (`Prix[[*].Cle]`, R/W), Valeur (formule) ; la rondelle n'a pas de prix (« ... ») ; trié par quantité ; **+ Article** ajoute une clé sans prix, **Réappro.** fait remonter la rondelle |
| T_Etat | une énumération | Nom, Valeur, Texte |
| Listes | sans source | une liste par colonne : `Lignes[*]` (4), `Cadences[*]` (5, R/W), `Debits[*]` (3) |
| Réglages | cases fixes | la colonne Valeur en R/W (R;W;R) : deux consignes, le prix de la vis, l'article choisi |

Un panneau « En marche » montre ce que les gestes écrivent : la cuve choisie, l'article choisi, le tri, la dernière saisie avec l'ancienne et la nouvelle valeur.

**L'exemple animé du Tableau** (F1 sur un tableau, ou l'aide des objets) est refait en lignes dynamiques. Il montre une ligne par cuve, choisit une ligne, saisit une consigne au clavier virtuel puis trie d'un clic sur l'en-tête Niveau. Ses chapitres nomment la colonne touchée.

## Tester vous-même

1. Créez un **nouveau projet IHM**, puis **Nouvelle vue › Tableaux dynamiques**. La galerie dit ce que le modèle emporte : 5 types IHM et 15 variables IHM, créés s'ils manquent. La vue a sept tableaux.
2. Choisissez **Tableau_Cuves**. « Le tableau montre » coche Lignes dynamiques ; Cases fixes et Fichier externe disent « vide ». L'onglet **Contenu** montre une ligne par colonne, avec le type attendu et le type reçu.
3. Dans Contenu, remplacez le contenu de la colonne Nom par `[*]` : la ligne dit la faute (une structure entière dans une case) et propose `[*].Nom` d'un clic.
4. Lancez la **simulation**. Les sept tableaux tournent : la cuve D n'a pas de débit (« ... »), la rondelle n'a pas de prix (« ... »), la colonne Cadence fait cinq lignes.
5. Cliquez la case **Consigne** de la cuve A : le clavier virtuel s'ouvre, de 0 à 10 m. Tapez 8 puis Entrée. La consigne s'écrit, « Dernière saisie » dit « Cuve 1 : 3 → 8 », et le niveau de la cuve A monte.
6. **Double-cliquez** la ligne de la cuve B : sa consigne passe à 5 (Element, par référence).
7. Cliquez l'**en-tête Nom** : les cuves se trient par nom ; un second clic inverse l'ordre.
8. **+ Article** ajoute « joint 1 », sans prix (« ... »). **Réappro.** fait remonter la rondelle dans le tri. **+ Alarme** et **− Alarme** font grandir et rétrécir la liste, jusqu'à « Aucune alarme ».
9. Enregistrez : le projet passe au format 25, parce qu'il a des gestes de tableau. Un projet sans ces gestes s'enregistre comme avant.
10. F1 sur un tableau : son exemple animé et son tutoriel.

## Installer

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.13.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.13-portable.zip` et `XPGAnalyser-IHM-1.12.13-portable.zip` : chaque application, sans installation ;
- ce document ;
- les captures `1213_ihm_*`, prises sous Wine avec l'exe livré.

1. Lancez `XPGAnalyser-Setup-1.12.13.exe`. Il met à jour la version installée, la 1.12.12 du 12/10 ou une plus ancienne : même identité d'installation, données reprises sans être déplacées. Il pose les deux applications et leurs deux raccourcis. Vos projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.12.13.exe` (29,7 Mo, 31 133 107 octets) :
`BF611361C4D65E6021053EDF555AB8AA925E67ECE1C64B9216FB59E763BD86A1`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `XPGAnalyser-API-1.12.13` ou `XPGAnalyser-IHM-1.12.13`. Décompressez, puis lancez l'exe.
- `XPGAnalyser-API-1.12.13-portable.zip` (22,2 Mo) :
  `ABDD8E061476A80B144A257113122F367D294EAEE580EF4741BA210C5DD78454`
- `XPGAnalyser-IHM-1.12.13-portable.zip` (22,2 Mo) :
  `0B2A5A942BC3724FE123638EFA2BF9A901B4F32CE07FCD1371921E23ADF74F29`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe, 74 sur 74. Elle comprend :
- 7 314 contrôles dans `hmieditor` (7 268 à la 1.12.12) et 5 647 dans `hmi` (5 463) ;
- 304 dans `hmireperes`, comme à la 1.12.12 : les réglages du Tableau y passent ;
- ceux d'avant, qui passent toujours.

Les essais du Tableau (`HMI_TEST_11213=1` les lance seuls, 181 contrôles) couvrent :
- les trois modes, la réserve, la migration d'un tableau d'avant ;
- l'analyse sans moteur : le type attendu et reçu de chaque colonne, les fautes et leurs corrections ;
- en marche : chaque source (ARRAY, LIST, MAP et une autre MAP par la clé, structure, énumération, une liste par colonne), « ... », les seuils, la formule de couleur, le tri, la ligne choisie ;
- les gestes : Element par référence, la saisie R/W au clavier, une énumération par son nom, une case de MAP, Case saisie, l'en-tête ;
- Générer et Dupliquer ;
- le modèle « Tableaux dynamiques » : ses types et ses variables, Générer sans erreur ni avertissement, en marche sans erreur, ses boutons, et le modèle posé deux fois ou dans un projet qui a déjà ces noms.

L'exemple animé du Tableau est joué de bout en bout par l'essai des exemples : la ligne choisie, la consigne saisie au clavier (8), Case saisie, le tri d'un clic sur l'en-tête Niveau.

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `XPGAnalyser API 1.12.13` et `XPGAnalyser IHM 1.12.13` ;
- ils importent exactement les mêmes DLL que ceux de la 1.12.12 (ADVAPI32, IPHLPAPI, KERNEL32, SDL3, SHELL32, USER32, WS2_32, msvcrt, ole32).

**La session sous Wine** (`tools/sessions/session-1213-ihm.txt`) a tourné avec l'exe livré, sur Armoire_Gaz. Un petit outil y a ajouté la vue `Vue_Tableaux`, créée comme « Nouvelle vue › Tableaux dynamiques ». Armoire_Gaz a déjà des types `T_Cuve`, `T_Vanne` et `T_Recette` différents, et des variables `Cuves`, `Recette` et `Stock` : le modèle a créé `T_Cuve_Ex`, `Cuves_Ex`, `Stock_Ex`… et s'y est lié.

Chaque ligne de la session a réussi. La simulation se construit (452 éléments générés, 371 compilés, 0 erreur), et la Console ne note aucune erreur. Les captures :
- `1213_ihm_01`, l'éditeur : les sept tableaux et l'aperçu de leurs colonnes ;
- `02`, l'inspecteur de Tableau_Cuves, section Tableau : « Le tableau montre » sur Lignes dynamiques, Cases fixes et Fichier externe « vide », Lignes depuis `Cuves_Ex` et le type reçu (`ARRAY[1..4] OF T_Cuve_Ex`), 8 colonnes, Ligne choisie → `Cuve_Choisie` ;
- `03`, Contenu de Tableau_Cuves : une ligne par colonne (N°, Nom, Niveau, %…) ; la première, `[#]`, reçoit « n° de ligne · INT » ;
- `04`, Actions : Double clic sur une case → Exécuter, Case saisie → Affecter Derniere_Saisie, Clic sur un en-tête → Exécuter. Sous l'action choisie, ce qu'elle reçoit : Ligne et Index (INT, des copies), Element (`REF_TO T_Cuve_Ex`, par référence), Colonne, Valeur… ;
- `05`, Contenu de Tableau_Stock : Lignes depuis `Stock_Ex` (une MAP), Article (`[*].Cle`, une STRING), Qté, Seuil et Prix, ces deux-là lus dans d'autres MAP par la clé ;
- `06`, la simulation : les sept tableaux en marche. La cuve D n'a pas de débit, la rondelle n'a pas de prix, la colonne Cadence fait cinq lignes ;
- `07`, un clic sur la consigne de la cuve A : le clavier virtuel, « un nombre · de 0 à 10 m » ;
- `08`, 8 tapé puis Entrée : la consigne à 8,0 (le tableau des réglages la montre aussi), « Dernière saisie : Cuve 1 : 3 → 8 », la vanne ouverte à 100 % et le niveau qui monte ;
- `09`, après + Article, Réappro., + Alarme et un clic sur l'en-tête Nom : « joint 1 » sans prix, la rondelle à 37 remontée dans le tri, une quatrième alarme, et le tri des cuves par nom.

La session a tourné plusieurs fois. La première passe a trouvé un défaut, corrigé avant cette livraison. Le modèle reprenait la variable `Stock` d'Armoire_Gaz parce qu'elle était du même type. Ses boutons auraient donc écrit dans les données du projet (Réappro. s'arrêtait sur la clé « rondelle » absente). Le modèle ne reprend plus que les variables qu'il a créées lui-même (son dossier) ; un essai le vérifie. Les passes suivantes ont seulement retouché le lanceur de scripts de session : il clique une case de tableau (`sim-partie "Tableau_Cuves" "case:0:5"`), et il choisit un sous-onglet même quand l'inspecteur, serré, ne montre que son icône.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes. Un premier installateur 1.12.13, fait le 12/10 avant les dernières retouches, a été posé par-dessus la 1.12.12. L'installateur livré a été refait le 16/10 (la date de livraison dans les notes de version, et deux mots de l'aide alignés sur l'inspecteur), puis posé à son tour :
- à chaque fois : « Installation process succeeded ». Il a repris l'installation en place : même dossier, et son journal de désinstallation complété ;
- avant comme après : `XpgAnalyzer-API.exe`, `XpgAnalyzer-IHM.exe`, et les raccourcis « XPGAnalyser API » et « XPGAnalyser IHM » ;
- les deux exe installés sont identiques à ceux qui ont été testés. Installés, ils répondent `XPGAnalyser API 1.12.13` et `XPGAnalyser IHM 1.12.13`, et l'API installée analyse `MAST.XPG` comme avant (891 variables, 29 POU).

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici). La section « Tester vous-même » donne le parcours à refaire chez vous.

## Limites connues

- **Un tableau en mode fichier externe n'a pas de gestes** : ses actions de case ne partent pas, et « Ligne choisie → » ne s'écrit pas.
- **Le niveau pour écrire** se règle par colonne en lignes dynamiques seulement. Une case fixe R/W prend le niveau de l'objet.
- **Changer de mode ne se confirme pas** : la barre d'état le dit, et Ctrl+Z le reprend.
- **Le type d'Ancienne, de Nouvelle et d'Element n'est pas vérifié par Générer** : ces noms sont connus des actions, mais leur type dépend de la ligne, donc `Element.Membre_Inconnu` n'est signalé qu'en marche.
- **La formule de couleur** rend une couleur `'#RRGGBB'` (ou `16#RRGGBB`) ; un nom de couleur (`'rouge'`) n'est pas lu.
- **Le tri** se fait sur le texte montré pour un texte, et sur le nombre pour un nombre ; il ne connaît ni les dates ni l'ordre d'une énumération.
- **Le modèle « Tableaux dynamiques »** est fait pour un écran d'au moins 1280 × 800. Plus petit, ses tableaux défilent.
- Comme pour les versions d'avant : ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici).

## La suite

- Essayer sur un vrai Windows, avec la section « Tester vous-même ».
- Les autres objets à listes (Liste, Liste déroulante, Graphique en barres) pourraient prendre les colonnes liées et les gestes du Tableau, si c'est utile.
