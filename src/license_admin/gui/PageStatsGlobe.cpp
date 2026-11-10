// =============================================================================
//  license_admin/gui/PageStatsGlobe.cpp - 1.47.0 : LE GLOBE DE STATISTIQUES ET
//  LA PETITE CARTE DU TABLEAU DE BORD (voir GlobeStats.hpp)
// -----------------------------------------------------------------------------
//  Les places reservees par StatsKit.hpp (statskit::makeStatsGlobeView,
//  statskit::makeDashboardGlobe) et la sous-page << globe >> (makeGlobePage).
// =============================================================================
#include "GlobeStats.hpp"

#include "../../ui/Icons.hpp"
#include "../../ui/widgets/FilterBar.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <mutex>
#include <utility>

namespace license_admin::gui {

namespace globestats {

namespace {

using gfx::Color;
using gfx::FontId;
using gfx::Rect;
namespace gl = ui::globe;
using namespace globekit;

constexpr double kStaleCount = 300.0;   // Compte : le globe relu toutes les 5 minutes (la vue montree)
constexpr double kStaleLive = 60.0;     // En direct : toutes les minutes (les PC actifs de chaque ville)
constexpr double kLivePeriod = 6.0;     // En direct : GET /admin/globe/direct toutes les 6 s
constexpr double kRetry = 5.0;          // un travail tournait : on reessaie

std::mutex& hookMutex() {
    static std::mutex m;
    return m;
}
Fetchers& hookStore() {
    static Fetchers f;
    return f;
}
Fetchers hooks() {
    std::lock_guard<std::mutex> lock(hookMutex());
    return hookStore();
}

std::tm localTm(Seconds t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    return tm;
}

// << mar. 10 nov. 2026 a 10:42 >> (l'heure de ce PC).
std::string whenText(Seconds t) {
    static const char* days[] = {"dim.", "lun.", "mar.", "mer.", "jeu.", "ven.", "sam."};
    static const char* months[] = {"janv.", "f\xC3\xA9vr.", "mars", "avr.", "mai", "juin", "juil.", "ao\xC3\xBBt", "sept.", "oct.", "nov.", "d\xC3\xA9" "c."};
    const std::tm tm = localTm(t);
    char b[64];
    std::snprintf(b, sizeof b, " %d ", tm.tm_mday);
    char hm[16];
    std::snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return std::string(days[tm.tm_wday % 7]) + b + months[tm.tm_mon % 12] + " " + std::to_string(tm.tm_year + 1900) + " \xC3\xA0 " + hm;
}

std::string clockText(Seconds t) {
    const std::tm tm = localTm(t);
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

std::string periodLabel(int days) {
    if (days >= 365) return "12 mois";
    return std::to_string(days) + " j";
}
std::string periodKey(int days) {
    if (days >= 365) return "12mois";
    return std::to_string(days) + "j";
}

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// La premiere barre de filtres sous un widget (la liste d'une page).
ui::FilterBar* findBar(ui::Widget& w) {
    if (auto* b = dynamic_cast<ui::FilterBar*>(&w)) return b;
    for (const auto& c : w.children())
        if (auto* b = findBar(*c)) return b;
    return nullptr;
}

// Ouvrir une liste (<< clients >>, << activations >>) filtree : la colonne `column` (son titre) ne garde que `values`.
// Rend vrai si la barre de la liste a pris le filtre.
bool openFiltered(const ShellHost& h, ui::Widget& from, const std::string& page, const std::string& column, const std::vector<std::string>& values) {
    if (h.navigate) h.navigate(page);
    ui::Widget& root = from.topWidget();
    ui::Widget* p = root.findById("page." + page);
    ui::FilterBar* bar = p ? findBar(*p) : nullptr;
    if (!bar) return false;
    const int field = bar->fieldByTitle(column);
    if (field < 0) return false;
    bar->clearAll();
    bar->setValues(static_cast<std::size_t>(field), values);
    return true;
}

template <class T>
T* add(ui::Widget& parent, std::unique_ptr<T> w) {
    T* p = w.get();
    parent.addChild(std::move(w));
    return p;
}

// L'appel du globe : les essais, les villes d'essai (XPG_STATS_ESSAI=1), sinon le service (par StatsKit : ses reponses
// d'essai a lui s'il en a).
OpResult fetchGlobeNow(const Workspace& w, int days, stats::GlobeReply& out) {
    if (const Fetchers f = hooks(); f.globe) return f.globe(days, out);
    if (sampleMode()) {
        out = sampleReply(days, w.now());
        return OpResult::done({});
    }
    return statskit::fetchGlobe(w, days, out);
}

} // namespace

void setFetchersForTests(Fetchers f) {
    std::lock_guard<std::mutex> lock(hookMutex());
    hookStore() = std::move(f);
}

bool sampleMode() {
    const char* v = std::getenv("XPG_STATS_ESSAI");
    return v && std::string(v) == "1";
}

gl::Place placeOf(const stats::GlobeCity& c) {
    gl::Place p;
    p.id = c.id;
    p.city = c.city;
    p.country = c.country;
    p.region = c.region;
    p.lat = c.lat;
    p.lon = c.lon;
    p.pcs = c.pcs;
    p.setCount(gl::Layer::Clients, c.clients);
    p.setCount(gl::Layer::ActivePcs, c.active);
    p.setCount(gl::Layer::Outdated, c.outdated);
    p.setCount(gl::Layer::Admins, c.admins);
    p.setCount(gl::Layer::Discoveries, c.discoveries);
    p.setCount(gl::Layer::Orders, c.orders);
    p.setCount(gl::Layer::Requests, c.requests);
    return p;
}

std::vector<gl::Place> placesOf(const stats::GlobeReply& r) {
    std::vector<gl::Place> out;
    out.reserve(r.cities.size());
    for (const auto& c : r.cities) out.push_back(placeOf(c));
    return out;
}

std::string labelOfId(const std::string& id) {
    const auto bar = id.find('|');
    if (bar == std::string::npos) return id;
    return id.substr(bar + 1) + ", " + id.substr(0, bar);
}

stats::GlobeReply sampleReply(int periodDays, Seconds now) {
    stats::GlobeReply r;
    r.at = now;
    r.periodDays = periodDays;
    r.latestVersion = "1.47.0";
    const double k = periodDays <= 7 ? 0.3 : periodDays <= 30 ? 1.0 : periodDays <= 90 ? 2.6 : 9.0;   // globe.js, PERIODE
    std::set<std::string> countries;
    for (const auto& p : gl::samplePlaces()) {
        stats::GlobeCity c;
        c.id = p.id;
        c.city = p.city;
        c.region = p.region;
        c.country = p.country;
        c.lat = p.lat;
        c.lon = p.lon;
        c.pcs = p.pcs;
        c.clients = p.count(gl::Layer::Clients);
        c.active = p.count(gl::Layer::ActivePcs);
        c.outdated = p.count(gl::Layer::Outdated);
        c.admins = p.count(gl::Layer::Admins);
        c.discoveries = p.count(gl::Layer::Discoveries);
        c.orders = static_cast<int>(std::lround(p.count(gl::Layer::Orders) * k));
        c.requests = p.count(gl::Layer::Requests);
        // des noms d'essai : << Atelier Exemple >>, << Societe d'essai >>... (jamais un vrai client)
        static const char* names[] = {"Atelier Exemple", "Soci\xC3\xA9t\xC3\xA9 d'essai", "Bureau Exemple", "Usine d'essai", "Cabinet Exemple",
                                      "Studio d'essai", "Garage Exemple", "Labo d'essai", "Ferme Exemple"};
        for (int i = 0; i < c.clients; ++i) c.customers.push_back(std::string(names[i % 9]) + " " + p.city + (i >= 9 ? " " + std::to_string(i / 9 + 1) : ""));
        r.totals.clients += c.clients;
        r.totals.pcs += c.pcs;
        r.totals.active += c.active;
        r.totals.outdated += c.outdated;
        r.totals.admins += c.admins;
        r.totals.discoveries += c.discoveries;
        r.totals.orders += c.orders;
        r.totals.requests += c.requests;
        countries.insert(c.country);
        r.cities.push_back(std::move(c));
    }
    r.cityCount = static_cast<int>(r.cities.size());
    r.countries = static_cast<int>(countries.size());
    r.unplacedPcs = 3;
    return r;
}

// ============================================================================ la vue
GlobeStatsView::GlobeStatsView(ShellHost host, bool title) : ui::Widget(title ? "globe.page-vue" : "globe.vue"), host_(std::move(host)), title_(title) {
    board_ = add(*this, std::make_unique<GlobeBoard>("globe"));
    period_ = add(*this, std::make_unique<statskit::SegGroup>("globe.periode", std::vector<statskit::SegGroup::Item>{
                                                                                   {"7j", "7 j"}, {"30j", "30 j"}, {"90j", "90 j"}, {"12mois", "12 mois"}}));
    deploy_ = add(*this, std::make_unique<ui::Button>("D\xC3\xA9ployer le service", "globe.deployer"));
    settings_ = add(*this, std::make_unique<ui::Button>("R\xC3\xA9gler le service", "globe.regler"));
    retry_ = add(*this, std::make_unique<ui::Button>("R\xC3\xA9" "essayer", "globe.reessayer"));
    deploy_->setStyle(ui::Button::Style::Primary);
    deploy_->setIcon(ui::Icon::Export);
    settings_->setIcon(ui::Icon::Settings);
    retry_->setIcon(ui::Icon::Refresh);
    period_->setCurrent("30j");
    board_->setHeader("D'o\xC3\xB9 se connectent vos clients", "les points sont \xC3\xA0 la ville, jamais \xC3\xA0 l'adresse");
    board_->setCardActions(true);
    board_->setLiveLabel("En direct \xC2\xB7 relev\xC3\xA9 toutes les 6 s");
    board_->setStatus("Relev\xC3\xA9 en cours\xE2\x80\xA6");
    board_->setOrdersPeriod(periodLabel(days_));
    period_->onPick = [this](const std::string& key) {
        setPeriodDays(key == "7j" ? 7 : key == "90j" ? 90 : key == "12mois" ? 365 : 30);
    };
    board_->onMode = [this](bool live) {
        if (live) {
            liveSince_ = hasReply_ ? reply_.at : 0;
            nextLive_ = 0.0;   // tout de suite
            board_->setTicker({}, {}, {});
        }
        wantFetch_ = live;   // En direct : le globe relu (les PC actifs de chaque ville)
    };
    board_->onFullScreen = [this](bool) {
        invalidateLayout();
        if (parent()) parent()->invalidateLayout();   // la sous-page : ses marges
    };
    board_->onClients = [this](int place) { (void)openClients(place); };
    board_->onPcs = [this](int place) { (void)openPcs(place); };
    links_ += deploy_->clicked->connect([this] {
        if (host_.openDialog) host_.openDialog(makeDeployDialog(host_, [this, alive = alive_] {
            if (*alive) reload();
        }));
    });
    links_ += settings_->clicked->connect([this] {
        if (host_.navigate) host_.navigate("service");
    });
    links_ += retry_->clicked->connect([this] { reload(); });
    syncState();
}

GlobeStatsView::~GlobeStatsView() {
    links_.clear();
    *alive_ = false;
}

ui::SizeHint GlobeStatsView::sizeHint() const { return ui::SizeHint{{1000.f, 700.f}, {600.f, 420.f}, 1.f, 1.f}; }

void GlobeStatsView::reload() {
    problem_.clear();
    tooOld_ = false;
    wantFetch_ = true;
    retryAt_ = 0.0;
    syncState();
    if (now_ >= 0.0) tick(now_);
}

void GlobeStatsView::setPeriodDays(int days) {
    const int d = days >= 365 ? 365 : days <= 7 ? 7 : days <= 30 ? 30 : 90;
    if (d == days_) return;
    days_ = d;
    period_->setCurrent(periodKey(d));
    board_->setOrdersPeriod(periodLabel(d));
    wantFetch_ = true;
    retryAt_ = 0.0;
    if (now_ >= 0.0) tick(now_);
}

// Le temps qui passe (la peinture : la vue est montree). Le globe : a l'arrivee, a la demande, quand il est vieux ; le
// direct : toutes les 6 s.
void GlobeStatsView::tick(double now) {
    now_ = now;
    const double stale = board_->live() ? kStaleLive : kStaleCount;
    if (!fetching_ && now >= retryAt_ && (wantFetch_ || now - lastFetch_ >= stale)) fetch();
    if (board_->live() && hasReply_ && !liveFetching_ && now >= nextLive_ && problem_.empty()) pollLive();
}

void GlobeStatsView::fetch() {
    Workspace& w = host_.workspace();
    // Les essais (les reponses d'essai) et les captures (XPG_STATS_ESSAI) sautent les controles du service, sauf la
    // version du service lue : le bandeau << deploie la 1.47.0 >> reste.
    std::string why;
    bool old = false;
    if (hooks().globe || sampleMode()) {
        why = w.statsServiceNote();
        old = !why.empty();
    } else {
        why = statskit::blocked(w, &old);
    }
    if (!why.empty()) {
        problem_ = why;
        tooOld_ = old;
        wantFetch_ = false;
        lastFetch_ = now_;
        syncState();
        return;
    }
    const Workspace* wp = &w;
    const int days = days_;
    auto out = std::make_shared<stats::GlobeReply>();
    fetching_ = true;
    syncState();
    const bool started = statskit::runQuietly(
        host_, "Globe : relev\xC3\xA9", [wp, days, out] { return fetchGlobeNow(*wp, days, *out); },
        [this, alive = alive_, out, days](const OpResult& r) {
            if (!*alive) return;
            fetching_ = false;
            lastFetch_ = now_;
            if (days != days_) {   // la periode a change pendant le releve : on relit
                wantFetch_ = true;
                syncState();
                return;
            }
            if (r.ok) {
                problem_.clear();
                tooOld_ = false;
                adopt(*out);
            } else {
                problem_ = r.message.empty() ? std::string("Le service n'a pas rendu le globe.") : r.message;
                tooOld_ = statskit::saysTooOld(problem_);
            }
            syncState();
        });
    if (!started) {   // un travail tourne (un releve, une operation) : on reessaie dans un moment
        fetching_ = false;
        retryAt_ = now_ + kRetry;
        syncState();
        return;
    }
    wantFetch_ = false;
}

void GlobeStatsView::pollLive() {
    const Workspace* wp = &host_.workspace();
    const Seconds since = liveSince_;
    auto out = std::make_shared<stats::LiveReply>();
    liveFetching_ = true;
    nextLive_ = now_ + kLivePeriod;
    ++livePolls_;
    const bool sample = !hooks().live && sampleMode();
    const int n = samplePing_++;
    const bool started = statskit::runQuietly(
        host_, "Globe : en direct",
        [wp, since, out, sample, n] {
            if (const Fetchers f = hooks(); f.live) return f.live(since, *out);
            if (sample) {   // une connexion d'essai (les villes de la maquette, a tour de role)
                const auto places = gl::samplePlaces();
                out->at = wp->now();
                out->active = 42;
                const auto& p = places[static_cast<std::size_t>(n) % places.size()];
                out->pings.push_back({wp->now() - 2, p.id, n % 5 == 4 ? "admin" : "studio"});
                return OpResult::done({});
            }
            return wp->statsLive(since, *out);
        },
        [this, alive = alive_, out](const OpResult& r) {
            if (!*alive) return;
            liveFetching_ = false;
            if (r.ok) adoptLive(*out);
        });
    if (!started) {
        liveFetching_ = false;
        nextLive_ = now_ + 1.0;
    }
}

void GlobeStatsView::adopt(const stats::GlobeReply& r) {
    reply_ = r;
    hasReply_ = true;
    auto places = placesOf(r);
    // Les admins d'une ville : leur nom (la liste des admins) ; l'editeur : << l'admin+ >>.
    std::vector<std::string> labels;
    const auto& list = host_.workspace().adminList();
    for (const auto& c : r.cities) {
        std::string first;
        for (const auto& id : c.adminIds) {
            const auto* a = list ? list->byKeyId(id) : nullptr;
            first = a && !a->name.empty() ? a->name : std::string("l'admin+");
            break;
        }
        if (c.adminIds.size() > 1) first += " +" + std::to_string(c.adminIds.size() - 1);
        labels.push_back(first);
    }
    board_->setPlaces(std::move(places), std::move(labels));
    std::array<int, gl::kLayerCount> totals{};
    totals[gl::index(gl::Layer::Clients)] = r.totals.clients;
    totals[gl::index(gl::Layer::ActivePcs)] = r.totals.active;
    totals[gl::index(gl::Layer::Outdated)] = r.totals.outdated;
    totals[gl::index(gl::Layer::Admins)] = r.totals.admins;
    totals[gl::index(gl::Layer::Discoveries)] = r.totals.discoveries;
    totals[gl::index(gl::Layer::Orders)] = r.totals.orders;
    totals[gl::index(gl::Layer::Requests)] = r.totals.requests;
    board_->setTotals(totals);
    if (!r.latestVersion.empty()) board_->setHint(gl::Layer::Outdated, "un PC pas en " + r.latestVersion);
    board_->setActiveTotal(r.totals.active);
    if (r.at > 0) board_->setHeader("D'o\xC3\xB9 se connectent vos clients", "Relev\xC3\xA9 le " + whenText(r.at) + " \xC2\xB7 les points sont \xC3\xA0 la ville, jamais \xC3\xA0 l'adresse");
    if (liveSince_ == 0) liveSince_ = r.at;
}

void GlobeStatsView::adoptLive(const stats::LiveReply& r) {
    board_->setActiveTotal(r.active);
    if (r.at > 0) liveSince_ = r.at;
    std::vector<stats::LivePing> pings = r.pings;
    std::stable_sort(pings.begin(), pings.end(), [](const stats::LivePing& a, const stats::LivePing& b) { return a.at < b.at; });
    for (const auto& p : pings) {
        const std::string key = std::to_string(p.at) + "|" + p.city + "|" + p.kind;
        if (!seenPings_.insert(key).second) continue;   // deja vue (les bornes de deux releves)
        const int i = board_->globe().findPlace(p.city);
        if (i >= 0) board_->globe().pulse(i);
        const std::string label = i >= 0 ? board_->places()[static_cast<std::size_t>(i)].label() : labelOfId(p.city);
        board_->setTicker(label, p.kind == "admin" ? "un Admin connect\xC3\xA9" : "un Studio connect\xC3\xA9", clockText(p.at));
    }
    if (seenPings_.size() > 2000) seenPings_.clear();
}

// La carte d'une ville : << Voir les N clients >> (Clients, la colonne Client).
std::string GlobeStatsView::openClients(int place) {
    if (place < 0 || place >= static_cast<int>(reply_.cities.size())) return {};
    const auto& c = reply_.cities[static_cast<std::size_t>(place)];
    const std::string label = labelOfId(c.id);
    std::string said;
    if (c.customers.empty()) {
        said = "Clients : le service ne nomme pas les clients de " + label + ".";
        if (host_.navigate) host_.navigate("clients");
    } else {
        const bool ok = openFiltered(host_, *this, "clients", "Client", c.customers);
        said = "Clients : les " + std::to_string(c.customers.size()) + " client" + (c.customers.size() > 1 ? "s" : "") + " de " + label;
        if (static_cast<int>(c.customers.size()) < c.clients) said += " (les " + std::to_string(c.customers.size()) + " premiers)";
        said += ok ? "." : " (la liste n'a pas pris le filtre).";
    }
    if (host_.flash) host_.flash(said, Tone::Info);
    return said;
}

// << Les N PC >> (Activations) : leurs empreintes, d'apres les cles que le service donne ; une cle que l'archive de ce
// poste ne connait pas : les PC des clients de la ville.
std::string GlobeStatsView::openPcs(int place) {
    if (place < 0 || place >= static_cast<int>(reply_.cities.size())) return {};
    const auto& c = reply_.cities[static_cast<std::size_t>(place)];
    const std::string label = labelOfId(c.id);
    std::set<std::string> keys;
    for (const auto& k : c.devices) keys.insert(lowerAscii(k));
    std::vector<std::string> prints;
    for (const auto& d : host_.workspace().devices())
        if (!d.deviceKey.empty() && keys.count(lowerAscii(d.deviceKey)) && !d.fingerprint.empty() &&
            std::find(prints.begin(), prints.end(), d.fingerprint) == prints.end())
            prints.push_back(d.fingerprint);
    std::string said;
    if (!prints.empty()) {
        const bool ok = openFiltered(host_, *this, "activations", "Empreinte", prints);
        said = "Activations : les " + std::to_string(prints.size()) + " PC de " + label;
        if (static_cast<int>(prints.size()) < c.pcs) said += " que l'archive de ce poste conna\xC3\xAEt (le service en compte " + std::to_string(c.pcs) + ")";
        said += ok ? "." : " (la liste n'a pas pris le filtre).";
    } else if (!c.customers.empty()) {
        const bool ok = openFiltered(host_, *this, "activations", "Client", c.customers);
        said = "Activations : les PC des clients de " + label + " (l'archive de ce poste ne conna\xC3\xAEt pas les PC que le service y place)";
        said += ok ? "." : " (la liste n'a pas pris le filtre).";
    } else {
        said = "Activations : l'archive de ce poste ne conna\xC3\xAEt pas les PC de " + label + ".";
        if (host_.navigate) host_.navigate("activations");
    }
    if (host_.flash) host_.flash(said, Tone::Info);
    return said;
}

// L'etat montre : le bandeau (le service a deployer, pas regle, un refus), l'etat du globe, le message au milieu.
void GlobeStatsView::syncState() {
    const bool notSet = !problem_.empty() && !tooOld_ &&
                        (problem_.find("pas r\xC3\xA9gl\xC3\xA9") != std::string::npos || problem_.find("illisible") != std::string::npos ||
                         problem_.find("cl\xC3\xA9 qui signe") != std::string::npos);
    deploy_->setVisibility(tooOld_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    settings_->setVisibility(notSet ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    retry_->setVisibility(!problem_.empty() && !tooOld_ && !notSet ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    board_->setStatus(fetching_ && !hasReply_ ? std::string("Relev\xC3\xA9 en cours\xE2\x80\xA6") : std::string());
    if (tooOld_)
        board_->setEmptyMessage("Le globe vient du service 1.47.0",
                                "D\xC3\xA9ploie la nouvelle version du service (le bouton en haut) : il note la ville de chaque PC \xC3\xA0 sa derni\xC3\xA8re connexion.");
    else if (!problem_.empty() && !hasReply_)
        board_->setEmptyMessage("Le globe n'est pas relev\xC3\xA9", problem_);
    else if (hasReply_ && reply_.cities.empty())
        board_->setEmptyMessage("Aucune ville pour l'instant",
                                "Les points viennent des appels au service des Studios et des Admins en 1.47.0 : la ville de leur derni\xC3\xA8re connexion." +
                                    (reply_.unplacedPcs > 0 ? " " + std::to_string(reply_.unplacedPcs) + " PC n'ont pas encore de lieu." : std::string()));
    else
        board_->setEmptyMessage({}, {});
    invalidateLayout();
    invalidate();
}

std::string GlobeStatsView::topLine() const {
    std::string s = "O\xC3\xB9 sont vos clients et leurs PC, d'apr\xC3\xA8s la ville de leur derni\xC3\xA8re connexion au service";
    if (hasReply_) {
        s += " \xC2\xB7 " + std::to_string(reply_.totals.clients) + " client" + (reply_.totals.clients > 1 ? "s" : "") + ", " + std::to_string(reply_.totals.pcs) +
             " PC, " + std::to_string(reply_.cityCount) + " ville" + (reply_.cityCount > 1 ? "s" : "") + ", " + std::to_string(reply_.countries) + " pays";
        if (reply_.unplacedPcs > 0) s += " \xC2\xB7 " + std::to_string(reply_.unplacedPcs) + " PC sans lieu";
    }
    return s;
}

void GlobeStatsView::onLayout() {
    const Rect b = bounds();
    if (board_->fullScreen()) {
        for (ui::Widget* w : {static_cast<ui::Widget*>(period_), static_cast<ui::Widget*>(deploy_), static_cast<ui::Widget*>(settings_),
                              static_cast<ui::Widget*>(retry_)})
            w->setBounds({b.x, b.y, 0.f, 0.f});
        period_->setVisibility(ui::Visibility::Collapsed);
        board_->setBounds(b);
        bannerRect_ = {};
        return;
    }
    period_->setVisibility(ui::Visibility::Visible);
    // en haut : le titre (la sous-page), la ligne, la periode a droite
    const float topH = title_ ? 64.f : 34.f;
    const float pw = period_->preferredWidth();
    period_->setBounds({b.right() - pw, b.y + (title_ ? 14.f : 0.f), pw, 34.f});
    float y = b.y + topH + 12.f;
    // le bandeau (un service a deployer, pas regle, un refus) : le texte, son bouton a droite
    bannerRect_ = {};
    ui::Button* btn = deploy_->visible() ? deploy_ : settings_->visible() ? settings_ : retry_->visible() ? retry_ : nullptr;
    if (!problem_.empty()) {
        const float bw = btn ? std::max(140.f, btn->sizeHint().preferred.w + 12.f) : 0.f;
        const float h = statskit::bannerHeight(problem_, b.w, bw + 24.f);
        bannerRect_ = {b.x, y, b.w, h};
        if (btn) btn->setBounds({bannerRect_.right() - 14.f - bw, bannerRect_.y + (h - 34.f) / 2.f, bw, 34.f});
        y += h + 12.f;
    }
    board_->setBounds({b.x, y, b.w, std::max(360.f, b.bottom() - y)});
}

void GlobeStatsView::onPaint(const ui::PaintContext& ctx) {
    tick(ctx.time);
    if (board_->fullScreen()) return;
    gfx::IRenderer& r = ctx.r;
    const Rect b = bounds();
    const Rect p = period_->bounds();
    const float lx = p.x - 10.f - r.measure("P\xC3\xA9riode", FontId{13}).width;
    if (title_) {
        r.drawText({b.x, b.y + 4.f}, "Le globe", gfx::semibold(FontId{28}), kInk);
        r.drawText({b.x, b.y + 44.f}, fitText(r, topLine(), FontId{13}, lx - 16.f - b.x), FontId{13}, kMuted);
    } else {
        r.drawText({b.x, midY(r, FontId{13}, b.y, 34.f)}, fitText(r, topLine(), FontId{13}, lx - 16.f - b.x), FontId{13}, kMuted);
    }
    r.drawText({lx, midY(r, FontId{13}, p.y, p.h)}, "P\xC3\xA9riode", FontId{13}, kMuted);
    if (!bannerRect_.empty()) {
        const ui::Button* btn = deploy_->visible() ? deploy_ : settings_->visible() ? settings_ : retry_->visible() ? retry_ : nullptr;
        statskit::paintBanner(r, ctx.theme, bannerRect_, problem_, tooOld_ || btn == retry_, btn ? btn->bounds().w + 24.f : 0.f);
    }
}

// ============================================================================ la petite carte du tableau de bord
namespace {

class DashboardGlobe final : public ui::Widget, public statskit::Reloadable {
public:
    explicit DashboardGlobe(ShellHost host) : ui::Widget("tableau.globe"), host_(std::move(host)) {
        card_ = add(*this, std::make_unique<GlobeMiniCard>("tableau.globe.carte"));
        card_->onOpen = [this] {
            if (host_.navigate) host_.navigate("globe");
        };
        card_->setMessage("Relev\xC3\xA9 en cours\xE2\x80\xA6");
    }
    ~DashboardGlobe() override { *alive_ = false; }
    void reload() override {
        lastFetch_ = -1e9;
        retryAt_ = 0.0;
        if (now_ >= 0.0) tick(now_);
    }
    [[nodiscard]] ui::SizeHint sizeHint() const override { return card_->sizeHint(); }

protected:
    void onLayout() override { card_->setBounds(bounds()); }
    void onPaint(const ui::PaintContext& ctx) override { tick(ctx.time); }

private:
    void tick(double now) {
        now_ = now;
        if (fetching_ || now < retryAt_ || now - lastFetch_ < kStaleCount) return;
        Workspace& w = host_.workspace();
        std::string why;
        if (hooks().globe || sampleMode()) why = w.statsServiceNote();
        else why = statskit::blocked(w);
        lastFetch_ = now;
        if (!why.empty()) {
            card_->setActive(-1);
            card_->setMessage(why);
            return;
        }
        const Workspace* wp = &w;
        auto out = std::make_shared<stats::GlobeReply>();
        fetching_ = true;
        const bool started = statskit::runQuietly(host_, "Globe : relev\xC3\xA9", [wp, out] { return fetchGlobeNow(*wp, 30, *out); },
                                                  [this, alive = alive_, out](const OpResult& r) {
                                                      if (!*alive) return;
                                                      fetching_ = false;
                                                      if (!r.ok) {
                                                          card_->setActive(-1);
                                                          card_->setMessage(r.message);
                                                          return;
                                                      }
                                                      card_->setPlaces(placesOf(*out));
                                                      card_->setActive(out->totals.active);
                                                      card_->setMessage(out->cities.empty() ? std::string("Aucune ville pour l'instant : les PC en 1.47.0 "
                                                                                                          "appara\xC3\xAEtront \xC3\xA0 leur ville.")
                                                                                            : std::string());
                                                  });
        if (!started) {
            fetching_ = false;
            lastFetch_ = -1e9;
            retryAt_ = now + kRetry;
        }
    }

    ShellHost             host_;
    GlobeMiniCard*        card_{nullptr};
    bool                  fetching_{false};
    double                now_{-1.0}, lastFetch_{-1e9}, retryAt_{0.0};
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// La sous-page << globe >> (Statistiques > Le globe) : la vue avec son titre, comme la planche.
class GlobePage final : public Page {
public:
    explicit GlobePage(ShellHost host) : Page("page.globe", host) { view_ = add(*this, std::make_unique<GlobeStatsView>(std::move(host), true)); }
    [[nodiscard]] std::string title() const override { return "Le globe"; }
    void refresh() override { invalidate(); }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const bool full = view_->board().fullScreen();
        const float pad = full ? 0.f : (b.w < 900.f ? 20.f : 28.f);
        view_->setBounds({b.x + pad, b.y + (full ? 0.f : 18.f), b.w - 2.f * pad, b.h - (full ? 0.f : 36.f)});
    }
    void onPaint(const ui::PaintContext& ctx) override { ctx.r.fillRect(bounds(), ctx.theme.color.windowBg); }

private:
    GlobeStatsView* view_{nullptr};
};

} // namespace

} // namespace globestats

namespace statskit {
std::unique_ptr<ui::Widget> makeStatsGlobeView(ShellHost host) { return std::make_unique<globestats::GlobeStatsView>(std::move(host), false); }
std::unique_ptr<ui::Widget> makeDashboardGlobe(ShellHost host) { return std::make_unique<globestats::DashboardGlobe>(std::move(host)); }
} // namespace statskit

PagePtr makeGlobePage(ShellHost host) { return std::make_unique<globestats::GlobePage>(std::move(host)); }

} // namespace license_admin::gui
