// project/RenamePlan.cpp - renommer en voyant tout ce qui suit (voir RenamePlan.hpp).
#include "RenamePlan.hpp"

#include "ApiCommands.hpp"           // identifierProblem, animationTableNameProblem
#include "BlockLibrary.hpp"          // les blocs standard : leurs noms sont pris
#include "MemberTree.hpp"            // lot API 8 : parseArray (l'element d'un tableau)
#include "RenameCommands.hpp"        // renameInCode : la meme reecriture que Renommer d'avant
#include "../domain/ExecutionOrder.hpp"
#include "../import/ProjectParser.hpp"   // classifyTypeName

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace project::rename {

using namespace domain;

namespace {

unsigned char uc(char c) { return static_cast<unsigned char>(c); }
bool identStart(char c) { return std::isalpha(uc(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(uc(c)) || c == '_'; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(uc(c)));
    return out;
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(uc(s[a]))) ++a;
    while (b > a && std::isspace(uc(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string text(const Project& p, SymbolId id) { return std::string(p.strings.text(id)); }

struct KindInfo {
    Kind             kind;
    std::string_view key, label;
};
constexpr KindInfo kKinds[] = {
    {Kind::Variable,    "variable",     "la variable"},
    {Kind::Ddt,         "ddt",          "le type d\xC3\xA9riv\xC3\xA9"},
    {Kind::Dfb,         "dfb",          "le bloc DFB"},
    {Kind::Unit,        "unite",        "l'unit\xC3\xA9"},
    {Kind::Section,     "section",      "la section"},
    {Kind::Table,       "table",        "la table d'animation"},
    {Kind::HmiVariable, "ihm-variable", "la variable IHM"},
    {Kind::HmiView,     "ihm-vue",      "la vue"},
    {Kind::DdtField,    "ddt-champ",    "le champ"},        // lot API 8
};

// ---------------------------------------------------------------- les textes -
// Le mot `from` remplace par `to` dans un texte de type ("ARRAY[0..1] OF
// armoire"), mot entier, sans la casse - comme RenameCommand.
std::string replaceWord(std::string_view text, std::string_view from, std::string_view to, bool& changed) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (identStart(text[i])) {
            std::size_t j = i + 1;
            while (j < text.size() && identChar(text[j])) ++j;
            const auto word = text.substr(i, j - i);
            if (sameName(word, from)) {
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

// Une ligne de table d'animation ("armoires[0].sorties.V3") : sa racine
// renommee (`member` : ses membres de ce nom).
std::string renamePath(std::string_view path, std::string_view from, std::string_view to, bool member) {
    std::string out;
    std::size_t i = 0;
    bool first = true;
    while (i <= path.size()) {
        auto end = path.find('.', i);
        if (end == std::string_view::npos) end = path.size();
        const auto part = path.substr(i, end - i);
        const auto bracket = part.find('[');
        const auto name = part.substr(0, bracket);
        const auto rest = bracket == std::string_view::npos ? std::string_view{} : part.substr(bracket);
        if (!first) out += '.';
        if ((member ? !first : first) && sameName(trimmed(name), from)) {
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

// Les lignes d'un texte, sans le \r d'une fin de ligne CRLF.
std::vector<std::string_view> splitLines(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        auto nl = s.find('\n', start);
        if (nl == std::string_view::npos) nl = s.size();
        auto line = s.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.push_back(line);
        if (nl == s.size()) break;
        start = nl + 1;
    }
    return out;
}

// ------------------------------------------------ ce que le programme change -
//  Une modification = un champ du projet et sa nouvelle valeur. Le plan les
//  calcule sans rien toucher (pour les montrer) ; la commande les calcule de
//  nouveau sur le projet du moment, puis les pose : ce qu'on a vu est ce qui
//  est fait, par le meme code.
struct Edit {
    enum class What : std::uint8_t {
        SectionBody, VariableName, VariableType, Attribute, TypeName, PouName, LibraryName,
        SectionName, TableName, TableOwner, TableEntry,
        InstanceElement,     // lot API 8 : <instanceElementDesc name="Nom_gaz"> d'une variable de ce type
    };
    What        what{What::SectionBody};
    Index       a{kNoIndex}, b{kNoIndex};
    std::string value;
};

class Recorder {
public:
    Recorder(std::vector<Change>* changes, std::vector<Edit>* edits) : changes_(changes), edits_(edits) {}
    void edit(Edit::What what, std::size_t a, std::size_t b, std::string value) {
        if (edits_) edits_->push_back({what, static_cast<Index>(a), static_cast<Index>(b), std::move(value)});
    }
    void change(Tab tab, std::vector<std::string> path, std::string label, std::string before, std::string after,
                int line = 0, bool info = false, bool code = false) {
        if (!changes_) return;
        Change c;
        c.tab = tab;
        c.path = std::move(path);
        c.label = std::move(label);
        c.before = std::move(before);
        c.after = std::move(after);
        c.line = line;
        c.info = info;
        c.code = code;
        changes_->push_back(std::move(c));
    }
    // Les lignes qui different entre deux versions d'un code : renommer
    // n'ajoute ni ne retire de ligne, elles se comparent une a une.
    void codeLines(Tab tab, const std::vector<std::string>& path, std::string_view before, std::string_view after) {
        if (!changes_) return;
        const auto a = splitLines(before), b = splitLines(after);
        if (a.size() != b.size()) {
            change(tab, path, "le texte", std::string(before), std::string(after), 0, false, true);
            return;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
                change(tab, path, "ligne " + std::to_string(i + 1), std::string(a[i]), std::string(b[i]), static_cast<int>(i + 1), false, true);
    }
    // Lot API 8 : un acces qui ne dit pas son type - montre (rien n'y est ecrit),
    // et retenu : le plan est refuse tant qu'il y en a (`where` : pour le dire).
    void doubt(Tab tab, std::vector<std::string> path, std::string label, std::string text, int line, std::string where) {
        doubts.push_back(std::move(where));
        if (!changes_) return;
        Change c;
        c.tab = tab;
        c.path = std::move(path);
        c.label = std::move(label);
        c.before = text;
        c.after = std::move(text);
        c.line = line;
        c.info = true;
        c.code = true;
        c.doubt = true;
        changes_->push_back(std::move(c));
    }
    std::vector<std::string> doubts;
private:
    std::vector<Change>* changes_;
    std::vector<Edit>*   edits_;
};

const std::string kDeclaration = "D\xC3\xA9" "claration";
const std::string kCode = "Code";
const std::string kOrder = "Ordre d'ex\xC3\xA9" "cution";

std::string scopeLabel(VariableScope s) {
    switch (s) {
        case VariableScope::Global:        return "variable globale";
        case VariableScope::Local:         return "variable priv\xC3\xA9" "e";
        case VariableScope::Public:        return "variable publique";
        case VariableScope::Input:         return "entr\xC3\xA9" "e";
        case VariableScope::Output:        return "sortie";
        case VariableScope::InOut:         return "entr\xC3\xA9" "e/sortie";
        case VariableScope::Constant:      return "constante";
        case VariableScope::DerivedMember: return "champ";
    }
    return "variable";
}

// Les sections d'un POU : celles qu'il liste, et celles qui le disent leur.
std::set<Index> sectionsOf(const Project& p, Index pou) {
    std::set<Index> out;
    if (pou < p.pous.size())
        for (const auto s : p.pous[pou].sections)
            if (s < p.sections.size()) out.insert(s);
    for (Index s = 0; s < p.sections.size(); ++s)
        if (p.sections[s].owner == pou) out.insert(s);
    return out;
}

// Le code de ce POU nomme-t-il `name` (en racine, hors commentaires et chaines) ?
bool pouCites(const Project& p, Index pou, std::string_view name) {
    for (const auto s : sectionsOf(p, pou)) {
        std::string body = p.sections[s].body;
        if (renameInCode(body, name, name, false) > 0) return true;
    }
    return false;
}

// Le nom d'une section tel que les onglets le montrent : "Bloc.Section" pour
// le corps d'un DFB (comme les diagnostics du simulateur), "Unite > Section"
// quand l'unite porte un autre nom que la section.
std::string sectionTitle(const Project& p, Index s) {
    const auto& sec = p.sections[s];
    const std::string name = text(p, sec.name);
    if (sec.owner < p.pous.size()) {
        const auto& pou = p.pous[sec.owner];
        const std::string owner = text(p, pou.name);
        if (pou.kind == PouKind::FunctionBlockType) return owner + "." + name;
        if (pou.kind == PouKind::ProgramUnit && !sameName(owner, name)) return owner + " \xE2\x80\xBA " + name;
    }
    return name;
}

std::string declText(const Project& p, const Variable& v, std::string_view name) {
    std::string s(name);
    s += " : " + text(p, v.type.name);
    if (!v.address.raw.empty()) s += " AT " + v.address.raw;
    return s;
}

// Ou vit une variable, pour ranger ses lignes : les globales ensemble, les
// champs sous leur type, les autres sous leur unite ou leur bloc.
std::string ownerGroup(const Project& p, const Variable& v) {
    if (v.scope == VariableScope::Global) return "Variables globales";
    if (v.scope == VariableScope::DerivedMember)
        return v.owner < p.derivedTypes.size() ? "Champs de " + text(p, p.derivedTypes[v.owner].name) : std::string("Champs");
    if (v.owner < p.pous.size()) {
        const auto& pou = p.pous[v.owner];
        return (pou.kind == PouKind::FunctionBlockType ? "Bloc " : "Unit\xC3\xA9 ") + text(p, pou.name);
    }
    return "Autres variables";
}

// Les unites (et les blocs) qui declarent une variable de ce nom : chez
// elles, le nom est le leur - leur code ne parle pas de la globale.
std::set<Index> shadowingPous(const Project& p, std::string_view name, Index except) {
    std::set<Index> out;
    for (Index i = 0; i < p.variables.size(); ++i) {
        const auto& v = p.variables[i];
        if (i == except || v.scope == VariableScope::Global || v.scope == VariableScope::DerivedMember) continue;
        if (v.owner < p.pous.size() && sameName(p.strings.text(v.name), name)) out.insert(v.owner);
    }
    return out;
}

// L'unite qui porte une table d'animation (nul : aucune).
Index tableUnit(const Project& p, const AnimationTable& t) {
    if (t.owner == 0) return kNoIndex;
    const auto owner = p.strings.text(t.owner);
    for (Index i = 0; i < p.pous.size(); ++i)
        if (sameName(p.strings.text(p.pous[i].name), owner)) return i;
    return kNoIndex;
}

// Les rangs d'une unite (ou d'une section) dans l'ordre de chaque tache : ce
// n'est pas reecrit (l'ordre tient par indices), mais c'est la qu'on la
// retrouvera sous son nouveau nom.
void orderRows(const Project& p, Recorder& rec, bool unit, Index index, const std::string& from, const std::string& to) {
    for (const auto& task : p.tasks) {
        const auto entries = executionEntries(p, task.name);
        for (std::size_t k = 0; k < entries.size(); ++k) {
            const auto& e = entries[k];
            const bool hit = unit ? (e.unit && e.pou == index)
                                  : std::find(e.sections.begin(), e.sections.end(), index) != e.sections.end() || e.section == index;
            if (!hit) continue;
            rec.change(Tab::Api, {kOrder, text(p, task.name)}, "rang " + std::to_string(k + 1), from, to, 0, true);
        }
    }
}

// ------------------------------------------------------ genre par genre -----
void planVariable(const Project& p, const Rename& r, Recorder& rec) {
    const auto& t = r.target;
    if (t.index >= p.variables.size()) return;
    const auto& v = p.variables[t.index];
    const std::string& from = t.name;
    const std::string& to = r.to;
    rec.edit(Edit::What::VariableName, t.index, kNoIndex, to);
    const std::string where = t.global || t.unit >= p.pous.size() ? scopeLabel(v.scope) : scopeLabel(v.scope) + " de " + text(p, p.pous[t.unit].name);
    rec.change(Tab::Api, {kDeclaration}, where, declText(p, v, from), declText(p, v, to));

    // Le code : toutes les sections pour une globale (sauf chez les unites qui
    // ont une variable a elles de ce nom), celles de son unite sinon.
    const std::set<Index> hidden = t.global ? shadowingPous(p, from, t.index) : std::set<Index>{};
    const std::set<Index> own = t.global ? std::set<Index>{} : sectionsOf(p, t.unit);
    for (Index s = 0; s < p.sections.size(); ++s) {
        const auto& sec = p.sections[s];
        if (t.global ? (sec.owner != kNoIndex && hidden.count(sec.owner) != 0) : own.count(s) == 0) continue;
        std::string body = sec.body;
        if (renameInCode(body, from, to, false) == 0 || body == sec.body) continue;
        rec.codeLines(Tab::Api, {kCode, sectionTitle(p, s)}, sec.body, body);
        rec.edit(Edit::What::SectionBody, s, kNoIndex, std::move(body));
    }

    // Les lignes des tables d'animation qui la nomment (une variable d'unite :
    // les tables de son unite).
    for (std::size_t ti = 0; ti < p.animationTables.size(); ++ti) {
        const auto& table = p.animationTables[ti];
        const Index unit = tableUnit(p, table);
        if (t.global ? (unit != kNoIndex && hidden.count(unit)) : unit != t.unit) continue;
        for (std::size_t ei = 0; ei < table.entries.size(); ++ei) {
            const auto& e = table.entries[ei];
            if (e.hmi) continue;
            const std::string path = text(p, e.name);
            std::string next = renamePath(path, from, to, false);
            if (next == path) continue;
            rec.change(Tab::Tables, {text(p, table.name)}, "ligne " + std::to_string(ei + 1) + " (API)", path, next);
            rec.edit(Edit::What::TableEntry, ti, ei, std::move(next));
        }
    }

    // Les parametres des unites qui la recoivent (l'EffectiveParameter de l'export).
    if (!t.global) return;
    for (Index i = 0; i < p.variables.size(); ++i) {
        const auto& w = p.variables[i];
        for (std::size_t k = 0; k < w.attributes.size(); ++k) {
            if (w.attributes[k].first != "EffectiveParameter") continue;
            std::string value = w.attributes[k].second;
            if (renameInCode(value, from, to, false) == 0 || value == w.attributes[k].second) continue;
            const std::string owner = w.owner < p.pous.size() ? text(p, p.pous[w.owner].name) : std::string("?");
            rec.change(Tab::Api, {"Param\xC3\xA8tres re\xC3\xA7us", owner}, text(p, w.name) + " \xE2\x87\x84", w.attributes[k].second, value, 0, false, true);
            rec.edit(Edit::What::Attribute, i, k, std::move(value));
        }
    }
}

void planType(const Project& p, const Rename& r, bool block, Recorder& rec) {
    const auto& t = r.target;
    const std::string& from = t.name;
    const std::string& to = r.to;
    if (block) {
        if (t.index >= p.pous.size()) return;
        rec.edit(Edit::What::PouName, t.index, kNoIndex, to);
        rec.change(Tab::Api, {kDeclaration}, "bloc DFB", "FUNCTION_BLOCK " + from, "FUNCTION_BLOCK " + to);
        for (std::size_t i = 0; i < p.libraries.size(); ++i)
            if (sameName(p.strings.text(p.libraries[i].name), from)) {
                rec.change(Tab::Api, {"Biblioth\xC3\xA8que"}, "entr\xC3\xA9" "e", from, to);
                rec.edit(Edit::What::LibraryName, i, kNoIndex, to);
            }
    } else {
        if (t.index >= p.derivedTypes.size()) return;
        rec.edit(Edit::What::TypeName, t.index, kNoIndex, to);
        const auto fields = p.derivedTypes[t.index].fields.size();
        const std::string shape = " : STRUCT (" + std::to_string(fields) + (fields > 1 ? " champs)" : " champ)");
        rec.change(Tab::Api, {kDeclaration}, "type d\xC3\xA9riv\xC3\xA9", "TYPE " + from + shape, "TYPE " + to + shape);
    }
    // Le type des variables, des champs, des parametres : "ARRAY[0..1] OF x" compris.
    const std::string group = block ? "Instances" : "Variables de ce type";
    for (Index i = 0; i < p.variables.size(); ++i) {
        const auto& v = p.variables[i];
        bool changed = false;
        const std::string before = text(p, v.type.name);
        std::string next = replaceWord(before, from, to, changed);
        if (!changed) continue;
        const std::string name = text(p, v.name);
        rec.change(Tab::Api, {group, ownerGroup(p, v)}, name, name + " : " + before, name + " : " + next);
        rec.edit(Edit::What::VariableType, i, kNoIndex, std::move(next));
    }
}

void planUnit(const Project& p, const Rename& r, Recorder& rec) {
    const auto& t = r.target;
    if (t.index >= p.pous.size()) return;
    rec.edit(Edit::What::PouName, t.index, kNoIndex, r.to);
    const auto& pou = p.pous[t.index];
    const std::string shape = " (" + std::to_string(pou.sections.size()) + (pou.sections.size() > 1 ? " sections)" : " section)");
    rec.change(Tab::Api, {kDeclaration}, "unit\xC3\xA9 de programme", t.name + shape, r.to + shape);
    // Les tables qu'elle porte (l'export les range dans l'unite).
    for (std::size_t ti = 0; ti < p.animationTables.size(); ++ti) {
        const auto& table = p.animationTables[ti];
        if (table.owner == 0 || !sameName(p.strings.text(table.owner), t.name)) continue;
        rec.change(Tab::Tables, {text(p, table.name)}, "unit\xC3\xA9 qui la porte", t.name, r.to);
        rec.edit(Edit::What::TableOwner, ti, kNoIndex, r.to);
    }
    orderRows(p, rec, true, t.index, t.name, r.to);
}

void planSection(const Project& p, const Rename& r, Recorder& rec) {
    const auto& t = r.target;
    if (t.index >= p.sections.size()) return;
    const auto& sec = p.sections[t.index];
    rec.edit(Edit::What::SectionName, t.index, kNoIndex, r.to);
    const std::string what = sec.isSubroutine ? "sous-routine" : "section";
    const std::string lang(toString(sec.language));
    rec.change(Tab::Api, {kDeclaration}, what, t.name + " (" + lang + ")", r.to + " (" + lang + ")");
    // Ses appels ("SR_Nom();") : le nom en racine, dans les autres sections.
    for (Index s = 0; s < p.sections.size(); ++s) {
        if (s == t.index) continue;
        std::string body = p.sections[s].body;
        if (renameInCode(body, t.name, r.to, false) == 0 || body == p.sections[s].body) continue;
        rec.codeLines(Tab::Api, {"Appels", sectionTitle(p, s)}, p.sections[s].body, body);
        rec.edit(Edit::What::SectionBody, s, kNoIndex, std::move(body));
    }
    orderRows(p, rec, false, t.index, t.name, r.to);
}

void planTable(const Project& p, const Rename& r, Recorder& rec) {
    const auto& t = r.target;
    if (t.index >= p.animationTables.size()) return;
    rec.change(Tab::Tables, {"Tables d'animation"}, "nom", t.name, r.to);
    rec.edit(Edit::What::TableName, t.index, kNoIndex, r.to);
}

// Une variable IHM : les lignes IHM des tables d'animation qui la nomment.
void planHmiVariableTables(const Project& p, const Rename& r, Recorder& rec) {
    for (std::size_t ti = 0; ti < p.animationTables.size(); ++ti) {
        const auto& table = p.animationTables[ti];
        for (std::size_t ei = 0; ei < table.entries.size(); ++ei) {
            const auto& e = table.entries[ei];
            if (!e.hmi) continue;
            const std::string path = text(p, e.name);
            std::string next = renamePath(path, r.target.name, r.to, false);
            if (next == path) continue;
            rec.change(Tab::Tables, {text(p, table.name)}, "ligne " + std::to_string(ei + 1) + " (IHM)", path, next);
            rec.edit(Edit::What::TableEntry, ti, ei, std::move(next));
        }
    }
}

// ---- Lot API 8 : un champ de DDT ------------------------------------------------
//  Sa declaration ; chaque acces qui passe par ce type : le code (les racines
//  lues dans l'unite ou le bloc de la section, puis les globales), les lignes
//  API des tables d'animation, les parametres effectifs des unites, les
//  elements decrits des variables de ce type. Un acces qui ne dit pas son type
//  (voir rewriteFieldAccesses) est montre et retenu (Recorder::doubt).
std::string lineOf(std::string_view text, int line) {
    const auto lines = splitLines(text);
    return line >= 1 && static_cast<std::size_t>(line) <= lines.size() ? std::string(lines[static_cast<std::size_t>(line - 1)]) : std::string(text);
}

void planField(const Project& p, const Rename& r, Recorder& rec) {
    const auto& t = r.target;
    if (t.index >= p.variables.size() || t.type >= p.derivedTypes.size()) return;
    const auto& v = p.variables[t.index];
    const PlcTypes types(p);
    FieldAccess fa;
    fa.type = text(p, p.derivedTypes[t.type].name);
    fa.field = t.name;
    fa.to = r.to;
    fa.unique = types.owners(t.name).size() <= 1;
    rec.edit(Edit::What::VariableName, t.index, kNoIndex, r.to);
    rec.change(Tab::Api, {kDeclaration}, "champ de " + fa.type, declText(p, v, t.name), declText(p, v, r.to));
    // `code` : un texte a lignes (une section) - la ligne se dit ; sinon une expression.
    const auto doubts = [&](const std::vector<std::string>& path, std::string_view source, const FieldScan& scan, const std::string& where, bool code) {
        for (const int line : scan.ambiguous)
            rec.doubt(Tab::Api, path, code ? "ligne " + std::to_string(line) + " : le type ne se lit pas" : std::string("le type ne se lit pas"),
                      lineOf(source, line), code ? line : 0, where + (code ? ", ligne " + std::to_string(line) : std::string{}));
    };

    // Le code de chaque section.
    for (Index s = 0; s < p.sections.size(); ++s) {
        const auto& sec = p.sections[s];
        if (sec.body.empty()) continue;
        const Index owner = sec.owner;
        FieldScan scan;
        std::string body = rewriteFieldAccesses(sec.body, fa, types, [&](std::string_view name) { return types.root(name, owner); },
                                                Syntax::St, &scan);
        const std::vector<std::string> path{kCode, sectionTitle(p, s)};
        doubts(path, sec.body, scan, sectionTitle(p, s), true);
        if (body == sec.body) continue;
        rec.codeLines(Tab::Api, path, sec.body, body);
        rec.edit(Edit::What::SectionBody, s, kNoIndex, std::move(body));
    }

    // Les lignes API des tables d'animation (une table d'unite : ses variables d'abord).
    for (std::size_t ti = 0; ti < p.animationTables.size(); ++ti) {
        const auto& table = p.animationTables[ti];
        const Index unit = tableUnit(p, table);
        for (std::size_t ei = 0; ei < table.entries.size(); ++ei) {
            const auto& e = table.entries[ei];
            if (e.hmi) continue;
            const std::string path = text(p, e.name);
            if (path.find('.') == std::string::npos) continue;
            FieldScan scan;
            std::string next = rewriteFieldAccesses(path, fa, types, [&](std::string_view name) { return types.root(name, unit); }, Syntax::St, &scan);
            if (!scan.ambiguous.empty())
                rec.doubt(Tab::Tables, {text(p, table.name)}, "ligne " + std::to_string(ei + 1) + " : le type ne se lit pas", path, 0,
                          "table " + text(p, table.name) + ", ligne " + std::to_string(ei + 1));
            if (next == path) continue;
            rec.change(Tab::Tables, {text(p, table.name)}, "ligne " + std::to_string(ei + 1) + " (API)", path, next);
            rec.edit(Edit::What::TableEntry, ti, ei, std::move(next));
        }
    }

    // Les parametres effectifs (ce qu'une unite recoit : une expression sur les globales).
    for (Index i = 0; i < p.variables.size(); ++i) {
        const auto& w = p.variables[i];
        for (std::size_t k = 0; k < w.attributes.size(); ++k) {
            if (w.attributes[k].first != "EffectiveParameter" || w.attributes[k].second.find('.') == std::string::npos) continue;
            const std::string& value = w.attributes[k].second;
            FieldScan scan;
            std::string next = rewriteFieldAccesses(value, fa, types, [&](std::string_view name) { return types.root(name); }, Syntax::St, &scan);
            const std::string owner = w.owner < p.pous.size() ? text(p, p.pous[w.owner].name) : std::string("?");
            const std::vector<std::string> path{"Param\xC3\xA8tres re\xC3\xA7us", owner};
            doubts(path, value, scan, owner + " (" + text(p, w.name) + ")", false);
            if (next == value) continue;
            rec.change(Tab::Api, path, text(p, w.name) + " \xE2\x87\x84", value, next, 0, false, true);
            rec.edit(Edit::What::Attribute, i, k, std::move(next));
        }
    }

    // Les elements decrits d'une variable (<instanceElementDesc name="Nom_gaz">,
    // "[2].Nom_gaz") : un chemin sous elle, suivi depuis son type.
    for (Index i = 0; i < p.variables.size(); ++i) {
        const auto& w = p.variables[i];
        if (w.instanceElements.empty()) continue;
        const RootType own{RootIs::Plc, trimmed(text(p, w.type.name))};
        const std::string wname = text(p, w.name);
        for (std::size_t k = 0; k < w.instanceElements.size(); ++k) {
            const std::string& name = w.instanceElements[k].name;
            // "[0]" (sans point) ne passe par aucun champ ; "Nom_gaz" seul : le champ lui-meme.
            if (name.empty() || (name.find('.') == std::string::npos && !sameName(name, t.name))) continue;
            const bool bare = name.front() != '[' && name.front() != '.';
            const std::string full = "X" + std::string(bare ? "." : "") + name;     // X : la variable elle-meme
            const std::string next = rewriteFieldAccesses(full, fa, types, [&](std::string_view) { return own; }, Syntax::St, nullptr);
            if (next == full) continue;
            std::string after = next.substr(bare ? 2 : 1);
            rec.change(Tab::Api, {"Valeurs d\xC3\xA9" "crites", ownerGroup(p, w)}, wname, wname + (bare ? "." : "") + name,
                       wname + (bare ? "." : "") + after);
            rec.edit(Edit::What::InstanceElement, i, k, std::move(after));
        }
    }
}

void planDomain(const Project& p, const Rename& r, Recorder& rec) {
    switch (r.target.kind) {
        case Kind::Variable:    planVariable(p, r, rec); break;
        case Kind::Ddt:         planType(p, r, false, rec); break;
        case Kind::Dfb:         planType(p, r, true, rec); break;
        case Kind::Unit:        planUnit(p, r, rec); break;
        case Kind::Section:     planSection(p, r, rec); break;
        case Kind::Table:       planTable(p, r, rec); break;
        case Kind::HmiVariable: planHmiVariableTables(p, r, rec); break;
        case Kind::HmiView:     break;
        case Kind::DdtField:    planField(p, r, rec); break;      // lot API 8
    }
}

void applyEdits(Project& p, const std::vector<Edit>& edits) {
    bool retyped = false;
    for (const auto& e : edits) {
        switch (e.what) {
            case Edit::What::SectionBody:
                if (e.a < p.sections.size()) p.sections[e.a].body = e.value;
                break;
            case Edit::What::VariableName:
                if (e.a < p.variables.size()) p.variables[e.a].name = p.strings.intern(e.value);
                break;
            case Edit::What::VariableType:
                // Reclasse comme a la creation ; linkTypes le resout plus bas.
                if (e.a < p.variables.size()) {
                    p.variables[e.a].type = importer::classifyTypeName(e.value, p.strings);
                    retyped = true;
                }
                break;
            case Edit::What::Attribute:
                if (e.a < p.variables.size() && e.b < p.variables[e.a].attributes.size()) p.variables[e.a].attributes[e.b].second = e.value;
                break;
            case Edit::What::TypeName:
                if (e.a < p.derivedTypes.size()) p.derivedTypes[e.a].name = p.strings.intern(e.value);
                break;
            case Edit::What::PouName:
                if (e.a < p.pous.size()) p.pous[e.a].name = p.strings.intern(e.value);
                break;
            case Edit::What::LibraryName:
                if (e.a < p.libraries.size()) p.libraries[e.a].name = p.strings.intern(e.value);
                break;
            case Edit::What::SectionName:
                if (e.a < p.sections.size()) p.sections[e.a].name = p.strings.intern(e.value);
                break;
            case Edit::What::TableName:
                if (e.a < p.animationTables.size()) p.animationTables[e.a].name = p.strings.intern(e.value);
                break;
            case Edit::What::TableOwner:
                if (e.a < p.animationTables.size()) p.animationTables[e.a].owner = p.strings.intern(e.value);
                break;
            case Edit::What::TableEntry:
                if (e.a < p.animationTables.size() && e.b < p.animationTables[e.a].entries.size())
                    p.animationTables[e.a].entries[e.b].name = p.strings.intern(e.value);
                break;
            case Edit::What::InstanceElement:     // lot API 8
                if (e.a < p.variables.size() && e.b < p.variables[e.a].instanceElements.size())
                    p.variables[e.a].instanceElements[e.b].name = e.value;
                break;
        }
    }
    // Les index par nom (et, pour un type renomme, les instances resolues).
    if (retyped) p.linkTypes();
    else p.buildIndices();
}

// --------------------------------------------------------------- les noms ----
constexpr std::string_view kReserved[] = {
    "IF", "THEN", "ELSIF", "ELSE", "END_IF", "CASE", "OF", "END_CASE", "FOR", "TO", "BY", "DO", "END_FOR",
    "WHILE", "END_WHILE", "REPEAT", "UNTIL", "END_REPEAT", "EXIT", "RETURN", "AND", "OR", "XOR", "NOT", "MOD",
    "TRUE", "FALSE", "VAR", "VAR_INPUT", "VAR_OUTPUT", "VAR_IN_OUT", "VAR_TEMP", "VAR_GLOBAL", "VAR_EXTERNAL",
    "END_VAR", "CONSTANT", "RETAIN", "AT", "FUNCTION", "END_FUNCTION", "FUNCTION_BLOCK", "END_FUNCTION_BLOCK",
    "PROGRAM", "END_PROGRAM", "TYPE", "END_TYPE", "STRUCT", "END_STRUCT", "ARRAY", "BOOL", "EBOOL", "BYTE",
    "WORD", "DWORD", "LWORD", "SINT", "INT", "DINT", "LINT", "USINT", "UINT", "UDINT", "ULINT", "REAL", "LREAL",
    "TIME", "DATE", "TOD", "DT", "TIME_OF_DAY", "DATE_AND_TIME", "STRING", "WSTRING", "ANY", "EN", "ENO",
    "R_EDGE", "F_EDGE", "JMP", "RET",
};

const HmiNames::Variable* hmiVariable(const HmiNames& n, std::string_view name) {
    for (const auto& v : n.variables)
        if (sameName(v.name, name)) return &v;
    return nullptr;
}
const HmiNames::View* hmiView(const HmiNames& n, std::string_view name) {
    for (const auto& v : n.views)
        if (v.name == name) return &v;
    for (const auto& v : n.views)
        if (sameName(v.name, name)) return &v;
    return nullptr;
}
bool hmiFunction(const HmiNames& n, std::string_view name) {
    return std::any_of(n.functions.begin(), n.functions.end(), [&](const std::string& f) { return sameName(f, name); });
}

const Variable* globalNamed(const Project& p, std::string_view name, Index except = kNoIndex) {
    for (Index i = 0; i < p.variables.size(); ++i)
        if (i != except && p.variables[i].scope == VariableScope::Global && sameName(p.strings.text(p.variables[i].name), name))
            return &p.variables[i];
    return nullptr;
}

// Ce qu'un nom du programme heurte ailleurs que chez les variables : un type,
// un bloc ou une unite, un bloc standard.
std::string programNameClash(const Project& p, std::string_view name, Kind self, Index selfIndex) {
    for (Index i = 0; i < p.derivedTypes.size(); ++i)
        if (!(self == Kind::Ddt && i == selfIndex) && sameName(p.strings.text(p.derivedTypes[i].name), name))
            return "un type d\xC3\xA9riv\xC3\xA9 porte ce nom";
    for (Index i = 0; i < p.pous.size(); ++i) {
        if ((self == Kind::Dfb || self == Kind::Unit) && i == selfIndex) continue;
        if (sameName(p.strings.text(p.pous[i].name), name))
            return p.pous[i].kind == PouKind::FunctionBlockType ? "un bloc DFB porte ce nom" : "une unit\xC3\xA9 de programme porte ce nom";
    }
    if (const auto* b = BlockLibrary::shared().find(name)) return "c'est le nom d'un bloc standard (" + b->name + ")";
    return {};
}

// `selfHmi` : la variable IHM qu'on renomme (ou que le plan renomme aussi) -
// elle ne heurte pas son propre nouveau nom.
std::string hmiClash(const HmiSide* hmi, std::string_view name, bool forPlc, std::string_view selfHmi) {
    if (!hmi) return {};
    const auto n = hmi->names();
    if (const auto* v = hmiVariable(n, name); v && !sameName(v->name, selfHmi))
        return forPlc ? "une variable IHM porte ce nom (" + v->name + ") : l'IHM lirait la sienne" : "une variable IHM porte d\xC3\xA9j\xC3\xA0 ce nom";
    if (hmiFunction(n, name)) return "une fonction IHM porte ce nom";
    if (const auto* w = hmiView(n, name)) return "une vue de l'IHM porte ce nom (" + w->name + ")";
    return {};
}

Verdict locate(const Project* p, const HmiSide* hmi, Kind kind, std::string_view raw, Target& out, std::string& why) {
    const std::string name = trimmed(raw);
    out = Target{};
    out.kind = kind;
    if (name.empty()) {
        why = "aucun nom donn\xC3\xA9";
        return Verdict::NotFound;
    }
    if (!isHmiKind(kind) && !p) {
        why = "aucun projet ouvert";
        return Verdict::NotFound;
    }
    const auto notFound = [&](std::string w) {
        why = std::move(w);
        return Verdict::NotFound;
    };
    switch (kind) {
        case Kind::Variable: {
            const auto dot = name.find('.');
            if (dot == std::string::npos) {
                for (Index i = 0; i < p->variables.size(); ++i) {
                    const auto& v = p->variables[i];
                    if (v.scope != VariableScope::Global || !sameName(p->strings.text(v.name), name)) continue;
                    out.name = text(*p, v.name);
                    out.display = out.name;
                    out.index = i;
                    out.global = true;
                    out.address = v.address.raw;
                    out.detail = "variable globale " + text(*p, v.type.name) + (v.address.raw.empty() ? std::string{} : " AT " + v.address.raw);
                    return Verdict::Ok;
                }
                return notFound("aucune variable globale " + name + " (une variable d'unit\xC3\xA9 : Unite.variable)");
            }
            const std::string unitName = trimmed(std::string_view(name).substr(0, dot));
            const std::string varName = trimmed(std::string_view(name).substr(dot + 1));
            for (Index u = 0; u < p->pous.size(); ++u) {
                if (!sameName(p->strings.text(p->pous[u].name), unitName)) continue;
                for (Index i = 0; i < p->variables.size(); ++i) {
                    const auto& v = p->variables[i];
                    if (v.owner != u || v.scope == VariableScope::Global || v.scope == VariableScope::DerivedMember
                        || !sameName(p->strings.text(v.name), varName))
                        continue;
                    out.name = text(*p, v.name);
                    out.display = text(*p, p->pous[u].name) + "." + out.name;
                    out.index = i;
                    out.unit = u;
                    const auto& owner = p->pous[u];
                    const std::string of = owner.kind == PouKind::FunctionBlockType ? " du bloc "
                                         : owner.kind == PouKind::ProgramUnit       ? " de l'unit\xC3\xA9 "
                                                                                    : " de ";
                    out.detail = scopeLabel(v.scope) + of + text(*p, owner.name) + ", " + text(*p, v.type.name);
                    if (p->pous[u].kind == PouKind::FunctionBlockType) {
                        why = "un param\xC3\xA8tre ou une variable de bloc DFB ne se renomme pas ici : les appels de ses instances ne suivraient pas";
                        return Verdict::Unsupported;
                    }
                    return Verdict::Ok;
                }
                return notFound(text(*p, p->pous[u].name) + " n'a pas de variable " + varName);
            }
            return notFound("aucune unit\xC3\xA9 " + unitName);
        }
        case Kind::Ddt:
            for (Index i = 0; i < p->derivedTypes.size(); ++i)
                if (sameName(p->strings.text(p->derivedTypes[i].name), name)) {
                    out.name = text(*p, p->derivedTypes[i].name);
                    out.display = out.name;
                    out.index = i;
                    out.detail = "type d\xC3\xA9riv\xC3\xA9, " + std::to_string(p->derivedTypes[i].fields.size()) + " champ(s)";
                    return Verdict::Ok;
                }
            return notFound("aucun type d\xC3\xA9riv\xC3\xA9 " + name);
        case Kind::DdtField: {
            // Lot API 8 : "Type.champ".
            const auto dot = name.find('.');
            if (dot == std::string::npos) return notFound("Type.champ attendu (" + name + ")");
            const std::string typeName = trimmed(std::string_view(name).substr(0, dot));
            const std::string fieldName = trimmed(std::string_view(name).substr(dot + 1));
            for (Index d = 0; d < p->derivedTypes.size(); ++d) {
                const auto& dt = p->derivedTypes[d];
                if (!sameName(p->strings.text(dt.name), typeName)) continue;
                for (const auto f : dt.fields) {
                    if (f >= p->variables.size() || !sameName(p->strings.text(p->variables[f].name), fieldName)) continue;
                    const auto& v = p->variables[f];
                    out.name = text(*p, v.name);
                    out.type = d;
                    out.typeName = text(*p, dt.name);
                    out.display = out.typeName + "." + out.name;
                    out.index = f;
                    out.detail = "champ de " + out.typeName + ", " + text(*p, v.type.name);
                    return Verdict::Ok;
                }
                return notFound(text(*p, dt.name) + " n'a pas de champ " + fieldName);
            }
            return notFound("aucun type d\xC3\xA9riv\xC3\xA9 " + typeName);
        }
        case Kind::Dfb:
        case Kind::Unit:
            for (Index i = 0; i < p->pous.size(); ++i) {
                const auto& pou = p->pous[i];
                if (!sameName(p->strings.text(pou.name), name)) continue;
                const bool block = pou.kind == PouKind::FunctionBlockType;
                if (kind == Kind::Dfb ? !block : pou.kind != PouKind::ProgramUnit && pou.kind != PouKind::Program) continue;
                out.name = text(*p, pou.name);
                out.display = out.name;
                out.index = i;
                out.detail = block ? "bloc DFB, " + std::to_string(pou.instanceCount) + " instance(s)"
                                   : "unit\xC3\xA9 de programme, " + std::to_string(pou.sections.size()) + " section(s)";
                if (block && !pou.userDefined) {
                    why = out.name + " est un bloc de la biblioth\xC3\xA8que standard : il ne se renomme pas";
                    return Verdict::Unsupported;
                }
                return Verdict::Ok;
            }
            return notFound(kind == Kind::Dfb ? "aucun bloc DFB " + name : "aucune unit\xC3\xA9 " + name);
        case Kind::Section:
            for (Index i = 0; i < p->sections.size(); ++i)
                if (sameName(p->strings.text(p->sections[i].name), name)) {
                    const auto& s = p->sections[i];
                    out.name = text(*p, s.name);
                    out.display = out.name;
                    out.index = i;
                    out.detail = std::string(s.isSubroutine ? "sous-routine " : "section ") + std::string(toString(s.language));
                    if (s.owner < p->pous.size() && p->pous[s.owner].kind == PouKind::FunctionBlockType) {
                        why = "le corps d'un bloc DFB ne se renomme pas ici";
                        return Verdict::Unsupported;
                    }
                    return Verdict::Ok;
                }
            return notFound("aucune section " + name);
        case Kind::Table:
            for (Index i = 0; i < p->animationTables.size(); ++i)
                if (sameName(p->strings.text(p->animationTables[i].name), name)) {
                    const auto& t = p->animationTables[i];
                    out.name = text(*p, t.name);
                    out.display = out.name;
                    out.index = i;
                    out.detail = "table d'animation, " + std::to_string(t.entries.size()) + " ligne(s)"
                               + (t.owner ? ", unit\xC3\xA9 " + text(*p, t.owner) : std::string{});
                    return Verdict::Ok;
                }
            return notFound("aucune table d'animation " + name);
        case Kind::HmiVariable: {
            if (!hmi) return notFound("pas d'IHM dans ce projet");
            const auto n = hmi->names();
            const auto* v = hmiVariable(n, name);
            if (!v) return notFound("aucune variable IHM " + name);
            out.name = v->name;
            out.display = v->name;
            out.address = v->address;
            out.detail = "variable IHM " + v->type + (v->equipment.empty() ? std::string(", locale \xC3\xA0 l'IHM") : ", li\xC3\xA9" "e \xC3\xA0 " + v->equipment + " (" + v->address + ")");
            return Verdict::Ok;
        }
        case Kind::HmiView: {
            if (!hmi) return notFound("pas d'IHM dans ce projet");
            const auto n = hmi->names();
            const auto* v = hmiView(n, name);
            if (!v) return notFound("aucune vue " + name);
            out.name = v->name;
            out.display = v->name;
            out.detail = v->role == "vue" || v->role.empty() ? std::string("vue") : "vue (" + v->role + ")";
            return Verdict::Ok;
        }
    }
    return notFound("genre inconnu");
}

// Le nouveau nom d'une variable liee, deduit de celui de la cible.
//  API -> IHM : Vitesse_IHM contient Vitesse -> Debit_IHM.
//  IHM -> API : Vitesse_IHM -> Debit_IHM ; ce qui entoure Vitesse (""/"_IHM")
//  se retrouve autour de Debit : l'API devient Debit.
std::string deduceFromHmi(std::string_view hmiOld, std::string_view hmiNew, std::string_view plc) {
    const std::string lo = lower(hmiOld), lp = lower(plc), ln = lower(hmiNew);
    const auto at = lo.find(lp);
    if (lp.empty() || at == std::string::npos) return {};
    const std::string prefix = lo.substr(0, at), suffix = lo.substr(at + lp.size());
    if (ln.size() <= prefix.size() + suffix.size()) return {};
    if (ln.compare(0, prefix.size(), prefix) != 0 || ln.compare(ln.size() - suffix.size(), suffix.size(), suffix) != 0) return {};
    return std::string(hmiNew.substr(prefix.size(), hmiNew.size() - prefix.size() - suffix.size()));
}

} // namespace

// ================================================================= public ====
std::optional<Kind> kindFromKey(std::string_view key) noexcept {
    const std::string k = lower(trimmed(key));
    for (const auto& info : kKinds)
        if (k == info.key) return info.kind;
    // Quelques synonymes : ce que les volets et les scripts ecrivent aussi.
    if (k == "type" || k == "type-derive") return Kind::Ddt;
    if (k == "bloc" || k == "fb") return Kind::Dfb;
    if (k == "unit" || k == "unit\xC3\xA9") return Kind::Unit;
    if (k == "sous-routine" || k == "sr") return Kind::Section;
    if (k == "table-animation") return Kind::Table;
    if (k == "ihm-var" || k == "variable-ihm") return Kind::HmiVariable;
    if (k == "vue" || k == "ihm-view" || k == "popup") return Kind::HmiView;
    // Lot API 8 ("champ" seul reste inconnu : il dirait mal de quoi).
    if (k == "champ-ddt" || k == "ddt.champ" || k == "champ-de-ddt" || k == "ddt-field") return Kind::DdtField;
    return std::nullopt;
}

std::string_view kindKey(Kind k) noexcept {
    for (const auto& info : kKinds)
        if (info.kind == k) return info.key;
    return "?";
}

std::string_view kindLabel(Kind k) noexcept {
    for (const auto& info : kKinds)
        if (info.kind == k) return info.label;
    return "?";
}

std::string_view supportedKinds() noexcept {
    return "variable, ddt, ddt-champ, dfb, unite, section, table, ihm-variable, ihm-vue";
}

const std::string& previewMark() {
    static const std::string mark("\x01");
    return mark;
}

bool sameName(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(uc(a[i])) != std::tolower(uc(b[i]))) return false;
    return true;
}

bool isIdentifier(std::string_view s) noexcept {
    if (s.empty() || !identStart(s.front())) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return identChar(c); });
}

bool isReservedWord(std::string_view s) noexcept {
    return std::any_of(std::begin(kReserved), std::end(kReserved), [&](std::string_view w) { return sameName(w, s); });
}

std::string replaceInsensitive(std::string_view text, std::string_view from, std::string_view to) {
    if (from.empty()) return std::string(text);
    const std::string lt = lower(text), lf = lower(from);
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto at = lt.find(lf, i);
        if (at == std::string::npos) break;
        out.append(text.substr(i, at - i));
        out.append(to);
        i = at + from.size();
    }
    out.append(text.substr(std::min(i, text.size())));
    return out;
}

bool resolve(const Project* p, const HmiSide* hmi, Kind kind, std::string_view name, Target& out, std::string* why) {
    std::string reason;
    const auto v = locate(p, hmi, kind, name, out, reason);
    if (why) *why = reason;
    return v == Verdict::Ok;
}

namespace {

// `freed` : un nom que le meme plan libere (la variable liee qu'on renomme
// aussi) - il ne compte pas comme pris.
std::string problemOf(const Project* p, const HmiSide* hmi, const Target& t, std::string_view raw, Verdict* verdict,
                      std::string_view freed) {
    const auto say = [&](Verdict v, std::string why) {
        if (verdict) *verdict = v;
        return why;
    };
    const auto global = [&](std::string_view n) -> const Variable* {
        const auto* g = p ? globalNamed(*p, n) : nullptr;
        return g && !(!freed.empty() && sameName(p->strings.text(g->name), freed)) ? g : nullptr;
    };
    const std::string name = trimmed(raw);
    if (name.empty()) return say(Verdict::Unchanged, "tape le nouveau nom");
    if (name == t.name) return say(Verdict::Unchanged, "c'est d\xC3\xA9j\xC3\xA0 son nom");
    const bool caseOnly = sameName(name, t.name);
    // ---- l'IHM : ses propres regles --------------------------------------------
    if (t.kind == Kind::HmiVariable) {
        if (!isIdentifier(name)) return say(Verdict::Invalid, "lettres sans accent, chiffres et _ ; pas de chiffre en t\xC3\xAAte");
        if (isReservedWord(name) || sameName(name, "SYS")) return say(Verdict::Invalid, "\xC2\xAB " + name + " \xC2\xBB est un mot r\xC3\xA9serv\xC3\xA9");
        if (!caseOnly) {
            if (auto clash = hmiClash(hmi, name, false, t.name); !clash.empty()) return say(Verdict::Taken, clash);
            if (const auto* g = global(name))
                return say(Verdict::Taken, "une variable de l'automate porte ce nom (" + text(*p, g->name) + ") : l'IHM ne la lirait plus");
        }
        return say(Verdict::Ok, {});
    }
    if (t.kind == Kind::HmiView) {
        for (const char c : name)
            if (c == ';' || c == '|' || c == '\'' || c == '"' || c == '\n' || c == '\t')
                return say(Verdict::Invalid, std::string("\xC2\xAB ") + c + " \xC2\xBB n'est pas permis dans un nom de vue");
        if (hmi) {
            const auto n = hmi->names();
            const auto* self = hmiView(n, t.name);
            if (self && self->role == "symbole" && !isIdentifier(name))
                return say(Verdict::Invalid, "un symbole se nomme comme une variable (lettres, chiffres, _)");
            for (const auto& v : n.views)
                if (v.name != t.name && (caseOnly ? v.name == name : sameName(v.name, name)))
                    return say(Verdict::Taken, "une vue porte d\xC3\xA9j\xC3\xA0 ce nom (" + v.name + ")");
            if (isIdentifier(name)) {
                if (const auto* v = hmiVariable(n, name))
                    return say(Verdict::Taken, "une variable IHM porte ce nom (" + v->name + ") : Vue.Objet se confondrait avec elle");
                if (const auto* g = global(name))
                    return say(Verdict::Taken, "une variable de l'automate porte ce nom (" + text(*p, g->name) + ")");
            }
        }
        return say(Verdict::Ok, {});
    }
    // ---- le programme : un identifiant Control Expert ---------------------------
    if (auto why = identifierProblem(name); !why.empty()) return say(Verdict::Invalid, why);
    if (isReservedWord(name)) return say(Verdict::Invalid, "\xC2\xAB " + name + " \xC2\xBB est un mot r\xC3\xA9serv\xC3\xA9 du langage");
    if (!p) return say(Verdict::NotFound, "aucun projet ouvert");
    if (t.kind == Kind::Table) {
        if (auto why = animationTableNameProblem(*p, name, t.index); !why.empty()) return say(Verdict::Taken, why);
        return say(Verdict::Ok, {});
    }
    if (caseOnly) return say(Verdict::Ok, {});
    switch (t.kind) {
        case Kind::Variable: {
            if (t.global) {
                if (const auto* g = globalNamed(*p, name, t.index)) return say(Verdict::Taken, "une variable globale porte d\xC3\xA9j\xC3\xA0 ce nom (" + text(*p, g->name) + ")");
                // Une unite qui a sa propre variable de ce nom et dont le code
                // nomme la globale : apres, il lirait la sienne. (Une unite qui
                // cache aussi l'ancien nom ne nomme pas la globale : rien n'y change.)
                const auto hiddenOld = shadowingPous(*p, t.name, t.index);
                for (const auto u : shadowingPous(*p, name, kNoIndex))
                    if (!hiddenOld.count(u) && pouCites(*p, u, t.name))
                        return say(Verdict::Taken, "l'unit\xC3\xA9 " + text(*p, p->pous[u].name) + " a une variable \xC3\xA0 elle de ce nom : son code, qui nomme "
                                                       + t.name + ", la lirait \xC3\xA0 la place");
            } else {
                for (Index i = 0; i < p->variables.size(); ++i) {
                    const auto& o = p->variables[i];
                    if (i == t.index || o.owner != t.unit || o.scope == VariableScope::Global || o.scope == VariableScope::DerivedMember) continue;
                    if (sameName(p->strings.text(o.name), name))
                        return say(Verdict::Taken, "une variable de la m\xC3\xAAme unit\xC3\xA9 porte d\xC3\xA9j\xC3\xA0 ce nom");
                }
                if (const auto* g = globalNamed(*p, name); g && pouCites(*p, t.unit, name))
                    return say(Verdict::Taken, "le code de l'unit\xC3\xA9 nomme la variable globale " + text(*p, g->name) + " : la locale la cacherait");
            }
            if (auto clash = programNameClash(*p, name, t.kind, t.index); !clash.empty()) return say(Verdict::Taken, clash);
            for (const auto& s : p->sections)
                if (s.isSubroutine && sameName(p->strings.text(s.name), name))
                    return say(Verdict::Taken, "une sous-routine porte ce nom (ses appels \xC2\xAB " + name + "(); \xC2\xBB)");
            if (t.global)
                if (auto clash = hmiClash(hmi, name, true, freed); !clash.empty()) return say(Verdict::Taken, clash);
            return say(Verdict::Ok, {});
        }
        case Kind::Ddt:
        case Kind::Dfb:
        case Kind::Unit: {
            if (auto clash = programNameClash(*p, name, t.kind, t.index); !clash.empty()) return say(Verdict::Taken, clash);
            if (const auto* g = globalNamed(*p, name)) return say(Verdict::Taken, "une variable globale porte ce nom (" + text(*p, g->name) + ")");
            return say(Verdict::Ok, {});
        }
        case Kind::Section: {
            for (Index i = 0; i < p->sections.size(); ++i)
                if (i != t.index && sameName(p->strings.text(p->sections[i].name), name))
                    return say(Verdict::Taken, "une section porte d\xC3\xA9j\xC3\xA0 ce nom");
            if (t.index < p->sections.size() && p->sections[t.index].isSubroutine)
                if (const auto* g = globalNamed(*p, name))
                    return say(Verdict::Taken, "une variable globale porte ce nom (" + text(*p, g->name) + ") : les appels \xC2\xAB " + name + "(); \xC2\xBB se confondraient");
            return say(Verdict::Ok, {});
        }
        case Kind::DdtField: {
            // Lot API 8 : libre parmi les champs de son type (une globale du meme
            // nom ne gene pas : Control Expert l'accepte).
            if (t.type < p->derivedTypes.size())
                for (const auto f : p->derivedTypes[t.type].fields)
                    if (f != t.index && f < p->variables.size() && sameName(p->strings.text(p->variables[f].name), name))
                        return say(Verdict::Taken, "un autre champ de " + t.typeName + " porte d\xC3\xA9j\xC3\xA0 ce nom ("
                                                       + text(*p, p->variables[f].name) + ")");
            return say(Verdict::Ok, {});
        }
        default: return say(Verdict::Ok, {});
    }
}

} // namespace

std::string nameProblem(const Project* p, const HmiSide* hmi, const Target& t, std::string_view raw, Verdict* verdict) {
    return problemOf(p, hmi, t, raw, verdict, {});
}

Plan makePlan(const Project* p, const HmiSide* hmi, Kind kind, std::string_view oldName, std::string_view newName, bool withLinks) {
    Plan plan;
    plan.newName = trimmed(newName);
    std::string why;
    const auto where = locate(p, hmi, kind, oldName, plan.target, why);
    if (where != Verdict::Ok) {
        plan.verdict = where;
        plan.problem = why;
        return plan;
    }
    Verdict verdict = Verdict::Ok;
    plan.problem = nameProblem(p, hmi, plan.target, plan.newName, &verdict);
    plan.verdict = verdict;
    const bool good = verdict == Verdict::Ok;
    const auto& t = plan.target;

    // ---- les variables liees de l'autre cote --------------------------------------
    if (hmi && t.kind == Kind::Variable && t.global) {
        const auto n = hmi->names();
        for (const auto& hv : n.variables) {
            if (hv.equipment.empty() || hv.address.empty()) continue;
            const bool byAddress = !t.address.empty() && sameName(hv.address, t.address);
            const bool byName = sameName(hv.address, t.name);
            if (!byAddress && !byName) continue;
            Link l;
            l.kind = Kind::HmiVariable;
            l.name = hv.name;
            l.how = byAddress ? "li\xC3\xA9" "e \xC3\xA0 la m\xC3\xAAme adresse " + t.address : "li\xC3\xA9" "e \xC3\xA0 " + hv.equipment + " par son nom";
            if (good) {
                if (lower(hv.name).find(lower(t.name)) == std::string::npos) {
                    l.problem = "son nom ne contient pas " + t.name;
                } else {
                    l.proposed = replaceInsensitive(hv.name, t.name, plan.newName);
                    Target lt;
                    lt.kind = Kind::HmiVariable;
                    lt.name = hv.name;
                    Verdict lv = Verdict::Ok;
                    // La globale qu'on renomme libere son nom : il ne compte pas.
                    l.problem = problemOf(p, hmi, lt, l.proposed, &lv, t.name);
                    // Le meme nom que la variable de l'automate : permis s'il
                    // l'etait deja avant (les deux s'appelaient pareil).
                    if (lv == Verdict::Ok && sameName(l.proposed, plan.newName) && !sameName(hv.name, t.name))
                        l.problem = "ce serait le nom de la variable de l'automate : l'IHM lirait la sienne";
                }
            }
            plan.links.push_back(std::move(l));
        }
    } else if (p && t.kind == Kind::HmiVariable && hmi) {
        const auto n = hmi->names();
        if (const auto* hv = hmiVariable(n, t.name); hv && !hv->equipment.empty() && !hv->address.empty()) {
            for (Index i = 0; i < p->variables.size(); ++i) {
                const auto& g = p->variables[i];
                if (g.scope != VariableScope::Global) continue;
                const std::string gname = text(*p, g.name);
                const bool byAddress = !g.address.raw.empty() && sameName(hv->address, g.address.raw);
                const bool byName = sameName(hv->address, gname);
                if (!byAddress && !byName) continue;
                Link l;
                l.kind = Kind::Variable;
                l.name = gname;
                l.how = byAddress ? "\xC3\xA0 la m\xC3\xAAme adresse " + g.address.raw : "li\xC3\xA9" "e par son nom";
                if (good) {
                    l.proposed = deduceFromHmi(t.name, plan.newName, gname);
                    if (l.proposed.empty()) {
                        l.problem = "son nom ne se d\xC3\xA9" "duit pas de " + plan.newName;
                    } else {
                        Target lt;
                        std::string ignored;
                        if (locate(p, hmi, Kind::Variable, gname, lt, ignored) == Verdict::Ok) {
                            Verdict lv = Verdict::Ok;
                            // La variable IHM qu'on renomme libere son nom : il ne compte pas.
                            l.problem = problemOf(p, hmi, lt, l.proposed, &lv, t.name);
                            if (lv == Verdict::Unchanged) l.problem = "elle porte d\xC3\xA9j\xC3\xA0 ce nom";
                        }
                    }
                }
                plan.links.push_back(std::move(l));
            }
        }
    }

    // ---- ce que la commande fera ------------------------------------------------
    plan.renames.push_back({t, good ? plan.newName : previewMark()});
    // Lot API 8 : un champ de DDT - les types de l'automate, pour la moitie IHM.
    if (p && t.kind == Kind::DdtField) plan.renames.back().plc = std::make_shared<const PlcTypes>(*p);
    if (good && withLinks) {
        for (const auto& l : plan.links) {
            if (!l.usable()) continue;
            Rename r;
            std::string ignored;
            if (locate(p, hmi, l.kind, l.name, r.target, ignored) != Verdict::Ok) continue;
            r.to = l.proposed;
            plan.renames.push_back(std::move(r));
            plan.withLinks = true;
        }
    }

    // ---- ce qui change ---------------------------------------------------------
    std::vector<std::string> doubts;      // lot API 8 : les acces qui ne disent pas leur type
    if (p) {
        Recorder rec(&plan.changes, nullptr);
        for (const auto& r : plan.renames) planDomain(*p, r, rec);
        doubts = std::move(rec.doubts);
    }
    if (hmi) {
        std::vector<Rename> seen;
        for (const auto& r : plan.renames)
            if ((r.target.kind == Kind::Variable && r.target.global) || isHmiKind(r.target.kind) || r.target.kind == Kind::DdtField) seen.push_back(r);
        if (!seen.empty()) {
            hmi->collect(seen, plan.changes);
            if (good)
                if (auto clash = hmi->conflict(seen); !clash.empty()) {
                    // Lot API 8 : un champ - l'IHM lit un chemin qui ne dit pas son type.
                    plan.verdict = t.kind == Kind::DdtField ? Verdict::Ambiguous : Verdict::Taken;
                    plan.problem = std::move(clash);
                }
        }
    }
    // Lot API 8 : un acces au champ que le plan ne sait pas rattacher a son type -
    // on ne devine pas : renommer a la main (la ou il est, dans le plan).
    if (good && plan.verdict == Verdict::Ok && !doubts.empty()) {
        std::string others;
        std::size_t n = 0;
        if (p && t.kind == Kind::DdtField)
            for (const auto& o : PlcTypes(*p).owners(t.name))
                if (!sameName(o, t.typeName)) {
                    others += (others.empty() ? "" : ", ") + o;
                    ++n;
                }
        const bool many = doubts.size() > 1;
        plan.verdict = Verdict::Ambiguous;
        plan.problem = std::to_string(doubts.size()) + " acc\xC3\xA8s \xC3\xA0 \xC2\xAB ." + t.name + " \xC2\xBB "
                     + (many ? "ne disent pas leur type (" : "ne dit pas son type (") + doubts.front() + (many ? ", \xE2\x80\xA6)" : ")")
                     + (others.empty() ? std::string{} : ", et " + others + (n > 1 ? " ont" : " a") + " aussi un membre " + t.name)
                     + (many ? " : renomme-les \xC3\xA0 la main" : " : renomme-le \xC3\xA0 la main");
    }
    // Une vue citee dans des expressions (Vue.Objet.Propriete) : son nouveau nom
    // doit pouvoir s'y ecrire.
    if (good && t.kind == Kind::HmiView && !isIdentifier(plan.newName)
        && std::any_of(plan.changes.begin(), plan.changes.end(), [](const Change& c) { return c.code; })) {
        plan.verdict = Verdict::Invalid;
        plan.problem = t.name + " est cit\xC3\xA9" "e dans des expressions (" + t.name + ".Objet...) : le nouveau nom doit \xC3\xAAtre un identifiant";
    }
    return plan;
}

std::size_t Plan::count(std::optional<Tab> tab) const {
    return static_cast<std::size_t>(std::count_if(changes.begin(), changes.end(), [&](const Change& c) {
        return !c.info && (!tab || c.tab == *tab);
    }));
}

std::size_t Plan::places(std::optional<Tab> tab) const {
    std::set<std::string> keys;
    for (const auto& c : changes) {
        if (c.info || (tab && c.tab != *tab)) continue;
        std::string key(1, static_cast<char>('0' + static_cast<int>(c.tab)));
        for (const auto& part : c.path) key += "\n" + part;
        keys.insert(std::move(key));
    }
    return keys.size();
}

bool Plan::touchesApi() const {
    return std::any_of(changes.begin(), changes.end(), [](const Change& c) { return !c.info && c.tab != Tab::Hmi; });
}

bool Plan::touchesHmi() const {
    return std::any_of(changes.begin(), changes.end(), [](const Change& c) { return !c.info && c.tab == Tab::Hmi; });
}

// ================================================================ commande ====
RenamePlanCommand::RenamePlanCommand(std::shared_ptr<Project> p, std::vector<Rename> api, core::CommandPtr hmi, std::string label)
    : project_(std::move(p)), api_(std::move(api)), hmi_(std::move(hmi)), label_(std::move(label)) {}

core::Status RenamePlanCommand::execute() {
    before_.reset();
    if (project_) {
        // Un renommage apres l'autre, chacun calcule sur le projet du moment
        // (la variable liee ne touche pas les memes champs, mais l'ordre reste
        // celui du plan).
        for (const auto& r : api_) {
            std::vector<Edit> edits;
            Recorder rec(nullptr, &edits);
            planDomain(*project_, r, rec);
            if (edits.empty()) continue;
            if (!before_) before_ = *project_;
            applyEdits(*project_, edits);
        }
    }
    if (hmi_) {
        if (auto st = hmi_->execute(); !st) {
            // L'IHM refuse : le programme revient tel qu'il etait, rien n'est empile.
            if (project_ && before_) *project_ = std::move(*before_);
            before_.reset();
            return st;
        }
    }
    if (!before_ && !hmi_) return core::fail(core::ErrorCode::Cancelled, "rien \xC3\xA0 renommer");
    return core::ok();
}

core::Status RenamePlanCommand::undo() {
    core::Status last = core::ok();
    if (hmi_) last = hmi_->undo();
    if (project_ && before_) {
        *project_ = std::move(*before_);
        before_.reset();
    }
    return last;
}

std::string commandLabel(const Plan& plan) {
    std::string label = "Renommer " + std::string(kindLabel(plan.target.kind)) + " " + plan.target.display + " en " + plan.newName;
    const auto links = plan.renames.size() > 1 ? plan.renames.size() - 1 : 0;
    if (links == 1) label += " (et " + plan.renames[1].target.name + " en " + plan.renames[1].to + ")";
    else if (links > 1) label += " (et " + std::to_string(links) + " li\xC3\xA9" "es)";
    return label;
}

core::CommandPtr makeCommand(std::shared_ptr<Project> p, const HmiSide* hmi, const Plan& plan) {
    if (!plan.ok()) return nullptr;
    const std::string label = commandLabel(plan);
    core::CommandPtr hmiPart;
    if (hmi) {
        std::vector<Rename> seen;
        for (const auto& r : plan.renames)
            if ((r.target.kind == Kind::Variable && r.target.global) || isHmiKind(r.target.kind) || r.target.kind == Kind::DdtField) seen.push_back(r);
        if (!seen.empty()) hmiPart = hmi->command(seen, label);
    }
    // Rien du programme ne change (une vue, une variable IHM hors des tables) :
    // la commande de l'IHM telle quelle - l'historique la range cote IHM.
    if (hmiPart && !plan.touchesApi()) return hmiPart;
    // Sans projet modifiable (un export lu tel quel), rien du programme ne change.
    return std::make_unique<RenamePlanCommand>(std::move(p), plan.renames, std::move(hmiPart), label);
}

// ============================================================ la difference ===
std::vector<Piece> inlineDiff(std::string_view before, std::string_view after) {
    // Des mots (lettres, chiffres, _) et des signes un par un.
    const auto tokens = [](std::string_view s) {
        std::vector<std::string_view> out;
        std::size_t i = 0;
        while (i < s.size()) {
            std::size_t j = i + 1;
            if (identChar(s[i]) || s[i] == '\x01')
                while (j < s.size() && (identChar(s[j]) || s[j] == '\x01')) ++j;
            else if (uc(s[i]) >= 0x80)
                while (j < s.size() && (uc(s[j]) & 0xC0) == 0x80) ++j;   // un caractere UTF-8 entier
            out.push_back(s.substr(i, j - i));
            i = j;
        }
        return out;
    };
    std::vector<Piece> out;
    const auto push = [&](std::string_view t, int kind) {
        if (t.empty()) return;
        if (!out.empty() && out.back().kind == kind) out.back().text.append(t);
        else out.push_back({std::string(t), kind});
    };
    const auto a = tokens(before), b = tokens(after);
    if (a.size() == b.size()) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] == b[i]) {
                push(a[i], 0);
            } else {
                push(a[i], 1);
                push(b[i], 2);
            }
        }
        return out;
    }
    // Autant de mots de part et d'autre : pas le cas. Le debut et la fin communs.
    std::size_t head = 0;
    while (head < before.size() && head < after.size() && before[head] == after[head]) ++head;
    std::size_t tail = 0;
    while (tail < before.size() - head && tail < after.size() - head
           && before[before.size() - 1 - tail] == after[after.size() - 1 - tail])
        ++tail;
    push(before.substr(0, head), 0);
    push(before.substr(head, before.size() - head - tail), 1);
    push(after.substr(head, after.size() - head - tail), 2);
    push(before.substr(before.size() - tail), 0);
    return out;
}

// ==================================================================== lot API 8 ====
//  LE TYPE D'UN CHEMIN DE L'AUTOMATE, ET LES ACCES A UN CHAMP DE DDT.
// ===================================================================================
namespace {

std::string upperKey(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(uc(c)));
    return out;
}

// Ce qu'une valeur d'un bloc montre au-dehors : ses broches et ses publiques.
bool visibleMember(VariableScope s) {
    return s == VariableScope::Input || s == VariableScope::Output || s == VariableScope::InOut || s == VariableScope::Public;
}

} // namespace

PlcTypes::PlcTypes(const Project& p) {
    for (Index d = 0; d < p.derivedTypes.size(); ++d) {
        const auto& dt = p.derivedTypes[d];
        const std::string key = upperKey(p.strings.text(dt.name));
        typeNames_[key] = text(p, dt.name);
        auto& names = types_[key];
        for (const auto f : dt.fields)
            if (f < p.variables.size()) names.emplace(upperKey(p.strings.text(p.variables[f].name)), trimmed(p.strings.text(p.variables[f].type.name)));
    }
    for (Index i = 0; i < p.variables.size(); ++i) {
        const auto& v = p.variables[i];
        const std::string name = upperKey(p.strings.text(v.name));
        std::string type = trimmed(p.strings.text(v.type.name));
        if (v.scope == VariableScope::Global) {
            globals_.emplace(name, std::move(type));
            continue;
        }
        if (v.scope == VariableScope::DerivedMember || v.owner >= p.pous.size()) continue;
        const auto& pou = p.pous[v.owner];
        if (pou.kind == PouKind::FunctionBlockType && visibleMember(v.scope)) {
            const std::string key = upperKey(p.strings.text(pou.name));
            typeNames_[key] = text(p, pou.name);
            types_[key].emplace(name, type);
        }
        pous_[v.owner].emplace(name, std::move(type));
    }
}

RootType PlcTypes::root(std::string_view name, Index pou) const {
    const std::string key = upperKey(name);
    if (const auto it = pous_.find(pou); it != pous_.end())
        if (const auto v = it->second.find(key); v != it->second.end()) return {RootIs::Plc, v->second};
    if (const auto g = globals_.find(key); g != globals_.end()) return {RootIs::Plc, g->second};
    return {};
}

bool PlcTypes::isGlobal(std::string_view name) const { return globals_.count(upperKey(name)) != 0; }

std::string PlcTypes::memberType(std::string_view type, std::string_view member) const {
    const std::string t = trimmed(type);
    if (const auto it = types_.find(upperKey(t)); it != types_.end()) {
        const auto m = it->second.find(upperKey(member));
        return m == it->second.end() ? std::string{} : m->second;
    }
    // Un bloc de la bibliotheque (TON.Q, R_TRIG.Q...).
    if (const auto* b = BlockLibrary::shared().find(t); b && b->kind == BlockKind::FunctionBlock)
        for (const auto& prm : b->parameters)
            if (sameName(prm.name, member)) return prm.type;
    return {};
}

std::string PlcTypes::elementType(std::string_view type) {
    const auto shape = members::parseArray(type);
    if (shape.valid()) return trimmed(shape.element);
    // Des bornes qui ne se lisent pas (des constantes) : ce qui suit OF.
    const std::string t = trimmed(type), u = upperKey(t);
    if (u.rfind("ARRAY", 0) != 0) return {};
    const auto of = u.rfind(" OF ");
    return of == std::string::npos ? std::string{} : trimmed(std::string_view(t).substr(of + 4));
}

std::vector<std::string> PlcTypes::owners(std::string_view member) const {
    const std::string key = upperKey(member);
    std::vector<std::string> out;
    for (const auto& [type, names] : types_)
        if (names.count(key)) {
            const auto shown = typeNames_.find(type);
            out.push_back(shown == typeNames_.end() ? type : shown->second);
        }
    for (const auto& b : BlockLibrary::shared().all()) {
        if (b.kind != BlockKind::FunctionBlock || types_.count(upperKey(b.name))) continue;
        if (std::any_of(b.parameters.begin(), b.parameters.end(), [&](const BlockParameter& prm) { return sameName(prm.name, member); }))
            out.push_back(b.name);
    }
    return out;
}

// Un seul passage sur le texte : chaque expression postfixee (racine, puis
// .membre, [indice], (appel)) garde le type de ce qui est a sa gauche ; un
// crochet ou une parenthese ouvre une expression neuve (les indices ont les
// leurs), refermee elle rend la main a celle d'avant. Les commentaires, les
// chaines, les nombres, les litteraux types (T#5s) et les adresses (%MW10.3) ne
// sont pas des expressions postfixees.
std::string rewriteFieldAccesses(std::string_view s, const FieldAccess& fa, const PlcTypes& types, const RootTyper& rootOf,
                                 Syntax syntax, FieldScan* scan) {
    const bool c = syntax == Syntax::C;
    struct State {
        RootType t;
        bool     valid{false};      // une expression finit juste a gauche
    };
    const std::size_t n = s.size();
    std::string out;
    out.reserve(n + 8);
    State cur;
    bool dot = false;               // un '.' (ou '->') attend son membre
    std::vector<State> stack;       // les crochets et les parentheses ouverts
    int line = 1;
    std::size_t i = 0;
    const auto copy = [&](std::size_t from, std::size_t to) {
        for (std::size_t k = from; k < to; ++k) line += s[k] == '\n' ? 1 : 0;
        out.append(s.substr(from, to - from));
    };
    const auto reset = [&] {
        cur = State{};
        dot = false;
    };
    while (i < n) {
        const char ch = s[i];
        const char nx = i + 1 < n ? s[i + 1] : '\0';
        if (std::isspace(uc(ch))) {                       // l'expression continue apres un blanc
            line += ch == '\n' ? 1 : 0;
            out += ch;
            ++i;
            continue;
        }
        // Les commentaires : l'expression d'avant continue apres eux.
        if ((!c && ch == '(' && nx == '*') || (c && ch == '/' && nx == '*')) {
            const auto end = s.find(c ? "*/" : "*)", i + 2);
            const std::size_t stop = end == std::string_view::npos ? n : end + 2;
            copy(i, stop);
            i = stop;
            continue;
        }
        if ((ch == '/' && nx == '/') || (c && ch == '#')) {  // // ; en C, une ligne du preprocesseur
            const auto end = s.find('\n', i);
            const std::size_t stop = end == std::string_view::npos ? n : end;
            copy(i, stop);
            i = stop;
            continue;
        }
        if (ch == '\'' || ch == '"') {                     // une chaine ($' ou \' l'y echappe)
            std::size_t j = i + 1;
            while (j < n && s[j] != ch) j += (s[j] == (c ? '\\' : '$') && j + 1 < n) ? 2 : 1;
            const std::size_t stop = std::min(n, j + 1);
            copy(i, stop);
            i = stop;
            reset();
            continue;
        }
        if (std::isdigit(uc(ch))) {                       // 1.5, 16#FF, 1e3, 0x1F
            std::size_t j = i;
            while (j < n && (identChar(s[j]) || s[j] == '#' || (s[j] == '.' && j + 1 < n && std::isdigit(uc(s[j + 1]))))) ++j;
            copy(i, j);
            i = j;
            reset();
            continue;
        }
        if (!c && ch == '%') {                            // %MW100.3, %I0.3.2
            std::size_t j = i + 1;
            while (j < n && (identChar(s[j]) || s[j] == '.')) ++j;
            copy(i, j);
            i = j;
            reset();
            continue;
        }
        if (identStart(ch)) {
            std::size_t j = i + 1;
            while (j < n && identChar(s[j])) ++j;
            const std::string_view word = s.substr(i, j - i);
            if (!c && j < n && s[j] == '#') {             // T#5s, INT#3, DT#2026-09-24-10:00:00
                std::size_t k = j + 1;
                while (k < n && (identChar(s[k]) || s[k] == '.' || s[k] == ':' || s[k] == '-' || s[k] == '+')) ++k;
                copy(i, k);
                i = k;
                reset();
                continue;
            }
            if (dot && cur.valid) {
                // Un membre de ce qui est a gauche.
                bool rewrite = false;
                if (sameName(word, fa.field)) {
                    if (cur.t.is == RootIs::Plc) {
                        rewrite = sameName(trimmed(cur.t.type), fa.type);
                    } else if (cur.t.is == RootIs::Unknown) {
                        if (fa.unique) rewrite = true;
                        else if (scan) scan->ambiguous.push_back(line);
                    }
                }
                if (rewrite) {
                    out += fa.to;
                    if (scan) ++scan->rewritten;
                } else {
                    out.append(word);
                }
                if (cur.t.is == RootIs::Plc) {
                    std::string next = types.memberType(cur.t.type, word);
                    cur.t = next.empty() ? RootType{} : RootType{RootIs::Plc, std::move(next)};
                }
            } else {
                // Une racine.
                cur.t = rootOf ? rootOf(word) : RootType{};
                out.append(word);
            }
            cur.valid = true;
            dot = false;
            i = j;
            continue;
        }
        if (ch == '.' && nx != '.') {                     // (".." : une plage)
            dot = cur.valid;
            out += ch;
            ++i;
            continue;
        }
        if (c && ch == '-' && nx == '>') {
            dot = cur.valid;
            out += "->";
            i += 2;
            continue;
        }
        if (ch == '[' || ch == '(') {
            stack.push_back(cur);
            reset();
            out += ch;
            ++i;
            continue;
        }
        if (ch == ']' || ch == ')') {
            State outer;
            if (!stack.empty()) {
                outer = std::move(stack.back());
                stack.pop_back();
            }
            if (ch == ']' && outer.valid) {
                // Un element du tableau de gauche.
                if (outer.t.is == RootIs::Plc) {
                    std::string e = PlcTypes::elementType(outer.t.type);
                    outer.t = e.empty() ? RootType{} : RootType{RootIs::Plc, std::move(e)};
                }
                cur = std::move(outer);
            } else if (ch == ')') {
                // Ce que rend un appel (ou une parenthese) ne se lit pas ; celui
                // d'une fonction de l'IHM n'est pas de l'automate.
                cur = State{};
                cur.valid = true;
                if (outer.valid && outer.t.is == RootIs::Other) cur.t.is = RootIs::Other;
            } else {
                cur = State{};
            }
            dot = false;
            out += ch;
            ++i;
            continue;
        }
        // Un operateur, un separateur : l'expression est finie.
        reset();
        out += ch;
        ++i;
    }
    return out;
}

// ---- Lot API 8 : les forcages enregistres de la simulation --------------------------
namespace {
// Un chemin dont la racine est `from` (sans casse), suivie de '.', '[' ou de rien :
// `to` a sa place (Pompe, Pompe.Etat, Pompes[2]). Sinon tel quel.
std::string renameForcingRoot(const std::string& path, std::string_view from, std::string_view to) {
    if (from.empty() || path.size() < from.size() || !sameName(std::string_view(path).substr(0, from.size()), from)) return path;
    if (path.size() > from.size() && path[from.size()] != '.' && path[from.size()] != '[') return path;
    return std::string(to) + path.substr(from.size());
}
} // namespace

std::string renameForcingLine(const std::string& line, const Rename& r) {
    std::size_t b = 0;
    while (b < line.size() && (line[b] == ' ' || line[b] == '\t')) ++b;
    const auto eq = line.find('=', b);
    if (b >= line.size() || line[b] == '#' || eq == std::string::npos) return line;
    std::size_t e = eq;
    while (e > b && (line[e - 1] == ' ' || line[e - 1] == '\t')) --e;
    const std::string name = line.substr(b, e - b);
    std::string next = name;
    switch (r.target.kind) {
        case Kind::Variable:
            if (r.target.global) {
                next = renameForcingRoot(name, r.target.name, r.to);
            } else if (const auto dot = r.target.display.rfind('.'); dot != std::string::npos) {
                const std::string unit = r.target.display.substr(0, dot + 1);   // "Unite."
                if (name.size() > unit.size() && sameName(std::string_view(name).substr(0, unit.size()), unit))
                    next = name.substr(0, unit.size()) + renameForcingRoot(name.substr(unit.size()), r.target.name, r.to);
            }
            break;
        case Kind::Unit:
            next = renameForcingRoot(name, r.target.name, r.to);
            break;
        case Kind::DdtField:
            if (r.plc) {
                const FieldAccess access{r.target.typeName, r.target.name, r.to, false};
                const auto plc = r.plc;
                next = rewriteFieldAccesses(name, access, *plc, [plc](std::string_view n) { return plc->root(n); });
            }
            break;
        default:
            break;
    }
    return next == name ? line : line.substr(0, b) + next + line.substr(e);
}

bool citedByForcings(const Rename& r) {
    if (r.to.empty()) return false;
    return r.target.kind == Kind::Variable || r.target.kind == Kind::Unit || (r.target.kind == Kind::DdtField && r.plc);
}
// ---- fin Lot API 8 ----

} // namespace project::rename
