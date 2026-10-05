// app/TypePanes.cpp - les types derives, les blocs DFB, les unites (lot API 5).
#include "TypePanes.hpp"

#include "../project/CodeIconKeys.hpp"

#include "ApiPanes.hpp"
#include "hmi/HmiPanels.hpp"

#include "../project/ApiCommands.hpp"
#include "../project/EditCommands.hpp"
#include "../project/MacroSpec.hpp"
#include "../project/RenameCommands.hpp"
#include "RenameDialog.hpp"               // lot 7 : requestRename (le dialogue qui montre tout)
#include "../ui/Icons.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <system_error>

namespace app {

namespace {
// 1.8.0 : l'icone au choix d'une ligne (sections, unites, blocs, types), dans sa couleur.
void codeIconCell(ui::CellStyle& s, int icon) {
    if (icon < 0) return;
    s.icon = ui::codeIcon(icon);
    s.iconColor = ui::codeIconColor(icon);
    s.iconTone = ui::Tone::None;
}
} // namespace


using ui::RowIndex;
using PG = ui::PropertyGrid;
using namespace apikit;
namespace usage = project::usage;
namespace mt = project::members;

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kBody{15};
constexpr std::size_t kNpos = static_cast<std::size_t>(-1);
// Lot API 7 : un niveau de plus, un cran de plus. 18 px : la fleche d'une
// ligne fille tombe sous l'icone de sa mere, a toute profondeur.
constexpr float kStep = 18.f;

std::string text(const domain::Project& p, domain::SymbolId id) { return std::string(p.strings.text(id)); }

PG::Property ro(std::string name, std::string value, std::string description = {}) {
    return {std::move(name), std::move(value), PG::ValueType::ReadOnly, std::move(description), {}, nullptr};
}

std::string fitText(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (r.measure(s, f).width <= w) return s;
    std::string out = s;
    while (!out.empty() && r.measure(out + "\xE2\x80\xA6", f).width > w) {
        out.pop_back();
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();
    }
    return out + "\xE2\x80\xA6";
}

// Les colonnes d'une table, en une ligne.
void columns(ui::TableView& t, const ui::ITableModel& m, std::initializer_list<float> widths, std::initializer_list<std::size_t> right = {}) {
    std::vector<ui::TableView::Column> cols(m.columnCount());
    std::size_t i = 0;
    for (const float w : widths) {
        if (i >= cols.size()) break;
        cols[i].title = m.headerText(i);
        cols[i].width = w;
        cols[i].sortable = false;
        ++i;
    }
    for (const auto r : right)
        if (r < cols.size()) cols[r].align = ui::Align::End;
    t.setColumns(std::move(cols));
}

// Le type d'une variable, tel que le projet l'ecrit.
std::string typeOf(const domain::Project& p, domain::Index v) {
    return v < p.variables.size() ? text(p, p.variables[v].type.name) : std::string{};
}

std::string effectiveOf(const domain::Variable& v) {
    for (const auto& [key, value] : v.attributes)
        if (key == "EffectiveParameter") return value;
    return {};
}

// Une editable dans la grille : le nom, qui renomme (une commande).
PG::Property renameProperty(std::string label, std::string value, bool editable, std::string description,
                            std::function<bool(std::string_view)> commit) {
    PG::Property prop;
    prop.name = std::move(label);
    prop.value = std::move(value);
    prop.type = editable ? PG::ValueType::Text : PG::ValueType::ReadOnly;
    prop.description = std::move(description);
    if (editable) prop.commit = std::move(commit);
    return prop;
}

// ---- les portees -------------------------------------------------------------
bool isParameter(domain::VariableScope s) {
    return s == domain::VariableScope::Input || s == domain::VariableScope::Output || s == domain::VariableScope::InOut;
}

// La portee dite en francais, comme la colonne l'ecrit.
std::string scopeLabel(domain::VariableScope s) {
    switch (s) {
        case domain::VariableScope::Input:  return "entr\xC3\xA9" "e";
        case domain::VariableScope::Output: return "sortie";
        case domain::VariableScope::InOut:  return "entr\xC3\xA9" "e-sortie";
        case domain::VariableScope::Local:  return "locale";
        case domain::VariableScope::Public: return "publique";
        default:                            return "\xE2\x80\x94";
    }
}

ui::Tone scopeTone(domain::VariableScope s) {
    switch (s) {
        case domain::VariableScope::Input:  return ui::Tone::Input;
        case domain::VariableScope::Output: return ui::Tone::Output;
        case domain::VariableScope::InOut:  return ui::Tone::InOut;
        default:                            return ui::Tone::Muted;
    }
}

// ---- lot API 7 : les dossiers d'une interface ----------------------------------
// Ceux de l'arbre de gauche (ViewModels : folderKinds), dans son ordre : les
// parametres, les publiques, les privees - et, pour un DFB, ses sections. Une
// locale est « privee » dans Control Expert ; tout ce qui n'est ni parametre ni
// publique y va (comme usage::interfaceOf), pour que rien ne manque.
enum Folder : int { FIn = 0, FOut, FInOut, FPublic, FPrivate, FSections, FCount };

int folderOf(domain::VariableScope s) {
    switch (s) {
        case domain::VariableScope::Input:  return FIn;
        case domain::VariableScope::Output: return FOut;
        case domain::VariableScope::InOut:  return FInOut;
        case domain::VariableScope::Public: return FPublic;
        default:                            return FPrivate;
    }
}

const char* folderLabel(int f) {
    switch (f) {
        case FIn:      return "Entr\xC3\xA9" "es";
        case FOut:     return "Sorties";
        case FInOut:   return "Entr\xC3\xA9" "es / sorties";
        case FPublic:  return "Variables publiques";
        case FPrivate: return "Variables priv\xC3\xA9" "es";
        default:       return "Sections";
    }
}

// Ce qu'un DFB en dit, au singulier (une broche, une variable).
const char* folderWord(int f) {
    switch (f) {
        case FIn:     return "entr\xC3\xA9" "e";
        case FOut:    return "sortie";
        case FInOut:  return "entr\xC3\xA9" "e-sortie";
        case FPublic: return "publique";
        default:      return "priv\xC3\xA9" "e";
    }
}

// Dans les cles, sans accent : "g:Gestion/entrees", "f:DFB_Vanne/sections".
const char* folderId(int f) {
    static const char* const kIds[] = {"entrees", "sorties", "es", "publiques", "privees", "sections"};
    return f >= 0 && f < FCount ? kIds[f] : "";
}

int folderById(std::string_view id) {
    const auto want = lower(id);
    for (int f = 0; f < FCount; ++f)
        if (want == folderId(f)) return f;
    return -1;
}

domain::VariableScope folderScope(int f) {
    switch (f) {
        case FIn:     return domain::VariableScope::Input;
        case FOut:    return domain::VariableScope::Output;
        case FInOut:  return domain::VariableScope::InOut;
        case FPublic: return domain::VariableScope::Public;
        default:      return domain::VariableScope::Local;
    }
}

ui::Tone folderTone(int f) {
    return f == FIn ? ui::Tone::Input : f == FOut ? ui::Tone::Output : f == FInOut ? ui::Tone::InOut : ui::Tone::Muted;
}

// ---- lot API 7 : les membres (project/MemberTree) ------------------------------
// L'icone d'un type, comme dans l'onglet Variables : un tableau, une structure
// (un DDT), un bloc (DFB, bibliotheque) - ce qui se deplie ; sinon une variable.
ui::Icon natureIcon(const domain::Project& p, std::string_view type) {
    switch (mt::natureOf(p, type)) {
        case mt::Nature::Array:     return ui::Icon::AnimationTable;
        case mt::Nature::Structure: return ui::Icon::DerivedType;
        case mt::Nature::Block:     return ui::Icon::FunctionBlock;
        default:                    return ui::Icon::Variable;
    }
}

// La couleur d'un membre : son sens (une broche, une publique, une privee) ;
// un champ, un element : la couleur ordinaire.
ui::Tone memberTone(const mt::Node& n) {
    if (n.what == "entr\xC3\xA9" "e") return ui::Tone::Input;
    if (n.what == "sortie") return ui::Tone::Output;
    if (n.what == "entr\xC3\xA9" "e-sortie") return ui::Tone::InOut;
    if (n.what == "publique") return ui::Tone::Info;
    if (n.what == "priv\xC3\xA9" "e") return ui::Tone::Muted;
    return ui::Tone::None;
}

// L'allure d'un membre (l'icone de sa nature, la couleur de son sens) ; un
// paquet, une ligne d'un tableau rangent des elements : en discret.
void memberLook(const domain::Project& p, const mt::Node& n, ui::Icon& icon, ui::Tone& tone) {
    if (!n.real) {
        icon = n.what == "ligne" ? ui::Icon::AnimationTable : ui::Icon::Folder;
        tone = ui::Tone::Muted;
        return;
    }
    icon = natureIcon(p, n.type);
    tone = memberTone(n);
}

// Ce qu'un membre est, quand ca apprend quelque chose : une broche, une
// publique, une privee, une ligne. « champ » et « element » se lisent deja
// dans son nom (.PT1, [3]) ; « paquet de 50 » est dans son type.
std::string memberNote(const mt::Node& n) {
    if (n.what == "champ" || n.what == "\xC3\xA9l\xC3\xA9ment" || n.what == "paquet") return {};
    return n.what;
}

// Son type ; celui d'un groupe dit ce qu'il range (« paquet de 50 »).
std::string memberType(const mt::Node& n) { return n.real ? n.type : mt::groupType(n); }

// La premiere colonne d'un membre : en retrait, sa fleche, son icone (memberLook).
// Un groupe (un paquet, une ligne d'un tableau) n'a pas de valeur a lui : en discret.
void memberNameStyle(ui::CellStyle& s, const mt::Node& n, ui::Icon icon, ui::Tone tone, float indent, bool expandable, bool open) {
    s.indent = indent;
    s.expander = expandable ? (open ? 1 : 0) : -1;
    s.monospace = true;
    s.icon = icon == ui::Icon::Folder && open ? ui::Icon::FolderOpen : icon;
    s.iconTone = tone;
    if (!n.real) s.fgTone = ui::Tone::Muted;
}

std::string memberTooltip(const mt::Node& n, const std::string& comment, bool expandable) {
    std::string t = (n.real ? n.path : n.path + n.label) + " \xC2\xB7 " + memberType(n);
    if (!n.what.empty()) t += " \xC2\xB7 " + n.what;
    if (!comment.empty()) t += " \xC2\xB7 " + comment;
    if (expandable) t += n.real ? " \xE2\x80\x94 la fl\xC3\xA8" "che d\xC3\xA9plie ses membres" : " \xE2\x80\x94 la fl\xC3\xA8" "che l'ouvre";
    return t;
}

// Le chemin d'un membre sous sa racine : ".sorties.V3" pour "armoire.sorties.V3"
// sous "armoire" - de quoi dire comment une variable (une instance) le lit.
std::string pathUnder(const mt::Node& n, const std::string& owner) {
    return n.path.size() > owner.size() ? n.path.substr(owner.size()) : std::string{};
}

// Un membre, en lecture, dans la grille des proprietes.
PG::Category memberCategory(const mt::Node& n, const std::string& comment, std::string pathNote) {
    PG::Category m;
    m.name = (n.real ? std::string("Membre ") : std::string("Groupe ")) + n.label;
    m.properties.push_back(ro("Chemin", n.real ? n.path : n.path + n.label, std::move(pathNote)));
    m.properties.push_back(ro("Type", memberType(n)));
    m.properties.push_back(ro("Nature", n.what));
    if (!comment.empty()) m.properties.push_back(ro("Commentaire", comment, "Celui de sa d\xC3\xA9" "claration (le champ, la broche, l'\xC3\xA9l\xC3\xA9ment du tableau)."));
    if (!n.real)
        m.properties.push_back(ro("\xC3\x89l\xC3\xA9ments", thousands(static_cast<std::size_t>(n.size()))));
    else if (const auto shape = mt::parseArray(n.type); shape.valid())
        m.properties.push_back(ro("\xC3\x89l\xC3\xA9ments", thousands(static_cast<std::size_t>(shape.count()))));
    m.properties.push_back(ro("", "Un membre se lit ici ; il se modifie dans son type (Types d\xC3\xA9riv\xC3\xA9s, Blocs DFB)."));
    return m;
}

// "a.b" est-il le debut du chemin "a.b.c[3]" ? (pas de "a.bc") - en minuscules.
bool pathPrefix(std::string_view head, std::string_view path) {
    if (head.empty() || path.size() < head.size() || path.compare(0, head.size(), head) != 0) return false;
    return path.size() == head.size() || path[head.size()] == '.' || path[head.size()] == '[';
}

// Un nom renomme : les cles qui le portent le suivent ("m:gestion.tempo" et
// "m:gestion.tempo.q" vers "m:gestion.tempo2"...). En minuscules, prefixe compris.
void moveKeys(std::set<std::string>& keys, const std::string& from, const std::string& to) {
    if (from == to) return;
    std::vector<std::string> moved;
    for (auto it = keys.begin(); it != keys.end();) {
        const auto& k = *it;
        const bool hit = k.size() >= from.size() && k.compare(0, from.size(), from) == 0
                      && (k.size() == from.size() || k[from.size()] == '.' || k[from.size()] == '[' || k[from.size()] == '/');
        if (hit) {
            moved.push_back(to + k.substr(from.size()));
            it = keys.erase(it);
        } else {
            ++it;
        }
    }
    keys.insert(moved.begin(), moved.end());
}

// Un type, un bloc, une unite renommes : ses cles de chaque prefixe ("tm" :
// "t:<nom>" et "m:<nom>...") le suivent - ce qui etait deplie le reste.
void renameOwner(std::set<std::string>& keys, std::string_view prefixes, const std::string& before, const std::string& after) {
    for (const char c : prefixes) {
        const std::string head{c, ':'};
        moveKeys(keys, head + lower(before), head + lower(after));
    }
}

// Le bloc, l'unite ou le type d'une cle : "v:Bloc/var", "m:Bloc.var.x" -> "Bloc".
std::string ownerOfKey(std::string_view key) {
    if (key.size() > 2 && key[1] == ':') key.remove_prefix(2);
    return std::string(key.substr(0, key.find_first_of("/.[")));
}

// ---- lot API 7 : deplier jusqu'a un membre cache (setExpanded) ------------------
// Un indice d'une cle d'element ("150"), ou le debut d'une plage ("100..199").
bool indexAt(std::string_view part, std::int64_t& out) {
    if (const auto dots = part.find(".."); dots != std::string_view::npos) part = part.substr(0, dots);
    if (part.empty()) return false;
    const char* end = part.data() + part.size();
    const auto [ptr, ec] = std::from_chars(part.data(), end, out);
    return ec == std::errc{} && ptr == end;
}

// Le membre de cette cle (en minuscules, sans "m:") vit-il SOUS ce noeud ? Un
// noeud reel : son chemin commence la cle (".x" ou "[3]" suivent). Un paquet,
// une ligne d'un tableau (project/MemberTree) : les indices deja fixes sont les
// memes, et celui de la dimension parcourue tombe dans sa plage -
// "tab[150]", "tab[100..199]" sous "tab[0..9999]" ; "grille[2,5]" sous "grille[2,*]".
bool holds(const mt::Node& n, std::string_view target) {
    if (n.real) {
        const auto key = lower(n.key);
        return target.size() > key.size() && pathPrefix(key, target);
    }
    const auto path = lower(n.path);
    if (target.size() <= path.size() + 1 || target.compare(0, path.size(), path) != 0 || target[path.size()] != '[') return false;
    const auto close = target.find(']', path.size());
    if (close == std::string_view::npos) return false;
    const auto inside = target.substr(path.size() + 1, close - path.size() - 1);
    std::vector<std::string_view> parts;
    for (std::size_t from = 0;;) {
        const auto comma = inside.find(',', from);
        parts.push_back(inside.substr(from, comma == std::string_view::npos ? std::string_view::npos : comma - from));
        if (comma == std::string_view::npos) break;
        from = comma + 1;
    }
    if (parts.size() <= n.dim || n.fixed.size() > n.dim) return false;
    for (std::size_t d = 0; d < n.fixed.size(); ++d) {
        std::int64_t v = 0;
        if (!indexAt(parts[d], v) || v != n.fixed[d]) return false;
    }
    std::int64_t i = 0;
    return indexAt(parts[n.dim], i) && i >= n.first && i <= n.last;
}

// Le chemin de `node` (une racine : un champ, une variable) jusqu'au membre de
// cle `target` (en minuscules, sans "m:") : les cles "m:..." a deplier pour le
// montrer (la racine comprise, lui non) ; `found` : le membre. Chaque niveau ne
// fabrique que ses enfants (quelques paquets, ou 100 elements au plus). Faux :
// il n'est pas sous cette racine.
bool memberChain(const domain::Project& p, mt::Node node, std::string_view target, std::vector<std::string>& chain, mt::Node& found) {
    // Chaque tour descend d'un niveau (un chemin plus long, une plage plus
    // courte) : la garde ne sert qu'a un projet incoherent.
    for (int guard = 0; guard < 512; ++guard) {
        if (lower(node.key) == target) {
            found = std::move(node);
            return true;
        }
        if (!holds(node, target)) return false;
        chain.push_back("m:" + lower(node.key));
        auto kids = mt::children(p, node);
        const auto next = std::find_if(kids.begin(), kids.end(), [&](const mt::Node& c) { return lower(c.key) == target || holds(c, target); });
        if (next == kids.end()) return false;
        node = std::move(*next);
    }
    return false;
}

// LE DEFILEMENT, quand les lignes se refont (une fleche cliquee loin dans la
// liste). ui::TableView revient en haut a chaque modelReset ; la ligne choisie
// reste en vue (selectModelRows l'y amene), mais la liste saute. Si la table
// sait rendre et reprendre son defilement (scrollOffset / setScrollOffset), le
// volet le garde : rien ne bouge sous la souris.
template <class Table>
float scrollOf(const Table& t) {
    if constexpr (requires(const Table& x) { x.scrollOffset(); }) return static_cast<float>(t.scrollOffset());
    else return 0.f;
}

template <class Table>
void keepScroll(Table& t, float y) {
    if constexpr (requires(Table& x) { x.setScrollOffset(0.f); }) t.setScrollOffset(y);
    else {
        (void)t;
        (void)y;
    }
}

} // namespace

// ======================================================================
//                            TYPES DERIVES
// ======================================================================
class DerivedTypesPane::Model final : public ui::ITableModel {
public:
    explicit Model(DerivedTypesPane& pane) : pane_(pane) {}
    enum Col : std::size_t { CName, CFields, CUsedBy, CVersion, CLibrary, CCount };
    [[nodiscard]] std::size_t rowCount() const override { return pane_.rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"Nom", "Champs", "Utilis\xC3\xA9 par", "Version", "Biblioth\xC3\xA8que"};
        return c < CCount ? kTitles[c] : "";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= pane_.rows_.size()) return {};
        const auto& row = pane_.rows_[r];
        if (row.kind == Row::Group) {
            if (c != CName) return {};
            return std::string(row.group == 0 ? "Du projet" : "De la biblioth\xC3\xA8que") + "   " + plural(row.count, "type", "types");
        }
        // Lot API 7 : un membre - son nom sous sa mere, son type (et ce qu'il
        // est, ce que sa declaration en dit), comme un champ.
        if (row.kind == Row::Member) {
            if (c == CName) return row.node.label;
            if (c != CUsedBy) return {};
            std::string t = memberType(row.node);
            if (const auto note = memberNote(row.node); !note.empty()) t += "  \xC2\xB7  " + note;
            if (!row.comment.empty()) t += "  \xC2\xB7  " + row.comment;
            return t;
        }
        const auto& info = pane_.infos_[row.info];
        if (row.kind == Row::Field) {
            const auto p = pane_.hosts_.view ? pane_.hosts_.view() : nullptr;
            if (!p || row.field >= p->variables.size()) return {};
            const auto& v = p->variables[row.field];
            switch (c) {
                case CName: return text(*p, v.name);
                case CFields: return {};
                case CUsedBy: {
                    std::string t = text(*p, v.type.name);
                    const std::string comment = text(*p, v.comment);
                    return comment.empty() ? t : t + "  \xC2\xB7  " + comment;
                }
                default: return {};
            }
        }
        switch (c) {
            case CName: return info.name;
            case CFields: return std::to_string(info.fields);
            case CUsedBy: return info.use.summary();
            case CVersion: return info.version.empty() ? std::string("\xE2\x80\x94") : info.version;
            default: return info.lib.label();
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= pane_.rows_.size()) return s;
        const auto& row = pane_.rows_[r];
        if (row.kind == Row::Group) {
            if (c == CName) {
                s.spanRow = true;
                s.bold = true;
                s.expander = row.open ? 1 : 0;
                s.icon = row.group == 0 ? ui::Icon::Folder : ui::Icon::Library;
                s.iconTone = row.group == 0 ? ui::Tone::Family3 : ui::Tone::Family2;
            }
            return s;
        }
        if (row.kind == Row::Member) {
            if (c == CName) memberNameStyle(s, row.node, row.icon, row.tone, 2.f * kStep + kStep * static_cast<float>(row.depth), row.expandable, row.open);
            else if (c == CUsedBy) {
                s.fgTone = ui::Tone::Muted;
                s.monospace = true;
            }
            return s;
        }
        const auto& info = pane_.infos_[row.info];
        if (row.kind == Row::Field) {
            if (c == CName) {
                // Lot API 7 : un champ structure a sa fleche (sous l'icone du
                // type) et l'icone de sa nature (tableau, structure, bloc).
                s.indent = 2.f * kStep;
                s.expander = row.expandable ? (row.open ? 1 : 0) : -1;
                s.icon = row.icon;
                s.iconTone = ui::Tone::Muted;
                s.monospace = true;
            } else if (c == CUsedBy) {
                s.fgTone = ui::Tone::Muted;
                s.monospace = true;
            }
            return s;
        }
        if (c == CName) {
            s.indent = kStep;
            s.expander = info.fields ? (row.open ? 1 : 0) : -1;
            s.icon = ui::Icon::DerivedType;
            s.iconTone = ui::Tone::Info;
            // 1.8.0 : l'icone au choix du type.
            if (const auto p = pane_.hosts_.view ? pane_.hosts_.view() : nullptr; p && row.group == 0)
                codeIconCell(s, project::codeicons::typeIcon(*p, info.index));
        } else if (c == CUsedBy && !info.use.used()) {
            s.fgTone = ui::Tone::Warning;
        } else if (c == CLibrary) {
            if (info.lib.kind == LibState::Different) s.fgTone = ui::Tone::Warning, s.bold = true;
            else if (info.lib.kind == LibState::UpToDate) s.fgTone = ui::Tone::Ok;
            else s.fgTone = ui::Tone::Muted;
        } else if (c == CVersion) {
            s.fgTone = ui::Tone::Muted;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(RowIndex r) const override {
        if (r >= pane_.rows_.size()) return {};
        const auto& row = pane_.rows_[r];
        if (row.kind == Row::Member) return memberTooltip(row.node, row.comment, row.expandable);
        if (row.kind == Row::Field)
            return row.expandable ? std::string("La fl\xC3\xA8" "che d\xC3\xA9plie ses membres, et chacun \xC3\xA0 son tour ; Renommer (F2) r\xC3\xA9\xC3\xA9" "crit "
                                                "les acc\xC3\xA8s \xC2\xAB .champ \xC2\xBB du code, des tables d'animation et de l'IHM, montr\xC3\xA9s avant.")
                                  : std::string{};
        if (row.kind != Row::Type) return {};
        const auto& info = pane_.infos_[row.info];
        if (info.lib.kind == LibState::Different)
            return "La biblioth\xC3\xA8que a la version " + info.lib.version + " (le projet : " + info.version + ") : \xC2\xAB Mettre \xC3\xA0 jour \xC2\xBB la prend.";
        return "La fl\xC3\xA8" "che d\xC3\xA9plie ses champs ; Renommer r\xC3\xA9\xC3\xA9" "crit les variables et le code qui l'utilisent.";
    }
private:
    DerivedTypesPane& pane_;
};

namespace {
// Lot recherche : le texte d'un type dans une colonne, tel que la table le montre.
template <class Info>
std::string derivedTypeCell(const Info& info, std::size_t c) {
    switch (c) {
        case 0: return info.name;
        case 1: return std::to_string(info.fields);
        case 2: return info.use.summary();
        case 3: return info.version.empty() ? std::string("\xE2\x80\x94") : info.version;
        default: return info.lib.label();
    }
}
} // namespace

DerivedTypesPane::DerivedTypesPane(std::string id) : ui::Widget(std::move(id)) {
    filters_ = &static_cast<ApiFilterBar&>(addChild(std::make_unique<ApiFilterBar>(this->id() + ".filtres", "Rechercher : nom, champ, commentaire\xE2\x80\xA6")));
    links_ += filters_->changed->connect([this] {
        if (table_) table_->setHighlight(filters_->search());     // lot recherche : surlignee
        rebuildRows();
        updateHint();
    });
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".types")));
    model_ = std::make_shared<Model>(*this);
    table_->setModel(model_);
    columns(*table_, *model_, {300.f, 80.f, 330.f, 90.f, 170.f}, {Model::CFields});
    // Lot recherche : les filtres des colonnes choisissent des TYPES (le volet
    // les applique : ses dossiers gardent leur compte, ses champs suivent).
    table_->setColumnFiltersEnabled(true);
    table_->setColumnFilterMode(ui::TableView::ColumnFilterMode::Host);
    table_->setColumnValuesProvider([this](std::size_t col, const std::function<void(const std::string&)>& emit) {
        const ui::SearchQuery query(filters_->search());
        for (const auto& i : infos_)
            if (keeps(i, query, static_cast<int>(col))) emit(derivedTypeCell(i, col));
    });
    links_ += table_->columnFiltersChanged->connect([this] {
        rebuildRows();
        updateHint();
    });
    table_->setSelectionMode(ui::SelectionMode::Single);
    links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>&) {
        if (syncing_) return;
        refreshProperties();
    });
    links_ += table_->expanderClicked->connect([this](RowIndex r) { toggle(r); });
    // Double-clic : un type se deplie sur ses champs ; un champ, un membre sur les siens.
    links_ += table_->activated->connect([this](RowIndex r) {
        if (r < rows_.size() && rows_[r].kind != Row::Group) toggle(r);
    });
    props_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(this->id() + ".proprietes")));
    props_->setShowDescriptionPane(true);
}

DerivedTypesPane::~DerivedTypesPane() = default;

void DerivedTypesPane::setHosts(ApiPaneHosts h) {
    hosts_ = std::move(h);
    refresh();
}

void DerivedTypesPane::attach(ApiFrame& frame) {
    frame_ = &frame;
    auto& t = frame.tools();
    t.add(AAddType, HmiGlyph::Plus, "Cr\xC3\xA9" "er un type d\xC3\xA9riv\xC3\xA9 (DDT)", "Type");
    t.add(AAddField, HmiGlyph::Plus, "Ajouter un champ au type choisi", "Champ");
    t.add(ARename, HmiGlyph::Text, "Renommer le type ou le champ choisi (F2) : le code, les tables et l'IHM suivent, montr\xC3\xA9s avant", "Renommer");
    t.add(ADelete, HmiGlyph::Delete, "Supprimer le type ou le champ choisi (Ctrl+Z le rend)", "Supprimer");
    t.separator();
    t.add(AUpdate, HmiGlyph::Refresh, "Mettre \xC3\xA0 jour depuis la biblioth\xC3\xA8que (la macro MettreAJourBibliotheque)", "Mettre \xC3\xA0 jour");
    t.separator();
    t.add(AVariables, HmiGlyph::SystemVars, "Voir les variables de ce type (l'onglet Variables, cherch\xC3\xA9 sur le type)", "Voir les variables");
    const auto editable = [this] { return hosts_.project && hosts_.project() != nullptr; };
    // Lot API 7 : un membre se lit - ni complete, ni renomme, ni supprime d'ici
    // (il appartient a un autre type : on le modifie dans le sien).
    const auto own = [this] {
        const auto* row = selectedRow();
        return row && (row->kind == Row::Type || row->kind == Row::Field);
    };
    t.setEnabledWhen(AAddType, editable);
    t.setEnabledWhen(AAddField, [editable, own] { return editable() && own(); });
    t.setEnabledWhen(ARename, [editable, own] { return editable() && own(); });
    t.setEnabledWhen(ADelete, [editable, own] { return editable() && own(); });
    t.setEnabledWhen(AUpdate, [this] { return outdated_ > 0; });
    t.setEnabledWhen(AVariables, [this] { return selectedInfo() != nullptr; });
    links_ += t.triggered->connect([this](int a) { runAction(a); });
    updateHint();
}

const DerivedTypesPane::Row* DerivedTypesPane::selectedRow() const {
    const auto sel = table_->selectedModelRows();
    return sel.empty() || sel.front() >= rows_.size() ? nullptr : &rows_[sel.front()];
}

// Le type de la ligne choisie : le sien, celui du champ, celui qui porte le membre.
const DerivedTypesPane::Info* DerivedTypesPane::selectedInfo() const {
    const auto* row = selectedRow();
    return row && row->kind != Row::Group && row->info < infos_.size() ? &infos_[row->info] : nullptr;
}

void DerivedTypesPane::refresh() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    // La ligne choisie, par sa cle (un champ, un membre compris) : elle survit
    // a la commande qui vient de tout refaire.
    if (const auto* row = selectedRow(); row && keep_.empty()) keep_ = row->key;
    infos_.clear();
    outdated_ = 0;
    if (p) {
        const auto library = hosts_.library ? hosts_.library() : nullptr;
        for (domain::Index i = 0; i < p->derivedTypes.size(); ++i) {
            const auto& dt = p->derivedTypes[i];
            Info info;
            info.index = i;
            info.name = text(*p, dt.name);
            info.version = dt.version;
            info.fields = dt.fields.size();
            info.use = usage::usesOfType(*p, info.name);
            info.lib = libraryState(library.get(), info.name, info.version, project::LibraryItemKind::DerivedType);
            outdated_ += info.lib.kind == LibState::Different ? 1u : 0u;
            infos_.push_back(std::move(info));
        }
    }
    std::size_t own = 0, lib = 0, unused = 0;
    for (const auto& i : infos_) {
        (i.lib.kind == LibState::Absent ? own : lib) += 1u;
        unused += i.use.used() ? 0u : 1u;
    }
    filters_->setChips({{"Tous", infos_.size()}, {"Du projet", own}, {"De la biblioth\xC3\xA8que", lib},
                        {"Plus r\xC3\xA9" "cents en biblioth\xC3\xA8que", outdated_}, {"Pas utilis\xC3\xA9s", unused}});
    if (frame_)
        frame_->tools().setText(AUpdate, "Mettre \xC3\xA0 jour depuis la biblioth\xC3\xA8que (la macro MettreAJourBibliotheque)",
                                "Mettre \xC3\xA0 jour (" + std::to_string(outdated_) + ")");
    rebuildRows();
    updateHint();
}

bool DerivedTypesPane::keeps(const Info& i, const ui::SearchQuery& query, int skipColumn) const {
    switch (filters_->current()) {
        case 1: if (i.lib.kind != LibState::Absent) return false; break;
        case 2: if (i.lib.kind == LibState::Absent) return false; break;
        case 3: if (i.lib.kind != LibState::Different) return false; break;
        case 4: if (i.use.used()) return false; break;
        default: break;
    }
    // Lot recherche : chaque mot dans le nom du type ou dans l'un de ses champs
    // (son nom, son type, son COMMENTAIRE) ; aucun -mot exclu.
    if (!query.empty()) {
        ui::SearchQuery::Progress found;
        query.feed(found, i.name);
        const auto p = hosts_.view ? hosts_.view() : nullptr;
        if (p && i.index < p->derivedTypes.size())
            for (const auto f : p->derivedTypes[i.index].fields) {
                if (query.hopeless(found) || (query.excluded().empty() && query.satisfied(found))) break;
                if (f >= p->variables.size()) continue;
                const auto& v = p->variables[f];
                query.feed(found, text(*p, v.name));
                query.feed(found, text(*p, v.type.name));
                query.feed(found, text(*p, v.comment));
            }
        if (!query.satisfied(found)) return false;
    }
    return ui::ColumnFilter::acceptsAll(table_->columnFilters(), [&](std::size_t c) { return derivedTypeCell(i, c); }, skipColumn);
}

void DerivedTypesPane::rebuildRows() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    if (keep_.empty())
        if (const auto* row = selectedRow()) keep_ = row->key;
    const ui::SearchQuery query(filters_->search());
    const auto keepInfo = [&](const Info& i) { return keeps(i, query, -1); };
    rows_.clear();
    std::size_t kept = 0;                 // lot recherche : "12 sur 40"
    for (int g = 0; g < 2; ++g) {
        std::vector<std::size_t> members;
        for (std::size_t k = 0; k < infos_.size(); ++k)
            if ((infos_[k].lib.kind == LibState::Absent) == (g == 0) && keepInfo(infos_[k])) members.push_back(k);
        kept += members.size();
        if (members.empty()) continue;
        Row head;
        head.kind = Row::Group;
        head.group = g;
        head.count = members.size();
        head.open = !collapsedGroups_.count(g);
        head.key = g == 0 ? "g:projet" : "g:bibliotheque";
        rows_.push_back(head);
        if (!head.open) continue;
        std::sort(members.begin(), members.end(), [&](std::size_t a, std::size_t b) { return lower(infos_[a].name) < lower(infos_[b].name); });
        for (const auto k : members) {
            Row r;
            r.kind = Row::Type;
            r.info = k;
            r.group = g;
            r.key = infos_[k].name;
            r.expandable = infos_[k].fields > 0;
            r.open = expanded_.count("t:" + lower(infos_[k].name)) != 0;
            rows_.push_back(r);
            if (!r.open || !p || infos_[k].index >= p->derivedTypes.size()) continue;
            for (const auto f : p->derivedTypes[infos_[k].index].fields) {
                Row fr;
                fr.kind = Row::Field;
                fr.info = k;
                fr.field = f;
                fr.group = g;
                if (f < p->variables.size()) {
                    // Lot API 7 : la racine de ses membres - "<Type>.<champ>", un
                    // chemin qui ne sert qu'aux cles et aux libelles (pas de valeur ici).
                    fr.key = infos_[k].name + "." + text(*p, p->variables[f].name);
                    fr.node = mt::root(fr.key, typeOf(*p, f));
                    fr.decl = f;
                    fr.icon = natureIcon(*p, fr.node.type);
                    fr.expandable = mt::hasChildren(*p, fr.node);
                    fr.open = fr.expandable && expanded_.count("m:" + lower(fr.node.key)) != 0;
                }
                rows_.push_back(fr);
                if (fr.open) addMembers(*p, fr, 1);
            }
        }
    }
    table_->setColumnFilterCounts(kept, infos_.size());
    const float scroll = scrollOf(*table_);
    model_->modelReset->emit();
    keepScroll(*table_, scroll);
    syncing_ = true;
    std::size_t pick = kNpos;
    if (!keep_.empty()) {
        const auto want = lower(keep_);
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (lower(rows_[i].key) == want) {
                pick = i;
                break;
            }
        // Repliee entre-temps (un champ, un membre) : l'ancetre montre le plus
        // proche - un membre, le champ, puis le type.
        if (pick == kNpos && want.rfind("g:", 0) != 0) {
            const auto path = want.rfind("m:", 0) == 0 ? want.substr(2) : want;
            std::size_t best = 0;
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                const auto& cand = rows_[i];
                if (cand.kind == Row::Group) continue;
                const auto k = cand.kind == Row::Type ? lower(infos_[cand.info].name) : lower(cand.node.key);
                if (k.size() > best && pathPrefix(k, path)) {
                    best = k.size();
                    pick = i;
                }
            }
        }
    }
    if (pick == kNpos)
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].kind == Row::Type) { pick = i; break; }
    if (pick != kNpos) table_->selectModelRows({static_cast<RowIndex>(pick)}, false);
    syncing_ = false;
    keep_.clear();
    refreshProperties();
}

void DerivedTypesPane::addMembers(const domain::Project& p, const Row& parent, int depth) {
    // Lot API 7 : sans limite de profondeur ; un grand tableau arrive par
    // paquets (project/MemberTree) - seules les lignes depliees sont faites.
    for (auto& n : mt::children(p, parent.node)) {
        Row c;
        c.kind = Row::Member;
        c.info = parent.info;
        c.field = parent.field;
        c.group = parent.group;
        c.depth = depth;
        c.expandable = mt::hasChildren(p, n);
        // Ce que le projet en dit : le commentaire du champ, de la broche, de l'element.
        c.decl = mt::declarationOf(p, parent.node, parent.decl, n);
        c.comment = mt::commentOf(p, c.decl, parent.node, n);
        memberLook(p, n, c.icon, c.tone);
        c.key = "m:" + n.key;
        c.open = c.expandable && expanded_.count("m:" + lower(n.key)) != 0;
        c.node = std::move(n);
        rows_.push_back(c);
        if (c.open) addMembers(p, c, depth + 1);      // c : une copie, rows_ grandit dessous
    }
}

void DerivedTypesPane::toggle(std::size_t r) {
    if (r >= rows_.size()) return;
    const auto& row = rows_[r];
    if (row.kind == Row::Group) {
        if (!collapsedGroups_.erase(row.group)) collapsedGroups_.insert(row.group);
    } else {
        std::string key;
        if (row.kind == Row::Type) key = "t:" + lower(infos_[row.info].name);
        else if (row.expandable) key = "m:" + lower(row.node.key);
        else return;
        if (!expanded_.erase(key)) expanded_.insert(key);
    }
    keep_ = row.key;
    rebuildRows();
}

bool DerivedTypesPane::setExpanded(std::string_view key, bool open) {
    const std::string k = lower(key);
    if (k.size() < 3 || k[1] != ':') return false;
    const std::string rest = k.substr(2);
    // Le dossier d'un type : 0 du projet, 1 de la bibliotheque.
    const auto folderOfType = [](const Info& i) { return i.lib.kind == LibState::Absent ? 0 : 1; };
    if (k[0] == 'g') {
        const int g = rest == "projet" || rest == "0" ? 0 : rest == "bibliotheque" || rest == "1" ? 1 : -1;
        if (g < 0) return false;
        if (open) collapsedGroups_.erase(g);
        else collapsedGroups_.insert(g);
    } else if (k[0] == 't') {
        const auto it = std::find_if(infos_.begin(), infos_.end(), [&](const Info& i) { return lower(i.name) == rest; });
        if (it == infos_.end() || it->fields == 0) return false;
        if (open) {
            expanded_.insert(k);
            collapsedGroups_.erase(folderOfType(*it));     // son dossier aussi : on le voit
        } else {
            expanded_.erase(k);
        }
    } else if (k[0] == 'm') {
        // Un champ ou un membre, montre ou non : on le cherche sous chaque champ
        // ("<type>.<champ>", sa racine) ; deplie, ses ancetres s'ouvrent avec lui.
        const auto p = hosts_.view ? hosts_.view() : nullptr;
        if (!p) return false;
        for (const auto& info : infos_) {
            if (info.index >= p->derivedTypes.size()) continue;
            for (const auto f : p->derivedTypes[info.index].fields) {
                if (f >= p->variables.size()) continue;
                std::vector<std::string> chain;
                mt::Node found;
                if (!memberChain(*p, mt::root(info.name + "." + text(*p, p->variables[f].name), typeOf(*p, f)), rest, chain, found)) continue;
                if (!mt::hasChildren(*p, found)) return false;          // rien a deplier
                if (open) {
                    collapsedGroups_.erase(folderOfType(info));
                    expanded_.insert("t:" + lower(info.name));
                    expanded_.insert(chain.begin(), chain.end());
                    expanded_.insert(k);
                } else {
                    expanded_.erase(k);
                }
                rebuildRows();
                return true;
            }
        }
        return false;
    } else {
        return false;
    }
    rebuildRows();
    return true;
}

bool DerivedTypesPane::isExpanded(std::string_view key) const {
    const std::string k = lower(key);
    if (k == "g:projet" || k == "g:0") return !collapsedGroups_.count(0);
    if (k == "g:bibliotheque" || k == "g:1") return !collapsedGroups_.count(1);
    return expanded_.count(k) != 0;
}

bool DerivedTypesPane::selectType(std::string_view name) {
    if (filters_->current() != 0 || !filters_->search().empty()) {
        filters_->setSearch("");
        filters_->setCurrent(0);
    }
    // Lot recherche : un filtre de colonne qui le cache s'efface aussi.
    for (const auto& i : infos_)
        if (lower(i.name) == lower(name) && !keeps(i, ui::SearchQuery{}, -1)) table_->clearColumnFilters();
    for (const auto& i : infos_)
        if (lower(i.name) == lower(name)) {
            collapsedGroups_.erase(i.lib.kind == LibState::Absent ? 0 : 1);
            keep_ = i.name;
            rebuildRows();
            for (std::size_t r = 0; r < rows_.size(); ++r)
                if (rows_[r].kind == Row::Type && lower(infos_[rows_[r].info].name) == lower(name)) {
                    table_->selectModelRows({static_cast<RowIndex>(r)}, true);
                    return true;
                }
        }
    return false;
}

void DerivedTypesPane::refreshProperties() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    const auto* row = selectedRow();
    const bool editable = hosts_.project && hosts_.project() != nullptr;
    std::vector<PG::Category> cats;
    if (!p || !row || row->kind == Row::Group) {
        PG::Category c;
        c.name = "Types d\xC3\xA9riv\xC3\xA9s";
        std::size_t unused = 0;
        for (const auto& i : infos_) unused += i.use.used() ? 0u : 1u;
        c.properties.push_back(ro("Types", std::to_string(infos_.size())));
        c.properties.push_back(ro("Plus r\xC3\xA9" "cents en biblioth\xC3\xA8que", std::to_string(outdated_)));
        c.properties.push_back(ro("Pas utilis\xC3\xA9s", std::to_string(unused)));
        cats.push_back(std::move(c));
        props_->setCategories(std::move(cats));
        return;
    }
    const auto& info = infos_[row->info];
    if (info.index >= p->derivedTypes.size()) {
        props_->setCategories({});
        return;
    }
    const auto& dt = p->derivedTypes[info.index];
    // Lot API 7 : un membre, en lecture ; puis le type qui le porte.
    if (row->kind == Row::Member)
        cats.push_back(memberCategory(row->node, row->comment,
                                      row->node.real ? "Depuis le type : une variable de type " + info.name + " le lit en Variable"
                                                           + pathUnder(row->node, info.name) + "."
                                                     : std::string{}));
    if (row->kind == Row::Field && row->field < p->variables.size()) {
        const auto& v = p->variables[row->field];
        PG::Category f;
        f.name = "Champ " + text(*p, v.name);
        const auto field = row->field;
        const std::string typeName = info.name, oldField = text(*p, v.name);
        f.properties.push_back(renameProperty("Nom", text(*p, v.name), editable,
                                              "Renommer le champ r\xC3\xA9\xC3\xA9" "crit les acc\xC3\xA8s \xC2\xAB .champ \xC2\xBB du code, des tables d'animation et de l'IHM, "
                                              "montr\xC3\xA9s avant (un Ctrl+Z).",
                                              [this, field, typeName, oldField](std::string_view value) {
                                                  // Lot API 8 : le nom tape ouvre le dialogue qui montre tout ce qui suit
                                                  // (la grille garde l'ancien : le dialogue renomme).
                                                  if (value != oldField && requestRename("ddt-champ", typeName + "." + oldField, std::string(value)))
                                                      return false;
                                                  auto doc = hosts_.project ? hosts_.project() : nullptr;
                                                  if (!doc || !hosts_.apply) return false;
                                                  const auto problem = project::renameProblem(*doc, project::RenameCommand::What::Field, field, value);
                                                  if (!problem.empty()) {
                                                      if (hosts_.status) hosts_.status(problem);
                                                      return false;
                                                  }
                                                  // Ses membres deplies le suivent ; il reste choisi.
                                                  moveKeys(expanded_, "m:" + lower(typeName + "." + oldField), "m:" + lower(typeName + "." + std::string(value)));
                                                  keep_ = typeName + "." + std::string(value);
                                                  hosts_.apply(std::make_unique<project::RenameCommand>(doc, project::RenameCommand::What::Field, field, std::string(value)));
                                                  return true;
                                              }));
        f.properties.push_back(ro("Type", text(*p, v.type.name)));
        f.properties.push_back(ro("Commentaire", text(*p, v.comment)));
        f.properties.push_back(ro("Initiale", text(*p, v.initValue)));
        f.properties.push_back(ro("Dans", info.name));
        cats.push_back(std::move(f));
    }
    PG::Category c;
    c.name = "Type " + info.name;
    const auto index = info.index;
    const std::string oldType = info.name;
    c.properties.push_back(renameProperty("Nom", info.name, editable,
                                          "Renommer le type r\xC3\xA9\xC3\xA9" "crit le type des variables, des champs et des param\xC3\xA8tres qui l'utilisent (un Ctrl+Z).",
                                          [this, index, oldType](std::string_view value) {
                                              // Lot 7 : le nom tape ouvre le dialogue qui montre tout ce qui suit
                                              // (la grille garde l'ancien : le dialogue renomme).
                                              if (value != oldType && requestRename("ddt", oldType, std::string(value))) return false;
                                              auto doc = hosts_.project ? hosts_.project() : nullptr;
                                              if (!doc || !hosts_.apply) return false;
                                              const auto problem = project::renameProblem(*doc, project::RenameCommand::What::DerivedType, index, value);
                                              if (!problem.empty()) {
                                                  if (hosts_.status) hosts_.status(problem);
                                                  return false;
                                              }
                                              renameOwner(expanded_, "tm", oldType, std::string(value));
                                              keep_ = std::string(value);
                                              hosts_.apply(std::make_unique<project::RenameCommand>(doc, project::RenameCommand::What::DerivedType, index, std::string(value)));
                                              return true;
                                          }));
    c.properties.push_back(ro("Version", info.version.empty() ? std::string("\xE2\x80\x94") : info.version));
    c.properties.push_back(ro("Champs", std::to_string(info.fields)));
    c.properties.push_back(ro("Utilis\xC3\xA9 par", info.use.summary(),
                              "Les variables globales de ce type (ou tableaux de ce type), les unit\xC3\xA9s et les DFB qui en d\xC3\xA9" "clarent, les types qui l'ont comme champ."));
    c.properties.push_back(ro("Biblioth\xC3\xA8que", info.lib.label(),
                              info.lib.kind == LibState::Different ? "\xC2\xAB Mettre \xC3\xA0 jour \xC2\xBB lance la macro MettreAJourBibliotheque : elle montre ce qui change avant de le faire."
                                                                   : std::string{}));
    cats.push_back(std::move(c));
    PG::Category fields;
    fields.name = "Champs  " + std::to_string(dt.fields.size());
    for (std::size_t k = 0; k < dt.fields.size() && k < 40; ++k) {
        const auto f = dt.fields[k];
        if (f >= p->variables.size()) continue;
        const auto& v = p->variables[f];
        const std::string comment = text(*p, v.comment);
        fields.properties.push_back(ro(text(*p, v.name), text(*p, v.type.name) + (comment.empty() ? std::string{} : "  \xC2\xB7  " + comment)));
    }
    if (dt.fields.size() > 40) fields.properties.push_back(ro("", "\xE2\x80\xA6 et " + std::to_string(dt.fields.size() - 40) + " autres champs"));
    cats.push_back(std::move(fields));
    if (!info.use.units.empty() || !info.use.dfbs.empty() || !info.use.types.empty()) {
        PG::Category u;
        u.name = "Qui s'en sert";
        if (info.use.globals) u.properties.push_back(ro("Variables globales", std::to_string(info.use.globals)));
        for (const auto& n : info.use.units) u.properties.push_back(ro("Unit\xC3\xA9", n));
        for (const auto& n : info.use.dfbs) u.properties.push_back(ro("Bloc DFB", n));
        for (const auto& n : info.use.types) u.properties.push_back(ro("Type", n));
        cats.push_back(std::move(u));
    }
    props_->setCategories(std::move(cats));
}

void DerivedTypesPane::updateHint() {
    if (!frame_) return;
    std::string t = plural(infos_.size(), "type d\xC3\xA9riv\xC3\xA9", "types d\xC3\xA9riv\xC3\xA9s");
    if (outdated_) t += " \xC2\xB7 " + std::to_string(outdated_) + " plus r\xC3\xA9" "cent" + (outdated_ > 1 ? "s" : "") + " en biblioth\xC3\xA8que";
    t += " \xC2\xB7 Renommer (F2) un type ou un champ montre d'abord ce qui suit (le code, les tables, l'IHM), puis le fait en une seule action (un Ctrl+Z).";
    t += " Un champ structur\xC3\xA9 (DDT, tableau, bloc) se d\xC3\xA9plie \xC3\xA0 son tour.";
    frame_->setHint(t);
}

void DerivedTypesPane::rename(const Info& info, domain::Index field) {
    if (!hosts_.ask) return;
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    if (!p) return;
    const bool isField = field != domain::kNoIndex && field < p->variables.size();
    const std::string old = isField ? text(*p, p->variables[field].name) : info.name;
    const auto what = isField ? project::RenameCommand::What::Field : project::RenameCommand::What::DerivedType;
    const auto index = isField ? field : info.index;
    const std::string typeName = info.name;
    hosts_.ask("Renommer " + old,
               isField ? "Le nouveau nom du champ : les acc\xC3\xA8s \xC2\xAB ." + old + " \xC2\xBB du code et des tables d'animation suivent."
                       : "Le nouveau nom du type : les variables, les champs et les param\xC3\xA8tres de ce type suivent.",
               old, [this, what, index, isField, typeName, old](const std::string& value) {
                   auto doc = hosts_.project ? hosts_.project() : nullptr;
                   if (!doc || !hosts_.apply) return;
                   const auto problem = project::renameProblem(*doc, what, index, value);
                   if (!problem.empty()) {
                       if (hosts_.status) hosts_.status(problem);
                       return;
                   }
                   // Ce qui etait deplie le reste, sous le nouveau nom.
                   if (isField) moveKeys(expanded_, "m:" + lower(typeName + "." + old), "m:" + lower(typeName + "." + value));
                   else renameOwner(expanded_, "tm", old, value);
                   keep_ = isField ? typeName + "." + value : value;
                   hosts_.apply(std::make_unique<project::RenameCommand>(doc, what, index, value));
                   if (hosts_.status) hosts_.status("Renomm\xC3\xA9 en " + value + " : ce qui l'utilisait suit. Ctrl+Z le d\xC3\xA9" "fait.");
               });
}

void DerivedTypesPane::runAction(int action) {
    const auto* row = selectedRow();
    const auto* info = selectedInfo();
    // Lot API 7 : un membre se lit seulement (voir attach) ; « Voir les
    // variables » prend le type qui le porte.
    const bool member = row && row->kind == Row::Member;
    switch (action) {
        case AAddType:
            if (hosts_.request) hosts_.request("create.ddt");
            return;
        case AAddField: {
            if (!info || !hosts_.ask || member) return;
            const auto index = info->index;
            const std::string typeName = info->name;
            hosts_.ask("Ajouter un champ \xC3\xA0 " + typeName, "Le nom et le type du champ, s\xC3\xA9par\xC3\xA9s par deux-points (ex. Debit : REAL).",
                       "Nouveau_champ : BOOL", [this, index, typeName](const std::string& value) {
                           auto doc = hosts_.project ? hosts_.project() : nullptr;
                           if (!doc || !hosts_.apply) return;
                           project::AddVariableCommand::Spec spec;
                           const auto colon = value.find(':');
                           const auto trim = [](std::string s) {
                               while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
                               while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
                               return s;
                           };
                           spec.name = trim(value.substr(0, colon));
                           spec.type = colon == std::string::npos ? std::string("BOOL") : trim(value.substr(colon + 1));
                           if (spec.type.empty()) spec.type = "BOOL";
                           spec.scope = domain::VariableScope::DerivedMember;
                           spec.owner = index;
                           expanded_.insert("t:" + lower(typeName));
                           keep_ = typeName + "." + spec.name;
                           hosts_.apply(std::make_unique<project::AddVariableCommand>(doc, std::move(spec)));
                       });
            return;
        }
        case ARename:
            // Lot 7 : le type - le dialogue qui montre tout ce qui suit (API et
            // IHM). Lot API 8 : un champ aussi ("Type.champ", genre ddt-champ).
            // Sans l'ecran d'analyse (un essai) : comme avant.
            if (info && !member) {
                const auto p = hosts_.view ? hosts_.view() : nullptr;
                const bool field = row && row->kind == Row::Field && p && row->field < p->variables.size();
                if (requestRename(field ? "ddt-champ" : "ddt", field ? info->name + "." + text(*p, p->variables[row->field].name) : info->name)) return;
            }
            if (info && !member) rename(*info, row && row->kind == Row::Field ? row->field : domain::kNoIndex);
            return;
        case ADelete:
            if (!info || !hosts_.remove || member) return;
            if (row && row->kind == Row::Field) hosts_.remove(domain::EntityKind::Variable, row->field);
            else hosts_.remove(domain::EntityKind::DerivedType, info->index);
            return;
        case AUpdate:
            if (hosts_.request) hosts_.request("bibliotheque");
            return;
        case AVariables:
            if (info && hosts_.request) hosts_.request("variables:" + info->name);
            return;
        default: return;
    }
}

void DerivedTypesPane::onLayout() {
    const auto b = bounds();
    const float rightW = std::clamp(b.w * 0.27f, 280.f, 440.f);
    const float barH = 44.f;
    filters_->setBounds({b.x, b.y, b.w - rightW - 1.f, barH});
    table_->setBounds({b.x, b.y + barH, b.w - rightW - 1.f, std::max(0.f, b.h - barH)});
    props_->setBounds({b.right() - rightW, b.y, rightW, b.h});
}

void DerivedTypesPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.windowBg);
    ctx.r.fillRect({props_->bounds().x - 1.f, bounds().y, 1.f, bounds().h}, ctx.theme.color.border);
}

// ---- Lot API 8 : renommer un champ (F2, le dialogue qui montre tout) ----------------
bool DerivedTypesPane::selectField(std::string_view type, std::string_view field, std::string_view was) {
    const auto it = std::find_if(infos_.begin(), infos_.end(), [&](const Info& i) { return lower(i.name) == lower(type); });
    if (it == infos_.end()) return false;
    // Le nom d'avant : ses membres deplies le suivent.
    if (!was.empty() && lower(was) != lower(field))
        moveKeys(expanded_, "m:" + lower(it->name + "." + std::string(was)), "m:" + lower(it->name + "." + std::string(field)));
    if (!selectType(it->name)) return false;
    const std::string key = it->name + "." + std::string(field);
    expanded_.insert("t:" + lower(it->name));
    keep_ = key;
    rebuildRows();
    const auto* row = selectedRow();
    return row && row->kind == Row::Field && lower(row->key) == lower(key);
}

bool DerivedTypesPane::renameSelected() {
    const auto* row = selectedRow();
    if (!row || (row->kind != Row::Type && row->kind != Row::Field) || !hosts_.project || !hosts_.project()) return false;
    runAction(ARename);
    return true;
}

ui::EventResult DerivedTypesPane::onEvent(const ui::InputEvent& ev) {
    // F2 dans la table (qui n'a pas de case a editer) : Renommer la ligne choisie.
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::F2 && k->mods.none() && !k->repeat && table_->focused())
        if (renameSelected()) return ui::EventResult::Consumed;
    return ui::EventResult::Ignored;
}
// ---- fin Lot API 8 ----

// ======================================================================
//                               BLOCS DFB
// ======================================================================
// Le bloc dessine : son nom en tete, les entrees (et E/S) a gauche, les
// sorties a droite, chaque broche avec son type - comme dans un schema FBD.
class DfbBlockView final : public ui::Widget {
public:
    explicit DfbBlockView(std::string id) : ui::Widget(std::move(id)) {}
    struct Pin { std::string name, type; bool inout{false}; };
    void set(std::string name, std::vector<Pin> left, std::vector<Pin> right, std::size_t publics) {
        name_ = std::move(name);
        left_ = std::move(left);
        right_ = std::move(right);
        publics_ = publics;
        invalidate();
    }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        r.fillRect(b, c.panelBg);
        if (name_.empty()) {
            r.drawText({b.x + 16.f, b.y + 14.f}, "Choisir un bloc dans la liste.", kBody, c.textMuted);
            return;
        }
        r.pushClip(b);
        const float pinH = 22.f;
        const std::size_t rows = std::max<std::size_t>({left_.size(), right_.size(), 2u});
        const std::size_t fit = static_cast<std::size_t>(std::max(2.f, (b.h - 70.f) / pinH));
        const std::size_t shown = std::min(rows, fit);
        const float boxW = std::clamp(b.w * 0.5f, 180.f, 300.f);
        const float boxX = b.x + (b.w - boxW) * 0.5f;
        const float boxY = b.y + 14.f;
        const float boxH = 34.f + static_cast<float>(shown) * pinH + 8.f;
        const auto accent = ctx.theme.tone(ui::Tone::Info, c.accent);
        r.fillRoundedRect({boxX, boxY, boxW, boxH}, c.windowBg, 4.f);
        r.strokeRect({boxX, boxY, boxW, boxH}, accent, 1.5f);
        r.fillRect({boxX, boxY, boxW, 28.f}, accent.withAlpha(50));
        const std::string title = fitText(r, name_, kBody, boxW - 16.f);
        r.drawText({boxX + (boxW - r.measure(title, kBody).width) * 0.5f, boxY + 5.f}, title, kBody, c.text);
        const auto pinText = [&](const Pin& pin) { return pin.inout ? pin.name + " \xE2\x87\x84" : pin.name; };
        for (std::size_t i = 0; i < shown && i < left_.size(); ++i) {
            const float y = boxY + 34.f + static_cast<float>(i) * pinH;
            r.line({boxX - 18.f, y + 9.f}, {boxX, y + 9.f}, c.borderStrong, 1.5f);
            r.drawText({boxX + 8.f, y + 1.f}, fitText(r, pinText(left_[i]), kSmall, boxW * 0.5f - 12.f), kSmall, c.text);
            const std::string ty = fitText(r, left_[i].type, kSmall, boxX - b.x - 24.f);
            r.drawText({boxX - 22.f - r.measure(ty, kSmall).width, y + 1.f}, ty, kSmall, c.textMuted);
        }
        for (std::size_t i = 0; i < shown && i < right_.size(); ++i) {
            const float y = boxY + 34.f + static_cast<float>(i) * pinH;
            r.line({boxX + boxW, y + 9.f}, {boxX + boxW + 18.f, y + 9.f}, c.borderStrong, 1.5f);
            const std::string n = fitText(r, right_[i].name, kSmall, boxW * 0.5f - 12.f);
            r.drawText({boxX + boxW - 8.f - r.measure(n, kSmall).width, y + 1.f}, n, kSmall, c.text);
            r.drawText({boxX + boxW + 22.f, y + 1.f}, fitText(r, right_[i].type, kSmall, b.right() - boxX - boxW - 26.f), kSmall, c.textMuted);
        }
        float y = boxY + boxH + 10.f;
        if (rows > shown) {
            r.drawText({boxX, y}, "\xE2\x80\xA6 et " + std::to_string(rows - shown) + " broches de plus (voir les propri\xC3\xA9t\xC3\xA9s)", kSmall, c.textMuted);
            y += 20.f;
        }
        if (right_.empty())
            r.drawText({b.x + 16.f, y}, fitText(r, "Pas de sortie : le bloc rend ses " + std::to_string(publics_) + " variables publiques (instance.Mesure\xE2\x80\xA6).", kSmall, b.w - 32.f),
                       kSmall, c.textMuted);
        r.popClip();
    }
private:
    std::string       name_;
    std::vector<Pin>  left_, right_;
    std::size_t       publics_{0};
};

// La table des blocs, en arbre (lot API 7) : un bloc, ses dossiers, leurs
// variables et leurs membres, ses sections. Les colonnes restent celles d'un
// bloc ; un dossier deplie titre celles qu'il reprend pour les lignes qui
// suivent (Type := initiale, Commentaire ; Langage, Lignes), comme le fait
// le titre des variables d'une unite.
class DfbPane::Model final : public ui::ITableModel {
public:
    explicit Model(DfbPane& pane) : pane_(pane) {}
    enum Col : std::size_t { CName, CVersion, CIn, COut, CInOut, CPublic, CSections, CInstances, CLibrary, CCount };
    [[nodiscard]] std::size_t rowCount() const override { return pane_.rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"Bloc", "Version", "Entr\xC3\xA9" "es", "Sorties", "E/S", "Publiques", "Sections", "Instances", "Biblioth\xC3\xA8que"};
        return c < CCount ? kTitles[c] : "";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= pane_.rows_.size()) return {};
        const auto& row = pane_.rows_[r];
        if (row.info >= pane_.infos_.size()) return {};
        if (row.kind == Row::Block) {
            const auto& i = pane_.infos_[row.info];
            switch (c) {
                case CName: return i.name;
                case CVersion: return i.version.empty() ? std::string("\xE2\x80\x94") : i.version;
                case CIn: return std::to_string(i.itf.inputs.size());
                case COut: return std::to_string(i.itf.outputs.size());
                case CInOut: return std::to_string(i.itf.inouts.size());
                case CPublic: return std::to_string(i.itf.publics.size());
                case CSections: return std::to_string(i.sections);
                case CInstances: return std::to_string(i.instances.size());
                default: return i.lib.label();
            }
        }
        if (row.kind == Row::Group) {
            if (c == CName) return std::string(folderLabel(row.folder)) + "  " + std::to_string(row.count);
            if (!row.open) return {};
            if (row.folder == FSections) return c == CVersion ? std::string("Langage") : c == CIn ? std::string("Lignes") : std::string{};
            return c == CVersion ? std::string("Type := initiale") : c == CLibrary ? std::string("Commentaire") : std::string{};
        }
        if (row.kind == Row::Member) {
            // Son type (et ce qu'il est : une broche, une publique...), le
            // commentaire de sa declaration.
            if (c == CName) return row.node.label;
            if (c == CVersion) {
                const auto note = memberNote(row.node);
                return note.empty() ? memberType(row.node) : memberType(row.node) + "  \xC2\xB7  " + note;
            }
            return c == CLibrary ? row.comment : std::string{};
        }
        const auto p = pane_.hosts_.view ? pane_.hosts_.view() : nullptr;
        if (!p) return {};
        if (row.kind == Row::Variable) {
            if (row.variable >= p->variables.size()) return {};
            const auto& v = p->variables[row.variable];
            if (c == CName) return text(*p, v.name);
            if (c == CVersion) {
                // Comme une declaration : REAL := 0.5.
                const std::string init = text(*p, v.initValue);
                return init.empty() ? text(*p, v.type.name) : text(*p, v.type.name) + " := " + init;
            }
            return c == CLibrary ? text(*p, v.comment) : std::string{};
        }
        if (row.section >= p->sections.size()) return {};
        const auto& s = p->sections[row.section];
        if (c == CName) return text(*p, s.name);
        if (c == CVersion) return std::string(domain::toString(s.language));
        return c == CIn ? thousands(s.lineCount) : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= pane_.rows_.size()) return s;
        const auto& row = pane_.rows_[r];
        if (row.info >= pane_.infos_.size()) return s;
        if (row.kind == Row::Block) {
            const auto& i = pane_.infos_[row.info];
            if (c == CName) {
                s.icon = ui::Icon::FunctionBlock;
                s.iconTone = ui::Tone::Info;
                s.bold = true;
                s.expander = row.expandable ? (row.open ? 1 : 0) : -1;
                if (const auto p = pane_.hosts_.view ? pane_.hosts_.view() : nullptr)
                    codeIconCell(s, project::codeicons::pouIcon(*p, i.pou));      // 1.8.0
            } else if (c == CInstances && i.instances.empty()) {
                s.fgTone = ui::Tone::Warning;
            } else if (c == CLibrary) {
                if (i.lib.kind == LibState::Different) s.fgTone = ui::Tone::Warning, s.bold = true;
                else if (i.lib.kind == LibState::UpToDate) s.fgTone = ui::Tone::Ok;
                else s.fgTone = ui::Tone::Muted;
            } else if (c == CVersion) {
                s.fgTone = ui::Tone::Muted;
            }
            return s;
        }
        if (row.kind == Row::Group) {
            s.fgTone = ui::Tone::Muted;
            s.bold = true;
            if (c == CName) {
                s.indent = kStep;
                s.expander = row.open ? 1 : 0;
                s.icon = row.open ? ui::Icon::FolderOpen : ui::Icon::Folder;
                s.iconTone = row.folder == FSections ? ui::Tone::Info : folderTone(row.folder);
            }
            return s;
        }
        if (row.kind == Row::Variable) {
            if (c == CName) {
                s.indent = 2.f * kStep;
                s.expander = row.expandable ? (row.open ? 1 : 0) : -1;
                s.icon = row.icon;
                s.iconTone = folderTone(row.folder);
                s.monospace = true;
            } else if (c == CVersion) {
                s.monospace = true;
            } else if (c == CLibrary) {
                s.fgTone = ui::Tone::Muted;
            }
            return s;
        }
        if (row.kind == Row::Member) {
            if (c == CName) {
                memberNameStyle(s, row.node, row.icon, row.tone, 2.f * kStep + kStep * static_cast<float>(row.depth), row.expandable, row.open);
            } else if (c == CVersion) {
                s.monospace = true;
                s.fgTone = ui::Tone::Muted;
            } else if (c == CLibrary) {
                s.fgTone = ui::Tone::Muted;
            }
            return s;
        }
        if (c == CName) {
            s.indent = 2.f * kStep;
            s.icon = ui::Icon::Section;
            s.iconTone = ui::Tone::Info;
            if (const auto p = pane_.hosts_.view ? pane_.hosts_.view() : nullptr)
                codeIconCell(s, project::codeicons::sectionIcon(*p, row.section));      // 1.8.0
        } else if (c == CVersion) {
            s.fgTone = ui::Tone::Muted;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(RowIndex r) const override {
        if (r >= pane_.rows_.size()) return {};
        const auto& row = pane_.rows_[r];
        if (row.kind == Row::Block)
            return "La fl\xC3\xA8" "che d\xC3\xA9plie ses entr\xC3\xA9" "es, ses sorties, ses variables et ses sections ; "
                   "double-clic : son code (tu choisis la section s'il en a plusieurs).";
        if (row.kind == Row::Group)
            return row.folder == FSections ? std::string("Double-clic sur une section : son code.")
                                           : std::string("Une variable structur\xC3\xA9" "e (DDT, tableau, bloc) se d\xC3\xA9plie sur ses membres ; double-clic : ses usages.");
        if (row.kind == Row::Member) return memberTooltip(row.node, row.comment, row.expandable);
        if (row.kind == Row::Section) return "Double-clic : le code de la section.";
        const auto p = pane_.hosts_.view ? pane_.hosts_.view() : nullptr;
        if (!p || row.variable >= p->variables.size()) return {};
        const auto& v = p->variables[row.variable];
        std::string t = text(*p, v.name) + " \xC2\xB7 " + text(*p, v.type.name) + " \xC2\xB7 " + folderWord(row.folder);
        if (const std::string comment = text(*p, v.comment); !comment.empty()) t += " \xC2\xB7 " + comment;
        t += row.expandable ? " \xE2\x80\x94 la fl\xC3\xA8" "che d\xC3\xA9plie ses membres ; double-clic : ses usages." : " \xE2\x80\x94 double-clic : ses usages.";
        return t;
    }
private:
    DfbPane& pane_;
};

class DfbPane::InstanceModel final : public ui::ITableModel {
public:
    explicit InstanceModel(DfbPane& pane) : pane_(pane) {}
    enum Col : std::size_t { CName, COwner, CCalled, CCount };
    [[nodiscard]] const std::vector<usage::Instance>* list() const {
        const auto s = pane_.selected();
        return s < pane_.infos_.size() ? &pane_.infos_[s].instances : nullptr;
    }
    [[nodiscard]] std::size_t rowCount() const override { return list() ? list()->size() : 0u; }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"Instance", "D\xC3\xA9" "clar\xC3\xA9" "e", "Appel\xC3\xA9" "e dans"};
        return c < CCount ? kTitles[c] : "";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        const auto* l = list();
        if (!l || r >= l->size()) return {};
        const auto& in = (*l)[r];
        switch (c) {
            case CName: return in.name;
            case COwner: return in.owner.empty() ? std::string("variable globale") : "dans l'unit\xC3\xA9 " + in.owner;
            default: return in.calledIn.empty() ? std::string("\xE2\x80\x94") : in.calledIn;
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex, std::size_t c) const override {
        ui::CellStyle s;
        if (c == CName) {
            s.icon = ui::Icon::Variable;
            s.iconTone = ui::Tone::Info;
            s.monospace = true;
        } else {
            s.fgTone = ui::Tone::Muted;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(RowIndex) const override { return "Double-clic : la variable, o\xC3\xB9 elle est \xC3\xA9" "crite et lue."; }
private:
    DfbPane& pane_;
};

DfbPane::DfbPane(std::string id) : ui::Widget(std::move(id)) {
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".blocs")));
    model_ = std::make_shared<Model>(*this);
    table_->setModel(model_);
    // Lot API 7 : « Version » plus large - sous un dossier deplie, elle porte le
    // type (REAL := 0.5), « Bibliotheque » le commentaire.
    columns(*table_, *model_, {260.f, 150.f, 90.f, 84.f, 60.f, 104.f, 94.f, 104.f, 190.f},
            {Model::CIn, Model::COut, Model::CInOut, Model::CPublic, Model::CSections, Model::CInstances});
    // Lot recherche : les filtres des colonnes (la table les applique) - ils
    // choisissent des blocs ; leurs dossiers et leurs variables les suivent.
    table_->setColumnFiltersEnabled(true);
    table_->setSelectionMode(ui::SelectionMode::Single);
    links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>&) {
        if (syncing_) return;
        refreshDetails();
    });
    links_ += table_->expanderClicked->connect([this](RowIndex r) { toggle(r); });
    // Le double-clic fait ce que la ligne veut dire : un bloc, son code ; une
    // variable, ses usages ; une section, son code ; un dossier, un membre se
    // deplient.
    links_ += table_->activated->connect([this](RowIndex r) {
        if (r >= rows_.size()) return;
        const auto kind = rows_[r].kind;
        if (kind == Row::Block) openCode(true);
        else if (kind == Row::Variable) {
            if (hosts_.request) hosts_.request("variable:" + std::to_string(rows_[r].variable));
        } else if (kind == Row::Section) {
            if (hosts_.request) hosts_.request("section:" + std::to_string(rows_[r].section));
        } else {
            toggle(r);
        }
    });
    block_ = &static_cast<DfbBlockView&>(addChild(std::make_unique<DfbBlockView>(this->id() + ".bloc")));
    instances_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".instances")));
    instanceModel_ = std::make_shared<InstanceModel>(*this);
    instances_->setModel(instanceModel_);
    columns(*instances_, *instanceModel_, {220.f, 220.f, 360.f});
    instances_->setSelectionMode(ui::SelectionMode::Single);
    links_ += instances_->activated->connect([this](RowIndex r) {
        const auto s = selected();
        if (s >= infos_.size() || r >= infos_[s].instances.size() || !hosts_.request) return;
        hosts_.request("variable:" + std::to_string(infos_[s].instances[r].variable));
    });
    props_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(this->id() + ".proprietes")));
    props_->setShowDescriptionPane(true);
    // Lot API 7 : « Ouvrir le code » d'un bloc a plusieurs sections - laquelle.
    menu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(this->id() + ".sections")));
    links_ += menu_->itemChosen->connect([this](int choice) {
        if (choice < 0 || static_cast<std::size_t>(choice) >= menuSections_.size() || !hosts_.request) return;
        hosts_.request("section:" + std::to_string(menuSections_[static_cast<std::size_t>(choice)]));
    });
}

DfbPane::~DfbPane() = default;

void DfbPane::setHosts(ApiPaneHosts h) {
    hosts_ = std::move(h);
    refresh();
}

void DfbPane::attach(ApiFrame& frame) {
    frame_ = &frame;
    auto& t = frame.tools();
    t.add(AAddBlock, HmiGlyph::Plus, "Cr\xC3\xA9" "er un bloc DFB", "Bloc DFB");
    t.add(ARename, HmiGlyph::Text, "Renommer le bloc choisi : ses instances suivent", "Renommer");
    t.add(ADelete, HmiGlyph::Delete, "Supprimer le bloc choisi (Ctrl+Z le rend)", "Supprimer");
    t.separator();
    t.add(AOpenCode, HmiGlyph::Code, "Ouvrir le code du bloc : la section choisie, sinon tu choisis parmi ses sections", "Ouvrir le code");
    t.separator();
    t.add(AUpdate, HmiGlyph::Refresh, "Mettre \xC3\xA0 jour depuis la biblioth\xC3\xA8que (la macro MettreAJourBibliotheque)", "Mettre \xC3\xA0 jour");
    t.separator();
    t.add(AInstance, HmiGlyph::Plus, "Cr\xC3\xA9" "er une instance du bloc choisi (une variable globale)", "Cr\xC3\xA9" "er une instance");
    const auto editable = [this] { return hosts_.project && hosts_.project() != nullptr; };
    const auto chosen = [this] { return selected() < infos_.size(); };
    // Renommer, Supprimer : sur la ligne du bloc seulement - une variable ou une
    // section choisie dessous ne doit pas faire supprimer tout le bloc.
    const auto blockRow = [this] {
        const auto* row = selectedRow();
        return row && row->kind == Row::Block && row->info < infos_.size();
    };
    t.setEnabledWhen(AAddBlock, editable);
    t.setEnabledWhen(ARename, [editable, blockRow] { return editable() && blockRow(); });
    t.setEnabledWhen(ADelete, [editable, blockRow] { return editable() && blockRow(); });
    // Un bloc sans section garde le bouton : la barre d'etat dit alors pourquoi
    // rien ne s'ouvre (un bouton grise ne le dirait pas).
    t.setEnabledWhen(AOpenCode, chosen);
    t.setEnabledWhen(AUpdate, [this] { return outdated_ > 0; });
    t.setEnabledWhen(AInstance, [editable, chosen] { return editable() && chosen(); });
    links_ += t.triggered->connect([this](int a) { runAction(a); });
    frame.setHint("La fl\xC3\xA8" "che d'un bloc d\xC3\xA9plie ses entr\xC3\xA9" "es, sorties, variables et sections, puis chaque variable ses membres. "
                  "Double-clic sur un bloc : son code ; sur une instance : la variable, o\xC3\xB9 elle est \xC3\xA9" "crite et lue.");
}

const DfbPane::Row* DfbPane::selectedRow() const {
    const auto sel = table_->selectedModelRows();
    return sel.empty() || sel.front() >= rows_.size() ? nullptr : &rows_[sel.front()];
}

// Le bloc de la ligne choisie : une ligne dessous (dossier, variable, membre,
// section) garde son bloc pour le dessin, les instances, les proprietes.
std::size_t DfbPane::selected() const {
    const auto* row = selectedRow();
    return row && row->info < infos_.size() ? row->info : kNpos;
}

std::string DfbPane::currentBlock() const {
    const auto s = selected();
    return s < infos_.size() ? infos_[s].name : std::string{};
}

void DfbPane::refresh() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    // La ligne choisie, par sa cle (un bloc renomme a deja mis la sienne).
    if (keep_.empty())
        if (const auto* row = selectedRow()) keep_ = row->key;
    infos_.clear();
    outdated_ = 0;
    if (p) {
        const auto library = hosts_.library ? hosts_.library() : nullptr;
        const usage::Where where(*p);
        for (domain::Index i = 0; i < p->pous.size(); ++i) {
            const auto& pou = p->pous[i];
            if (pou.kind != domain::PouKind::FunctionBlockType || !pou.userDefined) continue;
            Info info;
            info.pou = i;
            info.name = text(*p, pou.name);
            info.version = pou.version;
            info.itf = usage::interfaceOf(*p, i);
            info.sections = pou.sections.size();
            info.instances = usage::instancesOf(*p, info.name, where);
            info.lib = libraryState(library.get(), info.name, info.version, project::LibraryItemKind::FunctionBlock);
            outdated_ += info.lib.kind == LibState::Different ? 1u : 0u;
            infos_.push_back(std::move(info));
        }
    }
    if (frame_)
        frame_->tools().setText(AUpdate, "Mettre \xC3\xA0 jour depuis la biblioth\xC3\xA8que (la macro MettreAJourBibliotheque)",
                                "Mettre \xC3\xA0 jour (" + std::to_string(outdated_) + ")");
    rebuildRows();
    // Une commande a pu changer les instances du meme bloc : leur liste se refait.
    instanceModel_->modelReset->emit();
}

void DfbPane::rebuildRows() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    if (keep_.empty())
        if (const auto* row = selectedRow()) keep_ = row->key;
    rows_.clear();
    for (std::size_t k = 0; k < infos_.size(); ++k) {
        const auto& info = infos_[k];
        const std::vector<domain::Index>* lists[] = {&info.itf.inputs, &info.itf.outputs, &info.itf.inouts, &info.itf.publics, &info.itf.privates};
        const std::string blockKey = lower(info.name);
        Row b;
        b.kind = Row::Block;
        b.info = k;
        b.key = "b:" + info.name;
        b.expandable = info.sections > 0;
        for (const auto* l : lists) b.expandable = b.expandable || !l->empty();
        b.open = b.expandable && expanded_.count("b:" + blockKey) != 0;
        rows_.push_back(b);
        if (!b.open || !p) continue;
        // Ses dossiers non vides, dans l'ordre de l'arbre de gauche.
        for (int f = 0; f < FSections; ++f) {
            const auto& list = *lists[f];
            if (list.empty()) continue;
            Row head;
            head.kind = Row::Group;
            head.info = k;
            head.folder = f;
            head.count = list.size();
            head.key = "f:" + info.name + "/" + folderId(f);
            head.open = expanded_.count("f:" + blockKey + "/" + folderId(f)) != 0;
            rows_.push_back(head);
            if (!head.open) continue;
            for (const auto v : list) {
                if (v >= p->variables.size()) continue;
                const std::string name = text(*p, p->variables[v].name);
                Row r;
                r.kind = Row::Variable;
                r.info = k;
                r.folder = f;
                r.variable = v;
                r.key = "v:" + info.name + "/" + name;
                // Sa racine : "<Bloc>.<variable>" (pour les cles et les libelles ;
                // une instance la lirait en Instance.<variable>).
                r.node = mt::root(info.name + "." + name, typeOf(*p, v));
                r.decl = v;
                r.icon = natureIcon(*p, r.node.type);
                r.expandable = mt::hasChildren(*p, r.node);
                r.open = r.expandable && expanded_.count("m:" + lower(r.node.key)) != 0;
                rows_.push_back(r);
                if (r.open) addMembers(*p, r, 1);
            }
        }
        if (info.pou < p->pous.size() && !p->pous[info.pou].sections.empty()) {
            const auto& sections = p->pous[info.pou].sections;
            Row head;
            head.kind = Row::Group;
            head.info = k;
            head.folder = FSections;
            head.count = sections.size();
            head.key = "f:" + info.name + "/" + folderId(FSections);
            head.open = expanded_.count("f:" + blockKey + "/" + folderId(FSections)) != 0;
            rows_.push_back(head);
            if (!head.open) continue;
            for (const auto s : sections) {
                if (s >= p->sections.size()) continue;
                Row r;
                r.kind = Row::Section;
                r.info = k;
                r.folder = FSections;
                r.section = s;
                r.key = "s:" + info.name + "/" + text(*p, p->sections[s].name);
                rows_.push_back(r);
            }
        }
    }
    const float scroll = scrollOf(*table_);
    model_->modelReset->emit();
    // La table grandit avec ce qu'on deplie : sa hauteur tout de suite, pour que
    // la ligne gardee se montre dans la bonne hauteur.
    invalidateLayout();
    if (bounds().w > 0.f) onLayout();
    keepScroll(*table_, scroll);
    syncing_ = true;
    std::size_t pick = kNpos;
    if (!keep_.empty()) {
        std::string want = lower(keep_);
        if (want.size() < 2 || want[1] != ':') want = "b:" + want;       // un nom seul : un bloc
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (lower(rows_[i].key) == want) {
                pick = i;
                break;
            }
        // Repliee entre-temps : l'ancetre montre le plus proche (un membre, la
        // variable), sinon le bloc qui la porte.
        if (pick == kNpos && want.rfind("m:", 0) == 0) {
            const auto path = want.substr(2);
            std::size_t best = 0;
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                const auto& cand = rows_[i];
                if (cand.kind != Row::Variable && cand.kind != Row::Member) continue;
                const auto k = lower(cand.node.key);
                if (k.size() > best && pathPrefix(k, path)) {
                    best = k.size();
                    pick = i;
                }
            }
        }
        if (pick == kNpos) {
            const auto owner = "b:" + lower(ownerOfKey(want));
            for (std::size_t i = 0; i < rows_.size(); ++i)
                if (lower(rows_[i].key) == owner) {
                    pick = i;
                    break;
                }
        }
    }
    if (pick == kNpos && !rows_.empty()) pick = 0;
    if (pick != kNpos) table_->selectModelRows({static_cast<RowIndex>(pick)}, false);
    syncing_ = false;
    keep_.clear();
    refreshDetails();
}

void DfbPane::addMembers(const domain::Project& p, const Row& parent, int depth) {
    for (auto& n : mt::children(p, parent.node)) {
        Row c;
        c.kind = Row::Member;
        c.info = parent.info;
        c.folder = parent.folder;
        c.depth = depth;
        c.expandable = mt::hasChildren(p, n);
        c.decl = mt::declarationOf(p, parent.node, parent.decl, n);
        c.comment = mt::commentOf(p, c.decl, parent.node, n);
        memberLook(p, n, c.icon, c.tone);
        c.key = "m:" + n.key;
        c.open = c.expandable && expanded_.count("m:" + lower(n.key)) != 0;
        c.node = std::move(n);
        rows_.push_back(c);
        if (c.open) addMembers(p, c, depth + 1);      // c : une copie, rows_ grandit dessous
    }
}

void DfbPane::toggle(std::size_t r) {
    if (r >= rows_.size() || rows_[r].info >= infos_.size()) return;
    const auto& row = rows_[r];
    std::string key;
    switch (row.kind) {
        case Row::Block:
            if (!row.expandable) return;
            key = "b:" + lower(infos_[row.info].name);
            break;
        case Row::Group:
            key = "f:" + lower(infos_[row.info].name) + "/" + folderId(row.folder);
            break;
        case Row::Variable:
        case Row::Member:
            if (!row.expandable) return;
            key = "m:" + lower(row.node.key);
            break;
        default:
            return;
    }
    if (!expanded_.erase(key)) expanded_.insert(key);
    keep_ = row.key;
    rebuildRows();
}

bool DfbPane::setExpanded(std::string_view key, bool open) {
    const std::string k = lower(key);
    if (k.size() < 3 || k[1] != ':') return false;
    const std::string rest = k.substr(2);
    bool ok = false;
    std::vector<std::string> parents;         // deplier : ce qu'il faut ouvrir au-dessus pour le montrer
    if (k[0] == 'b') {
        for (const auto& row : rows_)
            ok = ok || (row.kind == Row::Block && row.expandable && lower(infos_[row.info].name) == rest);
    } else if (k[0] == 'f') {
        const auto slash = rest.find('/');
        const int f = slash == std::string::npos ? -1 : folderById(rest.substr(slash + 1));
        const std::string block = rest.substr(0, slash);
        for (const auto& info : infos_) {
            if (f < 0 || lower(info.name) != block) continue;
            const std::size_t counts[] = {info.itf.inputs.size(), info.itf.outputs.size(), info.itf.inouts.size(),
                                          info.itf.publics.size(), info.itf.privates.size(), info.sections};
            ok = counts[static_cast<std::size_t>(f)] > 0;
        }
        parents.push_back("b:" + block);
    } else if (k[0] == 'm') {
        // Une variable ou un membre, montre ou non : on le cherche sous chaque
        // variable ("<bloc>.<variable>", sa racine) ; deplie, son bloc, son
        // dossier et ses ancetres s'ouvrent avec lui.
        const auto p = hosts_.view ? hosts_.view() : nullptr;
        if (!p) return false;
        for (const auto& info : infos_) {
            const std::vector<domain::Index>* lists[] = {&info.itf.inputs, &info.itf.outputs, &info.itf.inouts, &info.itf.publics, &info.itf.privates};
            for (int f = 0; f < FSections && !ok; ++f)
                for (const auto v : *lists[f]) {
                    if (v >= p->variables.size()) continue;
                    std::vector<std::string> chain;
                    mt::Node found;
                    if (!memberChain(*p, mt::root(info.name + "." + text(*p, p->variables[v].name), typeOf(*p, v)), rest, chain, found)) continue;
                    if (!mt::hasChildren(*p, found)) return false;       // rien a deplier
                    parents = std::move(chain);
                    parents.push_back("b:" + lower(info.name));
                    parents.push_back("f:" + lower(info.name) + "/" + folderId(f));
                    ok = true;
                    break;
                }
            if (ok) break;
        }
    }
    if (!ok) return false;
    if (open) {
        expanded_.insert(parents.begin(), parents.end());
        expanded_.insert(k);
    } else {
        expanded_.erase(k);
    }
    rebuildRows();
    return true;
}

bool DfbPane::isExpanded(std::string_view key) const { return expanded_.count(lower(key)) != 0; }

bool DfbPane::selectBlock(std::string_view name) {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].kind == Row::Block && rows_[i].info < infos_.size() && lower(infos_[rows_[i].info].name) == lower(name)) {
            table_->selectModelRows({static_cast<RowIndex>(i)}, true);
            return true;
        }
    return false;
}

void DfbPane::refreshDetails() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    const auto s = selected();
    // Les instances ne changent qu'avec le bloc : une ligne choisie sous le
    // meme bloc garde la liste (et son defilement).
    if (s != shownBlock_) {
        shownBlock_ = s;
        instanceModel_->modelReset->emit();
    }
    const bool editable = hosts_.project && hosts_.project() != nullptr;
    if (!p || s >= infos_.size()) {
        block_->set({}, {}, {}, 0);
        props_->setCategories({});
        return;
    }
    const auto& info = infos_[s];
    std::vector<DfbBlockView::Pin> left, right;
    for (const auto v : info.itf.inputs) left.push_back({text(*p, p->variables[v].name), typeOf(*p, v), false});
    for (const auto v : info.itf.inouts) left.push_back({text(*p, p->variables[v].name), typeOf(*p, v), true});
    for (const auto v : info.itf.outputs) right.push_back({text(*p, p->variables[v].name), typeOf(*p, v), false});
    block_->set(info.name, std::move(left), std::move(right), info.itf.publics.size());

    std::vector<PG::Category> cats;
    // Lot API 7 : la ligne choisie sous le bloc d'abord (en lecture), puis le bloc.
    if (const auto* row = selectedRow()) {
        if (row->kind == Row::Variable && row->variable < p->variables.size()) {
            const auto& v = p->variables[row->variable];
            PG::Category k;
            k.name = "Variable " + text(*p, v.name);
            k.properties.push_back(ro("Type", text(*p, v.type.name)));
            k.properties.push_back(ro("Port\xC3\xA9" "e", folderWord(row->folder)));
            k.properties.push_back(ro("Valeur initiale", text(*p, v.initValue)));
            k.properties.push_back(ro("Commentaire", text(*p, v.comment)));
            k.properties.push_back(ro("Dans", info.name + " \xC2\xB7 " + folderLabel(row->folder)));
            cats.push_back(std::move(k));
        } else if (row->kind == Row::Member) {
            cats.push_back(memberCategory(row->node, row->comment,
                                          row->node.real ? "Depuis le bloc : une instance de " + info.name + " le lit en Instance"
                                                               + pathUnder(row->node, info.name) + "."
                                                         : std::string{}));
        } else if (row->kind == Row::Section && row->section < p->sections.size()) {
            const auto& sec = p->sections[row->section];
            PG::Category k;
            k.name = "Section " + text(*p, sec.name);
            k.properties.push_back(ro("Langage", std::string(domain::toString(sec.language))));
            k.properties.push_back(ro("Lignes", thousands(sec.lineCount), "Double-clic sur la ligne, ou \xC2\xAB Ouvrir le code \xC2\xBB : son code."));
            cats.push_back(std::move(k));
        }
    }
    PG::Category c;
    c.name = "Bloc " + info.name;
    const auto pou = info.pou;
    const std::string oldName = info.name;
    c.properties.push_back(renameProperty("Nom", info.name, editable, "Renommer le bloc : le type de ses instances suit (un Ctrl+Z).",
                                          [this, pou, oldName](std::string_view value) {
                                              // Lot 7 : le dialogue qui montre tout ce qui suit.
                                              if (value != oldName && requestRename("dfb", oldName, std::string(value))) return false;
                                              auto doc = hosts_.project ? hosts_.project() : nullptr;
                                              if (!doc || !hosts_.apply) return false;
                                              const auto problem = project::renameProblem(*doc, project::RenameCommand::What::Block, pou, value);
                                              if (!problem.empty()) {
                                                  if (hosts_.status) hosts_.status(problem);
                                                  return false;
                                              }
                                              renameOwner(expanded_, "bfm", oldName, std::string(value));
                                              keep_ = "b:" + std::string(value);
                                              hosts_.apply(std::make_unique<project::RenameCommand>(doc, project::RenameCommand::What::Block, pou, std::string(value)));
                                              return true;
                                          }));
    c.properties.push_back(ro("Version", info.version.empty() ? std::string("\xE2\x80\x94") : info.version));
    c.properties.push_back(ro("Sections", std::to_string(info.sections)));
    c.properties.push_back(ro("Instances", std::to_string(info.instances.size())));
    c.properties.push_back(ro("Biblioth\xC3\xA8que", info.lib.label(),
                              info.lib.kind == LibState::Different ? "La biblioth\xC3\xA8que a la version " + info.lib.version + " : \xC2\xAB Mettre \xC3\xA0 jour \xC2\xBB touche toutes les instances en une action."
                                                                   : std::string{}));
    cats.push_back(std::move(c));
    const auto pins = [&](std::string title, const std::vector<domain::Index>& list, std::size_t max) {
        PG::Category k;
        k.name = std::move(title) + "  " + std::to_string(list.size());
        for (std::size_t i = 0; i < list.size() && i < max; ++i) {
            const auto& v = p->variables[list[i]];
            const std::string comment = text(*p, v.comment);
            k.properties.push_back(ro(text(*p, v.name), text(*p, v.type.name), comment));
        }
        if (list.size() > max) k.properties.push_back(ro("", "\xE2\x80\xA6 et " + std::to_string(list.size() - max) + " autres"));
        if (list.empty()) k.properties.push_back(ro("", "aucune"));
        k.expanded = !list.empty();
        cats.push_back(std::move(k));
    };
    pins("Entr\xC3\xA9" "es", info.itf.inputs, 24);
    pins("Sorties", info.itf.outputs, 24);
    if (!info.itf.inouts.empty()) pins("Entr\xC3\xA9" "es / sorties", info.itf.inouts, 24);
    pins("Publiques", info.itf.publics, 16);
    props_->setCategories(std::move(cats));
    if (frame_) {
        std::string hint = plural(infos_.size(), "bloc", "blocs") + " \xC2\xB7 " + info.name + " : " + plural(info.instances.size(), "instance", "instances");
        if (info.lib.kind == LibState::Different)
            hint += " \xC2\xB7 " + info.lib.version + " dans la biblioth\xC3\xA8que (le projet a " + (info.version.empty() ? std::string("?") : info.version)
                  + ") : Mettre \xC3\xA0 jour touche les " + std::to_string(info.instances.size()) + " instances en une action.";
        else
            hint += " \xC2\xB7 la fl\xC3\xA8" "che d\xC3\xA9plie un bloc (ses variables, leurs membres, ses sections) ; "
                    "double-clic sur une instance : la variable, o\xC3\xB9 elle est \xC3\xA9" "crite et lue.";
        frame_->setHint(hint, info.lib.kind == LibState::Different ? ui::Tone::Warning : ui::Tone::None);
    }
}

// « Ouvrir le code » : la section choisie ; la seule d'un bloc ; sinon un menu
// de ses sections (nom, langage, lignes) - sous le bouton, ou sous la ligne
// double-cliquee.
void DfbPane::openCode(bool fromTable) {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    const auto s = selected();
    if (!p || s >= infos_.size() || !hosts_.request) return;
    const auto* row = selectedRow();
    if (row && row->kind == Row::Section) {
        hosts_.request("section:" + std::to_string(row->section));
        return;
    }
    const auto& info = infos_[s];
    if (info.pou >= p->pous.size()) return;
    const auto& sections = p->pous[info.pou].sections;
    if (sections.empty()) {
        if (hosts_.status) hosts_.status("Le bloc " + info.name + " n'a pas de section : il n'y a pas de code \xC3\xA0 ouvrir.");
        return;
    }
    if (sections.size() == 1) {
        hosts_.request("section:" + std::to_string(sections.front()));
        return;
    }
    menuSections_.clear();
    std::vector<ui::PopupMenu::Item> items;
    ui::PopupMenu::Item head;
    head.label = "Quelle section de " + info.name + " ouvrir ?";
    head.heading = true;
    items.push_back(std::move(head));
    for (const auto sec : sections) {
        if (sec >= p->sections.size()) continue;
        const auto& x = p->sections[sec];
        // Le libelle : le nom seul (un script choisit "<section>") ; le reste a droite.
        ui::PopupMenu::Item it;
        it.label = text(*p, x.name);
        it.shortcut = std::string(domain::toString(x.language)) + "  \xC2\xB7  " + plural(x.lineCount, "ligne", "lignes");
        it.icon = ui::Icon::Section;
        it.id = static_cast<int>(menuSections_.size());
        menuSections_.push_back(sec);
        items.push_back(std::move(it));
    }
    menu_->setItems(std::move(items));
    gfx::Point at{};
    bool placed = false;
    if (fromTable) {
        const auto sel = table_->selectedModelRows();
        for (std::size_t i = 0; i < table_->visibleRowCount() && !sel.empty(); ++i) {
            if (table_->viewRow(i) != sel.front()) continue;
            gfx::Rect rr{};
            if (table_->rowRect(i, rr)) {
                at = {rr.x + 40.f, rr.bottom() + 2.f};
                placed = true;
            }
            break;
        }
    }
    if (!placed && frame_) {
        const auto r = frame_->tools().rectOf(AOpenCode);
        if (r.w > 0.f) {
            at = {r.x, r.bottom() + 2.f};
            placed = true;
        }
    }
    if (!placed) at = {table_->bounds().x + 40.f, table_->bounds().y + 30.f};
    const auto surface = ui::surfaceSize();
    menu_->openAt(at, surface.w > 0 ? surface : gfx::Size{bounds().right(), bounds().bottom()});
}

void DfbPane::runAction(int action) {
    const auto s = selected();
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    const auto* row = selectedRow();
    const bool blockRow = row && row->kind == Row::Block;
    switch (action) {
        case AAddBlock:
            if (hosts_.request) hosts_.request("create.dfb");
            return;
        case ARename: {
            // Lot 7 : le dialogue qui montre tout ce qui suit ; sans lui, comme avant.
            if (s < infos_.size() && blockRow && requestRename("dfb", infos_[s].name)) return;
            if (s >= infos_.size() || !hosts_.ask || !blockRow) return;
            const auto pou = infos_[s].pou;
            const std::string old = infos_[s].name;
            hosts_.ask("Renommer " + old, "Le nouveau nom du bloc : le type de ses " + plural(infos_[s].instances.size(), "instance", "instances") + " suit.", old,
                       [this, pou, old](const std::string& value) {
                           auto doc = hosts_.project ? hosts_.project() : nullptr;
                           if (!doc || !hosts_.apply) return;
                           const auto problem = project::renameProblem(*doc, project::RenameCommand::What::Block, pou, value);
                           if (!problem.empty()) {
                               if (hosts_.status) hosts_.status(problem);
                               return;
                           }
                           renameOwner(expanded_, "bfm", old, value);
                           keep_ = "b:" + value;
                           hosts_.apply(std::make_unique<project::RenameCommand>(doc, project::RenameCommand::What::Block, pou, value));
                           if (hosts_.status) hosts_.status("Bloc renomm\xC3\xA9 en " + value + " : ses instances suivent. Ctrl+Z le d\xC3\xA9" "fait.");
                       });
            return;
        }
        case ADelete:
            if (s < infos_.size() && hosts_.remove && blockRow) hosts_.remove(domain::EntityKind::Pou, infos_[s].pou);
            return;
        case AOpenCode:
            openCode(false);
            return;
        case AUpdate:
            if (hosts_.request) hosts_.request("bibliotheque");
            return;
        case AInstance: {
            if (s >= infos_.size() || !hosts_.ask || !p) return;
            const std::string block = infos_[s].name;
            // Un nom libre : Bloc_1, Bloc_2...
            std::string proposal;
            for (int n = 1; n < 1000 && proposal.empty(); ++n) {
                const std::string candidate = block + "_" + std::to_string(n);
                bool used = false;
                for (const auto& v : p->variables)
                    used = used || (v.scope == domain::VariableScope::Global && lower(text(*p, v.name)) == lower(candidate));
                if (!used) proposal = candidate;
            }
            hosts_.ask("Cr\xC3\xA9" "er une instance de " + block, "Le nom de la variable globale (de type " + block + ") ; l'appeler ensuite depuis une section.",
                       proposal, [this, block](const std::string& value) {
                           auto doc = hosts_.project ? hosts_.project() : nullptr;
                           if (!doc || !hosts_.apply || value.empty()) return;
                           project::AddVariableCommand::Spec spec;
                           spec.name = value;
                           spec.type = block;
                           spec.scope = domain::VariableScope::Global;
                           keep_ = "b:" + block;
                           hosts_.apply(std::make_unique<project::AddVariableCommand>(doc, std::move(spec)));
                           if (hosts_.status) hosts_.status("Instance " + value + " cr\xC3\xA9\xC3\xA9" "e (Ctrl+Z la retire) : l'appeler depuis une section, " + value + "(...);");
                       });
            return;
        }
        default: return;
    }
}

void DfbPane::onLayout() {
    const auto b = bounds();
    const float rightW = std::clamp(b.w * 0.25f, 260.f, 400.f);
    const float mainW = b.w - rightW - 1.f;
    // Lot API 7 : la table grandit avec ce qu'on deplie (jusqu'a 60 % de la hauteur).
    const bool unfolded = rows_.size() > infos_.size();
    const float tableH = std::min(b.h * (unfolded ? 0.6f : 0.42f), 34.f + 26.f * static_cast<float>(std::max<std::size_t>(rows_.size(), 3u)) + 10.f);
    table_->setBounds({b.x, b.y, mainW, tableH});
    const float cardsY = b.y + tableH + 12.f, cardsH = std::max(0.f, b.bottom() - cardsY - 12.f);
    const float blockW = std::clamp(mainW * 0.42f, 320.f, 560.f);
    cardBlock_ = {b.x + 12.f, cardsY, blockW, cardsH};
    cardInstances_ = {cardBlock_.right() + 12.f, cardsY, std::max(0.f, b.x + mainW - 12.f - cardBlock_.right() - 12.f), cardsH};
    block_->setBounds({cardBlock_.x + 1.f, cardBlock_.y + 30.f, cardBlock_.w - 2.f, std::max(0.f, cardBlock_.h - 31.f)});
    instances_->setBounds({cardInstances_.x + 1.f, cardInstances_.y + 30.f, cardInstances_.w - 2.f, std::max(0.f, cardInstances_.h - 31.f)});
    props_->setBounds({b.right() - rightW, b.y, rightW, b.h});
    // Le choix de la section (Ouvrir le code) : des bornes reelles, sinon le menu
    // ouvert n'est pas peint (un widget vide ne va pas dans la passe du dessus).
    if (menu_) menu_->setBounds(b);
}

void DfbPane::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    r.fillRect(bounds(), c.windowBg);
    r.fillRect({props_->bounds().x - 1.f, bounds().y, 1.f, bounds().h}, c.border);
    const auto s = selected();
    const auto card = [&](const gfx::Rect& box, ui::Icon icon, const std::string& title) {
        if (box.w <= 0.f || box.h <= 0.f) return;
        r.fillRect(box, c.panelBg);
        r.strokeRect(box, c.border, 1.f);
        ui::drawIcon(r, icon, {box.x + 10.f, box.y + 7.f, 16.f, 16.f}, ctx.theme.tone(ui::Tone::Info, c.accent));
        r.drawText({box.x + 34.f, box.y + 7.f}, title, kSmall, c.textMuted);
        r.fillRect({box.x, box.y + 29.f, box.w, 1.f}, c.border);
    };
    card(cardBlock_, ui::Icon::FunctionBlock, "LE BLOC");
    card(cardInstances_, ui::Icon::Variable,
         "SES INSTANCES" + (s < infos_.size() ? std::string("  ") + std::to_string(infos_[s].instances.size()) : std::string{}));
}

// ======================================================================
//                          UNITES DE PROGRAMME
// ======================================================================
namespace {

// Une portee collee depuis Excel : "Entree", "IN", "VAR_INPUT", "E/S", "locale"...
// Faux : pas une portee.
bool parseScope(std::string_view text, domain::VariableScope& out) {
    std::string k = paste::normalizedTitle(text);
    k.erase(std::remove(k.begin(), k.end(), '/'), k.end());
    static const std::pair<const char*, domain::VariableScope> kWords[] = {
        {"entree", domain::VariableScope::Input},   {"in", domain::VariableScope::Input},     {"input", domain::VariableScope::Input},
        {"varinput", domain::VariableScope::Input}, {"e", domain::VariableScope::Input},       {"entrees", domain::VariableScope::Input},
        {"sortie", domain::VariableScope::Output},  {"out", domain::VariableScope::Output},    {"output", domain::VariableScope::Output},
        {"varoutput", domain::VariableScope::Output}, {"s", domain::VariableScope::Output},    {"sorties", domain::VariableScope::Output},
        {"entreesortie", domain::VariableScope::InOut}, {"es", domain::VariableScope::InOut},  {"inout", domain::VariableScope::InOut},
        {"varinout", domain::VariableScope::InOut}, {"entreessorties", domain::VariableScope::InOut},
        {"locale", domain::VariableScope::Local},   {"local", domain::VariableScope::Local},   {"privee", domain::VariableScope::Local},
        {"private", domain::VariableScope::Local},  {"var", domain::VariableScope::Local},     {"locales", domain::VariableScope::Local},
        {"publique", domain::VariableScope::Public}, {"public", domain::VariableScope::Public},
    };
    for (const auto& [word, scope] : kWords)
        if (k == word) {
            out = scope;
            return true;
        }
    return false;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// "Cuve_1.Niveau", "Tab[3]" : le nom de la variable du projet au debut.
std::string rootName(std::string_view expression) {
    std::size_t i = 0;
    while (i < expression.size() && (std::isalnum(static_cast<unsigned char>(expression[i])) || expression[i] == '_')) ++i;
    return std::string(expression.substr(0, i));
}

} // namespace

class UnitsPane::Model final : public ui::ITableModel {
public:
    explicit Model(UnitsPane& pane) : pane_(pane) {}
    enum Col : std::size_t { CName, CLanguage, CLines, CRank, CParams, CComment, CCount };
    [[nodiscard]] std::size_t rowCount() const override { return pane_.rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return CCount; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"Unit\xC3\xA9 / section", "Langage", "Lignes", "Rang", "Param\xC3\xA8tres", "Commentaire"};
        if (c == CRank) return "Rang dans " + pane_.task_;
        return c < CCount ? kTitles[c] : "";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        const auto p = pane_.hosts_.view ? pane_.hosts_.view() : nullptr;
        if (!p || r >= pane_.rows_.size()) return {};
        const auto& row = pane_.rows_[r];
        if (row.kind == Row::Unit) {
            if (row.pou >= p->pous.size()) return {};
            const auto& pou = p->pous[row.pou];
            std::size_t lines = 0;
            for (const auto s : pou.sections) if (s < p->sections.size()) lines += p->sections[s].lineCount;
            switch (c) {
                case CName: return text(*p, pou.name);
                case CLanguage: return plural(pou.sections.size(), "section", "sections");
                case CLines: return thousands(lines);
                case CRank: {
                    const auto rank = pane_.rankOf(row.pou);
                    return rank ? std::to_string(rank) : std::string("\xE2\x80\x94");
                }
                case CParams: return std::to_string(pou.parameters.size());
                default: return {};
            }
        }
        if (row.kind == Row::Group) {
            // Lot API 7 : un groupe par portee. Deplie, il titre les colonnes
            // pour les variables qui suivent : ce ne sont plus celles d'une unite.
            if (c == CName) return std::string(folderLabel(row.group)) + "  " + std::to_string(row.count);
            if (!row.open) return {};
            switch (c) {
                case CLanguage: return "Type";
                case CLines: return "Port\xC3\xA9" "e";
                case CRank: return row.group <= FInOut ? std::string("Re\xC3\xA7oit (variable du projet)") : std::string{};
                case CParams: return "Valeur initiale";
                default: return "Commentaire";
            }
        }
        if (row.kind == Row::Variable) {
            if (row.variable >= p->variables.size()) return {};
            const auto& v = p->variables[row.variable];
            switch (c) {
                case CName: return text(*p, v.name);
                case CLanguage: return text(*p, v.type.name);
                case CLines: return scopeLabel(v.scope);
                case CRank: {
                    if (!isParameter(v.scope)) return {};
                    const auto linked = effectiveOf(v);
                    return linked.empty() ? std::string("pas reli\xC3\xA9") : "\xE2\x87\x84 " + linked;
                }
                case CParams: return text(*p, v.initValue);
                default: return text(*p, v.comment);
            }
        }
        if (row.kind == Row::Member) {
            // Son type, ce qu'il est (une broche, une publique...) sous
            // « Portee », le commentaire de sa declaration.
            switch (c) {
                case CName: return row.node.label;
                case CLanguage: return memberType(row.node);
                case CLines: return memberNote(row.node);
                case CComment: return row.comment;
                default: return {};
            }
        }
        if (row.section >= p->sections.size()) return {};
        const auto& s = p->sections[row.section];
        const std::string name = text(*p, s.name);
        switch (c) {
            case CName: return name;
            case CLanguage: {
                std::string l(domain::toString(s.language));
                if (lower(name).rfind("sfc_", 0) == 0 || s.language == domain::PouLanguage::SFC) l += "  \xC2\xB7 grafcet";
                return l;
            }
            case CLines: return thousands(s.lineCount);
            default: return {};
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= pane_.rows_.size()) return s;
        const auto& row = pane_.rows_[r];
        if (row.kind == Row::Unit) {
            if (c == CName) {
                s.bold = true;
                s.icon = ui::Icon::Program;
                s.iconTone = ui::Tone::Family1;
                s.expander = row.open ? 1 : 0;
                if (const auto p = pane_.hosts_.view ? pane_.hosts_.view() : nullptr)
                    codeIconCell(s, project::codeicons::pouIcon(*p, row.pou));      // 1.8.0
            } else if (c == CLanguage) {
                s.fgTone = ui::Tone::Muted;
            }
            return s;
        }
        if (row.kind == Row::Group) {
            s.fgTone = ui::Tone::Muted;
            s.bold = true;
            if (c == CName) {
                s.indent = kStep;
                s.icon = row.open ? ui::Icon::FolderOpen : ui::Icon::Folder;
                s.iconTone = folderTone(row.group);
                s.expander = row.open ? 1 : 0;
            }
            return s;
        }
        if (row.kind == Row::Variable) {
            const auto p = pane_.hosts_.view ? pane_.hosts_.view() : nullptr;
            if (!p || row.variable >= p->variables.size()) return s;
            const auto& v = p->variables[row.variable];
            switch (c) {
                case CName:
                    s.indent = 2.f * kStep;
                    s.expander = row.expandable ? (row.open ? 1 : 0) : -1;
                    s.icon = row.icon;
                    s.iconTone = scopeTone(v.scope);
                    break;
                case CLanguage: s.monospace = true; break;
                case CLines: s.fgTone = scopeTone(v.scope); break;
                case CRank:
                    if (isParameter(v.scope)) s.fgTone = effectiveOf(v).empty() ? ui::Tone::Warning : ui::Tone::Info;
                    break;
                case CComment: s.fgTone = ui::Tone::Muted; break;
                default: break;
            }
            return s;
        }
        if (row.kind == Row::Member) {
            if (c == CName) {
                memberNameStyle(s, row.node, row.icon, row.tone, 2.f * kStep + kStep * static_cast<float>(row.depth), row.expandable, row.open);
            } else if (c == CLanguage) {
                s.monospace = true;
                s.fgTone = ui::Tone::Muted;
            } else if (c == CLines) {
                s.fgTone = row.tone == ui::Tone::None ? ui::Tone::Muted : row.tone;
            } else if (c == CComment) {
                s.fgTone = ui::Tone::Muted;
            }
            return s;
        }
        if (c == CName) {
            s.indent = kStep;
            s.icon = ui::Icon::Section;
            s.iconTone = ui::Tone::Info;
            if (const auto p = pane_.hosts_.view ? pane_.hosts_.view() : nullptr)
                codeIconCell(s, project::codeicons::sectionIcon(*p, row.section));      // 1.8.0
        } else if (c == CLanguage) {
            s.fgTone = ui::Tone::Muted;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(RowIndex r) const override {
        if (r >= pane_.rows_.size()) return {};
        const auto& row = pane_.rows_[r];
        if (row.kind == Row::Unit)
            return "La fl\xC3\xA8" "che d\xC3\xA9plie ses sections et ses variables, rang\xC3\xA9" "es par port\xC3\xA9" "e ; "
                   "l'unit\xC3\xA9 tourne en bloc, \xC3\xA0 son rang dans la t\xC3\xA2" "che.";
        if (row.kind == Row::Group)
            return "Coller depuis Excel (Ctrl+V) : Nom, Type, Port\xC3\xA9" "e, Re\xC3\xA7oit, Valeur initiale, Commentaire - un nom nouveau "
                   "cr\xC3\xA9" "e la variable dans l'unit\xC3\xA9 ; coll\xC3\xA9" "e sur ce titre, elle a cette port\xC3\xA9" "e (sauf colonne Port\xC3\xA9" "e).";
        if (row.kind == Row::Variable)
            return row.expandable ? std::string("La fl\xC3\xA8" "che d\xC3\xA9plie ses membres ; double-clic : ses usages. "
                                                "Ses cases se changent \xC3\xA0 droite, ou en collant depuis Excel.")
                                  : std::string("Double-clic : ses usages. Ses cases se changent \xC3\xA0 droite, ou en collant depuis Excel.");
        if (row.kind == Row::Member) return memberTooltip(row.node, row.comment, row.expandable);
        return "Double-clic : le code de la section.";
    }
private:
    UnitsPane& pane_;
};

UnitsPane::UnitsPane(std::string id) : ui::Widget(std::move(id)) {
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(this->id() + ".unites")));
    model_ = std::make_shared<Model>(*this);
    table_->setModel(model_);
    columns(*table_, *model_, {380.f, 190.f, 110.f, 200.f, 130.f, 320.f}, {Model::CLines, Model::CParams});
    // Lot recherche : les filtres des colonnes (la table les applique) - ils
    // choisissent des unites ; leurs sections et leurs variables les suivent.
    table_->setColumnFiltersEnabled(true);
    table_->setSelectionMode(ui::SelectionMode::Extended);   // 1.8.0 : plusieurs sections (Comparer, Exporter, Icone)
    links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>&) {
        if (syncing_) return;
        refreshProperties();
    });
    // Deplier, replier : une unite, un groupe (replie a part), une variable ou
    // un membre (sur ses membres). Faux : rien a deplier sur cette ligne.
    const auto toggle = [this](RowIndex r) {
        if (r >= rows_.size()) return false;
        const auto& row = rows_[r];
        std::string key;
        bool folded = false;
        switch (row.kind) {
            case Row::Unit: key = lower(row.key); break;                          // "u:<unite>"
            case Row::Group: key = lower(row.key); folded = true; break;          // "g:<unite>/<groupe>"
            case Row::Variable:
            case Row::Member:
                if (!row.expandable) return false;
                key = "m:" + lower(row.node.key);
                break;
            default: return false;
        }
        auto& keys = folded ? varsFolded_ : expanded_;
        if (!keys.erase(key)) keys.insert(key);
        keep_ = row.key;
        rebuildRows();
        return true;
    };
    links_ += table_->expanderClicked->connect([toggle](RowIndex r) { (void)toggle(r); });
    links_ += table_->activated->connect([this, toggle](RowIndex r) {
        if (r >= rows_.size()) return;
        const auto kind = rows_[r].kind;
        if (kind == Row::Unit || kind == Row::Group || kind == Row::Member) {
            (void)toggle(r);
            return;
        }
        if (kind == Row::Variable) {
            if (hosts_.request) hosts_.request("variable:" + std::to_string(rows_[r].variable));
            return;
        }
        runAction(AOpenCode);
    });
    // Lot API 6 : coller depuis Excel les variables d'une unite.
    paste_.table = table_;
    paste_.keyColumn = static_cast<int>(Model::CName);
    paste_.target = [this](const ui::TableView::PasteRequest& rq) { return pasteTarget(rq); };
    paste_.refresh = [this] {
        if (hosts_.request) hosts_.request("rafraichir");
        refresh();
    };
    paste_.done = [this](const paste::Report& rep, const paste::Target& t) {
        pasteFresh_ = true;
        if (hosts_.status) hosts_.status(rep.status(t));
        const auto p = hosts_.view ? hosts_.view() : nullptr;
        const auto* row = selectedRow();
        if (!p || !row || row->pou >= p->pous.size()) return;
        const std::string unit = text(*p, p->pous[row->pou].name);
        if (!rep.createdKeys.empty()) (void)selectVariable(unit, rep.createdKeys.front());
        else if (!rep.updatedKeys.empty()) (void)selectVariable(unit, rep.updatedKeys.front());
    };
    paste::bind(paste_);
    props_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(this->id() + ".proprietes")));
    props_->setShowDescriptionPane(true);
}

UnitsPane::~UnitsPane() = default;

void UnitsPane::setHosts(ApiPaneHosts h) {
    hosts_ = std::move(h);
    refresh();
}

void UnitsPane::attach(ApiFrame& frame) {
    frame_ = &frame;
    auto& t = frame.tools();
    t.add(AAddUnit, HmiGlyph::Plus, "Cr\xC3\xA9" "er une unit\xC3\xA9 de programme", "Unit\xC3\xA9");
    t.add(AAddSection, HmiGlyph::Plus, "Ajouter une section (ST) \xC3\xA0 l'unit\xC3\xA9 choisie", "Section");
    t.add(AAddVariable, HmiGlyph::Plus,
          "Ajouter une variable \xC3\xA0 l'unit\xC3\xA9 choisie : locale, ou de la port\xC3\xA9" "e du groupe choisi (ou coller ses variables depuis Excel)",
          "Variable");
    t.add(ARename, HmiGlyph::Text, "Renommer l'unit\xC3\xA9, la section ou la variable choisie", "Renommer");
    t.add(ADelete, HmiGlyph::Delete, "Supprimer l'unit\xC3\xA9, la section ou la variable choisie (Ctrl+Z la rend)", "Supprimer");
    t.separator();
    t.add(AOpenCode, HmiGlyph::Code, "Ouvrir le code de la section (ou de la premi\xC3\xA8re de l'unit\xC3\xA9)", "Ouvrir le code");
    t.add(AOrder, HmiGlyph::List, "L'unit\xC3\xA9 dans l'ordre d'ex\xC3\xA9" "cution de sa t\xC3\xA2" "che", "Voir dans l'ordre");
    // 1.8.0 : sur les sections choisies (Ctrl+clic, Maj+clic).
    t.separator();
    t.add(ACompare, HmiGlyph::Compare,
          "Comparer les sections choisies (Ctrl+clic pour en choisir plusieurs ; une seule : avec celle dont le nom lui ressemble)", "Comparer");
    t.add(AExport, HmiGlyph::Export, "Exporter le programme lisible (Excel, PDF, texte) : les sections choisies, ou l'unit\xC3\xA9", "Exporter");
    t.add(AIcon, HmiGlyph::Image, "L'ic\xC3\xB4ne de l'unit\xC3\xA9 ou des sections choisies : ce qu'elles font", "Ic\xC3\xB4ne");
    const auto editable = [this] { return hosts_.project && hosts_.project() != nullptr; };
    // Un titre de groupe, un membre : rien a renommer ni a supprimer.
    const auto named = [this] {
        const auto* row = selectedRow();
        return row && (row->kind == Row::Unit || row->kind == Row::Section || row->kind == Row::Variable);
    };
    t.setEnabledWhen(AAddUnit, editable);
    t.setEnabledWhen(AAddSection, [this, editable] { return editable() && selectedRow() != nullptr; });
    t.setEnabledWhen(AAddVariable, [this, editable] { return editable() && selectedRow() != nullptr; });
    t.setEnabledWhen(ARename, [editable, named] { return editable() && named(); });
    t.setEnabledWhen(ADelete, [editable, named] { return editable() && named(); });
    t.setEnabledWhen(AOpenCode, [this] { return selectedRow() != nullptr; });
    t.setEnabledWhen(AOrder, [this] { return selectedRow() != nullptr; });
    t.setEnabledWhen(ACompare, [this] { return !selectedSections().empty(); });
    t.setEnabledWhen(AExport, [this] { return static_cast<bool>(hosts_.request); });
    t.setEnabledWhen(AIcon, [this, editable] {
        const auto* row = selectedRow();
        return editable() && row && (row->kind == Row::Unit || row->kind == Row::Section);
    });
    links_ += t.triggered->connect([this](int a) { runAction(a); });
    frame.setHint("Double-clic sur une section : son code. Une unit\xC3\xA9 d\xC3\xA9pli\xC3\xA9" "e montre ses variables rang\xC3\xA9" "es par port\xC3\xA9" "e, "
                  "et chacune se d\xC3\xA9plie sur ses membres ; Ctrl+V y colle celles d'Excel (Nom, Type, Port\xC3\xA9" "e...). "
                  "Renommer une unit\xC3\xA9 : ses tables d'animation suivent.");
}

const UnitsPane::Row* UnitsPane::selectedRow() const {
    const auto sel = table_->selectedModelRows();
    return sel.empty() || sel.front() >= rows_.size() ? nullptr : &rows_[sel.front()];
}

std::vector<domain::Index> UnitsPane::selectedSections() const {
    std::vector<domain::Index> out;
    for (const auto r : table_->selectedModelRows())
        if (r < rows_.size() && rows_[r].kind == Row::Section && rows_[r].section != domain::kNoIndex
            && std::find(out.begin(), out.end(), rows_[r].section) == out.end())
            out.push_back(rows_[r].section);
    return out;
}

std::size_t UnitsPane::rankOf(domain::Index pou) const {
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].unit && entries_[i].unitIndex == pou) return i + 1;
    return 0;
}

void UnitsPane::renameUnitState(const std::string& before, const std::string& after) {
    renameOwner(expanded_, "um", before, after);
    renameOwner(varsFolded_, "g", before, after);
}

void UnitsPane::refresh() {
    // Une modification apres le collage : son bandeau ne dit plus vrai.
    if (!paste_.pasting) {
        if (pasteFresh_) pasteFresh_ = false;
        else paste::forget(paste_);
    }
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    entries_.clear();
    task_ = "MAST";
    if (p) {
        task_ = p->tasks.empty() ? std::string("MAST") : text(*p, p->tasks.front().name);
        entries_ = project::api::entriesOf(*p, task_);
        if (firstFill_) {
            // Au premier affichage : la premiere unite depliee (ses sections se voient).
            for (const auto& pou : p->pous)
                if (pou.kind == domain::PouKind::ProgramUnit) {
                    expanded_.insert("u:" + lower(text(*p, pou.name)));
                    break;
                }
            firstFill_ = false;
        }
    }
    rebuildRows();
}

void UnitsPane::rebuildRows() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    if (keep_.empty())
        if (const auto* row = selectedRow()) keep_ = row->key;
    rows_.clear();
    if (p) {
        std::vector<domain::Index> units;
        for (domain::Index i = 0; i < p->pous.size(); ++i)
            if (p->pous[i].kind == domain::PouKind::ProgramUnit) units.push_back(i);
        // Dans l'ordre de la tache (les autres a la fin).
        std::stable_sort(units.begin(), units.end(), [this](domain::Index a, domain::Index b) {
            const auto ra = rankOf(a), rb = rankOf(b);
            return (ra ? ra : 100000u) < (rb ? rb : 100000u);
        });
        for (const auto u : units) {
            const auto& pou = p->pous[u];
            const std::string name = text(*p, pou.name);
            const std::string unitKey = lower(name);
            Row unitRow;
            unitRow.kind = Row::Unit;
            unitRow.pou = u;
            unitRow.key = "u:" + name;
            unitRow.open = expanded_.count("u:" + unitKey) != 0;
            rows_.push_back(unitRow);
            if (!unitRow.open) continue;
            for (const auto s : pou.sections) {
                Row r;
                r.kind = Row::Section;
                r.pou = u;
                r.section = s;
                r.key = "s:" + (s < p->sections.size() ? text(*p, p->sections[s].name) : std::string{});
                rows_.push_back(r);
            }
            // Lot API 7 : ses variables rangees par portee (parametres puis
            // locales, dans l'ordre de la declaration), un groupe par portee non vide.
            std::array<std::vector<domain::Index>, static_cast<std::size_t>(FSections)> byFolder;
            for (const auto* list : {&pou.parameters, &pou.locals})
                for (const auto v : *list)
                    if (v < p->variables.size()) byFolder[static_cast<std::size_t>(folderOf(p->variables[v].scope))].push_back(v);
            for (int g = 0; g < FSections; ++g) {
                const auto& vars = byFolder[static_cast<std::size_t>(g)];
                if (vars.empty()) continue;
                Row head;
                head.kind = Row::Group;
                head.pou = u;
                head.group = g;
                head.count = vars.size();
                head.key = "g:" + name + "/" + folderId(g);
                head.open = !varsFolded_.count("g:" + unitKey + "/" + folderId(g));
                rows_.push_back(head);
                if (!head.open) continue;
                for (const auto v : vars) {
                    const std::string var = text(*p, p->variables[v].name);
                    Row r;
                    r.kind = Row::Variable;
                    r.pou = u;
                    r.variable = v;
                    r.group = g;
                    r.key = "v:" + name + "/" + var;
                    // Sa racine : "<Unite>.<variable>", le nom que la simulation connait.
                    r.node = mt::root(name + "." + var, typeOf(*p, v));
                    r.decl = v;
                    r.icon = natureIcon(*p, r.node.type);
                    r.expandable = mt::hasChildren(*p, r.node);
                    r.open = r.expandable && expanded_.count("m:" + lower(r.node.key)) != 0;
                    rows_.push_back(r);
                    if (r.open) addMembers(*p, r, 1);
                }
            }
        }
    }
    const float scroll = scrollOf(*table_);
    model_->modelReset->emit();
    keepScroll(*table_, scroll);
    syncing_ = true;
    std::size_t pick = kNpos;
    if (!keep_.empty()) {
        const auto want = lower(keep_);
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (lower(rows_[i].key) == want) {
                pick = i;
                break;
            }
        // Repliee entre-temps : l'ancetre montre le plus proche (un membre, la
        // variable), sinon l'unite qui la porte.
        if (pick == kNpos && want.rfind("m:", 0) == 0) {
            const auto path = want.substr(2);
            std::size_t best = 0;
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                const auto& cand = rows_[i];
                if (cand.kind != Row::Variable && cand.kind != Row::Member) continue;
                const auto k = lower(cand.node.key);
                if (k.size() > best && pathPrefix(k, path)) {
                    best = k.size();
                    pick = i;
                }
            }
        }
        std::string owner;
        if (pick == kNpos && want.rfind("s:", 0) == 0 && p) {
            // Une section (son nom seul dans la cle) : l'unite qui la porte.
            for (const auto& pou : p->pous)
                if (pou.kind == domain::PouKind::ProgramUnit)
                    for (const auto s : pou.sections)
                        if (s < p->sections.size() && owner.empty() && lower(text(*p, p->sections[s].name)) == want.substr(2))
                            owner = "u:" + lower(text(*p, pou.name));
        } else if (pick == kNpos) {
            owner = "u:" + lower(ownerOfKey(want));
        }
        if (pick == kNpos && !owner.empty())
            for (std::size_t i = 0; i < rows_.size(); ++i)
                if (lower(rows_[i].key) == owner) {
                    pick = i;
                    break;
                }
    }
    if (pick == kNpos && !rows_.empty()) pick = 0;
    if (pick != kNpos) table_->selectModelRows({static_cast<RowIndex>(pick)}, false);
    syncing_ = false;
    keep_.clear();
    refreshProperties();
}

void UnitsPane::addMembers(const domain::Project& p, const Row& parent, int depth) {
    for (auto& n : mt::children(p, parent.node)) {
        Row c;
        c.kind = Row::Member;
        c.pou = parent.pou;
        c.group = parent.group;
        c.depth = depth;
        c.expandable = mt::hasChildren(p, n);
        c.decl = mt::declarationOf(p, parent.node, parent.decl, n);
        c.comment = mt::commentOf(p, c.decl, parent.node, n);
        memberLook(p, n, c.icon, c.tone);
        c.key = "m:" + n.key;
        c.open = c.expandable && expanded_.count("m:" + lower(n.key)) != 0;
        c.node = std::move(n);
        rows_.push_back(c);
        if (c.open) addMembers(p, c, depth + 1);      // c : une copie, rows_ grandit dessous
    }
}

namespace {

// L'unite de programme de ce nom (en minuscules) ; nul : aucune.
const domain::Pou* unitNamed(const domain::Project& p, std::string_view wanted) {
    for (const auto& candidate : p.pous)
        if (candidate.kind == domain::PouKind::ProgramUnit && lower(text(p, candidate.name)) == wanted) return &candidate;
    return nullptr;
}

// "g:<unite>/<groupe>" (en minuscules) : l'unite a-t-elle des variables dans ce groupe ?
bool unitGroupExists(const domain::Project& p, std::string_view key) {
    if (key.size() < 3 || key.compare(0, 2, "g:") != 0) return false;
    const auto rest = key.substr(2);
    const auto slash = rest.find('/');
    if (slash == std::string_view::npos) return false;
    const int f = folderById(rest.substr(slash + 1));
    const auto* unit = unitNamed(p, rest.substr(0, slash));
    if (!unit || f < 0 || f >= FSections) return false;
    for (const auto* list : {&unit->parameters, &unit->locals})
        for (const auto v : *list)
            if (v < p.variables.size() && folderOf(p.variables[v].scope) == f) return true;
    return false;
}

} // namespace

bool UnitsPane::setExpanded(std::string_view key, bool open) {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    const std::string k = lower(key);
    if (!p || k.size() < 3 || k[1] != ':') return false;
    const std::string rest = k.substr(2);
    if (k[0] == 'u') {
        if (!unitNamed(*p, rest)) return false;
        if (open) expanded_.insert(k);
        else expanded_.erase(k);
    } else if (k[0] == 'g') {
        if (!unitGroupExists(*p, k)) return false;
        if (open) {
            varsFolded_.erase(k);
            expanded_.insert("u:" + rest.substr(0, rest.find('/')));   // son unite aussi : on le voit
        } else {
            varsFolded_.insert(k);
        }
    } else if (k[0] == 'm') {
        // Une variable ou un membre, montre ou non : on le cherche sous chaque
        // variable ("<unite>.<variable>", sa racine) ; deplie, son unite, son
        // groupe et ses ancetres s'ouvrent avec lui.
        for (const auto& pou : p->pous) {
            if (pou.kind != domain::PouKind::ProgramUnit) continue;
            const std::string unit = text(*p, pou.name);
            for (const auto* list : {&pou.parameters, &pou.locals})
                for (const auto v : *list) {
                    if (v >= p->variables.size()) continue;
                    std::vector<std::string> chain;
                    mt::Node found;
                    if (!memberChain(*p, mt::root(unit + "." + text(*p, p->variables[v].name), typeOf(*p, v)), rest, chain, found)) continue;
                    if (!mt::hasChildren(*p, found)) return false;         // rien a deplier
                    if (open) {
                        expanded_.insert("u:" + lower(unit));
                        varsFolded_.erase("g:" + lower(unit) + "/" + folderId(folderOf(p->variables[v].scope)));
                        expanded_.insert(chain.begin(), chain.end());
                        expanded_.insert(k);
                    } else {
                        expanded_.erase(k);
                    }
                    rebuildRows();
                    return true;
                }
        }
        return false;
    } else {
        return false;
    }
    rebuildRows();
    return true;
}

bool UnitsPane::isExpanded(std::string_view key) const {
    const std::string k = lower(key);
    if (k.rfind("g:", 0) == 0) {
        // Un groupe est deplie au depart - s'il existe.
        const auto p = hosts_.view ? hosts_.view() : nullptr;
        return p && unitGroupExists(*p, k) && !varsFolded_.count(k);
    }
    return expanded_.count(k) != 0;
}

bool UnitsPane::selectUnit(std::string_view name) {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    if (!p) return false;
    for (const auto& pou : p->pous)
        if (pou.kind == domain::PouKind::ProgramUnit && lower(text(*p, pou.name)) == lower(name)) {
            expanded_.insert("u:" + lower(text(*p, pou.name)));
            keep_ = "u:" + text(*p, pou.name);
            rebuildRows();
            if (const auto* row = selectedRow(); row && row->kind == Row::Unit) {
                refreshProperties();
                return true;
            }
        }
    return false;
}

bool UnitsPane::selectSection(std::string_view name) {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    if (!p) return false;
    for (const auto& pou : p->pous) {
        if (pou.kind != domain::PouKind::ProgramUnit) continue;
        for (const auto s : pou.sections)
            if (s < p->sections.size() && lower(text(*p, p->sections[s].name)) == lower(name)) {
                expanded_.insert("u:" + lower(text(*p, pou.name)));
                keep_ = "s:" + text(*p, p->sections[s].name);
                rebuildRows();
                return true;
            }
    }
    return false;
}

bool UnitsPane::selectVariable(std::string_view unit, std::string_view name) {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    if (!p) return false;
    for (const auto& pou : p->pous) {
        if (pou.kind != domain::PouKind::ProgramUnit || lower(text(*p, pou.name)) != lower(unit)) continue;
        const std::string u = text(*p, pou.name);
        expanded_.insert("u:" + lower(u));
        // Lot API 7 : le groupe de sa portee se deplie aussi.
        for (const auto* list : {&pou.parameters, &pou.locals})
            for (const auto v : *list)
                if (v < p->variables.size() && lower(text(*p, p->variables[v].name)) == lower(name))
                    varsFolded_.erase("g:" + lower(u) + "/" + folderId(folderOf(p->variables[v].scope)));
        keep_ = "v:" + u + "/" + std::string(name);
        rebuildRows();
        const auto* row = selectedRow();
        return row && row->kind == Row::Variable;
    }
    return false;
}

void UnitsPane::refreshProperties() {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    const auto* row = selectedRow();
    const bool editable = hosts_.project && hosts_.project() != nullptr;
    std::vector<PG::Category> cats;
    if (!p || !row || row->pou >= p->pous.size()) {
        props_->setCategories({});
        return;
    }
    if (row->isVariable() && row->variable < p->variables.size()) {
        using Field = project::SetVariableFieldCommand::Field;
        const auto& v = p->variables[row->variable];
        const auto index = row->variable;
        const std::string unitName = text(*p, p->pous[row->pou].name);
        const std::string varName = text(*p, v.name);
        // Une case : validee, puis une commande (Ctrl+Z la reprend).
        const auto field = [this, index, unitName, varName](Field f) {
            return [this, index, unitName, varName, f](std::string_view value) {
                auto doc = hosts_.project ? hosts_.project() : nullptr;
                if (!doc || !hosts_.apply || index >= doc->variables.size()) return false;
                std::string why;
                const std::string val(value);
                if (f == Field::Type && !apikit::validPlcType(*doc, val, &why)) {
                    if (hosts_.status) hosts_.status(why);
                    return false;
                }
                if (f == Field::Effective && !val.empty()) {
                    const std::string root = rootName(val);
                    bool known = false;
                    for (const auto& g : doc->variables)
                        if (g.scope == domain::VariableScope::Global && lower(text(*doc, g.name)) == lower(root)) known = true;
                    if (!known) {
                        if (hosts_.status) hosts_.status("\xC2\xAB " + root + " \xC2\xBB n'est pas une variable du projet.");
                        return false;
                    }
                }
                keep_ = "v:" + unitName + "/" + varName;
                hosts_.apply(std::make_unique<project::SetVariableFieldCommand>(doc, index, f, val));
                return true;
            };
        };
        PG::Category k;
        k.name = "Variable " + varName;
        k.properties.push_back(renameProperty("Nom", varName, editable, "Renommer la variable : le code de l'unit\xC3\xA9 et ses tables d'animation suivent.",
                                              [this, index, unitName, varName](std::string_view value) {
                                                  // Lot 7 : le dialogue qui montre tout ce qui suit.
                                                  if (value != varName && requestRename("variable", unitName + "." + varName, std::string(value))) return false;
                                                  auto doc = hosts_.project ? hosts_.project() : nullptr;
                                                  if (!doc || !hosts_.apply) return false;
                                                  const auto problem = project::renameProblem(*doc, project::RenameCommand::What::Variable, index, value);
                                                  if (!problem.empty()) {
                                                      if (hosts_.status) hosts_.status(problem);
                                                      return false;
                                                  }
                                                  // Ses membres deplies la suivent.
                                                  moveKeys(expanded_, "m:" + lower(unitName + "." + varName), "m:" + lower(unitName + "." + std::string(value)));
                                                  keep_ = "v:" + unitName + "/" + std::string(value);
                                                  hosts_.apply(std::make_unique<project::RenameCommand>(doc, project::RenameCommand::What::Variable, index,
                                                                                                        std::string(value)));
                                                  return true;
                                              }));
        k.properties.push_back(renameProperty("Type", text(*p, v.type.name), editable,
                                              "Comme dans une d\xC3\xA9" "claration : INT, ARRAY[0..9] OF REAL, un type d\xC3\xA9riv\xC3\xA9, un bloc DFB.", field(Field::Type)));
        k.properties.push_back(ro("Port\xC3\xA9" "e", scopeLabel(v.scope),
                                  isParameter(v.scope) ? std::string("Un param\xC3\xA8tre de l'unit\xC3\xA9 : il re\xC3\xA7oit une variable du projet.")
                                                       : std::string("Une variable de l'unit\xC3\xA9, que ses sections seules voient.")));
        if (isParameter(v.scope))
            k.properties.push_back(renameProperty("Re\xC3\xA7oit", effectiveOf(v), editable,
                                                  "La variable du projet que re\xC3\xA7oit le param\xC3\xA8tre (EffectiveParameter) ; vide : il n'est plus reli\xC3\xA9.",
                                                  field(Field::Effective)));
        k.properties.push_back(renameProperty("Valeur initiale", text(*p, v.initValue), editable, "Au d\xC3\xA9marrage de l'automate.", field(Field::InitValue)));
        k.properties.push_back(renameProperty("Commentaire", text(*p, v.comment), editable, "", field(Field::Comment)));
        k.properties.push_back(ro("Unit\xC3\xA9", unitName));
        cats.push_back(std::move(k));
    }
    // Lot API 7 : un membre, en lecture ; puis l'unite.
    if (row->kind == Row::Member)
        cats.push_back(memberCategory(row->node, row->comment,
                                      row->node.real ? std::string("Le nom que la simulation conna\xC3\xAEt : une table d'animation le prend tel quel.")
                                                     : std::string{}));
    if (row->isSection() && row->section < p->sections.size()) {
        const auto& s = p->sections[row->section];
        PG::Category k;
        k.name = "Section " + text(*p, s.name);
        const auto section = row->section;
        const std::string sectionName = text(*p, s.name);
        k.properties.push_back(renameProperty("Nom", text(*p, s.name), editable, "Renommer la section (ses appels \xC2\xAB Nom(); \xC2\xBB suivent).",
                                              [this, section, sectionName](std::string_view value) {
                                                  // Lot 7 : le dialogue qui montre tout ce qui suit.
                                                  if (value != sectionName && requestRename("section", sectionName, std::string(value))) return false;
                                                  auto doc = hosts_.project ? hosts_.project() : nullptr;
                                                  if (!doc || !hosts_.apply) return false;
                                                  const auto problem = project::renameProblem(*doc, project::RenameCommand::What::Section, section, value);
                                                  if (!problem.empty()) {
                                                      if (hosts_.status) hosts_.status(problem);
                                                      return false;
                                                  }
                                                  keep_ = "s:" + std::string(value);
                                                  hosts_.apply(std::make_unique<project::RenameCommand>(doc, project::RenameCommand::What::Section, section, std::string(value)));
                                                  return true;
                                              }));
        k.properties.push_back(ro("Langage", std::string(domain::toString(s.language))));
        k.properties.push_back(ro("Lignes", thousands(s.lineCount)));
        k.properties.push_back(ro("Unit\xC3\xA9", text(*p, p->pous[row->pou].name)));
        cats.push_back(std::move(k));
    }
    const auto& pou = p->pous[row->pou];
    const std::string name = text(*p, pou.name);
    std::size_t lines = 0, grafcets = 0;
    for (const auto s : pou.sections) {
        if (s >= p->sections.size()) continue;
        lines += p->sections[s].lineCount;
        const std::string sn = lower(text(*p, p->sections[s].name));
        grafcets += sn.rfind("sfc_", 0) == 0 || p->sections[s].language == domain::PouLanguage::SFC ? 1u : 0u;
    }
    PG::Category c;
    c.name = "Unit\xC3\xA9 " + name;
    const auto unit = row->pou;
    c.properties.push_back(renameProperty("Nom", name, editable, "Renommer l'unit\xC3\xA9 : les tables d'animation qu'elle porte suivent (un Ctrl+Z).",
                                          [this, unit, name](std::string_view value) {
                                              // Lot 7 : le dialogue qui montre tout ce qui suit.
                                              if (value != name && requestRename("unite", name, std::string(value))) return false;
                                              auto doc = hosts_.project ? hosts_.project() : nullptr;
                                              if (!doc || !hosts_.apply) return false;
                                              const auto problem = project::renameProblem(*doc, project::RenameCommand::What::Unit, unit, value);
                                              if (!problem.empty()) {
                                                  if (hosts_.status) hosts_.status(problem);
                                                  return false;
                                              }
                                              renameUnitState(text(*doc, doc->pous[unit].name), std::string(value));
                                              keep_ = "u:" + std::string(value);
                                              hosts_.apply(std::make_unique<project::RenameCommand>(doc, project::RenameCommand::What::Unit, unit, std::string(value)));
                                              return true;
                                          }));
    c.properties.push_back(ro("T\xC3\xA2" "che", pou.task ? text(*p, pou.task) : task_));
    const auto rank = rankOf(row->pou);
    c.properties.push_back(ro("Rang", rank ? std::to_string(rank) + " sur " + std::to_string(entries_.size()) : std::string("\xE2\x80\x94"),
                              "L'unit\xC3\xA9 tourne en bloc, avec ses sections, \xC3\xA0 ce rang de la t\xC3\xA2" "che (l'onglet Ordre d'ex\xC3\xA9" "cution la d\xC3\xA9place)."));
    c.properties.push_back(ro("Sections", std::to_string(pou.sections.size()) + (grafcets ? " (dont " + std::to_string(grafcets) + " grafcets)" : std::string{})));
    c.properties.push_back(ro("Lignes", thousands(lines)));
    c.properties.push_back(ro("Variables", std::to_string(pou.parameters.size()) + " param\xC3\xA8tre(s), " + std::to_string(pou.locals.size()) + " locale(s)"));
    cats.push_back(std::move(c));
    if (row->kind != Row::Variable && row->kind != Row::Member) {
        PG::Category params;
        params.name = "Param\xC3\xA8tres  " + std::to_string(pou.parameters.size());
        for (std::size_t i = 0; i < pou.parameters.size() && i < 40; ++i) {
            const auto v = pou.parameters[i];
            if (v >= p->variables.size()) continue;
            const auto& var = p->variables[v];
            const auto linked = effectiveOf(var);
            params.properties.push_back(ro(text(*p, var.name), text(*p, var.type.name) + (linked.empty() ? std::string{} : "  \xE2\x87\x84  " + linked),
                                           linked.empty() ? std::string("Pas reli\xC3\xA9 \xC3\xA0 une variable du projet.")
                                                          : "Re\xC3\xA7oit la variable " + linked + " du projet (EffectiveParameter)."));
        }
        if (pou.parameters.size() > 40) params.properties.push_back(ro("", "\xE2\x80\xA6 et " + std::to_string(pou.parameters.size() - 40) + " autres"));
        if (pou.parameters.empty()) params.properties.push_back(ro("", "aucun"));
        cats.push_back(std::move(params));
    }
    std::size_t tables = 0;
    for (const auto& t : p->animationTables) tables += lower(text(*p, t.owner)) == lower(name) ? 1u : 0u;
    if (tables) {
        PG::Category k;
        k.name = "Tables d'animation";
        for (const auto& t : p->animationTables)
            if (lower(text(*p, t.owner)) == lower(name)) k.properties.push_back(ro(text(*p, t.name), plural(t.entries.size(), "ligne", "lignes")));
        cats.push_back(std::move(k));
    }
    props_->setCategories(std::move(cats));
}

// ---- lot API 6 : coller depuis Excel les variables d'une unite -------------------
paste::Target UnitsPane::pasteTarget(const ui::TableView::PasteRequest& rq) {
    using Field = project::SetVariableFieldCommand::Field;
    paste::Target t;
    t.noun = "variable";
    t.nouns = "variables";
    t.feminine = true;
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    // L'unite de la ligne d'ou l'on colle (l'unite elle-meme, une section, un
    // groupe, une variable, un membre).
    domain::Index unit = domain::kNoIndex;
    // Lot API 7 : colle sur le titre d'un groupe, une variable nouvelle prend la
    // portee du groupe (une colonne Portee collee l'emporte) ; ailleurs, locale.
    domain::VariableScope defaultScope = domain::VariableScope::Local;
    const Row* anchor = nullptr;
    if (const auto r = rq.anchorViewRow < table_->visibleRowCount() ? static_cast<std::size_t>(table_->viewRow(rq.anchorViewRow)) : kNpos; r < rows_.size())
        anchor = &rows_[r];
    else
        anchor = selectedRow();
    if (anchor) {
        unit = anchor->pou;
        if (anchor->kind == Row::Group) defaultScope = folderScope(anchor->group);
    }
    if (!p || unit >= p->pous.size()) {
        t.unknown = "choisis d'abord une unit\xC3\xA9";
        t.exists = [](const std::string&) { return false; };
        t.create = [](const std::string&, const std::map<std::string, std::string>&, paste::Notes&, std::vector<std::string>&, std::string* why) {
            if (why) *why = "choisis d'abord une unit\xC3\xA9 (une ligne de l'unit\xC3\xA9 dans la table)";
            return std::string{};
        };
        return t;
    }
    const std::string unitName = text(*p, p->pous[unit].name);
    t.noun = "variable de " + unitName;
    t.nouns = "variables de " + unitName;
    const auto indexOf = [this, unit](const std::string& key) -> domain::Index {
        const auto q = hosts_.view ? hosts_.view() : nullptr;
        if (!q || unit >= q->pous.size()) return domain::kNoIndex;
        for (const auto* list : {&q->pous[unit].parameters, &q->pous[unit].locals})
            for (const auto v : *list)
                if (v < q->variables.size() && lower(text(*q, q->variables[v].name)) == lower(key)) return v;
        return domain::kNoIndex;
    };
    const auto globalKnown = [](const domain::Project& doc, const std::string& expression) {
        const std::string root = rootName(expression);
        for (const auto& g : doc.variables)
            if (g.scope == domain::VariableScope::Global && lower(text(doc, g.name)) == lower(root)) return true;
        return false;
    };
    const auto setter = [this, indexOf, globalKnown](Field f) {
        return [this, indexOf, globalKnown, f](const std::string& key, const std::string& value, std::string* why) -> bool {
            const auto doc = hosts_.project ? hosts_.project() : nullptr;
            const auto i = indexOf(key);
            if (!doc || i == domain::kNoIndex || i >= doc->variables.size()) {
                if (why) *why = doc ? "variable introuvable" : "le projet ne se modifie pas";
                return false;
            }
            const auto& v = doc->variables[i];
            std::string now;
            switch (f) {
                case Field::Type: now = text(*doc, v.type.name); break;
                case Field::InitValue: now = text(*doc, v.initValue); break;
                case Field::Comment: now = text(*doc, v.comment); break;
                case Field::Effective: now = effectiveOf(v); break;
                case Field::Address: break;
            }
            std::string val = value;
            if (f == Field::Effective && (lower(val) == "(aucun)" || lower(val) == "(aucune)" || val == "-" || val == "\xE2\x80\x94")) val.clear();
            if (f == Field::Effective && val.rfind("\xE2\x87\x84", 0) == 0) val = trim(val.substr(3));
            if (val == now) return true;
            if (f == Field::Type && !apikit::validPlcType(*doc, val, why)) return false;
            if (f == Field::Effective) {
                if (!isParameter(v.scope)) {
                    if (why) *why = "une locale ne re\xC3\xA7oit rien : seuls les param\xC3\xA8tres sont reli\xC3\xA9s";
                    return false;
                }
                if (!val.empty() && !globalKnown(*doc, val)) {
                    if (why) *why = "\xC2\xAB " + rootName(val) + " \xC2\xBB n'est pas une variable du projet";
                    return false;
                }
            }
            hosts_.apply(std::make_unique<project::SetVariableFieldCommand>(doc, i, f, val));
            return true;
        };
    };
    const auto scopeSetter = [this, indexOf](const std::string& key, const std::string& value, std::string* why) -> bool {
        const auto doc = hosts_.project ? hosts_.project() : nullptr;
        const auto i = indexOf(key);
        if (!doc || i == domain::kNoIndex || i >= doc->variables.size()) {
            if (why) *why = "variable introuvable";
            return false;
        }
        domain::VariableScope s{};
        if (!parseScope(value, s)) {
            if (why) *why = "\xC2\xAB " + value + " \xC2\xBB n'est pas une port\xC3\xA9" "e : entr\xC3\xA9" "e, sortie, entr\xC3\xA9" "e-sortie, locale, publique";
            return false;
        }
        if (s == doc->variables[i].scope) return true;
        if (why) *why = "la port\xC3\xA9" "e d'une variable qui existe ne change pas ici (supprimer puis recr\xC3\xA9" "er)";
        return false;
    };
    t.columns.push_back(paste::column("Nom", {"Name", "Variable", "Param\xC3\xA8tre", "Parametre", "Mnemonique", "Symbole", "Identifiant"},
                                      static_cast<int>(Model::CName), nullptr, true));
    t.columns.push_back(paste::column("Type", {"Type de donnee", "DataType", "Data type"}, static_cast<int>(Model::CLanguage), setter(Field::Type)));
    t.columns.push_back(paste::column("Port\xC3\xA9" "e", {"Portee", "Scope", "Sens", "Direction", "Genre"}, static_cast<int>(Model::CLines), scopeSetter));
    t.columns.push_back(paste::column("Re\xC3\xA7oit", {"Recoit", "Reliee a", "Relie a", "Variable du projet", "EffectiveParameter", "Effective",
                                                        "Parametre effectif", "Liee a"},
                                      static_cast<int>(Model::CRank), setter(Field::Effective)));
    t.columns.push_back(paste::column("Valeur initiale", {"Initiale", "Initial", "Init", "Initial value", "Valeur par defaut"},
                                      static_cast<int>(Model::CParams), setter(Field::InitValue)));
    t.columns.push_back(paste::column("Commentaire", {"Comment", "Description", "Libelle", "Designation"}, static_cast<int>(Model::CComment),
                                      setter(Field::Comment)));
    t.exists = [indexOf](const std::string& key) { return indexOf(key) != domain::kNoIndex; };
    t.unknown = "introuvable";
    t.freeKey = [this, unit](const std::string& key) {
        std::vector<std::string> taken;
        const auto q = hosts_.view ? hosts_.view() : nullptr;
        if (q && unit < q->pous.size())
            for (const auto* list : {&q->pous[unit].parameters, &q->pous[unit].locals})
                for (const auto v : *list)
                    if (v < q->variables.size()) taken.push_back(text(*q, q->variables[v].name));
        return apikit::freeName(key, taken);
    };
    t.create = [this, unit, indexOf, globalKnown, defaultScope](const std::string& key, const std::map<std::string, std::string>& cells, paste::Notes& notes,
                                                                std::vector<std::string>& used, std::string* why) -> std::string {
        const auto doc = hosts_.project ? hosts_.project() : nullptr;
        if (!doc) {
            if (why) *why = "le projet ne se modifie pas";
            return {};
        }
        if (!project::macro::isIdentifier(key)) {
            if (why) *why = "\xC2\xAB " + key + " \xC2\xBB n'est pas un nom IEC (une lettre, puis lettres, chiffres, _)";
            return {};
        }
        project::AddVariableCommand::Spec spec;
        spec.name = key;
        spec.type = "BOOL";
        spec.scope = defaultScope;
        spec.owner = unit;
        if (const auto it = cells.find("portee"); it != cells.end() && !it->second.empty()) {
            domain::VariableScope s{};
            if (parseScope(it->second, s)) spec.scope = s;
            else
                notes.push_back({"Port\xC3\xA9" "e", "\xC2\xAB " + it->second + " \xC2\xBB n'est pas une port\xC3\xA9" "e \xE2\x80\x94 la variable est cr\xC3\xA9\xC3\xA9" "e "
                                                         + (defaultScope == domain::VariableScope::Local ? std::string("locale")
                                                                                                         : "avec la port\xC3\xA9" "e " + scopeLabel(defaultScope))});
        }
        used.push_back("portee");
        if (const auto it = cells.find("type"); it != cells.end() && !it->second.empty()) {
            std::string w;
            if (apikit::validPlcType(*doc, it->second, &w)) spec.type = it->second;
            else notes.push_back({"Type", w + " \xE2\x80\x94 la variable est cr\xC3\xA9\xC3\xA9" "e en BOOL"});
        }
        used.push_back("type");
        if (const auto it = cells.find("commentaire"); it != cells.end()) {
            spec.comment = it->second;
            used.push_back("commentaire");
        }
        if (const auto it = cells.find("valeurinitiale"); it != cells.end()) {
            spec.initValue = it->second;
            used.push_back("valeurinitiale");
        }
        hosts_.apply(std::make_unique<project::AddVariableCommand>(doc, std::move(spec)));
        const auto made = indexOf(key);
        if (made == domain::kNoIndex) {
            if (why) *why = "refus\xC3\xA9" "e par le projet";
            return {};
        }
        if (const auto it = cells.find("recoit"); it != cells.end()) {
            used.push_back("recoit");
            std::string val = trim(it->second);
            if (val.rfind("\xE2\x87\x84", 0) == 0) val = trim(val.substr(3));
            const auto& v = doc->variables[made];
            if (!val.empty() && lower(val) != "(aucun)" && lower(val) != "pas reli\xC3\xA9") {
                if (!isParameter(v.scope)) notes.push_back({"Re\xC3\xA7oit", "une locale ne re\xC3\xA7oit rien"});
                else if (!globalKnown(*doc, val)) notes.push_back({"Re\xC3\xA7oit", "\xC2\xAB " + rootName(val) + " \xC2\xBB n'est pas une variable du projet"});
                else hosts_.apply(std::make_unique<project::SetVariableFieldCommand>(doc, made, Field::Effective, val));
            }
        }
        return key;
    };
    // Sans la colonne Nom : les lignes collees vont dans les variables de
    // l'unite montrees, a partir de la ligne choisie.
    for (std::size_t i = rq.anchorViewRow; i < table_->visibleRowCount(); ++i) {
        const auto r = table_->viewRow(i);
        if (r >= rows_.size() || rows_[r].pou != unit) continue;
        if (rows_[r].isVariable() && rows_[r].variable < p->variables.size()) t.keysFromAnchor.push_back(text(*p, p->variables[rows_[r].variable].name));
    }
    return t;
}

void UnitsPane::runAction(int action) {
    const auto p = hosts_.view ? hosts_.view() : nullptr;
    const auto* row = selectedRow();
    // Un titre de groupe, un membre : rien a renommer ni a supprimer (voir attach).
    const bool named = row && (row->kind == Row::Unit || row->kind == Row::Section || row->kind == Row::Variable);
    switch (action) {
        case AAddUnit:
            if (hosts_.request) hosts_.request("create.unit");
            return;
        case AAddSection: {
            if (!row || !p || row->pou >= p->pous.size() || !hosts_.ask) return;
            const auto unit = row->pou;
            const std::string unitName = text(*p, p->pous[unit].name);
            const std::string task = p->pous[unit].task ? text(*p, p->pous[unit].task) : task_;
            hosts_.ask("Ajouter une section \xC3\xA0 " + unitName, "Le nom de la section (en ST), ajout\xC3\xA9" "e \xC3\xA0 la fin de l'unit\xC3\xA9.", "Nouvelle_section",
                       [this, unit, unitName, task](const std::string& value) {
                           auto doc = hosts_.project ? hosts_.project() : nullptr;
                           if (!doc || !hosts_.apply || value.empty()) return;
                           expanded_.insert("u:" + lower(unitName));
                           keep_ = "s:" + value;
                           hosts_.apply(std::make_unique<project::AddSectionCommand>(doc, value, task, domain::PouLanguage::ST, unit));
                       });
            return;
        }
        case AAddVariable: {
            if (!row || !p || row->pou >= p->pous.size() || !hosts_.ask) return;
            const auto unit = row->pou;
            const std::string unitName = text(*p, p->pous[unit].name);
            // Lot API 7 : sur le titre d'un groupe, la variable prend sa portee.
            const auto scope = row->kind == Row::Group ? folderScope(row->group) : domain::VariableScope::Local;
            std::vector<std::string> taken;
            for (const auto* list : {&p->pous[unit].parameters, &p->pous[unit].locals})
                for (const auto v : *list)
                    if (v < p->variables.size()) taken.push_back(text(*p, p->variables[v].name));
            const std::string how = scope == domain::VariableScope::Local
                                        ? std::string("Son nom, et son type apr\xC3\xA8s deux-points si ce n'est pas un BOOL (Compteur : INT). Elle est locale ; "
                                                      "un param\xC3\xA8tre se colle depuis Excel avec sa port\xC3\xA9" "e.")
                                        : "Son nom, et son type apr\xC3\xA8s deux-points si ce n'est pas un BOOL (Compteur : INT). Elle va dans \xC2\xAB "
                                              + std::string(folderLabel(folderOf(scope))) + " \xC2\xBB (port\xC3\xA9" "e " + scopeLabel(scope) + ").";
            hosts_.ask("Ajouter une variable \xC3\xA0 " + unitName, how,
                       apikit::freeName("Nouvelle_variable", taken), [this, unit, unitName, scope](const std::string& value) {
                           auto doc = hosts_.project ? hosts_.project() : nullptr;
                           if (!doc || !hosts_.apply || value.empty()) return;
                           project::AddVariableCommand::Spec spec;
                           spec.scope = scope;
                           spec.owner = unit;
                           const auto colon = value.find(':');
                           spec.name = trim(value.substr(0, colon));
                           if (colon != std::string::npos) spec.type = trim(value.substr(colon + 1));
                           std::string why;
                           if (!project::macro::isIdentifier(spec.name)) why = "\xC2\xAB " + spec.name + " \xC2\xBB n'est pas un nom IEC";
                           else if (!apikit::validPlcType(*doc, spec.type, &why)) {}
                           else
                               for (const auto* list : {&doc->pous[unit].parameters, &doc->pous[unit].locals})
                                   for (const auto v : *list)
                                       if (v < doc->variables.size() && lower(text(*doc, doc->variables[v].name)) == lower(spec.name))
                                           why = unitName + " a d\xC3\xA9j\xC3\xA0 une variable " + spec.name;
                           if (!why.empty()) {
                               if (hosts_.status) hosts_.status(why);
                               return;
                           }
                           expanded_.insert("u:" + lower(unitName));
                           varsFolded_.erase("g:" + lower(unitName) + "/" + folderId(folderOf(scope)));
                           keep_ = "v:" + unitName + "/" + spec.name;
                           hosts_.apply(std::make_unique<project::AddVariableCommand>(doc, std::move(spec)));
                       });
            return;
        }
        case ARename: {
            if (!row || !p || !hosts_.ask || !named) return;
            // Lot 7 : le dialogue qui montre tout ce qui suit ; sans lui, comme avant.
            if (row->isVariable()) {
                if (row->variable < p->variables.size() && row->pou < p->pous.size()
                    && requestRename("variable", text(*p, p->pous[row->pou].name) + "." + text(*p, p->variables[row->variable].name)))
                    return;
            } else if (row->kind == Row::Unit || row->section >= p->sections.size()) {
                if (row->pou < p->pous.size() && requestRename("unite", text(*p, p->pous[row->pou].name))) return;
            } else if (requestRename("section", text(*p, p->sections[row->section].name))) {
                return;
            }
            if (row->isVariable()) {
                if (row->variable >= p->variables.size()) return;
                const auto index = row->variable;
                const std::string old = text(*p, p->variables[index].name);
                const std::string unitName = row->pou < p->pous.size() ? text(*p, p->pous[row->pou].name) : std::string{};
                hosts_.ask("Renommer " + old, "Le nouveau nom de la variable : le code de l'unit\xC3\xA9 suit.", old,
                           [this, index, old, unitName](const std::string& value) {
                               auto doc = hosts_.project ? hosts_.project() : nullptr;
                               if (!doc || !hosts_.apply) return;
                               const auto problem = project::renameProblem(*doc, project::RenameCommand::What::Variable, index, value);
                               if (!problem.empty()) {
                                   if (hosts_.status) hosts_.status(problem);
                                   return;
                               }
                               moveKeys(expanded_, "m:" + lower(unitName + "." + old), "m:" + lower(unitName + "." + value));
                               keep_ = "v:" + unitName + "/" + value;
                               hosts_.apply(std::make_unique<project::RenameCommand>(doc, project::RenameCommand::What::Variable, index, value));
                               if (hosts_.status) hosts_.status(old + " renomm\xC3\xA9" "e en " + value + ". Ctrl+Z le d\xC3\xA9" "fait.");
                           });
                return;
            }
            const bool unit = row->kind == Row::Unit || row->section >= p->sections.size();
            const auto what = unit ? project::RenameCommand::What::Unit : project::RenameCommand::What::Section;
            const auto index = unit ? row->pou : row->section;
            const std::string old = unit ? text(*p, p->pous[row->pou].name) : text(*p, p->sections[row->section].name);
            hosts_.ask("Renommer " + old,
                       unit ? std::string("Le nouveau nom de l'unit\xC3\xA9 : les tables d'animation qu'elle porte suivent.")
                            : std::string("Le nouveau nom de la section : ses appels suivent."),
                       old, [this, what, index, unit, old](const std::string& value) {
                           auto doc = hosts_.project ? hosts_.project() : nullptr;
                           if (!doc || !hosts_.apply) return;
                           const auto problem = project::renameProblem(*doc, what, index, value);
                           if (!problem.empty()) {
                               if (hosts_.status) hosts_.status(problem);
                               return;
                           }
                           if (unit) renameUnitState(old, value);
                           keep_ = (unit ? "u:" : "s:") + value;
                           hosts_.apply(std::make_unique<project::RenameCommand>(doc, what, index, value));
                           if (hosts_.status) hosts_.status(old + " renomm\xC3\xA9" "e en " + value + ". Ctrl+Z le d\xC3\xA9" "fait.");
                       });
            return;
        }
        case ADelete:
            if (!row || !hosts_.remove || !named) return;
            if (row->kind == Row::Unit) hosts_.remove(domain::EntityKind::Pou, row->pou);
            else if (row->isVariable()) hosts_.remove(domain::EntityKind::Variable, row->variable);
            else hosts_.remove(domain::EntityKind::Section, row->section);
            return;
        case AOpenCode: {
            if (!row || !p || !hosts_.request) return;
            domain::Index section = row->isSection() ? row->section : domain::kNoIndex;
            if (section == domain::kNoIndex && row->pou < p->pous.size() && !p->pous[row->pou].sections.empty()) section = p->pous[row->pou].sections.front();
            if (section != domain::kNoIndex) hosts_.request("section:" + std::to_string(section));
            return;
        }
        case AOrder:
            if (row && p && row->pou < p->pous.size() && hosts_.request) hosts_.request("ordre:" + text(*p, p->pous[row->pou].name));
            return;
        // ---- 1.8.0 : l'ecran fait le travail (LisibleWorkspace.cpp) ----
        case ACompare:
        case AExport: {
            if (!hosts_.request) return;
            const auto secs = selectedSections();
            std::string list;
            for (const auto s2 : secs) list += (list.empty() ? "" : ",") + std::to_string(s2);
            if (action == ACompare) {
                if (!list.empty()) hosts_.request("comparer:" + list);
            } else if (!list.empty()) {
                hosts_.request("exporter:sections:" + list);
            } else if (row && p && row->pou < p->pous.size()) {
                hosts_.request("exporter:unite:" + text(*p, p->pous[row->pou].name));
            } else {
                hosts_.request("exporter:tout");
            }
            return;
        }
        case AIcon: {
            if (!hosts_.request || !p) return;
            std::string keys;
            for (const auto r : table_->selectedModelRows()) {
                if (r >= rows_.size()) continue;
                const auto& x = rows_[r];
                std::string k;
                if (x.kind == Row::Section && x.section < p->sections.size()) k = project::codeicons::keyForSection(*p, x.section);
                else if (x.kind == Row::Unit && x.pou < p->pous.size()) k = project::codeicons::keyForPou(*p, x.pou);
                if (!k.empty() && keys.find(k + "\n") == std::string::npos) keys += k + "\n";
            }
            if (!keys.empty()) hosts_.request("icone:" + keys);
            return;
        }
        default: return;
    }
}

void UnitsPane::onLayout() {
    const auto b = bounds();
    const float rightW = std::clamp(b.w * 0.28f, 280.f, 460.f);
    table_->setBounds({b.x, b.y, b.w - rightW - 1.f, b.h});
    props_->setBounds({b.right() - rightW, b.y, rightW, b.h});
}

void UnitsPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.windowBg);
    ctx.r.fillRect({props_->bounds().x - 1.f, bounds().y, 1.f, bounds().h}, ctx.theme.color.border);
}

} // namespace app
