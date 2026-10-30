#include "HmiRuntime.hpp"
#include "../sim/ValueText.hpp"   // 1.12.19 : les mots d'un booleen, la lecture d'un nombre, partout les memes
#include "../core/Edition.hpp"   // 1.12.2 : IHM Studio n'a pas d'automate
#include "HmiNatives.hpp"   // 1.12.0 : couleurs, aleatoire, enumerations natives
#include "HmiTypeForms.hpp"   // 1.12.19 : la valeur initiale d'un rappel (=Fonction)
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
#include "HmiCalc.hpp"        // 1.12.7 : les valeurs calculees au demarrage
#include "HmiMethods.hpp"     // 1.13.0 (lot J1) : les methodes des types IHM (S2) a l'execution (S1)
#include "HmiUnits.hpp"       // 1.13.0 (lot J6) : les unites de mesure (EN / UNITE : declaredUnit, convertUnit)
#include "HmiLibrary.hpp"     // 1.13.0 : biblio::unitConfig (les unites des bibliotheques reliees)
#include "HmiNames.hpp"       // 1.13.0 (lot J8) : les espaces de noms - le code qualifie avant le moteur
#include "HmiOperators.hpp"   // 1.10 (integration) : les operateurs du projet (S2) a l'execution (S1)
#include "HmiRoles.hpp"       // 1.12.6 (lot B) : le lecteur du role, l'ecriture vers le type
#include "HmiEnums.hpp"       // 1.10 (decision 15) : les enumerations IHM dans les scripts (S1)
#include "HmiMarkers.hpp"     // 1.11 (REP) : les reperes $...$, transparents pour le calcul
#include "HmiApiVars.hpp"     // 1.11.1 (API-M) : API.<...> est le nom que l'automate connait
#include "HmiTypeRegistry.hpp" // 1.11.19 (refonte, lot 6) : le type du moteur d'un nom de type
#include "HmiOverload.hpp"     // 1.11.20 : les surcharges (la regle partagee avec le controle)
#include "HmiEquipmentLink.hpp" // 1.12.11 (lot G) : la liaison Equipement des symboles du synoptique
#include "HmiRanges.hpp"       // 1.12.10 (lot F) : la plage et les seuils de la variable
#include "HmiScriptTask.hpp"   // 1.12.26 : les scripts detaches
#include "HmiNet.hpp"          // 1.12.26 : WAIT, l'attente precise
#include "../core/CrashGuard.hpp"   // 1.12.26 : un WAIT bloquant n'est pas un blocage
#include "../sim/Interpreter.hpp"
#include "../sim/Runtime.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>

namespace hmi {

namespace {

constexpr int         kMaxDepth = 32;    // 1.11.10 : 8 avant - les fonctions et operateurs s'appellent entre eux
constexpr std::size_t kJournalMax = 500;    // sans projet ; 1.12.3 : sinon Configuration > Historiques (maxEntries, 2 000)

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// 1.13.0 : un message du moteur nomme une methode ou un operateur par la FUNCTION interne qui le porte ("fonction
// METHODE_T_CUVE_VERIFIER, ligne 2", "fonction OPERATEUR_ADD, ligne 4") : la Console, le CATCH disent leur nom et la
// ligne de leur editeur ("methode T_Cuve.Verifier, ligne 2", "operateur +, ligne 3" - le moteur lit l'en-tete d'un
// operateur sur sa propre ligne), comme le debogueur. Un autre nom : tel quel.
std::string shownCodeNames(const Project* p, std::string text) {
    if (!p || text.find("fonction ") == std::string::npos) return text;
    // labelOf : le nom montre (vide : inconnu) et les lignes d'en-tete que le moteur lit avant le corps.
    const auto rename = [&](std::string_view prefix, const std::function<std::pair<std::string, int>(const std::string&)>& labelOf) {
        const std::string head = "fonction " + std::string(prefix);
        for (std::size_t at = text.find(head); at != std::string::npos; at = text.find(head, at + 1)) {
            const std::size_t b = at + 9;
            std::size_t e = b;
            while (e < text.size() && (std::isalnum(static_cast<unsigned char>(text[e])) || text[e] == '_')) ++e;
            const auto [label, header] = labelOf(upper(std::string_view(text).substr(b, e - b)));
            if (label.empty()) continue;
            std::string shown = label;
            static constexpr std::string_view kLine = ", ligne ";
            std::size_t end = e;
            if (text.compare(e, kLine.size(), kLine) == 0) {
                std::size_t d = e + kLine.size();
                while (d < text.size() && std::isdigit(static_cast<unsigned char>(text[d]))) ++d;
                if (d > e + kLine.size()) {
                    const int n = std::stoi(text.substr(e + kLine.size(), d - e - kLine.size()));
                    shown += std::string(kLine) + std::to_string(n > header ? n - header : n);
                    end = d;
                }
            }
            text.replace(at, end - at, shown);
        }
    };
    rename("METHODE_", [&](const std::string& internal) {
        std::string label;
        const auto look = [&](const HmiType& t) {
            if (!label.empty()) return;
            for (const auto& m : t.methods)
                if (upper(methodFunctionName(fullName(t), m)) == internal) {
                    label = "m\xC3\xA9thode " + fullName(t) + "." + m.name;
                    return;
                }
        };
        for (const auto& t : p->programs.types) look(t);
        if (label.empty()) p->forEachLibraryType([&](const HmiType& t, const LoadedLibrary&) { look(t); });
        return std::make_pair(label, 0);
    });
    rename("OPERATEUR_", [&](const std::string& internal) {
        for (const auto& o : allOperators(*p))
            if (o.op && upper(operatorInternalName(*o.op)) == internal)
                return std::make_pair(o.op->op == kConversionOp ? "conversion " + operatorFunctionName(*o.op) : "op\xC3\xA9rateur " + o.op->op, 1);
        return std::make_pair(std::string{}, 0);
    });
    return text;
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
namespace {
// 1.11.24 : un chemin qui n'est pas en clair - un indice calcule (V[Idx].Consigne,
// Tab[I + 1]), des parentheses autour d'un chemin ((V[0]).Consigne). Le ST les defait
// avant d'ecrire ; une saisie, une commande, l'action d'un bouton les donnaient tels quels
// (variable inconnue). Rapide : la plupart des noms n'ont ni ( ni [ calcule.
bool needsConcretePath(std::string_view n) {
    if (n.empty()) return false;
    if (n.front() == '(') return true;
    for (std::size_t i = 0; i < n.size(); ++i) {
        if (n[i] != '[') continue;
        std::size_t j = i + 1;
        while (j < n.size() && (n[j] == ' ' || n[j] == '-' || n[j] == ',' || std::isdigit(static_cast<unsigned char>(n[j])))) ++j;
        if (j < n.size() && n[j] != ']') return true;
    }
    return false;
}
std::string trimmedView(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
} // namespace

// Les variables IHM d'abord (sans casse, comme le ST), puis l'automate ; les
// fonctions IHM_... ici, les autres au simulateur.
class Runtime::Env final : public sim::Environment, public FunctionHost {
public:
    explicit Env(Runtime& rt) : rt_(rt) {}
    // 1.11.24 : le chemin en clair (needsConcretePath) : les parentheses autour d'un chemin
    // tombent, chaque indice calcule se calcule ici (ses noms lus comme le reste). Un texte
    // qui n'est pas un chemin, un indice illisible : tel quel (la lecture, l'ecriture echouent).
    std::string concretePath(std::string_view n) {
        std::string t = trimmedView(n);
        while (!t.empty() && t.front() == '(') {
            int depth = 0;
            std::size_t close = std::string::npos;
            for (std::size_t i = 0; i < t.size(); ++i) {
                if (t[i] == '(') ++depth;
                else if (t[i] == ')' && --depth == 0) { close = i; break; }
            }
            if (close == std::string::npos) break;
            const std::string inner = trimmedView(std::string_view(t).substr(1, close - 1));
            const std::string rest = t.substr(close + 1);
            if (!isVariablePath(inner) || !(rest.empty() || rest.front() == '.' || rest.front() == '[')) break;
            t = inner + rest;
        }
        if (!isVariablePath(t)) return t;
        std::string out;
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (t[i] != '[') { out += t[i]; continue; }
            int depth = 0;
            std::size_t j = i;
            for (; j < t.size(); ++j) {
                if (t[j] == '[') ++depth;
                else if (t[j] == ']' && --depth == 0) break;
            }
            if (j >= t.size()) return t;
            // Les indices d'un tableau a plusieurs dimensions : coupes aux virgules de premier niveau.
            std::vector<std::string> parts;
            std::string cur;
            int nest = 0;
            for (std::size_t k = i + 1; k < j; ++k) {
                const char c = t[k];
                if (c == '[' || c == '(') ++nest;
                else if ((c == ']' || c == ')') && nest > 0) --nest;
                if (c == ',' && nest == 0) { parts.push_back(cur); cur.clear(); continue; }
                cur += c;
            }
            parts.push_back(cur);
            std::string index;
            for (const auto& part : parts) {
                std::string piece = trimmedView(part);
                const bool literal = !piece.empty() && std::all_of(piece.begin() + (piece[0] == '-' ? 1 : 0), piece.end(), [](char c) {
                    return std::isdigit(static_cast<unsigned char>(c)) != 0;
                }) && piece != "-";
                if (!literal) {
                    const auto value = Expression::compile(piece).evaluate(*this);
                    if (!value) return t;
                    piece = std::to_string(value->asInteger());
                }
                index += (index.empty() ? "" : ",") + piece;
            }
            out += "[" + index + "]";
            i = j;
        }
        return out;
    }
    std::map<std::string, sim::Value, std::less<>> vars;
    std::vector<sim::Diagnostic> diagnostics;
    // 1.13.0 : le message brut du moteur (en anglais) de la derniere fonction du projet qui a echoue - le code E_* d'un
    // TRY de l'appelant (lastCallError) ; vide : pas de cause connue.
    std::string lastCallRaw;
    // 1.13.0 : ... et ce que le TRY de l'appelant en recoit tel quel (lastCallRaised) : son code, son message, ou elle
    // est nee. Pose par callFunction, vide a chaque appel.
    struct RaisedCall {
        std::string code, message, origin;
    };
    std::optional<RaisedCall> lastRaised;
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
    // 1.13.0 (lot J6) : le projet porte-t-il une unite quelque part (-1 : pas encore regarde, remis a chaque
    // demarrage) ? Sans unite, declaredUnit rend vide tout de suite : l'affectation d'un chemin, que le moteur
    // regarde a chaque fois, ne coute rien a un projet qui n'en met pas.
    int anyUnit{-1};
    // 1.13.0 (seconde passe) : LES UNITES DECLAREES DU CODE EN COURS - pour un cadre (frames), les declarations de sa
    // fonction ou de son script (la colonne Unite de ses onglets) et l'unite du retour sous le nom de la fonction.
    // Une entree par cadre, retrouvee par son cadre (une tache detachee peut s'intercaler) ; retiree avec lui.
    struct CodeUnits {
        const Frame*                    frame{nullptr};
        const std::vector<Declaration>* decls{nullptr};
        std::string                     function, returnType, returnUnit;
    };
    std::vector<CodeUnits> codeUnits;
    struct CodeUnitsGuard {
        Env&         env;
        const Frame* frame;
        CodeUnitsGuard(Env& e, CodeUnits c) : env(e), frame(c.frame) { env.codeUnits.push_back(std::move(c)); }
        ~CodeUnitsGuard() {
            for (auto it = env.codeUnits.rbegin(); it != env.codeUnits.rend(); ++it)
                if (it->frame == frame) {
                    env.codeUnits.erase(std::next(it).base());
                    break;
                }
        }
        CodeUnitsGuard(const CodeUnitsGuard&) = delete;
        CodeUnitsGuard& operator=(const CodeUnitsGuard&) = delete;
    };
    // 1.13.0 (seconde passe) : l'unite d'un chemin deja demande (le chemin resolu -> l'unite) et la bibliotheque des
    // unites, relues a chaque demarrage (avant : deux parcours des variables et un catalogue refait a chaque affectation).
    std::map<std::string, std::string, std::less<>> unitCache;
    std::unique_ptr<units::Catalog>                 unitCatalog;
    // 1.13.0 (le debogueur) : pendant un espion ou une condition de point, le nombre de cadres d'avant lui (le code en
    // pause), plus un : ceux-la ne s'ecrivent pas (write). 0 : pas de lecture du debogueur en cours.
    std::size_t probeFloor{0};
    // 1.13.0 : le projet a-t-il un membre ou une methode prive (-1 : pas encore regarde, remis a chaque demarrage) ?
    // Sans prive, privateMember rend vide tout de suite : un chemin lu ne coute rien de plus.
    int anyPrivate{-1};
    // Le prive deja lu, par "TYPE|MEMBRE" (majuscules) : le message et le type qui le declare (vide : permis). Remis
    // avec anyPrivate : une boucle qui lit Pompes[i].Debit ne refait pas la recherche dans le type a chaque tour.
    std::map<std::string, std::pair<std::string, std::string>> privateSeen;
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
        if (needsConcretePath(n))                                   // 1.11.24 : V[Idx].Consigne, (V[0]).Consigne
            if (const std::string c = concretePath(n); c != n && !needsConcretePath(c)) return read(c, out);
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
            // 1.12.0 : les enumerations natives (TRANSITION#Fondu, POSITION_POPUP#Objet...) : un DINT.
            if (const natives::EnumValue* nv = nullptr; natives::parseEnumLiteral(n, nullptr, &nv) && nv) {
                out = sim::Value::integer(sim::Type::DInt, nv->number);
                return true;
            }
            return false;
        }
        if (aliases)
            if (const auto* v = aliases->value(n)) { out = *v; return true; }
        const std::string r = resolved(n);
        // 1.14.0 (relecture M4b) : une relecture - un parametre Copie / Les deux d'une popup rejouee se lit dans la piste
        // ($P0_TITRE : la popup de rang 0, son parametre Titre).
        if (rt_.replayOn_ && !r.empty() && r[0] == '$') {
            if (const sim::Value* v = rt_.replayValue(upper(r))) {
                out = *v;
                return true;
            }
            return false;
        }
        // 1.9 : un parametre Copie / Les deux - la copie de la popup, jamais l'original.
        if (!r.empty() && r[0] == '$') return rt_.copyRead(r, out);
        // 1.14.0 (M4) : UNE RELECTURE - la valeur de la piste passe avant tout le reste (variables IHM, automate,
        // variables systeme, proprietes) ; ce qu'elle n'a pas se lit comme avant.
        if (rt_.replayOn_)
            if (const sim::Value* v = rt_.replayValue(upper(r))) {
                out = *v;
                return true;
            }
        if (const auto it = vars.find(upper(r)); it != vars.end()) {
            // Lot 15 : liee a un equipement, elle se relit sur lui - sauf forcee (1.11.5) : elle garde sa valeur.
            if (!rt_.bound_.empty() && !rt_.forcedIhm_.count(it->first)) rt_.readBound(it->first, it->second);
            out = it->second;
            return true;
        }
        // Lot 16 : Consignes.Length, Four1.Words ; un indice hors des bornes d'un
        // tableau IHM rend la valeur par defaut (et le journal le dit).
        if (!rt_.aggregates_.empty() && rt_.aggregateRead(r, out)) return true;
        // 1.12.7 : Reglages.Seuil - la constante publique d'un script general (une variable IHM passe avant).
        // 1.14.0 (M4) : un enregistrement note ce que l'ecran lit (hors variables IHM, toutes prises) pour le garder.
        if (!rt_.qualified_.empty() && r.find('.') != std::string::npos && rt_.qualifiedRead(r, out)) {
            if (rt_.recordOn_) rt_.noteRead(upper(r), r, rec::KeyKind::Qualified);
            return true;
        }
        // Lot 9 : SYS.UserName, Vue.Objet.Propriete, Vue.Open.
        if (r.find('.') != std::string::npos && rt_.publicRead(r, out)) {
            if (rt_.recordOn_) rt_.noteRead(upper(r), r, rec::KeyKind::Public);
            return true;
        }
        if (rt_.plc_ && rt_.plc_->read(plcOf(r), out)) {
            if (rt_.recordOn_) rt_.noteRead(upper(r), r, rec::KeyKind::Plc);
            return true;
        }
        // 1.11.14 : INFO, ERROR... - les niveaux de IHM_LOG, en dernier : une vraie
        // variable du meme nom passe avant.
        if (const auto level = logLevelByName(n); level && n.find('.') == std::string_view::npos) {
            out = sim::Value::integer(sim::Type::DInt, static_cast<long long>(*level));
            return true;
        }
        return false;
    }
    bool write(std::string_view n, const sim::Value& v) override {
        if (needsConcretePath(n))                                   // 1.11.24
            if (const std::string c = concretePath(n); c != n && !needsConcretePath(c)) return write(c, v);
        // 1.13.0 (le debogueur) : un espion, une condition de point - le cadre du code en pause se lit sans s'ecrire ; une
        // fonction appelee par l'espion (son cadre a elle, plus haut) ecrit ses locales.
        if (probeFloor > 0 && frames.size() < probeFloor) {
            diagnostics.push_back({sim::Diagnostic::Severity::Error,
                                   "'" + std::string(n) + "' : un espion ou une condition de point ne modifie rien (lecture seule)", 0, {}});
            return false;
        }
        if (!frames.empty())
            if (const auto it = frames.back()->find(upper(n)); it != frames.back()->end()) { it->second.assignFrom(v); return true; }
        if (aliases && aliases->value(n)) {
            diagnostics.push_back({sim::Diagnostic::Severity::Error,
                                   "'" + std::string(n) + "' est un param\xC3\xA8tre pass\xC3\xA9 par valeur : en lecture seule", 0, {}});
            return false;
        }
        const std::string r = resolved(n);
        // 1.14.0 (relecture M4b) : UNE RELECTURE NE COMMANDE RIEN. Tant qu'elle est affichee, aucune ecriture ne part -
        // ni variable IHM (liee a un equipement ou non), ni automate, ni propriete, ni recette ; les variables locales
        // du code, oui (plus haut). Ignoree comme une variable forcee (le script continue : IHM_RELECTURE_ALLER apres
        // IHM_REJOUER), et dite une fois dans la Console.
        if (rt_.replayOn_) {
            rt_.replayWriteRefused(r);
            return true;
        }
        // Une fonction appelee par une expression de vue calcule, elle n'ecrit
        // rien au-dehors (une expression ne modifie jamais le procede).
        if (rt_.readOnly_ > 0) {
            diagnostics.push_back({sim::Diagnostic::Severity::Error,
                                   rt_.debugProbing_ > 0
                                       ? "'" + std::string(n) + "' : un espion ou une condition de point ne modifie rien (lecture seule)"
                                       : "'" + std::string(n) + "' : une fonction appel\xC3\xA9" "e par une expression de vue n'\xC3\xA9" "crit que ses variables locales",
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
            // 1.12.3 : la Console le dit (une fois par variable), l'action aussi (lastWriteForced_).
            if (rt_.forcedIhm_.count(it->first)) {
                rt_.forcedWriteIgnored(it->first, std::string(r), v);
                return true;
            }
            // 1.11.7 : liee a un esclave simule qui la force (ou l'anime) : le forcage passe
            // avant le script - l'ecriture est ignoree, sans erreur.
            if (!rt_.bound_.empty() && rt_.hooks_.boundForced && rt_.boundVariable(it->first) && rt_.hooks_.boundForced(it->first)) {
                rt_.forcedWriteIgnored(it->first, std::string(r), v);
                return true;
            }
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
            rt_.perfHistory_.write();                             // 1.12.17 : dans le temps
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
        // 1.12.7 : une constante qualifiee (Reglages.Seuil, Reglages.Table[2]) ne s'ecrit pas.
        if (!rt_.qualified_.empty() && r.find('.') != std::string::npos && rt_.qualifiedRoot(r)) {
            diagnostics.push_back({sim::Diagnostic::Severity::Error, r + " est une constante : elle ne s'\xC3\xA9" "crit pas", 0, {}});
            return false;
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
        rt_.perfHistory_.write();                                 // 1.12.17 : dans le temps
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
        if (needsConcretePath(n))                                   // 1.11.24
            if (const std::string c = concretePath(n); c != n && !needsConcretePath(c)) return exists(c);
        if (!frames.empty() && frames.back()->count(upper(n)) != 0) return true;
        if (sim::Value probe; callerLocals > 0 && sim::readCallerLocal(n, probe)) return true;     // 1.11.17
        if (aliases && (aliases->value(n) || aliases->object(n))) return true;     // 1.12.4 : un parametre objet
        const std::string r = resolved(n);
        if (!r.empty() && r[0] == '$') {   // 1.9 : la copie d'un parametre
            if (rt_.replayOn_) return rt_.replayValue(upper(r)) != nullptr;   // 1.14.0 (relecture M4b) : celle de la piste
            sim::Value probe;
            return rt_.copyRead(r, probe) || rt_.copyObject(r) != nullptr;      // 1.12.4 : ou son objet
        }
        if (vars.count(upper(r)) != 0) return true;
        if (rt_.rich_.count(upper(r)) != 0) return true;           // 1.12.2 : une variable objet
        if (!rt_.qualified_.empty() && r.find('.') != std::string::npos) {   // 1.12.7 : une constante qualifiee
            sim::Value probe;
            std::string rest;
            if (rt_.qualifiedRoot(r, &rest) && (rest.empty() || rt_.qualifiedRead(r, probe))) return true;
        }
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
    std::string lastCallError() override { return lastCallRaw; }
    bool lastCallRaised(std::string& code, std::string& message, std::string& origin) override {
        if (!lastRaised) return false;
        code = lastRaised->code;
        message = lastRaised->message;
        origin = lastRaised->origin;
        return true;
    }
    bool call(std::string_view name, std::string_view instance,
              const std::vector<std::pair<std::string, sim::Value>>& args, sim::Value& result) override {
        lastCallRaw.clear();                                   // 1.13.0 : la cause d'un echec, posee par callFunction
        lastRaised.reset();
        const auto u = upper(name);
        if (u.rfind("IHM_", 0) == 0) return ihm(u, args, result);
        // 1.12.16 : une fonction de la vue, par son nom dans la vue (elle passe avant celles du projet).
        // 1.13.0 : la copie liee reste en vie pendant tout l'appel (`keep`), meme si le cache se vide.
        if (const auto keep = viewFunction(name)) return rt_.callFunction(*keep, args, result);
        // Lot 7 : une fonction IHM du projet passe avant celles de l'automate.
        if (rt_.project_)
            if (const auto* f = rt_.project_->functionByKey(name)) return rt_.callFunction(*f, args, result);   // 1.11.20 : "Nom#id"
        // 1.11.10 : la fonction d'une instance de symbole (Vue_Vannes.Vanne_3.Ouvrir).
        if (const auto keep = symbolCall(name)) return rt_.callFunction(*keep, args, result);
        // 1.12.0 : les couleurs (RGB, COULEUR_...) et l'aleatoire (RANDOM...), natives de l'IHM -
        // apres les fonctions du projet et des symboles (un projet qui avait sa fonction Random).
        if (natives::isOwnFunction(u)) {
            std::vector<sim::Value> values;
            for (const auto& a : args) values.push_back(a.second);
            std::string why;
            if (natives::call(u, values, result, &why)) return true;
            diagnostics.push_back({sim::Diagnostic::Severity::Error, why, 0, {}});
            return false;
        }
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
        if (const_cast<Env*>(this)->viewFunction(name)) return true;   // 1.12.16 : une fonction de la vue
        if (rt_.project_ && rt_.project_->functionByKey(name) != nullptr) return true;
        return const_cast<Env*>(this)->symbolCall(name) != nullptr;   // 1.11.10 : {Vanne_3.Etat()}
    }
    // 1.11.10 : "Vue.Instance.Fonction" (ou un parametre de popup qui y mene) : la fonction
    // de l'instance, prete a tourner ; nul : ce n'est pas une fonction d'instance.
    // 1.12.16 : "Vue.Fonction" aussi (une fonction d'une vue ou d'une popup).
    // 1.13.0 : partagee (l'appel la garde en vie, voir Runtime::symbolCall).
    std::shared_ptr<const HmiFunction> symbolCall(std::string_view name) {
        if (!rt_.project_ || name.find('.') == std::string_view::npos) return nullptr;
        const std::string r = resolved(name);
        return rt_.symbolCall(r.empty() ? std::string(name) : r);
    }
    // 1.12.16 : un nom sans point que la portee de la vue mene a "Vue.Fonction" (une fonction de la
    // vue) ; "Pourcent#702" (une surcharge) aussi. Nul : pas une fonction de la vue.
    std::shared_ptr<const HmiFunction> viewFunction(std::string_view name) {
        if (!rt_.project_ || !aliases || name.find('.') != std::string_view::npos) return nullptr;
        const auto hash = name.find('#');
        const std::string_view base = name.substr(0, hash);
        const std::string* to = aliases->alias(base);
        if (!to || to->find('.') == std::string::npos) return nullptr;
        return rt_.symbolCall(*to + std::string(hash == std::string_view::npos ? std::string_view{} : name.substr(hash)));
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
        std::shared_ptr<const HmiFunction> keep = viewFunction(name);   // 1.12.16 : une fonction de la vue
        const HmiFunction* f = keep.get();
        if (!f && rt_.project_) f = rt_.project_->functionByKey(name);
        if (!f) {
            keep = symbolCall(name);                                     // 1.11.10 : {Vanne_3.Etat()}
            f = keep.get();
        }
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
            || u == "IHM_ESCLAVE_SIMULE"                                                    // 1.9
            || u == "IHM_SCRIPT_ACTIF"                                                      // 1.12.26
            || u == "IHM_ENREG_EN_COURS" || u == "IHM_ENREG_EXISTE" || u == "IHM_ENREG_DUREE"   // 1.14.0 (M4)
            || u == "IHM_SON_EST_MUET";                                                     // 1.14.0
    }
    void report(sim::Diagnostic d) override { diagnostics.push_back(std::move(d)); }
    std::string caughtMessage(std::string_view m) override { return shownCodeNames(rt_.project_, frenchSimMessage(m)); }   // 1.12.1 : CATCH Erreur
    // 1.13.0 : le code stable d'une erreur du moteur, lu par CATCH e dans e.Code. Le message est brut
    // (avant frenchSimMessage) : "division by zero" est anglais, les erreurs du dialecte deja francaises.
    std::string caughtCode(std::string_view m) override {
        const auto has = [&](std::string_view s) { return m.find(s) != std::string_view::npos; };
        const auto starts = [&](std::string_view s) { return m.rfind(s, 0) == 0; };
        // 1.13.0 (lot donnees) : avant « absente » (E_CLE), car une valeur JSON absente reste une erreur JSON.
        if (starts("REGEX") || has("motif invalide")) return "E_REGEX";
        if (has("JSON")) return "E_JSON";
        if (has("division by zero") || has("modulo by zero")) return "E_DIVISION";
        if (starts("DYNAMIC_CAST") || starts("STATIC_CAST")) return "E_CONVERSION";
        if (has("hors des bornes")) return "E_BORNE";
        if (has("absente"))                                                       // cle absente de la MAP, MAP_GET : cle absente
            return "E_CLE";
        if (starts("ASSERT ") || starts("ASSERT:")) return "E_ASSERT";
        return "E_MOTEUR";
    }
    // 1.13.0 : les trous {expr[:format]} du message d'un LEVER, remplis comme IHM_JOURNAL (lit aussi
    // les locales du script qui leve - sim::readCallerLocal) ; figes au moment du LEVER.
    std::string interpolateMessage(std::string_view text) override {
        const CallerLocals locals(*this);
        return TextTemplate::compile(std::string(text)).render(*this);
    }
    // 1.13.0 : WAIT_UNTIL - le script tourne-t-il detache (une tache) ? son heure (now_) pour le delai.
    bool   detachedTask() override { return rt_.currentTask_ != nullptr && rt_.taskIsDetached(rt_.currentTask_); }
    // 1.13.0 : le debogueur des scripts IHM - un point d'arret atteint, IHM_POINT_ARRET.
    void breakpointReached(std::uint32_t tag, std::uint32_t line, sim::LineProbe& probe) override { rt_.debugReached(tag, line, probe, false); }
    void debugBreak(std::uint32_t tag, std::uint32_t line, sim::LineProbe& probe) override { rt_.debugReached(tag, line, probe, true); }
    // 1.13.0 : la tache qui tourne est arretee (Arreter pendant une pause) : un appel rate n'est pas une faute.
    bool runStopped() override { return rt_.taskStopped(); }
    bool debugReading() override { return rt_.debugProbing_ > 0; }
    double scriptNow() override { return rt_.now_; }

    // ---- 1.10 : le dialecte IHM ----
    // Le type d'une variable IHM composee ("T_Four", "ARRAY[0..9] OF REAL") : une
    // fonction interne la recoit en entier (copie) ; FOR EACH la parcourt.
    std::string declaredType(std::string_view n) override {
        if (!rt_.project_) return {};
        // 1.12.4 : un parametre objet (recu en valeur, ou sa copie) : le type de son objet.
        if (aliases)
            if (auto own = aliases->object(n); own && own->type) return own->type->text();
        const std::string r = resolved(n);
        if (!r.empty() && r[0] == '$')
            if (auto copy = rt_.copyObject(r); copy && copy->type) return copy->type->text();
        std::string t = types::typeOfPath(*rt_.project_, r);
        if (!t.empty()) return t;
        // 1.12.7 : une constante qualifiee (Reglages.Seuils) : le type de son objet.
        if (!rt_.qualified_.empty() && r.find('.') != std::string::npos) {
            std::string rest;
            if (auto o = rt_.qualifiedRoot(r, &rest); o && rest.empty() && o->type)
                return o->type->kind == sim::TypeDesc::Kind::Struct ? o->type->name : o->type->text();
        }
        // 1.10 (S2) : Vue_A.Pompe1, une instance de symbole : le nom de son symbole
        // (l'operande d'un operateur du symbole).
        if (const auto dot = r.find('.'); dot != std::string::npos && r.find('.', dot + 1) == std::string::npos) {
            if (const auto* v = rt_.project_->viewByName(std::string_view(r).substr(0, dot)))
                if (const auto* o = v->objectByName(std::string_view(r).substr(dot + 1)))
                    if (const auto* sym = symbolOf(*rt_.project_, *o)) return sym->name;
        }
        return {};
    }
    // 1.13.0 (lot J6) : l'unite physique declaree d'un chemin (EN / UNITE, l'affectation convertie d'office). Une
    // variable IHM, un membre d'une structure (herite compris), une case d'un tableau ("Vannes[0]" : l'unite de
    // l'element). 1.13.0 (seconde passe) : un parametre ou une locale d'une fonction IHM (le cadre du moteur les tient)
    // porte l'unite de SA declaration (la colonne Unite) - pas celle d'une variable IHM du meme nom ; une constante de
    // vue (Vue_Four.Seuil_Haut, ou Seuil_Haut dans sa vue) la sienne. Les locales d'un script, tenues par
    // l'interpreteur, passent par localUnit.
    void readAnyUnit() {
        const Project& p = *rt_.project_;
        bool any = false;
        const auto inType = [&](const HmiType& t) {
            for (const auto& m : t.members) any = any || !m.unit.empty();
            for (const auto& f : t.methods) {
                any = any || !f.returnUnit.empty();
                for (const auto& d : f.decls) any = any || !d.unit.empty();
            }
        };
        const auto inFunction = [&](const HmiFunction& f) {
            any = any || !f.returnUnit.empty();
            for (const auto& d : f.decls) any = any || !d.unit.empty();
        };
        for (const auto& v : p.programs.variables) any = any || !v.unit.empty();
        for (const auto& t : p.programs.types) inType(t);
        // Une bibliotheque reliee : ses types et ses fonctions peuvent en porter (avant : toute bibliotheque comptait,
        // meme sans aucune unite, et chaque affectation payait la recherche).
        p.forEachLibraryType([&](const HmiType& t, const LoadedLibrary&) { inType(t); });
        p.forEachLibraryFunction([&](const HmiFunction& f, const LoadedLibrary&) { inFunction(f); });
        p.forEachLibraryScript([&](const Script& sc, const LoadedLibrary&) {
            for (const auto& d : sc.decls) any = any || !d.unit.empty();
        });
        forEachDeclarations(p, [&](const std::vector<Declaration>& decls) {
            for (const auto& d : decls) any = any || !d.unit.empty();
        });
        for (const auto& f : p.programs.functions) any = any || !f.returnUnit.empty();
        for (const auto& v : p.views)
            for (const auto& f : v.functions) any = any || !f.returnUnit.empty();
        anyUnit = any ? 1 : 0;
        unitCache.clear();
        unitCatalog.reset();
    }
    // L'unite d'un chemin qui part d'un nom du code en cours (`frame` : son cadre) ; nullopt : pas un nom de ce code.
    std::optional<std::string> codeUnit(const Frame* frame, std::string_view path) const {
        if (!rt_.project_ || !frame) return std::nullopt;
        const CodeUnits* cu = nullptr;
        for (auto it = codeUnits.rbegin(); it != codeUnits.rend() && !cu; ++it)
            if (it->frame == frame) cu = &*it;
        if (!cu) return std::nullopt;
        std::size_t n = 0;
        while (n < path.size() && (std::isalnum(static_cast<unsigned char>(path[n])) || path[n] == '_')) ++n;
        const std::string root = upper(path.substr(0, n));
        const std::string_view rest = path.substr(n);
        if (cu->decls)
            for (const auto& d : *cu->decls)
                if (upper(d.name) == root) return units::unitAlong(*rt_.project_, d.type, d.unit, rest);
        if (!cu->function.empty() && upper(cu->function) == root) return units::unitAlong(*rt_.project_, cu->returnType, cu->returnUnit, rest);
        return std::nullopt;
    }
    // Une constante de vue : Vue_Four.Seuil_Haut[.membre...] ; vide : pas une constante (ou sans unite).
    std::string viewConstantUnit(std::string_view r) const {
        const auto dot = r.find('.');
        if (dot == std::string_view::npos) return {};
        const View* v = rt_.project_->viewByName(r.substr(0, dot));
        if (!v) return {};
        std::size_t n = dot + 1;
        while (n < r.size() && (std::isalnum(static_cast<unsigned char>(r[n])) || r[n] == '_')) ++n;
        const Declaration* c = viewmembers::constant(*v, r.substr(dot + 1, n - dot - 1));
        return c ? units::unitAlong(*rt_.project_, c->type, c->unit, r.substr(n)) : std::string{};
    }
    // 1.13.1 : une constante globale - la constante publique d'un script general, Reglages.Pression[.membre...] ;
    // vide : pas une telle constante (ou sans unite). Avant, EN(Reglages.Pression, 'mbar') ne convertissait rien.
    std::string globalConstantUnit(std::string_view r) const {
        const auto dot = r.find('.');
        if (dot == std::string_view::npos || !rt_.project_->generalScript(r.substr(0, dot))) return {};
        std::size_t n = dot + 1;
        while (n < r.size() && (std::isalnum(static_cast<unsigned char>(r[n])) || r[n] == '_')) ++n;
        const Declaration* c = calc::qualified(*rt_.project_, r.substr(0, n));
        return c ? units::unitAlong(*rt_.project_, c->type, c->unit, r.substr(n)) : std::string{};
    }
    std::string declaredUnit(std::string_view n) override {
        if (!rt_.project_) return {};
        if (anyUnit < 0) readAnyUnit();
        if (anyUnit == 0) return {};
        if (!frames.empty()) {
            std::size_t k = 0;
            while (k < n.size() && (std::isalnum(static_cast<unsigned char>(n[k])) || n[k] == '_')) ++k;
            if (k > 0 && frames.back()->count(upper(n.substr(0, k)))) return codeUnit(frames.back(), n).value_or(std::string{});
        }
        const std::string r = resolved(n);
        if (r.empty()) return {};
        if (const auto it = unitCache.find(r); it != unitCache.end()) return it->second;
        std::string u;
        if (const Variable* var = rt_.project_->variable(r)) u = var->unit;
        else u = units::unitOfPath(*rt_.project_, r);
        if (u.empty() && !rt_.project_->variable(r.substr(0, r.find_first_of(".["))) ) u = viewConstantUnit(r);
        if (u.empty() && !rt_.project_->variable(r.substr(0, r.find_first_of(".[")))) u = globalConstantUnit(r);   // 1.13.1
        unitCache.emplace(r, u);
        return u;
    }
    // 1.13.0 (seconde passe) : une locale du script ou de la fonction en cours, tenue par l'interpreteur.
    std::string localUnit(std::string_view path) override {
        if (!rt_.project_ || frames.empty()) return {};
        if (anyUnit < 0) readAnyUnit();
        if (anyUnit == 0) return {};
        return codeUnit(frames.back(), path).value_or(std::string{});
    }
    // 1.13.0 (lot J6) : la conversion d'une unite vers une autre (meme famille), par la bibliotheque du projet.
    bool convertUnit(std::string_view from, std::string_view to, double& value) override {
        if (!rt_.project_) return false;
        // Une fois par demarrage ; 1.13.0 : celles des bibliotheques reliees aussi.
        if (!unitCatalog) unitCatalog = std::make_unique<units::Catalog>(biblio::unitConfig(*rt_.project_));
        if (const auto aff = unitCatalog->convert(from, to)) { value = aff->apply(value); return true; }
        return false;
    }
    // 1.12.6 (lot B) : les valeurs d'une enumeration du projet (le lecteur montre leur texte, une
    // commande ecrit par leur nom).
    bool enumItems(std::string_view typeName, std::vector<EnumItem>& out) override {
        if (!rt_.project_) return false;
        const HmiType* e = findEnumeration(*rt_.project_, typeName);
        if (!e) return false;
        out.clear();
        for (const auto& v : e->values) out.push_back(EnumItem{v.name, v.text.empty() ? v.name : v.text, v.value});
        return true;
    }
    // 1.12.2 : une variable IHM d'un type objet (LIST, VECTOR, MAP, TUPLE) : son objet, en place.
    sim::ObjRef richVariable(std::string_view n) override {
        if (!frames.empty() && frames.back()->count(upper(n)) != 0) return nullptr;    // une locale du meme nom
        if (aliases)
            if (auto own = aliases->object(n)) return own;                // 1.12.4 : un parametre objet recu en valeur
        const std::string r = resolved(n);
        if (!r.empty() && r[0] == '$') return rt_.copyObject(r);            // 1.12.4 : la copie d'un parametre objet
        // 1.12.7 : la constante qualifiee d'un type objet (Reglages.Seuils) : en place, en lecture seule
        // (constantVariable) ; une valeur simple se lit par read().
        if (!rt_.qualified_.empty() && r.find('.') != std::string::npos) {
            std::string rest;
            if (auto o = rt_.qualifiedRoot(r, &rest); o && rest.empty() && o->type && o->type->kind != sim::TypeDesc::Kind::Scalar) return o;
        }
        if (rt_.rich_.empty()) return nullptr;
        const auto it = rt_.rich_.find(upper(r.empty() ? std::string(n) : r));
        return it == rt_.rich_.end() ? nullptr : it->second;
    }
    // 1.12.7 : une constante qualifiee d'un autre script : lue en place, jamais ecrite.
    // 1.13.0 (le debogueur) : pendant un espion, une condition de point, une lecture de la pile, toute variable IHM
    // objet (LIST, MAP...) se lit sans s'ecrire - un LIST_ADD ou un VECTOR_POP y est refuse.
    bool constantVariable(std::string_view n) override {
        if (rt_.debugProbing_ > 0) return true;
        if (rt_.qualified_.empty()) return false;
        const std::string r = resolved(n);
        return r.find('.') != std::string::npos && rt_.qualifiedRoot(r) != nullptr;
    }
    // 1.12.4 : une structure IHM (ou un tableau de structures) qui porte des membres objets ; la copie
    // d'un parametre (une structure en Copie) aussi.
    bool hasRichMembers(std::string_view n) override {
        if (!frames.empty() && frames.back()->count(upper(n)) != 0) return false;
        // 1.12.7 : un script general dont une constante publique est un objet (Reglages.Seuils['bas']) :
        // le dialecte passe par ses emplacements (une cle de texte reste un texte).
        if (!rt_.qualified_.empty() && n.find('.') == std::string_view::npos && !(aliases && (aliases->object(n) || aliases->value(n)))) {
            const std::string pre = upper(n) + ".";
            for (auto it = rt_.qualified_.lower_bound(pre); it != rt_.qualified_.end() && it->first.compare(0, pre.size(), pre) == 0; ++it)
                if (it->second && it->second->type && it->second->type->kind != sim::TypeDesc::Kind::Scalar) return true;
        }
        if (aliases && (aliases->object(n) || aliases->value(n))) return false;
        const std::string r = resolved(n);
        if (!r.empty() && r[0] == '$') return rt_.copyHasObjects(r);
        // 1.12.16 : une constante de la vue (Couleurs -> Vue_Four.Couleurs) qui est un objet.
        if (!rt_.qualified_.empty() && r.find('.') != std::string::npos && r != n)
            if (auto o = rt_.qualifiedRoot(r); o && o->type && o->type->kind != sim::TypeDesc::Kind::Scalar) return true;
        if (rt_.rich_.empty()) return false;
        return rt_.hasRichUnder(r.empty() ? std::string(n) : r);
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
    // 1.12.20 : un type rappel nomme du projet (T_Filtre) : sa forme (CALLBACK (x : REAL) : BOOL).
    std::string typeAlias(std::string_view name) override {
        const std::string form = rt_.project_ ? types::callbackForm(rt_.project_, name) : std::string{};
        return upper(trimmedView(name)).rfind("CALLBACK", 0) == 0 ? std::string{} : form;
    }
    // 1.12.19 : les rappels - un minuteur (IHM_APRES, IHM_CHAQUE), un rappel vide.
    bool scheduleCallback(std::int64_t delayMs, std::int64_t periodMs, sim::ObjRef callback, std::int64_t& id, std::string* why) override {
        return rt_.scheduleCallback(delayMs, periodMs, std::move(callback), id, why);
    }
    bool cancelCallback(std::int64_t id) override { return rt_.cancelCallback(id); }
    bool watchCallback(const std::string& path, const sim::Value& now, sim::ObjRef callback, bool passValue, std::int64_t& id,
                       std::string* why) override {                                       // 1.12.20 : IHM_SUR_CHANGEMENT
        return rt_.watchCallback(path, now, std::move(callback), passValue, id, why);
    }
    void nullCallback(std::string_view name, std::string_view where) override { rt_.nullCallback(name, where); }
    // 1.12.26 : WAIT - un script detache rend la main jusqu'a son heure ; un autre bloque l'IHM (10 s au plus).
    bool wait(std::int64_t milliseconds, std::string& why) override { return rt_.waitScript(milliseconds, why); }
    // 1.12.26 : FILE_READ, FILE_WRITE, DIR_LIST, RESOURCE_READ... (HmiRuntimeFiles.cpp).
    bool fileOp(std::string_view op, const std::vector<std::string>& args, FileResult& out, std::string& why) override {
        if (op == "IHM_ENREG_LISTE") return rt_.recordingFileOp(op, args, out, why);   // 1.14.0 (M4) : les noms du dossier
        return rt_.fileOp(op, args, out, why);
    }
    // 1.10 (integration, S1 <-> S2) : un operateur d'un type IHM ou d'un symbole
    // (`a + b`, `a += b`, `TO_xxx(a)`...) : S2 le trouve, S1 l'appelle.
    bool findOperator(std::string_view op, std::string_view l, std::string_view r, sim::OperatorSource& out) override {
        hmi::OperatorCall c;
        if (!rt_.project_ || !hmi::resolveOperator(*rt_.project_, op, l, r, c)) return false;
        out.key = std::move(c.key);
        out.function = std::move(c.function);
        out.owner = std::move(c.owner);
        // 1.13.0 (le debogueur) : l'operateur trouve (son id) - ses points d'arret, son nom dans la pile. Le meme choix
        // que resolveOperator : une conversion par sa source ou sa cible, un operateur par ses deux types.
        if (rt_.debugOn_) {
            const std::string u = upper(std::string(op));
            const HmiOperator* found = u.size() > 3 && u.compare(0, 3, "TO_") == 0 ? hmi::findConversion(*rt_.project_, u, l)
                                                                                   : hmi::findOperator(*rt_.project_, op, l, r);
            if (found) out.debugId = found->id;
        }
        return true;
    }
    // 1.13.0 (lot J1) : une methode d'un type IHM (chemin.Methode(args)) - S2 (hmi::resolveMethod) la
    // trouve sur le type reel de l'objet, S1 (l'interpreteur) l'appelle, SELF en VAR_IN_OUT.
    bool findMethod(std::string_view ownerType, std::string_view method, sim::OperatorSource& out) override {
        hmi::MethodCall c;
        if (!rt_.project_ || !hmi::resolveMethod(*rt_.project_, ownerType, method, c)) return false;
        out.key = std::move(c.key);
        out.function = std::move(c.function);
        out.owner = std::move(c.owner);
        out.debugId = c.method;                                       // 1.13.0 (le debogueur) : ses points d'arret
        out.selfType = std::move(c.selfType);        // 1.13.0 : le prive a l'execution
        out.privateWhy = std::move(c.privateWhy);
        return true;
    }
    // 1.13.0 (lot J1) : SUPER.M(...) emis comme METHODE_<parent>_M - l'interpreteur le resout ici.
    bool findMethodByInternal(std::string_view internalName, sim::OperatorSource& out) override {
        hmi::MethodCall c;
        if (!rt_.project_ || !hmi::resolveMethodInternal(*rt_.project_, internalName, c)) return false;
        out.key = std::move(c.key);
        out.function = std::move(c.function);
        out.owner = std::move(c.owner);
        out.debugId = c.method;                                       // 1.13.0 (le debogueur) : ses points d'arret
        out.selfType = std::move(c.selfType);        // 1.13.0 : son cadre est une methode de ce type (le prive)
        return true;
    }
    // 1.13.0 : LE PRIVE A L'EXECUTION (sim::Environment::privateMember) - le porteur : le type d'un objet, ou celui du
    // chemin (une variable IHM, une case, un membre). Le debogueur (une expression sondee) voit tout.
    std::string privateMember(std::string_view type, std::string_view path, std::string_view member, std::string& owner) override {
        if (!rt_.project_ || rt_.debugProbing_ > 0) return {};
        const Project& p = *rt_.project_;
        if (anyPrivate < 0) {
            bool any = false;
            const auto scan = [&](const HmiType& t) {
                for (const auto& m : t.members) any = any || m.visibility == Visibility::Private;
                for (const auto& m : t.methods) any = any || m.visibility == Visibility::Private;
            };
            for (const auto& t : p.programs.types) scan(t);
            p.forEachLibraryType([&](const HmiType& t, const LoadedLibrary&) { scan(t); });
            anyPrivate = any ? 1 : 0;
        }
        if (anyPrivate == 0) return {};
        std::string holder(type);
        if (holder.empty()) {
            if (path.empty()) return {};
            const std::string r = resolved(path);
            if (r.empty() || r[0] == '$') return {};
            holder = types::typeOfPath(p, r);
            if (holder.empty()) return {};
        }
        std::string key = upper(holder);
        key += '|';
        key += upper(member);
        auto it = privateSeen.find(key);
        if (it == privateSeen.end()) {
            std::string declared;
            std::string why = types::privateMemberUse(p, holder, member, &declared);
            it = privateSeen.emplace(std::move(key), std::make_pair(std::move(why), std::move(declared))).first;
        }
        owner = it->second.second;
        return it->second.first;
    }
    // 1.13.0 (le debogueur) : une methode, un operateur va tourner - ses points d'arret (markRichFunction).
    void debugFunction(const sim::Function& f, std::uint32_t id) override { rt_.markRichFunction(f, static_cast<Id>(id)); }
    // 1.13.0 (lot J1) : la montee - un sous-type vers un parent, un type vers une interface implementee
    // (pour DYNAMIC_CAST<to>(objet)). Le meme type va toujours.
    bool typeConvertsTo(std::string_view from, std::string_view to) override {
        if (!rt_.project_) return false;
        return hmi::isA(*rt_.project_, from, to) || hmi::implementsInterface(*rt_.project_, from, to);
    }
    // 1.13.0 : une interface du projet, cible de DYNAMIC_CAST<I_x> (elle n'a pas de membre).
    bool interfaceType(std::string_view name) override { return rt_.project_ && hmi::isInterface(*rt_.project_, name); }
    // Une fonction IHM du projet qui emploie les types riches (VAR_IN_OUT, REF_TO,
    // POINTER TO, ARRAY, MAP, structure) tourne dans le dialecte, appelee du
    // script avec ses arguments riches ; une fonction simple, comme avant (env.call).
    std::shared_ptr<const sim::Function> dialectFunction(std::string_view name) override {
        std::shared_ptr<const HmiFunction> keep = viewFunction(name);   // 1.12.16 : une fonction de la vue
        const HmiFunction* f = keep.get();
        if (!f && rt_.project_) f = rt_.project_->functionByKey(name);                      // 1.11.20 : "Nom#id" aussi
        if (!f) {
            keep = symbolCall(name);                      // 1.11.10 : une fonction d'instance aux types riches
            f = keep.get();
        }
        if (!f) return nullptr;
        // 1.13.0 (le debogueur) : une copie liee (fonction de vue, de symbole) marque les points de la fonction qu'elle copie.
        const Id debugId = f->debugOrigin != kNoId ? f->debugOrigin : f->id;
        // 1.13.0 (lot J8) : une fonction d'une bibliotheque non chargee (son interface seule) : callFunction le dit.
        if (const LoadedLibrary* lib = rt_.project_ ? rt_.project_->libraryOf(f->id) : nullptr; lib && lib->fromCache) return nullptr;
        // 1.13.0 (lot J8) : une fonction d'un espace (ou qui importe des espaces) : ses noms courts qualifies.
        const std::string text = (!f->ns.empty() || names::mentionsUsing(f->body)) && rt_.project_
                                     ? names::qualify(*rt_.project_, functionText(*f), f->ns, {f->name}, true)
                                     : functionText(*f);
        if (const auto it = richFunctions_.find(text); it != richFunctions_.end()) {
            if (it->second) rt_.markRichFunction(*it->second, debugId);    // 1.13.0 : ses points d'arret
            return it->second;
        }
        std::shared_ptr<const sim::Function> fn;
        if (auto parsed = sim::parseFunction(text, "fonction " + f->name); parsed && !sim::functionIsSimple(**parsed)) fn = *parsed;
        richFunctions_[text] = fn;
        if (fn) rt_.markRichFunction(*fn, debugId);
        return fn;
    }
    // ---- 1.11.20 : les surcharges (la regle de hmi::overload, celle du controle) ----
    // Les fonctions de ce nom : du projet ("Convertir"), ou du symbole d'une instance
    // ("Vue.Vanne_3.Ouvrir", un parametre de popup qui y mene).
    std::vector<const HmiFunction*> named(std::string_view name) {
        if (!rt_.project_ || name.find('#') != std::string_view::npos) return {};
        if (name.find('.') == std::string_view::npos) {
            // 1.12.16 : une fonction de la vue (par son nom dans la vue) passe avant celles du projet.
            if (const std::string* to = aliases ? aliases->alias(name) : nullptr; to && to->find('.') != std::string::npos)
                if (auto fs = symbolFunctionsAt(*rt_.project_, *to); !fs.empty()) return fs;
            return rt_.project_->functionsNamed(name);
        }
        // 1.13.0 (lot J8) : Util.Max - les fonctions (et surcharges) d'un espace de noms.
        if (auto fs = rt_.project_->functionsNamed(name); !fs.empty()) return fs;
        const std::string r = resolved(name);
        return symbolFunctionsAt(*rt_.project_, r.empty() ? std::string(name) : r);
    }
    // Le choix a l'execution : un type encore inconnu ici ne peut plus attendre - ambigu.
    static bool decide(const std::vector<overload::Signature>& sigs, const std::vector<sim::ArgShape>& shapes, int& index,
                       std::string& why) {
        const auto c = overload::choose(sigs, overload::argsOf(shapes));
        index = c.chosen;
        if (c.chosen >= 0) return true;
        if (c.uncertain) {
            std::string shapesText;
            for (std::size_t i = 0; i < c.tied.size(); ++i)
                shapesText += (i ? " et " : "") + sigs[static_cast<std::size_t>(c.tied[i])].shape();
            why = "appel ambigu de " + sigs.front().name + " : " + shapesText
                + " conviennent autant (le type d'un argument n'est pas connu) - pr\xC3\xA9" "cisez-le (TO_INT(x), INT#5)";
        } else {
            why = c.why;
        }
        return false;
    }
    int overloads(std::string_view name) override { return static_cast<int>(named(name).size()); }
    // 1.13.0 (lot J8) : la signature d'une fonction du projet, ses types en noms complets (vus de son espace).
    overload::Signature signatureIn(const HmiFunction& f) const {
        auto sig = overload::signatureOf(f);
        if (!rt_.project_ || f.ns.empty()) return sig;
        for (auto& prm : sig.params) prm.type = names::canonicalType(*rt_.project_, prm.type, f.ns);
        sig.result = names::canonicalType(*rt_.project_, sig.result, f.ns);
        return sig;
    }
    bool chooseOverload(std::string_view name, const std::vector<sim::ArgShape>& args, std::string& key, std::string& why) override {
        const auto fs = named(name);
        if (fs.empty()) return false;
        std::vector<overload::Signature> sigs;
        for (const auto* f : fs) sigs.push_back(signatureIn(*f));
        int k = -1;
        // 1.13.0 (lot J8) : les types des arguments en noms complets aussi (T_Vanne ecrit court = Gaz.T_Vanne).
        std::vector<sim::ArgShape> shapes = args;
        if (rt_.project_ && names::active(*rt_.project_))
            for (auto& a : shapes) a.type = names::canonicalType(*rt_.project_, a.type);
        if (!decide(sigs, shapes, k, why)) return false;
        key = std::string(name) + sigs[static_cast<std::size_t>(k)].key;       // "Convertir#615", "Vue.Vanne_3.Ouvrir#702"
        return true;
    }
    int chooseInner(const std::vector<const sim::Function*>& candidates, const std::vector<sim::ArgShape>& args, std::string& why) override {
        std::vector<overload::Signature> sigs;
        for (const auto* f : candidates) sigs.push_back(overload::signatureOf(*f));
        int k = -1;
        if (sigs.empty() || !decide(sigs, args, k, why)) return -1;
        return k;
    }
    std::string resultType(std::string_view name, const std::vector<sim::ArgShape>& args) override {
        const auto fs = named(name);
        if (fs.empty()) {
            std::shared_ptr<const HmiFunction> keep = viewFunction(name);   // 1.12.16
            const HmiFunction* f = keep.get();
            if (!f && rt_.project_) f = rt_.project_->functionByKey(name);
            if (!f && name.find('.') != std::string_view::npos) {
                keep = symbolCall(name);
                f = keep.get();
            }
            return f ? f->returnType : std::string{};
        }
        if (fs.size() == 1) return fs.front()->returnType;
        std::vector<overload::Signature> sigs;
        for (const auto* f : fs) sigs.push_back(signatureIn(*f));
        const auto c = overload::choose(sigs, overload::argsOf(args));
        return c.chosen >= 0 ? sigs[static_cast<std::size_t>(c.chosen)].result : std::string{};
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
    bool ihm(const std::string& u, const std::vector<std::pair<std::string, sim::Value>>& given, sim::Value& result) {
        // 1.12.0 : un argument donne par une enumeration native (TRANSITION#Fondu) : le mot que
        // la fonction lisait deja ('Fondu').
        std::vector<std::pair<std::string, sim::Value>> mapped;
        for (std::size_t i = 0; i < given.size(); ++i)
            if (auto word = natives::enumArgument(u, i, given[i].second)) {
                if (mapped.empty()) mapped = given;
                mapped[i].second = sim::Value::text(std::move(*word));
            }
        const auto& args = mapped.empty() ? given : mapped;
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
        // 1.14.0 (M4) : les enregistrements et la relecture (HmiRuntimeRecord.cpp).
        if (u.rfind("IHM_ENREG_", 0) == 0 || u == "IHM_REJOUER" || u.rfind("IHM_RELECTURE_", 0) == 0)
            if (rt_.recordingNative(u, args, result)) return true;
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
        // 1.12.26 : les scripts detaches - tourne-t-il ; l'arreter.
        if (u == "IHM_SCRIPT_ACTIF") {
            result = sim::Value::boolean(rt_.scriptRunning(text(0)));
            return true;
        }
        if (u == "IHM_SCRIPT_ARRETER") {
            result = sim::Value::boolean(rt_.stopScript(text(0)));
            return true;
        }
        if (u == "IHM_SON") {
            result = sim::Value::boolean(rt_.playSound(text(0), rt_.source_, SoundChannel::Scripts));   // 1.14.0 : le canal Scripts
            return true;
        }
        // 1.14.0 : couper le son, le melangeur.
        //   IHM_SON_MUET(TRUE) coupe tout (ce qui joue se tait) ; FALSE le retablit (rien n'est rejoue).
        //   IHM_SON_EST_MUET() : vrai si tout est coupe (NOT SYS.SoundEnabled).
        //   IHM_SON_VOLUME('Alarmes', 80) ; IHM_SON_CANAL_MUET('Videos', TRUE) - 'General' : le volume, le muet de tout.
        //   Vrai : fait ; faux : canal inconnu (le journal le dit).
        if (u == "IHM_SON_MUET") {
            (void)rt_.setChannelMuted(SoundChannel::General, args.empty() || args[0].second.isTruthy(), rt_.source_);
            result = sim::Value::boolean(true);
            return true;
        }
        if (u == "IHM_SON_EST_MUET") {
            result = sim::Value::boolean(soundMuted(rt_.settings_, SoundChannel::General));
            return true;
        }
        if (u == "IHM_SON_VOLUME" || u == "IHM_SON_CANAL_MUET") {
            const auto channel = soundChannelFrom(text(0));
            if (!channel) {
                rt_.log("Erreur", rt_.source_, u + " : canal inconnu '" + text(0) + "' (General, Alarmes, Scripts, Actions, Videos ou Relecture)");
                result = sim::Value::boolean(false);
                return true;
            }
            if (u == "IHM_SON_VOLUME") {
                double v = 0;
                if (args.size() > 1) {
                    const auto& a = args[1].second;
                    if (a.type() == sim::Type::Real) v = a.asReal();
                    else if (a.type() == sim::Type::String) { if (!parseNumber(a.asString(), v)) v = 0; }
                    else v = static_cast<double>(a.asInteger());
                }
                (void)rt_.setChannelVolume(*channel, static_cast<int>(std::lround(v)), rt_.source_);
            } else {
                (void)rt_.setChannelMuted(*channel, args.size() < 2 || args[1].second.isTruthy(), rt_.source_);
            }
            result = sim::Value::boolean(true);
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
// ---- 1.12.26 : LES SCRIPTS DETACHES ------------------------------------------------------------------------------
// Ce qui decrit le code qui tourne : a chaque passage de main entre l'IHM et une tache, la tache retrouve le sien (sa
// source, sa pile de cadres, les parametres de vue qu'elle voit, ses compteurs...) et l'IHM le sien.
struct Runtime::ExecState {
    std::string                  source;
    CodeOrigin                   origin;
    sim::ExecTrace               trace;
    int                          depth{0};
    std::string                  abort;
    int                          readOnly{0};
    int                          gesture{0};
    std::string                  gestureWhere, gestureVerb;
    int                          exportAsk{0};
    bool                         inCycle{false};
    std::string                  actionOrigin;
    std::vector<SlowFrame>       slow;
    std::vector<Env::Frame*>     frames;
    const Scope*                 aliases{nullptr};
    std::vector<const Scope*>    savedAliases;
    int                          callerLocals{0};
    std::vector<sim::Diagnostic> diagnostics;
    std::vector<DebugExec>       debugExec;       // 1.13.0 : les executions en cours (le debogueur)
    int                          execDepth{0};    // 1.13.0 : leur nombre (un code change pendant la pause)
};

struct Runtime::TaskState {
    Id                          script{kNoId};
    std::string                 name, source;
    Script                      copy;                 // le script tel qu'il est parti (le projet peut changer)
    double                      startedAt{0};
    double                      wakeAt{0};            // WAIT : l'heure (de marche) du reveil ; 0 : la reprise suivante
    bool                        waiting{false};
    bool                        stopAsked{false};     // IHM_SCRIPT_ARRETER : la fin le dit
    bool                        ok{true};
    std::int64_t                sliceStart{0};        // steady_clock (ns) : le debut de sa tranche
    ExecState                   state;                // son contexte, quand il n'a pas la main
    std::unique_ptr<ScriptTask> task;
    // 1.13.0 (le debogueur) : un script NON detache peut tourner dans une tache (il peut s'arreter au milieu) ;
    // il garde alors le contexte de qui l'a lance (la vue, le geste, le cycle) et ses limites d'avant.
    bool                         detached{true};
    std::string                  context;             // ce qui l'a lance : "cycle IHM", "Clic sur Vue_A.Btn" (la pile)
    std::string                  why;                 // sa faute (un script non detache fini d'un trait)
    std::shared_ptr<const Scope> keepScope;           // les parametres de la vue, gardes en vie pendant la pause
    std::function<bool(std::string*)> work;           // ce qu'elle fait (un script, un tick, un geste, le demarrage)
    // 1.13.0 : le code en pause a change - ses executions en cours (execDepth_ > 0) s'arretent sans erreur, puis le
    // travail reprend (la suite du cycle, du geste, le demarrage).
    bool                         abandon{false};
    // 1.13.0 : un script detache arrete (un point) dans sa premiere tranche, recue dans un travail du debogueur (un geste,
    // un cycle) : ce travail, qui attend avec lui la fin de cette pause (startDetached, resumePaused).
    TaskState*                   heldWork{nullptr};
};

void Runtime::exchange(ExecState& s) {
    std::swap(source_, s.source);
    std::swap(origin_, s.origin);
    std::swap(trace_, s.trace);
    std::swap(depth_, s.depth);
    std::swap(abort_, s.abort);
    std::swap(readOnly_, s.readOnly);
    std::swap(gesture_, s.gesture);
    std::swap(gestureWhere_, s.gestureWhere);
    std::swap(gestureVerb_, s.gestureVerb);
    std::swap(exportAsk_, s.exportAsk);
    std::swap(inCycle_, s.inCycle);
    std::swap(actionOrigin_, s.actionOrigin);
    std::swap(slowStack_, s.slow);
    std::swap(env_->frames, s.frames);
    std::swap(env_->aliases, s.aliases);
    std::swap(env_->savedAliases_, s.savedAliases);
    std::swap(env_->callerLocals, s.callerLocals);
    std::swap(env_->diagnostics, s.diagnostics);
    std::swap(debugExec_, s.debugExec);              // 1.13.0
    std::swap(execDepth_, s.execDepth);
}

namespace {
constexpr std::int64_t kSliceNs = 8'000'000;          // une tache calcule 8 ms d'affilee au plus, puis rend la main
constexpr std::int64_t kMaxBlockingWaitMs = 10'000;   // un WAIT qui bloque l'IHM : 10 s au plus
constexpr std::size_t  kMaxTasks = 32;
std::int64_t steadyNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string secondsText(double s) {
    char b[32];
    if (s < 10) std::snprintf(b, sizeof b, "%.1f s", s);
    else std::snprintf(b, sizeof b, "%.0f s", s);
    std::string out = b;
    for (auto& c : out) if (c == '.') c = ',';
    return out;
}
} // namespace

bool Runtime::startDetached(const Script& sc, const std::string& source, double now, std::string* error) {
    (void)now;
    for (const auto& t : tasks_)
        if (t->script == sc.id && !t->task->finished()) return true;          // il tourne deja : une instance a la fois
    if (tasks_.size() >= kMaxTasks) {
        const std::string why = std::to_string(kMaxTasks) + " scripts d\xC3\xA9tach\xC3\xA9s tournent d\xC3\xA9j\xC3\xA0 : " + sc.name + " ne d\xC3\xA9marre pas";
        if (error) *error = why;
        log("Erreur", source, why);
        return false;
    }
    auto t = std::make_unique<TaskState>();
    TaskState* raw = t.get();
    raw->script = sc.id;
    raw->name = sc.name;
    raw->source = source;
    raw->copy = sc;
    raw->startedAt = now_;
    raw->task = std::make_unique<ScriptTask>([this, raw](ScriptTask&) {
        // Les sources d'elements des listes se lisent par fil (HmiControls) : ce fil-ci les lit dans ce moteur aussi.
        registerChoiceResolver(raw, [this](const Object& o, std::string_view src, std::vector<Choice>& out) { return resolveChoices(src, out, &o); });
        std::string why;
        raw->ok = runScriptHere(raw->copy, raw->source, now_, &why);
        unregisterChoiceResolver(raw);
    });
    logAt(LogLevel::Info, "Script", source, sc.name + " : d\xC3\xA9marr\xC3\xA9 en arri\xC3\xA8re-plan (d\xC3\xA9tach\xC3\xA9)");
    tasks_.push_back(std::move(t));
    // Sa premiere tranche tout de suite ; lance depuis un autre script detache : a la reprise suivante, par le fil de
    // l'IHM. 1.13.0 : depuis un travail du debogueur (un geste, un cycle sur sa tache), tout de suite aussi - comme sans
    // debogueur, avant la suite du geste.
    if (!currentTask_ || !currentTask_->detached) {
        TaskState* launcher = currentTask_;
        resumeTask(*raw);
        // 1.13.0 : cette premiere tranche s'est arretee (un point) - le travail qui l'a lancee attend avec elle, comme
        // tout le travail en cours attend une pause : la suite du geste (un autre script, une popup) part a la reprise,
        // une fois la tranche finie (resumePaused), dans l'ordre d'un geste sans debogueur.
        if (launcher && paused_ == raw) {
            raw->heldWork = launcher;
            // Faux : la simulation s'arrete (stopTasks reprend chaque tache jusqu'a sa fin) - rien a ranger ici, la suite
            // du geste se defait seule (taskStopped).
            // Le travail retenu ne compte pas la pause dans le journal des lenteurs : ses mesures en cours (le cadre
            // « Clic sur ... ») la retranchent, comme celles de la tache en pause (debugReached).
            const std::int64_t pausedBefore = pausedNs_;
            if (!pauseTask(launcher)) return true;
            for (auto& f : slowStack_) f.t0 += pausedNs_ - pausedBefore;
        }
        finishTasks();
    }
    return true;
}

void Runtime::resumeTask(TaskState& t) {
    if (t.task->finished()) return;
    TaskState* outer = currentTask_;
    exchange(t.state);
    currentTask_ = &t;
    t.sliceStart = steadyNs();
    t.task->resume();
    currentTask_ = outer;
    exchange(t.state);
}

void Runtime::tasksTick(double now) {
    std::vector<TaskState*> due;
    // 1.13.0 : une tache du debogueur (non detachee) ne reprend que par lui (Continuer, un pas, l'arret).
    for (const auto& t : tasks_)
        if (t->detached && !t->task->finished() && (t->task->cancelled() || now >= t->wakeAt)) due.push_back(t.get());
    for (TaskState* t : due) resumeTask(*t);
    finishTasks();
}

void Runtime::finishTasks() {
    for (auto it = tasks_.begin(); it != tasks_.end();) {
        TaskState& t = **it;
        if (!t.task->finished()) {
            ++it;
            continue;
        }
        // 1.13.0 (le debogueur) : le pas en cours finit avec son script ; un script non detache (une tache de
        // debogage) finit sans rien dire, comme avant.
        if (step_ && step_->task == &t) {
            const std::string what = step_->what.empty() ? t.name : step_->what;
            step_.reset();
            stepAll_ = pauseAsked_;
            ++debugRev_;
            logAt(LogLevel::Info, "D\xC3\xA9" "bogage", t.source, what + " est fini : l'IHM continue");
        }
        if (paused_ == &t) clearDebugState();
        if (!t.detached) {
            it = tasks_.erase(it);
            continue;
        }
        const std::string took = secondsText(std::max(0.0, now_ - t.startedAt));
        if (t.stopAsked) logAt(LogLevel::Warning, "Script", t.source, t.name + " : arr\xC3\xAAt\xC3\xA9 (IHM_SCRIPT_ARRETER) apr\xC3\xA8s " + took);
        else if (t.ok) logAt(LogLevel::Info, "Script", t.source, t.name + " : termin\xC3\xA9 (d\xC3\xA9tach\xC3\xA9, " + took + ")");
        it = tasks_.erase(it);                                        // son fil est fini : il se rejoint
    }
}

void Runtime::stopTasks() {
    if (currentTask_) {                                               // depuis une tache : on ne s'attend pas soi-meme
        for (const auto& t : tasks_) t->task->cancel();
        return;
    }
    for (const auto& t : tasks_) t->task->cancel();
    for (const auto& t : tasks_)
        while (!t->task->finished()) resumeTask(*t);                  // chacun se deroule jusqu'a sa fin, sans erreur
    tasks_.clear();
}

bool Runtime::taskSlice() {
    TaskState* t = currentTask_;
    if (!t || !t->detached) return true;
    if (t->task->cancelled()) return false;
    if (steadyNs() - t->sliceStart < kSliceNs) return true;
    t->wakeAt = 0;                                                    // la suite a la reprise suivante (l'IHM respire)
    return t->task->yield();
}

void Runtime::taskLimits(sim::RunLimits& limits) {
    if (!currentTask_ || !currentTask_->detached) return;      // 1.13.0 : une tache de debogage garde les limites d'avant
    // Une tache rend la main toutes les 8 ms : une boucle longue (WHILE TRUE ... WAIT ...) n'est plus une faute.
    limits.maxIterationsPerLoop = std::numeric_limits<std::uint32_t>::max();
    limits.maxStatementsPerScan = std::numeric_limits<std::uint32_t>::max();
    limits.slice = [this] { return taskSlice(); };
}

bool Runtime::waitScript(std::int64_t ms, std::string& why) {
    if (readOnly_ > 0) {
        why = "une fonction appel\xC3\xA9" "e par une expression n'attend pas";
        return false;
    }
    if (TaskState* t = currentTask_; t && t->detached) {      // 1.13.0 : un script non detache (debogue) bloque comme avant
        if (t->task->cancelled()) {
            why.clear();
            return false;
        }
        t->wakeAt = now_ + static_cast<double>(ms) / 1000.0;
        t->waiting = true;
        const bool go = t->task->yield();
        t->waiting = false;
        if (!go) why.clear();
        return go;
    }
    // Un script qui n'est pas detache : il garde la main, l'IHM attend avec lui.
    if (ms > kMaxBlockingWaitMs) {
        why = "un script non d\xC3\xA9tach\xC3\xA9 bloque l'IHM pendant son attente : 10 s au plus (coche \xC2\xAB D\xC3\xA9tach\xC3\xA9 \xC2\xBB dans ses propri\xC3\xA9t\xC3\xA9s)";
        return false;
    }
    if (ms > 0) {
        core::crash::holdWatchdog(true);
        net::preciseWait(static_cast<int>(ms));
        core::crash::holdWatchdog(false);
    }
    return true;
}

// 1.13.0 : les scripts DETACHES seulement - une tache du debogueur (un script, un cycle, un geste qui peut s'arreter)
// n'en est pas un : IHM_SCRIPT_ACTIF repond comme sans debogueur.
std::vector<std::string> Runtime::runningScripts() const {
    std::vector<std::string> out;
    for (const auto& t : tasks_)
        if (t->detached && !t->task->finished()) out.push_back(t->name);
    return out;
}

bool Runtime::scriptRunning(std::string_view name) const {
    const std::string u = upper(name);
    for (const auto& t : tasks_)
        if (t->detached && upper(t->name) == u && !t->task->finished()) return true;
    return false;
}

// 1.13.0 : les scripts DETACHES seulement, comme IHM_SCRIPT_ACTIF - une tache du debogueur (un cycle, un geste, un
// script qui peut s'arreter) n'en est pas un : IHM_SCRIPT_ARRETER ne l'arrete pas (FAUX, comme sans debogueur).
bool Runtime::stopScript(std::string_view name) {
    const std::string u = upper(name);
    bool any = false;
    for (const auto& t : tasks_)
        if (t->detached && upper(t->name) == u && !t->task->finished() && !t->task->cancelled()) {
            t->stopAsked = true;
            t->task->cancel();
            any = true;
        }
    return any;
}

// ---- 1.13.0 : LE DEBOGUEUR DES SCRIPTS IHM - ses taches (HmiRuntimeDebug.cpp pour le reste) ----------------------
bool Runtime::taskIsDetached(const TaskState* t) const noexcept { return t && t->detached; }
bool Runtime::taskIsCancelled(const TaskState* t) const noexcept { return t && t->task && t->task->cancelled(); }
std::string Runtime::taskName(const TaskState* t) const { return t ? t->name : std::string{}; }
std::string Runtime::taskContext(const TaskState* t) const {
    if (!t) return inCycle_ ? std::string("cycle IHM") : std::string("IHM");
    if (t->detached) return "script d\xC3\xA9tach\xC3\xA9 " + t->name;
    // 1.13.0 : une tache du debogueur porte tout un travail (un tick, un geste) : ce qui tourne a l'arret le dit - le
    // geste en cours ("Clic sur Vue_A/Btn"), le cycle, sinon ce qui l'a lance.
    if (gesture_ > 0 && !gestureWhere_.empty()) return (gestureVerb_.empty() ? std::string("Action sur") : gestureVerb_) + " " + gestureWhere_;
    if (inCycle_) return "cycle IHM";
    return t->context.empty() ? std::string("appel du script") : t->context;
}
void Runtime::cancelTask(TaskState* t) noexcept {
    if (t && t->task) t->task->cancel();
}

bool Runtime::pauseTask(TaskState* t) {
    if (!t || !t->task) return false;
    t->wakeAt = 0;
    return t->task->yield();
}

void Runtime::resumePaused(TaskState* t) {
    if (!t) return;
    if (currentTask_) return;                          // jamais depuis une tache : le fil de l'IHM le fera
    resumeTask(*t);
    rethrowTaskFailure(*t);
    // 1.13.0 : un script detache arrete dans sa premiere tranche - le travail qui l'a lance (un geste) l'attendait : sa
    // tranche finie (ni une nouvelle pause, ni un pas qui s'y arrete), ce travail reprend la ou il etait.
    TaskState* held = nullptr;
    if (paused_ != t && t->heldWork) {
        held = t->heldWork;
        t->heldWork = nullptr;
    }
    finishTasks();
    if (held) {
        resumeTask(*held);
        rethrowTaskFailure(*held);
        finishTasks();
    }
    // 1.13.0 : la pause est finie (le travail est alle au bout) - les relachements gardes partent tout de suite, avant
    // qu'une touche ne soit rappuyee (sinon : prise pour une repetition, puis relachee alors qu'elle est enfoncee).
    if (!paused_) replayDeferredInput(now_);
}

// 1.13.0 : une exception sortie du travail d'une tache du debogueur (non detachee) n'est pas perdue : elle remonte ici,
// sur le fil de l'IHM, comme quand le travail tournait la (le rapport de plantage). Un script detache : comme avant.
void Runtime::rethrowTaskFailure(TaskState& t) {
    if (t.detached || !t.task || !t.task->finished()) return;
    if (std::exception_ptr failure = t.task->takeFailure()) {
        finishTasks();
        std::rethrow_exception(failure);
    }
}

void Runtime::withTaskState(TaskState* t, const std::function<void()>& run) {
    if (!t) {
        run();
        return;
    }
    exchange(t->state);
    run();
    exchange(t->state);
}

// 1.13.0 : UNE LECTURE DU DEBOGUEUR (un espion, la condition d'un point, les valeurs de la ligne) ne laisse rien
// derriere elle : rien ne s'ecrit (readOnly_, les cadres du code en pause en lecture seule), rien ne se dit a la
// Console, et une fonction qui y echoue ne laisse au script ni sa faute (abort_) ni ses diagnostics - il reprend comme
// s'il n'avait pas ete lu.
void Runtime::probeQuietly(const std::function<void()>& run) {
    const std::string savedAbort = abort_;
    auto savedDiagnostics = env_->diagnostics;
    const std::string savedLastCall = env_->lastCallRaw;
    const auto savedRaised = env_->lastRaised;
    const std::size_t savedFloor = env_->probeFloor;
    env_->probeFloor = env_->frames.size() + 1;                  // les cadres d'avant, plus un (0 : pas de lecture)
    ++debugProbing_;
    ++readOnly_;
    run();
    --readOnly_;
    --debugProbing_;
    env_->probeFloor = savedFloor;
    env_->lastCallRaw = savedLastCall;
    env_->lastRaised = savedRaised;
    env_->diagnostics = std::move(savedDiagnostics);
    abort_ = savedAbort;
}

// Un travail NON detache que le debogueur peut arreter : il tourne dans une tache, tout de suite et d'un trait s'il ne
// s'arrete pas (le resultat est le meme qu'avant). Il part du contexte de qui le lance - la vue et ses parametres, le
// geste, le cycle - comme s'il tournait ici. Faux dans `finished` : en pause (ou arrete), il reprendra.
bool Runtime::runDebugTask(std::string name, std::function<bool(std::string*)> work, std::string* error, bool* finished) {
    if (finished) *finished = true;
    if (tasks_.size() >= kMaxTasks) return work(error);
    auto t = std::make_unique<TaskState>();
    TaskState* raw = t.get();
    raw->name = std::move(name);
    raw->source = source_;
    raw->startedAt = now_;
    raw->detached = false;
    raw->work = std::move(work);
    // Ce qui le lance (le bas de la pile du debogueur).
    if (gesture_ > 0 && !gestureWhere_.empty()) raw->context = (gestureVerb_.empty() ? std::string("Action sur") : gestureVerb_) + " " + gestureWhere_;
    else if (inCycle_) raw->context = "cycle IHM";
    else if (!source_.empty()) raw->context = "appel\xC3\xA9 par " + source_;
    ExecState& st = raw->state;
    st.source = source_;
    st.origin = origin_;
    st.depth = depth_;
    st.readOnly = readOnly_;
    st.gesture = gesture_;
    st.gestureWhere = gestureWhere_;
    st.gestureVerb = gestureVerb_;
    st.exportAsk = exportAsk_;
    st.inCycle = inCycle_;
    st.actionOrigin = actionOrigin_;
    st.aliases = env_->aliases;
    // Les parametres de la vue : gardes en vie tant que la tache les lit (la popup peut se fermer pendant la pause).
    for (const auto& slot : slots_)
        if (slot.scope && slot.scope.get() == env_->aliases) raw->keepScope = slot.scope;
    if (!raw->keepScope && currentScope_ && currentScope_.get() == env_->aliases) raw->keepScope = currentScope_;
    if (!raw->keepScope && gestureScope_ && gestureScope_.get() == env_->aliases) raw->keepScope = gestureScope_;
    raw->task = std::make_unique<ScriptTask>([this, raw](ScriptTask&) {
        registerChoiceResolver(raw, [this](const Object& o, std::string_view src, std::vector<Choice>& out) { return resolveChoices(src, out, &o); });
        raw->ok = raw->work(&raw->why);
        unregisterChoiceResolver(raw);
    });
    tasks_.push_back(std::move(t));
    resumeTask(*raw);
    if (raw->task->finished()) {
        rethrowTaskFailure(*raw);                      // une exception du travail : sur le fil de l'IHM, comme avant
        // Fini d'un trait : comme avant - sa faute, et la cause premiere d'un enchainement coupe.
        const bool ok = raw->ok;
        if (!ok && error) *error = raw->why;
        if (!raw->state.abort.empty() && abort_.empty() && depth_ > 0) abort_ = raw->state.abort;
        finishTasks();
        return ok;
    }
    if (finished) *finished = false;
    return true;                                       // en pause (ou arrete) : il reprendra
}

// Un script lance hors de tout travail du debogueur (un appel direct) : sa copie (le projet peut changer pendant la
// pause), sur sa tache.
bool Runtime::startDebugTask(const Script& sc, const std::string& source, double now, std::string* error) {
    (void)now;
    auto copy = std::make_shared<const Script>(sc);
    return runDebugTask(sc.name, [this, copy, source](std::string* why) { return runScriptHere(*copy, source, now_, why); }, error, nullptr);
}

bool Runtime::unitWanted() const noexcept { return debugOn_ && running_ && !currentTask_ && !paused_ && debugWanted(); }

bool Runtime::debugUnit(std::string label, std::function<void()> body) {
    bool finished = true;
    (void)runDebugTask(std::move(label), [body = std::move(body)](std::string*) {
        body();
        return true;
    }, nullptr, &finished);
    return finished;
}

// Arretee (la simulation s'arrete pendant une pause), ou abandonnee (son code a change pendant la pause) le temps que ses
// executions en cours se defassent.
bool Runtime::taskStopped() const noexcept {
    return currentTask_ && currentTask_->task && (currentTask_->task->cancelled() || (currentTask_->abandon && execDepth_ > 0));
}

// Une execution (runStatements, callFunction) finit : vrai si elle etait abandonnee (son code a change pendant la pause).
// La plus basse du travail finie, l'abandon s'efface : le reste du travail reprend.
bool Runtime::endExecution() noexcept {
    if (execDepth_ > 0) --execDepth_;
    if (!currentTask_ || !currentTask_->abandon) return false;
    if (execDepth_ == 0) currentTask_->abandon = false;
    return true;
}

// 1.13.0 : seulement si la pause est dans une execution comptee (execDepth_) - sinon l'abandon resterait leve et
// couperait sans un mot la prochaine execution du travail (endExecution ne l'effacerait qu'a sa fin).
void Runtime::abandonTask(TaskState* t) noexcept {
    if (t && t == currentTask_ && execDepth_ > 0) t->abandon = true;
}

// Les relachements arrives pendant la pause, rejoues des que la pause finit (la reprise qui va au bout, sinon le tick
// suivant ou le prochain appui) : la touche relachee (ses actions KeyRelease), le bouton relache - sur l'objet : le clic,
// comme sans debogueur ; dehors : l'impulsion retombe. Une reprise qui se remet en pause garde le reste pour la suivante.
void Runtime::replayDeferredInput(double now) {
    if (deferred_.empty() || paused_ || currentTask_) return;
    std::vector<DeferredInput> list;
    list.swap(deferred_);
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (paused_) {
            deferred_.insert(deferred_.end(), list.begin() + static_cast<std::ptrdiff_t>(i), list.end());
            return;
        }
        if (list[i].key) (void)keyUp(list[i].token, now);
        else release(list[i].object, now, list[i].inside);
    }
}

Runtime::~Runtime() {
    stopTasks();                                       // 1.12.26 : les scripts detaches s'arretent avant le moteur
    unregisterChoiceResolver(this);                    // 1.12.2 : ses sources d'elements partent avec lui
}

void Runtime::bind(const Project* project, sim::Environment* plc) {
    project_ = project;
    plc_ = plc;
}

namespace {
// 1.13.0 (le debogueur) : un crochet appele depuis une tache (un script detache, un travail du debogueur - un tick, un
// geste) tourne sur le fil de l'IHM (ScriptTask::onRootThread) : la tache attend sa reponse, comme s'il etait appele
// la. Un son, la Console, une question, la barre d'etat ne sont jamais touches d'un autre fil.
template <class R, class... A>
void onUiThread(std::function<R(A...)>& hook) {
    if (!hook) return;
    hook = [inner = std::move(hook)](A... args) -> R {
        if (!ScriptTask::onTaskThread()) return inner(std::forward<A>(args)...);
        if constexpr (std::is_void_v<R>) {
            ScriptTask::onRootThread([&] { inner(std::forward<A>(args)...); });
        } else {
            std::optional<R> out;
            ScriptTask::onRootThread([&] { out.emplace(inner(std::forward<A>(args)...)); });
            return std::move(*out);
        }
    };
}
} // namespace

void Runtime::setHooks(Hooks h) {
    // Ce qui agit sur l'ecran ou sur ce que l'interface tient : sur le fil de l'IHM. Les lectures pures (l'etat de
    // l'automate, du poste, des equipements, un forcage commun, le dossier du projet) restent directes : la tache a la
    // main seule, rien ne bouge pendant qu'elle lit.
    onUiThread(h.playSound);
    onUiThread(h.stopSounds);
    onUiThread(h.soundSettings);     // 1.14.0 : le melangeur (les voix qui jouent)
    onUiThread(h.journaled);
    onUiThread(h.askLogin);
    onUiThread(h.recipeRequest);
    onUiThread(h.requestResource);
    onUiThread(h.userRequest);
    onUiThread(h.restart);
    onUiThread(h.exportFile);
    onUiThread(h.audited);
    onUiThread(h.alarmNotice);
    onUiThread(h.reportWritten);
    onUiThread(h.simSlaveCommand);
    onUiThread(h.askExport);
    hooks_ = std::move(h);
}

sim::Environment& Runtime::environment() { return *env_; }
sim::Environment& Runtime::envOf() const { return *env_; }
// 1.14.0 (M4) : les variables IHM, case par case (un enregistrement les prend toutes).
void Runtime::forEachIhmValue(const std::function<void(const std::string& key, const sim::Value&)>& f) const {
    for (const auto& [key, value] : env_->vars) f(key, value);
}
std::string Runtime::resolvedPath(std::string_view path, bool concrete) {
    return concrete ? env_->concretePath(env_->resolved(path)) : env_->resolved(path);
}

const sim::Value* Runtime::variable(std::string_view name) const {
    const auto it = env_->vars.find(upper(name));
    return it == env_->vars.end() ? nullptr : &it->second;
}

sim::ObjRef Runtime::richVariable(std::string_view name) const {
    const auto it = rich_.find(upper(name));
    return it == rich_.end() ? nullptr : it->second;
}

std::string Runtime::richText(std::string_view name) const {
    const auto o = richVariable(name);
    if (!o) return {};
    std::string out;
    return sim::literalText(*o, out) ? out : sim::display(*o);
}

bool Runtime::setRichVariable(std::string_view name, std::string_view literal, std::string* why) {
    const auto it = rich_.find(upper(name));
    if (it == rich_.end() || !it->second || !it->second->type) {
        if (why) *why = std::string(name) + " n'est pas une variable objet (LIST, VECTOR, MAP, TUPLE)";
        return false;
    }
    std::string w;
    const std::string type = it->second->type->text();
    const std::string form = types::callbackForm(project_, type);       // 1.12.20 : un rappel nomme (T_Filtre)
    // 1.12.20 : une fonction du projet donnee a un rappel : sa forme d'abord (le moteur ne la voit qu'a l'appel) - la
    // meme regle que Generer (params::callbackArgument).
    if (!form.empty() && project_) {
        std::string name = trimmedView(literal);
        if (name.size() > 1 && name.front() == '=' && name[1] != '=') name = trimmedView(std::string_view(name).substr(1));
        if (name.size() >= 2 && name.front() == '\'' && name.back() == '\'') name = name.substr(1, name.size() - 2);
        const std::string u = upper(name);
        if (!name.empty() && u != "NULL" && u.rfind("LAMBDA", 0) != 0)
            if (const auto fs = project_->functionsNamed(name); fs.size() == 1) {
                const std::string got = overload::callbackText(overload::signatureOf(*fs.front()));
                std::string why2;
                if (got.empty() || !typeform::callbackFitsText(*project_, form, got, &why2)) {
                    if (why) *why = name + " ne convient pas \xC3\xA0 " + form
                                    + (why2.empty() ? std::string(" (ses VAR_IN_OUT ou VAR_OUTPUT)") : " \xE2\x80\x94 " + why2);
                    return false;
                }
            }
    }
    auto fresh = sim::makeRichValue(type, typeform::callbackInitial(form.empty() ? type : form, literal), *env_, &w);   // 1.12.19 : un rappel, =Valider
    if (!fresh) {
        if (why) *why = frenchSimMessage(w);
        return false;
    }
    // En place : les references et les scripts qui la tiennent voient la nouvelle valeur.
    if (!sim::assignObj(*it->second, *fresh, &w)) {
        if (why) *why = frenchSimMessage(w);
        return false;
    }
    return true;
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

bool Runtime::unforceVariable(std::string_view name) {
    forcedWarned_.erase(upper(name));      // 1.12.3 : une nouvelle ecriture ignoree se redira
    return forcedIhm_.erase(upper(name)) > 0;
}

void Runtime::unforceAllVariables() {
    forcedIhm_.clear();
    forcedWarned_.clear();
}

void Runtime::forcedWriteIgnored(const std::string& key, const std::string& shown, const sim::Value& v) {
    lastWriteForced_ = true;
    if (!forcedWarned_.insert(key).second) return;
    logAt(LogLevel::Warning, "Simulation", "IHM",
          shown + " est forc\xC3\xA9" "e : \xC3\xA9" "criture ignor\xC3\xA9" "e (" + (v.type() == sim::Type::String ? "'" + v.asString() + "'" : v.display())
              + ") - D\xC3\xA9" "faire le for\xC3\xA7" "age pour qu'elle reprenne");
}

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
    // 1.12.19 : une variable rappel garde le nom de sa fonction (NULL : vide) ; une LAMBDA ne se garde pas - elle
    // repart de sa valeur initiale (la maquette, decision 9).
    for (const auto& var : project_->programs.variables) {
        if (!var.equipment.empty() || (keep && !keep(var)) || types::callbackForm(project_, var.type).empty()) continue;   // 1.12.20 : T_Filtre aussi
        const auto o = richVariable(var.name);
        std::string text;
        if (o && sim::literalText(*o, text)) cells.push_back(simdata::Cell{var.id, var.name, var.type, {}, sim::Value::text(text)});
    }
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
    // 1.12.7 (lot E) : un joker [*] a deplier, une instance a repeter (une fois par tick : la question se
    // pose a chaque lecture de la vue).
    auto jk = jokerViews_.find(id);
    if (jk == jokerViews_.end()) jk = jokerViews_.emplace(id, sources::viewHasJoker(*v)).first;
    const bool jokers = jk->second;
    // 1.12.11 (lot G) : une liaison Equipement a remplir (ses roles, lus dans la structure).
    auto lk = linkViews_.find(id);
    if (lk == linkViews_.end()) lk = linkViews_.emplace(id, eqlink::viewHasLinks(*v)).first;
    const bool links = lk->second;
    // 1.12.10 (lot F) : la plage et les seuils pris a la variable, un nom tape sans = (une fois par tick).
    auto rg = rangeViews_.find(id);
    if (rg == rangeViews_.end()) rg = rangeViews_.emplace(id, ranges::needed(*project_, *v)).first;
    const bool ranged = rg->second;
    if (!inherited && !symbols && !owned && !overridden(*v) && !jokers && !links && !ranged) return v;
    // Une entree de std::map ne bouge pas quand on en ajoute d'autres : le
    // pointeur rendu vaut jusqu'au prochain effacement (entree du moteur).
    auto it = composed_.find(id);
    if (it == composed_.end()) {
        View c = inherited ? compose(*project_, *v) : *v;
        applyOverrides(c, *v);
        // 1.12.7 (lot E) : « Repeter depuis » - une instance par element, avant de developper les symboles.
        const sources::SourceFn source = [this, id](std::string_view name) { return jokerSource(name, id); };
        if (jokers) (void)sources::repeatInstances(c, source);
        if (symbols) {
            View e = expandInstances(*project_, c);
            // 1.12.3 : les objets d'une instance n'existent qu'une fois developpes - leurs
            // proprietes ecrites en marche (l'onglet choisi, le panneau replie, le
            // defilement) se posent apres. Avant : ces clics dans un symbole ne prenaient pas.
            if (!overrides_.empty()) applyOverrides(e, *v, &c);
            c = std::move(e);
        }
        if (owned) c = qualifiedOwnedPopup(*project_, c);        // 1.11.10 : Ouvrir() vise l'instance qui l'ouvre
        // 1.12.11 (lot G) : la liaison Equipement - les roles libres d'un symbole lus dans sa structure (P1.Marche,
        // P1.Defaut, {P1.Nom}), apres les instances (leurs objets ont recu leurs arguments).
        if (links || symbols) (void)eqlink::applyView(*project_, c);
        // 1.12.7 (lot E) : le joker [*] des listes (une plume, une barre, une ligne, une vue, un onglet par element).
        if (jokers || symbols) sources::expandView(c, source, [this, id](std::string_view f) { return jokerText(f, id); });
        // 1.12.10 (lot F) : une borne, un seuil laisses vides prennent ceux de la variable ; un nom tape sans =
        // est la variable (apres les jokers et les symboles : leurs objets aussi).
        (void)ranges::apply(c, *project_);
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
    if (logMute_ > 0) return;           // 1.14.0 (relecture M4b) : une relecture qui refait ses courbes (une remise a zero XY)
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
    e.declaration = origin_.declaration;
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
    // 1.12.3 : SYS.ErrorCount compte toutes les erreurs de la marche, meme celles que la memoire a oubliees.
    if (e.kind == "Erreur") {
        ++errorCount_;
        lastError_ = e.message;
    }
    if (recordOn_) recordEvent(rec::EventKind::Journal, e.kind, e.source.empty() ? e.message : e.source + " : " + e.message);   // 1.14.0 (M4)
    journal_.push_back(std::move(e));
    while (journal_.size() > journalCap()) journal_.pop_front();
}

// 1.12.3 : les lignes gardees en memoire (journal, evenements, alarmes closes) : le reglage des
// Historiques (Lignes gardees, 2 000 par defaut), 100 au moins.
std::size_t Runtime::journalCap() const {
    return project_ ? static_cast<std::size_t>(std::max(100, project_->history.maxEntries)) : kJournalMax;
}

void Runtime::event(std::string kind, std::string source, std::string message) {
    HistoryEvent e{dateStampOf(now_), kind, source, message, user_};
    if (history_ && project_ && project_->history.events) {
        history_->events.push_back(e);
        if (history_->events.size() > static_cast<std::size_t>(std::max(10, project_->history.maxEntries)) + 64)
            history_->trim(project_->history);
    }
    events_.push_back(std::move(e));
    while (events_.size() > journalCap()) events_.pop_front();
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
    return v ? exactValue(*v) : std::string{};   // 1.12.5 (D4) : sans perte (avant : 6 chiffres)
}

std::optional<std::string> Runtime::evaluatedProp(Id view, Kind kind, const Prop& p) const {
    if (p.expr.empty() || !env_) return std::nullopt;
    auto it = expressions_.find(p.expr);
    if (it == expressions_.end()) it = expressions_.emplace(p.expr, Expression::compile(p.expr)).first;
    if (!it->second.valid()) return std::nullopt;
    const Scope* scope = viewScope(view);
    auto v = it->second.evaluate(*env_, scope);
    if (!v) return std::nullopt;
    if (auto word = natives::propertyWord(kindKey(kind), p.key, *v)) return *word;   // comme LiveView
    if (!roles::keyRole(p.key)) return exactValue(*v);
    // 1.12.6 (lot B) : le lecteur du role, comme LiveView (une enumeration : son texte dans un texte).
    const auto role = roles::roleOf(kind, p.key);
    roles::Shape shape;
    const auto r = role.role == roles::Role::Formula ? role.item : role.role;
    if (r == roles::Role::Text || r == roles::Role::State) shape = it->second.shapeOf(*env_, scope, &*v);
    auto read = roles::adapt(role, *v, shape);
    if (!read.ok) return std::nullopt;
    return std::move(read.text);
}

bool Runtime::sampleNumber(const std::string& expr, double& out) const {
    if (!env_) return false;
    auto it = expressions_.find(expr);
    if (it == expressions_.end()) it = expressions_.emplace(expr, Expression::compile(expr)).first;
    if (!it->second.valid()) return false;
    auto v = it->second.evaluate(*env_);
    if (!v) return false;
    const auto read = roles::adapt(roles::KeyRole{"variables", roles::Role::Number}, *v);
    return read.ok && roles::readNumber(read.text, out);
}

// 1.12.7 (lot E) : le texte d'une formule (le nom d'un element d'une source : Cuves[2].Nom).
std::optional<std::string> Runtime::jokerText(std::string_view formula, Id view) const {
    if (!env_) return std::nullopt;
    const std::string f(formula);
    auto it = expressions_.find(f);
    if (it == expressions_.end()) it = expressions_.emplace(f, Expression::compile(f)).first;
    if (!it->second.valid()) return std::nullopt;
    auto v = it->second.evaluate(*env_, viewScope(view));
    if (!v) return std::nullopt;
    return v->type() == sim::Type::String ? v->asString() : roles::displayText(*v);
}

void Runtime::evaluateProps(Id view, Object& o) const {
    if (!env_) return;
    for (auto& p : o.props)
        if (!p.expr.empty())
            if (auto text = evaluatedProp(view, o.kind, p)) p.value = std::move(*text);
    roles::plainProps(o);   // 1.12.6 (lot C) : ses noms, ses listes, ses etats sans les $ de leurs reperes
}

std::string Runtime::shownText(Id view, const Object& o, std::string_view key, std::string_view fallback) const {
    const Prop* p = o.find(key);
    if (!p) return std::string(fallback);
    if (auto text = evaluatedProp(view, o.kind, *p)) return std::move(*text);
    return p->value;
}

double Runtime::shownNumber(Id view, const Object& o, std::string_view key, double fallback) const {
    double v = 0;
    return roles::readNumber(shownText(view, o, key), v) ? v : fallback;
}

bool Runtime::objectCondition(Id view, const Object& o, std::string_view key, bool fallback) const {
    const std::string src = formulaOf(o, key);
    if (src.empty() || !env_) return fallback;
    auto it = expressions_.find(src);
    if (it == expressions_.end()) it = expressions_.emplace(src, Expression::compile(src)).first;
    if (!it->second.valid()) return fallback;
    auto v = it->second.evaluate(*env_, viewScope(view));
    return v ? v->isTruthy() : fallback;
}

bool Runtime::shownFlag(Id view, const Object& o, std::string_view key, bool fallback) const {
    return roles::readBool(shownText(view, o, key), fallback);   // 1.12.6 : le lecteur du role (2, ON, 'TRUE')
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

std::string Runtime::concretePath(std::string_view path) {
    if (path.find('[') == std::string_view::npos) return std::string(path);
    std::string out;
    std::size_t i = 0;
    while (i < path.size()) {
        if (path[i] != '[') { out += path[i++]; continue; }
        int depth = 0;
        std::size_t j = i;
        for (; j < path.size(); ++j) {
            if (path[j] == '[') ++depth;
            else if (path[j] == ']' && --depth == 0) break;
        }
        if (j >= path.size()) { out.append(path.substr(i)); break; }
        const std::string_view inner = path.substr(i + 1, j - i - 1);
        out += '[';
        std::size_t from = 0;
        int d = 0;
        for (std::size_t k = 0; k <= inner.size(); ++k) {
            const bool end = k == inner.size();
            if (!end && (inner[k] == '[' || inner[k] == '(')) ++d;
            else if (!end && (inner[k] == ']' || inner[k] == ')')) --d;
            if (!end && !(inner[k] == ',' && d == 0)) continue;
            std::string e(inner.substr(from, k - from));
            while (!e.empty() && e.front() == ' ') e.erase(e.begin());
            while (!e.empty() && e.back() == ' ') e.pop_back();
            bool ok = false;
            const std::string v = evalText(concretePath(e), &ok);
            double x = 0;
            out += ok && parseNumber(v, x) && x == std::floor(x) ? formatNumber(x) : e;
            if (!end) out += ',';
            from = k + 1;
        }
        out += ']';
        i = j + 1;
    }
    return out;
}

std::string Runtime::writtenAs(std::string_view path, std::string_view concrete) const {
    return path == concrete ? std::string{} : " (" + std::string(path) + ")";
}

bool Runtime::write(const std::string& name, const sim::Value& v, const std::string& source) {
    publicWhy_.clear();
    lastWriteForced_ = false;
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

// 1.13.0 (seconde passe) : `type` est-il un generique de `f` (T, sans plus) ?
bool genericType(const HmiFunction& f, std::string_view type) {
    const std::string t = upper(trimmedView(type));
    for (const auto& g : f.generics)
        if (upper(g.name) == t) return true;
    return false;
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

const Runtime::Prepared& Runtime::prepare(const std::string& code, const std::string& source, bool function,
                                          const std::vector<GenericParam>* generics) {
    std::string key = function ? "\x01" + code : code;           // VAR_INPUT n'est permis qu'a une fonction
    if (generics && !generics->empty()) key += "\x02" + genericsText(*generics);   // 1.13.0 : T, un type de CE code
    if (const auto it = programs_.find(key); it != programs_.end()) return it->second;
    Prepared prep;
    // 1.11.1 (REP) : le code se lit sans les $ de ses reperes, comme une expression (les
    // chaines ST et les commentaires gardent les leurs ; les lignes ne bougent pas). La cle
    // du cache reste le texte enregistre.
    // 1.13.0 (lot J8) : les directives USING ne vont jamais au moteur (blanchies : les lignes restent).
    const std::string st = names::blankUsings(markers::strip(code));
    // 1.10 : une locale peut etre d'un type IHM du projet (structure).
    auto parts = splitDeclarations(st, function, [this, generics](std::string_view type) {
        if (project_ && types::knownTypeName(*project_, type)) return true;   // 1.12.20 : un rappel nomme aussi
        // 1.13.0 (seconde passe) : un generique de la fonction (T, LIST OF T) - son exemple dans la contrainte decide,
        // comme au controle (checkFunction) ; la valeur recue garde son type (T vaut ce qu'on lui passe).
        if (!generics || generics->empty()) return false;
        std::vector<std::string> samples;
        for (const auto& g : *generics) samples.push_back(genericSample(g.constraint));
        const std::string concrete = substituteGenerics(type, *generics, samples);
        return concrete != std::string(type) && (localTypeSupported(concrete) || richLocalType(concrete));
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
    if (taskStopped()) return false;             // 1.13.0 : la simulation s'arrete pendant une pause : plus rien ne part
    const Prepared& prep = prepare(code, source, false);
    const auto program = prep.program;           // 1.12.26 : une tache garde son programme pendant qu'elle attend
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
    // 1.13.0 (seconde passe) : les unites de ses declarations (la colonne Unite de ses Variables, de ses Constantes).
    const Script* declared = scriptId && project_ ? project_->script(scriptId) : nullptr;
    const Env::CodeUnitsGuard declaredUnits(*env_, Env::CodeUnits{&frame, declared && !declared->decls.empty() ? &declared->decls : nullptr, {}, {}, {}});
    sim::Locals* kept = nullptr;
    if (!prep.locals.empty()) kept = &scriptLocals_[scriptId ? "s" + std::to_string(scriptId) : "c" + code];
    // 1.11.18 (lot 5) : la premiere execution depuis le demarrage - ses declarations Persistantes.
    if (kept && scriptId && kept->all().empty() && !persistPending_.empty()) restorePersistent(scriptId, *kept);
    env_->diagnostics.clear();
    sim::RunLimits limits;
    limits.maxIterationsPerLoop = 100000;
    limits.maxStatementsPerScan = 200000;
    taskLimits(limits);                          // 1.12.26 : dans un script detache, des tranches sans plafond
    limits.locals = kept;
    // 1.12.7 : ses valeurs calculees au demarrage (ses constantes, les valeurs initiales calculees).
    if (scriptId)
        if (const auto it = initials_.find(scriptId); it != initials_.end()) limits.initials = &it->second;
    // 1.11.14 : la ligne en cours (IHM_LOG la dit) ; celle de l'appelant revient apres.
    const sim::ExecTrace callerTrace = trace_;
    trace_ = {};
    limits.trace = &trace_;
    // 1.13.0 : le debogueur - le numero de ce code, ses lignes marquees, le pas a pas.
    prepareDebug(*program, scriptId, kNoId, limits);
    debugExec_.push_back(DebugExec{sim::programTag(*program), nullptr, nullptr});
    ++execDepth_;
    // 1.13.0 : un script lance par un autre (IHM_APPELER) : les erreurs de ses fonctions se disent ou se rattrapent ici
    // (son TRY), pas d'apres le TRY de celui qui l'a lance (callerCatches).
    const sim::CallBoundary boundary;
    const auto result = sim::execute(*program, *env_, limits);
    const bool abandoned = endExecution();
    if (!debugExec_.empty()) debugExec_.pop_back();
    trace_ = callerTrace;
    env_->frames.pop_back();
    source_ = previous;
    if (scriptId) ++runs_[scriptId];
    // 1.12.26 : un script detache arrete (la simulation s'arrete, IHM_SCRIPT_ARRETER) : ni erreur, ni message. 1.13.0 : de
    // meme un code change pendant la pause (abandonne) - la suite du travail, elle, continue.
    if (abandoned || (currentTask_ && currentTask_->task->cancelled())) {
        if (depth_ <= 1) abort_.clear();
        return false;
    }
    std::string why;
    int whyLine = -1;                // -1 : pas de ligne (un budget depasse)
    for (const auto& d : env_->diagnostics)
        if (d.severity == sim::Diagnostic::Severity::Error) {
            why = (d.line ? "ligne " + std::to_string(d.line) + " : " : std::string{}) + shownCodeNames(project_, frenchSimMessage(d.message));
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

// 1.12.19 : LES MINUTEURS D'IHM_APRES ET IHM_CHAQUE. Un rappel garde (sa fonction, ou sa LAMBDA et ce qu'elle a
// copie), appele au premier tick apres son heure ; periodique : il repart de son heure (sans rattraper les
// periodes manquees). Au plus 256 en attente : au-dela, une boucle qui en lance a chaque cycle.
bool Runtime::scheduleCallback(std::int64_t delayMs, std::int64_t periodMs, sim::ObjRef callback, std::int64_t& id, std::string* why) {
    if (!running_) {
        if (why) *why = "l'IHM ne tourne pas : rien ne partirait";
        return false;
    }
    if (timers_.size() + watches_.size() >= 256) {
        if (why) *why = "256 minuteurs attendent d\xC3\xA9j\xC3\xA0 (une boucle en lance un \xC3\xA0 chaque cycle ?) ; IHM_ANNULER les arr\xC3\xAAte";
        return false;
    }
    CallbackTimer t;
    t.id = id = ++timerSeq_;
    t.due = now_ + static_cast<double>(delayMs) / 1000.0;
    t.period = static_cast<double>(periodMs) / 1000.0;
    t.callback = std::move(callback);
    t.source = source_;
    timers_.push_back(std::move(t));
    return true;
}

bool Runtime::cancelCallback(std::int64_t id) {
    const auto it = std::find_if(timers_.begin(), timers_.end(), [&](const CallbackTimer& t) { return t.id == id; });
    if (it != timers_.end()) {
        timers_.erase(it);
        return true;
    }
    const auto w = std::find_if(watches_.begin(), watches_.end(), [&](const CallbackWatch& x) { return x.id == id; });   // 1.12.20
    if (w == watches_.end()) return false;
    watches_.erase(w);
    return true;
}

// 1.12.20 : IHM_SUR_CHANGEMENT. Le chemin (ses indices figes a l'inscription) est relu a chaque tour, apres les
// minuteurs ; une valeur differente de la derniere appelle le rappel (une fois par changement vu, pas a l'inscription).
// Un chemin qui ne se lit plus (une variable retiree) : la Console le dit et la surveillance s'arrete.
bool Runtime::watchCallback(const std::string& path, const sim::Value& now, sim::ObjRef callback, bool passValue, std::int64_t& id,
                            std::string* why) {
    if (!running_) {
        if (why) *why = "l'IHM ne tourne pas : rien ne serait surveill\xC3\xA9";
        return false;
    }
    if (timers_.size() + watches_.size() >= 256) {
        if (why) *why = "256 minuteurs et surveillances tournent d\xC3\xA9j\xC3\xA0 (une boucle en lance une \xC3\xA0 chaque cycle ?) ; IHM_ANNULER les arr\xC3\xAAte";
        return false;
    }
    auto parsed = sim::parseExpression(path);
    if (!parsed) {
        if (why) *why = path + " ne se relit pas (" + frenchSimMessage(parsed.error().message()) + ")";
        return false;
    }
    CallbackWatch w;
    w.id = id = ++timerSeq_;
    w.path = path;
    w.expr = *parsed;
    w.last = now;
    w.callback = std::move(callback);
    w.passValue = passValue;
    w.source = source_;
    watches_.push_back(std::move(w));
    return true;
}

void Runtime::watchesTick() {
    std::vector<std::int64_t> ids;
    for (const auto& w : watches_) ids.push_back(w.id);
    for (const std::int64_t k : ids) {
        const auto it = std::find_if(watches_.begin(), watches_.end(), [&](const CallbackWatch& w) { return w.id == k; });
        if (it == watches_.end() || !running_) continue;      // annulee par un rappel d'avant
        Env::Frame frame;
        env_->frames.push_back(&frame);
        const auto v = sim::evaluate(*it->expr, *env_);
        env_->frames.pop_back();
        const std::string source = "surveillance " + std::to_string(k) + " de " + it->path + (it->source.empty() ? std::string{} : " (" + it->source + ")");
        if (!v) {
            logAt(LogLevel::Error, "Erreur", source,
                  "IHM_SUR_CHANGEMENT : " + it->path + " ne se lit plus (" + frenchSimMessage(v.error().message()) + ") : la surveillance s'arr\xC3\xAAte");
            watches_.erase(it);
            continue;
        }
        if (v->compare(it->last) == 0 && (v->type() == sim::Type::String) == (it->last.type() == sim::Type::String)) continue;
        it->last = *v;
        const sim::ObjRef callback = it->callback;            // le rappel peut en lancer ou en annuler d'autres
        std::vector<sim::Value> args;
        if (it->passValue) args.push_back(*v);
        if (callback) fireCallback(callback, args, source);
    }
}

void Runtime::timersTick(double now) {
    // Ceux dont l'heure est venue, dans l'ordre de leur heure ; un rappel peut en lancer d'autres (pour plus tard)
    // ou en annuler (un annule avant son tour ne part pas).
    std::vector<std::int64_t> due;
    for (const auto& t : timers_)
        if (t.due <= now + 1e-9) due.push_back(t.id);
    std::stable_sort(due.begin(), due.end(), [&](std::int64_t a, std::int64_t b) {
        const auto at = [&](std::int64_t k) {
            for (const auto& t : timers_) if (t.id == k) return t.due;
            return now;
        };
        return at(a) < at(b);
    });
    for (const std::int64_t k : due) {
        const auto it = std::find_if(timers_.begin(), timers_.end(), [&](const CallbackTimer& t) { return t.id == k; });
        if (it == timers_.end() || !running_) continue;
        const sim::ObjRef callback = it->callback;
        const std::string source = "minuteur " + std::to_string(k) + (it->source.empty() ? std::string{} : " (" + it->source + ")");
        if (it->period > 0) it->due = std::max(it->due + it->period, now);
        else timers_.erase(it);
        if (callback) fireCallback(callback, {}, source);
    }
}

void Runtime::fireCallback(const sim::ObjRef& callback, const std::vector<sim::Value>& args, const std::string& source) {
    const std::string previous = source_;
    source_ = source;
    Env::Frame frame;
    env_->frames.push_back(&frame);
    env_->diagnostics.clear();
    sim::RunLimits limits;
    limits.maxIterationsPerLoop = 100000;
    limits.maxStatementsPerScan = 200000;
    taskLimits(limits);                                       // 1.12.26
    if (debugOn_) limits.stepAll = &stepAll_;                 // 1.13.0 : le pas a pas suit le rappel
    const sim::ExecTrace callerTrace = trace_;
    trace_ = {};
    limits.trace = &trace_;
    ++depth_;
    ++callbackDepth_;                                         // 1.13.0 : un rappel ne s'arrete pas (debugReached le dit)
    const auto r = sim::callCallback(*callback, *env_, args, source, limits);
    --callbackDepth_;
    --depth_;
    trace_ = callerTrace;
    env_->frames.pop_back();
    source_ = previous;
    std::string why = !r ? frenchSimMessage(r.error().message()) : std::string{};
    if (!abort_.empty()) {
        why = abort_;
        abort_.clear();
    }
    if (!why.empty()) logAt(LogLevel::Error, "Erreur", source, why);
}

// 1.12.19 : un rappel vide (NULL) appele : rien n'est fait, la valeur par defaut est rendue ; la Console le dit une
// fois par endroit (le code et sa ligne), pas a chaque cycle.
void Runtime::nullCallback(std::string_view name, std::string_view where) {
    const std::string key = std::string(name) + "@" + std::string(where);
    if (!nullSaid_.insert(key).second) return;
    logAt(LogLevel::Warning, "Script", source_,
          std::string(name) + " est vide (NULL) : rien n'est appel\xC3\xA9, sa valeur par d\xC3\xA9" "faut est rendue (" + std::string(where) + ")");
}

// 1.11.10 : la fonction d'une instance de symbole, prete a tourner (HmiSymbols.hpp).
std::shared_ptr<const HmiFunction> Runtime::symbolCall(std::string_view call) {
    if (!project_) return nullptr;
    const std::string key = upper(call);
    if (const auto it = boundCalls_.find(key); it != boundCalls_.end()) return it->second;
    std::shared_ptr<const HmiFunction> fn;
    if (BoundFunction b; boundSymbolFunction(*project_, call, b)) {
        // 1.13.0 (le debogueur) : une redefinition d'instance n'a pas de fonction d'origine (son code est a elle) - le
        // debogueur la connait par l'id de sa copie : son nom, son code (la pile, le volet, un point d'arret).
        if (b.overridden && debugOn_) {
            std::string shown = b.function.name;
            if (const auto dot = std::string_view(call).rfind('.'); dot != std::string_view::npos) shown = std::string(call.substr(dot + 1));
            if (const auto hash = shown.find('#'); hash != std::string::npos) shown.resize(hash);
            boundCodes_[b.function.id] = BoundCode{"red\xC3\xA9" "finition " + b.instance + "." + shown, functionText(b.function), b.function.body};
        }
        fn = std::make_shared<const HmiFunction>(std::move(b.function));
    }
    boundCalls_[key] = fn;
    return fn;
}

bool Runtime::callFunction(const HmiFunction& f, const std::vector<std::pair<std::string, sim::Value>>& args,
                           sim::Value& result, std::vector<std::pair<std::string, sim::Value>>* outputs) {
    if (taskStopped()) return false;             // 1.13.0 : la simulation s'arrete pendant une pause : plus rien ne part
    // Lot 8 : une fonction ne voit pas les parametres de la vue qui l'appelle.
    AliasGuard aliasGuard(env_->aliases, nullptr);
    const std::string source = "fonction " + fullName(f);       // 1.13.0 (lot J8) : Util.Moyenne
    // 1.13.0 (le debogueur) : ce qui sert apres son execution, copie avant - elle peut s'arreter au milieu, et sa
    // fonction (une copie liee du cache, une fonction du projet) changer de place pendant la pause. Une copie liee
    // marque les points de la fonction qu'elle copie (debugOrigin).
    const Id fnId = f.id;
    const std::string fnName = f.name;
    const std::string fnReturn = f.returnType;
    const Id debugId = f.debugOrigin != kNoId ? f.debugOrigin : f.id;
    const sim::Type rt = typereg::simTypeOf(fnReturn);
    result = fnReturn.empty() ? sim::Value::boolean(true) : sim::Value::defaultOf(rt);
    // Une faute dans une fonction : l'appel echoue (l'instruction qui
    // l'appelle s'arrete), et tout l'enchainement avec lui - c'est la cause
    // premiere (abort_) qui remonte, dite une fois par celui qui la trouve.
    // Depuis une expression de vue (evaluee a chaque image), elle ne se dit
    // qu'une fois pour toutes, et l'echec s'arrete a l'expression.
    int failLine = 0;                // 1.11.14 : la ligne de la faute dans la fonction (0 : celle de l'appel)
    // 1.13.0 : l'erreur sera rattrapee par l'appelant (un TRY, ??) : elle ne se dit pas a la Console et ne coupe pas
    // l'enchainement (abort_) - elle lui revient telle quelle (lastCallRaised).
    bool caughtAbove = false;
    const auto say = [&](const std::string& message) {
        if (debugProbing_ > 0) return;           // 1.13.0 : un espion, une condition de point - l'echec se dit la, pas ici
        if (readOnly_ > 0 && !functionErrors_.insert(fnName + "|" + message).second) return;
        logAt(LogLevel::Error, "Erreur", source, message, failLine);
    };
    const auto fail = [&](const std::string& why) {
        if (abort_.empty() && !caughtAbove) {
            say(why);
            abort_ = source + " : " + why;
        }
        if (depth_ == 0) abort_.clear();
        return false;
    };
    if (depth_ >= kMaxDepth) return fail("appel circulaire (plus de " + std::to_string(kMaxDepth) + " niveaux)");
    // 1.13.0 (lot J8) : une fonction d'une bibliotheque qui n'est pas chargee (introuvable, incompatible) : le projet
    // garde son interface (pour verifier), pas son code - l'appel echoue en le disant.
    if (const LoadedLibrary* lib = project_ ? project_->libraryOf(f.id) : nullptr; lib && lib->fromCache)
        return fail("la biblioth\xC3\xA8que " + lib->name + " n'est pas charg\xC3\xA9" "e (" + lib->message + ") : " + fullName(f)
                    + " n'a pas de code ici");
    std::string composed;                                            // 1.11.18 (lot 3) : ses declarations du modele
    const std::string& code = decl::codeOf(f, composed);
    // 1.13.0 (lot J8) : une fonction d'un espace, ou qui importe des espaces (USING) : ses noms courts ecrits en
    // entier avant le moteur (Borne -> Util.Borne). Sans espace : le code tel quel.
    const bool spaced = project_ && (!f.ns.empty() || names::mentionsUsing(f.body));
    const Prepared& prep = prepare(spaced ? names::qualify(*project_, code, f.ns, {f.name}, true) : code, source, true, &f.generics);
    if (!prep.program) return fail(prep.error);
    Env::Frame frame;
    // Les parametres : dans l'ordre (Moyenne(1, 2)) ou par leur nom (Moyenne(b := 2, a := 1)).
    // 1.11.20 : TOUS - VAR_INPUT, VAR_IN_OUT, VAR_OUTPUT - dans l'ordre de leur declaration (avant,
    // les seules entrees : Random(Min, Max, Graine, Tirage) disait « trop d'arguments »). Ici un
    // argument est une VALEUR (une expression de vue, l'essai d'une fonction) : une E/S part de la
    // valeur donnee, une sortie de sa valeur par defaut, et leur valeur finale revient a `outputs`.
    // Depuis un script, l'appel passe par le dialecte : E/S et sorties y ecrivent dans la variable
    // de l'appelant.
    std::vector<const LocalVar*> inputs;
    for (const auto& l : prep.locals) if (l.isParameter()) inputs.push_back(&l);
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
        if (inputs[k]->section == LocalVar::Section::Output) continue;      // une sortie : sa valeur par defaut
        sim::Value v = sim::Value::defaultOf(typereg::simTypeOf(inputs[k]->type));
        v.assignFrom(value);
        // 1.13.0 (seconde passe) : un parametre generique (T) prend la valeur recue telle quelle - T vaut ce qu'on lui
        // passe (un cadre de type inconnu est un INT : 1.5 y devenait 2).
        if (genericType(f, inputs[k]->type)) v = value;
        frame[upper(inputs[k]->name)] = v;
        given[k] = true;
    }
    env_->frames.push_back(&frame);
    // 1.13.0 (seconde passe) : les unites de ses parametres et locales (la colonne Unite), et celle de son retour.
    const Env::CodeUnitsGuard declaredUnits(*env_, Env::CodeUnits{&frame, &f.decls, f.name, f.returnType, f.returnUnit});
    std::string initWhy;
    for (std::size_t j = 0; j < inputs.size(); ++j)
        if (!given[j]) frame[upper(inputs[j]->name)] = localInitial(*inputs[j], *env_, &initWhy);
    // Une fonction n'a pas de memoire : ses VAR et VAR_TEMP repartent a chaque appel.
    for (const auto& l : prep.locals)
        if (!l.isParameter()) frame[upper(l.name)] = localInitial(l, *env_, &initWhy);
    if (!fnReturn.empty()) frame[upper(fnName)] = genericType(f, fnReturn) ? sim::Value{} : sim::Value::defaultOf(rt);   // T : ce qu'on y range
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
    origin_.function = debugId;              // 1.13.0 : une copie liee - la fonction qu'elle copie (la Console y mene)
    origin_.script = kNoId;
    const sim::ExecTrace callerTrace = trace_;
    trace_ = {};
    ++depth_;
    sim::RunLimits limits;
    limits.maxIterationsPerLoop = 100000;
    limits.maxStatementsPerScan = 200000;
    taskLimits(limits);                                       // 1.12.26 : appelee par un script detache
    limits.trace = &trace_;
    if (!fnReturn.empty()) limits.returnTo = fnName;          // 1.12.16 : RETURN valeur ;
    // 1.13.0 : le debogueur - le numero de ce code, ses lignes marquees, ses locales (le cadre du moteur).
    prepareDebug(*prep.program, kNoId, debugId, limits);
    debugExec_.push_back(DebugExec{sim::programTag(*prep.program), &frame, &prep.locals});
    ++execDepth_;
    const auto run = sim::execute(*prep.program, *env_, limits);
    const bool abandoned = endExecution();
    if (!debugExec_.empty()) debugExec_.pop_back();
    --depth_;
    trace_ = callerTrace;
    source_ = previous;
    env_->frames.pop_back();
    auto mine = std::move(env_->diagnostics);
    env_->diagnostics = std::move(saved);
    ++runs_[fnId];
    // 1.13.0 : arretee pendant une pause (la simulation s'arrete), ou son code change pendant la pause (abandonnee) : sans
    // erreur, l'appelant s'arrete aussi.
    if (abandoned || (currentTask_ && currentTask_->task->cancelled())) return false;
    std::string why = initWhy;
    failLine = -1;
    env_->lastRaised.reset();                                  // 1.13.0 : rien d'une fonction qu'elle a appelee
    for (const auto& d : mine)
        if (d.severity == sim::Diagnostic::Severity::Error) {
            why = (d.line ? "ligne " + std::to_string(d.line) + " : " : std::string{}) + shownCodeNames(project_, frenchSimMessage(d.message));
            if (d.line) failLine = static_cast<int>(d.line);
            env_->lastCallRaw = d.message;                     // 1.13.0 : son code E_* pour le CATCH de l'appelant
            // 1.13.0 : ce que le TRY de l'appelant recoit TEL QUEL - une erreur levee (son code, son message) ou du moteur
            // (son code E_*, son message francais), nee ici a sa ligne ou plus bas (son origine). Sauf une boucle sans
            // fin, un cycle trop long : ceux-la ne se rattrapent pas (l'appel echoue comme avant).
            const std::string& raw = d.raw.empty() ? d.message : d.raw;
            if (raw.find(" loop ran more than ") == std::string::npos && raw.find("the scan exceeded") == std::string::npos) {
                Env::RaisedCall r;
                r.code = d.code.empty() ? env_->caughtCode(raw) : d.code;
                r.message = d.code.empty() ? frenchSimMessage(raw) : d.raw;
                r.origin = !d.origin.empty() ? shownCodeNames(project_, d.origin) : source + (d.line ? ", ligne " + std::to_string(d.line) : std::string{});
                env_->lastRaised = std::move(r);
                caughtAbove = sim::callerCatches();
            }
            break;
        }
    if (why.empty() && !run.completed) why = "arr\xC3\xAAt\xC3\xA9" " : budget d'instructions d\xC3\xA9pass\xC3\xA9 (boucle sans fin ?)";
    if (!why.empty()) return fail(why);
    if (!fnReturn.empty()) result = frame[upper(fnName)];
    if (outputs)
        for (const auto* p : inputs)
            if (p->section != LocalVar::Section::Input) outputs->emplace_back(p->name, frame[upper(p->name)]);
    return true;
}

// 1.12.26 : un script DETACHE tourne en arriere-plan (sa tache, a tour de role avec l'IHM) ; les autres, ici.
bool Runtime::runScript(const Script& sc, const std::string& source, double now, std::string* error) {
    if (taskStopped()) return false;              // 1.13.0 : la simulation s'arrete pendant une pause : plus rien ne part
    if (sc.detached && sc.lang == ScriptLang::ST && running_) return startDetached(sc, source, now, error);
    // 1.13.0 : le debogueur - un point arme, un pas ou une pause demandes, IHM_POINT_ARRET dans le projet : le script
    // tourne sur un fil a lui, pour pouvoir s'arreter au milieu. Sinon (et dans une tache deja), ici, comme avant.
    if (sc.lang == ScriptLang::ST && running_ && !currentTask_ && debugWanted()) return startDebugTask(sc, source, now, error);
    // Dans une tache du debogueur : sa copie - il peut s'arreter, et le projet changer pendant la pause.
    if (currentTask_ && !currentTask_->detached && sc.lang == ScriptLang::ST) {
        const Script copy = sc;
        return runScriptHere(copy, source, now, error);
    }
    return runScriptHere(sc, source, now, error);
}

bool Runtime::runScriptHere(const Script& sc, const std::string& source, double now, std::string* error) {
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
    const std::int64_t pausedBefore = pausedNs_;      // 1.13.0 : une pause du debogueur ne compte pas
    // 1.12.17 : son cadre (le journal des lenteurs) ; dans un geste, une ligne de son « Dedans ».
    const Id scriptView = project_ ? project_->viewOfScript(sc.id) : kNoId;
    // 1.13.0 : son nom, pas la vue elle-meme - le script peut s'arreter, et rien ne doit pendre apres la pause.
    const View* ownerView = scriptView != kNoId ? viewOf(scriptView) : nullptr;
    const std::string ownerName = ownerView ? ownerView->name : std::string{};
    const bool cyclic = inCycle_ && depth_ == 1;
    // 1.12.26 : dans une tache detachee, le temps compte ses attentes : pas une mesure. 1.13.0 : une tache du debogueur
    // (non detachee) se mesure, sans le temps de ses pauses (debugReached le retranche).
    const bool measured = !currentTask_ || !currentTask_->detached;
    std::optional<SlowGuard> slowFrame;
    if (measured)
        slowFrame.emplace(*this, SlowKind::Script, "script:" + std::to_string(sc.id),
                          "Script " + sc.name + (cyclic && !ownerName.empty() ? ", \xC3\xA0 chaque cycle" : std::string{}),
                          !ownerName.empty() ? ownerName : std::string("IHM"), scriptView, kNoId, sc.id);
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
    const std::string& code = decl::codeOf(sc, composed);
    // 1.13.0 (lot J8) : un script d'un espace, ou qui importe des espaces (USING) : ses noms courts en entier.
    const bool spaced = project_ && (!sc.ns.empty() || names::mentionsUsing(sc.body));
    const bool ok = runStatements(spaced ? names::qualify(*project_, code, sc.ns, {}, false) : code, source, now, why, sc.id);
    origin_ = callerOrigin;
    if (!ok) XPG_TRACE(Script, "erreur du script %s : %s", source.c_str(), why->empty() ? "(raison inconnue)" : why->c_str());
    auto& sp = perf_.scripts[sc.id];
    if (sp.name.empty()) sp.name = source;
    const double scriptMs = std::max(0.0, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()
                                              - static_cast<double>(pausedNs_ - pausedBefore) / 1e6);
    if (measured) sp.time.add(scriptMs);          // 1.12.26 : un script detache attend : pas une mesure
    if (cyclic && measured) {                     // 1.12.17 : un poste du cycle
        std::string label = !ownerName.empty() ? "Script " + sc.name + " (" + ownerName + ")"
                                               : "Script g\xC3\xA9n\xC3\xA9ral " + sc.name
                                            + (sc.event == "Cyclique" ? " (toutes les " + perfMs(std::max(10, sc.periodMs)) + ")"
                                                                      : std::string(" (sur changement)"));
        cyclePost("script:" + std::to_string(sc.id), label, scriptMs);
    }
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
    return runScript(*sc, "script " + fullName(*sc), now, error);   // 1.13.0 (lot J8) : Gaz.Horloge
}

// ---- 1.12.7 : LES VALEURS CALCULEES AU DEMARRAGE (HmiCalc.hpp) ----------------------------
namespace {
// Un pas d'un chemin dans un objet : .Membre, [3], [2, 1], ['cle'].
bool stepInto(const sim::ObjRef& from, std::string_view& rest, sim::ObjRef& to) {
    if (!from || !from->type || rest.empty()) return false;
    const auto& type = *from->type;
    if (rest.front() == '.') {
        std::size_t end = 1;
        while (end < rest.size() && rest[end] != '.' && rest[end] != '[') ++end;
        const std::string member = upper(rest.substr(1, end - 1));
        rest.remove_prefix(end);
        if (type.kind == sim::TypeDesc::Kind::List && (member == "FIRST" || member == "LAST")) {
            if (from->items.empty()) return false;
            to = member == "FIRST" ? from->items.front() : from->items.back();
            return true;
        }
        for (std::size_t k = 0; k < type.members.size() && k < from->items.size(); ++k)
            if (upper(type.members[k].first) == member) {
                to = from->items[k];
                return true;
            }
        return false;
    }
    if (rest.front() != '[') return false;
    const auto close = rest.find(']');
    if (close == std::string_view::npos) return false;
    const std::string inside = trimmedView(rest.substr(1, close - 1));
    rest.remove_prefix(close + 1);
    if (type.kind == sim::TypeDesc::Kind::Map) {
        if (inside.size() >= 2 && (inside.front() == '\'' || inside.front() == '"')) {
            const auto it = from->textKeys.find(inside.substr(1, inside.size() - 2));
            if (it == from->textKeys.end()) return false;
            to = it->second;
            return true;
        }
        char* stop = nullptr;
        const long long k = std::strtoll(inside.c_str(), &stop, 10);
        if (!stop || *stop) return false;
        const auto it = from->intKeys.find(k);
        if (it == from->intKeys.end()) return false;
        to = it->second;
        return true;
    }
    std::vector<std::int64_t> indices;
    std::size_t from0 = 0;
    for (;;) {
        const auto comma = inside.find(',', from0);
        const std::string piece = trimmedView(std::string_view(inside).substr(from0, comma == std::string::npos ? std::string::npos : comma - from0));
        char* stop = nullptr;
        const long long v = std::strtoll(piece.c_str(), &stop, 10);
        if (piece.empty() || !stop || *stop) return false;
        indices.push_back(v);
        if (comma == std::string::npos) break;
        from0 = comma + 1;
    }
    if (type.kind == sim::TypeDesc::Kind::Array) {
        std::size_t flat = 0;
        if (!sim::flatIndex(type, indices, flat, nullptr) || flat >= from->items.size()) return false;
        to = from->items[flat];
        return true;
    }
    if (type.kind == sim::TypeDesc::Kind::List && indices.size() == 1 && indices[0] >= 0
        && static_cast<std::size_t>(indices[0]) < from->items.size()) {
        to = from->items[static_cast<std::size_t>(indices[0])];
        return true;
    }
    return false;
}
} // namespace

sim::ObjRef Runtime::qualifiedRoot(std::string_view path, std::string* rest) const {
    if (qualified_.empty()) return nullptr;
    const auto dot = path.find('.');
    if (dot == std::string_view::npos) return nullptr;
    std::size_t end = dot + 1;
    while (end < path.size() && path[end] != '.' && path[end] != '[') ++end;
    const auto it = qualified_.find(upper(path.substr(0, end)));
    if (it == qualified_.end()) return nullptr;
    if (rest) *rest = std::string(path.substr(end));
    return it->second;
}

bool Runtime::qualifiedRead(std::string_view path, sim::Value& out) const {
    std::string restText;
    sim::ObjRef at = qualifiedRoot(path, &restText);
    if (!at) return false;
    std::string_view rest = restText;
    while (!rest.empty()) {
        sim::ObjRef next;
        if (!stepInto(at, rest, next) || !next) return false;
        at = next;
    }
    if (!at->type || at->type->kind != sim::TypeDesc::Kind::Scalar) return false;   // un objet : richVariable
    out = at->value;
    return true;
}

sim::ObjRef Runtime::computedValue(Id script, std::string_view name) const {
    const auto it = initials_.find(script);
    return it == initials_.end() ? nullptr : it->second.find(name);
}

std::map<std::string, sim::ObjRef> Runtime::computedObjects(const Project& p, std::string* error) {
    std::map<std::string, sim::ObjRef> out;
    if (error) error->clear();
    if (!calc::hasComputed(p)) return out;
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.prime(0.0);
    for (const auto& [script, values] : rt.initials_)
        for (const auto& [name, obj] : values.all())
            if (obj) out[std::to_string(script) + ":" + name] = obj;
    if (error) *error = rt.startError_;
    return out;
}

sim::ObjRef Runtime::valueObject(const Project& p, std::string_view type, std::string_view value, std::string* why) {
    Runtime rt;
    rt.bind(&p, nullptr);
    return sim::makeRichValue(type, value, *rt.env_, why);
}

bool Runtime::computeOne(const Script& sc, const Declaration& d, Id view, sim::ObjRef& out, std::string& why,
                         std::shared_ptr<const Scope> scope) {
    const View* v = view != kNoId && project_ ? project_->view(view) : nullptr;
    const std::string source = "calcul " + calc::pathOf(sc, d, v);
    // 1.12.16 : un calcul qui rend sa valeur (RETURN, Init_Nom := ...) tourne dans sa fonction Init_Nom
    // (runnableCode) ; un retour qui ne va pas dans le type est refuse, a sa ligne, avant de tourner.
    if (project_ && calc::returnsValue(d))
        for (const auto& is : calc::returnIssues(*project_, sc, d))
            if (is.error) {
                why = (is.line > 0 ? "ligne " + std::to_string(is.line) + " : " : std::string{}) + is.message;
                return false;
            }
    // 1.12.17 : un calcul se mesure (a l'ouverture d'une popup : une ligne de son « Dedans »).
    SlowGuard slowFrame(*this, SlowKind::Script, "calcul:" + std::to_string(sc.id) + ":" + d.name,
                        d.name + (v ? ", constante calcul\xC3\xA9" "e \xC3\xA0 l'ouverture" : ", valeur calcul\xC3\xA9" "e au d\xC3\xA9marrage"),
                        v ? v->name : std::string("IHM"), view, kNoId, sc.id);
    const Prepared& prep = prepare(calc::runnableCode(sc, d), source, false);
    if (!prep.program) {
        why = prep.error;
        return false;
    }
    // Les valeurs deja calculees de son script qu'il lit (une locale du meme nom passe avant).
    sim::Locals given;
    if (const auto it = initials_.find(sc.id); it != initials_.end())
        for (const auto& x : calc::declarationsFor(sc, d)) {
            const bool local = upper(x.name) == upper(d.name)
                            || std::any_of(d.locals.begin(), d.locals.end(), [&](const Declaration& l) { return upper(l.name) == upper(x.name); });
            if (!local)
                if (auto o = it->second.find(x.name)) given.set(x.name, o);
        }
    AliasGuard aliasGuard(env_->aliases, scope);             // ni vue ni parametres au demarrage (1.12.16 : ceux de l'ouverture)
    Env::Frame frame;
    env_->frames.push_back(&frame);
    sim::Locals mine;                                         // ses declarations, le temps du calcul
    env_->diagnostics.clear();
    const std::string previous = source_;
    source_ = source;
    const CodeOrigin callerOrigin = origin_;
    origin_ = CodeOrigin{};
    origin_.script = sc.id;
    origin_.view = view;
    origin_.name = source;
    sim::RunLimits limits;
    limits.maxIterationsPerLoop = 1000000;
    limits.maxStatementsPerScan = 5000000;
    std::uint64_t spent = 0;                                  // 2 s au plus par calcul (l'heure limite suit ce compte)
    limits.scanStatements = &spent;
    limits.maxScanStatements = 50000000;
    limits.hasDeadline = true;
    limits.deadlineMicros = sim::monotonicMicros() + 2000000;
    limits.locals = &mine;
    limits.initials = &given;
    const sim::ExecTrace callerTrace = trace_;
    trace_ = {};
    limits.trace = &trace_;
    ++depth_;
    // 1.13.0 (le debogueur) : une execution du travail (execDepth_) - un operateur, une methode qu'elle appelle peut
    // s'arreter, et son code changer pendant la pause : le calcul s'arrete la, sans erreur (why vide, voir computeAtOpen).
    ++execDepth_;
    const auto result = sim::execute(*prep.program, *env_, limits);
    const bool abandoned = endExecution();
    --depth_;
    trace_ = callerTrace;
    origin_ = callerOrigin;
    source_ = previous;
    env_->frames.pop_back();
    if (abandoned || taskStopped()) {
        abort_.clear();
        why.clear();
        return false;
    }
    for (const auto& dg : env_->diagnostics)
        if (dg.severity == sim::Diagnostic::Severity::Error) {
            why = (dg.line ? "ligne " + std::to_string(dg.line) + " : " : std::string{}) + shownCodeNames(project_, frenchSimMessage(dg.message));
            break;
        }
    if (!abort_.empty()) {
        if (why.empty()) why = abort_;
        abort_.clear();
    }
    if (why.empty() && !result.completed)
        why = "arr\xC3\xAAt\xC3\xA9 : trop long (plus de 2 s, ou budget d'instructions d\xC3\xA9pass\xC3\xA9 : boucle sans fin ?)";
    if (!why.empty()) return false;
    const sim::ObjRef got = mine.find(d.name);
    if (!got) {
        why = d.name + " : aucune valeur";
        return false;
    }
    out = sim::deepCopy(*got);
    return true;
}

bool Runtime::computeDeclarations() {
    initials_.clear();
    qualified_.clear();
    startError_.clear();
    if (!project_) return true;
    // Les constantes publiques ecrites des scripts generaux : lues d'ailleurs par leur nom qualifie. Une
    // valeur qui n'est pas un litteral (Max * 2, une autre constante) : calculee dans son script, plus bas.
    std::vector<std::pair<const Script*, const Declaration*>> written;
    for (const auto& sc : project_->programs.scripts) {
        if (sc.lang != ScriptLang::ST) continue;
        for (const auto& d : sc.decls) {
            if (d.kind != DeclKind::Constant || d.computed || d.visibility != Visibility::Public) continue;
            std::string why;
            if (auto o = sim::makeRichValue(d.type, d.value, *env_, &why)) qualified_[upper(sc.name + "." + d.name)] = std::move(o);
            else written.emplace_back(&sc, &d);
        }
    }
    // 1.12.16 : les constantes des vues et des popups, publiques et privees (Generer dit qui les lit) :
    // leur porteur est un script du nom de la vue. Celles de l'ouverture attendent l'ouverture.
    std::deque<Script> holders;
    for (const auto& v : project_->views) {
        if (v.constants.empty()) continue;
        holders.push_back(viewmembers::constantsScript(v));
        for (const auto& d : holders.back().decls) {
            if (d.kind != DeclKind::Constant || d.computed) continue;
            std::string why;
            if (auto o = sim::makeRichValue(d.type, d.value, *env_, &why)) qualified_[upper(v.name + "." + d.name)] = std::move(o);
            else written.emplace_back(&holders.back(), &d);
        }
    }
    const auto holderOf = [&](Id id) -> const Script* {
        for (const auto& h : holders)
            if (h.id == id) return &h;
        return nullptr;
    };
    const auto writtenByCode = [&] {
        for (const auto& [sc, d] : written) {
            Declaration tmp = *d;
            tmp.computed = true;
            tmp.calc = d->name + " := " + d->value + ";\n";
            sim::ObjRef value;
            std::string why;
            if (computeOne(*sc, tmp, kNoId, value, why)) qualified_[upper(sc->name + "." + d->name)] = std::move(value);
        }
    };
    if (!calc::hasComputed(*project_)) {
        writtenByCode();
        return true;
    }
    const auto stop = [&](const std::string& source, const std::string& why) {
        startError_ = source + " : " + why;
        logAt(LogLevel::Error, "Erreur", source, why, announcedLine(why));
        log("Syst\xC3\xA8me", {}, "d\xC3\xA9marrage arr\xC3\xAAt\xC3\xA9 : " + startError_);
        return false;
    };
    const auto pl = calc::plan(*project_);
    if (!pl.cycle.empty()) return stop("calcul", "calculs en boucle : " + pl.cycle + " (chacun attend l'autre)");
    for (const auto& step : pl.order) {
        const Script* sc = nullptr;
        if (step.view == kNoId) {
            for (const auto& g : project_->programs.scripts)
                if (g.id == step.script) sc = &g;
            if (!sc) sc = holderOf(step.script);                  // 1.12.16 : les constantes d'une vue
        } else if (const View* v = project_->view(step.view)) {
            for (const auto& vs : v->scripts)
                if (vs.id == step.script) sc = &vs;
        }
        if (!sc) continue;
        const Declaration* d = nullptr;
        for (const auto& x : sc->decls)
            if (upper(x.name) == upper(step.name)) d = &x;
        if (!d) continue;
        sim::ObjRef value;
        std::string why;
        if (!computeOne(*sc, *d, step.view, value, why)) {
            // La Console y mene (un double-clic) : le script, la declaration et la ligne de son calcul.
            const int at = announcedLine(why);
            startError_ = "calcul " + step.path + " : " + why;
            const CodeOrigin was = origin_;
            origin_ = CodeOrigin{"calcul " + step.path, step.view, kNoId, sc->id, kNoId, d->name};
            logAt(LogLevel::Error, "Erreur", "calcul " + step.path, why, at > 0 ? at : -1);
            origin_ = was;
            log("Syst\xC3\xA8me", {}, "d\xC3\xA9marrage arr\xC3\xAAt\xC3\xA9 : " + startError_);
            return false;
        }
        initials_[sc->id].set(d->name, value);
        if (step.view == kNoId && d->kind == DeclKind::Constant && (d->visibility == Visibility::Public || holderOf(sc->id)))
            qualified_[upper(sc->name + "." + d->name)] = value;
        log("Calcul", step.path, "= " + sim::display(*value));
    }
    writtenByCode();
    return true;
}

void Runtime::computeAtOpen(Id id) {
    const View* v = project_ ? project_->view(id) : nullptr;
    if (!v || v->constants.empty()) return;
    std::vector<const Declaration*> todo;
    for (const auto& d : v->constants)
        if (d.kind == DeclKind::Constant && viewmembers::computedAtOpen(*project_, *v, d)) todo.push_back(&d);
    if (todo.empty()) return;
    const Script holder = viewmembers::constantsScript(*v);
    const auto scope = scopePtr(id);
    // Dans l'ordre ou elles se lisent : une qui en lit une autre de l'ouverture passe apres (au plus
    // autant de tours que de constantes ; ce qui reste est une boucle, que Generer dit).
    std::set<std::string> pending;
    for (const auto* d : todo) pending.insert(upper(d->name));
    for (std::size_t round = 0; round <= todo.size() && !pending.empty(); ++round)
        for (const auto* d : todo) {
            if (!pending.count(upper(d->name))) continue;
            const auto a = calc::analyse(*project_, holder, *d, nullptr);
            const bool ready = std::none_of(a.reads.begin(), a.reads.end(), [&](const std::string& r) {
                return r.find('.') == std::string::npos && pending.count(upper(r)) && upper(r) != upper(d->name);
            });
            if (!ready && round < todo.size()) continue;
            pending.erase(upper(d->name));
            const Declaration* inHolder = nullptr;
            for (const auto& x : holder.decls)
                if (x.id == d->id) inHolder = &x;
            sim::ObjRef value;
            std::string why;
            if (!inHolder || !computeOne(holder, *inHolder, kNoId, value, why, scope)) {
                const std::string path = v->name + "." + d->name;
                // 1.13.0 (le debogueur) : arrete sans erreur (un code qu'il appelait a change pendant la pause, la
                // simulation s'arrete) - pas de faute ; la vue s'ouvre, son OnOpen part.
                if (inHolder && why.empty()) {
                    if (!taskStopped())
                        logAt(LogLevel::Warning, "D\xC3\xA9" "bogage", "calcul " + path + " (\xC3\xA0 l'ouverture)",
                              "arr\xC3\xAAt\xC3\xA9 sans erreur (son code a chang\xC3\xA9 pendant la pause) : pas de nouvelle valeur, "
                              "elle se recalcule \xC3\xA0 la prochaine ouverture");
                    continue;
                }
                const CodeOrigin was = origin_;
                origin_ = CodeOrigin{"calcul " + path, id, kNoId, kNoId, kNoId, d->name};
                logAt(LogLevel::Error, "Erreur", "calcul " + path + " (\xC3\xA0 l'ouverture)", why, announcedLine(why));
                origin_ = was;
                continue;
            }
            initials_[holder.id].set(d->name, value);
            qualified_[upper(v->name + "." + d->name)] = value;
        }
}

void Runtime::runViewScript(const View& v, std::string_view event, double now) {
    AliasGuard guard(env_->aliases, scopePtr(v.id));
    // Lot API 8 : un script de vue part avec la vue (ouverture, fermeture, cycle),
    // pas d'un geste : IHM_EXPORTER n'y demande pas ou enregistrer.
    struct ExportAskOff { int& n; int was; ~ExportAskOff() { n = was; } } exportAsk{exportAsk_, exportAsk_};
    exportAsk_ = 0;
    // 1.13.0 (le debogueur) : les scripts de cet evenement, copies d'abord - l'un d'eux peut s'arreter au milieu
    // (la suite attend), et la vue (une vue composee du moteur) ne doit pas pendre apres la pause.
    std::vector<std::pair<Script, std::string>> todo;
    for (const auto& sc : v.scripts) {
        if (sc.event != event) continue;
        // Un script emprunte (lot 6) : "Vue_Production.OnOpen (Modele_Conduite)".
        std::string source = v.name + "." + std::string(event);
        if (project_)
            if (const Id owner = project_->viewOfScript(sc.id); owner != kNoId && owner != v.id)
                if (const auto* ov = project_->view(owner)) source += " (" + ov->name + ")";
        todo.emplace_back(sc, std::move(source));
    }
    for (const auto& [sc, source] : todo) {
        if (taskStopped()) return;
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
        case Operation::RunScript: case Operation::CallScript: case Operation::Call:   // 1.12.19 : Appeler
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
    if (taskStopped()) return;                   // 1.13.0 : la simulation s'arrete pendant une pause : plus rien ne part
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
    // 1.12.13 : une action d'un geste sur un tableau voit ses parametres (Ligne, Colonne, Element...), fils de la vue.
    AliasGuard aliasGuard(env_->aliases, gestureScope_ && gestureView_ == v.id ? gestureScope_ : scopePtr(v.id));
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
                {
                    const std::string cell = concretePath(a.target);
                    log("Action", source, std::string(operationLabel(a.operation)) + " " + cell + " = " + (next ? "TRUE" : "FALSE") + (lastWriteForced_ ? std::string(" : ignor\xC3\xA9" "e (forc\xC3\xA9" "e)") : std::string{}) + writtenAs(a.target, cell));
                }
            break;
        }
        case Operation::Increment:
        case Operation::Decrement: {
            sim::Value cur;
            if (!currentValue(cur)) { log("Erreur", source, a.target + " : variable inconnue"); break; }
            // 1.12.5 (lot A, D15) : un texte ne s'incremente pas (avant : il se vidait).
            if (cur.type() == sim::Type::String) {
                log("Erreur", source, a.target + " est un texte (STRING) : " + std::string(a.operation == Operation::Increment ? "incr\xC3\xA9menter" : "d\xC3\xA9" "cr\xC3\xA9menter")
                                          + " ne s'y applique pas - rien n'est \xC3\xA9" "crit");
                break;
            }
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
                const std::string cell = concretePath(a.target);
                log("Action", source, cell + " : " + formatValue(cur) + " \xE2\x86\x92 " + formatValue(after) + (lastWriteForced_ ? std::string(" : ignor\xC3\xA9" "e (forc\xC3\xA9" "e)") : std::string{}) + writtenAs(a.target, cell));
            }
            break;
        }
        case Operation::Assign: {
            auto it = expressions_.find(a.value);
            if (it == expressions_.end()) it = expressions_.emplace(a.value, Expression::compile(a.value)).first;
            auto val = it->second.evaluate(*env_);
            if (!val) { log("Erreur", source, a.target + " := " + a.value + " : " + val.error().message()); break; }
            // 1.12.6 (lot B, § 2.2) : la valeur d'Affecter, convertie vers le type de la variable (une
            // enumeration par son nom, un texte '5 s' dans un TIME, 40000 ramene au maximum d'un INT, dit).
            if (o) {
                const auto converted = roles::toVariable(*val, writeTarget(a.target, o));
                if (!converted.ok) { log("Erreur", source, a.target + " := " + a.value + " : " + converted.note); break; }
                if (!converted.note.empty()) log("Action", source, a.target + " : " + converted.note);
                *val = converted.value;
            }
            if (write(a.target, *val, source)) {
                const std::string cell = concretePath(a.target);
                log("Action", source, cell + " := " + formatValue(*val) + (lastWriteForced_ ? std::string(" : ignor\xC3\xA9" "e (forc\xC3\xA9" "e)") : std::string{}) + writtenAs(a.target, cell));
            }
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
            // 1.13.0 (le debogueur) : son code peut s'arreter comme un script (hors d'un geste du debogueur : sa tache).
            if (unitWanted()) {
                const std::string code = a.value, src = source + " (script de l'action)";
                (void)runDebugTask(src, [this, code, src, now](std::string*) { return runStatements(code, src, now, nullptr); }, nullptr, nullptr);
            } else {
                (void)runStatements(a.value, source + " (script de l'action)", now, nullptr);
            }
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
        case Operation::Call: {                              // 1.12.19 : Appeler - Cible(arguments);
            const std::string code = callCode(a);
            if (code.empty()) { log("Erreur", source, "Appeler : rien \xC3\xA0 appeler (la cible est vide)"); break; }
            if (depth_ >= kMaxDepth) { log("Erreur", source, "appel circulaire"); break; }
            ++depth_;
            const CodeOrigin callerOrigin = origin_;
            origin_ = CodeOrigin{};
            origin_.name = source + " (Appeler " + trimmedView(a.target) + ")";
            origin_.view = v.id;
            origin_.object = o ? o->id : kNoId;
            // 1.13.0 (le debogueur) : la fonction appelee peut s'arreter (hors d'un geste du debogueur : sa tache).
            if (unitWanted()) {
                const std::string src = origin_.name;
                (void)runDebugTask(src, [this, code, src, now](std::string*) { return runStatements(code, src, now, nullptr); }, nullptr, nullptr);
            } else {
                (void)runStatements(code, origin_.name, now, nullptr);
            }
            origin_ = callerOrigin;
            --depth_;
            break;
        }
        case Operation::Log:
            log("Journal", source, TextTemplate::compile(a.value).render(*env_));
            break;
        // 1.14.0 (M4) : les enregistrements - Enregistrement, Relecture, Fichiers (HmiRuntimeRecord.cpp).
        case Operation::RecordStart: case Operation::RecordStop: case Operation::RecordToggle: case Operation::RecordCancel:
        case Operation::ReplayStart: case Operation::ReplayPause: case Operation::ReplaySeek: case Operation::ReplaySpeed:
        case Operation::ReplayQuit: case Operation::RecordingExport: case Operation::RecordingDelete:
            (void)recordingCommand(v, o, a, now);
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
            (void)playSound(name, source, SoundChannel::Actions);   // 1.14.0 : le canal Actions
            break;
        }
        // 1.14.0 : le son - tout couper, retablir, basculer ; le volume d'un canal ; couper ou retablir un canal.
        // Le canal : son mot (Alarmes), son nom (Videos accentue), ou une expression qui le donne.
        case Operation::SoundMute: (void)setChannelMuted(SoundChannel::General, true, source); break;
        case Operation::SoundRestore: (void)setChannelMuted(SoundChannel::General, false, source); break;
        case Operation::SoundToggle: (void)setChannelMuted(SoundChannel::General, settings_.soundOn, source); break;
        case Operation::SoundVolume: case Operation::SoundChannelMute: {
            std::string name = trimText(a.target);
            if (a.operation == Operation::SoundVolume && name.empty()) name = "General";
            auto channel = soundChannelFrom(name);
            if (!channel && !name.empty()) {
                bool ok = false;
                const auto evaluated = evalText(name, &ok);
                if (ok) channel = soundChannelFrom(evaluated);
            }
            if (!channel) {
                log("Erreur", source, std::string(operationLabel(a.operation)) + " : canal inconnu '" + name + "'");
                break;
            }
            if (a.operation == Operation::SoundVolume) {
                const std::string raw = trimText(a.value).empty() ? std::string("100") : trimText(a.value);
                double v = 0;
                if (!parseNumber(raw, v)) {
                    bool ok = false;
                    const auto evaluated = evalText(raw, &ok);
                    if (!ok || !parseNumber(evaluated, v)) {
                        log("Erreur", source, "r\xC3\xA9gler le volume : '" + raw + "' n'est pas un nombre de 0 \xC3\xA0 100");
                        break;
                    }
                }
                (void)setChannelVolume(*channel, static_cast<int>(std::lround(v)), source);
                break;
            }
            const std::string how = plain(a.value);
            const bool muted = how.rfind("bascul", 0) == 0 ? !soundMuted(settings_, *channel)
                             : how.rfind("r", 0) == 0 ? false : true;          // retablir ; sinon couper
            (void)setChannelMuted(*channel, muted, source);
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
        // 1.11.23 : Valider les saisies - le champ en cours de cette vue ecrit sa valeur (comme
        // Entree) ; refusee, il garde le focus et son message, et la suite du geste s'arrete.
        case Operation::SubmitInputs: {
            submitFailed_ = false;
            const Object* field = focused_ != kNoId ? v.object(focused_) : nullptr;
            if (field && field->kind == Kind::InputField) {
                const Id id = field->id;
                submitForm(v, *field, now);
                submitFailed_ = focused_ == id;
            }
            break;
        }
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
    const bool byUser = t == Trigger::Click || t == Trigger::DoubleClick || t == Trigger::LongPress
                     || triggerIsKey(t);                      // 1.11.23 : un raccourci (runKeyActions)
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
    std::size_t notDone = 0;
    for (std::size_t i = 0; i < copy.size(); ++i) {
        if (copy[i].trigger != t) continue;
        // 1.14.0 (relecture M4b) : UNE ACTION A LANCE UNE RELECTURE - la suite ne commande plus rien. Seules les actions
        // du groupe Relecture (Rejouer, Pause, Avancer ou reculer, Vitesse, Quitter) passent encore ; apres Quitter la
        // relecture, la suite reprend.
        if (replayOn_ && !operationIsReplay(copy[i].operation)) {
            ++notDone;
            continue;
        }
        actionOrigin_ = i < origins.size() ? origins[i] : std::string{};
        fire(v, o, copy[i], now, byUser);
        // 1.11.23 : une saisie refusee par Valider les saisies arrete le geste (la popup reste).
        if (copy[i].operation == Operation::SubmitInputs && submitFailed_) {
            submitFailed_ = false;
            break;
        }
    }
    actionOrigin_.clear();
    if (notDone > 0)
        log("Relecture", where(v, o),
            std::to_string(notDone) + (notDone > 1 ? " actions" : " action") + " de la suite non faite" + (notDone > 1 ? "s" : "")
                + " : une relecture est affich\xC3\xA9" "e, l'IHM ne commande rien (seules Pause, Avancer, Vitesse et Quitter passent)");
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
    computeAtOpen(id);                  // 1.12.16 : ses constantes de l'ouverture, avant son script d'ouverture
    if ((v = viewOf(id)) == nullptr) return;
    runViewScript(*v, "OnOpen", now);
    if ((v = viewOf(id)) == nullptr) return;
    const View snapshot = *v;
    runActions(snapshot, nullptr, Trigger::ViewOpen, now);
    for (const auto& o : snapshot.objects)
        if (!o.actions.empty()) runActions(snapshot, &o, Trigger::ViewOpen, now);
    gifsOpenView(snapshot, now);        // lot 16 : les GIF animes "a l'affichage" partent
    mediasOpenView(snapshot, now);      // 1.12.12 (lot H) : les videos "a l'affichage" aussi
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
    if (paused_) return false;                   // 1.13.0 : en pause (le debogueur), l'IHM est figee
    if (unitWanted()) {
        auto done = std::make_shared<bool>(false);
        (void)debugUnit("Navigation", [this, id, t, now, args = std::string(arguments), done] { *done = navigate(id, t, now, args); });
        return *done;
    }
    now_ = std::max(now_, now);
    if (!running_ || !viewOf(id)) return false;
    if (id == current_ && popups_.empty() && arguments.empty()) return true;
    if (depth_ >= kMaxDepth) {
        log("Erreur", source_, "navigation circulaire : plus de " + std::to_string(kMaxDepth) + " navigations en cha\xC3\xAEne");
        return false;
    }
    ++depth_;
    // 1.12.17 : afficher une vue se mesure (le journal des lenteurs).
    SlowGuard slowFrame(*this, SlowKind::View, "vue:" + std::to_string(id),
                        "Afficher " + viewOf(id)->name + " (" + std::to_string(viewOf(id)->objects.size()) + " objets)", "IHM", id);
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
    if (view == current_) return currentScope_;
    // 1.11.23 : le relachement d'un raccourci dont la popup s'est fermee - ses parametres d'alors.
    return view == keyScopeView_ ? keyScope_ : nullptr;
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
    // 1.12.17 : ouvrir une popup se mesure (le journal des lenteurs).
    const View* underView = viewOf(under);
    SlowGuard slowFrame(*this, SlowKind::Popup, "popup:" + std::to_string(id),
                        "Ouvrir " + v->name + (arguments.empty() ? std::string{} : " (" + std::string(arguments) + ")"),
                        underView ? underView->name : std::string("IHM"), id);
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
    if (paused_) return false;                   // 1.13.0 : en pause (le debogueur), la croix ne ferme rien (ni OnClose)
    if (unitWanted()) {
        auto done = std::make_shared<bool>(false);
        (void)debugUnit("Fermeture de la popup", [this, index, t, now, done] { *done = closePopupAt(index, t, now); });
        return *done;
    }
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
    ++startCount_;                      // 1.13.0 : la seance du debogueur repart de zero passage
    stopTasks();                        // 1.12.26 : aucune tache d'avant ne survit
    // 1.13.0 : le debogueur - ni pause ni pas d'avant ; les passages repartent de zero, les points se relisent
    // sur le projet de ce demarrage.
    clearDebugState();
    deferred_.clear();
    if (debugOn_) {
        for (auto& bp : breakpoints_) bp.hits = 0;
        debugSaid_.clear();
        codeBreaksAt_ = -1e9;
        resolveBreakpoints();
        debugTick(now);
    }
    env_->anyUnit = -1;                 // 1.13.0 (lot J6) : les unites du projet se relisent a chaque demarrage
    env_->unitCatalog.reset();          // (celles des bibliotheques reliees aussi)
    env_->anyPrivate = -1;              // 1.13.0 : le prive aussi (un membre rendu prive depuis)
    env_->privateSeen.clear();
    // 1.12.2 : les sources d'elements (itemsFrom) se lisent dans ce moteur tant qu'il tourne.
    registerChoiceResolver(this, [this](const Object& o, std::string_view source, std::vector<Choice>& out) { return resolveChoices(source, out, &o); });
    composed_.clear(); boundCalls_.clear();
    slaveReads_.clear();                // 1.9 : les bascules d'avant ne comptent plus
    forcedIhm_.clear();                 // 1.11.5 : les variables repartent de leur valeur initiale
    prompt_.reset();                    // 1.11.7 : aucun clavier d'action ouvert
    resetInput();                       // 1.11.23 : ni touche tenue ni bouton enfonce
    running_ = true;
    now_ = startNow_ = now;
    // 1.12.17 : les performances dans le temps repartent ; le journal des lenteurs reste. Les seuils :
    // ceux du projet.
    if (project_) {
        slowRules_.absoluteMs = std::max(1, project_->config.slowMs);
        slowRules_.factor = std::max(2, project_->config.slowFactor);
    }
    resetPerfHistory();
    usual_.clear();
    slowStack_.clear();
    cycleParts_.clear();
    exchangeRequests_.clear();
    exchangeSampled_ = -1;
    startWallMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count();
    heldWallMs_ = 0;                    // 1.14.0 (relecture M4b) : le temps tenu par les relectures
    lastCycle_ = -1;
    journal_.clear();
    errorCount_ = 0;
    lastError_.clear();
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
    gestureFormulas_.clear();
    // 1.12.5 (lot A) : un conteneur a onglets relie a une ENUMERATION : ses nombres, onglet par onglet
    // (pageValues) - la page montre le rang de la valeur, un clic ecrit la k-ieme (avant : decale d'un cran).
    if (project_)
        for (const auto& pv : project_->views)
            for (const auto& po : pv.objects) {
                if (po.kind != Kind::TabContainer) continue;
                const std::string var = trimText(markers::strip(po.text("variable")));
                if (var.empty()) continue;
                const auto* e = findEnumeration(*project_, params::expressionType(*project_, &pv, var));
                if (!e || e->values.empty()) continue;
                std::string numbers;
                for (const auto& ev : e->values) numbers += (numbers.empty() ? "" : ";") + std::to_string(ev.value);
                overrides_[{pv.id, po.id}]["pageValues"] = numbers;
            }
    backgrounds_.clear();
    gifs_.clear();                      // lot 16
    mediasReset();                      // 1.12.12 (lot H)
    docsReset();                        // 1.12.30
    converters_.clear();                // 1.14.1 (lot N5) : le Convertisseur d'unites
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
    pid_.clear();                       // 1.12.24 : les Reglages PID se relisent (leur memoire : l'historique)
    pidPlan_.clear();
    pidPlanAt_ = -1e9;
    pidWindow_ = PidWindow{};
    trends_.clear();
    trendPaused_.clear();   // 1.12.16
    trendStates_.clear();   // 1.13.1 : le gel, la fenetre, les curseurs
    resetLot11();            // lot 11 : graphiques, mises de cote, production, editeurs, export
    resetLot13();            // lot 13 : signature, renouvellement, badge (l'audit et les verrous restent)
    resetLot12();            // lot 12 : l'historique de navigation, le zoom
    resetLogin();            // lot 12 : le menu de connexion repart ferme
    recipeSelection_.clear();
    tableSources_.clear();
    retained_.clear();
    scriptLocals_.clear();                       // 1.10
    initials_.clear();                           // 1.12.7 : recalculees ci-dessous
    qualified_.clear();
    startError_.clear();
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
        auto variables = takePersistent(*startData_);
        // 1.12.19 : une variable rappel - le nom de sa fonction, ecrit tel quel (une fonction disparue : dit).
        std::size_t callbacks = 0;
        std::vector<std::string> lost;
        variables.erase(std::remove_if(variables.begin(), variables.end(), [&](const simdata::Cell& c) {
            if (types::callbackForm(project_, c.declared).empty() || c.value.type() != sim::Type::String) return false;   // 1.12.20 : T_Filtre aussi
            std::string why;
            if (setRichVariable(c.name, c.value.asString(), &why)) ++callbacks;
            else lost.push_back(c.name + " : " + why);
            return true;
        }), variables.end());
        lastRestore_ = simdata::restoreVariables(*project_, variables, [this](const std::string& path) { return ihmSlot(path); });
        if (callbacks) lastRestore_->notes.push_back(std::to_string(callbacks) + " variable(s) rappel rendue(s) (le nom de leur fonction)");
        for (auto& w : lost) lastRestore_->warnings.push_back(std::move(w));
        if (!persistPending_.empty())
            lastRestore_->notes.push_back(std::to_string(persistPending_.size()) + " d\xC3\xA9" "claration(s) Persistante(s) rendue(s) \xC3\xA0 la premi\xC3\xA8re ex\xC3\xA9" "cution de leur script");
        startData_.reset();
        const std::string label = startLabel_.empty() ? std::string("Donn\xC3\xA9" "es de simulation restaur\xC3\xA9" "es") : startLabel_;
        const std::string kind = startLabel_.empty() ? std::string("Simulation") : std::string("R\xC3\xA9manence");
        logAt(LogLevel::Info, kind, {}, label + " : " + lastRestore_->summary());
        for (const auto& w : lastRestore_->warnings) logAt(LogLevel::Warning, kind, {}, w);
    }
    // 1.12.3 : le poste reprend l'onglet choisi, les panneaux replies, le defilement.
    if (!startUi_.empty()) {
        for (const auto& s : startUi_)
            if (project_->view(s.view) && !s.key.empty()) overrides_[{s.view, s.object}][s.key] = s.value;
        startUi_.clear();
        composed_.clear(); boundCalls_.clear();
    }
    // 1.12.7 : les valeurs calculees au demarrage - apres les valeurs de depart (et la remanence), avant
    // les scripts de Demarrage et la premiere vue. Une erreur arrete le demarrage : la Console dit
    // laquelle (startError).
    if (!computeDeclarations()) {
        running_ = false;
        return;
    }
    log("Syst\xC3\xA8me", {}, "IHM d\xC3\xA9marr\xC3\xA9" "e : " + std::to_string(project_->programs.variables.size())
                                 + " variable(s) IHM, " + std::to_string(project_->programs.scripts.size())
                                 + " script(s) g\xC3\xA9n\xC3\xA9raux");
    // 1.13.0 (le debogueur) : les scripts de Demarrage puis la premiere vue, d'un seul travail - un point arme : sur une
    // tache du debogueur, un script de Demarrage qui s'arrete fige aussi la suite (la premiere vue, son OnOpen).
    std::vector<Id> boot;
    for (const auto& sc : project_->programs.scripts) {
        if (sc.event == "Demarrage") boot.push_back(sc.id);
        if (sc.event == "Cyclique") scriptNext_[sc.id] = now + std::max(10, sc.periodMs) / 1000.0;
    }
    const auto launch = [this, boot, now] {
        for (const Id scriptId : boot) {
            if (taskStopped() || !project_) return;
            for (const auto& sc : project_->programs.scripts)
                if (sc.id == scriptId) {
                    (void)runScript(sc, "script " + sc.name, now, nullptr);
                    break;
                }
        }
        if (taskStopped() || !project_ || !running_) return;
        Id first = project_->config.startView;
        // Lot 12 : l'utilisateur de depart ouvre la vue de demarrage de son groupe.
        if (const Id home = homeView(); home != kNoId && viewOf(home)) first = home;
        if (!viewOf(first) && !project_->views.empty()) first = project_->views.front().id;
        if (first != kNoId && viewOf(first)) {
            current_ = first;
            currentScope_ = buildScope(*viewOf(first), {});    // lot 8 : ses parametres par defaut
            log("Navigation", {}, "vue de d\xC3\xA9marrage : " + viewOf(first)->name);
            openView(first, now, false);
        }
    };
    if (unitWanted()) (void)debugUnit("d\xC3\xA9marrage de l'IHM", launch);
    else launch();
}

void Runtime::initVariables() {
    if (!project_) return;
    aggregates_.clear();
    rich_.clear();
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
        // 1.12.2 : un type objet (LIST, VECTOR, MAP, TUPLE) : un objet entier, a sa valeur initiale
        // (un litteral : [1, 2, 3], ['a' := 1], (1, 'x')) ; illisible : vide, et le journal le dit.
        if (const std::string form = types::callbackForm(project_, var.type); types::isRich(var.type) || !form.empty()) {   // 1.12.20 : T_Filtre
            std::string why;
            auto o = sim::makeRichValue(var.type, typeform::callbackInitial(form.empty() ? var.type : form, var.initial), *env_, &why);   // 1.12.19 : =Fonction
            if (!o && !var.initial.empty()) {
                log("Erreur", "variable " + var.name, "valeur initiale illisible : " + var.initial + " (" + frenchSimMessage(why) + ")");
                o = sim::makeRichValue(var.type, {}, *env_, &why);
            }
            if (o) rich_[upper(var.name)] = std::move(o);
            else log("Erreur", "variable " + var.name, frenchSimMessage(why));
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
        std::vector<types::ObjectMember> objects;
        std::string error;
        const auto leaves = types::leafVariables(*project_, var, &aggs, nullptr, &error, &objects);
        if (!error.empty()) {
            log("Erreur", "variable " + var.name, error);
            continue;
        }
        for (const auto& leaf : leaves) slot(leaf.name, leaf.type, leaf.initial, var.name);
        // 1.12.10 (lot F) : le litteral d'une structure, d'un tableau de structures - ce qui ne s'applique pas
        // (un membre inconnu, un texte pour un REAL, trop d'elements) garde la valeur du type, et se dit.
        if (!var.initial.empty())
            for (const auto& n : types::initialNotes(*project_, var)) logAt(LogLevel::Warning, "Variables", "variable " + var.name, n);
        // 1.12.4 : ses membres objets (Recette.Etapes : LIST OF STRING) - un objet chacun, a sa valeur
        // initiale (le litteral du type IHM) ; illisible : vide, et le journal le dit.
        for (const auto& m : objects) {
            std::string why;
            auto o = sim::makeRichValue(m.type, m.initial, *env_, &why);
            if (!o && !m.initial.empty()) {
                log("Erreur", "variable " + var.name, m.path + " : valeur initiale illisible : " + m.initial + " (" + frenchSimMessage(why) + ")");
                o = sim::makeRichValue(m.type, {}, *env_, &why);
            }
            if (o) rich_[upper(m.path)] = std::move(o);
            else log("Erreur", "variable " + var.name, m.path + " : " + frenchSimMessage(why));
        }
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
    // 1.12.4 : et ses membres objets (Four2.Etapes := Four1.Etapes), entiers.
    for (auto it = rich_.lower_bound(from); it != rich_.end() && it->first.compare(0, from.size(), from) == 0; ++it) {
        if (it->first.size() == from.size() || !it->second) continue;
        const char next = it->first[from.size()];
        if (next != '.' && next != '[') continue;
        if (const auto t2 = rich_.find(to + it->first.substr(from.size())); t2 != rich_.end() && t2->second) {
            std::string why;
            if (!sim::assignObj(*t2->second, *it->second, &why))
                env_->diagnostics.push_back({sim::Diagnostic::Severity::Error, target + " := " + source + " : " + frenchSimMessage(why), 0, {}});
        }
    }
    return true;
}

bool Runtime::hasRichUnder(std::string_view path) const {
    const std::string from = upper(path);
    for (auto it = rich_.lower_bound(from); it != rich_.end() && it->first.compare(0, from.size(), from) == 0; ++it)
        if (it->first.size() > from.size() && (it->first[from.size()] == '.' || it->first[from.size()] == '[')) return true;
    return false;
}

void Runtime::prime(double now) {
    composed_.clear(); boundCalls_.clear();
    now_ = startNow_ = now;
    startWallMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count();
    heldWallMs_ = 0;
    journal_.clear();
    errorCount_ = 0;
    lastError_.clear();
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
    gestureFormulas_.clear();
    backgrounds_.clear();
    gifs_.clear();                      // lot 16
    mediasReset();                      // 1.12.12 (lot H)
    docsReset();                        // 1.12.30
    converters_.clear();                // 1.14.1 (lot N5) : le Convertisseur d'unites
    explorers_.clear();                 // 1.14.1 (lot N5) : l'Explorateur de fichiers
    initVariables();
    // 1.12.7 : l'essai d'une fonction, le banc des expressions lisent aussi les constantes calculees.
    (void)computeDeclarations();
}

bool Runtime::runFunction(std::string_view name, const std::vector<std::pair<std::string, sim::Value>>& args,
                          sim::Value& result, std::string* why) {
    const auto* f = project_ ? project_->functionByName(name) : nullptr;
    if (!f) {
        if (why) *why = "fonction IHM '" + std::string(name) + "' introuvable";
        return false;
    }
    return runFunction(f->id, args, result, why);
}

bool Runtime::runFunction(Id id, const std::vector<std::pair<std::string, sim::Value>>& args, sim::Value& result,
                          std::string* why, std::vector<std::pair<std::string, sim::Value>>* outputs) {
    const auto* f = project_ ? project_->function(id) : nullptr;
    // 1.13.0 (tests unitaires) : une fonction d'une vue, d'une popup ou d'un symbole - Project::function ne voit que
    //  celles du projet. Celle d'une vue ou d'une popup se prepare comme son appel Vue.Fonction (ses constantes de
    //  vue qualifiees) ; celle d'un symbole tourne telle quelle (sans instance : ses parametres ne sont pas lies).
    HmiFunction ofView;
    if (!f && project_)
        for (const auto& v : project_->views) {
            for (const auto& vf : v.functions)
                if (vf.id == id) {
                    if (isSymbolView(v) || !viewmembers::bound(*project_, v.name + "." + vf.name + "#" + std::to_string(id), ofView))
                        ofView = vf;
                    f = &ofView;
                    break;
                }
            if (f) break;
        }
    if (!f) {
        if (why) *why = "fonction IHM introuvable";
        return false;
    }
    // Comme depuis un script : une faute (declaration, execution, appel
    // circulaire, argument) remonte ici au lieu d'etre dite une fois.
    const std::string before = abort_;
    abort_.clear();
    ++depth_;
    (void)callFunction(*f, args, result, outputs);
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
    // 1.14.0 (M4) : une relecture se quitte (l'etat d'avant revient), un enregistrement se garde.
    if (replayOn_) (void)quitReplay(now, "arr\xC3\xAAt de l'IHM");
    if (recordOn_ || (rec_ && recordingInfo().stopping)) (void)stopRecording(true, now);
    stopTasks();                                     // 1.12.26 : les scripts detaches s'arretent avec l'IHM
    clearDebugState();                               // 1.13.0 : une pause, un pas en cours : finis
    deferred_.clear();                               // 1.13.0 : les relachements gardes pendant une pause : oublies
    unregisterChoiceResolver(this);                  // 1.12.2
    if (!running_) return;
    now_ = std::max(now_, now);
    // 1.13.0 : les OnClose de l'arret vont au bout, sans s'arreter sur un point (l'IHM s'arrete).
    struct DebugOff {
        bool& on;
        bool  was;
        ~DebugOff() { on = was; }
    } debugOff{debugOn_, debugOn_};
    debugOn_ = false;
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
    if (hooks_.stopSounds) hooks_.stopSounds();          // 1.12.3 : l'arret coupe les sons qui jouent
    keysDown_.clear();                  // 1.11.23 : l'arret ne relache rien (aucune action ne part)
    animation_.reset();
    unfollowAll();                      // 1.9 : les liaisons ne suivent plus rien pour l'IHM
    timers_.clear();                    // 1.12.19 : les minuteurs d'IHM_APRES et IHM_CHAQUE s'arretent avec l'IHM
    watches_.clear();                   // 1.12.20 : les surveillances d'IHM_SUR_CHANGEMENT aussi
    nullSaid_.clear();
    fileSaid_.clear();
    pidKeepAll();                       // 1.12.24 : ce qu'ont appris les Reglages PID, dans l'historique
    pidWindow_ = PidWindow{};
}

void Runtime::press(Id object, double now) {
    if (paused_) return;                         // 1.13.0 : en pause (le debogueur), l'IHM est figee : le geste est ignore
    if (replayOn_) return;                       // 1.14.0 (M4) : une relecture ne recoit aucun clic
    // 1.13.0 : un relachement garde pendant une pause qui vient de finir part avant ce nouvel appui (dans l'ordre).
    if (!deferred_.empty() && !currentTask_) {
        replayDeferredInput(now);
        if (paused_) return;
    }
    // 1.13.0 : un point arme - le geste entier sur une tache du debogueur (une pause y fige la suite du geste).
    if (unitWanted()) { (void)debugUnit("Appui", [this, object, now] { press(object, now); }); return; }
    composed_.clear(); boundCalls_.clear();
    now_ = std::max(now_, now);
    lastActivity_ = now;
    pressed_ = object;
    pressStart_ = now;
    longFired_ = false;
    // Lot 9 : une impulsion ecrit des l'appui.
    if (running_ && object != kNoId)
        if (const auto* v = shownViewOf(object); v && v->object(object)) {
            View snapshot = *v;
            evaluateProps(snapshot.id, *snapshot.object(object));   // 1.12.6 (lot B) : tel que l'ecran le montre
            GestureGuard gesture(*this, where(snapshot, snapshot.object(object)), "Appui sur");     // lot 13 : l'audit
            controlPress(snapshot, *snapshot.object(object), now);
        }
}

void Runtime::release(Id object, double now, bool inside) {
    // 1.13.0 : en pause (le debogueur), l'IHM est figee : rien ne part tout de suite - mais l'objet appuye avant la pause
    // ne reste pas enfonce : le relachement est rejoue a la reprise, tel qu'il est arrive (sur l'objet : son clic, ses
    // actions Clic, comme sans debogueur ; dehors : l'impulsion retombe, sans clic).
    if (paused_) {
        if (object != kNoId && pressed_ == object)
            deferred_.push_back(DeferredInput{false, {}, object, inside});
        return;
    }
    if (replayOn_) return;                       // 1.14.0 (M4) : une relecture ne recoit aucun clic
    if (unitWanted()) { (void)debugUnit("Clic", [this, object, now, inside] { release(object, now, inside); }); return; }
    composed_.clear(); boundCalls_.clear();
    now_ = std::max(now_, now);
    const bool wasPressed = running_ && pressed_ == object && object != kNoId;
    const bool click = wasPressed && inside && !longFired_;
    pressed_ = kNoId;
    const auto* v = wasPressed ? shownViewOf(object) : nullptr;
    if (!(v ? v->object(object) : nullptr)) return;
    // 1.12.6 (lot B, § 2.7) : l'objet tel que l'ecran le montre - ses proprietes pilotees y portent
    // leur valeur (l'operation, l'impulsion inversee, la confirmation... suivent leur formule).
    View snapshot = *v;
    evaluateProps(snapshot.id, *snapshot.object(object));
    const Object* o = snapshot.object(object);
    GestureGuard gesture(*this, where(snapshot, snapshot.object(object)), "Clic sur");         // lot 13 : l'audit
    if (click && recordOn_) recordClick(rec::EventKind::Click, object);   // 1.14.0 (M4) : le clic, enregistre
    // Lot 13 : une commande a signature attend le panneau - la bascule avant de
    // basculer, le bouton apres sa confirmation.
    // 1.12.24 : le Reglage PID signe ce qu'il ecrit (Appliquer, Revenir), pas l'ouverture de sa fenetre.
    const bool signs = click && o->kind != Kind::PidTuner && signatureNeeded(snapshot, *snapshot.object(object));
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
    // 1.12.24 : le Reglage PID ouvre sa fenetre (Au clic : ouvrir ses statistiques).
    if (o->kind == Kind::PidTuner && shownText(snapshot.id, *o, "pidClick", "ouvrir").rfind("ouvrir", 0) == 0)
        pidOpen(snapshot.id, object, 0, now);
    // Lot 10 : l'objet Parametres systeme ouvre le menu natif, sur son onglet.
    if (o->kind == Kind::SystemButton) {
        const Object& so = *snapshot.object(object);
        std::string why;
        if (!objectAllowed(so, &why)) {
            event("Acc\xC3\xA8s refus\xC3\xA9", where(snapshot, &so), why);
            return;
        }
        openSystemMenu(systemTabFrom(plain(shownText(snapshot.id, so, "tab"))), now, where(snapshot, &so));   // 1.9 : et Simulation ; 1.12.5 : sa formule
    }
    // 1.14.0 : le Coupe-son coupe ou retablit tous les sons (ou ceux de son canal).
    if (o->kind == Kind::SoundMute) soundMuteClick(snapshot, *snapshot.object(object));
    // Lot 12 : l'objet Menu de connexion ouvre le menu natif, sur son onglet.
    if (o->kind == Kind::LoginMenuButton) {
        const Object& lo = *snapshot.object(object);
        std::string why;
        if (!objectAllowed(lo, &why)) {
            event("Acc\xC3\xA8s refus\xC3\xA9", where(snapshot, &lo), why);
            return;
        }
        openLoginMenu(loginTabFrom(shownText(snapshot.id, lo, "tab", "Connexion")), now, where(snapshot, &lo));   // 1.12.5 : sa formule
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
        // 1.12.5 (lot A, D3) : la source, le nom, le format et la question tels que l'ecran
        // les montre - leur formule evaluee (avant : la valeur fixe, la formule ignoree).
        (void)exportData(shownText(snapshot.id, eo, "exportSource", "alarmes"), shownText(snapshot.id, eo, "fileName", "export_{SYS.Date}"),
                         shownText(snapshot.id, eo, "fileFormat", "CSV"), now, where(snapshot, &eo), &why,
                         shownFlag(snapshot.id, eo, "askWhere", true));
    }
    if (o->kind == Kind::AlarmBanner)
        if (const int k = bannerAlarm(snapshot.id, *snapshot.object(object)); k >= 0) selectAlarm(alarms_[static_cast<std::size_t>(k)].name);
    runActions(snapshot, snapshot.object(object), Trigger::Click, now);
    // 1.12.13 : un tableau - la case cliquee (ses actions, la ligne choisie, la saisie R/W).
    if (o->kind == Kind::Table) tableGesture(snapshot, *snapshot.object(object), Trigger::CellClick, now);
}

void Runtime::doubleClick(Id object, double now) {
    if (paused_) return;                         // 1.13.0 : en pause (le debogueur), l'IHM est figee : le geste est ignore
    if (replayOn_) return;                       // 1.14.0 (M4)
    if (unitWanted()) { (void)debugUnit("Double-clic", [this, object, now] { doubleClick(object, now); }); return; }
    composed_.clear(); boundCalls_.clear();
    now_ = std::max(now_, now);
    const auto* v = shownViewOf(object);
    const auto* o = v ? v->object(object) : nullptr;
    if (!running_ || !o) return;
    View snapshot = *v;
    evaluateProps(snapshot.id, *snapshot.object(object));   // 1.12.6 (lot B) : tel que l'ecran le montre
    GestureGuard gesture(*this, where(snapshot, snapshot.object(object)), "Double-clic sur");         // lot 13 : l'audit
    if (recordOn_) recordClick(rec::EventKind::DoubleClick, object);   // 1.14.0 (M4)
    runActions(snapshot, snapshot.object(object), Trigger::DoubleClick, now);
    // 1.13.0 (le debogueur) : la copie, pas l'objet de la vue - les actions ont pu s'arreter (une pause), et la vue changer
    // pendant ce temps (un objet ajoute : l'objet d'avant n'est plus a sa place).
    if (snapshot.object(object)->kind == Kind::Table) tableGesture(snapshot, *snapshot.object(object), Trigger::CellDoubleClick, now);   // 1.12.13
}

void Runtime::watchTriggers(const View& view, double now, bool cycleTick) {
    // 1.13.0 (le debogueur) : la copie seulement, de bout en bout - une action peut s'arreter au milieu (la suite
    // attend la reprise), et la vue recue (une vue composee du moteur) ne doit pas pendre apres la pause.
    const View snapshot = view;
    const View& v = snapshot;
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
            if (taskStopped()) { closed = true; return; }
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
    check(nullptr, snapshot.actions);
    for (const auto& o : snapshot.objects) {
        if (closed) break;
        if (!o.actions.empty()) check(&o, o.actions);
    }
    actionOrigin_.clear();
}

void Runtime::cycle(double now) {
    if (!project_) return;
    // 1.12.17 : ou passe le temps - chaque script (runScript le dit), les alarmes, les courbes et
    // archives, le reste.
    auto lapStart = std::chrono::steady_clock::now();
    std::int64_t lapPaused = pausedNs_;          // 1.13.0 : une pause du debogueur ne compte dans aucun poste
    const auto lap = [this, &lapStart, &lapPaused] {
        const auto t = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t - lapStart).count() - static_cast<double>(pausedNs_ - lapPaused) / 1e6;
        lapStart = t;
        lapPaused = pausedNs_;
        return std::max(0.0, ms);
    };
    gifsCycle(now);                     // lot 16 : les GIF animes (conditions, fin de lecture)
    mediasCycle(now);                   // 1.12.12 (lot H) : les videos, les textes defilants, les codes QR
    docsCycle(now);                     // 1.12.30 : les visionneuses de documents (document, page)
    converterCycle(now);                // 1.14.1 (lot N5) : les convertisseurs d'unites (leur Valeur de depart)
    explorersCycle(now);                // 1.14.1 (lot N5) : les explorateurs (leur source, lue quand elle change)
    double rest = lap();
    double scripted = 0;
    for (const auto& part : cycleParts_) scripted += part.ms;
    // Les scripts generaux : cycliques a leur periode, "sur changement" quand
    // leur expression change. 1.13.0 (le debogueur) : par leur identifiant - un script peut s'arreter au milieu (la
    // suite du cycle attend la reprise), et la liste du projet changer pendant la pause.
    std::vector<Id> general;
    general.reserve(project_->programs.scripts.size());
    for (const auto& sc : project_->programs.scripts) general.push_back(sc.id);
    for (const Id scriptId : general) {
        if (taskStopped()) return;
        const Script* found = nullptr;
        for (const auto& s : project_->programs.scripts)
            if (s.id == scriptId) found = &s;
        if (!found) continue;
        const Script& sc = *found;
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
        if (taskStopped()) return;
        const auto* v = viewOf(id);
        if (!v) continue;
        runViewScript(*v, "OnCycle", now);
        if ((v = viewOf(id)) == nullptr) continue;
        if (id != current_ && std::find(popups_.begin(), popups_.end(), id) == popups_.end()) continue;
        watchTriggers(*v, now, true);
    }
    if (taskStopped()) return;
    {
        double nowScripted = 0;
        for (const auto& part : cycleParts_) nowScripted += part.ms;
        rest += std::max(0.0, lap() - (nowScripted - scripted));     // les scripts ont deja leur ligne
    }
    evaluateAlarms(now);
    alarmsCycle(now);        // lot 11 : la fin des mises de cote, les sons repetes
    cyclePost("alarmes", "Alarmes (" + std::to_string(project_->alarms.size()) + " conditions)", lap());
    sampleTrends(now);
    sampleCharts(now);       // lot 11 : chronogrammes, courbes XY, histogrammes
    sampleArchive(now);
    cyclePost("courbes", "Courbes et archives", lap());
    controlsCycle(now);      // lot 9 : compteurs horaires, fleches de tendance, programmateurs
    zoneMapsCycle();         // 1.12.12 (lot H) : la zone choisie par un programme
    productionCycle(now);    // lot 11 : compteurs de production
    pidCycle(now);           // 1.12.24 : les Reglages PID
    editorsCycle();          // lot 11 : les valeurs de l'installation, pour les editeurs de recette
    reportsCycle(now);       // lot 14 : les rapports periodiques a leur heure
    cyclePost("reste", "Le reste (compteurs, horloges, m\xC3\xA9" "dias, rapports)", rest + lap());
    ++cycles_;               // lot 9 : SYS.CycleCount
}

void Runtime::tick(double now) {
    if (!running_ || !project_) return;
    // 1.14.0 (M4) : UNE RELECTURE - le moteur est mis de cote (ni scripts, ni actions, ni alarmes, ni equipements) ;
    // replayTick fait avancer la piste.
    if (replayOn_) return;
    // 1.13.0 : EN PAUSE dans un script (le debogueur), l'IHM est figee : le prochain cycle attend la reprise.
    if (paused_) return;
    if (!currentTask_) {
        debugTick(now);
        // Les relachements arrives pendant la pause qui vient de finir, d'abord (dans l'ordre).
        replayDeferredInput(now);
        if (paused_) return;
        // Un point arme (un pas, une pause demandee) : le tick entier sur une tache du debogueur - un script qui s'y
        // arrete fige aussi la suite (les autres scripts du cycle, les declencheurs, les alarmes, les courbes).
        if (unitWanted()) {
            const bool done = debugUnit("IHM", [this, now] { tick(now); });
            if (done && !paused_ && !tasks_.empty()) tasksTick(now);    // les scripts detaches : par le fil de l'IHM
            return;
        }
    }
    composed_.clear(); boundCalls_.clear();                  // le projet a pu changer depuis le dernier appel
    jokerViews_.clear();                                     // 1.12.7 (lot E) : et ses jokers
    linkViews_.clear();                                      // 1.12.11 (lot G) : et ses liaisons Equipement
    rangeViews_.clear();                                     // 1.12.10 (lot F) : et ses plages
    refreshBound();                     // lot 15 : les variables liees a un equipement
    now_ = std::max(now_, now);
    followWatched(now, false);          // 1.9 : les variables des alarmes, suivies en permanence
    slaveWatch(now);                    // 1.9 : les bascules vers les esclaves simules
    if (animation_ && now - animation_->start >= std::max(1, animation_->spec.durationMs) / 1000.0) animation_.reset();
    // 1.12.6 (lot B) : une formule qui change reprend la main sur le geste fait a cote d'elle.
    if (!gestureFormulas_.empty()) releaseGestures();
    // Lot 9 : le bouton a confirmation par appui maintenu.
    holdTick(now);
    keysTick(now);                      // 1.11.23 : les raccourcis maintenus et repetes
    // L'appui long : une fois par appui, quand sa duree est atteinte.
    if (pressed_ != kNoId && !longFired_) {
        if (const auto* v = shownViewOf(pressed_)) {
            if (const auto* o = v->object(pressed_)) {
                int shortest = -1;
                for (const auto& a : o->actions)
                    if (a.trigger == Trigger::LongPress || a.trigger == Trigger::CellLongPress) {   // 1.12.13 : une case tenue aussi
                        const int d = a.delayMs > 0 ? a.delayMs : 800;
                        shortest = shortest < 0 ? d : std::min(shortest, d);
                    }
                if (shortest >= 0 && now - pressStart_ >= shortest / 1000.0) {
                    longFired_ = true;
                    const View snapshot = *v;
                    const Id held = pressed_;
                    GestureGuard gesture(*this, where(snapshot, snapshot.object(held)), "Appui long sur");   // lot 13 : l'audit
                    if (recordOn_) recordClick(rec::EventKind::LongPress, held);   // 1.14.0 (M4)
                    runActions(snapshot, snapshot.object(held), Trigger::LongPress, now);
                    if (const Object* t = snapshot.object(held); t && t->kind == Kind::Table) tableGesture(snapshot, *t, Trigger::CellLongPress, now);
                }
            }
        }
    }
    if (taskStopped()) return;                // 1.13.0 : arrete pendant une pause : la suite ne part pas
    if (!timers_.empty()) timersTick(now);   // 1.12.19 : IHM_APRES, IHM_CHAQUE
    if (!watches_.empty()) watchesTick();    // 1.12.20 : IHM_SUR_CHANGEMENT
    // 1.12.26 : les scripts detaches (leur reveil, leur tranche). 1.13.0 : par le fil de l'IHM seulement (dans une tache
    // du debogueur, apres elle).
    if (!tasks_.empty() && !currentTask_) tasksTick(now);
    // Les timers, a chaque appel ; le reste au rythme du cycle IHM.
    std::vector<Id> shown{current_};
    shown.insert(shown.end(), popups_.begin(), popups_.end());
    for (const Id id : shown)
        if (const auto* v = viewOf(id)) watchTriggers(*v, now, false);
    if (taskStopped()) return;
    const double period = std::max(10, project_->config.cycleMs) / 1000.0;
    if (lastCycle_ < 0 || now - lastCycle_ >= period - 1e-9) {
        lastCycle_ = now;
        // Lot 13 : le travail du cycle, mesure ; plus long que la periode : un depassement.
        const auto t0 = std::chrono::steady_clock::now();
        const std::int64_t paused0 = pausedNs_;          // 1.13.0 : sans le temps d'une pause du debogueur
        cycleParts_.clear();
        inCycle_ = true;
        cycle(now);
        inCycle_ = false;
        if (taskStopped()) return;
        const double ms = std::max(0.0, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()
                                            - static_cast<double>(pausedNs_ - paused0) / 1e6);
        perf_.cycle.add(ms);
        perf_.periodMs = period * 1000.0;
        if (ms > perf_.periodMs) ++perf_.overruns;
        // 1.12.17 : dans le temps ; un cycle plus long que sa periode est toujours une lenteur,
        // ses postes les plus longs dedans.
        perfHistory_.cycle(now, ms, perf_.periodMs);
        const double usualCycle = usual_.usual("cycle");
        usual_.add("cycle", ms, ms > perf_.periodMs);
        if (ms > perf_.periodMs) {
            SlowEntry e;
            e.kind = SlowKind::Cycle;
            e.ms = ms;
            e.usual = usualCycle;
            e.period = perf_.periodMs;
            e.what = "D\xC3\xA9passement : le cycle a pass\xC3\xA9 sa p\xC3\xA9riode";
            e.where = "IHM";
            e.runs = usual_.runs("cycle");
            e.slowRuns = usual_.slowRuns("cycle");
            auto parts = cycleParts_;
            std::stable_sort(parts.begin(), parts.end(), [](const SlowPart& a, const SlowPart& b) { return a.ms > b.ms; });
            if (parts.size() > 8) parts.resize(8);
            e.inside = std::move(parts);
            slowNote(std::move(e));
        }
    }
    perfHistory_.advance(now);          // 1.12.17 : les secondes passees se ferment
    sampleExchanges(now);               // ... et les echanges, une fois par seconde
    if (recordOn_) recordTick(now);     // 1.14.0 (M4) : une prise de l'enregistrement, a son heure
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
    while (closed_.size() > journalCap()) closed_.pop_front();
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
    if (paused_) {                               // 1.13.0 : en pause (le debogueur), l'IHM est figee
        if (why) *why = "l'IHM est en pause (d\xC3\xA9" "bogage) : Continuer (F5) d'abord";
        return 0;
    }
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
    if (paused_) {                               // 1.13.0 : en pause (le debogueur), rien ne s'ecrit
        if (why) *why = "l'IHM est en pause (d\xC3\xA9" "bogage) : Continuer (F5) d'abord";
        return false;
    }
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
    // 1.14.0 (relecture M4b) : une relecture affichee - une recette n'ecrit rien.
    if (replayOn_) {
        event("Recette", r->name, label + " refus\xC3\xA9 : une relecture est affich\xC3\xA9" "e (l'IHM ne commande rien) ; rien n'est \xC3\xA9" "crit");
        return fail("une relecture est affich\xC3\xA9" "e : rien ne s'\xC3\xA9" "crit");
    }
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
            } else if (sim::isDateType(current.type())) {
                rq.values[i] = current.display();      // 1.12.5 (D8) : D#2026-10-09, TOD#, DT# - se relit
            } else {
                rq.values[i] = exactValue(current);    // 1.12.5 (D4) : sans perte (avant : 6 chiffres)
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
    if (core::hasApi()) out.emplace_back("Automate", plc_ ? "simulateur reli\xC3\xA9" : "aucun (variables IHM seules)");   // 1.12.2
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
    if (paused_) return;                         // 1.13.0 : en pause (le debogueur), l'IHM est figee
    if (unitWanted()) { (void)debugUnit("D\xC3\xA9" "connexion", [this, now, r = std::string(reason)] { logout(now, r); }); return; }
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

bool Runtime::setTrendPaused(Id view, Id object, bool paused) {
    const View* v = viewOf(view);
    const Object* o = pilotableTrend(v, object);   // 1.13.1 : pas en mode historique
    if (!o) return false;
    const std::string key = trendKeyOf(view, object);
    if (paused) {
        if (!trendPaused_.count(key)) trendPaused_[key] = now_;
    } else {
        trendPaused_.erase(key);
        // 1.13.1 : Continuer revient au present (le zoom du temps reste, Tout voir l'enleve).
        if (const auto st = trendStates_.find(key); st != trendStates_.end()) st->second.pinned = false;
    }
    event(paused ? "Courbe en pause" : "Courbe reprise", v->name + "." + o->name, paused ? "affichage fig\xC3\xA9 (les mesures continuent)" : "temps r\xC3\xA9" "el");
    return true;
}

// ---- 1.13.1 : la courbe en marche - figer, la fenetre, les curseurs, l'export de la zone ----------------------
std::string Runtime::trendKeyOf(Id view, Id object) const {
    return std::to_string(view) + ":" + std::to_string(object) + argumentsKey(view);
}

// Une courbe pilotable de la vue (nulle : pas une courbe, pas pilotable, ou - 1.13.1, la contre-verification - en mode
// historique : elle relit un fichier, ni pause ni gel ni curseurs ni zoom ; son mode tel que l'ecran le montre).
const Object* Runtime::pilotableTrend(const View* v, Id object) const {
    const Object* o = v ? v->object(object) : nullptr;
    if (!o || o->kind != Kind::Trend || !o->flag("pilotable", false)) return nullptr;
    if (trimText(shownText(v->id, *o, "mode", "temps r\xC3\xA9" "el")).rfind("historique", 0) == 0) return nullptr;
    return o;
}

bool Runtime::setTrendFrozen(Id view, Id object, bool frozen) {
    const View* v = viewOf(view);
    const Object* o = pilotableTrend(v, object);
    if (!o) return false;
    auto& st = trendStates_[trendKeyOf(view, object)];
    if (st.frozen == frozen) return true;
    if (frozen) {
        st.frozen = true;
        st.frozenAt = now_;
    } else {
        st.frozen = false;
        if (now_ > st.frozenAt) st.gaps.emplace_back(st.frozenAt, now_);   // le temps du gel : un trou
        st.pinned = false;                                                  // Reprendre revient au present
    }
    event(frozen ? "Courbe fig\xC3\xA9" "e" : "Courbe d\xC3\xA9" "fig\xC3\xA9" "e", v->name + "." + o->name,
          frozen ? "acquisition arr\xC3\xAAt\xC3\xA9" "e (aucune mesure n'entre, aucune ne tombe)"
                 : "acquisition reprise (un trou pour le temps du gel)");
    return true;
}

const TrendState* Runtime::trendState(Id view, Id object) const {
    const auto it = trendStates_.find(trendKeyOf(view, object));
    return it == trendStates_.end() ? nullptr : &it->second;
}

double Runtime::trendEdge(Id view, Id object) const {
    const std::string key = trendKeyOf(view, object);
    double edge = now_;
    if (const auto p = trendPaused_.find(key); p != trendPaused_.end()) edge = std::min(edge, p->second);
    if (const auto st = trendStates_.find(key); st != trendStates_.end() && st->second.frozen) edge = std::min(edge, st->second.frozenAt);
    return edge;
}

std::pair<double, double> Runtime::trendWindowOf(Id view, Id object, double duration) const {
    return hmi::trendWindow(trendState(view, object), duration, trendEdge(view, object));
}

namespace {
// La duree de la courbe telle qu'elle se montre (sa formule evaluee), comme le dessin.
double shownDuration(const Object& evaluated) {
    double d = 60;
    const std::string t = evaluated.text("duration");
    if (!t.empty() && !roles::readNumber(t, d)) d = 60;
    return std::max(1.0, d);
}
} // namespace

bool Runtime::trendZoom(Id view, Id object, double factor, double at) {
    const View* v = viewOf(view);
    const Object* raw = pilotableTrend(v, object);
    if (!raw) return false;
    Object o = *raw;
    evaluateProps(view, o);
    const std::string key = trendKeyOf(view, object);
    auto& st = trendStates_[key];
    const bool moving = !st.frozen && !trendPaused_.count(key);
    hmi::trendZoomTime(st, factor, at, shownDuration(o), trendEdge(view, object), moving);
    return true;
}

bool Runtime::trendZoomValueAxis(Id view, Id object, double factor, double at, double lo, double hi) {
    if (!pilotableTrend(viewOf(view), object)) return false;
    return hmi::trendZoomValues(trendStates_[trendKeyOf(view, object)], factor, at, lo, hi);
}

bool Runtime::trendPanBy(Id view, Id object, double dt, double dv) {
    const View* v = viewOf(view);
    const Object* raw = pilotableTrend(v, object);
    if (!raw) return false;
    Object o = *raw;
    evaluateProps(view, o);
    const double edge = trendEdge(view, object);
    double oldest = edge;
    if (const auto* list = trend(view, object))
        for (const auto& s : *list)
            if (!s.points.empty()) oldest = std::min(oldest, s.points.front().first);
    hmi::trendPan(trendStates_[trendKeyOf(view, object)], dt, dv, shownDuration(o), edge, oldest);
    return true;
}

bool Runtime::trendResetView(Id view, Id object) {
    if (!pilotableTrend(viewOf(view), object)) return false;
    hmi::trendShowAll(trendStates_[trendKeyOf(view, object)]);
    return true;
}

bool Runtime::setTrendCursors(Id view, Id object, std::optional<double> a, std::optional<double> b) {
    if (!pilotableTrend(viewOf(view), object)) return false;
    auto& st = trendStates_[trendKeyOf(view, object)];
    st.cursorA = a;
    st.cursorB = b;
    return true;
}

bool Runtime::toggleTrendCursors(Id view, Id object) {
    const View* v = viewOf(view);
    const Object* raw = pilotableTrend(v, object);
    if (!raw) return false;
    Object o = *raw;
    evaluateProps(view, o);
    const auto [start, end] = trendWindowOf(view, object, shownDuration(o));
    hmi::trendToggleCursors(trendStates_[trendKeyOf(view, object)], start, end);
    return true;
}

std::vector<TrendReading> Runtime::trendCursorReadings(Id view, Id object) const {
    const View* v = viewOf(view);
    const Object* raw = pilotableTrend(v, object);
    const auto* st = trendState(view, object);
    const auto* list = trend(view, object);
    if (!raw || !st || !list) return {};
    Object o = *raw;
    evaluateProps(view, o);
    return hmi::trendReadings(o, *list, *st);
}

std::string Runtime::clockText(double at) const {
    const std::string stamp = dateStampOf(at);
    return stamp.size() > 11 ? stamp.substr(11) : stamp;
}

bool Runtime::exportTrendZone(Id view, Id object, std::string_view format, double now, bool askWhere, std::string* why) {
    now_ = std::max(now_, now);
    const View* v = viewOf(view);
    const Object* o = pilotableTrend(v, object);
    if (!o) {
        if (why) *why = "pas une courbe pilotable de la vue";
        return false;
    }
    const auto* st = trendState(view, object);
    if (!st || !st->cursors()) {
        if (why) *why = "pose d'abord les deux curseurs (le bouton Curseurs) : la zone est entre eux";
        return false;
    }
    GestureGuard gesture(*this, where(*v, o), "Clic sur");
    return exportData("zone:" + o->name, o->name + "_zone", format, now, "Op\xC3\xA9rateur", why, askWhere);
}

// La barre et les gestes d'une courbe pilotable : x, y dans l'objet (pixels de la vue).
void Runtime::trendObjectPart(const View& v, const Object& raw, std::string_view part, double now) {
    if (!pilotableTrend(&v, raw.id)) return;   // 1.13.1 : ni pas pilotable, ni en mode historique
    if (part == "figer" || part == "curseurs" || part == "toutvoir") {
        GestureGuard gesture(*this, where(v, &raw), "Clic sur");
        const std::string key = trendKeyOf(v.id, raw.id);
        if (part == "figer") {
            const auto it = trendStates_.find(key);
            (void)setTrendFrozen(v.id, raw.id, !(it != trendStates_.end() && it->second.frozen));
        } else if (part == "curseurs") {
            (void)toggleTrendCursors(v.id, raw.id);
        } else {
            (void)trendResetView(v.id, raw.id);
        }
        return;
    }
    if (part == "zone:csv" || part == "zone:excel") {
        std::string why;
        if (!exportTrendZone(v.id, raw.id, part == "zone:csv" ? "CSV" : "Excel", now, true, &why))
            event("Export", v.name + "." + raw.name, "zone refus\xC3\xA9" "e : " + why);
        return;
    }
    if (part.rfind("trend:", 0) != 0) return;
    // "trend:<geste>:a:b:..." - les nombres qui suivent le geste.
    std::vector<std::string> bits;
    for (std::size_t from = 0;;) {
        const auto at = part.find(':', from);
        bits.emplace_back(part.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from));
        if (at == std::string_view::npos) break;
        from = at + 1;
    }
    std::vector<double> n;
    for (std::size_t i = 2; i < bits.size(); ++i) {
        double x = 0;
        if (!parseNumber(bits[i], x)) return;
        n.push_back(x);
    }
    const std::string gesture = bits.size() > 1 ? bits[1] : std::string{};
    Object o = raw;
    evaluateProps(v.id, o);
    const std::string key = trendKeyOf(v.id, raw.id);
    auto& st = trendStates_[key];
    const double duration = shownDuration(o);
    const double edge = trendEdge(v.id, raw.id);
    const auto layout = hmi::trendLayout(o, o.number("w"), o.number("h"), st.cursors());
    const auto& plot = layout.plot;
    const auto window = hmi::trendWindow(&st, duration, edge);
    const double start = window.first, end = window.second;
    const auto* list = trend(v.id, raw.id);
    const auto toTime = [&](double x) { return start + (x - plot.x) / std::max(1.0, plot.w) * (end - start); };
    // L'echelle montree : celle que l'ecran a dessinee (lo, hi), sinon celle du moteur.
    const auto scale = [&](std::size_t at) {
        if (st.valueZoom) return std::pair<double, double>{st.lo, st.hi};
        if (n.size() >= at + 2 && n[at + 1] > n[at]) return std::pair<double, double>{n[at], n[at + 1]};
        return hmi::trendScale(o, list, start, end, &st);
    };
    if (gesture == "press" && n.size() >= 2) {
        GestureGuard guard(*this, where(v, &raw), "Clic sur");
        const int near = hmi::trendCursorNear(st, n[0], plot, start, end);
        st.grab = near ? near : 3;
        st.pressX = st.lastX = n[0];
        st.pressY = st.lastY = n[1];
        st.moved = false;
    } else if (gesture == "drag" && n.size() >= 2) {
        if (!st.grab) return;
        if (!st.moved && std::fabs(n[0] - st.pressX) + std::fabs(n[1] - st.pressY) < 3) return;   // un clic qui tremble
        st.moved = true;
        if (st.grab == 1 || st.grab == 2) {
            // Le curseur suit la souris (de ce qu'elle a bouge : attrape a 3 px de sa ligne, il ne saute pas).
            auto& cursor = st.grab == 1 ? st.cursorA : st.cursorB;
            const double from = cursor ? *cursor : toTime(st.lastX);
            cursor = std::clamp(from + (n[0] - st.lastX) / std::max(1.0, plot.w) * (end - start), start, end);
        } else {
            const auto [lo, hi] = scale(2);
            const double dt = -(n[0] - st.lastX) / std::max(1.0, plot.w) * (end - start);
            const double dv = (n[1] - st.lastY) / std::max(1.0, plot.h) * (hi - lo);
            double oldest = edge;
            if (list)
                for (const auto& s : *list)
                    if (!s.points.empty()) oldest = std::min(oldest, s.points.front().first);
            hmi::trendPan(st, dt, dv, duration, edge, oldest);
        }
        st.lastX = n[0];
        st.lastY = n[1];
    } else if (gesture == "release") {
        const bool click = st.grab != 0 && !st.moved;
        st.grab = 0;
        st.moved = false;
        // Un clic (sans glisser) dans la zone de trace pose un curseur : A, puis B, puis le plus proche.
        if (click && n.size() >= 2 && plot.contains(n[0], n[1])) hmi::trendPlaceCursor(st, std::clamp(toTime(n[0]), start, end));
    } else if (gesture == "wheel" && n.size() >= 4) {
        // Un cran vers le haut rapproche (x 0,8), vers le bas eloigne ; Maj (axe 1) : les valeurs.
        const double factor = std::pow(0.8, std::clamp(n[0], -10.0, 10.0));
        if (n[3] >= 0.5) {
            const auto [lo, hi] = scale(4);
            const double at = hi - (n[2] - plot.y) / std::max(1.0, plot.h) * (hi - lo);
            (void)hmi::trendZoomValues(st, factor, at, lo, hi);
        } else {
            const bool moving = !st.frozen && !trendPaused_.count(key);
            hmi::trendZoomTime(st, factor, toTime(n[1]), duration, edge, moving);
        }
    }
}

// F, C, Debut sur la courbe sous la souris - sinon la seule courbe pilotable montree.
bool Runtime::trendKey(const keys::Chord& chord) {
    if (chord.ctrl || chord.alt || chord.shift) return false;
    if (chord.key != "F" && chord.key != "C" && chord.key != "Home") return false;
    Id view = kNoId, object = kNoId;
    if (pointer_.view != kNoId && !pointer_.objectName.empty())
        if (const View* v = viewOf(pointer_.view)) {
            const std::string name = pointer_.objectName.substr(pointer_.objectName.find('.') + 1);
            for (const auto& o : v->objects)
                if (o.name == name && pilotableTrend(v, o.id)) { view = v->id; object = o.id; }
        }
    if (object == kNoId) {
        std::vector<Id> shown{current_};
        shown.insert(shown.end(), popups_.begin(), popups_.end());
        int found = 0;
        for (const Id id : shown)
            if (const View* v = viewOf(id))
                for (const auto& o : v->objects)
                    if (pilotableTrend(v, o.id)) { ++found; view = id; object = o.id; }
        if (found != 1) return false;
    }
    const View* v = viewOf(view);
    const Object* o = v ? v->object(object) : nullptr;
    if (!o) return false;
    trendObjectPart(*v, *o, chord.key == "F" ? "figer" : chord.key == "C" ? "curseurs" : "toutvoir", now_);
    return true;
}

std::optional<double> Runtime::trendPausedAt(Id view, Id object) const {
    const auto it = trendPaused_.find(trendKeyOf(view, object));
    return it == trendPaused_.end() ? std::nullopt : std::optional<double>(it->second);
}

const std::vector<TrendSeries>* Runtime::trend(Id view, Id object) const {
    const auto it = trends_.find(trendKeyOf(view, object));
    return it == trends_.end() ? nullptr : &it->second;
}

std::string Runtime::argumentsKey(Id view) const {
    const View* v = project_ ? project_->view(view) : nullptr;
    if (!v || v->params.empty()) return {};
    const Scope* s = viewScope(view);
    if (!s) return {};
    std::vector<std::string> names = s->names();
    std::sort(names.begin(), names.end());
    std::string out;
    for (const auto& n : names) {
        out += "|" + n + "=";
        if (const auto* a = s->alias(n)) out += *a;
        else if (const auto* x = s->value(n)) out += x->display();
    }
    return out;
}

std::vector<Id> Runtime::samplingPlan(double now) {
    // 1.14.0 (relecture M4b) : une relecture refait les courbes des vues montrees seulement (la piste ne dit rien des
    // autres) - le plan du direct ne bouge pas.
    if (replayOn_) {
        std::vector<Id> shown{current_};
        for (const Id p : popups_)
            if (std::find(shown.begin(), shown.end(), p) == shown.end()) shown.push_back(p);
        return shown;
    }
    if (project_ && (now - samplePlanAt_ >= 1.0 || now < samplePlanAt_)) {
        samplePlanAt_ = now;
        samplePlan_.clear();
        for (const auto& v : project_->views) {
            const bool navigable = v.role == "vue" || (v.role == "popup" && v.params.empty());
            if (!navigable) continue;
            // Seulement celles qui ont de quoi enregistrer (une courbe, un graphique en memoire) - la vue
            // composee : son ecran modele, ses instances de symbole comptent.
            const View* c = viewOf(v.id);
            if (!c) continue;
            // 1.12.5 (lot A, D10) : un compteur de production aussi (il compte en fond).
            const bool records = std::any_of(c->objects.begin(), c->objects.end(), [](const Object& o) {
                return o.kind == Kind::Trend || o.kind == Kind::StateChart || o.kind == Kind::XYChart || o.kind == Kind::Histogram
                    || o.kind == Kind::ProductionCounter;
            });
            if (records) samplePlan_.push_back(v.id);
        }
    }
    std::vector<Id> out{current_};
    for (const Id p : popups_)
        if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    for (const Id p : samplePlan_)
        if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    return out;
}

void Runtime::sampleTrends(double now) {
    for (const Id id : samplingPlan(now)) {
        const auto* v = viewOf(id);
        if (!v) continue;
        const auto aliases = viewAliases(id);   // 1.12.3 : une plume qui cite un parametre de la vue (Cuve.Niveau)
        for (const auto& so : v->objects) {
            if (so.kind != Kind::Trend) continue;
            // 1.12.6 (lot B, § 2.7) : la courbe telle que l'ecran la montre - ses plumes, son mode, sa duree
            // selon leurs formules (avant : les valeurs fixes).
            Object o = so;
            evaluateProps(id, o);
            const std::string key = std::to_string(id) + ":" + std::to_string(o.id) + argumentsKey(id);   // 1.12.3 : par jeu d'arguments
            const std::string mode = o.text("mode", "temps r\xC3\xA9" "el");
            if (trimText(mode).rfind("historique", 0) == 0) {
                // 1.13.1 (contre-verification) : passee en historique, rien a piloter - ni gel, ni pause, ni zoom ne restent.
                trendStates_.erase(key);
                trendPaused_.erase(key);
                continue;
            }
            // 1.12.6 (lot C) : le decoupeur unique - une plume M['a;b'] ou $A;B$ reste entiere.
            std::vector<std::string> pens;
            for (auto& pen : roles::splitList(o.text("variables")))
                if (!pen.empty()) pens.push_back(std::move(pen));
            auto& series = trends_[key];
            bool same = series.size() == pens.size();
            for (std::size_t i = 0; same && i < pens.size(); ++i) same = series[i].expression == pens[i];
            if (!same) {
                series.clear();
                for (const auto& p : pens) series.push_back(TrendSeries{p, {}});
            }
            // 1.12.16 : la condition de chaque plume (penVisible) - fausse, elle se cache (et se mesure quand meme).
            //  (Les parametres de la vue sont deja en place : `aliases`.)
            {
                const auto conds = roles::splitList(o.text("penVisible"));
                for (std::size_t i = 0; i < series.size(); ++i) {
                    const std::string c = i < conds.size() ? trimText(conds[i]) : std::string{};
                    series[i].visible = c.empty() || evalBool(c, true);
                }
            }
            // 1.13.1 : Pilotable decochee en marche : plus de barre pour reprendre - ni gel, ni pause, ni zoom.
            if (!o.flag("pilotable", false)) {
                trendStates_.erase(key);
                trendPaused_.erase(key);
            }
            // 1.13.1 : FIGEE - l'acquisition s'arrete : rien n'entre, rien ne tombe (Reprendre laisse un trou).
            TrendState* st = nullptr;
            if (const auto it = trendStates_.find(key); it != trendStates_.end()) st = &it->second;
            if (st && st->frozen) continue;
            const double keep = std::max(1.0, o.number("duration", 60)) * 1.2 + 1.0;
            // 1.13.1 : les mesures plus vieilles que la fenetre tombent - celles de la fenetre montree restent : en
            // pause (avant : la fenetre se vidait apres 1,2 fois la duree), posee dans le passe (un glisser).
            double holdFrom = now - keep;
            if (const auto p = trendPaused_.find(key); p != trendPaused_.end()) holdFrom = std::min(holdFrom, p->second - keep);
            if (st && st->pinned) holdFrom = std::min(holdFrom, st->end - keep);
            double oldest = now;
            for (auto& s : series) {
                // Lot 14 : une valeur de l'automate pas encore lue ne se trace pas.
                // 1.12.6 (lot B) : lue en nombre par le lecteur du role (un TIME : des secondes).
                double number = 0;
                const bool ok = !plcUnread(s.expression) && sampleNumber(s.expression, number);
                if (ok) s.points.emplace_back(now, number);
                while (!s.points.empty() && (s.points.front().first < holdFrom || s.points.size() > kTrendMaxPoints)) s.points.pop_front();
                if (!s.points.empty()) oldest = std::min(oldest, s.points.front().first);
            }
            // Les trous d'avant la plus vieille mesure n'ont plus rien a couper.
            if (st) std::erase_if(st->gaps, [&](const std::pair<double, double>& g) { return g.second < oldest; });
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
        double number = 0;
        if (!sampleNumber(expr, number)) continue;   // 1.12.6 (lot B) : le lecteur du role (un TIME : des secondes)
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
    if (v.params.empty() && given.empty() && !viewmembers::any(v)) return nullptr;
    auto scope = std::make_shared<Scope>();
    const auto bind = [&](const std::string& name, const std::string& text, const std::string& type) {
        // 1.11.7 : un repere dans un argument (IN_V := $V[0]$, pose par Dupliquer...) : ses $ sont
        // transparents - V[0] est une variable, la popup la recoit en reference (avant : evaluee, rien ne passait).
        const std::string t = trimmed(markers::strip(text));
        // 1.12.4 : un parametre d'un type objet (LIST, VECTOR, MAP, TUPLE) - sans argument, un objet vide.
        // 1.12.20 : un rappel nomme (T_Filtre) aussi : sans argument, NULL.
        const std::string form = type.empty() ? std::string{} : types::callbackForm(project_, type);
        const bool rich = !type.empty() && (types::isRich(type) || !form.empty());
        if (t.empty()) {
            if (rich)
                if (auto o = sim::makeRichValue(type, {}, *env_)) scope->setObject(name, std::move(o));
            return;
        }
        // 1.12.19 (R3) : un parametre rappel - Sur_Oui := Valider (une fonction), := LAMBDA () => ..., := Sur_Fin (une
        // variable rappel de l'appelant), := NULL : le rappel lui-meme, vise des maintenant (une fonction n'est pas une
        // variable : pas d'alias). Reference ou copie, la meme chose : la popup appelle Sur_Oui().
        if (rich && !form.empty()) {
            std::string why;
            if (auto o = sim::makeRichValue(type, typeform::callbackInitial(form, t), *env_, &why)) scope->setObject(name, std::move(o));
            else log("Erreur", source_, "param\xC3\xA8tre " + name + " := " + t + " : " + frenchSimMessage(why));
            return;
        }
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
                    // 1.12.4 : la cle d'une MAP (Stock['vis']) reste un texte.
                    if (value && value->type() == sim::Type::String) index = "'" + value->asString() + "'";
                    else if (value) index = std::to_string(value->asInteger());
                    else log("Erreur", source_, "param\xC3\xA8tre " + name + " : index illisible (" + inner + ")");
                }
                path += "[" + index + "]";
                i = j;
            }
            if (env_->aliases) {
                if (const auto* val = env_->aliases->value(path)) { scope->setValue(name, *val); return; }
                // 1.12.4 : un parametre objet de l'appelant, passe tel quel (le meme objet).
                if (auto own = env_->aliases->object(path)) { scope->setObject(name, std::move(own)); return; }
                path = env_->aliases->resolve(path);
            }
            scope->setAlias(name, path);
            return;
        }
        if (rich) {
            // 1.12.4 : un litteral ([1, 2], ['a' := 1], (1, 'x')) ou un calcul (SPLIT(...), une fonction) :
            // un objet du type du parametre - la popup le lit et l'ecrit en place.
            std::string why;
            if (auto o = sim::makeRichValue(type, t, *env_, &why)) scope->setObject(name, std::move(o));
            else log("Erreur", source_, "param\xC3\xA8tre " + name + " := " + t + " : " + frenchSimMessage(why));
            return;
        }
        auto value = Expression::compile(t).evaluate(*env_);
        if (value) scope->setValue(name, *value);
        else log("Erreur", source_, "param\xC3\xA8tre " + name + " := " + t + " : " + value.error().message());
    };
    // 1.12.20 (lot G) : une popup, un symbole generique - ses T donnes ou deduits ; un rappel (v : T) recoit le sien.
    const params::GenericBinding generics = project_ && !v.generics.empty() ? params::bindGenerics(*project_, nullptr, v, arguments, plcTypes_)
                                                                           : params::GenericBinding{};
    for (const auto& prm : v.params) {
        const auto it = std::find_if(given.begin(), given.end(), [&](const auto& g) { return upper(g.first) == upper(prm.name); });
        bind(prm.name, it != given.end() ? it->second : prm.defaultValue, params::boundType(v, prm.type, &generics));
    }
    // 1.11.10 : une popup d'un symbole - les parametres du symbole que l'ouverture ne donne pas
    // (ouverte hors d'une instance) prennent la valeur par defaut du symbole.
    if (const View* owner = project_ ? popupOwner(*project_, v) : nullptr)
        for (const auto& prm : owner->params) {
            const bool there = v.param(prm.name) || std::any_of(given.begin(), given.end(), [&](const auto& g) { return upper(g.first) == upper(prm.name); });
            if (!there) bind(prm.name, prm.defaultValue, prm.type);
        }
    // Un argument que la vue ne declare pas : accepte (Generer le signale). 1.12.4 : celui d'un
    // parametre du symbole qui porte la popup prend son type (un objet).
    const View* owner = project_ ? popupOwner(*project_, v) : nullptr;
    for (const auto& [name, text] : given)
        if (!v.param(name) && !params::isGenericName(v, name)) {      // 1.12.20 (lot G) : T := REAL n'est pas un parametre
            const ViewParam* own = owner ? owner->param(name) : nullptr;
            bind(name, text, own ? own->type : std::string{});
        }
    // 1.12.16 : ses constantes et ses fonctions, par leur nom dans la vue (Seuil_Haut -> Vue_Four.Seuil_Haut :
    // le moteur les lit sous leur nom qualifie). Un parametre du meme nom passe avant (Generer le refuse).
    const auto member = [&](const std::string& name) {
        if (!v.param(name) && !scope->alias(name) && !scope->value(name) && !scope->object(name)) scope->setAlias(name, v.name + "." + name);
    };
    for (const auto& d : v.constants) member(d.name);
    if (!isSymbolView(v))
        for (const auto& f : v.functions) member(f.name);
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
    // 1.11.24 : la variable en clair, maintenant - un parametre de la popup (C.Niveau -> V[0].Niveau),
    // un indice calcule. Avant, C.Niveau s'ecrivait a la validation, hors de la popup : refuse.
    p.target = env_->concretePath(env_->resolved(target));
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
            if (sim::trueWord(text)) next = sim::Value::boolean(true);          // 1.12.19 : les memes mots partout
            else if (sim::falseWord(text)) next = sim::Value::boolean(false);
            else { fail("TRUE ou FALSE (1 ou 0)"); return; }
            break;
        }
        case sim::Type::String: next = sim::Value::text(text); break;
        default: {
            double x = 0;
            // 1.12.13 : une enumeration du projet (une case R/W d'un tableau) - le nom ou le texte d'une valeur aussi.
            bool numeric = parseNumber(text, x);
            if (!numeric && !p.enumType.empty() && project_)
                if (const auto* e = findEnumeration(*project_, p.enumType)) {
                    std::string u = upper(text);
                    if (const auto hash = u.find('#'); hash != std::string::npos) u = u.substr(hash + 1);
                    for (const auto& ev : e->values)
                        if (upper(ev.name) == u || (!ev.text.empty() && upper(ev.text) == upper(text))) {
                            x = static_cast<double>(ev.value);
                            numeric = true;
                            break;
                        }
                    if (!numeric) {
                        std::string names;
                        for (const auto& ev : e->values) names += (names.empty() ? "" : ", ") + ev.name;
                        fail("une valeur de " + e->name + " : " + names);
                        return;
                    }
                }
            if (!numeric) { fail("un nombre est attendu"); return; }
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
    const KeyboardPrompt was = p;                       // 1.12.13 : une case de tableau (Case saisie ensuite)
    GestureGuard gesture(*this, source, "Saisie :");                // l'audit : un geste de l'operateur
    sim::Value after;
    if (was.tableObject != kNoId && !env_->exists(target)) {
        // 1.12.13 : une case d'un objet (Stock['vis'], Alarmes[1].Priorite) - ecrite par une instruction, comme un script.
        const auto parent = scopePtr(was.tableView);
        auto sc = std::make_shared<Scope>(parent.get());
        sc->setValue("__TABLE_V__", next);
        std::string err;
        bool ok = false;
        {
            AliasGuard guard(env_->aliases, sc);
            ok = runStatements(target + " := __TABLE_V__;", source, now, &err);
        }
        if (!ok) {
            fail("\xC3\xA9" "criture refus\xC3\xA9" "e" + (err.empty() ? std::string{} : " : " + err));
            return;
        }
        auto e = Expression::compile(target);
        if (auto val = e.evaluate(*env_)) after = *val;
    } else {
        if (!write(target, next, source)) { fail("\xC3\xA9" "criture refus\xC3\xA9" "e"); return; }
        (void)env_->read(target, after);
    }
    log("Action", source, (was.tableObject != kNoId ? std::string("case saisie : ") : std::string("clavier virtuel : ")) + target + " = "
                              + (mask ? std::string("********") : formatValue(after)));
    prompt_.reset();
    // 1.12.13 : Case saisie - les actions du tableau, avec Ancienne et Nouvelle.
    if (was.tableObject != kNoId)
        if (const View* tv = viewOf(was.tableView); tv && tv->object(was.tableObject)) {
            View snapshot = *tv;
            evaluateProps(snapshot.id, *snapshot.object(was.tableObject));
            table::Model m;
            if (tableShown(snapshot.id, *snapshot.object(was.tableObject), m))
                tableFire(snapshot, *snapshot.object(was.tableObject), Trigger::CellEdit, m, was.tableRow, was.tableColumn, now, &was.before, &after);
        }
}

void Runtime::promptPart(std::string_view part, double now) {
    if (paused_ || !prompt_) return;             // 1.13.0 : en pause (le debogueur), le clavier de l'action attend
    if (unitWanted()) { (void)debugUnit("Saisie", [this, p = std::string(part), now] { promptPart(p, now); }); return; }
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
    if (o->kind == Kind::DocumentViewer) return docKeyboardMode();   // 1.12.30 : son numero de page, sa recherche
    if (o->kind == Kind::FileExplorer)                               // 1.14.1 (lot N5) : sa case Nom contient, des lettres
        return settings_.keyboard == "toujours" ? std::string("complet") : std::string{};
    if (o->kind == Kind::UnitConverter)                              // 1.14.1 (lot N5) : sa case Valeur, des chiffres
        return settings_.keyboard == "jamais" || (settings_.keyboard != "toujours" && plain(o->text("keyboard", "aucun")).rfind("num", 0) != 0)
                   ? std::string{} : std::string("numerique");
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
    if (paused_) return;                         // 1.13.0 : en pause (le debogueur), l'IHM est figee : le geste est ignore
    if (replayOn_) return;                       // 1.14.0 (M4) : une relecture ne recoit aucun clic
    if (unitWanted()) { (void)debugUnit("Clic", [this, object, p = std::string(part), now] { objectPart(object, p, now); }); return; }
    composed_.clear(); boundCalls_.clear();
    now_ = std::max(now_, now);
    lastActivity_ = now;
    if (!running_ || !project_) return;
    const View* v = shownViewOf(object);
    const Object* found = v ? v->object(object) : nullptr;
    if (!found) return;
    // 1.12.13 : la case (ou l'en-tete) touchee d'un tableau - retenue pour le geste qui suit (HmiRuntimeTable.cpp).
    if (found->kind == Kind::Table && (part.rfind("case:", 0) == 0 || part.rfind("entete:", 0) == 0)) {
        View snapshot = *v;
        evaluateProps(snapshot.id, *snapshot.object(object));
        tablePart(snapshot, *snapshot.object(object), part, now);
        return;
    }
    if (found->kind == Kind::RecipeManager) {
        GestureGuard gesture(*this, where(*v, found), "Clic sur");   // lot 13 : l'audit
        recipeManagerClick(object, part, now);
        return;
    }
    // 1.12.24 : le Reglage PID - une partie de sa carte, ou le geste que la signature a valide.
    if (found->kind == Kind::PidTuner) {
        pidObjectPart(*v, *found, part, now);
        return;
    }
    // 1.12.30 : la visionneuse de documents - ses pages, son zoom, sa recherche (HmiRuntimeDocs.cpp).
    if (found->kind == Kind::DocumentViewer) {
        docObjectPart(*v, *found, part, now);
        return;
    }
    // 1.14.0 (M4) : l'Enregistreur (son bouton), le Lecteur d'enregistrements (une ligne, Rejouer).
    if (found->kind == Kind::Recorder || found->kind == Kind::RecordingPlayer) {
        View snapshot = *v;
        evaluateProps(snapshot.id, *snapshot.object(object));
        if (recordOn_) recordClick(rec::EventKind::Click, object);
        recorderPart(snapshot, *snapshot.object(object), part, now);
        return;
    }
    // 1.14.0 : le Melangeur de volume - une course tiree, la molette, le bouton M (HmiRuntimeSystem.cpp).
    if (found->kind == Kind::VolumeMixer) {
        View snapshot = *v;
        evaluateProps(snapshot.id, *snapshot.object(object));
        GestureGuard gesture(*this, where(snapshot, snapshot.object(object)), "R\xC3\xA9glage de");
        mixerPart(snapshot, *snapshot.object(object), part);
        return;
    }
    // 1.12.16 : le bouton Pause / Continuer d'une courbe pilotable.
    if (found->kind == Kind::Trend && part == "pause") {
        GestureGuard gesture(*this, where(*v, found), "Clic sur");
        (void)setTrendPaused(v->id, object, !trendPausedAt(v->id, object).has_value());
        return;
    }
    // 1.13.1 : Figer, Curseurs, Tout voir, l'export de la zone, et les gestes sur la zone de trace.
    if (found->kind == Kind::Trend
        && (part == "figer" || part == "curseurs" || part == "toutvoir" || part.rfind("zone:", 0) == 0 || part.rfind("trend:", 0) == 0)) {
        trendObjectPart(*v, *found, part, now);
        return;
    }
    // 1.12.6 (lot B, § 2.7) : l'objet tel que l'ecran le montre (ses positions, ses valeurs, ses
    // onglets, ses vues, ses bornes... selon leur formule).
    View snapshot = *v;
    evaluateProps(snapshot.id, *snapshot.object(object));
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
        // 1.14.1 (lot N5) : le Convertisseur d'unites - la case Valeur, ses listes, inverser, Ecrire.
        case Kind::UnitConverter:
            converterPart(snapshot, o, part, now);
            return;
        // 1.14.1 (lot N5) : l'Explorateur de fichiers - une ligne, un dossier, le fil, le tri, la recherche, Extraire.
        case Kind::FileExplorer:
            explorerPart(snapshot, o, part, now);
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
    if (paused_) return;                         // 1.13.0 : en pause (le debogueur), l'IHM est figee : le geste est ignore
    if (replayOn_) return;                       // 1.14.0 (M4) : une relecture ne recoit rien
    if (unitWanted()) { (void)debugUnit("Saisie", [this, s = std::string(text), now] { typeText(s, now); }); return; }
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
    if (o->kind == Kind::DocumentViewer) { docTypeText(*v, *o, text, now); return; }   // 1.12.30
    auto& f = forms_[focused_];
    if (f.focus.empty()) return;
    auto& buffer = f.text[f.focus];
    auto& caret = f.caret[f.focus];
    if (f.fresh) { buffer.clear(); caret = 0; f.fresh = false; }
    caret = std::min(caret, buffer.size());
    std::string accepted;
    const bool numeric = (o->kind == Kind::InputField && inputMode(*o) == "numerique") || o->kind == Kind::UnitConverter;   // 1.14.1 (N5)
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
    if (o->kind == Kind::UnitConverter) converterRecompute(focused_);   // 1.14.1 (lot N5) : le resultat suit chaque frappe
    if (o->kind == Kind::FileExplorer) explorerTyped(focused_);         // 1.14.1 (lot N5) : Nom contient, a chaque frappe
}

void Runtime::typeKey(EditKey k, double now) {
    if (paused_) return;                         // 1.13.0 : en pause (le debogueur), l'IHM est figee : le geste est ignore
    if (replayOn_) return;                       // 1.14.0 (M4) : une relecture ne recoit rien
    if (unitWanted()) { (void)debugUnit("Saisie", [this, k, now] { typeKey(k, now); }); return; }
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
    if (o->kind == Kind::DocumentViewer) { docTypeKey(*v, *o, k, now); return; }   // 1.12.30
    const Id object = focused_;
    auto& f = forms_[object];
    if (f.focus.empty()) return;
    GestureGuard gesture(*this, where(*v, o), "Saisie de");          // lot 13 : l'audit (Entree, Tab valident)
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
            if (o->kind == Kind::InputField) f.text.clear();     // 1.11.22 : la saisie en attente aussi (comme unfocus)
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
    if (converters_.count(object)) converterRecompute(object);   // 1.14.1 (lot N5) : effacer, Echap (la Valeur de depart revient)
    if (explorers_.count(object)) explorerTyped(object);         // 1.14.1 (lot N5) : effacer, Echap (la recherche s'efface)
}

bool Runtime::clickSubmits(Id object) const {
    const View* v = object != kNoId ? shownViewOf(object) : nullptr;
    const Object* o = v ? v->object(object) : nullptr;
    return o && std::any_of(o->actions.begin(), o->actions.end(), [](const Action& a) {
               return a.trigger == Trigger::Click && a.operation == Operation::SubmitInputs;
           });
}

void Runtime::unfocus(double now, Id towards) {
    if (paused_) return;                         // 1.13.0 : en pause (le debogueur), le champ garde sa saisie (rien ne s'ecrit)
    if (focused_ == kNoId) return;
    if (unitWanted()) { (void)debugUnit("Saisie", [this, now, towards] { unfocus(now, towards); }); return; }
    // 1.11.23 : le bouton Valider les saisies - le champ garde sa saisie, le bouton la valide.
    if (towards != kNoId && clickSubmits(towards)) return;
    const Id object = focused_;
    const View* v = shownViewOf(object);
    const Object* o = v ? v->object(object) : nullptr;
    if (o && o->kind == Kind::InputField && o->flag("validateOnExit", false)) {
        const View snapshot = *v;
        GestureGuard gesture(*this, where(snapshot, snapshot.object(object)), "Saisie de");     // lot 13 : l'audit
        submitForm(snapshot, *snapshot.object(object), now);
    }
    if (focused_ == object) {
        auto& f = forms_[object];
        if (o && o->kind == Kind::UnitConverter && f.fresh) f.text.erase("valeur");   // 1.14.1 (N5) : rien tape, la Valeur de depart reste suivie
        f.focus.clear();
        f.fresh = false;
        if (o && o->kind == Kind::InputField) f.text.clear();
        // 1.12.30 : la visionneuse quitte ses champs (le numero tape a moitie est oublie).
        if (const auto d = docs_.find(object); d != docs_.end()) {
            d->second.state.editingPage = false;
            d->second.state.pageText.clear();
            d->second.state.editingSearch = false;
            d->second.state.zoomMenu = false;
        }
        focused_ = kNoId;
        if (o && o->kind == Kind::UnitConverter) converterRecompute(object);   // 1.14.1 (lot N5)
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
            // 1.12.6 (lot B, § 2.2) : ce qui est tape, converti vers le type de la variable par la meme regle
            // que les commandes (HmiRoles.cpp) - une enumeration par son nom (Auto) ou son numero, une duree
            // (T#5s, 5 s, 00:05, 1 h 30), une date (09/10/2026), un booleen (vrai, ON, 1) ; un nombre hors
            // des bornes du type (40000 dans un INT) est refuse et dit (avant : 0, ou -25536, sans un mot).
            const auto target = writeTarget(var, &o);
            const auto refusedOr = [&](const roles::Written& w) {
                if (!w.ok) { fail(w.note); return false; }
                if (!w.note.empty()) { fail("hors bornes : " + w.note); return false; }
                next = w.value;
                return true;
            };
            if (inputMode(o) == "numerique") {
                double x = 0;
                if (!roles::readNumber(text, x)) { fail("un nombre est attendu"); return; }
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
                if (cur.type() == sim::Type::String) {
                    next = sim::Value::text(text);
                } else {
                    if (sim::isInteger(cur.type()) && cur.type() != sim::Type::Time && target.enumValues.empty()
                        && std::fabs(x - std::round(x)) > 1e-9) {
                        fail("un entier est attendu");
                        return;
                    }
                    if (!refusedOr(roles::toVariable(sim::Value::real(x), target))) return;
                }
            } else {
                const auto limit = static_cast<std::size_t>(std::max(1.0, o.number("maxLength", 32)));
                if (codepoints(text) > limit) { fail(std::to_string(limit) + " caract\xC3\xA8res au plus"); return; }
                next = sim::Value::text(text);
                // 1.12.5 (lot A, D15) : le mode texte vers une variable qui n'est pas un texte : ce qui
                // est tape, converti a son type ; illisible : refuse, et dit (avant : 0 ecrit sans un mot).
                if ((cur.type() != sim::Type::String && cur.type() != sim::Type::Unknown) || !target.enumValues.empty())
                    if (!refusedOr(roles::textToVariable(text, target))) return;
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
            // 1.14.0 (M4) : la saisie validee, enregistree (un mot de passe reste masque).
            if (recordOn_) recordEvent(rec::EventKind::Input, source, inputMode(o) == "motdepasse" ? std::string("********") : formatValue(after));
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
            const std::string after = roles::nameOf(o.text("afterLogin"));   // 1.12.6 (lot C) : sans ses $
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
        case Kind::UnitConverter:                              // 1.14.1 (lot N5) : Entree garde la valeur tapee
            converterSubmit(v, o, now);
            return;
        case Kind::FileExplorer:                               // 1.14.1 (lot N5) : Entree rend le clavier (la recherche reste)
            done();
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
