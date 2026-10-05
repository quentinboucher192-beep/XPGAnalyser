// =============================================================================
//  hmi/HmiLanguages.cpp - l'IHM en plusieurs langues (lot 13)
// -----------------------------------------------------------------------------
//  Une traduction est rangee sous son texte d'origine, SANS LES BLANCS AUTOUR :
//  "Marche" se traduit une fois, qu'il soit le texte d'un bouton, un choix
//  d'une liste ou l'etat d'un voyant. Les listes se coupent sur ';' en gardant
//  les places vides (les libelles d'une barre de navigation suivent ses vues) ;
//  une traduction qui porte un ';' (ou un '|' dans un etat) le perd : elle
//  couperait la liste.
// =============================================================================
#include "HmiLanguages.hpp"

#include "HmiExpr.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <utility>

namespace hmi {

namespace {

constexpr std::string_view kTextKeys[] = {"text",        "textOn",      "textOff", "label",  "placeholder", "empty",
                                          "title",       "buttonText",  "unknownText", "xLabel", "yLabel", "display"};
constexpr std::string_view kListKeys[] = {"items", "positions", "labels", "tabs", "columns", "names", "themeLabels"};
constexpr std::string_view kStateKey = "stateList";

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool iequal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// Coupe sur `sep` en gardant les morceaux vides.
std::vector<std::string_view> splitRaw(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == sep) {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    return out;
}

// Un octet de tete UTF-8 qui ouvre une lettre : latin accentue, grec, cyrillique,
// hebreu, arabe (C3..DF), indien, georgien, vietnamien (E0, E1), chinois, japonais,
// coreen (E3..EF). Pas E2 (ponctuation, fleches, symboles) ni C2 (degre, guillemets).
bool letterLead(unsigned char c) noexcept {
    return (c >= 0xC3 && c <= 0xDF) || c == 0xE0 || c == 0xE1 || (c >= 0xE3 && c <= 0xEF);
}

// Des lettres hors des trous : un texte a lire. Pas "{Temperature:0.0}", ni
// "42 %", ni "--".
bool hasWords(std::string_view text) noexcept {
    int depth = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '{') {
            if (depth == 0 && i + 1 < text.size() && text[i + 1] == '{') { ++i; continue; }
            ++depth;
            continue;
        }
        if (c == '}') {
            if (depth > 0) --depth;
            continue;
        }
        if (depth > 0) continue;
        if (std::isalpha(c) || letterLead(c)) return true;
    }
    return false;
}

// Un nom plutot qu'un texte : un seul mot avec un '_', un chiffre ou une majuscule
// au milieu (IW_Raw, P-101, T1, ScaledValue) - le membre d'une structure, le
// repere d'un appareil. Il se lit, mais ne se traduit pas : hors de l'inventaire.
bool looksLikeName(std::string_view text) noexcept {
    bool lowerSeen = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (std::isspace(c)) return false;
        if (c == '_' || std::isdigit(c)) return true;
        if (std::isupper(c) && lowerSeen) return true;
        if (std::islower(c)) lowerSeen = true;
    }
    return false;
}

// Une propriete qui se lit a l'ecran, fixe (pas pilotee par une expression).
// Le texte d'un code QR est une donnee (une adresse) : il ne se traduit pas.
bool translatableProp(const Object& o, const Prop& p) noexcept {
    if (!p.expr.empty() || p.value.empty()) return false;
    if (o.kind == Kind::QrCode) return false;
    return isTranslatableKey(p.key) || isTranslatableList(p.key) || p.key == kStateKey;
}

// La traduction posee (sans repli sur le texte d'origine) ; "pt-BR" prend "pt".
const std::string* lookup(const Languages& l, std::string_view code, std::string_view source) {
    const std::string key = trim(source);
    if (key.empty()) return nullptr;
    const auto it = l.texts.find(key);
    if (it == l.texts.end()) return nullptr;
    const auto byCode = [&](std::string_view c) -> const std::string* {
        for (const auto& [lang, text] : it->second)
            if (iequal(lang, c) && !text.empty()) return &text;
        return nullptr;
    };
    if (const auto* t = byCode(code)) return t;
    const auto dash = code.find_first_of("-_");
    if (dash != std::string_view::npos) return byCode(code.substr(0, dash));
    return nullptr;
}

// Un choix, un etat : la traduction sans le separateur de sa liste.
std::string listSafe(std::string text, bool state) {
    for (auto& c : text) {
        if (c == ';') c = ',';
        else if (state && c == '|') c = '/';
    }
    return text;
}

// "{Temperature:0.0}" -> "Temperature" ; "{Marche:en marche|arret}" -> "Marche" ;
// lot 13 : "{Pression:u}" (le format et l'unite de la variable) -> "Pression".
std::string holeExpression(std::string inside) {
    const auto colon = inside.rfind(':');
    if (colon != std::string::npos && colon + 1 < inside.size() && inside[colon + 1] != '=') {
        const std::string_view fmt = std::string_view(inside).substr(colon + 1);
        if (looksLikeFormat(fmt) || fmt == "u" || fmt == "U") inside.resize(colon);
    }
    return trim(inside);
}

// Les textes d'une propriete, pour l'inventaire : la valeur, ses choix, ses etats.
void textsOf(const Prop& p, const std::function<void(const std::string&)>& add) {
    if (p.key == kStateKey) {
        for (const auto entry : splitRaw(p.value, ';')) {
            const auto eq = entry.find('=');
            if (eq == std::string_view::npos) continue;
            const auto rest = entry.substr(eq + 1);
            add(trim(rest.substr(0, rest.find('|'))));
        }
        return;
    }
    if (isTranslatableList(p.key)) {
        for (const auto item : splitRaw(p.value, ';')) add(trim(item));
        return;
    }
    add(trim(p.value));
}

struct KnownName {
    std::string_view code, name;
};
// Le nom de chaque langue dans sa langue (l'operateur reconnait la sienne), quand l'appli sait l'afficher
// sous Windows : la police (Segoe UI, sans police de secours) a son ecriture, et l'atlas l'ecrit lettre a
// lettre, de gauche a droite : latin (vietnamien compris), grec, cyrillique.
// Sinon, en francais (1.11.2) : Segoe UI n'a ni le chinois, ni le japonais, ni le coreen, ni le
// devanagari, ni le thai, dont les noms s'affichaient en « ? » (decision 165) ; l'atlas ne met pas en
// forme les ecritures de droite a gauche ou a lettres liees, l'arabe et l'hebreu, dont les noms
// s'affichaient en lettres isolees, a l'envers (decision 176). L'essai glyphesWindows1112 le garde.
constexpr KnownName kKnownNames[] = {
    {"fr", "Fran\xC3\xA7" "ais"},
    {"en", "English"},
    {"de", "Deutsch"},
    {"es", "Espa\xC3\xB1ol"},
    {"it", "Italiano"},
    {"pt", "Portugu\xC3\xAAs"},
    {"nl", "Nederlands"},
    {"pl", "Polski"},
    {"cs", "\xC4\x8C" "e\xC5\xA1tina"},
    {"sk", "Sloven\xC4\x8Dina"},
    {"sl", "Sloven\xC5\xA1\xC4\x8Dina"},
    {"hu", "Magyar"},
    {"ro", "Rom\xC3\xA2n\xC4\x83"},
    {"sv", "Svenska"},
    {"da", "Dansk"},
    {"fi", "Suomi"},
    {"nb", "Norsk bokm\xC3\xA5l"},
    {"no", "Norsk"},
    {"el", "\xCE\x95\xCE\xBB\xCE\xBB\xCE\xB7\xCE\xBD\xCE\xB9\xCE\xBA\xCE\xAC"},
    {"tr", "T\xC3\xBCrk\xC3\xA7" "e"},
    {"ru", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9"},
    {"uk", "\xD0\xA3\xD0\xBA\xD1\x80\xD0\xB0\xD1\x97\xD0\xBD\xD1\x81\xD1\x8C\xD0\xBA\xD0\xB0"},
    {"bg", "\xD0\x91\xD1\x8A\xD0\xBB\xD0\xB3\xD0\xB0\xD1\x80\xD1\x81\xD0\xBA\xD0\xB8"},
    {"hr", "Hrvatski"},
    {"sr", "Srpski"},
    {"lt", "Lietuvi\xC5\xB3"},
    {"lv", "Latvie\xC5\xA1u"},
    {"et", "Eesti"},
    {"ca", "Catal\xC3\xA0"},
    {"eu", "Euskara"},
    {"gl", "Galego"},
    {"zh", "Chinois"},
    {"ja", "Japonais"},
    {"ko", "Cor\xC3\xA9" "en"},
    {"ar", "Arabe"},
    {"he", "H\xC3\xA9" "breu"},
    {"hi", "Hindi"},
    {"th", "Tha\xC3\xAF"},
    {"vi", "Ti\xE1\xBA\xBFng Vi\xE1\xBB\x87t"},
    {"id", "Bahasa Indonesia"},
    {"ms", "Bahasa Melayu"},
};

} // namespace

// ================================================================ quoi ======
bool isTranslatableKey(std::string_view key) noexcept {
    return std::find(std::begin(kTextKeys), std::end(kTextKeys), key) != std::end(kTextKeys);
}
bool isTranslatableList(std::string_view key) noexcept {
    return std::find(std::begin(kListKeys), std::end(kListKeys), key) != std::end(kListKeys);
}

std::vector<TranslatableText> translatableTexts(const Project& p) {
    std::vector<TranslatableText> out;
    std::map<std::string, std::size_t> seen;
    const auto add = [&](const std::string& text, const std::string& use) {
        if (text.empty() || !hasWords(text) || looksLikeName(text)) return;
        const auto [it, fresh] = seen.emplace(text, out.size());
        if (fresh) out.push_back({text, {}});
        auto& uses = out[it->second].uses;
        if (std::find(uses.begin(), uses.end(), use) == uses.end()) uses.push_back(use);
    };
    for (const auto& v : p.views) {
        if (v.role == "popup" && !v.popup.title.empty()) add(trim(v.popup.title), "popup " + v.name + " (titre)");
        for (const auto& o : v.objects)
            for (const auto& prop : o.props) {
                if (!translatableProp(o, prop)) continue;
                const std::string use = v.name + "." + o.name + " (" + prop.key + ")";
                textsOf(prop, [&](const std::string& t) { add(t, use); });
            }
    }
    for (const auto& a : p.alarms) {
        add(trim(a.message), "alarme " + a.name + " (message)");
        add(trim(a.instruction), "alarme " + a.name + " (consigne)");
    }
    return out;
}

// ================================================================ traduire ==
bool isTranslated(const Languages& l, std::string_view code) noexcept {
    if (code.empty() || l.list.size() < 2 || iequal(code, l.source())) return false;
    return l.find(code) != nullptr;
}

std::string translationOf(const Languages& l, std::string_view code, std::string_view source) {
    const auto* t = lookup(l, code, source);
    return t ? *t : std::string{};
}

std::string translateText(const Languages& l, std::string_view code, std::string_view source) {
    if (!isTranslated(l, code)) return std::string(source);
    const auto* t = lookup(l, code, source);
    return t ? *t : std::string(source);
}

std::string translateList(const Languages& l, std::string_view code, std::string_view list) {
    if (!isTranslated(l, code)) return std::string(list);
    bool any = false;
    std::string out;
    bool first = true;
    for (const auto item : splitRaw(list, ';')) {
        if (!first) out += ';';
        first = false;
        const auto* t = lookup(l, code, item);
        if (t) {
            out += listSafe(*t, false);
            any = true;
        } else {
            out += item;
        }
    }
    return any ? out : std::string(list);
}

std::string translateStates(const Languages& l, std::string_view code, std::string_view states) {
    if (!isTranslated(l, code)) return std::string(states);
    bool any = false;
    std::string out;
    bool first = true;
    for (const auto entry : splitRaw(states, ';')) {
        if (!first) out += ';';
        first = false;
        const auto eq = entry.find('=');
        const std::string_view rest = eq == std::string_view::npos ? std::string_view{} : entry.substr(eq + 1);
        const auto bar = rest.find('|');
        const auto* t = eq == std::string_view::npos ? nullptr : lookup(l, code, rest.substr(0, bar));
        if (!t) {
            out += entry;
            continue;
        }
        any = true;
        out += std::string(entry.substr(0, eq + 1)) + " " + listSafe(*t, true);
        if (bar != std::string_view::npos) out += " " + std::string(rest.substr(bar));
    }
    return any ? out : std::string(states);
}

View translatedView(const View& v, const Languages& l, std::string_view code) {
    View out = v;
    if (!isTranslated(l, code)) return out;
    for (auto& o : out.objects)
        for (auto& p : o.props) {
            if (!translatableProp(o, p)) continue;
            if (p.key == kStateKey) p.value = translateStates(l, code, p.value);
            else if (isTranslatableList(p.key)) p.value = translateList(l, code, p.value);
            else p.value = translateText(l, code, p.value);
        }
    if (!out.popup.title.empty()) out.popup.title = translateText(l, code, out.popup.title);
    return out;
}

// ================================================================ langues ===
std::string languageName(std::string_view code) {
    for (const auto& k : kKnownNames) if (iequal(k.code, code)) return std::string(k.name);
    const auto dash = code.find_first_of("-_");
    if (dash != std::string_view::npos)
        for (const auto& k : kKnownNames)
            if (iequal(k.code, code.substr(0, dash))) return std::string(k.name) + " (" + std::string(code.substr(dash + 1)) + ")";
    return std::string(code);
}

const std::vector<Language>& knownLanguages() {
    static const std::vector<Language> all = [] {
        std::vector<Language> out;
        for (const auto& k : kKnownNames) out.push_back({std::string(k.code), std::string(k.name)});
        return out;
    }();
    return all;
}

bool validLanguageCode(std::string_view code) noexcept {
    std::size_t i = 0;
    while (i < code.size() && std::isalpha(static_cast<unsigned char>(code[i]))) ++i;
    if (i < 2 || i > 3) return false;
    if (i == code.size()) return true;
    if (code[i] != '-' && code[i] != '_') return false;
    const std::size_t rest = code.size() - i - 1;
    if (rest < 2 || rest > 8) return false;
    for (std::size_t k = i + 1; k < code.size(); ++k)
        if (!std::isalnum(static_cast<unsigned char>(code[k]))) return false;
    return true;
}

std::vector<LanguageCoverage> coverage(const Project& p) {
    std::vector<LanguageCoverage> out;
    if (p.languages.list.size() < 2) return out;
    const auto texts = translatableTexts(p);
    for (std::size_t i = 1; i < p.languages.list.size(); ++i) {
        const auto& lang = p.languages.list[i];
        LanguageCoverage c;
        c.code = lang.code;
        c.name = lang.name.empty() ? languageName(lang.code) : lang.name;
        c.total = texts.size();
        for (const auto& t : texts) c.translated += lookup(p.languages, lang.code, t.source) != nullptr;
        out.push_back(std::move(c));
    }
    return out;
}

// ================================================================ Excel =====
ExportTable translationTable(const Project& p) {
    ExportTable t;
    const auto& l = p.languages;
    t.title = "Traductions de l'IHM " + p.config.name;
    std::string langs;
    for (const auto& lang : l.list) langs += (langs.empty() ? "" : ", ") + (lang.name.empty() ? lang.code : lang.name) + " (" + lang.code + ")";
    t.subtitle = langs;
    t.headers.push_back("Texte (" + l.source() + ")");
    for (std::size_t i = 1; i < l.list.size(); ++i)
        t.headers.push_back((l.list[i].name.empty() ? languageName(l.list[i].code) : l.list[i].name) + " (" + l.list[i].code + ")");
    t.headers.push_back("O\xC3\xB9");
    const auto texts = translatableTexts(p);
    std::map<std::string, bool> inProject;
    const auto row = [&](const std::string& source, const std::string& where) {
        std::vector<std::string> r{source};
        for (std::size_t i = 1; i < l.list.size(); ++i) {
            const auto it = l.texts.find(source);
            std::string cell;
            if (it != l.texts.end())
                for (const auto& [code, text] : it->second)
                    if (iequal(code, l.list[i].code)) cell = text;
            r.push_back(std::move(cell));
        }
        r.push_back(where);
        t.rows.push_back(std::move(r));
    };
    for (const auto& tx : texts) {
        inProject[tx.source] = true;
        std::string where;
        for (const auto& u : tx.uses) where += (where.empty() ? "" : ", ") + u;
        row(tx.source, where);
    }
    // Les traductions d'un texte qui n'est plus dans le projet : gardees (il
    // peut revenir), en fin de tableau.
    for (const auto& [source, byCode] : l.texts) {
        if (inProject.count(source)) continue;
        bool any = false;
        for (const auto& [code, text] : byCode) any = any || !text.empty();
        if (any) row(source, "(plus dans le projet)");
    }
    return t;
}

bool parseLanguageHeader(std::string_view header, std::string& name, std::string& code) {
    const std::string h = trim(header);
    const auto open = h.rfind('(');
    if (open != std::string::npos && !h.empty() && h.back() == ')') {
        code = trim(std::string_view(h).substr(open + 1, h.size() - open - 2));
        name = trim(std::string_view(h).substr(0, open));
        if (name.empty()) name = languageName(code);
        return validLanguageCode(code);
    }
    if (!validLanguageCode(h)) return false;
    code = h;
    name = languageName(h);
    return true;
}

TranslationImport importTranslations(const Project& p, Languages& languages, const std::vector<std::string>& headers,
                                     const std::vector<std::vector<std::string>>& rows) {
    TranslationImport out;
    // La colonne du texte d'origine : "Texte (fr)", sinon celle de la langue du
    // projet, sinon la premiere. "Ou" : la colonne des usages, ignoree.
    std::size_t sourceCol = std::string::npos;
    for (std::size_t c = 0; c < headers.size() && sourceCol == std::string::npos; ++c)
        if (lowerAscii(trim(headers[c])).rfind("texte", 0) == 0) sourceCol = c;
    for (std::size_t c = 0; c < headers.size() && sourceCol == std::string::npos; ++c) {
        std::string name, code;
        if (parseLanguageHeader(headers[c], name, code) && iequal(code, languages.source())) sourceCol = c;
    }
    if (sourceCol == std::string::npos) {
        sourceCol = 0;
        if (!headers.empty()) out.warnings.push_back("pas de colonne \xC2\xAB Texte \xC2\xBB : la premi\xC3\xA8re colonne est prise pour le texte d'origine");
    }
    struct Column {
        std::size_t index;
        std::string code;
    };
    std::vector<Column> columns;
    for (std::size_t c = 0; c < headers.size(); ++c) {
        if (c == sourceCol) continue;
        const std::string h = trim(headers[c]);
        const std::string low = lowerAscii(h);
        if (h.empty() || low == "ou" || low == "o\xC3\xB9") continue;
        std::string name, code;
        if (!parseLanguageHeader(h, name, code)) {
            out.warnings.push_back("colonne \xC2\xAB " + h + " \xC2\xBB ignor\xC3\xA9" "e : pas de code de langue (English (en))");
            continue;
        }
        if (iequal(code, languages.source())) continue;
        const Language* known = languages.find(code);
        if (!known) {
            languages.list.push_back({code, name});
            out.languagesAdded.push_back(name + " (" + code + ")");
            known = &languages.list.back();
        }
        columns.push_back({c, known->code});
    }
    std::map<std::string, bool> inProject;
    for (const auto& t : translatableTexts(p)) inProject[t.source] = true;
    for (const auto& r : rows) {
        if (sourceCol >= r.size()) continue;
        const std::string source = trim(r[sourceCol]);
        if (source.empty()) continue;
        if (!inProject.count(source)) ++out.unknown;
        for (const auto& col : columns) {
            const std::string cell = col.index < r.size() ? trim(r[col.index]) : std::string{};
            auto it = languages.texts.find(source);
            std::string* current = nullptr;
            std::string currentKey;
            if (it != languages.texts.end())
                for (auto& [code, text] : it->second)
                    if (iequal(code, col.code)) {
                        current = &text;
                        currentKey = code;
                    }
            if (cell.empty()) {
                if (current) {
                    const bool had = !current->empty();
                    it->second.erase(currentKey);
                    if (it->second.empty()) languages.texts.erase(it);
                    out.cleared += had;
                }
                continue;
            }
            if (current && *current == cell) continue;
            languages.texts[source][currentKey.empty() ? col.code : currentKey] = cell;
            ++out.updated;
            // Les trous : une traduction qui en perd (ou en invente) ne montrera
            // pas la meme chose.
            auto a = templateHoles(source), b = templateHoles(cell);
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            if (a != b)
                out.warnings.push_back("\xC2\xAB " + source + " \xC2\xBB (" + col.code + ") : la traduction ne reprend pas les m\xC3\xAAmes trous {...}");
        }
    }
    return out;
}

// ================================================================ selecteur =
std::vector<LanguageChoice> languageChoices(const Object& o, const Languages& l) {
    std::vector<LanguageChoice> out;
    const std::string mode = lowerAscii(trim(o.text("languageLabel", "code et nom")));
    const auto add = [&](const Language& lang) {
        for (const auto& c : out) if (iequal(c.code, lang.code)) return;
        LanguageChoice c;
        c.code = lang.code;
        c.name = lang.name.empty() ? languageName(lang.code) : lang.name;
        std::string code = lang.code;
        for (auto& ch : code) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        c.label = mode == "code" ? code : mode == "nom" ? c.name : code + "  " + c.name;
        out.push_back(std::move(c));
    };
    for (const auto item : splitRaw(o.text("languages"), ';')) {
        const std::string code = trim(item);
        if (code.empty()) continue;
        if (const auto* lang = l.find(code)) add(*lang);
    }
    if (out.empty())
        for (const auto& lang : l.list) add(lang);
    return out;
}

LanguageSelectorLayout languageSelectorLayout(const Object& o, double w, double h, std::size_t count) {
    LanguageSelectorLayout out;
    out.vertical = o.text("orientation", "horizontale").rfind("vert", 0) == 0;
    out.toggle = o.text("languageStyle", "boutons") == "bascule";
    out.fontSize = std::clamp(o.number("fontSize", 15), 7.0, 60.0);
    const double gap = std::max(0.0, o.number("gap", 4));
    const std::size_t n = out.toggle ? (count ? 1 : 0) : count;
    if (n == 0 || w <= 0 || h <= 0) return out;
    const double span = (out.vertical ? h : w) - gap * static_cast<double>(n - 1);
    const double each = std::max(1.0, span / static_cast<double>(n));
    for (std::size_t i = 0; i < n; ++i) {
        const double at = static_cast<double>(i) * (each + gap);
        out.buttons.push_back(out.vertical ? Box{0, at, w, each} : Box{at, 0, each, h});
    }
    return out;
}

std::string languageSelectorHit(const Object& o, double w, double h, double lx, double ly, std::size_t count) {
    const auto l = languageSelectorLayout(o, w, h, count);
    for (std::size_t i = 0; i < l.buttons.size(); ++i)
        if (l.buttons[i].contains(lx, ly)) return l.toggle ? std::string("langue:suivante") : "langue:" + std::to_string(i);
    return {};
}

std::vector<std::string> templateHoles(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if ((c == '{' || c == '}') && i + 1 < text.size() && text[i + 1] == c) { i += 2; continue; }
        if (c != '{') { ++i; continue; }
        const auto close = text.find('}', i + 1);
        if (close == std::string_view::npos) break;
        out.push_back(holeExpression(std::string(text.substr(i + 1, close - i - 1))));
        i = close + 1;
    }
    return out;
}

} // namespace hmi
