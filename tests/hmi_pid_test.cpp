// =============================================================================
//  tests/hmi_pid_test.cpp - 1.12.24 : LE REGLAGE PID, SANS ECRAN
// -----------------------------------------------------------------------------
//  hmi/HmiPid.cpp et HmiPidLearn.cpp :
//   - les formes du bloc (standard, parallele, serie ; secondes, minutes ; Kp
//     normalise), aller et retour ;
//   - l'identification exacte d'un premier ordre avec retard sans bruit (retard
//     fractionnaire), et sur plusieurs segments a la fois ;
//   - l'analyse d'une boucle connue (Ms, marges, stabilite, bruit de sortie) ;
//   - le reglage optimise (trois objectifs, la derivee qui n'aide pas avec une
//     mesure au dixieme), et son temps de calcul ;
//   - SEPT JOURS D'UN FOUR SIMULE (le scenario de la maquette) : un procede a
//     deux constantes de temps (112 s, 9 s) et 12 s de retard dont le gain derive
//     de 1,52 a 1,64, une mesure bruitee (0,05) et lue au dixieme, un PI mal regle
//     (Kp 1,8, Ti 30 s), des changements de consigne, des perturbations non
//     mesurees, un passage en MANU (+8 % pendant 20 min). Ce que l'objet en
//     apprend (le modele, ses echelons appris, gardes, ecartes et pourquoi), ses
//     statistiques par fenetre, l'oscillation, l'indice de Harris, la loi du
//     bloc relevee, le gain jour apres jour, le reglage propose et sa
//     verification sur le vrai procede ;
//   - un trou, une mesure figee, la saturation, Revenir/verifier, la memoire
//     relue (sauvegarde puis chargement).
//
//  PID_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiPid.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hmi::pid;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("PID_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    if (!ok || g_obs) std::printf("%s %s\n", ok ? "  ok  " : "ECHEC ", what.c_str());
}
std::string fmt(const char* f, double a, double b = 0, double c = 0, double d = 0) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, a, b, c, d);
    return buf;
}
bool near(double a, double b, double rel, double abs = 0) { return std::abs(a - b) <= std::max(abs, rel * std::abs(b)); }

// Un generateur reproductible partout (xorshift64*, Box-Muller).
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 2685821657736338717ULL + 1) {}
    double uniform() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return static_cast<double>((s * 2685821657736338717ULL) >> 11) * (1.0 / 9007199254740992.0);
    }
    double uniform(double a, double b) { return a + (b - a) * uniform(); }
    double gauss() {
        double u1 = uniform();
        if (u1 < 1e-300) u1 = 1e-300;
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * uniform());
    }
    int pick(int n) { return std::min(n - 1, static_cast<int>(uniform() * n)); }
};

// ---- les formes du bloc ---------------------------------------------------------------
void formes() {
    const Gains g{1.74, 109.0, 8.0};
    for (Form f : {Form::Standard, Form::Parallel, Form::Series}) {
        for (double tu : {1.0, 60.0, 0.001}) {
            const BlockUnits u{tu, 2.5};
            const BlockValues v = toBlock(g, f, u);
            const auto back = fromBlock(v, f, u);
            check(back && near(back->kp, g.kp, 1e-9) && near(back->ti, g.ti, 1e-9) && near(back->td, g.td, 1e-9),
                  std::string("forme ") + std::string(formName(f)) + fmt(" unite %g s : aller et retour", tu));
        }
    }
    const BlockValues par = toBlock({2.0, 100.0, 5.0}, Form::Parallel);
    check(near(par.p, 2.0, 1e-12) && near(par.i, 0.02, 1e-12) && near(par.d, 10.0, 1e-12), "parallele : Ki = Kp/Ti, Kd = Kp.Td");
    const BlockValues ser = toBlock({2.0, 100.0, 16.0}, Form::Series);   // r = 0,6 : Ti' 80, Td' 20, Kc 1,6
    check(near(ser.p, 1.6, 1e-12) && near(ser.i, 80.0, 1e-12) && near(ser.d, 20.0, 1e-12), "serie : Kc 1,6 Ti' 80 Td' 20");
    check(!seriesRepresentable({1, 10, 3}) && seriesRepresentable({1, 12, 3}), "serie : Ti >= 4 Td");
    const BlockValues mn = toBlock({1.0, 120.0, 30.0}, Form::Standard, {60.0, 1.0});
    check(near(mn.i, 2.0, 1e-12) && near(mn.d, 0.5, 1e-12), "en minutes : Ti 2 min, Td 0,5 min");
    check(parseForm("PARALL\xC3\x88LE") == Form::Parallel && parseForm("serie") == Form::Series && parseForm("ISA") == Form::Standard &&
              parseForm("2") == Form::Series && parseForm(" 0 ") == Form::Standard && !parseForm("??"),
          "la variable de la forme : PARALLELE, serie, ISA, 2, 0");
}

// ---- l'identification exacte -------------------------------------------------------------
Segment fopdtSegment(double K, double tau, double theta, double dt, double du, std::size_t pre, std::size_t n, double y0,
                     double u0 = 30.0, double tau2 = 0) {
    // le procede a pas fin (dt/50) puis lu tous les dt : la sortie tenue entre deux lectures
    Segment s;
    s.dt = dt;
    s.pre = pre;
    const int sub = 50;
    const double h = dt / sub;
    std::vector<double> uin;
    double x = 0, x2 = 0;
    for (std::size_t k = 0; k < n; ++k) {
        const double u = u0 + (k >= pre ? du : 0.0) + (k >= pre + n / 3 ? -0.6 * du : 0.0);
        s.u.push_back(static_cast<float>(u));
        s.y.push_back(static_cast<float>(y0 + K * (tau2 > 0 ? x2 : x)));
        for (int q = 0; q < sub; ++q) {
            uin.push_back(u - u0);
            const double tdel = static_cast<double>(uin.size()) * h - theta;
            const long idx = static_cast<long>(std::floor(tdel / h + 1e-9));
            const double ud = idx >= 0 ? uin[static_cast<std::size_t>(idx)] : 0.0;
            x += (ud - x) * (1.0 - std::exp(-h / tau));
            if (tau2 > 0) x2 += (x - x2) * (1.0 - std::exp(-h / tau2));
        }
    }
    return s;
}

void identification() {
    const Segment s = fopdtSegment(1.6, 114.0, 20.3, 0.5, 10.0, 120, 2400, 60.0);
    const Fit f = identify({&s});
    check(f.ok && near(f.k, 1.6, 2e-3) && near(f.tau, 114.0, 3e-3) && near(f.theta, 20.3, 0, 0.05) && f.fit > 99.9,
          fmt("sans bruit : K %.4f tau %.2f theta %.3f (1,6 114 20,3), fit %.3f %%", f.k, f.tau, f.theta, f.fit));
    const Segment s2 = fopdtSegment(1.6, 114.0, 20.3, 0.5, -6.0, 60, 1800, 70.0, 45.0);
    const Fit f2 = identify({&s, &s2}, {1.0, 0.5});
    check(f2.ok && near(f2.k, 1.6, 2e-3) && near(f2.tau, 114.0, 3e-3) && near(f2.theta, 20.3, 0, 0.05),
          fmt("deux segments a la fois : K %.4f tau %.2f theta %.3f", f2.k, f2.tau, f2.theta));
    const Fit f3 = identify({&s}, {}, {f2});
    check(f3.ok && near(f3.tau, f.tau, 1e-3), "depuis un depart donne : le meme optimum");
    check(f.tau2 == 0 && f2.tau2 == 0, "un premier ordre reste un premier ordre (pas de seconde constante)");
    const double cf = checkFit(s2, modelOf(f));
    check(cf > 99.5, fmt("verification sur un autre segment : %.2f %%", cf));
    Model m0;
    m0.tau = 114.0; m0.theta = 20.3;
    const double kOnly = gainWith(s2, m0);
    check(near(kOnly, 1.6, 2e-3), fmt("le gain seul, tau et theta tenus : %.4f", kOnly));
    // deux constantes de temps (le four : 112 s et 9 s, 12 s de retard)
    const Segment so = fopdtSegment(1.6, 112.0, 12.0, 0.5, 10.0, 120, 2400, 60.0, 30.0, 9.0);
    const Fit fs = identify({&so});
    check(fs.ok && fs.tau2 > 0 && near(fs.k, 1.6, 3e-3) && near(fs.tau, 112.0, 0.01) && near(fs.tau2, 9.0, 0.03) && near(fs.theta, 12.0, 0, 0.15),
          fmt("deux constantes : K %.4f tau %.2f tau2 %.2f theta %.2f (1,6 112 9 12)", fs.k, fs.tau, fs.tau2, fs.theta));
    const Fit fs1 = identify({&so}, {}, {}, false);
    check(fs1.ok && fs1.tau2 == 0 && fs1.fit < fs.fit && near(fs1.tau, 114.0, 0.03) && near(fs1.theta, 20.0, 0.05),
          fmt("le meilleur premier ordre : tau %.1f theta %.1f (114 et 20, comme la maquette), fit %.2f %% < %.2f %%", fs1.tau, fs1.theta, fs1.fit, fs.fit));
    // un integrateur (un niveau) : tau immense devant ce qui est vu
    const Segment lv = fopdtSegment(400.0, 4000.0, 8.0, 0.5, 2.0, 60, 1200, 50.0);
    const Fit fl = identify({&lv});
    check(fl.ok && near(fl.k / fl.tau, 0.1, 0.05) && near(fl.theta, 8.0, 0, 0.6),
          fmt("un niveau : K/tau %.4f (0,1), theta %.2f (8)", fl.k / fl.tau, fl.theta));
}

// ---- l'analyse et le reglage ------------------------------------------------------------------
Model ovenModel() {
    Model m;
    m.k = 1.615; m.tau = 114.4; m.theta = 20.3; m.valid = true; m.learned = 3; m.seen = 900;
    return m;
}
// Le cout de la maquette : IAE d'un echelon de 10 + IAE d'une perturbation de 5 %.
LoopContext ovenContext() {
    LoopContext c;
    c.period = 0.5;
    c.noise = std::sqrt(0.05 * 0.05 + 0.1 * 0.1 / 12.0);
    c.spWeight = 10.0;
    c.loadWeight = 5.0;
    return c;
}

void analyse() {
    const Model m = ovenModel();
    const LoopContext c = ovenContext();
    const Analysis cur = analyze(m, {1.8, 30.0, 0.0}, c);
    check(cur.stable && near(cur.ms, 3.2, 0.06) && near(cur.gm, 2.11, 0.06) && near(cur.pm, 19.8, 0, 2.0),
          fmt("le reglage actuel (1,8 ; 30 s) : Ms %.2f (3,20) GM %.2f (2,11) PM %.1f (19,8)", cur.ms, cur.gm, cur.pm));
    check(near(cur.noiseOut, 0.107, 0.1), fmt("le bruit envoye a la sortie : %.3f %% (0,107)", cur.noiseOut));
    const Analysis un = analyze(m, {6.0, 20.0, 0.0}, c);
    check(!un.stable, fmt("Kp 6, Ti 20 s : instable (GM %.2f)", un.gm));
    const Gains eq{1.742, 108.8, 0.0};
    const Analysis ae = analyze(m, eq, c);
    check(ae.stable && near(ae.ms, 1.6, 0.03) && ae.gm > 3.0 && ae.pm > 55, fmt("Equilibre : Ms %.3f GM %.2f PM %.1f", ae.ms, ae.gm, ae.pm));

    // la reponse prevue : consigne 60 -> 70, puis -5 % de perturbation
    Scenario sc;
    sc.sp0 = 60; sc.step = 10; sc.u0 = (60.0 - 20.0) / m.k; sc.load = -5.0;
    sc.stepAt = 30; sc.loadAt = 700; sc.duration = 1300;
    const Trace tc = simulate(m, {1.8, 30.0, 0.0}, sc, c);
    const Metrics mc = measure(tc, 0.2);
    check(near(mc.overshoot, 67.6, 0.04) && near(mc.loadDev, 2.03, 0.05),
          fmt("prevu, actuel : depassement %.1f %% (67,6), ecart sous la perturbation %.2f (2,03)", mc.overshoot, mc.loadDev));
    const Trace te = simulate(m, eq, sc, c);
    const Metrics me = measure(te, 0.2);
    check(near(me.overshoot, 4.9, 0.25) && me.settle > 90 && me.settle < 200 && near(me.loadDev, 2.15, 0.05),
          fmt("prevu, Equilibre : depassement %.1f %% (4,9), pose en %.0f s (135), ecart %.2f (2,15)", me.overshoot, me.settle, me.loadDev));

    const auto t0 = std::chrono::steady_clock::now();
    TuneRequest rq;
    rq.model = m;
    rq.ctx = c;
    const std::array<Proposal, 3> p = tuneAll(rq);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  reglage des trois objectifs : %.0f ms\n", ms);
    for (int g = 0; g < 3; ++g) {
        const Limits l = limitsOf(static_cast<Goal>(g));
        std::printf("  %-10s Kp %.3f Ti %.1f Td %.1f  Ms %.3f GM %.2f PM %.1f bruit %.3f depassement %.1f cout %.3f (derivee : %+.1f %%, bruit %.2f %%)\n",
                    std::string(goalName(static_cast<Goal>(g))).c_str(), p[g].gains.kp, p[g].gains.ti, p[g].gains.td, p[g].an.ms,
                    p[g].an.gm, p[g].an.pm, p[g].an.noiseOut, p[g].overshoot, p[g].cost, p[g].derivativeGain, p[g].derivativeNoise);
        check(p[g].ok && p[g].an.stable && p[g].an.ms <= l.ms + 0.01 && p[g].overshoot <= l.overshoot + 0.01 &&
                  p[g].an.noiseOut <= l.noise + 1e-9,
              std::string(goalName(static_cast<Goal>(g))) + " : dans ses limites");
    }
    check(near(p[1].gains.kp, 1.74, 0.12) && near(p[1].gains.ti, 109.0, 0.15),
          fmt("Equilibre : Kp %.3f (1,74), Ti %.1f (109)", p[1].gains.kp, p[1].gains.ti));
    check(p[0].gains.kp < p[1].gains.kp && p[1].gains.kp < p[2].gains.kp, "Robuste < Equilibre < Rapide (Kp)");
    check(p[0].cost > p[1].cost && p[1].cost > p[2].cost, "plus rapide, plus petit cout");
    check(costOf(m, {1.8, 30.0, 0.0}, c) > 1.3 * p[1].cost, "le reglage actuel coute plus qu'Equilibre");
    check(!p[1].withDerivative, fmt("pas de derivee (elle gagnerait %.1f %%, %.2f %% de bruit)", p[1].derivativeGain, p[1].derivativeNoise));
    check(ms < 1500.0, fmt("trois reglages en %.0f ms", ms));
    // sans bruit, la derivee aide un procede a retard
    Model slow = m;
    slow.theta = 60.0;
    slow.tau = 60.0;
    LoopContext quiet = c;
    quiet.noise = 0.0005;
    rq.model = slow;
    rq.ctx = quiet;
    const Proposal pd = tune(rq, Goal::Balanced);
    check(pd.ok && pd.withDerivative && pd.gains.td > 0, fmt("mesure propre, retard = tau : la derivee aide (Td %.1f s, +%.1f %%)", pd.gains.td, pd.derivativeGain));
    rq.derivative = Derivative::Never;
    const Proposal pn = tune(rq, Goal::Balanced);
    check(pn.ok && pn.gains.td == 0, "Derivee : jamais");
    rq.derivative = Derivative::IfHelps;
    rq.form = Form::Series;
    const Proposal ps = tune(rq, Goal::Balanced);
    check(ps.ok && seriesRepresentable(ps.gains), "bloc serie : Ti >= 4 Td");
}

// ---- sept jours d'un four ---------------------------------------------------------------------------
struct Oven {
    static constexpr double DT = 0.5, AMB = 20.0, T1 = 112.0, T2 = 9.0, TH0 = 12.0, NOISE = 0.05, QUANT = 0.1;
    static constexpr double DAY = 86400.0, TEND = 7 * DAY;
    static double kTrue(double t) { return 1.52 + 0.12 * t / TEND; }
    struct Ev { double t, v; };
    struct Load { double s, e, a; };
    std::vector<Ev> sp;
    std::vector<Load> loads;
    double manStart = 4 * DAY + 10.5 * 3600, manEnd = 4 * DAY + 10.5 * 3600 + 1200, manStep = 8.0;
    Rng rng{11};

    Oven() {
        double t = 0, v = 65;
        const double vals[4] = {60, 65, 70, 75};
        while (t < TEND - 3 * 3600) {
            t += rng.uniform(2, 6) * 3600;
            double nv;
            do nv = vals[rng.pick(4)]; while (nv == v);
            v = nv;
            if (t < TEND - 3 * 3600) sp.push_back({t, v});
        }
        const double t0 = TEND - 1800;
        sp.push_back({t0 - 3600, 60.0});
        sp.push_back({t0 + 120, 70.0});
        t = 0;
        while (t < TEND - 2 * 3600) {
            t += rng.uniform(1, 3) * 3600;
            const double a = (rng.uniform() < 0.5 ? -1 : 1) * rng.uniform(2, 6);
            const double e = t + rng.uniform(600, 2400);
            if (e < TEND - 3600) loads.push_back({t, e, a});
        }
        loads.push_back({t0 + 1000, TEND + 1, -4.0});
    }
    double loadAt(double t) const {
        double s = 0;
        for (const Load& l : loads) if (l.s <= t && t < l.e) s += l.a;
        return s;
    }
};

struct OvenRun {
    // l'etat du procede et du regulateur (le PI du four, borne, sans a-coup au retour en AUTO)
    Oven&  ov;
    double kp, ti, td;
    double x1, x2, I, D{0}, pvp;
    std::vector<double> buf;
    std::size_t k{0};
    std::size_t spIdx{0};
    double sp{65};
    double manBase{0};
    bool   wasAuto{true};
    double u{0};
    std::vector<double> loadEdges;
    double load{0};
    std::size_t loadIdx{0};
    std::vector<std::pair<double, double>> loadTable;

    OvenRun(Oven& o, double Kp, double Ti, double Td) : ov(o), kp(Kp), ti(Ti), td(Td) {
        const double u0 = (sp - Oven::AMB) / Oven::kTrue(0);
        x1 = x2 = Oven::kTrue(0) * u0;
        buf.assign(static_cast<std::size_t>(std::lround(Oven::TH0 / Oven::DT)), u0);
        I = u0;
        pvp = sp;
        u = u0;
        std::vector<double> edges;
        for (const auto& l : ov.loads) { edges.push_back(l.s); edges.push_back(l.e); }
        std::sort(edges.begin(), edges.end());
        for (double e : edges) loadTable.push_back({e, ov.loadAt(e)});
    }
    // un pas : la mesure (au dixieme) et la sortie a l'instant k*DT
    Sample step(bool forcePvFreeze = false, double frozenPv = 0) {
        const double t = static_cast<double>(k) * Oven::DT;
        const double K = Oven::kTrue(t);
        const double a1 = std::exp(-Oven::DT / Oven::T1), a2 = std::exp(-Oven::DT / Oven::T2);
        const std::size_t dl = buf.size();
        const double uin = buf[k % dl];
        x1 = a1 * x1 + (1 - a1) * K * uin;
        x2 = a2 * x2 + (1 - a2) * x1;
        double pvm = std::round((Oven::AMB + x2 + ov.rng.gauss() * Oven::NOISE) / Oven::QUANT) * Oven::QUANT;
        if (forcePvFreeze) pvm = frozenPv;
        while (spIdx < ov.sp.size() && ov.sp[spIdx].t <= t) sp = ov.sp[spIdx++].v;
        while (loadIdx < loadTable.size() && loadTable[loadIdx].first <= t) load = loadTable[loadIdx++].second;
        const double e = sp - pvm;
        const bool autoMode = !(ov.manStart <= t && t < ov.manEnd);
        if (autoMode) {
            const double P = kp * e;
            if (td > 0) {
                const double Tf = td / 10.0;
                D = (Tf / (Tf + Oven::DT)) * D - (kp * td / (Tf + Oven::DT)) * (pvm - pvp);
            }
            u = P + I + D;
            if (u > 100) { u = 100; if (e < 0) I += kp * Oven::DT / ti * e; }
            else if (u < 0) { u = 0; if (e > 0) I += kp * Oven::DT / ti * e; }
            else I += kp * Oven::DT / ti * e;
        } else {
            if (wasAuto) manBase = u;
            u = manBase + ov.manStep;
            I = u - kp * e;
            D = 0;
        }
        wasAuto = autoMode;
        pvp = pvm;
        buf[k % dl] = u + load;
        ++k;
        return Sample{t, sp, pvm, u, autoMode};
    }
};

int countEv(const Learner& l, EventKind k, Verdict v) {
    int n = 0;
    for (const Event& e : l.events()) if (e.kind == k && e.verdict == v) ++n;
    return n;
}
int countReason(const Learner& l, Reason r) {
    int n = 0;
    for (const Event& e : l.events()) if (e.reason == r) ++n;
    return n;
}

void septJours() {
    Oven ov;
    OvenRun run(ov, 1.8, 30.0, 0.0);
    Config cfg;
    cfg.period = 0.5;
    cfg.outMin = 0;
    cfg.outMax = 100;
    cfg.pvSpan = 100;
    cfg.tolerance = 1.0;
    Learner l(cfg);
    const std::size_t n = static_cast<std::size_t>(Oven::TEND / Oven::DT);
    const auto t0 = std::chrono::steady_clock::now();
    const Gains cur{1.8, 30.0, 0.0};
    double worst = 0, worstAt = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const Sample s = run.step();
        const auto a = std::chrono::steady_clock::now();
        l.push(s, cur);
        const double d = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
        if (d > worst) { worst = d; worstAt = s.t; }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  sept jours (%zu mesures) en %.2f s ; la mesure la plus longue : %.1f ms (a %.0f s)\n", n, secs, worst, worstAt);
    check(secs < 30.0, fmt("sept jours en %.2f s", secs));
    check(worst < 250.0, fmt("une mesure (avec l'apprentissage) en %.0f ms au plus", worst));

    const Model& m = l.model();
    const double Kend = Oven::kTrue(Oven::TEND);
    std::printf("  modele : K %.4f +/- %.4f, tau %.2f +/- %.2f, theta %.2f +/- %.2f, fit %.1f %%, verifie %.1f %%, %d appris\n", m.k, m.kHalf,
                m.tau, m.tauHalf, m.theta, m.thetaHalf, m.fitLearn, m.fitCheck, m.learned);
    std::printf("  (tau2 %.2f +/- %.2f)\n", m.tau2, m.tau2Half);
    check(m.valid && m.learned >= 5, fmt("le modele est appris (%.0f echelons)", m.learned));
    check(near(m.k, Kend, 0.04), fmt("K %.3f (le vrai gain en fin de semaine : %.3f)", m.k, Kend));
    check(m.order() == 2 && near(m.tau, 112.0, 0.06) && near(m.tau2, 9.0, 0.35) && near(m.theta, 12.0, 0, 2.0),
          fmt("deux constantes de temps et un retard : tau %.1f (112), tau2 %.1f (9), theta %.1f (12)", m.tau, m.tau2, m.theta));
    check(m.fitLearn > 94, fmt("colle a ce qu'il a appris : %.1f %% (le gain derive sur la semaine)", m.fitLearn));
    check(m.fitCheck > 93, fmt("sur l'echelon garde pour verifier : %.1f %%", m.fitCheck));
    check(m.kHalf > 0 && m.kHalf < 0.05 * m.k && m.tauHalf > 0 && m.thetaHalf > 0, "les intervalles a 95 %");
    check(l.sign() == 1, "sens : la mesure monte avec la sortie (une chauffe)");

    if (g_obs) {
        for (const Event& e : l.events())
            if (std::abs(e.t - ov.manStart) < 4 * 3600.0)
                std::printf("    [%+.0f s] type %d verdict %d raison %d a %.2f b %.2f fit %.1f fin %+.0f\n", e.t - ov.manStart,
                            static_cast<int>(e.kind), static_cast<int>(e.verdict), static_cast<int>(e.reason), e.a, e.b, e.fit, e.t2 - ov.manStart);
        for (const auto& ld : ov.loads)
            if (std::abs(ld.s - ov.manStart) < 4 * 3600.0) std::printf("    charge %+.0f s -> %+.0f s : %+.2f\n", ld.s - ov.manStart, ld.e - ov.manStart, ld.a);
    }
    const int learned = countEv(l, EventKind::SetpointStep, Verdict::Learned);
    const int checkN = countEv(l, EventKind::SetpointStep, Verdict::Check);
    const int discarded = countEv(l, EventKind::SetpointStep, Verdict::Discarded);
    const int dist = countReason(l, Reason::DisturbanceInProgress);
    const int manual = countEv(l, EventKind::ManualStep, Verdict::Learned) + countEv(l, EventKind::ManualStep, Verdict::Check);
    const int disturb = countEv(l, EventKind::Disturbance, Verdict::StatsOnly);
    std::printf("  consigne : %d appris, %d gardes, %d ecartes (%d perturbation en cours, %d mal expliques, %d trop courts) ; MANU %d ; perturbations vues %d (%zu reelles)\n",
                learned, checkN, discarded, dist, countReason(l, Reason::PoorFit), countReason(l, Reason::TooShort), manual, disturb,
                ov.loads.size());
    check(learned >= 8 && checkN >= 3, fmt("des echelons appris (%.0f) et gardes pour verifier (%.0f)", learned, checkN));
    check(dist >= 3, fmt("des echelons ecartes : une perturbation en cours (%.0f)", dist));
    check(manual == 1, "le passage en MANU appris (boucle ouverte)");
    check(disturb >= static_cast<int>(ov.loads.size()) / 3, fmt("des perturbations vues (%.0f sur %.0f)", disturb, static_cast<double>(ov.loads.size())));
    bool signOk = true;
    for (const Event& e : l.events()) {
        if (e.kind != EventKind::Disturbance) continue;
        double a = 0;
        for (const auto& ld : ov.loads) if (ld.s >= e.t - 60 && ld.s <= e.t + 60) a = ld.a;
        if (a != 0 && (e.b > 0) == (a > 0)) signOk = false;   // la sortie compense : de signe oppose a la charge
    }
    check(signOk, "la sortie compense chaque perturbation dans le bon sens");

    const WindowStats w30 = l.stats(1800), w8 = l.stats(8 * 3600), w24 = l.stats(86400), w7 = l.stats(7 * 86400);
    for (const auto* w : {&w30, &w8, &w24, &w7})
        std::printf("  fenetre %6.0f min : ecart %.3f, dans +/-1 %.1f %%, %d echelons, depassement %.1f %%, reponse %.0f s, course %.1f %%/h, inversions %.2f/h, saturee %.3f %%, AUTO %.2f %%, Harris %.3f, %d perturbations\n",
                    w->covered / 60.0, w->meanAbs, w->within, w->steps, w->overshoot, w->settle, w->travel, w->reversals, w->saturated,
                    w->autoPct, w->harris, w->disturbances);
    check(near(w30.covered, 1800, 0.05), "30 min couvertes");
    check(w30.within > 70 && w30.within < 95 && w7.within > 94, fmt("dans +/-1 : %.1f %% sur 30 min, %.1f %% sur 7 jours", w30.within, w7.within));
    check(w30.steps == 1 && w30.overshoot > 55 && w30.overshoot < 80, fmt("30 min : l'echelon de 14:02 depasse de %.1f %% (~66)", w30.overshoot));
    check(w7.steps >= 30 && w7.overshoot > 55 && w7.overshoot < 85, fmt("7 jours : %.0f echelons, %.1f %% de depassement moyen", w7.steps, w7.overshoot));
    check(w7.settle > 300 && w7.settle < 600, fmt("temps de reponse moyen %.0f s (~470)", w7.settle));
    check(w30.travel > w7.travel && w7.travel > 20, fmt("course de la sortie : %.0f %%/h sur 30 min, %.0f sur 7 jours", w30.travel, w7.travel));
    check(w30.reversals > w7.reversals, "plus d'inversions sur la derniere demi-heure (l'echelon)");
    check(w7.autoPct > 99.5 && w7.autoPct < 99.95, fmt("en AUTO %.2f %% (le MANU de 20 min)", w7.autoPct));
    check(w8.harris > 0.05 && w8.harris < 0.9 && w7.harris > 0.05, fmt("indice de Harris %.2f (8 h), %.2f (7 jours)", w8.harris, w7.harris));
    double hs = 0;
    for (double v : w30.hist) hs += v;
    check(near(hs, 100.0, 1e-6), "la repartition de l'ecart fait 100 %");

    const Oscillation o = l.oscillation(1800);
    check(o.found && !o.sustained && o.period > 150 && o.period < 190 && o.decay > 0.05 && o.decay < 0.5,
          fmt("oscillation : periode %.0f s (~170), chaque vague %.0f %% de la precedente", o.period, o.decay * 100));
    check(l.state() == State::Oscillating, "etat : Oscille");

    const auto dg = l.dailyGains();
    check(dg.size() >= 5 && dg.front().second < dg.back().second,
          fmt("le gain jour apres jour monte : %.3f -> %.3f", dg.empty() ? 0 : dg.front().second, dg.empty() ? 0 : dg.back().second));

    const auto bc = l.blockCheck();
    check(bc && near(bc->seen.kp, 1.8, 0.1) && near(bc->seen.ti, 30.0, 0.15) && bc->r2 > 0.8,
          fmt("la loi du bloc relevee : Kp %.3f Ti %.1f (1,8 ; 30), R2 %.3f", bc ? bc->seen.kp : 0, bc ? bc->seen.ti : 0, bc ? bc->r2 : 0));

    const Proposal& pb = l.proposal(Goal::Balanced);
    std::printf("  propose Equilibre : Kp %.3f Ti %.1f Td %.1f, Ms %.3f, bruit %.3f, depassement %.1f\n", pb.gains.kp, pb.gains.ti, pb.gains.td,
                pb.an.ms, pb.an.noiseOut, pb.overshoot);
    check(pb.ok && pb.gains.kp > 1.2 && pb.gains.kp < 2.6 && pb.gains.ti > 50 && pb.gains.ti < 130,
          fmt("Equilibre propose : Kp %.2f Ti %.0f s", pb.gains.kp, pb.gains.ti));
    const LoopContext lc = l.context();
    std::printf("  le cout : %.1f par echelon de 1, %.1f par perturbation de 1 %% (par jour)\n", lc.spWeight, lc.loadWeight);
    check(lc.spWeight > 30 && lc.spWeight < 120 && lc.loadWeight > 40 && lc.loadWeight < 200,
          "le cout pese ce qu'il a vu : ~6 echelons de 10 et ~22 perturbations de 4 % par jour");
    check(near(l.noise(), std::sqrt(0.05 * 0.05 + 0.1 * 0.1 / 12.0), 0.2), fmt("le bruit de mesure : %.4f (0,058)", l.noise()));
    check(near(l.resolution(), 0.1, 1e-3), fmt("la resolution : %.4f", l.resolution()));
    check(near(l.typicalStep(), 10.0, 0.5), fmt("l'echelon habituel %.1f", l.typicalStep()));

    // la verification sur le vrai procede (que l'objet ne connait pas) : un echelon 60 -> 70 avec le
    // reglage propose, puis avec l'actuel
    auto trueStep = [&](const Gains& g) {
        Oven o2;
        o2.sp = {{30.0, 70.0}};
        o2.loads.clear();
        o2.manStart = o2.manEnd = -1;
        OvenRun r2(o2, g.kp, g.ti, g.td);
        r2.sp = 60;
        const double u0 = (60.0 - Oven::AMB) / Oven::kTrue(Oven::TEND);
        r2.x1 = r2.x2 = Oven::kTrue(Oven::TEND) * u0;
        std::fill(r2.buf.begin(), r2.buf.end(), u0);
        r2.I = u0;
        r2.pvp = 60;
        r2.k = static_cast<std::size_t>(Oven::TEND / Oven::DT);   // le gain de la fin de semaine
        double peak = 0;
        for (int i = 0; i < 2400; ++i) {
            Sample s = r2.step();
            if (s.t - Oven::TEND >= 30.0) peak = std::max(peak, s.pv);
        }
        return (peak - 70.0) / 10.0 * 100.0;
    };
    const double ovProp = trueStep(pb.gains), ovCur = trueStep(cur);
    check(ovProp <= 12.0 && ovCur > 50, fmt("sur le vrai procede : %.1f %% de depassement avec le reglage propose, %.1f %% avec l'actuel", ovProp, ovCur));
    check(std::abs(ovProp - pb.overshoot) < 2.0, fmt("prevu %.1f %%, mesure sur le vrai procede %.1f %%", pb.overshoot, ovProp));

    // la memoire relue
    const std::string blob = l.save();
    Learner l2(cfg);
    check(l2.load(blob), fmt("la memoire relue (%.0f ko)", static_cast<double>(blob.size()) / 1024.0));
    check(l2.model().valid && near(l2.model().k, m.k, 1e-12) && l2.events().size() == l.events().size() &&
              near(l2.stats(7 * 86400).within, w7.within, 1e-3),
          "relue : le meme modele, les memes evenements, les memes statistiques");
    check(!l2.load(blob.substr(0, blob.size() / 2)) && l2.model().valid, "une memoire tronquee est refusee, rien ne change");
    // la suite apres l'arret : un trou d'une heure, puis les mesures reprennent
    OvenRun& r = run;
    for (int i = 0; i < 7200; ++i) r.step();   // une heure sans l'objet
    for (int i = 0; i < 1200; ++i) l2.push(r.step(), cur);
    check(countEv(l2, EventKind::Gap, Verdict::Info) + countEv(l2, EventKind::Gap, Verdict::Discarded) >= 1, "l'arret : un trou dans le Journal");
    check(l2.model().valid, "le modele tient apres l'arret");

    // Appliquer : le Journal le garde, le prochain echelon le verifie
    Learner l3 = l;
    l3.noteApplied(l3.lastT() + 1, cur, pb.gains, pb.overshoot);
    bool applied = false;
    for (const Event& e : l3.events()) if (e.kind == EventKind::Applied && e.before == cur && e.after == pb.gains) applied = true;
    check(applied && l3.currentGains() && *l3.currentGains() == pb.gains, "Appliquer : le reglage d'avant et le nouveau dans le Journal");
}

// ---- un trou, une mesure figee, la saturation ---------------------------------------------------------
void incidents() {
    Oven ov;
    ov.loads.clear();
    ov.sp = {{3000.0, 70.0}, {9000.0, 60.0}};
    ov.manStart = ov.manEnd = -1;
    OvenRun run(ov, 1.8, 30.0, 0.0);
    Config cfg;
    Learner l(cfg);
    const Gains cur{1.8, 30.0, 0.0};
    for (int i = 0; i < 5000; ++i) l.push(run.step(), cur);            // 2500 s
    for (int i = 0; i < 28; ++i) run.step();                           // 14 s sans donnees
    for (int i = 0; i < 4000; ++i) l.push(run.step(), cur);            // l'echelon de 3000 s, coupe ?
    int gaps = 0;
    for (const Event& e : l.events()) if (e.kind == EventKind::Gap && near(e.t2 - e.t, 14.5, 0.1)) ++gaps;
    check(gaps == 1, "un trou de 14 s dans le Journal");
    // la mesure figee : 6 min sans bouger pendant que la sortie bouge (l'echelon de 9000 s)
    while (run.k * Oven::DT < 9000.0) l.push(run.step(), cur);
    const double frozen = l.recent().back().pv;                        // la derniere valeur reste
    for (int i = 0; i < 720; ++i) l.push(run.step(true, frozen), cur);
    for (int i = 0; i < 2400; ++i) l.push(run.step(), cur);
    bool fr = false, disc = false;
    for (const Event& e : l.events()) {
        if (e.kind == EventKind::Frozen && e.t2 > e.t) fr = true;
        if (e.kind == EventKind::SetpointStep && e.reason == Reason::Frozen) disc = true;
    }
    check(fr && disc, "la mesure figee : vue, et l'echelon ecarte");

    // la saturation : une consigne inatteignable
    Oven o2;
    o2.loads.clear();
    o2.sp = {{600.0, 200.0}};
    o2.manStart = o2.manEnd = -1;
    OvenRun r2(o2, 1.8, 30.0, 0.0);
    Learner ls(cfg);
    for (int i = 0; i < 2400; ++i) ls.push(r2.step(), cur);
    check(ls.saturatedFor() > 300 && ls.state() == State::Saturated, fmt("saturee depuis %.0f s : etat Saturee", ls.saturatedFor()));
    bool satEv = false;
    for (const Event& e : ls.events()) if (e.kind == EventKind::Saturation) satEv = true;
    check(satEv, "la saturation dans le Journal");
    // 1.12.24 : relancee (la memoire relue), deux minutes plus tard, toujours en butee : la saturation compte
    // depuis son debut, des la premiere mesure ; un seul evenement, rouvert.
    {
        const double before = ls.saturatedFor();
        Learner lr(cfg);
        check(lr.load(ls.save()), "la memoire saturee relue");
        for (int i = 0; i < 240; ++i) r2.step();                       // 2 min sans le poste
        lr.push(r2.step(), cur);
        int open = 0, total = 0;
        for (const Event& e : lr.events())
            if (e.kind == EventKind::Saturation) { ++total; open += e.t2 == 0 ? 1 : 0; }
        check(lr.saturatedFor() > before + 100 && lr.state() == State::Saturated && total == 1 && open == 1,
              fmt("relancee 2 min plus tard : saturee depuis %.0f s, un evenement (%d), en cours", lr.saturatedFor(), total));
    }
    // 1.12.24 : passee en MANU, la sortie toujours a 100 % : c'est l'operateur qui la tient, pas une saturation.
    {
        Learner lm(cfg);
        check(lm.load(ls.save()), "la memoire saturee relue (MANU)");
        for (int i = 0; i < 120; ++i) {
            Sample s = r2.step();
            s.autoMode = false;
            lm.push(s, cur);
        }
        bool closed = false;
        for (const Event& e : lm.events()) closed = closed || (e.kind == EventKind::Saturation && e.t2 > 0);
        check(lm.saturatedFor() == 0 && lm.state() == State::Manual && closed,
              fmt("en MANU a %.0f %% : En MANU, pas Saturee ; la saturation close dans le Journal", lm.recent().back().out));
    }
    // Oublier et reapprendre
    l.forget();
    check(!l.model().valid && l.model().learned == 0, "Oublier : plus de modele");
    check(durationText(170) == "2 min 50 s" && durationText(14) == "14 s" && durationText(2.5) == "2,5 s" && durationText(3900) == "1 h 05",
          "les durees : 2 min 50 s, 14 s, 2,5 s, 1 h 05");
}

} // namespace

int main() {
    formes();
    identification();
    analyse();
    septJours();
    incidents();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
