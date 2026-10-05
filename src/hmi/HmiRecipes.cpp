#include "HmiRecipes.hpp"

#include "HmiHistory.hpp"

#include <algorithm>
#include <cctype>

namespace hmi {

namespace {

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

// "Pression (bar)" -> "pression" : le nom d'un element dans une entete.
std::string headerKey(std::string s) {
    if (const auto p = s.find(" ("); p != std::string::npos) s.erase(p);
    s = trim(std::move(s));
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::string recipeCsv(const Recipe& r) {
    std::string out = "Jeu";
    for (const auto& f : r.fields) out += ";" + csvField(f.unit.empty() ? f.name : f.name + " (" + f.unit + ")");
    out += "\n";
    for (const auto& rec : r.records) {
        out += csvField(rec.name);
        for (std::size_t i = 0; i < r.fields.size(); ++i) out += ";" + csvField(i < rec.values.size() ? rec.values[i] : std::string{});
        out += "\n";
    }
    return out;
}

bool importRecipeCsv(Project& p, Id recipeId, std::string_view text, RecipeImport* report, std::string* error) {
    auto* r = p.recipe(recipeId);
    if (!r) { if (error) *error = "recette introuvable"; return false; }
    // Les lignes, sans les vides.
    std::vector<std::string_view> lines;
    for (std::size_t pos = 0; pos < text.size();) {
        const auto eol = text.find('\n', pos);
        std::string_view line = text.substr(pos, (eol == std::string_view::npos ? text.size() : eol) - pos);
        pos = eol == std::string_view::npos ? text.size() : eol + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!trim(std::string(line)).empty()) lines.push_back(line);
    }
    if (lines.empty()) { if (error) *error = "fichier vide"; return false; }
    if (lines.front().size() >= 3 && static_cast<unsigned char>(lines.front()[0]) == 0xEF) lines.front().remove_prefix(3);   // BOM
    const char sep = lines.front().find(';') != std::string_view::npos ? ';'
                   : lines.front().find('\t') != std::string_view::npos ? '\t' : ',';
    const auto header = csvSplit(lines.front(), sep);
    if (header.size() < 2) { if (error) *error = "l'ent\xC3\xAAte doit avoir le nom du jeu puis au moins un \xC3\xA9l\xC3\xA9ment"; return false; }
    // Colonne -> element.
    std::vector<int> column(header.size(), -1);
    RecipeImport local;
    auto& rep = report ? *report : local;
    for (std::size_t c = 1; c < header.size(); ++c) {
        const std::string key = headerKey(header[c]);
        for (std::size_t f = 0; f < r->fields.size(); ++f)
            if (headerKey(r->fields[f].name) == key) column[c] = static_cast<int>(f);
        if (column[c] < 0) rep.warnings.push_back("colonne \xC2\xAB " + header[c] + " \xC2\xBB : aucun \xC3\xA9l\xC3\xA9ment de ce nom, ignor\xC3\xA9" "e");
    }
    for (std::size_t l = 1; l < lines.size(); ++l) {
        const auto cells = csvSplit(lines[l], sep);
        const std::string name = cells.empty() ? std::string{} : trim(cells[0]);
        if (name.empty()) { rep.warnings.push_back("ligne " + std::to_string(l + 1) + " : jeu sans nom, ignor\xC3\xA9"); continue; }
        RecipeRecord* rec = nullptr;
        for (auto& existing : r->records) if (existing.name == name) rec = &existing;
        if (rec) ++rep.replaced;
        else {
            RecipeRecord fresh;
            fresh.id = p.allocate();
            r = p.recipe(recipeId);          // allocate() ne deplace rien, mais restons prudents
            fresh.name = name;
            r->records.push_back(std::move(fresh));
            rec = &r->records.back();
            ++rep.added;
        }
        rec->values.resize(r->fields.size());
        for (std::size_t c = 1; c < cells.size() && c < column.size(); ++c)
            if (column[c] >= 0) rec->values[static_cast<std::size_t>(column[c])] = trim(cells[c]);
        rec->modified = wallStamp().substr(0, 19);
    }
    return true;
}

bool sameRecipeValue(std::string_view a, std::string_view b) {
    double x = 0, y = 0;
    if (parseNumber(a, x) && parseNumber(b, y)) return x == y;
    const auto strip = [](std::string_view s) {
        std::string t(s);
        if (t.size() >= 2 && (t.front() == '\'' || t.front() == '"') && t.back() == t.front()) t = t.substr(1, t.size() - 2);
        for (auto& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return t;
    };
    return strip(a) == strip(b);
}

std::vector<RecipeDiff> compareValues(const Recipe& r, const std::vector<std::string>& left, const std::vector<std::string>& right) {
    std::vector<RecipeDiff> out;
    for (std::size_t i = 0; i < r.fields.size(); ++i) {
        RecipeDiff d;
        d.field = r.fields[i].name;
        d.unit = r.fields[i].unit;
        d.left = i < left.size() ? left[i] : std::string{};
        d.right = i < right.size() ? right[i] : std::string{};
        d.differs = !sameRecipeValue(d.left, d.right);
        out.push_back(std::move(d));
    }
    return out;
}

} // namespace hmi
