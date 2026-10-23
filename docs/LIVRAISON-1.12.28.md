# API Studio et IHM Studio 1.12.28 : la fenêtre du blocage dessinée par l'application, la veille, le didacticiel dans l'aide F1, l'installateur animé

Livrée le 23/10/2026 dans la journée. Elle suit la 1.12.27 de la nuit (`docs/LIVRAISON-1.12.27.md`).

Elle répond à :

> « met pas un truc windows pour les blocages/crash fait le meme principe que l'explorateur de fichiers du projet / et attention à ne pas faire un blocage quand le pc est en veille, corrige et envoi des captures / dans le F1 le deballage/remballage ne fonctionne pas partout par exemple IHM en haut / en fait j'aimerais combiner le didacticiel de l'ihm avec l'aide F1 / met ça à jour en combinant / améliore encore l'installateur, y'a rien de dynamique là »

## 1. Ce qui change

### La fenêtre du blocage et du plantage, dessinée par l'application

Comme l'explorateur de fichiers du projet : plus de boîte grise de Windows aux boutons coupés. La fenêtre est celle d'IHM Studio, avec son thème (clair ou sombre, celui de tes réglages) et son logo.

- **Pendant un blocage** (l'application ne répond plus depuis 8 s) : une fenêtre à part s'ouvre devant, « IHM Studio ne répond plus ». Elle compte le temps qui passe, dit ce qui s'est passé et ce que l'application faisait (ses opérations en cours, et depuis combien de temps), et montre le rapport. **Attendre** la ferme et laisse l'application continuer ; **Fermer IHM Studio** l'arrête. Quand l'application repart, la fenêtre le dit (« IHM Studio est reparti », après combien de secondes d'arrêt) et se ferme ; si l'application se ferme, aussi.
- **Après un plantage** : « IHM Studio s'est arrêté », ce qui s'est passé, le rapport ; **Relancer IHM Studio** ou **Fermer**.
- Dans les deux : **Ouvrir le dossier** (le dossier des rapports), **Copier le rapport**, **Signaler le problème**.
- **Au lancement suivant**, un rapport que la fenêtre n'a pas pu montrer s'ouvre dans l'application, dès sa première image, avec les mêmes boutons.
- La fenêtre est ouverte **avant** que le rapport complet (l'image mémoire du processus) s'écrive : elle paraît tout de suite, même quand cette écriture dure.
- La boîte de Windows ne sert plus que de repli, si la fenêtre ne peut pas s'ouvrir du tout.

### La veille n'est plus un blocage

Le chien de garde comptait l'heure du mur : au réveil du PC, les minutes de veille devenaient « 8 s sans battement », et un faux rapport de blocage s'ouvrait. Il compte maintenant :
- le **temps où le PC est éveillé** (la veille et la veille prolongée n'y comptent pas) ;
- il se tait quand **le PC s'endort** ou que **l'écran s'éteint** (les avis de Windows), et quand il n'a pas tourné lui-même (l'application entière figée par le système) ;
- au réveil, il laisse **30 s** à l'application pour repartir avant de compter.

### Le didacticiel dans l'aide F1

Les six parcours du didacticiel de l'IHM vivaient à part, sur une page du volet Aide IHM. Ils sont maintenant **des pages de l'aide** : **Démarrer › Le didacticiel**.
- **La page d'ensemble** (Le didacticiel) montre tous les parcours en cartes : leur sorte (visite ou interactif), leurs étapes, leur durée, leur progression (fait, en cours 4 / 9) et leur bouton : **Commencer**, **Reprendre**, **Refaire**.
- **Une page par parcours** dit ce qu'il fait, étape par étape, avec sa carte en tête.
- Le parcours se joue **sur le projet ouvert** : l'aide se ferme et il commence là où tu travailles. Sans projet ouvert, la carte le dit.
- **Une page qui parle du même sujet le propose** en tête : « ▶ Parcours guidé : Créer un symbole de projet » sur la page des symboles, « Coller depuis Excel » sur celle d'Excel, « Historique et versions » sur celles de l'historique et des versions...
- **Aide › Didacticiel de l'IHM** mène à cette page. Sur l'accueil, **Découvrir l'IHM** lance la visite, et dit sa durée (3 min, comme sa carte).
- **La visite « Découvrir l'IHM » est à jour** : seize étapes, sans l'automate ni le dossier API, avec la bibliothèque rangée par familles, l'aperçu des styles, Pourquoi ? en simulation, plusieurs fenêtres, Aller à, et l'aide où vit maintenant le didacticiel.

### L'arbre de l'aide se replie partout

Le chapitre de la page ouverte (par exemple **L'IHM**, en haut) et ses sous-chapitres étaient dépliés d'office : un clic ne les repliait plus. Un clic les replie maintenant, à tous les niveaux ; aller sur une autre page rouvre les siens.

### L'installateur, vivant

Tout bouge, à chaque page, sans rien changer à ce qu'il fait :
- **L'accueil** : le réseau du bandeau s'allume et scintille, des impulsions courent sur ses liens ; les deux logos arrivent chacun de son côté et se relient, l'écran d'IHM Studio s'allume ; un reflet passe toutes les 5 s. Le titre, le texte et les deux cartes montent en fondu.
- **Chaque page** glisse en place, depuis la droite après Suivant, depuis la gauche après Précédent ; son titre arrive en fondu, l'icône de l'étape apparaît, le trait de l'en-tête se dessine.
- **La colonne des étapes** : au lancement, les étapes apparaissent une à une ; une pastille glisse d'une étape à l'autre et le lien entre elles se remplit ; la coche d'une étape faite se dessine ; l'étape en cours pulse ; pendant la copie, un anneau autour d'« Installation » se remplit avec la progression ; des impulsions lumineuses courent sur le circuit, en bas de la colonne.
- **La préparation** : l'animation de la copie et une phrase, au lieu d'une page vide.
- **La copie** : une barre qui suit la progression en douceur, avec un reflet et le pourcentage qui défile ; le fichier en cours et le temps restant ; un diaporama de six vues (API Studio, IHM Studio, les versions, tes données, l'aide, la maintenance) qui se fondent l'une dans l'autre.
- **Ce qui a été installé** s'écrit ligne par ligne.
- **La fin** : le disque apparaît, la coche se dessine, des confettis jaillissent, des étincelles scintillent.

Corrigé en passant :
- l'installateur pouvait se figer un instant à chaque changement de page : chaque image sortait du paquet en décompressant tout le programme avec elle. Ses images ont maintenant leur propre bloc, sorti en une fois au lancement (0,2 s) ;
- la question pour quitter disait « [name] » au lieu du nom ; la page de préparation vouvoyait ; « de API » devient « d'API » ; des textes de deux ou trois lignes étaient coupés à 150 % et 200 %.

Il pose toujours les deux applications, met à jour celle qui est installée sans toucher à tes données, et s'installe en silencieux (/SILENT, /VERYSILENT) comme avant : rien ne s'anime alors. Il pèse 119 Ko de plus.

## 2. Les essais

81 suites sur 81. Ce qui change :
- `hmieditor` (8284 contrôles) : le didacticiel dans l'aide - le catalogue des parcours, la page d'ensemble et une page par parcours dans Démarrer › Le didacticiel, autant d'étapes dans le guide qu'au parcours, le sous-chapitre en bas de Démarrer, l'arbre et le fil d'Ariane d'une page de parcours, API Studio sans eux ; le repli de l'arbre à tous les niveaux (L'IHM, la bibliothèque, une famille) ; les comptes de l'aide (240 sujets, Démarrer 27) et des notes (410, 61 versions).
- `crashtrail` (62 contrôles) : le chien de garde sur le temps éveillé - un processus figé 3 s n'est pas un blocage, les avis de veille et d'écran éteint le font taire, 30 s de répit au réveil.
- `crashreport` (40 contrôles, nouvelle suite) : la fenêtre de rapport - ce qu'elle dit d'un blocage, d'un plantage et d'un rapport au lancement suivant, le compteur, la reprise, ses boutons, qui ne sortent plus du cadre dans une fenêtre étroite.
- L'aide : le texte source et le programme disent la même chose, sujet par sujet (240 sujets, `tools/guide-ihm/depuis_cpp.py --verifier`).

## 3. Les captures

Prises avec l'exe Windows livré, lancé sous Wine. La fenêtre de rapport s'est capturée elle-même, pendant un vrai blocage puis après un vrai plantage (les essais cachés de l'exe, `--essai-blocage` et `--essai-plantage`) :
- `1228_01_blocage_la_fenetre.png` : « IHM Studio ne répond plus », depuis 9 s - ce qui s'est passé, ce que l'application faisait, le rapport ; Attendre, Fermer IHM Studio ;
- `1228_02_blocage_le_compteur.png` : le compteur, quelques secondes plus tard ;
- `1228_03_blocage_reparti.png` : l'application est repartie ; la fenêtre le dit et se ferme (18 s d'arrêt : sous Wine, l'image mémoire du processus, écrite pendant le blocage, l'allonge ; l'essai en dure 12) ;
- `1228_04_plantage_la_fenetre.png` : « IHM Studio s'est arrêté » (un accès mémoire invalide, provoqué) ; Relancer IHM Studio ;
- `1228_05_rapport_dans_l_appli.png` : au lancement suivant, le rapport dans l'application ; derrière, l'accueil et « Découvrir l'IHM · la visite, 3 min » ;
- `1228_10_aide_didacticiel_cartes.png` : F1, Démarrer › Le didacticiel - les six parcours en cartes, au-dessus de la page ;
- `1228_11_aide_parcours_symbole.png` : la page d'un parcours, sa carte et ses neuf étapes ;
- `1228_12_aide_symboles_parcours_guide.png` : la page des symboles propose « Parcours guidé : Créer un symbole de projet » ;
- `1228_13_aide_ihm_deplie.png` puis `1228_14_aide_ihm_replie.png` : la page de la Vanne ouverte, L'IHM déplié, puis replié d'un clic ;
- `1228_15_visite_etape_1.png` et `1228_16_visite_etape_4_les_vues.png` : la visite lancée depuis sa carte de l'aide, ses 16 étapes ;
- `installateur/` : les neuf écrans de l'installateur, et six animations (GIF) : l'accueil, une transition de page, la colonne des étapes sur toute l'installation, la copie, ce qui a été installé, la fin.

Deux détails vus sur les captures et corrigés avant la livraison : l'accueil annonçait une visite de 5 min (sa carte dit 3) ; la page d'un seul parcours affichait « 1 parcours · 0 fait » au-dessus de sa carte.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.12.28.exe` : l'installateur, qui pose les deux applications ;
- `API-Studio-1.12.28-portable.zip` et `IHM-Studio-1.12.28-portable.zip` : chaque application, sans installation ;
- les captures (`captures-1.12.28.zip`) ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.12.28.exe`. Il met à jour la version installée (la 1.12.27 ou une plus ancienne) au même endroit, et garde tes données. Les projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.12.28.exe` (33,0 Mo, 34 594 786 octets) :
`F61286FF4E7AB8F35ABB0E29736A86FC7ACADCD0454C13B1F9C089C8B2ADBD65`

Les zips portables (151 fichiers chacun ; décompresse, puis lance l'exe) :
- `API-Studio-1.12.28-portable.zip` (24,3 Mo) :
  `F8907132C6BA1DB3961370C6689FE3F791DC16FA66327A04BE88B3BA0DA2823B`
- `IHM-Studio-1.12.28-portable.zip` (24,3 Mo) :
  `821CB5EB3468A9E10F8F634545B5D7642D82157713EADDCA089B374EBF5E8B81`

## 5. Les vérifications sous Windows

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine : `--version` répond `API Studio 1.12.28` et `IHM Studio 1.12.28` ; `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB). Un vrai blocage et un vrai plantage ont ouvert la fenêtre de rapport à part ; quatre séances scriptées ont pris les autres captures.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine :
- l'assistant complet, sur un Wine neuf : chaque page s'affiche et s'anime, l'installation réussit, la fin s'affiche ; Précédent et Annuler marchent ; à 150 % et 200 %, les textes tiennent ;
- en silencieux, pour tous les comptes, par-dessus la 1.12.27 (installée avant : `IHM Studio 1.12.27`) : « Installation process succeeded » ; les deux exe installés sont identiques à ceux qui ont été testés et répondent `1.12.28` ; le menu Démarrer garde son dossier **API IHM Studio** ;
- /SILENT (la petite fenêtre de progression d'Inno Setup) et la désinstallation silencieuse réussissent.

**Pas fait :**
- une vraie mise en veille d'un PC Windows : sous Wine, la veille ne se joue pas ; les essais simulent les avis de Windows et un processus figé ;
- l'installateur sur un vrai Windows : sa fluidité (une minuterie de 15 ms) et le rendu du texte n'y ont pas été vus ; sous Wine, PowerShell manque, et l'installateur prend son chemin de secours, comme en 1.12.27 ;
- le projet Visual Studio n'a pas été compilé (pas de MSVC ici) ; il liste bien tous les fichiers (487 .cpp, comme CMake).

## 6. La suite

- Sur un vrai Windows : mettre le PC en veille, l'application ouverte, puis le réveiller (aucune fenêtre de blocage ne doit s'ouvrir) ; dérouler l'installateur animé.
- En attente : les fonctions IHM génériques (TEMPLATE) partout ; les raccourcis clavier avancés des éditeurs.
