# API Studio et IHM Studio 1.13.1 : les formules, les courbes, le code brut, les types IHM

Livrée le 29/10/2026. Elle suit la 1.13.0 (`docs/LIVRAISON-1.13.0.md`).

Elle répond à :

> « ok plusieurs bug, dans les formules des propriétés si je mets des constantes globales ca met erreur, ensuite quand je mets des $$ les aides à la saisie ne fonctionne plus dans les balises
> dans les courbes le il faut ajouter un mode freeze qui bloque l'acquisition de nouvelles valeurs, + pouvoir ajouter des lignes verticales pour faire des mesures de temps + pouvoir zoomer/dezoomer etirer/detendre de manière hyper zoomée, pouvoir exporter que la zone dans une balise de lignes verticales
> dans tous les objets des pages/vues avoir un onglet 'code brut' que l'on peut copier coller, ça serait un encodage brut copiable d'objets en objets, a ajouter dans les deballages »
>
> « ajoute une meilleur gestion des types IHM, là on a tout sur la meme page, un type aura ses propres onglets pour le rendre plus facilement configurable »

Tout est fait.

## 1. Ce qui change

### Les deux bugs des formules

**Les constantes globales dans une formule de propriété.** Une case ƒx de l'inspecteur (un objet de vue, de popup ou de symbole) peut lire une constante d'un script général : `=Reglages.Seuil * 10`. Elle n'est plus marquée en erreur (« Reglages n'existe pas »).
- Le carré de la case dit **Constante**.
- Le type suit le chemin : `Reglages.Four.Temp`, `Reglages.Table[1]`.
- L'aide à la saisie propose `Reglages`, puis ses constantes.
- Compiler, Générer et la marche donnent la même valeur.
- Dans sa vue, une constante de la vue s'écrit sans le nom de la vue. Un script rangé dans un espace de noms (`Gaz.Reglages`) se cite par son nom court : `Reglages.Seuil`.

L'unité d'une constante compte aussi : `EN(Reglages.Pression, 'mbar')` convertit. Dans une case ƒx, `EN` et `UNITE` convertissent maintenant aussi une variable IHM, une constante de la vue et un paramètre de popup. Avant, ils rendaient la valeur telle quelle.

> **Attention, un changement dans les scripts.** L'unité d'une constante globale compte désormais dans les scripts, comme celle d'une variable :
> - `Consigne_mbar := Reglages.Pression;` se convertit d'office (2500, et non plus 2,5) ;
> - `Consigne_mbar := Reglages.Pression * 1000.0;` devient une erreur de Compiler, qui demande `EN(Reglages.Pression, 'mbar')`.
>
> Si un de tes projets convertissait une constante à la main, Compiler te le montrera à la première ouverture.

**L'aide à la saisie dans les repères `$…$`.** La liste propose de nouveau ce qui peut y aller :
- dans un repère ouvert (`=$Mar`, `=$V[0].`) ;
- après un repère fermé suivi de son point (`=$V[0]$.`).

Elle propose les variables et leurs membres, les constantes, les paramètres d'un symbole (`$Van` → Vanne ; `$Vanne$.` → OUV, Position), et les repères déjà employés dans la vue. Ça marche dans une case ƒx, un champ, un texte à trous `{…}` et un script. Hors des repères, la liste marche comme sans `$`.

### Les courbes en marche

Sur une courbe **Pilotable** (case dans ses Propriétés), en simulation et sur le poste d'exploitation :

- **Figer** (bouton à côté de Pause, touche **F**) arrête l'**acquisition**. Aucune mesure n'entre dans les plumes et aucune n'en sort, aussi longtemps qu'il faut. Le cadre passe au bleu glacé et dit « FIGÉE depuis 14:02:31.125 ».
  - **Reprendre** relance l'acquisition. Le temps du gel reste un trou dans la courbe : une bande pâle marquée « gel », avec la plume coupée.
  - La **Pause**, elle, ne fige que l'affichage : les mesures continuent derrière.
- **Curseurs** (touche **C**) pose deux lignes verticales, A et B.
  - Un clic sur la courbe pose A, puis B, puis déplace la plus proche. On les tire aussi à la souris.
  - Leur cadre donne l'écart Δt (2.35 s, 12.5 ms, 250 µs) et l'heure de chacune à la milliseconde.
  - Pour chaque plume, il donne la valeur en A, en B et la différence. Les trois sont arrondies avec le même nombre de décimales (46.73, 32.01, -14.72).
- **Zoomer très près** :
  - la molette zoome le temps autour de la souris, jusqu'à 0,1 ms sur toute la largeur ; en direct, le bord droit reste le présent ;
  - **Maj+molette** zoome l'axe des valeurs ;
  - glisser déplace la fenêtre ;
  - **Tout voir** (touche **Début**) revient à la durée et à l'échelle de l'objet.

  Le zoom marche figée, en pause ou en marche. Les graduations parlent dans l'unité de la fenêtre (-2.5 ms).
- **Exporter la zone** : les boutons **CSV** et **Excel** du cadre des curseurs écrivent seulement les mesures entre A et B. Chaque ligne donne l'heure, l'écart à A en secondes, puis chaque plume (`Courbe_1_zone.csv`, là où tu choisis).
  - L'action Exporter, le bouton d'export et `IHM_EXPORTER` le font aussi, avec la source `zone:Courbe_1` (proposée dans leur liste Données, vérifiée par Générer).
- En simulation, le **clic droit** sur la courbe propose Figer, Curseurs, Tout voir et Exporter la zone.
- Une courbe qui a ses propres actions (Clic, Double clic, Appui long) les garde. Sans curseurs, un clic sur le tracé les déclenche, comme avant.
- **Corrigé** :
  - une courbe en pause plus longtemps que sa durée affichée perdait les mesures de la fenêtre montrée, et restait vide ;
  - en escalier, la courbe montrait la valeur suivante trop tôt.

### Le code brut des objets

- **L'onglet Code brut** de l'inspecteur montre le texte complet d'un objet choisi seul : celui que l'enregistrement écrit. Il contient :
  - les propriétés et les formules ƒx ;
  - les actions ;
  - le contenu (plumes, lignes, états) ;
  - les fonctions redéfinies et les alarmes surchargées ;
  - pour un groupe, ses objets.
- **Copier** met ce texte dans le presse-papiers.
- **Coller** remplace l'objet par le code copié : d'un objet du même genre, d'une autre vue ou d'un autre projet.
- On peut aussi modifier le texte, puis cliquer sur **Appliquer**.
- L'objet garde son nom et sa position. La case « Prendre aussi le nom et la position » prend ceux du code.
- Une faute de syntaxe donne sa ligne et ne change rien. Un code d'un autre genre d'objet est refusé. Ctrl+Z annule.
- **Partout où les objets sont déballés**, le clic droit propose Copier le code brut, Coller le code brut et Code brut… (l'onglet). C'est le cas sur la vue, dans l'Explorateur d'objets et dans l'arbre du projet sous une vue.
  - Sur la vue en marche en Simulation, on peut copier et coller le code brut.
  - Dans l'onglet Expressions, sur un objet déballé, on peut copier le code brut.
- **Ctrl+V d'un code brut dans une vue** (ou Coller ici) en pose les objets. Ce sont des objets neufs, avec des noms libres.

### Les types IHM dans leurs onglets

- **Le type choisi dans Types IHM se règle dans ses onglets**, comme une fonction IHM. Chaque onglet prend toute la hauteur de la page.
  - Une structure : Membres (la grille, puis les propriétés du membre choisi), Méthodes, Opérateurs, Héritage et interfaces (Hérite de…, la lignée, les sous-types, Implémente), Propriétés, Emplois.
  - Une interface : Signatures, Implémenteurs, Propriétés.
  - Une énumération : Valeurs, Opérateurs, Propriétés, Emplois.
  - Un rappel : Signature, Opérateurs, Propriétés, Emplois.
- Le titre de chaque onglet compte ce qu'il montre (Membres 4, Méthodes 5). L'onglet choisi reste quand on passe d'un type à l'autre.
- **Emplois** dit où le type sert : variables, membres, sous-types, paramètres et locales, popups. Un double-clic y mène.
- **Un type dans son propre onglet** s'ouvre de trois façons : un double-clic dans la liste des Types IHM, un double-clic dans l'arbre du projet, ou le clic droit › Ouvrir dans un onglet. Il s'ouvre comme une vue, avec son nom et les mêmes onglets, sans la liste.
  - Plusieurs types s'ouvrent à la fois.
  - Renommer le type renomme l'onglet ; le supprimer le ferme.
  - L'arbre suit l'onglet actif. Afficher dans l'arbre et Aller à (Ctrl+K, qui propose aussi les types IHM) font le va-et-vient.
- La liste des types a des colonnes qui se lisent : le nom entier, et Utilisé par qui prend la place libre.

### L'aide

Pas de nouveau sujet F1. Treize passages sont ajoutés et marqués « nouveau en 1.13.1 », dans ces sujets :
- L'éditeur de vues : le code brut ;
- Dupliquer avec des repères : l'aide dans les `$…$` ;
- Courbe : figer, curseurs, zoom, export de la zone ;
- Les types IHM ;
- Les énumérations IHM ;
- Les types objets : les onglets Méthodes et Héritage et interfaces ;
- Variables locales : les constantes globales.

Les notes de version 1.13.1 (11 notes) résument tout.

## 2. Les essais

**92 suites sur 92**, sur le tout fusionné et en version 1.13.1. `hmieditor` passe de 8434 à 9152 contrôles. Il compte 449 notes, 66 versions et 254 sujets d'aide, et le guide de l'aide et son C++ disent la même chose.

Les essais ajoutés :
- `hmicheckexpr` :
  - les constantes globales dans une formule : type, chemin, unité, espace de noms ;
  - `EN` et `UNITE` par la portée (variable, constante de vue, paramètre de popup).
- `hmieditor` :
  - les deux bugs des formules, reproduits avant d'être corrigés : l'inspecteur, l'aide à la saisie dans et après les repères `$…$`, sans les membres privés ;
  - le code brut : l'aller-retour de chaque famille d'objets, Coller sur un objet du même genre, une faute, un autre genre, Ctrl+Z, le clic droit, Ctrl+V, la vue en marche ;
  - les types en onglets : les onglets par genre de type, les comptes, l'onglet de document d'un type, les colonnes lisibles ;
  - la courbe en marche : figer, curseurs, zoom, export de la zone, les actions de clic gardées, le mode historique.
- `hmi` :
  - le modèle de la courbe : la fenêtre, le gel, les mesures entre deux instants, la valeur aux curseurs, l'arrondi des valeurs ;
  - une courbe en pause qui garde ses mesures.

**Le chemin jusqu'ici** :
1. Quatre lots faits à part : les formules, les courbes, le code brut, les types IHM.
2. Une contre-vérification de chaque lot par un relecteur qui cherchait à le prendre en défaut. Elle a trouvé 16 défauts, tous corrigés, dont trois sérieux :
   - une case ƒx avec `EN`/`UNITE` qui ne convertissait pas par la portée de la vue ;
   - un clic sur une courbe Pilotable qui ne lançait plus ses actions ;
   - les objets d'un groupe collé en code brut qui changeaient de calque.
3. Une série de captures d'aperçu sous Wine. Elle a montré :
   - l'inspecteur qui marquait encore une constante globale en erreur (corrigé) ;
   - des colonnes coupées dans les types (corrigé) ;
   - une courbe peu lisible (corrigé : bandeaux, cadre des curseurs, valeurs arrondies, boutons plus grands).
4. La fusion des quatre lots, puis tous les essais sur le tout.
5. Les captures finales, avec les exe livrés. Elles ont montré :
   - l'aide à la saisie qui proposait un membre privé (Heures, après `Pompe1.` ou `$Pompe1$.`), que la case refusait ensuite (corrigé, avec un essai) ;
   - une phrase fausse dans la note des constantes (corrigée).

## 3. Les captures

Dans `captures-1.13.1.zip` (32 images), prises sous Wine avec les exe livrés, à 1920 × 1200, sur une copie du projet de démonstration. Un dossier par sujet :
- `1-types-ihm/` (9) :
  - T_Pompe dans ses onglets : Membres (avec le membre privé Heures), Méthodes, Héritage et interfaces, Emplois ;
  - T_PompeDoublee, puis T_Pompe, chacun dans son propre onglet, ouverts par double-clic dans la liste et dans l'arbre ;
  - l'interface I_Demarrable : Signatures, Implémenteurs ;
  - la liste des types élargie, ses colonnes entières.
- `2-code-brut/` (4) :
  - l'onglet Code brut d'un bouton ;
  - un autre bouton avant puis après Coller : il garde son nom et sa position ;
  - le clic droit de l'Explorateur d'objets.
- `3-courbes/` (9) :
  - une courbe en marche ;
  - la courbe figée ;
  - les curseurs : Δt = 10 s et les valeurs arrondies ;
  - le zoom : 115 ms, puis 91 µs entre les curseurs ;
  - l'export de la zone : la fenêtre, puis « 100 lignes » ;
  - la reprise, avec le trou « gel » ;
  - le clic droit en simulation.
- `4-formules-aide-saisie/` (8) :
  - les constantes globales dans l'inspecteur, sans case rouge, avec l'infobulle « Constante » ;
  - Générer sans faute, puis la valeur en simulation ;
  - l'aide à la saisie qui propose `Reglages`, puis ses constantes ;
  - l'aide dans un repère ouvert (`=$Pom`), après un repère fermé (`=$Pompe1$.`, sans le membre privé), et dans un champ Texte.
- `5-notes/` (2) : les notes de version 1.13.1 dans l'aide.

Les trois défauts vus aux captures d'aperçu sont corrigés à l'écran (captures 22, 01, 07, 25, 13 à 21).

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.13.1.exe` : l'installateur des deux applications ;
- `API-Studio-1.13.1-portable.zip` et `IHM-Studio-1.13.1-portable.zip` : chaque application, sans installation ;
- les captures (`captures-1.13.1.zip`) ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.13.1.exe`. Il met à jour la version installée (la 1.13.0 ou une plus ancienne) au même endroit et garde tes données.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Le format des projets ne change pas (33) : les projets de la 1.13.0 s'ouvrent tels quels.

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.13.1.exe` (38,0 Mo, 39 860 815 octets) :
`1A940E21AE23438EA8B947CFA14075BFD5B98F8A2F278D994706D64D2AD2D181`

Les zips portables (167 fichiers chacun ; décompresse, puis lance l'exe) :
- `API-Studio-1.13.1-portable.zip` (29,6 Mo) :
  `ED383C4018D079CD0E393B614400F6C531FC05E1D89FEE59F1E0A81259D4B7C4`
- `IHM-Studio-1.13.1-portable.zip` (29,6 Mo) :
  `09339F5281BFAB6A5E2AA529E112D12727B3A78AE79BD45C6E5032B98204156C`

## 5. Les vérifications sous Windows

**Les deux exe Windows** sont compilés sous Linux avec MinGW-w64 et lancés sous Wine.
- `--version` répond `API Studio 1.13.1` et `IHM Studio 1.13.1`.
- `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant : 891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB.

**L'installateur** est compilé par Inno Setup 6.4.1 sous Wine. Il a été lancé en silencieux, pour tous les comptes, par-dessus la 1.13.0 livrée (installée avant : `IHM Studio 1.13.0`) : « Installation process succeeded ».
- Les deux exe et `pdfium.dll` installés sont identiques à ceux qui ont été testés.
- Le manifeste compte 167 fichiers.
- Les associations `.xihm` et `.xapi` sont toujours là.

**Les captures** (§ 3) ont été prises sous Wine avec les exe livrés.

**Pas fait (Wine seulement)** : un vrai écran Windows. C'est surtout le rendu des courbes en police Segoe UI et en thème jour qui reste à voir.

## 6. Les limites

- **Une courbe étroite** : la barre de la courbe est plus grande pour se lire. Sur une courbe de moins de 600 px de large, la légende se coupe plus tôt (« M… » pour la troisième plume).
- **Le cadre des curseurs** peut cacher la lettre A d'un curseur placé dessous. Tire le curseur ou le cadre pour la voir.
- **Emplois** ne voit pas un type cité seulement dans le corps du code d'un script.
- **Le code brut** ne change pas le genre d'un objet : coller le code d'un bouton sur un afficheur est refusé.
- **Les petits défauts vus aux captures finales**, pour une prochaine version :
  - l'étiquette A ou B d'un curseur est à moitié cachée quand sa ligne touche le cadre des curseurs ;
  - des colonnes restent étroites : Genre dans Emplois, Paramètres dans la liste des méthodes de la page Types IHM (entière dans l'onglet propre du type), Valeur dans Expressions en simulation ;
  - Aide › Nouveautés montre encore les cartes de la 1.11.2 (pas de carte pour les versions récentes) ; les notes de version, elles, sont à jour.

**En attente**, comme avant :
- les raccourcis clavier avancés des éditeurs ;
- les requêtes HTTP, MQTT ou SQL depuis un script ;
- les grafcets côté IHM.
