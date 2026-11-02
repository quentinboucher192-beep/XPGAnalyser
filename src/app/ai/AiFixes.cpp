// =============================================================================
//  app/ai/AiFixes.cpp - voir AiFixes.hpp
// =============================================================================
#include "AiFixes.hpp"

#include "AiAssistant.hpp"
#include "AiDelete.hpp"   // 1.22.7 : une suppression refusee (sa strategie, son genre)
#include "../../ui/Icons.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <unordered_map>

namespace app::ai::fixes {

namespace {

const char* const kOpen = "\xC2\xAB ";    // << (et son espace)
const char* const kClose = " \xC2\xBB";   // >>

std::string quoted(const std::string& s) { return kOpen + s + kClose; }

bool same(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

std::string trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// Les messages regroupes (le premier garde sa place), et les operations de chacun.
std::vector<Line> group(const std::vector<std::string>& messages, bool error) {
    std::vector<Line> out;
    std::unordered_map<std::string, std::size_t> at;
    for (const auto& m : messages) {
        const auto [it, fresh] = at.emplace(m, out.size());
        if (fresh) {
            Line l;
            l.message = m;
            l.error = error;
            out.push_back(std::move(l));
        } else {
            ++out[it->second].count;
        }
    }
    return out;
}

void addOp(Line& l, std::size_t op) {
    if (op == Check::Fault::kNoOp) return;
    if (std::find(l.ops.begin(), l.ops.end(), op) == l.ops.end()) l.ops.push_back(op);
}

void finish(std::vector<Line>& lines) {
    for (auto& l : lines) {
        std::sort(l.ops.begin(), l.ops.end());
        l.text = l.message;
        if (l.ops.size() > 1) l.text += " (" + std::to_string(l.ops.size()) + " op\xC3\xA9rations)";
        else if (l.count > 1) l.text += " (" + std::to_string(l.count) + " fois)";
    }
}

Action entry(Action::Kind kind, std::string label, std::string note = {}) {
    Action a;
    a.kind = kind;
    a.label = std::move(label);
    a.note = std::move(note);
    return a;
}

ui::Icon iconOf(Action::Kind k) {
    switch (k) {
        case Action::Kind::Use: return ui::Icon::Variable;
        case Action::Kind::Pick: return ui::Icon::Search;
        case Action::Kind::Create: return ui::Icon::Plus;
        case Action::Kind::RemoveUsing:
        case Action::Kind::RemoveOps: return ui::Icon::Delete;
        case Action::Kind::Copy: return ui::Icon::Copy;
        case Action::Kind::AskAi: return ui::Icon::Assistant;
        case Action::Kind::Revert:
        case Action::Kind::Restore: return ui::Icon::Undo;
        case Action::Kind::BuildFix:
        case Action::Kind::Strategy:
        case Action::Kind::Prop: return ui::Icon::Wrench;
        case Action::Kind::Quick:
        case Action::Kind::QuickAll: return ui::Icon::Ok;   // 1.22.7
        case Action::Kind::Undo: return ui::Icon::Undo;
        default: return ui::Icon::None;
    }
}

const char* const kClosed = "la proposition n'est plus \xC3\xA0 d\xC3\xA9" "cider";

// ---- 1.22.7 : LA VARIABLE NETTEMENT LA PLUS PROCHE ---------------------------------------------------------------------
// Un nom ramene a ses lettres et chiffres, en minuscules (<< Vanne.Cmd >>, << VANNE_CMD >> -> << vannecmd >>).
std::string foldedName(std::string_view s) {
    std::string out;
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) || c >= 0x80) out += static_cast<char>(std::tolower(c));
    }
    return out;
}
std::string digitsIn(std::string_view s) {
    std::string out;
    for (const char ch : s)
        if (std::isdigit(static_cast<unsigned char>(ch))) out += ch;
    return out;
}
// Les mots d'un nom, en minuscules : coupes aux separateurs, entre une minuscule et une majuscule (NbOuv : nb, ouv), entre
// lettres et chiffres (Pompe1 : pompe, 1).
std::vector<std::string> nameWords(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    int prev = 0;   // 1 : une minuscule (ou un octet UTF-8), 2 : une majuscule, 3 : un chiffre
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        const int k = std::isdigit(c) ? 3 : std::isupper(c) ? 2 : (std::islower(c) || c >= 0x80) ? 1 : 0;
        if (k == 0) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
            prev = 0;
            continue;
        }
        if (!cur.empty() && ((prev == 1 && k == 2) || (prev == 3) != (k == 3))) {
            out.push_back(cur);
            cur.clear();
        }
        cur += static_cast<char>(std::tolower(c));
        prev = k;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
// Deux mots qui disent la meme chose : egaux, ou l'un l'abrege de l'autre (la meme initiale, ses lettres dans l'ordre :
// Cmd / Commande, Nb / Nombre, Temp / Temperature). Un nombre : seulement egal.
bool wordLike(const std::string& a, const std::string& b) {
    if (a == b) return true;
    if (a.empty() || b.empty() || std::isdigit(static_cast<unsigned char>(a[0])) || std::isdigit(static_cast<unsigned char>(b[0]))) return false;
    const std::string& s = a.size() <= b.size() ? a : b;
    const std::string& t = a.size() <= b.size() ? b : a;
    if (s.size() < 2 || s[0] != t[0]) return false;
    std::size_t j = 0;
    for (const char c : t)
        if (j < s.size() && c == s[j]) ++j;
    return j == s.size();
}
std::size_t editDistance(const std::string& a, const std::string& b) {
    std::vector<std::size_t> row(b.size() + 1), next(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        next[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j)
            next[j] = std::min({row[j] + 1, next[j - 1] + 1, row[j - 1] + (a[i - 1] == b[j - 1] ? 0u : 1u)});
        std::swap(row, next);
    }
    return row[b.size()];
}
constexpr int kFar = 9;
// Le rang d'une candidate (plus petit : plus proche ; kFar : pas proche) - 0 le meme nom aux separateurs et a la casse pres ;
// 1 les memes mots (un abrege permis) ; 2 une faute de frappe (deux lettres au plus, sur un nom d'au moins six). Les chiffres
// doivent etre les memes (Pompe_2 n'est jamais Pompe_1).
int tierOf(const std::string& fm, const std::string& dm, const std::vector<std::string>& wm, std::string_view candidate) {
    const std::string fc = foldedName(candidate);
    if (fc.empty()) return kFar;
    if (fc == fm) return 0;
    if (digitsIn(fc) != dm) return kFar;
    const auto wc = nameWords(candidate);
    if (!wm.empty() && wc.size() == wm.size()) {
        bool all = true;
        for (std::size_t i = 0; i < wm.size() && all; ++i) all = wordLike(wm[i], wc[i]);
        if (all) return 1;
    }
    const std::size_t lm = fm.size(), lc = fc.size();
    if (lm >= 6 && (lm > lc ? lm - lc : lc - lm) <= 2 && editDistance(fm, fc) <= 2) return 2;
    return kFar;
}

// Une propriete d'une operation (vide : absente).
std::string propOf(const Operation& o, std::string_view key) {
    for (const auto& [k, v] : o.props)
        if (same(k, key)) return trimmed(v);
    return {};
}
bool saysNo(std::string_view v) {
    const std::string s = trimmed(v);
    return same(s, "non") || same(s, "false") || same(s, "faux") || same(s, "no") || same(s, "off") || s == "0";
}

// 1.22.7 : UNE SUPPRESSION REFUSEE PARCE QUE L'ELEMENT EST CITE AILLEURS - une suppression d'elements du projet, strategie
// << seul >>, a qui l'analyse trouve des dependances directes ; << references >> peut les retirer (pas un type : ses variables
// deviendraient invalides ; pas un dossier).
bool refusedRemoval(const Proposal& p, const Check& ck, std::size_t op) {
    if (op >= p.ops.size()) return false;
    const Operation& o = p.ops[op];
    if (o.action != Operation::Action::Remove || !del::projectRemoval(o)) return false;
    del::Request rq;
    if (!del::requestOf(o, rq) || rq.strategy != del::Strategy::Only || rq.genre == del::Genre::Type || rq.genre == del::Genre::Folder) return false;
    for (const auto& r : ck.removals)
        if (r.op == op)
            for (const auto& d : r.dependencies)
                if (d.what == "directe") return true;
    return false;
}

} // namespace

std::vector<Line> errorLines(const Check& ck) {
    std::vector<Line> out = group(ck.errors, true);
    std::unordered_map<std::string, std::size_t> at;
    for (std::size_t i = 0; i < out.size(); ++i) at.emplace(out[i].message, i);
    for (std::size_t k = 0; k < ck.faults.size(); ++k) {
        const auto it = at.find(ck.faults[k].message);
        if (it == at.end()) continue;
        Line& l = out[it->second];
        l.faults.push_back(k);
        addOp(l, ck.faults[k].op);
    }
    finish(out);
    return out;
}

std::vector<Line> warningLines(const Check& ck) {
    std::vector<Line> out = group(ck.warnings, false);
    std::unordered_map<std::string, std::size_t> at;
    for (std::size_t i = 0; i < out.size(); ++i) at.emplace(out[i].message, i);
    for (std::size_t op = 0; op < ck.outcomes.size(); ++op)
        for (const auto& w : ck.outcomes[op].warnings)
            if (const auto it = at.find(w); it != at.end()) addOp(out[it->second], op);
    finish(out);
    return out;
}

const std::vector<std::string>& types() {
    static const std::vector<std::string> all = {"BOOL", "INT", "DINT", "REAL", "WORD", "STRING", "TIME"};
    return all;
}

std::string opLabel(const Proposal& p, std::size_t op) {
    if (op >= p.ops.size()) return {};
    const Operation& o = p.ops[op];
    const std::string name = trimmed(o.name.empty() ? o.variable : o.name);
    return "op\xC3\xA9ration " + std::to_string(op + 1) + " (" + std::string(actionKey(o.action)) + (name.empty() ? std::string{} : " " + name) + ")";
}

std::string askText(const Line& l) {
    std::string t = "Corrige ta proposition. ";
    t += l.error ? "Erreur" : "Avertissement";
    if (l.ops.size() > 1) t += " (" + std::to_string(l.ops.size()) + " op\xC3\xA9rations)";
    t += " : " + l.message;
    t += "\nNe change que ce qui est en cause. N'invente aucune variable : utilise une variable du projet, ou dis-moi laquelle cr\xC3\xA9" "er.";
    return t;
}

std::string clearlyClosest(std::string_view missing, const std::vector<std::string>& candidates, std::string* why) {
    const std::string fm = foldedName(missing), dm = digitsIn(fm);
    const auto        wm = nameWords(missing);
    int               best = kFar;
    std::vector<std::string> at;
    std::set<std::string>    seen;
    for (const auto& c : candidates) {
        const std::string name = trimmed(c);
        if (name.empty() || !seen.insert(foldedName(name) + "|" + name).second) continue;
        const int t = tierOf(fm, dm, wm, name);
        if (t >= kFar || t > best) continue;
        if (t < best) {
            best = t;
            at.clear();
        }
        if (std::none_of(at.begin(), at.end(), [&](const std::string& x) { return same(x, name); })) at.push_back(name);
    }
    if (at.size() == 1) return at.front();
    if (why) {
        if (at.empty()) *why = "Aucune variable du projet n'est nettement la plus proche de " + quoted(std::string(missing));
        else *why = "Plusieurs variables conviennent autant (" + at[0] + ", " + at[1] + (at.size() > 2 ? std::string(", \xE2\x80\xA6") : std::string{}) + ")";
    }
    return {};
}

Action quick(const Proposal& p, const Check& ck, const Line& l, const hmi::Project* project) {
    Action q = entry(Action::Kind::Quick, "Corriger rapidement");
    const auto grey = [&](std::string why) {
        q.enabled = false;
        q.note = why;
        q.why = std::move(why);
        return q;
    };
    if (p.state != "proposee") return grey(kClosed);
    // UN CONSTAT DU LOGICIEL qui a sa correction : la poser.
    if (l.finding != Line::kNone && !l.fixFrom.empty()) {
        q.does = Action::Kind::BuildFix;
        q.ops = {l.finding};
        q.note = "Remplacer " + quoted(l.fixFrom) + " par " + quoted(l.fixTo) + " (la correction du logiciel).";
        return q;
    }
    const Check::Fault* variable = nullptr;
    const Check::Fault* first = nullptr;
    for (const auto k : l.faults) {
        if (k >= ck.faults.size()) continue;
        if (!first) first = &ck.faults[k];
        if (ck.faults[k].kind == "variable" && !variable) variable = &ck.faults[k];
    }
    // UNE VARIABLE INTROUVABLE : la nettement plus proche (toutes les variables du projet), sinon rien - on ne devine pas.
    if (variable) {
        std::vector<std::string> candidates = variable->closest;
        if (project) {
            candidates.clear();
            for (const auto& v : project->programs.variables) candidates.push_back(v.name);
        }
        std::string why;
        const std::string best = clearlyClosest(variable->name, candidates, &why);
        if (best.empty()) {
            if (!variable->closest.empty() && why.rfind("Aucune", 0) == 0) why += " (la plus proche, sans certitude : " + variable->closest.front() + ")";
            return grey(why + " : choisis-la (Choisir une variable\xE2\x80\xA6), ou cr\xC3\xA9" "e-la.");
        }
        q.does = Action::Kind::Use;
        q.name = variable->name;
        q.value = best;
        q.note = quoted(variable->name) + " \xE2\x86\x92 " + best + " : nettement la plus proche.";
        return q;
    }
    if (l.error) {
        if (l.ops.empty()) {
            if (first && first->kind == "perimee")
                return grey("La vue ou le projet a chang\xC3\xA9 depuis la proposition : rien \xC3\xA0 corriger ici, redemande-la (Modifier la demande).");
            if (l.finding != Line::kNone)
                return grey("Le logiciel ne propose pas de correction, et l'op\xC3\xA9ration en cause n'est pas connue : corrige \xC3\xA0 la main, ou demande \xC3\xA0 l'IA.");
            return grey("Cette erreur ne vient pas d'une op\xC3\xA9ration : rien de s\xC3\xBBr \xC3\xA0 changer sans deviner (Demander \xC3\xA0 l'IA de corriger).");
        }
        // UNE SUPPRESSION REFUSEE parce que l'element est cite ailleurs : la strategie << references >>.
        if (l.ops.size() == 1 && refusedRemoval(p, ck, l.ops.front())) {
            q.does = Action::Kind::Strategy;
            q.ops = l.ops;
            q.value = "references";
            q.note = "Strat\xC3\xA9gie \xC2\xAB r\xC3\xA9" "f\xC3\xA9rences \xC2\xBB (" + opLabel(p, l.ops.front())
                     + ") : l'\xC3\xA9l\xC3\xA9ment part avec les seules r\xC3\xA9" "f\xC3\xA9rences qui le citent.";
            return q;
        }
        // UNE AUTRE ERREUR D'UNE OPERATION (un constat d'erreur du logiciel sans correction aussi) : la retirer.
        q.does = Action::Kind::RemoveOps;
        q.ops = l.ops;
        q.note = l.ops.size() > 1 ? "Retirer ces " + std::to_string(l.ops.size()) + " op\xC3\xA9rations (ce qui en d\xC3\xA9pend est ignor\xC3\xA9)."
                                  : "Retirer l'" + opLabel(p, l.ops.front()) + " (ce qui en d\xC3\xA9pend est ignor\xC3\xA9).";
        return q;
    }
    // UN AVERTISSEMENT : des objets sous l'en-tete ou le pied (1.22.6), qu'une operation sait deplacer (<< decaler : non >>).
    for (const auto op : l.ops) {
        if (op >= p.ops.size()) continue;
        const Operation& o = p.ops[op];
        if ((o.action != Operation::Action::ViewEdit && o.action != Operation::Action::View) || !saysNo(propOf(o, "decaler"))) continue;
        if (std::none_of(ck.spots.begin(), ck.spots.end(), [&](const Check::Spot& s) { return s.op == op && s.message == l.message; })) continue;
        q.does = Action::Kind::Prop;
        q.ops = {op};
        q.name = "decaler";
        q.value = "oui";
        q.note = "D\xC3\xA9" "caler : oui (" + opLabel(p, op) + ") - les objets que la bande recouvre se d\xC3\xA9placent d'un m\xC3\xAA" "me d\xC3\xA9" "calage.";
        return q;
    }
    if (l.finding != Line::kNone) return grey("Le logiciel ne propose pas de correction pour ce constat : rien de s\xC3\xBBr \xC3\xA0 changer (Demander \xC3\xA0 l'IA de corriger).");
    // Des objets crees que rien ne relie a une variable : relier, c'est choisir la variable - jamais inventee.
    std::size_t spotted = 0, unbound = 0;
    for (const auto& s : ck.spots) {
        if (s.message != l.message || s.error) continue;
        ++spotted;
        if (std::any_of(ck.items.begin(), ck.items.end(), [&](const Item& it) {
                return it.name == s.object && (it.view.empty() || s.view.empty() || it.view == s.view) && it.what == Item::What::Create && it.variable.empty();
            }))
            ++unbound;
    }
    if (spotted > 0 && unbound == spotted)
        return grey("Rien de s\xC3\xBBr : relier ces objets \xC3\xA0 une variable, c'est la choisir - l'Assistant n'en invente pas. Associe-les \xC3\xA0 la main "
                    "apr\xC3\xA8s Appliquer, ou Demander \xC3\xA0 l'IA de corriger.");
    return grey("Rien de s\xC3\xBBr : le logiciel ne sait pas r\xC3\xA9gler cet avertissement sans deviner (ni variable, ni nom, ni place invent\xC3\xA9s). "
                "Corrige \xC3\xA0 la main, ou Demander \xC3\xA0 l'IA de corriger.");
}

Menu menuFor(const Proposal& p, const Check& ck, const Line& l, const hmi::Project* project) {
    Menu m;
    const bool open = p.state == "proposee";
    const Check::Fault* variable = nullptr;
    const Check::Fault* first = nullptr;
    for (const auto k : l.faults) {
        if (k >= ck.faults.size()) continue;
        if (!first) first = &ck.faults[k];
        if (ck.faults[k].kind == "variable" && !variable) variable = &ck.faults[k];
    }
    // 1.22.7 : CORRIGER RAPIDEMENT, en tete ; puis la correction du logiciel d'un constat qui en a une (1.22.5).
    m.actions.push_back(quick(p, ck, l, project));
    m.actions.push_back(entry(Action::Kind::Separator, {}));
    if (l.finding != Line::kNone && !l.fixFrom.empty()) {
        Action fix = entry(Action::Kind::BuildFix, "Appliquer la correction du logiciel", "remplacer " + quoted(l.fixFrom) + " par " + quoted(l.fixTo));
        fix.ops = {l.finding};
        m.actions.push_back(std::move(fix));
        m.actions.push_back(entry(Action::Kind::Separator, {}));
    }
    if (variable) {
        // UNE VARIABLE INTROUVABLE : une existante a la place, la choisir, la creer, ou retirer ce qui la lit.
        const std::string& name = variable->name;
        m.title = "Variable " + quoted(name) + " introuvable";
        const std::string made = creatableName(name);
        const bool madeExists = project && variableExists(*project, made);
        std::vector<std::string> closest = variable->closest;
        if (madeExists && std::none_of(closest.begin(), closest.end(), [&](const std::string& n) { return same(n, made); }))
            closest.insert(closest.begin(), made);
        if (closest.size() > 5) closest.resize(5);
        for (const auto& n : closest) {
            Action a = entry(Action::Kind::Use, "Utiliser " + quoted(n));
            a.name = name;
            a.value = n;
            m.actions.push_back(std::move(a));
        }
        {
            Action a = entry(Action::Kind::Pick, "Choisir une variable\xE2\x80\xA6", "Toutes les variables du projet, les plus proches d'abord.");
            a.name = name;
            m.actions.push_back(std::move(a));
        }
        if (!madeExists) {
            const std::string suggested = variable->type.empty() ? std::string("BOOL") : variable->type;
            Action a = entry(Action::Kind::Create, "Cr\xC3\xA9" "er la variable " + quoted(made),
                             made == trimmed(name) ? std::string("Dans les variables de l'IHM, \xC3\xA0 l'application.")
                                                   : "Un membre ne se cr\xC3\xA9" "e pas seul : " + made + " remplace " + name + ".");
            a.name = name;
            std::vector<std::string> order{suggested};
            for (const auto& t : types())
                if (t != suggested) order.push_back(t);
            for (const auto& t : order) {
                Action c = entry(Action::Kind::Create, t == suggested ? t + " (conseill\xC3\xA9)" : t);
                c.name = name;
                c.value = made;
                c.type = t;
                a.children.push_back(std::move(c));
            }
            m.actions.push_back(std::move(a));
        }
        if (!l.ops.empty()) {
            Action a = entry(Action::Kind::RemoveUsing, l.ops.size() > 1 ? "Retirer les " + std::to_string(l.ops.size()) + " op\xC3\xA9rations qui l'utilisent"
                                                                         : std::string("Retirer l'op\xC3\xA9ration qui l'utilise"),
                             l.ops.size() == 1 ? opLabel(p, l.ops.front()) : std::string("Ce qui en d\xC3\xA9pend est ignor\xC3\xA9."));
            a.name = name;
            a.ops = l.ops;
            m.actions.push_back(std::move(a));
        }
    } else {
        m.title = first && first->kind == "code" ? std::string("Code brut") : l.error ? std::string("Erreur") : std::string("Avertissement");
        if (!l.ops.empty()) {
            // UNE ERREUR D'UNE OPERATION : la retirer (ce qui en depend suit).
            Action a = entry(Action::Kind::RemoveOps, l.ops.size() > 1 ? "Retirer ces " + std::to_string(l.ops.size()) + " op\xC3\xA9rations"
                                                                       : std::string("Retirer cette op\xC3\xA9ration"),
                             l.ops.size() == 1 ? opLabel(p, l.ops.front()) + " ; ce qui en d\xC3\xA9pend est ignor\xC3\xA9."
                                               : std::string("Ce qui en d\xC3\xA9pend est ignor\xC3\xA9."));
            a.ops = l.ops;
            m.actions.push_back(std::move(a));
        }
    }
    for (auto& a : m.actions)
        if (!open) {
            a.enabled = false;
            a.why = kClosed;
        }
    if (!m.actions.empty() && m.actions.back().kind != Action::Kind::Separator) m.actions.push_back(entry(Action::Kind::Separator, {}));
    {
        Action a = entry(Action::Kind::Copy, "Copier le message");
        a.value = l.message;
        m.actions.push_back(std::move(a));
    }
    if (open) {
        Action a = entry(Action::Kind::AskAi, "Demander \xC3\xA0 l'IA de corriger", "Le texte va dans la saisie : rien ne part seul.");
        a.value = askText(l);
        m.actions.push_back(std::move(a));
    }
    return m;
}

std::vector<Correction> corrections(const Proposal& p) {
    std::vector<Correction> out;
    for (const auto& c : p.choices) {
        Correction x;
        x.key = "c:" + foldedName(c.from) + "|" + trimmed(c.from);   // 1.22.7
        if (c.create) {
            const std::string made = trimmed(c.use).empty() ? creatableName(c.from) : trimmed(c.use);
            x.text = quoted(c.from) + " \xE2\x86\x92 " + made;
            x.state = "\xC3\xA0 cr\xC3\xA9" "er (" + (c.type.empty() ? std::string("BOOL") : c.type) + ")";
            x.what = 0;
        } else {
            x.text = quoted(c.from) + " \xE2\x86\x92 " + c.use;
            x.state = "remplac\xC3\xA9" "e";
            x.what = 1;
        }
        x.undo = entry(Action::Kind::Revert, "Revenir", quoted(c.from) + " redevient introuvable.");
        x.undo.name = c.from;
        out.push_back(std::move(x));
    }
    for (const auto op : p.excluded) {
        if (op >= p.ops.size()) continue;
        Correction x;
        x.text = opLabel(p, op);
        x.state = "retir\xC3\xA9" "e";
        x.what = 2;
        x.key = "x:" + std::to_string(op);   // 1.22.7
        x.undo = entry(Action::Kind::Restore, "Revenir", "L'op\xC3\xA9ration revient dans la proposition.");
        x.undo.ops = {op};
        out.push_back(std::move(x));
    }
    // 1.22.7 : LES RETOUCHES (une strategie, une propriete, la correction du logiciel) - Revenir rend a l'operation sa forme d'avant.
    if (const json::Value* list = p.extra.isObject() ? p.extra.find("retouches") : nullptr; list && list->isArray())
        for (const auto& r : list->items()) {
            const std::int64_t at = r.isObject() ? r.integer("op", -1) : -1;
            const std::string  id = r.isObject() ? r.str("id") : std::string{};
            if (id.empty() || at < 0 || static_cast<std::size_t>(at) >= p.ops.size()) continue;
            const std::size_t op = static_cast<std::size_t>(at);
            const std::string what = r.str("quoi");
            Correction x;
            x.text = opLabel(p, op) + " : " + r.str("texte");
            x.state = what == "strategie" ? std::string("strat\xC3\xA9gie chang\xC3\xA9" "e")
                    : what == "logiciel"  ? std::string("corrig\xC3\xA9" "e (logiciel)")
                                          : std::string("retouch\xC3\xA9" "e");
            x.what = 3;
            x.op = op;
            x.key = "r:" + id;
            x.undo = entry(Action::Kind::Undo, "Revenir", "L'op\xC3\xA9ration reprend sa forme d'avant.");
            x.undo.value = id;
            x.undo.ops = {op};
            out.push_back(std::move(x));
        }
    return out;
}

Menu menuFor(const Proposal& p, const Correction& c) {
    Menu m;
    m.title = c.text + " \xE2\x80\x94 " + c.state;
    Action undo = c.undo;
    if (p.state != "proposee") {
        undo.enabled = false;
        undo.why = kClosed;
    }
    m.actions.push_back(std::move(undo));
    m.actions.push_back(entry(Action::Kind::Separator, {}));
    Action copy = entry(Action::Kind::Copy, "Copier");
    copy.value = c.text + " \xE2\x80\x94 " + c.state;
    m.actions.push_back(std::move(copy));
    return m;
}

std::string summary(const Proposal& p) {
    std::size_t replaced = 0, created = 0;
    for (const auto& c : p.choices) (c.create ? created : replaced) += 1;
    std::vector<std::string> parts;
    if (replaced) parts.push_back(plural(replaced, "variable remplac\xC3\xA9" "e", "variables remplac\xC3\xA9" "es"));
    if (created) parts.push_back(plural(created, "variable \xC3\xA0 cr\xC3\xA9" "er", "variables \xC3\xA0 cr\xC3\xA9" "er"));
    std::size_t removed = 0;
    for (const auto op : p.excluded) removed += op < p.ops.size() ? 1 : 0;
    if (removed) parts.push_back(plural(removed, "op\xC3\xA9ration retir\xC3\xA9" "e", "op\xC3\xA9rations retir\xC3\xA9" "es"));
    // 1.22.7 : les retouches
    std::size_t strategies = 0, software = 0, touched = 0;
    for (const auto& c : corrections(p)) {
        if (c.what != 3) continue;
        if (c.state.rfind("strat", 0) == 0) ++strategies;
        else if (c.state.find("logiciel") != std::string::npos) ++software;
        else ++touched;
    }
    if (strategies) parts.push_back(plural(strategies, "strat\xC3\xA9gie chang\xC3\xA9" "e", "strat\xC3\xA9gies chang\xC3\xA9" "es"));
    if (software) parts.push_back(plural(software, "correction du logiciel", "corrections du logiciel"));
    if (touched) parts.push_back(plural(touched, "op\xC3\xA9ration retouch\xC3\xA9" "e", "op\xC3\xA9rations retouch\xC3\xA9" "es"));
    std::string out;
    for (const auto& s : parts) out += (out.empty() ? "" : ", ") + s;
    return out;
}

std::string key(const Proposal& p) {
    std::string k;
    for (const auto& c : p.choices) k += "|c" + c.from + ">" + c.use + (c.create ? "+" + c.type : std::string{});
    // 1.22.7 : les retouches (une operation changee dans la proposition) - la carte et l'apercu se revalident.
    if (const json::Value* list = p.extra.isObject() ? p.extra.find("retouches") : nullptr; list && list->isArray())
        for (const auto& r : list->items())
            if (r.isObject()) k += "|r" + r.str("id") + ":" + std::to_string(r.integer("op", -1));
    return k;
}

bool apply(Assistant& a, int message, const Action& act, std::string* said) {
    bool ok = false;
    std::string text;
    switch (act.kind) {
        case Action::Kind::Use: {
            VariableChoice c;
            c.use = act.value;
            ok = a.chooseVariable(message, act.name, c);
            text = quoted(act.name) + " remplac\xC3\xA9" "e par " + act.value + " : la proposition se rev\xC3\xA9rifie.";
            break;
        }
        case Action::Kind::Create: {
            VariableChoice c;
            c.create = true;
            c.use = act.value;
            c.type = act.type.empty() ? std::string("BOOL") : act.type;
            ok = a.chooseVariable(message, act.name, c);
            text = (act.value.empty() ? act.name : act.value) + " (" + c.type + ") sera cr\xC3\xA9\xC3\xA9" "e \xC3\xA0 l'application, \xC3\xA0 ta demande : "
                   "la proposition se rev\xC3\xA9rifie.";
            break;
        }
        case Action::Kind::RemoveUsing:
        case Action::Kind::RemoveOps:
            ok = a.excludeOps(message, act.ops, true);
            text = act.ops.size() > 1 ? std::to_string(act.ops.size()) + " op\xC3\xA9rations retir\xC3\xA9" "es : ce qui en d\xC3\xA9pend est ignor\xC3\xA9."
                                      : std::string("Op\xC3\xA9ration retir\xC3\xA9" "e : ce qui en d\xC3\xA9pend est ignor\xC3\xA9.");
            break;
        case Action::Kind::Revert:
            ok = a.clearChoice(message, act.name);
            text = "Correction reprise : " + quoted(act.name) + " redevient introuvable.";
            break;
        case Action::Kind::Restore:
            ok = a.excludeOps(message, act.ops, false);
            text = act.ops.size() > 1 ? std::string("Op\xC3\xA9rations remises dans la proposition.") : std::string("Op\xC3\xA9ration remise dans la proposition.");
            break;
        case Action::Kind::BuildFix: {
            // 1.22.5 : la correction du logiciel, posee dans la PROPOSITION (jamais dans le projet) ; l'avis est redemande.
            std::string why;
            ok = !act.ops.empty() && a.applyBuildFix(message, act.ops.front(), &why);
            if (!ok) {
                if (said) *said = "Correction du logiciel impossible : " + (why.empty() ? std::string(kClosed) : why) + ".";
                return false;
            }
            text = "Correction du logiciel pos\xC3\xA9" "e dans la proposition (" + act.note + ") : G\xC3\xA9n\xC3\xA9rer et Compiler la rev\xC3\xA9rifient.";
            break;
        }
        // 1.22.7 : CORRIGER RAPIDEMENT - ce qu'elle fait (une des actions du menu, ou une retouche).
        case Action::Kind::Quick: {
            Action x = act;
            x.kind = act.does;
            x.does = Action::Kind::Separator;
            if (!act.enabled || x.kind == Action::Kind::Quick || x.kind == Action::Kind::Separator) {
                if (said) *said = "Rien n'a chang\xC3\xA9 : " + (act.why.empty() ? std::string(kClosed) : act.why);
                return false;
            }
            return apply(a, message, x, said);
        }
        case Action::Kind::Strategy:
        case Action::Kind::Prop: {
            std::string why;
            const std::size_t op = act.ops.empty() ? Check::Fault::kNoOp : act.ops.front();
            const std::string label = a.proposal(message) ? opLabel(*a.proposal(message), op) : std::string{};
            ok = op != Check::Fault::kNoOp && (act.kind == Action::Kind::Strategy ? a.setDeleteStrategy(message, op, act.value, &why)
                                                                                : a.setOperationProperty(message, op, act.name, act.value, &why));
            if (!ok) {
                if (said) *said = "Rien n'a chang\xC3\xA9 : " + (why.empty() ? std::string(kClosed) : why);
                return false;
            }
            text = act.kind == Action::Kind::Strategy
                       ? "Strat\xC3\xA9gie \xC2\xAB " + std::string(act.value == "references" ? "r\xC3\xA9" "f\xC3\xA9rences" : act.value) + " \xC2\xBB pos\xC3\xA9" "e ("
                             + label + ") : la proposition se rev\xC3\xA9rifie."
                       : act.name + " : " + act.value + " (" + label + ") : la proposition se rev\xC3\xA9rifie.";
            break;
        }
        case Action::Kind::Undo: {
            std::string why;
            ok = a.undoRetouch(message, act.value, &why);
            if (!ok) {
                if (said) *said = "Rien n'a chang\xC3\xA9 : " + (why.empty() ? std::string(kClosed) : why);
                return false;
            }
            text = "Retouche reprise : l'op\xC3\xA9ration reprend sa forme d'avant.";
            break;
        }
        default:
            return false;
    }
    if (said) *said = ok ? text : std::string("Rien n'a chang\xC3\xA9 : la proposition n'est plus \xC3\xA0 d\xC3\xA9" "cider.");
    return ok;
}

std::size_t applyAll(Assistant& a, int message, const std::vector<Action>& quick, std::string* said) {
    std::size_t fixed = 0;
    std::string last;
    const auto usable = [](const Action& q) { return q.kind == Action::Kind::Quick && q.enabled; };
    // Les corrections du logiciel d'abord, en une fois : tout autre changement de la proposition efface le verdict.
    std::vector<std::size_t> findings;
    for (const auto& q : quick)
        if (usable(q) && q.does == Action::Kind::BuildFix && !q.ops.empty()) findings.push_back(q.ops.front());
    if (!findings.empty()) fixed += a.applyBuildFixes(message, findings, &last);
    // Puis les variables, les strategies, les proprietes ; les retraits en une seule ecriture.
    std::vector<std::size_t> removed;
    std::size_t              removeLines = 0;
    for (const auto& q : quick) {
        if (!usable(q)) continue;
        switch (q.does) {
            case Action::Kind::Use:
            case Action::Kind::Strategy:
            case Action::Kind::Prop:
                if (apply(a, message, q, &last)) ++fixed;
                break;
            case Action::Kind::RemoveOps:
                ++removeLines;
                for (const auto op : q.ops)
                    if (std::find(removed.begin(), removed.end(), op) == removed.end()) removed.push_back(op);
                break;
            default: break;
        }
    }
    if (!removed.empty() && a.excludeOps(message, removed, true)) fixed += removeLines;
    if (said)
        *said = fixed == 0 ? "Rien n'a \xC3\xA9t\xC3\xA9 corrig\xC3\xA9" + (last.empty() ? std::string(".") : " : " + last)
                           : plural(fixed, "correction rapide pos\xC3\xA9" "e", "corrections rapides pos\xC3\xA9" "es")
                                 + " dans la proposition (le projet ne change pas) : elle se rev\xC3\xA9rifie.";
    return fixed;
}

void fill(ui::PopupMenu& menu, const Menu& m, std::vector<Action>& byId) {
    byId.clear();
    std::vector<ui::PopupMenu::Item> items;
    if (!m.title.empty()) {
        ui::PopupMenu::Item head;
        head.heading = true;
        head.label = m.title;
        items.push_back(std::move(head));
    }
    std::function<ui::PopupMenu::Item(const Action&)> item = [&](const Action& a) {
        ui::PopupMenu::Item it;
        if (a.kind == Action::Kind::Separator) {
            it.separator = true;
            return it;
        }
        it.label = a.label;
        it.note = a.note;
        it.icon = iconOf(a.kind);
        it.enabled = a.enabled;
        it.disabledReason = a.why;
        if (a.children.empty()) {
            it.id = static_cast<int>(byId.size());
            byId.push_back(a);
        } else {
            for (const auto& c : a.children) it.children.push_back(item(c));
        }
        return it;
    };
    for (const auto& a : m.actions) items.push_back(item(a));
    menu.setItems(std::move(items));
}

std::vector<std::string> labels(const ui::PopupMenu& menu) {
    std::vector<std::string> out;
    std::function<void(const std::vector<ui::PopupMenu::Item>&, const std::string&)> walk = [&](const std::vector<ui::PopupMenu::Item>& items,
                                                                                               const std::string& path) {
        for (const auto& it : items) {
            if (it.separator || it.heading) continue;
            const std::string label = path.empty() ? it.label : path + " > " + it.label;
            if (it.children.empty()) out.push_back(label);
            else walk(it.children, label);
        }
    };
    walk(menu.items(), {});
    return out;
}

int idOf(const ui::PopupMenu& menu, std::string_view label) {
    int found = -1;
    std::function<void(const std::vector<ui::PopupMenu::Item>&, const std::string&)> walk = [&](const std::vector<ui::PopupMenu::Item>& items,
                                                                                               const std::string& path) {
        for (const auto& it : items) {
            if (found >= 0) return;
            if (it.separator || it.heading) continue;
            const std::string full = path.empty() ? it.label : path + " > " + it.label;
            if (it.children.empty()) {
                if (full == label && it.enabled) found = it.id;
            } else {
                walk(it.children, full);
            }
        }
    };
    walk(menu.items(), {});
    return found;
}

} // namespace app::ai::fixes
