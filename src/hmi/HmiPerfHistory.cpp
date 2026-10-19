// =============================================================================
//  hmi/HmiPerfHistory.cpp - voir HmiPerfHistory.hpp
// =============================================================================
#include "HmiPerfHistory.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace hmi {

namespace {

// Un nombre a la francaise (l'export : separateur ;, virgule decimale).
std::string num(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s(buf);
    for (auto& ch : s)
        if (ch == '.') ch = ',';
    return s;
}

std::string csvCell(std::string s) {
    if (s.find_first_of(";\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (const char ch : s) {
        if (ch == '"') out += "\"\"";
        else if (ch == '\n') out += ' ';
        else out += ch;
    }
    return out + "\"";
}

// Le centile `q` (0..1) d'une liste (la liste est reordonnee).
float percentile(std::vector<float>& v, double q) {
    if (v.empty()) return 0.f;
    const auto k = static_cast<std::size_t>(std::clamp(std::ceil(q * static_cast<double>(v.size())) - 1.0, 0.0, static_cast<double>(v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

} // namespace

// ================================================================ le temps habituel ====
double UsualTimes::usual(const std::string& key) const {
    const auto it = map_.find(key);
    if (it == map_.end() || it->second.n < 3) return 0.0;
    std::vector<float> v(it->second.v.begin(), it->second.v.begin() + static_cast<std::ptrdiff_t>(it->second.n));
    return percentile(v, 0.5);
}

std::size_t UsualTimes::runs(const std::string& key) const {
    const auto it = map_.find(key);
    return it == map_.end() ? 0 : it->second.runs;
}

std::size_t UsualTimes::slowRuns(const std::string& key) const {
    const auto it = map_.find(key);
    return it == map_.end() ? 0 : it->second.slow;
}

void UsualTimes::add(const std::string& key, double ms, bool slow) {
    auto& r = map_[key];
    r.v[r.at] = static_cast<float>(ms);
    r.at = (r.at + 1) % kKeep;
    r.n = std::min(r.n + 1, kKeep);
    ++r.runs;
    if (slow) ++r.slow;
}

// ===================================================================== les lenteurs ====
std::string slowKindName(SlowKind k) noexcept {
    switch (k) {
        case SlowKind::Action:    return "Action";
        case SlowKind::Script:    return "Script";
        case SlowKind::View:      return "Vue";
        case SlowKind::Popup:     return "Popup";
        case SlowKind::Cycle:     return "Cycle";
        case SlowKind::Equipment: return "\xC3\x89quipement";
    }
    return {};
}

const SlowEntry& SlowLog::add(SlowEntry e) {
    e.serial = ++serial_;
    entries_.push_back(std::move(e));
    while (entries_.size() > kKeep) entries_.pop_front();
    ++revision_;
    return entries_.back();
}

void SlowLog::clear() {
    entries_.clear();
    ++revision_;
}

const SlowEntry* SlowLog::find(std::uint64_t serial) const {
    for (const auto& e : entries_)
        if (e.serial == serial) return &e;
    return nullptr;
}

std::size_t SlowLog::count(SlowKind k) const {
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [k](const SlowEntry& e) { return e.kind == k; }));
}

std::string SlowLog::csv() const {
    std::string out = "Heure;Dur\xC3\xA9" "e (ms);Habituel (ms);Rapport;Genre;Quoi;O\xC3\xB9;Utilisateur;Vue;Dedans\r\n";
    for (const auto& e : entries_) {
        std::string inside;
        for (const auto& p : e.inside) inside += (inside.empty() ? "" : " | ") + std::string(static_cast<std::size_t>(p.depth) * 2, ' ') + p.what + " " + slowMs(p.ms);
        out += csvCell(e.clock) + ";" + num(e.ms, 1) + ";" + (e.usual > 0 ? num(e.usual, 1) : std::string{}) + ";"
               + (e.usual > 0 ? num(e.ratio(), 1) : std::string{}) + ";" + csvCell(slowKindName(e.kind)) + ";" + csvCell(e.what) + ";"
               + csvCell(e.where) + ";" + csvCell(e.user) + ";" + csvCell(e.view) + ";" + csvCell(inside) + "\r\n";
    }
    return out;
}

// ==================================================== les performances dans le temps ====
void PerfHistory::reset(double now) {
    seconds_.clear();
    raw_.clear();
    posts_.clear();
    postOrder_.clear();
    open_ = Open{};
    open_.t = std::floor(now);
    since_ = now;
    wallSince_ = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    ++revision_;
}

std::string PerfHistory::clockAt(double t, bool millis) const {
    const double wall = wallSince_ + (t - since_);
    const auto secs = static_cast<std::time_t>(std::floor(wall));
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char buf[32];
    if (millis)
        std::snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec,
                      static_cast<int>(std::floor((wall - std::floor(wall)) * 1000.0)));
    else
        std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

void PerfHistory::cycle(double now, double ms, double periodMs) {
    advance(now);
    if (open_.cycles.empty() || ms > *std::max_element(open_.cycles.begin(), open_.cycles.end())) open_.maxAt = now;
    open_.cycles.push_back(static_cast<float>(ms));
    periodMs_ = periodMs > 0 ? periodMs : periodMs_;
    if (ms > periodMs_) ++open_.overruns;
    raw_.push_back({static_cast<float>(now), static_cast<float>(ms)});
}

void PerfHistory::post(const std::string& key, const std::string& label, double ms) {
    auto [it, added] = posts_.try_emplace(key);
    if (added) postOrder_.push_back(key);
    if (it->second.label != label) it->second.label = label;
    it->second.runs.push_back({static_cast<float>(open_.t < 0 ? 0.0 : open_.t), static_cast<float>(ms)});
}

void PerfHistory::screen(double paintMs, double evaluateMs, std::size_t expressions) {
    if (paintMs >= 0) {
        open_.paintSum += paintMs;
        ++open_.paintN;
    }
    if (evaluateMs >= 0) {
        open_.evalSum += evaluateMs;
        ++open_.evalN;
    }
    open_.expressions = static_cast<std::uint32_t>(expressions);
}

void PerfHistory::exchanges(std::uint32_t n, double responseMs) {
    open_.exchanges += n;
    if (n > 0 && responseMs > 0) {
        open_.exchangeMsSum += responseMs;
        ++open_.exchangeMsN;
    }
}

void PerfHistory::advance(double now) {
    if (!std::isfinite(now)) return;
    if (open_.t < 0) open_.t = std::floor(now);
    if (now < open_.t + 1.0) return;
    // Une longue absence (l'IHM arretee, le poste endormi) : pas de secondes vides a la chaine.
    const double next = now - open_.t > 10.0 ? std::floor(now) : open_.t + 1.0;
    close(open_.t);
    open_ = Open{};
    open_.t = next;
    // Les secondes vides entre deux (dix au plus) : Open{} remet t a -1, on garde la suivante a part.
    while (now >= open_.t + 1.0) {
        const double t = open_.t;
        close(t);
        open_ = Open{};
        open_.t = t + 1.0;
    }
    // La derniere heure seulement, a la seconde (les cycles, les postes).
    const auto cut = static_cast<float>(now - kFineSeconds);
    while (!raw_.empty() && raw_.front().first < cut) raw_.pop_front();
    for (auto& [key, p] : posts_)
        while (!p.runs.empty() && p.runs.front().t < cut) {
            p.olderTotal += p.runs.front().ms;
            ++p.olderTimes;
            p.olderMax = std::max(p.olderMax, p.runs.front().ms);
            p.runs.pop_front();
        }
}

void PerfHistory::close(double t) {
    PerfSecond s;
    s.t = t;
    s.span = 1.f;
    s.cycles = static_cast<std::uint32_t>(open_.cycles.size());
    if (!open_.cycles.empty()) {
        double total = 0;
        for (const float v : open_.cycles) total += v;
        s.mean = static_cast<float>(total / static_cast<double>(open_.cycles.size()));
        s.max = *std::max_element(open_.cycles.begin(), open_.cycles.end());
        s.maxAt = open_.maxAt;
        auto copy = open_.cycles;
        s.median = percentile(copy, 0.5);
        s.p95 = percentile(copy, 0.95);
    }
    s.paint = open_.paintN ? static_cast<float>(open_.paintSum / open_.paintN) : 0.f;
    s.evaluate = open_.evalN ? static_cast<float>(open_.evalSum / open_.evalN) : 0.f;
    s.overruns = open_.overruns;
    s.slow = open_.slow;
    s.writes = open_.writes;
    s.exchanges = open_.exchanges;
    s.exchangeMs = open_.exchangeMsN ? static_cast<float>(open_.exchangeMsSum / open_.exchangeMsN) : 0.f;
    s.expressions = open_.expressions;
    seconds_.push_back(s);
    squeeze();
    ++revision_;
}

// Au-dela d'une heure, dix secondes en une mesure ; au-dela de huit heures, plus rien.
void PerfHistory::squeeze() {
    if (seconds_.empty()) return;
    const double last = seconds_.back().t;
    // Les plus anciennes d'abord : les tranches de 10 s deja faites, puis les secondes.
    std::size_t first = 0;
    while (first < seconds_.size() && seconds_[first].span > 1.5f) ++first;
    while (first < seconds_.size() && seconds_[first].t < last - kFineSeconds) {
        const double bucket = std::floor(seconds_[first].t / 10.0) * 10.0;
        std::size_t end = first;
        while (end < seconds_.size() && seconds_[end].span < 1.5f && std::floor(seconds_[end].t / 10.0) * 10.0 == bucket) ++end;
        if (end == seconds_.size()) break;                     // la tranche n'est pas finie
        PerfSecond m;
        m.t = bucket;
        m.span = 10.f;
        std::vector<float> medians, p95s;
        double total = 0, paint = 0, eval = 0, exch = 0;
        std::size_t paintN = 0, evalN = 0, exchN = 0;
        for (std::size_t k = first; k < end; ++k) {
            const auto& s = seconds_[k];
            m.cycles += s.cycles;
            m.overruns += s.overruns;
            m.slow += s.slow;
            m.writes += s.writes;
            m.exchanges += s.exchanges;
            m.expressions = s.expressions;
            total += static_cast<double>(s.mean) * s.cycles;
            if (s.max > m.max) {
                m.max = s.max;
                m.maxAt = s.maxAt;
            }
            if (s.cycles) {
                medians.push_back(s.median);
                p95s.push_back(s.p95);
            }
            if (s.paint > 0) { paint += s.paint; ++paintN; }
            if (s.evaluate > 0) { eval += s.evaluate; ++evalN; }
            if (s.exchangeMs > 0) { exch += s.exchangeMs; ++exchN; }
        }
        m.mean = m.cycles ? static_cast<float>(total / m.cycles) : 0.f;
        m.median = percentile(medians, 0.5);
        m.p95 = p95s.empty() ? 0.f : *std::max_element(p95s.begin(), p95s.end());
        m.paint = paintN ? static_cast<float>(paint / static_cast<double>(paintN)) : 0.f;
        m.evaluate = evalN ? static_cast<float>(eval / static_cast<double>(evalN)) : 0.f;
        m.exchangeMs = exchN ? static_cast<float>(exch / static_cast<double>(exchN)) : 0.f;
        seconds_.erase(seconds_.begin() + static_cast<std::ptrdiff_t>(first), seconds_.begin() + static_cast<std::ptrdiff_t>(end));
        seconds_.insert(seconds_.begin() + static_cast<std::ptrdiff_t>(first), m);
        ++first;
    }
    while (!seconds_.empty() && seconds_.front().t < last - kCoarseSeconds) seconds_.pop_front();
}

std::vector<const PerfSecond*> PerfHistory::window(double now, double seconds) const {
    std::vector<const PerfSecond*> out;
    for (const auto& s : seconds_)
        if (seconds <= 0 || s.t + s.span > now - seconds) out.push_back(&s);
    return out;
}

PerfHistory::Summary PerfHistory::summary(double now, double seconds) const {
    Summary sm;
    const auto w = window(now, seconds);
    double total = 0, paint = 0, eval = 0, exch = 0;
    std::size_t paintN = 0, evalN = 0, exchN = 0;
    std::vector<float> p95s, paints;
    for (const auto* s : w) {
        sm.cycles += s->cycles;
        sm.overruns += s->overruns;
        sm.slow += s->slow;
        sm.writes += s->writes;
        sm.exchanges += s->exchanges;
        sm.span += s->span;
        total += static_cast<double>(s->mean) * s->cycles;
        if (s->max > sm.max) {
            sm.max = s->max;
            sm.maxAt = s->maxAt;
        }
        if (s->cycles) p95s.push_back(s->p95);
        if (s->paint > 0) { paint += s->paint; ++paintN; paints.push_back(s->paint); }
        if (s->evaluate > 0) { eval += s->evaluate; ++evalN; }
        if (s->exchangeMs > 0) { exch += s->exchangeMs; ++exchN; }
        sm.expressions = s->expressions;
    }
    sm.mean = sm.cycles ? total / static_cast<double>(sm.cycles) : 0.0;
    // Le p95 : sur les cycles eux-memes dans la derniere heure ; au-dela, sur ceux des mesures.
    if (seconds > 0 && seconds <= kFineSeconds) {
        std::vector<float> v;
        const auto from = static_cast<float>(now - seconds);
        for (const auto& [t, ms] : raw_)
            if (t >= from) v.push_back(ms);
        sm.p95 = v.empty() ? 0.0 : percentile(v, 0.95);
    } else {
        sm.p95 = percentile(p95s, 0.95);
    }
    sm.load = periodMs_ > 0 ? sm.mean / periodMs_ : 0.0;
    sm.worstLoad = periodMs_ > 0 ? sm.max / periodMs_ : 0.0;
    sm.worstAt = sm.maxAt;
    sm.paint = paintN ? paint / static_cast<double>(paintN) : 0.0;
    sm.paintP95 = percentile(paints, 0.95);
    sm.evaluate = evalN ? eval / static_cast<double>(evalN) : 0.0;
    sm.writesPerS = sm.span > 0 ? static_cast<double>(sm.writes) / sm.span : 0.0;
    sm.exchangesPerS = sm.span > 0 ? static_cast<double>(sm.exchanges) / sm.span : 0.0;
    sm.exchangeMs = exchN ? exch / static_cast<double>(exchN) : 0.0;
    return sm;
}

std::vector<PerfHistory::PostRow> PerfHistory::posts(double now, double seconds) const {
    std::vector<PostRow> out;
    const auto sm = summary(now, seconds);
    const double cycleTotal = sm.mean * static_cast<double>(sm.cycles);
    const double from = seconds > 0 ? now - seconds : since_;
    const bool all = seconds <= 0 || seconds > kFineSeconds;
    constexpr std::size_t kTrend = 60;
    const double span = std::max(1.0, now - from);
    for (const auto& key : postOrder_) {
        const auto& p = posts_.at(key);
        PostRow row;
        row.key = key;
        row.label = p.label;
        double total = all ? p.olderTotal : 0.0;
        row.times = all ? p.olderTimes : 0;
        row.max = all ? p.olderMax : 0.0;
        std::vector<float> v;
        std::vector<double> sums(kTrend, 0.0);
        std::vector<std::size_t> counts(kTrend, 0);
        for (const auto& r : p.runs) {
            if (!all && r.t < from) continue;
            total += r.ms;
            ++row.times;
            row.max = std::max<double>(row.max, r.ms);
            v.push_back(r.ms);
            const auto b = static_cast<std::size_t>(std::clamp((r.t - from) / span * kTrend, 0.0, static_cast<double>(kTrend - 1)));
            sums[b] += r.ms;
            ++counts[b];
        }
        if (row.times == 0) continue;
        row.mean = total / static_cast<double>(row.times);
        row.p95 = v.empty() ? row.max : percentile(v, 0.95);
        row.share = cycleTotal > 0 ? total / cycleTotal : 0.0;
        for (std::size_t b = 0; b < kTrend; ++b) row.trend.push_back(counts[b] ? static_cast<float>(sums[b] / static_cast<double>(counts[b])) : 0.f);
        out.push_back(std::move(row));
    }
    std::stable_sort(out.begin(), out.end(), [](const PostRow& a, const PostRow& b) {
        const bool ra = a.key == "reste", rb = b.key == "reste";       // le reste, toujours en bas
        if (ra != rb) return rb;
        return a.share > b.share;
    });
    return out;
}

std::string PerfHistory::csv() const {
    std::string out = "D\xC3\xA9" "but (s);Dur\xC3\xA9" "e (s);Cycles;M\xC3\xA9" "diane (ms);p95 (ms);Maximum (ms);Moyenne (ms);D\xC3\xA9passements;Lenteurs;"
                      "Dessin (ms);Expressions (ms);\xC3\x89" "critures;\xC3\x89" "changes;R\xC3\xA9ponse (ms)\r\n";
    for (const auto& s : seconds_)
        out += num(s.t - since_, 0) + ";" + num(s.span, 0) + ";" + std::to_string(s.cycles) + ";" + num(s.median, 2) + ";" + num(s.p95, 2) + ";"
               + num(s.max, 2) + ";" + num(s.mean, 2) + ";" + std::to_string(s.overruns) + ";" + std::to_string(s.slow) + ";" + num(s.paint, 2) + ";"
               + num(s.evaluate, 2) + ";" + std::to_string(s.writes) + ";" + std::to_string(s.exchanges) + ";" + num(s.exchangeMs, 1) + "\r\n";
    return out;
}

std::string slowMs(double ms) {
    if (ms >= 1000.0) return num(ms / 1000.0, 1) + " s";
    if (ms >= 10.0) return std::to_string(static_cast<long long>(std::lround(ms))) + " ms";
    return num(ms, 1) + " ms";
}

std::string wallClockMs() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return buf;
}

} // namespace hmi
