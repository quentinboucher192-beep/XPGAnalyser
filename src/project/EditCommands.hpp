// =============================================================================
//  project/EditCommands.hpp — creating things, undoably
// -----------------------------------------------------------------------------
//  Every one of these is a core::ICommand, so it goes on the CommandStack and
//  Ctrl+Z works from the first day rather than being retrofitted later. That was
//  the reason for building the stack before there was anything to undo.
//
//  WHY UNDO IS A TRUNCATION
//
//  The domain model stores entities in flat vectors and refers to them by index.
//  Removing an element from the middle would renumber everything after it and
//  invalidate every stored index in the project - a whole class of bugs for no
//  benefit. So creation always appends, and undo always removes from the end.
//
//  That is only sound because the CommandStack is strictly last-in-first-out:
//  the command being undone is necessarily the one that appended last. Each undo
//  checks that invariant and refuses rather than corrupting the model if some
//  future change breaks it.
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../domain/ProjectModel.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace project {

    using ProjectPtr = std::shared_ptr<domain::Project>;

    // --- a task section, or a section inside a POU ------------------------------
    //
    //  `subroutine` NE CHANGE PAS LA PRESENTATION, IL CHANGE LE PROGRAMME.
    //
    //  Une section et une sous-routine vivent au meme endroit du modele - une
    //  Section, un POU qui la porte - et se ressemblent a s'y meprendre. Mais une
    //  section tourne A CHAQUE CYCLE, dans l'ordre d'execution de sa tache, et une
    //  SR ne tourne QUE quand quelque chose l'appelle. Creer l'une pour l'autre ne
    //  se voit pas dans l'arbre et se voit tout de suite sur la machine.
    //
    //  Concretement, une sous-routine :
    //    * a un POU de genre SubRoutine, ce qui la range dans le dossier
    //      "Sous-routines" et la sort de "Program units" ;
    //    * porte sa tache SUR SON POU et non sur sa section, parce que `taskOf`
    //      n'ordonnance que les unites de programme : c'est ce qui la garde hors
    //      de l'ordre d'execution tout en sachant a quelle tache elle appartient ;
    //    * n'est PAS ajoutee a la liste des sections de la tache.
    class AddSectionCommand final : public core::ICommand {
    public:
        AddSectionCommand(ProjectPtr project, std::string name, std::string task,
            domain::PouLanguage language, domain::Index owner = domain::kNoIndex,
            bool subroutine = false);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

    private:
        ProjectPtr          project_;
        std::string         name_, task_;
        domain::PouLanguage language_;
        domain::Index       owner_;
        bool                subroutine_{ false };
        domain::Index       sectionIndex_{ domain::kNoIndex };
        domain::Index       createdPou_{ domain::kNoIndex };
    };

    // --- a program unit ----------------------------------------------------------
    class AddProgramUnitCommand final : public core::ICommand {
    public:
        AddProgramUnitCommand(ProjectPtr project, std::string name, std::string task);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

    private:
        ProjectPtr    project_;
        std::string   name_, task_;
        domain::Index pouIndex_{ domain::kNoIndex };
    };

    // --- a DFB type --------------------------------------------------------------
    class AddFunctionBlockCommand final : public core::ICommand {
    public:
        AddFunctionBlockCommand(ProjectPtr project, std::string name, std::string version);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

    private:
        ProjectPtr    project_;
        std::string   name_, version_;
        domain::Index pouIndex_{ domain::kNoIndex };
        domain::Index libraryIndex_{ domain::kNoIndex };
    };

    // --- a derived data type -----------------------------------------------------
    class AddDerivedTypeCommand final : public core::ICommand {
    public:
        AddDerivedTypeCommand(ProjectPtr project, std::string name, std::string version);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

    private:
        ProjectPtr    project_;
        std::string   name_, version_;
        domain::Index typeIndex_{ domain::kNoIndex };
    };

    // --- a variable, in any scope ------------------------------------------------
    class AddVariableCommand final : public core::ICommand {
    public:
        struct Spec {
            std::string          name;
            std::string          type{ "BOOL" };
            std::string          address;     // empty: unlocated
            std::string          comment;
            std::string          initValue;
            domain::VariableScope scope{ domain::VariableScope::Global };
            domain::Index        owner{ domain::kNoIndex };   // POU or DDT, by scope
        };

        AddVariableCommand(ProjectPtr project, Spec spec);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

    private:
        ProjectPtr    project_;
        Spec          spec_;
        domain::Index variableIndex_{ domain::kNoIndex };
    };

    // --- editing a section's body ------------------------------------------------
    //
    //  Typing produces one of these per keystroke, which would make Ctrl+Z undo a
    //  single character. ICommand::mergeableWith exists for exactly this: two edits
    //  to the same section, close together in the stack, collapse into one entry, so
    //  undo steps back over a sentence rather than a letter.
    class SetSectionBodyCommand final : public core::ICommand {
    public:
        SetSectionBodyCommand(ProjectPtr project, domain::Index section, std::string body);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;
        [[nodiscard]] bool mergeableWith(const core::ICommand& other) const override;
        void mergeFrom(const core::ICommand& other) override;

        [[nodiscard]] domain::Index section() const noexcept { return section_; }
        [[nodiscard]] const std::string& body() const noexcept { return newBody_; }

    private:
        ProjectPtr    project_;
        domain::Index section_;
        std::string   newBody_, oldBody_;
        bool          captured_{ false };
    };

    // --- l'ordre d'execution ----------------------------------------------------
    //
    //  Deplacer une section dans la tache. Deux formes, parce que l'interface en a
    //  deux : le menu contextuel connait un nombre de crans, le glisser-deposer
    //  connait une cible.
    //
    //  L'ANNULATION RETABLIT TOUS LES `order` DE LA TACHE, pas seulement celui de
    //  la section deplacee. Un deplacement renumerote ses voisines : ne rendre que
    //  l'ancien rang de la section laisserait la tache dans un etat que personne
    //  n'a demande - et ce serait un Ctrl+Z qui ment, ce qui est pire que pas de
    //  Ctrl+Z du tout.
    class ReorderSectionCommand final : public core::ICommand {
    public:
        // La cible d'un depot, enveloppee : `Index` et `int` sont tous deux des
        // entiers, et deux constructeurs qui ne different que par la seraient
        // choisis par des regles de conversion plutot que par ce qu'on voulait
        // dire. Le type rend l'intention lisible a l'appel.
        struct Before { domain::Index section{ domain::kNoIndex }; };

        // delta : -1 monter d'un cran, +1 descendre. Plusieurs crans sont permis.
        ReorderSectionCommand(ProjectPtr project, domain::Index section, int delta);
        // Deposer AVANT une section, ou a la fin quand Before{} est vide.
        ReorderSectionCommand(ProjectPtr project, domain::Index section, Before target);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

    private:
        ProjectPtr     project_;
        domain::Index  section_;
        domain::Index  before_{ domain::kNoIndex };
        int            delta_{ 0 };
        bool           byTarget_{ false };
        // L'etat d'avant, section par section : (indice, ancien order).
        std::vector<std::pair<domain::Index, std::uint32_t>> previous_;
        // Lot API 4 : le rang des unites de programme de la tache (une unite
        // tourne en bloc a son rang : la deplacer change `Pou::order`).
        std::vector<std::pair<domain::Index, std::uint32_t>> previousUnits_;
        std::vector<domain::Index>                           previousTaskCache_;
        domain::SymbolId                                     task_{ 0 };
    };

    // --- a rack, and a module in a rack -----------------------------------------
    //
    //  RACK RULES, from the Schneider documentation rather than from taste:
    //
    //   * "Each BMX XBP rack must be equipped with a power supply module. These
    //     modules are inserted in the first two slots of each rack (marked CPS)."
    //     The supply is double-width and has its own position; it is not one of the
    //     numbered slots, which is why the export gives it position -1.
    //   * "A BMX P34 20x0 processor is always installed on the rack in slot marked
    //     00 (address 0)" - and only on rack 0. Extension racks have no processor,
    //     so their slot 0 is an ordinary slot.
    //   * The processor limits the configuration: a BMX P34 2020 addresses 4 racks,
    //     1024 discrete points and 256 analog channels.
    //
    //  Adding a rack therefore also places its power supply, and adding rack 0
    //  places the processor in slot 0. That is not a convenience: a rack without a
    //  supply is not a configuration, and leaving slot 0 free would invite putting
    //  a module where the CPU has to go.
    class AddRackCommand final : public core::ICommand {
    public:
        AddRackCommand(ProjectPtr project, std::string reference,
            std::string powerSupply = "BMXCPS2000");
        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;
    private:
        ProjectPtr  project_;
        std::string reference_, powerSupply_;
    };

    class AddModuleCommand final : public core::ICommand {
    public:
        AddModuleCommand(ProjectPtr project, std::uint16_t rack, std::int16_t slot,
            std::string reference);
        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;
    private:
        ProjectPtr    project_;
        std::uint16_t rack_;
        std::int16_t  slot_;
        std::string   reference_;
    };

    // Names that must not collide with an existing one, checked before creating.
    [[nodiscard]] bool nameIsFree(const domain::Project&, std::string_view name);

    // A section name has to be unique within its container, not across the project:
    // two program units may each have an "Init", and Control Expert allows it.
    // `owner` is kNoIndex for a task section.
    [[nodiscard]] bool sectionNameIsFree(const domain::Project&, std::string_view name,
        domain::Index owner);

    // Every type a variable may be declared with, in the order a person looks for
    // them: elementary types first, then the project's own DDTs and DFB types.
    [[nodiscard]] std::vector<std::string> availableTypes(const domain::Project&);

    // --- ce que la boite "Nouvelle variable" a le droit de proposer --------------
    //
    //  LES REGLES SONT ICI, A COTE DE LA COMMANDE QUI LES FAIT RESPECTER, et pas
    //  dans le dialogue. `AddVariableCommand` refuse deja un tableau de blocs
    //  fonctionnels ; un dialogue qui l'offre et une commande qui le refuse, c'est
    //  un dialogue a qui on n'obeit plus. Une seule formulation des regles, donc,
    //  et elle se teste sans ecran.
    struct TypeChoice {
        std::string name;
        bool        allowsArray{ true };   // un ARRAY OF <bloc> n'existe pas
        std::string detail;              // "type derive", "DFB v1.01", "elementaire"
    };
    [[nodiscard]] std::vector<TypeChoice> availableTypeChoices(const domain::Project&);

    // "ST_EQ_Pump" + tableau + "0..15"  ->  "ARRAY[0..15] OF ST_EQ_Pump".
    // Le classificateur de l'importateur relit ensuite cette chaine : on ecrit donc
    // la forme qu'un export ecrirait, pas une forme a nous.
    [[nodiscard]] std::string composeTypeName(std::string type, bool array,
        std::string dimensions);

    // L'etat du formulaire : ce que l'utilisateur a choisi en entree, ce qui est
    // permis en sortie. `applyVariableFormRules` ne lit que les quatre premiers
    // champs et remplit les autres.
    struct VariableFormState {
        // ---- ce que le formulaire dit --------------------------------------
        std::string scope{ "Global" };     // Global / Input / Output / InOut / Public / Private / Member
        std::string container;           // le libelle choisi dans "Declared in"
        std::string type{ "BOOL" };
        bool        arrayWanted{ false };

        // ---- ce que les regles rendent -------------------------------------
        std::vector<std::string> containerChoices;
        bool                     containerEnabled{ true };
        std::string              containerHint;
        std::vector<std::string> typeChoices;
        bool                     arrayAllowed{ true };
        std::string              arrayHint;
    };
    void applyVariableFormRules(const domain::Project&, VariableFormState&);

    // --- ce qu'un nom designe, sous le curseur ----------------------------------
    //
    //  LA BARRE SOUS LE CODE. Un lecteur qui doit quitter sa section pour savoir ce
    //  qu'est `stnew1.test2` a perdu le fil de ce qu'il lisait. Tout ce qui se dit
    //  d'un nom tient sur une ligne : ce que c'est, ou il vit, comment on l'atteint.
    //
    //  `found` a faux N'EST PAS UNE ERREUR : un nom peut etre un mot-cle, un membre
    //  d'une structure, ou une faute de frappe, et c'est justement ce que la
    //  couleur ambre de la barre dit au lecteur.
    struct SymbolInfo {
        bool          found{ false };
        std::string   name;          // le nom tel qu'il a ete cherche
        std::string   type;
        std::string   scopeLabel;    // "globale", "locale de PompeGavage", "membre de ST_EQ_Pump"
        std::string   address;       // vide si non localisee
        std::string   comment;
        std::string   container;     // le POU ou le DDT qui la porte
        std::uint32_t usageCount{ 0 };
        domain::Index variable{ domain::kNoIndex };
    };

    // `section` sert a chercher d'abord dans le POU qui la contient : c'est la que
    // le nom a le plus de chances d'etre declare, et une locale doit gagner sur une
    // globale du meme nom - c'est ce que fait l'automate a l'execution.
    [[nodiscard]] SymbolInfo describeSymbol(const domain::Project&, domain::Index section,
        std::string_view name);

    // La ligne, prete a afficher. Separee de describeSymbol pour que le calcul se
    // teste sans se soucier de la mise en forme, et que la mise en forme change
    // sans toucher au calcul.
    [[nodiscard]] std::string symbolStatusLine(const SymbolInfo&);

    // --- what the editor offers while you type -----------------------------------
    struct Suggestion {
        std::string text;
        std::string detail;   // type, address, or the kind of thing it is
        int         rank{ 0 };  // 0 prefix match, 1 substring; lower sorts first
        enum class Kind : std::uint8_t {
            Keyword, Type, Variable, LocatedVariable, FunctionBlock, DerivedType, Function
        } kind{ Kind::Variable };

        // For a control structure, the whole skeleton rather than the bare word:
        // choosing IF should give you IF ... THEN / END_IF; with the cursor on the
        // condition, because the closing keyword is the one people forget.
        std::string insert;
        std::size_t caret{ std::string::npos };
    };

    // Symbols visible from `section`: the globals, that section's own POU
    // declarations, the project's types and blocks, and the ST keywords. Filtered
    // on `prefix`, which may be empty.
    [[nodiscard]] std::vector<Suggestion> suggestionsFor(const domain::Project&,
        domain::Index section,
        std::string_view prefix);

    // --- what a call takes -------------------------------------------------------
    struct CallSignature {
        std::string              name;
        std::vector<std::string> parameters;   // "IN  start : BOOL"
        std::string              returns;
    };

    // The interface of a DFB declared in the project, of an instance of one, or of
    // a standard IEC block. Returns false when the name is not a callable thing.
    [[nodiscard]] bool signatureFor(const domain::Project&, std::string_view name, CallSignature& out);

    // Containers a section or a variable can be created in, as label / index pairs.
    // Index kNoIndex means "not inside a POU".
    struct Container { std::string label; domain::Index index; };
    [[nodiscard]] std::vector<Container> sectionContainers(const domain::Project&);
    [[nodiscard]] std::vector<Container> variableContainers(const domain::Project&);

} // namespace project