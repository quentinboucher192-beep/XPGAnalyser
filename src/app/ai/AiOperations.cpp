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
#include "../../hmi/HmiAddressPlan.hpp"  // 1.18.0 : adresser (la formule de lien des cases)
#include "../../hmi/HmiAssets.hpp"       // 1.18.0 : creer_ressource (makeResource)
#include "../../hmi/HmiCheck.hpp"        // 1.18.0 : compiler les scripts, fonctions et alarmes crees
#include "../../hmi/HmiEquipment.hpp"    // 1.18.0 : creer_equipement, adresser
#include "../../hmi/HmiFolders.hpp"      // 1.18.0 : creer_dossier, ranger
#include "../../hmi/HmiMigrate.hpp"      // 1.18.0 : les blocs VAR d'un code cree deviennent ses declarations

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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
            } else if (k == "role") {
                // 1.18.0 : un symbole, une popup, un ecran modele, un en-tete, un pied de page.
                const std::string r = lower(trimmed(v));
                if (r != "vue" && r != "popup" && r != "symbole" && r != "modele" && r != "entete" && r != "pied")
                    if (!fail("Vue " + op.name + " : r\xC3\xB4le \xC2\xAB " + v + " \xC2\xBB inconnu (vue, popup, symbole, modele, entete, pied).")) return false;
            } else if (k != "description" && k != "titre" && k != "modale" && k != "dossier") {
                if (!fail("Vue " + op.name + " : \xC2\xAB " + k + " \xC2\xBB n'est pas un r\xC3\xA9glage d'une vue (largeur, hauteur, fond, description, role, "
                          "titre, modale, dossier).")) return false;
            }
        }
        // 1.18.0 : les parametres et les generiques d'un symbole ou d'une popup.
        const std::string role = lower(prop(op, "role").empty() ? std::string("vue") : prop(op, "role"));
        const bool takesParams = role == "symbole" || role == "popup";
        std::vector<hmi::GenericParam> gens;
        std::string gwhy;
        if (!op.generics.empty() && !takesParams)
            if (!fail("Vue " + op.name + " : seuls un symbole et une popup ont des g\xC3\xA9n\xC3\xA9riques.")) return false;
        if (!genericsOf(op.generics, gens, &gwhy))
            if (!fail("Vue " + op.name + " : g\xC3\xA9n\xC3\xA9riques : " + gwhy + ".")) return false;
        if (!op.params.empty() && !takesParams)
            if (!fail("Vue " + op.name + " : seuls un symbole et une popup ont des param\xC3\xA8tres.")) return false;
        std::vector<std::string> seen, shown;
        for (const auto& prm : op.params) {
            const std::string pn = trimmed(prm.name), pt = hmi::types::normalized(trimmed(prm.type));
            std::string bad = identifierProblem(pn);
            if (bad.empty() && std::any_of(seen.begin(), seen.end(), [&](const std::string& x) { return iequal(x, pn); })) bad = "deux param\xC3\xA8tres de ce nom";
            if (bad.empty() && !pt.empty()) {
                const bool generic = std::any_of(gens.begin(), gens.end(), [&](const hmi::GenericParam& g) { return iequal(g.name, pt); });
                std::string reason;
                if (!generic && !typeKnown(pt, &reason)) bad = unknownType(pt, reason);
            }
            if (!bad.empty()) {
                if (!fail("Vue " + op.name + ", param\xC3\xA8tre \xC2\xAB " + prm.name + " \xC2\xBB : " + bad + ".")) return false;
                continue;
            }
            seen.push_back(pn);
            shown.push_back(pn + " : " + (pt.empty() ? std::string("ANY") : pt) + (trimmed(prm.initial).empty() ? std::string{} : " = " + trimmed(prm.initial)));
        }
        if (check) check->newView = op.name;
        if (role != "vue") {
            static const std::map<std::string, std::string> label = {{"popup", "Popup"}, {"symbole", "Symbole"}, {"modele", "\xC3\x89" "cran mod\xC3\xA8le"},
                                                                     {"entete", "En-t\xC3\xAAte"}, {"pied", "Pied de page"}};
            made(label.at(role), op.name + (gens.empty() ? std::string{} : "<" + hmi::genericsText(gens) + ">"),
                 shown.empty() ? std::string("sans param\xC3\xA8tre") : joinList(shown));
        }
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
        // 1.18.0 : une structure generique (T_Pile<T>, T_Paire<T, K>) - ses membres peuvent etre de type T, K.
        if (std::string gwhy; !genericsOf(op.generics, t.generics, &gwhy)) return fail("Type " + name + " : g\xC3\xA9n\xC3\xA9riques : " + gwhy + ".");
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
                if (!t.generics.empty()) {
                    // Le type lui-meme (ses generiques) dans le projet d'essai : LIST OF T s'y verifie avec un type de T.
                    hmi::Project pr;
                    pr.programs.types = project.programs.types;
                    pr.programs.types.insert(pr.programs.types.end(), newTypes.begin(), newTypes.end());
                    pr.programs.types.push_back(t);
                    if (!hmi::types::validMemberType(pr, mt, &reason, name)) mb = unknownType(raw, reason);
                } else if (!typeKnown(mt, &reason, true)) {
                    mb = unknownType(raw, reason);
                }
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
        if (check) check->newTypes.emplace_back(t.generics.empty() ? name : name + "<" + hmi::genericsText(t.generics) + ">", std::move(shown));
        newTypes.push_back(std::move(t));
        return true;
    }

    // =========================================================================================
    //  1.18.0 : LES OUTILS DE L'IA - des elements du projet, crees ou touches comme par l'ecran
    //  qui les cree (memes regles, memes noms), montres dans l'apercu, poses par UNE commande.
    // =========================================================================================
    std::vector<hmi::Equipment>   newEquipments{};
    std::vector<hmi::Script>      newScripts{};
    std::vector<hmi::HmiFunction> newFunctions{};
    std::vector<hmi::AlarmDef>    newAlarms{};
    std::vector<hmi::Resource>    newResources{};
    std::vector<hmi::Variable>    addressed{};   // des variables du projet, adressees (remplacent celles du meme id)
    struct FolderOp {
        bool                     variables{false};
        hmi::fold::List          list{hmi::fold::List::Views};
        std::string              path;
        std::vector<std::string> names;          // vide : creer le dossier seulement
    };
    std::vector<FolderOp>         folderOps{};

    [[nodiscard]] static std::string prop(const Operation& op, std::string_view key) {
        for (const auto& [k, v] : op.props)
            if (k == key) return trimmed(v);
        return {};
    }
    void made(std::string what, std::string name, std::string detail) {
        if (check) check->made.push_back({std::move(what), std::move(name), std::move(detail)});
    }
    std::string created() const { return "Cr\xC3\xA9\xC3\xA9 par l'Assistant IA (demande : " + p.request + ")"; }
    static bool readBool(const std::string& v, bool fallback) {
        const std::string l = lower(v);
        if (l == "true" || l == "1" || l == "vrai" || l == "oui") return true;
        if (l == "false" || l == "0" || l == "faux" || l == "non") return false;
        return fallback;
    }
    // Un nombre entier dans [lo, hi] ; vide : `fallback`. Faux : illisible ou hors bornes.
    static bool readInt(const std::string& v, long long lo, long long hi, long long fallback, long long& out) {
        if (v.empty()) {
            out = fallback;
            return true;
        }
        double d = 0;
        if (!hmi::roles::readNumber(v, d) || d != static_cast<double>(static_cast<long long>(d)) || d < static_cast<double>(lo) || d > static_cast<double>(hi))
            return false;
        out = static_cast<long long>(d);
        return true;
    }
    [[nodiscard]] const hmi::Equipment* newEquipment(std::string_view name) const {
        for (const auto& e : newEquipments)
            if (iequal(e.name, name)) return &e;
        return nullptr;
    }
    [[nodiscard]] const hmi::Equipment* equipmentNamed(std::string_view name) const {
        if (const auto* e = project.equipmentByName(name)) return e;
        return newEquipment(name);
    }
    hmi::Variable* newVarMut(std::string_view name) {
        for (auto& v : newVars)
            if (iequal(v.name, name)) return &v;
        return nullptr;
    }
    // Le projet tel que la proposition le laisserait (les types, variables, equipements, scripts, fonctions, alarmes crees
    // plus haut) : pour les controles qui lisent un projet (le plan d'adresses, le compilateur).
    [[nodiscard]] hmi::Project probe() const {
        hmi::Project pr = project;
        for (const auto& t : newTypes) pr.programs.types.push_back(t);
        for (const auto& v : newVars) pr.programs.variables.push_back(v);
        for (const auto& v : addressed)
            if (auto* x = pr.variableById(v.id)) *x = v;
        for (const auto& e : newEquipments) pr.equipments.push_back(e);
        for (const auto& s : newScripts) pr.programs.scripts.push_back(s);
        for (const auto& f : newFunctions) pr.programs.functions.push_back(f);
        for (const auto& a : newAlarms) pr.alarms.push_back(a);
        return pr;
    }
    // Un nom d'element du projet : un identifiant ST (lettres sans accent, chiffres, _), pas un mot reserve. Vide : il convient.
    static std::string identifierProblem(const std::string& name) {
        if (!hmi::isIdentifier(name)) return "nom invalide (une lettre ou _, puis des lettres sans accent, des chiffres ou _)";
        if (hmi::isReservedWord(name) || iequal(name, "SYS") || iequal(name, "API")) return "\xC2\xAB " + name + " \xC2\xBB est un mot r\xC3\xA9serv\xC3\xA9";
        return {};
    }
    // "T", "K : ANY_NUM" -> les generiques ; faux et `why`.
    static bool genericsOf(const std::vector<std::string>& in, std::vector<hmi::GenericParam>& out, std::string* why) {
        out.clear();
        if (in.empty()) return true;
        std::string text;
        for (const auto& g : in) text += (text.empty() ? "" : ", ") + trimmed(g);
        return hmi::parseGenerics(text, out, why);
    }

    // CREER_EQUIPEMENT : comme Configuration > Equipements > Nouveau. `detecter` : la detection est PROPOSEE (la carte la
    // montre apres Appliquer) - jamais lancee par l'IA.
    bool equipment(const Operation& op) {
        const std::string name = trimmed(op.name);
        const std::string at = "\xC3\x89quipement \xC2\xAB " + op.name + " \xC2\xBB : ";
        std::string bad;
        if (name.empty() || name.size() > 64) bad = "son nom manque (64 caract\xC3\xA8res au plus)";
        else if (name.find_first_of("\"';|$\n\t") != std::string::npos) bad = "caract\xC3\xA8re interdit dans le nom";
        else if (equipmentNamed(name)) bad = "un \xC3\xA9quipement porte d\xC3\xA9j\xC3\xA0 ce nom";
        if (!bad.empty()) return fail(at + bad + ".");
        hmi::Equipment e;
        e.name = name;
        if (const std::string t = prop(op, "type"); !t.empty()) {
            const std::string l = lower(t);
            if (l == "modbus" || l == "modbus_tcp" || l == "modbus tcp" || l == "modbus tcp/ip") e.type = hmi::EquipmentType::ModbusTcp;
            else if (l == "ethernet" || l == "ethernet_tcp" || l == "ethernet tcp" || l == "ethernet tcp/ip" || l == "ip") e.type = hmi::EquipmentType::EthernetTcp;
            else if (const auto k = hmi::equipmentTypeFrom(t)) e.type = *k;
            else return fail(at + "type \xC2\xAB " + t + " \xC2\xBB inconnu (modbus_tcp ou ethernet_tcp ; Modbus RTU n'est pas encore pris en charge).");
        }
        e.simulated = readBool(prop(op, "simule"), false);
        std::string host = prop(op, "adresse");
        if (host.empty()) host = prop(op, "ip");
        std::uint32_t ip = 0;
        if (host.empty() && !e.simulated) return fail(at + "son adresse IP manque (ex. 192.168.1.40).");
        if (!host.empty()) {
            const bool hostname = host.size() <= 253 && std::all_of(host.begin(), host.end(), [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-';
            }) && std::any_of(host.begin(), host.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)); });
            if (!hmi::equip::parseIpv4(host, ip) && !hostname)
                return fail(at + "adresse \xC2\xAB " + host + " \xC2\xBB illisible (une adresse IPv4 : 192.168.1.40, ou un nom de poste).");
            e.host = host;
        }
        long long n = 0;
        if (!readInt(prop(op, "port"), 0, 65535, e.modbus() ? 502 : 0, n)) return fail(at + "port : un nombre de 0 \xC3\xA0 65535.");
        e.port = static_cast<int>(n);
        if (!readInt(prop(op, "esclave"), 0, 255, 1, n)) return fail(at + "esclave (Unit Id) : un nombre de 0 \xC3\xA0 255.");
        e.unit = static_cast<int>(n);
        if (!readInt(prop(op, "periode"), 50, 60000, 500, n)) return fail(at + "p\xC3\xA9riode : de 50 \xC3\xA0 60000 ms.");
        e.periodMs = static_cast<int>(n);
        e.description = prop(op, "description").empty() ? created() : prop(op, "description");
        e.id = nextId++;
        std::string detail = std::string(hmi::equipmentTypeLabel(e.type)) + (e.host.empty() ? std::string{} : " " + e.host)
                             + (e.port > 0 ? ":" + std::to_string(e.port) : std::string{}) + (e.modbus() ? ", esclave " + std::to_string(e.unit) : std::string{})
                             + (e.simulated ? ", simul\xC3\xA9" : "");
        made("\xC3\x89quipement", name, detail);
        if (op.detect) {
            std::string range;
            if (ip != 0) range = hmi::equip::ipv4Text(hmi::equip::networkOf(ip, 24)) + "/24";
            if (check) check->detections.emplace_back(name, range);
            warn("D\xC3\xA9tecter ce qui r\xC3\xA9pond" + (range.empty() ? std::string{} : " sur " + range)
                 + " : c'est toi qui la lances, apr\xC3\xA8s Appliquer (le bouton de la carte) - rien ne part sur le r\xC3\xA9seau avant.");
        }
        newEquipments.push_back(std::move(e));
        return true;
    }

    // ADRESSER : une variable IHM (du projet ou creee plus haut) liee a un equipement Modbus, a son adresse ; une formule de
    // lien pour les cases d'un tableau de structures (hmi::addr, << Modifier l'adresse dans toutes les cases >>).
    bool address(const Operation& op) {
        const std::string varName = trimmed(op.variable);
        const std::string eqName = prop(op, "equipement");
        const std::string addr = prop(op, "adresse");
        const std::string formula = prop(op, "formule");
        const std::string at = "Adresser \xC2\xAB " + varName + " \xC2\xBB : ";
        hmi::Variable* nv = newVarMut(varName);
        const hmi::Variable* pv = nv ? nullptr : project.variable(varName);
        if (!nv && !pv) {
            if (environment().plcName && environment().plcName(varName))
                return fail(at + "c'est une variable de l'automate reli\xC3\xA9 : seule une variable IHM se lie \xC3\xA0 un \xC3\xA9quipement.");
            std::string msg = at + "variable IHM introuvable (l'Assistant n'invente pas de variable)";
            if (const auto near = nearVariables(project, varName); !near.empty()) msg += " ; les plus proches : " + joinList(near);
            return fail(msg + ".");
        }
        const hmi::Equipment* eq = equipmentNamed(eqName);
        if (!eq) {
            std::vector<std::string> names;
            for (const auto& e : project.equipments) names.push_back(e.name);
            for (const auto& e : newEquipments) names.push_back(e.name);
            return fail(at + "\xC3\xA9quipement \xC2\xAB " + eqName + " \xC2\xBB introuvable"
                        + (names.empty() ? std::string(" (le projet n'en a aucun : creer_equipement d'abord)") : " (les \xC3\xA9quipements : " + joinList(names) + ")") + ".");
        }
        if (!eq->modbus()) return fail(at + eq->name + " est un \xC3\xA9quipement Ethernet TCP/IP : il n'a pas d'adresses (Modbus TCP/IP seulement).");
        hmi::Variable v = nv ? *nv : *pv;
        for (const auto& a : addressed)
            if (!nv && a.id == v.id) v = a;
        v.equipment = eq->name;
        if (!addr.empty()) {
            std::string why;
            if (hmi::equip::canonicalAddress(addr, &why).empty())
                return fail(at + "adresse \xC2\xAB " + addr + " \xC2\xBB illisible" + (why.empty() ? std::string{} : " (" + why + ")")
                            + " (ex. %MW100, %MF20, %M5, %MW10.3, 40101).");
            v.address = addr;
        } else if (formula.empty()) {
            v.address = hmi::equip::nextFreeAddress(probe(), *eq, hmi::equip::typeOfName(v.type));
            warn("Adresser " + v.name + " : aucune adresse donn\xC3\xA9" "e - la premi\xC3\xA8re libre de " + eq->name + " (" + v.address + ").");
        }
        if (const std::string ro = prop(op, "lecture_seule"); !ro.empty()) v.readOnly = readBool(ro, v.readOnly);
        std::string detail = eq->name + " " + v.address;
        if (!formula.empty()) {
            const std::string pattern = prop(op, "motif").empty() ? std::string("[*]") : prop(op, "motif");
            const hmi::Project pr = probe();
            const hmi::addr::Plan plan = hmi::addr::preview(pr, v, pattern, formula);
            if (!plan.errors.empty()) return fail(at + "formule \xC2\xAB " + pattern + " = " + formula + " \xC2\xBB : " + plan.errors.front() + ".");
            if (plan.shared > 0)
                warn("Adresser " + v.name + " : " + std::to_string(plan.shared) + (plan.shared > 1 ? " cases prennent une place d\xC3\xA9j\xC3\xA0 prise" : " case prend une place d\xC3\xA9j\xC3\xA0 prise")
                     + " ailleurs (l'autre variable garde la sienne).");
            v = hmi::addr::withFormula(v, pattern, formula);
            detail += (detail.back() == ' ' ? "" : " ; ") + pattern + " = " + formula + " (" + std::to_string(plan.rows.size()) + " cases)";
        }
        // Le meme mot deja pris par une autre variable de cet equipement : dit, pas refuse (comme l'ecran).
        if (!v.address.empty()) {
            const std::string mine = hmi::equip::canonicalAddress(v.address);
            const auto clash = [&](const hmi::Variable& o) {
                return o.id != v.id && iequal(o.equipment, eq->name) && !o.address.empty() && hmi::equip::canonicalAddress(o.address) == mine;
            };
            for (const auto& o : project.programs.variables)
                if (clash(o)) warn("Adresser " + v.name + " : " + v.address + " est d\xC3\xA9j\xC3\xA0 l'adresse de " + o.name + " sur " + eq->name + ".");
        }
        made("Adresse", v.name, detail + (v.readOnly ? " (lecture seule)" : ""));
        if (nv) *nv = v;
        else {
            bool replaced = false;
            for (auto& a : addressed)
                if (a.id == v.id) {
                    a = v;
                    replaced = true;
                }
            if (!replaced) addressed.push_back(v);
        }
        return true;
    }

    // CREER_SCRIPT : un script general (Programmation generale), du TEXTE - compile pour les diagnostics, jamais execute ici.
    bool script(const Operation& op) {
        const std::string name = trimmed(op.name);
        const std::string at = "Script \xC2\xAB " + op.name + " \xC2\xBB : ";
        if (const std::string bad = identifierProblem(name); !bad.empty()) return fail(at + bad + ".");
        if (project.generalScript(name) || std::any_of(newScripts.begin(), newScripts.end(), [&](const hmi::Script& s) { return iequal(s.name, name); }))
            return fail(at + "un script g\xC3\xA9n\xC3\xA9ral porte d\xC3\xA9j\xC3\xA0 ce nom.");
        hmi::Script sc;
        sc.name = name;
        const std::string lang = prop(op, "langage");
        if (!lang.empty()) {
            const auto l = hmi::scriptLangFromKey(lang);
            if (!l) return fail(at + "langage \xC2\xAB " + lang + " \xC2\xBB inconnu (ST, C ou C++).");
            sc.lang = *l;
        }
        const std::string ev = prop(op, "evenement");
        sc.event = "Appel";
        if (!ev.empty()) {
            std::string found;
            const std::string l = lower(ev);
            for (const auto k : hmi::kGeneralEvents)
                if (lower(k) == l) found = std::string(k);
            if (l == "d\xC3\xA9marrage" || l == "demarrage" || l == "au demarrage" || l == "startup") found = "Demarrage";
            if (l == "cyclique" || l == "cycle" || l == "periodique") found = "Cyclique";
            if (l == "changement" || l == "sur changement") found = "Changement";
            if (l == "appel" || l == "appele") found = "Appel";
            if (found.empty()) return fail(at + "\xC3\xA9v\xC3\xA9nement \xC2\xAB " + ev + " \xC2\xBB inconnu (Demarrage, Cyclique, Changement ou Appel).");
            sc.event = found;
        }
        long long n = 0;
        if (!readInt(prop(op, "periode"), 50, 3600000, 1000, n)) return fail(at + "p\xC3\xA9riode : de 50 \xC3\xA0 3600000 ms.");
        sc.periodMs = static_cast<int>(n);
        sc.watch = prop(op, "surveille");
        if (sc.event == "Changement" && sc.watch.empty()) return fail(at + "un script Changement surveille une expression (\xC2\xAB surveille \xC2\xBB).");
        sc.body = prop(op, "corps").empty() ? op.code : prop(op, "corps");
        if (trimmed(sc.body).empty()) return fail(at + "son code manque (\xC2\xAB corps \xC2\xBB).");
        if (sc.body.size() > 60000) return fail(at + "code trop long (60 000 caract\xC3\xA8res au plus).");
        sc.description = prop(op, "description").empty() ? created() : prop(op, "description");
        sc.folder = prop(op, "dossier");
        sc.id = nextId++;
        std::string detail = std::string(hmi::scriptLangKey(sc.lang)) + ", " + std::string(hmi::eventLabel(sc.event));
        if (sc.event == "Cyclique") detail += " (" + std::to_string(sc.periodMs) + " ms)";
        if (sc.event == "Changement") detail += " (" + sc.watch + ")";
        made("Script", name, detail);
        newScripts.push_back(std::move(sc));
        return true;
    }

    // CREER_FONCTION : une fonction IHM (son retour, ses generiques ; le corps declare ses parametres VAR_INPUT).
    bool function(const Operation& op) {
        const std::string name = trimmed(op.name);
        const std::string at = "Fonction \xC2\xAB " + op.name + " \xC2\xBB : ";
        if (const std::string bad = identifierProblem(name); !bad.empty()) return fail(at + bad + ".");
        if (project.functionByName(name) || std::any_of(newFunctions.begin(), newFunctions.end(), [&](const hmi::HmiFunction& f) { return iequal(f.name, name); }))
            return fail(at + "une fonction porte d\xC3\xA9j\xC3\xA0 ce nom (une surcharge se fait dans l'\xC3\xA9" "diteur des fonctions).");
        if (project.variable(name) || newVar(name)) return fail(at + "une variable IHM porte ce nom.");
        hmi::HmiFunction fn;
        fn.name = name;
        std::string why;
        if (!genericsOf(op.generics, fn.generics, &why)) return fail(at + "g\xC3\xA9n\xC3\xA9riques : " + why + ".");
        fn.returnType = hmi::types::normalized(prop(op, "retour"));
        if (!fn.returnType.empty()) {
            const bool generic = std::any_of(fn.generics.begin(), fn.generics.end(), [&](const hmi::GenericParam& g) { return iequal(g.name, fn.returnType); });
            std::string reason;
            if (!generic && !typeKnown(fn.returnType, &reason)) return fail(at + "retour : " + unknownType(fn.returnType, reason) + ".");
        }
        fn.body = prop(op, "corps").empty() ? op.code : prop(op, "corps");
        if (trimmed(fn.body).empty()) return fail(at + "son code manque (\xC2\xAB corps \xC2\xBB : VAR_INPUT ... END_VAR puis le calcul ; le r\xC3\xA9sultat s'affecte \xC3\xA0 son nom).");
        if (fn.body.size() > 60000) return fail(at + "code trop long (60 000 caract\xC3\xA8res au plus).");
        fn.description = prop(op, "description").empty() ? created() : prop(op, "description");
        fn.id = nextId++;
        made("Fonction", name, (fn.returnType.empty() ? std::string("sans retour") : "retour " + fn.returnType)
                                   + (fn.generics.empty() ? std::string{} : ", g\xC3\xA9n\xC3\xA9rique <" + hmi::genericsText(fn.generics) + ">"));
        newFunctions.push_back(std::move(fn));
        return true;
    }

    // CREER_ALARME : IHM > Alarmes (la condition et le message, compiles).
    bool alarm(const Operation& op) {
        const std::string name = trimmed(op.name);
        const std::string at = "Alarme \xC2\xAB " + op.name + " \xC2\xBB : ";
        if (const std::string bad = identifierProblem(name); !bad.empty()) return fail(at + bad + ".");
        if (project.alarmByName(name) || std::any_of(newAlarms.begin(), newAlarms.end(), [&](const hmi::AlarmDef& a) { return iequal(a.name, name); }))
            return fail(at + "une alarme porte d\xC3\xA9j\xC3\xA0 ce nom.");
        hmi::AlarmDef a;
        a.name = name;
        a.condition = prop(op, "condition");
        if (a.condition.empty()) return fail(at + "sa condition manque (une expression bool\xC3\xA9" "enne : Niveau_Cuve_1 > 90).");
        a.message = prop(op, "message").empty() ? name : prop(op, "message");
        long long n = 0;
        if (!readInt(prop(op, "priorite"), 1, hmi::kAlarmPriorities, 3, n)) return fail(at + "priorit\xC3\xA9 : de 1 \xC3\xA0 " + std::to_string(hmi::kAlarmPriorities) + ".");
        a.priority = static_cast<int>(n);
        if (const std::string c = prop(op, "categorie"); !c.empty()) a.category = c;
        a.group = prop(op, "groupe");
        if (!readInt(prop(op, "delai"), 0, 3600000, 0, n)) return fail(at + "d\xC3\xA9lai : de 0 \xC3\xA0 3600000 ms.");
        a.delayMs = static_cast<int>(n);
        a.ackRequired = readBool(prop(op, "acquittement"), true);
        a.instruction = prop(op, "consigne");
        a.description = prop(op, "description").empty() ? created() : prop(op, "description");
        a.id = nextId++;
        made("Alarme", name, "si " + a.condition + ", priorit\xC3\xA9 " + std::to_string(a.priority) + (a.group.empty() ? std::string{} : ", groupe " + a.group));
        newAlarms.push_back(std::move(a));
        return true;
    }

    // CREER_RESSOURCE : une piece jointe de la conversation devient une ressource du projet (copiee, comme Importer).
    bool resource(const Operation& op) {
        std::string file = prop(op, "piece_jointe");
        if (file.empty()) file = prop(op, "fichier");
        const std::string at = "Ressource \xC2\xAB " + (op.name.empty() ? file : op.name) + " \xC2\xBB : ";
        if (file.empty()) return fail(at + "la pi\xC3\xA8" "ce jointe manque (\xC2\xAB piece_jointe \xC2\xBB : le nom du fichier joint \xC3\xA0 la conversation).");
        const std::string path = environment().attachmentPath ? environment().attachmentPath(file) : std::string{};
        if (path.empty()) return fail(at + "\xC2\xAB " + file + " \xC2\xBB n'est pas une pi\xC3\xA8" "ce jointe de cette conversation (joins le fichier d'abord).");
        const std::filesystem::path fp(std::u8string(path.begin(), path.end()));
        std::error_code ec;
        const auto bytes = std::filesystem::file_size(fp, ec);
        if (ec) return fail(at + "le fichier joint ne se lit plus.");
        if (bytes > 200ull * 1024 * 1024) return fail(at + "plus de 200 Mo : trop gros pour une ressource.");
        std::string name = trimmed(op.name);
        if (name.empty()) {
            const auto u = fp.filename().u8string();
            name = std::string(u.begin(), u.end());
        }
        if (name.empty() || name.size() > 128 || name.find_first_of("\"\\/:*?<>|") != std::string::npos) return fail(at + "nom de ressource invalide.");
        if (project.resourceByName(name) || std::any_of(newResources.begin(), newResources.end(), [&](const hmi::Resource& r) { return iequal(r.name, name); }))
            return fail(at + "une ressource porte d\xC3\xA9j\xC3\xA0 ce nom (" + name + ").");
        hmi::Resource r;
        if (!check && !preview) {   // appliquer : le fichier est lu (copie dans le projet)
            std::ifstream in(fp, std::ios::binary);
            auto data = std::make_shared<hmi::Bytes>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (!in.good() && !in.eof()) return fail(at + "le fichier joint ne se lit pas.");
            hmi::Project ids;
            ids.nextId = nextId;
            r = hmi::makeResource(ids, name, std::move(data), path);
            nextId = ids.nextId;
        } else {
            r.id = nextId++;
            r.name = name;
            const auto ext = fp.extension().u8string();
            r.format = upperOf(std::string(ext.begin(), ext.end()).substr(ext.empty() ? 0 : 1));
            r.bytes = bytes;
        }
        r.folder = prop(op, "dossier");
        made("Ressource", name, (r.format.empty() ? std::string("fichier") : r.format) + ", " + std::to_string((bytes + 1023) / 1024) + " Ko (copi\xC3\xA9" "e dans le projet)");
        newResources.push_back(std::move(r));
        return true;
    }

    // CREER_DOSSIER, RANGER : les dossiers des listes (comme le clic droit > Nouveau dossier, et le glisser dans un dossier).
    bool folder(const Operation& op, bool store) {
        std::string list = lower(prop(op, "liste"));
        const std::string path = prop(op, "dossier");
        const std::string at = std::string(store ? "Ranger" : "Dossier") + " \xC2\xAB " + path + " \xC2\xBB : ";
        FolderOp f;
        if (list == "variables" || list == "variable") f.variables = true;
        else {
            if (list == "vue") list = "vues";
            if (list == "popup") list = "popups";
            if (list == "symbole") list = "symboles";
            if (list == "script") list = "scripts";
            if (list == "type") list = "types";
            if (list == "style") list = "styles";
            if (list == "ressource") list = "ressources";
            if (list == "modeles" || list == "ecrans modeles" || list == "modele") list = "ecrans_modeles";
            if (list == "entete" || list == "en-tetes") list = "entetes";
            if (list == "pied") list = "pieds";
            const auto l = hmi::fold::fromKey(list);
            if (!l) return fail(at + "liste \xC2\xAB " + prop(op, "liste") + " \xC2\xBB inconnue (vues, popups, symboles, ecrans_modeles, entetes, pieds, scripts, types, styles, ressources, variables).");
            f.list = *l;
        }
        if (path.empty() || path.size() > 128 || path.front() == '/' || path.back() == '/' || path.find("//") != std::string::npos
            || path.find_first_of("\\:*?\"<>|") != std::string::npos)
            return fail(at + "chemin de dossier invalide (\xC2\xAB Ligne 1/Pompes \xC2\xBB : des noms s\xC3\xA9par\xC3\xA9s par /).");
        f.path = path;
        if (store) {
            if (op.objects.empty()) return fail(at + "rien \xC3\xA0 ranger (\xC2\xAB objets \xC2\xBB : les noms des \xC3\xA9l\xC3\xA9ments).");
            for (const auto& raw : op.objects) {
                const std::string n = trimmed(raw);
                bool known = false;
                if (f.variables) known = project.variable(n) || newVar(n);
                else {
                    for (const auto& it : hmi::fold::items(project, f.list))
                        if (iequal(it.name, n)) known = true;
                    if (f.list == hmi::fold::List::Scripts)
                        known = known || std::any_of(newScripts.begin(), newScripts.end(), [&](const hmi::Script& s) { return iequal(s.name, n); });
                    if (f.list == hmi::fold::List::Types)
                        known = known || std::any_of(newTypes.begin(), newTypes.end(), [&](const hmi::HmiType& t) { return iequal(t.name, n); });
                    if (f.list == hmi::fold::List::Resources)
                        known = known || std::any_of(newResources.begin(), newResources.end(), [&](const hmi::Resource& r) { return iequal(r.name, n); });
                    if (createsView(p) && iequal(newViewName(p), n)) known = true;
                }
                if (!known) {
                    if (!fail(at + "\xC2\xAB " + n + " \xC2\xBB n'est pas dans la liste " + (f.variables ? std::string("variables") : std::string(hmi::fold::key(f.list))) + ".")) return false;
                    continue;
                }
                f.names.push_back(n);
            }
            if (f.names.empty()) return true;
            made("Rangement", path, std::to_string(f.names.size()) + (f.names.size() > 1 ? " \xC3\xA9l\xC3\xA9ments : " : " \xC3\xA9l\xC3\xA9ment : ") + joinList(f.names));
        } else {
            bool exists = false;
            if (f.variables) {
                for (const auto& d : project.programs.folders) exists = exists || iequal(d, path);
            } else {
                for (const auto& d : hmi::fold::allFolders(project, f.list)) exists = exists || iequal(d, path);
            }
            if (exists) {
                warn("Le dossier \xC2\xAB " + path + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0 : rien \xC3\xA0 cr\xC3\xA9" "er.");
                return true;
            }
            made("Dossier", path, "dans " + (f.variables ? std::string("variables") : std::string(hmi::fold::key(f.list))));
        }
        folderOps.push_back(std::move(f));
        return true;
    }

    // Les dossiers, poses sur le projet (apres les elements crees : ils peuvent y etre ranges).
    void applyFolders(hmi::Project& pr) const {
        for (const auto& f : folderOps) {
            if (f.variables) {
                if (std::none_of(pr.programs.folders.begin(), pr.programs.folders.end(), [&](const std::string& d) { return iequal(d, f.path); }))
                    pr.programs.folders.push_back(f.path);
                for (const auto& n : f.names)
                    for (auto& v : pr.programs.variables)
                        if (iequal(v.name, n)) v.folder = f.path;
                continue;
            }
            (void)hmi::fold::addFolder(pr, f.list, f.path);
            std::vector<hmi::Id> ids;
            for (const auto& it : hmi::fold::items(pr, f.list))
                for (const auto& n : f.names)
                    if (iequal(it.name, n)) ids.push_back(it.id);
            if (!ids.empty()) (void)hmi::fold::moveToFolder(pr, f.list, ids, f.path);
        }
    }

    // Les scripts, fonctions et alarmes crees : COMPILES (jamais executes) par le compilateur du logiciel - une erreur est
    // une erreur de la proposition (l'apercu et << appliquer malgre les erreurs >> laissent l'element de cote).
    void compileCreated() {
        if (newScripts.empty() && newFunctions.empty() && newAlarms.empty()) return;
        if (!check && !preview) return;   // appliquer : deja valide
        const hmi::Project pr = probe();
        hmi::CompileFocus focus;
        for (const auto& s : newScripts) focus.scripts.insert(s.id);
        for (const auto& f : newFunctions) focus.functions.insert(f.id);
        for (const auto& a : newAlarms) focus.alarms.insert(a.id);
        // Sans automate relie : aucun nom de l'automate (un nom inconnu est une erreur, pas << peut-etre de l'automate >>).
        const hmi::NameExists plc = environment().plcName ? hmi::NameExists(environment().plcName)
                                                          : hmi::NameExists([](std::string_view) { return false; });
        const auto issues = hmi::compileWith(pr, plc, hmi::exprcheck::PlcPaths{}, focus);
        std::set<hmi::Id> bad;
        for (const auto& is : issues) {
            std::string who;
            hmi::Id id = hmi::kNoId;
            for (const auto& s : newScripts)
                if (is.script == s.id) { who = "Script " + s.name; id = s.id; }
            for (const auto& f : newFunctions)
                if (is.item == f.id || is.script == f.id) { who = "Fonction " + f.name; id = f.id; }
            for (const auto& a : newAlarms)
                if (is.item == a.id) { who = "Alarme " + a.name; id = a.id; }
            if (who.empty()) continue;
            const std::string text = who + (is.line > 0 ? ", ligne " + std::to_string(is.line) : std::string{}) + " : " + is.message;
            if (is.severity == hmi::Issue::Severity::Error) {
                bad.insert(id);
                (void)fail(text);
            } else if (is.severity == hmi::Issue::Severity::Warning) {
                warn(text);
            }
        }
        if (!check && !bad.empty()) {   // l'apercu, appliquer malgre les erreurs : ce qui ne compile pas est laisse de cote
            std::erase_if(newScripts, [&](const hmi::Script& s) { return bad.count(s.id) > 0; });
            std::erase_if(newFunctions, [&](const hmi::HmiFunction& f) { return bad.count(f.id) > 0; });
            std::erase_if(newAlarms, [&](const hmi::AlarmDef& a) { return bad.count(a.id) > 0; });
        }
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
                // 1.18.0 : les outils de l'IA.
                case Operation::Action::Equipment: ok = equipment(op); break;
                case Operation::Action::Address: ok = address(op); break;
                case Operation::Action::Script: ok = script(op); break;
                case Operation::Action::Function: ok = function(op); break;
                case Operation::Action::Alarm: ok = alarm(op); break;
                case Operation::Action::Resource: ok = resource(op); break;
                case Operation::Action::Folder: ok = folder(op, false); break;
                case Operation::Action::Store: ok = folder(op, true); break;
            }
            if (!ok) return false;
        }
        compileCreated();
        if (!why.empty() && !check && !preview) return false;
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
    return std::any_of(p.ops.begin(), p.ops.end(), [](const Operation& op) { return projectLevel(op.action); });
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
        // 1.18.0 : les outils - l'element tel qu'il est dans le projet (absent, ou ce qui le decrit).
        const std::string name = trimmed(op.name);
        switch (op.action) {
            case Operation::Action::Equipment:
                out += "E:" + name + "=";
                if (const auto* e = project.equipmentByName(name)) out += e->host + ":" + std::to_string(e->port) + "/" + std::to_string(e->unit);
                else out += "-";
                out += ";";
                break;
            case Operation::Action::Address: {
                const std::string v = trimmed(op.variable);
                out += "A:" + v + "=";
                if (const auto* x = project.variable(v)) out += x->equipment + "@" + x->address + "#" + std::to_string(x->places.size());
                else out += "-";
                out += ";";
                break;
            }
            case Operation::Action::Script:
                out += "S:" + name + "=" + (project.generalScript(name) ? "1" : "-") + ";";
                break;
            case Operation::Action::Function:
                out += "F:" + name + "=" + (project.functionByName(name) ? "1" : "-") + ";";
                break;
            case Operation::Action::Alarm:
                out += "L:" + name + "=" + (project.alarmByName(name) ? "1" : "-") + ";";
                break;
            case Operation::Action::Resource:
                out += "R:" + name + "=" + (project.resourceByName(name) ? "1" : "-") + ";";
                break;
            case Operation::Action::Folder:
            case Operation::Action::Store:
                for (const auto& [k, v] : op.props)
                    if (k == "liste" || k == "dossier") out += "D:" + k + "=" + v + ";";
                for (const auto& n : op.objects) out += "D:" + n + ";";
                break;
            default:
                break;
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
        case Operation::Action::Equipment: return "creer_equipement";
        case Operation::Action::Address: return "adresser";
        case Operation::Action::Script: return "creer_script";
        case Operation::Action::Function: return "creer_fonction";
        case Operation::Action::Alarm: return "creer_alarme";
        case Operation::Action::Resource: return "creer_ressource";
        case Operation::Action::Folder: return "creer_dossier";
        case Operation::Action::Store: return "ranger";
    }
    return "creer";
}

std::vector<std::pair<std::string, std::string>> proposedDetections(const Proposal& p) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& op : p.ops) {
        if (op.action != Operation::Action::Equipment || !op.detect) continue;
        std::string host;
        for (const auto& [k, v] : op.props)
            if ((k == "adresse" || (k == "ip" && host.empty())) && !trimmed(v).empty()) host = trimmed(v);
        std::uint32_t ip = 0;
        std::string range;
        if (!host.empty() && hmi::equip::parseIpv4(host, ip) && ip != 0) range = hmi::equip::ipv4Text(hmi::equip::networkOf(ip, 24)) + "/24";
        out.emplace_back(trimmed(op.name), std::move(range));
    }
    return out;
}

bool projectLevel(Operation::Action a) noexcept {
    switch (a) {
        case Operation::Action::View:
        case Operation::Action::Variables:
        case Operation::Action::Type:
        case Operation::Action::Equipment:
        case Operation::Action::Address:
        case Operation::Action::Script:
        case Operation::Action::Function:
        case Operation::Action::Alarm:
        case Operation::Action::Resource:
        case Operation::Action::Folder:
        case Operation::Action::Store: return true;
        default: return false;
    }
}

std::optional<Operation::Action> actionFromKey(std::string_view k) noexcept {
    for (const auto a : {Operation::Action::Create, Operation::Action::Modify, Operation::Action::Bind, Operation::Action::Align,
                         Operation::Action::Distribute, Operation::Action::Remove, Operation::Action::Duplicate, Operation::Action::Code,
                         Operation::Action::View, Operation::Action::Variables, Operation::Action::Type, Operation::Action::Equipment,
                         Operation::Action::Address, Operation::Action::Script, Operation::Action::Function, Operation::Action::Alarm,
                         Operation::Action::Resource, Operation::Action::Folder, Operation::Action::Store})
        if (actionKey(a) == k) return a;
    // 1.18.0 : des noms de plus pour une vue d'un role (lus comme creer_vue + role).
    if (k == "creer_symbole" || k == "creer_popup" || k == "creer_modele_vue") return Operation::Action::View;
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
    return std::any_of(p.ops.begin(), p.ops.end(), [](const Operation& op) { return !projectLevel(op.action); });
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
    if (!view.empty()) {
        std::string role;
        for (const auto& [k, v] : p.ops.front().props)
            if (k == "role") role = lower(trimmed(v));
        static const std::map<std::string, std::string> noun = {{"popup", "la popup "}, {"symbole", "le symbole "}, {"modele", "l'\xC3\xA9" "cran mod\xC3\xA8le "},
                                                                {"entete", "l'en-t\xC3\xAAte "}, {"pied", "le pied de page "}};
        const auto it = noun.find(role);
        parts.push_back((it != noun.end() ? it->second : std::string("la vue ")) + view);
    }
    if (vars > 0) parts.push_back(std::to_string(vars) + (vars > 1 ? " variables IHM" : " variable IHM"));
    if (types > 0) parts.push_back(std::to_string(types) + (types > 1 ? " types IHM" : " type IHM"));
    // 1.18.0 : les outils.
    const auto count = [&](Operation::Action a) {
        return static_cast<std::size_t>(std::count_if(p.ops.begin(), p.ops.end(), [&](const Operation& op) { return op.action == a; }));
    };
    const auto add = [&](Operation::Action a, const char* one, const char* many) {
        if (const std::size_t n = count(a); n > 0) parts.push_back(std::to_string(n) + " " + (n > 1 ? many : one));
    };
    add(Operation::Action::Equipment, "\xC3\xA9quipement", "\xC3\xA9quipements");
    add(Operation::Action::Address, "adresse", "adresses");
    add(Operation::Action::Script, "script", "scripts");
    add(Operation::Action::Function, "fonction", "fonctions");
    add(Operation::Action::Alarm, "alarme", "alarmes");
    add(Operation::Action::Resource, "ressource", "ressources");
    add(Operation::Action::Folder, "dossier", "dossiers");
    add(Operation::Action::Store, "rangement", "rangements");
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
    // 1.18.0 : le role (symbole, popup, ecran modele, en-tete, pied de page) et sa taille par defaut.
    for (const auto& [k, val] : op.props)
        if (k == "role") {
            const std::string r = lower(trimmed(val));
            if (r == "popup" || r == "symbole" || r == "modele" || r == "entete" || r == "pied") v.role = r;
        }
    if (v.role == "symbole") { v.width = 200; v.height = 200; }
    if (v.role == "popup") { v.width = 600; v.height = 400; }
    if (v.role == "entete" || v.role == "pied") v.height = 100;
    for (const auto& [k, val] : op.props) {
        double d = 0;
        if (k == "largeur" && hmi::roles::readNumber(val, d) && d >= 50 && d <= 20000) v.width = static_cast<int>(d);
        if (k == "hauteur" && hmi::roles::readNumber(val, d) && d >= 50 && d <= 20000) v.height = static_cast<int>(d);
        if (k == "fond" && hmi::roles::colorOk(val)) v.background = val;
        if (k == "description") v.description = val;
        if (k == "titre") v.popup.title = trimmed(val);
        if (k == "modale") v.popup.modal = !(lower(trimmed(val)) == "false" || lower(trimmed(val)) == "0" || lower(trimmed(val)) == "non");
        if (k == "dossier") v.folder = trimmed(val);
    }
    if (v.role == "symbole" || v.role == "popup") {
        std::string text;
        for (const auto& g : op.generics) text += (text.empty() ? "" : ", ") + trimmed(g);
        if (!text.empty()) (void)hmi::parseGenerics(text, v.generics);
        for (const auto& prm : op.params) {
            hmi::ViewParam vp;
            vp.name = trimmed(prm.name);
            vp.type = hmi::types::normalized(trimmed(prm.type));
            vp.defaultValue = trimmed(prm.initial);
            vp.description = trimmed(prm.description);
            v.params.push_back(std::move(vp));
        }
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
            // 1.18.0 : les outils - equipements, adresses, scripts, fonctions, alarmes, ressources.
            for (auto& e : run.newEquipments) pr.equipments.push_back(std::move(e));
            for (const auto& av : run.addressed)
                if (hmi::Variable* x = pr.variableById(av.id)) *x = av;
            std::set<hmi::Id> codes;
            for (auto& sc : run.newScripts) {
                codes.insert(sc.id);
                pr.programs.scripts.push_back(std::move(sc));
            }
            for (auto& fn : run.newFunctions) {
                codes.insert(fn.id);
                pr.programs.functions.push_back(std::move(fn));
            }
            for (auto& al : run.newAlarms) pr.alarms.push_back(std::move(al));
            for (auto& rs : run.newResources) pr.assets.resources.push_back(std::move(rs));
            // Les blocs VAR d'un code cree deviennent ses declarations (comme a l'ouverture d'un projet d'avant).
            if (!codes.empty()) {
                const auto plan = hmi::migrate::plan(pr, [&](const hmi::migrate::Place& pl) { return codes.count(pl.id) > 0; });
                if (!plan.items.empty()) (void)hmi::migrate::apply(pr, plan);
            }
            if (createsView(p)) {
                // La vue de demarrage : une vue ordinaire, s'il n'y en a pas (comme "Nouvelle vue") - jamais un modele ni une popup.
                if (pr.config.startView == hmi::kNoId && work.role == "vue") pr.config.startView = work.id;
                pr.views.push_back(std::move(work));
            } else if (hmi::View* target = pr.view(viewId)) {
                *target = std::move(work);
            }
            run.applyFolders(pr);
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
            // 1.18.0 : les outils (leurs champs : dans proprietes).
            case Operation::Action::Equipment:
            case Operation::Action::Script:
            case Operation::Action::Function:
            case Operation::Action::Alarm:
            case Operation::Action::Resource:
                o.set("nom", op.name);
                break;
            case Operation::Action::Address:
                o.set("variable", op.variable);
                break;
            case Operation::Action::Folder:
                break;
            default: {
                json::Value list = json::Value::array();
                for (const auto& n : op.objects) list.push(n);
                o.set("objets", std::move(list));
                break;
            }
        }
        if (!op.generics.empty()) {
            json::Value list = json::Value::array();
            for (const auto& g : op.generics) list.push(g);
            o.set("generiques", std::move(list));
        }
        if (!op.params.empty()) {
            json::Value list = json::Value::array();
            for (const auto& v : op.params) {
                json::Value x = json::Value::object();
                x.set("nom", v.name);
                if (!v.type.empty()) x.set("type", v.type);
                if (!v.description.empty()) x.set("description", v.description);
                if (!v.initial.empty()) x.set("valeur", v.initial);
                list.push(std::move(x));
            }
            o.set("parametres", std::move(list));
        }
        if (op.detect) o.set("detecter", true);
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
        const bool named = *action == Operation::Action::Create || *action == Operation::Action::View || *action == Operation::Action::Type
                           || *action == Operation::Action::Equipment || *action == Operation::Action::Script || *action == Operation::Action::Function
                           || *action == Operation::Action::Alarm || *action == Operation::Action::Resource;
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
        // 1.18.0 : les generiques, les parametres (un symbole, une popup), la detection proposee, et les champs des outils.
        const auto scalar = [](const json::Value& v) {
            return v.isString() ? v.asString() : v.isNumber() ? hmi::formatNumber(v.asNumber()) : v.isBool() ? std::string(v.asBool() ? "true" : "false") : std::string{};
        };
        for (const auto& g : o.at("generiques").items())
            if (g.isString()) op.generics.push_back(g.asString());
        for (const auto& v : o.at("parametres").items()) op.params.push_back({v.str("nom"), v.str("type"), v.str("description"), scalar(v.at("valeur"))});
        op.detect = o.flag("detecter");
        if (*action == Operation::Action::Store)
            for (const auto& n : o.at("elements").items()) op.objects.push_back(n.asString());
        if (projectLevel(*action) && *action != Operation::Action::Variables && *action != Operation::Action::Type) {
            static const std::set<std::string, std::less<>> reserved = {"action", "nom", "objet", "objets", "elements", "variable", "proprietes", "variables",
                                                                        "membres", "parametres", "generiques", "detecter", "genre", "noms", "dx", "dy",
                                                                        "code", "copies", "reperes"};
            for (const auto& [k, v] : o.members())
                if (!reserved.count(k) && (v.isString() || v.isNumber() || v.isBool())) {
                    const bool already = std::any_of(op.props.begin(), op.props.end(), [&](const auto& kv) { return kv.first == k; });
                    if (!already) op.props.emplace_back(k, scalar(v));
                }
        }
        // creer_symbole, creer_popup, creer_modele_vue : une vue de ce role (sauf si proprietes le dit).
        if (*action == Operation::Action::View) {
            const std::string raw = o.str("action");
            const bool hasRole = std::any_of(op.props.begin(), op.props.end(), [](const auto& kv) { return kv.first == "role"; });
            if (!hasRole && raw == "creer_symbole") op.props.emplace_back("role", "symbole");
            if (!hasRole && raw == "creer_popup") op.props.emplace_back("role", "popup");
            if (!hasRole && raw == "creer_modele_vue") op.props.emplace_back("role", "modele");
        }
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
