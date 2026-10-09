// =============================================================================
//  hmi/HmiScriptFile.cpp - voir HmiScriptFile.hpp
// =============================================================================
#include "HmiScriptFile.hpp"

#include "HmiOperators.hpp"
#include "../core/Version.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>

namespace hmi::scriptfile {

namespace {

unsigned char uc(char c) { return static_cast<unsigned char>(c); }

bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(uc(a[i])) != std::toupper(uc(b[i]))) return false;
    return true;
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(uc(s[a]))) ++a;
    while (b > a && std::isspace(uc(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// Le code sans les blancs de fin (le fichier et la vue se comparent ainsi).
std::string normalBody(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s)
        if (c != '\r') out += c;
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\t')) out.pop_back();
    return out;
}

std::size_t lineCount(std::string_view s) {
    const std::string b = normalBody(s);
    if (b.empty()) return 0;
    return static_cast<std::size_t>(std::count(b.begin(), b.end(), '\n')) + 1;
}

// Une valeur d'un bloc : sans guillemets si elle n'a ni blanc, ni guillemet, ni *).
std::string quote(std::string_view v) {
    bool plain = !v.empty();
    for (const char c : v)
        if (std::isspace(uc(c)) || c == '"' || c == '=' || c == '*' || c == '(' || c == ')') plain = false;
    if (plain) return std::string(v);
    std::string out = "\"";
    for (const char c : v) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        if (c == '\r') continue;
        out += c;
    }
    return out + "\"";
}

// "(*# script evenement=OnOpen langage=ST *)" -> {"", "script"} {"evenement", "OnOpen"}...
bool parseBlock(std::string_view line, std::string& word, std::map<std::string, std::string>& kv) {
    std::string t = trimmed(line);
    if (t.size() < 7 || t.rfind("(*#", 0) != 0 || t.substr(t.size() - 2) != "*)") return false;
    t = t.substr(3, t.size() - 5);
    std::size_t i = 0;
    const auto skip = [&] { while (i < t.size() && std::isspace(uc(t[i]))) ++i; };
    skip();
    while (i < t.size() && !std::isspace(uc(t[i]))) word += t[i++];
    kv.clear();
    while (true) {
        skip();
        if (i >= t.size()) break;
        std::string key;
        while (i < t.size() && t[i] != '=' && !std::isspace(uc(t[i]))) key += t[i++];
        if (i >= t.size() || t[i] != '=') return false;
        ++i;
        std::string value;
        if (i < t.size() && t[i] == '"') {
            ++i;
            bool closed = false;
            while (i < t.size()) {
                const char c = t[i++];
                if (c == '\\' && i < t.size()) {
                    const char n = t[i++];
                    value += n == 'n' ? '\n' : n;
                    continue;
                }
                if (c == '"') { closed = true; break; }
                value += c;
            }
            if (!closed) return false;
        } else {
            while (i < t.size() && !std::isspace(uc(t[i]))) value += t[i++];
        }
        std::string lower;
        for (const char c : key) lower += static_cast<char>(std::tolower(uc(c)));
        kv[lower] = value;
    }
    return !word.empty();
}

const std::string& at(const std::map<std::string, std::string>& kv, const char* key) {
    static const std::string none;
    const auto it = kv.find(key);
    return it == kv.end() ? none : it->second;
}

bool knownEvent(std::string_view e, std::string& canonical) {
    for (const auto ev : kViewEvents)
        if (sameName(ev, e)) { canonical = std::string(ev); return true; }
    return false;
}

const Script* scriptOf(const View& v, std::string_view event) {
    for (const auto& s : v.scripts)
        if (sameName(s.event, event)) return &s;
    return nullptr;
}

bool sameSignature(const HmiOperator& a, const HmiOperator& b) {
    return a.op == b.op && sameName(a.left, b.left) && sameName(a.right, b.right) && (a.op != "TO" || sameName(a.result, b.result));
}

// 1.11.18 (lot 3) : deux listes de declarations du modele egales, a leurs identifiants pres.
bool sameDeclarations(const std::vector<Declaration>& a, const std::vector<Declaration>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        Declaration x = a[i], y = b[i];
        x.id = y.id = kNoId;
        if (!(x == y)) return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------- ecrire ---
File fromView(const View& v, std::string_view onlyEvent) {
    File f;
    f.genre = Genre::ViewScripts;
    f.writer = XPG_ANALYZER_VERSION;
    f.source = v.name;
    f.role = v.role;
    for (const auto ev : kViewEvents) {
        if (!onlyEvent.empty() && !sameName(ev, onlyEvent)) continue;
        const Script* s = scriptOf(v, ev);
        if (!s || normalBody(s->body).empty()) continue;
        Entry e;
        e.event = std::string(ev);
        e.lang = s->lang;
        e.body = s->body;
        e.decls = s->decls;                                 // 1.11.18 (lot 3)
        f.entries.push_back(std::move(e));
    }
    return f;
}

File fromOperators(const std::vector<HmiOperator>& ops, std::string_view owner, std::string_view role) {
    File f;
    f.genre = Genre::Operators;
    f.writer = XPG_ANALYZER_VERSION;
    f.source = std::string(owner);
    f.role = std::string(role);
    for (const auto& o : ops) {
        Entry e;
        e.op = o;
        e.body = o.body;
        e.decls = o.decls;                                  // 1.11.18 (lot 3)
        f.entries.push_back(std::move(e));
    }
    return f;
}

std::string write(const File& f) {
    std::string out;
    const bool ops = f.genre == Genre::Operators;
    out += "(* XPGAnalyser " + std::string(XPG_ANALYZER_VERSION) + " - " + (ops ? "op\xC3\xA9rateurs de " : "scripts de ")
         + f.source + (f.role.empty() ? std::string{} : " (" + f.role + ")") + "\n";
    out += "   Un bloc (*# ... *) avant chaque " + std::string(ops ? "op\xC3\xA9rateur" : "script")
         + ", puis son code jusqu'au bloc suivant. Le code se modifie ici ;\n"
           "   les blocs se gardent tels quels. Importer : Scripts (ou Op\xC3\xA9rateurs) > Importer\xE2\x80\xA6 *)\n";
    // 1.11.18 (lot 3) : le format 2 seulement s'il a des declarations du modele.
    const bool declared = std::any_of(f.entries.begin(), f.entries.end(), [](const Entry& e) { return !e.decls.empty(); });
    out += "(*# xpgst format=" + std::to_string(declared ? kFormat : 1) + " genre=" + (ops ? "operateurs" : "scripts-vue") + " source=" + quote(f.source)
         + (f.role.empty() ? std::string{} : " role=" + quote(f.role)) + " version=" + quote(f.writer.empty() ? XPG_ANALYZER_VERSION : f.writer)
         + " *)\n";
    for (const auto& e : f.entries) {
        if (ops) {
            out += "(*# operateur op=" + quote(e.op.op) + " gauche=" + quote(e.op.left);
            if (!e.op.right.empty()) out += " droite=" + quote(e.op.right);
            if (!e.op.result.empty()) out += " resultat=" + quote(e.op.result);
            if (!e.op.description.empty()) out += " description=" + quote(e.op.description);
            out += " *)\n";
        } else {
            out += "(*# script evenement=" + e.event + " langage=" + std::string(scriptLangKey(e.lang)) + " *)\n";
        }
        for (const auto& d : e.decls) {                     // 1.11.18 (lot 3, decision D4)
            out += "(*# declaration genre=" + std::string(declKindKey(d.kind)) + " nom=" + quote(d.name) + " type=" + quote(d.type);
            if (!d.value.empty()) out += " valeur=" + quote(d.value);
            if (d.kind == DeclKind::Variable) out += " stockage=" + std::string(storageKey(d.storage));
            if (d.kind == DeclKind::Parameter) out += " mode=" + std::string(passModeKey(d.mode));
            out += " visibilite=" + std::string(visibilityKey(d.visibility));
            if (!d.description.empty()) out += " description=" + quote(d.description);
            out += " *)\n";
        }
        const std::string body = normalBody(e.body);
        if (!body.empty()) out += body + "\n";
    }
    out += "(*# fin *)\n";
    return out;
}

// ---------------------------------------------------------------- lire ---
bool read(std::string_view text, File& out, std::string* why) {
    const auto fail = [why](std::string m) { if (why) *why = std::move(m); return false; };
    out = File{};
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);
    std::vector<std::string> lines;
    {
        std::string cur;
        for (const char c : text) {
            if (c == '\r') continue;
            if (c == '\n') { lines.push_back(std::move(cur)); cur.clear(); continue; }
            cur += c;
        }
        if (!cur.empty()) lines.push_back(std::move(cur));
    }
    bool header = false, ended = false;
    Entry* open = nullptr;
    std::string word;
    std::map<std::string, std::string> kv;
    for (std::size_t n = 0; n < lines.size(); ++n) {
        const std::string& line = lines[n];
        word.clear();
        if (!parseBlock(line, word, kv)) {
            if (open) open->body += line + "\n";
            continue;
        }
        const std::string where = " (ligne " + std::to_string(n + 1) + ")";
        if (word == "xpgst") {
            header = true;
            const std::string& fmt = at(kv, "format");
            out.format = fmt.empty() ? 1 : std::atoi(fmt.c_str());
            if (out.format > kFormat)
                return fail("Ce fichier vient d'une version plus r\xC3\xA9" "cente de XpgAnalyzer (" + at(kv, "version")
                            + ") : cette version (" + std::string(XPG_ANALYZER_VERSION) + ") ne sait pas le lire.");
            const std::string& g = at(kv, "genre");
            if (g == "operateurs") out.genre = Genre::Operators;
            else if (g.empty() || g == "scripts-vue") out.genre = Genre::ViewScripts;
            else return fail("Genre inconnu : " + g + where + ".");
            out.source = at(kv, "source");
            out.role = at(kv, "role");
            out.writer = at(kv, "version");
            continue;
        }
        if (word == "fin") { open = nullptr; ended = true; continue; }
        if (word == "declaration") {                        // 1.11.18 (lot 3, decision D4)
            if (!open) return fail("Une d\xC3\xA9" "claration hors d'un script ou d'un op\xC3\xA9rateur" + where + ".");
            Declaration d;
            const auto kind = declKindFromKey(at(kv, "genre"));
            if (!kind) return fail("Genre de d\xC3\xA9" "claration inconnu : \xC2\xAB " + at(kv, "genre") + " \xC2\xBB" + where + ".");
            d.kind = *kind;
            d.name = at(kv, "nom");
            d.type = at(kv, "type");
            d.value = at(kv, "valeur");
            d.description = at(kv, "description");
            d.storage = storageFromKey(at(kv, "stockage")).value_or(Storage::Execution);
            d.mode = passModeFromKey(at(kv, "mode")).value_or(PassMode::In);
            d.visibility = visibilityFromKey(at(kv, "visibilite")).value_or(Visibility::Public);
            if (d.name.empty()) return fail("Une d\xC3\xA9" "claration sans nom" + where + ".");
            open->decls.push_back(std::move(d));
            continue;
        }
        if (word == "script") {
            if (!header) out.genre = Genre::ViewScripts;
            if (out.genre != Genre::ViewScripts) return fail("Un script dans un fichier d'op\xC3\xA9rateurs" + where + ".");
            Entry e;
            if (!knownEvent(at(kv, "evenement"), e.event))
                return fail("\xC3\x89v\xC3\xA9nement inconnu : \xC2\xAB " + at(kv, "evenement") + " \xC2\xBB" + where + " (OnOpen, OnCycle ou OnClose).");
            const std::string& lang = at(kv, "langage");
            if (!lang.empty()) {
                const auto l = scriptLangFromKey(lang);
                if (!l) return fail("Langage inconnu : " + lang + where + ".");
                e.lang = *l;
            }
            out.entries.push_back(std::move(e));
            open = &out.entries.back();
            continue;
        }
        if (word == "operateur") {
            if (!header) out.genre = Genre::Operators;
            if (out.genre != Genre::Operators) return fail("Un op\xC3\xA9rateur dans un fichier de scripts" + where + ".");
            Entry e;
            e.op.op = at(kv, "op");
            e.op.left = at(kv, "gauche");
            e.op.right = at(kv, "droite");
            e.op.result = at(kv, "resultat");
            e.op.description = at(kv, "description");
            if (e.op.op.empty() || e.op.left.empty()) return fail("Un op\xC3\xA9rateur sans op ou sans gauche" + where + ".");
            out.entries.push_back(std::move(e));
            open = &out.entries.back();
            continue;
        }
        return fail("Bloc inconnu : (*# " + word + " ... *)" + where + ".");
    }
    (void)ended;
    for (auto& e : out.entries) {
        e.body = normalBody(e.body);
        if (out.genre == Genre::Operators) {
            e.op.body = e.body;
            e.op.decls = e.decls;
        }
    }
    if (!header && out.entries.empty()) {
        // Un fichier .st sans bloc : un seul script.
        out.plain = true;
        Entry e;
        e.event = "OnOpen";
        e.body = normalBody(text);
        if (e.body.empty()) return fail("Le fichier est vide.");
        out.entries.push_back(std::move(e));
        return true;
    }
    if (out.entries.empty())
        return fail(out.genre == Genre::Operators ? "Le fichier n'a aucun op\xC3\xA9rateur." : "Le fichier n'a aucun script.");
    return true;
}

bool save(const File& f, const std::string& path, std::string* why) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { if (why) *why = "\xC3\xA9" "criture impossible : " + path; return false; }
    std::string text = write(f), crlf;
    crlf.reserve(text.size() + text.size() / 20);
    for (const char c : text) {           // CRLF : le Bloc-notes le lit tel quel
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    out << "\xEF\xBB\xBF" << crlf;
    out.flush();
    if (!out) { if (why) *why = "\xC3\xA9" "criture incompl\xC3\xA8" "te : " + path; return false; }
    return true;
}

bool load(const std::string& path, File& out, std::string* why) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { if (why) *why = "fichier introuvable : " + path; return false; }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return read(text, out, why);
}

// ---------------------------------------------------------------- importer ---
std::string_view stateLabel(State s) noexcept {
    switch (s) {
        case State::New: return "nouveau";
        case State::Same: return "identique";
        case State::Different: return "diff\xC3\xA9rent";
    }
    return {};
}

std::vector<State> compareView(const File& f, const View& v) {
    std::vector<State> out;
    for (const auto& e : f.entries) {
        const Script* s = scriptOf(v, e.event);
        if (!s || normalBody(s->body).empty()) out.push_back(State::New);
        else if (normalBody(s->body) == normalBody(e.body) && s->lang == e.lang && sameDeclarations(s->decls, e.decls)) out.push_back(State::Same);
        else out.push_back(State::Different);
    }
    return out;
}

std::vector<State> compareOperators(const File& f, const std::vector<HmiOperator>& ops) {
    std::vector<State> out;
    for (const auto& e : f.entries) {
        const auto it = std::find_if(ops.begin(), ops.end(), [&](const HmiOperator& o) { return sameSignature(o, e.op); });
        if (it == ops.end()) out.push_back(State::New);
        else if (normalBody(it->body) == normalBody(e.body) && it->result == e.op.result && it->description == e.op.description
                 && sameDeclarations(it->decls, e.decls))
            out.push_back(State::Same);
        else out.push_back(State::Different);
    }
    return out;
}

std::string describe(const File& f, std::size_t index, State st, const View* view) {
    if (index >= f.entries.size()) return {};
    const auto& e = f.entries[index];
    const std::size_t n = lineCount(e.body);
    std::string head = f.genre == Genre::Operators ? operatorSignature(e.op) : e.event;
    std::string out = head + " \xC2\xB7 " + std::to_string(n) + " ligne" + (n > 1 ? "s" : "");
    if (st == State::New) return out + " \xC2\xB7 nouveau";
    if (st == State::Same) return out + " \xC2\xB7 identique (rien ne change)";
    std::size_t ours = 0;
    if (view)
        if (const Script* s = scriptOf(*view, e.event)) ours = lineCount(s->body);
    return out + " \xC2\xB7 diff\xC3\xA9rent" + (view ? " de l'actuel (" + std::to_string(ours) + " ligne" + (ours > 1 ? "s" : "") + ")" : std::string{});
}

std::size_t applyToView(Project& p, View& v, const File& f, const std::vector<bool>& chosen, Mode mode, std::string_view plainEvent) {
    std::size_t changed = 0;
    for (std::size_t i = 0; i < f.entries.size(); ++i) {
        if (!chosen.empty() && (i >= chosen.size() || !chosen[i])) continue;
        Entry e = f.entries[i];
        if (f.plain) {
            std::string ev;
            e.event = knownEvent(plainEvent, ev) ? ev : std::string("OnOpen");
        }
        Script* s = nullptr;
        for (auto& sc : v.scripts)
            if (sameName(sc.event, e.event)) s = &sc;
        if (!s) {
            Script made;
            made.id = p.allocate();
            made.name = v.name + "." + e.event;
            made.lang = e.lang;
            made.event = e.event;
            made.body = e.body + "\n";
            made.decls = e.decls;                           // 1.11.18 (lot 3) : leurs identifiants donnes par la commande
            v.scripts.push_back(std::move(made));
            ++changed;
            continue;
        }
        const std::string ours = normalBody(s->body);
        if (ours == normalBody(e.body) && s->lang == e.lang && sameDeclarations(s->decls, e.decls)) continue;
        if (mode == Mode::Append && !ours.empty()) {
            s->body = ours + "\n\n(* ---- import\xC3\xA9 de " + (f.source.empty() ? std::string("un fichier") : f.source) + " ---- *)\n" + e.body + "\n";
            // 1.11.18 (lot 3) : ses declarations, sauf celles dont le script a deja le nom.
            for (const auto& d : e.decls)
                if (std::none_of(s->decls.begin(), s->decls.end(), [&](const Declaration& x) { return sameName(x.name, d.name); }))
                    s->decls.push_back(d);
        } else {
            s->body = e.body + "\n";
            s->lang = e.lang;
            s->decls = e.decls;
        }
        ++changed;
    }
    return changed;
}

std::size_t applyToOperators(Project& p, std::vector<HmiOperator>& ops, const File& f, const std::vector<bool>& chosen) {
    std::size_t changed = 0;
    for (std::size_t i = 0; i < f.entries.size(); ++i) {
        if (!chosen.empty() && (i >= chosen.size() || !chosen[i])) continue;
        HmiOperator o = f.entries[i].op;
        o.body = f.entries[i].body;
        o.decls = f.entries[i].decls;                       // 1.11.18 (lot 3)
        const auto it = std::find_if(ops.begin(), ops.end(), [&](const HmiOperator& x) { return sameSignature(x, o); });
        if (it == ops.end()) {
            o.id = p.allocate();
            ops.push_back(std::move(o));
            ++changed;
            continue;
        }
        if (normalBody(it->body) == normalBody(o.body) && it->result == o.result && it->description == o.description
            && sameDeclarations(it->decls, o.decls))
            continue;
        o.id = it->id;
        *it = std::move(o);
        ++changed;
    }
    return changed;
}

} // namespace hmi::scriptfile
