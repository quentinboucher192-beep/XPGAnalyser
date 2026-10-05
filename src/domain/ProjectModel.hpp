// =============================================================================
//  domain/ProjectModel.hpp — the imported project, independent of any view
// -----------------------------------------------------------------------------
//  Storage strategy for the 50 000-variable target:
//
//   * Every entity lives in a flat std::vector owned by Project. Relationships
//     are 32-bit indices into those vectors, not pointers. That gives contiguous
//     iteration for the analyzer, trivial serialisation of a cached project, and
//     stable identity across a std::vector reallocation.
//
//   * Names are interned. A project of this kind repeats "BOOL", "EBOOL",
//     "%MW", "MAST" tens of thousands of times; interning turns each into a
//     4-byte id and makes type resolution an integer compare.
//
//   * Nothing here knows about widgets. The view models in ui/ adapt these
//     containers to ITableModel / ITreeModel.
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace domain {

    using Index = std::uint32_t;
    using SymbolId = std::uint32_t;
    constexpr Index kNoIndex = 0xFFFFFFFFu;

    // --------------------------------------------------------- string interning ---
    class StringPool {
    public:
        // LA COPIE REFAIT L'INDEX. Les cles de `lookup_` sont des vues sur les
        // chaines de `storage_` : copiees telles quelles, elles regardaient les
        // chaines de l'ORIGINAL - un projet copie (l'etat d'avant d'une
        // commande, une version) cherchait ses noms dans un autre projet, puis
        // dans de la memoire rendue une fois l'original remplace. Le deplacement,
        // lui, garde les chaines a leur place (std::deque) : il reste par defaut.
        StringPool() = default;
        StringPool(const StringPool& o) : storage_(o.storage_) { reindex(); }
        StringPool& operator=(const StringPool& o) {
            if (this != &o) {
                storage_ = o.storage_;
                reindex();
            }
            return *this;
        }
        StringPool(StringPool&&) noexcept = default;
        StringPool& operator=(StringPool&&) noexcept = default;
        ~StringPool() = default;

        SymbolId intern(std::string_view s);
        [[nodiscard]] std::string_view text(SymbolId id) const;
        [[nodiscard]] std::size_t size() const noexcept { return storage_.size(); }
    private:
        // std::deque, not std::vector: the lookup map keys are string_views into
        // these strings. A vector<string> reallocation moves every element, and for
        // short names (BOOL, INT, %MW) the characters live inside the string object
        // itself, so every key in the map would dangle. deque never moves an element
        // that has already been pushed.
        std::deque<std::string>                        storage_;
        std::unordered_map<std::string_view, SymbolId> lookup_;
        void reindex() {
            lookup_.clear();
            lookup_.reserve(storage_.size());
            for (std::size_t i = 0; i < storage_.size(); ++i)
                lookup_.emplace(std::string_view(storage_[i]), static_cast<SymbolId>(i));
        }
    };

    // ------------------------------------------------------------------ types ---
    enum class TypeClass : std::uint8_t {
        Unknown, Elementary,      // BOOL, EBOOL, INT, DINT, UDINT, REAL, WORD, TIME, STRING…
        Array,                    // ARRAY[0..27] OF ST_GC_Step
        Derived,                  // a DDT declared in this project
        FunctionBlock,            // an instance of a DFB or of a standard FB (TON, TOF…)
    };

    struct TypeRef {
        SymbolId  name{ 0 };             // as written: "ARRAY[0..1] OF armoire"
        TypeClass klass{ TypeClass::Unknown };
        SymbolId  elementType{ 0 };      // "armoire" for the example above
        std::int64_t arrayLow{ 0 }, arrayHigh{ -1 };
        std::uint32_t stringLength{ 0 }; // string[32] -> 32
        Index     derivedIndex{ kNoIndex };   // -> Project::derivedTypes
        Index     fbTypeIndex{ kNoIndex };    // -> Project::functionBlockTypes
    };

    // -------------------------------------------------------------- addresses ---
    // %MW1174, %M1000, %I0.3, %Q0.1, %S11, %SW125, %KW20
    enum class MemoryArea : std::uint8_t {
        None, Internal /*%M*/, InternalWord /*%MW*/, Input /*%I*/, InputWord /*%IW*/,
        Output /*%Q*/, OutputWord /*%QW*/, System /*%S*/, SystemWord /*%SW*/,
        Constant /*%K*/, ConstantWord /*%KW*/, Topological /*%CH, rack.slot.channel*/
    };

    struct Address {
        MemoryArea               area{ MemoryArea::None };
        std::uint32_t            offset{ 0 };
        std::optional<std::uint8_t> bit;        // %I0.3 -> offset 0, bit 3
        std::string              raw;           // exactly as it appeared in the file

        [[nodiscard]] bool valid() const noexcept { return area != MemoryArea::None; }
        // Ordering used by the "Address" column: area first, then numeric offset,
        // so %MW9 sorts before %MW10 (a plain string sort gets that wrong).
        [[nodiscard]] bool operator<(const Address& o) const noexcept;
        static Address parse(std::string_view s);
    };

    // -------------------------------------------------------------- variables ---
    enum class VariableScope : std::uint8_t {
        Global,        // <dataBlock>
        Local,         // POU privateLocalVariables
        Public,        // POU publicLocalVariables
        Input, Output, InOut,        // FB / POU parameters
        Constant,
        DerivedMember, // a field inside a DDT
    };

    // Per-array-element documentation: <instanceElementDesc name="[0]"><comment>.
    // Control Expert uses it to label the members of an array of BOOL, and losing
    // it on a round trip would quietly delete an engineer's documentation.
    struct InstanceElement {
        std::string name;       // "[0]", "[1]"...
        SymbolId    comment{ 0 };
    };

    struct Variable {
        SymbolId      name{ 0 };
        TypeRef       type;
        Address       address;
        VariableScope scope{ VariableScope::Global };
        SymbolId      comment{ 0 };
        SymbolId      initValue{ 0 };
        Index         owner{ kNoIndex };      // POU / FB type / DDT that declares it
        std::uint32_t referenceCount{ 0 };    // filled by ProjectAnalyzer
        bool          located{ false };       // has a topological or memory address
        // <attribute> elements on the declaration, e.g. EffectiveParameter. Kept
        // verbatim: they are Control Expert's, not ours, and dropping them on
        // export would silently change the meaning of a DFB call.
        std::vector<std::pair<std::string, std::string>> attributes;
        std::vector<InstanceElement>                     instanceElements;
    };

    // ------------------------------------------------------------ derived types ---
    struct DerivedType {                       // <DDTSource>
        SymbolId            name{ 0 };
        std::string         version;
        std::string         checksum;          // TypeSignatureCheckSumString
        std::vector<Index>  fields;            // -> Project::variables
        std::uint32_t       instanceCount{ 0 };  // computed
    };

    // ------------------------------------------------------------------- POUs ---
    enum class PouLanguage : std::uint8_t { Unknown, ST, IL, LD, FBD, SFC };
    enum class PouKind : std::uint8_t {
        Program, Section, SubRoutine, FunctionBlockType,
        Function, ProgramUnit, DfbInstance
    };

    struct Section {                            // <program> / <FBProgram>
        SymbolId      name{ 0 };
        PouLanguage   language{ PouLanguage::Unknown };
        SymbolId      task{ 0 };                  // MAST, FAST, AUX0…
        std::uint32_t order{ 0 };
        SymbolId      activationCondition{ 0 };   // empty => always active
        SymbolId      logicCondition{ 0 };
        // 1.8.0 : OU LE FICHIER PORTAIT LA CONDITION. Control Expert V15 la met sur
        // <sectionDesc> (la tache) ; d'anciens exports, sur <program>. La lecture
        // prend les deux ; l'ecriture la rend a la meme place (vrai : <program> aussi).
        bool          conditionOnProgram{ false };
        // A SUBROUTINE, not a section. Both live here because both are named blocks
        // of code attached to a task, but they run differently: a section runs every
        // scan, a subroutine runs when something calls it. Treating one as the other
        // is not a formatting difference, it is a change to the program.
        bool          isSubroutine{ false };
        std::string   body;                     // source text (ST/IL) or serialised graph (LD/FBD/SFC)
        std::uint32_t lineCount{ 0 };
        std::uint32_t statementCount{ 0 };
        Index         owner{ kNoIndex };          // -> pous
    };

    struct Pou {
        SymbolId            name{ 0 };
        PouKind             kind{ PouKind::Section };
        std::string         version;
        std::vector<Index>  sections;           // -> Project::sections
        std::vector<Index>  parameters;         // -> variables (In/Out/InOut)
        std::vector<Index>  locals;             // -> variables
        std::vector<Index>  children;           // nested program units
        Index               parent{ kNoIndex };
        SymbolId            task{ 0 };            // program units: the task that runs it
        std::uint32_t       order{ 0 };           // its SectionOrder within that task
        std::uint32_t       instanceCount{ 0 };   // DFB types: how many instances exist
        bool                userDefined{ true };  // false for standard library blocks (TON, RTC…)
        // <attribute> elements carried on FBSource: TypeCodeCheckSumString,
        // TypeSignatureCheckSumString, UseNewTplSignAlgo. Kept verbatim so a
        // re-export gives back the flags Control Expert put there; the checksums
        // among them are zeroed on write, the rest is passed through untouched.
        std::vector<std::pair<std::string, std::string>> attributes;
    };

    // ------------------------------------------------------------- libraries ---
    enum class LibraryKind : std::uint8_t { Standard, Motion, Process, Communication, Safety, User, Custom };

    struct LibraryEntry {
        SymbolId      name{ 0 };
        LibraryKind   kind{ LibraryKind::User };
        std::string   version;
        SymbolId      family{ 0 };                // "Standard", "Motion", "Process"…
        Index         pouIndex{ kNoIndex };       // DFB type this entry refers to
        std::uint32_t usageCount{ 0 };
    };

    // --------------------------------------------------------------- hardware ---
    // One configured channel group on a module. A discrete module declares its
    // points in groups of eight; an analog module declares them one by one. Both
    // shapes come straight out of <channelATS>.
    enum class ChannelDirection : std::uint8_t { Unknown, Input, Output, Communication };

    struct Channel {
        std::uint16_t    number{ 0 };        // first point of the group
        ChannelDirection direction{ ChannelDirection::Unknown };
        std::string      role;             // ASFCatKey, e.g. "BasicInRackTORInSTD_MASTER"
        std::string      task;             // MAST, FAST...
        std::uint16_t    functionCode{ 0 };  // descFB/@code
        std::uint16_t    functionVersion{ 0 };
        std::string      iobFile;          // descIOB/@IOBFileName
        std::vector<std::uint32_t> paramKW, paramKPW;
    };

    enum class ModuleKind : std::uint8_t {
        Unknown, Cpu, PowerSupply, DiscreteInput, DiscreteOutput, DiscreteMixed,
        AnalogInput, AnalogOutput, Communication, Counting, Motion, Rack, Extension,
    };

    struct Module {
        std::string   reference;       // "BMXDDI3202K", exactly as exported
        std::string   description;     // from the catalog; empty when unknown
        std::string   family;          // partItem/@family: "Discrete", "Analog"...
        std::string   firmware;        // partItem/@version
        ModuleKind    kind{ ModuleKind::Unknown };
        std::uint16_t rack{ 0 };
        std::int16_t  slot{ 0 };         // -1 for the power supply
        std::uint16_t inputPoints{ 0 }, outputPoints{ 0 };
        bool          pointsFromCatalog{ false };   // true when the file was ambiguous
        bool          knownReference{ false };      // false: not in the catalog
        bool          isCpu{ false };
        std::string   topologicalAddress;         // "\0.0\0.3"
        std::string   nodeGuid;
        std::vector<Channel> channels;

        [[nodiscard]] std::uint16_t points() const { return inputPoints + outputPoints; }
    };

    struct Rack {
        std::uint16_t       number{ 0 };
        std::string         reference;      // "BMXXBP0800"
        std::string         description;
        std::uint16_t       slotCount{ 0 };   // from the catalog; 0 when unknown
        std::string         topologicalAddress;
        std::vector<Module> modules;        // slot order, power supply first if present
    };

    // Data-memory sizing declared on <PLC>. Comparing it against the located
    // variables actually declared is one of the few genuine cross-file checks this
    // tool can make.
    struct MemoryConfiguration {
        std::uint32_t internalBits{ 0 };      // %M
        std::uint32_t internalWords{ 0 };     // %MW
        std::uint32_t constantWords{ 0 };     // %KW
        bool          initialiseWords{ false };
        bool          autoRun{ false };
        bool          coldStartOnly{ false };
        bool          changeConfigOnTheFly{ false };
        bool          declared{ false };      // false: no .XHW was imported
    };

    struct HardwareConfig {
        std::string         family;         // "Modicon M340"
        std::string         cpuReference;   // "BMX P34 2020"
        std::string         cpuFirmware;    // "03.30"
        std::string         resourceName;   // resName attribute, e.g. "Micro Basic"
        std::string         busName;        // busATS/@name, e.g. "XBusMicro"
        std::string         powerSupply;    // reference of rack 0's supply
        std::vector<Rack>   racks;
        MemoryConfiguration memory;
        // True while the layout is only inferred from the CPU identity: a .XPG
        // carries the CPU but not the rack. Cleared once a .XHW is merged in.
        bool inferred{ true };

        [[nodiscard]] std::uint16_t totalModules() const;
        [[nodiscard]] std::uint16_t totalInputPoints() const;
        [[nodiscard]] std::uint16_t totalOutputPoints() const;
    };

    std::string_view toString(ModuleKind) noexcept;
    std::string_view toString(ChannelDirection) noexcept;

    struct Task {
        SymbolId      name{ 0 };        // MAST, FAST, AUX0..3, EVTi
        std::string   type;           // cyclic / periodic
        std::uint32_t period{ 0 };
        std::uint32_t watchdog{ 0 };    // maxExecTime
        std::vector<Index> sections;  // in execution order
    };

    // -------------------------------------------------------- animation tables ---
    // Lot API 3 : UNE LIGNE PORTE SA SOURCE. Une variable de l'automate - ce que
    // Control Expert connait, ce que l'export ecrit - ou une variable de l'IHM :
    // le projet seul la connait, elle reste dans tables/animation.txt et ne part
    // jamais vers Control Expert.
    struct AnimationEntry {
        SymbolId name{ 0 };
        bool     hmi{ false };
    };

    struct AnimationTable {
        SymbolId                    name{ 0 };
        SymbolId                    owner{ 0 };      // l'unite de programme qui la porte (l'export l'y range)
        std::vector<AnimationEntry> entries;
    };

    // ------------------------------------------------------------------ project ---
    struct ProjectHeader {
        std::string company;          // "Schneider Automation"
        std::string product;          // "Control Expert V15.3 - 230214C"
        std::string productVersion;   // "V15.3"
        std::string dtdVersion;       // "41"
        std::string contentKind;
        std::string projectName;
        std::string projectVersion;   // "0.0.531"
        std::string exportedAt;        // fileHeader/dateTime: quand CE FICHIER a ete ecrit

        // contentHeader/dateTime: quand le PROGRAMME a ete modifie. Deux faits
        // differents, et ils etaient confondus: exportedAt etait lu sur fileHeader
        // puis reecrit sur contentHeader, si bien qu'un fichier dont le
        // contentHeader ne portait pas la date revenait en la portant.
        std::string contentDateTime;

        // Les fins de ligne du document tel qu'il a ete lu.
        //
        // Control Expert V15.3 ecrit en CRLF. Le writer emettait des LF, donc un
        // fichier relu et reecrit voyait CHACUNE de ses lignes changer - une
        // comparaison avec l'original devenait illisible, alors que c'est
        // exactement ce qu'on veut pouvoir faire apres une modification.
        bool crlf{ true };
        std::string sourceFile;
    };

    // ---------------------------------------------------------------------------
    //  BUILD CONSISTENCY
    //
    //  Every entity here lives in a vector and is reached by index, so the whole
    //  model depends on every translation unit agreeing on sizeof(Variable) and
    //  friends. When an update adds a member and some object files are not
    //  recompiled - which happens whenever an archive is unpacked with timestamps
    //  older than the build outputs - two halves of the program walk the same array
    //  with different strides. The result is not a compile error and not a clean
    //  crash: it is silent memory corruption that surfaces somewhere unrelated.
    //
    //  That has now happened twice. So each Project records the sizes as seen by
    //  the translation unit that *created* it, and any other unit can compare them
    //  against its own in one integer comparison. A mismatch becomes a message
    //  naming the problem instead of a garbage read.
    // ---------------------------------------------------------------------------
    struct LayoutStamp {
        std::uint32_t variableBytes{ static_cast<std::uint32_t>(sizeof(Variable)) };
        std::uint32_t pouBytes{ static_cast<std::uint32_t>(sizeof(Pou)) };
        std::uint32_t sectionBytes{ static_cast<std::uint32_t>(sizeof(Section)) };
        std::uint32_t derivedTypeBytes{ static_cast<std::uint32_t>(sizeof(DerivedType)) };
        std::uint32_t moduleBytes{ static_cast<std::uint32_t>(sizeof(Module)) };

        [[nodiscard]] bool operator==(const LayoutStamp&) const = default;
    };

    // Lot API 5 : LES BORNES DE LECTURE DU PLAN MEMOIRE, une par zone (%M, %MW,
    // %KW). Un reglage du projet (config/memoire.txt), pas du materiel : l'export
    // vers Control Expert ne l'ecrit pas, et importer un autre .XHW ne l'efface pas.
    enum class MemoryZone : std::uint8_t { Bits = 0, Words = 1, Constants = 2 };   // %M, %MW, %KW
    struct MemoryWindow {
        bool          set{ false };        // false : toute la zone
        std::uint32_t from{ 0 }, to{ 0 };  // les cellules lues, bornes comprises
        [[nodiscard]] bool operator==(const MemoryWindow&) const = default;
    };

    // Lot API 6 : L'ICONE DU PROJET, 32 x 32. Chaque pixel est un indice dans
    // une palette de seize couleurs propre a l'icone (0 : transparent). Vide :
    // pas d'icone, l'application montre son logo. Rangee dans config/icone.txt,
    // en texte - une lettre par pixel -, comme le reste du dossier ; project/
    // ProjectIcon.hpp la dessine, la lit et l'ecrit.
    struct ProjectIcon {
        std::array<std::uint32_t, 16> palette{};   // 0xRRGGBB : les couleurs 1 a 16
        std::vector<std::uint8_t>      pixels;      // 32 x 32 indices 0..16, ligne par ligne
        [[nodiscard]] bool empty() const noexcept { return pixels.empty(); }
        [[nodiscard]] bool operator==(const ProjectIcon&) const = default;
    };

    class Project {
    public:
        // Filled by whichever translation unit constructs the Project. Compared,
        // not trusted: see layoutMatches() below.
        LayoutStamp               layout{};

        ProjectHeader             header;
        HardwareConfig            hardware;
        StringPool                strings;

        std::vector<Variable>     variables;
        std::vector<DerivedType>  derivedTypes;
        std::vector<Pou>          pous;
        std::vector<Section>      sections;
        std::vector<LibraryEntry> libraries;
        std::vector<Task>         tasks;
        std::vector<AnimationTable> animationTables;
        std::array<MemoryWindow, 3> memoryWindows{};     // indice : MemoryZone
        ProjectIcon                 icon;                // lot API 6 : vide = le logo de l'application
        // 1.8.0 : LES ICONES AU CHOIX de ce qui porte du code - une section, une
        // unite, un bloc DFB et ses sections, un DDT, un script ou une fonction de
        // l'IHM. Cle : core::codeicons::keyOf ("section:MAST/Init") ; valeur : la
        // cle du catalogue ("init"). Rangees dans config/icones-code.txt ; un
        // element sans entree garde son icone de toujours.
        std::map<std::string, std::string> codeIcons;

        // name -> index maps, rebuilt after import; used for resolution and for the
        // "go to declaration" jump in the variable browser.
        std::unordered_map<SymbolId, Index> variableByName;
        std::unordered_map<SymbolId, Index> typeByName;
        std::unordered_map<SymbolId, Index> pouByName;

        // Set when the importer could only fill part of the model, e.g. hardware
        // absent from a .XPG. The UI shows an inline notice rather than empty panes.
        std::vector<std::string> partialDataNotices;

        void buildIndices();

        // Resolves every variable's TypeRef against the types this project holds:
        // derivedIndex, fbTypeIndex, klass, and the instance counts.
        //
        // It lived inside the XPG parser, which was right until something else
        // created a variable. A type imported from the shared library, or a variable
        // created by a macro, came out textually correct and structurally empty - so
        // the explorer filed an instance under "elementary variables" and the
        // simulator could not see a single one of its members.
        //
        // IDEMPOTENT. The instance counts are reset before being recounted, so
        // calling it twice does not report twice as many instances.
        void linkTypes();
        void clear();

        // True when the caller's view of the model layout matches the one that
        // built this Project. Deliberately defined in the header so that each
        // translation unit compiles its own sizes into the comparison.
        [[nodiscard]] bool layoutMatches() const { return layout == LayoutStamp{}; }
        [[nodiscard]] std::string layoutMismatchMessage() const;

        [[nodiscard]] std::string_view name(SymbolId id) const { return strings.text(id); }
        [[nodiscard]] std::string_view variableName(Index i) const { return strings.text(variables[i].name); }
        [[nodiscard]] std::size_t      memoryFootprint() const;   // for the status bar
    };

    // ---------------------------------------------------------------- sizing ---
    // Memory footprint of a type, in bits.
    //
    // These are the Modicon M340 / Control Expert conventions, not a guess:
    // BOOL and EBOOL occupy one bit, INT/UINT/WORD 16, DINT/UDINT/DWORD/REAL/TIME
    // 32, LREAL/LINT/DT 64, and STRING[n] takes n+2 bytes (a length byte and a
    // terminator). Inside a DDT, non-bit members are aligned on a 16-bit boundary
    // and consecutive bits are packed into the current word, which is what the
    // compiler does; the result is a close model, not a substitute for the actual
    // build report, and the UI says so.
    [[nodiscard]] std::uint32_t elementaryBits(std::string_view typeName) noexcept;
    [[nodiscard]] std::uint32_t typeSizeInBits(const class Project&, const TypeRef&);
    [[nodiscard]] std::uint32_t derivedTypeSizeInBits(const class Project&, Index derivedIndex);

    // Language detection from an element name, kept in one place because the same
    // mapping is needed by the tree icons, the editor and the statistics.
    PouLanguage languageFromElement(std::string_view elementName) noexcept;
    std::string_view toString(PouLanguage) noexcept;
    std::string_view toString(VariableScope) noexcept;
    std::string_view toString(MemoryArea) noexcept;

} // namespace domain