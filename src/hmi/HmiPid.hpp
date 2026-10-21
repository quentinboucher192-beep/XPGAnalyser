// =============================================================================
//  hmi/HmiPid.hpp - 1.12.24 : le Reglage PID, ce qui se calcule sans ecran
// -----------------------------------------------------------------------------
//  L'OBJET REGARDE UNE BOUCLE : la consigne (SP), la mesure (PV), la sortie
//  (OUT) et le mode (AUTO ou MANU), toutes les `period` secondes. De ce qu'il
//  voit, il apprend le procede, en tire des statistiques et propose un reglage.
//
//    Le procede       un premier ordre avec retard (PV = K . OUT retardee de
//                     theta, lissee par tau), ou deux constantes de temps et un
//                     retard quand la seconde se voit. Appris sur les changements de
//                     consigne et les mouvements de la sortie en MANU (la sortie
//                     mesuree est l'entree du modele : la boucle peut etre
//                     fermee), sur plusieurs echelons a la fois, le plus recent
//                     comptant le plus (l'oubli). Un echelon sur trois est garde
//                     de cote pour verifier le modele. Ecartes : une
//                     perturbation en cours, la sortie saturee, un trou de
//                     donnees, une mesure figee, une reponse que le modele
//                     n'explique pas.
//    Les statistiques par minute (7 jours), les echelons (depassement, temps de
//                     reponse), les perturbations (ce que la sortie a compense),
//                     l'oscillation, la course et les inversions de la sortie,
//                     l'indice de Harris par heure.
//    Le reglage       pour trois objectifs (Robuste, Equilibre, Rapide) : le plus
//                     rapide pour la consigne et les perturbations (IAE) qui
//                     tient dans les limites - robustesse Ms, depassement, bruit
//                     envoye a la sortie -, la derivee seulement si elle aide.
//    Le bloc          standard (ISA), parallele (Kp, Ki, Kd) ou serie ; Ti et Td
//                     en secondes, minutes ou millisecondes ; Kp physique ou
//                     normalise sur les echelles. Et ce que le bloc fait vraiment
//                     (sa loi relevee sur les donnees, comparee a ce qu'il dit).
//
//  Le dessin, la popup et l'ecriture dans les variables : HmiRuntime et
//  l'editeur. Ici tout se teste sans ecran (tests/hmi_pid_test.cpp rejoue sept
//  jours d'un four simule, deux constantes de temps et un retard, avec bruit et
//  mesure au dixieme).
// =============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::pid {

// ---- le bloc PID ---------------------------------------------------------------
enum class Form : std::uint8_t { Standard, Parallel, Series };
[[nodiscard]] std::string_view formName(Form) noexcept;          // "Standard", "Parallele", "Serie"
// Ce que dit la variable de la forme : 0/1/2, ou un nom (STANDARD, ISA, MIXTE,
// IDEAL, PARALLELE, PARALLEL, SERIE, SERIES, INTERACTIVE...). nullopt : illisible.
[[nodiscard]] std::optional<Form> parseForm(std::string_view) noexcept;

// Un reglage, toujours en forme standard : u = Kp (e + 1/Ti . integrale de e + Td de/dt),
// Ti et Td en secondes ; ti <= 0 : sans integrale, td <= 0 : sans derivee.
struct Gains {
    double kp{0}, ti{0}, td{0};
    [[nodiscard]] bool operator==(const Gains&) const = default;
};
// Les unites du bloc : Ti et Td en `timeSeconds` secondes (60 : en minutes) ; le Kp du bloc vaut
// `kpScale` fois le Kp physique (en sortie par unite de mesure).
struct BlockUnits {
    double timeSeconds{1};
    double kpScale{1};
};
// Les trois nombres du bloc : (Kp, Ti, Td) standard, (Kc, Ti, Td) serie, (Kp, Ki, Kd) parallele
// (Ki = Kp/Ti par unite de temps, Kd = Kp.Td).
struct BlockValues {
    double p{0}, i{0}, d{0};
};
[[nodiscard]] BlockValues          toBlock(const Gains&, Form, const BlockUnits& = {}) noexcept;
[[nodiscard]] std::optional<Gains> fromBlock(const BlockValues&, Form, const BlockUnits& = {}) noexcept;
// Un reglage standard s'ecrit en serie si Ti >= 4 Td.
[[nodiscard]] bool seriesRepresentable(const Gains&) noexcept;

// ---- le procede appris ------------------------------------------------------------
//  K e^(-theta s) / ((tau s + 1)(tau2 s + 1)) ; tau2 = 0 : un premier ordre avec retard.
struct Model {
    double k{0}, tau{0}, theta{0};             // PV par unite de sortie ; s ; s
    double tau2{0};                            // la seconde constante de temps (0 : aucune)
    double kHalf{0}, tauHalf{0}, thetaHalf{0}; // demi-largeurs a 95 %
    double tau2Half{0};
    double fitLearn{-1};                       // % : colle a ce qu'il a appris
    double fitCheck{-1};                       // % : sur l'echelon garde pour verifier (-1 : aucun)
    double checkAt{0};                         // l'instant de cet echelon
    double seen{0};                            // la plus longue reponse vue (s)
    int    learned{0};                         // les echelons appris
    bool   valid{false};
    // Un tau tres grand devant ce qu'il a vu : un integrateur (un niveau) ; K/tau compte seul.
    [[nodiscard]] bool integrating() const noexcept { return valid && seen > 0 && tau > 8 * seen; }
    [[nodiscard]] int  order() const noexcept { return tau2 > 0 ? 2 : 1; }
};

// ---- l'analyse d'une boucle : le modele et un reglage --------------------------------
struct LoopContext {
    double period{0.5};       // la periode du regulateur (s) : celle des mesures
    double noise{0};          // l'ecart type du bruit de mesure (unites de PV)
    double derivFilter{10};   // N : la derivee filtree par Td/N
    // Le cout d'un reglage : spWeight . IAE(echelon de consigne de 1) + loadWeight . IAE(perturbation
    // de 1 a l'entree). Ce que la boucle voit par jour (echelons fois leur taille, perturbations fois
    // la leur) : l'ecart cumule d'une journee comme celles qu'il a vues. <= 0 : poids par defaut.
    double spWeight{0}, loadWeight{0};
};
struct Analysis {
    bool   stable{false};
    double ms{0}, mt{0};      // max |S|, max |T|
    double gm{0};             // marge de gain (fois) ; 0 : infinie
    double pm{0};             // marge de phase (degres) ; 0 : sans passage a 1
    double noiseOut{0};       // ecart type de la sortie du au bruit de mesure
    double wc{0};             // pulsation de coupure (rad/s)
};
[[nodiscard]] Analysis analyze(const Model&, const Gains&, const LoopContext&);

// ---- la reponse prevue ----------------------------------------------------------------
//  Un echelon de consigne puis une perturbation a l'entree du procede (en unites
//  de sortie), sur le modele, avec les bornes de la sortie (anti-emballement).
struct Scenario {
    double sp0{0}, step{1};
    double u0{50};                    // la sortie au repos
    double outMin{0}, outMax{100};
    double load{-5};
    double stepAt{-1}, loadAt{-1}, duration{-1};   // < 0 : d'apres le modele
};
struct Trace {
    double dt{0};
    double stepAt{0}, loadAt{0};
    double sp0{0}, sp1{0};
    std::vector<float> pv, out;
};
struct Metrics {
    double overshoot{0};        // % de l'echelon
    double settle{-1};          // s apres l'echelon pour rester dans la bande ; -1 : pas pose
    double rise{-1};            // 10 a 90 % (s)
    double loadDev{0};          // ecart maximal sous la perturbation
    double loadRecover{-1};     // s pour revenir dans la bande ; -1 : pas revenu
    double iaeSp{0}, iaeLoad{0};
};
[[nodiscard]] Trace   simulate(const Model&, const Gains&, const Scenario&, const LoopContext&);
[[nodiscard]] Metrics measure(const Trace&, double band);

// ---- le reglage optimise ----------------------------------------------------------------
enum class Goal : std::uint8_t { Robust, Balanced, Fast };
enum class Derivative : std::uint8_t { IfHelps, Never, Always };
struct Limits {
    double ms{1.6};             // robustesse : max |S| (et |T|)
    double overshoot{10};       // % d'un echelon de consigne
    double noise{0.3};          // % de l'etendue de la sortie (ecart type)
};
[[nodiscard]] Limits      limitsOf(Goal) noexcept;
[[nodiscard]] std::string_view goalName(Goal) noexcept;   // "Robuste", "Equilibre", "Rapide"
struct TuneRequest {
    Model       model;
    LoopContext ctx;
    Derivative  derivative{Derivative::IfHelps};
    Form        form{Form::Standard};
    double      outSpan{100};
};
struct Proposal {
    bool     ok{false};
    Goal     goal{Goal::Balanced};
    Gains    gains;
    Analysis an;
    double   overshoot{0};       // % (echelon unitaire, lineaire)
    double   cost{0};            // IAE consigne + perturbation, normalises
    bool     withDerivative{false};
    double   derivativeGain{0};  // ce que la meilleure derivee gagnait (%, > 0 : mieux)
    double   derivativeNoise{0}; // le bruit qu'elle mettait sur la sortie (% de l'etendue)
};
[[nodiscard]] Proposal tune(const TuneRequest&, Goal);
// Les trois objectifs d'un coup (les essais communs ne sont faits qu'une fois).
[[nodiscard]] std::array<Proposal, 3> tuneAll(const TuneRequest&);
// Le cout du reglage `g` sur le modele, dans les memes unites que Proposal::cost.
[[nodiscard]] double costOf(const Model&, const Gains&, const LoopContext&);

// ---- l'identification (exposee pour les essais) -----------------------------------------------
//  Un enregistrement : la sortie et la mesure a pas constant `dt`, les `pre`
//  premiers points avant le debut du mouvement (au repos).
struct Segment {
    double             t0{0}, dt{0.5};
    std::size_t        pre{0};
    std::vector<float> u, y;
    [[nodiscard]] double duration() const noexcept { return dt * static_cast<double>(u.size()); }
};
struct Fit {
    bool   ok{false};
    double k{0}, tau{0}, theta{0}, tau2{0};
    double kHalf{0}, tauHalf{0}, thetaHalf{0}, tau2Half{0};
    double sse1{0};       // le meilleur premier ordre (pour juger la seconde constante)
    double fit{-1};       // %
    double sse{0};
    double rho{0};        // l'autocorrelation des residus (pas 1)
};
// Le modele qui colle le mieux a tous les segments a la fois (un decalage par segment), chacun
// pese par `weights` (vide : 1) : le premier ordre avec retard, et deux constantes de temps si la
// seconde se voit (l'ecart quadratique baisse de 15 % au moins, la constante est bien determinee).
// `starts` : d'ou partir (le meilleur d'entre eux, a petits pas ; aucun : une grille). `secondOrder`
// faux : le premier ordre seul.
[[nodiscard]] Fit identify(const std::vector<const Segment*>&, const std::vector<double>& weights = {},
                           const std::vector<Fit>& starts = {}, bool secondOrder = true);
[[nodiscard]] Model modelOf(const Fit&);
// La mesure prevue par le modele sur un segment ; le decalage : la moyenne avant le mouvement.
[[nodiscard]] std::vector<double> predict(const Segment&, const Model&);
// % : 100 (1 - |y - prevu| / |y - moyenne|), le decalage pris avant le mouvement.
[[nodiscard]] double checkFit(const Segment&, const Model&);
// Le gain seul, les constantes de temps et le retard du modele tenus (le gain jour apres jour).
[[nodiscard]] double gainWith(const Segment&, const Model&);
// L'indice de Harris : la variance minimale atteignable avec ce retard (en pas de `e`) sur la
// variance de `e` ; 1 : le mieux possible. -1 : trop peu de points.
[[nodiscard]] double harrisIndex(const std::vector<double>& e, int delaySteps, int order = 20);

// ---- ce qu'il voit et ce qu'il en tire --------------------------------------------------------
struct Sample {
    double t{0};                 // s, horloge murale
    double sp{0}, pv{0}, out{0};
    bool   autoMode{true};
};
struct Config {
    double period{0.5};
    double outMin{0}, outMax{100};
    double pvSpan{0};            // l'etendue de la mesure (0 : inconnue)
    double tolerance{1};         // +/- autour de la consigne
    double memoryDays{7};
    double forgetHours{24};      // <= 0 : tous les echelons comptent pareil
    bool   learnAuto{true}, learnManual{true};
    int    action{0};            // 0 : detecte ; +1 inverse (la sortie monte quand la mesure baisse) ; -1 direct
    Goal       goal{Goal::Balanced};
    Derivative derivative{Derivative::IfHelps};
    Form       form{Form::Standard};
    [[nodiscard]] double outSpan() const noexcept { return outMax > outMin ? outMax - outMin : 100.0; }
    [[nodiscard]] bool operator==(const Config&) const = default;
};

enum class EventKind : std::uint8_t {
    SetpointStep,   // un changement de consigne
    ManualStep,     // la sortie bougee en MANU
    Disturbance,    // une perturbation : la sortie a compense
    Saturation,     // la sortie en butee
    Gap,            // un trou de donnees
    Frozen,         // la mesure figee
    GainsChanged,   // le reglage du bloc a change
    Applied,        // le reglage ecrit par l'objet
    Verified,       // apres un reglage : prevu et mesure
    ModelChanged,   // le procede a change
    Forgotten       // Oublier et reapprendre
};
enum class Verdict : std::uint8_t { Learned, Check, Discarded, StatsOnly, Info };
enum class Reason : std::uint8_t {
    None, Clean, OpenLoop, KeptForCheck,
    DisturbanceInProgress, Saturated, Gap, Frozen, PoorFit, TooSmall, TooShort, NotLearning
};
struct Event {
    double    t{0}, t2{0};       // debut, fin (0 : un instant)
    EventKind kind{EventKind::SetpointStep};
    Verdict   verdict{Verdict::Info};
    Reason    reason{Reason::None};
    double    a{0}, b{0};        // consigne de a a b ; sortie de a a b ; ecart max a, retour b...
    double    fit{-1};           // % (appris, verifie)
    double    since{0};          // une perturbation en cours : depuis quand
    Gains     before, after;     // GainsChanged, Applied, Verified (after : prevu/mesure)
    double    predicted{-1}, measured{-1};   // Verified : depassement prevu et mesure
};
struct StepRecord {
    double t{0};
    double from{0}, to{0};
    double overshoot{0};          // %
    double settle{-1};            // s ; -1 : pas pose dans la fenetre
    double band{0};
};
struct Oscillation {
    bool   found{false};
    bool   sustained{false};      // en regime etabli (pas une reponse qui s'amortit)
    double period{0};
    double decay{0};              // une vague sur la precedente
    double amplitude{0};
    double at{0};                 // la reponse d'ou il la tire
};
inline constexpr int kHistBins = 16;   // l'ecart de -4 a +4 tolerances, par demi-tolerance
struct WindowStats {
    double      covered{0};       // s de donnees dans la fenetre
    std::size_t samples{0};
    double      meanAbs{0}, sd{0}, within{0};   // en AUTO ; within : % du temps
    int         steps{0};
    double      overshoot{-1}, settle{-1};      // moyennes sur les echelons propres
    double      travel{0}, reversals{0};        // % de l'etendue par heure ; par heure
    double      saturated{0}, autoPct{0};       // % du temps
    double      harris{-1};
    int         disturbances{0};
    int         gaps{0};
    std::array<double, kHistBins> hist{};       // % du temps en AUTO
};
// Ce que le bloc fait vraiment : sa loi relevee sur les donnees (Kp, Ti, Td vus), la qualite du
// releve (R2) et ce qu'il dit (lu). nullopt : pas assez de mouvement pour le dire.
struct BlockCheck {
    Gains  seen;
    double r2{0};
    std::size_t samples{0};
};

enum class State : std::uint8_t { NoData, Learning, Healthy, Oscillating, Sluggish, Saturated, Manual, Frozen };

class Learner {
public:
    explicit Learner(const Config& = {});
    void configure(const Config&);
    [[nodiscard]] const Config& config() const noexcept { return cfg_; }

    // Une mesure. `gains` : le reglage lu dans le bloc (standard, secondes) ; nullopt : pas lie.
    void push(const Sample&, const std::optional<Gains>& gains = std::nullopt);
    // Oublier et reapprendre : le modele et ses echelons ; les statistiques restent.
    void forget();

    [[nodiscard]] const Model& model() const noexcept { return model_; }
    [[nodiscard]] int    sign() const noexcept { return model_.valid ? (model_.k >= 0 ? 1 : -1) : 0; }
    [[nodiscard]] double noise() const noexcept;         // ecart type du bruit de mesure
    [[nodiscard]] double resolution() const noexcept { return resolution_; }
    [[nodiscard]] double lastT() const noexcept { return lastT_; }
    [[nodiscard]] std::size_t sampleCount() const noexcept { return total_; }
    [[nodiscard]] const std::optional<Gains>& currentGains() const noexcept { return gains_; }
    [[nodiscard]] double typicalStep() const;             // l'echelon de consigne habituel
    [[nodiscard]] double outRest() const;                  // la sortie au repos
    [[nodiscard]] int    stepsSeen() const noexcept { return stepsSeen_; }
    [[nodiscard]] int    learnedCount() const noexcept { return model_.learned; }
    [[nodiscard]] double saturatedFor() const noexcept;   // s, en butee maintenant (0 : non)

    [[nodiscard]] WindowStats             stats(double window) const;
    [[nodiscard]] Oscillation             oscillation(double window) const;
    [[nodiscard]] std::vector<StepRecord> steps(double window) const;
    [[nodiscard]] const std::deque<Event>& events() const noexcept { return events_; }
    [[nodiscard]] std::vector<std::pair<double, double>> dailyGains() const;  // (debut du jour, K)
    [[nodiscard]] std::optional<BlockCheck> blockCheck() const;
    [[nodiscard]] State state() const;

    // Les points recents (les dernieres heures), pour les courbes.
    struct Point {
        double t{0};
        float  sp{0}, pv{0}, out{0};
        bool   autoMode{true};
    };
    [[nodiscard]] const std::deque<Point>& recent() const noexcept { return raw_; }
    // Une minute des 7 jours (les courbes longues).
    struct MinuteView {
        double t{0};
        float  sp{0}, pv{0}, pvMin{0}, pvMax{0}, out{0};
    };
    [[nodiscard]] std::vector<MinuteView> minutes(double window) const;
    // Le dernier echelon appris (mesure, modele, sortie), et celui garde pour verifier.
    struct SegmentView {
        double             t0{0}, dt{0}, onset{0};
        std::vector<float> y, yhat, u;
        double             fit{-1};
    };
    [[nodiscard]] std::optional<SegmentView> lastLearned() const;
    [[nodiscard]] std::optional<SegmentView> lastCheck() const;

    // Le reglage propose pour un objectif (garde tant que rien ne change).
    [[nodiscard]] const Proposal& proposal(Goal, Derivative = Derivative::IfHelps, Form = Form::Standard) const;
    [[nodiscard]] LoopContext context() const;

    // Le reglage ecrit par l'objet : le Journal le garde (Revenir), le prochain echelon le verifie.
    void noteApplied(double t, const Gains& before, const Gains& after, double predictedOvershoot);

    // La memoire sur disque : un bloc d'octets (versionne). Faux : illisible (rien n'est change).
    [[nodiscard]] std::string save() const;
    bool load(std::string_view);

private:
    struct Minute {
        std::int64_t  minute{0};
        std::uint32_t n{0}, nAuto{0}, nSat{0}, nWithin{0}, reversals{0};
        float sumAbsE{0}, sumE{0}, sumE2{0};
        float sp{0}, pv{0}, pvMin{0}, pvMax{0}, out{0};
        float travel{0};
        std::array<std::uint16_t, kHistBins> hist{};
    };
    struct MinuteAcc {
        std::int64_t  minute{-1};
        std::uint32_t n{0}, nAuto{0}, nSat{0}, nWithin{0}, reversals{0};
        double sumAbsE{0}, sumE{0}, sumE2{0}, sp{0}, pv{0}, pvMin{0}, pvMax{0}, out{0}, travel{0};
        std::array<std::uint32_t, kHistBins> hist{};
    };
    struct Stored {
        Segment seg;
        double  onset{0};
        Verdict verdict{Verdict::Learned};
        double  fit{-1};
    };
    // Un mouvement (consigne, ou sortie en MANU) : de son debut a son repos.
    struct Capture {
        bool        active{false};
        bool        learn{true};
        EventKind   kind{EventKind::SetpointStep};
        double      onset{0}, lastMove{0}, lastCheck{0};
        double      spFrom{0}, spTo{0}, outFrom{0}, outTo{0};
        bool        disturbed{false};
        double      disturbedSince{0};
        bool        gap{false}, frozen{false};
        std::size_t samples{0}, saturated{0};
    };
    struct Disturb {
        bool        active{false};
        double      start{0}, maxDev{0}, outBefore{0};
        std::size_t over{0};
        double      quietSince{0}, quietSum{0};
        std::size_t quietN{0};
    };
    struct StepWatch {
        bool   active{false};
        double t{0}, from{0}, to{0}, last{0};   // last : le dernier changement (une rampe)
    };
    struct HourIndex {
        std::int64_t hour{0};
        float        value{0}, minutes{0};
    };

    void   onGap(double from, double to);
    void   addRaw(const Sample&);
    void   addMinute(const Sample&, bool sat);
    void   flushMinute();
    void   trackOutput(double t, double out);
    void   watchSaturation(const Sample&, bool sat);
    void   watchFrozen(const Sample&, double prevPv);
    void   watchNoise(const Sample&);
    void   watchBlock(const Sample&, bool sat, double dt);
    void   watchSteps(const Sample&);
    void   finishStep(double end);
    void   watchDisturbance(const Sample&);
    void   watchExcitation(const Sample&);
    void   finishCapture(double end);
    void   evaluate(const Capture&, double end);
    void   refit(const Fit* hint);
    void   hourly(double t);
    void   prune(double t);
    void   addEvent(const Event&);
    void   invalidate() const;
    [[nodiscard]] bool   steadyBefore(double onset, double pre, bool autoMode) const;
    [[nodiscard]] bool   settledNow(double t, double window, double step) const;
    [[nodiscard]] double band() const;
    [[nodiscard]] double stepMin() const;
    [[nodiscard]] double outStepMin() const;
    [[nodiscard]] double settleBand(double step) const;
    [[nodiscard]] double prePeriod() const;
    [[nodiscard]] bool   extract(double from, double onset, double to, Segment& seg) const;
    [[nodiscard]] double ageWeight(double t) const;
    [[nodiscard]] double harrisOver(double from, double to) const;
    [[nodiscard]] SegmentView view(const Stored&) const;

    Config cfg_;
    Model  model_, modelRef_;
    Fit    fit_;
    std::optional<Gains> gains_;

    std::deque<Point>      raw_;
    std::deque<Minute>     minutes_;
    MinuteAcc              acc_;
    std::deque<Stored>     stored_;
    std::deque<Event>      events_;
    std::deque<StepRecord> stepRecords_;
    std::deque<HourIndex>  harris_;
    Capture   cap_;
    Disturb   dist_;
    StepWatch stepWatch_;

    double      lastT_{0};
    bool        started_{false};
    std::size_t total_{0};
    double spAnchor_{0}, spLeaveAt_{0};
    double outAnchor_{0}, outLeaveAt_{0};
    double prevSp_{0}, prevPv_{0}, prevOut_{0};
    bool   lastAuto_{true};
    double outSteady_{0};
    bool   outSteadyHave_{false};
    double resolution_{0};
    double noiseVar_{0}, noiseW_{0};
    double pv1_{0}, pv2_{0};
    int    pvHist_{0};
    double lastPvChange_{0}, frozenOutMin_{0}, frozenOutMax_{0};
    bool   frozen_{false};
    double satSince_{-1};
    bool   satEvent_{false};
    double blockSum_{0}, blockStart_{0}, blockPrev_{0}, revExt_{0};
    std::size_t blockN_{0};
    bool   blockHave_{false};
    int    revDir_{0};
    // la loi du bloc : les equations normales de du = a de + b e dt + c d2pv/dt (oubli sur 1 h)
    std::array<double, 9> bxx_{};
    std::array<double, 3> bxy_{};
    double byy_{0}, bn_{0};
    double be1_{0}, bpv1_{0}, bpv2_{0}, bout1_{0};
    int    bHist_{0};
    int    cleanSeen_{0};
    int    stepsSeen_{0};
    std::int64_t lastHour_{-1};
    double lastPrune_{0};
    bool   verifyPending_{false};
    double verifyAt_{0}, verifyPredicted_{-1};
    Gains  verifyGains_;

    struct Cached {
        std::uint32_t           key{0};
        std::array<Proposal, 3> p;
    };
    mutable std::vector<Cached> cache_;
    mutable double stateAt_{-1};
    mutable State  state_{State::NoData};
};

// L'ecriture d'une duree pour les textes (2 min 50 s, 1 h 05, 109 s...).
[[nodiscard]] std::string durationText(double seconds);

} // namespace hmi::pid
