// =============================================================================
//  app/ai/AiOperations.cpp - voir AiOperations.hpp
// =============================================================================
#include "AiOperations.hpp"

#include "AiRegistry.hpp"
#include "../../hmi/HmiDuplicate.hpp"    // 1.16.0 : dupliquer avec reperes, comme Ctrl+Maj+D
#include "../../hmi/HmiEdit.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiPublicVars.hpp"   // isSysRoot, viewNamed : les noms qu'une formule peut lire
#include "../../hmi/HmiRoles.hpp"
#include "../../hmi/HmiScript.hpp"       // isHmiFunction
#include "../../hmi/HmiStore.hpp"        // parseObjectCode : le code brut
#include "../../hmi/HmiTypes.hpp"        // 1.16.0 : les types d'une variable, d'un membre (validType)

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
    // 1.16.1 : un REPERE ($Armoires[0]$.Prete : ce que Dupliquer avec reperes fait avancer) se lit sans ses $ - la
    // variable visee est Armoires (avant : << $Armoires >>, introuvable, alors que la variable existe).
    std::string plain;
    plain.reserve(name.size());
    for (char ch : name)
        if (ch != '$') plain += ch;
    std::string_view n = plain;
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

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// 1.16.0 : une valeur qui commence par "=" est une formule (fx), comme dans l'inspecteur.
bool isFormula(std::string_view v) noexcept { return !v.empty() && v.front() == '='; }

// 1.16.0 : un nom de vue neuve (les regles du renommage d'une vue, RenamePlan) ; vide : il convient.
std::string viewNameProblem(const hmi::Project& p, const std::string& name, const Env& env) {
    if (trimmed(name).empty()) return "le nom de la vue est vide";
    if (trimmed(name) != name) return "pas d'espace au d\xC3\xA9" "but ni \xC3\xA0 la fin du nom";
    if (name.size() > 64) return "nom trop long (64 caract\xC3\xA8res au plus)";
    for (const char c : name)
        if (c == ';' || c == '|' || c == '\'' || c == '"' || c == '\n' || c == '\t' || c == '$')
            return std::string("\xC2\xAB ") + c + " \xC2\xBB n'est pas permis dans un nom de vue";
    for (const auto& v : p.views)
        if (iequal(v.name, name)) return "une vue porte d\xC3\xA9j\xC3\xA0 ce nom (" + v.name + ")";
    if (hmi::isIdentifier(name)) {
        if (const auto* v = p.variable(name)) return "une variable IHM porte ce nom (" + v->name + ") : Vue.Objet se confondrait avec elle";
        if (env.plcName && env.plcName(name)) return "une variable de l'automate porte ce nom";
    }
    return {};
}

// 1.16.0 : les types elementaires que le logiciel connait (pour "les plus proches" d'un type inconnu).
const std::vector<std::string>& elementaryTypes() {
    static const std::vector<std::string> all = [] {
        std::vector<std::string> out;
        for (const char* t : {"BOOL", "INT", "UINT", "WORD", "DINT", "UDINT", "DWORD", "REAL", "LREAL", "STRING", "TIME", "BYTE",
                              "SINT", "USINT", "LINT", "ULINT", "LWORD", "DATE", "TIME_OF_DAY", "DATE_AND_TIME"})
            if (hmi::types::isElementary(t)) out.emplace_back(t);
        return out;
    }();
    return all;
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
    // 1.16.0 : ce que la proposition cree au projet, dans l'ordre des operations (celles qui suivent les
    // connaissent : associer une variable creee, un membre d'un type cree plus haut). L'appelant les pose.
    std::vector<hmi::Variable> newVars{};
    std::vector<hmi::HmiType>  newTypes{};

    [[nodiscard]] const Env& environment() const {
        static const Env none;
        return env ? *env : none;
    }

    // ---- 1.16.0 : les variables et les types crees plus haut dans la proposition -------------
    [[nodiscard]] const hmi::Variable* newVar(std::string_view name) const {
        const std::string root = rootOf(name);
        for (const auto& v : newVars)
            if (iequal(v.name, root)) return &v;
        return nullptr;
    }
    [[nodiscard]] const hmi::HmiType* newType(std::string_view name) const {
        for (const auto& t : newTypes)
            if (iequal(t.name, name)) return &t;
        return nullptr;
    }
    // Une variable existe : dans le projet, l'automate relie, ou creee plus haut (creer_variables).
    [[nodiscard]] bool exists(std::string_view name) const { return variableExists(project, name, environment()) || newVar(name); }
    // Un type connu (d'une variable ; `member` : d'un membre de type IHM) : les types du projet ET ceux crees plus haut.
    bool typeKnown(const std::string& type, std::string* reason, bool member = false) const {
        if (newTypes.empty()) return member ? hmi::types::validMemberType(project, type, reason) : hmi::types::validType(project, type, reason);
        hmi::Project probe;
        probe.programs.types = project.programs.types;
        probe.programs.types.insert(probe.programs.types.end(), newTypes.begin(), newTypes.end());
        return member ? hmi::types::validMemberType(probe, type, reason) : hmi::types::validType(probe, type, reason);
    }
    // Les types les plus proches d'un type inconnu : les elementaires, ceux du projet, ceux crees plus haut.
    [[nodiscard]] std::vector<std::string> nearTypes(std::string_view type) const {
        std::vector<std::string> all = elementaryTypes();
        for (const auto& t : project.programs.types) all.push_back(t.name);
        for (const auto& t : newTypes) all.push_back(t.name);
        std::vector<std::pair<std::size_t, std::string>> scored;
        for (const auto& c : all) {
            const std::size_t d = distance(lower(c), lower(type));
            if (d <= std::max<std::size_t>(2, type.size() / 2)) scored.emplace_back(d, c);
        }
        std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<std::string> out;
        for (const auto& [d, c] : scored)
            if (out.size() < 4) out.push_back(c);
        return out;
    }
    std::string unknownType(const std::string& type, const std::string& reason) const {
        std::string msg = "type \xC2\xAB " + type + " \xC2\xBB inconnu" + (reason.empty() ? std::string{} : " (" + reason + ")");
        msg += " (les types : BOOL, INT, DINT, REAL, STRING, TIME..., un tableau ARRAY[0..9] OF REAL, ou un type IHM du projet)";
        if (const auto near = nearTypes(hmi::types::normalized(type)); !near.empty()) msg += " ; les plus proches : " + joinList(near);
        return msg;
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
        return exists(root) || hmi::isHmiFunction(root) || hmi::pub::isSysRoot(root) || view.param(root)
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
        const bool found = exists(name);
        if (!found) {
            if (!p.variable.use.empty()) {
                if (!exists(p.variable.use))
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
        if (check)
            check->bindings.push_back(o.name + kArrow + name
                                      + (found ? (newVar(name) && !variableExists(project, name, environment()) ? std::string(" (cr\xC3\xA9\xC3\xA9" "e par la proposition)") : std::string{})
                                               : p.variable.create ? " (\xC3\xA0 cr\xC3\xA9" "er)" : std::string{}));
        prop->value = name;
        prop->expr.clear();
        noteModified(o);
        return true;
    }

    // 1.16.0 : UNE FORMULE (fx) - une valeur qui commence par "=" : lue et verifiee comme dans l'inspecteur
    // (en lecture seule, lisible, chaque nom lu existe - variables IHM, automate, creees plus haut ; les
    // reperes $...$ y sont transparents), posee comme lui (Prop::expr ; la valeur fixe reste dessous).
    bool formula(hmi::Object& o, const std::string& k, const std::string& v) {
        hmi::Prop* prop = o.find(k);
        if (!prop) return fail("\xC2\xAB " + k + " \xC2\xBB n'est pas une propri\xC3\xA9t\xC3\xA9 de " + label(o) + ".");
        const std::string e = trimmed(std::string_view(v).substr(1));
        const std::string at = o.name + "." + k + " = \xC2\xAB " + v + " \xC2\xBB : ";
        if (k == "variable")
            return fail(at + "la variable s'\xC3\xA9" "crit par son nom, sans = (une formule va dans une autre propri\xC3\xA9t\xC3\xA9 : visible, fill...).");
        if (e.empty()) return fail(at + "formule vide apr\xC3\xA8s le =.");
        std::string reason;
        if (!hmi::isReadOnly(e, &reason)) return fail(at + "une formule ne fait que lire (" + reason + ").");
        if (const auto bad = formulaProblem(e); !bad.empty()) return fail(at + bad + ".");
        change(o, {k, prop->expr.empty() ? prop->value : "=" + prop->expr, "=" + e});
        prop->expr = e;
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
            if (k == "variable" && !isFormula(v)) {
                variable = v;
                hasVariable = !v.empty();
                continue;
            }
            if (isFormula(v)) {   // 1.16.0 : une formule (fx)
                if (!formula(o, k, v)) return false;
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
            if (k == "variable" && !isFormula(v)) {
                variable = v;
                hasVariable = true;
                continue;
            }
            if (isFormula(v)) {   // 1.16.0 : une formule (fx)
                if (!formula(*o, k, v)) return false;
                noteModified(*o);
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
    // 1.16.0 : `count` copies (1 a 50), la copie k decalee de k fois (dx, dy) ; les noms voulus dans l'ordre
    // (copie 1 de chaque objet, puis copie 2...). `markers` : comme Ctrl+Maj+D (duplicateMarkers).
    bool duplicate(const Operation& op) {
        if (op.count < 1 || op.count > 50) return fail("dupliquer : de 1 \xC3\xA0 50 copies (count vaut " + std::to_string(op.count) + ").");
        if (op.markers) return duplicateMarkers(op);
        std::vector<hmi::Id> ids;
        if (!resolve(op.objects, ids)) return false;
        if (ids.empty()) return fail("Rien \xC3\xA0 dupliquer : aucun objet cit\xC3\xA9.");
        // Un objet dont un parent est aussi cite est copie avec lui (une seule fois).
        std::set<hmi::Id> inside;
        for (const auto id : ids)
            for (const auto d : view.descendantsOf(id)) inside.insert(d);
        for (int k = 1; k <= op.count; ++k)
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
                const std::size_t wantedAt = static_cast<std::size_t>(k - 1) * ids.size() + i;
                for (auto& c : copies) {
                    const bool top = c.id == renamedIds[id];
                    const std::string wanted = top && wantedAt < op.names.size() ? op.names[wantedAt] : std::string{};
                    c.name = top && !wanted.empty() ? freshName(wanted, baseOf(srcName)) : hmi::uniqueObjectName(view, baseOf(c.name));
                    if (top && !wanted.empty()) aliases[wanted] = c.id;
                    c.setNumber("x", c.number("x") + k * op.dx);
                    c.setNumber("y", c.number("y") + k * op.dy);
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

    // 1.16.0 : DUPLIQUER AVEC REPERES - le moteur de Ctrl+Maj+D (hmi::dup), sans sa fenetre : le plan de
    // depart (defaultPlan : les noms en serie, les reperes a indices avances d'une copie a l'autre - $V[1].Ouv$
    // devient $V[2].Ouv$, $V[3].Ouv$... -, les indices hors repere suivis si aucun repere n'en porte), puis
    // dup::apply. Seule la pose change : la copie k decalee de k fois (dx, dy).
    bool duplicateMarkers(const Operation& op) {
        namespace dup = hmi::dup;
        std::vector<hmi::Id> ids;
        if (!resolve(op.objects, ids)) return false;
        if (ids.empty()) return fail("Rien \xC3\xA0 dupliquer : aucun objet cit\xC3\xA9.");
        const dup::Scan sc = dup::scan(view, ids, dup::projectBounds(project));
        if (sc.units.empty()) return fail("Rien \xC3\xA0 dupliquer : aucun objet cit\xC3\xA9.");
        dup::Plan plan = dup::defaultPlan(view, sc, op.count);
        for (int k = 1; k <= op.count; ++k)
            if (static_cast<std::size_t>(k - 1) < op.names.size() && !op.names[static_cast<std::size_t>(k - 1)].empty())
                plan.rows[static_cast<std::size_t>(k)].name = op.names[static_cast<std::size_t>(k - 1)];
        const hmi::Box sel = hmi::edit::selectionBounds(view, sc.units);
        plan.layout.axis = dup::Axis::X;
        plan.layout.spacing = op.dx - sel.w;   // la copie k : k * dx en x (dup::offsets)
        plan.layout.spacingY = -1;
        for (const auto& m : sc.markers)
            if (!dup::hasIndices(m.name))
                warn("Le rep\xC3\xA8re $" + m.name + "$ n'a pas d'indice : il reste tel quel dans les copies (Dupliquer\xE2\x80\xA6, Ctrl+Maj+D, le remplit \xC3\xA0 la main).");
        if (sc.markers.empty() && sc.indices.empty())
            warn("Ni rep\xC3\xA8re $...$ ni indice de tableau dans les objets copi\xC3\xA9s : les copies sont identiques \xC3\xA0 l'original (sauf leur nom).");
        // L'ordre de dessin de l'arbre copie (les unites et leurs elements) : la copie k en reprend l'ordre.
        std::set<hmi::Id> treeSet;
        for (const auto u : sc.units) {
            treeSet.insert(u);
            for (const auto d : view.descendantsOf(u)) treeSet.insert(d);
        }
        const std::vector<hmi::Object> originals = [&] {
            std::vector<hmi::Object> out;
            for (const auto& o : view.objects)
                if (treeSet.count(o.id)) out.push_back(o);
            return out;
        }();
        hmi::Project idSource;
        idSource.nextId = nextId;
        const std::size_t before = view.objects.size();
        const dup::Result r = dup::apply(idSource, view, plan);
        nextId = idSource.nextId;
        const std::size_t made = view.objects.size() - before;
        const std::size_t per = originals.empty() ? 0 : originals.size();
        if (op.dy != 0 && per > 0) {
            for (std::size_t j = before; j < view.objects.size(); ++j) {
                auto& c = view.objects[j];
                const int k = static_cast<int>((j - before) / per) + 1;
                c.setNumber("y", c.number("y") + k * op.dy);
            }
            hmi::edit::refreshGroupBounds(view);
        }
        for (std::size_t j = before; j < before + made && j < view.objects.size(); ++j) {
            const hmi::Object& c = view.objects[j];
            createdIds.insert(c.id);
            const bool root = std::find(r.created.begin(), r.created.end(), c.id) != r.created.end();
            if (Item* it = itemFor(Item::What::Create, c)) {
                if (per > 0) {
                    const hmi::Object& src = originals[(j - before) % per];
                    if (root) it->changes.push_back({"copie de", {}, src.name});
                    // Ce que les reperes et les indices ont change (le reste est celui de l'original).
                    for (const auto& pr : c.props) {
                        if (pr.key == "x" || pr.key == "y") continue;
                        const hmi::Prop* sp = src.find(pr.key);
                        if (sp && sp->value == pr.value && sp->expr == pr.expr) continue;
                        it->changes.push_back({pr.key, {}, pr.expr.empty() ? pr.value : "=" + pr.expr});
                    }
                }
                it->changes.push_back({"x", {}, hmi::formatNumber(c.number("x"))});
                it->changes.push_back({"y", {}, hmi::formatNumber(c.number("y"))});
            }
            if (touched) touched->created.push_back(c.id);
            if (root) boundsWarning(c);
        }
        // Les noms voulus : la copie k (son premier objet) - les operations suivantes la citent par ce nom.
        const std::size_t units = sc.units.size();
        for (int k = 1; k <= op.count; ++k) {
            const std::size_t at = static_cast<std::size_t>(k - 1) * units;
            if (static_cast<std::size_t>(k - 1) < op.names.size() && at < r.created.size()) {
                const std::string& wanted = op.names[static_cast<std::size_t>(k - 1)];
                if (const hmi::Object* c = view.object(r.created[at])) {
                    aliases[wanted] = c->id;
                    if (!wanted.empty() && c->name != wanted) renamed.push_back(wanted + kArrow + c->name);
                }
            }
        }
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
                    if (!pr.value.empty() && !exists(pr.value))
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
                    if (target.empty() || !exists(target))
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

    // ---- 1.16.0 : la vue, les variables, les types -------------------------------------------
    // CREER_VUE : en premier (une seule) ; l'appelant a deja fait la vue neuve (newViewFor) et la passe :
    // ici, son nom et ses reglages se verifient.
    bool makeView(const Operation& op, std::size_t index) {
        if (index != 0)
            return fail("creer_vue vient en PREMIER dans la proposition, une seule fois (les op\xC3\xA9rations qui suivent visent la vue neuve).");
        if (const std::string bad = viewNameProblem(project, op.name, environment()); !bad.empty())
            return fail("Vue \xC2\xAB " + op.name + " \xC2\xBB : " + bad + ".");
        for (const auto& [k, v] : op.props) {
            double d = 0;
            if (k == "largeur" || k == "hauteur") {
                if (!hmi::roles::readNumber(v, d) || d < 50 || d > 20000 || d != static_cast<double>(static_cast<long long>(d)))
                    if (!fail("Vue " + op.name + " : " + k + " = \xC2\xAB " + v + " \xC2\xBB : un nombre entier de pixels est attendu (50 \xC3\xA0 20000).")) return false;
            } else if (k == "fond") {
                if (!hmi::roles::colorOk(v))
                    if (!fail("Vue " + op.name + " : fond = \xC2\xAB " + v + " \xC2\xBB : une couleur #RRGGBB est attendue.")) return false;
            } else if (k != "description") {
                if (!fail("Vue " + op.name + " : \xC2\xAB " + k + " \xC2\xBB n'est pas un r\xC3\xA9glage d'une vue (largeur, hauteur, fond, description).")) return false;
            }
        }
        if (check) check->newView = op.name;
        return true;
    }

    // La valeur initiale d'une variable, lue selon son type ; vide : elle convient (`out` : telle qu'elle s'ecrit).
    std::string initialValue(const std::string& type, const std::string& given, std::string& out) const {
        hmi::types::Spec spec;
        const bool parsed = hmi::types::parseSpec(type, spec);
        const std::string t = parsed && !spec.array() ? upperOf(spec.element) : std::string{};
        const bool composite = !parsed || spec.array() || !hmi::types::isElementary(t);
        const std::string g = trimmed(given);
        if (g.empty()) {
            out = composite ? std::string{} : t == "BOOL" ? "FALSE" : t == "STRING" ? "''" : "0";
            return {};
        }
        if (composite) {
            if (parsed && !spec.array())
                if (const hmi::HmiType* ht = project.hmiTypeByName(spec.element); ht && ht->kind == hmi::HmiTypeKind::Enumeration) {
                    out = g;   // une valeur d'enumeration (Auto)
                    return {};
                }
            return "un " + type + " n'a pas de valeur initiale ici (laisse-la vide ; ses membres prennent les leurs)";
        }
        if (t == "BOOL") {
            const std::string l = lower(g);
            if (l == "true" || l == "1" || l == "vrai") out = "TRUE";
            else if (l == "false" || l == "0" || l == "faux") out = "FALSE";
            else return "TRUE ou FALSE est attendu pour un BOOL (pas \xC2\xAB " + g + " \xC2\xBB)";
            return {};
        }
        if (t == "STRING") {
            if (g.size() >= 2 && g.front() == '\'' && g.back() == '\'') out = g;
            else {
                out = "'";
                for (const char c : g) out += c == '\'' ? std::string("$'") : std::string(1, c);
                out += "'";
            }
            return {};
        }
        if (t == "TIME") {
            std::int64_t ms = 0;
            if (upperOf(g).rfind("T#", 0) == 0) out = g;
            else if (hmi::roles::parseDuration(g, ms, hmi::roles::Unit::Seconds)) out = "T#" + std::to_string(ms) + "ms";
            else return "une dur\xC3\xA9" "e est attendue pour un TIME (T#5s, 500 ms)";
            return {};
        }
        double d = 0;
        if (!hmi::roles::readNumber(g, d)) return "un nombre est attendu pour un " + t + " (pas \xC2\xAB " + g + " \xC2\xBB)";
        if (t == "REAL" || t == "LREAL") {
            out = hmi::formatNumber(d);
            return {};
        }
        if (d != static_cast<double>(static_cast<long long>(d))) return "un nombre entier est attendu pour un " + t;
        struct Range { const char* type; double lo, hi; };
        for (const Range r : {Range{"INT", -32768, 32767}, Range{"UINT", 0, 65535}, Range{"WORD", 0, 65535}, Range{"SINT", -128, 127},
                              Range{"USINT", 0, 255}, Range{"BYTE", 0, 255}, Range{"DINT", -2147483648.0, 2147483647.0},
                              Range{"UDINT", 0, 4294967295.0}, Range{"DWORD", 0, 4294967295.0}})
            if (t == r.type && (d < r.lo || d > r.hi))
                return hmi::formatNumber(d) + " ne tient pas dans un " + t + " (" + hmi::formatNumber(r.lo) + " \xC3\xA0 " + hmi::formatNumber(r.hi) + ")";
        out = std::to_string(static_cast<long long>(d));
        return {};
    }

    // CREER_VARIABLES : des variables de l'IHM, les regles du logiciel (le volet Variables IHM) : un nom
    // valable et libre (ni une variable IHM, ni une fonction, ni une variable de l'automate, ni plus haut),
    // un type connu (sinon les plus proches), la valeur initiale lue selon le type.
    bool variables(const Operation& op, std::size_t index) {
        if (op.variables.empty()) return fail("creer_variables ne porte aucune variable (\xC2\xAB variables \xC2\xBB est vide).");
        if (op.variables.size() > 500) return fail("creer_variables : 500 variables au plus en une proposition.");
        for (const auto& spec : op.variables) {
            const std::string name = trimmed(spec.name);
            const std::string at = "Variable \xC2\xAB " + spec.name + " \xC2\xBB : ";
            std::string bad;
            if (!hmi::isIdentifier(name)) bad = "nom invalide (une lettre ou _, puis des lettres sans accent, des chiffres ou _)";
            else if (hmi::isReservedWord(name) || iequal(name, "SYS") || iequal(name, "API")) bad = "\xC2\xAB " + name + " \xC2\xBB est un mot r\xC3\xA9serv\xC3\xA9";
            else if (const auto* v = project.variable(name)) bad = "une variable IHM porte d\xC3\xA9j\xC3\xA0 ce nom (" + v->name + ")";
            else if (newVar(name)) bad = "elle est d\xC3\xA9j\xC3\xA0 cr\xC3\xA9\xC3\xA9" "e plus haut dans la proposition";
            else if (std::any_of(project.programs.functions.begin(), project.programs.functions.end(),
                                 [&](const hmi::HmiFunction& f) { return iequal(f.name, name); }))
                bad = "une fonction de l'IHM porte ce nom";
            else if (trimmed(spec.type).empty()) bad = "son type manque (BOOL, INT, REAL, STRING... ou un type IHM)";
            hmi::Variable v;
            if (bad.empty()) {
                v.type = hmi::types::normalized(trimmed(spec.type));
                std::string reason;
                hmi::types::Spec ts;
                const std::string element = hmi::types::parseSpec(v.type, ts) ? ts.element : v.type;
                bool later = false;
                for (std::size_t j = index + 1; j < p.ops.size() && !project.hmiTypeByName(element) && !newType(element); ++j)
                    if (p.ops[j].action == Operation::Action::Type && iequal(trimmed(p.ops[j].name), element)) later = true;
                if (later) bad = "son type " + element + " est cr\xC3\xA9\xC3\xA9 PLUS BAS dans la proposition : le creer_type doit venir avant";
                else if (!typeKnown(v.type, &reason)) bad = unknownType(trimmed(spec.type), reason);
                else if (auto b2 = initialValue(v.type, spec.initial, v.initial); !b2.empty()) bad = "valeur initiale : " + b2;
            }
            if (!bad.empty()) {
                if (!fail(at + bad + ".")) return false;
                continue;
            }
            // Le nom d'une globale de l'automate : voulu pour une IHM autonome (la migration 1.12.0 fait de meme ;
            // l'automate relie s'ecrit API.Nom) - dit, pas refuse.
            if (environment().plcName && environment().plcName(name))
                warn("La variable IHM " + name + " porte le nom d'une variable de l'automate reli\xC3\xA9 (API." + name
                     + ") : c'est l'IHM qui la lira sous ce nom.");
            v.id = nextId++;
            v.name = name;
            v.description = trimmed(spec.description).empty() ? "Cr\xC3\xA9\xC3\xA9" "e par l'Assistant IA (demande : " + p.request + ")"
                                                              : trimmed(spec.description);
            if (check) check->newVariables.push_back({v.name, v.type, v.description, v.initial});
            newVars.push_back(std::move(v));
        }
        return true;
    }

    // CREER_TYPE : un type IHM (une structure), comme Types IHM > Nouveau : un nom libre, des membres (un nom
    // unique, un type connu ou cree PLUS HAUT dans la proposition - l'ordre des dependances).
    bool makeType(const Operation& op, std::size_t index) {
        const std::string name = trimmed(op.name);
        std::string bad;
        if (!hmi::isIdentifier(name)) bad = "nom invalide (une lettre ou _, puis des lettres sans accent, des chiffres ou _)";
        else if (hmi::types::isElementary(name)) bad = name + " est un type \xC3\xA9l\xC3\xA9mentaire";
        else if (const auto* t = project.hmiTypeByName(name)) bad = "le type IHM " + t->name + " existe d\xC3\xA9j\xC3\xA0";
        else if (newType(name)) bad = "il est d\xC3\xA9j\xC3\xA0 cr\xC3\xA9\xC3\xA9 plus haut dans la proposition";
        else if (op.members.empty()) bad = "il n'a aucun membre (une structure en a au moins un : nom, type)";
        if (!bad.empty()) return fail("Type \xC2\xAB " + op.name + " \xC2\xBB : " + bad + ".");
        hmi::HmiType t;
        t.name = name;
        for (const auto& [k, v] : op.props)
            if (k == "description") t.description = v;
        if (t.description.empty()) t.description = "Cr\xC3\xA9\xC3\xA9 par l'Assistant IA (demande : " + p.request + ")";
        std::vector<Operation::MemberSpec> shown;
        for (const auto& m : op.members) {
            const std::string mn = trimmed(m.name), raw = trimmed(m.type);
            const std::string mt = hmi::types::normalized(raw);
            std::string mb;
            hmi::types::Spec spec;
            const std::string element = hmi::types::parseSpec(mt, spec) ? spec.element : mt;
            if (!hmi::isIdentifier(mn)) mb = "nom de membre invalide (lettres sans accent, chiffres et _)";
            else if (std::any_of(t.members.begin(), t.members.end(), [&](const hmi::TypeMember& x) { return iequal(x.name, mn); }))
                mb = "ce membre existe d\xC3\xA9j\xC3\xA0 dans le type";
            else if (raw.empty()) mb = "son type manque";
            else if (iequal(element, name)) mb = "un type IHM ne se contient pas lui-m\xC3\xAAme";
            else if (!hmi::types::isElementary(element) && !project.hmiTypeByName(element) && !newType(element)) {
                bool later = false;
                for (std::size_t j = index + 1; j < p.ops.size(); ++j)
                    if (p.ops[j].action == Operation::Action::Type && iequal(trimmed(p.ops[j].name), element)) later = true;
                if (later) mb = "le type " + element + " est cr\xC3\xA9\xC3\xA9 PLUS BAS dans la proposition : son creer_type doit venir avant celui-ci (l'ordre des d\xC3\xA9pendances)";
            }
            if (mb.empty()) {
                std::string reason;
                if (!typeKnown(mt, &reason, true)) mb = unknownType(raw, reason);
            }
            if (!mb.empty()) {
                if (!fail("Type " + name + ", membre \xC2\xAB " + m.name + " \xC2\xBB : " + mb + ".")) return false;
                continue;
            }
            hmi::TypeMember tm;
            tm.name = mn;
            tm.type = mt;
            tm.description = trimmed(m.description);
            t.members.push_back(std::move(tm));
            shown.push_back({mn, mt, trimmed(m.description)});
        }
        t.id = nextId++;
        if (check) check->newTypes.emplace_back(name, std::move(shown));
        newTypes.push_back(std::move(t));
        return true;
    }

    bool go() {
        for (std::size_t index = 0; index < p.ops.size(); ++index) {
            const Operation& op = p.ops[index];
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
                // 1.16.0 : la vue neuve, les variables, les types IHM.
                case Operation::Action::View: ok = makeView(op, index); break;
                case Operation::Action::Variables: ok = variables(op, index); break;
                case Operation::Action::Type: ok = makeType(op, index); break;
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

// 1.16.0 : la proposition cree-t-elle des variables ou des types (le projet change, pas seulement une vue) ?
bool createsProjectItems(const Proposal& p) noexcept {
    return std::any_of(p.ops.begin(), p.ops.end(), [](const Operation& op) {
        return op.action == Operation::Action::Variables || op.action == Operation::Action::Type || op.action == Operation::Action::View;
    });
}

// Les variables qu'une proposition demande (associer, la propriete variable d'un creer, d'un modifier) ;
// 1.16.0 : sans celles qu'elle cree elle-meme (creer_variables).
std::vector<std::string> requestedVariables(const Proposal& p) {
    std::set<std::string> made;
    for (const auto& op : p.ops)
        if (op.action == Operation::Action::Variables)
            for (const auto& v : op.variables) made.insert(upperOf(trimmed(v.name)));
    std::vector<std::string> out;
    for (const auto& op : p.ops) {
        if (op.action == Operation::Action::Bind && !op.variable.empty()) out.push_back(op.variable);
        if (op.action == Operation::Action::Create || op.action == Operation::Action::Modify)
            for (const auto& [k, v] : op.props)
                if (k == "variable" && !v.empty()) out.push_back(v);
    }
    std::erase_if(out, [&](const std::string& n) { return made.count(upperOf(rootOf(n))) > 0; });
    return out;
}

// 1.16.0 : l'etat des variables et des types qu'une proposition cree, tel qu'il est dans le projet
// (absents, ou leur definition) - la part "projet" de son empreinte ; vide : elle n'en cree pas.
std::string createdState(const hmi::Project& project, const Proposal& p) {
    std::string out;
    for (const auto& op : p.ops) {
        if (op.action == Operation::Action::Variables)
            for (const auto& spec : op.variables) {
                const std::string name = trimmed(spec.name);
                out += "V:" + name + "=";
                if (const auto* v = project.variable(name)) out += v->type + "|" + v->initial + "|" + v->description;
                else out += "-";
                out += ";";
            }
        if (op.action == Operation::Action::Type) {
            const std::string name = trimmed(op.name);
            out += "T:" + name + "=";
            if (const auto* t = project.hmiTypeByName(name)) {
                for (const auto& m : t->members) out += m.name + ":" + m.type + ",";
            } else {
                out += "-";
            }
            out += ";";
        }
    }
    return out;
}

std::string hashText(std::string_view text) {
    std::uint64_t h = 1469598103934665603ull;
    for (const char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    char buf[20];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
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
        case Operation::Action::View: return "creer_vue";
        case Operation::Action::Variables: return "creer_variables";
        case Operation::Action::Type: return "creer_type";
    }
    return "creer";
}

std::optional<Operation::Action> actionFromKey(std::string_view k) noexcept {
    for (const auto a : {Operation::Action::Create, Operation::Action::Modify, Operation::Action::Bind, Operation::Action::Align,
                         Operation::Action::Distribute, Operation::Action::Remove, Operation::Action::Duplicate, Operation::Action::Code,
                         Operation::Action::View, Operation::Action::Variables, Operation::Action::Type})
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

// ---- 1.16.0 : la portee, l'empreinte, la vue neuve ---------------------------------------------
bool createsView(const Proposal& p) noexcept { return !p.ops.empty() && p.ops.front().action == Operation::Action::View; }

std::string newViewName(const Proposal& p) { return createsView(p) ? trimmed(p.ops.front().name) : std::string{}; }

bool usesOpenView(const Proposal& p) noexcept {
    if (createsView(p)) return false;
    // Une proposition vide (une ancienne conversation) : la vue visee, comme avant.
    if (p.ops.empty()) return true;
    return std::any_of(p.ops.begin(), p.ops.end(), [](const Operation& op) {
        return op.action != Operation::Action::Variables && op.action != Operation::Action::Type && op.action != Operation::Action::View;
    });
}

std::string proposalPrint(const hmi::Project& project, const Proposal& p, std::string_view viewPrint) {
    std::string s;
    if (createsView(p)) {
        const hmi::View* v = p.viewId != hmi::kNoId ? project.view(p.viewId) : nullptr;
        if (!v) v = project.viewByName(p.ops.front().name);
        s = "vue:" + (v ? fingerprint(*v) : std::string("absente"));
    } else if (usesOpenView(p)) {
        s = std::string(viewPrint);
    } else {
        s = "projet";
    }
    if (const std::string state = createdState(project, p); !state.empty()) s += "+" + hashText(state);
    return s;
}

std::string proposalPrint(const hmi::Project& project, const Proposal& p) {
    std::string view;
    if (usesOpenView(p))
        if (const hmi::View* v = targetView(project, p)) view = fingerprint(*v);
    return proposalPrint(project, p, view);
}

std::string creationSummary(const Proposal& p) {
    std::size_t vars = 0, types = 0;
    std::string view;
    for (const auto& op : p.ops) {
        if (op.action == Operation::Action::View && view.empty()) view = op.name;
        if (op.action == Operation::Action::Variables) vars += op.variables.size();
        if (op.action == Operation::Action::Type) ++types;
    }
    std::vector<std::string> parts;
    if (!view.empty()) parts.push_back("la vue " + view);
    if (vars > 0) parts.push_back(std::to_string(vars) + (vars > 1 ? " variables IHM" : " variable IHM"));
    if (types > 0) parts.push_back(std::to_string(types) + (types > 1 ? " types IHM" : " type IHM"));
    return joinList(parts);
}

hmi::View newViewFor(const hmi::Project& project, const Operation& op, hmi::Id& nextId) {
    // Comme "Nouvelle vue" (hmiNewViewCommand) : hmi::makeView - un premier calque actif, la taille du
    // projet - puis la taille, le fond et la description demandes.
    hmi::Project ids;
    ids.nextId = nextId;
    ids.config.width = project.config.width;
    ids.config.height = project.config.height;
    hmi::View v = hmi::makeView(ids, trimmed(op.name));
    nextId = ids.nextId;
    v.role = "vue";
    for (const auto& [k, val] : op.props) {
        double d = 0;
        if (k == "largeur" && hmi::roles::readNumber(val, d) && d >= 50 && d <= 20000) v.width = static_cast<int>(d);
        if (k == "hauteur" && hmi::roles::readNumber(val, d) && d >= 50 && d <= 20000) v.height = static_cast<int>(d);
        if (k == "fond" && hmi::roles::colorOk(val)) v.background = val;
        if (k == "description") v.description = val;
    }
    if (v.description.empty()) v.description = "Cr\xC3\xA9\xC3\xA9" "e par l'Assistant IA";
    return v;
}

Check validate(const hmi::Project& project, const Proposal& p, const Env& env) {
    Check check;
    hmi::Id next = project.nextId;
    hmi::View work;
    if (createsView(p)) {
        work = newViewFor(project, p.ops.front(), next);
    } else if (usesOpenView(p)) {
        const hmi::View* v = targetView(project, p);
        if (!v) {
            check.viewMissing = true;
            check.errors.push_back("La vue \xC2\xAB " + p.view + " \xC2\xBB n'existe plus dans le projet : rien ne peut s'appliquer.");
            return check;
        }
        work = *v;
    }
    if (!p.fingerprint.empty() && proposalPrint(project, p) != p.fingerprint) {
        check.stale = true;
        if (usesOpenView(p))
            check.errors.push_back("La vue \xC2\xAB " + p.view + " \xC2\xBB a chang\xC3\xA9 depuis cette proposition : elle ne s'applique pas "
                                   "sur un \xC3\xA9tat qui n'est plus le sien. Redemande-la.");
        else
            check.errors.push_back("Le projet a chang\xC3\xA9 depuis cette proposition (une vue, une variable ou un type du m\xC3\xAAme nom "
                                   "existe maintenant) : elle ne s'applique plus telle quelle. Redemande-la.");
    }
    Run run{project, work, next, p, &env, &check};
    (void)run.go();
    return check;
}

bool applyOps(const hmi::Project& project, hmi::View& view, hmi::Id& nextId, const Proposal& p, Touched* touched, std::string* why,
              bool skipRemovals, const Env* env, bool lenient) {
    Run run{project, view, nextId, p, env, nullptr, touched, skipRemovals};
    run.preview = lenient;   // 1.16.1 : ce qui est en erreur est laisse de cote (comme l'apercu)
    const bool ok = run.go();
    if (!ok && why) *why = run.why;
    return ok;
}

namespace {
core::CommandPtr commandFor(const hmi::DocumentPtr& doc, const Proposal& p, std::string* why, const Env& env, std::vector<std::string>* skipped) {
    if (!doc) {
        if (why) *why = "Pas de projet IHM ouvert.";
        return nullptr;
    }
    const Check check = validate(doc->project, p, env);
    const bool lenient = skipped != nullptr && !check.stale && !check.viewMissing;
    if (!check.ok() && !lenient) {
        if (why) *why = check.errors.front();
        return nullptr;
    }
    if (skipped) *skipped = check.errors;
    const bool onView = usesOpenView(p);
    const hmi::View* v = onView ? targetView(doc->project, p) : nullptr;
    const hmi::Id viewId = v ? v->id : hmi::kNoId;
    const std::string label = "Assistant IA : " + (p.summary.empty() ? std::string("proposition") : p.summary);
    // Une variable a creer (choisie par l'utilisateur) : le projet ET la vue, dans une seule commande.
    std::string createVar;
    if (p.variable.create && p.variable.use.empty())
        for (const auto& name : requestedVariables(p))
            if (!variableExists(doc->project, name, env)) createVar = name;
    std::string failed;
    core::CommandPtr cmd;
    if (!createVar.empty() || createsProjectItems(p)) {
        // 1.16.0 : UNE commande pour tout - la vue neuve, ses objets, les variables, les types (Ctrl+Z
        // reprend tout ensemble). Sur des copies : rien a moitie (une operation qui echoue ne laisse rien).
        cmd = hmi::changeProject(doc, label, [&](hmi::Project& pr) {
            const hmi::Id savedNext = pr.nextId;
            const std::size_t savedVars = pr.programs.variables.size();
            if (!createVar.empty()) {
                hmi::Variable var;
                var.id = pr.allocate();
                var.name = createVar;
                var.type = p.variable.type.empty() ? "BOOL" : p.variable.type;
                var.initial = var.type == "BOOL" ? "FALSE" : "0";
                var.description = "Cr\xC3\xA9\xC3\xA9" "e par l'Assistant IA (demande : " + p.request + ")";
                pr.programs.variables.push_back(std::move(var));
            }
            hmi::View work;
            if (createsView(p)) work = newViewFor(pr, p.ops.front(), pr.nextId);
            else if (const hmi::View* target = pr.view(viewId)) work = *target;
            Run run{pr, work, pr.nextId, p, &env, nullptr, nullptr, false};
            run.preview = lenient && !check.ok();   // 1.16.1 : appliquer malgre les erreurs
            if (!run.go()) {
                failed = run.why.empty() ? std::string("La proposition ne s'applique pas.") : run.why;
                pr.programs.variables.resize(savedVars);
                pr.nextId = savedNext;
                return;
            }
            for (auto& t : run.newTypes) pr.programs.types.push_back(std::move(t));
            for (auto& nv : run.newVars) pr.programs.variables.push_back(std::move(nv));
            if (createsView(p)) {
                // La vue de demarrage : une vue ordinaire, s'il n'y en a pas (comme "Nouvelle vue").
                if (pr.config.startView == hmi::kNoId) pr.config.startView = work.id;
                pr.views.push_back(std::move(work));
            } else if (hmi::View* target = pr.view(viewId)) {
                *target = std::move(work);
            }
        });
    } else {
        cmd = hmi::changeView(doc, viewId, label, [&](hmi::Project& pr, hmi::View& view) {
            hmi::View work = view;
            if (applyOps(pr, work, pr.nextId, p, nullptr, &failed, false, &env, lenient && !check.ok())) view = std::move(work);
        });
    }
    if (!failed.empty()) {
        if (why) *why = failed;
        return nullptr;
    }
    if (!cmd && why) *why = "La proposition ne change rien au projet.";
    return cmd;
}
} // namespace

core::CommandPtr makeCommand(const hmi::DocumentPtr& doc, const Proposal& p, std::string* why, const Env& env) {
    return commandFor(doc, p, why, env, nullptr);
}

core::CommandPtr makeCommandLenient(const hmi::DocumentPtr& doc, const Proposal& p, std::vector<std::string>& skipped, std::string* why,
                                    const Env& env) {
    return commandFor(doc, p, why, env, &skipped);
}

Preview preview(const hmi::Project& project, const Proposal& p, const Env& env) {
    Preview out;
    hmi::Id next = project.nextId;
    if (createsView(p)) {
        out.view = newViewFor(project, p.ops.front(), next);
    } else if (usesOpenView(p)) {
        const hmi::View* v = targetView(project, p);
        if (!v) {
            out.why = "La vue \xC2\xAB " + p.view + " \xC2\xBB n'existe plus.";
            return out;
        }
        out.view = *v;
    } else {
        // Ni vue ni objet : des variables, des types (l'apercu de la carte le dit, la liste les montre).
        out.why = "Rien \xC3\xA0 dessiner : la proposition cr\xC3\xA9" "e " + creationSummary(p) + " (la liste de l'aper\xC3\xA7u les montre).";
        return out;
    }
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
            // 1.16.0 : la vue neuve (nom, proprietes : largeur, hauteur, fond, description), les variables,
            // le type IHM (nom, description, membres).
            case Operation::Action::View:
                o.set("nom", op.name);
                break;
            case Operation::Action::Variables: {
                json::Value list = json::Value::array();
                for (const auto& v : op.variables) {
                    json::Value x = json::Value::object();
                    x.set("nom", v.name);
                    x.set("type", v.type);
                    if (!v.description.empty()) x.set("description", v.description);
                    if (!v.initial.empty()) x.set("valeur", v.initial);
                    list.push(std::move(x));
                }
                o.set("variables", std::move(list));
                break;
            }
            case Operation::Action::Type: {
                o.set("nom", op.name);
                for (const auto& [k, v] : op.props)
                    if (k == "description") o.set("description", v);
                json::Value list = json::Value::array();
                for (const auto& m : op.members) {
                    json::Value x = json::Value::object();
                    x.set("nom", m.name);
                    x.set("type", m.type);
                    if (!m.description.empty()) x.set("description", m.description);
                    list.push(std::move(x));
                }
                o.set("membres", std::move(list));
                break;
            }
            default: {
                json::Value list = json::Value::array();
                for (const auto& n : op.objects) list.push(n);
                o.set("objets", std::move(list));
                break;
            }
        }
        if (!op.props.empty() && op.action != Operation::Action::Type) {
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
            // 1.16.0 : seulement s'ils servent (une proposition de 1.15.0 s'ecrit comme avant).
            if (op.count != 1) o.set("copies", static_cast<std::int64_t>(op.count));
            if (op.markers) o.set("reperes", true);
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
        const bool named = *action == Operation::Action::Create || *action == Operation::Action::View || *action == Operation::Action::Type;
        op.name = named ? o.str("nom") : o.str("objet");
        for (const auto& n : o.at("objets").items()) op.objects.push_back(n.asString());
        for (const auto& [k, v] : o.at("proprietes").members())
            op.props.emplace_back(k, v.isString() ? v.asString() : v.isNumber() ? hmi::formatNumber(v.asNumber()) : v.isBool() ? (v.asBool() ? "TRUE" : "FALSE") : std::string{});
        op.variable = o.str("variable");
        op.edge = *action == Operation::Action::Distribute ? o.str("sens") : o.str("bord");
        for (const auto& n : o.at("noms").items()) op.names.push_back(n.asString());
        op.dx = o.num("dx", 20);
        op.dy = o.num("dy", 20);
        op.code = o.str("code");
        // 1.16.0 : les variables, le type (sa description, ses membres), les copies et les reperes.
        for (const auto& v : o.at("variables").items())
            op.variables.push_back({v.str("nom"), v.str("type"), v.str("description"),
                                    v.at("valeur").isString() ? v.str("valeur")
                                    : v.at("valeur").isNumber() ? hmi::formatNumber(v.at("valeur").asNumber())
                                    : v.at("valeur").isBool() ? (v.at("valeur").asBool() ? std::string("TRUE") : std::string("FALSE"))
                                                              : std::string{}});
        for (const auto& m : o.at("membres").items()) op.members.push_back({m.str("nom"), m.str("type"), m.str("description")});
        if (*action == Operation::Action::Type && o.at("description").isString() && !o.str("description").empty())
            op.props.emplace_back("description", o.str("description"));
        op.count = static_cast<int>(o.integer("copies", 1));
        op.markers = o.flag("reperes");
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
