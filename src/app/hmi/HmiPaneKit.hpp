// =============================================================================
//  app/hmi/HmiPaneKit.hpp — les petits outils communs des volets du lot 4
// -----------------------------------------------------------------------------
//  Un modele de tableau fait de lignes de texte (et d'un style par case), les
//  proprietes de grille en une ligne, et quelques operations de texte. Interne
//  aux volets (HmiSupervisionPanes, HmiSecurityPane, HmiExchangePanes).
// =============================================================================
#pragma once

#include "../../hmi/HmiCheck.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace app::hmikit {

class Rows final : public ui::ITableModel {
public:
    using Style = std::function<ui::CellStyle(ui::RowIndex, std::size_t)>;
    Rows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, Style style = {})
        : headers_(std::move(headers)), rows_(std::move(rows)), style_(std::move(style)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : ""; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        return style_ ? style_(r, c) : ui::CellStyle{};
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override {
        return cellText(a, c) < cellText(b, c);
    }
private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    Style style_;
};

inline std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
inline bool same(std::string_view a, std::string_view b) { return lower(std::string(a)) == lower(std::string(b)); }

inline std::string trimmed(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

// "a; b ;c" ou "a,b" -> {a, b, c}
inline std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ';' || c == ',') {
            if (!trimmed(cur).empty()) out.push_back(trimmed(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!trimmed(cur).empty()) out.push_back(trimmed(cur));
    return out;
}

inline std::string joinList(const std::vector<std::string>& v, const char* sep = "; ") {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : sep) + s;
    return out;
}

inline bool yes(const std::string& v) { return hmi::parseBool(v, false); }
inline const char* tf(bool b) { return b ? "TRUE" : "FALSE"; }

// Une propriete de grille ; sans `commit`, en lecture seule.
inline ui::PropertyGrid::Property prop(std::string name, std::string value, ui::PropertyGrid::ValueType type,
                                       std::function<bool(std::string_view)> commit = {}, std::string help = {},
                                       std::vector<std::string> choices = {}) {
    ui::PropertyGrid::Property p;
    p.name = std::move(name);
    p.value = std::move(value);
    p.type = commit ? type : ui::PropertyGrid::ValueType::ReadOnly;
    p.commit = std::move(commit);
    p.description = std::move(help);
    p.enumValues = std::move(choices);
    if (p.type == ui::PropertyGrid::ValueType::Enum
        && std::find(p.enumValues.begin(), p.enumValues.end(), p.value) == p.enumValues.end())
        p.enumValues.push_back(p.value);
    return p;
}

// "a, b, c (+4)"
inline std::string fewOf(const std::vector<std::string>& items, std::size_t n = 3) {
    std::string out;
    for (std::size_t i = 0; i < items.size() && i < n; ++i) out += (i ? ", " : "") + items[i];
    if (items.size() > n) out += " (+" + std::to_string(items.size() - n) + ")";
    return out;
}

inline int selectedRow(const ui::TableView& t) {
    const auto rows = t.selectedModelRows();
    return rows.empty() ? -1 : static_cast<int>(rows.front());
}

// Les actions d'une operation qui visent `target` (sans casse) : "Vue/Objet".
inline std::vector<std::string> actionUses(const hmi::Project& p, hmi::Operation op, const std::string& target) {
    std::vector<std::string> out;
    for (const auto& v : p.views) {
        for (const auto& a : v.actions)
            if (a.operation == op && same(a.target, target)) out.push_back(v.name + " (action de vue)");
        for (const auto& o : v.objects) {
            for (const auto& a : o.actions)
                if (a.operation == op && same(a.target, target)) out.push_back(v.name + "/" + o.name);
            // Lot 6 : un gestionnaire de recettes montre (et change) la recette.
            if (op == hmi::Operation::LoadRecipe && o.kind == hmi::Kind::RecipeManager && same(o.text("recipe"), target))
                out.push_back(v.name + "/" + o.name + " (gestionnaire)");
        }
    }
    return out;
}

// Les actions qui visaient `from` visent `to` (un renommage) ; rend leur nombre.
inline std::size_t retarget(hmi::Project& p, hmi::Operation op, const std::string& from, const std::string& to) {
    std::size_t n = 0;
    for (auto& v : p.views) {
        for (auto& a : v.actions)
            if (a.operation == op && same(a.target, from)) { a.target = to; ++n; }
        for (auto& o : v.objects) {
            for (auto& a : o.actions)
                if (a.operation == op && same(a.target, from)) { a.target = to; ++n; }
            if (op == hmi::Operation::LoadRecipe && o.kind == hmi::Kind::RecipeManager && same(o.text("recipe"), from)) {
                o.set("recipe", to);
                ++n;
            }
        }
    }
    return n;
}

// Les chemins de variables d'une expression, entiers (Armoires[0].ana.PT1.mes),
// sans les mots de la langue (AND, OR, NOT, TRUE, FALSE, MOD, XOR) ni les
// appels de fonction ; hors des chaines entre apostrophes.
inline std::vector<std::string> variablePaths(std::string_view e) {
    static const char* words[] = {"AND", "OR", "NOT", "XOR", "MOD", "TRUE", "FALSE"};
    std::vector<std::string> out;
    std::size_t i = 0;
    const auto ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    while (i < e.size()) {
        const char c = e[i];
        if (c == '\'') {                                   // une chaine : sautee entiere
            ++i;
            while (i < e.size() && e[i] != '\'') ++i;
            ++i;
            continue;
        }
        if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_')) { ++i; continue; }
        std::size_t j = i;
        int depth = 0;
        while (j < e.size()) {
            if (depth == 0 && (ident(e[j]) || e[j] == '.')) { ++j; continue; }
            if (e[j] == '[') { ++depth; ++j; continue; }
            if (e[j] == ']' && depth > 0) { --depth; ++j; continue; }
            if (depth > 0) { ++j; continue; }
            break;
        }
        std::string path(e.substr(i, j - i));
        std::size_t k = j;
        while (k < e.size() && e[k] == ' ') ++k;
        const bool call = k < e.size() && e[k] == '(';
        std::string upper = path;
        for (auto& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        bool keyword = false;
        for (const char* w : words) keyword = keyword || upper == w;
        // T#5s, 16#FF : des litteraux, pas des variables.
        const bool literal = j < e.size() && e[j] == '#';
        if (!call && !keyword && !literal && std::find(out.begin(), out.end(), path) == out.end()) out.push_back(path);
        i = j;
        if (literal) while (i < e.size() && (ident(e[i]) || e[i] == '#' || e[i] == '.')) ++i;
    }
    return out;
}

// ---- lot 13 : la couleur de la gravite ------------------------------------------------
//  LA MEME DANS GENERER, COMPILER ET LE CONTROLE DE L'ECHANGE : une erreur en
//  rouge, un avertissement en orange, une information en bleu. La premiere
//  colonne (l'icone et le mot), le message, et un fond teinte sur toute la
//  ligne pour les erreurs et les avertissements : ce qui bloque se voit de loin.
inline int severityRank(hmi::Issue::Severity s) {
    return s == hmi::Issue::Severity::Error ? 0 : s == hmi::Issue::Severity::Warning ? 1 : 2;
}
inline ui::Tone severityTone(hmi::Issue::Severity s) {
    return s == hmi::Issue::Severity::Error ? ui::Tone::Error : s == hmi::Issue::Severity::Warning ? ui::Tone::Warning : ui::Tone::Info;
}
inline ui::CellStyle issueCellStyle(hmi::Issue::Severity s, std::size_t column, std::size_t messageColumn) {
    ui::CellStyle st;
    const ui::Tone tone = severityTone(s);
    if (s == hmi::Issue::Severity::Error) st.bg = gfx::Color{229, 83, 75, 40};
    else if (s == hmi::Issue::Severity::Warning) st.bg = gfx::Color{242, 153, 74, 30};
    if (column == 0) {
        st.icon = s == hmi::Issue::Severity::Error ? ui::Icon::Error : s == hmi::Issue::Severity::Warning ? ui::Icon::Warning : ui::Icon::Info;
        st.iconTone = tone;
        st.fgTone = tone;
        st.bold = s != hmi::Issue::Severity::Info;
    } else if (column == messageColumn) {
        st.fgTone = tone;
    }
    return st;
}
// Les erreurs d'abord, puis les avertissements, puis les informations ; a
// gravite egale, l'ordre des controles.
inline void sortIssues(std::vector<hmi::Issue>& issues) {
    std::stable_sort(issues.begin(), issues.end(),
                     [](const hmi::Issue& a, const hmi::Issue& b) { return severityRank(a.severity) < severityRank(b.severity); });
}
// Un tableau de constats : une gravite par ligne ; trier la premiere colonne
// range par gravite (pas par ordre alphabetique des mots).
class IssueRows final : public ui::ITableModel {
public:
    IssueRows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, std::vector<hmi::Issue::Severity> severities,
              std::size_t messageColumn)
        : headers_(std::move(headers)), rows_(std::move(rows)), sev_(std::move(severities)), message_(messageColumn) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : ""; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        return r < sev_.size() ? issueCellStyle(sev_[r], c, message_) : ui::CellStyle{};
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override {
        if (c == 0 && a < sev_.size() && b < sev_.size()) return severityRank(sev_[a]) < severityRank(sev_[b]);
        return cellText(a, c) < cellText(b, c);
    }
private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<hmi::Issue::Severity> sev_;
    std::size_t message_{0};
};

} // namespace app::hmikit
