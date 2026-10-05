// hmi/HmiRuntimeLot16.cpp - le moteur de l'IHM : les GIF animes (lot 16).
#include "HmiRuntime.hpp"

#include "HmiMarkers.hpp"   // 1.11 (REP-1) : l'image sans les $ de ses reperes
#include "HmiMedia.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace hmi {

namespace {

std::string lowerOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool sameName(std::string_view a, std::string_view b) { return lowerOf(a) == lowerOf(b); }

// "en boucle" -> 0 (sans fin), "une fois" -> 1, "N fois" -> count.
int loopsOf(const Object& o) {
    const std::string play = lowerOf(o.text("play", "en boucle"));
    if (play.find("une") != std::string::npos) return 1;
    if (play.find("n fois") != std::string::npos || play.find("fois") != std::string::npos) return std::max(1, static_cast<int>(o.number("count", 3)));
    return 0;
}

double speedOf(const Object& o) { return std::clamp(o.number("speed", 100) / 100.0, 0.05, 20.0); }

// La lecture a l'heure `t` : l'image, les tours faits, fini ou non.
struct Position {
    int       frame{0};
    long long loops{0};
    bool      ended{false};
};
Position positionAt(const Runtime::GifPlayback& g, const GifTiming& timing, double t) {
    Position pos;
    if (timing.frames() == 0 || timing.totalMs <= 0) return pos;
    const double clock = g.paused ? g.pausedAt : t;
    const double elapsed = std::max(0.0, (clock - g.startedAt) * g.speed * 1000.0);
    const double total = static_cast<double>(timing.totalMs);
    if (g.loops > 0 && elapsed >= g.loops * total) {
        pos.ended = true;
        pos.loops = g.loops;
        pos.frame = static_cast<int>(timing.frames()) - 1;
        return pos;
    }
    pos.loops = static_cast<long long>(elapsed / total);
    pos.frame = static_cast<int>(timing.frameAt(std::fmod(elapsed, total)));
    return pos;
}

} // namespace

const GifTiming* Runtime::gifTimingOf(const Object& o) const {
    if (!project_) return nullptr;
    const auto* r = project_->resourceByName(markers::strip(o.text("image"), markers::Mode::Text));   // 1.11 (REP-1) : sans ses $
    if (!r || !r->data || !isGif(*r->data)) return nullptr;
    auto it = gifTimings_.find(r->data.get());
    if (it == gifTimings_.end()) {
        GifTiming timing;
        if (!gifTiming(*r->data, timing)) timing = GifTiming{};
        it = gifTimings_.emplace(r->data.get(), std::move(timing)).first;
    }
    return it->second.frames() ? &it->second : nullptr;
}

const Object* Runtime::openGifNamed(std::string_view name, const View** where) const {
    const auto look = [&](Id viewId) -> const Object* {
        const View* v = viewOf(viewId);
        if (!v) return nullptr;
        for (const auto& o : v->objects)
            if (o.kind == Kind::AnimatedGif && sameName(o.name, name)) {
                if (where) *where = v;
                return &o;
            }
        return nullptr;
    };
    // Les popups d'abord (au-dessus), puis la vue courante.
    for (auto it = popups_.rbegin(); it != popups_.rend(); ++it)
        if (const auto* o = look(*it)) return o;
    return look(current_);
}

void Runtime::gifStart(const Object& o, GifPlayback& g, double now, int loops) {
    g.playing = true;
    g.paused = false;
    g.ended = false;
    g.startedAt = now;
    g.loops = loops < 0 ? loopsOf(o) : loops;
    g.speed = speedOf(o);
    g.frame = 0;
    g.loopsDone = 0;
}

void Runtime::gifsOpenView(const View& v, double now) {
    for (const auto& o : v.objects) {
        if (o.kind != Kind::AnimatedGif) continue;
        GifPlayback g;
        const std::string start = lowerOf(o.text("start", "\xC3\xA0 l'affichage"));
        if (start.find("affichage") != std::string::npos || start.empty()) gifStart(o, g, now, -1);
        gifs_[o.id] = g;
    }
}

void Runtime::gifsCycle(double now) {
    if (gifs_.empty()) return;
    const auto visit = [&](Id viewId) {
        const View* v = viewOf(viewId);
        if (!v) return;
        for (const auto& o : v->objects) {
            if (o.kind != Kind::AnimatedGif) continue;
            auto& g = gifs_[o.id];
            // "sur condition" : joue tant qu'elle est vraie.
            if (lowerOf(o.text("start")).find("condition") != std::string::npos) {
                const std::string cond = o.text("condition");
                const bool on = !cond.empty() && evalBool(cond, false);
                if (on && !g.conditionWas) gifStart(o, g, now, -1);
                if (!on && g.conditionWas) {
                    g.playing = false;
                    g.paused = false;
                    g.ended = false;
                    g.frame = 0;
                }
                g.conditionWas = on;
            }
            if (!g.playing) continue;
            if (const auto* timing = gifTimingOf(o)) {
                const Position pos = positionAt(g, *timing, now);
                g.frame = pos.frame;
                g.loopsDone = pos.loops;
                if (pos.ended) {
                    g.playing = false;
                    g.ended = true;
                    log("GIF", o.name, "fin de la lecture (" + std::to_string(g.loops) + " fois)");
                }
            }
        }
    };
    visit(current_);
    for (const Id p : popups_) visit(p);
}

const Runtime::GifPlayback* Runtime::gifPlayback(Id object) const {
    const auto it = gifs_.find(object);
    return it == gifs_.end() ? nullptr : &it->second;
}

int Runtime::gifFrameAt(Id object, double t) const {
    const auto it = gifs_.find(object);
    if (it == gifs_.end()) return 0;
    const auto& g = it->second;
    const Object* o = nullptr;
    for (const Id vid : popups_)
        if (const View* v = viewOf(vid))
            if (const auto* x = v->object(object)) o = x;
    if (!o)
        if (const View* v = viewOf(current_)) o = v->object(object);
    if (!o) return 0;
    const auto* timing = gifTimingOf(*o);
    if (!timing) return 0;
    const std::string end = lowerOf(o->text("end", "derni\xC3\xA8re image"));
    if (g.ended || (g.playing && positionAt(g, *timing, std::max(t, now_)).ended)) {
        if (end.find("cach") != std::string::npos) return -1;
        if (end.find("premi") != std::string::npos) return 0;
        return static_cast<int>(timing->frames()) - 1;
    }
    if (!g.playing) return g.paused ? g.frame : g.frame;
    return positionAt(g, *timing, std::max(t, now_)).frame;
}

bool Runtime::gifCommand(std::string_view objectName, std::string_view what, int count, double now, const std::string& source, std::string* why) {
    const View* v = nullptr;
    const Object* o = openGifNamed(objectName, &v);
    if (!o) {
        const std::string text = "GIF anim\xC3\xA9 '" + std::string(objectName) + "' introuvable dans les vues ouvertes";
        if (why) *why = text;
        log("Erreur", source, text);
        return false;
    }
    now_ = std::max(now_, now);
    auto& g = gifs_[o->id];
    const std::string w = lowerOf(what);
    if (w == "jouer") {
        if (g.paused) {
            g.startedAt += now - g.pausedAt;
            g.paused = false;
            g.playing = true;
        } else if (!g.playing) {
            gifStart(*o, g, now, count);
        }
    } else if (w == "pause") {
        if (g.playing && !g.paused) {
            g.paused = true;
            g.pausedAt = now;
            if (const auto* timing = gifTimingOf(*o)) g.frame = positionAt(g, *timing, now).frame;
        }
    } else if (w == "arreter") {
        g.playing = false;
        g.paused = false;
        g.ended = false;
        g.frame = 0;
        g.loopsDone = 0;
    } else if (w == "rejouer") {
        gifStart(*o, g, now, count < 0 ? loopsOf(*o) : count);
    } else {
        if (why) *why = "commande de GIF inconnue : " + std::string(what);
        return false;
    }
    log("GIF", source, o->name + " : " + w + (w == "rejouer" || (w == "jouer" && count >= 0)
                                                    ? (g.loops == 0 ? std::string(" (sans fin)") : " (" + std::to_string(g.loops) + " fois)")
                                                    : std::string{}));
    return true;
}

bool Runtime::gifMemberValue(const Object& o, std::string_view info, sim::Value& out) const {
    if (o.kind != Kind::AnimatedGif) return false;
    const auto* g = gifPlayback(o.id);
    const auto* timing = gifTimingOf(o);
    if (info == "Playing") {
        out = sim::Value::boolean(g && g->playing && !g->paused);
        return true;
    }
    if (info == "Frame") {
        out = sim::Value::integer(sim::Type::Int, std::max(0, gifFrameAt(o.id, now_)));
        return true;
    }
    if (info == "FrameCount") {
        out = sim::Value::integer(sim::Type::Int, timing ? static_cast<long long>(timing->frames()) : 0);
        return true;
    }
    if (info == "Loops") {
        long long loops = 0;
        if (g && timing && g->playing) loops = positionAt(*g, *timing, now_).loops;
        else if (g) loops = g->ended ? g->loops : g->loopsDone;
        out = sim::Value::integer(sim::Type::DInt, loops);
        return true;
    }
    return false;
}

} // namespace hmi
