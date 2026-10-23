# API Studio et IHM Studio 1.12.29 : l'installateur plus grand, sans clignotement, qui montre ce qu'il fait

Livrée le 23/10/2026 en fin de journée. Elle suit la 1.12.28 de l'après-midi (`docs/LIVRAISON-1.12.28.md`).

Elle répond à :

> « dans l'installateur y'a des textes qui clignotent, augmente la taille de la fenetre de l'installateur, montre plus de details sur ce qui se passe pendant l'installation/migration / liste moi des objets manquants à la bibliothèques d'objets, des améliorations de concepts à implémenter dans l'aide au design des vues graphiques »

Les applications ne changent pas, sauf leur numéro de version et leurs notes. Les deux listes demandées sont au § 7.

## 1. Ce qui change

### Les textes ne clignotent plus

Sur Windows, des textes de l'installateur clignotaient pendant les animations : chacun s'effaçait, puis se redessinait, à chaque image de l'animation. Maintenant :
- **la fenêtre se dessine d'un bloc** : Windows la compose hors de l'écran avant de la montrer (le double tampon, `WS_EX_COMPOSITED`) ;
- **aucun texte n'est posé sur une animation** : les noms des étapes sont une image à part, que la pulsation de l'étape en cours ne touche plus ; la note du bas de la colonne (« Tout est inclus… ») passe au-dessus du circuit ;
- **les textes ne changent plus de couleur en arrivant** (ce fondu les redessinait à chaque pas) : ils apparaissent et glissent ;
- **pendant la copie**, le pourcentage, le fichier en cours et le temps restant s'écrivent dans des images, seulement quand ils changent et au plus cinq fois par seconde ; une position, une taille ou une visibilité ne s'écrit que si elle change ; le diaporama passe d'une vue à l'autre en une seule image.

Si la fenêtre se dessinait mal sur un PC, l'option `/COMPOSITION=non` enlève le double tampon.

### Une fenêtre plus grande

- **912 × 580** à 100 % (la 1.12.28 faisait 750 × 465) ; 1368 × 848 à 150 %, 1824 × 1115 à 200 %. Si l'écran est trop petit, elle est ramenée à sa zone de travail.
- La colonne des étapes, les bandeaux de l'accueil et de la fin, les cartes, le diaporama et la page de la copie sont refaits à cette taille, images comprises (simples et doubles, pour 200 %).
- Pourquoi la 1.12.28 restait petite : le mode « redimensionnable » d'Inno Setup bloque l'agrandissement à 150 % de la taille d'origine. La taille est maintenant fixée par l'installateur lui-même.
- `/SILENT` garde sa petite fenêtre de progression.

### Le détail de l'installation et de la migration

Pendant la préparation et la copie, le panneau **Détails** montre chaque action au moment où elle se fait, avec son heure et son état (en cours, fait, attention, erreur), et ses détails en retrait. Il défile seul ; sa phase est écrite en haut à droite (avant la copie, la copie, après la copie).
- **Avant la copie** : la version trouvée (« La version 1.12.28 est installée : la 1.12.29 la remplace »), le dossier du programme et celui de tes données, les dossiers à reprendre, puis la sauvegarde de la version installée, avec son compteur (« 100 / 153 fichiers (102,8 Mo / 103,4 Mo) ») et son dossier.
- **La copie** : le programme (« 37 / 152 fichiers (x sur 103,7 Mo) »), ses gros fichiers un par un, la réserve de réparation, les raccourcis vérifiés (un raccourci manquant est nommé), le registre, la désinstallation enregistrée.
- **Après la copie**, ce que font les étapes PowerShell, lu pendant qu'elles tournent : tes dossiers, le catalogue livré, chaque ancienne version reprise et ce qu'on y prend, chaque projet repris (un projet renommé parce que son nom était pris est signalé), la bibliothèque et les captures (un fichier que tu avais modifié est gardé, la version livrée est rangée à côté), tes réglages et le démarrage du poste d'exploitation réécrits, le lot 8 désinstallé, la vérification fichier par fichier (151 / 151), le rapport.
- **Ce qui a été installé** dit aussi la version d'avant (ce qui en a été sauvegardé, et où) et ce qui a été repris de chaque ancienne version, avec ses projets.
- Sans assistant (`/SILENT`, `/VERYSILENT`), les mêmes lignes vont dans le journal de l'installation (`/LOG`).
- Chaque ligne a été mesurée avec la vraie police : aucune n'est coupée ; seul un chemin très long est raccourci par « … » au milieu.

### Corrigé : le poste d'exploitation après une mise à jour

Ce détail l'a montré : sur un PC où Windows lançait un poste d'exploitation au démarrage (lot 8, 1.11), la reprise le faisait démarrer **API Studio**, qui n'a pas le poste depuis la 1.12.0. Au démarrage du PC, l'éditeur s'ouvrait à la place du poste. Il démarre maintenant **IHM Studio**, avec son projet (chemin réécrit lui aussi).
- La réparation des paramètres (`repair /mode:parametres`) avait la même erreur : corrigée.
- La vérification de l'installation signale un poste qui vise encore API Studio, et la réparation le corrige. Un PC déjà mis à jour par une version d'avant se répare donc : Maintenance › Réparer, puis « Réparation automatique » (ou `repair /mode:parametres`).

Le guide d'installation (`docs/GUIDE-INSTALLATION.md`) dit le panneau Détails, le nouveau contenu de « Ce qui a été installé » et l'option `/COMPOSITION=non`.

## 2. Les essais

81 suites sur 81. Ce qui change : `hmieditor` (8288 contrôles) compte les trois notes de la 1.12.29 (413 notes, 62 versions).

Les étapes PowerShell ont été jouées pour de vrai, sous Wine, avec PowerShell 7.4 (un faux `powershell.exe` le lance) :
- **la reprise** : un PC qui a déjà servi (la 1.12.27 installée, des données anciennes, la version du lot 8 dans le registre, un poste d'exploitation lancé au démarrage) ;
- **la mise à jour** par-dessus la 1.12.28 officielle ;
- **le chemin de secours** : sans PowerShell, l'installateur le dit dans les Détails, et copie quand même.

Les quatre fichiers PowerShell changés restent écrits pour Windows PowerShell 5.1 (pas de `??`, ni de `?.`, ni de ternaire), et leur syntaxe a été vérifiée.

## 3. Les captures

Prises avec l'installateur livré, sous Wine, dans `captures-1.12.29.zip` (dossier `installateur/`) :
- `1-bienvenue.png` à `9-termine.png` : chaque page de l'assistant, à 912 × 580 ;
- `3b-reprendre-tes-anciennes-donnees.png` : la page qui propose les anciennes versions trouvées ;
- `7a-preparation.png` : la préparation et ses Détails (la version trouvée, la sauvegarde qui avance) ;
- `7-installation.png` : la copie sur un PC neuf ;
- `7b-installation-reprise.png` : la reprise d'un PC qui a déjà servi - les projets repris, un projet renommé, un bloc de bibliothèque modifié gardé, les réglages, le poste d'exploitation vers IHM Studio, le lot 8 ;
- `8-ce-qui-a-ete-installe.png` : la version d'avant, ce qui a été repris et d'où, les programmes ;
- `dpi-150-*` et `dpi-200-*` : l'accueil et la copie à 150 % et 200 % ;
- les animations (GIF) : l'accueil, une transition de page, les Détails qui se remplissent pendant la copie et la reprise (`7-details-anime.gif`), la copie, la colonne des étapes, ce qui a été installé, la fin.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.12.29.exe` : l'installateur, qui pose les deux applications ;
- `API-Studio-1.12.29-portable.zip` et `IHM-Studio-1.12.29-portable.zip` : chaque application, sans installation ;
- les captures (`captures-1.12.29.zip`) ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.12.29.exe`. Il met à jour la version installée (la 1.12.28 ou une plus ancienne) au même endroit, et garde tes données. Les projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.12.29.exe` (33,1 Mo, 34 672 412 octets, 76 Ko de plus que la 1.12.28) :
`B2EBDC9755E78894AA7AD1ECF34650C6603B998BE16B0213DF1405A67404390B`

Les zips portables (151 fichiers chacun ; décompresse, puis lance l'exe) :
- `API-Studio-1.12.29-portable.zip` (24,3 Mo) :
  `CA6F5876E549A72B7E5EABF576379EFEBAEB6A1E9BCAA02E2B815A5AEB9B30DE`
- `IHM-Studio-1.12.29-portable.zip` (24,3 Mo) :
  `BC0CF3059036D916E52417B6C4066A80EB16BC3C8894E601BBE80B244F51D140`

## 5. Les vérifications sous Windows

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine : `--version` répond `API Studio 1.12.29` et `IHM Studio 1.12.29` ; `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB).

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, sans avertissement :
- l'assistant complet sur un Wine neuf, à 100 %, 150 % et 200 % : chaque page s'affiche, les étapes PowerShell rendent le code 0 ; Précédent et Annuler marchent ;
- la reprise d'un PC qui a déjà servi, et la mise à jour par-dessus la 1.12.28 officielle : réussies ; après la reprise, le démarrage du poste d'exploitation vise `IHM-Studio.exe` et son projet, et la vérification n'en dit rien ; avec une valeur d'avant la 1.12.29, la vérification la signale et la réparation la corrige ;
- en silencieux, pour tous les comptes, par-dessus la 1.12.28 (installée avant : `IHM Studio 1.12.28`) : « Installation process succeeded » ; les deux exe installés sont identiques à ceux qui ont été testés et répondent `1.12.29` ; les Détails sont dans le journal ;
- `/SILENT` par-dessus, et la désinstallation silencieuse (le programme enlevé, les données gardées) : réussis.

**Pas fait (Wine seulement) :**
- l'installateur sur un vrai Windows : le double tampon (`WS_EX_COMPOSITED`) sur un vrai écran, le rendu des polices ; s'il se dessinait mal, `/COMPOSITION=non` l'enlève ;
- Windows PowerShell 5.1 lui-même : les essais ont tourné avec PowerShell 7.4 ;
- les raccourcis : sous Wine, 4 sur 9 ne se créent pas (les sous-dossiers du menu Démarrer), et les Détails le signalent ; Windows n'a pas ce défaut ;
- l'installation pour moi seul (`/CURRENTUSER` échoue sous Wine), et l'affichage à 125 %.

## 6. La suite

- Sur un vrai Windows : dérouler l'installateur (rien ne clignote, la taille, les Détails) ; mettre à jour un PC qui a un poste d'exploitation lancé au démarrage, puis redémarrer le PC.
- En attente : les fonctions IHM génériques (TEMPLATE) partout ; les raccourcis clavier avancés des éditeurs.

## 7. Les objets manquants et l'aide au dessin des vues

Vérifié contre la bibliothèque et l'aide de la 1.12.29 : ce qui existe déjà n'est pas proposé.

### Les objets qui manquent à la bibliothèque

IHM Studio a 103 objets en 11 familles, plus les symboles du projet. Ce qui manque, par famille (★ : les plus utiles pour tes projets de gaz, d'extraction et de CVC).

**Synoptique : gaz, extraction, CVC**
- ★ **Électrovanne** 2 ou 3 voies : bobine, ouverte / fermée / défaut, commande et recopie (la Vanne est manuelle, motorisée, pneumatique ou réglante, jamais à bobine).
- ★ **Détendeur / régulateur de pression** : pression amont et aval, consigne.
- ★ **Détecteur de gaz** : valeur (ppm ou % LIE), seuils 1 et 2, défaut, maintenance, étalonnage.
- ★ **Gaine de ventilation**, ronde ou rectangulaire : coudes, réductions, piquages, sens et débit animés (la Tuyauterie est un tube rond ; les registres ne dessinent qu'un bout de gaine).
- **Soupape de sécurité**, disque de rupture.
- **Caisson de traitement d'air** : filtres et leur encrassement, batteries chaude et froide, récupérateur.
- **Laveur de gaz** (scrubber), dépoussiéreur, cyclone ; **groupe froid**, tour de refroidissement.
- **Capteurs dédiés** : débitmètre, pressostat, thermostat, niveau TOR ou analogique (l'Instrument ISA reste générique).
- **Balance** : poids brut, net, tare (la Bouteille de gaz montre une pression, pas un poids).

**Électrique**
- ★ **Variateur de vitesse** : fréquence, courant, sens, prêt, défaut (aujourd'hui un variateur n'est qu'un équipement Modbus, sans symbole).
- **Onduleur** (sur batterie, autonomie), groupe électrogène, batterie ; **compteur d'énergie** (puissance, kWh, cos φ).
- **Jeu de barres**, fusible, câble coloré selon la tension ou l'état.

**Commandes**
- **Plus / moins** : pas, bornes, répétition au maintien.

**Affichage**
- ★ **Tuile d'indicateur** : grande valeur, unité, variation, seuil coloré, mini-courbe, en un objet (aujourd'hui : un afficheur, une flèche de tendance et une courbe à assembler).
- ★ **Mini-courbe** (sparkline), à côté d'un afficheur ou dans une case de tableau.
- **Chaîne d'autorisations** : les conditions d'un démarrage, une par ligne, vertes ou rouges.
- **Séquence en cours** : les étapes d'un cycle, l'étape active, le temps passé, ce qui la bloque.

**Graphiques**
- **Carte thermique** : un plan coloré par la mesure (le Plan à zones l'est par les alarmes), ou zones × temps.
- **Gantt** (cycles, planning de maintenance) ; **Pareto** de n'importe quelles causes (les Statistiques d'alarmes n'en font qu'un des alarmes).
- **Carte de contrôle** (moyenne, étendue, limites), boîte à moustaches ; **profil de consigne** avec sa bande de tolérance.

**Alarmes et exploitation**
- **Main courante** : notes de quart horodatées et signées.
- **Liste de contrôle** : une procédure à cocher pas à pas (changement de bouteille), signée à la fin ; **saisie des causes d'arrêt**.

**Navigation, communication, médias**
- ★ **Répéteur de symbole** : une grille d'instances depuis un tableau ou une liste (`Armoires[*]`) ; le nombre de cartes suit le tableau.
- **Arborescence** (site › zone › équipement) qui navigue ; **pagination** des longues listes de cartes.
- **Topologie du réseau**, dessinée depuis Configuration › Équipements, avec l'état de chaque liaison.
- **Visionneuse de documents** (PDF, schéma, procédure) ; **flux de caméra IP** (aujourd'hui, une caméra n'est que surveillée par ping).

Déjà là, donc pas dans la liste : le bouton « maintenir pour valider » (Bouton › Confirmation › maintien, une barre se remplit), couper la sirène (Faire taire les alarmes), la vanne réglante et son positionneur, le registre papillon, le clavier virtuel des champs de saisie.

### L'aide au dessin des vues : ce qui irait plus loin

Déjà là : grille, magnétisme (bords, espacement égal), guides, aligner / distribuer, calques, groupes, styles nommés avec aperçu, modèles de vues, écrans modèles avec en-tête et pied (estompés dans la vue), Dupliquer avec repères, une variable glissée qui pose son objet, la popup générée d'un type, et les contrôles de qualité de Générer. Ce qui irait plus loin :

1. ★ **La qualité en direct** : les contrôles de Générer (texte qui déborde, contraste, cible trop petite, objet hors de la vue) marqués sur la vue pendant qu'on dessine, chacun avec sa correction en un clic. S'y ajoutent : un objet presque aligné (à 1 ou 2 px), le même équipement dessiné autrement d'une vue à l'autre, une couleur hors charte.
2. ★ **Une charte graphique du projet** (esprit ISA-101, IHM haute performance) : une palette nommée par rôle (fond, structure, valeurs, états, alarmes ; aujourd'hui la palette ne propose que les dix couleurs déjà employées), les couleurs d'alarme réservées aux alarmes, des rôles de texte (titre, libellé, valeur, unité). Un style par défaut par genre d'objet : un afficheur posé prend tout de suite le bon. L'éditeur propose la charte d'abord, et Générer signale les écarts.
3. ★ **Les raccords des synoptiques** : des points de raccordement sur pompes, vannes, cuves et gaines. La tuyauterie s'y accroche, se route à angle droit et suit l'équipement déplacé. Ensuite, la couleur se propage dans le réseau : le tube est plein quand la pompe en amont tourne et que les vannes sont ouvertes.
4. ★ **Un équipement complet en un geste** : aujourd'hui, une structure glissée pose un bouton et sa popup générée, et l'Équipement d'un symbole lie ses membres par leur nom. Les réunir : la structure (`Pompe_01`) lâchée sur une pompe règle son Équipement, son libellé et le clic qui ouvre sa popup ; un tableau (`Pompes[*]`) pose toute la rangée, alignée, une pompe par élément.
5. **Une mise en page qui s'adapte** : des rangées et des colonnes qui rangent leurs objets, des ancrages aux bords, une vue pensée pour 1280×800 qui tient en 1920×1080, et l'aperçu dans chaque résolution du projet.
6. **Les états sans simulation** : dans l'éditeur, faire défiler les états d'un objet ou de toute la vue (normal, alarme, défaut, communication perdue, valeur haute ou basse) pour voir chaque apparence.
7. **Des cotes comme dans un logiciel de dessin** : Alt au survol montre les distances aux voisins et aux bords (aujourd'hui, les cotes ne paraissent qu'avec l'espacement égal). Règles graduées, guides tirés des règles et gardés dans la vue, grille de colonnes.
8. **Des calques de lecture** : les objets colorés selon ce qu'ils lisent (lié, non lié, en faute), les liens de navigation, l'ordre de tabulation, les zones tactiles.
9. **La carte des vues** : le graphe de la navigation, qui ouvre quoi (Générer dit déjà les vues jamais atteintes ; la carte les montre) ; un glisser d'une vue à l'autre y crée le bouton.
10. **Les langues en vis-à-vis** : la vue dans chaque langue, côte à côte, les débordements marqués. Une pseudo-langue (textes allongés de 40 %) pour tester avant la traduction.
11. **La comparaison visuelle** : deux versions d'une vue superposées, ce qui a bougé, été ajouté, supprimé ou changé, surligné (aujourd'hui : vignettes côte à côte).
12. **Une vue depuis des variables** : 20 variables choisies dans Variables IHM donnent une page de réglages ou un tableau de bord rangé (libellés, valeurs, unités, colonnes alignées, charte appliquée) ; aujourd'hui, ce choix n'ouvre qu'une visualisation graphique.
13. **La lisibilité à distance** : la taille réelle d'un texte sur le pupitre (en mm, d'après la taille de l'écran) et la distance de lecture, avec un avertissement sous un seuil.
