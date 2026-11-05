# API Studio et IHM Studio 1.23.1 : les couleurs sémantiques, le style en formule, Styles déplié

Faite à partir de tes demandes du 05/11. Tes mots :
- « le choix de base du style peut-il être sur un fx ? … ok pouvoir mettre un fx oui » ;
- « ajuster le menu contextuel sur clic droit dans Styles (même les styles lockés doivent être pris dans le compteur de styles dans le menu de gauche) » ;
- « Ajouter un type : du déballage/réduire, des dossiers par catégories et de petites icônes à droite » ; « pareil pour Ajouter une propriété, avec le type de la variable, sans icône » ;
- « dans Styles, un onglet Couleurs sémantiques (globales), utilisables partout comme des constantes globales, pouvoir en définir soi-même » ;
- « mettre à jour le déballage de Styles aussi, avec les styles, les couleurs sémantiques, des icônes locké et utilisateur pour distinguer ».

Elle se met à jour par-dessus la 1.23.0 et garde tes données. Tes projets se relisent tels quels.

## 1. Ce qui change pour toi

### Les couleurs sémantiques

Des couleurs globales sous un nom, utilisables partout comme des constantes :
- une case couleur (remplissage, contour, texte, couleur allumé…) écrit **@Alarme** au lieu d'un code. L'objet se dessine dans cette couleur, et **suit si tu la changes** ;
- la **palette** d'une case couleur les propose dans une bande en bas : un clic écrit `@Nom` ;
- une **formule** aussi : `=SEL(Defaut_P1, '@Alarme', '@Marche')`, et les fonctions de couleur (`COULEUR_ECLAIRCIR('@Alarme', 20)`) ;
- un **style**, le **fond d'une vue** et les **zones** d'un bargraphe (`0-60 = @Marche`) aussi ; une couleur peut en citer une autre.

**Douze couleurs fournies**, verrouillées, suivent l'ISA-101 : Fond, Texte, Équipement, Marche (gris foncé, pas de vert), Arrêt, Défaut, Alarme critique, Alarme, Avertissement, Information, Accent, Désactivé.

**Les tiennes** se font dans IHM › Styles, onglet **Couleurs sémantiques** (en haut de la page) :
- à gauche, la liste : les tiennes (une personne), puis les fournies (un cadenas), avec leur couleur et le nombre de citations ;
- à droite, la fiche : le nom, la couleur (la palette, ou `@Autre`), la description, le dossier, où elle est citée ;
- la barre et le clic droit : Nouvelle (Inser), Dupliquer à partir de (Ctrl+D, une fournie aussi), Copier @Nom (Ctrl+C), Retirer (Suppr) ; chaque geste se reprend par Ctrl+Z ;
- une valeur illisible, le nom d'une fournie ou une boucle (`@A` vaut `@B` qui vaut `@A`) sont refusés, avec la raison.
- **Renommer** : tout ce qui la cite suit.
- **Retirer** : ce qui la cite garde sa couleur, écrite en dur ; rien ne change à l'écran.
- **Générer** signale une couleur inconnue (un double-clic mène à l'objet), une couleur illisible, le nom d'une fournie, des citations qui tournent en rond.
- Un **paquet de vues** emporte celles que ses vues citent ; l'import ajoute celles qui manquent.

### Le style nommé par une formule (fx)

**Style nommé** accepte une formule : `=SEL(Defaut_P1, 'Contraste élevé', 'ISA-101')`.
- Dans l'éditeur, l'objet garde le style de sa valeur (le carré O, l'aperçu).
- En marche, il prend les valeurs du style que rend la formule, et les reprend quand elle change.
- Une valeur brisée garde la sienne, une propriété en formule aussi.
- Générer vérifie les noms écrits dans la formule.

### IHM › Styles dans l'arbre

- **Styles** se déplie : **Styles** et **Couleurs sémantiques**, chacun avec son nombre. Les styles fournis comptent maintenant (4 dans un projet neuf).
- Un **cadenas** pour ce qui est fourni, une **personne** pour ce qui est à toi ; une couleur a son icône dans sa teinte et sa valeur après son nom.
- Un clic ouvre la page Styles sur l'élément. Clic droit : **Nouveau style…**, **Dupliquer à partir de…**.

### La page Styles

- **Deux onglets** en haut : Styles et Couleurs sémantiques.
- **Le clic droit** choisit la ligne et ouvre le menu de sa liste ; une entrée grisée dit pourquoi (un style fourni ne se modifie pas) :
  - les styles : Nouveau style, Dupliquer à partir de…, Renommer…, Retirer, Ranger dans un dossier (sous-menu, avec Nouveau dossier…), Copier le nom ; sur un dossier : le renommer, le retirer ;
  - les types : Ajouter un type…, Retirer le type, Choisir pour l'aperçu ;
  - les propriétés imposées : Ajouter une propriété…, Retirer la propriété (aussi la touche Menu).
- **Ajouter un type…** : la bibliothèque en dossiers dépliables (ses catégories, dans l'ordre de la palette), puis Mes symboles ; chaque type a sa petite icône à droite. Le premier dossier est déplié ; une recherche déplie ce qui a un résultat.
- **Ajouter une propriété…** : les catégories de l'inspecteur, toutes dépliées, puis Paramètres du symbole ; à droite, le genre de la valeur (nombre, couleur, booléen, texte, choix, durée), sans icône.

## 2. Les essais

Sous Linux : **132 suites d'essais sur 132** réussies (CTest), dont une nouvelle.

Au premier passage, `hmieditor` a eu un échec : un contrôle comptait 245 sujets d'aide pour l'IHM, et le nouveau sujet « Les couleurs sémantiques » en fait 246. Le compte est corrigé ; l'essai repasse seul.

- **Nouvel essai : `semantic_colors_test`** (49 vérifications). Il couvre :
  - les 12 couleurs fournies (ISA-101 : la marche en gris, pas de vert), un nom accentué, sans casse, avec ou sans @ ;
  - celles du projet, une couleur qui en cite une autre, le registre que lisent le dessin, le contraste et les fonctions de couleur ;
  - citer dans une valeur, une formule, un texte, des zones de bargraphe, un style, une autre couleur, le fond d'une vue ; compter ;
  - renommer (tout suit) et retirer (la couleur reste écrite en dur) ;
  - les noms libres, dupliquer une fournie ;
  - l'enregistrement et la relecture, ce que Générer dit (inconnue, illisible, nom d'une fournie, boucle, l'objet retrouvé) ;
  - un paquet de vues qui emporte les couleurs citées et l'import dans un autre projet.
- **`hmi_editor_test`** : 9 839 contrôles (9 685 en 1.23.0). Ce qui est nouveau :
  - l'arbre : Styles déplié, les comptes (les fournis comptent), le cadenas et la personne, la teinte d'une couleur ;
  - la palette : la bande des couleurs sémantiques, un clic qui écrit `@Nom` ;
  - l'onglet Couleurs sémantiques : la liste, les gestes, les refus (fournie, illisible, boucle), retirer puis Ctrl+Z, les deux onglets de la page ;
  - les menus du clic droit des trois listes et chaque entrée grisée avec sa raison, les dossiers des deux listes de choix (déplier, replier, la recherche, le clavier, les icônes, les genres).
- `styles_types_test` : 68 vérifications, dont le style choisi par une formule.

## 3. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.23.1.exe` : l'installateur (48 570 496 octets). Il met à jour la 1.23.0 au même endroit ;
- `API-Studio-1.23.1-portable.zip` et `IHM-Studio-1.23.1-portable.zip` (167 fichiers chacun) ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.23.1.exe` | `98F9140D363AD3B8FC745CBE6A274A007DB251970DC6DEDE20D3732565179DB1` |
| `API-Studio-1.23.1-portable.zip` | `0B0DFFBB5EC3837D5246E1CC368DDAD086DEC9B23DC95014ED1A76545EABB2DB` |
| `IHM-Studio-1.23.1-portable.zip` | `C4CAA343B6F822BF8C0988DA61D28F6AD34BC9EAB3020F7E6CBED7414E09B7F6` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.23.0 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.23.1 » et « IHM Studio 1.23.1 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.23.0 est gardée.

L'interface n'a pas été ouverte dans une fenêtre.

## 4. Limites

- **Un projet relu par une version d'avant la 1.23.1** ne connaît pas `@Nom` : la case y est une couleur illisible.
- **Un script** peut écrire `'@Alarme'` dans une propriété (elle se dessine), mais Renommer, Retirer et Générer ne lisent pas les scripts ni les actions.
- **La palette** montre 20 couleurs sémantiques au plus.
- **Dans la grille des propriétés imposées**, le nouveau menu du clic droit remplace celui des cases : « Copier la couleur » et « Tout déplier / Tout replier » n'y sont plus.
- **Les dossiers des listes de choix** reviennent à leur état de départ à chaque ouverture.
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows.** Les nouveaux écrans n'ont été vus que dessinés hors écran par les essais : l'application n'a pas été ouverte dans une fenêtre.
