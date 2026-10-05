# XPGAnalyser 1.11.10 — les fonctions et les popups d'un symbole, les fonctions et les opérateurs entre eux

Livrée le 05/10/2026 au soir. Elle reprend vos demandes :

| Votre demande | Où |
|---|---|
| « des fonctions aux symboles utilisateurs, avec des icônes en violet quand on déballera » | l'éditeur du symbole, sous-onglet **Fonctions** ; l'arbre du projet |
| « les mettre en virtual, qui seront alors modifiables par les instances » | l'inspecteur de l'instance, section **Fonctions du symbole** |
| « les fonctions peuvent utiliser toutes les variables dispos, comme les scripts généraux ; aide à la saisie ; type void ou avec retour ; tous les types en arguments et retours » | le corps d'une fonction de symbole |
| « les fonctions des symboles sont disponibles dans les scripts généraux, et visibles partout où on déballe les instances ou les symboles » | `Vue_Vannes.Vanne_3.Ouvrir()` ; l'arbre, l'aide à la saisie |
| « qu'un symbole ait ses propres popups dans sa structure à lui, afficher ça quand on déballe un symbole et les instances » | sous-onglet **Popups** ; l'arbre |
| « que les fonctions soient utilisables entre elles et dans les opérateurs, que les opérateurs puissent s'utiliser entre eux » | le moteur des scripts |
| « quand je modifie les paramètres d'un popup ou d'un symbole, il faut mettre à jour les instances » | corrigé (§ 7) |
| « comment puis-je avoir des mauvaises données sur un esclave simulé avec des zones mémoire à 65535 ? » | corrigé (§ 8) |

Fichiers livrés : `XPGAnalyser-Setup-1.11.10.exe` (l'installateur), ce document, CAPTURES captures, et la maquette `maquette-1.11.10.html` envoyée plus tôt.

Les exemples suivent la maquette : le symbole `S_Vanne` (paramètres `Vanne : Vanne` et `Nom : STRING`), posé deux fois dans `Vue_Vannes` :
- `Vanne_3`, liée à `V[3]` ;
- `Vanne_4`, liée à `V[4]` (votre `V : ARRAY[0..63] OF Vanne`, sur l'Esclave virtuel 1).

## 1. Les fonctions d'un symbole

L'éditeur du symbole a un nouveau sous-onglet, **Fonctions**. On y retrouve le même éditeur que pour les fonctions IHM :
- la liste, avec des **icônes violettes** ;
- les propriétés ;
- l'éditeur ST, avec les couleurs, l'aide à la saisie et la ligne du nom sous le curseur ;
- les diagnostics.

**Une fonction de symbole a :**

| | |
|---|---|
| un nom | unique dans le symbole ; ni le nom d'un paramètre, ni `SUPER` |
| un type de retour | **(aucun)** pour une procédure (void), ou n'importe quel type : élémentaire, STRING, TIME, une structure ou une énumération IHM, un ARRAY… |
| des paramètres | `VAR_INPUT`, `VAR_IN_OUT`, de tous les types, avec leur valeur par défaut |
| la case **Virtuelle** | une instance peut la redéfinir (§ 3) |
| un corps ST | il lit et écrit **les paramètres du symbole**, c'est-à-dire ceux de l'instance qui l'appelle. Il voit toutes les variables IHM, l'automate et `SYS.`, comme un script général, et il appelle les autres fonctions et les opérateurs |

Dans `Ouvrir`, `Vanne.CMD_OUV := TRUE;` écrit `V[3].CMD_OUV` pour Vanne_3 et `V[4].CMD_OUV` pour Vanne_4.

**Les propriétés** disent aussi quelles instances redéfinissent la fonction (« Redéfinie par ») et comment l'appeler.

**Renommer une fonction** renomme ses appels partout : dans le symbole, dans les vues, dans les scripts et les fonctions, ainsi que les redéfinitions des instances. Un seul Ctrl+Z reprend tout.

Capture : `11110_01_fonctions_du_symbole.png`.

## 2. Où l'appeler, et comment l'écrire

| D'où | Comment |
|---|---|
| dans le symbole : ses objets, ses actions, ses scripts, ses autres fonctions, ses popups | `Ouvrir('bouton')` |
| dans la vue qui pose l'instance | `Vanne_3.Ouvrir()` |
| partout ailleurs : scripts généraux, fonctions IHM, opérateurs, autres vues, actions « Exécuter un script » | `Vue_Vannes.Vanne_3.Ouvrir()` |
| dans une expression de vue (en lecture, comme les fonctions IHM) | `{Vanne_4.Etat()}` |

Les fonctions d'un symbole posé **dans un autre symbole** s'appellent aussi : `Vue.Groupe_1.Vanne_2.Fermer()`.

**L'aide à la saisie :**
- après `Vanne_3.` ou `Vue_Vannes.Vanne_3.`, elle propose les fonctions de l'instance (avec « redéfinie ici » ou « virtuelle »), puis ses variables publiques ;
- dans le symbole, elle propose ses fonctions par leur nom.

**Compiler (F7)** contrôle ces appels : la fonction, le nombre d'arguments, le retour d'une procédure.

Capture : `11110_06_script_general.png`, le script général `Sequence_Matin`.

## 3. Une fonction virtuelle, redéfinie dans une instance

L'inspecteur d'une instance gagne la section **Fonctions du symbole** :

| La fonction | La case | Le bouton … |
|---|---|---|
| virtuelle | « du symbole » ou « redéfinie ici » | ouvre la fenêtre du script de la redéfinition |
| non virtuelle | « du symbole (non virtuelle) », en lecture | — |

**Comment faire :**
- **« redéfinie ici »** donne à l'instance son propre corps, avec celui du symbole pour départ.
- **Le bouton …** ouvre la fenêtre du script de la 1.11.9. Le corps y est contrôlé comme une fonction : ses entrées, son résultat, les paramètres du symbole. Il peut rappeler le corps du symbole par **`SUPER.Ouvrir(Motif)`**.
- **« du symbole »** efface la redéfinition.
- Chaque geste est annulable par Ctrl+Z.

La signature (nom, paramètres, retour) reste celle du symbole : seule l'implémentation change.

**Dans l'exemple**, `Vanne_3` (l'entrée gaz) redéfinit `Ouvrir` :

```
IF NOT Purge_OK THEN
    Journaliser('Vanne_3 : purge non faite');
    RETURN;
END_IF;
SUPER.Ouvrir(Motif);   (* le corps du symbole *)
```

Captures :
- `11110_04_inspecteur_instance.png` : la section, Ouvrir redéfinie ici ;
- `11110_05_redefinition_fenetre.png` : la fenêtre de la redéfinition, avec `SUPER.Ouvrir`.

## 4. Les popups d'un symbole

Le sous-onglet **Popups** de l'éditeur du symbole liste ses popups : **Nouvelle popup**, **Ouvrir le dessin** (un onglet, comme une vue) et **Supprimer**. À droite, « Ce qu'elle connaît » : les paramètres du symbole et les fonctions de l'instance.

**Une popup du symbole :**
- **connaît d'office les paramètres du symbole.** Ouverte depuis Vanne_3, `Vanne` y vaut `V[3]` et `Nom` « Vanne d'entrée gaz », sans rien passer ;
- **appelle les fonctions de l'instance qui l'a ouverte.** `Ouvrir('popup')` dans Pop_Vanne ouvre Vanne_3 avec sa redéfinition, ou Vanne_4 avec le corps du symbole ;
- peut déclarer ses propres paramètres, passés comme d'habitude.

**L'ouvrir :**
- dans le symbole : action « Ouvrir une popup », cible `Pop_Vanne` ;
- dans la vue qui pose l'instance : cible `Vanne_3.Pop_Vanne` ;
- depuis un script : `IHM_POPUP('Vue_Vannes.Vanne_3.Pop_Vanne')`.

Elle est rangée **sous son symbole**, et n'apparaît plus dans IHM › Popups.

Capture : `11110_02_popups_du_symbole.png`.

## 5. L'arbre du projet, déballé

| Sous… | On voit |
|---|---|
| le symbole | ses parties, plus **Fonctions** (icônes violettes, « virtuelle ») et **Popups**, qui se déballent comme des vues |
| chaque instance (Vue › Objets › Vanne_3) | **Fonctions (n)**, avec « redéfinie » ou « du symbole », et **Popups (n)** |

**Un double-clic :**
- sur une fonction du symbole : ouvre son éditeur sur elle ;
- sous une instance : choisit l'instance, et son inspecteur montre ses fonctions.

Capture : `11110_03_arbre_deballe.png`.

## 6. Les fonctions et les opérateurs, entre eux

| Dans le corps de… | une fonction IHM | une fonction de symbole | un opérateur |
|---|---|---|---|
| une fonction IHM | oui | oui (`Vue.Instance.F()`) | oui (`v1 + v2`) |
| une fonction de symbole | oui | oui (`Etat()`, `Vanne_4.Fermer()`) | oui |
| un opérateur | oui (`Bonus(x)`) | oui | oui (`Resultat := a + a`) |

**Les appels imbriqués** vont maintenant jusqu'à **32 niveaux** (8 avant). Au-delà, l'appel s'arrête (« appel circulaire »), le journal le dit, et l'IHM continue.

Capture : `11110_07_operateur_emploie_operateur.png`, l'opérateur `T_VEC * REAL` :
- `Resultat := a + a` emploie l'opérateur `T_VEC + T_VEC` ;
- `Bonus(Resultat.X)` appelle une fonction IHM.

## 7. Correction : les paramètres d'un symbole ou d'une popup, et leurs instances

**Ce qui se passait :** renommer un paramètre de symbole laissait l'ancien nom dans les instances. `Name := 'A'` restait, le nouveau paramètre prenait sa valeur par défaut, et l'inspecteur montrait l'ancien argument. Supprimer ou déplacer un paramètre n'était pas suivi non plus.

**Maintenant :**

| Geste | Les instances (toutes les vues, symboles compris) et les boutons qui ouvrent la popup |
|---|---|
| renommer | `Name := 'A'` devient `Titre := 'A'` |
| supprimer | l'argument du paramètre part |
| monter, descendre | les arguments positionnels (`Voiture;50`) passent en nommés : ils gardent leur sens |

L'inspecteur d'une instance ouverte dans un autre onglet suit aussitôt. Un seul Ctrl+Z reprend le paramètre et ses instances.

## 8. Correction : des valeurs « mauvaises » sur un esclave simulé

Ce n'étaient pas les zones mémoire. **Un script qui écrit à chaque image** remplissait la file d'écritures de la liaison plus vite qu'elle ne l'envoyait (une requête par écriture, au temps de réponse de l'esclave). La liaison ne repassait jamais aux lectures : les valeurs vieillissaient, « ancienne » puis « mauvaise » au bout de 30 s (« pas relue depuis 2 min 49 s »).

**Maintenant :**
- la liaison n'envoie que les écritures déjà en attente au début de son cycle, **puis lit** ;
- les écritures d'une même case en attente **se regroupent**. Dans l'essai, 64 positions écrites à chaque image pendant 4 s ont donné environ 850 envois pour 14 000 demandes. La dernière valeur part toujours, et un 1 suivi d'un 0 (une impulsion) part en entier ;
- une lecture n'efface plus une valeur écrite qui n'est pas encore partie.

## Installer

1. Lancer `XPGAnalyser-Setup-1.11.10.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisir « Informations complémentaires », puis « Exécuter quand même ».

**Compatibilité des projets :** ceux enregistrés par la 1.11.10 gardent les fonctions des symboles, les redéfinitions des instances et les popups des symboles. Une version plus ancienne qui les relit les ignore : un avertissement par ligne inconnue, et la popup redevient une popup du projet.

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.10.exe` (TAILLE) :
`EMPREINTE`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (55 sur 55). Les nouveaux contrôles :

| Test | Ce qu'il vérifie |
|---|---|
| `fonctionsSymbole11110` (hmi_test) | les appels qualifiés à l'expansion ; un script général appelle `Vue_V.V1.Ouvrir()` ; la redéfinition de V2 et `SUPER` ; une fonction qui appelle les siennes (`Ouvrir(Pas := Etat())`) ; un opérateur qui emploie `+` et une fonction ; le bouton du symbole dans chaque instance ; `V1.Ouvrir(5)` depuis la vue ; la popup du symbole ouverte depuis le symbole, depuis la vue (`V2.Pop_Vanne`) et par `IHM_POPUP` (Pos lu, Ouvrir de l'instance) ; une expression `Vue_V.V1.Etat()` ; la boucle sans fin arrêtée ; enregistré puis relu |
| `fonctionsSymboleEditeur11110` (hmi_editor_test) | les sous-onglets Fonctions et Popups ; une fonction ajoutée, virtuelle, un nom refusé ; renommer (le script général suit) et Ctrl+Z ; une popup du symbole ; l'inspecteur de l'instance (redéfinie ici, la fenêtre, SUPER sans faute, Ctrl+Z, du symbole) ; l'arbre déballé |
| `parametresInstances11110` (hmi_editor_test) | renommer, déplacer, supprimer un paramètre de symbole ou de popup : les instances (positionnelles, nommées, imbriquées) et les appelants suivent ; l'inspecteur ouvert ; Ctrl+Z |
| `ecrituresContinues11110` (hmi_editor_test) | un esclave simulé, 64 écritures par image pendant 4 s : la variable seulement lue reste bonne et suit l'esclave ; les dernières positions arrivent ; les écritures se regroupent |

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.10` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session `session-11110-fonctions-symbole.txt` a été rejouée sur une copie d'Armoire_Gaz : ce sont les captures.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux : INSTALL.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Merci de vérifier sur votre projet :
- la redéfinition d'une fonction dans une instance ;
- votre script qui écrit les positions : les vannes doivent rester bonnes sur l'esclave simulé.

Les sources sont dans le dépôt GitHub, branche `claude/hopeful-goodall-h0srq0`, avec la session de captures `tools/sessions/session-11110-fonctions-symbole.txt` et son projet (`tools/sessions/preparer-projet-11110.cpp`).
