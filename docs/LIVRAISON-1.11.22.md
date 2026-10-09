# XPGAnalyser 1.11.22 : votre liste du 09/10

Livrée le 09/10/2026 en fin d'après-midi. Elle répond à la liste que vous avez envoyée cet après-midi, avec la capture du renommage refusé « Fonction » → « Move » dans un symbole :
- « pouvoir créer des surcharges dans les symboles pour les fonctions » ;
- « pouvoir retirer des paramètres dans les popup/symbole et casser les compilations des éléments liés, renommer un paramètre renomme partout » ;
- « dans l'onglet général → Simulation il faut ajouter les déballages » ;
- « attention dans la bibliothèque d'objets : les champs de saisie n'écrivent pas en continu » ;
- « modernise l'explorateur d'application » : la maquette, validée avec vos quatre réponses, puis son code.

La souris et le clavier dans les variables système, et la section « Raccourcis » des vues et des popups, sont pour la 1.11.23 (voir « La suite »).

**Un projet de la 1.11.21 s'ouvre tel quel** : le format ne change pas.

Fichiers livrés :
- `XPGAnalyser-Setup-1.11.22.exe` (l'installateur) ;
- `XPGAnalyser-1.11.22-portable.zip` (le même programme, sans installation) ;
- ce document ;
- les captures `1122_*`, prises sous Wine avec l'exe livré.

## 1. Ce qui change pour vous

- **Les surcharges dans un symbole.**
  - Un outil **Nouvelle surcharge** dans l'onglet Fonctions d'un symbole (et dans Programmation générale › Fonctions) : il crée une fonction du même nom que la fonction choisie, avec son type de retour et sa description. Elle part du modèle d'une nouvelle fonction, avec son paramètre d'exemple (`Entree`, du type de retour). Son onglet **Paramètres** s'ouvre, car ce sont eux qui la distinguent. La barre dit « Une surcharge : 2 fonctions Etat », ou « Même forme que … » si sa forme est déjà celle d'une autre.
  - **Renommer vers un nom pris en fait une surcharge**, quand c'est sûr. C'est votre capture : « Fonction » renommée « Move » est acceptée, et l'arbre montre « Move · 2 surcharges ». Le renommage reste refusé, avec sa raison et ce qu'il faut faire :
    - si l'une des deux fonctions est **virtuelle** (ses redéfinitions la désignent par son nom) ;
    - si elle a **la même forme** que l'autre (changez d'abord ses paramètres) ;
    - si elle est **déjà appelée** : renommés, ses appels seraient choisis parmi les surcharges et pourraient changer de cible. Dans ce cas, créez plutôt la surcharge avec l'outil.
  - **Nouvelle fonction**, dans un symbole, demande son nom (un nom libre est proposé), son type de retour et sa description, comme pour les fonctions IHM. Avant, elle était créée sans rien demander.
- **Les paramètres d'un popup ou d'un symbole.**
  - **Renommer un paramètre le renomme partout** : chaque propriété des objets (expressions, textes à trous, repères), le titre du popup, les valeurs par défaut des autres paramètres, les arguments des instances et des actions qui ouvrent la vue. Pour un symbole, aussi son code : ses fonctions, ses alarmes, ses popups, et les redéfinitions de ses instances.
  - **Retirer un paramètre** (inspecteur, ligne **Ordre** › Supprimer) pose d'abord une question quand il est employé : elle liste ses emplois et les arguments qui lui sont donnés. Si vous confirmez (« Supprimer quand même »), le paramètre part, mais **ses emplois et ses arguments restent à leur place** : ils deviennent des **fautes de compilation**, chacune avec ce qu'il faut faire. Avant, les arguments partaient sans rien dire. Ctrl+Z rend le paramètre.
- **Le dossier Simulation se déplie** dans l'arbre de l'application :
  - **Débogage** montre chaque point d'arrêt (« Acquisitions_ANA · ligne 12 »), avec « désactivé » en gris, sa condition ou son nombre de passages ;
  - **Forçages** montre chaque forçage et sa valeur (« Armoires[0].ana.PT1.mes = 5 »), avec ce que le programme dirait ;
  - **Courbes** montre chaque courbe et sa dernière valeur ;
  - **Journal** compte ses lignes.

  Un clic ouvre l'onglet : le Débogage à la ligne du point d'arrêt, les Forçages sur le forçage, les Courbes.
- **L'explorateur modernisé**, selon la maquette validée :
  - le compteur et l'état du build sont dans **deux colonnes de largeur fixe**, alignées d'une ligne à l'autre ; la ligne à jour garde son ✓ vert ;
  - les **actions au survol** (épingler, ouvrir, le menu) se placent devant la colonne d'état : **le compteur reste visible** ;
  - l'icône prend **la couleur de son genre** (script, fonction, paramètre, constante, variable), les dossiers restent gris ;
  - les modes deviennent des **étiquettes** après le nom : **E/S**, **sortie**, **conservée**, **persistante** ; de même « 3 surcharges » et le rôle d'une vue (popup, symbole, modèle…) ;
  - les **signatures** et les types sont en gris, dans la police du code, après le nom en clair ;
  - l'icône d'un domaine (API, IHM, Simulation, Versions) est sur sa pastille de couleur, celles de la maquette : bleu, violet, vert, ambre (l'IHM était bleu clair, les Versions violettes).
- **Les champs de saisie n'écrivent qu'à la validation.** C'était déjà le cas : la valeur tapée attend **Entrée**, **Tab**, ou un clic ailleurs si « Valider en quittant » est coché. Ce comportement est maintenant vérifié par des essais à chaque livraison. Nouveau : **Échap** efface la saisie en attente (avant, elle restait dans le champ sans être écrite).

## 2. Les surcharges dans un symbole

- **Nouvelle surcharge** (`HmiFunctionsPane::addOverload`) : refusée sur une fonction virtuelle, sinon une nouvelle fonction du même nom, même retour et même description, partie du modèle (le paramètre d'exemple `Entree` et la variable `Resultat`, du type de retour). Le volet ouvre son onglet Paramètres. Si sa forme est celle d'une autre, la barre dit « Même forme que Etat(INT) : changez ses paramètres (onglet Paramètres) pour en faire une surcharge », et Compiler le signale tant que c'est le cas.
- **Renommer vers un nom pris** (`nameAllowed`, appelé par `renameFunction`) :
  - les fonctions déjà de ce nom sont réunies ; si l'une est virtuelle, ou si la forme est la même, c'est refusé ;
  - puis un **renommage à blanc**, sur une copie du projet, compte les textes qui changeraient. Les changements du corps de la fonction elle-même ne comptent pas (son propre nom, dans son modèle). S'il en reste, la fonction est appelée : refusé, avec le nombre de textes et le conseil de créer la surcharge à la place ;
  - accepté, la barre dit « une surcharge : 2 fonctions Move (chaque appel prend la sienne) ».
- **Nouvelle fonction d'un symbole** (`askHmiNewFunction`) : la fenêtre « Nouvelle fonction de S_Vanne » propose un nom libre (Fonction, Fonction2…), le retour « Aucun » et une description. Un nom déjà pris crée une surcharge.

## 3. Les paramètres : renommés partout, retirés en laissant des fautes

- **Renommer** (`hmi::params::renameParam`) :
  - dans la vue : chaque propriété de chaque objet (les noms dans les expressions, les textes à trous, les repères), sans toucher aux chaînes, aux commentaires, aux membres (`.Pos` d'un autre objet) ni aux noms d'arguments ; le titre du popup ; les valeurs par défaut des autres paramètres ;
  - pour un symbole : ses popups (objets, actions, scripts, titre), et tout code dont la portée est le symbole (ses fonctions, ses alarmes, les redéfinitions de ses instances) ;
  - hors de la vue : l'argument nommé de chaque instance et de chaque action qui ouvre la vue, comme en 1.11.10.
- **Retirer** (`hmi::params::removeParam`) ne retire plus rien d'autre que le paramètre. Les arguments donnés en positionnel sont d'abord écrits en nommé, pour garder leur sens. La liste rendue dit chaque argument qui reste.
- **La question avant de retirer** (`HmiEditor::commitView`) : un retrait à blanc, sur une copie, liste les arguments qui resteraient et les emplois dans la vue. S'il y en a, la fenêtre « Supprimer le paramètre Vanne de S_Vanne » les montre, en simple liste à lire (`HmiAskDialog::Spec::plainItems` : sans cases à cocher), avec « Supprimer quand même » en rouge.
- **Les fautes de compilation.** Un argument que le symbole ou la vue ouverte ne déclare plus est maintenant une **erreur** (c'était un avertissement) :
  - pour une instance : « argument Vanne : S_Vanne n'a pas (ou plus) ce paramètre (ses paramètres : Nom) - retirez-le de l'instance (inspecteur, Arguments), ou déclarez-le dans le symbole ». Le même texte au **Compiler** (les expressions de la vue) et au **Générer** (la validation) : le build ne le dit qu'une fois ;
  - pour une action qui ouvre un popup : « paramètre X : Pop n'a pas (ou plus) ce paramètre - corrigez l'appel… » ;
  - les emplois du paramètre dans la vue elle-même : « Vanne n'existe pas : veux-tu dire Vanne_Purge ? », quand le programme de l'automate est chargé.

  Le build incrémental suit : l'animation d'une vue dépend de **l'interface** de chaque symbole qu'elle instancie. Un paramètre retiré ou renommé dans le symbole recompile donc les vues qui l'emploient.

## 4. Le dossier Simulation déplié

- Les lignes viennent de la simulation à chaque rafraîchissement des pastilles de l'arbre (quatre fois par seconde) : les points d'arrêt de l'utilisateur (pas celui d'« aller à la ligne »), les forçages et les courbes du centre de simulation, la taille du journal.
- L'arbre ne se reconstruit que si **la liste** change (un point d'arrêt ajouté, un forçage retiré). Une valeur qui change met seulement sa ligne à jour.
- Sans point d'arrêt, Débogage reste une feuille, comme avant.

## 5. L'explorateur modernisé

- `TreeView::setModernLook` (`src/ui/widgets/DataViews.cpp`) dessine la ligne en colonnes : le nom (et son texte gris), les étiquettes, puis à droite **la colonne d'état** (34 px) et **le compteur** (30 px), de largeur fixe. Un compteur numérique est un nombre gris aligné à droite ; une pastille colorée reste une pastille.
- `ProjectTreeModel::modernize` (`src/app/ViewModels.cpp`) traduit les marques d'aujourd'hui pour ce dessin : « (E/S) », « (sortie) », « (conservée) », « (persistante) », « · 3 surcharges » et le rôle d'une vue deviennent des étiquettes. La signature d'une fonction et le type d'une déclaration passent en gris.
- Le texte du nœud **ne change pas** : la recherche, la carte au survol, les sessions et les essais lisent le même texte qu'avant.
- **Pas encore fait** : la portée en onglets et les puces de filtre (Modifiés, En faute, À générer, Épinglés), l'en-tête de domaine sur deux lignes avec sa ligne d'état, le pied de santé redessiné et la légende dans l'appli. Ils sont dans la 1.11.23.

## 6. Les champs de saisie

L'analyse du moteur (`hmi::Runtime`) : **une seule** écriture, `submitForm`, appelée par Entrée, par Tab, et par la perte du focus quand « Valider en quittant » est coché. La frappe ne fait que remplir le texte du champ. Aucun bogue n'a été trouvé. Ce qui a changé :
- **Échap** abandonne la saisie **et efface le texte en attente** ;
- des essais vérifient, dans la simulation, qu'une valeur tapée n'est **pas** écrite avant Entrée, ni après un cycle ;
- le guide (sujet « Champ de saisie ») le dit.

**Un piège trouvé, à corriger en 1.11.23** : dans le modèle de popup « Saisie d'une consigne », le bouton **Valider** ferme le popup sans valider le champ. Comme « Valider en quittant » n'est pas coché par défaut, une valeur tapée sans Entrée est perdue. En attendant : appuyez sur Entrée avant Valider, ou cochez « Valider en quittant » sur le champ de vos popups.

## 7. Le parcours vérifié sous Wine

Une session jouée avec l'exe livré (`tools/sessions/session-1122-liste.txt`), sur une copie d'Armoire_Gaz sans cache, celle de la 1.11.17, préparée comme pour la 1.11.21 (`preparer-projet-11120`, puis `preparer-projet-11121`). Le symbole **S_Vanne** du projet a deux paramètres (Vanne, Nom), cinq fonctions (dont Fermer, virtuelle, et Etat, sans paramètre) et deux instances, Vanne_3 et Vanne_4, dans Vue_Vannes.

Les commandes de contrôle de la session (`volet-message`, `ihm-diagnostics`) échouent si le résultat n'est pas celui attendu ; il n'y a eu aucun échec.

| Étape | Ce qui s'est passé | Capture |
|---|---|---|
| **1.** L'arbre : la branche API repliée ; IHM › Programmation générale › Fonctions › **Random** (et ses Paramètres) et **Convertir** dépliés, puis un clic sur Random | L'icône de l'IHM est sur sa pastille violette, celle de l'API sur sa pastille bleue. Les signatures sont grises après le nom (« Bonus(x : REAL) : REAL »). Sous Random : `RandomSeed : REAL` avec l'étiquette **E/S**, `test : REAL` avec l'étiquette **sortie**. « Convertir » porte l'étiquette **3 surcharges**. À droite, la colonne d'état (○ : jamais généré, aucun build n'a encore tourné) et les compteurs (7, 18, 39…) sont alignés d'une ligne à l'autre | `1122_01` |
| **2.** IHM › Symboles › **S_Vanne** ouvert, onglet Fonctions : Etat choisie, puis **Nouvelle surcharge** ; dans son onglet Paramètres, **Ajouter** un paramètre, nommé `Detail`, de type BOOL | La barre : « Fonction Etat créée : retour INT. Une surcharge : 2 fonctions Etat (chaque appel prend la sienne). Ctrl+Z la retire. » La liste du symbole montre les deux Etat ; la nouvelle a le paramètre d'exemple `Entree : INT` et `Detail : BOOL`. L'arbre, sous S_Vanne › Fonctions : « Etat **2 surcharges** » › `Etat() : INT` et `Etat(Entree : INT, Detail : BOOL) : INT` | `1122_02` |
| **3.** Onglet Dessin, inspecteur : la ligne **Ordre** du paramètre Vanne › **Supprimer** | La fenêtre « Supprimer le paramètre Vanne de S_Vanne » : « Le paramètre est employé. Ses emplois restent tels quels et deviennent des fautes de compilation, à leur place… Ctrl+Z rend le paramètre. » Puis « 4 emploi(s) », à lire, sans cases : `Corps / propriété fill` et `Pos / texte text` (dans S_Vanne), `Vue_Vannes / Vanne_3` et `Vue_Vannes / Vanne_4` (« son argument Vanne reste : une faute à corriger ») | `1122_03` |
| **4.** « Supprimer quand même », puis **Compiler** ; les Diagnostics du panneau du bas filtrés sur « argument » | Le build échoue : 15 erreurs. Dans l'arbre, IHM, Vues, Symboles, S_Vanne et ses fonctions sont en faute (✕ rouge). Les emplois de Vanne dans le symbole : « Vanne n'existe pas : veux-tu dire Vanne_Purge ? ». Les deux instances, une ligne chacune : « argument Vanne : S_Vanne n'a pas (ou plus) ce paramètre (ses paramètres : Nom) - retirez-le de l'instance… », élément IHM/Vues/Vue_Vannes/Animations | `1122_04` |
| **5.** Un point d'arrêt (Acquisitions_ANA, ligne 12), deux forçages (PT1.mes = 5, PT3.mes = 7,5), une courbe (PT1.mes, centre de simulation) ; la branche IHM repliée, Simulation › Débogage, Forçages et Courbes dépliés | Simulation, sur sa pastille verte : **Débogage** › « Acquisitions_ANA · ligne 12 » ; **Forçages** (3) › « Armoires[0].ana.PT1.mes = 5 », « Armoires[0].ana.PT3.mes = 7.5 », et le forçage d'esclave du projet de démonstration (« 43021 · INT = 5100 », Centrale PM5560) ; **Courbes** (1) › « Armoires[0].ana.PT1.mes » ; Journal (8). Versions est sur sa pastille ambre. L'onglet Simulation · Courbes montre la courbe | `1122_05` |

**Ce que la préparation de la session a montré.**
- La première exécution attendait « Même forme que Etat » après Nouvelle surcharge. Mais la surcharge part du modèle d'une nouvelle fonction, qui lui donne le paramètre d'exemple `Entree : INT` : sa forme diffère déjà de celle d'Etat(), et la barre a dit, à juste titre, « Une surcharge : 2 fonctions Etat ». La session attend maintenant ce texte. Le programme n'était pas en cause.
- La capture 4 a montré **chaque faute deux fois** : « argument Vanne : S_Vanne n'a pas (ou plus) ce paramètre » (le contrôle des expressions, ajouté dans cette version) et « argument inconnu du symbole S_Vanne : Vanne » (la validation, qui le disait déjà en avertissement). Le build n'écarte un diagnostic de la validation que s'il a le même texte que celui de la compilation. Les deux contrôles donnent maintenant le même texte, et le build le dit une fois.
- La capture 3 montrait des **cases à cocher** devant les emplois, qui n'avaient aucun effet. La liste est maintenant à lire, sans cases.
- La capture 1 montrait l'IHM en bleu clair et les Versions en violet : les couleurs d'avant la maquette. Le nouveau dessin prend celles que vous avez validées.
- La première courbe de la session avait été suivie depuis l'onglet de simulation de l'API (« Suivre sur la courbe »). Cette courbe appartient à cet onglet, pas au centre de simulation : le dossier Courbes de l'arbre, qui ouvre le centre, ne la montrait pas. La session ajoute maintenant la courbe au centre (onglet Courbes).

Les quatre corrections ont été faites avant la livraison, et la session a été rejouée avec l'exe livré.

## 8. Fichiers modifiés

- les surcharges : `HmiFunctionPanes` (Nouvelle surcharge, le renommage en surcharge), `HmiWorkspace` et `Screens.hpp` (Nouvelle fonction d'un symbole demande son nom) ;
- les paramètres : `HmiPopupParams` (renommer partout, retirer en laissant les arguments), `HmiEditor` (la question), `HmiAskDialog` (une liste à lire, sans cases), `HmiParamPanes` (l'aide des lignes Nom et Ordre), `HmiCheck` (les arguments inconnus en erreur, au Compiler aussi, un seul texte), `HmiPipeline` (l'animation dépend de l'interface du symbole) ;
- la simulation : `SimulationWorkspace` (les lignes poussées dans l'arbre, le clic), `SimDebugWorkspace` (ouvrir le débogage à une ligne), `ViewModels` (le genre de nœud `SimRow`) ;
- l'explorateur : `DataViews` (le dessin en colonnes, les étiquettes, les couleurs des domaines), `ViewModels` (`modernize`), `MainAnalysisScreen` (l'explorateur passe au nouveau dessin) ;
- les champs de saisie : `HmiRuntime` (Échap efface) ;
- le guide (fonctions, paramètres des popups, champ de saisie), les notes (5), la version 1.11.22 ;
- la session Wine : `tools/sessions/session-1122-liste.txt` ;
- les essais : `hmi_editor_test`, `hmi_test`.

## 9. Les tests ajoutés

- **`hmieditor`**, *lot1122* (14 contrôles) :
  - dans un symbole : « Fonction » renommée « Move » devient une surcharge de Move ; Nouvelle surcharge en fait une troisième ; deux Ctrl+Z rendent Move seule et Fonction ;
  - renommer le paramètre Pos en Position : l'état du voyant, le corps de la fonction Move du symbole, le texte à trous de son popup, l'argument de l'instance ;
  - retirer Position : l'argument de l'instance reste, le retrait le dit, et Générer met une erreur sur l'instance ;
  - le dossier Simulation : Débogage sans point d'arrêt est une feuille ; deux points d'arrêt le déplient ; le second est désactivé (gris) ; les mêmes lignes ne refont rien ; un forçage dont la valeur change met sa ligne à jour sans refaire l'arbre.
- **`hmieditor`**, essais existants complétés : le renommage en surcharge accepté puis annulé, et refusé quand la fonction est appelée (*surcharges1120*) ; le retrait d'un paramètre qui garde les arguments des instances, y compris imbriquées, et la faute au Générer (*parametresInstances11110*) ; la saisie d'un popup pas écrite avant Entrée (*lot8_simulation_popups*) ; les étiquettes et les couleurs de l'explorateur (*explorateurs1121*, 5 contrôles de plus).
- **`hmi`**, *saisieLot8* : un REAL et un INT tapés ne sont pas écrits avant Entrée, ni après un cycle ; Échap efface le texte en attente. *popupsLot8* : un paramètre inconnu d'un popup est une erreur.

## 10. Les limites

- **Rien n'a été lancé sur un vrai Windows** : tout a été vérifié sous Wine.
- **Renommer un paramètre ne suit pas deux écritures** : le texte des arguments passé à `IHM_POPUP('Vue', 'P := …')` dans un script (une chaîne), et les chemins `Vue.Instance.P` écrits hors du symbole. Compiler signale l'argument inconnu dans le premier cas.
- **Un paramètre retiré, employé dans le symbole lui-même** (« Vanne.Etat » dans un voyant) : l'emploi est signalé comme un nom inconnu quand le programme de l'automate est chargé. Sans lui, le nom pourrait être une variable de l'automate, et le contrôle ne le juge pas.
- **L'explorateur** : le reste de la maquette (filtres, en-têtes de domaine sur deux lignes, pied, légende) vient en 1.11.23 (§ 5).
- **La source du guide Word** (`tools/guide-ihm/guide-ihm.txt`) n'a pas suivi les versions depuis la 1.11.2 : l'aide F1 (le C++) a été modifiée directement, comme dans les versions précédentes. Le guide Word devra être resynchronisé avant d'être régénéré.

## Tester vous-même

1. Dans un symbole, onglet Fonctions : choisissez une fonction non virtuelle, puis **Nouvelle surcharge**. Ajoutez-lui un paramètre : l'arbre montre « · 2 surcharges ».
2. Créez une fonction « Fonction » sans paramètre, puis renommez-la du nom d'une fonction qui a des paramètres et qu'aucun code n'appelle : c'est accepté. Renommez-la du nom d'une fonction appelée : le refus dit pourquoi.
3. Dans un symbole, renommez un paramètre (inspecteur, ligne Nom) : ses objets, ses fonctions, ses popups et les arguments des instances suivent.
4. Retirez un paramètre employé (ligne Ordre › Supprimer) : la question liste ses emplois. Confirmez, puis **Compiler** : les instances qui le donnent sont en faute. Ctrl+Z le rend.
5. Posez un point d'arrêt et un forçage, puis dépliez Simulation dans l'arbre : Débogage et Forçages les montrent ; un clic y mène.

## Installer

1. Lancez `XPGAnalyser-Setup-1.11.22.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.22.exe` (15,3 Mo, 16 002 743 octets) :
`8DA606C41B7E6978791CE1466F57E362E162B63990C4EED4A56BF24C3D55CBB3`

Le zip portable `XPGAnalyser-1.11.22-portable.zip` (20,4 Mo) contient le même exe et les mêmes fichiers que l'installateur, dans un dossier `XPGAnalyser-1.11.22`. Décompressez-le, puis lancez `XpgAnalyzer.exe`. Son empreinte SHA-256 :
`54C402D959574DDC097A30EF317C06CA57E8C6E2AC58D9F324F865F8F97812B2`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (66 sur 66). Elle comprend les 6 583 contrôles de `hmieditor` (dont 28 nouveaux) et les 4 684 de `hmi` (dont 4 nouveaux), ainsi que les deux traces de référence, `hmitrace` et `hmitracemig`.

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.22` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session du § 7 a été rejouée avec l'exe livré : 5 captures `1122_*`, aucun contrôle en échec.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé pour tous les comptes, par-dessus la 1.11.21 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.22`.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. La section « Tester vous-même » donne le parcours à refaire chez vous.

## La suite

**La 1.11.23** :
- la **souris** et le **clavier** dans les variables système (SYS) : la position, les boutons, la molette, l'objet sous la souris ; la dernière touche, les touches enfoncées, Ctrl, Maj, Alt, la durée d'appui ;
- dans chaque vue et chaque popup, une section **Raccourcis** : une touche (ou Ctrl+S, F5…) liée à une action, avec son déclencheur : **front montant** (touche enfoncée), **front descendant** (touche relâchée), **durée** (maintenue N ms) ;
- la fin de l'explorateur : les puces de filtre, les en-têtes de domaine sur deux lignes, le pied de santé, la légende ;
- le bouton Valider du popup « Saisie d'une consigne » qui valide la saisie.
