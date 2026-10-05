// app/hmi/HmiWebHost.cpp - l'acces par navigateur, a l'application (lot 14).
#include "HmiWebHost.hpp"

#include "../Capture.hpp"
#include "../../hmi/HmiCrypto.hpp"
#include "../../hmi/HmiNet.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace app {

WebHost::~WebHost() { shutdown(); }

void WebHost::shutdown() {
    if (server_) server_->stop();
    server_.reset();
    started_ = false;
}

std::vector<std::string> WebHost::addresses() const {
    std::vector<std::string> out;
    if (!running()) return out;
    out.push_back("http://127.0.0.1:" + std::to_string(port()));
    if (settings_.allInterfaces) {
        const std::string local = hmi::net::localAddress();
        if (!local.empty() && local != "127.0.0.1") out.push_back("http://" + local + ":" + std::to_string(port()));
    }
    return out;
}

bool WebHost::check(const std::string& login, const std::string& password, bool* control, std::string* why) {
    std::lock_guard<std::mutex> lock(accountsMutex_);
    const auto sameLogin = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
        return true;
    };
    for (const auto& a : accounts_) {
        if (!sameLogin(a.login, login)) continue;
        if (!a.enabled) {
            if (why) *why = "compte d\xC3\xA9sactiv\xC3\xA9";
            return false;
        }
        if (a.protection != "classique") {
            if (why) *why = "ce compte se connecte par code ou par expression : pas depuis le web";
            return false;
        }
        if (a.hash.empty() || !hmi::passwordMatches(a.salt, a.hash, password)) {
            if (why) *why = "utilisateur ou mot de passe faux";
            return false;
        }
        if (control) *control = !securityOn_ || a.pilot;
        return true;
    }
    if (why) *why = "utilisateur ou mot de passe faux";
    return false;
}

void WebHost::tick(const hmi::Project* project) {
    const hmi::WebAccess wanted = project ? project->web : hmi::WebAccess{};
    // Les comptes : une copie, refaite quand la securite change.
    if (project && !(project->security == security_)) {
        security_ = project->security;
        std::vector<Account> list;
        for (const auto& u : project->security.users) {
            Account a;
            a.login = u.login;
            a.salt = u.salt;
            a.hash = u.passwordHash;
            a.protection = u.protection;
            a.enabled = u.enabled;
            for (const auto& g : project->security.groups)
                if (g.id == u.group)
                    for (const auto& roleName : g.roles)
                        for (const auto& r : project->security.roles)
                            if (r.name == roleName && std::find(r.permissions.begin(), r.permissions.end(), "Piloter") != r.permissions.end())
                                a.pilot = true;
            list.push_back(std::move(a));
        }
        std::lock_guard<std::mutex> lock(accountsMutex_);
        accounts_ = std::move(list);
        securityOn_ = project->security.enabled;
    }
    const std::string name = project ? project->config.name : std::string{};
    const bool restart = started_ && (wanted.port != settings_.port || wanted.allInterfaces != settings_.allInterfaces || wanted.login != settings_.login
                                      || name != project_);
    if (!wanted.enabled || restart) {
        if (server_ && server_->running()) {
            server_->stop();
            events_.push_back("Acc\xC3\xA8s web arr\xC3\xAAt\xC3\xA9");
        }
        server_.reset();
        started_ = false;
    }
    if (wanted.enabled && !started_) {
        started_ = true;
        settings_ = wanted;
        project_ = name;
        server_ = std::make_unique<hmi::web::Server>();
        std::string why;
        if (server_->start(wanted, name, [this](const std::string& l, const std::string& p, bool* c, std::string* w) { return check(l, p, c, w); }, &why)) {
            error_.clear();
            const auto where = addresses();
            events_.push_back("Acc\xC3\xA8s web ouvert : " + (where.empty() ? std::string{} : where.back()) + (wanted.login ? " (connexion demand\xC3\xA9" "e)" : " (sans connexion)"));
        } else {
            error_ = why;
            events_.push_back("Acc\xC3\xA8s web impossible : " + why);
            server_.reset();
        }
    } else if (wanted.enabled && server_ && !(wanted == settings_)) {
        settings_ = wanted;
        server_->configure(wanted);
    }
    if (server_)
        for (auto& e : server_->takeEvents()) events_.push_back("Acc\xC3\xA8s web : " + e);
    // Personne ne les lit (aucune IHM en marche) : les 200 dernieres.
    if (events_.size() > 200) events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(events_.size() - 200));
}

bool WebHost::due(double time) const {
    if (!server_ || !server_->running() || !server_->wantsFrame()) return false;
    return time - lastFrame_ >= std::max(100, settings_.refreshMs) / 1000.0;
}

void WebHost::publish(gfx::IRenderer& renderer, gfx::Rect area, const std::string& view, bool running, double time) {
    if (!server_) return;
    lastFrame_ = time;
    hmi::web::Frame f;
    f.view = view;
    f.running = running;
    f.serial = ++serial_;
    if (running && area.w > 4.f && area.h > 4.f) {
        std::vector<std::uint8_t> rgba;
        int fw = 0, fh = 0;
        if (renderer.readPixels(rgba, fw, fh) && fw > 0 && fh > 0) {
            const int x = std::clamp(static_cast<int>(std::floor(area.x)), 0, fw - 1);
            const int y = std::clamp(static_cast<int>(std::floor(area.y)), 0, fh - 1);
            const int w = std::clamp(static_cast<int>(std::ceil(area.w)), 1, fw - x);
            const int h = std::clamp(static_cast<int>(std::ceil(area.h)), 1, fh - y);
            std::vector<std::uint8_t> crop(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
            for (int row = 0; row < h; ++row)
                std::copy_n(rgba.data() + (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(fw) + static_cast<std::size_t>(x)) * 4,
                            static_cast<std::size_t>(w) * 4, crop.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4);
            std::vector<std::uint8_t> jpeg;
            if (encodeJpeg(crop, w, h, settings_.quality, jpeg)) {
                f.jpeg = std::make_shared<const hmi::Bytes>(jpeg.begin(), jpeg.end());
                f.width = w;
                f.height = h;
                lastArea_ = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
            }
        }
    }
    server_->setFrame(std::move(f));
}

std::vector<WebHost::ScreenClick> WebHost::takeClicks() {
    std::vector<ScreenClick> out;
    if (!server_) return out;
    for (const auto& k : server_->takeClicks()) {
        if (lastArea_.w <= 0.f) continue;
        ScreenClick c;
        c.at = {lastArea_.x + static_cast<float>(k.x), lastArea_.y + static_cast<float>(k.y)};
        c.user = k.user;
        c.peer = k.peer;
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<std::string> WebHost::takeEvents() {
    if (server_)
        for (auto& e : server_->takeEvents()) events_.push_back("Acc\xC3\xA8s web : " + e);
    std::vector<std::string> out;
    out.swap(events_);
    return out;
}

} // namespace app
