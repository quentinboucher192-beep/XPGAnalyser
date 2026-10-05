// hmi/HmiRenameRefs.cpp - renommer un objet d'une vue : ce qui le cite suit
// (voir HmiRenameRefs.hpp). Lot API 8 : renommer partout (IHM).
#include "HmiRenameRefs.hpp"

#include "HmiScenarios.hpp"       // stepKind : ce que vise un pas d'essai
#include "HmiSymbols.hpp"         // replacePath, rewriteNames
#include "HmiObjectAlarms.hpp"     // 1.9 : renameOverridePaths

#include <algorithm>     // lot API 8 : finitions
#include <cctype>
#include <cstdint>       // lot API 8 : finitions
#include <functional>
#include <map>
#include <string_view>

namespace hmi {

namespace {

using Fn = std::function<std::string(std::string_view, bool code)>;

bool sameText(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

std::string trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

// Un texte passe par `f` comme rewriteNames le passerait sous `key` : "text"
// (un texte a trous : chaque trou), "variable" (une expression).
std::string through(const char* key, const std::string& text, const Fn& f) {
    if (trimmed(text).empty()) return text;
    Object tmp;
    tmp.props.push_back(Prop{key, text, {}});
    rewriteNames(tmp, f);
    return tmp.props.front().value;
}

bool isTemplateRole(const View& v) { return v.role == "modele" || v.role == "entete" || v.role == "pied"; }

// ---- Lot API 8 : finitions (les chaines comparees, les scripts C / C++) ----
// Les morceaux d'un texte ST (le meme decoupage que le dialogue Renommer) : les
// commentaires sautes, une chaine avec ses guillemets.
struct Tok {
    enum class K : std::uint8_t { Ident, Str, Num, Op } k{K::Op};
    std::size_t b{0}, e{0};
};

std::vector<Tok> stTokens(std::string_view s) {
    std::vector<Tok> out;
    const std::size_t n = s.size();
    std::size_t i = 0;
    const auto uc = [](char c) { return static_cast<unsigned char>(c); };
    while (i < n) {
        const char c = s[i];
        const char nx = i + 1 < n ? s[i + 1] : '\0';
        if (std::isspace(uc(c))) { ++i; continue; }
        if (c == '(' && nx == '*') {
            const auto end = s.find("*)", i + 2);
            i = end == std::string_view::npos ? n : end + 2;
            continue;
        }
        if (c == '/' && nx == '/') {
            const auto end = s.find('\n', i);
            i = end == std::string_view::npos ? n : end;
            continue;
        }
        if (c == '\'' || c == '"') {
            std::size_t j = i + 1;
            while (j < n && s[j] != c) j += (s[j] == '$' && j + 1 < n) ? 2 : 1;
            const std::size_t e = std::min(n, j + 1);
            out.push_back({Tok::K::Str, i, e});
            i = e;
            continue;
        }
        if (std::isalnum(uc(c)) || c == '_') {
            std::size_t j = i + 1;
            while (j < n && (std::isalnum(uc(s[j])) || s[j] == '_' || s[j] == '#')) ++j;
            out.push_back({std::isdigit(uc(c)) ? Tok::K::Num : Tok::K::Ident, i, j});
            i = j;
            continue;
        }
        std::size_t len = 1;
        if ((c == '<' && nx == '>') || (c == ':' && nx == '=') || (c == '<' && nx == '=') || (c == '>' && nx == '=')) len = 2;
        out.push_back({Tok::K::Op, i, i + len});
        i += len;
    }
    return out;
}

// La chaine qui nomme tout entier `from`, ou un chemin qui en part : `to` a sa place.
std::string renamedPath(std::string_view s, std::string_view from, std::string_view to) {
    if (s == from) return std::string(to);
    if (s.size() > from.size() + 1 && s.substr(0, from.size()) == from && s[from.size()] == '.')
        return std::string(to) + std::string(s.substr(from.size()));
    return std::string(s);
}

// La chaine egale a `from`, PREMIER argument d'une des fonctions `fns` (sans la
// casse du nom de la fonction) - IHM_METTRE_DE_COTE('Alarme_1', 60),
// IHM_GIF_JOUER('Ventilateur') : `to` a sa place. Ailleurs, un texte.
std::string renameFirstArguments(std::string_view s, const std::vector<std::string>& fns, std::string_view from, std::string_view to) {
    if (from.empty() || from == to || s.find(from) == std::string_view::npos) return std::string(s);
    const auto toks = stTokens(s);
    std::string out;
    std::size_t done = 0;
    for (std::size_t k = 2; k < toks.size(); ++k) {
        if (toks[k].k != Tok::K::Str || toks[k].e - toks[k].b < 2) continue;
        if (s.substr(toks[k].b + 1, toks[k].e - toks[k].b - 2) != from) continue;
        const auto& open = toks[k - 1];
        const auto& fn = toks[k - 2];
        if (open.k != Tok::K::Op || s.substr(open.b, open.e - open.b) != "(" || fn.k != Tok::K::Ident) continue;
        const auto name = s.substr(fn.b, fn.e - fn.b);
        if (std::none_of(fns.begin(), fns.end(), [&](const std::string& f) { return sameText(name, f); })) continue;
        out.append(s.substr(done, toks[k].b + 1 - done));
        out.append(to);
        done = toks[k].e - 1;
    }
    if (done == 0) return std::string(s);
    out.append(s.substr(done));
    return out;
}
// ---- fin Lot API 8 : finitions ----

} // namespace

// ---- Lot API 8 : finitions (les chaines comparees, les scripts C / C++) ----
std::string renameComparedStrings(std::string_view s, const std::vector<std::string>& sysVars, std::string_view from, std::string_view to) {
    if (from.empty() || from == to || s.find(from) == std::string_view::npos) return std::string(s);
    const auto toks = stTokens(s);
    const auto word = [&](std::size_t k) { return s.substr(toks[k].b, toks[k].e - toks[k].b); };
    const auto is = [&](std::size_t k, Tok::K kind, std::string_view text) {
        return k < toks.size() && toks[k].k == kind && sameText(word(k), text);
    };
    const auto named = [&](std::size_t k) {
        if (k >= toks.size() || toks[k].k != Tok::K::Ident) return false;
        for (const auto& v : sysVars)
            if (sameText(word(k), v)) return true;
        return false;
    };
    // L'operande qui finit en k (x = 'A') ; celui qui commence en k ('A' = x) : SYS.<variable>.
    const auto endsAt = [&](std::size_t k) {
        return k >= 2 && named(k) && is(k - 1, Tok::K::Op, ".") && is(k - 2, Tok::K::Ident, "SYS") && !(k >= 3 && is(k - 3, Tok::K::Op, "."));
    };
    const auto startsAt = [&](std::size_t k) {
        return is(k, Tok::K::Ident, "SYS") && is(k + 1, Tok::K::Op, ".") && named(k + 2) && !is(k + 3, Tok::K::Op, ".");
    };
    const auto comparison = [&](std::size_t k) { return is(k, Tok::K::Op, "=") || is(k, Tok::K::Op, "<>"); };
    std::string out;
    std::size_t done = 0;
    for (std::size_t k = 0; k < toks.size(); ++k) {
        if (toks[k].k != Tok::K::Str || toks[k].e - toks[k].b < 2) continue;
        if (s.substr(toks[k].b + 1, toks[k].e - toks[k].b - 2) != from) continue;
        const bool hit = (k >= 2 && comparison(k - 1) && endsAt(k - 2)) || (comparison(k + 1) && startsAt(k + 2));
        if (!hit) continue;
        out.append(s.substr(done, toks[k].b + 1 - done));
        out.append(to);
        done = toks[k].e - 1;
    }
    if (done == 0) return std::string(s);
    out.append(s.substr(done));
    return out;
}

std::string rewriteCStrings(std::string_view s, const std::function<std::string(std::string_view)>& onString) {
    const std::size_t n = s.size();
    std::string out;
    std::size_t done = 0, i = 0;
    while (i < n) {
        const char c = s[i];
        const char nx = i + 1 < n ? s[i + 1] : '\0';
        if (c == '/' && nx == '*') {
            const auto end = s.find("*/", i + 2);
            i = end == std::string_view::npos ? n : end + 2;
            continue;
        }
        if ((c == '/' && nx == '/') || c == '#') {        // un commentaire ; une ligne du preprocesseur
            const auto end = s.find('\n', i);
            i = end == std::string_view::npos ? n : end;
            continue;
        }
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {   // un nom, un nombre (1'000 n'ouvre pas un caractere)
            while (i < n && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '\'' || s[i] == '.')) ++i;
            continue;
        }
        if (c != '"' && c != '\'') { ++i; continue; }
        std::size_t j = i + 1;
        while (j < n && s[j] != c && s[j] != '\n') j += (s[j] == '\\' && j + 1 < n) ? 2 : 1;
        if (c == '"' && j < n && s[j] == c && onString) {
            const auto inner = s.substr(i + 1, j - i - 1);
            if (const std::string next = onString(inner); next != inner) {
                out.append(s.substr(done, i + 1 - done));
                out.append(next);
                done = j;
            }
        }
        i = std::min(n, j + 1);
    }
    if (done == 0) return std::string(s);
    out.append(s.substr(done));
    return out;
}
// ---- fin Lot API 8 : finitions ----

std::size_t renameObjectReferences(Project& p, const std::string& view, const std::string& from, const std::string& to,
                                   std::vector<std::string>* where) {
    if (from.empty() || to.empty() || from == to) return 0;
    const View* self = nullptr;
    for (const auto& v : p.views)
        if (sameText(v.name, view)) self = &v;
    if (!self) return 0;
    // Les vues par lesquelles l'objet se lit : la sienne, et celles qui
    // l'empruntent (sans objet a elles du meme nom).
    std::vector<std::string> owners{self->name};
    std::vector<Id> ownerIds{self->id};
    if (isTemplateRole(*self)) {
        Id firstHeader = kNoId, firstFooter = kNoId;
        for (const auto& v : p.views) {
            if (v.role == "entete" && firstHeader == kNoId) firstHeader = v.id;
            if (v.role == "pied" && firstFooter == kNoId) firstFooter = v.id;
        }
        // Tranche 2 : a tous les niveaux - une vue qui emprunte une emprunteuse (un
        // modele sur un modele) lit l'objet elle aussi.
        for (std::size_t k = 0; k < ownerIds.size(); ++k) {
            const Id lender = ownerIds[k];
            for (const auto& v : p.views) {
                bool known = false;
                for (Id o : ownerIds) known = known || o == v.id;
                if (known) continue;
                const bool borrows = v.templateView == lender
                                  || (v.showHeader && (v.header == lender || (v.header == kNoId && firstHeader == lender)))
                                  || (v.showFooter && (v.footer == lender || (v.footer == kNoId && firstFooter == lender)));
                if (!borrows || v.objectByName(from)) continue;
                owners.push_back(v.name);
                ownerIds.push_back(v.id);
            }
        }
    }
    // Lot API 8 : finitions - la chaine 'Vue.Objet' comparee a SYS.FocusedObject / PressedObject.
    const std::vector<std::string> objectVars{"FocusedObject", "PressedObject"};
    const Fn f = [&](std::string_view t, bool code) {
        std::string s(t);
        for (const auto& o : owners) s = replacePath(s, o + "." + from, o + "." + to, code);
        for (const auto& o : owners) s = renameComparedStrings(s, objectVars, o + "." + from, o + "." + to);   // lot API 8 : finitions
        return s;
    };
    // Lot API 8 : finitions (suite) - un GIF anime : IHM_GIF_JOUER('Objet') (PAUSE, ARRETER, REJOUER)
    // dans un script ST de sa vue (ou d'une qui l'emprunte), comme ses actions ; un script general
    // peut viser le GIF d'une autre vue ouverte du meme nom : il reste.
    const Object* renamed = self->objectByName(to) ? self->objectByName(to) : self->objectByName(from);
    const bool gif = renamed && renamed->kind == Kind::AnimatedGif;
    const std::vector<std::string> gifFns{"IHM_GIF_JOUER", "IHM_GIF_PAUSE", "IHM_GIF_ARRETER", "IHM_GIF_REJOUER"};
    // Lot API 8 : finitions - un script C / C++ : la chaine "Vue.Objet" (ou un chemin qui en part).
    const auto cStrings = [&](const std::string& body) {
        return rewriteCStrings(body, [&](std::string_view str) {
            std::string next(str);
            for (const auto& o : owners) next = renamedPath(next, o + "." + from, o + "." + to);
            return next;
        });
    };
    std::size_t n = 0;
    const auto note = [&](std::string place) {
        ++n;
        if (where) where->push_back(std::move(place));
    };
    const auto isOwner = [&](Id id) {
        for (Id o : ownerIds) if (o == id) return true;
        return false;
    };
    // Les actions qui visent l'objet par son nom (dans une vue ou il se lit).
    const auto retarget = [&](std::vector<Action>& list) {
        bool changed = false;
        for (auto& a : list)
            if ((operationTargetsGif(a.operation) || a.operation == Operation::BindTable) && sameText(trimmed(a.target), from)) {
                a.target = to;
                changed = true;
            }
        return changed;
    };

    for (auto& v : p.views) {
        const bool owner = isOwner(v.id);
        for (auto& o : v.objects) {
            Object b = o;
            rewriteNames(b, f);
            if (owner) (void)retarget(b.actions);
            if (b == o) continue;
            o = std::move(b);
            note(v.name + " / " + (o.name.empty() ? "objet " + std::to_string(o.id) : o.name));
        }
        {
            Object holder;
            holder.actions = v.actions;
            rewriteNames(holder, f);
            bool changed = !(holder.actions == v.actions);
            if (owner) changed = retarget(holder.actions) || changed;
            if (changed) {
                v.actions = std::move(holder.actions);
                note(v.name + " / actions de la vue");
            }
        }
        for (auto& sc : v.scripts) {
            if (sc.lang != ScriptLang::ST && !sc.body.empty()) {      // lot API 8 : finitions - C / C++
                const std::string next = cStrings(sc.body);
                if (next == sc.body) continue;
                sc.body = next;
                note(v.name + " / script " + (sc.name.empty() ? sc.event : sc.name));
                continue;
            }
            if (sc.lang != ScriptLang::ST || sc.body.empty()) continue;
            std::string next = f(sc.body, true);
            if (owner && gif) next = renameFirstArguments(next, gifFns, from, to);    // lot API 8 : finitions (suite)
            if (next == sc.body) continue;
            sc.body = next;
            note(v.name + " / script " + (sc.name.empty() ? sc.event : sc.name));
        }
        for (auto& prm : v.params) {
            const std::string next = through("variable", prm.defaultValue, f);
            if (next == prm.defaultValue) continue;
            prm.defaultValue = next;
            note(v.name + " / param\xC3\xA8tre " + prm.name);
        }
        if (const std::string t = through("text", v.popup.title, f); t != v.popup.title) {
            v.popup.title = t;
            note(v.name + " / titre de la popup");
        }
    }
    for (auto& sc : p.programs.scripts) {
        const std::string body = sc.lang == ScriptLang::ST && !sc.body.empty() ? f(sc.body, true)
                               : sc.body.empty() ? sc.body : cStrings(sc.body);      // lot API 8 : finitions - C / C++
        const std::string watch = through("variable", sc.watch, f);
        if (body == sc.body && watch == sc.watch) continue;
        sc.body = body;
        sc.watch = watch;
        note("Scripts g\xC3\xA9n\xC3\xA9raux / " + sc.name);
    }
    for (auto& fn : p.programs.functions) {
        if (fn.body.empty()) continue;
        const std::string body = f(fn.body, true);
        if (body == fn.body) continue;
        fn.body = body;
        note("Fonctions / " + fn.name);
    }
    for (auto& a : p.alarms) {
        const std::string c = through("variable", a.condition, f), m = through("text", a.message, f),
                          i = through("text", a.instruction, f);
        if (c == a.condition && m == a.message && i == a.instruction) continue;
        a.condition = c;
        a.message = m;
        a.instruction = i;
        note("Alarmes / " + a.name);
    }
    for (auto& rc : p.recipes)
        for (auto& fld : rc.fields)
            if (const std::string v = through("variable", fld.variable, f); v != fld.variable) {
                fld.variable = v;
                note("Recettes / " + rc.name + " / " + fld.name);
            }
    for (auto& item : p.history.archived)
        if (const std::string v = through("variable", item, f); v != item) {
            item = v;
            note("Historiques / " + v);
        }
    for (auto& u : p.security.users)
        if (const std::string e = through("variable", u.expression, f); e != u.expression) {
            u.expression = e;
            note("Utilisateurs / " + u.login);
        }
    for (auto& rp : p.reports)
        if (const std::string m = through("variable", rp.measures, f); m != rp.measures) {
            rp.measures = m;
            note("Rapports / " + rp.name);
        }
    for (auto& st : p.styles) {
        bool changed = false;
        for (auto& prop : st.props)
            if (const std::string e = through("variable", prop.expr, f); e != prop.expr) {
                prop.expr = e;
                changed = true;
            }
        if (changed) note("Styles / " + st.name);
    }
    for (auto& d : p.displays)
        if (const std::string v = through("variable", d.path, f); v != d.path) {
            d.path = v;
            note("Unit\xC3\xA9s et formats / " + v);
        }
    for (auto& sc : p.scenarios) {
        bool changed = false;
        for (auto& st : sc.steps) {
            const auto kind = stepKind(st.action);
            if (kind == StepKind::Write) {
                const std::string t = through("variable", st.target, f), v = through("variable", st.value, f);
                changed = changed || t != st.target || v != st.value;
                st.target = t;
                st.value = v;
            } else if (kind == StepKind::WaitUntil || kind == StepKind::Check) {
                const std::string v = through("variable", st.value, f);
                changed = changed || v != st.value;
                st.value = v;
            } else if (kind == StepKind::Click) {
                for (const auto& o : owners)
                    if (sameText(trimmed(st.target), o + "." + from)) {
                        st.target = o + "." + to;
                        changed = true;
                    }
            }
        }
        if (changed) note("Essais / " + sc.name);
    }
    // Les traductions sont rangees sous leur texte d'origine : un texte a
    // trous qui change de trou change de cle.
    std::map<std::string, std::map<std::string, std::string>> texts;
    bool moved = false;
    for (const auto& [key, tr] : p.languages.texts) {
        const std::string k = through("text", key, f);
        std::map<std::string, std::string> t2;
        for (const auto& [code, t] : tr) t2[code] = through("text", t, f);
        if (k != key || t2 != tr) {
            moved = true;
            note("Langues / " + k);
        }
        auto& slot = texts[k];
        for (auto& [code, t] : t2) slot.emplace(code, std::move(t));
    }
    if (moved) p.languages.texts = std::move(texts);
    // 1.9 : un objet renomme dans un symbole - les surcharges d'alarmes des instances (leur chemin) suivent.
    if (const View* sv = p.viewByName(view); sv && isSymbolView(*sv))
        if (renameOverridePaths(p, sv->name, from, to) > 0) note("Surcharges d'alarmes");
    // 1.9 : le filtre de groupe des objets d'alarmes et des historiques qui nomme le
    // groupe interne de l'objet ("Vue.Objet", ou "Vue.Objet.X" d'un objet qu'il
    // contient) suit ; les motifs (Vue.*) et les autres groupes ne changent pas.
    {
        const std::string oldGroup = self->name + "." + from, newGroup = self->name + "." + to;
        for (auto& v : p.views)
            for (auto& o : v.objects) {
                if (o.kind != Kind::History && !kindIsAlarmView(o.kind)) continue;
                Prop* g = o.find("group");
                if (!g || g->value.empty()) continue;
                std::string next;
                bool changed = false;
                std::size_t start = 0;
                while (start <= g->value.size()) {
                    const std::size_t semi = std::min(g->value.find(';', start), g->value.size());
                    std::string item = trimmed(std::string_view(g->value).substr(start, semi - start));
                    if (sameText(item, oldGroup)) {
                        item = newGroup;
                        changed = true;
                    } else if (item.size() > oldGroup.size() && item[oldGroup.size()] == '.'
                               && sameText(std::string_view(item).substr(0, oldGroup.size()), oldGroup)) {
                        item = newGroup + item.substr(oldGroup.size());
                        changed = true;
                    }
                    if (!item.empty()) next += (next.empty() ? "" : "; ") + item;
                    start = semi + 1;
                }
                if (!changed) continue;
                g->value = std::move(next);
                note(v.name + " / " + o.name + " (filtre d'alarmes)");
            }
    }
    return n;
}

// ---- Lot API 8 : finitions (les chaines comparees : renommer une alarme) ----
std::size_t renameAlarmReferences(Project& p, const std::string& from, const std::string& to, std::vector<std::string>* where) {
    if (from.empty() || to.empty() || from == to) return 0;
    const std::vector<std::string> alarmVars{"AlarmSelected", "AlarmLastName"};
    // Lot API 8 : finitions (suite) - et le premier argument de IHM_METTRE_DE_COTE / IHM_REMETTRE.
    const std::vector<std::string> alarmFns{"IHM_METTRE_DE_COTE", "IHM_REMETTRE"};
    const Fn f = [&](std::string_view t, bool) {
        return renameFirstArguments(renameComparedStrings(t, alarmVars, from, to), alarmFns, from, to);
    };
    std::size_t n = 0;
    const auto note = [&](std::string place) {
        ++n;
        if (where) where->push_back(std::move(place));
    };
    const auto st = [&](std::string& body) {
        if (body.empty()) return false;
        std::string next = f(body, true);
        if (next == body) return false;
        body = std::move(next);
        return true;
    };
    for (auto& v : p.views) {
        for (auto& o : v.objects) {
            Object b = o;
            rewriteNames(b, f);
            if (b == o) continue;
            o = std::move(b);
            note(v.name + " / " + (o.name.empty() ? "objet " + std::to_string(o.id) : o.name));
        }
        Object holder;
        holder.actions = v.actions;
        rewriteNames(holder, f);
        if (!(holder.actions == v.actions)) {
            v.actions = std::move(holder.actions);
            note(v.name + " / actions de la vue");
        }
        for (auto& sc : v.scripts)
            if (sc.lang == ScriptLang::ST && st(sc.body)) note(v.name + " / script " + (sc.name.empty() ? sc.event : sc.name));
        for (auto& prm : v.params)
            if (const std::string next = through("variable", prm.defaultValue, f); next != prm.defaultValue) {
                prm.defaultValue = next;
                note(v.name + " / param\xC3\xA8tre " + prm.name);
            }
        if (const std::string t = through("text", v.popup.title, f); t != v.popup.title) {
            v.popup.title = t;
            note(v.name + " / titre de la popup");
        }
    }
    for (auto& sc : p.programs.scripts) {
        bool changed = sc.lang == ScriptLang::ST && st(sc.body);
        if (const std::string w = through("variable", sc.watch, f); w != sc.watch) {
            sc.watch = w;
            changed = true;
        }
        if (changed) note("Scripts g\xC3\xA9n\xC3\xA9raux / " + sc.name);
    }
    for (auto& fn : p.programs.functions)
        if (st(fn.body)) note("Fonctions / " + fn.name);
    for (auto& a : p.alarms) {
        const std::string c = through("variable", a.condition, f), m = through("text", a.message, f),
                          i = through("text", a.instruction, f);
        if (c == a.condition && m == a.message && i == a.instruction) continue;
        a.condition = c;
        a.message = m;
        a.instruction = i;
        note("Alarmes / " + a.name);
    }
    for (auto& u : p.security.users)
        if (const std::string e = through("variable", u.expression, f); e != u.expression) {
            u.expression = e;
            note("Utilisateurs / " + u.login);
        }
    for (auto& s : p.styles) {
        bool changed = false;
        for (auto& prop : s.props)
            if (const std::string e = through("variable", prop.expr, f); e != prop.expr) {
                prop.expr = e;
                changed = true;
            }
        if (changed) note("Styles / " + s.name);
    }
    for (auto& sc : p.scenarios) {
        bool changed = false;
        for (auto& step : sc.steps) {
            const auto kind = stepKind(step.action);
            if (kind != StepKind::Write && kind != StepKind::WaitUntil && kind != StepKind::Check) continue;
            const std::string v = through("variable", step.value, f);
            changed = changed || v != step.value;
            step.value = v;
        }
        if (changed) note("Essais / " + sc.name);
    }
    return n;
}
// ---- fin Lot API 8 : finitions ----

} // namespace hmi
