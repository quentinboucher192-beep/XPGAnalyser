#include "EditCommands.hpp"

#include "../import/ProjectParser.hpp"
#include "BlockLibrary.hpp"
#include "../domain/ExecutionOrder.hpp"

#include <algorithm>
#include <cctype>

namespace project {

    using namespace domain;

    namespace {

        // Undo is only ever a truncation, and only ever of the element this command
        // appended. If anything else has been appended since, the stack has not
        // unwound in order and removing would corrupt the model - so refuse instead.
        template <class T>
        core::Status popIfLast(std::vector<T>& v, Index index, const char* what) {
            if (index == kNoIndex) return core::ok();          // never executed
            if (index + 1 != v.size())
                return core::fail(core::ErrorCode::OutOfRange,
                    std::string("cannot undo the creation of this ") + what
                    + ": something was added after it");
            v.pop_back();
            return core::ok();
        }

    } // namespace

    bool sectionNameIsFree(const Project& p, std::string_view name, Index owner) {
        if (name.empty()) return false;
        for (const auto& s : p.sections) {
            if (p.strings.text(s.name) != name) continue;
            // Same name in a different container is legal, and common: every
            // program unit in the reference project has its own "Init".
            if (s.owner == owner) return false;
            if (owner == kNoIndex && s.owner != kNoIndex && s.owner < p.pous.size()
                && p.pous[s.owner].kind == PouKind::Section)
                return false;                       // both are bare task sections
        }
        return true;
    }

    std::vector<std::string> availableTypes(const Project& p) {
        std::vector<std::string> out = {
            "BOOL", "EBOOL", "BYTE", "WORD", "DWORD", "INT", "UINT", "DINT", "UDINT",
            "REAL", "TIME", "DATE", "TOD", "DT", "STRING",
        };
        // A project's own types are what people actually declare with, so they go
        // in the same list rather than having to be typed from memory.
        for (const auto& d : p.derivedTypes) out.emplace_back(p.strings.text(d.name));
        for (const auto& pou : p.pous)
            if (pou.kind == PouKind::FunctionBlockType) out.emplace_back(p.strings.text(pou.name));
        return out;
    }

    std::vector<Container> sectionContainers(const Project& p) {
        std::vector<Container> out;
        out.push_back(Container{ "<task sections>", kNoIndex });
        for (Index i = 0; i < p.pous.size(); ++i) {
            if (p.pous[i].kind == PouKind::ProgramUnit)
                out.push_back(Container{ "unit: " + std::string(p.strings.text(p.pous[i].name)), i });
            else if (p.pous[i].kind == PouKind::FunctionBlockType)
                out.push_back(Container{ "DFB: " + std::string(p.strings.text(p.pous[i].name)), i });
        }
        return out;
    }

    std::vector<Container> variableContainers(const Project& p) {
        std::vector<Container> out;
        out.push_back(Container{ "<global>", kNoIndex });
        for (Index i = 0; i < p.pous.size(); ++i) {
            if (p.pous[i].kind == PouKind::ProgramUnit)
                out.push_back(Container{ "unit: " + std::string(p.strings.text(p.pous[i].name)), i });
            else if (p.pous[i].kind == PouKind::FunctionBlockType)
                out.push_back(Container{ "DFB: " + std::string(p.strings.text(p.pous[i].name)), i });
        }
        // DDT members are addressed by the derived-type index, flagged by the scope.
        for (Index i = 0; i < p.derivedTypes.size(); ++i)
            out.push_back(Container{ "DDT: " + std::string(p.strings.text(p.derivedTypes[i].name)), i });
        return out;
    }

    bool nameIsFree(const Project& p, std::string_view name) {
        if (name.empty()) return false;
        auto same = [&](SymbolId id) { return p.strings.text(id) == name; };
        for (const auto& d : p.derivedTypes) if (same(d.name)) return false;
        for (const auto& pou : p.pous)       if (same(pou.name)) return false;
        for (const auto& s : p.sections)     if (same(s.name)) return false;
        for (const auto& v : p.variables)
            if (v.scope == VariableScope::Global && same(v.name)) return false;
        return true;
    }

    // ================================================ le formulaire de variable ==
    std::vector<TypeChoice> availableTypeChoices(const Project& p) {
        std::vector<TypeChoice> out;
        static constexpr std::string_view kElementary[] = {
            "BOOL", "EBOOL", "BYTE", "WORD", "DWORD", "INT", "UINT", "DINT", "UDINT",
            "REAL", "TIME", "DATE", "TOD", "DT", "STRING",
        };
        for (auto e : kElementary) out.push_back({ std::string(e), true, "elementaire" });

        for (const auto& d : p.derivedTypes)
            out.push_back({ std::string(p.strings.text(d.name)), true, "type derive" });

        // UN TABLEAU DE BLOCS FONCTIONNELS N'EXISTE PAS. Un bloc a un etat et des
        // instances qui se declarent une par une ; la commande le refuse deja, et
        // le dire ici evite de l'offrir pour le reprendre ensuite.
        for (const auto& pou : p.pous)
            if (pou.kind == PouKind::FunctionBlockType)
                out.push_back({ std::string(p.strings.text(pou.name)), false,
                               "DFB v" + pou.version });
        return out;
    }

    std::string composeTypeName(std::string type, bool array, std::string dimensions) {
        if (!array) return type;

        // Des bornes vides ou saugrenues ne doivent pas produire "ARRAY[] OF X",
        // qui ne se relit pas : on retombe sur la forme la plus courante plutot que
        // d'ecrire quelque chose d'invalide.
        std::string dims;
        for (char c : dimensions)
            if (!std::isspace(static_cast<unsigned char>(c))) dims += c;
        if (dims.empty() || dims.find("..") == std::string::npos) dims = "0..9";

        return "ARRAY[" + dims + "] OF " + type;
    }

    void applyVariableFormRules(const Project& p, VariableFormState& st) {
        const bool isGlobal = st.scope == "Global";
        const bool isMember = st.scope == "Member";

        // --- ou la variable peut etre declaree -----------------------------------
        st.containerChoices.clear();
        if (isGlobal) {
            // Une globale n'appartient a personne, et proposer une liste de
            // conteneurs laisserait croire le contraire.
            st.containerChoices.push_back("<global>");
            st.container = "<global>";
            st.containerEnabled = false;
            st.containerHint = "une variable globale n'est dans aucun conteneur";
        }
        else if (isMember) {
            for (const auto& c : variableContainers(p))
                if (c.label.rfind("DDT: ", 0) == 0) st.containerChoices.push_back(c.label);
            st.containerEnabled = true;
            st.containerHint = st.containerChoices.empty()
                ? "aucun type derive dans ce projet : creez-en un d'abord"
                : "le champ appartiendra a ce type derive";
        }
        else {
            for (const auto& c : variableContainers(p))
                if (c.label.rfind("unit: ", 0) == 0 || c.label.rfind("DFB: ", 0) == 0)
                    st.containerChoices.push_back(c.label);
            st.containerEnabled = true;
            st.containerHint = st.scope == "Input" || st.scope == "Output" || st.scope == "InOut"
                ? "ce parametre fera partie de l'interface de ce bloc"
                : "cette variable sera privee a ce bloc";
        }
        // Le choix courant doit rester dans la liste : une valeur qui n'y est plus
        // s'affiche comme selectionnee et ne l'est pas.
        if (std::find(st.containerChoices.begin(), st.containerChoices.end(), st.container)
            == st.containerChoices.end())
            st.container = st.containerChoices.empty() ? std::string{} : st.containerChoices.front();

        // --- quels types ---------------------------------------------------------
        const auto choices = availableTypeChoices(p);
        st.typeChoices.clear();
        for (const auto& c : choices) st.typeChoices.push_back(c.name);
        if (std::find(st.typeChoices.begin(), st.typeChoices.end(), st.type)
            == st.typeChoices.end())
            st.type = st.typeChoices.empty() ? std::string{} : st.typeChoices.front();

        // --- tableau ou non ------------------------------------------------------
        const auto it = std::find_if(choices.begin(), choices.end(),
            [&](const TypeChoice& c) { return c.name == st.type; });
        st.arrayAllowed = it == choices.end() ? true : it->allowsArray;
        if (!st.arrayAllowed) {
            st.arrayWanted = false;
            st.arrayHint = "un tableau de blocs fonctionnels n'existe pas : "
                "declarez les instances une par une";
        }
        else {
            st.arrayHint = st.arrayWanted ? "les bornes vont dans Dimensions"
                : "ARRAY[...] OF " + st.type;
        }
    }

    // ================================================= ce qu'un nom designe ======
    namespace {

        // "Pompes[0].Fbk" -> "Pompes". C'est la variable declaree ; le reste est un
        // chemin dans sa valeur, et le declarer nulle part est normal.
        std::string rootOf(std::string_view name) {
            std::size_t end = 0;
            while (end < name.size() && name[end] != '.' && name[end] != '[') ++end;
            return std::string(name.substr(0, end));
        }

        std::string_view scopeWord(VariableScope s) {
            switch (s) {
            case VariableScope::Global:        return "globale";
            case VariableScope::Constant:      return "constante";
            case VariableScope::Input:         return "entree";
            case VariableScope::Output:        return "sortie";
            case VariableScope::InOut:         return "entree/sortie";
            case VariableScope::Public:        return "publique";
            case VariableScope::DerivedMember: return "membre";
            default:                           return "privee";
            }
        }

    } // namespace

    SymbolInfo describeSymbol(const Project& p, Index section, std::string_view name) {
        SymbolInfo info;
        info.name = std::string(name);
        if (name.empty()) return info;

        const std::string root = rootOf(name);
        if (root.empty()) return info;

        // LE POU DE LA SECTION D'ABORD. Une locale et une globale peuvent porter le
        // meme nom, et c'est la locale que l'automate lit : chercher globalement
        // d'abord afficherait la mauvaise.
        const Index owner = section < p.sections.size() ? p.sections[section].owner : kNoIndex;
        auto matches = [&](Index vi) { return p.strings.text(p.variables[vi].name) == root; };

        Index found = kNoIndex;
        if (owner < p.pous.size()) {
            for (auto vi : p.pous[owner].parameters) if (matches(vi)) { found = vi; break; }
            if (found == kNoIndex)
                for (auto vi : p.pous[owner].locals)  if (matches(vi)) { found = vi; break; }
        }
        if (found == kNoIndex)
            for (Index vi = 0; vi < p.variables.size(); ++vi) {
                const auto& v = p.variables[vi];
                if ((v.scope == VariableScope::Global || v.scope == VariableScope::Constant)
                    && matches(vi)) {
                    found = vi; break;
                }
            }
        if (found == kNoIndex) return info;

        const auto& v = p.variables[found];
        info.found = true;
        info.variable = found;
        info.type = std::string(p.strings.text(v.type.name));
        info.address = v.address.raw;
        info.comment = std::string(p.strings.text(v.comment));
        info.scopeLabel = std::string(scopeWord(v.scope));

        if (v.scope == VariableScope::DerivedMember && v.owner < p.derivedTypes.size())
            info.container = std::string(p.strings.text(p.derivedTypes[v.owner].name));
        else if (v.owner < p.pous.size())
            info.container = std::string(p.strings.text(p.pous[v.owner].name));

        // Combien de fois elle est nommee ailleurs. Le compte vaut ce que vaut une
        // recherche textuelle : il repond a "est-ce que ca sert", pas a "ou".
        for (const auto& s : p.sections) {
            std::size_t at = 0;
            while ((at = s.body.find(root, at)) != std::string::npos) {
                const bool leftOk = at == 0
                    || (!std::isalnum(static_cast<unsigned char>(s.body[at - 1]))
                        && s.body[at - 1] != '_');
                const auto after = at + root.size();
                const bool rightOk = after >= s.body.size()
                    || (!std::isalnum(static_cast<unsigned char>(s.body[after]))
                        && s.body[after] != '_');
                if (leftOk && rightOk) ++info.usageCount;
                at = after;
            }
        }
        return info;
    }

    std::string symbolStatusLine(const SymbolInfo& info) {
        if (!info.found)
            return info.name.empty()
            ? std::string{}
        : info.name + "  -  non declare dans ce projet";

        std::string line = info.name + " : " + info.type;
        if (!info.address.empty()) line += "   " + info.address;
        line += "   " + info.scopeLabel;
        if (!info.container.empty()) line += " de " + info.container;
        line += "   " + std::to_string(info.usageCount)
            + (info.usageCount == 1 ? " emploi" : " emplois");
        if (!info.comment.empty()) line += "   // " + info.comment;
        return line;
    }

    // ============================================================== sections ====
    AddSectionCommand::AddSectionCommand(ProjectPtr project, std::string name, std::string task,
        PouLanguage language, Index owner, bool subroutine)
        : project_(std::move(project)), name_(std::move(name)), task_(std::move(task)),
        language_(language), owner_(owner), subroutine_(subroutine) {}

    core::Status AddSectionCommand::execute() {
        if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "no project");
        auto& p = *project_;
        if (name_.empty()) return core::fail(core::ErrorCode::InvalidArgument, "the section needs a name");
        if (!sectionNameIsFree(p, name_, owner_))
            return core::fail(core::ErrorCode::DuplicateSymbol,
                owner_ == kNoIndex
                ? "a task section called '" + name_ + "' already exists"
                : "'" + name_ + "' already exists in this program unit or DFB");
        if (owner_ != kNoIndex && owner_ >= p.pous.size())
            return core::fail(core::ErrorCode::OutOfRange, "the owning POU does not exist");
        // Une SR est toujours a elle-meme son propre POU : une sous-routine DANS un
        // DFB n'existe pas dans Control Expert. Le dire plutot que d'ignorer l'un
        // des deux arguments en silence.
        if (subroutine_ && owner_ != kNoIndex)
            return core::fail(core::ErrorCode::InvalidArgument,
                "une sous-routine ne se cree pas dans un bloc : elle a son propre POU");

        Section s;
        s.name = p.strings.intern(name_);
        s.language = language_;
        s.owner = owner_;
        // Only a task section carries a task; a POU body does not, and giving it one
        // would put it in the task descriptor on export.
        //
        // UNE SR N'EN PORTE PAS NON PLUS, et c'est ce qui la garde hors du cycle :
        // `taskOf` rend la tache de la section quand elle en a une, et ne remonte au
        // POU que pour les unites de programme. Une SR dont la section porterait
        // "MAST" apparaitrait dans l'ordre d'execution et tournerait a chaque scan -
        // exactement ce qu'une sous-routine n'est pas.
        if (owner_ == kNoIndex && !subroutine_) s.task = p.strings.intern(task_);
        s.body = "(* " + name_ + " *)\n";
        s.lineCount = 2;

        // Execution order: after everything already in the same task - its
        // sections AND its program units, which run as a block at their own rank
        // (lot API 4). In a program unit: after the unit's own sections.
        std::uint32_t highest = 0;
        if (owner_ != kNoIndex && owner_ < p.pous.size()) {
            for (const auto si : p.pous[owner_].sections)
                if (si < p.sections.size()) highest = std::max(highest, p.sections[si].order);
        } else {
            for (const auto& other : p.sections)
                if (other.task == s.task) highest = std::max(highest, other.order);
            if (s.task != 0)
                for (const auto& pou : p.pous)
                    if (pou.kind == PouKind::ProgramUnit && pou.task == s.task) highest = std::max(highest, pou.order);
        }
        s.order = highest + 1;

        p.sections.push_back(std::move(s));
        sectionIndex_ = static_cast<Index>(p.sections.size() - 1);

        if (owner_ != kNoIndex && owner_ < p.pous.size()) {
            p.pous[owner_].sections.push_back(sectionIndex_);
        }
        else {
            // A bare task section gets its own POU, so the explorer stays uniform.
            Pou pou;
            pou.name = p.sections[sectionIndex_].name;
            pou.kind = subroutine_ ? PouKind::SubRoutine : PouKind::Section;
            // La tache vit sur le POU pour une SR : elle appartient bien a MAST -
            // l'export le demande - sans etre ordonnancee par elle.
            if (subroutine_) pou.task = p.strings.intern(task_);
            pou.sections.push_back(sectionIndex_);
            p.pous.push_back(std::move(pou));
            createdPou_ = static_cast<Index>(p.pous.size() - 1);
            p.sections[sectionIndex_].owner = createdPou_;
        }

        // Une SR n'entre pas dans la liste des sections de la tache : cette liste
        // est ce que le cycle parcourt.
        //
        // CE GARDE EST UN SECOND VERROU, et une mutation le montre : l'enlever ne
        // casse aucun test, parce que la section d'une SR porte `task == 0` et que
        // la boucle ne trouve alors aucune tache. Il est garde expres - si un jour
        // l'export demande que la section porte sa tache, c'est lui, et lui seul,
        // qui empechera la sous-routine de tourner a chaque cycle.
        if (!subroutine_)
            for (auto& t : p.tasks)
                if (t.name == p.sections[sectionIndex_].task) {
                    t.sections.push_back(sectionIndex_);
                    break;
                }
        p.buildIndices();
        return core::ok();
    }

    core::Status AddSectionCommand::undo() {
        if (!project_ || sectionIndex_ == kNoIndex) return core::ok();
        auto& p = *project_;

        for (auto& t : p.tasks) std::erase(t.sections, sectionIndex_);
        if (owner_ != kNoIndex && owner_ < p.pous.size())
            std::erase(p.pous[owner_].sections, sectionIndex_);

        if (createdPou_ != kNoIndex)
            if (auto r = popIfLast(p.pous, createdPou_, "section"); !r) return r;
        if (auto r = popIfLast(p.sections, sectionIndex_, "section"); !r) return r;

        sectionIndex_ = createdPou_ = kNoIndex;
        p.buildIndices();
        return core::ok();
    }

    std::string AddSectionCommand::label() const {
        return (subroutine_ ? "Ajouter la sous-routine " : "Ajouter la section ") + name_;
    }

    // ========================================================= program units ====
    AddProgramUnitCommand::AddProgramUnitCommand(ProjectPtr project, std::string name, std::string task)
        : project_(std::move(project)), name_(std::move(name)), task_(std::move(task)) {}

    core::Status AddProgramUnitCommand::execute() {
        if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "no project");
        auto& p = *project_;
        if (!nameIsFree(p, name_))
            return core::fail(core::ErrorCode::DuplicateSymbol,
                "'" + name_ + "' is already used in this project");

        Pou pou;
        pou.name = p.strings.intern(name_);
        pou.kind = PouKind::ProgramUnit;
        pou.task = p.strings.intern(task_);

        // Apres tout ce que la tache execute deja : ses unites ET ses sections
        // (une unite tourne en bloc a son rang parmi elles - lot API 4).
        std::uint32_t highest = 0;
        for (const auto& other : p.pous)
            if (other.kind == PouKind::ProgramUnit && other.task == pou.task)
                highest = std::max(highest, other.order);
        for (const auto& s : p.sections)
            if (s.task == pou.task) highest = std::max(highest, s.order);
        pou.order = highest + 1;

        p.pous.push_back(std::move(pou));
        pouIndex_ = static_cast<Index>(p.pous.size() - 1);
        p.buildIndices();
        return core::ok();
    }

    core::Status AddProgramUnitCommand::undo() {
        if (!project_ || pouIndex_ == kNoIndex) return core::ok();
        if (auto r = popIfLast(project_->pous, pouIndex_, "program unit"); !r) return r;
        pouIndex_ = kNoIndex;
        project_->buildIndices();
        return core::ok();
    }

    std::string AddProgramUnitCommand::label() const { return "Ajouter l'unit\xC3\xA9 de programme " + name_; }

    // ============================================================ DFB types =====
    AddFunctionBlockCommand::AddFunctionBlockCommand(ProjectPtr project, std::string name,
        std::string version)
        : project_(std::move(project)), name_(std::move(name)), version_(std::move(version)) {}

    core::Status AddFunctionBlockCommand::execute() {
        if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "no project");
        auto& p = *project_;
        if (!nameIsFree(p, name_))
            return core::fail(core::ErrorCode::DuplicateSymbol,
                "'" + name_ + "' is already used in this project");

        Pou pou;
        pou.name = p.strings.intern(name_);
        pou.kind = PouKind::FunctionBlockType;
        pou.version = version_.empty() ? "0.01" : version_;
        // Control Expert sets this flag on types it generates; keeping it means a
        // block created here is signed the same way as one created there.
        pou.attributes.emplace_back("UseNewTplSignAlgo", "TRUE");
        p.pous.push_back(std::move(pou));
        pouIndex_ = static_cast<Index>(p.pous.size() - 1);

        LibraryEntry lib;
        lib.name = p.pous[pouIndex_].name;
        lib.kind = LibraryKind::User;
        lib.version = p.pous[pouIndex_].version;
        lib.family = p.strings.intern("Custom");
        lib.pouIndex = pouIndex_;
        p.libraries.push_back(lib);
        libraryIndex_ = static_cast<Index>(p.libraries.size() - 1);

        p.buildIndices();
        return core::ok();
    }

    core::Status AddFunctionBlockCommand::undo() {
        if (!project_ || pouIndex_ == kNoIndex) return core::ok();
        auto& p = *project_;
        if (auto r = popIfLast(p.libraries, libraryIndex_, "DFB type"); !r) return r;
        if (auto r = popIfLast(p.pous, pouIndex_, "DFB type"); !r) return r;
        pouIndex_ = libraryIndex_ = kNoIndex;
        p.buildIndices();
        return core::ok();
    }

    std::string AddFunctionBlockCommand::label() const { return "Ajouter le bloc DFB " + name_; }

    // ======================================================== derived types =====
    AddDerivedTypeCommand::AddDerivedTypeCommand(ProjectPtr project, std::string name,
        std::string version)
        : project_(std::move(project)), name_(std::move(name)), version_(std::move(version)) {}

    core::Status AddDerivedTypeCommand::execute() {
        if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "no project");
        auto& p = *project_;
        if (!nameIsFree(p, name_))
            return core::fail(core::ErrorCode::DuplicateSymbol,
                "'" + name_ + "' is already used in this project");

        DerivedType d;
        d.name = p.strings.intern(name_);
        d.version = version_.empty() ? "0.01" : version_;
        p.derivedTypes.push_back(std::move(d));
        typeIndex_ = static_cast<Index>(p.derivedTypes.size() - 1);
        p.buildIndices();
        return core::ok();
    }

    core::Status AddDerivedTypeCommand::undo() {
        if (!project_ || typeIndex_ == kNoIndex) return core::ok();
        if (auto r = popIfLast(project_->derivedTypes, typeIndex_, "derived type"); !r) return r;
        typeIndex_ = kNoIndex;
        project_->buildIndices();
        return core::ok();
    }

    std::string AddDerivedTypeCommand::label() const { return "Ajouter le type d\xC3\xA9riv\xC3\xA9 " + name_; }

    // ============================================================ variables =====
    AddVariableCommand::AddVariableCommand(ProjectPtr project, Spec spec)
        : project_(std::move(project)), spec_(std::move(spec)) {}

    core::Status AddVariableCommand::execute() {
        if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "no project");
        auto& p = *project_;
        if (spec_.name.empty())
            return core::fail(core::ErrorCode::InvalidArgument, "the variable needs a name");

        // A global name must be unique; a local one only within its POU, which the
        // owner check below covers.
        if (spec_.scope == VariableScope::Global && !nameIsFree(p, spec_.name))
            return core::fail(core::ErrorCode::DuplicateSymbol,
                "'" + spec_.name + "' is already declared");

        // Everything that can fail is checked BEFORE the model is touched. A command
        // that half-executes and then reports failure is worse than one that fails,
        // because the caller has no way to know what to put back.
        Address address;
        if (!spec_.address.empty()) {
            address = Address::parse(spec_.address);
            if (!address.valid())
                return core::fail(core::ErrorCode::InvalidArgument,
                    "'" + spec_.address + "' is not an address: %MW10, %I0.3, %Q0.1...");
        }
        if (spec_.scope == VariableScope::DerivedMember && spec_.owner >= p.derivedTypes.size())
            return core::fail(core::ErrorCode::OutOfRange, "the owning derived type does not exist");
        if (spec_.scope != VariableScope::DerivedMember && spec_.scope != VariableScope::Global
            && spec_.owner >= p.pous.size())
            return core::fail(core::ErrorCode::OutOfRange, "the owning POU does not exist");

        Variable v;
        v.name = p.strings.intern(spec_.name);
        v.scope = spec_.scope;
        v.owner = spec_.owner;
        // The same classifier the importer uses, so "ARRAY[0..9] OF ST_Cabinet"
        // typed into the dialog means exactly what it means in an export - bounds,
        // element type and all. Writing a second parser here would have been a
        // second chance to disagree with the first.
        v.type = importer::classifyTypeName(spec_.type, p.strings);
        v.comment = p.strings.intern(spec_.comment);
        v.initValue = p.strings.intern(spec_.initValue);
        v.address = std::move(address);
        v.located = v.address.valid();

        // Resolve the type now so the tree and the memory figures are right at once.
        // An array resolves on its element type, not on the whole declaration.
        const auto elementId = v.type.elementType ? v.type.elementType : v.type.name;
        const bool isArray = v.type.klass == TypeClass::Array;
        if (auto it = p.typeByName.find(elementId); it != p.typeByName.end()) {
            v.type.derivedIndex = it->second;
            // An array of a structure stays an array: resolving the element must not
            // overwrite what the declaration said the variable is.
            if (!isArray) v.type.klass = TypeClass::Derived;
        }
        else if (auto ip = p.pouByName.find(elementId);
            ip != p.pouByName.end() && p.pous[ip->second].kind == PouKind::FunctionBlockType) {
            // An ARRAY OF a DFB type is not a thing: a function block has state and
            // instances of it are declared one by one, which is also why the editor
            // only offers ARRAY for derived types.
            if (isArray)
                return core::fail(core::ErrorCode::InvalidArgument,
                    "an array of function blocks is not allowed; declare the instances "
                    "individually");
            v.type.fbTypeIndex = ip->second;
            v.type.klass = TypeClass::FunctionBlock;
        }

        p.variables.push_back(std::move(v));
        variableIndex_ = static_cast<Index>(p.variables.size() - 1);

        if (spec_.scope == VariableScope::DerivedMember && spec_.owner < p.derivedTypes.size()) {
            p.derivedTypes[spec_.owner].fields.push_back(variableIndex_);
        }
        else if (spec_.owner < p.pous.size()) {
            auto& pou = p.pous[spec_.owner];
            if (spec_.scope == VariableScope::Input || spec_.scope == VariableScope::Output
                || spec_.scope == VariableScope::InOut)
                pou.parameters.push_back(variableIndex_);
            else if (spec_.scope != VariableScope::Global)
                pou.locals.push_back(variableIndex_);
        }
        p.buildIndices();
        return core::ok();
    }

    core::Status AddVariableCommand::undo() {
        if (!project_ || variableIndex_ == kNoIndex) return core::ok();
        auto& p = *project_;

        if (spec_.scope == VariableScope::DerivedMember && spec_.owner < p.derivedTypes.size())
            std::erase(p.derivedTypes[spec_.owner].fields, variableIndex_);
        else if (spec_.owner < p.pous.size()) {
            std::erase(p.pous[spec_.owner].parameters, variableIndex_);
            std::erase(p.pous[spec_.owner].locals, variableIndex_);
        }
        if (auto r = popIfLast(p.variables, variableIndex_, "variable"); !r) return r;
        variableIndex_ = kNoIndex;
        p.buildIndices();
        return core::ok();
    }

    std::string AddVariableCommand::label() const {
        return (spec_.scope == domain::VariableScope::DerivedMember ? "Ajouter le champ " : "Ajouter la variable ") + spec_.name;
    }

    // ========================================================= section bodies ====
    SetSectionBodyCommand::SetSectionBodyCommand(ProjectPtr project, Index section, std::string body)
        : project_(std::move(project)), section_(section), newBody_(std::move(body)) {}

    core::Status SetSectionBodyCommand::execute() {
        if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "no project");
        if (section_ >= project_->sections.size())
            return core::fail(core::ErrorCode::OutOfRange, "the section does not exist");

        auto& s = project_->sections[section_];
        if (!captured_) { oldBody_ = s.body; captured_ = true; }
        s.body = newBody_;

        // The metrics the dashboard and the analyzer read are derived from the body,
        // so they are refreshed here rather than going stale until the next import.
        s.lineCount = s.body.empty()
            ? 0u : static_cast<std::uint32_t>(std::count(s.body.begin(), s.body.end(), '\n')) + 1;
        std::uint32_t statements = 0;
        bool inComment = false, inString = false;
        for (std::size_t i = 0; i < s.body.size(); ++i) {
            if (inComment) {
                if (s.body[i] == '*' && i + 1 < s.body.size() && s.body[i + 1] == ')') { inComment = false; ++i; }
                continue;
            }
            if (inString) { if (s.body[i] == '\'') inString = false; continue; }
            if (s.body[i] == '(' && i + 1 < s.body.size() && s.body[i + 1] == '*') { inComment = true; ++i; continue; }
            if (s.body[i] == '\'') { inString = true; continue; }
            if (s.body[i] == ';') ++statements;
        }
        s.statementCount = statements;
        return core::ok();
    }

    core::Status SetSectionBodyCommand::undo() {
        if (!project_ || section_ >= project_->sections.size() || !captured_) return core::ok();
        auto restore = std::make_unique<SetSectionBodyCommand>(project_, section_, oldBody_);
        return restore->execute();
    }

    bool SetSectionBodyCommand::mergeableWith(const core::ICommand& other) const {
        const auto* edit = dynamic_cast<const SetSectionBodyCommand*>(&other);
        return edit && edit->section_ == section_;
    }

    void SetSectionBodyCommand::mergeFrom(const core::ICommand& other) {
        const auto* edit = dynamic_cast<const SetSectionBodyCommand*>(&other);
        if (!edit) return;
        // Keep the original "before" text and take the newest "after": undoing the
        // merged command returns to where the typing started.
        newBody_ = edit->newBody_;
        (void)execute();
    }

    std::string SetSectionBodyCommand::label() const { return "Modifier la section"; }

    // ============================================================ suggestions ====
    std::vector<Suggestion> suggestionsFor(const Project& p, Index section, std::string_view prefix) {
        std::vector<Suggestion> out;

        auto lowered = [](std::string_view s) {
            std::string t(s);
            std::transform(t.begin(), t.end(), t.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return t;
            };
        const auto needle = lowered(prefix);

        auto offer = [&](std::string_view text, std::string detail, Suggestion::Kind kind) {
            if (text.empty()) return;
            if (!needle.empty()) {
                const auto hay = lowered(text);
                const auto at = hay.find(needle);
                if (at == std::string::npos) return;
                out.push_back(Suggestion{ std::string(text), std::move(detail),
                                         at == 0 ? 0 : 1, kind, {}, std::string::npos });
                return;
            }
            out.push_back(Suggestion{ std::string(text), std::move(detail), 0, kind,
                                     {}, std::string::npos });
            };

        // Everything the section can actually name, in the order of usefulness:
        // its own POU's declarations first, then the globals, then types.
        const Index owner = section < p.sections.size() ? p.sections[section].owner : kNoIndex;
        if (owner < p.pous.size()) {
            const auto& pou = p.pous[owner];
            auto offerLocal = [&](Index vi) {
                const auto& v = p.variables[vi];
                std::string detail = std::string(p.strings.text(v.type.name)) + "  "
                    + std::string(toString(v.scope));
                offer(p.strings.text(v.name), std::move(detail),
                    v.located ? Suggestion::Kind::LocatedVariable : Suggestion::Kind::Variable);
                };
            for (auto vi : pou.parameters) offerLocal(vi);
            for (auto vi : pou.locals)     offerLocal(vi);
        }

        for (const auto& v : p.variables) {
            if (v.scope != VariableScope::Global && v.scope != VariableScope::Constant) continue;
            std::string detail = std::string(p.strings.text(v.type.name));
            if (v.located) detail += "  " + v.address.raw;
            offer(p.strings.text(v.name), std::move(detail),
                v.located ? Suggestion::Kind::LocatedVariable : Suggestion::Kind::Variable);
        }

        for (const auto& d : p.derivedTypes)
            offer(p.strings.text(d.name), "derived type", Suggestion::Kind::DerivedType);
        for (const auto& pou : p.pous)
            if (pou.kind == PouKind::FunctionBlockType)
                offer(p.strings.text(pou.name), "DFB v" + pou.version, Suggestion::Kind::FunctionBlock);

        // Control structures are offered as whole skeletons. The '|' marks where the
        // caret goes; it is stripped before insertion. Writing the closing keyword
        // for the user is the point: END_IF and END_WHILE are what gets forgotten,
        // and a missing one is a compile error a long way from its cause.
        struct Skeleton { std::string_view word, body; };
        static constexpr Skeleton kStructures[] = {
            {"IF",      "IF | THEN\n    \nEND_IF;"},
            {"IFELSE",  "IF | THEN\n    \nELSE\n    \nEND_IF;"},
            {"ELSIF",   "ELSIF | THEN\n    "},
            {"ELSE",    "ELSE\n    |"},
            {"FOR",     "FOR i := 0 TO | DO\n    \nEND_FOR;"},
            {"WHILE",   "WHILE | DO\n    \nEND_WHILE;"},
            {"REPEAT",  "REPEAT\n    |\nUNTIL \nEND_REPEAT;"},
            {"CASE",    "CASE | OF\n    1:\n        \nELSE\n        \nEND_CASE;"},
        };
        for (const auto& sk : kStructures) {
            const auto before = out.size();
            offer(sk.word, "structure", Suggestion::Kind::Keyword);
            if (out.size() == before) continue;           // filtered out by the prefix
            auto& added = out.back();
            added.insert = std::string(sk.body);
            const auto marker = added.insert.find('|');
            if (marker != std::string::npos) {
                added.insert.erase(marker, 1);
                added.caret = marker;
            }
            // "IFELSE" is a handle, not something anyone types; show what it makes.
            if (sk.word == "IFELSE") added.text = "IF ... ELSE";
        }

        static constexpr std::string_view kKeywords[] = {
            "THEN", "OF", "TO", "BY", "DO", "UNTIL", "EXIT", "RETURN",
            "END_IF", "END_FOR", "END_WHILE", "END_REPEAT", "END_CASE",
            "AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE",
        };
        for (auto k : kKeywords) offer(k, "keyword", Suggestion::Kind::Keyword);

        static constexpr std::string_view kTypes[] = {
            "BOOL", "EBOOL", "INT", "UINT", "DINT", "UDINT", "REAL", "WORD",
            "DWORD", "BYTE", "TIME", "STRING",
        };
        for (auto t : kTypes) offer(t, "type", Suggestion::Kind::Type);

        // Everything Control Expert provides, from the library file. The detail
        // column carries the family and, when the definition is not confirmed, says
        // so - a signature the tool guessed and one it knows are different claims.
        for (const auto& b : BlockLibrary::shared().all()) {
            std::string detail = b.family;
            if (b.provenance == Provenance::Unverified) detail += "  (unverified)";
            offer(b.name, std::move(detail),
                b.kind == BlockKind::FunctionBlock ? Suggestion::Kind::FunctionBlock
                : Suggestion::Kind::Function);
        }

        // A name offered twice is a name the reader has to think about.
        std::stable_sort(out.begin(), out.end(), [](const Suggestion& a, const Suggestion& b) {
            if (a.rank != b.rank) return a.rank < b.rank;
            return a.text < b.text;
            });
        out.erase(std::unique(out.begin(), out.end(),
            [](const Suggestion& a, const Suggestion& b) { return a.text == b.text; }),
            out.end());
        return out;
    }

    // ============================================================= signatures ====
    namespace {

        std::string_view directionOf(VariableScope scope) {
            switch (scope) {
            case VariableScope::Input:  return "IN ";
            case VariableScope::Output: return "OUT";
            case VariableScope::InOut:  return "I/O";
            default:                    return "   ";
            }
        }

    } // namespace

    bool signatureFor(const Project& p, std::string_view name, CallSignature& out) {
        if (name.empty()) return false;

        auto describe = [&](Index pouIndex) {
            const auto& pou = p.pous[pouIndex];
            out.name = std::string(p.strings.text(pou.name));
            out.parameters.clear();
            // Declaration order is kept: it is the order the arguments go in.
            for (auto vi : pou.parameters) {
                const auto& v = p.variables[vi];
                out.parameters.push_back(std::string(directionOf(v.scope)) + " "
                    + std::string(p.strings.text(v.name)) + " : "
                    + std::string(p.strings.text(v.type.name)));
            }
            };

        // A DFB type named directly.
        const auto id = const_cast<Project&>(p).strings.intern(name);
        if (auto it = p.pouByName.find(id); it != p.pouByName.end()
            && p.pous[it->second].kind == PouKind::FunctionBlockType) {
            describe(it->second);
            return !out.parameters.empty();
        }

        // An *instance* of one: `Timer(` should show what TON takes, not nothing.
        if (auto vi = p.variableByName.find(id); vi != p.variableByName.end()) {
            const auto& v = p.variables[vi->second];
            if (v.type.fbTypeIndex != kNoIndex) {
                describe(v.type.fbTypeIndex);
                out.name = std::string(name) + " : " + std::string(p.strings.text(v.type.name));
                return !out.parameters.empty();
            }
        }
        for (const auto& v : p.variables) {
            if (p.strings.text(v.name) != name) continue;
            if (v.type.fbTypeIndex == kNoIndex) continue;
            describe(v.type.fbTypeIndex);
            out.name = std::string(name) + " : " + std::string(p.strings.text(v.type.name));
            return !out.parameters.empty();
        }

        // A function or block the platform provides.
        if (const auto* b = BlockLibrary::shared().find(name)) {
            out.name = b->name;
            // Unverified definitions say so in the panel. Silently presenting a
            // guess with the same authority as a known signature is how a wrong
            // point count survived in the hardware catalogue.
            if (b->provenance == Provenance::Unverified) out.name += "  (unverified)";
            out.parameters.clear();
            for (const auto& param : b->parameters)
                out.parameters.push_back(std::string(param.directionLabel()) + " " + param.name
                    + " : " + param.type);
            out.returns = b->returns;
            return !out.parameters.empty();
        }
        return false;
    }

    // ================================================================ racks =====
    AddRackCommand::AddRackCommand(ProjectPtr project, std::string reference, std::string powerSupply)
        : project_(std::move(project)), reference_(std::move(reference)),
        powerSupply_(std::move(powerSupply)) {}

    core::Status AddRackCommand::execute() {
        if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "no project");
        auto& hw = project_->hardware;
        const auto catalog = importer::HardwareCatalog::builtinFallback();

        // The processor decides how many racks the configuration may have.
        if (const auto* cpu = catalog.findCpu(hw.cpuReference)) {
            if (cpu->maxRacks && hw.racks.size() >= cpu->maxRacks)
                return core::fail(core::ErrorCode::OutOfRange,
                    hw.cpuReference + " addresses at most "
                    + std::to_string(cpu->maxRacks) + " racks");
        }

        const auto* entry = catalog.findModule(reference_);
        if (!entry || entry->slots == 0)
            return core::fail(core::ErrorCode::UnknownCpuReference,
                "'" + reference_ + "' is not a backplane in the catalog");

        Rack rack;
        rack.number = static_cast<std::uint16_t>(hw.racks.size());
        rack.reference = reference_;
        rack.slotCount = entry->slots;
        rack.description = entry->description;
        rack.topologicalAddress = "\\0.0\\" + std::to_string(rack.number);

        // Every rack carries its supply, in its own double-width position rather
        // than in a numbered slot. Slot -1 is what the export uses for it.
        Module supply;
        supply.reference = powerSupply_;
        supply.family = "Supply";
        supply.kind = ModuleKind::PowerSupply;
        supply.rack = rack.number;
        supply.slot = -1;
        supply.firmware = "01.00";
        supply.topologicalAddress = "\\0.0\\" + std::to_string(rack.number) + ".(P) (P)";
        if (const auto* ps = catalog.findModule(powerSupply_)) {
            supply.description = ps->description;
            supply.knownReference = true;
        }
        rack.modules.push_back(std::move(supply));

        // Rack 0 also carries the processor, in slot 0, because that is the only
        // place it is allowed to be.
        if (rack.number == 0 && !hw.cpuReference.empty()) {
            Module cpu;
            cpu.reference = hw.cpuReference;
            cpu.family = hw.resourceName.empty() ? "Micro Basic" : hw.resourceName;
            cpu.kind = ModuleKind::Cpu;
            cpu.isCpu = true;
            cpu.rack = 0;
            cpu.slot = 0;
            cpu.firmware = hw.cpuFirmware.empty() ? "03.50" : hw.cpuFirmware;
            cpu.topologicalAddress = "\\0.0\\0.0";
            if (const auto* e = catalog.findModule(hw.cpuReference)) {
                cpu.description = e->description;
                cpu.knownReference = true;
            }
            rack.modules.push_back(std::move(cpu));
        }

        hw.racks.push_back(std::move(rack));
        hw.inferred = false;
        hw.powerSupply = powerSupply_;
        return core::ok();
    }

    core::Status AddRackCommand::undo() {
        if (!project_ || project_->hardware.racks.empty()) return core::ok();
        project_->hardware.racks.pop_back();       // takes its supply and CPU with it
        if (project_->hardware.racks.empty()) project_->hardware.inferred = true;
        return core::ok();
    }

    std::string AddRackCommand::label() const { return "Ajouter le rack " + reference_; }

    // ============================================================== modules =====
    AddModuleCommand::AddModuleCommand(ProjectPtr project, std::uint16_t rack, std::int16_t slot,
        std::string reference)
        : project_(std::move(project)), rack_(rack), slot_(slot), reference_(std::move(reference)) {}

    core::Status AddModuleCommand::execute() {
        if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "no project");
        auto& hw = project_->hardware;
        const auto catalog = importer::HardwareCatalog::builtinFallback();

        auto it = std::find_if(hw.racks.begin(), hw.racks.end(),
            [&](const Rack& r) { return r.number == rack_; });
        if (it == hw.racks.end())
            return core::fail(core::ErrorCode::OutOfRange, "rack " + std::to_string(rack_) + " does not exist");
        if (slot_ < 0)
            return core::fail(core::ErrorCode::OutOfRange,
                "the power supply has its own position and is placed with the rack");
        if (it->slotCount && slot_ >= static_cast<std::int16_t>(it->slotCount))
            return core::fail(core::ErrorCode::OutOfRange,
                "slot " + std::to_string(slot_) + " is past the end of a "
                + std::to_string(it->slotCount) + "-slot backplane");
        if (rack_ == 0 && slot_ == 0)
            return core::fail(core::ErrorCode::DuplicateSymbol,
                "slot 0 of rack 0 is the processor's: a BMX P34 processor is always "
                "installed in the slot marked 00");
        for (const auto& m : it->modules)
            if (m.slot == slot_)
                return core::fail(core::ErrorCode::DuplicateSymbol,
                    "slot " + std::to_string(slot_) + " already holds " + m.reference);

        // The processor's published limits, checked against what the configuration
        // would become rather than what it already is.
        if (const auto* cpu = catalog.findCpu(hw.cpuReference);
            cpu && cpu->maxDiscretePoints) {
            const auto* entry = catalog.findModule(reference_);
            const std::uint32_t addingDiscrete =
                entry && entry->slots == 0 ? entry->inputPoints + entry->outputPoints : 0u;
            std::uint32_t discrete = 0, analog = 0;
            for (const auto& rack : hw.racks)
                for (const auto& m : rack.modules) {
                    if (m.kind == ModuleKind::AnalogInput || m.kind == ModuleKind::AnalogOutput)
                        analog += m.points();
                    else
                        discrete += m.points();
                }
            if (discrete + addingDiscrete > cpu->maxDiscretePoints)
                return core::fail(core::ErrorCode::OutOfRange,
                    hw.cpuReference + " addresses at most "
                    + std::to_string(cpu->maxDiscretePoints)
                    + " discrete points; this would make "
                    + std::to_string(discrete + addingDiscrete));
            (void)analog;
        }

        Module m;
        m.reference = reference_;
        m.rack = rack_;
        m.slot = slot_;
        m.topologicalAddress = "\\0.0\\" + std::to_string(rack_) + "." + std::to_string(slot_);

        if (const auto* entry = catalog.findModule(reference_)) {
            if (entry->slots > 0)
                return core::fail(core::ErrorCode::InvalidArgument,
                    "'" + reference_ + "' is a backplane, not a module");
            m.knownReference = true;
            m.description = entry->description;
            m.inputPoints = entry->inputPoints;
            m.outputPoints = entry->outputPoints;
            m.pointsFromCatalog = true;      // nothing measured it: the catalog said so
            m.family = m.inputPoints && m.outputPoints ? "Discrete"
                : m.inputPoints ? "Discrete" : m.outputPoints ? "Discrete" : "Communication";
            m.kind = m.inputPoints && m.outputPoints ? ModuleKind::DiscreteMixed
                : m.inputPoints ? ModuleKind::DiscreteInput
                : m.outputPoints ? ModuleKind::DiscreteOutput
                : ModuleKind::Communication;
        }
        else {
            return core::fail(core::ErrorCode::UnknownCpuReference,
                "'" + reference_ + "' is not in the catalog; add it to "
                "resources/plc_catalog.txt first");
        }

        it->modules.push_back(std::move(m));
        std::stable_sort(it->modules.begin(), it->modules.end(),
            [](const Module& a, const Module& b) { return a.slot < b.slot; });
        return core::ok();
    }

    core::Status AddModuleCommand::undo() {
        if (!project_) return core::ok();
        for (auto& rack : project_->hardware.racks) {
            if (rack.number != rack_) continue;
            std::erase_if(rack.modules, [this](const Module& m) { return m.slot == slot_; });
            return core::ok();
        }
        return core::ok();
    }

    std::string AddModuleCommand::label() const {
        return "Ajouter " + reference_ + " au rack " + std::to_string(rack_)
            + ", emplacement " + std::to_string(slot_);
    }


    // ======================================================= ordre d'execution ==
    //
    //  Deplacer une section renumerote ses voisines : l'annulation doit donc
    //  rendre TOUS les `order` de la tache, et pas seulement celui de la section
    //  deplacee. Le cout est un vecteur de quelques dizaines de paires ; le prix
    //  de l'autre choix serait un Ctrl+Z qui rend un etat que personne n'a
    //  demande, ce qui est pire que pas de Ctrl+Z.
    ReorderSectionCommand::ReorderSectionCommand(ProjectPtr project, domain::Index section,
        int delta)
        : project_(std::move(project)), section_(section), delta_(delta) {}

    ReorderSectionCommand::ReorderSectionCommand(ProjectPtr project, domain::Index section,
        Before target)
        : project_(std::move(project)), section_(section), before_(target.section),
        byTarget_(true) {}

    core::Status ReorderSectionCommand::execute() {
        if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "no project");
        auto& p = *project_;
        if (section_ >= p.sections.size())
            return core::fail(core::ErrorCode::OutOfRange, "cette section n'existe plus");

        task_ = domain::taskOf(p, section_);
        if (task_ == 0)
            return core::fail(core::ErrorCode::InvalidArgument,
                "cette section n'est executee par aucune tache : "
                "le corps d'un DFB tourne quand on l'appelle, pas dans un cycle");

        // L'etat d'avant, capture avant la premiere renumerotation - y compris
        // celle que `moveSection` fait pour ecarter les ex aequo. Capturer apres
        // rendrait un projet deja modifie.
        previous_.clear();
        previousUnits_.clear();
        for (domain::Index i = 0; i < p.sections.size(); ++i)
            if (domain::taskOf(p, i) == task_) previous_.emplace_back(i, p.sections[i].order);
        for (domain::Index u = 0; u < p.pous.size(); ++u)
            if (p.pous[u].kind == domain::PouKind::ProgramUnit && p.pous[u].task == task_)
                previousUnits_.emplace_back(u, p.pous[u].order);
        for (const auto& t : p.tasks)
            if (t.name == task_) previousTaskCache_ = t.sections;

        const bool moved = byTarget_ ? domain::moveSectionBefore(p, section_, before_)
            : domain::moveSection(p, section_, delta_);
        if (!moved) {
            previous_.clear();
            previousTaskCache_.clear();
            return core::fail(core::ErrorCode::Cancelled,
                byTarget_ ? "la section est deja a cette place"
                : "la section est deja au bout de la tache");
        }
        return core::ok();
    }

    core::Status ReorderSectionCommand::undo() {
        if (!project_ || previous_.empty()) return core::ok();
        auto& p = *project_;
        for (const auto& [index, order] : previous_)
            if (index < p.sections.size()) p.sections[index].order = order;
        for (const auto& [unit, order] : previousUnits_)
            if (unit < p.pous.size()) p.pous[unit].order = order;
        for (auto& t : p.tasks)
            if (t.name == task_) t.sections = previousTaskCache_;
        return core::ok();
    }

    std::string ReorderSectionCommand::label() const {
        std::string name = "section";
        if (project_ && section_ < project_->sections.size())
            name = "'" + std::string(project_->strings.text(project_->sections[section_].name)) + "'";
        if (byTarget_) return "D\xC3\xA9placer " + name + " dans l'ordre d'ex\xC3\xA9" "cution";
        if (delta_ < 0) return "Monter " + name + " dans l'ordre d'ex\xC3\xA9" "cution";
        return "Descendre " + name + " dans l'ordre d'ex\xC3\xA9" "cution";
    }

} // namespace project