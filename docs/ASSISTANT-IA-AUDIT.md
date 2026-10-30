# Assistant IA dans IHM Studio — audit (phase 1)

Écrit pour la 1.14.2, avant la maquette (partie du commit 6733013). Tout ce qui suit a été lu dans le code ;
les fichiers et les fonctions sont cités pour qu'on puisse vérifier. Pas de code écrit avant ce document.

---

## 1. Ce qui existe

### 1.1 L'architecture générale

| Dossier | Bibliothèque CMake | Rôle | Sait-il qu'il y a un écran ? |
|---|---|---|---|
| `src/core` | `xpg_core` | commandes et annulation (`core/Command.hpp`), signaux (`Signal.hpp`), `Result`, fichiers atomiques | non |
| `src/platform` | (dans `xpg_app`) | `Renderer.hpp` (l'interface de dessin), `FontAtlas`, `InputEvent.hpp`, le branchement SDL3 (`SdlRenderer.cpp`, `SdlEventPump.cpp`) | c'est l'écran |
| `src/ui`, `src/menu` | `xpg_ui` | le cadre d'interface : `Widget`, `WidgetHost`, les widgets (`ui/widgets/*`), le thème, les icônes, la pile d'écrans (`menu::MenuManager`, `menu::WidgetMenu`) | oui, par `gfx::IRenderer` seulement |
| `src/domain`, `import`, `export`, `project`, `sim` | `xpg_import` | le programme automate (API Studio) et le simulateur ST | non |
| `src/hmi` | `xpg_hmi` | **le modèle IHM** (`HmiModel.hpp`), ses fichiers (`HmiStore`, `HmiArchive`), ses commandes annulables (`HmiCommands`), l'édition (`HmiEdit`), Générer / Compiler (`HmiCheck`), le moteur d'exécution | **non** (« LE MODELE NE SAIT RIEN DE L'ECRAN », en tête de `HmiModel.hpp`) |
| `src/help` | `xpg_help` | l'aide, la table des raccourcis (`help/Shortcuts.cpp`) | non |
| `src/app` (+ `app/hmi`, `app/screens`) | `xpg_app` (bibliothèque d'objets) | l'application : `App`, l'écran du projet (`MainAnalysisScreen`), l'éditeur de vues (`app/hmi/HmiEditor`), les painters (`app/hmi/Hmi*Painter.cpp`) | oui |

Deux exécutables (`API-Studio`, `IHM-Studio`), un seul code : seul `main.cpp` change (`XPG_EDITION_*`,
`core/Edition.hpp`, `core::hasIhm()`). Les sources sont ramassées par **glob** (`xpg_sources()` dans
`CMakeLists.txt`) : un fichier ajouté sous `src/app/` se compile tout seul ; les essais, eux, se déclarent.

### 1.2 `ui::Widget` et `WidgetHost`

- `ui/Widget.hpp` : un arbre (le parent possède ses enfants par `unique_ptr`, l'enfant garde un pointeur
  non propriétaire sur son parent). Chaque widget a un `id` (cherché par `findById` : les essais et les
  séances de script s'en servent), des bornes, une marge, `Visibility` (Visible / Hidden / Collapsed),
  `enabled`, survol (`hovered`), focus (`acceptsFocus`, `takeFocus`), une infobulle (fixe, ou calculée :
  `setTooltipProvider`, `liveTooltip`).
- La boucle : `layout()` (si la mise en page est sale) → `render(PaintContext)` → `onPaint` puis les
  enfants, puis `onPaintOverlay` ; `dispatch(InputEvent)` (capture → cible → remontée). **Le dessin est
  refait à chaque image** par l'écran (`WidgetMenu::Render`) : un widget peint son état du moment, il n'a
  rien à « invalider » pour une animation (la `PaintContext::time` donne l'heure, pour un curseur, un
  indicateur). Ce qui peint au-dessus de tout (une liste ouverte, un menu) passe par `requestsOverlayPass`.
- Les événements (`platform/InputEvent.hpp`) : souris, molette, `KeyDown` (touche + Ctrl/Maj/Alt),
  `TextInput` (UTF-8), **`FileDropped`** (un fichier lâché, avec sa position : le glisser-déposer existe
  déjà, `App::dropFile` le passe d'abord au volet qui est sous la souris).
- La souris tenue (`captureMouse`), le pointeur (`ui::cursor`), les barres de défilement communes
  (`ui/widgets/ScrollBar.hpp` : `EdgeScrollBar` pour un widget qui dessine tout lui-même).
- **Le zoom** (1.12.30, « le contrat du zoom » dans `Widget.hpp`) : `ZoomSupport::Scaled` (la base met le
  widget en page dans un repère agrandi : texte, marges, icônes suivent sans rien coder), `Native` (il
  zoome lui-même : la vue, le code). Ctrl+molette, Ctrl+plus/moins ; la puce du bandeau bas.
- `ui/WidgetHost.hpp` : un arbre hors de la pile des écrans (une fenêtre détachée) — mise en page à la
  taille de la surface, dessin + passe du dessus + infobulle, focus (Tab). L'écran principal, lui, est un
  `menu::WidgetMenu` (`menu/IMenu.hpp`) qui fait la même chose pour sa racine.

### 1.3 Panneaux, fenêtres modales, commandes et raccourcis, Disposition

- **L'écran du projet** : `MainAnalysisScreen::buildUi` (`app/screens/MainAnalysisScreen.cpp`, l. 941) —
  un `DockLayout` (barre du haut `TopBar`, bandeau bas), au centre un `Splitter` vertical `analysis.outer`,
  dont la rangée du haut est le `Splitter` horizontal **`analysis.upper`** : l'explorateur (volet 0), la
  colonne du centre (volet 1 : les onglets `TabArea` et le panneau du bas `HmiBuildOutputPane`), l'ancien
  explorateur de variables (volet 2, toujours replié depuis le lot API 6). Par-dessus, un `OverlayHost` :
  les menus, le didacticiel, **le tiroir de l'historique** (`HistoryPanel`, Ctrl+H), Aller à (`GoToPanel`).
- **L'éditeur d'une vue** : `app/hmi/HmiEditor` (un onglet du centre) — la barre d'outils, l'explorateur
  d'objets, la vue (`HmiCanvas`), l'inspecteur à droite (`ui::TabControl` : Propriétés, Actions, Contenu,
  Code brut, Raccourcis), la bibliothèque (`HmiPalette`). Il ne possède pas la vue : il envoie des
  commandes (`Apply`) ; `canvas().selection()` donne la sélection.
- **Les fenêtres modales** : des `menu::WidgetMenu` de genre `MenuKind::Dialog` (ex.
  `app/DispositionDialog.cpp`), ouvertes par `app_.menus().ShowDialog(...)`. Leur cadre passe par
  `ui::dialogframe::place` (`ui/DialogFrame.hpp`) : elles se **déplacent** par le titre et
  **s'agrandissent** par les bords, comme toutes celles du logiciel. Une question avant d'agir :
  `app/hmi/HmiAskDialog` (un texte, une liste, des choix exclusifs avec un champ, bouton « danger »).
- **Les commandes de l'application** : `core::ActionRegistry` (`core/Command.hpp`), des identifiants
  texte (`"project.disposition"`, `"app.findFiles"`) enregistrés par `App::registerActions`
  (`app/App.cpp`, l. 691) ; leur touche vient de **la** table des raccourcis `help/Shortcuts.cpp`
  (`help::keys::bindingOf`). Les touches globales de l'écran sont dans `MainAnalysisScreen::handleShortcut`
  (`app/screens/HistoryWorkspace.cpp`, l. 380 : Ctrl+J le panneau du bas, Ctrl+H l'historique,
  Ctrl+Maj+P Chercher des fichiers…). La table du clic droit est `ui/Commands.hpp` (`ui::cmd::Id` : un
  mot, une icône, une touche par verbe). **Aller à / Faire (Ctrl+K)** : `app/screens/GoToWorkspace.cpp`
  (`kActions`, clés `act:…`, exécutées dans `goToResult`).
- **Ctrl+Maj+I est libre** (ni dans le contexte Général ni dans l'éditeur de vues de la table ; `Ctrl+I`
  n'existe que dans les éditeurs de code).
- **La Disposition** (`app/Disposition.hpp/.cpp`) : un catalogue de lignes (panneaux de la fenêtre,
  sous-onglets, pages) par édition, gardé dans les réglages (`disposition.*`), une fenêtre
  (`DispositionDialog`) avec son aperçu (`DispositionPreview`). Les panneaux de la fenêtre sont aussi des
  cases d'« Affichage › Panneaux » (`buildViewPanel`, `PanelToggle` : une case → un volet d'un
  `Splitter`, `collapsePane`), retenues dans les réglages (`loadWorkspace`).

### 1.4 `project::` et `hmi::Project`

- `project::` (dossier `src/project`) : le projet **automate** d'API Studio (sections, DFB, variables,
  `ProjectStore`) et ce qui les lit. IHM Studio n'en a qu'une partie : les variables de l'automate quand
  un export est relié (`hmi/HmiApiVars.hpp`, le préfixe `API.`).
- `hmi::Project` (`hmi/HmiModel.hpp`, l. 2258) : `config`, `views` (les vues, popups, symboles, modèles),
  `assets` (ressources, fichiers externes), `programs` (scripts généraux, **variables IHM**
  `programs.variables`, fonctions, types), alarmes, recettes, sécurité, styles, langues, communication,
  équipements… et `nextId` (`allocate()` : un identifiant ne revient jamais).
- `hmi::Document` (`hmi/HmiCommands.hpp`) : le projet + `changed` (le signal : la vue touchée) + `dirty`.
  `App::hmi()` le donne ; `App::apply(command)` l'empile.

### 1.5 `hmi::Object`, `Kind`, les 116 genres, les propriétés

- **Une seule structure** `hmi::Object` (`HmiModel.hpp`, l. 739) : `id`, `kind`, `name`, `layer`,
  `parent` (le groupe ou le conteneur), `locked`, `hidden`, **`props`** (un sac de `Prop {key, value,
  expr}` : la valeur en texte, et une expression facultative), `actions`, alarmes et fonctions
  surchargées. Pas de classe par genre : le dessin et le comportement sont des `switch` sur `kind`.
- **`Kind` et `kKindInfos`** (l. 54 et 251) : 116 lignes `{Kind, clé enregistrée, libellé}` — `"Tank"` /
  « Cuve », `"Pump"` / « Pompe », `"Valve"` / « Vanne », `"PushButton"` / « Bouton à impulsion »… ;
  `kindKey`, `kindLabel`, `kindFromKey`. Un `static_assert` garde l'ordre.
- **La création par défaut d'un genre** : `hmi::makeObject(Kind, Id, nom, x, y, calque)`
  (`HmiModel.cpp`, l. 562) donne à chaque genre sa taille et ses propriétés (ex. une cuve : `common` x, y,
  w, h, rot, visible, opacity ; `synoptic` value, fault, couleurs, label ; `vessel` fillColor, min, max,
  unit, seuils ; et pour tous sauf le groupe `communication` : **`variable`**, refresh, access).
  `hmi::edit::add(Project&, View&, Kind, x, y)` (`HmiEdit.cpp`, l. 282) est le geste natif : un nom libre
  (`uniqueObjectName`), le calque actif. Les variantes d'un genre : `kindVariants`, `applyKindVariant`.
- **Les familles** : `kindIsSynoptic`, `kindHasLevel`, `kindIsChart`, `kindIsAlarmView`,
  `kindHoldsChildren`, `kindWritesVariable`, `kindShowsValue`… (en ligne dans `HmiModel.hpp`) ; la
  bibliothèque de l'éditeur range les genres en 13 familles (`categories()` dans `app/hmi/HmiPanels.cpp`,
  l. 1400 : Formes, Commandes, Affichage, Graphiques, Alarmes, Production, Régulation, Synoptique,
  Navigation et structure, Communication, Médias, Enregistrements, Utilisateurs).
- **Le rôle de chaque propriété** : `hmi/HmiRoles.hpp` (`kKeyRoles`, `roleOf`) — Nombre, Booléen, État,
  Texte, Durée, Couleur, Nom, Formule, Liste…, et le type de `Vue.Objet.Clé`. C'est la table qu'un
  validateur doit lire (une couleur attendue, une formule…).
- **Associer une variable** : la propriété `variable` (rôle Formule) — la variable écrite par une
  commande, lue par un symbole quand `value` n'a pas d'expression ; ou une expression sur `value`. Les
  variables connues : `programs.variables` (IHM) et, si un projet automate est relié, ses noms
  (`hmi::NameExists`, `HmiCheck.hpp`).

### 1.6 Les painters et `platform/Renderer.hpp`

- `platform/Renderer.hpp` : `gfx::IRenderer` — rectangles (pleins, arrondis, contours), lignes, texte
  (atlas de police : `FontId` = une taille en pixels, `measure`, `fitCharacters`), triangles colorés,
  texte tourné, images RGBA, découpe (`pushClip`), et **dessin hors écran** (`beginOffscreen` /
  `endOffscreen`). Une seule implémentation réelle : `SdlRenderer` (SDL_Renderer de SDL3). Les essais
  passent un renderer qui enregistre.
- Les painters (`app/hmi/HmiPainter.hpp`) : `paintHmiView`, `paintHmiObject` (un `HmiViewport` : origine
  et zoom), `paintHmiViewPreview` (une vue ajustée dans un rectangle — la vignette au survol, l. 2393 de
  `HmiPainter.cpp`), `HmiPropertySource` (les valeurs statiques dans l'éditeur, les expressions en
  marche). **Un aperçu fidèle d'une vue modifiée = ces fonctions sur une copie de la vue.**

### 1.7 La sauvegarde

- Le projet IHM : du **texte**, un fichier par vue (`hmi/HmiStore.hpp` : `ihm/ihm.txt`,
  `ihm/vues/0003-Synoptique.vue`…), écrit à côté puis renommé, terminé par `fin` ;
  **`kFormatVersion = 33`** (`HmiStore.hpp`, l. 47) ; une clé inconnue est dite (`LoadReport.warnings`).
- **L'archive `.xihm`** (`hmi/HmiArchive.hpp`, 1.12.30) : le dossier du projet entier dans un zip (miniz).
- **Les réglages de l'utilisateur** : `app/Settings` (un fichier `clé=valeur`, les clés inconnues
  gardées) ; `Settings::sharedFolder()` (%APPDATA%\XpgAnalyzer, ~/.config/xpg-analyzer) tient ce que les
  deux applications partagent : les thèmes (`themes/`), **Mes images** (`images/`, fixé par
  `hmi::imagelib::setUserFolder` dans `App::create`, `app/App.cpp` l. 263). C'est là que va **`IA/`**
  (`app/ai/AiPaths.hpp`, le contrat).

### 1.8 L'annulation (Undo / Redo)

- `core::CommandStack` (`core/Command.hpp`) : une pile de `ICommand` (execute / undo), profonde,
  fusion des gestes continus, l'historique (Ctrl+H) montre chaque pas.
- Côté IHM, **par instantané** : `hmi::changeView(doc, vue, libellé, [](Project&, View&){...})` rend une
  `ViewCommand` qui garde la vue avant/après **sans ses objets** et les seuls objets qui changent ;
  `hmi::changeProject` fait de même pour le projet (vues comprises). Une fonction qui ne change rien rend
  `nullptr` : rien n'est empilé.
- **Un changement multiple en un Ctrl+Z** : soit une seule fonction `changeView` qui fait tout (créer 31
  objets, en modifier, en supprimer : UNE commande) ; soit, à travers plusieurs commandes,
  `CommandStack::beginGroup / endGroup` (ou `core::CommandGroupScope`, lot 20) qui les empile comme **un**
  pas (`GroupCommand`). Les identifiants alloués ne reviennent jamais, même annulés.

### 1.9 Le threading

- **Un seul fil pour l'interface et le modèle** : tout ce qui touche `hmi::Document` et les widgets se
  fait sur le fil de la boucle (`App::run`).
- Les fils existants : la génération incrémentale de l'IHM (`app/hmi/HmiBuild` : un fil, `std::atomic`
  pour annuler, un `mutex` pour l'état), Chercher des fichiers (`app/FileFinder` : fil de priorité basse,
  drapeau d'arrêt), les imports (`ImportJob`), Modbus et le serveur web (`hmi/HmiModbus*`, `HmiWeb`), la
  visionneuse PDF (un fil pour PDFium), le simulateur.
- **Le retour au fil de l'interface : par interrogation à chaque image** — le fil écrit dans un état
  partagé protégé ; l'écran le lit dans `Update` (`MainAnalysisScreen::Update` appelle `tickHmiBuild`,
  `tickFileFinder`, `pollLisibleJobs`…). Pas de file de messages vers l'interface : c'est le modèle à
  suivre pour le réseau.
- Les tâches de fond se montrent dans la barre du haut (`app/BackgroundTasks.hpp`, `bgtasks::begin /
  progress / end`, avec une action d'annulation).

### 1.10 Le code brut d'un objet

- `hmi::serializeObjectCode(View, ids)` et `hmi::parseObjectCode(texte)` (`HmiStore.hpp`, l. 172 et
  182) : les lignes du fichier de la vue (propriétés, formules, actions, contenu, enfants) ;
  `hmi::edit::applyCode` / `pasteCode` (`HmiEdit.hpp`, l. 72) les appliquent (refus d'un autre genre). L'onglet
  « Code brut » de l'inspecteur (`HmiRawCodePanel`) en est l'écran. C'est un format **natif et validé** :
  la phase 7 peut s'en servir pour des objets riches (actions, contenu) que le JSON n'a pas à réinventer.

### 1.11 Générer et ses vérifications

- `hmi::generateWith(Project, NameExists, GenerateOptions)` et `hmi::compileWith`
  (`hmi/HmiCheck.hpp`) : vues invalides, noms en double, **variables inexistantes** (préfixe
  `unknownVariablePrefix()` : « variable inexistante dans l'IHM : » dans IHM Studio), ressources,
  expressions impossibles (avec le nom le plus proche). Chaque constat dit où (`Issue` : vue, objet,
  propriété). L'IA peut faire passer sa proposition, appliquée sur une copie, dans ces vérifications.

### 1.12 Le JSON dans le dépôt

- `sim/Json.hpp` (`parseJson`, `writeJson`) : lit et écrit, mais **dans les valeurs du dialecte ST**
  (`sim::ObjRef`, des `MAP[STRING] OF ANY` aux clés triées) — fait pour `JSON_LIRE` des scripts IHM, pas
  pour des fichiers de l'application. `hmi::checkJson` (`HmiExternal.hpp`) ne fait que valider.
- **Il faut donc un petit JSON à nous** (un arbre de valeurs, l'ordre des clés gardé, lire / écrire) :
  `app/ai/AiJson` (la maquette l'apporte ; le centre de mémoire s'en sert aussi).

### 1.13 Ce qui manque

- **Aucun client HTTPS** (vérifié : ni WinHTTP, ni libcurl, ni OpenSSL dans les sources ni le
  `CMakeLists.txt` ; les courriels partent en SMTP **sans TLS** vers le relais local, `HmiNotify.hpp`). Le
  serveur web de l'IHM (`HmiWeb`) est en HTTP sur le réseau local.
- Pas de stockage chiffré par l'utilisateur (DPAPI) ; `hmi/HmiCrypto` a SHA-256 et HMAC (empreintes de
  mots de passe), pas de chiffrement réversible.
- Pas de base SQLite.

---

## 2. Ce que ça veut dire pour l'IA (phases 3 à 9, décrites, pas codées)

### 2.1 Les modules de la demande, dans les conventions du dépôt

| Demande | Ici | Contenu |
|---|---|---|
| CAIManager | `app::ai::Assistant` (`app/ai/AiAssistant.*`) | la séance : la conversation courante, l'envoi, les états de génération, Arrêter, le mode, appliquer / refuser ; un `Provider` interchangeable (la démonstration aujourd'hui) |
| CAIHttpClient | `app::ai::HttpClient` (`app/ai/AiHttp.*`, phase 5) | HTTPS, streaming, délai, annulation, sur un fil de travail |
| CAIConversation | `app::ai::Conversation` + `ConversationStore` (`app/ai/AiStore.*`) | créer, charger, renommer, supprimer, reprendre |
| CAIContextBuilder | `app::ai::ContextBuilder` (`app/ai/AiContext.*`, phase 6) | le projet, la vue active, ses objets, la sélection, les variables, la bibliothèque |
| CAIObjectRegistry | `app::ai::KindRegistry` (`app/ai/AiRegistry.*`, phase 6) | les 116 genres, **tirés du code** : `kKindInfos`, `makeObject` (les clés et les valeurs par défaut de chaque genre), `roles::roleOf` (type, rôle), les familles (`kindIsSynoptic`…) |
| CAICommandParser / Validator / Executor | `app::ai::Operation`, `parseOperations`, `validate`, `toCommand` (`app/ai/AiOperations.*`) | le JSON des opérations → des opérations ; leur vérification sur le modèle ; leur exécution par `hmi::changeView` |
| CAIPreviewManager | `app::ai::previewView` + `AiPreviewDialog` | la copie de la vue modifiée, dessinée par les painters |
| CAIAttachmentManager | dans `AiStore` (`addAttachment`…) | copie dans `IA/pieces-jointes/<id>/`, type, taille |
| CAIPersistenceManager | `AiStore` + `AiJson` | les fichiers JSON du contrat |

Pas de hiérarchie de classes par genre : les opérations portent une clé de genre et des clés de propriétés.

### 2.2 Le format des opérations (sur les vraies structures)

Une réponse de l'IA porte (en plus de son texte) un bloc JSON :

```json
{"operations": [
  {"action": "creer", "genre": "Tank", "nom": "Cuve_1",
   "proprietes": {"x": "160", "y": "220", "w": "160", "h": "220", "label": "Cuve 1", "max": "100", "unit": "%"}},
  {"action": "modifier", "objet": "Cuve_2", "proprietes": {"fillColor": "#E5534B"}},
  {"action": "associer", "objet": "Pompe_1", "variable": "PUMP_01_RUN"},
  {"action": "aligner", "objets": ["Marche_1", "Marche_2", "Arret_1", "Arret_2"], "bord": "haut"},
  {"action": "supprimer", "objets": ["Vanne_1", "Vanne_2", "Vanne_3"]}
]}
```

- `genre` : une **clé de `kKindInfos`** (`kindFromKey`) ; `proprietes` : des **clés que `makeObject`
  donne à ce genre** (une clé inconnue de ce genre est une erreur : on n'invente pas de propriété) ; les
  valeurs en texte, comme `Prop::value`, vérifiées par leur rôle (`HmiRoles.hpp` : une couleur, un nombre,
  un booléen).
- `objet` : un nom d'objet **existant** dans la vue (ou un nom créé plus haut dans la même liste).
- `aligner` / `distribuer` : les gestes natifs `hmi::edit::align`, `distribute`.
- Pour des objets riches (actions, contenu d'un tableau), une opération `code` peut porter du **code
  brut** (`parseObjectCode` + `pasteCode`) : le format natif, déjà validé, plutôt qu'un second format.
- Jamais de code C++ ni de script exécuté : une opération inconnue est refusée.

La maquette écrit ce format dans ses conversations (le champ `operations` d'une proposition), et le relit.

### 2.3 La validation (les variables ne s'inventent jamais)

- Genre connu (`kindFromKey`), propriétés de ce genre (`makeObject`), valeurs lisibles selon leur rôle,
  objets cités existants, calque actif, coordonnées dans la vue.
- **Variables** : une variable citée doit exister — `programs.variables` (IHM), et les noms de
  l'automate relié (`NameExists`, préfixe `API.`). Sinon : une **erreur** qui bloque Appliquer, avec les
  noms existants les plus proches et « Créer la variable… » comme choix explicite de l'utilisateur.
- Enfin, la proposition appliquée **sur une copie** passe dans `hmi::generateWith` : les constats
  nouveaux (une expression impossible, un nom en double) s'affichent comme avertissements.

### 2.4 L'exécution par les mécanismes existants

Toutes les opérations d'une proposition dans **une** fonction `hmi::changeView` (créer avec
`makeObject` / `edit::add`, modifier `Object::set`, `edit::align`, `edit::remove`) → **une commande, un
Ctrl+Z** ; si elle crée une variable (choisi par l'utilisateur), `beginGroup / endGroup` autour de la
`changeProject` de la variable et de la `changeView` : toujours un seul pas. `App::apply` l'empile ;
l'historique la montre sous « Assistant IA : … ».

### 2.5 L'aperçu : copie temporaire ou transaction ? — **la copie**

- **Copie** : `hmi::View` et `hmi::Project` sont des valeurs (copiables) ; on applique la même fonction
  que `toCommand` sur une copie de la vue (et du projet pour les identifiants), on la dessine avec
  `paintHmiView`, et on entoure les objets nouveaux, modifiés, à supprimer. Rien ne touche le document :
  aucun signal `changed`, aucun risque de laisser le projet à moitié modifié, l'éditeur ouvert ne bouge pas.
- Transaction (appliquer puis annuler) : déclencherait les signaux, la remise en page de l'éditeur, les
  marques de suivi, et laisserait un état faux si l'utilisateur fait autre chose pendant l'aperçu.
- **Recommandé : la copie.** Appliquer refait la même fonction sur le vrai projet — d'où la même image.

### 2.6 Le threading du réseau

Un `Provider` (interface) : `start(requête)`, `poll()` (le texte reçu depuis la dernière fois, l'état),
`cancel()`. Le fournisseur réseau tourne sur **un fil de travail** (comme `HmiBuild` / `FileFinder`) :
il lit le flux HTTPS, pousse les morceaux dans un tampon protégé ; l'écran fait `poll()` à chaque image
(`MainAnalysisScreen::Update`), comme pour la génération de l'IHM. Arrêter pose le drapeau ; le fil ferme
la connexion. **Le modèle n'est modifié que sur le fil de l'interface**, après validation.
La démonstration de la maquette a la même interface, sans fil (une horloge simulée), pour que les essais
et les séances de script soient reproductibles.

### 2.7 HTTPS : ce que ça coûte

- **Windows : WinHTTP** (`winhttp.dll`, livrée avec Windows, TLS du système, proxies et certificats de
  l'entreprise) : aucune DLL à livrer, `-lwinhttp` sous MinGW (en-têtes présents dans mingw-w64). C'est le
  choix naturel pour l'exe croisé.
- **Linux : libcurl** chargée à la demande (`dlopen`, comme libpcap au lot 15) : aucune dépendance à la
  compilation ; sans libcurl, l'Assistant le dit (« pas de connexion possible sur ce poste »).
- Une petite interface commune (`HttpClient` : POST JSON, flux d'événements serveur, délai, annulation).
  Pas d'OpenSSL embarqué (taille, mises à jour de sécurité).

### 2.8 La persistance : JSON dans `IA/` ou SQLite ? — **JSON**

- Le volume est faible (des dizaines à quelques milliers de messages), l'accès est par conversation
  entière ; **un fichier JSON par conversation** s'écrit atomiquement (`core::writeFileAtomic`, `core/AtomicFile.hpp` : à côté puis
  renommé), se lit à la main, se mesure et se supprime fichier par fichier (ce dont le centre de mémoire
  a besoin), et ne demande aucune bibliothèque. Un champ inconnu est gardé (`AiJson` garde l'ordre
  et les clés) ; `"format": 1` permet les migrations.
- SQLite ajouterait une dépendance (deux plates-formes, l'exe croisé), un fichier opaque et des
  transactions dont on n'a pas besoin ici. À reconsidérer seulement pour un index de recherche plein
  texte sur de très gros historiques (`IA/index/`, régénérable).
- Le projet reste la référence pour les objets : une conversation garde le nom du projet, son chemin et
  la vue, jamais une copie du projet.

### 2.9 La clé API

- Jamais dans l'exécutable ni en clair sur le disque. **Windows : DPAPI** (`CryptProtectData`, liée à la
  session de l'utilisateur) → `IA/cle.bin`. Linux : le trousseau (libsecret par `dlopen`), sinon la clé
  n'est gardée que pour la séance.
- Pour un déploiement en entreprise, le mieux est **un relais** (un serveur de l'entreprise qui détient
  la clé et à qui le logiciel parle) : la clé ne quitte jamais le serveur ; le réglage « adresse de
  connexion » le permet.
- Dans la maquette : la clé est gardée **en mémoire seulement**, l'écran le dit, et un essai vérifie
  qu'elle n'est jamais écrite.

### 2.10 La confidentialité et le hors-ligne

- Ce qui partirait chez le fournisseur : la demande, l'historique de la conversation, et selon les
  réglages (chacun désactivable) la vue active (ses objets et leurs propriétés), la sélection, la liste
  des variables (noms, types — jamais leurs valeurs en marche), les pièces jointes. Le panneau dira
  « ce qui sera envoyé » avant le premier envoi d'une conversation.
- Hors ligne (ou sans fournisseur) : l'Assistant reste ouvrable, les conversations se relisent, les
  propositions déjà reçues s'appliquent (si la vue n'a pas changé) ; l'envoi dit pourquoi il ne part pas.

### 2.11 Risques et limites (francs)

- **La qualité des propositions dépend du modèle** : il se trompera (coordonnées absurdes, genre mal
  choisi). D'où la validation stricte, l'aperçu et le Ctrl+Z — l'IA ne doit jamais pouvoir modifier le
  projet sans passer par eux.
- **Le contexte coûte** : une vue de 500 objets à 30 propriétés, c'est trop pour chaque requête ; il
  faudra résumer (les clés non par défaut seulement, les objets proches de la sélection).
- **Les références spatiales** (« celle de droite ») et les fautes se résolvent côté modèle d'IA, mais la
  désignation finale doit être un nom d'objet existant, vérifié.
- **Coût et disponibilité** d'un service en ligne ; données d'un client qui partent hors du site : à
  décider avec lui (le réglage de confidentialité et le relais d'entreprise y répondent en partie).
- La maquette ne comprend pas le langage : elle reconnaît des phrases (mots-clés). Elle le dit.
- Une proposition d'une ancienne conversation : l'**empreinte** de la vue (calculée sur ses objets) est
  gardée avec elle ; si la vue a changé, elle n'est jamais appliquée (on propose de la redemander).

### 2.12 Estimation de l'effort (après validation de la maquette)

| Phase | Contenu | Effort |
|---|---|---|
| 3 Architecture IA | interfaces `Provider`, `HttpClient`, `ContextBuilder`, découpage définitif | 2 à 3 j |
| 4 Conversations et persistance | déjà réel dans la maquette ; migrations, rétention, export | 2 j |
| 5 Communication IA | WinHTTP + libcurl, flux, délais, annulation, erreurs réseau, DPAPI | 6 à 8 j |
| 6 Contexte et registre | registre des 116 genres tiré du code, contexte résumé, variables | 5 à 7 j |
| 7 Création et modification | toutes les opérations, code brut, symboles, styles, validation par rôle | 8 à 10 j |
| 8 Prévisualisation | déjà dans la maquette ; vues longues, symboles, différences fines | 3 j |
| 9 Essais | essais de bout en bout avec un fournisseur simulé, essais réseau | 4 à 5 j |

Soit **6 à 8 semaines** pour un copilote réellement utilisable, hors réglage du dialogue avec le modèle.

---

## 3. Le plan de la maquette (phase 2)

### 3.1 Où vit le panneau, et pourquoi

**Un panneau latéral à droite de la fenêtre**, quatrième volet du séparateur `analysis.upper` (après la
colonne de l'éditeur). Pourquoi pas ailleurs :

- pas dans l'inspecteur de l'éditeur (un onglet de plus à côté de Propriétés) : l'Assistant doit voir la
  vue **et** l'inspecteur en même temps, et rester ouvert quand on change d'onglet de vue ;
- pas en tiroir par-dessus (comme l'historique) : il cacherait la vue qu'il modifie ;
- en volet du séparateur, il **pousse** l'éditeur, se tire comme les autres, se replie, se retient, et
  entre dans Affichage › Panneaux et dans la Disposition comme les autres panneaux.

L'ouvrir : **Ctrl+Maj+I** (« I » comme IA ; libre), Affichage › Panneaux › « Assistant IA », **Projet ›
Disposition** (la ligne « Assistant IA » de « La fenêtre »), **Ctrl+K** « Assistant IA » (Aller à / Faire).
IHM Studio seulement (`core::hasIhm()`).

### 3.2 Ses parties (de haut en bas)

1. **L'en-tête** (comme celui de l'historique) : l'icône de l'Assistant (dessinée en primitives, comme
   `ui/Icons`), « Assistant IA », la pastille d'état « Démonstration – aucune IA connectée », les boutons
   Nouvelle conversation, Paramètres, Replier ; dessous, le mode **Prévisualisation | Direct** et la vue
   visée (« Vue : Vue_Station »).
2. **Les conversations récentes** (une liste repliable) : le titre, la date, « démo » ; clic : choisir ;
   double-clic ou F2 : renommer en place ; clic droit : Renommer, Supprimer… (avec confirmation).
3. **La discussion** (défile seule) : bulles utilisateur et Assistant, horodatage, mise en forme simple
   (gras, listes, `code`, titres courts), la carte **Opérations proposées** (les comptes, un aperçu
   miniature dessiné par les painters, Prévisualiser… / Appliquer / Refuser, puis « appliquée » ou
   « refusée »), l'indicateur de génération (en attente, réflexion, écriture mot à mot) et **Arrêter**.
4. **Les pièces jointes** en attente d'envoi (icône du type, nom, taille, progression, retirer).
5. **La saisie** (toujours visible, en bas) : un champ multiligne, Joindre, Envoyer ; **Entrée envoie,
   Maj+Entrée passe à la ligne** (le logiciel n'a pas d'autre convention pour un champ de message ;
   Ctrl+Entrée envoie aussi) ; des **suggestions** cliquables quand la conversation est vide.

Les fenêtres : **Prévisualisation des modifications** (comptes, objets, propriétés avant → après,
variables, avertissements et erreurs, l'aperçu en grand ; Appliquer, Annuler, Modifier la demande) et
**Paramètres de l'Assistant IA**, des fenêtres modales du logiciel (déplaçables, redimensionnables).

### 3.3 Les données de démonstration

- Six demandes reconnues : le synoptique de station de pompage (3 cuves, 3 bargraphes de niveau,
  3 vannes, 3 tuyauteries, 6 pompes, 12 boutons à impulsion, un titre : 31 vrais objets), « Ajoute une
  cuve avec un indicateur de niveau », « Mets celle de droite en rouge » (la sélection), « Aligne les
  quatre boutons », « Associe cette pompe à la variable PUMP_01_RUN » (variable introuvable : pas
  inventée, choisir une existante ou la créer), « Supprime les vannes » (confirmation). Toute autre
  demande : une réponse honnête et les suggestions.
- Deux conversations « démo » à la première ouverture (marquées « démo »), dont une avec une
  proposition sur une vue qui n'existe plus (pour montrer le refus d'une proposition périmée).
- Les pièces jointes : PNG, JPG, PDF, TXT, CSV, XLSX, XML, JSON, `.xihm`, `.xapi`, le projet ; copiées
  dans `IA/pieces-jointes/<id>/` ; un autre format est refusé (et le dit).

---

## 4. Ce que la maquette contient (après l'audit)

| Fichier (`src/app/ai/`) | Rôle (la demande) |
|---|---|
| `AiJson` | le petit JSON (ordre et champs inconnus gardés) |
| `AiOperations` | les opérations, leur validation, la commande unique, l'aperçu sur copie, l'empreinte (CAICommandParser / Validator / Executor / PreviewManager) |
| `AiStore` | conversations, paramètres, pièces jointes, activité (CAIConversation, CAIAttachmentManager, CAIPersistenceManager) |
| `AiDemo` | les réponses de démonstration (le futur fournisseur réseau prendra sa place) |
| `AiAssistant` | la séance : envoi, états de génération, Arrêter, mode, appliquer / refuser (CAIManager) |
| `AiPanel` | le panneau (en-tête, conversations, discussion, cartes, pièces, saisie) |
| `AiPreviewDialog`, `AiSettingsDialog` | les fenêtres de prévisualisation et des paramètres |
| `AiWorkspace` | le branchement dans l'écran du projet (le volet, Ctrl+Maj+I, la Disposition, Aller à) |

Hors de `src/app/ai/` : l'icône `ui::Icon::Assistant` (Icons), la ligne « assistant » de la Disposition, la touche
(help/Shortcuts, HistoryWorkspace), l'entrée Affichage › Assistant IA (TopBar), Aller à / Faire (GoToWorkspace), la
racine `IA/` (App.cpp), la commande de séance `ia` (ScriptRunner). Essais : `tests/ai_assistant_test.cpp`.

Le centre « Mémoire et apprentissage de l'IA » (section 12 de la demande) s'y ajoute : `AiMemory`, `AiMemoryJson`,
`AiKnowledge`, `AiMemoryDialog`, la commande `ia.memoire` et les commandes de séance `memoire-…`. Essais :
`tests/ai_memoire_test.cpp`. Le détail est dans `docs/LIVRAISON-1.14.2.md`.
