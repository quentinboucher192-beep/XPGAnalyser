// hmi/HmiWeb.cpp - l'IHM dans un navigateur : le serveur web integre (lot 14).
#include "HmiWeb.hpp"

#include "HmiCrypto.hpp"
#include "HmiHistory.hpp"
#include "HmiNet.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace hmi::web {

double clockSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

namespace {

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string htmlEscape(std::string_view s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string jsonEscape(std::string_view s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += b;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string urlDecode(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1]))
                   && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::strtol(std::string(s.substr(i + 1, 2)).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::map<std::string, std::string> formOf(std::string_view q) {
    std::map<std::string, std::string> out;
    std::size_t pos = 0;
    while (pos <= q.size()) {
        const std::size_t amp = q.find('&', pos);
        const std::string_view kv = q.substr(pos, (amp == std::string_view::npos ? q.size() : amp) - pos);
        const std::size_t eq = kv.find('=');
        if (eq != std::string_view::npos) out[lowerAscii(urlDecode(kv.substr(0, eq)))] = urlDecode(kv.substr(eq + 1));
        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
    return out;
}

std::string clockText() {
    const std::string w = wallStamp();
    return w.size() >= 19 ? w.substr(11, 8) : w;
}

const char* statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 204: return "No Content";
        case 303: return "See Other";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 429: return "Too Many Requests";
        case 503: return "Service Unavailable";
        default: return "OK";
    }
}

constexpr const char* kCookie = "xpg_ihm";

const char* const kStyle =
    ":root{color-scheme:dark}"
    "body{margin:0;background:#11151b;color:#e6eaf0;font:14px system-ui,'Segoe UI',sans-serif;display:flex;flex-direction:column;min-height:100vh}"
    "header{display:flex;align-items:center;gap:12px;padding:8px 14px;background:#1b2028;border-bottom:1px solid #2f3b4c}"
    "header b{font-size:15px}.sp{flex:1}a{color:#8ab4ff;text-decoration:none}"
    ".badge{padding:2px 9px;border-radius:10px;font-size:12px;background:#2f3b4c}"
    ".ok{background:#1f5134;color:#9ff0bd}.ro{background:#4a3a14;color:#ffd98a}"
    "main{flex:1;display:flex;align-items:center;justify-content:center;overflow:hidden;padding:8px;height:calc(100vh - 60px)}"
    "img{max-width:100%;max-height:100%;box-shadow:0 2px 16px #0008;background:#000}img.cmd{cursor:crosshair}"
    "#msg{color:#9aa6b8;padding:24px}"
    ".card{background:#1b2028;border:1px solid #2f3b4c;border-radius:8px;padding:24px 28px;width:320px;display:flex;flex-direction:column;gap:12px}"
    ".card h1{font-size:18px;margin:0}.card p{margin:0;color:#9aa6b8}"
    "label{display:flex;flex-direction:column;gap:4px;font-size:13px;color:#c8d0dc}"
    "input{background:#11151b;border:1px solid #3a4556;border-radius:4px;color:#e6eaf0;padding:8px;font-size:15px}"
    "button{background:#2f6fdb;border:0;border-radius:4px;color:#fff;padding:9px;font-size:15px;cursor:pointer}"
    ".err{color:#ff9b93}.small{font-size:12px}";

} // namespace

std::string Server::page(const std::string& projectName, bool loginForm, const std::string& message, int refreshMs) {
    const std::string name = htmlEscape(projectName.empty() ? std::string("IHM") : projectName);
    std::string html = "<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                       "<title>" + name + " \xE2\x80\x94 IHM</title><style>" + kStyle + "</style></head><body>";
    if (loginForm) {
        html += "<main><form class=\"card\" method=\"post\" action=\"/connexion\"><h1>" + name + "</h1><p>Connexion \xC3\xA0 l'IHM</p>";
        if (!message.empty()) html += "<p class=\"err\">" + htmlEscape(message) + "</p>";
        html += "<label>Utilisateur<input name=\"login\" autocomplete=\"username\" autofocus></label>"
                "<label>Mot de passe<input name=\"password\" type=\"password\" autocomplete=\"current-password\"></label>"
                "<button>Se connecter</button>"
                "<p class=\"small\">Les comptes sont ceux de l'IHM. En lecture seule, sauf si l'acc\xC3\xA8s web permet la commande "
                "et que l'utilisateur a la permission Piloter.</p></form></main></body></html>";
        return html;
    }
    html += "<header><b>" + name + "</b><span id=\"vue\" class=\"badge\">\xE2\x80\xA6</span><span id=\"mode\" class=\"badge ro\">lecture seule</span>"
            "<span class=\"sp\"></span><span id=\"user\"></span><a href=\"/deconnexion\">Se d\xC3\xA9" "connecter</a></header>"
            "<main><img id=\"img\" alt=\"La vue en marche\" style=\"display:none\"><div id=\"msg\">Connexion au poste\xE2\x80\xA6</div></main>"
            "<script>"
            "const img=document.getElementById('img'),msg=document.getElementById('msg');let serial=-1,control=false,busy=false;"
            "async function tick(){if(busy)return;busy=true;try{const r=await fetch('/etat',{cache:'no-store'});"
            "if(r.status==401){location.href='/';return}const s=await r.json();"
            "document.getElementById('vue').textContent=s.vue||'\xE2\x80\x94';document.getElementById('user').textContent=s.utilisateur||'';"
            "control=s.commande;const m=document.getElementById('mode');m.textContent=control?'commande':'lecture seule';"
            "m.className='badge '+(control?'ok':'ro');img.className=control?'cmd':'';"
            "if(!s.enMarche){msg.textContent=\"L'IHM n'est pas en marche sur le poste.\";msg.style.display='';img.style.display='none'}"
            "else if(s.serie!=serial){serial=s.serie;img.src='/vue.jpg?n='+serial;img.style.display='';msg.style.display='none'}}"
            "catch(e){msg.textContent='Poste injoignable : nouvel essai\xE2\x80\xA6';msg.style.display=''}finally{busy=false}}"
            "setInterval(tick," + std::to_string(std::max(100, refreshMs)) + ");tick();"
            "img.addEventListener('click',e=>{if(!control)return;const r=img.getBoundingClientRect();"
            "const x=(e.clientX-r.left)*img.naturalWidth/r.width,y=(e.clientY-r.top)*img.naturalHeight/r.height;"
            "fetch('/clic',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'x='+x.toFixed(1)+'&y='+y.toFixed(1)})});"
            "</script></body></html>";
    return html;
}

struct Server::Listener {
    net::Socket socket;
};

Server::Server() = default;
Server::~Server() { stop(); }

bool Server::start(const WebAccess& w, std::string projectName, Authenticate auth, std::string* why) {
    stop();
    auto l = std::make_unique<Listener>();
    std::string err;
    if (!l->socket.listen(w.allInterfaces ? "0.0.0.0" : "127.0.0.1", w.port, &err)) {
        if (why) *why = "port " + std::to_string(w.port) + " : " + err;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings_ = w;
        project_ = std::move(projectName);
        auth_ = std::move(auth);
        sessions_.clear();
        clicks_.clear();
    }
    port_ = l->socket.localPort();
    listener_ = std::move(l);
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void Server::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    running_ = false;
    listener_.reset();
}

void Server::configure(const WebAccess& w) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool controlChanged = w.control != settings_.control;
    settings_ = w;
    if (controlChanged)
        for (auto& s : sessions_) s.control = s.control && w.control;
}

void Server::setFrame(Frame f) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_ = std::move(f);
}

bool Server::wantsFrame() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clockSeconds() - lastRequest_ < 3.0;
}

std::vector<Click> Server::takeClicks() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Click> out;
    out.swap(clicks_);
    return out;
}

std::vector<std::string> Server::takeEvents() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    out.swap(events_);
    return out;
}

void Server::event(std::string text) {
    events_.push_back(std::move(text));
    if (events_.size() > 200) events_.erase(events_.begin());
}

std::vector<Client> Server::clients() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Client> out;
    const double now = clockSeconds();
    for (const auto& s : sessions_)
        if (now - s.lastSeen < 30.0) out.push_back({s.user, s.peer, s.since, s.lastSeen, s.control, s.images, s.clicks});
    return out;
}

int Server::clientCount() const { return static_cast<int>(clients().size()); }

void Server::disconnectAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!sessions_.empty()) event(std::to_string(sessions_.size()) + " session(s) ferm\xC3\xA9" "e(s) depuis le poste");
    sessions_.clear();
}

Server::Session* Server::sessionOf(const std::string& cookie, double now) {
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (it->token != cookie) continue;
        if (now - it->lastSeen > settings_.sessionMin * 60.0) {
            event("Session expir\xC3\xA9" "e : " + (it->user.empty() ? std::string("anonyme") : it->user) + " (" + it->peer + ")");
            sessions_.erase(it);
            return nullptr;
        }
        return &*it;
    }
    return nullptr;
}

void Server::run() {
    while (!stop_) {
        const auto ready = net::waitReadable({&listener_->socket}, 150);
        if (ready.empty()) continue;
        std::string peer;
        auto conn = listener_->socket.accept(0, &peer);
        if (!conn.valid()) continue;
        serve(&conn, peer.substr(0, peer.rfind(':')));
    }
    running_ = false;
}

void Server::serve(void* raw, const std::string& peer) {
    auto& c = *static_cast<net::Socket*>(raw);
    constexpr int kWait = 3000;
    std::string requestLine;
    if (!c.receiveLine(requestLine, kWait)) return;
    std::string cookie;
    long long length = 0;
    for (int guard = 0; guard < 100; ++guard) {
        std::string h;
        if (!c.receiveLine(h, kWait) || h.empty()) break;
        const std::string l = lowerAscii(h);
        if (l.rfind("content-length:", 0) == 0) length = std::atoll(h.c_str() + 15);
        if (l.rfind("cookie:", 0) == 0) {
            const std::string v = h.substr(7);
            const std::size_t at = v.find(std::string(kCookie) + "=");
            if (at != std::string::npos) {
                const std::size_t start = at + std::string(kCookie).size() + 1;
                cookie = v.substr(start, v.find(';', start) - start);
            }
        }
    }
    std::string body;
    if (length > 0 && length <= 16384) {
        body.resize(static_cast<std::size_t>(length));
        if (!c.receiveExact(body.data(), body.size(), kWait)) return;
    }
    const std::size_t sp1 = requestLine.find(' '), sp2 = requestLine.find(' ', sp1 + 1);
    if (sp1 == std::string::npos) return;
    const std::string method = requestLine.substr(0, sp1);
    const std::string target = requestLine.substr(sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1);
    const std::string path = target.substr(0, target.find('?'));

    const auto reply = [&](int code, const std::string& type, const std::string& content, const std::string& extra = {}) {
        std::string head = "HTTP/1.1 " + std::to_string(code) + " " + statusText(code) + "\r\n";
        if (!type.empty()) head += "Content-Type: " + type + "\r\n";
        head += "Content-Length: " + std::to_string(content.size()) + "\r\nCache-Control: no-store\r\nConnection: close\r\n"
                "X-Content-Type-Options: nosniff\r\n" + extra + "\r\n";
        (void)c.sendAll(head + content, kWait);
    };
    const auto redirect = [&](const std::string& to, const std::string& extra = {}) { reply(303, {}, {}, "Location: " + to + "\r\n" + extra); };

    const double now = clockSeconds();
    std::unique_lock<std::mutex> lock(mutex_);
    const WebAccess cfg = settings_;
    const std::string project = project_;
    lastRequest_ = now;
    Session* session = cookie.empty() ? nullptr : sessionOf(cookie, now);
    const auto activeCount = [&] {
        return static_cast<int>(std::count_if(sessions_.begin(), sessions_.end(), [&](const Session& s) { return now - s.lastSeen < 30.0; }));
    };
    // Sans connexion demandee : une session d'office, par navigateur.
    std::string setCookie;
    if (!session && !cfg.login) {
        if (activeCount() >= cfg.maxClients) {
            lock.unlock();
            reply(503, "text/plain; charset=utf-8", "Trop de navigateurs connect\xC3\xA9s (" + std::to_string(cfg.maxClients) + " au plus).");
            return;
        }
        Session s;
        s.token = randomHex(16);
        s.peer = peer;
        s.since = clockText();
        s.lastSeen = now;
        s.control = cfg.control;
        event("Navigateur connect\xC3\xA9 : " + peer + (s.control ? " (commande)" : " (lecture seule)"));
        sessions_.push_back(std::move(s));
        session = &sessions_.back();
        setCookie = "Set-Cookie: " + std::string(kCookie) + "=" + session->token + "; Path=/; HttpOnly; SameSite=Strict\r\n";
    }
    if (session) session->lastSeen = now;

    if (path == "/favicon.ico") {
        lock.unlock();
        reply(204, {}, {});
        return;
    }
    if (path == "/connexion" && method == "POST") {
        auto& f = failures_[peer];
        if (f.first >= 5 && now - f.second < 60.0) {
            lock.unlock();
            reply(429, "text/html; charset=utf-8", page(project, true, "Trop d'essais : attendez une minute.", cfg.refreshMs));
            return;
        }
        if (f.first >= 5) f = {0, now};
        const auto form = formOf(body);
        const std::string login = form.count("login") ? form.at("login") : std::string{};
        const std::string password = form.count("password") ? form.at("password") : std::string{};
        bool control = false;
        std::string why;
        const Authenticate auth = auth_;
        lock.unlock();
        const bool ok = auth && auth(login, password, &control, &why);
        lock.lock();
        if (!ok) {
            f = {f.first + 1, now};
            event("Connexion web refus\xC3\xA9" "e : " + login + " (" + peer + ") - " + why);
            lock.unlock();
            reply(401, "text/html; charset=utf-8", page(project, true, why.empty() ? std::string("Utilisateur ou mot de passe faux.") : why, cfg.refreshMs));
            return;
        }
        f = {0, now};
        if (activeCount() >= cfg.maxClients) {
            lock.unlock();
            reply(503, "text/html; charset=utf-8", page(project, true, "Trop de navigateurs connect\xC3\xA9s.", cfg.refreshMs));
            return;
        }
        Session s;
        s.token = randomHex(16);
        s.user = login;
        s.peer = peer;
        s.since = clockText();
        s.lastSeen = now;
        s.control = cfg.control && control;
        event("Connexion web : " + login + " (" + peer + ")" + (s.control ? " - commande" : " - lecture seule"));
        const std::string token = s.token;
        sessions_.push_back(std::move(s));
        lock.unlock();
        redirect("/", "Set-Cookie: " + std::string(kCookie) + "=" + token + "; Path=/; HttpOnly; SameSite=Strict\r\n");
        return;
    }
    if (path == "/deconnexion") {
        if (session) {
            event("D\xC3\xA9" "connexion web : " + (session->user.empty() ? std::string("anonyme") : session->user) + " (" + peer + ")");
            const std::string token = session->token;
            std::erase_if(sessions_, [&](const Session& s) { return s.token == token; });
        }
        lock.unlock();
        redirect("/", "Set-Cookie: " + std::string(kCookie) + "=; Path=/; Max-Age=0\r\n");
        return;
    }
    if (!session) {
        lock.unlock();
        if (path == "/" || path == "/index.html") reply(200, "text/html; charset=utf-8", page(project, true, {}, cfg.refreshMs));
        else reply(401, "text/plain; charset=utf-8", "Connexion demand\xC3\xA9" "e.");
        return;
    }
    if (path == "/" || path == "/index.html") {
        lock.unlock();
        reply(200, "text/html; charset=utf-8", page(project, false, {}, cfg.refreshMs), setCookie);
        return;
    }
    if (path == "/etat") {
        const std::string json = "{\"enMarche\":" + std::string(frame_.running && frame_.jpeg ? "true" : "false") + ",\"vue\":\"" + jsonEscape(frame_.view)
                               + "\",\"largeur\":" + std::to_string(frame_.width) + ",\"hauteur\":" + std::to_string(frame_.height)
                               + ",\"serie\":" + std::to_string(frame_.serial) + ",\"commande\":" + (session->control ? "true" : "false")
                               + ",\"utilisateur\":\"" + jsonEscape(session->user) + "\"}";
        lock.unlock();
        reply(200, "application/json; charset=utf-8", json, setCookie);
        return;
    }
    if (path == "/vue.jpg") {
        const auto jpeg = frame_.jpeg;
        ++session->images;
        lock.unlock();
        if (!jpeg) reply(404, "text/plain; charset=utf-8", "Pas encore d'image.");
        else reply(200, "image/jpeg", std::string(reinterpret_cast<const char*>(jpeg->data()), jpeg->size()), setCookie);
        return;
    }
    if (path == "/clic") {
        if (method != "POST") {
            lock.unlock();
            reply(405, "text/plain; charset=utf-8", "POST");
            return;
        }
        if (!session->control) {
            lock.unlock();
            reply(403, "text/plain; charset=utf-8", "Lecture seule.");
            return;
        }
        const auto form = formOf(body);
        Click k;
        k.x = form.count("x") ? std::atof(form.at("x").c_str()) : -1;
        k.y = form.count("y") ? std::atof(form.at("y").c_str()) : -1;
        k.user = session->user;
        k.peer = peer;
        if (k.x < 0 || k.y < 0 || k.x > frame_.width || k.y > frame_.height) {
            lock.unlock();
            reply(400, "text/plain; charset=utf-8", "Hors de la vue.");
            return;
        }
        ++session->clicks;
        clicks_.push_back(std::move(k));
        lock.unlock();
        reply(204, {}, {});
        return;
    }
    lock.unlock();
    reply(404, "text/plain; charset=utf-8", "Introuvable.");
}

} // namespace hmi::web
