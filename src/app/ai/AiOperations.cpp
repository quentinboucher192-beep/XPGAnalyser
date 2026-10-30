// =============================================================================
//  app/ai/AiOperations.cpp - voir AiOperations.hpp
// =============================================================================
#include "AiOperations.hpp"

#include "../../hmi/HmiEdit.hpp"
#include "../../hmi/HmiRoles.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

namespace app::ai {

namespace {

const char* const kArrow = " \xE2\x86\x92 ";   // " -> "

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

hmi::Object* findObject(hmi::View& v, std::string_view name) {
    for (auto& o : v.objects)
        if (o.name == name) return &o;
    for (auto& o : v.objects)
        if (iequal(o.name, name)) return &o;
    return nullptr;
}

std::string label(const hmi::Object& o) {
    return o.name + " (" + std::string(hmi::kindLabel(o.kind)) + ")";
}

// La valeur convient-elle au role de la cle (une couleur, un nombre, un booleen) ? Vide : oui.
std::string badValue(hmi::Kind k, std::string_view key, std::string_view value) {
    if (value.empty()) return {};
    const auto r = hmi::roles::roleOf(k, key);
    switch (r.role) {
        case hmi::roles::Role::Color:
            if (!hmi::roles::colorOk(value)) return "une couleur #RRGGBB est attendue";
            break;
        case hmi::roles::Role::Number: {
            double d = 0;
            if (!hmi::roles::readNumber(value, d)) return "un nombre est attendu";
            break;
        }
        case hmi::roles::Role::Boolean: {
            const std::string l = lower(value);
            if (l != "true" && l != "false" && l != "0" && l != "1" && l != "vrai" && l != "faux") return "TRUE ou FALSE est attendu";
            break;
        }
        default:
            break;
    }
    return {};
}

bool valueBearing(hmi::Kind k) {
    return hmi::kindIsSynoptic(k) || hmi::kindShowsValue(k) || hmi::kindWritesVariable(k);
}

// Le nom d'une variable, ramene a sa racine (Pompe.Marche, Pompes[3] -> Pompe, Pompes).
std::string rootOf(std::string_view name) {
    std::string_view n = name;
    if (n.size() > 4 && iequal(n.substr(0, 4), "API.")) n.remove_prefix(4);
    const auto cut = n.find_first_of(".[");
    return std::string(n.substr(0, cut));
}

std::size_t distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const bool same = std::tolower(static_cast<unsigned char>(a[i - 1])) == std::tolower(static_cast<unsigned char>(b[j - 1]));
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (same ? 0 : 1)});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    std::string w;
    for (const char c : s) {
        if (std::isalpha(static_cast<unsigned char>(c))) w += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else {
            if (w.size() >= 2) out.push_back(w);
            w.clear();
        }
    }
    if (w.size() >= 2) out.push_back(w);
    return out;
}

// ---- le moteur : valider (check) ou appliquer (touched), une seule lecture des operations ----
struct Run {
    const hmi::Project& project;
    hmi::View&          view;
    hmi::Id&            nextId;
    const Proposal&     p;
    const Env*          env{nullptr};
    Check*              check{nullptr};     // valider : tout noter, continuer
    Touched*            touched{nullptr};
    bool                skipRemovals{false};
    bool                preview{false};     // l'apercu : comme valider (continuer), sans rien dire
    std::string         why{};
    std::set<hmi::Id>   createdIds{}, modifiedIds{}, removedIds{};

    bool fail(std::string text) {
        if (preview && !check) return true;   // l'apercu montre ce qui peut l'etre
        if (check) {
            check->errors.push_back(std::move(text));
            return true;     // on continue : toutes les fautes sont dites
        }
        if (why.empty()) why = std::move(text);
        return false;
    }
    void warn(std::string text) {
        if (check) check->warnings.push_back(std::move(text));
    }
    Item* itemFor(Item::What what, const hmi::Object& o) {
        if (!check) return nullptr;
        for (auto& it : check->items)
            if (it.id == o.id && it.what == what) return &it;
        Item it;
        it.what = what;
        it.id = o.id;
        it.name = o.name;
        it.kind = o.kind;
        check->items.push_back(std::move(it));
        return &check->items.back();
    }
    void noteModified(const hmi::Object& o) {
        if (createdIds.count(o.id)) return;   // un objet cree puis retouche reste "cree"
        modifiedIds.insert(o.id);
    }

    bool create(const Operation& op) {
        const auto kind = hmi::kindFromKey(op.kind);
        if (!kind) return fail("Genre d'objet inconnu : \xC2\xAB " + op.kind + " \xC2\xBB (les genres sont ceux de la biblioth\xC3\xA8que).");
        double x = 40, y = 40;
        for (const auto& [k, v] : op.props) {
            double d = 0;
            if (k == "x" && hmi::roles::readNumber(v, d)) x = d;
            if (k == "y" && hmi::roles::readNumber(v, d)) y = d;
        }
        // Le nom voulu s'il est libre et valable ; sinon le premier libre a partir de lui (Cuve_1_1).
        const std::string base = op.name.empty() ? std::string(hmi::kindKey(*kind)) : op.name;
        const bool taken = op.name.empty() || !hmi::edit::validName(base)
                           || std::any_of(view.objects.begin(), view.objects.end(), [&](const hmi::Object& x2) { return x2.name == base; });
        hmi::Object o = hmi::makeObject(*kind, nextId++, taken ? hmi::uniqueObjectName(view, base) : base, x, y, view.activeLayer);
        if (o.layer == hmi::kNoId && !view.layers.empty()) o.layer = view.layers.front().id;
        std::vector<Change> given;
        for (const auto& [k, v] : op.props) {
            hmi::Prop* prop = o.find(k);
            if (!prop) {
                if (!fail("\xC2\xAB " + k + " \xC2\xBB n'est pas une propri\xC3\xA9t\xC3\xA9 d'un objet " + std::string(hmi::kindLabel(*kind))
                          + " : refus\xC3\xA9" "e (aucune propri\xC3\xA9t\xC3\xA9 n'est invent\xC3\xA9" "e)."))
                    return false;
                continue;
            }
            if (const auto bad = badValue(*kind, k, v); !bad.empty()) {
                if (!fail(o.name + "." + k + " = \xC2\xAB " + v + " \xC2\xBB : " + bad + ".")) return false;
                continue;
            }
            prop->value = v;
            given.push_back({k, {}, v});
        }
        const hmi::Box b = o.box();
        if (b.x < 0 || b.y < 0 || b.right() > view.width || b.bottom() > view.height)
            warn(o.name + " d\xC3\xA9" "passe du cadre de la vue (" + std::to_string(view.width) + " \xC3\x97 " + std::to_string(view.height) + ").");
        createdIds.insert(o.id);
        if (Item* it = itemFor(Item::What::Create, o)) it->changes = std::move(given);
        if (touched) touched->created.push_back(o.id);
        (void)hmi::edit::addObject(view, std::move(o));
        return true;
    }

    bool modify(const Operation& op) {
        hmi::Object* o = findObject(view, op.name);
        if (!o) return fail("Objet introuvable dans la vue : \xC2\xAB " + op.name + " \xC2\xBB.");
        for (const auto& [k, v] : op.props) {
            hmi::Prop* prop = o->find(k);
            if (!prop) {
                if (!fail("\xC2\xAB " + k + " \xC2\xBB n'est pas une propri\xC3\xA9t\xC3\xA9 de " + label(*o) + ".")) return false;
                continue;
            }
            if (const auto bad = badValue(o->kind, k, v); !bad.empty()) {
                if (!fail(o->name + "." + k + " = \xC2\xAB " + v + " \xC2\xBB : " + bad + ".")) return false;
                continue;
            }
            if (Item* it = itemFor(createdIds.count(o->id) ? Item::What::Create : Item::What::Modify, *o))
                it->changes.push_back({k, prop->value, v});
            prop->value = v;
        }
        noteModified(*o);
        return true;
    }

    bool bind(const Operation& op) {
        hmi::Object* o = findObject(view, op.name);
        if (!o) return fail("Objet introuvable dans la vue : \xC2\xAB " + op.name + " \xC2\xBB.");
        hmi::Prop* prop = o->find("variable");
        if (!prop) return fail(label(*o) + " ne se relie pas \xC3\xA0 une variable.");
        std::string name = op.variable;
        const bool exists = variableExists(project, name, env ? *env : Env{});
        if (!exists) {
            if (!p.variable.use.empty()) {
                if (!variableExists(project, p.variable.use, env ? *env : Env{}))
                    return fail("La variable choisie \xC2\xAB " + p.variable.use + " \xC2\xBB n'existe pas non plus.");
                name = p.variable.use;
            } else if (p.variable.create) {
                if (!hmi::isIdentifier(name)) return fail("\xC2\xAB " + name + " \xC2\xBB n'est pas un nom de variable valable.");
                warn("La variable " + name + " (" + p.variable.type + ") sera cr\xC3\xA9\xC3\xA9" "e dans les variables de l'IHM, \xC3\xA0 votre demande.");
            } else {
                if (check) {
                    check->missingVariable = name;
                    check->nearVariables = nearVariables(project, name);
                    // L'objet vise se montre quand meme (l'apercu l'encadre) : il changera quand la variable sera choisie.
                    if (Item* it = itemFor(createdIds.count(o->id) ? Item::What::Create : Item::What::Modify, *o)) {
                        it->changes.push_back({"variable", prop->value, name + " (introuvable)"});
                        it->variable = name;
                    }
                    noteModified(*o);
                }
                if (touched && preview) touched->modified.push_back(o->id);
                return fail("La variable \xC2\xAB " + name + " \xC2\xBB n'existe pas dans le projet : l'Assistant ne l'invente pas. "
                            "Choisissez une variable existante, ou demandez sa cr\xC3\xA9" "ation.");
            }
        }
        if (Item* it = itemFor(createdIds.count(o->id) ? Item::What::Create : Item::What::Modify, *o)) {
            it->changes.push_back({"variable", prop->value, name});
            it->variable = name;
        }
        if (check) check->bindings.push_back(o->name + kArrow + name + (exists ? std::string{} : p.variable.create ? " (\xC3\xA0 cr\xC3\xA9" "er)" : std::string{}));
        prop->value = name;
        prop->expr.clear();
        noteModified(*o);
        return true;
    }

    bool resolve(const std::vector<std::string>& names, std::vector<hmi::Id>& ids) {
        for (const auto& n : names) {
            hmi::Object* o = findObject(view, n);
            if (!o) {
                if (!fail("Objet introuvable dans la vue : \xC2\xAB " + n + " \xC2\xBB.")) return false;
                continue;
            }
            ids.push_back(o->id);
        }
        return true;
    }

    bool arrange(const Operation& op) {
        std::vector<hmi::Id> ids;
        if (!resolve(op.objects, ids)) return false;
        if (ids.size() < 2) return fail("Il faut au moins deux objets \xC3\xA0 " + std::string(op.action == Operation::Action::Align ? "aligner." : "r\xC3\xA9partir."));
        std::vector<std::pair<hmi::Id, hmi::Box>> before;
        for (const auto id : ids)
            for (const auto& o : view.objects)
                if (o.id == id) before.emplace_back(id, o.box());
        if (op.action == Operation::Action::Align) {
            using A = hmi::edit::Align;
            const std::string e = lower(op.edge);
            const A a = e == "bas" ? A::Bottom : e == "gauche" ? A::Left : e == "droite" ? A::Right
                      : e == "centre-h" ? A::CenterH : e == "centre-v" ? A::CenterV : A::Top;
            hmi::edit::align(view, ids, a);
        } else {
            hmi::edit::distribute(view, ids, lower(op.edge) != "vertical");
        }
        for (const auto& [id, box] : before)
            for (const auto& o : view.objects) {
                if (o.id != id) continue;
                const hmi::Box nb = o.box();
                if (Item* it = itemFor(createdIds.count(id) ? Item::What::Create : Item::What::Modify, o)) {
                    if (nb.x != box.x) it->changes.push_back({"x", hmi::formatNumber(box.x), hmi::formatNumber(nb.x)});
                    if (nb.y != box.y) it->changes.push_back({"y", hmi::formatNumber(box.y), hmi::formatNumber(nb.y)});
                }
                noteModified(o);
            }
        return true;
    }

    bool remove(const Operation& op) {
        std::vector<hmi::Id> ids;
        for (const auto& n : op.objects) {
            hmi::Object* o = findObject(view, n);
            if (!o) {
                warn("\xC2\xAB " + n + " \xC2\xBB n'est plus dans la vue (rien \xC3\xA0 supprimer).");
                continue;
            }
            ids.push_back(o->id);
            removedIds.insert(o->id);
            itemFor(Item::What::Remove, *o);
            if (touched) touched->removed.push_back(o->id);
        }
        if (!skipRemovals && !ids.empty()) hmi::edit::remove(view, ids);
        return true;
    }

    bool go() {
        for (const auto& op : p.ops) {
            bool ok = true;
            switch (op.action) {
                case Operation::Action::Create: ok = create(op); break;
                case Operation::Action::Modify: ok = modify(op); break;
                case Operation::Action::Bind: ok = bind(op); break;
                case Operation::Action::Align:
                case Operation::Action::Distribute: ok = arrange(op); break;
                case Operation::Action::Remove: ok = remove(op); break;
            }
            if (!ok) return false;
        }
        for (const auto id : modifiedIds)
            if (removedIds.count(id) == 0 && touched && std::find(touched->modified.begin(), touched->modified.end(), id) == touched->modified.end())
                touched->modified.push_back(id);
        if (check) {
            check->create = static_cast<int>(createdIds.size());
            check->modify = static_cast<int>(std::count_if(modifiedIds.begin(), modifiedIds.end(), [&](hmi::Id id) { return !removedIds.count(id); }));
            check->remove = static_cast<int>(removedIds.size());
            check->confirmRemoval = removedIds.size() >= 2;
            // Les objets crees qui montrent ou ecrivent une valeur, sans variable : dit, jamais invente.
            int unbound = 0;
            for (const auto& o : view.objects)
                if (createdIds.count(o.id) && valueBearing(o.kind) && o.text("variable").empty() && o.expr("value").empty()) ++unbound;
            if (unbound > 0)
                check->warnings.push_back(std::to_string(unbound) + (unbound > 1 ? " objets cr\xC3\xA9\xC3\xA9s ne sont reli\xC3\xA9s"
                                                                             : " objet cr\xC3\xA9\xC3\xA9 n'est reli\xC3\xA9")
                                          + " \xC3\xA0 aucune variable : \xC3\xA0 associer ensuite (l'Assistant n'invente pas de variable).");
        }
        return true;
    }
};

} // namespace

std::string_view actionKey(Operation::Action a) noexcept {
    switch (a) {
        case Operation::Action::Create: return "creer";
        case Operation::Action::Modify: return "modifier";
        case Operation::Action::Bind: return "associer";
        case Operation::Action::Align: return "aligner";
        case Operation::Action::Distribute: return "distribuer";
        case Operation::Action::Remove: return "supprimer";
    }
    return "creer";
}

std::optional<Operation::Action> actionFromKey(std::string_view k) noexcept {
    for (const auto a : {Operation::Action::Create, Operation::Action::Modify, Operation::Action::Bind, Operation::Action::Align,
                         Operation::Action::Distribute, Operation::Action::Remove})
        if (actionKey(a) == k) return a;
    return std::nullopt;
}

std::string fingerprint(const hmi::View& v) {
    // FNV-1a sur 64 bits : l'identifiant, la taille, et chaque objet (genre, nom, calque,
    // parent, etats, proprietes et formules) dans l'ordre de dessin.
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::string_view s) {
        for (const char c : s) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ull;
        }
        h ^= 0xFF;
        h *= 1099511628211ull;
    };
    mix(std::to_string(v.id));
    mix(std::to_string(v.width) + "x" + std::to_string(v.height));
    for (const auto& o : v.objects) {
        mix(std::to_string(o.id));
        mix(hmi::kindKey(o.kind));
        mix(o.name);
        mix(std::to_string(o.layer) + "/" + std::to_string(o.parent) + (o.locked ? "L" : "") + (o.hidden ? "H" : ""));
        for (const auto& pr : o.props) {
            mix(pr.key);
            mix(pr.value);
            mix(pr.expr);
        }
    }
    char buf[20];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

bool variableExists(const hmi::Project& p, std::string_view name, const Env& env) {
    if (name.empty()) return false;
    const std::string root = rootOf(name);
    if (p.variable(root)) return true;
    if (env.plcName && (env.plcName(root) || env.plcName(name))) return true;
    return false;
}

std::vector<std::string> nearVariables(const hmi::Project& p, std::string_view name, std::size_t max) {
    struct Scored { std::size_t score; std::string name; };
    std::vector<Scored> all;
    const auto mine = words(name);
    for (const auto& v : p.programs.variables) {
        const std::size_t d = distance(lower(v.name), lower(name));
        std::size_t shared = 0;
        for (const auto& w : words(v.name))
            for (const auto& m : mine)
                if (w == m || (w.size() >= 4 && m.rfind(w, 0) == 0) || (m.size() >= 4 && w.rfind(m, 0) == 0)) ++shared;
        // Moins c'est mieux : la distance, moins trois par mot partage.
        const std::size_t score = d + 50 - std::min<std::size_t>(50, shared * 3);
        if (shared == 0 && d > name.size() / 2 + 2) continue;
        all.push_back({score, v.name});
    }
    std::stable_sort(all.begin(), all.end(), [](const Scored& a, const Scored& b) { return a.score < b.score; });
    std::vector<std::string> out;
    for (const auto& s : all) {
        if (out.size() >= max) break;
        out.push_back(s.name);
    }
    return out;
}

const hmi::View* targetView(const hmi::Project& p, const Proposal& pr) {
    if (pr.viewId != hmi::kNoId)
        if (const auto* v = p.view(pr.viewId); v && (pr.view.empty() || v->name == pr.view)) return v;
    if (!pr.view.empty()) return p.viewByName(pr.view);
    return nullptr;
}

Check validate(const hmi::Project& project, const Proposal& p, const Env& env) {
    Check check;
    const hmi::View* v = targetView(project, p);
    if (!v) {
        check.viewMissing = true;
        check.errors.push_back("La vue \xC2\xAB " + p.view + " \xC2\xBB n'existe plus dans le projet : rien ne peut s'appliquer.");
        return check;
    }
    if (!p.fingerprint.empty() && fingerprint(*v) != p.fingerprint) {
        check.stale = true;
        check.errors.push_back("La vue \xC2\xAB " + v->name + " \xC2\xBB a chang\xC3\xA9 depuis cette proposition : elle ne s'applique pas "
                               "sur un \xC3\xA9tat qui n'est plus le sien. Redemandez-la.");
    }
    hmi::View copy = *v;
    hmi::Id next = project.nextId;
    Run run{project, copy, next, p, &env, &check};
    (void)run.go();
    return check;
}

bool applyOps(const hmi::Project& project, hmi::View& view, hmi::Id& nextId, const Proposal& p, Touched* touched, std::string* why,
              bool skipRemovals) {
    Run run{project, view, nextId, p, nullptr, nullptr, touched, skipRemovals};
    const bool ok = run.go();
    if (!ok && why) *why = run.why;
    return ok;
}

core::CommandPtr makeCommand(const hmi::DocumentPtr& doc, const Proposal& p, std::string* why) {
    if (!doc) {
        if (why) *why = "Pas de projet IHM ouvert.";
        return nullptr;
    }
    const Check check = validate(doc->project, p);
    if (!check.ok()) {
        if (why) *why = check.errors.front();
        return nullptr;
    }
    const hmi::View* v = targetView(doc->project, p);
    const hmi::Id viewId = v->id;
    const std::string label = "Assistant IA : " + (p.summary.empty() ? std::string("proposition") : p.summary);
    // Une variable a creer (choisie par l'utilisateur) : le projet ET la vue, dans une seule commande.
    std::string createVar;
    for (const auto& op : p.ops)
        if (op.action == Operation::Action::Bind && p.variable.create && p.variable.use.empty() && !variableExists(doc->project, op.variable))
            createVar = op.variable;
    std::string failed;
    core::CommandPtr cmd;
    if (!createVar.empty()) {
        cmd = hmi::changeProject(doc, label, [&](hmi::Project& pr) {
            hmi::Variable var;
            var.id = pr.allocate();
            var.name = createVar;
            var.type = p.variable.type.empty() ? "BOOL" : p.variable.type;
            var.initial = var.type == "BOOL" ? "FALSE" : "0";
            var.description = "Cr\xC3\xA9\xC3\xA9" "e par l'Assistant IA (demande : " + p.request + ")";
            pr.programs.variables.push_back(std::move(var));
            if (hmi::View* target = pr.view(viewId)) {
                if (!applyOps(pr, *target, pr.nextId, p, nullptr, &failed)) {
                    // Rien a moitie : la variable repart aussi.
                    pr.programs.variables.pop_back();
                }
            }
        });
    } else {
        cmd = hmi::changeView(doc, viewId, label, [&](hmi::Project& pr, hmi::View& view) {
            hmi::View work = view;
            if (applyOps(pr, work, pr.nextId, p, nullptr, &failed)) view = std::move(work);
        });
    }
    if (!failed.empty()) {
        if (why) *why = failed;
        return nullptr;
    }
    if (!cmd && why) *why = "La proposition ne change rien \xC3\xA0 la vue.";
    return cmd;
}

Preview preview(const hmi::Project& project, const Proposal& p) {
    Preview out;
    const hmi::View* v = targetView(project, p);
    if (!v) {
        out.why = "La vue \xC2\xAB " + p.view + " \xC2\xBB n'existe plus.";
        return out;
    }
    out.view = *v;
    hmi::Id next = project.nextId;
    // Tolerant : une operation impossible (une variable introuvable) n'empeche pas de voir le reste.
    Run run{project, out.view, next, p, nullptr, nullptr, &out.touched, /*skipRemovals=*/true};
    run.preview = true;
    out.ok = run.go();
    return out;
}

// ---- le JSON ----------------------------------------------------------------------
json::Value operationsToJson(const std::vector<Operation>& ops) {
    json::Value arr = json::Value::array();
    for (const auto& op : ops) {
        json::Value o = json::Value::object();
        o.set("action", std::string(actionKey(op.action)));
        switch (op.action) {
            case Operation::Action::Create:
                o.set("genre", op.kind);
                o.set("nom", op.name);
                break;
            case Operation::Action::Modify:
            case Operation::Action::Bind:
                o.set("objet", op.name);
                break;
            default: {
                json::Value list = json::Value::array();
                for (const auto& n : op.objects) list.push(n);
                o.set("objets", std::move(list));
                break;
            }
        }
        if (!op.props.empty()) {
            json::Value props = json::Value::object();
            for (const auto& [k, v] : op.props) props.set(k, v);
            o.set("proprietes", std::move(props));
        }
        if (op.action == Operation::Action::Bind) o.set("variable", op.variable);
        if (op.action == Operation::Action::Align) o.set("bord", op.edge.empty() ? std::string("haut") : op.edge);
        if (op.action == Operation::Action::Distribute) o.set("sens", op.edge.empty() ? std::string("horizontal") : op.edge);
        arr.push(std::move(o));
    }
    return arr;
}

bool operationsFromJson(const json::Value& array, std::vector<Operation>& out, std::string* why) {
    out.clear();
    if (!array.isArray()) {
        if (why) *why = "\xC2\xAB operations \xC2\xBB doit \xC3\xAAtre une liste.";
        return false;
    }
    for (const auto& o : array.items()) {
        const auto action = actionFromKey(o.str("action"));
        if (!action) {
            if (why) *why = "Op\xC3\xA9ration refus\xC3\xA9" "e : \xC2\xAB " + o.str("action") + " \xC2\xBB n'est pas une op\xC3\xA9ration connue.";
            return false;
        }
        Operation op;
        op.action = *action;
        op.kind = o.str("genre");
        op.name = *action == Operation::Action::Create ? o.str("nom") : o.str("objet");
        for (const auto& n : o.at("objets").items()) op.objects.push_back(n.asString());
        for (const auto& [k, v] : o.at("proprietes").members())
            op.props.emplace_back(k, v.isString() ? v.asString() : v.isNumber() ? hmi::formatNumber(v.asNumber()) : v.isBool() ? (v.asBool() ? "TRUE" : "FALSE") : std::string{});
        op.variable = o.str("variable");
        op.edge = *action == Operation::Action::Distribute ? o.str("sens") : o.str("bord");
        out.push_back(std::move(op));
    }
    return true;
}

json::Value toJson(const Proposal& p) {
    json::Value o = json::Value::object();
    o.set("id", p.id);
    o.set("etat", p.state);
    o.set("creer", p.create);
    o.set("modifier", p.modify);
    o.set("supprimer", p.remove);
    o.set("vue", p.view);
    o.set("vueId", static_cast<std::int64_t>(p.viewId));
    o.set("empreinte", p.fingerprint);
    o.set("demande", p.request);
    o.set("resume", p.summary);
    if (!(p.variable == VariableChoice{})) {
        json::Value c = json::Value::object();
        c.set("utiliser", p.variable.use);
        c.set("creer", p.variable.create);
        c.set("type", p.variable.type);
        o.set("variableChoisie", std::move(c));
    }
    o.set("operations", operationsToJson(p.ops));
    json::addMembers(o, p.extra);
    return o;
}

Proposal proposalFromJson(const json::Value& o) {
    Proposal p;
    p.id = o.str("id");
    p.state = o.str("etat", "proposee");
    p.create = static_cast<int>(o.integer("creer"));
    p.modify = static_cast<int>(o.integer("modifier"));
    p.remove = static_cast<int>(o.integer("supprimer"));
    p.view = o.str("vue");
    p.viewId = static_cast<hmi::Id>(o.integer("vueId"));
    p.fingerprint = o.str("empreinte");
    p.request = o.str("demande");
    p.summary = o.str("resume");
    if (const auto* c = o.find("variableChoisie")) {
        p.variable.use = c->str("utiliser");
        p.variable.create = c->flag("creer");
        p.variable.type = c->str("type", "BOOL");
    }
    if (const auto* ops = o.find("operations")) (void)operationsFromJson(*ops, p.ops);
    p.extra = json::unknownMembers(o, {"id", "etat", "creer", "modifier", "supprimer", "vue", "vueId", "empreinte", "demande", "resume",
                                       "variableChoisie", "operations"});
    return p;
}

} // namespace app::ai
