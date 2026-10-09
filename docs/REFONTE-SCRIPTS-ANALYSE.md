# Refonte des scripts et des fonctions : l'analyse du dépôt (§ 26)

> **Ce document répond au § 26 de ta demande** : avant toute modification, il fait l'état des lieux du logiciel existant. On y trouve :
> - l'architecture actuelle ;
> - les modèles ;
> - la compilation, la génération, l'import/export ;
> - les composants d'interface réutilisables ;
> - les incohérences et duplications ;
> - les risques de régression ;
> - la liste précise des fichiers ;
> - la charge relative (XS à XL, § 23, sans estimation en heures) ;
> - le plan de migration par lots.
>
> Les références `chemin:ligne` renvoient au code de la 1.11.16. Les points marqués « non vérifié » ou « à confirmer » le sont honnêtement. J'ai vérifié moi-même les constats principaux sur le code :
> - le trou de `renameFunctionEverywhere` ;
> - « Compiler (F7) » d'un éditeur qui compile tout le projet ;
> - les touches absentes de `ui::Key`.
>
> La **maquette interactive** de la cible est livrée à part : `docs/maquettes/maquette-refonte-scripts-fonctions.html`.

## Les décisions que je retiens par défaut (§ 11.3), sauf avis contraire de ta part

| # | Décision | Retenue |
|---|---|---|
| D1 | Comment le moteur lit les déclarations devenues des modèles | **Voie A** : le modèle est la source, et un texte équivalent est reconstruit pour le moteur. Le ST de l'automate n'est pas touché. |
| D2 | Fonctions internes `FUNCTION…END_FUNCTION` d'un script | Migrées en fonctions **privées** du script (aucune nouvelle collision de noms). |
| D3 | `VAR RETAIN` (sans effet aujourd'hui) | Stockage « Conservée » ; le rapport de migration le signale. « Persistante » seulement sur ton choix. |
| D4 | Fichiers `.xpgst` | Déclarations en en-tête `(*# … *)`, corps sans `VAR`. L'ancien format reste lu. |
| D5 | Raccourcis par défaut | Le profil actuel (ta table validée le 02/10) reste le défaut. Un profil « Visual Studio » est proposé à côté, et les conflits sont signalés. **À confirmer par toi.** |
| D6 | Récursion | Permise, profondeur bornée et réglable (32 par défaut, comme aujourd'hui). |
| D7 | Le mot « Générer » | « Générer » désigne la production. L'actuel IHM › Générer (contrôle de cohérence) deviendrait « Vérifier la cohérence ». **À confirmer par toi** (lot 13, rien ne change avant). |
| D8 | Retour d'une fonction | `RETURN valeur;` **et** `Nom := valeur;` (les deux sont déjà compris). |
| D9 | `XpgAnalyzer.vcxproj`, désynchronisé depuis la 1.11.3 | Resynchronisé au lot 0 (la liste des fichiers), pour que la construction Visual Studio refonctionne. |
| D10 | Nom des scripts de vue | Dérivé de la vue et de l'événement (aujourd'hui figé à la création, faux après une duplication). |
| D11 | Visibilité par défaut | **Nouvelles** déclarations : Publique (ta demande). Déclarations **migrées** : Privée (le comportement actuel), à publier depuis le rapport. |
| D12 | Le « format 2 » de la maquette | C'est le **format IHM 23** du dépôt (un seul compteur pour tout le dossier `ihm/`). |

---


> Analyse **en lecture seule** du dépôt `/home/user/XPGAnalyser` (version 1.11.16 en cours, commit `a05dcba`).
> Réponse au § 26 de la spécification (`spec_refonte_scripts.txt`) : architecture, modèles, compilation,
> génération, import/export, composants réutilisables, incohérences, risques, fichiers à modifier,
> charge relative (§ 23) et plan de migration.
>
> Convention : `chemin:ligne` renvoie au fichier source du dépôt au moment de l'analyse. Quand un point
> n'a pas pu être vérifié avec certitude, c'est dit explicitement (« non vérifié », « à confirmer »).

## Sommaire

0. Synthèse en dix constats
1. Architecture actuelle du système de scripts
2. Modèles existants (et format persistant réel)
3. Fonctionnement actuel de la compilation
4. Fonctionnement actuel de la génération
5. Fonctionnement actuel de l'import/export
6. Composants d'interface réutilisables
7. Incohérences et duplications
8. Risques de régression
9. Liste précise des fichiers à modifier
10. Charge relative par module (XS/S/M/L/XL, § 23 de la spécification)
11. Plan de migration par lots

---

## 0. Synthèse en dix constats

1. **Un seul moteur d'exécution, beaucoup de lecteurs.** Tout le ST de l'application (sections de
   l'automate simulé, scripts et fonctions IHM, expressions *fx*, opérateurs, macros d'édition,
   conditions de points d'arrêt) passe par **un seul analyseur/interpréteur** : `sim::parse` /
   `sim::execute` (`src/sim/Interpreter.cpp`, 3 624 lignes), avec une option `ParseOptions{hmiDialect}`
   pour le « dialecte IHM » (1.10). Mais **au moins neuf lectures lexicales indépendantes** du même
   texte coexistent (voir § 7.1).
2. **Les déclarations sont du texte.** Paramètres (`VAR_INPUT`), variables (`VAR`, `VAR_TEMP`),
   `VAR_IN_OUT`/`VAR_OUTPUT` (dialecte riche) vivent **dans le corps** (`Script::body`,
   `HmiFunction::body`, `FunctionOverride::body`, `HmiOperator::body`). Elles sont relues par
   **trois analyseurs de déclarations** distincts : `hmi::splitDeclarations` (texte, `HmiScript.cpp:434`),
   `sim::Parser::varBlock`/`functionDeclaration` (`Interpreter.cpp:1145` et `:1175`) et
   `hmitree::withoutDeclarations` (arbre et import, `app/hmi/HmiTreeData.hpp:522`).
3. **Pas de constantes de script, pas de références externes, pas de fonctions de script.**
   `VAR CONSTANT` est accepté par les deux analyseurs mais n'est exposé à personne (pas de nom
   qualifié). Les fonctions sont soit **globales au projet** (`Programs::functions`), soit **portées
   par un symbole** (`View::functions`, 1.11.10) ; un script n'en déclare qu'en **fonctions
   internes** `FUNCTION … END_FUNCTION` (dialecte 1.10, invisibles des autres scripts).
4. **La résolution des noms est par nom court, sans casse, globale** (`Project::functionByName`,
   `HmiModel.hpp:1733` ; `Env::call`, `HmiRuntime.cpp:373`). Seules les **fonctions de symbole**
   ont déjà un **nom qualifié** `Vue.Instance.Fonction` (réécriture textuelle à l'expansion,
   `qualifySymbolCalls`, `HmiSymbols.hpp`) — c'est le meilleur point de départ pour le § 8 de la
   spécification.
5. **Surcharge : rien pour les fonctions**, mais un **moteur de choix par types** existe pour les
   **opérateurs** (`findOperator` : exact puis promotion numérique, `HmiOperators.hpp:101`) et une
   **redéfinition virtuelle** par instance (`FunctionOverride`, `SUPER.`) depuis 1.11.10. Ce n'est
   pas de la surcharge (même signature imposée), mais l'infrastructure « plusieurs corps, un nom,
   choix du bon » est là.
6. **La compilation incrémentale existe (1.11.13)** : éléments à clé stable (`"fonction:152"`),
   empreintes contenu/configuration/interface, graphe de dépendances interface/contenu, cache
   `.xpg/build/build-cache.txt`, états, diagnostics par élément (`src/hmi/HmiPipeline.*`). **Mais**
   l'interface d'une fonction est calculée **en relisant le texte du bloc `VAR_INPUT`**
   (`signatureOf`, `HmiPipeline.cpp:371`) et les dépendances par **recherche de noms**
   (`identifiers` + `resolveCode`, `:300`/`:390`).
7. **Le bouton « Compiler (F7) » des éditeurs compile tout le projet** : `HmiScriptsPane::compileHere`
   appelle `hmi::compileWith(projet entier)` (`HmiScriptPanes.cpp:964`) ; celui des fonctions ouvre
   **IHM › Compiler** (rapport complet, `HmiFunctionPanes.cpp:267-271` → `HmiWorkspace.cpp:1090`).
   Les boutons « Générer / Générer et compiler » de la même barre, eux, sont déjà **ciblés sur
   l'élément choisi** (clé de build). Le § 13 de la spécification est donc surtout du **recâblage**.
8. **Import/export : un socle transactionnel solide**, par paquet zip (`.xpgvues`, `.xpgsymboles`,
   `.xpgtypes`, `.xpgfonctions`, `.xpgscripts` ; `HmiPackage.hpp`) avec **plan avant action**
   (absent / identique / différent → ajouter / garder / renommer / remplacer / ignorer), **une seule
   commande annulable**, refus d'un format plus récent. Il manque : identifiants stables dans la
   comparaison (tout se compare **par nom**), somme de contrôle, dépendances manquantes listées
   comme telles, rapport persistant.
9. **Presse-papiers : déjà le presse-papiers système** (SDL, `App.cpp:497-507`) ; l'éditeur de code
   colle en retirant les `\r` (`Controls.cpp:2312-2314`). Le collage Excel→grille est un moteur
   générique réutilisable (`app/TablePaste.*`). L'éditeur de vues pratique déjà le **double format**
   demandé au § 15 : objets riches dans un presse-papiers **interne au processus** + liste TSV
   lisible dans celui du système, et à Ctrl+V il compare les deux pour savoir lequel coller
   (`HmiCanvas.cpp:34-50`, `:235-300`). Il manque le même mécanisme pour les éléments de script
   (et entre deux instances de l'application), et le comportement « copier la ligne » sans
   sélection (aujourd'hui : **tout le texte**, `Controls.cpp:1349`).
10. **Raccourcis : une table de documentation centrale mais un traitement éparpillé.**
    `help::keys` (`src/help/Shortcuts.*`) décrit 61 raccourcis ; **environ 400 comparaisons de
    touches** (`key == …`, `case Key::…`) réparties dans **76 fichiers** les traitent. L'énumération `ui::Key` (`platform/InputEvent.hpp:17`) ne
    connaît **ni B, G, M, T, U, E, R, F4, F6, `-`, `,`, `.`** : `Ctrl+B`, `Ctrl+M,Ctrl+M`, `Ctrl+G`,
    `Ctrl+T`, `Ctrl+-`, `Ctrl+.`, `F6` sont **impossibles sans toucher la couche plateforme**.
    Plusieurs touches demandées sont **déjà prises** : `F12` (capture d'écran), `Ctrl+H` (historique),
    `Ctrl+K` (Aller à…), `F7` (Compiler), `F8` (démarrer/arrêter l'IHM), `Ctrl+J` (panneau du bas).

---

## 1. Architecture actuelle du système de scripts

### 1.1 Bibliothèques et sens des dépendances (CMake)

| Cible | Dossiers (glob `xpg_sources`, `CMakeLists.txt:65-76`) | Rôle pour les scripts |
|---|---|---|
| `xpg_core` | `src/core` | `Command`/`CommandStack`/`GroupCommand` (annulation), `ActionRegistry` (`core/Command.hpp:331-362`), `AtomicFile` (écriture atomique), `Signal`. |
| `xpg_xls` | `src/xls` | Lecture `.xlsx/.xlsm` (miniz). |
| `xpg_import` | `src/domain src/import src/export src/project src/sim` | **Interpréteur ST** (`sim/`), modèle automate (`domain/`), renommage automate (`project/RenamePlan.*`), macros ST (`project/Macro.*`). |
| `xpg_ui` | `src/ui src/menu` | Widgets : éditeur de code `ui::MultiLineText`, `TableView`, `TreeView`, `PropertyGrid`, `TabControl`, coloration `ui/Syntax.*`. |
| `xpg_help` | `src/help` | Table des raccourcis `help::keys`, centre d'aide. |
| `xpg_hmi` | `src/hmi` | **Modèle IHM**, stockage, vérifications, **pipeline de build**, moteur d'exécution IHM. |
| `xpg_analyzer` | `src/app` | Écrans, volets (`src/app/hmi/*`), arbre du projet (`app/ViewModels.cpp`). |

Sens : `app → hmi → import(sim, project, domain) → core`. **Le module IHM ne lit pas le projet
automate** : l'application lui passe des fonctions (`NameExists`, `exprcheck::PlcPaths`,
`pipeline::ApiInfo`). Toute nouvelle brique « script » de bas niveau (registre de types, résolveur)
doit vivre dans `src/hmi` (ou `src/sim` pour ce qui touche l'analyseur) pour rester testable sans écran.

> Les sources sont ramassées **par glob** : un nouveau `.cpp` dans `src/hmi/` est compilé sans
> toucher `CMakeLists.txt`. En revanche `XpgAnalyzer.vcxproj` **liste les fichiers à la main** (393
> `ClCompile`, alors que `src/` compte 413 `.cpp`) et **ne contient pas** `HmiPipeline.cpp`,
> `HmiScriptFile.cpp`, `HmiRetain.cpp`… : ce projet MSBuild (appelé par `outils/build.bat` →
> `outils/lib/build.ps1`) est **désynchronisé** depuis au moins la 1.11.3 (voir § 7). Les
> installateurs récents semblent produits par la chaîne CMake (`build-mingw/`). *À confirmer avec
> l'auteur avant d'ajouter des fichiers.*

### 1.2 Les « scripts » qui existent aujourd'hui (inventaire des contextes)

| # | Contexte | Où dans le modèle | Langage | Déclarations | Exécution |
|---|---|---|---|---|---|
| 1 | Script **général** (Programmation générale) | `Programs::scripts` (`HmiModel.hpp:932`), `Script` (`:619`) | ST, C, C++ (`ScriptLang`, `:608`) | `VAR`/`VAR_TEMP` dans `body` ; fonctions internes `FUNCTION…END_FUNCTION` (1.10) | Événement `Demarrage`/`Cyclique`/`Changement`/`Appel` (`kGeneralEvents`, `:632`) ; C/C++ **jamais exécutés** (accolades vérifiées seulement, `HmiScript.cpp:27`) |
| 2 | Script **de vue** (vue, popup, symbole, écran modèle, en-tête, pied) | `View::scripts` (`HmiModel.hpp:690`) | idem | idem | `OnOpen`/`OnCycle`/`OnClose` (`kViewEvents`, `:631`) ; ceux d'un symbole sont **recopiés et réécrits** dans chaque instance à l'expansion (`Expansion::scripts`, `HmiSymbols.hpp`) |
| 3 | **Fonction IHM** du projet | `Programs::functions`, `HmiFunction` (`:641`) | ST | `VAR_INPUT` (paramètres), `VAR`, `VAR_TEMP` ; `VAR_IN_OUT`/`VAR_OUTPUT`/types riches → dialecte | Appel par **nom court** (`Env::call`, `HmiRuntime.cpp:373`) ; deux chemins d'exécution (§ 1.4) |
| 4 | **Fonction de symbole** (1.11.10) | `View::functions` (`:728`) ; redéfinition `Object::functionOverrides` (`FunctionOverride`, `:513`) | ST | idem fonction IHM ; `isVirtual` | Appel qualifié `Vue.Instance.Fonction(` ; `SUPER.` ; `boundSymbolFunction` (`HmiSymbols.hpp`) |
| 5 | **Opérateur** d'un symbole ou d'un type IHM (1.10) | `View::operators`, `HmiType::operators`, `HmiOperator` (`:669`) | ST (opérandes `A`, `B`) | `VAR` dans le corps (relu par `splitDeclarations`, `HmiOperators.cpp:947`) | Résolu par types (`resolveOperator`) et exécuté comme `FUNCTION` du dialecte (`operatorFunctionText`) |
| 6 | **Action « Script ST »** (code en ligne) | `Action::value` quand `operation == RunScript` (`:453`) | ST | `VAR` possibles (mêmes règles qu'un script) | Au déclencheur de l'action |
| 7 | **Expressions *fx*** (propriétés, textes à trous `{…}`, conditions d'alarme, gardes/surveillances d'action) | `Prop::expr`, `Prop::value`, `AlarmDef::condition`, `Action::guard/watch` | expression ST (lecture seule) | aucune | `hmi::Expression::compile` → `sim::parse("__hmi_r := <expr>;")` (`HmiExpr.hpp:4-9`) |
| 8 | **Fonction interne** d'un script (1.10) | dans le texte de 1/2/6 | ST dialecte | paramètres entre parenthèses ou `VAR_INPUT`… | `sim::Program::functions` (`Interpreter.cpp:403`) — invisible hors du script |
| 9 | **Sections de l'automate simulé** | `domain::Project` | ST Control Expert | blocs de l'automate (pas des scripts IHM) | `sim::Runtime` — **hors périmètre** de la refonte, mais **même analyseur** |
| 10 | **Macros d'édition** | `project/Macro.*` | ST | — | `sim::execute` sur un `MacroEnvironment` (`project/Macro.hpp:1-45`) |

Les « contextes » demandés par la spécification (§ 1 : projet, programme, module, symbole, instance,
objet graphique, équipement, bibliothèque, utilisateur, système) se ramènent aujourd'hui à **trois
propriétaires réels** : le **projet** (`Programs`), une **vue/symbole** (`View`) et une **instance**
(`Object` de genre `SymbolInstance`, via `functionOverrides`). « Équipement », « bibliothèque »,
« utilisateur », « système » n'ont **aucun script** aujourd'hui (les équipements ont des
*comportements* simulés, pas du code ; « Ma bibliothèque » ne contient que des modèles de vues).

### 1.3 Langages et moteurs : combien, et que partagent-ils ?

- **Un seul langage exécuté** : le ST, en deux variantes du **même** analyseur `sim::Parser`
  (`Interpreter.cpp`, `class Lexer` ligne 78) :
  - ST de l'automate (`sim::parse(source, nom)`) ;
  - **dialecte IHM** (`sim::parse(source, nom, ParseOptions{true})`, `Interpreter.hpp:141-152`) :
    fonctions internes, `RETURN expr`, `VAR_IN_OUT`, `REF_TO`, `POINTER TO`, `ADR`, `^`, tableaux à
    N dimensions, `MAP`, `FOR EACH`, `NULL`, `+=`. Activé par `hmi::dialectOptions()`
    (`HmiScript.hpp:112`).
- **C et C++** : édités, coloriés, **vérifiés pour l'équilibre des délimiteurs** (`checkC`,
  `HmiScript.cpp:27`), jamais compilés ni exécutés (message d'information explicite, `:201`).
- **Expressions *fx*** : pas un langage à part — le ST du simulateur enveloppé
  (`HmiExpr.hpp:4-9`), avec un contrôle sémantique propre (`hmi::exprcheck`, `HmiExprCheck.*`).
- **Système de types** : `sim::Value`/`sim::Type` à l'exécution (`sim/Value.hpp`) ; `TypeRef`
  du dialecte (`Interpreter.cpp`) ; côté IHM, des **types écrits en texte** (`"REAL"`,
  `"ARRAY[1..3] OF T_Vanne"`, `"T_Four"`) relus par `hmi::types::parseSpec` (`HmiTypes.hpp`) et par
  le dialecte (`parseTypeText`). **Pas de registre central, pas d'identifiant de type** : un type IHM
  (`HmiType`, `HmiModel.hpp:920`) a un `Id`, mais **toutes les références à un type sont son nom**.

Conclusion : lexer et parser **d'exécution** partagés ; **analyse sémantique, aide à la saisie,
renommage, dépendances : chacun relit le texte à sa façon** (§ 7.1).

### 1.4 Chaîne d'exécution d'un script et d'une fonction (moteur IHM)

1. **Préparation** `Runtime::prepare` (`HmiRuntime.cpp:1003`) : `markers::strip` (repères `$…$`),
   puis `splitDeclarations` (texte) pour extraire les locales, **puis** `sim::parse` :
   - pour un **script** : sur le texte **avec** ses blocs `VAR` (le dialecte les tient lui-même,
     `RunLimits::locals`, `Interpreter.hpp:214-218`) ;
   - pour une **fonction** : sur le corps **blanchi** (les déclarations remplacées par des espaces,
     lignes conservées) ; les paramètres et locales sont portés par un cadre du moteur IHM.
   Le résultat est **mis en cache par texte** (`programs_`, clé = texte, préfixe `\x01` pour une fonction).
2. **Script** `Runtime::runStatements` (`:1034`) : locales `VAR` gardées d'une exécution à l'autre
   dans `scriptLocals_["s"+id]`, `VAR_TEMP` réinitialisées ; budgets d'instructions.
3. **Fonction** : appel par `Env::call` (`:373`) → `IHM_*` d'abord, puis **fonction IHM par nom
   court** (`functionByName`), puis **fonction de symbole** si le nom contient un point
   (`symbolCall` → `boundSymbolFunction`), puis l'automate, puis les fonctions standard.
   **Deux chemins** : une fonction « simple » passe par `Runtime::callFunction` (`:1105`, paramètres
   = `LocalVar::Section::Input` dans l'ordre ou par nom) ; une fonction « riche » est fournie à
   l'interpréteur comme `sim::Function` par `Env::dialectFunction` (`:477`), relue par
   `sim::parseFunction(functionText(f))` et exécutée dans le dialecte (`functionIsSimple` tranche).
4. **Profondeur** : `kMaxDepth = 32` côté IHM (« appel circulaire », `HmiRuntime.cpp:34`, `:1130`),
   `RunLimits::maxCallDepth{64}` côté dialecte (`Interpreter.hpp:220`) — deux protections différentes
   pour les deux chemins.

### 1.5 Où le texte d'un script est lu (cartographie des appelants)

`splitDeclarations` est appelé en **22 endroits hors de `HmiScript.cpp`** (et 5 fois dedans : `checkScript`,
`scriptNames`, `scriptCallees`, `functionSignature`, `checkFunction`) : moteur (`HmiRuntime.cpp:1012`),
contrôles (`HmiScriptCheck.cpp:274/278/943/947`, `HmiScriptCheck110.cpp:310/543`,
`HmiExprCheck.cpp:724`, `HmiOperators.cpp:947`, `HmiSymbols.cpp:1436`), interface
(`HmiFunctionPanes.cpp:99/481/631/806`, `HmiAssist.cpp:390/773/827/1910/2339/2486`,
`HmiWorkspace.cpp:2801`, `RenameDialog.cpp:668`). S'y ajoutent les lecteurs **qui ne passent pas par
`splitDeclarations`** : `hmitree::withoutDeclarations`/`signatureOf` (`HmiTreeData.hpp:516-594`,
appelés par l'arbre et `ImportWorkspace.cpp:135`/`:179`), `pipeline::signatureOf`
(`HmiPipeline.cpp:371`) et le dialecte lui-même. **C'est la surface exacte de la suppression des
blocs `VAR`** (§ 8, § 9).

### 1.6 Les blocs `VAR … END_VAR` : où ils sont, ce qu'on en fait, ce qui casserait

**Où un bloc de déclaration peut s'écrire aujourd'hui** : script général (`VAR`, `VAR_TEMP`),
script de vue/popup/symbole/modèle (idem, recopié dans chaque instance), action « Script ST »
(idem), fonction IHM et fonction de symbole (`VAR_INPUT`, `VAR`, `VAR_TEMP`, et dans le dialecte
`VAR_IN_OUT`, `VAR_OUTPUT`, `CONSTANT`), redéfinition d'instance (mêmes blocs que la fonction
redéfinie), opérateur (`VAR`, ses opérandes `A`/`B` étant implicites), fonction interne
`FUNCTION … END_FUNCTION` d'un script (paramètres dans l'en-tête et blocs). **Jamais** dans une
expression *fx* (lecture seule, ni `;` ni `:=`).

| Consommateur | Ce qu'il fait des blocs | Si les déclarations quittaient le texte **sans pont** |
|---|---|---|
| Moteur `Runtime::prepare` (`HmiRuntime.cpp:1012`) et dialecte (`Interpreter.cpp:2228-2246`) | locales d'un script gardées (`VAR`) ou remises (`VAR_TEMP`) ; paramètres d'une fonction = `VAR_INPUT` dans l'ordre, défauts | « variable inconnue » à l'exécution, arité fausse, état des compteurs perdu |
| `checkScript` / `checkFunction` (`HmiScript.cpp:210`, `:842`) | erreurs de déclaration, locale du nom de la fonction | fausses erreurs ou erreurs manquées |
| `scriptcheck` (`HmiScriptCheck.cpp:274-278`, `:943-947`) | locales connues ; **arité** d'un appel = entrées sans défaut | chaque locale « nom inconnu » ; appels refusés |
| `lang110::analyze` (`HmiScriptCheck110.cpp:310`, `:543`) | noms et fonctions du dialecte | constats du dialecte faux |
| `scriptNames` (IHM › Générer, « variable inexistante ») (`HmiScript.cpp:232`) | exclut les locales | fausses « variables inexistantes » |
| `functionSignature` (`HmiScript.cpp:795`), `hmitree::signatureOf` (`HmiTreeData.hpp:587`), `pipeline::functionIface` (`HmiPipeline.cpp:284`) | signature affichée (arbre, aide) ; **empreinte d'interface** | signatures vides ; interface constante → **les appelants ne sont plus recompilés** quand un paramètre change |
| `HmiExprCheck.cpp:724` | arité d'une fonction de symbole appelée dans une case *fx* | fausses erreurs d'arité |
| `boundSymbolFunction` (`HmiSymbols.cpp:1436`) | une locale ou un paramètre de la fonction **cache** le paramètre du symbole de même nom lors de la substitution | **substitution fausse à l'exécution** (un paramètre du symbole remplacerait une locale) |
| `operatorIssues` (`HmiOperators.cpp:947`) | erreurs de déclaration d'un opérateur | erreurs non signalées |
| `HmiAssist` (6 appels) | locales en tête de la complétion, types après `nom :`, paramètres dans la signature d'aide | complétion sans locales ni paramètres |
| `RenameDialog.cpp:668` | une locale homonyme **protège** le script du renommage d'une variable IHM | une locale homonyme serait renommée à tort |
| Essai d'une fonction (`HmiWorkspace.cpp:2801`, `HmiFunctionPanes.cpp:99/481/631/806`) | demande une valeur par paramètre | l'essai ne demande plus rien |
| `ImportWorkspace.cpp:135/179` (via `hmitree::withoutDeclarations`) | variables employées, locales exclues | locales comptées comme variables du projet |

D'où le principe du § 11 : **les modèles deviennent la source, et un texte équivalent est
reconstruit pour ces consommateurs** tant qu'ils n'ont pas été portés un à un sur le modèle.

---

## 2. Modèles existants

### 2.1 Structures du modèle IHM (`src/hmi/HmiModel.hpp`)

| Structure | Ligne | Champs utiles à la refonte | Remarques |
|---|---|---|---|
| `Id` / `kNoId` / `Project::allocate()` | 47 / 48 / 1711 | entier 32 bits, **jamais réutilisé** (`nextId`, persisté `prochain_id`) | Base saine pour des **identifiants stables** de constantes, variables, paramètres, références. |
| `Script` | 619 | `id, name, lang, event, body, periodMs, watch, description, folder` | Ni constantes, ni variables, ni fonctions, ni références, ni version, ni visibilité, ni date. |
| `HmiFunction` | 641 | `id, name, returnType` (texte ; **`""` = sans retour**), `body`, `description`, `isVirtual` | **Paramètres dans `body`** (`VAR_INPUT`). Le « pas de retour » est une **chaîne vide** — exactement ce que le § 6 de la spécification demande d'éviter. |
| `FunctionOverride` | 513 | `function` (nom), `body` | Redéfinition par instance ; **référence par nom** à la fonction du symbole. |
| `HmiOperator` | 669 | `id, op, left, right, result` (types en texte), `body`, `description` | Signature **structurée** (op/gauche/droite/résultat) : le seul code dont la signature n'est pas dans le corps. |
| `View` | 680 | `scripts`, `params` (`ViewParam`), `functions`, `operators`, `alarms`, `ownerSymbol`, `role` | Un **symbole** est une `View` de rôle `"symbole"` (`kSymbolRole`, `HmiSymbols.hpp`). |
| `ViewParam` | 564 | `name, defaultValue, description, type` (texte, vide = ANY), `mode` (`Reference/Copy/Both`) | **Le seul « paramètre » déjà modélisé hors du texte** (popups, symboles) : modèle à imiter pour `ParameterDefinition`. |
| `Object` | 519 | `functionOverrides`, `alarmOverrides`, `props`, `actions` | Une instance = objet `Kind::SymbolInstance`, son symbole **par nom** (`prop "symbol"`). |
| `Variable` (variable IHM) | 842 | `id, name, type` (texte), `initial`, `description`, `folder`, `retain` (1.11.16)… | Variables **globales** du projet, partagent l'espace de noms des fonctions (`uniqueFunctionName`, `:1783`). |
| `HmiType`, `TypeMember`, `HmiEnumValue`, `HmiTypeKind` | 920, 898, 912, 919 | structure ou énumération, `operators`, `folder` | `id` existe, **jamais utilisé comme référence**. |
| `Programs` | 932 | `scripts, variables, functions, types, folders` | Conteneur « Programmation générale ». |
| `Action` | 453 | `operation` (`RunScript` : code dans `value` ; `CallScript` : nom dans `target`), `guard`, `watch`, `params` | Script en ligne et appel de script **par nom**. |
| `Project` | 1680 | accès `script(Id)`, `function(Id)`, `functionByName`, `hmiTypeByName`, `variable(name)`, `viewOfScript` | Tous les « byName » sont **sans casse**. |

Types d'analyse (non persistés) : `hmi::LocalVar` / `ScriptParts` (`HmiScript.hpp:86-98` :
`name, type, initial, section{Var,Temp,Input}, line`), `sim::VarDecl` (`Interpreter.cpp:381` :
`mode{Input,InOut,Output,Local,Temp}, constant, initial` en expression), `sim::Function`
(`:393` : `params`, `locals`, `result`), `hmi::lang110::InnerFunction`/`Param`
(`HmiScriptCheck110.hpp`). **Trois représentations d'une même déclaration**, aucune persistée.

### 2.2 Diagnostics : sept structures

| Structure | Fichier | Champs |
|---|---|---|
| `sim::Diagnostic` | `sim/Interpreter.hpp:36` | `severity, message, line, section` |
| `hmi::ScriptDiagnostic` | `HmiScript.hpp:28` | `severity, line, message, column, length` |
| `hmi::scriptcheck::Finding` | `HmiScriptCheck.hpp:38` | `+ name, suggestion, fixLabel, fixLine, fixText` |
| `hmi::lang110::Finding` | `HmiScriptCheck110.hpp:59` | `line, column, length, message, error, fix…` |
| `hmi::exprcheck::Problem` | `HmiExprCheck.hpp:63` | (expressions) |
| `hmi::Issue` | `HmiCheck.hpp:31` | `category, view, object, property, message, script, line, item, column, length` — **pas de code, pas d'identifiant de fonction distinct de `item`** |
| `hmi::pipeline::Diagnostic` | `HmiPipeline.hpp:131` | `id, severity, code, message, category, element (clé), path, file, line, column, length, date, step, suggestion, view/object/script/item, property` — **le plus proche du § 16** |

La spécification (§ 16) demande code, niveau, message, script, fonction, ligne, colonne, longueur,
identifiant d'élément, suggestion, action rapide : `pipeline::Diagnostic` couvre tout sauf
« fonction » (distincte du script) et « action rapide » (qui existe, elle, dans
`scriptcheck::Finding::fix*`). **Il faut converger vers `pipeline::Diagnostic` enrichi**, pas créer un
huitième type.

### 2.3 Format persistant réel (format IHM 22, `kFormatVersion`, `HmiStore.hpp:47`)

Arborescence (`HmiStore.hpp:4-12`) : `ihm/ihm.txt` (index), `ihm/vues/NNNN-Nom.vue`,
`ihm/scripts/NNNN-Nom.st|.c|.cpp`, `ihm/fonctions/NNNN-Nom.st`, `ihm/ressources/`, `ihm/modeles/`,
`ihm/corbeille/`. Chaque fichier : écriture `.tmp` + renommage, index écrit **en dernier**, ligne
finale `fin` (fichier coupé refusé). Un outil plus ancien **refuse** un format plus récent
(`HmiStore.cpp:1161-1164`). Le format 22 n'est écrit que si nécessaire (sinon 21, `HmiStore.cpp:703`).

**Index** (`bac/Armoire_Gaz/ihm/ihm.txt`, réel — un projet au **format 19**, toujours relu tel quel) :

```text
# XpgAnalyzer - projet IHM (format 19)
ihm format=19 prochain_id=730
script id=154 nom="Statistiques_Pression" langage=ST evenement=Cyclique periode=1000 surveille="" fichier="scripts/0154-Statistiques_Pression.st" description="Mini et maxi de la pression A depuis le lancement" dossier="Calculs"
fonction id=152 nom="Moyenne_Pression" retour="REAL" fichier="fonctions/0152-Moyenne_Pression.st" description="Moyenne pondérée des pressions de deux armoires (bar)"
fonction id=153 nom="Tracer_Evenement" retour="" fichier="fonctions/0153-Tracer_Evenement.st" description="Une ligne au journal (alerte au-dessus du niveau 1) et le compte des clics"
```

(écrit par `HmiStore.cpp:808-826`, relu par `:1341-1364`).

**Corps d'une fonction** (`bac/Armoire_Gaz/ihm/fonctions/0152-Moyenne_Pression.st`, réel) :

```text
(* Moyenne pondérée des pressions de deux armoires, en bar *)
VAR_INPUT
    A : REAL;
    B : REAL;
    Poids_A : REAL := 0.5;   (* 0.5 : la moyenne simple *)
END_VAR
VAR_TEMP
    Somme : REAL;
END_VAR

Somme := A * Poids_A + B * (1.0 - Poids_A);
Moyenne_Pression := Somme;
```

**Corps d'un script** (`ihm/scripts/0154-Statistiques_Pression.st`, réel) : un bloc `VAR` (valeurs
**gardées** entre deux exécutions, avec commentaires en fin de ligne) et un bloc `VAR_TEMP` :

```text
VAR
    Mini : REAL := 1000.0;   (* VAR : gardée d'une exécution à l'autre *)
    Maxi : REAL;
    Tours : DINT;
    Ecart : REAL;
END_VAR
VAR_TEMP
    P : REAL;                (* VAR_TEMP : repart à chaque exécution *)
END_VAR
P := Armoires[0].ana.PT1.mes;
```

**Scripts de vue et fonctions de symbole : en ligne dans le fichier `.vue`** (corps échappé `\n`),
`HmiStore.cpp:349-352`, `:390-391`, `:522-538`, `:603-611`. La première ligne est réelle
(`bac/Armoire_Gaz/ihm/vues/0003-Vue_Armoire_A.vue:507`) ; les deux suivantes sont une **illustration
construite** d'après le code d'écriture (aucun projet du dépôt ne contient encore de fonction de
symbole ; `tools/sessions/preparer-projet-11110.cpp` en fabrique un) :

```text
script id=72 nom="Vue_Armoire_A_OnOpen" langage=ST evenement=OnOpen corps="Compteur_Clics := Compteur_Clics + 1;\nIHM_JOURNAL('Armoire A ouverte ({Compteur_Clics} fois)');\n"
fonction_symbole id=… nom="Ouvrir" retour="" virtuelle=1 description="…" corps="VAR_INPUT\n  …\nEND_VAR\n…"
redefinition fonction="Ouvrir" corps="…"
```

Conséquences pour la refonte :
- le **nom de fichier** d'une fonction ou d'un script contient l'`Id` (`scriptFileName`,
  `HmiStore.cpp:76`) : un renommage change le fichier (l'ancien part à la corbeille, `:1123`) ;
- **le format est par lignes `mot clé=valeur`** (`Record`, `parseRecord`, `HmiStore.hpp:112-117`) :
  ajouter des lignes `constante …`, `variable_script …`, `parametre_fonction …`, `reference …`
  sous un `script`/`fonction` est **naturel** dans ce format (comme `membre` sous `type_ihm`,
  `valeur_enum`, ou `redefinition` sous `objet`) ;
- un projet relu par une version plus ancienne qui **ignore** des lignes inconnues perdrait les
  déclarations au premier enregistrement : il faudra **monter `kFormatVersion` à 23** (refus par les
  anciennes versions) — c'est la politique déjà suivie à chaque lot (`HmiStore.hpp:47-86`).

### 2.4 Autres modèles réutilisables

- **Paramètres de symbole/popup** (`ViewParam`) avec **arguments d'instance** (`symbolArguments`,
  `givenArguments`, `withArgument`, `argumentLiteral` : conversion d'un texte vers un littéral ST
  typé, `HmiSymbols.hpp`) : exactement ce qu'il faut pour les « valeurs par défaut » typées.
- **Opérateurs** (`HmiOperator`) : signature structurée + choix par types (`findOperator`).
- **Énumérations IHM** (`HmiEnumValue`, `HmiEnums.hpp`) : `T_MODE#Auto`, conversions générées.
- **Variables IHM** (`Variable`) : dossiers, rétention (`retain`), types composés dépliés
  (`types::flatten`, `leafVariables`) — le modèle des « variables exposées » existe déjà côté projet.
- **Commandes annulables par instantané** (`hmi::changeProject`, `changeView`, `HmiCommands.hpp:138-140`)
  et **groupes** (`core::GroupCommand`, `CommandGroupScope`) : un import ou une migration = une
  commande.

---

## 3. Fonctionnement actuel de la compilation

### 3.1 Trois niveaux qui portent le même nom

| Niveau | Entrée | Ce qu'il fait | Où |
|---|---|---|---|
| **Syntaxe** (« se lit-il ? ») | un texte | `checkScript(lang, body)` : `splitDeclarations` puis `sim::parse(..., dialectOptions())` ; message anglais du parseur traduit (`frenchSimMessage`) ; C/C++ : délimiteurs. `checkFunction` : type de retour permis, `parseFunction` (dialecte riche), déclarations, affectation du nom de la fonction (« ne donne jamais sa valeur ») | `HmiScript.cpp:192`, `:813` |
| **Sémantique** (« peut-il marcher ? ») | un texte + une portée (`scriptcheck::Scope` : projet, vue, fonction, noms de l'automate) | noms inconnus avec « veux-tu dire », membres, indices, **arité** des appels, procédure appelée dans un calcul, écritures interdites, types « évidents » (texte↔nombre) ; constats du dialecte délégués à `lang110::analyze` (fonctions internes, `FOR EACH`, énumérations, arité des fonctions internes) | `HmiScriptCheck.cpp` (1 483 l.), `HmiScriptCheck110.cpp` (640 l.) |
| **Projet** (« IHM › Compiler ») | le projet | `compileWith(p, plc, paths, focus)` = `compileFocused` (syntaxe de chaque script/fonction/action) + `ExprWalker` (expressions) + `scriptcheck::projectIssues` + `operatorIssues` | `HmiCheck.cpp:3252` |

Le contrôle des **appels de fonctions du projet** ne vérifie que **le nombre d'arguments** (entrées
avec valeur initiale = facultatives) et l'emploi d'une procédure dans une expression
(`HmiScriptCheck.cpp:938-953`) : **les types des arguments ne sont pas comparés aux types des
paramètres**, sauf le cas « entier passé à un paramètre énumération » d'une fonction **interne**
(`HmiScriptCheck110.cpp:470-507`). Aucune détection de récursion statique ; à l'exécution, une
profondeur maximale (§ 1.4).

### 3.2 La compilation incrémentale (1.11.13) : ce qui existe vraiment

`hmi::pipeline` (`HmiPipeline.hpp/.cpp`, 2 370 lignes) :

- **Éléments** (`Element`, `HmiPipeline.hpp:95`) à **clé stable** `genre:id` (`keyOf`,
  `HmiPipeline.cpp:291`) : `fonction:152`, `fonction-symbole:31`, `script:154`, `script-vue:72`,
  `vue:3`, `animations:3`, `actions:3`, `type:615`… avec un **chemin logique**
  (`IHM/Programmation générale/Fonctions/Moyenne_Pression`).
- **Trois empreintes** par élément : contenu, configuration, **interface** ; une dépendance
  d'interface n'est refaite que si l'interface change (`DepMode::Interface/Content`,
  `HmiPipeline.hpp:87`). Pour une fonction : contenu = `retour=… virtuelle=…\n<corps>`
  (`functionCanon`, `:283`), interface = `Nom(<bloc VAR_INPUT normalisé>) : Retour`
  (`functionIface`, `:284` + `signatureOf`, `:371`).
- **Dépendances** par **recherche de noms** dans le texte (`identifiers`, `:390` ; `resolveCode`,
  `:300`) : variable IHM, fonction, type, variable de l'API, `Vue.Instance.Fonction`. Les locales
  sont exclues par **une liste de noms** passée à la main (paramètres de la vue, `a`/`b` d'un
  opérateur, le nom de la fonction) — **pas** les locales déclarées dans les blocs `VAR`.
- **Plan** (`plan`, `:1248`) : ce qui est périmé + **la fermeture transitive des dépendances
  périmées** ; `Request::explicitSelection` recompile l'élément choisi même à jour. → *« compiler
  cette fonction et les dépendances strictement nécessaires » existe déjà.*
- **Compilation d'un ensemble** (`compileElements`, `:1394`) : traduit les éléments en
  `CompileFocus` (`HmiCheck.hpp:94`) et appelle `compileWith(..., &focus)` ; les fonctions de
  symbole passent par `checkFunction` + `scriptcheck::check` avec la portée du symbole.
- **Cache** `.xpg/build/build-cache.txt` (écriture atomique, `.bak`, ligne `fin`), **états**
  (`State` : À jour, Modifié, Compilation requise, Échec…), **diagnostics par élément**
  (`CacheEntry::diagnostics`), **verrou** multi-instances (`acquireLock`).
- **Arrêt sur modification** pendant la simulation (`runPrints`/`runChange`, 1.11.15-16).

**Limites qui comptent pour la refonte**

1. **La phase E « Validation » tourne sur tout le projet à chaque build**, même ciblé
   (`HmiPipeline.cpp:1828-1925` : `generateWith(p, …)` sans filtre de portée ; les erreurs des
   autres éléments s'ajoutent à `rep.errors`). Un « Compiler la fonction » par le pipeline peut donc
   **échouer à cause d'un autre élément** et coûte une validation complète.
2. **Interface textuelle** : deux écritures équivalentes du bloc `VAR_INPUT` (commentaire, ordre des
   espaces) donnent la même empreinte, mais **un paramètre déplacé dans un modèle n'aurait plus
   d'interface** tant que `functionIface` n'est pas réécrit.
3. **Les éléments sont la vue, le script, la fonction** : pas de grain plus fin (une constante, une
   surcharge, un paramètre). Ajouter des éléments « déclaration » est possible (le cache est générique
   par clé) mais `ElementKind` est une énumération fermée (`HmiPipeline.hpp:58-76`) dont les clés
   texte sont écrites dans le cache (`kindKey`).
4. **Résolution par nom** : `Names` (`HmiPipeline.cpp:286`) indexe par **nom en minuscules** →
   collisions entre homonymes (deux fonctions de même nom dans deux scripts seraient indistinguables).

### 3.3 Les boutons « Compiler » aujourd'hui (§ 13 de la spécification)

| Endroit | Libellé / info-bulle | Ce qui est réellement compilé | Référence |
|---|---|---|---|
| Barre de l'éditeur de **scripts** (généraux et de vue) | « Compiler (F7) : les fautes de tous les scripts » | `compileHere()` = `hmi::compileWith(doc_->project, …)` **sur tout le projet**, filtré à l'affichage sur les constats de scripts ; **puis** un build pipeline `Mode::Compile` sur l'élément choisi | `HmiScriptPanes.cpp:340-342`, `:523-526`, `:964-993` |
| Barre de l'éditeur de **fonctions** (et des fonctions de symbole) | « Compiler (F7) : toutes les fonctions, scripts, expressions et actions » | diagnostics de la fonction affichée + build ciblé + **ouverture de IHM › Compiler** (rapport complet) | `HmiFunctionPanes.cpp:157`, `:267-271` ; `HmiWorkspace.cpp:1090` |
| **F7** dans l'éditeur de scripts | — | `compileHere()` (projet entier) | `HmiScriptPanes.cpp:1034-1039` |
| **F7** ailleurs | — | `openHmiPane("compiler")` (projet entier) | `HistoryWorkspace.cpp:325-329` (`MainAnalysisScreen::handleShortcut`) |
| Boutons **Générer / Régénérer / Générer et compiler** des mêmes barres | « … le script choisi » | **l'élément choisi** (clé `script:`/`script-vue:`/`fonction:`/`fonction-symbole:`) via `runHmiBuildFor` | `HmiScriptPanes.cpp:287-291`, `HmiBuildWorkspace.cpp:397-403` |
| Menu de l'arbre (Compiler un dossier / un élément) | — | portée = chemin ou clé (`inScope`) | `ViewModels.cpp:2668-2701` (cibles de build par nœud) |

Le document compilé est **le modèle en mémoire** (le build travaille sur une copie,
`HmiBuildWorkspace.cpp:49-51`), et la frappe est déjà une commande appliquée au modèle
(`HmiScriptPanes.cpp:573-592`) : « compiler la version affichée » est donc déjà vrai.

### 3.4 Pendant la frappe

`placedScriptDiagnostics` (`HmiScriptPanes.hpp:47`) recalcule les constats **du seul code
affiché** à chaque modification (squiggles `MultiLineText::Squiggle`) — « les mêmes que Compiler ».
C'est le meilleur candidat pour « Compiler le document actif » **sans** passer par la validation
globale.

---

## 4. Fonctionnement actuel de la génération

Le mot « générer » couvre aujourd'hui **trois choses différentes**, ce qui est une source de
confusion à lever dans la refonte (§ 7) :

1. **« IHM › Générer »** = **contrôle de cohérence** du projet (`hmi::generate`/`generateWith`,
   `HmiCheck.cpp:3311-3530`) : noms invalides ou en double, déclencheurs, `IHM_APPELER('X')`
   introuvable, vues introuvables, variables inexistantes (`scriptNames`), références circulaires,
   ressources, qualité, langues, plan Modbus… **Rien n'est écrit.**
2. **La phase « Génération » du pipeline** (`HmiPipeline.cpp:1668-1730`) : pour chaque élément
   périmé, `generationProblems` puis `generatedText` (`:1313`) — un **artefact texte**
   (`.xpg/build/ihm/NN-etape/<cle>-<nom>.txt`, `artifactPathOf`, `:1304`) : l'en-tête de l'artefact,
   ses dépendances, et pour une vue **ses instances de symboles déballées** (`expandInstances`).
   Chaque artefact est écrit **atomiquement** (`core::writeFileAtomic`, `.tmp` relu puis renommé,
   `core/AtomicFile.hpp`), le cache **en dernier**.
   **Les artefacts ne sont pas relus par le moteur** : la simulation exécute le modèle en mémoire
   (aucune lecture des artefacts dans `HmiRuntime.cpp` ni `HmiSimulation.cpp` ; l'écran ne s'en sert
   que pour montrer un chemin, `HmiBuildWorkspace.cpp:439`). *À confirmer* : la spécification
   « génération de la cible » n'a donc pas de cible exécutable côté scripts IHM aujourd'hui ; c'est un
   **enregistrement de build** (traçabilité, invalidation).
3. **Les exports** qui produisent des fichiers : `.XPG` vers Control Expert (`export/XpgWriter`,
   côté API), dossier de l'IHM (PDF/zip, `HmiDossier`), archive IHM (`HmiArchive`, manifeste
   SHA-256), paquets (§ 5), exports de données CSV/XLSX/PDF (`HmiExport`).

Ce qui manque par rapport au § 17 :
- **pas d'écriture « tout ou rien » à l'échelle d'un ensemble** : chaque artefact est atomique,
  mais un build interrompu ou en échec laisse un mélange d'artefacts neufs et anciens (le cache dit
  lesquels sont valides : c'est **cohérent** mais **pas transactionnel**). Même remarque pour
  `hmi::save` (chaque fichier atomique, index en dernier, `HmiStore.cpp:1104-1121`) ;
- **pas de dossier temporaire puis bascule** ;
- **rapport** : `pipeline::Report` (`HmiPipeline.hpp:273`) porte déjà durée totale, comptes,
  diagnostics, journal (`LogLine`) ; il manque **la durée par étape**, la **liste des sorties
  écrites** et **les éléments ignorés** (seul le compte `reused` existe) ;
- **pas d'étape « représentation intermédiaire »** : le seul intermédiaire est l'AST privé de
  `sim::Program`, interne à `Interpreter.cpp` (classes déclarées dans le `.cpp`, non exposées).

---

## 5. Fonctionnement actuel de l'import/export

### 5.1 Formats d'échange existants (scripts, fonctions, types, symboles)

| Extension | Contenu | Écrit/lu par | Version | Comparaison à l'import |
|---|---|---|---|---|
| `.xpgst` (1.11.3) | **texte ST** : scripts d'une vue (`OnOpen/OnCycle/OnClose`) ou opérateurs d'un symbole/type, séparés par des commentaires `(*# … *)` ; un `.st` nu = un seul script | `hmi::scriptfile` (`HmiScriptFile.hpp/.cpp`) | `kFormat = 1`, refus d'un format plus récent | par **événement** (scripts) ou **signature op/gauche/droite** (opérateurs) : nouveau / identique / différent → remplacer ou ajouter à la suite |
| `.xpgfonctions`, `.xpgscripts`, `.xpgtypes` (1.11.2, décision 174) | **zip** : `paquet.txt` (manifeste : format, version de l'écrivain, genre, éléments choisis) + un **petit projet IHM** écrit par `serializeProject` (donc le format 22, blocs `VAR` compris) | `hmi::pkg::collectPrograms`, `toZip`/`fromZip` (`HmiPackage.hpp:110-139`) | `kPackageFormat = 2`, `kWriterVersion = "1.11.16"` ; refus si paquet ou format IHM plus récents (`tooNew`) | **par nom** : `State{Missing, Same, Different}` → `Choice{Add, KeepOurs, Rename, Replace, Skip}` (`HmiPackage.hpp:143-185`) |
| `.xpgvues`, `.xpgsymboles`, `.xpgmodele` | idem, vues/symboles + tout ce qu'ils emportent (symboles imbriqués, popups, ressources, styles, variables IHM lues, types, fonctions appelées) | `hmi::pkg::collect`, `collectSymbols` | idem | idem ; une vue/un symbole différent → renommé par défaut |
| Archive IHM `.zip` | le dossier `ihm/` complet + `manifeste.txt` (comptes + **SHA-256 par fichier**) | `HmiArchive.*` | — | reconstruction complète puis contrôle de cohérence |

Les dépendances emportées sont calculées **par recherche de mots** (`usesWord`/`renamedWord`,
`HmiPackage.cpp:169-180`, via `design::replaced` en mot entier) ; un renommage à l'import réécrit les
textes du paquet par **remplacement de mot** (et `renameSymbol` pour les symboles,
`renameViewIn`, `:124-141`). L'import passe par **`importInto` dans une seule `changeProject`** :
**un Ctrl+Z annule l'import entier** ; un fichier illisible ou plus récent grise « Importer »
(`HmiImportDialog.hpp`). Les identifiants sont **réattribués** à l'import (`withNewIds`, `:100`) :
les `Id` du paquet ne servent pas à reconnaître un élément déjà présent.

### 5.2 Excel / CSV / TSV / XLSX (réutilisable pour les grilles du § 2 de la spécification)

- **Coller depuis Excel** : `app::paste` (`app/TablePaste.hpp`) — grille TSV relue
  (`parseGrid`, guillemets Excel), **colonnes reconnues par leur titre** (alias, accents/casse
  ignorés : `normalizedTitle`), colonne clé = la ligne visée, création/mise à jour, **case refusée
  notée sans arrêter le reste**, **un seul Ctrl+Z** (`CommandGroupScope`). Utilisé par 8 volets
  (variables API et IHM, types, tables d'animation, affichages, notifications, sécurité,
  supervision).
- **Copier vers Excel** : `ui::TableView::copyText/copySelection` (TSV, titres facultatifs).
- **Écrire** CSV (`;`, BOM UTF-8), **XLSX** (zip XML, en-tête figé et filtrable, plusieurs feuilles),
  PDF : `hmi::exportCsv/exportXlsx/exportXlsxSheets/exportPdf` (`HmiExport.hpp:59-64`) — **dupliqué**
  dans `export/DocKit.*` pour `xpg_import` (voir § 7).
- **Lire** XLSX/XLSM : `xls::Workbook` (`xls/Workbook.hpp:62-111`, miniz).
- Le collage de **membres de type IHM** depuis Excel existe avec aperçu (`HmiVariablePanes.cpp:3287-3470`).

### 5.3 Ce qui manque par rapport au § 18

| Exigence | État |
|---|---|
| Version du format, identifiants, noms qualifiés, dépendances, types référencés | version : oui ; **identifiants : réattribués** ; noms qualifiés : non ; dépendances : implicites (contenu du petit projet), pas listées comme « requises » |
| Checksum | non dans les paquets ; **oui dans l'archive IHM** (SHA-256, `HmiArchive`, `HmiCrypto`) — réutilisable |
| Valider avant de modifier, aperçu | oui (`plan` + dialogue) |
| Conflits d'identifiants | **non** (comparaison par nom) |
| Remplacer / fusionner / dupliquer / ignorer / renommer | remplacer, renommer, garder, ignorer (variables) : oui ; **fusionner, dupliquer : non** |
| Dépendances manquantes affichées | partiel (variables absentes) ; une fonction appelée mais non emportée n'est pas signalée |
| Transactionnel, annulation complète | **oui** (une commande) |
| Rapport | `ImportResult::summary()` (texte), non persistant |
| Compatibilité anciens formats | oui par construction (le petit projet est relu par `parseProject`, qui lit tous les formats ≤ 22) |

---

## 6. Composants d'interface réutilisables

### 6.1 Éditeur de code : `ui::MultiLineText` (`src/ui/widgets/Controls.hpp:168-513`)

| Capacité | État | Détail |
|---|---|---|
| Coloration | oui | `ui::Syntax` (`tokenizeLine`, ST/C/C++/IL), état de commentaire par ligne |
| Complétion | oui | `CompletionProvider`, `Completion{text, detail, icon, rank, insert, caret, extend, chain, badge}` (extraits avec curseur, réouverture) ; Ctrl+Espace ; ouverture après un point (`setCompleteAfterDot`) |
| Aide aux paramètres | **une seule signature** | `Signature{name, parameters, returns}` + `activeParameter()` ; **pas de liste de surcharges**, pas de documentation par paramètre |
| Soulignement des fautes | oui (1.10) | `Squiggle{line, column, length, tone, message}`, `selectRange`, `squiggleAt` |
| Repliage | oui, **par indentation** | `toggleFold`, `foldAll`, `unfoldAll`, `foldToDepth`, colonne des marqueurs à la souris ; au clavier, la branche « Ctrl+Espace → replier » (`Controls.cpp:2374-2375`) est **du code mort** : Ctrl+Espace est pris juste avant par la complétion (`:2272`) |
| Marge des points d'arrêt, ligne d'exécution, notes de ligne | oui (lot API 8) | `setBreakpointGutter`, `setExecutionLine`, `setLineNotes` |
| Valeur au survol | oui | `ValueProvider`, `liveTooltip` |
| Nom sous le curseur, double-clic « ouvrir » | oui | `symbolAtCaret`, `caretSymbolChanged`, `symbolActivated` — **branché pour les sections API et le débogage seulement** (`MainAnalysisScreen.cpp:1395`, `SimDebugPane.cpp:539`), **pas pour les scripts IHM** |
| Presse-papiers | **système** (SDL) | Ctrl+C (sans sélection : **tout le texte**, `Controls.cpp:1349`), Ctrl+X (avec sélection seulement), Ctrl+V (`\r` retirés, `:2312-2314`) |
| Annuler / rétablir | **pile de l'application** | chaque frappe est une commande fusionnée (`HmiScriptPanes.cpp:573-592`) |
| Rechercher/remplacer dans le texte, aller à la ligne, dupliquer/déplacer une ligne, commenter, formater | **non** | (le volet IHM « Rechercher » travaille sur tout le projet : `hmi::design::FindOptions`, `HmiDesign.hpp:81`) |

Le **même widget** sert aux sections API, au grafcet (`GrafcetCodeEditor`, Ctrl+Entrée), aux
macros, aux scripts et fonctions IHM : toute amélioration (raccourcis VS, ligne, repliage) profite
partout — et **tout changement de comportement touche tous ces éditeurs** (risque, § 8).

### 6.2 Grilles, arbres, propriétés (`src/ui/widgets/DataViews.hpp`)

- **`ui::TableView`** (`:494`) + `ITableModel` (`:406`) : tri (`less` par colonne, `sortBy`),
  filtre global et **filtres par colonne** (`FilterChain`, `ColumnFilter` : contient, =, entre…,
  listes de valeurs ; mémorisés), **édition en place** (`editable`, `setCellText`, **liste de choix**
  `cellChoices` → la colonne Type, Visibilité, Stockage, Mode), multi-sélection,
  **glisser-déposer de lignes** (`setRowDragEnabled`, `rowsDropped` → réordonner les paramètres),
  copier TSV (`copyText`), **collage** (`setPasteHandler`, `PasteRequest`, marques
  créé/modifié/refusé `setPasteResult`), menu contextuel extensible (`setExtraContextItems`),
  surlignage de la recherche, F2/Suppr → `renameRequested`/`deleteRequested`
  (`DataViews.cpp:1164-1166`). **C'est exactement la grille des onglets Constantes, Variables,
  Paramètres, Références** de la maquette.
- **`ui::TreeView`** (`:215`) + `ITreeModel` (`:198`) : l'arbre du projet
  (`ProjectTreeModel`, `app/ViewModels.cpp`, 4 097 l.), identifiants de nœud empaquetés
  `genre(8 bits)|index(28)|sous-index(28)` (`ViewModels.cpp:432-462`) ; dépôt, déplier, filtre
  (Ctrl+Maj+F), marques d'état de build (`HmiBuildMarks`, `ViewModels.hpp:535`).
- **`ui::PropertyGrid`** (`:813`) avec aide à la saisie par case (`assist::gridAssist`).
- **`ui::TabControl`** (`Containers.hpp:63`) : les sous-onglets (Programmation générale : « Scripts
  généraux | Variables IHM | Types IHM », `HmiScriptPanes.cpp:454-462` ; symbole : « Fonctions |
  Popups »).

### 6.3 Briques propres à l'IHM (`src/app/hmi/*`)

| Composant | Fichier | Réemploi pour la refonte |
|---|---|---|
| `HmiToolStrip` | `HmiPanels.hpp:67` | barres d'outils `[+] [-] [Dupliquer] [↑] [↓] [Compiler]`, `setEnabledWhen` (bouton grisé sans document compilable), `setText` (info-bulle avec raccourci) |
| `HmiFolderTable` | `HmiFolderTable.*` | listes rangées en dossiers (lot 21) |
| `HmiVariablesPane` | `HmiVariablePanes.*` (3 660 l.) | grille de déclarations typées, Excel ↔ grille, membres de types, F2 |
| `HmiParamPanes` + `hmi::params` | `HmiParamPanes.*`, `HmiPopupParams.hpp` | **grille de paramètres** (nom, type, mode, défaut, description), **compatibilité de types** (`typeAccepts`, `typeAcceptsFor`), **type d'une expression** (`expressionType`), **contrôle des arguments** (`checkArguments` : inconnu, manquant, incompatible), **renommage d'un paramètre avec ses emplois** (`renameParam`) |
| `HmiOperatorPanes` | `HmiOperatorPanes.*` | formulaire de **signature structurée** (op, gauche, droite, résultat) au-dessus d'un éditeur — le patron de la vue « Fonction » (§ 20) |
| `HmiValuePicker` | `HmiValuePicker.*` | sélecteur arborescent **filtré par type attendu**, catégories (API, IHM, Système, Symbole/vue, Constante), recherche, détail, création d'une variable manquante : base du **sélecteur de type** et du **choix de cible d'une référence externe** |
| `assist::*` | `HmiAssist.*` (3 028 l.) | complétion par contexte (`locate` : `Code`, `Member`, `LocalType`…), pastilles nature/provenance (`badgeOf`), signatures (`signature`), description (`describe`), aide des champs (`fieldAssist`) |
| `HmiImportDialog` | `HmiImportDialog.*` | **aperçu d'import** (tableau nouveau/identique/conflit, choix par élément, une commande) |
| `RenameDialog` | `app/RenameDialog.*` | **aperçu du renommage** (arbre des endroits, avant/après, onglets Tout/API/IHM, un Ctrl+Z) |
| `GoToPanel` / `gotosearch` | `app/GoToPanel.*`, `GoToSearch.*` | **Ctrl+K** recherche globale par catégorie (dont `GScript`, `GCode` : le texte des scripts ligne à ligne) → « Ctrl+T / Ctrl+, » |
| `HmiBuildOutputPane`, panneau du bas | `HmiBuildPanes.*`, `HmiConsole.*` | **Sorties / Console / Diagnostics** (1.11.14) : gravité, code, élément, fichier, ligne, colonne, étape, suggestion ; double-clic → source. Base du « panneau de diagnostics filtré sur l'élément compilé » |
| `HmiBuildManager` | `HmiBuild.*` | build en fil de fond, analyse 300 ms après la dernière modification, états de l'arbre |
| `app::paste` | `app/TablePaste.*` | collage Excel générique (§ 5.2) |
| `FormDialog`, `HmiAskDialog` | `screens/Dialogs.cpp`, `HmiAskDialog.*` | petites fenêtres de saisie |
| `Settings` | `app/Settings.*` | fichier `clé=valeur`, **clés inconnues conservées** : stockage naturel des raccourcis personnalisés |

### 6.4 Infrastructure non graphique réutilisable

- **Commandes** : `hmi::changeProject/changeView` (instantané, `HmiCommands.hpp:138-140`),
  `core::GroupCommand`/`CommandGroupScope` (« un geste, un pas », `core/Command.hpp`) → migration et
  import en **une** commande annulable.
- **Écriture atomique** : `core::writeFileAtomic` (`.tmp` relu, `.bak`, renommage).
- **Empreintes** : `pipeline::hash` (FNV-1a 64), SHA-256 (`HmiCrypto`).
- **Pipeline** : clés stables, cache, plan, états (§ 3.2).
- **Double presse-papiers** de l'éditeur de vues (`HmiCanvas.cpp:34-50`, `:235-300`) : objets
  riches en mémoire + texte TSV dans le presse-papiers système (`systemText`), Ctrl+V choisit selon
  que le texte système a changé — patron direct du « format riche + texte lisible » du § 15
  (limite : le format riche ne sort pas du processus).
- **Rejeu de sessions** (`app/ScriptRunner.*` : `touche Ctrl+Z`, `clic`, `arbre "A/B"`…) et 128
  sessions dans `tools/sessions/` : tests d'intégration clavier/souris possibles sans écran réel.

---

## 7. Incohérences et duplications

### 7.1 Lectures multiples du même ST (duplication principale)

| # | Lecteur | Fichier:ligne | Usage |
|---|---|---|---|
| 1 | `sim::Lexer` + `Parser` | `sim/Interpreter.cpp:78` | exécution, syntaxe |
| 2 | `ui::tokenizeLine` | `ui/Syntax.cpp` | coloration |
| 3 | `scriptcheck::lex` | `hmi/HmiScriptCheck.cpp:60` | contrôle sémantique |
| 4 | `exprcheck::lex` | `hmi/HmiExprCheck.cpp:80` | contrôle des expressions |
| 5 | `lang110::tokensOf` | `hmi/HmiScriptCheck110.cpp:179` | dialecte |
| 6 | `splitDeclarations` | `hmi/HmiScript.cpp:434` | déclarations `VAR` |
| 7 | `scriptNamesRaw`, `scriptPaths`, `scriptCallees`, `renameCalls`, `quotedArgs` | `hmi/HmiScript.cpp:251/316/609/638/85` | noms, chemins, appels, renommage |
| 8 | `pipeline::identifiers`, `signatureOf` | `hmi/HmiPipeline.cpp:390/371` | dépendances, interface |
| 9 | `substituteParams`, `qualifySymbolCalls`, `replacePath` | `hmi/HmiSymbols.cpp` | expansion des symboles |
| 10 | `assist::locate` et voisins | `app/hmi/HmiAssist.cpp` | complétion |
| 11 | `rewriteEverywhere` (énumérations), `renameComparedStrings` | `HmiEnums.cpp:137`, `HmiRenameRefs.*` | renommages |

**33 fichiers** définissent leur propre `identStart`/`identChar` (grep). Chaque lecteur a ses règles
pour les chaînes (`'…'`, `$'` d'échappement ST, `"…"`), les commentaires (`(* *)`, `//`), les
littéraux typés (`T#5s`, `16#FF`, `T_MODE#Auto`), les repères `$…$` (1.11.1) : **un écart entre deux
lecteurs = une fonctionnalité qui voit un nom que l'autre ne voit pas**. La refonte doit fournir
**un seul lexer partagé exposé** (celui de `sim`, avec positions ligne/colonne) et faire converger
les autres progressivement, pas en ajouter un douzième.

### 7.2 Déclarations : trois analyseurs qui ne disent pas la même chose

- `splitDeclarations` (`HmiScript.cpp:434`) : `VAR`, `VAR_TEMP`, `VAR_INPUT` (fonction seulement) ;
  **laisse `VAR_IN_OUT`/`VAR_OUTPUT` dans le corps** (`HmiScriptCheck.cpp:165-166`) ; ignore
  `RETAIN`/`CONSTANT` (acceptés puis **oubliés** : `LocalVar` n'a pas de champ « constante »,
  `HmiScript.cpp:539`) ; saute `FUNCTION…END_FUNCTION`.
- `sim::Parser::varBlock` (`Interpreter.cpp:1145`) : les cinq sortes, `CONSTANT` gardé dans
  `VarDecl::constant`, `RETAIN` accepté et ignoré ; refuse un bloc après la première instruction
  (`:550`).
- `hmitree::withoutDeclarations` (`app/hmi/HmiTreeData.hpp:522`, en ligne pour être compilé sans la
  bibliothèque IHM) : `VAR`, `VAR_TEMP`, `VAR_INPUT` seulement, cherche `END_VAR` par simple recherche
  de texte (`:553`) ; sert l'arbre (signature d'une fonction IHM, variables employées) et
  l'import (`ImportWorkspace.cpp:135`, `:179`) — **troisième** source de vérité sur les paramètres.
- Le moteur combine les deux premiers différemment pour un **script** (texte non blanchi, le dialecte tient
  les `VAR`) et pour une **fonction** (texte blanchi, le moteur IHM tient les locales)
  (`HmiRuntime.cpp:1012-1020`).
- **Deux chemins d'exécution des fonctions** (simple / riche, `HmiRuntime.cpp:477-488` et `:1105`)
  avec deux limites de profondeur (`kMaxDepth` / `maxCallDepth{64}`).

### 7.3 Types : listes et règles en double

- Listes de types élémentaires : `kVariableTypes` (11, `HmiModel.hpp:890`), `kLocalTypes`
  (14 : + `SINT`, `USINT`, `BYTE`, `HmiScript.hpp:103`), `HmiEnums.cpp:71`, `params::baseTypes`
  (`HmiPopupParams.cpp:125`), mots-clés de `HmiScriptCheck.cpp:1363` et `HmiAssist.cpp:305`,
  `sim::typeFromName`. → une variable IHM ne peut pas être `BYTE`, une locale si.
- **Deux règles de conversion implicite contradictoires** :
  - `matchCost` (opérateurs, `HmiOperators.cpp:82-90`) : `DINT`→`REAL` permis (coût 2),
    `LREAL`→`REAL` permis (coût 1, **rétrécissement**), `UINT`→`INT` permis (même rang) ; **pas de
    détection d'ambiguïté** (le premier de coût minimal gagne, `:490`) ;
  - `params::typeAccepts` (paramètres de popup, `HmiPopupParams.cpp:170-186`) : `REAL` n'accepte
    qu'un entier ≤ 16 bits, `LREAL`→`REAL` refusé, `UINT`→`INT` refusé.
  - S'y ajoutent les « genres » grossiers de `exprcheck`/`scriptcheck` (`kindOf` :
    booléen/nombre/texte/durée, `HmiScriptCheck.cpp:234`) et les conversions du moteur
    (`sim::Value::assignFrom`).
- **Aucun identifiant de type** : `Variable::type`, `TypeMember::type`, `ViewParam::type`,
  `HmiFunction::returnType`, `HmiOperator::left/right/result` sont du texte ; renommer un type IHM
  passe par des réécritures textuelles (`renameEnumType`, `renameTypeInOperators`).
- « Sans retour » = **chaîne vide** (`HmiFunction::returnType`, persisté `retour=""`).

### 7.4 Noms, résolution, renommage

- **Espace de noms global par nom court** : `functionByName` (premier trouvé, sans casse),
  fonctions et variables IHM **partagent** leurs noms (`uniqueFunctionName`, `HmiModel.hpp:1783`).
- **Trois implémentations** de la résolution `Vue.Instance.Fonction` : moteur (`Env::symbolCall` →
  `boundSymbolFunction`), contrôle (`scriptcheck::Walker::instanceFunction`,
  `HmiScriptCheck.cpp:847-876`), dépendances (`pipeline::resolveCode`, `HmiPipeline.cpp:300-336`).
- **Renommage par réécriture de texte**, une fonction par genre, chacune avec **sa propre liste
  d'endroits** : `renameFunctionEverywhere`, `renameSymbolFunction`, `renameSymbol`,
  `renameObjectReferences`, `renameAlarmReferences`, `renameEnumValue/Type`, `renameTypeInOperators`,
  `renameParam`, `renameAlarmGroup`, `renameSymbolAlarmOverrides`, `renameOverridePaths`
  (`src/hmi/*.hpp`). Écarts constatés :
  - **`renameFunctionEverywhere` (`HmiScript.cpp:703-740`) et `functionCallers` (`:742-778`)
    ignorent les corps des fonctions de symbole (`View::functions`), les redéfinitions
    (`Object::functionOverrides`), les opérateurs (`View::operators`, `HmiType::operators`) et les
    alarmes de symbole (`View::alarms`)** : renommer une fonction IHM appelée depuis une fonction de
    symbole **laisse un appel cassé**, et « Appelé par » ne le montre pas (le parcours
    `rewriteEverywhere` des énumérations, lui, ajoute les opérateurs : `HmiEnums.cpp:136-137`) ;
  - **renommer un script général** met à jour les actions « Script général » mais **pas
    `IHM_APPELER('Ancien')`**, et le dit (`HmiWorkspace.cpp:2592` ; `HmiScriptPanes.cpp:1273-1296`).
- **Pas d'« Atteindre la définition »** dans les éditeurs de scripts IHM (`symbolActivated` non
  branché) ; « Rechercher les références » = recherche de **texte** en mot entier (volet
  « Rechercher », `ExplorerMenus.cpp:1355-1360`).

### 7.5 Compilation, génération, diagnostics

- Boutons « Compiler (F7) » des éditeurs = **projet entier** (§ 3.3) ; le volet de scripts compile
  **deux fois** (projet + build ciblé, `HmiScriptPanes.cpp:523-526`).
- La phase **Validation** du pipeline n'est **pas limitée à la portée** (`HmiPipeline.cpp:1828-1925`).
- **« Générer » désigne la validation de cohérence** (IHM › Générer) **et** la production
  d'artefacts (pipeline) — deux sens pour un mot que la spécification (§ 17) réserve à la production.
- **Sept structures de diagnostic** (§ 2.2) ; codes d'erreur seulement dans le pipeline (`E150`,
  `E401`, `E402`…), catégories texte ailleurs (`"Script"`, `"Fonction"`).

### 7.6 Stockage, outillage, divers

- **Deux formes de stockage du même concept** : script général et fonction IHM = un fichier
  (`ihm/scripts/`, `ihm/fonctions/`) ; script de vue et fonction de symbole = une ligne `corps="…"`
  dans le `.vue` (`HmiStore.cpp:349-352`, `:390-391`).
- **Données réelles incohérentes** : la vue dupliquée `0521-Vue_Armoire_B.vue` porte des scripts
  nommés `Vue_Armoire_A_OnOpen/OnCycle/OnClose` (ligne 507-509) — le nom d'un script de vue est
  généré à la création (`vv.name + "_" + event`, `HmiScriptPanes.cpp:584`) et **ne suit ni la
  duplication ni le renommage de la vue**. Un nom qualifié fondé sur ce nom serait faux.
- `HmiFunction::isVirtual` existe sur toutes les fonctions mais n'a de sens que pour un symbole
  (« Sans effet sur une fonction IHM », `HmiModel.hpp:647-649`).
- Commentaire mort : `HmiModel.hpp:725` renvoie à `HmiSymbolFunctions.hpp`, **qui n'existe pas**
  (le code est dans `HmiSymbols.hpp`).
- **Duplication d'outils** : écriture atomique (`HmiStore.cpp:38` `writeAtomically` vs
  `core::writeFileAtomic`, garanties différentes : pas de relecture ni de `.bak` dans `HmiStore`) ;
  empreintes (FNV-1a 32 `fingerprint` dans `HmiOperators.cpp:98`, FNV-1a 64 `pipeline::hash`,
  SHA-256 `HmiCrypto`) ; exports CSV/XLSX/PDF/zip (`hmi/HmiExport.*` et `export/DocKit.*`, dupliqué
  volontairement pour des raisons de dépendance de bibliothèques, `DocKit.hpp:6-7`).
- **`XpgAnalyzer.vcxproj` désynchronisé** (§ 1.1) ; `build.sh` (sans CMake) est un troisième système
  de construction à tenir.
- `functionTemplate` (`HmiScript.cpp:780-787`) **génère encore des blocs `VAR_INPUT`/`VAR_TEMP`**
  pour toute nouvelle fonction.

### 7.7 Raccourcis et presse-papiers

- Traitement **éparpillé** : `MainAnalysisScreen::handleShortcut` (`HistoryWorkspace.cpp:312-414`,
  deux passes avant/après les widgets), `App.cpp:2384-2393` (F11, F12), `DetachedWindows.cpp:678-712`
  (doublons pour les fenêtres détachées), et les widgets (`MultiLineText`, `TableView`,
  `HmiEditor`, `HmiPanels`…) — environ **400 comparaisons de touches dans 76 fichiers** (hors
  `SdlEventPump` et le rejeu de sessions).
- `help::keys` (61 lignes) est la **seule table** et sert à l'aide, au menu, à `bindingOf` pour
  quelques actions du registre (`core::ActionRegistry`) — mais la plupart des touches ne passent pas
  par le registre ; les libellés « (F7) » sont **écrits en dur** dans les info-bulles
  (`HmiScriptPanes.cpp:341-342`, `HmiFunctionPanes.cpp:157`).
- `ui::Key` (`platform/InputEvent.hpp:17-40`) est un **sous-ensemble** : pas de B, E, G, I, M, R,
  T, U, F4, F6, `-`, `,`, `.`, `=` ; traduction par **keycode SDL** (`SdlEventPump.cpp:28-75`) —
  dépendant de la disposition (bon pour les lettres, à vérifier pour `-`/`,`/`.` sur AZERTY).
- **Conflits avec la cible Visual Studio** : F12 (capture), Ctrl+H (historique), Ctrl+K (Aller à…,
  pris **avant** les widgets : `HistoryWorkspace.cpp:347-349` — les accords Ctrl+K,Ctrl+C seraient
  avalés), F7 (Compiler), F8 (démarrer/arrêter l'IHM ; VS : diagnostic suivant), Ctrl+J (panneau du
  bas), Ctrl+Tab (sous-onglet suivant du volet ; VS : document suivant), Ctrl+D (Dupliquer… dans
  l'éditeur de vues ; VS : dupliquer la ligne — contextes différents, compatible), Ctrl+Espace
  (complétion ; la branche de repli au clavier qui le suit est inaccessible, `Controls.cpp:2272` puis `:2374`).
- Copier sans sélection = **tout le texte** (Visual Studio : la ligne) ; couper sans sélection : rien.

---

## 8. Risques de régression

Classés par gravité (G = grave : perte ou corruption de données / comportement d'exécution changé ;
M = moyen : fonctionnalité dégradée ; F = faible : confort, documentation).

### 8.1 Données et formats

| # | Risque | Gravité | Origine dans le code | Parade |
|---|---|---|---|---|
| R1 | **Perte des déclarations** si un projet migré est rouvert puis enregistré par une 1.11.16 (lignes inconnues ignorées) | G | `parseProject` ignore un mot inconnu de l'index (« ignoré », `HmiStore.cpp:1850-1851`) et `parseView` celui d'une vue (`:631`) ; `kFormatVersion` | passer à **format 23** ; une version plus ancienne **refuse** (`HmiStore.cpp:1161-1164`, politique de chaque lot) ; écrire 23 **seulement** si le projet porte des déclarations modélisées (comme 21/22, `HmiStore.cpp:703`) |
| R2 | **Commentaires perdus** à la migration (`Poids_A : REAL := 0.5; (* 0.5 : la moyenne simple *)`) | G | `splitDeclarations` jette les commentaires du bloc (`HmiScript.cpp:518-527`) | extracteur sans perte : commentaire de fin de ligne → documentation de la déclaration ; commentaire de bloc → documentation du script ; rapport ligne à ligne |
| R3 | **`CONSTANT` et `RETAIN` perdus** (acceptés puis oubliés par `splitDeclarations`, `HmiScript.cpp:539`) | G | idem | utiliser `sim::VarDecl::constant` (`Interpreter.cpp:381-389`) ; `RETAIN` → **« Conservée »** (comportement actuel : le moteur ne persiste pas) et **signalé** dans le rapport, jamais converti silencieusement en « Persistante » |
| R4 | **`VAR_IN_OUT` / `VAR_OUTPUT` restés dans le corps** non migrés | G | `HmiScriptCheck.cpp:165-166` | migration fondée sur l'analyseur du dialecte (5 sortes) |
| R5 | **Fonctions internes** `FUNCTION…END_FUNCTION` d'un script (1.10) : leurs déclarations sont dans leur en-tête et leurs blocs | M | `splitDeclarations` les saute (`HmiScript.cpp:473-503`) | les migrer en **fonctions du script** (privées par défaut pour ne pas changer la résolution), ou les laisser telles quelles (décision à prendre ; voir § 11) |
| R6 | **Redéfinitions d'instance** (`FunctionOverride::body`) portent leur propre `VAR_INPUT` (« même signature ») | M | `HmiModel.hpp:510-517`, `HmiStore.cpp:533-538` | la signature vient de la fonction du symbole ; seules les **locales** de la redéfinition deviennent un modèle |
| R7 | **Anciens paquets** (`.xpgfonctions`, `.xpgscripts`, `.xpgvues`) et **`.xpgst`** contiennent des blocs `VAR` | M | `pkg::fromZip`, `scriptfile::read` | migration à l'import (même service), rapport dans l'aperçu |
| R8 | **Cache de build** et **artefacts** décrivent des corps avec blocs `VAR` (lignes, interfaces) | M | `pipeline::kGeneratorVersion = "1"` (`HmiPipeline.hpp:152`) | monter `kGeneratorVersion` : tout est régénéré une fois, la raison est dite |
| R9 | **Fichiers de script renommés/déplacés** (le nom contient l'`Id` et le nom) | F | `scriptFileName` (`HmiStore.cpp:76`) | inchangé ; la corbeille garde l'ancien |

### 8.2 Comportement à l'exécution

| # | Risque | Gravité | Origine | Parade |
|---|---|---|---|---|
| R10 | **Durée de vie** : `VAR` (conservée), `VAR_TEMP` (exécution) — tout changement de sens fausse les scripts cycliques (compteurs, mini/maxi) | G | `scriptLocals_` (`HmiRuntime.cpp:1052-1054`) | la migration mappe 1:1 (`VAR`→Conservée, `VAR_TEMP`→Exécution) ; tests d'exécution avant/après sur `bac/Armoire_Gaz` (`Statistiques_Pression`) |
| R11 | **Priorité des noms** : aujourd'hui une locale masque une variable IHM et un nom de l'automate du même nom (`HmiScript.hpp:82-85`) | G | moteur et contrôles | garder l'ordre « fonction › script › propriétaire › projet › bibliothèque › système » ; une variable **publique** d'un script ne doit **jamais** masquer silencieusement une variable IHM d'un **autre** script (diagnostic d'ambiguïté) |
| R12 | **Nouveaux noms qualifiés** `Script.X` en collision avec des chemins existants : `Vue.Objet.Propriété` (`HmiPublicVars.hpp`), variable IHM structurée (`Moteur.Vitesse`), `Vue.Instance.Fonction` (1.11.10), `SYS.`, `API.` | G | aujourd'hui les noms de scripts **ne sont pas** dans l'espace des expressions (appel par chaîne `IHM_APPELER('X')`) | règle de résolution écrite et testée, diagnostic d'ambiguïté ; contrôle de collision **avant** migration sur les projets réels |
| R13 | **Lignes décalées** : retirer les déclarations du texte change les numéros de ligne (diagnostics, `IHM_LOG` qui cite sa ligne — 1.11.14, Console, points de saut des diagnostics enregistrés dans le cache) | M | `HmiRuntime.cpp:983-990` (`announcedLine`), `trace_` | table de correspondance de lignes si l'on reconstruit un texte pour le moteur ; régénération du cache (R8) |
| R14 | **Deux chemins d'exécution des fonctions** (simple / riche) : unifier peut changer le résultat d'un cas limite (paramètre facultatif, conversion de retour `LREAL`→`REAL`, `HmiRuntime.cpp:1110`) | M | `callFunction` vs `dialectFunction` | tests de non-régression d'exécution sur les deux chemins avant de toucher |
| R15 | **Expressions *fx*** appellent les fonctions **en lecture seule** (`callFromExpression`, `HmiRuntime.cpp:404-423`) : une fonction qui écrit une variable publique de script depuis une expression doit rester refusée | M | `readOnly_` | même garde pour les nouvelles variables |
| R16 | **Surcharges** : un appel aujourd'hui valide peut devenir **ambigu** (deux fonctions de même nom importées) | M | `functionByName` = premier trouvé | diagnostic d'ambiguïté explicite ; aucun choix silencieux |
| R17 | **Simulation de l'automate** : toucher `sim::Parser` peut casser les sections API (MAST.XPG) | G | `Interpreter.cpp` partagé | **ne pas modifier le ST de l'automate** ; toute évolution derrière `ParseOptions::hmiDialect` ; `simulation_test`, `simdebug_test`, `hmi_test` à chaque lot |

### 8.3 Interface et habitudes

| # | Risque | Gravité | Origine | Parade |
|---|---|---|---|---|
| R18 | **Raccourcis réaffectés** (F12 capture, Ctrl+H historique, Ctrl+K Aller à…, F7/F8, Ctrl+J, Ctrl+Tab) : table « validée par le client le 02/10 » (`Shortcuts.cpp:4-7`), tutoriels, **sessions de captures** (`tools/sessions/*.txt`, `touche …`) | M | § 7.7 | profil « XPGAnalyser (actuel) » par défaut + profil « Visual Studio » choisissable ; conflits détectés ; sessions et `help::keys` mis à jour dans le même lot |
| R19 | **Copier sans sélection** passerait de « tout le texte » à « la ligne » dans **tous** les éditeurs (sections API, grafcet, macros) | M | `Controls.cpp:1349` | réglage, et test `fold_test`/`hmi_editor_test` qui lisent le presse-papiers |
| R20 | **Repli au clavier inexistant de fait** (branche Ctrl+Espace morte, `Controls.cpp:2272`/`:2374`) : ajouter Ctrl+M,Ctrl+M ne doit pas changer Ctrl+Espace (complétion) | F | `MultiLineText::handleEvent` | Ctrl+M,… pour le repli ; retirer la branche morte |
| R21 | **Performance de frappe** : diagnostics recalculés à chaque frappe (`placedScriptDiagnostics`) et analyse du build 300 ms après chaque modification (projet entier, `HmiBuildWorkspace.cpp:4-6`) | M | — | résolveur incrémental par élément ; mesurer sur `bac/Armoire_Gaz` (19 009 nœuds parcourus en 1.11.12) |
| R22 | **Fils** : le build travaille sur une **copie** du projet dans un autre fil ; un registre de types global mutable serait partagé | M | `HmiBuildManager`, `setPlcNames` (global partagé, `HmiSymbols.hpp`) | registres **immuables**, construits par copie du projet |
| R23 | **Aide et guide** décrivent l'ancienne syntaxe (`HmiGuideText.cpp` : 45 occurrences, `tools/guide-ihm/*.txt`, `HelpTryIt.cpp`) | F | — | mise à jour dans le lot de migration |

### 8.4 Tests existants qui dépendent de l'ancienne syntaxe

`tests/hmi_test.cpp` (21 082 lignes, **123** occurrences de `VAR_INPUT`/`VAR_TEMP`/`END_VAR`),
`tests/hmi_editor_test.cpp` (27 207 lignes, **41**), `tests/hmi_build_test.cpp` (**6**, dont
`signatureOf`), `tests/hmi_log_test.cpp` (**1**). **Règle proposée : l'ancienne syntaxe reste
LUE (compatibilité) pendant toute la refonte** ; ces tests restent verts et servent de filet ; de
nouveaux tests couvrent le modèle. Les essais `hmi_test` et `hmi_editor_test` prennent la fixture
`tests/fixtures/ihm` et `tests/fixtures/MAST.XPG` (présents dans le dépôt) ; un essai dont une source
ou un fichier d'entrée manque est **sauté sans échec** (`CMakeLists.txt:221-271`) : vérifier dans la
sortie de CMake qu'aucun essai IHM n'est « sauté » ou « non enregistré » avant de s'y fier.
**21 sources d'essai citées par `CMakeLists.txt` sont absentes du dépôt** (dont `treednd_test`,
`macro_test`, `grafcet_test`, `xref_test`, `executionorder_test`, `help_test`) : ces essais sont
sautés ; pour la refonte, la couverture de l'arbre repose donc sur `viewmodel_test` seul.

---

## 9. Liste précise des fichiers à modifier

Légende : **[M]** modifier, **[N]** nouveau (dans `src/hmi/` ou `src/app/…`, ramassé par le glob
CMake), **[D]** documentation/aide, **[T]** test. Les numéros de lot renvoient au § 11.

### 9.1 Modèle, persistance, migration (`src/hmi`)

| Fichier | Nature | Lot | Pourquoi |
|---|---|---|---|
| `src/hmi/HmiModel.hpp`, `HmiModel.cpp` | [M] | 0, 3 | sous-modèle de déclarations sur `Script`, `HmiFunction` (projet et symbole), `FunctionOverride` (locales), `HmiOperator` (locales) ; `ReturnType` explicite « Aucun » ; visibilité, documentation, version ; accès par `Id` ; fabriques `unique…Name` ; corriger le commentaire `HmiSymbolFunctions.hpp` (`:725`) |
| `src/hmi/HmiDecl.hpp/.cpp` | [N] | 2 (extraction), 3 (modèle) | `ConstantDef`, `VariableDef` (stockage : Exécution / Conservée / Persistante / Partagée), `ParameterDef` (mode Entrée / Sortie / Entrée-sortie, défaut), `ExternalRefDef` (cible = clé stable + nom qualifié de repli, alias, statut), `FunctionDef`/`OverloadSet` ; égalité, copie, renumérotation (`Id` neufs) |
| `src/hmi/HmiStore.hpp/.cpp` | [M] | 3 (8 : homonymes) | format **23** : lignes `constante`, `variable_script`, `parametre`, `reference`, `fonction_script` sous `script`/`fonction`/`fonction_symbole`/`redefinition` ; lecture 1–22 inchangée ; `kFormatVersion` ; écrire 23 seulement si nécessaire |
| `src/hmi/HmiMigrate.hpp/.cpp` | [N] | 4 | détection des blocs, extraction **sans perte** (via `sim::Parser`), conversion, rapport (`MigrationReport` : par script, par déclaration, avertissements), application en **une** commande ; sauvegarde par **version du projet** (`HmiVersions`) ou archive (`hmi::exportArchive`) |
| `src/hmi/HmiScript.hpp/.cpp` | [M] | 0, 3–8 | `splitDeclarations` → couche de compatibilité ; `functionSignature`/`functionText`/`checkFunction` depuis le modèle ; `functionTemplate` sans `VAR` ; `renameFunctionEverywhere`/`functionCallers` complétés (fonctions de symbole, redéfinitions, opérateurs, alarmes de symbole) — **correctif immédiat**, lot 0 |
| `src/hmi/HmiCommands.hpp/.cpp` | [M] | 3, 5 | commandes fusionnables pour les grilles de déclarations (clés de fusion par déclaration) |
| `src/hmi/HmiDuplicate.hpp/.cpp` | [M] | 3 | dupliquer un script/une fonction : nouvelles `Id` des déclarations ; nom des scripts de vue (incohérence § 7.6) |
| `src/hmi/HmiVersions.cpp` | [M] | 3, 4 (version « Avant migration ») | comparer les déclarations (aujourd'hui : texte des corps, `:791-818`) |
| `src/hmi/HmiDossier.cpp`, `HmiArchive.cpp` | [M] | 3 | dossier IHM (signatures, « (sans retour) », `:405-421`) ; archive (comptes) |

### 9.2 Analyse, types, résolution, compilation (`src/sim`, `src/hmi`)

| Fichier | Nature | Lot | Pourquoi |
|---|---|---|---|
| `src/sim/Interpreter.hpp/.cpp` | [M] (minimal, dialecte seulement) | 2 ; 8 (profondeur) ; voie B facultative | exposer le **lexer** (jetons avec ligne/colonne) et l'extraction des déclarations (`VarDecl`) pour l'extracteur sans perte ; accepter des **déclarations fournies** (paramètres/locales d'un modèle) sans texte, ou garder la reconstruction textuelle (§ 11, choix A/B) ; profondeur de récursion configurable |
| `src/hmi/HmiTypeRegistry.hpp/.cpp` | [N] | 6 | registre (clés stables `base:REAL`, `ihm:615`, `api:T_ANA`, `sym:…`, `void`), catégories, provenance, **une** règle de compatibilité (fusion de `matchCost`, `params::typeAccepts`, règles de `simdata`), types récents |
| `src/hmi/HmiPopupParams.hpp/.cpp`, `HmiOperators.hpp/.cpp`, `HmiSimData.cpp`, `HmiTypes.hpp/.cpp`, `HmiEnums.hpp/.cpp` | [M] | 6 | brancher sur le registre ; supprimer les listes de types en double (`kVariableTypes`, `kLocalTypes`, `HmiEnums.cpp:71`, `HmiPopupParams.cpp:125`) |
| `src/hmi/HmiNames.hpp/.cpp` (`QualifiedName`, `SymbolResolver`) | [N] | 7 (index des références : 10) | **une** résolution : portées emboîtées, alias, priorités, ambiguïtés, `Vue.Instance.Script.X`, index des références (positions) pour la navigation et le renommage |
| `src/hmi/HmiOverloads.hpp/.cpp` (`OverloadResolver`) | [N] | 8 | meilleure signature par coûts (registre), ambiguïté, diagnostics, liste pour l'aide |
| `src/hmi/HmiScriptCheck.hpp/.cpp`, `HmiScriptCheck110.hpp/.cpp`, `HmiExprCheck.hpp/.cpp` | [M] | 3, 7–9 | portée = modèle (plus de `splitDeclarations`) ; appels via le résolveur ; types d'arguments ; récursion ; un seul `Finding` → `Diagnostic` |
| `src/hmi/HmiCheck.hpp/.cpp` | [M] | 3, 9 | `compileFocused`/`projectIssues` sur le modèle ; `Issue` → diagnostic unifié (code, fonction, élément) |
| `src/hmi/HmiSymbols.hpp/.cpp` | [M] | 7, 10 | `boundSymbolFunction`, `qualifySymbolCalls`, `renameSymbolFunction` via le résolveur ; fonctions **dans les scripts du symbole** |
| `src/hmi/HmiRuntime.hpp/.cpp` (+ `HmiRuntimePublic.cpp`) | [M] | 3, 7, 8 | `prepare`/`callFunction`/`Env::call`/`dialectFunction` : déclarations depuis le modèle ; appels qualifiés et surcharges ; stockage Conservée/Persistante/Partagée ; un seul chemin d'appel |
| `src/hmi/HmiExpr.hpp/.cpp` | [M] | 7 | `FunctionHost` : fonctions de script appelables depuis *fx* (lecture seule) |
| `src/hmi/HmiPipeline.hpp/.cpp` | [M] | 1, 2, 3, 9, 13 | interface depuis le modèle (remplace `signatureOf`) ; dépendances par clés (résolveur) au lieu de `identifiers` ; éléments « fonction de script » ; **validation limitée à la portée** ; `kGeneratorVersion` ; durée par étape ; dossier temporaire + bascule (lot 13) |
| `src/hmi/HmiRenameRefs.hpp/.cpp`, `HmiEnums.cpp` (`rewriteEverywhere`) | [M] | 10 | renommage par références (index du résolveur), le texte n'étant réécrit qu'aux positions résolues |
| `src/hmi/HmiDiagnostics.hpp` | [N] | 9 | **un** type de diagnostic (celui de `pipeline::Diagnostic` enrichi : `function`, `quickAction`) et conversions depuis les anciens |

### 9.3 Import / export

| Fichier | Nature | Lot | Pourquoi |
|---|---|---|---|
| `src/hmi/HmiPackage.hpp/.cpp` | [M] | 3, 4, 12 | manifeste : identifiants, noms qualifiés, dépendances requises, types référencés, **SHA-256** par fichier (comme `HmiArchive`) ; comparaison par **identifiant puis nom** ; choix « fusionner », « dupliquer » ; rapport ; migration à l'import |
| `src/hmi/HmiScriptFile.hpp/.cpp` | [M] | 4, 12 | `.xpgst` format 2 : déclarations en en-tête `(*# constante … *)` ; lecture du format 1 avec migration |
| `src/app/hmi/HmiImportDialog.hpp/.cpp` | [M] | 4, 12 | colonnes « identifiant », « dépendances manquantes », choix supplémentaires, rapport |
| `src/app/DropFilesPlan.cpp`, `src/app/screens/ImportWorkspace.cpp` | [M] | 2 (`ImportWorkspace`), 12 | `hmitree::withoutDeclarations` (3e analyseur) remplacé par le modèle (`ImportWorkspace.cpp:135`, `:179`) |

### 9.4 Interface (`src/app`, `src/ui`)

| Fichier | Nature | Lot | Pourquoi |
|---|---|---|---|
| `src/app/hmi/HmiScriptPanes.hpp/.cpp` | [M] | 1, 5, 9 | onglets Code / Constantes / Variables / Références / Fonctions / Diagnostics ; **Compiler = le script affiché** (`compileHere` retiré au profit du build ciblé), info-bulle « Compiler le script actuel », grisé sans script |
| `src/app/hmi/HmiFunctionPanes.hpp/.cpp` | [M] | 1, 5, 8 | formulaire Signature (nom, retour, visibilité), grilles Paramètres/Locales/Constantes/Références/Surcharges/Appels ; **Compiler = la fonction affichée** (plus d'ouverture de IHM › Compiler) ; `parameterList` (`:97-103`) depuis le modèle |
| `src/app/hmi/HmiDeclGrid.hpp/.cpp` | [N] | 5 | **une** grille générique de déclarations (TableView + HmiToolStrip `[+] [-] [Dupliquer] [↑] [↓]`, tri, recherche, validation immédiate, erreurs, Excel) réutilisée par tous les onglets |
| `src/app/hmi/HmiTypePicker.hpp/.cpp` | [N] | 6 | sélecteur de type (recherche, catégories, récents, provenance, ouvrir la définition, invalides) bâti sur le registre et le patron `HmiValuePicker` |
| `src/app/hmi/HmiAssist.hpp/.cpp` | [M] | 11 | complétion après `Script.`, `Instance.Script.`, `Vue.Instance.Script.` ; surcharges, argument actif, type attendu, défaut, documentation ; filtrage par type ; `splitDeclarations` (6 appels) retiré |
| `src/app/hmi/HmiEditor.hpp/.cpp`, `HmiActionDialogs.hpp/.cpp` | [M] | 1, 5, 7 | fonctions de symbole, redéfinitions (fenêtre du script), scripts d'action |
| `src/app/hmi/HmiOperatorPanes.*`, `HmiParamPanes.*` | [M] | 6 | sélecteur de type commun |
| `src/app/hmi/HmiTreeData.hpp` | [M] | 2 (analyseur), 10 (nœuds) | supprimer `withoutDeclarations`/`signatureOf` (3e analyseur, `:516-594`) |
| `src/app/ViewModels.hpp/.cpp` | [M] | 10 | nœuds (ajoutés **à la fin** de `NodeKind`, 149/256 utilisés) : Constantes, Variables, Références, Fonctions (surcharges groupées), Paramètres, Retour, Appels, Diagnostics ; libellés et cibles de build |
| `src/app/screens/ExplorerMenus.cpp` | [M] | 10 | menus : copier le nom qualifié, atteindre la définition, rechercher les références, compiler l'élément, exporter, propriétés |
| `src/app/screens/HmiWorkspace.cpp`, `HmiBuildWorkspace.cpp`, `HistoryWorkspace.cpp` (`handleShortcut`), `GoToWorkspace.cpp` | [M] | 1, 4, 10, 14 | ouverture des éléments, F7/F8, navigation, Aller à… (déclarations) |
| `src/app/RenameDialog.hpp/.cpp` | [M] | 10 | moitié IHM du plan : renommage symbolique (constante, variable de script, fonction, script) |
| `src/app/hmi/HmiBuildPanes.*`, `HmiConsole.*` | [M] | 1, 9, 13 | diagnostics filtrés sur l'élément compilé ; diagnostic suivant/précédent ; rapport de génération par étape |
| `src/ui/widgets/Controls.hpp/.cpp` (`MultiLineText`) | [M] | 8 (signatures), 10 (navigation), 14 | commandes d'édition VS (couper/copier la ligne, dupliquer, déplacer, commenter, indenter, aller à la ligne, rechercher/remplacer, repli Ctrl+M), **signatures multiples** (surcharges), historique de navigation |
| `src/ui/widgets/DataViews.hpp/.cpp` (`TableView`) | [M] | 5 | Ctrl+D (recopier vers le bas), réordonnancement clavier si absent |
| `src/platform/InputEvent.hpp`, `src/platform/SdlEventPump.cpp`, `src/app/ScriptRunner.cpp`/`UiDriver` (`parseKey`) | [M] | 14 | touches manquantes (B, E, G, I, M, R, T, U, F4, F6, `-`, `,`, `.`), **accords** (Ctrl+K, Ctrl+C), disposition clavier |
| `src/app/ShortcutService.hpp/.cpp` (ou `src/help/` pour la table) | [N] | 14 | commandes nommées, liaisons par contexte, accords, conflits, profils, persistance (`Settings`), libellés des menus et info-bulles |
| `src/help/Shortcuts.hpp/.cpp` | [M] | 1 (texte de F7), 14 | la table devient les **valeurs par défaut** du service ; `bindingOf` lit le service |
| `src/app/App.hpp/.cpp`, `src/app/DetachedWindows.cpp` | [M] | 14 | raccourcis globaux via le service ; presse-papiers riche (`services.clipboardSet` + format interne, `App.cpp:497-507`) |
| `src/app/ClipboardFiles.*`, `src/app/TablePaste.*` | [M] | 14 | format riche des éléments ; CSV `;`/TSV/HTML |

### 9.5 Documentation, outillage, tests

| Fichier | Nature | Lot |
|---|---|---|
| `src/hmi/HmiGuideText.cpp`, `src/hmi/HmiGuide.hpp`, `tools/guide-ihm/*.txt`, `src/help/HelpTryIt.cpp`, `src/help/ReleaseNotes.cpp`, `src/help/Novelties.cpp` | [D] | 4, 15 |
| `tools/sessions/*` (sessions qui tapent `F7`, créent des fonctions), `tools/tutoriels/*` | [D]/[T] | 14, 15 |
| `tests/hmi_test.cpp`, `tests/hmi_editor_test.cpp`, `tests/hmi_build_test.cpp`, `tests/hmicheck_expr_test.cpp`, `tests/fold_test.cpp`, `tests/editor_test.cpp`, `tests/simulation_test.cpp` | [T] (compléter, ne rien retirer) | tous |
| `tests/hmi_decl_test.cpp`, `tests/hmi_names_test.cpp`, `tests/hmi_migrate_test.cpp`, `tests/shortcuts_test.cpp` | [T][N] + **une ligne chacun dans `CMakeLists.txt`** (`xpg_add_test_executable` + `xpg_add_test`, sur le modèle des lignes 516-531) | 2–14 |
| `XpgAnalyzer.vcxproj`, `XpgAnalyzer.vcxproj.filters` | [M] | 0 (décision : resynchroniser ou déclarer obsolète) |
| `build.sh` | [M] si conservé | 0 |

---

## 10. Charge relative par module (niveaux du § 23)

Niveaux : **XS** modification locale · **S** petite fonctionnalité isolée · **M** plusieurs
composants · **L** refonte importante · **XL** refonte structurante et transversale. Aucune estimation
en heures. « Révisée » = ce que montre le dépôt ; quand deux niveaux sont donnés, le premier
correspond à la voie recommandée (§ 11), le second à la variante maximale.

### 10.1 Tableau

| # | Fonctionnalité | Spec | **Révisée** | Fichiers / modules principaux | Dépend de | Difficulté | Risque | Refactorisation | Tests nécessaires | Lot (§ 11) |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Modèle générique de script | XL | **L** (façade + sous-modèle) · XL (remplacement des structures) | `HmiModel`, `HmiDecl`[N], `HmiStore`, `HmiDuplicate`, `HmiVersions`, `HmiDossier`, `HmiPackage` | — | Élevée | Élevé (R1) | Moyenne (ajout) · Importante (remplacement) | aller-retour format 23, lecture 19–22, refus 24, `Id` neufs à la duplication | 3 |
| 2 | Suppression et migration des blocs VAR | XL | **XL** (confirmé) | `HmiMigrate`[N], `HmiScript`, `HmiRuntime`, `HmiScriptCheck(110)`, `HmiExprCheck`, `HmiSymbols`, `HmiOperators`, `HmiPipeline`, `HmiTreeData`, `ImportWorkspace`, `HmiAssist`, `RenameDialog`, `sim/Interpreter` (extraction), aides | 1, lexer partagé | Très élevée | Très élevé (R2–R6, R10, R13) | Importante | chaque forme de bloc ; exécution identique avant/après sur `bac/Armoire_Gaz` ; rapport ; annulation ; sauvegarde ; idempotence | 2, 4 |
| 3 | Constantes et variables par interface | L | **L** (confirmé ; IHM seule : M) | `HmiDeclGrid`[N], `HmiScriptPanes`, `HmiFunctionPanes`, `HmiRuntime` (stockage), `HmiRetain`/`HmiSimData` (Persistante), `TablePaste` | 1 ; 7 pour l'accès qualifié | Moyenne | Moyen | Moyenne | +/−/dupliquer/renommer/tri/réordonner, validation immédiate, Excel, Ctrl+Z, persistance, durées de vie à l'exécution | 5 |
| 4 | Fonctions utilisateur | XL | **L** | `HmiDecl`, `HmiFunctionPanes`, `HmiScript`, `HmiRuntime` (un seul chemin d'appel), `HmiScriptCheck`, `ViewModels` | 1, 2, 5 ; 7 pour `Script.F()` | Élevée | Élevé (R14) | Importante (deux chemins d'exécution) | sans argument, sans retour, avec retour, plusieurs arguments, enum et type utilisateur, modes, défauts, récursion bornée, appel depuis *fx* | 8 |
| 5 | Types utilisateur et registre de types | XL | **L** (registre-service à clés stables) · XL (toutes les références persistées en identifiants) | `HmiTypeRegistry`[N], `HmiTypePicker`[N], `HmiPopupParams`, `HmiOperators`, `HmiTypes`, `HmiEnums`, `HmiSimData`, listes de types (6 endroits) | 1 | Élevée | Élevé (règles de conversion unifiées) | Moyenne à importante | matrice de compatibilité, types créés après ouverture, type supprimé/renommé, énumérations, tableaux, DDT | 6 |
| 6 | Surcharges | L–XL | **L** (XL si un typeur d'expressions complet est exigé) | `HmiOverloads`[N], `HmiNames`, `HmiRuntime`, `HmiScriptCheck`, `HmiAssist`, `MultiLineText::Signature`, `HmiStore` | 4, 5, 7 | Élevée | Moyen (R16) | Moyenne | résolue, ambiguë, dupliquée, retour seul différent refusé, conversions, aucune ne correspond | 8 |
| 7 | Résolution des noms qualifiés | XL | **XL** (confirmé) | `HmiNames`[N], `HmiRuntime(+Public)`, `HmiSymbols`, `HmiScriptCheck`, `HmiExprCheck`, `HmiPipeline`, `HmiAssist`, `HmiPublicVars`, `HmiViewPaths`, `HmiExpr` | 1, 5 | Très élevée | Très élevé (R11, R12, R15, R22) | Importante | `Script.Const`, `Inst.Script.Var`, `Vue.Inst.Script.F()`, priorités, alias, collisions, ambiguïtés, contexte d'instance, statique/instance, déplacement | 7 |
| 8 | Explorateurs enrichis | L | **M** (à L avec la synchronisation et les appels) | `ViewModels`, `HmiTreeData`, `ExplorerMenus`, `HmiWorkspace` | 1, 3, 4 ; 7 pour « Appels » | Moyenne | Faible à moyen | Faible | `viewmodel_test` (nœuds, comptes, libellés ; `treednd_test` est cité par CMake mais sa source est absente), menus | 10 |
| 9 | Compilation contextuelle du document actif | M–L | **S à M** | `HmiScriptPanes`, `HmiFunctionPanes`, `HmiEditor`, `HmiPipeline` (portée de la validation), `HmiBuildWorkspace`, `HmiBuildPanes`, `HistoryWorkspace` | — | Faible | Faible | Faible | build ciblé qui n'échoue pas pour un autre élément ; bouton grisé ; info-bulle ; diagnostics filtrés | 1 |
| 10 | Compilation incrémentale | XL | **M** (à L) | `HmiPipeline`, `app/hmi/HmiBuild`, `HmiBuildWorkspace`, `ViewModels` (marques) | 1, 7 | Moyenne | Moyen (R8) | Moyenne | une constante publique modifiée recompile ses utilisateurs, un corps modifié ne recompile pas les appelants, une surcharge ajoutée invalide les appels | 9 |
| 11 | Import/export transactionnel | XL | **L** | `HmiPackage`, `HmiScriptFile`, `HmiImportDialog`, `DropFilesPlan`, `HmiArchive` (SHA-256) | 1, 4, 5, 7 | Moyenne à élevée | Moyen (R7) | Moyenne | valide, invalide (tronqué, empreinte fausse, plus récent), conflit d'identifiant et de nom, export→réimport sans perte, dépendances manquantes, annulation complète | 12 |
| 12 | Génération sécurisée | L–XL | **M** (L si une vraie cible de code est ajoutée) | `HmiPipeline`, `core/AtomicFile`, `HmiBuildPanes`, (`HmiStore`) | 9–10 | Moyenne | Faible à moyen | Faible | échec simulé au N-ième artefact → anciens intacts ; rapport par étape mesurée | 13 |
| 13 | Raccourcis Visual Studio | M | **L** | `platform/InputEvent`, `SdlEventPump`, `ShortcutService`[N], `help/Shortcuts`, `HistoryWorkspace`, `App`, `DetachedWindows`, `MultiLineText`, `TableView`, `ScriptRunner`/`UiDriver`, info-bulles « (F7) », `tools/sessions` | 9 (commandes de compilation) | Moyenne | Moyen (R18–R20) | Importante (traitement éparpillé, ~400 comparaisons de touches / 76 fichiers) | conflits, accords, profils, persistance, dispositions, contexte (F2 grille/éditeur), commandes de ligne | 14 |
| 14 | Presse-papiers externe | S–M | **S à M** (confirmé ; le double format existe déjà pour les objets de vue, `HmiCanvas`) | `MultiLineText`, `App` (`PlatformServices`), `ClipboardFiles`, `TablePaste`, `ExplorerMenus`, patron `HmiCanvas::copySelection/paste` | 1 (éléments structurés) | Faible / Moyenne | Faible | Faible | CRLF/LF/CR, tabulations, Unicode, multiligne, ligne sans sélection, élément riche + texte lisible | 14 |
| 15 | Autocomplétion typée | XL | **L** | `HmiAssist`, `MultiLineText` (signatures multiples), `HmiNames`, `HmiOverloads`, `HmiTypeRegistry` | 5, 6, 7 | Élevée | Moyen (R21) | Moyenne | `suggest()` après `Script.`, `Inst.Script.`, `Vue.Inst.` ; filtrage par type ; visibilité ; argument actif | 11 |
| 16 | Navigation et renommage symbolique | XL | **XL** (confirmé) | `HmiNames` (index des références), `HmiRenameRefs`, `HmiScript`, `HmiSymbols`, `HmiEnums`, `RenameDialog`, éditeurs (F12, Alt+F12, Maj+F12, F2), `GoToSearch` | 7, 8, 13 | Très élevée | Élevé | Importante | renommer fonction/script/constante/variable/paramètre ; cible supprimée → référence cassée réparable ; F12 ouvre la déclaration | 10 |
| + | *Lexer partagé et extracteur sans perte* (préalable, non listé par la spec) | — | **M** | `sim/Interpreter` (API en plus), `HmiDecl`, `HmiTreeData`, `HmiPipeline` | — | Moyenne | Moyen (R17) | Faible | toutes les formes de déclaration ; mêmes résultats que l'existant sur tout le corpus | 2 |
| + | *Diagnostics unifiés* (préalable du § 16) | — | **M** | `HmiDiagnostics`[N], `HmiCheck`, `HmiScriptCheck`, `HmiPipeline`, volets | — | Moyenne | Faible | Moyenne | conversions depuis les 7 anciens types, double-clic → source | 9 |

### 10.2 Pourquoi les révisions (preuves)

- **Fonctions utilisateur XL → L** : le moteur sait déjà tout exécuter (paramètres positionnels et
  nommés, facultatifs avec défaut, `VAR_IN_OUT`/`VAR_OUTPUT`/types riches dans le dialecte, appels
  imbriqués et depuis les expressions en lecture seule, renommage suivi, essai sur banc, export) ;
  les **fonctions de symbole** ont même virtuelles, redéfinitions et `SUPER.` depuis **1.11.10**
  (`HmiSymbols.hpp`). Le travail est le **modèle** (paramètres hors du texte, « Aucun »
  explicite, propriétaire script) et l'**unification des deux chemins d'appel**, pas un moteur neuf.
- **Compilation incrémentale XL → M** : elle **existe** depuis **1.11.13** (clés stables, trois
  empreintes, graphe interface/contenu, cache atomique, états, plan avec fermeture des dépendances,
  annulation, verrou, tests `hmi_build_test`). Restent : l'interface lue dans le modèle, les
  dépendances par clés, un grain plus fin, la portée de la validation.
- **Compilation contextuelle M–L → S–M** : `runHmiBuildFor(Mode::Compile, clé)` + `explicitSelection`
  + fermeture des dépendances **compilent déjà un seul élément** ; il faut retirer `compileHere()` et
  l'ouverture de IHM › Compiler des boutons, limiter la validation à la portée, et poser les libellés.
- **Import/export XL → L** : plan avant action, choix par élément, commande unique annulable,
  refus des formats plus récents et SHA-256 (archive) existent ; manquent les identifiants, les noms
  qualifiés, le manifeste de dépendances, « fusionner/dupliquer », le rapport.
- **Génération L–XL → M** : écriture atomique par fichier, cache en dernier, rapport et journal
  existent ; la « cible » des scripts n'est pas un code généré mais un enregistrement de build
  (artefacts non relus par le moteur, § 4) — sauf décision d'ajouter une vraie cible.
- **Explorateurs L → M** : l'arbre est générique (149 genres sur 256, identifiant `genre|index|sous-index`
  qui suffit pour `(script, rang)` et `(fonction, rang)`), menus par genre, marques d'état et
  commandes de build par nœud existent ; la signature d'une fonction de symbole est déjà affichée.
- **Autocomplétion XL → L** : `HmiAssist` (3 028 l.) gère contexte, membres, fonctions d'instance
  après `Vanne_3.`/`Vue.Vanne_3.`/`SUPER.` (`HmiAssist.cpp:1827-1849`), pastilles nature/provenance,
  description ; il manque la liste des surcharges, le type/défaut/documentation du paramètre actif et
  le filtrage par type — qui dépendent du résolveur et du registre (comptés lignes 5–7).
- **Raccourcis M → L** : la spécification suppose un gestionnaire à brancher ; le dépôt montre un
  traitement **éparpillé** (~400 comparaisons de touches dans 76 fichiers), une énumération de touches
  **incomplète** (pas de B, G, M, T…), **aucun accord** (Ctrl+K est pris avant les widgets),
  **des conflits** avec la table validée par le client, des libellés écrits en dur, et l'éditeur n'a
  ni commandes de ligne, ni rechercher/remplacer, ni aller à la ligne.
- **Confirmés XL** : migration des blocs `VAR` (trois analyseurs de déclarations, deux chemins
  d'exécution, ~170 occurrences dans les tests, aide), noms qualifiés (trois résolutions
  `Vue.Instance.Fonction`, espace de noms global, collisions avec `Vue.Objet.Propriété`), renommage
  symbolique (douze réécritures textuelles, aucun index de références, trou avéré dans
  `renameFunctionEverywhere`).

---

## 11. Plan de migration par lots

### 11.1 Principes (tirés de ce que montre le dépôt)

1. **Le modèle devient la source, le moteur ne change pas d'abord (« pont de reconstruction »).**
   Le § 2 de la spécification demande que « le compilateur et le générateur reconstruisent
   automatiquement les déclarations à partir des modèles ». Le dialecte IHM sait déjà tout lire :
   paramètres dans l'en-tête `FUNCTION Nom(a : T := défaut; VAR_IN_OUT b : U) : R`
   (`Interpreter.cpp:1175-1235`), `VAR CONSTANT` (écriture refusée, `Place{…, constant}`,
   `:2245`), `VAR` conservée / `VAR_TEMP` réinitialisée (`:2238-2244`), paramètres non donnés = valeur
   initiale (`:2911`). **Voie A (recommandée d'abord)** : un `compose(modèle) → texte + décalage de
   lignes` fournit au moteur et aux contrôles le texte qu'ils lisent aujourd'hui ; les diagnostics
   sont ramenés aux lignes du corps. Le ST de l'automate n'est **pas** touché (R17).
   **Voie B (plus tard, facultative)** : passer les `VarDecl` au dialecte comme des données.
2. **L'ancienne syntaxe reste lue** pendant toute la refonte (projets, paquets, `.xpgst`, tests) ;
   elle n'est plus **produite** (modèles de nouvelles fonctions, exports) et un avertissement « à
   migrer » la signale.
3. **Format 23 seulement si nécessaire** (comme 21/22, `HmiStore.cpp:703`) ; une version plus
   ancienne refuse un projet 23.
4. **Chaque lot** : compile (CMake ; décision sur `XpgAnalyzer.vcxproj` au lot 0), passe les essais
   CTest `hmi`, `hmieditor`, `hmibuild`, `hmicheckexpr`, `hmilog`, `hmisimdata`, `hmiretain`,
   `simulation`, `simdebug`, `editor`, `fold`, `viewmodel`, `renameplan`, `renamehmipaths`
   + ses nouveaux tests, donne la liste de ses fichiers, et **ne casse aucun test
   existant** (§ 8.4).
5. **Une commande annulable par geste** (`changeProject`, `GroupCommand`), **une version du projet
   avant toute migration** (`HmiVersions` : « Restaurer » crée d'abord « Avant restauration »).

Ordre : celui de la spécification (§ 23 : 1 audit, 2 modèles, 3 types et noms qualifiés,
4 constantes/variables/références, 5 migration, 6 fonctions, 7 appels et surcharges, 8 compilation
contextuelle, 9 explorateurs et navigation, 10 autocomplétion, 11 import/export, 12 génération,
13 raccourcis, 14 intégration), avec quatre écarts justifiés par le dépôt :
- la **compilation du document actif** (spéc. 8) remonte en **lot 1** : elle est indépendante, à
  faible risque (§ 10, ligne 9) et corrige un comportement trompeur dès maintenant ;
- le **lexer partagé / extracteur sans perte** devient un préalable explicite (**lot 2**) : sans
  lui, la migration reposerait sur `splitDeclarations`, qui perd commentaires, `CONSTANT` et
  `VAR_IN_OUT` (R2–R4) ;
- la **migration** (spéc. 5) passe **avant** le registre de types et les noms qualifiés (spéc. 3) :
  elle n'a besoin que du modèle et de l'extracteur, elle met les vrais projets sur le modèle tôt, et
  les déclarations migrées restent **privées** (décision D11) — donc aucune résolution de nom ne
  change avant le lot 7 ;
- les **onglets** (interface de la spéc. 4) précèdent le registre : ils utilisent d'abord la liste de
  types existante (`params::proposedTypes`) puis le sélecteur du lot 6.
Les **raccourcis** restent en fin (lot 14) : ils touchent tous les éditeurs et une table validée par
le client.

### 11.2 Les lots

#### Lot 0 — Préparation et correctifs indépendants · XS–S
- **But** : corriger ce qui est faux aujourd'hui sans rien changer au modèle ; poser un filet.
- **Contenu** : `renameFunctionEverywhere` et `functionCallers` couvrent aussi `View::functions`,
  `Object::functionOverrides`, `View::operators`, `HmiType::operators`, `View::alarms`
  (`HmiScript.cpp:703-778`) ; commentaire `HmiSymbolFunctions.hpp` (`HmiModel.hpp:725`) ; décision sur
  `XpgAnalyzer.vcxproj`/`build.sh` (resynchroniser ou déclarer obsolètes) ; **trace d'exécution de
  référence** de `bac/Armoire_Gaz` (N cycles de simulation IHM, valeurs des variables IHM et journal)
  enregistrée comme test doré.
- **Fichiers** : `src/hmi/HmiScript.cpp`, `src/hmi/HmiModel.hpp`, `tests/hmi_test.cpp`, (vcxproj).
- **Tests** : renommer une fonction IHM appelée depuis une fonction de symbole, une redéfinition, un
  opérateur ; « Appelé par » les liste ; trace dorée stable.
- **Risques** : faibles. **Compatibilité** : aucun changement de format.

#### Lot 1 — Compiler le document actif · S–M (spéc. § 13)
- **But** : le bouton « Compiler » d'un éditeur compile **l'élément affiché** (et ses dépendances
  strictement nécessaires) ; « Compiler le projet » reste à part.
- **Contenu** : `TCompile` des volets de scripts/fonctions (et des fonctions de symbole, redéfinitions)
  → `runHmiBuildFor(Mode::Compile, clé)` uniquement (retirer `compileHere()` et
  `hosts_.compile()`) ; **phase Validation limitée à la portée** quand la requête est ciblée
  (`HmiPipeline.cpp:1828-1925`) ; info-bulles « Compiler le script actuel » / « Compiler la fonction
  actuelle » ; bouton grisé sans élément (`HmiToolStrip::setEnabledWhen`) ; à la fin, panneau du bas
  **Diagnostics filtré sur l'élément** ; F7 dans un éditeur = le document, ailleurs = le projet ;
  `help::keys` mis à jour.
- **Fichiers** : `HmiScriptPanes.cpp`, `HmiFunctionPanes.cpp`, `HmiEditor.cpp`, `HmiActionDialogs.cpp`,
  `HmiPipeline.hpp/.cpp`, `HmiBuildWorkspace.cpp`, `HmiBuildPanes.cpp`, `HistoryWorkspace.cpp`,
  `help/Shortcuts.cpp`.
- **Tests** : `hmi_build_test` (un élément hors portée en erreur ne fait pas échouer la compilation
  ciblée) ; `hmi_editor_test` (le bouton ne compile que le script affiché ; grisé sans script ; le
  rapport global n'est pas ouvert).
- **Risques** : habitude « F7 = tous les scripts » (documenter). **Compatibilité** : aucune.

#### Lot 2 — Lexer partagé et extracteur de déclarations sans perte · M
- **But** : une seule lecture fiable des déclarations, avant de les déplacer.
- **Contenu** : API **additive** dans `sim` (jetons avec ligne/colonne ; analyse des blocs `VAR*` et
  de l'en-tête `FUNCTION` sans exécuter) ; `hmi::decl::extract` : les cinq sortes, `CONSTANT`,
  `RETAIN`, plusieurs noms par ligne, valeur initiale (texte), **commentaires rattachés**, étendue des
  lignes, fonctions internes, repères `$…$` ; `hmitree::withoutDeclarations`/`signatureOf` et
  `pipeline::signatureOf` réécrits dessus (mêmes résultats).
- **Fichiers** : `src/sim/Interpreter.hpp/.cpp` (ajouts), `src/hmi/HmiDecl.hpp/.cpp` [N],
  `src/app/hmi/HmiTreeData.hpp`, `src/app/screens/ImportWorkspace.cpp`, `src/hmi/HmiPipeline.cpp`,
  `tests/hmi_decl_test.cpp` [N] + `CMakeLists.txt`.
- **Tests** : toutes les formes ; **égalité avec `splitDeclarations`** sur tout le corpus
  (`tests/fixtures/ihm`, `bac/Armoire_Gaz`, `projets/project`) ; `simulation_test`, `simdebug_test`
  inchangés.
- **Risques** : R17 (ne toucher que le dialecte). **Compatibilité** : aucune.

#### Lot 3 — Modèle des déclarations, format 23, pont de reconstruction · L
- **But** : constantes, variables (stockage : Exécution / Conservée / Persistante / Partagée),
  paramètres (mode, défaut), retour explicite (« Aucun »), références externes et documentation
  **dans le modèle**, persistés, et compris par le moteur et les contrôles **sans changer le moteur**.
- **Contenu** : `HmiDecl` attaché à `Script`, `HmiFunction` (projet et symbole), `FunctionOverride`
  (locales), `HmiOperator` (locales) ; lignes du format 23 ; `compose()` (voie A) utilisé par
  `Runtime::prepare`, `checkScript`, `checkFunction`, `functionSignature`/`functionText`,
  `scriptcheck::Scope`, `pipeline::functionIface` ; table de lignes ; un script qui a **à la fois** un
  modèle et un bloc textuel → diagnostic « déclaré deux fois » ; duplication et comparaison de versions.
- **Fichiers** : `HmiModel.hpp/.cpp`, `HmiDecl.*`, `HmiStore.*`, `HmiScript.*`, `HmiRuntime.*`,
  `HmiScriptCheck.*`, `HmiPipeline.*`, `HmiDuplicate.*`, `HmiVersions.cpp`, `HmiPackage.cpp`
  (sérialisation du petit projet), `HmiCommands.*`, `tests/hmi_decl_test.cpp`.
- **Tests** : aller-retour disque (23), lecture des formats 19 et 22 réels, refus de 24 ; **même
  exécution** pour un script à modèle et le même script à blocs textuels ; lignes des diagnostics ;
  `Id` neufs à la duplication ; « Persistante » rendue au redémarrage (via `hmi::retain`/`simdata`,
  clés = identifiants stables).
- **Risques** : R1, R13. **Compatibilité** : un projet sans déclarations modélisées reste au format
  21/22.

#### Lot 4 — Migration des anciens blocs VAR · L–XL (spéc. § 2)
- **But** : convertir sans perte, avec rapport, sauvegarde et annulation.
- **Contenu** : `MigrationService` (`HmiMigrate`) : détecter (projet ouvert, paquet importé,
  `.xpgst`, collage), extraire (lot 2), convertir (lot 3), retirer les blocs du code (commentaires
  conservés en documentation), **rapport** (par script : déclarations, défauts, commentaires, points
  d'attention : `RETAIN`, `VAR_IN_OUT`, fonctions internes, redéfinitions, types inconnus), **version
  « Avant migration des déclarations »** puis **une** commande (Ctrl+Z) ; bandeau à l'ouverture d'un
  projet à migrer ; « Migrer ce script » dans l'éditeur ; aide et guide mis à jour.
- **Fichiers** : `HmiMigrate.*` [N], `HmiVersions.*`, `HmiWorkspace.cpp` (bandeau, rapport),
  `HmiImportDialog.*`, `HmiScriptFile.*`, `HmiPackage.*`, `HmiGuideText.cpp`, `tools/guide-ihm/*`,
  `tests/hmi_migrate_test.cpp` [N].
- **Tests** : chaque forme ; **`bac/Armoire_Gaz` migré complet : trace dorée identique** (lot 0) ;
  idempotence ; annulation qui rend les corps **octet pour octet** ; rapport ; aucune déclaration
  perdue (modèle → `compose()` = original normalisé).
- **Risques** : R2–R6, R10. **Compatibilité** : format 23 après migration ; version de sauvegarde
  restaurable.

#### Lot 5 — Onglets Constantes, Variables, Références, Paramètres, Retour · L (spéc. § 2–5, 19–20)
- **But** : l'éditeur ne montre que la logique ; les déclarations se gèrent dans des grilles.
- **Contenu** : `HmiDeclGrid` générique (TableView + `[+] [-] [Dupliquer] [↑] [↓]`, tri, recherche,
  édition en place avec listes, validation immédiate, erreurs, usages, documentation, Excel
  copier/coller) ; intégré aux volets de scripts et de fonctions, aux fonctions de symbole et
  redéfinitions ; `functionTemplate` sans `VAR` ; barre d'état « Erreurs / Avertissements / Dernière
  compilation ».
- **Fichiers** : `HmiDeclGrid.*` [N], `HmiScriptPanes.*`, `HmiFunctionPanes.*`, `HmiEditor.*`,
  `HmiActionDialogs.*`, `HmiCommands.*`, `TablePaste.*`, `HmiScript.cpp`, `tests/hmi_editor_test.cpp`.
- **Tests** : chaque bouton, Ctrl+Z, Excel (titres FR/EN, refus `#N/A`), persistance, « fermeture avec
  modifications non enregistrées ».
- **Risques** : R21 (perf). **Compatibilité** : inchangée.

#### Lot 6 — Registre de types et sélecteur · L (spéc. § 7)
- **But** : un registre central à clés stables et **une** règle de conversion.
- **Contenu** : `TypeRegistry` (élémentaires, chaînes et durées, tableaux, structures et énumérations
  IHM, DDT de l'API, symboles, références, « Aucun ») ; fusion de `matchCost`, `typeAccepts` et des
  règles de `simdata` (écarts publiés dans les notes de version) ; `HmiTypePicker` (recherche,
  catégories, récents, provenance, définition, invalides, rafraîchi) ; suppression des listes en
  double ; type persistant = nom **et** clé (format 23) pour survivre au renommage.
- **Fichiers** : `HmiTypeRegistry.*` [N], `HmiTypePicker.*` [N], `HmiPopupParams.*`, `HmiOperators.*`,
  `HmiTypes.*`, `HmiEnums.*`, `HmiSimData.cpp`, `HmiScript.hpp`, `HmiModel.hpp`, volets à listes de
  types (`HmiVariablePanes`, `HmiEquipmentPanes`, `HmiWorkspace` ×4, `HmiFunctionPanes`, `HmiAssist`).
- **Tests** : matrice complète de compatibilité ; type créé après ouverture ; type supprimé → invalide
  signalé ; type renommé → suivi par clé.
- **Risques** : programmes acceptés hier refusés (ou l'inverse) → liste des écarts. **Compatibilité** :
  noms inchangés dans les fichiers.

#### Lot 7 — Noms qualifiés, résolveur unique, références externes · XL (spéc. § 5, 8, 12)
- **But** : `Script.X`, `Instance.Script.X`, `Vue.Instance.Script.F(…)` résolus par **un** service à
  identifiants stables, partout.
- **Contenu** : `SymbolResolver` (portées : fonction › script › propriétaire › projet ›
  bibliothèques › système ; alias ; ambiguïté = erreur qui cite les candidats ; visibilité) ;
  remplacement des trois résolutions de `Vue.Instance.Fonction` ; scripts propriétaires de fonctions ;
  variables publiques lues/écrites depuis d'autres scripts (stockage par script, initialisé au
  démarrage) ; références externes (cible = clé + nom de repli ; statuts valide / introuvable / ambiguë
  / incompatible / obsolète / bibliothèque manquante ; réparation par clé) ; **contrôle de collisions**
  sur les projets réels avant activation.
- **Fichiers** : `HmiNames.*` [N], `HmiRuntime.*`, `HmiRuntimePublic.cpp`, `HmiSymbols.*`,
  `HmiScriptCheck.*`, `HmiExprCheck.*`, `HmiPipeline.*`, `HmiExpr.*`, `HmiPublicVars.hpp`,
  `HmiViewPaths.*`, `HmiScriptPanes.*` (onglet Références), `tests/hmi_names_test.cpp` [N].
- **Tests** (§ 24) : appel depuis un autre script, via une instance, propriété qualifiée, appels
  imbriqués, référence manquante, collision, contexte d'instance correct, statique vs instance.
- **Risques** : R11, R12, R15, R22. **Compatibilité** : les appels non qualifiés d'aujourd'hui
  (fonctions du projet) restent valides.

#### Lot 8 — Fonctions utilisateur complètes et surcharges · L (spéc. § 6, 9, 10)
- **But** : fonctions avec modes, défauts, « Aucun », locales/constantes/références, surcharges.
- **Contenu** : `OverloadResolver` (coûts du registre, ambiguïté, « aucune ne correspond » avec la
  liste) ; **un seul chemin d'appel** (dialecte) ; récursion bornée configurable (décision D6) ;
  onglets Surcharges et Appels ; `MultiLineText::Signature` → **liste** de signatures + argument actif.
- **Fichiers** : `HmiOverloads.*` [N], `HmiDecl.*`, `HmiRuntime.*`, `HmiScriptCheck.*`, `HmiStore.*`
  (homonymes à signatures différentes), `HmiFunctionPanes.*`, `ui/widgets/Controls.*`, `HmiAssist.*`.
- **Tests** (§ 24) : sans argument, sans retour, avec retour, plusieurs arguments, enum et type
  utilisateur, surcharge résolue, ambiguë, signature dupliquée, type incompatible, récursion.
- **Risques** : R14, R16. **Compatibilité** : format 23.

#### Lot 9 — Compilation incrémentale fine et diagnostics unifiés · M–L (spéc. § 16)
- **Contenu** : interface des fonctions depuis le modèle ; dépendances par clés du résolveur ;
  éléments « fonction de script » et ensembles de surcharges ; `kGeneratorVersion` monté ;
  **un** `Diagnostic` (code, niveau, message, script, fonction, ligne, colonne, longueur,
  identifiant, suggestion, action rapide) avec conversions depuis les anciens types.
- **Fichiers** : `HmiPipeline.*`, `HmiDiagnostics.hpp` [N], `HmiCheck.*`, `HmiScriptCheck.*`,
  `HmiBuildPanes.*`, volets d'édition, `tests/hmi_build_test.cpp`.
- **Tests** : invalidations fines (constante publique, corps, surcharge ajoutée) ; commandes
  « diagnostic suivant / précédent » (leurs touches F8/Maj+F8 au lot 14 : F8 démarre aujourd'hui l'IHM).
- **Risques** : R8.

#### Lot 10 — Explorateurs, navigation, renommage symbolique · L–XL (spéc. § 3, 11, 14)
- **Contenu** : nœuds Constantes / Variables / Références / Fonctions (surcharges groupées) /
  Diagnostics sous un script, Paramètres / Retour / Locales / Constantes / Références / Appels sous une
  fonction (genres **ajoutés en fin** de `NodeKind`) ; menus (copier le nom qualifié, atteindre la
  définition, rechercher les références, compiler, exporter, propriétés) ; synchronisation
  éditeur ↔ arbre ; F12 / Alt+F12 / Maj+F12 par l'**index des références** ; **renommage par
  références** (fonction, script — y compris `IHM_APPELER('X')` —, constante, variable, paramètre)
  avec l'aperçu de `RenameDialog` ; références cassées après suppression, réparables.
- **Fichiers** : `ViewModels.*`, `HmiTreeData.hpp`, `ExplorerMenus.cpp`, `HmiWorkspace.cpp`,
  `RenameDialog.*`, `HmiRenameRefs.*`, `HmiScript.*`, `HmiSymbols.*`, `HmiEnums.*`, `GoToSearch.*`,
  `GoToWorkspace.cpp`, `ui/widgets/Controls.*` (historique de navigation).
- **Tests** (§ 24) : renommer fonction / script / constante ; supprimer une cible référencée ;
  `viewmodel_test`, `renameplan_test`, `rename_hmi_paths_test`.

#### Lot 11 — Autocomplétion typée · L (spéc. § 21)
- **Contenu** : propositions après `Vue.Instance.`, `Vue.Instance.Script.`, `Script.` ; filtrage par
  contexte, type attendu, visibilité, portée, bibliothèque ; surcharges, argument actif, type,
  documentation, défaut, erreurs de compatibilité en direct.
- **Fichiers** : `HmiAssist.*`, `ui/widgets/Controls.*`, `HmiNames.*`, `HmiOverloads.*`.
- **Tests** : `suggest()` par contexte ; temps de réponse mesuré sur `bac/Armoire_Gaz`.

#### Lot 12 — Import/export transactionnel · L (spéc. § 18)
- **Contenu** : manifeste (format, identifiants, noms qualifiés, dépendances requises, types
  référencés, **SHA-256** par fichier, informations facultatives séparées) ; comparaison par
  identifiant puis nom ; remplacer / fusionner / dupliquer / ignorer / renommer ; dépendances
  manquantes ; migration à l'import ; rapport conservé ; `.xpgst` format 2.
- **Fichiers** : `HmiPackage.*`, `HmiScriptFile.*`, `HmiImportDialog.*`, `DropFilesPlan.cpp`,
  `HmiArchive.*` (réemploi de l'empreinte).
- **Tests** (§ 24) : import valide, invalide, conflit, export puis réimport sans perte.

#### Lot 13 — Génération sécurisée · M (spéc. § 17)
- **Contenu** : étapes nommées et **mesurées** ; artefacts écrits dans un dossier temporaire puis
  **bascule** après réussite ; rapport (cible, générés, ignorés, avertissements, erreurs, dépendances
  manquantes, chemins, durées) ; décision de vocabulaire « Générer » (validation) vs génération (D7).
- **Fichiers** : `HmiPipeline.*`, `core/AtomicFile.*`, `HmiBuildPanes.*`.
- **Tests** : échec injecté au N-ième artefact → artefacts précédents intacts ; rapport complet.

#### Lot 14 — Raccourcis, éditeur, presse-papiers · L + S–M (spéc. § 13–15)
- **Contenu** : touches manquantes et **accords** (`ui::Key`, `SdlEventPump`, `UiDriver::parseKey`) ;
  `ShortcutService` (commandes nommées, contextes, profils « actuel » et « Visual Studio », conflits,
  restauration, persistance `Settings`, libellés des menus et info-bulles) ; `handleShortcut`,
  `App`, `DetachedWindows` passent par lui ; `MultiLineText` : couper/copier la ligne, dupliquer,
  déplacer, commenter/décommenter, indenter, aller à la ligne, rechercher/remplacer, repli Ctrl+M ;
  presse-papiers : ligne sans sélection (réglable), format riche des éléments + texte lisible, CR seul.
- **Fichiers** : `platform/InputEvent.hpp`, `platform/SdlEventPump.cpp`, `app/ShortcutService.*` [N],
  `help/Shortcuts.*`, `screens/HistoryWorkspace.cpp`, `App.cpp`, `DetachedWindows.cpp`,
  `ui/widgets/Controls.*`, `ui/widgets/DataViews.*`, `ScriptRunner.cpp`, `ClipboardFiles.*`,
  `TablePaste.*`, `tools/sessions/*`, `tests/shortcuts_test.cpp` [N], `tests/editor_test.cpp`,
  `tests/fold_test.cpp`.
- **Tests** (§ 24) : copier/coller externe, raccourcis selon le contexte, annulation/rétablissement.
- **Risques** : R18–R20.

#### Lot 15 — Intégration et migration complète · M
- **Contenu** : `bac/Armoire_Gaz` et `projets/project` migrés et comparés à la trace dorée ; aide,
  guide, tutoriels, notes de version, sessions de captures ; vérification des critères du § 25.

### 11.3 Décisions à prendre avant le lot 3

| # | Question | Proposition |
|---|---|---|
| D1 | Voie A (reconstruction textuelle) ou B (déclarations passées au dialecte) | **A d'abord**, B plus tard si le coût le justifie |
| D2 | Fonctions internes `FUNCTION…END_FUNCTION` des scripts | migrées en fonctions **privées** du script (pas de nouvelle collision) |
| D3 | `VAR RETAIN` (aujourd'hui sans effet) | « Conservée » + note du rapport ; « Persistante » seulement sur choix explicite |
| D4 | `.xpgst` (texte pour éditeurs externes) | déclarations en en-tête `(*# … *)`, corps sans `VAR` |
| D5 | Profil de raccourcis par défaut | « XPGAnalyser actuel » (table validée le 02/10) + profil « Visual Studio » ; F12 capture déplacée si VS |
| D6 | Récursion | permise, profondeur bornée et réglable (la maquette propose 32 ; aujourd'hui `kMaxDepth = 32` côté moteur IHM, `maxCallDepth = 64` côté dialecte) |
| D7 | Le mot « Générer » | réserver « Générer » à la production ; « Vérifier la cohérence » pour l'actuel IHM › Générer |
| D8 | Retour d'une fonction | `RETURN valeur;` (déjà compris par le dialecte) **et** `Nom := valeur;` gardé |
| D9 | `XpgAnalyzer.vcxproj` | resynchroniser au lot 0 ou le déclarer obsolète (la CMake ramasse les fichiers par glob) |
| D10 | Scripts de vue : nom généré `Vue_Événement` | nom **dérivé** (la vue + l'événement) au lieu d'un nom figé, pour des noms qualifiés justes (§ 7.6) |
| D11 | Visibilité par défaut | **nouvelles** déclarations : Publique (la demande) ; déclarations **migrées** : Privée (comportement actuel : une locale n'est vue que de son script, `HmiScript.hpp:82`), à publier depuis le rapport |
| D12 | Vocabulaire de la maquette | la maquette parle d'un « format 2 » des scripts : dans le dépôt, c'est le **format IHM 23** (`kFormatVersion` est un compteur unique pour tout le dossier `ihm/`) |

### 11.4 Correspondance avec les tests obligatoires (§ 24)

| Test exigé | Lot |
|---|---|
| script sans déclaration ; avec constantes ; avec variables ; avec référence externe | 3, 5, 7 |
| fonction sans argument / sans retour / avec retour / plusieurs arguments / enum utilisateur / type personnalisé | 8 |
| appel depuis un autre script ; via une instance de symbole ; propriété qualifiée ; appels imbriqués | 7 |
| surcharge résolue ; ambiguë ; signature dupliquée ; type incompatible ; référence manquante | 7, 8 |
| renommage d'une fonction / d'un script / d'une constante ; suppression d'une cible référencée | 0 (correctif), 10 |
| compilation du document actif uniquement | 1 |
| import valide / invalide / conflit / export puis réimport sans perte | 12 |
| migration d'un ancien bloc VAR | 2, 4 |
| copier/coller externe ; raccourcis selon le contexte ; annulation et rétablissement | 14 (et 5 pour Ctrl+Z des grilles) |
| fermeture avec modifications non enregistrées | 5 (existe déjà : Ctrl+W pose la question, `HistoryWorkspace.cpp:388-393`) |

---

*Fin de l'analyse. Points explicitement non vérifiés : la conversion LF→CRLF de
`SDL_SetClipboardText` sous Windows ; le comportement de `SDLK_MINUS`/`SDLK_COMMA` sur AZERTY ; la
chaîne réellement utilisée pour produire les installateurs (CMake/mingw présumée) ; l'absence de
toute lecture des artefacts de build par le moteur (aucune trouvée par recherche).*
