// =============================================================================
//  app/ai/AiOperations.cpp - voir AiOperations.hpp
// =============================================================================
#include "AiOperations.hpp"
#include "AiDelete.hpp" // 1.21.0 : la suppression intelligente (genre, strategie, verification)
#include "AiPlan.hpp"   // 1.19.0 : l'ordre d'execution (les unites, leurs dependances)
#include "AiVarFolders.hpp"   // 1.22.1 : le dossier des variables creees (normalise, l'existant repris, deduit, melange)

#include "AiRegistry.hpp"
#include "../../hmi/HmiStyles.hpp"   // 1.23.0 : un style nomme pose par l'IA s'applique
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
#include "../../hmi/HmiMarkers.hpp"      // 1.22.2 : un repere ($Vanne$.Cmd) se lit sans ses $
#include "../../hmi/HmiPopupParams.hpp"  // 1.22.2 : le type d'un parametre de la vue et de ses membres
#include "../../hmi/HmiSymbols.hpp"      // 1.22.4 : la regle des cases (cellValue), un texte ou un nom
#include "../../hmi/HmiRuntime.hpp"      // 1.22.4 : isVariablePath (un nom seul)
#include "../../hmi/HmiMigrate.hpp"      // 1.18.0 : les blocs VAR d'un code cree deviennent ses declarations
// 1.22.6 : les options des variables, la securite, les vues - le code des ecrans (app::projedit), Renommer, la carte memoire.
#include "../RenameDialog.hpp"             // rewriteHmiReferences : renommer une variable et ses emplois
#include "../hmi/HmiProjectEdits.hpp"      // securite, liens d'une vue, valeur initiale, membres internes
#include "../hmi/HmiUnitPanes.hpp"         // unitui::problem : l'unite d'une variable
#include "../../hmi/HmiAudit.hpp"          // overlaps, nearestFreeAddress : le chevauchement d'une adresse
#include "../../hmi/HmiEnums.hpp"          // le type enumeration d'une variable
#include "../../hmi/HmiTemplates.hpp"      // headerOf, footerOf : les bandes d'une vue
#include "../../hmi/HmiUnits.hpp"          // typeTakesUnit
#include "../../sim/ValueText.hpp"         // readInteger, readDecimal : les nombres lus comme partout

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>

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

// 1.22.1 : LE TYPE CONSEILLE pour creer une variable introuvable (le clic droit << Creer la variable >>, AiFixes) - d'apres
// son nom (Cmd, Raz : BOOL ; Nb, Cpt : DINT ; Temperature, Niveau, Debit : REAL) et l'objet qui la lit (un champ de saisie :
// STRING ; une cuve, une courbe : REAL) ; sinon BOOL (une commande, un etat). Un conseil : l'utilisateur choisit le type.
std::string suggestedType(std::string_view name, hmi::Kind kind) {
    if (hmi::kindTakesText(kind)) return "STRING";
    std::string leaf(name);
    if (const auto dot = leaf.find_last_of('.'); dot != std::string::npos) leaf = leaf.substr(dot + 1);
    const std::string l = lower(leaf);
    const auto starts = [&](std::string_view w) { return l.rfind(w, 0) == 0; };
    for (const char* w : {"raz", "reset", "cmd", "etat", "def", "alarme", "marche", "arret"})
        if (starts(w)) return "BOOL";
    for (const char* w : {"nb", "cpt", "compt", "count", "nombre", "index", "idx"})
        if (starts(w)) return "DINT";
    for (const char* w : {"temp", "niveau", "level", "debit", "pression", "mesure", "consigne", "vitesse", "valeur", "pourcent", "ouverture",
                          "position", "puissance", "courant", "tension"})
        if (l.find(w) != std::string::npos) return "REAL";
    if (hmi::kindHasLevel(kind) || hmi::kindIsChart(kind)) return "REAL";
    return "BOOL";
}

// =============================================================================================
//  1.22.6 : LES OPTIONS DES VARIABLES, LA SECURITE, LES VUES - les petits outils communs
// =============================================================================================
// Les mots d'un booleen (oui, non, true, false, 1, 0, vrai, faux, coche, decoche) ; nullopt : illisible.
std::optional<bool> boolWord(std::string_view v) {
    const std::string l = lower(trimmed(v));
    if (l == "true" || l == "1" || l == "vrai" || l == "oui" || l == "yes" || l == "coche" || l == "cochee") return true;
    if (l == "false" || l == "0" || l == "faux" || l == "non" || l == "no" || l == "decoche" || l == "decochee") return false;
    return std::nullopt;
}

// Les vues les plus proches d'un nom (d'un role donne ; vide : toutes).
std::vector<std::string> nearViews(const hmi::Project& p, std::string_view name, std::string_view role = {}) {
    std::vector<std::pair<std::size_t, std::string>> scored;
    const std::string l = lower(name);
    for (const auto& v : p.views) {
        if (!role.empty() && v.role != role) continue;
        const std::string n = lower(v.name);
        const std::size_t d = distance(n, l);
        if (d <= std::max<std::size_t>(3, l.size() / 2) || (!l.empty() && (n.find(l) != std::string::npos || l.find(n) != std::string::npos)))
            scored.emplace_back(d, v.name);
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> out;
    for (const auto& [d, n] : scored)
        if (out.size() < 4) out.push_back(n);
    if (out.empty() && !role.empty())   // aucun proche : ceux de ce role (peu nombreux)
        for (const auto& v : p.views)
            if (v.role == role && out.size() < 4) out.push_back(v.name);
    return out;
}

// LES LIENS D'UNE VUE (creer_vue, modifier_vue) : parent, modele, entete, pied, entete_modele, pied_modele - poses sur `vv` par le
// code de l'inspecteur (app::projedit::setViewLink : pas de boucle, le bon role). entete / pied : oui, non, ou le NOM d'un
// modele d'en-tete / de pied (coche avec ce modele). Faux et `why` (avec les plus proches) ; `known` : la cle en est une.
bool viewLinkProp(const hmi::Project& p, hmi::View& vv, const std::string& key, const std::string& raw, std::string& why, bool* known = nullptr) {
    const std::string value = trimmed(raw);
    if (known) *known = true;
    const auto near = [&](std::string_view role) {
        const auto list = nearViews(p, value, role);
        if (!list.empty()) why += " ; les plus proches : " + joinList(list);
    };
    if (key == "parent" || key == "vue_parente") {
        const std::string l = lower(value);
        const bool none = value.empty() || l == "aucune" || l == "(aucune)" || l == "racine" || l == "non";
        if (!app::projedit::setViewLink(p, vv, "vue_parente", none ? std::string{} : value, why)) {
            if (!p.viewByName(value)) near({});
            return false;
        }
        return true;
    }
    if (key == "modele") {
        const std::string l = lower(value);
        const bool none = value.empty() || l == "aucun" || l == "(aucun)" || l == "non";
        if (!app::projedit::setViewLink(p, vv, "modele", none ? std::string{} : value, why)) {
            near("modele");
            return false;
        }
        return true;
    }
    if (key == "entete" || key == "pied") {
        if (vv.role == "entete" || vv.role == "pied") {
            why = vv.name + " est un mod\xC3\xA8le " + (vv.role == "entete" ? std::string("d'en-t\xC3\xAAte") : std::string("de pied de page"))
                  + " : il n'a ni en-t\xC3\xAAte ni pied";
            return false;
        }
        if (const auto b = boolWord(value)) return app::projedit::setViewLink(p, vv, key, *b ? "TRUE" : "FALSE", why);
        if (!app::projedit::setViewLink(p, vv, key + "_modele", value, why)) {
            near(key);
            return false;
        }
        return app::projedit::setViewLink(p, vv, key, "TRUE", why);
    }
    if (key == "entete_modele" || key == "pied_modele") {
        if (!app::projedit::setViewLink(p, vv, key, value, why)) {
            near(key == "entete_modele" ? "entete" : "pied");
            return false;
        }
        return true;
    }
    if (known) *known = false;
    return true;
}

// Une cle qui porterait un SECRET (un mot de passe, un badge, un secret de code dynamique) : jamais lue, jamais gardee. Pour
// << securite >>, << badge >> est le reglage << connexion par badge >> (oui, non) : permis.
bool secretKey(Operation::Action a, std::string_view key) {
    const std::string l = lower(key);
    for (const char* k : {"mot_de_passe", "motdepasse", "mot_passe", "mdp", "password", "passwd", "pass", "passe", "secret", "secret_totp", "totp",
                          "empreinte", "hash", "sel", "salt", "pin", "code_pin", "numero_badge", "badge_numero"})
        if (l == k) return true;
    return l == "badge" && a != Operation::Action::Security;
}
const char* const kNoSecret = "l'Assistant ne choisit ni ne transmet jamais de mot de passe, de badge ni de secret : l'utilisateur le donne "
                              "lui-m\xC3\xAAme dans Configuration > Utilisateurs (gard\xC3\xA9 en empreinte sal\xC3\xA9" "e, jamais en clair)";

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
    // 1.19.0 : les variables creees par les unites precedentes du plan (en minuscules) - deja posees dans le projet de
    // travail, mais toujours << creees par la proposition >> pour l'utilisateur.
    const std::set<std::string>* plannedVars{nullptr};
    // 1.22.1 : la proposition ENTIERE (le plan donne a cette Run une unite) - ses << ranger >> de variables (nul : `p`).
    const Proposal* whole{nullptr};
    // 1.22.1 : l'operation en cours (son indice dans `p.ops`) - les defauts structures (Check::faults) la notent ; les plus
    // proches d'une variable introuvable, cherchees une fois par nom (une grosse proposition la cite souvent).
    std::size_t current{0};
    std::map<std::string, std::vector<std::string>> nearCache{};
    // 1.22.4 : LES OBJETS EN CAUSE (Check::spots) - ceux que touche chaque operation (une fois par objet), ceux que vise
    // un message ; les noms des objets neufs renommes (l'avertissement les vise).
    std::set<std::pair<std::size_t, hmi::Id>> spotted{};
    std::vector<std::string>                  renamedTo{};
    // `overall` : un message de la proposition entiere (apres ses operations : les objets sans variable...).
    void spot(const hmi::Object& o, const std::string& message = {}, bool error = false, bool overall = false) {
        if (!check) return;
        const std::size_t at = overall ? Check::Fault::kNoOp : current;
        if (message.empty() && !spotted.emplace(at, o.id).second) return;
        check->spots.push_back({at, view.name, o.id, o.name, message, error});
    }
    void warnAt(const hmi::Object& o, std::string text) {
        spot(o, text);
        warn(std::move(text));
    }
    bool failAt(const hmi::Object& o, std::string text) {
        spot(o, text, true);
        return fail(std::move(text));
    }

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
    // 1.22.2 : UN PARAMETRE DE LA VUE (popup, symbole) et ses membres - << Vanne >>, << Vanne.Cmd >> dans Popup_Vanne, comme le
    // moteur les lit (params::expressionType : le type declare du parametre, puis ses membres). Ce n'est pas une variable du
    // projet : l'appelant la donne a l'ouverture (Vanne := Vannes[3]). L'IA les prenait pour des variables inventees (01/11).
    // Faux, et `why` : la racine est un parametre mais le chemin ne mene a rien (un membre inconnu de son type).
    [[nodiscard]] bool paramPath(std::string_view name, std::string* why = nullptr) const {
        const std::string t = trimmed(hmi::markers::strip(name));
        std::size_t r = 0;
        while (r < t.size() && (std::isalnum(static_cast<unsigned char>(t[r])) || t[r] == '_')) ++r;
        if (r == 0) return false;
        const hmi::ViewParam* prm = view.param(std::string_view(t).substr(0, r));
        if (!prm) return false;
        if (r == t.size()) return true;
        const std::string type = trimmed(prm->type);
        if (type.empty() || hmi::params::normalizedType(type) == "ANY" || hmi::params::isGenericName(view, type)) return true;
        if (!hmi::params::expressionType(project, &view, t).empty()) return true;
        if (why) {
            std::string members;
            if (const hmi::HmiType* ht = project.hmiTypeByName(type))
                for (const auto& m : ht->members) members += (members.empty() ? "" : ", ") + m.name;
            *why = "\xC2\xAB " + t + " \xC2\xBB : " + prm->name + " est un param\xC3\xA8tre de " + view.name + " (" + type + ")"
                   + (members.empty() ? std::string(", ce chemin n'y m\xC3\xA8ne \xC3\xA0 rien")
                                      : ", et " + type + " n'a pas ce membre (ses membres : " + members + ")")
                   + ".";
        }
        return false;
    }
    // Ce qu'un objet peut lire ou ecrire : une variable (du projet, de l'automate relie, creee plus haut) ou un parametre de la vue.
    [[nodiscard]] bool bindable(std::string_view name) const { return exists(name) || paramPath(name); }
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
        spot(o);   // 1.22.4 : l'operation en cours touche cet objet (l'apercu l'encadre quand on choisit son etape)
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
        if (!wanted.empty()) {
            renamed.push_back(wanted + kArrow + name);
            renamedTo.push_back(name);   // 1.22.4 : l'avertissement des noms pris vise cet objet
        }
        return name;
    }
    void boundsWarning(const hmi::Object& o) {
        const hmi::Box b = hmi::edit::rotatedBounds(o);
        if (b.x < 0 || b.y < 0 || b.right() > view.width || b.bottom() > view.height)
            warnAt(o, o.name + " d\xC3\xA9" "passe du cadre de la vue (" + std::to_string(view.width) + " \xC3\x97 " + std::to_string(view.height) + ").");
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
        if (!prop) return failAt(o, label(o) + " ne se relie pas \xC3\xA0 une variable.");
        std::string name = requested;
        // 1.22.2 : un parametre de la vue (Vanne.Cmd dans Popup_Vanne) : il existe ; un membre inconnu de son type : dit tel quel.
        std::string paramWhy;
        const bool param = !exists(name) && paramPath(name, &paramWhy);
        if (!paramWhy.empty()) return fail(paramWhy);
        const bool found = exists(name) || param;
        const VariableChoice* chosen = found ? nullptr : choiceFor(p, requested);   // 1.22.1 : le choix pour CETTE variable
        bool toCreate = false;
        if (chosen) {
            if (!chosen->create) {
                if (!exists(chosen->use))
                    return fail("La variable choisie \xC2\xAB " + chosen->use + " \xC2\xBB n'existe pas non plus.");
                name = chosen->use;
            } else {
                name = trimmed(chosen->use).empty() ? creatableName(requested) : trimmed(chosen->use);
                if (!hmi::isIdentifier(name)) return fail("\xC2\xAB " + name + " \xC2\xBB n'est pas un nom de variable valable.");
                toCreate = !exists(name);
                const std::string type = chosen->type.empty() ? std::string("BOOL") : chosen->type;
                if (toCreate)
                    warn("La variable " + name + " (" + type + ") sera cr\xC3\xA9\xC3\xA9" "e dans les variables de l'IHM, \xC3\xA0 ta demande.");
            }
        } else if (!found) {
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
                const std::string msg = "La variable \xC2\xAB " + name + " \xC2\xBB n'existe pas dans le projet : l'Assistant ne l'invente pas. "
                                        "Choisis une variable existante, ou demande sa cr\xC3\xA9" "ation.";
                if (check) {   // 1.22.1 : le defaut structure (le clic droit : utiliser une proche, creer, retirer)
                    Check::Fault f;
                    f.op = current;
                    f.kind = "variable";
                    f.name = name;
                    const auto [cached, fresh] = nearCache.try_emplace(name);
                    if (fresh) cached->second = nearVariables(project, name, 5);
                    f.closest = cached->second;
                    f.message = msg;
                    f.type = suggestedType(name, o.kind);
                    check->faults.push_back(std::move(f));
                }
                return failAt(o, msg);   // 1.22.4 : l'apercu encadre l'objet en rouge
            }
        }
        change(o, {"variable", prop->value, name});
        if (Item* it = itemFor(whatOf(o), o)) it->variable = name;
        if (check)
            check->bindings.push_back(o.name + kArrow + name
                                      + (chosen ? (!toCreate ? " (\xC3\xA0 la place de " + requested + ")"
                                             : name == requested ? std::string(" (\xC3\xA0 cr\xC3\xA9" "er)") : " (\xC3\xA0 cr\xC3\xA9" "er, pour " + requested + ")")
                                         : param ? std::string(" (param\xC3\xA8tre de " + view.name + ")")
                                         : found ? ((newVar(name) && !variableExists(project, name, environment())) || (plannedVars && plannedVars->count(lower(rootOf(name)))) ? std::string(" (cr\xC3\xA9\xC3\xA9" "e par la proposition)") : std::string{})
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
        if (!prop) return failAt(o, "\xC2\xAB " + k + " \xC2\xBB n'est pas une propri\xC3\xA9t\xC3\xA9 de " + label(o) + ".");
        const std::string e = trimmed(std::string_view(v).substr(1));
        const std::string at = o.name + "." + k + " = \xC2\xAB " + v + " \xC2\xBB : ";
        if (k == "variable")
            return failAt(o, at + "la variable s'\xC3\xA9" "crit par son nom, sans = (une formule va dans une autre propri\xC3\xA9t\xC3\xA9 : visible, fill...).");
        if (e.empty()) return failAt(o, at + "formule vide apr\xC3\xA8s le =.");
        std::string reason;
        if (!hmi::isReadOnly(e, &reason)) return failAt(o, at + "une formule ne fait que lire (" + reason + ").");
        if (const auto bad = formulaProblem(e); !bad.empty()) return failAt(o, at + bad + ".");
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
                if (!failAt(o, "\xC2\xAB " + k + " \xC2\xBB n'est pas une propri\xC3\xA9t\xC3\xA9 d'un objet " + std::string(hmi::kindLabel(*kind))
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
                if (!failAt(o, o.name + "." + k + " = \xC2\xAB " + v + " \xC2\xBB : " + bad + ".")) return false;
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
                if (!failAt(*o, "\xC2\xAB " + k + " \xC2\xBB n'est pas une propri\xC3\xA9t\xC3\xA9 de " + label(*o) + ".")) return false;
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
                if (!failAt(*o, o->name + "." + k + " = \xC2\xAB " + v + " \xC2\xBB : " + bad + ".")) return false;
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
                warnAt(*o, o->name + "." + k + " est pilot\xC3\xA9" "e par une formule (fx) : la valeur fixe ne se verra pas tant que la formule reste.");
            change(*o, {k, prop->value, v});
            prop->value = v;
        }
        if (geometry && !(box == before)) {
            if (view.effectivelyLocked(*o)) {
                if (!failAt(*o, o->name + " est verrouill\xC3\xA9 : d\xC3\xA9verrouille-le pour le d\xC3\xA9placer ou le redimensionner.")) return false;
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
                if (view.effectivelyLocked(*o)) warnAt(*o, o->name + " est verrouill\xC3\xA9 : il ne bouge pas.");
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

    // 1.21.0 : des objets (genre vide ou objet) ou des instances de symbole (genre instance : jamais leur definition, ni les
    // autres instances) ; la strategie seul (l'objet part, sa variable reste) ; les exclusions restent.
    bool remove(const Operation& op) {
        {
            del::Request rq;
            std::string bad;
            if (!del::requestOf(op, rq, &bad, view.name)) return fail(bad);
        }
        const bool instances = del::genreFromKey(op.kind) == del::Genre::Instance;
        std::vector<hmi::Id> ids;
        for (const auto& n : op.objects) {
            if (std::any_of(op.keep.begin(), op.keep.end(), [&](const std::string& k) { return iequal(trimmed(k), trimmed(n)); })) {
                const std::string text = "\xC2\xAB " + n + " \xC2\xBB est exclu de la suppression : il reste.";
                if (const hmi::Object* kept = find(n)) spot(*kept, text);   // 1.22.4
                warn(text);
                continue;
            }
            hmi::Object* o = find(n);
            if (!o) {
                warn("\xC2\xAB " + n + " \xC2\xBB n'est plus dans la vue (rien \xC3\xA0 supprimer).");
                continue;
            }
            if (instances && o->kind != hmi::Kind::SymbolInstance) {
                if (!fail("\xC2\xAB " + o->name + " \xC2\xBB n'est pas une instance de symbole (c'est " + std::string(hmi::kindLabel(o->kind))
                          + ") : genre objet.")) return false;
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
                    if (!pr.value.empty() && !bindable(pr.value))
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
                    if (target.empty() || !bindable(target))
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
            hmi::Object* o = view.object(id);
            if (!o) continue;
            createdIds.insert(id);
            if (k < objs.size()) {
                aliases[objs[k].name] = id;
                if (objs[k].name != o->name) renamed.push_back(objs[k].name + kArrow + o->name);
                // 1.22.8 : verrou=1 dans le code reste verrouille (Ctrl+V deverrouille) - le calque de reference d'un modele de
                // vues : une image ni selectionnable ni deplacable a la souris, masquee en marche (visible FALSE).
                if (objs[k].locked) o->locked = true;
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
            } else if (k == "parent" || k == "modele" || k == "entete" || k == "pied" || k == "entete_modele" || k == "pied_modele") {
                // 1.22.6 : ses liens, controles comme dans l'inspecteur (newViewFor les a poses ; ici, la raison d'un refus).
                hmi::View probe;
                probe.id = view.id;
                probe.name = op.name;
                probe.role = lower(prop(op, "role").empty() ? std::string("vue") : prop(op, "role"));
                std::string why2;
                if (k == "modele" && (probe.role == "entete" || probe.role == "pied")) why2 = "un mod\xC3\xA8le d'en-t\xC3\xAAte ou de pied n'h\xC3\xA9rite pas d'un \xC3\xA9" "cran mod\xC3\xA8le";
                else (void)viewLinkProp(project, probe, k, v, why2);
                if (!why2.empty())
                    if (!fail("Vue " + op.name + " : " + why2 + ".")) return false;
            } else if (k == "decaler" || k == "agrandir") {
                if (!boolWord(v))
                    if (!fail("Vue " + op.name + " : " + k + " : oui ou non.")) return false;
            } else if (k != "description" && k != "titre" && k != "modale" && k != "dossier") {
                if (!fail("Vue " + op.name + " : \xC2\xAB " + k + " \xC2\xBB n'est pas un r\xC3\xA9glage d'une vue (largeur, hauteur, fond, description, role, "
                          "titre, modale, dossier, parent, modele, entete, pied).")) return false;
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
            // 1.22.4 : la valeur par defaut d'un parametre texte - 'Vanne' est un texte, Vanne un NOM (un autre
            // parametre, une variable). Un nom qui n'est pas un texte donne a un STRING : l'ambiguite, refusee.
            if (bad.empty()) bad = textDefaultProblem(op, pt, trimmed(prm.initial));
            if (!bad.empty()) {
                if (!fail("Vue " + op.name + ", param\xC3\xA8tre \xC2\xAB " + prm.name + " \xC2\xBB : " + bad + ".")) return false;
                continue;
            }
            if (hmi::isTextType(pt) && hmi::isVariablePath(trimmed(prm.initial)) && !knownForDefault(op, trimmed(prm.initial)))
                warn("Vue " + op.name + ", param\xC3\xA8tre " + pn + " : la valeur par d\xC3\xA9" "faut " + trimmed(prm.initial)
                     + " est prise comme le texte '" + trimmed(prm.initial) + "' (un texte s'\xC3\xA9" "crit entre apostrophes).");
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

    // 1.22.4 : un nom connu pour une valeur par defaut - un parametre de la meme vue, ou un nom du projet.
    bool knownForDefault(const Operation& op, const std::string& value) const {
        const std::string root = value.substr(0, value.find_first_of(".["));
        if (std::any_of(op.params.begin(), op.params.end(), [&](const auto& q) { return iequal(trimmed(q.name), root); })) return true;
        return hmi::isKnownName(&project, root);
    }
    // 1.22.4 : la valeur par defaut `g` d'un parametre de type `pt` - vide : elle convient. Pour un texte, un nom
    // sans apostrophes dont le type n'est pas un texte (Vanne : T_Vanne) est l'ambiguite de ta capture.
    std::string textDefaultProblem(const Operation& op, const std::string& pt, const std::string& g) const {
        if (g.empty() || !hmi::isTextType(pt) || !hmi::isVariablePath(g) || !knownForDefault(op, g)) return {};
        const std::string root = g.substr(0, g.find_first_of(".["));
        std::string given;
        for (const auto& q : op.params)
            if (iequal(trimmed(q.name), root)) given = g == root ? hmi::types::normalized(trimmed(q.type)) : std::string{};
        if (given.empty()) given = hmi::params::expressionType(project, nullptr, g);
        if (given.empty() || hmi::isTextType(given)) return {};
        return "la valeur par d\xC3\xA9" "faut " + g + " sans apostrophes est un NOM (" + root + ", " + given + "), pas un texte : un texte "
               "s'\xC3\xA9" "crit '" + g + "' ; une valeur calcul\xC3\xA9" "e, une formule ('Vanne ' + Numero.ToString())";
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
    // 1.22.1 : + LE DOSSIER de chaque variable (AiVarFolders.hpp) - le sien, sinon celui de l'operation ; un chemin invalide
    // est refuse ; sans dossier, deduit du prefixe (et dit) ; un melange d'equipements est signale.
    bool variables(const Operation& op, std::size_t index) {
        if (op.variables.empty()) return fail("creer_variables ne porte aucune variable (\xC2\xAB variables \xC2\xBB est vide).");
        if (op.variables.size() > 500) return fail("creer_variables : 500 variables au plus en une proposition.");
        std::string opFolder, folderWhy;
        if (!varfold::normalize(prop(op, "dossier"), opFolder, &folderWhy)) return fail("creer_variables : " + folderWhy + ".");
        const std::size_t firstNew = newVars.size(), firstShown = check ? check->newVariables.size() : 0;
        const std::map<std::string, std::string> stored = storedVariables();
        std::vector<varfold::Request> asks;
        for (const auto& spec : op.variables) {
            const std::string name = trimmed(spec.name);
            const std::string at = "Variable \xC2\xAB " + spec.name + " \xC2\xBB : ";
            std::string bad;
            if (!hmi::isIdentifier(name)) bad = "nom invalide (une lettre ou _, puis des lettres sans accent, des chiffres ou _)";
            else if (hmi::isReservedWord(name) || iequal(name, "SYS") || iequal(name, "API")) bad = "\xC2\xAB " + name + " \xC2\xBB est un mot r\xC3\xA9serv\xC3\xA9";
            else if (const auto* v = project.variable(name)) {
                // 1.19.0 : la meme variable (meme type) : reutilisee telle quelle ; un autre type : refusee, jamais remplacee.
                const std::string wanted = hmi::types::normalized(trimmed(spec.type));
                if (!wanted.empty() && iequal(wanted, v->type)) {
                    reuse("Variable", v->name, v->type + " (existe d\xC3\xA9j\xC3\xA0, gard\xC3\xA9" "e telle quelle)");
                    continue;
                }
                bad = "une variable IHM porte d\xC3\xA9j\xC3\xA0 ce nom avec le type " + v->type + " (demand\xC3\xA9 : " + (wanted.empty() ? std::string("?") : wanted)
                      + ") : elle n'est jamais remplac\xC3\xA9" "e en silence";
            }
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
            std::string folder;   // 1.22.1 : son dossier, normalise (vide : celui de l'operation)
            if (bad.empty() && !varfold::normalize(spec.folder, folder, &folderWhy)) bad = folderWhy;
            // 1.22.8 : son unite - connue (fournie, du projet), ou creee plus haut (creer_unite : le plan la pose avant).
            std::string unitSymbol;
            if (bad.empty() && !trimmed(spec.unit).empty())
                if (const std::string u = unitui::problem(project, v.type, spec.unit, &unitSymbol); !u.empty()) {
                    bad = "unit\xC3\xA9 : " + u;
                    unknownUnit(spec.unit, v.type, at + bad + ".");
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
            v.unit = unitSymbol;   // 1.22.8
            v.description = trimmed(spec.description).empty() ? "Cr\xC3\xA9\xC3\xA9" "e par l'Assistant IA (demande : " + p.request + ")"
                                                              : trimmed(spec.description);
            if (check) {
                check->newVariables.push_back({v.name, v.type, v.description, v.initial});
                check->newVariables.back().unit = v.unit;   // 1.22.8
            }
            varfold::Request rq;
            rq.name = v.name;
            rq.folder = folder.empty() ? opFolder : folder;
            if (const auto it = stored.find(lower(v.name)); it != stored.end()) {
                rq.stored = true;
                rq.folder = it->second;
            }
            asks.push_back(std::move(rq));
            newVars.push_back(std::move(v));
        }
        placeFolders(firstNew, firstShown, asks);
        return true;
    }

    // 1.22.1 : les variables qu'un << ranger >> (liste variables) de la proposition range - leur nom (en minuscules) -> le
    // dossier de ce ranger (normalise) : elles ne sont ni rangees ni deduites ici.
    [[nodiscard]] std::map<std::string, std::string> storedVariables() const {
        std::map<std::string, std::string> out;
        const Proposal& all = whole ? *whole : p;
        for (std::size_t i = 0; i < all.ops.size(); ++i) {
            const Operation& o = all.ops[i];
            if (o.action != Operation::Action::Store || std::find(all.excluded.begin(), all.excluded.end(), i) != all.excluded.end()) continue;
            const std::string list = lower(prop(o, "liste"));
            if (list != "variables" && list != "variable") continue;
            std::string path;
            if (!varfold::normalize(prop(o, "dossier"), path)) path.clear();
            for (const auto& n : o.objects) out[lower(trimmed(n))] = path;
        }
        return out;
    }

    // 1.22.1 : LE RANGEMENT des variables creees par une operation (AiVarFolders : place) - le dossier de chacune (dans
    // newVars et dans l'apercu), les dossiers a creer (une ligne << Dossier >> ; poses par applyFolders), ce qui est deduit
    // ou melange (des avertissements). Une variable rangee par un << ranger >> garde son dossier vide ici : le ranger le pose.
    void placeFolders(std::size_t firstNew, std::size_t firstShown, const std::vector<varfold::Request>& asks) {
        if (asks.empty()) return;
        const varfold::Placement pl = varfold::place(project, asks);
        for (std::size_t i = 0; i < asks.size() && i < pl.placed.size(); ++i) {
            const varfold::Placed& at = pl.placed[i];
            if (firstNew + i < newVars.size() && at.how != varfold::Placed::How::Stored) newVars[firstNew + i].folder = at.folder;
            if (check && firstShown + i < check->newVariables.size()) check->newVariables[firstShown + i].folder = at.folder;
        }
        for (const auto& w : pl.warnings) warn(w);
        for (const auto& c : pl.created) {
            made("Dossier", c, "dossier cr\xC3\xA9\xC3\xA9 : " + c + " (variables IHM)");
            FolderOp f;
            f.variables = true;
            f.path = c;
            folderOps.push_back(std::move(f));
        }
    }

    // CREER_TYPE : un type IHM (une structure), comme Types IHM > Nouveau : un nom libre, des membres (un nom
    // unique, un type connu ou cree PLUS HAUT dans la proposition - l'ordre des dependances).
    bool makeType(const Operation& op, std::size_t index) {
        const std::string name = trimmed(op.name);
        std::string bad;
        if (!hmi::isIdentifier(name)) bad = "nom invalide (une lettre ou _, puis des lettres sans accent, des chiffres ou _)";
        else if (hmi::types::isElementary(name)) bad = name + " est un type \xC3\xA9l\xC3\xA9mentaire";
        else if (const auto* t = project.hmiTypeByName(name)) {
            // 1.19.0 : le meme type (memes membres, memes types, memes generiques) : reutilise ; sinon refuse.
            bool same = t->members.size() == op.members.size() && t->generics.size() == op.generics.size();
            for (const auto& m : op.members) {
                const auto it = std::find_if(t->members.begin(), t->members.end(), [&](const hmi::TypeMember& x) { return iequal(x.name, trimmed(m.name)); });
                if (it == t->members.end() || !iequal(it->type, hmi::types::normalized(trimmed(m.type)))) same = false;
            }
            if (same) {
                reuse("Type", t->name, std::to_string(t->members.size()) + " membres (existe d\xC3\xA9j\xC3\xA0, gard\xC3\xA9 tel quel)");
                return true;
            }
            std::string has;
            for (std::size_t k = 0; k < t->members.size() && k < 6; ++k) has += (k ? ", " : "") + t->members[k].name + " : " + t->members[k].type;
            bad = "le type IHM " + t->name + " existe d\xC3\xA9j\xC3\xA0 avec d'autres membres (" + has + ") : il n'est jamais remplac\xC3\xA9 en silence";
        }
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
    // 1.20.0 : beaucoup d'adresses a la suite sans geler l'appli - partage par toutes les unites d'une proposition
    // (runStaged : une Run par unite) : la prochaine adresse libre de chaque equipement (lue une fois, avancee a chaque
    // variable placee) et les adresses deja prises (indexees une fois, tenues a jour).
    struct FreeState {
        hmi::equip::FreeCursor cursor;
        std::size_t            vars{0}, types{0}, equipments{0};
        bool                   valid{false};
    };
    struct Shared {
        std::map<std::string, FreeState>                                     cursors;
        std::map<std::string, std::vector<std::pair<hmi::Id, std::string>>> taken;   // EQUIPEMENT|adresse -> variables
        bool                                                                 takenBuilt{false};
    };
    Shared                        ownShared{};
    Shared*                       shared{nullptr};   // runStaged : celui de la proposition ; nul : celui de cette Run
    std::shared_ptr<hmi::Project> typesProbe{};      // construit a la demande (un projet coute)
    std::size_t                   typesProbeN{static_cast<std::size_t>(-1)};
    struct FolderOp {
        bool                     variables{false};
        hmi::fold::List          list{hmi::fold::List::Views};
        std::string              path;
        std::vector<std::string> names;          // vide : creer le dossier seulement
    };
    std::vector<FolderOp>         folderOps{};
    // 1.19.0 : LE PLAN - ce qui existe deja a l'identique et sert tel quel (jamais compte comme une creation) ; par
    // operation, ce qu'elle a dit (les erreurs, avertissements, lignes << aussi cree >> et << reutilise >> qu'elle a
    // ajoutes : leurs bornes dans le Check) ; la compilation des codes crees, remise a la fin du plan (deux fonctions
    // qui s'appellent : chacune est compilee une fois l'autre creee).
    std::vector<Check::Made> reusedItems{};
    struct Span {
        std::size_t op{0}, e0{0}, e1{0}, w0{0}, w1{0}, m0{0}, m1{0}, r0{0}, r1{0};
    };
    std::vector<Span> spans{};
    bool              deferCompile{false};
    void reuse(std::string what, std::string name, std::string detail) {
        Check::Made m{std::move(what), std::move(name), std::move(detail)};
        if (check) check->reused.push_back(m);
        reusedItems.push_back(std::move(m));
    }

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
        // 1.19.0 : le meme equipement (memes reglages) : reutilise ; d'autres reglages : refuse, jamais remplace.
        if (const hmi::Equipment* same = equipmentNamed(name)) {
            if (same->type == e.type && iequal(same->host, e.host) && same->port == e.port && same->unit == e.unit && same->simulated == e.simulated) {
                reuse("\xC3\x89quipement", same->name, "existe d\xC3\xA9j\xC3\xA0 avec ces r\xC3\xA9glages, gard\xC3\xA9 tel quel");
                return true;
            }
            return fail(at + "un \xC3\xA9quipement porte d\xC3\xA9j\xC3\xA0 ce nom avec d'autres r\xC3\xA9glages (" + (same->host.empty() ? std::string("simul\xC3\xA9") : same->host)
                        + ":" + std::to_string(same->port) + ", esclave " + std::to_string(same->unit) + ") : il n'est jamais remplac\xC3\xA9 en silence.");
        }
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

    // 1.20.0 : le projet des types (ceux du projet et ceux crees plus haut) - sans copier tout le projet.
    const hmi::Project& typesProject() {
        if (newTypes.empty()) return project;
        if (!typesProbe || typesProbeN != newTypes.size()) {
            typesProbe = std::make_shared<hmi::Project>();
            typesProbe->programs.types = project.programs.types;
            typesProbe->programs.types.insert(typesProbe->programs.types.end(), newTypes.begin(), newTypes.end());
            typesProbeN = newTypes.size();
        }
        return *typesProbe;
    }
    Shared& sh() { return shared ? *shared : ownShared; }
    // La premiere adresse libre de `eq` (comme hmi::equip::nextFreeAddress sur le projet de travail), sans le relire a
    // chaque variable : le curseur est relu quand une variable, un type ou un equipement s'ajoute.
    std::string nextFree(const hmi::Equipment& eq, sim::Type type) {
        auto& st = sh().cursors[upperOf(eq.name)];
        const std::size_t vars = project.programs.variables.size() + newVars.size(), types = project.programs.types.size() + newTypes.size(),
                          equipments = project.equipments.size() + newEquipments.size();
        if (!st.valid || st.vars != vars || st.types != types || st.equipments != equipments) {
            st.cursor = hmi::equip::freeCursor(probe(), eq);
            st.vars = vars;
            st.types = types;
            st.equipments = equipments;
            st.valid = true;
        }
        return hmi::equip::freeAddress(st.cursor, type);
    }
    // Les variables du projet qui ont deja cette adresse sur cet equipement (l'index fait une fois).
    const std::vector<std::pair<hmi::Id, std::string>>& takenBy(const std::string& equipment, const std::string& canonical) {
        auto& s = sh();
        if (!s.takenBuilt) {
            for (const auto& o : project.programs.variables)
                if (!o.address.empty() && !o.equipment.empty())
                    s.taken[upperOf(o.equipment) + "|" + hmi::equip::canonicalAddress(o.address)].emplace_back(o.id, o.name);
            s.takenBuilt = true;
        }
        static const std::vector<std::pair<hmi::Id, std::string>> none;
        const auto it = s.taken.find(upperOf(equipment) + "|" + canonical);
        return it == s.taken.end() ? none : it->second;
    }
    // Une variable placee : l'index des adresses prises et le curseur de son equipement suivent ; une variable qui avait
    // deja une place (elle la quitte) : les curseurs seront relus (rare).
    void notePlaced(const hmi::Variable& before, const hmi::Variable& after) {
        auto& s = sh();
        if (s.takenBuilt) {
            if (!before.equipment.empty() && !before.address.empty()) {
                auto& list = s.taken[upperOf(before.equipment) + "|" + hmi::equip::canonicalAddress(before.address)];
                std::erase_if(list, [&](const auto& x) { return x.first == before.id; });
            }
            if (!after.equipment.empty() && !after.address.empty())
                s.taken[upperOf(after.equipment) + "|" + hmi::equip::canonicalAddress(after.address)].emplace_back(after.id, after.name);
        }
        if (!before.equipment.empty() && (!before.address.empty() || !before.places.empty())) {
            for (auto& [k, st] : s.cursors) st.valid = false;
            return;
        }
        if (const auto it = s.cursors.find(upperOf(after.equipment)); it != s.cursors.end() && it->second.valid)
            hmi::equip::advance(it->second.cursor, typesProject(), after);
    }

    // 1.20.0 : ADRESSER UN MEMBRE. La variable (la racine du chemin) doit exister ; le chemin doit etre un membre de son
    // type (lu dans le type deplie : structures imbriquees, tableaux) - jamais invente. La variable suit un seul
    // equipement : liee a un autre, c'est refuse ; pas encore liee, elle l'est (dit). L'adresse du membre va dans
    // Variable::places (remplace celle du meme chemin).
    bool addressMember(const Operation& op, const std::string& path, std::size_t cut) {
        const std::string rootName = path.substr(0, cut);
        std::string rel = path.substr(cut);
        if (!rel.empty() && rel.front() == '.') rel.erase(0, 1);
        const std::string eqName = prop(op, "equipement");
        const std::string addr = prop(op, "adresse");
        const std::string at = "Adresser \xC2\xAB " + path + " \xC2\xBB : ";
        if (!prop(op, "formule").empty()) return fail(at + "une formule d'adresse se donne sur la variable enti\xC3\xA8re (" + rootName + "), pas sur un membre.");
        hmi::Variable* nv = newVarMut(rootName);
        const hmi::Variable* pv = nv ? nullptr : project.variable(rootName);
        if (!nv && !pv) {
            if (environment().plcName && environment().plcName(rootName))
                return fail(at + "c'est une variable de l'automate reli\xC3\xA9 : seule une variable IHM se lie \xC3\xA0 un \xC3\xA9quipement.");
            std::string msg = at + "variable IHM \xC2\xAB " + rootName + " \xC2\xBB introuvable (l'Assistant n'invente pas de variable)";
            if (const auto near = nearVariables(project, rootName); !near.empty()) msg += " ; les plus proches : " + joinList(near);
            return fail(msg + ".");
        }
        hmi::Variable v = nv ? *nv : *pv;
        for (const auto& a : addressed)
            if (!nv && a.id == v.id) v = a;
        const hmi::Variable before = v;
        // le membre existe-t-il ? (le type deplie : une feuille de ce nom, ou un membre compose qui en a dessous)
        const hmi::Project& types = typesProject();
        std::string error;
        const auto leaves = hmi::types::leafVariables(types, v, nullptr, nullptr, &error);
        const std::string full = upperOf(v.name + "." + rel), fullIdx = upperOf(v.name + rel);
        bool found = false;
        // pour l'aide : les membres du niveau ou le chemin s'arrete (Config.Sous.Inconnu : ceux de Config.Sous), sinon du premier
        const auto lastSep = rel.find_last_of(".[");
        const std::string parent = upperOf(v.name + (lastSep == std::string::npos ? std::string{} : (rel.front() == '[' ? "" : ".") + rel.substr(0, lastSep)));
        std::vector<std::string> firstLevel, level;
        auto headAfter = [](const std::string& name, std::size_t from) {
            std::string tail = name.size() > from ? name.substr(from) : std::string{};
            if (!tail.empty() && tail.front() == '.') tail.erase(0, 1);
            const auto stop = tail.find_first_of(".[", tail.empty() || tail.front() != '[' ? 0 : 1);
            return stop == std::string::npos ? tail : tail.substr(0, stop);
        };
        auto keep = [](std::vector<std::string>& list, const std::string& head) {
            if (!head.empty() && std::find(list.begin(), list.end(), head) == list.end() && list.size() < 12) list.push_back(head);
        };
        for (const auto& l : leaves) {
            const std::string n = upperOf(l.name);
            for (const auto& want : {full, fullIdx})
                if (n == want || (n.size() > want.size() && n.compare(0, want.size(), want) == 0 && (n[want.size()] == '.' || n[want.size()] == '[')))
                    found = true;
            keep(firstLevel, headAfter(l.name, v.name.size()));
            if (n.size() > parent.size() && n.compare(0, parent.size(), parent) == 0 && (n[parent.size()] == '.' || n[parent.size()] == '['))
                keep(level, headAfter(l.name, parent.size()));
        }
        std::string owner = v.name;
        if (!level.empty() && lastSep != std::string::npos) {
            firstLevel = std::move(level);
            owner += (rel.front() == '[' ? "" : ".") + rel.substr(0, lastSep);
        }
        if (!found)
            return fail(at + "\xC2\xAB " + rel + " \xC2\xBB n'est pas un membre de " + v.name + " (" + v.type + ")"
                        + (!error.empty() ? " - le type ne se d\xC3\xA9plie pas : " + error
                                          : firstLevel.empty() ? std::string{} : " ; les membres de " + owner + " : " + joinList(firstLevel))
                        + " (l'Assistant n'invente pas de membre).");
        if (hmi::types::isInternalMember(v, rel))
            return fail(at + "ce membre est interne (il reste dans l'IHM, l'\xC3\xA9quipement ne le lit pas) : retire-le des membres internes d'abord.");
        const hmi::Equipment* eq = equipmentNamed(eqName.empty() ? v.equipment : eqName);
        if (!eq) return fail(at + "\xC3\xA9quipement \xC2\xAB " + (eqName.empty() ? v.equipment : eqName) + " \xC2\xBB introuvable.");
        if (!eq->modbus()) return fail(at + eq->name + " est un \xC3\xA9quipement Ethernet TCP/IP : il n'a pas d'adresses (Modbus TCP/IP seulement).");
        if (!v.equipment.empty() && !iequal(v.equipment, eq->name))
            return fail(at + v.name + " est li\xC3\xA9" "e \xC3\xA0 " + v.equipment + " : tous ses membres suivent cet \xC3\xA9quipement (pas " + eq->name + ").");
        if (v.equipment.empty()) {
            v.equipment = eq->name;
            warn("Adresser " + path + " : " + v.name + " n'\xC3\xA9tait li\xC3\xA9" "e \xC3\xA0 aucun \xC3\xA9quipement - elle l'est \xC3\xA0 " + eq->name
                 + (v.address.empty() ? std::string(" (sans adresse de d\xC3\xA9part : seuls les membres adress\xC3\xA9s sont lus)") : std::string{}) + ".");
        }
        if (addr.empty()) {
            if (v.address.empty()) return fail(at + "donne l'adresse du membre (" + v.name + " n'a pas d'adresse de d\xC3\xA9part d'o\xC3\xB9 la calculer).");
            made("Adresse", path, eq->name + " : suit l'adresse de " + v.name + " (" + v.address + ")");
        } else {
            std::string bad;
            if (hmi::equip::canonicalAddress(addr, &bad).empty())
                return fail(at + "adresse \xC2\xAB " + addr + " \xC2\xBB illisible" + (bad.empty() ? std::string{} : " (" + bad + ")")
                            + " (ex. %MW100, %MF20, %M5, %MW10.3, 40101).");
            bool replaced = false;
            for (auto& pl : v.places)
                if (iequal(pl.path, rel)) {
                    pl.address = addr;
                    replaced = true;
                }
            if (!replaced) v.places.push_back({rel, addr});
            made("Adresse", path, eq->name + " " + addr);
        }
        notePlaced(before, v);
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

    // ADRESSER : une variable IHM (du projet ou creee plus haut) liee a un equipement Modbus, a son adresse ; une formule de
    // lien pour les cases d'un tableau de structures (hmi::addr, << Modifier l'adresse dans toutes les cases >>).
    bool address(const Operation& op) {
        const std::string varName = trimmed(op.variable);
        const std::string eqName = prop(op, "equipement");
        const std::string addr = prop(op, "adresse");
        const std::string formula = prop(op, "formule");
        const std::string at = "Adresser \xC2\xAB " + varName + " \xC2\xBB : ";
        // 1.20.0 : UN MEMBRE d'une structure, a toute profondeur ("Config.ConfPoids", "Config.Sous.Valeur", "Tab[2].Pos") :
        // son adresse va dans les adresses corrigees de sa variable (Variable::places), comme l'ecran des variables.
        if (const auto cut = varName.find_first_of(".["); cut != std::string::npos && cut > 0) return addressMember(op, varName, cut);
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
        const hmi::Variable before = v;
        v.equipment = eq->name;
        if (!addr.empty()) {
            std::string why;
            if (hmi::equip::canonicalAddress(addr, &why).empty())
                return fail(at + "adresse \xC2\xAB " + addr + " \xC2\xBB illisible" + (why.empty() ? std::string{} : " (" + why + ")")
                            + " (ex. %MW100, %MF20, %M5, %MW10.3, 40101).");
            v.address = addr;
        } else if (formula.empty()) {
            v.address = nextFree(*eq, hmi::equip::typeOfName(v.type));
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
            for (const auto& [id, name] : takenBy(eq->name, mine))
                if (id != v.id) warn("Adresser " + v.name + " : " + v.address + " est d\xC3\xA9j\xC3\xA0 l'adresse de " + name + " sur " + eq->name + ".");
        }
        notePlaced(before, v);
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
        a.incidents = readBool(prop(op, "incidents"), true);   // 1.20.2 : la case Incidents (cochee par defaut)
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
        // 1.22.1 : un dossier de variables (AiVarFolders) - le chemin remis en forme, un dossier existant repris tel qu'il
        // s'ecrit (sans la casse ni les blancs en trop) : jamais un double.
        if (f.variables) {
            std::string norm, why2;
            if (!varfold::normalize(path, norm, &why2)) return fail(at + why2 + ".");
            std::vector<std::string> known = hmi::types::allFolders(project);
            for (const auto& x : folderOps)
                if (x.variables) known.push_back(x.path);
            f.path = varfold::resolve(norm, known);
        }
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
            made("Rangement", f.path, std::to_string(f.names.size()) + (f.names.size() > 1 ? " \xC3\xA9l\xC3\xA9ments : " : " \xC3\xA9l\xC3\xA9ment : ") + joinList(f.names));
        } else {
            bool exists = false;
            if (f.variables) {
                for (const auto& d : hmi::types::allFolders(project)) exists = exists || varfold::key(d) == varfold::key(f.path);
            } else {
                for (const auto& d : hmi::fold::allFolders(project, f.list)) exists = exists || iequal(d, path);
            }
            if (exists) {
                warn("Le dossier \xC2\xAB " + f.path + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0 : rien \xC3\xA0 cr\xC3\xA9" "er.");
                return true;
            }
            made("Dossier", f.path, "dans " + (f.variables ? std::string("variables") : std::string(hmi::fold::key(f.list))));
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

    // =========================================================================================
    //  1.22.6 : LES OPTIONS DES VARIABLES, LA SECURITE, LES VUES. Ces operations sont seules dans leur unite (une operation
    //  du projet) : elles se posent DIRECTEMENT sur le projet de travail (`edit`, celui de runStaged : la copie de la
    //  validation, de l'apercu, ou le projet de la commande) - les unites qui suivent les voient. Refusees : rien n'a change.
    // =========================================================================================
    hmi::Project* edit{nullptr};

    // Ce que l'operation dit d'une modification : "lecture seule d\xC3\xA9" "coch\xC3\xA9" "e".
    static std::string checkedText(const char* what, bool on) {
        return std::string(what) + (on ? " coch\xC3\xA9" "e" : " d\xC3\xA9" "coch\xC3\xA9" "e");
    }
    bool noEdit(const Operation& op) {
        return fail(std::string(actionKey(op.action)) + " : seulement dans une proposition enti\xC3\xA8re (le projet de travail manque).");
    }

    // MODIFIER_VARIABLE : une ou plusieurs variables IHM existantes (variable, objets), les memes options.
    bool editVariables(const Operation& op) {
        if (!edit) return noEdit(op);
        std::vector<std::string> names;
        if (!trimmed(op.variable).empty()) names.push_back(trimmed(op.variable));
        for (const auto& n : op.objects)
            if (!trimmed(n).empty()) names.push_back(trimmed(n));
        if (names.empty()) return fail("modifier_variable : quelle variable ? (\xC2\xAB variable \xC2\xBB : le nom d'une variable IHM existante).");
        if (op.props.empty()) return fail("modifier_variable " + names.front() + " : rien \xC3\xA0 modifier (les options vont dans \xC2\xAB proprietes \xC2\xBB).");
        if (names.size() > 1 && !prop(op, "renommer").empty()) return fail("modifier_variable : renommer, une variable \xC3\xA0 la fois.");
        for (const auto& n : names)
            if (!editVariable(op, n)) return false;
        return true;
    }

    bool editVariable(const Operation& op, const std::string& name) {
        namespace ty = hmi::types;
        namespace eq = hmi::equip;
        const std::string at = "Variable \xC2\xAB " + name + " \xC2\xBB : ";
        hmi::Variable* cur = nullptr;
        for (auto& v : edit->programs.variables)
            if (v.name == name) cur = &v;
        if (!cur)
            for (auto& v : edit->programs.variables)
                if (!cur && iequal(v.name, name)) cur = &v;
        if (!cur) {
            if (environment().plcName && environment().plcName(name))
                return fail(at + "c'est une variable de l'automate reli\xC3\xA9 : l'Assistant ne modifie pas l'automate (seulement les variables IHM).");
            if (const std::string root = rootOf(name); !iequal(root, name) && edit->variable(root))
                return fail(at + "une option se r\xC3\xA8gle sur la variable enti\xC3\xA8re (" + root + "), pas sur un membre.");
            std::string msg = at + "variable IHM introuvable (l'Assistant n'invente pas de variable)";
            if (const auto near = nearVariables(*edit, name); !near.empty()) msg += " ; les plus proches : " + joinList(near);
            return fail(msg + ".");
        }
        static const std::vector<std::string> keys = {"type", "equipement", "adresse", "lecture_seule", "remanente", "unite", "valeur",
                                                      "valeur_initiale", "description", "dossier", "brut_min", "brut_max", "echelle_min",
                                                      "echelle_max", "type_brut", "bools_par_mot", "internes", "attribues", "compacter", "renommer"};
        for (const auto& [k, v] : op.props) {
            if (k == "secret_refuse") return fail(at + "\xC2\xAB " + v + " \xC2\xBB refus\xC3\xA9 : " + kNoSecret + ".");
            if (std::find(keys.begin(), keys.end(), k) == keys.end())
                return fail(at + "\xC2\xAB " + k + " \xC2\xBB n'est pas une option d'une variable (" + joinList(keys) + ").");
        }
        const auto has = [&](const char* k) { return std::any_of(op.props.begin(), op.props.end(), [&](const auto& kv) { return kv.first == k; }); };
        const auto boolOf = [&](const char* k, bool& out) {
            const auto b = boolWord(prop(op, k));
            if (!b) return fail(at + k + " : oui ou non (pas \xC2\xAB " + prop(op, k) + " \xC2\xBB).");
            out = *b;
            return true;
        };
        const hmi::Variable before = *cur;
        hmi::Variable next = *cur;
        std::vector<std::string> said;
        std::string error;
        // ---- le type (comme la colonne Type : une enumeration, un type objet, une structure)
        if (has("type")) {
            const std::string raw = prop(op, "type");
            const std::string type = ty::normalized(raw);
            if (!ty::validType(*edit, type, &error)) return fail(at + unknownType(raw, error) + ".");
            if (!iequal(type, next.type)) {
                const std::string was = next.type;
                if (const auto* e = hmi::findEnumeration(*edit, type)) {
                    next.type = e->name;
                    next.initial = e->values.empty() ? std::string{} : e->values.front().name;
                    if (next.scaled()) next.rawMin = next.rawMax = next.engMin = next.engMax = 0;
                    next.unit.clear();
                } else if (ty::isRich(type)) {
                    next.type = type;
                    if (!app::projedit::validInitial(*edit, type, next.initial, nullptr)) next.initial.clear();
                    if (next.bound()) said.push_back("d\xC3\xA9li\xC3\xA9" "e de " + next.equipment + " (" + ty::unbindableReason(type) + ")");
                    next.equipment.clear();
                    next.address.clear();
                    next.readOnly = false;
                    next.rawMin = next.rawMax = next.engMin = next.engMax = 0;
                    next.rawType.clear();
                    next.places.clear();
                    next.internal.clear();
                    next.compact = false;
                    next.retain = false;
                    next.unit.clear();
                } else {
                    if (ty::isComposite(type))
                        if (const auto f = ty::flatten(*edit, next.name, type, {}, next.packBools); !f.ok()) return fail(at + "type " + type + " : " + f.error + ".");
                    const bool wasComposite = ty::isComposite(next.type);
                    next.type = type;
                    if (!app::projedit::validInitial(*edit, type, next.initial, nullptr) || ty::isComposite(type) != wasComposite)
                        next.initial = ty::isComposite(type) ? std::string{} : (type == "BOOL" ? "FALSE" : type == "STRING" ? "''" : "0");
                    if (ty::isComposite(type) && next.scaled()) next.rawMin = next.rawMax = next.engMin = next.engMax = 0;
                    if (!next.unit.empty() && !hmi::units::typeTakesUnit(type)) next.unit.clear();
                }
                said.push_back("type " + was + kArrow + next.type);
                if (!before.unit.empty() && next.unit.empty()) said.push_back("unit\xC3\xA9 " + before.unit + " retir\xC3\xA9" "e");
            }
        }
        // ---- l'equipement (Lier, Delier)
        if (has("equipement")) {
            const std::string e = prop(op, "equipement");
            const std::string le = lower(e);
            if (e.empty() || le == "aucun" || le == "(aucun)" || le == "locale" || le == "(locale)" || le == "non") {
                if (next.bound()) {
                    said.push_back("d\xC3\xA9li\xC3\xA9" "e de " + next.equipment + " (elle reste dans l'IHM)");
                    next.equipment.clear();
                    next.address.clear();
                    next.readOnly = false;
                    next.rawMin = next.rawMax = next.engMin = next.engMax = 0;
                    next.rawType.clear();
                    next.places.clear();
                }
            } else {
                if (const std::string reason = ty::unbindableReason(*edit, next.type); !reason.empty()) return fail(at + reason + ".");
                const hmi::Equipment* eqp = edit->equipmentByName(e);
                if (!eqp) {
                    std::vector<std::string> all;
                    for (const auto& x : edit->equipments) all.push_back(x.name);
                    return fail(at + "\xC3\xA9quipement \xC2\xAB " + e + " \xC2\xBB introuvable" + (all.empty() ? std::string(" (le projet n'en a aucun)") : " (les \xC3\xA9quipements : " + joinList(all) + ")") + ".");
                }
                if (!eqp->modbus()) return fail(at + eqp->name + " est un \xC3\xA9quipement Ethernet TCP/IP : il n'a pas de variables.");
                if (!iequal(next.equipment, eqp->name)) {
                    if (!has("adresse") && (next.address.empty() || !before.bound())) {
                        ty::Spec sp;
                        const bool bits = ty::parseSpec(next.type, sp) && sp.element == "BOOL" && !ty::isComposite(next.type);
                        next.address = eq::nextFreeAddress(*edit, *eqp, bits ? sim::Type::Bool : ty::isComposite(next.type) ? sim::Type::Int : eq::typeOfName(next.type));
                    }
                    next.equipment = eqp->name;
                    said.push_back("li\xC3\xA9" "e \xC3\xA0 " + eqp->name + (next.address.empty() ? std::string{} : " (" + next.address + ")"));
                    if (next.retain) {
                        next.retain = false;
                        said.push_back("r\xC3\xA9manente retir\xC3\xA9" "e (l'\xC3\xA9quipement garde sa valeur)");
                    }
                }
            }
        }
        // ---- l'adresse (comme la colonne Adresse : lisible, de la bonne zone ; le chevauchement plus bas)
        if (has("adresse")) {
            const std::string a = prop(op, "adresse");
            if (const std::string reason = ty::unbindableReason(*edit, next.type); !reason.empty()) return fail(at + reason + ".");
            if (!next.bound()) return fail(at + next.name + " n'est li\xC3\xA9" "e \xC3\xA0 aucun \xC3\xA9quipement : donne aussi \xC2\xAB equipement \xC2\xBB.");
            if (a.empty()) return fail(at + "adresse vide (pour la d\xC3\xA9lier : equipement = aucun).");
            if (ty::isComposite(next.type)) {
                if (ty::memberAddress(a, 0, -1, "INT", &error).empty()) {
                    if (!ty::bitArea(a)) return fail(at + "adresse \xC2\xAB " + a + " \xC2\xBB : " + error + ".");
                    hmi::Variable probeVar = next;
                    probeVar.address = a;
                    for (const auto& l : ty::leafVariables(*edit, probeVar))
                        if (l.type != "BOOL") return fail(at + "le d\xC3\xA9part " + a + " est un bit : " + l.name + " (" + l.type + ") n'y tient pas.");
                }
            } else {
                hmi::comm::Point pt;
                hmi::Variable probeVar = next;
                probeVar.address = a;
                if (!eq::placeEquipmentAddress(a, eq::registerType(probeVar), pt, &error))
                    return fail(at + "adresse \xC2\xAB " + a + " \xC2\xBB : " + (error.empty() ? std::string("illisible") : error) + ".");
            }
            if (next.address != a) {
                said.push_back("adresse " + (next.address.empty() ? std::string("(aucune)") : next.address) + kArrow + a);
                next.address = a;
            }
        }
        // ---- les cases a cocher
        if (has("lecture_seule")) {
            bool on = false;
            if (!boolOf("lecture_seule", on)) return false;
            if (on != next.readOnly) said.push_back(checkedText("lecture seule", on));
            next.readOnly = on;
        }
        if (has("remanente")) {
            bool on = false;
            if (!boolOf("remanente", on)) return false;
            if (on && ty::isRich(next.type))
                return fail(at + next.name + " est un " + ty::normalized(next.type) + " : un objet de l'IHM, que R\xC3\xA9manente ne garde pas (des cases simples seulement).");
            if (on && next.bound())
                return fail(at + next.name + " est li\xC3\xA9" "e \xC3\xA0 " + next.equipment + " : sa valeur vient de l'\xC3\xA9quipement, qui la garde (R\xC3\xA9manente vaut pour une variable de l'IHM).");
            if (on != next.retain) said.push_back(checkedText("r\xC3\xA9manente", on));
            next.retain = on;
        }
        if (has("bools_par_mot")) {
            bool on = false;
            if (!boolOf("bools_par_mot", on)) return false;
            if (on != next.packBools) said.push_back(on ? std::string("BOOL rang\xC3\xA9s 16 par mot") : std::string("BOOL un mot chacun"));
            next.packBools = on;
        }
        if (has("compacter")) {
            bool on = false;
            if (!boolOf("compacter", on)) return false;
            if (on && (!next.bound() || !ty::isComposite(next.type)))
                return fail(at + "recalculer la place : une structure ou un tableau li\xC3\xA9 \xC3\xA0 un \xC3\xA9quipement seulement.");
            if (on != next.compact) said.push_back(on ? std::string("place m\xC3\xA9moire recalcul\xC3\xA9" "e") : std::string("place d'origine"));
            next.compact = on;
        }
        // ---- l'unite (la colonne Unite : un symbole connu, un type numerique)
        if (has("unite")) {
            std::string unit;
            if (const std::string bad = unitui::problem(*edit, next.type, prop(op, "unite"), &unit); !bad.empty()) {
                unknownUnit(prop(op, "unite"), next.type, at + "unit\xC3\xA9 : " + bad + ".");   // 1.22.8 : le clic droit la cree
                return fail(at + "unit\xC3\xA9 : " + bad + ".");
            }
            if (unit != next.unit) said.push_back(unit.empty() ? "sans unit\xC3\xA9" : "unit\xC3\xA9 " + (next.unit.empty() ? std::string("(aucune)") : next.unit) + kArrow + unit);
            next.unit = unit;
        }
        // ---- la valeur initiale (lue comme toute case : hmi::typedInitial ; controlee comme la colonne)
        if (has("valeur") || has("valeur_initiale")) {
            const std::string raw = has("valeur") ? prop(op, "valeur") : prop(op, "valeur_initiale");
            std::string initial = raw;
            if (!hmi::typedInitial(*edit, next.type, initial, &error) || !app::projedit::validInitial(*edit, next.type, initial, &error))
                return fail(at + "valeur initiale \xC2\xAB " + raw + " \xC2\xBB : " + error + ".");
            if (const auto* e = hmi::findEnumeration(*edit, next.type); e && !initial.empty()) {
                std::int64_t n = 0;
                if (hmi::enumNumberOf(*e, initial, n))
                    if (const auto* x = hmi::enumValueByNumber(*e, n)) initial = x->name;
            }
            if (initial != next.initial) said.push_back("valeur initiale " + (next.initial.empty() ? std::string("(vide)") : next.initial) + kArrow + initial);
            next.initial = initial;
        }
        if (has("description") && prop(op, "description") != trimmed(next.description)) {
            next.description = prop(op, "description");
            said.push_back("description : " + next.description);
        }
        // ---- le dossier (comme creer_variables : normalise, l'existant repris sans la casse, le manquant cree)
        std::string newFolder;
        if (has("dossier")) {
            const std::string raw = prop(op, "dossier");
            const std::string l = lower(raw);
            std::string folder;
            if (!(raw.empty() || raw == "/" || l == "racine" || l == "(racine)")) {
                if (!varfold::normalize(raw, folder, &error)) return fail(at + "dossier : " + error + ".");
                const auto known = ty::allFolders(*edit);
                folder = varfold::resolve(folder, known);
                if (std::none_of(known.begin(), known.end(), [&](const std::string& f) { return varfold::key(f) == varfold::key(folder); })) newFolder = folder;
            }
            if (folder != next.folder) said.push_back(folder.empty() ? std::string("rang\xC3\xA9" "e \xC3\xA0 la racine") : "rang\xC3\xA9" "e dans " + folder);
            next.folder = folder;
        }
        // ---- la mise a l'echelle (Communication : brut -> echelle)
        for (const char* k : {"brut_min", "brut_max", "echelle_min", "echelle_max"}) {
            if (!has(k)) continue;
            double d = 0;
            if (!sim::readDecimal(prop(op, k), d) || !std::isfinite(d)) return fail(at + k + " : un nombre (pas \xC2\xAB " + prop(op, k) + " \xC2\xBB).");
            if (ty::isComposite(next.type)) return fail(at + "une structure ou un tableau ne se met pas \xC3\xA0 l'\xC3\xA9" "chelle (ses cases gardent leur valeur).");
            double& target = std::string_view(k) == "brut_min" ? next.rawMin : std::string_view(k) == "brut_max" ? next.rawMax
                           : std::string_view(k) == "echelle_min" ? next.engMin : next.engMax;
            target = d;
        }
        if (has("type_brut")) {
            const std::string t = upperOf(prop(op, "type_brut"));
            if (!t.empty() && t != "INT" && t != "UINT" && t != "WORD" && t != "DINT" && t != "UDINT" && t != "DWORD" && t != "REAL")
                return fail(at + "type du registre : INT, UINT, WORD, DINT, UDINT, DWORD, REAL.");
            next.rawType = t;
        }
        if (next.rawMin != before.rawMin || next.rawMax != before.rawMax || next.engMin != before.engMin || next.engMax != before.engMax
            || next.rawType != before.rawType) {
            said.push_back(next.scaled() ? "\xC3\xA9" "chelle brut " + hmi::formatNumber(next.rawMin) + ".." + hmi::formatNumber(next.rawMax) + " = "
                                               + hmi::formatNumber(next.engMin) + ".." + hmi::formatNumber(next.engMax)
                                         : std::string("sans mise \xC3\xA0 l'\xC3\xA9" "chelle"));
            if (next.scaled() && !next.bound()) warn(at + "la mise \xC3\xA0 l'\xC3\xA9" "chelle ne joue que pour une variable li\xC3\xA9" "e \xC3\xA0 un \xC3\xA9quipement.");
        }
        // ---- les membres internes (Rendre interne / Attribuer a l'equipement)
        for (const char* k : {"internes", "attribues"}) {
            if (!has(k)) continue;
            std::vector<std::string> paths;
            for (const auto& part : hmi::roles::splitList(prop(op, k), ';'))
                for (auto& item : hmi::roles::splitList(part, ','))
                    if (!trimmed(item).empty()) paths.push_back(trimmed(item));
            std::vector<std::string> internal;
            if (!app::projedit::membersInternal(*edit, next, paths, std::string_view(k) == "internes", false, internal, &error))
                return fail(at + k + " : " + error + ".");
            if (internal != next.internal)
                said.push_back(std::string(std::string_view(k) == "internes" ? "interne" : "attribu\xC3\xA9 \xC3\xA0 " + next.equipment) + " : " + joinList(paths));
            next.internal = internal;
        }
        // ---- le chevauchement : une place deja prise par une autre variable de l'equipement est REFUSEE (l'Assistant choisit
        // une place libre ; l'ecran, lui, laisse faire et le montre dans la carte memoire).
        if (next.bound() && (next.equipment != before.equipment || next.address != before.address || next.type != before.type
                             || next.packBools != before.packBools || next.internal != before.internal || next.compact != before.compact)) {
            const hmi::Variable saved = *cur;
            *cur = next;
            std::string clash, nearest;
            for (const auto& o : hmi::audit::overlaps(*edit, next.equipment)) {
                const bool mineA = iequal(o.a.variable, next.name), mineB = iequal(o.b.variable, next.name);
                if (mineA == mineB) continue;   // deux cases de la meme variable, ou deux autres variables
                clash = (mineA ? o.b.variable : o.a.variable) + " (" + o.text + ")";
                break;
            }
            if (!clash.empty()) nearest = hmi::audit::nearestFreeAddress(*edit, next.name);
            *cur = saved;
            if (!clash.empty())
                return fail(at + "l'adresse " + next.address + " sur " + next.equipment + " chevauche " + clash + " : choisis une place libre"
                            + (nearest.empty() ? std::string{} : " (la plus proche : " + nearest + ")") + ".");
        }
        // ---- renommer (avec tous ses emplois, comme Renommer : un seul Ctrl+Z avec le reste)
        std::string newName;
        if (has("renommer")) {
            const std::string to = prop(op, "renommer");
            if (const std::string bad = identifierProblem(to); !bad.empty()) return fail(at + "renommer en \xC2\xAB " + to + " \xC2\xBB : " + bad + ".");
            if (to != next.name) {
                for (const auto& v : edit->programs.variables)
                    if (&v != cur && iequal(v.name, to)) return fail(at + "renommer : une variable IHM s'appelle d\xC3\xA9j\xC3\xA0 " + v.name + ".");
                if (std::any_of(edit->programs.functions.begin(), edit->programs.functions.end(), [&](const hmi::HmiFunction& f) { return iequal(f.name, to); }))
                    return fail(at + "renommer : une fonction de l'IHM s'appelle " + to + ".");
                if (environment().plcName && environment().plcName(to))
                    return fail(at + "renommer : " + to + " est une variable de l'automate reli\xC3\xA9 (l'IHM lirait la sienne \xC3\xA0 la place).");
                newName = to;
            }
        }
        if (said.empty() && newName.empty()) {
            reuse("Variable", next.name, "d\xC3\xA9j\xC3\xA0 ainsi : rien ne change");
            return true;
        }
        if (newName.empty()) {
            *cur = next;
            if (!newFolder.empty() && std::none_of(edit->programs.folders.begin(), edit->programs.folders.end(), [&](const std::string& f) { return iequal(f, newFolder); }))
                edit->programs.folders.push_back(newFolder);
        } else {
            hmi::Project trial = *edit;
            if (hmi::Variable* x = trial.variableById(next.id)) *x = next;
            if (!newFolder.empty()) trial.programs.folders.push_back(newFolder);
            const auto conflicts = rewriteHmiReferences(trial, false, next.name, newName);
            if (!conflicts.empty()) return fail(at + "renommer en " + newName + " : " + conflicts.front() + " (rien n'est renomm\xC3\xA9).");
            std::size_t places = 0;
            for (std::size_t i = 0; i < trial.views.size() && i < edit->views.size(); ++i) places += !(trial.views[i] == edit->views[i]);
            for (std::size_t i = 0; i < trial.programs.scripts.size() && i < edit->programs.scripts.size(); ++i)
                places += !(trial.programs.scripts[i] == edit->programs.scripts[i]);
            for (std::size_t i = 0; i < trial.programs.functions.size() && i < edit->programs.functions.size(); ++i)
                places += !(trial.programs.functions[i] == edit->programs.functions[i]);
            for (std::size_t i = 0; i < trial.alarms.size() && i < edit->alarms.size(); ++i) places += !(trial.alarms[i] == edit->alarms[i]);
            said.push_back("renomm\xC3\xA9" "e en " + newName + (places == 0 ? std::string(" (rien ne la citait)")
                                                                  : " (" + std::to_string(places) + (places > 1 ? " vues, scripts ou alarmes la citaient : ils suivent)"
                                                                                                                : " vue, script ou alarme la citait : il suit)")));
            *edit = std::move(trial);
        }
        if (!newFolder.empty()) made("Dossier", newFolder, "dossier cr\xC3\xA9\xC3\xA9 : " + newFolder + " (variables IHM)");
        made("Variable", name, joinList(said, " ; "));
        return true;
    }

    // ---- LA SECURITE ------------------------------------------------------------------------------------------------
    static std::string securityLabel(const std::string& field) {
        static const std::map<std::string, std::string> labels = {
            {"active", "s\xC3\xA9" "curit\xC3\xA9"}, {"depart", "utilisateur au d\xC3\xA9marrage"}, {"deconnexion", "d\xC3\xA9" "connexion automatique (min)"},
            {"menu_comptes", "niveau de l'onglet Comptes"}, {"menu_acces", "niveau de l'onglet Acc\xC3\xA8s"}, {"menu_journal", "niveau de l'onglet Journal"},
            {"periode", "p\xC3\xA9riode du code (s)"}, {"chiffres", "chiffres du code"}, {"mdp_longueur", "longueur minimale du mot de passe"},
            {"mdp_chiffre", "un chiffre"}, {"mdp_lettre", "une lettre"}, {"mdp_casse", "majuscules et minuscules"},
            {"mdp_special", "un caract\xC3\xA8re sp\xC3\xA9" "cial"}, {"mdp_duree", "expiration (jours)"}, {"mdp_historique", "derniers mots de passe interdits"},
            {"mdp_premiere", "changer \xC3\xA0 la premi\xC3\xA8re connexion"}, {"verrou_essais", "verrouillage apr\xC3\xA8s (\xC3\xA9" "checs)"},
            {"verrou_minutes", "dur\xC3\xA9" "e du verrouillage (min)"}, {"avertir", "avertissement avant d\xC3\xA9" "connexion (s)"}, {"badge", "connexion par badge"}};
        const auto it = labels.find(field);
        return it == labels.end() ? field : it->second;
    }
    static std::string securityValue(const hmi::Security& s, const std::string& f) {
        const auto yn = [](bool b) { return std::string(b ? "oui" : "non"); };
        if (f == "active") return s.enabled ? "active" : "inactive";
        if (f == "depart") return s.startUser.empty() ? std::string("personne") : s.startUser;
        if (f == "deconnexion") return std::to_string(s.autoLogoutMin);
        if (f == "menu_comptes") return std::to_string(s.menuLevelAccounts);
        if (f == "menu_acces") return std::to_string(s.menuLevelAccess);
        if (f == "menu_journal") return std::to_string(s.menuLevelJournal);
        if (f == "periode") return std::to_string(s.dynamicPeriodS);
        if (f == "chiffres") return std::to_string(s.dynamicDigits);
        if (f == "mdp_longueur") return std::to_string(s.pwMinLength);
        if (f == "mdp_chiffre") return yn(s.pwDigit);
        if (f == "mdp_lettre") return yn(s.pwLetter);
        if (f == "mdp_casse") return yn(s.pwMixedCase);
        if (f == "mdp_special") return yn(s.pwSpecial);
        if (f == "mdp_duree") return std::to_string(s.pwMaxAgeDays);
        if (f == "mdp_historique") return std::to_string(s.pwHistory);
        if (f == "mdp_premiere") return yn(s.pwChangeFirst);
        if (f == "verrou_essais") return std::to_string(s.lockAttempts);
        if (f == "verrou_minutes") return std::to_string(s.lockMinutes);
        if (f == "avertir") return std::to_string(s.logoutWarnS);
        if (f == "badge") return yn(s.badgeLogin);
        return {};
    }
    // SECURITE : ses reglages, par le code de Configuration > Utilisateurs (app::projedit::setSecurityField).
    bool securityOp(const Operation& op) {
        if (!edit) return noEdit(op);
        if (op.props.empty()) return fail("securite : rien \xC3\xA0 r\xC3\xA9gler (active, depart, deconnexion, menu_comptes... dans \xC2\xAB proprietes \xC2\xBB).");
        const hmi::Security before = edit->security;
        const auto refuse = [&](std::string text) {
            edit->security = before;
            return fail(std::move(text));
        };
        const auto& fields = app::projedit::securityFields();
        std::vector<std::string> said;
        for (const auto& [k, raw] : op.props) {
            if (k == "secret_refuse") return refuse("S\xC3\xA9" "curit\xC3\xA9 : \xC2\xAB " + raw + " \xC2\xBB refus\xC3\xA9 : " + kNoSecret + ".");
            if (std::find(fields.begin(), fields.end(), k) == fields.end())
                return refuse("S\xC3\xA9" "curit\xC3\xA9 : \xC2\xAB " + k + " \xC2\xBB n'est pas un r\xC3\xA9glage (" + joinList(fields) + ").");
            std::string value = trimmed(raw);
            if (k == "active" || k == "badge" || k.rfind("mdp_", 0) == 0) {
                if (k == "active" || k == "badge" || k == "mdp_chiffre" || k == "mdp_lettre" || k == "mdp_casse" || k == "mdp_special" || k == "mdp_premiere") {
                    const auto b = boolWord(value);
                    if (!b) return refuse("S\xC3\xA9" "curit\xC3\xA9, " + k + " : oui ou non (pas \xC2\xAB " + value + " \xC2\xBB).");
                    value = *b ? "TRUE" : "FALSE";
                }
            }
            const std::string was = securityValue(edit->security, k);
            std::string err;
            if (!app::projedit::setSecurityField(*edit, k, value, err)) return refuse("S\xC3\xA9" "curit\xC3\xA9 : " + err + ".");
            const std::string now = securityValue(edit->security, k);
            if (was == now) continue;
            if (k == "active") said.push_back(edit->security.enabled ? "activ\xC3\xA9" "e (niveaux et permissions appliqu\xC3\xA9s en marche)"
                                                                     : "D\xC3\x89" "SACTIV\xC3\x89" "E (tout est permis en marche)");
            else said.push_back(securityLabel(k) + " : " + was + kArrow + now);
        }
        if (said.empty()) reuse("S\xC3\xA9" "curit\xC3\xA9", "r\xC3\xA9glages", "d\xC3\xA9j\xC3\xA0 ainsi : rien ne change");
        else made("S\xC3\xA9" "curit\xC3\xA9", "Configuration > Utilisateurs", joinList(said, " ; "));
        return true;
    }

    // CREER_GROUPE, MODIFIER_GROUPE : par le code de la fiche d'un groupe (app::projedit::addGroup, setGroupField).
    bool groupOp(const Operation& op, bool create) {
        if (!edit) return noEdit(op);
        const std::string name = create ? trimmed(op.name) : (prop(op, "groupe").empty() ? trimmed(op.name) : prop(op, "groupe"));
        const std::string at = "Groupe \xC2\xAB " + name + " \xC2\xBB : ";
        std::vector<std::string> groups;
        for (const auto& g : edit->security.groups) groups.push_back(g.name + " (" + std::to_string(g.level) + ")");
        if (name.empty()) return fail(std::string(actionKey(op.action)) + " : le nom du groupe manque.");
        static const std::vector<std::string> keys = {"groupe", "niveau", "roles", "description", "vue_demarrage", "renommer"};
        for (const auto& [k, v] : op.props) {
            if (k == "secret_refuse") return fail(at + "\xC2\xAB " + v + " \xC2\xBB refus\xC3\xA9 : " + kNoSecret + ".");
            if (std::find(keys.begin(), keys.end(), k) == keys.end() || (create && (k == "renommer" || k == "groupe")))
                return fail(at + "\xC2\xAB " + k + " \xC2\xBB n'est pas un champ d'un groupe (niveau, roles, description, vue_demarrage"
                            + (create ? std::string{} : std::string(", renommer")) + ").");
        }
        const hmi::Security before = edit->security;
        const auto refuse = [&](std::string text) {
            edit->security = before;
            return fail(std::move(text));
        };
        std::string err;
        hmi::Id id = hmi::kNoId;
        long long level = 1;
        if (!readInt(prop(op, "niveau"), 1, 9, 1, level)) return fail(at + "niveau : de 1 \xC3\xA0 9 (pas \xC2\xAB " + prop(op, "niveau") + " \xC2\xBB).");
        if (create) {
            if (const hmi::UserGroup* g = edit->groupByName(name)) {
                if (g->level == level && (prop(op, "roles").empty() || [&] {
                        std::vector<std::string> wanted;
                        for (const auto& part : hmi::roles::splitList(prop(op, "roles"), ';'))
                            for (auto& r : hmi::roles::splitList(part, ','))
                                if (!trimmed(r).empty()) wanted.push_back(lower(trimmed(r)));
                        std::vector<std::string> have;
                        for (const auto& r : g->roles) have.push_back(lower(r));
                        std::sort(wanted.begin(), wanted.end());
                        std::sort(have.begin(), have.end());
                        return wanted == have;
                    }())) {
                    reuse("Groupe", g->name, "existe d\xC3\xA9j\xC3\xA0 avec ce niveau et ces r\xC3\xB4les, gard\xC3\xA9 tel quel");
                    return true;
                }
                return fail(at + "il existe d\xC3\xA9j\xC3\xA0 (niveau " + std::to_string(g->level) + ") : modifier_groupe pour le changer.");
            }
            id = app::projedit::addGroup(*edit, name, static_cast<int>(level), err);
            if (id == hmi::kNoId) return refuse(at + err + ".");
        } else {
            const hmi::UserGroup* g = edit->groupByName(name);
            if (!g) return fail(at + "introuvable (l'Assistant n'invente pas de groupe) ; les groupes : " + joinList(groups) + ".");
            id = g->id;
            if (!prop(op, "niveau").empty() && !app::projedit::setGroupField(*edit, id, "niveau", std::to_string(level), err)) return refuse(at + err + ".");
        }
        const hmi::UserGroup old = [&] {
            for (const auto& g : before.groups)
                if (g.id == id) return g;
            return hmi::UserGroup{};
        }();
        for (const char* k : {"roles", "description", "vue_demarrage", "renommer"}) {
            if (std::none_of(op.props.begin(), op.props.end(), [&](const auto& kv) { return kv.first == k; })) continue;
            const std::string field = std::string_view(k) == "renommer" ? "nom" : k;
            if (!app::projedit::setGroupField(*edit, id, field, prop(op, k), err)) {
                std::string msg = at + err;
                if (field == "vue_demarrage")
                    if (const auto near = nearViews(*edit, prop(op, k)); !near.empty()) msg += " ; les plus proches : " + joinList(near);
                if (field == "roles") {
                    std::vector<std::string> roles;
                    for (const auto& r : edit->security.roles) roles.push_back(r.name);
                    msg += " (les r\xC3\xB4les : " + joinList(roles) + ")";
                }
                return refuse(msg + ".");
            }
        }
        const hmi::UserGroup* g = edit->group(id);
        const auto rolesText = [](const hmi::UserGroup& x) { return x.roles.empty() ? std::string("aucun r\xC3\xB4le") : "r\xC3\xB4les " + joinList(x.roles); };
        const auto viewText = [&](hmi::Id v) {
            const hmi::View* vw = edit->view(v);
            return vw ? vw->name : std::string("la vue de d\xC3\xA9marrage");
        };
        if (create) {
            made("Groupe", g->name, "niveau " + std::to_string(g->level) + ", " + rolesText(*g)
                                       + (g->startView != hmi::kNoId ? ", vue de d\xC3\xA9part " + viewText(g->startView) : std::string{}));
        } else {
            std::vector<std::string> said;
            if (old.name != g->name) said.push_back("renomm\xC3\xA9 en " + g->name);
            if (old.level != g->level) said.push_back("niveau " + std::to_string(old.level) + kArrow + std::to_string(g->level));
            if (old.roles != g->roles) said.push_back(rolesText(*g));
            if (old.description != g->description) said.push_back("description : " + g->description);
            if (old.startView != g->startView) said.push_back("vue de d\xC3\xA9part " + viewText(g->startView));
            if (said.empty()) reuse("Groupe", g->name, "d\xC3\xA9j\xC3\xA0 ainsi : rien ne change");
            else made("Groupe", name, joinList(said, " ; "));
        }
        return true;
    }

    // CREER_UTILISATEUR, MODIFIER_UTILISATEUR : par le code de la fiche d'un utilisateur (app::projedit::addUser, setUserField).
    // JAMAIS de mot de passe : un compte cree par l'Assistant est desactive, << mot de passe a definir >> (a changer a la
    // premiere connexion) ; il ne s'active (actif) qu'une fois son mot de passe donne dans Configuration > Utilisateurs.
    static bool loggable(const hmi::User& u) {
        if (u.protection == "classique") return !u.passwordHash.empty();
        if (u.protection == "dynamique") return !u.secret.empty();
        if (u.protection == "expression") return !u.expression.empty();
        return false;
    }
    bool userOp(const Operation& op, bool create) {
        if (!edit) return noEdit(op);
        const std::string login = create ? trimmed(op.name) : (prop(op, "utilisateur").empty() ? trimmed(op.name) : prop(op, "utilisateur"));
        const std::string at = "Utilisateur \xC2\xAB " + login + " \xC2\xBB : ";
        if (login.empty()) return fail(std::string(actionKey(op.action)) + " : le login manque (\xC2\xAB " + (create ? "nom" : "utilisateur") + " \xC2\xBB).");
        static const std::vector<std::string> keys = {"utilisateur", "nom_complet", "groupe", "protection", "actif", "description", "changer", "renommer"};
        for (const auto& [k, v] : op.props) {
            if (k == "secret_refuse") return fail(at + "\xC2\xAB " + v + " \xC2\xBB refus\xC3\xA9 : " + kNoSecret + ".");
            if (std::find(keys.begin(), keys.end(), k) == keys.end() || (create && (k == "renommer" || k == "utilisateur" || k == "changer")))
                return fail(at + "\xC2\xAB " + k + " \xC2\xBB n'est pas un champ d'un utilisateur (nom_complet, groupe, protection, actif, description"
                            + (create ? std::string{} : std::string(", changer, renommer")) + ") ; " + kNoSecret + ".");
        }
        std::vector<std::string> groups;
        for (const auto& g : edit->security.groups) groups.push_back(g.name);
        const std::string protection = lower(prop(op, "protection"));
        if (!protection.empty() && protection != "classique" && protection != "dynamique") {
            if (protection == "expression")
                return fail(at + "la protection par expression est un moyen de connexion : elle se r\xC3\xA8gle dans Configuration > Utilisateurs "
                                 "(l'Assistant ne le choisit pas). Ici : classique (un mot de passe) ou dynamique (un code \xC3\xA0 6 chiffres).");
            return fail(at + "protection : classique ou dynamique (pas \xC2\xAB " + prop(op, "protection") + " \xC2\xBB).");
        }
        const hmi::Security before = edit->security;
        const auto refuse = [&](std::string text) {
            edit->security = before;
            return fail(std::move(text));
        };
        std::string err;
        hmi::Id id = hmi::kNoId;
        hmi::User old;
        if (create) {
            if (edit->userByLogin(login)) return fail(at + "il existe d\xC3\xA9j\xC3\xA0 : modifier_utilisateur pour le changer.");
            const std::string group = prop(op, "groupe");
            if (group.empty()) return fail(at + "son groupe manque (les groupes : " + joinList(groups) + ").");
            if (!edit->groupByName(group)) return fail(at + "groupe \xC2\xAB " + group + " \xC2\xBB introuvable ; les groupes : " + joinList(groups) + ".");
            id = app::projedit::addUser(*edit, login, group, err);
            if (id == hmi::kNoId) return refuse(at + err + ".");
        } else {
            const hmi::User* u = edit->userByLogin(login);
            if (!u) {
                std::vector<std::string> users;
                for (const auto& x : edit->security.users) users.push_back(x.login);
                return fail(at + "introuvable (l'Assistant n'invente pas de compte)" + (users.empty() ? std::string(" ; le projet n'en a aucun") : " ; les utilisateurs : " + joinList(users)) + ".");
            }
            id = u->id;
            old = *u;
        }
        // les champs, dans un ordre fixe ; le login en dernier (les actions qui le citent suivent)
        for (const char* k : {"nom_complet", "groupe", "protection", "description", "changer"}) {
            if (std::none_of(op.props.begin(), op.props.end(), [&](const auto& kv) { return kv.first == k; })) continue;
            if (create && std::string_view(k) == "groupe") continue;
            std::string value = std::string_view(k) == "protection" ? protection : prop(op, k);
            if (std::string_view(k) == "changer") {
                const auto b = boolWord(value);
                if (!b) return refuse(at + "changer : oui ou non.");
                value = *b ? "TRUE" : "FALSE";
            }
            const std::string field = std::string_view(k) == "nom_complet" ? "nom" : k;
            if (!app::projedit::setUserField(*edit, id, field, value, err)) return refuse(at + err + (field == "groupe" ? " ; les groupes : " + joinList(groups) : std::string{}) + ".");
        }
        if (create) {
            // Jamais de mot de passe : desactive et << a definir >> tant que l'utilisateur ne l'a pas donne.
            hmi::User* u = edit->user(id);
            u->enabled = false;
            u->mustChange = true;
            if (const auto b = boolWord(prop(op, "actif")); b && *b)
                warn(at + "reste d\xC3\xA9sactiv\xC3\xA9 tant que son mot de passe n'est pas d\xC3\xA9" "fini (l'Assistant n'en donne jamais).");
            const hmi::UserGroup* g = edit->group(u->group);
            made("Utilisateur", u->login, (u->fullName.empty() ? std::string{} : u->fullName + ", ") + "groupe " + (g ? g->name + " (niveau " + std::to_string(g->level) + ")" : std::string("?"))
                                              + (u->protection == "dynamique" ? ", code dynamique (secret cr\xC3\xA9\xC3\xA9 par le logiciel)" : std::string{})
                                              + " ; d\xC3\xA9sactiv\xC3\xA9 : mot de passe \xC3\xA0 d\xC3\xA9" "finir dans Configuration > Utilisateurs");
            warn(u->login + " : mot de passe \xC3\xA0 d\xC3\xA9" "finir dans Configuration > Utilisateurs (l'Assistant n'en choisit jamais) - le compte reste "
                 "d\xC3\xA9sactiv\xC3\xA9 jusque-l\xC3\xA0, puis \xC3\xA0 changer \xC3\xA0 sa premi\xC3\xA8re connexion.");
            return true;
        }
        if (const std::string a = prop(op, "actif"); !a.empty()) {
            const auto b = boolWord(a);
            if (!b) return refuse(at + "actif : oui ou non.");
            if (*b && !loggable(*edit->user(id)))
                return refuse(at + "pas encore de " + (edit->user(id)->protection == "dynamique" ? std::string("secret") : std::string("mot de passe"))
                              + " : donne-le dans Configuration > Utilisateurs, puis active le compte (l'Assistant n'en donne jamais).");
            if (!app::projedit::setUserField(*edit, id, "actif", *b ? "TRUE" : "FALSE", err)) return refuse(at + err + ".");
        }
        std::size_t followed = 0;
        if (const std::string to = prop(op, "renommer"); !to.empty() && to != old.login)
            if (!app::projedit::setUserField(*edit, id, "login", to, err, &followed)) return refuse(at + "renommer : " + err + ".");
        const hmi::User& u = *edit->user(id);
        std::vector<std::string> said;
        if (old.login != u.login) said.push_back("login " + u.login + (followed ? " (" + std::to_string(followed) + " action(s) suivent)" : std::string{}));
        if (old.fullName != u.fullName) said.push_back("nom " + u.fullName);
        if (old.group != u.group) {
            const hmi::UserGroup* g = edit->group(u.group);
            said.push_back("groupe " + (g ? g->name + " (niveau " + std::to_string(g->level) + ")" : std::string("?")));
        }
        if (old.protection != u.protection)
            said.push_back("protection " + u.protection + (u.protection == "dynamique" && old.secret != u.secret ? std::string(" (secret cr\xC3\xA9\xC3\xA9 par le logiciel : \xC3\xA0 lire dans Configuration > Utilisateurs)") : std::string{}));
        if (old.enabled != u.enabled) said.push_back(u.enabled ? "activ\xC3\xA9" : "D\xC3\x89" "SACTIV\xC3\x89");
        if (old.description != u.description) said.push_back("description : " + u.description);
        if (old.mustChange != u.mustChange) said.push_back(u.mustChange ? "\xC3\xA0 changer \xC3\xA0 la prochaine connexion" : "plus \xC3\xA0 changer");
        if (said.empty()) reuse("Utilisateur", u.login, "d\xC3\xA9j\xC3\xA0 ainsi : rien ne change");
        else made("Utilisateur", login, joinList(said, " ; "));
        return true;
    }

    // ---- LES VUES -------------------------------------------------------------------------------------------------------
    // MODIFIER_VUE : la parente, l'ecran modele, l'en-tete, le pied (et leurs modeles), la taille - par le code de l'inspecteur
    // d'une vue (app::projedit::setViewLink). Les objets que la bande recouvre : la passe des bandes, apres tout le plan.
    bool viewOp(const Operation& op) {
        if (!edit) return noEdit(op);
        const std::string name = trimmed(op.name);
        const std::string at = "Vue \xC2\xAB " + name + " \xC2\xBB : ";
        if (name.empty()) return fail("modifier_vue : quelle vue ? (\xC2\xAB vue \xC2\xBB : son nom).");
        hmi::View* v = edit->viewByName(name);
        if (!v) {
            std::string msg = at + "introuvable (l'Assistant n'invente pas de vue)";
            if (const auto near = nearViews(*edit, name); !near.empty()) msg += " ; les plus proches : " + joinList(near);
            return fail(msg + ".");
        }
        if (op.props.empty()) return fail(at + "rien \xC3\xA0 modifier (parent, modele, entete, pied, hauteur...).");
        // une vue legere : ses liens et sa taille seulement (setViewLink ne lit qu'eux)
        hmi::View probe;
        probe.id = v->id;
        probe.name = v->name;
        probe.role = v->role;
        probe.upView = v->upView;
        probe.templateView = v->templateView;
        probe.showHeader = v->showHeader;
        probe.header = v->header;
        probe.showFooter = v->showFooter;
        probe.footer = v->footer;
        probe.width = v->width;
        probe.height = v->height;
        for (const auto& [k, raw] : op.props) {
            if (k == "decaler" || k == "agrandir") {
                if (!boolWord(raw)) return fail(at + k + " : oui ou non.");
                continue;
            }
            if (k == "hauteur" || k == "largeur") {
                long long n = 0;
                if (!sim::readInteger(trimmed(raw), n) || n < 16 || n > 100000)
                    return fail(at + (k == "hauteur" ? "hauteur" : "largeur") + " : un nombre entier de pixels, 16 au moins (pas \xC2\xAB " + raw + " \xC2\xBB).");
                (k == "hauteur" ? probe.height : probe.width) = static_cast<int>(n);
                continue;
            }
            std::string refused;
            bool known = false;
            if (k == "modele" && (v->role == "entete" || v->role == "pied"))
                return fail(at + "un mod\xC3\xA8le d'en-t\xC3\xAAte ou de pied n'h\xC3\xA9rite pas d'un \xC3\xA9" "cran mod\xC3\xA8le.");
            if (!viewLinkProp(*edit, probe, k, raw, refused, &known)) return fail(at + refused + ".");
            if (!known) return fail(at + "\xC2\xAB " + k + " \xC2\xBB n'est pas un r\xC3\xA9glage de modifier_vue (parent, modele, entete, pied, entete_modele, pied_modele, hauteur, largeur, decaler, agrandir).");
        }
        std::vector<std::string> said;
        const auto nameOf = [&](hmi::Id id, const char* none) {
            const hmi::View* x = edit->view(id);
            return x ? x->name : std::string(none);
        };
        if (probe.upView != v->upView) said.push_back(probe.upView == hmi::kNoId ? std::string("sans parente (une racine)") : "parent " + nameOf(probe.upView, "?"));
        if (probe.templateView != v->templateView) said.push_back(probe.templateView == hmi::kNoId ? std::string("sans \xC3\xA9" "cran mod\xC3\xA8le") : "mod\xC3\xA8le " + nameOf(probe.templateView, "?"));
        if (probe.width != v->width || probe.height != v->height)
            said.push_back("taille " + std::to_string(v->width) + " \xC3\x97 " + std::to_string(v->height) + kArrow + std::to_string(probe.width) + " \xC3\x97 " + std::to_string(probe.height));
        const bool headChanged = probe.showHeader != v->showHeader || probe.header != v->header;
        const bool footChanged = probe.showFooter != v->showFooter || probe.footer != v->footer;
        // le commit (les liens et la taille), puis ce que montrent les bandes
        v->upView = probe.upView;
        v->templateView = probe.templateView;
        v->showHeader = probe.showHeader;
        v->header = probe.header;
        v->showFooter = probe.showFooter;
        v->footer = probe.footer;
        v->width = probe.width;
        v->height = probe.height;
        if (headChanged) {
            const hmi::View* h = hmi::headerOf(*edit, *v);
            said.push_back(!v->showHeader ? std::string("sans en-t\xC3\xAAte") : h ? "en-t\xC3\xAAte " + h->name + " (" + std::to_string(h->height) + " px)" : std::string("en-t\xC3\xAAte coch\xC3\xA9"));
            if (v->showHeader && !h) warn(at + "l'en-t\xC3\xAAte est coch\xC3\xA9, mais le projet n'a aucun mod\xC3\xA8le d'en-t\xC3\xAAte : rien ne s'affiche (creer_vue, role entete).");
        }
        if (footChanged) {
            const hmi::View* f = hmi::footerOf(*edit, *v);
            said.push_back(!v->showFooter ? std::string("sans pied") : f ? "pied " + f->name + " (" + std::to_string(f->height) + " px)" : std::string("pied coch\xC3\xA9"));
            if (v->showFooter && !f) warn(at + "le pied est coch\xC3\xA9, mais le projet n'a aucun mod\xC3\xA8le de pied : rien ne s'affiche (creer_vue, role pied).");
        }
        if (said.empty()) reuse("Vue", v->name, "d\xC3\xA9j\xC3\xA0 ainsi : rien ne change");
        else made("Vue", v->name, joinList(said, ", "));
        return true;
    }

    // ---- 1.22.8 : LES UNITES DE MESURE -------------------------------------------------------------------------------
    // Une unite inconnue (creer_variables, modifier_variable) : son defaut structure - le clic droit la cree (<< Creer l'unite... >>,
    // Corriger rapidement quand le logiciel la reconnait). Seulement un symbole inconnu sur un nombre (pas une unite sur un BOOL).
    void unknownUnit(const std::string& symbol, const std::string& type, const std::string& message) {
        if (!check || trimmed(symbol).empty() || !hmi::units::typeTakesUnit(type)) return;
        if (hmi::units::Catalog((edit ? *edit : project).config).known(symbol)) return;
        Check::Fault f;
        f.op = current;
        f.kind = "unite";
        f.name = trimmed(symbol);
        f.message = message;
        f.type = type;
        check->faults.push_back(std::move(f));
    }

    // CREER_UNITE : une unite du projet (et sa famille neuve, dite par sa reference) - les controles de Configuration > Unites de
    // mesure (planUnit) ; un facteur loin de celui d'une unite courante de meme symbole : un avertissement ; la conversion montree.
    bool unitOp(const Operation& op) {
        if (!edit) return noEdit(op);
        const std::string at = "Unit\xC3\xA9 \xC2\xAB " + trimmed(op.name) + " \xC2\xBB : ";
        const UnitPlan pl = planUnit(edit->config, op);
        if (!pl.error.empty()) return fail(at + pl.error + ".");
        if (pl.same) {
            reuse("Unit\xC3\xA9", pl.unit.symbol, "existe d\xC3\xA9j\xC3\xA0 \xC3\xA0 l'identique (" + pl.conversion + ") : gard\xC3\xA9" "e telle quelle");
            return true;
        }
        for (const auto& w : pl.warnings) warn(at + w + ".");
        if (!pl.family.name.empty()) {
            edit->config.unitFamilies.push_back(pl.family);
            if (lower(pl.family.reference) != lower(pl.unit.symbol)) {
                const auto* k = hmi::units::knownUnit(pl.family.reference);
                edit->config.units.push_back({pl.family.reference, pl.family.name, 1.0, 0.0, k ? std::string(k->label) : std::string{}});
            }
            made("Famille d'unit\xC3\xA9s", pl.family.name, "nouvelle, r\xC3\xA9" "f\xC3\xA9rence " + pl.family.reference + " (cr\xC3\xA9\xC3\xA9" "e avec elle)");
        }
        edit->config.units.push_back(pl.unit);
        made("Unit\xC3\xA9", pl.unit.symbol, unitText(pl));
        return true;
    }

    // MODIFIER_UNITE : une unite du projet (une fournie est verrouillee) - son facteur, son decalage, son libelle.
    bool unitEditOp(const Operation& op) {
        if (!edit) return noEdit(op);
        namespace hu = hmi::units;
        const std::string symbol = trimmed(op.name);
        const std::string at = "Unit\xC3\xA9 \xC2\xAB " + symbol + " \xC2\xBB : ";
        if (symbol.empty()) return fail("modifier_unite : le symbole manque (symbole : mmCE).");
        auto& units = edit->config.units;
        const std::string canon = hu::Catalog(edit->config).canonical(symbol);   // sans casse, comme la case Unite
        const auto cur = std::find_if(units.begin(), units.end(), [&](const hmi::UnitDef& u) { return !canon.empty() && u.symbol == canon; });
        if (cur == units.end()) {
            if (hu::builtinUnit(symbol))
                return fail(at + "une unit\xC3\xA9 fournie est verrouill\xC3\xA9" "e (une autre conversion : creer_unite d'un autre symbole).");
            return fail(at + "unit\xC3\xA9 du projet introuvable (l'Assistant n'invente pas d'unit\xC3\xA9 ; une neuve : creer_unite).");
        }
        hmi::UnitDef next = *cur;
        for (const char* k : {"facteur", "decalage"}) {
            const std::string t = prop(op, k);
            if (t.empty()) continue;
            double d = 0;
            if (!hu::parseValue(t, d)) return fail(at + k + " \xC2\xAB " + t + " \xC2\xBB : un nombre (9.80665).");
            (std::string_view(k) == "facteur" ? next.factor : next.offset) = d;
        }
        if (const std::string f = prop(op, "famille"); !f.empty() && !iequal(f, cur->family) && !iequal(hu::familyFrom(hu::Catalog(edit->config), f), cur->family))
            return fail(at + "sa famille ne change pas (" + cur->family + ") : cr\xC3\xA9" "e plut\xC3\xB4t une unit\xC3\xA9 d'un autre symbole.");
        if (std::any_of(op.props.begin(), op.props.end(), [](const auto& kv) { return kv.first == "libelle"; })) next.label = prop(op, "libelle");
        if (const std::string bad = hu::unitChangeProblem(edit->config, cur->symbol, next.factor, next.offset); !bad.empty()) return fail(at + bad + ".");
        if (next == *cur) {
            reuse("Unit\xC3\xA9", cur->symbol, "d\xC3\xA9j\xC3\xA0 ainsi : rien ne change");
            return true;
        }
        {   // la coherence avec une unite courante de meme symbole (le catalogue sans elle)
            hmi::Config probe = edit->config;
            probe.units.erase(probe.units.begin() + (cur - units.begin()));
            if (const std::string w = hu::coherenceWarning(hu::Catalog(probe), next); !w.empty()) warn(at + w + ".");
        }
        if (next.factor != cur->factor || next.offset != cur->offset)
            if (const auto uses = unitui::usesOf(*edit, cur->symbol); !uses.empty())
                warn(at + "employ\xC3\xA9" "e (" + std::to_string(uses.size()) + " : " + joinList(std::vector<std::string>(uses.begin(), uses.begin() + std::min<std::size_t>(uses.size(), 3)))
                     + (uses.size() > 3 ? ", \xE2\x80\xA6" : "") + ") : leurs valeurs converties changent.");
        *cur = next;
        const hu::Catalog cat(edit->config);
        const hmi::UnitFamily* f = cat.family(next.family);
        made("Unit\xC3\xA9", next.symbol, "modifi\xC3\xA9" "e : " + (f ? hu::formulaText(cat, next.symbol, f->reference) : std::string("?")));
        return true;
    }

    // VUE_DE_DEMARRAGE : la vue ouverte au lancement (une vue ordinaire : ni popup, ni symbole, ni modele).
    bool startViewOp(const Operation& op) {
        if (!edit) return noEdit(op);
        const std::string name = trimmed(op.name);
        const std::string at = "Vue de d\xC3\xA9marrage \xC2\xAB " + name + " \xC2\xBB : ";
        const hmi::View* v = edit->viewByName(name);
        if (!v) {
            std::string msg = at + "vue introuvable (l'Assistant n'invente pas de vue)";
            if (const auto near = nearViews(*edit, name, "vue"); !near.empty()) msg += " ; les plus proches : " + joinList(near);
            return fail(msg + ".");
        }
        if (v->role != "vue") return fail(at + v->name + " est un(e) " + std::string(hmi::viewRoleLabel(v->role)) + " : la vue de d\xC3\xA9marrage est une vue ordinaire.");
        const hmi::View* was = edit->view(edit->config.startView);
        if (was && was->id == v->id) {
            reuse("Vue de d\xC3\xA9marrage", v->name, "c'est d\xC3\xA9j\xC3\xA0 elle");
            return true;
        }
        edit->config.startView = v->id;
        made("Vue de d\xC3\xA9marrage", v->name, "avant : " + (was ? was->name : std::string("aucune")));
        return true;
    }

    bool go() {
        for (std::size_t index = 0; index < p.ops.size(); ++index) {
            const Operation& op = p.ops[index];
            current = index;   // 1.22.1
            bool ok = true;
            Span span;
            span.op = index;
            span.e0 = check ? check->errors.size() : 0;
            span.w0 = check ? check->warnings.size() : 0;
            span.m0 = check ? check->made.size() : 0;
            span.r0 = reusedItems.size();
            // 1.22.6 : un secret retire a la lecture (operationsFromJson) - l'operation est refusee, la raison dite (jamais sa valeur).
            if (const std::string sk = prop(op, "secret_refuse"); !sk.empty())
                ok = fail(std::string(actionKey(op.action)) + (trimmed(op.name).empty() ? std::string{} : " " + trimmed(op.name)) + " : \xC2\xAB " + sk
                          + " \xC2\xBB refus\xC3\xA9 : " + kNoSecret + ".");
            else switch (op.action) {
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
                // 1.22.6 : les options des variables, la securite, les vues.
                case Operation::Action::VarEdit: ok = editVariables(op); break;
                case Operation::Action::Security: ok = securityOp(op); break;
                case Operation::Action::Group: ok = groupOp(op, true); break;
                case Operation::Action::GroupEdit: ok = groupOp(op, false); break;
                case Operation::Action::User: ok = userOp(op, true); break;
                case Operation::Action::UserEdit: ok = userOp(op, false); break;
                case Operation::Action::ViewEdit: ok = viewOp(op); break;
                case Operation::Action::StartView: ok = startViewOp(op); break;
                // 1.22.8 : les unites de mesure.
                case Operation::Action::Unit: ok = unitOp(op); break;
                case Operation::Action::UnitEdit: ok = unitEditOp(op); break;
            }
            span.e1 = check ? check->errors.size() : 0;
            span.w1 = check ? check->warnings.size() : 0;
            span.m1 = check ? check->made.size() : 0;
            span.r1 = reusedItems.size();
            spans.push_back(span);
            if (!ok) return false;
        }
        if (!deferCompile) compileCreated();
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
                const std::string text = std::to_string(renamed.size())
                                         + (renamed.size() > 1 ? " noms pris ou invalides : les objets neufs s'appellent autrement ("
                                                               : " nom pris ou invalide : l'objet neuf s'appelle autrement (")
                                         + list + ").";
                check->warnings.push_back(text);
                // 1.22.4 : les objets renommes (l'apercu les encadre ; le message n'en cite que quatre)
                const std::set<std::string> fresh(renamedTo.begin(), renamedTo.end());
                for (const auto& o : view.objects)
                    if (createdIds.count(o.id) && fresh.count(o.name)) spot(o, text, false, true);
            }
            // Les objets crees qui montrent ou ecrivent une valeur, sans variable : dit, jamais invente.
            // 1.22.4 : CES objets sont notes (Check::spots) - le message ne les nomme pas, l'apercu les encadre.
            std::vector<const hmi::Object*> unbound;
            for (const auto& o : view.objects)
                if (createdIds.count(o.id) && valueBearing(o.kind) && o.text("variable").empty() && o.expr("value").empty()) unbound.push_back(&o);
            if (!unbound.empty()) {
                const std::string text = std::to_string(unbound.size())
                                         + (unbound.size() > 1 ? " objets cr\xC3\xA9\xC3\xA9s ne sont reli\xC3\xA9s" : " objet cr\xC3\xA9\xC3\xA9 n'est reli\xC3\xA9")
                                         + " \xC3\xA0 aucune variable : \xC3\xA0 associer ensuite (l'Assistant n'invente pas de variable).";
                check->warnings.push_back(text);
                for (const auto* o : unbound) spot(*o, text, false, true);
            }
        }
        return true;
    }
};

// 1.16.0 : la proposition cree-t-elle des variables ou des types (le projet change, pas seulement une vue) ?
bool createsProjectItems(const Proposal& p) noexcept {
    return std::any_of(p.ops.begin(), p.ops.end(), [](const Operation& op) { return projectLevel(op); });
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
    // 1.20.0 : une grosse proposition (des centaines d'adresses) : les variables indexees une fois (la carte relit cette
    // empreinte souvent ; project.variable parcourt toute la liste a chaque nom).
    std::unordered_map<std::string, const hmi::Variable*> byName;
    const auto variableNamed = [&](const std::string& name) -> const hmi::Variable* {
        if (p.ops.size() < 16) return project.variable(name);
        if (byName.empty())
            for (const auto& v : project.programs.variables) byName.emplace(lower(v.name), &v);   // le premier garde (comme variable())
        const auto it = byName.find(lower(name));
        return it == byName.end() ? nullptr : it->second;
    };
    for (const auto& op : p.ops) {
        if (op.action == Operation::Action::Variables)
            for (const auto& spec : op.variables) {
                const std::string name = trimmed(spec.name);
                out += "V:" + name + "=";
                if (const auto* v = variableNamed(name)) out += v->type + "|" + v->initial + "|" + v->description;
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
        // 1.21.0 : une suppression d'elements du projet - chacun present ou absent (applique : absents ; Ctrl+Z : presents).
        if (del::projectRemoval(op))
            for (const auto& it : del::namedItems(op)) out += "X:" + op.kind + ":" + lower(it.name) + "=" + (del::exists(project, it) ? "1" : "-") + ";";
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
                if (const auto* x = variableNamed(v)) out += x->equipment + "@" + x->address + "#" + std::to_string(x->places.size());
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
            // 1.19.0 : une vue neuve autre que la principale (un symbole, une popup... avec une vue de supervision) :
            // absente, ou son empreinte (la principale est en tete de proposalPrint).
            case Operation::Action::View:
                if (!iequal(name, newViewName(p))) {
                    const hmi::View* v = project.viewByName(name);
                    out += "W:" + name + "=" + (v ? fingerprint(*v) : std::string("-")) + ";";
                }
                break;
            // 1.22.6 : la variable telle qu'elle est (ses options ; renommee : le nouveau nom aussi), la securite (jamais une
            // empreinte ni un secret : les reglages, les groupes, les logins), les liens d'une vue, la vue de demarrage.
            case Operation::Action::VarEdit: {
                std::vector<std::string> names;
                if (!trimmed(op.variable).empty()) names.push_back(trimmed(op.variable));
                for (const auto& n : op.objects) names.push_back(trimmed(n));
                for (const auto& [k, v] : op.props)
                    if (k == "renommer") names.push_back(trimmed(v));
                for (const auto& n : names) {
                    out += "M:" + n + "=";
                    if (const auto* x = variableNamed(n))
                        out += x->name + "|" + x->type + "|" + x->initial + "|" + x->description + "|" + x->folder + "|" + x->unit + "|" + x->equipment + "@"
                               + x->address + "|" + (x->readOnly ? "L" : "") + (x->retain ? "R" : "") + (x->packBools ? "P" : "") + (x->compact ? "C" : "")
                               + "|" + hmi::formatNumber(x->rawMin) + "," + hmi::formatNumber(x->rawMax) + "," + hmi::formatNumber(x->engMin) + ","
                               + hmi::formatNumber(x->engMax) + "," + x->rawType + "|" + std::to_string(x->internal.size());
                    else out += "-";
                    out += ";";
                }
                break;
            }
            case Operation::Action::Security:
            case Operation::Action::Group:
            case Operation::Action::GroupEdit:
            case Operation::Action::User:
            case Operation::Action::UserEdit: {
                const hmi::Security& sc = project.security;
                out += std::string("Z:") + (sc.enabled ? "1" : "0") + "|" + sc.startUser + "|" + std::to_string(sc.autoLogoutMin) + "|"
                       + std::to_string(sc.menuLevelAccounts) + std::to_string(sc.menuLevelAccess) + std::to_string(sc.menuLevelJournal) + "|"
                       + std::to_string(sc.pwMinLength) + "," + std::to_string(sc.pwMaxAgeDays) + "," + std::to_string(sc.pwHistory) + ","
                       + std::to_string(sc.lockAttempts) + "," + std::to_string(sc.lockMinutes) + "," + std::to_string(sc.logoutWarnS)
                       + (sc.pwDigit ? "d" : "") + (sc.pwLetter ? "l" : "") + (sc.pwMixedCase ? "c" : "") + (sc.pwSpecial ? "s" : "")
                       + (sc.pwChangeFirst ? "f" : "") + (sc.badgeLogin ? "b" : "") + "|";
                for (const auto& g : sc.groups) {
                    out += g.name + ":" + std::to_string(g.level) + ":" + std::to_string(g.startView) + ":" + g.description + ":";
                    for (const auto& r : g.roles) out += r + ",";
                    out += "/";
                }
                out += "|";
                for (const auto& u : sc.users)
                    out += u.login + ":" + std::to_string(u.group) + ":" + u.fullName + ":" + u.protection + ":" + (u.enabled ? "1" : "0")
                           + (u.mustChange ? "m" : "") + ":" + u.description + "/";
                out += ";";
                break;
            }
            case Operation::Action::ViewEdit: {
                out += "Y:" + name + "=";
                if (const hmi::View* v = project.viewByName(name))
                    out += std::to_string(v->upView) + "|" + std::to_string(v->templateView) + "|" + (v->showHeader ? "H" : "") + std::to_string(v->header) + "|"
                           + (v->showFooter ? "F" : "") + std::to_string(v->footer) + "|" + std::to_string(v->width) + "x" + std::to_string(v->height) + "|"
                           + fingerprint(*v);
                else out += "-";
                out += ";";
                break;
            }
            case Operation::Action::StartView:
                out += "S0:" + std::to_string(project.config.startView) + ";";
                break;
            // 1.22.8 : l'unite telle qu'elle est (absente, ou sa famille, son facteur, son decalage, son libelle).
            case Operation::Action::Unit:
            case Operation::Action::UnitEdit: {
                out += "U:" + name + "=";
                const hmi::units::Catalog cat(project.config);
                if (const auto* u = cat.unit(name))
                    out += u->family + "|" + hmi::units::exactNumber(u->factor) + "|" + hmi::units::exactNumber(u->offset) + "|" + u->label;
                else out += "-";
                out += ";";
                break;
            }
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
        case Operation::Action::VarEdit: return "modifier_variable";
        case Operation::Action::Security: return "securite";
        case Operation::Action::Group: return "creer_groupe";
        case Operation::Action::GroupEdit: return "modifier_groupe";
        case Operation::Action::User: return "creer_utilisateur";
        case Operation::Action::UserEdit: return "modifier_utilisateur";
        case Operation::Action::ViewEdit: return "modifier_vue";
        case Operation::Action::StartView: return "vue_de_demarrage";
        case Operation::Action::Unit: return "creer_unite";          // 1.22.8
        case Operation::Action::UnitEdit: return "modifier_unite";
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

bool projectLevel(const Operation& op) noexcept { return projectLevel(op.action) || del::projectRemoval(op); }

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
        case Operation::Action::Store:
        // 1.22.6 : les options des variables, la securite, les vues (leurs liens) - le projet, pas les objets de la vue ouverte.
        case Operation::Action::VarEdit:
        case Operation::Action::Security:
        case Operation::Action::Group:
        case Operation::Action::GroupEdit:
        case Operation::Action::User:
        case Operation::Action::UserEdit:
        case Operation::Action::ViewEdit:
        case Operation::Action::StartView:
        case Operation::Action::Unit:        // 1.22.8 : les unites de mesure du projet
        case Operation::Action::UnitEdit: return true;
        default: return false;
    }
}

std::optional<Operation::Action> actionFromKey(std::string_view k) noexcept {
    for (const auto a : {Operation::Action::Create, Operation::Action::Modify, Operation::Action::Bind, Operation::Action::Align,
                         Operation::Action::Distribute, Operation::Action::Remove, Operation::Action::Duplicate, Operation::Action::Code,
                         Operation::Action::View, Operation::Action::Variables, Operation::Action::Type, Operation::Action::Equipment,
                         Operation::Action::Address, Operation::Action::Script, Operation::Action::Function, Operation::Action::Alarm,
                         Operation::Action::Resource, Operation::Action::Folder, Operation::Action::Store, Operation::Action::VarEdit,
                         Operation::Action::Security, Operation::Action::Group, Operation::Action::GroupEdit, Operation::Action::User,
                         Operation::Action::UserEdit, Operation::Action::ViewEdit, Operation::Action::StartView, Operation::Action::Unit,
                         Operation::Action::UnitEdit})
        if (actionKey(a) == k) return a;
    if (k == "creer_unites") return Operation::Action::Unit;   // 1.22.8
    // 1.22.6 : des noms de plus (une liste de variables, les mots de la demande).
    if (k == "modifier_variables") return Operation::Action::VarEdit;
    if (k == "modifier_securite") return Operation::Action::Security;
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

const VariableChoice* choiceFor(const Proposal& p, std::string_view missing) {
    if (p.choices.empty()) return nullptr;
    const std::string want = trimmed(missing);
    for (const auto& c : p.choices)
        if (!c.from.empty() && iequal(trimmed(c.from), want)) return &c;
    return nullptr;
}

std::string creatableName(std::string_view missing) {
    std::string n = trimmed(missing);
    std::erase(n, '$');   // un repere ($Vanne$.Cmd) se lit sans ses $
    if (hmi::isIdentifier(n)) return n;
    // Un membre, un element de tableau (Vanne.Cmd, Pompes[3].Marche) : un seul identifiant (Vanne_Cmd, Pompes_3_Marche).
    std::string out;
    for (const char c : n) {
        const char k = std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? c : '_';
        if (k == '_' && !out.empty() && out.back() == '_') continue;
        out += k;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    while (!out.empty() && out.front() == '_') out.erase(out.begin());
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front()))) out.insert(0, "V_");
    if (out.size() > 64) out.resize(64);
    return out;
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
// 1.19.0 : une proposition peut creer PLUSIEURS vues (un symbole, une popup, la vue de supervision) et toucher aussi la
// vue ouverte. La vue neuve PRINCIPALE : la premiere creer_vue de role << vue >>, sinon la premiere creer_vue (en 1.18.0,
// la seule, en premier : le meme resultat).
bool createsView(const Proposal& p) noexcept {
    return std::any_of(p.ops.begin(), p.ops.end(), [](const Operation& op) { return op.action == Operation::Action::View; });
}

std::string newViewName(const Proposal& p) {
    const Operation* first = nullptr;
    for (const auto& op : p.ops) {
        if (op.action != Operation::Action::View) continue;
        if (!first) first = &op;
        std::string role;
        for (const auto& [k, v] : op.props)
            if (k == "role") role = lower(trimmed(v));
        if (role.empty() || role == "vue") return trimmed(op.name);
    }
    return first ? trimmed(first->name) : std::string{};
}

std::string opTarget(const Proposal& p, std::size_t index) {
    if (index >= p.ops.size() || projectLevel(p.ops[index])) return {};
    if (const std::string t = trimmed(p.ops[index].target); !t.empty()) return t;
    for (std::size_t j = index; j-- > 0;)
        if (p.ops[j].action == Operation::Action::View) return trimmed(p.ops[j].name);
    return {};
}

void scopeProposal(Proposal& p, const hmi::View* open) {
    if (usesOpenView(p) && open) {
        p.view = open->name;
        p.viewId = open->id;
    } else if (createsView(p)) {
        p.view = newViewName(p);
        p.viewId = hmi::kNoId;
    } else if (!usesOpenView(p)) {
        p.view.clear();
        p.viewId = hmi::kNoId;
    }
}

bool usesOpenView(const Proposal& p) noexcept {
    // Une proposition vide (une ancienne conversation) : la vue visee, comme avant.
    if (p.ops.empty()) return true;
    for (std::size_t i = 0; i < p.ops.size(); ++i)
        if (!projectLevel(p.ops[i]) && opTarget(p, i).empty()) return true;
    return false;
}

std::string proposalPrint(const hmi::Project& project, const Proposal& p, std::string_view viewPrint) {
    std::string s;
    if (createsView(p)) {
        // la vue neuve principale : par son identifiant une fois creee (une proposition qui ne touche pas la vue ouverte :
        // viewId est alors le sien), sinon par son nom
        const hmi::View* v = !usesOpenView(p) && p.viewId != hmi::kNoId ? project.view(p.viewId) : nullptr;
        if (!v) v = project.viewByName(newViewName(p));
        s = "vue:" + (v ? fingerprint(*v) : std::string("absente"));
        if (usesOpenView(p)) s += "|" + std::string(viewPrint);
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
    // 1.22.6 : les options des variables, la securite, les vues.
    add(Operation::Action::VarEdit, "variable modifi\xC3\xA9" "e", "variables modifi\xC3\xA9" "es");
    add(Operation::Action::Security, "r\xC3\xA9glage de la s\xC3\xA9" "curit\xC3\xA9", "r\xC3\xA9glages de la s\xC3\xA9" "curit\xC3\xA9");
    add(Operation::Action::Group, "groupe", "groupes");
    add(Operation::Action::GroupEdit, "groupe modifi\xC3\xA9", "groupes modifi\xC3\xA9s");
    add(Operation::Action::User, "utilisateur", "utilisateurs");
    add(Operation::Action::UserEdit, "utilisateur modifi\xC3\xA9", "utilisateurs modifi\xC3\xA9s");
    add(Operation::Action::ViewEdit, "vue modifi\xC3\xA9" "e", "vues modifi\xC3\xA9" "es");
    add(Operation::Action::StartView, "vue de d\xC3\xA9marrage", "vues de d\xC3\xA9marrage");
    add(Operation::Action::Unit, "unit\xC3\xA9", "unit\xC3\xA9s");   // 1.22.8
    add(Operation::Action::UnitEdit, "unit\xC3\xA9 modifi\xC3\xA9" "e", "unit\xC3\xA9s modifi\xC3\xA9" "es");
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
    // 1.22.6 : ses liens - parente, ecran modele, en-tete, pied (le code de l'inspecteur ; un refus est dit par la validation).
    for (const auto& [k, val] : op.props) {
        if (k == "modele" && (v.role == "entete" || v.role == "pied")) continue;
        std::string why;
        (void)viewLinkProp(project, v, k, val, why);
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
            // 1.22.4 : gardee comme toute case de valeur (hmi::cellValue) - 'Vanne' et "Vanne" : 'Vanne' ; un texte
            // tape sans apostrophes, inconnu : 'Pompe' ; un nom connu : le nom ; =formule : la formule.
            if (!vp.defaultValue.empty()) {
                const auto cell = hmi::cellValue(&project, vp.type, vp.defaultValue, [&op](std::string_view n) {
                    return std::any_of(op.params.begin(), op.params.end(), [&](const auto& q) { return iequal(trimmed(q.name), n); });
                });
                if (cell.ok() && !cell.text.empty()) vp.defaultValue = cell.text;
            }
            vp.description = trimmed(prm.description);
            v.params.push_back(std::move(vp));
        }
    }
    if (v.description.empty()) v.description = "Cr\xC3\xA9\xC3\xA9" "e par l'Assistant IA";
    return v;
}

// =============================================================================================
//  1.19.0 : L'EXECUTION EN ETAPES (le plan, AiPlan.hpp). Les operations sont regroupees en
//  UNITES (une operation du projet ; une vue neuve et ses objets ; les objets d'une vue
//  existante), triees par leurs dependances - jamais l'ordre ecrit par l'IA. Chaque unite passe
//  par le meme moteur (Run) que la 1.18.0, sur un projet qui grandit d'unite en unite :
//    VALIDER   sur une COPIE du projet : tout est verifie, chaque operation recoit son etat
//              (validee, echouee, ignoree parce qu'elle depend d'une operation echouee ou
//              retiree) ; le code cree est compile une fois tout cree ;
//    APPLIQUER dans UNE commande (hmi::changeProject) : les seules operations validees, dans
//              l'ordre ; si une unite echoue malgre tout, le projet est remis tel qu'il etait
//              (rien a moitie) et la raison est dite ;
//    APERCU    comme valider, sans rien dire : la vue principale telle qu'elle serait.
// =============================================================================================
namespace {

enum class Mode : std::uint8_t { Check, Preview, Apply };

// Ce qu'une unite cree au projet : pose dans `pr` (les vues a part, par l'appelant).
void commitRun(Run& run, hmi::Project& pr) {
    for (auto& t : run.newTypes) pr.programs.types.push_back(std::move(t));
    for (auto& nv : run.newVars) pr.programs.variables.push_back(std::move(nv));
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
}

// Le court nom d'une operation, pour dire pourquoi une autre est ignoree.
std::string opLabel(const Proposal& p, std::size_t i) {
    if (i >= p.ops.size()) return {};
    const Operation& op = p.ops[i];
    const std::string name = trimmed(op.name.empty() ? op.variable : op.name);
    return std::string(actionKey(op.action)) + (name.empty() ? std::string{} : " " + name);
}

using Outcome = Check::Outcome;

// 1.21.0 : ce que les suppressions ont emporte (l'operation, l'element) et les elements dont une reference a change : la
// verification d'apres (validate, applyStaged) les relit.
struct RemovalLog {
    std::vector<std::pair<std::size_t, del::Item>> removed;
    std::vector<std::string>                       touched;
    bool                                           project{false};   // un element du projet est parti (pas seulement des objets)
    [[nodiscard]] std::vector<del::Item> items() const {
        std::vector<del::Item> out;
        for (const auto& [i, it] : removed) out.push_back(it);
        return out;
    }
};

bool stopped(const Outcome& o) noexcept {
    return o.state == Outcome::State::Failed || o.state == Outcome::State::Skipped || o.state == Outcome::State::Excluded;
}

// =============================================================================================
//  1.22.6 : LES BANDES D'EN-TETE ET DE PIED. La bande du modele d'en-tete se pose en haut de la vue (sa hauteur : celle du
//  modele), celle du pied en bas, PAR-DESSUS le contenu (hmi::compose) : un objet dessous est cache. Apres tout le plan (les
//  objets que la proposition cree ou deplace compris), pour chaque operation qui coche (ou change) un en-tete ou un pied
//  (modifier_vue, creer_vue) : les objets de la vue que la bande recouvre sont DITS (un avertissement, l'objet encadre) ; avec
//  << decaler >> (par defaut), ils glissent d'un meme decalage sous l'en-tete (au-dessus du pied), avec ceux qu'ils
//  pousseraient, leurs ecarts gardes ; plus la place : dit (agrandir, ou la hauteur de la vue). Un seul Ctrl+Z avec le reste.
// =============================================================================================
struct BandObject {
    hmi::Id   id{hmi::kNoId};
    hmi::Box  box;
    bool      locked{false};
};

// Les objets du haut de la vue (les enfants suivent leur groupe), leur cadre a l'ecran.
std::vector<BandObject> topObjects(const hmi::View& v) {
    std::vector<BandObject> out;
    for (const auto& o : v.objects)
        if (o.parent == hmi::kNoId) out.push_back({o.id, hmi::edit::rotatedBounds(o), v.effectivelyLocked(o)});
    return out;
}

std::string namesOf(const hmi::View& v, const std::vector<hmi::Id>& ids, std::size_t max = 6) {
    std::vector<std::string> names;
    for (const auto id : ids)
        if (const hmi::Object* o = v.object(id); o && names.size() < max) names.push_back(o->name);
    std::string out = joinList(names);
    if (ids.size() > max) out += ", \xE2\x80\xA6";
    return out;
}

struct BandResult {
    std::vector<std::string> warnings, made;
    std::vector<std::pair<hmi::Id, std::string>> spots;   // l'objet, le message (vide : deplace)
    std::vector<std::pair<hmi::Id, double>>      moved;   // l'objet, son y d'avant
};

// Une bande (`head` : l'en-tete, sinon le pied) de hauteur `band` sur la vue `v` ; `only` non vide : ces objets seulement (des
// objets poses par la proposition sous une bande deja la : dits, jamais deplaces).
BandResult checkBand(hmi::View& v, bool head, int band, int otherBand, bool shift, bool grow, const std::set<hmi::Id>* only) {
    BandResult r;
    const std::string what = head ? "l'en-t\xC3\xAAte" : "le pied";
    const auto all = topObjects(v);
    const double edge = head ? static_cast<double>(band) : static_cast<double>(v.height - band);
    std::vector<const BandObject*> under;
    for (const auto& b : all) {
        if (only && !only->count(b.id)) continue;
        const bool covered = head ? (b.box.y < edge && b.box.bottom() > 0) : (b.box.bottom() > edge && b.box.y < v.height);
        if (covered) under.push_back(&b);
    }
    if (under.empty()) return r;
    std::vector<hmi::Id> ids;
    for (const auto* b : under) ids.push_back(b->id);
    const std::string list = namesOf(v, ids);
    const std::string n = std::to_string(ids.size()) + (ids.size() > 1 ? " objets" : " objet");
    if (only) {
        const std::string text = v.name + " : " + n + " pos\xC3\xA9" + (ids.size() > 1 ? "s" : "") + " par la proposition " + (ids.size() > 1 ? "sont" : "est")
                                 + (head ? " sous l'en-t\xC3\xAAte (" : " sous le pied (") + std::to_string(band) + " px) : " + list
                                 + (head ? " - place-les plus bas." : " - place-les plus haut.");
        r.warnings.push_back(text);
        for (const auto id : ids) r.spots.emplace_back(id, text);
        return r;
    }
    const std::string covers = v.name + " : " + what + " (" + std::to_string(band) + " px) recouvre " + n + " : " + list;
    if (!shift) {
        r.warnings.push_back(covers + " (decaler : non - rien n'est d\xC3\xA9plac\xC3\xA9).");
        for (const auto id : ids) r.spots.emplace_back(id, r.warnings.back());
        return r;
    }
    // Le decalage commun : le haut le plus haut juste sous l'en-tete (le bas le plus bas juste au-dessus du pied).
    double dy = 0;
    if (head) {
        double top = under.front()->box.y;
        for (const auto* b : under) top = std::min(top, b->box.y);
        dy = edge - top;
    } else {
        double bottom = under.front()->box.bottom();
        for (const auto* b : under) bottom = std::max(bottom, b->box.bottom());
        dy = edge - bottom;
    }
    // Ceux qu'ils pousseraient glissent avec eux (les ecarts gardes) : de proche en proche.
    std::set<hmi::Id> moving(ids.begin(), ids.end());
    for (bool more = true; more;) {
        more = false;
        for (const auto& b : all) {
            if (moving.count(b.id)) continue;
            for (const auto& m : all) {
                if (!moving.count(m.id)) continue;
                const hmi::Box shifted{m.box.x, m.box.y + dy, m.box.w, m.box.h};
                const bool cross = shifted.x < b.box.right() && b.box.x < shifted.right() && shifted.y < b.box.bottom() && b.box.y < shifted.bottom();
                if (cross) {
                    moving.insert(b.id);
                    more = true;
                    break;
                }
            }
        }
    }
    std::vector<hmi::Id> locked, movable;
    double lo = 1e18, hi = -1e18;
    for (const auto& b : all) {
        if (!moving.count(b.id)) continue;
        if (b.locked) {
            locked.push_back(b.id);
            continue;
        }
        movable.push_back(b.id);
        lo = std::min(lo, b.box.y + dy);
        hi = std::max(hi, b.box.bottom() + dy);
    }
    // La place : sous l'en-tete et au-dessus du pied (l'autre bande, si elle est la).
    const double floor = head ? static_cast<double>(v.height - otherBand) : static_cast<double>(v.height);
    const double ceiling = head ? 0.0 : static_cast<double>(otherBand);
    double missing = 0;
    if (!movable.empty()) missing = head ? std::max(0.0, hi - floor) : std::max(0.0, ceiling - lo);
    if (missing > 0.5) {
        const int old = v.height;
        // l'en-tete : la vue grandit de ce qui manque en bas ; le pied : de ce qu'il recouvre (il descend avec le bas de la vue)
        const int need = old + static_cast<int>(std::ceil(head ? missing : -dy));
        if (!grow) {
            r.warnings.push_back(covers + " ; plus la place pour les d\xC3\xA9" "caler (il manque " + std::to_string(static_cast<int>(std::ceil(missing)))
                                 + " px) : rien n'est d\xC3\xA9plac\xC3\xA9. Agrandis la vue \xC3\xA0 " + std::to_string(need)
                                 + " px (modifier_vue : hauteur, ou agrandir : oui).");
            for (const auto id : ids) r.spots.emplace_back(id, r.warnings.back());
            return r;
        }
        v.height = need;
        r.made.push_back("hauteur de la vue " + std::to_string(old) + kArrow + std::to_string(need) + " px (agrandir)");
        if (!head) return r;   // le pied est descendu sous les objets : plus rien ne le touche
    }
    for (const auto id : movable)
        if (const hmi::Object* o = v.object(id)) r.moved.emplace_back(id, o->number("y"));
    if (!movable.empty()) {
        hmi::edit::move(v, movable, 0, dy);
        const int px = static_cast<int>(std::lround(std::fabs(dy)));
        r.made.push_back(std::to_string(movable.size()) + (movable.size() > 1 ? " objets " : " objet ") + (head ? "descendu" : "remont\xC3\xA9")
                         + (movable.size() > 1 ? "s" : "") + " de " + std::to_string(px) + " px " + (head ? "sous l'en-t\xC3\xAAte" : "au-dessus du pied")
                         + " : " + namesOf(v, movable) + " (leurs \xC3\xA9" "carts gard\xC3\xA9s)");
        for (const auto id : movable) r.spots.emplace_back(id, std::string{});
    }
    if (!locked.empty()) {
        r.warnings.push_back(v.name + " : " + namesOf(v, locked) + (locked.size() > 1 ? " sont verrouill\xC3\xA9s" : " est verrouill\xC3\xA9") + " : pas d\xC3\xA9plac\xC3\xA9"
                             + (locked.size() > 1 ? "s" : "") + ", " + what + " les recouvre (d\xC3\xA9verrouille-les).");
        for (const auto id : locked) r.spots.emplace_back(id, r.warnings.back());
    }
    return r;
}

// La passe des bandes (voir plus haut) : les operations validees qui cochent ou changent un en-tete, un pied (ou la hauteur
// d'une vue qui a un pied) ; puis, a la validation, les objets poses par la proposition sous une bande deja la (dits).
void bandPass(hmi::Project& pr, const Proposal& p, Check* check, std::vector<Check::Outcome>& out, std::map<hmi::Id, Touched>* touched,
              const std::map<hmi::Id, std::set<hmi::Id>>& placed) {
    std::set<hmi::Id> done;
    const auto apply = [&](std::size_t i, hmi::View& v, const BandResult& r) {
        for (const auto& w : r.warnings) {
            if (i < out.size()) out[i].warnings.push_back(w);
            if (check) check->warnings.push_back(w);
        }
        for (const auto& m : r.made) {
            const Check::Made x{"Disposition", v.name, m};
            if (i < out.size()) out[i].made.push_back(x);
            if (check) check->made.push_back(x);
        }
        if (check)
            for (const auto& [id, msg] : r.spots)
                if (const hmi::Object* o = v.object(id)) check->spots.push_back({i, v.name, id, o->name, msg, false});
        for (const auto& [id, y0] : r.moved) {
            const hmi::Object* o = v.object(id);
            if (!o) continue;
            if (touched) {
                Touched& t = (*touched)[v.id];
                if (std::find(t.created.begin(), t.created.end(), id) == t.created.end() && std::find(t.modified.begin(), t.modified.end(), id) == t.modified.end())
                    t.modified.push_back(id);
            }
            if (!check) continue;
            Item* it = nullptr;
            for (auto& x : check->items)
                if (x.id == id && x.view == v.name) it = &x;
            if (!it) {
                Item n;
                n.what = Item::What::Modify;
                n.id = id;
                n.name = o->name;
                n.kind = o->kind;
                n.view = v.name;
                check->items.push_back(std::move(n));
                it = &check->items.back();
                ++check->modify;
            }
            it->changes.push_back({"y", hmi::formatNumber(y0), hmi::formatNumber(o->number("y"))});
        }
    };
    for (std::size_t i = 0; i < p.ops.size() && i < out.size(); ++i) {
        const Operation& op = p.ops[i];
        if ((op.action != Operation::Action::ViewEdit && op.action != Operation::Action::View) || out[i].state != Check::Outcome::State::Validated) continue;
        const auto has = [&](const char* k) { return std::any_of(op.props.begin(), op.props.end(), [&](const auto& kv) { return kv.first == k; }); };
        const bool headKey = has("entete") || has("entete_modele");
        const bool footKey = has("pied") || has("pied_modele") || (op.action == Operation::Action::ViewEdit && has("hauteur"));
        if (!headKey && !footKey) continue;
        hmi::View* v = pr.viewByName(trimmed(op.name));
        if (!v) continue;
        const bool shift = boolWord(Run::prop(op, "decaler")).value_or(true);
        const bool grow = boolWord(Run::prop(op, "agrandir")).value_or(false);
        done.insert(v->id);
        const hmi::View* h = hmi::headerOf(pr, *v);
        const hmi::View* f = hmi::footerOf(pr, *v);
        const int hh = h ? h->height : 0, fh = f ? f->height : 0;
        if (headKey && h) apply(i, *v, checkBand(*v, true, hh, fh, shift, grow, nullptr));
        if (footKey && f) apply(i, *v, checkBand(*v, false, fh, hh, shift, grow, nullptr));
    }
    if (!check) return;
    for (const auto& [vid, ids] : placed) {
        if (done.count(vid) || ids.empty()) continue;
        hmi::View* v = pr.view(vid);
        if (!v) continue;
        const hmi::View* h = hmi::headerOf(pr, *v);
        const hmi::View* f = hmi::footerOf(pr, *v);
        if (h) apply(Check::Fault::kNoOp, *v, checkBand(*v, true, h->height, f ? f->height : 0, false, false, &ids));
        if (f) apply(Check::Fault::kNoOp, *v, checkBand(*v, false, f->height, h ? h->height : 0, false, false, &ids));
    }
}

// La securite ACTIVEE par la proposition sans un compte actif qui puisse administrer et se connecter : refusee - en marche, plus
// personne ne gererait les comptes (on se fermerait dehors). Faux et `why` : a l'application (la validation l'a dit avant).
bool securityPass(const hmi::Project& pr, const Proposal& p, Mode mode, Check* check, std::vector<Check::Outcome>& out, bool wasEnabled,
                  std::string* why) {
    if (wasEnabled || !pr.security.enabled || !app::projedit::administrators(pr, true).empty()) return true;
    std::vector<std::string> admins = app::projedit::administrators(pr, false);
    const std::string text = "Activer la s\xC3\xA9" "curit\xC3\xA9 : aucun compte actif ne peut administrer ET se connecter (la permission Administrer par son "
                             "groupe, un mot de passe)" + (admins.empty() ? std::string{} : " - " + joinList(admins) + " n'a pas encore de mot de passe")
                             + " ; en marche, plus personne ne pourrait g\xC3\xA9rer les comptes : on se fermerait dehors. Donne d'abord un mot de passe \xC3\xA0 un "
                               "compte administrateur dans Configuration > Utilisateurs (l'Assistant n'en donne jamais), puis redemande.";
    if (mode == Mode::Apply) {
        if (why) *why = text;
        return false;
    }
    for (std::size_t i = 0; i < p.ops.size() && i < out.size(); ++i)
        if (p.ops[i].action == Operation::Action::Security && boolWord(Run::prop(p.ops[i], "active")).value_or(false)
            && out[i].state == Check::Outcome::State::Validated) {
            out[i].errors.push_back(text);
            out[i].state = Check::Outcome::State::Failed;
        }
    if (check) check->errors.push_back(text);
    return true;
}

// Le coeur : `pr` grandit d'unite en unite. `only` (appliquer) : les operations a executer ; nul : toutes celles qui ne
// sont ni retirees, ni bloquees, ni dependantes d'une operation qui l'est. Faux et `why` : une unite a echoue (appliquer).
thread_local std::atomic<std::size_t>* tlProgress = nullptr;   // 1.20.0 : setProgressCounter

bool runStaged(hmi::Project& pr, const Proposal& p, const Env& env, Mode mode, Check* check, std::vector<Outcome>& out,
               const plan::Schedule& sch, const std::set<std::size_t>* only, std::string* why,
               std::map<hmi::Id, Touched>* touched, hmi::Id openView, RemovalLog* log = nullptr) {
    const std::size_t n = p.ops.size();
    out.assign(n, Outcome{});
    // 1.21.0 : la vue ouverte (son nom) ; ce que les AUTRES suppressions de la proposition emportent (celles qui ne sont ni
    // retirees, ni echouees) - leurs dependances ne bloquent pas celle-ci.
    const std::string openName = [&] {
        const hmi::View* v = pr.view(openView);
        return v ? v->name : p.view;
    }();
    const auto alsoFor = [&](std::size_t self) {
        std::vector<del::Item> list;
        for (std::size_t j = 0; j < n; ++j) {
            if (j == self || p.ops[j].action != Operation::Action::Remove || stopped(out[j])) continue;
            const std::string target = opTarget(p, j);
            for (auto& it : del::namedItems(p.ops[j], target.empty() ? openName : target)) list.push_back(std::move(it));
        }
        return list;
    };
    const std::set<std::size_t> excluded(p.excluded.begin(), p.excluded.end());
    const std::set<std::size_t> blocked(sch.blocked.begin(), sch.blocked.end());
    for (std::size_t i = 0; i < n; ++i) {
        if (excluded.count(i)) {
            out[i].state = Outcome::State::Excluded;
            out[i].note = "retir\xC3\xA9" "e du plan";
        } else if (blocked.count(i)) {
            out[i].state = Outcome::State::Failed;
            out[i].errors = sch.errors;
        }
    }
    const bool                     securityWas = pr.security.enabled;   // 1.22.6 : activee par la proposition ?
    std::map<hmi::Id, std::set<hmi::Id>> placed;   // 1.22.6 : les objets crees ou modifies, par vue (la passe des bandes)
    std::set<std::string>          plannedVars;    // les variables creees par les unites precedentes
    Run::Shared                    shared;         // 1.20.0 : les adresses libres et prises, d'une unite a l'autre
    std::map<hmi::Id, std::size_t> codeOwner;      // un code cree -> l'operation qui le cree
    std::map<hmi::Id, std::string> codeLabel;
    for (const auto& unit : sch.units) {
        // 1. ce qui s'execute dans cette unite (sans ce qui depend d'une operation echouee, ignoree ou retiree)
        std::vector<std::size_t> run;
        for (const auto i : unit.ops) {
            if (stopped(out[i])) continue;
            if (only && !only->count(i)) {
                out[i].state = Outcome::State::Skipped;
                if (out[i].note.empty()) out[i].note = "non valid\xC3\xA9" "e : laiss\xC3\xA9" "e de c\xC3\xB4t\xC3\xA9";
                continue;
            }
            std::string cause;
            for (const auto d : sch.deps[i])
                if (d < n && stopped(out[d])) {
                    cause = opLabel(p, d) + (out[d].state == Outcome::State::Excluded ? " (retir\xC3\xA9" "e)" : out[d].state == Outcome::State::Failed
                                                                                                                ? " (\xC3\xA9" "chou\xC3\xA9" "e)"
                                                                                                                : " (ignor\xC3\xA9" "e)");
                    break;
                }
            if (!cause.empty()) {
                out[i].state = Outcome::State::Skipped;
                out[i].note = "d\xC3\xA9pend de " + cause;
                continue;
            }
            run.push_back(i);
        }
        if (run.empty()) continue;
        // 1.21.0 : UNE SUPPRESSION D'ELEMENTS DU PROJET (AiDelete) - sur `pr`, la copie qui grandit d'unite en unite :
        // l'analyse, la strategie, le retrait, la verification de l'operation ; refusee : `pr` est inchange.
        if (unit.kind == plan::Unit::Kind::Project && run.size() == 1 && del::projectRemoval(p.ops[run.front()])) {
            const std::size_t i = run.front();
            del::Request rq;
            del::Analysis an;
            std::string bad;
            bool ok = del::requestOf(p.ops[i], rq, &bad);
            if (!ok) {
                an.title = opLabel(p, i);
                an.errors.push_back(bad);
            } else {
                del::Context ctx;
                ctx.plcName = env.plcName;
                ctx.global = false;   // une fois pour toute la proposition, a la fin (validate, applyStaged)
                ctx.also = alsoFor(i);
                ok = del::run(pr, rq, an, ctx);
            }
            if (mode == Mode::Apply && !ok) {
                if (why) *why = an.errors.empty() ? std::string("La suppression ne s'applique pas.") : an.errors.front();
                return false;
            }
            Outcome& o = out[i];
            o.errors = an.errors;
            o.warnings = an.warnings;
            o.state = ok ? Outcome::State::Validated : Outcome::State::Failed;
            if (ok && log) {
                log->project = true;
                for (const auto& it : an.removed) log->removed.emplace_back(i, it);
                for (const auto& d : an.direct) log->touched.push_back(d.name);
            }
            if (ok) o.made.insert(o.made.end(), an.made.begin(), an.made.end());   // 1.22.7 : une vue, le poste, un groupe regles
            if (check) {
                for (const auto& e : an.errors) check->errors.push_back(e);
                for (const auto& w : an.warnings) check->warnings.push_back(w);
                if (ok) check->made.insert(check->made.end(), an.made.begin(), an.made.end());
                check->removals.push_back(del::removalOf(i, an, p.ops[i].keep));
                check->remove += static_cast<int>(an.removed.size());
                ++check->projectRemovals;
                check->confirmRemoval = true;   // un element du projet part : toujours une confirmation
                for (const auto& ed : an.objects) {
                    if (ed.removed) continue;   // une instance retiree : dans la liste de la suppression
                    Item item;
                    item.what = Item::What::Modify;
                    item.id = ed.object;
                    item.name = ed.name;
                    item.kind = ed.kind;
                    item.changes = ed.changes;
                    item.view = ed.viewName;
                    check->spots.push_back({i, ed.viewName, ed.object, ed.name, {}, false});   // 1.22.4 : l'etape touche cet objet
                    check->items.push_back(std::move(item));
                    ++check->modify;
                }
            }
            if (tlProgress) tlProgress->fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        const bool newView = unit.kind == plan::Unit::Kind::NewView;
        if (newView && p.ops[run.front()].action != Operation::Action::View) {
            for (const auto i : run) {
                out[i].state = Outcome::State::Skipped;
                out[i].note = "sa vue " + unit.target + " n'est pas cr\xC3\xA9\xC3\xA9" "e";
            }
            continue;
        }
        // 2. la sous-proposition de l'unite (le meme moteur que la 1.18.0)
        Proposal sub;
        sub.id = p.id;
        sub.request = p.request;
        sub.summary = p.summary;
        sub.variable = p.variable;
        sub.choices = p.choices;   // 1.22.1 : les corrections
        for (const auto i : run) sub.ops.push_back(p.ops[i]);
        hmi::View work;
        hmi::Id existing = hmi::kNoId;
        if (newView) {
            work = newViewFor(pr, sub.ops.front(), pr.nextId);
        } else if (unit.kind == plan::Unit::Kind::ExistingView) {
            const hmi::View* v = unit.target.empty() ? pr.view(openView) : pr.viewByName(unit.target);
            if (!v) {
                const std::string text = "La vue \xC2\xAB " + (unit.target.empty() ? p.view : unit.target) + " \xC2\xBB n'existe pas : ses objets ne peuvent pas s'appliquer.";
                if (mode == Mode::Apply) {
                    if (why) *why = text;
                    return false;
                }
                for (const auto i : run) {
                    out[i].state = Outcome::State::Failed;
                    out[i].errors.push_back(text);
                }
                if (check) check->errors.push_back(text);
                continue;
            }
            existing = v->id;
            work = *v;
            sub.view = v->name;
            sub.viewId = v->id;
        }
        Check unitCheck;
        Touched* t = touched && (newView || existing != hmi::kNoId) ? &(*touched)[newView ? work.id : existing] : nullptr;
        Run r{pr, work, pr.nextId, sub, &env, mode == Mode::Check ? &unitCheck : nullptr, t, mode == Mode::Preview};
        r.preview = mode == Mode::Preview;
        r.deferCompile = true;
        r.plannedVars = &plannedVars;
        r.shared = &shared;
        r.whole = &p;   // 1.22.1 : les << ranger >> de variables de toute la proposition
        r.edit = &pr;   // 1.22.6 : les options des variables, la securite, les vues se posent sur le projet de travail
        const bool ok = r.go();
        if (mode == Mode::Apply && !ok) {
            if (why) *why = r.why.empty() ? std::string("La proposition ne s'applique pas.") : r.why;
            return false;
        }
        // 1.21.0 : les objets supprimes (la verification d'apres les cherche) et, a la validation, leur analyse pour
        // l'apercu - ce qui en depend, ce qui devient orphelin ; la variable d'un objet, le symbole d'une instance restent.
        if (existing != hmi::kNoId) {
            std::size_t owner = run.front();
            for (const auto i : run)
                if (p.ops[i].action == Operation::Action::Remove) owner = i;
            if (log)
                if (const hmi::View* v = pr.view(existing))
                    for (const auto id : r.removedIds)
                        if (const hmi::Object* o = v->object(id)) {
                            del::Item it;
                            it.kind = hmi::rel::Kind::Object;
                            it.name = o->name;
                            it.id = id;
                            it.view = existing;
                            it.viewName = v->name;
                            log->removed.emplace_back(owner, std::move(it));
                        }
            if (check)
                for (const auto i : run) {
                    if (p.ops[i].action != Operation::Action::Remove) continue;
                    del::Request rq;
                    if (!del::requestOf(p.ops[i], rq, nullptr, work.name)) continue;
                    del::Context ctx;
                    ctx.plcName = env.plcName;
                    ctx.global = false;
                    ctx.also = alsoFor(i);
                    const del::Analysis an = del::analyze(pr, rq, ctx);
                    if (!an.removed.empty()) check->removals.push_back(del::removalOf(i, an, p.ops[i].keep));
                }
        }
        if (newView || existing != hmi::kNoId) {   // 1.22.6 : ce que l'unite pose dans la vue (la passe des bandes)
            auto& set = placed[newView ? work.id : existing];
            set.insert(r.createdIds.begin(), r.createdIds.end());
            for (const auto id : r.modifiedIds)
                if (!r.removedIds.count(id)) set.insert(id);
        }
        if (tlProgress) tlProgress->fetch_add(run.size(), std::memory_order_relaxed);   // 1.20.0 : l'avancement
        // 3. l'etat de chaque operation (ce qu'elle a dit)
        for (const auto& sp : r.spans) {
            if (sp.op >= run.size()) continue;
            Outcome& o = out[run[sp.op]];
            for (std::size_t k = sp.e0; k < sp.e1 && k < unitCheck.errors.size(); ++k) o.errors.push_back(unitCheck.errors[k]);
            for (std::size_t k = sp.w0; k < sp.w1 && k < unitCheck.warnings.size(); ++k) o.warnings.push_back(unitCheck.warnings[k]);
            for (std::size_t k = sp.m0; k < sp.m1 && k < unitCheck.made.size(); ++k) o.made.push_back(unitCheck.made[k]);
            for (std::size_t k = sp.r0; k < sp.r1 && k < r.reusedItems.size(); ++k) o.reused.push_back(r.reusedItems[k]);
            o.state = o.errors.empty() ? Outcome::State::Validated : Outcome::State::Failed;
        }
        // ce que l'unite a dit en plus (apres ses operations : les objets sans variable...) : a la derniere
        if (!run.empty() && !r.spans.empty()) {
            Outcome& last = out[run[r.spans.back().op < run.size() ? r.spans.back().op : run.size() - 1]];
            for (std::size_t k = r.spans.back().w1; k < unitCheck.warnings.size(); ++k) last.warnings.push_back(unitCheck.warnings[k]);
            for (std::size_t k = r.spans.back().e1; k < unitCheck.errors.size(); ++k) {
                last.errors.push_back(unitCheck.errors[k]);
                last.state = Outcome::State::Failed;
            }
        }
        if (check) {
            for (auto& it : unitCheck.items) {
                it.view = work.name;
                check->items.push_back(std::move(it));
            }
            for (auto& x : unitCheck.warnings) check->warnings.push_back(std::move(x));
            for (auto& x : unitCheck.errors) check->errors.push_back(std::move(x));
            for (auto& x : unitCheck.bindings) check->bindings.push_back(std::move(x));
            check->create += unitCheck.create;
            check->modify += unitCheck.modify;
            check->remove += unitCheck.remove;
            check->confirmRemoval = check->confirmRemoval || unitCheck.confirmRemoval;
            if (check->missingVariable.empty() && !unitCheck.missingVariable.empty()) {
                check->missingVariable = unitCheck.missingVariable;
                check->nearVariables = unitCheck.nearVariables;
            }
            for (auto& x : unitCheck.newVariables) check->newVariables.push_back(std::move(x));
            for (auto& x : unitCheck.newTypes) check->newTypes.push_back(std::move(x));
            for (auto& x : unitCheck.made) check->made.push_back(std::move(x));
            for (auto& x : unitCheck.detections) check->detections.push_back(std::move(x));
            for (auto& x : unitCheck.reused) check->reused.push_back(std::move(x));
            for (auto& x : unitCheck.faults) {   // 1.22.1 : l'operation de la proposition (pas son rang dans l'unite)
                x.op = x.op < run.size() ? run[x.op] : Check::Fault::kNoOp;
                check->faults.push_back(std::move(x));
            }
            for (auto& x : unitCheck.spots) {    // 1.22.4 : de meme pour les objets en cause
                x.op = x.op < run.size() ? run[x.op] : Check::Fault::kNoOp;
                check->spots.push_back(std::move(x));
            }
        }
        // 4. poser ce que l'unite cree (la suite du plan le voit)
        if (unit.kind == plan::Unit::Kind::Project && run.size() == 1) {
            for (const auto& sc : r.newScripts) {
                codeOwner[sc.id] = run.front();
                codeLabel[sc.id] = "Script " + sc.name;
            }
            for (const auto& fn : r.newFunctions) {
                codeOwner[fn.id] = run.front();
                codeLabel[fn.id] = "Fonction " + fn.name;
            }
            for (const auto& al : r.newAlarms) {
                codeOwner[al.id] = run.front();
                codeLabel[al.id] = "Alarme " + al.name;
            }
        }
        for (const auto& v : r.newVars) plannedVars.insert(lower(v.name));
        commitRun(r, pr);
        if (newView) {
            if (out[run.front()].state == Outcome::State::Failed) {
                // la vue elle-meme est refusee (un nom pris...) : ses objets ne peuvent pas etre poses
                for (std::size_t k = 1; k < run.size(); ++k)
                    if (out[run[k]].state != Outcome::State::Failed) {
                        out[run[k]].state = Outcome::State::Skipped;
                        out[run[k]].note = "sa vue " + unit.target + " est refus\xC3\xA9" "e";
                    }
            } else {
                // La vue de demarrage : une vue ordinaire, s'il n'y en a pas (comme "Nouvelle vue") - jamais un modele ni une popup.
                if (pr.config.startView == hmi::kNoId && work.role == "vue") pr.config.startView = work.id;
                pr.views.push_back(std::move(work));
            }
        } else if (existing != hmi::kNoId) {
            if (hmi::View* target = pr.view(existing)) *target = std::move(work);
        }
        r.applyFolders(pr);
    }
    // 1.22.6 : les bandes d'en-tete et de pied (apres tout le plan : les objets crees ou deplaces compris) ; la securite activee
    // sans personne pour l'administrer.
    bandPass(pr, p, check, out, touched, placed);
    if (!securityPass(pr, p, mode, check, out, securityWas, why)) return false;
    // 5. le code cree, compile une fois TOUT cree (deux fonctions qui s'appellent, un script et sa fonction)
    if (mode != Mode::Apply && !codeOwner.empty()) {
        hmi::CompileFocus focus;
        for (const auto& [id, op] : codeOwner) {
            focus.scripts.insert(id);
            focus.functions.insert(id);
            focus.alarms.insert(id);
        }
        const hmi::NameExists plc = env.plcName ? hmi::NameExists(env.plcName) : hmi::NameExists([](std::string_view) { return false; });
        for (const auto& is : hmi::compileWith(pr, plc, hmi::exprcheck::PlcPaths{}, focus)) {
            hmi::Id id = hmi::kNoId;
            if (codeOwner.count(is.script)) id = is.script;
            else if (codeOwner.count(is.item)) id = is.item;
            if (id == hmi::kNoId) continue;
            const std::string text = codeLabel[id] + (is.line > 0 ? ", ligne " + std::to_string(is.line) : std::string{}) + " : " + is.message;
            Outcome& o = out[codeOwner[id]];
            if (is.severity == hmi::Issue::Severity::Error) {
                o.errors.push_back(text);
                o.state = Outcome::State::Failed;
                if (check) check->errors.push_back(text);
            } else if (is.severity == hmi::Issue::Severity::Warning) {
                o.warnings.push_back(text);
                if (check) check->warnings.push_back(text);
            }
        }
    }
    return true;
}

// 1.22.1 : LES DEFAUTS STRUCTURES - chaque erreur qui n'en a pas encore un (une variable introuvable en a un, fait par le
// moteur) : celle d'une operation (son indice ; "code" pour le code brut), sinon de la proposition entiere.
void structureFaults(Check& check, const Proposal& p) {
    std::set<std::pair<std::size_t, std::string>> known;
    std::set<std::string>                         said;
    for (const auto& f : check.faults) {
        known.emplace(f.op, f.message);
        said.insert(f.message);
    }
    for (std::size_t i = 0; i < check.outcomes.size(); ++i)
        for (const auto& e : check.outcomes[i].errors) {
            if (!known.emplace(i, e).second) continue;
            Check::Fault f;
            f.op = i;
            f.kind = i < p.ops.size() && p.ops[i].action == Operation::Action::Code ? "code" : "operation";
            f.message = e;
            check.faults.push_back(std::move(f));
            said.insert(e);
        }
    for (const auto& e : check.errors) {
        if (!said.insert(e).second) continue;
        Check::Fault f;
        f.kind = check.stale || check.viewMissing ? "perimee" : "proposition";
        f.message = e;
        check.faults.push_back(std::move(f));
    }
}

// 1.22.6 : CE QUI REND UNE PROPOSITION SENSIBLE, lu sur le projet d'avant et celui d'apres (la copie de la validation) : la
// securite desactivee, le dernier compte actif qui administre, le dernier groupe qui a Administrer. Vide : rien.
std::string sensitiveText(const hmi::Project& before, const hmi::Project& after) {
    std::string why;
    if (before.security.enabled && !after.security.enabled)
        why = "la proposition D\xC3\x89" "SACTIVE la s\xC3\xA9" "curit\xC3\xA9 : en marche, tout sera permis \xC3\xA0 tout le monde (niveaux et permissions ignor\xC3\xA9s)";
    else if (const auto was = app::projedit::administrators(before); !was.empty() && app::projedit::administrators(after).empty())
        why = "la proposition retire le dernier compte actif qui peut administrer (" + joinList(was) + ") : plus personne ne pourra g\xC3\xA9rer les comptes en marche";
    else if (const auto groups = app::projedit::adminGroups(before); !groups.empty() && app::projedit::adminGroups(after).empty())
        why = "la proposition retire la permission Administrer au dernier groupe qui l'avait (" + joinList(groups) + ")";
    if (why.empty()) return {};
    return std::string(kSensitive) + " : " + why + ". Le mode Direct ne l'applique pas : v\xC3\xA9rifie l'aper\xC3\xA7u, puis clique Appliquer.";
}

// La vue ouverte d'une proposition (kNoId : elle n'en touche pas, ou plus).
hmi::Id openViewOf(const hmi::Project& project, const Proposal& p) {
    if (!usesOpenView(p)) return hmi::kNoId;
    const hmi::View* v = targetView(project, p);
    return v ? v->id : hmi::kNoId;
}

} // namespace

namespace {

// Le code appelle-t-il une fonction de fichiers ? Un identifiant entier, sans la casse : FILE_*, DIR_*, RESOURCE_EXTRACT.
// Prudent : un nom dans un commentaire ou une chaine compte aussi (la proposition attend alors un clic, rien de plus).
bool callsFileFunctions(std::string_view code) {
    const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    for (std::size_t i = 0; i < code.size();) {
        if (!word(code[i]) || (i > 0 && word(code[i - 1]))) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < code.size() && word(code[j])) ++j;
        std::string id(code.substr(i, j - i));
        for (auto& c : id) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (id.rfind("FILE_", 0) == 0 || id.rfind("DIR_", 0) == 0 || id == "RESOURCE_EXTRACT") return true;
        i = j;
    }
    return false;
}

// Un evenement de script qui part seul (sans appel) : Demarrage, Cyclique, Changement - les memes mots que creer_script.
bool selfStarting(std::string_view event) {
    const std::string l = lower(event);
    return l == "demarrage" || l == "d\xC3\xA9marrage" || l == "au demarrage" || l == "startup" || l == "cyclique" || l == "cycle"
           || l == "periodique" || l == "changement" || l == "sur changement";
}

} // namespace

std::string sensitiveReason(const Proposal& p) {
    for (const auto& op : p.ops) {
        if (std::find(p.excluded.begin(), p.excluded.end(), static_cast<std::size_t>(&op - p.ops.data())) != p.excluded.end()) continue;
        const auto has = [&](const char* k) { return std::any_of(op.props.begin(), op.props.end(), [&](const auto& kv) { return kv.first == k; }); };
        const std::string body = Run::prop(op, "corps").empty() ? op.code : Run::prop(op, "corps");
        switch (op.action) {
            case Operation::Action::Security:
                if (has("active") && !boolWord(Run::prop(op, "active")).value_or(true)) return "la s\xC3\xA9" "curit\xC3\xA9, les comptes";   // la desactiver
                break;
            case Operation::Action::UserEdit:   // un compte desactive, change de groupe ou de protection : peut-etre le dernier qui administre
                if ((has("actif") && !boolWord(Run::prop(op, "actif")).value_or(true)) || has("groupe") || has("protection"))
                    return "la s\xC3\xA9" "curit\xC3\xA9, les comptes";
                break;
            case Operation::Action::GroupEdit:  // les roles d'un groupe : peut-etre le dernier qui a Administrer
                if (has("roles")) return "la s\xC3\xA9" "curit\xC3\xA9, les comptes";
                break;
            // Ce qui touche aux fichiers du poste ou part seul en marche : toujours relu avant d'etre applique.
            case Operation::Action::Script: {
                if (callsFileFunctions(body)) return "un script qui lit ou \xC3\xA9" "crit des fichiers";
                if (selfStarting(Run::prop(op, "evenement"))) return "un script qui s'ex\xC3\xA9" "cute seul (d\xC3\xA9marrage, cyclique ou sur changement)";
                const std::string lang = lower(Run::prop(op, "langage"));
                if (lang == "c" || lang == "c++" || lang == "cpp") return "un script C ou C++";
                break;
            }
            case Operation::Action::Function:
                if (callsFileFunctions(body)) return "une fonction qui lit ou \xC3\xA9" "crit des fichiers";
                break;
            // Un equipement, une adresse : ce que l'IHM lit et ecrit sur le reseau de l'usine.
            case Operation::Action::Equipment: return "un \xC3\xA9quipement";
            case Operation::Action::Address: return "une adresse d'\xC3\xA9quipement";
            case Operation::Action::VarEdit:
                if (has("equipement") || has("adresse") || has("formule") || has("motif")) return "le lien d'une variable \xC3\xA0 un \xC3\xA9quipement";
                break;
            case Operation::Action::Remove:
                if (lower(op.kind) == "equipement" || lower(op.kind) == "\xC3\xA9quipement") return "un \xC3\xA9quipement";
                break;
            default:
                break;
        }
    }
    return {};
}

bool sensitive(const Proposal& p) { return !sensitiveReason(p).empty(); }

bool secretField(Operation::Action a, std::string_view key) { return secretKey(a, key); }

bool checkInBackground(const Proposal& p) noexcept {
    if (p.ops.size() >= kHeavyOps) return true;
    return std::any_of(p.ops.begin(), p.ops.end(), [](const Operation& op) { return op.action == Operation::Action::Address; });
}

Check validate(const hmi::Project& project, const Proposal& p, const Env& env) {
    Check check;
    const hmi::Id open = openViewOf(project, p);
    if (usesOpenView(p) && open == hmi::kNoId) {
        check.viewMissing = true;
        check.errors.push_back("La vue \xC2\xAB " + p.view + " \xC2\xBB n'existe plus dans le projet : rien ne peut s'appliquer.");
        structureFaults(check, p);   // 1.22.1
        return check;
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
    const plan::Schedule sch = plan::schedule(p);
    hmi::Project copy = project;
    std::vector<Check::Outcome> out;
    RemovalLog log;
    (void)runStaged(copy, p, env, Mode::Check, &check, out, sch, nullptr, nullptr, nullptr, open, &log);
    for (const auto& e : sch.errors) check.errors.push_back(e);
    // 1.21.0 : LA VERIFICATION D'APRES SUPPRESSION, sur la copie : chaque element parti est absent, aucune reference ne
    // reste, la verification globale ne trouve aucune erreur bloquante nouvelle qui le cite. L'etape fautive echoue.
    if (log.project)
        for (const auto& pb : del::verify(project, copy, log.items(), log.touched, env.plcName)) {
            const std::string text = "V\xC3\xA9rification apr\xC3\xA8s suppression (sur une copie) : " + pb + ".";
            check.errors.push_back(text);
            std::size_t owner = log.removed.back().first;
            for (const auto& [i, it] : log.removed)
                if (!it.name.empty() && pb.find(it.name) != std::string::npos) {
                    owner = i;
                    break;
                }
            if (owner < out.size()) {
                out[owner].errors.push_back(text);
                out[owner].state = Check::Outcome::State::Failed;
            }
        }
    check.outcomes = std::move(out);
    check.order = sch.order();
    check.deps = sch.deps;
    check.planErrors = sch.errors;
    check.newView = createsView(p) ? newViewName(p) : std::string{};
    // 1.22.6 : une proposition SENSIBLE (la securite desactivee, le dernier administrateur retire) : dit EN TETE des avertissements -
    // la premiere ligne de l'apercu et de la carte.
    if (const std::string why = sensitiveText(project, copy); !why.empty()) check.warnings.insert(check.warnings.begin(), why);
    structureFaults(check, p);   // 1.22.1 : chaque erreur, son defaut structure (le clic droit)
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
// 1.22.1 : LES VARIABLES A CREER CHOISIES UNE A UNE (Proposal::choices, create) - celles que lit une operation qui s'applique
// (`allowed`) et qui n'existent pas : posees dans `pr` avant le plan, dans la meme commande (un seul Ctrl+Z), comme le choix
// unique d'avant. Jamais une variable que personne n'a demandee : seulement celles que l'utilisateur a choisi de creer.
void createChosen(hmi::Project& pr, const Proposal& p, const std::set<std::size_t>& allowed, const Env& env) {
    if (p.choices.empty()) return;
    std::set<std::string> made;
    const auto make = [&](const std::string& requested) {
        if (requested.empty() || isFormula(requested) || variableExists(pr, requested, env)) return;
        const VariableChoice* c = choiceFor(p, requested);
        if (!c || !c->create) return;
        const std::string name = trimmed(c->use).empty() ? creatableName(requested) : trimmed(c->use);
        if (!hmi::isIdentifier(name) || variableExists(pr, name, env) || !made.insert(lower(name)).second) return;
        hmi::Variable var;
        var.id = pr.allocate();
        var.name = name;
        var.type = c->type.empty() ? std::string("BOOL") : c->type;
        static const std::set<std::string> numbers = {"INT", "UINT", "WORD", "DINT", "UDINT", "DWORD", "REAL", "LREAL", "BYTE",
                                                      "SINT", "USINT", "LINT", "ULINT", "LWORD"};
        var.initial = var.type == "BOOL" ? std::string("FALSE") : numbers.count(upperOf(var.type)) ? std::string("0") : std::string{};
        var.description = "Cr\xC3\xA9\xC3\xA9" "e par l'Assistant IA (demande : " + p.request + ")";
        pr.programs.variables.push_back(std::move(var));
    };
    for (const auto i : allowed) {
        if (i >= p.ops.size()) continue;
        const Operation& op = p.ops[i];
        if (op.action == Operation::Action::Bind) make(trimmed(op.variable));
        if (op.action == Operation::Action::Create || op.action == Operation::Action::Modify)
            for (const auto& [k, v] : op.props)
                if (k == "variable") make(trimmed(v));
    }
}

// Applique la proposition a `pr` (le projet de travail) : la validation d'abord (sur `pr` tel qu'il est), puis le plan par
// unites ; faux et `why` : rien n'est change (une unite qui echoue : `pr` est remis tel qu'il etait). `skipped` non nul :
// malgre les erreurs (ce qui est valide s'applique, le reste y est note).
bool applyStaged(hmi::Project& pr, const Proposal& p, const Env& env, std::string* why, std::vector<std::string>* skipped) {
    const Check check = validate(pr, p, env);
    const bool lenient = skipped != nullptr && !check.stale && !check.viewMissing;
    if (!check.ok() && !lenient) {
        if (why) *why = check.errors.front();
        return false;
    }
    if (skipped) *skipped = check.errors;
    // Les operations a executer : celles que la validation a validees (le reste : retire, echoue, ou qui en depend).
    std::set<std::size_t> allowed;
    for (std::size_t i = 0; i < check.outcomes.size(); ++i)
        if (check.outcomes[i].state == Check::Outcome::State::Validated) allowed.insert(i);
    if (allowed.empty()) {
        if (why) *why = check.errors.empty() ? std::string("Rien \xC3\xA0 appliquer : toutes les op\xC3\xA9rations sont retir\xC3\xA9" "es.") : check.errors.front();
        return false;
    }
    const hmi::Id open = openViewOf(pr, p);
    // Une variable a creer (choisie par l'utilisateur) : le projet ET la vue, dans une seule commande.
    std::string createVar;
    if (p.variable.create && p.variable.use.empty())
        for (const auto& name : requestedVariables(p))
            if (!variableExists(pr, name, env)) createVar = name;
    const plan::Schedule sch = plan::schedule(p);
    std::string failed;
    const hmi::Project backup = pr;
    if (!createVar.empty()) {
        hmi::Variable var;
        var.id = pr.allocate();
        var.name = createVar;
        var.type = p.variable.type.empty() ? "BOOL" : p.variable.type;
        var.initial = var.type == "BOOL" ? "FALSE" : "0";
        var.description = "Cr\xC3\xA9\xC3\xA9" "e par l'Assistant IA (demande : " + p.request + ")";
        pr.programs.variables.push_back(std::move(var));
    }
    createChosen(pr, p, allowed, env);   // 1.22.1 : les corrections (une variable a creer, choisie une a une)
    std::vector<Check::Outcome> out;
    RemovalLog log;
    bool ok = runStaged(pr, p, env, Mode::Apply, nullptr, out, sch, &allowed, &failed, nullptr, open, &log);
    // 1.21.0 : une suppression est VERIFIEE avant la commande (sur ce projet de travail, une copie) : absente, aucune
    // reference restante - sinon rien n'est applique (la verification globale : faite par la validation juste au-dessus,
    // sur le meme projet).
    if (ok && log.project) {
        const auto problems = del::verify(backup, pr, log.items(), log.touched, env.plcName, false);
        if (!problems.empty()) {
            ok = false;
            failed = "Suppression refus\xC3\xA9" "e \xC3\xA0 la v\xC3\xA9rification : " + problems.front()
                     + (problems.size() > 1 ? " (et " + std::to_string(problems.size() - 1) + " autres)." : std::string("."));
        }
    }
    if (!ok) {
        if (failed.empty()) failed = "La proposition ne s'applique pas.";
        failed += " Le projet est rest\xC3\xA9 tel qu'il \xC3\xA9tait (rien n'a \xC3\xA9t\xC3\xA9 appliqu\xC3\xA9).";
        const hmi::Id keep = pr.nextId;
        pr = backup;
        pr.nextId = std::max(pr.nextId, keep);
        if (why) *why = failed;
        return false;
    }
    // 1.23.0 : un style nomme pose par l'IA (namedStyle) s'applique comme dans l'inspecteur : ses valeurs imposees, la
    // valeur d'avant retenue - pour chaque objet dont le style cite a change (ou neuf, sans etat de style).
    for (auto& v : pr.views)
        for (auto& o : v.objects) {
            const std::string now = hmi::styles::citedStyle(o);
            if (now.empty()) continue;
            const hmi::View* bv = backup.view(v.id);
            const hmi::Object* bo = bv ? bv->object(o.id) : nullptr;
            if (bo && hmi::styles::citedStyle(*bo) == now) continue;
            if (const hmi::Style* st = hmi::styles::find(pr, now)) (void)hmi::styles::apply(pr, o, *st);
        }
    return true;
}

std::string commandLabel(const Proposal& p) { return "Assistant IA : " + (p.summary.empty() ? std::string("proposition") : p.summary); }

core::CommandPtr commandFor(const hmi::DocumentPtr& doc, const Proposal& p, std::string* why, const Env& env, std::vector<std::string>* skipped) {
    if (!doc) {
        if (why) *why = "Pas de projet IHM ouvert.";
        return nullptr;
    }
    // 1.19.0 : UNE commande pour tout le plan (la vue neuve et ses objets, les autres vues, les variables, les types, les
    // outils) : Ctrl+Z reprend tout, Ctrl+Y le refait. Une unite qui echoue : le projet est remis tel qu'il etait.
    std::string failed;
    // 1.26.1 : ce que la proposition retire du projet (une suppression confirmee) entre dans sa corbeille.
    core::CommandPtr cmd = hmi::deleteFromProject(doc, commandLabel(p), [&](hmi::Project& pr) {
        if (!applyStaged(pr, p, env, &failed, skipped) && failed.empty()) failed = "La proposition ne s'applique pas.";
    });
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

std::shared_ptr<hmi::Project> appliedCopy(const hmi::Project& project, const Proposal& p, const Env& env, std::string* why,
                                          std::vector<std::string>* skipped) {
    auto copy = std::make_shared<hmi::Project>(project);
    if (!applyStaged(*copy, p, env, why, skipped)) return nullptr;
    return copy;
}

core::CommandPtr commandSetting(const hmi::DocumentPtr& doc, const Proposal& p, std::shared_ptr<const hmi::Project> after, std::string* why) {
    if (!doc || !after) {
        if (why) *why = "Pas de projet IHM ouvert.";
        return nullptr;
    }
    core::CommandPtr cmd = hmi::deleteFromProject(doc, commandLabel(p), [after](hmi::Project& pr) { pr = *after; });   // 1.26.1
    if (!cmd && why) *why = "La proposition ne change rien au projet.";
    return cmd;
}

Preview preview(const hmi::Project& project, const Proposal& p, const Env& env) {
    Preview out;
    const hmi::Id open = openViewOf(project, p);
    // 1.22.6 : modifier_vue - la vue qu'elle change (les objets que son en-tete ou son pied recouvre peuvent y bouger).
    std::string edited;
    for (const auto& op : p.ops)
        if (op.action == Operation::Action::ViewEdit && edited.empty()) edited = trimmed(op.name);
    if (!createsView(p) && !usesOpenView(p) && edited.empty()) {
        // Ni vue ni objet : des variables, des types (l'apercu de la carte le dit, la liste les montre).
        const bool removes = std::any_of(p.ops.begin(), p.ops.end(), [](const Operation& op) { return del::projectRemoval(op); });
        const std::string made = creationSummary(p);
        out.why = "Rien \xC3\xA0 dessiner : la proposition " + (made.empty() ? std::string{} : "cr\xC3\xA9" "e " + made + (removes ? " et " : ""))
                  + (removes ? std::string("supprime des \xC3\xA9l\xC3\xA9ments du projet") : std::string{})
                  + " (la liste de l'aper\xC3\xA7u les montre" + (removes ? std::string(" : Suppressions") : std::string{}) + ").";
        return out;
    }
    if (usesOpenView(p) && open == hmi::kNoId && !createsView(p)) {
        out.why = "La vue \xC2\xAB " + p.view + " \xC2\xBB n'existe plus.";
        return out;
    }
    // Tolerant : une operation impossible (une variable introuvable) n'empeche pas de voir le reste.
    hmi::Project copy = project;
    std::vector<Check::Outcome> outcomes;
    std::map<hmi::Id, Touched> touched;
    (void)runStaged(copy, p, env, Mode::Preview, nullptr, outcomes, plan::schedule(p), nullptr, nullptr, &touched, open);
    // La vue montree : la vue neuve principale, sinon la vue ouverte.
    const hmi::View* shown = createsView(p) ? copy.viewByName(newViewName(p)) : nullptr;
    if (!shown && open != hmi::kNoId) shown = copy.view(open);
    if (!shown && !edited.empty()) shown = copy.viewByName(edited);   // 1.22.6
    if (!shown) {
        out.why = "La vue \xC2\xAB " + (createsView(p) ? newViewName(p) : p.view) + " \xC2\xBB ne peut pas \xC3\xAA" "tre dessin\xC3\xA9" "e (refus\xC3\xA9" "e).";
        return out;
    }
    out.view = *shown;
    if (auto it = touched.find(shown->id); it != touched.end()) out.touched = it->second;
    out.ok = true;
    return out;
}

// ---- 1.22.6 : toutes les vues de la proposition -------------------------------------------------------------------------
std::vector<ViewPreview> previews(const hmi::Project& project, const Proposal& p, const Env& env) {
    const hmi::Id open = openViewOf(project, p);
    const bool views = createsView(p) || usesOpenView(p);
    const bool removes = std::any_of(p.ops.begin(), p.ops.end(), [](const Operation& op) { return del::projectRemoval(op); });
    // Rien a dessiner (le projet seul ; la vue ouverte disparue) : la reponse de preview, telle quelle (elle ne copie rien).
    // Une suppression du projet peut toutefois modifier des objets d'autres vues : celles-la se montrent.
    if ((!views && !removes) || (usesOpenView(p) && open == hmi::kNoId && !createsView(p))) return {ViewPreview{preview(project, p, env), false}};
    hmi::Project copy = project;
    std::vector<Check::Outcome> outcomes;
    std::map<hmi::Id, Touched> touched;
    (void)runStaged(copy, p, env, Mode::Preview, nullptr, outcomes, plan::schedule(p), nullptr, nullptr, &touched, open);
    // La vue principale : celle de preview (la vue neuve principale, sinon la vue ouverte).
    const hmi::View* main = nullptr;
    if (views) {
        main = createsView(p) ? copy.viewByName(newViewName(p)) : nullptr;
        if (!main && open != hmi::kNoId) main = copy.view(open);
    }
    // Les vues modifiees par une suppression du projet (aucune unite ne les a touchees) : leurs objets changes, compares a
    // ceux du projet.
    std::map<hmi::Id, Touched> changed;
    if (removes)
        for (const auto& v : copy.views) {
            if (touched.count(v.id)) continue;
            const hmi::View* before = project.view(v.id);
            if (!before || fingerprint(*before) == fingerprint(v)) continue;
            Touched t;
            for (const auto& o : v.objects) {
                const hmi::Object* was = before->object(o.id);
                if (!was) t.created.push_back(o.id);
                else if (!(*was == o)) t.modified.push_back(o.id);
            }
            changed.emplace(v.id, std::move(t));
        }
    // L'ordre : la principale, puis chaque vue dans l'ordre de la proposition (la premiere operation qui la vise ; une vue
    // dont la creation est refusee - un nom pris - n'est pas la sienne : seulement si ses objets changent), puis le reste.
    std::vector<hmi::Id> order;
    const auto shows = [&](const hmi::View* v) {
        if (!v) return false;
        if (v == main || !project.view(v->id) || changed.count(v->id)) return true;
        const auto it = touched.find(v->id);
        return it != touched.end() && (!it->second.created.empty() || !it->second.modified.empty() || !it->second.removed.empty());
    };
    const auto add = [&](const hmi::View* v) {
        if (shows(v) && std::find(order.begin(), order.end(), v->id) == order.end()) order.push_back(v->id);
    };
    add(main);
    for (std::size_t i = 0; i < p.ops.size(); ++i) {
        const Operation& op = p.ops[i];
        if (op.action == Operation::Action::View) {
            add(copy.viewByName(trimmed(op.name)));
        } else if (!projectLevel(op)) {
            const std::string target = opTarget(p, i);
            add(target.empty() ? (open != hmi::kNoId ? copy.view(open) : nullptr) : copy.viewByName(target));
        }
    }
    for (const auto& [id, t] : touched) add(copy.view(id));
    for (const auto& [id, t] : changed) add(copy.view(id));
    std::vector<ViewPreview> out;
    for (const auto id : order) {
        const hmi::View* v = copy.view(id);
        if (!v) continue;
        ViewPreview vp;
        vp.preview.ok = true;
        vp.preview.view = *v;
        if (const auto it = touched.find(id); it != touched.end()) vp.preview.touched = it->second;
        else if (const auto ch = changed.find(id); ch != changed.end()) vp.preview.touched = ch->second;
        vp.created = project.view(id) == nullptr;
        out.push_back(std::move(vp));
    }
    if (out.empty()) {
        ViewPreview none;
        if (views)
            none.preview.why = "La vue \xC2\xAB " + (createsView(p) ? newViewName(p) : p.view) + " \xC2\xBB ne peut pas \xC3\xAA" "tre dessin\xC3\xA9" "e (refus\xC3\xA9" "e).";
        else
            none.preview = preview(project, p, env);   // << Rien a dessiner... >> (sans rien recalculer)
        out.push_back(std::move(none));
    }
    return out;
}

// ---- le JSON ----------------------------------------------------------------------
// ---- 1.22.8 : les unites de mesure (creer_unite) ----------------------------------------------------------------------
UnitPlan planUnit(const hmi::Config& cfg, const Operation& op) {
    namespace hu = hmi::units;
    using F = UnitPlan::Field;
    UnitPlan out;
    const auto refuse = [&](F field, std::string why) {
        out.error = std::move(why);
        out.field = field;
        return out;
    };
    const auto propOf = [&](std::string_view key) {
        for (const auto& [k, v] : op.props)
            if (k == key) return trimmed(v);
        return std::string{};
    };
    const auto shownFamily = [](const std::string& label, const std::string& name) {
        std::string t = label.empty() ? name : label;
        if (t.size() >= 2 && static_cast<unsigned char>(t[0]) == 0xC3 && static_cast<unsigned char>(t[1]) >= 0x80 && static_cast<unsigned char>(t[1]) <= 0x9E)
            t[1] = static_cast<char>(static_cast<unsigned char>(t[1]) + 0x20);   // << Energie >> (E accent aigu) : sa minuscule
        else if (!t.empty()) t[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(t[0])));
        return t;
    };
    const std::string symbol = trimmed(op.name);
    if (symbol.empty()) return refuse(F::Symbol, "le symbole manque (symbole : mmCE)");
    double factor = 1.0, offset = 0.0;
    const std::string factorText = propOf("facteur"), offsetText = propOf("decalage");
    if (!factorText.empty() && !hu::parseValue(factorText, factor)) return refuse(F::Factor, "facteur \xC2\xAB " + factorText + " \xC2\xBB : un nombre (9.80665)");
    if (!offsetText.empty() && !hu::parseValue(offsetText, offset)) return refuse(F::Offset, "d\xC3\xA9" "calage \xC2\xAB " + offsetText + " \xC2\xBB : un nombre (273.15)");
    const std::string famText = propOf("famille");
    const std::string label = propOf("libelle");
    const hu::Catalog cat(cfg);
    // Resoudre la famille : une famille du catalogue (son nom, son libelle), sinon une famille NEUVE dite par sa reference.
    std::string familyName;
    if (const hmi::UnitFamily* f = cat.family(famText)) familyName = f->name;
    else familyName = hu::familyFrom(cat, famText);
    // Deja la, a l'identique (meme famille, meme conversion) : rien a poser.
    if (const hmi::UnitDef* u = cat.unit(symbol); u && !familyName.empty() && lower(u->family) == lower(familyName)
                                                   && std::fabs(u->factor - factor) <= 1e-12 * std::max(1.0, std::fabs(factor))
                                                   && std::fabs(u->offset - offset) <= 1e-12 * std::max(1.0, std::fabs(offset))) {
        out.same = true;
        out.unit = *u;
        const hmi::UnitFamily* f = cat.family(u->family);
        out.familyLabel = shownFamily(f ? f->label : std::string{}, u->family);
        out.conversion = f ? hu::formulaText(cat, u->symbol, f->reference) : std::string{};
        return out;
    }
    if (famText.empty()) return refuse(F::Family, "la famille manque (pression, temperature, debit... ; une famille neuve : son nom et sa reference)");
    // Une famille qui existe deja, dite avec une AUTRE reference : le facteur serait faux - jamais converti en silence.
    if (const std::string ref = propOf("reference"); !familyName.empty() && !ref.empty())
        if (const hmi::UnitFamily* f = cat.family(familyName); f && lower(f->reference) != lower(ref))
            return refuse(F::Reference, "la famille " + f->name + " existe d\xC3\xA9j\xC3\xA0, de r\xC3\xA9" "f\xC3\xA9rence " + f->reference + " (pas " + ref
                                            + ") : donne le facteur vers " + f->reference);
    hmi::Config probe = cfg;
    if (familyName.empty()) {
        const std::string ref = propOf("reference");
        if (ref.empty()) {
            std::string names;
            for (const auto& f : cat.families()) names += (names.empty() ? "" : ", ") + f.name;
            return refuse(F::Family, "famille \xC2\xAB " + famText + " \xC2\xBB inconnue (les familles : " + names
                                        + ") ; une famille neuve se dit avec sa r\xC3\xA9" "f\xC3\xA9rence (reference : W)");
        }
        if (const std::string bad = hu::newFamilyProblem(cat, famText, ref); !bad.empty())
            return refuse(hu::familyNameOk(famText) ? (lower(ref) == lower(symbol) ? F::Symbol : F::Reference) : F::Family, "famille neuve " + famText + " : " + bad);
        out.family = {famText, ref, propOf("libelle_famille")};
        out.familyLabel = shownFamily(out.family.label, famText);
        probe.unitFamilies.push_back(out.family);
        familyName = famText;
        if (lower(ref) == lower(symbol)) {   // l'unite EST la reference de sa famille neuve : facteur 1, decalage 0
            if (factor != 1.0 || offset != 0.0)
                return refuse(F::Factor, symbol + " est la r\xC3\xA9" "f\xC3\xA9rence de sa famille neuve : facteur 1, d\xC3\xA9" "calage 0");
            out.family.reference = symbol;
            out.unit = {symbol, famText, 1.0, 0.0, label};
            out.conversion = "r\xC3\xA9" "f\xC3\xA9rence de la famille";
            return out;
        }
        probe.units.push_back({ref, famText, 1.0, 0.0, {}});
    }
    if (factorText.empty()) {
        const hmi::UnitFamily* f = hu::Catalog(probe).family(familyName);
        return refuse(F::Factor, "le facteur manque : combien vaut 1 " + symbol + " en " + (f ? f->reference : std::string("?")) + " (la r\xC3\xA9" "f\xC3\xA9rence de "
                                     + familyName + ")");
    }
    hmi::UnitDef u{symbol, familyName, factor, offset, label};
    const hu::Catalog before(probe);
    if (const std::string bad = hu::newUnitProblem(before, u, &u.family); !bad.empty()) {
        const F field = !hu::symbolOk(symbol) || before.unit(symbol) ? F::Symbol : !std::isfinite(factor) || factor == 0.0 ? F::Factor : F::Offset;
        return refuse(field, bad);
    }
    out.unit = u;
    const hmi::UnitFamily* f = before.family(u.family);
    if (out.familyLabel.empty()) out.familyLabel = shownFamily(f ? f->label : std::string{}, u.family);
    if (const std::string w = hu::coherenceWarning(before, u); !w.empty()) out.warnings.push_back(w);
    probe.units.push_back(u);
    out.conversion = f ? hu::formulaText(hu::Catalog(probe), u.symbol, f->reference) : std::string{};
    return out;
}

Operation unitOperation(const hmi::UnitDef& u, const hmi::UnitFamily& family) {
    Operation op;
    op.action = Operation::Action::Unit;
    op.name = u.symbol;
    op.props.emplace_back("famille", u.family);
    op.props.emplace_back("facteur", hmi::units::exactNumber(u.factor));
    if (u.offset != 0.0) op.props.emplace_back("decalage", hmi::units::exactNumber(u.offset));
    if (!u.label.empty()) op.props.emplace_back("libelle", u.label);
    if (!family.name.empty()) {
        op.props.emplace_back("reference", family.reference);
        if (!family.label.empty()) op.props.emplace_back("libelle_famille", family.label);
    }
    return op;
}

std::string unitText(const UnitPlan& pl) {
    std::string fam = pl.familyLabel.empty() ? pl.unit.family : pl.familyLabel;
    if (!pl.family.name.empty()) fam += ", famille neuve, r\xC3\xA9" "f\xC3\xA9rence " + pl.family.reference;
    return "Unit\xC3\xA9 " + pl.unit.symbol + " (" + fam + ")" + (pl.conversion.empty() ? std::string{} : " : " + pl.conversion);
}

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
                    if (!v.folder.empty()) x.set("dossier", v.folder);   // 1.22.1 : son dossier
                    if (!v.unit.empty()) x.set("unite", v.unit);         // 1.22.8 : son unite
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
            // 1.22.6 : la variable (ou les variables), le nom d'un groupe ou d'un login cree, la vue visee ; le reste : proprietes.
            case Operation::Action::VarEdit:
                if (!op.variable.empty()) o.set("variable", op.variable);
                if (!op.objects.empty()) {
                    json::Value list = json::Value::array();
                    for (const auto& n : op.objects) list.push(n);
                    o.set("objets", std::move(list));
                }
                break;
            case Operation::Action::Group:
            case Operation::Action::User:
                o.set("nom", op.name);
                break;
            case Operation::Action::ViewEdit:
            case Operation::Action::StartView:
                o.set("vue", op.name);
                break;
            case Operation::Action::Security:
            case Operation::Action::GroupEdit:
            case Operation::Action::UserEdit:
                break;
            // 1.22.8 : le symbole ; la famille, le facteur... : proprietes.
            case Operation::Action::Unit:
            case Operation::Action::UnitEdit:
                o.set("symbole", op.name);
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
            for (const auto& [k, v] : op.props) {
                // 1.22.1 : creer_variables - le dossier de l'operation s'ecrit au premier niveau, comme l'IA l'envoie.
                if (op.action == Operation::Action::Variables && k == "dossier") o.set("dossier", v);
                else props.set(k, v);
            }
            if (!props.members().empty()) o.set("proprietes", std::move(props));
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
        // 1.19.0 : la vue visee par une operation sur des objets (seulement si elle est dite).
        if (!projectLevel(op) && !op.target.empty()) o.set("vue", op.target);
        if (!op.step.empty()) o.set("etape", op.step);   // 1.22.0 : l'etape de la mission
        // 1.21.0 : supprimer - le genre de ce qui part (vide : des objets, comme avant) et les exclusions.
        if (op.action == Operation::Action::Remove) {
            if (!op.kind.empty()) o.set("genre", op.kind);
            if (!op.keep.empty()) {
                json::Value keep = json::Value::array();
                for (const auto& k : op.keep) keep.push(k);
                o.set("exclure", std::move(keep));
            }
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
        // 1.21.0 : supprimer - un genre inconnu est refuse a la lecture, comme une operation inconnue.
        if (*action == Operation::Action::Remove && !del::genreFromKey(op.kind)) {
            if (why)
                *why = "Op\xC3\xA9ration refus\xC3\xA9" "e : supprimer, genre \xC2\xAB " + op.kind + " \xC2\xBB inconnu (objet, instance, vue, popup, symbole, "
                       "modele, variable, type, alarme, script, fonction, equipement, ressource, dossier).";
            return false;
        }
        const bool named = *action == Operation::Action::Create || *action == Operation::Action::View || *action == Operation::Action::Type
                           || *action == Operation::Action::Equipment || *action == Operation::Action::Script || *action == Operation::Action::Function
                           || *action == Operation::Action::Alarm || *action == Operation::Action::Resource
                           || *action == Operation::Action::Group || *action == Operation::Action::User;   // 1.22.6
        op.name = named ? o.str("nom") : o.str("objet");
        // 1.22.6 : creer_utilisateur - le login (nom, ou login) ; modifier_vue, vue_de_demarrage - la vue visee (vue, ou nom).
        if (*action == Operation::Action::User && op.name.empty()) op.name = o.str("login");
        if (*action == Operation::Action::ViewEdit || *action == Operation::Action::StartView) op.name = !o.str("vue").empty() ? o.str("vue") : o.str("nom");
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
        for (const auto& v : o.at("variables").items()) {
            op.variables.push_back({v.str("nom"), v.str("type"), v.str("description"),
                                    v.at("valeur").isString() ? v.str("valeur")
                                    : v.at("valeur").isNumber() ? hmi::formatNumber(v.at("valeur").asNumber())
                                    : v.at("valeur").isBool() ? (v.at("valeur").asBool() ? std::string("TRUE") : std::string("FALSE"))
                                                              : std::string{}});
            op.variables.back().folder = v.str("dossier");   // 1.22.1 : son dossier (absent : celui de l'operation, sinon deduit)
            op.variables.back().unit = v.str("unite");       // 1.22.8 : son unite
        }
        for (const auto& m : o.at("membres").items()) op.members.push_back({m.str("nom"), m.str("type"), m.str("description")});
        if (*action == Operation::Action::Type && o.at("description").isString() && !o.str("description").empty())
            op.props.emplace_back("description", o.str("description"));
        // 1.22.1 : creer_variables - le dossier de l'operation (pour celles de ses variables qui n'en disent pas).
        if (*action == Operation::Action::Variables && o.at("dossier").isString()
            && std::none_of(op.props.begin(), op.props.end(), [](const auto& kv) { return kv.first == "dossier"; }))
            op.props.emplace_back("dossier", o.str("dossier"));
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
        if (!projectLevel(*action)) op.target = o.str("vue");   // 1.19.0 : la vue visee (vide : comme avant)
        op.step = o.str("etape");   // 1.22.0 : l'etape de la mission (vide : toutes celles de la proposition)
        if (*action == Operation::Action::Store)
            for (const auto& n : o.at("elements").items()) op.objects.push_back(n.asString());
        // 1.21.0 : supprimer - la strategie, le remplacant, la liste d'un dossier, les devenus inutiles (aussi au premier
        // niveau), les exclusions.
        if (*action == Operation::Action::Remove) {
            for (const char* k : {"strategie", "remplacer_par", "par", "liste", "inutiles"}) {
                const json::Value& v = o.at(k);
                if (!(v.isString() || v.isNumber() || v.isBool())) continue;
                const bool already = std::any_of(op.props.begin(), op.props.end(), [&](const auto& kv) { return kv.first == k; });
                if (!already) op.props.emplace_back(k, scalar(v));
            }
            for (const auto& n : o.at("exclure").items())
                if (n.isString() && !trimmed(n.asString()).empty()) op.keep.push_back(trimmed(n.asString()));
        }
        if (projectLevel(*action) && *action != Operation::Action::Variables && *action != Operation::Action::Type) {
            static const std::set<std::string, std::less<>> reserved = {"action", "nom", "objet", "objets", "elements", "variable", "proprietes", "variables",
                                                                        "membres", "parametres", "generiques", "detecter", "genre", "noms", "dx", "dy",
                                                                        "code", "copies", "reperes", "vue", "etape"};
            for (const auto& [k, v] : o.members())
                if (!reserved.count(k) && (v.isString() || v.isNumber() || v.isBool())) {
                    const bool already = std::any_of(op.props.begin(), op.props.end(), [&](const auto& kv) { return kv.first == k; });
                    if (!already) op.props.emplace_back(k, scalar(v));
                }
        }
        // 1.22.6 : JAMAIS UN SECRET GARDE - un mot de passe, un badge, un secret (au premier niveau ou dans proprietes) est retire
        // de l'operation des sa lecture ; seul son NOM reste (secret_refuse), et l'operation est refusee avec la raison.
        for (auto it = op.props.begin(); it != op.props.end();) {
            if (secretKey(*action, it->first)) {
                const std::string key = it->first;
                it = op.props.erase(it);
                if (std::none_of(op.props.begin(), op.props.end(), [](const auto& kv) { return kv.first == "secret_refuse"; }))
                    it = op.props.insert(it, {"secret_refuse", key}) + 1;
                continue;
            }
            ++it;
        }
        // 1.22.8 : creer_unite, modifier_unite - le symbole (au premier niveau, ou dans proprietes), le facteur et le decalage
        // EXACTS : un nombre JSON garde tous ses chiffres (9.80665, pas 9.807 comme une propriete d'objet).
        if (*action == Operation::Action::Unit || *action == Operation::Action::UnitEdit) {
            if (op.name.empty()) op.name = !o.str("symbole").empty() ? o.str("symbole") : o.str("nom");
            for (auto it = op.props.begin(); it != op.props.end();) {
                if (it->first != "symbole") {
                    ++it;
                    continue;
                }
                if (op.name.empty()) op.name = it->second;
                it = op.props.erase(it);
            }
            const json::Value* inner = o.find("proprietes");
            for (const char* k : {"facteur", "decalage"}) {
                const json::Value* v = o.find(k);
                if (!(v && v->isNumber()) && inner && inner->isObject()) v = inner->find(k);
                if (v && v->isNumber())
                    for (auto& [pk, pv] : op.props)
                        if (pk == k) pv = hmi::units::exactNumber(v->asNumber());
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
    // 1.22.1 : les corrections - un choix par variable introuvable (absent : aucun ; un fichier 1.22.0 n'en a pas).
    if (!p.choices.empty()) {
        json::Value a = json::Value::array();
        for (const auto& ch : p.choices) {
            json::Value c = json::Value::object();
            c.set("de", ch.from);
            c.set("utiliser", ch.use);
            c.set("creer", ch.create);
            c.set("type", ch.type);
            a.push(std::move(c));
        }
        o.set("variablesChoisies", std::move(a));
    }
    o.set("operations", operationsToJson(p.ops));
    // 1.19.0 : les operations retirees du plan, le plan applique (ses etats) et le compte rendu reel.
    if (!p.excluded.empty()) {
        json::Value a = json::Value::array();
        for (const auto i : p.excluded) a.push(static_cast<std::int64_t>(i));
        o.set("exclues", std::move(a));
    }
    if (!p.plan.isNull()) o.set("plan", p.plan);
    if (!p.report.isNull()) o.set("compteRendu", p.report);
    if (!p.next.empty()) o.set("suite", p.next);   // 1.21.3
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
    if (const auto* ex = o.find("exclues"); ex && ex->isArray())
        for (const auto& x : ex->items())
            if (x.isNumber() && x.asInt() >= 0 && static_cast<std::size_t>(x.asInt()) < p.ops.size()) p.excluded.push_back(static_cast<std::size_t>(x.asInt()));
    if (const auto* pl = o.find("plan")) p.plan = *pl;
    if (const auto* cr = o.find("compteRendu")) p.report = *cr;
    p.next = o.str("suite");   // 1.21.3
    if (const auto* cs = o.find("variablesChoisies"); cs && cs->isArray())   // 1.22.1 : les corrections
        for (const auto& c : cs->items()) {
            if (!c.isObject()) continue;
            VariableChoice ch;
            ch.from = c.str("de");
            ch.use = c.str("utiliser");
            ch.create = c.flag("creer");
            ch.type = c.str("type", "BOOL");
            if (ch.from.empty() || (ch.use.empty() && !ch.create) || choiceFor(p, ch.from)) continue;   // illisible, ou en double
            p.choices.push_back(std::move(ch));
        }
    p.extra = json::unknownMembers(o, {"id", "etat", "creer", "modifier", "supprimer", "vue", "vueId", "empreinte", "empreinteApres", "demande", "resume",
                                       "variableChoisie", "operations", "exclues", "plan", "compteRendu", "suite", "variablesChoisies"});
    return p;
}

// ---- 1.20.0 : une verification sur un fil de fond -----------------------------------------------------
void setProgressCounter(std::atomic<std::size_t>* counter) noexcept { tlProgress = counter; }

bool frozenEnv(const Env& live, const std::map<std::string, std::string>& attachments, Env& out) {
    out = Env{};
    if (live.plcName) {
        if (!live.plcNameList) return false;
        auto names = std::make_shared<std::set<std::string>>();
        for (const auto& n : live.plcNameList()) names->insert(upperOf(n));
        out.plcName = [names](std::string_view root) { return names->count(upperOf(root)) > 0; };
        out.plcNameList = [names] { return std::vector<std::string>(names->begin(), names->end()); };
    }
    if (live.plcVariables) {
        auto vars = std::make_shared<std::vector<std::pair<std::string, std::string>>>(live.plcVariables());
        out.plcVariables = [vars] { return *vars; };
    }
    if (live.plcTypes) {
        auto types = std::make_shared<std::vector<Env::PlcType>>(live.plcTypes());
        out.plcTypes = [types] { return *types; };
    }
    if (live.plcSource) {
        const std::string source = live.plcSource();
        out.plcSource = [source] { return source; };
    }
    auto files = std::make_shared<std::map<std::string, std::string>>(attachments);
    out.attachmentPath = [files](std::string_view name) {
        const auto it = files->find(std::string(name));
        return it == files->end() ? std::string{} : it->second;
    };
    return true;
}

} // namespace app::ai
