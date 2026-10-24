# API Studio et IHM Studio 1.12.31 : la page web en marche, le zoom de toute la page, le cache dans l'archive

Livrée le 24/10/2026, dans la journée. Elle suit la 1.12.30 (`docs/LIVRAISON-1.12.30.md`).

Elle répond à :

> « le html doit etre dynamique exactement comme l'appli faire les requetes modbus etc, tout pareil mais sur un html
> Un projet rouvert depuis une archive se régénère, car son cache n'y est pas.
> Défaut d'avant cette version, trouvé en passant et pas corrigé : dans l'aperçu relié de l'éditeur, une propriété de rôle Formule tapée dans l'inspecteur n'est pas évaluée. Ça touche les fins de course et le déclenchement des registres, et les positions A et B de la vanne 3 voies.
> Les barres d'outils, le panneau du bas, l'explorateur d'objets, les calques et la bibliothèque ne zooment pas encore. Le zoom n'est pas gardé d'une séance à l'autre.
> En AZERTY, Ctrl + − de la rangée des chiffres ne marche pas : prends le − du pavé numérique, la puce ou Ctrl + molette.
> fait tout ça »

**Ce qui n'est pas fait : les requêtes Modbus depuis la page.** La page fait tourner l'IHM comme l'application, mais ses équipements (Modbus TCP) n'y sont pas reliés : une variable liée à un équipement garde sa valeur initiale, et la page le dit (la barre du bas, le bouton Limites). Pour piloter les vrais appareils, ouvre le projet dans IHM Studio ou en poste d'exploitation.

## 1. Ce qui change

### La page web en marche

**Projet › Exporter en page web (.html)…** écrit toujours une seule page, sans Internet ; elle fait maintenant **tourner ton IHM** dans le navigateur. Le moteur de l'IHM d'IHM Studio et son dessin sont compilés pour le navigateur (WebAssembly) et embarqués dans la page avec le projet : c'est la simulation d'IHM Studio, avec le même dessin.

- **Les vues et les popups** s'animent ; la vue de démarrage s'ouvre d'abord, à la taille de la fenêtre.
- **La navigation** : les boutons qui ouvrent une vue ou une popup, Précédent, Fermer la popup ; une popup se déplace par son titre.
- **Les saisies** : boutons, interrupteurs, listes déroulantes, boutons radio, curseurs, champs de saisie au clavier (Entrée valide).
- **Les scripts** : cycliques, sur événement, au démarrage ; un script détaché attend sur WAIT sans figer la page ; les fonctions IHM.
- **Les alarmes** apparaissent et disparaissent avec leur condition, dans le bandeau et les listes d'alarmes, et s'acquittent.
- **Les utilisateurs** : Changer d'utilisateur ouvre le menu de connexion du poste ; une commande à signer ouvre le panneau de signature ; Paramètres système s'ouvre comme sur le poste.
- **Les variables Rémanentes** sont gardées par le navigateur et reviennent à l'ouverture suivante de la page.
- **La barre du bas** dit le mode, les équipements, l'utilisateur et la vue ; Alarmes, Variables, Scripts, Fiche ouvrent la documentation du projet ; **Limites** dit ce que la page ne fait pas ; Télécharger le projet enregistre l'archive.

Le moteur est livré dans le dossier `web` à côté des exe (l'installateur et les portables le posent). S'il manque, la page s'écrit au repos, comme en 1.12.30, et elle le dit. Il faut un navigateur récent (Edge, Chrome, Firefox, Safari de 2023 ou plus).

**Ce que la page ne fait pas** (un navigateur ne laisse pas une page ouvrir une connexion réseau à un appareil, ni toucher au PC) : les équipements Modbus TCP ne sont pas reliés ; pas de ping ni de recherche d'adresses IP, pas de port série, pas de courriel ni de SMS, pas de vidéo MP4/WMV, pas de lecteur PDF, pas de serveur web intégré, pas de clavier virtuel, pas de fenêtre Réglage PID ni de page Simulation dans Paramètres système. Dans un onglet caché, le navigateur ralentit la page.

### L'archive emporte le cache de construction

Un projet rouvert depuis une archive `.xihm`, ou depuis sa page web, **ne se régénère plus**. Le cache du dernier build (`.xpg/build/build-cache.txt`) voyage avec l'archive ; les artefacts se refont au déballage, chacun vérifié par l'empreinte que le cache a retenue. L'arbre dit « Projet à jour » et Démarrer ne génère ni ne compile rien.

- Ce qui a changé depuis le dernier build (un élément enregistré après lui, un fichier externe relié à sa copie) se refait : jamais un cache dépassé pris pour bon.
- Une archive d'avant, sans cache, s'ouvre toujours : son premier build refait tout.
- La `.xapi` d'un dossier qui a aussi son IHM l'emporte de même.
- Ce que ça pèse, sur le projet de démonstration (371 éléments) : + 29 Ko pour l'archive (1,9 Mo), + 39 Ko pour la page. Les artefacts, douze fois plus lourds, ne voyagent pas : ils se refont au déballage (moins d'une demi-seconde pour la démo).

### Les formules tapées dans l'inspecteur se calculent en marche

Une case qui n'attend qu'une formule, tapée `=FCO_RG1`, se lisait comme une constante en simulation, sur le poste et dans l'aperçu relié de l'éditeur : le registre restait à l'arrêt en discordance, le clapet ne se déclenchait jamais, la vanne 3 voies restait fermée. Toute propriété de rôle Formule se calcule maintenant comme une expression, pour tous les objets - les fins de course et le déclenchement des registres, les booléens A et B de la vanne 3 voies, une condition - et l'onglet Expressions de la simulation la montre avec sa valeur. Dans un symbole, le paramètre qu'elle cite reçoit l'argument de l'instance ; renommer la variable ou le paramètre la suit.

### Le zoom de toute la page, gardé d'une séance à l'autre

- **Toute la page** : la puce du zoom grandit maintenant ses barres d'outils et de filtres, ses sous-onglets, ses tableaux, ses fiches et sa ligne d'état, avec leurs icônes, leurs hauteurs et leurs marges ; dans l'éditeur de vues, l'**explorateur d'objets**, les **calques**, la **bibliothèque** et l'inspecteur aussi. Un code, une vue, un grafcet, le rack et l'IHM en marche gardent leur zoom à eux. Quand la place manque, une barre de filtres raccourcit sa recherche et ses libellés : rien ne sort de la page.
- **L'arbre du projet et le panneau du bas** (Sorties, Console, Diagnostics) ont chacun leur zoom : Ctrl + molette dessus, ou Ctrl + plus, Ctrl + moins, Ctrl + 0 quand le clavier y est ; le bandeau bas dit le pourcentage.
- **Gardé** : chaque page retrouve son zoom quand tu la rouvres, même le lendemain ; l'arbre, le panneau du bas et chaque code aussi. Revenir à 100 % (Ctrl + 0) l'oublie.
- **Ctrl + moins sur un clavier AZERTY** : la touche − de la rangée des chiffres (celle du 6) dézoome, comme celle du pavé numérique ; Ctrl + = (avec ou sans Maj) zoome. Les chiffres des autres raccourcis restent des chiffres. Vaut aussi pour un AZERTY belge et un QWERTZ.

## 2. Les essais

**86 suites sur 86** (84 à la 1.12.30). Les deux nouvelles :
- `web_moteur` : le moteur de l'IHM compilé pour le navigateur, essayé sous Node, sans navigateur : les cycles, les valeurs, un script cyclique, une consigne, une alarme, le WAIT d'un script détaché ;
- `web_page` : la page en marche dans Chromium : aucune erreur, la vue se dessine et change, un clic de navigation, une popup, une consigne saisie, un curseur, le WAIT d'un script détaché, une alarme, la documentation du projet.

`echanges` passe de 105 à 150 vérifications (le cache dans l'archive et dans la page, une archive sans cache, ce que pèse le cache, la page en marche et la page au repos) ; `zoom` couvre toute la page, l'arbre, le panneau du bas, le zoom gardé et l'AZERTY ; `hmieditor` (8469 contrôles, 8390 à la 1.12.30) compte les nouveautés : 428 notes, 64 versions, 248 sujets d'aide (Démarrer 28, L'IHM 224, L'automate 17). Le guide de l'aide et son C++ disent la même chose ; le projet Visual Studio (`XpgAnalyzer.vcxproj`) a tous les nouveaux fichiers.

Les trois lots ont été faits à part, puis fusionnés ; les essais ont tourné sur le tout.

## 3. Les captures

Dans `captures-1.12.31.zip` (63 images), un dossier par sujet :
- `1-page-web/` (30) : `livree_*`, prises avec les exe livrés - la fenêtre Exporter en page web, la page écrite, puis la page du projet de démonstration dans Chromium (en marche, trois secondes après, une navigation, Limites) ; `essai_*`, la page du projet d'essai - l'ouverture, la navigation, une popup, une saisie, le script détaché, une alarme puis acquittée, la documentation, Limites, la connexion (refusée, puis le menu), une commande et sa liste, Paramètres système, et une animation (`page-en-marche.gif`) ; `demo_*` et `armoire_*`, la démonstration et l'armoire d'essai en marche ;
- `2-archive-et-cache/` (6) : avant (1.12.30) et après - l'export, l'archive rouverte, Démarrer ;
- `3-formules-en-marche/` (8) : avant et après - l'inspecteur d'un registre, celui de la vanne 3 voies, l'aperçu relié, la fenêtre en marche ;
- `4-zoom/` (19) : à 150 et 200 %, les pages d'IHM Studio (variables, éditeur de vues, panneau du bas, arbre, alarmes, Générer, langues côte à côte) et d'API Studio (variables, configuration, code, simulation, forçages, panneau du bas, tâches) ; Ctrl + moins en AZERTY ; le zoom gardé d'une séance à l'autre (zoomé avant de fermer, rouvert, puis Revenir à 100 % qui l'oublie).

Les captures `livree_*` ont été prises sous Wine et dans Chromium avec les exe livrés ; les autres pendant le travail de chaque lot, avec son propre exe.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.12.31.exe` : l'installateur, qui pose les deux applications et le moteur de la page web ;
- `API-Studio-1.12.31-portable.zip` et `IHM-Studio-1.12.31-portable.zip` : chaque application, sans installation ;
- les captures (`captures-1.12.31.zip`) ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.12.31.exe`. Il met à jour la version installée (la 1.12.30 ou une plus ancienne) au même endroit, et garde tes données. Les projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.12.31.exe` (40,1 Mo, 42 056 039 octets, 3,7 Mo de plus que la 1.12.30 : le moteur de la page web) :
`F3687CC697045D985EE9442BCD3C2D0EABBC08D6E541AED42E4A1C2A362D0242`

Les zips portables (174 fichiers chacun, dont le dossier `web` ; décompresse, puis lance l'exe) :
- `API-Studio-1.12.31-portable.zip` (32,1 Mo) :
  `FAAA7004E1B4EF25353EC5F6664BB69FD5BD540048BBD2AAACC65471B525483A`
- `IHM-Studio-1.12.31-portable.zip` (32,1 Mo) :
  `78F828B82A37B0B5EAA50163DD3D91DC906E60F59FA4FF8FD38DF6011B8044C8`

## 5. Les vérifications sous Windows

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine : `--version` répond `API Studio 1.12.31` et `IHM Studio 1.12.31` ; `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB).

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, lancé en silencieux, pour tous les comptes, par-dessus la 1.12.30 officielle (installée avant : `IHM Studio 1.12.30`) : « Installation process succeeded ». Les deux exe et `pdfium.dll` installés sont identiques à ceux qui ont été testés ; le dossier `web\` est posé à côté, ses fichiers identiques au moteur essayé, et le manifeste les compte (174 fichiers). Les associations `.xihm` et `.xapi` sont toujours là.

**La page web écrite par l'exe livré** : sous Wine, sur une copie du projet de démonstration, Projet › Exporter en page web écrit une page de 8,1 Mo. Ouverte dans Chromium, elle est prête en 2,5 s et s'anime (l'heure, les extracteurs, les registres) ; un clic mène de la Synthèse au Réseau AMF ; 15 ms par image en moyenne ; aucune requête réseau, aucune erreur ; la barre du bas dit « 1 équipement : non relié dans cette page ».

**Pas fait (Wine et Chromium seulement)** :
- la page dans Edge, Firefox et Safari, et sur un vrai écran Windows ;
- Ctrl + moins sur un vrai clavier AZERTY sous Windows (essayé par les essais et sous Wine) ;
- Windows PowerShell 5.1 lui-même : les étapes de l'installateur ont tourné avec PowerShell 7.4.

## 6. Les limites, et la suite

**Les limites connues :**
- **La page web** : les équipements (Modbus TCP) n'y sont pas reliés (voir plus haut), ni le reste de « Ce que la page ne fait pas ».
- **Le zoom** : la barre du haut, les onglets, le bandeau bas, les fenêtres de dialogue, les infobulles et les menus restent à 100 % ; les règles d'une vue, la mini-carte du grafcet et le menu d'un code ne suivent pas le zoom ; dans une page étroite, quelques textes sont coupés sans « … ». Le zoom d'une vue dans l'éditeur, d'un grafcet ou du rack n'est pas gardé d'une séance à l'autre.

**En attente** : les fonctions IHM génériques (TEMPLATE) partout ; les raccourcis clavier avancés des éditeurs.
