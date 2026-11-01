// =============================================================================
//  core/Perf.cpp - 1.22.2 : voir Perf.hpp
// -----------------------------------------------------------------------------
//  LE CHEMIN CHAUD. Une portee : un booleen du fil (thread_local), deux lectures
//  de l'horloge, quelques additions dans des tableaux fixes. L'image en cours
//  garde la liste des parties touchees : la fin de l'image ne parcourt qu'elles.
//  Rien n'alloue, rien n'attend, aucune chaine n'est construite.
//
//  LES SECONDES. kSeconds cases, chacune marquee de sa seconde (stamp = seconde
//  + 1 ; 0 : vide) : une case reprise pour une nouvelle seconde est videe (les
//  parties connues seulement). Les fenetres de 10 s et 60 s additionnent les
//  cases dont la marque est dans la fenetre ; << depuis le lancement >> lit les
//  totaux.
//
//  LES TRAVAUX DE FOND s'ecrivent depuis leurs fils : des atomiques, une case
//  prise par compare-and-swap ; les derniers finis dans un anneau a numeros (le
//  principe de CallTrail : le lecteur relit le numero apres sa copie).
// =============================================================================
#include "Perf.hpp"

#include "CallTrail.hpp"
#include "Edition.hpp"
#include "Version.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/resource.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

namespace core::perf {
namespace {

constexpr const char*  kUnmeasuredName = "(hors mesure)";
constexpr std::int64_t kNsPerMs = 1'000'000;
constexpr std::int64_t kNsPerSecond = 1'000'000'000;
// Une image de plus de deux minutes n'en est pas une : le PC endormi, un debogueur. Ignoree.
constexpr std::int64_t kIgnoreNs = 120 * kNsPerSecond;

// ---- les parties ------------------------------------------------------------------------
std::mutex                    g_registry;
std::atomic<const char*>      g_names[kMaxParts];
std::atomic<std::size_t>      g_count{1};      // la partie 0 : << (hors mesure) >>

// ---- l'horloge --------------------------------------------------------------------------
const std::int64_t* g_testClock = nullptr;

std::int64_t nowNs() noexcept {
    if (g_testClock) return *g_testClock;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::int64_t steadyMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Le fil de l'interface : trivial (aucune enveloppe d'acces dans le chemin chaud).
thread_local bool t_ui = false;

// ---- l'image en cours -------------------------------------------------------------------
struct Acc {
    std::int64_t  self{0};
    std::int64_t  incl{0};
    std::uint32_t calls{0};
    std::uint16_t depth{0};      // les portees ouvertes de cette partie (une partie qui s'appelle elle-meme)
    bool          touched{false};
};
Acc         g_cur[kMaxParts];
Part        g_touched[kMaxParts];
std::size_t g_touchedCount = 0;
Scope*      g_top = nullptr;
bool        g_haveLast = false;
std::int64_t g_lastEnd = 0;

// ---- les secondes, les totaux -----------------------------------------------------------
struct Cell {
    std::int64_t  self{0};
    std::int64_t  incl{0};
    std::int64_t  worstSelf{0};
    std::int64_t  worstIncl{0};
    std::uint64_t calls{0};
};
struct Bucket {
    std::int64_t  stamp{0};      // la seconde + 1 ; 0 : vide
    std::uint32_t frames{0};
    std::int64_t  frameSum{0};
    std::int64_t  frameMax{0};
    Cell          parts[kMaxParts];
};
Bucket        g_buckets[kSeconds];
Cell          g_total[kMaxParts];
std::uint64_t g_totalFrames = 0;
std::int64_t  g_totalFrameNs = 0;
std::int64_t  g_totalFrameMax = 0;
bool          g_epochSet = false;
std::int64_t  g_epoch = 0;       // l'origine des secondes
std::int64_t  g_resetAt = 0;     // depuis quand comptent les totaux

// ---- les saccades -----------------------------------------------------------------------
Stutter       g_stutters[kStutterCapacity];
std::uint64_t g_stutterNext = 0;          // le numero de la derniere
std::int64_t  g_thresholdNs = kDefaultThresholdMs * kNsPerMs;

// ---- les travaux de fond ----------------------------------------------------------------
struct JobSlot {
    std::atomic<int>          state{0};     // 0 libre, 1 en preparation, 2 en cours
    std::atomic<const char*>  name{nullptr};
    std::atomic<std::int64_t> startMs{0};
};
struct FinishedSlot {
    std::atomic<std::uint64_t> seq{0};
    std::atomic<const char*>   name{nullptr};
    std::atomic<std::int64_t>  durationMs{0};
    std::atomic<std::int64_t>  endMs{0};
};
constexpr std::uint64_t     kWriting = ~std::uint64_t{0};
JobSlot                     g_jobs[kMaxJobs];
FinishedSlot                g_finished[kFinishedJobs];
std::atomic<std::uint64_t>  g_finishedNext{0};

// -----------------------------------------------------------------------------------------
std::size_t knownParts() noexcept { return std::min(g_count.load(std::memory_order_acquire), kMaxParts); }

void ensureEpoch(std::int64_t now) noexcept {
    if (g_epochSet) return;
    g_epochSet = true;
    g_epoch = now;
    g_resetAt = now;
}

std::int64_t secondOf(std::int64_t now) noexcept {
    const std::int64_t d = now - g_epoch;
    return d > 0 ? d / kNsPerSecond : 0;
}

Bucket& bucketFor(std::int64_t sec) noexcept {
    Bucket& b = g_buckets[static_cast<std::size_t>(sec) % kSeconds];
    if (b.stamp != sec + 1) {
        b.stamp = sec + 1;
        b.frames = 0;
        b.frameSum = 0;
        b.frameMax = 0;
        // Seules les parties connues ont pu y ecrire (les numeros ne font que croitre).
        const std::size_t n = knownParts();
        for (std::size_t p = 0; p < n; ++p) b.parts[p] = Cell{};
    }
    return b;
}

void touch(Part p) noexcept {
    Acc& a = g_cur[p];
    if (!a.touched) {
        a.touched = true;
        g_touched[g_touchedCount++] = p;
    }
}

void clearCurrent() noexcept {
    for (std::size_t k = 0; k < g_touchedCount; ++k) {
        Acc& a = g_cur[g_touched[k]];
        a.self = 0;
        a.incl = 0;
        a.calls = 0;
        a.touched = false;          // depth reste : une portee ouverte la tient
    }
    g_touchedCount = 0;
}

void add(Cell& c, const Acc& a) noexcept {
    c.self += a.self;
    c.incl += a.incl;
    c.calls += a.calls;
    c.worstSelf = std::max(c.worstSelf, a.self);
    c.worstIncl = std::max(c.worstIncl, a.incl);
}

void merge(Cell& into, const Cell& c) noexcept {
    into.self += c.self;
    into.incl += c.incl;
    into.calls += c.calls;
    into.worstSelf = std::max(into.worstSelf, c.worstSelf);
    into.worstIncl = std::max(into.worstIncl, c.worstIncl);
}

long long msOf(std::int64_t ns) noexcept { return static_cast<long long>((ns + kNsPerMs / 2) / kNsPerMs); }

void recordStutter(std::int64_t frame) noexcept {
    Stutter s;
    s.serial = ++g_stutterNext;
    s.utcMs = core::trail::nowMs();
    s.frameNs = frame;
    // Les trois parties qui ont pris l'image (leur temps propre), la plus gourmande d'abord.
    for (std::size_t k = 0; k < g_touchedCount; ++k) {
        const Part p = g_touched[k];
        const std::int64_t t = g_cur[p].self;
        if (t <= 0) continue;
        int at = s.count;
        while (at > 0 && s.topNs[at - 1] < t) --at;
        if (at >= 3) continue;
        for (int j = std::min<int>(s.count, 2); j > at; --j) {
            s.top[j] = s.top[j - 1];
            s.topNs[j] = s.topNs[j - 1];
        }
        s.top[at] = p;
        s.topNs[at] = t;
        if (s.count < 3) ++s.count;
    }
    g_stutters[(s.serial - 1) % kStutterCapacity] = s;
    // Une longue : une ligne dans l'historique interne (le rapport d'un futur blocage la montre).
    if (frame >= static_cast<std::int64_t>(kTrailStutterMs) * kNsPerMs) {
        char parts[200];
        parts[0] = '\0';
        std::size_t used = 0;
        for (int k = 0; k < s.count && used < sizeof parts; ++k) {
            const int n = std::snprintf(parts + used, sizeof parts - used, "%s%s %lld ms", k ? " ; " : "", partName(s.top[k]), msOf(s.topNs[k]));
            if (n < 0) break;
            used += static_cast<std::size_t>(n);
        }
        core::trail::notef(core::trail::Kind::Watchdog, "saccade de %lld ms (%s)", msOf(frame), parts);
    }
}

std::string withComma(std::string s) {
    for (auto& ch : s)
        if (ch == '.') ch = ',';
    return s;
}

std::string fixed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return withComma(buf);
}

// 42300 -> "42 300"
std::string thousands(std::uint64_t v) {
    std::string digits = std::to_string(v);
    std::string out;
    const std::size_t n = digits.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0) out += ' ';
        out += digits[i];
    }
    return out;
}

// 723 s -> "12 min 3 s"
std::string span(double seconds) {
    if (seconds < 0) seconds = 0;
    const auto s = static_cast<long long>(seconds);
    if (s < 60) return std::to_string(s) + " s";
    if (s < 3600) return std::to_string(s / 60) + " min " + std::to_string(s % 60) + " s";
    return std::to_string(s / 3600) + " h " + std::to_string((s / 60) % 60) + " min";
}

std::string pad(std::string s, std::size_t width) {
    // les colonnes du rapport : des chiffres, des espaces, des virgules et % (ASCII)
    if (s.size() < width) s.append(width - s.size(), ' ');
    return s;
}

} // namespace

// ======================================================================= les parties ====
Part part(const char* name) noexcept {
    if (!name || !*name) return kNoPart;
    std::lock_guard<std::mutex> lk(g_registry);
    const std::size_t n = g_count.load(std::memory_order_relaxed);
    for (std::size_t p = 1; p < n; ++p) {
        const char* known = g_names[p].load(std::memory_order_relaxed);
        if (known == name || (known && std::strcmp(known, name) == 0)) return static_cast<Part>(p);
    }
    if (n >= kMaxParts) return kNoPart;
    g_names[n].store(name, std::memory_order_relaxed);
    g_count.store(n + 1, std::memory_order_release);
    return static_cast<Part>(n);
}

const char* partName(Part p) noexcept {
    if (p == kUnmeasured) return kUnmeasuredName;
    if (p >= knownParts()) return "";
    const char* n = g_names[p].load(std::memory_order_relaxed);
    return n ? n : "";
}

std::size_t partCount() noexcept { return knownParts(); }

void attachUiThread() noexcept { t_ui = true; }
bool onUiThread() noexcept { return t_ui; }

// ======================================================================= la portee ====
Scope::Scope(Part p) noexcept {
    if (!t_ui || p == kNoPart || p >= kMaxParts) return;
    part_ = p;
    parent_ = g_top;
    g_top = this;
    ++g_cur[p].depth;
    start_ = nowNs();
}

Scope::~Scope() {
    if (part_ == kNoPart) return;
    const std::int64_t d = std::max<std::int64_t>(0, nowNs() - start_);
    Acc& a = g_cur[part_];
    a.self += d > children_ ? d - children_ : 0;
    a.calls += 1;
    if (a.depth > 0) --a.depth;
    if (a.depth == 0) a.incl += d;      // la plus exterieure seulement : rien n'est compte deux fois
    touch(part_);
    if (parent_) parent_->children_ += d;
    g_top = parent_;
}

// ======================================================================= les etapes ====
void Lap::to(Part p) noexcept {
    scope_.reset();          // l'etape d'avant se ferme d'abord (l'emboitement)
    scope_.emplace(p);
}

void Lap::stop() noexcept { scope_.reset(); }

// ======================================================================= l'image ====
void frameEnd() noexcept {
    if (!t_ui) return;
    const std::int64_t now = nowNs();
    ensureEpoch(now);
    if (!g_haveLast) {          // la premiere : pas de debut connu, rien a compter
        g_haveLast = true;
        g_lastEnd = now;
        clearCurrent();
        return;
    }
    const std::int64_t frame = now - g_lastEnd;
    g_lastEnd = now;
    if (frame <= 0 || frame > kIgnoreNs) {
        clearCurrent();
        return;
    }
    // Le reste de l'image : ce qu'aucune portee n'a mesure.
    std::int64_t measured = 0;
    for (std::size_t k = 0; k < g_touchedCount; ++k) measured += g_cur[g_touched[k]].self;
    Acc& rest = g_cur[kUnmeasured];
    rest.self = std::max<std::int64_t>(0, frame - measured);
    rest.incl = rest.self;
    rest.calls = 1;
    touch(kUnmeasured);

    Bucket& b = bucketFor(secondOf(now));
    ++b.frames;
    b.frameSum += frame;
    b.frameMax = std::max(b.frameMax, frame);
    ++g_totalFrames;
    g_totalFrameNs += frame;
    g_totalFrameMax = std::max(g_totalFrameMax, frame);
    for (std::size_t k = 0; k < g_touchedCount; ++k) {
        const Part p = g_touched[k];
        add(b.parts[p], g_cur[p]);
        add(g_total[p], g_cur[p]);
    }
    if (frame >= g_thresholdNs) recordStutter(frame);
    clearCurrent();
}

void setStutterThresholdMs(int ms) noexcept { g_thresholdNs = static_cast<std::int64_t>(std::clamp(ms, 1, 10000)) * kNsPerMs; }
int stutterThresholdMs() noexcept { return static_cast<int>(g_thresholdNs / kNsPerMs); }

void reset() noexcept {
    for (auto& b : g_buckets) b.stamp = 0;     // videe a sa reprise (bucketFor)
    const std::size_t n = knownParts();
    for (std::size_t p = 0; p < n; ++p) g_total[p] = Cell{};
    g_totalFrames = 0;
    g_totalFrameNs = 0;
    g_totalFrameMax = 0;
    g_stutterNext = 0;
    for (auto& s : g_stutters) s = Stutter{};
    const std::int64_t now = nowNs();
    if (g_epochSet) g_resetAt = now;
    else ensureEpoch(now);
}

// ======================================================================= lire ====
double Window::fps() const noexcept { return frameNs > 0 ? static_cast<double>(frames) * 1e9 / static_cast<double>(frameNs) : 0.0; }
double Window::averageFrameMs() const noexcept { return frames ? static_cast<double>(frameNs) / 1e6 / static_cast<double>(frames) : 0.0; }

Window window(int seconds) {
    Window w;
    w.seconds = seconds > 0 ? std::min<int>(seconds, static_cast<int>(kSeconds) - 2) : 0;
    const std::size_t n = knownParts();
    std::vector<Cell> sum(n);
    const std::int64_t now = nowNs();
    const double sinceReset = g_epochSet ? static_cast<double>(std::max<std::int64_t>(0, now - g_resetAt)) / 1e9 : 0.0;
    if (w.seconds == 0) {
        w.frames = g_totalFrames;
        w.frameNs = g_totalFrameNs;
        w.worstFrameNs = g_totalFrameMax;
        for (std::size_t p = 0; p < n; ++p) sum[p] = g_total[p];
        w.span = sinceReset;
    } else {
        if (g_epochSet) {
            const std::int64_t nowSec = secondOf(now);
            for (int k = 0; k < w.seconds; ++k) {
                const std::int64_t sec = nowSec - k;
                if (sec < 0) break;
                const Bucket& b = g_buckets[static_cast<std::size_t>(sec) % kSeconds];
                if (b.stamp != sec + 1) continue;
                w.frames += b.frames;
                w.frameNs += b.frameSum;
                w.worstFrameNs = std::max(w.worstFrameNs, b.frameMax);
                for (std::size_t p = 0; p < n; ++p)
                    if (b.parts[p].calls) merge(sum[p], b.parts[p]);
            }
        }
        w.span = std::min(static_cast<double>(w.seconds), sinceReset);
    }
    for (std::size_t p = 0; p < n; ++p) {
        if (!sum[p].calls) continue;
        PartStat s;
        s.part = static_cast<Part>(p);
        s.name = partName(s.part);
        s.selfNs = sum[p].self;
        s.inclNs = sum[p].incl;
        s.worstSelfNs = sum[p].worstSelf;
        s.worstInclNs = sum[p].worstIncl;
        s.calls = sum[p].calls;
        w.parts.push_back(s);
    }
    std::stable_sort(w.parts.begin(), w.parts.end(), [](const PartStat& a, const PartStat& b) {
        if (a.selfNs != b.selfNs) return a.selfNs > b.selfNs;
        return std::strcmp(a.name, b.name) < 0;
    });
    return w;
}

double measuredSeconds() noexcept {
    return g_epochSet ? static_cast<double>(std::max<std::int64_t>(0, nowNs() - g_resetAt)) / 1e9 : 0.0;
}

std::vector<Second> timeline(int seconds) {
    seconds = std::clamp(seconds, 1, static_cast<int>(kSeconds) - 2);
    std::vector<Second> out(static_cast<std::size_t>(seconds));
    const std::int64_t nowSec = g_epochSet ? secondOf(nowNs()) : -1;
    for (int i = 0; i < seconds; ++i) {
        Second& s = out[static_cast<std::size_t>(i)];
        s.ago = seconds - 1 - i;
        const std::int64_t sec = nowSec - s.ago;
        if (nowSec < 0 || sec < 0) continue;
        const Bucket& b = g_buckets[static_cast<std::size_t>(sec) % kSeconds];
        if (b.stamp != sec + 1 || b.frames == 0) continue;
        s.frames = b.frames;
        s.averageMs = static_cast<double>(b.frameSum) / 1e6 / static_cast<double>(b.frames);
        s.worstMs = static_cast<double>(b.frameMax) / 1e6;
    }
    return out;
}

std::vector<Stutter> stutters() {
    std::vector<Stutter> out;
    const std::uint64_t n = std::min<std::uint64_t>(g_stutterNext, kStutterCapacity);
    out.reserve(static_cast<std::size_t>(n));
    for (std::uint64_t s = g_stutterNext - n + 1; s <= g_stutterNext && n > 0; ++s) out.push_back(g_stutters[(s - 1) % kStutterCapacity]);
    return out;
}

std::uint64_t stutterCount() noexcept { return g_stutterNext; }

std::size_t recentStutters(int seconds) noexcept {
    const std::uint64_t now = core::trail::nowMs();
    const std::uint64_t since = now > static_cast<std::uint64_t>(seconds) * 1000u ? now - static_cast<std::uint64_t>(seconds) * 1000u : 0;
    const std::uint64_t n = std::min<std::uint64_t>(g_stutterNext, kStutterCapacity);
    std::size_t count = 0;
    for (std::uint64_t k = 0; k < n; ++k) {
        const Stutter& s = g_stutters[(g_stutterNext - 1 - k) % kStutterCapacity];   // de la plus recente a la plus ancienne
        if (s.utcMs < since) break;
        if (s.frameNs >= g_thresholdNs) ++count;
    }
    return count;
}

std::string stutterClock(const Stutter& s) {
    char buf[32];
    core::trail::formatClock(s.utcMs, buf);
    return buf;
}

std::string stutterParts(const Stutter& s) {
    std::string out;
    for (int k = 0; k < s.count; ++k) {
        if (k) out += " ; ";
        out += partName(s.top[k]);
        out += ' ';
        out += formatMs(static_cast<double>(s.topNs[k]) / 1e6);
    }
    return out;
}

// ======================================================================= les travaux ====
Job::Job(const char* name) noexcept : name_(name && *name ? name : "travail"), startMs_(steadyMs()) {
    for (std::size_t i = 0; i < kMaxJobs; ++i) {
        int expected = 0;
        if (g_jobs[i].state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
            g_jobs[i].name.store(name_, std::memory_order_relaxed);
            g_jobs[i].startMs.store(startMs_, std::memory_order_relaxed);
            g_jobs[i].state.store(2, std::memory_order_release);
            slot_ = static_cast<int>(i);
            break;
        }
    }
}

Job::~Job() {
    const std::int64_t end = steadyMs();
    if (slot_ >= 0) g_jobs[slot_].state.store(0, std::memory_order_release);
    const std::uint64_t s = g_finishedNext.fetch_add(1, std::memory_order_relaxed) + 1;
    FinishedSlot& f = g_finished[(s - 1) % kFinishedJobs];
    f.seq.store(kWriting, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    f.name.store(name_, std::memory_order_relaxed);
    f.durationMs.store(std::max<std::int64_t>(0, end - startMs_), std::memory_order_relaxed);
    f.endMs.store(end, std::memory_order_relaxed);
    f.seq.store(s, std::memory_order_release);
}

std::vector<JobInfo> runningJobs() {
    struct Seen {
        const char*  name;
        std::int64_t start;
    };
    std::vector<Seen> seen;
    for (const auto& j : g_jobs)
        if (j.state.load(std::memory_order_acquire) == 2) {
            const char* n = j.name.load(std::memory_order_relaxed);
            seen.push_back({n ? n : "travail", j.startMs.load(std::memory_order_relaxed)});
        }
    std::stable_sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) { return a.start < b.start; });
    const std::int64_t now = steadyMs();
    std::vector<JobInfo> out;
    out.reserve(seen.size());
    for (const auto& s : seen) out.push_back({s.name, static_cast<double>(std::max<std::int64_t>(0, now - s.start)) / 1000.0, -1.0});
    return out;
}

std::vector<JobInfo> finishedJobs(std::size_t max) {
    std::vector<JobInfo> out;
    const std::uint64_t next = g_finishedNext.load(std::memory_order_acquire);
    const std::int64_t now = steadyMs();
    for (std::uint64_t s = next; s >= 1 && next - s < kFinishedJobs && out.size() < max; --s) {
        const FinishedSlot& f = g_finished[(s - 1) % kFinishedJobs];
        const std::uint64_t a = f.seq.load(std::memory_order_acquire);
        if (a != s) continue;
        const char* n = f.name.load(std::memory_order_relaxed);
        const std::int64_t d = f.durationMs.load(std::memory_order_relaxed);
        const std::int64_t e = f.endMs.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (f.seq.load(std::memory_order_relaxed) != a) continue;
        out.push_back({n ? n : "travail", static_cast<double>(d) / 1000.0, static_cast<double>(std::max<std::int64_t>(0, now - e)) / 1000.0});
    }
    return out;
}

// ======================================================================= le processus ====
namespace {

#if defined(_WIN32)
// PROCESS_MEMORY_COUNTERS_EX (psapi.h), recopiee : rien de plus a inclure ni a lier.
struct MemCounters {
    DWORD  cb;
    DWORD  pageFaults;
    SIZE_T peakWorkingSet;
    SIZE_T workingSet;
    SIZE_T quotaPeakPaged;
    SIZE_T quotaPaged;
    SIZE_T quotaPeakNonPaged;
    SIZE_T quotaNonPaged;
    SIZE_T pagefile;
    SIZE_T peakPagefile;
    SIZE_T privateUsage;
};
using GetInfo = BOOL(WINAPI*)(HANDLE, void*, DWORD);

GetInfo memoryReader() {
    // K32GetProcessMemoryInfo est dans kernel32 depuis Windows 7 ; avant, dans psapi.dll : cherche a la
    // demande (comme AiMemory.cpp), rien de plus a lier.
    static const GetInfo fn = [] {
        GetInfo f = nullptr;
        if (HMODULE k = GetModuleHandleW(L"kernel32.dll"))
            f = reinterpret_cast<GetInfo>(reinterpret_cast<void*>(GetProcAddress(k, "K32GetProcessMemoryInfo")));
        if (!f)
            if (HMODULE p = LoadLibraryW(L"psapi.dll"))
                f = reinterpret_cast<GetInfo>(reinterpret_cast<void*>(GetProcAddress(p, "GetProcessMemoryInfo")));
        return f;
    }();
    return fn;
}

std::uint64_t ticks(const FILETIME& ft) {
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | static_cast<std::uint64_t>(ft.dwLowDateTime);
}
#endif

void readProcess(ProcessStats& s) {
    s.cores = std::thread::hardware_concurrency();
#if defined(_WIN32)
    MemCounters mc;
    std::memset(&mc, 0, sizeof mc);
    mc.cb = static_cast<DWORD>(sizeof mc);
    if (const GetInfo fn = memoryReader(); fn && fn(GetCurrentProcess(), &mc, mc.cb)) {
        s.memoryKnown = true;
        s.workingSet = static_cast<std::uint64_t>(mc.workingSet);
        s.privateBytes = static_cast<std::uint64_t>(mc.privateUsage);
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        s.cpuKnown = true;
        s.cpuSeconds = static_cast<double>(ticks(kernel) + ticks(user)) / 1e7;   // des pas de 100 ns
    }
#else
#  if defined(__linux__)
    if (std::FILE* f = std::fopen("/proc/self/statm", "r")) {
        unsigned long long size = 0, resident = 0, shared = 0;
        if (std::fscanf(f, "%llu %llu %llu", &size, &resident, &shared) == 3) {
            const long page = sysconf(_SC_PAGESIZE);
            const auto bytes = static_cast<std::uint64_t>(page > 0 ? page : 4096);
            s.memoryKnown = true;
            s.workingSet = resident * bytes;
            s.privateBytes = (resident > shared ? resident - shared : 0) * bytes;
        }
        std::fclose(f);
    }
#  endif
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        s.cpuKnown = true;
        s.cpuSeconds = static_cast<double>(ru.ru_utime.tv_sec) + static_cast<double>(ru.ru_utime.tv_usec) / 1e6
                     + static_cast<double>(ru.ru_stime.tv_sec) + static_cast<double>(ru.ru_stime.tv_usec) / 1e6;
    }
#endif
}

} // namespace

ProcessStats processStats() {
    static ProcessStats last;
    static std::int64_t lastAt = -1;          // ms (horloge du systeme)
    static double       prevCpu = -1.0;
    static std::int64_t prevAt = -1;
    const std::int64_t now = steadyMs();
    if (lastAt >= 0 && now - lastAt < 1000) return last;
    ProcessStats s;
    readProcess(s);
    if (s.cpuKnown && prevCpu >= 0.0 && now > prevAt) s.cpuPercent = std::max(0.0, (s.cpuSeconds - prevCpu) * 100000.0 / static_cast<double>(now - prevAt));
    if (s.cpuKnown) {
        prevCpu = s.cpuSeconds;
        prevAt = now;
    }
    last = s;
    lastAt = now;
    return s;
}

// ======================================================================= les nombres ====
std::string formatMs(double ms) {
    if (!(ms > 0.0)) return "0 ms";
    if (ms < 10.0) return fixed(ms, 2) + " ms";
    if (ms < 100.0) return fixed(ms, 1) + " ms";
    if (ms < 1000.0) return fixed(ms, 0) + " ms";
    const double s = ms / 1000.0;
    if (s < 10.0) return fixed(s, 2) + " s";
    if (s < 100.0) return fixed(s, 1) + " s";
    return fixed(s, 0) + " s";
}

std::string formatBytes(std::uint64_t bytes) {
    const double mo = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mo < 1.0) return fixed(static_cast<double>(bytes) / 1024.0, 0) + " Ko";
    if (mo < 1024.0) return fixed(mo, 0) + " Mo";
    return fixed(mo / 1024.0, 1) + " Go";
}

std::string formatPercent(double fraction) { return fixed(std::max(0.0, fraction) * 100.0, 1) + " %"; }

std::string windowLabel(int seconds) {
    if (seconds <= 0) return "depuis le lancement";
    return std::to_string(seconds) + " s";
}

// ======================================================================= le rapport ====
std::string report(int windowSeconds, std::size_t topParts, std::size_t lastStutters) {
    const Window w = window(windowSeconds);
    const Window all = window(0);
    const ProcessStats ps = processStats();
    std::string r;
    char when[32];
    core::trail::formatDateTime(core::trail::nowMs(), when);
    r += core::productName() + " " XPG_ANALYZER_VERSION " \xE2\x80\x94 rapport de performances (" + when + ")\n";
#if defined(_WIN32)
    const char* osName = "Windows";
#elif defined(__linux__)
    const char* osName = "Linux";
#else
    const char* osName = "autre";
#endif
    r += std::string("Syst\xC3\xA8me : ") + osName + (sizeof(void*) == 8 ? " (64 bits)" : " (32 bits)") + ", " + std::to_string(ps.cores)
         + " processeurs logiques\n";
    r += "Mesure : le fil de l'interface, depuis " + span(all.span) + " ; fen\xC3\xAAtre du tableau : " + windowLabel(w.seconds) + "\n";
    r += "Cadence (" + windowLabel(w.seconds) + ") : " + fixed(w.fps(), 1) + " images/s ; image moyenne " + formatMs(w.averageFrameMs())
         + " ; pire image " + formatMs(static_cast<double>(w.worstFrameNs) / 1e6) + "\n";
    r += "Depuis le lancement : " + thousands(all.frames) + " images ; pire image " + formatMs(static_cast<double>(all.worstFrameNs) / 1e6) + "\n";
    r += "Processus : ";
    if (ps.memoryKnown) r += "m\xC3\xA9moire " + formatBytes(ps.workingSet) + " (priv\xC3\xA9" "e " + formatBytes(ps.privateBytes) + ")";
    else r += "m\xC3\xA9moire inconnue";
    if (ps.cpuKnown) {
        r += " ; processeur ";
        r += ps.cpuPercent >= 0.0 ? fixed(ps.cpuPercent, 0) + " % (100 % = un processeur logique)" : std::string("(mesure en cours)");
        r += ", " + span(ps.cpuSeconds) + " en tout";
    }
    r += "\n";
    r += "Saccades : seuil " + std::to_string(stutterThresholdMs()) + " ms ; " + thousands(stutterCount()) + " depuis le lancement ("
         + std::to_string(kStutterCapacity) + " gard\xC3\xA9" "es au plus)\n";
    {
        const auto run = runningJobs();
        r += "Travaux de fond en cours : ";
        if (run.empty()) r += "aucun";
        for (std::size_t k = 0; k < run.size(); ++k)
            r += (k ? " ; " : "") + std::string(run[k].name) + " (depuis " + formatMs(run[k].seconds * 1000.0) + ")";
        r += "\n";
        const auto done = finishedJobs(10);
        if (!done.empty()) {
            r += "Travaux de fond finis r\xC3\xA9" "cemment : ";
            for (std::size_t k = 0; k < done.size(); ++k)
                r += (k ? " ; " : "") + std::string(done[k].name) + " " + formatMs(done[k].seconds * 1000.0) + " (il y a " + span(done[k].endedAgo) + ")";
            r += "\n";
        }
    }
    // ---- les parties
    r += "\nLes " + std::to_string(topParts) + " parties qui consomment le plus (" + windowLabel(w.seconds)
         + " ; temps propre, sans les parties mesur\xC3\xA9" "es dedans) :\n";
    r += pad("Part", 9) + pad("Cumul", 10) + pad("Moy./image", 12) + pad("Pire image", 12) + pad("Appels", 11) + "Partie\n";
    const double total = static_cast<double>(std::max<std::int64_t>(1, w.frameNs));
    const double frames = static_cast<double>(std::max<std::uint64_t>(1, w.frames));
    for (std::size_t k = 0; k < w.parts.size() && k < topParts; ++k) {
        const PartStat& p = w.parts[k];
        r += pad(formatPercent(static_cast<double>(p.selfNs) / total), 9) + pad(formatMs(static_cast<double>(p.selfNs) / 1e6), 10)
             + pad(formatMs(static_cast<double>(p.selfNs) / 1e6 / frames), 12) + pad(formatMs(static_cast<double>(p.worstSelfNs) / 1e6), 12)
             + pad(thousands(p.calls), 11) + p.name + "\n";
    }
    if (w.parts.empty()) r += "(rien de mesur\xC3\xA9 sur cette fen\xC3\xAAtre)\n";
    // ---- les saccades
    const auto list = stutters();
    const std::int64_t threshold = static_cast<std::int64_t>(stutterThresholdMs()) * kNsPerMs;
    r += "\nLes " + std::to_string(lastStutters) + " derni\xC3\xA8res saccades (au moins " + std::to_string(stutterThresholdMs())
         + " ms), la plus r\xC3\xA9" "cente d'abord :\n";
    std::size_t shown = 0;
    for (auto it = list.rbegin(); it != list.rend() && shown < lastStutters; ++it) {
        if (it->frameNs < threshold) continue;
        r += stutterClock(*it) + "  " + pad(formatMs(static_cast<double>(it->frameNs) / 1e6), 10) + stutterParts(*it) + "\n";
        ++shown;
    }
    if (shown == 0) r += "(aucune)\n";
    return r;
}

// ======================================================================= les essais ====
void setTestClock(const std::int64_t* ns) noexcept { g_testClock = ns; }

void resetForTests() noexcept {
    // Les parties gardent leur numero : les statiques de XPG_MESURE le tiennent.
    for (auto& a : g_cur) a = Acc{};
    g_touchedCount = 0;
    g_top = nullptr;
    g_haveLast = false;
    g_lastEnd = 0;
    for (auto& b : g_buckets) {
        b.stamp = 0;
        b.frames = 0;
        b.frameSum = 0;
        b.frameMax = 0;
        for (auto& c : b.parts) c = Cell{};
    }
    for (auto& c : g_total) c = Cell{};
    g_totalFrames = 0;
    g_totalFrameNs = 0;
    g_totalFrameMax = 0;
    g_epochSet = false;
    g_epoch = 0;
    g_resetAt = 0;
    g_stutterNext = 0;
    for (auto& s : g_stutters) s = Stutter{};
    g_thresholdNs = kDefaultThresholdMs * kNsPerMs;
}

} // namespace core::perf
