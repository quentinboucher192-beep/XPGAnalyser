// project/RenameCommands.cpp - renommer, et reecrire ce qui en depend (lot API 5).
#include "RenameCommands.hpp"

#include "CodeIconKeys.hpp"

#include "ApiCommands.hpp"
#include "../import/ProjectParser.hpp"

#include <algorithm>
#include <cctype>

namespace project {

using namespace domain;

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
bool same(std::string_view a, std::string_view b) { return lower(a) == lower(b); }
bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Le mot `from` remplace par `to` partout dans un texte de type
// (« ARRAY[0..1] OF armoire »), mot entier, sans la casse.
std::string replaceWord(const std::string& text, std::string_view from, std::string_view to, bool& changed) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (isIdentStart(text[i])) {
            std::size_t j = i + 1;
            while (j < text.size() && isIdentChar(text[j])) ++j;
            const auto word = std::string_view(text).substr(i, j - i);
            if (same(word, from)) {
                out += to;
                changed = true;
            } else {
                out += word;
            }
            i = j;
            continue;
        }
        out += text[i++];
    }
    return out;
}

// Une ligne de table d'animation (« armoires[0].sorties.V3 ») : la racine, ou
// les membres de ce nom, renommes.
std::string renamePath(std::string_view path, std::string_view from, std::string_view to, bool member) {
    std::string out;
    std::size_t i = 0;
    bool first = true;
    while (i <= path.size()) {
        auto end = path.find('.', i);
        if (end == std::string_view::npos) end = path.size();
        auto part = path.substr(i, end - i);
        const auto bracket = part.find('[');
        const auto name = part.substr(0, bracket);
        const auto rest = bracket == std::string_view::npos ? std::string_view{} : part.substr(bracket);
        if (!first) out += '.';
        if ((member ? !first : first) && same(name, from)) {
            out += to;
            out += rest;
        } else {
            out += part;
        }
        first = false;
        if (end == path.size()) break;
        i = end + 1;
    }
    return out;
}

const char* whatLabel(RenameCommand::What w) {
    switch (w) {
        case RenameCommand::What::Variable:    return "la variable";
        case RenameCommand::What::Field:       return "le champ";
        case RenameCommand::What::DerivedType: return "le type";
        case RenameCommand::What::Block:       return "le bloc DFB";
        case RenameCommand::What::Unit:        return "l'unit\xC3\xA9";
        default:                               return "la section";
    }
}

std::string currentName(const Project& p, RenameCommand::What w, Index i) {
    switch (w) {
        case RenameCommand::What::Variable:
        case RenameCommand::What::Field:       return i < p.variables.size() ? std::string(p.strings.text(p.variables[i].name)) : std::string{};
        case RenameCommand::What::DerivedType: return i < p.derivedTypes.size() ? std::string(p.strings.text(p.derivedTypes[i].name)) : std::string{};
        case RenameCommand::What::Block:
        case RenameCommand::What::Unit:        return i < p.pous.size() ? std::string(p.strings.text(p.pous[i].name)) : std::string{};
        default:                               return i < p.sections.size() ? std::string(p.strings.text(p.sections[i].name)) : std::string{};
    }
}

std::string usageOwner(const Project& p, const Variable& v) {
    if (v.scope == VariableScope::DerivedMember && v.owner < p.derivedTypes.size()) return std::string(p.strings.text(p.derivedTypes[v.owner].name));
    if (v.owner < p.pous.size()) return std::string(p.strings.text(p.pous[v.owner].name));
    return "un autre type";
}

bool elementaryName(std::string_view n) {
    static const char* const k[] = {"BOOL", "EBOOL", "INT", "UINT", "DINT", "UDINT", "WORD", "DWORD", "BYTE", "REAL", "LREAL",
                                    "TIME", "DATE", "TOD", "DT", "STRING", "SINT", "USINT", "LINT", "ULINT", "ARRAY", "OF"};
    return std::any_of(std::begin(k), std::end(k), [&](const char* e) { return same(n, e); });
}

} // namespace

std::size_t renameInCode(std::string& code, std::string_view from, std::string_view to, bool member) {
    std::string out;
    out.reserve(code.size());
    std::size_t count = 0, i = 0;
    const auto n = code.size();
    char prev = 0;
    while (i < n) {
        const char c = code[i];
        if (c == '(' && i + 1 < n && code[i + 1] == '*') {
            const auto end = code.find("*)", i + 2);
            const auto stop = end == std::string::npos ? n : end + 2;
            out.append(code, i, stop - i);
            i = stop;
            continue;
        }
        if (c == '/' && i + 1 < n && code[i + 1] == '/') {
            const auto end = code.find('\n', i);
            const auto stop = end == std::string::npos ? n : end;
            out.append(code, i, stop - i);
            i = stop;
            continue;
        }
        if (c == '\'' || c == '"') {
            const auto end = code.find(c, i + 1);
            const auto stop = end == std::string::npos ? n : end + 1;
            out.append(code, i, stop - i);
            i = stop;
            prev = c;
            continue;
        }
        if (isIdentStart(c)) {
            std::size_t j = i + 1;
            while (j < n && isIdentChar(code[j])) ++j;
            const auto word = std::string_view(code).substr(i, j - i);
            const bool afterDot = prev == '.';
            if (afterDot == member && same(word, from)) {
                out += to;
                ++count;
            } else {
                out += word;
            }
            prev = 'a';
            i = j;
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(c))) prev = c;
        out += c;
        ++i;
    }
    if (count) code = std::move(out);
    return count;
}

std::string renameProblem(const Project& p, RenameCommand::What what, Index index, std::string_view newName) {
    using W = RenameCommand::What;
    const auto old = currentName(p, what, index);
    if (old.empty()) return "introuvable";
    if (auto why = identifierProblem(newName); !why.empty()) return why;
    if (old == newName) return "c'est d\xC3\xA9j\xC3\xA0 son nom";
    const bool caseOnly = same(old, newName);
    switch (what) {
        case W::Variable: {
            const auto& v = p.variables[index];
            for (Index i = 0; i < p.variables.size(); ++i) {
                if (i == index || caseOnly) continue;
                const auto& o = p.variables[i];
                if (!same(p.strings.text(o.name), newName)) continue;
                if (v.scope == VariableScope::Global && o.scope == VariableScope::Global) return "une variable globale porte d\xC3\xA9j\xC3\xA0 ce nom";
                if (v.scope != VariableScope::Global && o.owner == v.owner && o.scope != VariableScope::DerivedMember)
                    return "une variable de la m\xC3\xAAme unit\xC3\xA9 porte d\xC3\xA9j\xC3\xA0 ce nom";
            }
            for (const auto& dt : p.derivedTypes) if (same(p.strings.text(dt.name), newName)) return "c'est le nom d'un type d\xC3\xA9riv\xC3\xA9";
            for (const auto& pou : p.pous) if (same(p.strings.text(pou.name), newName)) return "c'est le nom d'un bloc ou d'une unit\xC3\xA9";
            return {};
        }
        case W::Field: {
            const auto& v = p.variables[index];
            if (v.scope != VariableScope::DerivedMember || v.owner >= p.derivedTypes.size()) return "ce n'est pas un champ de type d\xC3\xA9riv\xC3\xA9";
            for (const auto f : p.derivedTypes[v.owner].fields)
                if (f != index && f < p.variables.size() && same(p.strings.text(p.variables[f].name), newName) && !caseOnly)
                    return "un autre champ du type porte d\xC3\xA9j\xC3\xA0 ce nom";
            // Le texte « x.champ » ne dit pas quel type est vise : un membre du
            // meme nom ailleurs rendrait la reecriture fausse.
            for (Index i = 0; i < p.variables.size(); ++i) {
                if (i == index) continue;
                const auto& o = p.variables[i];
                const bool member = o.scope == VariableScope::DerivedMember || o.scope == VariableScope::Input || o.scope == VariableScope::Output
                                 || o.scope == VariableScope::InOut || o.scope == VariableScope::Public;
                if (member && same(p.strings.text(o.name), old))
                    return "\xC2\xAB " + old + " \xC2\xBB est aussi un membre de " + usageOwner(p, o) + " : le code ne dit pas lequel est vis\xC3\xA9, renomme \xC3\xA0 la main";
            }
            return {};
        }
        case W::DerivedType:
        case W::Block:
        case W::Unit: {
            if (elementaryName(newName)) return "c'est le nom d'un type \xC3\xA9l\xC3\xA9mentaire";
            if (caseOnly) return {};
            for (const auto& dt : p.derivedTypes) if (same(p.strings.text(dt.name), newName)) return "un type d\xC3\xA9riv\xC3\xA9 porte d\xC3\xA9j\xC3\xA0 ce nom";
            for (const auto& pou : p.pous) if (same(p.strings.text(pou.name), newName)) return "un bloc ou une unit\xC3\xA9 porte d\xC3\xA9j\xC3\xA0 ce nom";
            for (const auto& v : p.variables)
                if (v.scope == VariableScope::Global && same(p.strings.text(v.name), newName)) return "une variable globale porte d\xC3\xA9j\xC3\xA0 ce nom";
            return {};
        }
        default: {
            if (caseOnly) return {};
            for (const auto& s : p.sections) if (same(p.strings.text(s.name), newName)) return "une section porte d\xC3\xA9j\xC3\xA0 ce nom";
            return {};
        }
    }
}

RenameCommand::RenameCommand(std::shared_ptr<Project> p, What what, Index index, std::string newName)
    : project_(std::move(p)), what_(what), index_(index), newName_(std::move(newName)) {}

core::Status RenameCommand::execute() {
    if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "pas de projet");
    auto& p = *project_;
    if (auto why = renameProblem(p, what_, index_, newName_); !why.empty())
        return core::fail(core::ErrorCode::InvalidArgument, why);
    oldName_ = currentName(p, what_, index_);
    before_ = p;
    sections_ = 0;
    // 1.8.0 : l'icone choisie suit le nouveau nom (Ctrl+Z rend le projet d'avant, icones comprises).
    const std::string iconKeyBefore = what_ == What::Section ? codeicons::keyForSection(p, index_) : std::string{};
    const auto rewrite = [&](Index s, bool member) {
        if (s < p.sections.size() && renameInCode(p.sections[s].body, oldName_, newName_, member) > 0) ++sections_;
    };
    const auto renameTableLines = [&](bool member) {
        for (auto& t : p.animationTables)
            for (auto& e : t.entries) {
                if (e.hmi) continue;
                const std::string path(p.strings.text(e.name));
                const auto next = renamePath(path, oldName_, newName_, member);
                if (next != path) e.name = p.strings.intern(next);
            }
    };
    // Le type des variables : le texte reecrit, puis reclasse et resolu comme a la creation.
    const auto retype = [&] {
        p.buildIndices();
        for (auto& v : p.variables) {
            bool changed = false;
            const auto text = replaceWord(std::string(p.strings.text(v.type.name)), oldName_, newName_, changed);
            if (!changed) continue;
            v.type = importer::classifyTypeName(text, p.strings);
            const auto element = v.type.elementType ? v.type.elementType : v.type.name;
            if (const auto it = p.typeByName.find(element); it != p.typeByName.end()) {
                v.type.derivedIndex = it->second;
                if (v.type.klass != TypeClass::Array) v.type.klass = TypeClass::Derived;
            } else if (const auto ip = p.pouByName.find(element); ip != p.pouByName.end() && p.pous[ip->second].kind == PouKind::FunctionBlockType) {
                v.type.fbTypeIndex = ip->second;
                v.type.klass = TypeClass::FunctionBlock;
            }
        }
    };
    switch (what_) {
        case What::Variable: {
            auto& v = p.variables[index_];
            v.name = p.strings.intern(newName_);
            if (v.scope == VariableScope::Global) {
                for (Index s = 0; s < p.sections.size(); ++s) rewrite(s, false);
                renameTableLines(false);
                // Les parametres des unites qui la recoivent (« EffectiveParameter »).
                for (auto& other : p.variables)
                    for (auto& [key, value] : other.attributes)
                        if (key == "EffectiveParameter") (void)renameInCode(value, oldName_, newName_, false);
            } else if (v.owner < p.pous.size()) {
                for (const auto s : p.pous[v.owner].sections) rewrite(s, false);
                renameTableLines(false);
            }
            break;
        }
        case What::Field:
            p.variables[index_].name = p.strings.intern(newName_);
            for (Index s = 0; s < p.sections.size(); ++s) rewrite(s, true);
            renameTableLines(true);
            for (auto& other : p.variables)
                for (auto& [key, value] : other.attributes)
                    if (key == "EffectiveParameter") (void)renameInCode(value, oldName_, newName_, true);
            break;
        case What::DerivedType:
            p.derivedTypes[index_].name = p.strings.intern(newName_);
            retype();
            break;
        case What::Block: {
            const auto oldSymbol = p.pous[index_].name;
            p.pous[index_].name = p.strings.intern(newName_);
            for (auto& e : p.libraries)
                if (e.name == oldSymbol) e.name = p.pous[index_].name;
            retype();
            break;
        }
        case What::Unit: {
            const auto oldSymbol = p.pous[index_].name;
            p.pous[index_].name = p.strings.intern(newName_);
            for (auto& t : p.animationTables)
                if (t.owner == oldSymbol) t.owner = p.pous[index_].name;
            break;
        }
        default:
            p.sections[index_].name = p.strings.intern(newName_);
            // Les appels d'une sous-routine (« SR_Nom(); ») : le nom en racine.
            for (Index s = 0; s < p.sections.size(); ++s)
                if (s != index_) rewrite(s, false);
            break;
    }
    switch (what_) {
        case What::DerivedType: codeicons::renameKey(p, "ddt:" + oldName_, "ddt:" + newName_); break;
        case What::Block:
            codeicons::renameKey(p, "dfb:" + oldName_, "dfb:" + newName_);
            codeicons::renamePrefix(p, "dfbsection:" + oldName_ + "/", "dfbsection:" + newName_ + "/");
            break;
        case What::Unit:
            codeicons::renameKey(p, "unit:" + oldName_, "unit:" + newName_);
            codeicons::renamePrefix(p, "section:" + oldName_ + "/", "section:" + newName_ + "/");
            break;
        case What::Section: codeicons::renameKey(p, iconKeyBefore, codeicons::keyForSection(p, index_)); break;
        default: break;
    }
    p.buildIndices();
    return core::ok();
}

core::Status RenameCommand::undo() {
    if (!project_ || !before_) return core::ok();
    *project_ = std::move(*before_);
    before_.reset();
    return core::ok();
}

std::string RenameCommand::label() const {
    return std::string("Renommer ") + whatLabel(what_) + " " + (oldName_.empty() ? std::string("?") : oldName_) + " en " + newName_;
}

} // namespace project
