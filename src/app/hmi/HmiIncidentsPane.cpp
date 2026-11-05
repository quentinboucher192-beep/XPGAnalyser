// app/hmi/HmiIncidentsPane.cpp - 1.20.0 : IHM > Ressources > Incidents (voir l'en-tete).
#include "HmiIncidentsPane.hpp"

#include "HmiPaneKit.hpp"
#include "../FilePreview.hpp"
#include "../../hmi/HmiRecording.hpp"
#include "../../hmi/HmiVideo.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/Shapes.hpp"      // 1.21.0 : les triangles du lecteur (comme la barre de relecture)
#include "../../ui/TextSearch.hpp"   // 1.20.2 : les alarmes pliees (dossiers, tri)
#include "../../ui/widgets/PropertyGridHooks.hpp"   // 1.24.1 : le dossier d'un incident (Ouvrir dans l'explorateur, Aller a)

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <utility>

namespace app {

namespace st = ai::incidents;
namespace inc = hmi::incident;

// ================================================================================================ incidentsui ==
namespace incidentsui {
namespace {

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// "### 7. Analyse technique" -> 7 ; une autre ligne -> 0.
int headerNumber(std::string_view line) {
    if (!startsWith(line, "### ")) return 0;
    std::size_t i = 4;
    int n = 0;
    while (i < line.size() && i < 7 && std::isdigit(static_cast<unsigned char>(line[i]))) n = n * 10 + (line[i++] - '0');
    return i < line.size() && line[i] == '.' && n >= 1 && n <= 10 ? n : 0;
}

// 2.5 -> "2,5" ; 10 -> "10".
std::string number(double v) {
    char buf[32];
    if (std::fabs(v - std::round(v)) < 1e-9) std::snprintf(buf, sizeof buf, "%.0f", v);
    else std::snprintf(buf, sizeof buf, "%.2f", v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

bool parseNumber(const std::string& raw, double& out) {
    std::string s = trim(raw);
    std::replace(s.begin(), s.end(), ',', '.');
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

// "Activee", "Oui", "vrai"... -> vrai ; "Desactivee", "Non"... -> faux ; autre chose : rien.
std::optional<bool> yesNo(const std::string& raw) {
    const std::string v = hmikit::lower(trim(raw));
    if (v == "oui" || v == "vrai" || v == "true" || v == "1" || v == "yes" || startsWith(v, "activ")) return true;
    if (v == "non" || v == "faux" || v == "false" || v == "0" || v == "no" || startsWith(v, "d\xC3\xA9sactiv") || startsWith(v, "desactiv")
        || startsWith(v, "d\xC3\x89sactiv"))
        return false;
    return std::nullopt;
}

std::string seconds(double v) { return number(v) + " s"; }

} // namespace

std::vector<Section> splitDiagnosis(std::string_view text, std::string* title) {
    struct Line {
        std::size_t begin, end, next;
    };
    std::vector<Line> lines;
    for (std::size_t pos = 0; pos < text.size();) {
        std::size_t e = text.find('\n', pos);
        const std::size_t next = e == std::string_view::npos ? text.size() : e + 1;
        if (e == std::string_view::npos) e = text.size();
        std::size_t end = e;
        if (end > pos && text[end - 1] == '\r') --end;
        lines.push_back({pos, end, next});
        pos = next;
    }
    const auto lineAt = [&](std::size_t i) { return text.substr(lines[i].begin, lines[i].end - lines[i].begin); };
    if (title) {
        title->clear();
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (startsWith(lineAt(i), "## ")) {
                *title = trim(lineAt(i).substr(3));
                break;
            }
    }
    // Les sections 1 a 7 dans l'ordre, depuis le debut ; 8 a 10 depuis la fin (une analyse jointe en 7 peut ecrire ses
    // propres titres "### 8." : les vrais sont apres elle).
    std::array<long, 12> at{};
    at.fill(-1);
    long from = -1;
    for (int n = 1; n <= 7; ++n)
        for (long i = from + 1; i < static_cast<long>(lines.size()); ++i)
            if (headerNumber(lineAt(static_cast<std::size_t>(i))) == n) {
                at[static_cast<std::size_t>(n)] = i;
                from = i;
                break;
            }
    long upto = static_cast<long>(lines.size());
    for (int n = 10; n >= 8; --n)
        for (long i = upto - 1; i > from; --i)
            if (headerNumber(lineAt(static_cast<std::size_t>(i))) == n) {
                at[static_cast<std::size_t>(n)] = i;
                upto = i;
                break;
            }
    std::vector<long> found;
    for (int n = 1; n <= 10; ++n)
        if (at[static_cast<std::size_t>(n)] >= 0) found.push_back(at[static_cast<std::size_t>(n)]);
    std::vector<Section> out;
    for (std::size_t k = 0; k < found.size(); ++k) {
        const auto start = static_cast<std::size_t>(found[k]);
        const std::size_t stop = k + 1 < found.size() ? static_cast<std::size_t>(found[k + 1]) : lines.size();
        Section s;
        s.title = trim(lineAt(start).substr(4));
        const std::size_t b = lines[start].next;
        const std::size_t e = stop < lines.size() ? lines[stop].begin : text.size();
        s.body = std::string(text.substr(b, e > b ? e - b : 0));
        while (!s.body.empty() && (s.body.back() == '\n' || s.body.back() == '\r' || s.body.back() == ' ')) s.body.pop_back();
        out.push_back(std::move(s));
    }
    if (out.empty() && !trim(text).empty()) out.push_back({"Fiche", std::string(text)});
    return out;
}

std::string listText(const std::vector<std::string>& names) { return hmikit::joinList(names, ", "); }

std::vector<std::string> listOf(std::string_view text) {
    std::vector<std::string> out;
    for (auto& n : hmikit::splitList(std::string(text))) {
        std::string t = trim(n);
        if (!t.empty()) out.push_back(std::move(t));
    }
    return out;
}

std::vector<std::string> settingsLabels() {
    return {"Surveillance",
            "Alarmes surveill\xC3\xA9" "es",
            "Alarmes ignor\xC3\xA9" "es",
            "D\xC3\xA9" "fauts de communication",
            "Secondes avant le d\xC3\xA9" "faut",
            "Secondes apr\xC3\xA8s le d\xC3\xA9" "faut",
            "Images par seconde",
            "Maintien signal\xC3\xA9 apr\xC3\xA8s (s)",
            "Anti-rebond (s)",
            "Regroupement des r\xC3\xA9" "apparitions (s)",       // 1.21.0
            "Nouveaux incidents par heure, au plus",
            "Quota des images et vid\xC3\xA9os (Mo)"};
}

std::vector<std::string> settingsValues(const inc::Settings& s) {
    return {s.enabled ? "Activ\xC3\xA9" "e" : "D\xC3\xA9sactiv\xC3\xA9" "e",
            listText(s.watched),
            listText(s.ignored),
            s.communication ? "Oui" : "Non",
            number(s.before),
            number(s.after),
            std::to_string(s.framesPerSecond),
            number(s.heldAfter),
            number(s.debounce),
            number(s.regroup),                       // 1.21.0
            std::to_string(s.maxPerHour),
            std::to_string(s.mediaQuotaMb)};
}

bool settingsFrom(const std::vector<std::string>& v, inc::Settings& io, std::string* why, std::size_t* field) {
    const auto fail = [&](std::size_t f, std::string w) {
        if (why) *why = std::move(w);
        if (field) *field = f;
        return false;
    };
    if (v.size() < kSettingsFields) return fail(0, "il manque des champs");
    inc::Settings s = io;
    const auto on = yesNo(v[0]);
    if (!on) return fail(0, "Activ\xC3\xA9" "e ou D\xC3\xA9sactiv\xC3\xA9" "e");
    s.enabled = *on;
    s.watched = listOf(v[1]);
    s.ignored = listOf(v[2]);
    const auto comm = yesNo(v[3]);
    if (!comm) return fail(3, "Oui ou Non");
    s.communication = *comm;
    const auto range = [&](std::size_t f, double lo, double hi, double& out, const char* what) {
        double x = 0;
        if (!parseNumber(v[f], x)) return fail(f, std::string(what) + " : un nombre (" + number(lo) + " \xC3\xA0 " + number(hi) + ")");
        if (x < lo || x > hi) return fail(f, std::string(what) + " : de " + number(lo) + " \xC3\xA0 " + number(hi));
        out = x;
        return true;
    };
    double fps = 0;
    if (!range(4, 0, 120, s.before, "secondes avant")) return false;
    if (!range(5, 0, 120, s.after, "secondes apr\xC3\xA8s")) return false;
    if (!range(6, 1, 10, fps, "images par seconde")) return false;
    if (std::fabs(fps - std::round(fps)) > 1e-9) return fail(6, "images par seconde : un nombre entier (1 \xC3\xA0 10)");
    s.framesPerSecond = static_cast<int>(std::lround(fps));
    if (!range(7, 0, 86400, s.heldAfter, "maintien")) return false;
    if (!range(8, 0, 3600, s.debounce, "anti-rebond")) return false;
    // 1.21.0 : le regroupement des reapparitions, les garde-fous (des nombres entiers pour les deux derniers)
    if (!range(9, 0, 86400, s.regroup, "regroupement")) return false;
    double perHour = 0, quota = 0;
    if (!range(10, 0, 10000, perHour, "nouveaux incidents par heure")) return false;
    if (std::fabs(perHour - std::round(perHour)) > 1e-9) return fail(10, "nouveaux incidents par heure : un nombre entier (0 : sans plafond)");
    if (!range(11, 0, 1048576, quota, "quota des images et vid\xC3\xA9os")) return false;
    if (std::fabs(quota - std::round(quota)) > 1e-9) return fail(11, "quota des images et vid\xC3\xA9os : un nombre entier de Mo (0 : sans quota)");
    s.maxPerHour = static_cast<std::size_t>(std::llround(perHour));
    s.mediaQuotaMb = static_cast<std::size_t>(std::llround(quota));
    io = std::move(s);
    return true;
}

std::string videoLabel(const st::Item& it) {
    if (!it.hasVideo) return "pas de vid\xC3\xA9o" + (it.videoNote.empty() ? std::string{} : " (" + it.videoNote + ")");
    std::string t = "vid\xC3\xA9o Motion-JPEG de " + std::to_string(it.videoFrames) + (it.videoFrames > 1 ? " images" : " image");
    t += " (" + seconds(it.videoSeconds);
    if (it.videoFps > 0) t += ", " + number(it.videoFps) + " images par seconde";
    return t + ")";
}

std::vector<std::string> integrityLines(const st::Integrity& r) {
    std::vector<std::string> out;
    if (r.ok) {
        out.push_back("intact : " + std::to_string(r.checked) + (r.checked > 1 ? " fichiers v\xC3\xA9rifi\xC3\xA9s" : " fichier v\xC3\xA9rifi\xC3\xA9"));
        return out;
    }
    if (!r.why.empty()) {
        out.push_back("non v\xC3\xA9rifiable : " + r.why);
        return out;
    }
    out.push_back("pas intact : " + std::to_string(r.missing.size()) + " manquant(s), " + std::to_string(r.altered.size()) + " modifi\xC3\xA9(s), "
                  + std::to_string(r.extra.size()) + " en trop (" + std::to_string(r.checked) + " lu(s))");
    for (const auto& f : r.missing) out.push_back("manquant : " + f);
    for (const auto& f : r.altered) out.push_back("modifi\xC3\xA9 : " + f);
    for (const auto& f : r.extra) out.push_back("en trop : " + f);
    return out;
}

std::string dateText(std::string_view s) {
    // "2026-10-31 17:40:12.350"
    if (s.size() < 16 || s[4] != '-' || s[7] != '-') return std::string(s);
    std::string t = std::string(s.substr(8, 2)) + "/" + std::string(s.substr(5, 2)) + "/" + std::string(s.substr(0, 4));
    std::string_view time = s.substr(11);
    if (const auto dot = time.find('.'); dot != std::string_view::npos) time = time.substr(0, dot);
    return t + " " + std::string(time);
}

// 1.20.2 : l'alarme, la criticite, l'horodatage d'un incident (la liste, les dossiers, la recherche, le tri).
std::string alarmOf(const st::Item& it) { return it.snapshot.alarm.name.empty() ? it.snapshot.trigger.name : it.snapshot.alarm.name; }

int criticalityOf(const st::Item& it) {
    if (!it.readable) return 0;
    // comme le contexte (hmi::incident::snapshot) : la priorite de l'alarme du projet, sinon celle de l'avis
    const int p = it.snapshot.alarm.known ? it.snapshot.alarm.priority : it.snapshot.trigger.priority;
    return p >= 1 && p <= hmi::kAlarmPriorities ? p : 0;
}

std::string stampOf(const st::Item& it) { return it.snapshot.createdAt.empty() ? it.snapshot.trigger.at : it.snapshot.createdAt; }

std::string validationLabel(const st::Item& it) {
    if (it.state == st::State::Pending) return "En attente";
    if (!it.validation) return "Valid\xC3\xA9";
    return "Valid\xC3\xA9 le " + dateText(it.validation->at) + (it.validation->author.empty() ? std::string{} : " par " + it.validation->author);
}

Target targetOf(const hmi::Project* p, const inc::Link& l) {
    Target t;
    t.kind = l.kind;
    t.name = l.name;
    if (!p) return t;
    const auto rootOf = [](std::string_view path) {
        const auto cut = path.find_first_of(".[");
        return trim(cut == std::string_view::npos ? path : path.substr(0, cut));
    };
    if (l.kind == "variable" || l.kind == "adresse") {
        std::string_view n = l.name;
        if (l.kind == "adresse")
            if (const auto at = n.find(" @ "); at != std::string_view::npos) n = n.substr(0, at);
        t.kind = "variable";
        t.name = rootOf(n);
        t.exists = p->variable(t.name) != nullptr;
    } else if (l.kind == "structure" || l.kind == "type") {
        t.kind = "type";
        t.exists = p->hmiTypeByName(l.name) != nullptr;
    } else if (l.kind == "equipement" || l.kind == "\xC3\xA9quipement") {
        t.kind = "equipement";
        t.exists = p->equipmentByName(l.name) != nullptr;
    } else if (l.kind == "script") {
        if (p->generalScript(l.name)) {
            t.exists = true;
        } else if (const auto dot = l.name.find('.'); dot != std::string::npos) {
            // un script de vue : "Vue_Station.Init" (son evenement ou son nom)
            t.view = l.name.substr(0, dot);
            const std::string rest = l.name.substr(dot + 1);
            if (const auto* v = p->viewByName(t.view))
                for (const auto& sc : v->scripts)
                    if (hmikit::same(sc.event, rest) || hmikit::same(sc.name, rest)) t.exists = true;
        }
    } else if (l.kind == "fonction") {
        if (p->functionByName(l.name)) {
            t.exists = true;
        } else if (const auto dot = l.name.rfind('.'); dot != std::string::npos) {
            const std::string owner = l.name.substr(0, dot), rest = l.name.substr(dot + 1);
            if (const auto* v = p->viewByName(owner)) {                       // une fonction de vue
                t.view = owner;
                for (const auto& f : v->functions)
                    if (hmikit::same(f.name, rest)) t.exists = true;
            } else if (p->hmiTypeByName(owner)) {                             // une methode d'un type
                t.kind = "type";
                t.name = owner;
                t.exists = true;
            }
        }
    } else if (l.kind == "vue" || l.kind == "symbole" || l.kind == "popup") {
        t.exists = p->viewByName(l.name) != nullptr;
    } else if (l.kind == "objet") {
        // "Pompe_1 (Vue_Station)"
        std::string obj = l.name;
        if (const auto open = obj.rfind(" ("); open != std::string::npos && obj.back() == ')') {
            t.view = obj.substr(open + 2, obj.size() - open - 3);
            obj = obj.substr(0, open);
        }
        t.name = obj;
        if (const auto* v = t.view.empty() ? nullptr : p->viewByName(t.view))
            for (const auto& o : v->objects)
                if (o.name == obj) t.exists = true;
    } else if (l.kind == "alarme") {
        t.exists = p->alarmByName(l.name) != nullptr;
    } else if (l.kind == "ressource") {
        t.exists = p->resourceByName(l.name) != nullptr;
    }
    return t;
}

// ---- 1.21.0 : le lecteur de la video ----
void VideoClock::start(double d, double f, double t) {
    duration = std::isfinite(d) ? std::max(0.0, d) : 0.0;
    fps = std::isfinite(f) ? std::max(0.0, f) : 0.0;
    trigger = std::isfinite(t) ? t : -1.0;
    position = 0;
    playing = true;
    ended = false;
    setSpeed(speed);
}

void VideoClock::advance(double dt) {
    if (!playing || !(dt > 0) || !std::isfinite(dt)) return;
    position += dt * speed;
    if (duration <= 0 || position < duration) return;
    if (loop) {
        position = std::fmod(position, duration);
    } else {
        position = duration;   // la derniere image reste
        playing = false;
        ended = true;
    }
}

void VideoClock::playPause() {
    if (playing) {
        playing = false;
        return;
    }
    if (ended || (duration > 0 && position >= duration - 1e-6)) position = 0;   // au bout : Lecture repart du debut
    ended = false;
    playing = true;
}

void VideoClock::seek(double seconds) {
    if (!std::isfinite(seconds)) return;
    position = std::max(0.0, seconds);
    if (duration > 0) position = std::min(position, duration);
    ended = !loop && duration > 0 && position >= duration - 1e-6;
    if (ended) playing = false;
}

void VideoClock::step(double seconds) { seek(position + seconds); }

void VideoClock::stepFrame(int n) {
    playing = false;
    if (fps <= 0) {
        seek(position + 0.1 * n);
        return;
    }
    const int count = frames();
    int k = frame() + n;
    k = count > 0 ? std::clamp(k, 0, count - 1) : std::max(0, k);
    seek(static_cast<double>(k) / fps);
}

void VideoClock::toTrigger() {
    if (hasTrigger()) seek(trigger);
}

void VideoClock::faster() {
    for (const double v : hmi::rec::kSpeeds)
        if (v > speed + 1e-9) {
            speed = v;
            return;
        }
}

void VideoClock::slower() {
    for (auto it = std::rbegin(hmi::rec::kSpeeds); it != std::rend(hmi::rec::kSpeeds); ++it)
        if (*it < speed - 1e-9) {
            speed = *it;
            return;
        }
}

void VideoClock::setSpeed(double v) {
    if (!(v > 0) || !std::isfinite(v)) v = 1.0;
    double best = 1.0, gap = 1e300;
    for (const double s : hmi::rec::kSpeeds)
        if (const double g = std::fabs(std::log(v / s)); g < gap) {
            gap = g;
            best = s;
        }
    speed = best;
}

int VideoClock::frames() const { return duration > 0 && fps > 0 ? static_cast<int>(std::lround(duration * fps)) : 0; }

int VideoClock::frame() const {
    if (fps <= 0) return 0;
    const int k = static_cast<int>(std::floor(std::max(0.0, position) * fps + 1e-6));   // comme MjpegSource::frameAt
    const int count = frames();
    return count > 0 ? std::min(k, count - 1) : k;
}

double triggerPosition(const std::vector<double>& times, double fps, double before, double duration) {
    double t = -1.0;
    if (fps > 0 && !times.empty()) {
        // la premiere image au declenchement ou apres ; entre elle et la precedente, au prorata de leurs instants (une
        // cadence juste - depuis la 1.20.2 - donne exactement -instants[0])
        std::size_t k = 0;
        while (k < times.size() && times[k] < 0) ++k;
        if (k == 0) {
            t = times[0] <= 1.0 / fps + 1e-9 ? 0.0 : -1.0;   // la video commence apres le declenchement
        } else if (k == times.size()) {
            t = static_cast<double>(times.size() - 1) / fps - times.back();   // il vient apres la derniere image
            if (duration > 0 && t > duration + 1.0 / fps) t = -1.0;
        } else {
            const double a = times[k - 1], b = times[k];
            const double frac = b > a ? std::clamp(-a / (b - a), 0.0, 1.0) : 1.0;
            t = (static_cast<double>(k - 1) + frac) / fps;
        }
    } else if (before > 0 && std::isfinite(before)) {
        t = duration > 0 && before > duration + 1e-6 ? -1.0 : before;
    }
    if (t >= 0 && duration > 0) t = std::min(t, duration);
    return t;
}

std::string clockTenths(double seconds) {
    const long long d = std::llround(std::max(0.0, std::isfinite(seconds) ? seconds : 0.0) * 10.0);
    const long long s = d / 10;
    char buf[40];
    if (s >= 3600) std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld,%lld", s / 3600, (s / 60) % 60, s % 60, d % 10);
    else std::snprintf(buf, sizeof buf, "%02lld:%02lld,%lld", s / 60, s % 60, d % 10);
    return buf;
}

std::string signedSeconds(double seconds) {
    const long long d = std::llround((std::isfinite(seconds) ? seconds : 0.0) * 10.0);
    const long long a = d < 0 ? -d : d;
    const std::string sign = d > 0 ? "+" : d < 0 ? "\xE2\x88\x92" : "";
    return sign + std::to_string(a / 10) + "," + std::to_string(a % 10) + " s";
}

std::string speedLabel(double speed) { return "\xC3\x97" + number(speed); }

gfx::Rect ImageView::place(const gfx::Rect& box, int w, int h) const {
    if (w <= 0 || h <= 0 || box.w <= 0.f || box.h <= 0.f) return box;
    const float s = std::min(box.w / static_cast<float>(w), box.h / static_cast<float>(h)) * zoom;
    const float dw = std::floor(static_cast<float>(w) * s), dh = std::floor(static_cast<float>(h) * s);
    return {std::floor(box.x + (box.w - dw) * 0.5f + pan.x), std::floor(box.y + (box.h - dh) * 0.5f + pan.y), dw, dh};
}

void ImageView::clamp(const gfx::Rect& box, int w, int h) {
    zoom = std::isfinite(zoom) ? std::clamp(zoom, kMin, kMax) : kMin;
    if (!zoomed()) {
        fit();
        return;
    }
    const gfx::Point keep = pan;
    pan = {0.f, 0.f};
    const gfx::Rect r = place(box, w, h);
    const float mx = std::max(0.f, (r.w - box.w) * 0.5f), my = std::max(0.f, (r.h - box.h) * 0.5f);
    pan = {std::clamp(std::isfinite(keep.x) ? keep.x : 0.f, -mx, mx), std::clamp(std::isfinite(keep.y) ? keep.y : 0.f, -my, my)};
}

void ImageView::zoomAt(float z, const gfx::Rect& box, int w, int h, gfx::Point at) {
    const gfx::Rect before = place(box, w, h);
    zoom = std::isfinite(z) ? std::clamp(z, kMin, kMax) : kMin;
    if (before.w <= 0.f || before.h <= 0.f || !zoomed()) {
        clamp(box, w, h);
        return;
    }
    // le point de l'image sous la souris, en fraction de l'image : il y reste
    const float u = (at.x - before.x) / before.w, v = (at.y - before.y) / before.h;
    pan = {0.f, 0.f};
    const gfx::Rect centered = place(box, w, h);
    pan = {at.x - (centered.x + u * centered.w), at.y - (centered.y + v * centered.h)};
    clamp(box, w, h);
}

void ImageView::zoomBy(float factor, const gfx::Rect& box, int w, int h) {
    zoomAt(zoom * factor, box, w, h, {box.x + box.w * 0.5f, box.y + box.h * 0.5f});
}

void ImageView::drag(gfx::Point delta, const gfx::Rect& box, int w, int h) {
    pan = {pan.x + delta.x, pan.y + delta.y};
    clamp(box, w, h);
}

} // namespace incidentsui

namespace {

namespace ui_ = incidentsui;

// ============================================================================================= les images ==
//  Une capture se decode UNE fois, sur un fil a part (app::preview::thumbnail : lue, decodee, ramenee a `side`), puis
//  devient une texture sur le fil de l'interface. Gardees par (taille, chemin) - une capture ne change jamais (le
//  dossier est immuable) ; les plus anciennes rendues au renderer au-dela de 80.
class Pictures {
public:
    struct Pic {
        gfx::TextureId tex{};
        int            w{0}, h{0};
        bool           ready{false}, failed{false};
    };
    static Pictures& instance() {
        static Pictures p;
        return p;
    }
    // Non bloquant : pas encore pret, la prochaine image le redemande.
    Pic get(gfx::IRenderer& r, const std::string& path, int side) {
        if (path.empty()) {
            Pic none;
            none.failed = true;
            return none;
        }
        if (renderer_ != &r) {                       // un autre renderer : ses textures ne sont pas les notres
            renderer_ = &r;
            for (auto it = entries_.begin(); it != entries_.end();)
                it = it->second.job.valid() ? std::next(it) : entries_.erase(it);
            for (auto& [k, e] : entries_) e.pic = {};
        }
        const std::string key = std::to_string(side) + "|" + path;
        auto& e = entries_[key];
        e.used = ++tick_;
        if (e.pic.ready || e.pic.failed) return e.pic;
        if (!e.job.valid()) {
            if (inFlight() >= 1) return e.pic;      // une a la fois : son tour viendra
            e.job = std::async(std::launch::async, [path, side] { return preview::thumbnail(path, side); });
            return e.pic;
        }
        if (e.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return e.pic;
        const auto img = e.job.get();
        if (!img || img->width <= 0 || img->height <= 0) {
            e.pic.failed = true;
        } else {
            e.pic.tex = r.createImage(img->rgba.data(), img->width, img->height);
            e.pic.w = static_cast<int>(img->fileWidth ? img->fileWidth : static_cast<std::uint32_t>(img->width));
            e.pic.h = static_cast<int>(img->fileHeight ? img->fileHeight : static_cast<std::uint32_t>(img->height));
            e.pic.ready = true;
            if (!e.pic.tex.v) e.pic.failed = true;   // un renderer sans images (les essais) : le cadre
        }
        const Pic out = e.pic;
        purge(r);
        return out;
    }
    [[nodiscard]] bool busy() const {
        for (const auto& [k, e] : entries_)
            if (e.job.valid()) return true;
        return false;
    }

private:
    struct Entry {
        std::future<std::shared_ptr<const ui::PreviewImage>> job;
        Pic                                                  pic;
        std::uint64_t                                        used{0};
    };
    [[nodiscard]] std::size_t inFlight() const {
        std::size_t n = 0;
        for (const auto& [k, e] : entries_) n += e.job.valid() ? 1u : 0u;
        return n;
    }
    void purge(gfx::IRenderer& r) {
        if (entries_.size() <= 80) return;
        std::vector<std::pair<std::uint64_t, std::string>> byAge;
        for (const auto& [k, e] : entries_)
            if (!e.job.valid()) byAge.emplace_back(e.used, k);
        std::sort(byAge.begin(), byAge.end());
        for (std::size_t i = 0; i < byAge.size() && entries_.size() > 60; ++i) {
            auto it = entries_.find(byAge[i].second);
            if (it->second.pic.tex.v) r.releaseImage(it->second.pic.tex);
            entries_.erase(it);
        }
    }
    gfx::IRenderer*              renderer_{nullptr};
    std::map<std::string, Entry> entries_;
    std::uint64_t                tick_{0};
};

// Une image de w x h dans `box`, a ses proportions, centree.
gfx::Rect fitInto(const gfx::Rect& box, int w, int h) {
    if (w <= 0 || h <= 0) return box;
    const float s = std::min(box.w / static_cast<float>(w), box.h / static_cast<float>(h));
    const float dw = std::floor(static_cast<float>(w) * s), dh = std::floor(static_cast<float>(h) * s);
    return {std::floor(box.x + (box.w - dw) * 0.5f), std::floor(box.y + (box.h - dh) * 0.5f), dw, dh};
}

std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

// ============================================================================================== le texte ==
const char* const kEllipsis = "\xE2\x80\xA6";

std::size_t utf8Step(std::string_view s, std::size_t i) {
    std::size_t n = 1;
    while (i + n < s.size() && (static_cast<unsigned char>(s[i + n]) & 0xC0) == 0x80) ++n;
    return n;
}

std::string ellipsize(const gfx::IRenderer& r, std::string_view s, gfx::FontId f, float width) {
    if (width <= 0.f) return {};
    if (r.measure(s, f).width <= width) return std::string(s);
    const float ew = r.measure(kEllipsis, f).width;
    const std::size_t n = r.fitCharacters(s, f, std::max(0.f, width - ew));
    return std::string(s.substr(0, n)) + kEllipsis;
}

// Couper en lignes de `width` (la premiere : `first`, quand elle commence apres autre chose).
std::vector<std::string> wrapText(const gfx::IRenderer& r, std::string_view text, gfx::FontId f, float width, float first = -1.f) {
    std::vector<std::string> out;
    width = std::max(width, 24.f);
    const auto room = [&] { return out.empty() && first >= 0.f ? std::max(first, 24.f) : width; };
    std::string line;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t j = i;
        while (j < text.size() && text[j] == ' ') ++j;
        if (j >= text.size()) break;
        std::size_t k = j;
        while (k < text.size() && text[k] != ' ') ++k;
        const std::string_view word = text.substr(j, k - j);
        std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
        if (r.measure(candidate, f).width <= room()) {
            line = std::move(candidate);
            i = k;
            continue;
        }
        if (!line.empty()) {                  // le mot passe a la ligne suivante
            out.push_back(std::move(line));
            line.clear();
            continue;
        }
        // un mot plus long que la ligne : coupe la ou il deborde
        std::size_t n = r.fitCharacters(word, f, room());
        if (n == 0) n = utf8Step(word, 0);
        out.emplace_back(word.substr(0, n));
        i = j + n;
    }
    if (!line.empty()) out.push_back(std::move(line));
    if (out.empty()) out.emplace_back();
    return out;
}

enum class Tone : std::uint8_t { Text, Muted, Accent, Ok, Warning, Error, Info, Band, Field, Border, Window, Selection };

gfx::Color colorOf(const ui::Theme& th, Tone t) {
    const auto& c = th.color;
    switch (t) {
        case Tone::Text:      return c.text;
        case Tone::Muted:     return c.textMuted;
        case Tone::Accent:    return c.accent;
        case Tone::Ok:        return c.ok;
        case Tone::Warning:   return c.warning;
        case Tone::Error:     return c.error;
        case Tone::Info:      return c.info;
        case Tone::Band:      return c.headerBg;
        case Tone::Field:     return c.inputBg;
        case Tone::Border:    return c.border;
        case Tone::Window:    return c.windowBg;
        case Tone::Selection: return c.selectionBg;
    }
    return c.text;
}

// La couleur d'une certitude ("fait verifie", "relation indirecte", "hypothese") ou d'un etat d'analyse.
Tone toneOfWords(std::string_view s) {
    const std::string t = hmikit::lower(std::string(s));
    if (t.find("\xC3\xA9" "chec") != std::string::npos || t.find("echec") != std::string::npos) return Tone::Warning;
    if (t.find("fait") != std::string::npos) return Tone::Ok;
    if (t.find("indirect") != std::string::npos) return Tone::Info;
    if (t.find("hypoth") != std::string::npos) return Tone::Warning;
    return Tone::Muted;
}

// 1.20.2 : la couleur d'une criticite, comme la colonne Priorite des alarmes (1 Critique : erreur, 2 Haute :
// avertissement, 3 Moyenne : accent, 4 Basse et inconnue : grise) ; son nom ("Critique" ; 0 : "criticit\xC3\xA9 inconnue").
Tone toneOfCriticality(int p) {
    switch (p) {
        case 1:  return Tone::Error;
        case 2:  return Tone::Warning;
        case 3:  return Tone::Accent;
        default: return Tone::Muted;
    }
}
std::string criticalityLabel(int p) {
    return p >= 1 && p <= hmi::kAlarmPriorities ? std::string(hmi::alarmPriorityLabel(p)) : std::string("criticit\xC3\xA9 inconnue");
}
std::string incidentsWord(std::size_t n) { return std::to_string(n) + (n > 1 ? " incidents" : " incident"); }

// 1.21.0 : LES REAPPARITIONS d'un incident - la rafale en cours (l'hote, a jour a chaque reapparition), sinon celle
// relue du dossier (reapparitions.json) ; nulle : aucune.
const inc::Burst* burstOf(const IncidentHost* host, const st::Item& it) {
    if (host)
        if (const inc::Burst* b = host->reappearances(it.snapshot.id)) return b;
    return it.reappearances ? &*it.reappearances : nullptr;
}
std::uint64_t reappearancesOf(const IncidentHost* host, const st::Item& it) {
    const inc::Burst* b = burstOf(host, it);
    return b ? b->count : 0;
}
// "\xC3\x97" "327" : le badge d'une ligne ; "327 r\xC3\xA9" "apparitions" : le texte.
std::string badgeText(std::uint64_t n) { return "\xC3\x97" + inc::countText(n); }
std::string reappearancesWord(std::uint64_t n) { return inc::countText(n) + (n > 1 ? " r\xC3\xA9" "apparitions" : " r\xC3\xA9" "apparition"); }

} // namespace

// ============================================================================================== la liste ==
//  Une ligne par incident : la miniature, le nom et la date, l'alarme, l'equipement et la vue, la synthese, puis la
//  certitude, l'analyse et la validation. Dans un ScrollablePanel (la molette, la barre) ; seules les lignes visibles
//  se dessinent.
//  1.20.2 : les lignes sont celles du volet (rows_) - un incident, ou le DOSSIER d'une alarme (le chevron, l'icone,
//  l'alarme, le nombre d'incidents, la criticite, la date du plus recent et sa miniature sur une pile) ; deplie, ses
//  incidents en retrait. Toutes de la meme hauteur : rowAt et reveal restent une division.
class HmiIncidentList final : public ui::Widget {
public:
    static constexpr float kRowH = 92.f, kThumbW = 136.f, kThumbH = 76.f;
    static constexpr float kIndent = 22.f;   // 1.20.2 : le retrait d'un incident dans un dossier deplie

    HmiIncidentList(HmiIncidentsPane& pane, int tab) : ui::Widget(pane.id() + (tab ? ".valides" : ".attente")), pane_(pane), tab_(tab) {
        setFocusPolicy(true);
    }
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        const std::size_t n = pane_.rows_[tab_].size();   // 1.20.2 : les lignes affichees
        h.preferred = {0.f, static_cast<float>(std::max<std::size_t>(n, 1)) * kRowH + 8.f};
        h.stretchX = h.stretchY = 1.f;
        return h;
    }
    void reveal(int row) {
        if (row < 0) return;
        if (auto* sp = dynamic_cast<ui::ScrollablePanel*>(parent())) sp->ensureVisible({0.f, static_cast<float>(row) * kRowH, 10.f, kRowH});
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const auto b = bounds();
        ctx.r.fillRect(b, c.panelBg);
        const auto* items = pane_.items_[tab_];
        const auto& rows = pane_.rows_[tab_];
        if (!items || items->empty() || rows.empty()) {
            const std::string text = items && !items->empty()
                                       // 1.20.2 : tout est cache par la recherche ou la criticite
                                       ? "Aucun incident ne correspond \xC3\xA0 la recherche ou \xC3\xA0 la criticit\xC3\xA9 choisie ("
                                             + incidentsWord(items->size()) + " dans cet onglet)."
                                   : !pane_.host_ ? std::string("La surveillance des incidents n'est pas branch\xC3\xA9" "e ici.")
                                   : pane_.host_->projectFolder().empty()
                                       ? std::string("Le projet n'est pas encore enregistr\xC3\xA9 : pas de dossier d'incident. Enregistre-le, "
                                                     "puis les alarmes en feront ici.")
                                   : !pane_.listing_ ? std::string("Lecture des dossiers d'incident\xE2\x80\xA6")
                                   : tab_ == 0
                                       ? std::string("Aucun incident en attente. Quand une alarme appara\xC3\xAEt en simulation IHM ou au poste "
                                                     "d'exploitation, son dossier se cr\xC3\xA9" "e ici tout seul (R\xC3\xA9glages\xE2\x80\xA6 : "
                                                     "lesquelles sont surveill\xC3\xA9" "es).")
                                       : std::string("Aucun incident valid\xC3\xA9. Valider copie un incident en attente ici, apr\xC3\xA8s "
                                                     "avoir v\xC3\xA9rifi\xC3\xA9 chacun de ses fichiers.");
            float y = b.y + 14.f;
            for (const auto& l : wrapText(ctx.r, text, th.font.ui, b.w - 28.f)) {
                ctx.r.drawText({b.x + 14.f, y}, l, th.font.ui, c.textMuted);
                y += ctx.r.lineHeight(th.font.ui) + 2.f;
            }
            return;
        }
        const auto clip = ctx.clip;
        const int n = static_cast<int>(rows.size());
        const int first = std::max(0, static_cast<int>((clip.y - b.y) / kRowH));
        const int last = std::min(n - 1, static_cast<int>((clip.bottom() - b.y) / kRowH));
        bool waiting = false;
        for (int i = first; i <= last; ++i) {
            const auto& row = rows[static_cast<std::size_t>(i)];
            if (row.item < 0 || static_cast<std::size_t>(row.item) >= items->size()) continue;
            waiting |= row.group ? paintGroup(ctx, *items, row, i) : paintRow(ctx, (*items)[static_cast<std::size_t>(row.item)], i, row.depth);
        }
        if (waiting) invalidate();   // une miniature se decode : on la redemande a la prochaine image
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        const auto& rows = pane_.rows_[tab_];   // 1.20.2 : les lignes affichees
        const int n = static_cast<int>(rows.size());
        const auto rowAt = [&](gfx::Point p) {
            const int r = static_cast<int>((p.y - bounds().y) / kRowH);
            return r >= 0 && r < n ? r : -1;
        };
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            const int h = bounds().contains(m->pos) ? rowAt(m->pos) : -1;
            if (h != hover_) {
                hover_ = h;
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            if (!bounds().contains(d->pos)) return ui::EventResult::Ignored;
            grabFocus();
            const int row = rowAt(d->pos);
            if (row < 0) return ui::EventResult::Consumed;
            const bool group = rows[static_cast<std::size_t>(row)].group;
            // 1.24.1 : Ctrl+clic ajoute la ligne au choix (ou l'en retire), Maj+clic choisit jusqu'a elle ; un clic droit
            // dans le choix le garde (le menu porte sur tous) ; un clic simple revient a un seul.
            if (d->button == ui::MouseButton::Left && d->mods.ctrl) {
                pane_.toggleRowChoice(static_cast<std::size_t>(row));
                return ui::EventResult::Consumed;
            }
            if (d->button == ui::MouseButton::Left && d->mods.shift) {
                pane_.extendChoiceTo(static_cast<std::size_t>(row));
                return ui::EventResult::Consumed;
            }
            if (d->button == ui::MouseButton::Right && pane_.chosenIncidents().size() > 1 && pane_.rowChosen(tab_, row)) {
                pane_.openContextMenu(toSurface(d->pos));
                return ui::EventResult::Consumed;
            }
            pane_.clearChoice();
            pane_.selectRow(tab_, row);
            if (d->button == ui::MouseButton::Right) {
                pane_.openContextMenu(toSurface(d->pos));
            } else if (d->button == ui::MouseButton::Left && d->clickCount >= 2) {
                pane_.consult();
            } else if (d->button == ui::MouseButton::Left && group) {
                pane_.toggleGroupAt(tab_, row);   // 1.20.2 : un clic sur un dossier le deplie ou le replie
            }
            return ui::EventResult::Consumed;
        }
        if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
            if (!focused() || n == 0) return ui::EventResult::Ignored;
            const int cur = pane_.row_[tab_];
            const HmiIncidentsPane::Row* at = cur >= 0 && cur < n ? &rows[static_cast<std::size_t>(cur)] : nullptr;
            const int page = std::max(1, static_cast<int>(parent() ? parent()->bounds().h / kRowH : 5.f) - 1);
            int to = -2;
            if (ui::isContextMenuKey(*k)) {
                const int row = std::max(0, cur);
                pane_.openContextMenu(toSurface(gfx::Point{bounds().x + 40.f, bounds().y + (static_cast<float>(row) + 0.6f) * kRowH}));
                return ui::EventResult::Consumed;
            }
            switch (k->key) {
                case ui::Key::Up:       to = std::max(0, cur - 1); break;
                case ui::Key::Down:     to = std::min(n - 1, cur + 1); break;
                case ui::Key::Home:     to = 0; break;
                case ui::Key::End:      to = n - 1; break;
                case ui::Key::PageUp:   to = std::max(0, cur - page); break;
                case ui::Key::PageDown: to = std::min(n - 1, cur + page); break;
                case ui::Key::Right:    // 1.20.2 : deplier le dossier choisi ; deja deplie : son premier incident
                    if (!at || !at->group) return ui::EventResult::Ignored;
                    if (!at->open) {
                        pane_.toggleGroupAt(tab_, cur);
                        return ui::EventResult::Consumed;
                    }
                    to = std::min(n - 1, cur + 1);
                    break;
                case ui::Key::Left: {   // 1.20.2 : replier le dossier choisi, ou celui de l'incident choisi
                    if (!at || (at->group ? !at->open : at->depth == 0)) return ui::EventResult::Ignored;
                    const std::string key = at->key;   // une copie : les lignes se refont
                    pane_.setGroupOpen(tab_, key, false);
                    return ui::EventResult::Consumed;
                }
                case ui::Key::Return:
                    if (at && at->group) pane_.toggleGroupAt(tab_, cur);   // 1.20.2 : Entree deplie ou replie un dossier
                    else pane_.consult();
                    return ui::EventResult::Consumed;
                case ui::Key::Delete:
                    if (tab_ == 0) pane_.askRemove();   // un dossier choisi : l'incident qu'il montre (le dialogue le nomme)
                    return ui::EventResult::Consumed;
                default: break;
            }
            if (to >= -1) {
                pane_.clearChoice();   // 1.24.1 : les fleches reviennent a un seul
                pane_.selectRow(tab_, std::max(0, to));
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

private:
    // 1.21.0 : le badge << x327 >> dont le bord droit est `right` ; rend sa largeur.
    static float paintBadge(const ui::PaintContext& ctx, std::uint64_t n, float right, float y) {
        const auto& th = ctx.theme;
        const auto small = th.font.smallUi;
        const std::string t = badgeText(n);
        const float tw = ctx.r.measure(t, small).width, h = ctx.r.lineHeight(small) + 4.f;
        const gfx::Rect box{right - tw - 12.f, y + 1.f, tw + 12.f, h};
        ctx.r.fillRoundedRect(box, th.color.warning, h * 0.5f);
        ctx.r.drawText({box.x + 6.f, box.y + 2.f}, t, small, th.color.windowBg);
        ctx.r.drawText({box.x + 6.6f, box.y + 2.f}, t, small, th.color.windowBg);   // en gras (simule)
        return box.w;
    }

    // La miniature de `it` dans `box` (sinon un cadre et la raison) ; vrai : pas encore prete.
    static bool paintThumb(const ui::PaintContext& ctx, const st::Item& it, const gfx::Rect& box) {
        const auto& th = ctx.theme;
        const auto& c = th.color;
        ctx.r.fillRect(box, c.windowBg);
        bool waiting = false;
        const auto small = th.font.smallUi;
        const float sl = ctx.r.lineHeight(small);
        if (it.hasCapture) {
            const auto pic = Pictures::instance().get(ctx.r, it.capturePath, 288);
            if (pic.ready && pic.tex.v) {
                ctx.r.drawImage(pic.tex, fitInto(box, pic.w, pic.h));
            } else {
                waiting = !pic.ready && !pic.failed;
                const std::string t = waiting ? std::string("capture\xE2\x80\xA6") : std::string("capture");
                const float tw = ctx.r.measure(t, small).width;
                ctx.r.drawText({box.x + (box.w - tw) * 0.5f, box.y + (box.h - sl) * 0.5f}, t, small, c.textMuted);
            }
        } else {
            // pas de capture : un cadre gris et la raison
            ctx.r.fillRect(box, c.headerBg);
            float y = box.y + 6.f;
            int lines = 0;
            for (const auto& l : wrapText(ctx.r, it.captureNote.empty() ? std::string("pas de capture") : "pas de capture : " + it.captureNote, small,
                                          box.w - 10.f)) {
                if (++lines > 4) break;
                ctx.r.drawText({box.x + 5.f, y}, l, small, c.textMuted);
                y += sl;
            }
        }
        ctx.r.strokeRect(box, c.border, 1.f);
        return waiting;
    }

    // Vrai : sa miniature n'est pas encore prete. 1.20.2 : `depth` 1, dans un dossier deplie (en retrait).
    bool paintRow(const ui::PaintContext& ctx, const st::Item& it, int i, int depth) {
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const auto b = bounds();
        const gfx::Rect r{b.x, b.y + static_cast<float>(i) * kRowH, b.w, kRowH};
        const bool sel = pane_.row_[tab_] == i || pane_.rowChosen(tab_, i);   // 1.24.1 : les lignes choisies a plusieurs aussi
        if (sel) ctx.r.fillRect(r, c.selectionBg);
        else if (hover_ == i) ctx.r.fillRect(r, th.brand.hover);
        ctx.r.line({r.x, r.bottom() - 0.5f}, {r.right(), r.bottom() - 0.5f}, c.border, 1.f);
        // 1.20.2 : dans un dossier deplie - en retrait, le filet du dossier a gauche
        const float indent = static_cast<float>(std::max(0, depth)) * kIndent;
        if (depth > 0) {
            const float gx = std::floor(r.x + 8.f + kIndent * 0.5f) + 0.5f;
            ctx.r.line({gx, r.y}, {gx, r.bottom()}, colorOf(th, toneOfCriticality(ui_::criticalityOf(it))), 2.f);
        }
        // la miniature
        const gfx::Rect box{r.x + 8.f + indent, r.y + 8.f, kThumbW, kThumbH};
        const bool waiting = paintThumb(ctx, it, box);
        const auto small = th.font.smallUi;
        const float sl = ctx.r.lineHeight(small);
        // le texte
        const float x = box.right() + 12.f, w = std::max(40.f, r.right() - x - 10.f);
        const auto font = th.font.ui;
        const float y0 = r.y + 7.f;
        const std::string date = ui_::dateText(ui_::stampOf(it));
        const float dw = ctx.r.measure(date, small).width;
        // 1.21.0 : une alarme qui bat - le badge << x327 >> (ses reapparitions regroupees), avant la date
        const std::uint64_t again = reappearancesOf(pane_.host_, it);
        const float bw = again ? paintBadge(ctx, again, r.right() - 10.f - dw - 8.f, y0) + 8.f : 0.f;
        const std::string name = ellipsize(ctx.r, it.name, font, w - dw - 10.f - bw);
        ctx.r.drawText({x, y0}, name, font, sel ? c.selectionText : c.text);
        ctx.r.drawText({x + 0.6f, y0}, name, font, sel ? c.selectionText : c.text);   // en gras (simule)
        ctx.r.drawText({r.right() - 10.f - dw, y0 + 2.f}, date, small, c.textMuted);
        const float y1 = y0 + ctx.r.lineHeight(font) + 3.f;
        if (!it.readable) {
            ctx.r.drawText({x, y1}, ellipsize(ctx.r, "illisible : " + it.why, small, w), small, c.error);
            return waiting;
        }
        const std::string alarm = ui_::alarmOf(it);
        const std::string equipment = it.equipment();
        std::string facts = "Alarme : " + alarm + " \xC2\xB7 \xC3\x89quipement : " + (equipment.empty() ? std::string("\xE2\x80\x94") : equipment)
                            + " \xC2\xB7 Vue : " + (it.snapshot.openView.empty() ? std::string("\xE2\x80\x94") : it.snapshot.openView);
        ctx.r.drawText({x, y1}, ellipsize(ctx.r, facts, small, w), small, sel ? c.selectionText : c.text);
        const float y2 = y1 + sl + 2.f;
        ctx.r.drawText({x, y2}, ellipsize(ctx.r, it.synthesis(), small, w), small, c.textMuted);
        // criticite (1.20.2), certitude, analyse, validation
        const float y3 = y2 + sl + 3.f;
        float cx = x;
        const auto chip = [&](const std::string& text, Tone tone) {
            if (cx >= x + w) return;
            const std::string t = ellipsize(ctx.r, text, small, x + w - cx);
            ctx.r.drawText({cx, y3}, t, small, colorOf(th, tone));
            cx += ctx.r.measure(t, small).width;
        };
        const int crit = ui_::criticalityOf(it);
        chip(criticalityLabel(crit), toneOfCriticality(crit));
        chip(" \xC2\xB7 ", Tone::Muted);
        const std::string certainty = it.certainty();
        chip("certitude : " + certainty, toneOfWords(certainty));
        chip(" \xC2\xB7 ", Tone::Muted);
        const bool running = pane_.host_ && pane_.host_->analysing() && pane_.host_->analysingFolder() == it.folder;
        const std::string state = running ? std::string("analyse en cours\xE2\x80\xA6") : "analyse : " + it.analysisState();
        chip(state, running ? Tone::Info : it.analysisState() == "faite" ? Tone::Ok : toneOfWords(it.analysisState()));
        chip(" \xC2\xB7 ", Tone::Muted);
        chip(ui_::validationLabel(it), it.state == st::State::Validated ? Tone::Ok : Tone::Accent);
        if (!it.comments.empty()) {
            chip(" \xC2\xB7 ", Tone::Muted);
            chip(std::to_string(it.comments.size()) + (it.comments.size() > 1 ? " commentaires" : " commentaire"), Tone::Muted);
        }
        return waiting;
    }

    // 1.20.2 : LA LIGNE D'UN DOSSIER - la miniature du plus recent sur une pile, le chevron, l'icone et l'alarme, la date
    // du plus recent ; le nombre d'incidents, la criticite, du premier au dernier ; l'equipement et la vue ; la synthese
    // du plus recent. Un filet de la couleur de la criticite a gauche. Vrai : la miniature n'est pas encore prete.
    bool paintGroup(const ui::PaintContext& ctx, const std::vector<st::Item>& items, const HmiIncidentsPane::Row& row, int i) {
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const auto b = bounds();
        const gfx::Rect r{b.x, b.y + static_cast<float>(i) * kRowH, b.w, kRowH};
        const bool sel = pane_.row_[tab_] == i || pane_.rowChosen(tab_, i);   // 1.24.1 : les lignes choisies a plusieurs aussi
        if (sel) ctx.r.fillRect(r, c.selectionBg);
        else if (hover_ == i) ctx.r.fillRect(r, th.brand.hover);
        ctx.r.line({r.x, r.bottom() - 0.5f}, {r.right(), r.bottom() - 0.5f}, c.border, 1.f);
        const gfx::Color critColor = colorOf(th, toneOfCriticality(row.criticality));
        ctx.r.fillRect({r.x, r.y + 6.f, 3.f, kRowH - 12.f}, critColor);
        const st::Item& latest = items[static_cast<std::size_t>(row.item)];
        const st::Item* oldest = &latest;
        for (const int m : row.members)
            if (m >= 0 && static_cast<std::size_t>(m) < items.size()) oldest = &items[static_cast<std::size_t>(m)];
        // la pile : deux cadres derriere la miniature du plus recent
        const float tw = kThumbW - 6.f, tht = kThumbH - 6.f;
        for (const float off : {6.f, 3.f}) {
            const gfx::Rect back{r.x + 8.f + off, r.y + 12.f - off, tw, tht};
            ctx.r.fillRect(back, c.windowBg);
            ctx.r.strokeRect(back, c.border, 1.f);
        }
        const bool waiting = paintThumb(ctx, latest, {r.x + 8.f, r.y + 12.f, tw, tht});
        // le texte
        const auto font = th.font.ui;
        const auto small = th.font.smallUi;
        const float sl = ctx.r.lineHeight(small);
        const gfx::Color ink = sel ? c.selectionText : c.text;
        const float x = r.x + 8.f + kThumbW + 12.f, w = std::max(40.f, r.right() - x - 10.f);
        const float y0 = r.y + 7.f, lh = ctx.r.lineHeight(font);
        ui::drawIcon(ctx.r, row.open ? ui::Icon::Collapse : ui::Icon::Expand, {x, y0 + (lh - 14.f) * 0.5f, 14.f, 14.f}, sel ? c.selectionText : c.textMuted);
        ui::drawIcon(ctx.r, row.open ? ui::Icon::FolderOpen : ui::Icon::Folder, {x + 18.f, y0 + (lh - 16.f) * 0.5f, 16.f, 16.f}, critColor);
        const std::string date = "dernier : " + ui_::dateText(ui_::stampOf(latest));
        const float dw = ctx.r.measure(date, small).width;
        const float nx = x + 40.f;
        const std::string name = ellipsize(ctx.r, row.alarm, font, std::max(0.f, x + w - nx - dw - 10.f));
        ctx.r.drawText({nx, y0}, name, font, ink);
        ctx.r.drawText({nx + 0.6f, y0}, name, font, ink);   // en gras (simule)
        ctx.r.drawText({r.right() - 10.f - dw, y0 + 2.f}, date, small, c.textMuted);
        // le nombre, la criticite, du premier au dernier
        const float y1 = y0 + lh + 3.f;
        float cx = x;
        const auto chip = [&](const std::string& text, gfx::Color col, float yy) {
            if (cx >= x + w) return;
            const std::string t = ellipsize(ctx.r, text, small, x + w - cx);
            ctx.r.drawText({cx, yy}, t, small, col);
            cx += ctx.r.measure(t, small).width;
        };
        const std::string count = incidentsWord(row.members.size());
        ctx.r.drawText({x + 0.6f, y1}, count, small, ink);   // en gras (simule)
        chip(count, ink, y1);
        chip(" \xC2\xB7 ", c.textMuted, y1);
        chip(criticalityLabel(row.criticality), critColor, y1);
        chip(" \xC2\xB7 du " + ui_::dateText(ui_::stampOf(*oldest)) + " au " + ui_::dateText(ui_::stampOf(latest)), c.textMuted, y1);
        std::uint64_t again = 0;   // 1.21.0 : les reapparitions regroupees de ses incidents
        for (const int m : row.members)
            if (m >= 0 && static_cast<std::size_t>(m) < items.size()) again += reappearancesOf(pane_.host_, items[static_cast<std::size_t>(m)]);
        if (again) chip(" \xC2\xB7 " + reappearancesWord(again), c.warning, y1);
        // l'equipement et la vue du plus recent ; sa synthese
        const float y2 = y1 + sl + 2.f;
        const std::string equipment = latest.equipment();
        ctx.r.drawText({x, y2},
                       ellipsize(ctx.r,
                                 "\xC3\x89quipement : " + (equipment.empty() ? std::string("\xE2\x80\x94") : equipment) + " \xC2\xB7 Vue : "
                                     + (latest.snapshot.openView.empty() ? std::string("\xE2\x80\x94") : latest.snapshot.openView),
                                 small, w),
                       small, ink);
        const float y3 = y2 + sl + 3.f;
        ctx.r.drawText({x, y3}, ellipsize(ctx.r, "le plus r\xC3\xA9" "cent : " + latest.synthesis(), small, w), small, c.textMuted);
        return waiting;
    }

    HmiIncidentsPane& pane_;
    int               tab_{0};
    int               hover_{-1};
};

// ============================================================================================== la fiche ==
//  Mise en page quand l'incident, la largeur ou un depli change (build) : une liste de primitives (textes, cadres,
//  icones, images) et de zones cliquables, relatives au haut de la fiche. Le dessin ne fait que les poser - sauf la
//  capture (sa texture, quand elle est prete) et la video (son image du moment).
class HmiIncidentSheet final : public ui::Widget {
public:
    enum Group : int { GCapture, GDiag, GChain, GAnalyses, GComments, GValidation, GIntegrity, GReappear /* 1.21.0 */, GCount };
    enum Act : int {
        ANone, AGroup, ASection, AAnalysis, AReveal, AAlarm, APlay, AStop, AOpenVideo, AFolder, ACompare, AAnalyze, AComment, AValidate,
        ARemove, AVerify, AExpandAll,
        // 1.21.0 : le lecteur (la barre sous l'image) et le zoom (les boutons sur l'image)
        AVStart, AVBack, AVPrev, AVPlay, AVNext, AVFwd, AVEnd, AVTrigger, AVLoop, AVSpeed, AVSeek, AZoomOut, AZoomFit, AZoomIn
    };

    explicit HmiIncidentSheet(HmiIncidentsPane& pane) : ui::Widget(pane.id() + ".fiche"), pane_(pane) {
        for (int g = 0; g < GCount; ++g) groupOpen_[static_cast<std::size_t>(g)] = g != GChain;
        sectionOpen_ = {0, 7};
        setFocusPolicy(true);   // 1.21.0 : les touches du lecteur (Espace, les fleches, + -...)
    }

    void setItem(const st::Item* it) {
        const std::string name = it ? it->name : std::string{};
        const bool other = name != name_;
        item_ = it;
        if (other) {
            name_ = name;
            stopVideo();
            view_.fit();   // 1.21.0 : une autre image, entiere
            analysisOpen_.clear();
            if (it)
                for (std::size_t k = 0; k < it->analyses.size(); ++k)
                    if (it->analyses[k].number > 0 && &it->analyses[k] == it->latest()) analysisOpen_.insert(static_cast<int>(k));
            integrity_.clear();
            integritySig_.clear();
        }
        // l'integrite : relue quand l'incident change ou que son contenu change (un commentaire, une analyse)
        const std::string sig = it ? it->folder + "|" + std::to_string(it->analyses.size()) + "|" + std::to_string(it->comments.size()) + "|"
                                         + (it->hasVideo ? "v" : "") + (it->validation ? "ok" : "")
                                   : std::string{};
        if (sig != integritySig_) {
            integritySig_ = sig;
            integrity_ = it ? pane_.verifySelected() : std::vector<std::string>{};
        }
        dirty();
        if (other) wantScroll_ = 0.f;
    }
    void dirty() {
        dirty_ = true;
        invalidate();
    }
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {0.f, height_};
        h.stretchX = h.stretchY = 1.f;
        return h;
    }
    [[nodiscard]] std::optional<ui::cursor::Shape> cursorAt(gfx::Point local) const override {
        const gfx::Point l{local.x - bounds().x, local.y - bounds().y};
        if (panning_) return ui::cursor::Shape::Move;
        for (const auto& h : hits_)
            if (h.r.contains(l)) return ui::cursor::Shape::Pointer;
        if (view_.zoomed() && picture_.contains(l)) return ui::cursor::Shape::Move;   // 1.21.0 : l'image zoomee se glisse
        return std::nullopt;
    }
    // 1.21.0 : les infobulles du lecteur et du zoom (leurs touches).
    [[nodiscard]] bool        hasTooltip() const override { return true; }
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override {
        const gfx::Point l{mouse.x - bounds().x, mouse.y - bounds().y};
        for (const auto& h : hits_) {
            if (!h.r.contains(l)) continue;
            switch (h.act) {
                case AVStart:   return "D\xC3\xA9" "but : la premi\xC3\xA8re image (touche D\xC3\xA9" "but)";
                case AVBack:    return "10 s en arri\xC3\xA8re (\xE2\x86\x90)";
                case AVPrev:    return "L'image pr\xC3\xA9" "c\xC3\xA9" "dente, en pause (Maj+\xE2\x86\x90, ou la virgule)";
                case AVPlay:    return "Lecture ou pause (Espace) ; au bout, Lecture repart du d\xC3\xA9" "but";
                case AVNext:    return "L'image suivante, en pause (Maj+\xE2\x86\x92, ou le point)";
                case AVFwd:     return "10 s en avant (\xE2\x86\x92)";
                case AVEnd:     return "Fin : la derni\xC3\xA8re image (touche Fin)";
                case AVTrigger: return "Aller au d\xC3\xA9" "clenchement : t = 0, le rep\xC3\xA8re rouge de la barre (D)";
                case AVLoop:
                    return std::string(clock_.loop ? "Boucle activ\xC3\xA9" "e" : "Boucle d\xC3\xA9sactiv\xC3\xA9" "e")
                         + " : au bout, la lecture " + (clock_.loop ? "repart du d\xC3\xA9" "but" : "s'arr\xC3\xAAte sur la derni\xC3\xA8re image")
                         + " (B, ou un clic, change)";
                case AVSpeed:
                    return "Vitesse " + ui_::speedLabel(hmi::rec::kSpeeds[static_cast<std::size_t>(std::clamp(h.arg, 0, kSpeedCount - 1))])
                         + " (+ et \xE2\x88\x92 : la voisine, comme la relecture des enregistrements)";
                case AVSeek:
                    return "La barre de temps : un clic ou un glisser d\xC3\xA9place la lecture ; le rep\xC3\xA8re rouge : le d\xC3\xA9" "clenchement (t = 0)";
                case AZoomOut:  return "Zoom arri\xC3\xA8re (la molette sur l'image)";
                case AZoomFit:  return "Ajuster : l'image enti\xC3\xA8re (ou un double-clic sur l'image)";
                case AZoomIn:   return "Zoom avant (la molette sur l'image, autour de la souris)";
                default:        return {};
            }
        }
        // (pas d'infobulle sur l'image : elle cacherait la video ; la legende sous la barre dit les gestes)
        if (player_ && timeRow_.contains(l))
            return "La position et la dur\xC3\xA9" "e ; t : les secondes depuis le d\xC3\xA9" "clenchement (n\xC3\xA9gatives : avant) ; l'image montr\xC3\xA9" "e";
        return {};
    }

    // ---- pour le volet ----
    [[nodiscard]] std::vector<std::string> sectionTitles() const {
        std::vector<std::string> out;
        for (const auto& s : sections()) out.push_back(s.title);
        return out;
    }
    [[nodiscard]] bool sectionOpen(std::size_t i) const { return sectionOpen_.count(static_cast<int>(i)) != 0; }
    void toggleSection(std::size_t i) {
        const int k = static_cast<int>(i);
        if (!sectionOpen_.erase(k)) sectionOpen_.insert(k);
        groupOpen_[GDiag] = true;
        dirty();
    }
    void setCompare(bool on) {
        compare_ = on;
        groupOpen_[GAnalyses] = true;
        dirty();
    }
    [[nodiscard]] bool comparing() const noexcept { return compare_; }
    void consult() {
        groupOpen_[GCapture] = groupOpen_[GDiag] = true;
        sectionOpen_.insert(0);
        wantScroll_ = 0.f;
        dirty();
    }
    void openAll() {
        groupOpen_[GDiag] = groupOpen_[GAnalyses] = true;
        for (int i = 0; i < 10; ++i) sectionOpen_.insert(i);
        if (item_ && !item_->analyses.empty()) {
            int k = static_cast<int>(item_->analyses.size()) - 1;
            for (std::size_t j = 0; j < item_->analyses.size(); ++j)
                if (&item_->analyses[j] == item_->latest()) k = static_cast<int>(j);
            analysisOpen_.insert(k);
        }
        wantGroup_ = GDiag;
        dirty();
    }
    void openGroup(Group g) {
        groupOpen_[static_cast<std::size_t>(g)] = true;
        wantGroup_ = g;
        dirty();
    }
    void reverify() {
        integrity_ = item_ ? pane_.verifySelected() : std::vector<std::string>{};
        groupOpen_[GIntegrity] = true;
        dirty();
    }
    void stopVideo() {
        if (player_ && player_->tex.v) dropped_.emplace_back(player_->owner, player_->tex);
        player_.reset();
        clock_.playing = false;
        lastTick_ = -1.0;
        imageW_ = imageH_ = 0;   // la prochaine image dessinee les redonne
        endDrags();
        if (view_.zoomed()) view_.fit();   // 1.21.0 : la capture revient entiere (une autre taille que la video)
        dirty();
    }
    bool playVideo(std::string* why) {
        if (!item_ || !item_->hasVideo || item_->videoPath.empty()) {
            if (why) *why = "pas de vid\xC3\xA9o pour cet incident";
            return false;
        }
        std::ifstream in(pathOf(item_->videoPath), std::ios::binary);
        if (!in) {
            if (why) *why = "vid\xC3\xA9o illisible : " + item_->videoPath;
            return false;
        }
        auto bytes = std::make_shared<hmi::Bytes>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        hmi::BlobPtr blob = std::move(bytes);
        std::string reason;
        auto src = hmi::video::openFrameSource(blob, "AVI", &reason);
        if (!src) {
            if (why) *why = "vid\xC3\xA9o non d\xC3\xA9" "cod\xC3\xA9" "e : " + reason;
            return false;
        }
        // 1.21.0 : la cadence (celle du fichier), la duree, et la place du declenchement (video.json : instants_s, avant_s)
        hmi::video::AviInfo info;
        const double fps = hmi::video::parseAvi(*blob, info) && info.fps > 0 ? info.fps : item_->videoFps;
        std::vector<double> times;
        double before = 0;
        {
            std::ifstream js(pathOf(item_->folder) / "video.json", std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(js)), std::istreambuf_iterator<char>());
            ai::json::Value doc;
            if (!text.empty() && ai::json::parse(text, doc)) {
                before = doc.num("avant_s");
                for (const auto& v : doc.at("instants_s").items())
                    if (v.isNumber()) times.push_back(v.asNumber());
            }
        }
        const double duration = src->duration();
        stopVideo();
        player_ = std::make_unique<Player>();
        player_->blob = std::move(blob);
        player_->src = std::move(src);
        // elle part du debut, en lecture (comme avant) ; la vitesse et la boucle restent celles choisies
        clock_.start(duration, fps, ui_::triggerPosition(times, fps, before, duration));
        lastTick_ = -1.0;
        view_.fit();
        groupOpen_[GCapture] = true;
        wantGroup_ = GCapture;
        dirty();
        return true;
    }

    // 1.21.0 : LES GESTES DU LECTEUR ET DU ZOOM (les boutons, les touches) ; faux : rien a faire ici (pas de video).
    bool playerAct(int what, int arg) {
        if (what >= AZoomOut && what <= AZoomIn) {
            if (picture_.w <= 0.f) return false;
            const gfx::Rect box = screenPicture();
            if (what == AZoomFit) view_.fit();
            else view_.zoomBy(what == AZoomIn ? ui_::ImageView::kStep : 1.f / ui_::ImageView::kStep, box, imageW(), imageH());
            invalidate();
            return true;
        }
        if (!player_) return false;
        switch (what) {
            case AVStart:   clock_.seek(0); break;
            case AVBack:    clock_.step(-10.0); break;
            case AVPrev:    clock_.stepFrame(-1); break;
            case AVPlay:    clock_.playPause(); break;
            case AVNext:    clock_.stepFrame(1); break;
            case AVFwd:     clock_.step(10.0); break;
            case AVEnd:     clock_.seek(clock_.duration); break;
            case AVTrigger: clock_.toTrigger(); break;
            case AVLoop:    clock_.loop = !clock_.loop; break;
            case AVSpeed:   clock_.setSpeed(hmi::rec::kSpeeds[static_cast<std::size_t>(std::clamp(arg, 0, kSpeedCount - 1))]); break;
            default:        return false;
        }
        invalidate();   // en lecture, onPaint fait avancer ; en pause, l'image demandee se montre
        return true;
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto b = bounds();
        ctx.r.fillRect(b, th.color.panelBg);
        for (auto it = dropped_.begin(); it != dropped_.end();) {
            if (it->first == &ctx.r) {
                ctx.r.releaseImage(it->second);
                it = dropped_.erase(it);
            } else {
                ++it;
            }
        }
        // 1.21.0 : LA LECTURE AVANCE de (dt x vitesse), dt l'horloge entre deux images (bornee : une fiche cachee un
        // moment ne saute pas la video) ; en pause, rien ne bouge et rien ne se redessine.
        if (player_ && clock_.playing) {
            if (lastTick_ >= 0.0) clock_.advance(std::clamp(ctx.time - lastTick_, 0.0, 0.5));
            lastTick_ = ctx.time;
        } else {
            lastTick_ = -1.0;
        }
        if (dirty_ || b.w != builtWidth_) {
            build(ctx.r, th, b.w);
            dirty_ = false;
            builtWidth_ = b.w;
        }
        // l'image suivante, si l'image ou la barre se voient ; repliees ou hors de vue, la lecture attend (rien ne
        // tourne pour rien)
        if (player_ && clock_.playing) {
            const float top = b.y + picture_.y, bottom = b.y + std::max(picture_.bottom(), timeRow_.bottom());
            if (picture_.w > 0.f && bottom >= ctx.clip.y && top <= ctx.clip.bottom()) invalidate();
            else lastTick_ = -1.0;
        }
        if (wantGroup_ >= 0 || wantScroll_ >= 0.f) {
            const float y = wantGroup_ >= 0 ? groupY_[static_cast<std::size_t>(wantGroup_)] : wantScroll_;
            wantGroup_ = -1;
            wantScroll_ = -1.f;
            if (auto* sp = dynamic_cast<ui::ScrollablePanel*>(parent())) sp->scrollTo({0.f, std::max(0.f, y - 4.f)});
        }
        const float ox = b.x, oy = b.y;
        const auto clip = ctx.clip;
        for (const auto& p : prims_) {
            const gfx::Rect r{ox + p.r.x, oy + p.r.y, p.r.w, p.r.h};
            if (r.bottom() < clip.y || r.y > clip.bottom()) continue;
            const auto col = colorOf(th, p.tone);
            switch (p.k) {
                case Prim::K::Text:
                    ctx.r.drawText({r.x, r.y}, p.text, p.font, col);
                    if (p.bold) ctx.r.drawText({r.x + 0.6f, r.y}, p.text, p.font, col);
                    break;
                case Prim::K::Fill:   ctx.r.fillRect(r, col); break;
                case Prim::K::Round:  ctx.r.fillRoundedRect(r, col, 4.f); break;
                case Prim::K::Stroke: ctx.r.strokeRect(r, col, 1.f); break;
                case Prim::K::Icon:   ui::drawIcon(ctx.r, p.icon, r, col); break;
                case Prim::K::Picture: paintPicture(ctx, r); break;
                case Prim::K::Player: paintPlayer(ctx, ox, oy); break;
            }
        }
        // la zone sous la souris (pas la barre de temps : son bouton le montre)
        if (hover_ >= 0 && static_cast<std::size_t>(hover_) < hits_.size() && hits_[static_cast<std::size_t>(hover_)].act != AVSeek) {
            const auto& h = hits_[static_cast<std::size_t>(hover_)].r;
            ctx.r.strokeRect({ox + h.x, oy + h.y, h.w, h.h}, th.brand.focusRing, 1.f);
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        const auto hitAt = [&](gfx::Point p) {
            const gfx::Point l{p.x - bounds().x, p.y - bounds().y};
            for (std::size_t i = 0; i < hits_.size(); ++i)
                if (hits_[i].r.contains(l)) return static_cast<int>(i);
            return -1;
        };
        // 1.21.0 : un glisser en cours - la barre de temps, ou l'image zoomee (la souris est tenue)
        if (seeking_ || panning_) {
            if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
                if (seeking_) seekAt(m->pos.x);
                else view_.drag({m->pos.x - panFrom_.x, m->pos.y - panFrom_.y}, screenPicture(), imageW(), imageH());
                panFrom_ = m->pos;
                invalidate();
                return ui::EventResult::Consumed;
            }
            if (std::holds_alternative<ui::MouseUp>(ev)) {
                endDrags();
                return ui::EventResult::Consumed;
            }
        }
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            const int h = bounds().contains(m->pos) ? hitAt(m->pos) : -1;
            if (h != hover_) {
                hover_ = h;
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        // 1.21.0 : la molette sur l'image zoome autour de la souris ; ajustee, vers le bas, la fiche defile (comme ailleurs)
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && w->dy != 0.f && !w->mods.ctrl && !w->mods.alt) {
            const gfx::Rect box = screenPicture();
            if (box.w > 0.f && box.contains(w->pos) && (w->dy > 0.f || view_.zoomed())) {
                view_.zoomAt(view_.zoom * (w->dy > 0.f ? ui_::ImageView::kStep : 1.f / ui_::ImageView::kStep), box, imageW(), imageH(), w->pos);
                invalidate();
                return ui::EventResult::Consumed;
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && bounds().contains(d->pos)) {
            grabFocus();   // 1.21.0 : les touches du lecteur
            const int h = hitAt(d->pos);
            if (h >= 0) {
                const Hit hit = hits_[static_cast<std::size_t>(h)];
                if (hit.act == AVSeek) {   // la barre de temps : un clic y va, un glisser la suit
                    seeking_ = true;
                    captureMouse();
                    seekAt(d->pos.x);
                } else {
                    run(hit.act, hit.arg);
                }
                return ui::EventResult::Consumed;
            }
            // 1.21.0 : l'image - un double-clic l'ajuste ; zoomee, un glisser la deplace
            if (const gfx::Rect box = screenPicture(); box.w > 0.f && box.contains(d->pos)) {
                if (d->clickCount >= 2) {
                    view_.fit();
                    invalidate();
                } else if (view_.zoomed()) {
                    panning_ = true;
                    panFrom_ = d->pos;
                    captureMouse();
                }
                return ui::EventResult::Consumed;
            }
            return ui::EventResult::Ignored;
        }
        // 1.21.0 : LES TOUCHES DU LECTEUR, celles de la relecture des enregistrements (la fiche a le clavier : un clic
        // dessus le lui donne). Espace lecture ou pause ; fleches 10 s ; Maj + fleches (ou la virgule, le point) une
        // image ; Debut, Fin ; + et - la vitesse ; D le declenchement ; B la boucle ; Echap arrete la video.
        if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
            if (!focused() || k->mods.ctrl || k->mods.alt || k->mods.super) return ui::EventResult::Ignored;
            if (!player_) {
                // sans video lue : Espace la lit (s'il y en a une)
                if (k->key != ui::Key::Space || !item_ || !item_->hasVideo) return ui::EventResult::Ignored;
                if (!k->repeat) run(APlay, 0);
                return ui::EventResult::Consumed;
            }
            switch (k->key) {
                case ui::Key::Space:  return k->repeat ? ui::EventResult::Consumed : press(AVPlay);
                case ui::Key::Left:   return press(k->mods.shift ? AVPrev : AVBack);
                case ui::Key::Right:  return press(k->mods.shift ? AVNext : AVFwd);
                case ui::Key::Home:   return press(AVStart);
                case ui::Key::End:    return press(AVEnd);
                case ui::Key::D:      return k->repeat ? ui::EventResult::Consumed : press(AVTrigger);
                case ui::Key::B:      return k->repeat ? ui::EventResult::Consumed : press(AVLoop);
                case ui::Key::Plus:   signKey_ = true; clock_.faster(); invalidate(); return ui::EventResult::Consumed;   // son "+" ne recompte pas
                case ui::Key::Minus:  signKey_ = true; clock_.slower(); invalidate(); return ui::EventResult::Consumed;
                case ui::Key::Escape:
                    if (k->repeat) return ui::EventResult::Consumed;
                    stopVideo();
                    return ui::EventResult::Consumed;
                default:              return ui::EventResult::Ignored;
            }
        }
        if (const auto* t = std::get_if<ui::TextInput>(&ev)) {
            if (!focused() || !player_) return ui::EventResult::Ignored;
            const bool sign = signKey_;
            signKey_ = false;
            if (sign && (t->utf8 == "+" || t->utf8 == "-" || t->utf8 == "=")) return ui::EventResult::Consumed;
            if (t->utf8 == "+") clock_.faster();        // le + d'un clavier AZERTY (Maj + =)
            else if (t->utf8 == "-") clock_.slower();   // le - de la rangee des chiffres d'un AZERTY
            else if (t->utf8 == ",") return press(AVPrev);
            else if (t->utf8 == ".") return press(AVNext);
            else return ui::EventResult::Ignored;
            invalidate();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    struct Prim {
        enum class K : std::uint8_t { Text, Fill, Round, Stroke, Icon, Picture, Player /* 1.21.0 */ } k{K::Text};
        gfx::Rect   r{};
        std::string text;
        gfx::FontId font{16};
        Tone        tone{Tone::Text};
        bool        bold{false};
        ui::Icon    icon{ui::Icon::None};
    };
    struct Hit {
        gfx::Rect r{};
        int       act{ANone};
        int       arg{0};
    };
    // La video lue ici : une seule texture a la fois (l'image du moment). 1.21.0 : sa position est celle de clock_.
    struct Player {
        hmi::BlobPtr                             blob;
        std::unique_ptr<hmi::video::FrameSource> src;
        gfx::TextureId                           tex{};
        gfx::IRenderer*                          owner{nullptr};
        std::uint64_t                            serial{0};
        int                                      w{0}, h{0};
    };

    [[nodiscard]] std::vector<ui_::Section> sections() const {
        if (!item_ || !item_->readable) return {};
        const auto* last = item_->latest();
        const std::string_view extra = last && last->number > 0 ? std::string_view(last->text) : std::string_view{};
        return ui_::splitDiagnosis(inc::diagnosis(item_->snapshot, extra));
    }

    void run(int act, int arg) {
        std::string why;
        switch (act) {
            case AGroup: {
                auto& o = groupOpen_[static_cast<std::size_t>(arg)];
                o = !o;
                dirty();
                break;
            }
            case ASection: toggleSection(static_cast<std::size_t>(arg)); break;
            case AAnalysis:
                if (!analysisOpen_.erase(arg)) analysisOpen_.insert(arg);
                dirty();
                break;
            case AReveal:
                if (!pane_.revealLink(static_cast<std::size_t>(arg), &why)) pane_.say(why, true);
                break;
            case AAlarm: {
                if (!item_) break;
                const std::string a = item_->snapshot.alarm.name.empty() ? item_->snapshot.trigger.name : item_->snapshot.alarm.name;
                const auto* p = pane_.doc_ ? &pane_.doc_->project : nullptr;
                if (!p || !p->alarmByName(a) || !pane_.hosts_.reveal || !pane_.hosts_.reveal("alarme", a, {}))
                    pane_.say("\xC2\xAB " + a + " \xC2\xBB n'existe plus dans les alarmes du projet : la fiche garde son nom d'origine.", true);
                break;
            }
            case APlay:
                if (!playVideo(&why)) pane_.say(why, true);
                break;
            case AStop: stopVideo(); break;
            case AOpenVideo:
                if (!item_) break;
                if (!pane_.hosts_.openFile) pane_.say("Pas de lecteur vid\xC3\xA9o du syst\xC3\xA8me ici.", true);
                else if (const std::string w = pane_.hosts_.openFile(item_->videoPath); !w.empty()) pane_.say(w, true);
                break;
            case AFolder: pane_.openSelectedFolder(); break;
            case ACompare: setCompare(!compare_); break;
            case AAnalyze:
                if (!pane_.analyzeSelected(&why)) pane_.say(why, true);
                dirty();
                break;
            case AComment: pane_.askComment(); break;
            case AValidate: pane_.askValidate(); break;
            case ARemove: pane_.askRemove(); break;
            case AVerify: reverify(); break;
            case AExpandAll: pane_.showFullAnalysis(); break;
            default: (void)playerAct(act, arg); break;   // 1.21.0 : le lecteur et le zoom
        }
    }

    // ---- 1.21.0 : le lecteur et le zoom ----
    static constexpr int kSpeedCount = static_cast<int>(std::size(hmi::rec::kSpeeds));
    ui::EventResult press(int what) {
        (void)playerAct(what, 0);
        return ui::EventResult::Consumed;
    }
    // Le cadre de l'image, dans le repere de la fiche (vide : pas d'image).
    [[nodiscard]] gfx::Rect screenPicture() const {
        if (picture_.w <= 0.f) return {};
        return {bounds().x + picture_.x, bounds().y + picture_.y, picture_.w, picture_.h};
    }
    // La taille de l'image montree (la derniere dessinee ; sinon celle du fichier, ou le cadre).
    [[nodiscard]] int imageW() const {
        if (imageW_ > 0) return imageW_;
        if (player_ && player_->w > 0) return player_->w;
        if (item_ && item_->captureWidth > 0) return item_->captureWidth;
        return static_cast<int>(std::max(1.f, picture_.w));
    }
    [[nodiscard]] int imageH() const {
        if (imageH_ > 0) return imageH_;
        if (player_ && player_->h > 0) return player_->h;
        if (item_ && item_->captureHeight > 0) return item_->captureHeight;
        return static_cast<int>(std::max(1.f, picture_.h));
    }
    // La piste de la barre de temps (dans le repere de la fiche) ; `x` -> la position.
    [[nodiscard]] gfx::Rect seekTrack() const {
        for (const auto& h : hits_)
            if (h.act == AVSeek) return {bounds().x + h.r.x + 7.f, bounds().y + h.r.y + 4.f, std::max(10.f, h.r.w - 14.f), 16.f};
        return {};
    }
    void seekAt(float x) {
        const gfx::Rect t = seekTrack();
        if (!player_ || t.w <= 0.f) return;
        clock_.seek(std::clamp(static_cast<double>((x - t.x) / t.w), 0.0, 1.0) * clock_.duration);
        invalidate();
    }
    void endDrags() {
        if (seeking_ || panning_) releaseMouse();
        seeking_ = panning_ = false;
    }

    // ---- la mise en page ----
    struct Builder {
        gfx::IRenderer&     r;
        const ui::Theme&    th;
        std::vector<Prim>&  prims;
        std::vector<Hit>&   hits;
        float               x0{14.f}, w{100.f}, y{12.f};

        float lh(gfx::FontId f) const { return r.lineHeight(f); }
        void text(float x, float yy, std::string s, gfx::FontId f, Tone t, bool bold = false) {
            Prim p;
            p.k = Prim::K::Text;
            p.r = {x, yy, r.measure(s, f).width, lh(f)};
            p.text = std::move(s);
            p.font = f;
            p.tone = t;
            p.bold = bold;
            prims.push_back(std::move(p));
        }
        void rect(Prim::K k, gfx::Rect rr, Tone t) {
            Prim p;
            p.k = k;
            p.r = rr;
            p.tone = t;
            prims.push_back(std::move(p));
        }
        void icon(gfx::Rect rr, ui::Icon i, Tone t) {
            Prim p;
            p.k = Prim::K::Icon;
            p.r = rr;
            p.icon = i;
            p.tone = t;
            prims.push_back(std::move(p));
        }
        // Un paragraphe, coupe en lignes ; `first` : la place de la premiere ligne (apres une etiquette).
        float para(float x, float width, std::string_view s, gfx::FontId f, Tone t, float firstX = -1.f, bool bold = false) {
            const float firstRoom = firstX >= 0.f ? x + width - firstX : -1.f;
            const auto lines = wrapText(r, s, f, width, firstRoom);
            for (std::size_t i = 0; i < lines.size(); ++i) {
                text(i == 0 && firstX >= 0.f ? firstX : x, y, lines[i], f, t, bold);
                y += lh(f) + 2.f;
            }
            return y;
        }
        // Une rangee de boutons (a la ligne quand ils ne tiennent pas).
        void buttons(const std::vector<std::tuple<std::string, int, int>>& list) {
            const auto f = th.font.ui;
            const float h = lh(f) + 10.f;
            float x = x0;
            for (const auto& [label, act, arg] : list) {
                const float bw = r.measure(label, f).width + 22.f;
                if (x > x0 && x + bw > x0 + w) {
                    x = x0;
                    y += h + 6.f;
                }
                const gfx::Rect rr{x, y, bw, h};
                rect(Prim::K::Round, rr, Tone::Field);
                rect(Prim::K::Stroke, rr, Tone::Border);
                text(x + 11.f, y + 5.f, label, f, Tone::Accent);
                hits.push_back({rr, act, arg});
                x += bw + 8.f;
            }
            if (!list.empty()) y += h + 8.f;
        }
        // Une ligne qu'on deplie : son icone, son titre, et la zone cliquable.
        void fold(float x, const std::string& title, bool open, int act, int arg, gfx::FontId f, bool band, Tone t = Tone::Text,
                  const std::string& note = {}) {
            const float h = lh(f) + (band ? 10.f : 6.f);
            const gfx::Rect rr{x - (band ? 6.f : 2.f), y, x0 + w - x + (band ? 6.f : 2.f), h};
            if (band) rect(Prim::K::Fill, rr, Tone::Band);
            icon({x, y + (h - 14.f) * 0.5f, 14.f, 14.f}, open ? ui::Icon::Collapse : ui::Icon::Expand, Tone::Muted);
            text(x + 20.f, y + (h - lh(f)) * 0.5f, ellipsize(r, title, f, w - (x - x0) - 30.f), f, t, true);
            if (!note.empty()) {
                const float tx = x + 20.f + r.measure(title, f).width + 10.f;
                if (tx < x0 + w - 40.f)
                    text(tx, y + (h - lh(th.font.smallUi)) * 0.5f, ellipsize(r, note, th.font.smallUi, x0 + w - tx), th.font.smallUi, Tone::Muted);
            }
            hits.push_back({rr, act, arg});
            y += h + 4.f;
        }
        // Le texte d'une fiche ou d'une reponse (markdown leger : titres, puces, listes numerotees, *notes*, **gras**,
        // `code`, [etiquettes de certitude]).
        void markdown(float x, float width, std::string_view src) {
            const auto f = th.font.ui;
            std::size_t pos = 0;
            while (pos <= src.size()) {
                std::size_t e = src.find('\n', pos);
                if (e == std::string_view::npos) e = src.size();
                std::string line(src.substr(pos, e - pos));
                pos = e + 1;
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                if (line.empty()) {
                    y += 6.f;
                    if (e >= src.size()) break;
                    continue;
                }
                std::size_t spaces = 0;
                while (spaces < line.size() && line[spaces] == ' ') ++spaces;
                std::string s = line.substr(spaces);
                const float indent = static_cast<float>(std::min<std::size_t>(3, spaces / 2)) * 16.f;
                bool heading = false;
                Tone tone = Tone::Text;
                std::string prefix;
                if (s.rfind("#", 0) == 0) {
                    std::size_t h = 0;
                    while (h < s.size() && s[h] == '#') ++h;
                    if (h < s.size() && s[h] == ' ') {
                        s = s.substr(h + 1);
                        heading = true;
                        y += 4.f;
                    }
                } else if ((s.rfind("- ", 0) == 0) || (s.rfind("* ", 0) == 0)) {
                    prefix = "\xE2\x80\xA2 ";
                    s = s.substr(2);
                } else {
                    std::size_t d = 0;
                    while (d < s.size() && d < 3 && std::isdigit(static_cast<unsigned char>(s[d]))) ++d;
                    if (d > 0 && d + 1 < s.size() && s[d] == '.' && s[d + 1] == ' ') {
                        prefix = s.substr(0, d + 2);
                        s = s.substr(d + 2);
                    }
                }
                if (s.size() >= 2 && s.front() == '*' && s.back() == '*' && s.rfind("**", 0) != 0) {
                    s = s.substr(1, s.size() - 2);
                    tone = Tone::Muted;
                }
                for (const char* mark : {"**", "`"})
                    for (std::size_t at = s.find(mark); at != std::string::npos; at = s.find(mark, at)) s.erase(at, std::char_traits<char>::length(mark));
                float xx = x + indent;
                if (!prefix.empty()) {
                    text(xx, y, prefix, f, Tone::Muted);
                    xx += r.measure(prefix, f).width;
                }
                const float right = x + width;
                std::string tag;
                if (!s.empty() && s.front() == '[')
                    if (const auto close = s.find(']'); close != std::string::npos && close < 40) {
                        tag = s.substr(0, close + 1);
                        s = trimLeft(s.substr(close + 1));
                    }
                float firstX = xx;
                if (!tag.empty()) {
                    text(xx, y, tag, th.font.smallUi, toneOfWords(tag));
                    firstX = xx + r.measure(tag, th.font.smallUi).width + 6.f;
                }
                para(xx, right - xx, s, f, heading ? Tone::Text : tone, firstX > xx ? firstX : -1.f, heading);
                if (e >= src.size()) break;
            }
        }
        static std::string trimLeft(std::string s) {
            std::size_t i = 0;
            while (i < s.size() && s[i] == ' ') ++i;
            return s.substr(i);
        }
    };

    void build(gfx::IRenderer& r, const ui::Theme& th, float width) {
        prims_.clear();
        hits_.clear();
        groupY_.fill(0.f);
        picture_ = timeRow_ = {};   // 1.21.0 : refaits avec l'image et le lecteur
        Builder B{r, th, prims_, hits_};
        B.x0 = 16.f;
        B.w = std::max(120.f, width - 32.f - 12.f);
        B.y = 12.f;
        const auto f = th.font.ui;
        const auto small = th.font.smallUi;
        const float W = B.w, X = B.x0;
        if (!item_) {
            B.para(X, W, "Choisis un incident dans la liste : sa fiche s'affiche ici - la capture, la vid\xC3\xA9o, le diagnostic en dix "
                         "sections, les d\xC3\xA9pendances, les analyses, les commentaires, la validation et l'int\xC3\xA9grit\xC3\xA9.",
                   f, Tone::Muted);
            finish(B);
            return;
        }
        const auto& it = *item_;
        const auto& s = it.snapshot;
        // ---- l'en-tete ----
        B.text(X, B.y, ellipsize(r, it.name, th.font.lead, W), th.font.lead, Tone::Text, true);
        B.y += B.lh(th.font.lead) + 4.f;
        B.para(X, W, ui_::validationLabel(it) + " \xC2\xB7 cr\xC3\xA9\xC3\xA9 le " + ui_::dateText(s.createdAt), small,
               it.state == st::State::Validated ? Tone::Ok : Tone::Accent);
        if (!it.readable) {
            B.y += 4.f;
            B.para(X, W, "Illisible : " + it.why, f, Tone::Error);
            B.y += 6.f;
            B.buttons({{"Ouvrir le dossier", AFolder, 0}});
            integrityGroup(B);
            finish(B);
            return;
        }
        const std::string alarm = s.alarm.name.empty() ? s.trigger.name : s.alarm.name;
        B.para(X, W, std::string(inc::eventLabel(s.trigger.kind)) + " de \xC2\xAB " + alarm + " \xC2\xBB"
                         + (s.trigger.at.empty() ? std::string{} : " le " + ui_::dateText(s.trigger.at))
                         + (s.alarm.known ? std::string{} : " (alarme introuvable dans le projet au moment du d\xC3\xA9" "faut)"),
               f, Tone::Text);
        const std::string eq = it.equipment();
        B.para(X, W, "\xC3\x89quipement : " + (eq.empty() ? std::string("\xE2\x80\x94") : eq) + " \xC2\xB7 Vue affich\xC3\xA9" "e : "
                         + (s.openView.empty() ? std::string("aucune") : s.openView) + " \xC2\xB7 Projet : " + s.project
                         + (s.projectVersion.empty() ? std::string{} : " (" + s.projectVersion + ")"),
               small, Tone::Muted);
        if (!s.summary.empty()) {
            B.y += 2.f;
            B.para(X, W, s.summary, f, Tone::Text);
        }
        B.y += 6.f;
        B.buttons({{"Ouvrir l'alarme", AAlarm, 0}, {"Voir l'analyse compl\xC3\xA8te", AExpandAll, 0}, {"Ouvrir le dossier", AFolder, 0}});
        reappearGroup(B);   // 1.21.0 : une alarme qui bat - ses reapparitions regroupees

        // ---- la capture et la video ----
        group(B, GCapture, "Capture et vid\xC3\xA9o", it.hasVideo ? "vid\xC3\xA9o Motion-JPEG" : "pas de vid\xC3\xA9o");
        if (groupOpen_[GCapture]) {
            if (it.hasCapture || player_) {
                int iw = it.captureWidth, ih = it.captureHeight;
                if (player_ && player_->w > 0) {
                    iw = player_->w;
                    ih = player_->h;
                }
                const float aspect = iw > 0 && ih > 0 ? static_cast<float>(ih) / static_cast<float>(iw) : 0.5625f;
                float bw = std::min(W, 960.f), bh = bw * aspect;
                if (bh > 420.f) {
                    bh = 420.f;
                    bw = bh / aspect;
                }
                Prim p;
                p.k = Prim::K::Picture;
                p.r = {X, B.y, bw, bh};
                prims_.push_back(p);
                picture_ = p.r;
                B.rect(Prim::K::Stroke, {X, B.y, bw, bh}, Tone::Border);
                // 1.21.0 : LE ZOOM - en haut a droite de l'image : moins, Ajuster, plus (le pourcentage a cote, au dessin)
                if (bw >= 150.f && bh >= 60.f) {
                    const float zy = B.y + 6.f, zin = X + bw - 34.f;
                    hits_.push_back({{zin, zy, 28.f, 24.f}, AZoomIn, 0});
                    const float fitW = r.measure("Ajuster", small).width + 16.f;
                    hits_.push_back({{zin - fitW - 2.f, zy, fitW, 24.f}, AZoomFit, 0});
                    hits_.push_back({{zin - fitW - 32.f, zy, 28.f, 24.f}, AZoomOut, 0});
                }
                B.y += bh + 6.f;
                // 1.21.0 : LE LECTEUR, sous l'image (comme la barre de relecture des enregistrements)
                if (player_) playerBar(B, X, std::min(W, std::max(bw, 600.f)));
                B.para(X, W,
                       player_ ? std::string("Vid\xC3\xA9o Motion-JPEG : les secondes d'avant et d'apr\xC3\xA8s le d\xC3\xA9" "faut")
                                     + (clock_.hasTrigger() ? " ; t = 0 au d\xC3\xA9" "clenchement (le rep\xC3\xA8re rouge de la barre)." : ".")
                               : "Capture de l'\xC3\xA9" "cran au d\xC3\xA9" "clenchement : une image fixe, pas une vid\xC3\xA9o"
                                     + (it.captureWidth > 0 ? " (" + std::to_string(it.captureWidth) + " \xC3\x97 " + std::to_string(it.captureHeight) + ")"
                                                            : std::string{})
                                     + ".",
                       small, Tone::Muted);
                B.para(X, W,
                       player_ ? std::string("Espace : lecture ou pause \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 : 10 s \xC2\xB7 Maj+\xE2\x86\x90 \xE2\x86\x92 (ou , .) : "
                                             "une image \xC2\xB7 D\xC3\xA9" "but, Fin \xC2\xB7 D : le d\xC3\xA9" "clenchement \xC2\xB7 B : la boucle \xC2\xB7 + "
                                             "\xE2\x88\x92 : la vitesse \xC2\xB7 \xC3\x89" "chap : arr\xC3\xAAter la vid\xC3\xA9o \xC2\xB7 molette : zoom \xC2\xB7 glisser : "
                                             "d\xC3\xA9placer \xC2\xB7 double-clic : ajuster")
                               : std::string("Molette sur l'image : zoom \xC2\xB7 glisser : d\xC3\xA9placer (zoom\xC3\xA9" "e) \xC2\xB7 double-clic : ajuster"),
                       small, Tone::Muted);
            } else {
                const gfx::Rect box{X, B.y, std::min(W, 320.f), 72.f};
                B.rect(Prim::K::Fill, box, Tone::Band);
                B.rect(Prim::K::Stroke, box, Tone::Border);
                const float keep = B.y;
                B.y += 8.f;
                B.para(X + 10.f, box.w - 20.f, it.captureNote.empty() ? std::string("Pas de capture.") : "Pas de capture : " + it.captureNote, small,
                       Tone::Muted);
                B.y = std::max(B.y, keep + box.h) + 6.f;
            }
            B.para(X, W, "Vid\xC3\xA9o : " + ui_::videoLabel(it) + ".", f, it.hasVideo ? Tone::Text : Tone::Muted);
            B.y += 4.f;
            std::vector<std::tuple<std::string, int, int>> bl;
            if (it.hasVideo) {
                bl.emplace_back(player_ ? "Arr\xC3\xAAter la vid\xC3\xA9o" : "Lire la vid\xC3\xA9o ici", player_ ? AStop : APlay, 0);
                bl.emplace_back("Ouvrir avec le lecteur du syst\xC3\xA8me", AOpenVideo, 0);
            }
            bl.emplace_back("Ouvrir le dossier", AFolder, 0);
            B.buttons(bl);
        }

        // ---- la fiche de diagnostic ----
        const auto secs = sections();
        group(B, GDiag, "Fiche de diagnostic", std::to_string(secs.size()) + " sections");
        if (groupOpen_[GDiag]) {
            const auto* last = it.latest();
            B.para(X, W,
                   last && last->number > 0 ? "Relue depuis le dossier ; sa section 7 porte la derni\xC3\xA8re analyse (" + last->source + ", "
                                                  + ui_::dateText(last->createdAt) + ")."
                                            : std::string("Relue depuis le dossier : le projet tel qu'il \xC3\xA9tait au moment du d\xC3\xA9" "faut."),
                   small, Tone::Muted);
            B.y += 2.f;
            for (std::size_t i = 0; i < secs.size(); ++i) {
                const bool open = sectionOpen_.count(static_cast<int>(i)) != 0;
                B.fold(X + 4.f, secs[i].title, open, ASection, static_cast<int>(i), f, false);
                if (open) {
                    B.markdown(X + 26.f, W - 26.f, secs[i].body);
                    B.y += 6.f;
                }
            }
        }

        // ---- les dependances ----
        group(B, GChain, "D\xC3\xA9pendances", std::to_string(s.chain.size()) + (s.chain.size() > 1 ? " maillons" : " maillon"));
        if (groupOpen_[GChain]) {
            B.para(X, W, "Un clic ouvre l'\xC3\xA9l\xC3\xA9ment dans le projet, s'il existe encore. Les noms sont ceux du moment du d\xC3\xA9" "faut.", small,
                   Tone::Muted);
            B.y += 2.f;
            if (s.chain.empty()) B.para(X, W, "Aucune d\xC3\xA9pendance lue dans le projet.", f, Tone::Muted);
            const auto* project = pane_.doc_ ? &pane_.doc_->project : nullptr;
            for (std::size_t i = 0; i < s.chain.size(); ++i) {
                const auto& l = s.chain[i];
                const auto t = ui_::targetOf(project, l);
                const float x = X + 4.f + static_cast<float>(std::clamp(l.depth, 1, 4) - 1) * 18.f;
                const float top = B.y;
                const std::string tag = "[" + std::string(inc::certaintyLabel(l.certainty)) + "]";
                B.text(x, B.y + 1.f, tag, small, toneOfWords(tag));
                const float nx = x + r.measure(tag, small).width + 6.f;
                const std::string label = l.kind + " " + l.name;
                B.text(nx, B.y, ellipsize(r, label, f, X + W - nx), f, t.exists ? Tone::Accent : Tone::Muted);
                B.y += B.lh(f) + 1.f;
                std::string more = l.relation;
                if (!l.where.empty()) more += " \xE2\x80\x94 " + l.where;
                if (!l.detail.empty()) more += " \xE2\x80\x94 " + l.detail;
                B.para(nx, X + W - nx, more, small, Tone::Muted);
                if (!t.exists) B.para(nx, X + W - nx, "n'existe plus dans le projet : la fiche garde son nom d'origine", small, Tone::Warning);
                hits_.push_back({{x - 2.f, top - 1.f, X + W - x + 2.f, B.y - top + 1.f}, AReveal, static_cast<int>(i)});
                B.y += 4.f;
            }
        }

        // ---- les analyses ----
        group(B, GAnalyses, "Analyses", std::to_string(it.analyses.size()));
        if (groupOpen_[GAnalyses]) {
            const bool running = pane_.host_ && pane_.host_->analysing() && pane_.host_->analysingFolder() == it.folder;
            if (running) B.para(X, W, "Une analyse est en cours (l'IA choisie dans l'Assistant)\xE2\x80\xA6", f, Tone::Info);
            else if (pane_.analysisWaiting(it.name))
                B.para(X, W, "Nouvelle analyse demand\xC3\xA9" "e : elle attend son tour ; sa r\xC3\xA9ponse sera un fichier de plus.", f, Tone::Info);
            std::vector<std::tuple<std::string, int, int>> bl{{"Demander une nouvelle analyse", AAnalyze, 0}};
            if (it.analyses.size() >= 2)
                bl.emplace_back(compare_ ? "Ne plus comparer" : "Comparer l'initiale et la derni\xC3\xA8re", ACompare, 0);
            B.buttons(bl);
            if (it.analyses.empty()) B.para(X, W, "Aucune analyse pour l'instant.", f, Tone::Muted);
            if (compare_ && it.analyses.size() >= 2) {
                const st::Analysis& a0 = it.analyses.front();
                const st::Analysis* an = it.latest();
                if (!an || an == &a0) an = &it.analyses.back();
                const bool side = W >= 700.f;
                const float cw = side ? (W - 18.f) * 0.5f : W;
                const float top = B.y;
                const auto column = [&](const st::Analysis& a, float x, const char* what) {
                    B.text(x, B.y, std::string(what) + " \xE2\x80\x94 n\xC2\xB0" + std::to_string(a.number), f, Tone::Text, true);
                    B.y += B.lh(f) + 2.f;
                    B.para(x, cw, meta(a), small, Tone::Muted);
                    B.y += 4.f;
                    if (!a.error.empty()) B.para(x, cw, "\xC3\x89" "chec : " + a.error, f, Tone::Warning);
                    B.markdown(x, cw, a.text);
                };
                column(a0, X, "Initiale");
                const float leftEnd = B.y;
                if (side) {
                    B.rect(Prim::K::Fill, {X + cw + 8.f, top, 1.f, 1.f}, Tone::Border);   // remplace apres (la hauteur)
                    const std::size_t sep = prims_.size() - 1;
                    B.y = top;
                    column(*an, X + cw + 18.f, "Derni\xC3\xA8re");
                    B.y = std::max(B.y, leftEnd);
                    prims_[sep].r.h = B.y - top;
                } else {
                    B.y += 8.f;
                    B.rect(Prim::K::Fill, {X, B.y, W, 1.f}, Tone::Border);
                    B.y += 8.f;
                    column(*an, X, "Derni\xC3\xA8re");
                }
                B.y += 6.f;
            } else {
                for (std::size_t k = 0; k < it.analyses.size(); ++k) {
                    const auto& a = it.analyses[k];
                    const bool open = analysisOpen_.count(static_cast<int>(k)) != 0;
                    const std::string title = "n\xC2\xB0" + std::to_string(a.number) + " \xC2\xB7 " + (a.kind.empty() ? std::string("analyse") : a.kind);
                    B.fold(X + 4.f, title, open, AAnalysis, static_cast<int>(k), f, false, a.state == "faite" ? Tone::Text : Tone::Warning, meta(a));
                    if (open) {
                        if (!a.error.empty()) B.para(X + 26.f, W - 26.f, "\xC3\x89" "chec : " + a.error, f, Tone::Warning);
                        B.markdown(X + 26.f, W - 26.f, a.text);
                        B.y += 6.f;
                    }
                }
            }
        }

        // ---- les commentaires ----
        group(B, GComments, "Commentaires", std::to_string(it.comments.size()));
        if (groupOpen_[GComments]) {
            if (it.comments.empty()) B.para(X, W, "Aucun commentaire. Un commentaire s'ajoute au dossier (un fichier de plus) : rien n'est r\xC3\xA9\xC3\xA9" "crit.",
                                            f, Tone::Muted);
            for (const auto& c : it.comments) {
                B.para(X, W, "n\xC2\xB0" + std::to_string(c.number) + " \xC2\xB7 " + ui_::dateText(c.createdAt)
                                 + (c.author.empty() ? std::string{} : " \xC2\xB7 " + c.author),
                       small, Tone::Muted);
                B.markdown(X + 8.f, W - 8.f, c.text);
                B.y += 6.f;
            }
            B.buttons({{"Ajouter un commentaire\xE2\x80\xA6", AComment, 0}});
        }

        // ---- la validation ----
        group(B, GValidation, "Validation", ui_::validationLabel(it));
        if (groupOpen_[GValidation]) {
            if (it.state == st::State::Validated) {
                if (it.validation) {
                    const auto& v = *it.validation;
                    B.para(X, W, "Valid\xC3\xA9 le " + ui_::dateText(v.at), f, Tone::Ok);
                    B.para(X, W, "Par : " + (v.author.empty() ? std::string("\xE2\x80\x94") : v.author), f, Tone::Text);
                    B.para(X, W, "Note : " + (v.note.empty() ? std::string("\xE2\x80\x94") : v.note), f, Tone::Text);
                    if (!v.from.empty()) B.para(X, W, "Depuis : " + v.from, small, Tone::Muted);
                    if (!v.manifestHash.empty()) B.para(X, W, "Empreinte de la liste des empreintes : " + v.manifestHash, small, Tone::Muted);
                } else {
                    B.para(X, W, "Valid\xC3\xA9 (la fiche de validation est illisible).", f, Tone::Warning);
                }
            } else {
                B.para(X, W,
                       "Pas encore valid\xC3\xA9 : l'incident est dans \xC2\xAB en attente \xC2\xBB. Valider le copie dans \xC2\xAB valides \xC2\xBB, "
                       "v\xC3\xA9rifie chaque fichier de la copie, puis seulement retire l'original.",
                       f, Tone::Muted);
                B.y += 2.f;
                B.buttons({{"Valider\xE2\x80\xA6", AValidate, 0}, {"Supprimer\xE2\x80\xA6", ARemove, 0}});
            }
        }
        integrityGroup(B);
        finish(B);
    }

    // 1.21.0 : LES REAPPARITIONS REGROUPEES - le nombre, la premiere et la derniere, la frequence, deux histogrammes
    // (par heure sur 24 h, par minute sur la derniere heure, aux couleurs du theme), le diagnostic en clair (une alarme
    // instable, les pistes) et les dernieres reapparitions. Rien quand l'incident n'en a pas.
    void reappearGroup(Builder& B) {
        if (!item_) return;
        const inc::Burst* b = burstOf(pane_.host_, *item_);
        if (!b || b->count == 0) return;
        const bool live = pane_.host_ && pane_.host_->reappearances(item_->snapshot.id) != nullptr;
        group(B, GReappear, "R\xC3\xA9" "apparitions", badgeText(b->count) + " \xC2\xB7 " + inc::everyText(b->meanGap()));
        if (!groupOpen_[GReappear]) return;
        const auto f = B.th.font.ui;
        const auto small = B.th.font.smallUi;
        const float X = B.x0, W = B.w;
        const double window = pane_.host_ ? pane_.host_->settings().regroup : 0.0;
        B.para(X, W,
               reappearancesWord(b->count) + " apr\xC3\xA8s l'incident, regroup\xC3\xA9" "es"
                   + (window > 0 ? " (fen\xC3\xAAtre de " + inc::durationText(window) + ", glissante)" : std::string{})
                   + " : aucun nouveau dossier, ni image, ni vid\xC3\xA9o."
                   + (live ? " L'alarme bat encore : les chiffres suivent." : " Termin\xC3\xA9" "e (relue du dossier : reapparitions.json)."),
               f, Tone::Text);
        B.para(X, W,
               "Premi\xC3\xA8re : " + ui_::dateText(b->firstAt.view()) + " \xC2\xB7 derni\xC3\xA8re : " + ui_::dateText(b->lastAt.view())
                   + " \xC2\xB7 fr\xC3\xA9quence : " + inc::everyText(b->meanGap()) + " (de " + inc::durationText(b->minGap) + " \xC3\xA0 "
                   + inc::durationText(b->maxGap) + ")",
               small, Tone::Muted);
        B.y += 6.f;
        // les histogrammes : une barre par heure (puis par minute), la derniere a droite
        const auto histogram = [&](const std::string& title, const std::uint32_t* cells, int n, const std::string& left) {
            std::uint32_t top = 0;
            for (int i = 0; i < n; ++i) top = std::max(top, cells[i]);
            B.text(X, B.y, title + " \xE2\x80\x94 au plus " + inc::countText(top), small, Tone::Muted);
            B.y += B.lh(small) + 2.f;
            const float bw = std::min(W, 480.f), bh = 54.f;
            const gfx::Rect box{X, B.y, bw, bh};
            B.rect(Prim::K::Fill, box, Tone::Band);
            const float step = (bw - 8.f) / static_cast<float>(n), gap = step > 6.f ? 2.f : 1.f;
            for (int i = 0; i < n; ++i) {
                if (!cells[i] || !top) continue;
                const float h = std::max(2.f, (bh - 8.f) * static_cast<float>(cells[i]) / static_cast<float>(top));
                B.rect(Prim::K::Fill, {X + 4.f + step * static_cast<float>(i), B.y + bh - 4.f - h, std::max(1.f, step - gap), h},
                       i == n - 1 ? Tone::Error : Tone::Warning);
            }
            B.rect(Prim::K::Fill, {X + 4.f, B.y + bh - 4.f, bw - 8.f, 1.f}, Tone::Border);
            B.rect(Prim::K::Stroke, box, Tone::Border);
            B.y += bh + 2.f;
            B.text(X, B.y, left, small, Tone::Muted);
            const std::string right = "la derni\xC3\xA8re r\xC3\xA9" "apparition";
            B.text(X + bw - B.r.measure(right, small).width, B.y, right, small, Tone::Muted);
            B.y += B.lh(small) + 8.f;
        };
        histogram("Par heure (24 h)", b->hours.data(), inc::Burst::kHours, "il y a 23 h");
        histogram("Par minute (la derni\xC3\xA8re heure)", b->minutes.data(), inc::Burst::kMinutes, "il y a 59 min");
        // le diagnostic, en clair : instable, les pistes (jamais une cause affirmee)
        B.markdown(X, W, inc::burstDiagnosis(*b, &item_->snapshot));
        B.y += 4.f;
        // les dernieres reapparitions (la plus recente d'abord)
        const auto recent = b->recent();
        B.text(X, B.y, "Les derni\xC3\xA8res (" + std::to_string(recent.size()) + " gard\xC3\xA9" "es en d\xC3\xA9tail, dans reapparitions.json) :", f,
               Tone::Text, true);
        B.y += B.lh(f) + 2.f;
        int shown = 0;
        for (auto r = recent.rbegin(); r != recent.rend() && shown < 12; ++r, ++shown)
            B.para(X + 8.f, W - 8.f,
                   "\xE2\x80\xA2 " + (r->at.empty() ? inc::durationText(r->t) : ui_::dateText(r->at.view()))
                       + (r->active >= 0.f ? " \xC2\xB7 active " + inc::durationText(r->active) : std::string(" \xC2\xB7 disparition non vue"))
                       + (r->value.empty() ? std::string{} : " \xC2\xB7 valeur " + r->value.str()),
                   small, Tone::Muted);
        B.y += 4.f;
    }

    // 1.21.0 : LA BARRE DU LECTEUR, mise en page ici (ses zones sont des Hit ; paintPlayer les dessine dans leur etat
    // du moment). La barre de temps (le repere rouge du declenchement), la ligne du temps, puis les commandes - celles de
    // la relecture des enregistrements : Debut, -10 s, -1 image, Lecture / Pause, +1 image, +10 s, Fin ; Declenchement
    // et Boucle ; les vitesses x0,25 a x8. Une rangee qui ne tient pas passe a la ligne (un groupe entier si possible).
    void playerBar(Builder& B, float x, float w) {
        auto& r = B.r;
        const auto f = B.th.font.ui;
        const float top = B.y;
        hits_.push_back({{x, B.y, w, 24.f}, AVSeek, 0});
        B.y += 24.f;
        timeRow_ = {x, B.y, w, B.lh(B.th.font.smallUi) + 2.f};
        B.y += timeRow_.h + 6.f;
        struct Item {
            int   act, arg;
            float w;
        };
        const auto tw = [&](const char* s, float pad, float least) { return std::max(least, r.measure(s, f).width + pad); };
        std::vector<std::vector<Item>> groups;
        groups.push_back({{AVStart, 0, 34.f},
                          {AVBack, 0, tw("\xE2\x88\x92" "10 s", 16.f, 52.f)},
                          {AVPrev, 0, tw("\xE2\x88\x92" "1 image", 16.f, 52.f)},
                          {AVPlay, 0, std::max(tw("Lecture", 40.f, 92.f), tw("Pause", 40.f, 92.f))},
                          {AVNext, 0, tw("+1 image", 16.f, 52.f)},
                          {AVFwd, 0, tw("+10 s", 16.f, 52.f)},
                          {AVEnd, 0, 34.f}});
        std::vector<Item> more;
        if (clock_.hasTrigger()) more.push_back({AVTrigger, 0, tw("D\xC3\xA9" "clenchement", 30.f, 60.f)});
        more.push_back({AVLoop, 0, tw("Boucle", 22.f, 52.f)});
        groups.push_back(std::move(more));
        std::vector<Item> speeds;
        for (int i = 0; i < kSpeedCount; ++i) speeds.push_back({AVSpeed, i, 44.f});
        groups.push_back(std::move(speeds));
        const float h = B.lh(f) + 10.f;
        float cx = x, cy = B.y;
        for (std::size_t g = 0; g < groups.size(); ++g) {
            const float gap = g == 2 ? 2.f : 6.f;   // les vitesses se touchent presque (comme la relecture)
            float gw = -gap;
            for (const auto& it : groups[g]) gw += it.w + gap;
            if (cx > x && cx + gw > x + w) {
                cx = x;
                cy += h + 6.f;
            }
            for (const auto& it : groups[g]) {
                if (cx > x && cx + it.w > x + w) {
                    cx = x;
                    cy += h + 6.f;
                }
                hits_.push_back({{cx, cy, it.w, h}, it.act, it.arg});
                cx += it.w + gap;
            }
            cx += 12.f - gap;
        }
        B.y = cy + h + 8.f;
        Prim p;
        p.k = Prim::K::Player;
        p.r = {x, top, w, B.y - top};
        prims_.push_back(p);
    }

    void integrityGroup(Builder& B) {
        group(B, GIntegrity, "Int\xC3\xA9grit\xC3\xA9", integrity_.empty() ? std::string{} : integrity_.front());
        if (!groupOpen_[GIntegrity]) return;
        const auto f = B.th.font.ui;
        for (std::size_t i = 0; i < integrity_.size(); ++i)
            B.para(B.x0, B.w, integrity_[i], i == 0 ? f : B.th.font.smallUi,
                   i == 0 ? (integrity_[i].rfind("intact", 0) == 0 ? Tone::Ok : Tone::Error) : Tone::Warning);
        B.para(B.x0, B.w,
               "Chaque fichier a son empreinte SHA-256 dans empreintes.sha256 (v\xC3\xA9rifiable aussi hors de l'application : sha256sum -c).",
               B.th.font.smallUi, Tone::Muted);
        B.y += 2.f;
        B.buttons({{"V\xC3\xA9rifier \xC3\xA0 nouveau", AVerify, 0}});
    }

    void group(Builder& B, Group g, const std::string& title, const std::string& note) {
        B.y += 8.f;
        groupY_[static_cast<std::size_t>(g)] = B.y;
        B.fold(B.x0 + 6.f, title, groupOpen_[static_cast<std::size_t>(g)], AGroup, g, B.th.font.ui, true, Tone::Text, note);
        B.y += 2.f;
    }

    void finish(Builder& B) {
        const float h = B.y + 24.f;
        if (std::fabs(h - height_) > 0.5f) {
            height_ = h;
            invalidateLayout();
        }
        if (hover_ >= static_cast<int>(hits_.size())) hover_ = -1;
    }

    static std::string meta(const st::Analysis& a) {
        std::string t = a.source;
        if (!a.createdAt.empty()) t += (t.empty() ? "" : " \xC2\xB7 ") + ui_::dateText(a.createdAt);
        t += (t.empty() ? "" : " \xC2\xB7 ") + (a.state.empty() ? std::string("?") : a.state);
        if (!a.certainty.empty()) t += " \xC2\xB7 certitude : " + std::string(inc::certaintyLabel(inc::certaintyFromKey(a.certainty)));
        return t;
    }

    // L'image : la video a la position du lecteur, sinon la capture - au zoom de view_, tenue dans son cadre ; puis les
    // boutons du zoom par-dessus. 1.21.0 : la position ne vient plus de l'horloge brute (ctx.time) mais de clock_.
    void paintPicture(const ui::PaintContext& ctx, const gfx::Rect& r) {
        const auto& th = ctx.theme;
        const auto small = th.font.smallUi;
        ctx.r.fillRect(r, th.color.windowBg);
        ctx.r.pushClip(r);
        if (player_) {
            std::uint64_t serial = 0;
            if (const hmi::Rgba* fr = player_->src->frameAt(clock_.position, serial); fr && (serial != player_->serial || !player_->tex.v)) {
                if (player_->tex.v && player_->owner == &ctx.r) ctx.r.releaseImage(player_->tex);   // une texture a la fois
                player_->tex = ctx.r.createImage(fr->pixels.data(), fr->width, fr->height);
                player_->owner = &ctx.r;
                player_->serial = serial;
                if (player_->w != fr->width || player_->h != fr->height) {
                    player_->w = fr->width;
                    player_->h = fr->height;
                    dirty_ = true;   // la boite prend les proportions de la video
                    invalidate();    // ... meme en pause
                }
            }
            if (player_->w > 0 && player_->h > 0) {
                imageW_ = player_->w;
                imageH_ = player_->h;
            }
            if (player_->tex.v) ctx.r.drawImage(player_->tex, view_.place(r, player_->w, player_->h));
            if (std::string why; player_->src->failed(&why)) {
                const std::string label = ellipsize(ctx.r, "vid\xC3\xA9o illisible : " + why, small, std::max(0.f, r.w - 24.f));
                const float lw = ctx.r.measure(label, small).width;
                const gfx::Rect tag{r.x + 6.f, r.bottom() - ctx.r.lineHeight(small) - 10.f, lw + 12.f, ctx.r.lineHeight(small) + 6.f};
                ctx.r.fillRoundedRect(tag, th.brand.tooltipBg, 3.f);
                ctx.r.drawText({tag.x + 6.f, tag.y + 3.f}, label, small, th.brand.tooltipText);
            }
        } else if (item_ && item_->hasCapture) {
            const auto pic = Pictures::instance().get(ctx.r, item_->capturePath, 1600);
            if (pic.ready && pic.tex.v) {
                imageW_ = pic.w;
                imageH_ = pic.h;
                ctx.r.drawImage(pic.tex, view_.place(r, pic.w, pic.h));
            } else {
                const bool waiting = !pic.ready && !pic.failed;
                const std::string t = waiting ? std::string("Chargement de la capture\xE2\x80\xA6") : std::string("Capture (aper\xC3\xA7u indisponible ici)");
                const float tw = ctx.r.measure(t, small).width;
                ctx.r.drawText({r.x + (r.w - tw) * 0.5f, r.y + (r.h - ctx.r.lineHeight(small)) * 0.5f}, t, small, th.color.textMuted);
                if (waiting) invalidate();
            }
        }
        ctx.r.popClip();
        paintZoom(ctx);
    }

    // 1.21.0 : les boutons du zoom (moins, Ajuster, plus) et le pourcentage (l'image montree, rapportee a ses pixels).
    void paintZoom(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        const auto small = ctx.theme.font.smallUi;
        const float lh = ctx.r.lineHeight(small);
        for (std::size_t i = 0; i < hits_.size(); ++i) {
            const Hit& h = hits_[i];
            if (h.act < AZoomOut || h.act > AZoomIn) continue;
            const gfx::Rect b{bounds().x + h.r.x, bounds().y + h.r.y, h.r.w, h.r.h};
            const bool over = hover_ == static_cast<int>(i);
            ctx.r.fillRoundedRect(b, (over ? c.selectionBg : c.panelBg).withAlpha(230), 4.f);
            ctx.r.strokeRect(b, c.border, 1.f);
            const std::string t = h.act == AZoomOut ? std::string("\xE2\x88\x92") : h.act == AZoomIn ? std::string("+") : std::string("Ajuster");
            ctx.r.drawText({b.x + (b.w - ctx.r.measure(t, small).width) * 0.5f, b.y + (b.h - lh) * 0.5f}, t, small, c.text);
            if (h.act != AZoomOut) continue;
            const gfx::Rect shown = view_.place(screenPicture(), imageW(), imageH());
            const std::string pct = std::to_string(static_cast<long>(std::lround(shown.w / static_cast<float>(std::max(1, imageW())) * 100.f))) + " %";
            const float pw = ctx.r.measure(pct, small).width + 12.f;
            const gfx::Rect pr{b.x - pw - 4.f, b.y, pw, b.h};
            ctx.r.fillRoundedRect(pr, c.panelBg.withAlpha(230), 4.f);
            ctx.r.drawText({pr.x + 6.f, pr.y + (pr.h - lh) * 0.5f}, pct, small, c.textMuted);
        }
    }

    // 1.21.0 : LA BARRE DU LECTEUR dans son etat du moment - le meme dessin que la barre de relecture des enregistrements
    // (paintReplayChrome) : la piste, le rempli, le repere (rouge : le declenchement), le bouton ; les boutons arrondis,
    // Lecture / Pause en couleur d'accent ; les vitesses en cases ; puis le temps.
    void paintPlayer(const ui::PaintContext& ctx, float ox, float oy) {
        if (!player_) return;
        const auto& col = ctx.theme.color;
        const auto f = ctx.theme.font.ui, small = ctx.theme.font.smallUi;
        const gfx::Color accent = col.accent;
        const gfx::Color red = gfx::Color::rgb(0xE5484D);   // le rouge des reperes d'alarme de la relecture
        const auto tri = [&](float cx, float cy, float s, bool right, gfx::Color ink) {
            const float d = right ? 1.f : -1.f;
            ui::shapes::fillPolygon(ctx.r,
                                    std::vector<gfx::Point>{{cx - d * s * 0.45f, cy - s * 0.55f}, {cx + d * s * 0.55f, cy}, {cx - d * s * 0.45f, cy + s * 0.55f}},
                                    ink);
        };
        const double dur = clock_.duration;
        for (std::size_t i = 0; i < hits_.size(); ++i) {
            const Hit& h = hits_[i];
            if (h.act < AVStart || h.act > AVSeek) continue;
            const gfx::Rect b{ox + h.r.x, oy + h.r.y, h.r.w, h.r.h};
            const bool over = hover_ == static_cast<int>(i);
            if (h.act == AVSeek) {   // la piste, le rempli, le declenchement, le bouton
                const gfx::Rect pr{b.x + 7.f, b.y + 4.f, std::max(10.f, b.w - 14.f), 16.f};
                const float trackY = pr.y + 6.f;
                ctx.r.fillRoundedRect({pr.x, trackY, pr.w, 4.f}, col.inputBg, 2.f);
                const double frac = dur > 0 ? std::clamp(clock_.position / dur, 0.0, 1.0) : 0.0;
                const float knobX = pr.x + static_cast<float>(frac) * pr.w;
                if (knobX - pr.x > 0.5f) ctx.r.fillRoundedRect({pr.x, trackY, knobX - pr.x, 4.f}, accent, 2.f);
                if (clock_.hasTrigger() && dur > 0) {
                    const float mx = pr.x + static_cast<float>(std::clamp(clock_.trigger / dur, 0.0, 1.0)) * pr.w;
                    ctx.r.fillRect({mx - 1.f, pr.y, 2.f, 16.f}, red);
                }
                ctx.r.fillRoundedRect({knobX - 7.f, pr.y + 1.f, 14.f, 14.f}, over || seeking_ ? col.accentHover : accent, 7.f);
                ctx.r.fillRoundedRect({knobX - 5.f, pr.y + 3.f, 10.f, 10.f}, col.text, 5.f);
                continue;
            }
            if (h.act == AVSpeed) {   // une case par vitesse, celle du moment en couleur d'accent
                const double v = hmi::rec::kSpeeds[static_cast<std::size_t>(std::clamp(h.arg, 0, kSpeedCount - 1))];
                const bool on = std::fabs(v - clock_.speed) < 1e-6;
                ctx.r.fillRect(b, on ? accent : over ? col.selectionBg : col.headerBg);
                ctx.r.strokeRect(b, col.border, 1.f);
                const std::string t = ui_::speedLabel(v);
                ctx.r.drawText({b.x + (b.w - ctx.r.measure(t, small).width) / 2.f, b.y + (b.h - ctx.r.lineHeight(small)) / 2.f}, t, small,
                               on ? col.textInverted : col.textMuted);
                continue;
            }
            const bool primary = h.act == AVPlay, on = h.act == AVLoop && clock_.loop;
            ctx.r.fillRoundedRect(b, primary || on ? (over ? col.accentHover : accent) : (over ? col.selectionBg : col.headerBg), 4.f);
            if (!primary && !on) ctx.r.strokeRect(b, col.border, 1.f);
            const gfx::Color ink = primary || on ? col.textInverted : col.text;
            const float s = std::min(12.f, b.h * 0.45f), cy = b.y + b.h / 2.f, textY = b.y + (b.h - ctx.r.lineHeight(f)) / 2.f;
            const auto centered = [&](const std::string& t) { ctx.r.drawText({b.x + (b.w - ctx.r.measure(t, f).width) / 2.f, textY}, t, f, ink); };
            switch (h.act) {
                case AVStart:   // |<
                    ctx.r.fillRect({b.x + b.w / 2.f - s * 0.65f, cy - s * 0.55f, 2.f, s * 1.1f}, ink);
                    tri(b.x + b.w / 2.f + s * 0.15f, cy, s, false, ink);
                    break;
                case AVEnd:     // >|
                    tri(b.x + b.w / 2.f - s * 0.15f, cy, s, true, ink);
                    ctx.r.fillRect({b.x + b.w / 2.f + s * 0.45f, cy - s * 0.55f, 2.f, s * 1.1f}, ink);
                    break;
                case AVBack: centered("\xE2\x88\x92" "10 s"); break;
                case AVFwd:  centered("+10 s"); break;
                case AVPrev: centered("\xE2\x88\x92" "1 image"); break;
                case AVNext: centered("+1 image"); break;
                case AVLoop: centered("Boucle"); break;
                case AVPlay: {
                    const bool paused = !clock_.playing;
                    const std::string t = paused ? "Lecture" : "Pause";
                    const float gx = b.x + (b.w - ctx.r.measure(t, f).width - s - 8.f) / 2.f;
                    if (paused) {
                        tri(gx + s / 2.f, cy, s, true, ink);
                    } else {
                        ctx.r.fillRect({gx, cy - s * 0.5f, s * 0.32f, s}, ink);
                        ctx.r.fillRect({gx + s * 0.6f, cy - s * 0.5f, s * 0.32f, s}, ink);
                    }
                    ctx.r.drawText({gx + s + 8.f, textY}, t, f, ink);
                    break;
                }
                case AVTrigger: {   // le repere rouge de la barre, puis le mot
                    const std::string t = "D\xC3\xA9" "clenchement";
                    const float gx = b.x + (b.w - ctx.r.measure(t, f).width - 10.f) / 2.f;
                    ctx.r.fillRect({gx, cy - s * 0.6f, 3.f, s * 1.2f}, red);
                    ctx.r.drawText({gx + 10.f, textY}, t, f, ink);
                    break;
                }
                default: break;
            }
        }
        // le temps : la position et la duree (au dixieme) ; a droite, t (depuis le declenchement) et l'image montree
        if (timeRow_.w > 0.f) {
            const gfx::Rect tr{ox + timeRow_.x, oy + timeRow_.y, timeRow_.w, timeRow_.h};
            const std::string left = ui_::clockTenths(clock_.position) + " / " + ui_::clockTenths(dur) + (clock_.ended ? "  (fin)" : "");
            ctx.r.drawText({tr.x + 2.f, tr.y + 1.f}, left, small, col.text);
            std::string right;
            if (clock_.hasTrigger()) right = "t = " + ui_::signedSeconds(clock_.sinceTrigger());
            if (const int n = clock_.frames(); n > 0)
                right += (right.empty() ? "" : " \xC2\xB7 ") + std::string("image ") + std::to_string(clock_.frame() + 1) + " / " + std::to_string(n);
            right = ellipsize(ctx.r, right, small, tr.w - ctx.r.measure(left, small).width - 18.f);
            if (!right.empty()) {
                const bool atTrigger = clock_.hasTrigger() && std::fabs(clock_.sinceTrigger()) < 0.05;
                ctx.r.drawText({tr.right() - 2.f - ctx.r.measure(right, small).width, tr.y + 1.f}, right, small, atTrigger ? red : col.textMuted);
            }
        }
    }

    HmiIncidentsPane&                               pane_;
    const st::Item*                                 item_{nullptr};
    std::string                                     name_;
    std::array<bool, GCount>                        groupOpen_{};
    std::array<float, GCount>                       groupY_{};
    std::set<int>                                   sectionOpen_, analysisOpen_;
    bool                                            compare_{false};
    std::vector<Prim>                               prims_;
    std::vector<Hit>                                hits_;
    float                                           height_{200.f}, builtWidth_{-1.f};
    bool                                            dirty_{true};
    int                                             hover_{-1};
    int                                             wantGroup_{-1};
    float                                           wantScroll_{-1.f};
    std::vector<std::string>                        integrity_;
    std::string                                     integritySig_;
    std::unique_ptr<Player>                         player_;
    std::vector<std::pair<gfx::IRenderer*, gfx::TextureId>> dropped_;
    // 1.21.0 : le lecteur (position, vitesse, boucle - avance par onPaint) et le zoom de l'image
    ui_::VideoClock                                 clock_;
    double                                          lastTick_{-1.0};   // ctx.time de la derniere avance (-1 : aucune)
    ui_::ImageView                                  view_;
    gfx::Rect                                       picture_{}, timeRow_{};   // relatifs a la fiche (vides : pas d'image)
    int                                             imageW_{0}, imageH_{0};  // l'image dessinee (le zoom)
    bool                                            seeking_{false}, panning_{false};
    bool                                            signKey_{false};   // un + ou - tape : son caractere ne recompte pas
    gfx::Point                                      panFrom_{};
};

// ============================================================================================== le volet ==
namespace {
enum Tool : int { TRefresh = 1, TConsult, TFull, TChain, TComment, TAnalyze, TValidate, TDelete, TFolder, TSettings, TOnOff /* 1.20.2 */ };
enum MenuItem : int { MConsult = 1, MFull, MChain, MComment, MAnalyze, MValidate, MDelete, MFolder, MVerify, MToggle /* 1.20.2 */ };
constexpr int kIncidentPathId = 900;   // 1.24.1 : Ouvrir dans l'explorateur, Aller a, Copier le chemin (900..902)
} // namespace

HmiIncidentsPane::HmiIncidentsPane(std::string id, hmi::DocumentPtr doc, IncidentHost* host)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), host_(host) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(TRefresh, HmiGlyph::Refresh, "Actualiser : relire les dossiers d'incident sur le disque", "Actualiser");
    tools->separator();
    tools->add(TConsult, HmiGlyph::View, "Consulter : la fiche de l'incident choisi, depuis le haut (Entr\xC3\xA9" "e, double-clic)", "Consulter");
    tools->add(TFull, HmiGlyph::Search, "Voir l'analyse compl\xC3\xA8te : les dix sections de la fiche et la derni\xC3\xA8re analyse, d\xC3\xA9pli\xC3\xA9" "es",
               "Analyse compl\xC3\xA8te");
    tools->add(TChain, HmiGlyph::Network,
               "Explorer les d\xC3\xA9pendances : la cha\xC3\xAEne de l'alarme (variables, \xC3\xA9quipements, scripts, vues...) ; un clic ouvre "
               "l'\xC3\xA9l\xC3\xA9ment dans le projet",
               "D\xC3\xA9pendances");
    tools->separator();
    tools->add(TComment, HmiGlyph::Text, "Ajouter un commentaire : un fichier de plus dans le dossier (rien n'est r\xC3\xA9\xC3\xA9" "crit)", "Commenter\xE2\x80\xA6");
    tools->add(TAnalyze, HmiGlyph::Diagnostic,
               "Demander une nouvelle analyse : l'IA choisie dans l'Assistant (sinon la fiche d\xC3\xA9terministe) ; l'initiale ne change pas",
               "Nouvelle analyse");
    tools->add(TValidate, HmiGlyph::Check,
               "Valider : une copie v\xC3\xA9rifi\xC3\xA9" "e (empreintes SHA-256) va dans \xC2\xAB valides \xC2\xBB, puis l'original est retir\xC3\xA9",
               "Valider\xE2\x80\xA6");
    tools->add(TDelete, HmiGlyph::Delete,
               "Supprimer l'incident en attente : son dossier entier (un incident valid\xC3\xA9 ne se supprime pas d'ici ; un dossier d'alarme "
               "choisi : l'incident qu'il montre, pas tout le dossier)",
               "Supprimer");
    tools->separator();
    tools->add(TFolder, HmiGlyph::Link, "Ouvrir le dossier de l'incident choisi (sans choix : celui des incidents en attente)", "Ouvrir le dossier");
    tools->add(TSettings, HmiGlyph::Gear,
               "R\xC3\xA9glages de la surveillance : activ\xC3\xA9" "e, les alarmes surveill\xC3\xA9" "es ou ignor\xC3\xA9" "es, les secondes avant et "
               "apr\xC3\xA8s, les images par seconde, le maintien, l'anti-rebond, le regroupement des r\xC3\xA9" "apparitions, le plafond horaire, "
               "le quota des images et vid\xC3\xA9os",
               "R\xC3\xA9glages\xE2\x80\xA6");
    // 1.20.2 : L'INTERRUPTEUR GENERAL - arreter (ou reprendre) toute creation d'incident, en un clic ; garde avec le projet
    // (le reglage << surveillance active >>).
    tools->separator();
    tools->add(TOnOff, HmiGlyph::Stop,
               "Arr\xC3\xAAter la cr\xC3\xA9" "ation des incidents : plus aucun dossier n'est cr\xC3\xA9\xC3\xA9, quelle que soit l'alarme "
               "(les incidents d\xC3\xA9j\xC3\xA0 enregistr\xC3\xA9s restent) ; un second clic la reprend",
               "Arr\xC3\xAAter les incidents");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(TOnOff, [this] { return host_ != nullptr; });
    // 1.24.1 : plusieurs incidents choisis (Ctrl+clic, Maj+clic) - Supprimer les supprime tous (une question : definitif) ;
    // le reste : un a la fois.
    for (const int a : {TConsult, TFull, TChain, TComment, TAnalyze})
        tools_->setEnabledWhen(a, [this] { return selected() != nullptr && chosenIncidents().size() <= 1; });
    tools_->setEnabledWhen(TValidate, [this] {
        const auto* it = selected();
        return it && it->state == st::State::Pending && chosenIncidents().size() <= 1;
    });
    tools_->setEnabledWhen(TDelete, [this] {
        for (const auto* it : chosenIncidents())
            if (it->state == st::State::Pending) return true;
        return false;
    });
    tools_->setLabelWhen(TDelete, [this] { return several::label("Supprimer", chosenIncidents().size()); });
    tools_->setEnabledWhen(TRefresh, [this] { return host_ != nullptr; });
    tools_->setEnabledWhen(TSettings, [this] { return host_ != nullptr; });

    // 1.20.2 : LA LIGNE DES FILTRES - la recherche de toute l'application, la criticite, le tri.
    search_ = &static_cast<ui::SearchField&>(addChild(std::make_unique<ui::SearchField>(
        base + ".search", "Rechercher : alarme, \xC3\xA9quipement, vue, synth\xC3\xA8se, commentaire, date\xE2\x80\xA6",
        "Chaque mot est cherch\xC3\xA9 dans l'alarme, l'\xC3\xA9quipement, la vue, la synth\xC3\xA8se, les commentaires, la date, le nom du "
        "dossier, le groupe, la cat\xC3\xA9gorie et la criticit\xC3\xA9 de chaque incident - tous les mots (ET), sans casse ni accents ; "
        "\"une phrase\" entre guillemets ; -mot : l'exclure.")));
    {
        auto crit = std::make_unique<ui::DropDown>(base + ".criticite");
        std::vector<ui::DropDown::Item> levels{{"Toutes les criticit\xC3\xA9s", "", {}, true}};
        for (int p = 1; p <= hmi::kAlarmPriorities; ++p) levels.push_back({std::string(hmi::alarmPriorityLabel(p)), std::to_string(p), {}, true});
        crit->setItems(std::move(levels));
        crit->setSelectedIndex(0);
        crit->setTooltip("Criticit\xC3\xA9 : seulement les incidents des alarmes de cette priorit\xC3\xA9 (Critique, Haute, Moyenne, Basse)");
        criticality_ = &static_cast<ui::DropDown&>(addChild(std::move(crit)));
        auto order = std::make_unique<ui::DropDown>(base + ".tri");
        order->setItems({{"Plus r\xC3\xA9" "cents d'abord", "recents", {}, true},
                        {"Plus anciens d'abord", "anciens", {}, true},
                        {"Criticit\xC3\xA9 (critique d'abord)", "criticite", {}, true},
                        {"Alarme (A \xE2\x86\x92 Z)", "alarme", {}, true},
                        {"Nombre d'incidents", "nombre", {}, true},
                        {"Nombre de r\xC3\xA9" "apparitions", "reapparitions", {}, true}});   // 1.21.0
        order->setSelectedIndex(SortRecent);
        order->setTooltip("Trier les lignes : les dossiers d'alarme et les incidents seuls ensemble ; dans un dossier, le plus r\xC3\xA9" "cent "
                         "d'abord");
        sort_ = &static_cast<ui::DropDown&>(addChild(std::move(order)));
    }

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    auto tabs = std::make_unique<ui::TabControl>(base + ".onglets");
    for (int t = 0; t < 2; ++t) {
        auto scroll = std::make_unique<ui::ScrollablePanel>(base + (t ? ".defile-valides" : ".defile-attente"));
        scroll->setScrollPolicy(false, true);
        auto list = std::make_unique<HmiIncidentList>(*this, t);
        lists_[t] = list.get();
        scroll->setContent(std::move(list));
        scrolls_[t] = scroll.get();
        tabs->addTab({t ? "Valid\xC3\xA9s" : "En attente", t ? ui::Icon::Ok : ui::Icon::Warning, false, false}, std::move(scroll));
    }
    tabs_ = &static_cast<ui::TabControl&>(split->addPane(std::move(tabs), 0.42f, 300.f));
    auto sheetScroll = std::make_unique<ui::ScrollablePanel>(base + ".defile-fiche");
    sheetScroll->setScrollPolicy(false, true);
    auto sheet = std::make_unique<HmiIncidentSheet>(*this);
    sheet_ = sheet.get();
    sheetScroll->setContent(std::move(sheet));
    sheetScroll_ = &static_cast<ui::ScrollablePanel&>(split->addPane(std::move(sheetScroll), 0.58f, 320.f));
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        std::string why;
        switch (a) {
            case TRefresh:
                refresh();
                say("Relecture des dossiers d'incident\xE2\x80\xA6");
                break;
            case TConsult: consult(); break;
            case TFull: showFullAnalysis(); break;
            case TChain: exploreDependencies(); break;
            case TComment: askComment(); break;
            case TAnalyze:
                if (!analyzeSelected(&why)) say(why, true);
                break;
            case TValidate: askValidate(); break;
            case TDelete: askRemove(); break;
            case TFolder: openSelectedFolder(); break;
            case TSettings:
                if (hosts_.settings) hosts_.settings();
                else say("Les r\xC3\xA9glages se changent depuis IHM Studio.", true);
                break;
            case TOnOff: (void)setIncidentsEnabled(!incidentsEnabled()); break;   // 1.20.2

            default: break;
        }
    });
    links_ += tabs_->currentChanged->connect([this](std::size_t i) {
        validatedTab_ = i == 1;
        showSelected();
        updateStatus();   // 1.20.2 : le compte de la recherche suit l'onglet
    });
    if (doc_) links_ += doc_->changed->connect([this](hmi::Id) { sheet_->dirty(); });   // un element renomme, supprime
    // 1.20.2 : la recherche, la criticite, le tri refont les lignes ; la recherche branchee, puis relue (retenue d'une
    // seance a l'autre, comme les autres volets).
    links_ += criticality_->selectionChanged->connect([this](int) { refilter(); });
    links_ += sort_->selectionChanged->connect([this](int) { refilter(); });
    links_ += search_->changed->connect([this] { refilter(); });
    search_->recall();
    (void)poll();
    refresh();
}

HmiIncidentsPane::~HmiIncidentsPane() = default;

std::string HmiIncidentsPane::author() const { return hosts_.author ? hosts_.author() : std::string{}; }

bool HmiIncidentsPane::analysisWaiting(const std::string& name) const {
    return requested_.count(name) != 0 && host_ && (host_->analysing() || host_->queuedAnalyses() > 0);
}

void HmiIncidentsPane::say(std::string text, bool warning) {
    message_ = std::move(text);
    if (status_) status_->setTransientMessage(message_, 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
    invalidate();
}

void HmiIncidentsPane::refresh() {
    if (host_) host_->refresh();
}

bool HmiIncidentsPane::poll() {
    if (!host_) return false;
    auto l = host_->listing();
    if (!l || (l == listing_ && l->revision == revision_)) return false;
    listing_ = std::move(l);
    revision_ = listing_->revision;
    items_[0] = &listing_->pending;
    items_[1] = &listing_->validated;
    // la selection suit le NOM (une relecture refait la liste) ; disparu (valide, supprime) : la ligne d'a cote.
    // 1.20.2 : par les lignes affichees (la recherche, la criticite, les dossiers d'alarme, le tri).
    for (int t = 0; t < 2; ++t) {
        rebuildRows(t);
        placeSelection(t);
        lists_[t]->invalidateLayout();
    }
    // les analyses demandees d'ici qui sont arrivees
    for (auto it = requested_.begin(); it != requested_.end();) {
        bool arrived = true;
        for (const auto* v : {items_[0], items_[1]})
            for (const auto& item : *v)
                if (item.name == *it) arrived = item.analyses.size() > analysesSeen_[*it];
        if (arrived || !analysisWaiting(*it)) {   // arrivee, ou abandonnee par l'hote (deja faite pour ce contexte)
            analysesSeen_.erase(*it);
            it = requested_.erase(it);
        } else {
            ++it;
        }
    }
    tabs_->setTabBadge(0, std::to_string(items_[0]->size()), items_[0]->empty() ? ui::Tone::None : ui::Tone::Warning);
    tabs_->setTabBadge(1, std::to_string(items_[1]->size()));   // les badges : TOUS les incidents (les filtres n'y font rien)
    updateStatus();
    showSelected();
    invalidate();
    return true;
}

void HmiIncidentsPane::showValidated(bool validated) {
    validatedTab_ = validated;
    if (tabs_ && tabs_->currentIndex() != (validated ? 1u : 0u)) tabs_->setCurrentIndex(validated ? 1 : 0);
    showSelected();
    updateStatus();
}

std::size_t HmiIncidentsPane::rowCount() const { return rows_[validatedTab_ ? 1 : 0].size(); }
std::size_t HmiIncidentsPane::pendingCount() const { return items_[0] ? items_[0]->size() : 0; }
std::size_t HmiIncidentsPane::validatedCount() const { return items_[1] ? items_[1]->size() : 0; }
std::size_t HmiIncidentsPane::shownCount() const { return shown_[validatedTab_ ? 1 : 0]; }
int HmiIncidentsPane::selectedRow() const { return row_[validatedTab_ ? 1 : 0]; }

const st::Item* HmiIncidentsPane::selected() const {
    const int t = validatedTab_ ? 1 : 0;
    return rowItem(t, row_[t]);
}

std::string HmiIncidentsPane::selectedName() const {
    const auto* it = selected();
    return it ? it->name : std::string{};
}

void HmiIncidentsPane::selectRow(int tab, int row) {
    const Row* r = row >= 0 ? rowAt(tab, static_cast<std::size_t>(row)) : nullptr;
    if (!r) return;
    row_[tab] = row;
    // 1.20.2 : un dossier deplie choisi montre son plus recent ; replie, celui qu'il cache deja (sinon son plus recent)
    const st::Item* it = r->group && r->open ? itemAt(tab, r->item) : rowItem(tab, row);
    if (it) chosen_[tab] = it->name;
    lists_[tab]->reveal(row);
    if ((tab == 1) == validatedTab_) {
        showSelected();
        updateStatus();
    }
    invalidate();
}

bool HmiIncidentsPane::select(const std::string& key) {
    for (const int t : {validatedTab_ ? 1 : 0, validatedTab_ ? 0 : 1}) {
        const auto* v = items_[t];
        if (!v) continue;
        for (std::size_t i = 0; i < v->size(); ++i)
            if ((*v)[i].name == key || (*v)[i].folder == key) {
                // 1.20.2 : sa ligne, ou son dossier ferme ; cache par la recherche ou la criticite : pas choisi
                const std::string name = (*v)[i].name, before = chosen_[t];
                const int beforeRow = row_[t];
                chosen_[t] = name;
                placeSelection(t);
                if (chosen_[t] != name || row_[t] < 0) {
                    chosen_[t] = before;
                    row_[t] = beforeRow;
                    return false;
                }
                if ((t == 1) != validatedTab_) showValidated(t == 1);
                selectRow(t, row_[t]);
                return true;
            }
    }
    return false;
}

std::string HmiIncidentsPane::rowText(std::size_t row) const {
    const int t = validatedTab_ ? 1 : 0;
    const Row* r = rowAt(t, row);
    const st::Item* item = r ? itemAt(t, r->item) : nullptr;
    if (!item) return {};
    const auto& it = *item;
    if (r->group) {   // 1.20.2 : une ligne-dossier
        std::uint64_t again = 0;   // 1.21.0
        for (const int m : r->members)
            if (const auto* x = itemAt(t, m)) again += reappearancesOf(host_, *x);
        return "Dossier | " + r->alarm + " | " + incidentsWord(r->members.size()) + " | " + criticalityLabel(r->criticality) + " | dernier : "
               + incidentsui::dateText(incidentsui::stampOf(it)) + " | " + it.name + (again ? " | " + reappearancesWord(again) : std::string{});
    }
    const std::uint64_t again = reappearancesOf(host_, it);   // 1.21.0 : une alarme qui bat
    return it.name + " | " + incidentsui::dateText(it.snapshot.createdAt) + " | " + incidentsui::alarmOf(it) + " | " + it.equipment() + " | "
           + it.snapshot.openView + " | " + it.synthesis() + " | " + it.certainty() + " | " + it.analysisState() + " | "
           + incidentsui::validationLabel(it) + (again ? " | " + reappearancesWord(again) : std::string{});
}

void HmiIncidentsPane::showSelected() {
    if (sheet_) sheet_->setItem(selected());
}

// ---- 1.20.2 : les lignes affichees (les dossiers par alarme, la recherche, la criticite, le tri) ----
bool HmiIncidentsPane::rowIsGroup(std::size_t row) const {
    const Row* r = rowAt(validatedTab_ ? 1 : 0, row);
    return r && r->group;
}
bool HmiIncidentsPane::rowIsOpen(std::size_t row) const {
    const Row* r = rowAt(validatedTab_ ? 1 : 0, row);
    return r && r->group && r->open;
}
int HmiIncidentsPane::rowDepth(std::size_t row) const {
    const Row* r = rowAt(validatedTab_ ? 1 : 0, row);
    return r ? r->depth : 0;
}
std::size_t HmiIncidentsPane::rowIncidents(std::size_t row) const {
    const Row* r = rowAt(validatedTab_ ? 1 : 0, row);
    return !r ? 0u : r->group ? r->members.size() : 1u;
}
void HmiIncidentsPane::toggleGroup(std::size_t row) { toggleGroupAt(validatedTab_ ? 1 : 0, static_cast<int>(row)); }

const st::Item* HmiIncidentsPane::itemAt(int tab, int index) const {
    const auto* v = items_[tab];
    return v && index >= 0 && static_cast<std::size_t>(index) < v->size() ? &(*v)[static_cast<std::size_t>(index)] : nullptr;
}

const HmiIncidentsPane::Row* HmiIncidentsPane::rowAt(int tab, std::size_t row) const {
    return row < rows_[tab].size() ? &rows_[tab][row] : nullptr;
}

const st::Item* HmiIncidentsPane::rowItem(int tab, int row) const {
    const Row* r = row >= 0 ? rowAt(tab, static_cast<std::size_t>(row)) : nullptr;
    if (!r) return nullptr;
    if (r->group)   // un dossier : l'incident choisi s'il en est, sinon le plus recent
        for (const int m : r->members)
            if (const auto* it = itemAt(tab, m); it && it->name == chosen_[tab]) return it;
    return itemAt(tab, r->item);
}

bool HmiIncidentsPane::filtering() const {
    return (search_ && !search_->query().empty()) || (criticality_ && criticality_->selectedIndex() > 0);
}

void HmiIncidentsPane::rebuildRows(int tab) {
    auto& out = rows_[tab];
    out.clear();
    shown_[tab] = 0;
    const auto* v = items_[tab];
    if (!v) return;
    const ui::SearchQuery none;
    const ui::SearchQuery& query = search_ ? search_->query() : none;
    const int wanted = criticality_ ? std::max(0, criticality_->selectedIndex()) : 0;   // 0 : toutes
    const int order = sort_ ? std::max(0, sort_->selectedIndex()) : static_cast<int>(SortRecent);
    // 1. ce que la criticite et la recherche retiennent ; 2. rassemble par alarme (pliee : sans casse ni accents), dans
    //    l'ordre de la liste - le plus recent d'abord. Un incident illisible (sans alarme) reste seul.
    struct Entry {
        std::string      key, alarm;
        std::vector<int> members;
        int              criticality{0};
        std::uint64_t    reappear{0};   // 1.21.0 : les reapparitions regroupees de ses incidents
    };
    std::vector<Entry> entries;
    std::map<std::string, std::size_t> byKey;
    for (std::size_t i = 0; i < v->size(); ++i) {
        const auto& it = (*v)[i];
        const int crit = incidentsui::criticalityOf(it);
        if (wanted > 0 && crit != wanted) continue;
        if (!query.empty()) {
            const std::string stamp = incidentsui::stampOf(it);
            std::vector<std::string> texts{it.name,
                                           incidentsui::alarmOf(it),
                                           it.equipment(),
                                           it.snapshot.openView,
                                           it.synthesis(),
                                           incidentsui::dateText(stamp),
                                           stamp,
                                           it.snapshot.alarm.group,
                                           it.snapshot.alarm.category,
                                           it.snapshot.alarm.message,
                                           it.snapshot.alarm.description,
                                           crit ? std::string(hmi::alarmPriorityLabel(crit)) : std::string{},
                                           it.certainty(),
                                           it.analysisState(),
                                           incidentsui::validationLabel(it)};
            if (!it.readable) texts.push_back(it.why);
            for (const auto& c : it.comments) {
                texts.push_back(c.text);
                texts.push_back(c.author);
            }
            if (!query.matches(texts)) continue;
        }
        ++shown_[tab];
        const int index = static_cast<int>(i);
        const std::string alarm = it.readable ? incidentsui::alarmOf(it) : std::string{};
        const std::string key = alarm.empty() ? std::string{} : ui::foldForSearch(alarm);
        if (!key.empty()) {
            if (const auto found = byKey.find(key); found != byKey.end()) {
                auto& e = entries[found->second];
                e.members.push_back(index);
                e.reappear += reappearancesOf(host_, it);
                if (crit > 0 && (e.criticality == 0 || crit < e.criticality)) e.criticality = crit;
                continue;
            }
            byKey.emplace(key, entries.size());
        }
        entries.push_back({key, alarm, {index}, crit, reappearancesOf(host_, it)});
    }
    // 3. le tri des lignes du haut (dossiers et incidents seuls ensemble) ; a egalite, le plus recent d'abord. La liste
    //    de l'hote va du plus recent au plus ancien : l'index du premier membre est la date du plus recent.
    const auto rank = [](int crit) { return crit > 0 ? crit : hmi::kAlarmPriorities + 1; };   // l'inconnue en dernier
    std::sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
        const int na = a.members.front(), nb = b.members.front();
        switch (order) {
            case SortOldest: return na > nb;
            case SortCriticality:
                if (rank(a.criticality) != rank(b.criticality)) return rank(a.criticality) < rank(b.criticality);
                break;
            case SortAlarm: {
                const bool ea = a.alarm.empty(), eb = b.alarm.empty();   // sans alarme : en dernier
                if (ea != eb) return eb;
                const std::string fa = ui::foldForSearch(a.alarm), fb = ui::foldForSearch(b.alarm);
                if (fa != fb) return fa < fb;
                break;
            }
            case SortCount:
                if (a.members.size() != b.members.size()) return a.members.size() > b.members.size();
                break;
            case SortReappear:   // 1.21.0 : les alarmes qui battent le plus d'abord
                if (a.reappear != b.reappear) return a.reappear > b.reappear;
                break;
            default: break;
        }
        return na < nb;
    });
    // 4. les lignes : un dossier pour deux incidents ou plus de la meme alarme ; deplie, ses incidents en retrait (le
    //    plus recent d'abord, quel que soit le tri)
    for (auto& e : entries) {
        Row r;
        r.key = e.key;
        r.alarm = e.alarm;
        r.item = e.members.front();
        r.criticality = e.criticality;
        if (e.members.size() < 2) {
            out.push_back(std::move(r));
            continue;
        }
        r.group = true;
        r.open = openGroups_[tab].count(e.key) != 0;
        r.members = e.members;
        const bool open = r.open;
        out.push_back(std::move(r));
        if (!open) continue;
        for (const int m : e.members) {
            Row child;
            child.key = e.key;
            child.alarm = e.alarm;
            child.item = m;
            child.depth = 1;
            if (const auto* it = itemAt(tab, m)) child.criticality = incidentsui::criticalityOf(*it);
            out.push_back(std::move(child));
        }
    }
}

void HmiIncidentsPane::placeSelection(int tab) {
    const auto& rows = rows_[tab];
    if (rows.empty()) {
        // rien d'affiche : plus d'incident (le choix s'oublie), ou tout cache par un filtre (il revient avec la liste)
        row_[tab] = -1;
        if (!items_[tab] || items_[tab]->empty()) chosen_[tab].clear();
        return;
    }
    const auto named = [&](int index) {
        const auto* it = itemAt(tab, index);
        return it && !chosen_[tab].empty() && it->name == chosen_[tab];
    };
    int found = -1;
    // la ligne de l'incident choisi ; cache dans un dossier ferme : le dossier (l'incident reste celui des gestes)
    for (std::size_t r = 0; r < rows.size() && found < 0; ++r)
        if (!rows[r].group && named(rows[r].item)) found = static_cast<int>(r);
    for (std::size_t r = 0; r < rows.size() && found < 0; ++r)
        if (rows[r].group && !rows[r].open && std::any_of(rows[r].members.begin(), rows[r].members.end(), named)) found = static_cast<int>(r);
    if (found < 0) found = std::clamp(row_[tab], 0, static_cast<int>(rows.size()) - 1);   // disparu ou cache : la ligne d'a cote
    row_[tab] = found;
    if (const auto* it = rowItem(tab, found)) chosen_[tab] = it->name;
}

void HmiIncidentsPane::refilter() {
    for (int t = 0; t < 2; ++t) {
        rebuildRows(t);
        placeSelection(t);
        if (lists_[t]) {
            lists_[t]->invalidateLayout();
            lists_[t]->reveal(row_[t]);
        }
    }
    showSelected();
    updateStatus();
    invalidate();
}

void HmiIncidentsPane::toggleGroupAt(int tab, int row) {
    const Row* r = row >= 0 ? rowAt(tab, static_cast<std::size_t>(row)) : nullptr;
    if (!r || !r->group) return;
    setGroupOpen(tab, r->key, !r->open);
}

void HmiIncidentsPane::setGroupOpen(int tab, std::string key, bool open) {
    if (key.empty()) return;
    if (open) openGroups_[tab].insert(key);
    else openGroups_[tab].erase(key);
    // l'incident choisi garde sa ligne ; un dossier replie sur lui le montre a sa place, deplie il le rend
    rebuildRows(tab);
    placeSelection(tab);
    lists_[tab]->invalidateLayout();
    lists_[tab]->reveal(row_[tab]);
    if ((tab == 1) == validatedTab_) {
        showSelected();
        updateStatus();
    }
    invalidate();
}

bool HmiIncidentsPane::incidentsEnabled() const { return host_ && host_->settings().enabled; }

bool HmiIncidentsPane::setIncidentsEnabled(bool on, std::string* why) {
    if (!host_) {
        if (why) *why = "pas de surveillance ici";
        return false;
    }
    auto s = host_->settings();
    if (s.enabled == on) return true;
    s.enabled = on;
    std::string err;
    if (!host_->setSettings(s, &err)) {
        say("Incidents : " + err, true);
        if (why) *why = err;
        return false;
    }
    say(on ? std::string("Cr\xC3\xA9" "ation des incidents reprise : chaque alarme coch\xC3\xA9" "e \xC2\xAB Incidents \xC2\xBB fera de nouveau son dossier.")
           : std::string("Cr\xC3\xA9" "ation des incidents arr\xC3\xAAt\xC3\xA9" "e : plus aucun dossier n'est cr\xC3\xA9\xC3\xA9 (les incidents d\xC3\xA9j\xC3\xA0 "
                         "enregistr\xC3\xA9s restent). Un clic sur \xC2\xAB Reprendre les incidents \xC2\xBB la relance."));
    updateStatus();
    return true;
}

void HmiIncidentsPane::updateStatus() {
    // 1.20.2 : l'interrupteur dit ce qu'il fera
    if (tools_) {
        const bool on = incidentsEnabled();
        tools_->setText(TOnOff,
                        on ? std::string("Arr\xC3\xAAter la cr\xC3\xA9" "ation des incidents : plus aucun dossier n'est cr\xC3\xA9\xC3\xA9, quelle que soit "
                                         "l'alarme (les incidents d\xC3\xA9j\xC3\xA0 enregistr\xC3\xA9s restent) ; un second clic la reprend")
                           : std::string("La cr\xC3\xA9" "ation des incidents est ARR\xC3\x8AT\xC3\x89" "E : un clic la reprend (les alarmes coch\xC3\xA9" "es "
                                         "\xC2\xAB Incidents \xC2\xBB refont leur dossier)"),
                        on ? std::string("Arr\xC3\xAAter les incidents") : std::string("Reprendre les incidents"));
    }
    const int t = validatedTab_ ? 1 : 0;
    const std::size_t total = items_[t] ? items_[t]->size() : 0;
    if (search_) search_->setCount(shown_[t], total);
    if (!status_ || !host_ || !items_[0] || !items_[1]) return;
    std::string msg = std::to_string(items_[0]->size()) + " en attente \xC2\xB7 " + std::to_string(items_[1]->size()) + " valid\xC3\xA9(s)"
                      + (host_->projectFolder().empty() ? std::string(" \xC2\xB7 projet non enregistr\xC3\xA9 : aucun dossier d'incident")
                                                        : std::string(" \xC2\xB7 ") + std::string(hmi::rec::kNativeFolder));
    // la recherche ou la criticite : ce qui est affiche dans l'onglet ouvert
    if (filtering())
        msg = std::to_string(shown_[t]) + (shown_[t] > 1 ? " incidents affich\xC3\xA9s sur " : " incident affich\xC3\xA9 sur ") + std::to_string(total)
              + " \xC2\xB7 " + msg;
    // 1.21.0 : les garde-fous - le plafond horaire (les incidents non crees), le quota des images et videos
    const auto g = host_->guards();
    if (g.skippedNow > 0 || g.capReached)
        msg += " \xC2\xB7 plafond horaire atteint (" + std::to_string(g.maxPerHour) + " nouveaux incidents par heure) : "
               + std::to_string(g.skippedNow) + (g.skippedNow > 1 ? " incidents non cr\xC3\xA9\xC3\xA9s" : " incident non cr\xC3\xA9\xC3\xA9")
               + (g.skippedSince.empty() ? std::string{} : " depuis " + incidentsui::dateText(g.skippedSince));
    else if (g.skipped > 0)
        msg += " \xC2\xB7 " + std::to_string(g.skipped) + (g.skipped > 1 ? " incidents non cr\xC3\xA9\xC3\xA9s" : " incident non cr\xC3\xA9\xC3\xA9")
               + " (plafond horaire) depuis le lancement";
    if (g.quotaReached)
        msg += " \xC2\xB7 quota des images et vid\xC3\xA9os atteint : les nouveaux incidents sont cr\xC3\xA9\xC3\xA9s sans image ni vid\xC3\xA9o";
    // un dossier choisi : les gestes portent sur l'incident qu'il montre
    const Row* r = row_[t] >= 0 ? rowAt(t, static_cast<std::size_t>(row_[t])) : nullptr;
    if (r && r->group)
        if (const auto* it = selected())
            msg += " \xC2\xB7 dossier \xC2\xAB " + r->alarm + " \xC2\xBB (" + incidentsWord(r->members.size()) + ") : les gestes portent sur \xC2\xAB "
                   + it->name + " \xC2\xBB";
    status_->setMessage(std::move(msg));
}

// ---- les gestes ----
bool HmiIncidentsPane::commentSelected(const std::string& text, std::string* why) {
    const auto* it = selected();
    std::string reason;
    const std::string t = hmikit::trimmed(text);
    if (!host_) reason = "la surveillance des incidents n'est pas branch\xC3\xA9" "e ici";
    else if (!it) reason = "aucun incident choisi";
    else if (t.empty()) reason = "le commentaire est vide";
    if (!reason.empty()) {
        if (why) *why = reason;
        return false;
    }
    host_->comment(it->folder, t, author());
    say("Commentaire ajout\xC3\xA9 \xC3\xA0 \xC2\xAB " + it->name + " \xC2\xBB : un fichier de plus dans son dossier.");
    return true;
}

bool HmiIncidentsPane::analyzeSelected(std::string* why) {
    const auto* it = selected();
    std::string reason;
    if (!host_) reason = "la surveillance des incidents n'est pas branch\xC3\xA9" "e ici";
    else if (!it) reason = "aucun incident choisi";
    else if (!it->readable) reason = "incident illisible : " + it->why;
    else if (host_->analysing() && host_->analysingFolder() == it->folder) reason = "une analyse de cet incident est d\xC3\xA9j\xC3\xA0 en cours";
    else if (analysisWaiting(it->name)) reason = "une analyse de cet incident est d\xC3\xA9j\xC3\xA0 demand\xC3\xA9" "e : elle attend son tour";
    if (!reason.empty()) {
        if (why) *why = reason;
        return false;
    }
    host_->analyze(it->folder);
    requested_.insert(it->name);
    analysesSeen_[it->name] = it->analyses.size();
    say("Nouvelle analyse demand\xC3\xA9" "e pour \xC2\xAB " + it->name + " \xC2\xBB : sa r\xC3\xA9ponse sera un fichier de plus (l'initiale reste).");
    if (sheet_) sheet_->dirty();
    return true;
}

bool HmiIncidentsPane::validateSelected(const std::string& note, std::string* why) {
    const auto* it = selected();
    std::string reason;
    if (!host_) reason = "la surveillance des incidents n'est pas branch\xC3\xA9" "e ici";
    else if (!it) reason = "aucun incident choisi";
    else if (it->state != st::State::Pending) reason = "\xC2\xAB " + it->name + " \xC2\xBB est d\xC3\xA9j\xC3\xA0 valid\xC3\xA9";
    if (!reason.empty()) {
        if (why) *why = reason;
        return false;
    }
    host_->validate(it->folder, hmikit::trimmed(note), author());
    say("Validation de \xC2\xAB " + it->name + " \xC2\xBB : copie v\xC3\xA9rifi\xC3\xA9" "e vers \xC2\xAB valides \xC2\xBB, puis l'original retir\xC3\xA9.");
    return true;
}

bool HmiIncidentsPane::removeSelected(std::string* why) {
    const auto* it = selected();
    std::string reason;
    if (!host_) reason = "la surveillance des incidents n'est pas branch\xC3\xA9" "e ici";
    else if (!it) reason = "aucun incident choisi";
    else if (it->state != st::State::Pending) reason = "un incident valid\xC3\xA9 ne se supprime pas d'ici";
    if (!reason.empty()) {
        if (why) *why = reason;
        return false;
    }
    host_->remove(it->folder);
    say("Suppression de \xC2\xAB " + it->name + " \xC2\xBB demand\xC3\xA9" "e.");
    return true;
}

void HmiIncidentsPane::askComment() {
    const auto* it = selected();
    if (!it) return;
    if (hosts_.comment) hosts_.comment(it->folder, it->name);
    else say("Ajouter un commentaire : pas de saisie ici.", true);
}

void HmiIncidentsPane::askValidate() {
    const auto* it = selected();
    if (!it) return;
    std::string why;
    if (it->state != st::State::Pending) say("\xC2\xAB " + it->name + " \xC2\xBB est d\xC3\xA9j\xC3\xA0 valid\xC3\xA9.", true);
    else if (hosts_.validate) hosts_.validate(it->folder, it->name);
    else if (!validateSelected({}, &why)) say(why, true);
}

void HmiIncidentsPane::askRemove() {
    if (chosenIncidents().size() > 1) {   // 1.24.1 : plusieurs - une question (definitif), puis tous
        removeChosen();
        return;
    }
    const auto* it = selected();
    if (!it) return;
    std::string why;
    if (it->state != st::State::Pending) say("Un incident valid\xC3\xA9 ne se supprime pas d'ici.", true);
    else if (hosts_.remove) hosts_.remove(it->folder, it->name);
    else if (!removeSelected(&why)) say(why, true);
}

// ---- 1.24.1 : plusieurs incidents d'un coup ----
std::vector<std::string> HmiIncidentsPane::rowNames(int tab, std::size_t row) const {
    std::vector<std::string> out;
    const Row* r = rowAt(tab, row);
    if (!r) return out;
    if (r->group) {
        for (const int m : r->members)
            if (const auto* x = itemAt(tab, m)) out.push_back(x->name);
    } else if (const auto* x = itemAt(tab, r->item)) {
        out.push_back(x->name);
    }
    return out;
}

bool HmiIncidentsPane::rowChosen(int tab, int row) const {
    if (tab < 0 || tab > 1 || marks_[tab].empty() || row < 0) return false;
    const auto names = rowNames(tab, static_cast<std::size_t>(row));
    if (names.empty()) return false;
    for (const auto& n : names)
        if (std::find(marks_[tab].begin(), marks_[tab].end(), n) == marks_[tab].end()) return false;
    return true;
}

std::vector<const st::Item*> HmiIncidentsPane::chosenIncidents() const {
    std::vector<const st::Item*> out;
    const int t = validatedTab_ ? 1 : 0;
    if (marks_[t].empty()) {
        if (const auto* it = selected()) out.push_back(it);
        return out;
    }
    if (const auto* v = items_[t])
        for (const auto& it : *v)
            if (std::find(marks_[t].begin(), marks_[t].end(), it.name) != marks_[t].end()) out.push_back(&it);
    return out;
}

void HmiIncidentsPane::toggleRowChoice(std::size_t row) {
    const int t = validatedTab_ ? 1 : 0;
    auto& marks = marks_[t];
    if (marks.empty() && row_[t] >= 0)   // le premier Ctrl+clic : la ligne choisie entre dans le choix
        for (auto& n : rowNames(t, static_cast<std::size_t>(row_[t]))) marks.push_back(std::move(n));
    const auto names = rowNames(t, row);
    if (names.empty()) return;
    const bool all = rowChosen(t, static_cast<int>(row));
    for (const auto& n : names) {
        const auto at = std::find(marks.begin(), marks.end(), n);
        if (all && at != marks.end()) marks.erase(at);
        else if (!all && at == marks.end()) marks.push_back(n);
    }
    if (!all) selectRow(t, static_cast<int>(row));   // la fiche suit la derniere ligne ajoutee
    updateStatus();
    invalidate();
    if (lists_[t]) lists_[t]->invalidate();
}

void HmiIncidentsPane::extendChoiceTo(std::size_t row) {
    const int t = validatedTab_ ? 1 : 0;
    const std::size_t from = row_[t] >= 0 ? static_cast<std::size_t>(row_[t]) : 0;
    marks_[t].clear();
    for (std::size_t r = std::min(from, row); r <= std::max(from, row) && r < rows_[t].size(); ++r)
        for (auto& n : rowNames(t, r))
            if (std::find(marks_[t].begin(), marks_[t].end(), n) == marks_[t].end()) marks_[t].push_back(std::move(n));
    updateStatus();
    invalidate();
    if (lists_[t]) lists_[t]->invalidate();
}

void HmiIncidentsPane::clearChoice() {
    const int t = validatedTab_ ? 1 : 0;
    if (marks_[t].empty()) return;
    marks_[t].clear();
    invalidate();
    if (lists_[t]) lists_[t]->invalidate();
}

// SUPPRIMER PLUSIEURS INCIDENTS : une question qui le dit definitif (leurs dossiers sont effaces, Ctrl+Z ne les rend pas),
// puis chacun par la surveillance (en arriere-plan, comme un seul) ; un incident valide reste, le compte rendu le dit.
void HmiIncidentsPane::removeChosen() {
    const auto chosen = chosenIncidents();
    if (chosen.empty()) return;
    several::Batch b;
    b.noun = "incident";
    b.nouns = "incidents";
    b.ask = true;
    b.final = true;
    b.warning = "Leurs dossiers sont effac\xC3\xA9s de ce PC (la capture, la vid\xC3\xA9o, la fiche, les commentaires).";
    std::vector<std::pair<std::string, bool>> doomed;   // le dossier, valide
    for (const auto* it : chosen) {
        b.names.push_back(it->name);
        doomed.emplace_back(it->folder, it->state != st::State::Pending);
    }
    b.remove = [this, doomed](std::size_t i, std::string* why) {
        if (!host_) {
            if (why) *why = "la surveillance des incidents n'est pas branch\xC3\xA9" "e ici";
            return false;
        }
        if (doomed[i].second) {
            if (why) *why = "valid\xC3\xA9 : il ne se supprime pas d'ici";
            return false;
        }
        host_->remove(doomed[i].first);
        return true;
    };
    b.after = [this] { clearChoice(); };
    b.report = [this](const std::string& text, bool warning) { say(text + ".", warning); };
    b.alive = tools_->triggered;
    several::run(std::move(b));
}

void HmiIncidentsPane::openSelectedFolder() {
    std::string folder;
    if (const auto* it = selected()) {
        folder = it->folder;
    } else if (host_ && !host_->projectFolder().empty()) {
        folder = (pathOf(host_->projectFolder()) / pathOf(std::string(hmi::rec::kNativeFolder)) / pathOf(std::string(st::kPendingFolder)))
                     .lexically_normal()
                     .generic_string();
        std::error_code ec;
        std::filesystem::create_directories(pathOf(folder), ec);
    }
    if (folder.empty()) {
        say("Pas de dossier : le projet n'est pas encore enregistr\xC3\xA9.", true);
        return;
    }
    if (hosts_.openFolder) hosts_.openFolder(folder);
    else say(folder);
}

std::vector<std::string> HmiIncidentsPane::verifySelected() {
    const auto* it = selected();
    if (!it || !host_) return {};
    return incidentsui::integrityLines(host_->verify(it->folder));
}

bool HmiIncidentsPane::revealLink(std::size_t index, std::string* why) {
    const auto* it = selected();
    if (!it || index >= it->snapshot.chain.size()) {
        if (why) *why = "aucun maillon choisi";
        return false;
    }
    const auto& l = it->snapshot.chain[index];
    const auto t = incidentsui::targetOf(doc_ ? &doc_->project : nullptr, l);
    const std::string gone = "\xC2\xAB " + l.name + " \xC2\xBB (" + l.kind + ") n'existe plus dans le projet : la fiche garde son nom d'origine.";
    if (!t.exists || !hosts_.reveal || !hosts_.reveal(t.kind, t.name, t.view)) {
        if (why) *why = t.exists && !hosts_.reveal ? std::string("rien ne s'ouvre ici") : gone;
        return false;
    }
    say("Ouvert : " + l.kind + " " + t.name + (t.view.empty() ? std::string{} : " (" + t.view + ")"));
    return true;
}

// ---- la fiche ----
void HmiIncidentsPane::consult() {
    if (!sheet_) return;
    sheet_->consult();
}

void HmiIncidentsPane::showFullAnalysis() {
    if (sheet_ && selected()) sheet_->openAll();
}

void HmiIncidentsPane::exploreDependencies() {
    if (sheet_ && selected()) sheet_->openGroup(HmiIncidentSheet::GChain);
}

std::vector<std::string> HmiIncidentsPane::sheetSections() const { return sheet_ ? sheet_->sectionTitles() : std::vector<std::string>{}; }
bool HmiIncidentsPane::sheetSectionOpen(std::size_t index) const { return sheet_ && sheet_->sectionOpen(index); }
void HmiIncidentsPane::toggleSheetSection(std::size_t index) {
    if (sheet_) sheet_->toggleSection(index);
}
void HmiIncidentsPane::setCompare(bool on) {
    if (sheet_) sheet_->setCompare(on);
}
bool HmiIncidentsPane::comparing() const { return sheet_ && sheet_->comparing(); }

void HmiIncidentsPane::openContextMenu(gfx::Point surface) {
    const auto* it = selected();
    if (!it) return;
    if (!menu_) {
        menu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(id() + ".menu")));
        links_ += menu_->itemChosen->connect([this](int action) {
            std::string why;
            // 1.24.1 : Ouvrir dans l'explorateur, Aller a, Copier le chemin - le dossier de l'incident (ui::paths).
            if (ui::paths::owns(action, kIncidentPathId)) {
                const std::string path = menuPath_;   // une copie : le geste peut refaire la liste
                if (const std::string no = ui::paths::run(action, kIncidentPathId, path); !no.empty()) say(no, true);
                return;
            }
            switch (action) {
                case MConsult: consult(); break;
                case MFull: showFullAnalysis(); break;
                case MChain: exploreDependencies(); break;
                case MComment: askComment(); break;
                case MAnalyze:
                    if (!analyzeSelected(&why)) say(why, true);
                    break;
                case MValidate: askValidate(); break;
                case MDelete: askRemove(); break;
                case MFolder: openSelectedFolder(); break;
                case MVerify: {
                    const auto lines = verifySelected();
                    if (sheet_) sheet_->reverify();
                    if (!lines.empty()) say("Int\xC3\xA9grit\xC3\xA9 : " + lines.front(), lines.front().rfind("intact", 0) != 0);
                    break;
                }
                case MToggle: {   // 1.20.2 : le dossier d'alarme choisi (ou celui de l'incident choisi)
                    const int t = validatedTab_ ? 1 : 0;
                    if (const Row* r = row_[t] >= 0 ? rowAt(t, static_cast<std::size_t>(row_[t])) : nullptr; r && (r->group || r->depth > 0))
                        setGroupOpen(t, r->key, r->group && !r->open);
                    break;
                }
                default: break;
            }
        });
    }
    const bool pending = it->state == st::State::Pending;
    // 1.20.2 : un dossier d'alarme choisi - son nom en tete, puis l'incident sur lequel portent les gestes
    const int tab = validatedTab_ ? 1 : 0;
    const Row* row = row_[tab] >= 0 ? rowAt(tab, static_cast<std::size_t>(row_[tab])) : nullptr;
    const auto entry = [](std::string label, int id, std::string key = {}, bool enabled = true, std::string why = {}) {
        ui::PopupMenu::Item e;
        e.label = std::move(label);
        e.id = id;
        e.shortcut = std::move(key);
        e.enabled = enabled;
        e.disabledReason = enabled ? std::string{} : std::move(why);
        return e;
    };
    std::vector<ui::PopupMenu::Item> items;
    // 1.24.1 : PLUSIEURS INCIDENTS CHOISIS - Supprimer les N (une question : definitif) ; le reste, un a la fois.
    if (const auto chosen = chosenIncidents(); chosen.size() > 1) {
        ui::PopupMenu::Item head;
        head.heading = true;
        head.label = incidentsWord(chosen.size());
        std::size_t waiting = 0;
        for (const auto* x : chosen) waiting += x->state == st::State::Pending ? 1u : 0u;
        head.shortcut = std::to_string(waiting) + " en attente";
        items.push_back(std::move(head));
        const std::string one = "un \xC3\xA0 la fois";
        for (const auto& [label, id] : std::vector<std::pair<std::string, int>>{{"Consulter", MConsult},
                                                                               {"Voir l'analyse compl\xC3\xA8te", MFull},
                                                                               {"Explorer les d\xC3\xA9pendances", MChain},
                                                                               {"Ajouter un commentaire\xE2\x80\xA6", MComment},
                                                                               {"Demander une nouvelle analyse", MAnalyze},
                                                                               {"Valider\xE2\x80\xA6", MValidate}})
            items.push_back(entry(label, id, {}, false, one));
        ui::PopupMenu::Item sep;
        sep.separator = true;
        items.push_back(sep);
        items.push_back(entry(several::label("Supprimer", chosen.size()) + "\xE2\x80\xA6", MDelete, "Suppr", waiting > 0,
                              "un incident valid\xC3\xA9 ne se supprime pas d'ici"));
        menuPath_.clear();
        menu_->setItems(std::move(items));
        menu_->setBounds(bounds());
        menu_->openAtSurface(surface);
        return;
    }
    if (row && row->group) {
        ui::PopupMenu::Item folder;
        folder.heading = true;
        folder.label = "Dossier \xC2\xAB " + row->alarm + " \xC2\xBB";
        folder.shortcut = incidentsWord(row->members.size());
        items.push_back(std::move(folder));
    }
    ui::PopupMenu::Item head;
    head.heading = true;
    head.label = it->name;
    head.shortcut = pending ? "en attente" : "valid\xC3\xA9";
    items.push_back(std::move(head));
    const bool folderRow = row && row->group;
    if (folderRow || (row && row->depth > 0)) {
        // Entree (ou un clic) deplie / replie le dossier choisi ; fleche gauche replie celui de l'incident choisi
        items.push_back(entry(folderRow && !row->open ? "D\xC3\xA9plier le dossier" : "Replier le dossier", MToggle,
                              folderRow ? "Entr\xC3\xA9" "e" : "\xE2\x86\x90"));
        ui::PopupMenu::Item sep;
        sep.separator = true;
        items.push_back(sep);
    }
    items.push_back(entry("Consulter", MConsult, folderRow ? std::string{} : std::string("Entr\xC3\xA9" "e")));
    items.push_back(entry("Voir l'analyse compl\xC3\xA8te", MFull));
    items.push_back(entry("Explorer les d\xC3\xA9pendances", MChain));
    ui::PopupMenu::Item rule;
    rule.separator = true;
    items.push_back(rule);
    items.push_back(entry("Ajouter un commentaire\xE2\x80\xA6", MComment));
    items.push_back(entry("Demander une nouvelle analyse", MAnalyze, {}, it->readable, "incident illisible"));
    items.push_back(entry("Valider\xE2\x80\xA6", MValidate, {}, pending, "d\xC3\xA9j\xC3\xA0 valid\xC3\xA9"));
    items.push_back(entry("Supprimer\xE2\x80\xA6", MDelete, "Suppr", pending, "un incident valid\xC3\xA9 ne se supprime pas d'ici"));
    items.push_back(rule);
    // 1.24.1 : le dossier de l'incident, comme partout ou un chemin se montre - Ouvrir dans l'explorateur, Aller a,
    // Copier le chemin (ui::paths) ; sans l'application (les essais) : Ouvrir le dossier, comme avant.
    menuPath_ = it->folder;
    if (auto pathItems = ui::paths::menuItems(menuPath_, kIncidentPathId); !pathItems.empty())
        for (auto& p : pathItems) items.push_back(std::move(p));
    else
        items.push_back(entry("Ouvrir le dossier", MFolder));
    items.push_back(entry("V\xC3\xA9rifier l'int\xC3\xA9grit\xC3\xA9", MVerify));
    menu_->setItems(std::move(items));
    menu_->setBounds(bounds());
    menu_->openAtSurface(surface);
}

void HmiIncidentsPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    // 1.20.2 : la ligne des filtres sous la barre (comme celle des alarmes) - la recherche, la criticite, le tri ; un
    // volet etroit serre d'abord les deux listes.
    {
        const float y = b.y + 41.f, h = 28.f, gap = 8.f;
        const float room = std::max(0.f, b.w - 4.f * gap);
        float critW = 176.f, sortW = 226.f;
        if (room - critW - sortW < 200.f) {
            const float k = std::max(0.f, room - 200.f) / (critW + sortW);
            critW = std::floor(critW * std::min(1.f, std::max(k, 0.45f)));
            sortW = std::floor(sortW * std::min(1.f, std::max(k, 0.45f)));
        }
        const float searchW = std::clamp(room - critW - sortW, 0.f, 600.f);
        float x = b.x + gap;
        search_->setBounds({x, y, searchW, h});
        x += searchW + gap;
        criticality_->setBounds({x, y, critW, h});
        x += critW + gap;
        sort_->setBounds({x, y, sortW, h});
    }
    split_->setBounds({b.x, b.y + 73, b.w, std::max(0.f, b.h - 97)});
    if (menu_) menu_->setBounds(b);
}

void HmiIncidentsPane::onPaint(const ui::PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
    (void)poll();   // la liste de l'hote a change (un incident cree, un geste fait) : on la reprend
    // 1.21.0 : une alarme qui bat - ses chiffres (la fiche, la barre d'etat) suivent, au plus une fois par seconde
    if (host_ && host_->liveRevision() != liveSeen_ && ctx.time - liveAt_ >= 1.0) {
        liveSeen_ = host_->liveRevision();
        liveAt_ = ctx.time;
        if (sheet_) sheet_->dirty();
        updateStatus();
    }
}

} // namespace app
