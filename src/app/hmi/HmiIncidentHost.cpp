// =============================================================================
//  app/hmi/HmiIncidentHost.cpp - 1.20.0 : la surveillance des incidents (voir l'en-tete)
// =============================================================================
#include "HmiIncidentHost.hpp"

#include "../Capture.hpp"
#include "../ai/AiJson.hpp"
#include "../../core/AtomicFile.hpp"
#include "../../core/Version.hpp"
#include "../../hmi/HmiRecording.hpp"
#include "../../hmi/HmiRelations.hpp"
#include "../../platform/Geometry.hpp"
#include "../../platform/Renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <map>
#include <utility>

namespace app {

namespace inc = hmi::incident;
namespace st = app::ai::incidents;
namespace json = app::ai::json;

namespace {

constexpr std::size_t kMaxFrameBytes = 96u * 1024u * 1024u;   // le tampon circulaire, borne
// 1.20.2 : la qualite des images et des videos (<< horrible >> en 1.20.0). Avant : JPEG qualite 75, que stb code en
// 4:2:0 (la couleur a demi-resolution : textes colores et traits fins baveux), une reduction par un facteur ENTIER
// (une vue de 1300 px tombait a 650), 2 images par seconde a une cadence fausse. Maintenant : 4:4:4 (stb : qualite
// > 90), une reduction a la juste taille (moyenne ponderee), l'image du defaut codee a part en qualite maximale.
constexpr int         kMaxFrameWidth = 1920;                   // au-dela : reduite (la memoire, le disque)
constexpr int         kVideoQuality = 92;                      // > 90 : 4:4:4 chez stb (pas de couleur baveuse)
constexpr int         kStillQuality = 95;                      // l'image du defaut (capture.jpg)
constexpr std::size_t kMaxEvents = 300;
constexpr double      kAnalysisTimeout = 240.0;
constexpr std::size_t kMaxRetries = 50;

std::filesystem::path P(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string U(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// 1.20.2 : une image plus large que kMaxFrameWidth, reduite A LA JUSTE TAILLE (et plus d'un facteur entier : 1300 px
// donnaient 650) - chaque pixel d'arrivee est la moyenne ponderee des pixels qu'il couvre (une boite fractionnaire).
void shrink(std::vector<std::uint8_t>& rgba, int& w, int& h) {
    if (w <= kMaxFrameWidth) return;
    const int nw = kMaxFrameWidth;
    const int nh = std::max(1, static_cast<int>(std::lround(static_cast<double>(h) * nw / w)));
    const double sx = static_cast<double>(w) / nw, sy = static_cast<double>(h) / nh;
    // les poids de chaque colonne (et de chaque ligne) d'arrivee : les pixels de depart couverts, et leur part
    struct Span { int from{0}, to{0}; std::vector<double> wt; };
    const auto spans = [](int n, double scale, int limit) {
        std::vector<Span> out(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const double a = i * scale, b = (i + 1) * scale;
            Span sp;
            sp.from = static_cast<int>(std::floor(a));
            sp.to = std::min(limit, static_cast<int>(std::ceil(b)));
            for (int k = sp.from; k < sp.to; ++k) sp.wt.push_back((std::min(b, k + 1.0) - std::max(a, static_cast<double>(k))) / scale);
            out[static_cast<std::size_t>(i)] = std::move(sp);
        }
        return out;
    };
    const auto cols = spans(nw, sx, w), rows = spans(nh, sy, h);
    // d'abord en largeur (une ligne a la fois), puis en hauteur
    std::vector<float> mid(static_cast<std::size_t>(nw) * static_cast<std::size_t>(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < nw; ++x) {
            const Span& sp = cols[static_cast<std::size_t>(x)];
            float acc[4] = {0.f, 0.f, 0.f, 0.f};
            for (int k = sp.from; k < sp.to; ++k) {
                const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(k)) * 4;
                const float f = static_cast<float>(sp.wt[static_cast<std::size_t>(k - sp.from)]);
                for (int c = 0; c < 4; ++c) acc[c] += f * rgba[i + static_cast<std::size_t>(c)];
            }
            const std::size_t o = (static_cast<std::size_t>(y) * static_cast<std::size_t>(nw) + static_cast<std::size_t>(x)) * 4;
            for (int c = 0; c < 4; ++c) mid[o + static_cast<std::size_t>(c)] = acc[c];
        }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh) * 4);
    for (int y = 0; y < nh; ++y) {
        const Span& sp = rows[static_cast<std::size_t>(y)];
        for (int x = 0; x < nw; ++x) {
            float acc[4] = {0.f, 0.f, 0.f, 0.f};
            for (int k = sp.from; k < sp.to; ++k) {
                const std::size_t i = (static_cast<std::size_t>(k) * static_cast<std::size_t>(nw) + static_cast<std::size_t>(x)) * 4;
                const float f = static_cast<float>(sp.wt[static_cast<std::size_t>(k - sp.from)]);
                for (int c = 0; c < 4; ++c) acc[c] += f * mid[i + static_cast<std::size_t>(c)];
            }
            const std::size_t o = (static_cast<std::size_t>(y) * static_cast<std::size_t>(nw) + static_cast<std::size_t>(x)) * 4;
            for (int c = 0; c < 4; ++c) out[o + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::clamp(acc[c] + 0.5f, 0.f, 255.f));
        }
    }
    rgba = std::move(out);
    w = nw;
    h = nh;
}

// L'empreinte grossiere du projet en marche : le graphe est refait quand elle change (une simulation relancee
// sur un projet modifie) - jamais a chaque cycle.
std::uint64_t printOf(const hmi::Project& p) {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(p.programs.variables.size());
    mix(p.alarms.size());
    mix(p.views.size());
    mix(p.programs.scripts.size());
    mix(p.programs.functions.size());
    mix(p.equipments.size());
    mix(p.programs.types.size());
    for (const auto& a : p.alarms) {
        mix(std::hash<std::string>{}(a.name));
        mix(std::hash<std::string>{}(a.condition));
    }
    for (const auto& v : p.programs.variables) {
        mix(std::hash<std::string>{}(v.name));
        mix(std::hash<std::string>{}(v.equipment));
    }
    return h;
}

json::Value settingsJson(const inc::Settings& s) {
    json::Value o = json::Value::object();
    o.set("format", 1);
    o.set("active", s.enabled);
    json::Value w = json::Value::array();
    for (const auto& x : s.watched) w.push(x);
    o.set("surveillees", w);
    json::Value ig = json::Value::array();
    for (const auto& x : s.ignored) ig.push(x);
    o.set("ignorees", ig);
    o.set("communication", s.communication);
    o.set("maintien_s", s.heldAfter);
    o.set("anti_rebond_s", s.debounce);
    o.set("avant_s", s.before);
    o.set("apres_s", s.after);
    o.set("chronologie_s", s.timeline);
    o.set("images_par_seconde", s.framesPerSecond);
    o.set("en_attente_max", static_cast<std::int64_t>(s.maxPending));
    return o;
}
inc::Settings settingsOf(const json::Value& o) {
    inc::Settings s;
    s.enabled = o.flag("active", true);
    for (const auto& x : o.at("surveillees").items()) s.watched.push_back(x.asString());
    for (const auto& x : o.at("ignorees").items()) s.ignored.push_back(x.asString());
    s.communication = o.flag("communication", true);
    s.heldAfter = std::clamp(o.num("maintien_s", 30), 0.0, 86400.0);
    s.debounce = std::clamp(o.num("anti_rebond_s", 5), 0.0, 3600.0);
    s.before = std::clamp(o.num("avant_s", 10), 0.0, 120.0);   // 1.20.2 : comme la fenetre des reglages (0 a 120 s)
    s.after = std::clamp(o.num("apres_s", 5), 0.0, 120.0);
    s.timeline = std::clamp(o.num("chronologie_s", 120), 5.0, 3600.0);
    s.framesPerSecond = static_cast<int>(std::clamp<std::int64_t>(o.integer("images_par_seconde", inc::Settings{}.framesPerSecond), 0, 10));
    s.maxPending = static_cast<std::size_t>(std::clamp<std::int64_t>(o.integer("en_attente_max", 200), 1, 100000));
    return s;
}

} // namespace

// ======================================================================= la vie ==
IncidentHost::IncidentHost() : listing_(std::make_shared<Listing>()) {
    thread_ = std::thread([this] { worker(); });
}

IncidentHost::~IncidentHost() { shutdown(); }

void IncidentHost::shutdown() {
    if (provider_) {
        provider_->cancel();
        provider_.reset();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return;
        stop_ = true;   // le fil finit les ecritures en cours, puis s'arrete
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void IncidentHost::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return;
        jobs_.push_back(std::move(job));
    }
    wake_.notify_all();
}

void IncidentHost::worker() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
            if (jobs_.empty()) return;   // arrete, et plus rien a ecrire
            job = std::move(jobs_.front());
            jobs_.pop_front();
            busy_ = true;
        }
        try {
            job();
        } catch (const std::exception& e) {
            postEvent(std::string("Incidents : erreur interne (") + e.what() + ")");
        } catch (...) {
            postEvent("Incidents : erreur interne");
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
        }
    }
}

bool IncidentHost::drain(int timeoutMs) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (jobs_.empty() && !busy_) return true;
        }
        if (std::chrono::steady_clock::now() > end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

std::size_t IncidentHost::writesWaiting() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size() + (busy_ ? 1 : 0);
}
std::size_t IncidentHost::bufferedFrames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return frames_.size();
}
std::size_t IncidentHost::bufferedBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return frameBytes_;
}

void IncidentHost::postEvent(std::string line) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(std::move(line));
    if (events_.size() > kMaxEvents) events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(events_.size() - kMaxEvents));
}

std::vector<std::string> IncidentHost::takeEvents() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(events_, {});
}

std::shared_ptr<const IncidentHost::Listing> IncidentHost::listing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return listing_;
}

std::string IncidentHost::root() const {
    if (projectFolder_.empty()) return {};
    return U((P(projectFolder_) / P(std::string(hmi::rec::kNativeFolder))).lexically_normal());
}

// ================================================================== les reglages ==
void IncidentHost::setProjectFolder(const std::string& folder) {
    if (folder == projectFolder_) return;
    projectFolder_ = folder;
    triggers_.clear();
    recordings_.clear();
    analyses_.clear();
    monitor_.clear();
    graph_.reset();
    graphProject_ = nullptr;
    store_ = folder.empty() ? nullptr : std::make_shared<st::Store>(root());
    loadSettings();
    if (!store_) {
        std::lock_guard<std::mutex> lock(mutex_);
        listing_ = std::make_shared<Listing>();
        ++revision_;
        return;
    }
    // la reprise apres un arret brutal, puis la liste - sur le fil d'ecriture
    auto store = store_;
    post([this, store] {
        for (auto& n : store->recover()) postEvent("Incidents : " + n);
    });
    refresh();
}

void IncidentHost::loadSettings() {
    settings_ = {};
    if (!projectFolder_.empty()) {
        std::string text;
        json::Value o;
        if (core::readFileAll(P(root()) / "reglages-incidents.json", text) && json::parse(text, o)) settings_ = settingsOf(o);
    }
    monitor_.setSettings(settings_);
}

bool IncidentHost::setSettings(const inc::Settings& s, std::string* why) {
    // 1.20.2 : en marche <-> arretee - le detecteur repart de zero (arrete, il ne voyait plus les disparitions : une
    // alarme disparue pendant l'arret, reapparue apres, n'aurait pas fait d'incident) ; les defauts en attente de leur
    // image sont oublies ; arretee, les videos en cours se terminent avec les images deja prises.
    if (s.enabled != settings_.enabled) {
        triggers_.clear();
        captureNow_ = false;
        monitor_.clear();
        if (!s.enabled)
            for (auto& r : recordings_) r.until = std::min(r.until, now_);
    }
    settings_ = s;
    monitor_.setSettings(s);
    if (projectFolder_.empty()) {
        if (why) *why = "aucun projet ouvert : les r\xC3\xA9glages valent pour cette s\xC3\xA9" "ance";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(P(root()), ec);
    core::AtomicWrite how;
    how.keepBackup = false;
    const auto r = core::writeFileAtomic(P(root()) / "reglages-incidents.json", json::write(settingsJson(s)), how);
    if (!r) {
        if (why) *why = r.error().message();
        return false;
    }
    return true;
}

void IncidentHost::saveSettings() { (void)setSettings(settings_); }

// 1.20.2 : l'interrupteur general.
bool IncidentHost::setEnabled(bool on, std::string* why) {
    inc::Settings s = settings_;
    s.enabled = on;
    return setSettings(s, why);
}

// ======================================================= le fil de l'interface ==
void IncidentHost::notice(const hmi::AlarmNotice& n) {
    if (!store_ || !settings_.enabled) return;
    monitor_.notice(n, now_);
    auto fronts = monitor_.takeTriggers();
    if (fronts.empty()) return;
    // 1.20.2 : la case << Incidents >> de l'alarme (ou de son groupe) decochee : pas d'incident - decide ICI, avant
    // toute capture (ni image, ni video, rien d'ecrit). Le detecteur a quand meme vu le front : l'alarme reste dans la
    // chronologie des autres incidents. Le projet en marche (celui que la simulation ou le poste fait tourner).
    const std::optional<IncidentContext> ctx = source_ ? source_() : std::nullopt;
    for (auto& e : fronts) {
        if (ctx && ctx->project && !inc::allowedFor(*ctx->project, e.name, e.name == n.name ? std::string_view(n.group) : std::string_view{}))
            continue;
        Trigger t;
        t.event = std::move(e);
        t.at = now_;
        triggers_.push_back(std::move(t));
        captureNow_ = true;   // l'image de ce cycle : la prochaine dessinee
    }
}

bool IncidentHost::wantsFrame(double now) const {
    if (!store_ || !settings_.enabled) return false;
    if (captureNow_) return true;
    if (settings_.framesPerSecond <= 0) return false;
    return now - lastFrame_ >= 1.0 / settings_.framesPerSecond;
}

void IncidentHost::offerFrame(gfx::IRenderer& renderer, const gfx::Rect& area, double now) {
    if (area.w <= 4.f || area.h <= 4.f) return;
    std::vector<std::uint8_t> rgba;
    int fw = 0, fh = 0;
    if (!renderer.readPixels(rgba, fw, fh) || fw <= 0 || fh <= 0) return;
    const int x = std::clamp(static_cast<int>(std::floor(area.x)), 0, fw - 1);
    const int y = std::clamp(static_cast<int>(std::floor(area.y)), 0, fh - 1);
    const int w = std::clamp(static_cast<int>(std::ceil(area.w)), 1, fw - x);
    const int h = std::clamp(static_cast<int>(std::ceil(area.h)), 1, fh - y);
    std::vector<std::uint8_t> crop(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    for (int row = 0; row < h; ++row)
        std::copy_n(rgba.data() + (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(fw) + static_cast<std::size_t>(x)) * 4,
                    static_cast<std::size_t>(w) * 4, crop.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4);
    offerPixels(std::move(crop), w, h, now);
}

void IncidentHost::offerPixels(std::vector<std::uint8_t> rgba, int w, int h, double now) {
    if (!store_ || w <= 0 || h <= 0 || rgba.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4) return;
    lastFrame_ = now;
    const std::uint64_t seq = ++frameSeq_;
    const bool capture = captureNow_;
    captureNow_ = false;
    if (capture)
        for (auto& t : triggers_)
            if (t.waitingCapture && t.captureSeq == 0) t.captureSeq = seq;
    // le codage JPEG sur le fil d'ecriture : le fil de l'interface n'a fait qu'une copie
    const double keep = settings_.before + settings_.after + 2.0;
    post([this, rgba = std::move(rgba), w, h, now, seq, capture, keep]() mutable {
        encodeFrame(std::move(rgba), w, h, now, capture ? seq : 0, keep);
    });
}

void IncidentHost::encodeFrame(std::vector<std::uint8_t> rgba, int w, int h, double t, std::uint64_t seq, double keep) {
    shrink(rgba, w, h);
    std::vector<std::uint8_t> jpeg;
    if (!encodeJpeg(rgba, w, h, kVideoQuality, jpeg) || jpeg.empty()) return;
    Frame f;
    f.jpeg = std::make_shared<const std::vector<std::uint8_t>>(std::move(jpeg));
    f.w = w;
    f.h = h;
    f.t = t;
    // 1.20.2 : l'image du defaut (capture.jpg) codee a part, en qualite maximale ; la video garde la sienne
    Frame still = f;
    if (seq != 0) {
        std::vector<std::uint8_t> best;
        if (encodeJpeg(rgba, w, h, kStillQuality, best) && !best.empty()) still.jpeg = std::make_shared<const std::vector<std::uint8_t>>(std::move(best));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    frames_.push_back(f);
    frameBytes_ += f.jpeg->size();
    while (!frames_.empty() && (frames_.front().t < t - keep || frameBytes_ > kMaxFrameBytes)) {
        frameBytes_ -= frames_.front().jpeg->size();
        frames_.pop_front();
    }
    if (seq != 0) captures_.emplace_back(seq, still);
}

void IncidentHost::tick(double now) {
    now_ = now;
    if (!store_) return;
    // les resultats du fil d'ecriture : les incidents crees, a analyser
    std::vector<std::pair<std::string, inc::Snapshot>> created;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        created.swap(createdQueue_);
    }
    for (auto& [folder, snap] : created) {
        ++created_;
        if (!analyst_) continue;
        std::string why;
        const auto a = analyst_(&why);
        // l'IA integree (ou aucune) : la fiche de l'incident est son analyse ; sinon, l'IA choisie en arriere-plan
        if (a && !a->provider.empty() && a->provider != "integree" && a->provider != "demo") analyses_.push_back(Job{folder, std::move(snap)});
    }
    if (!settings_.enabled) {
        triggers_.clear();
        (void)monitor_.take();
        // 1.20.2 : arretee, et plus de video a finir : le tampon des images est rendu (rien ne s'y ajoute)
        if (recordings_.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            frames_.clear();
            frameBytes_ = 0;
            captures_.clear();
        }
    } else {
        monitor_.tick(now);
        (void)monitor_.take();   // la chronologie est gardee par le detecteur ; rien a montrer ici
        // l'etat des equipements, une fois par seconde : les pertes de communication
        if (settings_.communication && source_ && now - lastEquip_ >= 1.0) {
            lastEquip_ = now;
            if (const auto ctx = source_(); ctx && !ctx->equipments.empty()) {
                monitor_.equipments(ctx->equipments, now);
                for (auto& e : monitor_.takeTriggers()) {
                    Trigger t;
                    t.event = std::move(e);
                    t.at = now;
                    triggers_.push_back(std::move(t));
                    captureNow_ = true;
                }
            }
        }
        // les defauts : leur image arrivee (ou une seconde sans image : aucune vue en marche affichee)
        for (std::size_t i = 0; i < triggers_.size();) {
            Trigger& t = triggers_[i];
            if (t.captureSeq != 0 && !t.capture) {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto it = captures_.begin(); it != captures_.end(); ++it)
                    if (it->first == t.captureSeq) {
                        t.capture = it->second;
                        captures_.erase(it);
                        break;
                    }
            }
            if (t.capture || now - t.at > 1.0) {
                createIncident(t, now);
                triggers_.erase(triggers_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // les images de defaut que plus personne n'attend (bornees)
            if (captures_.size() > 16) captures_.erase(captures_.begin(), captures_.end() - 16);
        }
    }
    // les videos : les secondes d'apres sont passees
    for (std::size_t i = 0; i < recordings_.size();) {
        if (now < recordings_[i].until) {
            ++i;
            continue;
        }
        const Recording rec = recordings_[i];
        recordings_.erase(recordings_.begin() + static_cast<std::ptrdiff_t>(i));
        st::Video v;
        v.fps = settings_.framesPerSecond;
        v.before = settings_.before;
        v.after = settings_.after;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<const Frame*> window;
            for (const auto& f : frames_)
                if (f.t >= rec.trigger - settings_.before - 0.01 && f.t <= rec.until + 0.01) window.push_back(&f);
            if (!window.empty()) {
                // une seule taille d'image (la fenetre a pu changer) : celle de la plus recente
                v.width = window.back()->w;
                v.height = window.back()->h;
                std::vector<const Frame*> same;
                for (const Frame* f : window)
                    if (f->w == v.width && f->h == v.height) same.push_back(f);
                // 1.20.2 : une CADENCE JUSTE. Les images arrivent a intervalles irreguliers (une image par cycle
                // dessine, plus celle du defaut) : la video les ecrivait a la suite, a la cadence reglee - elle allait
                // trop vite ou par saccades. Maintenant, a chaque top de la cadence, l'image la plus recente a ce
                // moment (repetee si rien de neuf) : la duree de la video est la duree reelle.
                if (v.fps > 0 && same.size() >= 2) {
                    const double step = 1.0 / v.fps, t0 = same.front()->t, t1 = same.back()->t;
                    std::size_t k = 0;
                    for (double at = t0; at <= t1 + 1e-6 && v.frames.size() < 20000; at += step) {
                        while (k + 1 < same.size() && same[k + 1]->t <= at + 1e-6) ++k;
                        v.frames.push_back(same[k]->jpeg);   // 1.21.0 : partagee, pas copiee
                        v.times.push_back(at - rec.trigger);
                    }
                } else {
                    for (const Frame* f : same) {
                        v.frames.push_back(f->jpeg);
                        v.times.push_back(f->t - rec.trigger);
                    }
                }
            }
        }
        if (v.frames.size() < 2) v.note = "trop peu d'images : la vue en marche n'\xC3\xA9tait pas affich\xC3\xA9" "e assez longtemps";
        auto store = store_;
        const std::string id = rec.id;   // l'identifiant de l'incident (son dossier est connu du fil)
        post([this, store, id, v = std::move(v)] {
            const auto it = idToFolder_.find(id);
            if (it == idToFolder_.end()) return;   // la creation a echoue : rien a completer
            std::string why;
            if (!store->addVideo(it->second, v, &why)) postEvent("Incident : vid\xC3\xA9o non enregistr\xC3\xA9" "e (" + why + ")");
        });
    }
    // les creations qui ont echoue (le disque) : reessayees toutes les 30 s
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& r : pendingRetries_) retries_.push_back(std::move(r));
        pendingRetries_.clear();
    }
    if (retries_.size() > kMaxRetries) retries_.erase(retries_.begin(), retries_.end() - static_cast<std::ptrdiff_t>(kMaxRetries));
    if (!retries_.empty() && now - lastRetry_ >= 30.0) {
        lastRetry_ = now;
        auto store = store_;
        std::vector<Retry> again;
        again.swap(retries_);
        for (auto& r : again) post([this, store, r = std::move(r)]() mutable { writeIncident(store, std::move(r)); });
    }
    // les analyses
    if (provider_) pollAnalysis();
    if (!provider_ && !analyses_.empty()) startNextAnalysis();
}

// =================================================================== la creation ==
void IncidentHost::createIncident(Trigger& t, double now) {
    std::optional<IncidentContext> ctx = source_ ? source_() : std::nullopt;
    inc::SnapshotInput in;
    in.trigger = t.event;
    in.timeline = monitor_.timeline(t.event.t - settings_.timeline, now + 0.001);
    in.active = monitor_.active();
    in.software = std::string(XPG_PRODUCT_IHM " " XPG_ANALYZER_VERSION);
    in.id = st::newId();
    in.createdAt = st::nowStamp();
    if (in.trigger.at.empty()) in.trigger.at = in.createdAt;
    if (ctx) {
        in.project = ctx->project;
        in.read = ctx->read;
        in.equipments = ctx->equipments;
        in.openView = ctx->openView;
        in.projectName = ctx->projectName;
        in.projectVersion = ctx->projectVersion;
        if (ctx->project) {
            const std::uint64_t print = printOf(*ctx->project);
            if (!graph_ || graphProject_ != ctx->project || graphPrint_ != print) {
                graph_ = std::make_shared<const hmi::rel::Graph>(hmi::rel::build(*ctx->project));
                graphProject_ = ctx->project;
                graphPrint_ = print;
            }
            in.graph = graph_.get();
        }
    }
    Retry r;
    r.maxPending = settings_.maxPending;
    r.snapshot = inc::snapshot(in);   // copie : rien n'est relu du projet ensuite
    if (!ctx) r.snapshot.missing.push_back("aucune vue en marche : ni valeur, ni \xC3\xA9tat des \xC3\xA9quipements");
    if (t.capture) {
        r.capture.jpeg = *t.capture->jpeg;
        r.capture.width = t.capture->w;
        r.capture.height = t.capture->h;
        r.capture.at = r.snapshot.createdAt;
    } else {
        r.capture.note = "aucune vue en marche n'\xC3\xA9tait affich\xC3\xA9" "e : pas d'image";
    }
    if (settings_.framesPerSecond > 0 && t.capture) recordings_.push_back({r.snapshot.id, t.at, t.at + settings_.after});
    auto store = store_;
    post([this, store, r = std::move(r)]() mutable { writeIncident(store, std::move(r)); });
}

void IncidentHost::writeIncident(const std::shared_ptr<st::Store>& store, Retry r) {
    // l'historique (les incidents precedents de cette alarme) : lu ici, sur le fil d'ecriture
    if (r.attempts == 0) r.snapshot.previous = store->history(r.snapshot.trigger.name, r.snapshot.id, 5);
    const st::Analysis initial = st::initialAnalysis(r.snapshot, r.snapshot.createdAt);
    std::string folder, why;
    if (store->create(r.snapshot, r.capture, initial, folder, &why)) {
        idToFolder_[r.snapshot.id] = folder;
        const std::size_t pending = store->pendingCount();
        postEvent("Incident enregistr\xC3\xA9 : " + r.snapshot.trigger.name + " (" + std::string(inc::eventLabel(r.snapshot.trigger.kind)) + ", "
                  + U(P(folder).filename()) + ")");
        if (pending > r.maxPending)
            postEvent("Incidents : " + std::to_string(pending) + " en attente (plus de " + std::to_string(r.maxPending)
                      + ") - valide ou supprime les anciens");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            createdQueue_.emplace_back(folder, r.snapshot);
        }
        relist(store);
        return;
    }
    ++r.attempts;
    postEvent("Incident non enregistr\xC3\xA9 (" + r.snapshot.trigger.name + ") : " + why
              + (r.attempts < 4 ? " - il est gard\xC3\xA9 en m\xC3\xA9moire et r\xC3\xA9" "essay\xC3\xA9" : " - abandonn\xC3\xA9 apr\xC3\xA8s 4 essais"));
    if (r.attempts < 4) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pendingRetries_.size() < kMaxRetries) pendingRetries_.push_back(std::move(r));
    }
}

void IncidentHost::relist(const std::shared_ptr<st::Store>& store) {
    auto l = std::make_shared<Listing>();
    l->pending = store->list(st::State::Pending);
    l->validated = store->list(st::State::Validated);
    std::lock_guard<std::mutex> lock(mutex_);
    l->revision = ++revision_;
    listing_ = std::move(l);
}

void IncidentHost::refresh() {
    if (!store_) return;
    auto store = store_;
    post([this, store] { relist(store); });
}

// ===================================================================== les gestes ==
void IncidentHost::comment(const std::string& folder, const std::string& text, const std::string& author) {
    if (!store_ || text.empty()) return;
    auto store = store_;
    post([this, store, folder, text, author] {
        st::Comment c;
        c.createdAt = st::nowStamp();
        c.author = author;
        c.text = text;
        std::string why;
        if (store->addComment(folder, c, &why)) postEvent("Incident : commentaire " + std::to_string(c.number) + " ajout\xC3\xA9");
        else postEvent("Incident : commentaire non ajout\xC3\xA9 (" + why + ")");
        relist(store);
    });
}

void IncidentHost::analyze(const std::string& folder) {
    if (!store_) return;
    const auto l = listing();
    for (const auto* list : {&l->pending, &l->validated})
        for (const auto& it : *list)
            if (it.folder == folder && it.readable) {
                for (const auto& j : analyses_)
                    if (j.folder == folder) return;   // deja demandee
                if (current_ && current_->folder == folder) return;
                analyses_.push_back(Job{folder, it.snapshot});
                return;
            }
    postEvent("Incident : introuvable dans la liste (" + folder + ")");
}

void IncidentHost::validate(const std::string& folder, const std::string& note, const std::string& author) {
    if (!store_) return;
    auto store = store_;
    post([this, store, folder, note, author] {
        std::string validated, why;
        if (store->validate(folder, author, note, validated, &why)) postEvent("Incident valid\xC3\xA9 : " + U(P(validated).filename()));
        else postEvent("Incident non valid\xC3\xA9 : " + why);
        relist(store);
    });
}

void IncidentHost::remove(const std::string& folder) {
    if (!store_) return;
    auto store = store_;
    post([this, store, folder] {
        std::string why;
        if (store->remove(folder, &why)) postEvent("Incident supprim\xC3\xA9 : " + U(P(folder).filename()));
        else postEvent("Incident non supprim\xC3\xA9 : " + why);
        relist(store);
    });
}

st::Integrity IncidentHost::verify(const std::string& folder) const {
    if (!store_) return {};
    return store_->verify(folder);
}

// ==================================================================== l'analyse ==
void IncidentHost::startNextAnalysis() {
    Job job = std::move(analyses_.front());
    analyses_.pop_front();
    auto store = store_;
    const auto fail = [&](const std::string& why) {
        st::Analysis a;
        a.kind = "complementaire";
        a.createdAt = st::nowStamp();
        a.source = currentSource_.empty() ? std::string("IA distante") : currentSource_;
        a.state = "echec";
        a.error = why;
        post([this, store, folder = job.folder, a]() mutable {
            std::string w;
            if (store->addAnalysis(folder, a, &w)) postEvent("Analyse en attente (" + a.error + ") : l'incident est gard\xC3\xA9, relance-la plus tard");
            else postEvent("Analyse : " + w);
            relist(store);
        });
    };
    currentSource_.clear();
    std::string why;
    const auto setup = analyst_ ? analyst_(&why) : std::nullopt;
    if (!setup || setup->provider.empty() || setup->provider == "integree" || setup->provider == "demo") {
        fail(why.empty() ? std::string("aucune IA distante choisie dans l'Assistant (l'IA int\xC3\xA9gr\xC3\xA9" "e a d\xC3\xA9j\xC3\xA0 fait la fiche)") : why);
        return;
    }
    currentSource_ = setup->label.empty() ? setup->provider : setup->label;
    // les analyses deja faites (approfondir, et le cache)
    std::vector<st::Analysis> earlier;
    {
        const auto l = listing();
        for (const auto* list : {&l->pending, &l->validated})
            for (const auto& it : *list)
                if (it.folder == job.folder) earlier = it.analyses;
    }
    const std::string system = st::analysisSystemPrompt();
    const std::string request = st::analysisRequest(job.snapshot, 12000, earlier);
    currentModel_ = setup->base.model;
    // le cache : l'empreinte du contexte de l'INCIDENT (pas des analyses jointes) pour ce modele - la meme demande au
    // meme modele n'est pas renvoyee ; un autre modele, oui (il approfondit, les analyses d'avant jointes)
    currentHash_ = st::contextHash(system, inc::aiContext(job.snapshot, 12000), setup->provider + "/" + currentModel_);
    for (const auto& a : earlier)
        if (a.state == "faite" && a.contextHash == currentHash_) {
            postEvent("Analyse d\xC3\xA9j\xC3\xA0 faite pour ce contexte par " + a.source + " (" + a.createdAt + ") : pas renvoy\xC3\xA9" "e");
            return;
        }
    auto prov = factory_ ? factory_(setup->provider) : app::ai::makeProvider(setup->provider);
    if (!prov) {
        fail("fournisseur " + setup->provider + " non branch\xC3\xA9");
        return;
    }
    app::ai::ProviderRequest req = setup->base;
    req.system = system;
    req.systemStable.clear();
    req.systemDynamic.clear();
    req.toolsJson.clear();
    req.toolsInText = false;
    req.maxTokens = std::min(req.maxTokens > 0 ? req.maxTokens : 4000, 4000);
    req.turns.clear();
    app::ai::ProviderTurn turn;
    turn.role = app::ai::ProviderTurn::Role::User;
    turn.text = request;
    req.turns.push_back(std::move(turn));
    std::string w;
    if (!prov->start(std::move(req), &w)) {
        fail(w.empty() ? std::string("le fournisseur refuse la demande") : w);
        return;
    }
    provider_ = std::move(prov);
    current_ = std::move(job);
    answer_.clear();
    analysisStarted_ = now_;
    postEvent("Analyse IA lanc\xC3\xA9" "e en arri\xC3\xA8re-plan (" + currentSource_ + ")");
}

void IncidentHost::pollAnalysis() {
    bool done = false;
    std::string error;
    for (auto& e : provider_->poll()) {
        switch (e.type) {
            case app::ai::ProviderEvent::Type::Text: answer_ += e.text; break;
            case app::ai::ProviderEvent::Type::Done:
                done = true;
                if (e.stopReason == "refusal") error = "le mod\xC3\xA8le a refus\xC3\xA9 de r\xC3\xA9pondre";
                break;
            case app::ai::ProviderEvent::Type::Error:
                done = true;
                error = e.text.empty() ? std::string("erreur du fournisseur") : e.text;
                break;
            default: break;
        }
        if (done) break;
    }
    if (!done && now_ - analysisStarted_ > kAnalysisTimeout) {
        provider_->cancel();
        done = true;
        error = "pas de r\xC3\xA9ponse en " + std::to_string(static_cast<int>(kAnalysisTimeout)) + " s";
    }
    if (!done) return;
    if (error.empty() && answer_.empty()) error = "r\xC3\xA9ponse vide";
    st::Analysis a;
    a.kind = "complementaire";
    a.createdAt = st::nowStamp();
    a.source = currentSource_;
    a.contextHash = currentHash_;
    if (error.empty()) {
        a.state = "faite";
        a.text = answer_;
        a.certainty = st::certaintyOf(answer_);
    } else {
        a.state = "echec";
        a.error = error;
    }
    provider_.reset();
    auto store = store_;
    const std::string folder = current_ ? current_->folder : std::string{};
    current_.reset();
    answer_.clear();
    if (!store || folder.empty()) return;
    post([this, store, folder, a]() mutable {
        std::string w;
        if (!store->addAnalysis(folder, a, &w)) postEvent("Analyse non enregistr\xC3\xA9" "e : " + w);
        else if (a.state == "faite") postEvent("Analyse IA ajout\xC3\xA9" "e (" + a.source + ") : " + U(P(folder).filename()));
        else postEvent("Analyse en attente (" + a.error + ") : l'incident est gard\xC3\xA9, relance-la plus tard");
        relist(store);
    });
}

} // namespace app
