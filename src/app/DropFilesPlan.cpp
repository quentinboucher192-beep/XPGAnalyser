// =============================================================================
//  app/DropFilesPlan.cpp - lot API 8 : ce qu'on peut faire des fichiers deposes
//  (voir l'en-tete). Rien n'est dessine ni modifie ici.
// =============================================================================
#include "DropFilesPlan.hpp"

#include "../domain/ProjectModel.hpp"
#include "../hmi/HmiAssets.hpp"
#include "../hmi/HmiLanguages.hpp"
#include "../hmi/HmiMedia.hpp"
#include "../hmi/HmiTypes.hpp"
#include "../project/ApiCommands.hpp"
#include "../project/MacroSpec.hpp"
#include "../project/MastImport.hpp"
#include "../project/SharedLibrary.hpp"
#include "../project/Table.hpp"
#include "../xls/Workbook.hpp"
#include "../xls/XlsmSource.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <system_error>

namespace app::dropfiles {

namespace fs = std::filesystem;
namespace mm = project::macro;

namespace {

constexpr std::size_t npos = static_cast<std::size_t>(-1);
// Au-dela, une ressource n'est pas relue pour son apercu (elle l'est a l'ajout).
constexpr std::uint64_t kPreviewBytes = 64ull * 1024 * 1024;
constexpr std::uint64_t kResourceBytes = 256ull * 1024 * 1024;     // hmi::readResource

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string leafOf(const std::string& path) { return fs::path(path).filename().string(); }
std::string stemOf(const std::string& path) { return fs::path(path).stem().string(); }
std::string extOf(const std::string& path) { return lower(fs::path(path).extension().string()); }

std::string join(const std::vector<std::string>& items, std::string_view sep = ", ") {
    std::string out;
    for (const auto& s : items) {
        if (!out.empty()) out += sep;
        out += s;
    }
    return out;
}

// "Nom, Type, Adresse et 4 autres".
std::string joinSome(const std::vector<std::string>& items, std::size_t max) {
    if (items.size() <= max) return join(items);
    std::vector<std::string> head(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(max));
    const std::size_t rest = items.size() - max;
    return join(head) + " et " + std::to_string(rest) + (rest > 1 ? " autres" : " autre");
}

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n > 1 ? many : one);
}

// La lettre de base d'une lettre accentuee (U+00C0..U+00FF, en UTF-8 : C3 xx).
char baseLetter(unsigned char second) {
    const auto in = [second](int lo, int hi) { return second >= lo && second <= hi; };
    if (in(0x80, 0x85) || in(0xA0, 0xA5)) return 'a';
    if (second == 0x87 || second == 0xA7) return 'c';
    if (in(0x88, 0x8B) || in(0xA8, 0xAB)) return 'e';
    if (in(0x8C, 0x8F) || in(0xAC, 0xAF)) return 'i';
    if (second == 0x91 || second == 0xB1) return 'n';
    if (in(0x92, 0x96) || in(0xB2, 0xB6)) return 'o';
    if (in(0x99, 0x9C) || in(0xB9, 0xBC)) return 'u';
    if (second == 0x9D || second == 0xBD || second == 0xBF) return 'y';
    return 0;
}

// Un titre de colonne, pour le comparer : lettres et chiffres, sans casse ni
// accents ("Qualite (en marche)" = "qualiteenmarche", "R/W" = "rw") - la meme
// regle que le collage des tableaux (TablePaste::normalizedTitle).
std::string titleKey(std::string_view title) {
    std::string out;
    for (std::size_t i = 0; i < title.size(); ++i) {
        const auto c = static_cast<unsigned char>(title[i]);
        if (c >= 0x80) {
            if (c == 0xC3 && i + 1 < title.size())
                if (const char b = baseLetter(static_cast<unsigned char>(title[i + 1]))) {
                    out += b;
                    ++i;
                }
            continue;
        }
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    }
    return out;
}

// "Pression (bar)" -> "pression" : le nom d'un element de recette dans un
// en-tete, comme hmi::importRecipeCsv le lit.
std::string recipeKey(std::string s) {
    if (const auto p = s.find(" ("); p != std::string::npos) s.erase(p);
    return lower(trim(s));
}

// ---- les tableaux d'un fichier -----------------------------------------------------
struct Grid {
    std::string                           name;       // l'onglet ; le nom du fichier pour un CSV
    std::vector<std::string>              columns;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string>              settings;   // Config : les cles du formulaire (Setting)
    [[nodiscard]] std::string cell(std::size_t r, std::size_t c) const {
        return r < rows.size() && c < rows[r].size() ? rows[r][c] : std::string{};
    }
    [[nodiscard]] std::size_t filled(std::size_t c) const {
        std::size_t n = 0;
        for (std::size_t r = 0; r < rows.size(); ++r)
            if (!trim(cell(r, c)).empty()) ++n;
        return n;
    }
};

bool numeric(const std::string& t) {
    if (t.empty()) return false;
    char* stop = nullptr;
    std::strtod(t.c_str(), &stop);
    return stop != nullptr && *stop == '\0';
}

// Un onglet sans en-tete reconnu par le lecteur (moins de trois titres) : la
// premiere des quinze premieres lignes qui porte au moins deux libelles, et
// aucun nombre ; les donnees s'arretent a la premiere premiere-case vide.
void fallbackGrid(const xls::Sheet& s, Grid& g) {
    constexpr std::size_t kCols = 64;
    const std::size_t limit = std::min<std::size_t>(s.rawLineCount(), 15);
    for (std::size_t l = 0; l < limit; ++l) {
        std::size_t count = 0;
        bool number = false;
        std::size_t last = 0;
        for (std::size_t c = 0; c < kCols; ++c) {
            const auto t = trim(s.raw(l, c));
            if (t.empty()) continue;
            ++count;
            last = c + 1;
            if (numeric(t)) number = true;
        }
        if (count < 2 || number) continue;
        for (std::size_t c = 0; c < last; ++c) g.columns.push_back(trim(s.raw(l, c)));
        std::size_t first = 0;
        while (first < g.columns.size() && g.columns[first].empty()) ++first;
        for (std::size_t r = l + 1; r < s.rawLineCount(); ++r) {
            if (trim(s.raw(r, first)).empty()) break;
            std::vector<std::string> row;
            for (std::size_t c = 0; c < g.columns.size(); ++c) row.push_back(s.raw(r, c));
            g.rows.push_back(std::move(row));
        }
        return;
    }
}

bool readGrids(const std::string& path, Genre genre, std::vector<Grid>& out, std::string& error, bool* vba = nullptr) {
    out.clear();
    if (genre == Genre::Table) {
        const std::string bytes = readAll(path);
        if (bytes.empty()) {
            error = "fichier vide ou illisible";
            return false;
        }
        project::TableOptions o;
        o.descriptionRows = 0;
        const auto t = project::Table::parse(bytes, o);
        Grid g;
        g.name = leafOf(path);
        g.columns = t.headers();
        for (std::size_t r = 0; r < t.rowCount(); ++r) {
            std::vector<std::string> row;
            for (std::size_t c = 0; c < t.columnCount(); ++c) row.push_back(t.cell(r, c));
            g.rows.push_back(std::move(row));
        }
        if (g.columns.empty()) {
            error = "aucune colonne lue";
            return false;
        }
        out.push_back(std::move(g));
        return true;
    }
    if (extOf(path) == ".xls") {
        error = "un .xls (Excel 97-2003) ne se lit pas : enregistre-le en .xlsx dans Excel";
        return false;
    }
    auto wb = xls::Workbook::open(path);
    if (!wb) {
        error = wb.error().context.empty() ? wb.error().message() : wb.error().context;
        return false;
    }
    if (vba) *vba = xls::inspect(path).hasMacros;
    for (const auto& name : wb->sheetNames()) {
        const xls::Sheet* s = wb->sheet(name);
        if (!s) continue;
        Grid g;
        g.name = name;
        if (s->headerLine() > 0) {
            g.columns = s->columns();
            for (std::size_t r = 0; r < s->rowCount(); ++r) {
                std::vector<std::string> row;
                row.reserve(g.columns.size());
                for (std::size_t c = 0; c < g.columns.size(); ++c) row.push_back(s->cell(r, c));
                g.rows.push_back(std::move(row));
            }
        } else {
            fallbackGrid(*s, g);
        }
        // L'onglet Config est un formulaire : une cle, sa valeur a droite
        // (xls::Workbook::setting) - ses lignes etroites, leur premiere case.
        if (name == "Config")
            for (std::size_t l = 0; l < s->rawLineCount(); ++l) {
                std::vector<std::string> cells;
                for (std::size_t c = 0; c < 24; ++c)
                    if (auto t = trim(s->raw(l, c)); !t.empty()) cells.push_back(std::move(t));
                if (cells.size() >= 2 && cells.size() <= 4) g.settings.push_back(cells.front());
            }
        out.push_back(std::move(g));
    }
    return true;
}

// ---- les macros de la bibliotheque ----------------------------------------------------
struct LibMacro {
    std::string              name;
    mm::MacroSpec            spec;
    std::vector<std::string> columns;     // Cell(..., 'Nom'), HasColumn(..., 'Nom')
};

std::vector<LibMacro> libraryMacros(const std::string& libsRoot) {
    std::vector<LibMacro> out;
    std::error_code ec;
    if (libsRoot.empty() || !fs::is_directory(libsRoot, ec)) return out;
    project::SharedLibrary library(libsRoot);
    if (!library.scan()) return out;
    for (const auto& item : library.items()) {
        if (item.kind != project::LibraryItemKind::Macro) continue;
        const std::string source = readAll(item.path);
        if (source.empty()) continue;
        LibMacro m;
        m.name = item.name;
        m.spec = mm::parseMacroSpec(source, item.name);
        m.columns = cellColumns(source);
        out.push_back(std::move(m));
    }
    std::sort(out.begin(), out.end(), [](const LibMacro& a, const LibMacro& b) { return lower(a.name) < lower(b.name); });
    return out;
}

const LibMacro* findMacro(const std::vector<LibMacro>& all, std::string_view name) {
    for (const auto& m : all)
        if (lower(m.name) == lower(std::string(name))) return &m;
    return nullptr;
}

// Les onglets qu'elle lit - les siens, puis ceux des macros qu'elle lance quand
// elle ne les declare pas ("#! lit") -, comme la fiche de l'onglet Macros
// (MacrosPane::readsOf) ; et les colonnes que tout ce code lit.
void readsOf(const std::vector<LibMacro>& all, const LibMacro& root, std::vector<std::string>& required,
             std::vector<std::string>& optional, std::vector<std::string>& columns) {
    std::set<std::string> visited;
    const auto addTo = [](std::vector<std::string>& v, const std::string& s) {
        if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
    };
    std::function<void(const LibMacro&, int)> walk = [&](const LibMacro& m, int depth) {
        if (depth > 3 || !visited.insert(lower(m.name)).second) return;
        for (const auto& r : m.spec.reads) addTo(required, r);
        for (const auto& r : m.spec.readsOptional) addTo(optional, r);
        for (const auto& c : m.columns) addTo(columns, c);
        for (const auto& l : m.spec.launches)
            if (const auto* launched = findMacro(all, l)) {
                // Declarees ("#! lit") : ses onglets sont dits ; les colonnes des
                // macros lancees servent quand meme au detail.
                if (m.spec.readsDeclared) {
                    for (const auto& c : launched->columns) addTo(columns, c);
                } else {
                    walk(*launched, depth + 1);
                }
            }
    };
    walk(root, 0);
    std::erase_if(optional, [&](const std::string& s) { return std::find(required.begin(), required.end(), s) != required.end(); });
}

// Le champ qui recoit un classeur : un champ fichier .xlsx / .xlsm (le sien,
// sinon celui d'une macro qu'elle lance).
std::string workbookField(const std::vector<LibMacro>& all, const LibMacro& m, int depth = 0) {
    for (const auto& f : m.spec.fields) {
        if (f.kind != mm::FieldKind::File) continue;
        for (const auto& e : f.extensions)
            if (lower(e) == ".xlsx" || lower(e) == ".xlsm") return f.key;
    }
    if (depth < 2)
        for (const auto& l : m.spec.launches)
            if (const auto* launched = findMacro(all, l))
                if (auto key = workbookField(all, *launched, depth + 1); !key.empty()) return key;
    return {};
}

// ---- les colonnes reconnues des variables IHM -------------------------------------------
//  Les memes titres que le collage de IHM > Variables (HmiVariablesPane::pasteTarget).
struct Title {
    const char*              title;
    std::vector<const char*> aliases;
};
const std::vector<Title>& hmiVariableTitles() {
    static const std::vector<Title> titles = {
        {"Nom", {"Name", "Variable", "Mnemonique", "Symbole", "Tag", "Identifiant"}},
        {"Type", {"Type de donnee", "DataType", "Data type"}},
        {"Initiale", {"Valeur initiale", "Initial", "Init", "Initial value", "Valeur par defaut", "Defaut"}},
        {"\xC3\x89quipement", {"Equipment", "Esclave", "Automate", "Liaison", "Device"}},
        {"Adresse", {"Address", "Registre", "Adresse Modbus", "Mot", "Register"}},
        {"Acc\xC3\xA8s", {"Access", "Lecture/ecriture", "R/W", "Mode"}},
        {"Place Modbus", {"Place"}},
        {"Qualit\xC3\xA9 (en marche)", {"Qualite"}},
        {"Description", {"Commentaire", "Comment", "Libelle", "Designation"}},
        {"Dossier", {"Folder", "Groupe", "Repertoire"}},
    };
    return titles;
}
// Le titre reconnu d'une colonne ("" : aucun).
std::string hmiTitleOf(const std::string& column) {
    const auto key = titleKey(column);
    if (key.empty()) return {};
    for (const auto& t : hmiVariableTitles()) {
        if (titleKey(t.title) == key) return t.title;
        for (const char* a : t.aliases)
            if (titleKey(a) == key) return t.title;
    }
    return {};
}

bool validHmiType(const Context& ctx, const std::string& type) {
    if (ctx.hmi) return hmi::types::validType(*ctx.hmi, hmi::types::normalized(type));
    return hmi::types::isElementary(type);
}

std::string sheetSuffix(const Grid& g, Genre genre) {
    return genre == Genre::Workbook ? " \xE2\x80\x94 onglet \xC2\xAB " + g.name + " \xC2\xBB" : std::string{};
}

// ---- les detecteurs : une ligne chacun, ou rien -------------------------------------------
std::optional<Action> hmiVariablesAction(const Grid& g, Genre genre, const Context& ctx, bool single) {
    std::size_t keyCol = npos, typeCol = npos;
    std::vector<std::string> recognized, ignored;
    for (std::size_t c = 0; c < g.columns.size(); ++c) {
        if (trim(g.columns[c]).empty()) continue;
        const auto title = hmiTitleOf(g.columns[c]);
        if (title.empty()) {
            ignored.push_back(g.columns[c]);
            continue;
        }
        recognized.push_back(g.columns[c]);
        if (title == std::string("Nom") && keyCol == npos) keyCol = c;
        if (title == std::string("Type") && typeCol == npos) typeCol = c;
    }
    // Un nom et un type ; et plus de colonnes reconnues qu'ignorees : un
    // catalogue (Famille, Genre, Fichier, Portee...) n'est pas une liste de variables.
    if (keyCol == npos || typeCol == npos || recognized.size() < ignored.size()) return std::nullopt;
    std::size_t n = 0, typed = 0, valid = 0, existing = 0;
    std::vector<std::string> unknownTypes;
    for (std::size_t r = 0; r < g.rows.size(); ++r) {
        const auto name = trim(g.cell(r, keyCol));
        if (name.empty()) continue;
        ++n;
        const auto type = trim(g.cell(r, typeCol));
        if (!type.empty()) {
            ++typed;
            if (validHmiType(ctx, type)) ++valid;
            else if (std::find(unknownTypes.begin(), unknownTypes.end(), type) == unknownTypes.end()) unknownTypes.push_back(type);
        }
        if (ctx.hmi && ctx.hmi->variable(name)) ++existing;
    }
    if (n == 0 || typed == 0 || valid * 2 < typed) return std::nullopt;
    Action a;
    a.kind = ActionKind::HmiVariables;
    a.sheet = genre == Genre::Workbook ? g.name : std::string{};
    a.rows = n;
    a.label = "Importer " + plural(n, "variable IHM", "variables IHM") + sheetSuffix(g, genre);
    a.detail = "Colonnes reconnues : " + joinSome(recognized, 8) + "."
             + (ignored.empty() ? std::string{} : " Ignor\xC3\xA9" "es : " + joinSome(ignored, 5) + ".") + "\n";
    if (existing == n)
        a.detail += n > 1 ? "Toutes d\xC3\xA9j\xC3\xA0 dans l'IHM : mises \xC3\xA0 jour" : "D\xC3\xA9j\xC3\xA0 dans l'IHM : mise \xC3\xA0 jour";
    else if (existing > 0)
        a.detail += plural(n - existing, "nouvelle", "nouvelles") + ", " + std::to_string(existing)
                  + (existing > 1 ? " d\xC3\xA9j\xC3\xA0 dans l'IHM (mises \xC3\xA0 jour)" : " d\xC3\xA9j\xC3\xA0 dans l'IHM (mise \xC3\xA0 jour)");
    else
        a.detail += n > 1 ? "Toutes nouvelles" : "Nouvelle";
    if (valid < typed)
        a.detail += " ; " + plural(typed - valid, "type inconnu", "types inconnus") + " (" + joinSome(unknownTypes, 3)
                  + ") : marqu\xC3\xA9" + (typed - valid > 1 ? "s" : "") + " dans le tableau, une variable nouvelle est cr\xC3\xA9\xC3\xA9" "e en REAL";
    a.detail += ".\nComme un collage depuis Excel dans IHM > Programmation g\xC3\xA9n\xC3\xA9rale > Variables : un Ctrl+Z.";
    if (!ctx.hmi) {
        a.possible = false;
        a.why = "le projet n'a pas d'IHM";
    }
    a.checked = a.possible && single;
    return a;
}

std::optional<Action> translationsAction(const Grid& g, Genre genre, const Context& ctx) {
    std::size_t textCol = npos;
    std::string code;
    for (std::size_t c = 0; c < g.columns.size() && textCol == npos; ++c) {
        const auto t = trim(g.columns[c]);
        if (t.size() > 8 && t.rfind("Texte (", 0) == 0 && t.back() == ')') {
            textCol = c;
            code = t.substr(7, t.size() - 8);
        }
    }
    if (textCol == npos) return std::nullopt;
    std::vector<std::string> languages;
    for (std::size_t c = 0; c < g.columns.size(); ++c) {
        if (c == textCol) continue;
        std::string name, lcode;
        if (hmi::parseLanguageHeader(trim(g.columns[c]), name, lcode) && lower(lcode) != lower(code)) languages.push_back(trim(g.columns[c]));
    }
    const std::size_t n = g.filled(textCol);
    Action a;
    a.kind = ActionKind::Translations;
    a.sheet = genre == Genre::Workbook ? g.name : std::string{};
    a.rows = n;
    a.label = "Importer les traductions (" + plural(n, "texte", "textes") + ")" + sheetSuffix(g, genre);
    a.detail = "Colonne \xC2\xAB " + trim(g.columns[textCol]) + " \xC2\xBB"
             + (languages.empty() ? std::string(" ; aucune colonne de langue")
                                  : ", " + std::string(languages.size() > 1 ? "langues : " : "langue : ") + join(languages))
             + ".\nComme IHM > Configuration > Langues > Importer : une langue nouvelle s'ajoute, une case vide retire "
               "la traduction ; un Ctrl+Z.";
    if (!ctx.hmi) {
        a.possible = false;
        a.why = "le projet n'a pas d'IHM";
    } else if (lower(code) != lower(ctx.hmi->languages.source())) {
        a.possible = false;
        a.why = "l'IHM part du \xC2\xAB " + ctx.hmi->languages.source() + " \xC2\xBB, ce classeur du \xC2\xAB " + code + " \xC2\xBB";
    } else if (languages.empty()) {
        a.possible = false;
        a.why = "aucune colonne de langue (English (en)...) \xC3\xA0 c\xC3\xB4t\xC3\xA9 des textes";
    }
    a.checked = a.possible;
    return a;
}

std::optional<Action> recipeAction(const Grid& g, Genre genre, const Context& ctx, bool single) {
    if (!ctx.hmi || ctx.hmi->recipes.empty() || g.columns.size() < 2) return std::nullopt;
    struct Match {
        const hmi::Recipe*       recipe;
        std::vector<std::string> columns;
        std::size_t              total;
    };
    std::vector<Match> matches;
    for (const auto& r : ctx.hmi->recipes) {
        Match m{&r, {}, 0};
        for (std::size_t c = 1; c < g.columns.size(); ++c) {
            if (trim(g.columns[c]).empty()) continue;
            ++m.total;
            const auto key = recipeKey(g.columns[c]);
            for (const auto& f : r.fields)
                if (recipeKey(f.name) == key) {
                    m.columns.push_back(trim(g.columns[c]));
                    break;
                }
        }
        if (!m.columns.empty() && m.columns.size() * 2 >= m.total) matches.push_back(std::move(m));
    }
    if (matches.empty()) return std::nullopt;
    std::stable_sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) { return a.columns.size() > b.columns.size(); });
    const auto& best = matches.front();
    std::size_t n = 0, existing = 0;
    std::vector<std::string> names;
    for (std::size_t r = 0; r < g.rows.size(); ++r) {
        const auto name = trim(g.cell(r, 0));
        if (name.empty()) continue;
        ++n;
        if (names.size() < 4) names.push_back(name);
        if (best.recipe->record(name)) ++existing;
    }
    if (n == 0) return std::nullopt;
    Action a;
    a.kind = ActionKind::Recipe;
    a.sheet = genre == Genre::Workbook ? g.name : std::string{};
    a.rows = n;
    a.label = "Importer " + plural(n, "jeu", "jeux") + " dans une recette" + sheetSuffix(g, genre);
    a.detail = "Colonne \xC2\xAB " + trim(g.columns[0]) + " \xC2\xBB : les jeux (" + joinSome(names, 3) + ") ; reconnues pour \xC2\xAB "
             + best.recipe->name + " \xC2\xBB : " + join(best.columns) + " (" + std::to_string(best.columns.size()) + " sur "
             + std::to_string(best.total) + ")"
             + (existing > 0 ? " ; " + plural(existing, "jeu du m\xC3\xAAme nom remplac\xC3\xA9", "jeux du m\xC3\xAAme nom remplac\xC3\xA9s") : std::string{})
             + ".\nComme IHM > Recettes > Importer (CSV) : un Ctrl+Z.";
    for (const auto& m : matches)
        a.targets.push_back({"Recette \xC2\xAB " + m.recipe->name + " \xC2\xBB (" + std::to_string(m.columns.size()) + " colonne"
                                 + (m.columns.size() > 1 ? "s" : "") + " sur " + std::to_string(m.total) + ")",
                             std::to_string(m.recipe->id)});
    a.checked = single && best.columns.size() == best.total;
    return a;
}

std::optional<Action> animationAction(const Grid& g, Genre genre, const Context& ctx, const std::set<std::string>& globals) {
    if (!ctx.plc) return std::nullopt;
    static const std::set<std::string> kTitles = {"variable", "nom", "name", "mnemonique", "symbole", "chemin", "tag", "identifiant"};
    std::size_t bestCol = npos, bestKnown = 0, bestFilled = 0;
    std::vector<std::string> bestItems;
    std::vector<bool> bestHmi;
    for (std::size_t c = 0; c < g.columns.size(); ++c) {
        if (!kTitles.count(titleKey(g.columns[c]))) continue;
        std::size_t filled = 0, known = 0;
        std::vector<std::string> items;
        std::vector<bool> isHmi;
        std::set<std::string> seen;
        for (std::size_t r = 0; r < g.rows.size() && r < 5000; ++r) {
            const auto v = trim(g.cell(r, c));
            if (v.empty()) continue;
            ++filled;
            std::size_t end = 0;
            while (end < v.size() && v[end] != '.' && v[end] != '[') ++end;
            const auto root = lower(v.substr(0, end));
            bool plc = false, hmiVar = false;
            if (globals.count(root)) plc = end == v.size() || project::mast::pathExists(*ctx.plc, v);
            else if (ctx.hmi && ctx.hmi->variable(v)) hmiVar = true;
            if (!plc && !hmiVar) continue;
            ++known;
            if (seen.insert(lower(v)).second) {
                items.push_back(v);
                isHmi.push_back(hmiVar);
            }
        }
        if (known > bestKnown) {
            bestCol = c;
            bestKnown = known;
            bestFilled = filled;
            bestItems = std::move(items);
            bestHmi = std::move(isHmi);
        }
    }
    if (bestCol == npos || bestKnown == 0 || bestKnown * 2 < bestFilled) return std::nullopt;
    Action a;
    a.kind = ActionKind::AnimationTable;
    a.sheet = genre == Genre::Workbook ? g.name : std::string{};
    a.rows = bestItems.size();
    a.label = "Ajouter " + plural(bestItems.size(), "ligne", "lignes") + " \xC3\xA0 une table d'animation" + sheetSuffix(g, genre);
    std::vector<std::string> some(bestItems.begin(), bestItems.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(3, bestItems.size())));
    a.detail = "Colonne \xC2\xAB " + trim(g.columns[bestCol]) + " \xC2\xBB : " + plural(bestKnown, "variable reconnue", "variables reconnues")
             + " sur " + std::to_string(bestFilled) + " (" + join(some) + (bestItems.size() > 3 ? ", \xE2\x80\xA6" : "") + ")"
             + (bestKnown < bestFilled ? " ; les autres sont laiss\xC3\xA9" "es" : "")
             + ".\nUne commande (Ctrl+Z la retire) ; la table s'ouvre dans API > Tables d'animation.";
    const std::string base = tableName(genre == Genre::Workbook ? g.name : stemOf(g.name));
    const std::string fresh = project::freeAnimationTableName(*ctx.plc, base);
    a.targets.push_back({"Nouvelle table \xC2\xAB " + fresh + " \xC2\xBB", "+" + fresh});
    for (std::size_t i = 0; i < ctx.plc->animationTables.size(); ++i) {
        const auto& t = ctx.plc->animationTables[i];
        a.targets.push_back({"Table \xC2\xAB " + std::string(ctx.plc->strings.text(t.name)) + " \xC2\xBB (" + plural(t.entries.size(), "ligne", "lignes") + ")",
                             std::to_string(i)});
    }
    a.items = std::move(bestItems);
    a.itemHmi = std::move(bestHmi);
    return a;
}

std::optional<hmi::ExternalKind> externalKindOf(const std::string& path) {
    if (extOf(path) == ".tsv") return hmi::ExternalKind::Csv;       // le lecteur CSV reconnait la tabulation
    return hmi::externalKindFromPath(path);
}

// ---- Lot API 8 : glisser de fichiers, 2e partie ---- tout fichier se lie (un
// document, s'il n'est pas un classeur, un CSV, un texte...) : il ne reste que le chemin vide.
const char* const kExternalKinds = "l'IHM ne sait pas lier ce chemin";
// ---- fin Lot API 8 : glisser de fichiers, 2e partie ----

// Le lien existant vers ce fichier ; ou, a defaut, un lien du meme nom.
const hmi::ExternalFile* linkedFile(const Context& ctx, const std::string& path, bool& same) {
    same = false;
    if (!ctx.hmi) return nullptr;
    std::error_code ec;
    const auto want = fs::weakly_canonical(fs::path(path), ec);
    const hmi::ExternalFile* byName = nullptr;
    for (const auto& f : ctx.hmi->assets.files) {
        if (f.kind == hmi::ExternalKind::Database) continue;
        std::error_code ec2;
        const auto have = fs::weakly_canonical(fs::path(hmi::resolveExternalPath(f, ctx.projectFolder)), ec2);
        if (!ec && !ec2 && have == want) {
            same = true;
            return &f;
        }
        if (!byName && lower(f.name) == lower(stemOf(path))) byName = &f;
    }
    return byName;
}

Action externalFileAction(const std::string& path, Genre genre, const Context& ctx) {
    Action a;
    a.kind = ActionKind::ExternalFile;
    a.label = "Ajouter aux Fichiers externes de l'IHM";
    std::string where = "hors du dossier du projet : chemin absolu";
    if (!ctx.projectFolder.empty()) {
        std::error_code ec;
        const auto rel = fs::weakly_canonical(fs::path(path), ec).lexically_relative(fs::weakly_canonical(fs::path(ctx.projectFolder), ec));
        if (!ec && !rel.empty() && *rel.begin() != fs::path("..")) where = "dans le dossier du projet : chemin relatif";
    }
    a.detail = std::string("Un lien vers le fichier, qui reste o\xC3\xB9 il est (") + where + ") ; un Tableau de l'IHM peut en montrer "
             + (genre == Genre::Workbook ? "un onglet" : "le contenu") + ". Comme IHM > Fichiers externes > Lier.";
    bool same = false;
    const auto* existing = linkedFile(ctx, path, same);
    if (!ctx.hmi) {
        a.possible = false;
        a.why = "le projet n'a pas d'IHM";
    } else if (!externalKindOf(path)) {
        a.possible = false;
        a.why = kExternalKinds;
    } else if (existing && same) {
        a.possible = false;
        a.why = "d\xC3\xA9j\xC3\xA0 li\xC3\xA9 : \xC2\xAB " + existing->name + " \xC2\xBB";
    } else if (existing) {
        a.detail += "\nUn lien \xC2\xAB " + existing->name + " \xC2\xBB vise un autre fichier : celui-ci prendra un autre nom.";
    }
    return a;
}

Action resourceAction(Genre genre) {
    Action a;
    a.kind = ActionKind::Resource;
    a.label = "Ajouter aux Ressources de l'IHM";
    a.possible = false;
    // Lot API 8 : l'IHM garde aussi des documents, mais un classeur se lie (un Tableau en lit les onglets).
    a.why = std::string(genre == Genre::Workbook ? "un classeur" : "un tableau CSV")
          + " se lie (Fichiers externes) : un Tableau de l'IHM en lit le contenu, une copie dans les Ressources ne se lirait pas";
    return a;
}

Action macroWorkbookAction(const WorkbookPlan& w, const Context& ctx) {
    Action a;
    a.kind = ActionKind::MacroWorkbook;
    a.label = "En faire le classeur des macros";
    a.detail = "Un champ Classeur laiss\xC3\xA9 vide le lira (aujourd'hui : "
             + (ctx.macroWorkbook.empty() ? std::string("aucun") : leafOf(ctx.macroWorkbook))
             + "). Lancer une macro sur ce classeur le fait aussi.";
    std::error_code ec, ec2;
    const bool current = !ctx.macroWorkbook.empty()
                      && fs::weakly_canonical(fs::path(ctx.macroWorkbook), ec) == fs::weakly_canonical(fs::path(w.path), ec2) && !ec && !ec2;
    if (w.genre == Genre::Table) {
        a.possible = false;
        a.why = "les macros ouvrent un .xlsx ou un .xlsm (OpenWorkbook), pas un CSV";
    } else if (!w.readable) {
        a.possible = false;
        a.why = "le classeur ne se lit pas";
    } else if (current) {
        a.possible = false;
        a.why = "c'est d\xC3\xA9j\xC3\xA0 le classeur des macros";
    }
    return a;
}

// La ligne d'un onglet que lit une macro : "onglet ES : 80 lignes ;
// colonnes Adresse, Tableau, Index...".
std::string sheetLine(const Grid& g, const std::vector<std::string>& macroColumns) {
    std::vector<std::string> read;
    for (const auto& c : g.columns) {
        if (trim(c).empty()) continue;
        const auto key = project::normaliseKey(trim(c));
        for (const auto& m : macroColumns)
            if (project::normaliseKey(m) == key) {
                read.push_back(trim(c));
                break;
            }
    }
    // Config est un formulaire (une cle, sa valeur a droite) : ses cles.
    if (g.name == "Config") {
        const auto& keys = g.settings;
        return "onglet \xC2\xAB Config \xC2\xBB : les r\xC3\xA9glages" + (keys.empty() ? std::string{} : " (" + joinSome(keys, 5) + ")");
    }
    if (read.empty())
        for (const auto& c : g.columns)
            if (!trim(c).empty()) read.push_back(trim(c));
    return "onglet \xC2\xAB " + g.name + " \xC2\xBB : " + plural(g.rows.size(), "ligne", "lignes")
         + (read.empty() ? std::string{} : " ; colonnes " + joinSome(read, 5));
}

std::vector<Action> workbookMacros(const std::vector<LibMacro>& all, const std::vector<Grid>& grids) {
    std::vector<Action> out;
    const auto gridOf = [&](const std::string& sheet) -> const Grid* {
        for (const auto& g : grids)
            if (g.name == sheet) return &g;         // exact, espaces compris (comme OpenSheet)
        return nullptr;
    };
    for (const auto& m : all) {
        if (!m.spec.touches.workbook) continue;
        std::vector<std::string> required, optional, columns;
        readsOf(all, m, required, optional, columns);
        std::vector<std::string> present, missing, optionalPresent;
        for (const auto& r : required) (gridOf(r) ? present : missing).push_back(r);
        for (const auto& r : optional)
            if (gridOf(r)) optionalPresent.push_back(r);
        // Rien en commun avec ce classeur : ce n'est pas le sien, pas de ligne.
        if (present.empty() && optionalPresent.empty()) continue;
        Action a;
        a.kind = ActionKind::Macro;
        a.macro = m.name;
        a.field = workbookField(all, m);
        if (a.field.empty()) a.field = "classeur";
        a.score = static_cast<int>(present.size() + optionalPresent.size());
        const auto& shown = present.empty() ? optionalPresent : present;
        a.label = "Lancer " + m.name + " \xE2\x80\x94 " + (shown.size() > 1 ? "onglets " : "onglet ") + joinSome(shown, 4)
                + (shown.size() > 1 ? " reconnus" : " reconnu");
        std::string d;
        if (!m.spec.summary.empty()) d += m.spec.summary + "\n";
        std::size_t lines = 0;
        for (const auto& s : present) {
            if (lines++ == 3) {
                d += "\xE2\x80\xA6\n";
                break;
            }
            d += sheetLine(*gridOf(s), columns) + "\n";
        }
        if (!optionalPresent.empty() && !present.empty()) d += "Facultatifs pr\xC3\xA9sents : " + joinSome(optionalPresent, 6) + ".\n";
        if (!m.spec.launches.empty()) d += "Encha\xC3\xAEne : " + joinSome(m.spec.launches, 6) + ".\n";
        while (!d.empty() && d.back() == '\n') d.pop_back();
        a.detail = std::move(d);
        if (!missing.empty()) {
            a.possible = false;
            a.why = std::string(missing.size() > 1 ? "il manque les onglets " : "il manque l'onglet ") + join(missing);
        }
        out.push_back(std::move(a));
    }
    return out;
}

// Un CSV : les macros qui lisent un tableau ("#! tableau", OpenTable) et dont
// les colonnes sont la (au moins deux, et la moitie de celles qu'elles lisent).
std::vector<Action> tableMacros(const std::vector<LibMacro>& all, const Grid& g) {
    std::vector<Action> out;
    std::set<std::string> have;
    for (const auto& c : g.columns)
        if (!trim(c).empty()) have.insert(project::normaliseKey(trim(c)));
    for (const auto& m : all) {
        if (m.spec.tables.empty() || m.spec.touches.workbook || m.columns.size() < 2) continue;
        std::vector<std::string> found;
        for (const auto& c : m.columns)
            if (have.count(project::normaliseKey(c))) found.push_back(c);
        if (found.size() < 2 || found.size() * 2 < m.columns.size()) continue;
        Action a;
        a.kind = ActionKind::Macro;
        a.macro = m.name;
        a.field = "tableau:" + m.spec.tables.front().name;
        a.score = static_cast<int>(found.size());
        a.label = "Lancer " + m.name + " \xE2\x80\x94 ce fichier comme tableau \xC2\xAB " + m.spec.tables.front().name + " \xC2\xBB";
        a.detail = (m.spec.summary.empty() ? std::string{} : m.spec.summary + "\n") + "Colonnes reconnues : " + joinSome(found, 8) + " ("
                 + std::to_string(found.size()) + " sur " + std::to_string(m.columns.size()) + " lues par la macro).";
        out.push_back(std::move(a));
    }
    return out;
}

std::set<std::string> globalNames(const domain::Project* p) {
    std::set<std::string> out;
    if (!p) return out;
    for (const auto& v : p->variables)
        if (v.scope == domain::VariableScope::Global) out.insert(lower(std::string(p->strings.text(v.name))));
    return out;
}

// Ecrit une case pour Excel (tabulations) ou un CSV (';').
std::string quoted(const std::string& cell, char sep) {
    if (cell.find_first_of(std::string(1, sep) + "\"\r\n") == std::string::npos) return cell;
    std::string out = "\"";
    for (const char c : cell) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    return out + "\"";
}

std::string delimited(const SheetData& d, char sep) {
    std::string out;
    const auto line = [&](const std::vector<std::string>& cells, std::size_t n) {
        for (std::size_t c = 0; c < n; ++c) {
            if (c) out += sep;
            out += quoted(c < cells.size() ? cells[c] : std::string{}, sep);
        }
        out += "\r\n";
    };
    line(d.columns, d.columns.size());
    for (const auto& r : d.rows) line(r, d.columns.size());
    return out;
}

} // namespace

// =================================================================== le genre ==
Genre genreOf(const std::string& path) {
    std::error_code ec;
    if (fs::is_directory(path, ec)) return Genre::Folder;
    if (!fs::is_regular_file(path, ec)) return Genre::Missing;
    const std::string ext = extOf(path);
    // Un classeur ou une image ne se lisent pas comme un export : pas la peine
    // de les ouvrir pour le savoir. Le reste (un .XPG, un .XML renomme...) par
    // son contenu.
    const bool obvious = ext == ".xlsx" || ext == ".xlsm" || ext == ".xls" || ext == ".csv" || ext == ".tsv" || ext == ".pdf"
                      || !hmi::formatFromExtension(leafOf(path)).empty();
    if (!obvious) {
        const int k = project::mast::sniffFile(path);
        if (k == 1) return Genre::Program;
        if (k == 2) return Genre::Hardware;
        if (k < 0) return Genre::Missing;
    }
    if (ext == ".xlsx" || ext == ".xlsm" || ext == ".xls") return Genre::Workbook;
    if (ext == ".csv" || ext == ".tsv") return Genre::Table;
    if (const auto format = hmi::formatFromExtension(leafOf(path)); !format.empty()) {
        switch (hmi::kindOfFormat(format)) {
            case hmi::MediaKind::Image: return Genre::Image;
            case hmi::MediaKind::Sound: return Genre::Sound;
            case hmi::MediaKind::Video: return Genre::Video;
            case hmi::MediaKind::Font:  return Genre::Font;
            case hmi::MediaKind::Document:                   // lot API 8 (formatFromExtension n'en rend pas)
            case hmi::MediaKind::Unknown: break;
        }
    }
    if (ext == ".pdf") return Genre::Pdf;
    if (ext == ".txt" || ext == ".log") return Genre::Text;
    if (ext == ".json") return Genre::Json;
    if (ext == ".xml") return Genre::Xml;
    if (ext == ".db" || ext == ".sqlite" || ext == ".sqlite3") return Genre::Database;
    return Genre::Other;
}

std::string genreLabel(Genre g) {
    switch (g) {
        case Genre::Program:  return "Programme Control Expert (.XPG)";
        case Genre::Hardware: return "Configuration mat\xC3\xA9rielle (.XHW)";
        case Genre::Workbook: return "Classeur Excel";
        case Genre::Table:    return "Tableau CSV";
        case Genre::Image:    return "Image";
        case Genre::Sound:    return "Son";
        case Genre::Video:    return "Vid\xC3\xA9o";
        case Genre::Font:     return "Police";
        case Genre::Pdf:      return "Document PDF";
        case Genre::Text:     return "Texte";
        case Genre::Json:     return "Donn\xC3\xA9" "es JSON";
        case Genre::Xml:      return "Donn\xC3\xA9" "es XML";
        case Genre::Database: return "Base SQLite";
        case Genre::Folder:   return "Dossier";
        case Genre::Other:    return "Fichier";
        case Genre::Missing:  return "Introuvable";
    }
    return "Fichier";
}

Stage stageOf(ActionKind k) noexcept {
    switch (k) {
        case ActionKind::HmiVariables:
        case ActionKind::Translations:
        case ActionKind::Recipe:
        case ActionKind::AnimationTable: return Stage::Import;
        case ActionKind::ExternalFile:
        case ActionKind::Resource:       return Stage::Add;
        case ActionKind::MacroWorkbook:
        case ActionKind::Macro:          return Stage::Macros;
    }
    return Stage::Import;
}

std::string stageTitle(Stage s) {
    switch (s) {
        case Stage::Import: return "Importer maintenant \xE2\x80\x94 un Ctrl+Z chacun";
        case Stage::Add:    return "Ajouter au projet";
        case Stage::Macros: return "Les macros \xE2\x80\x94 leur formulaire, jusqu'\xC3\xA0 l'aper\xC3\xA7u : rien n'est appliqu\xC3\xA9 sans toi";
    }
    return {};
}

std::string conflictLabel(Conflict c) {
    switch (c) {
        case Conflict::Replace:  return "Remplacer";
        case Conflict::KeepBoth: return "Garder les deux";
        case Conflict::Ignore:   return "Ignorer";
    }
    return {};
}

// ================================================================= un classeur ==
std::string WorkbookPlan::summary() const {
    std::string s = genreLabel(genre);
    if (genre == Genre::Workbook && extOf(path) == ".xlsm") s += " avec macros";
    s += ", " + hmi::formatBytes(bytes);
    if (!readable) return s + " \xE2\x80\x94 illisible : " + error;
    if (genre == Genre::Workbook) {
        std::vector<std::string> names;
        for (const auto& sh : sheets) names.push_back(sh.name);
        s += " \xE2\x80\x94 " + plural(sheets.size(), "onglet", "onglets") + " : " + joinSome(names, 6);
    } else if (!sheets.empty()) {
        s += " \xE2\x80\x94 " + plural(sheets.front().rows, "ligne", "lignes") + ", " + plural(sheets.front().columns.size(), "colonne", "colonnes");
    }
    if (vba) s += " ; les macros VBA du classeur sont ignor\xC3\xA9" "es";
    return s;
}

WorkbookPlan analyseWorkbook(const std::string& path, const Context& ctx) {
    WorkbookPlan w;
    w.path = path;
    w.name = leafOf(path);
    w.genre = extOf(path) == ".csv" || extOf(path) == ".tsv" ? Genre::Table : Genre::Workbook;
    std::error_code ec;
    w.bytes = static_cast<std::uint64_t>(fs::file_size(path, ec));
    if (ec) w.bytes = 0;
    std::vector<Grid> grids;
    w.readable = readGrids(path, w.genre, grids, w.error, &w.vba);
    for (const auto& g : grids) w.sheets.push_back(SheetInfo{g.name, g.rows.size(), g.columns});

    std::vector<Action> imports, adds, macros;
    if (w.readable) {
        const bool single = grids.size() == 1;
        const auto globals = globalNames(ctx.plc);
        for (const auto& g : grids) {
            if (g.columns.empty() || g.rows.empty()) continue;
            if (auto a = hmiVariablesAction(g, w.genre, ctx, single)) imports.push_back(std::move(*a));
            if (auto a = translationsAction(g, w.genre, ctx)) imports.push_back(std::move(*a));
            if (auto a = recipeAction(g, w.genre, ctx, single)) imports.push_back(std::move(*a));
            if (auto a = animationAction(g, w.genre, ctx, globals)) imports.push_back(std::move(*a));
        }
    }
    adds.push_back(externalFileAction(path, w.genre, ctx));
    adds.push_back(resourceAction(w.genre));

    if (w.readable) {
        const auto all = libraryMacros(ctx.libsRoot);
        std::vector<Action> found = w.genre == Genre::Workbook ? workbookMacros(all, grids)
                                  : grids.empty() ? std::vector<Action>{} : tableMacros(all, grids.front());
        // Les possibles d'abord, la plus complete en tete ; puis les grisees.
        std::stable_sort(found.begin(), found.end(), [](const Action& a, const Action& b) {
            if (a.possible != b.possible) return a.possible;
            return a.score > b.score;
        });
        // LA PLUS COMPLETE EST COCHEE : celle qui lit le plus d'onglets de ce
        // classeur (ImporterClasseur pour celui de l'affaire). Elle s'arrete a
        // l'apercu : la cocher ne modifie rien.
        if (!found.empty() && found.front().possible) found.front().checked = true;
        if (w.genre == Genre::Workbook) {
            auto book = macroWorkbookAction(w, ctx);
            book.checked = book.possible && !found.empty() && found.front().checked;
            macros.push_back(std::move(book));
        }
        for (auto& a : found) macros.push_back(std::move(a));
    } else if (w.genre == Genre::Workbook) {
        macros.push_back(macroWorkbookAction(w, ctx));
    }

    for (auto* list : {&imports, &adds, &macros})
        for (auto& a : *list) w.actions.push_back(std::move(a));
    // Rien d'autre a faire : le lien, au moins (ce que l'on fait d'un fichier
    // qu'on ne sait pas lire autrement).
    const bool any = std::any_of(w.actions.begin(), w.actions.end(), [](const Action& a) { return a.checked && a.possible; });
    if (!any)
        for (auto& a : w.actions)
            if (a.kind == ActionKind::ExternalFile && a.possible) a.checked = true;
    return w;
}

// ============================================================== un autre fichier ==
FileRow analyseFile(const std::string& path, const Context& ctx) {
    FileRow r;
    r.path = path;
    r.name = leafOf(path);
    r.genre = genreOf(path);
    std::error_code ec;
    r.bytes = static_cast<std::uint64_t>(fs::file_size(path, ec));
    if (ec) r.bytes = 0;
    r.kind = genreLabel(r.genre);

    const bool media = r.genre == Genre::Image || r.genre == Genre::Sound || r.genre == Genre::Video || r.genre == Genre::Font;
    if (media) {
        r.kind += " " + hmi::formatFromExtension(r.name);
        // Une image se relit pour sa vignette ; un son, une video, une police
        // pour leur duree, leur format - pas au-dela de 16 Mo (lus a l'ajout).
        const std::uint64_t readNow = r.genre == Genre::Image ? kPreviewBytes : kPreviewBytes / 4;
        if (r.bytes > kResourceBytes) {
            r.resourceWhy = "trop lourd pour \xC3\xAAtre embarqu\xC3\xA9 (" + hmi::formatBytes(r.bytes) + ", 256 Mo au plus)";
        } else if (r.bytes <= readNow) {
            hmi::Project scratch;
            if (auto res = hmi::readResource(scratch, path)) {
                r.resourceOk = true;
                r.kind = std::string(hmi::mediaKindLabel(res->kind())) + " " + res->format;
                if (res->width > 0 && res->height > 0) r.kind += ", " + std::to_string(res->width) + " \xC3\x97 " + std::to_string(res->height);
                if (res->seconds > 0) r.kind += ", " + hmi::formatDuration(res->seconds);
                if (res->kind() == hmi::MediaKind::Image) {
                    r.preview = std::move(*res);
                    r.hasPreview = true;
                }
            } else {
                r.resourceWhy = res.error().context.empty() ? res.error().message() : res.error().context;
            }
        } else {
            r.resourceOk = true;          // lu a l'ajout
        }
    } else if (r.bytes > kResourceBytes) {
        // ---- Lot API 8 : glisser de fichiers, 2e partie ---- un document (PDF,
        // DOCX, ZIP, texte...) est une ressource aussi, gardee telle quelle.
        r.resourceWhy = "trop lourd pour \xC3\xAAtre embarqu\xC3\xA9 (" + hmi::formatBytes(r.bytes) + ", 256 Mo au plus)";
    } else {
        r.resourceOk = true;
    }
    if (r.genre == Genre::Other) {
        auto ext = fs::path(r.name).extension().string();
        for (auto& c : ext) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (!ext.empty()) r.kind += " " + ext;
        // Un paquet de vues (lot 20) a son chemin a lui.
        if (extOf(path) == ".xpgvues") {
            r.kind = "Paquet de vues IHM (.xpgvues)";
            r.resourceOk = false;                                  // lot API 8 : pas un document
            r.resourceWhy = "un paquet de vues s'importe par IHM > Vues > Importer des vues";
        }
        // 1.11.2 (decisions 162, 174) : les symboles, types IHM, fonctions IHM, scripts generaux aussi.
        if (const auto e = extOf(path); e == ".xpgsymboles" || e == ".xpgtypes" || e == ".xpgfonctions" || e == ".xpgscripts") {
            r.kind = "Paquet IHM (" + e + ")";
            r.resourceOk = false;
            r.resourceWhy = "un paquet s'importe par Importer\xE2\x80\xA6 (IHM > Symboles, Types IHM, Fonctions ou Scripts g\xC3\xA9n\xC3\xA9raux)";
        }
    }
    r.fileOk = externalKindOf(path).has_value();
    if (!r.fileOk) r.fileWhy = kExternalKinds;
    if (const auto e = extOf(path);
        e == ".xpgvues" || e == ".xpgsymboles" || e == ".xpgtypes" || e == ".xpgfonctions" || e == ".xpgscripts") {   // lot API 8 : ni lie (1.11.2 : tout paquet)
        r.fileOk = false;
        r.fileWhy = r.resourceWhy;
    }
    if (!ctx.hmi) {
        r.resourceOk = r.fileOk = false;
        r.resourceWhy = r.fileWhy = "le projet n'a pas d'IHM";
    }
    // Par defaut, la case qui convient au genre : un media en ressource, le
    // reste (un document, des donnees) en fichier externe - comme la maquette.
    r.resource = r.resourceOk && media;
    r.file = r.fileOk && !r.resource;
    // ---- fin Lot API 8 : glisser de fichiers, 2e partie ----

    if (ctx.hmi) {
        for (const auto& res : ctx.hmi->assets.resources)
            if (lower(res.name) == lower(r.name)) {
                const auto uses = hmi::citations(*ctx.hmi, res.name).size();
                r.resourceExisting = res.name + (uses > 0 ? " (cit\xC3\xA9" "e " + std::to_string(uses) + " fois)" : std::string{});
                r.resourceId = res.id;
                r.identical = r.hasPreview && res.data && r.preview.data && *res.data == *r.preview.data;
                if (!r.identical && res.data && r.bytes == res.data->size() && r.bytes <= kPreviewBytes && !r.hasPreview) {
                    const std::string bytes = readAll(path);
                    r.identical = bytes.size() == res.data->size() && std::equal(bytes.begin(), bytes.end(), res.data->begin(),
                        [](char a, std::uint8_t b) { return static_cast<std::uint8_t>(a) == b; });
                }
                break;
            }
        bool same = false;
        if (const auto* f = linkedFile(ctx, path, same)) {
            r.fileExisting = f->name + (same ? " (ce fichier)" : " (un autre fichier)");
            r.fileId = f->id;
            r.sameFile = same;
        }
    }
    // Deja la. Une ressource du meme nom : la remplacer (une nouvelle version
    // du logo ; Ctrl+Z rend l'ancienne), sauf si c'est la meme. Un lien du meme
    // nom vers UN AUTRE fichier : garder les deux (l'autre a peut-etre sa
    // raison) ; vers ce fichier : rien a faire.
    if (!r.resourceExisting.empty()) r.conflict = r.identical ? Conflict::Ignore : Conflict::Replace;
    else if (!r.fileExisting.empty()) r.conflict = r.sameFile ? Conflict::Ignore : Conflict::KeepBoth;
    return r;
}

// ================================================================== le depot ==
std::size_t Plan::count() const {
    std::size_t n = 0;
    if (!programs.empty() || !hardware.empty())
        if (programChoice >= 0 && static_cast<std::size_t>(programChoice) < programOptions.size()
            && programOptions[static_cast<std::size_t>(programChoice)].code >= 0)
            ++n;
    for (const auto& w : workbooks)
        for (const auto& a : w.actions)
            if (a.checked && a.possible) ++n;
    for (const auto& f : files) {
        // Une case cochee compte, sauf si ce qu'elle vise est deja la et qu'on l'ignore.
        const bool resource = f.resource && f.resourceOk && (f.resourceExisting.empty() || f.conflict != Conflict::Ignore);
        const bool file = f.file && f.fileOk && (f.fileExisting.empty() || f.conflict != Conflict::Ignore);
        if (resource || file) ++n;
    }
    return n;
}

Plan analyse(const std::vector<std::string>& paths, const Context& ctx) {
    Plan p;
    std::set<std::string> seen;
    for (const auto& path : paths) {
        if (path.empty() || !seen.insert(path).second) continue;
        switch (genreOf(path)) {
            case Genre::Program:
                if (p.programs.empty()) p.programs.push_back(path);
                else p.ignored.push_back(leafOf(path) + " (un seul .XPG \xC3\xA0 la fois)");
                break;
            case Genre::Hardware:
                if (p.hardware.empty()) p.hardware.push_back(path);
                else p.ignored.push_back(leafOf(path) + " (un seul .XHW \xC3\xA0 la fois)");
                break;
            case Genre::Workbook:
            case Genre::Table:
                p.workbooks.push_back(analyseWorkbook(path, ctx));
                break;
            case Genre::Folder:
                p.ignored.push_back(leafOf(path) + " (un dossier : glisse les fichiers qu'il contient)");
                break;
            case Genre::Missing:
                p.ignored.push_back(leafOf(path) + " (introuvable)");
                break;
            default:
                p.files.push_back(analyseFile(path, ctx));
                break;
        }
    }
    return p;
}

// ======================================================= relire, ecrire un onglet ==
bool readSheet(const std::string& path, const std::string& sheet, SheetData& out, std::string* why) {
    out = {};
    const Genre genre = extOf(path) == ".csv" || extOf(path) == ".tsv" ? Genre::Table : Genre::Workbook;
    std::vector<Grid> grids;
    std::string error;
    if (!readGrids(path, genre, grids, error)) {
        if (why) *why = error;
        return false;
    }
    for (auto& g : grids)
        if (sheet.empty() || g.name == sheet) {
            out.columns = std::move(g.columns);
            out.rows = std::move(g.rows);
            return true;
        }
    if (why) *why = "onglet \xC2\xAB " + sheet + " \xC2\xBB introuvable";
    return false;
}

std::string tabText(const SheetData& d) { return delimited(d, '\t'); }
std::string csvText(const SheetData& d) { return delimited(d, ';'); }

// ============================================================ les morceaux ==
std::vector<std::string> cellColumns(std::string_view src) {
    std::vector<std::string> out;
    // Le code sans ses commentaires, chaines gardees : (* ... *) et // ...
    std::string code;
    code.reserve(src.size());
    for (std::size_t i = 0; i < src.size();) {
        if (src.compare(i, 2, "(*") == 0) {
            const auto end = src.find("*)", i + 2);
            i = end == std::string_view::npos ? src.size() : end + 2;
            code += ' ';
            continue;
        }
        if (src.compare(i, 2, "//") == 0) {
            const auto end = src.find('\n', i);
            i = end == std::string_view::npos ? src.size() : end;
            continue;
        }
        if (src[i] == '\'' || src[i] == '"') {
            const char q = src[i];
            std::size_t j = i + 1;
            while (j < src.size() && src[j] != q) j += src[j] == '$' ? 2 : 1;
            j = std::min(j + 1, src.size());
            code.append(src.substr(i, j - i));
            i = j;
            continue;
        }
        code += src[i++];
    }
    const auto isWord = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (!isWord(code[i]) || (i > 0 && isWord(code[i - 1]))) continue;
        std::size_t j = i;
        while (j < code.size() && isWord(code[j])) ++j;
        const std::string word = lower(code.substr(i, j - i));
        const std::size_t argIndex = word == "cell" ? 2 : word == "hascolumn" ? 1 : npos;
        std::size_t k = j;
        while (k < code.size() && std::isspace(static_cast<unsigned char>(code[k]))) ++k;
        if (argIndex == npos || k >= code.size() || code[k] != '(') {
            i = j - 1;
            continue;
        }
        // Les arguments, au premier niveau de parentheses.
        std::vector<std::string> args(1);
        int depth = 0;
        std::size_t p = k + 1;
        for (; p < code.size(); ++p) {
            const char c = code[p];
            if (c == '\'' || c == '"') {
                std::size_t e = p + 1;
                while (e < code.size() && code[e] != c) e += code[e] == '$' ? 2 : 1;
                args.back().append(code.substr(p, std::min(e + 1, code.size()) - p));
                p = std::min(e, code.size() - 1);
                continue;
            }
            if (c == '(') ++depth;
            if (c == ')') {
                if (depth == 0) break;
                --depth;
            }
            if (c == ',' && depth == 0) {
                args.emplace_back();
                continue;
            }
            args.back() += c;
        }
        if (argIndex < args.size()) {
            const auto a = trim(args[argIndex]);
            if (a.size() >= 2 && a.front() == '\'' && a.back() == '\'') {
                std::string text;
                bool literal = true;
                for (std::size_t q = 1; q + 1 < a.size(); ++q) {
                    if (a[q] == '$' && q + 2 < a.size()) {
                        text += a[q + 1] == '\'' ? '\'' : a[q + 1] == '$' ? '$' : a[q + 1];
                        ++q;
                        continue;
                    }
                    if (a[q] == '\'') literal = false;      // 'S' + i + '_En' : calculee
                    text += a[q];
                }
                if (literal && !text.empty() && std::find(out.begin(), out.end(), text) == out.end()) out.push_back(text);
            }
        }
        i = p;
    }
    return out;
}

std::string fold(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 0xC3 && i + 1 < text.size())
            if (const char b = baseLetter(static_cast<unsigned char>(text[i + 1]))) {
                out += b;
                ++i;
                continue;
            }
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

bool startsFolded(std::string_view text, std::string_view prefix) {
    const auto t = fold(text), p = fold(trim(prefix));
    return !p.empty() && t.rfind(p, 0) == 0;
}

std::string tableName(std::string_view base) {
    std::string out;
    for (std::size_t i = 0; i < base.size(); ++i) {
        const auto c = static_cast<unsigned char>(base[i]);
        char keep = 0;
        if (std::isalnum(c) && c < 0x80) keep = static_cast<char>(c);
        else if (c == 0xC3 && i + 1 < base.size()) {
            keep = baseLetter(static_cast<unsigned char>(base[i + 1]));
            ++i;
        }
        if (keep) out += keep;
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty() || !std::isalpha(static_cast<unsigned char>(out.front()))) out = "Table_" + out;
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.size() > 32) out.resize(32);
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (!project::identifierProblem(out).empty()) out = "Table";
    return out;
}

} // namespace app::dropfiles
