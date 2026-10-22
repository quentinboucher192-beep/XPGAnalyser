// =============================================================================
//  hmi/HmiPidLearn.cpp - 1.12.24 : le Reglage PID, ce qu'il voit et ce qu'il en tire
// -----------------------------------------------------------------------------
//  Chaque mesure passe par une suite de guetteurs :
//    - les points tels quels (4 h) et les minutes (la memoire, 7 jours) : l'ecart,
//      le temps dans la tolerance, la saturation, l'AUTO, la sortie en moyennes
//      de 5 s (sa course, ses inversions a 1 % pres), la repartition de l'ecart ;
//    - la saturation, la mesure figee, le bruit (differences secondes) et la
//      resolution de la mesure ;
//    - la loi du bloc (du = Kp de + Kp/Ti e dt - Kp Td d2pv/dt, en AUTO) ;
//    - les echelons de consigne (depassement, temps de reponse) ;
//    - les perturbations : la boucle quitte la consigne sans que la consigne
//      bouge, y revient, et la sortie a compense ;
//    - les mouvements qui apprennent : un changement de consigne, la sortie
//      bougee en MANU. Du repos d'avant au repos d'apres, l'enregistrement est
//      juge (perturbation en cours, saturation, trou, mesure figee, trop court,
//      trop petit, mal explique) puis appris ou garde pour verifier.
//  L'heure : l'indice de Harris, l'oubli. La minute : le menage.
// =============================================================================
#include "HmiPid.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <type_traits>

namespace hmi::pid {

namespace {

constexpr double      kRawSeconds = 4 * 3600.0;   // les points gardes tels quels
constexpr std::size_t kRawMax     = 200000;
constexpr std::size_t kEventsMax  = 2000;
constexpr std::size_t kStepsMax   = 5000;
constexpr std::size_t kStoredMax  = 60;
constexpr std::size_t kSegPoints  = 4000;
constexpr double      kInf        = std::numeric_limits<double>::infinity();

bool sameGains(const Gains& a, const Gains& b) {
    auto near = [](double x, double y) { return std::abs(x - y) <= 1e-6 * std::max({1.0, std::abs(x), std::abs(y)}); };
    return near(a.kp, b.kp) && near(a.ti, b.ti) && near(a.td, b.td);
}

// La premiere position de `raw` a l'instant `t` ou apres.
template <class D>
std::size_t lowerAt(const D& raw, double t) {
    std::size_t lo = 0, hi = raw.size();
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        if (raw[mid].t < t) lo = mid + 1; else hi = mid;
    }
    return lo;
}

bool solve(std::vector<double> a, std::vector<double> b, int n, std::vector<double>& x) {
    // Gauss avec pivot partiel (n <= 3)
    for (int c = 0; c < n; ++c) {
        int p = c;
        for (int r = c + 1; r < n; ++r) if (std::abs(a[r * n + c]) > std::abs(a[p * n + c])) p = r;
        if (!(std::abs(a[p * n + c]) > 1e-300)) return false;
        if (p != c) { for (int k = 0; k < n; ++k) std::swap(a[c * n + k], a[p * n + k]); std::swap(b[c], b[p]); }
        for (int r = c + 1; r < n; ++r) {
            const double f = a[r * n + c] / a[c * n + c];
            for (int k = c; k < n; ++k) a[r * n + k] -= f * a[c * n + k];
            b[r] -= f * b[c];
        }
    }
    x.assign(n, 0.0);
    for (int r = n - 1; r >= 0; --r) {
        double s = b[r];
        for (int k = r + 1; k < n; ++k) s -= a[r * n + k] * x[k];
        x[r] = s / a[r * n + r];
    }
    for (double v : x) if (!std::isfinite(v)) return false;
    return true;
}

// ---- la memoire sur disque ---------------------------------------------------------
struct Out {
    std::string b;
    template <class T> void put(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        b.append(reinterpret_cast<const char*>(&v), sizeof v);
    }
    void floats(const std::vector<float>& v) {
        put<std::uint32_t>(static_cast<std::uint32_t>(v.size()));
        if (!v.empty()) b.append(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(float));
    }
    template <class D> void pods(const D& d) {
        put<std::uint32_t>(static_cast<std::uint32_t>(d.size()));
        for (const auto& v : d) put(v);
    }
};
struct In {
    std::string_view b;
    std::size_t      pos{0};
    bool             ok{true};
    template <class T> T get() {
        static_assert(std::is_trivially_copyable_v<T>);
        T v{};
        if (!ok || pos + sizeof v > b.size()) { ok = false; return v; }
        std::memcpy(&v, b.data() + pos, sizeof v);
        pos += sizeof v;
        return v;
    }
    bool floats(std::vector<float>& v, std::size_t max) {
        const auto n = get<std::uint32_t>();
        if (!ok || n > max || pos + n * sizeof(float) > b.size()) { ok = false; return false; }
        v.resize(n);
        if (n) std::memcpy(v.data(), b.data() + pos, n * sizeof(float));
        pos += n * sizeof(float);
        return true;
    }
    template <class D> bool pods(D& d, std::size_t max) {
        const auto n = get<std::uint32_t>();
        if (!ok || n > max) { ok = false; return false; }
        d.clear();
        for (std::uint32_t i = 0; i < n && ok; ++i) d.push_back(get<typename D::value_type>());
        return ok;
    }
};
constexpr char        kMagic[8] = {'X', 'P', 'G', 'P', 'I', 'D', '1', '\0'};
constexpr std::uint32_t kVersion = 1;

} // namespace

// =============================================================================
Learner::Learner(const Config& c) : cfg_(c) {}

void Learner::configure(const Config& c) {
    if (c == cfg_) return;
    cfg_ = c;
    invalidate();
}

void Learner::invalidate() const {
    cache_.clear();
    stateAt_ = -1;
}

double Learner::noise() const noexcept { return noiseW_ > 20 ? std::sqrt(std::max(0.0, noiseVar_)) : 0.0; }

double Learner::band() const {
    const double tol = cfg_.tolerance > 0 ? cfg_.tolerance : 1.0;
    return std::max({0.5 * tol, 4.0 * noise(), 3.0 * resolution_, 1e-9});
}
double Learner::stepMin() const {
    const double tol = cfg_.tolerance > 0 ? cfg_.tolerance : 1.0;
    return std::max({10.0 * noise(), 5.0 * resolution_, 0.1 * tol, 1e-9});
}
double Learner::outStepMin() const { return std::max(0.01 * cfg_.outSpan(), 1e-9); }
double Learner::settleBand(double step) const {
    return std::max({0.02 * std::abs(step), 3.0 * resolution_, 2.0 * noise(), 1e-9});
}
double Learner::prePeriod() const {
    if (!model_.valid) return 60.0;
    return std::clamp(model_.theta + 0.5 * std::min(model_.tau, std::max(model_.seen, 60.0)), 60.0, 900.0);
}
double Learner::ageWeight(double t) const {
    if (!(cfg_.forgetHours > 0)) return 1.0;
    return std::exp(-std::max(0.0, lastT_ - t) / (cfg_.forgetHours * 3600.0));
}

void Learner::addEvent(const Event& e) {
    events_.push_back(e);
    while (events_.size() > kEventsMax) events_.pop_front();
}

// ---- une mesure ---------------------------------------------------------------------
void Learner::push(const Sample& s, const std::optional<Gains>& g) {
    if (!std::isfinite(s.t) || !std::isfinite(s.sp) || !std::isfinite(s.pv) || !std::isfinite(s.out)) return;
    if (started_ && s.t <= lastT_) return;                       // deja vu, ou l'horloge recule
    const double P = std::max(cfg_.period, 1e-3);
    if (!started_ && !raw_.empty() && s.t < raw_.back().t) {     // l'horloge a recule depuis la memoire
        raw_.clear();
        acc_ = MinuteAcc{};
    }
    double dt = P;
    if (started_) {
        dt = s.t - lastT_;
        if (dt > std::max(3.0 * P, P + 2.0)) onGap(lastT_, s.t);
    } else if (lastT_ > 0 && s.t - lastT_ > std::max(3.0 * P, P + 2.0)) {
        onGap(lastT_, s.t);                                       // l'arret depuis la memoire
    }
    if (g) {
        if (gains_ && !sameGains(*gains_, *g)) {
            Event e;
            e.t = s.t;
            e.kind = EventKind::GainsChanged;
            e.before = *gains_;
            e.after = *g;
            addEvent(e);
            invalidate();
        }
        gains_ = g;
    }
    // en butee : a 0,5 % de l'etendue d'une borne, et jusqu'a 1 % pour en sortir
    const double span = cfg_.outSpan();
    const double near = satSince_ >= 0 ? 0.01 * span : 0.005 * span;
    const bool sat = s.out >= cfg_.outMax - near || s.out <= cfg_.outMin + near;
    const bool fresh = !started_ || dt > std::max(3.0 * P, P + 2.0);
    if (fresh) {                                                  // le debut, ou apres un trou : on repart d'ici
        spAnchor_ = prevSp_ = s.sp;
        spLeaveAt_ = s.t;
        outAnchor_ = prevOut_ = s.out;
        outLeaveAt_ = s.t;
        prevPv_ = s.pv;
        lastPvChange_ = s.t;
        frozenOutMin_ = frozenOutMax_ = s.out;
        lastAuto_ = s.autoMode;
        started_ = true;
    }
    const double prevPv = prevPv_;
    addRaw(s);
    addMinute(s, sat);
    trackOutput(s.t, s.out);
    watchSaturation(s, sat);
    watchFrozen(s, prevPv);
    watchNoise(s);
    watchBlock(s, sat, dt);
    watchSteps(s);
    watchDisturbance(s);
    watchExcitation(s);
    hourly(s.t);
    prevSp_ = s.sp;
    prevPv_ = s.pv;
    prevOut_ = s.out;
    lastAuto_ = s.autoMode;
    lastT_ = s.t;
    ++total_;
    if (s.t - lastPrune_ >= 60.0) {
        prune(s.t);
        lastPrune_ = s.t;
    }
}

void Learner::onGap(double from, double to) {
    Event e;
    e.t = from;
    e.t2 = to;
    e.kind = EventKind::Gap;
    if (cap_.active) {
        cap_.gap = true;
        e.verdict = Verdict::Discarded;
        e.reason = Reason::Gap;
        finishCapture(from);
    }
    if (stepWatch_.active) finishStep(from);
    addEvent(e);
    dist_ = Disturb{};
    pvHist_ = 0;
    bHist_ = 0;
    blockN_ = 0;
    if (satEvent_) {
        for (auto it = events_.rbegin(); it != events_.rend(); ++it)
            if (it->kind == EventKind::Saturation && it->t2 == 0) { it->t2 = from; break; }
    }
    // 1.12.24 : une butee qui dure encore au retour d'un trou d'une heure au plus (une relance du poste)
    // compte depuis son debut (watchSaturation) ; plus long, on ne sait pas : elle repart.
    satBeforeGap_ = to - from <= 3600.0 ? satSince_ : -1.0;
    satSince_ = -1;
    satEvent_ = false;
}

void Learner::addRaw(const Sample& s) {
    raw_.push_back({s.t, static_cast<float>(s.sp), static_cast<float>(s.pv), static_cast<float>(s.out), s.autoMode});
    while (!raw_.empty() && (raw_.size() > kRawMax || raw_.front().t < s.t - kRawSeconds)) raw_.pop_front();
}

void Learner::addMinute(const Sample& s, bool sat) {
    const auto minute = static_cast<std::int64_t>(std::floor(s.t / 60.0));
    if (minute != acc_.minute) {
        flushMinute();
        acc_ = MinuteAcc{};
        acc_.minute = minute;
        acc_.pvMin = kInf;
        acc_.pvMax = -kInf;
    }
    ++acc_.n;
    acc_.sp += s.sp;
    acc_.pv += s.pv;
    acc_.out += s.out;
    acc_.pvMin = std::min(acc_.pvMin, s.pv);
    acc_.pvMax = std::max(acc_.pvMax, s.pv);
    if (sat) ++acc_.nSat;
    if (s.autoMode) {
        const double e = s.sp - s.pv;
        const double tol = cfg_.tolerance > 0 ? cfg_.tolerance : 1.0;
        ++acc_.nAuto;
        acc_.sumAbsE += std::abs(e);
        acc_.sumE += e;
        acc_.sumE2 += e * e;
        if (std::abs(e) <= tol) ++acc_.nWithin;
        const int b = std::clamp(static_cast<int>(std::floor((e / tol + 4.0) * 2.0)), 0, kHistBins - 1);
        ++acc_.hist[static_cast<std::size_t>(b)];
    }
}

void Learner::flushMinute() {
    if (acc_.minute < 0 || acc_.n == 0) return;
    Minute m;
    m.minute = acc_.minute;
    m.n = acc_.n;
    m.nAuto = acc_.nAuto;
    m.nSat = acc_.nSat;
    m.nWithin = acc_.nWithin;
    m.reversals = acc_.reversals;
    m.sumAbsE = static_cast<float>(acc_.sumAbsE);
    m.sumE = static_cast<float>(acc_.sumE);
    m.sumE2 = static_cast<float>(acc_.sumE2);
    const double n = acc_.n;
    m.sp = static_cast<float>(acc_.sp / n);
    m.pv = static_cast<float>(acc_.pv / n);
    m.pvMin = static_cast<float>(acc_.pvMin);
    m.pvMax = static_cast<float>(acc_.pvMax);
    m.out = static_cast<float>(acc_.out / n);
    m.travel = static_cast<float>(acc_.travel);
    for (int i = 0; i < kHistBins; ++i)
        m.hist[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(std::min<std::uint32_t>(acc_.hist[static_cast<std::size_t>(i)], 65535));
    minutes_.push_back(m);
}

// La sortie en moyennes de 5 s : sa course et ses inversions (a 1 % de l'etendue pres).
void Learner::trackOutput(double t, double out) {
    if (blockN_ == 0) blockStart_ = t;
    blockSum_ += out;
    ++blockN_;
    if (t - blockStart_ < 5.0 - 0.5 * std::max(cfg_.period, 1e-3)) return;
    const double m = blockSum_ / static_cast<double>(blockN_);
    blockSum_ = 0;
    blockN_ = 0;
    if (!blockHave_) {
        blockHave_ = true;
        blockPrev_ = revExt_ = m;
        revDir_ = 0;
        return;
    }
    acc_.travel += std::abs(m - blockPrev_);
    blockPrev_ = m;
    const double h = 0.01 * cfg_.outSpan();
    if (revDir_ >= 0 && m < revExt_ - h) {
        if (revDir_ > 0) ++acc_.reversals;
        revDir_ = -1;
        revExt_ = m;
    } else if (revDir_ <= 0 && m > revExt_ + h) {
        if (revDir_ < 0) ++acc_.reversals;
        revDir_ = 1;
        revExt_ = m;
    } else if ((revDir_ > 0 && m > revExt_) || (revDir_ < 0 && m < revExt_)) {
        revExt_ = m;
    }
}

void Learner::watchSaturation(const Sample& s, bool sat) {
    if (sat && satSince_ < 0 && satBeforeGap_ >= 0) {   // 1.12.24 : elle durait avant le trou ; son evenement reprend
        satSince_ = satBeforeGap_;
        for (auto it = events_.rbegin(); it != events_.rend(); ++it)
            if (it->kind == EventKind::Saturation) {
                if (std::abs(it->t - satSince_) < 1e-6) { it->t2 = 0; satEvent_ = true; }
                break;
            }
    }
    satBeforeGap_ = -1;
    if (sat) {
        if (satSince_ < 0) satSince_ = s.t;
        if (!satEvent_ && s.t - satSince_ >= 10.0) {
            satEvent_ = true;
            Event e;
            e.t = satSince_;
            e.kind = EventKind::Saturation;
            e.a = s.out;
            addEvent(e);
        }
    } else {
        if (satEvent_) {
            for (auto it = events_.rbegin(); it != events_.rend(); ++it)
                if (it->kind == EventKind::Saturation && it->t2 == 0) { it->t2 = s.t; break; }
        }
        satSince_ = -1;
        satEvent_ = false;
    }
    if (cap_.active) {
        ++cap_.samples;
        if (sat) ++cap_.saturated;
    }
}

double Learner::saturatedFor() const noexcept { return satSince_ >= 0 ? lastT_ - satSince_ : 0.0; }

// La mesure figee : elle ne bouge plus du tout alors que la sortie bouge (une communication
// arretee, un capteur bloque). Le temps : 3 retards du procede, 5 min tant qu'il est inconnu.
void Learner::watchFrozen(const Sample& s, double prevPv) {
    if (s.pv != prevPv) {
        if (frozen_) {
            for (auto it = events_.rbegin(); it != events_.rend(); ++it)
                if (it->kind == EventKind::Frozen && it->t2 == 0) { it->t2 = s.t; break; }
            frozen_ = false;
        }
        lastPvChange_ = s.t;
        frozenOutMin_ = frozenOutMax_ = s.out;
        return;
    }
    frozenOutMin_ = std::min(frozenOutMin_, s.out);
    frozenOutMax_ = std::max(frozenOutMax_, s.out);
    const double P = std::max(cfg_.period, 1e-3);
    const double need = std::max({60.0, 100.0 * P, model_.valid ? 3.0 * model_.theta : 300.0});
    if (!frozen_ && s.t - lastPvChange_ >= need && frozenOutMax_ - frozenOutMin_ >= 0.02 * cfg_.outSpan()) {
        frozen_ = true;
        Event e;
        e.t = lastPvChange_;
        e.kind = EventKind::Frozen;
        e.a = s.pv;
        addEvent(e);
    }
    if (frozen_ && cap_.active) cap_.frozen = true;
}

// Le bruit de mesure : les differences secondes (la variance du bruit blanc fois 6, le mouvement
// lent du procede n'y compte presque pas), en moyenne sur l'heure ; la resolution : le plus petit
// pas de la mesure.
void Learner::watchNoise(const Sample& s) {
    const double P = std::max(cfg_.period, 1e-3);
    if (pvHist_ >= 2) {
        const double d2 = s.pv - 2.0 * pv1_ + pv2_;
        double v = d2 * d2 / 6.0;
        if (noiseW_ > 20 && v > 25.0 * noiseVar_ + 1e-12) v = 25.0 * noiseVar_ + 1e-12;
        const double alpha = std::max(1.0 / (noiseW_ + 1.0), std::min(1.0, P / 3600.0));
        noiseVar_ += alpha * (v - noiseVar_);
        noiseW_ += 1.0;
    }
    if (pvHist_ >= 1) {
        const double d1 = std::abs(s.pv - pv1_);
        if (d1 > 1e-9 * std::max(1.0, std::abs(s.pv))) resolution_ = resolution_ > 0 ? std::min(resolution_, d1) : d1;
    }
    pv2_ = pv1_;
    pv1_ = s.pv;
    pvHist_ = std::min(pvHist_ + 1, 2);
}

// La loi du bloc, relevee en AUTO hors saturation et sans changement de consigne.
void Learner::watchBlock(const Sample& s, bool sat, double dt) {
    const double P = std::max(cfg_.period, 1e-3);
    const double e = s.sp - s.pv;
    const bool ok = s.autoMode && lastAuto_ && !sat && s.sp == prevSp_ && dt > 0 && dt <= 3.0 * P;
    if (!ok) bHist_ = 0;
    if (ok && bHist_ >= 2) {
        const double x[3] = {e - be1_, 0.5 * (e + be1_) * dt, ((s.pv - bpv1_) - (bpv1_ - bpv2_)) / dt};
        const double y = s.out - bout1_;
        const double lam = std::exp(-dt / 3600.0);
        for (double& v : bxx_) v *= lam;
        for (double& v : bxy_) v *= lam;
        byy_ *= lam;
        bn_ *= lam;
        for (int a = 0; a < 3; ++a) {
            for (int b = 0; b < 3; ++b) bxx_[static_cast<std::size_t>(a * 3 + b)] += x[a] * x[b];
            bxy_[static_cast<std::size_t>(a)] += x[a] * y;
        }
        byy_ += y * y;
        bn_ += 1.0;
    }
    bpv2_ = bHist_ >= 1 ? bpv1_ : s.pv;
    bpv1_ = s.pv;
    be1_ = e;
    bout1_ = s.out;
    bHist_ = std::min(bHist_ + 1, 2);
}

std::optional<BlockCheck> Learner::blockCheck() const {
    if (bn_ < 200) return std::nullopt;
    const bool useD = gains_ && gains_->td > 0;
    const int n = useD ? 3 : 2;
    std::vector<double> a(static_cast<std::size_t>(n * n)), b(static_cast<std::size_t>(n)), x;
    for (int i = 0; i < n; ++i) {
        b[static_cast<std::size_t>(i)] = bxy_[static_cast<std::size_t>(i)];
        for (int j = 0; j < n; ++j) a[static_cast<std::size_t>(i * n + j)] = bxx_[static_cast<std::size_t>(i * 3 + j)];
    }
    if (!solve(a, b, n, x)) return std::nullopt;
    if (!(std::abs(x[0]) > 0)) return std::nullopt;
    BlockCheck c;
    c.seen.kp = std::abs(x[0]);
    c.seen.ti = x[1] != 0 && x[0] / x[1] > 0 ? x[0] / x[1] : 0.0;
    c.seen.td = useD ? std::max(0.0, -x[2] / x[0]) : 0.0;
    double sse = byy_;
    for (int i = 0; i < n; ++i) sse -= x[static_cast<std::size_t>(i)] * bxy_[static_cast<std::size_t>(i)];
    c.r2 = byy_ > 0 ? std::clamp(1.0 - sse / byy_, 0.0, 1.0) : 0.0;
    c.samples = static_cast<std::size_t>(bn_);
    return c;
}

// ---- les echelons de consigne : depassement, temps de reponse ---------------------------------
void Learner::watchSteps(const Sample& s) {
    const double W = model_.valid ? std::clamp(6.0 * (model_.theta + model_.tau), 600.0, 7200.0) : 1200.0;
    if (s.sp != prevSp_) {
        if (stepWatch_.active && s.t - stepWatch_.last <= 5.0) {   // une rampe, une saisie en deux fois
            stepWatch_.to = s.sp;
            stepWatch_.last = s.t;
        } else {
            if (stepWatch_.active) finishStep(s.t);
            stepWatch_ = StepWatch{true, s.t, prevSp_, s.sp, s.t};
        }
    }
    if (stepWatch_.active && s.t - stepWatch_.t >= W) finishStep(s.t);
}

void Learner::finishStep(double end) {
    const StepWatch w = stepWatch_;
    stepWatch_.active = false;
    const double step = w.to - w.from;
    if (std::abs(step) < stepMin()) return;
    const double minDur = model_.valid ? std::min(600.0, 3.0 * (model_.theta + model_.tau)) : 600.0;
    if (end - w.t < minDur) return;
    StepRecord r;
    r.t = w.t;
    r.from = w.from;
    r.to = w.to;
    r.band = settleBand(step);
    double peak = 0, lastOut = w.t;
    bool outsideAtEnd = false, any = false;
    const double dir = step > 0 ? 1.0 : -1.0;
    for (std::size_t i = lowerAt(raw_, w.t); i < raw_.size() && raw_[i].t <= end; ++i) {
        const Point& p = raw_[i];
        peak = std::max(peak, (p.pv - w.to) * dir);
        const bool out = std::abs(p.pv - w.to) > r.band;
        if (out) lastOut = p.t;
        outsideAtEnd = out;
        any = true;
    }
    if (!any) return;
    r.overshoot = std::max(0.0, peak / std::abs(step) * 100.0);
    r.settle = outsideAtEnd ? -1.0 : lastOut - w.t;
    stepRecords_.push_back(r);
    while (stepRecords_.size() > kStepsMax) stepRecords_.pop_front();
    ++stepsSeen_;
    if (verifyPending_ && w.t >= verifyAt_) {
        Event e;
        e.t = w.t;
        e.t2 = end;
        e.kind = EventKind::Verified;
        e.after = verifyGains_;
        e.predicted = verifyPredicted_;
        e.measured = r.overshoot;
        e.a = w.from;
        e.b = w.to;
        e.since = r.settle;
        addEvent(e);
        verifyPending_ = false;
    }
}

// ---- les perturbations --------------------------------------------------------------------------
void Learner::watchDisturbance(const Sample& s) {
    if (!s.autoMode || cap_.active) {
        dist_ = Disturb{};
        return;
    }
    const double P = std::max(cfg_.period, 1e-3);
    const double e = s.sp - s.pv;
    const double B = band();
    const bool spSteady = std::abs(s.sp - spAnchor_) < stepMin();
    if (!dist_.active) {
        if (std::abs(e) <= B) {
            dist_.over = 0;
            dist_.maxDev = 0;
            if (spSteady) {                                        // la sortie au repos : 2 min glissantes
                if (!outSteadyHave_) { outSteady_ = s.out; outSteadyHave_ = true; }
                else outSteady_ += std::min(1.0, P / 120.0) * (s.out - outSteady_);
            }
        } else if (spSteady && outSteadyHave_) {
            if (dist_.over == 0) dist_.start = s.t;
            ++dist_.over;
            if (std::abs(e) > std::abs(dist_.maxDev)) dist_.maxDev = e;
            if (static_cast<double>(dist_.over) * P >= 10.0) {
                dist_.active = true;
                dist_.outBefore = outSteady_;
            }
        }
        return;
    }
    if (std::abs(e) > std::abs(dist_.maxDev)) dist_.maxDev = e;
    if (std::abs(e) <= B) {
        if (dist_.quietSince == 0) {
            dist_.quietSince = s.t;
            dist_.quietSum = 0;
            dist_.quietN = 0;
        }
        dist_.quietSum += s.out;
        ++dist_.quietN;
        if (s.t - dist_.quietSince >= 120.0) {
            const double after = dist_.quietSum / static_cast<double>(dist_.quietN);
            const double d = after - dist_.outBefore;
            if (std::abs(d) >= 0.005 * cfg_.outSpan()) {
                Event ev;
                ev.t = dist_.start;
                ev.t2 = dist_.quietSince;
                ev.kind = EventKind::Disturbance;
                ev.verdict = Verdict::StatsOnly;
                ev.a = dist_.maxDev;
                ev.b = d;
                addEvent(ev);
            }
            outSteady_ = after;
            dist_ = Disturb{};
        }
    } else {
        dist_.quietSince = 0;
    }
    if (dist_.active && s.t - dist_.start > 4 * 3600.0) dist_ = Disturb{};   // une derive lente
}

// ---- les mouvements qui apprennent --------------------------------------------------------------
void Learner::watchExcitation(const Sample& s) {
    const double sm = stepMin(), om = outStepMin();
    if (!s.autoMode && lastAuto_ && !cap_.active) {               // le passage en MANU : la sortie d'avant
        outAnchor_ = prevOut_;                                     // (le premier pas en MANU peut deja l'avoir bougee)
        outLeaveAt_ = s.t;
    }
    if (std::abs(s.sp - spAnchor_) <= 0.25 * sm) spLeaveAt_ = s.t;
    if (std::abs(s.out - outAnchor_) <= 0.25 * om) outLeaveAt_ = s.t;
    const bool spExc = std::abs(s.sp - spAnchor_) >= sm;
    const bool outExc = !s.autoMode && std::abs(s.out - outAnchor_) >= om;

    if (!cap_.active) {
        if (spExc || outExc) {
            cap_ = Capture{};
            cap_.active = true;
            cap_.kind = spExc ? EventKind::SetpointStep : EventKind::ManualStep;
            cap_.learn = s.autoMode ? cfg_.learnAuto : cfg_.learnManual;
            cap_.onset = spExc ? spLeaveAt_ : outLeaveAt_;
            cap_.lastMove = cap_.lastCheck = s.t;
            cap_.spFrom = spAnchor_;
            cap_.spTo = s.sp;
            cap_.outFrom = outAnchor_;
            cap_.outTo = s.out;
            cap_.disturbed = dist_.active;
            cap_.disturbedSince = dist_.start;
            cap_.frozen = frozen_;
            spAnchor_ = s.sp;
            if (!s.autoMode) outAnchor_ = s.out;
        } else if (s.autoMode) {
            outAnchor_ = s.out;
            outLeaveAt_ = s.t;
        }
        return;
    }
    // un mouvement en cours : chaque nouveau mouvement repousse la fin
    if (s.sp != spAnchor_) {
        cap_.lastMove = s.t;
        spAnchor_ = s.sp;
        cap_.spTo = s.sp;
        if (cap_.kind == EventKind::ManualStep && std::abs(s.sp - cap_.spFrom) >= sm) cap_.kind = EventKind::SetpointStep;
    }
    if (!s.autoMode && std::abs(s.out - outAnchor_) >= 0.25 * om) {
        cap_.lastMove = s.t;
        outAnchor_ = s.out;
        cap_.outTo = s.out;
    }
    const bool mv = model_.valid;
    const double T = mv ? model_.theta + model_.tau : 0.0;
    const double Lmin = mv ? std::clamp(4.0 * T, 120.0, 7200.0) : 240.0;
    const double Lmax = mv ? std::clamp(8.0 * T, 600.0, 10800.0) : 3600.0;
    bool done = s.t - cap_.onset >= Lmax;
    // une mesure qui ne bouge plus du tout pendant que la sortie bouge : peut-etre figee ; on ne
    // conclut pas avant de le savoir
    const bool suspicious = s.t - lastPvChange_ >= 60.0 && frozenOutMax_ - frozenOutMin_ >= 0.02 * cfg_.outSpan();
    if (!done && !suspicious && s.t - cap_.lastCheck >= 5.0) {
        cap_.lastCheck = s.t;
        const double quiet = s.t - cap_.lastMove;
        if (quiet >= Lmin) {
            const double W = mv ? std::max(T, 60.0) : std::max(120.0, 0.3 * quiet);
            done = settledNow(s.t, W, cap_.spTo - cap_.spFrom);
        }
    }
    if (done) {
        finishCapture(s.t);
        spAnchor_ = s.sp;
        spLeaveAt_ = s.t;
        outAnchor_ = s.out;
        outLeaveAt_ = s.t;
    }
}

// Au repos : sur les `window` dernieres secondes, la mesure et la sortie en moyennes de 5 s
// tiennent chacune dans une bande.
bool Learner::settledNow(double t, double window, double step) const {
    const std::size_t i0 = lowerAt(raw_, t - window);
    if (raw_.size() - i0 < 8) return false;
    double pmin = kInf, pmax = -kInf, omin = kInf, omax = -kInf;
    double ps = 0, os = 0, cs = raw_[i0].t;
    std::size_t n = 0;
    for (std::size_t i = i0; i < raw_.size(); ++i) {
        ps += raw_[i].pv;
        os += raw_[i].out;
        ++n;
        if (raw_[i].t - cs >= 5.0 || i + 1 == raw_.size()) {
            const double pm = ps / static_cast<double>(n), om = os / static_cast<double>(n);
            pmin = std::min(pmin, pm); pmax = std::max(pmax, pm);
            omin = std::min(omin, om); omax = std::max(omax, om);
            ps = os = 0;
            n = 0;
            cs = raw_[i].t;
        }
    }
    const double thrPv = std::max({4.0 * noise(), 2.0 * resolution_, 0.03 * std::abs(step), 1e-9});
    const double thrOut = 0.02 * cfg_.outSpan();
    return pmax - pmin <= thrPv && omax - omin <= thrOut;
}

void Learner::finishCapture(double end) {
    const Capture c = cap_;
    cap_ = Capture{};
    evaluate(c, end);
}

// Au repos avant le mouvement : la mesure ne derive pas, la sortie non plus (en AUTO, une
// perturbation en cours se voit a la sortie qui compense).
bool Learner::steadyBefore(double onset, double pre, bool autoMode) const {
    const std::size_t i0 = lowerAt(raw_, onset - pre), i1 = lowerAt(raw_, onset);
    const double P = std::max(cfg_.period, 1e-3);
    if (i1 <= i0 || static_cast<double>(i1 - i0) < std::max(9.0, 0.4 * pre / P)) return false;
    const std::size_t n = i1 - i0, third = std::max<std::size_t>(n / 3, 1);
    double pa = 0, pb = 0, oa = 0, ob = 0, e = 0;
    for (std::size_t i = 0; i < third; ++i) {
        pa += raw_[i0 + i].pv;
        oa += raw_[i0 + i].out;
        pb += raw_[i1 - 1 - i].pv;
        ob += raw_[i1 - 1 - i].out;
    }
    for (std::size_t i = i0; i < i1; ++i) e += raw_[i].sp - raw_[i].pv;
    const double k = static_cast<double>(third);
    const double tol = cfg_.tolerance > 0 ? cfg_.tolerance : 1.0;
    const double thrPv = std::max({4.0 * noise(), 2.0 * resolution_, 0.1 * tol, 1e-9});
    if (std::abs(pb / k - pa / k) > thrPv) return false;
    if (std::abs(ob / k - oa / k) > 0.01 * cfg_.outSpan()) return false;
    if (autoMode && std::abs(e / static_cast<double>(n)) > band()) return false;
    return true;
}

bool Learner::extract(double from, double onset, double to, Segment& seg) const {
    if (raw_.empty()) return false;
    const double P = std::max(cfg_.period, 1e-3);
    from = std::max(from, static_cast<double>(raw_.front().t));
    if (!(to > from)) return false;
    const std::size_t n = static_cast<std::size_t>(std::floor((to - from) / P)) + 1;
    if (n < 16) return false;
    std::vector<float> u(n), y(n);
    std::size_t j = lowerAt(raw_, from);
    if (j > 0 && raw_[j].t > from) --j;
    std::size_t pre = 0;
    for (std::size_t k = 0; k < n; ++k) {
        const double t = from + static_cast<double>(k) * P;
        while (j + 1 < raw_.size() && raw_[j + 1].t <= t) ++j;
        const Point& a = raw_[j];
        if (j + 1 < raw_.size() && raw_[j + 1].t > a.t && t > a.t) {
            const Point& b = raw_[j + 1];
            const double r = std::clamp((t - a.t) / (b.t - a.t), 0.0, 1.0);
            y[k] = static_cast<float>(a.pv + r * (b.pv - a.pv));
        } else {
            y[k] = a.pv;
        }
        u[k] = a.out;                                              // la sortie tenue jusqu'au point suivant
        if (t < onset) ++pre;
    }
    seg = Segment{};
    seg.t0 = from;
    seg.dt = P;
    if (n > kSegPoints) {                                          // trop long : des moyennes par paquets
        const std::size_t m = (n + kSegPoints - 1) / kSegPoints, nn = n / m;
        seg.u.resize(nn);
        seg.y.resize(nn);
        for (std::size_t k = 0; k < nn; ++k) {
            double su = 0, sy = 0;
            for (std::size_t q = 0; q < m; ++q) { su += u[k * m + q]; sy += y[k * m + q]; }
            seg.u[k] = static_cast<float>(su / static_cast<double>(m));
            seg.y[k] = static_cast<float>(sy / static_cast<double>(m));
        }
        seg.dt = P * static_cast<double>(m);
        seg.pre = pre / m;
    } else {
        seg.u = std::move(u);
        seg.y = std::move(y);
        seg.pre = pre;
    }
    return seg.u.size() >= 16;
}

void Learner::evaluate(const Capture& c, double end) {
    Event ev;
    ev.t = c.onset;
    ev.t2 = end;
    ev.kind = c.kind;
    if (c.kind == EventKind::SetpointStep) { ev.a = c.spFrom; ev.b = c.spTo; }
    else { ev.a = c.outFrom; ev.b = c.outTo; }
    auto discard = [&](Reason r) {
        ev.verdict = Verdict::Discarded;
        ev.reason = r;
        addEvent(ev);
    };
    if (!c.learn) {
        ev.verdict = Verdict::StatsOnly;
        ev.reason = Reason::NotLearning;
        addEvent(ev);
        return;
    }
    if (c.gap) return discard(Reason::Gap);
    if (c.frozen) return discard(Reason::Frozen);
    if (c.samples > 0 && static_cast<double>(c.saturated) > 0.3 * static_cast<double>(c.samples)) return discard(Reason::Saturated);
    const double pre = prePeriod();
    if (c.disturbed) {
        ev.since = c.disturbedSince;
        return discard(Reason::DisturbanceInProgress);
    }
    if (!steadyBefore(c.onset, pre, c.kind == EventKind::SetpointStep)) return discard(Reason::DisturbanceInProgress);
    const double Lmin = model_.valid ? std::clamp(2.0 * (model_.theta + model_.tau), 120.0, 3600.0) : 180.0;
    if (end - c.onset < Lmin) return discard(Reason::TooShort);
    Segment seg;
    if (!extract(c.onset - pre, c.onset, end, seg)) return discard(Reason::TooShort);
    // la sortie doit avoir bouge par rapport a son repos d'avant
    double u0 = 0, moved = 0;
    for (std::size_t k = 0; k < seg.pre; ++k) u0 += seg.u[k];
    u0 = seg.pre ? u0 / static_cast<double>(seg.pre) : seg.u.front();
    for (std::size_t k = seg.pre; k < seg.u.size(); ++k) moved = std::max(moved, std::abs(seg.u[k] - u0));
    if (!(moved >= outStepMin())) return discard(Reason::TooSmall);
    const Fit f = identify({&seg});
    ev.fit = f.fit;
    if (!f.ok || f.fit < 70.0) return discard(Reason::PoorFit);
    ++cleanSeen_;
    const bool check = model_.learned >= 2 && cleanSeen_ % 3 == 0;
    if (check) {
        ev.verdict = Verdict::Check;
        ev.reason = Reason::KeptForCheck;
        stored_.push_back({std::move(seg), c.onset, Verdict::Check, f.fit});
        if (model_.valid) {
            ev.fit = checkFit(stored_.back().seg, model_);
            stored_.back().fit = ev.fit;
            model_.fitCheck = ev.fit;
            model_.checkAt = c.onset;
        }
        addEvent(ev);
        invalidate();
        return;
    }
    ev.verdict = Verdict::Learned;
    ev.reason = c.kind == EventKind::ManualStep ? Reason::OpenLoop : Reason::Clean;
    stored_.push_back({std::move(seg), c.onset, Verdict::Learned, f.fit});
    addEvent(ev);
    refit(&f);
}

// Le modele sur tous les echelons appris a la fois, chacun pese par son age.
void Learner::refit(const Fit* hint) {
    std::vector<const Segment*> segs;
    std::vector<double> w;
    double seen = 0;
    for (const Stored& st : stored_) {
        if (st.verdict != Verdict::Learned) continue;
        const double wi = ageWeight(st.onset);
        if (wi < 0.03) continue;                                   // oublie : il ne compte presque plus
        segs.push_back(&st.seg);
        w.push_back(wi);
        seen = std::max(seen, st.seg.duration() - static_cast<double>(st.seg.pre) * st.seg.dt);
    }
    if (segs.empty()) {
        model_ = Model{};
        fit_ = Fit{};
        invalidate();
        return;
    }
    // d'ou partir : le modele d'avant, et ce que le dernier echelon a appris seul (le meilleur des deux)
    std::vector<Fit> starts;
    if (fit_.ok) starts.push_back(fit_);
    if (hint && hint->ok) starts.push_back(*hint);
    const Fit f = identify(segs, w, starts);
    invalidate();
    if (!f.ok) {
        model_.valid = false;
        return;
    }
    fit_ = f;
    Model m = modelOf(f);
    m.learned = static_cast<int>(segs.size());
    m.seen = seen;
    m.valid = m.learned >= 2 && f.fit >= 70.0;
    for (auto it = stored_.rbegin(); it != stored_.rend(); ++it) {
        if (it->verdict != Verdict::Check) continue;
        m.fitCheck = checkFit(it->seg, m);
        m.checkAt = it->onset;
        it->fit = m.fitCheck;
        break;
    }
    model_ = m;
    if (!m.valid) return;
    if (!modelRef_.valid) {
        modelRef_ = m;
        return;
    }
    const bool kChg = std::abs(m.k - modelRef_.k) > std::max(0.05 * std::abs(modelRef_.k), 2.0 * m.kHalf);
    const bool tChg = std::abs(m.tau - modelRef_.tau) > std::max(0.10 * modelRef_.tau, 2.0 * m.tauHalf);
    const bool hChg = std::abs(m.theta - modelRef_.theta) > std::max({0.10 * modelRef_.theta, 2.0 * m.thetaHalf, 2.0 * cfg_.period});
    const bool oChg = m.order() != modelRef_.order();
    if (kChg || tChg || hChg || oChg) {
        Event e;
        e.t = lastT_;
        e.kind = EventKind::ModelChanged;
        e.before = {modelRef_.k, modelRef_.tau, modelRef_.theta};   // (K, tau, theta)
        e.after = {m.k, m.tau, m.theta};
        e.fit = m.fitLearn;
        addEvent(e);
        modelRef_ = m;
    }
}

void Learner::forget() {
    stored_.clear();
    model_ = Model{};
    modelRef_ = Model{};
    fit_ = Fit{};
    cleanSeen_ = 0;
    Event e;
    e.t = lastT_;
    e.kind = EventKind::Forgotten;
    addEvent(e);
    invalidate();
}

// ---- l'heure : l'indice de Harris, l'oubli ; la minute : le menage -------------------------------
double Learner::harrisOver(double from, double to) const {
    if (!model_.valid || raw_.empty()) return -1;
    const double P = std::max(cfg_.period, 1e-3);
    const double excl = std::clamp(6.0 * (model_.theta + std::min(model_.tau, std::max(model_.seen, 60.0))), 600.0, 3600.0);
    std::vector<double> e;
    double lastChange = -kInf;
    float prevSp = std::numeric_limits<float>::quiet_NaN();
    for (std::size_t i = lowerAt(raw_, from - excl); i < raw_.size() && raw_[i].t < to; ++i) {
        const Point& p = raw_[i];
        if (!(p.sp == prevSp)) { if (prevSp == prevSp) lastChange = p.t; prevSp = p.sp; }
        if (p.t >= from && p.autoMode && p.t - lastChange >= excl) e.push_back(p.sp - p.pv);
    }
    if (static_cast<double>(e.size()) * P < 1200.0) return -1;   // 20 min de regime etabli au moins
    const int dec = std::max(1, static_cast<int>(std::lround(model_.theta / (10.0 * P))));
    std::vector<double> x;
    x.reserve(e.size() / static_cast<std::size_t>(dec) + 1);
    for (std::size_t i = 0; i < e.size(); i += static_cast<std::size_t>(dec)) x.push_back(e[i]);
    const int b = std::max(1, static_cast<int>(std::lround(model_.theta / (static_cast<double>(dec) * P))));
    return harrisIndex(x, b, 20);
}

void Learner::hourly(double t) {
    const auto hour = static_cast<std::int64_t>(std::floor(t / 3600.0));
    if (lastHour_ < 0) { lastHour_ = hour; return; }
    if (hour == lastHour_) return;
    if (model_.valid) {
        const double h0 = static_cast<double>(lastHour_) * 3600.0;
        const double v = harrisOver(h0, h0 + 3600.0);
        if (v >= 0) harris_.push_back({lastHour_, static_cast<float>(v), 60.0f});
    }
    lastHour_ = hour;
    // l'oubli : les poids changent lentement, le modele est repris toutes les 6 heures
    if (cfg_.forgetHours > 0 && model_.learned > 0 && hour % 6 == 0) refit(nullptr);
}

void Learner::prune(double t) {
    const double from = t - std::max(cfg_.memoryDays, 0.05) * 86400.0;
    while (!minutes_.empty() && static_cast<double>(minutes_.front().minute) * 60.0 < from) minutes_.pop_front();
    while (!events_.empty() && events_.front().t < from) events_.pop_front();
    while (!stepRecords_.empty() && stepRecords_.front().t < from) stepRecords_.pop_front();
    while (!harris_.empty() && static_cast<double>(harris_.front().hour) * 3600.0 < from) harris_.pop_front();
    bool changed = false;
    while (!stored_.empty() && (stored_.size() > kStoredMax || stored_.front().onset < from)) {
        stored_.pop_front();
        changed = true;
    }
    if (changed) refit(nullptr);
}

// ---- ce qu'il en tire ----------------------------------------------------------------------------
WindowStats Learner::stats(double window) const {
    WindowStats w;
    if (!started_ && minutes_.empty()) return w;
    const double from = lastT_ - window;
    std::uint64_t n = 0, nAuto = 0, nSat = 0, nWithin = 0, rev = 0;
    double sAbs = 0, sE = 0, sE2 = 0, travel = 0;
    std::array<double, kHistBins> hist{};
    auto add = [&](std::uint32_t mn, std::uint32_t ma, std::uint32_t ms, std::uint32_t mw, std::uint32_t mr, double a, double e,
                   double e2, double tr, auto& h) {
        n += mn; nAuto += ma; nSat += ms; nWithin += mw; rev += mr;
        sAbs += a; sE += e; sE2 += e2; travel += tr;
        for (int i = 0; i < kHistBins; ++i) hist[static_cast<std::size_t>(i)] += h[static_cast<std::size_t>(i)];
    };
    for (auto it = minutes_.rbegin(); it != minutes_.rend(); ++it) {
        if (static_cast<double>(it->minute) * 60.0 < from - 1e-9) break;
        add(it->n, it->nAuto, it->nSat, it->nWithin, it->reversals, it->sumAbsE, it->sumE, it->sumE2, it->travel, it->hist);
    }
    if (acc_.minute >= 0 && acc_.n > 0)
        add(acc_.n, acc_.nAuto, acc_.nSat, acc_.nWithin, acc_.reversals, acc_.sumAbsE, acc_.sumE, acc_.sumE2, acc_.travel, acc_.hist);
    const double P = std::max(cfg_.period, 1e-3);
    w.samples = static_cast<std::size_t>(n);
    w.covered = static_cast<double>(n) * P;
    if (n == 0) return w;
    if (nAuto > 0) {
        const double na = static_cast<double>(nAuto);
        w.meanAbs = sAbs / na;
        const double m = sE / na;
        w.sd = std::sqrt(std::max(0.0, sE2 / na - m * m));
        w.within = 100.0 * static_cast<double>(nWithin) / na;
        for (int i = 0; i < kHistBins; ++i) w.hist[static_cast<std::size_t>(i)] = 100.0 * hist[static_cast<std::size_t>(i)] / na;
    }
    const double hours = std::max(w.covered / 3600.0, 1e-9);
    w.travel = travel / hours;
    w.reversals = static_cast<double>(rev) / hours;
    w.saturated = 100.0 * static_cast<double>(nSat) / static_cast<double>(n);
    w.autoPct = 100.0 * static_cast<double>(nAuto) / static_cast<double>(n);
    double so = 0, ss = 0;
    int ns = 0;
    for (const StepRecord& r : stepRecords_) {
        if (r.t < from) continue;
        ++w.steps;
        so += r.overshoot;
        if (r.settle >= 0) { ss += r.settle; ++ns; }
    }
    if (w.steps > 0) w.overshoot = so / w.steps;
    if (ns > 0) w.settle = ss / ns;
    if (window <= 3600.0 + 1e-9) {
        w.harris = harrisOver(from, lastT_ + 1e-9);
    } else {
        double hv = 0, hw = 0;
        for (const HourIndex& h : harris_) {
            if (static_cast<double>(h.hour) * 3600.0 < from - 1e-9) continue;
            hv += h.value * h.minutes;
            hw += h.minutes;
        }
        if (hw > 0) w.harris = hv / hw;
    }
    for (const Event& e : events_) {
        if (e.t < from) continue;
        if (e.kind == EventKind::Disturbance) ++w.disturbances;
        if (e.kind == EventKind::Gap) ++w.gaps;
    }
    return w;
}

std::vector<StepRecord> Learner::steps(double window) const {
    std::vector<StepRecord> out;
    for (const StepRecord& r : stepRecords_) if (r.t >= lastT_ - window) out.push_back(r);
    return out;
}

// L'oscillation : sur la derniere reponse a un echelon (les extremums successifs de la mesure
// lissee autour de la consigne), sinon en regime etabli (l'autocorrelation de l'ecart).
Oscillation Learner::oscillation(double window) const {
    Oscillation o;
    if (raw_.size() < 32) return o;
    const double P = std::max(cfg_.period, 1e-3);
    const double from = lastT_ - window;
    const StepRecord* last = nullptr;
    for (auto it = stepRecords_.rbegin(); it != stepRecords_.rend(); ++it) if (it->t >= from) { last = &*it; break; }
    double stepT = -1, sp1 = 0, step = 0;
    if (stepWatch_.active && stepWatch_.t >= from && std::abs(stepWatch_.to - stepWatch_.from) >= stepMin()) {
        stepT = stepWatch_.t; sp1 = stepWatch_.to; step = stepWatch_.to - stepWatch_.from;
    } else if (last) {
        stepT = last->t; sp1 = last->to; step = last->to - last->from;
    }
    const double tol = cfg_.tolerance > 0 ? cfg_.tolerance : 1.0;
    if (stepT >= 0) {
        // la reponse seule : jusqu'au changement de consigne suivant, a la premiere perturbation vue
        // ensuite, et pas plus de 6 fois le temps du procede (20 min tant qu'il est inconnu)
        double until = stepT + (model_.valid ? std::max(6.0 * (model_.theta + model_.tau + model_.tau2), 300.0) : 1200.0);
        for (const Event& e : events_)
            if (e.kind == EventKind::Disturbance && e.t > stepT + 5.0) until = std::min(until, e.t);
        if (dist_.active && dist_.start > stepT) until = std::min(until, dist_.start);
        std::size_t i0 = lowerAt(raw_, stepT), i1 = lowerAt(raw_, until);
        for (std::size_t i = i0 + 1; i < i1; ++i) if (raw_[i].sp != raw_[i - 1].sp && raw_[i].t > stepT + 5.0) { i1 = i; break; }
        if (i1 > i0 + 16) {
            const double hwSec = std::max(5.0, model_.valid ? model_.theta / 4.0 : 5.0);
            const std::size_t hw = std::max<std::size_t>(2, static_cast<std::size_t>(std::lround(hwSec / P)));
            std::vector<double> z(i1 - i0);
            std::vector<double> pre(i1 - i0 + 1, 0.0);
            for (std::size_t i = i0; i < i1; ++i) pre[i - i0 + 1] = pre[i - i0] + (raw_[i].pv - sp1);
            for (std::size_t k = 0; k < z.size(); ++k) {
                const std::size_t a = k >= hw ? k - hw : 0, b = std::min(z.size(), k + hw + 1);
                z[k] = (pre[b] - pre[a]) / static_cast<double>(b - a);
            }
            const double thr = std::max({3.0 * noise(), 2.0 * resolution_, 0.02 * std::abs(step)});
            struct Ext { double t, v; };
            std::vector<Ext> ext;
            for (std::size_t k = 1; k + 1 < z.size(); ++k) {
                const bool mx = z[k] > z[k - 1] && z[k] >= z[k + 1];
                const bool mn = z[k] < z[k - 1] && z[k] <= z[k + 1];
                if (!(mx || mn) || std::abs(z[k]) < thr) continue;
                const double t = raw_[i0 + k].t;
                if (!ext.empty() && (ext.back().v > 0) == (z[k] > 0)) {
                    if (std::abs(z[k]) > std::abs(ext.back().v)) ext.back() = {t, z[k]};
                    continue;
                }
                ext.push_back({t, z[k]});
                if (ext.size() > 6) break;                               // les premieres vagues suffisent
            }
            // le premier extremum doit etre un depassement (au-dela de la consigne)
            if (!ext.empty() && step != 0 && ext.front().v * step < 0) ext.erase(ext.begin());
            if (ext.size() > 5) ext.resize(5);
            if (ext.size() >= 2) {
                o.found = true;
                o.period = 2.0 * (ext.back().t - ext.front().t) / static_cast<double>(ext.size() - 1);
                o.amplitude = std::abs(ext[0].v);
                o.decay = ext.size() >= 3 ? std::abs(ext[2].v) / std::abs(ext[0].v)
                                          : std::pow(std::abs(ext[1].v) / std::abs(ext[0].v), 2.0);
                o.at = stepT;
                return o;
            }
        }
        if (stepT >= from) return o;
    }
    // en regime etabli : l'autocorrelation de l'ecart
    std::vector<double> e;
    for (std::size_t i = lowerAt(raw_, from); i < raw_.size(); ++i) if (raw_[i].autoMode) e.push_back(raw_[i].sp - raw_[i].pv);
    if (e.size() < 64) return o;
    const std::size_t dec = std::max<std::size_t>(1, e.size() / 1500);
    std::vector<double> x;
    for (std::size_t i = 0; i < e.size(); i += dec) x.push_back(e[i]);
    double m = 0;
    for (double v : x) m += v;
    m /= static_cast<double>(x.size());
    double r0 = 0;
    for (double& v : x) { v -= m; r0 += v * v; }
    if (!(r0 > 0)) return o;
    const double sd = std::sqrt(r0 / static_cast<double>(x.size()));
    if (sd < std::max(0.25 * tol, 3.0 * noise())) return o;
    const std::size_t maxLag = x.size() / 3;
    bool crossed = false;
    double bestR = 0;
    std::size_t bestLag = 0;
    for (std::size_t L = 1; L < maxLag; ++L) {
        double s = 0;
        for (std::size_t i = 0; i + L < x.size(); ++i) s += x[i] * x[i + L];
        const double r = s / r0;
        if (!crossed) { if (r < 0) crossed = true; continue; }
        if (r > bestR) { bestR = r; bestLag = L; }
        else if (bestLag && r < bestR - 0.2) break;
    }
    if (bestLag && bestR >= 0.3) {
        o.found = true;
        o.sustained = true;
        o.period = static_cast<double>(bestLag * dec) * P;
        o.decay = bestR;
        o.amplitude = sd * std::sqrt(2.0);
    }
    return o;
}

std::vector<Learner::MinuteView> Learner::minutes(double window) const {
    std::vector<MinuteView> out;
    const double from = lastT_ - window;
    for (const Minute& m : minutes_) {
        if (static_cast<double>(m.minute) * 60.0 < from) continue;
        out.push_back({static_cast<double>(m.minute) * 60.0 + 30.0, m.sp, m.pv, m.pvMin, m.pvMax, m.out});
    }
    if (acc_.minute >= 0 && acc_.n > 0) {
        const double n = acc_.n;
        out.push_back({static_cast<double>(acc_.minute) * 60.0 + 30.0, static_cast<float>(acc_.sp / n), static_cast<float>(acc_.pv / n),
                       static_cast<float>(acc_.pvMin), static_cast<float>(acc_.pvMax), static_cast<float>(acc_.out / n)});
    }
    return out;
}

Learner::SegmentView Learner::view(const Stored& st) const {
    SegmentView v;
    v.t0 = st.seg.t0;
    v.dt = st.seg.dt;
    v.onset = st.onset;
    v.y = st.seg.y;
    v.u = st.seg.u;
    v.fit = st.fit;
    if (model_.valid) {
        const std::vector<double> yh = predict(st.seg, model_);
        v.yhat.assign(yh.begin(), yh.end());
    }
    return v;
}

std::optional<Learner::SegmentView> Learner::lastLearned() const {
    for (auto it = stored_.rbegin(); it != stored_.rend(); ++it) if (it->verdict == Verdict::Learned) return view(*it);
    return std::nullopt;
}
std::optional<Learner::SegmentView> Learner::lastCheck() const {
    for (auto it = stored_.rbegin(); it != stored_.rend(); ++it) if (it->verdict == Verdict::Check) return view(*it);
    return std::nullopt;
}

std::vector<std::pair<double, double>> Learner::dailyGains() const {
    std::map<int, std::pair<double, int>> days;
    if (!model_.valid) return {};
    for (const Stored& st : stored_) {
        const double k = gainWith(st.seg, model_);
        if (k == 0) continue;
        const int d = static_cast<int>(std::floor(std::max(0.0, lastT_ - st.onset) / 86400.0));
        days[d].first += k;
        days[d].second += 1;
    }
    std::vector<std::pair<double, double>> out;
    for (auto it = days.rbegin(); it != days.rend(); ++it) out.push_back({-static_cast<double>(it->first), it->second.first / it->second.second});
    return out;
}

double Learner::typicalStep() const {
    std::vector<double> v;
    for (const StepRecord& r : stepRecords_) v.push_back(std::abs(r.to - r.from));
    if (!v.empty()) {
        std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
        return v[v.size() / 2];
    }
    if (cfg_.pvSpan > 0) return 0.1 * cfg_.pvSpan;
    return std::max(5.0 * (cfg_.tolerance > 0 ? cfg_.tolerance : 1.0), 1.0);
}

double Learner::outRest() const {
    if (raw_.empty()) return 0.5 * (cfg_.outMin + cfg_.outMax);
    double s = 0;
    std::size_t n = 0;
    for (std::size_t i = lowerAt(raw_, lastT_ - 600.0); i < raw_.size(); ++i) { s += raw_[i].out; ++n; }
    return n ? s / static_cast<double>(n) : raw_.back().out;
}

// Le cout d'un reglage pese ce que la boucle voit par jour : ses echelons de consigne (combien,
// de combien) et ses perturbations (combien, ce que la sortie a du compenser). Sans rien vu : un
// echelon et une perturbation par jour, de tailles habituelles.
LoopContext Learner::context() const {
    LoopContext c;
    c.period = std::max(cfg_.period, 1e-3);
    c.noise = noise();
    c.derivFilter = 10.0;
    double days = 0;
    for (const Minute& m : minutes_) days += static_cast<double>(m.n);
    days = std::max(days * c.period / 86400.0, 1.0);
    std::vector<double> loads;
    for (const Event& e : events_) if (e.kind == EventKind::Disturbance) loads.push_back(std::abs(e.b));
    double load = 0.02 * cfg_.outSpan();
    if (!loads.empty()) {
        std::nth_element(loads.begin(), loads.begin() + static_cast<std::ptrdiff_t>(loads.size() / 2), loads.end());
        load = loads[loads.size() / 2];
    }
    c.spWeight = (static_cast<double>(stepRecords_.size()) + 1.0) / days * typicalStep();
    c.loadWeight = (static_cast<double>(loads.size()) + 1.0) / days * load;
    return c;
}

const Proposal& Learner::proposal(Goal goal, Derivative d, Form f) const {
    const std::uint32_t key = (static_cast<std::uint32_t>(d) << 4) | (static_cast<std::uint32_t>(f) << 8);
    for (const Cached& c : cache_) if (c.key == key) return c.p[static_cast<std::size_t>(goal)];
    TuneRequest rq;
    rq.model = model_;
    rq.ctx = context();
    rq.derivative = d;
    rq.form = f;
    rq.outSpan = cfg_.outSpan();
    cache_.push_back({key, tuneAll(rq)});
    return cache_.back().p[static_cast<std::size_t>(goal)];
}

State Learner::state() const {
    if (stateAt_ >= 0 && std::abs(lastT_ - stateAt_) < 5.0) return state_;
    stateAt_ = lastT_;
    State s = State::Healthy;
    if (total_ == 0) s = State::NoData;
    else if (frozen_) s = State::Frozen;
    else if (saturatedFor() >= 30.0) s = State::Saturated;
    else if (!lastAuto_) s = State::Manual;
    else if (!model_.valid) s = State::Learning;
    else {
        const Oscillation o = oscillation(1800.0);
        double over = 0;
        for (auto it = stepRecords_.rbegin(); it != stepRecords_.rend(); ++it) if (it->t >= lastT_ - 8 * 3600.0) { over = it->overshoot; break; }
        if (o.found && (o.sustained || (o.decay >= 0.15 && over >= 25.0))) s = State::Oscillating;
        else if (gains_) {
            const Proposal& p = proposal(cfg_.goal, cfg_.derivative, cfg_.form);
            if (p.ok) {
                const double cur = costOf(model_, *gains_, context());
                const Analysis a = analyze(model_, *gains_, context());
                if (!a.stable || a.ms > 2.2) s = State::Oscillating;
                else if (cur > 1.5 * p.cost) s = State::Sluggish;
            }
        }
    }
    state_ = s;
    return s;
}

void Learner::noteApplied(double t, const Gains& before, const Gains& after, double predictedOvershoot) {
    Event e;
    e.t = t;
    e.kind = EventKind::Applied;
    e.before = before;
    e.after = after;
    e.predicted = predictedOvershoot;
    addEvent(e);
    verifyPending_ = true;
    verifyAt_ = t;
    verifyPredicted_ = predictedOvershoot;
    verifyGains_ = after;
    gains_ = after;
    invalidate();
}

// ---- la memoire sur disque ------------------------------------------------------------------------
std::string Learner::save() const {
    Out o;
    o.b.append(kMagic, sizeof kMagic);
    o.put(kVersion);
    o.put(model_);
    o.put(modelRef_);
    o.put(fit_);
    o.put<std::uint8_t>(gains_ ? 1 : 0);
    o.put(gains_ ? *gains_ : Gains{});
    o.put<std::int32_t>(cleanSeen_);
    o.put<std::int32_t>(stepsSeen_);
    o.put(resolution_);
    o.put(noiseVar_);
    o.put(noiseW_);
    o.put(lastT_);
    o.put(outSteady_);
    o.put<std::uint8_t>(outSteadyHave_ ? 1 : 0);
    o.put<std::uint8_t>(verifyPending_ ? 1 : 0);
    o.put(verifyAt_);
    o.put(verifyPredicted_);
    o.put(verifyGains_);
    o.put(bxx_);
    o.put(bxy_);
    o.put(byy_);
    o.put(bn_);
    o.put<std::uint32_t>(static_cast<std::uint32_t>(stored_.size()));
    for (const Stored& st : stored_) {
        o.put(st.seg.t0);
        o.put(st.seg.dt);
        o.put<std::uint64_t>(st.seg.pre);
        o.floats(st.seg.u);
        o.floats(st.seg.y);
        o.put(st.onset);
        o.put(st.verdict);
        o.put(st.fit);
    }
    o.pods(events_);
    o.pods(stepRecords_);
    o.pods(harris_);
    o.pods(minutes_);
    o.pods(raw_);
    return std::move(o.b);
}

bool Learner::load(std::string_view data) {
    if (data.size() < sizeof kMagic || std::memcmp(data.data(), kMagic, sizeof kMagic) != 0) return false;
    In in{data, sizeof kMagic};
    if (in.get<std::uint32_t>() != kVersion) return false;
    Learner l(cfg_);
    l.model_ = in.get<Model>();
    l.modelRef_ = in.get<Model>();
    l.fit_ = in.get<Fit>();
    const bool hasGains = in.get<std::uint8_t>() != 0;
    const Gains g = in.get<Gains>();
    if (hasGains) l.gains_ = g;
    l.cleanSeen_ = in.get<std::int32_t>();
    l.stepsSeen_ = in.get<std::int32_t>();
    l.resolution_ = in.get<double>();
    l.noiseVar_ = in.get<double>();
    l.noiseW_ = in.get<double>();
    l.lastT_ = in.get<double>();
    l.outSteady_ = in.get<double>();
    l.outSteadyHave_ = in.get<std::uint8_t>() != 0;
    l.verifyPending_ = in.get<std::uint8_t>() != 0;
    l.verifyAt_ = in.get<double>();
    l.verifyPredicted_ = in.get<double>();
    l.verifyGains_ = in.get<Gains>();
    l.bxx_ = in.get<std::array<double, 9>>();
    l.bxy_ = in.get<std::array<double, 3>>();
    l.byy_ = in.get<double>();
    l.bn_ = in.get<double>();
    const auto ns = in.get<std::uint32_t>();
    if (!in.ok || ns > kStoredMax * 4) return false;
    for (std::uint32_t i = 0; i < ns && in.ok; ++i) {
        Stored st;
        st.seg.t0 = in.get<double>();
        st.seg.dt = in.get<double>();
        st.seg.pre = static_cast<std::size_t>(in.get<std::uint64_t>());
        in.floats(st.seg.u, kSegPoints * 4);
        in.floats(st.seg.y, kSegPoints * 4);
        st.onset = in.get<double>();
        st.verdict = in.get<Verdict>();
        st.fit = in.get<double>();
        if (st.seg.u.size() != st.seg.y.size() || !(st.seg.dt > 0) || st.seg.pre > st.seg.u.size()) return false;
        l.stored_.push_back(std::move(st));
    }
    in.pods(l.events_, kEventsMax * 4);
    in.pods(l.stepRecords_, kStepsMax * 4);
    in.pods(l.harris_, 100000);
    in.pods(l.minutes_, 2000000);
    in.pods(l.raw_, kRawMax * 2);
    if (!in.ok || !std::isfinite(l.lastT_)) return false;
    for (std::size_t i = 1; i < l.raw_.size(); ++i) if (!(l.raw_[i].t >= l.raw_[i - 1].t)) return false;
    l.started_ = false;                 // la prochaine mesure repart : ancres, differences, trous
    l.total_ = 0;
    // 1.12.24 : la butee en cours a l'enregistrement, relue sur les derniers points (la relance la reprend).
    {
        const double span = l.cfg_.outSpan(), near = 0.01 * span;
        for (auto it = l.raw_.rbegin(); it != l.raw_.rend(); ++it) {
            if (!(it->out >= l.cfg_.outMax - near || it->out <= l.cfg_.outMin + near)) break;
            l.satSince_ = it->t;
        }
        if (l.satSince_ >= 0)   // son evenement encore ouvert dit quand elle a commence
            for (auto it = l.events_.rbegin(); it != l.events_.rend(); ++it)
                if (it->kind == EventKind::Saturation) {
                    if (it->t2 == 0) { l.satEvent_ = true; l.satSince_ = std::min(l.satSince_, it->t); }
                    break;
                }
    }
    *this = std::move(l);
    invalidate();
    return true;
}

} // namespace hmi::pid
