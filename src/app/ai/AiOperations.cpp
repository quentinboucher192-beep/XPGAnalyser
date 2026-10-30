// =============================================================================
//  app/ai/AiOperations.cpp - voir AiOperations.hpp
// =============================================================================
#include "AiOperations.hpp"

#include "AiRegistry.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiPublicVars.hpp"   // isSysRoot, viewNamed : les noms qu'une formule peut lire
#include "../../hmi/HmiRoles.hpp"
#include "../../hmi/HmiScript.hpp"       // isHmiFunction
#include "../../hmi/HmiStore.hpp"        // parseObjectCode : le code brut

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
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

// Un nom d'objet valable tire d'un nom voulu ("Cuve 1" -> "Cuve_1", "1_Cuve" -> "_1_Cuve").
std::string sanitizeName(std::string_view n) {
    std::string out;
    for (const char c : n) out += std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? c : '_';
    if (out.empty()) return "Objet";
    if (!(std::isalpha(static_cast<unsigned char>(out[0])) || out[0] == '_')) out.insert(out.begin(), '_');
    if (out.size() > 64) out.resize(64);
    return out;
}

// Le nom sans son numero de fin ("Vanne_12" -> "Vanne") : la base d'une copie (comme Dupliquer).
std::string baseOf(const std::string& name) {
    std::size_t end = name.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(name[end - 1]))) --end;
    if (end < name.size() && end > 0 && name[end - 1] == '_') return name.substr(0, end - 1);
    return name;
}

std::string joinList(const std::vector<std::string>& items, std::string_view sep = ", ") {
    std::string s;
    for (const auto& i : items) s += (s.empty() ? "" : std::string(sep)) + i;
    return s;
}

// Un genre que la bibliotheque de cette edition ne propose pas (le groupe, l'instance de symbole, la
// communication dans IHM Studio) ne se cree pas par l'Assistant. Vide : il se cree.
std::string notInLibrary(hmi::Kind k) {
    const auto* e = registry().find(k);
    if (e && !e->family.empty()) return {};
    const std::string name = "\xC2\xAB " + std::string(hmi::kindKey(k)) + " \xC2\xBB (" + std::string(hmi::kindLabel(k)) + ")";
    if (k == hmi::Kind::Group) return name + " ne se cr\xC3\xA9" "e pas : un groupe na\xC3\xAEt d'une s\xC3\xA9lection (Ctrl+G dans l'\xC3\xA9" "diteur).";
    if (k == hmi::Kind::SymbolInstance)
        return name + " ne se cr\xC3\xA9" "e pas ici : une instance se pose depuis la section Symboles de la biblioth\xC3\xA8que.";
    return name + " n'est pas dans la biblioth\xC3\xA8que de ce logiciel : l'Assistant ne le cr\xC3\xA9" "e pas.";
}

// Les operations d'une action qui portent du CODE (un script, un appel, un calcul a references) :
// jamais par l'Assistant.
bool runsCode(hmi::Operation o) noexcept {
    return o == hmi::Operation::RunScript || o == hmi::Operation::CallScript || o == hmi::Operation::Call || o == hmi::Operation::Maths;
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
    // 1.15.0 : le nom ecrit dans une operation -> l'objet cree (renomme si le nom etait pris) :
    // les operations suivantes qui citent ce nom visent le nouvel objet.
    std::map<std::string, hmi::Id> aliases{};
    std::vector<std::string>       renamed{};   // "Cuve_1 -> Cuve_1_1"

    [[nodiscard]] const Env& environment() const {
        static const Env none;
        return env ? *env : none;
    }

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
            if (it.id == o.id && it.what == what) {
                it.name = o.name;
                return &it;
            }
        Item it;
        it.what = what;
        it.id = o.id;
        it.name = o.name;
        it.kind = o.kind;
        check->items.push_back(std::move(it));
        return &check->items.back();
    }
    Item::What whatOf(const hmi::Object& o) const { return createdIds.count(o.id) ? Item::What::Create : Item::What::Modify; }
    void change(const hmi::Object& o, Change c) {
        if (Item* it = itemFor(whatOf(o), o)) it->changes.push_back(std::move(c));
    }
    void noteModified(const hmi::Object& o) {
        if (createdIds.count(o.id)) return;   // un objet cree puis retouche reste "cree"
        modifiedIds.insert(o.id);
    }

    hmi::Object* find(std::string_view name) {
        if (const auto it = aliases.find(std::string(name)); it != aliases.end())
            if (hmi::Object* o = view.object(it->second)) return o;
        for (auto& o : view.objects)
            if (o.name == name) return &o;
        for (auto& o : view.objects)
            if (iequal(o.name, name)) return &o;
        return nullptr;
    }
    [[nodiscard]] bool nameTaken(std::string_view name) const {
        return std::any_of(view.objects.begin(), view.objects.end(), [&](const hmi::Object& o) { return iequal(o.name, name); });
    }
    // Le nom d'un objet neuf : le nom voulu s'il est valable et libre ; sinon le premier libre a partir de lui.
    std::string freshName(const std::string& wanted, std::string_view fallbackBase) {
        if (!wanted.empty() && hmi::edit::validName(wanted) && !nameTaken(wanted)) return wanted;
        const std::string base = wanted.empty() ? std::string(fallbackBase) : sanitizeName(wanted);
        std::string name = !wanted.empty() && base != wanted && !nameTaken(base) ? base : hmi::uniqueObjectName(view, base);
        if (!wanted.empty()) renamed.push_back(wanted + kArrow + name);
        return name;
    }
    void boundsWarning(const hmi::Object& o) {
        const hmi::Box b = hmi::edit::rotatedBounds(o);
        if (b.x < 0 || b.y < 0 || b.right() > view.width || b.bottom() > view.height)
            warn(o.name + " d\xC3\xA9" "passe du cadre de la vue (" + std::to_string(view.width) + " \xC3\x97 " + std::to_string(view.height) + ").");
    }

    // ---- les valeurs ----------------------------------------------------------------
    // Un nom lu par une formule : une variable (IHM, automate relie), une fonction de l'IHM, une
    // variable systeme (SYS.), un parametre de la vue, une vue (Vue.Objet.Cle).
    [[nodiscard]] bool knownName(const std::string& root) const {
        return variableExists(project, root, environment()) || hmi::isHmiFunction(root) || hmi::pub::isSysRoot(root) || view.param(root)
            || hmi::pub::viewNamed(project, root);
    }
    // Une formule (une condition, un retour d'etat, une borne) : lisible, et chaque nom lu existe. Vide : oui.
    std::string formulaProblem(const std::string& text) const {
        const auto e = hmi::Expression::compile(text);
        if (!e.valid()) return "formule illisible (" + e.error() + ")";
        for (const auto& root : e.roots()) {
            if (knownName(root)) continue;
            std::string msg = "\xC2\xAB " + root + " \xC2\xBB n'est pas une variable du projet (l'Assistant n'invente pas de variable)";
            if (const auto near = nearVariables(project, root, 3); !near.empty()) msg += " ; les plus proches : " + joinList(near);
            return msg;
        }
        return {};
    }
    // La valeur convient-elle a la propriete `key` d'un objet de ce genre ? Vide : oui.
    std::string valueProblem(hmi::Kind k, std::string_view key, const std::string& value) const {
        if (auto bad = badValue(k, key, value); !bad.empty()) return bad;
        if (value.empty()) return {};
        const auto r = hmi::roles::roleOf(k, key);
        if (r.role == hmi::roles::Role::Choice)
            if (const auto* e = registry().find(k))
                if (const auto* pi = e->prop(key); pi && !pi->choices.empty()
                                                   && std::find(pi->choices.begin(), pi->choices.end(), value) == pi->choices.end())
                    return "un de ces mots est attendu : " + joinList(pi->choices);
        if (r.role == hmi::roles::Role::Duration) {
            std::int64_t ms = 0;
            if (!hmi::roles::parseDuration(value, ms, r.unit == hmi::roles::Unit::Milliseconds ? hmi::roles::Unit::Milliseconds : hmi::roles::Unit::Seconds))
                return "une dur\xC3\xA9" "e est attendue (500 ms, 2 s)";
        }
        // Un texte a trous ({Variable}, {Expression:0.0}) : chaque trou ne lit que des noms qui existent.
        if (r.role == hmi::roles::Role::Text && value.find('{') != std::string::npos) {
            const auto t = hmi::TextTemplate::compile(value);
            if (const auto errs = t.errors(); !errs.empty()) return "texte \xC3\xA0 trous illisible (" + errs.front() + ")";
            for (const auto& root : t.roots())
                if (!knownName(root)) {
                    std::string msg = "le trou {" + root + "} ne lit pas une variable du projet (l'Assistant n'invente pas de variable)";
                    if (const auto near = nearVariables(project, root, 3); !near.empty()) msg += " ; les plus proches : " + joinList(near);
                    return msg;
                }
            return {};
        }
        if (hmi::roles::readsAsFormula(k, key)) {
            if (r.role == hmi::roles::Role::List) {
                for (const auto& item : hmi::roles::splitList(value))
                    if (!item.empty())
                        if (auto bad = formulaProblem(item); !bad.empty()) return bad;
                return {};
            }
            return formulaProblem(value);
        }
        return {};
    }

    // La variable `requested` sur l'objet `o` (associer, ou la propriete `variable` d'un creer, d'un
    // modifier) : elle existe, ou l'utilisateur a choisi (une existante, ou la creer) ; sinon une
    // erreur, et la variable manquante est notee (l'apercu propose les plus proches).
    bool setVariable(hmi::Object& o, const std::string& requested) {
        hmi::Prop* prop = o.find("variable");
        if (!prop) return fail(label(o) + " ne se relie pas \xC3\xA0 une variable.");
        std::string name = requested;
        const bool exists = variableExists(project, name, environment());
        if (!exists) {
            if (!p.variable.use.empty()) {
                if (!variableExists(project, p.variable.use, environment()))
                    return fail("La variable choisie \xC2\xAB " + p.variable.use + " \xC2\xBB n'existe pas non plus.");
                name = p.variable.use;
            } else if (p.variable.create) {
                if (!hmi::isIdentifier(name)) return fail("\xC2\xAB " + name + " \xC2\xBB n'est pas un nom de variable valable.");
                warn("La variable " + name + " (" + p.variable.type + ") sera cr\xC3\xA9\xC3\xA9" "e dans les variables de l'IHM, \xC3\xA0 ta demande.");
            } else {
                if (check) {
                    if (check->missingVariable.empty()) {
                        check->missingVariable = name;
                        check->nearVariables = nearVariables(project, name);
                    }
                    // L'objet vise se montre quand meme (l'apercu l'encadre) : il changera quand la variable sera choisie.
                    change(o, {"variable", prop->value, name + " (introuvable)"});
                    if (Item* it = itemFor(whatOf(o), o)) it->variable = name;
                    noteModified(o);
                }
                if (touched && preview && !createdIds.count(o.id)) touched->modified.push_back(o.id);
                return fail("La variable \xC2\xAB " + name + " \xC2\xBB n'existe pas dans le projet : l'Assistant ne l'invente pas. "
                            "Choisis une variable existante, ou demande sa cr\xC3\xA9" "ation.");
            }
        }
        change(o, {"variable", prop->value, name});
        if (Item* it = itemFor(whatOf(o), o)) it->variable = name;
        if (check) check->bindings.push_back(o.name + kArrow + name + (exists ? std::string{} : p.variable.create ? " (\xC3\xA0 cr\xC3\xA9" "er)" : std::string{}));
        prop->value = name;
        prop->expr.clear();
        noteModified(o);
        return true;
    }

    // ---- les operations ----------------------------------------------------------------
    bool create(const Operation& op) {
        const auto kind = hmi::kindFromKey(op.kind);
        if (!kind) {
            std::string msg = "Genre d'objet inconnu : \xC2\xAB " + op.kind + " \xC2\xBB (les genres sont ceux de la biblioth\xC3\xA8que).";
            if (const auto near = registry().nearKinds(op.kind); !near.empty()) msg += " Les plus proches : " + joinList(near) + ".";
            return fail(msg);
        }
        if (const std::string why2 = notInLibrary(*kind); !why2.empty()) return fail(why2);
        double x = 40, y = 40;
        for (const auto& [k, v] : op.props) {
            double d = 0;
            if (k == "x" && hmi::roles::readNumber(v, d)) x = d;
            if (k == "y" && hmi::roles::readNumber(v, d)) y = d;
        }
        hmi::Object o = hmi::makeObject(*kind, nextId++, freshName(op.name, hmi::kindKey(*kind)), x, y, view.activeLayer);
        if (o.layer == hmi::kNoId && !view.layers.empty()) o.layer = view.layers.front().id;
        createdIds.insert(o.id);
        if (!op.name.empty()) aliases[op.name] = o.id;
        (void)itemFor(Item::What::Create, o);
        std::string variable;
        bool hasVariable = false;
        for (const auto& [k, v] : op.props) {
            hmi::Prop* prop = o.find(k);
            if (!prop) {
                if (!fail("\xC2\xAB " + k + " \xC2\xBB n'est pas une propri\xC3\xA9t\xC3\xA9 d'un objet " + std::string(hmi::kindLabel(*kind))
                          + " : refus\xC3\xA9" "e (aucune propri\xC3\xA9t\xC3\xA9 n'est invent\xC3\xA9" "e)."))
                    return false;
                continue;
            }
            if (k == "variable") {
                variable = v;
                hasVariable = !v.empty();
                continue;
            }
            if (const auto bad = valueProblem(*kind, k, v); !bad.empty()) {
                if (!fail(o.name + "." + k + " = \xC2\xAB " + v + " \xC2\xBB : " + bad + ".")) return false;
                continue;
            }
            prop->value = v;
            change(o, {k, {}, v});
        }
        if (hasVariable && !setVariable(o, variable)) return false;
        boundsWarning(o);
        if (touched) touched->created.push_back(o.id);
        (void)hmi::edit::addObject(view, std::move(o));
        return true;
    }

    bool modify(const Operation& op) {
        hmi::Object* o = find(op.name);
        if (!o) return fail("Objet introuvable dans la vue : \xC2\xAB " + op.name + " \xC2\xBB.");
        const hmi::Box before = o->box();
        hmi::Box box = before;
        bool geometry = false, hasVariable = false;
        std::string variable;
        for (const auto& [k, v] : op.props) {
            hmi::Prop* prop = o->find(k);
            if (!prop) {
                if (!fail("\xC2\xAB " + k + " \xC2\xBB n'est pas une propri\xC3\xA9t\xC3\xA9 de " + label(*o) + ".")) return false;
                continue;
            }
            if (k == "variable") {
                variable = v;
                hasVariable = true;
                continue;
            }
            if (const auto bad = valueProblem(o->kind, k, v); !bad.empty()) {
                if (!fail(o->name + "." + k + " = \xC2\xAB " + v + " \xC2\xBB : " + bad + ".")) return false;
                continue;
            }
            // DEPLACER, REDIMENSIONNER : x, y, w, h passent par le geste natif (un groupe garde ses enfants).
            double d = 0;
            if ((k == "x" || k == "y" || k == "w" || k == "h") && hmi::roles::readNumber(v, d)) {
                (k == "x" ? box.x : k == "y" ? box.y : k == "w" ? box.w : box.h) = d;
                geometry = true;
                continue;
            }
            if (!prop->expr.empty())
                warn(o->name + "." + k + " est pilot\xC3\xA9" "e par une formule (fx) : la valeur fixe ne se verra pas tant que la formule reste.");
            change(*o, {k, prop->value, v});
            prop->value = v;
        }
        if (geometry && !(box == before)) {
            if (view.effectivelyLocked(*o)) {
                if (!fail(o->name + " est verrouill\xC3\xA9 : d\xC3\xA9verrouille-le pour le d\xC3\xA9placer ou le redimensionner.")) return false;
            } else {
                const hmi::Id id = o->id;
                hmi::edit::setBox(view, id, box);
                o = view.object(id);
                const hmi::Box nb = o->box();
                if (nb.x != before.x) change(*o, {"x", hmi::formatNumber(before.x), hmi::formatNumber(nb.x)});
                if (nb.y != before.y) change(*o, {"y", hmi::formatNumber(before.y), hmi::formatNumber(nb.y)});
                if (nb.w != before.w) change(*o, {"w", hmi::formatNumber(before.w), hmi::formatNumber(nb.w)});
                if (nb.h != before.h) change(*o, {"h", hmi::formatNumber(before.h), hmi::formatNumber(nb.h)});
                boundsWarning(*o);
            }
        }
        if (hasVariable) {
            if (variable.empty()) {
                if (hmi::Prop* prop = o->find("variable"); prop && !prop->value.empty()) {
                    change(*o, {"variable", prop->value, {}});
                    prop->value.clear();
                }
            } else if (!setVariable(*o, variable)) {
                return false;
            }
        }
        noteModified(*o);
        return true;
    }

    bool bind(const Operation& op) {
        hmi::Object* o = find(op.name);
        if (!o) return fail("Objet introuvable dans la vue : \xC2\xAB " + op.name + " \xC2\xBB.");
        if (op.variable.empty()) return fail("Quelle variable associer \xC3\xA0 " + o->name + " ? (le champ variable est vide)");
        return setVariable(*o, op.variable);
    }

    bool resolve(const std::vector<std::string>& names, std::vector<hmi::Id>& ids) {
        for (const auto& n : names) {
            hmi::Object* o = find(n);
            if (!o) {
                if (!fail("Objet introuvable dans la vue : \xC2\xAB " + n + " \xC2\xBB.")) return false;
                continue;
            }
            if (std::find(ids.begin(), ids.end(), o->id) == ids.end()) ids.push_back(o->id);
        }
        return true;
    }

    bool arrange(const Operation& op) {
        std::vector<hmi::Id> ids;
        if (!resolve(op.objects, ids)) return false;
        const bool align = op.action == Operation::Action::Align;
        if (align && ids.size() < 2) return fail("Il faut au moins deux objets \xC3\xA0 aligner.");
        if (!align && ids.size() < 3) return fail("Il faut au moins trois objets \xC3\xA0 r\xC3\xA9partir (des \xC3\xA9" "carts \xC3\xA9gaux).");
        std::vector<std::pair<hmi::Id, hmi::Box>> before;
        for (const auto id : ids)
            if (const hmi::Object* o = view.object(id)) {
                before.emplace_back(id, o->box());
                if (view.effectivelyLocked(*o)) warn(o->name + " est verrouill\xC3\xA9 : il ne bouge pas.");
            }
        if (align) {
            using A = hmi::edit::Align;
            const std::string e = lower(op.edge);
            const A a = e == "bas" ? A::Bottom : e == "gauche" ? A::Left : e == "droite" ? A::Right
                      : e == "centre-h" ? A::CenterH : e == "centre-v" ? A::CenterV : A::Top;
            hmi::edit::align(view, ids, a);
        } else {
            hmi::edit::distribute(view, ids, lower(op.edge) != "vertical");
        }
        for (const auto& [id, box] : before)
            if (const hmi::Object* o = view.object(id)) {
                const hmi::Box nb = o->box();
                if (nb.x != box.x) change(*o, {"x", hmi::formatNumber(box.x), hmi::formatNumber(nb.x)});
                if (nb.y != box.y) change(*o, {"y", hmi::formatNumber(box.y), hmi::formatNumber(nb.y)});
                noteModified(*o);
            }
        return true;
    }

    bool remove(const Operation& op) {
        std::vector<hmi::Id> ids;
        for (const auto& n : op.objects) {
            hmi::Object* o = find(n);
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

    // DUPLIQUER : chaque objet (avec ses enfants) copie, decale, ses copies dans le meme groupe ; des
    // identifiants neufs ; la copie du haut prend le nom voulu (s'il est libre), les autres un nom libre.
    bool duplicate(const Operation& op) {
        std::vector<hmi::Id> ids;
        if (!resolve(op.objects, ids)) return false;
        if (ids.empty()) return fail("Rien \xC3\xA0 dupliquer : aucun objet cit\xC3\xA9.");
        // Un objet dont un parent est aussi cite est copie avec lui (une seule fois).
        std::set<hmi::Id> inside;
        for (const auto id : ids)
            for (const auto d : view.descendantsOf(id)) inside.insert(d);
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const hmi::Id id = ids[i];
            if (inside.count(id)) continue;
            const hmi::Object* src = view.object(id);
            if (!src) continue;
            std::vector<hmi::Id> tree{id};
            for (const auto d : view.descendantsOf(id)) tree.push_back(d);
            std::map<hmi::Id, hmi::Id> renamedIds;
            for (const auto t : tree) renamedIds[t] = nextId++;
            std::vector<hmi::Object> copies;
            for (const auto& o : view.objects) {
                if (!renamedIds.count(o.id)) continue;
                hmi::Object c = o;
                c.id = renamedIds[o.id];
                c.parent = renamedIds.count(o.parent) ? renamedIds[o.parent] : o.parent;
                c.locked = false;
                copies.push_back(std::move(c));
            }
            const std::string srcName = src->name;
            for (auto& c : copies) {
                const bool top = c.id == renamedIds[id];
                const std::string wanted = top && i < op.names.size() ? op.names[i] : std::string{};
                c.name = top && !wanted.empty() ? freshName(wanted, baseOf(srcName)) : hmi::uniqueObjectName(view, baseOf(c.name));
                if (top && !wanted.empty()) aliases[wanted] = c.id;
                c.setNumber("x", c.number("x") + op.dx);
                c.setNumber("y", c.number("y") + op.dy);
                createdIds.insert(c.id);
                if (Item* it = itemFor(Item::What::Create, c)) {
                    if (top) it->changes.push_back({"copie de", {}, srcName});
                    it->changes.push_back({"x", {}, hmi::formatNumber(c.number("x"))});
                    it->changes.push_back({"y", {}, hmi::formatNumber(c.number("y"))});
                }
                if (touched) touched->created.push_back(c.id);
                view.objects.push_back(c);
                if (top) boundsWarning(view.objects.back());
            }
        }
        hmi::edit::refreshGroupBounds(view);
        return true;
    }

    // LE CODE BRUT : relu par le logiciel (parseObjectCode), chaque objet verifie comme un creer, ses
    // actions sans code ; pose comme Ctrl+V d'un code (pasteCode) : des ids neufs, des noms libres.
    bool code(const Operation& op) {
        if (op.code.empty()) return fail("L'op\xC3\xA9ration code ne porte aucun code brut.");
        auto parsed = hmi::parseObjectCode(op.code);
        if (!parsed) return fail("Code brut illisible : " + parsed.error().message());
        // Une propriete absente du code garde la valeur de la creation par defaut (dans l'ordre de celle-ci).
        std::vector<hmi::Object> objs = parsed->objects;
        if (objs.empty()) return fail("Le code brut ne contient aucun objet.");
        for (auto& o : objs) {
            const hmi::Object base = hmi::makeObject(o.kind, o.id, o.name, 0, 0, o.layer);
            std::vector<hmi::Prop> merged;
            for (const auto& b : base.props) {
                const hmi::Prop* given = o.find(b.key);
                merged.push_back(given ? *given : b);
            }
            for (const auto& pr : o.props)
                if (!base.find(pr.key)) merged.push_back(pr);   // refusee plus bas : aucune propriete inventee
            o.props = std::move(merged);
        }
        for (const auto& o : objs) {
            const hmi::Object base = hmi::makeObject(o.kind, 1, "x", 0, 0, hmi::kNoId);
            const std::string who = o.name + " (" + std::string(hmi::kindLabel(o.kind)) + ")";
            if (o.kind != hmi::Kind::Group)   // un groupe vient avec ses enfants, dans le code
                if (const std::string why2 = notInLibrary(o.kind); !why2.empty())
                    if (!fail("Code brut : " + why2)) return false;
            for (const auto& pr : o.props) {
                if (!base.find(pr.key)) {
                    if (!fail("Code brut : \xC2\xAB " + pr.key + " \xC2\xBB n'est pas une propri\xC3\xA9t\xC3\xA9 de " + who + " (aucune propri\xC3\xA9t\xC3\xA9 n'est invent\xC3\xA9" "e).")) return false;
                    continue;
                }
                if (pr.key == "variable") {
                    if (!pr.value.empty() && !variableExists(project, pr.value, environment()))
                        if (!fail("Code brut : la variable \xC2\xAB " + pr.value + " \xC2\xBB de " + who + " n'existe pas (l'Assistant ne l'invente pas).")) return false;
                } else if (const auto bad = valueProblem(o.kind, pr.key, pr.value); !bad.empty()) {
                    if (!fail("Code brut : " + o.name + "." + pr.key + " = \xC2\xAB " + pr.value + " \xC2\xBB : " + bad + ".")) return false;
                }
                if (!pr.expr.empty())
                    if (const auto bad = formulaProblem(pr.expr); !bad.empty())
                        if (!fail("Code brut : la formule de " + o.name + "." + pr.key + " : " + bad + ".")) return false;
            }
            if (!o.functionOverrides.empty())
                if (!fail("Code brut : " + who + " red\xC3\xA9" "finit des fonctions (du code) : refus\xC3\xA9.")) return false;
            if (!o.alarmOverrides.empty())
                if (!fail("Code brut : " + who + " surcharge des alarmes : refus\xC3\xA9 (\xC3\xA0 faire dans l'\xC3\xA9" "diteur).")) return false;
            for (const auto& a : o.actions) {
                const std::string what = "Code brut : l'action \xC2\xAB " + std::string(hmi::operationLabel(a.operation)) + " \xC2\xBB de " + who;
                if (runsCode(a.operation)) {
                    if (!fail(what + " ex\xC3\xA9" "cute du code : refus\xC3\xA9" "e (jamais de code venant de l'Assistant).")) return false;
                    continue;
                }
                for (const std::string* f : {&a.watch, &a.guard})
                    if (!f->empty())
                        if (const auto bad = formulaProblem(*f); !bad.empty())
                            if (!fail(what + " : " + bad + ".")) return false;
                if (hmi::operationWritesVariable(a.operation)) {
                    const std::string target = hmi::targetVariable(a.target);
                    if (target.empty() || !variableExists(project, target, environment()))
                        if (!fail(what + " \xC3\xA9" "crit \xC2\xAB " + a.target + " \xC2\xBB, qui n'est pas une variable du projet.")) return false;
                    if (a.operation == hmi::Operation::Assign && !a.value.empty())
                        if (const auto bad = formulaProblem(a.value); !bad.empty())
                            if (!fail(what + " : " + bad + ".")) return false;
                }
                if (hmi::operationOpensView(a.operation) && !a.target.empty() && !hmi::pub::viewNamed(project, a.target))
                    if (!fail(what + " ouvre \xC2\xAB " + a.target + " \xC2\xBB, qui n'est pas une vue du projet.")) return false;
            }
        }
        // Poser : les ids viennent de nextId (un projet de passage les distribue, comme Ctrl+V).
        hmi::Project ids;
        ids.nextId = nextId;
        const hmi::Id first = nextId;
        std::string why2;
        const auto top = hmi::edit::pasteCode(ids, view, objs, hmi::kNoId, &why2);
        if (top.empty()) return fail("Le code brut ne se pose pas : " + why2 + ".");
        nextId = ids.nextId;
        std::size_t k = 0;
        for (hmi::Id id = first; id < nextId; ++id, ++k) {
            const hmi::Object* o = view.object(id);
            if (!o) continue;
            createdIds.insert(id);
            if (k < objs.size()) {
                aliases[objs[k].name] = id;
                if (objs[k].name != o->name) renamed.push_back(objs[k].name + kArrow + o->name);
            }
            if (Item* it = itemFor(Item::What::Create, *o)) {
                it->changes.push_back({"code brut", {}, std::to_string(o->props.size()) + " propri\xC3\xA9t\xC3\xA9s, "
                                                            + std::to_string(o->actions.size()) + " actions"});
            }
            if (touched) touched->created.push_back(id);
            if (o->parent == hmi::kNoId) boundsWarning(*o);
        }
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
                case Operation::Action::Duplicate: ok = duplicate(op); break;
                case Operation::Action::Code: ok = code(op); break;
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
            if (!renamed.empty()) {
                std::string list;
                for (std::size_t i = 0; i < renamed.size() && i < 4; ++i) list += (list.empty() ? "" : ", ") + renamed[i];
                if (renamed.size() > 4) list += ", \xE2\x80\xA6";
                check->warnings.push_back(std::to_string(renamed.size()) + (renamed.size() > 1 ? " noms pris ou invalides : les objets neufs s'appellent autrement ("
                                                                                              : " nom pris ou invalide : l'objet neuf s'appelle autrement (")
                                          + list + ").");
            }
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

// Les variables qu'une proposition demande (associer, la propriete variable d'un creer, d'un modifier).
std::vector<std::string> requestedVariables(const Proposal& p) {
    std::vector<std::string> out;
    for (const auto& op : p.ops) {
        if (op.action == Operation::Action::Bind && !op.variable.empty()) out.push_back(op.variable);
        if (op.action == Operation::Action::Create || op.action == Operation::Action::Modify)
            for (const auto& [k, v] : op.props)
                if (k == "variable" && !v.empty()) out.push_back(v);
    }
    return out;
}

} // namespace

std::string_view actionKey(Operation::Action a) noexcept {
    switch (a) {
        case Operation::Action::Create: return "creer";
        case Operation::Action::Modify: return "modifier";
        case Operation::Action::Bind: return "associer";
        case Operation::Action::Align: return "aligner";
        case Operation::Action::Distribute: return "distribuer";
        case Operation::Action::Remove: return "supprimer";
        case Operation::Action::Duplicate: return "dupliquer";
        case Operation::Action::Code: return "code";
    }
    return "creer";
}

std::optional<Operation::Action> actionFromKey(std::string_view k) noexcept {
    for (const auto a : {Operation::Action::Create, Operation::Action::Modify, Operation::Action::Bind, Operation::Action::Align,
                         Operation::Action::Distribute, Operation::Action::Remove, Operation::Action::Duplicate, Operation::Action::Code})
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
                               "sur un \xC3\xA9tat qui n'est plus le sien. Redemande-la.");
    }
    hmi::View copy = *v;
    hmi::Id next = project.nextId;
    Run run{project, copy, next, p, &env, &check};
    (void)run.go();
    return check;
}

bool applyOps(const hmi::Project& project, hmi::View& view, hmi::Id& nextId, const Proposal& p, Touched* touched, std::string* why,
              bool skipRemovals, const Env* env) {
    Run run{project, view, nextId, p, env, nullptr, touched, skipRemovals};
    const bool ok = run.go();
    if (!ok && why) *why = run.why;
    return ok;
}

core::CommandPtr makeCommand(const hmi::DocumentPtr& doc, const Proposal& p, std::string* why, const Env& env) {
    if (!doc) {
        if (why) *why = "Pas de projet IHM ouvert.";
        return nullptr;
    }
    const Check check = validate(doc->project, p, env);
    if (!check.ok()) {
        if (why) *why = check.errors.front();
        return nullptr;
    }
    const hmi::View* v = targetView(doc->project, p);
    const hmi::Id viewId = v->id;
    const std::string label = "Assistant IA : " + (p.summary.empty() ? std::string("proposition") : p.summary);
    // Une variable a creer (choisie par l'utilisateur) : le projet ET la vue, dans une seule commande.
    std::string createVar;
    if (p.variable.create && p.variable.use.empty())
        for (const auto& name : requestedVariables(p))
            if (!variableExists(doc->project, name, env)) createVar = name;
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
                // Sur une copie : rien a moitie (la variable repart aussi si une operation echoue).
                hmi::View work = *target;
                if (applyOps(pr, work, pr.nextId, p, nullptr, &failed, false, &env)) *target = std::move(work);
                else pr.programs.variables.pop_back();
            }
        });
    } else {
        cmd = hmi::changeView(doc, viewId, label, [&](hmi::Project& pr, hmi::View& view) {
            hmi::View work = view;
            if (applyOps(pr, work, pr.nextId, p, nullptr, &failed, false, &env)) view = std::move(work);
        });
    }
    if (!failed.empty()) {
        if (why) *why = failed;
        return nullptr;
    }
    if (!cmd && why) *why = "La proposition ne change rien \xC3\xA0 la vue.";
    return cmd;
}

Preview preview(const hmi::Project& project, const Proposal& p, const Env& env) {
    Preview out;
    const hmi::View* v = targetView(project, p);
    if (!v) {
        out.why = "La vue \xC2\xAB " + p.view + " \xC2\xBB n'existe plus.";
        return out;
    }
    out.view = *v;
    hmi::Id next = project.nextId;
    // Tolerant : une operation impossible (une variable introuvable) n'empeche pas de voir le reste.
    Run run{project, out.view, next, p, &env, nullptr, &out.touched, /*skipRemovals=*/true};
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
            case Operation::Action::Code:
                o.set("code", op.code);
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
        if (op.action == Operation::Action::Duplicate) {
            if (!op.names.empty()) {
                json::Value names = json::Value::array();
                for (const auto& n : op.names) names.push(n);
                o.set("noms", std::move(names));
            }
            o.set("dx", op.dx);
            o.set("dy", op.dy);
        }
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
        for (const auto& n : o.at("noms").items()) op.names.push_back(n.asString());
        op.dx = o.num("dx", 20);
        op.dy = o.num("dy", 20);
        op.code = o.str("code");
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
    if (!p.fingerprintAfter.empty()) o.set("empreinteApres", p.fingerprintAfter);
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
    p.fingerprintAfter = o.str("empreinteApres");
    p.request = o.str("demande");
    p.summary = o.str("resume");
    if (const auto* c = o.find("variableChoisie")) {
        p.variable.use = c->str("utiliser");
        p.variable.create = c->flag("creer");
        p.variable.type = c->str("type", "BOOL");
    }
    if (const auto* ops = o.find("operations")) (void)operationsFromJson(*ops, p.ops);
    p.extra = json::unknownMembers(o, {"id", "etat", "creer", "modifier", "supprimer", "vue", "vueId", "empreinte", "empreinteApres", "demande", "resume",
                                       "variableChoisie", "operations"});
    return p;
}

} // namespace app::ai
