// =============================================================================
//  core/CallTrail.cpp - 1.10.2 : l'historique interne des appels
// -----------------------------------------------------------------------------
//  Le tampon : kCapacity cases. Ecrire = prendre un numero (fetch_add), marquer
//  la case « en cours », la remplir, puis y poser le numero (release). Lire =
//  lire le numero de la case, copier, relire le numero : s'il n'a pas change et
//  qu'il est celui attendu, la copie est entiere (le principe d'un seqlock).
//  Rien n'alloue, rien n'attend : le gestionnaire d'un plantage peut lire.
//
//  Les fils : kMaxThreads places. Un fil prend une place a sa premiere ecriture
//  et la rend a sa fin (un thread_local qui la libere). Sa pile de portees est
//  dans sa place, en atomiques, pour que le chien de garde la lise sans verrou.
// =============================================================================
#include "CallTrail.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace core::trail {
namespace {

constexpr std::uint64_t kWriting = ~std::uint64_t{0};

struct Slot {
    std::atomic<std::uint64_t> stamp{0};
    Entry entry;
};

struct ThreadSlot {
    std::atomic<int> state{0};                 // 0 libre, 1 pris, 2 en preparation
    std::atomic<std::uint16_t> id{0};
    char name[kThreadNameMax] = {};
    std::atomic<std::size_t> depth{0};
    std::atomic<const char*> frames[kMaxDepth] = {};
    std::atomic<std::uint64_t> since[kMaxDepth] = {};
};

Slot g_ring[kCapacity];
ThreadSlot g_threads[kMaxThreads];
std::atomic<std::uint64_t> g_next{0};
std::atomic<std::uint16_t> g_nextThreadId{0};
std::atomic<bool> g_enabled{true};
std::atomic<std::int64_t> g_offsetMs{LLONG_MIN};

// Le fil appelant : trivial (pas d'enveloppe d'acces au TLS dans le chemin chaud).
struct LocalData {
    ThreadSlot* slot;
    std::uint16_t id;
    bool claimed;
};
thread_local LocalData t_data{nullptr, 0, false};

// Rend la place du fil a sa fin. A part de LocalData pour que celle-ci reste triviale :
// une portee ouverte apres sa destruction ecrit encore dans le tampon, sans pile.
struct Releaser {
    ~Releaser() {
        if (t_data.slot) {
            t_data.slot->depth.store(0, std::memory_order_relaxed);
            t_data.slot->state.store(0, std::memory_order_release);
            t_data.slot = nullptr;
        }
    }
};
thread_local Releaser t_releaser;

LocalData& local() noexcept {
    if (!t_data.claimed) {
        t_data.claimed = true;
        t_data.id = static_cast<std::uint16_t>(g_nextThreadId.fetch_add(1, std::memory_order_relaxed) + 1);
        for (auto& s : g_threads) {
            int expected = 0;
            if (s.state.compare_exchange_strong(expected, 2, std::memory_order_acq_rel)) {
                s.id.store(t_data.id, std::memory_order_relaxed);
                s.depth.store(0, std::memory_order_relaxed);
                s.name[0] = '\0';
                s.state.store(1, std::memory_order_release);
                t_data.slot = &s;
                (void)&t_releaser;   // premier usage : son destructeur est inscrit pour ce fil
                break;
            }
        }
    }
    return t_data;
}

std::uint64_t nowUs() noexcept {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
}

// Copie au plus `size - 1` octets sans couper un caractere UTF-8 ; rend la longueur.
std::size_t copyText(char* out, std::size_t size, const char* text, std::size_t len) noexcept {
    if (size == 0) return 0;
    std::size_t n = std::min(len, size - 1);
    if (n < len)
        while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;
    std::memcpy(out, text, n);
    out[n] = '\0';
    return n;
}

void write(Kind kind, const char* text, std::size_t len, std::uint64_t us, std::uint32_t micros,
           std::size_t depth, std::uint16_t thread) noexcept {
    const std::uint64_t seq = g_next.fetch_add(1, std::memory_order_relaxed) + 1;
    Slot& s = g_ring[(seq - 1) % kCapacity];
    s.stamp.store(kWriting, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    Entry& e = s.entry;
    e.seq = seq;
    e.ms = us / 1000;
    e.micros = micros;
    e.thread = thread;
    e.kind = kind;
    e.depth = static_cast<std::uint8_t>(std::min<std::size_t>(depth, 255));
    copyText(e.text, kTextMax, text, len);
    s.stamp.store(seq, std::memory_order_release);
}

bool readSlot(std::uint64_t seq, Entry& out) noexcept {
    const Slot& s = g_ring[(seq - 1) % kCapacity];
    const std::uint64_t a = s.stamp.load(std::memory_order_acquire);
    if (a != seq) return false;
    std::memcpy(static_cast<void*>(&out), static_cast<const void*>(&s.entry), sizeof(Entry));
    std::atomic_thread_fence(std::memory_order_acquire);
    const std::uint64_t b = s.stamp.load(std::memory_order_relaxed);
    return a == b;
}

// ---- l'heure, sans localtime (les jours civils, d'apres H. Hinnant) ----------------
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) noexcept {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}
void civilFromDays(std::int64_t z, int& y, unsigned& m, unsigned& d) noexcept {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int>(static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2));
}

// Un petit ecrivain dans un tampon fixe (pas de snprintf dans le gestionnaire).
struct Out {
    char* p;
    std::size_t size;
    std::size_t n = 0;
    std::size_t cols = 0;   // les caracteres affiches (un caractere UTF-8 = une colonne)
    void put(char c) noexcept {
        if (n + 1 < size) {
            p[n++] = c;
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++cols;
        }
    }
    void str(const char* s) noexcept { while (s && *s) put(*s++); }
    void str(const char* s, std::size_t len) noexcept { for (std::size_t i = 0; i < len && s[i]; ++i) put(s[i]); }
    void num(std::uint64_t v, int width = 0) noexcept {
        char b[24];
        int k = 0;
        do { b[k++] = static_cast<char>('0' + v % 10); v /= 10; } while (v && k < 24);
        while (k < width && k < 24) b[k++] = '0';
        while (k) put(b[--k]);
    }
    void pad(std::size_t column) noexcept { while (cols < column && n + 1 < size) put(' '); }
    void end() noexcept { if (size) p[std::min(n, size - 1)] = '\0'; }
};

} // namespace

const char* kindName(Kind k) noexcept {
    switch (k) {
        case Kind::Info:     return "note";
        case Kind::Action:   return "action";
        case Kind::Menu:     return "menu";
        case Kind::Dialog:   return "fen\xC3\xAAtre";
        case Kind::Document: return "document";
        case Kind::File:     return "fichier";
        case Kind::Sim:      return "simulation";
        case Kind::Script:   return "script";
        case Kind::Build:    return "compiler";
        case Kind::Warning:  return "avertissement";
        case Kind::Error:    return "erreur";
        case Kind::Enter:    return "entr\xC3\xA9" "e";
        case Kind::Leave:    return "sortie";
        case Kind::Watchdog: return "chien de garde";
        case Kind::Crash:    return "plantage";
    }
    return "?";
}

void note(Kind kind, std::string_view text) noexcept {
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    auto& l = local();
    const std::size_t depth = l.slot ? l.slot->depth.load(std::memory_order_relaxed) : 0;
    write(kind, text.data(), text.size(), nowUs(), 0, depth, l.id);
}

void notef(Kind kind, const char* format, ...) noexcept {
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    char buffer[kTextMax * 2];
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    if (n < 0) return;
    note(kind, std::string_view(buffer, std::min<std::size_t>(static_cast<std::size_t>(n), sizeof buffer - 1)));
}

void nameThread(const char* name) noexcept {
    auto& l = local();
    if (l.slot && name) copyText(l.slot->name, kThreadNameMax, name, std::strlen(name));
}

std::uint16_t currentThread() noexcept { return local().id; }

void setEnabled(bool on) noexcept { g_enabled.store(on, std::memory_order_relaxed); }
bool enabled() noexcept { return g_enabled.load(std::memory_order_relaxed); }

Scope::Scope(const char* name) noexcept : Scope(name, std::string_view{}) {}

Scope::Scope(const char* name, std::string_view detail) noexcept
    : name_(name ? name : "?"), startUs_(0), pushed_(false) {
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    auto& l = local();
    startUs_ = nowUs();
    std::size_t depth = 0;
    if (l.slot) {
        depth = l.slot->depth.load(std::memory_order_relaxed);
        if (depth < kMaxDepth) {
            l.slot->frames[depth].store(name_, std::memory_order_relaxed);
            l.slot->since[depth].store(startUs_ / 1000, std::memory_order_relaxed);
        }
        l.slot->depth.store(depth + 1, std::memory_order_release);
    }
    pushed_ = true;
    if (detail.empty()) {
        write(Kind::Enter, name_, std::strlen(name_), startUs_, 0, depth, l.id);
    } else {
        char text[kTextMax];
        Out o{text, sizeof text};
        o.str(name_);
        o.str(" : ");
        o.str(detail.data(), detail.size());
        o.end();
        write(Kind::Enter, text, o.n, startUs_, 0, depth, l.id);
    }
}

Scope::~Scope() {
    if (!pushed_) return;
    LocalData& l = t_data;
    const std::uint64_t us = nowUs();
    std::size_t depth = 0;
    if (l.slot) {
        depth = l.slot->depth.load(std::memory_order_relaxed);
        if (depth > 0) --depth;
        l.slot->depth.store(depth, std::memory_order_release);
    }
    const std::uint64_t spent = us > startUs_ ? us - startUs_ : 0;
    write(Kind::Leave, name_, std::strlen(name_), us,
          static_cast<std::uint32_t>(std::min<std::uint64_t>(spent, UINT32_MAX)), depth, l.id);
}

std::uint64_t written() noexcept { return g_next.load(std::memory_order_acquire); }

void visitRecent(std::size_t max, void (*fn)(const Entry&, void*), void* context) noexcept {
    if (!fn || max == 0) return;
    const std::uint64_t last = g_next.load(std::memory_order_acquire);
    if (last == 0) return;
    const std::uint64_t span = std::min<std::uint64_t>({max, kCapacity, last});
    Entry e;
    for (std::uint64_t seq = last - span + 1; seq <= last; ++seq)
        if (readSlot(seq, e)) fn(e, context);
}

std::vector<Entry> recent(std::size_t max) {
    std::vector<Entry> out;
    out.reserve(std::min<std::size_t>(max, kCapacity));
    visitRecent(max, [](const Entry& e, void* c) { static_cast<std::vector<Entry>*>(c)->push_back(e); }, &out);
    return out;
}

namespace {
void copyStack(const ThreadSlot& s, ThreadStack& out) noexcept {
    out.thread = s.id.load(std::memory_order_relaxed);
    std::memcpy(out.name, s.name, kThreadNameMax);
    out.name[kThreadNameMax - 1] = '\0';
    out.depth = s.depth.load(std::memory_order_acquire);
    out.kept = std::min(out.depth, kMaxDepth);
    for (std::size_t i = 0; i < out.kept; ++i) {
        out.frames[i].name = s.frames[i].load(std::memory_order_relaxed);
        out.frames[i].sinceMs = s.since[i].load(std::memory_order_relaxed);
    }
}
} // namespace

std::size_t stacks(ThreadStack* out, std::size_t max) noexcept {
    std::size_t n = 0;
    for (const auto& s : g_threads) {
        if (n >= max) break;
        if (s.state.load(std::memory_order_acquire) != 1) continue;
        copyStack(s, out[n++]);
    }
    return n;
}

bool stackOf(std::uint16_t thread, ThreadStack& out) noexcept {
    for (const auto& s : g_threads)
        if (s.state.load(std::memory_order_acquire) == 1 && s.id.load(std::memory_order_relaxed) == thread) {
            copyStack(s, out);
            return true;
        }
    return false;
}

void threadName(std::uint16_t thread, char* out, std::size_t size) noexcept {
    if (!out || size == 0) return;
    out[0] = '\0';
    for (const auto& s : g_threads)
        if (s.state.load(std::memory_order_acquire) == 1 && s.id.load(std::memory_order_relaxed) == thread) {
            copyText(out, size, s.name, strnlen(s.name, kThreadNameMax));
            return;
        }
}

std::uint64_t nowMs() noexcept { return nowUs() / 1000; }

void refreshLocalOffset() noexcept {
    const std::time_t t = std::time(nullptr);
    std::tm lt{}, gt{};
#if defined(_WIN32)
    localtime_s(&lt, &t);
    gmtime_s(&gt, &t);
#else
    localtime_r(&t, &lt);
    gmtime_r(&t, &gt);
#endif
    auto seconds = [](const std::tm& x) {
        return daysFromCivil(x.tm_year + 1900, static_cast<unsigned>(x.tm_mon + 1), static_cast<unsigned>(x.tm_mday)) * 86400
             + x.tm_hour * 3600 + x.tm_min * 60 + x.tm_sec;
    };
    g_offsetMs.store((seconds(lt) - seconds(gt)) * 1000, std::memory_order_relaxed);
}

std::int64_t localOffsetMs() noexcept {
    std::int64_t o = g_offsetMs.load(std::memory_order_relaxed);
    if (o == LLONG_MIN) {
        refreshLocalOffset();
        o = g_offsetMs.load(std::memory_order_relaxed);
    }
    return o;
}

void formatClock(std::uint64_t utcMs, char* out) noexcept {
    const std::int64_t local = static_cast<std::int64_t>(utcMs) + localOffsetMs();
    const std::int64_t day = ((local / 1000) % 86400 + 86400) % 86400;
    Out o{out, 13};
    o.num(static_cast<std::uint64_t>(day / 3600), 2); o.put(':');
    o.num(static_cast<std::uint64_t>(day / 60 % 60), 2); o.put(':');
    o.num(static_cast<std::uint64_t>(day % 60), 2); o.put('.');
    o.num(static_cast<std::uint64_t>((local % 1000 + 1000) % 1000), 3);
    o.end();
}

void formatDateTime(std::uint64_t utcMs, char* out) noexcept {
    const std::int64_t local = static_cast<std::int64_t>(utcMs) + localOffsetMs();
    const std::int64_t secs = local / 1000;
    const std::int64_t days = secs >= 0 ? secs / 86400 : (secs - 86399) / 86400;
    const std::int64_t day = secs - days * 86400;
    int y = 0;
    unsigned m = 0, d = 0;
    civilFromDays(days, y, m, d);
    Out o{out, 20};
    o.num(static_cast<std::uint64_t>(y), 4); o.put('-');
    o.num(m, 2); o.put('-');
    o.num(d, 2); o.put(' ');
    o.num(static_cast<std::uint64_t>(day / 3600), 2); o.put(':');
    o.num(static_cast<std::uint64_t>(day / 60 % 60), 2); o.put(':');
    o.num(static_cast<std::uint64_t>(day % 60), 2);
    o.end();
}

std::size_t formatEntry(const Entry& e, char* out, std::size_t size) noexcept {
    Out o{out, size};
    char clock[16];
    formatClock(e.ms, clock);
    o.str(clock);
    o.str("  #");
    o.num(e.thread);
    char name[kThreadNameMax];
    threadName(e.thread, name, sizeof name);
    if (name[0]) { o.put(' '); o.str(name); }
    o.pad(o.cols < 30 ? 30 : o.cols + 1);
    o.str(kindName(e.kind));
    o.pad(o.cols < 46 ? 46 : o.cols + 1);
    for (int i = 0; i < std::min<int>(e.depth, 20); ++i) o.str("  ");
    if (e.kind == Kind::Enter) o.str("> ");
    else if (e.kind == Kind::Leave) o.str("< ");
    o.str(e.text, kTextMax);
    if (e.kind == Kind::Leave) {
        o.str("  (");
        if (e.micros >= 1000) {
            o.num(e.micros / 1000);
            o.put(',');
            o.num(e.micros % 1000 / 100);
            o.str(" ms)");
        } else {
            o.num(e.micros);
            o.str(" \xC2\xB5s)");
        }
    }
    o.end();
    return std::min(o.n, size ? size - 1 : 0);
}

void resetForTests() noexcept {
    g_next.store(0, std::memory_order_relaxed);
    for (auto& s : g_ring) s.stamp.store(0, std::memory_order_relaxed);
}

} // namespace core::trail
