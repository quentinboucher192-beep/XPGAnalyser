// Configuration > Acces web (lot 14).
#include "HmiWebPanes.hpp"

#include "HmiIcons.hpp"
#include "HmiPaneKit.hpp"
#include "HmiWebHost.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace app {

using namespace hmikit;
using hmi::Id;
using PG = ui::PropertyGrid;

namespace {
enum : int { CDisconnect = 1 };
}

HmiWebPane::HmiWebPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(CDisconnect, HmiGlyph::Logout, "D\xC3\xA9" "connecter tous les navigateurs (ils devront se reconnecter)", "D\xC3\xA9" "connecter tout");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(CDisconnect, [this] { return hosts_.web && hosts_.web() && hosts_.web()->clientCount() > 0; });
    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    auto clients = std::make_unique<ui::TableView>(base + ".clients");
    clients->setColumns({{"Navigateur", 360.f}, {"Utilisateur", 150.f}, {"Depuis", 90.f}, {"Vu il y a", 90.f}, {"Acc\xC3\xA8s", 120.f},
                         {"Images", 80.f}, {"Clics", 70.f}});
    clients_ = static_cast<ui::TableView*>(clients.get());
    tabs->addTab({"Navigateurs", ui::Icon::Globe}, std::move(clients));
    tabs_ = &static_cast<ui::TabControl&>(addChild(std::move(tabs)));
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".grid")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    links_ += tools_->triggered->connect([this](int a) {
        if (a == CDisconnect)
            if (auto* host = hosts_.web ? hosts_.web() : nullptr) {
                host->disconnectAll();
                say("Tous les navigateurs sont d\xC3\xA9" "connect\xC3\xA9s.");
                refreshLive();
            }
    });
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
}

void HmiWebPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    refresh();
}

void HmiWebPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

void HmiWebPane::refresh() {
    rebuildProperties();
    refreshLive();
}

void HmiWebPane::refreshLive() {
    auto* host = hosts_.web ? hosts_.web() : nullptr;
    std::vector<std::vector<std::string>> rows;
    std::vector<int> tones;
    if (host && host->running()) {
        for (const auto& a : host->addresses()) {
            rows.push_back({a, "\xE2\x80\x94", "\xE2\x80\x94", "\xE2\x80\x94", "l'adresse", "", ""});
            tones.push_back(4);
        }
        const double now = hmi::web::clockSeconds();
        for (const auto& c : host->clients()) {
            rows.push_back({c.peer, c.user.empty() ? std::string("(sans connexion)") : c.user, c.since, std::to_string(static_cast<long long>(std::floor(now - c.lastSeen))) + " s",
                            c.control ? "commande" : "lecture seule", std::to_string(c.images), std::to_string(c.clicks)});
            tones.push_back(c.control ? 1 : 0);
        }
    } else {
        rows.push_back({host && !host->error().empty() ? "impossible : " + host->error() : std::string("arr\xC3\xAAt\xC3\xA9 (r\xC3\xA9glage Actif)"),
                        "", "", "", "", "", ""});
        tones.push_back(host && !host->error().empty() ? 3 : 0);
    }
    clientsModel_ = std::make_shared<Rows>(std::vector<std::string>{"Navigateur", "Utilisateur", "Depuis", "Vu il y a", "Acc\xC3\xA8s", "Images", "Clics"},
                                           std::move(rows), [tones](ui::RowIndex r, std::size_t c) {
                                               ui::CellStyle st;
                                               if (r >= tones.size()) return st;
                                               if (tones[r] == 4 && c == 0) {
                                                   st.bold = true;
                                                   st.fgTone = ui::Tone::Accent;
                                                   st.icon = ui::Icon::Globe;
                                               }
                                               if (tones[r] == 3) st.fgTone = ui::Tone::Error;
                                               if (tones[r] == 1 && c == 4) st.fgTone = ui::Tone::Ok;
                                               return st;
                                           });
    clients_->setModel(clientsModel_);
    const int n = host ? host->clientCount() : 0;
    tabs_->setTabBadge(0, host && host->running() ? std::to_string(n) : std::string("arr\xC3\xAAt\xC3\xA9"), host && host->running() ? ui::Tone::Ok : ui::Tone::None);
}

bool HmiWebPane::setSetting(const std::string& key, const std::string& raw, std::string* why) {
    const auto fail = [&](std::string m) {
        say(m, true);
        if (why) *why = std::move(m);
        return false;
    };
    const std::string v = trimmed(raw);
    const auto apply = [&](const std::string& label, const std::function<void(hmi::WebAccess&)>& fn) {
        auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& p) { fn(p.web); });
        if (!cmd) return false;
        apply_(std::move(cmd));
        refresh();
        return true;
    };
    if (key == "actif" || key == "reseau" || key == "connexion" || key == "commande") {
        const bool on = yes(v);
        const bool ok = apply(key == "actif" ? (on ? "Acc\xC3\xA8s web actif" : "Acc\xC3\xA8s web arr\xC3\xAAt\xC3\xA9")
                              : key == "reseau" ? "Acc\xC3\xA8s web : r\xC3\xA9seau" : key == "connexion" ? "Acc\xC3\xA8s web : connexion" : "Acc\xC3\xA8s web : commande",
                              [&](hmi::WebAccess& w) {
                                  if (key == "actif") w.enabled = on;
                                  else if (key == "reseau") w.allInterfaces = on;
                                  else if (key == "connexion") w.login = on;
                                  else w.control = on;
                              });
        if (ok && key == "commande" && on && !doc_->project.web.login)
            say("Commande sans connexion : n'importe quel navigateur qui trouve le poste pilote l'IHM.", true);
        return ok;
    }
    struct Num {
        const char* key;
        long long lo, hi;
        int hmi::WebAccess::*field;
        const char* label;
    };
    static const Num kNums[] = {
        {"port", 1, 65535, &hmi::WebAccess::port, "Port"},
        {"rafraichir", 100, 10000, &hmi::WebAccess::refreshMs, "Rafra\xC3\xAE" "chir"},
        {"qualite", 20, 100, &hmi::WebAccess::quality, "Qualit\xC3\xA9 de l'image"},
        {"clients", 1, 64, &hmi::WebAccess::maxClients, "Navigateurs au plus"},
        {"session", 1, 1440, &hmi::WebAccess::sessionMin, "Session"},
    };
    for (const auto& k : kNums)
        if (key == k.key) {
            char* end = nullptr;
            const long long x = std::strtoll(v.c_str(), &end, 10);
            if (v.empty() || (end && *end) || x < k.lo || x > k.hi)
                return fail(std::string(k.label) + " : un nombre de " + std::to_string(k.lo) + " \xC3\xA0 " + std::to_string(k.hi));
            return apply(std::string("Acc\xC3\xA8s web : ") + k.label, [&](hmi::WebAccess& w) { w.*(k.field) = static_cast<int>(x); });
        }
    return fail("r\xC3\xA9glage inconnu : " + key);
}

void HmiWebPane::rebuildProperties() {
    const auto& w = doc_->project.web;
    const auto commit = [this](const char* key) {
        return [this, key](std::string_view v) {
            message_.clear();
            return setSetting(key, std::string(v));
        };
    };
    std::vector<PG::Category> cats;
    PG::Category srv;
    srv.name = "Serveur web";
    srv.properties.push_back(prop("Actif", tf(w.enabled), PG::ValueType::Boolean, commit("actif"),
                                  "Le serveur web int\xC3\xA9gr\xC3\xA9 montre la vue en marche \xC3\xA0 un navigateur (une tablette, un autre PC)."));
    srv.properties.push_back(prop("Port", std::to_string(w.port), PG::ValueType::Integer, commit("port"), "8080 : http://<le poste>:8080."));
    srv.properties.push_back(prop("Accessible du r\xC3\xA9seau", tf(w.allInterfaces), PG::ValueType::Boolean, commit("reseau"),
                                  "D\xC3\xA9" "coch\xC3\xA9 : ce poste seulement (127.0.0.1). Coch\xC3\xA9 : les autres postes du r\xC3\xA9seau aussi (toutes les cartes)."));
    {
        auto* host = hosts_.web ? hosts_.web() : nullptr;
        std::string state = "arr\xC3\xAAt\xC3\xA9";
        if (host && host->running()) {
            const auto a = host->addresses();
            state = "en marche : " + (a.empty() ? std::string{} : a.back());
        } else if (host && !host->error().empty()) {
            state = "impossible : " + host->error();
        }
        shownState_ = state;
        srv.properties.push_back(prop("\xC3\x89tat", state, PG::ValueType::ReadOnly));
    }
    cats.push_back(std::move(srv));
    PG::Category acc;
    acc.name = "Acc\xC3\xA8s";
    acc.properties.push_back(prop("Se connecter", tf(w.login), PG::ValueType::Boolean, commit("connexion"),
                                  "Un utilisateur de l'IHM et son mot de passe (Configuration > Utilisateurs). D\xC3\xA9" "coch\xC3\xA9 : sans connexion."));
    acc.properties.push_back(prop("Commande", tf(w.control), PG::ValueType::Boolean, commit("commande"),
                                  "Les clics sur l'image sont des clics sur la vue - pour un utilisateur qui a la permission Piloter. D\xC3\xA9" "coch\xC3\xA9 : lecture seule."));
    acc.properties.push_back(prop("Navigateurs au plus", std::to_string(w.maxClients), PG::ValueType::Integer, commit("clients")));
    acc.properties.push_back(prop("Session (min)", std::to_string(w.sessionMin), PG::ValueType::Integer, commit("session"),
                                  "Sans activit\xC3\xA9 pendant N minutes, la session se ferme."));
    cats.push_back(std::move(acc));
    PG::Category img;
    img.name = "Image";
    img.properties.push_back(prop("Rafra\xC3\xAE" "chir (ms)", std::to_string(w.refreshMs), PG::ValueType::Integer, commit("rafraichir"),
                                  "Une image toutes les N ms, tant qu'un navigateur regarde."));
    img.properties.push_back(prop("Qualit\xC3\xA9 (JPEG)", std::to_string(w.quality), PG::ValueType::Integer, commit("qualite"),
                                  "De 20 (l\xC3\xA9g\xC3\xA8re) \xC3\xA0 100 (nette)."));
    cats.push_back(std::move(img));
    grid_->setCategories(std::move(cats));
}

void HmiWebPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    const float h = std::max(0.f, b.h - 62);
    const float gridW = std::min(460.f, b.w * 0.32f);
    tabs_->setBounds({b.x, b.y + 38, std::max(0.f, b.w - gridW - 4), h});
    grid_->setBounds({b.x + b.w - gridW, b.y + 38, gridW, h});
}

void HmiWebPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
    if (ctx.time - lastLive_ >= 1.0) {
        lastLive_ = ctx.time;
        refreshLive();
        auto* host = hosts_.web ? hosts_.web() : nullptr;
        std::string state = "arr\xC3\xAAt\xC3\xA9";
        if (host && host->running()) {
            const auto a = host->addresses();
            state = "en marche : " + (a.empty() ? std::string{} : a.back());
        } else if (host && !host->error().empty()) {
            state = "impossible : " + host->error();
        }
        if (state != shownState_) rebuildProperties();
    }
}

} // namespace app
