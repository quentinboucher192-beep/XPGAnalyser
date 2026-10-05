// IHM > Configuration > Utilisateurs : utilisateurs, groupes, roles, securite.
#include "HmiSupervisionPanes.hpp"
#include "HmiAssist.hpp"

#include "HmiPaneKit.hpp"

#include "../../hmi/HmiCrypto.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiHistory.hpp"
#include "../../hmi/HmiPolicy.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace app {

using hmi::Id;
using hmi::kNoId;
using PG = ui::PropertyGrid;
using namespace hmikit;

// Le code du moment d'un utilisateur a code dynamique, en grand, et le temps
// qui lui reste : ce que l'utilisateur lirait sur son application
// d'authentification. Redessine a chaque image (le code change seul).
class HmiDynamicCodePanel final : public ui::Widget {
public:
    HmiDynamicCodePanel(std::string id, hmi::DocumentPtr doc) : ui::Widget(std::move(id)), doc_(std::move(doc)) {}
    void show(Id user) { user_ = user; invalidate(); }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(b, c.panelBg);
        ctx.r.fillRect({b.x, b.y, b.w, 1}, c.border);
        const auto& sec = doc_->project.security;
        const auto* u = doc_->project.user(user_);
        float y = b.y + 10;
        if (!u || u->protection != "dynamique") {
            ctx.r.drawText({b.x + 12, y}, "Code dynamique", ctx.theme.font.smallUi, c.textMuted);
            y += 22;
            ctx.r.drawText({b.x + 12, y}, "Choisis un utilisateur \xC3\xA0 code dynamique pour voir son code du moment.",
                           ctx.theme.font.ui, c.textMuted);
            return;
        }
        const double now = hmi::wallEpoch();
        ctx.r.drawText({b.x + 12, y}, "Code dynamique de " + u->login + "  (TOTP HMAC-SHA-256, " + std::to_string(sec.dynamicDigits)
                                          + " chiffres, " + std::to_string(sec.dynamicPeriodS) + " s)",
                       ctx.theme.font.smallUi, c.textMuted);
        y += 20;
        if (u->secret.empty()) {
            ctx.r.drawText({b.x + 12, y}, "Pas encore de secret : \xC2\xAB Nouveau secret \xC2\xBB dans la barre.", ctx.theme.font.ui, c.warning);
            return;
        }
        std::string code = hmi::dynamicCode(u->secret, now, sec.dynamicPeriodS, sec.dynamicDigits);
        if (code.size() == 6) code.insert(3, " ");
        // En grand : c'est un code qu'on recopie.
        const gfx::FontId font{36};
        ctx.r.drawText({b.x + 14, y + 2}, code, font, c.accent);
        const float codeW = ctx.r.measure(code, font).width;
        const float codeH = ctx.r.lineHeight(font);
        const int left = hmi::dynamicCodeRemaining(now, sec.dynamicPeriodS);
        const float frac = static_cast<float>(left) / static_cast<float>(std::max(1, sec.dynamicPeriodS));
        const gfx::Rect bar{b.x + 30 + codeW, y + codeH * 0.5f - 4.f, std::max(40.f, b.w - codeW - 56), 8};
        ctx.r.fillRect(bar, c.inputBg);
        ctx.r.fillRect({bar.x, bar.y, bar.w * frac, bar.h}, left <= 5 ? c.warning : c.accent);
        ctx.r.drawText({bar.x, bar.y + 14}, "valable encore " + std::to_string(left) + " s", ctx.theme.font.smallUi, c.textMuted);
        ctx.r.drawText({b.x + 14, y + codeH + 12},
                       "L'utilisateur le lit sur son application d'authentification (secret partag\xC3\xA9, voir la fiche).",
                       ctx.theme.font.smallUi, c.textMuted);
        invalidate();
    }
private:
    hmi::DocumentPtr doc_;
    Id               user_{kNoId};
};

namespace {
enum UserAction : int { UNewUser = 1, UNewGroup, UNewRole, UDelete, UPassword, USecret, UEnable, UUnlock };

// Lot 13 : "2026-09-25 14:05:12.350" -> "25/09 14:05".
std::string shortDate(const std::string& s) {
    if (s.size() < 16) return s;
    return s.substr(8, 2) + "/" + s.substr(5, 2) + " " + s.substr(11, 5);
}

std::string protectionLabel(const hmi::User& u) {
    if (u.protection == "dynamique") return "code dynamique";
    if (u.protection == "expression") return "expression";
    return "mot de passe";
}

std::string permissionsOf(const hmi::Project& p, const hmi::User& u) {
    std::vector<std::string> out;
    for (const auto& perm : hmi::permissionNames())
        if (hmi::userHas(p, u, perm)) out.push_back(perm);
    return out.empty() ? std::string("aucune") : joinList(out, ", ");
}

// Le secret en base32 (RFC 4648, sans remplissage) : ce que les applications
// d'authentification demandent a la saisie manuelle.
std::string base32(const std::string& hex) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    const std::string raw = hmi::fromHex(hex);
    std::string out;
    unsigned buffer = 0;
    int bits = 0;
    for (unsigned char c : raw) {
        buffer = (buffer << 8) | c;
        bits += 8;
        while (bits >= 5) {
            out += alphabet[(buffer >> (bits - 5)) & 31u];
            bits -= 5;
        }
    }
    if (bits > 0) out += alphabet[(buffer << (5 - bits)) & 31u];
    return out;
}

// Le nom d'un champ, tel que la fiche l'ecrit (pour les messages).
std::string fieldLabel(const std::string& field) {
    static const std::map<std::string, std::string> labels = {
        {"depart", "utilisateur au d\xC3\xA9marrage"}, {"periode", "p\xC3\xA9riode du code (s)"}, {"chiffres", "chiffres du code"},
        {"deconnexion", "d\xC3\xA9" "connexion automatique (min)"}, {"nom", "nom"}, {"groupe", "groupe"},
        {"protection", "protection"}, {"expression", "expression d'autorisation"}, {"actif", "actif"},
        {"description", "description"}, {"niveau", "niveau"}, {"roles", "r\xC3\xB4les"},
        {"vue_demarrage", "vue de d\xC3\xA9marrage"},                   // lot 12
        // lot 13
        {"mdp_longueur", "longueur minimale"}, {"mdp_chiffre", "un chiffre"}, {"mdp_lettre", "une lettre"},
        {"mdp_casse", "majuscules et minuscules"}, {"mdp_special", "un caract\xC3\xA8re sp\xC3\xA9" "cial"},
        {"mdp_duree", "expiration (jours)"}, {"mdp_historique", "derniers mots de passe interdits"},
        {"mdp_premiere", "changer \xC3\xA0 la premi\xC3\xA8re connexion"}, {"verrou_essais", "verrouillage apr\xC3\xA8s (\xC3\xA9" "checs)"},
        {"verrou_minutes", "dur\xC3\xA9" "e du verrouillage (min)"}, {"avertir", "avertissement (s)"}, {"badge", "connexion par badge"},
        {"changer", "\xC3\xA0 changer \xC3\xA0 la prochaine connexion"}};
    const auto it = labels.find(field);
    return it == labels.end() ? field : it->second;
}

std::vector<std::string> groupNames(const hmi::Project& p) {
    std::vector<std::string> out;
    for (const auto& g : p.security.groups) out.push_back(g.name);
    return out;
}
} // namespace

HmiUsersPane::HmiUsersPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(UNewUser, HmiGlyph::User, "Nouvel utilisateur", "Utilisateur");
    tools->add(UNewGroup, HmiGlyph::Plus, "Nouveau groupe", "Groupe");
    tools->add(UNewRole, HmiGlyph::Plus, "Nouveau r\xC3\xB4le", "R\xC3\xB4le");
    tools->add(UDelete, HmiGlyph::Delete, "Supprimer l'\xC3\xA9l\xC3\xA9ment choisi (Ctrl+Z le rend)", "Supprimer");
    tools->separator();
    tools->add(UPassword, HmiGlyph::Key, "D\xC3\xA9" "finir le mot de passe (gard\xC3\xA9 sous forme d'empreinte sal\xC3\xA9" "e)", "Mot de passe");
    tools->add(USecret, HmiGlyph::Refresh, "Nouveau secret pour le code dynamique", "Nouveau secret");
    tools->separator();
    tools->add(UUnlock, HmiGlyph::Unlock, "D\xC3\xA9verrouiller le compte choisi (trop d'\xC3\xA9" "checs de connexion)", "D\xC3\xA9verrouiller");
    tools->separator();
    tools->add(UEnable, HmiGlyph::Lock, "S\xC3\xA9" "curit\xC3\xA9 active : niveaux d'acc\xC3\xA8s et permissions appliqu\xC3\xA9s en marche",
               "S\xC3\xA9" "curit\xC3\xA9 active");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(UUnlock, [this] {
        const auto* u = doc_->project.user(selectedUser());
        const auto* st = u ? doc_->history.account(u->login) : nullptr;
        return currentTab() == Users && st && !st->lockedAt.empty();
    });
    tools_->setEnabledWhen(UDelete, [this] {
        switch (currentTab()) {
            case Users: return selectedUser() != kNoId;
            case Groups: return selectedGroup() != kNoId;
            default: return !selectedRole().empty();
        }
    });
    tools_->setEnabledWhen(UPassword, [this] {
        const auto* u = doc_->project.user(selectedUser());
        return currentTab() == Users && u && u->protection == "classique";
    });
    tools_->setEnabledWhen(USecret, [this] {
        const auto* u = doc_->project.user(selectedUser());
        return currentTab() == Users && u && u->protection == "dynamique";
    });
    tools_->setCheckedWhen(UEnable, [this] { return doc_->project.security.enabled; });
    // Lot API 8 : chercher dans l'onglet montre - la recherche de toute l'application.
    search_ = &static_cast<ui::SearchField&>(addChild(std::make_unique<ui::SearchField>(
        base + ".search", "Rechercher : login, nom, groupe, r\xC3\xB4le, permission, description\xE2\x80\xA6",
        "Chaque mot est cherch\xC3\xA9 dans l'onglet montr\xC3\xA9 : un utilisateur (login, nom, groupe, protection, \xC3\xA9tat, permissions, "
        "description), un groupe (nom, r\xC3\xB4les, description), un r\xC3\xB4le (nom, permissions, groupes, description) - tous les mots (ET), "
        "sans casse ni accents ; \"une phrase\" entre guillemets ; -mot : l'exclure.")));

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    {
        auto t = std::make_unique<ui::TableView>(base + ".users");
        t->setColumns({{"Login", 130.f}, {"Nom", 170.f}, {"Groupe", 120.f}, {"Niveau", 76.f, 40.f, true, true, true, ui::Align::End},
                       {"Protection", 130.f}, {"\xC3\x89tat", 170.f}, {"Permissions", 320.f}});
        t->setSelectionMode(ui::SelectionMode::Single);
        users_ = t.get();
        tabs->addTab(ui::TabControl::Tab{"Utilisateurs", ui::Icon::User, false, false}, std::move(t));
    }
    {
        auto t = std::make_unique<ui::TableView>(base + ".groups");
        t->setColumns({{"Groupe", 150.f}, {"Niveau", 76.f, 40.f, true, true, true, ui::Align::End}, {"R\xC3\xB4les", 260.f},
                       {"Utilisateurs", 90.f, 40.f, true, true, true, ui::Align::End}, {"Description", 260.f}});
        t->setSelectionMode(ui::SelectionMode::Single);
        groups_ = t.get();
        tabs->addTab(ui::TabControl::Tab{"Groupes", ui::Icon::Folder, false, false}, std::move(t));
    }
    {
        auto t = std::make_unique<ui::TableView>(base + ".roles");
        t->setColumns({{"R\xC3\xB4le", 140.f}, {"Permissions", 300.f}, {"Groupes", 220.f}, {"Description", 260.f}});
        t->setSelectionMode(ui::SelectionMode::Single);
        roles_ = t.get();
        tabs->addTab(ui::TabControl::Tab{"R\xC3\xB4les et permissions", ui::Icon::Lock, false, false}, std::move(t));
    }
    tabs->setCurrentIndex(0);
    tabs_ = &static_cast<ui::TabControl&>(split->addPane(std::move(tabs), 0.62f, 360.f));
    auto side = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".side");
    grid_ = &static_cast<ui::PropertyGrid&>(side->addPane(std::make_unique<ui::PropertyGrid>(base + ".grid"), 0.76f, 200.f));
    grid_->setFieldAssist(assist::gridAssist(assist::sourcesFor(doc_)));
    code_ = &static_cast<HmiDynamicCodePanel&>(side->addPane(std::make_unique<HmiDynamicCodePanel>(base + ".code", doc_), 0.24f, 110.f));
    side_ = &static_cast<ui::Splitter&>(split->addPane(std::move(side), 0.38f, 280.f));
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case UNewUser: {
                const Id u = addUser();
                if (u && hosts_.password) hosts_.password(u);
                break;
            }
            case UNewGroup: (void)addGroup(); break;
            case UNewRole: (void)addRole(); break;
            case UDelete:
                if (currentTab() == Users && selectedUser()) {
                    if (hosts_.removeUser) hosts_.removeUser(selectedUser());
                    else (void)deleteUser(selectedUser());
                } else if (currentTab() == Groups && selectedGroup()) {
                    if (hosts_.removeGroup) hosts_.removeGroup(selectedGroup());
                    else (void)deleteGroup(selectedGroup());
                } else if (currentTab() == Roles && !selectedRole().empty()) {
                    (void)deleteRole(selectedRole());
                }
                break;
            case UPassword: if (selectedUser() && hosts_.password) hosts_.password(selectedUser()); break;
            case USecret: if (selectedUser()) (void)newSecret(selectedUser()); break;
            case UUnlock: if (selectedUser()) (void)unlockUser(selectedUser()); break;
            case UEnable: (void)setSecurity("active", doc_->project.security.enabled ? "FALSE" : "TRUE"); break;
            default: break;
        }
    });
    for (auto* t : {users_, groups_, roles_})
        links_ += t->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
            if (!refreshing_) rebuildProperties();
        });
    links_ += tabs_->currentChanged->connect([this](std::size_t) { if (!refreshing_) rebuildProperties(); });
    // Lot 20 : coller des utilisateurs depuis Excel (le login dit lequel). Le
    // mot de passe ne se colle pas : il se donne ici, utilisateur par utilisateur.
    users_->setSelectionMode(ui::SelectionMode::Extended);
    paste_.table = users_;
    paste_.keyColumn = 0;
    paste_.refresh = [this] { refresh(); };
    paste_.done = [this](const paste::Report& rep, const paste::Target& target) {
        say(rep.status(target), !rep.error.empty() || !rep.refused.empty());
    };
    paste_.target = [this](const ui::TableView::PasteRequest& rq) {
        paste::Target tg;
        tg.noun = "utilisateur";
        tg.nouns = "utilisateurs";
        tg.feminine = false;
        const auto idOf = [this](const std::string& k) -> Id {
            const auto* u = doc_->project.userByLogin(k);
            return u ? u->id : kNoId;
        };
        const auto field = [this, idOf](std::string name) {
            return [this, idOf, name](const std::string& k, const std::string& v, std::string* why) {
                return setUserField(idOf(k), name, v, why);
            };
        };
        tg.columns.push_back(paste::column("Login", {"Identifiant", "Utilisateur", "User", "Compte"}, 0, nullptr, true));
        tg.columns.push_back(paste::column("Nom", {"Nom complet", "Name", "Full name", "Prenom Nom"}, 1, field("nom")));
        tg.columns.push_back(paste::column("Groupe", {"Group", "Profil"}, 2, field("groupe")));
        tg.columns.push_back(paste::column("Niveau", {}, 3, nullptr));
        tg.columns.push_back(paste::column("Protection", {"Mode"}, 4, field("protection")));
        tg.columns.push_back(paste::column("\xC3\x89tat", {}, 5, nullptr));
        tg.columns.push_back(paste::column("Permissions", {}, 6, nullptr));
        tg.columns.push_back(paste::column("Description", {"Commentaire", "Fonction"}, -1, field("description")));
        tg.columns.push_back(paste::column("Actif", {"Active", "Enabled"}, -1, field("actif")));
        tg.exists = [this](const std::string& k) { return doc_->project.userByLogin(k) != nullptr; };
        tg.freeKey = [this](const std::string& k) { return hmi::isIdentifier(k) ? hmi::uniqueLogin(doc_->project, k) : k; };
        tg.create = [this](const std::string& k, const std::map<std::string, std::string>& cells, paste::Notes& notes,
                           std::vector<std::string>& used, std::string* why) -> std::string {
            std::string group;
            if (const auto it = cells.find("groupe"); it != cells.end()) {
                if (doc_->project.groupByName(it->second)) group = it->second;
                else notes.push_back({"Groupe", "groupe inconnu : " + it->second + " \xE2\x80\x94 l'utilisateur est dans le premier groupe"});
                used.push_back("groupe");
            }
            const Id made = addUser(k, group, why);
            if (made == kNoId) return {};
            const auto* u = doc_->project.user(made);
            return u ? u->login : k;
        };
        tg.keysFromAnchor = paste::keysFrom(*users_, rq.anchorViewRow, 0);
        return tg;
    };
    paste::bind(paste_);
    links_ += doc_->changed->connect([this](Id) { refresh(); paste::forget(paste_); });
    refresh();
    // Lot API 8 : la recherche, branchee, puis relue (retenue d'une seance a
    // l'autre) ; son compte suit l'onglet montre.
    links_ += search_->changed->connect([this] { refresh(); });
    links_ += tabs_->currentChanged->connect([this](std::size_t) { updateSearchCount(); });
    search_->recall();
}

// ---- Lot API 8 : chercher dans l'onglet montre ----
std::size_t HmiUsersPane::shownRows() const {
    switch (currentTab()) {
        case Users: return userOrder_.size();
        case Groups: return groupOrder_.size();
        default: return roleOrder_.size();
    }
}

void HmiUsersPane::updateSearchCount() {
    const auto& sec = doc_->project.security;
    const std::size_t total = currentTab() == Users ? sec.users.size() : currentTab() == Groups ? sec.groups.size() : sec.roles.size();
    search_->setCount(shownRows(), total);
}
// ---- fin Lot API 8 ----

void HmiUsersPane::refresh() {
    const Id keepUser = selectedUser(), keepGroup = selectedGroup();
    const std::string keepRole = selectedRole();
    const auto& p = doc_->project;
    const auto& sec = p.security;
    const ui::SearchQuery& query = search_->query();      // lot API 8
    refreshing_ = true;
    {
        std::vector<std::vector<std::string>> rows;
        std::vector<bool> off, missing;
        userOrder_.clear();
        for (const auto& u : sec.users) {
            const auto* g = p.group(u.group);
            std::string state = u.enabled ? "actif" : "d\xC3\xA9sactiv\xC3\xA9";
            if (u.protection == "classique" && u.passwordHash.empty()) state = "sans mot de passe";
            if (u.protection == "dynamique" && u.secret.empty()) state = "sans secret";
            // Lot 13 : verrouille (l'historique le garde), a changer, un badge.
            if (const auto* st = doc_->history.account(u.login); st && !st->lockedAt.empty()) state = "verrouill\xC3\xA9";
            if (u.mustChange) state += " \xC2\xB7 \xC3\xA0 changer";
            if (!u.badge.empty()) state += " \xC2\xB7 badge";
            if (same(sec.startUser, u.login)) state += " \xC2\xB7 d\xC3\xA9marrage";
            std::vector<std::string> row{u.login, u.fullName, g ? g->name : std::string("(aucun)"), g ? std::to_string(g->level) : "0",
                                         protectionLabel(u), state, permissionsOf(p, u)};
            // Lot API 8 : la recherche - ses cases, et sa description.
            if (!query.empty()) {
                std::vector<std::string> texts = row;
                texts.push_back(u.description);
                if (!query.matches(texts)) continue;
            }
            userOrder_.push_back(u.id);
            rows.push_back(std::move(row));
            off.push_back(!u.enabled);
            missing.push_back(state.rfind("sans", 0) == 0 || state.rfind("verrouill", 0) == 0);
        }
        usersModel_ = std::make_shared<Rows>(std::vector<std::string>{"Login", "Nom", "Groupe", "Niveau", "Protection", "\xC3\x89tat", "Permissions"},
                                             std::move(rows), [off, missing](ui::RowIndex r, std::size_t c) {
                                                 ui::CellStyle s;
                                                 if (c == 0) { s.icon = ui::Icon::User; s.bold = true; }
                                                 if (r < off.size() && off[r]) s.fgTone = ui::Tone::Muted;
                                                 // Sans mot de passe / sans secret : il ne pourra pas se connecter.
                                                 if (c == 5 && r < missing.size() && missing[r]) s.fgTone = ui::Tone::Warning;
                                                 if (c == 0 && r < missing.size() && missing[r]) s.iconTone = ui::Tone::Warning;
                                                 return s;
                                             });
        users_->setModel(usersModel_);
    }
    {
        std::vector<std::vector<std::string>> rows;
        groupOrder_.clear();
        for (const auto& g : sec.groups) {
            std::size_t n = 0;
            for (const auto& u : sec.users) n += u.group == g.id;
            std::vector<std::string> row{g.name, std::to_string(g.level), joinList(g.roles, ", "), std::to_string(n), g.description};
            if (!query.empty() && !query.matches(row)) continue;        // lot API 8 : la recherche
            groupOrder_.push_back(g.id);
            rows.push_back(std::move(row));
        }
        groupsModel_ = std::make_shared<Rows>(std::vector<std::string>{"Groupe", "Niveau", "R\xC3\xB4les", "Utilisateurs", "Description"},
                                              std::move(rows), [](ui::RowIndex, std::size_t c) {
                                                  ui::CellStyle s;
                                                  if (c == 0) { s.icon = ui::Icon::Folder; s.bold = true; }
                                                  return s;
                                              });
        groups_->setModel(groupsModel_);
    }
    {
        std::vector<std::vector<std::string>> rows;
        roleOrder_.clear();
        for (const auto& r : sec.roles) {
            std::vector<std::string> in;
            for (const auto& g : sec.groups)
                for (const auto& name : g.roles) if (same(name, r.name)) in.push_back(g.name);
            std::vector<std::string> row{r.name, joinList(r.permissions, ", "), joinList(in, ", "), r.description};
            if (!query.empty() && !query.matches(row)) continue;        // lot API 8 : la recherche
            roleOrder_.push_back(r.name);
            rows.push_back(std::move(row));
        }
        rolesModel_ = std::make_shared<Rows>(std::vector<std::string>{"R\xC3\xB4le", "Permissions", "Groupes", "Description"},
                                             std::move(rows), [](ui::RowIndex, std::size_t c) {
                                                 ui::CellStyle s;
                                                 if (c == 0) { s.icon = ui::Icon::Lock; s.bold = true; }
                                                 return s;
                                             });
        roles_->setModel(rolesModel_);
    }
    refreshing_ = false;
    // Lot API 8 : ce qui etait choisi, si la recherche le montre encore (une
    // recherche tapee ne s'efface pas d'elle-meme) ; les termes surlignes ; les
    // onglets disent "3/12" pendant une recherche.
    const auto shown = [](const auto& order, const auto& key) { return std::find(order.begin(), order.end(), key) != order.end(); };
    if (keepUser && shown(userOrder_, keepUser)) selectUser(keepUser);
    if (keepGroup && shown(groupOrder_, keepGroup)) selectGroup(keepGroup);
    if (!keepRole.empty() && shown(roleOrder_, keepRole)) selectRole(keepRole);
    for (auto* t : {users_, groups_, roles_}) t->setHighlight(search_->text());
    const auto badge = [&](std::size_t n, std::size_t total) {
        return query.empty() ? std::to_string(total) : std::to_string(n) + "/" + std::to_string(total);
    };
    tabs_->setTabBadge(0, badge(userOrder_.size(), sec.users.size()), ui::Tone::Accent);
    tabs_->setTabBadge(1, badge(groupOrder_.size(), sec.groups.size()), ui::Tone::Accent);
    tabs_->setTabBadge(2, badge(roleOrder_.size(), sec.roles.size()), ui::Tone::Accent);
    updateSearchCount();
    std::string msg = sec.enabled ? "S\xC3\xA9" "curit\xC3\xA9 ACTIVE" : "S\xC3\xA9" "curit\xC3\xA9 inactive (tout est permis en marche)";
    msg += " \xC2\xB7 " + std::to_string(sec.users.size()) + " utilisateur(s), " + std::to_string(sec.groups.size()) + " groupe(s), "
         + std::to_string(sec.roles.size()) + " r\xC3\xB4le(s)";
    msg += " \xC2\xB7 au d\xC3\xA9marrage : " + (sec.startUser.empty() ? std::string("personne (niveau 0)") : sec.startUser);
    status_->setMessage(msg, sec.enabled ? ui::StatusBar::Severity::Success : ui::StatusBar::Severity::Info);
    rebuildProperties();
    invalidate();
}

void HmiUsersPane::showTab(int tab) { tabs_->setCurrentIndex(static_cast<std::size_t>(std::clamp(tab, 0, 2))); }
int HmiUsersPane::currentTab() const { return static_cast<int>(tabs_->currentIndex()); }

Id HmiUsersPane::selectedUser() const {
    const int r = selectedRow(*users_);
    return r >= 0 && static_cast<std::size_t>(r) < userOrder_.size() ? userOrder_[static_cast<std::size_t>(r)] : kNoId;
}
Id HmiUsersPane::selectedGroup() const {
    const int r = selectedRow(*groups_);
    return r >= 0 && static_cast<std::size_t>(r) < groupOrder_.size() ? groupOrder_[static_cast<std::size_t>(r)] : kNoId;
}
std::string HmiUsersPane::selectedRole() const {
    const int r = selectedRow(*roles_);
    return r >= 0 && static_cast<std::size_t>(r) < roleOrder_.size() ? roleOrder_[static_cast<std::size_t>(r)] : std::string{};
}
void HmiUsersPane::selectUser(Id id) {
    // Lot API 8 : demande (cree, renomme) mais cache par la recherche : elle s'efface.
    if (id != kNoId && !search_->text().empty() && doc_->project.user(id)
        && std::find(userOrder_.begin(), userOrder_.end(), id) == userOrder_.end())
        search_->setText("");
    for (std::size_t i = 0; i < userOrder_.size(); ++i)
        if (userOrder_[i] == id) { users_->selectModelRows({static_cast<ui::RowIndex>(i)}); return; }
}
void HmiUsersPane::selectGroup(Id id) {
    if (id != kNoId && !search_->text().empty() && doc_->project.group(id)       // lot API 8 (voir selectUser)
        && std::find(groupOrder_.begin(), groupOrder_.end(), id) == groupOrder_.end())
        search_->setText("");
    for (std::size_t i = 0; i < groupOrder_.size(); ++i)
        if (groupOrder_[i] == id) { groups_->selectModelRows({static_cast<ui::RowIndex>(i)}); return; }
}
void HmiUsersPane::selectRole(const std::string& name) {
    if (!name.empty() && !search_->text().empty() && doc_->project.role(name)      // lot API 8 (voir selectUser)
        && std::none_of(roleOrder_.begin(), roleOrder_.end(), [&](const std::string& r) { return same(r, name); }))
        search_->setText("");
    for (std::size_t i = 0; i < roleOrder_.size(); ++i)
        if (same(roleOrder_[i], name)) { roles_->selectModelRows({static_cast<ui::RowIndex>(i)}); return; }
}

void HmiUsersPane::say(std::string text, bool warning) {
    message_ = text;
    status_->setTransientMessage(std::move(text), 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

bool HmiUsersPane::change(const std::string& label, const std::function<bool(hmi::Project&, std::string&)>& f, std::string* why) {
    // L'essai d'abord, sur une copie : une modification refusee ne laisse ni
    // commande vide ni projet a moitie change.
    hmi::Project trial = doc_->project;
    std::string error;
    if (!f(trial, error)) {
        if (why) *why = error;
        say(error, true);
        return false;
    }
    auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& p) {
        std::string ignored;
        (void)f(p, ignored);
    });
    if (cmd) apply_(std::move(cmd));
    return true;
}

void HmiUsersPane::rebuildProperties() {
    const auto& p = doc_->project;
    const auto& sec = p.security;
    std::vector<PG::Category> cats;
    PG::Category s;
    s.name = "S\xC3\xA9" "curit\xC3\xA9";
    const auto secCommit = [this](const char* f) {
        return [this, f](std::string_view v) { return setSecurity(f, std::string(v)); };
    };
    s.properties.push_back(prop("S\xC3\xA9" "curit\xC3\xA9 active", tf(sec.enabled), PG::ValueType::Boolean, secCommit("active"),
                                "Inactive : tout est permis en marche. Active : les niveaux d'acc\xC3\xA8s des objets et les "
                                "permissions des r\xC3\xB4les s'appliquent."));
    std::vector<std::string> logins{""};
    for (const auto& u : sec.users) logins.push_back(u.login);
    s.properties.push_back(prop("Utilisateur au d\xC3\xA9marrage", sec.startUser, PG::ValueType::Enum, secCommit("depart"),
                                "Connect\xC3\xA9 au lancement de l'IHM. Vide : personne (niveau 0).", logins));
    s.properties.push_back(prop("Code dynamique : p\xC3\xA9riode (s)", std::to_string(sec.dynamicPeriodS), PG::ValueType::Integer,
                                secCommit("periode"), "Un nouveau code toutes les N secondes (30 : l'usage)."));
    s.properties.push_back(prop("Code dynamique : chiffres", std::to_string(sec.dynamicDigits), PG::ValueType::Integer,
                                secCommit("chiffres"), "6 \xC3\xA0 8."));
    s.properties.push_back(prop("D\xC3\xA9" "connexion automatique (min)", std::to_string(sec.autoLogoutMin), PG::ValueType::Integer,
                                secCommit("deconnexion"), "Apr\xC3\xA8s N minutes sans un clic. 0 : jamais."));
    cats.push_back(std::move(s));
    // Lot 12 : le menu natif de connexion - le niveau a partir duquel chaque
    // onglet se voit ; y changer quelque chose demande toujours Administrer.
    PG::Category m;
    m.name = "Menu de connexion";
    m.properties.push_back(prop("Onglet Comptes d\xC3\xA8s le niveau", std::to_string(sec.menuLevelAccounts), PG::ValueType::Integer,
                                secCommit("menu_comptes"),
                                "Le niveau (1 \xC3\xA0 4) \xC3\xA0 partir duquel l'onglet Comptes se montre. La permission Administrer "
                                "montre tout ; ajouter, activer, changer de groupe ou supprimer la demande toujours."));
    m.properties.push_back(prop("Onglet Acc\xC3\xA8s d\xC3\xA8s le niveau", std::to_string(sec.menuLevelAccess), PG::ValueType::Integer,
                                secCommit("menu_acces"),
                                "Les r\xC3\xB4les de chaque groupe et la d\xC3\xA9" "connexion automatique : visibles \xC3\xA0 partir de ce niveau, "
                                "modifiables avec Administrer."));
    m.properties.push_back(prop("Onglet Journal d\xC3\xA8s le niveau", std::to_string(sec.menuLevelJournal), PG::ValueType::Integer,
                                secCommit("menu_journal"), "Les connexions, les d\xC3\xA9" "connexions, les refus et les comptes chang\xC3\xA9s."));
    cats.push_back(std::move(m));
    // Lot 13 : la politique des mots de passe, le verrouillage, l'avertissement, le badge.
    PG::Category pw;
    pw.name = "Politique des mots de passe";
    pw.properties.push_back(prop("Longueur minimale", std::to_string(sec.pwMinLength), PG::ValueType::Integer, secCommit("mdp_longueur"),
                                 "0 : celle de chaque endroit (4 dans l'\xC3\xA9" "diteur, 6 en marche). Sinon la plus grande des deux."));
    pw.properties.push_back(prop("Au moins un chiffre", tf(sec.pwDigit), PG::ValueType::Boolean, secCommit("mdp_chiffre")));
    pw.properties.push_back(prop("Au moins une lettre", tf(sec.pwLetter), PG::ValueType::Boolean, secCommit("mdp_lettre")));
    pw.properties.push_back(prop("Majuscules et minuscules", tf(sec.pwMixedCase), PG::ValueType::Boolean, secCommit("mdp_casse")));
    pw.properties.push_back(prop("Un caract\xC3\xA8re sp\xC3\xA9" "cial", tf(sec.pwSpecial), PG::ValueType::Boolean, secCommit("mdp_special"),
                                 "Ni lettre ni chiffre : ! # - _ @..."));
    pw.properties.push_back(prop("Expire apr\xC3\xA8s (jours)", std::to_string(sec.pwMaxAgeDays), PG::ValueType::Integer, secCommit("mdp_duree"),
                                 "0 : jamais. P\xC3\xA9rim\xC3\xA9, la connexion demande d'abord un nouveau mot de passe."));
    pw.properties.push_back(prop("Derniers mots de passe interdits", std::to_string(sec.pwHistory), PG::ValueType::Integer,
                                 secCommit("mdp_historique"), "Les N derniers (celui du moment compris) ne reviennent pas. 0 : aucun contr\xC3\xB4le."));
    pw.properties.push_back(prop("\xC3\x80 changer \xC3\xA0 la premi\xC3\xA8re connexion", tf(sec.pwChangeFirst), PG::ValueType::Boolean,
                                 secCommit("mdp_premiere"),
                                 "Un mot de passe donn\xC3\xA9 par un administrateur (ici ou dans la gestion des comptes) se change \xC3\xA0 la "
                                 "premi\xC3\xA8re connexion de son titulaire."));
    pw.properties.push_back(prop("R\xC3\xA8gle en clair", hmi::passwordRules(sec, 6), PG::ValueType::ReadOnly, {},
                                 "Ce que demande le menu de connexion (l'\xC3\xA9" "diteur : 4 caract\xC3\xA8res au moins, et le reste)."));
    cats.push_back(std::move(pw));
    PG::Category lk;
    lk.name = "Verrouillage et session";
    lk.properties.push_back(prop("Verrouiller apr\xC3\xA8s (\xC3\xA9" "checs)", std::to_string(sec.lockAttempts), PG::ValueType::Integer,
                                 secCommit("verrou_essais"), "N \xC3\xA9" "checs de suite (mot de passe, code, signature) verrouillent le compte. 0 : jamais."));
    lk.properties.push_back(prop("Dur\xC3\xA9" "e du verrouillage (min)", std::to_string(sec.lockMinutes), PG::ValueType::Integer,
                                 secCommit("verrou_minutes"), "0 : jusqu'\xC3\xA0 ce qu'un administrateur le d\xC3\xA9verrouille."));
    lk.properties.push_back(prop("Avertir avant la d\xC3\xA9" "connexion (s)", std::to_string(sec.logoutWarnS), PG::ValueType::Integer,
                                 secCommit("avertir"),
                                 "Les derni\xC3\xA8res secondes avant la d\xC3\xA9" "connexion automatique : un bandeau compte ; un toucher garde "
                                 "l'utilisateur connect\xC3\xA9. 0 : sans avertissement."));
    lk.properties.push_back(prop("Connexion par badge", tf(sec.badgeLogin), PG::ValueType::Boolean, secCommit("badge"),
                                 "Un lecteur de badge qui tape comme un clavier (le num\xC3\xA9ro, puis Entr\xC3\xA9" "e) connecte le compte dont "
                                 "c'est le badge - hors de tout champ, ou dans le menu de connexion."));
    cats.push_back(std::move(lk));

    Id codeUser = kNoId;
    if (currentTab() == Users) {
        if (const auto* u = p.user(selectedUser())) {
            const Id id = u->id;
            codeUser = id;
            const auto uc = [this, id](const char* f) {
                return [this, id, f](std::string_view v) { return setUserField(id, f, std::string(v)); };
            };
            PG::Category c;
            c.name = "Utilisateur : " + u->login;
            c.properties.push_back(prop("Login", u->login, PG::ValueType::Text, uc("login"),
                                        "Unique, sans distinction de casse. Les actions \xC2\xAB Changer d'utilisateur \xC2\xBB suivent."));
            c.properties.push_back(prop("Nom complet", u->fullName, PG::ValueType::Text, uc("nom")));
            const auto* g = p.group(u->group);
            c.properties.push_back(prop("Groupe", g ? g->name : std::string{}, PG::ValueType::Enum, uc("groupe"),
                                        "Le groupe donne le niveau d'acc\xC3\xA8s et les r\xC3\xB4les.", groupNames(p)));
            c.properties.push_back(prop("Niveau", std::to_string(hmi::userLevel(p, *u)), PG::ValueType::ReadOnly));
            c.properties.push_back(prop("Protection", u->protection, PG::ValueType::Enum, uc("protection"),
                                        "classique : mot de passe (empreinte sal\xC3\xA9" "e) ; dynamique : code \xC3\xA0 6 chiffres "
                                        "qui change toutes les 30 s ; expression : connexion permise tant qu'elle est vraie.",
                                        hmi::protectionNames()));
            if (u->protection == "classique") {
                c.properties.push_back(prop("Mot de passe", u->passwordHash.empty() ? std::string("aucun : bouton Mot de passe")
                                                                                   : "d\xC3\xA9" "fini (empreinte " + u->passwordHash.substr(0, 12) + "...)",
                                            PG::ValueType::ReadOnly, {}, "Jamais gard\xC3\xA9 en clair : SHA-256 sal\xC3\xA9, 4096 tours."));
                // Lot 13 : sa date, sa peremption, le changement exige.
                if (!u->passwordHash.empty()) {
                    std::string when = u->passwordSet.empty() ? std::string("date inconnue") : "le " + u->passwordSet;
                    if (const auto left = hmi::passwordDaysLeft(sec, *u, hmi::dayOf(hmi::wallStamp())))
                        when += *left >= 0 ? " \xC2\xB7 expire dans " + std::to_string(*left) + " jour(s)" : std::string(" \xC2\xB7 EXPIR\xC3\x89");
                    if (!u->previous.empty()) when += " \xC2\xB7 " + std::to_string(u->previous.size()) + " pr\xC3\xA9" "c\xC3\xA9" "dent(s) gard\xC3\xA9(s)";
                    c.properties.push_back(prop("Donn\xC3\xA9", when, PG::ValueType::ReadOnly, {},
                                                "Le jour o\xC3\xB9 le mot de passe a \xC3\xA9t\xC3\xA9 donn\xC3\xA9 ; la politique dit quand il expire."));
                }
                c.properties.push_back(prop("\xC3\x80 changer \xC3\xA0 la prochaine connexion", tf(u->mustChange), PG::ValueType::Boolean, uc("changer"),
                                            "Le menu de connexion demandera un nouveau mot de passe avant d'entrer."));
            }
            if (u->protection == "dynamique") {
                c.properties.push_back(prop("Secret partag\xC3\xA9", u->secret.empty() ? std::string("aucun : bouton Nouveau secret")
                                                                                       : u->secret.substr(0, 8) + "... (" + std::to_string(u->secret.size() / 2) + " octets)",
                                            PG::ValueType::ReadOnly, {}, "\xC3\x80 donner \xC3\xA0 l'application d'authentification de l'utilisateur."));
                if (!u->secret.empty())
                    c.properties.push_back(prop("Secret (base32)", base32(u->secret), PG::ValueType::ReadOnly, {},
                                                "La cl\xC3\xA9 \xC3\xA0 saisir dans l'application d'authentification (TOTP, "
                                                "algorithme SHA-256, " + std::to_string(p.security.dynamicDigits) + " chiffres, "
                                                + std::to_string(p.security.dynamicPeriodS) + " s)."));
            }
            if (u->protection == "expression" || !u->expression.empty())
                c.properties.push_back(prop("Expression d'autorisation", u->expression, PG::ValueType::Text, uc("expression"),
                                            "Connexion permise tant qu'elle est vraie : Cle_Maintenance, Badge = 1234"));
            // Lot 13 : le badge (ecrire son numero ; il n'est garde qu'en empreinte).
            c.properties.push_back(prop("Badge", u->badge.empty() ? std::string{} : std::string("\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2 (d\xC3\xA9" "fini)"),
                                        PG::ValueType::Text, [this, id](std::string_view v) { return setBadge(id, std::string(v)); },
                                        "Le num\xC3\xA9ro du badge : tape-le (ou passe le badge sur le lecteur) ; il est gard\xC3\xA9 sous forme "
                                        "d'empreinte sal\xC3\xA9" "e, jamais en clair. Vide : pas de badge."));
            if (const auto* st = doc_->history.account(u->login); st && (st->failures > 0 || !st->lockedAt.empty())) {
                std::string state = !st->lockedAt.empty() ? "verrouill\xC3\xA9 depuis " + shortDate(st->lockedAt)
                                                          : std::to_string(st->failures) + " \xC3\xA9" "chec(s) de suite";
                if (!st->lockedAt.empty()) state += " (" + std::to_string(st->failures) + " \xC3\xA9" "checs)";
                c.properties.push_back(prop("\xC3\x89tat du compte", state, PG::ValueType::ReadOnly, {},
                                            "Les \xC3\xA9" "checs de connexion de suite (la simulation les compte). D\xC3\xA9verrouiller : le bouton de la barre."));
            }
            c.properties.push_back(prop("Actif", tf(u->enabled), PG::ValueType::Boolean, uc("actif"), "D\xC3\xA9sactiv\xC3\xA9 : ne peut plus se connecter."));
            c.properties.push_back(prop("Description", u->description, PG::ValueType::Text, uc("description")));
            c.properties.push_back(prop("Permissions", permissionsOf(p, *u), PG::ValueType::ReadOnly));
            cats.push_back(std::move(c));
        }
    } else if (currentTab() == Groups) {
        if (const auto* g = p.group(selectedGroup())) {
            const Id id = g->id;
            const auto gc = [this, id](const char* f) {
                return [this, id, f](std::string_view v) { return setGroupField(id, f, std::string(v)); };
            };
            PG::Category c;
            c.name = "Groupe : " + g->name;
            c.properties.push_back(prop("Nom", g->name, PG::ValueType::Text, gc("nom")));
            c.properties.push_back(prop("Niveau", std::to_string(g->level), PG::ValueType::Integer, gc("niveau"),
                                        "1 op\xC3\xA9rateur ... 4 administrateur. Un objet de niveau d'acc\xC3\xA8s N ne r\xC3\xA9pond "
                                        "qu'\xC3\xA0 partir du niveau N."));
            std::vector<std::string> names;
            for (const auto& r : p.security.roles) names.push_back(r.name);
            c.properties.push_back(prop("R\xC3\xB4les (a; b)", joinList(g->roles), PG::ValueType::Text, gc("roles"),
                                        "Parmi : " + joinList(names, ", ")));
            c.properties.push_back(prop("Description", g->description, PG::ValueType::Text, gc("description")));
            // Lot 12 : la vue ouverte a la connexion d'un utilisateur du groupe.
            std::vector<std::string> views{"(celle du projet)"};
            for (const auto& v : p.views)
                if (v.role == "vue") views.push_back(v.name);
            const auto* sv = g->startView != hmi::kNoId ? p.view(g->startView) : nullptr;
            c.properties.push_back(prop("Vue de d\xC3\xA9marrage", sv ? sv->name : std::string("(celle du projet)"), PG::ValueType::Enum,
                                        gc("vue_demarrage"),
                                        "La vue ouverte quand un utilisateur du groupe se connecte (et au lancement pour "
                                        "l'utilisateur de d\xC3\xA9part) ; c'est aussi sa vue d'accueil (action Vue d'accueil).",
                                        views));
            std::vector<std::string> members;
            for (const auto& u : p.security.users) if (u.group == id) members.push_back(u.login);
            c.properties.push_back(prop("Utilisateurs", members.empty() ? std::string("aucun") : fewOf(members, 6), PG::ValueType::ReadOnly));
            cats.push_back(std::move(c));
        }
    } else {
        if (const auto* r = p.role(selectedRole())) {
            const std::string name = r->name;
            const auto rc = [this, name](std::string f) {
                return [this, name, f](std::string_view v) { return setRoleField(name, f, std::string(v)); };
            };
            PG::Category c;
            c.name = "R\xC3\xB4le : " + r->name;
            c.properties.push_back(prop("Nom", r->name, PG::ValueType::Text, rc("nom"), "Les groupes qui le citent suivent."));
            c.properties.push_back(prop("Description", r->description, PG::ValueType::Text, rc("description")));
            cats.push_back(std::move(c));
            PG::Category perms;
            perms.name = "Permissions";
            static const std::map<std::string, std::string> help = {
                {"Naviguer", "Changer de vue, ouvrir une popup."},
                {"Piloter", "\xC3\x89" "crire une variable : bascule, mise \xC3\xA0 1, affectation..."},
                {"Acquitter", "Acquitter les alarmes."},
                {"Recettes", "Charger une recette."},
                {"Scripts", "Lancer un script depuis une action."},
                {"Administrer", "Tout, y compris ce qui n'est pas list\xC3\xA9 ici."}};
            for (const auto& perm : hmi::permissionNames()) {
                bool on = false;
                for (const auto& x : r->permissions) on = on || same(x, perm);
                const auto it = help.find(perm);
                perms.properties.push_back(prop(perm, tf(on), PG::ValueType::Boolean, rc(perm), it == help.end() ? std::string{} : it->second));
            }
            cats.push_back(std::move(perms));
        }
    }
    grid_->setCategories(std::move(cats));
    code_->show(codeUser);
}

bool HmiUsersPane::setSecurity(const std::string& field, const std::string& raw, std::string* why) {
    const std::string value = trimmed(raw);
    const bool ok = change("S\xC3\xA9" "curit\xC3\xA9 : " + field, [&](hmi::Project& p, std::string& err) {
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
        if (!hmi::parseNumber(value, n)) { err = field + " : un nombre"; return false; }
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
    }, why);
    if (!ok) return false;
    refresh();
    if (field == "active") {
        const auto& sec = doc_->project.security;
        say(sec.enabled ? (sec.users.empty() ? std::string("S\xC3\xA9" "curit\xC3\xA9 active, mais aucun utilisateur : personne ne pourra se connecter")
                                             : std::string("S\xC3\xA9" "curit\xC3\xA9 active : niveaux et permissions appliqu\xC3\xA9s en simulation"))
                        : std::string("S\xC3\xA9" "curit\xC3\xA9 inactive : tout est permis en marche"),
            sec.enabled && sec.users.empty());
    } else {
        say("S\xC3\xA9" "curit\xC3\xA9 : " + fieldLabel(field) + " = " + (value.empty() ? std::string("(vide)") : value));
    }
    return true;
}

Id HmiUsersPane::addUser(std::string login, std::string group, std::string* why) {
    auto& p = doc_->project;
    if (login.empty()) login = hmi::uniqueLogin(p, "utilisateur");
    Id made = kNoId;
    const bool ok = change("Nouvel utilisateur " + login, [&](hmi::Project& pr, std::string& err) {
        if (!hmi::isIdentifier(login)) { err = "login invalide : lettres, chiffres et _ (" + login + ")"; return false; }
        if (pr.userByLogin(login)) { err = "'" + login + "' existe d\xC3\xA9j\xC3\xA0"; return false; }
        hmi::User u;
        u.id = pr.allocate();
        u.login = login;
        const auto* g = group.empty() ? (pr.security.groups.empty() ? nullptr : &pr.security.groups.front()) : pr.groupByName(group);
        if (!group.empty() && !g) { err = "groupe inconnu : " + group; return false; }
        u.group = g ? g->id : kNoId;
        made = u.id;
        pr.security.users.push_back(std::move(u));
        return true;
    }, why);
    if (!ok) return kNoId;
    showTab(Users);
    refresh();
    selectUser(made);
    say("Utilisateur " + login + " cr\xC3\xA9\xC3\xA9 : donne-lui un mot de passe");
    return made;
}

bool HmiUsersPane::deleteUser(Id id) {
    const auto* u = doc_->project.user(id);
    if (!u) return false;
    const std::string login = u->login;
    const bool ok = change("Supprimer " + login, [&](hmi::Project& pr, std::string&) {
        auto& v = pr.security.users;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const hmi::User& x) { return x.id == id; }), v.end());
        if (same(pr.security.startUser, login)) pr.security.startUser.clear();
        return true;
    }, nullptr);
    if (!ok) return false;
    refresh();
    say("Utilisateur " + login + " supprim\xC3\xA9");
    return true;
}

bool HmiUsersPane::setUserField(Id id, const std::string& field, const std::string& raw, std::string* why) {
    const auto* cur = doc_->project.user(id);
    if (!cur) { if (why) *why = "aucun utilisateur choisi"; return false; }
    const std::string value = field == "description" || field == "nom" ? raw : trimmed(raw);
    const std::string old = cur->login;
    std::size_t followed = 0;
    const bool ok = change("Utilisateur " + old + " : " + field, [&](hmi::Project& p, std::string& err) {
        auto* u = p.user(id);
        if (!u) { err = "utilisateur introuvable"; return false; }
        if (field == "login") {
            if (!hmi::isIdentifier(value)) { err = "login invalide : lettres, chiffres et _ (" + value + ")"; return false; }
            for (const auto& o : p.security.users)
                if (o.id != id && same(o.login, value)) { err = "'" + value + "' existe d\xC3\xA9j\xC3\xA0"; return false; }
            u->login = value;
            if (same(p.security.startUser, old)) p.security.startUser = value;
            followed = retarget(p, hmi::Operation::ChangeUser, old, value);
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
    }, why);
    if (!ok) return false;
    refresh();
    selectUser(id);
    if (field == "login") say("Renomm\xC3\xA9 en " + value + (followed ? " : " + std::to_string(followed) + " action(s) suivent" : std::string{}));
    else say(old + " : " + fieldLabel(field) + " = " + (value.empty() ? std::string("(vide)") : value));
    return true;
}

bool HmiUsersPane::setPassword(Id id, const std::string& password, std::string* why) {
    const auto* u = doc_->project.user(id);
    if (!u) { if (why) *why = "aucun utilisateur choisi"; return false; }
    const std::string login = u->login;
    const std::string salt = hmi::randomHex(16);
    const std::string hash = hmi::passwordHash(salt, password);
    const std::string today = hmi::dayOf(hmi::wallStamp());
    const bool ok = change("Mot de passe de " + login, [&](hmi::Project& p, std::string& err) {
        if (password.size() < 4) { err = "mot de passe trop court : 4 caract\xC3\xA8res au moins"; return false; }
        auto* x = p.user(id);
        if (!x) { err = "utilisateur introuvable"; return false; }
        // Lot 13 : la politique du projet ; le precedent rejoint l'historique, la date du jour.
        if (const std::string problem = hmi::passwordProblem(p.security, x, password, 4); !problem.empty()) {
            err = "mot de passe de " + login + " : " + problem;
            return false;
        }
        hmi::storePassword(p.security, *x, salt, hash, today, true);
        return true;
    }, why);
    if (!ok) return false;
    refresh();
    selectUser(id);
    const bool first = doc_->project.security.pwChangeFirst;
    say("Mot de passe de " + login + " d\xC3\xA9" "fini (empreinte sal\xC3\xA9" "e, jamais en clair)"
        + (first ? std::string(" : \xC3\xA0 changer \xC3\xA0 sa premi\xC3\xA8re connexion") : std::string{}));
    return true;
}

bool HmiUsersPane::setBadge(Id id, const std::string& raw, std::string* why) {
    const auto* u = doc_->project.user(id);
    if (!u) { if (why) *why = "aucun utilisateur choisi"; return false; }
    const std::string login = u->login;
    std::string number;
    for (const char c : raw) if (!std::isspace(static_cast<unsigned char>(c))) number += c;
    // Le champ montre des points quand un badge est defini : les retaper ne change rien.
    if (number.find("\xE2\x80\xA2") != std::string::npos) return true;
    if (!number.empty()) {
        if (number.size() < 4) { const std::string e = "badge : 4 caract\xC3\xA8res au moins"; if (why) *why = e; say(e, true); return false; }
        for (const char c : number)
            if (!std::isalnum(static_cast<unsigned char>(c))) {
                const std::string e = "badge : des lettres et des chiffres seulement (ce que tape le lecteur)";
                if (why) *why = e;
                say(e, true);
                return false;
            }
    }
    std::string owner;
    for (const auto& o : doc_->project.security.users)
        if (o.id != id && !o.badge.empty() && !number.empty() && hmi::badgeMatches(o.badge, number)) owner = o.login;
    if (!owner.empty()) { const std::string e = "ce badge est d\xC3\xA9j\xC3\xA0 celui de " + owner; if (why) *why = e; say(e, true); return false; }
    const std::string stored = number.empty() ? std::string{} : hmi::badgeHash(number);
    const bool ok = change(number.empty() ? "Retirer le badge de " + login : "Badge de " + login, [&](hmi::Project& p, std::string& err) {
        auto* x = p.user(id);
        if (!x) { err = "utilisateur introuvable"; return false; }
        x->badge = stored;
        return true;
    }, why);
    if (!ok) return false;
    refresh();
    selectUser(id);
    say(number.empty() ? "Badge de " + login + " retir\xC3\xA9" : "Badge de " + login + " d\xC3\xA9" "fini (empreinte sal\xC3\xA9" "e, jamais en clair)");
    return true;
}

bool HmiUsersPane::unlockUser(Id id, std::string* why) {
    const auto* u = doc_->project.user(id);
    if (!u) { if (why) *why = "aucun utilisateur choisi"; return false; }
    auto* st = doc_->history.account(u->login);
    if (!st || st->lockedAt.empty()) {
        const std::string e = u->login + " n'est pas verrouill\xC3\xA9";
        if (why) *why = e;
        say(e, true);
        return false;
    }
    const std::string login = u->login;
    st->failures = 0;
    st->lockedAt.clear();
    st->lockedUntil = 0;
    doc_->dirty = true;             // l'etat des comptes s'enregistre avec l'historique
    refresh();
    selectUser(id);
    say(login + " d\xC3\xA9verrouill\xC3\xA9 (enregistre le projet pour le garder)");
    return true;
}

bool HmiUsersPane::newSecret(Id id) {
    const auto* u = doc_->project.user(id);
    if (!u) return false;
    const std::string login = u->login;
    const std::string secret = hmi::randomHex(20);
    const bool ok = change("Nouveau secret de " + login, [&](hmi::Project& p, std::string&) {
        if (auto* x = p.user(id)) { x->secret = secret; x->protection = "dynamique"; }
        return true;
    }, nullptr);
    if (!ok) return false;
    refresh();
    selectUser(id);
    say("Nouveau secret pour " + login + " : l'ancien code ne vaut plus");
    return true;
}

std::string HmiUsersPane::codeOf(Id id, double unixSeconds) const {
    const auto* u = doc_->project.user(id);
    if (!u || u->secret.empty()) return {};
    const auto& sec = doc_->project.security;
    return hmi::dynamicCode(u->secret, unixSeconds < 0 ? hmi::wallEpoch() : unixSeconds, sec.dynamicPeriodS, sec.dynamicDigits);
}

std::string HmiUsersPane::secretBase32(Id id) const {
    const auto* u = doc_->project.user(id);
    return u ? base32(u->secret) : std::string{};
}

Id HmiUsersPane::addGroup(std::string name, int level, std::string* why) {
    auto& p = doc_->project;
    if (name.empty())
        for (int i = 1;; ++i) {
            name = "Groupe " + std::to_string(i);
            if (!p.groupByName(name)) break;
        }
    Id made = kNoId;
    const bool ok = change("Nouveau groupe " + name, [&](hmi::Project& pr, std::string& err) {
        if (pr.groupByName(name)) { err = "le groupe '" + name + "' existe d\xC3\xA9j\xC3\xA0"; return false; }
        if (level < 1 || level > 9) { err = "niveau de 1 \xC3\xA0 9"; return false; }
        hmi::UserGroup g;
        g.id = pr.allocate();
        g.name = name;
        g.level = level;
        made = g.id;
        pr.security.groups.push_back(std::move(g));
        return true;
    }, why);
    if (!ok) return kNoId;
    showTab(Groups);
    refresh();
    selectGroup(made);
    say("Groupe " + name + " cr\xC3\xA9\xC3\xA9 : donne-lui un niveau et des r\xC3\xB4les");
    return made;
}

bool HmiUsersPane::deleteGroup(Id id, std::string* why) {
    const auto* g = doc_->project.group(id);
    if (!g) return false;
    const std::string name = g->name;
    const bool ok = change("Supprimer le groupe " + name, [&](hmi::Project& p, std::string& err) {
        std::size_t members = 0;
        for (const auto& u : p.security.users) members += u.group == id;
        if (members) {
            err = std::to_string(members) + " utilisateur(s) dans " + name + " : change d'abord leur groupe";
            return false;
        }
        auto& v = p.security.groups;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const hmi::UserGroup& x) { return x.id == id; }), v.end());
        return true;
    }, why);
    if (!ok) return false;
    refresh();
    say("Groupe " + name + " supprim\xC3\xA9");
    return true;
}

bool HmiUsersPane::setGroupField(Id id, const std::string& field, const std::string& raw, std::string* why) {
    const auto* cur = doc_->project.group(id);
    if (!cur) { if (why) *why = "aucun groupe choisi"; return false; }
    const std::string value = field == "description" ? raw : trimmed(raw);
    const std::string old = cur->name;
    const bool ok = change("Groupe " + old + " : " + field, [&](hmi::Project& p, std::string& err) {
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
            double n = 0;
            if (!hmi::parseNumber(value, n) || n < 1 || n > 9) { err = "niveau : de 1 \xC3\xA0 9"; return false; }
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
    }, why);
    if (!ok) return false;
    refresh();
    selectGroup(id);
    say(old + " : " + fieldLabel(field) + " = " + value);
    return true;
}

bool HmiUsersPane::addRole(std::string name, std::string* why) {
    auto& p = doc_->project;
    if (name.empty())
        for (int i = 1;; ++i) {
            name = "R\xC3\xB4le " + std::to_string(i);
            if (!p.role(name)) break;
        }
    const bool ok = change("Nouveau r\xC3\xB4le " + name, [&](hmi::Project& pr, std::string& err) {
        if (pr.role(name)) { err = "le r\xC3\xB4le '" + name + "' existe d\xC3\xA9j\xC3\xA0"; return false; }
        hmi::Role r;
        r.name = name;
        r.permissions = {"Naviguer"};
        pr.security.roles.push_back(std::move(r));
        return true;
    }, why);
    if (!ok) return false;
    showTab(Roles);
    refresh();
    selectRole(name);
    say("R\xC3\xB4le " + name + " cr\xC3\xA9\xC3\xA9 (Naviguer) : coche ses permissions \xC3\xA0 droite");
    return true;
}

bool HmiUsersPane::deleteRole(const std::string& name, std::string* why) {
    std::size_t cited = 0;
    const bool ok = change("Supprimer le r\xC3\xB4le " + name, [&](hmi::Project& p, std::string& err) {
        auto& v = p.security.roles;
        const auto before = v.size();
        v.erase(std::remove_if(v.begin(), v.end(), [&](const hmi::Role& x) { return same(x.name, name); }), v.end());
        if (v.size() == before) { err = "r\xC3\xB4le introuvable : " + name; return false; }
        for (auto& g : p.security.groups) {
            const auto n = g.roles.size();
            g.roles.erase(std::remove_if(g.roles.begin(), g.roles.end(), [&](const std::string& r) { return same(r, name); }), g.roles.end());
            cited += n - g.roles.size();
        }
        return true;
    }, why);
    if (!ok) return false;
    refresh();
    say("R\xC3\xB4le " + name + " supprim\xC3\xA9" + (cited ? " (retir\xC3\xA9 de " + std::to_string(cited) + " groupe(s))" : std::string{}));
    return true;
}

bool HmiUsersPane::setRoleField(const std::string& role, const std::string& field, const std::string& raw, std::string* why) {
    const std::string value = field == "description" ? raw : trimmed(raw);
    std::string now = role;
    const bool ok = change("R\xC3\xB4le " + role + " : " + field, [&](hmi::Project& p, std::string& err) {
        hmi::Role* r = nullptr;
        for (auto& x : p.security.roles) if (same(x.name, role)) r = &x;
        if (!r) { err = "r\xC3\xB4le introuvable : " + role; return false; }
        if (field == "nom") {
            if (value.empty()) { err = "un r\xC3\xB4le a un nom"; return false; }
            for (const auto& o : p.security.roles)
                if (&o != r && same(o.name, value)) { err = "le r\xC3\xB4le '" + value + "' existe d\xC3\xA9j\xC3\xA0"; return false; }
            for (auto& g : p.security.groups)
                for (auto& n : g.roles) if (same(n, r->name)) n = value;
            r->name = value;
            now = value;
            return true;
        }
        if (field == "description") { r->description = value; return true; }
        for (const auto& perm : hmi::permissionNames()) {
            if (!same(perm, field)) continue;
            auto& v = r->permissions;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const std::string& x) { return same(x, perm); }), v.end());
            if (yes(value)) {
                // Dans l'ordre de la liste des permissions.
                std::vector<std::string> ordered;
                for (const auto& q : hmi::permissionNames()) {
                    bool in = same(q, perm);
                    for (const auto& x : v) in = in || same(x, q);
                    if (in) ordered.push_back(q);
                }
                v = ordered;
            }
            return true;
        }
        err = "champ inconnu : " + field;
        return false;
    }, why);
    if (!ok) return false;
    refresh();
    selectRole(now);
    say("R\xC3\xB4le " + now + " : " + field + " = " + value);
    return true;
}

void HmiUsersPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    // Lot API 8 : la recherche sous la barre, comme celle des alarmes.
    search_->setBounds({b.x + 8, b.y + 42, std::max(0.f, std::min(560.f, b.w - 16)), 28});
    split_->setBounds({b.x, b.y + 76, b.w, std::max(0.f, b.h - 100)});
}

void HmiUsersPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
