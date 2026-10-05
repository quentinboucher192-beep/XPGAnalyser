// =============================================================================
//  app/SimDebugText.cpp - lot API 8 : Simulation > Debogage, sans ecran
// -----------------------------------------------------------------------------
//  Voir SimDebugText.hpp. Tout ici est en francais et tutoie, comme l'onglet.
// =============================================================================
#include "SimDebugText.hpp"

#include "../sim/Value.hpp"
#include "../sim/Runtime.hpp"          // Lot API 8 (2e partie) : la condition lue maintenant

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace app::simdebug {

namespace {

unsigned char uc(char c) noexcept { return static_cast<unsigned char>(c); }

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(uc(c)));
    return out;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(uc(s[a]))) ++a;
    while (b > a && std::isspace(uc(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool identStart(char c) noexcept { return std::isalpha(uc(c)) || c == '_'; }
bool identChar(char c) noexcept { return std::isalnum(uc(c)) || c == '_'; }

// Un nombre decimal avec `decimals` chiffres apres la virgule, sans les zeros
// inutiles : 1,25 ; 2 ; 0,042.
std::string decimal(double x, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, x);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    std::string head = s, tail;
    if (const auto dot = s.find('.'); dot != std::string::npos) {
        head = s.substr(0, dot);
        tail = s.substr(dot + 1);
    }
    std::string sign;
    if (!head.empty() && head.front() == '-') {
        sign = "-";
        head.erase(0, 1);
    }
    const auto whole = static_cast<std::uint64_t>(std::strtoull(head.c_str(), nullptr, 10));
    return sign + grouped(whole) + (tail.empty() ? std::string{} : "," + tail);
}

// Une duree simulee, pour « il y a » : 0,8 s ; 12 s ; 3 min 05 s ; 1 h 02 min.
std::string ago(std::int64_t ms) {
    if (ms < 0) ms = 0;
    if (ms < 10000) return decimal(static_cast<double>(ms) / 1000.0, 1) + " s";
    const std::int64_t s = ms / 1000;
    if (s < 60) return std::to_string(s) + " s";
    char buf[48];
    if (s < 3600) {
        std::snprintf(buf, sizeof buf, "%lld min %02lld s", static_cast<long long>(s / 60), static_cast<long long>(s % 60));
        return buf;
    }
    std::snprintf(buf, sizeof buf, "%lld h %02lld min", static_cast<long long>(s / 3600), static_cast<long long>((s % 3600) / 60));
    return buf;
}

// « 1er », « 2e » : le rang en francais.
std::string rankText(std::uint64_t n) { return n == 1 ? "1er" : grouped(n) + "e"; }

bool isKeyword(std::string_view word) {
    static const char* const kWords[] = {
        "if", "then", "else", "elsif", "end_if", "case", "of", "end_case", "for", "to", "by", "do", "end_for",
        "while", "end_while", "repeat", "until", "end_repeat", "exit", "return", "and", "or", "xor", "not", "mod",
        "true", "false", "var", "end_var", "var_input", "var_output", "var_in_out", "var_temp", "constant",
        "at", "array", "struct", "end_struct", "type", "end_type", "jmp", "cal", "ret", "st", "ld", "ldn"};
    const auto w = lower(word);
    return std::any_of(std::begin(kWords), std::end(kWords), [&](const char* k) { return w == k; });
}

// Les indices d'un tableau ecrits en nombres : "[3]", "[2, 5]" -> "[2,5]". Faux :
// une expression (x[i + 1]) - le nom ne se lit pas tel quel.
bool constantIndex(std::string_view inside, std::string& out) {
    std::string norm;
    bool digit = false;
    for (const char c : inside) {
        if (std::isspace(uc(c))) continue;
        if (std::isdigit(uc(c))) { norm += c; digit = true; continue; }
        if (c == ',' || c == '-') { norm += c; continue; }
        return false;
    }
    if (!digit) return false;
    out = "[" + norm + "]";
    return true;
}

} // namespace

// ------------------------------------------------------------------ nombres ----
std::string grouped(std::uint64_t n) {
    const std::string digits = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += digits[i];
    }
    return out;
}

std::string millis(std::int64_t micros) {
    if (micros <= 0) return "0 ms";
    const double ms = static_cast<double>(micros) / 1000.0;
    if (ms < 1.0) return decimal(ms, 3) + " ms";
    if (ms < 10.0) return decimal(ms, 2) + " ms";
    if (ms < 100.0) return decimal(ms, 1) + " ms";
    return grouped(static_cast<std::uint64_t>(std::llround(ms))) + " ms";
}

std::string percent(double p) {
    if (!(p > 0.0)) return "0\xC2\xA0%";
    if (p < 0.1) return "< 0,1\xC2\xA0%";
    if (p < 10.0) return decimal(p, 1) + "\xC2\xA0%";
    return grouped(static_cast<std::uint64_t>(std::llround(p))) + "\xC2\xA0%";
}

std::string plural(std::uint64_t n, std::string_view one, std::string_view many) {
    return grouped(n) + " " + std::string(n == 1 ? one : many);
}

// Les valeurs, comme l'onglet Automate (SimulationPane) les ecrit.
namespace {

std::string signedGrouped(long long n) {
    const bool negative = n < 0;
    const unsigned long long a = negative ? 0ULL - static_cast<unsigned long long>(n) : static_cast<unsigned long long>(n);
    return (negative ? std::string("-") : std::string{}) + grouped(a);
}

std::string formatReal(double x) {
    if (std::isnan(x)) return "NaN";
    if (std::isinf(x)) return x > 0.0 ? "+inf" : "-inf";
    const double a = std::fabs(x);
    char buf[64];
    if (a >= 1e15 || (a > 0.0 && a < 1e-4)) {
        std::snprintf(buf, sizeof buf, "%.3E", x);
        std::string s = buf;
        std::replace(s.begin(), s.end(), '.', ',');
        return s;
    }
    if (x == std::floor(x)) return signedGrouped(static_cast<long long>(x));
    return decimal(x, a >= 1.0 ? 2 : a >= 0.01 ? 3 : 5);
}

std::string formatTime(std::int64_t ms) {
    if (ms == 0) return "T#0ms";
    std::string out = ms < 0 ? "-T#" : "T#";
    std::uint64_t v = ms < 0 ? 0ULL - static_cast<std::uint64_t>(ms) : static_cast<std::uint64_t>(ms);
    const std::pair<std::uint64_t, const char*> units[] = {
        {86400000ULL, "d"}, {3600000ULL, "h"}, {60000ULL, "m"}, {1000ULL, "s"}, {1ULL, "ms"}};
    for (const auto& [size, name] : units)
        if (v >= size) {
            out += std::to_string(v / size) + name;
            v %= size;
        }
    return out;
}

} // namespace

std::string formatValue(const sim::Value& v) {
    switch (v.type()) {
        case sim::Type::Bool:    return v.isTruthy() ? "TRUE" : "FALSE";
        case sim::Type::Real:    return formatReal(v.asReal());
        case sim::Type::Time:    return formatTime(v.asInteger());
        case sim::Type::String:  return "'" + v.asString() + "'";
        case sim::Type::Unknown: return v.display();
        default:                 return signedGrouped(static_cast<long long>(v.asInteger()));
    }
}

// ---------------------------------------------------------------- sections ----
std::string sectionKey(const domain::Project& p, domain::Index section) {
    if (section >= p.sections.size()) return {};
    const auto& s = p.sections[section];
    std::string name(p.strings.text(s.name));
    if (s.owner < p.pous.size() && p.pous[s.owner].kind == domain::PouKind::FunctionBlockType)
        return std::string(p.strings.text(p.pous[s.owner].name)) + "." + name;
    return name;
}

domain::Index findSection(const domain::Project& p, std::string_view key) {
    const auto want = lower(trim(key));
    if (want.empty()) return domain::kNoIndex;
    // Le nom entier d'abord : une section du programme (ou une section de bloc
    // dont le nom contient un point, s'il y en a).
    for (domain::Index i = 0; i < p.sections.size(); ++i)
        if (lower(p.strings.text(p.sections[i].name)) == want) return i;
    // "Bloc.Section" : la section de ce nom dont le proprietaire est ce bloc ; a
    // defaut, la seule section de ce nom.
    const auto dot = want.find('.');
    if (dot == std::string::npos) return domain::kNoIndex;
    const auto owner = want.substr(0, dot), name = want.substr(dot + 1);
    domain::Index any = domain::kNoIndex;
    std::size_t count = 0;
    for (domain::Index i = 0; i < p.sections.size(); ++i) {
        const auto& s = p.sections[i];
        if (lower(p.strings.text(s.name)) != name) continue;
        if (s.owner < p.pous.size() && lower(p.strings.text(p.pous[s.owner].name)) == owner) return i;
        any = i;
        ++count;
    }
    return count == 1 ? any : domain::kNoIndex;
}

bool sameSection(std::string_view a, std::string_view b) { return lower(trim(a)) == lower(trim(b)); }

// ------------------------------------------------------- un point d'arret tape ----
bool parseBreakpoint(std::string_view text, TypedBreakpoint& out, std::string* why) {
    out = {};
    std::string head = trim(text);
    // La condition : apres « si » (ou « if »), un mot seul.
    {
        const auto low = lower(head);
        for (const char* word : {" si ", " if "}) {
            const auto at = low.find(word);
            if (at == std::string::npos) continue;
            out.condition = trim(std::string_view(head).substr(at + 4));
            head = trim(std::string_view(head).substr(0, at));
            break;
        }
    }
    // Le numero de la ligne, a la fin.
    std::size_t end = head.size();
    std::size_t begin = end;
    while (begin > 0 && std::isdigit(uc(head[begin - 1]))) --begin;
    if (begin == end) {
        if (why) *why = "il manque le num\xC3\xA9ro de la ligne (\xC2\xAB SFC_PurgeA 42 \xC2\xBB)";
        return false;
    }
    const long line = std::strtol(head.c_str() + begin, nullptr, 10);
    std::string rest = head.substr(0, begin);
    // Ce qui separe : « : », « , », « ligne », « line », « l. », des espaces.
    for (bool again = true; again;) {
        again = false;
        while (!rest.empty() && (std::isspace(uc(rest.back())) || rest.back() == ':' || rest.back() == ',')) {
            rest.pop_back();
            again = true;
        }
        const auto low = lower(rest);
        for (const char* word : {"ligne", "line", "l."}) {
            const std::string_view w(word);
            if (low.size() < w.size() || low.compare(low.size() - w.size(), w.size(), w) != 0) continue;
            const std::size_t at = low.size() - w.size();
            // Un mot a lui : precede d'un separateur (ou de rien, pour « l. »).
            const bool alone = at == 0 || std::isspace(uc(rest[at - 1])) || rest[at - 1] == ',' || rest[at - 1] == ':';
            if (!alone) continue;
            rest.erase(at);
            again = true;
            break;
        }
    }
    rest = trim(rest);
    if (rest.empty()) {
        if (why) *why = "il manque le nom de la section (\xC2\xAB SFC_PurgeA 42 \xC2\xBB)";
        return false;
    }
    for (const char c : rest)
        if (!identChar(c) && c != '.') {
            if (why) *why = "le nom de la section n'est pas un nom : \xC2\xAB " + rest + " \xC2\xBB";
            return false;
        }
    if (line < 1) {
        if (why) *why = "les lignes commencent \xC3\xA0 1";
        return false;
    }
    out.section = rest;
    out.line = static_cast<int>(std::min<long>(line, 1000000L));
    return true;
}

// ------------------------------------------------------------------- la pile ----
std::vector<StackLevel> stackLevels(const std::vector<std::string>& stack, const std::string& hitSection) {
    std::vector<StackLevel> out;
    if (stack.empty()) {
        if (hitSection.empty()) return out;
        StackLevel s;
        s.kind = StackLevel::Kind::Section;
        s.label = s.name = s.section = hitSection;
        out.push_back(std::move(s));
        return out;
    }
    // Une instance s'ecrit « nom (TYPE) ».
    const auto parseInstance = [](const std::string& label, std::string& name, std::string& type) {
        const auto open = label.rfind(" (");
        if (open == std::string::npos || label.size() < open + 4 || label.back() != ')') return false;
        name = trim(std::string_view(label).substr(0, open));
        type = trim(std::string_view(label).substr(open + 2, label.size() - open - 3));
        return !name.empty() && !type.empty();
    };
    std::size_t firstInstance = stack.size();
    for (std::size_t i = 0; i < stack.size() && firstInstance == stack.size(); ++i) {
        std::string n, t;
        if (parseInstance(stack[i], n, t)) firstInstance = i;
    }
    const std::size_t sectionAt = firstInstance > 0 ? firstInstance - 1 : static_cast<std::size_t>(-1);
    std::string instancePath, instanceType;
    bool afterInstance = false;
    for (std::size_t i = 0; i < stack.size(); ++i) {
        StackLevel l;
        l.label = l.name = stack[i];
        std::string n, t;
        if (i == sectionAt) {
            l.kind = StackLevel::Kind::Section;
            l.section = stack[i];
            afterInstance = false;
        } else if (i < sectionAt) {
            l.kind = i == 0 ? StackLevel::Kind::Task : StackLevel::Kind::Unit;
        } else if (parseInstance(stack[i], n, t)) {
            l.kind = StackLevel::Kind::Instance;
            l.name = n;
            l.type = t;
            instancePath = instancePath.empty() ? n : instancePath + "." + n;
            instanceType = t;
            l.instance = instancePath;
            // Un clic montre le code du bloc : la section qui suit dans la pile.
            if (i + 1 < stack.size()) {
                std::string n2, t2;
                if (!parseInstance(stack[i + 1], n2, t2)) l.section = t + "." + stack[i + 1];
            }
            afterInstance = true;
        } else if (afterInstance) {
            l.kind = StackLevel::Kind::BlockSection;
            l.type = instanceType;
            l.section = instanceType + "." + stack[i];
            l.instance = instancePath;
            afterInstance = false;
        } else {
            l.kind = StackLevel::Kind::Other;
        }
        out.push_back(std::move(l));
    }
    return out;
}

std::string levelTip(const StackLevel& l) {
    switch (l.kind) {
        case StackLevel::Kind::Task:
            return "La t\xC3\xA2" "che " + l.name + " : un clic ouvre son ordre d'ex\xC3\xA9" "cution.";
        case StackLevel::Kind::Unit:
            return "L'unit\xC3\xA9 de programme " + l.name + " : un clic la montre dans l'ordre d'ex\xC3\xA9" "cution.";
        case StackLevel::Kind::Section:
            return "La section " + l.name + " : un clic montre son code ici.";
        case StackLevel::Kind::Instance:
            return "L'instance " + l.name + " du bloc " + l.type + " : un clic montre le code du bloc, tel qu'elle l'ex\xC3\xA9" "cute.";
        case StackLevel::Kind::BlockSection:
            return "La section " + l.name + " du bloc " + l.type + ", ex\xC3\xA9" "cut\xC3\xA9" "e pour l'instance " + l.instance
                 + " : un clic montre son code ici.";
        case StackLevel::Kind::Other:
            break;
    }
    return l.label;
}

// ------------------------------------------------------------- l'etat en clair ----
std::string stateSentence(const StateFacts& f) {
    using S = SimulationHost::State;
    const std::string dash = " \xE2\x80\x94 ";
    const std::string cycle = grouped(f.cycle);
    if (!f.attached)
        return "Pas encore lanc\xC3\xA9" "e" + dash + "Continuer (F5) pr\xC3\xA9pare le programme de MAST et le lance";
    switch (f.state) {
        case S::Stopped: {
            std::string s = "Arr\xC3\xAAt\xC3\xA9" "e" + dash + "Continuer (F5) lance le programme depuis les valeurs initiales";
            if (f.active > 0) s += " ; " + plural(f.active, "point d'arr\xC3\xAAt l'attend", "points d'arr\xC3\xAAt l'attendent");
            return s;
        }
        case S::Running: {
            std::string s = "En marche, cycle " + cycle;
            if (!f.runToLine.empty()) return s + dash + "jusqu'\xC3\xA0 " + f.runToLine;
            if (f.active > 0) return s + dash + plural(f.active, "point d'arr\xC3\xAAt actif", "points d'arr\xC3\xAAt actifs");
            return s + dash + "aucun point d'arr\xC3\xAAt : un clic dans la marge du code en pose un";
        }
        case S::Halted:
            return "Halte au cycle " + cycle + (f.haltMessage.empty() ? std::string{} : " : " + f.haltMessage);
        case S::Paused:
            break;
    }
    if (f.hit) {
        const std::string where = f.hit->section + ", ligne " + std::to_string(f.hit->line) + ", cycle " + grouped(f.hit->scan);
        if (f.breakpoint) {
            std::string s = "En pause au point d'arr\xC3\xAAt " + std::to_string(f.number ? f.number : f.breakpoint->id) + " : " + where;
            if (!f.breakpoint->condition.empty()) s += dash + "la condition " + f.breakpoint->condition + " est vraie";
            return s;
        }
        if (f.gesture == LastGesture::RunToLine) return "En pause \xC3\xA0 la ligne demand\xC3\xA9" "e : " + where;
        return "En pause : " + where;
    }
    switch (f.gesture) {
        case LastGesture::StepSection:
            return "En pause apr\xC3\xA8s une section, cycle " + cycle
                 + (f.nextSection.empty() ? dash + "le cycle est fini" : dash + "la suivante : " + f.nextSection);
        case LastGesture::StepCycle:
            return "En pause \xC3\xA0 la fin du cycle " + cycle;
        default:
            break;
    }
    return "En pause, cycle " + cycle + (f.nextSection.empty() ? std::string{} : dash + "prochaine section : " + f.nextSection);
}

std::vector<std::string> whyHere(const StateFacts& f) {
    using S = SimulationHost::State;
    std::vector<std::string> out;
    const std::string buttons =
        "Section suivante (F10) ex\xC3\xA9" "cute une section puis s'arr\xC3\xAAte ; Cycle suivant (F11) va jusqu'\xC3\xA0 la fin du cycle ; "
        "Continuer (F5) repart jusqu'au prochain point d'arr\xC3\xAAt.";
    // La maquette (en pause) : la modification en ligne reprend au meme cycle.
    const char* const kModifyNow = "Tu peux modifier le projet maintenant : la simulation reprendra ici, pas au cycle 0.";
    if (!f.attached || f.state == S::Stopped) {
        out.push_back("La simulation est arr\xC3\xAAt\xC3\xA9" "e : rien ne s'ex\xC3\xA9" "cute.");
        out.push_back(f.total > 0
                          ? "Continuer (F5) lance le programme de MAST ; il tournera jusqu'au premier point d'arr\xC3\xAAt qui l'arr\xC3\xAAte."
                          : "Pose un point d'arr\xC3\xAAt (un clic dans la marge du code, ou F9 sur la ligne du curseur), puis Continuer (F5) : "
                            "le programme tournera jusqu'\xC3\xA0 cette ligne.");
        return out;
    }
    if (f.state == S::Halted) {
        out.push_back("Le simulateur s'est arr\xC3\xAAt\xC3\xA9 sur une erreur" + (f.haltMessage.empty() ? std::string(".") : " : " + f.haltMessage + "."));
        out.push_back("Ce n'est pas un point d'arr\xC3\xAAt : l'\xC3\xA9tat n'est plus s\xC3\xBBr. Arr\xC3\xAAter repart des valeurs initiales.");
        return out;
    }
    if (f.state == S::Running) {
        out.push_back("Le programme tourne, cycle apr\xC3\xA8s cycle.");
        if (!f.runToLine.empty()) out.push_back("Il s'arr\xC3\xAAtera en arrivant \xC3\xA0 " + f.runToLine + ".");
        else if (f.active > 0) out.push_back("Il s'arr\xC3\xAAtera au premier des " + plural(f.active, "point d'arr\xC3\xAAt actif", "points d'arr\xC3\xAAt actifs") + " qu'il atteindra (et dont la condition est vraie).");
        else out.push_back("Pour l'arr\xC3\xAAter sur une ligne : un clic dans la marge du code, ici ou dans l'onglet d'une section (F9 sur la ligne du curseur).");
        out.push_back("Pause l'arr\xC3\xAAte entre deux sections, l\xC3\xA0 o\xC3\xB9 il en est.");
        return out;
    }
    // En pause.
    if (f.hit) {
        const std::string line = "ligne " + std::to_string(f.hit->line);
        if (f.breakpoint) {
            const auto number = std::to_string(f.number ? f.number : f.breakpoint->id);
            std::string first = "Le point d'arr\xC3\xAAt " + number + " est pos\xC3\xA9 sur " + f.hit->section + ", " + line + ".";
            if (f.breakpoint->hits == 1) first += " C'est la premi\xC3\xA8re fois qu'il arr\xC3\xAAte le programme.";
            else if (f.breakpoint->hits > 1) first += " Il l'arr\xC3\xAAte pour la " + rankText(f.breakpoint->hits) + " fois.";
            out.push_back(first);
            if (!f.breakpoint->condition.empty()) {
                out.push_back("Sa condition \xC2\xAB " + f.breakpoint->condition + " \xC2\xBB est vraie \xC3\xA0 ce passage : sans elle, le programme aurait continu\xC3\xA9.");
                // La maquette : "C'est vrai : Armoires[0].etat vaut 3" - les noms de la
                // condition que la ligne lit aussi (ses valeurs au passage), mot entier.
                const auto cond = lower(f.breakpoint->condition);
                const auto part = [](char ch) {
                    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '.' || ch == '[' || ch == ']';
                };
                std::string said;
                for (const auto& [name, value] : f.hit->values) {
                    const auto want = lower(name);
                    for (auto at = want.empty() ? std::string::npos : cond.find(want); at != std::string::npos; at = cond.find(want, at + 1)) {
                        const auto end = at + want.size();
                        if ((at > 0 && part(cond[at - 1])) || (end < cond.size() && part(cond[end]))) continue;
                        said += (said.empty() ? std::string() : ", ") + name + " vaut " + value;
                        break;
                    }
                }
                if (!said.empty()) out.push_back("C'est vrai : " + said + ".");
            } else {
                out.push_back("Il n'a pas de condition : le programme s'arr\xC3\xAAte \xC3\xA0 chaque passage sur cette ligne.");
                out.push_back("Pour ne t'arr\xC3\xAAter que quand \xC3\xA7" "a t'int\xC3\xA9resse, donne-lui une condition : dans la liste des points d'arr\xC3\xAAt,"
                              " ou clic droit sur le point rouge.");   // Lot API 8 (2e partie) : le clic droit
            }
            if (!f.breakpoint->note.empty()) out.push_back("\xC3\x80 savoir : " + f.breakpoint->note + ".");
        } else if (f.gesture == LastGesture::RunToLine) {
            out.push_back("Tu as demand\xC3\xA9 d'ex\xC3\xA9" "cuter jusqu'\xC3\xA0 " + f.hit->section + ", " + line + " : le programme y est arriv\xC3\xA9.");
        } else {
            out.push_back("Le programme est en pause sur " + f.hit->section + ", " + line + ".");
        }
        out.push_back("Les valeurs \xC3\xA0 droite du code sont celles de ce moment du cycle " + grouped(f.hit->scan) + ".");
        // Un bloc : pour quelle instance, appele d'ou.
        const auto levels = stackLevels(f.hit->stack, f.hit->section);
        for (std::size_t i = levels.size(); i-- > 0;)
            if (levels[i].kind == StackLevel::Kind::Instance) {
                std::string caller;
                for (std::size_t j = i; j-- > 0;)
                    if (levels[j].kind == StackLevel::Kind::Section || levels[j].kind == StackLevel::Kind::BlockSection) {
                        caller = levels[j].name;
                        break;
                    }
                out.push_back("Ce code est celui du bloc " + levels[i].type + ", ex\xC3\xA9" "cut\xC3\xA9 pour l'instance " + levels[i].instance
                              + (caller.empty() ? std::string(".") : ", appel\xC3\xA9" "e depuis " + caller + "."));
                break;
            }
        out.push_back(buttons);
        out.push_back(kModifyNow);
        return out;
    }
    switch (f.gesture) {
        case LastGesture::StepSection:
            out.push_back("Tu avances section par section : la pr\xC3\xA9" "c\xC3\xA9" "dente vient de s'ex\xC3\xA9" "cuter en entier.");
            break;
        case LastGesture::StepCycle:
            out.push_back("Tu avances cycle par cycle : le cycle " + grouped(f.cycle) + " vient de se terminer.");
            break;
        default:
            out.push_back("Le programme est en pause entre deux sections (Pause, ou un pas \xC3\xA0 pas).");
            break;
    }
    out.push_back(f.nextSection.empty() ? "Le prochain cycle commencera par la premi\xC3\xA8re entr\xC3\xA9" "e de MAST."
                                        : "La prochaine section \xC3\xA0 s'ex\xC3\xA9" "cuter : " + f.nextSection + ".");
    out.push_back(buttons);
    out.push_back(kModifyNow);
    return out;
}

// ---------------------------------------------------------- le temps du cycle ----
std::int64_t totalMicros(const std::vector<SimSectionTime>& times) {
    std::int64_t total = 0;
    for (const auto& t : times) total += std::max<std::int64_t>(0, t.micros);
    return total;
}

std::vector<TimeBar> timeBars(const std::vector<SimSectionTime>& times, std::int64_t periodMs) {
    const auto total = totalMicros(times);
    std::vector<TimeBar> out;
    out.reserve(times.size());
    for (const auto& t : times) {
        TimeBar b;
        b.entry = t.entry;
        b.section = t.section;
        b.label = t.entry.empty() || sameSection(t.entry, t.section) ? t.section : t.entry + " \xE2\x80\xBA " + t.section;
        b.micros = std::max<std::int64_t>(0, t.micros);
        b.statements = t.statements;
        if (periodMs > 0) b.ofPeriod = static_cast<double>(b.micros) * 100.0 / (static_cast<double>(periodMs) * 1000.0);
        if (total > 0) b.ofCycle = static_cast<double>(b.micros) * 100.0 / static_cast<double>(total);
        out.push_back(std::move(b));
    }
    std::stable_sort(out.begin(), out.end(), [](const TimeBar& a, const TimeBar& b) { return a.micros > b.micros; });
    return out;
}

std::string cycleSummary(const std::vector<SimSectionTime>& times, std::int64_t periodMs) {
    if (times.empty()) return {};
    const auto total = totalMicros(times);
    std::string s = "Le cycle : " + millis(total);
    if (periodMs > 0) {
        const double share = static_cast<double>(total) * 100.0 / (static_cast<double>(periodMs) * 1000.0);
        s += " sur " + grouped(static_cast<std::uint64_t>(periodMs)) + " ms (" + percent(share) + ")";
        if (share > 100.0) s += " \xE2\x80\x94 plus long que la p\xC3\xA9riode de MAST";
    }
    const auto bars = timeBars(times, periodMs);
    if (!bars.empty() && bars.front().micros > 0) s += " \xC2\xB7 la plus lente : " + bars.front().label + ", " + millis(bars.front().micros);
    return s;
}

// ------------------------------------------------------------ la trace du cycle ----
std::vector<TraceRow> traceRows(const std::vector<SimSectionTime>& times) {
    std::vector<TraceRow> out;
    out.reserve(times.size());
    for (std::size_t i = 0; i < times.size(); ++i) {
        TraceRow r;
        r.rank = i + 1;
        r.entry = times[i].entry.empty() ? times[i].section : times[i].entry;
        r.section = times[i].section;
        r.statements = times[i].statements;
        r.micros = std::max<std::int64_t>(0, times[i].micros);
        r.firstOfEntry = i == 0 || !sameSection(r.entry, out.back().entry);
        r.active = times[i].active;
        r.condition = times[i].condition;
        out.push_back(std::move(r));
    }
    return out;
}

// ------------------------------------------------------ les noms d'une ligne ----
std::vector<std::string> lineSymbols(std::string_view line, bool& inComment) {
    std::vector<std::string> out;
    std::vector<std::string> seen;           // en minuscules
    const auto add = [&](const std::string& path) {
        const auto key = lower(path);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) return;
        seen.push_back(key);
        out.push_back(path);
    };
    const std::size_t n = line.size();
    std::size_t i = 0;
    int depth = 0;                            // les parentheses (les parametres d'un appel)
    const auto skipSpaces = [&](std::size_t k) {
        while (k < n && std::isspace(uc(line[k]))) ++k;
        return k;
    };
    while (i < n) {
        if (inComment) {
            const auto close = line.find("*)", i);
            if (close == std::string_view::npos) return out;
            inComment = false;
            i = close + 2;
            continue;
        }
        const char c = line[i];
        if (c == '(' && i + 1 < n && line[i + 1] == '*') { inComment = true; i += 2; continue; }
        if (c == '/' && i + 1 < n && line[i + 1] == '/') break;
        if (c == '\'' || c == '"') {
            // Une chaine : jusqu'au guillemet qui la ferme ($' est un guillemet ecrit).
            std::size_t k = i + 1;
            while (k < n && line[k] != c) k += line[k] == '$' ? 2 : 1;
            i = k + 1;
            continue;
        }
        if (std::isdigit(uc(c))) {
            // Un nombre, un litteral type (16#FF, 2#1010, 1.5E3, T#2s500ms).
            while (i < n && (identChar(line[i]) || line[i] == '#' || line[i] == '.')) ++i;
            continue;
        }
        if (c == '(') { ++depth; ++i; continue; }
        if (c == ')') { if (depth > 0) --depth; ++i; continue; }
        if (c == '%' || identStart(c)) {
            // Un chemin : nom(.nom | [indices])* ; une adresse %MW10, %I0.3.
            std::size_t k = i;
            std::string path;
            if (c == '%') {
                path += c;
                ++k;
                while (k < n && (identChar(line[k]) || line[k] == '.')) path += line[k++];
                i = k;
                if (path.size() > 1) add(path);
                continue;
            }
            while (k < n && identChar(line[k])) path += line[k++];
            // Un litteral type : T#2s, TIME#1h, DT#... - pas un nom.
            if (k < n && line[k] == '#') {
                ++k;
                while (k < n && (identChar(line[k]) || line[k] == '#' || line[k] == '.' || line[k] == ':' || line[k] == '-')) ++k;
                i = k;
                continue;
            }
            bool broken = false;              // un indice calcule : le chemin ne se lit pas tel quel
            std::vector<std::string_view> computed;        // ces indices (x[i + 1] : i + 1), lus a part
            for (;;) {
                if (k + 1 < n && line[k] == '.' && identStart(line[k + 1])) {
                    std::string part;
                    ++k;
                    while (k < n && identChar(line[k])) part += line[k++];
                    if (!broken) path += "." + part;
                    continue;
                }
                if (k < n && line[k] == '[') {
                    int level = 0;
                    std::size_t close = k;
                    for (; close < n; ++close) {
                        if (line[close] == '[') ++level;
                        else if (line[close] == ']' && --level == 0) break;
                    }
                    if (close >= n) { broken = true; break; }
                    std::string index;
                    if (!broken && constantIndex(line.substr(k + 1, close - k - 1), index)) {
                        path += index;
                    } else {
                        computed.push_back(line.substr(k + 1, close - k - 1));
                        broken = true;
                    }
                    k = close + 1;
                    continue;
                }
                break;
            }
            const std::size_t after = skipSpaces(k);
            const bool call = after < n && line[after] == '(' && !(after + 1 < n && line[after + 1] == '*');
            const bool formal = depth > 0 && after + 1 < n
                             && ((line[after] == ':' && line[after + 1] == '=') || (line[after] == '=' && line[after + 1] == '>'));
            if (!broken && !call && !formal && !isKeyword(path)) add(path);
            // Les noms d'un indice calcule (x[i + 1] : i) se lisent a part.
            for (const auto expr : computed) {
                bool nested = false;
                for (const auto& name : lineSymbols(expr, nested)) add(name);
            }
            i = k;
            continue;
        }
        ++i;
    }
    return out;
}

// ----------------------------------------------------------------- les espions ----
std::string sinceText(bool everChanged, std::uint64_t changedScan, std::uint64_t nowScan, std::int64_t periodMs) {
    if (!everChanged) return "inchang\xC3\xA9" "e depuis l'ajout";
    if (changedScan >= nowScan) return "vient de changer (ce cycle)";
    const auto cycles = nowScan - changedScan;
    std::string s = "chang\xC3\xA9" "e au cycle " + grouped(changedScan) + " (il y a " + plural(cycles, "cycle", "cycles");
    if (periodMs > 0) s += ", " + ago(static_cast<std::int64_t>(cycles) * periodMs);
    return s + ")";
}

std::string writeText(const SimLastWrite& w, std::uint64_t nowScan) {
    std::string s = w.section + ", ligne " + std::to_string(w.line) + ", au cycle " + grouped(w.scan);
    if (w.scan == nowScan) s += " (ce cycle)";
    else if (w.scan < nowScan) s += " (il y a " + plural(nowScan - w.scan, "cycle", "cycles") + ")";
    return s;
}

std::string inactiveSectionText(const std::vector<SimSectionTime>& times, std::string_view unit, std::string_view section) {
    // La trace nomme l'entree : l'unite (une unite a son rang, en bloc), sinon la
    // section elle-meme (une section de tache, une unite sans rang). L'unite
    // d'abord ; sinon l'entree du nom de la section.
    std::vector<const SimSectionTime*> found;
    if (!unit.empty())
        for (const auto& t : times)
            if (sameSection(t.entry, unit) && sameSection(t.section, section)) found.push_back(&t);
    if (found.empty())
        for (const auto& t : times)
            if (sameSection(t.entry, section) && sameSection(t.section, section)) found.push_back(&t);
    if (found.empty()) return {};
    for (const auto* t : found)
        if (t->active || t->condition.empty() || t->condition != found.front()->condition) return {};
    return "section inactive (condition fausse : " + found.front()->condition + ")";
}

// ---- Lot API 8 (2e partie) : la condition d'un point d'arret, les espions tapes ----
std::string stLiteral(std::string_view shown) {
    std::string s = trim(shown);
    // " (forcee)" et toute precision entre parentheses a la fin : la valeur seule.
    if (const auto paren = s.find(" ("); paren != std::string::npos && paren > 0) s = trim(std::string_view(s).substr(0, paren));
    if (s.empty() || s == "?" || s.front() == '(') return {};
    const auto low = lower(s);
    if (low == "true" || low == "false") return low == "true" ? "TRUE" : "FALSE";
    if (s.size() > 2 && (low.rfind("t#", 0) == 0 || low.rfind("time#", 0) == 0)) return s;
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') return s;
    // Un nombre ecrit en francais : les espaces de milliers (normales, insecables,
    // fines) s'en vont, la virgule devient un point.
    std::string n;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == ' ') continue;
        if (uc(c) == 0xC2 && i + 1 < s.size() && uc(s[i + 1]) == 0xA0) { ++i; continue; }
        if (uc(c) == 0xE2 && i + 2 < s.size() && uc(s[i + 1]) == 0x80 && (uc(s[i + 2]) == 0xAF || uc(s[i + 2]) == 0x89)) { i += 2; continue; }
        n += c == ',' ? '.' : c;
    }
    std::size_t i = 0;
    if (i < n.size() && (n[i] == '-' || n[i] == '+')) ++i;
    bool digits = false, dot = false, exp = false;
    for (; i < n.size(); ++i) {
        const char c = n[i];
        if (std::isdigit(uc(c))) digits = true;
        else if (c == '.' && !dot && !exp) dot = true;
        else if ((c == 'e' || c == 'E') && digits && !exp) {
            exp = true;
            if (i + 1 < n.size() && (n[i + 1] == '-' || n[i + 1] == '+')) ++i;
        } else return {};
    }
    return digits ? n : std::string{};
}

std::vector<std::string> conditionIdeas(const std::vector<std::pair<std::string, std::string>>& values, std::size_t max) {
    std::vector<std::string> out;
    const auto add = [&](std::string idea) {
        if (out.size() >= max) return;
        for (const auto& o : out)
            if (lower(o) == lower(idea)) return;
        out.push_back(std::move(idea));
    };
    for (const auto& [name, shown] : values) {
        const auto lit = stLiteral(shown);
        if (trim(name).empty() || lit.empty()) continue;
        if (lit == "TRUE" || lit == "FALSE") {
            add(name + " = TRUE");
            add(name + " = FALSE");
            continue;
        }
        add(name + " = " + lit);
        const bool number = std::isdigit(uc(lit.back())) && lit.front() != '\'' && lower(lit).rfind("t#", 0) != 0;
        if (number) add(name + " > " + lit);
    }
    return out;
}

std::string suggestedCondition(const std::vector<std::pair<std::string, std::string>>& values) {
    // Un entier d'abord (un etat, une etape : « etat = 6 », comme la maquette),
    // sinon la premiere valeur utilisable.
    for (const auto& [name, shown] : values) {
        const auto lit = stLiteral(shown);
        const bool integer = !lit.empty() && std::all_of(lit.begin() + ((lit[0] == '-' || lit[0] == '+') ? 1 : 0), lit.end(),
                                                         [](char c) { return std::isdigit(uc(c)) != 0; });
        if (!trim(name).empty() && integer && lit != "-" && lit != "+") return trim(name) + " = " + lit;
    }
    for (const auto& [name, shown] : values) {
        const auto lit = stLiteral(shown);
        if (!trim(name).empty() && !lit.empty()) return trim(name) + " = " + lit;
    }
    return {};
}

std::string conditionTitle(std::size_t number, const std::string& section, int line) {
    return "Condition du point d'arr\xC3\xAAt " + std::to_string(number) + " : " + section + ", ligne " + std::to_string(line);
}

std::vector<std::string> namesStartingWith(const std::vector<std::string>& names, std::string_view typed, std::size_t max) {
    const auto want = lower(trim(typed));
    std::vector<std::string> out;
    if (want.empty()) return out;
    for (const auto& n : names)
        if (lower(n).rfind(want, 0) == 0) out.push_back(n);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        const auto la = lower(a), lb = lower(b);
        return la != lb ? la < lb : a < b;
    });
    out.erase(std::unique(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return lower(a) == lower(b); }), out.end());
    if (out.size() > max) out.resize(max);
    return out;
}

std::string unknownName(std::string_view typed) {
    return "\xC2\xAB " + trim(typed) + " \xC2\xBB n'existe pas dans le projet.";
}

std::vector<ConditionName> conditionNames(std::string_view c, bool& call) {
    std::vector<ConditionName> out;
    call = false;
    const auto n = c.size();
    std::size_t i = 0;
    while (i < n) {
        const char ch = c[i];
        if (ch == '\'' || ch == '"') {                                  // une chaine
            const auto close = c.find(ch, i + 1);
            i = close == std::string_view::npos ? n : close + 1;
            continue;
        }
        if (ch == '(' && i + 1 < n && c[i + 1] == '*') {                // un commentaire
            const auto close = c.find("*)", i + 2);
            i = close == std::string_view::npos ? n : close + 2;
            continue;
        }
        if (std::isdigit(uc(ch))) {                                      // un nombre : 3, 4.5, 16#FF, 1.5E3
            while (i < n && (identChar(c[i]) || c[i] == '.' || c[i] == '#')) ++i;
            continue;
        }
        if (!identStart(ch)) { ++i; continue; }
        std::size_t j = i;
        while (j < n && identChar(c[j])) ++j;
        if (j < n && c[j] == '#') {                                      // un litteral type : T#1s, INT#3
            while (j < n && (identChar(c[j]) || c[j] == '#' || c[j] == '.' || c[j] == ':' || c[j] == '-')) ++j;
            i = j;
            continue;
        }
        for (;;) {                                                        // .membre, [indice]
            if (j + 1 < n && c[j] == '.' && identStart(c[j + 1])) {
                ++j;
                while (j < n && identChar(c[j])) ++j;
            } else if (j < n && c[j] == '[') {
                int depth = 0;
                std::size_t k = j;
                for (; k < n; ++k) {
                    if (c[k] == '[') ++depth;
                    else if (c[k] == ']' && --depth == 0) break;
                }
                if (k >= n) break;
                j = k + 1;
            } else {
                break;
            }
        }
        std::string word(c.substr(i, j - i));
        std::string up = word;
        for (auto& x : up) x = static_cast<char>(std::toupper(uc(x)));
        static const char* const kWords[] = {"AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE"};
        if (std::none_of(std::begin(kWords), std::end(kWords), [&](const char* w) { return up == w; })) {
            std::size_t k = j;                                          // NOT (a = b) n'est pas un appel
            while (k < n && std::isspace(uc(c[k]))) ++k;
            if (k < n && c[k] == '(' && !(k + 1 < n && c[k + 1] == '*')) call = true;   // (* un commentaire *) : non
            out.push_back({i, j, std::move(word)});
        }
        i = j;
    }
    return out;
}

namespace {

// Ce que dit le moteur d'une condition qui ne va pas, en francais (comme les
// notes des points d'arret).
std::string conditionTrouble(const core::Error& e) {
    const std::string& m = e.context;
    if (m.size() > 2 && m.front() == '\'') {
        if (const auto close = m.find('\'', 1); close != std::string::npos) {
            const auto name = m.substr(1, close - 1);
            const std::string_view rest = std::string_view(m).substr(close + 1);
            if (rest == " is not declared") return "\xC2\xAB " + name + " \xC2\xBB n'existe pas ici";
            if (rest.rfind(" is not a function", 0) == 0) return name + " n'est pas une fonction que le simulateur conna\xC3\xAEt";
        }
    }
    if (m.rfind("line ", 0) == 0) return "il faut du ST (x = 3, x > 4.5 AND y)";
    if (m == "division by zero" || m == "modulo by zero") return "division par z\xC3\xA9ro";
    return m.empty() ? std::string("elle ne se lit pas") : m;
}

} // namespace

std::string conditionPreview(sim::Runtime* rt, const std::vector<std::string>& prefixes, std::string_view condition) {
    const std::string c = trim(condition);
    if (c.empty()) return "Sans condition : la simulation s'arr\xC3\xAAtera \xC3\xA0 chaque cycle.";
    const auto parsed = sim::parseExpression(c);
    if (!parsed) return "Elle ne se lit pas : " + conditionTrouble(parsed.error()) + ".";
    bool call = false;
    const auto names = conditionNames(c, call);
    if (call) return "Elle appelle une fonction : la simulation la lira au passage, pas ici.";
    if (!rt) return "La simulation n'est pas pr\xC3\xAAte : la condition sera lue au premier passage.";
    // Chaque nom court complete du prefixe qui le trouve ; le premier connu dit sa valeur.
    std::string rewritten, first;
    std::size_t at = 0;
    for (const auto& name : names) {
        std::string path = name.name;
        sim::Value v;
        bool known = false;
        for (const auto& prefix : prefixes)
            if (rt->get(prefix + name.name, v)) {
                path = prefix + name.name;
                known = true;
                break;
            }
        if (known && first.empty()) first = name.name + " = " + formatValue(v);
        rewritten += c.substr(at, name.from - at) + path;
        at = name.to;
    }
    rewritten += c.substr(at);
    const auto expr = sim::parseExpression(rewritten);
    if (!expr) return "Elle ne se lit pas : " + conditionTrouble(expr.error()) + ".";
    const auto value = sim::evaluate(**expr, *rt);
    if (!value) return "Maintenant, elle ne se lit pas : " + conditionTrouble(value.error()) + ".";
    const sim::Value& v = *value;
    if (v.type() != sim::Type::Bool) return "Ce n'est pas vrai ou faux : elle vaut " + formatValue(v) + ".";
    return std::string("Maintenant, c'est ") + (v.isTruthy() ? "vrai" : "faux") + (first.empty() ? std::string{} : " (" + first + ")") + ".";
}
// ---- fin Lot API 8 (2e partie) ----

} // namespace app::simdebug
