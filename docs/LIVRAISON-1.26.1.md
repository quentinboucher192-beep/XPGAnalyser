# API Studio et IHM Studio 1.26.1 : la police du logiciel, la police en formule, les listes rangées, les couleurs sémantiques et les constantes à portée de main, la Corbeille du projet

Faite à partir de tes demandes du 06/11. Tes mots :
- « fait la police du logiciel et les polices doivent pouvoir prendre un fx » ;
- « faire quelque chose de mieux si y'en a bcp on pourrait vite s'y perdre » (les listes Police et Style nommé) ;
- « virer la partie 'Variables IHM' ici dans scripts généraux » ;
- « la corbeille ne fonctionne pas je supprime des choses ça n'apparait pas dedans (j'ai supprimé une ressource, et une vue) » ;
- « comment accéder aux couleurs sémantiques dans les scripts/propriétés ?? à corriger » ;
- « ici ajouter tous les @ qui correspondent avec les constantes de vues/scripts qui ont le même type ; dans constante les constantes globales (couleurs sémantiques, constantes globales de programmation générale) ; dans symbole/vue les constantes de vue/scripts ».

Elle se met à jour par-dessus la 1.26.0 et garde tes données.

## 1. Ce qui change pour toi

### Les listes longues, rangées
**La case Police** (objets, styles, Configuration › Affichage) :
- des **sections** : Thème, Du projet, Mes polices, Bibliothèque › Sans empattement, › Avec empattement, › Chasse fixe. Un clic sur un titre le replie ;
- **une ligne par famille, écrite dans sa police**, ses styles en **pastilles** à droite (Normal, Gras, Italique). Un clic sur une pastille prend ce style ; un clic sur la ligne prend le style Normal. Au clavier : les flèches gauche et droite ;
- à droite de chaque police du projet, ce qui l'emploie (« 3 objets »).

**La case Style nommé** : (aucun), les styles du projet (par dossier), puis les fournis. Chacun a son **aperçu** (son remplissage, son contour, « Aa » dans sa couleur de texte) et ses objets.

**Chercher** : toute liste de plus de douze choix a un champ de recherche en tête. Tape « gras », « mono » : la liste se réduit. Échap vide la recherche, un second Échap ferme la liste.

### La police en formule (fx)
- La case **Police** accepte une formule : `=SEL(Defaut_P1, 'Carlito Gras', 'Police par défaut')`.
- Le nom rendu se trouve sans casse ni accents : le nom de la ressource (avec ou sans .ttf), le nom de la police (« Carlito Gras »), sa famille (« Carlito » : le style Normal), « Police par défaut », « Police des valeurs ».
- **Générer** vérifie chaque nom écrit. Il signale une police de la bibliothèque pas encore importée (le poste n'aura que celles du projet) et un nom inconnu.

### La police du logiciel
- Affichage › **Thème…**, en bas : **Police du logiciel**. C'est celle des menus, des arbres, des tableaux et des fenêtres.
- Choix : **D'origine**, ou une police de la bibliothèque (fournies, Mes polices), chacune écrite dans sa police.
- Elle s'applique tout de suite. **Garder** la retient, **Revenir** remet l'ancienne.
- Les **IHM gardent leurs polices** : une vue se dessine pareil sur tous les postes.

### Les couleurs sémantiques et les constantes, à portée de main
- **Tape `@`** dans une formule ou un script : la liste propose les **couleurs sémantiques** (`@Alarme`, avec leur couleur ; le choix s'écrit `'@Alarme'`) et les **constantes** : celles de la vue, et les publiques des scripts généraux (`Init.MAX_P`) et des autres vues. Leur type est en tête : la case range d'abord ce qui convient.
- **La palette** d'une case couleur montre les couleurs sémantiques **avec leur nom**.
- **Choisir la valeur** (le carré de la case) :
  - l'onglet **Constante** liste les **constantes globales**, filtrées sur le type attendu : les couleurs sémantiques (une case couleur ; elles s'écrivent `@Alarme`, sans fx), les constantes des scripts généraux, celles des autres vues ;
  - l'onglet **Symbole / vue** liste les **constantes de la vue**.

### Scripts généraux : sans la partie Variables IHM
IHM › Programmation générale › Scripts généraux n'a plus le tableau VARIABLES IHM en bas, ni ses boutons. Les variables IHM restent dans leur onglet, **Variables IHM**, juste à côté.

### La Corbeille reçoit ce qu'on supprime du projet IHM
- **Ce qui y va** : les vues (un symbole avec ses popups), les ressources (images, sons, polices, vidéos, avec leurs octets), les fichiers externes, les scripts généraux, les fonctions, les variables et les types IHM, les alarmes, les recettes, les styles.
- **Où** : un dossier par genre, en tête de la Corbeille (Vues, Ressources, Scripts généraux…), avant les enregistrements, incidents, versions, macros et éléments de l'Assistant IA. « Corbeille [N] » dans l'arbre se met à jour dès la suppression.
- **Restaurer** remet l'élément dans sa liste et dans son dossier. Si son nom a été repris entre-temps, il prend le premier nom libre (Vue_Accueil_2) et la Corbeille le dit.
- **Ctrl+Z** d'une suppression reprend l'élément de la Corbeille ; **Ctrl+Y** l'y remet ; un seul Ctrl+Z annule une restauration.
- **Enregistrée avec le projet**, dans `ihm/supprimes/`, et retrouvée à la réouverture. Une version plus ancienne d'IHM Studio ouvre toujours le projet (elle ignore ce dossier, sans le toucher).
- Ce qui y est depuis plus de 7 jours part à l'ouverture du projet ; **Supprimer définitivement** et **Vider** le retirent aussi du disque, tout de suite.
- **N'y vont pas** : les objets supprimés dans une vue (Ctrl+Z les rend), les scripts et fonctions propres à une vue, les utilisateurs et groupes, les couleurs sémantiques, les équipements.

## 2. Les captures

L'application installée a été ouverte dans une fenêtre (sous Wine), sur le projet de démonstration Armoire_Gaz, et pilotée par une session (`--script`, `--captures`). Les huit images jointes :
1. `1261_01_liste_police.png` : la case Police d'un objet, ses sections (Thème, Du projet, Bibliothèque › Sans empattement…), chaque famille dans sa police et ses styles en pastilles ;
2. `1261_02_police_recherche.png` : la même liste, la recherche « gras » (7 choix) ;
3. `1261_03_style_nomme.png` : la case Style nommé, les styles du projet par dossier puis les fournis, chacun avec son aperçu ;
4. `1261_04_palette_semantique.png` : la palette d'une case couleur, les couleurs sémantiques avec leur nom ;
5. `1261_05_formule_arobase.png` : `@` tapé dans une formule, la liste propose `@Alarme critique` et `@Alarme` avec leur couleur ;
6. `1261_06_police_du_logiciel.png` : Affichage › Thème…, la Police du logiciel en bas ; DejaVu Serif choisie s'applique tout de suite à tout l'écran ;
7. `1261_07_scripts_generaux.png` : Scripts généraux, sans la partie Variables IHM ;
8. `1261_08_corbeille_projet.png` : la Corbeille après la suppression d'une vue (Vue_Donnees) et d'une ressource (schema.bmp) : un dossier Vues et un dossier Ressources, en tête, avant les enregistrements.

## 3. Les essais

Sous Linux : **135 suites d'essais sur 135** réussies (CTest).

Au premier passage, l'essai de l'éditeur avait un échec : il cherchait encore le bouton « Nouvelle variable IHM » dans Scripts généraux, que tu m'as demandé de retirer. C'est l'essai qui a été corrigé ; il vérifie maintenant que la partie n'y est plus.

- **`hmi_editor_test`** : 10 914 contrôles (10 846 en 1.26.0). Ce qui est nouveau :
  - **la Corbeille du projet** (51) :
    - une vue supprimée y entre, Ctrl+Z l'en ressort, Ctrl+Y l'y remet ;
    - Restaurer rend la vue avec ses objets ; un nom repris donne Vue_Accueil_2 ;
    - un symbole et ses popups ;
    - une ressource : ses octets identiques après enregistrement, réouverture et restauration ;
    - huit autres genres en une commande, la suppression depuis l'arbre ;
    - la purge après 7 jours, Supprimer définitivement (sur le disque aussi) ;
    - un objet supprimé dans une vue n'y entre pas ;
  - **les « @ », les constantes et les listes riches** (12) :
    - `@` propose `@Alarme`, qui s'écrit `'@Alarme'`, et `Init.MAX_P` (REAL) ;
    - dans une chaîne ouverte, le nom se complète ;
    - Choisir la valeur : l'onglet Constante d'une case couleur montre les couleurs (sans fx), celui d'une case REAL montre MAX_P (en fx) ; Symbole / vue montre la constante de la vue ;
    - la liste Police (section Thème, Liberation Sans Gras en pastille) et la liste Style nommé (aperçus) ;
  - **Scripts généraux** : plus de partie Variables IHM.
- **`hmi_test`** : 6 973 contrôles (6 965 en 1.26.0). La police en formule :
  - les noms trouvés sans casse ni accents (ressource, nom, famille, Police par défaut, Police des valeurs) ;
  - les noms écrits d'une formule, ce que Générer dit d'une police pas importée ou inconnue ;
  - la liste riche (sections, familles, styles).
- **`popup_test`** : la nouvelle liste déroulante.
  - Une liste courte est la même qu'avant ;
  - les titres, les familles et leurs pastilles, au clic et au clavier ;
  - replier une section ;
  - la recherche (sans accents, la hauteur qui ne bouge pas, Échap, Retour arrière).
- **`truetype`** : 280 contrôles, toujours sans échec.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.26.1.exe` : l'installateur (52 356 019 octets). Il met à jour la 1.26.0 au même endroit ;
- le même installateur dans un zip ;
- `API-Studio-1.26.1-portable.zip` et `IHM-Studio-1.26.1-portable.zip` (188 fichiers chacun) ;
- les huit captures ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.26.1.exe` | `031049F6F7A106522FB38E6ED8E0C7B564A26964281FCB60E340CC89A5A6537C` |
| `API-Studio-1.26.1-portable.zip` | `E4E46442216237B900AFDAD59F6E850EF4C5DFA5A203A75AB58E837697149D00` |
| `IHM-Studio-1.26.1-portable.zip` | `319CCC9D0FDA530ABF2CAC007C7B38A606DF8C2AC20F4E45BCCF45B2F1AB9530` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.26.0 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.26.1 » et « IHM Studio 1.26.1 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.26.0 est gardée ;
- **IHM Studio a été ouvert dans une fenêtre** (sous Wine) : les huit captures viennent de l'exécutable livré.

## 5. Limites

**Les limites de la 1.26.0, corrigées** :
- la case Police est maintenant écrite dans chaque police ;
- la police du logiciel se choisit ;
- la Corbeille reçoit les vues et le reste du projet IHM.

**Ce qui reste** :
- **Les constantes des scripts d'une vue** (OnOpen, OnCycle…) ne sont proposées ni après `@` ni dans Choisir la valeur. Le moteur ne les lit que dans leur script : une formule d'objet ne peut pas les employer. Celles de la vue, et les publiques des scripts généraux, le sont.
- **La Corbeille** :
  - n'y vont pas : les objets supprimés dans une vue (Ctrl+Z les rend), les scripts et fonctions propres à une vue, les utilisateurs et groupes, les couleurs sémantiques, les équipements, les scénarios d'essai, les mises en page de rapports ;
  - le menu des chemins (colonne Origine) est grisé pour un élément du projet : il n'a pas de fichier à lui ;
  - après « Supprimer définitivement », un Ctrl+Y de la suppression d'origine remet l'élément dans la Corbeille (refaire une suppression supprime de nouveau).
- **La police en formule** : un nom calculé (`Nom_Police`) ne se vérifie qu'en marche ; un nom inconnu donne la police par défaut.
- **Générer**, sur une formule de police, lit tous les textes entre apostrophes : un texte qui n'est pas une police (une comparaison `Etat = 'Marche'`) est signalé lui aussi.
- **La police du logiciel** : une seule face par taille, comme avant. Les textes en gras de l'interface prennent la même police, sans fichier gras.
- **L'aperçu d'un style nommé** dans la liste montre son remplissage, son contour et sa couleur de texte, pas toutes ses propriétés.
- **La recherche des listes** n'apparaît qu'au-delà de douze choix.
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows.**
