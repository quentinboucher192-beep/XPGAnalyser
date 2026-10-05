// project/MacroSpecWriter.cpp - ecrire les lignes "#!" d'une macro (lot API 6).
#include "MacroSpecWriter.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <utility>

namespace project::macro {

namespace {

struct Line {
    std::string text;
    std::string eol;
};

std::vector<Line> splitLines(std::string_view s) {
    std::vector<Line> out;
    std::size_t from = 0;
    while (from < s.size()) {
        const auto nl = s.find('\n', from);
        if (nl == std::string_view::npos) {
            out.push_back({std::string(s.substr(from)), {}});
            break;
        }
        std::size_t end = nl;
        std::string eol = "\n";
        if (end > from && s[end - 1] == '\r') {
            --end;
            eol = "\r\n";
        }
        out.push_back({std::string(s.substr(from, end - from)), eol});
        from = nl + 1;
    }
    return out;
}

std::string joinLines(const std::vector<Line>& lines) {
    std::string out;
    for (const auto& l : lines) out += l.text + l.eol;
    return out;
}

std::string eolOf(const std::vector<Line>& lines) {
    for (const auto& l : lines)
        if (!l.eol.empty()) return l.eol;
    return "\n";
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool sameKey(std::string_view a, std::string_view b) { return lower(a) == lower(b); }

std::string indentOf(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
    return text.substr(0, i);
}

// "#! param Fbk = le retour" -> mot "param", cle "Fbk", valeur "le retour".
bool parseHeader(const std::string& text, std::string& word, std::string& key, std::string& value) {
    const auto t = trim(text);
    if (t.size() < 2 || t.compare(0, 2, "#!") != 0) return false;
    const auto rest = trim(std::string_view(t).substr(2));
    const auto eq = rest.find('=');
    if (eq == std::string::npos) return false;
    const auto spec = trim(std::string_view(rest).substr(0, eq));
    value = trim(std::string_view(rest).substr(eq + 1));
    if (spec.empty()) return false;
    const auto space = spec.find_first_of(" \t");
    if (space == std::string::npos) {
        word = spec;
        key.clear();
    } else {
        word = spec.substr(0, space);
        key = trim(std::string_view(spec).substr(space + 1));
    }
    word = lower(foldAccents(word));
    return true;
}

std::vector<std::string> splitCommas(std::string_view s) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= s.size()) {
        const auto c = s.find(',', from);
        const auto part = trim(s.substr(from, (c == std::string_view::npos ? s.size() : c) - from));
        if (!part.empty()) out.push_back(part);
        if (c == std::string_view::npos) break;
        from = c + 1;
    }
    return out;
}

std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + v[i];
    return out;
}

// L'en-tete : de la ligne qui ouvre "(*" (rien que des blancs avant) a celle
// qui le ferme. [open, close] ; npos : pas d'en-tete.
std::pair<std::size_t, std::size_t> headerRange(const std::vector<Line>& lines) {
    constexpr auto npos = static_cast<std::size_t>(-1);
    std::size_t open = npos;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto t = trim(lines[i].text);
        if (t.empty()) continue;
        if (t.compare(0, 2, "(*") == 0) open = i;
        break;
    }
    if (open == npos) return {npos, npos};
    for (std::size_t i = open; i < lines.size(); ++i) {
        const auto& t = lines[i].text;
        const auto from = i == open ? t.find("(*") + 2 : 0;
        if (t.find("*)", from) != std::string::npos) return {open, i};
    }
    return {npos, npos};
}

// Le texte d'une ligne "#!" a garder jusqu'au '=' compris (retrait, mot, cle
// tels qu'ecrits), puis la nouvelle valeur.
std::string withValue(const std::string& text, const std::string& value) {
    const auto eq = text.find('=');
    if (eq == std::string::npos) return text;
    return text.substr(0, eq + 1) + " " + value;
}

// Applique : des lignes retirees, remplacees, et des lignes ajoutees apres une autre.
struct Edit {
    std::set<std::size_t>                                   drop;
    std::map<std::size_t, std::string>                      replace;
    std::vector<std::pair<std::size_t, std::string>>        after;   // (ligne, texte) dans l'ordre
};
std::string applyEdit(const std::vector<Line>& lines, const Edit& e) {
    const auto eol = eolOf(lines);
    std::vector<Line> out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!e.drop.count(i)) {
            Line l = lines[i];
            if (const auto it = e.replace.find(i); it != e.replace.end()) l.text = it->second;
            if (l.eol.empty() && std::any_of(e.after.begin(), e.after.end(), [&](const auto& a) { return a.first == i; })) l.eol = eol;
            out.push_back(std::move(l));
        }
        for (const auto& [at, text] : e.after)
            if (at == i) out.push_back({text, eol});
    }
    // Une derniere ligne sans fin de ligne le reste.
    if (!out.empty() && !lines.empty() && lines.back().eol.empty() && out.back().eol == eol
        && std::none_of(e.after.begin(), e.after.end(), [&](const auto& a) { return a.first == lines.size() - 1; }))
        out.back().eol.clear();
    return joinLines(out);
}

bool isAskLine(const std::string& text, const std::string& key) {
    const auto l = lower(text);
    std::size_t at = 0;
    while ((at = l.find("ask", at)) != std::string::npos) {
        std::size_t p = at + 3;
        while (p < l.size() && (std::isalpha(static_cast<unsigned char>(l[p])))) ++p;   // AskChoice, AskNumber...
        while (p < l.size() && std::isspace(static_cast<unsigned char>(l[p]))) ++p;
        if (p < l.size() && l[p] == '(') {
            ++p;
            while (p < l.size() && std::isspace(static_cast<unsigned char>(l[p]))) ++p;
            if (p < l.size() && l[p] == '\'') {
                const auto end = l.find('\'', p + 1);
                if (end != std::string::npos && l.substr(p + 1, end - p - 1) == lower(key)) return true;
            }
        }
        at += 3;
    }
    return false;
}

bool hasAsk(const std::string& text) {
    const auto l = lower(text);
    const auto at = l.find("ask(");
    return at != std::string::npos || l.find("askchoice(") != std::string::npos || l.find("asknumber(") != std::string::npos
        || l.find("askyesno(") != std::string::npos;
}

} // namespace

std::vector<HeaderLine> headerLines(std::string_view source) {
    std::vector<HeaderLine> out;
    const auto lines = splitLines(source);
    const auto [open, close] = headerRange(lines);
    if (open == static_cast<std::size_t>(-1)) return out;
    for (std::size_t i = open; i <= close && i < lines.size(); ++i) {
        HeaderLine h;
        if (!parseHeader(lines[i].text, h.word, h.key, h.value)) continue;
        h.line = i;
        out.push_back(std::move(h));
    }
    return out;
}

std::vector<std::size_t> linesOf(std::string_view source, const std::string& key) {
    std::set<std::size_t> out;
    const auto lines = splitLines(source);
    const auto [open, close] = headerRange(lines);
    for (const auto& h : headerLines(source)) {
        if (h.word == "champ" || h.word == "libelle" || h.word == "label" || h.word == "param" || h.word == "exemple") {
            if (sameKey(h.key, key)) out.insert(h.line);
        } else if (h.word == "option") {
            const auto space = h.key.find_first_of(" \t");
            if (sameKey(h.key.substr(0, space), key)) out.insert(h.line);
        } else if ((h.word == "groupe" && !h.key.empty()) || (h.word == "avance" && h.key.empty())) {
            for (const auto& k : splitCommas(h.value))
                if (sameKey(k, key)) out.insert(h.line);
        }
    }
    const std::size_t codeFrom = close == static_cast<std::size_t>(-1) ? 0 : close + 1;
    for (std::size_t i = codeFrom; i < lines.size(); ++i)
        if (isAskLine(lines[i].text, key)) out.insert(i);
    (void)open;
    return {out.begin(), out.end()};
}

std::string writeGroups(std::string_view source, const std::vector<GroupSpec>& groups, const std::vector<std::string>& advanced) {
    const auto lines = splitLines(source);
    const auto [open, close] = headerRange(lines);
    if (open == static_cast<std::size_t>(-1)) return std::string(source);
    // Ce qui est ecrit aujourd'hui.
    std::vector<std::pair<std::string, std::vector<std::size_t>>> existing;   // nom -> lignes
    std::vector<std::size_t> avance;
    std::size_t lastGroup = static_cast<std::size_t>(-1), lastField = static_cast<std::size_t>(-1);
    for (const auto& h : headerLines(source)) {
        if (h.word == "groupe" && !h.key.empty()) {
            auto it = std::find_if(existing.begin(), existing.end(), [&](const auto& e) { return sameKey(e.first, h.key); });
            if (it == existing.end()) existing.push_back({h.key, {h.line}});
            else it->second.push_back(h.line);
            lastGroup = h.line;
        } else if (h.word == "avance" && h.key.empty()) {
            avance.push_back(h.line);
        } else if (h.word == "champ" || h.word == "libelle" || h.word == "label" || h.word == "param") {
            lastField = h.line;
        }
    }
    const auto keysOf = [&](const std::vector<std::size_t>& at) {
        std::vector<std::string> keys;
        for (const auto i : at) {
            std::string w, k, v;
            if (parseHeader(lines[i].text, w, k, v))
                for (auto& part : splitCommas(v)) keys.push_back(std::move(part));
        }
        return keys;
    };
    Edit e;
    std::size_t anchor = lastGroup != static_cast<std::size_t>(-1) ? lastGroup
                       : !avance.empty() ? avance.front() - (avance.front() > open ? 1 : 0)
                       : lastField != static_cast<std::size_t>(-1) ? lastField
                       : (close > open ? close - 1 : open);
    const std::string indent = indentOf(lines[anchor == open ? std::min(open + 1, close) : anchor].text);
    for (const auto& g : groups) {
        if (g.keys.empty()) continue;
        const auto it = std::find_if(existing.begin(), existing.end(), [&](const auto& x) { return sameKey(x.first, g.name); });
        if (it == existing.end()) {
            e.after.push_back({anchor, indent + "#! groupe " + g.name + " = " + join(g.keys)});
            continue;
        }
        if (keysOf(it->second) == g.keys) continue;       // tel quel, a l'octet pres
        e.replace[it->second.front()] = withValue(lines[it->second.front()].text, join(g.keys));
        for (std::size_t k = 1; k < it->second.size(); ++k) e.drop.insert(it->second[k]);
    }
    for (const auto& [name, at] : existing) {
        const bool wanted = std::any_of(groups.begin(), groups.end(), [&](const GroupSpec& g) { return sameKey(g.name, name) && !g.keys.empty(); });
        if (!wanted) for (const auto i : at) e.drop.insert(i);
    }
    if (keysOf(avance) != advanced) {
        if (advanced.empty()) {
            for (const auto i : avance) e.drop.insert(i);
        } else if (avance.empty()) {
            e.after.push_back({anchor, indent + "#! avance = " + join(advanced)});
        } else {
            e.replace[avance.front()] = withValue(lines[avance.front()].text, join(advanced));
            for (std::size_t k = 1; k < avance.size(); ++k) e.drop.insert(avance[k]);
        }
    }
    if (e.drop.empty() && e.replace.empty() && e.after.empty()) return std::string(source);
    return applyEdit(lines, e);
}

std::string moveField(std::string_view source, const std::string& key, const std::string& group, std::size_t index) {
    const auto spec = parseMacroSpec(source);
    auto groups = spec.groups;
    for (auto& g : groups)
        g.keys.erase(std::remove_if(g.keys.begin(), g.keys.end(), [&](const std::string& k) { return sameKey(k, key); }), g.keys.end());
    if (!group.empty()) {
        auto it = std::find_if(groups.begin(), groups.end(), [&](const GroupSpec& g) { return sameKey(g.name, group); });
        if (it == groups.end()) {
            groups.push_back({group, {}});
            it = groups.end() - 1;
        }
        const auto at = std::min(index, it->keys.size());
        it->keys.insert(it->keys.begin() + static_cast<std::ptrdiff_t>(at), key);
    }
    return writeGroups(source, groups, spec.advanced);
}

std::string setAdvanced(std::string_view source, const std::string& key, bool advanced) {
    const auto spec = parseMacroSpec(source);
    auto adv = spec.advanced;
    const auto it = std::find_if(adv.begin(), adv.end(), [&](const std::string& k) { return sameKey(k, key); });
    if (advanced && it == adv.end()) adv.push_back(key);
    if (!advanced && it != adv.end()) adv.erase(it);
    return writeGroups(source, spec.groups, adv);
}

std::string setKeyLine(std::string_view source, const std::string& word, const std::string& key, const std::string& value) {
    const auto lines = splitLines(source);
    const auto [open, close] = headerRange(lines);
    if (open == static_cast<std::size_t>(-1)) return std::string(source);
    const auto want = lower(foldAccents(word));
    std::vector<std::size_t> found;
    std::size_t lastSameKey = static_cast<std::size_t>(-1), lastChamp = static_cast<std::size_t>(-1);
    for (const auto& h : headerLines(source)) {
        if (h.word == want && sameKey(h.key, key)) found.push_back(h.line);
        if (sameKey(h.key, key) && !h.key.empty()) lastSameKey = h.line;
        if (h.word == "champ" || h.word == "libelle") lastChamp = h.line;
    }
    Edit e;
    if (!found.empty()) {
        std::string w, k, v;
        (void)parseHeader(lines[found.front()].text, w, k, v);
        if (value.empty()) {
            for (const auto i : found) e.drop.insert(i);
        } else {
            if (v == value) return std::string(source);
            e.replace[found.front()] = withValue(lines[found.front()].text, value);
        }
        return applyEdit(lines, e);
    }
    if (value.empty()) return std::string(source);
    const auto anchor = lastSameKey != static_cast<std::size_t>(-1) ? lastSameKey
                      : lastChamp != static_cast<std::size_t>(-1) ? lastChamp
                      : (close > open ? close - 1 : open);
    const auto indent = indentOf(lines[anchor == open ? std::min(open + 1, close) : anchor].text);
    e.after.push_back({anchor, indent + "#! " + word + " " + key + " = " + value});
    return applyEdit(lines, e);
}

std::string setHelp(std::string_view source, const std::string& key, const std::string& help) {
    return setKeyLine(source, "param", key, help);
}

std::string addField(std::string_view source, const std::string& key, const std::string& kind, const std::string& label,
                     const std::string& preset) {
    for (const auto& h : headerLines(source))
        if (h.word == "champ" && sameKey(h.key, key)) return std::string(source);
    const auto lines = splitLines(source);
    const auto [open, close] = headerRange(lines);
    Edit e;
    if (open != static_cast<std::size_t>(-1)) {
        std::size_t anchor = close > open ? close - 1 : open;
        for (const auto& h : headerLines(source))
            if (h.word == "champ" || h.word == "libelle") anchor = h.line;
        const auto indent = indentOf(lines[anchor == open ? std::min(open + 1, close) : anchor].text);
        e.after.push_back({anchor, indent + "#! champ " + key + " = " + (kind.empty() ? std::string("texte") : kind)});
        if (!label.empty()) e.after.push_back({anchor, indent + "#! libelle " + key + " = " + label});
    }
    const std::size_t codeFrom = close == static_cast<std::size_t>(-1) ? 0 : close + 1;
    std::size_t lastAsk = static_cast<std::size_t>(-1);
    for (std::size_t i = codeFrom; i < lines.size(); ++i)
        if (hasAsk(lines[i].text)) lastAsk = i;
    const std::string call = key + " := Ask('" + key + "', '" + stLiteral(label.empty() ? key : label) + "', '" + stLiteral(preset) + "');";
    if (lastAsk != static_cast<std::size_t>(-1)) e.after.push_back({lastAsk, indentOf(lines[lastAsk].text) + call});
    else if (close != static_cast<std::size_t>(-1)) e.after.push_back({close, call});
    return applyEdit(lines, e);
}

std::string bumpVersion(std::string_view source, const std::string& version, const std::string& changes) {
    const auto lines = splitLines(source);
    const auto [open, close] = headerRange(lines);
    if (open == static_cast<std::size_t>(-1)) return std::string(source);
    std::size_t versionLine = static_cast<std::size_t>(-1), summaryLine = static_cast<std::size_t>(-1), firstChanges = static_cast<std::size_t>(-1),
                sameChanges = static_cast<std::size_t>(-1);
    for (const auto& h : headerLines(source)) {
        if (h.word == "version" && h.key.empty() && versionLine == static_cast<std::size_t>(-1)) versionLine = h.line;
        if (h.word == "summary" && h.key.empty()) summaryLine = h.line;
        if (h.word == "changes") {
            if (firstChanges == static_cast<std::size_t>(-1)) firstChanges = h.line;
            if (h.key == version) sameChanges = h.line;
        }
    }
    Edit e;
    const auto anchorIndent = [&](std::size_t at) { return indentOf(lines[at == open ? std::min(open + 1, close) : at].text); };
    std::string indent = anchorIndent(versionLine != static_cast<std::size_t>(-1) ? versionLine : summaryLine != static_cast<std::size_t>(-1) ? summaryLine : open);
    if (versionLine != static_cast<std::size_t>(-1)) {
        e.replace[versionLine] = withValue(lines[versionLine].text, version);
    } else {
        versionLine = summaryLine != static_cast<std::size_t>(-1) ? summaryLine : open;
        e.after.push_back({versionLine, indent + "#! version = " + version});
    }
    if (!changes.empty()) {
        const std::string line = indent + "#! changes " + version + " = " + changes;
        if (sameChanges != static_cast<std::size_t>(-1)) e.replace[sameChanges] = withValue(lines[sameChanges].text, changes);
        else if (firstChanges != static_cast<std::size_t>(-1) && firstChanges > 0) e.after.push_back({firstChanges - 1, line});
        else e.after.push_back({versionLine, line});
    }
    return applyEdit(lines, e);
}

std::string nextVersion(std::string_view version) {
    const auto v = trim(version);
    if (v.empty()) return "1.0";
    const auto dot = v.rfind('.');
    const std::string head = dot == std::string::npos ? std::string{} : v.substr(0, dot + 1);
    const std::string last = dot == std::string::npos ? v : v.substr(dot + 1);
    if (last.empty() || !std::all_of(last.begin(), last.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return v + ".1";
    if (dot == std::string::npos) return v + ".1";
    return head + std::to_string(std::stoll(last) + 1);
}

std::string stLiteral(std::string_view text) {
    const auto plain = foldAccents(text);
    std::string out;
    for (const char c : plain) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '\'') out += "$'";
        else if (c == '$') out += "$$";
        else if (u >= 32 && u < 127) out += c;
        else if (c == '\n') out += "$N";
    }
    return out;
}

} // namespace project::macro
