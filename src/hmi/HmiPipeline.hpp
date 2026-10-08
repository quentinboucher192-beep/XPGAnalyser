// =============================================================================
//  hmi/HmiPipeline.hpp - 1.11.13 : la generation incrementale de l'IHM
// -----------------------------------------------------------------------------
//  Le demarrage de la simulation IHM devient un BUILD : analyse des
//  modifications, generation de l'API (ce que l'IHM en voit), generation de
//  l'IHM (16 etapes), compilation, validation. Seul ce qui a change - et ce
//  qui en depend vraiment - est refait ; un projet a jour ne refait rien.
//
//  LES ELEMENTS. Chaque chose qui se genere ou se compile a une cle stable
//  ("vue:12", "script:45", "fonction-symbole:31", "api-var:Pression_Gaz"), un
//  chemin ("IHM/Vues/Vue_Vannes/Scripts/OnCycle") et trois textes canoniques :
//    - son CONTENU (ce qui change ce qui s'execute ou s'affiche),
//    - sa CONFIGURATION (reglages : taille, fond, adresse...),
//    - son INTERFACE (ce que les autres en voient : un nom et un type, une
//      signature) - une dependance d'interface n'est refaite que si
//      l'interface change (le corps d'une fonction change : ses appelants
//      restent a jour) ; une dependance de CONTENU (une vue et son modele, une
//      instance et son symbole) suit tout.
//  La documentation (description, commentaire, dossier) n'entre dans aucun :
//  la changer ne refait rien.
//
//  LE CACHE (.xpg/build/build-cache.txt) garde, par element, ses empreintes,
//  celles de ses dependances, la version du generateur, ses etats, ses
//  diagnostics et son artefact. Ecrit d'un bloc (un .tmp verifie, puis
//  renomme ; l'ancien garde en .bak) ; une derniere ligne "fin" : un fichier
//  coupe est refuse. Absent, illisible ou d'une autre version : tout est
//  refait, et la raison est dite.
//
//  LES ARTEFACTS (.xpg/build/api/... et ihm/...) : la forme generee de chaque
//  element (une vue : ses instances de symboles deballees), au format texte
//  des fichiers IHM, ecrits de la meme facon. Un artefact absent ou altere
//  (empreinte) fait regenerer son element.
//
//  SANS ECRAN, SANS FIL : run() travaille sur une copie du projet (l'ecran le
//  lance dans un fil a part et lit la progression) ; `cancel` est regarde entre
//  deux taches - un build annule ne laisse jamais un artefact a moitie ecrit.
// =============================================================================
#pragma once

#include "HmiCheck.hpp"
#include "HmiModel.hpp"
#include "../core/Result.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hmi::pipeline {

// ---- les elements -------------------------------------------------------------
enum class Area : std::uint8_t { Api, Ihm };
enum class ElementKind : std::uint8_t {
    ApiConfig, ApiVariable, ApiType, ApiExchange,          // B. l'API vue par l'IHM
    Variable, Displays,                                    // C1. variables IHM (et leurs formats)
    Function,                                              // C2. fonctions IHM
    Type,                                                  // C3. types IHM
    Symbol, SymbolFunction,                                // C4. symboles et leurs fonctions
    Script,                                                // C5. scripts generaux
    Resource, Languages,                                   // C6. ressources, fichiers externes, langues
    ViewTemplate, Styles,                                  // C7. modeles de vues (ecrans modeles, en-tetes, pieds), styles
    View, ViewScript,                                      // C8. vues et leurs scripts
    Popup,                                                 // C9. popups
    Animations,                                            // C10. les expressions d'une vue
    Actions,                                               // C11. les actions d'une vue
    Alarm, AlarmSettings,                                  // C12. alarmes, reglages, groupes, notifications
    Recipe,                                                // C13. recettes
    Security,                                              // C14. utilisateurs et securite
    History,                                               // C15. historiques et rapports
    SimConfig,                                             // C16. configuration de simulation (equipements, poste...)
};
[[nodiscard]] Area             areaOf(ElementKind) noexcept;
[[nodiscard]] int              stepOf(ElementKind) noexcept;   // API : 0..3 ; IHM : 0..15
[[nodiscard]] std::string_view stepName(Area, int step) noexcept;
[[nodiscard]] std::string_view kindLabel(ElementKind) noexcept;   // "Vue", "Script de vue"...
[[nodiscard]] std::string_view kindKey(ElementKind) noexcept;     // "vue", "script-vue"... (le cache)
[[nodiscard]] std::optional<ElementKind> kindFromKey(std::string_view) noexcept;
[[nodiscard]] bool             compilable(ElementKind) noexcept;  // a du code (scripts, fonctions, expressions...)
inline constexpr int kApiSteps = 4;
inline constexpr int kIhmSteps = 16;

enum class DepMode : std::uint8_t { Interface, Content };
struct Dependency {
    std::string name;          // ce que l'element cite
    std::string key;           // l'element trouve ; vide : introuvable
    DepMode     mode{DepMode::Interface};
    std::string via;           // "modele", "instance", "code", "popup"... (le journal)
};

struct Element {
    std::string key;           // identifiant stable
    ElementKind kind{ElementKind::View};
    std::string name;
    std::string path;          // chemin logique
    Id          id{kNoId};     // l'objet du modele (vue, script, fonction, alarme...) ; kNoId : l'API, un ensemble
    Id          owner{kNoId};  // la vue (ou le symbole) qui le porte
    std::string content, config, iface;
    std::vector<Dependency> deps;
};

// Ce que l'IHM voit de l'automate : fourni par l'appelant (le module IHM ne lit
// pas le projet automate lui-meme), comme NameExists.
struct ApiInfo {
    std::string config;        // la CPU, les taches... : un changement refait l'etape B1
    struct Var { std::string name, type, address; };
    std::vector<Var> variables;   // les variables globales
    struct Type { std::string name, definition; };
    std::vector<Type> types;      // les types derives (DDT)
};

// Tous les elements du projet (et ceux de l'API qu'il utilise), dans l'ordre du
// build : API puis IHM, etape par etape. Leurs dependances sont resolues.
[[nodiscard]] std::vector<Element> collect(const Project&, const ApiInfo& = {});

// ---- les etats --------------------------------------------------------------
enum class State : std::uint8_t {
    UpToDate, Modified, NotGenerated, GenerationRequired, Generating, Generated, GenerationFailed,
    CompilationRequired, Compiling, Compiled, CompilationFailed, InvalidDependency, Obsolete,
};
[[nodiscard]] std::string_view   stateLabel(State) noexcept;     // "A jour", "Modifie"... (accentues)
[[nodiscard]] std::string_view   stateKey(State) noexcept;       // le cache
[[nodiscard]] std::optional<State> stateFromKey(std::string_view) noexcept;

enum class Severity : std::uint8_t { Information, Success, Warning, Error, Critical };
[[nodiscard]] std::string_view severityLabel(Severity) noexcept;
struct Diagnostic {
    std::string id;            // "D12"
    Severity    severity{Severity::Error};
    std::string code;          // "E201", ou la categorie du controle ("Script")
    std::string message;
    std::string category;      // "Script", "Expression", "Validation"...
    std::string element;       // la cle de l'element
    std::string path;          // son chemin
    std::string file;          // le document source (ihm/vues/0003-Vue.vue...)
    int         line{0}, column{0}, length{0};
    std::string date;          // "2026-10-08 10:42:09"
    std::string step;          // "Génération", "Compilation", "Validation"
    std::string suggestion;
    // aller a la source (l'ecran), comme hmi::Issue
    Id          view{kNoId}, object{kNoId}, script{kNoId}, item{kNoId};
    std::string property;
    [[nodiscard]] bool blocking() const noexcept { return severity == Severity::Error || severity == Severity::Critical; }
};
[[nodiscard]] Diagnostic fromIssue(const Issue&, std::string_view step);

// ---- le cache ---------------------------------------------------------------
inline constexpr std::string_view kGeneratorVersion = "1";   // change : tout est obsolete (horloge)
inline constexpr int              kCacheFormat = 1;
struct CacheEntry {
    std::string key, kind, path;
    std::string contentHash, configHash, depsHash, ifaceHash;
    std::string failedHash;                     // le contenu qui a echoue (vide : aucun echec)
    std::map<std::string, std::string> depHashes, depSigs;   // dependance -> empreinte, -> interface lisible
    std::string lastValid, lastGenerated, lastCompiled;      // "2026-10-08 10:42:09"
    std::string generator;
    State       generation{State::NotGenerated};
    State       compilation{State::CompilationRequired};    // sans code : Compiled
    std::vector<Diagnostic> diagnostics;
    std::string artifact, artifactHash;
};
struct Cache {
    enum class Status : std::uint8_t { Ok, Absent, Corrupt, Incompatible };
    Status      status{Status::Absent};
    std::string why;                            // la raison (francais) quand ce n'est pas Ok
    std::string generator, written;
    std::map<std::string, CacheEntry> entries;
    std::vector<Diagnostic> project;            // la validation, hors de tout element (la vue de demarrage...)
};
[[nodiscard]] std::string serialize(const Cache&);
[[nodiscard]] Cache       parseCache(std::string_view text);
// `buildFolder` : <projet>/.xpg/build
[[nodiscard]] Cache        loadCache(const std::string& buildFolder);
[[nodiscard]] core::Status saveCache(const Cache&, const std::string& buildFolder);
[[nodiscard]] std::string  buildFolderOf(const std::string& projectFolder);   // "<projet>/.xpg/build"

// ---- l'analyse ----------------------------------------------------------------
enum class Status : std::uint8_t {
    UpToDate, Full, Added, Modified, Renamed, Obsolete, ArtifactMissing, Failed, MissingDependency,
    DependencyFailed, NeedsCompile,
};
struct Item {
    std::size_t element{0};                     // dans le vecteur des elements
    std::string contentHash, configHash, ifaceHash, depsHash;
    std::map<std::string, std::string> depHashes, depSigs;
    Status      status{Status::UpToDate};
    std::vector<std::string> reasons;           // en francais
    std::vector<std::string> missing;           // les noms introuvables
};
struct Counts { std::size_t added{0}, modified{0}, renamed{0}, deleted{0}, obsolete{0}, artifacts{0}, failed{0}, missing{0}, upToDate{0}; };
struct Analysis {
    std::vector<Element>  elements;
    std::vector<Item>     items;                // un par element, dans le meme ordre
    std::map<std::string, std::size_t> byKey;   // cle -> rang
    std::vector<CacheEntry> deleted;            // dans le cache, plus dans le projet
    std::string           fullReason;           // non vide : tout est a refaire (cache absent...)
    Counts                counts;
    [[nodiscard]] const Item*    item(std::string_view key) const;
    [[nodiscard]] const Element* element(std::string_view key) const;
    [[nodiscard]] bool           upToDate() const;   // rien a faire
};
// L'artefact est-il la, avec cette empreinte ? (le disque ; un essai peut tricher)
using ArtifactCheck = std::function<bool(const std::string& relativePath, const std::string& hash)>;
[[nodiscard]] ArtifactCheck diskArtifacts(const std::string& buildFolder);
[[nodiscard]] Analysis analyse(std::vector<Element> elements, const Cache&, const ArtifactCheck&);

// Ce que montrent l'arbre et l'editeur pour un element.
struct Shown {
    State       state{State::NotGenerated};
    int         errors{0}, warnings{0};
    std::string detail;        // "compilation requise", "generation requise"...
    std::string tip;           // l'infobulle (la raison)
};
[[nodiscard]] Shown shown(const Analysis&, const Cache&, std::string_view key);

// ---- les commandes ------------------------------------------------------------
enum class Mode : std::uint8_t { Generate, Regenerate, Compile, GenerateCompile, RegenerateCompile, Start, Clean };
[[nodiscard]] std::string_view modeLabel(Mode) noexcept;   // "Générer", "Régénérer"...
struct Request {
    Mode mode{Mode::Generate};
    // Des cles d'elements, ou des debuts de chemin ("IHM/Vues", "IHM") ; vide : tout le projet.
    std::vector<std::string> scope;
    // Un choix explicite (un element, pas un dossier) : Compiler le recompile meme a jour.
    bool explicitSelection{false};
    // 1.11.15 : la phase G d'un Demarrer - ce que la remanence de simulation rendra au
    // demarrage ("donnees du 2026-10-08 22:10:05 : rendues au demarrage") ; vide : rien a
    // restaurer, ou (restoreOff) la remanence est decochee.
    std::string restore{};
    bool        restoreOff{false};
};
struct Plan {
    std::vector<std::size_t> generate, compile;   // rangs dans Analysis::elements, dans l'ordre
    std::size_t reused{0};
};
[[nodiscard]] bool inScope(const Element&, const std::vector<std::string>& scope);
[[nodiscard]] Plan plan(const Analysis&, const Request&);

// ---- l'execution ------------------------------------------------------------
enum class Phase : std::uint8_t { Analyse, Api, Ihm, Compile, Validate, Start, Restore, Count };
[[nodiscard]] std::string_view phaseLabel(Phase) noexcept;   // "Analyse des modifications"...
enum class PhaseState : std::uint8_t { Todo, Running, Done, Skipped, Failed, Cancelled, NotAsked };
struct Progress {
    std::size_t total{0}, done{0};
    Phase       phase{Phase::Analyse};
    std::string step;          // "C. Génération de l'IHM › 8. Vues"
    std::string element;       // le chemin de l'element en cours
    int         warnings{0}, errors{0};
    PhaseState  phases[static_cast<int>(Phase::Count)]{};
    std::string phaseNotes[static_cast<int>(Phase::Count)];
    struct Sub { int todo{0}, done{0}, failed{0}; };
    Sub         api[kApiSteps]{}, ihm[kIhmSteps]{};
    int         currentSub{-1};            // l'etape IHM en cours (-1 : aucune)
    // l'etat transitoire des elements (Generation en cours, Compilation en cours...)
    std::map<std::string, State> live;
};
struct LogLine { Severity severity{Severity::Information}; std::string category, message, element; int line{0}, column{0}; };
struct Options {
    std::string buildFolder;                    // vide : rien n'est ecrit (un essai en memoire)
    std::string projectFolder;                  // les fichiers externes relatifs (Generer)
    NameExists  plcHasName;
    exprcheck::PlcPaths plcPaths;
    const comm::Plan* commPlan{nullptr};        // le plan Modbus (Generer)
    ArtifactCheck artifacts;                    // vide : le disque (buildFolder)
    std::function<void(const std::string& path, const std::string& text)> writeArtifact;   // vide : le disque
    std::function<void(const std::string& path)> removeArtifact;                          // vide : le disque
    std::string now;                            // l'horodatage (vide : l'heure)
    const Cache* cache{nullptr};                // le cache de depart (vide : celui du disque, ou aucun sans buildFolder)
};
struct Report {
    bool        ok{false}, cancelled{false}, upToDate{false}, locked{false};
    int         errors{0}, warnings{0};
    std::size_t generated{0}, compiled{0}, reused{0}, tasks{0};
    double      ms{0};
    std::string fullReason;
    std::vector<Diagnostic> diagnostics;        // ceux de ce build (generation, compilation, validation)
    std::vector<LogLine>    log;                // ce que disent les Sorties
    Cache       cache;                          // le cache apres le build
    Analysis    analysis;                       // l'analyse de depart
};
using ProgressFn = std::function<void(const Progress&)>;
[[nodiscard]] Report run(const Project&, const ApiInfo&, const Request&, const Options&,
                         const ProgressFn& onProgress = {}, const std::atomic<bool>* cancel = nullptr);

// ---- 1.11.15 : L'ARRET SUR MODIFICATION (la regle ; l'ecran l'applique en marche) ----
//  Au demarrage de la simulation : l'empreinte d'execution de chaque element (son
//  contenu, sa configuration, son interface - pas ses dependances : un script
//  modifie change, ce qui l'appelle non). A chaque analyse en marche : ce qui a
//  change ou disparu depuis. Une modification du DEVELOPPEUR qui en change une
//  arrete la simulation ; une description, un dossier (aucune empreinte ne change)
//  ou ce que la simulation ecrit elle-meme (`developer` faux : une recette, un
//  utilisateur, un forcage de jumeau) la laissent tourner. (1.11.16 : sortie de
//  l'ecran pour etre essayee.)
using RunPrints = std::unordered_map<std::string, std::string>;
[[nodiscard]] RunPrints runPrints(const Analysis&);
struct RunChange {
    bool                     stop{false};
    std::vector<std::string> changed, removed;   // les cles, triees
    std::size_t              compile{0}, generate{0};
    std::string              head;               // "2 elements modifies : 1 a compiler, 1 a generer"
    std::string              card;               // head, puis 3 lignes au plus ("· Plus (Script general, a compiler)")
    std::vector<std::string> paths;              // les chemins (les Sorties)
};
[[nodiscard]] RunChange runChange(const RunPrints& atStart, const std::unordered_map<std::string, std::string>& pathsAtStart,
                                  const Analysis& now, bool developer);

// ---- le verrou (plusieurs instances) -------------------------------------------
//  build.lock : le PID et l'heure de l'instance qui construit. Un verrou de plus
//  de 10 minutes, ou d'un PID mort, est repris.
struct Lock {
    bool        held{false};
    std::string owner;         // ce que dit le verrou d'un autre ("PID 4120, depuis 10:38")
};
[[nodiscard]] Lock acquireLock(const std::string& buildFolder);
void               releaseLock(const std::string& buildFolder);

// ---- utilitaires (exposes pour les essais) ----------------------------------
[[nodiscard]] std::string hash(std::string_view text);   // FNV-1a 64 bits, 16 chiffres hexadecimaux
[[nodiscard]] std::string signatureOf(std::string_view body);   // le bloc VAR_INPUT d'un corps, normalise
[[nodiscard]] std::vector<std::string> identifiers(std::string_view code);   // hors chaines et commentaires

} // namespace hmi::pipeline
