// =============================================================================
//  app/SessionService.cpp - 1.43.0 : voir SessionService.hpp
//  1.45.0 : retirer un equipier, partir (l'accueil, la copie effacee), le journal.
// =============================================================================
#include "SessionService.hpp"

#include "App.hpp"
#include "Fenetres.hpp"           // 1.45.0 : une copie ouverte dans une autre fenetre n'est pas purgee
#include "Licence.hpp"
#include "LicenceService.hpp"     // 1.44.0 : l'adresse du service XPG (le relais des sessions par Internet)
#include "Notices.hpp"            // 1.45.0 : les departs, le retrait (la cloche)
#include "SessionMarks.hpp"
#include "SessionRelayHttp.hpp"   // 1.44.0
#include "Settings.hpp"
#include "screens/Screens.hpp"

#include "../core/Edition.hpp"
#include "../core/Version.hpp"
#include "../deploy/Crypto.hpp"
#include "../deploy/DevKey.hpp"
#include "../deploy/TlsIdentity.hpp"
#include "../hmi/HmiCommands.hpp"
#include "../hmi/HmiEditHistory.hpp"
#include "../hmi/HmiNetInfo.hpp"
#include "../hmi/HmiStore.hpp"
#include "../hmi/HmiTrash.hpp"
#include "../licensing/Licensing.hpp"
#include "../menu/MenuManager.hpp"
#include "../session/SessionLink.hpp"
#include "../session/SessionRelay.hpp"
#include "../session/SessionSync.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <set>

namespace app {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kChunk = 1024u * 1024u;   // le projet part en morceaux de 1 Mio
constexpr double kViewEvery = 0.06;             // un glisser : 16 envois par seconde au plus
constexpr double kProjectEvery = 0.35;          // le reste du projet (une saisie dans un script)
constexpr double kPresenceEvery = 0.1;
constexpr double kStateEvery = 4.0;             // les empreintes de l'hote
constexpr std::size_t kChatMax = 200;

double now() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

std::string clock() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[8];
    std::snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return b;
}

core::json::Value op(std::string_view name) {
    core::json::Value v = core::json::Value::object();
    v.set("op", std::string(name));
    return v;
}

std::string myName() {
    std::string n = licence::loadProfile().name;
    if (n.empty()) n = hmi::netinfo::computerName();
    if (n.empty()) n = "?";
    return n;
}

std::string mo(std::uint64_t bytes) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f Mo", static_cast<double>(bytes) / (1024.0 * 1024.0));
    std::string s = b;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

// 1.45.0 : les chemins en UTF-8 (C++20 : u8string est d'un autre type).
fs::path u8p(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string u8s(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// Le nom du projet tel que la barre du haut le montre : son dossier (le manifeste, faute de dossier).
std::string projectLabel(const App& app) {
    std::string n = fs::path(app.projectFolder()).filename().string();
    return n.empty() ? app.manifest().name : n;
}

// Un nom de dossier sur (le projet recu) : lettres, chiffres, espace, - _ . ; "Projet" sinon.
std::string folderName(const std::string& name) {
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == ' ' || c == '-' || c == '_' || c == '.' || u >= 0x80) out.push_back(c);
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    while (!out.empty() && (out.front() == '.' || out.front() == ' ')) out.erase(out.begin());
    if (out.size() > 60) out.resize(60);
    return out.empty() ? std::string("Projet") : out;
}

// Un chemin recu pour le dossier de session : relatif, sans remonter, sous ihm/ (ou le manifeste).
bool safePath(const std::string& p) {
    if (p.empty() || p.size() > 300 || p.front() == '/' || p.find("..") != std::string::npos || p.find('\\') != std::string::npos
        || p.find(':') != std::string::npos)
        return false;
    return p == "project.xpgproj" || p.rfind("ihm/", 0) == 0;
}

// 1.44.0 : le transport du relais (le service XPG : l'adresse du service des licences), signe par la cle de ce PC.
session::RelayTransportPtr relayTransport(std::string* why) {
    const auto a = licence::service::address();
    if (a.empty()) {
        if (why) *why = "ce programme n'a pas de service XPG (les sessions par Internet passent par le service des licences)";
        return nullptr;
    }
    sessionrelay::HttpOptions o;
    o.baseUrl = a.url;
    std::string product = core::productName();
    for (char& ch : product)
        if (ch == ' ') ch = '-';
    o.software = product + "/" + XPG_ANALYZER_VERSION;
    o.publicKey = [](deploy::crypto::PublicKey& pk, std::string& w) {
        std::string hex;
        deploy::crypto::Bytes b;
        if (!licensing::deviceKeyHex(hex, &w)) return false;
        if (!deploy::crypto::unhex(hex, b, pk.size())) {
            w = "la cl\xC3\xA9 de ce PC est mal \xC3\xA9" "crite";
            return false;
        }
        std::copy(b.begin(), b.end(), pk.begin());
        return true;
    };
    o.sign = [](std::string_view m, deploy::crypto::Signature& sig, std::string& w) {
        deploy::crypto::PublicKey pub{};
        return licensing::deviceSignServiceMessage(m, sig, pub, &w);
    };
    return sessionrelay::httpTransport(std::move(o));
}

// 1.44.0 : l'hote et l'equipier, sur le reseau du bureau (TLS) ou par Internet (le relais) : les memes appels.
struct HostAny {
    session::HostLink lan;
    session::RelayHostLink relay;
    bool internet{false};
    [[nodiscard]] std::vector<session::LinkEvent> poll() { return internet ? relay.poll() : lan.poll(); }
    void send(int conn, const core::json::Value& h, std::string data = {}) {
        if (internet) relay.send(conn, h, std::move(data));
        else lan.send(conn, h, std::move(data));
    }
    void closeAfter(int conn, const core::json::Value& h) {
        if (internet) relay.closeAfter(conn, h);
        else lan.closeAfter(conn, h);
    }
    void close(int conn) {
        if (internet) relay.close(conn);
        else lan.close(conn);
    }
    void stop() {
        lan.stop();
        relay.stop();
    }
    [[nodiscard]] int port() const noexcept { return internet ? -1 : lan.port(); }
    [[nodiscard]] int discoveryPort() const noexcept { return internet ? -1 : lan.discoveryPort(); }
    void setAnnounce(const session::Announce& a) {
        if (!internet) lan.setAnnounce(a);
    }
};
struct GuestAny {
    session::GuestLink lan;
    session::RelayGuestLink relay;
    bool internet{false};
    [[nodiscard]] std::vector<session::LinkEvent> poll() { return internet ? relay.poll() : lan.poll(); }
    void send(const core::json::Value& h, std::string data = {}) {
        if (internet) relay.send(h, std::move(data));
        else lan.send(h, std::move(data));
    }
    void closeAfter(const core::json::Value& h) {
        if (internet) relay.closeAfter(h);
        else lan.closeAfter(h);
    }
    void stop() {
        lan.stop();
        relay.stop();
    }
};

} // namespace

struct SessionService::Impl {
    HostAny host;     // 1.44.0 : le reseau du bureau ou Internet
    GuestAny guest;
    session::Mirror mirror;
    deploy::TlsIdentity identity;
    bool identityReady{false};

    std::weak_ptr<hmi::Document> doc;
    core::Connection docConn;
    std::set<hmi::Id> touched;
    bool projectTouched{false};
    bool applying{false};
    double lastViewSend{0}, lastProjectSend{0}, lastPresence{0}, lastState{0}, lastRemote{0}, lastLocalSend{0};
    std::uint64_t stateRevision{~0ull};
    std::uint64_t presenceSerial{~0ull};

    // l'hote
    std::map<int, int> memberOfConn;   // connexion -> membre
    std::map<int, int> connOfMember;
    std::map<std::pair<hmi::Id, hmi::Id>, int> held;   // (vue, objet) -> membre
    std::vector<int> knockQueue;     // les entrees a demander (une fenetre a la fois)
    bool knockDialogOpen{false};
    hmi::Id hostBlock{hmi::kNoId};

    // l'equipier
    SessionService::JoinTarget target;
    std::map<std::string, std::string> incoming;
    std::uint64_t received{0}, total{0};
    std::string folder;
    hmi::Id block{hmi::kNoId};
    std::vector<session::LinkEvent> early;   // les changements arrives avant l'ouverture du projet
    bool projectOpen{false};
    std::map<std::string, int> resyncAsked;
    double lastResync{0};

    // la recherche
    std::future<std::vector<session::Found>> searching;
    std::string searchWhy;

    // 1.45.0 : l'hote - l'empreinte de chaque membre (le retrait la retient), les PC retires, le journal a envoyer,
    // le dossier du projet (le journal s'y enregistre a la fin, meme si le projet vient d'etre ferme), le debut.
    std::map<int, std::string> printOf;
    std::set<std::string> removed;
    std::vector<std::uint64_t> journalDirty;
    std::string hostFolder;
    std::int64_t startedAt{0};
    std::uint64_t chatSerial{0};   // 1.50.0 : l'hote - le numero du dernier message (son identifiant "m<n>")
};

SessionService::SessionService(App& app) : d_(std::make_unique<Impl>()), app_(app) {
    sessionmarks::setRefusedNotice([this](const std::string& t) { app_.events().publish(StatusNotice{t, 5.0}); });
    // 1.45.0 : ce que l'interface demande (le panneau du bas, l'arbre du projet).
    sessionmarks::Hooks h;
    h.say = [this](const std::string& t, const std::string& replyTo) { say(t, replyTo); };   // 1.50.0 : la reponse
    h.follow = [this](int n) { follow(n); };
    h.exportJournal = [this](std::string* why) { return exportJournal(why); };
    sessionmarks::setHooks(std::move(h));
    // 1.45.0 : L'EQUIPIER NE GARDE RIEN - ce qu'un arret brutal a laisse dans le dossier des copies part au lancement
    // (sauf une copie ouverte dans une autre fenetre de ce PC).
    if (const auto n = session::purgeArea(guestArea(), [](const std::string& f) { return fenetres::autreFenetre(f).has_value(); }); n > 0)
        log_.push_back("lancement : " + std::to_string(n) + " copie(s) de session effac\xC3\xA9" "e(s)");
}

SessionService::~SessionService() {
    if (mode_ == Mode::Host || mode_ == Mode::Guest || mode_ == Mode::Joining) stop();
    // 1.45.0 : l'appli se ferme - la copie de l'equipier s'efface (sans fenetre : l'appli part).
    if (!copySid_.empty()) (void)session::removeCopy(guestArea(), copySid_);
    sessionmarks::setRefusedNotice({});
    sessionmarks::setHooks({});
}

// 1.45.0 : le dossier des copies des equipiers.
std::string SessionService::guestArea() { return session::guestArea(Settings::sharedFolder()); }

bool SessionService::inGuestArea(const std::string& folder) { return session::insideArea(guestArea(), folder); }

const session::Member* SessionService::member(int number) const {
    for (const auto& m : members_)
        if (m.number == number) return &m;
    return nullptr;
}

std::string SessionService::viewName(hmi::Id view) const {
    const auto doc = app_.hmi();
    if (!doc || view == hmi::kNoId) return {};
    const auto* v = doc->project.view(view);
    return v ? v->name : std::string{};
}

int SessionService::port() const { return d_->host.port(); }
int SessionService::discoveryPort() const { return d_->host.discoveryPort(); }
std::string SessionService::myFingerprint() const { return d_->identityReady ? d_->identity.fingerprintHex() : std::string{}; }

std::string SessionService::codeText() const {
    if (code_.empty()) return {};
    return internet_ ? session::relayCodeShown(code_) : session::codeShown(code_);
}

bool SessionService::internetAvailable(std::string* why) {
    if (licence::service::address().empty()) {
        if (why) *why = "ce programme n'a pas de service XPG : seulement le r\xC3\xA9seau du bureau";
        return false;
    }
    return true;
}

std::string SessionService::chipText() const {
    if (mode_ == Mode::Joining) return "Session \xC2\xB7 on entre\xE2\x80\xA6";
    if (!active()) return {};
    return "Session \xC2\xB7 " + std::to_string(members_.size()) + (knocks_.empty() ? std::string{} : " \xC2\xB7 " + std::to_string(knocks_.size()) + " \xC3\xA0 faire entrer");
}

std::string SessionService::chipTip() const {
    if (!active()) return progress_;
    std::string t = "\xC2\xAB " + name_ + " \xC2\xBB \xE2\x80\x94 h\xC3\xB4te : " + hostName_ + (internet_ ? " \xC2\xB7 par Internet" : "") + ". ";
    for (const auto& m : members_) {
        t += "\n" + m.name + (m.number == me_ ? " (toi)" : std::string{}) + " \xC2\xB7 " + m.machine + " \xC2\xB7 " + session::roleText(m.role);
        if (const auto v = viewName(m.view); !v.empty()) t += " \xC2\xB7 " + v;
    }
    return t + "\nUn clic : l'\xC3\xA9quipe, la discussion.";
}

// ================================================================================================ l'hote ===
namespace {

bool loadIdentity(SessionService::Impl& d, std::string* why) {
    if (d.identityReady) return true;
    if (!deploy::crypto::init()) {
        if (why) *why = "la cryptographie ne d\xC3\xA9marre pas";
        return false;
    }
    const std::string folder = (fs::path(Settings::sharedFolder()) / "Deploiement").string();
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (!deploy::TlsIdentity::loadOrCreate(folder, "studio", false, "XPG-Studio " + deploy::DevKey::computerName(), d.identity, why)) return false;
    d.identityReady = true;
    return true;
}

} // namespace

bool SessionService::start(const StartOptions& o, std::string* why) {
    const auto fail = [&](std::string w) { if (why) *why = std::move(w); return false; };
    if (active() || mode_ == Mode::Joining) return fail("une session est d\xC3\xA9j\xC3\xA0 en cours");
    const auto doc = app_.hmi();
    if (!doc || app_.projectFolder().empty()) return fail("ouvre d'abord un projet IHM (enregistr\xC3\xA9 dans un dossier)");
    if (app_.manifest().state == project::State::Lock) return fail("le projet est verrouill\xC3\xA9 (LOCK) : on ne le modifie pas \xC3\xA0 plusieurs");
    if (!licence::allows("multi_user")) return fail("ta licence n'a pas \xC2\xAB Travail \xC3\xA0 plusieurs \xC2\xBB (l'offre ENTERPRISE, ou l'option)");
    if (!loadIdentity(*d_, why)) return false;
    unsigned char raw[8];
    deploy::crypto::randomBytes(raw, sizeof raw);
    sid_ = deploy::crypto::hex(raw, sizeof raw);
    name_ = o.name.empty() ? "Session " + projectLabel(app_) : o.name;
    entry_ = o.entry;
    defaultRole_ = o.role;
    hostName_ = myName();
    if (o.internet) {
        // 1.44.0 : par Internet - le code (10 caracteres) est toujours demande : c'est le secret du chiffrement.
        std::string code;
        if (!session::relayCodeTyped(o.code, code)) code = session::newRelayCode();
        auto transport = relayTransport(why);
        if (!transport) return false;
        session::RelayHostConfig rc;
        rc.transport = std::move(transport);
        rc.code = code;
        rc.sessionName = name_;
        rc.hostName = hostName_;
        rc.project = projectLabel(app_);
        d_->host.internet = true;
        if (!d_->host.relay.start(rc, why)) return false;
        entry_ = session::Entry::Code;
        code_ = code;
    } else {
        code_ = o.entry == session::Entry::Code ? session::newCode() : std::string{};
        session::HostConfig cfg;
        cfg.port = o.port;
        cfg.discoveryPort = o.discoveryPort;
        cfg.hostName = hostName_;
        cfg.sessionId = sid_;
        cfg.code = code_;
        d_->host.internet = false;
        if (!d_->host.lan.start(cfg, d_->identity, why)) return false;
    }
    internet_ = o.internet;
    relayOpen_ = false;
    // 1.45.0 : le journal, les PC retires, le dossier du projet (le journal s'y enregistre a la fin).
    journal_.clear();
    savedJournal_.clear();
    messagesIn_ = 0;
    d_->removed.clear();
    d_->printOf.clear();
    d_->journalDirty.clear();
    d_->hostFolder = app_.projectFolder();
    d_->startedAt = static_cast<std::int64_t>(std::time(nullptr));
    members_.clear();
    session::Member me;
    me.number = 0;
    me.name = hostName_;
    me.machine = hmi::netinfo::computerName();
    me.machineId = d_->identity.machineId();
    me.app = "IHM Studio";
    me.version = XPG_ANALYZER_VERSION;
    me.color = session::palette()[0];
    me.role = session::Role::Edit;
    members_.push_back(me);
    me_ = 0;
    knocks_.clear();
    chat_.clear();
    log_.clear();
    resyncs_ = refusals_ = 0;
    d_->memberOfConn.clear();
    d_->connOfMember.clear();
    d_->held.clear();
    mode_ = Mode::Host;
    // Le projet partage : celui d'aujourd'hui ; Ctrl+Z n'annule plus que ses propres gestes.
    doc->shared = true;
    doc->mergeShell = [](hmi::Project& cur, const hmi::Project& from, const hmi::Project& to) { return session::mergeShell(cur, from, to); };
    d_->mirror.reset(doc->project);
    d_->doc = doc;
    d_->touched.clear();
    d_->projectTouched = false;
    d_->docConn = doc->changed->connect([this](hmi::Id v) {
        if (d_->applying) return;
        if (v == hmi::kNoId) d_->projectTouched = true;
        else d_->touched.insert(v);
    });
    hmi::edits::setLocalStation(me.machineId);
    hmi::edits::setCurrentSession(name_);
    sessionmarks::setActive(true);
    sessionmarks::setHeld({});
    session::Announce a;
    a.session = name_;
    a.host = hostName_;
    a.machine = me.machine;
    a.machineId = me.machineId;
    a.app = "IHM Studio";
    a.version = XPG_ANALYZER_VERSION;
    a.project = projectLabel(app_);
    a.fingerprint = d_->identity.fingerprintHex();
    a.sessionId = sid_;
    a.members = 1;
    a.needsCode = !code_.empty();
    d_->host.setAnnounce(a);
    if (internet_) {
        log_.push_back("session d\xC3\xA9marr\xC3\xA9" "e : " + name_ + " (par Internet)");
        chat_.push_back({"", "Session d\xC3\xA9marr\xC3\xA9" "e par Internet (chiffr\xC3\xA9" "e de bout en bout). Le code : " + codeText()
                                 + " \xE2\x80\x94 tes \xC3\xA9quipiers le tapent dans Rejoindre une session \xE2\x80\xBA Par Internet.",
                         clock(), 0, true});
    } else {
        log_.push_back("session d\xC3\xA9marr\xC3\xA9" "e : " + name_ + " (port " + std::to_string(d_->host.port()) + ")");
        chat_.push_back({"", "Session d\xC3\xA9marr\xC3\xA9" "e. " + std::string(code_.empty() ? "Tu acceptes chaque entr\xC3\xA9" "e." : "Le code : " + codeText() + "."), clock(), 0, true});
    }
    ++revision_;
    return true;
}

namespace {

// Ce que la decouverte annonce : le nombre de membres change.
void announce(SessionService& s, HostAny& host, const std::string& project, const std::string& fp) {
    session::Announce a;
    a.session = s.sessionName();
    a.host = s.hostName();
    if (const auto* me = s.member(0)) {
        a.machine = me->machine;
        a.machineId = me->machineId;
    }
    a.app = "IHM Studio";
    a.version = XPG_ANALYZER_VERSION;
    a.project = project;
    a.fingerprint = fp;
    a.sessionId = s.sessionId();
    a.members = static_cast<int>(s.members().size());
    a.needsCode = !s.code().empty();
    host.setAnnounce(a);
}

core::json::Value membersJson(const std::vector<session::Member>& ms) {
    core::json::Value a = core::json::Value::array();
    for (const auto& m : ms) a.push(session::toJson(m));
    return a;
}

} // namespace

struct SessionServiceAccess {
    static void broadcast(SessionService& s, const core::json::Value& h, const std::string& data, int except = -1) {
        for (const auto& [conn, m] : s.d_->memberOfConn) {
            (void)m;
            if (conn != except) s.d_->host.send(conn, h, data);
        }
    }

    // ---- 1.50.0 : la discussion - l'identifiant de chaque message, la reponse a un message ----
    // Le message auquel `id` renvoie, tel que ce PC l'a (son auteur, son extrait) ; vide : inconnu.
    static session::ChatReply replyOf(const SessionService& s, const std::string& id) {
        session::ChatReply r;
        if (id.empty()) return r;
        for (auto it = s.chat_.rbegin(); it != s.chat_.rend(); ++it)
            if (!it->system && it->id == id) {
                r.id = id;
                r.from = it->from;
                r.excerpt = session::chatExcerpt(it->text);
                break;
            }
        return r;
    }
    // Un message dans la discussion de ce PC (la borne : les plus anciens tombent).
    static void keep(SessionService& s, const session::ChatMessage& m) {
        s.chat_.push_back({m.from, m.text, m.time, m.color, false, m.id, m.reply});
        if (s.chat_.size() > kChatMax) s.chat_.erase(s.chat_.begin());
    }
    // L'hote : le message part a chacun - avec `id` et `reponse` ; a un Studio d'avant la 1.50, la reponse part avec
    // la citation en premiere ligne du texte (il ignore `reponse`).
    static void broadcastChat(SessionService& s, const session::ChatMessage& m) {
        for (const auto& [conn, n] : s.d_->memberOfConn) {
            const auto* mem = s.member(n);
            s.d_->host.send(conn, session::chatFrame(m, mem ? std::string_view(mem->version) : std::string_view{}), {});
        }
    }
    // L'hote : le message d'un membre (le sien ou celui d'un equipier) - son identifiant, la reponse d'apres sa copie.
    static session::ChatMessage hostMessage(SessionService& s, const std::string& from, std::uint32_t color, const std::string& text,
                                            const session::ChatReply& asked) {
        session::ChatMessage m;
        m.id = "m" + std::to_string(++s.d_->chatSerial);
        m.from = from;
        m.color = color;
        m.text = text;
        m.time = clock();
        // Le message cite tel que l'hote l'a (il fait foi) ; inconnu de lui (tombe de la liste) : ce que l'equipier en dit.
        m.reply = replyOf(s, asked.id);
        if (m.reply.empty() && !asked.empty()) m.reply = asked;
        return m;
    }

    static void sendMembers(SessionService& s) {
        core::json::Value h = op("MEMBRES");
        h.set("membres", membersJson(s.members_));
        broadcast(s, h, {});
        announce(s, s.d_->host, projectLabel(s.app_), s.d_->identity.fingerprintHex());
        ++s.revision_;
    }

    // Les objets tenus (choisis) par chaque membre, pour tous ; chez l'hote : SessionMarks.
    static void sendHolds(SessionService& s) {
        core::json::Value h = op("RESERVATIONS");
        core::json::Value list = core::json::Value::array();
        for (const auto& [k, m] : s.d_->held) {
            core::json::Value x = core::json::Value::array();
            x.push(static_cast<std::int64_t>(k.first));
            x.push(static_cast<std::int64_t>(k.second));
            x.push(static_cast<std::int64_t>(m));
            list.push(std::move(x));
        }
        h.set("objets", std::move(list));
        broadcast(s, h, {});
        publishHolds(s);
    }

    static void publishHolds(SessionService& s) {
        sessionmarks::Held held;
        for (const auto& [k, m] : s.d_->held) {
            if (m == s.me_) continue;
            const auto* mm = s.member(m);
            held[k] = sessionmarks::Holder{m, mm ? mm->name : std::string("?"), mm ? mm->color : 0x475569u};
        }
        sessionmarks::setHeld(std::move(held));
        ++s.revision_;
    }

    // Le choix d'un membre : il tient ce qui n'est pas deja tenu par un autre.
    static bool hold(SessionService& s, int member, hmi::Id view, const std::vector<hmi::Id>& sel) {
        auto& held = s.d_->held;
        auto before = held;
        for (auto it = held.begin(); it != held.end();) it = it->second == member ? held.erase(it) : std::next(it);
        for (const auto id : sel) {
            const auto key = std::make_pair(view, id);
            if (!held.count(key)) held[key] = member;
        }
        return held != before;
    }

    static hmi::edits::Signature signatureOf(const SessionService& s, const session::Member& m) {
        hmi::edits::Signature sig;
        sig.t = static_cast<std::int64_t>(std::time(nullptr));
        sig.pc = m.machine;
        sig.account = m.name;
        sig.version = m.version;
        sig.station = m.machineId;
        sig.session = s.name_;
        return sig;
    }

    // ---- 1.45.0 : le journal (l'hote) ----
    static void journal(SessionService& s, int member, const std::vector<std::string>& lines) {
        if (s.mode_ != SessionService::Mode::Host || lines.empty()) return;
        const auto* m = s.member(member);
        const std::string who = m ? m->name : std::string("?");
        const std::uint32_t col = m ? m->color : 0x475569u;
        for (const auto& l : session::trimmed(lines)) {
            const auto& e = s.journal_.add(member, who, col, l);
            if (std::find(s.d_->journalDirty.begin(), s.d_->journalDirty.end(), e.n) == s.d_->journalDirty.end())
                s.d_->journalDirty.push_back(e.n);
        }
        ++s.revision_;
    }

    // Les entrees neuves (ou fondues) depuis la derniere image, a tous.
    static void sendJournal(SessionService& s) {
        if (s.d_->journalDirty.empty()) return;
        core::json::Value h = op("JOURNAL");
        core::json::Value list = core::json::Value::array();
        for (const auto& e : s.journal_.entries())
            if (std::find(s.d_->journalDirty.begin(), s.d_->journalDirty.end(), e.n) != s.d_->journalDirty.end()) list.push(session::toJson(e));
        s.d_->journalDirty.clear();
        if (list.items().empty()) return;
        h.set("entrees", std::move(list));
        broadcast(s, h, {});
    }

    // Tout le journal (un equipier qui entre).
    static core::json::Value wholeJournal(const SessionService& s) {
        core::json::Value h = op("JOURNAL");
        h.set("tout", true);
        core::json::Value list = core::json::Value::array();
        for (const auto& e : s.journal_.entries()) list.push(session::toJson(e));
        h.set("entrees", std::move(list));
        return h;
    }

    // ---- 1.45.0 : une nouvelle de la session (un depart, un retrait) : la discussion, la barre d'etat, la cloche ----
    static void tell(SessionService& s, const std::string& text, std::uint32_t color, const char* tone = "info") {
        s.chat_.push_back({"", text + ".", clock(), color, true});
        if (s.chat_.size() > kChatMax) s.chat_.erase(s.chat_.begin());
        s.app_.events().publish(StatusNotice{text + ".", 8.0});
        notices::Item n;
        n.category = "Session";
        n.title = text;
        n.detail = s.name_.empty() ? std::string{} : "\xC2\xAB " + s.name_ + " \xC2\xBB";
        n.tone = tone;
        (void)notices::post(std::move(n));
        ++s.revision_;
    }

    // L'hote : la nouvelle a tous les equipiers, puis chez lui.
    static void tellAll(SessionService& s, const std::string& text, std::uint32_t color, const char* tone = "info") {
        core::json::Value h = op("NOUVELLE");
        h.set("texte", text);
        h.set("couleur", static_cast<std::int64_t>(color));
        h.set("ton", std::string(tone));
        broadcast(s, h, {});
        tell(s, text, color, tone);
    }

    // ---- la sortie de ce PC : ses changements, son choix ----
    static void flush(SessionService& s, bool force) {
        auto doc = s.d_->doc.lock();
        if (!doc) return;
        const double t = now();
        const bool viewsDue = !s.d_->touched.empty() && (force || t - s.d_->lastViewSend >= kViewEvery);
        const bool projectDue = s.d_->projectTouched && (force || t - s.d_->lastProjectSend >= kProjectEvery);
        if (!viewsDue && !projectDue) return;
        std::set<hmi::Id> touched;
        if (viewsDue) touched.swap(s.d_->touched);
        std::vector<session::ViewDelta> views;
        session::ProjectDelta pd;
        // 1.45.0 : l'hote - ce que ses changements remplacent (son journal).
        session::Mirror::Previous prev;
        const bool host = s.mode_ == SessionService::Mode::Host;
        s.d_->mirror.collect(doc->project, touched, projectDue, views, pd, host ? &prev : nullptr);
        if (host && (!views.empty() || !pd.empty())) {
            std::vector<std::string> lines;
            for (const auto& v : views) {
                const auto it = prev.views.find(v.view);
                const auto* now = doc->project.view(v.view);
                for (auto& l : session::describeView(v, it == prev.views.end() ? nullptr : &it->second, now ? now->name : std::string{}))
                    lines.push_back(std::move(l));
            }
            if (!pd.empty()) {
                for (auto& l : session::describeFiles(pd, [&](const std::string& path) { return prev.files.count(path) > 0; }))
                    lines.push_back(std::move(l));
                for (const auto& v : doc->project.views)
                    if (!prev.viewNames.count(v.id)) lines.push_back("vue " + v.name + " ajout\xC3\xA9" "e");
                for (const auto& [id, name] : prev.viewNames)
                    if (!doc->project.view(id)) lines.push_back("vue " + name + " supprim\xC3\xA9" "e");
            }
            journal(s, 0, lines);
        }
        if (viewsDue) s.d_->lastViewSend = t;
        if (projectDue) {
            s.d_->projectTouched = false;
            s.d_->lastProjectSend = t;
        }
        for (const auto& v : views) {
            core::json::Value h;
            std::string data;
            session::encodeView(v, h, data);
            h.set("de", s.me_);
            send(s, h, data);
        }
        if (!pd.empty()) {
            core::json::Value h;
            std::string data;
            session::encodeProject(pd, h, data);
            h.set("de", s.me_);
            send(s, h, data);
        }
        if (!views.empty() || !pd.empty()) s.d_->lastLocalSend = t;
    }

    static void send(SessionService& s, const core::json::Value& h, const std::string& data) {
        if (s.mode_ == SessionService::Mode::Host) broadcast(s, h, data);
        else if (s.mode_ == SessionService::Mode::Guest) s.d_->guest.send(h, data);
    }

    static void presence(SessionService& s) {
        const auto& mine = sessionmarks::current();
        if (mine.serial == s.d_->presenceSerial) return;
        const double t = now();
        if (t - s.d_->lastPresence < kPresenceEvery) return;
        s.d_->lastPresence = t;
        s.d_->presenceSerial = mine.serial;
        if (s.mode_ == SessionService::Mode::Host) {
            for (auto& m : s.members_)
                if (m.number == 0) {
                    m.view = mine.view;
                    m.selection = mine.selection;
                }
            const bool changed = hold(s, 0, mine.view, mine.selection);
            sendMembers(s);
            if (changed) sendHolds(s);
        } else if (s.mode_ == SessionService::Mode::Guest) {
            core::json::Value h = op("PRESENCE");
            h.set("vue", static_cast<std::int64_t>(mine.view));
            core::json::Value sel = core::json::Value::array();
            for (const auto id : mine.selection) sel.push(static_cast<std::int64_t>(id));
            h.set("choisis", std::move(sel));
            s.d_->guest.send(h);
        }
    }

    // ---- l'hote : ce qui arrive ----
    static void admit(SessionService& s, int conn) {
        const auto it = std::find_if(s.knocks_.begin(), s.knocks_.end(), [&](const SessionService::Knock& k) { return k.conn == conn; });
        if (it == s.knocks_.end()) return;
        const SessionService::Knock k = *it;
        s.knocks_.erase(it);
        auto doc = s.d_->doc.lock();
        if (!doc) return;
        if (s.members_.size() >= static_cast<std::size_t>(session::kMaxMembers)) {
            core::json::Value r = op("REFUS");
            r.set("raison", "la session est pleine (" + std::to_string(session::kMaxMembers) + " personnes)");
            s.d_->host.closeAfter(conn, r);
            return;
        }
        int n = 1;
        while (n < 16 && s.member(n)) ++n;
        if (n >= 16) return;
        session::Member m;
        m.number = n;
        m.name = k.name.empty() ? std::string("\xC3\x89quipier ") + std::to_string(n) : k.name;
        m.machine = k.machine;
        m.machineId = k.machineId;
        m.app = k.app;
        m.version = k.version;
        m.color = session::freeColor(s.members_, k.color);
        m.role = s.defaultRole_;
        s.members_.push_back(m);
        s.d_->memberOfConn[conn] = n;
        s.d_->connOfMember[n] = conn;
        // 1.45.0 : son empreinte (un retrait la retient) ; l'hote qui le fait entrer de nouveau le pardonne.
        const std::string print = !k.fingerprint.empty() ? k.fingerprint : k.machineId;
        s.d_->printOf[n] = print;
        s.d_->removed.erase(print);
        // Ce qui n'est pas encore parti part d'abord : le projet envoye est l'etat partage.
        flush(s, true);
        core::json::Value e = op("ENTREE");
        e.set("moi", session::toJson(m));
        e.set("membres", membersJson(s.members_));
        e.set("session", s.name_);
        e.set("sid", s.sid_);
        e.set("hote", s.hostName_);
        e.set("bloc", static_cast<std::int64_t>(session::firstFreeInBlock(doc->project, n)));
        core::json::Value chat = core::json::Value::array();
        const std::size_t from = s.chat_.size() > 50 ? s.chat_.size() - 50 : 0;
        const bool replies = session::knowsReplies(m.version);   // 1.50.0 : un Studio d'avant lit la citation dans le texte
        for (std::size_t i = from; i < s.chat_.size(); ++i) {
            if (s.chat_[i].system) continue;
            core::json::Value c = core::json::Value::object();
            const auto& l = s.chat_[i];
            session::writeChat(c, session::ChatMessage{l.id, l.from, l.text, l.time, l.color, l.reply}, replies);
            chat.push(std::move(c));
        }
        e.set("messages", std::move(chat));
        s.d_->host.send(conn, e);
        s.d_->host.send(conn, wholeJournal(s));   // 1.45.0 : tout le journal (un equipier qui entre tard l'a)
        // Le projet : le manifeste, puis les fichiers d'ihm/ (tels que l'enregistrement les ecrit), en morceaux.
        std::vector<std::pair<std::string, std::shared_ptr<const std::vector<std::uint8_t>>>> files;
        {
            std::ifstream in(fs::path(s.app_.projectFolder()) / "project.xpgproj", std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            files.emplace_back("project.xpgproj", std::make_shared<const std::vector<std::uint8_t>>(bytes.begin(), bytes.end()));
        }
        for (auto& f : hmi::serializeProject(doc->project)) files.emplace_back("ihm/" + f.path, f.data);
        std::uint64_t total = 0;
        for (const auto& f : files) total += f.second ? f.second->size() : 0;
        for (const auto& [path, data] : files) {
            const std::size_t size = data ? data->size() : 0;
            std::size_t at = 0;
            do {
                const std::size_t n2 = std::min(kChunk, size - at);
                core::json::Value h = op("PROJET");
                h.set("chemin", path);
                h.set("debut", static_cast<std::int64_t>(at));
                h.set("taille", static_cast<std::int64_t>(size));
                h.set("total", static_cast<std::int64_t>(total));
                std::string chunk;
                if (n2) chunk.assign(reinterpret_cast<const char*>(data->data()) + at, n2);
                s.d_->host.send(conn, h, std::move(chunk));
                at += n2;
            } while (at < size);
        }
        core::json::Value fin = op("PROJET_FIN");
        fin.set("fichiers", static_cast<std::int64_t>(files.size()));
        fin.set("nom", projectLabel(s.app_));
        s.d_->host.send(conn, fin);
        sendMembers(s);
        sendHolds(s);
        s.chat_.push_back({"", m.name + " est entr\xC3\xA9" "e (" + m.machine + (m.machineId.empty() ? std::string{} : " \xC2\xB7 " + m.machineId) + ").", clock(), m.color, true});
        s.log_.push_back("entree : " + m.name + " (membre " + std::to_string(n) + ")");
        s.app_.events().publish(StatusNotice{m.name + " est entr\xC3\xA9" "e dans la session.", 5.0});
        ++s.revision_;
    }

    static void hostFrame(SessionService& s, int conn, const core::json::Value& h, const std::string& data) {
        const auto mc = s.d_->memberOfConn.find(conn);
        const std::string kind = h.str("op");
        if (mc == s.d_->memberOfConn.end()) {
            if (kind == "AUREVOIR") {
                std::erase_if(s.knocks_, [&](const SessionService::Knock& k) { return k.conn == conn; });
                ++s.revision_;
            }
            return;   // pas encore entre : rien d'autre ne compte
        }
        const int n = mc->second;
        session::Member* m = nullptr;
        for (auto& x : s.members_)
            if (x.number == n) m = &x;
        if (!m) return;
        auto doc = s.d_->doc.lock();
        if (!doc) return;
        if (kind == "VUE") {
            session::ViewDelta d;
            std::string why;
            if (!session::decodeView(h, data, d, &why)) return;
            const auto* v = doc->project.view(d.view);
            // Refuse : il regarde seulement, ou un objet est tenu par un autre.
            std::string refused;
            if (m->role == session::Role::Watch) refused = "tu regardes seulement : demande \xC3\xA0 " + s.hostName_ + " de te laisser modifier";
            for (const auto id : d.touched()) {
                if (!refused.empty()) break;
                const auto hk = s.d_->held.find({d.view, id});
                if (hk != s.d_->held.end() && hk->second != n) {
                    const auto* holder = s.member(hk->second);
                    const auto* o = v ? v->object(id) : nullptr;
                    refused = "\xC2\xAB " + (o ? o->name : std::string("un objet")) + " \xC2\xBB est pris par " + (holder ? holder->name : std::string("un autre"));
                }
            }
            if (!refused.empty() || !v) {
                if (v) {
                    auto fix = session::Mirror::objectsOf(*v, d.touched());
                    if (d.shellChanged) {
                        fix.shellChanged = true;
                        fix.shell = *v;
                        fix.shell.objects.clear();
                    }
                    core::json::Value fh;
                    std::string fd;
                    session::encodeView(fix, fh, fd);
                    fh.set("de", 0);
                    s.d_->host.send(conn, fh, fd);
                }
                core::json::Value r = op("REFUSE_CHANGEMENT");
                r.set("raison", refused.empty() ? std::string("la vue n'existe plus") : refused);
                s.d_->host.send(conn, r);
                ++s.refusals_;
                s.log_.push_back("changement refuse (" + m->name + ") : " + (refused.empty() ? std::string("vue absente") : refused));
                return;
            }
            // 1.45.0 : les phrases du journal, avant (la vue d'avant).
            auto lines = session::describeView(d, v, v->name);
            s.d_->applying = true;
            const bool ok = s.d_->mirror.applyView(doc->project, d, &why);
            if (ok) journal(s, n, lines);
            if (ok) {
                const auto sig = signatureOf(s, *m);
                for (const auto id : d.touched()) doc->editAuthors[id] = sig;
                doc->touched(d.view);
            }
            s.d_->applying = false;
            if (ok) {
                core::json::Value relay = h;
                relay.set("de", n);
                broadcast(s, relay, data, conn);
                s.d_->lastRemote = now();
            }
            return;
        }
        if (kind == "FICHIERS") {
            session::ProjectDelta pd;
            std::string why;
            if (!session::decodeProject(h, data, pd, &why)) return;
            if (m->role == session::Role::Watch) {
                // L'etat juste de ce qu'il a touche (et l'index : une vue qu'il aurait creee repart).
                session::ProjectDelta back;
                std::set<std::string> paths{"ihm.txt"};
                for (const auto& f : pd.files) paths.insert(f.path);
                for (const auto& p : paths) {
                    session::FileDelta f;
                    s.d_->mirror.fileOf(doc->project, p, f);
                    back.files.push_back(std::move(f));
                }
                core::json::Value bh;
                std::string bd;
                session::encodeProject(back, bh, bd);
                bh.set("de", 0);
                s.d_->host.send(conn, bh, bd);
                core::json::Value r = op("REFUSE_CHANGEMENT");
                r.set("raison", "tu regardes seulement : demande \xC3\xA0 " + s.hostName_ + " de te laisser modifier");
                s.d_->host.send(conn, r);
                ++s.refusals_;
                return;
            }
            const hmi::Project before = doc->project;
            // 1.45.0 : les phrases du journal (les fichiers d'avant : l'etat partage).
            auto lines = session::describeFiles(pd, [&](const std::string& path) { return s.d_->mirror.hasFile(path); });
            session::ProjectApplied done;
            s.d_->applying = true;
            const bool ok = s.d_->mirror.applyProject(doc->project, pd, done, &why);
            if (ok) {
                for (const auto id : done.addedViews)
                    if (const auto* nv = doc->project.view(id)) lines.push_back("vue " + nv->name + " ajout\xC3\xA9" "e");
                for (const auto& gone : done.removed) lines.push_back("vue " + gone.name + " supprim\xC3\xA9" "e (dans la corbeille de l'h\xC3\xB4te)");
                journal(s, n, lines);
                // Ce qu'un equipier retire part dans la corbeille de l'hote (Ctrl+Z ne le rend pas : la corbeille, si).
                (void)hmi::bin::capture(before, doc->project);
                doc->touched(hmi::kNoId);
            }
            s.d_->applying = false;
            if (!ok) {
                s.log_.push_back("fichiers refuses (" + m->name + ") : " + why);
                return;
            }
            // Ce qui est passe repart aux autres ; un morceau qui n'a pas trouve sa place : l'etat de l'hote, a tous.
            session::ProjectDelta relay = pd;
            if (!done.conflicts.empty()) {
                std::erase_if(relay.files, [&](const session::FileDelta& f) {
                    return std::find(done.conflicts.begin(), done.conflicts.end(), f.path) != done.conflicts.end();
                });
                session::ProjectDelta fix;
                for (const auto& p : done.conflicts) {
                    session::FileDelta f;
                    s.d_->mirror.fileOf(doc->project, p, f);
                    fix.files.push_back(f);
                    relay.files.push_back(std::move(f));
                }
                core::json::Value fh;
                std::string fd;
                session::encodeProject(fix, fh, fd);
                fh.set("de", 0);
                s.d_->host.send(conn, fh, fd);
                s.log_.push_back("conflit sur " + std::to_string(done.conflicts.size()) + " fichier(s) : l'etat de l'hote l'emporte");
            }
            core::json::Value rh;
            std::string rd;
            session::encodeProject(relay, rh, rd);
            rh.set("de", n);
            broadcast(s, rh, rd, conn);
            s.d_->lastRemote = now();
            return;
        }
        if (kind == "PRESENCE") {
            const auto v = h.integer("vue");
            m->view = v > 0 && v <= 0xFFFFFFFFll ? static_cast<hmi::Id>(v) : hmi::kNoId;
            m->selection.clear();
            for (const auto& x : h.at("choisis").items()) {
                if (m->selection.size() >= 4096) break;
                const auto id = x.asInt();
                if (id > 0 && id <= 0xFFFFFFFFll) m->selection.push_back(static_cast<hmi::Id>(id));
            }
            const bool changed = hold(s, n, m->view, m->role == session::Role::Watch ? std::vector<hmi::Id>{} : m->selection);
            sendMembers(s);
            if (changed) sendHolds(s);
            return;
        }
        if (kind == "MESSAGE") {
            std::string text = h.str("texte").substr(0, session::kMessageMax);
            if (text.empty()) return;
            // 1.50.0 : l'identifiant (donne ici, le meme pour tous) et la reponse ; un Studio d'avant : la citation.
            const auto msg = hostMessage(s, m->name, m->color, text, session::readReply(h));
            broadcastChat(s, msg);
            keep(s, msg);
            ++s.messagesIn_;   // 1.45.0 : le non lu de l'onglet Session
            ++s.revision_;
            return;
        }
        if (kind == "RESYNC") {
            ++s.resyncs_;
            for (const auto& x : h.at("vues").items()) {
                const auto id = x.asInt();
                if (id <= 0 || id > 0xFFFFFFFFll) continue;
                if (const auto* v = doc->project.view(static_cast<hmi::Id>(id))) {
                    core::json::Value vh;
                    std::string vd;
                    session::encodeView(session::Mirror::fullView(*v), vh, vd);
                    vh.set("de", 0);
                    vh.set("rattrapage", true);
                    s.d_->host.send(conn, vh, vd);
                }
            }
            session::ProjectDelta fix;
            for (const auto& x : h.at("fichiers").items()) {
                session::FileDelta f;
                s.d_->mirror.fileOf(doc->project, x.asString(), f);
                fix.files.push_back(std::move(f));
            }
            if (!fix.empty()) {
                core::json::Value fh;
                std::string fd;
                session::encodeProject(fix, fh, fd);
                fh.set("de", 0);
                fh.set("rattrapage", true);
                s.d_->host.send(conn, fh, fd);
            }
            s.log_.push_back("rattrapage demande par " + m->name);
            return;
        }
        if (kind == "AUREVOIR") {
            hostGone(s, conn, {});   // 1.45.0 : il part de lui-meme (<< Lea a quitte la session >>, sans raison)
            s.d_->host.close(conn);
            return;
        }
    }

    static void hostGone(SessionService& s, int conn, const std::string& why) {
        std::erase_if(s.knocks_, [&](const SessionService::Knock& k) { return k.conn == conn; });
        const auto mc = s.d_->memberOfConn.find(conn);
        if (mc == s.d_->memberOfConn.end()) {
            ++s.revision_;
            return;
        }
        const int n = mc->second;
        s.d_->memberOfConn.erase(mc);
        s.d_->connOfMember.erase(n);
        std::string who = "?";
        std::uint32_t col = 0;
        for (auto it = s.members_.begin(); it != s.members_.end(); ++it)
            if (it->number == n) {
                who = it->name;
                col = it->color;
                s.members_.erase(it);
                break;
            }
        for (auto it = s.d_->held.begin(); it != s.d_->held.end();) it = it->second == n ? s.d_->held.erase(it) : std::next(it);
        s.d_->printOf.erase(n);
        s.log_.push_back("depart : " + who);
        sendMembers(s);
        sendHolds(s);
        // 1.45.0 : les autres le savent (la discussion, une notification) ; un lien perdu le dit.
        tellAll(s, who + " a quitt\xC3\xA9 la session" + std::string(why.empty() ? "" : " (" + why + ")"), col);
    }

    // 1.44.0 : la session par Internet perdue (le relais : elle ne s'ouvre pas, elle n'existe plus, plus de reseau).
    static void relayLost(SessionService& s, const std::string& why) {
        const bool opened = s.relayOpen_;
        s.log_.push_back("relais : " + why);
        s.stop(opened ? "la session par Internet s'est arr\xC3\xAAt\xC3\xA9" "e (" + why + ")" : std::string{});
        s.progress_ = opened ? "La session par Internet s'est arr\xC3\xAAt\xC3\xA9" "e : " + why : "La session ne d\xC3\xA9marre pas : " + why;
        if (!opened && session::relayTooOld(why)) s.progress_ = why;   // 1.45.0 : un service en 1.43 - la phrase seule
        if (!s.app_.scripted())
            s.app_.menus().ShowDialog(std::make_unique<MessageDialog>(opened ? "La session est termin\xC3\xA9" "e" : "D\xC3\xA9marrer une session",
                                                                      s.progress_ + ".", MessageDialog::Icon::Warning),
                                      [](const menu::DialogResult&) {});
        s.app_.events().publish(StatusNotice{s.progress_ + ".", 8.0});
        ++s.revision_;
    }

    static void askKnock(SessionService& s) {
        if (s.d_->knockDialogOpen || s.d_->knockQueue.empty() || s.app_.scripted()) return;
        const int conn = s.d_->knockQueue.front();
        s.d_->knockQueue.erase(s.d_->knockQueue.begin());
        const auto it = std::find_if(s.knocks_.begin(), s.knocks_.end(), [&](const SessionService::Knock& k) { return k.conn == conn; });
        if (it == s.knocks_.end()) return;
        s.d_->knockDialogOpen = true;
        const std::string text = it->name + (it->removed ? " (retir\xC3\xA9 tout \xC3\xA0 l'heure) veut revenir dans \xC2\xAB " : " veut entrer dans \xC2\xAB ") + s.name_ + " \xC2\xBB.\n\n"
                               + "PC : " + it->machine + (it->machineId.empty() ? std::string{} : " \xC2\xB7 identifiant " + it->machineId) + "\n"
                               + it->app + " " + it->version + " \xC2\xB7 " + it->address + "\n\n"
                               + "En entrant, " + it->name + " " + (s.defaultRole_ == session::Role::Watch ? "regarde seulement" : "peut modifier")
                               + " (\xC3\xA7" "a se change ensuite, personne par personne).";
        s.app_.menus().ShowDialog(std::make_unique<MessageDialog>(it->name + " veut entrer", text, MessageDialog::Icon::Question, "La faire entrer"),
                                  [&s, conn](const menu::DialogResult& r) {
                                      s.d_->knockDialogOpen = false;
                                      if (r.accepted()) s.accept(conn);
                                      else s.refuse(conn, s.hostName_ + " ne t'a pas fait entrer");
                                  });
    }

    // ---- l'equipier : ce qui arrive ----
    static void guestFrame(SessionService& s, const core::json::Value& h, const std::string& data) {
        const std::string kind = h.str("op");
        if (kind == "ATTENTE") {
            s.progress_ = (s.internet_ ? std::string("Connect\xC3\xA9" "e par Internet \xC2\xB7 chiffr\xC3\xA9 de bout en bout. ")
                                       : "Connect\xC3\xA9" "e \xC3\xA0 " + s.d_->target.machine + " \xC2\xB7 chiffr\xC3\xA9. ")
                        + (s.d_->target.hostName.empty() ? std::string("L'h\xC3\xB4te") : s.d_->target.hostName) + " doit te faire entrer\xE2\x80\xA6";
            ++s.revision_;
            return;
        }
        if (kind == "REFUS") {
            const std::string why = h.str("raison");
            s.log_.push_back("refus : " + why);
            s.progress_ = "Refus\xC3\xA9" "e : " + why;
            s.d_->guest.stop();
            s.mode_ = SessionService::Mode::Off;
            ++s.revision_;
            if (!s.app_.scripted())
                s.app_.menus().ShowDialog(std::make_unique<MessageDialog>("Tu n'es pas entr\xC3\xA9" "e", "La session ne t'a pas fait entrer : " + why + ".",
                                                                          MessageDialog::Icon::Warning),
                                          [](const menu::DialogResult&) {});
            s.app_.events().publish(StatusNotice{"Session : " + why, 6.0});
            return;
        }
        if (kind == "ENTREE") {
            session::Member me;
            if (!session::fromJson(h.at("moi"), me)) return;
            s.me_ = me.number;
            s.members_.clear();
            for (const auto& x : h.at("membres").items()) {
                session::Member m;
                if (session::fromJson(x, m)) s.members_.push_back(m);
            }
            s.name_ = h.str("session");
            s.sid_ = h.str("sid");
            s.hostName_ = h.str("hote");
            const auto b = h.integer("bloc");
            s.d_->block = b > 0 && b <= 0xFFFFFFFFll ? static_cast<hmi::Id>(b) : session::blockStart(me.number);
            s.chat_.clear();
            for (const auto& x : h.at("messages").items()) keep(s, session::readChat(x));   // 1.50.0 : id, reponse
            s.progress_ = (s.hostName_.empty() ? std::string("L'h\xC3\xB4te") : s.hostName_) + " t'a fait entrer. Le projet arrive\xE2\x80\xA6";
            s.d_->incoming.clear();
            s.d_->received = 0;
            s.d_->total = 0;
            ++s.revision_;
            return;
        }
        if (kind == "PROJET") {
            const std::string path = h.str("chemin");
            if (!safePath(path)) return;
            const auto at = h.integer("debut", -1), size = h.integer("taille", -1);
            if (at < 0 || size < 0 || size > 512ll * 1024 * 1024) return;
            auto& buf = s.d_->incoming[path];
            if (static_cast<std::int64_t>(buf.size()) != at) return;   // dans l'ordre, sans trou
            buf += data;
            s.d_->received += data.size();
            s.d_->total = static_cast<std::uint64_t>(std::max<std::int64_t>(0, h.integer("total")));
            s.progress_ = "Le projet arrive : " + mo(s.d_->received) + " sur " + mo(s.d_->total);
            ++s.revision_;
            return;
        }
        if (kind == "PROJET_FIN") {
            openProject(s, h.str("nom"));
            return;
        }
        // 1.45.0 : le journal de l'hote (tout, a l'entree ; puis les entrees neuves ou fondues).
        if (kind == "JOURNAL") {
            std::vector<session::JournalEntry> all;
            for (const auto& x : h.at("entrees").items()) {
                session::JournalEntry e;
                if (all.size() < session::kJournalMax * 2 && session::fromJson(x, e)) all.push_back(std::move(e));
            }
            if (h.flag("tout")) s.journal_.replace(std::move(all));
            else
                for (const auto& e : all) s.journal_.put(e);
            ++s.revision_;
            return;
        }
        // 1.45.0 : une nouvelle de la session (un depart, un retrait) : la discussion, la barre d'etat, la cloche.
        if (kind == "NOUVELLE") {
            const std::string text = h.str("texte").substr(0, 300);
            if (!text.empty()) {
                const std::string tone = h.str("ton");
                tell(s, text, static_cast<std::uint32_t>(h.integer("couleur") & 0xFFFFFF), tone == "warning" ? "warning" : "info");
            }
            return;
        }
        if (!s.d_->projectOpen) return;
        auto doc = s.d_->doc.lock();
        if (kind == "VUE" && doc) {
            session::ViewDelta d;
            std::string why;
            if (!session::decodeView(h, data, d, &why)) return;
            s.d_->applying = true;
            if (s.d_->mirror.applyView(doc->project, d, &why)) doc->touched(d.view);
            s.d_->applying = false;
            s.d_->lastRemote = now();
            if (h.flag("rattrapage")) ++s.resyncs_;
            return;
        }
        if (kind == "FICHIERS" && doc) {
            session::ProjectDelta pd;
            std::string why;
            if (!session::decodeProject(h, data, pd, &why)) return;
            session::ProjectApplied done;
            s.d_->applying = true;
            const bool ok = s.d_->mirror.applyProject(doc->project, pd, done, &why);
            if (ok) doc->touched(hmi::kNoId);
            s.d_->applying = false;
            s.d_->lastRemote = now();
            if (!ok || !done.conflicts.empty()) {
                core::json::Value r = op("RESYNC");
                core::json::Value files = core::json::Value::array();
                for (const auto& p : done.conflicts) files.push(p);
                if (!ok) files.push("ihm.txt");
                r.set("fichiers", std::move(files));
                r.set("vues", core::json::Value::array());
                s.d_->guest.send(r);
            }
            if (h.flag("rattrapage")) ++s.resyncs_;
            return;
        }
        if (kind == "MEMBRES") {
            std::vector<session::Member> ms;
            for (const auto& x : h.at("membres").items()) {
                session::Member m;
                if (session::fromJson(x, m)) ms.push_back(m);
            }
            s.members_ = std::move(ms);
            publishGuestHolds(s);
            ++s.revision_;
            return;
        }
        if (kind == "RESERVATIONS") {
            s.d_->held.clear();
            for (const auto& x : h.at("objets").items()) {
                const auto& it = x.items();
                if (it.size() != 3) continue;
                const auto v = it[0].asInt(), o = it[1].asInt(), n = it[2].asInt();
                if (v <= 0 || o <= 0 || v > 0xFFFFFFFFll || o > 0xFFFFFFFFll || n < 0 || n > 15) continue;
                s.d_->held[{static_cast<hmi::Id>(v), static_cast<hmi::Id>(o)}] = static_cast<int>(n);
            }
            publishGuestHolds(s);
            return;
        }
        if (kind == "MESSAGE") {
            keep(s, session::readChat(h));   // 1.50.0 : son identifiant (celui de l'hote, ou tire du message), la reponse
            if (h.str("de") != myName()) ++s.messagesIn_;   // 1.45.0 : le non lu de l'onglet Session
            ++s.revision_;
            return;
        }
        if (kind == "REFUSE_CHANGEMENT") {
            ++s.refusals_;
            s.app_.events().publish(StatusNotice{"Ce changement n'est pas pass\xC3\xA9 : " + h.str("raison") + ".", 6.0});
            s.log_.push_back("changement refuse : " + h.str("raison"));
            return;
        }
        if (kind == "ETAT" && doc) {
            compareState(s, *doc, h);
            return;
        }
        if (kind == "AUREVOIR") {
            // 1.45.0 : retire par l'hote (<< Camille t'a retire de la session >>), ou la session arretee.
            ended(s, h.str("raison").empty() ? s.hostName_ + " a arr\xC3\xAAt\xC3\xA9 la session" : h.str("raison").substr(0, 300),
                  h.flag("retire"));
            return;
        }
    }

    static void publishGuestHolds(SessionService& s) { publishHolds(s); }

    static void openProject(SessionService& s, const std::string& name) {
        // Le dossier de la session, a part des projets : <dossier commun>/Sessions/<identifiant>.
        if (s.sid_.size() != 16 || s.sid_.find_first_not_of("0123456789abcdef") != std::string::npos) return;
        const fs::path base = fs::path(Settings::sharedFolder()) / "Sessions" / s.sid_;
        const fs::path folder = base / fs::u8path(folderName(name));
        std::error_code ec;
        fs::remove_all(base, ec);
        fs::create_directories(folder, ec);
        for (const auto& [path, bytes] : s.d_->incoming) {
            const fs::path target = folder / fs::path(path);
            fs::create_directories(target.parent_path(), ec);
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!out) {
                s.progress_ = "Le projet ne s'\xC3\xA9" "crit pas dans " + folder.string();
                return;
            }
        }
        s.d_->incoming.clear();
        s.d_->folder = folder.string();
        // 1.45.0 : la copie de la session (fermee et effacee quand ce PC part).
        s.copyFolder_ = folder.string();
        s.copySid_ = s.sid_;
        s.progress_ = "Le projet est arriv\xC3\xA9 (" + name + ")";
        s.d_->projectOpen = false;
        s.app_.openProjectFolderChecked(folder.string());
        auto doc = s.app_.hmi();
        if (!doc || s.app_.projectFolder() != folder.string()) {
            s.progress_ = "Le projet re\xC3\xA7u ne s'ouvre pas";
            s.stop();
            return;
        }
        doc->shared = true;
        doc->mergeShell = [](hmi::Project& cur, const hmi::Project& from, const hmi::Project& to) { return session::mergeShell(cur, from, to); };
        doc->project.nextId = std::max(doc->project.nextId, s.d_->block);   // ce PC cree dans son bloc
        s.d_->mirror.reset(doc->project);
        s.d_->doc = doc;
        s.d_->touched.clear();
        s.d_->projectTouched = false;
        s.d_->docConn = doc->changed->connect([&s](hmi::Id v) {
            if (s.d_->applying) return;
            if (v == hmi::kNoId) s.d_->projectTouched = true;
            else s.d_->touched.insert(v);
        });
        s.d_->projectOpen = true;
        s.mode_ = SessionService::Mode::Guest;
        hmi::edits::setLocalStation(s.d_->identity.machineId());
        hmi::edits::setCurrentSession(s.name_);
        sessionmarks::setActive(true);
        sessionmarks::setGuest(true);   // 1.45.0 : l'equipier ne garde rien (Enregistrer sous, les exports : refuses)
        publishHolds(s);
        s.chat_.push_back({"", "Tu es dans \xC2\xAB " + s.name_ + " \xC2\xBB. C'est " + s.hostName_
                                   + " qui enregistre ; tes gestes partent tout seuls. Le projet reste chez " + s.hostName_
                                   + " : quand tu pars, sa copie s'efface de ce PC.",
                           clock(), 0, true});
        s.log_.push_back("dans la session : " + s.name_ + " (membre " + std::to_string(s.me_) + ")");
        s.app_.events().publish(StatusNotice{"Tu es dans la session \xC2\xAB " + s.name_ + " \xC2\xBB : " + std::to_string(s.members_.size()) + " personnes.", 6.0});
        ++s.revision_;
        // Ce qui est arrive pendant l'ouverture.
        auto early = std::move(s.d_->early);
        s.d_->early.clear();
        for (auto& e : early) guestFrame(s, e.header, e.data);
    }

    static void compareState(SessionService& s, hmi::Document& doc, const core::json::Value& h) {
        const double t = now();
        // Seulement au calme : rien a envoyer, rien d'envoye ni de recu depuis un moment.
        if (!s.d_->touched.empty() || s.d_->projectTouched || t - s.d_->lastLocalSend < 1.5 || t - s.d_->lastRemote < 0.5) return;
        const auto mine = s.d_->mirror.hashes(doc.project);
        std::vector<hmi::Id> views;
        std::vector<std::string> files;
        bool index = false;
        for (const auto& m : h.at("empreintes").members()) {
            const auto it = mine.find(m.first);
            if (it != mine.end() && it->second == m.second.asString()) continue;
            if (s.d_->resyncAsked[m.first] >= 3) continue;   // il ne se rattrape pas : on ne boucle pas
            if (m.first.rfind("v:", 0) == 0) {
                const auto id = std::strtoull(m.first.c_str() + 2, nullptr, 10);
                if (id > 0 && id <= 0xFFFFFFFFull) {
                    views.push_back(static_cast<hmi::Id>(id));
                    if (!doc.project.view(static_cast<hmi::Id>(id))) index = true;
                }
            } else if (m.first.rfind("f:", 0) == 0) {
                files.push_back(m.first.substr(2));
            }
            ++s.d_->resyncAsked[m.first];
        }
        for (const auto& [k, v] : mine) {
            (void)v;
            if (!h.at("empreintes").has(k) && k.rfind("v:", 0) == 0) index = true;   // une vue que l'hote n'a pas
            if (!h.at("empreintes").has(k) && k.rfind("f:", 0) == 0) files.push_back(k.substr(2));
        }
        if (index && std::find(files.begin(), files.end(), "ihm.txt") == files.end()) files.push_back("ihm.txt");
        if (views.empty() && files.empty()) return;
        core::json::Value r = op("RESYNC");
        core::json::Value vs = core::json::Value::array(), fsv = core::json::Value::array();
        for (const auto v : views) vs.push(static_cast<std::int64_t>(v));
        for (const auto& f : files) fsv.push(f);
        r.set("vues", std::move(vs));
        r.set("fichiers", std::move(fsv));
        s.d_->guest.send(r);
        s.log_.push_back("ecart : " + std::to_string(views.size()) + " vue(s), " + std::to_string(files.size()) + " fichier(s) redemandes");
    }

    static void sendState(SessionService& s) {
        auto doc = s.d_->doc.lock();
        if (!doc || s.d_->memberOfConn.empty()) return;
        const double t = now();
        if (t - s.d_->lastState < kStateEvery || doc->revision == s.d_->stateRevision) return;
        if (!s.d_->touched.empty() || s.d_->projectTouched) return;
        s.d_->lastState = t;
        s.d_->stateRevision = doc->revision;
        core::json::Value h = op("ETAT");
        core::json::Value e = core::json::Value::object();
        for (const auto& [k, v] : s.d_->mirror.hashes(doc->project)) e.set(k, v);
        h.set("empreintes", std::move(e));
        broadcast(s, h, {});
    }

    // L'equipier : la session finit pour lui (l'hote l'arrete ou le retire, le lien est perdu). 1.45.0 : il revient a
    // l'accueil, sa copie se ferme et s'efface (la prochaine image : leaveCopy) ; une notification le dit.
    static void ended(SessionService& s, const std::string& why, bool removed = false) {
        const bool wasGuest = s.mode_ == SessionService::Mode::Guest;
        s.d_->guest.stop();
        teardown(s);
        s.log_.push_back(std::string(removed ? "retiree : " : "fin : ") + why);
        tell(s, why, 0, removed ? "warning" : "info");
        if (wasGuest) {
            s.leaving_ = true;
            s.leaveWhy_ = why + ".\n\nTu reviens \xC3\xA0 l'accueil. Le projet reste chez " + (s.hostName_.empty() ? std::string("l'h\xC3\xB4te") : s.hostName_)
                        + " : la copie re\xC3\xA7ue pour la session est effac\xC3\xA9" "e de ce PC.";
        }
    }

    static void teardown(SessionService& s) {
        if (auto doc = s.d_->doc.lock()) {
            doc->shared = false;
            doc->mergeShell = {};
        }
        s.d_->docConn = core::Connection{};
        s.d_->doc.reset();
        s.d_->touched.clear();
        s.d_->projectTouched = false;
        s.d_->held.clear();
        s.d_->memberOfConn.clear();
        s.d_->connOfMember.clear();
        s.d_->knockQueue.clear();
        s.d_->projectOpen = false;
        s.d_->early.clear();
        s.d_->incoming.clear();
        s.d_->resyncAsked.clear();
        s.knocks_.clear();
        s.members_.clear();
        s.code_.clear();
        s.relayOpen_ = false;
        s.d_->printOf.clear();       // 1.45.0 (le journal reste lisible : session-journal, l'export)
        s.d_->journalDirty.clear();
        s.boardSeen_ = s.journalSeen_ = ~0ull;
        s.mode_ = SessionService::Mode::Off;
        hmi::edits::setCurrentSession({});
        sessionmarks::setActive(false);
        ++s.revision_;
    }
};

void SessionService::accept(int conn) { SessionServiceAccess::admit(*this, conn); }

void SessionService::refuse(int conn, const std::string& reason) {
    const auto it = std::find_if(knocks_.begin(), knocks_.end(), [&](const Knock& k) { return k.conn == conn; });
    if (it == knocks_.end()) return;
    log_.push_back("refus : " + it->name);
    chat_.push_back({"", it->name + " n'est pas entr\xC3\xA9" "e.", clock(), it->color, true});
    knocks_.erase(it);
    core::json::Value r = op("REFUS");
    r.set("raison", reason);
    d_->host.closeAfter(conn, r);
    ++revision_;
}

void SessionService::setRole(int number, session::Role role) {
    if (mode_ != Mode::Host || number == 0) return;
    for (auto& m : members_)
        if (m.number == number && m.role != role) {
            m.role = role;
            if (role == session::Role::Watch) SessionServiceAccess::hold(*this, number, m.view, {});
            chat_.push_back({"", m.name + (role == session::Role::Watch ? " regarde seulement." : " peut modifier."), clock(), m.color, true});
            SessionServiceAccess::sendMembers(*this);
            SessionServiceAccess::sendHolds(*this);
        }
}

// 1.45.0 : RETIRER UN EQUIPIER - il est coupe avec sa raison ; les autres le lisent (la discussion, une notification) ;
// son PC (son empreinte) est retenu : il ne revient que si l'hote le fait entrer (meme avec le code).
void SessionService::remove(int number) {
    if (mode_ != Mode::Host || number == 0) return;
    const auto cm = d_->connOfMember.find(number);
    const auto* m = member(number);
    if (cm == d_->connOfMember.end() || !m) return;
    const int conn = cm->second;
    const std::string who = m->name;
    const std::uint32_t col = m->color;
    if (const auto p = d_->printOf.find(number); p != d_->printOf.end() && !p->second.empty()) d_->removed.insert(p->second);
    else if (!m->machineId.empty()) d_->removed.insert(m->machineId);
    core::json::Value bye = op("AUREVOIR");
    bye.set("retire", true);
    bye.set("raison", hostName_ + " t'a retir\xC3\xA9 de la session");
    d_->host.closeAfter(conn, bye);
    // Il n'est plus membre tout de suite : ce qu'il enverrait encore ne compte plus (sa fermeture ne dit rien de plus).
    d_->memberOfConn.erase(conn);
    d_->connOfMember.erase(number);
    d_->printOf.erase(number);
    std::erase_if(members_, [&](const session::Member& x) { return x.number == number; });
    for (auto it = d_->held.begin(); it != d_->held.end();) it = it->second == number ? d_->held.erase(it) : std::next(it);
    log_.push_back("retrait : " + who + " (membre " + std::to_string(number) + ")");
    SessionServiceAccess::sendMembers(*this);
    SessionServiceAccess::sendHolds(*this);
    SessionServiceAccess::tellAll(*this, hostName_ + " a retir\xC3\xA9 " + who + " de la session", col, "warning");
}

void SessionService::stop(const std::string& reason) {
    if (mode_ == Mode::Host) {
        SessionServiceAccess::flush(*this, true);
        SessionServiceAccess::sendJournal(*this);
        core::json::Value bye = op("AUREVOIR");
        bye.set("fin", true);
        // 1.45.0 : << Camille a arrete la session >> - le bouton, le projet ou l'appli fermes.
        bye.set("raison", reason.empty() ? hostName_ + " a arr\xC3\xAAt\xC3\xA9 la session" : reason);
        for (const auto& [conn, m] : d_->memberOfConn) {
            (void)m;
            d_->host.closeAfter(conn, bye);
        }
        for (const auto& k : knocks_) d_->host.closeAfter(k.conn, bye);
        d_->host.stop();
        log_.push_back("session arretee");
        // 1.45.0 : le journal enregistre avec le projet (ihm/historique/sessions/).
        std::string saved, why;
        if (session::saveJournal(d_->hostFolder, name_, d_->startedAt, journal_.entries(), &saved, &why)) {
            savedJournal_ = saved;
            if (!saved.empty()) log_.push_back("journal : " + saved);
        } else {
            log_.push_back("journal non enregistre : " + why);
        }
        SessionServiceAccess::teardown(*this);
        app_.events().publish(StatusNotice{"La session est arr\xC3\xAAt\xC3\xA9" "e. Pense \xC3\xA0 enregistrer."
                                               + std::string(savedJournal_.empty() ? "" : " Son journal : " + std::string(session::journalFolder()) + "/"
                                                                                           + u8s(u8p(savedJournal_).filename())),
                                           8.0});
        return;
    }
    if (mode_ == Mode::Guest || mode_ == Mode::Joining) {
        const bool wasGuest = mode_ == Mode::Guest;
        if (wasGuest) SessionServiceAccess::flush(*this, true);
        core::json::Value bye = op("AUREVOIR");
        d_->guest.closeAfter(bye);
        d_->guest.stop();
        log_.push_back("session quittee");
        progress_.clear();
        SessionServiceAccess::teardown(*this);
        // 1.45.0 : l'equipier revient a l'accueil ; sa copie se ferme et s'efface (la prochaine image) - aussi une copie
        // recue qui ne s'est pas ouverte.
        if (wasGuest || !copySid_.empty()) {
            leaving_ = true;
            leaveWhy_.clear();
            if (wasGuest)
                app_.events().publish(StatusNotice{"Tu as quitt\xC3\xA9 la session \xC2\xAB " + name_ + " \xC2\xBB : sa copie s'efface de ce PC.", 6.0});
        }
    }
}

// 1.45.0 : la copie de l'equipier se ferme (l'accueil) et s'efface ; ce que la fenetre dit (si la session a fini sans lui).
void SessionService::leaveCopy() {
    leaving_ = false;
    const std::string folder = copyFolder_, sid = copySid_;
    copyFolder_.clear();
    copySid_.clear();
    if (!folder.empty()) app_.closeSessionCopy(folder);
    std::string why;
    if (!sid.empty() && !session::removeCopy(guestArea(), sid, &why)) {
        log_.push_back("copie non effacee : " + why);
    } else if (!folder.empty()) {
        lastCopy_ = folder;
        log_.push_back("copie effacee : " + folder);
    }
    const std::string text = std::move(leaveWhy_);
    leaveWhy_.clear();
    if (!text.empty() && !app_.scripted())
        app_.menus().ShowDialog(std::make_unique<MessageDialog>("La session est termin\xC3\xA9" "e", text, MessageDialog::Icon::Info),
                                [](const menu::DialogResult&) {});
    ++revision_;
}

void SessionService::flushNow() {
    if (active()) SessionServiceAccess::flush(*this, true);
}

std::string SessionService::exportJournal(std::string* why) {
    const auto fail = [&](std::string w) { if (why) *why = std::move(w); return std::string{}; };
    if (!active()) return fail("pas de session en cours");
    if (!host()) return fail("c'est " + hostName_ + " (l'h\xC3\xB4te) qui garde le journal : il l'exporte, et il l'enregistre avec le projet");
    if (journal_.entries().empty()) return fail("le journal est vide : aucun changement depuis le d\xC3\xA9" "but de la session");
    const std::string folder = d_->hostFolder.empty() ? app_.projectFolder() : d_->hostFolder;
    if (folder.empty()) return fail("le projet n'a pas de dossier");
    const fs::path dir = u8p(folder) / "exports";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path target = dir / u8p("journal-" + session::journalFileName(name_, d_->startedAt));
    const std::string text = session::journalCsv(journal_.entries());
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) return fail(u8s(target) + " ne s'\xC3\xA9" "crit pas");
    log_.push_back("journal exporte : " + u8s(target));
    return u8s(target);
}

// 1.45.0 : ce que l'interface montre (SessionMarks::Board) - quand quelque chose a change.
void SessionService::publishBoard() {
    if (!active()) return;
    if (revision_ == boardSeen_ && journal_.revision() == journalSeen_) return;
    boardSeen_ = revision_;
    journalSeen_ = journal_.revision();
    sessionmarks::Board b;
    b.sessionName = name_;
    b.hostName = hostName_;
    b.host = host();
    b.internet = internet_;
    for (const auto& m : members_) {
        sessionmarks::Person p;
        p.number = m.number;
        p.name = m.name;
        p.initials = session::initials(m.name);
        p.machine = m.machine;
        p.machineId = m.machineId;
        p.view = m.view;
        p.viewName = viewName(m.view);
        p.color = m.color;
        p.host = m.number == 0;
        p.me = m.number == me_;
        p.watch = m.role == session::Role::Watch;
        b.people.push_back(std::move(p));
    }
    const auto* mine = member(me_);
    for (const auto& l : chat_) {
        sessionmarks::Line x;
        x.time = l.time;
        x.from = l.from;
        x.text = l.text;
        x.color = l.color;
        x.system = l.system;
        x.mine = !l.system && mine && l.from == mine->name;
        x.id = l.id;   // 1.50.0 : repondre a un message
        x.replyId = l.reply.id;
        x.replyFrom = l.reply.from;
        x.replyText = l.reply.excerpt;
        b.chat.push_back(std::move(x));
    }
    for (const auto& e : journal_.entries()) {
        sessionmarks::Line x;
        x.time = session::clockText(e.t);
        x.from = e.who;
        x.text = e.text;
        x.color = e.color;
        x.count = e.count;
        x.mine = e.member == me_;
        b.journal.push_back(std::move(x));
    }
    b.messages = messagesIn_;
    sessionmarks::setBoard(std::move(b));
}

// ============================================================================================ l'equipier ===
void SessionService::search(const std::vector<std::string>& direct, int discoveryPort) {
    if (searching()) return;
    found_.clear();
    searchWhy_.clear();
    d_->searching = std::async(std::launch::async, [direct, discoveryPort, this]() {
        session::SearchOptions o;
        o.port = discoveryPort;
        o.direct = direct;
        o.direct.push_back("127.0.0.1");   // une session sur ce PC (un essai, une autre fenetre)
        std::string why;
        auto out = session::search(o, &why);
        d_->searchWhy = why;
        return out;
    });
}

bool SessionService::searching() const noexcept { return d_->searching.valid(); }

bool SessionService::join(const JoinTarget& t, const std::string& code, std::string* why) {
    if (active() || mode_ == Mode::Joining) {
        if (why) *why = "tu es d\xC3\xA9j\xC3\xA0 dans une session";
        return false;
    }
    if (!licence::gateProjects(app_)) {   // rejoindre demande sa propre licence d'IHM Studio
        if (why) *why = "ta licence ne permet pas d'ouvrir un projet";
        return false;
    }
    if (!loadIdentity(*d_, why)) return false;
    core::json::Value hello = core::json::Value::object();
    hello.set("nom", myName());
    hello.set("pc", hmi::netinfo::computerName());
    hello.set("id_pc", d_->identity.machineId());
    hello.set("logiciel", "IHM Studio");
    hello.set("version", XPG_ANALYZER_VERSION);
    hello.set("couleur", static_cast<std::int64_t>(session::palette()[1]));
    if (t.internet) {
        // 1.44.0 : par Internet - le code seul (10 caracteres) : le relais trouve la session, l'echange le prouve.
        std::string norm;
        if (!session::relayCodeTyped(code, norm)) {
            if (why) *why = "ce n'est pas le code d'une session par Internet (10 caract\xC3\xA8res, comme K7Q4-M2XP-T9)";
            return false;
        }
        auto transport = relayTransport(why);
        if (!transport) return false;
        session::RelayGuestConfig g;
        g.transport = std::move(transport);
        g.code = norm;
        g.hello = std::move(hello);
        d_->guest.internet = true;
        if (!d_->guest.relay.start(g, why)) return false;
    } else {
        session::GuestConfig g;
        g.address = t.address;
        g.port = t.port;
        g.fingerprint = t.fingerprint;
        g.sessionId = t.sessionId;
        g.code = code;
        g.hello = std::move(hello);
        d_->guest.internet = false;
        if (!d_->guest.lan.start(g, d_->identity, why)) return false;
    }
    internet_ = t.internet;
    relayOpen_ = false;
    d_->target = t;
    if (t.internet && d_->target.machine.empty()) d_->target.machine = "Internet";
    mode_ = Mode::Joining;
    name_ = t.sessionName;
    hostName_ = t.hostName;
    sid_ = t.sessionId;
    chat_.clear();
    log_.clear();
    journal_.clear();       // 1.45.0 : celui de l'hote arrive a l'entree
    savedJournal_.clear();
    messagesIn_ = 0;
    resyncs_ = refusals_ = 0;
    progress_ = t.internet ? std::string("Recherche de la session sur le service XPG\xE2\x80\xA6")
                           : "Connexion \xC3\xA0 " + (t.machine.empty() ? t.address : t.machine) + "\xE2\x80\xA6";
    ++revision_;
    return true;
}

// ================================================================================================== tick ===
void SessionService::tick() {
    // La recherche finie.
    if (d_->searching.valid() && d_->searching.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        found_ = d_->searching.get();
        searchWhy_ = d_->searchWhy;
        ++revision_;
        if (onSearchDone) {
            auto fn = std::move(onSearchDone);
            onSearchDone = {};
            fn();
        }
    }
    // 1.45.0 : l'equipier est parti - sa copie se ferme (l'accueil) et s'efface.
    if (leaving_ && mode_ == Mode::Off) leaveCopy();
    if (mode_ == Mode::Off) return;
    // 1.45.0 : a la fin de l'image, ce que l'interface montre (les membres, la discussion, le journal).
    struct Publish {
        SessionService& s;
        ~Publish() { s.publishBoard(); }
    } publish{*this};
    // Le projet a change sous la session (ferme, un autre ouvert) : elle s'arrete.
    if (active()) {
        auto doc = d_->doc.lock();
        if (!doc || app_.hmi() != doc) {
            stop();   // 1.45.0 : << Camille a arrete la session >> (l'equipier : il part, sa copie s'efface)
            return;
        }
    }
    if (mode_ == Mode::Host) {
        for (auto& e : d_->host.poll()) {
            switch (e.kind) {
                case session::LinkEvent::Kind::Knock: {
                    Knock k;
                    k.conn = e.conn;
                    k.name = e.header.str("nom").substr(0, 64);
                    k.machine = e.header.str("pc").substr(0, 64);
                    k.machineId = e.header.str("id_pc").substr(0, 16);
                    k.app = e.header.str("logiciel").substr(0, 32);
                    k.version = e.header.str("version").substr(0, 16);
                    k.address = e.address;
                    k.codeOk = e.codeOk;
                    k.color = static_cast<std::uint32_t>(e.header.integer("couleur"));
                    // 1.45.0 : un PC retire tout a l'heure ne rentre pas seul (meme avec le code) : l'hote decide.
                    k.fingerprint = e.fingerprint;
                    k.removed = d_->removed.count(!k.fingerprint.empty() ? k.fingerprint : k.machineId) > 0;
                    knocks_.push_back(k);
                    core::json::Value w = op("ATTENTE");
                    d_->host.send(e.conn, w);
                    log_.push_back("frappe : " + k.name + (k.codeOk ? " (code bon)" : "") + (k.removed ? " (retiree)" : ""));
                    if (entry_ == session::Entry::Code && k.codeOk && !k.removed) {
                        accept(e.conn);
                    } else {
                        d_->knockQueue.push_back(e.conn);
                        chat_.push_back({"", k.removed ? k.name + " (retir\xC3\xA9 tout \xC3\xA0 l'heure) veut revenir : \xC3\xA0 toi de d\xC3\xA9" "cider."
                                                       : k.name + " veut entrer (" + k.machine + ").",
                                         clock(), 0, true});
                        app_.events().publish(StatusNotice{k.name + (k.removed ? " veut revenir dans la session (retir\xC3\xA9 tout \xC3\xA0 l'heure)."
                                                                               : " veut entrer dans la session."),
                                                           8.0});
                    }
                    ++revision_;
                    break;
                }
                case session::LinkEvent::Kind::Frame:
                    SessionServiceAccess::hostFrame(*this, e.conn, e.header, e.data);
                    break;
                case session::LinkEvent::Kind::Closed:
                    if (internet_ && e.conn == 0) {   // 1.44.0 : la session elle-meme (le relais)
                        SessionServiceAccess::relayLost(*this, e.why);
                        break;
                    }
                    SessionServiceAccess::hostGone(*this, e.conn, e.why == "l'autre PC a ferm\xC3\xA9 la connexion" ? std::string{} : e.why);
                    break;
                case session::LinkEvent::Kind::Opened:   // 1.44.0 : ouverte sur le relais
                    relayOpen_ = true;
                    log_.push_back("ouverte sur le service XPG");
                    chat_.push_back({"", "La session est ouverte sur le service XPG : tes \xC3\xA9quipiers peuvent entrer avec le code " + codeText() + ".", clock(), 0, true});
                    ++revision_;
                    break;
                default: break;
            }
            if (mode_ != Mode::Host) return;
        }
        SessionServiceAccess::askKnock(*this);
        SessionServiceAccess::presence(*this);
        SessionServiceAccess::flush(*this, false);
        SessionServiceAccess::sendJournal(*this);   // 1.45.0
        SessionServiceAccess::sendState(*this);
        return;
    }
    // L'equipier.
    for (auto& e : d_->guest.poll()) {
        switch (e.kind) {
            case session::LinkEvent::Kind::Opened:   // 1.44.0 : par Internet, le service connait la session
                if (!e.header.str("nom").empty()) name_ = d_->target.sessionName = e.header.str("nom").substr(0, 200);
                if (!e.header.str("hote").empty()) hostName_ = d_->target.hostName = e.header.str("hote").substr(0, 100);
                relayOpen_ = true;
                progress_ = "Session \xC2\xAB " + name_ + " \xC2\xBB trouv\xC3\xA9" "e" + (hostName_.empty() ? std::string{} : " (h\xC3\xB4te : " + hostName_ + ")")
                          + ". V\xC3\xA9rification du code\xE2\x80\xA6";
                log_.push_back("trouvee sur le service XPG : " + name_);
                ++revision_;
                break;
            case session::LinkEvent::Kind::Connected:
                progress_ = internet_ ? std::string("Connect\xC3\xA9" "e par Internet \xC2\xB7 chiffr\xC3\xA9 de bout en bout")
                                      : "Connect\xC3\xA9" "e \xC3\xA0 " + (d_->target.machine.empty() ? d_->target.address : d_->target.machine) + " \xC2\xB7 chiffr\xC3\xA9";
                log_.push_back("connectee" + std::string(e.codeOk ? " (code bon)" : ""));
                ++revision_;
                break;
            case session::LinkEvent::Kind::Frame: {
                const std::string kind = e.header.str("op");
                if (!d_->projectOpen && (kind == "VUE" || kind == "FICHIERS" || kind == "ETAT")) {
                    d_->early.push_back(std::move(e));   // apres l'ouverture du projet
                    break;
                }
                SessionServiceAccess::guestFrame(*this, e.header, e.data);
                break;
            }
            case session::LinkEvent::Kind::Closed:
                if (mode_ == Mode::Guest) SessionServiceAccess::ended(*this, hostName_ + " n'est plus joignable (" + e.why + ")");
                else if (mode_ == Mode::Joining) {
                    log_.push_back("echec : " + e.why);
                    d_->guest.stop();
                    SessionServiceAccess::teardown(*this);
                    // 1.45.0 : un service des licences en 1.43 (pas de relais) : la phrase seule, qui dit quoi faire.
                    progress_ = session::relayTooOld(e.why) ? e.why : "Pas entr\xC3\xA9" "e : " + e.why;
                    app_.events().publish(StatusNotice{session::relayTooOld(e.why) ? e.why + "." : "Session : " + e.why, 8.0});
                }
                break;
            default: break;
        }
        if (mode_ == Mode::Off) return;
    }
    if (mode_ == Mode::Guest) {
        SessionServiceAccess::presence(*this);
        SessionServiceAccess::flush(*this, false);
    }
}

void SessionService::say(const std::string& text, const std::string& replyTo) {
    if (text.empty() || !active()) return;
    const auto* me = member(me_);
    // 1.50.0 : la reponse a un message de la discussion (inconnu ici : un message simple).
    const session::ChatReply reply = SessionServiceAccess::replyOf(*this, replyTo);
    if (mode_ == Mode::Host) {
        const auto msg = SessionServiceAccess::hostMessage(*this, hostName_, me ? me->color : 0, text.substr(0, session::kMessageMax), reply);
        SessionServiceAccess::broadcastChat(*this, msg);
        SessionServiceAccess::keep(*this, msg);
    } else {
        // Un hote d'avant la 1.50 ne garderait pas `reponse` : la citation part alors dans le texte, pour tous.
        const auto* host = member(0);
        d_->guest.send(session::sayFrame(text, reply, host ? std::string_view(host->version) : std::string_view{}));   // il revient par l'hote
    }
    ++revision_;
}

void SessionService::follow(int number) {
    const auto* m = member(number);
    if (!m || m->view == hmi::kNoId) return;
    app_.events().publish(OpenHmiView{m->view});
}

} // namespace app
