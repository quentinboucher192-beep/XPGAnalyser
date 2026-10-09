#include "HmiRuntime.hpp"
#include "HmiActionKinds.hpp"   // 1.11.7 : Maths, le clavier virtuel
#include "../core/CallTrail.hpp"   // 1.10.2 (CR) : les scripts de l'IHM, dans le journal interne
#include "HmiObjectAlarms.hpp"
#include "HmiAlarmGroups.hpp"   // 1.10.2 (AL) : les reglages des groupes d'alarmes
#include "HmiSymbols.hpp"
#include "HmiTemplates.hpp"
#include "HmiWidgets.hpp"
#include "HmiCrypto.hpp"
#include "HmiLanguages.hpp"
#include "HmiPolicy.hpp"

#include "HmiScript.hpp"
#include "HmiDecl.hpp"        // 1.11.18 (refonte, lot 3) : les declarations du modele, reconstruites
#include "HmiOperators.hpp"   // 1.10 (integration) : les operateurs du projet (S2) a l'execution (S1)
#include "HmiEnums.hpp"       // 1.10 (decision 15) : les enumerations IHM dans les scripts (S1)
#include "HmiMarkers.hpp"     // 1.11 (REP) : les reperes $...$, transparents pour le calcul
#include "HmiApiVars.hpp"     // 1.11.1 (API-M) : API.<...> est le nom que l'automate connait
#include "HmiTypeRegistry.hpp" // 1.11.19 (refonte, lot 6) : le type du moteur d'un nom de type
#include "../sim/Interpreter.hpp"
#include "../sim/Runtime.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iterator>

namespace hmi {

namespace {

constexpr int         kMaxDepth = 32;    // 1.11.10 : 8 avant - les fonctions et operateurs s'appellent entre eux
constexpr std::size_t kJournalMax = 500;

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// "Acc\xC3\xA9l\xC3\xA9r\xC3\xA9" "e" -> "acceleree" : les courbes se comparent sans accents ni casse.
std::string plain(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            if ((d >= 0xA8 && d <= 0xAB) || (d >= 0x88 && d <= 0x8B)) out += 'e';
            else if ((d >= 0xA0 && d <= 0xA5) || (d >= 0x80 && d <= 0x85)) out += 'a';
            else if (d == 0xA7 || d == 0x87) out += 'c';
            else out += '?';
            ++i;
            continue;
        }
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

double bounceOut(double t) {
    const double n = 7.5625, d = 2.75;
    if (t < 1 / d) return n * t * t;
    if (t < 2 / d) { t -= 1.5 / d; return n * t * t + 0.75; }
    if (t < 2.5 / d) { t -= 2.25 / d; return n * t * t + 0.9375; }
    t -= 2.625 / d;
    return n * t * t + 0.984375;
}

// Une courbe de Bezier cubique (0,0) (x1,y1) (x2,y2) (1,1), comme en CSS :
// on cherche u tel que x(u) = t, et on rend y(u).
double cubicBezier(double x1, double y1, double x2, double y2, double t) {
    const auto bez = [](double a, double b, double u) {
        const double v = 1 - u;
        return 3 * v * v * u * a + 3 * v * u * u * b + u * u * u;
    };
    double lo = 0, hi = 1, u = t;
    for (int i = 0; i < 40; ++i) {
        const double x = bez(x1, x2, u);
        if (std::fabs(x - t) < 1e-6) break;
        if (x < t) lo = u; else hi = u;
        u = (lo + hi) / 2;
    }
    return bez(y1, y2, u);
}

double lerp(double a, double b, double p) { return a + (b - a) * p; }

// Lot 11 : un texte sans ses blancs de tete et de fin.
std::string trimText(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

} // namespace

double ease(std::string_view curve, double t) {
    t = std::clamp(t, 0.0, 1.0);
    const std::string c = plain(curve);
    if (c.rfind("bezier", 0) == 0) {
        double v[4] = {0.25, 0.1, 0.25, 1.0};
        const auto open = c.find('(');
        if (open != std::string::npos) {
            std::size_t at = open + 1;
            for (int k = 0; k < 4 && at < c.size(); ++k) {
                char* end = nullptr;
                v[k] = std::strtod(c.c_str() + at, &end);
                if (!end) break;
                at = static_cast<std::size_t>(end - c.c_str());
                while (at < c.size() && (c[at] == ',' || c[at] == ' ')) ++at;
            }
        }
        return cubicBezier(std::clamp(v[0], 0.0, 1.0), v[1], std::clamp(v[2], 0.0, 1.0), v[3], t);
    }
    if (c == "acceleree") return t * t * t;
    if (c == "deceleree") return 1 - std::pow(1 - t, 3);
    if (c == "douce") return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
    if (c == "rebond") return bounceOut(t);
    return t;   // lineaire
}

Frame incomingFrame(const Transition& tr, double p) {
    Frame f;
    const std::string dir = plain(tr.direction);
    switch (tr.kind) {
        case TransitionKind::Instant: break;
        case TransitionKind::Fade: f.opacity = std::clamp(p, 0.0, 1.0); break;
        case TransitionKind::Slide: {
            // La direction est celle du mouvement : "gauche" = tout part vers la
            // gauche, la nouvelle vue arrive par la droite.
            const double rest = 1 - p;
            if (dir == "droite") f.offsetX = -rest;
            else if (dir == "haut") f.offsetY = rest;
            else if (dir == "bas") f.offsetY = -rest;
            else f.offsetX = rest;
            break;
        }
        case TransitionKind::Zoom:
            f.opacity = std::clamp(p, 0.0, 1.0);
            f.scale = dir == "arriere" ? lerp(1.4, 1.0, p) : lerp(0.6, 1.0, p);
            break;
        case TransitionKind::Rotate:
            f.opacity = std::clamp(p, 0.0, 1.0);
            f.angle = (dir == "antihoraire" ? -90.0 : 90.0) * (1 - p);
            f.scale = lerp(0.6, 1.0, p);
            break;
        case TransitionKind::Custom:
            f.opacity = lerp(std::clamp(tr.fromOpacity, 0.0, 1.0), 1.0, std::clamp(p, 0.0, 1.0));
            f.offsetX = tr.fromOffsetX * (1 - p);
            f.offsetY = tr.fromOffsetY * (1 - p);
            f.scale = lerp(tr.fromScale, 1.0, p);
            f.angle = tr.fromAngle * (1 - p);
            break;
    }
    return f;
}

Frame outgoingFrame(const Transition& tr, double p) {
    Frame f;
    const std::string dir = plain(tr.direction);
    switch (tr.kind) {
        case TransitionKind::Instant: f.opacity = 0; break;
        case TransitionKind::Fade: f.opacity = std::clamp(1 - p, 0.0, 1.0); break;
        case TransitionKind::Slide:
            if (dir == "droite") f.offsetX = p;
            else if (dir == "haut") f.offsetY = -p;
            else if (dir == "bas") f.offsetY = p;
            else f.offsetX = -p;
            break;
        case TransitionKind::Zoom:
            f.opacity = std::clamp(1 - p, 0.0, 1.0);
            f.scale = dir == "arriere" ? lerp(1.0, 0.6, p) : lerp(1.0, 1.4, p);
            break;
        case TransitionKind::Rotate:
            f.opacity = std::clamp(1 - p, 0.0, 1.0);
            f.angle = (dir == "antihoraire" ? 90.0 : -90.0) * p;
            break;
        case TransitionKind::Custom:
            f.opacity = std::clamp(1 - p, 0.0, 1.0);
            break;
    }
    return f;
}

// Lot 8 : les fonctions standard (SEL, SIN, LIMIT, REAL_TO_INT...) sans automate :
// un simulateur sans programme les calcule - les memes que celles de l'automate.
static sim::Runtime& standardFunctions() {
    static sim::Runtime pure(std::make_shared<const domain::Project>());
    return pure;
}

// =========================================================== environnement ==
// Les variables IHM d'abord (sans casse, comme le ST), puis l'automate ; les
// fonctions IHM_... ici, les autres au simulateur.
class Runtime::Env final : public sim::Environment, public FunctionHost {
public:
    explicit Env(Runtime& rt) : rt_(rt) {}
    std::map<std::string, sim::Value, std::less<>> vars;
    std::vector<sim::Diagnostic> diagnostics;
    // Lot 7 : les variables locales du script (ou de la fonction) qui tourne.
    // Seul le cadre du dessus est vu : un script appele ne voit pas celles de
    // l'appelant.
    using Frame = std::map<std::string, sim::Value, std::less<>>;
    std::vector<Frame*> frames;
    // Lot 8 : les parametres de la vue dont le code tourne (Moteur -> Pompes[3]).
    // Les variables locales passent avant, les variables IHM et l'automate apres.
    const Scope* aliases{nullptr};
    // 1.11.17 : le texte a trous d'IHM_JOURNAL ou d'IHM_LOG se remplit : il lit aussi les
    // locales du code qui l'appelle (VAR, VAR_TEMP, parametres : sim::readCallerLocal).
    // Avant, '{Mini:0.0}' dans un script qui declare Mini rendait ###.
    int callerLocals{0};
    struct CallerLocals {
        Env& env;
        explicit CallerLocals(Env& e) : env(e) { ++env.callerLocals; }
        ~CallerLocals() { --env.callerLocals; }
        CallerLocals(const CallerLocals&) = delete;
        CallerLocals& operator=(const CallerLocals&) = delete;
    };

    [[nodiscard]] std::string resolved(std::string_view n) const { return aliases ? aliases->resolve(n) : std::string(n); }
    // 1.11.1 (API-M) : API.<globale>, API.<Unite>.<variable> - le nom que
    // l'automate (simule ou reel) connait deja : sans le prefixe. Une variable
    // IHM, une vue nommee API gardent leur sens : elles passent avant.
    [[nodiscard]] static std::string plcOf(std::string r) { return apivars::isApiPath(r) ? apivars::stripApi(r) : r; }

    bool read(std::string_view n, sim::Value& out) override {
        if (!frames.empty())
            if (const auto it = frames.back()->find(upper(n)); it != frames.back()->end()) { out = it->second; return true; }
        if (callerLocals > 0 && sim::readCallerLocal(n, out)) return true;     // 1.11.17
        // 1.10 (decision 15) : un litteral d'enumeration T_MODE#Auto (ou T_MODE#1) : sa valeur, un DINT.
        if (n.find('#') != std::string_view::npos && rt_.project_) {
            const HmiType* type = nullptr;
            const HmiEnumValue* value = nullptr;
            if (parseEnumLiteral(*rt_.project_, n, &type, &value) && value) {
                out = sim::Value::integer(sim::Type::DInt, value->value);
                return true;
            }
            // 1.11.14 : NIVEAU_LOG#INFO, le niveau de IHM_LOG.
            if (const auto level = logLevelByName(n)) {
                out = sim::Value::integer(sim::Type::DInt, static_cast<long long>(*level));
                return true;
            }
            return false;
        }
        if (aliases)
            if (const auto* v = aliases->value(n)) { out = *v; return true; }
        const std::string r = resolved(n);
        // 1.9 : un parametre Copie / Les deux - la copie de la popup, jamais l'original.
        if (!r.empty() && r[0] == '$') return rt_.copyRead(r, out);
        if (const auto it = vars.find(upper(r)); it != vars.end()) {
            // Lot 15 : liee a un equipement, elle se relit sur lui - sauf forcee (1.11.5) : elle garde sa valeur.
            if (!rt_.bound_.empty() && !rt_.forcedIhm_.count(it->first)) rt_.readBound(it->first, it->second);
            out = it->second;
            return true;
        }
        // Lot 16 : Consignes.Length, Four1.Words ; un indice hors des bornes d'un
        // tableau IHM rend la valeur par defaut (et le journal le dit).
        if (!rt_.aggregates_.empty() && rt_.aggregateRead(r, out)) return true;
        // Lot 9 : SYS.UserName, Vue.Objet.Propriete, Vue.Open.
        if (r.find('.') != std::string::npos && rt_.publicRead(r, out)) return true;
        if (rt_.plc_ && rt_.plc_->read(plcOf(r), out)) return true;
        // 1.11.14 : INFO, ERROR... - les niveaux de IHM_LOG, en dernier : une vraie
        // variable du meme nom passe avant.
        if (const auto level = logLevelByName(n); level && n.find('.') == std::string_view::npos) {
            out = sim::Value::integer(sim::Type::DInt, static_cast<long long>(*level));
            return true;
        }
        return false;
    }
    bool write(std::string_view n, const sim::Value& v) override {
        if (!frames.empty())
            if (const auto it = frames.back()->find(upper(n)); it != frames.back()->end()) { it->second.assignFrom(v); return true; }
        if (aliases && aliases->value(n)) {
            diagnostics.push_back({sim::Diagnostic::Severity::Error,
                                   "'" + std::string(n) + "' est un param\xC3\xA8tre pass\xC3\xA9 par valeur : en lecture seule", 0, {}});
            return false;
        }
        const std::string r = resolved(n);
        // Une fonction appelee par une expression de vue calcule, elle n'ecrit
        // rien au-dehors (une expression ne modifie jamais le procede).
        if (rt_.readOnly_ > 0) {
            diagnostics.push_back({sim::Diagnostic::Severity::Error,
                                   "'" + std::string(n) + "' : une fonction appel\xC3\xA9" "e par une expression de vue n'\xC3\xA9" "crit que ses variables locales",
                                   0, {}});
            return false;
        }
        // 1.9 : un parametre Copie / Les deux s'ecrit dans la copie de la popup
        // (rien ne part vers l'automate ni vers les variables IHM).
        if (!r.empty() && r[0] == '$') {
            std::string why;
            if (rt_.copyWrite(r, v, &why) == 1) return true;
            diagnostics.push_back({sim::Diagnostic::Severity::Error, why, 0, {}});
            return false;
        }
        if (const auto it = vars.find(upper(r)); it != vars.end()) {
            // 1.11.5 : forcee (l'onglet Variables IHM de la simulation), elle garde sa valeur.
            if (rt_.forcedIhm_.count(it->first)) return true;
            // 1.11.7 : liee a un esclave simule qui la force (ou l'anime) : le forcage passe
            // avant le script - l'ecriture est ignoree, sans erreur.
            if (!rt_.bound_.empty() && rt_.hooks_.boundForced && rt_.boundVariable(it->first) && rt_.hooks_.boundForced(it->first))
                return true;
            // Lot 15 : une variable liee a un equipement s'ecrit dans l'equipement ;
            // refusee (lecture seule, sans liaison), elle ne change pas.
            if (!rt_.bound_.empty()) {
                std::string why;
                if (rt_.writeBound(it->first, v, &why) == 0) {
                    diagnostics.push_back({sim::Diagnostic::Severity::Error, why, 0, {}});
                    return false;
                }
            }
            ++rt_.perf_.writes;                                   // lot 13 : les performances
            // Lot 13 : un geste de l'operateur - le journal d'audit garde avant et apres.
            if (rt_.auditingWrites()) {
                const sim::Value before = it->second;
                it->second.assignFrom(v);
                rt_.auditWrite(r, before, it->second);
                return true;
            }
            it->second.assignFrom(v);
            return true;
        }
        // Lot 16 : une case hors des bornes d'un tableau IHM, une propriete de
        // tableau (Length...) : refusees, et dites.
        if (!rt_.aggregates_.empty()) {
            std::string why;
            if (rt_.outOfBoundsPath(r, &why)) {
                rt_.reportBounds(r, why + " : \xC3\xA9" "criture refus\xC3\xA9" "e");
                diagnostics.push_back({sim::Diagnostic::Severity::Error, r + " : " + why, 0, {}});
                return false;
            }
            if (const auto dot = r.rfind('.'); dot != std::string::npos && types::property(std::string_view(r).substr(dot + 1))
                && rt_.aggregates_.count(upper(r.substr(0, dot)))) {
                diagnostics.push_back({sim::Diagnostic::Severity::Error,
                                       r + " : une propri\xC3\xA9t\xC3\xA9 de tableau ou de structure se lit seulement", 0, {}});
                return false;
            }
        }
        // Lot 9 : une propriete d'objet (figee a cette valeur), la place d'une
        // popup ; une variable systeme, en lecture seule, est refusee.
        if (r.find('.') != std::string::npos) {
            std::string why;
            const int written = rt_.publicWrite(r, v, &why);
            if (written == 1) return true;
            if (written == 0) {
                rt_.publicWhy_ = why;
                diagnostics.push_back({sim::Diagnostic::Severity::Error, why, 0, {}});
                return false;
            }
        }
        if (!rt_.plc_) return false;
        ++rt_.perf_.writes;                                       // lot 13 : les performances
        const std::string pn = plcOf(r);                          // 1.11.1 : sans API.
        if (rt_.auditingWrites()) {
            sim::Value before, after;
            const bool known = rt_.plc_->read(pn, before);
            if (!rt_.plc_->write(pn, v)) return false;
            if (rt_.plc_->read(pn, after)) rt_.auditWrite(r, known ? before : sim::Value{}, after);
            return true;
        }
        return rt_.plc_->write(pn, v);
    }
    bool exists(std::string_view n) override {
        if (!frames.empty() && frames.back()->count(upper(n)) != 0) return true;
        if (sim::Value probe; callerLocals > 0 && sim::readCallerLocal(n, probe)) return true;     // 1.11.17
        if (aliases && aliases->value(n)) return true;
        const std::string r = resolved(n);
        if (!r.empty() && r[0] == '$') {   // 1.9 : la copie d'un parametre
            sim::Value probe;
            return rt_.copyRead(r, probe);
        }
        if (vars.count(upper(r)) != 0) return true;
        // Lot 16 : une propriete (Consignes.Length) existe ; une structure ou un
        // tableau entier (Four1) non - l'interpreteur tente alors la copie en bloc.
        if (!rt_.aggregates_.empty()) {
            sim::Value probe;
            if (const auto dot = r.rfind('.'); dot != std::string::npos)
                if (const auto it = rt_.aggregates_.find(upper(r.substr(0, dot))); it != rt_.aggregates_.end()
                    && types::propertyValue(it->second, std::string_view(r).substr(dot + 1), probe))
                    return true;
        }
        return (r.find('.') != std::string::npos && rt_.publicExists(r)) || (rt_.plc_ && rt_.plc_->exists(plcOf(r)));
    }
    bool call(std::string_view name, std::string_view instance,
              const std::vector<std::pair<std::string, sim::Value>>& args, sim::Value& result) override {
        const auto u = upper(name);
        if (u.rfind("IHM_", 0) == 0) return ihm(u, args, result);
        // Lot 7 : une fonction IHM du projet passe avant celles de l'automate.
        if (rt_.project_)
            if (const auto* f = rt_.project_->functionByName(name)) return rt_.callFunction(*f, args, result);
        // 1.11.10 : la fonction d'une instance de symbole (Vue_Vannes.Vanne_3.Ouvrir).
        if (const auto* f = symbolCall(name)) return rt_.callFunction(*f, args, result);
        if (rt_.plc_) return rt_.plc_->call(name, instance, args, result);
        // Lot 8 : sans automate (l'IHM seule, les exemples de l'aide), les
        // fonctions standard restent la : SEL, SIN, LIMIT, les conversions...
        return isStandardFunction(name) && standardFunctions().call(name, instance, args, result);
    }
    bool assignAggregate(std::string_view target, std::string_view source) override {
        if (rt_.readOnly_ > 0) return false;
        // Lot 16 : Four2 := Four1 entre deux structures (ou tableaux) IHM du meme type.
        if (!rt_.aggregates_.empty() && rt_.aggregateAssign(resolved(target), resolved(source))) return true;
        return rt_.plc_ && rt_.plc_->assignAggregate(plcOf(resolved(target)), plcOf(resolved(source)));
    }
    // Lot 7 : les fonctions IHM du projet, pour les expressions de vue ; lot 8 :
    // et les fonctions IHM_ qui ne font que lire (l'utilisateur, la vue...).
    [[nodiscard]] bool hostsFunction(std::string_view name) const override {
        if (readOnlyIhm(upper(name))) return true;
        if (rt_.project_ && rt_.project_->functionByName(name) != nullptr) return true;
        return const_cast<Env*>(this)->symbolCall(name) != nullptr;   // 1.11.10 : {Vanne_3.Etat()}
    }
    // 1.11.10 : "Vue.Instance.Fonction" (ou un parametre de popup qui y mene) : la fonction
    // de l'instance, prete a tourner ; nul : ce n'est pas une fonction d'instance.
    const HmiFunction* symbolCall(std::string_view name) {
        if (!rt_.project_ || name.find('.') == std::string_view::npos) return nullptr;
        const std::string r = resolved(name);
        return rt_.symbolCall(r.empty() ? std::string(name) : r);
    }
    bool callFromExpression(std::string_view name, const std::vector<std::pair<std::string, sim::Value>>& args,
                            sim::Value& result) override {
        const auto u = upper(name);
        if (readOnlyIhm(u)) {
            ++rt_.readOnly_;
            const bool ok = ihm(u, args, result);
            --rt_.readOnly_;
            return ok;
        }
        const auto* f = rt_.project_ ? rt_.project_->functionByName(name) : nullptr;
        if (!f) f = symbolCall(name);                     // 1.11.10 : {Vanne_3.Etat()}
        if (!f) return false;
        ++rt_.readOnly_;
        // Une fonction ne voit pas les parametres de la vue : on les lui passe.
        const Scope* saved = aliases;
        aliases = nullptr;
        const bool ok = rt_.callFunction(*f, args, result);
        aliases = saved;
        --rt_.readOnly_;
        return ok;
    }
    [[nodiscard]] static bool readOnlyIhm(const std::string& u) {
        return u == "IHM_VUE" || u == "IHM_TEMPS" || u == "IHM_POPUP_OUVERTE" || u == "IHM_UTILISATEUR"
            || u == "IHM_NOM_UTILISATEUR" || u == "IHM_GROUPE" || u == "IHM_NIVEAU"
            || u == "IHM_EQUIPEMENT_OK" || u == "IHM_EQUIPEMENT_PING"                       // lot 15
            || u == "IHM_ESCLAVE_SIMULE";                                                   // 1.9
    }
    void report(sim::Diagnostic d) override { diagnostics.push_back(std::move(d)); }

    // ---- 1.10 : le dialecte IHM ----
    // Le type d'une variable IHM composee ("T_Four", "ARRAY[0..9] OF REAL") : une
    // fonction interne la recoit en entier (copie) ; FOR EACH la parcourt.
    std::string declaredType(std::string_view n) override {
        if (!rt_.project_) return {};
        const std::string r = resolved(n);
        std::string t = types::typeOfPath(*rt_.project_, r);
        if (!t.empty()) return t;
        // 1.10 (S2) : Vue_A.Pompe1, une instance de symbole : le nom de son symbole
        // (l'operande d'un operateur du symbole).
        if (const auto dot = r.find('.'); dot != std::string::npos && r.find('.', dot + 1) == std::string::npos) {
            if (const auto* v = rt_.project_->viewByName(std::string_view(r).substr(0, dot)))
                if (const auto* o = v->objectByName(std::string_view(r).substr(dot + 1)))
                    if (const auto* sym = symbolOf(*rt_.project_, *o)) return sym->name;
        }
        return {};
    }
    bool structMembers(std::string_view typeName, std::vector<std::pair<std::string, std::string>>& out) override {
        if (!rt_.project_) return false;
        // 1.10 (decision 15) : une enumeration repond par ses valeurs, (nom, "#nombre"), dans l'ordre.
        if (const auto* e = findEnumeration(*rt_.project_, typeName)) {
            for (const auto& v : e->values) out.emplace_back(v.name, "#" + std::to_string(v.value));
            return !out.empty();
        }
        for (const auto& m : types::membersOf(*rt_.project_, typeName)) out.emplace_back(m.name, m.type);
        return !out.empty();
    }
    std::string canonicalName(std::string_view n) override { return resolved(n); }
    // 1.10 (integration, S1 <-> S2) : un operateur d'un type IHM ou d'un symbole
    // (`a + b`, `a += b`, `TO_xxx(a)`...) : S2 le trouve, S1 l'appelle.
    bool findOperator(std::string_view op, std::string_view l, std::string_view r, sim::OperatorSource& out) override {
        hmi::OperatorCall c;
        if (!rt_.project_ || !hmi::resolveOperator(*rt_.project_, op, l, r, c)) return false;
        out.key = std::move(c.key);
        out.function = std::move(c.function);
        out.owner = std::move(c.owner);
        return true;
    }
    // Une fonction IHM du projet qui emploie les types riches (VAR_IN_OUT, REF_TO,
    // POINTER TO, ARRAY, MAP, structure) tourne dans le dialecte, appelee du
    // script avec ses arguments riches ; une fonction simple, comme avant (env.call).
    std::shared_ptr<const sim::Function> dialectFunction(std::string_view name) override {
        const auto* f = rt_.project_ ? rt_.project_->functionByName(name) : nullptr;
        if (!f) f = symbolCall(name);                     // 1.11.10 : une fonction d'instance aux types riches
        if (!f) return nullptr;
        const std::string text = functionText(*f);
        if (const auto it = richFunctions_.find(text); it != richFunctions_.end()) return it->second;
        std::shared_ptr<const sim::Function> fn;
        if (auto parsed = sim::parseFunction(text, "fonction " + f->name); parsed && !sim::functionIsSimple(**parsed)) fn = *parsed;
        richFunctions_[text] = fn;
        return fn;
    }
    // Une fonction du projet ne voit pas les parametres de la vue qui l'appelle.
    void enterFunction(std::string_view) override {
        savedAliases_.push_back(aliases);
        aliases = nullptr;
    }
    void leaveFunction(std::string_view) override {
        if (savedAliases_.empty()) return;
        aliases = savedAliases_.back();
        savedAliases_.pop_back();
    }
    std::map<std::string, std::shared_ptr<const sim::Function>> richFunctions_;
    std::vector<const Scope*> savedAliases_;

private:
    bool ihm(const std::string& u, const std::vector<std::pair<std::string, sim::Value>>& args, sim::Value& result) {
        // Depuis une expression de vue (par une fonction du projet) : lire oui,
        // agir non (naviguer, ecrire au journal, jouer un son, appeler un script).
        if (rt_.readOnly_ > 0 && !readOnlyIhm(u)) {
            diagnostics.push_back({sim::Diagnostic::Severity::Error,
                                   u + " : interdit dans une fonction appel\xC3\xA9" "e par une expression de vue", 0, {}});
            return false;
        }
        const auto text = [&](std::size_t i) {
            return i < args.size() ? (args[i].second.type() == sim::Type::String ? args[i].second.asString()
                                                                                 : args[i].second.display())
                                   : std::string{};
        };
        const double now = rt_.now_;
        if (u == "IHM_NAVIGUER") {
            Transition t;
            if (args.size() > 1)
                if (const auto k = transitionFromLabel(text(1))) t.kind = *k;
            const auto* v = rt_.project_ ? rt_.project_->viewByName(text(0)) : nullptr;
            result = sim::Value::boolean(v && rt_.navigate(v->id, t, now, text(2)));
            return true;
        }
        // Lot 8 : IHM_POPUP('Vue' [, 'Moteur := Pompes[3]' [, 'objet']]).
        if (u == "IHM_POPUP" || u == "IHM_CHANGER_POPUP") {
            Transition t;
            t.kind = TransitionKind::Fade;
            t.durationMs = 250;
            const auto* v = rt_.project_ ? rt_.project_->viewByName(text(0)) : nullptr;
            std::string args = text(1);
            // 1.11.10 : IHM_POPUP('Vue.Vanne_3.Pop_Detail') - la popup du symbole, avec l'instance.
            if (std::string popup, given; !v && rt_.project_ && popupOfInstance(*rt_.project_, nullptr, resolved(text(0)), popup, given)) {
                v = rt_.project_->viewByName(popup);
                args = given + (args.find_first_not_of(" \t") == std::string::npos ? std::string{} : "; " + args);
            }
            if (!v) {
                rt_.log("Erreur", rt_.source_, u + " : vue '" + text(0) + "' introuvable");
                result = sim::Value::boolean(false);
                return true;
            }
            result = sim::Value::boolean(u == "IHM_POPUP" ? rt_.openPopup(v->id, t, now, args, text(2), kNoId)
                                                          : rt_.changePopup(v->id, t, now, args));
            return true;
        }
        if (u == "IHM_FERMER_POPUP") {
            Transition t;
            t.kind = TransitionKind::Fade;
            t.durationMs = 200;
            result = sim::Value::boolean(rt_.closePopup(t, now));
            return true;
        }
        if (u == "IHM_FERMER_POPUPS") {
            result = sim::Value::boolean(rt_.closeAllPopups(Transition{}, now) > 0);
            return true;
        }
        if (u == "IHM_CENTRER_POPUP") {
            const auto* v = args.empty() || !rt_.project_ ? nullptr : rt_.project_->viewByName(text(0));
            result = sim::Value::boolean(rt_.centerPopup(v ? v->id : kNoId));
            return true;
        }
        if (u == "IHM_POPUP_PRECEDENTE") {
            Transition t;
            t.kind = TransitionKind::Fade;
            t.durationMs = 200;
            result = sim::Value::boolean(rt_.previousPopup(t, now));
            return true;
        }
        if (u == "IHM_POPUP_OUVERTE") {
            const auto* v = rt_.project_ ? rt_.project_->viewByName(text(0)) : nullptr;
            result = sim::Value::boolean(v && std::find(rt_.popups_.begin(), rt_.popups_.end(), v->id) != rt_.popups_.end());
            return true;
        }
        // Lot 8 : l'utilisateur connecte.
        if (u == "IHM_UTILISATEUR") { result = sim::Value::text(rt_.user_); return true; }
        if (u == "IHM_NOM_UTILISATEUR") {
            const auto* who = rt_.user();
            result = sim::Value::text(who ? (who->fullName.empty() ? who->login : who->fullName) : std::string{});
            return true;
        }
        if (u == "IHM_GROUPE") {
            const auto* who = rt_.user();
            const auto* g = who && rt_.project_ ? rt_.project_->group(who->group) : nullptr;
            result = sim::Value::text(g ? g->name : std::string{});
            return true;
        }
        if (u == "IHM_NIVEAU") {
            result = sim::Value::integer(sim::Type::Int, rt_.user_.empty() && rt_.project_ && rt_.project_->security.enabled ? 0 : rt_.level());
            return true;
        }
        if (u == "IHM_DECONNECTER") {
            const bool was = !rt_.user_.empty();
            rt_.logout(now, "IHM_DECONNECTER (" + rt_.source_ + ")");
            result = sim::Value::boolean(was);
            return true;
        }
        if (u == "IHM_JOURNAL") {
            const CallerLocals locals(*this);
            rt_.log("Journal", rt_.source_, TextTemplate::compile(text(0)).render(*this));
            result = sim::Value::boolean(true);
            return true;
        }
        // 1.11.14 : IHM_LOG(INFO, 'message {Variable:0.0}') - une ligne de la Console, de ce
        // niveau, avec la source et la ligne de l'instruction (lue avant que le message ne
        // calcule ses trous : une fonction appelee la changerait).
        if (u == "IHM_LOG") {
            std::optional<LogLevel> level;
            if (!args.empty()) {
                const auto& v = args[0].second;
                level = v.type() == sim::Type::String ? logLevelByName(v.asString()) : logLevelOf(v.asInteger());
            }
            if (!level) {
                diagnostics.push_back({sim::Diagnostic::Severity::Error,
                                       "IHM_LOG : niveau inconnu (TRACE, DEBUG, INFO, SUCCESS, WARNING, ERROR ou CRITICAL)", 0, {}});
                return false;
            }
            const int line = static_cast<int>(rt_.trace_.line);
            const CallerLocals locals(*this);
            rt_.logAt(*level, "IHM_LOG", rt_.source_, TextTemplate::compile(text(1)).render(*this), line > 0 ? line : -1);
            result = sim::Value::boolean(true);
            return true;
        }
        if (u == "IHM_APPELER") {
            std::string why;
            result = sim::Value::boolean(rt_.callScript(text(0), now, &why));
            return true;
        }
        if (u == "IHM_SON") {
            result = sim::Value::boolean(rt_.playSound(text(0), rt_.source_));
            return true;
        }
        // Lot 10 : le menu natif Parametres systeme ('Diagnostic' : cet onglet ;
        // 1.9 : 'Simulation', la page des esclaves simules).
        if (u == "IHM_PARAMETRES_SYSTEME") {
            const std::string tab = args.empty() ? std::string{} : text(0);
            rt_.openSystemMenu(systemTabFrom(plain(tab)), now, rt_.source_);
            result = sim::Value::boolean(true);
            return true;
        }
        // Lot 12 : le menu natif de connexion ('Comptes' : cet onglet, s'il se voit).
        //   Vrai : l'onglet demande est montre ; faux : le menu s'ouvre sur Connexion.
        if (u == "IHM_MENU_CONNEXION") {
            const LoginTab tab = loginTabFrom(args.empty() ? std::string{} : text(0));
            rt_.openLoginMenu(tab, now, rt_.source_);
            result = sim::Value::boolean(rt_.loginTab() == tab);
            return true;
        }
        // Lot 13 : le theme ('jour', 'nuit', 'bascule') ; vrai : accepte.
        // Lot 16 : les GIF animes d'une vue ouverte, par leur nom.
        //   IHM_GIF_JOUER('Objet' [, N]) ; IHM_GIF_PAUSE ; IHM_GIF_ARRETER ; IHM_GIF_REJOUER('Objet', N).
        if (u == "IHM_GIF_JOUER" || u == "IHM_GIF_PAUSE" || u == "IHM_GIF_ARRETER" || u == "IHM_GIF_REJOUER") {
            const char* what = u == "IHM_GIF_JOUER" ? "jouer" : u == "IHM_GIF_PAUSE" ? "pause" : u == "IHM_GIF_ARRETER" ? "arreter" : "rejouer";
            int count = -1;
            if (args.size() > 1) count = static_cast<int>(std::max<long long>(0, args[1].second.asInteger()));
            if (u == "IHM_GIF_JOUER" && args.size() > 1) what = "rejouer";
            result = sim::Value::boolean(rt_.gifCommand(text(0), what, count, now, rt_.source_));
            return true;
        }
        if (u == "IHM_THEME") {
            std::string why;
            result = sim::Value::boolean(rt_.setDisplay("theme", args.empty() ? std::string("bascule") : text(0), now, rt_.source_, &why));
            return true;
        }
        // Lot 13 : la langue ('en', 'English', 'suivante') ; vrai : elle est connue.
        if (u == "IHM_LANGUE") {
            std::string why;
            result = sim::Value::boolean(rt_.setLanguage(args.empty() ? std::string("suivante") : text(0), now, rt_.source_, &why));
            return true;
        }
        // Lot 11 : les alarmes (mise de cote, silence) et l'export.
        //   IHM_METTRE_DE_COTE('Alarme' [, minutes [, 'raison']]) : le nombre mis de cote
        //   IHM_REMETTRE('Alarme')      IHM_FAIRE_TAIRE()
        //   IHM_EXPORTER('alarmes' [, 'fichier.xlsx' [, demander]]) : vrai si le fichier
        //   est ecrit (lot API 8 : ou si la question "ou l'enregistrer" est posee)
        if (u == "IHM_METTRE_DE_COTE") {
            double minutes = 0;
            if (args.size() > 1 && !parseNumber(text(1), minutes)) minutes = 0;
            std::string why;
            const auto n = rt_.shelve(text(0), minutes, text(2), now, &why);
            if (n == 0) rt_.log("Action", rt_.source_, "IHM_METTRE_DE_COTE : " + why);
            result = sim::Value::integer(sim::Type::Int, static_cast<std::int64_t>(n));
            return true;
        }
        if (u == "IHM_REMETTRE") {
            std::string why;
            const auto n = rt_.unshelve(text(0), now, &why);
            result = sim::Value::integer(sim::Type::Int, static_cast<std::int64_t>(n));
            return true;
        }
        if (u == "IHM_FAIRE_TAIRE") {
            rt_.silenceAlarms(now, rt_.source_);
            result = sim::Value::boolean(true);
            return true;
        }
        if (u == "IHM_EXPORTER") {
            std::string why;
            // Lot API 8 : IHM_EXPORTER('alarmes', 'f.csv', FALSE) - jamais de question ;
            // sans le 3e argument (TRUE) : ou l'ecrire se demande si l'action d'un
            // geste de l'operateur a lance le script (Executer / Appeler un script,
            // au clic), jamais dans un script de vue, periodique, ou d'un timer.
            const bool askWhere = (args.size() > 2 ? args[2].second.isTruthy() : true) && rt_.exportAsk_ > 0;
            result = sim::Value::boolean(rt_.exportData(text(0).empty() ? std::string("alarmes") : text(0),
                                                        args.size() > 1 ? text(1) : std::string("export_{SYS.Date}"), {}, now,
                                                        rt_.source_, &why, askWhere));
            return true;
        }
        // Lot 12 : l'historique de navigation et la vue d'accueil.
        //   IHM_PRECEDENTE() IHM_SUIVANTE() IHM_ACCUEIL() : vrai si la vue a change
        if (u == "IHM_PRECEDENTE" || u == "IHM_SUIVANTE" || u == "IHM_ACCUEIL") {
            std::string why;
            const bool done = u == "IHM_PRECEDENTE" ? rt_.goBack(Transition{}, now, 1, &why)
                            : u == "IHM_SUIVANTE" ? rt_.goForward(Transition{}, now, &why) : rt_.goHome(Transition{}, now, &why);
            if (!done) rt_.log("Action", rt_.source_, u + " : " + why);
            result = sim::Value::boolean(done);
            return true;
        }
        if (u == "IHM_VUE") {
            const auto* v = rt_.viewOf(rt_.current_);
            result = sim::Value::text(v ? v->name : std::string{});
            return true;
        }
        if (u == "IHM_TEMPS") {
            result = sim::Value::time(static_cast<std::int64_t>(std::llround((now - rt_.startNow_) * 1000.0)));
            return true;
        }
        // Lot 15 : un equipement du reseau repond-il (IHM_EQUIPEMENT_OK('Analyseur')),
        // et son dernier ping en ms (-1 : pas de reponse). Pas encore essaye (la
        // premiere seconde) : vrai - pas de fausse alarme au demarrage.
        if (u == "IHM_EQUIPEMENT_OK" || u == "IHM_EQUIPEMENT_PING") {
            const auto st = rt_.equipmentStatus(text(0));
            if (u == "IHM_EQUIPEMENT_OK") result = sim::Value::boolean(st && st->enabled && (st->reachable || (!st->tested && !st->simulated)));
            else result = sim::Value::real(st ? st->pingMs : -1.0);
            return true;
        }
        // 1.9 : l'IHM lit-elle en ce moment cet equipement sur son esclave simule
        // (ou n'est-il que simule) ? IHM_ESCLAVE_SIMULE('Variateur ATV320').
        if (u == "IHM_ESCLAVE_SIMULE") {
            result = sim::Value::boolean(rt_.readOnSlave(text(0)));
            return true;
        }
        return false;
    }
    Runtime& rt_;
};

// Lot 8 : le temps d'un code, les parametres de SA vue (et la portee gardee
// en vie : une action peut fermer la popup qui la porte).
struct AliasGuard {
    AliasGuard(const Scope*& slot, std::shared_ptr<const Scope> scope) : slot_(slot), saved_(slot), keep_(std::move(scope)) {
        slot_ = keep_.get();
    }
    ~AliasGuard() { slot_ = saved_; }
    AliasGuard(const AliasGuard&) = delete;
    AliasGuard& operator=(const AliasGuard&) = delete;
private:
    const Scope*&                slot_;
    const Scope*                 saved_;
    std::shared_ptr<const Scope> keep_;
};

// =============================================================== moteur ====
Runtime::Runtime() : env_(std::make_unique<Env>(*this)) {}

std::shared_ptr<void> Runtime::viewAliases(Id view) {
    auto keep = scopePtr(view);
    Env* env = env_.get();
    const Scope* saved = env->aliases;
    env->aliases = keep.get();
    return std::shared_ptr<void>(nullptr, [env, saved, keep](void*) { env->aliases = saved; });
}
Runtime::~Runtime() = default;

void Runtime::bind(const Project* project, sim::Environment* plc) {
    project_ = project;
    plc_ = plc;
}

sim::Environment& Runtime::environment() { return *env_; }

const sim::Value* Runtime::variable(std::string_view name) const {
    const auto it = env_->vars.find(upper(name));
    return it == env_->vars.end() ? nullptr : &it->second;
}

bool Runtime::forceVariable(std::string_view name, const sim::Value& value, std::string* why) {
    const auto it = env_->vars.find(upper(name));
    if (it == env_->vars.end()) {
        if (why) *why = std::string(name) + " n'est pas une variable IHM (une structure se force case par case)";
        return false;
    }
    it->second.assignFrom(value);   // dans son type (comme une ecriture)
    forcedIhm_.insert(it->first);
    return true;
}

bool Runtime::unforceVariable(std::string_view name) { return forcedIhm_.erase(upper(name)) > 0; }

void Runtime::unforceAllVariables() { forcedIhm_.clear(); }

bool Runtime::variableForced(std::string_view name) const { return forcedIhm_.count(upper(name)) > 0; }

std::vector<std::string> Runtime::forcedVariables() const { return {forcedIhm_.begin(), forcedIhm_.end()}; }

sim::Value* Runtime::ihmSlot(std::string_view name) {
    const auto it = env_->vars.find(upper(name));
    return it == env_->vars.end() ? nullptr : &it->second;
}

// 1.11.15 : les cases des variables IHM non liees, a leur valeur du moment.
std::vector<simdata::Cell> Runtime::captureData(const std::function<bool(const Variable&)>& keep) const {
    if (!project_) return {};
    auto cells = simdata::captureVariables(*project_, [this](const std::string& path) -> const sim::Value* {
        const auto it = env_->vars.find(upper(path));
        return it == env_->vars.end() ? nullptr : &it->second;
    }, keep);
    // 1.11.18 (lot 5) : les declarations Persistantes des scripts - leur valeur du moment ; un
    // script qui n'a pas tourne depuis le demarrage garde celle qui lui avait ete rendue.
    const auto take = [&](const Script& sc, const std::string& owner) {
        const auto locals = scriptLocals_.find("s" + std::to_string(sc.id));
        for (const auto& d : sc.decls) {
            if (d.kind != DeclKind::Variable || d.storage != Storage::Persistent) continue;
            simdata::Cell c;
            c.variable = d.id;
            c.name = owner + "." + d.name;
            c.declared = d.type;
            c.path = std::string(simdata::kDeclarationPath);
            const sim::ObjRef obj = locals != scriptLocals_.end() ? locals->second.find(d.name) : nullptr;
            if (obj && obj->type && obj->type->kind == sim::TypeDesc::Kind::Scalar) c.value = obj->value;
            else if (const auto p = persistPending_.find(d.id); p != persistPending_.end()) c.value = p->second.value;
            else continue;
            if (c.value.type() != sim::Type::Unknown) cells.push_back(std::move(c));
        }
    };
    for (const auto& sc : project_->programs.scripts) take(sc, sc.name);
    for (const auto& v : project_->views)
        for (const auto& sc : v.scripts) take(sc, v.name + "." + sc.event);
    return cells;
}

// 1.11.18 (lot 5) : les cases des declarations Persistantes, gardees jusqu'a la premiere
// execution de leur script ; les autres (les variables IHM) sont rendues a l'appelant.
std::vector<simdata::Cell> Runtime::takePersistent(const std::vector<simdata::Cell>& cells) {
    std::vector<simdata::Cell> rest;
    rest.reserve(cells.size());
    for (const auto& c : cells) {
        if (!simdata::isDeclarationCell(c)) {
            rest.push_back(c);
            continue;
        }
        persistPending_[c.variable] = c;
    }
    return rest;
}

// 1.11.18 (lot 5) : rendre les declarations Persistantes de ce script, avant sa premiere
// execution - le simulateur garde une VAR deja la si son type est le meme (startDialect).
void Runtime::restorePersistent(Id script, sim::Locals& locals) {
    const Script* sc = project_ ? project_->script(script) : nullptr;
    if (!sc) return;
    for (const auto& d : sc->decls) {
        if (d.kind != DeclKind::Variable || d.storage != Storage::Persistent) continue;
        const auto it = persistPending_.find(d.id);
        if (it == persistPending_.end()) continue;
        const simdata::Cell cell = it->second;
        persistPending_.erase(it);
        const std::string type = upper(trimText(d.type));
        sim::Type t = typereg::simTypeOf(type);
        std::string typeName;
        if (t == sim::Type::Unknown && findEnumeration(*project_, d.type)) {      // une enumeration : un DINT qui garde son nom
            t = sim::Type::DInt;
            typeName = d.type;
        }
        const std::string who = sc->name + "." + d.name;
        if (t == sim::Type::Unknown) {
            logAt(LogLevel::Warning, "R\xC3\xA9manence", {}, who + " : Persistante d'un type compos\xC3\xA9 (" + d.type
                                                                + ") - repart de sa valeur initiale (seuls les types simples sont gard\xC3\xA9s)");
            continue;
        }
        const auto v = simdata::convert(cell.value, t);
        if (!v) {
            logAt(LogLevel::Warning, "R\xC3\xA9manence", {}, who + " : " + cell.declared + " devient " + d.type
                                                                + " - incompatible, valeur initiale");
            continue;
        }
        auto obj = sim::makeObj(sim::scalarType(t, typeName));
        obj->value = *v;
        locals.set(d.name, obj);
    }
}

// 1.11.16 : des cases rendues en marche (voir le .hpp).
simdata::Report Runtime::applyData(const std::vector<simdata::Cell>& cells, const std::string& label) {
    simdata::Report rep;
    if (!project_ || cells.empty()) return rep;
    // 1.11.18 (lot 5) : une declaration Persistante - dans son script s'il a deja tourne, sinon a
    // sa premiere execution.
    const auto variables = takePersistent(cells);
    for (const auto& [id, cell] : std::map<Id, simdata::Cell>(persistPending_)) {
        for (auto& [key, locals] : scriptLocals_) {
            if (key.empty() || key[0] != 's') continue;
            const Id script = static_cast<Id>(std::strtoull(key.c_str() + 1, nullptr, 10));
            const Script* sc = project_->script(script);
            if (!sc || std::none_of(sc->decls.begin(), sc->decls.end(), [&](const Declaration& d) { return d.id == id; })) continue;
            locals.erase(std::find_if(sc->decls.begin(), sc->decls.end(), [&](const Declaration& d) { return d.id == id; })->name);
            restorePersistent(script, locals);
        }
    }
    rep = simdata::restoreVariables(*project_, variables, [this](const std::string& path) { return ihmSlot(path); });
    logAt(LogLevel::Info, "R\xC3\xA9manence", {}, label + " : " + rep.summary());
    for (const auto& w : rep.warnings) logAt(LogLevel::Warning, "R\xC3\xA9manence", {}, w);
    return rep;
}

const View* Runtime::viewOf(Id id) const {
    const View* v = project_ ? project_->view(id) : nullptr;
    if (!v) return v;
    // Lot 9 : les proprietes ecrites en marche (variables d'instances) se posent
    // sur la vue composee - une vue sans modele en a une aussi, des qu'on ecrit.
    // Lot 10 : les instances de symboles se developpent (leurs objets, relies a
    // leurs arguments), apres les surcharges : une instance cachee cache les siens.
    const bool inherited = inherits(*project_, *v);
    const bool symbols = usesSymbols(*project_, *v);
    const bool owned = popupOwner(*project_, *v) != nullptr;    // 1.11.10 : une popup d'un symbole
    if (!inherited && !symbols && !owned && !overridden(*v)) return v;
    // Une entree de std::map ne bouge pas quand on en ajoute d'autres : le
    // pointeur rendu vaut jusqu'au prochain effacement (entree du moteur).
    auto it = composed_.find(id);
    if (it == composed_.end()) {
        View c = inherited ? compose(*project_, *v) : *v;
        applyOverrides(c, *v);
        if (symbols) c = expandInstances(*project_, c);
        if (owned) c = qualifiedOwnedPopup(*project_, c);        // 1.11.10 : Ouvrir() vise l'instance qui l'ouvre
        it = composed_.emplace(id, std::move(c)).first;
    }
    return &it->second;
}

std::size_t Runtime::runs(Id script) const {
    const auto it = runs_.find(script);
    return it == runs_.end() ? 0 : it->second;
}
std::string Runtime::lastError(Id script) const {
    const auto it = errors_.find(script);
    return it == errors_.end() ? std::string{} : it->second;
}

std::string Runtime::stampOf(double now) const {
    // Lot 10 : l'heure de l'IHM, avec l'ecart regle dans Parametres systeme.
    const long long ms = startWallMs_ + static_cast<long long>(std::llround((now - startNow_ + settings_.clockOffset) * 1000.0));
    const std::time_t secs = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char b[32];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms % 1000));
    return b;
}

double Runtime::epochOf(double now) const {
    return static_cast<double>(startWallMs_) / 1000.0 + (now - startNow_) + settings_.clockOffset;
}

std::string Runtime::dateStampOf(double now) const {
    const long long ms = startWallMs_ + static_cast<long long>(std::llround((now - startNow_ + settings_.clockOffset) * 1000.0));
    const std::time_t secs = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char b[96];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms % 1000));
    return b;
}

void Runtime::log(std::string kind, std::string source, std::string message) {
    const LogLevel level = logLevelOfKind(kind);
    logAt(level, std::move(kind), std::move(source), std::move(message));
}

void Runtime::logAt(LogLevel level, std::string kind, std::string source, std::string message, int line) {
    JournalEntry e;
    e.time = now_ - startNow_;
    e.stamp = stampOf(now_);
    e.kind = std::move(kind);
    e.source = std::move(source);
    e.message = std::move(message);
    // 1.11.14 : la Console - le niveau, le code qui tourne (sa ligne), le cycle, la session.
    e.level = level;
    e.code = origin_.name;
    e.view = origin_.view;
    e.object = origin_.object;
    e.script = origin_.script;
    e.function = origin_.function;
    e.line = line > 0 ? line : line == 0 && !origin_.name.empty() ? static_cast<int>(trace_.line) : 0;
    e.cycle = cycles_;
    e.session = session_;
    if (hooks_.journaled) hooks_.journaled(e);
    // L'historique systeme : le journal, date, avec l'utilisateur du moment.
    if (history_ && project_ && project_->history.system) {
        history_->system.push_back(HistoryEvent{dateStampOf(now_), e.kind, e.source, e.message, user_});
        if (history_->system.size() > static_cast<std::size_t>(std::max(10, project_->history.maxEntries)) + 64)
            history_->trim(project_->history);
    }
    journal_.push_back(std::move(e));
    while (journal_.size() > kJournalMax) journal_.pop_front();
}

void Runtime::event(std::string kind, std::string source, std::string message) {
    HistoryEvent e{dateStampOf(now_), kind, source, message, user_};
    if (history_ && project_ && project_->history.events) {
        history_->events.push_back(e);
        if (history_->events.size() > static_cast<std::size_t>(std::max(10, project_->history.maxEntries)) + 64)
            history_->trim(project_->history);
    }
    events_.push_back(std::move(e));
    while (events_.size() > kJournalMax) events_.pop_front();
    log(std::move(kind), std::move(source), std::move(message));
}

std::string Runtime::where(const View& v, const Object* o) const {
    // Lot 6 : ce que la vue emprunte (ecran modele, en-tete, pied) dit d'ou il vient.
    if (!o) return actionOrigin_.empty() ? v.name : v.name + " (" + actionOrigin_ + ")";
    if (project_) {
        const View* own = project_->view(v.id);
        if (own && !own->object(o->id))
            for (const auto& other : project_->views)
                if (other.id != v.id && other.object(o->id)) return v.name + "/" + o->name + " (" + other.name + ")";
    }
    return v.name + "/" + o->name;
}

std::string Runtime::evalText(const std::string& expr, bool* ok) {
    auto it = expressions_.find(expr);
    if (it == expressions_.end()) it = expressions_.emplace(expr, Expression::compile(expr)).first;
    auto v = it->second.evaluate(*env_);
    if (ok) *ok = static_cast<bool>(v);
    return v ? formatValue(*v) : std::string{};
}

bool Runtime::evalBool(const std::string& expr, bool fallback) {
    auto it = expressions_.find(expr);
    if (it == expressions_.end()) it = expressions_.emplace(expr, Expression::compile(expr)).first;
    auto v = it->second.evaluate(*env_);
    return v ? v->isTruthy() : fallback;
}

// 1.9 : un chemin absolu (la variable de l'appelant d'une popup), sans les
// parametres de la vue dont le code tourne.
bool Runtime::readDirect(const std::string& path, sim::Value& out) {
    AliasGuard guard(env_->aliases, nullptr);
    return env_->read(path, out);
}
bool Runtime::writeDirect(const std::string& path, const sim::Value& v, const std::string& source) {
    AliasGuard guard(env_->aliases, nullptr);
    return write(path, v, source);
}

bool Runtime::write(const std::string& name, const sim::Value& v, const std::string& source) {
    publicWhy_.clear();
    if (env_->exists(name) && env_->write(name, v)) return true;
    // Lot 9 : une variable systeme, une propriete en lecture seule - dit pourquoi.
    log("Erreur", source, "\xC3\xA9" "criture refus\xC3\xA9" "e : " + (publicWhy_.empty() ? name + " (variable inconnue)" : publicWhy_));
    return false;
}

namespace {
// 1.11.14 : "ligne 3 : ..." -> 3 ; -1 : le message ne dit pas de ligne.
int announcedLine(const std::string& why) {
    if (why.rfind("ligne ", 0) != 0) return -1;
    const int n = std::atoi(why.c_str() + 6);
    return n > 0 ? n : -1;
}

// La valeur de depart d'une variable locale : son type, puis son ":= valeur".
sim::Value localInitial(const LocalVar& l, sim::Environment& env, std::string* why) {
    sim::Value v = sim::Value::defaultOf(typereg::simTypeOf(l.type));   // 1.11.19 : LINT, USINT, lreal... (avant : un INT)
    if (!l.initial.empty()) {
        auto init = Expression::compile(l.initial).evaluate(env);
        if (init) v.assignFrom(*init);
        else if (why && why->empty()) *why = l.name + " : valeur initiale illisible (" + l.initial + ")";
    }
    return v;
}
} // namespace

const Runtime::Prepared& Runtime::prepare(const std::string& code, const std::string& source, bool function) {
    const std::string key = function ? "\x01" + code : code;     // VAR_INPUT n'est permis qu'a une fonction
    if (const auto it = programs_.find(key); it != programs_.end()) return it->second;
    Prepared prep;
    // 1.11.1 (REP) : le code se lit sans les $ de ses reperes, comme une expression (les
    // chaines ST et les commentaires gardent les leurs ; les lignes ne bougent pas). La cle
    // du cache reste le texte enregistre.
    const std::string st = markers::strip(code);
    // 1.10 : une locale peut etre d'un type IHM du projet (structure).
    auto parts = splitDeclarations(st, function, [this](std::string_view type) {
        return project_ && (!types::membersOf(*project_, type).empty() || findEnumeration(*project_, type) != nullptr);
    });
    prep.locals = std::move(parts.locals);
    if (!parts.errors.empty()) {
        const auto& e = parts.errors.front();
        prep.error = (e.line ? "ligne " + std::to_string(e.line) + " : " : std::string{}) + e.message;
    } else if (auto parsed = sim::parse(function ? std::string_view(parts.body) : std::string_view(st), source, dialectOptions());
               !parsed) {
        // 1.10 : le dialecte IHM ; un script garde ses VAR (le simulateur les tient),
        // une fonction IHM a les siennes otees (parametres et locales : son cadre).
        // 1.11.2 (REP) : un $ reste seul - la faute dite comme dans l'editeur (checkScript) : le conseil de sa ligne.
        std::string rest;
        const int line = splitLine(parsed.error().context.empty() ? parsed.error().message() : parsed.error().context, &rest);
        const std::string fr = markers::explainScriptDollar(frenchSimMessage(rest), code, line);
        prep.error = line ? "ligne " + std::to_string(line) + " : " + fr : fr;
    } else {
        prep.program = *parsed;
    }
    return programs_.emplace(key, std::move(prep)).first->second;
}

bool Runtime::runStatements(const std::string& code, const std::string& source, double now, std::string* error,
                            Id scriptId) {
    (void)now;
    const Prepared& prep = prepare(code, source, false);
    if (!prep.program) {
        const std::string why = prep.error;
        if (error) *error = why;
        if (scriptId) errors_[scriptId] = why;
        logAt(LogLevel::Error, "Erreur", source, why, announcedLine(why));
        return false;
    }
    const std::string previous = source_;
    source_ = source;
    // Lot 7 : un cadre pour ce script. Ses VAR reprennent leur valeur de la
    // fois d'avant, ses VAR_TEMP repartent de leur valeur initiale. 1.10 : le
    // simulateur les tient (dialecte IHM : types riches, references) ; le cadre
    // du moteur reste vide (un script ne voit pas les locales de l'appelant).
    Env::Frame frame;
    env_->frames.push_back(&frame);
    sim::Locals* kept = nullptr;
    if (!prep.locals.empty()) kept = &scriptLocals_[scriptId ? "s" + std::to_string(scriptId) : "c" + code];
    // 1.11.18 (lot 5) : la premiere execution depuis le demarrage - ses declarations Persistantes.
    if (kept && scriptId && kept->all().empty() && !persistPending_.empty()) restorePersistent(scriptId, *kept);
    env_->diagnostics.clear();
    sim::RunLimits limits;
    limits.maxIterationsPerLoop = 100000;
    limits.maxStatementsPerScan = 200000;
    limits.locals = kept;
    // 1.11.14 : la ligne en cours (IHM_LOG la dit) ; celle de l'appelant revient apres.
    const sim::ExecTrace callerTrace = trace_;
    trace_ = {};
    limits.trace = &trace_;
    const auto result = sim::execute(*prep.program, *env_, limits);
    trace_ = callerTrace;
    env_->frames.pop_back();
    source_ = previous;
    if (scriptId) ++runs_[scriptId];
    std::string why;
    int whyLine = -1;                // -1 : pas de ligne (un budget depasse)
    for (const auto& d : env_->diagnostics)
        if (d.severity == sim::Diagnostic::Severity::Error) {
            why = (d.line ? "ligne " + std::to_string(d.line) + " : " : std::string{}) + frenchSimMessage(d.message);
            if (d.line) whyLine = static_cast<int>(d.line);
            break;
        }
    // Un appel imbrique a ete coupe (appel circulaire) : tout l'enchainement
    // echoue, et c'est cette raison qui remonte.
    if (!abort_.empty()) why = abort_;
    if (why.empty() && !result.completed) why = "arr\xC3\xAAt\xC3\xA9 : budget d'instructions d\xC3\xA9pass\xC3\xA9 (boucle sans fin ?)";
    if (!why.empty()) {
        if (error) *error = why;
        if (scriptId) errors_[scriptId] = why;
        if (why != abort_ || depth_ <= 1) {
            if (why != abort_) logAt(LogLevel::Error, "Erreur", source, why, whyLine);
        }
        if (depth_ <= 1) abort_.clear();    // l'appel le plus haut l'a rapporte
        return false;
    }
    if (scriptId) errors_.erase(scriptId);
    return true;
}

// 1.11.10 : la fonction d'une instance de symbole, prete a tourner (HmiSymbols.hpp).
const HmiFunction* Runtime::symbolCall(std::string_view call) {
    if (!project_) return nullptr;
    const std::string key = upper(call);
    if (const auto it = boundCalls_.find(key); it != boundCalls_.end()) return it->second.get();
    std::shared_ptr<const HmiFunction> fn;
    if (BoundFunction b; boundSymbolFunction(*project_, call, b)) fn = std::make_shared<const HmiFunction>(std::move(b.function));
    boundCalls_[key] = fn;
    return fn.get();
}

bool Runtime::callFunction(const HmiFunction& f, const std::vector<std::pair<std::string, sim::Value>>& args,
                           sim::Value& result) {
    // Lot 8 : une fonction ne voit pas les parametres de la vue qui l'appelle.
    AliasGuard aliasGuard(env_->aliases, nullptr);
    const std::string source = "fonction " + f.name;
    const sim::Type rt = typereg::simTypeOf(f.returnType);
    result = f.returnType.empty() ? sim::Value::boolean(true) : sim::Value::defaultOf(rt);
    // Une faute dans une fonction : l'appel echoue (l'instruction qui
    // l'appelle s'arrete), et tout l'enchainement avec lui - c'est la cause
    // premiere (abort_) qui remonte, dite une fois par celui qui la trouve.
    // Depuis une expression de vue (evaluee a chaque image), elle ne se dit
    // qu'une fois pour toutes, et l'echec s'arrete a l'expression.
    int failLine = 0;                // 1.11.14 : la ligne de la faute dans la fonction (0 : celle de l'appel)
    const auto say = [&](const std::string& message) {
        if (readOnly_ > 0 && !functionErrors_.insert(f.name + "|" + message).second) return;
        logAt(LogLevel::Error, "Erreur", source, message, failLine);
    };
    const auto fail = [&](const std::string& why) {
        if (abort_.empty()) {
            say(why);
            abort_ = source + " : " + why;
        }
        if (depth_ == 0) abort_.clear();
        return false;
    };
    if (depth_ >= kMaxDepth) return fail("appel circulaire (plus de " + std::to_string(kMaxDepth) + " niveaux)");
    std::string composed;                                            // 1.11.18 (lot 3) : ses declarations du modele
    const Prepared& prep = prepare(decl::codeOf(f, composed), source, true);
    if (!prep.program) return fail(prep.error);
    Env::Frame frame;
    // Les parametres : dans l'ordre (Moyenne(1, 2)) ou par leur nom (Moyenne(b := 2, a := 1)).
    std::vector<const LocalVar*> inputs;
    for (const auto& l : prep.locals) if (l.section == LocalVar::Section::Input) inputs.push_back(&l);
    std::vector<bool> given(inputs.size(), false);
    std::size_t next = 0;
    for (const auto& [name, value] : args) {
        std::size_t k = inputs.size();
        if (name.empty()) {
            if (next >= inputs.size())
                return fail("trop d'arguments (" + std::to_string(args.size()) + " pour " + std::to_string(inputs.size()) + ")");
            k = next++;
        } else {
            for (std::size_t j = 0; j < inputs.size(); ++j)
                if (upper(inputs[j]->name) == upper(name)) k = j;
            if (k == inputs.size()) return fail("param\xC3\xA8tre inconnu : " + name);
        }
        sim::Value v = sim::Value::defaultOf(typereg::simTypeOf(inputs[k]->type));
        v.assignFrom(value);
        frame[upper(inputs[k]->name)] = v;
        given[k] = true;
    }
    env_->frames.push_back(&frame);
    std::string initWhy;
    for (std::size_t j = 0; j < inputs.size(); ++j)
        if (!given[j]) frame[upper(inputs[j]->name)] = localInitial(*inputs[j], *env_, &initWhy);
    // Une fonction n'a pas de memoire : ses VAR et VAR_TEMP repartent a chaque appel.
    for (const auto& l : prep.locals)
        if (l.section != LocalVar::Section::Input) frame[upper(l.name)] = localInitial(l, *env_, &initWhy);
    if (!f.returnType.empty()) frame[upper(f.name)] = sim::Value::defaultOf(rt);
    auto saved = std::move(env_->diagnostics);
    env_->diagnostics.clear();
    const std::string previous = source_;
    source_ = source;
    // 1.11.14 : la Console - la fonction est la source de ce qu'elle dit, a sa ligne.
    struct OriginBack {
        CodeOrigin& slot;
        CodeOrigin  saved;
        ~OriginBack() { slot = std::move(saved); }
    } originBack{origin_, origin_};
    origin_.name = source;
    origin_.function = f.id;
    origin_.script = kNoId;
    const sim::ExecTrace callerTrace = trace_;
    trace_ = {};
    ++depth_;
    sim::RunLimits limits;
    limits.maxIterationsPerLoop = 100000;
    limits.maxStatementsPerScan = 200000;
    limits.trace = &trace_;
    const auto run = sim::execute(*prep.program, *env_, limits);
    --depth_;
    trace_ = callerTrace;
    source_ = previous;
    env_->frames.pop_back();
    auto mine = std::move(env_->diagnostics);
    env_->diagnostics = std::move(saved);
    ++runs_[f.id];
    std::string why = initWhy;
    failLine = -1;
    for (const auto& d : mine)
        if (d.severity == sim::Diagnostic::Severity::Error) {
            why = (d.line ? "ligne " + std::to_string(d.line) + " : " : std::string{}) + frenchSimMessage(d.message);
            if (d.line) failLine = static_cast<int>(d.line);
            break;
        }
    if (why.empty() && !run.completed) why = "arr\xC3\xAAt\xC3\xA9" " : budget d'instructions d\xC3\xA9pass\xC3\xA9 (boucle sans fin ?)";
    if (!why.empty()) return fail(why);
    if (!f.returnType.empty()) result = frame[upper(f.name)];
    return true;
}

bool Runtime::runScript(const Script& sc, const std::string& source, double now, std::string* error) {
    if (sc.lang != ScriptLang::ST) {
        log("Script", source, std::string(scriptLangKey(sc.lang)) + " : \xC3\xA9" "dit\xC3\xA9 et v\xC3\xA9rifi\xC3\xA9, non ex\xC3\xA9" "cut\xC3\xA9 en simulation");
        return true;
    }
    if (depth_ >= kMaxDepth) {
        const std::string why = "appel circulaire : " + sc.name + " (plus de " + std::to_string(kMaxDepth) + " niveaux)";
        if (error) *error = why;
        errors_[sc.id] = why;
        if (abort_.empty()) log("Erreur", source, why);
        abort_ = why;
        return false;
    }
    ++depth_;
    // Lot 13 : chaque execution, mesuree (onglet Performances de la simulation).
    const auto t0 = std::chrono::steady_clock::now();
    XPG_PORTEE_TEXTE("script IHM", source);   // 1.10.2 (CR) : un script bloque se voit dans la pile
    // 1.11.2 (BLK) : la raison, meme quand l'appelant n'en veut pas (les scripts
    // cycliques et "sur changement" passent nullptr) : la trace du rapport la dit.
    std::string reason;
    std::string* why = error ? error : &reason;
    // 1.11.14 : la Console - ce script est la source de ce qu'il dit (sa vue, s'il en a une).
    const CodeOrigin callerOrigin = origin_;
    origin_ = CodeOrigin{};
    origin_.script = sc.id;
    origin_.view = project_ ? project_->viewOfScript(sc.id) : kNoId;
    origin_.name = origin_.view != kNoId ? source : sc.name;
    std::string composed;                                            // 1.11.18 (lot 3) : ses declarations du modele
    const bool ok = runStatements(decl::codeOf(sc, composed), source, now, why, sc.id);
    origin_ = callerOrigin;
    if (!ok) XPG_TRACE(Script, "erreur du script %s : %s", source.c_str(), why->empty() ? "(raison inconnue)" : why->c_str());
    auto& sp = perf_.scripts[sc.id];
    if (sp.name.empty()) sp.name = source;
    sp.time.add(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    --depth_;
    return ok;
}

bool Runtime::callScript(std::string_view name, double now, std::string* error) {
    now_ = std::max(now_, now);
    const Script* sc = project_ ? project_->generalScript(name) : nullptr;
    if (!sc) {
        const std::string why = "script g\xC3\xA9n\xC3\xA9ral '" + std::string(name) + "' introuvable";
        if (error) *error = why;
        log("Erreur", source_, why);
        return false;
    }
    AliasGuard guard(env_->aliases, nullptr);
    return runScript(*sc, "script " + sc->name, now, error);
}

void Runtime::runViewScript(const View& v, std::string_view event, double now) {
    AliasGuard guard(env_->aliases, scopePtr(v.id));
    // Lot API 8 : un script de vue part avec la vue (ouverture, fermeture, cycle),
    // pas d'un geste : IHM_EXPORTER n'y demande pas ou enregistrer.
    struct ExportAskOff { int& n; int was; ~ExportAskOff() { n = was; } } exportAsk{exportAsk_, exportAsk_};
    exportAsk_ = 0;
    for (const auto& sc : v.scripts) {
        if (sc.event != event) continue;
        // Un script emprunte (lot 6) : "Vue_Production.OnOpen (Modele_Conduite)".
        std::string source = v.name + "." + std::string(event);
        if (project_)
            if (const Id owner = project_->viewOfScript(sc.id); owner != kNoId && owner != v.id)
                if (const auto* ov = project_->view(owner)) source += " (" + ov->name + ")";
        (void)runScript(sc, source, now, nullptr);
    }
}

namespace {
// Ce que demande une operation lancee par l'operateur (securite active).
std::string_view permissionFor(Operation op) {
    switch (op) {
        case Operation::Toggle: case Operation::Set: case Operation::Reset: case Operation::Increment:
        case Operation::Decrement: case Operation::Assign:
        case Operation::Maths: case Operation::Keyboard:        // 1.11.7
            return "Piloter";
        case Operation::Navigate: case Operation::Popup: case Operation::ChangePopup:
        case Operation::NavigateBack: case Operation::NavigateForward: case Operation::NavigateHome:   // lot 12
            return "Naviguer";
        case Operation::RunScript: case Operation::CallScript:
            return "Scripts";
        case Operation::AckAlarm:
        case Operation::ShelveAlarm:            // lot 11
        case Operation::UnshelveAlarm:
            return "Acquitter";
        case Operation::LoadRecipe:
            return "Recettes";
        case Operation::RequestResource:
            return "Administrer";     // une ressource de plus dans le projet
        default:
            return {};        // journaliser, fermer une popup, changer d'utilisateur : libres
    }
}
} // namespace

void Runtime::fire(const View& v, const Object* o, const Action& a, double now, bool byUser) {
    // 1.11 (REP) : les $ d'un repere sont transparents ($V[2]$.Cmd ecrit V[2].Cmd) ;
    // la valeur, la condition et l'expression surveillee passent par Expression::compile.
    const auto targetMode = operationWritesVariable(a.operation) ? markers::Mode::Expression : markers::Mode::Text;   // comme dup::fields
    // 1.11.7 : une variable visee tapee "=Vanne.CMD_OUV" (sa case a la pastille fx) : Vanne.CMD_OUV.
    if (operationWritesVariable(a.operation) && !a.target.empty() && (a.target.front() == '=' || a.target.front() == ' ')) {
        Action plain = a;
        plain.target = targetVariable(a.target);
        if (plain.target != a.target) {
            fire(v, o, plain, now, byUser);
            return;
        }
    }
    if (a.target.find('$') != std::string::npos && !markers::find(a.target, targetMode).empty()) {
        Action plain = a;
        plain.target = markers::strip(a.target, targetMode);
        fire(v, o, plain, now, byUser);
        return;
    }
    AliasGuard aliasGuard(env_->aliases, scopePtr(v.id));
    // Lot API 8 : l'action d'un geste de l'operateur (clic, double clic, appui
    // long) et le script qu'elle execute : IHM_EXPORTER y demande ou ; une action
    // qui part seule (timer, front, ouverture de vue...) : jamais, meme au milieu d'un geste.
    struct ExportAskScope { int& n; int was; ~ExportAskScope() { n = was; } } exportAsk{exportAsk_, exportAsk_};
    exportAsk_ = byUser ? exportAsk_ + 1 : 0;
    if (!a.guard.empty() && !evalBool(a.guard, false)) return;
    const std::string source = where(v, o);
    // Lancee par l'operateur, sous securite : sa permission d'abord.
    if (byUser && project_ && project_->security.enabled) {
        const auto need = permissionFor(a.operation);
        if (!need.empty() && !permitted(need)) {
            event("Acc\xC3\xA8s refus\xC3\xA9", source,
                  std::string(operationLabel(a.operation)) + " : permission \xC2\xAB " + std::string(need) + " \xC2\xBB requise ("
                      + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")");
            return;
        }
    }
    const auto currentValue = [&](sim::Value& out) { return env_->read(a.target, out); };
    switch (a.operation) {
        case Operation::Toggle:
        case Operation::Set:
        case Operation::Reset: {
            sim::Value cur;
            const bool known = currentValue(cur);
            const bool next = a.operation == Operation::Set ? true
                            : a.operation == Operation::Reset ? false
                            : !(known && cur.isTruthy());
            if (write(a.target, sim::Value::boolean(next), source))
                log("Action", source, std::string(operationLabel(a.operation)) + " " + a.target + " = " + (next ? "TRUE" : "FALSE"));
            break;
        }
        case Operation::Increment:
        case Operation::Decrement: {
            sim::Value cur;
            if (!currentValue(cur)) { log("Erreur", source, a.target + " : variable inconnue"); break; }
            double step = 1;
            if (!a.value.empty()) {
                bool ok = false;
                const auto s = evalText(a.value, &ok);
                if (!ok || !parseNumber(s, step)) { log("Erreur", source, "pas illisible : " + a.value); break; }
            }
            if (a.operation == Operation::Decrement) step = -step;
            sim::Value next = cur.type() == sim::Type::Real
                                  ? sim::Value::real(cur.asReal() + step)
                                  : sim::Value::integer(cur.type() == sim::Type::Unknown ? sim::Type::Int : cur.type(),
                                                        cur.asInteger() + static_cast<std::int64_t>(std::llround(step)));
            if (write(a.target, next, source)) {
                sim::Value after;
                (void)env_->read(a.target, after);
                log("Action", source, a.target + " : " + formatValue(cur) + " \xE2\x86\x92 " + formatValue(after));
            }
            break;
        }
        case Operation::Assign: {
            auto it = expressions_.find(a.value);
            if (it == expressions_.end()) it = expressions_.emplace(a.value, Expression::compile(a.value)).first;
            auto val = it->second.evaluate(*env_);
            if (!val) { log("Erreur", source, a.target + " := " + a.value + " : " + val.error().message()); break; }
            if (write(a.target, *val, source)) log("Action", source, a.target + " := " + formatValue(*val));
            break;
        }
        case Operation::Navigate: {
            const auto* target = project_ ? project_->viewByName(a.target) : nullptr;
            if (!target || !navigate(target->id, a.transition, now, a.value))
                log("Erreur", source, "navigation impossible : vue '" + a.target + "'");
            break;
        }
        case Operation::Popup: {
            const auto* target = project_ ? project_->viewByName(a.target) : nullptr;
            if (!target) { log("Erreur", source, "popup : vue '" + a.target + "' introuvable"); break; }
            // L'objet clique, pour une popup posee "sous l'objet" : dans la vue
            // qui le porte (une popup peut en ouvrir une autre).
            openerView_ = v.id;
            (void)openPopup(target->id, a.transition, now, a.value, a.placement, o ? o->id : kNoId);
            openerView_ = kNoId;
            break;
        }
        case Operation::ChangePopup: {
            const auto* target = project_ ? project_->viewByName(a.target) : nullptr;
            if (!target) { log("Erreur", source, "changer de popup : vue '" + a.target + "' introuvable"); break; }
            (void)changePopup(target->id, a.transition, now, a.value);
            break;
        }
        case Operation::CenterPopup: {
            const auto* target = project_ && !a.target.empty() ? project_->viewByName(a.target) : nullptr;
            if (!centerPopup(target ? target->id : kNoId)) log("Action", source, "centrer : aucune popup ouverte");
            break;
        }
        case Operation::PreviousPopup:
            (void)previousPopup(a.transition, now);
            break;
        // Lot 12 : l'historique de navigation, la vue d'accueil.
        case Operation::NavigateBack: case Operation::NavigateForward: case Operation::NavigateHome: {
            std::string why;
            const std::string previous = source_;
            source_ = source;
            const bool done = a.operation == Operation::NavigateBack ? goBack(a.transition, now, 1, &why)
                            : a.operation == Operation::NavigateForward ? goForward(a.transition, now, &why)
                                                                        : goHome(a.transition, now, &why);
            source_ = previous;
            if (!done) log("Action", source, std::string(operationLabel(a.operation)) + " : " + why);
            break;
        }
        case Operation::CloseAllPopups:
            if (closeAllPopups(a.transition, now) == 0) log("Action", source, "aucune popup ouverte");
            break;
        case Operation::Logout:
            if (user_.empty()) log("Action", source, "d\xC3\xA9" "connecter : personne n'est connect\xC3\xA9");
            else logout(now, "action " + source);
            break;
        case Operation::ClosePopup:
            if (!closePopup(a.transition, now)) log("Action", source, "aucune popup ouverte");
            break;
        case Operation::RunScript: {
            if (depth_ >= kMaxDepth) { log("Erreur", source, "appel circulaire"); break; }
            ++depth_;
            const CodeOrigin callerOrigin = origin_;           // 1.11.14 : la source, pour la Console
            origin_ = CodeOrigin{};
            origin_.name = source + " (script de l'action)";
            origin_.view = v.id;
            origin_.object = o ? o->id : kNoId;
            (void)runStatements(a.value, source + " (script de l'action)", now, nullptr);
            origin_ = callerOrigin;
            --depth_;
            break;
        }
        case Operation::CallScript: {
            const std::string previous = source_;
            source_ = source;
            (void)callScript(a.target, now, nullptr);
            source_ = previous;
            break;
        }
        case Operation::Log:
            log("Journal", source, TextTemplate::compile(a.value).render(*env_));
            break;
        case Operation::AckAlarm: {
            std::string why;
            const auto n = acknowledge(a.target, now, &why);
            if (n == 0) log("Action", source, "acquitter" + (a.target.empty() ? std::string{} : " " + a.target) + " : " + why);
            break;
        }
        case Operation::LoadRecipe: {
            // Le jeu : son nom tel quel, ou une expression qui le donne.
            // 1.11 (REP-9) : sans les $ de ses reperes, comme le fichier de "lier un tableau".
            const std::string value = markers::strip(a.value, markers::Mode::Text);
            std::string record = value;
            const auto* recipe = project_ ? project_->recipeByName(a.target) : nullptr;
            if (recipe && !recipe->record(record)) {
                bool ok = false;
                const auto evaluated = evalText(value, &ok);
                if (ok) record = evaluated;
            }
            std::string why;
            if (!applyRecipe(a.target, record, now, &why)) log("Erreur", source, "recette " + a.target + " : " + why);
            break;
        }
        case Operation::ChangeUser: {
            const std::string target = a.target;
            if (target == "-" || upper(target) == "DECONNEXION" || target == "D\xC3\xA9" "connexion") {
                logout(now, "action " + source);
                break;
            }
            const auto* u = project_ ? project_->userByLogin(target) : nullptr;
            if (u && u->protection == "expression") {
                std::string why;
                (void)login(target, {}, now, &why);
                break;
            }
            if (hooks_.askLogin) hooks_.askLogin(target);
            else log("Action", source, "changer d'utilisateur : la connexion se fait dans le volet Simulation (Utilisateur...)");
            break;
        }
        // ---- lot 6 : les ressources
        case Operation::RequestResource: {
            ResourceRequest rq;
            rq.extensions = parseExtensionFilter(a.value);
            rq.variable = a.target;
            rq.source = source;
            if (!rq.variable.empty() && !env_->exists(rq.variable)) {
                log("Erreur", source, "demander une ressource : variable " + rq.variable + " inconnue");
                break;
            }
            log("Action", source, "demander une ressource" + (a.value.empty() ? std::string{} : " (" + a.value + ")"));
            if (hooks_.requestResource) hooks_.requestResource(rq);
            else log("Action", source, "demander une ressource : pas de s\xC3\xA9lecteur de fichiers ici");
            break;
        }
        case Operation::BindTable: {
            const Object* table = nullptr;
            for (const auto& obj : v.objects)
                if (upper(obj.name) == upper(a.target)) table = &obj;
            if (!table || table->kind != Kind::Table) {
                log("Erreur", source, "lier un tableau : tableau '" + a.target + "' introuvable dans " + v.name);
                break;
            }
            // Le fichier : son nom tel quel, ou une expression qui le donne.
            // 1.11 (REP-9) : sans les $ de ses reperes (mode texte, celui de dup::fields) : $parametres$ -> parametres.
            const std::string value = markers::strip(a.value, markers::Mode::Text);
            std::string file = value;
            if (!file.empty() && project_ && !project_->externalByName(file)) {
                bool ok = false;
                const auto evaluated = evalText(value, &ok);
                if (ok) file = evaluated;
            }
            if (file.empty()) {
                tableSources_.erase(table->id);
                log("Action", source, table->name + " : retour \xC3\xA0 sa source");
                break;
            }
            const auto* ext = project_ ? project_->externalByName(file) : nullptr;
            if (!ext) { log("Erreur", source, "lier un tableau : fichier externe '" + file + "' introuvable"); break; }
            tableSources_[table->id] = ext->name;
            log("Action", source, table->name + " li\xC3\xA9 \xC3\xA0 " + ext->name + (ext->part.empty() ? std::string{} : " (" + ext->part + ")"));
            break;
        }
        case Operation::PlaySound: {
            std::string name = a.target;
            if (project_ && !name.empty() && !project_->resourceByName(name)) {
                bool ok = false;
                const auto evaluated = evalText(a.target, &ok);
                if (ok) name = evaluated;
            }
            if (!project_ || !project_->resourceByName(name)) {
                log("Erreur", source, "jouer un son : ressource '" + name + "' introuvable");
                break;
            }
            (void)playSound(name, source);
            break;
        }
        case Operation::ShowSystem:
            // Lot 10 : le menu natif, sur l'onglet que dit l'action (Reglages par defaut ;
            // 1.9 : Simulation).
            openSystemMenu(systemTabFrom(plain(a.value)), now, source);
            break;
        case Operation::ShowLogin:
            // Lot 12 : le menu natif de connexion, sur l'onglet que dit l'action.
            openLoginMenu(loginTabFrom(trimText(a.value)), now, source);
            break;
        case Operation::SetTheme:
            // Lot 13 : jour, nuit ; vide : l'autre.
            (void)setDisplay("theme", a.target.empty() ? std::string("bascule") : trimText(a.target), now, source);
            break;
        // Lot 16 : un GIF anime de la vue - la cible est son nom ; Rejouer : N fois (0 sans fin).
        case Operation::GifPlay: (void)gifCommand(trimText(a.target), "jouer", -1, now, source); break;
        case Operation::GifPause: (void)gifCommand(trimText(a.target), "pause", -1, now, source); break;
        case Operation::GifStop: (void)gifCommand(trimText(a.target), "arreter", -1, now, source); break;
        // 1.9 : Appliquer copie sur reference (la popup qui porte l'action)
        case Operation::ApplyCopy: {
            std::string why;
            if (applyCopy(trimText(a.target), v.id, now, &why) < 0) log("Erreur", source, why);
            break;
        }
        // 1.11.7 : Maths - la formule, ses references remplacees par leurs chemins, va dans la cible.
        case Operation::Maths: {
            const auto refs = actionkinds::params(a);
            std::string expr = actionkinds::mathsExpression(a.value, refs);
            auto it = expressions_.find(expr);
            if (it == expressions_.end()) it = expressions_.emplace(expr, Expression::compile(expr)).first;
            auto val = it->second.evaluate(*env_);
            if (!val) {
                log("Erreur", source, "maths " + a.target + " := " + a.value + " : " + val.error().message());
                break;
            }
            if (write(a.target, *val, source)) log("Action", source, "maths : " + a.target + " := " + formatValue(*val));
            break;
        }
        // 1.11.7 : le clavier virtuel - le champ de saisie s'ouvre (la cible s'ecrit a la validation).
        case Operation::Keyboard:
            openPrompt(v, o, a, source);
            break;
        case Operation::GifReplay: {
            bool ok = false;
            const std::string n = trimText(a.value).empty() ? std::string("1") : evalText(a.value, &ok);
            (void)gifCommand(trimText(a.target), "rejouer", std::max(0, std::atoi((ok ? n : trimText(a.value).empty() ? std::string("1") : a.value).c_str())), now, source);
            break;
        }
        case Operation::SetLanguage: {
            // Lot 13 : un code (en), un nom (English), "suivante" ; sinon une
            // expression qui donne le code (Langue_Choisie).
            std::string code = trimText(a.target);
            if (!code.empty() && project_ && !project_->languages.find(code) && plain(code) != "suivante") {
                bool known = false;
                for (const auto& l : project_->languages.list) known = known || plain(l.name) == plain(code);
                if (!known) {
                    bool ok = false;
                    const auto evaluated = evalText(code, &ok);
                    if (ok) code = evaluated;
                }
            }
            (void)setLanguage(code.empty() ? std::string("suivante") : code, now, source);
            break;
        }
        // ---- lot 11
        case Operation::ShelveAlarm: {
            // "30; Capteur en essai" : les minutes (une expression), puis la raison (texte a trous).
            const auto semi = a.value.find(';');
            const std::string minutesText = trimText(a.value.substr(0, semi));
            const std::string reason = semi == std::string::npos ? std::string{}
                                                                 : TextTemplate::compile(trimText(a.value.substr(semi + 1))).render(*env_);
            double minutes = 0;
            if (!minutesText.empty() && !parseNumber(minutesText, minutes)) {
                bool ok = false;
                const auto evaluated = evalText(minutesText, &ok);
                if (!ok || !parseNumber(evaluated, minutes)) { log("Erreur", source, "mettre de c\xC3\xB4t\xC3\xA9 : dur\xC3\xA9" "e illisible (" + minutesText + ")"); break; }
            }
            std::string why;
            if (shelve(a.target, minutes, reason, now, &why) == 0) log("Action", source, "mettre de c\xC3\xB4t\xC3\xA9 : " + why);
            break;
        }
        case Operation::UnshelveAlarm: {
            std::string why;
            if (unshelve(a.target, now, &why) == 0) log("Action", source, "remettre en service : " + why);
            break;
        }
        case Operation::SilenceAlarms:
            silenceAlarms(now, source);
            break;
        case Operation::Export: {
            std::string file = a.value;
            std::string why;
            // Lot API 8 : au clic, double clic, appui long, l'option cochee : ou
            // l'enregistrer se demande ; les autres declencheurs partent seuls : jamais.
            if (!exportData(a.target.empty() ? std::string("alarmes") : a.target, file.empty() ? std::string("export_{SYS.Date}") : file,
                            {}, now, source, &why, byUser && a.askWhere))
                log("Erreur", source, "exporter : " + why);
            break;
        }
    }
}

void Runtime::runActions(const View& v, const Object* o, Trigger t, double now) {
    const bool byUser = t == Trigger::Click || t == Trigger::DoubleClick || t == Trigger::LongPress;
    // Lot 10 : un objet d'une instance de symbole qui n'a pas d'action pour ce
    // geste : ce sont celles de l'instance (un clic n'importe ou sur la carte
    // ouvre sa popup ; un bouton de la carte garde les siennes).
    if (byUser && o) {
        const auto answers = [t](const Object& x) {
            return std::any_of(x.actions.begin(), x.actions.end(), [t](const Action& a) { return a.trigger == t; });
        };
        if (!answers(*o))
            for (Id up = o->parent; up != kNoId;) {
                const Object* holder = v.object(up);
                if (!holder) break;
                if (holder->kind == Kind::SymbolInstance && answers(*holder)) { o = holder; break; }
                up = holder->parent;
            }
    }
    const auto& list = o ? o->actions : v.actions;
    if (byUser && o) {
        bool any = false;
        for (const auto& a : list) any = any || a.trigger == t;
        std::string why;
        if (any && !objectAllowed(*o, &why)) {
            event("Acc\xC3\xA8s refus\xC3\xA9", where(v, o), why);
            return;
        }
    }
    // Une copie : une action peut changer de vue, et la vue d'origine n'est
    // plus alors ce que `v` designait.
    const std::vector<Action> copy = list;
    const auto origins = o ? std::vector<std::string>{} : actionOriginNames(v);
    for (std::size_t i = 0; i < copy.size(); ++i) {
        if (copy[i].trigger != t) continue;
        actionOrigin_ = i < origins.size() ? origins[i] : std::string{};
        fire(v, o, copy[i], now, byUser);
    }
    actionOrigin_.clear();
}

std::vector<std::string> Runtime::actionOriginNames(const View& v) const {
    std::vector<std::string> out;
    const View* own = project_ ? project_->view(v.id) : nullptr;
    if (!own || !inherits(*project_, *own)) return out;
    for (const Id id : actionOrigins(*project_, *own)) {
        const auto* ov = project_->view(id);
        out.push_back(ov && id != v.id ? ov->name : std::string{});
    }
    return out;
}

void Runtime::openView(Id id, double now, bool popup) {
    (void)popup;
    const auto* v = viewOf(id);
    if (!v) return;
    ++openCounts_[id];           // lot 9 : Vue.OpenCount
    // Les declencheurs de cette vue repartent de zero : un timer recommence,
    // un front se reinitialise sur la valeur du moment.
    const std::string prefix = "v" + std::to_string(id) + ":";
    for (auto it = triggers_.begin(); it != triggers_.end();)
        it = it->first.rfind(prefix, 0) == 0 ? triggers_.erase(it) : std::next(it);
    // Les timers partent de l'ouverture : le premier tombe une periode plus tard.
    const auto arm = [&](Id owner, const std::vector<Action>& list) {
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i].trigger == Trigger::Timer) {
                auto& st = triggers_[prefix + std::to_string(owner) + ":" + std::to_string(i)];
                st.known = true;
                st.nextFire = now + std::max(10, list[i].delayMs > 0 ? list[i].delayMs : 1000) / 1000.0;
            }
    };
    arm(0, v->actions);
    for (const auto& o : v->objects) arm(o.id, o.actions);
    runViewScript(*v, "OnOpen", now);
    if ((v = viewOf(id)) == nullptr) return;
    const View snapshot = *v;
    runActions(snapshot, nullptr, Trigger::ViewOpen, now);
    for (const auto& o : snapshot.objects)
        if (!o.actions.empty()) runActions(snapshot, &o, Trigger::ViewOpen, now);
    gifsOpenView(snapshot, now);        // lot 16 : les GIF animes "a l'affichage" partent
}

void Runtime::closeView(Id id, double now) {
    const auto* v = viewOf(id);
    if (!v) return;
    const View snapshot = *v;
    runActions(snapshot, nullptr, Trigger::ViewClose, now);
    for (const auto& o : snapshot.objects)
        if (!o.actions.empty()) runActions(snapshot, &o, Trigger::ViewClose, now);
    runViewScript(snapshot, "OnClose", now);
}

bool Runtime::navigate(std::string_view name, const Transition& t, double now) {
    const auto* v = project_ ? project_->viewByName(name) : nullptr;
    if (!v) return false;
    return navigate(v->id, t, now, {});
}

bool Runtime::navigate(Id id, const Transition& t, double now) { return navigate(id, t, now, {}); }

bool Runtime::navigate(Id id, const Transition& t, double now, std::string_view arguments) {
    now_ = std::max(now_, now);
    if (!running_ || !viewOf(id)) return false;
    if (id == current_ && popups_.empty() && arguments.empty()) return true;
    if (depth_ >= kMaxDepth) {
        log("Erreur", source_, "navigation circulaire : plus de " + std::to_string(kMaxDepth) + " navigations en cha\xC3\xAEne");
        return false;
    }
    ++depth_;
    // Lot 8 : les parametres se resolvent chez l'appelant (un alias de la vue
    // qui navigue), avant de fermer quoi que ce soit.
    auto scope = buildScope(*viewOf(id), arguments);
    while (!slots_.empty()) {
        const Id p = slots_.back().view;
        closeView(p, now);           // ses OnClose voient encore ses parametres
        if (slots_.empty()) break;
        lastPopupPos_[slots_.back().view] = {slots_.back().x, slots_.back().y};
        slots_.pop_back();
        syncPopupIds();
    }
    const Id old = current_;
    if (old != kNoId) closeView(old, now);
    // Lot 12 : l'historique - la vue quittee s'empile (Precedent y revient) ; une
    // navigation ordinaire oublie Suivant.
    if (!historyMove_ && old != kNoId && old != id) {
        back_.push_back({old, currentArgs_});
        if (back_.size() > 50) back_.erase(back_.begin());
        forward_.clear();
    }
    currentArgs_ = std::string(arguments);
    current_ = id;
    currentScope_ = std::move(scope);
    previousView_ = old;         // lot 9 : SYS.PreviousView, SYS.NavigationCount
    ++navigations_;
    // La saisie en cours part avec la vue qu'on quitte.
    forms_.clear();
    focused_ = kNoId;
    const auto* from = viewOf(old);
    const auto* to = viewOf(id);
    std::string msg = (from ? from->name : std::string("(aucune)")) + " \xE2\x86\x92 " + (to ? to->name : std::string{});
    if (!arguments.empty()) msg += " (" + std::string(arguments) + ")";
    if (t.kind != TransitionKind::Instant)
        msg += " (" + std::string(transitionLabel(t.kind)) + ", " + std::to_string(t.durationMs) + " ms)";
    log("Navigation", source_, msg);
    if (t.kind != TransitionKind::Instant && t.durationMs > 0 && old != kNoId)
        animation_ = Animation{old, id, t, now, false, false};
    else
        animation_.reset();
    openView(id, now, false);
    --depth_;
    return true;
}

// ============================================== les popups (lot 8 : slots) ==
double PopupSlot::width(const View& v) const { return v.width; }
double PopupSlot::height(const View& v) const { return v.height + (popupSettingsOf(v).titleBar ? kPopupTitleHeight : 0.0); }

void Runtime::syncPopupIds() {
    popups_.clear();
    for (const auto& sl : slots_) popups_.push_back(sl.view);
}

std::shared_ptr<const Scope> Runtime::scopePtr(Id view) const {
    for (auto it = slots_.rbegin(); it != slots_.rend(); ++it)
        if (it->view == view) return it->scope;
    return view == current_ ? currentScope_ : nullptr;
}

const Scope* Runtime::viewScope(Id view) const { return scopePtr(view).get(); }

void Runtime::placeSlot(PopupSlot& slot, const View* openerView) {
    const View* pv = viewOf(slot.view);
    if (!pv) return;
    const View* base = viewOf(current_);
    const double bw = base ? base->width : pv->width, bh = base ? base->height : slot.height(*pv);
    const double w = slot.width(*pv), h = slot.height(*pv);
    const std::string where = plain(slot.placement);
    constexpr double margin = 16.0;
    double x = (bw - w) / 2.0, y = (bh - h) / 2.0;
    if (where == "objet" && slot.opener != kNoId && openerView) {
        if (const auto* o = openerView->object(slot.opener)) {
            Box b = o->box();
            // L'objet est dans une popup : ses coordonnees sont celles de la popup.
            for (const auto& other : slots_)
                if (other.view == openerView->id) {
                    b.x += other.x;
                    b.y += other.y + (popupSettingsOf(*openerView).titleBar ? kPopupTitleHeight : 0.0);
                }
            x = b.x;
            y = b.y + b.h + 6.0;
            if (y + h > bh && b.y - 6.0 - h >= 0) y = b.y - 6.0 - h;   // pas la place dessous : au-dessus
        }
    } else if (where == "haut-gauche") { x = margin; y = margin; }
    else if (where == "haut-droite") { x = bw - w - margin; y = margin; }
    else if (where == "bas-gauche") { x = margin; y = bh - h - margin; }
    else if (where == "bas-droite") { x = bw - w - margin; y = bh - h - margin; }
    else if (where == "derniere") {
        if (const auto it = lastPopupPos_.find(slot.view); it != lastPopupPos_.end()) { x = it->second.first; y = it->second.second; }
    } else if (const auto comma = where.find(','); comma != std::string::npos) {
        double px = 0, py = 0;
        if (parseNumber(where.substr(0, comma), px) && parseNumber(where.substr(comma + 1), py)) { x = px; y = py; }
    }
    // Toujours dans la vue du dessous (quand elle y tient).
    slot.x = std::clamp(x, 0.0, std::max(0.0, bw - w));
    slot.y = std::clamp(y, 0.0, std::max(0.0, bh - h));
}

bool Runtime::openPopup(Id id, const Transition& t, double now) { return openPopup(id, t, now, {}, {}, kNoId); }

bool Runtime::openPopup(Id id, const Transition& t, double now, std::string_view arguments, std::string_view placement, Id opener) {
    now_ = std::max(now_, now);
    const View* v = viewOf(id);
    if (!running_ || !v) return false;
    if (id == current_) {
        log("Erreur", source_, "popup : " + v->name + " est la vue affich\xC3\xA9" "e");
        return false;
    }
    // DEJA OUVERTE : elle revient devant, avec les parametres donnes.
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].view != id) continue;
        if (!arguments.empty()) {
            slots_[i].arguments = std::string(arguments);
            slots_[i].scope = buildScope(*v, arguments);
            captureCopies(slots_[i], *v, arguments);   // 1.9 : une nouvelle capture
        }
        const bool moved = raisePopup(i);
        log("Navigation", source_, "popup " + v->name + (moved ? " ramen\xC3\xA9" "e devant" : " d\xC3\xA9j\xC3\xA0 ouverte"));
        return true;
    }
    if (depth_ >= kMaxDepth) { log("Erreur", source_, "ouverture de popup circulaire"); return false; }
    ++depth_;
    const Id under = topView();
    PopupSlot slot;
    slot.view = id;
    slot.arguments = std::string(arguments);
    slot.placement = placement.empty() ? popupSettingsOf(*v).placement : std::string(placement);
    slot.opener = opener;
    slot.scope = buildScope(*v, arguments);
    captureCopies(slot, *v, arguments);   // 1.9 : les parametres Copie / Les deux
    placeSlot(slot, openerView_ != kNoId ? viewOf(openerView_) : nullptr);
    slots_.push_back(std::move(slot));
    syncPopupIds();
    log("Navigation", source_, "popup " + v->name + (arguments.empty() ? std::string{} : " (" + std::string(arguments) + ")"));
    if (t.kind != TransitionKind::Instant && t.durationMs > 0) animation_ = Animation{under, id, t, now, true, false};
    openView(id, now, true);
    --depth_;
    return true;
}

bool Runtime::changePopup(Id id, const Transition& t, double now, std::string_view arguments) {
    now_ = std::max(now_, now);
    if (slots_.empty()) return openPopup(id, t, now, arguments, {}, kNoId);
    const View* v = viewOf(id);
    if (!running_ || !v) return false;
    const std::size_t index = slots_.size() - 1;
    // La meme popup avec d'autres parametres (Armoire B apres Armoire A) : un
    // changement comme un autre (Popup precedente y revient).
    const bool same = slots_[index].view == id;
    if (same && (arguments.empty() || arguments == slots_[index].arguments)) return true;
    if (!same && (id == current_ || std::find(popups_.begin(), popups_.end(), id) != popups_.end())) {
        log("Erreur", source_, "changer de popup : " + v->name + " est d\xC3\xA9j\xC3\xA0 affich\xC3\xA9" "e");
        return false;
    }
    if (depth_ >= kMaxDepth) { log("Erreur", source_, "changement de popup circulaire"); return false; }
    ++depth_;
    auto scope = buildScope(*v, arguments);
    const Id old = slots_[index].view;
    closeView(old, now);
    if (index >= slots_.size() || slots_[index].view != old) { --depth_; return false; }   // OnClose a tout change
    auto& sl = slots_[index];
    sl.history.push_back(PopupSlot::Back{sl.view, sl.x, sl.y, sl.arguments, sl.scope, sl.copies});
    lastPopupPos_[old] = {sl.x, sl.y};
    sl.view = id;
    sl.arguments = std::string(arguments);
    sl.scope = std::move(scope);
    sl.copies.clear();
    captureCopies(sl, *v, arguments);   // 1.9
    // Au meme endroit, ramenee dans la vue si la nouvelle est plus grande.
    if (const View* base = viewOf(current_)) {
        sl.x = std::clamp(sl.x, 0.0, std::max(0.0, base->width - sl.width(*v)));
        sl.y = std::clamp(sl.y, 0.0, std::max(0.0, base->height - sl.height(*v)));
    }
    syncPopupIds();
    const auto* ov = viewOf(old);
    log("Navigation", source_, "popup " + (ov ? ov->name : std::string{}) + " \xE2\x86\x92 " + v->name
                                   + (arguments.empty() ? std::string{} : " (" + std::string(arguments) + ")"));
    if (t.kind != TransitionKind::Instant && t.durationMs > 0 && old != id) animation_ = Animation{old, id, t, now, true, false};
    openView(id, now, true);
    --depth_;
    return true;
}

bool Runtime::previousPopup(const Transition& t, double now) {
    now_ = std::max(now_, now);
    if (!running_ || slots_.empty() || slots_.back().history.empty()) {
        log("Action", source_, "popup pr\xC3\xA9" "c\xC3\xA9" "dente : aucune");
        return false;
    }
    const std::size_t index = slots_.size() - 1;
    const auto back = slots_[index].history.back();
    const bool elsewhere = back.view != slots_[index].view
                        && (back.view == current_ || std::count(popups_.begin(), popups_.end(), back.view) != 0);
    if (elsewhere || !viewOf(back.view)) {
        slots_[index].history.pop_back();
        log("Erreur", source_, "popup pr\xC3\xA9" "c\xC3\xA9" "dente : elle est d\xC3\xA9j\xC3\xA0 affich\xC3\xA9" "e ou n'existe plus");
        return false;
    }
    if (depth_ >= kMaxDepth) return false;
    ++depth_;
    const Id old = slots_[index].view;
    closeView(old, now);
    if (index >= slots_.size() || slots_[index].view != old) { --depth_; return false; }
    auto& sl = slots_[index];
    sl.history.pop_back();
    lastPopupPos_[old] = {sl.x, sl.y};
    sl.view = back.view;
    sl.x = back.x;
    sl.y = back.y;
    sl.arguments = back.arguments;
    sl.scope = back.scope;
    sl.copies = back.copies;   // 1.9 : la copie reprend ou elle en etait
    syncPopupIds();
    const auto* ov = viewOf(old);
    const auto* nv = viewOf(back.view);
    log("Navigation", source_, "popup pr\xC3\xA9" "c\xC3\xA9" "dente : " + (ov ? ov->name : std::string{}) + " \xE2\x86\x92 "
                                   + (nv ? nv->name : std::string{}));
    if (t.kind != TransitionKind::Instant && t.durationMs > 0) animation_ = Animation{old, back.view, t, now, true, false};
    openView(back.view, now, true);
    --depth_;
    return true;
}

bool Runtime::centerPopup(Id view) {
    if (slots_.empty()) return false;
    PopupSlot* sl = nullptr;
    if (view == kNoId) sl = &slots_.back();
    else
        for (auto& x : slots_) if (x.view == view) sl = &x;
    if (!sl) return false;
    const View* pv = viewOf(sl->view);
    const View* base = viewOf(current_);
    if (!pv || !base) return false;
    sl->x = std::max(0.0, (base->width - sl->width(*pv)) / 2.0);
    sl->y = std::max(0.0, (base->height - sl->height(*pv)) / 2.0);
    lastPopupPos_[sl->view] = {sl->x, sl->y};
    log("Action", source_, "popup " + pv->name + " centr\xC3\xA9" "e");
    return true;
}

bool Runtime::closePopup(const Transition& t, double now) {
    if (slots_.empty()) return false;
    return closePopupAt(slots_.size() - 1, t, now);
}

bool Runtime::closePopupAt(std::size_t index, const Transition& t, double now) {
    now_ = std::max(now_, now);
    if (index >= slots_.size()) return false;
    const Id id = slots_[index].view;
    const bool top = index + 1 == slots_.size();
    closeView(id, now);                 // ses OnClose voient encore ses parametres
    // OnClose a pu fermer ou ouvrir d'autres popups : on la retrouve.
    std::size_t at = slots_.size();
    for (std::size_t i = 0; i < slots_.size(); ++i) if (slots_[i].view == id) at = i;
    if (at == slots_.size()) return true;
    lastPopupPos_[id] = {slots_[at].x, slots_[at].y};
    if (focused_ != kNoId)
        if (const auto* v = viewOf(id); v && v->object(focused_)) focused_ = kNoId;
    slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(at));
    syncPopupIds();
    log("Navigation", source_, "popup ferm\xC3\xA9" "e : " + (viewOf(id) ? viewOf(id)->name : std::string{}));
    if (top && t.kind != TransitionKind::Instant && t.durationMs > 0) animation_ = Animation{id, topView(), t, now, true, true};
    else if (top) animation_.reset();
    return true;
}

std::size_t Runtime::closeAllPopups(const Transition& t, double now) {
    std::size_t n = 0;
    while (!slots_.empty() && n < 64) {
        if (!closePopupAt(slots_.size() - 1, slots_.size() == 1 ? t : Transition{}, now)) break;
        ++n;
    }
    return n;
}

void Runtime::movePopup(std::size_t index, double x, double y) {
    if (index >= slots_.size()) return;
    auto& sl = slots_[index];
    const View* pv = viewOf(sl.view);
    const View* base = viewOf(current_);
    if (!pv) return;
    const double bw = base ? base->width : pv->width, bh = base ? base->height : sl.height(*pv);
    sl.x = std::clamp(x, 0.0, std::max(0.0, bw - sl.width(*pv)));
    sl.y = std::clamp(y, 0.0, std::max(0.0, bh - sl.height(*pv)));
    lastPopupPos_[sl.view] = {sl.x, sl.y};
}

bool Runtime::raisePopup(std::size_t index) {
    if (index + 1 >= slots_.size()) return false;
    auto sl = std::move(slots_[index]);
    slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(index));
    slots_.push_back(std::move(sl));
    syncPopupIds();
    return true;
}

double Runtime::animationProgress(double now) const {
    if (!animation_) return 1.0;
    const double d = std::max(1, animation_->spec.durationMs) / 1000.0;
    return ease(animation_->spec.easing, (now - animation_->start) / d);
}

void Runtime::start(double now) {
    // 1.11.14 : la session de la Console - le numero du demarrage depuis l'ouverture de
    // l'application (un onglet Simulation referme puis rouvert ne repart pas a 1).
    static int sessions = 0;
    session_ = ++sessions;
    composed_.clear(); boundCalls_.clear();
    slaveReads_.clear();                // 1.9 : les bascules d'avant ne comptent plus
    forcedIhm_.clear();                 // 1.11.5 : les variables repartent de leur valeur initiale
    prompt_.reset();                    // 1.11.7 : aucun clavier d'action ouvert
    running_ = true;
    now_ = startNow_ = now;
    startWallMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count();
    lastCycle_ = -1;
    journal_.clear();
    triggers_.clear();
    scriptNext_.clear();
    scriptWatch_.clear();
    runs_.clear();
    errors_.clear();
    popups_.clear();
    slots_.clear();
    currentScope_.reset();
    lastPopupPos_.clear();
    forms_.clear();
    controls_.clear();       // lot 9 : glisser, confirmations, compteurs, programmateurs
    overrides_.clear();      // lot 9 : les proprietes ecrites en marche repartent de l'editeur
    backgrounds_.clear();
    gifs_.clear();                      // lot 16
    openCounts_.clear();
    previousView_ = kNoId;
    navigations_ = cycles_ = recipeApplies_ = 0;
    lastRecipe_.clear();
    lastRecipeAt_.clear();
    lastSound_.clear();
    lastAlarm_.clear();
    lastAlarmMessage_.clear();
    lastAlarmAt_.clear();
    publicDepth_ = 0;
    focused_ = kNoId;
    loginAt_ = 0;
    animation_.reset();
    current_ = kNoId;
    pressed_ = kNoId;
    depth_ = 0;
    source_.clear();
    env_->vars.clear();
    alarms_.clear();
    alarmPending_.clear();
    alarmBroken_.clear();
    events_.clear();
    closed_.clear();
    trends_.clear();
    resetLot11();            // lot 11 : graphiques, mises de cote, production, editeurs, export
    resetLot13();            // lot 13 : signature, renouvellement, badge (l'audit et les verrous restent)
    resetLot12();            // lot 12 : l'historique de navigation, le zoom
    resetLogin();            // lot 12 : le menu de connexion repart ferme
    recipeSelection_.clear();
    tableSources_.clear();
    retained_.clear();
    scriptLocals_.clear();                       // 1.10
    functionErrors_.clear();
    systemShown_ = false;
    systemTab_ = 0;              // lot 10 : le menu repart ferme, sur Reglages...
    systemScroll_ = 0;
    systemMessage_.clear();
    systemMessageError_ = false;
    systemClock_.reset();        // ... les reglages du poste (settings_) restent
    simChosen_.clear();          // 1.9 : la page Simulation repart du premier esclave
    simScroll_ = 0;
    simListScroll_ = SimPageShape::kAutoScroll;
    ++simRevision_;
    sleeping_ = sleepNow_ = false;
    soundsPlayed_ = 0;
    user_.clear();
    lastActivity_ = now;
    nextSample_ = -1;
    bound_.clear();              // lot 15 : refait ci-dessous, avec le projet
    boundSource_.clear();
    alarmWatch_.clear();         // 1.9 : les alarmes a surveiller, remises ci-dessous
    watchedProject_.clear();
    watchedArchive_.clear();
    followNext_ = -1;
    if (!project_) return;
    refreshBound();
    // 1.9 : les variables des alarmes du projet suivies des maintenant par les
    // liaisons, sans attendre qu'une vue ou un script les lise.
    watchedProject_ = project_->alarms;
    (void)watchAlarms("projet", watchedProject_);
    // 1.9 (fusion de B et G) : et celles des alarmes des objets cochees, sous la
    // source "objets" (objectAlarmsCycle la remet a chaque regeneration).
    refreshObjectAlarms();
    objectAlarmsWatched_ = objectAlarmsGen_;
    (void)watchAlarms("objets", activeObjectAlarmDefs());
    // La securite : l'utilisateur de depart est connecte sans mot de passe.
    if (project_->security.enabled && !project_->security.startUser.empty()) {
        if (const auto* u = project_->userByLogin(project_->security.startUser); u && u->enabled) {
            user_ = u->login;
            loginAt_ = now;
            event("Connexion", u->login, "utilisateur de d\xC3\xA9part : " + u->login);
            audit("Connexion", "d\xC3\xA9marrage", u->login, {}, "utilisateur de d\xC3\xA9part");   // lot 13
        }
    }
    initVariables();
    // 1.11.15 : la remanence de simulation - les valeurs gardees, rendues avant les
    // scripts de Demarrage (une fois : le demarrage d'apres repart des valeurs initiales).
    lastRestore_.reset();
    persistPending_.clear();
    if (startData_) {
        // 1.11.18 (lot 5) : les declarations Persistantes attendent la premiere execution de leur script.
        const auto variables = takePersistent(*startData_);
        lastRestore_ = simdata::restoreVariables(*project_, variables, [this](const std::string& path) { return ihmSlot(path); });
        if (!persistPending_.empty())
            lastRestore_->notes.push_back(std::to_string(persistPending_.size()) + " d\xC3\xA9" "claration(s) Persistante(s) rendue(s) \xC3\xA0 la premi\xC3\xA8re ex\xC3\xA9" "cution de leur script");
        startData_.reset();
        const std::string label = startLabel_.empty() ? std::string("Donn\xC3\xA9" "es de simulation restaur\xC3\xA9" "es") : startLabel_;
        const std::string kind = startLabel_.empty() ? std::string("Simulation") : std::string("R\xC3\xA9manence");
        logAt(LogLevel::Info, kind, {}, label + " : " + lastRestore_->summary());
        for (const auto& w : lastRestore_->warnings) logAt(LogLevel::Warning, kind, {}, w);
    }
    log("Syst\xC3\xA8me", {}, "IHM d\xC3\xA9marr\xC3\xA9" "e : " + std::to_string(project_->programs.variables.size())
                                 + " variable(s) IHM, " + std::to_string(project_->programs.scripts.size())
                                 + " script(s) g\xC3\xA9n\xC3\xA9raux");
    for (const auto& sc : project_->programs.scripts) {
        if (sc.event == "Demarrage") (void)runScript(sc, "script " + sc.name, now, nullptr);
        if (sc.event == "Cyclique") scriptNext_[sc.id] = now + std::max(10, sc.periodMs) / 1000.0;
    }
    Id first = project_->config.startView;
    // Lot 12 : l'utilisateur de depart ouvre la vue de demarrage de son groupe.
    if (const Id home = homeView(); home != kNoId && viewOf(home)) first = home;
    if (!viewOf(first) && !project_->views.empty()) first = project_->views.front().id;
    if (first != kNoId) {
        current_ = first;
        currentScope_ = buildScope(*viewOf(first), {});    // lot 8 : ses parametres par defaut
        log("Navigation", {}, "vue de d\xC3\xA9marrage : " + viewOf(first)->name);
        openView(first, now, false);
    }
}

void Runtime::initVariables() {
    if (!project_) return;
    aggregates_.clear();
    boundsReported_.clear();
    // Les variables IHM, a leur valeur initiale (une expression : 0, TRUE, T#5s, 'Azote').
    // Lot 16 : une structure ou un tableau, case par case (Four1.Temperature,
    // Consignes[3]) ; une meme valeur initiale ne se calcule qu'une fois.
    std::map<std::string, std::optional<sim::Value>> computed;
    const auto slot = [&](const std::string& name, const std::string& type, const std::string& initial, const std::string& owner) {
        sim::Value v = sim::Value::defaultOf(typereg::simTypeOf(type));
        if (!initial.empty()) {
            auto it = computed.find(initial);
            if (it == computed.end()) {
                auto init = Expression::compile(initial).evaluate(*env_);
                it = computed.emplace(initial, init ? std::optional<sim::Value>(*init) : std::nullopt).first;
            }
            if (it->second) v.assignFrom(*it->second);
            else log("Erreur", "variable " + owner, "valeur initiale illisible : " + initial);
        }
        env_->vars[upper(name)] = v;
    };
    for (const auto& var : project_->programs.variables) {
        if (!types::isComposite(var.type)) {
            slot(var.name, var.type, var.initial, var.name);
            continue;
        }
        // 1.10 (decision 15) : une variable de type enumeration vaut un DINT, le nombre de
        // sa valeur ; sa valeur initiale s'ecrit Auto, T_MODE#Auto, le texte ou le nombre
        // (vide : la premiere valeur declaree).
        if (const auto* e = findEnumeration(*project_, var.type)) {
            std::int64_t n = e->values.empty() ? 0 : e->values.front().value;
            if (!var.initial.empty() && !enumNumberOf(*e, var.initial, n))
                log("Erreur", "variable " + var.name, "valeur initiale illisible : " + var.initial + " (pas une valeur de " + e->name + ")");
            slot(var.name, "DINT", std::to_string(n), var.name);
            continue;
        }
        std::vector<types::Aggregate> aggs;
        std::string error;
        const auto leaves = types::leafVariables(*project_, var, &aggs, nullptr, &error);
        if (!error.empty()) {
            log("Erreur", "variable " + var.name, error);
            continue;
        }
        for (const auto& leaf : leaves) slot(leaf.name, leaf.type, leaf.initial, var.name);
        for (auto& a : aggs) {
            std::string key = upper(a.path);
            aggregates_[std::move(key)] = std::move(a);
        }
    }
}

const types::Aggregate* Runtime::aggregate(std::string_view path) const {
    const auto it = aggregates_.find(upper(path));
    return it == aggregates_.end() ? nullptr : &it->second;
}

// Lot 16 : le chemin designe-t-il une case hors des bornes d'un tableau IHM
// ("Consignes[12]", "Fours[7].Temperature", "Matrice[5,0]") ?
bool Runtime::outOfBoundsPath(const std::string& path, std::string* why) const {
    for (std::size_t k = path.find('['); k != std::string::npos; k = path.find('[', k + 1)) {
        const auto it = aggregates_.find(upper(path.substr(0, k)));
        if (it == aggregates_.end() || !it->second.array) continue;
        const auto close = path.find(']', k);
        if (close == std::string::npos) return false;
        const std::string inside = path.substr(k + 1, close - k - 1);
        const auto& s = it->second.spec;
        long long idx[2] = {0, 0};
        int n = 0;
        std::size_t from = 0;
        while (n < 2) {
            const auto comma = inside.find(',', from);
            const std::string part = inside.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
            char* end = nullptr;
            idx[n++] = std::strtoll(part.c_str(), &end, 10);
            if (comma == std::string::npos) break;
            from = comma + 1;
        }
        const auto bounds = [&] {
            std::string b = std::to_string(s.low[0]) + ".." + std::to_string(s.high[0]);
            if (s.dims == 2) b += ", " + std::to_string(s.low[1]) + ".." + std::to_string(s.high[1]);
            return b;
        };
        if (n != s.dims) {
            if (why) *why = it->second.path + " a " + std::to_string(s.dims) + " dimension" + (s.dims > 1 ? "s" : "") + " : "
                            + std::to_string(s.dims) + " indice" + (s.dims > 1 ? "s" : "") + " entre crochets";
            return true;
        }
        bool out = idx[0] < s.low[0] || idx[0] > s.high[0];
        if (s.dims == 2) out = out || idx[1] < s.low[1] || idx[1] > s.high[1];
        if (out) {
            if (why) *why = "indice hors des bornes de " + it->second.path + " (" + bounds() + ")";
            return true;
        }
    }
    return false;
}

void Runtime::reportBounds(const std::string& path, const std::string& why) {
    if (!boundsReported_.insert(upper(path)).second) return;
    log("Erreur", source_.empty() ? std::string("IHM") : source_, path + " : " + why);
}

bool Runtime::aggregateRead(const std::string& path, sim::Value& out) {
    // Une propriete : Consignes.Length, Fours[2].Vannes.High, Four1.Words.
    if (const auto dot = path.rfind('.'); dot != std::string::npos)
        if (const auto it = aggregates_.find(upper(path.substr(0, dot))); it != aggregates_.end())
            if (types::propertyValue(it->second, std::string_view(path).substr(dot + 1), out)) return true;
    // Une case hors des bornes : la valeur par defaut de son type.
    std::string why;
    if (!outOfBoundsPath(path, &why)) return false;
    const std::string type = project_ ? types::typeOfPath(*project_, path) : std::string{};
    const sim::Type t = typereg::simTypeOf(type);
    out = sim::Value::defaultOf(t == sim::Type::Unknown ? sim::Type::Int : t);
    reportBounds(path, why + " : lu " + out.display());
    return true;
}

bool Runtime::aggregateAssign(const std::string& target, const std::string& source) {
    const auto t = aggregates_.find(upper(target));
    const auto s = aggregates_.find(upper(source));
    if (t == aggregates_.end() || s == aggregates_.end()) return false;
    if (upper(t->second.typeName) != upper(s->second.typeName)) {
        env_->diagnostics.push_back({sim::Diagnostic::Severity::Error,
                                     target + " := " + source + " : types diff\xC3\xA9rents (" + t->second.typeName + ", " + s->second.typeName + ")",
                                     0, {}});
        return true;       // dit ; rien n'est copie
    }
    const std::string from = upper(source), to = upper(target);
    std::vector<std::pair<std::string, sim::Value>> copies;
    for (auto it = env_->vars.lower_bound(from); it != env_->vars.end() && it->first.compare(0, from.size(), from) == 0; ++it) {
        if (it->first.size() == from.size()) continue;
        const char next = it->first[from.size()];
        if (next != '.' && next != '[') continue;
        copies.emplace_back(to + it->first.substr(from.size()), it->second);
    }
    for (const auto& [name, value] : copies) (void)env_->write(name, value);
    return true;
}

void Runtime::prime(double now) {
    composed_.clear(); boundCalls_.clear();
    now_ = startNow_ = now;
    startWallMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count();
    journal_.clear();
    runs_.clear();
    errors_.clear();
    depth_ = 0;
    readOnly_ = 0;
    abort_.clear();
    source_.clear();
    env_->vars.clear();
    env_->frames.clear();
    retained_.clear();
    scriptLocals_.clear();                       // 1.10
    functionErrors_.clear();
    controls_.clear();       // lot 9
    overrides_.clear();
    backgrounds_.clear();
    gifs_.clear();                      // lot 16
    initVariables();
}

bool Runtime::runFunction(std::string_view name, const std::vector<std::pair<std::string, sim::Value>>& args,
                          sim::Value& result, std::string* why) {
    const auto* f = project_ ? project_->functionByName(name) : nullptr;
    if (!f) {
        if (why) *why = "fonction IHM '" + std::string(name) + "' introuvable";
        return false;
    }
    // Comme depuis un script : une faute (declaration, execution, appel
    // circulaire, argument) remonte ici au lieu d'etre dite une fois.
    const std::string before = abort_;
    abort_.clear();
    ++depth_;
    (void)callFunction(*f, args, result);
    --depth_;
    const std::string fault = abort_;
    abort_ = before;
    if (!fault.empty()) {
        if (why) *why = fault;
        return false;
    }
    return true;
}

void Runtime::stop(double now, const std::string& why) {
    if (!running_) return;
    now_ = std::max(now_, now);
    while (!slots_.empty()) {
        closeView(slots_.back().view, now);
        if (slots_.empty()) break;
        slots_.pop_back();
        syncPopupIds();
    }
    if (current_ != kNoId) closeView(current_, now);
    focused_ = kNoId;
    log("Syst\xC3\xA8me", {}, "IHM arr\xC3\xAAt\xC3\xA9" "e" + (why.empty() ? std::string{} : " (" + why + ")"));
    running_ = false;
    animation_.reset();
    unfollowAll();                      // 1.9 : les liaisons ne suivent plus rien pour l'IHM
}

void Runtime::press(Id object, double now) {
    composed_.clear(); boundCalls_.clear();
    now_ = std::max(now_, now);
    lastActivity_ = now;
    pressed_ = object;
    pressStart_ = now;
    longFired_ = false;
    // Lot 9 : une impulsion ecrit des l'appui.
    if (running_ && object != kNoId)
        if (const auto* v = shownViewOf(object); v && v->object(object)) {
            const View snapshot = *v;
            GestureGuard gesture(*this, where(snapshot, snapshot.object(object)));     // lot 13 : l'audit
            controlPress(snapshot, *snapshot.object(object), now);
        }
}

void Runtime::release(Id object, double now, bool inside) {
    composed_.clear(); boundCalls_.clear();
    now_ = std::max(now_, now);
    const bool wasPressed = running_ && pressed_ == object && object != kNoId;
    const bool click = wasPressed && inside && !longFired_;
    pressed_ = kNoId;
    const auto* v = wasPressed ? shownViewOf(object) : nullptr;
    const auto* o = v ? v->object(object) : nullptr;
    if (!o) return;
    const View snapshot = *v;
    GestureGuard gesture(*this, where(snapshot, snapshot.object(object)));         // lot 13 : l'audit
    // Lot 13 : une commande a signature attend le panneau - la bascule avant de
    // basculer, le bouton apres sa confirmation.
    const bool signs = click && signatureNeeded(snapshot, *snapshot.object(object));
    if (signs && o->kind != Kind::Button) {
        requestSignature(snapshot, *snapshot.object(object), SignatureRequest::Gesture::Click, now);
        return;
    }
    // Lot 9 : l'impulsion retombe (meme relachee dehors) ; une bascule change au clic.
    controlRelease(snapshot, *snapshot.object(object), now, click);
    if (!click) return;
    // Lot 9 : une commande sensible attend sa confirmation (second clic, appui maintenu).
    if (o->kind == Kind::Button && buttonConfirmHolds(*o, now)) return;
    if (signs) {
        requestSignature(snapshot, *snapshot.object(object), SignatureRequest::Gesture::Click, now);
        return;
    }
    // Lot 10 : l'objet Parametres systeme ouvre le menu natif, sur son onglet.
    if (o->kind == Kind::SystemButton) {
        const Object& so = *snapshot.object(object);
        std::string why;
        if (!objectAllowed(so, &why)) {
            event("Acc\xC3\xA8s refus\xC3\xA9", where(snapshot, &so), why);
            return;
        }
        openSystemMenu(systemTabFrom(plain(so.text("tab"))), now, where(snapshot, &so));   // 1.9 : et Simulation
    }
    // Lot 12 : l'objet Menu de connexion ouvre le menu natif, sur son onglet.
    if (o->kind == Kind::LoginMenuButton) {
        const Object& lo = *snapshot.object(object);
        std::string why;
        if (!objectAllowed(lo, &why)) {
            event("Acc\xC3\xA8s refus\xC3\xA9", where(snapshot, &lo), why);
            return;
        }
        openLoginMenu(loginTabFrom(lo.text("tab", "Connexion")), now, where(snapshot, &lo));
    }
    // Lot 11 : le bouton d'export ecrit son fichier ; un clic sur le bandeau choisit
    // l'alarme qu'il montre (la consigne la suit).
    if (o->kind == Kind::ExportButton) {
        const Object& eo = *snapshot.object(object);
        std::string why;
        if (!objectAllowed(eo, &why)) {
            event("Acc\xC3\xA8s refus\xC3\xA9", where(snapshot, &eo), why);
            return;
        }
        const auto aliases = viewAliases(snapshot.id);
        // Lot API 8 : "Demander ou enregistrer" (absent d'un bouton d'avant : oui).
        (void)exportData(eo.text("exportSource", "alarmes"), eo.text("fileName", "export_{SYS.Date}"), eo.text("fileFormat", "CSV"), now,
                         where(snapshot, &eo), &why, eo.flag("askWhere", true));
    }
    if (o->kind == Kind::AlarmBanner)
        if (const int k = bannerAlarm(*snapshot.object(object)); k >= 0) selectAlarm(alarms_[static_cast<std::size_t>(k)].name);
    runActions(snapshot, snapshot.object(object), Trigger::Click, now);
}

void Runtime::doubleClick(Id object, double now) {
    composed_.clear(); boundCalls_.clear();
    now_ = std::max(now_, now);
    const auto* v = shownViewOf(object);
    const auto* o = v ? v->object(object) : nullptr;
    if (!running_ || !o) return;
    const View snapshot = *v;
    GestureGuard gesture(*this, where(snapshot, snapshot.object(object)));         // lot 13 : l'audit
    runActions(snapshot, snapshot.object(object), Trigger::DoubleClick, now);
}

void Runtime::watchTriggers(const View& v, double now, bool cycleTick) {
    AliasGuard aliasGuard(env_->aliases, scopePtr(v.id));
    // Une action peut fermer la vue (naviguer ailleurs) : on s'arrete alors,
    // ses autres declencheurs n'ont plus a tourner.
    const auto shown = [&] {
        return current_ == v.id || std::find(popups_.begin(), popups_.end(), v.id) != popups_.end();
    };
    bool closed = false;
    const auto origins = actionOriginNames(v);
    const auto check = [&](const Object* o, const std::vector<Action>& list) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto& a = list[i];
            actionOrigin_ = !o && i < origins.size() ? origins[i] : std::string{};
            const std::string key = "v" + std::to_string(v.id) + ":" + std::to_string(o ? o->id : 0) + ":" + std::to_string(i);
            if (a.trigger == Trigger::Timer) {
                auto& st = triggers_[key];
                const double period = std::max(10, a.delayMs > 0 ? a.delayMs : 1000) / 1000.0;
                if (!st.known) { st.known = true; st.nextFire = now + period; continue; }
                int guard = 0;
                while (now >= st.nextFire && guard++ < 10) {
                    st.nextFire += period;
                    fire(v, o, a, now);
                    if (!shown()) { closed = true; return; }
                }
                if (now >= st.nextFire) st.nextFire = now + period;   // trop en retard : on repart
            } else if (cycleTick && triggerWatches(a.trigger) && !a.watch.empty()) {
                bool ok = false;
                const std::string value = evalText(a.watch, &ok);
                if (!ok) continue;
                auto& st = triggers_[key];
                if (!st.known) { st.known = true; st.last = value; continue; }
                if (value == st.last) continue;
                const bool wasTrue = st.last == "TRUE" || (st.last != "FALSE" && st.last != "0" && !st.last.empty());
                const bool isTrue = value == "TRUE" || (value != "FALSE" && value != "0" && !value.empty());
                st.last = value;
                const bool go = a.trigger == Trigger::ValueChange || (a.trigger == Trigger::RisingEdge && !wasTrue && isTrue)
                             || (a.trigger == Trigger::FallingEdge && wasTrue && !isTrue);
                if (go) {
                    fire(v, o, a, now);
                    if (!shown()) { closed = true; return; }
                }
            }
        }
    };
    const View snapshot = v;
    check(nullptr, snapshot.actions);
    for (const auto& o : snapshot.objects) {
        if (closed) break;
        if (!o.actions.empty()) check(&o, o.actions);
    }
    actionOrigin_.clear();
}

void Runtime::cycle(double now) {
    if (!project_) return;
    gifsCycle(now);                     // lot 16 : les GIF animes (conditions, fin de lecture)
    // Les scripts generaux : cycliques a leur periode, "sur changement" quand
    // leur expression change.
    for (const auto& sc : project_->programs.scripts) {
        if (sc.event == "Cyclique") {
            auto& next = scriptNext_[sc.id];
            if (next == 0) next = now + std::max(10, sc.periodMs) / 1000.0;
            if (now >= next) {
                next = now + std::max(10, sc.periodMs) / 1000.0;
                (void)runScript(sc, "script " + sc.name, now, nullptr);
            }
        } else if (sc.event == "Changement" && !sc.watch.empty()) {
            bool ok = false;
            const auto value = evalText(sc.watch, &ok);
            if (!ok) continue;
            auto it = scriptWatch_.find(sc.id);
            if (it == scriptWatch_.end()) { scriptWatch_[sc.id] = value; continue; }
            if (it->second != value) {
                it->second = value;
                (void)runScript(sc, "script " + sc.name, now, nullptr);
            }
        }
    }
    // La vue et ses popups : OnCycle, puis fronts et changements.
    std::vector<Id> shown{current_};
    shown.insert(shown.end(), popups_.begin(), popups_.end());
    for (const Id id : shown) {
        const auto* v = viewOf(id);
        if (!v) continue;
        runViewScript(*v, "OnCycle", now);
        if ((v = viewOf(id)) == nullptr) continue;
        if (id != current_ && std::find(popups_.begin(), popups_.end(), id) == popups_.end()) continue;
        watchTriggers(*v, now, true);
    }
    evaluateAlarms(now);
    alarmsCycle(now);        // lot 11 : la fin des mises de cote, les sons repetes
    sampleTrends(now);
    sampleCharts(now);       // lot 11 : chronogrammes, courbes XY, histogrammes
    sampleArchive(now);
    controlsCycle(now);      // lot 9 : compteurs horaires, fleches de tendance, programmateurs
    productionCycle(now);    // lot 11 : compteurs de production
    editorsCycle();          // lot 11 : les valeurs de l'installation, pour les editeurs de recette
    reportsCycle(now);       // lot 14 : les rapports periodiques a leur heure
    ++cycles_;               // lot 9 : SYS.CycleCount
}

void Runtime::tick(double now) {
    if (!running_ || !project_) return;
    composed_.clear(); boundCalls_.clear();                  // le projet a pu changer depuis le dernier appel
    refreshBound();                     // lot 15 : les variables liees a un equipement
    now_ = std::max(now_, now);
    followWatched(now, false);          // 1.9 : les variables des alarmes, suivies en permanence
    slaveWatch(now);                    // 1.9 : les bascules vers les esclaves simules
    if (animation_ && now - animation_->start >= std::max(1, animation_->spec.durationMs) / 1000.0) animation_.reset();
    // Lot 9 : le bouton a confirmation par appui maintenu.
    holdTick(now);
    // L'appui long : une fois par appui, quand sa duree est atteinte.
    if (pressed_ != kNoId && !longFired_) {
        if (const auto* v = shownViewOf(pressed_)) {
            if (const auto* o = v->object(pressed_)) {
                int shortest = -1;
                for (const auto& a : o->actions)
                    if (a.trigger == Trigger::LongPress) {
                        const int d = a.delayMs > 0 ? a.delayMs : 800;
                        shortest = shortest < 0 ? d : std::min(shortest, d);
                    }
                if (shortest >= 0 && now - pressStart_ >= shortest / 1000.0) {
                    longFired_ = true;
                    const View snapshot = *v;
                    GestureGuard gesture(*this, where(snapshot, snapshot.object(pressed_)));   // lot 13 : l'audit
                    runActions(snapshot, snapshot.object(pressed_), Trigger::LongPress, now);
                }
            }
        }
    }
    // Les timers, a chaque appel ; le reste au rythme du cycle IHM.
    std::vector<Id> shown{current_};
    shown.insert(shown.end(), popups_.begin(), popups_.end());
    for (const Id id : shown)
        if (const auto* v = viewOf(id)) watchTriggers(*v, now, false);
    const double period = std::max(10, project_->config.cycleMs) / 1000.0;
    if (lastCycle_ < 0 || now - lastCycle_ >= period - 1e-9) {
        lastCycle_ = now;
        // Lot 13 : le travail du cycle, mesure ; plus long que la periode : un depassement.
        const auto t0 = std::chrono::steady_clock::now();
        cycle(now);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        perf_.cycle.add(ms);
        perf_.periodMs = period * 1000.0;
        if (ms > perf_.periodMs) ++perf_.overruns;
    }
    // Deconnexion automatique apres N minutes sans un clic (lot 10 : le reglage du
    // poste, sinon celui du projet).
    const int idle = autoLogoutMinutes();
    // Lot 13 : l'avertissement des dernieres secondes - dit une fois au journal.
    if (const bool warn = logoutWarning(now); warn && !warned_) {
        warned_ = true;
        log("S\xC3\xA9" "curit\xC3\xA9", "d\xC3\xA9" "connexion automatique",
            user_ + " sera d\xC3\xA9" "connect\xC3\xA9 dans " + std::to_string(static_cast<int>(std::ceil(autoLogoutRemaining(now))))
                + " s (un toucher le garde connect\xC3\xA9)");
    } else if (!warn) {
        warned_ = false;
    }
    if (project_->security.enabled && idle > 0 && !user_.empty() && now - lastActivity_ >= idle * 60.0)
        logout(now, "inactivit\xC3\xA9 (" + std::to_string(idle) + " min)");
    // Lot 10 : la mise en veille (l'ecran s'eteint ; le journal le dit).
    sleepTick(now);
}

// ================================================================ alarmes ====
std::string LiveAlarm::state() const {
    if (!ackRequired) return active ? "Active, sans acquittement" : "Termin\xC3\xA9" "e";
    if (active) return acked ? "Acquitt\xC3\xA9" "e" : "Active";
    return "Disparue, \xC3\xA0 acquitter";
}

std::size_t Runtime::unacknowledged() const noexcept {
    std::size_t n = 0;
    for (const auto& a : alarms_) n += !a.acked;
    return n;
}

int Runtime::highestPriority() const noexcept {
    int best = 0;
    for (const auto& a : alarms_)
        if (!a.acked && (best == 0 || a.priority < best)) best = a.priority;
    return best;
}

void Runtime::closeAlarm(std::size_t index) {
    if (index >= alarms_.size()) return;
    const auto& a = alarms_[index];
    AlarmOccurrence occ;
    occ.alarm = a.alarm;
    occ.name = a.name;
    occ.message = a.message;
    occ.group = a.group;
    occ.category = a.category;
    occ.priority = a.priority;
    occ.appeared = a.appeared;
    occ.acked = a.ackedAt;
    occ.ackedBy = a.ackedBy;
    occ.cleared = a.cleared;
    // 1.10.2 (AL) : un groupe d'alarmes sans archivage ne laisse rien dans l'historique.
    if (history_ && project_ && project_->history.alarms && (a.group.empty() || alarmGroupSettings(*project_, a.group).archive)) {
        history_->alarms.push_back(occ);
        if (history_->alarms.size() > static_cast<std::size_t>(std::max(10, project_->history.maxEntries)) + 64)
            history_->trim(project_->history);
    }
    closed_.push_back(std::move(occ));
    while (closed_.size() > kJournalMax) closed_.pop_front();
    alarms_.erase(alarms_.begin() + static_cast<long>(index));
}

void Runtime::evaluateAlarms(double now) {
    if (!project_) return;
    // 1.9 : les alarmes du projet, puis celles des objets (cochees), par le meme chemin.
    objectAlarmsCycle(now);
    std::vector<std::pair<const AlarmDef*, const ObjectAlarm*>> defs;
    defs.reserve(project_->alarms.size());
    for (const auto& def : project_->alarms) defs.emplace_back(&def, nullptr);
    for (const auto& oa : objectAlarmList())
        if (oa.active) defs.emplace_back(&oa.def, &oa);
    for (const auto& [defPtr, fromObject] : defs) {
        const AlarmDef& def = *defPtr;
        if (def.condition.empty()) continue;
        // Lot 11 : mise de cote, elle n'apparait pas (et ne se temporise pas).
        if (isShelved(def.id)) { alarmPending_.erase(def.id); continue; }
        // Lot 14 : relie a un automate reel, une condition dont une variable n'est
        // pas encore lue (ou ne se lit pas) ne decide rien - pas de fausse alarme
        // au demarrage, avant la premiere lecture.
        if (alarmUndecided(def)) { alarmPending_.erase(def.id); continue; }
        bool ok = false;
        const std::string value = evalText(def.condition, &ok);
        if (!ok) {
            if (alarmBroken_.insert(def.id).second)
                log("Erreur", "alarme " + def.name, "condition illisible : " + def.condition);
            continue;
        }
        alarmBroken_.erase(def.id);
        const bool on = value == "TRUE" || (value != "FALSE" && value != "0" && !value.empty());
        auto it = std::find_if(alarms_.begin(), alarms_.end(), [&](const LiveAlarm& a) { return a.alarm == def.id; });
        if (on) {
            if (it != alarms_.end()) {
                if (!it->active) {                     // revenue avant d'etre acquittee
                    it->active = true;
                    it->cleared.clear();
                    event("R\xC3\xA9" "apparition", def.name, it->message);
                    notice(*it, "R\xC3\xA9" "apparition");         // lot 14
                }
                continue;
            }
            // La temporisation : la condition doit tenir `delayMs`.
            if (def.delayMs > 0) {
                const auto pending = alarmPending_.emplace(def.id, now).first;
                if (now - pending->second < def.delayMs / 1000.0 - 1e-9) continue;
            }
            alarmPending_.erase(def.id);
            LiveAlarm a;
            a.alarm = def.id;
            a.name = def.name;
            // Lot 13 : le message et la consigne dans la langue du moment.
            const auto& langs = project_->languages;
            a.message = def.message.empty() ? def.name
                                            : TextTemplate::compile(formatTemplate(translateText(langs, language_, def.message), *project_)).render(*env_);
            a.group = def.group;
            if (fromObject) {                    // 1.9 : son groupe interne, ses symboles
                a.objectGroup = fromObject->objectGroup;
                a.symbols = fromObject->symbols;
            }
            a.category = def.category;
            a.priority = std::clamp(def.priority, 1, kAlarmPriorities);
            a.active = true;
            // 1.10.2 (AL) : un groupe a acquittement automatique au retour - rien a acquitter.
            a.ackRequired = def.ackRequired && alarmGroupSettings(*project_, def.group).ack != AlarmAckMode::Auto;
            a.acked = !a.ackRequired;
            a.appeared = dateStampOf(now);
            // Lot 11 : la consigne (un texte a trous, rempli a cet instant).
            if (!def.instruction.empty())
                a.instruction = TextTemplate::compile(formatTemplate(translateText(langs, language_, def.instruction), *project_)).render(*env_);
            event("Apparition", def.name, "[" + std::string(alarmPriorityLabel(a.priority)) + "] " + a.message);
            lastAlarm_ = def.name;               // lot 9 : SYS.AlarmLast*
            lastAlarmMessage_ = a.message;
            lastAlarmAt_ = a.appeared.substr(0, 19);
            alarmAppeared(a, now);               // lot 11 : le son de sa priorite
            notice(a, "Apparition");             // lot 14 : les notifications
            alarms_.push_back(std::move(a));
            // Les plus graves en tete, puis les plus recentes.
            std::stable_sort(alarms_.begin(), alarms_.end(), [](const LiveAlarm& x, const LiveAlarm& y) {
                return x.priority != y.priority ? x.priority < y.priority : x.appeared > y.appeared;
            });
        } else {
            alarmPending_.erase(def.id);
            if (it == alarms_.end() || !it->active) continue;
            it->active = false;
            it->cleared = dateStampOf(now);
            event("Disparition", def.name, it->message);
            notice(*it, "Disparition");          // lot 14
            if (it->acked) closeAlarm(static_cast<std::size_t>(it - alarms_.begin()));
        }
    }
    // Une alarme dont la definition a disparu (editee en marche) : terminee.
    //  1.9 : une alarme d'objet decochee, ou dont l'objet a disparu, aussi.
    for (std::size_t i = alarms_.size(); i-- > 0;) {
        const Id id = alarms_[i].alarm;
        if (project_->alarm(id)) continue;
        const ObjectAlarm* oa = objectAlarm(id);
        if (!oa || !oa->active) closeAlarm(i);
    }
}

std::size_t Runtime::acknowledge(std::string_view target, double now, std::string* why) {
    now_ = std::max(now_, now);
    if (project_ && project_->security.enabled && !permitted("Acquitter")) {
        const std::string reason = "permission \xC2\xAB Acquitter \xC2\xBB requise ("
                                 + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")";
        if (why) *why = reason;
        event("Acc\xC3\xA8s refus\xC3\xA9", "acquittement", reason);
        return 0;
    }
    const bool all = target.empty() || target == "*";
    const bool group = target.rfind("groupe:", 0) == 0;
    const std::string groupName = group ? std::string(target.substr(7)) : std::string{};
    // 1.10.2 (AL) : une alarme d'un groupe acquitte « par groupe » acquitte tout son groupe.
    std::string wholeGroup;
    if (!all && !group && project_)
        for (const auto& a : alarms_)
            if (a.name == target && !a.group.empty() && alarmGroupSettings(*project_, a.group).ack == AlarmAckMode::Group) {
                wholeGroup = a.group;
                break;
            }
    std::size_t done = 0, refused = 0;
    for (std::size_t i = 0; i < alarms_.size();) {
        auto& a = alarms_[i];
        // 1.9 : un groupe d'objet ("groupe:Vue.Pompe_3") prend ses alarmes et celles
        // des objets qu'il contient ; les filtres des objets d'alarmes valent aussi.
        const bool match = all || (group ? alarmGroupMatches(a.group, a.objectGroup, a.symbols, groupName)
                                         : a.name == target || (!wholeGroup.empty() && a.group == wholeGroup));
        if (!match || a.acked) { ++i; continue; }
        // 1.10.2 (AL) : le niveau d'acces du groupe pour acquitter.
        if (project_ && project_->security.enabled && !a.group.empty())
            if (const int need = alarmGroupSettings(*project_, a.group).level; need > 0 && level() < need) {
                ++refused;
                ++i;
                continue;
            }
        a.acked = true;
        a.ackedAt = dateStampOf(now_);
        a.ackedBy = user_;
        ++done;
        event("Acquittement", a.name, a.message);
        notice(a, "Acquittement");               // lot 14 : une notification en attente ne part plus
        audit("Acquittement", {}, a.name, a.active ? "active" : "disparue", "acquitt\xC3\xA9" "e");     // lot 13
        if (!a.active) closeAlarm(i);
        else ++i;
    }
    if (done == 0 && why)
        *why = refused > 0 ? "niveau d'acc\xC3\xA8s insuffisant pour acquitter (groupe d'alarmes)"
             : all ? "aucune alarme \xC3\xA0 acquitter" : "rien \xC3\xA0 acquitter pour " + std::string(target);
    return done;
}

// ================================================================ recettes ===
bool Runtime::applyRecipe(std::string_view recipeName, std::string_view recordName, double now, std::string* why) {
    now_ = std::max(now_, now);
    const auto* r = project_ ? project_->recipeByName(recipeName) : nullptr;
    const auto fail = [&](std::string reason) {
        if (why) *why = reason;
        return false;
    };
    if (!r) return fail("recette '" + std::string(recipeName) + "' introuvable");
    const auto* rec = r->record(recordName);
    if (!rec) return fail("jeu '" + std::string(recordName) + "' introuvable dans " + r->name);
    return applyValues(*r, rec->values, rec->name, now, why);
}

// Lot 11 : le coeur d'Appliquer, partage avec l'editeur de recette (qui ecrit les
// valeurs qu'il montre, modifiees ou non).
bool Runtime::applyValues(const Recipe& recipe, const std::vector<std::string>& values, const std::string& label, double now,
                          std::string* why) {
    const auto* r = &recipe;
    const auto fail = [&](std::string reason) {
        if (why) *why = reason;
        return false;
    };
    // TOUT OU RIEN : chaque valeur est verifiee (lisible, dans ses bornes,
    // variable connue) avant d'en ecrire une seule. Un jeu a moitie applique
    // laisserait l'installation dans un etat qu'aucune recette ne decrit.
    std::vector<std::pair<std::string, sim::Value>> writes;
    std::vector<std::string> problems;
    for (std::size_t i = 0; i < r->fields.size(); ++i) {
        const auto& f = r->fields[i];
        const std::string value = i < values.size() ? values[i] : std::string{};
        if (f.variable.empty() || value.empty()) continue;
        auto v = Expression::compile(value).evaluate(*env_);
        if (!v) { problems.push_back(f.name + " : valeur illisible (" + value + ")"); continue; }
        double number = 0, lo = 0, hi = 0;
        const bool numeric = parseNumber(formatValue(*v), number);
        if (numeric && ((!f.min.empty() && parseNumber(f.min, lo) && number < lo) || (!f.max.empty() && parseNumber(f.max, hi) && number > hi))) {
            problems.push_back(f.name + " : " + value + " hors bornes [" + f.min + " ; " + f.max + "]");
            continue;
        }
        if (!env_->exists(f.variable)) { problems.push_back(f.name + " : variable " + f.variable + " inconnue"); continue; }
        writes.emplace_back(f.variable, *v);
    }
    if (!problems.empty()) {
        event("Recette", r->name, label + " refus\xC3\xA9 : " + problems.front()
                                      + (problems.size() > 1 ? " (+" + std::to_string(problems.size() - 1) + ")" : std::string{})
                                      + " ; rien n'est \xC3\xA9" "crit");
        return fail(problems.front());
    }
    // Lot 13 : le journal d'audit - une ligne pour le jeu entier (chaque variable,
    // avant et apres), pas une par variable.
    std::string auditBefore, auditAfter;
    std::size_t written = 0;
    ++auditMute_;
    for (const auto& [variable, value] : writes) {
        sim::Value old;
        const bool known = env_->read(variable, old);
        if (env_->write(variable, value)) {
            ++written;
            sim::Value now2;
            const std::string after = env_->read(variable, now2) ? formatValue(now2) : formatValue(value);
            auditBefore += (auditBefore.empty() ? "" : "; ") + variable + " = " + (known ? formatValue(old) : std::string("?"));
            auditAfter += (auditAfter.empty() ? "" : "; ") + variable + " = " + after;
        } else {
            problems.push_back(variable + " : \xC3\xA9" "criture refus\xC3\xA9" "e");
        }
    }
    --auditMute_;
    audit("Recette", {}, r->name + " / " + label, auditBefore, auditAfter);
    std::string message = label + " appliqu\xC3\xA9 : " + std::to_string(written) + " valeur(s) \xC3\xA9" "crite(s)";
    lastRecipe_ = r->name + "/" + label;       // lot 9 : SYS.RecipeLastApplied
    lastRecipeAt_ = dateStampOf(now).substr(0, 19);
    ++recipeApplies_;
    if (!problems.empty()) message += " ; " + problems.front();
    event("Recette", r->name, message);
    if (!problems.empty()) return fail(problems.front());
    return true;
}

bool Runtime::readRecipe(std::string_view recipeName, std::vector<std::string>& values, std::string* why) {
    const auto* r = project_ ? project_->recipeByName(recipeName) : nullptr;
    if (!r) {
        if (why) *why = "recette '" + std::string(recipeName) + "' introuvable";
        return false;
    }
    values.assign(r->fields.size(), std::string{});
    bool all = true;
    for (std::size_t i = 0; i < r->fields.size(); ++i) {
        sim::Value v;
        if (!r->fields[i].variable.empty() && env_->read(r->fields[i].variable, v)) values[i] = formatValue(v);
        else all = false;
    }
    if (!all && why) *why = "certaines variables sont inconnues";
    return all;
}

// ============================================ gestionnaire de recettes (lot 6) ==
Id Runtime::recipeSelection(Id object) const {
    const auto it = recipeSelection_.find(object);
    if (it == recipeSelection_.end() || !project_) return kNoId;
    // Un jeu supprime entre-temps : plus rien de choisi.
    for (const auto& r : project_->recipes)
        if (r.record(it->second)) return it->second;
    return kNoId;
}

void Runtime::selectRecipeRecord(Id object, Id record) {
    if (record == kNoId) recipeSelection_.erase(object);
    else recipeSelection_[object] = record;
}

void Runtime::recipeManagerClick(Id object, std::string_view part, double now) {
    composed_.clear(); boundCalls_.clear();
    now_ = std::max(now_, now);
    lastActivity_ = now;
    if (!running_ || !project_) return;
    const auto* v = viewOf(topView());
    const auto* o = v ? v->object(object) : nullptr;
    if (!o || o->kind != Kind::RecipeManager) return;
    const View snapshot = *v;
    const Object& obj = *snapshot.object(object);
    const std::string source = where(snapshot, &obj);
    const std::string recipeName = markers::strip(obj.text("recipe"), markers::Mode::Text);   // 1.11 (REP-1) : sans ses $
    const auto* recipe = project_->recipeByName(recipeName);
    if (!recipe) {
        log("Erreur", source, "gestionnaire de recettes : recette '" + recipeName + "' introuvable");
        return;
    }
    if (part.rfind("ligne:", 0) == 0) {
        const auto row = static_cast<std::size_t>(std::max(0, std::atoi(std::string(part.substr(6)).c_str())));
        if (row < recipe->records.size()) recipeSelection_[object] = recipe->records[row].id;
        return;
    }
    if (part.rfind("bouton:", 0) != 0) return;
    const std::string button(part.substr(7));
    std::string why;
    if (!objectAllowed(obj, &why)) { event("Acc\xC3\xA8s refus\xC3\xA9", source, why); return; }
    if (project_->security.enabled && !permitted("Recettes")) {
        event("Acc\xC3\xA8s refus\xC3\xA9", source,
              button + " : permission \xC2\xAB Recettes \xC2\xBB requise ("
                  + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")");
        return;
    }
    const Id sel = recipeSelection(object);
    const auto* rec = sel != kNoId ? recipe->record(sel) : nullptr;
    if (button != "Ajouter" && !rec) {
        log("Action", source, button + " : aucun jeu choisi (cliquer une ligne)");
        return;
    }
    if (button == "Appliquer") {
        std::string reason;
        if (!applyRecipe(recipe->name, rec->name, now, &reason)) log("Erreur", source, "recette " + recipe->name + " : " + reason);
        return;
    }
    RecipeRequest rq;
    rq.op = button == "Ajouter" ? "ajouter" : button == "Modifier" ? "modifier" : button == "Supprimer" ? "supprimer"
          : button == "Lire" ? "lire" : std::string{};
    if (rq.op.empty()) { log("Erreur", source, "bouton inconnu : " + button); return; }
    rq.object = object;
    rq.recipe = recipe->name;
    // Ajouter ne vise pas le jeu choisi : il en cree un.
    rq.record = rec && rq.op != "ajouter" ? rec->id : kNoId;
    rq.recordName = rec && rq.op != "ajouter" ? rec->name : std::string{};
    rq.source = source;
    // Ajouter part des valeurs de l'installation (on enregistre ce qui tourne) ;
    // Lire les prend pour les ranger dans le jeu choisi.
    if (rq.op == "ajouter" || rq.op == "lire") {
        // Les valeurs du jeu sont des expressions : un texte y est entre
        // apostrophes ('Azote'), comme dans le volet Recettes.
        bool all = true;
        rq.values.assign(recipe->fields.size(), std::string{});
        for (std::size_t i = 0; i < recipe->fields.size(); ++i) {
            sim::Value current;
            if (recipe->fields[i].variable.empty() || !env_->read(recipe->fields[i].variable, current)) { all = false; continue; }
            if (current.type() == sim::Type::String) {
                std::string quoted = "'";
                for (const char c : current.asString()) quoted += c == '\'' ? std::string("$'") : std::string(1, c);
                rq.values[i] = quoted + "'";
            } else {
                rq.values[i] = formatValue(current);
            }
        }
        if (!all && rq.op == "lire") log("Action", source, "lire : certaines variables sont inconnues (valeurs vides gard\xC3\xA9" "es)");
    }
    if (!hooks_.recipeRequest) {
        log("Action", source, button + " : l'\xC3\xA9" "cran ne sait pas modifier les recettes ici");
        return;
    }
    hooks_.recipeRequest(rq);
}

// ======================================================= ressources (lot 6) ==
std::vector<std::string> parseExtensionFilter(std::string_view text) {
    std::vector<std::string> out;
    std::string cur;
    const auto flush = [&] {
        std::size_t a = 0;
        while (a < cur.size() && (cur[a] == ' ' || cur[a] == '*' || cur[a] == '.')) ++a;
        std::string e = cur.substr(a);
        while (!e.empty() && e.back() == ' ') e.pop_back();
        for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!e.empty() && std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
        cur.clear();
    };
    for (const char c : text) {
        if (c == ';' || c == ',' || c == ' ' || c == '|') flush();
        else cur += c;
    }
    flush();
    return out;
}

bool extensionAccepted(const std::vector<std::string>& extensions, std::string_view fileName) {
    if (extensions.empty()) return true;
    const auto dot = fileName.rfind('.');
    if (dot == std::string_view::npos) return false;
    std::string ext(fileName.substr(dot + 1));
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

void Runtime::resourceProvided(const ResourceRequest& rq, std::string_view resourceName, double now) {
    now_ = std::max(now_, now);
    if (resourceName.empty()) {
        log("Action", rq.source, "demander une ressource : annul\xC3\xA9");
        return;
    }
    log("Action", rq.source, "ressource ajout\xC3\xA9" "e : " + std::string(resourceName));
    if (!rq.variable.empty()) (void)write(rq.variable, sim::Value::text(std::string(resourceName)), rq.source);
}

const std::string* Runtime::tableSource(Id object) const {
    const auto it = tableSources_.find(object);
    return it == tableSources_.end() ? nullptr : &it->second;
}

std::vector<std::pair<std::string, std::string>> Runtime::systemInfo() const {
    std::vector<std::pair<std::string, std::string>> out;
    const auto count = [](std::size_t n, const char* one, const char* many) {
        return std::to_string(n) + " " + (n > 1 ? many : one);
    };
    if (project_) {
        out.emplace_back("Projet IHM", project_->config.name + "  (version " + project_->config.version + ")");
        out.emplace_back("R\xC3\xA9solution", std::to_string(project_->config.width) + " x " + std::to_string(project_->config.height)
                                                   + "  " + project_->config.orientation);
        out.emplace_back("Cycle IHM", std::to_string(project_->config.cycleMs) + " ms");
    }
    out.emplace_back("Date et heure", dateStampOf(now_));
    const long long up = static_cast<long long>(std::max(0.0, now_ - startNow_));
    char b[48];
    std::snprintf(b, sizeof b, "%lld h %02lld min %02lld s", up / 3600, (up / 60) % 60, up % 60);
    out.emplace_back("En marche depuis", b);
    const auto* cur = viewOf(current_);
    out.emplace_back("Vue courante", (cur ? cur->name : std::string("(aucune)"))
                                         + (popups_.empty() ? std::string{} : "  + " + count(popups_.size(), "popup", "popups")));
    if (project_) {
        out.emplace_back("S\xC3\xA9" "curit\xC3\xA9", project_->security.enabled ? "active" : "inactive");
        out.emplace_back("Utilisateur", user_.empty() ? std::string("personne") : user_ + "  (niveau " + std::to_string(level()) + ")");
    }
    out.emplace_back("Alarmes", count(alarms_.size(), "en cours", "en cours") + ", " + std::to_string(unacknowledged()) + " \xC3\xA0 acquitter");
    out.emplace_back("Automate", plc_ ? "simulateur reli\xC3\xA9" : "aucun (variables IHM seules)");
    if (project_) {
        out.emplace_back("Contenu", count(project_->views.size(), "vue", "vues") + ", "
                                        + count(project_->programs.variables.size(), "variable IHM", "variables IHM") + ", "
                                        + count(project_->programs.scripts.size(), "script g\xC3\xA9n\xC3\xA9ral", "scripts g\xC3\xA9n\xC3\xA9raux"));
        out.emplace_back("Ressources", count(project_->assets.resources.size(), "ressource", "ressources") + ", "
                                           + count(project_->assets.files.size(), "fichier externe", "fichiers externes"));
    }
    out.emplace_back("Journal", count(journal_.size(), "entr\xC3\xA9" "e", "entr\xC3\xA9" "es") + ", "
                                    + count(soundsPlayed_, "son jou\xC3\xA9", "sons jou\xC3\xA9s"));
    return out;
}

// ================================================================ securite ===
const User* Runtime::user() const { return project_ && !user_.empty() ? project_->userByLogin(user_) : nullptr; }

int Runtime::level() const {
    if (!project_ || !project_->security.enabled) return 99;
    const auto* u = user();
    return u ? userLevel(*project_, *u) : 0;
}

bool Runtime::permitted(std::string_view permission) const {
    if (!project_ || !project_->security.enabled) return true;
    const auto* u = user();
    return u && userHas(*project_, *u, permission);
}

bool Runtime::objectAllowed(const Object& o, std::string* why) {
    if (!project_ || !project_->security.enabled) return true;
    const int needed = static_cast<int>(o.number("access", 0));
    if (needed > level()) {
        if (why) {
            std::string groupName;
            for (const auto& g : project_->security.groups) if (g.level == needed) { groupName = g.name; break; }
            *why = o.name + " : niveau d'acc\xC3\xA8s " + std::to_string(needed)
                 + (groupName.empty() ? std::string{} : " (" + groupName + ")") + " requis ; "
                 + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : user_ + " a le niveau " + std::to_string(level()));
        }
        return false;
    }
    const std::string auth = o.text("auth");
    if (!auth.empty() && !evalBool(auth, false)) {
        if (why) *why = o.name + " : autorisation refus\xC3\xA9" "e (" + auth + " est faux)";
        return false;
    }
    return true;
}

bool Runtime::login(std::string_view loginName, std::string_view secret, double now, std::string* why, double unixSeconds) {
    now_ = std::max(now_, now);
    const auto* u = project_ ? project_->userByLogin(loginName) : nullptr;
    // Lot 13 : par ou passe cette connexion (le journal d'audit) - pour cet appel.
    const std::string via = loginVia_.empty() ? std::string("connexion") : loginVia_;
    loginVia_.clear();
    const auto refuse = [&](const std::string& reason) {
        if (why) *why = reason;
        event("Connexion refus\xC3\xA9" "e", std::string(loginName), reason);
        audit("Connexion refus\xC3\xA9" "e", via, std::string(loginName), {}, {}, reason);
        return false;
    };
    if (!u) return refuse("utilisateur inconnu");
    if (!u->enabled) return refuse("compte d\xC3\xA9sactiv\xC3\xA9");
    // Lot 13 : un compte verrouille ne se connecte plus (le temps, ou un administrateur).
    if (const auto* st = accountState(u->login); st && st->locked(epochOf(now))) {
        std::string reason = "compte verrouill\xC3\xA9";
        if (st->lockedUntil > 0) {
            const std::string until = dateStampOf(now + (st->lockedUntil - epochOf(now)));
            reason += " jusqu'\xC3\xA0 " + until.substr(11, 5);
        } else {
            reason += " : un administrateur doit le d\xC3\xA9verrouiller";
        }
        return refuse(reason);
    }
    bool ok = false;
    std::string reason;
    if (u->protection == "dynamique") {
        const double unixTime = unixSeconds >= 0 ? unixSeconds : wallEpoch();
        ok = dynamicCodeMatches(u->secret, secret, unixTime, project_->security.dynamicPeriodS, project_->security.dynamicDigits);
        reason = "code incorrect ou expir\xC3\xA9";
    } else if (u->protection == "expression") {
        ok = !u->expression.empty() && evalBool(u->expression, false);
        reason = "autorisation refus\xC3\xA9" "e : " + u->expression + " est faux";
    } else {
        ok = passwordMatches(u->salt, u->passwordHash, secret);
        reason = u->passwordHash.empty() ? "aucun mot de passe d\xC3\xA9" "fini" : "mot de passe incorrect";
    }
    if (!ok) {
        // Lot 13 : l'echec compte (le verrouillage au N-ieme de suite).
        const bool counts = u->protection != "expression" && !(u->protection == "classique" && u->passwordHash.empty());
        return refuse(reason + (counts ? loginFailed(*u, now) : std::string{}));
    }
    loginSucceeded(*u);
    // Lot 13 : un mot de passe perime (ou a changer a la premiere connexion) - pas
    // encore : le menu de connexion demande le nouveau.
    if (const std::string renew = hmi::renewalReason(project_->security, *u, dayOf(dateStampOf(now))); !renew.empty()) {
        renewal_ = Renewal{u->login, renew};
        event("Mot de passe \xC3\xA0 renouveler", u->login, renew);
        if (why) *why = renew;
        if (!loginShown_) openLoginMenu(LoginTab::Connexion, now, via);
        loginTab_ = LoginTab::Connexion;
        loginForm_.focus = "nouveau";
        for (const char* f : {"nouveau", "confirmation", "secret"}) { loginForm_.text[f].clear(); loginForm_.caret[f] = 0; }
        // La raison est dans l'en-tete du menu ; en bas, ce que le nouveau doit respecter.
        loginSay("\xC3\xA0 respecter : " + passwordRules(project_->security, kMenuPasswordMin), false, now);
        return false;
    }
    completeLogin(*u, now, via);
    return true;
}

void Runtime::completeLogin(const User& account, double now, const std::string& how) {
    const User* u = &account;
    renewal_.reset();
    user_ = u->login;
    lastActivity_ = now;
    loginAt_ = now;
    warned_ = false;
    // Les reponses des objets (permission refusee...) valaient pour l'utilisateur d'avant.
    for (auto& [id, f] : forms_) { f.message.clear(); f.error = false; }
    const auto* g = project_->group(u->group);
    const std::string who = u->login;
    const Id startView = g ? g->startView : kNoId;
    const std::string groupText = g ? g->name + ", niveau " + std::to_string(g->level) : std::string{};
    event("Connexion", who, (u->fullName.empty() ? who : u->fullName) + " connect\xC3\xA9"
                                + (groupText.empty() ? std::string{} : " (" + groupText + ")"));
    audit("Connexion", how, who, {}, groupText);
    loginAfterUserChange();          // lot 12 : les onglets du menu de connexion suivent
    // Lot 12 : la vue de demarrage de son groupe s'ouvre.
    if (startView != kNoId && running_ && viewOf(startView) && startView != current_) {
        const std::string previous = source_;
        source_ = "connexion de " + who;
        (void)navigate(startView, Transition{}, now, {});
        source_ = previous;
    }
}

void Runtime::logout(double now, std::string_view reason) {
    now_ = std::max(now_, now);
    if (user_.empty()) return;
    const std::string who = user_;
    // Lot 13 : le journal d'audit - qui se deconnecte (avant qu'il ne le soit).
    audit("D\xC3\xA9" "connexion", reason.empty() ? std::string("d\xC3\xA9" "connexion") : std::string(reason), who);
    user_.clear();
    loginAt_ = 0;
    // Et ce qu'il avait choisi (une ligne de la gestion des utilisateurs).
    for (auto& [id, f] : forms_) { f.message.clear(); f.error = false; f.confirmUntil = 0; f.selected = kNoId; }
    event("D\xC3\xA9" "connexion", who, reason.empty() ? std::string("d\xC3\xA9" "connect\xC3\xA9") : "d\xC3\xA9" "connect\xC3\xA9 : " + std::string(reason));
    warned_ = false;
    // Lot 13 : une signature en attente ne survit pas a son signataire.
    if (signature_) signatureCancel(now, "d\xC3\xA9" "connexion de " + who);
    loginAfterUserChange();          // lot 12 : les onglets du menu de connexion suivent
}

// ================================================================ courbes ====
void Runtime::addTrendMarker(std::string variable, std::string text, bool forced, bool slave) {
    markers_.push_back({now_, std::move(variable), std::move(text), forced, slave});
    // Les plus vieilles partent (une heure de courbes au plus).
    while (!markers_.empty() && (markers_.size() > 200 || markers_.front().at < now_ - 3600)) markers_.erase(markers_.begin());
}

const std::vector<TrendSeries>* Runtime::trend(Id view, Id object) const {
    const auto it = trends_.find(std::to_string(view) + ":" + std::to_string(object));
    return it == trends_.end() ? nullptr : &it->second;
}

void Runtime::sampleTrends(double now) {
    std::vector<Id> shown{current_};
    shown.insert(shown.end(), popups_.begin(), popups_.end());
    for (const Id id : shown) {
        const auto* v = viewOf(id);
        if (!v) continue;
        for (const auto& o : v->objects) {
            if (o.kind != Kind::Trend) continue;
            const std::string mode = o.text("mode", "temps r\xC3\xA9" "el");
            if (mode.rfind("historique", 0) == 0) continue;
            std::vector<std::string> pens;
            {
                const std::string list = o.text("variables");
                std::size_t from = 0;
                while (from <= list.size()) {
                    const auto at = list.find(';', from);
                    std::string pen = list.substr(from, at == std::string::npos ? std::string::npos : at - from);
                    while (!pen.empty() && pen.front() == ' ') pen.erase(pen.begin());
                    while (!pen.empty() && pen.back() == ' ') pen.pop_back();
                    if (!pen.empty()) pens.push_back(pen);
                    if (at == std::string::npos) break;
                    from = at + 1;
                }
            }
            auto& series = trends_[std::to_string(id) + ":" + std::to_string(o.id)];
            bool same = series.size() == pens.size();
            for (std::size_t i = 0; same && i < pens.size(); ++i) same = series[i].expression == pens[i];
            if (!same) {
                series.clear();
                for (const auto& p : pens) series.push_back(TrendSeries{p, {}});
            }
            const double keep = std::max(1.0, o.number("duration", 60)) * 1.2 + 1.0;
            for (auto& s : series) {
                bool ok = false;
                // Lot 14 : une valeur de l'automate pas encore lue ne se trace pas.
                const std::string value = plcUnread(s.expression) ? std::string{} : evalText(s.expression, &ok);
                double number = 0;
                if (ok) {
                    if (value == "TRUE") number = 1;
                    else if (value == "FALSE") number = 0;
                    else if (!parseNumber(value, number)) ok = false;
                }
                if (ok) s.points.emplace_back(now, number);
                while (!s.points.empty() && s.points.front().first < now - keep) s.points.pop_front();
            }
        }
    }
}

void Runtime::sampleArchive(double now) {
    if (!project_ || !history_ || project_->history.archived.empty()) return;
    if (nextSample_ >= 0 && now < nextSample_ - 1e-9) return;
    nextSample_ = now + std::max(50, project_->history.samplePeriodMs) / 1000.0;
    const std::string stamp = dateStampOf(now);
    const double epoch = epochOf(now);
    for (const auto& expr : project_->history.archived) {
        if (plcUnread(expr)) continue;           // lot 14 : pas encore lue, ou illisible
        bool ok = false;
        const std::string value = evalText(expr, &ok);
        double number = 0;
        if (!ok) continue;
        if (value == "TRUE") number = 1;
        else if (value == "FALSE") number = 0;
        else if (!parseNumber(value, number)) continue;
        history_->samples.push_back(HistorySample{stamp, epoch, expr, number});
    }
    const auto cap = static_cast<std::size_t>(std::max(10, project_->history.maxEntries)) * 8 + 512;
    if (history_->samples.size() > cap) history_->trim(project_->history);
}

// ============================================ parametres de vue (lot 8) ====
namespace {

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Un environnement vide : les valeurs par defaut des parametres dans
// l'editeur (des litteraux, rien a lire).
class NullEnv final : public sim::Environment {
public:
    bool read(std::string_view, sim::Value&) override { return false; }
    bool write(std::string_view, const sim::Value&) override { return false; }
    bool exists(std::string_view) override { return false; }
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override {
        return false;
    }
    void report(sim::Diagnostic) override {}
};

} // namespace

std::vector<std::pair<std::string, std::string>> parseArguments(std::string_view text) {
    std::vector<std::pair<std::string, std::string>> out;
    std::vector<std::string> pieces;
    std::string cur;
    int depth = 0;
    bool quoted = false;
    for (const char c : text) {
        if (quoted) {
            cur += c;
            if (c == '\'') quoted = false;
            continue;
        }
        if (c == '\'') { quoted = true; cur += c; continue; }
        if (c == '[' || c == '(') ++depth;
        if ((c == ']' || c == ')') && depth > 0) --depth;
        if (c == ';' && depth == 0) { pieces.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    pieces.push_back(cur);
    for (const auto& piece : pieces) {
        const std::string p = trimmed(piece);
        if (p.empty()) continue;
        std::size_t at = p.find(":=");
        std::size_t len = 2;
        if (at == std::string::npos) { at = p.find('='); len = 1; }
        if (at == std::string::npos) continue;
        std::string name = trimmed(p.substr(0, at));
        std::string value = trimmed(p.substr(at + len));
        if (!name.empty()) out.emplace_back(std::move(name), std::move(value));
    }
    return out;
}

bool isVariablePath(std::string_view text) {
    const std::string t = trimmed(text);
    if (t.empty() || !identStart(t[0])) return false;
    const std::string u = upper(t);
    if (u == "TRUE" || u == "FALSE") return false;
    std::size_t i = 0;
    const auto ident = [&] {
        if (i >= t.size() || !identStart(t[i])) return false;
        while (i < t.size() && identChar(t[i])) ++i;
        return true;
    };
    if (!ident()) return false;
    // T#5s, 16#FF : un litteral, pas un nom.
    if (i < t.size() && t[i] == '#') return false;
    while (i < t.size()) {
        if (t[i] == '.') { ++i; if (!ident()) return false; continue; }
        if (t[i] == '[') {
            int depth = 0;
            for (; i < t.size(); ++i) {
                if (t[i] == '[') ++depth;
                else if (t[i] == ']' && --depth == 0) break;
            }
            if (i >= t.size()) return false;
            ++i;
            continue;
        }
        return false;
    }
    return true;
}

Scope designScope(const View& v) {
    Scope scope;
    NullEnv none;
    for (const auto& prm : v.params) {
        const std::string t = trimmed(markers::strip(prm.defaultValue));   // 1.11.7 : les $ d'un repere sont transparents
        if (t.empty()) continue;
        if (isVariablePath(t)) { scope.setAlias(prm.name, t); continue; }
        if (auto value = Expression::compile(t).evaluate(none)) scope.setValue(prm.name, *value);
    }
    return scope;
}

std::shared_ptr<const Scope> Runtime::buildScope(const View& v, std::string_view arguments) {
    const auto given = parseArguments(arguments);
    if (v.params.empty() && given.empty()) return nullptr;
    auto scope = std::make_shared<Scope>();
    const auto bind = [&](const std::string& name, const std::string& text) {
        // 1.11.7 : un repere dans un argument (IN_V := $V[0]$, pose par Dupliquer...) : ses $ sont
        // transparents - V[0] est une variable, la popup la recoit en reference (avant : evaluee, rien ne passait).
        const std::string t = trimmed(markers::strip(text));
        if (t.empty()) return;
        if (isVariablePath(t)) {
            // Les index calcules maintenant (Pompes[i + 1] -> Pompes[3]) ; les
            // parametres de l'appelant deroules (Moteur := Moteur).
            std::string path;
            for (std::size_t i = 0; i < t.size(); ++i) {
                if (t[i] != '[') { path += t[i]; continue; }
                int depth = 0;
                std::size_t j = i;
                for (; j < t.size(); ++j) {
                    if (t[j] == '[') ++depth;
                    else if (t[j] == ']' && --depth == 0) break;
                }
                const std::string inner = t.substr(i + 1, j - i - 1);
                std::string index = trimmed(inner);
                const bool literal = !index.empty() && std::all_of(index.begin(), index.end(), [](char c) {
                    return std::isdigit(static_cast<unsigned char>(c)) != 0;
                });
                if (!literal) {
                    auto value = Expression::compile(inner).evaluate(*env_);
                    if (value) index = std::to_string(value->asInteger());
                    else log("Erreur", source_, "param\xC3\xA8tre " + name + " : index illisible (" + inner + ")");
                }
                path += "[" + index + "]";
                i = j;
            }
            if (env_->aliases) {
                if (const auto* val = env_->aliases->value(path)) { scope->setValue(name, *val); return; }
                path = env_->aliases->resolve(path);
            }
            scope->setAlias(name, path);
            return;
        }
        auto value = Expression::compile(t).evaluate(*env_);
        if (value) scope->setValue(name, *value);
        else log("Erreur", source_, "param\xC3\xA8tre " + name + " := " + t + " : " + value.error().message());
    };
    for (const auto& prm : v.params) {
        const auto it = std::find_if(given.begin(), given.end(), [&](const auto& g) { return upper(g.first) == upper(prm.name); });
        bind(prm.name, it != given.end() ? it->second : prm.defaultValue);
    }
    // 1.11.10 : une popup d'un symbole - les parametres du symbole que l'ouverture ne donne pas
    // (ouverte hors d'une instance) prennent la valeur par defaut du symbole.
    if (const View* owner = project_ ? popupOwner(*project_, v) : nullptr)
        for (const auto& prm : owner->params) {
            const bool there = v.param(prm.name) || std::any_of(given.begin(), given.end(), [&](const auto& g) { return upper(g.first) == upper(prm.name); });
            if (!there) bind(prm.name, prm.defaultValue);
        }
    // Un argument que la vue ne declare pas : accepte (Generer le signale).
    for (const auto& [name, text] : given)
        if (!v.param(name)) bind(name, text);
    if (scope->empty()) return nullptr;
    return scope;
}

// =================================== la saisie et les utilisateurs (lot 8) ==
namespace {

std::size_t prevCp(const std::string& s, std::size_t at) {
    if (at == 0) return 0;
    std::size_t i = at - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return i;
}
std::size_t nextCp(const std::string& s, std::size_t at) {
    if (at >= s.size()) return s.size();
    std::size_t i = at + 1;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    return i;
}
std::size_t codepoints(const std::string& s) {
    std::size_t n = 0;
    for (const char c : s) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return n;
}

// Les champs d'un objet, dans l'ordre de Tab.
std::vector<std::string> fieldsOf(const Object& o) {
    switch (o.kind) {
        case Kind::InputField: return {"valeur"};
        case Kind::LoginPanel:
            return o.flag("userList", true) ? std::vector<std::string>{"motdepasse"} : std::vector<std::string>{"utilisateur", "motdepasse"};
        case Kind::PasswordChange: return {"ancien", "nouveau", "confirmation"};
        default: return {};
    }
}

std::string inputMode(const Object& o) {
    const std::string m = plain(o.text("mode", "num\xC3\xA9rique"));
    if (m.rfind("texte", 0) == 0) return "texte";
    if (m.rfind("mot", 0) == 0) return "motdepasse";
    return "numerique";
}

} // namespace

// ---------------------------------------------- 1.11.7 : le clavier virtuel d'une action ---
void Runtime::openPrompt(const View& v, const Object* o, const Action& a, const std::string& source) {
    (void)o;
    const std::string target = markers::strip(a.target, markers::Mode::Expression);
    sim::Value cur;
    if (target.empty() || !env_->read(target, cur)) {
        log("Erreur", source, "clavier virtuel : variable inconnue " + a.target);
        return;
    }
    const auto spec = actionkinds::keyboardSpec(a);
    KeyboardPrompt p;
    p.target = target;
    p.type = cur.type();
    // Le titre : un texte a trous (Consigne de {Four}) ; vide : le nom de la variable.
    {
        AliasGuard aliasGuard(env_->aliases, scopePtr(v.id));
        p.title = spec.title.empty() ? target : TextTemplate::compile(spec.title).render(*env_);
        const auto bound = [&](const std::string& e) -> std::optional<double> {
            if (trimText(e).empty()) return std::nullopt;
            double x = 0;
            if (parseNumber(trimText(e), x)) return x;
            bool ok = false;
            const std::string t = evalText(e, &ok);
            if (ok && parseNumber(t, x)) return x;
            return std::nullopt;
        };
        p.min = bound(spec.min);
        p.max = bound(spec.max);
    }
    p.unit = spec.unit;
    p.mask = spec.mask;
    p.keyboard = std::string(actionkinds::keyboardFor(std::string(sim::toString(cur.type())), spec.keyboard));
    p.source = source;
    prompt_ = std::move(p);
    promptForm_ = FormState{};
    promptForm_.focus = "valeur";
    // La valeur en cours, choisie : la premiere frappe la remplace (comme un champ de saisie).
    promptForm_.text["valeur"] = spec.mask ? std::string{} : formatValue(cur);
    promptForm_.caret["valeur"] = promptForm_.text["valeur"].size();
    promptForm_.fresh = !spec.mask;
    log("Action", source, "clavier virtuel : " + target);
}

void Runtime::promptTypeText(std::string_view text) {
    if (!prompt_) return;
    auto& buffer = promptForm_.text["valeur"];
    auto& caret = promptForm_.caret["valeur"];
    if (promptForm_.fresh) { buffer.clear(); caret = 0; promptForm_.fresh = false; }
    caret = std::min(caret, buffer.size());
    const bool numeric = prompt_->keyboard == "numerique";
    std::string accepted;
    for (const char c : text) {
        if (c == '\n' || c == '\r' || c == '\t') continue;
        if (numeric && !(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == ',' || c == '-' || c == '+' || c == 'e' || c == 'E'))
            continue;
        accepted += c;
    }
    if (accepted.empty()) return;
    if (codepoints(buffer) + codepoints(accepted) > 64) return;
    buffer.insert(caret, accepted);
    caret += accepted.size();
    if (promptForm_.error) { promptForm_.message.clear(); promptForm_.error = false; }
}

void Runtime::promptTypeKey(EditKey k, double now) {
    if (!prompt_) return;
    auto& buffer = promptForm_.text["valeur"];
    auto& caret = promptForm_.caret["valeur"];
    caret = std::min(caret, buffer.size());
    switch (k) {
        case EditKey::Backspace:
            if (promptForm_.fresh) { buffer.clear(); caret = 0; promptForm_.fresh = false; break; }
            if (caret > 0) { const auto p = prevCp(buffer, caret); buffer.erase(p, caret - p); caret = p; }
            break;
        case EditKey::Delete:
            if (promptForm_.fresh) { buffer.clear(); caret = 0; promptForm_.fresh = false; break; }
            if (caret < buffer.size()) buffer.erase(caret, nextCp(buffer, caret) - caret);
            break;
        case EditKey::Left: promptForm_.fresh = false; caret = prevCp(buffer, caret); break;
        case EditKey::Right: promptForm_.fresh = false; caret = nextCp(buffer, caret); break;
        case EditKey::Home: promptForm_.fresh = false; caret = 0; break;
        case EditKey::End: promptForm_.fresh = false; caret = buffer.size(); break;
        case EditKey::Enter: promptSubmit(now); break;
        case EditKey::Escape:
            log("Action", prompt_->source, "clavier virtuel : annul\xC3\xA9");
            prompt_.reset();
            break;
        default: break;
    }
}

void Runtime::promptSubmit(double now) {
    if (!prompt_) return;
    const auto& p = *prompt_;
    const std::string text = trimText(promptForm_.text["valeur"]);
    const auto fail = [&](const std::string& m) {
        promptForm_.message = m;
        promptForm_.error = true;
        promptForm_.messageAt = now;
    };
    sim::Value next;
    switch (p.type) {
        case sim::Type::Bool: {
            const std::string u = upper(text);
            if (u == "TRUE" || u == "VRAI" || u == "1" || u == "OUI" || u == "ON") next = sim::Value::boolean(true);
            else if (u == "FALSE" || u == "FAUX" || u == "0" || u == "NON" || u == "OFF") next = sim::Value::boolean(false);
            else { fail("TRUE ou FALSE (1 ou 0)"); return; }
            break;
        }
        case sim::Type::String: next = sim::Value::text(text); break;
        default: {
            double x = 0;
            if (!parseNumber(text, x)) { fail("un nombre est attendu"); return; }
            if ((p.min && x < *p.min) || (p.max && x > *p.max)) {
                fail("hors limites : de " + (p.min ? formatNumber(*p.min) : std::string("-")) + " \xC3\xA0 "
                     + (p.max ? formatNumber(*p.max) : std::string("-")));
                return;
            }
            if (p.type == sim::Type::Real || p.type == sim::Type::Unknown) next = sim::Value::real(x);
            else if (p.type == sim::Type::Time) next = sim::Value::time(static_cast<std::int64_t>(std::llround(x)));
            else {
                if (std::fabs(x - std::round(x)) > 1e-9) { fail("un entier est attendu"); return; }
                next = sim::Value::integer(p.type, static_cast<std::int64_t>(std::llround(x)));
            }
            break;
        }
    }
    const std::string target = p.target, source = p.source;
    const bool mask = p.mask;
    GestureGuard gesture(*this, source);                // l'audit : un geste de l'operateur
    if (!write(target, next, source)) { fail("\xC3\xA9" "criture refus\xC3\xA9" "e"); return; }
    sim::Value after;
    (void)env_->read(target, after);
    log("Action", source, "clavier virtuel : " + target + " = " + (mask ? std::string("********") : formatValue(after)));
    prompt_.reset();
}

void Runtime::promptPart(std::string_view part, double now) {
    if (!prompt_) return;
    lastActivity_ = now;
    if (part == "bouton:valider") promptSubmit(now);
    else if (part == "bouton:annuler" || part == "fermer") promptTypeKey(EditKey::Escape, now);
    // "champ", "dehors" : rien (le panneau est modal ; le clavier reste).
}


const FormState* Runtime::formState(Id object) const {
    const auto it = forms_.find(object);
    return it == forms_.end() ? nullptr : &it->second;
}

const View* Runtime::shownViewOf(Id object) const {
    for (auto it = popups_.rbegin(); it != popups_.rend(); ++it)
        if (const auto* v = viewOf(*it); v && v->object(object)) return v;
    if (const auto* v = viewOf(current_); v && v->object(object)) return v;
    return nullptr;
}

std::string Runtime::keyboardMode() const {
    if (prompt_) return settings_.keyboard == "jamais" ? std::string{} : prompt_->keyboard;   // 1.11.7 : le clavier d'une action
    if (signature_) return signatureKeyboardMode();             // lot 13 : le panneau de signature
    if (loginShown_) return loginKeyboardMode();                // lot 12 : le menu de connexion
    if (focused_ == kNoId) return {};
    const View* v = shownViewOf(focused_);
    const Object* o = v ? v->object(focused_) : nullptr;
    if (!o) return {};
    // Lot 10 : le reglage du poste - jamais aucun clavier, ou toujours un.
    if (settings_.keyboard == "jamais") return {};
    const std::string k = plain(o->text("keyboard", "aucun"));
    if (k.rfind("num", 0) == 0) return "numerique";
    if (k.rfind("complet", 0) == 0 || k.rfind("alpha", 0) == 0) return "complet";
    if (settings_.keyboard == "toujours") {
        const std::string mode = plain(o->text("mode", "num\xC3\xA9rique"));
        return o->kind == Kind::InputField && mode.rfind("num", 0) == 0 ? "numerique" : "complet";
    }
    return {};
}

double Runtime::autoLogoutRemaining(double now) const {
    const int idle = autoLogoutMinutes();
    if (!project_ || !project_->security.enabled || idle <= 0 || user_.empty()) return -1;
    return std::max(0.0, idle * 60.0 - (now - lastActivity_));
}

const User* Runtime::loginChoice(Id object) const {
    if (!project_) return nullptr;
    std::vector<const User*> users;
    for (const auto& u : project_->security.users) if (u.enabled) users.push_back(&u);
    if (users.empty()) return nullptr;
    const auto it = forms_.find(object);
    const std::string want = upper(it != forms_.end() && !it->second.chosen.empty() ? it->second.chosen : user_);
    if (!want.empty())
        for (const auto* u : users) if (upper(u->login) == want) return u;
    return users.front();
}

void Runtime::formMessage(Id object, std::string message, bool error, double now) {
    auto& f = forms_[object];
    f.message = std::move(message);
    f.error = error;
    f.messageAt = now;
}

void Runtime::objectPart(Id object, std::string_view part, double now) {
    composed_.clear(); boundCalls_.clear();
    now_ = std::max(now_, now);
    lastActivity_ = now;
    if (!running_ || !project_) return;
    const View* v = shownViewOf(object);
    const Object* found = v ? v->object(object) : nullptr;
    if (!found) return;
    if (found->kind == Kind::RecipeManager) {
        GestureGuard gesture(*this, where(*v, found));   // lot 13 : l'audit
        recipeManagerClick(object, part, now);
        return;
    }
    const View snapshot = *v;
    const Object& o = *snapshot.object(object);
    const std::string source = where(snapshot, &o);
    GestureGuard gesture(*this, source);                // lot 13 : l'audit
    AliasGuard aliasGuard(env_->aliases, scopePtr(snapshot.id));
    std::string why;
    if (!objectAllowed(o, &why)) {
        event("Acc\xC3\xA8s refus\xC3\xA9", source, why);
        formMessage(object, why, true, now);
        return;
    }
    // Un autre objet avait le focus : il le perd (et valide s'il le doit).
    if (focused_ != kNoId && focused_ != object) unfocus(now);
    auto& f = forms_[object];
    const auto focusOn = [&](const std::string& field) {
        f.focus = field;
        f.caret[field] = f.text[field].size();
        focused_ = object;
    };
    switch (o.kind) {
        case Kind::InputField: {
            // Ecrire une variable : la permission Piloter, comme Affecter.
            if (project_->security.enabled && !permitted("Piloter")) {
                const std::string refused = "saisie : permission \xC2\xAB Piloter \xC2\xBB requise ("
                                          + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")";
                event("Acc\xC3\xA8s refus\xC3\xA9", source, refused);
                formMessage(object, "permission Piloter requise", true, now);
                return;
            }
            if (focused_ == object && f.focus == "valeur") return;     // deja en saisie
            std::string text;
            const std::string var = markers::strip(o.text("variable"));
            sim::Value cur;
            if (!var.empty() && env_->read(var, cur)) {
                const std::string mode = inputMode(o);
                if (mode == "numerique") text = formatValue(cur, project_ ? effectiveFormat(*project_, o) : o.text("format"));   // lot 13
                else text = cur.type() == sim::Type::String ? cur.asString() : cur.display();
                if (mode == "motdepasse") text.clear();
            }
            f.text["valeur"] = text;
            focusOn("valeur");
            f.fresh = true;          // la premiere frappe remplace la valeur montree
            f.message.clear();
            f.error = false;
            return;
        }
        case Kind::LoginPanel: {
            std::vector<const User*> users;
            for (const auto& u : project_->security.users) if (u.enabled) users.push_back(&u);
            if (part == "precedent" || part == "suivant") {
                if (users.empty()) return;
                const std::size_t n = users.size();
                const User* shown = loginChoice(object);
                std::size_t at = 0;
                for (std::size_t k = 0; k < n; ++k) if (users[k] == shown) at = k;
                at = part == "suivant" ? (at + 1) % n : (at + n - 1) % n;
                f.chosen = users[at]->login;
                f.text["motdepasse"].clear();
                focusOn("motdepasse");
                return;
            }
            if (part == "champ:utilisateur" && !o.flag("userList", true)) { focusOn("utilisateur"); return; }
            if (part == "champ:motdepasse") { focusOn("motdepasse"); return; }
            if (part == "bouton") submitForm(snapshot, o, now);
            return;
        }
        case Kind::PasswordChange: {
            if (part == "champ:ancien") { focusOn("ancien"); return; }
            if (part == "champ:nouveau") { focusOn("nouveau"); return; }
            if (part == "champ:confirmation") { focusOn("confirmation"); return; }
            if (part == "bouton") submitForm(snapshot, o, now);
            return;
        }
        case Kind::LogoutButton: {
            if (user_.empty()) {
                formMessage(object, "personne n'est connect\xC3\xA9", true, now);
                log("Action", source, "d\xC3\xA9" "connexion : personne n'est connect\xC3\xA9");
                return;
            }
            if (o.flag("confirm", false) && now > f.confirmUntil) {
                f.confirmUntil = now + 3.0;
                formMessage(object, "Cliquer encore pour confirmer", false, now);
                return;
            }
            f.confirmUntil = 0;
            f.message.clear();
            logout(now, "bouton " + source);
            return;
        }
        case Kind::UserManager:
            userManagerClick(snapshot, o, part, now);
            return;
        // Lot 11 : une ligne d'une liste d'alarmes, le bandeau, le resume par zone, les
        // compteurs de production, le tableau de variables, l'editeur de recette.
        case Kind::History: case Kind::AlarmBanner: case Kind::AlarmSummary: case Kind::ProductionCounter:
        case Kind::VariableTable: case Kind::RecipeEditor:
            lot11Part(snapshot, o, part, now);
            return;
        // Lot 12 : la navigation et la structure (un bouton de la barre, une etape du
        // fil d'Ariane, un onglet, la barre d'un panneau, un bandeau, une zone).
        case Kind::NavBar: case Kind::Breadcrumb: case Kind::TabContainer: case Kind::ScrollPanel:
        case Kind::CollapsiblePanel: case Kind::ZoneMap:
            lot12Part(snapshot, o, part, now);
            return;
        // Lot 13 : un bouton du selecteur de langue, du selecteur de theme.
        case Kind::LanguageSelector:
            languagePart(snapshot, o, part, now);
            return;
        case Kind::ThemeSelector:
            if (part == "theme:jour" || part == "theme:nuit") (void)setDisplay("theme", part.substr(6), now, where(snapshot, &o));
            return;
        // Lot 14 : les boutons du diagnostic automate (Reconnecter, Remettre a zero).
        case Kind::PlcDiagnostic:
            commPart(o, part, now);
            return;
        default:
            // Lot 13 : un choix qui ecrit, sur une commande a signature, attend le panneau.
            if (signatureCommits(o, part) && signatureNeeded(snapshot, o)) {
                requestSignature(snapshot, o, SignatureRequest::Gesture::Part, now, std::string(part));
                return;
            }
            controlPart(snapshot, o, part, now);     // lot 9
            return;
    }
}

void Runtime::typeText(std::string_view text, double now) {
    now_ = std::max(now_, now);
    lastActivity_ = now;
    if (prompt_) { promptTypeText(text); return; }             // 1.11.7 : le clavier d'une action est modal
    if (signature_) { signatureTypeText(text, now); return; }  // lot 13 : le panneau de signature est modal
    if (loginShown_) { loginTypeText(text, now); return; }     // lot 12 : le menu de connexion est modal
    if (focused_ == kNoId) { badgeTyped(text, now); return; }  // lot 13 : un lecteur de badge tape
    badgeBuffer_.clear();
    const View* v = shownViewOf(focused_);
    const Object* o = v ? v->object(focused_) : nullptr;
    if (!o) { focused_ = kNoId; return; }
    auto& f = forms_[focused_];
    if (f.focus.empty()) return;
    auto& buffer = f.text[f.focus];
    auto& caret = f.caret[f.focus];
    if (f.fresh) { buffer.clear(); caret = 0; f.fresh = false; }
    caret = std::min(caret, buffer.size());
    std::string accepted;
    const bool numeric = o->kind == Kind::InputField && inputMode(*o) == "numerique";
    for (const char c : text) {
        if (c == '\n' || c == '\r' || c == '\t') continue;
        if (numeric && !(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == ',' || c == '-' || c == '+' || c == 'e' || c == 'E'))
            continue;
        accepted += c;
    }
    if (accepted.empty()) return;
    if (o->kind == Kind::InputField && inputMode(*o) != "numerique") {
        const auto limit = static_cast<std::size_t>(std::max(1.0, o->number("maxLength", 32)));
        if (codepoints(buffer) + codepoints(accepted) > limit) {
            formMessage(focused_, std::to_string(limit) + " caract\xC3\xA8res au plus", true, now);
            return;
        }
    }
    buffer.insert(caret, accepted);
    caret += accepted.size();
    if (f.error) { f.message.clear(); f.error = false; }
}

void Runtime::typeKey(EditKey k, double now) {
    now_ = std::max(now_, now);
    lastActivity_ = now;
    if (prompt_) { promptTypeKey(k, now); return; }            // 1.11.7 : le clavier d'une action est modal
    if (signature_) { signatureTypeKey(k, now); return; }      // lot 13 : le panneau de signature est modal
    if (loginShown_) { (void)loginTypeKey(k, now); return; }   // lot 12 : le menu de connexion est modal
    if (focused_ == kNoId) {
        // Lot 13 : Entree termine le numero d'un badge ; le reste l'oublie.
        if (k == EditKey::Enter) (void)badgeEnter(now);
        else badgeBuffer_.clear();
        return;
    }
    const View* v = shownViewOf(focused_);
    const Object* o = v ? v->object(focused_) : nullptr;
    if (!o) { focused_ = kNoId; return; }
    const Id object = focused_;
    auto& f = forms_[object];
    if (f.focus.empty()) return;
    GestureGuard gesture(*this, where(*v, o));          // lot 13 : l'audit (Entree, Tab valident)
    auto& buffer = f.text[f.focus];
    auto& caret = f.caret[f.focus];
    caret = std::min(caret, buffer.size());
    const auto fields = fieldsOf(*o);
    const auto moveField = [&](int step) {
        if (fields.empty()) return;
        std::size_t at = 0;
        for (std::size_t i = 0; i < fields.size(); ++i) if (fields[i] == f.focus) at = i;
        const std::size_t n = fields.size();
        f.focus = fields[(at + n + static_cast<std::size_t>(step + static_cast<int>(n))) % n];
        f.caret[f.focus] = f.text[f.focus].size();
    };
    switch (k) {
        case EditKey::Backspace:
            if (f.fresh) { buffer.clear(); caret = 0; f.fresh = false; break; }
            if (caret > 0) { const auto p = prevCp(buffer, caret); buffer.erase(p, caret - p); caret = p; }
            break;
        case EditKey::Delete:
            if (f.fresh) { buffer.clear(); caret = 0; f.fresh = false; break; }
            if (caret < buffer.size()) buffer.erase(caret, nextCp(buffer, caret) - caret);
            break;
        case EditKey::Left: f.fresh = false; caret = prevCp(buffer, caret); break;
        case EditKey::Right: f.fresh = false; caret = nextCp(buffer, caret); break;
        case EditKey::Home: f.fresh = false; caret = 0; break;
        case EditKey::End: f.fresh = false; caret = buffer.size(); break;
        case EditKey::Tab:
        case EditKey::BackTab: {
            if (o->kind != Kind::InputField) { moveField(k == EditKey::Tab ? 1 : -1); break; }
            // Un champ de saisie : Tab valide, puis passe au champ suivant de la vue
            // (Maj+Tab : au precedent), dans l'ordre des objets. Refusee, la
            // saisie garde le focus.
            const View snapshot = *v;
            submitForm(snapshot, *snapshot.object(object), now);
            if (focused_ == object) break;
            std::vector<Id> inputs;
            for (const auto& x : snapshot.objects)
                if (x.kind == Kind::InputField && parseBool(x.text("visible", "TRUE"), true)) inputs.push_back(x.id);
            std::size_t at = 0;
            for (std::size_t i = 0; i < inputs.size(); ++i) if (inputs[i] == object) at = i;
            if (inputs.size() > 1) {
                const std::size_t n = inputs.size();
                objectPart(inputs[(at + (k == EditKey::Tab ? 1 : n - 1)) % n], "champ", now);
            }
            break;
        }
        case EditKey::Escape: {
            // Annuler : rien n'est ecrit ; les mots de passe tapes s'effacent.
            if (o->kind != Kind::InputField)
                for (auto& [name, text] : f.text) if (name != "utilisateur") text.clear();
            f.focus.clear();
            f.fresh = false;
            focused_ = kNoId;
            break;
        }
        case EditKey::Enter: {
            // Entree dans un champ qui n'est pas le dernier : le suivant.
            if (!fields.empty() && f.focus != fields.back()) { moveField(1); break; }
            const View snapshot = *v;
            submitForm(snapshot, *snapshot.object(object), now);
            break;
        }
    }
}

void Runtime::unfocus(double now) {
    if (focused_ == kNoId) return;
    const Id object = focused_;
    const View* v = shownViewOf(object);
    const Object* o = v ? v->object(object) : nullptr;
    if (o && o->kind == Kind::InputField && o->flag("validateOnExit", false)) {
        const View snapshot = *v;
        GestureGuard gesture(*this, where(snapshot, snapshot.object(object)));     // lot 13 : l'audit
        submitForm(snapshot, *snapshot.object(object), now);
    }
    if (focused_ == object) {
        auto& f = forms_[object];
        f.focus.clear();
        f.fresh = false;
        if (o && o->kind == Kind::InputField) f.text.clear();
        focused_ = kNoId;
    }
}

void Runtime::submitForm(const View& v, const Object& o, double now) {
    AliasGuard aliasGuard(env_->aliases, scopePtr(v.id));
    const std::string source = where(v, &o);
    auto& f = forms_[o.id];
    const auto done = [&] {
        f.focus.clear();
        f.fresh = false;
        if (focused_ == o.id) focused_ = kNoId;
    };
    const auto fail = [&](const std::string& message) { formMessage(o.id, message, true, now); };
    switch (o.kind) {
        case Kind::InputField: {
            const std::string var = markers::strip(o.text("variable"));
            if (var.empty()) { fail("aucune variable reli\xC3\xA9" "e"); return; }
            if (project_ && project_->security.enabled && !permitted("Piloter")) { fail("permission Piloter requise"); return; }
            sim::Value cur;
            if (!env_->read(var, cur)) {
                fail("variable inconnue : " + var);
                log("Erreur", source, "saisie : variable inconnue " + var);
                return;
            }
            const std::string text = trimmed(f.text["valeur"]);
            sim::Value next;
            if (inputMode(o) == "numerique") {
                double x = 0;
                if (!parseNumber(text, x)) { fail("un nombre est attendu"); return; }
                const auto bound = [&](const char* key, double& out) {
                    const std::string b = trimmed(o.text(key));
                    if (b.empty()) return false;
                    if (parseNumber(b, out)) return true;
                    bool ok = false;
                    const std::string evaluated = evalText(b, &ok);
                    return ok && parseNumber(evaluated, out);
                };
                double lo = 0, hi = 0;
                const bool hasLo = bound("min", lo), hasHi = bound("max", hi);
                if ((hasLo && x < lo) || (hasHi && x > hi)) {
                    fail("hors bornes : de " + (hasLo ? formatNumber(lo) : std::string("-")) + " \xC3\xA0 "
                         + (hasHi ? formatNumber(hi) : std::string("-")));
                    return;
                }
                switch (cur.type()) {
                    case sim::Type::Real: next = sim::Value::real(x); break;
                    case sim::Type::Bool: next = sim::Value::boolean(x != 0.0); break;
                    case sim::Type::String: next = sim::Value::text(text); break;
                    default:
                        if (std::fabs(x - std::round(x)) > 1e-9) { fail("un entier est attendu"); return; }
                        next = sim::Value::integer(cur.type() == sim::Type::Unknown ? sim::Type::Int : cur.type(),
                                                   static_cast<std::int64_t>(std::llround(x)));
                        break;
                }
            } else {
                const auto limit = static_cast<std::size_t>(std::max(1.0, o.number("maxLength", 32)));
                if (codepoints(text) > limit) { fail(std::to_string(limit) + " caract\xC3\xA8res au plus"); return; }
                next = sim::Value::text(text);
            }
            // Lot 13 : une saisie a signature attend le panneau (la valeur tapee attend avec elle).
            if (signatureNeeded(v, o)) {
                requestSignature(v, o, SignatureRequest::Gesture::Input, now, {}, 0, text);
                done();
                return;
            }
            if (!write(var, next, source)) { fail("\xC3\xA9" "criture refus\xC3\xA9" "e"); return; }
            sim::Value after;
            (void)env_->read(var, after);
            log("Action", source, "saisie : " + var + " = " + (inputMode(o) == "motdepasse" ? std::string("********") : formatValue(after)));
            f.message.clear();
            f.error = false;
            f.text.clear();
            done();
            return;
        }
        case Kind::LoginPanel: {
            std::string loginName;
            if (o.flag("userList", true)) {
                if (const User* shown = loginChoice(o.id)) loginName = shown->login;
            } else {
                loginName = trimmed(f.text["utilisateur"]);
            }
            if (loginName.empty()) { fail("choisir un utilisateur"); return; }
            std::string why;
            loginVia_ = source;                                  // lot 13 : le journal d'audit
            const bool ok = login(loginName, f.text["motdepasse"], now, &why);
            f.text["motdepasse"].clear();
            f.caret["motdepasse"] = 0;
            if (!ok) { fail(why); f.focus = "motdepasse"; focused_ = o.id; return; }
            const auto* u = user();
            formMessage(o.id, "Bienvenue, " + (u && !u->fullName.empty() ? u->fullName : loginName), false, now);
            done();
            const std::string after = o.text("afterLogin");
            if (!after.empty()) {
                if (const auto* target = project_ ? project_->viewByName(after) : nullptr)
                    (void)navigate(target->id, Transition{}, now, {});
                else log("Erreur", source, "apr\xC3\xA8s la connexion : vue '" + after + "' introuvable");
            }
            return;
        }
        case Kind::PasswordChange: {
            const User* u = user();
            if (!u) { fail("personne n'est connect\xC3\xA9"); return; }
            if (u->protection != "classique") { fail("ce compte n'a pas de mot de passe (" + u->protection + ")"); return; }
            const std::string oldPwd = f.text["ancien"], newPwd = f.text["nouveau"], confirm = f.text["confirmation"];
            const auto clearAll = [&] {
                for (auto& [name, text] : f.text) text.clear();
                for (auto& [name, at] : f.caret) at = 0;
            };
            if (!passwordMatches(u->salt, u->passwordHash, oldPwd)) {
                event("Mot de passe refus\xC3\xA9", u->login, "ancien mot de passe incorrect (" + source + ")");
                clearAll();
                f.focus = "ancien";
                focused_ = o.id;
                fail("ancien mot de passe incorrect");
                return;
            }
            const auto minLength = static_cast<std::size_t>(std::max(1.0, o.number("minLength", 6)));
            std::string problem;
            if (codepoints(newPwd) < minLength) problem = "au moins " + std::to_string(minLength) + " caract\xC3\xA8res";
            else if (o.flag("requireDigit", false) && std::none_of(newPwd.begin(), newPwd.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
                problem = "au moins un chiffre";
            // Lot 13 : la politique des mots de passe du projet s'ajoute a celle de l'objet.
            else if (const std::string rule = passwordProblem(project_->security, u, newPwd, minLength); !rule.empty()) problem = rule;
            else if (newPwd == oldPwd) problem = "le nouveau doit \xC3\xAAtre diff\xC3\xA9rent de l'ancien";
            else if (newPwd != confirm) problem = "la confirmation ne correspond pas";
            if (!problem.empty()) {
                f.text["confirmation"].clear();
                f.caret["confirmation"] = 0;
                f.focus = "nouveau";
                focused_ = o.id;
                fail(problem);
                return;
            }
            if (!hooks_.userRequest) { fail("l'\xC3\xA9" "cran ne sait pas enregistrer le mot de passe ici"); return; }
            UserRequest rq;
            rq.op = "changer";
            rq.object = o.id;
            rq.user = u->id;
            rq.login = u->login;
            rq.salt = randomHex(16);
            rq.hash = passwordHash(rq.salt, newPwd);
            rq.source = source;
            const std::string who = u->login;
            hooks_.userRequest(rq);
            event("Mot de passe chang\xC3\xA9", who, "par " + who + " (" + source + ")");
            audit("Mot de passe", source, who, {}, "chang\xC3\xA9 par " + who);        // lot 13 (jamais le mot de passe)
            clearAll();
            formMessage(o.id, "Mot de passe chang\xC3\xA9", false, now);
            done();
            return;
        }
        case Kind::VariableTable: case Kind::RecipeEditor:     // lot 11 : Entree dans une ligne
            (void)lot11Submit(v, o, now);
            return;
        default:
            return;
    }
}

void Runtime::userManagerClick(const View& v, const Object& o, std::string_view part, double now) {
    const std::string source = where(v, &o);
    auto& f = forms_[o.id];
    if (!project_) return;
    const auto& users = project_->security.users;
    if (part.rfind("ligne:", 0) == 0) {
        const auto row = static_cast<std::size_t>(std::max(0, std::atoi(std::string(part.substr(6)).c_str())));
        if (row < users.size()) f.selected = users[row].id;
        return;
    }
    if (part.rfind("bouton:", 0) != 0) return;
    const std::string button(part.substr(7));
    if (project_->security.enabled && !permitted("Administrer")) {
        event("Acc\xC3\xA8s refus\xC3\xA9", source,
              button + " : permission \xC2\xAB Administrer \xC2\xBB requise ("
                  + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")");
        formMessage(o.id, "permission Administrer requise", true, now);
        return;
    }
    const User* sel = f.selected != kNoId ? project_->user(f.selected) : nullptr;
    UserRequest rq;
    rq.object = o.id;
    rq.source = source;
    if (button == "Ajouter") {
        rq.op = "ajouter";
    } else {
        if (!sel) {
            formMessage(o.id, button + " : choisir une ligne", true, now);
            log("Action", source, button + " : aucun utilisateur choisi (cliquer une ligne)");
            return;
        }
        rq.user = sel->id;
        rq.login = sel->login;
        if (button == "Modifier") rq.op = "modifier";
        else if (button == "Supprimer") rq.op = "supprimer";
        else if (button == "Mot de passe") rq.op = "motdepasse";
        else if (button == "Activer" || button == "D\xC3\xA9sactiver") { rq.op = "activer"; rq.enabled = !sel->enabled; }
        else { log("Erreur", source, "bouton inconnu : " + button); return; }
        // On ne se retire pas soi-meme le droit d'entrer.
        if ((rq.op == "supprimer" || (rq.op == "activer" && !rq.enabled)) && upper(sel->login) == upper(user_)) {
            formMessage(o.id, "pas l'utilisateur connect\xC3\xA9", true, now);
            log("Action", source, button + " : " + sel->login + " est l'utilisateur connect\xC3\xA9");
            return;
        }
    }
    if (!hooks_.userRequest) {
        formMessage(o.id, "l'\xC3\xA9" "cran ne sait pas modifier les utilisateurs ici", true, now);
        log("Action", source, button + " : l'\xC3\xA9" "cran ne sait pas modifier les utilisateurs ici");
        return;
    }
    f.message.clear();
    hooks_.userRequest(rq);
}

} // namespace hmi
