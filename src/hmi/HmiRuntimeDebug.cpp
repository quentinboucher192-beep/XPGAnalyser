// =============================================================================
//  hmi/HmiRuntimeDebug.cpp - 1.13.0 : le debogueur des scripts IHM
// -----------------------------------------------------------------------------
//  LA PAUSE EST REELLE, au milieu de l'execution (le debogueur de l'automate,
//  lui, ne s'arrete qu'en fin de cycle). Un point arme (ou un pas demande, une
//  pause demandee, IHM_POINT_ARRET quelque part dans le projet) : le TRAVAIL qui
//  peut lancer un script - un tick (le cycle, les minuteries), un geste, le
//  demarrage - tourne sur un fil a lui (ScriptTask, celui des scripts detaches,
//  1.12.26 ; un fil repris d'un travail a l'autre, pas un fil neuf par image),
//  meme s'il n'est pas detache. Ce qui touche l'ecran (les crochets : un son, la
//  Console, une question) y retourne au fil de l'IHM ; une exception du travail
//  remonte sur le fil de l'IHM. Au point d'arret, ce fil rend la main a
//  celui de l'IHM (yield) et attend : la suite du travail (les autres scripts du
//  cycle, les actions suivantes du clic, la premiere vue apres les scripts de
//  Demarrage) attend avec lui. L'IHM se fige (tick ne fait plus rien, les gestes
//  sont ignores, un relachement est garde puis rejoue), l'ecran continue de
//  peindre la derniere image. Un seul fil tourne a la fois : rien a verrouiller.
//
//  LE COUT EST NUL QUAND ON NE DEBOGUE PAS : sans point arme ni pas demande, les
//  scripts tournent sur le fil de l'IHM comme avant, et aucun programme ne porte
//  de ligne marquee.
//
//  OU L'ON EST. Chaque execution (un script, une fonction IHM simple) porte un
//  numero de programme (sim::setProgramTag) qui dit son code ; une fonction aux
//  types riches tourne dans le script qui l'appelle, sous sa propre Function
//  (richOwners_) ; une copie liee (fonction de vue, de popup, de symbole) porte
//  l'id de la fonction qu'elle copie (HmiFunction::debugOrigin) ; une methode et un
//  operateur, lus par le moteur (sa reserve), y sont marques par leur id comme une
//  fonction riche (sim::Environment::debugFunction ; une methode se reconnait aussi
//  a son nom interne, METHODE_<type>_<methode>) ; une redefinition d'instance, par
//  son id de copie (boundCodes_). La pile se lit dans le
//  moteur : les executions qui attendent un appel (sim::callerFrames), puis celle
//  de la ligne (LineProbe::frames), et au bas le contexte (le cycle IHM, un
//  geste, un script detache).
//
//  LE PAS A PAS se regle par une profondeur : le nombre de cadres de la pile a
//  la pause. Suivant : la prochaine ligne a une profondeur au plus egale ;
//  Entrer : la prochaine ligne, ou qu'elle soit (dans du code qu'on sait
//  montrer) ; Sortir : la prochaine ligne d'un cadre plus bas. Un WAIT au milieu
//  rend la main normalement : l'IHM respire, puis le pas reprend.
//
//  UNE LECTURE DU DEBOGUEUR (un espion, la condition d'un point, les valeurs de la
//  ligne) ne change rien : les locales du code en pause et les variables IHM se
//  lisent seulement, une fonction qui y echoue ne laisse rien au script
//  (probeQuietly).
// =============================================================================
#include "HmiRuntime.hpp"

#include "HmiDecl.hpp"
#include "HmiEnums.hpp"
#include "HmiLog.hpp"
#include "HmiMarkers.hpp"
#include "HmiMethods.hpp"   // 1.13.0 : les points d'arret dans une methode d'un type IHM
#include "HmiNames.hpp"
#include "HmiOperators.hpp"
#include "HmiScript.hpp"
#include "HmiTypes.hpp"
#include "../sim/DebugShared.hpp"
#include "../sim/Interpreter.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <set>

namespace hmi {

namespace {

// Une methode n'a pas de decalage : sa FUNCTION interne (methodFunctionText) porte son en-tete et SELF sur la ligne 1,
// avec le debut du corps - la ligne N du moteur est la ligne N de l'editeur de la methode (marge, pile, Compiler).
constexpr std::uint32_t kOperatorHeaderLines = 1; // operatorFunctionText : FUNCTION OPERATEUR_ADD(a : T; b : REAL) : T

std::string upperText(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool blank(std::string_view s) {
    return std::all_of(s.begin(), s.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; });
}

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::size_t textHash(std::string_view s) { return std::hash<std::string_view>{}(s) ^ (s.size() * 0x9E3779B97F4A7C15ULL); }

// Une methode d'un type IHM (du projet, d'une bibliotheque reliee) par son id ; `owner` : son type.
const HmiFunction* codeMethodById(const Project& p, Id id, const HmiType** owner = nullptr) {
    if (id == kNoId) return nullptr;
    const HmiFunction* found = nullptr;
    const auto look = [&](const HmiType& t) {
        if (found) return;
        for (const auto& m : t.methods)
            if (m.id == id) {
                found = &m;
                if (owner) *owner = &t;
                return;
            }
    };
    for (const auto& t : p.programs.types) look(t);
    if (!found) p.forEachLibraryType([&](const HmiType& t, const LoadedLibrary&) { look(t); });
    return found;
}

// 1.13.0 : un operateur d'un type IHM ou d'un symbole par son id ; `owner` : son porteur.
const HmiOperator* operatorOf(const Project& p, Id id, OperatorOwner* owner = nullptr) {
    return id == kNoId ? nullptr : operatorById(p, id, owner);
}

// 1.13.0 : un nom (une locale, une valeur de la ligne) lu dans un operateur, montre comme son code l'ecrit : son nom
// interne (OPERATEUR_ADD, OPERATEUR_ADD.X) redevient Resultat (Resultat.X) - l'inverse de operatorInternalText.
std::string operatorShownName(const HmiOperator& o, const std::string& name) {
    const std::string internal = upperText(operatorInternalName(o));
    if (name.size() < internal.size() || upperText(std::string_view(name).substr(0, internal.size())) != internal) return name;
    if (name.size() > internal.size() && identChar(name[internal.size()])) return name;      // OPERATEUR_ADDX : un autre nom
    return std::string(kResultName) + name.substr(internal.size());
}

// Le texte d'un operateur pour le debogueur : sa FUNCTION interne, l'en-tete sur la ligne 1 avec le debut du corps - les
// lignes sont celles de son editeur (le moteur, lui, lit l'en-tete seul sur sa ligne : kOperatorHeaderLines).
std::string operatorDebugText(const HmiOperator& o) {
    std::string t = operatorFunctionText(o);
    if (const auto nl = t.find('\n'); nl != std::string::npos) t[nl] = ' ';
    return markers::strip(t);
}

// Une fonction a deboguer par son id : du projet (ou d'une bibliotheque), d'une vue, d'une popup ou d'un symbole, une
// methode d'un type IHM.
const HmiFunction* functionById(const Project& p, Id id) {
    if (id == kNoId) return nullptr;
    if (const auto* f = p.function(id)) return f;
    for (const auto& v : p.views)
        for (const auto& f : v.functions)
            if (f.id == id) return &f;
    return codeMethodById(p, id);   // 1.13.0 : une methode d'un type IHM
}

// Le texte d'un code a deboguer, tel que le moteur le lit (les lignes du corps restent les memes que
// dans l'editeur) ; faux et `why` : inconnu, ou pas du ST execute.
bool debugText(const Project& p, Id script, Id function, std::string& text, bool& isFunction, std::string* why) {
    if (script != kNoId) {
        const Script* sc = p.script(script);
        if (!sc) {
            if (why) *why = "script introuvable : il ne s'arr\xC3\xAAtera pas";
            return false;
        }
        if (sc->lang != ScriptLang::ST) {
            if (why) *why = "non ex\xC3\xA9" "cut\xC3\xA9 en simulation (C/C++) : il ne s'arr\xC3\xAAtera pas";
            return false;
        }
        text = markers::strip(decl::codeOf(*sc));
        isFunction = false;
        return true;
    }
    // 1.13.0 (J1b) : une methode - sa FUNCTION interne telle que le moteur la lit (methodFunctionText, en-tete sur la
    // ligne 1 : les lignes du corps sont celles de l'editeur).
    const HmiType* owner = nullptr;
    if (const HmiFunction* m = codeMethodById(p, function, &owner); m && owner) {
        MethodCall mc;
        if (!resolveMethodInternal(p, methodFunctionName(fullName(*owner), *m), mc)) {
            if (why) *why = "m\xC3\xA9thode introuvable : il ne s'arr\xC3\xAAtera pas";
            return false;
        }
        text = mc.function;
        isFunction = true;
        return true;
    }
    if (const HmiFunction* f = functionById(p, function)) {
        text = functionText(*f);
        isFunction = true;
        return true;
    }
    if (const HmiOperator* o = operatorOf(p, function)) {            // 1.13.0 : un operateur, une conversion
        text = operatorDebugText(*o);
        isFunction = true;
        return true;
    }
    if (why) *why = "fonction introuvable : il ne s'arr\xC3\xAAtera pas";
    return false;
}

std::vector<std::uint32_t> linesOfText(const std::string& text, bool isFunction, std::string* why) {
    if (isFunction) {
        auto fn = sim::parseFunction(text, "fonction");
        if (!fn) {
            if (why) *why = frenchSimMessage(fn.error().context.empty() ? fn.error().message() : fn.error().context);
            return {};
        }
        return sim::statementLines(**fn);
    }
    auto program = sim::parse(text, "script", dialectOptions());
    if (!program) {
        if (why) *why = frenchSimMessage(program.error().context.empty() ? program.error().message() : program.error().context);
        return {};
    }
    return sim::codeStatementLines(**program);
}

// Le texte sans ses commentaires ni ses chaines (remplaces par des blancs : les places restent).
std::string codeOnly(std::string_view s) {
    std::string out(s);
    for (std::size_t i = 0; i < out.size();) {
        if (out[i] == '(' && i + 1 < out.size() && out[i + 1] == '*') {
            const auto end = out.find("*)", i + 2);
            const std::size_t stop = end == std::string::npos ? out.size() : end + 2;
            for (std::size_t k = i; k < stop; ++k) if (out[k] != '\n') out[k] = ' ';
            i = stop;
        } else if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '/') {
            while (i < out.size() && out[i] != '\n') out[i++] = ' ';
        } else if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '*') {      // le commentaire du C et du C++
            const auto end = out.find("*/", i + 2);
            const std::size_t stop = end == std::string::npos ? out.size() : end + 2;
            for (std::size_t k = i; k < stop; ++k) if (out[k] != '\n') out[k] = ' ';
            i = stop;
        } else if (out[i] == '\'' || out[i] == '"') {
            const char q = out[i];
            out[i++] = ' ';
            while (i < out.size() && out[i] != q && out[i] != '\n') out[i++] = ' ';
            if (i < out.size() && out[i] == q) out[i++] = ' ';
        } else {
            ++i;
        }
    }
    return out;
}

// 1.13.0 : le code appelle-t-il IHM_POINT_ARRET - le mot entier, hors des commentaires et des chaines (un point mis en
// commentaire "(* IHM_POINT_ARRET; *)" ne compte plus, ni IHM_POINT_ARRET_X).
bool writesCodeBreak(std::string_view body) {
    static const std::string kWord = "IHM_POINT_ARRET";
    if (body.size() < kWord.size()) return false;
    const std::string s = codeOnly(body);
    for (std::size_t i = 0; i + kWord.size() <= s.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(s[i])) != 'I' || (i > 0 && identChar(s[i - 1]))) continue;
        std::size_t k = 0;
        while (k < kWord.size() && std::toupper(static_cast<unsigned char>(s[i + k])) == kWord[k]) ++k;
        if (k == kWord.size() && (i + k >= s.size() || !identChar(s[i + k]))) return true;
    }
    return false;
}

// Les noms que declare un code (en majuscules) : ce qui precede « : » (pas « := ») dans un bloc VAR,
// l'en-tete d'une fonction ; les variables de FOR, FOR EACH et CATCH ; le nom d'une fonction (son retour).
std::set<std::string> declaredNames(std::string_view code) {
    std::set<std::string> out;
    const std::string s = codeOnly(code);
    std::string previous;
    bool afterForEach = false;
    for (std::size_t i = 0; i < s.size();) {
        if (!identStart(s[i])) {
            if (s[i] == ';' || s[i] == '\n') afterForEach = false;
            ++i;
            continue;
        }
        const std::size_t from = i;
        while (i < s.size() && identChar(s[i])) ++i;
        const std::string word = upperText(std::string_view(s).substr(from, i - from));
        std::size_t j = i;
        while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
        const bool colon = j < s.size() && s[j] == ':' && (j + 1 >= s.size() || s[j + 1] != '=');
        if (colon || previous == "FOR" || previous == "CATCH" || previous == "FUNCTION" || afterForEach) out.insert(word);
        if (previous == "FOR" && word == "EACH") afterForEach = true;
        if (word == "IN" || word == "DO" || word == "TO") afterForEach = false;
        if (afterForEach && word != "EACH") out.insert(word);
        previous = word;
    }
    return out;
}

// Les noms d'une condition qu'il faut connaitre : le debut de chaque chemin (Armoires dans
// Armoires[0].ana), sans les mots du langage (ENTRE ... ET compris : P ENTRE 2.0 ET 5.0), les litteraux types (T#5s,
// 16#FF, T_MODE#Auto) ni les fonctions appelees (un nom suivi de "(").
std::vector<std::string> conditionRoots(std::string_view condition) {
    static const std::set<std::string> kWords = {"AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE", "NULL", "IN", "ENTRE", "ET"};
    std::vector<std::string> out;
    const std::string s = codeOnly(condition);
    for (std::size_t i = 0; i < s.size();) {
        if (std::isdigit(static_cast<unsigned char>(s[i]))) {             // un nombre : 12, 3.5, 16#FF, 2#1010, 1e3
            while (i < s.size() && (identChar(s[i]) || s[i] == '.' || s[i] == '#')) ++i;
            continue;
        }
        if (!identStart(s[i])) { ++i; continue; }
        std::size_t back = i;
        while (back > 0 && (s[back - 1] == ' ' || s[back - 1] == '\t')) --back;
        const bool member = back > 0 && (s[back - 1] == '.' || s[back - 1] == '#');
        const std::size_t from = i;
        while (i < s.size() && identChar(s[i])) ++i;
        const std::string word(s.substr(from, i - from));
        std::size_t j = i;
        while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
        bool called = j < s.size() && s[j] == '(';
        if (!called && i < s.size() && s[i] == '<') {                   // Limiter<INT>(x) : un appel generique (colle)
            const auto close = s.find('>', i);
            std::size_t k = close == std::string::npos ? s.size() : close + 1;
            while (k < s.size() && s[k] == ' ') ++k;
            called = close != std::string::npos && k < s.size() && s[k] == '(';
        }
        const bool typed = i < s.size() && s[i] == '#';
        if (typed) {                                                     // T#5s, T_MODE#Auto : tout le litteral
            ++i;
            while (i < s.size() && (identChar(s[i]) || s[i] == '.' || s[i] == ':' || s[i] == '-')) ++i;
            continue;
        }
        if (member || called || kWords.count(upperText(word))) continue;
        if (std::find(out.begin(), out.end(), word) == out.end()) out.push_back(word);
    }
    return out;
}

std::string conditionFrench(const core::Error& e) { return frenchSimMessage(sim::debug::conditionProblem(e)); }

// 1.13.0 (lot J8, le debogueur) : l'espace de noms et le corps d'un code (ses USING) ; faux : inconnu.
bool codeSpace(const Project& p, Id script, Id function, std::string& ns, std::string& body) {
    if (script != kNoId) {
        const Script* sc = p.script(script);
        if (!sc) return false;
        ns = sc->ns;
        body = sc->body;
        return true;
    }
    if (const HmiFunction* f = functionById(p, function)) {
        const HmiType* owner = nullptr;
        if (codeMethodById(p, function, &owner) && owner) ns = owner->ns;     // une methode : l'espace de son type
        else ns = f->ns;
        body = f->body;
        return true;
    }
    OperatorOwner owner;
    if (const HmiOperator* o = operatorOf(p, function, &owner)) {        // 1.13.0 : un operateur - l'espace de son type
        const HmiType* t = owner.kind == OperatorOwner::Kind::Type ? p.hmiType(owner.id) : nullptr;
        ns = t ? t->ns : std::string{};
        body = o->body;
        return true;
    }
    return false;
}

// Une condition (un espion) lue comme le code qui la porte lit ses noms : dans un espace de noms ou sous USING, ses noms
// courts ecrits en entier (EnDefaut -> Gaz.EnDefaut), comme le moteur le fait pour le code (names::qualify) ; les
// locales du code (`hidden`) ne bougent pas. Sans espace : telle quelle.
std::string qualifiedCondition(const Project& p, Id script, Id function, const std::string& written) {
    // 1.13.0 : dans un operateur, Resultat (et le nom de la conversion) se lit comme le moteur execute son code
    // (operatorFunctionText) : sous son nom interne, OPERATEUR_ADD. Sinon la condition "Resultat.X > 1" echouerait au
    // premier passage (le point se desarmerait, "condition invalide").
    // Dans une methode, comme son corps (methodFunctionText) : THIS -> SELF, un membre -> SELF.X, son nom -> son retour.
    const HmiOperator* op = script == kNoId ? operatorOf(p, function) : nullptr;
    const HmiType* methodOwner = nullptr;
    const HmiFunction* method = script == kNoId && !op ? codeMethodById(p, function, &methodOwner) : nullptr;
    const std::string condition = op ? operatorInternalText(*op, written)
                                     : method && methodOwner ? methodInternalText(p, fullName(*methodOwner), *method, written) : written;
    std::string ns, body;
    if (!codeSpace(p, script, function, ns, body)) return condition;
    if (ns.empty() && !names::mentionsUsing(body)) return condition;
    // Les USING du code, recopies devant la condition (une ligne chacun), puis retires.
    std::string prefix;
    std::size_t lines = 0;
    const std::string code = codeOnly(body);
    for (std::size_t at = 0; at <= code.size();) {
        const auto nl = code.find('\n', at);
        const std::size_t end = nl == std::string::npos ? code.size() : nl;
        std::size_t b = at;
        while (b < end && std::isspace(static_cast<unsigned char>(code[b]))) ++b;
        if (end - b > 6 && upperText(std::string_view(code).substr(b, 6)) == "USING ") {
            prefix += body.substr(b, end - b) + "\n";
            ++lines;
        }
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
    std::vector<std::string> hidden;
    std::string text;
    bool isFunction = false;
    if (debugText(p, script, function, text, isFunction, nullptr))
        for (const auto& n : declaredNames(text)) hidden.push_back(n);
    const std::string out = names::qualify(p, prefix + condition, ns, hidden, false);
    std::size_t at = 0;
    for (std::size_t k = 0; k < lines && at != std::string::npos; ++k) {
        at = out.find('\n', at);
        if (at != std::string::npos) ++at;
    }
    return at == std::string::npos ? condition : out.substr(at);
}

} // namespace

// ------------------------------------------------------------------- les codes ---
std::vector<std::uint32_t> Runtime::breakableLines(const Project& p, Id script, Id function) {
    std::string text;
    bool isFunction = false;
    if (!debugText(p, script, function, text, isFunction, nullptr)) return {};
    return linesOfText(text, isFunction, nullptr);
}

std::string Runtime::debugCodeName(const Project& p, Id script, Id function) {
    if (script != kNoId) {
        const Script* sc = p.script(script);
        if (!sc) return {};
        if (const Id v = p.viewOfScript(script); v != kNoId)
            if (const View* view = p.view(v)) return view->name + "." + sc->event;
        return "script " + fullName(*sc);
    }
    if (function != kNoId) {
        if (const auto* f = p.function(function)) return "fonction " + fullName(*f);   // 1.13.0 : Util.Moyenne
        for (const auto& v : p.views)                                                  // une vue, une popup, un symbole
            for (const auto& f : v.functions)
                if (f.id == function) return "fonction " + v.name + "." + f.name;
        const HmiType* owner = nullptr;   // 1.13.0 : "methode T_Pompe.Demarrer" (Gaz.T_Pompe.Demarrer dans un espace)
        if (const auto* m = codeMethodById(p, function, &owner); m && owner) return "m\xC3\xA9thode " + fullName(*owner) + "." + m->name;
        // 1.13.0 : "operateur + (type T_Vecteur)", "conversion TO_REAL (symbole Sym_Pompe)".
        OperatorOwner by;
        if (const auto* o = operatorOf(p, function, &by))
            return (o->op == kConversionOp ? "conversion " + operatorFunctionName(*o) : "op\xC3\xA9rateur " + o->op) + " (" + by.label() + ")";
    }
    return {};
}

std::string Runtime::debugCodeBody(const Project& p, Id script, Id function) {
    if (script != kNoId) {
        const Script* sc = p.script(script);
        return sc ? sc->body : std::string{};
    }
    if (const HmiFunction* f = functionById(p, function)) return f->body;
    const HmiOperator* o = operatorOf(p, function);
    return o ? o->body : std::string{};
}

std::string Runtime::boundCodeName(Id function) const {
    const auto it = boundCodes_.find(function);
    return it == boundCodes_.end() ? std::string{} : it->second.name;
}

std::string Runtime::boundCodeBody(Id function) const {
    const auto it = boundCodes_.find(function);
    return it == boundCodes_.end() ? std::string{} : it->second.body;
}

bool Runtime::debugTextOf(Id script, Id function, std::string& text, bool& isFunction, std::string* why) const {
    if (project_ && debugText(*project_, script, function, text, isFunction, nullptr)) return true;
    // 1.13.0 : une redefinition d'instance deja appelee (sa copie liee porte un id a elle).
    if (script == kNoId)
        if (const auto it = boundCodes_.find(function); it != boundCodes_.end()) {
            text = it->second.text;
            isFunction = true;
            return true;
        }
    if (!project_) return false;
    return debugText(*project_, script, function, text, isFunction, why);
}

std::uint32_t Runtime::debugHeaderLines(Id function) const {
    if (!project_ || function == kNoId) return 0;
    if (operatorOf(*project_, function)) return kOperatorHeaderLines;
    return 0;
}

std::size_t Runtime::debugCodeHash(Id script, Id function) const {
    std::string text;
    bool isFunction = false;
    if (!debugTextOf(script, function, text, isFunction, nullptr)) return 0;
    return textHash(text);
}

std::uint32_t Runtime::debugTag(Id script, Id function) {
    if (script == kNoId && function == kNoId) return 0;
    const auto key = std::make_pair(script, function);
    if (const auto it = ownerTags_.find(key); it != ownerTags_.end()) return it->second;
    tagOwners_.push_back(key);
    const auto tag = static_cast<std::uint32_t>(tagOwners_.size());
    ownerTags_[key] = tag;
    return tag;
}

const HmiFunction* Runtime::methodOfFrame(std::string_view internalName) const {
    if (!project_ || internalName.size() <= 8 || upperText(internalName.substr(0, 8)) != "METHODE_") return nullptr;
    const std::string wanted = upperText(internalName);
    const HmiFunction* found = nullptr;
    const auto look = [&](const HmiType& t) {
        if (found) return;
        for (const auto& m : t.methods)
            if (upperText(methodFunctionName(fullName(t), m)) == wanted) {
                found = &m;
                return;
            }
    };
    for (const auto& t : project_->programs.types) look(t);
    if (!found) project_->forEachLibraryType([&](const HmiType& t, const LoadedLibrary&) { look(t); });
    return found;
}

std::uint32_t Runtime::frameLine(const sim::LineProbe::Frame& f, std::uint32_t line) const {
    if (!f.standalone) return line;
    // Un operateur : la ligne de l'en-tete que le moteur lit avant son corps (une methode : aucune).
    std::uint32_t header = 0;
    if (const auto it = richOwners_.find(static_cast<const sim::Function*>(f.code)); it != richOwners_.end()) header = debugHeaderLines(it->second);
    return line > header ? line - header : line;
}

std::pair<Id, Id> Runtime::frameOwner(const sim::LineProbe::Frame& f) const {
    if (f.standalone) {
        const auto it = richOwners_.find(static_cast<const sim::Function*>(f.code));
        if (it != richOwners_.end()) return std::make_pair(kNoId, it->second);
        // 1.13.0 : une methode d'un type IHM (son nom interne) - son code se montre (la pile, le volet, Entrer).
        if (const HmiFunction* m = methodOfFrame(f.function)) return std::make_pair(kNoId, m->id);
        return std::make_pair(kNoId, kNoId);
    }
    if (f.tag == 0 || f.tag > tagOwners_.size()) return {kNoId, kNoId};
    return tagOwners_[f.tag - 1];
}

// ----------------------------------------------------------------- les points ---
void Runtime::setDebugger(bool on) {
    if (debugOn_ == on) return;
    debugOn_ = on;
    if (!on) {
        breakpoints_.clear();
        resolveBreakpoints();
    }
    ++debugRev_;
}

void Runtime::setBreakpoints(std::vector<DebugBreakpoint> list) {
    // Les passages comptes restent (le meme point, par son identifiant). 1.13.0 : un point qui en a recueilli un autre
    // (deux points fondus sur une ligne, HmiDebugSession::shiftLines) arrive avec leur somme : la plus grande des deux
    // valeurs reste (la liste ne compte jamais moins que le moteur, sauf ce fondu qui compte plus).
    for (auto& bp : list)
        for (const auto& old : breakpoints_)
            if (old.id == bp.id) bp.hits = std::max(bp.hits, old.hits);
    breakpoints_ = std::move(list);
    resolveBreakpoints();
}

// Le premier nom d'une condition que ni le code (ses declarations), ni sa vue, ni l'IHM ne connaissent ; vide : tous connus.
std::string Runtime::unknownConditionName(const std::string& text, Id script, Id function, const std::string& condition) const {
    if (!project_) return {};
    // Les noms : ceux que le code declare (ses VAR, ses parametres, ses boucles), les declarations du
    // modele, puis tout ce que l'IHM connait (variables, vues, systeme, automate, enumerations, espaces de noms).
    auto declared = declaredNames(text);
    const View* view = nullptr;
    if (script != kNoId) {
        if (const Script* sc = project_->script(script)) {
            for (const auto& d : sc->decls) declared.insert(upperText(d.name));
            if (const Id v = project_->viewOfScript(script); v != kNoId) view = project_->view(v);
        }
    } else if (const HmiFunction* f = functionById(*project_, function)) {
        for (const auto& d : f->decls) declared.insert(upperText(d.name));
        declared.insert(upperText(f->name));
        // Une methode : SELF (THIS), l'objet, et les membres de son type par leur nom (lus comme son corps les lit).
        const HmiType* owner = nullptr;
        if (codeMethodById(*project_, function, &owner) && owner) {
            declared.insert("SELF");
            declared.insert("THIS");
            for (const auto& mem : types::membersOf(*project_, fullName(*owner))) declared.insert(upperText(mem.name));
        }
    } else if (const HmiOperator* o = operatorOf(*project_, function)) {
        // 1.13.0 : un operateur - a, b, Resultat (et le nom d'une conversion), ses declarations.
        for (const auto& d : o->decls) declared.insert(upperText(d.name));
        for (const char* n : {"A", "B"}) declared.insert(n);
        declared.insert(upperText(kResultName));
        if (const auto fn = operatorFunctionName(*o); !fn.empty()) declared.insert(upperText(fn));
    }
    for (const auto& root : conditionRoots(condition)) {
        const std::string u = upperText(root);
        if (declared.count(u)) continue;
        if (view && (view->param(root) || view->objectByName(root))) continue;
        if (view && std::any_of(view->constants.begin(), view->constants.end(), [&](const Declaration& d) { return upperText(d.name) == u; }))
            continue;
        if (envOf().exists(root) || !envOf().declaredType(root).empty() || envOf().richVariable(root)) continue;
        if (project_->variable(root) || project_->viewByName(root) || project_->generalScript(root) || u == "SYS" || u == "API") continue;
        if (project_->isNamespace(root)) continue;                         // Gaz.Seuil : un espace de noms
        if (logLevelByName(root)) continue;
        bool enumValue = false;
        for (const auto& t : project_->programs.types)
            for (const auto& ev : t.values)
                if (upperText(ev.name) == u) enumValue = true;
        if (enumValue) continue;
        return root;
    }
    return {};
}

std::string Runtime::conditionProblem(Id script, Id function, const std::string& condition) const {
    if (blank(condition) || !project_) return {};
    std::string text, why;
    bool isFunction = false;
    if (!debugTextOf(script, function, text, isFunction, &why)) return why;
    auto parsed = sim::parseExpression(qualifiedCondition(*project_, script, function, condition), dialectOptions());
    if (!parsed) return conditionFrench(parsed.error());
    const std::string unknown = unknownConditionName(text, script, function, condition);
    return unknown.empty() ? std::string{} : unknown + " n'est pas d\xC3\xA9" "clar\xC3\xA9" "e ici";
}

// Chacun retrouve son code, sa ligne (la premiere instruction a partir de celle demandee), sa condition
// (lue dans le dialecte, avec les noms de l'espace du code ; ses noms connus du code ou de l'IHM - un nom inconnu se dit
// tout de suite).
void Runtime::resolveBreakpoints() {
    breakStates_.assign(breakpoints_.size(), BreakState{});
    anyArmed_ = false;
    for (std::size_t i = 0; i < breakpoints_.size(); ++i) {
        auto& bp = breakpoints_[i];
        auto& st = breakStates_[i];
        bp.effectiveLine = 0;
        bp.note.clear();
        if (!project_) continue;
        std::string text, why;
        bool isFunction = false;
        if (!debugTextOf(bp.script, bp.function, text, isFunction, &why)) {
            bp.note = why;
            continue;
        }
        st.codeHash = textHash(text);
        const auto lines = linesOfText(text, isFunction, &why);
        if (lines.empty()) {
            bp.note = why.empty() ? std::string("aucune instruction : il ne s'arr\xC3\xAAtera pas")
                                  : "le code ne se lit pas (" + why + ") : il ne s'arr\xC3\xAAtera pas";
            continue;
        }
        const auto want = std::max<std::uint32_t>(1, bp.line);
        const auto at = sim::debug::effectiveLine(lines, want);
        if (at == 0) {
            bp.note = sim::debug::noStatementNote(want);
            continue;
        }
        bp.effectiveLine = at;
        if (at != bp.line) bp.note = sim::debug::shiftedNote(at, bp.line);
        if (!blank(bp.condition)) {
            auto parsed = sim::parseExpression(qualifiedCondition(*project_, bp.script, bp.function, bp.condition), dialectOptions());
            if (!parsed) {
                sim::debug::addNote(bp.note, sim::debug::invalidCondition(conditionFrench(parsed.error())));
                continue;
            }
            const std::string unknown = unknownConditionName(text, bp.script, bp.function, bp.condition);
            if (!unknown.empty()) {
                sim::debug::addNote(bp.note, sim::debug::invalidCondition(unknown + " n'est pas d\xC3\xA9" "clar\xC3\xA9" "e ici"));
                continue;
            }
            st.condition = *parsed;
        }
        st.armed = bp.enabled;
        anyArmed_ = anyArmed_ || st.armed;
    }
    remarkPrograms();
    ++debugRev_;
}

// Les lignes marquees de chaque programme deja lu et de chaque fonction aux types riches, refaites tout
// de suite (un point pose pendant la pause sert des la reprise) ; aucun point : plus rien de marque.
void Runtime::remarkPrograms() {
    const auto linesOf = [this](Id script, Id function, std::uint32_t header) {
        std::vector<std::uint32_t> lines;
        for (std::size_t i = 0; i < breakpoints_.size() && i < breakStates_.size(); ++i)
            if (breakStates_[i].armed && breakpoints_[i].effectiveLine && breakpoints_[i].script == script
                && breakpoints_[i].function == function)
                lines.push_back(breakpoints_[i].effectiveLine + header);
        return lines;
    };
    if (!anyArmed_ && !marked_) return;
    for (auto& [key, prep] : programs_) {
        if (!prep.program) continue;
        const auto tag = sim::programTag(*prep.program);
        if (tag == 0 || tag > tagOwners_.size()) {
            sim::setBreakLines(*prep.program, {});
            continue;
        }
        const auto [script, function] = tagOwners_[tag - 1];
        sim::setBreakLines(*prep.program, anyArmed_ ? linesOf(script, function, 0) : std::vector<std::uint32_t>{});
    }
    // Une fonction riche, une methode, un operateur : ses lignes dans le texte que le moteur lit (apres son en-tete).
    for (const auto& [fn, id] : richOwners_)
        if (fn) sim::setBreakLines(*fn, anyArmed_ ? linesOf(kNoId, id, debugHeaderLines(id)) : std::vector<std::uint32_t>{});
    marked_ = anyArmed_;
}

// Avant chaque execution : le numero de son code, le pas a pas possible, ses lignes marquees.
void Runtime::prepareDebug(sim::Program& program, Id script, Id function, sim::RunLimits& limits) {
    if (!debugOn_) return;
    sim::setProgramTag(program, debugTag(script, function));
    limits.stepAll = &stepAll_;
    if (!anyArmed_ && !marked_) return;
    std::vector<std::uint32_t> lines;
    if (anyArmed_)
        for (std::size_t i = 0; i < breakpoints_.size() && i < breakStates_.size(); ++i)
            if (breakStates_[i].armed && breakpoints_[i].effectiveLine && breakpoints_[i].script == script
                && breakpoints_[i].function == function && (script != kNoId || function != kNoId))
                lines.push_back(breakpoints_[i].effectiveLine);
    sim::setBreakLines(program, lines);
}

// Une fonction lue a part (une fonction IHM aux types riches, une methode, un operateur) : son id, ses lignes marquees.
void Runtime::markRichFunction(const sim::Function& fn, Id function) {
    if (!debugOn_) return;
    richOwners_[&fn] = function;
    if (!anyArmed_ && !marked_) return;
    std::vector<std::uint32_t> lines;
    if (anyArmed_) {
        const std::uint32_t header = debugHeaderLines(function);   // 1.13.0 : une methode, un operateur - apres l'en-tete
        for (std::size_t i = 0; i < breakpoints_.size() && i < breakStates_.size(); ++i)
            if (breakStates_[i].armed && breakpoints_[i].effectiveLine && breakpoints_[i].script == kNoId
                && breakpoints_[i].function == function)
                lines.push_back(breakpoints_[i].effectiveLine + header);
    }
    sim::setBreakLines(fn, lines);
}

bool Runtime::debugWanted() const noexcept { return debugOn_ && (anyArmed_ || codeBreaks_ || pauseAsked_ || step_.has_value()); }

// Une fois par seconde : le projet ecrit-il IHM_POINT_ARRET quelque part (un appel, pas un commentaire ni une chaine) -
// ses scripts, ses fonctions, celles des vues, des popups et des symboles, leurs redefinitions d'instance, le code des
// actions "Execute du ST ecrit ici", les methodes et les operateurs des types, le code des bibliotheques reliees ; un
// code dont un point depend a-t-il change (son texte : il se relit, la ligne se re-decale et le dit).
void Runtime::debugTick(double now) {
    if (!debugOn_ || !project_ || now - codeBreaksAt_ < 1.0) return;
    codeBreaksAt_ = now;
    bool any = false;
    const auto script = [&](const Script& sc) { any = any || (sc.lang == ScriptLang::ST && writesCodeBreak(sc.body)); };
    const auto type = [&](const HmiType& t) {
        for (const auto& m : t.methods) any = any || writesCodeBreak(m.body);
        for (const auto& o : t.operators) any = any || writesCodeBreak(o.body);
    };
    for (const auto& sc : project_->programs.scripts) script(sc);
    for (const auto& f : project_->programs.functions) any = any || writesCodeBreak(f.body);
    for (const auto& t : project_->programs.types) type(t);
    for (const auto& v : project_->views) {
        for (const auto& sc : v.scripts) script(sc);
        for (const auto& f : v.functions) any = any || writesCodeBreak(f.body);
        for (const auto& o : v.operators) any = any || writesCodeBreak(o.body);
        for (const auto& a : v.actions)
            if (a.operation == Operation::RunScript) any = any || writesCodeBreak(a.value);
        for (const auto& ob : v.objects) {
            for (const auto& ov : ob.functionOverrides) any = any || writesCodeBreak(ov.body);
            for (const auto& a : ob.actions)                     // le code d'une action "Execute du ST ecrit ici"
                if (a.operation == Operation::RunScript) any = any || writesCodeBreak(a.value);
        }
    }
    if (!any) project_->forEachLibraryScript([&](const Script& sc, const LoadedLibrary&) { script(sc); });
    if (!any) project_->forEachLibraryFunction([&](const HmiFunction& f, const LoadedLibrary&) { any = any || writesCodeBreak(f.body); });
    if (!any) project_->forEachLibraryType([&](const HmiType& t, const LoadedLibrary&) { type(t); });
    codeBreaks_ = any;
    for (std::size_t i = 0; i < breakpoints_.size() && i < breakStates_.size(); ++i)
        if (debugCodeHash(breakpoints_[i].script, breakpoints_[i].function) != breakStates_[i].codeHash) {
            resolveBreakpoints();
            break;
        }
}

void Runtime::debugNote(std::size_t index, const std::string& note) {
    if (index >= breakpoints_.size() || index >= breakStates_.size()) return;
    if (!breakStates_[index].said.insert(note).second) return;
    sim::debug::addNote(breakpoints_[index].note, note);
    ++debugRev_;
}

// ------------------------------------------------------------------ la pile ---
std::vector<DebugFrame> Runtime::debugStack(const std::vector<sim::LineProbe::Frame>& chain,
                                            const std::vector<sim::LineProbe::Frame>& here) const {
    std::vector<DebugFrame> out;
    // Au bas : ce qui a lance le code.
    DebugFrame context;
    context.label = taskContext(currentTask_);
    out.push_back(std::move(context));
    std::vector<const sim::LineProbe::Frame*> all;
    for (const auto& f : chain) all.push_back(&f);
    for (const auto& f : here) all.push_back(&f);
    std::size_t cursor = 0;                    // les executions (debugExec_) suivent l'ordre de la pile
    for (const auto* f : all) {
        if (f->tag == 0 && !f->standalone) continue;          // une expression (un texte a trous, une fx) : pas de code
        const auto [script, function] = frameOwner(*f);
        DebugFrame d;
        d.script = script;
        d.function = function;
        d.line = frameLine(*f, f->line);
        d.locals = f->locals;
        // 1.13.0 : un operateur - son resultat sous le nom que son code lui donne (Resultat), pas son nom interne.
        if (script == kNoId && project_)
            if (const HmiOperator* o = operatorOf(*project_, function))
                for (auto& l : d.locals) l.name = operatorShownName(*o, l.name);
        std::string code = project_ ? debugCodeName(*project_, script, function) : std::string{};
        if (code.empty() && script == kNoId) code = boundCodeName(function);   // 1.13.0 : une redefinition d'instance
        if (f->function.empty()) d.label = code.empty() ? std::string("code") : code;
        else if (f->standalone) d.label = code.empty() ? "fonction " + f->function : code;
        else d.label = f->function + " (fonction interne de " + (code.empty() ? std::string("ce code") : code) + ")";
        // Une fonction IHM simple : ses locales sont dans le cadre du moteur.
        if (f->function.empty() && function != kNoId) {
            for (std::size_t k = cursor; k < debugExec_.size(); ++k) {
                if (debugExec_[k].tag != f->tag) continue;
                cursor = k + 1;
                const auto* frame = debugExec_[k].frame;
                const auto* locals = debugExec_[k].locals;
                if (!frame) break;
                d.locals.clear();
                const auto shown = [](const sim::Value& v) { return v.type() == sim::Type::String ? "'" + v.asString() + "'" : v.display(); };
                std::set<std::string> seen;
                if (locals)
                    for (const auto& l : *locals) {
                        const auto it = frame->find(upperText(l.name));
                        if (it == frame->end()) continue;
                        DebugLocal x;
                        x.name = l.name;
                        x.type = l.type;
                        x.value = shown(it->second);
                        x.parameter = l.isParameter();
                        d.locals.push_back(std::move(x));
                        seen.insert(it->first);
                    }
                const HmiFunction* hf = project_ ? functionById(*project_, function) : nullptr;
                for (const auto& [name, v] : *frame) {
                    if (seen.count(name)) continue;
                    DebugLocal x;
                    x.name = name;
                    // Le resultat (Moyenne_Pression := ...) : le nom tel qu'ecrit, son type.
                    if (hf && name == upperText(hf->name)) {
                        x.name = hf->name;
                        x.type = hf->returnType;
                    }
                    x.value = shown(v);
                    d.locals.push_back(std::move(x));
                }
                break;
            }
        }
        out.push_back(std::move(d));
    }
    return out;
}

// --------------------------------------------------------- une ligne atteinte ---
void Runtime::debugReached(std::uint32_t tag, std::uint32_t line, sim::LineProbe& probe, bool code) {
    (void)tag;
    if (!debugOn_ || debugProbing_ > 0 || !running_) return;
    if (taskStopped()) {                       // arretee, ou son code change pendant la pause : elle se defait
        probe.stop();
        return;
    }
    std::vector<sim::LineProbe::Frame> here;
    probeQuietly([&] { here = probe.frames(); });
    if (here.empty()) return;
    const auto owner = frameOwner(here.back());
    const bool known = owner.first != kNoId || owner.second != kNoId;
    const std::uint32_t at = frameLine(here.back(), line);         // la ligne du code tel que son editeur le montre

    // Pourquoi s'arreter : un point de cette ligne (sa condition vraie - un BOOL), IHM_POINT_ARRET, une pause
    // demandee, le pas en cours (dans cette tache, a la bonne profondeur). Un passage ne compte que s'il arrete.
    std::uint32_t hitId = 0;
    std::size_t hitIndex = static_cast<std::size_t>(-1);
    bool disarmed = false;
    if (!code && known) {
        for (std::size_t i = 0; i < breakpoints_.size() && i < breakStates_.size(); ++i) {
            auto& bp = breakpoints_[i];
            auto& st = breakStates_[i];
            if (!st.armed || bp.effectiveLine != at || bp.script != owner.first || bp.function != owner.second) continue;
            if (st.condition) {
                core::Result<sim::Value> v = sim::Value::boolean(false);
                probeQuietly([&] { v = probe.evaluate(*st.condition); });
                if (!v) {
                    // Invalide ici (un indice hors des bornes pour cette instance...) : il ne s'arretera plus, et le dit.
                    st.armed = false;
                    st.condition.reset();
                    debugNote(i, sim::debug::invalidCondition(conditionFrench(v.error())));
                    disarmed = true;
                    continue;
                }
                if (v->type() != sim::Type::Bool) {
                    // Pas un BOOL (Tours MOD 100, le "= 0" oublie) : comme IHM_POINT_ARRET, refusee - et dite.
                    st.armed = false;
                    st.condition.reset();
                    debugNote(i, sim::debug::invalidCondition("elle rend un " + std::string(sim::toString(v->type())) + ", pas un BOOL"));
                    disarmed = true;
                    continue;
                }
                if (!v->isTruthy()) continue;
            }
            if (hitId == 0) {
                hitId = bp.id;
                hitIndex = i;
            }
        }
    }
    if (disarmed) {
        anyArmed_ = std::any_of(breakStates_.begin(), breakStates_.end(), [](const BreakState& s) { return s.armed; });
        remarkPrograms();
    }
    DebugHit::Why why = DebugHit::Why::Breakpoint;
    bool stop = false;
    std::vector<sim::LineProbe::Frame> chain;
    bool chainRead = false;
    const auto readChain = [&] {
        if (chainRead) return;
        probeQuietly([&] { chain = sim::callerFrames(); });
        chainRead = true;
    };
    if (code) {
        stop = true;
        why = DebugHit::Why::Code;
    } else if (hitId) {
        stop = true;
    } else if (pauseAsked_ && known) {
        stop = true;
        why = DebugHit::Why::Pause;
    } else if (step_ && currentTask_ && step_->task == currentTask_ && known) {
        readChain();
        const std::size_t depth = chain.size() + here.size();
        switch (step_->step) {
            case DebugStep::Over: stop = depth <= step_->depth; break;
            case DebugStep::Into: stop = true; break;
            case DebugStep::Out:  stop = depth < step_->depth; break;
        }
        why = DebugHit::Why::Step;
    }
    if (!stop) return;

    // On ne peut pas s'arreter partout : un autre script deja en pause, une fonction appelee par une expression (elle
    // doit rendre sa valeur tout de suite, comme WAIT), un rappel (minuteur, surveillance), hors de tout travail que le
    // debogueur suit. La raison va dans la note du point (une fois), ou a la Console pour IHM_POINT_ARRET.
    const auto refuse = [&](const std::string& said) {
        if (hitIndex != static_cast<std::size_t>(-1)) {
            debugNote(hitIndex, said);
        } else if (why == DebugHit::Why::Code) {
            const std::string key = std::to_string(owner.first) + ":" + std::to_string(owner.second) + ":" + std::to_string(at);
            if (debugSaid_.insert(key).second)
                logAt(LogLevel::Warning, "D\xC3\xA9" "bogage", source_, "IHM_POINT_ARRET : " + said, static_cast<int>(at));
        }
    };
    if (readOnly_ > 0) {                       // d'abord : une expression de la vue se lit aussi pendant une pause
        refuse("appel\xC3\xA9" "e par une expression : pas d'arr\xC3\xAAt");
        return;
    }
    if (callbackDepth_ > 0) {
        refuse("dans un rappel (IHM_APRES, IHM_CHAQUE, IHM_SUR_CHANGEMENT) : pas d'arr\xC3\xAAt");
        return;
    }
    if (paused_) {
        refuse("un autre script est d\xC3\xA9j\xC3\xA0 en pause : pas d'arr\xC3\xAAt");
        return;
    }
    if (!currentTask_) {
        refuse("hors du cycle et des gestes de l'IHM en marche (l'essai d'une fonction...) : pas d'arr\xC3\xAAt");
        return;
    }

    // L'arret : ou, pourquoi, les valeurs de la ligne, la pile et ses locales. Le passage compte.
    if (hitIndex != static_cast<std::size_t>(-1)) ++breakpoints_[hitIndex].hits;
    readChain();
    DebugHit hit;
    hit.why = why;
    hit.breakpoint = hitId;
    hit.script = owner.first;
    hit.function = owner.second;
    hit.line = at;
    hit.cycle = static_cast<std::uint64_t>(cycles_);
    hit.detached = taskIsDetached(currentTask_);
    probeQuietly([&] { hit.values = probe.lineValues(); });
    // 1.13.0 : les valeurs de la ligne d'un operateur, sous les noms de son code (Resultat.X, pas OPERATEUR_ADD.X).
    const HmiOperator* pausedOperator = owner.first == kNoId && project_ ? operatorOf(*project_, owner.second) : nullptr;
    for (auto& [name, value] : hit.values) {
        if (value.empty()) {
            const bool aggregate = envOf().richVariable(name) != nullptr || this->aggregate(name) != nullptr;
            value = aggregate ? std::string("(structure ou tableau)") : std::string("?");
        } else if (variableForced(name)) {
            value += " (forc\xC3\xA9" "e)";
        }
        if (pausedOperator) name = operatorShownName(*pausedOperator, name);
    }
    hit.stack = debugStack(chain, here);
    hit.context = hit.stack.empty() ? std::string{} : hit.stack.front().label;
    std::string codeName = project_ ? debugCodeName(*project_, owner.first, owner.second) : std::string{};
    if (codeName.empty() && owner.first == kNoId) codeName = boundCodeName(owner.second);   // 1.13.0 : une redefinition
    // Un code sans nom (le code d'une action "Execute du ST ecrit ici") : sa source ("Vue_A/Btn (script de l'action)").
    const std::string shownName = !codeName.empty() ? codeName : (!source_.empty() ? source_ : std::string("code"));
    hit.where = shownName + ", ligne " + std::to_string(at);
    std::string said;
    switch (why) {
        case DebugHit::Why::Breakpoint: {
            std::size_t rank = 0;
            for (std::size_t i = 0; i < breakpoints_.size(); ++i)
                if (breakpoints_[i].id == hitId) rank = i + 1;
            said = "arr\xC3\xAAt\xC3\xA9 au point " + std::to_string(rank) + " (" + hit.where + ")";
            break;
        }
        case DebugHit::Why::Code: said = "arr\xC3\xAAt\xC3\xA9 par IHM_POINT_ARRET (" + hit.where + ")"; break;
        case DebugHit::Why::Pause: said = "en pause (" + hit.where + ")"; break;
        case DebugHit::Why::Step: said = "en pause apr\xC3\xA8s le pas (" + hit.where + ")"; break;
    }
    pausedDepth_ = chain.size() + here.size();
    debugHit_ = std::move(hit);
    paused_ = currentTask_;
    pausedProbe_ = &probe;
    pausedScript_ = owner.first;
    pausedFunction_ = owner.second;
    pausedName_ = shownName;
    pausedHash_ = debugCodeHash(owner.first, owner.second);
    step_.reset();
    pauseAsked_ = false;
    stepAll_ = false;
    ++pauseCount_;
    ++debugRev_;
    logAt(LogLevel::Info, "D\xC3\xA9" "bogage", source_, said, static_cast<int>(at));

    // La pause : le fil du travail rend la main a celui de l'IHM et attend Continuer, un pas, ou l'arret. Le temps de la
    // pause ne compte ni dans les Performances ni dans le journal des lenteurs (les mesures en cours le retranchent).
    TaskState* self = currentTask_;
    const auto pausedSince = std::chrono::steady_clock::now();
    const bool go = pauseTask(self);
    const auto pausedFor = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - pausedSince).count();
    pausedNs_ += pausedFor;
    for (auto& f : slowStack_) f.t0 += pausedFor;
    pausedProbe_ = nullptr;
    if (paused_ == self) {                     // l'arret de la simulation (stopTasks) : sans reprise
        paused_ = nullptr;
        debugHit_.reset();
    }
    if (!go) {
        probe.stop();
        return;
    }
    // Le code a change pendant la pause : il ne peut pas reprendre au milieu d'une ligne qui n'existe plus. Ce script
    // (avec ce qui l'appelle dans ce travail) s'arrete la, sans erreur ; le reste du travail continue - la suite du
    // cycle, du geste, la premiere vue d'un demarrage (pas tout le travail : abandonTask, pas cancelTask).
    if (debugCodeHash(owner.first, owner.second) != pausedHash_) {
        logAt(LogLevel::Warning, "D\xC3\xA9" "bogage", source_,
              (codeName.empty() ? std::string("le script") : codeName)
                  + " a chang\xC3\xA9 pendant la pause : son ex\xC3\xA9" "cution s'arr\xC3\xAAte l\xC3\xA0, sans erreur ; la suite du cycle "
                    "ou du geste continue (un script cyclique repart au prochain cycle)",
              static_cast<int>(at));
        abandonTask(self);
        probe.stop();
    }
}

// ---------------------------------------------------------------- la reprise ---
void Runtime::debugResume(std::optional<StepOrder> order, const std::string& said) {
    TaskState* t = paused_;
    if (!t) return;
    paused_ = nullptr;
    debugHit_.reset();
    step_ = order;
    if (step_) step_->task = t;
    stepAll_ = step_.has_value() || pauseAsked_;
    ++debugRev_;
    if (!said.empty()) logAt(LogLevel::Info, "D\xC3\xA9" "bogage", "IHM", said);
    resumePaused(t);
}

bool Runtime::debugContinue() {
    if (!paused_) return false;
    debugResume(std::nullopt, "continu\xC3\xA9");
    return true;
}

bool Runtime::debugStep(DebugStep step) {
    if (!paused_) return false;
    StepOrder order;
    order.step = step;
    order.depth = pausedDepth_;
    order.what = pausedName_;
    const char* name = step == DebugStep::Over ? "Suivant" : step == DebugStep::Into ? "Entrer" : "Sortir";
    debugResume(order, std::string("pas : ") + name);
    return true;
}

void Runtime::debugPause() {
    if (paused_ || !debugOn_) return;
    pauseAsked_ = true;
    stepAll_ = true;
    ++debugRev_;
    logAt(LogLevel::Info, "D\xC3\xA9" "bogage", "IHM", "pause demand\xC3\xA9" "e : le prochain script qui tourne s'arr\xC3\xAAtera \xC3\xA0 sa ligne");
}

// 1.13.0 : une pause demandee qui n'a rien arrete se retire - l'IHM continue sans s'arreter (un pas en cours, lui, reste).
bool Runtime::debugCancelPause() {
    if (!pauseAsked_ || paused_) return false;
    pauseAsked_ = false;
    stepAll_ = step_.has_value();
    ++debugRev_;
    logAt(LogLevel::Info, "D\xC3\xA9" "bogage", "IHM", "pause annul\xC3\xA9" "e : l'IHM continue sans s'arr\xC3\xAAter");
    return true;
}

void Runtime::clearDebugState() {
    paused_ = nullptr;
    pausedProbe_ = nullptr;
    debugHit_.reset();
    step_.reset();
    pauseAsked_ = false;
    stepAll_ = false;
    ++debugRev_;
}

bool Runtime::debugEvaluate(const std::string& expression, std::string& value, std::string* why, std::string* type) {
    // En pause : lue comme le code en pause lit ses noms (son espace de noms, ses USING).
    const std::string text = paused_ && project_ ? qualifiedCondition(*project_, pausedScript_, pausedFunction_, expression) : expression;
    auto parsed = sim::parseExpression(text, dialectOptions());
    if (!parsed) {
        if (why) *why = conditionFrench(parsed.error());
        return false;
    }
    core::Result<sim::Value> r = sim::Value::boolean(false);
    if (paused_ && pausedProbe_) {
        withTaskState(paused_, [&] { probeQuietly([&] { r = pausedProbe_->evaluate(**parsed); }); });
    } else {
        probeQuietly([&] { r = sim::evaluate(**parsed, envOf()); });
    }
    if (!r) {
        if (why) *why = conditionFrench(r.error());
        return false;
    }
    value = r->type() == sim::Type::String ? "'" + r->asString() + "'" : r->display();
    if (type) *type = std::string(sim::toString(r->type()));
    return true;
}

} // namespace hmi
