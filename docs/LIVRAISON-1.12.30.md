# API Studio et IHM Studio 1.12.30 : la visionneuse de documents, le variateur, les cotes, les langues côte à côte, le zoom partout, les archives et la page web

Livrée le 24/10/2026, dans la nuit. Elle suit la 1.12.29 (`docs/LIVRAISON-1.12.29.md`).

Elle répond à :

> « dans l'installateur la page bibliothèques la scrollbar marche pas et ca clignote
> ajoute visionneur de document, avec systeme de zoom, navigation des pages etc
> a faire : cotes, langes en vis à vis, variateur de vitesse,
> il n'y a pas bcp de menus avec des zooms, corrige ca, pour utiliser le zoom dans le bandeau bas
> pouvoir exporter les projets IHM dans un seul fichier qui stockerait tout un peu comme un fichier 'archive', avec l'extension 'xihm', ca prend les resources tout, pareil avec api 'xapi', pouvoir sortir les applis en ".html" automatiquement aussi, et les importer en html aussi
> envoie des captures »

## 1. Ce qui change

### L'installateur : la page Bibliothèque défile, sans clignoter

Sur Windows, la barre de défilement de la liste ne suivait plus le curseur, et la liste clignotait pendant que la colonne des étapes s'animait. La cause : la 1.12.29 dessinait toute la fenêtre d'un bloc (le double tampon de Windows), ce que les listes de Windows ne supportent pas.

- Seuls l'en-tête, l'accueil et la page de la copie se dessinent encore d'un bloc : ce sont eux qui clignotaient avant la 1.12.29, et ils n'ont pas de liste.
- Les pages qui ont une liste, un texte ou un champ ne glissent plus en arrivant ; « Ce qui a été installé » s'affiche d'un coup.
- Le curseur, les flèches, la gouttière, la molette et le clavier font défiler les listes et les textes de toutes les pages.
- `/COMPOSITION=non` enlève aussi le double tampon qui reste.

### La visionneuse de documents

Un nouvel objet de la famille **Médias**, après la vidéo : il lit sur le poste une notice, une procédure ou un schéma.

- **Les documents** : PDF, images (PNG, JPEG, BMP, GIF, SVG) et texte (TXT), pris dans les ressources du projet ou sur le disque (un chemin absolu ou relatif au projet). Une expression STRING change de document en marche.
- **La barre d'outils** : première, précédente, suivante et dernière page ; le champ « 3 / 12 », où tu tapes un numéro ; − et + ; un menu de zoom (ajuster à la largeur, page entière, 50 % à 400 %) ; tourner la page ; les vignettes ; chercher dans le texte.
- **Les gestes** : la molette fait défiler, Ctrl + molette zoome autour du curseur, glisser déplace la page, un double-clic passe de l'ajustement à 100 %. Au clavier : Page précédente, Page suivante, Début, Fin, +, −, Ctrl+0, Ctrl+F.
- **Reliée à une variable**, la page suit l'étape d'une procédure, et la page que choisit l'opérateur y est écrite. Décoche *Navigation autorisée* pour qu'il ne la quitte pas.
- **Les PDF** se lisent avec PDFium, le moteur PDF de Chromium (licence BSD-3-Clause), livré à côté des applications (`pdfium.dll`, 7 Mo) et chargé seulement quand une visionneuse ouvre un PDF. Sans lui, la visionneuse dit « Lecteur PDF absent », et les images et le texte se lisent toujours.
- Seules les pages visibles se dessinent : un document de 200 pages reste fluide.
- Une vue d'exemple montre la notice d'un ventilateur qui suit l'étape de la procédure.

### Le variateur de vitesse

Un nouveau symbole de la famille **Synoptique**, à côté du moteur : le convertisseur normalisé (le cadre, la diagonale, le réseau ~ et la sortie en deux ondes).

- Sa couleur dit s'il tourne, s'il est prêt ou en défaut (elle clignote alors).
- Sa pastille dit MARCHE AV, MARCHE AR, PRÊT, ARRÊT ou DÉFAUT, et LOC ou DIST si tu donnes le mode local.
- À côté : la fréquence en gros, sa consigne, le courant et la vitesse, chacun avec son format et son unité.
- En marche, les ondes et les chevrons avancent dans le sens de rotation.
- Il se tourne et se retourne comme les autres symboles.
- Donne-lui la structure du variateur dans Équipement : Marche, Defaut, Pret, Sens, Frequence, Consigne, Courant, Vitesse et Local se lient par leur nom. Son défaut fait l'alarme par défaut *Defaut*, et Générer signale une valeur qui n'est ni un nombre ni une variable.

### Le zoom du bandeau bas, sur toutes les pages

La puce du zoom, en bas à droite, ne servait qu'aux éditeurs de code, de grafcet et de vues. Elle zoome maintenant **chaque page** : ses tableaux et ses listes (variables, alarmes, recettes, tâches, Générer…), ses arbres, ses fiches, l'inspecteur de l'éditeur de vues, l'aide, les pages de la simulation. Le texte, la hauteur des lignes, les marges et les icônes grandissent ensemble, et les barres de défilement suivent.

- − et + de part et d'autre du pourcentage changent d'un cran ; un clic sur le nombre ouvre son menu (50 % à 200 %, et Revenir à 100 %).
- Le même geste partout : Ctrl + molette, Ctrl + plus, Ctrl + moins, Ctrl + 0.
- Chaque page garde son zoom tant qu'elle est ouverte ; un code, un grafcet, une vue gardent le leur, et la puce suit celui que tu as cliqué en dernier.
- Sur l'arbre du projet, Ctrl + molette le zoome seul.

### Les règles, les guides et les cotes de l'éditeur de vues

- **Les règles**, en haut et à gauche (Ctrl+R), graduées en pixels de la vue : elles suivent le zoom, le défilement, la souris et la sélection.
- **Les guides** : tire-en un d'une règle. Il se pose au pas de la grille, il est gardé avec la vue (Ctrl+Z le retire), et le magnétisme s'y accroche ; ramène-le sur la règle pour l'enlever. Ctrl+; cache les guides, Ctrl+Alt+; les verrouille. Générer signale un guide hors de la vue.
- **La grille de colonnes** (nombre, gouttière, marges, dans Propriétés de la vue) aimante aussi les objets.
- **Les cotes** : un objet choisi, tiens Alt et survole. La distance à l'objet survolé, ou à ses voisins et aux bords de la vue, s'écrit sur la vue (et un moment après un déplacement aux flèches).
- Rien de cela ne se voit en marche.
- Une barre d'outils trop étroite range ses derniers boutons dans le menu » au bout, au lieu de les couper.

### Les langues côte à côte

- La vue dans chaque langue du projet, deux à quatre à la fois, à la même échelle : le bouton *Langues côte à côte* de l'éditeur, ou *Côte à côte* dans Configuration › Langues.
- Un texte qui sort de sa boîte est entouré de rouge, et l'en-tête de la langue dit combien. Un clic sur la marque choisit l'objet dans l'éditeur, ou mène à sa traduction.
- La pseudo-langue **Pseudo (+40 %)** allonge chaque texte de 40 % et l'accentue : tu vois, avant toute traduction, les boîtes trop justes. Elle n'est jamais enregistrée.
- Générer signale maintenant les textes qui débordent dans chaque langue.

### Les archives .xihm et .xapi : tout le projet dans un seul fichier

**Projet › Exporter en archive .xihm…** (IHM Studio) ou **.xapi…** (API Studio) met tout ton projet dans un seul fichier :
- pour l'IHM : les vues, les ressources de tout genre, les fichiers externes liés (même rangés hors du projet : ils reviennent dans `externes/`), les équipements, la simulation, l'historique des objets, les versions ;
- pour l'automate : le programme tel quel (ladders et grafcets compris), la disposition des grafcets et les macros (une macro que ta bibliothèque n'a pas y est ajoutée ; une macro déjà là est gardée).

L'archive est un zip avec un manifeste : pour chaque fichier, sa taille, son empreinte SHA-256 et sa date. Le cache de construction et les pages déjà écrites n'y vont pas : un projet rouvert se régénère.

**Pour la rouvrir** : Projet › Ouvrir…, le fichier lâché sur la fenêtre, ou un **double-clic** dans l'Explorateur (l'installateur associe les `.xihm` à IHM Studio et les `.xapi` à API Studio ; la désinstallation retire ces associations). Une fenêtre demande où la déballer (par défaut, ton dossier des projets), chaque fichier est vérifié, puis le projet s'ouvre et son contrôle dit ce qu'il a retrouvé.

Une archive abîmée, coupée, d'une version plus récente ou de l'autre application est refusée, et la raison est dite. D'une archive abîmée, tu peux déballer ce qui est intact.

IHM › Exporter / Importer garde ses boutons : **Exporter** écrit maintenant l'archive `.xihm` du projet entier ; **Importer** reprend l'IHM d'une archive `.xihm` (ou de l'ancienne `.zip`) dans le projet ouvert.

### La page web (.html), et retour

**Projet › Exporter en page web (.html)…** écrit ton application dans **une seule page**, lisible dans n'importe quel navigateur, sans Internet (aucun lien extérieur) :
- **IHM Studio** : chaque vue et chaque popup, dessinée par le même dessin que l'éditeur, au repos (un tiret à la place des valeurs) ; ses boutons de navigation et ses popups se cliquent. Puis les alarmes, les variables, les scripts et la fiche du projet, avec une recherche dans le menu.
- **API Studio** : le programme entier - la fiche, la configuration, les tâches et l'ordre d'exécution, chaque section et son code, les unités, les DFB, les types et les variables.

**Le projet complet est dans la page**, avec un bouton *Télécharger le projet* : **Projet › Importer depuis une page web…** (ou Ouvrir… sur le `.html`) le reconstruit à l'identique.

Coche **Mettre à jour la page web à chaque enregistrement** : elle se réécrit à chaque Ctrl+S, sur un autre fil, sans ralentir l'enregistrement. Le choix est gardé avec le projet.

Contrôlé sur le projet de démonstration : rouvert depuis son archive, puis depuis sa page web, le dossier est identique au fichier près (hors le cache et `exports/`). La page de la démo pèse 6,1 Mo (dont son archive de 1,9 Mo).

## 2. Les essais

**84 suites sur 84** (81 à la 1.12.29). Les trois nouvelles :
- `hmidocviewer` (102 contrôles) : la visionneuse - PDF, images, texte, les pages, le zoom, la recherche, la page reliée à une variable, et sans PDFium ;
- `echanges` (105 vérifications) : les allers-retours `.xihm` et `.xapi` fichier par fichier et compte par compte ; les archives abîmées, coupées, plus récentes ou de l'autre application ; l'extension inconnue ; la page sans archive ; l'échappement des textes ; l'archive reprise depuis la page ;
- `zoom` : le contrat du zoom des pages (les crans, les bornes, ce qui grandit).

`hmieditor` (8390 contrôles) compte les nouveautés : 421 notes, 63 versions, 248 sujets d'aide (Démarrer 28, L'IHM 224, L'automate 17). Le guide de l'aide et son C++ disent la même chose ; le projet Visual Studio (`XpgAnalyzer.vcxproj`) a tous les nouveaux fichiers.

Les trois lots ont été faits à part, puis fusionnés ; les essais ont tourné sur le tout.

## 3. Les captures

Dans `captures-1.12.30.zip` (81 images, prises sous Wine avec les exe livrés), un dossier par sujet :
- `1-installateur/` (15) : l'accueil ; la page Bibliothèque à chaque geste (en haut, tes blocs ajoutés, le curseur tiré au milieu, les flèches et la gouttière, la molette, la touche Fin, un bloc retiré) et son défilement animé ; la copie ; « Ce qui a été installé » (en haut, le curseur tiré, Ctrl+Fin) et son défilement animé ; la fin ;
- `2-visionneuse-de-documents/` (11) : dans l'éditeur et dans l'inspecteur ; en marche (la notice d'un ventilateur, son schéma, sa procédure), en fenêtre ; la page qui suit l'étape ; les vignettes ; le zoom et son menu ; la recherche ; l'aide F1 ; sans PDFium (« Lecteur PDF absent ») ;
- `3-variateur-de-vitesse/` (4) : dans l'éditeur ; en marche (marche avant, marche arrière en local, prêt, défaut, ses formes et son miroir), sur deux moments ; l'aide F1 ;
- `4-zoom/` (9) : à 150 %, les variables, les alarmes et les natives d'IHM Studio, puis les variables, la vue d'ensemble de la simulation et les tâches d'API Studio ; l'inspecteur de l'éditeur ; le menu de la puce ; l'explorateur ;
- `5-regles-guides-cotes/` (9) : les cotes avec Alt (vers un objet, dans son cadre, vers ses voisins et les bords, après une flèche) ; un guide tiré de la règle ; les règles et les guides ; la grille de colonnes ; le menu de la règle ; la barre d'outils trop étroite et son menu » ;
- `6-langues-cote-a-cote/` (5) : trois langues côte à côte et leurs débordements marqués ; au zoom ; le menu d'une marque ; la pseudo-langue ; Générer et les débordements de chaque langue ;
- `7-archives-et-page-web/` (28) : les menus Projet des deux applications ; l'export en `.xihm` et en `.xapi` ; l'ouverture d'une archive (où la déballer, puis le projet ouvert) ; la page web d'IHM Studio dans un navigateur (la vue de démarrage, une navigation, une popup, les alarmes, la fiche) et celle d'API Studio (la fiche, une section et son code, l'ordre d'exécution) ; la page réécrite à l'enregistrement ; le retour depuis une page ; une archive abîmée ; une page sans archive ; F1 dans la fenêtre d'export.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.12.30.exe` : l'installateur, qui pose les deux applications ;
- `API-Studio-1.12.30-portable.zip` et `IHM-Studio-1.12.30-portable.zip` : chaque application, sans installation ;
- les captures (`captures-1.12.30.zip`) ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.12.30.exe`. Il met à jour la version installée (la 1.12.29 ou une plus ancienne) au même endroit, et garde tes données. Les projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.12.30.exe` (36,4 Mo, 38 157 474 octets, 3,3 Mo de plus que la 1.12.29 : le lecteur PDF) :
`937906081B8E11CB1D26EDAB9F5017900BFBFD668D6F9B85FA2E23B47DC92CD4`

Les zips portables (167 fichiers chacun ; décompresse, puis lance l'exe) :
- `API-Studio-1.12.30-portable.zip` (28,4 Mo) :
  `2BD51295A7AE2753AF8768FB44C1B6994E1F2D22A95290B1A17EDF9D86C4B578`
- `IHM-Studio-1.12.30-portable.zip` (28,4 Mo) :
  `BCECF45DD38F6B053748C63BC110A79D8306E8F33A38BDDF6E7039B9535894EA`

## 5. Les vérifications sous Windows

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine : `--version` répond `API Studio 1.12.30` et `IHM Studio 1.12.30` ; `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB).

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine :
- en silencieux, pour tous les comptes, par-dessus la 1.12.29 officielle (installée avant : `IHM Studio 1.12.29`) : « Installation process succeeded ». Les deux exe et `pdfium.dll` installés sont identiques à ceux qui ont été testés ; les licences de PDFium sont dans `licences\pdfium\`, et le manifeste les compte (167 fichiers) ;
- les associations, dans `HKLM\Software\Classes` : `.xihm` mène à `APIIHMStudio.xihm`, ouvert par `"C:\Program Files\XPGAnalyser\IHM-Studio.exe" "%1"` avec l'icône d'IHM Studio ; `.xapi` mène à `APIIHMStudio.xapi`, ouvert par `API-Studio.exe` avec la sienne ;
- la page Bibliothèque et « Ce qui a été installé », dans l'assistant : le curseur de la barre tiré, les flèches et la gouttière, la molette, les touches Début et Fin font défiler la liste, qui ne clignote plus (les captures, et deux animations).

**Les échanges**, dans les deux applications sous Wine, sur des copies : exporter en `.xihm` et en `.xapi`, rouvrir chaque archive, écrire la page web puis la réécrire à l'enregistrement, réimporter depuis la page, refuser une archive abîmée et une page sans archive. Les pages écrites, ouvertes dans Chromium : aucune requête réseau, aucune erreur ; aller à une vue, Précédent, ouvrir et fermer une popup, télécharger l'archive.

**Pas fait (Wine seulement)** :
- la page Bibliothèque de l'installateur sur un vrai écran Windows (le glisser-déposer, Ajouter un dossier) ;
- le double-clic sur un `.xihm` ou un `.xapi` dans l'Explorateur, et l'installation pour moi seul (`/CURRENTUSER` échoue sous Wine) ;
- le vrai glisser-déposer d'une archive sur la fenêtre, et son ouverture dans une instance déjà lancée (ils passent par le même chemin que Projet › Ouvrir…, qui, lui, a été joué) ;
- Windows PowerShell 5.1 lui-même : les étapes de l'installateur ont tourné avec PowerShell 7.4.

## 6. Les limites, et la suite

**Les limites connues :**
- **Le zoom** : les barres d'outils et de filtres, le panneau du bas, l'explorateur d'objets, les calques et la bibliothèque ne zooment pas encore. Le zoom d'une page n'est pas gardé d'une séance à l'autre. Sur un clavier AZERTY, Ctrl + − de la rangée des chiffres arrive comme « 6 » : prends le − du pavé numérique, la puce ou Ctrl + molette.
- **La visionneuse** : une page se dessine en 16 millions de pixels au plus (nette jusqu'à 420 % environ pour une page A4). Un PDF protégé par un mot de passe affiche un message. Pas d'impression, ni de liens, ni de signets, ni de sélection du texte.
- **Les archives** : le cache de construction n'y est pas, un projet rouvert se régénère.
- **La page web** : elle montre les vues au repos, sans valeurs ni animations ; une courbe y dit « aucune mesure archivée ».
- **Trouvé en passant, pas corrigé** (d'avant cette version) : dans l'aperçu relié de l'éditeur, une propriété de rôle Formule tapée dans l'inspecteur n'est pas évaluée - les fins de course et le déclenchement des registres, les positions A et B de la vanne 3 voies.

**En attente** : les fonctions IHM génériques (TEMPLATE) partout ; les raccourcis clavier avancés des éditeurs ; le zoom des barres d'outils et du panneau du bas.
