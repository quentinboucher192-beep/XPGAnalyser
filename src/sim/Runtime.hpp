// =============================================================================
//  sim/Runtime.hpp — the simulated PLC
// -----------------------------------------------------------------------------
//  WHAT THIS IS. A deterministic execution of the project's Structured Text over
//  a variable table, one scan at a time, with a simulated clock. It answers the
//  question "does this logic do what I meant" - which is what was asked for.
//
//  WHAT IT IS NOT, said plainly so nobody is surprised in front of a machine:
//    * not cycle-accurate. A scan takes as long as it takes here; the simulated
//      clock advances by whatever the caller says, not by the real elapsed time.
//    * no real I/O. Inputs come from forcing; outputs are written by the program
//      and observed here, exactly as the user asked for.
//    * ST only. LD, FBD and SFC sections are reported as unsupported rather than
//      quietly skipped, because a simulation missing a third of the program
//      would be worse than no simulation.
//
//  FORCING. A forced variable ignores writes from the program and always reads
//  back its forced value - the same contract as a PLC's forcing table. %Q may be
//  written by the program and forced too; that is what makes it possible to test
//  a sequence without the machine.
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"
#include "Interpreter.hpp"

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sim {

struct ScanReport {
    std::uint32_t             scan{0};
    std::int64_t              clockMs{0};
    std::uint32_t             statements{0};
    std::uint32_t             sectionsRun{0};
    std::vector<Diagnostic>   diagnostics;
    bool                      halted{false};   // an error stopped the scan
    // Lot API 8 : faux apres un pas par entree (stepEntry) qui n'a pas fini le
    // cycle : `scan` est alors le cycle en cours, pas encore compte.
    bool                      completed{true};
};

// One recorded sample, for the trend window.
struct Sample { std::int64_t clockMs; double value; };

// ---- Lot API 8 : le debogage et la modification en ligne ------------------------
// Un point d'arret, tel que le runtime le suit (setBreakpoints).
struct Breakpoint {
    std::uint32_t id{0};
    // "Section", "Unite.Section" (une section d'unite ; son nom seul s'il est
    // unique) ou "BLOC.Section" (le corps d'un DFB) - sans la casse.
    std::string   section;
    std::uint32_t line{0};            // demandee (1 = la premiere)
    std::string   condition;          // ST facultatif : "Armoires[0].etat = 3"
    bool          enabled{true};
    std::uint64_t hits{0};            // les passages (condition vraie)
    // Ce que le runtime en a compris :
    std::uint32_t effectiveLine{0};   // la ligne ou il s'arrete (0 : nulle part)
    std::string   note;               // en francais : decale, condition invalide, section introuvable
};
// Le passage d'un point d'arret : ou, quand, les valeurs de la ligne, la pile.
struct BreakHit {
    std::uint32_t id{0};
    std::string   section;
    std::uint32_t line{0};
    std::uint64_t scan{0};
    std::vector<std::pair<std::string, std::string>> values;
    std::vector<std::string> stack;   // MAST > unite > section > instance (TYPE) > TYPE.Section
};
// Ou passe le temps : une section de MAST (blocs appeles compris), au dernier cycle complet.
struct SectionTime {
    std::string   entry, section;
    std::uint64_t statements{0};
    std::int64_t  micros{0};
    // 1.10.2 : la condition d'activation (vide : aucune) et, au dernier
    // passage, si elle etait vraie - fausse, la section n'a pas tourne.
    bool          active{true};
    std::string   condition;
};
// Qui a ecrit : la derniere ecriture d'une case. `section` vide : hors du
// programme (l'IHM, un script, l'ecran).
struct WriteSite {
    std::string   section;
    std::uint32_t line{0};
    std::uint64_t scan{0};
};
// Ce que la modification en ligne a garde.
struct Adoption {
    std::size_t kept{0}, added{0}, dropped{0};
};

class Runtime final : public Environment {
public:
    explicit Runtime(std::shared_ptr<const domain::Project> project);

    // Builds the variable table and parses every ST section of `task`. Parse
    // errors and unsupported languages come back as diagnostics; the runtime is
    // still usable for whatever did compile.
    [[nodiscard]] core::Result<void> prepare(std::string_view task = "MAST");

    // Advances the simulated clock and runs one scan of the task.
    // Lot API 8 : un cycle commence par stepEntry() est fini (l'horloge a deja
    // avance) - le reste de ses entrees.
    ScanReport step(std::int64_t deltaMilliseconds = 20);
    void       reset();

    // ---- the variable table -------------------------------------------------
    [[nodiscard]] std::vector<std::string> names() const;
    [[nodiscard]] bool  get(std::string_view name, Value& out) const;
    [[nodiscard]] bool  set(std::string_view name, const Value& v);   // ignores forcing
    [[nodiscard]] bool  known(std::string_view name) const;

    // ---- forcing -------------------------------------------------------------
    bool force(std::string_view name, const Value& v);
    bool unforce(std::string_view name);
    void unforceAll();
    [[nodiscard]] bool isForced(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> forcedNames() const;

    // The forcing configuration as text: one `name = value` per line. A test
    // scenario is worth keeping and worth sharing, and a file that can be read
    // and edited in Notepad is worth more than a binary one.
    [[nodiscard]] std::string  exportForcing() const;
    [[nodiscard]] std::size_t  importForcing(std::string_view text,
                                             std::vector<std::string>* unknown = nullptr);

    // ---- trends --------------------------------------------------------------
    void watch(std::string_view name);
    void unwatch(std::string_view name);
    [[nodiscard]] const std::deque<Sample>* history(std::string_view name) const;
    void setHistoryDepth(std::size_t samples) { historyDepth_ = samples; }

    [[nodiscard]] std::int64_t clockMs() const noexcept { return clockMs_; }
    [[nodiscard]] std::uint32_t scanCount() const noexcept { return scans_; }
    [[nodiscard]] const std::vector<Diagnostic>& preparationDiagnostics() const noexcept {
        return prepareDiagnostics_;
    }

    // Environment
    bool read(std::string_view name, Value& out) override;
    bool write(std::string_view name, const Value& v) override;
    [[nodiscard]] bool exists(std::string_view name) override;
    bool assignAggregate(std::string_view target, std::string_view source) override;
    bool call(std::string_view name, std::string_view instance,
              const std::vector<std::pair<std::string, Value>>& arguments, Value& result) override;
    void report(Diagnostic) override;
    bool tolerateUnknownCall(std::string_view name, std::uint32_t line) override;

    // ---- lot API 7 : les fonctions inconnues -------------------------------------
    // Continuer (l'appel rend 0) ou s'arreter (comme avant, le defaut du moteur).
    void setContinueOnUnknownCalls(bool on) noexcept { continueOnUnknown_ = on; }
    [[nodiscard]] bool continueOnUnknownCalls() const noexcept { return continueOnUnknown_; }
    // Ce qui a ete appele sans etre connu, depuis prepare() : le nom (tel
    // qu'ecrit), combien d'appels, la section et la ligne du premier.
    struct UnknownCall { std::string name; std::uint64_t calls{0}; std::string section; std::uint32_t line{0}; };
    [[nodiscard]] const std::vector<UnknownCall>& unknownCalls() const noexcept { return unknownCalls_; }
    // Lot API 7 : les limites d'un cycle (defaut : 2 000 000 instructions, 1 500 ms).
    // Les essais et la mesure les changent ; 0 : pas de limite de temps.
    void setScanLimits(std::uint64_t maxStatements, std::int64_t maxMilliseconds) noexcept {
        maxScanStatementsSetting_ = maxStatements;
        maxScanMsSetting_ = maxMilliseconds;
    }
    // Lot API 7 : combien de cases la simulation tient (l'onglet Simulation le
    // dit) - sans copier ni trier les 64 000 noms que rend names().
    [[nodiscard]] std::size_t slotCount() const noexcept { return slots_.size(); }
    // Lot API 7 : chaque case, sans copier ni trier les noms - l'onglet
    // Simulation y voit ce qui change (« Qui changent »), repliees comprises.
    // `value` est la valeur du programme (un forcage ne la change pas) ; son
    // adresse reste bonne tant que ce runtime vit : une case ne se retire
    // jamais, seul prepare() refait la table (avant toute lecture).
    void forEachSlot(const std::function<void(const std::string& name, const Value& value)>& fn) const;

    // ---- lot API 8 : le debogage --------------------------------------------------
    // LES POINTS D'ARRET (remplace la liste). Chacun est retrouve dans sa section
    // (sa ligne decalee sur l'instruction suivante s'il n'y en a pas), sa
    // condition lue. Il ne coupe pas le cycle : le passage est retenu (valeurs de
    // la ligne, pile), le cycle va au bout, et takeBreakHit() le rend. Sans point
    // d'arret, rien ne coute : un vecteur vide teste par instruction.
    void setBreakpoints(std::vector<Breakpoint> list);
    [[nodiscard]] const std::vector<Breakpoint>& breakpoints() const noexcept { return breakpoints_; }
    [[nodiscard]] std::optional<BreakHit> takeBreakHit();
    // LE PAS A PAS : la prochaine entree de MAST (une section, ou une unite de
    // programme en bloc). Le premier pas commence le cycle (l'horloge avance),
    // le dernier le finit (courbes, compteur de cycles). step() finit un cycle
    // commence. Entre deux entrees, les valeurs se lisent et se forcent.
    ScanReport stepEntry(std::int64_t deltaMilliseconds = 20);
    [[nodiscard]] bool scanInProgress() const noexcept { return scanOpen_; }
    [[nodiscard]] std::string nextEntry() const;                 // "" : entre deux cycles
    [[nodiscard]] const std::vector<std::string>& entries() const noexcept { return entryNames_; }
    [[nodiscard]] std::size_t nextEntryIndex() const noexcept;   // 0 entre deux cycles
    // OU PASSE LE TEMPS : chaque section de MAST au dernier cycle complet (vide
    // avant le premier).
    [[nodiscard]] std::vector<SectionTime> sectionTimes() const;
    // QUI A ECRIT : la derniere ecriture de cette case (Armoires[0].ana.PT1.mes,
    // Unite.variable, %MW100, %MW100.3). Faux : inconnue, ou jamais ecrite.
    [[nodiscard]] bool lastWrite(std::string_view path, WriteSite& out) const;
    // LE PROGRAMME DIRAIT : la valeur que le programme a ecrite dans la case
    // (Slot.value), pas son forcage - une case forcee la garde en dessous (voir
    // write()). Memes chemins que lastWrite. Faux : inconnue.
    [[nodiscard]] bool programValue(std::string_view path, Value& out) const;
    // Le cycle en cours (ou le dernier) : celui qu'une ecriture porte.
    [[nodiscard]] std::uint64_t currentScan() const noexcept { return scanOpen_ ? currentScan_ : scans_; }

    // ---- lot API 8 : la modification en ligne ----------------------------------------
    // L'empreinte de ce qui fait le programme (sections, variables, types, DFB,
    // unites, taches, memoire) - pas l'IHM, ni les commentaires, ni les tables
    // d'animation. Deux projets de meme empreinte se simulent pareil.
    [[nodiscard]] static std::uint64_t programFingerprint(const domain::Project& project);
    // Ce runtime vient d'etre prepare sur le nouveau programme : il reprend les
    // valeurs de `previous` (chaque case de meme nom et de meme type, les
    // adresses et variables de boucle que le programme avait creees, l'etat des
    // temporisations et compteurs), son horloge, son compte de cycles, ses
    // forcages (par leur chemin), ses courbes. `previous` doit etre entre deux cycles.
    Adoption adoptStateFrom(const Runtime& previous);
    // Les forcages seuls (le nouveau programme prepare apres un Arret).
    void adoptForcingFrom(const Runtime& previous);
    // Les erreurs de preparation que `previous` n'avait pas : une section qui ne
    // se lit plus. Non vide : la modification en ligne est refusee.
    [[nodiscard]] std::vector<Diagnostic> newPreparationErrors(const Runtime& previous) const;

    void breakpointReached(std::uint32_t tag, std::uint32_t line, LineProbe& probe) override;

private:
    std::uint64_t maxScanStatementsSetting_{2000000};
    std::int64_t  maxScanMsSetting_{1500};
    // 1.10 : ce que FIND rend quand la chaine n'est pas trouvee - 0 (IEC 61131-3),
    // ou -1 quand le programme compare le resultat de FIND a -1 (voir prepare()).
    std::int64_t  findNotFound_{0};
    struct Slot {
        Value value;
        bool  forced{false};
        Value forcedValue;
        bool  written{false};     // has the program ever assigned to it
        // Lot API 8 : qui a ecrit (la section - son numero, voir tagNames_ -, la
        // ligne, le cycle) ; et une case que le programme a creee lui-meme (une
        // adresse, une variable de boucle) : la modification en ligne la garde.
        bool          dynamic{false};
        std::uint32_t writerTag{0};
        std::uint32_t writerLine{0};
        std::uint64_t writerScan{0};
    };
    // State a standard function block carries between scans.
    struct BlockState {
        std::string   type;
        std::int64_t  elapsed{0};
        bool          running{false};
        bool          previousClock{false};
        std::int64_t  counter{0};
    };

    bool                     continueOnUnknown_{false};
    std::uint64_t            scanStatements_{0};  // lot API 7 : tout le cycle, blocs compris
    RunLimits                scanLimits_;
    std::vector<UnknownCall> unknownCalls_;
    std::string              currentSection_;   // la section en cours (pour les appels inconnus)

    void declare(const std::string& name, Type type, bool dynamic = false);
    // "%MW0[57]" is word 57, not a member of word 0. Rewrites indexed direct
    // addresses onto the flat address space before any lookup.
public:
    [[nodiscard]] static std::string normaliseAddress(std::string_view name);
private:
    void checkAgainstConfiguredMemory(const std::string& name);
    // A direct address exists because the program names it, exactly as the I/O
    // image exists because the module is in the rack. Declaring the whole
    // address space up front made a 67 000-entry table for a program that
    // touches a few hundred of them.
    Slot* ensureAddress(std::string_view name);
    void declareFrom(const domain::Project&, const domain::Variable&, const std::string& prefix);
    [[nodiscard]] Slot* find(std::string_view name);
    [[nodiscard]] const Slot* find(std::string_view name) const;

    bool runStandardBlock(const std::string& instance, const std::string& type,
                          const std::vector<std::pair<std::string, Value>>& arguments);
    bool runProjectBlock(const std::string& instance, const std::string& type,
                         const std::vector<std::pair<std::string, Value>>& arguments);
    bool runFunction(const std::string& name,
                     const std::vector<std::pair<std::string, Value>>& arguments, Value& result);

    std::shared_ptr<const domain::Project>        project_;
    std::unordered_map<std::string, Slot>         slots_;
    // Lot API 7 : CE QUE find() A DEJA TROUVE, par portee. Un nom se resolvait a
    // chaque lecture (la portee, puis sans la casse, puis sans la portee) : une
    // dizaine d'allocations et de hachages, 40 000 instructions par instruction
    // ST. La table, cherchee par string_view (sans allocation), ne garde que les
    // noms trouves ; elle est videe quand le jeu de cases change.
    struct NameHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
    };
    using ResolveCache = std::unordered_map<std::string, const Slot*, NameHash, std::equal_to<>>;
    mutable std::unordered_map<std::string, ResolveCache, NameHash, std::equal_to<>> resolveCache_;
    mutable std::string   cacheScope_{"\x01"};
    mutable ResolveCache* cacheCurrent_{nullptr};
    void                  clearResolveCache() noexcept { resolveCache_.clear(); cacheCurrent_ = nullptr; cacheScope_ = "\x01"; }
    // Structured Text is case-insensitive: the declaration says Nom_gaz and the
    // code writes config.nom_gaz, and both name the same thing. Lookups go
    // through this map, so the table keeps the spelling the project used while
    // still resolving either.
    std::unordered_map<std::string, std::string>  canonical_;
    std::unordered_map<std::string, BlockState>   blocks_;
    std::unordered_map<std::string, std::string>  instanceTypes_;   // name -> FB type
    std::unordered_map<std::string, std::string>  instanceLower_;   // lot API 7 : nom sans casse -> nom
    // For each array or structure, the suffixes of its members: "[0]", ".field".
    // Built while declaring, so a whole-aggregate copy knows what to copy.
    std::unordered_map<std::string, std::vector<std::string>> aggregates_;
    // A section runs in the scope of the POU that owns it: an unqualified name
    // is that POU's declaration if it has one, and a global otherwise. Without
    // this every program unit's own variables are invisible to its own code.
    struct Runnable {
        std::string                  section;
        std::string                  scope;      // "Unit." or empty
        std::shared_ptr<Program>     program;
        std::uint32_t                entry{0};   // lot API 8 : l'entree de MAST (entryNames_)
        // 1.10.2 : LA CONDITION D'ACTIVATION de la section (vide : toujours
        // active), lue dans la portee de son unite ; fausse, la section ne
        // s'execute pas (comme sur l'automate). `active` : au dernier passage.
        std::shared_ptr<const Expression> condition;
        std::string                  conditionText;
        bool                         active{true};
    };
    std::vector<Runnable> programs_;
    std::string           scopePrefix_;

    // 1.10.2 : LES PARAMETRES DES UNITES DE PROGRAMME relies a une globale
    // (EffectiveParameter de l'export). Comme Control Expert : une entree est
    // copiee depuis sa globale au debut de l'unite, une sortie vers sa globale
    // a la fin, une entree/sortie EST la globale (find() y renvoie) ; une
    // structure ou un tableau, en entier (case par case).
    struct ParamCopy {
        Slot* from{nullptr};
        Slot* to{nullptr};
    };
    struct UnitLinks {
        std::vector<ParamCopy> in;      // globale -> parametre, avant la 1re section
        std::vector<ParamCopy> out;     // parametre -> globale, apres la derniere
        std::vector<ParamCopy> mirror;  // IN_OUT : la globale -> sa copie (l'affichage)
    };
    std::vector<UnitLinks> unitLinks_;                               // par entree (entryNames_)
    std::unordered_map<std::string, std::string> paramAliases_;     // "unite.param" (minuscules) -> globale
    void linkUnitParameters(const domain::Project&, domain::Index pou, UnitLinks& links);
    void copyParameters(const std::vector<ParamCopy>& copies, bool noteWriter);
    [[nodiscard]] const Slot* findParamAlias(std::string_view name) const;

    // The body of each DFB type, parsed once and run per instance with the
    // instance as its scope. A block that calls another block nests, so the
    // depth is counted and capped: a DFB that instantiates itself would
    // otherwise recurse until the stack gives out.
    std::unordered_map<std::string, std::vector<std::shared_ptr<Program>>> blockBodies_;
    int                                                                    callDepth_{0};
    static constexpr int kMaxCallDepth = 16;
    std::vector<Diagnostic>                       prepareDiagnostics_;
    std::vector<Diagnostic>                       scanDiagnostics_;
    std::map<std::string, std::deque<Sample>>     history_;
    std::vector<std::string>                      memoryWarnings_;
    std::size_t                                   historyDepth_{2000};
    std::int64_t                                  clockMs_{0};
    std::int64_t                                  deltaMs_{20};
    std::uint32_t                                 scans_{0};
    bool                                          halted_{false};

    // ---- lot API 8 ------------------------------------------------------------
    // Les sections preparees, par numero (0 : aucune - une ecriture hors du
    // programme) : "Section" ou "BLOC.Section", et leur programme.
    std::vector<std::string> tagNames_{std::string{}};
    std::vector<Program*>    tagPrograms_{nullptr};
    std::vector<std::string> entryNames_;              // les entrees de MAST
    std::string              task_{"MAST"};
    ExecTrace                trace_;                   // la section et la ligne en cours
    // Le cycle en cours (pas a pas) : ouvert, la prochaine section, ses comptes.
    bool                     scanOpen_{false};
    std::size_t              nextRunnable_{0};
    std::size_t              currentRunnable_{static_cast<std::size_t>(-1)};
    std::uint64_t            currentScan_{0};
    std::uint32_t            scanTopStatements_{0};
    std::uint32_t            scanSectionsRun_{0};
    std::size_t              diagReported_{0};
    // Les instructions et microsecondes de chaque section : le cycle en cours,
    // le dernier complet.
    std::vector<std::pair<std::uint64_t, std::int64_t>> pendingTimes_, lastTimes_;
    // Les points d'arret : ce que le runtime en a compris (meme rang que breakpoints_).
    struct BreakInfo {
        std::uint32_t                     tag{0};
        std::shared_ptr<const Expression> condition;
        bool                              armed{false};
        // Une condition s'arrete quand elle DEVIENT vraie (comme la maquette) :
        // vraie a un passage de ce cycle, jamais au cycle d'avant.
        bool                              trueNow{false};
        bool                              trueBefore{false};
    };
    // Les conditions deja vraies, gardees quand la liste change ou que le
    // programme change en ligne (par numero, a condition egale).
    struct ConditionTruth {
        std::uint32_t id{0};
        std::string   condition;
        bool          wasTrue{false};
    };
    std::vector<ConditionTruth> adoptedTruth_;
    void rollConditionTruth() noexcept;
    std::vector<Breakpoint>  breakpoints_;
    std::vector<BreakInfo>   breakInfo_;
    bool                     anyArmed_{false};
    int                      probing_{0};              // une lecture au passage : pas d'appel de bloc
    std::optional<BreakHit>  scanHit_, pendingHit_;
    // Les appels de DFB en cours - tenus seulement s'il y a un point d'arret.
    struct Frame {
        const std::string* instance;
        const std::string* type;
        std::uint32_t      tag;
    };
    std::vector<Frame>       frames_;
    // Une adresse situee -> la variable qui y est (qui a ecrit %MW100).
    std::unordered_map<std::string, std::string> locatedAt_;

    void       beginScan(std::int64_t deltaMilliseconds);
    void       armDeadline();
    void       runRunnable(std::size_t index, bool& halted);
    void       runSection(std::size_t index, bool& halted);    // 1.10.2 : sans les parametres
    ScanReport finishScan(bool halted);
    void       noteWrite(Slot& slot) noexcept;
    void       resolveBreakpoints();
    void       markBreakLines();
    [[nodiscard]] std::uint32_t tagOfSection(std::string_view section) const;
    [[nodiscard]] std::string   whyNotSimulated(const std::string& section) const;
    [[nodiscard]] bool          scopeOfTag(std::uint32_t tag, std::string& scope) const;
    [[nodiscard]] std::vector<std::string> stackNow() const;
};

} // namespace sim
