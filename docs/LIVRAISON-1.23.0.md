# API Studio et IHM Studio 1.23.0 : les styles par type d'objet, les styles fournis, le carré O

Faite à partir de ta demande du 05/11 et de la maquette que tu as validée. Tes mots :
- « ajoute des styles par défaut en natif, avec un cadenas, et "dupliquer à partir de" » ;
- « l'onglet Styles ne doit pas gérer que les boutons : une liste d'objets de la bibliothèque ou de symboles utilisateur, et dans chaque objet une propriété au choix » ;
- « quand on applique un style, ça override les paramètres ; on le voit dans l'objet, et on brise l'override une propriété à la fois pour garder l'ancienne valeur » ;
- « un filtre et un visuel, un O à la place du C » ;
- « l'aperçu de l'onglet Styles, sur le type choisi ».

Elle se met à jour par-dessus la 1.22.9 et garde tes données. Tes projets se relisent tels quels : un style d'avant vaut pour « tous les objets ».

## 1. Ce qui change pour toi

### Des styles fournis, verrouillés

IHM › Styles commence par quatre styles **fournis**, avec un cadenas :
- **ISA-101** : l'IHM haute performance. Gris neutres et texte sombre ; la couleur est réservée aux anomalies (défaut, alarme). Une pompe en marche est pleine, à l'arrêt vide : l'état se lit sans couleur ;
- **Sombre (d'origine)** : l'aspect d'un objet neuf du logiciel, pour revenir au départ ;
- **Clair** ;
- **Contraste élevé** : noir et blanc, jaune pour ce qui agit.

Ils visent les boutons, les textes, les rectangles, les afficheurs, les champs de saisie, les voyants, les pompes, moteurs, ventilateurs, compresseurs, vannes, les cuves, les tuyauteries et les bargraphes. Un objet peut les citer directement. Ils ne se modifient pas : **Dupliquer à partir de…** (ou Ctrl+D) en fait une copie à toi, choisie aussitôt, son nom prêt à taper. Un style du projet ne peut pas porter le nom d'un style fourni.

### Un style vise des types d'objets

La page a trois colonnes, comme sur la maquette :
- **Les styles** : les fournis, puis ceux du projet (avec leurs dossiers), chacun avec sa pastille et le nombre d'objets qui le citent. La recherche lit les noms, les descriptions, les types et les valeurs.
- **Les types d'objets du style** : **+ Ajouter un type…** ouvre une liste avec recherche, rangée **Bibliothèque** puis **Mes symboles** ; **− Retirer le type choisi**.
- **Les propriétés imposées** au type choisi : **n'importe quelle propriété** du type (couleur, nombre, case à cocher, liste de choix, texte ; pour un symbole, ses paramètres), chacune avec la case de l'inspecteur. **+ Ajouter une propriété…** propose celles du type qui n'y sont pas encore ; **− Retirer la propriété**. Au-dessus : le nom, la description, les objets qui le citent, et ce qui est à vérifier.

Chaque changement est une commande (Ctrl+Z). Les objets qui citent le style suivent, et le message dit combien suivent et combien gardent leur propre valeur. Un style fourni choisi se lit sans se modifier, avec le bandeau « Style fourni : verrouillé ».

**L'aperçu** montre le style sur le type choisi dans **Type :** : n'importe quel objet de la bibliothèque ou un de tes symboles, ceux du style en tête. Un équipement se montre en marche, à l'arrêt et en défaut.

**Créer un style** depuis un objet (barre d'outils de l'éditeur) fait maintenant un style **de son type** (un bouton fait un style des boutons).

### Les valeurs imposées : le carré O

- Appliquer un style (Style nommé, dans l'inspecteur) **impose** ses valeurs à l'objet. Chacune porte **O** dans son carré, au lieu de C, et son infobulle dit la valeur d'avant le style.
- **Le menu du carré O** :
  - « Briser : reprendre l'ancienne valeur » : la propriété reprend sa valeur d'avant le style, et le style ne l'impose plus ;
  - « Briser : garder la valeur actuelle » ;
  - « Ouvrir le style » : IHM › Styles, ce style choisi.
- **Taper une autre valeur** sur une case O la brise aussi, et la saisie est gardée.
- **Une valeur brisée** redevient C et porte l'étiquette **BRISÉE**. Sa flèche au bout de la ligne, ou son menu, la rétablit.
- **Sous Style nommé**, la ligne **Valeurs du style** dit combien de valeurs sont imposées et combien sont brisées. Sa flèche fait **Tout rétablir**, son bouton ouvre le style.
- **Le filtre O Style**, dans la barre « Montrer », ne garde que les valeurs imposées.
- **Une propriété pilotée par une formule (fx) n'est jamais imposée** : la formule garde la main, comme tu l'as validé.
- **Quand le style change**, les valeurs imposées suivent et les valeurs brisées restent celles de l'objet.
- **La case Style nommé** est toujours là et propose aussi les styles fournis.
- **Un style choisi par l'Assistant IA** s'applique de la même façon.

## 2. Les essais

Sous Linux : **131 suites d'essais sur 131** réussies (CTest), dont une nouvelle.

Au premier passage, `crashtrail` a échoué : un essai du journal des plantages, sans lien avec les styles. Machine très chargée (construction Windows en parallèle), son lecteur avait fait sa seule lecture avant la première écriture. L'essai attend maintenant d'avoir lu quelque chose ; il passe 8 fois sur 8 sous la même charge, puis dans CTest.

- **Nouvel essai : `styles_types_test`** (56 vérifications). Il couvre :
  - les styles fournis (ISA-101 sans vert pour la marche, Sombre égal à un objet neuf) ;
  - appliquer, avec la valeur d'avant retenue et une formule qui garde la main ;
  - briser (reprendre ou garder), rétablir, une saisie qui brise, tout rétablir ;
  - un autre style ensuite, ne plus citer ;
  - propager un style changé : ce qui est imposé suit, ce qui est brisé reste, une valeur changée à la main devient brisée ;
  - un projet d'avant ;
  - un paramètre de symbole ;
  - l'enregistrement et la relecture ;
  - ce que Générer dit d'un style mal formé ;
  - faire un style depuis un objet, l'aperçu.
- **`hmi_editor_test`** : 9 685 contrôles (9 580 en 1.22.9). Ce qui est nouveau :
  - **l'inspecteur** (le carré O, le menu, briser, Ctrl+Z, la flèche, une saisie qui brise, Tout rétablir, le filtre O Style, Créer un style) ;
  - **la page Styles** (69 contrôles : les styles fournis, le verrou, Dupliquer à partir de, la recherche, les types et les propriétés ajoutés et retirés, les listes avec recherche, un paramètre de symbole, l'aperçu en trois états et un symbole dessiné, Ctrl+Z et Ctrl+Y).
- **Corrigé avant livraison** :
  - la liste avec recherche relisait une ligne de la liste précédente quand on la rouvrait, et l'essai de l'éditeur plantait ;
  - un paquet de vues réimporté dans son projet paraissait différent, parce que la relecture complétait l'état des objets stylés. Désormais rien n'est réécrit à la lecture : une valeur qui diffère de celle du style compte d'office comme brisée.

## 3. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.23.0.exe` : l'installateur (48 379 568 octets). Il met à jour la 1.22.9 au même endroit ;
- `API-Studio-1.23.0-portable.zip` et `IHM-Studio-1.23.0-portable.zip` (167 fichiers chacun) ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.23.0.exe` | `AFE7DFE734E1FB6B5AE14896A8C63444DA1F703C542605D74B9805CE628B571C` |
| `API-Studio-1.23.0-portable.zip` | `A2A9DAFFFDDB5C264F2897EB4607CA939BB248B11D416411680F515CC72BF347` |
| `IHM-Studio-1.23.0-portable.zip` | `DF28098F81343A3D38625EC4BD627D8B5B2DA033FA8349DF2B4FAAFD354EF32F` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.22.9 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.23.0 » et « IHM Studio 1.23.0 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.22.9 est gardée.

L'interface n'a pas été ouverte dans une fenêtre.

## 4. Limites

- **Un type sans propriété n'est pas gardé** : ajoute-lui au moins une propriété (la liste s'ouvre dès que le type est ajouté).
- **« Tous les objets »** ne s'ajoute plus : il reste affiché, et retirable, pour les styles d'avant la 1.23.0.
- **Un voyant** se montre en deux exemplaires dans l'aperçu (allumé, éteint) : il n'a pas d'état « défaut ».
- **Un style imposé ne touche pas une propriété en formule (fx).** Pour qu'un état en couleur suive l'ISA-101, la formule doit utiliser les couleurs du style.
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows.** Les nouveaux écrans n'ont été vus que dessinés hors écran par les essais : l'application n'a pas été ouverte dans une fenêtre.
- **La machine de construction a été remplacée depuis la 1.22.9** : la chaîne de construction Windows a été réinstallée (MinGW, Wine, Inno Setup 6.4.1 et SDL3 3.4.12 officiels, empreintes vérifiées).
