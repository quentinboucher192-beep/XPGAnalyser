# IHM Studio

Référence fonctionnelle d'**IHM Studio** (exécutable `IHM-Studio.exe`, édition `core::Edition::Ihm`), version 1.22.0 : conception, simulation et exploitation d'une interface homme-machine de supervision. Chaque section renvoie au code qui l'implémente ; le mode d'emploi détaillé, écran par écran, est l'aide F1 (source : `tools/guide-ihm/guide-ihm.txt`, 269 sujets d'après la note de livraison 1.21.3). L'Assistant IA a sa propre référence : [ASSISTANT-IA.md](ASSISTANT-IA.md). Retour au [README](../README.md).

## Sommaire

1. [Rôle et périmètre](#role)
2. [Projets IHM](#projets)
3. [L'éditeur de vues](#editeur)
4. [Objets graphiques](#objets)
5. [Propriétés, animations, actions](#proprietes)
6. [Vues, popups, modèles, symboles](#vues)
7. [Variables et types IHM](#variables)
8. [Scripts et fonctions](#scripts)
9. [Alarmes, recettes, historiques, rapports](#alarmes)
10. [Utilisateurs et sécurité](#securite)
11. [Équipements et communications](#communication)
12. [Ressources, médias et fichiers externes](#ressources)
13. [Langues, unités, styles, affichage](#langues)
14. [Générer, Compiler et diagnostics](#compiler)
15. [Simulation](#simulation)
16. [Poste d'exploitation et accès web](#poste)
17. [Enregistrements](#enregistrements)
18. [Incidents](#incidents)
19. [Échanges, documentation et historique](#echanges)
20. [Limitations](#limitations)

---

<a id="role"></a>
## 1. Rôle et périmètre

IHM Studio conçoit les écrans d'exploitation d'une installation (vues, synoptiques, alarmes, recettes, utilisateurs), les relie à des équipements par **Modbus TCP**, les essaie en **simulation** et les fait tourner en plein écran sur un **poste d'exploitation**.

Depuis la 1.12.0, IHM Studio **ne contient pas le programme de l'automate** : ses variables sont les siennes (variables IHM), liées aux adresses de ses équipements ; l'automate est un équipement Modbus TCP comme un autre (`docs/LIVRAISON-1.12.0.md`, `src/project/ProjectStore.hpp`). Le préfixe `API.` des variables de l'automate n'existe que dans l'édition qui a l'automate.

Le modèle IHM ne dépend pas de l'écran (`src/hmi/HmiModel.hpp` : « le modèle ne sait rien de l'écran ») ; l'éditeur et les volets sont dans `src/app/hmi/`.

---

<a id="projets"></a>
## 2. Projets IHM

- **Nouveau projet IHM** (Ctrl+N) : un dossier `<dossier des données>/projets/ihm/<Nom>`, un manifeste `project.xpgproj` (`edition = ihm`) et une vue de démarrage (`App::projectsRoot`, `src/app/App.cpp`).
- **Ouvrir** (Ctrl+O) : un dossier de projet IHM ou une archive `.xihm` ; un projet d'API Studio est refusé.
- **Format** : du texte, un fichier par vue, sous `ihm/` (`src/hmi/HmiStore.hpp`) — détail dans [FORMATS.md](FORMATS.md).
- **Enregistrer** (Ctrl+S) : écriture atomique, index écrit en dernier, chaque fichier terminé par `fin` ; une vue retirée de l'index part dans `ihm/corbeille/`.
- **États** DEV, FINISH, LOCK et **versions** du projet : comme dans API Studio ([API-STUDIO.md](API-STUDIO.md#projets)). Le poste d'exploitation n'ouvre qu'un projet FINISH.
- **Reprise après arrêt brutal** : voir le [README](../README.md#communes).

---

<a id="editeur"></a>
## 3. L'éditeur de vues

`src/app/hmi/HmiEditor.*`, `HmiCanvas.*`, `HmiPanels.*`.

- Un onglet par vue ouverte : barre d'outils, explorateur d'objets, **la vue** (zoom, règles, guides, cotes), **l'inspecteur** à droite, **la bibliothèque d'objets**.
- Onglets de l'inspecteur : **Propriétés**, **Actions**, **Contenu** (selon l'objet), **Code brut**, **Raccourcis**.
- **Code brut** : le texte exact d'un objet (ses lignes du fichier de vue), copiable et recollable ; relu sans perte par le lecteur des vues (`hmi::serializeObjectCode`, `hmi::parseObjectCode`).
- Gestes : poser depuis la bibliothèque, déplacer, redimensionner, grouper, calques, aligner, distribuer, dupliquer (Ctrl+D), **dupliquer avec repères** (Ctrl+Maj+D : les `$…$` avancent d'une copie à l'autre).
- **Copier/coller avec Excel** dans les tableaux des volets (`src/app/TablePaste.*`) ; **plusieurs fenêtres** pour copier d'un projet à l'autre (Ctrl+Maj+N).
- **Rechercher et remplacer** dans une vue ou tout le projet, avec aperçu.
- **Modèles de vues** (galerie) et **paquets** d'export de vues, symboles, types, fonctions, scripts (`.xpgvues`, `.xpgsymboles`, `.xpgtypes`, `.xpgfonctions`, `.xpgscripts`…).
- Toute modification est une commande annulable (Ctrl+Z / Ctrl+Y, historique Ctrl+H).

---

<a id="objets"></a>
## 4. Objets graphiques

- **116 genres d'objets** déclarés dans `kKindInfos` (`src/hmi/HmiModel.hpp`, garanti par un `static_assert`), chacun avec sa clé enregistrée, son libellé et ses propriétés par défaut (`hmi::makeObject`).
- La bibliothèque les range en **13 familles** (`categories()`, `src/app/hmi/HmiPanels.cpp`) : Formes, Commandes, Affichage, Graphiques, Alarmes, Production, Régulation, Synoptique, Navigation et structure, Communication, Médias, Enregistrements, Utilisateurs.
- Exemples : cuve, silo, trémie, pompe, moteur, vanne et vanne 3 voies, tuyauterie, variateur, instrument ISA, disjoncteur… (synoptique) ; bouton, bouton à impulsion, interrupteur, curseur, champ de saisie, liste déroulante, programmateur horaire… (commandes) ; voyant, jauge, bargraphe, afficheur numérique, afficheur 7 segments, tableau, code QR… (affichage) ; courbe, courbe XY, graphique en barres, camembert, radar, histogramme, chronogramme (graphiques) ; bandeau, compteur, historique et statistiques d'alarmes ; gestion et éditeur de recettes, compteurs de production ; **Réglage PID** ; conteneurs, onglets, barre de navigation, fil d'Ariane ; état de la communication, diagnostic automate ; vidéo, visionneuse de documents, image animée, GIF animé, explorateur de fichiers ; enregistreur, lecteur d'enregistrements ; connexion, gestion des utilisateurs.
- Un rôle par propriété (nombre, booléen, couleur, formule, texte, durée…) : `src/hmi/HmiRoles.hpp`.
- Bibliothèque d'images embarquée, dessinée en SVG depuis `tools/images/images.txt` (`tools/images/generer_images.py`), et **Mes images** de l'utilisateur (`<réglages>/images/`).

---

<a id="proprietes"></a>
## 5. Propriétés, animations, actions

- Chaque propriété a une **valeur** et, au besoin, une **expression** (formule) évaluée en marche : animation d'une couleur, d'un niveau, d'une visibilité, textes à trous.
- La propriété `variable` d'un objet de commande ou d'affichage désigne la variable lue ou écrite.
- **Actions** sur déclencheurs (clic, appui, touche enfoncée/relâchée/maintenue, gestes sur un tableau…) : ouvrir une vue ou une popup, écrire une variable, exécuter un script, etc. (`src/hmi/HmiActionKinds.*`).
- **Raccourcis clavier d'une vue** (format de projet 24), **constantes et fonctions d'une vue** (format 26).
- **Variables d'instance** `Vue.Objet.Propriété` lisibles dans les scripts ; variables système `SYS.` (`src/hmi/HmiRuntimeSystem.*`).

---

<a id="vues"></a>
## 6. Vues, popups, modèles, symboles

- **Rôles de vue** : vue, popup, écran modèle, en-tête, pied de page, symbole (`src/hmi/HmiTemplates.hpp`).
- **Popups** avec paramètres (format 7), appelées avec leurs valeurs.
- **Symboles réutilisables** : une vue de rôle symbole, posée en instances ; paramètres, opérateurs, alarmes propres, génériques ; « appliquer copie sur référence ».
- **Navigation** : vue de démarrage, vue parente, vues de démarrage par groupe d'utilisateurs, objets de navigation.
- Rangement en **dossiers** (vues, popups, modèles, symboles, scripts, types, styles, ressources) par glisser-déposer.

---

<a id="variables"></a>
## 7. Variables et types IHM

- **Variables IHM** : nom, type, valeur initiale, description, dossier ; liées ou non à un équipement (adresse, mise à l'échelle) ; **rémanentes** en exploitation au besoin (`src/hmi/HmiRetain.hpp`).
- **Types IHM** (`src/hmi/HmiTypes.*`, `HmiTypeRegistry.*`) : structures, tableaux (y compris à N dimensions), **énumérations** (avec conversions vers/depuis le texte), **types objets** (méthodes, membres privés, héritage, interfaces), types rappel, **génériques** (`T_Pile<T>`), **unités de mesure** dans les types et contrôle strict optionnel (`src/hmi/HmiUnits.hpp`), **espaces de noms et bibliothèques** reliées. Chacune de ces capacités correspond à un numéro de format de projet (voir [FORMATS.md](FORMATS.md#format-ihm)).
- Import et export des membres d'un type avec Excel.
- Visualisation graphique des variables.

---

<a id="scripts"></a>
## 8. Scripts et fonctions

- **Scripts généraux** (au démarrage, cycliques, sur changement, appelés) et **scripts de vue** ; **fonctions IHM** ; branche **Natives** (fonctions et types natifs utilisables dans les scripts, en lecture seule, générée par `outils/natives/generer.py` depuis `outils/natives/catalogue.py`).
- **Langage** : le **ST** est analysé et exécuté par l'interpréteur de l'application (dialecte IHM : `VAR`/`VAR_TEMP`, JSON, expressions régulières, `MAP`, `FOR EACH`, `SET OF`, `RING[N] OF`, `CALLBACK`, `LAMBDA`, `TEMPLATE`, gestion d'erreurs `LEVER`/`RELEVER`, `WAIT_UNTIL`…). Les scripts écrits en **C ou C++ ne sont ni compilés ni exécutés** : seule une vérification de forme (accolades, parenthèses, chaînes) est faite, et c'est signalé (`src/hmi/HmiScript.hpp`, `src/hmi/HmiCheck.hpp`).
- **Erreurs à leur place**, aide à la saisie, champs à expression.
- **Tests unitaires d'une fonction** : des cas rejoués sur un banc (`src/hmi/HmiFunctionTests.hpp`, format 29).
- **Débogueur des scripts IHM** : points d'arrêt, pas à pas, pile, variables locales (`src/hmi/HmiRuntimeDebug.*`).
- **Journal** `IHM_LOG` et console (Ctrl+J : panneau du bas).

---

<a id="alarmes"></a>
## 9. Alarmes, recettes, historiques, rapports

- **Alarmes** : condition, message, priorité, délai, groupes d'alarmes et leurs réglages, alarmes propres à un objet ou surchargées sur un objet, sons par priorité, consigne d'alarme ; objets bandeau, compteur, historique, résumé par zone, statistiques (`src/hmi/HmiAlarmGroups.*`, `HmiObjectAlarms.*`).
- **Notifications des alarmes** par **courriel** (SMTP **sans TLS**, vers le relais local de l'usine, `AUTH LOGIN` facultatif) et par **SMS** via une passerelle HTTP ; destinataires par priorité, groupe et astreinte, anti-rafale ; boîte d'essai locale (`src/hmi/HmiNotify.hpp`).
- **Recettes** : gestion, éditeur, import/export CSV, comparaison (`src/hmi/HmiRecipes.hpp`).
- **Historiques et courbes** ; **rapports périodiques** (`src/hmi/HmiReports.hpp`).
- Données d'exploitation dans `ihm/historique/` (alarmes, mesures, audit).

---

<a id="securite"></a>
## 10. Utilisateurs et sécurité

- Utilisateurs, groupes et permissions (par exemple **Piloter**, **Administrer**), menu de connexion, objets de connexion et de gestion des utilisateurs.
- **Politique des mots de passe**, verrouillage, **badge** (`src/hmi/HmiPolicy.hpp`) ; mots de passe stockés en empreinte salée (`src/hmi/HmiCrypto.*`).
- **Signature électronique** d'une commande (`src/hmi/HmiSignature.hpp`) et **journal d'audit**.

---

<a id="communication"></a>
## 11. Équipements et communications

- **Équipements** (`src/hmi/HmiEquipment.hpp`) de deux types : **Modbus TCP/IP** (registres lus et écrits) et **Ethernet TCP/IP** (seulement joignable ou non). Adresses au format Schneider (`%MW100`, `%MF20`, `%M5`, `%MW10.3`…) ou Modicon (`40101`, `30001`, `00017`, `10005`, `HR100`…), mise à l'échelle brut → physique.
- **Variables liées** à un équipement, **zones mémoire**, carte mémoire, types d'accès, **qualité des valeurs** lues.
- **Outil Modbus** : lire, écrire, trames, **lecture cyclique** à plusieurs requêtes, **espion** des trames (capture par Npcap sous Windows ou libpcap sous Linux, chargés à la demande s'ils sont installés ; retirable par `XPG_SANS_NPCAP`) — `src/hmi/HmiModbusTool.*`, `HmiModbusCyclic.*`, `HmiSpy.hpp`.
- **Réseau du PC** (ports Ethernet et adresses) et **scanner IP** : ports TCP connus (502 Modbus, 80/443 web, 102 S7, 44818 EtherNet/IP, 4840 OPC UA, 9100 imprimante) pour **identifier** des appareils ; aucun de ces autres protocoles n'est piloté (`src/hmi/HmiIpScan.hpp`).
- **Découverte des équipements sans intrusion** (1.21.0, `src/hmi/HmiDiscovery.hpp`) : ne lit que ce que les appareils annoncent.
- **Esclaves Modbus simulés (jumeaux)** : un équipement virtuel pour essayer sans matériel, valeurs animées ou forcées (`src/hmi/HmiTwin.hpp`, `HmiZones.*`).
- Sous Windows, l'installateur peut ajouter une règle de pare-feu entrante pour `IHM-Studio.exe` (installation pour tous les comptes).

---

<a id="ressources"></a>
## 12. Ressources, médias et fichiers externes

- **Ressources** du projet (`ihm/ressources/`) : images, sons, vidéos, polices.
- **Vidéo** : Motion-JPEG en AVI lu partout ; MP4/H.264, WMV, MOV via Media Foundation sous Windows (`src/hmi/HmiVideo*.cpp`).
- **Visionneuse de documents** : PDF par PDFium chargé à la demande (`pdfium.dll` à côté de l'exécutable, ou `XPG_PDFIUM`) ; sans PDFium, la visionneuse le dit (`third_party/pdfium/ORIGINE.txt`).
- **Fichiers externes** (`src/hmi/HmiExternal.hpp`) : Excel (`.xlsx`/`.xlsm`), CSV, TXT, JSON, XML, **SQLite** (lu directement dans le fichier, sans pilote). Une « base externe » par chaîne de connexion est seulement décrite : la connexion demande un pilote que l'outil n'embarque pas.

---

<a id="langues"></a>
## 13. Langues, unités, styles, affichage

- **Plusieurs langues** et sélecteur de langue en marche ; vue des langues côte à côte (`src/hmi/HmiLanguages.hpp`).
- **Unités et formats** des variables.
- **Styles nommés** réutilisés par les objets.
- Taille, couleurs, thème de l'IHM ; sélecteur de thème en marche.

---

<a id="compiler"></a>
## 14. Générer, Compiler et diagnostics

- **Générer** vérifie la cohérence : vues invalides, noms en double, calques et groupes cassés, références circulaires, variables inexistantes, ressources manquantes ou inutilisées, fichiers externes absents ou modifiés (`src/hmi/HmiCheck.hpp`).
- **Compiler** (F7) traduit tout ce qui s'exécute : expressions de propriétés, textes à trous, scripts ST. Chaque constat dit où (vue, objet, propriété) et s'ouvre d'un double-clic.
- **Génération incrémentale** (`src/hmi/HmiPipeline.hpp`) : seul ce qui a changé, et ce qui en dépend, est refait ; cache dans `.xpg/build/build-cache.txt` du projet.
- **Vérification globale du projet**, analyse et optimisation des adresses (`src/hmi/HmiAudit.hpp`) ; **graphe des relations** (qui utilise quoi, orphelins) (`src/hmi/HmiRelations.*`).
- **Essais de réception** décrits dans le projet.

---

<a id="simulation"></a>
## 15. Simulation

- **Démarrer l'IHM** (F8), **Arrêter l'IHM** (Maj+F8) : l'IHM tourne dans l'application, avec ses équipements simulés (`src/app/hmi/HmiSimulation.*`).
- Dossier **Simulation** de l'arbre : Vue d'ensemble, Forçages, Courbes, Journal (`src/app/SimCenter.hpp`).
- Rémanence de simulation (les valeurs reprennent d'une séance à l'autre, `src/hmi/HmiSimData.*`), page Simulation des paramètres système.
- La simulation de l'automate (programme ST) n'existe que dans API Studio.

---

<a id="poste"></a>
## 16. Poste d'exploitation et accès web

- **Poste d'exploitation** : l'IHM seule, en plein écran, sans l'éditeur — `IHM-Studio.exe --poste <dossier du projet>` (ou `--ihm`), bouton de l'accueil, ou installation dédiée (profil « Poste d'exploitation » de l'installateur). Kiosque, mot de passe de sortie (Ctrl+Alt+Q), écrans secondaires, démarrage avec la session (valeur « XPG Poste d'exploitation » de la clé `Run` sous Windows ; fichier `xpg-poste.desktop` dans `~/.config/autostart` sous Linux — `src/app/hmi/HmiStationLaunch.*`), lanceur `lancer-poste.cmd` écrit dans le dossier du projet. Projet FINISH exigé. État sauvegardé toutes les 10 s pour la reprise (`src/app/screens/StationScreen.*`, `src/app/Recovery.hpp`).
- **Accès web** (`src/hmi/HmiWeb.hpp`) : serveur HTTP intégré (port 8080 par défaut d'après l'aide) qui diffuse **l'image JPEG** de la vue en marche ; comptes de l'IHM, lecture seule par défaut, clics transmis seulement si l'accès le permet et que l'utilisateur a la permission Piloter. Rien d'autre ne sort (ni fichier, ni projet). Le serveur est en HTTP, sans TLS.
- L'export de l'IHM en **page web autonome** a été **retiré en 1.13.0** (aide F1, sujet « page-web »).

---

<a id="enregistrements"></a>
## 17. Enregistrements

`src/hmi/HmiRecording.hpp` (1.14.0) : pendant que l'IHM tourne, un enregistrement garde les valeurs (image complète puis changements), l'écran (vue, popups, langue, thème), les gestes de l'opérateur et les événements (alarmes, sons, journal), dans un fichier **`.xrec`** (zip ; depuis la 1.20.2, il contient une copie chiffrée du projet au départ). Relecture à vitesse réglable. Pilotage depuis une autre application :

```text
IHM-Studio.exe --enregistrer "Nom" [--duree 10m] [--mot-de-passe x]
IHM-Studio.exe --arreter-enregistrement [--jeter]
IHM-Studio.exe --rejouer "Nom" [--vitesse 2] [--mot-de-passe x]
IHM-Studio.exe --quitter-relecture
```

Codes de sortie : 0 fait, 1 option mal écrite, 2 aucune IHM en marche, 3 nom introuvable, 4 mot de passe faux, 5 refusé par la configuration, 6 occupée, 7 erreur de fichier (`src/main.cpp`).

---

<a id="incidents"></a>
## 18. Incidents

Depuis la 1.20.0 (`src/hmi/HmiIncident.hpp`, `src/app/ai/AiIncident.hpp`) : à l'apparition d'une alarme (fronts seulement, jamais à chaque cycle), un **dossier d'incident** autonome est créé dans `ihm/ressources/enregistrements/` : contexte copié au moment du défaut (alarme, chaîne de dépendances lue dans le graphe des relations, valeurs disponibles, chronologie, alarmes liées, pistes classées avec leur niveau de certitude), capture, vidéo Motion-JPEG des secondes autour du défaut, analyses et commentaires successifs, empreintes SHA-256 vérifiables par `sha256sum`. Valider un incident le copie vers `valides/` après vérification.

Le code le précise : cette fonction aide à comprendre un événement ; elle n'est pas une surveillance de sécurité de la logique automate.

---

<a id="echanges"></a>
## 19. Échanges, documentation et historique

- **Archive `.xihm`** : le projet entier en un fichier vérifiable (**Projet › Exporter en archive .xihm…** ; ouverture par double-clic si l'association est installée) — `src/hmi/HmiArchive.hpp`, voir [FORMATS.md](FORMATS.md).
- **Exporter / Importer** l'IHM : archive (`.xihm`, ou `.zip` des versions 1.x) ; importer reconstruit l'IHM du projet ouvert en une commande annulable.
- **Dossier de l'IHM** en **Word ou PDF** (`src/hmi/HmiDossier.hpp`) : couverture, présentation, chaque vue avec sa vignette, variables, alarmes, recettes, utilisateurs, scripts, essais ; jamais les mots de passe.
- **Historique des modifications des objets** (`src/hmi/HmiEditHistory.hpp`) : à chaque enregistrement, chaque objet créé, modifié ou supprimé est noté propriété par propriété, signé (date, nom du PC, compte Windows, version), 100 entrées par objet, dans `ihm/historique-edition/<id de vue>.txt`.

---

<a id="limitations"></a>
## 20. Limitations

- Communication : **Modbus TCP** seulement (plus la surveillance « joignable » d'un appareil Ethernet). Modbus RTU, S7, EtherNet/IP, OPC UA ne sont pas pilotés (le scanner IP ne fait que reconnaître leurs ports).
- Scripts **C/C++** : vérifiés en forme seulement, jamais exécutés.
- Courriel **sans TLS** (relais local) ; serveur web **HTTP**.
- Bases de données externes par chaîne de connexion : non connectées.
- Vidéo MP4/WMV/MOV : Windows seulement (Media Foundation).
- Pas de programme automate dans IHM Studio : il se relie à l'automate comme à un équipement.
- Les exécutables graphiques livrés sont pour Windows x64 ; voir les plates-formes dans le [README](../README.md#plates-formes).
