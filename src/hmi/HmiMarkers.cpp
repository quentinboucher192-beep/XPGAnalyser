// =============================================================================
//  hmi/HmiMarkers.cpp - le lecteur des reperes `$...$` (1.11, chantier REP)
// -----------------------------------------------------------------------------
//  Voir HmiMarkers.hpp. Un seul parcours (walk) sert a trouver les reperes et a
//  les retirer : ce que Dupliquer... voit et ce que l'analyse lit sont toujours
//  les memes `$`.
// =============================================================================
#include "HmiMarkers.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::markers {
namespace {
// Le bout d'une chaine ST ouverte en `at` ('...' ou "...") : apres son
// guillemet fermant ; `$` y echappe le caractere suivant. Sans fin sur la
// ligne : la fin de la ligne.
std::size_t stringEnd(std::string_view t, std::size_t at) {
    const char q = t[at];
    std::size_t j = at + 1;
    while (j < t.size() && t[j] != q && t[j] != '\n') j += (t[j] == '$' && j + 1 < t.size() && t[j + 1] != '\n') ? 2 : 1;
    return j < t.size() && t[j] == q ? j + 1 : j;
}
// Le contenu d'un repere : validContent, mais dans une expression, un `$` d'une
// chaine du morceau ($Msg = 'a$$b'$) est permis.
bool validSpan(std::string_view s, bool inExpr) {
    // R111 (REP-3) : dans un texte (hors des trous), un $ legitime ("10$, 20$", "5$/h",
    // "\\SRV\\D$\\", "?a=$x&b=$y") n'ouvre pas un repere : il commence par une lettre, _ ou [
    // et finit par une lettre, un chiffre, _, ] ou ).
    if (!inExpr && !s.empty()) {
        const auto head = static_cast<unsigned char>(s.front()), tail = static_cast<unsigned char>(s.back());
        const bool headOk = std::isalpha(head) || head == '_' || head == '[' || head >= 0x80;
        const bool tailOk = std::isalnum(tail) || tail == '_' || tail == ']' || tail == ')' || tail >= 0x80;
        if (!headOk || !tailOk) return false;
    }
    if (!inExpr || s.find('$') == std::string_view::npos) return validContent(s);
    std::string bare;
    for (std::size_t j = 0; j < s.size();) {
        if (s[j] == '\'' || s[j] == '"') {
            j = stringEnd(s, j);
            bare += "''";
            continue;
        }
        bare += s[j++];
    }
    return validContent(bare) && s.front() == bare.front() && s.back() == bare.back();
}
// Le parcours commun : `onMarker(span)` pour chaque repere, `onDollars(at,
// dansUneExpression)` pour chaque `$$` hors chaine (la ou un repere pourrait s'ouvrir),
// `onLone(at)` (1.11.1, REP-5) pour chaque `$` reste seul hors chaine et commentaire.
template <class M, class D, class L>
void walk(std::string_view t, Mode mode, M&& onMarker, D&& onDollars, L&& onLone) {
    bool hole = false;   // Mode::Text : dans un trou {...} (le meme decoupage que TextTemplate)
    for (std::size_t i = 0; i < t.size();) {
        const char c = t[i];
        if (mode == Mode::Text) {
            if (!hole && (c == '{' || c == '}') && i + 1 < t.size() && t[i + 1] == c) { i += 2; continue; }   // {{ }}
            if (!hole && c == '{' && t.find('}', i + 1) != std::string_view::npos) { hole = true; ++i; continue; }
            if (hole && c == '}') { hole = false; ++i; continue; }
        }
        const bool inExpr = mode == Mode::Expression || hole;
        if (inExpr && (c == '\'' || c == '"')) { i = stringEnd(t, i); continue; }
        if (inExpr && c == '(' && i + 1 < t.size() && t[i + 1] == '*') {
            const auto end = t.find("*)", i + 2);
            i = end == std::string_view::npos ? t.size() : end + 2;
            continue;
        }
        if (mode == Mode::Expression && c == '/' && i + 1 < t.size() && t[i + 1] == '/') { i = std::min(t.size(), t.find('\n', i)); continue; }   // 1.11 (REP-8) : // jusqu'a la fin de la ligne
        if (c != '$') { ++i; continue; }
        if (i + 1 < t.size() && t[i + 1] == '$') { onDollars(i, inExpr); i += 2; continue; }
        // Le `$` qui ferme, sur la meme ligne (une chaine du morceau ne compte pas). 1.11.1 : un commentaire
        // l'arrete - un repere ne prend jamais le `$` d'un commentaire (`x := $V[1];(*$Ab*)` : le `$` de fin
        // manque, le commentaire garde le sien).
        std::size_t j = i + 1;
        while (j < t.size() && t[j] != '$' && t[j] != '\n') {
            if (inExpr && (t[j] == '\'' || t[j] == '"')) j = stringEnd(t, j);
            else if (inExpr && t[j] == '(' && j + 1 < t.size() && t[j + 1] == '*') break;
            else if (mode == Mode::Expression && t[j] == '/' && j + 1 < t.size() && t[j + 1] == '/') break;
            else ++j;
        }
        if (j < t.size() && t[j] == '$' && validSpan(t.substr(i + 1, j - i - 1), inExpr)) {
            onMarker(Span{i, j - i + 1});
            i = j + 1;
            continue;
        }
        onLone(i);
        ++i;
    }
}
bool operandChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '[' || c == ']'; }
} // namespace

namespace {
// REP-5 : le texte probable, avec un `$` de plus, pour le `$` seul en `at` ; `holds(candidat)` dit s'il est
// juste. `from` : le debut de la ligne de `at` (1.11.2 : un repere d'une ligne d'avant ne compte pas ; 0 pour
// une expression, comme en 1.11.1). Vide : rien de sur.
template <class H>
std::string repairAt(std::string_view t, Mode mode, std::size_t at, std::size_t from, H&& holds) {
    const auto insert = [&](std::size_t where) { return std::string(t.substr(0, where)) + "$" + std::string(t.substr(where)); };
    // a) Un repere a pris le `$` d'un autre : `$V[1].Ouv+$V[2].Ouv$` (le repere `V[1].Ouv+`) -> un `$`
    //    apres l'operande du repere : `$V[1].Ouv$+$V[2].Ouv$`.
    // (Une copie : `find` rend un vecteur temporaire, detruit apres la boucle.)
    std::optional<Span> before;
    for (const auto& s : find(t, mode))
        if (s.at >= from && s.end() <= at) before = s;
    if (before) {
        const auto content = before->content(t);
        std::size_t k = content.size();
        while (k > 0 && !operandChar(content[k - 1])) --k;
        if (k > 0 && k < content.size())
            if (auto fix = insert(before->at + 1 + k); holds(fix)) return fix;
    }
    // b) Le `$` ouvre un operande sans le fermer : `$V[1].Ouv + 2` -> `$V[1].Ouv$ + 2`.
    if (at + 1 < t.size() && (std::isalpha(static_cast<unsigned char>(t[at + 1])) || t[at + 1] == '_')) {
        std::size_t e = at + 1;
        while (e < t.size() && operandChar(t[e])) ++e;
        if (auto fix = insert(e); holds(fix)) return fix;
    }
    // c) Le `$` ferme un operande sans ouverture : `V[1].Ouv$ + 2` -> `$V[1].Ouv$ + 2`.
    if (at > 0 && operandChar(t[at - 1])) {
        std::size_t b = at;
        while (b > 0 && operandChar(t[b - 1])) --b;
        if (std::isalpha(static_cast<unsigned char>(t[b])) || t[b] == '_')
            if (auto fix = insert(b); holds(fix)) return fix;
    }
    return {};
}
// La faute d'un `$` de plus dans `fix` (le texte juste de `t`), et `shown`, le texte a ecrire.
std::string adviceFor(std::string_view t, const std::string& fix, Mode mode, const std::string& shown) {
    std::size_t p = 0;   // le `$` ajoute
    while (p < t.size() && t[p] == fix[p]) ++p;
    for (const auto& s : find(fix, mode)) {
        if (p == s.at)
            return "il manque le $ d'ouverture de " + std::string(fix.substr(p + 1, s.end() - p - 1)) + " : \xC3\xA9" "cris " + shown;
        if (p + 1 == s.end())
            return "il manque le $ de fin de " + std::string(fix.substr(s.at, p - s.at)) + " : \xC3\xA9" "cris " + shown;
    }
    return "il manque sans doute un $ : \xC3\xA9" "cris " + shown;
}
} // namespace

bool validContent(std::string_view s) noexcept {
    // 1.11.24 : une lettre seule ($V$, $I$ : un tableau, un indice d'une lettre) ; sinon deux
    // caracteres au moins. Avant, $V$ etait refuse : "un repere s'ecrit entre deux $".
    if (s.size() == 1) return std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_';
    if (s.size() < 2) return false;
    const auto blank = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    if (blank(s.front()) || blank(s.back()) || std::isdigit(static_cast<unsigned char>(s.front()))) return false;
    return s.find('$') == std::string_view::npos && s.find('\n') == std::string_view::npos;
}

std::vector<Span> find(std::string_view text, Mode mode) {
    std::vector<Span> out;
    if (text.find('$') == std::string_view::npos) return out;
    walk(text, mode, [&](Span s) { out.push_back(s); }, [](std::size_t, bool) {}, [](std::size_t) {});
    return out;
}

Stripped stripKeep(std::string_view text, Mode mode) {
    Stripped out;
    if (text.find('$') == std::string_view::npos) {
        out.text = std::string(text);
        return out;
    }
    auto& drop = out.removed;   // les `$` a retirer, dans l'ordre
    walk(text, mode,
         [&](Span s) { drop.push_back(s.at); drop.push_back(s.end() - 1); },
         [&](std::size_t at, bool inExpr) { if (!inExpr) drop.push_back(at); },   // un texte : `$$` -> `$`
         [](std::size_t) {});
    if (drop.empty()) {
        out.text = std::string(text);
        return out;
    }
    out.text.reserve(text.size());
    std::size_t k = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (k < drop.size() && drop[k] == i) { ++k; continue; }
        out.text += text[i];
    }
    return out;
}

std::string strip(std::string_view text, Mode mode) { return stripKeep(text, mode).text; }

std::vector<std::size_t> lone(std::string_view text, Mode mode) {
    std::vector<std::size_t> out;
    if (text.find('$') == std::string_view::npos) return out;
    walk(text, mode, [](Span) {}, [](std::size_t, bool) {}, [&](std::size_t at) { out.push_back(at); });
    return out;
}

std::string repairPairing(std::string_view t, Mode mode) {
    const auto alone = lone(t, mode);
    if (alone.size() != 1) return {};
    return repairAt(t, mode, alone.front(), 0, [&](const std::string& candidate) { return lone(candidate, mode).empty(); });
}

std::string pairingAdvice(std::string_view t, Mode mode) {
    const std::string fix = repairPairing(t, mode);
    if (fix.empty()) return {};
    return adviceFor(t, fix, mode, fix);
}

std::string_view pairingRule() noexcept {
    return " \xE2\x80\x94 un rep\xC3\xA8re s'\xC3\xA9" "crit entre deux $ ($V[1].Ouv$) ; un vrai $ s'\xC3\xA9" "crit $$ dans une cha\xC3\xAEne";
}

std::string lineAdvice(std::string_view t, int line, Mode mode) {
    if (line < 1 || t.find('$') == std::string_view::npos) return {};
    std::size_t from = 0;   // la ligne : [from, to)
    for (int l = 1; l < line; ++l) {
        const auto nl = t.find('\n', from);
        if (nl == std::string_view::npos) return {};
        from = nl + 1;
    }
    const auto nl = t.find('\n', from);
    const std::size_t to = nl == std::string_view::npos ? t.size() : nl;
    // Les `$` seuls, lus dans tout le texte (un commentaire ouvert plus haut garde les siens).
    std::vector<std::size_t> here, elsewhere;
    for (const std::size_t a : lone(t, mode)) (a >= from && a < to ? here : elsewhere).push_back(a);
    if (here.size() != 1) return {};
    // Juste : la ligne n'a plus de `$` seul, et ceux des autres lignes restent les memes (le `$` ajoute ne
    // change que la ligne ; ceux d'apres reculent d'un octet).
    std::vector<std::size_t> expected;
    expected.reserve(elsewhere.size());
    for (const std::size_t a : elsewhere) expected.push_back(a < from ? a : a + 1);
    const std::string fix = repairAt(t, mode, here.front(), from,
                                     [&](const std::string& candidate) { return lone(candidate, mode) == expected; });
    if (fix.empty()) return {};
    // La ligne a ecrire (un octet de plus), sans ses blancs de bord.
    std::string shown = fix.substr(from, to + 1 - from);
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (!shown.empty() && blank(shown.back())) shown.pop_back();
    std::size_t lead = 0;
    while (lead < shown.size() && blank(shown[lead])) ++lead;
    shown.erase(0, lead);
    return adviceFor(t, fix, mode, shown);
}

std::string explainScriptDollar(std::string message, std::string_view text, int line) {
    if (message.find("'$'") == std::string::npos) return message;
    if (auto advice = lineAdvice(text, line); !advice.empty()) message = std::move(advice);
    message += pairingRule();
    return message;
}

int Stripped::column(std::string_view original, int line, int col) const {
    if (removed.empty() || line < 1 || col < 1) return col;
    std::size_t start = 0;
    for (int l = 1; l < line; ++l) {
        const auto nl = original.find('\n', start);
        if (nl == std::string_view::npos) return col;
        start = nl + 1;
    }
    const auto nl = original.find('\n', start);
    const std::size_t end = nl == std::string_view::npos ? original.size() : nl;
    // Chaque `$` retire de la ligne, avant la colonne cherchee, la pousse d'un cran.
    std::size_t at = static_cast<std::size_t>(col);
    for (const std::size_t r : removed) {
        if (r < start) continue;
        if (r >= end) break;
        if (r - start + 1 <= at) ++at;
    }
    return static_cast<int>(at);
}
} // namespace hmi::markers
