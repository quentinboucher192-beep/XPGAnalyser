// =============================================================================
//  hmi/HmiPid.cpp - 1.12.24 : le Reglage PID, les calculs (sans etat)
// -----------------------------------------------------------------------------
//  Les formes du bloc, l'analyse frequentielle d'une boucle (Ms, Mt, marges,
//  stabilite, bruit envoye a la sortie), la reponse prevue, le reglage optimise
//  et l'identification du procede. L'apprentissage en continu (ce qu'il voit,
//  les evenements, les statistiques) : HmiPidLearn.cpp.
//
//  LE PROCEDE, EXACT ENTRE DEUX MESURES. La sortie est tenue d'une mesure a la
//  suivante ; un retard theta = (d + f) pas la decale de d pas et d'une
//  fraction f. Sur un pas, l'entree retardee vaut u(k-d-1) pendant f.dt puis
//  u(k-d) : un premier ordre de constante tau s'y integre exactement,
//      x(k+1) = a x(k) + (1 - a^(1-f)) u(k-d) + (a^(1-f) - a) u(k-d-1),  a = e^(-dt/tau).
//  Deux constantes de temps : 1/((tau s+1)(tau2 s+1)) = (tau/(tau s+1) - tau2/(tau2 s+1)) / (tau - tau2),
//  deux premiers ordres exacts, combines. Le modele, la reponse prevue et la
//  boucle simulee pour le reglage en sont tous faits.
// =============================================================================
#include "HmiPid.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdio>
#include <limits>

namespace hmi::pid {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kInf = std::numeric_limits<double>::infinity();
using cd = std::complex<double>;

bool finite(double v) noexcept { return std::isfinite(v); }

// ---- le regulateur et le procede en frequence ------------------------------------
cd controllerAt(const Gains& g, double w, double n) noexcept {
    const cd s(0.0, w);
    cd c = 1.0;
    if (g.ti > 0) c += 1.0 / (g.ti * s);
    if (g.td > 0) c += g.td * s / (1.0 + g.td * s / n);
    return g.kp * c;
}
cd plantAt(const Model& m, double w, double extra) noexcept {
    const cd s(0.0, w);
    cd p = std::abs(m.k) * std::polar(1.0, -w * (m.theta + extra)) / (1.0 + m.tau * s);
    if (m.tau2 > 0) p /= (1.0 + m.tau2 * s);
    return p;
}
double plantPhase(const Model& m, double w, double extra) noexcept {
    return -std::atan(w * m.tau) - (m.tau2 > 0 ? std::atan(w * m.tau2) : 0.0) - w * (m.theta + extra);
}

// Le premier ordre equivalent (la regle de la moitie : la petite constante va pour moitie au
// retard, pour moitie a la grande).
double thetaEq(const Model& m) noexcept { return m.theta + 0.5 * std::max(m.tau2, 0.0); }
double tauEq(const Model& m) noexcept { return m.tau + 0.5 * std::max(m.tau2, 0.0); }

// Le temps qui donne l'echelle d'une boucle : retard + constante de temps, la constante bornee
// pour un integrateur (un tau immense ne dit pas le temps de la boucle fermee, le retard si).
double charTime(const Model& m, double h) noexcept {
    const double th = thetaEq(m);
    return std::max(th + std::min(tauEq(m), 12.0 * std::max(th, 4.0 * h)), 4.0 * h);
}

// La reponse indicielle unitaire du procede a l'instant t (sans retard).
double stepAt(const Model& m, double t) noexcept {
    if (t <= 0) return 0.0;
    if (m.tau2 > 0) return 1.0 - (m.tau * std::exp(-t / m.tau) - m.tau2 * std::exp(-t / m.tau2)) / (m.tau - m.tau2);
    return 1.0 - std::exp(-t / m.tau);
}

// ---- l'analyse frequentielle -------------------------------------------------------
//  `stopAt` > 0 : des que |S| ou |T| le depasse, on s'arrete (le candidat est hors limite).
Analysis analyzeImpl(const Model& m, const Gains& g, const LoopContext& ctx, int points, bool withNoise, double stopAt = 0) {
    Analysis a;
    if (!(m.tau > 0) || m.k == 0 || !(g.kp > 0) || !finite(g.kp)) return a;
    const double h = std::max(ctx.period, 1e-3);
    const double extra = h / 2;                         // le bloqueur et le calcul : une demi-periode
    const double N = ctx.derivFilter > 1 ? ctx.derivFilter : 10.0;
    const double T = charTime(m, h);
    const double wlo = 1e-3 / std::max(T, h), whi = kPi / h;
    const double lr = std::log(whi / wlo);
    double prevW = 0, prevMag = 0, prevPh = 0, prevF = 0;
    bool   unstable = false;
    double gm = kInf, pm = kInf, integral = 0, best = 0;
    int    imax = 0;
    for (int i = 0; i < points; ++i) {
        const double w = wlo * std::exp(lr * i / (points - 1));
        const cd c = controllerAt(g, w, N);
        const cd l = c * plantAt(m, w, extra);
        const double s = 1.0 / std::abs(1.0 + l);
        const double mag = std::abs(l);
        const double t = mag * s;
        if (s > best) { best = s; imax = i; }
        a.mt = std::max(a.mt, t);
        if (stopAt > 0 && (s > stopAt || t > stopAt)) {
            a.ms = s;
            a.stable = false;
            return a;
        }
        const double ph = std::arg(c) + plantPhase(m, w, extra);
        const double f = std::norm(c) * s * s;          // |C S|^2
        if (i > 0) {
            if (withNoise) integral += 0.5 * (f + prevF) * (w - prevW);
            // le passage de |L| a 1 : la marge de phase (la plus petite)
            if ((prevMag - 1.0) * (mag - 1.0) <= 0 && prevMag != mag) {
                const double r = std::log(prevMag) / (std::log(prevMag) - std::log(mag));
                const double phc = prevPh + r * (ph - prevPh);
                pm = std::min(pm, 180.0 + phc * 180.0 / kPi);
                if (a.wc == 0) a.wc = prevW * std::pow(w / prevW, r);
            }
            // les passages de la phase par -pi, -3pi... : la marge de gain, la stabilite (le lieu
            // de Nyquist ne coupe l'axe reel negatif qu'a droite de -1)
            const double q0 = std::floor((-prevPh - kPi) / (2 * kPi));
            const double q1 = std::floor((-ph - kPi) / (2 * kPi));
            if (q0 != q1) {
                const double target = -kPi - 2 * kPi * std::max(q0, q1);
                const double r = (target - prevPh) / (ph - prevPh);
                const double magc = std::exp(std::log(prevMag) + r * (std::log(mag) - std::log(prevMag)));
                if (magc >= 1.0) unstable = true;
                gm = std::min(gm, 1.0 / magc);
            }
        } else if (withNoise) {
            integral += f * w;                          // [0 ; wlo] : la valeur au bord
        }
        prevW = w; prevMag = mag; prevPh = ph; prevF = f;
    }
    // le maximum de |S| precise entre ses voisins (section doree en log w)
    {
        double lo = std::log(wlo) + lr * std::max(imax - 1, 0) / (points - 1);
        double hi = std::log(wlo) + lr * std::min(imax + 1, points - 1) / (points - 1);
        auto sAt = [&](double lw) {
            const double w = std::exp(lw);
            return 1.0 / std::abs(1.0 + controllerAt(g, w, N) * plantAt(m, w, extra));
        };
        const double gr = 0.5 * (std::sqrt(5.0) - 1.0);
        double x1 = hi - gr * (hi - lo), x2 = lo + gr * (hi - lo);
        double f1 = sAt(x1), f2 = sAt(x2);
        for (int it = 0; it < 20; ++it) {
            if (f1 > f2) { hi = x2; x2 = x1; f2 = f1; x1 = hi - gr * (hi - lo); f1 = sAt(x1); }
            else         { lo = x1; x1 = x2; f1 = f2; x2 = lo + gr * (hi - lo); f2 = sAt(x2); }
        }
        best = std::max({best, f1, f2});
    }
    a.ms = best;
    a.stable = !unstable && finite(a.ms);
    a.gm = finite(gm) ? gm : 0.0;
    a.pm = finite(pm) ? pm : 0.0;
    if (a.pm < 0) a.stable = false;
    if (withNoise) a.noiseOut = ctx.noise * std::sqrt(std::max(0.0, integral * h / kPi));
    return a;
}

// ---- le procede discret (exact pour une entree tenue) ----------------------------------------
struct Lag {
    double a{0}, b1{0}, b2{0};
};
Lag lagOf(double tau, double f, double dt) {
    Lag l;
    l.a = std::exp(-dt / tau);
    const double a1f = std::exp(-(1.0 - f) * dt / tau);
    l.b1 = 1.0 - a1f;
    l.b2 = a1f - l.a;
    return l;
}
struct Disc {
    std::size_t d{0};
    Lag  A, B;
    bool two{false};
    double cA{1}, cB{0};
    void init(const Model& m, double dt) {
        const double q = std::max(m.theta, 0.0) / dt;
        d = static_cast<std::size_t>(std::floor(q));
        const double f = q - static_cast<double>(d);
        A = lagOf(m.tau, f, dt);
        two = m.tau2 > 0;
        if (two) {
            B = lagOf(m.tau2, f, dt);
            cA = m.tau / (m.tau - m.tau2);
            cB = -m.tau2 / (m.tau - m.tau2);
        } else {
            cA = 1.0;
            cB = 0.0;
        }
    }
};

// ---- la reponse lineaire (le cout d'un reglage) -------------------------------------------
//  Un echelon unitaire de consigne, puis (a part) une perturbation unitaire a l'entree, sans
//  bornes ; le regulateur du bloc (proportionnelle sur l'ecart, derivee filtree sur la mesure).
struct Linear {
    double iaeSp{0}, iaeLd{0}, over{0};
    bool   diverged{false};
};

Linear linearResponses(const Model& m, const Gains& g, double dt, double horizon, double N) {
    Linear r;
    const double K = std::abs(m.k);
    Disc p;
    p.init(m, dt);
    const std::size_t n = static_cast<std::size_t>(horizon / dt) + 1;
    std::vector<double> hist(p.d + 2, 0.0);   // l'entree du procede, en anneau
    const std::size_t L = hist.size();
    const double Tf = g.td > 0 ? g.td / N : 0.0;
    const double cI = g.ti > 0 ? g.kp * dt / g.ti : 0.0;
    const double cD1 = g.td > 0 ? Tf / (Tf + dt) : 0.0;
    const double cD2 = g.td > 0 ? g.kp * g.td / (Tf + dt) : 0.0;
    for (int pass = 0; pass < 2; ++pass) {
        std::fill(hist.begin(), hist.end(), 0.0);
        double xa = 0, xb = 0, I = 0, D = 0, pvPrev = 0, iae = 0, peak = 0;
        const double sp = pass == 0 ? 1.0 : 0.0;
        const double load = pass == 0 ? 0.0 : 1.0;
        std::size_t pos = 0;
        for (std::size_t k = 0; k < n; ++k) {
            const double pv = K * (p.cA * xa + p.cB * xb);
            const double e = sp - pv;
            iae += std::abs(e);
            if (pass == 0 && pv > peak) peak = pv;
            if (std::abs(e) > 1e4) { r.diverged = true; return r; }
            if (g.td > 0) D = cD1 * D - cD2 * (pv - pvPrev);
            const double u = g.kp * e + I + D;
            I += cI * e;
            pvPrev = pv;
            hist[pos] = u + load;
            // l'anneau : u(k-d) et u(k-d-1), nuls tant qu'ils n'ont pas ete ecrits
            const std::size_t ia = pos >= p.d ? pos - p.d : pos + L - p.d;
            const std::size_t ib = ia == 0 ? L - 1 : ia - 1;
            xa = p.A.a * xa + p.A.b1 * hist[ia] + p.A.b2 * hist[ib];
            if (p.two) xb = p.B.a * xb + p.B.b1 * hist[ia] + p.B.b2 * hist[ib];
            if (++pos == L) pos = 0;
        }
        iae *= dt;
        if (pass == 0) { r.iaeSp = iae; r.over = std::max(0.0, (peak - 1.0) * 100.0); }
        else           r.iaeLd = iae;
    }
    return r;
}

struct Costing {
    double Tc{1}, wSp{1}, wLd{1}, dt{0.5}, N{10};
};
Costing costing(const Model& m, const LoopContext& ctx) {
    Costing c;
    const double h = std::max(ctx.period, 1e-3);
    c.Tc = charTime(m, h);
    if (ctx.spWeight > 0 && ctx.loadWeight > 0) {
        c.wSp = ctx.spWeight;
        c.wLd = ctx.loadWeight;
    } else {
        // par defaut : un echelon de 1 et la perturbation qui deplacerait autant la mesure
        double g = std::abs(m.k) * stepAt(m, c.Tc);
        if (!(g > 0)) g = std::abs(m.k);
        c.wSp = 1.0 / c.Tc;
        c.wLd = 1.0 / (g * c.Tc);
    }
    c.dt = std::max(h, c.Tc / 150.0);
    c.N = ctx.derivFilter > 1 ? ctx.derivFilter : 10.0;
    return c;
}
double costWith(const Model& m, const Gains& g, const Costing& c, double* over) {
    const double horizon = std::min(10.0 * c.Tc + 3.0 * std::max(g.ti, 0.0), 40.0 * c.Tc);
    const Linear r = linearResponses(m, g, c.dt, horizon, c.N);
    if (over) *over = r.diverged ? 1e9 : r.over;
    if (r.diverged) return kInf;
    return c.wSp * r.iaeSp + c.wLd * r.iaeLd;
}

// ---- l'identification ------------------------------------------------------------------
struct SegStat {
    double u0{0}, ymean{0}, syy{0};
};
SegStat segStat(const Segment& s) {
    SegStat st;
    const std::size_t n = s.y.size();
    if (n == 0) return st;
    const std::size_t pre = std::min<std::size_t>(std::max<std::size_t>(s.pre, 1), s.u.size());
    double su = 0;
    for (std::size_t k = 0; k < pre; ++k) su += s.u[k];
    st.u0 = su / static_cast<double>(pre);
    double sy = 0;
    for (float v : s.y) sy += v;
    st.ymean = sy / static_cast<double>(n);
    for (float v : s.y) st.syy += (v - st.ymean) * (v - st.ymean);
    return st;
}

// La reponse du modele de gain 1 (ecart a u0) ; x[k] : la sortie du modele a l'instant k.
template <class F>
void responseLoop(const Segment& s, double u0, double tau, double tau2, double theta, F&& visit) {
    Model m;
    m.tau = tau;
    m.tau2 = tau2;
    m.theta = theta;
    Disc p;
    p.init(m, s.dt);
    const std::size_t n = s.u.size();
    double xa = 0, xb = 0;
    for (std::size_t k = 0; k < n; ++k) {
        visit(k, p.cA * xa + p.cB * xb);
        const double ua = k >= p.d ? s.u[k - p.d] - u0 : 0.0;
        const double ub = k >= p.d + 1 ? s.u[k - p.d - 1] - u0 : 0.0;
        xa = p.A.a * xa + p.A.b1 * ua + p.A.b2 * ub;
        if (p.two) xb = p.B.a * xb + p.B.b1 * ua + p.B.b2 * ub;
    }
}

struct Reduced {
    double sse{kInf}, k{0};
};
// Gauss-Jordan (n <= 4) ; faux : singuliere.
bool invert(std::vector<double> a, int n, std::vector<double>& inv) {
    inv.assign(static_cast<std::size_t>(n * n), 0.0);
    for (int i = 0; i < n; ++i) inv[static_cast<std::size_t>(i * n + i)] = 1.0;
    for (int c = 0; c < n; ++c) {
        int p = c;
        for (int r = c + 1; r < n; ++r) if (std::abs(a[static_cast<std::size_t>(r * n + c)]) > std::abs(a[static_cast<std::size_t>(p * n + c)])) p = r;
        const double piv = a[static_cast<std::size_t>(p * n + c)];
        if (!(std::abs(piv) > 1e-300) || !finite(piv)) return false;
        if (p != c)
            for (int k = 0; k < n; ++k) {
                std::swap(a[static_cast<std::size_t>(c * n + k)], a[static_cast<std::size_t>(p * n + k)]);
                std::swap(inv[static_cast<std::size_t>(c * n + k)], inv[static_cast<std::size_t>(p * n + k)]);
            }
        const double ip = 1.0 / a[static_cast<std::size_t>(c * n + c)];
        for (int k = 0; k < n; ++k) { a[static_cast<std::size_t>(c * n + k)] *= ip; inv[static_cast<std::size_t>(c * n + k)] *= ip; }
        for (int r = 0; r < n; ++r) {
            if (r == c) continue;
            const double f = a[static_cast<std::size_t>(r * n + c)];
            if (f == 0) continue;
            for (int k = 0; k < n; ++k) {
                a[static_cast<std::size_t>(r * n + k)] -= f * a[static_cast<std::size_t>(c * n + k)];
                inv[static_cast<std::size_t>(r * n + k)] -= f * inv[static_cast<std::size_t>(c * n + k)];
            }
        }
    }
    return true;
}

// ---- l'identification rapide : les segments prepares, Levenberg-Marquardt ----------------------
//  Les entrees en ecart au repos, precedees de zeros (le retard sans test), les mesures centrees
//  (le decalage de chaque segment s'en va). Le gain est lineaire : pour des constantes de temps et
//  un retard donnes, il se calcule ; le reste se cherche.
struct Prep {
    std::vector<const Segment*>      segs;
    std::vector<double>              w;
    std::vector<std::size_t>         off;
    std::vector<std::vector<double>> du;
    std::vector<double>              yc;
    std::size_t pad{0}, N{0};
    double syyW{0}, dtMin{0}, longest{0}, thHi{0}, ltLo{0}, ltHi{0};
};

bool prepare(Prep& P, const std::vector<const Segment*>& segsIn, const std::vector<double>& weightsIn) {
    for (std::size_t i = 0; i < segsIn.size(); ++i) {
        const Segment* s = segsIn[i];
        if (!s || s->u.size() < 8 || s->u.size() != s->y.size() || !(s->dt > 0)) continue;
        const double wi = i < weightsIn.size() ? weightsIn[i] : 1.0;
        if (!(wi > 0)) continue;
        P.segs.push_back(s);
        P.w.push_back(wi);
    }
    if (P.segs.empty()) return false;
    P.dtMin = P.segs[0]->dt;
    for (const Segment* s : P.segs) {
        P.longest = std::max(P.longest, s->dt * static_cast<double>(s->u.size() - std::min(s->pre, s->u.size())));
        P.dtMin = std::min(P.dtMin, s->dt);
    }
    P.thHi = 0.6 * P.longest;
    P.pad = static_cast<std::size_t>(std::ceil(P.thHi / P.dtMin)) + 3;
    P.ltLo = std::log(0.3 * P.dtMin);
    P.ltHi = std::log(50.0 * std::max(P.longest, P.dtMin));
    for (std::size_t i = 0; i < P.segs.size(); ++i) {
        const Segment& s = *P.segs[i];
        const SegStat st = segStat(s);
        std::vector<double> d(P.pad + s.u.size(), 0.0);
        for (std::size_t k = 0; k < s.u.size(); ++k) d[P.pad + k] = s.u[k] - st.u0;
        P.du.push_back(std::move(d));
        P.off.push_back(P.N);
        for (float v : s.y) P.yc.push_back(v - st.ymean);
        P.N += s.y.size();
        P.syyW += P.w[i] * st.syy;
    }
    return P.syyW > 0;
}

// La reponse de gain 1 de chaque segment, centree, dans x (N valeurs) ; sans x : les sommes seules.
template <class Sink>
void sweep(const Prep& P, double tau, double tau2, double theta, Sink&& sink) {
    Model m;
    m.tau = tau;
    m.tau2 = tau2;
    m.theta = std::clamp(theta, 0.0, P.thHi);
    for (std::size_t i = 0; i < P.segs.size(); ++i) {
        const Segment& s = *P.segs[i];
        Disc p;
        p.init(m, s.dt);
        const std::ptrdiff_t d = static_cast<std::ptrdiff_t>(std::min<std::size_t>(p.d, P.pad - 2));
        const double* base = P.du[i].data() + P.pad;
        const std::size_t n = s.u.size();
        double xa = 0, xb = 0;
        if (p.two) {
            for (std::size_t k = 0; k < n; ++k) {
                const std::ptrdiff_t q = static_cast<std::ptrdiff_t>(k) - d;
                sink(i, k, p.cA * xa + p.cB * xb);
                const double ua = base[q], ub = base[q - 1];
                xa = p.A.a * xa + p.A.b1 * ua + p.A.b2 * ub;
                xb = p.B.a * xb + p.B.b1 * ua + p.B.b2 * ub;
            }
        } else {
            for (std::size_t k = 0; k < n; ++k) {
                const std::ptrdiff_t q = static_cast<std::ptrdiff_t>(k) - d;
                sink(i, k, xa);
                xa = p.A.a * xa + p.A.b1 * base[q] + p.A.b2 * base[q - 1];
            }
        }
    }
}

// L'ecart quadratique (pese) avec le meilleur gain, sans rien garder.
Reduced sseOf(const Prep& P, double tau, double tau2, double theta) {
    std::vector<double> sx(P.segs.size(), 0.0), sxx(P.segs.size(), 0.0), sxy(P.segs.size(), 0.0);
    sweep(P, tau, tau2, theta, [&](std::size_t i, std::size_t k, double x) {
        sx[i] += x; sxx[i] += x * x; sxy[i] += x * P.yc[P.off[i] + k];
    });
    double Sxx = 0, Sxy = 0;
    for (std::size_t i = 0; i < P.segs.size(); ++i) {
        const double n = static_cast<double>(P.segs[i]->y.size());
        Sxx += P.w[i] * (sxx[i] - sx[i] * sx[i] / n);
        Sxy += P.w[i] * sxy[i];
    }
    Reduced r;
    if (!(Sxx > 1e-300)) { r.sse = P.syyW; return r; }
    r.k = Sxy / Sxx;
    r.sse = std::max(0.0, P.syyW - Sxy * Sxy / Sxx);
    return r;
}

void responses(const Prep& P, double tau, double tau2, double theta, std::vector<double>& x) {
    x.resize(P.N);
    sweep(P, tau, tau2, theta, [&](std::size_t i, std::size_t k, double v) { x[P.off[i] + k] = v; });
    for (std::size_t i = 0; i < P.segs.size(); ++i) {
        const std::size_t n = P.segs[i]->y.size();
        double mean = 0;
        for (std::size_t k = 0; k < n; ++k) mean += x[P.off[i] + k];
        mean /= static_cast<double>(n);
        for (std::size_t k = 0; k < n; ++k) x[P.off[i] + k] -= mean;
    }
}

// Levenberg-Marquardt sur (K, log tau, theta[, log r]), tau2 = r . tau ; la jacobienne par
// differences avant. Au point trouve : J'WJ (pour les intervalles) et l'autocorrelation des residus.
struct Lm {
    bool   ok{false};
    double k{0}, tau{0}, tau2{0}, theta{0}, sse{kInf}, rho{0};
    std::vector<double> A;
    int np{3};
};
Lm levenberg(const Prep& P, double tau0, double tau20, double th0, int maxIter) {
    Lm out;
    const bool two = tau20 > 0;
    const int np = two ? 4 : 3;
    out.np = np;
    const double lrLo = std::log(0.01), lrHi = std::log(0.9);
    auto clampq = [&](std::array<double, 4>& q) {
        q[1] = std::clamp(q[1], P.ltLo, P.ltHi);
        q[2] = std::clamp(q[2], 0.0, P.thHi);
        if (two) q[3] = std::clamp(q[3], lrLo, lrHi);
    };
    auto lags = [&](const std::array<double, 4>& q, double& t1, double& t2) {
        t1 = std::exp(q[1]);
        t2 = two ? std::exp(q[3]) * t1 : 0.0;
    };
    std::array<double, 4> q{0.0, std::log(std::max(tau0, 1e-9)), th0, two ? std::log(std::max(tau20, 1e-12) / std::max(tau0, 1e-9)) : 0.0};
    clampq(q);
    double t1, t2;
    lags(q, t1, t2);
    Reduced cur = sseOf(P, t1, t2, q[2]);
    q[0] = cur.k;
    std::vector<double> x, xj, J(static_cast<std::size_t>(np - 1) * P.N);
    std::vector<double> A(static_cast<std::size_t>(np * np)), g(static_cast<std::size_t>(np));
    double lambda = 1e-3;
    auto jacobian = [&]() {
        lags(q, t1, t2);
        responses(P, t1, t2, q[2], x);
        for (int j = 1; j < np; ++j) {
            std::array<double, 4> qq = q;
            double h = j == 2 ? std::max(1e-4 * q[2], 1e-3 * P.dtMin) : 1e-4;
            if (j == 2 && qq[2] + h > P.thHi) h = -h;
            qq[static_cast<std::size_t>(j)] += h;
            double a1, a2;
            lags(qq, a1, a2);
            responses(P, a1, a2, qq[2], xj);
            double* col = J.data() + static_cast<std::size_t>(j - 1) * P.N;
            for (std::size_t k = 0; k < P.N; ++k) col[k] = q[0] * (xj[k] - x[k]) / h;
        }
        std::fill(A.begin(), A.end(), 0.0);
        std::fill(g.begin(), g.end(), 0.0);
        double r1num = 0, r1den = 0;
        for (std::size_t i = 0; i < P.segs.size(); ++i) {
            const double w = P.w[i];
            const std::size_t n = P.segs[i]->y.size(), o = P.off[i];
            double prev = 0;
            for (std::size_t k = 0; k < n; ++k) {
                double c[4] = {x[o + k], 0, 0, 0};
                for (int j = 1; j < np; ++j) c[j] = J[static_cast<std::size_t>(j - 1) * P.N + o + k];
                const double r = P.yc[o + k] - q[0] * x[o + k];
                for (int a = 0; a < np; ++a) {
                    g[static_cast<std::size_t>(a)] += w * c[a] * r;
                    for (int b = 0; b < np; ++b) A[static_cast<std::size_t>(a * np + b)] += w * c[a] * c[b];
                }
                if (k > 0) r1num += w * r * prev;
                r1den += w * r * r;
                prev = r;
            }
        }
        out.rho = r1den > 0 ? std::clamp(r1num / r1den, 0.0, 0.95) : 0.0;
    };
    for (int it = 0; it < maxIter; ++it) {
        jacobian();
        bool accepted = false;
        const double before = cur.sse;
        for (int tries = 0; tries < 14 && !accepted; ++tries) {
            std::vector<double> M = A, inv;
            for (int a = 0; a < np; ++a) M[static_cast<std::size_t>(a * np + a)] *= 1.0 + lambda;
            if (!invert(M, np, inv)) { lambda *= 4.0; continue; }
            std::array<double, 4> qn = q;
            for (int a = 0; a < np; ++a) {
                double d = 0;
                for (int b = 0; b < np; ++b) d += inv[static_cast<std::size_t>(a * np + b)] * g[static_cast<std::size_t>(b)];
                qn[static_cast<std::size_t>(a)] += d;
            }
            clampq(qn);
            double n1, n2;
            lags(qn, n1, n2);
            const Reduced rn = sseOf(P, n1, n2, qn[2]);
            if (rn.sse < cur.sse) {
                q = qn;
                q[0] = rn.k;                               // le gain : son optimum exact
                cur = rn;
                lambda = std::max(lambda / 3.0, 1e-12);
                accepted = true;
            } else {
                lambda *= 4.0;
            }
        }
        if (!accepted || (before - cur.sse) <= 1e-10 * before) break;
    }
    jacobian();                                            // au point trouve
    out.ok = finite(cur.sse) && q[0] != 0;
    out.k = q[0];
    lags(q, out.tau, out.tau2);
    out.theta = q[2];
    out.sse = cur.sse;
    out.A = A;
    return out;
}
} // namespace

// ---- le bloc ------------------------------------------------------------------------------
std::string_view formName(Form f) noexcept {
    switch (f) {
    case Form::Parallel: return "Parall\xC3\xA8le";
    case Form::Series:   return "S\xC3\xA9rie";
    default:             return "Standard";
    }
}

std::optional<Form> parseForm(std::string_view text) noexcept {
    std::string s;
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x80 && std::isalnum(c)) s += static_cast<char>(std::toupper(c));
    }
    if (s.empty()) return std::nullopt;
    if (s == "0") return Form::Standard;
    if (s == "1") return Form::Parallel;
    if (s == "2") return Form::Series;
    auto starts = [&](std::string_view p) { return s.compare(0, p.size(), p) == 0; };
    if (starts("NONINTERACT")) return Form::Standard;
    if (starts("STAND") || starts("STD") || starts("ISA") || starts("MIX") || starts("IDEAL")) return Form::Standard;
    if (starts("PARAL") || starts("INDEP")) return Form::Parallel;
    if (starts("SERI") || starts("INTERACT") || starts("CLASS")) return Form::Series;
    return std::nullopt;
}

bool seriesRepresentable(const Gains& g) noexcept {
    return g.td <= 0 || g.ti <= 0 || g.ti >= 4.0 * g.td - 1e-12;
}

BlockValues toBlock(const Gains& g, Form f, const BlockUnits& u) noexcept {
    const double tu = u.timeSeconds > 0 ? u.timeSeconds : 1.0;
    const double ks = u.kpScale != 0 ? u.kpScale : 1.0;
    BlockValues v;
    switch (f) {
    case Form::Parallel:
        v.p = ks * g.kp;
        v.i = g.ti > 0 ? ks * g.kp * tu / g.ti : 0.0;
        v.d = g.td > 0 ? ks * g.kp * g.td / tu : 0.0;
        break;
    case Form::Series:
        if (g.td <= 0 || g.ti <= 0) {
            v.p = ks * g.kp; v.i = g.ti > 0 ? g.ti / tu : 0.0; v.d = g.td > 0 ? g.td / tu : 0.0;
        } else {
            const double r = std::sqrt(std::max(0.0, 1.0 - 4.0 * g.td / g.ti));
            v.p = ks * g.kp * (1.0 + r) / 2.0;
            v.i = g.ti * (1.0 + r) / 2.0 / tu;
            v.d = g.ti * (1.0 - r) / 2.0 / tu;
        }
        break;
    default:
        v.p = ks * g.kp;
        v.i = g.ti > 0 ? g.ti / tu : 0.0;
        v.d = g.td > 0 ? g.td / tu : 0.0;
        break;
    }
    return v;
}

std::optional<Gains> fromBlock(const BlockValues& v, Form f, const BlockUnits& u) noexcept {
    const double tu = u.timeSeconds > 0 ? u.timeSeconds : 1.0;
    const double ks = u.kpScale != 0 ? u.kpScale : 1.0;
    if (!finite(v.p) || !finite(v.i) || !finite(v.d)) return std::nullopt;
    Gains g;
    switch (f) {
    case Form::Parallel:
        if (!(v.p > 0)) return std::nullopt;
        g.kp = v.p / ks;
        g.ti = v.i > 0 ? v.p * tu / v.i : 0.0;
        g.td = v.d > 0 ? v.d * tu / v.p : 0.0;
        break;
    case Form::Series: {
        const double kc = v.p / ks, ti = v.i * tu, td = v.d * tu;
        if (ti > 0 && td > 0) {
            g.kp = kc * (1.0 + td / ti);
            g.ti = ti + td;
            g.td = ti * td / (ti + td);
        } else {
            g.kp = kc; g.ti = ti > 0 ? ti : 0.0; g.td = td > 0 ? td : 0.0;
        }
        break;
    }
    default:
        g.kp = v.p / ks;
        g.ti = v.i > 0 ? v.i * tu : 0.0;
        g.td = v.d > 0 ? v.d * tu : 0.0;
        break;
    }
    if (!(g.kp >= 0) || !finite(g.kp) || !finite(g.ti) || !finite(g.td)) return std::nullopt;
    return g;
}

// ---- l'analyse, la reponse --------------------------------------------------------------------
Analysis analyze(const Model& m, const Gains& g, const LoopContext& ctx) {
    return analyzeImpl(m, g, ctx, 480, true);
}

Trace simulate(const Model& m, const Gains& g, const Scenario& sc, const LoopContext& ctx) {
    Trace tr;
    if (!(m.tau > 0) || m.k == 0) return tr;
    const double h = std::max(ctx.period, 1e-3);
    const double T = charTime(m, h);
    tr.stepAt = sc.stepAt >= 0 ? sc.stepAt : std::max(10.0 * h, 0.2 * T);
    tr.loadAt = sc.loadAt >= 0 ? sc.loadAt : tr.stepAt + 5.0 * T + 2.0 * std::max(g.ti, 0.0);
    double duration = sc.duration > 0 ? sc.duration : tr.loadAt + 5.0 * T + 2.0 * std::max(g.ti, 0.0);
    duration = std::max(duration, tr.stepAt + h);
    double dt = h;
    if (duration / dt > 30000.0) dt = duration / 30000.0;
    tr.dt = dt;
    tr.sp0 = sc.sp0;
    tr.sp1 = sc.sp0 + sc.step;
    const double N = ctx.derivFilter > 1 ? ctx.derivFilter : 10.0;
    const double sgn = m.k >= 0 ? 1.0 : -1.0;          // une boucle directe : la sortie a l'envers
    Disc p;
    p.init(m, dt);
    const std::size_t n = static_cast<std::size_t>(duration / dt) + 1;
    std::vector<double> hist(p.d + 2, 0.0);
    const std::size_t L = hist.size();
    const double Tf = g.td > 0 ? g.td / N : 0.0;
    double xa = 0, xb = 0, I = sc.u0, D = 0, pvPrev = sc.sp0;
    std::size_t pos = 0;
    tr.pv.reserve(n);
    tr.out.reserve(n);
    for (std::size_t k = 0; k < n; ++k) {
        const double t = static_cast<double>(k) * dt;
        const double pv = sc.sp0 + m.k * (p.cA * xa + p.cB * xb);
        const double sp = t >= tr.stepAt ? tr.sp1 : tr.sp0;
        const double e = sp - pv;
        if (g.td > 0) D = Tf / (Tf + dt) * D - sgn * g.kp * g.td / (Tf + dt) * (pv - pvPrev);
        const double free = sgn * g.kp * e + I + D;
        double u = free;
        const double dI = g.ti > 0 ? sgn * g.kp * dt / g.ti * e : 0.0;
        if (free > sc.outMax) { u = sc.outMax; if (dI < 0) I += dI; }
        else if (free < sc.outMin) { u = sc.outMin; if (dI > 0) I += dI; }
        else I += dI;
        pvPrev = pv;
        tr.pv.push_back(static_cast<float>(pv));
        tr.out.push_back(static_cast<float>(u));
        hist[pos] = u + (t >= tr.loadAt ? sc.load : 0.0) - sc.u0;
        const std::size_t ia = pos >= p.d ? pos - p.d : pos + L - p.d;
        const std::size_t ib = ia == 0 ? L - 1 : ia - 1;
        xa = p.A.a * xa + p.A.b1 * hist[ia] + p.A.b2 * hist[ib];
        if (p.two) xb = p.B.a * xb + p.B.b1 * hist[ia] + p.B.b2 * hist[ib];
        if (++pos == L) pos = 0;
    }
    return tr;
}

Metrics measure(const Trace& tr, double band) {
    Metrics mt;
    if (tr.pv.empty() || !(tr.dt > 0)) return mt;
    const double step = tr.sp1 - tr.sp0;
    if (!(band > 0)) band = std::max(0.02 * std::abs(step), 1e-9);
    const std::size_t n = tr.pv.size();
    const std::size_t i0 = std::min(n, static_cast<std::size_t>(std::ceil(tr.stepAt / tr.dt - 1e-9)));
    const std::size_t i1 = std::min(n, static_cast<std::size_t>(std::ceil(tr.loadAt / tr.dt - 1e-9)));
    double peak = 0;
    std::size_t lastOut = i0;
    bool outside = false;
    long t10 = -1, t90 = -1;
    for (std::size_t k = i0; k < i1; ++k) {
        const double v = tr.pv[k];
        const double dev = step != 0 ? (v - tr.sp1) / step : 0.0;   // > 0 : au-dela de la consigne
        peak = std::max(peak, dev);
        mt.iaeSp += std::abs(tr.sp1 - v) * tr.dt;
        if (std::abs(v - tr.sp1) > band) { lastOut = k; outside = true; }
        const double frac = step != 0 ? (v - tr.sp0) / step : 1.0;
        if (t10 < 0 && frac >= 0.1) t10 = static_cast<long>(k);
        if (t90 < 0 && frac >= 0.9) t90 = static_cast<long>(k);
    }
    mt.overshoot = std::max(0.0, peak * 100.0);
    if (i1 > i0) {
        const bool settledAtEnd = std::abs(tr.pv[i1 - 1] - tr.sp1) <= band;
        mt.settle = !settledAtEnd ? -1.0 : (outside ? static_cast<double>(lastOut + 1 - i0) * tr.dt : 0.0);
    }
    if (t10 >= 0 && t90 >= 0) mt.rise = static_cast<double>(t90 - t10) * tr.dt;
    std::size_t lastLd = i1;
    bool outLd = false;
    for (std::size_t k = i1; k < n; ++k) {
        const double d = std::abs(tr.pv[k] - tr.sp1);
        mt.loadDev = std::max(mt.loadDev, d);
        mt.iaeLoad += d * tr.dt;
        if (d > band) { lastLd = k; outLd = true; }
    }
    if (i1 < n) {
        const bool back = std::abs(tr.pv[n - 1] - tr.sp1) <= band;
        mt.loadRecover = !back ? -1.0 : (outLd ? static_cast<double>(lastLd + 1 - i1) * tr.dt : 0.0);
    }
    return mt;
}

// ---- le reglage --------------------------------------------------------------------------------
Limits limitsOf(Goal g) noexcept {
    switch (g) {
    case Goal::Robust: return {1.4, 5.0, 0.2};
    case Goal::Fast:   return {1.9, 20.0, 0.6};
    default:           return {1.6, 10.0, 0.3};
    }
}

std::string_view goalName(Goal g) noexcept {
    switch (g) {
    case Goal::Robust: return "Robuste";
    case Goal::Fast:   return "Rapide";
    default:           return "\xC3\x89quilibr\xC3\xA9";
    }
}

double costOf(const Model& m, const Gains& g, const LoopContext& ctx) {
    if (!(m.tau > 0) || m.k == 0 || !(g.kp > 0)) return kInf;
    return costWith(m, g, costing(m, ctx), nullptr);
}

std::array<Proposal, 3> tuneAll(const TuneRequest& rq) {
    std::array<Proposal, 3> out;
    for (int i = 0; i < 3; ++i) out[static_cast<std::size_t>(i)].goal = static_cast<Goal>(i);
    const Model& m = rq.model;
    if (!m.valid || !(m.tau > 0) || m.k == 0 || !(m.theta >= 0)) return out;
    const LoopContext& ctx = rq.ctx;
    const double h = std::max(ctx.period, 1e-3);
    const Costing cs = costing(m, ctx);
    const double span = rq.outSpan > 0 ? rq.outSpan : 100.0;
    std::array<Limits, 3> lim{};
    std::array<double, 3> noiseMax{};
    for (int i = 0; i < 3; ++i) {
        lim[static_cast<std::size_t>(i)] = limitsOf(static_cast<Goal>(i));
        noiseMax[static_cast<std::size_t>(i)] = lim[static_cast<std::size_t>(i)].noise / 100.0 * span;
    }
    const double msTop = lim[2].ms;
    const double thE = std::max(thetaEq(m), h);
    const double tauC = std::min(tauEq(m), 40.0 * std::max(thetaEq(m), 4.0 * h));
    // les candidats de Ti (log) et de Td (en part du retard ; la seconde constante de temps)
    double tiLo = std::max(0.2 * thetaEq(m), 2.0 * h);
    double tiHi = 2.0 * (tauC + thetaEq(m));
    if (tiHi < 3.0 * tiLo) tiHi = 3.0 * tiLo;
    constexpr int nTi = 12;
    std::vector<double> tds;
    if (rq.derivative != Derivative::Always) tds.push_back(0.0);
    if (rq.derivative != Derivative::Never) {
        for (double r : {0.15, 0.3, 0.5}) tds.push_back(r * thE);
        if (m.tau2 > 2.0 * h) tds.push_back(m.tau2);
    }

    struct Cand {
        Gains    g;
        double   cost{kInf};
        double   over{0};
        Analysis an;
        bool     ok{false};          // stable, calcule
    };
    // le Kp le plus fort qui tient la robustesse (et le bruit si `noise` > 0)
    auto feasible = [&](const Gains& g, double ms, double noise) {
        const Analysis a = analyzeImpl(m, g, ctx, 160, noise > 0, ms);
        return a.stable && a.ms <= ms && a.mt <= ms && (!(noise > 0) || a.noiseOut <= noise);
    };
    auto kpMax = [&](double ti, double td, double ms, double noise) {
        Gains g{1.0 / std::abs(m.k), ti, td};
        double lo = 0, hi = 0;
        if (feasible(g, ms, noise)) {
            lo = g.kp;
            for (int i = 0; i < 40; ++i) {
                g.kp *= 2.0;
                if (!feasible(g, ms, noise)) { hi = g.kp; break; }
                lo = g.kp;
            }
            if (hi == 0) return lo;
        } else {
            hi = g.kp;
            for (int i = 0; i < 40; ++i) {
                g.kp *= 0.5;
                if (feasible(g, ms, noise)) { lo = g.kp; break; }
                hi = g.kp;
            }
            if (lo == 0) return 0.0;
        }
        for (int i = 0; i < 20; ++i) {
            g.kp = std::sqrt(lo * hi);
            if (feasible(g, ms, noise)) lo = g.kp; else hi = g.kp;
        }
        return lo;
    };
    // un candidat : son analyse (sans limite), son cout et son depassement
    auto evaluate = [&](const Gains& g) {
        Cand c;
        c.g = g;
        if (!(g.kp > 0)) return c;
        c.an = analyzeImpl(m, g, ctx, 160, true, 2.5);
        if (!c.an.stable || c.an.ms > msTop || c.an.mt > msTop) return c;
        c.cost = costWith(m, g, cs, &c.over);
        c.ok = finite(c.cost);
        return c;
    };
    auto fits = [&](const Cand& c, int goal, bool noise) {
        const Limits& l = lim[static_cast<std::size_t>(goal)];
        return c.ok && c.an.ms <= l.ms && c.an.mt <= l.ms && c.over <= l.overshoot &&
               (!noise || c.an.noiseOut <= noiseMax[static_cast<std::size_t>(goal)]);
    };
    std::array<Cand, 3> bestPI, bestPID, bestPIDnoNoise;
    auto consider = [&](const Cand& c) {
        const bool pid = c.g.td > 0;
        for (int i = 0; i < 3; ++i) {
            if (pid && fits(c, i, false) && c.cost < bestPIDnoNoise[static_cast<std::size_t>(i)].cost) bestPIDnoNoise[static_cast<std::size_t>(i)] = c;
            if (!fits(c, i, true)) continue;
            Cand& b = pid ? bestPID[static_cast<std::size_t>(i)] : bestPI[static_cast<std::size_t>(i)];
            if (c.cost < b.cost) b = c;
        }
    };
    static constexpr double kRatios[] = {0.3, 0.45, 0.6, 0.75, 0.87, 0.95, 1.0};
    for (double td0 : tds) {
        const bool pid = td0 > 0;
        for (int i = 0; i < nTi; ++i) {
            const double ti = tiLo * std::pow(tiHi / tiLo, static_cast<double>(i) / (nTi - 1));
            double td = td0;
            if (pid && td > 0.25 * ti) {
                if (rq.derivative == Derivative::Always || rq.form == Form::Series) td = 0.25 * ti; else continue;
            }
            std::vector<double> kps;
            for (int gi = 0; gi < 3; ++gi) {
                const double kAll = kpMax(ti, td, lim[static_cast<std::size_t>(gi)].ms, noiseMax[static_cast<std::size_t>(gi)]);
                const double kMs = pid ? kpMax(ti, td, lim[static_cast<std::size_t>(gi)].ms, 0.0) : kAll;
                for (double r : kRatios) {
                    if (kAll > 0) kps.push_back(kAll * r);
                    if (pid && kMs > kAll * 1.05) kps.push_back(kMs * r);
                }
            }
            std::sort(kps.begin(), kps.end());
            double last = -1;
            for (double kp : kps) {
                if (last > 0 && kp < last * 1.002) continue;
                last = kp;
                consider(evaluate({kp, ti, td}));
            }
        }
    }
    // l'affinage, par objectif : recherche par motifs en (log Kp, log Ti), Td dans la meme part de Ti
    auto refine = [&](Cand c, int goal) {
        if (!c.ok) return c;
        const bool pid = c.g.td > 0;
        double sk = 0.1, st = 0.5 * std::log(tiHi / tiLo) / (nTi - 1);
        const double ratio = c.g.ti > 0 ? c.g.td / c.g.ti : 0.0;
        for (int it = 0; it < 60 && (sk > 0.003 || st > 0.003); ++it) {
            Cand best = c;
            for (int dk = -1; dk <= 1; ++dk) for (int dt = -1; dt <= 1; ++dt) {
                if (!dk && !dt) continue;
                Gains g = c.g;
                g.kp *= std::exp(dk * sk);
                g.ti *= std::exp(dt * st);
                g.td = pid ? ratio * g.ti : 0.0;
                if (g.ti < 0.5 * tiLo || g.ti > 2.0 * tiHi) continue;
                const Cand e = evaluate(g);
                if (fits(e, goal, true) && e.cost < best.cost) best = e;
            }
            if (best.cost < c.cost - 1e-12 * std::abs(c.cost)) c = best; else { sk *= 0.5; st *= 0.5; }
        }
        return c;
    };
    for (int gi = 0; gi < 3; ++gi) {
        const std::size_t i = static_cast<std::size_t>(gi);
        Proposal& p = out[i];
        const Cand pi = refine(bestPI[i], gi);
        const Cand pd = refine(bestPID[i], gi);
        const Cand* pick = nullptr;
        if (rq.derivative == Derivative::Never) pick = pi.ok ? &pi : nullptr;
        else if (rq.derivative == Derivative::Always) pick = pd.ok ? &pd : (pi.ok ? &pi : nullptr);
        else if (pi.ok && pd.ok) pick = pd.cost < 0.9 * pi.cost ? &pd : &pi;
        else pick = pi.ok ? &pi : (pd.ok ? &pd : nullptr);
        if (pi.ok && bestPIDnoNoise[i].ok) {
            p.derivativeGain = (pi.cost - bestPIDnoNoise[i].cost) / pi.cost * 100.0;
            p.derivativeNoise = bestPIDnoNoise[i].an.noiseOut / span * 100.0;
        }
        if (!pick) continue;
        p.ok = true;
        p.gains = pick->g;
        p.an = analyze(m, pick->g, ctx);
        p.cost = pick->cost;
        p.overshoot = pick->over;
        p.withDerivative = pick->g.td > 0;
    }
    return out;
}

Proposal tune(const TuneRequest& rq, Goal goal) { return tuneAll(rq)[static_cast<std::size_t>(goal)]; }

// ---- l'identification ---------------------------------------------------------------------------
Model modelOf(const Fit& f) {
    Model m;
    m.k = f.k;
    m.tau = f.tau;
    m.tau2 = f.tau2;
    m.theta = f.theta;
    m.kHalf = f.kHalf;
    m.tauHalf = f.tauHalf;
    m.tau2Half = f.tau2Half;
    m.thetaHalf = f.thetaHalf;
    m.fitLearn = f.fit;
    m.valid = f.ok;
    return m;
}

Fit identify(const std::vector<const Segment*>& segsIn, const std::vector<double>& weightsIn, const std::vector<Fit>& starts,
             bool secondOrder) {
    Fit f;
    Prep P;
    if (!prepare(P, segsIn, weightsIn)) return f;
    // ---- le premier ordre : depuis les departs donnes (a petits pas), sinon une grille
    double bT = 0, bH = 0, bS = kInf;
    bool warm = false;
    for (const Fit& st0 : starts) {
        if (!st0.ok || !(st0.tau > 0)) continue;
        const double t = st0.tau + 0.5 * std::max(st0.tau2, 0.0), h = st0.theta + 0.5 * std::max(st0.tau2, 0.0);
        const Reduced r = sseOf(P, t, 0.0, h);
        if (r.sse < bS) { bS = r.sse; bT = t; bH = h; }
        warm = true;
    }
    if (!warm) {
        constexpr int nT = 18, nH = 16;
        for (int jh = 0; jh < nH; ++jh) {
            const double h = P.thHi * jh / (nH - 1);
            for (int it = 0; it < nT; ++it) {
                const double t = std::exp(P.ltLo + (P.ltHi - P.ltLo) * it / (nT - 1));
                const Reduced r = sseOf(P, t, 0.0, h);
                if (r.sse < bS) { bS = r.sse; bT = t; bH = h; }
            }
        }
    }
    if (!finite(bS)) return f;
    const Lm fo = levenberg(P, bT, 0.0, bH, warm ? 25 : 60);
    if (!fo.ok) return f;
    f.sse1 = fo.sse;
    const Lm* chosen = &fo;
    // ---- deux constantes de temps : depuis le premier ordre (la regle de la moitie a l'envers) et
    //      depuis les departs donnes
    Lm so;
    auto halves = [&](const Lm& r, double& kH, double& tH, double& hH, double& t2H) {
        const double dof = std::max(1.0, [&] {
            double sw = 0;
            for (std::size_t i = 0; i < P.segs.size(); ++i) sw += P.w[i] * static_cast<double>(P.segs[i]->y.size());
            return sw;
        }() - r.np - static_cast<double>(P.segs.size()));
        const double s2 = r.sse / dof * (1.0 + r.rho) / (1.0 - r.rho);
        std::vector<double> cov;
        kH = tH = hH = t2H = 0;
        if (!invert(r.A, r.np, cov)) return false;
        auto c = [&](int a, int b) { return s2 * cov[static_cast<std::size_t>(a * r.np + b)]; };
        kH = 1.96 * std::sqrt(std::max(0.0, c(0, 0)));
        tH = r.tau * 1.96 * std::sqrt(std::max(0.0, c(1, 1)));
        hH = 1.96 * std::sqrt(std::max(0.0, c(2, 2)));
        if (r.np == 4) t2H = r.tau2 * 1.96 * std::sqrt(std::max(0.0, c(1, 1) + c(3, 3) + 2.0 * c(1, 3)));
        return true;
    };
    if (secondOrder) {
        double s1 = 0, s2v = 0, sh = 0, sS = kInf;
        auto seed = [&](double t1, double t2, double th) {
            if (!(t1 > 0) || !(t2 > 0) || t2 >= t1) return;
            t2 = std::clamp(t2 / t1, 0.01, 0.9) * t1;
            th = std::clamp(th, 0.0, P.thHi);
            const Reduced r = sseOf(P, t1, t2, th);
            if (r.sse < sS) { sS = r.sse; s1 = t1; s2v = t2; sh = th; }
        };
        for (double r0 : {0.05, 0.12, 0.25, 0.45}) {
            const double t1 = fo.tau / (1.0 + 0.5 * r0);
            seed(t1, r0 * t1, fo.theta - 0.5 * r0 * t1);
        }
        for (const Fit& st0 : starts) if (st0.ok && st0.tau2 > 0) seed(st0.tau, st0.tau2, st0.theta);
        if (finite(sS)) {
            so = levenberg(P, s1, s2v, sh, warm ? 25 : 60);
            double kH, tH, hH, t2H;
            if (so.ok && so.sse <= 0.85 * fo.sse && so.tau2 >= std::max(2.0 * P.dtMin, 0.02 * so.tau) &&
                halves(so, kH, tH, hH, t2H) && t2H > 0 && t2H <= 0.5 * so.tau2)
                chosen = &so;
        }
    }
    const Lm& r = *chosen;
    f.k = r.k;
    f.tau = r.tau;
    f.tau2 = r.tau2;
    f.theta = r.theta;
    f.sse = r.sse;
    f.rho = r.rho;
    f.fit = 100.0 * (1.0 - std::sqrt(r.sse / P.syyW));
    halves(r, f.kHalf, f.tauHalf, f.thetaHalf, f.tau2Half);
    f.ok = finite(f.k) && f.k != 0 && finite(f.fit);
    return f;
}

std::vector<double> predict(const Segment& s, const Model& m) {
    std::vector<double> out(s.u.size(), 0.0);
    if (s.u.empty() || !(m.tau > 0)) return out;
    const SegStat st = segStat(s);
    const std::size_t pre = std::min<std::size_t>(std::max<std::size_t>(s.pre, 1), s.y.size());
    double y0 = 0;
    for (std::size_t i = 0; i < pre; ++i) y0 += s.y[i];
    y0 /= static_cast<double>(pre);
    responseLoop(s, st.u0, m.tau, m.tau2, m.theta, [&](std::size_t i, double x) { out[i] = y0 + m.k * x; });
    return out;
}

double checkFit(const Segment& s, const Model& m) {
    if (s.y.size() < 8) return -1;
    const std::vector<double> yh = predict(s, m);
    const SegStat st = segStat(s);
    if (!(st.syy > 0)) return -1;
    double e2 = 0;
    for (std::size_t i = 0; i < s.y.size(); ++i) e2 += (s.y[i] - yh[i]) * (s.y[i] - yh[i]);
    return 100.0 * (1.0 - std::sqrt(e2 / st.syy));
}

double gainWith(const Segment& s, const Model& m) {
    if (s.y.size() < 8 || !(m.tau > 0)) return 0;
    const SegStat st = segStat(s);
    double sx = 0, sxx = 0, sxy = 0;
    responseLoop(s, st.u0, m.tau, m.tau2, m.theta, [&](std::size_t k, double x) {
        sx += x; sxx += x * x; sxy += x * (s.y[k] - st.ymean);
    });
    const double n = static_cast<double>(s.y.size());
    const double cxx = sxx - sx * sx / n;
    return cxx > 1e-300 ? sxy / cxx : 0.0;
}

double harrisIndex(const std::vector<double>& e, int b, int p) {
    const std::size_t n = e.size();
    if (p < 1) p = 1;
    if (n < static_cast<std::size_t>(std::max(10 * p, 200))) return -1;
    if (b < 1) b = 1;
    double mean = 0;
    for (double v : e) mean += v;
    mean /= static_cast<double>(n);
    std::vector<double> r(static_cast<std::size_t>(p) + 1, 0.0);
    for (int k = 0; k <= p; ++k) {
        double s = 0;
        for (std::size_t i = 0; i + static_cast<std::size_t>(k) < n; ++i) s += (e[i] - mean) * (e[i + static_cast<std::size_t>(k)] - mean);
        r[static_cast<std::size_t>(k)] = s / static_cast<double>(n);
    }
    if (!(r[0] > 0)) return -1;
    std::vector<double> a(static_cast<std::size_t>(p) + 1, 0.0), tmp;
    a[0] = 1.0;
    double err = r[0];
    for (int k = 1; k <= p; ++k) {
        double acc = r[static_cast<std::size_t>(k)];
        for (int j = 1; j < k; ++j) acc += a[static_cast<std::size_t>(j)] * r[static_cast<std::size_t>(k - j)];
        const double lam = -acc / err;
        tmp = a;
        for (int j = 1; j < k; ++j) tmp[static_cast<std::size_t>(j)] = a[static_cast<std::size_t>(j)] + lam * a[static_cast<std::size_t>(k - j)];
        tmp[static_cast<std::size_t>(k)] = lam;
        a.swap(tmp);
        err *= (1.0 - lam * lam);
        if (!(err > 0)) return -1;
    }
    std::vector<double> psi(static_cast<std::size_t>(b), 0.0);
    psi[0] = 1.0;
    for (int i = 1; i < b; ++i) {
        double s = 0;
        for (int j = 1; j <= std::min(i, p); ++j) s -= a[static_cast<std::size_t>(j)] * psi[static_cast<std::size_t>(i - j)];
        psi[static_cast<std::size_t>(i)] = s;
    }
    double s2 = 0;
    for (double v : psi) s2 += v * v;
    return std::clamp(err * s2 / r[0], 0.0, 1.0);
}

std::string durationText(double s) {
    char buf[64];
    if (!finite(s)) return "\xE2\x80\x94";
    const bool neg = s < 0;
    s = std::abs(s);
    if (s < 60.0) {
        if (s < 10.0 && std::abs(s - std::round(s)) > 0.05) {
            std::snprintf(buf, sizeof buf, "%.1f s", s);
            for (char* c = buf; *c; ++c) if (*c == '.') *c = ',';
        } else {
            std::snprintf(buf, sizeof buf, "%.0f s", std::round(s));
        }
    } else if (s < 3600.0) {
        const long t = std::lround(s);
        const long mn = t / 60, sec = t % 60;
        if (sec == 0) std::snprintf(buf, sizeof buf, "%ld min", mn);
        else std::snprintf(buf, sizeof buf, "%ld min %02ld s", mn, sec);
    } else {
        const long t = std::lround(s / 60.0);
        std::snprintf(buf, sizeof buf, "%ld h %02ld", t / 60, t % 60);
    }
    return neg ? std::string("-") + buf : std::string(buf);
}

} // namespace hmi::pid
