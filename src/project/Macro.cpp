#include "Macro.hpp"

#include "EditCommands.hpp"
#include "LibraryCatalog.hpp"
#include "SharedLibrary.hpp"
#include "../domain/ExecutionOrder.hpp"
#include "../xls/MacroXls.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace project {

    using sim::Value;

    // ---------------------------------------------------------------------------
    //  LOT MACROS 1 : LIBIMPORT EST UNE COMMANDE.
    //
    //  Il agissait sur le projet hors de la pile. Un import pose APRES une
    //  sous-routine de la macro faisait donc echouer le Ctrl+Z de cette
    //  sous-routine ("something was added after it") : ImporterClasseur se
    //  defaisait a moitie, sections et variables comprises.
    //
    //  Un import refuse d'ecraser (OnConflict::Refuse) n'AJOUTE qu'en fin de
    //  listes : les membres ou les parametres, le type ou le bloc, ses sections.
    //  Le defaire, c'est revenir aux tailles d'avant - la pile se deroule dans
    //  l'ordre, rien d'autre n'a ete ajoute depuis. Refaire l'importe a nouveau,
    //  depuis la bibliotheque de ce dossier (celle de la macro ne vit pas aussi
    //  longtemps que la pile).
    // ---------------------------------------------------------------------------
    class LibImportCommand final : public core::ICommand {
    public:
        LibImportCommand(std::shared_ptr<domain::Project> project, const SharedLibrary* library,
            std::string name)
            : project_(std::move(project)), library_(library),
              root_(library ? library->root() : std::string{}), name_(std::move(name)) {}

        core::Status execute() override {
            if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "no project");
            auto& p = *project_;
            variables_ = p.variables.size();
            types_     = p.derivedTypes.size();
            pous_      = p.pous.size();
            sections_  = p.sections.size();
            const SharedLibrary* library = library_;
            library_ = nullptr;
            std::unique_ptr<SharedLibrary> fresh;
            if (!library) {
                fresh = std::make_unique<SharedLibrary>(root_);
                if (auto r = fresh->scan(); !r) return r;
                library = fresh.get();
            }
            auto outcome = library->import(p, name_, SharedLibrary::OnConflict::Refuse);
            if (!outcome) return core::fail(outcome.error().code, outcome.error().context, outcome.error().source);
            importedAs_ = outcome->importedAs;
            applied_ = true;
            return core::ok();
        }

        core::Status undo() override {
            if (!applied_ || !project_) return core::ok();
            auto& p = *project_;
            if (p.variables.size() < variables_ || p.derivedTypes.size() < types_ || p.pous.size() < pous_
                || p.sections.size() < sections_)
                return core::fail(core::ErrorCode::OutOfRange,
                    "cannot undo the import of " + name_ + ": the project changed since");
            p.variables.erase(p.variables.begin() + static_cast<std::ptrdiff_t>(variables_), p.variables.end());
            p.derivedTypes.erase(p.derivedTypes.begin() + static_cast<std::ptrdiff_t>(types_), p.derivedTypes.end());
            p.pous.erase(p.pous.begin() + static_cast<std::ptrdiff_t>(pous_), p.pous.end());
            p.sections.erase(p.sections.begin() + static_cast<std::ptrdiff_t>(sections_), p.sections.end());
            p.buildIndices();
            p.linkTypes();
            applied_ = false;
            return core::ok();
        }

        [[nodiscard]] std::string label() const override { return "importe " + name_; }
        [[nodiscard]] const std::string& importedAs() const noexcept { return importedAs_; }

    private:
        std::shared_ptr<domain::Project> project_;
        const SharedLibrary*             library_{ nullptr };
        std::string                      root_, name_, importedAs_;
        std::size_t                      variables_{ 0 }, types_{ 0 }, pous_{ 0 }, sections_{ 0 };
        bool                             applied_{ false };
    };

    // ---------------------------------------------------------------------------
    //  Appending one line to a section.
    //
    //  Rule 1 has no exceptions: a macro that wrote into a section directly would be
    //  a macro whose effect Ctrl+Z could not reach. The previous body is kept whole
    //  rather than a length remembered - a few kilobytes, and exactly inverse.
    // ---------------------------------------------------------------------------
    class AppendToSectionCommand final : public core::ICommand {
    public:
        AppendToSectionCommand(std::shared_ptr<domain::Project> project,
            domain::Index section, std::string line)
            : project_(std::move(project)), section_(section), line_(std::move(line)) {}

        core::Status execute() override {
            if (!project_ || section_ >= project_->sections.size())
                return core::fail(core::ErrorCode::OutOfRange, "that section no longer exists");
            auto& body = project_->sections[section_].body;
            previous_ = body;
            if (!body.empty() && body.back() != '\n') body.push_back('\n');
            body += line_;
            body.push_back('\n');
            project_->sections[section_].lineCount =
                static_cast<std::uint32_t>(std::count(body.begin(), body.end(), '\n'));
            applied_ = true;
            return core::ok();
        }

        core::Status undo() override {
            if (!applied_ || !project_ || section_ >= project_->sections.size()) return core::ok();
            auto& body = project_->sections[section_].body;
            body = previous_;
            project_->sections[section_].lineCount =
                body.empty() ? 0u : static_cast<std::uint32_t>(
                    std::count(body.begin(), body.end(), '\n'));
            applied_ = false;
            return core::ok();
        }

        [[nodiscard]] std::string label() const override {
            std::string shown = line_;
            if (shown.size() > 48) shown = shown.substr(0, 45) + "...";
            return "append '" + shown + "'";
        }

    private:
        std::shared_ptr<domain::Project> project_;
        domain::Index                    section_;
        std::string                      line_;
        std::string                      previous_;
        bool                             applied_{ false };
    };

    // ---------------------------------------------------------------------------
    //  Vider une section.
    //
    //  Le corps precedent est garde en entier plutot qu'une longueur retenue :
    //  quelques kilo-octets, et l'inverse est exact.
    // ---------------------------------------------------------------------------
    class ClearSectionCommand final : public core::ICommand {
    public:
        ClearSectionCommand(std::shared_ptr<domain::Project> project, domain::Index section)
            : project_(std::move(project)), section_(section) {}

        core::Status execute() override {
            if (!project_ || section_ >= project_->sections.size())
                return core::fail(core::ErrorCode::OutOfRange, "that section no longer exists");
            previous_ = project_->sections[section_].body;
            project_->sections[section_].body.clear();
            project_->sections[section_].lineCount = 0;
            applied_ = true;
            return core::ok();
        }

        core::Status undo() override {
            if (!applied_ || !project_ || section_ >= project_->sections.size()) return core::ok();
            auto& body = project_->sections[section_].body;
            body = previous_;
            project_->sections[section_].lineCount =
                body.empty() ? 0u : static_cast<std::uint32_t>(
                    std::count(body.begin(), body.end(), '\n'));
            applied_ = false;
            return core::ok();
        }

        [[nodiscard]] std::string label() const override { return "clear section"; }

    private:
        std::shared_ptr<domain::Project> project_;
        domain::Index                    section_;
        std::string                      previous_;
        bool                             applied_{ false };
    };

    // ============================================================ CompoundCommand ==
    CompoundCommand::CompoundCommand(std::string label) : label_(std::move(label)) {}

    void CompoundCommand::add(core::CommandPtr command) {
        if (command) commands_.push_back(std::move(command));
    }

    void CompoundCommand::addApplied(core::CommandPtr command) {
        if (!command) return;
        commands_.push_back(std::move(command));
        applied_ = commands_.size();
        skipFirst_ = true;
    }

    core::Status CompoundCommand::execute() {
        // The macro ran its commands as it built them, so the first execute() after
        // that has nothing to do. Undo still works, and a redo comes back here with
        // skipFirst_ cleared and replays for real.
        if (skipFirst_) { skipFirst_ = false; return core::ok(); }

        // Applied in order, and on a failure everything already applied is rolled
        // back. Piege n 7 again, one level up: a compound command that fails halfway
        // must leave the project as it found it, or the reader is left with half a
        // macro and no way to name what happened.
        applied_ = 0;
        for (auto& command : commands_) {
            if (auto status = command->execute(); !status) {
                for (std::size_t i = applied_; i-- > 0;) (void)commands_[i]->undo();
                applied_ = 0;
                return status;
            }
            ++applied_;
        }
        return core::ok();
    }

    core::Status CompoundCommand::undo() {
        for (std::size_t i = applied_; i-- > 0;)
            if (auto status = commands_[i]->undo(); !status) return status;
        applied_ = 0;
        // Whatever the macro did as it ran has now been taken back, so the next
        // execute() is a real redo. Leaving the flag set made a redo AFTER AN UNDO
        // do nothing at all and report success - the worst pair of outcomes.
        skipFirst_ = false;
        return core::ok();
    }

    std::string CompoundCommand::label() const {
        return label_ + " (" + std::to_string(commands_.size()) + " etape(s))";
    }

    // ============================================================== Environment ===
    namespace {

        std::string lower(std::string_view s) {
            std::string out(s);
            std::transform(out.begin(), out.end(), out.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        std::string upper(std::string_view s) {
            std::string out(s);
            std::transform(out.begin(), out.end(), out.begin(),
                [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return out;
        }

        // Un texte en LITTERAL ST : entre apostrophes, l'apostrophe ecrite $' et
        // le dollar $$. Une designation comme "Arret d'urgence", recopiee telle
        // quelle entre deux apostrophes, fermait la chaine au milieu - et la
        // section entiere ne compilait plus.
        std::string stQuote(std::string_view s) {
            std::string out = "'";
            for (const char c : s) {
                if (c == '\'') out += "$'";
                else if (c == '$') out += "$$";
                else out.push_back(c);
            }
            out.push_back('\'');
            return out;
        }

        // Le texte en ASCII : les lettres accentuees du classeur (UTF-8) perdent
        // leur accent, le reste de ce qui n'est pas ASCII devient '?'. Une
        // designation saisie dans Excel porte des accents, et un fichier de
        // bibliotheque ou un libelle d'alarme n'en veut pas.
        std::string toAscii(std::string_view s) {
            // Les deux octets UTF-8 C3 xx, de U+00C0 a U+00FF.
            static const char* const kLatin1 =
                "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYTs"
                "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";
            std::string out;
            for (std::size_t i = 0; i < s.size(); ++i) {
                const auto c = static_cast<unsigned char>(s[i]);
                if (c < 0x80) { out.push_back(static_cast<char>(c)); continue; }
                if (c == 0xC3 && i + 1 < s.size()) {
                    const auto d = static_cast<unsigned char>(s[i + 1]);
                    if (d >= 0x80 && d <= 0xBF) {
                        out.push_back(kLatin1[d - 0x80]);
                        ++i;
                        continue;
                    }
                }
                if (c == 0xC5 && i + 1 < s.size()
                    && (static_cast<unsigned char>(s[i + 1]) == 0x92
                        || static_cast<unsigned char>(s[i + 1]) == 0x93)) {
                    out += static_cast<unsigned char>(s[i + 1]) == 0x92 ? "OE" : "oe";
                    ++i;
                    continue;
                }
                // Une suite multi-octets inconnue : un seul '?' pour elle entiere.
                std::size_t extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
                while (extra-- > 0 && i + 1 < s.size()
                       && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80)
                    ++i;
                out.push_back('?');
            }
            return out;
        }

        // Un fichier entier, ou rien. Les lectures de ce fichier-ci portent sur
        // une macro ou un bloc de quelques kilo-octets.
        std::string readWhole(const std::string& path) {
            std::ifstream in(path, std::ios::binary);
            if (!in) return {};
            std::ostringstream out;
            out << in.rdbuf();
            return out.str();
        }

        // ---------------------------------------------------------------------
        //  OU UNE MACRO A LE DROIT D'ECRIRE UN FICHIER.
        //
        //  A cote du classeur, et nulle part ailleurs. Une macro vient d'un
        //  dossier partage : si elle pouvait ecrire n'importe ou, une macro
        //  modifiee par erreur pourrait ecraser le classeur lui-meme, ou un
        //  bloc de libs/. D'ou trois refus, chacun dit :
        //
        //    - un chemin absolu, ou un ".." : on sort du dossier du classeur ;
        //    - une extension hors liste : .csv, .txt, .md, .st, .log, .json.
        //      Un .xlsm, un .mac ou un .dfb ne s'ecrit PAS depuis une macro ;
        //    - aucun classeur ouvert : il n'y a pas de "a cote".
        // ---------------------------------------------------------------------
        std::string outputPathFor(const std::string& wanted, std::string& why) {
            namespace fs = std::filesystem;
            why.clear();
            if (wanted.empty()) { why = "le nom du fichier est vide"; return {}; }
            const fs::path relative(wanted);
            if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()
                || wanted.find(':') != std::string::npos) {
                why = "'" + wanted + "' est un chemin absolu : une macro n'ecrit qu'a cote du classeur";
                return {};
            }
            for (const auto& part : relative)
                if (part == "..") {
                    why = "'" + wanted + "' remonte d'un dossier : une macro n'ecrit qu'a cote du classeur";
                    return {};
                }
            static const char* const kAllowed[] = { ".csv", ".txt", ".md", ".st", ".log", ".json" };
            const auto extension = lower(relative.extension().string());
            bool allowed = false;
            for (const auto* e : kAllowed) if (extension == e) allowed = true;
            if (!allowed) {
                why = "'" + wanted + "' : seuls .csv, .txt, .md, .st, .log et .json s'ecrivent "
                    "depuis une macro";
                return {};
            }
            const auto& workbook = xls::MacroXls::instance().loadedWorkbook();
            if (workbook.empty()) {
                why = "aucun classeur ouvert, donc pas de dossier ou ecrire : le fichier va "
                    "a cote du classeur";
                return {};
            }
            return (fs::path(workbook).parent_path() / relative).lexically_normal().string();
        }

        domain::VariableScope scopeFromText(std::string_view text) {
            const auto k = lower(text);
            if (k == "input")  return domain::VariableScope::Input;
            if (k == "output") return domain::VariableScope::Output;
            if (k == "inout")  return domain::VariableScope::InOut;
            if (k == "public") return domain::VariableScope::Public;
            if (k == "private" || k == "local") return domain::VariableScope::Local;
            if (k == "member") return domain::VariableScope::DerivedMember;
            return domain::VariableScope::Global;
        }

        domain::PouLanguage languageFromText(std::string_view text) {
            const auto k = lower(text);
            if (k == "il")  return domain::PouLanguage::IL;
            if (k == "ld")  return domain::PouLanguage::LD;
            if (k == "fbd") return domain::PouLanguage::FBD;
            if (k == "sfc") return domain::PouLanguage::SFC;
            return domain::PouLanguage::ST;
        }

    } // namespace

    class MacroRunner::Environment final : public sim::Environment {
    public:
        Environment(ProjectPtr project, SharedLibrary* library)
            : project_(std::move(project)), library_(library) {}

        // ---- variables of the macro itself ---------------------------------
        // A macro's variables are its own, not the project's. They appear on first
        // assignment, which is what a script wants and what a PLC section must never
        // have - hence a separate environment rather than a flag on the simulator's.
        bool read(std::string_view name, Value& out) override {
            const auto at = locals_.find(std::string(name));
            if (at == locals_.end()) return false;
            out = at->second;
            return true;
        }

        bool write(std::string_view name, const Value& v) override {
            locals_[std::string(name)] = v;
            return true;
        }

        bool exists(std::string_view name) override {
            return locals_.count(std::string(name)) != 0;
        }

        void report(sim::Diagnostic d) override { report_.diagnostics.push_back(std::move(d)); }

        bool call(std::string_view name, std::string_view instance,
            const std::vector<std::pair<std::string, Value>>& arguments,
            Value& result) override;

        // ---- the run --------------------------------------------------------
        MacroReport& report() { return report_; }
        MacroMode     mode{ MacroMode::Preview };
        std::unique_ptr<CompoundCommand> commands;
        std::map<std::string, std::string> answers;
        std::map<std::string, Table>       tables;
        std::vector<std::string>           tableOrder;
        bool                               failed{ false };
        // Set by AskNow: the round is over, but this is not a failure.
        bool                               stopped{ false };

        void reset(std::string label) {
            locals_.clear();
            report_ = MacroReport{};
            commands = std::make_unique<CompoundCommand>(std::move(label));
            failed = false;
            stopped = false;
            files_.clear();
            fileOrder_.clear();
            depth_ = 0;
        }

        // Les fichiers que la macro a ecrits, a la fin du tour. Voir FileWrite.
        void settleFiles(bool keep);

    private:
        // A native that changes something. The command is built AND RUN, in both
        // modes, because the next statement may ask about what it just created.
        // Preview undoes everything at the end of the run.
        //
        // A refusal stops the macro. Carrying on after one would build the rest on
        // an assumption that has just been shown false.
        bool stage(core::CommandPtr command) {
            if (!command) return false;
            const auto label = command->label();
            if (auto status = command->execute(); !status) {
                report_.warnings.push_back(label + " : " + status.error().message());
                report_.failure = label + " : " + status.error().message();
                report_.ok = false;
                failed = true;
                return false;
            }
            report_.actions.push_back(label);
            commands->addApplied(std::move(command));
            return true;
        }

        std::string ask(MacroQuestion question);

        [[nodiscard]] static std::string text(const std::vector<std::pair<std::string, Value>>& a,
            std::size_t index) {
            if (index >= a.size()) return {};
            const auto& v = a[index].second;
            return v.type() == sim::Type::String ? v.asString() : v.display();
        }
        [[nodiscard]] static double number(const std::vector<std::pair<std::string, Value>>& a,
            std::size_t index) {
            if (index >= a.size()) return 0.0;
            return a[index].second.asReal();
        }

        // HasMember : 1 le champ existe, 0 le type existe sans ce champ, -1 le
        // type n'est pas la. Trois reponses, parce que "pas dans le projet" doit
        // renvoyer a la bibliotheque et "pas ce champ" non.
        // `found` recoit le type du champ tel qu'il est ecrit : MemberType le rend.
        [[nodiscard]] int memberInProject(const std::string& type, const std::string& member,
            std::string& found) const;
        [[nodiscard]] int memberInLibrary(const std::string& type, const std::string& member,
            std::string& found) const;

        bool runMacro(const std::string& wanted, Value& result);

        ProjectPtr                    project_;
        SharedLibrary* library_{ nullptr };
        std::map<std::string, Value>  locals_;
        MacroReport                   report_;

        // Un fichier ecrit par la macro n'est PAS ecrit pendant qu'elle tourne.
        // Il est tenu ici et pose sur le disque a la fin, si le tour est garde
        // - sinon un Fail() a la ligne 300 laisserait un CSV a moitie ecrit a
        // cote du classeur, que personne ne saurait perime. Ctrl+Z, lui, ne le
        // reprendra pas : c'est dit dans le compte rendu.
        struct StagedFile {
            std::string content;
            std::size_t lines{ 0 };
        };
        std::map<std::string, StagedFile> files_;
        std::vector<std::string>          fileOrder_;

        // RunMacro dans RunMacro dans RunMacro... La profondeur est bornee : une
        // macro qui s'appelle elle-meme ne doit pas finir en pile debordee.
        int depth_{ 0 };
        static constexpr int kMaxDepth = 4;
    };

    // ---------------------------------------------------------------------------
    //  A question. In a first round it returns the preset and records itself; once
    //  answered it returns the answer and asks nothing.
    // ---------------------------------------------------------------------------
    std::string MacroRunner::Environment::ask(MacroQuestion question) {
        // Lot macros 1 : chaque question atteinte est notee, une fois par cle,
        // meme quand elle a deja sa reponse - c'est ce que le formulaire montre.
        if (std::none_of(report_.reached.begin(), report_.reached.end(),
                [&](const MacroQuestion& q) { return q.key == question.key; }))
            report_.reached.push_back(question);

        const auto known = answers.find(question.key);
        if (known != answers.end()) return known->second;

        // Recorded once per key. A question inside a loop would otherwise be asked
        // as many times as the loop runs.
        for (const auto& already : report_.questions)
            if (already.key == question.key) return question.preset;

        report_.questions.push_back(std::move(question));
        report_.needsAnswers = true;
        return report_.questions.back().preset;
    }

    // ---------------------------------------------------------------------------
    //  Un champ dans un type du PROJET. Un DDT porte ses champs ; un DFB porte
    //  ses parametres et ses variables publiques. Ses variables privees ne
    //  comptent pas : "Inst.Compteur" ne compile pas si Compteur est prive, et
    //  c'est exactement la question que pose la macro.
    // ---------------------------------------------------------------------------
    int MacroRunner::Environment::memberInProject(const std::string& type,
        const std::string& member, std::string& found) const {
        const auto wantedType = lower(type);
        const auto& names = project_->strings;
        for (const auto& d : project_->derivedTypes) {
            if (lower(names.text(d.name)) != wantedType) continue;
            for (const auto index : d.fields)
                if (index < project_->variables.size()
                    && lower(names.text(project_->variables[index].name)) == member) {
                    found = std::string(names.text(project_->variables[index].type.name));
                    return 1;
                }
            return 0;
        }
        for (const auto& pou : project_->pous) {
            if (pou.kind != domain::PouKind::FunctionBlockType
                || lower(names.text(pou.name)) != wantedType)
                continue;
            for (const auto* list : { &pou.parameters, &pou.locals })
                for (const auto index : *list) {
                    if (index >= project_->variables.size()) continue;
                    const auto& v = project_->variables[index];
                    if (v.scope == domain::VariableScope::Local) continue;
                    if (lower(names.text(v.name)) == member) {
                        found = std::string(names.text(v.type.name));
                        return 1;
                    }
                }
            return 0;
        }
        return -1;
    }

    // ---------------------------------------------------------------------------
    //  Le meme, dans la BIBLIOTHEQUE : le fichier est relu et ses declarations
    //  comptees par le meme lecteur que l'aide. Un second lecteur ici serait une
    //  seconde occasion de ne pas compter la meme chose.
    // ---------------------------------------------------------------------------
    int MacroRunner::Environment::memberInLibrary(const std::string& type,
        const std::string& member, std::string& found) const {
        if (!library_) return -1;
        const auto wantedType = lower(type);
        const LibraryItem* item = nullptr;
        for (const auto& candidate : library_->items())
            if (lower(candidate.name) == wantedType
                && candidate.kind != LibraryItemKind::Macro)
                item = &candidate;
        if (!item) return -1;
        const auto contents = readWhole(item->path);
        if (contents.empty()) return -1;
        const auto entry = parseLibraryFile(contents, item->fileName());
        for (const auto& d : entry.declarations)
            if (!d.isLocal() && lower(d.name) == member) {
                found = d.type;
                return 1;
            }
        return 0;
    }

    // ---------------------------------------------------------------------------
    //  RunMacro : une macro de la bibliotheque, lancee depuis celle-ci.
    //
    //  CE QUI EST PARTAGE : le projet, les reponses, les tableaux, le classeur,
    //  le compte rendu et la commande composee. Une seule annulation reprend donc
    //  l'appelante ET les appelees, et une question posee par l'appelee se pose
    //  dans le meme dialogue que celles de l'appelante - avec la meme cle, elle
    //  n'est posee qu'une fois.
    //
    //  CE QUI NE L'EST PAS : les variables. Chaque macro a les siennes. Une
    //  appelee qui ecrirait `n` ou `i` dans la memoire de l'appelante la ferait
    //  boucler sur une valeur qu'elle n'a pas posee.
    //
    //  UN ECHEC DE L'APPELEE EST UN ECHEC DE L'APPELANTE. Continuer apres une
    //  etape ratee, c'est ecrire le cablage d'un tableau qui n'a pas ete declare.
    // ---------------------------------------------------------------------------
    bool MacroRunner::Environment::runMacro(const std::string& wanted, Value& result) {
        result = Value::boolean(false);
        if (depth_ >= kMaxDepth) {
            report_.failure = "RunMacro " + wanted + " : plus de "
                + std::to_string(kMaxDepth) + " macros imbriquees - une macro qui "
                "s'appelle elle-meme ?";
            report_.ok = false;
            failed = true;
            return false;
        }
        if (!library_) {
            report_.failure = "RunMacro " + wanted + " : aucune bibliotheque partagee n'est ouverte";
            report_.ok = false;
            failed = true;
            return false;
        }
        const LibraryItem* item = nullptr;
        for (const auto& candidate : library_->items())
            if (candidate.kind == LibraryItemKind::Macro && lower(candidate.name) == lower(wanted))
                item = &candidate;
        const auto source = item ? readWhole(item->path) : std::string{};
        if (source.empty()) {
            report_.failure = "RunMacro : la macro '" + wanted + "' n'est pas dans la "
                "bibliotheque (libs/Macros, et une ligne mac dans index.txt)";
            report_.ok = false;
            failed = true;
            return false;
        }
        auto program = sim::parse(source, wanted);
        if (!program) {
            report_.failure = "RunMacro " + wanted + " : " + program.error().message();
            report_.ok = false;
            failed = true;
            return false;
        }

        auto saved = std::move(locals_);
        locals_.clear();
        ++depth_;
        report_.log.push_back("--- " + wanted + " ---");
        const auto outcome = sim::execute(**program, *this);
        --depth_;
        locals_ = std::move(saved);

        if (failed) {
            // Le motif est celui de l'appelee ; on dit juste d'ou il vient.
            if (report_.failure.rfind(wanted, 0) != 0)
                report_.failure = wanted + " : " + report_.failure;
            return false;
        }
        if (!outcome.completed) {
            std::string why;
            for (const auto& d : report_.diagnostics) {
                if (d.severity != sim::Diagnostic::Severity::Error) continue;
                if (!why.empty()) why += "\n";
                why += d.line > 0 ? "ligne " + std::to_string(d.line) + " : " + d.message
                    : d.message;
            }
            report_.failure = wanted + " s'est arretee"
                + (why.empty() ? std::string(" sans dire pourquoi") : " : " + why);
            report_.ok = false;
            failed = true;
            return false;
        }
        // AskNow dans l'appelee : le tour s'arrete la, pour toutes les deux.
        // Rendre FAUX sans arreter le flot, comme AskNow lui-meme.
        result = Value::boolean(!stopped);
        return true;
    }

    // ---------------------------------------------------------------------------
    //  Les fichiers, a la fin du tour.
    //
    //    garde  : ecrits, et le compte rendu le dit - avec "pas annulable",
    //             parce que Ctrl+Z reprend le projet et pas le disque ;
    //    apercu : annonces, avec leur nombre de lignes ;
    //    sinon  : oublies. Un tour qui a echoue n'ecrit rien.
    // ---------------------------------------------------------------------------
    void MacroRunner::Environment::settleFiles(bool keep) {
        for (const auto& path : fileOrder_) {
            const auto& staged = files_[path];
            const auto lines = std::to_string(staged.lines) + " ligne(s)";
            if (mode == MacroMode::Preview) {
                report_.actions.push_back("ecrire le fichier " + path + " (" + lines
                    + ") - a l'application, et Ctrl+Z ne le reprendra pas");
                continue;
            }
            if (!keep) continue;
            std::error_code ec;
            const auto folder = std::filesystem::path(path).parent_path();
            if (!folder.empty()) std::filesystem::create_directories(folder, ec);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (out) out << staged.content;
            if (!out) {
                report_.warnings.push_back("fichier " + path + " : ecriture impossible "
                    "(dossier en lecture seule, ou fichier ouvert dans un autre programme ?)");
                continue;
            }
            report_.log.push_back("fichier ecrit : " + path + " (" + lines
                + "). Ctrl+Z ne le reprend pas.");
        }
        files_.clear();
        fileOrder_.clear();
    }

    // ---------------------------------------------------------------------------
    //  The natives. Grouped by category, and that grouping is the documentation:
    //  what a macro may do IS this list.
    // ---------------------------------------------------------------------------
    bool MacroRunner::Environment::call(std::string_view rawName, std::string_view instance,
        const std::vector<std::pair<std::string, Value>>& args,
        Value& result) {
        (void)instance;
        const auto name = lower(rawName);
        result = Value::boolean(true);

        // Once Fail() has been called nothing else runs. A macro that reported a
        // fatal problem and then carried on editing would be worse than one that
        // stopped.
        if ((failed || stopped) && name != "log" && name != "warn") {
            result = Value::boolean(false);
            return true;
        }

        // ---- ce que la macro a fait, en chiffres ------------------------------
        if (name == "asknow") {
            // "Pose ce que j'ai demande jusqu'ici, puis relance-moi."
            //
            // Sans ca, un tour pose TOUTES les questions atteignables avec les
            // valeurs par defaut - y compris celles de la branche que la reponse
            // suivante va ecarter. Le lecteur voyait quatre noms de section alors
            // qu'il venait de choisir "une seule", puis se les voyait redemander.
            // C'est le macro-ecrivain qui sait ou couper, pas le moteur.
            if (report_.needsAnswers) {
                stopped = true;
                result = Value::boolean(false);
                return true;
            }
            result = Value::boolean(true);
            return true;
        }

        // ---- rendre compte --------------------------------------------------
        if (name == "log") { report_.log.push_back(text(args, 0)); return true; }
        if (name == "warn") { report_.warnings.push_back(text(args, 0)); return true; }
        if (name == "fail") {
            report_.failure = text(args, 0);
            report_.ok = false;
            failed = true;
            // ARRETE VRAIMENT. Le gate plus haut neutralisait les natives suivantes
            // mais laissait le flot continuer, si bien qu'un Warn place a la fin
            // s'executait quand meme - et annoncait un second probleme qui
            // contredisait le premier. "Fail arrete la macro" doit etre vrai.
            return false;
        }

        // ---- conversions ------------------------------------------------------
        //
        // Spelled the way IEC 61131-3 spells them, because that is what the reader
        // will type without thinking. A macro that builds a line of ST spends most
        // of its time turning numbers into text.
        if (name == "int_to_string" || name == "dint_to_string" || name == "udint_to_string"
            || name == "uint_to_string" || name == "bool_to_string") {
            const auto v = args.empty() ? Value() : args[0].second;
            if (name == "bool_to_string") {
                result = Value::text(v.isTruthy() ? "TRUE" : "FALSE");
            }
            else {
                result = Value::text(std::to_string(v.asInteger()));
            }
            return true;
        }
        if (name == "real_to_string") {
            // Trailing zeros trimmed, and a decimal point kept: a REAL written as
            // "16" is an INT literal in Control Expert, and the paste would not
            // compile. Same rule as the workbook's.
            char buffer[64];
            std::snprintf(buffer, sizeof buffer, "%.6f", number(args, 0));
            std::string out(buffer);
            while (out.size() > 1 && out.back() == '0') out.pop_back();
            if (!out.empty() && out.back() == '.') out.push_back('0');
            result = Value::text(out);
            return true;
        }
        if (name == "real_to_int" || name == "real_to_dint" || name == "trunc") {
            result = Value::integer(sim::Type::DInt,
                static_cast<std::int64_t>(number(args, 0)));
            return true;
        }
        if (name == "string_to_int" || name == "string_to_dint") {
            result = Value::integer(sim::Type::DInt,
                static_cast<std::int64_t>(std::atoll(text(args, 0).c_str())));
            return true;
        }
        if (name == "string_to_real") {
            result = Value::real(std::atof(text(args, 0).c_str()));
            return true;
        }
        if (name == "mid" || name == "substr") {
            const auto whole = text(args, 0);
            const auto from = static_cast<std::size_t>(std::max(0.0, number(args, 1)));
            const auto count = args.size() > 2 ? static_cast<std::size_t>(number(args, 2))
                : std::string::npos;
            result = Value::text(from >= whole.size() ? std::string{} : whole.substr(from, count));
            return true;
        }
        if (name == "chr") {
            // Un caractere par son code. Il existe pour une seule raison : ecrire
            // une apostrophe dans une chaine generee. CHR(39) se lit ; doubler une
            // apostrophe dans un litteral lui-meme delimite par des apostrophes, non.
            const auto code = static_cast<int>(number(args, 0));
            result = Value::text(code > 0 && code < 256
                ? std::string(1, static_cast<char>(code))
                : std::string{});
            return true;
        }
        if (name == "find") {
            // Where a needle starts, or -1. Without it a macro cannot take an
            // address apart, and %IW0.10.3 is exactly the kind of thing a macro
            // needs to take apart.
            const auto whole = text(args, 0), needle = text(args, 1);
            const auto from = args.size() > 2 ? static_cast<std::size_t>(number(args, 2)) : 0u;
            const auto at = from >= whole.size() ? std::string::npos : whole.find(needle, from);
            // Le test explicite est redondant - npos convertie en int64 vaut deja -1,
            // parce que c'est (size_t)-1. Il reste parce que s'appuyer sur cette
            // identite est le genre d'astuce qui casse en silence le jour ou le type
            // change, et parce qu'un lecteur ne devrait pas avoir a la connaitre.
            result = Value::integer(sim::Type::DInt,
                at == std::string::npos ? -1 : static_cast<std::int64_t>(at));
            return true;
        }
        if (name == "startswith") {
            const auto whole = text(args, 0), prefix = text(args, 1);
            result = Value::boolean(whole.size() >= prefix.size()
                && whole.compare(0, prefix.size(), prefix) == 0);
            return true;
        }
        if (name == "len") {
            result = Value::integer(sim::Type::DInt,
                static_cast<std::int64_t>(text(args, 0).size()));
            return true;
        }
        if (name == "quote") {
            result = Value::text(stQuote(text(args, 0)));
            return true;
        }
        if (name == "ascii") {
            result = Value::text(toAscii(text(args, 0)));
            return true;
        }
        if (name == "upper" || name == "lower") {
            // Une famille s'appelle ST_EQ_Pump, son bloc DFB_EQ_PUMP. Sans UPPER,
            // la correspondance s'ecrivait famille par famille dans un ELSIF de
            // dix branches, et une famille ajoutee a libs/ n'etait appelee par
            // aucune macro tant que personne ne pensait a allonger la liste.
            result = Value::text(name == "upper" ? upper(text(args, 0)) : lower(text(args, 0)));
            return true;
        }

        // ---- interroger ------------------------------------------------------
        if (name == "typeexists") {
            const auto wanted = text(args, 0);
            bool found = false;
            for (const auto& d : project_->derivedTypes)
                if (project_->strings.text(d.name) == wanted) found = true;
            result = Value::boolean(found);
            return true;
        }
        if (name == "pouexists" || name == "blockexists") {
            const auto wanted = text(args, 0);
            bool found = false;
            for (const auto& p : project_->pous)
                if (project_->strings.text(p.name) == wanted) found = true;
            result = Value::boolean(found);
            return true;
        }
        if (name == "variableexists") {
            // VariableExists(nom) : une variable GLOBALE de ce nom. Les
            // parametres et les locales des DFB n'en sont pas, ni les champs des
            // DDT : DFB_COM_MODBUS a une entree SimActive, et la globale
            // SimActive n'etait jamais creee - "elle existe deja".
            // VariableExists(nom, proprietaire) : une variable de ce DFB ou de
            // cette unite, ou un champ de ce DDT.
            const auto wanted = text(args, 0);
            const auto owner = args.size() > 1 ? text(args, 1) : std::string{};
            bool found = false;
            if (owner.empty()) {
                for (const auto& v : project_->variables)
                    if ((v.scope == domain::VariableScope::Global
                         || v.scope == domain::VariableScope::Constant)
                        && v.owner == domain::kNoIndex
                        && project_->strings.text(v.name) == wanted)
                        found = true;
            } else {
                // Un DDT : ses champs sont ses `fields` - l'import de la
                // bibliotheque ne pose pas `owner` sur un champ.
                for (const auto& d : project_->derivedTypes)
                    if (project_->strings.text(d.name) == owner)
                        for (const auto f : d.fields)
                            if (f < project_->variables.size()
                                && project_->strings.text(project_->variables[f].name) == wanted)
                                found = true;
                // Un DFB ou une unite de programme : `owner` est pose.
                for (const auto& v : project_->variables)
                    if (v.owner < project_->pous.size()
                        && v.scope != domain::VariableScope::DerivedMember
                        && project_->strings.text(project_->pous[v.owner].name) == owner
                        && project_->strings.text(v.name) == wanted)
                        found = true;
            }
            result = Value::boolean(found);
            return true;
        }
        if (name == "sectionexists") {
            const auto wanted = text(args, 0);
            bool found = false;
            for (const auto& s : project_->sections)
                if (project_->strings.text(s.name) == wanted) found = true;
            result = Value::boolean(found);
            return true;
        }
        if (name == "sectionlines") {
            const auto wanted = text(args, 0);
            std::int64_t lines = 0;
            for (const auto& s : project_->sections)
                if (project_->strings.text(s.name) == wanted) lines = s.lineCount;
            result = Value::integer(sim::Type::DInt, lines);
            return true;
        }
        if (name == "variabletype") {
            // Le type d'une variable, tel qu'il est ecrit, ou ''. VariableExists
            // ne suffisait pas : une variable du bon nom et du mauvais type passait
            // pour "deja la", et la macro cablait un ARRAY[0..7] comme un [0..15].
            const auto wanted = text(args, 0);
            std::string type;
            for (const auto& v : project_->variables)
                if (v.scope == domain::VariableScope::Global
                    && project_->strings.text(v.name) == wanted)
                    type = std::string(project_->strings.text(v.type.name));
            result = Value::text(type);
            return true;
        }
        if (name == "hasmember" || name == "membertype") {
            // HasMember(type, champ) : ce DDT ou ce DFB a-t-il ce champ ?
            // MemberType(type, champ) : son type tel qu'il est ecrit, ou ''.
            //
            // Le projet d'abord, puis la bibliotheque. Le projet d'abord parce
            // que c'est SA copie qui compilera : un DDT importe en 1.00 n'a pas
            // le champ que la 1.01 a ajoute, et c'est justement ce qu'une macro
            // doit savoir avant d'ecrire une ligne qui le cite.
            //
            // Un seul niveau : 'Thr' oui, 'Thr[0].Val' non. Le type d'un tableau
            // se lit tel quel : 'ARRAY[0..3] OF ST_IO_Thr'.
            const auto member = lower(text(args, 1));
            std::string type;
            int found = memberInProject(text(args, 0), member, type);
            if (found < 0) found = memberInLibrary(text(args, 0), member, type);
            if (name == "membertype") result = Value::text(found == 1 ? type : std::string{});
            else result = Value::boolean(found == 1);
            return true;
        }

        // ---- creer ------------------------------------------------------------
        if (name == "adddertype" || name == "addderivedtype") {
            const bool done = stage(std::make_unique<AddDerivedTypeCommand>(
                project_, text(args, 0), args.size() > 1 ? text(args, 1) : "0.01"));
            if (done) ++report_.tally.types;
            result = Value::boolean(done);
            return true;
        }
        if (name == "addfunctionblock") {
            result = Value::boolean(stage(std::make_unique<AddFunctionBlockCommand>(
                project_, text(args, 0), args.size() > 1 ? text(args, 1) : "1.00")));
            return true;
        }
        if (name == "addprogramunit") {
            result = Value::boolean(stage(std::make_unique<AddProgramUnitCommand>(
                project_, text(args, 0), args.size() > 1 ? text(args, 1) : "MAST")));
            return true;
        }
        if (name == "addsubroutine") {
            // Une SOUS-ROUTINE, pas une section. Les deux vivent au meme endroit
            // dans le modele, et c'est isSubroutine qui les separe: une section
            // tourne a chaque cycle, une SR quand on l'appelle. Confondre les deux
            // n'est pas une difference de presentation, c'est un changement de
            // programme.
            const bool done = stage(std::make_unique<AddSectionCommand>(
                project_, text(args, 0), args.size() > 1 ? text(args, 1) : "MAST",
                domain::PouLanguage::ST, domain::kNoIndex, /*subroutine=*/true));
            if (done) ++report_.tally.sections;
            result = Value::boolean(done);
            return true;
        }
        if (name == "issubroutine") {
            const auto wanted = text(args, 0);
            bool found = false;
            // isSubroutine, OU l'unite qui la porte est une SR : AddSubroutine
            // (AddSectionCommand, subroutine a true) cree la SR sous une unite
            // de genre SubRoutine sans poser isSubroutine - relancee, une macro
            // prenait sa propre SR pour une section.
            for (const auto& s : project_->sections)
                if (project_->strings.text(s.name) == wanted
                    && (s.isSubroutine
                        || (s.owner < project_->pous.size()
                            && project_->pous[s.owner].kind == domain::PouKind::SubRoutine)))
                    found = true;
            result = Value::boolean(found);
            return true;
        }
        if (name == "addsection") {
            // AddSection(name, task, language, owner). The owner is empty for a task
            // section and a POU name otherwise - which is exactly the pair the
            // dialog offers, so a macro is refused wherever the dialog refuses.
            const auto owner = args.size() > 3 ? text(args, 3) : std::string{};
            domain::Index ownerIndex = domain::kNoIndex;
            if (!owner.empty()) {
                for (domain::Index i = 0; i < project_->pous.size(); ++i)
                    if (project_->strings.text(project_->pous[i].name) == owner) ownerIndex = i;
                if (ownerIndex == domain::kNoIndex) {
                    report_.warnings.push_back("AddSection : '" + owner + "' n'existe pas");
                    result = Value::boolean(false);
                    return true;
                }
            }
            const bool done = stage(std::make_unique<AddSectionCommand>(
                project_, text(args, 0), args.size() > 1 ? text(args, 1) : "MAST",
                languageFromText(args.size() > 2 ? text(args, 2) : "ST"), ownerIndex));
            if (done) ++report_.tally.sections;
            result = Value::boolean(done);
            return true;
        }
        if (name == "addvariable") {
            // AddVariable(name, type, scope, owner, initial, commentaire, adresse)
            //
            // Les trois derniers sont facultatifs. La commande savait deja les
            // poser ; la macro ne pouvait pas les lui passer, si bien qu'une
            // variable qui DOIT naitre a -1 (le mode courant de GenererModes)
            // naissait a 0, et qu'un mot de report sur %MW naissait non localise.
            AddVariableCommand::Spec spec;
            spec.name = text(args, 0);
            spec.type = text(args, 1);
            spec.scope = scopeFromText(args.size() > 2 ? text(args, 2) : "Global");
            if (args.size() > 4) spec.initValue = text(args, 4);
            if (args.size() > 5) spec.comment = text(args, 5);
            if (args.size() > 6) spec.address = text(args, 6);
            const auto owner = args.size() > 3 ? text(args, 3) : std::string{};
            if (!owner.empty()) {
                if (spec.scope == domain::VariableScope::DerivedMember) {
                    for (domain::Index i = 0; i < project_->derivedTypes.size(); ++i)
                        if (project_->strings.text(project_->derivedTypes[i].name) == owner)
                            spec.owner = i;
                }
                else {
                    for (domain::Index i = 0; i < project_->pous.size(); ++i)
                        if (project_->strings.text(project_->pous[i].name) == owner) spec.owner = i;
                }
                if (spec.owner == domain::kNoIndex) {
                    report_.warnings.push_back("AddVariable : '" + owner + "' n'existe pas");
                    result = Value::boolean(false);
                    return true;
                }
            }
            const bool done = stage(std::make_unique<AddVariableCommand>(project_, spec));
            if (done) {
                ++report_.tally.variables;
                // An instance is a variable whose type is a block. Counting them
                // apart is the difference between "42 variables" and "the two
                // instances the card needs are there".
                for (const auto& pou : project_->pous)
                    if (pou.kind == domain::PouKind::FunctionBlockType
                        && project_->strings.text(pou.name) == spec.type)
                        ++report_.tally.instances;
            }
            result = Value::boolean(done);
            return true;
        }
        if (name == "clearsection") {
            // Vider une section avant de la reecrire. Une section generee qui serait
            // a moitie ancienne et a moitie nouvelle est pire que les deux.
            const auto wanted = text(args, 0);
            domain::Index at = domain::kNoIndex;
            for (domain::Index i = 0; i < project_->sections.size(); ++i)
                if (project_->strings.text(project_->sections[i].name) == wanted) at = i;
            if (at == domain::kNoIndex) {
                report_.warnings.push_back("ClearSection : la section '" + wanted
                    + "' n'existe pas");
                result = Value::boolean(false);
                return true;
            }
            result = Value::boolean(stage(std::make_unique<ClearSectionCommand>(project_, at)));
            return true;
        }
        if (name == "sectiontext") {
            // SectionText(section) : le texte, '' si la section n'existe pas. De
            // quoi retrouver ce qu'une macro precedente y a ecrit - le Count d'un
            // appel, une ligne deja la - sans la reecrire en entier.
            const auto wanted = text(args, 0);
            std::string body;
            for (const auto& s : project_->sections)
                if (project_->strings.text(s.name) == wanted) body = s.body;
            result = Value::text(body);
            return true;
        }
        if (name == "replaceinsection") {
            // ReplaceInSection(section, ancien, nouveau) : chaque occurrence exacte
            // d'ancien devient nouveau ; rend combien. UNE commande, donc une
            // seule annulation - et rien n'est touche s'il n'y en a aucune.
            const auto wanted = text(args, 0);
            const auto from = text(args, 1);
            const auto to = text(args, 2);
            domain::Index at = domain::kNoIndex;
            for (domain::Index i = 0; i < project_->sections.size(); ++i)
                if (project_->strings.text(project_->sections[i].name) == wanted) at = i;
            if (at == domain::kNoIndex || from.empty()) {
                report_.warnings.push_back(at == domain::kNoIndex
                    ? "ReplaceInSection : la section '" + wanted + "' n'existe pas"
                    : std::string("ReplaceInSection : le texte a remplacer est vide"));
                result = Value::integer(sim::Type::DInt, 0);
                return true;
            }
            std::string body = project_->sections[at].body;
            std::int64_t count = 0;
            for (auto pos = body.find(from); pos != std::string::npos; pos = body.find(from, pos + to.size())) {
                body.replace(pos, from.size(), to);
                ++count;
            }
            if (count > 0 && !stage(std::make_unique<SetSectionBodyCommand>(project_, at, std::move(body))))
                count = 0;
            result = Value::integer(sim::Type::DInt, count);
            return true;
        }
        if (name == "appendtosection") {
            const auto wanted = text(args, 0);
            domain::Index at = domain::kNoIndex;
            for (domain::Index i = 0; i < project_->sections.size(); ++i)
                if (project_->strings.text(project_->sections[i].name) == wanted) at = i;
            if (at == domain::kNoIndex) {
                report_.warnings.push_back("AppendToSection : la section '" + wanted
                    + "' n'existe pas");
                result = Value::boolean(false);
                return true;
            }
            const bool done = stage(std::make_unique<AppendToSectionCommand>(
                project_, at, text(args, 1)));
            if (done) ++report_.tally.lines;
            result = Value::boolean(done);
            return true;
        }
        if (name == "placesection") {
            // PlaceSection(section, avant) : la section passe juste AVANT `avant`
            // dans l'ordre d'execution de sa tache ; `avant` vide la met en
            // dernier.
            //
            // C'ETAIT LA MOITIE DU SUJET, ET LA SEULE QU'UNE MACRO NE POUVAIT PAS
            // FAIRE. Les imports ecrivaient "posez cet ordre a la main" dans leur
            // compte rendu, et une TC lue apres les equipements fait travailler
            // tout le programme sur la commande du cycle precedent. La commande
            // existe depuis l'ordre d'execution ; il manquait de la tendre aux
            // macros.
            //
            // DEJA A SA PLACE N'EST PAS UN ECHEC : relancee, une macro replace
            // des sections qui n'ont pas bouge, et ca ne doit ni l'arreter ni
            // empiler des etapes d'annulation vides.
            const auto wanted = text(args, 0);
            const auto target = args.size() > 1 ? text(args, 1) : std::string{};
            domain::Index at = domain::kNoIndex, before = domain::kNoIndex;
            for (domain::Index i = 0; i < project_->sections.size(); ++i) {
                const auto n = project_->strings.text(project_->sections[i].name);
                if (n == wanted) at = i;
                if (!target.empty() && n == target) before = i;
            }
            if (at == domain::kNoIndex || (!target.empty() && before == domain::kNoIndex)) {
                report_.warnings.push_back("PlaceSection : la section '"
                    + (at == domain::kNoIndex ? wanted : target) + "' n'existe pas");
                result = Value::boolean(false);
                return true;
            }
            // Deja juste avant sa cible (ou deja derniere) : rien a faire. Le
            // deplacement, lui, le ferait quand meme et empilerait un Ctrl+Z
            // qui ne defait rien.
            if (const auto task = domain::taskOf(*project_, at); task != 0) {
                const auto steps = domain::executionOrder(*project_, task);
                std::size_t mine = steps.size(), theirs = steps.size();
                for (std::size_t k = 0; k < steps.size(); ++k) {
                    if (steps[k].section == at) mine = k;
                    if (steps[k].section == before) theirs = k;
                }
                const bool inPlace = mine < steps.size()
                    && (before == domain::kNoIndex ? mine + 1 == steps.size() : mine + 1 == theirs);
                if (inPlace) {
                    result = Value::boolean(true);
                    return true;
                }
            }
            auto command = std::make_unique<ReorderSectionCommand>(
                project_, at, ReorderSectionCommand::Before{ before });
            const auto label = command->label();
            const auto status = command->execute();
            if (!status) {
                const bool alreadyThere = status.error().code == core::ErrorCode::Cancelled;
                if (!alreadyThere)
                    report_.warnings.push_back("PlaceSection " + wanted + " : "
                        + status.error().message());
                result = Value::boolean(alreadyThere);
                return true;
            }
            report_.actions.push_back(label);
            commands->addApplied(std::move(command));
            result = Value::boolean(true);
            return true;
        }
        if (name == "sectionrank") {
            // Le rang d'une section dans l'ordre d'execution de sa tache, a
            // partir de 0 ; -1 si aucune tache ne l'execute (SR, corps de DFB)
            // ou si elle n'existe pas.
            const auto wanted = text(args, 0);
            std::int64_t rank = -1;
            for (domain::Index i = 0; i < project_->sections.size(); ++i) {
                if (project_->strings.text(project_->sections[i].name) != wanted) continue;
                const auto task = domain::taskOf(*project_, i);
                if (task == 0) break;
                const auto steps = domain::executionOrder(*project_, task);
                for (std::size_t k = 0; k < steps.size(); ++k)
                    if (steps[k].section == i) rank = static_cast<std::int64_t>(k);
                break;
            }
            result = Value::integer(sim::Type::DInt, rank);
            return true;
        }

        // ---- bibliotheque -----------------------------------------------------
        if (name == "runmacro") return runMacro(text(args, 0), result);
        if (name == "libhasitem" || name == "libversion" || name == "libimport"
            || name == "libisoutdated" || name == "libupdate" || name == "liboutdated") {
            if (!library_) {
                report_.warnings.push_back(std::string(rawName)
                    + " : aucune bibliotheque partagee n'est ouverte");
                result = Value::boolean(false);
                if (name == "libversion" || name == "liboutdated") result = Value::text("");
                return true;
            }
            if (name == "liboutdated") {
                // Tout ce que le projet a en retard, 'ST_EQ_Pump,DFB_EQ_PUMP'. Sans
                // cette liste, VerifierBibliotheque ne controlait que les trois
                // noms ecrits en dur dans son corps, et une famille ajoutee a libs/
                // n'etait verifiee par personne.
                std::string list;
                for (const auto& notice : library_->outdated(*project_))
                    list += (list.empty() ? "" : ",") + notice.name;
                result = Value::text(list);
                return true;
            }
            const auto wanted = text(args, 0);
            const LibraryItem* item = nullptr;
            for (const auto& candidate : library_->items())
                if (candidate.name == wanted) item = &candidate;

            if (name == "libhasitem") { result = Value::boolean(item != nullptr); return true; }
            if (name == "libversion") {
                result = Value::text(item ? item->version : std::string{});
                return true;
            }
            if (name == "libisoutdated") {
                bool stale = false;
                for (const auto& notice : library_->outdated(*project_))
                    if (notice.name == wanted) stale = true;
                result = Value::boolean(stale);
                return true;
            }
            if (name == "libupdate") {
                // LibUpdate : la copie du projet remplacee par celle de la
                // bibliotheque, SI ELLE EST PLUS ANCIENNE, et seulement alors.
                // Rend VRAI quand une mise a jour a eu lieu (ou aurait lieu, en
                // apercu), FAUX quand il n'y avait rien a faire.
                //
                // Meme regle que LibImport, et pour la meme raison : l'import
                // ecrit dans le modele sans commande. En apercu il est annonce,
                // pas fait ; applique, Ctrl+Z ne le reprend pas - c'est dit.
                std::string from, to;
                bool stale = false;
                for (const auto& notice : library_->outdated(*project_))
                    if (notice.name == wanted) {
                        stale = true;
                        from = notice.projectVersion;
                        to = notice.libraryVersion;
                    }
                if (!stale) {
                    report_.log.push_back("LibUpdate " + wanted + " : "
                        + (item ? "deja a jour, ou absent du projet" : "absent de la bibliotheque"));
                    result = Value::boolean(false);
                    return true;
                }
                const auto what = wanted + " " + from + " -> " + to;
                if (mode == MacroMode::Preview) {
                    report_.actions.push_back("mise a jour de " + what
                        + " depuis la bibliotheque (a l'application, pas annulable par Ctrl+Z)");
                    result = Value::boolean(true);
                    return true;
                }
                auto outcome = library_->import(*project_, wanted,
                    SharedLibrary::OnConflict::Overwrite);
                if (!outcome) {
                    report_.warnings.push_back("LibUpdate " + wanted + " : "
                        + outcome.error().message());
                    result = Value::boolean(false);
                    return true;
                }
                report_.actions.push_back("mis a jour " + what + " (pas annulable par Ctrl+Z)");
                ++report_.tally.imports;
                result = Value::boolean(true);
                return true;
            }
            // LibImport : in Preview it is NOT performed, and says so, rather than
            // being half-done. Applied, it is a command of the macro (lot macros
            // 1, LibImportCommand above) : one Ctrl+Z takes it back with the rest.
            if (mode == MacroMode::Preview) {
                report_.actions.push_back("import " + wanted + " depuis la bibliotheque");
                result = Value::boolean(item != nullptr);
                return true;
            }
            auto command = std::make_unique<LibImportCommand>(project_, library_, wanted);
            if (auto status = command->execute(); !status) {
                report_.warnings.push_back("LibImport " + wanted + " : " + status.error().message());
                result = Value::boolean(false);
            }
            else {
                report_.actions.push_back("importe " + command->importedAs());
                commands->addApplied(std::move(command));
                ++report_.tally.imports;
                result = Value::boolean(true);
            }
            return true;
        }

        // ---- tableaux ---------------------------------------------------------
        if (name == "opentable" || name == "importtable") {
            const auto wanted = text(args, 0);
            auto at = tables.find(wanted);
            if (at == tables.end() && tableOrder.size() == 1) {
                // ONE TABLE LOADED, ONE ASKED FOR: it is that one.
                //
                // The name was a trap of my own making. A macro says
                // OpenTable('cartes') and the reader, who loaded a file called
                // config_es.csv, has no way to know that 'cartes' was the name they
                // were supposed to give it. Matching the only table there removes
                // the question entirely, and the warning below still catches the
                // real case - several tables and a name that matches none.
                at = tables.find(tableOrder.front());
                report_.log.push_back("OpenTable('" + wanted + "') : un seul tableau est charge ('"
                    + tableOrder.front() + "'), c'est celui-la");
            }
            if (at == tables.end()) {
                std::string loaded;
                for (const auto& name2 : tableOrder) loaded += (loaded.empty() ? "" : ", ") + name2;
                report_.warnings.push_back(
                    "OpenTable : aucun tableau nomme '" + wanted + "'"
                    + (loaded.empty() ? " (aucun n'est charge : utilisez le bouton Table...)"
                        : " - charges : " + loaded));
                result = Value::integer(sim::Type::DInt, -1);
                return true;
            }
            // A handle, not the table. The language has no record type, and adding
            // one would mean changing the evaluator the simulator shares.
            auto position = std::find(tableOrder.begin(), tableOrder.end(), at->first);
            result = Value::integer(sim::Type::DInt,
                static_cast<std::int64_t>(position - tableOrder.begin()));
            return true;
        }
        if (name == "rowcount") {
            const auto handle = static_cast<int>(number(args, 0));
            // Un onglet de classeur : ses identifiants vivent dans une plage a
            // part, ce qui suit n'est pas touche. Voir la section "classeur".
            if (xls::MacroXls::isSheetHandle(handle)) {
                const auto rows = xls::MacroXls::instance().rowCount(handle);
                report_.tally.rowsRead = static_cast<std::size_t>(rows);
                result = Value::integer(sim::Type::DInt, rows);
                return true;
            }
            const auto csv = static_cast<std::size_t>(handle);
            if (csv >= tableOrder.size()) { result = Value::integer(sim::Type::DInt, 0); return true; }
            const auto rows = tables[tableOrder[csv]].rowCount();
            report_.tally.rowsRead = rows;
            result = Value::integer(sim::Type::DInt, static_cast<std::int64_t>(rows));
            return true;
        }
        if (name == "skiprow") {
            ++report_.tally.rowsSkipped;
            report_.warnings.push_back(text(args, 0));
            result = Value::boolean(true);
            return true;
        }
        if (name == "cell") {
            const auto handle = static_cast<int>(number(args, 0));
            const auto column = text(args, 2);
            if (xls::MacroXls::isSheetHandle(handle)) {
                // Le meme avertissement que pour un CSV, et pour la meme raison :
                // une colonne mal orthographiee rend la chaine vide, ce qui est
                // indistinguable d'une cellule vide si personne ne le dit.
                if (!xls::MacroXls::instance().hasColumn(handle, column))
                    report_.warnings.push_back("Cell : aucune colonne '" + column + "'");
                result = Value::text(xls::MacroXls::instance().cell(
                    handle, static_cast<int>(number(args, 1)), column));
                return true;
            }
            const auto csv = static_cast<std::size_t>(handle);
            if (csv >= tableOrder.size()) { result = Value::text(""); return true; }
            const auto& table = tables[tableOrder[csv]];
            const auto row = static_cast<std::size_t>(number(args, 1));
            if (!table.has(column))
                report_.warnings.push_back("Cell : aucune colonne '" + column + "'");
            result = Value::text(table.cell(row, column));
            return true;
        }
        if (name == "hascolumn") {
            const auto handle = static_cast<int>(number(args, 0));
            if (xls::MacroXls::isSheetHandle(handle)) {
                result = Value::boolean(
                    xls::MacroXls::instance().hasColumn(handle, text(args, 1)));
                return true;
            }
            const auto csv = static_cast<std::size_t>(handle);
            result = Value::boolean(csv < tableOrder.size()
                && tables[tableOrder[csv]].has(text(args, 1)));
            return true;
        }

        // ---- classeur ---------------------------------------------------------
        //
        //  Trois natives, et une seule idee : un onglet de classeur se lit comme
        //  un tableau. `OpenSheet` rend un identifiant du MEME GENRE
        //  qu'`OpenTable`, si bien que `RowCount`, `Cell` et `HasColumn`
        //  marchent sans changer de vocabulaire. C'est ce qui rend l'ajout
        //  petit : une seule notion de table, deux facons de la remplir.
        //
        //  Les identifiants des onglets vivent dans une plage qui leur est
        //  propre (a partir de 1000), donc les trois fonctions ci-dessus
        //  gagnent un aiguillage en tete et le chemin CSV n'est pas touche.
        //  Enlever les trois aiguillages et ce bloc rend tout comme avant.
        //
        //  `SkipRow` n'est PAS concernee : chez toi elle prend un texte et
        //  compte une ligne ecartee, elle ne lit aucune table.
        if (name == "openworkbook") {
            // '' = le dernier classeur ouvert, ou celui que l'hote a designe.
            // Les macros posent d'abord Ask('classeur', ..., '') : repondre
            // vide au deuxieme import reprend le classeur du premier.
            std::string why;
            const int handle = xls::MacroXls::instance().openWorkbook(text(args, 0), why);
            if (handle < 0 && !why.empty()) {
                // Le motif, pas seulement l'echec : la macro qui suit appelle
                // Fail('classeur introuvable'), et sans ca le lecteur n'a aucun
                // moyen de savoir si c'est le chemin, le format, ou rien du tout.
                report_.warnings.push_back("OpenWorkbook : " + why);
            }
            // JAMAIS une valeur positive sur un echec : les macros testent `< 0`
            // et rien d'autre.
            result = Value::integer(sim::Type::DInt, handle);
            return true;
        }
        if (name == "opensheet") {
            const auto wanted = text(args, 0);
            const int handle = xls::MacroXls::instance().openSheet(wanted);
            if (handle < 0) {
                // Dire ce qui existe. `Cartes API` porte une espace, et un
                // onglet renomme est la premiere cause d'echec d'un import.
                std::string available;
                for (const auto& n : xls::MacroXls::instance().sheetNames())
                    available += (available.empty() ? "" : ", ") + n;
                report_.warnings.push_back(
                    "OpenSheet : aucun onglet nomme '" + wanted + "'"
                    + (available.empty()
                        ? " (aucun classeur ouvert : appelez OpenWorkbook d'abord)"
                        : " - dans le classeur : " + available));
            }
            result = Value::integer(sim::Type::DInt, handle);
            return true;
        }
        if (name == "hassheet") {
            // L'onglet existe-t-il ? Sans elle, un onglet FACULTATIF (Reglages,
            // Modbus) ne se testait qu'en l'ouvrant - et OpenSheet, a juste
            // titre, avertit quand il manque. Le compte rendu se remplissait
            // d'avertissements pour des onglets que personne n'avait demandes.
            const auto wanted = text(args, 0);
            bool found = false;
            for (const auto& n : xls::MacroXls::instance().sheetNames())
                if (n == wanted) found = true;
            result = Value::boolean(found);
            return true;
        }
        if (name == "setting") {
            // L'onglet Config est un formulaire, pas un tableau : la cle dans
            // une colonne, la valeur dans la premiere cellule non vide a sa
            // droite. Cle absente = chaine vide ; toutes les macros ont un repli.
            result = Value::text(xls::MacroXls::instance().setting(text(args, 0)));
            return true;
        }

        // ---- fichiers ---------------------------------------------------------
        //
        //  FileWrite(nom, texte) remplace le fichier par une ligne ; FileAppend
        //  (nom, texte) en ajoute une. Deux natives et pas une, parce qu'une
        //  liste d'alarmes s'ecrit toujours ainsi : l'en-tete, puis une ligne
        //  par alarme - et qu'un CSV relance ne doit pas grossir de la liste
        //  precedente.
        //
        //  Rien n'est ecrit pendant le tour : voir settleFiles. Un FileAppend
        //  sur un fichier que la macro n'a pas encore touche part de ce qu'il
        //  contient deja sur le disque.
        if (name == "filewrite" || name == "fileappend") {
            std::string why;
            const auto path = outputPathFor(text(args, 0), why);
            if (path.empty()) {
                report_.warnings.push_back(std::string(rawName) + " : " + why);
                result = Value::boolean(false);
                return true;
            }
            auto at = files_.find(path);
            if (at == files_.end()) {
                StagedFile fresh;
                if (name == "fileappend") {
                    fresh.content = readWhole(path);
                    if (!fresh.content.empty() && fresh.content.back() != '\n')
                        fresh.content.push_back('\n');
                    fresh.lines = static_cast<std::size_t>(
                        std::count(fresh.content.begin(), fresh.content.end(), '\n'));
                }
                at = files_.emplace(path, std::move(fresh)).first;
                fileOrder_.push_back(path);
            }
            if (name == "filewrite") {
                at->second.content.clear();
                at->second.lines = 0;
            }
            at->second.content += text(args, 1);
            at->second.content.push_back('\n');
            ++at->second.lines;
            result = Value::boolean(true);
            return true;
        }

        // ---- demander ---------------------------------------------------------
        if (name == "ask") {
            MacroQuestion q;
            q.kind = MacroQuestion::Kind::Text;
            q.key = text(args, 0);
            q.prompt = text(args, 1);
            q.preset = args.size() > 2 ? text(args, 2) : std::string{};
            result = Value::text(ask(std::move(q)));
            return true;
        }
        if (name == "askchoice") {
            MacroQuestion q;
            q.kind = MacroQuestion::Kind::Choice;
            q.key = text(args, 0);
            q.prompt = text(args, 1);
            // "A,B,C" rather than a list, for the same reason handles exist.
            std::string csv = text(args, 2);
            std::size_t from = 0;
            while (from <= csv.size()) {
                const auto comma = csv.find(',', from);
                auto part = csv.substr(from, (comma == std::string::npos ? csv.size() : comma) - from);
                if (!part.empty()) q.choices.push_back(part);
                if (comma == std::string::npos) break;
                from = comma + 1;
            }
            q.preset = args.size() > 3 ? text(args, 3)
                : (q.choices.empty() ? std::string{} : q.choices.front());
            result = Value::text(ask(std::move(q)));
            return true;
        }
        if (name == "asknumber") {
            MacroQuestion q;
            q.kind = MacroQuestion::Kind::Number;
            q.key = text(args, 0);
            q.prompt = text(args, 1);
            q.minimum = number(args, 2);
            q.maximum = number(args, 3);
            q.preset = args.size() > 4 ? text(args, 4) : "0";
            result = Value::real(std::atof(ask(std::move(q)).c_str()));
            return true;
        }
        if (name == "askyesno" || name == "confirm") {
            MacroQuestion q;
            q.kind = MacroQuestion::Kind::YesNo;
            q.key = name == "confirm" ? "confirm" : text(args, 0);
            q.prompt = name == "confirm" ? text(args, 0) + " - " + text(args, 1) : text(args, 1);
            q.preset = "O";
            const auto answer = ask(std::move(q));
            result = Value::boolean(lower(answer) == "o" || lower(answer) == "oui"
                || lower(answer) == "yes" || answer == "1");
            return true;
        }

        // Unknown. Reported by name, because a typo in a macro must not look like a
        // function that quietly did nothing.
        report_.diagnostics.push_back(sim::Diagnostic{
            sim::Diagnostic::Severity::Error,
            "fonction inconnue : " + std::string(rawName), 0, "macro" });
        report_.ok = false;
        failed = true;
        return false;
    }

    // ================================================================ MacroRunner ==
    MacroRunner::MacroRunner(ProjectPtr project, SharedLibrary* library)
        : env_(std::make_unique<Environment>(std::move(project), library)) {}

    MacroRunner::~MacroRunner() = default;

    void MacroRunner::setAnswers(std::map<std::string, std::string> answers) {
        env_->answers = std::move(answers);
    }

    void MacroRunner::addTable(std::string name, std::string csvBytes, TableOptions options) {
        if (options.anchors.empty()) {
            // The workbook's own anchors, so ImportTable('cartes', ...) works with no
            // ceremony on the file this program produces.
            options.anchors = { "Carte", "Designation", "Adresse" };
            options.descriptionRows = 1;
        }
        env_->tables[name] = Table::parse(csvBytes, options);
        if (std::find(env_->tableOrder.begin(), env_->tableOrder.end(), name)
            == env_->tableOrder.end())
            env_->tableOrder.push_back(name);
    }

    MacroReport MacroRunner::run(std::string_view source, std::string name, MacroMode mode) {
        env_->reset(name);
        env_->mode = mode;

        auto program = sim::parse(source, name);
        if (!program) {
            MacroReport report;
            report.ok = false;
            report.failure = program.error().message();
            report.diagnostics.push_back(sim::Diagnostic{
                sim::Diagnostic::Severity::Error, program.error().message(), 0, name });
            return report;
        }

        const auto outcome = sim::execute(**program, *env_);
        auto& report = env_->report();
        // AskNow stopped the round on purpose. Not a failure: a round that did what
        // it was told.
        if (env_->stopped) {
            report.needsAnswers = true;
            report.stopped = true;
        }
        if (!outcome.completed && report.failure.empty()) {
            report.ok = false;
            // NAME THE REASON. It used to say "interrupted (execution limit or
            // error)", which covers both a runaway loop and an undefined variable
            // and tells the reader neither. The interpreter already reported what
            // happened; the only thing missing was passing it on.
            std::string why;
            for (const auto& d : report.diagnostics) {
                // UNREACHABLE TODAY, AND KEPT ON PURPOSE. Nothing on the macro path
                // emits a non-Error diagnostic: the interpreter emits none, and this
                // environment's Warn() goes to warnings, not here. A mutation run
                // showed the guard could be deleted with no test noticing.
                //
                // It stays because the day a native reports something informational,
                // without it that line lands in the FAILURE text and reads as the
                // reason the macro stopped. One line against a wrong explanation.
                if (d.severity != sim::Diagnostic::Severity::Error) continue;
                if (!why.empty()) why += "\n";
                why += d.line > 0 ? "ligne " + std::to_string(d.line) + " : " + d.message
                    : d.message;
            }
            report.failure = why.empty()
                ? "la macro s'est arretee sans expliquer pourquoi ; "
                "une boucle sans fin est le cas le plus probable"
                : why;
        }
        for (const auto& d : report.diagnostics)
            if (d.severity == sim::Diagnostic::Severity::Error) report.ok = false;

        // A run that asked something is not a failure - it is a run that needs a
        // second pass. Saying "ok" for it would make the caller apply a macro that
        // ran with blanks.
        if (report.needsAnswers) report.ok = false;

        // Everything ran. Now decide what to keep.
        //
        //   Preview  : undo the lot. The reader sees what would happen, and the
        //              project is exactly as they left it.
        //   A round that asked, or a failure : undo too. A macro that ran with a
        //              blank, or stopped halfway, must leave nothing behind.
        //   Apply, successful : keep it, marked as already applied.
        const bool keep = (mode == MacroMode::Apply) && report.ok && !report.needsAnswers;
        if (!keep) {
            if (env_->commands) (void)env_->commands->undo();
            env_->commands = std::make_unique<CompoundCommand>(name);
        }
        // Les fichiers suivent la meme regle que le projet : ecrits si le tour
        // est garde, annonces par un apercu reussi, oublies sinon. Un tour qui
        // pose encore des questions n'annonce rien : il sera rejoue.
        if (keep || (mode == MacroMode::Preview && report.ok)) env_->settleFiles(keep);
        return report;
    }

    std::vector<std::string> MacroRunner::tableNames() const { return env_->tableOrder; }

    core::CommandPtr MacroRunner::takeCommand() {
        if (!env_->commands || env_->commands->empty()) return nullptr;
        return std::move(env_->commands);
    }

    const std::vector<MacroRunner::NativeInfo>& MacroRunner::natives() {
        // The list IS the permission list, and it is also what the editor's
        // completion offers. One place, so the two cannot disagree about what a
        // macro is allowed to do.
        static const std::vector<NativeInfo> kNatives = {
            {"Interroger", "TypeExists(nom) : BOOL", "Un DDT de ce nom est-il dans le projet ?"},
            {"Interroger", "PouExists(nom) : BOOL", "Un DFB ou une unite de programme de ce nom ?"},
            {"Interroger", "VariableExists(nom, proprietaire) : BOOL",
             "Une variable GLOBALE de ce nom ? Avec un proprietaire (facultatif) : une variable de ce DFB, "
             "de ce DDT ou de cette unite. Les parametres des DFB ne comptent pas pour une globale."},
            {"Interroger", "SectionExists(nom) : BOOL", "Une section de ce nom ?"},
            {"Interroger", "SectionLines(nom) : DINT", "Combien de lignes elle compte."},
            {"Interroger", "VariableType(nom) : STRING",
             "Le type d'une variable globale, tel qu'il est ecrit, ou '' si elle n'existe pas."},
            {"Interroger", "HasMember(type, champ) : BOOL",
             "Ce DDT ou ce DFB a-t-il ce champ ? Le projet d'abord, puis la bibliotheque. "
             "Un seul niveau : 'Thr', pas 'Thr[0].Val'."},
            {"Interroger", "MemberType(type, champ) : STRING",
             "Le type de ce champ, tel qu'il est ecrit ('REAL', 'ARRAY[0..1] OF BOOL'), "
             "ou '' s'il n'existe pas."},
            {"Interroger", "SectionRank(nom) : DINT",
             "Le rang de la section dans l'ordre d'execution de sa tache, a partir de 0 ; "
             "-1 si aucune tache ne l'execute."},

            {"Creer", "AddDerivedType(nom, version) : BOOL", "Cree un DDT vide."},
            {"Creer", "AddFunctionBlock(nom, version) : BOOL", "Cree un type DFB."},
            {"Creer", "AddProgramUnit(nom, tache) : BOOL", "Cree une unite de programme."},
            {"Creer", "AddSubroutine(nom, tache) : BOOL",
             "Cree une sous-routine. Elle ne tourne que lorsqu'on l'appelle."},
            {"Interroger", "IsSubroutine(nom) : BOOL", "Cette section est-elle une SR ?"},
            {"Creer", "AddSection(nom, tache, langage, proprietaire) : BOOL",
             "Proprietaire vide pour une section de tache, sinon le nom du POU."},
            {"Creer", "AddVariable(nom, type, portee, proprietaire, initial, commentaire, adresse) : BOOL",
             "Portee : Global, Input, Output, InOut, Public, Private, Member. "
             "Proprietaire vide pour une globale. Les trois derniers sont facultatifs : "
             "la valeur initiale ('-1'), le commentaire, l'adresse ('%MW100')."},

            {"Editer", "ClearSection(section) : BOOL",
             "Vide une section. Une section generee se reecrit entierement, "
             "jamais a moitie."},
            {"Editer", "AppendToSection(section, ligne) : BOOL",
             "Ajoute une ligne a la fin d'une section existante."},
            {"Editer", "SectionText(section) : STRING",
             "Le texte de la section, '' si elle n'existe pas : retrouver ce qu'une macro "
             "y a deja ecrit (le Count d'un appel, une ligne deja la)."},
            {"Editer", "ReplaceInSection(section, ancien, nouveau) : DINT",
             "Remplace chaque occurrence exacte d'ancien par nouveau et rend combien. Une "
             "seule annulation ; aucune occurrence : rien n'est touche."},
            {"Editer", "PlaceSection(section, avant) : BOOL",
             "Place la section juste avant 'avant' dans l'ordre d'execution de sa tache ; "
             "'' la met en dernier. Deja a sa place : VRAI, rien ne change."},

            {"Bibliotheque", "LibHasItem(nom) : BOOL", "La bibliotheque partagee propose-t-elle cet element ?"},
            {"Bibliotheque", "LibVersion(nom) : STRING", "La version qu'elle propose."},
            {"Bibliotheque", "LibIsOutdated(nom) : BOOL",
             "Le projet en a-t-il une copie plus ancienne que la bibliotheque ?"},
            {"Bibliotheque", "LibImport(nom) : BOOL",
             "Importe l'element. En apercu, l'import n'est pas effectue : il est annonce."},
            {"Bibliotheque", "LibOutdated() : STRING",
             "Ce que le projet a en retard sur la bibliotheque, separe par des virgules : "
             "'ST_EQ_Pump,DFB_EQ_PUMP'. Vide si tout est a jour."},
            {"Bibliotheque", "LibUpdate(nom) : BOOL",
             "Remplace la copie du projet par celle de la bibliotheque si elle est plus "
             "ancienne. VRAI si une mise a jour a eu lieu. Pas annulable par Ctrl+Z."},
            {"Bibliotheque", "RunMacro(nom) : BOOL",
             "Lance une macro de la bibliotheque. Elle partage les reponses, le classeur et "
             "l'annulation, pas les variables. Son echec arrete l'appelante."},

            {"Fichiers", "FileWrite(nom, texte) : BOOL",
             "Remplace le fichier par cette ligne. A cote du classeur, en .csv, .txt, .md, "
             ".st, .log ou .json. Ecrit a la fin, si la macro reussit."},
            {"Fichiers", "FileAppend(nom, texte) : BOOL",
             "Ajoute une ligne au fichier. Memes regles que FileWrite."},

            {"Tableaux", "OpenTable(nom) : DINT", "Ouvre un tableau fourni par l'hote. Rend un handle."},
            {"Tableaux", "ImportTable(nom) : DINT", "Synonyme d'OpenTable."},
            {"Tableaux", "RowCount(handle) : DINT", "Combien de lignes."},
            {"Tableaux", "Cell(handle, ligne, colonne) : STRING",
             "La colonne est cherchee par son nom, casse et accents ignores."},
            {"Tableaux", "HasColumn(handle, colonne) : BOOL", "Cette colonne existe-t-elle ?"},

            {"Classeur", "OpenWorkbook(chemin) : DINT",
             "Ouvre un .xlsx ou .xlsm. Chemin vide = le dernier ouvert. "
             "Rend 0 ou plus, ou -1 : les macros testent '< 0'."},
            {"Classeur", "OpenSheet(nom) : DINT",
             "Un onglet, rendu comme un tableau : RowCount, Cell et HasColumn "
             "marchent dessus. Le nom est exact, espaces compris."},
            {"Classeur", "HasSheet(nom) : BOOL",
             "Cet onglet est-il dans le classeur ouvert ? Pour un onglet facultatif, "
             "avant OpenSheet, qui avertit quand il manque."},
            {"Classeur", "Setting(cle) : STRING",
             "Un reglage de l'onglet Config, qui est un formulaire et non un "
             "tableau. Cle absente = chaine vide."},

            {"Demander", "AskNow()",
             "Pose ce qui a ete demande jusqu'ici, puis relance la macro. Sans elle, un "
             "tour pose aussi les questions de la branche que la reponse va ecarter."},
            {"Demander", "Ask(cle, libelle, defaut) : STRING", "Une question libre."},
            {"Demander", "AskChoice(cle, libelle, 'A,B,C', defaut) : STRING", "Un choix dans une liste."},
            {"Demander", "AskNumber(cle, libelle, mini, maxi, defaut) : REAL", "Un nombre borne."},
            {"Demander", "AskYesNo(cle, libelle) : BOOL", "Oui ou non."},
            {"Demander", "Confirm(titre, message) : BOOL", "La question posee avant d'appliquer."},

            {"Conversions", "INT_TO_STRING(v) : STRING", "Aussi DINT, UDINT, UINT, BOOL."},
            {"Conversions", "REAL_TO_STRING(v) : STRING",
             "Garde un point decimal : un REAL ecrit '16' est un INT pour Control Expert."},
            {"Conversions", "STRING_TO_INT(t) : DINT", "Aussi STRING_TO_REAL."},
            {"Conversions", "REAL_TO_INT(v) : DINT", "Tronque. Aussi REAL_TO_DINT et TRUNC."},
            {"Conversions", "LEN(t) : DINT", "Longueur d'une chaine."},
            {"Conversions", "MID(t, depuis, combien) : STRING",
             "Un morceau de chaine. Le troisieme argument est facultatif."},
            {"Conversions", "STARTSWITH(t, debut) : BOOL", "La chaine commence-t-elle par ca ?"},
            {"Conversions", "CHR(code) : STRING",
             "Un caractere par son code. CHR(39) est une apostrophe."},
            {"Conversions", "FIND(t, cherche, depuis) : DINT",
             "Ou commence 'cherche', ou -1. Le troisieme argument est facultatif."},
            {"Conversions", "UPPER(t) : STRING",
             "En majuscules. Aussi LOWER. ST_EQ_Pump -> 'DFB_EQ_' + UPPER('Pump')."},
            {"Conversions", "QUOTE(t) : STRING",
             "Le texte en litteral ST, apostrophes comprises : d'urgence -> 'd$'urgence'. "
             "A utiliser pour tout libelle ecrit dans une section."},
            {"Conversions", "ASCII(t) : STRING",
             "Le texte sans accents : Debit -> Debit, e accent aigu -> e. Ce qui n'a pas "
             "d'equivalent devient '?'."},

            {"Rendre compte", "Log(texte)", "Une ligne dans le compte rendu."},
            {"Rendre compte", "Warn(texte)", "Un avertissement, sans arreter la macro."},
            {"Rendre compte", "Fail(texte)", "Arrete la macro. Rien n'est applique."},
            {"Rendre compte", "SkipRow(texte)",
             "Compte une ligne du tableau ecartee, et dit pourquoi."},
        };
        return kNatives;
    }

} // namespace project