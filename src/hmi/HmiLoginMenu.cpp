#include "HmiLoginMenu.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace hmi {

std::string_view loginTabKey(LoginTab t) noexcept {
    for (const auto& s : kLoginTabs) if (s.tab == t) return s.key;
    return "connexion";
}
std::string_view loginTabLabel(LoginTab t) noexcept {
    for (const auto& s : kLoginTabs) if (s.tab == t) return s.label;
    return "Connexion";
}
LoginTab loginTabFrom(std::string_view s) noexcept {
    std::string low;
    for (char c : s) low += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    for (const auto& t : kLoginTabs) {
        std::string label;
        for (char c : t.label) label += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (low == t.key || low == label) return t.tab;
    }
    if (low.rfind("acc", 0) == 0) return LoginTab::Acces;
    if (low.rfind("mon", 0) == 0) return LoginTab::Compte;
    return LoginTab::Connexion;
}

std::vector<LoginTab> visibleLoginTabs(const Project& p, const User* u) {
    std::vector<LoginTab> out{LoginTab::Connexion};
    if (!p.security.enabled) {
        out.insert(out.end(), {LoginTab::Compte, LoginTab::Comptes, LoginTab::Acces, LoginTab::Journal});
        return out;
    }
    if (!u) return out;
    out.push_back(LoginTab::Compte);
    const bool admin = userHas(p, *u, "Administrer");
    const int level = userLevel(p, *u);
    const auto& s = p.security;
    if (admin || level >= s.menuLevelAccounts) out.push_back(LoginTab::Comptes);
    if (admin || level >= s.menuLevelAccess) out.push_back(LoginTab::Acces);
    if (admin || level >= s.menuLevelJournal) out.push_back(LoginTab::Journal);
    return out;
}

const std::vector<int>& autoLogoutSteps() {
    static const std::vector<int> steps = {0, 1, 2, 5, 10, 15, 30, 60, 120};
    return steps;
}

LoginMenuLayout loginMenuLayout(double w, double h, const std::vector<LoginTab>& tabs, LoginTab tab, std::size_t rows,
                                std::size_t groups, std::size_t roles, bool renewal) {
    LoginMenuLayout l;
    l.renewal = renewal && tab == LoginTab::Connexion;
    const bool compact = h < 420;
    const double pw = std::max(160.0, std::min(w - 24, 820.0)), ph = std::max(160.0, std::min(h - 20, 560.0));
    const double px = (w - pw) / 2, py = (h - ph) / 2;
    const double titleH = compact ? 30 : 40, tabH = compact ? 24 : 30, statusH = compact ? 22 : 28;
    l.panel = {px, py, pw, ph};
    l.title = {px, py, pw, titleH};
    l.close = {px + pw - titleH, py, titleH, titleH};
    const double n = static_cast<double>(std::max<std::size_t>(1, tabs.size()));
    const double tabW = std::min(150.0, (pw - 24 - 4 * (n - 1)) / n);
    for (std::size_t i = 0; i < tabs.size(); ++i)
        l.tabs.push_back({tabs[i], {px + 12 + static_cast<double>(i) * (tabW + 4), py + titleH + 4, tabW, tabH}});
    l.status = {px, py + ph - statusH, pw, statusH};
    l.body = {px + 16, py + titleH + tabH + 12, pw - 32, std::max(20.0, ph - titleH - tabH - 12 - statusH - 8)};
    l.rowH = std::clamp(l.body.h / 10.0, 16.0, 40.0);
    l.fontSize = std::clamp(l.rowH * 0.44, 9.0, 16.0);
    const double labelW = std::min(210.0, l.body.w * 0.3);
    const double x0 = l.body.x + labelW, fieldW = std::min(360.0, l.body.right() - x0);
    double y = l.body.y;
    switch (tab) {
        case LoginTab::Connexion: {
            if (l.renewal) {
                // Lot 13 : pourquoi (deux lignes), le nouveau, la confirmation.
                l.who = {l.body.x, y, l.body.w, l.rowH * 1.8};
                y += l.rowH * 2.2;
                l.newField = {x0, y, fieldW, l.rowH};
                y += l.rowH * 1.3;
                l.confirmField = {x0, y, fieldW, l.rowH};
                y += l.rowH * 1.6;
                const double bw = std::min(180.0, (fieldW - 10) / 2);
                l.login = {x0, y, bw, l.rowH};
                l.logout = {x0 + bw + 10, y, bw, l.rowH};
                y += l.rowH * 1.5;
                l.message = {l.body.x, y, l.body.w, l.rowH};
                break;
            }
            l.who = {l.body.x, y, l.body.w, l.rowH};
            y += l.rowH * 1.4;
            l.prev = {x0, y, l.rowH, l.rowH};
            l.account = {x0 + l.rowH + 6, y, std::max(10.0, fieldW - 2 * l.rowH - 12), l.rowH};
            l.next = {l.account.right() + 6, y, l.rowH, l.rowH};
            y += l.rowH * 1.35;
            l.secret = {x0, y, fieldW, l.rowH};
            y += l.rowH * 1.6;
            const double bw = std::min(180.0, (fieldW - 10) / 2);
            l.login = {x0, y, bw, l.rowH};
            l.logout = {x0 + bw + 10, y, bw, l.rowH};
            y += l.rowH * 1.5;
            l.message = {l.body.x, y, l.body.w, l.rowH};
            break;
        }
        case LoginTab::Compte: {
            l.info = {l.body.x, y, l.body.w, l.rowH * 3};
            y += l.rowH * 3.4;
            l.oldField = {x0, y, fieldW, l.rowH};
            y += l.rowH * 1.3;
            l.newField = {x0, y, fieldW, l.rowH};
            y += l.rowH * 1.3;
            l.confirmField = {x0, y, fieldW, l.rowH};
            y += l.rowH * 1.5;
            l.change = {x0, y, std::min(240.0, fieldW), l.rowH};
            y += l.rowH * 1.4;
            l.message = {l.body.x, y, l.body.w, l.rowH};
            break;
        }
        case LoginTab::Comptes: case LoginTab::Journal: {
            const double buttonsH = tab == LoginTab::Comptes ? l.rowH * 1.5 : 0.0;
            const double listH = std::max(l.rowH, l.body.bottom() - (y + l.rowH) - buttonsH - l.rowH * 0.9);
            const auto visible = static_cast<std::size_t>(std::max(1.0, std::floor(listH / l.rowH)));
            const double arrows = rows > visible ? 30 : 0;     // les fleches, s'il y a de quoi defiler
            l.header = {l.body.x, y, l.body.w - arrows, l.rowH};
            for (std::size_t i = 0; i < visible; ++i)
                l.rows.push_back({l.body.x, y + l.rowH * static_cast<double>(i + 1), l.body.w - arrows, l.rowH});
            if (rows > visible) {
                l.up = {l.body.right() - arrows + 4, y + l.rowH, arrows - 6, arrows - 6};
                l.down = {l.body.right() - arrows + 4, y + l.rowH * static_cast<double>(visible + 1) - arrows + 6, arrows - 6, arrows - 6};
            }
            y += l.rowH * static_cast<double>(visible + 1) + 6;
            l.message = {l.body.x, l.body.bottom() - l.rowH * 0.9, l.body.w, l.rowH * 0.9};
            if (tab == LoginTab::Comptes) {
                static const char* keys[] = {"ajouter", "motdepasse", "activer", "groupe:precedent", "groupe:suivant", "supprimer"};
                const double gap = 6, count = static_cast<double>(std::size(keys));
                const double bw = (l.body.w - gap * (count - 1)) / count;
                for (std::size_t i = 0; i < std::size(keys); ++i)
                    l.buttons.push_back({keys[i], {l.body.x + static_cast<double>(i) * (bw + gap), y, std::max(10.0, bw), l.rowH}});
            }
            break;
        }
        case LoginTab::Acces: {
            const double gw = std::min(220.0, l.body.w * 0.3);
            const double cw = roles ? std::min(130.0, (l.body.w - gw) / static_cast<double>(roles)) : 0.0;
            l.header = {l.body.x, y, l.body.w, l.rowH};
            for (std::size_t r = 0; r < roles; ++r) l.roleColumns.push_back({l.body.x + gw + cw * static_cast<double>(r), y, cw, l.rowH});
            for (std::size_t g = 0; g < groups; ++g) l.groupRows.push_back({l.body.x, y + l.rowH * static_cast<double>(g + 1), l.body.w, l.rowH});
            y += l.rowH * static_cast<double>(groups + 1) + l.rowH * 0.6;
            l.logoutRow = {l.body.x, y, l.body.w, l.rowH};
            l.logoutMinus = {x0 + 40, y + 2, l.rowH - 4, l.rowH - 4};
            l.logoutPlus = {x0 + 40 + 150, y + 2, l.rowH - 4, l.rowH - 4};
            l.message = {l.body.x, l.body.bottom() - l.rowH * 0.9, l.body.w, l.rowH * 0.9};
            break;
        }
    }
    return l;
}

std::string loginMenuHit(const LoginMenuLayout& l, LoginTab tab, double x, double y, std::size_t scroll, std::size_t rows) {
    if (!l.panel.contains(x, y)) return "dehors";
    if (l.close.contains(x, y)) return "fermer";
    for (const auto& [t, b] : l.tabs)
        if (b.contains(x, y)) return "onglet:" + std::string(loginTabKey(t));
    switch (tab) {
        case LoginTab::Connexion:
            if (l.renewal) {
                if (l.newField.contains(x, y)) return "champ:nouveau";
                if (l.confirmField.contains(x, y)) return "champ:confirmation";
                if (l.login.contains(x, y)) return "bouton:renouveler";
                if (l.logout.contains(x, y)) return "bouton:annuler";
                break;
            }
            if (l.prev.contains(x, y)) return "precedent";
            if (l.next.contains(x, y)) return "suivant";
            if (l.secret.contains(x, y)) return "champ:secret";
            if (l.login.contains(x, y)) return "bouton:connexion";
            if (l.logout.contains(x, y)) return "bouton:deconnexion";
            break;
        case LoginTab::Compte:
            if (l.oldField.contains(x, y)) return "champ:ancien";
            if (l.newField.contains(x, y)) return "champ:nouveau";
            if (l.confirmField.contains(x, y)) return "champ:confirmation";
            if (l.change.contains(x, y)) return "bouton:changer";
            break;
        case LoginTab::Comptes: case LoginTab::Journal:
            if (l.up.w > 0 && l.up.contains(x, y)) return "defiler:-1";
            if (l.down.w > 0 && l.down.contains(x, y)) return "defiler:1";
            for (std::size_t i = 0; i < l.rows.size(); ++i)
                if (l.rows[i].contains(x, y) && scroll + i < rows) return "ligne:" + std::to_string(scroll + i);
            for (const auto& b : l.buttons)
                if (b.box.contains(x, y)) return "bouton:" + b.key;
            break;
        case LoginTab::Acces:
            for (std::size_t g = 0; g < l.groupRows.size(); ++g)
                for (std::size_t r = 0; r < l.roleColumns.size(); ++r) {
                    const Box cell{l.roleColumns[r].x, l.groupRows[g].y, l.roleColumns[r].w, l.groupRows[g].h};
                    if (cell.contains(x, y)) return "role:" + std::to_string(g) + "," + std::to_string(r);
                }
            if (l.logoutMinus.contains(x, y)) return "deconnexion:moins";
            if (l.logoutPlus.contains(x, y)) return "deconnexion:plus";
            break;
    }
    return {};
}

bool loginMenuPartBox(const LoginMenuLayout& l, LoginTab tab, std::string_view part, Box& out) {
    const auto set = [&](const Box& b) { if (b.w <= 0) return false; out = b; return true; };
    if (part == "fermer") return set(l.close);
    if (part.rfind("onglet:", 0) == 0) {
        for (const auto& [t, b] : l.tabs)
            if (loginTabKey(t) == part.substr(7)) return set(b);
        return false;
    }
    if (part == "precedent") return set(l.prev);
    if (part == "suivant") return set(l.next);
    if (part == "champ:secret") return set(l.secret);
    if (part == "bouton:connexion" || part == "bouton:renouveler") return set(l.login);
    if (part == "bouton:deconnexion" || part == "bouton:annuler") return set(l.logout);
    if (part == "champ:ancien") return set(l.oldField);
    if (part == "champ:nouveau") return set(l.newField);
    if (part == "champ:confirmation") return set(l.confirmField);
    if (part == "bouton:changer") return set(l.change);
    if (part == "defiler:-1") return set(l.up);
    if (part == "defiler:1") return set(l.down);
    if (part == "deconnexion:moins") return set(l.logoutMinus);
    if (part == "deconnexion:plus") return set(l.logoutPlus);
    if (part.rfind("ligne:", 0) == 0) {
        const auto i = static_cast<std::size_t>(std::max(0, std::atoi(std::string(part.substr(6)).c_str())));
        return i < l.rows.size() && set(l.rows[i]);    // i : le rang a l'ecran
    }
    if (part.rfind("bouton:", 0) == 0) {
        for (const auto& b : l.buttons)
            if (b.key == part.substr(7)) return set(b.box);
        return false;
    }
    if (part.rfind("role:", 0) == 0) {
        const std::string rest(part.substr(5));
        const auto comma = rest.find(',');
        if (comma == std::string::npos) return false;
        const auto g = static_cast<std::size_t>(std::max(0, std::atoi(rest.substr(0, comma).c_str())));
        const auto r = static_cast<std::size_t>(std::max(0, std::atoi(rest.substr(comma + 1).c_str())));
        if (g >= l.groupRows.size() || r >= l.roleColumns.size()) return false;
        out = {l.roleColumns[r].x, l.groupRows[g].y, l.roleColumns[r].w, l.groupRows[g].h};
        return true;
    }
    (void)tab;
    return false;
}

} // namespace hmi
