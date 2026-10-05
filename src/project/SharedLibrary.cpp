#include "SharedLibrary.hpp"

#include "../import/ProjectParser.hpp"

#include "EditCommands.hpp"
#include "../domain/Reindex.hpp"      // lot API 7 : remplacer un bloc sans casser les indices
#include "MacroFolders.hpp"
#include "MacroSpec.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace project {

    namespace fs = std::filesystem;

    namespace {

        std::string trim(std::string_view s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
            return std::string(s);
        }

        std::vector<std::string> split(std::string_view text, char sep) {
            std::vector<std::string> out;
            std::size_t from = 0;
            while (from <= text.size()) {
                const auto at = text.find(sep, from);
                out.push_back(trim(text.substr(from, (at == std::string_view::npos ? text.size() : at) - from)));
                if (at == std::string_view::npos) break;
                from = at + 1;
            }
            return out;
        }

        std::string readAll(const fs::path& p) {
            std::ifstream in(p, std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        core::Status writeAll(const fs::path& p, const std::string& text) {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            if (!out) return core::fail(core::ErrorCode::FileUnreadable, "cannot write", p.string());
            out << text;
            return core::ok();
        }

        std::string safeName(std::string_view name) {
            std::string out;
            for (char c : name) {
                const bool bad = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?'
                    || c == '"' || c == '<' || c == '>' || c == '|';
                out.push_back(bad ? '_' : c);
            }
            return out.empty() ? "unnamed" : out;
        }

        // One variable per line, the same shape the project folder uses so a library
        // entry and a project file can be read with the same eyes.
        std::string encodeVariable(const domain::Project& p, const domain::Variable& v) {
            std::string line;
            line += p.strings.text(v.name);                     line += " ; ";
            line += p.strings.text(v.type.name);                line += " ; ";
            line += std::string(domain::toString(v.scope));     line += " ; ";
            line += p.strings.text(v.initValue);                line += " ; ";
            std::string comment(p.strings.text(v.comment));
            std::replace(comment.begin(), comment.end(), '\n', ' ');
            std::replace(comment.begin(), comment.end(), ';', ',');
            line += comment;
            return line;
        }

        domain::VariableScope scopeFromName(std::string_view s) {
            if (s == "Local")    return domain::VariableScope::Local;
            if (s == "Public")   return domain::VariableScope::Public;
            if (s == "Input")    return domain::VariableScope::Input;
            if (s == "Output")   return domain::VariableScope::Output;
            if (s == "InOut")    return domain::VariableScope::InOut;
            if (s == "Member")   return domain::VariableScope::DerivedMember;
            return domain::VariableScope::Global;
        }

    } // namespace

    std::string LibraryItem::fileName() const {
        switch (kind) {
        case LibraryItemKind::DerivedType:   return safeName(name) + ".ddt";
        case LibraryItemKind::FunctionBlock: return safeName(name) + ".dfb";
        case LibraryItemKind::Macro:         return safeName(name) + ".mac";
        }
        return safeName(name);
    }

    SharedLibrary::SharedLibrary(std::string root) : root_(std::move(root)) {}

    namespace {
        std::string& rootOverride() {
            static std::string root;
            return root;
        }
    } // namespace

    std::string SharedLibrary::defaultRoot() {
        if (!rootOverride().empty()) return rootOverride();
        std::error_code ec;
        return (fs::current_path(ec) / "libs").string();
    }

    void SharedLibrary::setDefaultRoot(std::string root) { rootOverride() = std::move(root); }

    // --------------------------------------------------------------------- scan --
    core::Status SharedLibrary::scan() {
        items_.clear();
        std::error_code ec;
        if (!fs::exists(root_, ec)) return core::ok();      // an empty library is not an error

        const auto index = fs::path(root_) / "index.txt";
        if (!fs::exists(index, ec)) return core::ok();

        const auto text = readAll(index);
        std::size_t from = 0;
        while (from <= text.size()) {
            const auto nl = text.find('\n', from);
            const auto line = trim(std::string_view(text).substr(
                from, (nl == std::string::npos ? text.size() : nl) - from));
            from = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
            if (line.empty() || line.front() == '#') continue;

            const auto f = split(line, ';');
            if (f.size() < 4) continue;
            LibraryItem item;
            item.kind = f[0] == "dfb" ? LibraryItemKind::FunctionBlock
                : f[0] == "mac" ? LibraryItemKind::Macro
                : LibraryItemKind::DerivedType;
            item.category = f[1];
            item.name = f[2];
            item.version = f[3];
            if (f.size() > 4) item.author = f[4];
            if (f.size() > 5) item.comment = f[5];
            item.path = (fs::path(root_) / item.category / item.fileName()).string();
            items_.push_back(std::move(item));
        }
        return core::ok();
    }

    std::vector<std::string> SharedLibrary::categories() const {
        std::vector<std::string> out;
        for (const auto& item : items_)
            if (std::find(out.begin(), out.end(), item.category) == out.end())
                out.push_back(item.category);
        std::sort(out.begin(), out.end());
        return out;
    }

    const LibraryItem* SharedLibrary::find(std::string_view name) const {
        for (const auto& item : items_)
            if (item.name == name) return &item;
        return nullptr;
    }

    // ------------------------------------------------------------------ publish --
    namespace {

        core::Status rewriteIndex(const std::string& root, std::vector<LibraryItem> items) {
            std::sort(items.begin(), items.end(), [](const LibraryItem& a, const LibraryItem& b) {
                return a.category == b.category ? a.name < b.name : a.category < b.category;
                });
            std::ostringstream out;
            out << "# Shared library. One entry per line:\n"
                "#   kind ; category ; name ; version ; author ; comment\n"
                "# kind is ddt or dfb. The file itself lives at <category>/<name>.<kind>.\n"
                "# Versions here are what the library offers; a project keeps the copy it\n"
                "# imported until someone decides to update it.\n";
            for (const auto& item : items)
                out << (item.kind == LibraryItemKind::FunctionBlock ? "dfb"
                    : item.kind == LibraryItemKind::Macro ? "mac" : "ddt") << " ; "
                << item.category << " ; " << item.name << " ; " << item.version << " ; "
                << item.author << " ; " << item.comment << '\n';
            return writeAll(fs::path(root) / "index.txt", out.str());
        }

    } // namespace

    core::Status SharedLibrary::publishDerivedType(const domain::Project& p, std::string_view name,
        std::string category, bool replace) {
        const domain::DerivedType* ddt = nullptr;
        for (const auto& d : p.derivedTypes)
            if (p.strings.text(d.name) == name) ddt = &d;
        if (!ddt) return core::fail(core::ErrorCode::OutOfRange,
            "no derived type called '" + std::string(name) + "'");

        if (const auto* existing = find(name); existing && !replace && existing->version != ddt->version)
            return core::fail(core::ErrorCode::DuplicateSymbol,
                "the library already holds '" + std::string(name) + "' at version "
                + existing->version + "; publishing would replace it");

        std::ostringstream body;
        body << "# Derived data type, shared.\n";
        body << "name = " << name << '\n';
        body << "version = " << (ddt->version.empty() ? "0.01" : ddt->version) << '\n';
        body << "\n# name ; type ; scope ; initial ; comment\n";
        for (auto fi : ddt->fields) body << encodeVariable(p, p.variables[fi]) << '\n';

        LibraryItem item;
        item.kind = LibraryItemKind::DerivedType;
        item.name = std::string(name);
        item.category = category.empty() ? "General" : std::move(category);
        item.version = ddt->version.empty() ? "0.01" : ddt->version;
        item.path = (fs::path(root_) / item.category / item.fileName()).string();

        if (auto r = writeAll(item.path, body.str()); !r) return r;

        auto updated = items_;
        std::erase_if(updated, [&](const LibraryItem& i) { return i.name == item.name; });
        updated.push_back(item);
        if (auto r = rewriteIndex(root_, updated); !r) return r;
        items_ = std::move(updated);
        return core::ok();
    }

    core::Status SharedLibrary::publishFunctionBlock(const domain::Project& p, std::string_view name,
        std::string category, bool replace) {
        const domain::Pou* pou = nullptr;
        for (const auto& candidate : p.pous)
            if (candidate.kind == domain::PouKind::FunctionBlockType
                && p.strings.text(candidate.name) == name) pou = &candidate;
        if (!pou) return core::fail(core::ErrorCode::OutOfRange,
            "no DFB type called '" + std::string(name) + "'");

        if (const auto* existing = find(name); existing && !replace && existing->version != pou->version)
            return core::fail(core::ErrorCode::DuplicateSymbol,
                "the library already holds '" + std::string(name) + "' at version "
                + existing->version + "; publishing would replace it");

        std::ostringstream body;
        body << "# Derived function block, shared.\n";
        body << "name = " << name << '\n';
        body << "version = " << (pou->version.empty() ? "0.01" : pou->version) << '\n';
        for (const auto& [key, value] : pou->attributes)
            body << "attribute = " << key << " = " << value << '\n';
        body << "\n# name ; type ; scope ; initial ; comment\n";
        for (auto vi : pou->parameters) body << encodeVariable(p, p.variables[vi]) << '\n';
        for (auto vi : pou->locals)     body << encodeVariable(p, p.variables[vi]) << '\n';

        // Bodies are kept verbatim, delimited so a section containing anything at
        // all still round-trips.
        for (auto si : pou->sections) {
            if (si >= p.sections.size()) continue;
            const auto& section = p.sections[si];
            body << "\n<<<section " << p.strings.text(section.name) << " "
                << domain::toString(section.language) << ">>>\n";
            body << section.body;
            if (!section.body.empty() && section.body.back() != '\n') body << '\n';
            body << "<<<end>>>\n";
        }

        LibraryItem item;
        item.kind = LibraryItemKind::FunctionBlock;
        item.name = std::string(name);
        item.category = category.empty() ? "General" : std::move(category);
        item.version = pou->version.empty() ? "0.01" : pou->version;
        item.path = (fs::path(root_) / item.category / item.fileName()).string();

        if (auto r = writeAll(item.path, body.str()); !r) return r;

        auto updated = items_;
        std::erase_if(updated, [&](const LibraryItem& i) { return i.name == item.name; });
        updated.push_back(item);
        if (auto r = rewriteIndex(root_, updated); !r) return r;
        items_ = std::move(updated);
        return core::ok();
    }

    // ------------------------------------------------------------------- import --
    // ---------------------------------------------------------------- macros -----
    std::string SharedLibrary::macroSource(std::string_view name) const {
        const auto* item = find(name);
        if (!item || item->kind != LibraryItemKind::Macro) return {};
        return readAll(item->path);
    }

    core::Status SharedLibrary::publishMacro(std::string_view name, std::string_view source,
        std::string category, std::string version,
        std::string comment) {
        LibraryItem item;
        item.kind = LibraryItemKind::Macro;
        item.name = std::string(name);
        item.category = category.empty() ? "Macros" : std::move(category);
        // La version et le commentaire : ceux qu'on donne, sinon ceux de la macro
        // elle-meme ("#! version", "#! summary"), sinon ceux de l'index.
        const auto spec = macro::parseMacroSpec(source, name);
        const auto* before = find(name);
        if (version.empty()) version = !spec.version.empty() ? spec.version
                                     : before && !before->version.empty() ? before->version : std::string("1.00");
        if (comment.empty()) comment = !spec.summary.empty() ? macro::foldAccents(spec.summary)
                                     : before ? before->comment : std::string{};
        std::replace(comment.begin(), comment.end(), ';', ',');
        std::replace(comment.begin(), comment.end(), '\n', ' ');
        if (before && before->kind == LibraryItemKind::Macro && !before->author.empty()) item.author = before->author;
        item.version = std::move(version);
        item.comment = std::move(comment);
        item.path = (fs::path(root_) / item.category / item.fileName()).string();

        if (auto r = writeAll(item.path, std::string(source)); !r) return r;

        auto updated = items_;
        std::erase_if(updated, [&](const LibraryItem& i) { return i.name == item.name; });
        updated.push_back(item);
        if (auto r = rewriteIndex(root_, updated); !r) return r;
        items_ = std::move(updated);
        return core::ok();
    }

    core::Status SharedLibrary::ensureDefaultMacros() {
        auto updated = items_;
        bool changed = false;

        // Lot macros 1 : une macro livree qu'on a supprimee (elle est dans la
        // corbeille) ou renommee n'est pas remise : c'etait une decision.
        std::vector<std::string> retired;
        {
            const auto trash = fs::path(macrosFolder()) / "_corbeille" / "corbeille.txt";
            const auto text = readAll(trash);
            std::size_t from = 0;
            while (from < text.size()) {
                const auto nl = text.find('\n', from);
                const auto line = trim(std::string_view(text).substr(from, (nl == std::string::npos ? text.size() : nl) - from));
                from = nl == std::string::npos ? text.size() : nl + 1;
                if (line.empty() || line.front() == '#') continue;
                const auto f = split(line, ';');
                if (f.size() >= 2 && (f[0] == "supprimee" || f[0] == "renommee")) retired.push_back(f[1]);
            }
        }
        for (const auto& def : defaultMacros()) {
            if (std::find(retired.begin(), retired.end(), def.name) != retired.end()) continue;
            LibraryItem item;
            item.kind = LibraryItemKind::Macro;
            item.name = def.name;
            item.category = def.category;
            item.version = def.version;
            item.comment = def.comment;
            item.path = (fs::path(root_) / item.category / item.fileName()).string();

            std::error_code ec;
            // ONLY WHAT IS MISSING. A file somebody corrected stays corrected; if its
            // version is behind, outdated() says so and the reader decides.
            if (fs::exists(item.path, ec)) {
                if (std::none_of(updated.begin(), updated.end(),
                    [&](const LibraryItem& i) { return i.name == item.name; })) {
                    updated.push_back(item);      // present on disk, missing from the index
                    changed = true;
                }
                continue;
            }
            if (auto r = writeAll(item.path, def.source); !r) return r;
            std::erase_if(updated, [&](const LibraryItem& i) { return i.name == item.name; });
            updated.push_back(item);
            changed = true;
        }

        if (!changed) return core::ok();
        if (auto r = rewriteIndex(root_, updated); !r) return r;
        items_ = std::move(updated);
        return core::ok();
    }

    core::Result<SharedLibrary::ImportOutcome> SharedLibrary::import(domain::Project& p,
        std::string_view name,
        OnConflict onConflict) const {
        const auto* item = find(name);
        if (!item) return core::fail(core::ErrorCode::OutOfRange,
            "the library has no '" + std::string(name) + "'");

        // A macro is not copied into the project; it is run against it. Answering
        // "imported" here would leave the caller believing something is in the
        // project that never went anywhere near it.
        if (item->kind == LibraryItemKind::Macro)
            return core::fail(core::ErrorCode::InvalidArgument,
                "'" + std::string(name) + "' is a macro: it is not imported into the "
                "project, it is run against it. Use macroSource() and MacroRunner.");

        const bool exists = !nameIsFree(p, name);
        std::string importAs = item->name;
        if (exists) {
            switch (onConflict) {
            case OnConflict::Refuse:
                return core::fail(core::ErrorCode::DuplicateSymbol,
                    "'" + item->name + "' already exists in this project");
            case OnConflict::Duplicate: {
                // A version suffix rather than "copy": which one you are looking
                // at matters more than the fact that there are two.
                auto suffix = item->version;
                std::replace(suffix.begin(), suffix.end(), '.', '_');
                importAs = item->name + "_v" + suffix;
                int attempt = 2;
                while (!nameIsFree(p, importAs))
                    importAs = item->name + "_v" + suffix + "_" + std::to_string(attempt++);
                break;
            }
            case OnConflict::Overwrite:
                break;      // handled below by removing the existing definition
            }
        }

        const auto text = readAll(item->path);
        if (text.empty()) return core::fail(core::ErrorCode::FileNotFound, item->path);

        // Overwriting means the old definition goes; its members go with it.
        //
        // LOT API 7 : AVEC LA RENUMEROTATION. L'ancien code faisait un erase_if
        // sur p.pous (ou p.derivedTypes) : tout ce qui suivait glissait d'un
        // cran SANS que les indices qui le designent soient recales. Les
        // sections d'une unite de programme gardaient l'indice de leur POU -
        // devenu celui de la POU d'apres : l'ordre d'execution, les unites et
        // le simulateur voyaient des sections rattachees au mauvais bloc
        // (8 662 lignes devenaient 2 074 au tableau de bord), et les sections
        // et variables de l'ancien bloc restaient en orphelines. Chaque morceau
        // part maintenant par domain::eraseEntity, qui recale TOUTES les
        // references (Reindex.hpp) : ses sections, ses variables, puis lui -
        // du plus grand indice au plus petit, pour que ceux qui restent a
        // effacer ne bougent pas.
        if (exists && onConflict == OnConflict::Overwrite) {
            const auto eraseAll = [&p](domain::EntityKind kind, std::vector<domain::Index> list) {
                std::sort(list.begin(), list.end(), std::greater<domain::Index>());
                list.erase(std::unique(list.begin(), list.end()), list.end());
                const std::size_t count = kind == domain::EntityKind::Section ? p.sections.size() : p.variables.size();
                for (const auto i : list)
                    if (i < count) (void)domain::eraseEntity(p, kind, i);
            };
            if (item->kind == LibraryItemKind::DerivedType) {
                for (domain::Index d = 0; d < p.derivedTypes.size(); ++d) {
                    if (p.strings.text(p.derivedTypes[d].name) != name) continue;
                    eraseAll(domain::EntityKind::Variable, p.derivedTypes[d].fields);
                    (void)domain::eraseEntity(p, domain::EntityKind::DerivedType, d);
                    break;
                }
            } else {
                for (domain::Index i = 0; i < p.pous.size(); ++i) {
                    const auto& pou = p.pous[i];
                    if (pou.kind != domain::PouKind::FunctionBlockType || p.strings.text(pou.name) != name) continue;
                    eraseAll(domain::EntityKind::Section, pou.sections);
                    std::vector<domain::Index> vars = p.pous[i].parameters;
                    vars.insert(vars.end(), p.pous[i].locals.begin(), p.pous[i].locals.end());
                    eraseAll(domain::EntityKind::Variable, std::move(vars));
                    (void)domain::eraseEntity(p, domain::EntityKind::Pou, i);
                    break;
                }
            }
        }

        std::string version = item->version;
        std::vector<domain::Variable> declarations;
        std::vector<std::pair<std::string, std::string>> attributes;
        std::vector<std::tuple<std::string, domain::PouLanguage, std::string>> sections;

        std::size_t from = 0;
        while (from <= text.size()) {
            const auto nl = text.find('\n', from);
            const auto raw = std::string_view(text).substr(
                from, (nl == std::string::npos ? text.size() : nl) - from);
            from = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
            const auto line = trim(raw);
            if (line.empty() || line.front() == '#') continue;

            if (line.rfind("<<<section ", 0) == 0) {
                const auto header = line.substr(11, line.size() - 14);
                const auto space = header.rfind(' ');
                const auto sectionName = space == std::string::npos ? header : header.substr(0, space);
                const auto language = space == std::string::npos ? std::string("ST")
                    : header.substr(space + 1);
                std::string body;
                while (from <= text.size()) {
                    const auto end = text.find('\n', from);
                    const auto bodyLine = std::string_view(text).substr(
                        from, (end == std::string::npos ? text.size() : end) - from);
                    from = (end == std::string::npos) ? text.size() + 1 : end + 1;
                    if (trim(bodyLine) == "<<<end>>>") break;
                    body.append(bodyLine);
                    body.push_back('\n');
                }
                sections.emplace_back(sectionName,
                    language == "IL" ? domain::PouLanguage::IL
                    : domain::PouLanguage::ST,
                    std::move(body));
                continue;
            }

            const auto eq = line.find('=');
            if (eq != std::string::npos && line.find(';') == std::string::npos) {
                const auto key = trim(line.substr(0, eq));
                const auto value = trim(line.substr(eq + 1));
                if (key == "version") version = value;
                else if (key == "attribute") {
                    const auto eq2 = value.find('=');
                    if (eq2 != std::string::npos)
                        attributes.emplace_back(trim(value.substr(0, eq2)), trim(value.substr(eq2 + 1)));
                }
                continue;
            }

            const auto f = split(line, ';');
            if (f.size() < 3) continue;
            domain::Variable v;
            v.name = p.strings.intern(f[0]);
            // RESOLU, pas seulement ecrit.
            //
            // Ceci ne posait que le NOM du type. Le resultat etait textuellement
            // juste et structurellement vide : ni classe, ni bornes de tableau, ni
            // derivedIndex. Control Expert s'en moque, il relit le texte - mais cet
            // analyseur-ci, non : une instance importee etait rangee dans les
            // variables elementaires, et le simulateur ne voyait aucun de ses
            // membres. Un ST_IO_Ana importe n'avait pas de .Thr[0].
            v.type = importer::classifyTypeName(f[1], p.strings);
            v.scope = scopeFromName(f[2]);
            if (f.size() > 3) v.initValue = p.strings.intern(f[3]);
            if (f.size() > 4) v.comment = p.strings.intern(f[4]);
            declarations.push_back(std::move(v));
        }

        ImportOutcome outcome;
        outcome.importedAs = importAs;
        outcome.replacedExisting = exists && onConflict == OnConflict::Overwrite;

        if (item->kind == LibraryItemKind::DerivedType) {
            domain::DerivedType ddt;
            ddt.name = p.strings.intern(importAs);
            ddt.version = version;
            for (auto& v : declarations) {
                v.scope = domain::VariableScope::DerivedMember;
                p.variables.push_back(std::move(v));
                ddt.fields.push_back(static_cast<domain::Index>(p.variables.size() - 1));
            }
            p.derivedTypes.push_back(std::move(ddt));
        }
        else {
            domain::Pou pou;
            pou.name = p.strings.intern(importAs);
            pou.kind = domain::PouKind::FunctionBlockType;
            pou.version = version;
            pou.attributes = std::move(attributes);
            const auto pouIndex = static_cast<domain::Index>(p.pous.size());

            for (auto& v : declarations) {
                v.owner = pouIndex;
                const auto scope = v.scope;
                p.variables.push_back(std::move(v));
                const auto vi = static_cast<domain::Index>(p.variables.size() - 1);
                if (scope == domain::VariableScope::Input || scope == domain::VariableScope::Output
                    || scope == domain::VariableScope::InOut)
                    pou.parameters.push_back(vi);
                else
                    pou.locals.push_back(vi);
            }
            p.pous.push_back(std::move(pou));

            for (auto& [sectionName, language, body] : sections) {
                domain::Section s;
                s.name = p.strings.intern(sectionName);
                s.language = language;
                s.owner = pouIndex;
                s.body = std::move(body);
                s.lineCount = s.body.empty()
                    ? 0u : static_cast<std::uint32_t>(std::count(s.body.begin(), s.body.end(), '\n'));
                p.sections.push_back(std::move(s));
                p.pous[pouIndex].sections.push_back(static_cast<domain::Index>(p.sections.size() - 1));
            }
        }

        p.buildIndices();
        // Le nouveau type doit etre RELIE, sinon la variable qui l'utilisera ensuite
        // le nommera sans le trouver - et sera classee comme elementaire.
        p.linkTypes();
        return outcome;
    }

    // ------------------------------------------------------------------ notices --
    std::vector<VersionNotice> SharedLibrary::outdated(const domain::Project& p) const {
        std::vector<VersionNotice> out;
        auto behind = [](const std::string& mine, const std::string& theirs) {
            // Compared as text, deliberately: version schemes vary and pretending to
            // understand them invites being wrong about which is newer. Different
            // means "worth telling you about".
            return !theirs.empty() && mine != theirs;
            };

        for (const auto& d : p.derivedTypes) {
            const auto name = std::string(p.strings.text(d.name));
            const auto* item = find(name);
            if (!item || item->kind != LibraryItemKind::DerivedType) continue;
            if (behind(d.version, item->version))
                out.push_back(VersionNotice{ name, d.version, item->version,
                                            LibraryItemKind::DerivedType });
        }
        for (const auto& pou : p.pous) {
            if (pou.kind != domain::PouKind::FunctionBlockType) continue;
            const auto name = std::string(p.strings.text(pou.name));
            const auto* item = find(name);
            if (!item || item->kind != LibraryItemKind::FunctionBlock) continue;
            if (behind(pou.version, item->version))
                out.push_back(VersionNotice{ name, pou.version, item->version,
                                            LibraryItemKind::FunctionBlock });
        }
        return out;
    }

    // ================================================================ lot macros 1 ==
    //  Creer, supprimer (dans la corbeille), restaurer, renommer, dupliquer.
    namespace {

        std::string lowerName(std::string_view s) {
            std::string out(s);
            for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        std::string nowText() {
            const std::time_t t = std::time(nullptr);
            const std::tm* tm = std::localtime(&t);
            char buf[32] = {};
            if (tm) std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", tm);
            return buf;
        }

        std::string field(std::string s) {
            std::replace(s.begin(), s.end(), ';', ',');
            std::replace(s.begin(), s.end(), '\n', ' ');
            std::replace(s.begin(), s.end(), '\r', ' ');
            return s;
        }

        // Deplacer un fichier ; a defaut (autre volume), le copier puis l'effacer.
        bool moveFile(const fs::path& from, const fs::path& to) {
            std::error_code ec;
            fs::create_directories(to.parent_path(), ec);
            ec.clear();
            fs::rename(from, to, ec);
            if (!ec) return true;
            ec.clear();
            fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
            if (ec) return false;
            fs::remove(from, ec);
            return true;
        }

        const char* const kTrashHeader =
            "# La corbeille des macros : Restaurer (onglet Macros, dossier Corbeille) remet une macro\n"
            "# a sa place, dans son dossier.\n"
            "#   supprimee ; <nom> ; <fichier> ; <date> ; <dossier> ; <version> ; <commentaire>\n"
            "#   renommee ; <ancien nom> ; <nouveau nom> ; <date>\n";

    } // namespace

    std::string SharedLibrary::macrosFolder() const { return (fs::path(root_) / "Macros").string(); }
    std::string SharedLibrary::macroFoldersFile() const { return (fs::path(root_) / "Macros" / "dossiers.txt").string(); }

    std::vector<SharedLibrary::TrashedMacro> SharedLibrary::trashedMacros() const {
        std::vector<TrashedMacro> out;
        const auto text = readAll(fs::path(macrosFolder()) / "_corbeille" / "corbeille.txt");
        std::size_t from = 0;
        while (from < text.size()) {
            const auto nl = text.find('\n', from);
            const auto line = trim(std::string_view(text).substr(from, (nl == std::string::npos ? text.size() : nl) - from));
            from = nl == std::string::npos ? text.size() : nl + 1;
            if (line.empty() || line.front() == '#') continue;
            const auto f = split(line, ';');
            if (f.size() < 3 || f[0] != "supprimee") continue;
            TrashedMacro t;
            t.name = f[1];
            t.file = f[2];
            if (f.size() > 3) t.date = f[3];
            if (f.size() > 4) t.folder = f[4];
            if (f.size() > 5) t.version = f[5];
            if (f.size() > 6) t.comment = f[6];
            out.push_back(std::move(t));
        }
        std::reverse(out.begin(), out.end());
        return out;
    }

    std::string SharedLibrary::macroFolderOf(std::string_view name) const {
        macro::MacroFolders folders(macroFoldersFile());
        (void)folders.load();
        for (const auto& [n, f] : folders.placements())
            if (lowerName(n) == lowerName(name)) return f;
        return macro::parseMacroSpec(macroSource(name), name).category;
    }

    core::Status SharedLibrary::createMacro(std::string_view name, std::string_view source, const std::string& folder) {
        const auto check = macro::checkMacroName(name);
        if (check.verdict == macro::Verdict::Error)
            return core::fail(core::ErrorCode::InvalidArgument, std::string(name) + " : " + check.message);
        for (const auto& item : items_)
            if (lowerName(item.name) == lowerName(name))
                return core::fail(core::ErrorCode::DuplicateSymbol,
                    "le nom " + std::string(name) + " est deja pris dans la bibliotheque (" + item.category + ")");
        const auto path = fs::path(macrosFolder()) / (std::string(name) + ".mac");
        std::error_code ec;
        if (fs::exists(path, ec))
            return core::fail(core::ErrorCode::DuplicateSymbol, "le fichier " + path.string() + " existe deja");
        if (auto r = publishMacro(name, source, "Macros"); !r) return r;
        macro::MacroFolders folders(macroFoldersFile());
        (void)folders.load();
        std::vector<std::pair<std::string, std::string>> known;
        for (const auto& item : items_)
            if (item.kind == LibraryItemKind::Macro)
                known.emplace_back(item.name, lowerName(item.name) == lowerName(name) ? std::string{}
                                                                                        : macro::parseMacroSpec(readAll(item.path), item.name).category);
        folders.setMacros(std::move(known));
        folders.place(std::string(name), folder);
        return folders.save();
    }

    core::Status SharedLibrary::deleteMacro(std::string_view name) {
        const LibraryItem* item = nullptr;
        for (const auto& i : items_)
            if (i.kind == LibraryItemKind::Macro && lowerName(i.name) == lowerName(name)) item = &i;
        if (!item) return core::fail(core::ErrorCode::OutOfRange, "aucune macro " + std::string(name) + " dans la bibliotheque");
        const std::string realName = item->name;
        const std::string folder = macroFolderOf(realName);
        const auto trash = fs::path(macrosFolder()) / "_corbeille";
        std::string fileName = realName + ".mac";
        std::error_code ec;
        for (int n = 2; fs::exists(trash / fileName, ec) && n < 1000; ++n) fileName = realName + "." + std::to_string(n) + ".mac";
        if (!moveFile(item->path, trash / fileName))
            return core::fail(core::ErrorCode::FileUnreadable, "impossible de deplacer " + item->path + " dans la corbeille");
        const std::string line = "supprimee ; " + realName + " ; " + fileName + " ; " + nowText() + " ; " + field(folder) + " ; "
                               + field(item->version) + " ; " + field(item->comment) + "\n";
        auto updated = items_;
        std::erase_if(updated, [&](const LibraryItem& i) { return i.kind == LibraryItemKind::Macro && i.name == realName; });
        if (auto r = rewriteIndex(root_, updated); !r) return r;
        items_ = std::move(updated);
        auto text = readAll(trash / "corbeille.txt");
        if (text.empty()) text = kTrashHeader;
        text += line;
        if (auto r = writeAll(trash / "corbeille.txt", text); !r) return r;
        macro::MacroFolders folders(macroFoldersFile());
        (void)folders.load();
        folders.forget(realName);
        return folders.save();
    }

    core::Status SharedLibrary::restoreMacro(std::string_view name) {
        const auto trashed = trashedMacros();
        const auto it = std::find_if(trashed.begin(), trashed.end(), [&](const TrashedMacro& t) { return lowerName(t.name) == lowerName(name); });
        if (it == trashed.end()) return core::fail(core::ErrorCode::OutOfRange, std::string(name) + " n'est pas dans la corbeille");
        for (const auto& item : items_)
            if (lowerName(item.name) == lowerName(it->name))
                return core::fail(core::ErrorCode::DuplicateSymbol,
                    "une macro " + item.name + " existe deja : renomme-la d'abord, puis restaure");
        const auto trash = fs::path(macrosFolder()) / "_corbeille";
        const auto target = fs::path(macrosFolder()) / (it->name + ".mac");
        if (!moveFile(trash / it->file, target))
            return core::fail(core::ErrorCode::FileUnreadable, "impossible de remettre " + it->file + " dans libs/Macros");
        LibraryItem item;
        item.kind = LibraryItemKind::Macro;
        item.name = it->name;
        item.category = "Macros";
        item.version = it->version.empty() ? "1.00" : it->version;
        item.comment = it->comment;
        item.author = "XpgAnalyzer";
        item.path = target.string();
        auto updated = items_;
        updated.push_back(item);
        if (auto r = rewriteIndex(root_, updated); !r) return r;
        items_ = std::move(updated);
        // La ligne de la corbeille s'en va (la plus recente de ce nom).
        const auto text = readAll(trash / "corbeille.txt");
        std::string out;
        std::vector<std::string> lines;
        std::size_t from = 0;
        while (from < text.size()) {
            const auto nl = text.find('\n', from);
            lines.push_back(text.substr(from, (nl == std::string::npos ? text.size() : nl) - from));
            from = nl == std::string::npos ? text.size() : nl + 1;
        }
        for (auto l = lines.rbegin(); l != lines.rend(); ++l) {
            const auto f = split(*l, ';');
            if (f.size() >= 3 && f[0] == "supprimee" && f[1] == it->name && f[2] == it->file) {
                lines.erase(std::next(l).base());
                break;
            }
        }
        for (const auto& l : lines) out += l + "\n";
        if (auto r = writeAll(trash / "corbeille.txt", out); !r) return r;
        macro::MacroFolders folders(macroFoldersFile());
        (void)folders.load();
        std::vector<std::pair<std::string, std::string>> known;
        for (const auto& i : items_)
            if (i.kind == LibraryItemKind::Macro)
                known.emplace_back(i.name, macro::parseMacroSpec(readAll(i.path), i.name).category);
        folders.setMacros(std::move(known));
        folders.place(it->name, it->folder);
        return folders.save();
    }

    core::Status SharedLibrary::purgeMacro(std::string_view name) {
        const auto trashed = trashedMacros();
        const auto it = std::find_if(trashed.begin(), trashed.end(), [&](const TrashedMacro& t) { return lowerName(t.name) == lowerName(name); });
        if (it == trashed.end()) return core::fail(core::ErrorCode::OutOfRange, std::string(name) + " n'est pas dans la corbeille");
        const auto trash = fs::path(macrosFolder()) / "_corbeille";
        std::error_code ec;
        fs::remove(trash / it->file, ec);
        const auto text = readAll(trash / "corbeille.txt");
        std::vector<std::string> lines;
        std::size_t from = 0;
        while (from < text.size()) {
            const auto nl = text.find('\n', from);
            lines.push_back(text.substr(from, (nl == std::string::npos ? text.size() : nl) - from));
            from = nl == std::string::npos ? text.size() : nl + 1;
        }
        std::string out;
        bool removed = false;
        for (auto l = lines.rbegin(); l != lines.rend(); ++l) {
            const auto f = split(*l, ';');
            if (!removed && f.size() >= 3 && f[0] == "supprimee" && f[1] == it->name && f[2] == it->file) {
                lines.erase(std::next(l).base());
                removed = true;
                break;
            }
        }
        // Une macro livree qu'on vide de la corbeille ne doit pas revenir pour autant.
        lines.push_back("renommee ; " + it->name + " ; ; " + nowText());
        for (const auto& l : lines) out += l + "\n";
        return writeAll(trash / "corbeille.txt", out);
    }

    std::string SharedLibrary::renameInHeader(std::string_view source, std::string_view from, std::string_view to) {
        std::string out(source);
        std::size_t i = 0;
        while (i < out.size() && std::isspace(static_cast<unsigned char>(out[i]))) ++i;
        if (out.compare(i, 2, "(*") != 0) return out;
        std::size_t j = i + 2;
        while (j < out.size() && (out[j] == ' ' || out[j] == '\t')) ++j;
        std::size_t k = j;
        while (k < out.size() && (std::isalnum(static_cast<unsigned char>(out[k])) || out[k] == '_')) ++k;
        if (k > j && lowerName(std::string_view(out).substr(j, k - j)) == lowerName(from)) out.replace(j, k - j, to);
        return out;
    }

    std::size_t SharedLibrary::replaceRunMacro(std::string& source, std::string_view from, std::string_view to) {
        std::size_t count = 0;
        const std::string wantedName = lowerName(from);
        std::size_t i = 0;
        while (i < source.size()) {
            // Les commentaires et les chaines hors appel ne sont pas touches.
            if (source.compare(i, 2, "(*") == 0) {
                const auto end = source.find("*)", i + 2);
                i = end == std::string::npos ? source.size() : end + 2;
                continue;
            }
            if (source[i] == '\'') {
                std::size_t j = i + 1;
                while (j < source.size() && source[j] != '\'') j += source[j] == '$' ? 2 : 1;
                i = j + 1;
                continue;
            }
            if (lowerName(std::string_view(source).substr(i, 8)) == "runmacro"
                && (i == 0 || !(std::isalnum(static_cast<unsigned char>(source[i - 1])) || source[i - 1] == '_'))) {
                std::size_t p = i + 8;
                while (p < source.size() && std::isspace(static_cast<unsigned char>(source[p]))) ++p;
                if (p < source.size() && source[p] == '(') {
                    ++p;
                    while (p < source.size() && std::isspace(static_cast<unsigned char>(source[p]))) ++p;
                    if (p < source.size() && source[p] == '\'') {
                        const auto close = source.find('\'', p + 1);
                        if (close != std::string::npos && lowerName(std::string_view(source).substr(p + 1, close - p - 1)) == wantedName) {
                            source.replace(p + 1, close - p - 1, to);
                            ++count;
                            i = p + 1 + to.size() + 1;
                            continue;
                        }
                    }
                }
                i += 8;
                continue;
            }
            ++i;
        }
        return count;
    }

    core::Result<std::vector<std::string>> SharedLibrary::renameMacro(std::string_view from, std::string_view to) {
        const auto check = macro::checkMacroName(to);
        if (check.verdict == macro::Verdict::Error)
            return core::fail(core::ErrorCode::InvalidArgument, std::string(to) + " : " + check.message);
        const LibraryItem* item = nullptr;
        for (const auto& i : items_)
            if (i.kind == LibraryItemKind::Macro && lowerName(i.name) == lowerName(from)) item = &i;
        if (!item) return core::fail(core::ErrorCode::OutOfRange, "aucune macro " + std::string(from));
        const std::string oldName = item->name;
        if (oldName == to) return std::vector<std::string>{};
        for (const auto& i : items_)
            if (lowerName(i.name) == lowerName(to) && lowerName(i.name) != lowerName(oldName))
                return core::fail(core::ErrorCode::DuplicateSymbol, "le nom " + std::string(to) + " est deja pris");
        const auto source = readAll(item->path);
        if (source.empty()) return core::fail(core::ErrorCode::FileUnreadable, "macro illisible : " + item->path);
        const auto newPath = fs::path(macrosFolder()) / (std::string(to) + ".mac");
        if (auto r = writeAll(newPath, renameInHeader(source, oldName, to)); !r) return core::Err<core::Error>(r.error());
        std::error_code ec;
        if (fs::path(item->path) != newPath) fs::remove(item->path, ec);

        auto updated = items_;
        for (auto& i : updated)
            if (i.kind == LibraryItemKind::Macro && i.name == oldName) {
                i.name = std::string(to);
                i.path = newPath.string();
            }
        if (auto r = rewriteIndex(root_, updated); !r) return core::Err<core::Error>(r.error());
        items_ = std::move(updated);

        // Les appelants.
        std::vector<std::string> callers;
        for (const auto& i : items_) {
            if (i.kind != LibraryItemKind::Macro || i.name == to) continue;
            auto text = readAll(i.path);
            if (replaceRunMacro(text, oldName, to) > 0 && writeAll(i.path, text)) callers.push_back(i.name);
        }

        macro::MacroFolders folders(macroFoldersFile());
        (void)folders.load();
        folders.renameMacro(oldName, std::string(to));
        if (auto r = folders.save(); !r) return core::Err<core::Error>(r.error());
        const auto trash = fs::path(macrosFolder()) / "_corbeille" / "corbeille.txt";
        auto text = readAll(trash);
        if (text.empty()) text = kTrashHeader;
        text += "renommee ; " + oldName + " ; " + std::string(to) + " ; " + nowText() + "\n";
        (void)writeAll(trash, text);
        return callers;
    }

    core::Status SharedLibrary::duplicateMacro(std::string_view from, std::string_view to) {
        const LibraryItem* item = nullptr;
        for (const auto& i : items_)
            if (i.kind == LibraryItemKind::Macro && lowerName(i.name) == lowerName(from)) item = &i;
        if (!item) return core::fail(core::ErrorCode::OutOfRange, "aucune macro " + std::string(from));
        const auto source = readAll(item->path);
        const auto folder = macroFolderOf(item->name);
        return createMacro(to, renameInHeader(source, item->name, to), folder);
    }

} // namespace project
