// =============================================================================
//  project/IoCheck.cpp - lot API 4
// =============================================================================
#include "IoCheck.hpp"

#include "../domain/ExecutionOrder.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <optional>
#include <map>
#include <set>
#include <tuple>

namespace project::io {

using namespace domain;

namespace {

// Le code sans ses commentaires (* ... *) ni ses chaines : remplaces par des
// blancs (les positions restent).
std::string stripped(std::string_view code) {
    std::string out(code);
    std::size_t i = 0;
    while (i < out.size()) {
        if (out[i] == '(' && i + 1 < out.size() && out[i + 1] == '*') {
            const auto end = out.find("*)", i + 2);
            const auto stop = end == std::string::npos ? out.size() : end + 2;
            for (auto k = i; k < stop; ++k) if (out[k] != '\n') out[k] = ' ';
            i = stop;
            continue;
        }
        if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '/') {
            while (i < out.size() && out[i] != '\n') out[i++] = ' ';
            continue;
        }
        if (out[i] == '\'' || out[i] == '"') {
            const char q = out[i];
            auto k = i + 1;
            while (k < out.size() && out[k] != q && out[k] != '\n') ++k;
            const auto stop = std::min(out.size(), k + 1);
            for (auto m = i; m < stop; ++m) if (out[m] != '\n') out[m] = ' ';
            i = stop;
            continue;
        }
        ++i;
    }
    return out;
}

struct Token {
    std::string       area;      // "I", "IW", "Q", "QW", "MW", "MD", "MF"...
    std::vector<long> numbers;
    std::size_t       end{0};    // position apres le jeton
};

// Un jeton d'adresse a partir de text[i] == '%'.
bool readToken(const std::string& text, std::size_t i, Token& t) {
    std::size_t k = i + 1;
    while (k < text.size() && std::isalpha(static_cast<unsigned char>(text[k]))) t.area += static_cast<char>(std::toupper(static_cast<unsigned char>(text[k++])));
    if (t.area.empty() || t.area.size() > 3) return false;
    while (k < text.size() && std::isdigit(static_cast<unsigned char>(text[k]))) {
        long n = 0;
        while (k < text.size() && std::isdigit(static_cast<unsigned char>(text[k]))) n = n * 10 + (text[k++] - '0');
        t.numbers.push_back(n);
        if (k + 1 < text.size() && text[k] == '.' && std::isdigit(static_cast<unsigned char>(text[k + 1]))) { ++k; continue; }
        break;
    }
    t.end = k;
    return !t.numbers.empty();
}

// Suivi d'un ':=' apres le jeton (espaces permis) : une ecriture.
bool assigned(const std::string& text, std::size_t from) {
    auto k = from;
    // un indice ou un membre colle a l'adresse ne change pas l'affaire
    while (k < text.size() && (text[k] == ' ' || text[k] == '\t')) ++k;
    return k + 1 < text.size() && text[k] == ':' && text[k + 1] == '=';
}

bool topological(const Token& t, Direction& dir, bool& word) {
    if (t.numbers.size() < 3) return false;
    if (t.area == "I") { dir = Direction::Input; word = false; return true; }
    if (t.area == "IW" || t.area == "ID") { dir = Direction::Input; word = true; return true; }
    if (t.area == "Q") { dir = Direction::Output; word = false; return true; }
    if (t.area == "QW" || t.area == "QD") { dir = Direction::Output; word = true; return true; }
    return false;
}

std::string normalised(const Token& t) {
    std::string s = "%" + t.area;
    for (std::size_t i = 0; i < t.numbers.size(); ++i) s += (i ? "." : "") + std::to_string(t.numbers[i]);
    return s;
}

// Les sections que la tache (ou une autre) execute, avec le nom de leur entree.
std::vector<std::pair<Index, std::string>> codeOf(const Project& p) {
    std::vector<std::pair<Index, std::string>> out;
    std::set<Index> seen;
    for (const auto& task : p.tasks)
        for (const auto& e : executionEntries(p, task.name)) {
            const std::string name = e.unit && e.pou < p.pous.size() ? std::string(p.strings.text(p.pous[e.pou].name)) : std::string();
            for (const auto s : e.sections) {
                if (s >= p.sections.size() || !seen.insert(s).second) continue;
                out.emplace_back(s, name.empty() ? std::string(p.strings.text(p.sections[s].name)) : name);
            }
        }
    // Les sous-routines et les sections qu'aucune tache ne porte (elles tournent quand on les appelle).
    for (Index s = 0; s < p.sections.size(); ++s) {
        if (seen.count(s)) continue;
        const auto owner = p.sections[s].owner;
        if (owner != kNoIndex && owner < p.pous.size() && p.pous[owner].kind == PouKind::FunctionBlockType) continue;
        out.emplace_back(s, std::string(p.strings.text(p.sections[s].name)));
    }
    return out;
}

const Module* moduleAt(const Project& p, int rack, int slot) {
    for (const auto& r : p.hardware.racks)
        if (r.number == rack)
            for (const auto& m : r.modules)
                if (m.slot == slot) return &m;
    return nullptr;
}

bool isOutputOnly(const Module& m) {
    return m.kind == ModuleKind::DiscreteOutput || m.kind == ModuleKind::AnalogOutput
        || (m.outputPoints > 0 && m.inputPoints == 0 && m.kind != ModuleKind::Cpu && m.kind != ModuleKind::Communication);
}
bool isInputOnly(const Module& m) {
    return m.kind == ModuleKind::DiscreteInput || m.kind == ModuleKind::AnalogInput
        || (m.inputPoints > 0 && m.outputPoints == 0 && m.kind != ModuleKind::Cpu && m.kind != ModuleKind::Communication);
}

std::uint32_t wordsOf(const Project& p, const Variable& v, const std::string& area) {
    const auto bits = typeSizeInBits(p, v.type);
    std::uint32_t words = bits ? (bits + 15) / 16 : 1;
    if ((area == "MD" || area == "MF") && words < 2) words = 2;
    return std::max<std::uint32_t>(1, words);
}

} // namespace

std::string verdict(const Address& a) {
    switch (a.status) {
        case Address::Status::Ok:            return "ok";
        case Address::Status::NoHardware:    return "pas de .XHW : rien \xC3\xA0 comparer";
        default:                             return a.detail;
    }
}

Report check(const Project& p) {
    Report rep;
    rep.hardware = !p.hardware.racks.empty();
    std::map<std::tuple<int, int, int, int, int, int>, Address> found;   // (sens, mot, rack, emplacement, voie, sub)
    const auto note = [&](const Token& t, Direction dir, bool word, const std::string& user, bool write, const std::string& declared) {
        const int sub = t.numbers.size() > 3 ? static_cast<int>(t.numbers[3]) : -1;
        const auto key = std::make_tuple(static_cast<int>(dir), word ? 1 : 0, static_cast<int>(t.numbers[0]),
                                         static_cast<int>(t.numbers[1]), static_cast<int>(t.numbers[2]), sub);
        auto& a = found[key];
        if (a.text.empty()) {
            a.text = normalised(t);
            a.direction = dir;
            a.word = word;
            a.rack = static_cast<int>(t.numbers[0]);
            a.slot = static_cast<int>(t.numbers[1]);
            a.channel = static_cast<int>(t.numbers[2]);
            a.sub = sub;
        }
        if (!user.empty() && std::find(a.users.begin(), a.users.end(), user) == a.users.end()) a.users.push_back(user);
        if (!declared.empty()) a.declaredAs = declared;
        if (write) ++a.writes;
        else if (declared.empty()) ++a.reads;
    };

    for (const auto& [si, user] : codeOf(p)) {
        const auto text = stripped(p.sections[si].body);
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '%') continue;
            Token t;
            if (!readToken(text, i, t)) continue;
            Direction dir{};
            bool word = false;
            if (topological(t, dir, word)) note(t, dir, word, user, assigned(text, t.end), {});
            i = t.end > i ? t.end - 1 : i;
        }
    }
    // Les variables situees sur une adresse topologique.
    for (const auto& v : p.variables) {
        if (!v.located || v.address.raw.empty() || v.address.raw.front() != '%') continue;
        Token t;
        if (!readToken(v.address.raw, 0, t)) continue;
        Direction dir{};
        bool word = false;
        if (topological(t, dir, word)) note(t, dir, word, {}, false, std::string(p.strings.text(v.name)));
    }

    // Le verdict, face aux racks.
    for (auto& [key, a] : found) {
        if (!rep.hardware) {
            a.status = Address::Status::NoHardware;
        } else if (const auto* m = moduleAt(p, a.rack, a.slot)) {
            a.module = m->reference;
            const bool in = a.direction == Direction::Input;
            if (in && isOutputOnly(*m)) {
                a.status = Address::Status::WrongDirection;
                a.detail = m->reference + " est un module de sorties";
            } else if (!in && isInputOnly(*m)) {
                a.status = Address::Status::WrongDirection;
                a.detail = m->reference + " est un module d'entr\xC3\xA9" "es";
            } else {
                const auto count = in ? m->inputPoints : m->outputPoints;
                if (count > 0 && a.channel >= static_cast<int>(count) && m->kind != ModuleKind::Cpu
                    && m->kind != ModuleKind::Communication) {
                    a.status = Address::Status::NoSuchChannel;
                    a.detail = m->reference + " n'a que " + std::to_string(count) + (in ? " entr\xC3\xA9" "es" : " sorties")
                             + " (voies 0 \xC3\xA0 " + std::to_string(count - 1) + ")";
                }
            }
        } else {
            a.status = Address::Status::NoModule;
            bool rackExists = false;
            std::uint16_t slots = 0;
            for (const auto& r : p.hardware.racks)
                if (r.number == a.rack) { rackExists = true; slots = r.slotCount; }
            if (!rackExists) a.detail = "pas de rack " + std::to_string(a.rack);
            else if (slots && a.slot >= static_cast<int>(slots))
                a.detail = "le rack " + std::to_string(a.rack) + " n'a que " + std::to_string(slots) + " emplacements";
            else a.detail = "aucun module \xC3\xA0 cet emplacement";
        }
        if (a.faulty()) ++rep.faulty;
    }

    // Tri : entrees (bits, mots), sorties (bits, mots) ; rack, emplacement, voie.
    for (auto& [key, a] : found) rep.addresses.push_back(std::move(a));

    // Les modules : les voies employees.
    for (const auto& r : p.hardware.racks)
        for (const auto& m : r.modules) {
            if (m.slot < 0) continue;
            ModuleUse u;
            u.rack = r.number;
            u.slot = m.slot;
            u.reference = m.reference;
            u.total = m.points();
            std::set<std::pair<int, int>> channels;
            for (const auto& a : rep.addresses)
                if (a.rack == r.number && a.slot == m.slot && a.status == Address::Status::Ok) {
                    channels.insert({static_cast<int>(a.direction), a.channel});
                    u.addresses.push_back(a.text);
                }
            u.used = channels.size();
            rep.modules.push_back(std::move(u));
        }
    return rep;
}

Memory memoryOf(const Project& p) {
    Memory mem;
    if (p.hardware.memory.declared) mem.configured = p.hardware.memory.internalWords;
    for (const auto& v : p.variables) {
        if (!v.located || v.address.raw.empty() || v.address.raw.front() != '%') continue;
        Token t;
        if (!readToken(v.address.raw, 0, t)) continue;
        if (t.area != "MW" && t.area != "MD" && t.area != "MF") continue;
        if (t.numbers.size() != 1) continue;            // %MW10.3 : un bit d'un mot, pas une place
        Located l;
        l.name = std::string(p.strings.text(v.name));
        l.type = std::string(p.strings.text(v.type.name));
        l.address = v.address.raw;
        l.first = static_cast<std::uint32_t>(t.numbers[0]);
        l.words = wordsOf(p, v, t.area);
        mem.variables.push_back(std::move(l));
    }
    std::stable_sort(mem.variables.begin(), mem.variables.end(),
                     [](const Located& a, const Located& b) { return a.first < b.first; });
    if (!mem.variables.empty()) {
        mem.low = mem.variables.front().first;
        std::uint32_t end = 0;          // le premier mot libre apres la precedente
        const Located* previous = nullptr;
        std::uint32_t bestGap = 0;
        for (const auto& l : mem.variables) {
            if (previous) {
                if (l.first < end) mem.overlaps.push_back({previous->name, l.name, l.first});
                else if (l.first - end > bestGap) {
                    bestGap = l.first - end;
                    mem.gapFrom = end;
                    mem.gapTo = l.first - 1;
                }
            }
            if (!previous || l.first + l.words > end) { end = l.first + l.words; previous = &l; }
        }
        mem.high = end ? end - 1 : 0;
    }
    // Les %MW que le code nomme en direct.
    std::set<std::uint32_t> words;
    for (const auto& s : p.sections) {
        const auto text = stripped(s.body);
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '%') continue;
            Token t;
            if (!readToken(text, i, t)) continue;
            if (t.area == "MW" && t.numbers.size() == 1) {
                ++mem.directReferences;
                words.insert(static_cast<std::uint32_t>(t.numbers[0]));
            }
            i = t.end > i ? t.end - 1 : i;
        }
    }
    mem.directWords.assign(words.begin(), words.end());
    return mem;
}

// ------------------------------------------ lot API 5 : les trois zones ----
namespace {

// La zone d'une adresse situee, ou d'une adresse du code : %M -> bits ; %MW,
// %MD, %MF -> mots ; %KW, %KD, %KF -> constantes. -1 : pas du plan memoire.
int zoneOfArea(const std::string& area) {
    if (area == "M") return 0;
    if (area == "MW" || area == "MD" || area == "MF") return 1;
    if (area == "KW" || area == "KD" || area == "KF") return 2;
    return -1;
}

// Les cellules d'une variable : une par element de %M (un tableau d'EBOOL en
// prend autant que d'elements) ; des mots ailleurs (un DINT, un REAL : deux).
std::uint32_t cellsOf(const Project& p, const Variable& v, int zone, const std::string& area) {
    if (zone == 0) {
        if (v.type.klass == TypeClass::Array && v.type.arrayHigh >= v.type.arrayLow)
            return static_cast<std::uint32_t>(std::min<std::int64_t>(v.type.arrayHigh - v.type.arrayLow + 1, 1 << 20));
        return 1;
    }
    const auto bits = typeSizeInBits(p, v.type);
    std::uint32_t words = bits ? (bits + 15) / 16 : 1;
    if ((area == "MD" || area == "MF" || area == "KD" || area == "KF") && words < 2) words = 2;
    return std::max<std::uint32_t>(1, words);
}

// ---- un petit evaluateur d'expressions entieres (les indices entre []) ----
//  Des nombres (123, 16#FF, 2#1010, 1_000, INT#5), + - * / MOD, des
//  parentheses, et des noms que `lookup` sait valoir (les constantes, la
//  variable d'une boucle FOR). Un nom inconnu : pas de valeur.
class Evaluator {
public:
    using Lookup = std::function<std::optional<long long>(const std::string&)>;
    Evaluator(std::string_view text, Lookup lookup) : s_(text), lookup_(std::move(lookup)) {}
    std::optional<long long> run() {
        auto v = expr();
        ws();
        if (!v || i_ != s_.size()) return std::nullopt;
        return v;
    }
private:
    void ws() { while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_; }
    bool word(std::string_view w) {
        ws();
        if (s_.size() - i_ < w.size()) return false;
        for (std::size_t k = 0; k < w.size(); ++k)
            if (std::toupper(static_cast<unsigned char>(s_[i_ + k])) != w[k]) return false;
        const auto after = i_ + w.size();
        if (after < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[after])) || s_[after] == '_')) return false;
        i_ = after;
        return true;
    }
    std::optional<long long> expr() {
        auto v = term();
        while (v) {
            ws();
            if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) {
                const char op = s_[i_++];
                const auto r = term();
                if (!r) return std::nullopt;
                v = op == '+' ? *v + *r : *v - *r;
            } else break;
        }
        return v;
    }
    std::optional<long long> term() {
        auto v = factor();
        while (v) {
            ws();
            if (i_ < s_.size() && (s_[i_] == '*' || s_[i_] == '/')) {
                const char op = s_[i_++];
                const auto r = factor();
                if (!r || (op == '/' && *r == 0)) return std::nullopt;
                v = op == '*' ? *v * *r : *v / *r;
            } else if (word("MOD")) {
                const auto r = factor();
                if (!r || *r == 0) return std::nullopt;
                v = *v % *r;
            } else break;
        }
        return v;
    }
    std::optional<long long> factor() {
        ws();
        if (i_ >= s_.size()) return std::nullopt;
        const char c = s_[i_];
        if (c == '-' || c == '+') {
            ++i_;
            const auto v = factor();
            if (!v) return std::nullopt;
            return c == '-' ? -*v : *v;
        }
        if (c == '(') {
            ++i_;
            const auto v = expr();
            ws();
            if (!v || i_ >= s_.size() || s_[i_] != ')') return std::nullopt;
            ++i_;
            return v;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            long long base = 10, v = 0;
            std::size_t k = i_;
            while (k < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[k])) || s_[k] == '_')) {
                if (s_[k] != '_') v = v * 10 + (s_[k] - '0');
                ++k;
            }
            if (k < s_.size() && s_[k] == '#') {          // 16#FF, 2#1010, 8#17
                base = v;
                if (base != 2 && base != 8 && base != 16) return std::nullopt;
                v = 0;
                ++k;
                bool any = false;
                while (k < s_.size() && (std::isxdigit(static_cast<unsigned char>(s_[k])) || s_[k] == '_')) {
                    if (s_[k] != '_') {
                        const int d = std::isdigit(static_cast<unsigned char>(s_[k])) ? s_[k] - '0' : std::toupper(static_cast<unsigned char>(s_[k])) - 'A' + 10;
                        if (d >= base) return std::nullopt;
                        v = v * base + d;
                        any = true;
                    }
                    ++k;
                }
                if (!any) return std::nullopt;
            }
            i_ = k;
            return v;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t k = i_;
            while (k < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[k])) || s_[k] == '_' || s_[k] == '.')) ++k;
            std::string name(s_.substr(i_, k - i_));
            if (k < s_.size() && s_[k] == '#') {          // INT#5, DINT#16#FF : le type saute
                i_ = k + 1;
                return factor();
            }
            i_ = k;
            if (!lookup_) return std::nullopt;
            return lookup_(name);
        }
        return std::nullopt;
    }
    std::string_view s_;
    Lookup           lookup_;
    std::size_t      i_{0};
};

std::string lowered(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Les noms d'une expression (sans les mots MOD, AND...).
std::vector<std::string> namesIn(std::string_view s) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < s.size();) {
        if (std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_') {
            std::size_t k = i;
            while (k < s.size() && (std::isalnum(static_cast<unsigned char>(s[k])) || s[k] == '_' || s[k] == '.')) ++k;
            if (k < s.size() && s[k] == '#') { i = k + 1; continue; }     // INT#5
            std::string n(s.substr(i, k - i));
            const auto u = lowered(n);
            if (u != "mod" && u != "and" && u != "or" && u != "xor" && u != "not") out.push_back(std::move(n));
            i = k;
        } else if (std::isdigit(static_cast<unsigned char>(s[i]))) {
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '#')) ++i;
        } else {
            ++i;
        }
    }
    return out;
}

// Les boucles FOR d'une section : « FOR i := 0 TO 15 [BY 1] DO » -> i : [0, 15].
struct ForRange {
    std::string var;
    std::string from, to;
    std::size_t begin{0}, end{0};     // l'en-tete, et le END_FOR qui ferme la boucle
};
std::vector<ForRange> forLoopsOf(const std::string& text) {
    std::vector<ForRange> out;
    const auto upperText = [&] {
        std::string u(text);
        for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return u;
    }();
    const auto isWordAt = [&](std::size_t at, std::string_view w) {
        if (upperText.compare(at, w.size(), w) != 0) return false;
        const bool beforeOk = at == 0 || !(std::isalnum(static_cast<unsigned char>(upperText[at - 1])) || upperText[at - 1] == '_');
        const auto after = at + w.size();
        const bool afterOk = after >= upperText.size() || !(std::isalnum(static_cast<unsigned char>(upperText[after])) || upperText[after] == '_');
        return beforeOk && afterOk;
    };
    const auto findWord = [&](std::string_view w, std::size_t from, std::size_t limit) {
        for (auto at = upperText.find(w, from); at != std::string::npos && at < limit; at = upperText.find(w, at + 1))
            if (isWordAt(at, w)) return at;
        return std::string::npos;
    };
    // Les END_FOR, pour savoir ce que chaque boucle contient (une pile).
    std::vector<std::pair<std::size_t, bool>> marks;     // (position, ouvre ?)
    for (std::size_t at = findWord("FOR", 0, upperText.size()); at != std::string::npos; at = findWord("FOR", at + 3, upperText.size()))
        marks.emplace_back(at, true);
    for (std::size_t at = findWord("END_FOR", 0, upperText.size()); at != std::string::npos; at = findWord("END_FOR", at + 7, upperText.size()))
        marks.emplace_back(at, false);
    std::sort(marks.begin(), marks.end());
    std::map<std::size_t, std::size_t> closing;          // debut -> fin
    {
        std::vector<std::size_t> stack;
        for (const auto& [pos, opens] : marks) {
            if (opens) stack.push_back(pos);
            else if (!stack.empty()) { closing[stack.back()] = pos; stack.pop_back(); }
        }
        for (const auto open : stack) closing[open] = upperText.size();
    }
    for (std::size_t at = findWord("FOR", 0, upperText.size()); at != std::string::npos; at = findWord("FOR", at + 3, upperText.size())) {
        const auto assign = text.find(":=", at + 3);
        const auto doAt = findWord("DO", at + 3, upperText.size());
        if (assign == std::string::npos || doAt == std::string::npos || assign > doAt) continue;
        std::string var(text.substr(at + 3, assign - at - 3));
        var.erase(std::remove_if(var.begin(), var.end(), [](unsigned char ch) { return std::isspace(ch); }), var.end());
        const auto toAt = findWord("TO", assign + 2, doAt);
        if (var.empty() || toAt == std::string::npos) continue;
        const auto byAt = findWord("BY", toAt + 2, doAt);
        ForRange f;
        f.var = var;
        f.from = text.substr(assign + 2, toAt - assign - 2);
        f.to = text.substr(toAt + 2, (byAt == std::string::npos ? doAt : byAt) - toAt - 2);
        f.begin = at;
        f.end = closing.count(at) ? closing[at] : upperText.size();
        out.push_back(std::move(f));
    }
    return out;
}

} // namespace

std::string percentText(double percent) {
    if (percent <= 0.0) return "0 %";
    if (percent < 0.1) return "< 0,1 %";
    char buf[32];
    std::snprintf(buf, sizeof buf, percent < 10.0 ? "%.1f %%" : "%.0f %%", percent);
    std::string out(buf);
    for (auto& ch : out) if (ch == '.') ch = ',';
    return out;
}

std::vector<ZoneMap> memoryZones(const Project& p) {
    std::vector<ZoneMap> zones(3);
    static const char* const kPrefix[] = {"%M", "%MW", "%KW"};
    for (int z = 0; z < 3; ++z) {
        auto& m = zones[static_cast<std::size_t>(z)];
        m.zone = static_cast<MemoryZone>(z);
        m.prefix = kPrefix[z];
        m.unit = z == 0 ? "bit" : "mot";
        m.units = z == 0 ? "bits" : "mots";
    }
    // Les variables situees.
    std::vector<std::set<std::uint32_t>> used(3);
    for (const auto& v : p.variables) {
        if (!v.located || v.address.raw.empty() || v.address.raw.front() != '%') continue;
        Token t;
        if (!readToken(v.address.raw, 0, t)) continue;
        const int z = zoneOfArea(t.area);
        if (z < 0) continue;
        auto& m = zones[static_cast<std::size_t>(z)];
        if (t.numbers.size() == 2 && z != 0) {
            // %MW10.3 : un bit d'un mot - le mot est employe, sans y prendre une place.
            used[static_cast<std::size_t>(z)].insert(static_cast<std::uint32_t>(t.numbers[0]));
            continue;
        }
        if (t.numbers.size() != 1) continue;
        Located l;
        l.name = std::string(p.strings.text(v.name));
        l.type = std::string(p.strings.text(v.type.name));
        l.address = v.address.raw;
        l.first = static_cast<std::uint32_t>(t.numbers[0]);
        l.words = cellsOf(p, v, z, t.area);
        m.variables.push_back(std::move(l));
    }
    // Les constantes du projet : un indice peut les nommer (%MW100[NB_ARMOIRES]).
    std::map<std::string, long long> known;
    std::map<std::string, std::string> origin;     // d'ou vient la valeur (pour le dire)
    for (const auto& v : p.variables) {
        if (v.scope != VariableScope::Constant) continue;
        const auto init = std::string(p.strings.text(v.initValue));
        if (const auto value = Evaluator(init, nullptr).run()) {
            known[lowered(p.strings.text(v.name))] = *value;
            origin[lowered(p.strings.text(v.name))] = std::string(p.strings.text(v.name)) + " = " + std::to_string(*value) + " (constante)";
        }
    }
    // ... et les CONSTANTES DE FAIT : une variable que le code n'affecte qu'a une
    // seule valeur constante (« position_depart_memoire := 6000; » dans
    // Preconfig). Une variable jamais affectee par le code n'en est pas une : l'IHM
    // ou un bloc peut l'ecrire.
    std::map<std::string, std::vector<std::string>> assigns;
    std::map<std::string, std::string> assignedIn;
    for (const auto& sec : p.sections) {
        const auto text = stripped(sec.body);
        for (std::size_t i = 0; i + 1 < text.size(); ++i) {
            if (text[i] != ':' || text[i + 1] != '=') continue;
            std::size_t e = i;
            while (e > 0 && std::isspace(static_cast<unsigned char>(text[e - 1]))) --e;
            std::size_t b = e;
            while (b > 0 && (std::isalnum(static_cast<unsigned char>(text[b - 1])) || text[b - 1] == '_')) --b;
            if (b == e || std::isdigit(static_cast<unsigned char>(text[b]))) continue;
            if (b > 0 && (text[b - 1] == '.' || text[b - 1] == '%' || text[b - 1] == '#' || text[b - 1] == ']')) continue;
            std::size_t w = b;
            while (w > 0 && std::isspace(static_cast<unsigned char>(text[w - 1]))) --w;
            if (w >= 3 && lowered(text.substr(w - 3, 3)) == "for" && (w == 3 || !(std::isalnum(static_cast<unsigned char>(text[w - 4])) || text[w - 4] == '_')))
                continue;                                         // « FOR i := 0 TO ... »
            const auto semi = text.find(';', i + 2);
            if (semi == std::string::npos) continue;
            const auto name = lowered(text.substr(b, e - b));
            assigns[name].push_back(text.substr(i + 2, semi - i - 2));
            assignedIn.emplace(name, std::string(p.strings.text(sec.name)));
        }
    }
    const auto lookupKnown = [&](const std::string& n) -> std::optional<long long> {
        const auto it = known.find(lowered(n));
        return it == known.end() ? std::nullopt : std::optional<long long>(it->second);
    };
    for (int pass = 0; pass < 6; ++pass) {
        bool changed = false;
        for (const auto& [name, list] : assigns) {
            if (known.count(name)) continue;
            std::optional<long long> value;
            bool same = true;
            for (const auto& rhs : list) {
                const auto v = Evaluator(rhs, lookupKnown).run();
                if (!v || (value && *v != *value)) { same = false; break; }
                value = v;
            }
            if (same && value) {
                known[name] = *value;
                origin[name] = name + " = " + std::to_string(*value) + " (" + assignedIn[name] + ")";
                changed = true;
            }
        }
        if (!changed) break;
    }
    // Le code, en direct : lu ou ecrit, un bit de mot, une table (%MW20:5), une
    // adresse indexee (%MW0[index]).
    std::vector<std::set<std::uint32_t>> direct(3), written(3), dynamicCells(3);
    for (const auto& s : p.sections) {
        const auto text = stripped(s.body);
        std::vector<ForRange> loops;
        bool loopsRead = false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '%') continue;
            Token t;
            if (!readToken(text, i, t)) continue;
            const int z = zoneOfArea(t.area);
            if (z < 0 || !(t.numbers.size() == 1 || (z != 0 && t.numbers.size() == 2))) {
                i = t.end > i ? t.end - 1 : i;
                continue;
            }
            auto& m = zones[static_cast<std::size_t>(z)];
            const auto zi = static_cast<std::size_t>(z);
            ++m.directReferences;
            const auto first = static_cast<std::uint32_t>(t.numbers[0]);
            const bool two = t.area == "MD" || t.area == "MF" || t.area == "KD" || t.area == "KF";
            const std::uint32_t step = two ? 2u : 1u;        // un %MD, un %MF : deux mots par element
            std::size_t after = t.end;
            // Une table : %MW20:5 (cinq mots) - pas « := ».
            std::uint32_t count = 1;
            if (t.numbers.size() == 1 && after + 1 < text.size() && text[after] == ':' && std::isdigit(static_cast<unsigned char>(text[after + 1]))) {
                std::size_t k = after + 1;
                long n = 0;
                while (k < text.size() && std::isdigit(static_cast<unsigned char>(text[k]))) n = n * 10 + (text[k++] - '0');
                count = static_cast<std::uint32_t>(std::clamp<long>(n, 1, 65536));
                after = k;
            }
            // Un indice : %MW0[index].
            std::optional<Indexed> idx;
            {
                std::size_t k = after;
                while (k < text.size() && (text[k] == ' ' || text[k] == '\t')) ++k;
                if (t.numbers.size() == 1 && k < text.size() && text[k] == '[') {
                    int depth = 0;
                    std::size_t close = std::string::npos;
                    for (std::size_t q = k; q < text.size() && text[q] != '\n'; ++q) {
                        if (text[q] == '[') ++depth;
                        else if (text[q] == ']' && --depth == 0) { close = q; break; }
                    }
                    if (close != std::string::npos) {
                        const std::string inside = text.substr(k + 1, close - k - 1);
                        Indexed in;
                        in.section = std::string(p.strings.text(s.name));
                        in.base = first;
                        std::string compact = "%" + t.area + std::to_string(first) + "[" + inside + "]";
                        compact.erase(std::remove_if(compact.begin(), compact.end(), [](unsigned char ch) { return std::isspace(ch); }), compact.end());
                        in.text = compact;
                        // Les noms de l'indice : connus (constantes, constantes de fait),
                        // variables d'une boucle FOR de la section, ou inconnus.
                        std::vector<std::string> names, loopVars, others;
                        for (const auto& n : namesIn(inside)) {
                            const auto l = lowered(n);
                            if (std::find(names.begin(), names.end(), l) != names.end()) continue;
                            names.push_back(l);
                        }
                        if (!loopsRead) { loops = forLoopsOf(text); loopsRead = true; }
                        std::map<std::string, std::pair<long long, long long>> ranges;
                        std::string facts;
                        for (const auto& l : names) {
                            if (known.count(l)) {
                                if (origin.count(l)) facts += (facts.empty() ? "" : ", ") + origin[l];
                                continue;
                            }
                            // La boucle FOR de cette variable qui CONTIENT l'adresse (la plus
                            // interieure) : une boucle d'avant, deja finie, ne la borne pas.
                            const ForRange* enclosing = nullptr;
                            for (const auto& f : loops)
                                if (lowered(f.var) == l && f.begin < i && i < f.end && (!enclosing || f.begin > enclosing->begin)) enclosing = &f;
                            bool bounded = false;
                            if (enclosing) {
                                const auto a = Evaluator(enclosing->from, lookupKnown).run();
                                const auto b = Evaluator(enclosing->to, lookupKnown).run();
                                if (a && b) {
                                    ranges[l] = {std::min(*a, *b), std::max(*a, *b)};
                                    bounded = true;
                                }
                            }
                            (bounded ? loopVars : others).push_back(l);
                        }
                        // Evaluer aux coins : chaque variable de boucle a ses bornes, les
                        // inconnues a 0 (une estimation, dite comme telle).
                        std::optional<long long> lo, hi;
                        if (loopVars.size() <= 4) {
                            const std::size_t corners = std::size_t{1} << loopVars.size();
                            bool ok = true;
                            for (std::size_t mask = 0; mask < corners && ok; ++mask) {
                                const auto v = Evaluator(inside, [&](const std::string& n) -> std::optional<long long> {
                                    const auto l = lowered(n);
                                    for (std::size_t q = 0; q < loopVars.size(); ++q)
                                        if (loopVars[q] == l) return (mask >> q) & 1u ? ranges[l].second : ranges[l].first;
                                    if (std::find(others.begin(), others.end(), l) != others.end()) return 0LL;
                                    return lookupKnown(n);
                                }).run();
                                if (!v) { ok = false; break; }
                                lo = lo ? std::min(*lo, *v) : *v;
                                hi = hi ? std::max(*hi, *v) : *v;
                            }
                            if (!ok) lo.reset(), hi.reset();
                        }
                        const auto cellOf = [&](long long index) {
                            return static_cast<std::uint32_t>(std::clamp<long long>(static_cast<long long>(first) + index * step, 0, 0xFFFFFFF0LL));
                        };
                        if (lo && hi && loopVars.empty() && others.empty()) {
                            in.constant = true;
                            in.lo = cellOf(*lo);
                            in.hi = in.lo + step - 1;
                            in.why = "indice \\xC3\\xA9valu\\xC3\\xA9 : " + std::to_string(*lo) + (facts.empty() ? std::string{} : " (" + facts + ")");
                        } else {
                            in.dynamic = true;
                            std::string loopsText;
                            for (const auto& l : loopVars)
                                loopsText += (loopsText.empty() ? "" : ", ") + l + " de " + std::to_string(ranges[l].first) + " \\xC3\\xA0 " + std::to_string(ranges[l].second);
                            if (lo && hi && *hi - *lo < 1000000) {
                                in.lo = cellOf(*lo);
                                in.hi = cellOf(*hi) + step - 1;
                                in.bounded = others.empty();
                                in.estimated = !others.empty();
                                std::string unknownText;
                                for (const auto& l : others) unknownText += (unknownText.empty() ? "" : ", ") + l;
                                in.why = (loopsText.empty() ? std::string{} : loopsText + " (FOR)")
                                       + (others.empty() ? std::string{} : std::string(loopsText.empty() ? "" : " ; ") + unknownText + " inconnu \\xC3\\xA0 l'avance : estim\\xC3\\xA9 \\xC3\\xA0 0")
                                       + (facts.empty() ? std::string{} : " ; " + facts);
                            } else {
                                in.lo = in.hi = first;
                                std::string unknownText;
                                for (const auto& l : others) unknownText += (unknownText.empty() ? "" : ", ") + l;
                                in.why = "plage inconnue : " + (unknownText.empty() ? inside : unknownText) + " change \\xC3\\xA0 l'ex\\xC3\\xA9" "cution";
                            }
                        }
                        after = close + 1;
                        // « %MW0[...].0 := TRUE » : un bit du mot indexe.
                        if (after + 1 < text.size() && text[after] == '.' && std::isdigit(static_cast<unsigned char>(text[after + 1]))) {
                            after += 1;
                            while (after < text.size() && std::isdigit(static_cast<unsigned char>(text[after]))) ++after;
                        }
                        idx = std::move(in);
                    }
                }
            }
            const bool write = assigned(text, after);
            if (idx) {
                idx->write = write;
                if (idx->constant) {
                    for (std::uint32_t c = idx->lo; c < idx->lo + step; ++c) {
                        direct[zi].insert(c);
                        if (write) written[zi].insert(c);
                    }
                } else if (idx->bounded) {
                    for (std::uint32_t c = idx->lo; c <= idx->hi && c - idx->lo < 1000000u; ++c) dynamicCells[zi].insert(c);
                } else if (idx->estimated) {
                    // Estimee (une inconnue a 0) : comptee, et signalee comme incertaine.
                    ++m.dynamicUnknown;
                    for (std::uint32_t c = idx->lo; c <= idx->hi && c - idx->lo < 1000000u; ++c) dynamicCells[zi].insert(c);
                } else {
                    ++m.dynamicUnknown;          // rien d'evaluable : signalee, pas comptee
                }
                m.indexed.push_back(std::move(*idx));
            } else {
                for (std::uint32_t c = first; c < first + count * step; ++c) {
                    direct[zi].insert(c);
                    if (write) written[zi].insert(c);
                }
            }
            i = after > i ? after - 1 : i;
        }
    }
    const auto& hw = p.hardware.memory;
    for (int z = 0; z < 3; ++z) {
        auto& m = zones[static_cast<std::size_t>(z)];
        auto& cells = used[static_cast<std::size_t>(z)];
        std::stable_sort(m.variables.begin(), m.variables.end(), [](const Located& a, const Located& b) { return a.first < b.first; });
        // Les chevauchements (toute la zone).
        std::uint32_t end = 0;
        const Located* previous = nullptr;
        for (const auto& l : m.variables) {
            if (previous && l.first < end) m.overlaps.push_back({previous->name, l.name, l.first});
            if (!previous || l.first + l.words > end) { end = l.first + l.words; previous = &l; }
            for (std::uint32_t c = l.first; c < l.first + l.words && c < l.first + (1u << 20); ++c) cells.insert(c);
        }
        for (const auto c : direct[static_cast<std::size_t>(z)]) cells.insert(c);
        for (const auto c : dynamicCells[static_cast<std::size_t>(z)]) cells.insert(c);
        m.direct.assign(direct[static_cast<std::size_t>(z)].begin(), direct[static_cast<std::size_t>(z)].end());
        m.written.assign(written[static_cast<std::size_t>(z)].begin(), written[static_cast<std::size_t>(z)].end());
        m.dynamicCells.assign(dynamicCells[static_cast<std::size_t>(z)].begin(), dynamicCells[static_cast<std::size_t>(z)].end());
        m.highest = cells.empty() ? 0u : *cells.rbegin() + 1u;
        // La taille complete : celle du .XHW ; sinon, jusqu'a la derniere cellule
        // employee, arrondie a la centaine (au moins 100).
        const std::uint32_t declared = !hw.declared ? 0u : z == 0 ? hw.internalBits : z == 1 ? hw.internalWords : hw.constantWords;
        m.configured = declared > 0;
        m.size = m.configured ? declared : std::max<std::uint32_t>(100u, (m.highest + 99u) / 100u * 100u);
        if (m.configured) {
            for (const auto& l : m.variables)
                if (l.first + l.words > m.size)
                    m.outside.push_back(l.name + " (" + l.address + (l.words > 1 ? " \xC3\xA0 " + m.cell(l.first + l.words - 1) : std::string{}) + ")");
            // Le code aussi : une adresse directe, une plage indexee au-dela de la
            // zone - a l'execution, un indice hors zone met l'automate en defaut.
            std::size_t beyond = 0;
            std::uint32_t firstBeyond = 0;
            for (const auto c : direct[static_cast<std::size_t>(z)])
                if (c >= m.size && !variableAt(m, c)) {
                    if (!beyond) firstBeyond = c;
                    ++beyond;
                }
            if (beyond)
                m.outside.push_back(std::to_string(beyond) + (beyond == 1 ? " adresse du code" : " adresses du code") + " (" + m.cell(firstBeyond)
                                    + (beyond > 1 ? "\xE2\x80\xA6" : "") + ")");
            for (const auto& in : m.indexed)
                if (in.dynamic && in.hi >= m.size)
                    m.outside.push_back(in.text + " (" + in.section + ") : jusqu'\xC3\xA0 " + m.cell(in.hi) + (in.estimated ? " (estim\xC3\xA9)" : ""));
        }
        // Les bornes : celles du projet, sinon toute la zone. Elles peuvent aller
        // au-dela de la taille configuree (voir ce qui deborde), pas au-dela du
        // plus loin employe.
        const std::uint32_t limit = std::max(m.size, m.highest);
        const auto& w = p.memoryWindows[static_cast<std::size_t>(z)];
        m.bounded = w.set;
        m.from = w.set ? std::min(w.from, limit - 1) : 0u;
        m.to = w.set ? std::min(std::max(w.to, m.from), limit - 1) : m.size - 1;
        // Dans les bornes : les cellules employees, la plus grande place libre.
        std::uint32_t run = 0, runFrom = m.from, best = 0;
        for (std::uint32_t c = m.from; c <= m.to; ++c) {
            if (cells.count(c)) {
                ++m.used;
                run = 0;
            } else {
                if (run == 0) runFrom = c;
                ++run;
                if (run > best) {
                    best = run;
                    m.gapFrom = runFrom;
                    m.gapTo = c;
                }
            }
            if (c == 0xFFFFFFFFu) break;
        }
    }
    return zones;
}

const Located* variableAt(const ZoneMap& m, std::uint32_t cell) {
    for (const auto& l : m.variables)
        if (cell >= l.first && cell < l.first + l.words) return &l;
    return nullptr;
}

std::string protocolOf(std::string_view role) {
    std::string r(role);
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (r.find("ethernet") != std::string::npos || r.find("eth") == 0)
        return r.find("cip") != std::string::npos ? "Ethernet (EtherNet/IP, Modbus TCP)" : "Ethernet (Modbus TCP)";
    if (r.find("modbus") != std::string::npos && r.find("serial") != std::string::npos) return "Modbus s\xC3\xA9rie";
    if (r.find("serial") != std::string::npos || r.find("uni-te") != std::string::npos) return "Liaison s\xC3\xA9rie";
    if (r.find("canopen") != std::string::npos) return "CANopen";
    if (r.find("usb") != std::string::npos) return "USB";
    if (r.find("profibus") != std::string::npos) return "Profibus";
    return {};
}

std::vector<Port> networkOf(const Project& p) {
    std::vector<Port> out;
    for (const auto& r : p.hardware.racks)
        for (const auto& m : r.modules)
            for (const auto& c : m.channels) {
                auto proto = protocolOf(c.role);
                if (proto.empty()) continue;
                Port port;
                port.rack = r.number;
                port.slot = m.slot;
                port.channel = c.number;
                port.module = m.reference;
                port.role = c.role;
                port.protocol = std::move(proto);
                port.task = c.task;
                out.push_back(std::move(port));
            }
    return out;
}

const Located* variableAt(const Memory& m, std::uint32_t word) {
    for (const auto& l : m.variables)
        if (word >= l.first && word < l.first + l.words) return &l;
    return nullptr;
}

} // namespace project::io
