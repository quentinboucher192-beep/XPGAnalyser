// =============================================================================
//  hmi/HmiRuntimeLogin.cpp - le menu natif de connexion en marche (lot 12)
// -----------------------------------------------------------------------------
//  Comme Parametres systeme (HmiRuntimeSystem.cpp) : un menu que l'IHM porte
//  elle-meme, par-dessus la vue. Ses onglets (HmiLoginMenu.hpp) se montrent
//  selon le niveau de l'utilisateur connecte : Connexion a tout le monde, Mon
//  compte des qu'on est connecte, Comptes, Acces et Journal selon les niveaux
//  de la securite (la permission Administrer les montre tous).
//
//  LA SAISIE : un champ touche prend le focus (le clavier virtuel vient, selon
//  le reglage du poste) ; ce qui est tape n'est jamais journalise ; Entree
//  valide, Echap efface, Tab passe au champ suivant. A l'ouverture, personne
//  n'etant connecte, le mot de passe attend deja (un clavier physique tape
//  tout de suite).
//
//  CE QUI CHANGE LE PROJET (un compte, un role, la deconnexion automatique)
//  demande la permission Administrer et passe par l'ecran (Hooks::userRequest),
//  en commandes annulables ; un refus est un evenement "Acces refuse", chaque
//  changement un evenement "Compte modifie" ou "Acces modifie". On ne se retire
//  pas soi-meme le droit d'entrer ni celui d'administrer.
// =============================================================================
#include "HmiRuntime.hpp"

#include "HmiCrypto.hpp"
#include "HmiPolicy.hpp"

#include <algorithm>
#include <cstdlib>

namespace hmi {

namespace {

constexpr const char* kSource = "Menu de connexion";
constexpr std::size_t kMinPassword = 6;
constexpr std::size_t kMaxTyped = 64;

std::string upperCopy(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    return out;
}

// Les positions des caracteres (UTF-8) : le curseur ne coupe pas un "e".
std::size_t prevCodepoint(const std::string& s, std::size_t at) {
    if (at == 0) return 0;
    std::size_t p = at - 1;
    while (p > 0 && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80) --p;
    return p;
}
std::size_t nextCodepoint(const std::string& s, std::size_t at) {
    if (at >= s.size()) return s.size();
    std::size_t p = at + 1;
    while (p < s.size() && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80) ++p;
    return p;
}
std::size_t codepointCount(const std::string& s) {
    std::size_t n = 0;
    for (const char c : s) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}

// Les champs d'un onglet, dans l'ordre (Tab, Entree) ; lot 13 : le renouvellement
// demande le nouveau mot de passe et sa confirmation.
std::vector<std::string> fieldsOfTab(LoginTab t, bool renewal = false) {
    if (t == LoginTab::Connexion) return renewal ? std::vector<std::string>{"nouveau", "confirmation"} : std::vector<std::string>{"secret"};
    if (t == LoginTab::Compte) return {"ancien", "nouveau", "confirmation"};
    return {};
}

// Ce que l'onglet Journal montre.
bool isLoginEvent(std::string_view kind) {
    static const char* kinds[] = {"Connexion", "Connexion refus\xC3\xA9" "e", "D\xC3\xA9" "connexion", "Acc\xC3\xA8s refus\xC3\xA9",
                                  "Mot de passe chang\xC3\xA9", "Mot de passe refus\xC3\xA9", "Compte modifi\xC3\xA9",
                                  "Acc\xC3\xA8s modifi\xC3\xA9",
                                  // lot 13
                                  "Compte verrouill\xC3\xA9", "Compte d\xC3\xA9verrouill\xC3\xA9", "Mot de passe \xC3\xA0 renouveler",
                                  "Mot de passe renouvel\xC3\xA9"};
    return std::any_of(std::begin(kinds), std::end(kinds), [&](const char* k) { return kind == k; });
}

// Le groupe a-t-il la permission par ses roles, le role `without` retire ?
bool groupHas(const Project& p, const UserGroup& g, std::string_view permission, std::string_view without = {}) {
    for (const auto& roleName : g.roles) {
        if (!without.empty() && upperCopy(roleName) == upperCopy(without)) continue;
        if (const auto* r = p.role(roleName))
            for (const auto& perm : r->permissions)
                if (upperCopy(perm) == upperCopy(permission) || upperCopy(perm) == "ADMINISTRER") return true;
    }
    return false;
}

std::string logoutMinutesText(int minutes) {
    return minutes <= 0 ? std::string("jamais") : std::to_string(minutes) + " min";
}

// "vue:3" -> 3 ; -1 sinon.
long numberAfter(std::string_view part, std::string_view prefix) {
    if (part.substr(0, prefix.size()) != prefix) return -1;
    const std::string rest(part.substr(prefix.size()));
    if (rest.empty()) return -1;
    char* end = nullptr;
    const long v = std::strtol(rest.c_str(), &end, 10);
    return end && *end == '\0' && v >= 0 ? v : -1;
}

} // namespace

// ================================================================= ouvrir ===
std::vector<LoginTab> Runtime::loginTabs() const {
    if (!project_) return {LoginTab::Connexion};
    return visibleLoginTabs(*project_, user());
}

void Runtime::loginSay(std::string text, bool error, double now) {
    loginForm_.message = std::move(text);
    loginForm_.error = error;
    loginForm_.messageAt = now;
}

void Runtime::openLoginMenu(LoginTab tab, double now, const std::string& source) {
    now_ = std::max(now_, now);
    lastActivity_ = std::max(lastActivity_, now);
    if (focused_ != kNoId) unfocus(now);          // un champ de la vue perd le focus
    const bool wasShown = loginShown_;
    if (!wasShown) {
        loginForm_ = FormState{};
        loginKeyboard_ = false;
    }
    systemShown_ = false;                          // un menu natif a la fois
    systemClock_.reset();
    loginShown_ = true;
    loginScroll_ = 0;
    const auto tabs = loginTabs();
    const bool visible = std::find(tabs.begin(), tabs.end(), tab) != tabs.end();
    loginTab_ = visible ? tab : LoginTab::Connexion;
    loginWanted_.reset();
    if (!visible) {
        // L'onglet demande ne se voit pas : Connexion, et pourquoi ; il s'ouvrira
        // a la connexion d'un compte qui le voit.
        loginWanted_ = tab;
        std::string why;
        if (tab == LoginTab::Compte) why = "connecte-toi d'abord";
        else {
            const auto& s = project_ ? project_->security : Security{};
            const int need = tab == LoginTab::Comptes ? s.menuLevelAccounts : tab == LoginTab::Acces ? s.menuLevelAccess : s.menuLevelJournal;
            why = "il se montre d\xC3\xA8s le niveau " + std::to_string(need) + " (ou avec la permission Administrer)";
        }
        loginSay(std::string(loginTabLabel(tab)) + " : " + why, true, now);
    } else if (!wasShown) {
        loginForm_.message.clear();
        loginForm_.error = false;
    }
    // Personne n'est connecte : le mot de passe attend (un clavier physique tape
    // tout de suite ; le clavier virtuel vient quand on touche le champ).
    if (loginTab_ == LoginTab::Connexion && user_.empty()) {
        loginForm_.focus = "secret";
        loginForm_.caret["secret"] = loginForm_.text["secret"].size();
    }
    log("Action", source.empty() ? std::string(kSource) : source,
        "menu de connexion (" + std::string(loginTabLabel(loginTab_)) + ")");
}

void Runtime::resetLogin() {
    // Lot 13 : fermer le menu abandonne un renouvellement (personne n'est connecte).
    if (renewal_) {
        event("Connexion refus\xC3\xA9" "e", renewal_->login, "renouvellement du mot de passe abandonn\xC3\xA9");
        renewal_.reset();
    }
    loginShown_ = false;
    loginTab_ = LoginTab::Connexion;
    loginForm_ = FormState{};
    loginKeyboard_ = false;
    loginScroll_ = 0;
    loginWanted_.reset();
    simAfterLogin_ = false;           // 1.9 : la page Simulation ne revient plus
}

void Runtime::loginAfterUserChange() {
    // Les mots de passe tapes ne survivent pas a un changement d'utilisateur, ni
    // la ligne choisie ; un onglet qui ne se voit plus laisse la place a Connexion.
    for (auto& [name, text] : loginForm_.text) text.clear();
    for (auto& [name, at] : loginForm_.caret) at = 0;
    loginForm_.selected = kNoId;
    if (!loginShown_) return;
    const auto tabs = loginTabs();
    if (std::find(tabs.begin(), tabs.end(), loginTab_) == tabs.end()) {
        loginTab_ = LoginTab::Connexion;
        loginScroll_ = 0;
        loginForm_.focus.clear();
        loginKeyboard_ = false;
    }
}

const User* Runtime::loginAccount() const {
    if (!project_) return nullptr;
    std::vector<const User*> users;
    for (const auto& u : project_->security.users) if (u.enabled) users.push_back(&u);
    if (users.empty()) return nullptr;
    const std::string want = upperCopy(!loginForm_.chosen.empty() ? loginForm_.chosen : user_);
    if (!want.empty())
        for (const auto* u : users) if (upperCopy(u->login) == want) return u;
    return users.front();
}

std::vector<HistoryEvent> Runtime::loginJournal() const {
    std::vector<HistoryEvent> out;
    const auto take = [&](const auto& list) {
        for (auto it = list.rbegin(); it != list.rend() && out.size() < 300; ++it)
            if (isLoginEvent(it->kind)) out.push_back(*it);
    };
    if (history_ && project_ && project_->history.events) take(history_->events);
    else take(events_);
    return out;
}

std::size_t Runtime::loginRows() const {
    if (!project_) return 0;
    if (loginTab_ == LoginTab::Comptes) return project_->security.users.size();
    if (loginTab_ == LoginTab::Journal) return loginJournal().size();
    return 0;
}

// ============================================================ une partie ===
void Runtime::loginPart(std::string_view part, double now) {
    now_ = std::max(now_, now);
    lastActivity_ = std::max(lastActivity_, now);        // un toucher
    if (!loginShown_) return;
    GestureGuard gesture(*this, kSource);                // lot 13 : l'audit
    auto& f = loginForm_;
    if (part == "fermer" || part == "dehors") {
        resetLogin();
        return;
    }
    if (part.empty() || part == "rien") {                 // le fond du menu : le clavier s'en va
        f.focus.clear();
        loginKeyboard_ = false;
        return;
    }
    if (part.rfind("onglet:", 0) == 0) {
        const LoginTab t = loginTabFrom(part.substr(7));
        const auto tabs = loginTabs();
        if (std::find(tabs.begin(), tabs.end(), t) == tabs.end()) {
            loginSay(std::string(loginTabLabel(t)) + " : cet onglet ne se voit pas pour " + (user_.empty() ? std::string("personne") : user_), true, now);
            return;
        }
        if (t == loginTab_) return;
        if (renewal_) {                                  // lot 13 : un autre onglet abandonne le renouvellement
            event("Connexion refus\xC3\xA9" "e", renewal_->login, "renouvellement du mot de passe abandonn\xC3\xA9");
            renewal_.reset();
        }
        loginTab_ = t;
        loginScroll_ = 0;
        f.focus.clear();
        loginKeyboard_ = false;
        for (auto& [name, text] : f.text) text.clear();
        for (auto& [name, at] : f.caret) at = 0;
        f.message.clear();
        f.error = false;
        if (t == LoginTab::Connexion && user_.empty()) f.focus = "secret";
        return;
    }
    if (part.rfind("defiler:", 0) == 0) {
        const std::string arg(part.substr(8));
        const std::size_t rows = loginRows();
        const long long last = rows > 0 ? static_cast<long long>(rows) - 1 : 0;
        long long next = static_cast<long long>(loginScroll_);
        if (!arg.empty() && arg[0] == '=') next = std::atoll(arg.c_str() + 1);
        else next += std::atoll(arg.c_str());
        loginScroll_ = static_cast<std::size_t>(std::clamp(next, 0LL, last));
        return;
    }
    if (part.rfind("champ:", 0) == 0) {
        const std::string field(part.substr(6));
        const auto fields = fieldsOfTab(loginTab_, renewal_.has_value());
        if (std::find(fields.begin(), fields.end(), field) == fields.end()) return;
        if (loginTab_ == LoginTab::Compte && !user_.empty()) {
            if (const User* u = user(); u && u->protection != "classique") {
                loginSay("ce compte n'a pas de mot de passe (" + u->protection + ")", true, now);
                return;
            }
        }
        if (loginTab_ == LoginTab::Compte && user_.empty()) {
            loginSay("personne n'est connect\xC3\xA9", true, now);
            return;
        }
        f.focus = field;
        f.caret[field] = f.text[field].size();
        loginKeyboard_ = true;
        return;
    }
    switch (loginTab_) {
        case LoginTab::Connexion: {
            // Lot 13 : le renouvellement - le nouveau mot de passe, ou abandonner.
            if (renewal_) {
                if (part == "bouton:renouveler") renewalSubmit(now);
                else if (part == "bouton:annuler") {
                    const std::string who = renewal_->login;
                    event("Connexion refus\xC3\xA9" "e", who, "renouvellement du mot de passe abandonn\xC3\xA9");
                    renewal_.reset();
                    f.focus = "secret";
                    for (auto& [name, text] : f.text) text.clear();
                    for (auto& [name, at] : f.caret) at = 0;
                    f.chosen = who;
                    loginSay("renouvellement abandonn\xC3\xA9 : " + who + " n'est pas connect\xC3\xA9", true, now);
                }
                return;
            }
            if (part == "precedent" || part == "suivant") {
                std::vector<const User*> users;
                if (project_)
                    for (const auto& u : project_->security.users) if (u.enabled) users.push_back(&u);
                if (users.empty()) { loginSay("aucun compte actif", true, now); return; }
                const std::size_t n = users.size();
                const User* shown = loginAccount();
                std::size_t at = 0;
                for (std::size_t k = 0; k < n; ++k) if (users[k] == shown) at = k;
                at = part == "suivant" ? (at + 1) % n : (at + n - 1) % n;
                f.chosen = users[at]->login;
                f.text["secret"].clear();
                f.caret["secret"] = 0;
                f.focus = "secret";
                if (f.error) { f.message.clear(); f.error = false; }
                return;
            }
            if (part == "bouton:connexion") { loginSubmit(now); return; }
            if (part == "bouton:deconnexion") {
                if (user_.empty()) { loginSay("personne n'est connect\xC3\xA9", true, now); return; }
                const std::string who = user_;
                logout(now, "menu de connexion");
                f.chosen = who;             // on se reconnecte en un geste
                f.focus = "secret";
                loginSay(who + " d\xC3\xA9" "connect\xC3\xA9", false, now);
                return;
            }
            return;
        }
        case LoginTab::Compte:
            if (part == "bouton:changer") loginSubmit(now);
            return;
        case LoginTab::Comptes: {
            if (const long row = numberAfter(part, "ligne:"); row >= 0) {
                if (project_ && static_cast<std::size_t>(row) < project_->security.users.size()) {
                    f.selected = project_->security.users[static_cast<std::size_t>(row)].id;
                    if (f.error) { f.message.clear(); f.error = false; }
                }
                return;
            }
            if (part.rfind("bouton:", 0) == 0) loginAccountsPart(part.substr(7), now);
            return;
        }
        case LoginTab::Acces:
            if (part.rfind("role:", 0) == 0 || part.rfind("deconnexion:", 0) == 0) loginAccessPart(part, now);
            return;
        case LoginTab::Journal:
            return;
    }
}

// ============================================================== valider ===
void Runtime::loginSubmit(double now) {
    auto& f = loginForm_;
    if (loginTab_ == LoginTab::Connexion) {
        if (renewal_) { renewalSubmit(now); return; }      // lot 13
        const User* account = loginAccount();
        if (!account) { loginSay("aucun compte actif : rien \xC3\xA0 connecter", true, now); return; }
        const std::string who = account->login;
        const std::string secret = f.text["secret"];
        f.text["secret"].clear();
        f.caret["secret"] = 0;
        std::string why;
        // Lot 13 : un badge passe dans le champ (le lecteur tape comme un clavier).
        bool done = false;
        if (project_ && project_->security.badgeLogin && !secret.empty())
            for (const auto& u : project_->security.users)
                if (!u.badge.empty() && badgeMatches(u.badge, secret)) {
                    done = loginBadge(secret, now, &why);
                    if (!done) { f.focus = "secret"; loginSay(why, true, now); return; }
                    break;
                }
        if (!done) {
            loginVia_ = kSource;
            if (!login(who, secret, now, &why)) {
                if (renewal_) return;            // lot 13 : le renouvellement a pris la main (message, champs)
                f.focus = "secret";
                loginSay(why, true, now);
                return;
            }
        }
        f.focus.clear();
        f.chosen.clear();                  // le compte montre : celui connecte
        loginKeyboard_ = false;
        const User* u = user();
        // 1.9 : venu de la page Simulation (Se connecter...) - avec la permission
        // Administrer, la page revient.
        if (simAfterLogin_) {
            simAfterLogin_ = false;
            if (simPageAllowed()) {
                const std::string welcome = "Connect\xC3\xA9 : " + (u && !u->fullName.empty() ? u->fullName : who);
                openSystemMenu(kSimulationTab, now, kSource);
                systemSay(welcome, false);
                return;
            }
        }
        // L'onglet demande a l'ouverture, s'il se voit maintenant.
        if (loginWanted_) {
            const auto tabs = loginTabs();
            if (std::find(tabs.begin(), tabs.end(), *loginWanted_) != tabs.end()) loginTab_ = *loginWanted_;
            loginWanted_.reset();
        }
        loginSay("Bienvenue, " + (u && !u->fullName.empty() ? u->fullName : who), false, now);
        return;
    }
    if (loginTab_ != LoginTab::Compte) return;
    const User* u = user();
    if (!u) { loginSay("personne n'est connect\xC3\xA9", true, now); return; }
    if (u->protection != "classique") { loginSay("ce compte n'a pas de mot de passe (" + u->protection + ")", true, now); return; }
    const std::string oldPwd = f.text["ancien"], newPwd = f.text["nouveau"], confirm = f.text["confirmation"];
    const auto clearAll = [&] {
        for (auto& [name, text] : f.text) text.clear();
        for (auto& [name, at] : f.caret) at = 0;
    };
    const std::string who = u->login;
    if (!passwordMatches(u->salt, u->passwordHash, oldPwd)) {
        event("Mot de passe refus\xC3\xA9", who, "ancien mot de passe incorrect (menu de connexion)");
        clearAll();
        f.focus = "ancien";
        loginSay("ancien mot de passe incorrect", true, now);
        return;
    }
    // Lot 13 : la politique des mots de passe du projet s'ajoute (longueur, chiffres...).
    std::string problem = passwordProblem(project_->security, u, newPwd, kMinPassword);
    if (!problem.empty()) {}
    else if (newPwd == oldPwd) problem = "le nouveau doit \xC3\xAAtre diff\xC3\xA9rent de l'ancien";
    else if (newPwd != confirm) problem = "la confirmation ne correspond pas";
    if (!problem.empty()) {
        f.text["confirmation"].clear();
        f.caret["confirmation"] = 0;
        f.focus = "nouveau";
        loginSay(problem, true, now);
        return;
    }
    if (!hooks_.userRequest) { loginSay("l'\xC3\xA9" "cran ne sait pas enregistrer le mot de passe ici", true, now); return; }
    UserRequest rq;
    rq.op = "changer";
    rq.user = u->id;
    rq.login = who;
    rq.salt = randomHex(16);
    rq.hash = passwordHash(rq.salt, newPwd);
    rq.source = kSource;
    clearAll();
    hooks_.userRequest(rq);
    event("Mot de passe chang\xC3\xA9", who, "par " + who + " (menu de connexion)");
    audit("Mot de passe", kSource, who, {}, "chang\xC3\xA9 par " + who);      // lot 13 (jamais le mot de passe)
    f.focus.clear();
    loginKeyboard_ = false;
    loginSay("Mot de passe chang\xC3\xA9", false, now);
}

// ================================================================ Comptes ===
void Runtime::loginAccountsPart(std::string_view button, double now) {
    if (!project_) return;
    auto& f = loginForm_;
    std::string what = "Ajouter";
    if (button == "motdepasse") what = "Mot de passe";
    else if (button == "activer") what = "Activer";
    else if (button.rfind("groupe:", 0) == 0) what = "Groupe";
    else if (button == "supprimer") what = "Supprimer";
    else if (button != "ajouter") return;
    if (!permitted("Administrer")) {
        const std::string msg = what + " : permission \xC2\xAB Administrer \xC2\xBB requise ("
                              + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")";
        event("Acc\xC3\xA8s refus\xC3\xA9", kSource, msg);
        loginSay(msg, true, now);
        return;
    }
    UserRequest rq;
    rq.source = kSource;
    const User* sel = f.selected != kNoId ? project_->user(f.selected) : nullptr;
    std::string before, after;             // le groupe, avant et apres
    if (button == "ajouter") {
        rq.op = "ajouter";
    } else {
        if (!sel) { loginSay(what + " : choisis un compte (touche sa ligne)", true, now); return; }
        rq.user = sel->id;
        rq.login = sel->login;
        const bool self = !user_.empty() && upperCopy(sel->login) == upperCopy(user_);
        if (button == "motdepasse") {
            rq.op = "motdepasse";
        } else if (button == "activer" && accountLocked(sel->login)) {
            // Lot 13 : un compte verrouille - le bouton le deverrouille (rien ne change au projet).
            const std::string login = sel->login;
            std::string why;
            if (unlockAccount(login, now, &why, kSource)) loginSay(login + " d\xC3\xA9verrouill\xC3\xA9", false, now);
            else loginSay(why, true, now);
            return;
        } else if (button == "activer") {
            rq.op = "activer";
            rq.enabled = !sel->enabled;
            if (self && !rq.enabled) { loginSay("pas le compte connect\xC3\xA9 : tu ne peux pas te d\xC3\xA9sactiver", true, now); return; }
        } else if (button == "supprimer") {
            rq.op = "supprimer";
            if (self) { loginSay("pas le compte connect\xC3\xA9 : tu ne peux pas te supprimer", true, now); return; }
        } else {
            const auto& groups = project_->security.groups;
            if (groups.empty()) { loginSay("aucun groupe dans le projet", true, now); return; }
            const std::size_t n = groups.size();
            std::size_t at = 0;
            bool found = false;
            for (std::size_t i = 0; i < n; ++i)
                if (groups[i].id == sel->group) { at = i; found = true; }
            const std::size_t next = !found ? 0 : button == "groupe:suivant" ? (at + 1) % n : (at + n - 1) % n;
            if (found && next == at) { loginSay("un seul groupe : rien \xC3\xA0 changer", true, now); return; }
            if (self && project_->security.enabled && !groupHas(*project_, groups[next], "Administrer")) {
                loginSay("pas ton propre compte : dans " + groups[next].name + ", tu perdrais la permission Administrer", true, now);
                return;
            }
            before = found ? groups[at].name : std::string("aucun");
            after = groups[next].name;
            rq.op = "groupe";
            rq.group = groups[next].id;
        }
    }
    if (!hooks_.userRequest) { loginSay("l'\xC3\xA9" "cran ne sait pas modifier les comptes ici", true, now); return; }
    // Le projet peut changer pendant l'appel : on ne garde que des copies.
    const std::string login = rq.login, op = rq.op;
    const bool enabled = rq.enabled;
    hooks_.userRequest(rq);
    if (op == "activer") {
        event("Compte modifi\xC3\xA9", login, std::string(enabled ? "activ\xC3\xA9" : "d\xC3\xA9sactiv\xC3\xA9") + " (menu de connexion)");
        audit("Compte", kSource, login, enabled ? "d\xC3\xA9sactiv\xC3\xA9" : "actif", enabled ? "actif" : "d\xC3\xA9sactiv\xC3\xA9");
        loginSay(login + (enabled ? " activ\xC3\xA9" : " d\xC3\xA9sactiv\xC3\xA9"), false, now);
    } else if (op == "groupe") {
        event("Compte modifi\xC3\xA9", login, "groupe : " + before + " \xE2\x86\x92 " + after + " (menu de connexion)");
        audit("Compte", kSource, login + " (groupe)", before, after);
        loginSay(login + " : groupe " + after, false, now);
    } else if (op == "ajouter") {
        loginSay("Nouveau compte : \xC3\xA0 remplir dans le dialogue", false, now);
    } else if (op == "motdepasse") {
        loginSay("Mot de passe de " + login + " : \xC3\xA0 donner dans le dialogue", false, now);
    } else if (op == "supprimer") {
        loginSay("Supprimer " + login + " : \xC3\xA0 confirmer", false, now);
    }
}

// ================================================================== Acces ===
void Runtime::loginAccessPart(std::string_view part, double now) {
    if (!project_) return;
    if (!permitted("Administrer")) {
        const std::string msg = "Acc\xC3\xA8s : permission \xC2\xAB Administrer \xC2\xBB requise ("
                              + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")";
        event("Acc\xC3\xA8s refus\xC3\xA9", kSource, msg);
        loginSay(msg, true, now);
        return;
    }
    if (!hooks_.userRequest) { loginSay("l'\xC3\xA9" "cran ne sait pas modifier les acc\xC3\xA8s ici", true, now); return; }
    const auto& sec = project_->security;
    if (part.rfind("role:", 0) == 0) {
        const std::string rest(part.substr(5));
        const auto comma = rest.find(',');
        if (comma == std::string::npos) return;
        const long gi = std::atol(rest.substr(0, comma).c_str()), ri = std::atol(rest.substr(comma + 1).c_str());
        if (gi < 0 || ri < 0 || static_cast<std::size_t>(gi) >= sec.groups.size() || static_cast<std::size_t>(ri) >= sec.roles.size()) return;
        const UserGroup& g = sec.groups[static_cast<std::size_t>(gi)];
        const std::string roleName = sec.roles[static_cast<std::size_t>(ri)].name, groupName = g.name;
        const bool has = std::any_of(g.roles.begin(), g.roles.end(), [&](const std::string& r) { return upperCopy(r) == upperCopy(roleName); });
        // On ne se retire pas soi-meme le droit d'administrer.
        if (has && sec.enabled)
            if (const User* me = user(); me && me->group == g.id && userHas(*project_, *me, "Administrer")
                                         && !groupHas(*project_, g, "Administrer", roleName)) {
                loginSay(groupName + " sans " + roleName + " : tu perdrais la permission Administrer", true, now);
                return;
            }
        UserRequest rq;
        rq.op = "role";
        rq.group = g.id;
        rq.role = roleName;
        rq.enabled = !has;
        rq.source = kSource;
        hooks_.userRequest(rq);
        event("Acc\xC3\xA8s modifi\xC3\xA9", groupName, (has ? "r\xC3\xB4le retir\xC3\xA9 : " : "r\xC3\xB4le donn\xC3\xA9 : ") + roleName + " (menu de connexion)");
        audit("Acc\xC3\xA8s", kSource, groupName + " / " + roleName, has ? "r\xC3\xB4le donn\xC3\xA9" : "sans le r\xC3\xB4le",
              has ? "sans le r\xC3\xB4le" : "r\xC3\xB4le donn\xC3\xA9");
        loginSay(groupName + (has ? " perd le r\xC3\xB4le " : " re\xC3\xA7oit le r\xC3\xB4le ") + roleName, false, now);
        return;
    }
    if (part == "deconnexion:moins" || part == "deconnexion:plus") {
        const auto& steps = autoLogoutSteps();
        const int cur = sec.autoLogoutMin;
        int next = cur;
        if (part == "deconnexion:plus") {
            for (const int s : steps) if (s > cur) { next = s; break; }
        } else {
            for (auto it = steps.rbegin(); it != steps.rend(); ++it) if (*it < cur) { next = *it; break; }
        }
        if (next == cur) {
            loginSay(cur <= 0 ? std::string("D\xC3\xA9" "connexion automatique : jamais (d\xC3\xA9j\xC3\xA0)")
                              : "D\xC3\xA9" "connexion automatique : " + logoutMinutesText(cur) + " (le plus long)", false, now);
            return;
        }
        UserRequest rq;
        rq.op = "deconnexion";
        rq.minutes = next;
        rq.source = kSource;
        hooks_.userRequest(rq);
        event("Acc\xC3\xA8s modifi\xC3\xA9", "S\xC3\xA9" "curit\xC3\xA9", "d\xC3\xA9" "connexion automatique : " + logoutMinutesText(next)
                                                              + " (menu de connexion)");
        audit("Acc\xC3\xA8s", kSource, "d\xC3\xA9" "connexion automatique", logoutMinutesText(cur), logoutMinutesText(next));
        std::string shown = "D\xC3\xA9" "connexion automatique : " + logoutMinutesText(next);
        if (settings_.autoLogoutMin >= 0)
            shown += " (le poste impose " + logoutMinutesText(settings_.autoLogoutMin) + " : Param\xC3\xA8tres syst\xC3\xA8me)";
        loginSay(shown, false, now);
    }
}

// ================================================================ la saisie ===
std::string Runtime::loginKeyboardMode() const {
    if (loginForm_.focus.empty() || settings_.keyboard == "jamais") return {};
    if (settings_.keyboard != "toujours" && !loginKeyboard_) return {};
    if (loginForm_.focus == "secret")
        if (const User* a = loginAccount(); a && a->protection == "dynamique") return "numerique";
    return "complet";
}

void Runtime::loginTypeText(std::string_view text, double now) {
    auto& f = loginForm_;
    if (f.focus.empty()) return;
    auto& buffer = f.text[f.focus];
    auto& caret = f.caret[f.focus];
    caret = std::min(caret, buffer.size());
    bool digitsOnly = false;
    if (f.focus == "secret")
        if (const User* a = loginAccount(); a && a->protection == "dynamique") digitsOnly = true;
    std::string accepted;
    for (const char c : text) {
        if (c == '\n' || c == '\r' || c == '\t') continue;
        if (digitsOnly && !(c >= '0' && c <= '9')) continue;
        accepted += c;
    }
    if (accepted.empty()) return;
    if (codepointCount(buffer) + codepointCount(accepted) > kMaxTyped) {
        loginSay(std::to_string(kMaxTyped) + " caract\xC3\xA8res au plus", true, now);
        return;
    }
    buffer.insert(caret, accepted);
    caret += accepted.size();
    if (f.error) { f.message.clear(); f.error = false; }
}

bool Runtime::loginTypeKey(EditKey k, double now) {
    auto& f = loginForm_;
    if (f.focus.empty()) {
        // Le menu est modal : sans champ, Echap le ferme, Entree connecte.
        if (k == EditKey::Escape) loginPart("fermer", now);
        else if (k == EditKey::Enter && loginTab_ == LoginTab::Connexion) loginSubmit(now);
        return true;
    }
    auto& buffer = f.text[f.focus];
    auto& caret = f.caret[f.focus];
    caret = std::min(caret, buffer.size());
    const auto fields = fieldsOfTab(loginTab_, renewal_.has_value());
    const auto moveField = [&](int step) {
        if (fields.empty()) return;
        std::size_t at = 0;
        for (std::size_t i = 0; i < fields.size(); ++i) if (fields[i] == f.focus) at = i;
        const std::size_t n = fields.size();
        f.focus = fields[(at + n + static_cast<std::size_t>(step + static_cast<int>(n))) % n];
        f.caret[f.focus] = f.text[f.focus].size();
    };
    switch (k) {
        case EditKey::Backspace:
            if (caret > 0) { const auto p = prevCodepoint(buffer, caret); buffer.erase(p, caret - p); caret = p; }
            break;
        case EditKey::Delete:
            if (caret < buffer.size()) buffer.erase(caret, nextCodepoint(buffer, caret) - caret);
            break;
        case EditKey::Left: caret = prevCodepoint(buffer, caret); break;
        case EditKey::Right: caret = nextCodepoint(buffer, caret); break;
        case EditKey::Home: caret = 0; break;
        case EditKey::End: caret = buffer.size(); break;
        case EditKey::Tab: moveField(1); break;
        case EditKey::BackTab: moveField(-1); break;
        case EditKey::Escape:
            // Annuler : ce qui est tape s'efface.
            for (auto& [name, text] : f.text) text.clear();
            for (auto& [name, at] : f.caret) at = 0;
            f.focus.clear();
            loginKeyboard_ = false;
            break;
        case EditKey::Enter:
            if (!fields.empty() && f.focus != fields.back()) { moveField(1); break; }
            loginSubmit(now);
            break;
    }
    return true;
}

} // namespace hmi
