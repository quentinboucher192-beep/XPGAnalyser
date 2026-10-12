// =============================================================================
//  hmi/HmiRuntimeTable.cpp - 1.12.13 : LE TABLEAU EN LIGNES DYNAMIQUES, EN MARCHE
// -----------------------------------------------------------------------------
//  La source (« Lignes depuis ») se lit ici, en marche : un tableau de l'IHM
//  (Cuves : un alias par ligne, Cuves[2]), une LIST, un VECTOR, une MAP, une
//  structure objet (l'objet en place), une structure de l'IHM (un membre par
//  ligne), une enumeration (une valeur par ligne), un parametre de la vue (ce
//  qu'il designe, ou sa copie). Chaque ligne a sa portee, fille de celle de la
//  vue : __TABLE_ELT__ (l'element), __TABLE_KEY__ / __TABLE_VAL__ (la cle et la
//  valeur d'une MAP, le membre et sa valeur d'une structure, le nom et le nombre
//  d'une enumeration), __TABLE_TXT__ (son texte), __TABLE_POS__ (le rang, 0...),
//  __TABLE_ROW__ (le numero, 1...). Les contenus des colonnes, compiles une fois
//  (table::compileContent), s'y evaluent.
//
//  « ... » : une colonne qui nomme sa liste (Debits[*]) et dont la liste n'a pas
//  cette ligne - sans l'evaluer (un indice hors des bornes ne remplit pas le
//  Journal). Sans source, la plus longue liste des colonnes fait les lignes.
// =============================================================================
#include "HmiRuntime.hpp"

#include "HmiActionKinds.hpp"   // le clavier d'une case R/W
#include "HmiDisplay.hpp"
#include "HmiEnums.hpp"
#include "HmiExpr.hpp"
#include "HmiMarkers.hpp"
#include "HmiNatives.hpp"    // parseColor : la formule de couleur
#include "HmiRanges.hpp"     // les seuils de la variable
#include "HmiRoles.hpp"
#include "HmiSources.hpp"
#include "HmiTable.hpp"
#include "HmiTypeRegistry.hpp"  // la plage d'un entier (une case R/W)
#include "HmiTypes.hpp"
#include "HmiWidgets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace hmi {

namespace {

std::string trimmedText(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return std::string(s);
}
bool sameText(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
bool numericType(std::string_view type) {
    std::string u(type);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return sim::isNumeric(sim::typeFromName(u));
}

// L'objet que designe un chemin sous une variable objet : Alarmes[1], Stock['vis'], Recette.Etapes[0].Nom.
sim::ObjRef walkRich(sim::ObjRef o, std::string_view rest) {
    std::size_t i = 0;
    while (o && i < rest.size()) {
        if (rest[i] == '.') {
            std::size_t j = i + 1;
            while (j < rest.size() && rest[j] != '.' && rest[j] != '[') ++j;
            const std::string name(rest.substr(i + 1, j - i - 1));
            if (!o->type || (o->type->kind != sim::TypeDesc::Kind::Struct && o->type->kind != sim::TypeDesc::Kind::Tuple)) return nullptr;
            sim::ObjRef next;
            for (std::size_t m = 0; m < o->type->members.size() && m < o->items.size(); ++m)
                if (sameText(o->type->members[m].first, name)) next = o->items[m];
            o = next;
            i = j;
        } else if (rest[i] == '[') {
            const auto close = rest.find(']', i);
            if (close == std::string_view::npos) return nullptr;
            const std::string inside = trimmedText(rest.substr(i + 1, close - i - 1));
            i = close + 1;
            if (!o->type) return nullptr;
            if (o->type->kind == sim::TypeDesc::Kind::Map) {
                if (inside.size() >= 2 && inside.front() == '\'' && inside.back() == '\'') {
                    std::string key;
                    for (std::size_t k = 1; k + 1 < inside.size(); ++k) {
                        if (inside[k] == '\'' && k + 2 < inside.size() && inside[k + 1] == '\'') ++k;
                        key += inside[k];
                    }
                    const auto it = o->textKeys.find(key);
                    o = it == o->textKeys.end() ? nullptr : it->second;
                } else {
                    char* end = nullptr;
                    const long long k = std::strtoll(inside.c_str(), &end, 10);
                    if (!end || *end) return nullptr;
                    const auto it = o->intKeys.find(k);
                    o = it == o->intKeys.end() ? nullptr : it->second;
                }
                continue;
            }
            char* end = nullptr;
            long long k = std::strtoll(inside.c_str(), &end, 10);
            if (!end || *end) return nullptr;
            if (o->type->kind == sim::TypeDesc::Kind::Array && !o->type->bounds.empty()) k -= o->type->bounds[0].first;
            if (k < 0 || static_cast<std::size_t>(k) >= o->items.size()) return nullptr;
            o = o->items[static_cast<std::size_t>(k)];
        } else {
            return nullptr;
        }
    }
    return o;
}

} // namespace

// Ce que designe la source, en marche.
struct Runtime::TableSourceRow {
    long long   position{0};
    std::string index;          // ce qu'on montre et passe : 2, vis, Nom, ARRET
    std::string element;        // le chemin de l'element (Cuves[2], Stock['vis'], Recette.Nom) ; vide : `object`
    sim::ObjRef object;         // un element objet (d'une LIST), la valeur d'une MAP, le membre d'une structure objet
    bool        keyed{false};   // une MAP, une structure, une enumeration : sa cle
    sim::Value  key;
    sim::Value  number;         // une enumeration : son nombre
    std::string text;           // une enumeration : son texte
};
struct Runtime::TableRowSource {
    table::SourceKind kind{table::SourceKind::None};
    std::string       path;     // la source ecrite, resolue (Cuves ; le parametre d'une popup : ce qu'il designe)
    std::string       keyType;  // une MAP : STRING ou un entier
    long long         low{0};
    std::vector<TableSourceRow> rows;
    std::string       error;
};

bool Runtime::tableSource(Id view, const Object& o, TableRowSource& out) const {
    out = {};
    if (!project_) return false;
    std::string src = trimmedText(markers::strip(o.text("rowsFrom")));
    if (!src.empty() && src.front() == '=') src = trimmedText(std::string_view(src).substr(1));
    if (src.empty()) return true;   // une liste par colonne
    const Scope* scope = viewScope(view);
    // Un parametre objet de la vue (en valeur), sa copie, une variable objet, un objet sous elle.
    sim::ObjRef obj;
    std::string path = src;
    if (scope) {
        if (auto own = scope->object(src)) {
            obj = own;
            path.clear();
        } else if (const std::string r = scope->resolve(src); !r.empty()) {
            path = r;
        }
    }
    if (!obj && !path.empty() && path.front() == '$') {
        obj = copyObject(path);
        path.clear();
    }
    if (!obj && !path.empty()) {
        obj = richVariable(path);
        // Sous une variable objet : Alarmes[1] (une structure de la LIST), Recette.Etapes.
        for (std::size_t k = path.size(); !obj && k > 0; --k)
            if (path[k - 1] == '.' || path[k - 1] == '[')
                if (auto root = richVariable(std::string_view(path).substr(0, k - 1))) obj = walkRich(root, std::string_view(path).substr(k - 1));
    }
    const auto child = [&](const std::string& suffix) { return path.empty() ? std::string{} : path + suffix; };
    out.path = path.empty() ? src : path;
    if (obj && obj->type) {
        const auto kind = obj->type->kind;
        if (kind == sim::TypeDesc::Kind::Map) {
            out.kind = table::SourceKind::Map;
            out.keyType = obj->type->key == sim::Type::String ? "STRING" : std::string(sim::toString(obj->type->key));
            long long n = 0;
            for (const auto& [k, v] : obj->textKeys) {
                TableSourceRow r;
                r.position = n++;
                r.index = k;
                r.element = child("[" + table::keyLiteral(k, true) + "]");
                r.object = v;
                r.keyed = true;
                r.key = sim::Value::text(k);
                out.rows.push_back(std::move(r));
            }
            for (const auto& [k, v] : obj->intKeys) {
                TableSourceRow r;
                r.position = n++;
                r.index = std::to_string(k);
                r.element = child("[" + std::to_string(k) + "]");
                r.object = v;
                r.keyed = true;
                r.key = sim::Value::integer(obj->type->key == sim::Type::String ? sim::Type::DInt : obj->type->key, k);
                out.rows.push_back(std::move(r));
            }
            return true;
        }
        if (kind == sim::TypeDesc::Kind::List || kind == sim::TypeDesc::Kind::Array) {
            if (kind == sim::TypeDesc::Kind::Array && obj->type->bounds.size() > 1) {
                out.kind = table::SourceKind::Error;
                out.error = out.path + " est un tableau \xC3\xA0 2 dimensions : prends une ligne (" + out.path + "[1]).";
                return false;
            }
            out.kind = kind == sim::TypeDesc::Kind::List ? table::SourceKind::List : table::SourceKind::Array;
            out.low = kind == sim::TypeDesc::Kind::Array && !obj->type->bounds.empty() ? obj->type->bounds[0].first : 0;
            for (std::size_t i = 0; i < obj->items.size(); ++i) {
                TableSourceRow r;
                r.position = static_cast<long long>(i);
                r.index = std::to_string(out.low + static_cast<long long>(i));
                r.element = child("[" + r.index + "]");
                r.object = obj->items[i];
                out.rows.push_back(std::move(r));
            }
            return true;
        }
        if (kind == sim::TypeDesc::Kind::Struct || kind == sim::TypeDesc::Kind::Tuple) {
            out.kind = table::SourceKind::Struct;
            for (std::size_t m = 0; m < obj->type->members.size() && m < obj->items.size(); ++m) {
                TableSourceRow r;
                r.position = static_cast<long long>(m);
                r.index = obj->type->members[m].first;
                r.element = child("." + r.index);
                r.object = obj->items[m];
                r.keyed = true;
                r.key = sim::Value::text(r.index);
                out.rows.push_back(std::move(r));
            }
            return true;
        }
        out.kind = table::SourceKind::Error;
        out.error = out.path + " n'est pas une collection : une seule valeur.";
        return false;
    }
    // Un tableau de l'IHM (deplie en cases) : un alias par ligne ; une structure : un membre simple par ligne.
    if (const auto* a = path.empty() ? nullptr : aggregate(path)) {
        if (a->array) {
            if (a->spec.dims != 1) {
                out.kind = table::SourceKind::Error;
                out.error = path + " est un tableau \xC3\xA0 2 dimensions : prends une ligne (" + path + "["
                          + std::to_string(a->spec.low[0]) + "]).";
                return false;
            }
            out.kind = table::SourceKind::Array;
            out.low = a->spec.low[0];
            for (long long i = a->spec.low[0]; i <= a->spec.high[0]; ++i) {
                TableSourceRow r;
                r.position = i - a->spec.low[0];
                r.index = std::to_string(i);
                r.element = path + "[" + r.index + "]";
                out.rows.push_back(std::move(r));
            }
            return true;
        }
        out.kind = table::SourceKind::Struct;
        long long n = 0;
        for (const auto& m : sources::leafMembers(*project_, a->typeName)) {
            TableSourceRow r;
            r.position = n++;
            r.index = m.name;
            r.element = path + "." + m.name;
            r.keyed = true;
            r.key = sim::Value::text(m.name);
            out.rows.push_back(std::move(r));
        }
        return true;
    }
    // Une enumeration du projet : une ligne par valeur.
    if (const auto* e = findEnumeration(*project_, path.empty() ? src : path)) {
        out.kind = table::SourceKind::Enum;
        long long n = 0;
        for (const auto& ev : e->values) {
            TableSourceRow r;
            r.position = n++;
            r.index = ev.name;
            r.keyed = true;
            r.key = sim::Value::text(ev.name);
            r.number = sim::Value::integer(sim::Type::DInt, ev.value);
            r.text = ev.text.empty() ? ev.name : ev.text;
            out.rows.push_back(std::move(r));
        }
        return true;
    }
    out.kind = table::SourceKind::Error;
    out.error = variable(path) ? "\xC2\xAB " + src + " \xC2\xBB est une seule valeur, pas une collection de lignes."
                               : "\xC2\xAB " + src + " \xC2\xBB ne se lit pas (ni une variable, ni un param\xC3\xA8tre de la vue).";
    return false;
}

namespace {

// La portee d'une ligne : l'element et ses pseudo-membres.
void fillRowScope(Scope& s, table::SourceKind kind, long long position, const std::string& element, const sim::ObjRef& object,
                  bool keyed, const sim::Value& key, const sim::Value& number, const std::string& text) {
    s.setValue(std::string(table::kPos), sim::Value::integer(sim::Type::DInt, position));
    s.setValue(std::string(table::kRow), sim::Value::integer(sim::Type::DInt, position + 1));
    const auto elementTo = [&](std::string_view name) {
        if (object) s.setObject(std::string(name), object);
        else if (!element.empty()) s.setAlias(std::string(name), element);
    };
    switch (kind) {
        case table::SourceKind::Enum:
            s.setValue(std::string(table::kKey), key);
            s.setValue(std::string(table::kElt), key);
            s.setValue(std::string(table::kVal), number);
            s.setValue(std::string(table::kTxt), sim::Value::text(text));
            break;
        case table::SourceKind::Map:
        case table::SourceKind::Struct:
            if (keyed) s.setValue(std::string(table::kKey), key);
            elementTo(table::kVal);
            elementTo(table::kElt);
            break;
        default:
            elementTo(table::kElt);
            break;
    }
}

// Les MAP lues par la cle de la ligne (Prix[[*].Cle] compile en Prix[__TABLE_KEY__]) : leurs chemins.
std::vector<std::string> keyedMapsOf(const std::string& code) {
    std::vector<std::string> out;
    const std::string needle = "[" + std::string(table::kKey) + "]";
    for (std::size_t at = code.find(needle); at != std::string::npos; at = code.find(needle, at + needle.size())) {
        std::size_t a = at;
        int depth = 0;
        while (a > 0) {
            const char c = code[a - 1];
            const auto u = static_cast<unsigned char>(c);
            if (c == ']') { ++depth; --a; continue; }
            if (c == '[') {
                if (depth == 0) break;
                --depth;
                --a;
                continue;
            }
            if (depth > 0 || std::isalnum(u) != 0 || c == '_' || c == '.' || u >= 0x80) { --a; continue; }
            break;
        }
        if (a < at) out.push_back(code.substr(a, at - a));
    }
    return out;
}

// Une couleur ecrite ('#RRGGBB', 'RRGGBB') : 0xRRGGBB.
bool colorOf(const sim::Value& v, std::uint32_t& out) {
    if (v.type() != sim::Type::String) return false;
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
    if (!natives::parseColor(v.asString(), r, g, b, a)) return false;
    out = (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | b;
    return true;
}

} // namespace

bool Runtime::tableRowScope(Id view, const Object& o, long long position, Scope& scope, table::RowPaths* paths,
                            table::Source* source) const {
    TableRowSource src;
    if (!tableSource(view, o, src)) return false;
    if (source) {
        *source = table::analyzeSource(*project_, o.text("rowsFrom"));
        source->kind = src.kind == table::SourceKind::None ? table::SourceKind::None : src.kind;
        source->low = src.low;
        if (!src.keyType.empty()) source->key = src.keyType;
    }
    if (src.kind == table::SourceKind::None) {
        // Une liste par colonne : la ligne n'a que son rang.
        fillRowScope(scope, src.kind, position, {}, nullptr, false, {}, {}, {});
        if (paths) paths->position = position;
        return position >= 0;
    }
    if (position < 0 || static_cast<std::size_t>(position) >= src.rows.size()) return false;
    const auto& r = src.rows[static_cast<std::size_t>(position)];
    fillRowScope(scope, src.kind, r.position, r.element, r.object, r.keyed, r.key, r.number, r.text);
    if (paths) {
        paths->position = r.position;
        paths->element = r.element;
        paths->value = (src.kind == table::SourceKind::Map || src.kind == table::SourceKind::Struct) ? r.element : std::string{};
        paths->key = src.kind == table::SourceKind::Map ? table::keyLiteral(r.index, r.key.type() == sim::Type::String)
                   : table::keyLiteral(r.index, true);
    }
    return true;
}

bool Runtime::tableModel(Id view, const Object& o, table::Model& out) const {
    out = {};
    if (table::modeOf(o) != table::Mode::Rows || !project_ || !env_) return false;
    out.emptyText = trimmedText(o.text(table::kEmptyKey));
    // ---- le tableau d'avant (sans dynColumns) : ses lignes, telles quelles ------------------
    if (!table::hasColumns(o)) {
        std::vector<std::vector<std::string>> rows;
        // Illisible : faux, et le dessin d'avant (les colonnes que donnera la source) reste.
        if (!tableRows(o, out.headers, rows)) return false;
        std::size_t n = out.headers.size();
        for (const auto& r : rows) n = std::max(n, r.size());
        n = std::max<std::size_t>(1, n);
        out.widths = columnFractions(o, n);
        out.aligns.assign(n, "gauche");
        out.writable.assign(n, false);
        // Les chemins des lignes (une action du tableau) : ceux de la source, dans le meme ordre.
        TableRowSource src;
        const bool known = tableSource(view, o, src);
        out.kind = src.kind;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            table::ModelRow row;
            row.position = static_cast<long long>(i);
            if (known && i < src.rows.size()) {
                row.index = src.rows[i].index;
                row.element = src.rows[i].element;
            }
            for (auto& text : rows[i]) {
                table::ModelCell c;
                c.text = std::move(text);
                double x = 0;
                if (roles::readNumber(c.text, x)) {
                    c.numeric = true;
                    c.number = x;
                }
                row.cells.push_back(std::move(c));
            }
            out.rows.push_back(std::move(row));
        }
        return true;
    }
    // ---- les colonnes de 1.12.13 ------------------------------------------------------------
    out.columns = true;
    const auto cols = table::columnsOf(o);
    TableRowSource src;
    tableSource(view, o, src);
    out.kind = src.kind;
    if (src.kind == table::SourceKind::Error) out.error = src.error;
    // La source vue par l'analyse (sans moteur), au genre trouve en marche : les pseudo-membres en dependent.
    table::Source info = table::analyzeSource(*project_, o.text("rowsFrom"));
    if (src.kind != table::SourceKind::Error) {
        info.kind = src.kind;
        info.low = src.low;
        if (!src.keyType.empty()) info.key = src.keyType;
    }
    const Scope* viewSc = viewScope(view);
    const View* shownIn = project_->view(view);
    struct Compiled {
        table::Content kind{table::Content::Empty};
        table::ColumnInfo info;
        std::string    code;             // le contenu compile (une expression, un texte a trous, un texte)
        TextTemplate   tpl;
        std::vector<std::string> lists;  // les listes nommees (Debits)
        std::vector<long long>   lengths;
        std::vector<sim::ObjRef> maps;   // les autres MAP lues par la cle de la ligne (Prix[[*].Cle])
        bool           enumType{false};
        std::string    format, unit;
        std::string    colorCode;
        double         th[4]{0, 0, 0, 0};
        bool           hasTh[4]{false, false, false, false};
    };
    // L'objet d'une liste ou d'une MAP nommee (Debits, Prix, un parametre de la vue) ; nul : pas un objet.
    const auto objectAt = [&](std::string p) -> sim::ObjRef {
        if (viewSc) {
            if (auto own = viewSc->object(p)) return own;
            if (const std::string r = viewSc->resolve(p); !r.empty()) p = r;
        }
        sim::ObjRef obj = !p.empty() && p.front() == '$' ? copyObject(p) : richVariable(p);
        for (std::size_t k = p.size(); !obj && k > 0; --k)
            if (p[k - 1] == '.' || p[k - 1] == '[')
                if (auto root = richVariable(std::string_view(p).substr(0, k - 1))) obj = walkRich(root, std::string_view(p).substr(k - 1));
        return obj;
    };
    // La longueur d'une liste nommee ; -1 : inconnue.
    const auto lengthOf = [&](const std::string& listPath) -> long long {
        if (auto obj = objectAt(listPath))
            return obj->type && obj->type->kind == sim::TypeDesc::Kind::Map ? static_cast<long long>(obj->textKeys.size() + obj->intKeys.size())
                                                                           : static_cast<long long>(obj->items.size());
        std::string p = listPath;
        if (viewSc)
            if (const std::string r = viewSc->resolve(p); !r.empty()) p = r;
        if (const auto* a = aggregate(p); a && a->array) return a->spec.count(0);
        return -1;
    };
    std::vector<Compiled> comp(cols.size());
    for (std::size_t k = 0; k < cols.size(); ++k) {
        auto& c = comp[k];
        const auto& col = cols[k];
        c.kind = table::contentKind(col.content);
        c.info = table::analyzeColumn(*project_, info, col);
        c.code = table::compileContent(col.content, info, project_, &c.lists);
        std::sort(c.lists.begin(), c.lists.end());
        c.lists.erase(std::unique(c.lists.begin(), c.lists.end()), c.lists.end());
        for (const auto& l : c.lists) c.lengths.push_back(lengthOf(l));
        for (const auto& m : keyedMapsOf(c.code))
            if (auto obj = objectAt(m); obj && obj->type && obj->type->kind == sim::TypeDesc::Kind::Map) c.maps.push_back(std::move(obj));
        if (c.kind == table::Content::Template) c.tpl = TextTemplate::compile(c.code);
        if (c.kind == table::Content::Text) c.code = trimmedText(markers::strip(col.content));
        c.enumType = !c.info.type.empty() && findEnumeration(*project_, c.info.type) != nullptr;
        c.format = !trimmedText(col.format).empty() ? trimmedText(col.format) : c.kind == table::Content::Path ? c.info.format : std::string{};
        c.unit = !trimmedText(col.unit).empty() ? trimmedText(col.unit) : c.kind == table::Content::Path ? c.info.unit : std::string{};
        if (sameText(col.color, "formule") && !trimmedText(col.colorExpr).empty())
            c.colorCode = table::compileContent(col.colorExpr, info, project_, nullptr);
        out.headers.push_back(col.title);
        out.widths.push_back(col.width > 0 ? col.width : 1.0);
        out.aligns.push_back(!col.align.empty() ? col.align
                             : (c.kind == table::Content::Path || c.kind == table::Content::Formula || c.kind == table::Content::RowNumber)
                                       && numericType(c.info.type) && !c.enumType ? "droite" : "gauche");
        out.writable.push_back(col.write && c.info.writable);
    }
    // Les seuils d'une colonne (couleur selon les seuils) : ceux de sa variable (Unites et formats).
    const auto thresholdsFor = [&](Compiled& c, const table::Column& col, const table::RowPaths& first) {
        if (!sameText(col.color, "seuils") || c.kind != table::Content::Path) return;
        const std::string path = table::writePathOf(col.content, info, first, project_);
        if (path.empty()) return;
        for (int t = 0; t < 4; ++t) {
            const auto inh = ranges::inheritedOf(*project_, path, ranges::kThresholds[t], shownIn);
            const std::string v = trimmedText(inh.value);
            if (v.empty()) continue;
            double x = 0;
            if (roles::readNumber(v, x)) {
                c.th[t] = x;
                c.hasTh[t] = true;
            } else if (const auto* var = variable(markers::strip(v)); var && sim::isNumeric(var->type())) {
                c.th[t] = var->asReal();
                c.hasTh[t] = true;
            }
        }
    };
    // Le nombre de lignes : la source ; sans source, la plus longue liste des colonnes.
    long long count = 0;
    if (src.kind == table::SourceKind::None) {
        for (const auto& c : comp)
            for (const long long n : c.lengths) count = std::max(count, n);
    } else if (src.kind != table::SourceKind::Error) {
        count = static_cast<long long>(src.rows.size());
    }
    if (!src.rows.empty() || src.kind == table::SourceKind::None) {
        table::RowPaths first;
        if (!src.rows.empty()) {
            first.position = src.rows[0].position;
            first.element = src.rows[0].element;
            first.value = src.rows[0].element;
            first.key = table::keyLiteral(src.rows[0].index, true);
        }
        for (std::size_t k = 0; k < cols.size(); ++k) thresholdsFor(comp[k], cols[k], first);
    }
    // La ligne choisie (pickVariable) : son numero (un entier) ou sa cle (un texte).
    const std::string pickName = trimmedText(markers::strip(o.text(table::kPickKey)));
    const sim::Value* pick = pickName.empty() ? nullptr : variable(pickName);
    for (long long i = 0; i < count; ++i) {
        table::ModelRow row;
        Scope rs(viewSc);
        const sim::Value* rowKey = nullptr;
        if (src.kind == table::SourceKind::None) {
            row.position = i;
            row.index = std::to_string(i + 1);
            fillRowScope(rs, src.kind, i, {}, nullptr, false, {}, {}, {});
        } else {
            const auto& r = src.rows[static_cast<std::size_t>(i)];
            row.position = r.position;
            row.index = r.index;
            row.element = r.element;
            fillRowScope(rs, src.kind, r.position, r.element, r.object, r.keyed, r.key, r.number, r.text);
            if (r.keyed) rowKey = &r.key;
        }
        if (pick) {
            if (pick->type() == sim::Type::String) row.picked = sameText(pick->asString(), row.index) || pick->asString() == std::to_string(row.position + 1);
            else if (sim::isInteger(pick->type())) row.picked = pick->asInteger() == row.position + 1;
        }
        for (std::size_t k = 0; k < cols.size(); ++k) {
            auto& c = comp[k];
            table::ModelCell cell;
            bool missing = false;
            for (const long long n : c.lengths)
                if (n >= 0 && row.position >= n) missing = true;
            // Une autre MAP lue par la cle de la ligne, sans cette cle (Prix n'a pas 'rondelle') : « ... ».
            if (rowKey)
                for (const auto& m : c.maps) {
                    const bool has = rowKey->type() == sim::Type::String ? m->textKeys.count(rowKey->asString()) > 0
                                                                         : m->intKeys.count(rowKey->asInteger()) > 0;
                    if (!has) missing = true;
                }
            if (missing) {
                cell.missing = true;
                cell.text = std::string(table::kMissing);
                row.cells.push_back(std::move(cell));
                continue;
            }
            switch (c.kind) {
                case table::Content::Empty:
                    break;
                case table::Content::Text:
                    cell.text = c.code;
                    break;
                case table::Content::RowNumber:
                    cell.text = std::to_string(row.position + 1);
                    cell.numeric = true;
                    cell.number = static_cast<double>(row.position + 1);
                    break;
                case table::Content::Template:
                    cell.text = c.tpl.render(envOf(), &rs);
                    cell.error = cell.text.find("###") != std::string::npos;
                    break;
                case table::Content::Path:
                case table::Content::Formula: {
                    auto it = expressions_.find(c.code);
                    if (it == expressions_.end()) it = expressions_.emplace(c.code, Expression::compile(c.code)).first;
                    if (!it->second.valid()) {
                        cell.error = true;
                        cell.text = "###";
                        break;
                    }
                    const auto v = it->second.evaluate(envOf(), &rs);
                    if (!v) {
                        cell.error = true;
                        cell.text = "###";
                        break;
                    }
                    std::string shown;
                    if (c.enumType || (c.info.type.empty() && sim::isInteger(v->type()))) {
                        const auto shape = it->second.shapeOf(envOf(), &rs, &*v);
                        shown = shape.enumText;
                    }
                    if (shown.empty()) shown = c.format.empty() ? roles::displayText(*v) : formatValue(*v, c.format);
                    if (!c.unit.empty() && !shown.empty()) shown += " " + c.unit;
                    cell.text = std::move(shown);
                    if (sim::isNumeric(v->type()) && v->type() != sim::Type::Bool) {
                        cell.numeric = true;
                        cell.number = v->asReal();
                        if (sameText(cols[k].color, "seuils")) {
                            const double x = cell.number;
                            if ((c.hasTh[0] && x < c.th[0]) || (c.hasTh[3] && x > c.th[3])) cell.tone = 2;
                            else if ((c.hasTh[1] && x < c.th[1]) || (c.hasTh[2] && x > c.th[2])) cell.tone = 1;
                        }
                    }
                    break;
                }
            }
            if (!c.colorCode.empty() && !cell.error) {
                auto it = expressions_.find(c.colorCode);
                if (it == expressions_.end()) it = expressions_.emplace(c.colorCode, Expression::compile(c.colorCode)).first;
                if (it->second.valid())
                    if (const auto v = it->second.evaluate(envOf(), &rs)) cell.colored = colorOf(*v, cell.color);
            }
            row.cells.push_back(std::move(cell));
        }
        out.rows.push_back(std::move(row));
    }
    // Le tri : par la colonne choisie, les nombres comme des nombres ; « ... » et ### a la fin.
    out.sortColumn = table::sortColumnOf(o, cols);
    out.sortDescending = table::sortDescending(o);
    if (out.sortColumn >= 0) {
        const auto k = static_cast<std::size_t>(out.sortColumn);
        const bool desc = out.sortDescending;
        std::stable_sort(out.rows.begin(), out.rows.end(), [&](const table::ModelRow& a, const table::ModelRow& b) {
            const auto& x = a.cells[k];
            const auto& y = b.cells[k];
            const bool xOut = x.missing || x.error, yOut = y.missing || y.error;
            if (xOut != yOut) return yOut;
            if (xOut) return false;
            int r = 0;
            if (x.numeric && y.numeric) r = x.number < y.number ? -1 : x.number > y.number ? 1 : 0;
            else r = x.text < y.text ? -1 : x.text > y.text ? 1 : 0;
            return desc ? r > 0 : r < 0;
        });
    }
    return true;
}

// ============================================================ les gestes sur un tableau (1.12.13)
bool Runtime::tableShown(Id view, const Object& o, table::Model& out) const {
    out = {};
    switch (table::modeOf(o)) {
        case table::Mode::Rows:
            return tableModel(view, o, out);
        case table::Mode::File:
            return false;
        case table::Mode::Fixed:
            break;
    }
    // Les cases fixes : leur texte calcule (une formule, un texte a trous), comme l'ecran les montre.
    const auto cells = parseCells(o.text("cells"));
    if (cells.empty() || !env_) return false;
    std::size_t n = splitSemicolons(o.text("columns")).size();
    for (const auto& r : cells) n = std::max(n, r.size());
    n = std::max<std::size_t>(1, n);
    out.headers = splitSemicolons(o.text("columns"));
    out.headers.resize(n);
    out.widths = columnFractions(o, n);
    out.aligns.assign(n, "gauche");
    const auto access = table::fixedAccess(o, n);
    out.writable = access;
    const Scope* scope = viewScope(view);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        table::ModelRow row;
        row.position = static_cast<long long>(i);
        row.index = std::to_string(i + 1);
        for (std::size_t k = 0; k < n; ++k) {
            table::ModelCell c;
            const std::string raw = k < cells[i].size() ? cells[i][k] : std::string{};
            if (cellIsExpression(raw)) {
                const std::string code = raw.substr(1);
                auto it = expressions_.find(code);
                if (it == expressions_.end()) it = expressions_.emplace(code, Expression::compile(code)).first;
                bool ok = false;
                if (it->second.valid())
                    if (auto val = it->second.evaluate(envOf(), scope)) {
                        c.text = roles::displayText(*val);
                        ok = true;
                    }
                if (!ok) {
                    c.error = true;
                    c.text = "###";
                }
            } else if (cellIsTemplate(raw)) {
                c.text = TextTemplate::compile(raw).render(envOf(), scope);
            } else {
                c.text = raw;
            }
            row.cells.push_back(std::move(c));
        }
        out.rows.push_back(std::move(row));
    }
    return true;
}

void Runtime::tablePart(const View& v, const Object& o, std::string_view part, double now) {
    auto& cell = tableCells_[o.id];
    if (part.rfind("entete:", 0) == 0) {
        cell.row = -1;
        cell.column = std::atoi(std::string(part.substr(7)).c_str());
        table::Model m;
        if (tableShown(v.id, o, m)) tableFire(v, o, Trigger::HeaderClick, m, -1, cell.column, now);
        return;
    }
    // "case:R:C" : la ligne montree R (0...), la colonne C ; "case:-1:-1" : hors des cases.
    const std::string rest(part.substr(5));
    const auto colon = rest.find(':');
    cell.row = std::atoll(rest.substr(0, colon).c_str());
    cell.column = colon == std::string::npos ? -1 : std::atoi(rest.substr(colon + 1).c_str());
}

void Runtime::tableGesture(const View& v, const Object& o, Trigger t, double now) {
    const auto it = tableCells_.find(o.id);
    if (it == tableCells_.end() || it->second.row < 0) return;
    const long long row = it->second.row;
    const int column = it->second.column;
    table::Model m;
    if (!tableShown(v.id, o, m) || row >= static_cast<long long>(m.rows.size())) return;
    tableFire(v, o, t, m, row, column, now);
    if (t != Trigger::CellClick) return;
    // La ligne choisie : la variable de « Ligne choisie → » (son numero, ou sa cle), puis ses actions - quand elle change.
    const auto& r = m.rows[static_cast<std::size_t>(row)];
    auto& cell = tableCells_[o.id];
    if (m.columns || table::modeOf(o) == table::Mode::Rows) {
        const std::string pick = trimmedText(markers::strip(o.text(table::kPickKey)));
        if (cell.picked != r.position) {
            cell.picked = r.position;
            if (!pick.empty()) {
                sim::Value cur;
                const auto aliases = viewAliases(v.id);
                if (envOf().read(pick, cur)) {
                    const sim::Value next = cur.type() == sim::Type::String ? sim::Value::text(r.index)
                                                                            : sim::Value::integer(cur.type() == sim::Type::Unknown ? sim::Type::Int : cur.type(),
                                                                                                  r.position + 1);
                    if (write(pick, next, where(v, &o))) log("Action", where(v, &o), "ligne choisie : " + pick + " := " + formatValue(next));
                }
            }
            tableFire(v, o, Trigger::RowPick, m, row, column, now);
        }
    }
    // Une case R/W : sa saisie (le clavier virtuel), Case saisie apres.
    if (column >= 0 && static_cast<std::size_t>(column) < m.writable.size() && m.writable[static_cast<std::size_t>(column)])
        (void)tablePrompt(v, o, m, row, column);
}

void Runtime::tableFire(const View& v, const Object& o, Trigger t, const table::Model& m, long long row, int column, double now,
                        const sim::Value* before, const sim::Value* after) {
    const std::string title = column >= 0 && static_cast<std::size_t>(column) < m.headers.size() ? m.headers[static_cast<std::size_t>(column)] : std::string{};
    std::vector<Action> list;
    for (const auto& a : o.actions)
        if (a.trigger == t && (a.column.empty() || t == Trigger::RowPick || (!title.empty() && sameText(trimmedText(a.column), trimmedText(title)))))
            list.push_back(a);
    if (list.empty()) return;
    std::string why;
    if (!objectAllowed(o, &why)) {
        event("Acc\xC3\xA8s refus\xC3\xA9", where(v, &o), why);
        return;
    }
    // Les parametres recus : une portee fille de celle de la vue (Ligne, Colonne, Valeur, Valeurs ; Index, Cle,
    // Membre, Element en lignes dynamiques ; Ancienne, Nouvelle ; Titre).
    const auto parent = scopePtr(v.id);
    auto sc = std::make_shared<Scope>(parent.get());
    const auto integer = [](long long x) { return sim::Value::integer(sim::Type::DInt, x); };
    if (t == Trigger::HeaderClick) {
        sc->setValue("Colonne", integer(column + 1));
        sc->setValue("Titre", sim::Value::text(title));
    } else if (row >= 0 && static_cast<std::size_t>(row) < m.rows.size()) {
        const auto& r = m.rows[static_cast<std::size_t>(row)];
        sc->setValue("Ligne", integer(row + 1));
        if (table::modeOf(o) == table::Mode::Rows && m.kind != table::SourceKind::None) {
            table::RowPaths rp;
            table::Source src;
            if (tableRowScope(v.id, o, r.position, *sc, &rp, &src)) {
                switch (src.kind) {
                    case table::SourceKind::Array:
                    case table::SourceKind::List: sc->setValue("Index", integer(std::atoll(r.index.c_str()))); break;
                    case table::SourceKind::Map:
                        if (src.key == "STRING" || src.key.empty()) sc->setValue("Cle", sim::Value::text(r.index));
                        else sc->setValue("Cle", integer(std::atoll(r.index.c_str())));
                        break;
                    case table::SourceKind::Struct: sc->setValue("Membre", sim::Value::text(r.index)); break;
                    default: break;
                }
                // Element : l'element de la ligne, par reference (un alias vers Cuves[2], l'objet en place d'une LIST).
                if (src.kind != table::SourceKind::Enum) {
                    if (auto obj = sc->object(table::kElt)) sc->setObject("Element", obj);
                    else if (const auto* al = sc->alias(table::kElt)) sc->setAlias("Element", *al);
                }
            }
        }
        if (t != Trigger::RowPick && column >= 0 && static_cast<std::size_t>(column) < r.cells.size()) {
            sc->setValue("Colonne", integer(column + 1));
            sc->setValue("Valeur", sim::Value::text(r.cells[static_cast<std::size_t>(column)].text));
        }
        if (!r.cells.empty()) {
            auto td = std::make_shared<sim::TypeDesc>();
            td->kind = sim::TypeDesc::Kind::Array;
            td->bounds = {{1, static_cast<std::int64_t>(r.cells.size())}};
            td->element = sim::scalarType(sim::Type::String);
            auto arr = sim::makeObj(td);
            for (std::size_t k = 0; k < r.cells.size() && k < arr->items.size(); ++k)
                if (arr->items[k]) arr->items[k]->value = sim::Value::text(r.cells[k].text);
            sc->setObject("Valeurs", arr);
        }
        if (t == Trigger::CellEdit) {
            if (before) sc->setValue("Ancienne", *before);
            if (after) sc->setValue("Nouvelle", *after);
        }
    }
    // Le geste : fire() prend cette portee pour les actions de cette vue (une popup ouverte garde la sienne).
    struct Restore {
        Runtime& rt;
        std::shared_ptr<const Scope> scope;
        Id view;
        ~Restore() {
            rt.gestureScope_ = std::move(scope);
            rt.gestureView_ = view;
        }
    } restore{*this, gestureScope_, gestureView_};
    gestureScope_ = sc;
    gestureView_ = v.id;
    for (auto& a : list) {
        // Element^ (une reference deref, comme un REF_TO) se lit Element : l'alias suit deja l'element.
        a.target = table::withoutCaret(a.target);
        a.value = table::withoutCaret(a.value);
        a.guard = table::withoutCaret(a.guard);
        a.params = table::withoutCaret(a.params);
        fire(v, &o, a, now, true);
    }
}

bool Runtime::tablePrompt(const View& v, const Object& o, const table::Model& m, long long row, int column) {
    if (row < 0 || static_cast<std::size_t>(row) >= m.rows.size() || column < 0) return false;
    const auto& r = m.rows[static_cast<std::size_t>(row)];
    const auto k = static_cast<std::size_t>(column);
    if (k >= r.cells.size() || r.cells[k].missing) return false;
    const std::string source = where(v, &o);
    std::string path, unit;
    if (table::modeOf(o) == table::Mode::Rows) {
        const auto cols = table::columnsOf(o);
        if (k >= cols.size()) return false;
        // Le niveau pour ecrire de la colonne (sous securite).
        if (cols[k].level > 0 && project_ && project_->security.enabled && cols[k].level > level()) {
            event("Acc\xC3\xA8s refus\xC3\xA9", source, "colonne " + cols[k].title + " : niveau " + std::to_string(cols[k].level) + " requis pour \xC3\xA9" "crire");
            return false;
        }
        Scope sc;
        table::RowPaths rp;
        table::Source src;
        if (!tableRowScope(v.id, o, r.position, sc, &rp, &src)) return false;
        path = table::writePathOf(cols[k].content, src, rp, project_);
        unit = cols[k].unit;
    } else {
        const auto cells = parseCells(o.text("cells"));
        if (static_cast<std::size_t>(row) >= cells.size() || k >= cells[static_cast<std::size_t>(row)].size()) return false;
        path = table::fixedWritePath(cells[static_cast<std::size_t>(row)][k]);
    }
    if (path.empty()) return false;
    const auto aliases = viewAliases(v.id);
    sim::Value cur;
    // Une variable a plat (Cuves[2].Consigne) se lit directement ; un objet (Stock['vis'], Alarmes[1].Priorite) par
    // l'interpreteur.
    const bool flat = envOf().read(path, cur);
    if (!flat) {
        auto it = expressions_.find(path);
        if (it == expressions_.end()) it = expressions_.emplace(path, Expression::compile(path)).first;
        auto val = it->second.valid() ? it->second.evaluate(envOf()) : decltype(it->second.evaluate(envOf())){};
        if (!it->second.valid() || !val) {
            log("Erreur", source, "case R/W : " + path + " ne se lit pas");
            return false;
        }
        cur = *val;
    }
    KeyboardPrompt p;
    p.target = resolvedPath(path, flat);
    p.type = cur.type();
    p.source = source;
    p.tableView = v.id;
    p.tableObject = o.id;
    p.tableRow = row;
    p.tableColumn = column;
    p.before = cur;
    const std::string head = k < m.headers.size() ? m.headers[k] : std::string{};
    const std::string first = r.cells.empty() ? std::string{} : r.cells.front().text;
    p.title = (head.empty() ? p.target : head) + (first.empty() || k == 0 ? std::string{} : " \xC2\xB7 " + first);
    // La plage : celle de la variable (Unites et formats), sinon celle de son type (un INT : -32768 a 32767).
    if (project_) {
        const View* shownIn = project_->view(v.id);
        const auto bound = [&](std::string_view key) -> std::optional<double> {
            const std::string t = trimmedText(ranges::inheritedOf(*project_, p.target, key, shownIn).value);
            if (t.empty()) return std::nullopt;
            double x = 0;
            if (roles::readNumber(t, x)) return x;
            if (const auto* var = variable(markers::strip(t)); var && sim::isNumeric(var->type())) return var->asReal();
            return std::nullopt;
        };
        p.min = bound("min");
        p.max = bound("max");
        if (const auto* d = displayOf(*project_, p.target, shownIn); d && unit.empty()) unit = d->unit;
        Expression e = Expression::compile(p.target);
        if (e.valid()) {
            const std::string declared = e.pathType(envOf(), nullptr);
            if (!declared.empty() && findEnumeration(*project_, declared)) p.enumType = declared;
            const auto num = typereg::numericOf(declared.empty() ? std::string(sim::toString(cur.type())) : declared);
            if (num.family == typereg::Family::Integer && p.enumType.empty()) {
                if (!p.min) p.min = static_cast<double>(num.low);
                if (!p.max) p.max = static_cast<double>(num.high);
            }
        }
    }
    p.unit = unit;
    p.keyboard = std::string(actionkinds::keyboardFor(std::string(sim::toString(cur.type())), p.enumType.empty() ? std::string_view{} : "complet"));
    const std::string target = p.target;
    prompt_ = std::move(p);
    promptForm_ = FormState{};
    promptForm_.focus = "valeur";
    promptForm_.text["valeur"] = formatValue(cur);
    if (!prompt_->enumType.empty() && project_)
        if (const auto* e = findEnumeration(*project_, prompt_->enumType))
            for (const auto& ev : e->values)
                if (ev.value == cur.asInteger()) promptForm_.text["valeur"] = ev.name;
    promptForm_.caret["valeur"] = promptForm_.text["valeur"].size();
    promptForm_.fresh = true;
    log("Action", source, "case R/W : " + target);
    return true;
}

} // namespace hmi
