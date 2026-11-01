// =============================================================================
//  app/hmi/HmiProjectEdits.cpp - voir HmiProjectEdits.hpp
// -----------------------------------------------------------------------------
//  1.22.6 : le corps des modifications de Configuration > Utilisateurs
//  (HmiUsersPane), des liens d'une vue (HmiEditor) et de la valeur initiale /
//  des membres internes d'une variable (HmiVariablesPane), tels qu'ils etaient
//  dans ces volets - deplaces ici sans rien changer, pour que l'Assistant IA
//  passe par le meme code.
// =============================================================================
#include "HmiProjectEdits.hpp"

#include "HmiPaneKit.hpp"
#include "../../sim/ValueText.hpp"
#include "../../hmi/HmiCrypto.hpp"     // randomHex : le secret d'un code dynamique
#include "../../hmi/HmiEnums.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiHistory.hpp"    // wallStamp : la date d'un mot de passe
#include "../../hmi/HmiPolicy.hpp"
#include "../../hmi/HmiTemplates.hpp"
#include "../../hmi/HmiTypeForms.hpp"
#include "../../hmi/HmiTypes.hpp"

#include <algorithm>
#include <cctype>

namespace app::projedit {

using namespace hmikit;
namespace ty = hmi::types;

namespace {

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

// 1.11.8 : le meme membre dans toutes les cases : chaque indice devient [*] ("[0].NOM" -> "[*].NOM").
std::string allElementsOf(std::string_view rel) {
    std::string out;
    for (std::size_t i = 0; i < rel.size(); ++i) {
        if (rel[i] != '[') {
            out += rel[i];
            continue;
        }
        const auto close = rel.find(']', i);
        if (close == std::string_view::npos) {
            out += rel.substr(i);
            break;
        }
        out += "[*]";
        i = close;
    }
    return out;
}

// Le compte peut-il se connecter (sa protection a ce qu'il faut) ?
bool canLogIn(const hmi::User& u) {
    if (u.protection == "classique") return !u.passwordHash.empty();
    if (u.protection == "dynamique") return !u.secret.empty();
    if (u.protection == "expression") return !u.expression.empty();
    return false;
}

} // namespace

// ---- la securite ---------------------------------------------------------------------------------
const std::vector<std::string>& securityFields() {
    static const std::vector<std::string> all = {"active", "depart", "deconnexion", "menu_comptes", "menu_acces", "menu_journal", "periode",
                                                 "chiffres", "mdp_longueur", "mdp_chiffre", "mdp_lettre", "mdp_casse", "mdp_special", "mdp_duree",
                                                 "mdp_historique", "mdp_premiere", "verrou_essais", "verrou_minutes", "avertir", "badge"};
    return all;
}

bool setSecurityField(hmi::Project& p, const std::string& field, const std::string& value, std::string& err) {
    auto& sec = p.security;
    double n = 0;
    if (field == "active") { sec.enabled = yes(value); return true; }
    // Lot 13 : les regles cochees.
    if (field == "mdp_chiffre") { sec.pwDigit = yes(value); return true; }
    if (field == "mdp_lettre") { sec.pwLetter = yes(value); return true; }
    if (field == "mdp_casse") { sec.pwMixedCase = yes(value); return true; }
    if (field == "mdp_special") { sec.pwSpecial = yes(value); return true; }
    if (field == "mdp_premiere") { sec.pwChangeFirst = yes(value); return true; }
    if (field == "badge") { sec.badgeLogin = yes(value); return true; }
    if (field == "depart") {
        if (!value.empty() && !p.userByLogin(value)) { err = "utilisateur inconnu : " + value; return false; }
        sec.startUser = value.empty() ? std::string{} : p.userByLogin(value)->login;
        return true;
    }
    // 1.12.19 : un nombre entier, lu comme partout (1 000 ; 12.7 refuse, plus tronque en 12).
    long long whole = 0;
    if (!sim::readInteger(value, whole)) { err = field + " : un nombre entier"; return false; }
    n = static_cast<double>(whole);
    if (field == "periode") {
        if (n < 10 || n > 300) { err = "p\xC3\xA9riode : de 10 \xC3\xA0 300 s"; return false; }
        sec.dynamicPeriodS = static_cast<int>(n);
        return true;
    }
    if (field == "chiffres") {
        if (n < 6 || n > 8) { err = "chiffres : de 6 \xC3\xA0 8"; return false; }
        sec.dynamicDigits = static_cast<int>(n);
        return true;
    }
    if (field == "deconnexion") {
        if (n < 0 || n > 1440) { err = "d\xC3\xA9" "connexion : de 0 (jamais) \xC3\xA0 1440 min"; return false; }
        sec.autoLogoutMin = static_cast<int>(n);
        return true;
    }
    if (field == "menu_comptes" || field == "menu_acces" || field == "menu_journal") {
        if (n < 0 || n > 99) { err = "niveau : de 0 \xC3\xA0 99"; return false; }
        (field == "menu_comptes" ? sec.menuLevelAccounts : field == "menu_acces" ? sec.menuLevelAccess : sec.menuLevelJournal) = static_cast<int>(n);
        return true;
    }
    // Lot 13 : la politique des mots de passe, le verrouillage, l'avertissement.
    const auto bounded = [&](int& target, double lo, double hi, const std::string& text) {
        if (n < lo || n > hi) { err = text; return false; }
        target = static_cast<int>(n);
        return true;
    };
    if (field == "mdp_longueur") return bounded(sec.pwMinLength, 0, 64, "longueur minimale : de 0 \xC3\xA0 64");
    if (field == "mdp_historique") return bounded(sec.pwHistory, 0, 24, "derniers mots de passe interdits : de 0 \xC3\xA0 24");
    if (field == "verrou_essais") return bounded(sec.lockAttempts, 0, 99, "verrouillage : de 0 (jamais) \xC3\xA0 99 \xC3\xA9" "checs");
    if (field == "verrou_minutes") return bounded(sec.lockMinutes, 0, 10080, "dur\xC3\xA9" "e du verrouillage : de 0 \xC3\xA0 10080 min");
    if (field == "avertir") return bounded(sec.logoutWarnS, 0, 600, "avertissement : de 0 \xC3\xA0 600 s");
    if (field == "mdp_duree") {
        const int before = sec.pwMaxAgeDays;
        if (!bounded(sec.pwMaxAgeDays, 0, 3650, "expiration : de 0 (jamais) \xC3\xA0 3650 jours")) return false;
        // L'expiration s'allume : un mot de passe sans date prend celle du jour
        // (il ne serait jamais perime - ou le serait tout de suite).
        if (before <= 0 && sec.pwMaxAgeDays > 0)
            for (auto& u : sec.users)
                if (u.protection == "classique" && !u.passwordHash.empty() && u.passwordSet.empty()) u.passwordSet = hmi::dayOf(hmi::wallStamp());
        return true;
    }
    err = "champ inconnu : " + field;
    return false;
}

hmi::Id addUser(hmi::Project& pr, const std::string& login, const std::string& group, std::string& err) {
    if (!hmi::isIdentifier(login)) { err = "login invalide : lettres, chiffres et _ (" + login + ")"; return hmi::kNoId; }
    if (pr.userByLogin(login)) { err = "'" + login + "' existe d\xC3\xA9j\xC3\xA0"; return hmi::kNoId; }
    hmi::User u;
    u.id = pr.allocate();
    u.login = login;
    const auto* g = group.empty() ? (pr.security.groups.empty() ? nullptr : &pr.security.groups.front()) : pr.groupByName(group);
    if (!group.empty() && !g) { err = "groupe inconnu : " + group; return hmi::kNoId; }
    u.group = g ? g->id : hmi::kNoId;
    const hmi::Id made = u.id;
    pr.security.users.push_back(std::move(u));
    return made;
}

bool setUserField(hmi::Project& p, hmi::Id id, const std::string& field, const std::string& value, std::string& err, std::size_t* followed) {
    auto* u = p.user(id);
    if (!u) { err = "utilisateur introuvable"; return false; }
    const std::string old = u->login;
    if (field == "login") {
        if (!hmi::isIdentifier(value)) { err = "login invalide : lettres, chiffres et _ (" + value + ")"; return false; }
        for (const auto& o : p.security.users)
            if (o.id != id && same(o.login, value)) { err = "'" + value + "' existe d\xC3\xA9j\xC3\xA0"; return false; }
        u->login = value;
        if (same(p.security.startUser, old)) p.security.startUser = value;
        const std::size_t n = retarget(p, hmi::Operation::ChangeUser, old, value);
        if (followed) *followed = n;
        return true;
    }
    if (field == "nom") { u->fullName = value; return true; }
    if (field == "groupe") {
        const auto* g = p.groupByName(value);
        if (!g) { err = "groupe inconnu : " + value; return false; }
        u->group = g->id;
        return true;
    }
    if (field == "protection") {
        const auto& names = hmi::protectionNames();
        if (std::find(names.begin(), names.end(), value) == names.end()) { err = "protection : classique, dynamique ou expression"; return false; }
        u->protection = value;
        // Passer au code dynamique : un secret tout de suite (le code s'affiche).
        if (value == "dynamique" && u->secret.empty()) u->secret = hmi::randomHex(20);
        return true;
    }
    if (field == "expression") {
        if (!value.empty()) {
            const auto e = hmi::Expression::compile(value);
            if (!e.valid()) { err = "expression illisible : " + e.error(); return false; }
        }
        u->expression = value;
        return true;
    }
    if (field == "actif") { u->enabled = yes(value); return true; }
    if (field == "description") { u->description = value; return true; }
    if (field == "changer") { u->mustChange = yes(value); return true; }        // lot 13
    err = "champ inconnu : " + field;
    return false;
}

hmi::Id addGroup(hmi::Project& pr, const std::string& name, int level, std::string& err) {
    if (pr.groupByName(name)) { err = "le groupe '" + name + "' existe d\xC3\xA9j\xC3\xA0"; return hmi::kNoId; }
    if (level < 1 || level > 9) { err = "niveau de 1 \xC3\xA0 9"; return hmi::kNoId; }
    hmi::UserGroup g;
    g.id = pr.allocate();
    g.name = name;
    g.level = level;
    const hmi::Id made = g.id;
    pr.security.groups.push_back(std::move(g));
    return made;
}

bool setGroupField(hmi::Project& p, hmi::Id id, const std::string& field, const std::string& value, std::string& err) {
    auto* g = p.group(id);
    if (!g) { err = "groupe introuvable"; return false; }
    if (field == "nom") {
        if (value.empty()) { err = "un groupe a un nom"; return false; }
        for (const auto& o : p.security.groups)
            if (o.id != id && same(o.name, value)) { err = "le groupe '" + value + "' existe d\xC3\xA9j\xC3\xA0"; return false; }
        g->name = value;
        return true;
    }
    if (field == "niveau") {
        long long n = 0;
        if (!sim::readInteger(value, n) || n < 1 || n > 9) { err = "niveau : de 1 \xC3\xA0 9"; return false; }
        g->level = static_cast<int>(n);
        return true;
    }
    if (field == "roles") {
        std::vector<std::string> roles;
        for (const auto& r : splitList(value)) {
            const auto* role = p.role(r);
            if (!role) { err = "r\xC3\xB4le inconnu : " + r; return false; }
            roles.push_back(role->name);
        }
        g->roles = roles;
        return true;
    }
    if (field == "description") { g->description = value; return true; }
    if (field == "vue_demarrage") {                             // lot 12
        if (value.empty() || value.rfind("(", 0) == 0) { g->startView = hmi::kNoId; return true; }
        const auto* v = p.viewByName(value);
        if (!v) { err = "vue introuvable : " + value; return false; }
        g->startView = v->id;
        return true;
    }
    err = "champ inconnu : " + field;
    return false;
}

std::vector<std::string> administrators(const hmi::Project& p, bool loggable) {
    std::vector<std::string> out;
    for (const auto& u : p.security.users)
        if (u.enabled && hmi::userHas(p, u, "Administrer") && (!loggable || canLogIn(u))) out.push_back(u.login);
    return out;
}

std::vector<std::string> adminGroups(const hmi::Project& p) {
    std::vector<std::string> out;
    for (const auto& g : p.security.groups)
        for (const auto& roleName : g.roles)
            if (const auto* r = p.role(roleName))
                if (std::any_of(r->permissions.begin(), r->permissions.end(), [](const std::string& x) { return same(x, "Administrer"); })) {
                    out.push_back(g.name);
                    break;
                }
    return out;
}

// ---- une vue -------------------------------------------------------------------------------------
bool isViewLinkField(std::string_view f) noexcept {
    return f == "modele" || f == "entete" || f == "pied" || f == "entete_modele" || f == "pied_modele" || f == "vue_parente";
}

bool setViewLink(const hmi::Project& p, hmi::View& vv, const std::string& field, const std::string& value, std::string& why) {
    // ---- lot 6 : ecran modele, en-tete, pied
    if (field == "modele") {
        if (value.empty() || value == "(aucun)") { vv.templateView = hmi::kNoId; return true; }
        const auto* t = p.viewByName(value);
        if (!t || t->role != "modele") { why = "'" + value + "' n'est pas un \xC3\xA9" "cran mod\xC3\xA8le"; return false; }
        // Pas de boucle : le modele ne doit pas heriter (de proche en proche) de cette vue.
        for (const auto* up : hmi::templateChain(p, *t))
            if (up->id == vv.id) { why = "h\xC3\xA9ritage circulaire : " + t->name + " h\xC3\xA9rite d\xC3\xA9j\xC3\xA0 de " + vv.name; return false; }
        if (t->id == vv.id) { why = "une vue n'h\xC3\xA9rite pas d'elle-m\xC3\xAAme"; return false; }
        vv.templateView = t->id;
        return true;
    }
    if (field == "entete") { vv.showHeader = hmi::parseBool(value, false); return true; }
    if (field == "pied") { vv.showFooter = hmi::parseBool(value, false); return true; }
    if (field == "entete_modele" || field == "pied_modele") {
        const bool head = field == "entete_modele";
        if (value.empty() || value == "(le premier)") { (head ? vv.header : vv.footer) = hmi::kNoId; return true; }
        const auto* t = p.viewByName(value);
        if (!t || t->role != (head ? "entete" : "pied")) {
            why = "'" + value + "' n'est pas un mod\xC3\xA8le " + (head ? std::string("d'en-t\xC3\xAAte") : std::string("de pied de page"));
            return false;
        }
        (head ? vv.header : vv.footer) = t->id;
        return true;
    }
    // ---- lot 12 : la vue parente (fil d'Ariane)
    if (field == "vue_parente") {
        if (value.empty() || value == "(aucune)") { vv.upView = hmi::kNoId; return true; }
        const auto* up = p.viewByName(value);
        if (!up) { why = "'" + value + "' : vue introuvable"; return false; }
        // Pas de boucle : la parente ne doit pas descendre (de proche en proche) de cette vue.
        for (const auto* x = up; x; x = x->upView != hmi::kNoId ? p.view(x->upView) : nullptr) {
            if (x->id == vv.id) { why = "boucle : " + up->name + " a d\xC3\xA9j\xC3\xA0 " + vv.name + " pour parente"; return false; }
            if (x->upView == x->id) break;
        }
        vv.upView = up->id;
        return true;
    }
    why = "champ inconnu : " + field;
    return false;
}

// ---- une variable IHM ------------------------------------------------------------------------------
bool validInitial(const hmi::Project& p, const std::string& type, const std::string& initial, std::string* why) {
    if (trimmed(initial).empty()) return true;
    // 1.12.2 : un type objet - un litteral que le moteur range dans son type ([1, 2, 3], ['a' := 1], (1, 'x')).
    if (ty::isRich(type)) {
        std::string w;
        if (hmi::typeform::valueFits(p, type, initial, &w)) return true;
        return fail(why, "valeur initiale illisible pour un " + ty::normalized(type) + " : " + w + " (" + std::string(hmi::typeform::valueHint(hmi::typeform::decompose(type).form)) + ")");
    }
    // 1.10 (chantier U) : une enumeration prend le nom d'une de ses valeurs (T_MODE#Auto, le texte, le nombre).
    if (const auto* e = hmi::findEnumeration(p, type)) {
        std::int64_t n = 0;
        if (hmi::enumNumberOf(*e, initial, n)) return true;
        std::string names;
        for (std::size_t i = 0; i < e->values.size() && i < 6; ++i) names += (names.empty() ? "" : ", ") + e->values[i].name;
        return fail(why, "valeur inconnue de " + e->name + " : " + names);
    }
    const auto f = ty::isComposite(type) ? ty::flatten(p, "x", type, initial) : ty::Flat{};
    // 1.12.10 (lot F) : une structure prend un litteral (Marche := TRUE, Consigne := 12.5) - les membres nommes,
    // les autres gardent la valeur de leur type ; un tableau de structures, un litteral par element
    // [(Nom := 'Cuve A', Niveau := 4.2), (...)]. Avant : refuse (<< une structure prend les valeurs initiales de
    // son type >>). Ce que le moteur ne pourrait pas appliquer est refuse, avec la raison.
    if (!f.notes.empty()) {
        std::string note = f.notes.front();
        if (note.rfind("x : ", 0) == 0) note.erase(0, 4);
        else if (note.rfind("x.", 0) == 0) note.erase(0, 2);
        else if (note.rfind("x[", 0) == 0) note.erase(0, 1);
        for (const char* tail : {" - la valeur du type reste", " - les valeurs du type "})
            if (const auto at = note.find(tail); at != std::string::npos) note.erase(at);
        return fail(why, note.rfind("valeur initiale", 0) == 0 ? note : "valeur initiale : " + note);
    }
    std::vector<std::string> texts;
    if (ty::isComposite(type)) for (const auto& l : f.leaves) texts.push_back(l.initial);
    else texts.push_back(initial);
    for (const auto& text : texts) {
        if (text.empty()) continue;
        const auto e = hmi::Expression::compile(text);
        if (!e.valid()) return fail(why, "valeur initiale illisible : " + text + " (" + e.error() + ")");
    }
    return true;
}

bool membersInternal(const hmi::Project& p, const hmi::Variable& v, const std::vector<std::string>& relPaths, bool internal, bool allElements,
                     std::vector<std::string>& next, std::string* why) {
    if (!v.bound()) return fail(why, v.name + " n'est li\xC3\xA9" "e \xC3\xA0 aucun \xC3\xA9quipement : ses membres sont d\xC3\xA9j\xC3\xA0 dans l'IHM");
    if (!ty::isComposite(v.type)) return fail(why, v.name + " n'a pas de membres");
    std::vector<std::string> wanted;
    for (const auto& r : relPaths) {
        const std::string rel = trimmed(r);
        if (rel.empty()) continue;
        if (ty::typeOfPath(p, v.name + (rel.front() == '[' ? "" : ".") + rel).empty())
            return fail(why, rel + " : pas un membre de " + v.name);
        const std::string pattern = allElements ? allElementsOf(rel) : rel;
        if (std::find(wanted.begin(), wanted.end(), pattern) == wanted.end()) wanted.push_back(pattern);
    }
    if (wanted.empty()) return fail(why, "aucun membre choisi");
    // Les cases de la variable (sans membres internes) : pour defaire un motif qui en couvre plus.
    hmi::Variable all = v;
    all.internal.clear();
    std::vector<std::string> rels;
    for (const auto& l : ty::leafVariables(p, all)) {
        const std::string rest = l.name.substr(std::min(l.name.size(), v.name.size()));
        rels.push_back(!rest.empty() && rest.front() == '.' ? rest.substr(1) : rest);
    }
    next = v.internal;
    for (const auto& pattern : wanted) {
        if (internal) {
            // Deja couvert : rien a faire ; sinon il remplace ceux qu'il couvre.
            if (std::any_of(next.begin(), next.end(), [&](const std::string& e) { return ty::memberCovers(e, pattern); })) continue;
            std::erase_if(next, [&](const std::string& e) { return ty::memberCovers(pattern, e); });
            next.push_back(pattern);
            continue;
        }
        // Attribuer : retirer ce qu'il couvre ; un motif plus large ([*].NOM pour [0].NOM) se defait
        // en ses cases, moins celles-ci.
        std::vector<std::string> keep, expanded;
        for (const auto& e : next) {
            if (ty::memberCovers(pattern, e)) continue;                       // lui, ou dessous
            if (!ty::memberCovers(e, pattern) && !std::any_of(rels.begin(), rels.end(), [&](const std::string& r) {
                    return ty::memberCovers(e, r) && ty::memberCovers(pattern, r);
                })) {
                keep.push_back(e);
                continue;
            }
            for (const auto& r : rels)
                if (ty::memberCovers(e, r) && !ty::memberCovers(pattern, r)) expanded.push_back(r);
        }
        for (auto& x : expanded) keep.push_back(std::move(x));
        next = std::move(keep);
    }
    return true;
}

} // namespace app::projedit
