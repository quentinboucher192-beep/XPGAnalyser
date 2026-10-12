// =============================================================================
//  app/hmi/HmiUnfold.hpp - 1.12.7 : LE DEBALLAGE D'UNE VALEUR A TAILLE
// -----------------------------------------------------------------------------
//  Une valeur du moteur (LIST, VECTOR, MAP, SET, RING, ARRAY a une ou deux
//  dimensions avec ses vrais indices, TUPLE, structure) lue element par element,
//  en arbre : une ligne par element (element, type, valeur), chaque element a
//  taille se deplie a son tour. Sous la grille des declarations (HmiDeclGrid) et
//  sous celle des locales d'un calcul (HmiCalcDialog).
// =============================================================================
#pragma once

#include "../../sim/Rich.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace app {

class HmiUnfoldModel final : public ui::ITableModel {
public:
    struct Line {
        std::string label, type, value, path;
        int         depth{0};
        bool        children{false};
    };
    std::vector<Line>     lines;
    std::set<std::string> open{""};          // les chemins deplies (la racine : d'emblee)
    sim::ObjRef           root;
    std::string           rootName;
    std::string           why;                // la valeur illisible : pourquoi

    static bool sized(const sim::ObjRef& o) { return o && o->type && o->type->kind != sim::TypeDesc::Kind::Scalar; }
    static std::string typeOf(const sim::ObjRef& o) {
        if (!o || !o->type) return {};
        return o->type->kind == sim::TypeDesc::Kind::Struct ? o->type->name : o->type->text();
    }
    static std::string valueOf(const sim::ObjRef& o) {
        if (!o) return {};
        if (!sized(o)) return o->value.display();
        const std::size_t n = o->type->kind == sim::TypeDesc::Kind::Map ? o->textKeys.size() + o->intKeys.size() : o->items.size();
        std::string text = sim::display(*o);
        if (text.size() > 80) text = text.substr(0, 77) + "...";
        // 1.12.7 : un RING dit sa capacite (« 3 elements sur 5 »).
        const std::string of = sim::isRingType(*o->type) ? " sur " + std::to_string(o->type->capacity) : std::string{};
        return std::to_string(n) + (n > 1 ? " \xC3\xA9l\xC3\xA9ments" : " \xC3\xA9l\xC3\xA9ment") + of + "  " + text;
    }
    void add(const sim::ObjRef& o, const std::string& label, const std::string& path, int depth) {
        if (lines.size() > 2000) return;                         // une tres grande valeur : le debut
        lines.push_back({label, typeOf(o), valueOf(o), path, depth, sized(o)});
        if (!sized(o) || !open.count(path)) return;
        const auto& t = *o->type;
        switch (t.kind) {
            case sim::TypeDesc::Kind::Map:
                for (const auto& [k, v] : o->textKeys) add(v, "'" + k + "'", path + "/'" + k + "'", depth + 1);
                for (const auto& [k, v] : o->intKeys) add(v, std::to_string(k), path + "/" + std::to_string(k), depth + 1);
                break;
            case sim::TypeDesc::Kind::Struct:
            case sim::TypeDesc::Kind::Tuple:
                for (std::size_t k = 0; k < o->items.size(); ++k) {
                    const std::string m = k < t.members.size() ? t.members[k].first : "Item" + std::to_string(k + 1);
                    add(o->items[k], m, path + "/" + m, depth + 1);
                }
                break;
            case sim::TypeDesc::Kind::Array: {
                // L'indice ecrit : les bornes de chaque dimension (la derniere varie le plus vite).
                for (std::size_t flat = 0; flat < o->items.size(); ++flat) {
                    std::vector<std::int64_t> idx(t.bounds.size(), 0);
                    std::size_t rest = flat;
                    for (std::size_t d = t.bounds.size(); d-- > 0;) {
                        const auto span = static_cast<std::size_t>(t.bounds[d].second - t.bounds[d].first + 1);
                        idx[d] = t.bounds[d].first + static_cast<std::int64_t>(span ? rest % span : 0);
                        rest = span ? rest / span : 0;
                    }
                    std::string label = "[";
                    for (std::size_t d = 0; d < idx.size(); ++d) label += (d ? ", " : "") + std::to_string(idx[d]);
                    label += "]";
                    add(o->items[flat], label, path + "/" + label, depth + 1);
                }
                break;
            }
            default:
                for (std::size_t k = 0; k < o->items.size(); ++k)
                    add(o->items[k], "[" + std::to_string(k) + "]", path + "/[" + std::to_string(k) + "]", depth + 1);
                break;
        }
    }
    // Ses colonnes (Element, Type, Valeur ; une table sans colonnes ne montre rien), sans tri.
    [[nodiscard]] static std::vector<ui::TableView::Column> columns() {
        return {{"\xC3\x89l\xC3\xA9ment", 240.f, 60.f, true, false, true, ui::Align::Start, 0, false},
                {"Type", 190.f, 50.f, true, false, true, ui::Align::Start, 0, false},
                {"Valeur", 560.f, 60.f, true, false, true, ui::Align::Start, 0, false}};
    }
    // Montrer `value` (nulle : rien, ou la raison `whyNot`) sous le nom `name` ; un autre nom : replie sous la racine.
    void show(const sim::ObjRef& value, const std::string& name, const std::string& whyNot) {
        if (name != rootName) open = {""};
        root = value;
        rootName = name;
        why = value ? std::string{} : whyNot;
        rebuild();
        modelReset->emit();
    }
    // La fleche d'une ligne : son chemin deplie ou replie.
    void toggle(ui::RowIndex r) {
        if (r >= lines.size()) return;
        const std::string path = lines[r].path;
        if (!open.erase(path)) open.insert(path);
        rebuild();
        modelReset->emit();
    }
    void rebuild() {
        lines.clear();
        if (root) add(root, rootName, "", 0);
        else if (!why.empty()) lines.push_back({rootName, {}, "illisible : " + why, "", 0, false});
    }

    [[nodiscard]] std::size_t rowCount() const override { return lines.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 3; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        return c == 0 ? std::string("\xC3\x89l\xC3\xA9ment") : c == 1 ? std::string("Type") : std::string("Valeur");
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= lines.size()) return {};
        const auto& l = lines[r];
        return c == 0 ? l.label : c == 1 ? l.type : l.value;
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= lines.size()) return s;
        const auto& l = lines[r];
        s.monospace = true;
        if (c == 0) {
            s.indent = 16.f * static_cast<float>(l.depth);
            s.expander = l.children ? (open.count(l.path) ? 1 : 0) : -1;
        }
        if (c == 1) s.fgTone = ui::Tone::Muted;
        return s;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
};

} // namespace app
