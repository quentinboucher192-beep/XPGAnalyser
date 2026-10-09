#include "HmiScript.hpp"

#include "../sim/Interpreter.hpp"
#include "HmiScriptCheck110.hpp"   // 1.10 : les noms du dialecte (fonctions internes, FOR EACH)
#include "HmiDecl.hpp"              // 1.11.18 (refonte, lot 3) : les declarations du modele, reconstruites
#include "HmiMarkers.hpp"          // 1.11.1 (REP) : les $ des reperes, transparents dans un script ST
#include "HmiOperators.hpp"        // 1.11.17 : la signature d'un operateur (le lieu d'un appel)
#include "HmiSymbols.hpp"          // 1.11.17 : la portee d'un symbole, rewriteNames (les textes d'un objet)
#include "HmiTypeRegistry.hpp"     // 1.11.19 (refonte, lot 6) : les types d'une locale

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <set>
#include <type_traits>

namespace hmi {

namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

// Les delimiteurs d'un code C / C++, avec la ligne de la faute.
std::vector<ScriptDiagnostic> checkC(std::string_view code) {
    std::vector<ScriptDiagnostic> out;
    struct Open { char c; int line; };
    std::vector<Open> stack;
    bool inString = false, inChar = false, lineComment = false, blockComment = false;
    int line = 1, stringLine = 0, commentLine = 0;
    for (std::size_t i = 0; i < code.size(); ++i) {
        const char c = code[i];
        const char n = i + 1 < code.size() ? code[i + 1] : '\0';
        if (c == '\n') {
            ++line;
            if (inString || inChar) {
                out.push_back({ScriptDiagnostic::Severity::Error, stringLine,
                               inString ? "cha\xC3\xAEne non ferm\xC3\xA9" "e en fin de ligne" : "caract\xC3\xA8re non ferm\xC3\xA9"});
                inString = inChar = false;
            }
            lineComment = false;
            continue;
        }
        if (lineComment) continue;
        if (blockComment) { if (c == '*' && n == '/') { blockComment = false; ++i; } continue; }
        if (inString) { if (c == '\\') ++i; else if (c == '"') inString = false; continue; }
        if (inChar) { if (c == '\\') ++i; else if (c == '\'') inChar = false; continue; }
        if (c == '/' && n == '/') { lineComment = true; ++i; continue; }
        if (c == '/' && n == '*') { blockComment = true; commentLine = line; ++i; continue; }
        if (c == '"') { inString = true; stringLine = line; continue; }
        if (c == '\'') { inChar = true; stringLine = line; continue; }
        if (c == '(' || c == '{' || c == '[') stack.push_back({c, line});
        if (c == ')' || c == '}' || c == ']') {
            const char want = c == ')' ? '(' : c == '}' ? '{' : '[';
            if (stack.empty() || stack.back().c != want) {
                std::string msg = std::string("'") + c + "' sans ouverture correspondante";
                if (!stack.empty())
                    msg += " (le '" + std::string(1, stack.back().c) + "' de la ligne " + std::to_string(stack.back().line)
                         + " est encore ouvert)";
                out.push_back({ScriptDiagnostic::Severity::Error, line, msg});
                return out;
            }
            stack.pop_back();
        }
    }
    if (inString) out.push_back({ScriptDiagnostic::Severity::Error, stringLine, "cha\xC3\xAEne non ferm\xC3\xA9" "e"});
    if (blockComment) out.push_back({ScriptDiagnostic::Severity::Error, commentLine, "commentaire /* non ferm\xC3\xA9"});
    if (!stack.empty())
        out.push_back({ScriptDiagnostic::Severity::Error, stack.back().line,
                       std::string("'") + stack.back().c + "' ouvert ici n'est jamais ferm\xC3\xA9"});
    return out;
}

const std::set<std::string>& stKeywords() {
    static const std::set<std::string> k = {
        "AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE", "IF", "THEN", "ELSIF", "ELSE", "END_IF", "CASE", "OF",
        "END_CASE", "FOR", "TO", "BY", "DO", "END_FOR", "WHILE", "END_WHILE", "REPEAT", "UNTIL", "END_REPEAT",
        "EXIT", "RETURN"};
    return k;
}

// Les arguments textes d'un appel : IHM_APPELER('X') -> X, pour chaque appel.
std::vector<std::string> quotedArgs(std::string_view body, std::string_view function) {
    std::vector<std::string> out;
    const std::string up = upper(body);
    const std::string f = upper(function);
    for (std::size_t at = up.find(f); at != std::string::npos; at = up.find(f, at + f.size())) {
        if (at > 0 && identChar(up[at - 1])) continue;
        std::size_t k = at + f.size();
        while (k < body.size() && std::isspace(static_cast<unsigned char>(body[k]))) ++k;
        if (k >= body.size() || body[k] != '(') continue;
        ++k;
        while (k < body.size() && std::isspace(static_cast<unsigned char>(body[k]))) ++k;
        if (k >= body.size() || (body[k] != '\'' && body[k] != '"')) continue;
        const char q = body[k];
        const auto end = body.find(q, k + 1);
        if (end == std::string_view::npos) continue;
        out.emplace_back(body.substr(k + 1, end - k - 1));
    }
    return out;
}

} // namespace

std::vector<NameUse> scriptNamesRaw(std::string_view s);

int splitLine(std::string_view m, std::string* rest) {
    // "line 3: expected THEN" (le parseur) ; "ligne 3 : ..." (deja traduit).
    for (std::string_view prefix : {std::string_view("line "), std::string_view("ligne ")}) {
        if (!startsWith(m, prefix)) continue;
        std::size_t k = prefix.size();
        int line = 0;
        while (k < m.size() && std::isdigit(static_cast<unsigned char>(m[k]))) line = line * 10 + (m[k++] - '0');
        while (k < m.size() && (m[k] == ' ' || m[k] == ':')) ++k;
        if (rest) *rest = std::string(m.substr(k));
        return line;
    }
    if (rest) *rest = std::string(m);
    return 0;
}

std::string frenchSimMessage(std::string_view english) {
    std::string rest;
    const int line = splitLine(english, &rest);
    // "expected END_IF (found '')" : ce que le parseur a trouve a la place.
    std::string found;
    bool hasFound = false;
    if (const auto at = rest.rfind(" (found '"); at != std::string::npos && !rest.empty() && rest.back() == ')') {
        found = rest.substr(at + 9, rest.size() - at - 9 - 2);
        rest.resize(at);
        hasFound = true;
    }
    std::string m = rest;
    const auto replaceAll = [&](std::string_view from, std::string_view to) {
        for (auto at = m.find(from); at != std::string::npos; at = m.find(from, at + to.size())) m.replace(at, from.size(), to);
    };
    std::string out;
    if (startsWith(m, "expected ")) {
        std::string what = m.substr(9);
        if (what == "a value") out = "une valeur est attendue";
        else if (what == "':=' or a call") out = "':=' ou un appel attendu";
        else if (startsWith(what, "the ")) out = what.substr(4) + " attendu";
        else if (startsWith(what, "a ")) out = what.substr(2) + " attendu";
        else out = what + " attendu";
        // Quelques mots du parseur, en francais.
        const std::pair<const char*, const char*> words[] = {
            {"loop variable", "la variable de boucle"}, {"case label", "une \xC3\xA9tiquette de CASE"},
            {"end of the range", "la fin de l'intervalle"}, {"after the case labels", "apr\xC3\xA8s les \xC3\xA9tiquettes"},
            {"after the loop variable", "apr\xC3\xA8s la variable de boucle"}};
        for (const auto& [en, fr] : words)
            for (auto at = out.find(en); at != std::string::npos; at = out.find(en, at + std::strlen(fr))) out.replace(at, std::strlen(en), fr);
    } else if (m.find("cannot be written") != std::string::npos) {
        replaceAll(" cannot be written", " ne peut pas \xC3\xAAtre \xC3\xA9" "crite (inconnue, ou en lecture seule)");
        out = m;
    } else if (m.find("is not a function or a block this simulator knows") != std::string::npos) {
        replaceAll(" is not a function or a block this simulator knows", " : fonction ou bloc inconnu du simulateur");
        out = m;
    } else if (startsWith(m, "WHILE loop ran more than") || startsWith(m, "REPEAT loop ran more than")
               || startsWith(m, "FOR loop ran more than")) {
        const std::string kind = m.substr(0, m.find(' '));
        const auto from = m.find("more than ") + 10;
        const std::string count = m.substr(from, m.find(' ', from) - from);
        out = "boucle " + kind + " : plus de " + count + " tours, arr\xC3\xAAt\xC3\xA9" "e (boucle sans fin ?)";
    } else if (startsWith(m, "the scan exceeded")) {
        const auto from = m.find("exceeded ") + 9;
        const std::string count = m.substr(from, m.find(' ', from) - from);
        out = "plus de " + count + " instructions : arr\xC3\xAAt\xC3\xA9 (une boucle ne se termine sans doute pas)";
    } else if (m == "division by zero") {
        out = "division par z\xC3\xA9ro";
    } else if (m == "modulo by zero") {
        out = "modulo par z\xC3\xA9ro";
    } else if (m == "FOR loop with a step of zero would never end") {
        out = "boucle FOR de pas nul : elle ne finirait jamais";
    } else if (m == "bad loop bounds") {
        out = "bornes de boucle illisibles";
    } else if (m.find("has no output called") != std::string::npos) {
        replaceAll(" has no output called ", " n'a pas de sortie ");
        out = m;
    } else if (startsWith(m, "unexpected character ")) {
        out = "caract\xC3\xA8re inattendu : " + m.substr(21);
    } else if (startsWith(m, "unexpected ")) {
        out = "inattendu : " + m.substr(11);
    } else {
        out = m;
    }
    if (hasFound) out += found.empty() ? std::string(" (fin du script atteinte)") : " (trouv\xC3\xA9 : '" + found + "')";
    return line ? "ligne " + std::to_string(line) + " : " + out : out;
}

std::vector<ScriptDiagnostic> checkScript(ScriptLang lang, std::string_view code, std::string_view name, const TypeKnown& knownType) {
    std::vector<ScriptDiagnostic> out;
    // 1.11.1 (REP) : un script ST se lit sans les $ de ses reperes, comme une expression
    // (les chaines ST, (* *) et // gardent les leurs) ; les lignes ne bougent pas. Le C et
    // le C++ ne sont pas compiles : checkC ne lit pas les $ (rien n'est retire).
    const std::string st = lang == ScriptLang::ST ? markers::strip(code) : std::string{};
    const std::string_view body = lang == ScriptLang::ST ? std::string_view(st) : code;
    if (lang != ScriptLang::ST) {
        out = checkC(body);
        out.push_back({ScriptDiagnostic::Severity::Info, 0,
                       std::string(scriptLangKey(lang)) + " : \xC3\xA9" "dit\xC3\xA9 et v\xC3\xA9rifi\xC3\xA9, non ex\xC3\xA9" "cut\xC3\xA9 en simulation"});
        return out;
    }
    if (body.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        out.push_back({ScriptDiagnostic::Severity::Warning, 0, "script vide"});
        return out;
    }
    // Lot 7 : les blocs VAR / VAR_TEMP d'abord ; le simulateur lit le reste.
    const auto parts = splitDeclarations(body, false, knownType);
    out.insert(out.end(), parts.errors.begin(), parts.errors.end());
    auto program = sim::parse(parts.body, std::string(name), dialectOptions());   // 1.10 : le dialecte IHM
    if (!program) {
        const std::string raw = program.error().context.empty() ? program.error().message() : program.error().context;
        std::string rest;
        const int line = splitLine(raw, &rest);
        // 1.11.2 (REP) : un $ reste seul (le $ de fin d'un repere oublie ?) - comme dans une expression, la
        // faute dite a sa place avec la ligne juste, puis la regle ("il manque le $ de fin de $V[1] : ecris
        // y := $V[1]$; - un repere s'ecrit entre deux $..."), au lieu du seul "caractere inattendu : '$'".
        std::string fr = markers::explainScriptDollar(frenchSimMessage(rest), code, line);
        out.push_back({ScriptDiagnostic::Severity::Error, line, fr});
    }
    return out;
}

std::vector<ScriptDiagnostic> checkScript(const Script& sc, const TypeKnown& knownType) {
    if (sc.decls.empty() || sc.lang != ScriptLang::ST) return checkScript(sc.lang, sc.body, sc.name, knownType);
    // 1.11.18 (refonte, lot 3) : ses declarations du modele d'abord ; le code se lit avec
    // celles qui sont justes, reconstruites sur sa ligne 1 (les lignes ne bougent pas).
    std::vector<Declaration> valid;
    auto out = decl::checkDeclarations(sc.decls, decl::Role::Script, sc.body, knownType, nullptr, &valid);
    if (sc.body.find_first_not_of(" \t\r\n") == std::string::npos) {
        out.push_back({ScriptDiagnostic::Severity::Warning, 0, "script vide"});
        return out;
    }
    const auto composed = decl::composeCode(sc.body, valid, decl::Role::Script);
    for (auto d : checkScript(sc.lang, composed.text, sc.name, knownType)) {
        d.column = composed.bodyColumn(d.line, d.column);
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<NameUse> scriptNames(std::string_view source) {
    const std::string code = markers::strip(source);   // 1.11.1 (REP) : les $ des reperes, transparents
    // Lot 7 : ce que les blocs de declaration nomment n'est pas lu ailleurs, et
    // une variable locale n'est ni IHM ni de l'automate. 1.10 : une locale de
    // type riche (MAP, ARRAY, REF_TO, un type IHM) reste une locale ; les
    // parametres et locales des fonctions internes, les noms de FOR EACH aussi.
    const auto parts = splitDeclarations(code, true, [](std::string_view) { return true; });
    const auto lang = lang110::analyze(code);
    const auto dialectName = [&](const std::string& name) {
        const std::string u = upper(name);
        for (const auto& f : lang.functions) {
            if (upper(f.name) == u) return true;
            for (const auto& p : f.params) if (upper(p.name) == u) return true;
            for (const auto& l : f.locals) if (upper(l.name) == u) return true;
        }
        for (const auto& n : lang.names) if (upper(n.name) == u) return true;
        return false;
    };
    std::vector<NameUse> all = scriptNamesRaw(parts.body);
    std::vector<NameUse> out;
    for (auto& u : all)
        if (!parts.local(u.name) && !dialectName(u.name)) out.push_back(std::move(u));
    return out;
}

std::vector<NameUse> scriptNamesRaw(std::string_view s) {
    std::vector<NameUse> out;
    int line = 1;
    std::size_t i = 0;
    const auto advanceTo = [&](std::size_t to) {
        for (; i < to && i < s.size(); ++i) if (s[i] == '\n') ++line;
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == '\'' || c == '"') {
            const auto end = s.find(c, i + 1);
            advanceTo(end == std::string_view::npos ? s.size() : end + 1);
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            advanceTo(end == std::string_view::npos ? s.size() : end + 2);
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            advanceTo(end == std::string_view::npos ? s.size() : end);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            while (i < s.size() && (identChar(s[i]) || s[i] == '#' || s[i] == '.')) ++i;
            continue;
        }
        if (identStart(c)) {
            const std::size_t start = i;
            while (i < s.size() && identChar(s[i])) ++i;
            const std::string word(s.substr(start, i - start));
            const bool member = start > 0 && s[start - 1] == '.';
            const bool typed = i < s.size() && s[i] == '#';
            std::size_t k = i;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            const bool call = k < s.size() && s[k] == '(';
            if (typed) {
                ++i;
                while (i < s.size() && (identChar(s[i]) || s[i] == '.')) ++i;
                continue;
            }
            // 1.11.14 : IHM_LOG(INFO, ...) - le premier argument est un niveau (NIVEAU_LOG),
            // pas une variable : il n'est pas lu ici (Compiler verifie que c'est un niveau).
            if (call && !member && upper(word) == "IHM_LOG") {
                std::size_t a = k + 1;
                while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
                std::size_t b = a;
                while (b < s.size() && identChar(s[b])) ++b;
                std::size_t after = b;
                while (after < s.size() && (s[after] == ' ' || s[after] == '\t')) ++after;
                if (b > a && identStart(s[a]) && after < s.size() && (s[after] == ',' || s[after] == ')')) advanceTo(b);
                continue;
            }
            if (member || call || stKeywords().count(upper(word))) continue;
            const bool seen = std::any_of(out.begin(), out.end(), [&](const NameUse& u) { return upper(u.name) == upper(word); });
            if (!seen) out.push_back({word, line});
            continue;
        }
        ++i;
    }
    return out;
}

std::vector<PathUse> scriptPaths(std::string_view source) {
    const std::string s = markers::strip(source);   // 1.11.1 (REP) : $Vanne$.Ouv se lit Vanne.Ouv (les lignes restent)
    std::vector<PathUse> out;
    int line = 1;
    std::size_t i = 0;
    const auto advanceTo = [&](std::size_t to) {
        for (; i < to && i < s.size(); ++i) if (s[i] == '\n') ++line;
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == '\'' || c == '"') {
            const auto end = s.find(c, i + 1);
            advanceTo(end == std::string_view::npos ? s.size() : end + 1);
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            advanceTo(end == std::string_view::npos ? s.size() : end + 2);
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            advanceTo(end == std::string_view::npos ? s.size() : end);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            while (i < s.size() && (identChar(s[i]) || s[i] == '#' || s[i] == '.')) ++i;
            continue;
        }
        if (identStart(c)) {
            const std::size_t start = i;
            const bool member = start > 0 && s[start - 1] == '.';
            while (i < s.size() && identChar(s[i])) ++i;
            if (i < s.size() && s[i] == '#') {           // T#5s, INT#3
                ++i;
                while (i < s.size() && (identChar(s[i]) || s[i] == '.')) ++i;
                continue;
            }
            if (member) continue;
            // La suite pointee : .nom.nom (un index l'arrete).
            std::size_t end = i;
            while (end + 1 < s.size() && s[end] == '.' && identStart(s[end + 1])) {
                end += 1;
                while (end < s.size() && identChar(s[end])) ++end;
            }
            std::size_t k = end;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            const bool call = k < s.size() && s[k] == '(';
            const bool assigned = k + 1 < s.size() && s[k] == ':' && s[k + 1] == '=';
            const std::string word(s.substr(start, i - start));
            if (!call && !stKeywords().count(upper(word)) && end > i)
                out.push_back({std::string(s.substr(start, end - start)), line, assigned});
            i = end;
            continue;
        }
        ++i;
    }
    return out;
}

std::vector<std::string> scriptCalls(std::string_view body) { return quotedArgs(body, "IHM_APPELER"); }

bool isReservedWord(std::string_view name) {
    static const std::set<std::string> blocks = {"VAR", "VAR_TEMP", "VAR_INPUT", "VAR_IN_OUT", "VAR_OUTPUT", "END_VAR", "CONSTANT",
                                                 "RETAIN", "FUNCTION", "END_FUNCTION"};
    const std::string u = upper(name);
    return stKeywords().count(u) > 0 || blocks.count(u) > 0;
}

std::vector<std::string> scriptViews(std::string_view body) {
    auto v = quotedArgs(body, "IHM_NAVIGUER");
    for (const char* f : {"IHM_POPUP", "IHM_CHANGER_POPUP", "IHM_CENTRER_POPUP", "IHM_POPUP_OUVERTE"}) {
        const auto p = quotedArgs(body, f);
        v.insert(v.end(), p.begin(), p.end());
    }
    return v;
}

// ================================================================ lot 7 =====
const LocalVar* ScriptParts::local(std::string_view name) const noexcept {
    const std::string u = upper(name);
    for (const auto& l : locals) if (upper(l.name) == u) return &l;
    return nullptr;
}

std::vector<const LocalVar*> ScriptParts::inputs() const {
    std::vector<const LocalVar*> out;
    for (const auto& l : locals) if (l.section == LocalVar::Section::Input) out.push_back(&l);
    return out;
}

// 1.11.19 (refonte, lot 6) : les types d'une locale viennent du registre des types (ceux qu'une
// declaration propose : BOOL, SINT ... STRING, TIME), sous leur nom exact.
bool localTypeSupported(std::string_view type) noexcept {
    const std::string u = upper(type);
    const auto* e = typereg::baseRegistry().byName(u);
    return e && (e->proposed & typereg::UseDeclaration) != 0 && u == e->name;
}

bool richLocalType(std::string_view type, const std::function<bool(std::string_view)>& knownType) {
    std::string u = upper(type);
    while (!u.empty() && std::isspace(static_cast<unsigned char>(u.back()))) u.pop_back();
    if (u.empty()) return false;
    for (std::string_view p : {"ARRAY", "MAP", "REF_TO", "REFERENCE", "POINTER", "STRING"})
        if (u.rfind(p, 0) == 0 && (u.size() == p.size() || !identChar(u[p.size()]))) return true;
    if (u == "MAP_ITERATOR" || u == "ITERATOR") return true;
    // un type de base permis a une declaration (LINT, ULINT : sans etre proposes)
    if (const auto* e = typereg::baseRegistry().byName(u); e && e->usable(typereg::UseDeclaration) && u == e->name) return true;
    // Un type IHM (structure) : un nom que le projet connait.
    if (!identStart(u[0])) return false;
    for (const char c : u) if (!identChar(c)) return false;
    return knownType && knownType(type);
}

namespace {
std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
bool isIdent(std::string_view s) {
    if (s.empty() || !identStart(s[0])) return false;
    for (const char c : s) if (!identChar(c)) return false;
    return true;
}
} // namespace

ScriptParts splitDeclarations(std::string_view code, bool function, const TypeKnown& knownType) {
    ScriptParts out;
    out.body.assign(code.begin(), code.end());
    using E = ScriptDiagnostic::Severity;
    // On avance dans le code en sautant chaines et commentaires ; un mot VAR,
    // VAR_TEMP ou VAR_INPUT ouvre un bloc, END_VAR le ferme.
    int line = 1;
    std::size_t i = 0;
    const auto skipTo = [&](std::size_t to) {
        for (; i < to && i < code.size(); ++i) if (code[i] == '\n') ++line;
    };
    const auto blank = [&](std::size_t from, std::size_t to) {
        for (std::size_t k = from; k < to && k < out.body.size(); ++k)
            if (out.body[k] != '\n') out.body[k] = ' ';
    };
    while (i < code.size()) {
        const char c = code[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == '\'' || c == '"') {
            const auto end = code.find(c, i + 1);
            skipTo(end == std::string_view::npos ? code.size() : end + 1);
            continue;
        }
        if (c == '(' && i + 1 < code.size() && code[i + 1] == '*') {
            const auto end = code.find("*)", i + 2);
            skipTo(end == std::string_view::npos ? code.size() : end + 2);
            continue;
        }
        if (c == '/' && i + 1 < code.size() && code[i + 1] == '/') {
            const auto end = code.find('\n', i);
            skipTo(end == std::string_view::npos ? code.size() : end);
            continue;
        }
        if (!identStart(c) || (i > 0 && (identChar(code[i - 1]) || code[i - 1] == '.'))) { ++i; continue; }
        const std::size_t wordStart = i;
        while (i < code.size() && identChar(code[i])) ++i;
        const std::string word = upper(code.substr(wordStart, i - wordStart));
        // 1.10 : une fonction interne (FUNCTION ... END_FUNCTION) garde ses
        // declarations : le simulateur (dialecte IHM) les lit avec elle.
        if (word == "FUNCTION" && !function) {
            while (i < code.size()) {
                const char d = code[i];
                if (d == '\n') { ++line; ++i; continue; }
                if (d == '\'' || d == '"') {
                    const auto end = code.find(d, i + 1);
                    skipTo(end == std::string_view::npos ? code.size() : end + 1);
                    continue;
                }
                if (d == '(' && i + 1 < code.size() && code[i + 1] == '*') {
                    const auto end = code.find("*)", i + 2);
                    skipTo(end == std::string_view::npos ? code.size() : end + 2);
                    continue;
                }
                if (d == '/' && i + 1 < code.size() && code[i + 1] == '/') {
                    const auto end = code.find('\n', i);
                    skipTo(end == std::string_view::npos ? code.size() : end);
                    continue;
                }
                if (identStart(d) && !identChar(code[i - 1])) {
                    std::size_t k = i;
                    while (k < code.size() && identChar(code[k])) ++k;
                    const bool end = upper(code.substr(i, k - i)) == "END_FUNCTION";
                    i = k;
                    if (end) break;
                    continue;
                }
                ++i;
            }
            continue;
        }
        if (word != "VAR" && word != "VAR_TEMP" && word != "VAR_INPUT") continue;
        const int blockLine = line;
        LocalVar::Section section = word == "VAR" ? LocalVar::Section::Var
                                  : word == "VAR_TEMP" ? LocalVar::Section::Temp : LocalVar::Section::Input;
        if (section == LocalVar::Section::Input && !function)
            out.errors.push_back({E::Error, blockLine, "VAR_INPUT : seulement dans une fonction IHM (les param\xC3\xA8tres)"});
        // Le contenu du bloc, commentaires ote, jusqu'a END_VAR.
        std::string decl;
        std::vector<int> declLines;   // la ligne de chaque caractere de `decl`
        bool closed = false;
        // RETAIN, CONSTANT apres VAR : acceptes (VAR est deja gardee).
        while (i < code.size()) {
            const char d = code[i];
            if (d == '\n') { ++line; decl += ' '; declLines.push_back(line); ++i; continue; }
            if (d == '(' && i + 1 < code.size() && code[i + 1] == '*') {
                const auto end = code.find("*)", i + 2);
                skipTo(end == std::string_view::npos ? code.size() : end + 2);
                continue;
            }
            if (d == '/' && i + 1 < code.size() && code[i + 1] == '/') {
                const auto end = code.find('\n', i);
                skipTo(end == std::string_view::npos ? code.size() : end);
                continue;
            }
            if (identStart(d) && !(i > 0 && identChar(code[i - 1]))) {
                std::size_t k = i;
                while (k < code.size() && identChar(code[k])) ++k;
                const std::string w = upper(code.substr(i, k - i));
                if (w == "END_VAR") {
                    i = k;
                    while (i < code.size() && (code[i] == ' ' || code[i] == '\t')) ++i;
                    if (i < code.size() && code[i] == ';') ++i;
                    closed = true;
                    break;
                }
                if ((w == "RETAIN" || w == "CONSTANT") && trimmed(decl).empty()) { i = k; continue; }
                for (std::size_t m = i; m < k; ++m) { decl += code[m]; declLines.push_back(line); }
                i = k;
                continue;
            }
            decl += d;
            declLines.push_back(line);
            ++i;
        }
        blank(wordStart, i);
        if (!closed) {
            out.errors.push_back({E::Error, blockLine, word + " sans END_VAR"});
            break;
        }
        // Les declarations : "a, b : INT := 0;".
        std::size_t start = 0;
        while (start < decl.size()) {
            auto semi = decl.find(';', start);
            if (semi == std::string::npos) semi = decl.size();
            const std::string one = trimmed(std::string_view(decl).substr(start, semi - start));
            std::size_t lead = start;
            while (lead < semi && std::isspace(static_cast<unsigned char>(decl[lead]))) ++lead;
            const int at = lead < declLines.size() ? declLines[lead] : blockLine;
            start = semi + 1;
            if (one.empty()) continue;
            if (semi == decl.size()) out.errors.push_back({E::Error, at, "';' attendu apr\xC3\xA8s la d\xC3\xA9" "claration \xC2\xAB " + one + " \xC2\xBB"});
            const auto colon = one.find(':');
            if (colon == std::string::npos || (colon + 1 < one.size() && one[colon + 1] == '=')) {
                out.errors.push_back({E::Error, at, "d\xC3\xA9" "claration illisible : \xC2\xAB " + one + " \xC2\xBB (nom : TYPE ;)"});
                continue;
            }
            std::string type = trimmed(std::string_view(one).substr(colon + 1));
            std::string initial;
            if (const auto assign = type.find(":="); assign != std::string::npos) {
                initial = trimmed(std::string_view(type).substr(assign + 2));
                type = trimmed(std::string_view(type).substr(0, assign));
            }
            // 1.10 : un script (dialecte IHM) a aussi des locales riches : ARRAY[..]
            // (N dimensions), MAP[..] OF, REF_TO, POINTER TO, une structure IHM.
            // (1.10 : aussi dans une fonction IHM : une fonction aux types riches tourne dans le dialecte.)
            const bool rich = !localTypeSupported(type) && richLocalType(trimmed(type), knownType);
            if (!localTypeSupported(type) && !rich) {
                out.errors.push_back({E::Error, at, "type non pris en charge pour une variable locale : " + type
                                                        + " (BOOL, INT, DINT, REAL, TIME, STRING...)"});
                continue;
            }
            if (!rich) type = upper(type);
            std::string names = std::string(std::string_view(one).substr(0, colon));
            std::size_t ns = 0;
            while (ns <= names.size()) {
                auto comma = names.find(',', ns);
                if (comma == std::string::npos) comma = names.size();
                const std::string name = trimmed(std::string_view(names).substr(ns, comma - ns));
                ns = comma + 1;
                if (name.empty()) continue;
                if (!isIdent(name) || stKeywords().count(upper(name))) {
                    out.errors.push_back({E::Error, at, "nom de variable locale invalide : '" + name + "'"});
                    continue;
                }
                if (out.local(name)) {
                    out.errors.push_back({E::Error, at, "variable locale en double : " + name});
                    continue;
                }
                out.locals.push_back({name, type, initial, section, at});
            }
        }
    }
    return out;
}

std::vector<NameUse> scriptCallees(std::string_view code) {
    std::vector<NameUse> out;
    const auto parts = splitDeclarations(code, true);
    const std::string_view s = parts.body;
    int line = 1;
    std::size_t i = 0;
    std::set<std::string> seen;
    const auto skipTo = [&](std::size_t to) {
        for (; i < to && i < s.size(); ++i) if (s[i] == '\n') ++line;
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == '\'' || c == '"') { const auto end = s.find(c, i + 1); skipTo(end == std::string_view::npos ? s.size() : end + 1); continue; }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') { const auto end = s.find("*)", i + 2); skipTo(end == std::string_view::npos ? s.size() : end + 2); continue; }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') { const auto end = s.find('\n', i); skipTo(end == std::string_view::npos ? s.size() : end); continue; }
        if (!identStart(c) || (i > 0 && (identChar(s[i - 1]) || s[i - 1] == '.'))) { ++i; continue; }
        const std::size_t start = i;
        while (i < s.size() && identChar(s[i])) ++i;
        std::size_t k = i;
        while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
        if (k < s.size() && s[k] == '(') {
            const std::string name(s.substr(start, i - start));
            if (!stKeywords().count(upper(name)) && seen.insert(upper(name)).second) out.push_back({name, line});
        }
    }
    return out;
}

std::string renameCalls(std::string_view s, std::string_view from, std::string_view to, bool bare) {
    std::string out;
    out.reserve(s.size() + 16);
    const std::string key = upper(from);
    std::size_t i = 0;
    const auto copyTo = [&](std::size_t end) {
        end = std::min(end, s.size());
        out.append(s.substr(i, end - i));
        i = end;
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\'' || c == '"') {
            std::size_t k = i + 1;
            while (k < s.size() && s[k] != c) k += s[k] == '$' ? 2 : 1;
            copyTo(k + 1);
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            copyTo(end == std::string_view::npos ? s.size() : end + 2);
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            copyTo(end == std::string_view::npos ? s.size() : end);
            continue;
        }
        if (!identStart(c) || (i > 0 && (identChar(s[i - 1]) || s[i - 1] == '.' || s[i - 1] == '#'))) {
            out += c;
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < s.size() && identChar(s[i])) ++i;
        const auto word = s.substr(start, i - start);
        bool rename = upper(word) == key;
        if (rename && !bare) {
            std::size_t k = i;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            rename = k < s.size() && s[k] == '(';
        }
        out.append(rename ? to : word);
    }
    return out;
}

std::string rewriteInText(std::string_view text, const std::function<std::string(std::string_view)>& f) {
    if (!text.empty() && text.front() == '=') return "=" + f(text.substr(1));
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto open = text.find('{', i);
        if (open == std::string_view::npos) break;
        const auto close = text.find('}', open);
        if (close == std::string_view::npos) break;
        out.append(text.substr(i, open + 1 - i));
        out += f(text.substr(open + 1, close - open - 1));
        out += '}';
        i = close + 1;
    }
    out.append(text.substr(i));
    return out;
}

std::string renameCallsInText(std::string_view text, std::string_view from, std::string_view to) {
    return rewriteInText(text, [&](std::string_view e) { return renameCalls(e, from, to); });
}

// ---- 1.11.17 (refonte, lot 0) : le parcours de chaque texte du projet --------------
namespace {

// La valeur d'une action : un script (Executer un script), un texte a trous (Journal),
// sinon une expression (une affectation, des arguments...).
CodeForm actionForm(const Action& a) {
    if (a.operation == Operation::RunScript) return CodeForm::Code;
    return a.operation == Operation::Log ? CodeForm::Template : CodeForm::Expression;
}

// 1.11.18 (refonte, lot 3) : les valeurs des declarations du modele d'un code (initiale,
// constante, par defaut) - des expressions, lues dans le meme lieu que lui.
template <class Decls, class Visit>
void visitDeclarations(Decls& decls, const CodeSite& site, Visit& visit) {
    for (auto& d : decls)
        if (!d.value.empty()) visit(d.value, CodeForm::Expression, site);
}

// Les morceaux qu'en lit rewriteNames (une expression, une accolade d'un texte a trous,
// une plume, un etat, une cellule, un argument...) ; vrai : l'un d'eux a change.
template <class Visit>
bool visitPieces(Object& holder, const CodeSite& site, Visit& visit) {
    bool touched = false;
    rewriteNames(holder, [&](std::string_view t, bool code) {
        std::string text(t);
        visit(text, code ? CodeForm::Code : CodeForm::Expression, site);
        if (text != t) touched = true;
        return text;
    });
    return touched;
}

// Un objet : ses proprietes, ses actions, ses alarmes surchargees et (une instance)
// ses fonctions redefinies. `O` : Object ou const Object ; `view` : la vue qui le porte.
template <class O, class Visit>
void visitObject(const Project& p, O& o, const View* view, const View* scope, Visit& visit) {
    constexpr bool kWrite = !std::is_const_v<O>;
    const std::string at = view->name + "/" + o.name;
    const std::string props = at + " (propri\xC3\xA9t\xC3\xA9s)";
    for (auto& pr : o.props) {
        const CodeSite site{.scope = scope, .view = view, .where = at + " (" + pr.key + ")", .group = props};
        Object holder;
        holder.kind = o.kind;
        holder.props.push_back(pr);
        if (visitPieces(holder, site, visit)) {
            if constexpr (kWrite) pr = std::move(holder.props.front());
        }
        if (o.kind == Kind::InputField && (pr.key == "min" || pr.key == "max")) visit(pr.value, CodeForm::Expression, site);
        visit(pr.value, CodeForm::Template, site);       // un texte a trous ailleurs que dans "text" (un nom de fichier...)
    }
    if (!o.actions.empty()) {
        const CodeSite site{.scope = scope, .view = view, .where = at + " (action)"};
        Object holder;
        holder.actions = o.actions;
        if (visitPieces(holder, site, visit)) {
            if constexpr (kWrite) o.actions = std::move(holder.actions);
        }
        for (auto& a : o.actions) visit(a.value, actionForm(a), site);
    }
    for (auto& ov : o.alarmOverrides) {                   // 1.9 : une instance surcharge une alarme du symbole
        const CodeSite site{.where = at + " (alarme " + ov.alarm + ")"};
        if (ov.condition) visit(*ov.condition, CodeForm::Expression, site);
        if (ov.message) visit(*ov.message, CodeForm::Template, site);
        if (ov.instruction) visit(*ov.instruction, CodeForm::Template, site);
    }
    // 1.11.10 : le corps d'une redefinition est lu dans SON symbole (celui de l'instance).
    const View* own = o.functionOverrides.empty() ? nullptr : symbolOf(p, o);
    for (auto& fo : o.functionOverrides) {
        const CodeSite site{.scope = own, .view = own, .where = at + "." + fo.function + " (red\xC3\xA9" "finition)"};
        visit(fo.body, CodeForm::Code, site);
        visitDeclarations(fo.decls, site, visit);
    }
}

template <class P, class Visit>
void visitProject(P& p, Visit& visit) {
    const auto alarm = [&](auto& a, const std::string& where) {
        const CodeSite site{.where = where};              // une alarme de symbole : pas de qualification (HmiObjectAlarms)
        visit(a.condition, CodeForm::Expression, site);
        visit(a.message, CodeForm::Template, site);
        visit(a.instruction, CodeForm::Template, site);
    };
    const auto ops = [&](auto& list, const std::string& owner) {
        for (auto& o : list) {
            const CodeSite site{.where = "op\xC3\xA9rateur " + operatorSignature(o) + " (" + owner + ")"};
            visit(o.body, CodeForm::Code, site);
            visitDeclarations(o.decls, site, visit);
        }
    };
    for (auto& sc : p.programs.scripts) {
        if (sc.lang != ScriptLang::ST) continue;
        const CodeSite site{.where = "script " + sc.name};
        visit(sc.body, CodeForm::Code, site);
        visit(sc.watch, CodeForm::Expression, site);
        visitDeclarations(sc.decls, site, visit);
    }
    for (auto& f : p.programs.functions) {
        const CodeSite site{.own = &f, .where = "fonction " + f.name};
        visit(f.body, CodeForm::Code, site);
        visitDeclarations(f.decls, site, visit);
    }
    for (auto& t : p.programs.types) ops(t.operators, "type " + t.name);
    for (auto& v : p.views) {
        // Le code d'un symbole, et d'une popup qu'il porte : un appel court y vise d'abord ses fonctions.
        const View* scope = isSymbolView(v) ? &v : popupOwner(p, v);
        for (auto& sc : v.scripts)
            if (sc.lang == ScriptLang::ST) {
                const CodeSite site{.scope = scope, .view = &v, .where = v.name + "." + sc.event};
                visit(sc.body, CodeForm::Code, site);
                visitDeclarations(sc.decls, site, visit);
            }
        if (!v.actions.empty()) {
            const CodeSite site{.scope = scope, .view = &v, .where = v.name + " (action de vue)"};
            Object holder;
            holder.actions = v.actions;
            if (visitPieces(holder, site, visit)) {
                if constexpr (!std::is_const_v<P>) v.actions = std::move(holder.actions);
            }
            for (auto& a : v.actions) visit(a.value, actionForm(a), site);
        }
        for (auto& o : v.objects) visitObject(p, o, &v, scope, visit);
        // Le titre d'une popup et la valeur par defaut d'un parametre ne sont pas qualifies
        // (qualifiedOwnedPopup, symbolArguments) : un appel court y vise une fonction IHM.
        visit(v.popup.title, CodeForm::Template, CodeSite{.where = v.name + " (titre)"});
        for (auto& prm : v.params)
            visit(prm.defaultValue, CodeForm::Expression, CodeSite{.where = v.name + " (param\xC3\xA8tre " + prm.name + ")"});
        for (auto& fn : v.functions) {
            const CodeSite site{.scope = scope, .view = &v, .where = "fonction " + v.name + "." + fn.name};
            visit(fn.body, CodeForm::Code, site);
            visitDeclarations(fn.decls, site, visit);
        }
        ops(v.operators, v.name);
        for (auto& a : v.alarms) alarm(a, "alarme " + v.name + "." + a.name);
    }
    for (auto& a : p.alarms) alarm(a, "alarme " + a.name);
    for (auto& r : p.recipes)
        for (auto& rec : r.records)
            for (auto& val : rec.values) visit(val, CodeForm::Expression, CodeSite{.where = "recette " + r.name});
    for (auto& u : p.security.users)
        if (u.protection == "expression")
            visit(u.expression, CodeForm::Expression, CodeSite{.where = "utilisateur " + u.login + " (autorisation)"});
    for (auto& archived : p.history.archived) visit(archived, CodeForm::Expression, CodeSite{.where = "historique"});
}

// Un appel court de `name` y vise la fonction du symbole, pas la fonction IHM.
bool shadowed(const CodeSite& s, std::string_view name) { return s.scope && symbolFunction(*s.scope, name); }

bool callsIn(const std::string& text, CodeForm form, std::string_view name) {
    if (text.empty()) return false;
    const auto next = form == CodeForm::Template ? renameCallsInText(text, name, "\x01") : renameCalls(text, name, "\x01");
    return next != text;
}

} // namespace

void forEachCode(Project& p, const CodeVisit& visit) { visitProject(p, visit); }
void forEachCode(const Project& p, const ConstCodeVisit& visit) { visitProject(p, visit); }

std::size_t renameFunctionEverywhere(Project& p, std::string_view from, std::string_view to) {
    std::size_t changed = 0;
    const std::string key = upper(from), next = upper(to);
    forEachCode(p, [&](std::string& text, CodeForm form, const CodeSite& s) {
        if (text.empty() || shadowed(s, from)) return;
        const bool bare = s.own && (upper(s.own->name) == key || upper(s.own->name) == next);
        auto out = form == CodeForm::Template ? renameCallsInText(text, from, to) : renameCalls(text, from, to, bare);
        if (out != text) { text = std::move(out); ++changed; }
    });
    return changed;
}

std::vector<std::string> functionCallers(const Project& p, std::string_view name) {
    std::vector<std::string> out;
    std::set<std::string> said;
    const std::string key = upper(name);
    forEachCode(p, [&](const std::string& text, CodeForm form, const CodeSite& s) {
        if ((s.own && upper(s.own->name) == key) || shadowed(s, name)) return;   // pas elle-meme
        const std::string& group = s.group.empty() ? s.where : s.group;
        if (said.count(group) || !callsIn(text, form, name)) return;
        said.insert(group);
        out.push_back(s.where);
    });
    return out;
}

std::vector<std::string> renameCaptures(const Project& p, const View* symbol, std::string_view from, std::string_view to) {
    std::vector<std::string> out;
    std::set<std::string> said;
    forEachCode(p, [&](const std::string& text, CodeForm form, const CodeSite& s) {
        if (!s.scope) return;
        std::string_view call;
        if (!symbol) {
            if (symbolFunction(*s.scope, from) || !symbolFunction(*s.scope, to)) return;
            call = from;
        } else {
            if (s.scope != symbol || symbolFunction(*symbol, to)) return;
            call = to;
        }
        if (callsIn(text, form, call) && said.insert(s.where).second) out.push_back(s.where);
    });
    return out;
}

bool declarationValueReadable(std::string_view type, std::string_view value, std::string* why) {
    // Lue comme le simulateur la lira : une declaration du dialecte, seule.
    const std::string code = "VAR\n    Valeur__ : " + std::string(type) + " := " + std::string(value) + ";\nEND_VAR\n";
    const auto program = sim::parse(code, "valeur", dialectOptions());
    if (program) return true;
    if (why) {
        const std::string raw = program.error().context.empty() ? program.error().message() : program.error().context;
        std::string rest;
        (void)splitLine(raw, &rest);
        *why = frenchSimMessage(rest);
    }
    return false;
}

std::string functionTemplate(std::string_view name, std::string_view returnType, std::string_view description) {
    std::string head = "(* " + std::string(name) + (description.empty() ? std::string{} : " : " + std::string(description)) + " *)\n";
    // 1.11.18 (lot 5) : ses declarations dans le modele (functionTemplateDecls), plus dans le code.
    if (returnType.empty()) return head + "IHM_JOURNAL(Message);\n";
    return head + "Resultat := Entree;\n" + std::string(name) + " := Resultat;\n";
}

std::vector<Declaration> functionTemplateDecls(std::string_view returnType) {
    const auto make = [](DeclKind kind, std::string name, std::string type, std::string doc) {
        Declaration d;
        d.kind = kind;
        d.name = std::move(name);
        d.type = std::move(type);
        d.description = std::move(doc);
        d.visibility = kind == DeclKind::Parameter ? Visibility::Public : Visibility::Private;
        return d;
    };
    if (returnType.empty())
        return {make(DeclKind::Parameter, "Message", "STRING", "un exemple : le texte \xC3\xA9" "crit au journal")};
    const std::string t(returnType);
    return {make(DeclKind::Parameter, "Entree", t, "un exemple : renommez-le, changez son type, ajoutez-en (onglet Param\xC3\xA8tres)"),
            make(DeclKind::Variable, "Resultat", t, "le r\xC3\xA9sultat, avant de le rendre")};
}

std::string functionText(const HmiFunction& f) {
    // 1.11.1 (REP) : le corps sans les $ de ses reperes (ce texte ne sert qu'a l'analyse ; les lignes restent).
    // 1.11.18 (lot 3) : ses declarations du modele reconstruites sur la ligne 1 (decl::codeOf).
    return "FUNCTION " + f.name + (f.returnType.empty() ? std::string{} : " : " + f.returnType) + " " + markers::strip(decl::codeOf(f))
         + "\nEND_FUNCTION\n";
}

std::string functionSignature(const HmiFunction& f) {
    // 1.10 : une fonction aux types riches (VAR_IN_OUT, REF_TO, ARRAY...) : la signature du dialecte.
    if (auto fn = sim::parseFunction(functionText(f), f.name); fn && !sim::functionIsSimple(**fn)) {
        std::string sig = sim::functionSignature(**fn);
        return sig;
    }
    std::string s = f.name + "(";
    const auto parts = splitDeclarations(decl::codeOf(f), true);       // 1.11.18 (lot 3) : ses parametres du modele aussi
    bool first = true;
    for (const auto* in : parts.inputs()) {
        s += (first ? "" : ", ") + in->name + " : " + in->type;
        first = false;
    }
    s += ")";
    if (!f.returnType.empty()) s += " : " + f.returnType;
    return s;
}

std::vector<ScriptDiagnostic> checkFunction(const HmiFunction& source, const TypeKnown& knownType) {
    using E = ScriptDiagnostic::Severity;
    // 1.11.18 (refonte, lot 3) : ses declarations du modele d'abord ; le code se lit avec
    // celles qui sont justes, reconstruites sur sa ligne 1 (les lignes ne bougent pas).
    if (!source.decls.empty()) {
        std::vector<Declaration> valid;
        auto out = decl::checkDeclarations(source.decls, decl::Role::Function, source.body, knownType, nullptr, &valid);
        valid.erase(std::remove_if(valid.begin(), valid.end(), [&](const Declaration& d) {
                        if (upper(d.name) != upper(source.name)) return false;
                        out.push_back({E::Error, 0, "d\xC3\xA9" "claration \xC2\xAB " + d.name
                                                        + " \xC2\xBB : elle ne peut pas porter le nom de la fonction"});
                        return true;
                    }),
                    valid.end());
        HmiFunction flat = source;
        flat.decls = std::move(valid);
        flat.body = decl::codeOf(flat);
        flat.decls.clear();
        for (auto& d : checkFunction(flat, knownType)) out.push_back(std::move(d));
        return out;
    }
    HmiFunction f = source;                // 1.11.1 (REP) : le corps sans les $ de ses reperes (les lignes restent)
    f.body = markers::strip(source.body);
    std::vector<ScriptDiagnostic> out;
    // 1.10 : un type de retour riche (ARRAY, MAP, REF_TO, POINTER TO, un type IHM
    // connu) est permis ; un nom inconnu reste refuse, comme en 1.9.
    if (!f.returnType.empty() && !localTypeSupported(f.returnType) && !richLocalType(f.returnType, knownType)) {
        out.push_back({E::Error, 0, "type de retour non pris en charge : " + f.returnType});
        return out;
    }
    // 1.10 : une fonction aux types riches se lit d'un bloc dans le dialecte (ses
    // VAR_IN_OUT, ses REF_TO, ses tableaux) ; une faute : sa ligne.
    {
        auto fn = sim::parseFunction(functionText(f), f.name);
        if (fn && !sim::functionIsSimple(**fn)) return out;
        if (!fn) {
            const std::string raw = fn.error().context.empty() ? fn.error().message() : fn.error().context;
            const bool rich = f.body.find("VAR_IN_OUT") != std::string::npos || f.body.find("VAR_OUTPUT") != std::string::npos
                           || (!f.returnType.empty() && !localTypeSupported(f.returnType));
            if (rich) {
                std::string rest;
                const int line = splitLine(raw, &rest);
                // 1.11.2 (REP) : la ligne N de functionText est celle du corps (il commence sur la ligne de FUNCTION).
                out.push_back({E::Error, line, markers::explainScriptDollar(frenchSimMessage(rest), source.body, line)});
                return out;
            }
        }
    }
    const auto parts = splitDeclarations(f.body, true);
    out.insert(out.end(), parts.errors.begin(), parts.errors.end());
    if (parts.local(f.name))
        out.push_back({E::Error, parts.local(f.name)->line, "une variable locale ne peut pas porter le nom de la fonction : " + f.name});
    if (parts.body.find_first_not_of(" \t\r\n") == std::string::npos) {
        out.push_back({E::Warning, 0, "fonction sans instruction"});
        return out;
    }
    auto program = sim::parse(parts.body, f.name, dialectOptions());              // 1.10 : le dialecte IHM
    if (!program) {
        const std::string raw = program.error().context.empty() ? program.error().message() : program.error().context;
        std::string rest;
        const int line = splitLine(raw, &rest);
        out.push_back({E::Error, line, markers::explainScriptDollar(frenchSimMessage(rest), source.body, line)});   // 1.11.2 (REP)
        return out;
    }
    // Une fonction qui rend une valeur l'affecte a son nom : Moyenne := ... ;
    if (!f.returnType.empty()) {
        bool assigned = false;
        const std::string u = upper(f.name);
        const std::string code = upper(parts.body);
        for (std::size_t at = code.find(u); at != std::string::npos && !assigned; at = code.find(u, at + u.size())) {
            if (at > 0 && (identChar(code[at - 1]) || code[at - 1] == '.')) continue;
            std::size_t k = at + u.size();
            if (k < code.size() && identChar(code[k])) continue;
            while (k < code.size() && (code[k] == ' ' || code[k] == '\t')) ++k;
            assigned = code.compare(k, 2, ":=") == 0;
        }
        if (!assigned)
            out.push_back({E::Warning, 0, "la fonction ne donne jamais sa valeur (" + f.name + " := ... ;) : elle rendra "
                                              + (f.returnType == "STRING" ? std::string("''") : std::string("0"))});
    }
    return out;
}

bool isHmiFunction(std::string_view name) noexcept {
    static const char* kNames[] = {"IHM_NAVIGUER", "IHM_POPUP", "IHM_FERMER_POPUP", "IHM_JOURNAL", "IHM_APPELER",
                                   "IHM_SON", "IHM_VUE", "IHM_TEMPS",
                                   // lot 8 : les popups intelligentes, l'utilisateur connecte
                                   "IHM_CHANGER_POPUP", "IHM_POPUP_PRECEDENTE", "IHM_CENTRER_POPUP", "IHM_FERMER_POPUPS",
                                   "IHM_POPUP_OUVERTE", "IHM_UTILISATEUR", "IHM_NOM_UTILISATEUR", "IHM_GROUPE", "IHM_NIVEAU",
                                   "IHM_DECONNECTER",
                                   // lot 10 : le menu natif Parametres systeme
                                   "IHM_PARAMETRES_SYSTEME",
                                   // lot 11 : les alarmes mises de cote, le silence, l'export
                                   "IHM_METTRE_DE_COTE", "IHM_REMETTRE", "IHM_FAIRE_TAIRE", "IHM_EXPORTER",
                                   // lot 12 : l'historique de navigation, la vue d'accueil
                                   "IHM_PRECEDENTE", "IHM_SUIVANTE", "IHM_ACCUEIL",
                                   // lot 12 : le menu natif de connexion
                                   "IHM_MENU_CONNEXION",
                                   // lot 13 : la langue, le theme
                                   "IHM_LANGUE", "IHM_THEME",
                                   // lot 15 : les equipements ; lot 16 : les GIF animes
                                   "IHM_EQUIPEMENT_OK", "IHM_EQUIPEMENT_PING",
                                   "IHM_GIF_JOUER", "IHM_GIF_PAUSE", "IHM_GIF_ARRETER", "IHM_GIF_REJOUER",
                                   // 1.9 : l'IHM lit-elle cet equipement sur son esclave simule ?
                                   "IHM_ESCLAVE_SIMULE",
                                   // 1.11.14 : une ligne de la Console, d'un niveau
                                   "IHM_LOG"};
    const std::string u = upper(name);
    for (const char* n : kNames) if (u == n) return true;
    return false;
}

} // namespace hmi
