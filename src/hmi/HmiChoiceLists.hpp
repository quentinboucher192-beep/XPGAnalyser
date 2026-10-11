// =============================================================================
//  hmi/HmiChoiceLists.hpp - 1.12.7 (lot D de l'audit des objets, § 2.5) : CHOISIR
//                           AU LIEU DE TAPER
// -----------------------------------------------------------------------------
//  Ce que l'inspecteur propose en liste a choix, la ou il fallait taper un code
//  a retenir :
//   - les lettres d'un instrument ISA (PT, TT, FT, LT, PIC, TIC...), chacune avec
//     l'unite qu'elle propose (bar pour PT, degre Celsius pour TT) ;
//   - la position fixe d'une vanne 3 voies : « fermee », « 1-2 », « 1-3 »,
//     « 2-3 », au lieu des codes 0/1/2/3 (12/13/23). La valeur enregistree reste
//     le code (le moteur, le dessin et les projets d'avant la lisent telle quelle) ;
//     une variable garde ses codes.
//  Un nom tape a la main (un repere $Fonction$) reste accepte : la liste propose,
//  elle n'impose pas.
//
//  Et (§ 2.4) LES COLONNES D'UNE SOURCE DE TABLEAU (« Lignes depuis ») : lesquelles
//  se montrent, dans quel ordre, sous quel titre et a quel format - la propriete
//  `sourceColumns`, absente d'un tableau d'avant (toutes les colonnes, comme avant).
// =============================================================================
#pragma once

#include "HmiExpr.hpp"    // formatValue : le format d'une colonne
#include "HmiModel.hpp"   // parseNumber
#include "HmiRoles.hpp"   // le decoupeur unique, les textes sans leurs reperes
#include "HmiEnums.hpp"   // 1.12.7 (lot E) : une enumeration, source de lignes
#include "HmiMarkers.hpp"
#include "HmiSources.hpp" // 1.12.7 (lot E) : les membres simples d'une structure
#include "HmiTypeForms.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::choices {

// ---- l'instrument ISA ---------------------------------------------------------------
struct IsaFunction {
    std::string_view letters;   // "PT"
    std::string_view meaning;   // "transmetteur de pression"
    std::string_view unit;      // "bar" ; vide : aucune a proposer
};
inline constexpr IsaFunction kIsaFunctions[] = {
    {"PT", "transmetteur de pression", "bar"},
    {"PI", "indicateur de pression", "bar"},
    {"PIC", "r\xC3\xA9gulateur indicateur de pression", "bar"},
    {"PDT", "transmetteur de pression diff\xC3\xA9rentielle", "mbar"},
    {"PSH", "pressostat haut", "bar"},
    {"PSL", "pressostat bas", "bar"},
    {"TT", "transmetteur de temp\xC3\xA9rature", "\xC2\xB0" "C"},
    {"TI", "indicateur de temp\xC3\xA9rature", "\xC2\xB0" "C"},
    {"TIC", "r\xC3\xA9gulateur indicateur de temp\xC3\xA9rature", "\xC2\xB0" "C"},
    {"TSH", "thermostat haut", "\xC2\xB0" "C"},
    {"FT", "transmetteur de d\xC3\xA9" "bit", "m\xC2\xB3/h"},
    {"FI", "indicateur de d\xC3\xA9" "bit", "m\xC2\xB3/h"},
    {"FIC", "r\xC3\xA9gulateur indicateur de d\xC3\xA9" "bit", "m\xC2\xB3/h"},
    {"FQ", "totalisateur de d\xC3\xA9" "bit", "m\xC2\xB3"},
    {"LT", "transmetteur de niveau", "%"},
    {"LI", "indicateur de niveau", "%"},
    {"LIC", "r\xC3\xA9gulateur indicateur de niveau", "%"},
    {"LSH", "d\xC3\xA9tecteur de niveau haut", ""},
    {"LSL", "d\xC3\xA9tecteur de niveau bas", ""},
    {"AT", "analyseur", "ppm"},
    {"AIC", "r\xC3\xA9gulateur indicateur d'analyse", "ppm"},
    {"ST", "transmetteur de vitesse", "tr/min"},
    {"WT", "transmetteur de poids", "kg"},
    {"HS", "commande manuelle", ""},
    {"ZT", "transmetteur de position", "%"},
};

[[nodiscard]] inline const IsaFunction* isaFunction(std::string_view letters) noexcept {
    for (const auto& f : kIsaFunctions)
        if (f.letters == letters) return &f;
    return nullptr;
}
// L'unite que propose une fonction ISA ; vide : aucune (lettres inconnues, un repere).
[[nodiscard]] inline std::string_view isaUnitFor(std::string_view letters) noexcept {
    const auto* f = isaFunction(letters);
    return f ? f->unit : std::string_view{};
}

// ---- la vanne 3 voies -----------------------------------------------------------------
//  Les positions, dans l'ordre de la liste : fermee (0), 1-2 (1), 1-3 (2), 2-3 (3).
inline constexpr std::string_view kValve3Labels[] = {"ferm\xC3\xA9" "e", "1-2", "1-3", "2-3"};
inline constexpr std::string_view kValve3Codes[] = {"0", "1", "2", "3"};

// Le libelle d'un code fixe (1, 12 ou 21 : « 1-2 » ; 2, 13, 31 : « 1-3 » ; 3, 23, 32 : « 2-3 » ;
// 0 : « fermee ») ; vide : pas un code connu (un repere, un autre texte - montre tel quel).
[[nodiscard]] inline std::string_view valve3Label(std::string_view code) noexcept {
    std::string_view c = code;
    while (!c.empty() && (c.front() == ' ' || c.front() == '\t')) c.remove_prefix(1);
    while (!c.empty() && (c.back() == ' ' || c.back() == '\t')) c.remove_suffix(1);
    if (c == "0") return kValve3Labels[0];
    if (c == "1" || c == "12" || c == "21") return kValve3Labels[1];
    if (c == "2" || c == "13" || c == "31") return kValve3Labels[2];
    if (c == "3" || c == "23" || c == "32") return kValve3Labels[3];
    return {};
}
// Le code d'un libelle de la liste ; le texte tel quel sinon (un code tape, un repere).
[[nodiscard]] inline std::string valve3Code(std::string_view label) {
    for (std::size_t i = 0; i < std::size(kValve3Labels); ++i)
        if (kValve3Labels[i] == label) return std::string(kValve3Codes[i]);
    return std::string(label);
}

// ---- les colonnes d'une source de tableau (lot D, § 2.4) ----------------------------------
//  "Nom; Debit = Debit (m3/h) | 0.0; Etat" : les membres montres, dans l'ordre ; apres `=` le titre,
//  apres `|` le format (0.0, 0.00). Vide : toutes les colonnes de la source, sous leur nom.
inline constexpr std::string_view kSourceColumnsKey = "sourceColumns";
struct SourceColumn {
    std::string member, title, format;
};

namespace detail {
inline std::string trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return std::string(s);
}
inline bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
} // namespace detail

[[nodiscard]] inline std::vector<SourceColumn> parseSourceColumns(std::string_view text) {
    std::vector<SourceColumn> out;
    for (const auto& item : roles::splitList(text, ';')) {
        if (item.empty()) continue;
        SourceColumn c;
        std::string left = item;
        if (const auto bar = left.find('|'); bar != std::string::npos) {
            c.format = detail::trimmed(std::string_view(left).substr(bar + 1));
            left.erase(bar);
        }
        if (const auto eq = left.find('='); eq != std::string::npos) {
            c.title = detail::trimmed(std::string_view(left).substr(eq + 1));
            left.erase(eq);
        }
        c.member = detail::trimmed(left);
        if (!c.member.empty()) out.push_back(std::move(c));
    }
    return out;
}

[[nodiscard]] inline std::string formatSourceColumns(const std::vector<SourceColumn>& cols) {
    std::string out;
    for (const auto& c : cols) {
        if (!out.empty()) out += "; ";
        out += c.member;
        if (!c.title.empty()) out += " = " + c.title;
        if (!c.format.empty()) out += " | " + c.format;
    }
    return out;
}

// Les colonnes d'une source, refaites selon `spec` (vide : telles quelles) : celles-la, dans cet ordre,
// leur titre (sans les $ de ses reperes), leurs nombres a leur format. `missing` : les membres que la
// source n'a pas (Generer les dit). Aucune ne correspond : rien ne change (mieux vaut tout montrer).
inline void applySourceColumns(std::string_view spec, std::vector<std::string>& headers, std::vector<std::vector<std::string>>* rows,
                               std::vector<std::string>* missing = nullptr) {
    const auto cols = parseSourceColumns(spec);
    if (cols.empty()) return;
    std::vector<std::size_t> idx;
    std::vector<std::string> titles, formats;
    for (const auto& c : cols) {
        std::size_t found = headers.size();
        for (std::size_t i = 0; i < headers.size(); ++i)
            if (detail::sameName(detail::trimmed(headers[i]), c.member)) { found = i; break; }
        if (found == headers.size()) {
            if (missing) missing->push_back(c.member);
            continue;
        }
        idx.push_back(found);
        titles.push_back(c.title.empty() ? headers[found] : roles::plainText(c.title));
        formats.push_back(c.format);
    }
    if (idx.empty()) return;
    if (rows)
        for (auto& row : *rows) {
            std::vector<std::string> next;
            next.reserve(idx.size());
            for (std::size_t k = 0; k < idx.size(); ++k) {
                std::string cell = idx[k] < row.size() ? row[idx[k]] : std::string{};
                double x = 0;
                if (!formats[k].empty() && parseNumber(cell, x)) cell = formatValue(sim::Value::real(x), formats[k]);
                next.push_back(std::move(cell));
            }
            row = std::move(next);
        }
    headers = std::move(titles);
}

// 1.12.7 (lot E, § 2.3) : LES COLONNES QUE DONNE « LIGNES DEPUIS » - une variable IHM : celles de son type
// (typeform::columnsOf : les membres simples d'un tableau de structures, Cle / Valeur d'une MAP...) ; une
// structure seule : Membre, Valeur (une ligne par membre simple) ; une enumeration du projet : Nom, Valeur,
// Texte (une ligne par valeur). Vide : ni l'une ni l'autre (un parametre, un membre : connus en marche).
// Le moteur (Runtime::tableRows), l'apercu de l'editeur, Contenu et Generer lisent celles-ci.
[[nodiscard]] inline std::vector<std::string> rowsFromColumns(const Project& p, std::string_view from) {
    const std::string name = detail::trimmed(markers::strip(from, markers::Mode::Expression));
    if (name.empty()) return {};
    if (const auto* var = p.variable(name)) {
        if (typeform::decompose(var->type).form == typeform::Form::Simple && !sources::leafMembers(p, var->type).empty())
            return {"Membre", "Valeur"};
        return typeform::columnsOf(p, var->type);
    }
    if (findEnumeration(p, name)) return {"Nom", "Valeur", "Texte"};
    return {};
}

} // namespace hmi::choices
