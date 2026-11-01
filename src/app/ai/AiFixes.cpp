// =============================================================================
//  app/ai/AiFixes.cpp - voir AiFixes.hpp
// =============================================================================
#include "AiFixes.hpp"

#include "AiAssistant.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
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
        default: return ui::Icon::None;
    }
}

const char* const kClosed = "la proposition n'est plus \xC3\xA0 d\xC3\xA9" "cider";

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
    if (!m.actions.empty()) m.actions.push_back(entry(Action::Kind::Separator, {}));
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
        x.undo = entry(Action::Kind::Restore, "Revenir", "L'op\xC3\xA9ration revient dans la proposition.");
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
    std::string out;
    for (const auto& s : parts) out += (out.empty() ? "" : ", ") + s;
    return out;
}

std::string key(const Proposal& p) {
    std::string k;
    for (const auto& c : p.choices) k += "|c" + c.from + ">" + c.use + (c.create ? "+" + c.type : std::string{});
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
        default:
            return false;
    }
    if (said) *said = ok ? text : std::string("Rien n'a chang\xC3\xA9 : la proposition n'est plus \xC3\xA0 d\xC3\xA9" "cider.");
    return ok;
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
