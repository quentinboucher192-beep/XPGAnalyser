// hmi/HmiZones.cpp - les zones memoire d'un equipement, et sa carte (lot 17).
#include "HmiZones.hpp"

#include "HmiEquipment.hpp"
#include "HmiScript.hpp"
#include "HmiTypes.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <thread>

namespace hmi::zones {

namespace {

constexpr std::uint32_t kSpace = 65536;      // les adresses Modbus : 0..65535

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
// Sans espaces, en majuscules : "Fours[ 2 ].x" -> "FOURS[2].X".
std::string keyOf(std::string_view s) {
    std::string out;
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return out;
}
// Une ecriture de `written` touche-t-elle la case `leaf` ? Oui si c'est elle, ou
// l'une de ses racines (Four1 := ... ; Fours[2] := ... ; Fours (index coupe)).
bool covers(const std::string& written, const std::string& leaf) {
    if (written.empty()) return false;
    if (written == leaf) return true;
    if (leaf.size() > written.size() && leaf.compare(0, written.size(), written) == 0
        && (leaf[written.size()] == '.' || leaf[written.size()] == '['))
        return true;
    // Un indice qui n'est pas un nombre (Fours[i].Consigne) : n'importe quel element.
    std::size_t a = 0, b = 0;
    while (a < written.size() && b < leaf.size()) {
        if (written[a] == '[' && leaf[b] == '[') {
            const auto ea = written.find(']', a), eb = leaf.find(']', b);
            if (ea == std::string::npos || eb == std::string::npos) return false;
            const std::string ia = written.substr(a + 1, ea - a - 1), ib = leaf.substr(b + 1, eb - b - 1);
            const bool number = !ia.empty() && std::all_of(ia.begin(), ia.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == ','; });
            if (number && ia != ib) return false;
            a = ea + 1;
            b = eb + 1;
            continue;
        }
        if (written[a] != leaf[b]) return false;
        ++a;
        ++b;
    }
    return a == written.size() && (b == leaf.size() || leaf[b] == '.' || leaf[b] == '[');
}

// Les chemins ECRITS d'un code ST (a := ...), simples ou pointes, indices
// compris ("Consigne", "Four1.Consigne", "Fours[i].Vannes[1].Position"), avec
// leur ligne ; hors commentaires et chaines.
std::vector<std::pair<std::string, int>> assignedPaths(std::string_view s) {
    std::vector<std::pair<std::string, int>> out;
    const auto identStart = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; };
    const auto identChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    int line = 1;
    std::size_t i = 0;
    const auto skipTo = [&](std::size_t to) {
        for (; i < to && i < s.size(); ++i)
            if (s[i] == '\n') ++line;
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (c == '\'' || c == '"') {
            const auto end = s.find(c, i + 1);
            skipTo(end == std::string_view::npos ? s.size() : end + 1);
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            skipTo(end == std::string_view::npos ? s.size() : end + 2);
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            skipTo(end == std::string_view::npos ? s.size() : end);
            continue;
        }
        if (identStart(c) && (i == 0 || (!identChar(s[i - 1]) && s[i - 1] != '.' && s[i - 1] != '#'))) {
            const std::size_t start = i;
            while (i < s.size() && identChar(s[i])) ++i;
            while (i < s.size()) {
                if (s[i] == '.' && i + 1 < s.size() && identStart(s[i + 1])) {
                    ++i;
                    while (i < s.size() && identChar(s[i])) ++i;
                } else if (s[i] == '[') {
                    const auto end = s.find(']', i);
                    if (end == std::string_view::npos) break;
                    i = end + 1;
                } else {
                    break;
                }
            }
            std::size_t k = i;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            if (k + 1 < s.size() && s[k] == ':' && s[k + 1] == '=') out.push_back({std::string(s.substr(start, i - start)), line});
            continue;
        }
        ++i;
    }
    return out;
}
std::string num(std::uint32_t n) { return std::to_string(n); }

} // namespace

// ------------------------------------------------------------------- tables ---
std::string_view tableLabel(MemTable t) noexcept {
    switch (t) {
        case MemTable::Coils: return "Bobines";
        case MemTable::DiscreteInputs: return "Entr\xC3\xA9" "es TOR";
        case MemTable::InputRegisters: return "Registres d'entr\xC3\xA9" "e";
        case MemTable::Holding: return "Registres de maintien";
    }
    return "Registres de maintien";
}
std::string_view tableModicon(MemTable t) noexcept {
    switch (t) {
        case MemTable::Coils: return "0x";
        case MemTable::DiscreteInputs: return "1x";
        case MemTable::InputRegisters: return "3x";
        case MemTable::Holding: return "4x";
    }
    return "4x";
}
std::string_view tableSchneider(MemTable t) noexcept {
    switch (t) {
        case MemTable::Coils: return "%M";
        case MemTable::DiscreteInputs: return "%I";
        case MemTable::InputRegisters: return "%IW";
        case MemTable::Holding: return "%MW";
    }
    return "%MW";
}
std::string_view tableKey(MemTable t) noexcept { return tableModicon(t); }
std::optional<MemTable> tableFrom(std::string_view s) noexcept {
    const std::string k = upperOf(trimmed(s));
    if (k == "0X" || k == "%M" || k == "BOBINES" || k == "CO" || k == "COILS") return MemTable::Coils;
    if (k == "1X" || k == "%I" || k == "ENTREES TOR" || k == "DI") return MemTable::DiscreteInputs;
    if (k == "3X" || k == "%IW" || k == "IR" || k.find("ENTR") != std::string::npos) return MemTable::InputRegisters;
    if (k == "4X" || k == "%MW" || k == "HR" || k.find("MAINTIEN") != std::string::npos) return MemTable::Holding;
    return std::nullopt;
}
bool isBits(MemTable t) noexcept { return t == MemTable::Coils || t == MemTable::DiscreteInputs; }
MemTable tableOf(comm::Area a) noexcept {
    switch (a) {
        case comm::Area::Coils: return MemTable::Coils;
        case comm::Area::DiscreteInputs: return MemTable::DiscreteInputs;
        case comm::Area::InputRegisters: return MemTable::InputRegisters;
        case comm::Area::Holding: return MemTable::Holding;
    }
    return MemTable::Holding;
}
comm::Area areaOf(MemTable t) noexcept {
    switch (t) {
        case MemTable::Coils: return comm::Area::Coils;
        case MemTable::DiscreteInputs: return comm::Area::DiscreteInputs;
        case MemTable::InputRegisters: return comm::Area::InputRegisters;
        case MemTable::Holding: return comm::Area::Holding;
    }
    return comm::Area::Holding;
}
std::string modicon(MemTable t, std::uint32_t offset) {
    const char lead = t == MemTable::Coils ? '0' : t == MemTable::DiscreteInputs ? '1' : t == MemTable::InputRegisters ? '3' : '4';
    char b[16];
    const unsigned long long n = static_cast<unsigned long long>(offset) + 1;
    if (n <= 9999) std::snprintf(b, sizeof b, "%c%04llu", lead, n);
    else std::snprintf(b, sizeof b, "%c%05llu", lead, n);
    return b;
}
std::string schneider(MemTable t, std::uint32_t offset) { return std::string(tableSchneider(t)) + num(offset); }

// ------------------------------------------------------------------- plages ---
void normalize(std::vector<MemRange>& r) {
    std::sort(r.begin(), r.end(), [](const MemRange& a, const MemRange& b) { return a.first < b.first; });
    std::vector<MemRange> out;
    for (const auto& x : r) {
        if (x.last < x.first) continue;
        if (!out.empty() && static_cast<std::uint64_t>(out.back().last) + 1 >= x.first) out.back().last = std::max(out.back().last, x.last);
        else out.push_back(x);
    }
    r = std::move(out);
}

std::string rangesText(const std::vector<MemRange>& r) {
    if (r.empty()) return "aucune";
    std::string out;
    for (const auto& x : r) {
        if (!out.empty()) out += " ; ";
        out += x.first == x.last ? num(x.first) : num(x.first) + " - " + num(x.last);
    }
    return out;
}

namespace {
// Une borne : "99", "40100", "%MW99", "HR99", "4x0100" -> le numero (a partir de 0).
bool parseBound(MemTable t, std::string_view raw, std::uint32_t& out, std::string* why) {
    std::string s = upperOf(trimmed(raw));
    if (s.empty()) {
        if (why) *why = "borne vide";
        return false;
    }
    bool modiconForm = false;
    const std::string sch = upperOf(tableSchneider(t));
    if (s.rfind(sch, 0) == 0 && s.size() > sch.size() && std::isdigit(static_cast<unsigned char>(s[sch.size()]))) {
        s = s.substr(sch.size());
    } else if (s.size() > 2 && (s.rfind("HR", 0) == 0 || s.rfind("IR", 0) == 0 || s.rfind("CO", 0) == 0 || s.rfind("DI", 0) == 0)) {
        s = s.substr(2);
    } else if (s.size() > 2 && s[1] == 'X' && std::isdigit(static_cast<unsigned char>(s[0]))) {
        s = s.substr(2);
        modiconForm = true;       // 4x0101 : a partir de 1
    }
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            if (why) *why = "\xC2\xAB " + std::string(raw) + " \xC2\xBB illisible";
            return false;
        }
    unsigned long long n = 0;
    for (char c : s) {
        n = n * 10 + static_cast<unsigned long long>(c - '0');
        if (n > 9999999ull) break;
    }
    // A la Modicon : 5 ou 6 chiffres qui commencent par le chiffre de la table (40101, 400101).
    const char lead = t == MemTable::Coils ? '0' : t == MemTable::DiscreteInputs ? '1' : t == MemTable::InputRegisters ? '3' : '4';
    // (Les bobines aussi : 00001-00100 ; un numero ordinaire ne s'ecrit pas avec des zeros devant.)
    if (!modiconForm && (s.size() == 5 || s.size() == 6) && s[0] == lead) {
        n = std::stoull(s.substr(1));
        modiconForm = true;
    }
    if (modiconForm) {
        if (n == 0) {
            if (why) *why = "\xC2\xAB " + std::string(raw) + " \xC2\xBB : \xC3\xA0 la Modicon, on compte \xC3\xA0 partir de 1";
            return false;
        }
        n -= 1;
    }
    if (n >= kSpace) {
        if (why) *why = "\xC2\xAB " + std::string(raw) + " \xC2\xBB : au-del\xC3\xA0 de 65535";
        return false;
    }
    out = static_cast<std::uint32_t>(n);
    return true;
}
} // namespace

bool parseRanges(MemTable t, std::string_view text, std::vector<MemRange>& out, std::string* why) {
    out.clear();
    const std::string all = trimmed(text);
    const std::string up = upperOf(all);
    if (all.empty() || up == "AUCUNE" || up == "AUCUN" || up == "-" || up == "\xE2\x80\x94") return true;
    if (up == "TOUT" || up == "TOUTE") {
        out.push_back({0, kSpace - 1});
        return true;
    }
    std::size_t start = 0;
    while (start <= all.size()) {
        std::size_t end = all.find_first_of(";,", start);
        if (end == std::string::npos) end = all.size();
        const std::string part = trimmed(std::string_view(all).substr(start, end - start));
        start = end + 1;
        if (part.empty()) {
            if (end >= all.size()) break;
            continue;
        }
        // "a - b", "a..b", "a a b" (le tiret peut etre un moins... pas de negatif ici).
        std::string a = part, b;
        std::size_t dash = part.find("..");
        std::size_t len = 2;
        if (dash == std::string::npos) {
            dash = part.find('-');
            len = 1;
        }
        if (dash == std::string::npos) {
            const auto k = upperOf(part).find(" A ");
            if (k != std::string::npos) {
                dash = k;
                len = 3;
            }
        }
        if (dash != std::string::npos) {
            a = part.substr(0, dash);
            b = part.substr(dash + len);
        }
        MemRange r;
        if (!parseBound(t, a, r.first, why)) return false;
        r.last = r.first;
        if (!b.empty() && !parseBound(t, b, r.last, why)) return false;
        if (r.last < r.first) {
            if (why) *why = "\xC2\xAB " + part + " \xC2\xBB : la fin avant le d\xC3\xA9" "but";
            return false;
        }
        out.push_back(r);
        if (end >= all.size()) break;
    }
    normalize(out);
    return true;
}

bool contains(const std::vector<MemRange>& r, std::uint32_t first, std::uint32_t count) noexcept {
    if (count == 0) return true;
    const std::uint64_t last = static_cast<std::uint64_t>(first) + count - 1;
    for (const auto& x : r)
        if (first >= x.first && last <= x.last) return true;
    return false;
}

std::uint32_t totalSize(const std::vector<MemRange>& r) noexcept {
    std::uint32_t n = 0;
    for (const auto& x : r) n += x.size();
    return n;
}

std::string summary(const MemZones& z) {
    if (!z.declared) return "non d\xC3\xA9" "clar\xC3\xA9" "es";
    std::string out;
    for (const auto t : kMemTables) {
        const auto& r = z.of(t);
        if (r.empty()) continue;
        if (!out.empty()) out += " \xC2\xB7 ";
        out += std::string(tableSchneider(t)) + " " + rangesText(r);
    }
    return out.empty() ? std::string("aucune table") : out;
}

// ------------------------------------------------------------------ modeles ---
std::vector<Preset> presets(const Project& p, const Equipment& e, std::uint32_t plcBits, std::uint32_t plcWords) {
    std::vector<Preset> out;
    out.push_back({"variables", "selon ses variables li\xC3\xA9" "es (arrondi \xC3\xA0 la centaine)"});
    if (plcWords > 0) out.push_back({"automate", "comme l'automate du projet (%M 0-" + num(plcBits ? plcBits - 1 : 0) + ", %MW 0-" + num(plcWords - 1) + ")"});
    for (const auto& o : p.equipments)
        if (o.id != e.id && o.zones.declared && o.modbus()) out.push_back({"comme:" + o.name, "comme " + o.name + " (" + summary(o.zones) + ")"});
    out.push_back({"registres", "une centrale : registres 0-9999 (maintien et entr\xC3\xA9" "e)"});
    out.push_back({"tout", "tout : 0-65535 dans les quatre tables"});
    out.push_back({"aucune", "non d\xC3\xA9" "clar\xC3\xA9" "es (aucun contr\xC3\xB4le)"});
    return out;
}

bool applyPreset(const Project& p, const Equipment& e, std::string_view key, std::uint32_t plcBits, std::uint32_t plcWords, MemZones& out,
                 std::string* why) {
    MemZones z;
    z.declared = true;
    if (key == "variables") {
        // Les centaines qu'occupent ses variables (deux groupes eloignes : deux plages).
        const auto m = buildMap(p, e);
        bool any = false;
        for (const auto& v : m.vars) {
            any = true;
            for (std::uint32_t b = v.first / 100; b <= v.last / 100 && b < kSpace / 100 + 1; ++b)
                z.of(v.table).push_back({b * 100, std::min<std::uint32_t>(kSpace - 1, b * 100 + 99)});
        }
        for (auto& t : z.tables) normalize(t);
        if (!any) {
            if (why) *why = e.name + " n'a pas de variable li\xC3\xA9" "e";
            return false;
        }
        z.origin = "mod\xC3\xA8le : selon ses variables";
    } else if (key == "automate") {
        if (plcWords == 0) {
            if (why) *why = "la configuration de l'automate n'est pas connue (pas de .XHW)";
            return false;
        }
        if (plcBits) {
            z.of(MemTable::Coils).push_back({0, plcBits - 1});
            z.of(MemTable::DiscreteInputs).push_back({0, plcBits - 1});
        }
        z.of(MemTable::Holding).push_back({0, plcWords - 1});
        z.of(MemTable::InputRegisters).push_back({0, plcWords - 1});
        z.origin = "mod\xC3\xA8le : l'automate du projet";
    } else if (key.rfind("comme:", 0) == 0) {
        const auto* o = p.equipmentByName(key.substr(6));
        if (!o || !o->zones.declared) {
            if (why) *why = "pas de zones d\xC3\xA9" "clar\xC3\xA9" "es pour " + std::string(key.substr(6));
            return false;
        }
        z = o->zones;
        z.origin = "mod\xC3\xA8le : comme " + o->name;
    } else if (key == "registres") {
        z.of(MemTable::Holding).push_back({0, 9999});
        z.of(MemTable::InputRegisters).push_back({0, 9999});
        z.origin = "mod\xC3\xA8le : une centrale";
    } else if (key == "tout") {
        for (const auto t : kMemTables) z.of(t).push_back({0, kSpace - 1});
        z.origin = "mod\xC3\xA8le : tout";
    } else if (key == "aucune") {
        z = MemZones{};
    } else {
        if (why) *why = "mod\xC3\xA8le inconnu : " + std::string(key);
        return false;
    }
    out = std::move(z);
    return true;
}

// ---------------------------------------------------------------- detection ---
MemZones DetectReport::zones(const std::string& origin) const {
    MemZones z;
    z.declared = true;
    for (const auto& t : tables) z.of(t.table) = t.ranges;
    z.origin = origin;
    return z;
}

DetectTable detectTable(MemTable table, const Reader& read, const std::atomic<bool>* stop, const std::function<void(double)>& progress,
                        int maxRequests) {
    DetectTable out;
    out.table = table;
    const std::uint32_t block = isBits(table) ? 2000u : 125u;
    std::vector<MemRange> found;
    int failures = 0;
    const auto stopped = [&] { return (stop && stop->load()) || out.failed || out.cut; };
    // Une lecture ; -1 (pas de reponse) une fois de plus, puis on renonce.
    const auto ask = [&](std::uint32_t first, std::uint32_t count) {
        if (stopped()) return -1;
        if (out.requests >= maxRequests) {
            out.cut = true;
            return -1;
        }
        ++out.requests;
        int r = read(table, first, count);
        if (r < 0) {
            ++out.requests;
            r = read(table, first, count);
            if (r < 0 && ++failures >= 3) out.failed = true;
        }
        return r;
    };
    const auto report = [&](double f) {
        if (progress) progress(std::clamp(f, 0.0, 1.0));
    };
    // 1. L'adresse 0.
    const int at0 = ask(0, 1);
    if (at0 == 1) {
        out.absent = true;
        out.comment = "fonction refus\xC3\xA9" "e (exception 01) : l'appareil n'en a pas";
        report(1.0);
        return out;
    }
    if (at0 < 0) {
        out.failed = true;
        out.comment = "pas de r\xC3\xA9ponse";
        return out;
    }
    std::uint32_t scanFrom = 0;
    bool contiguous = false;
    if (at0 == 0) {
        // 2. La fin d'un seul tenant, par moities (en supposant la memoire d'un bloc).
        std::uint32_t lo = 0, hi = kSpace;       // lo lisible, hi : pas encore
        while (hi - lo > 1 && !stopped()) {
            const std::uint32_t mid = lo + (hi - lo) / 2;
            const int r = ask(mid, 1);
            if (r < 0) break;
            if (r == 0) lo = mid;
            else hi = mid;
            report(0.15 * (1.0 - static_cast<double>(hi - lo) / kSpace));
        }
        // 3. Verifier le bloc 0..lo par lectures pleines.
        bool whole = !stopped();
        for (std::uint32_t s = 0; whole && s <= lo; s += block) {
            const std::uint32_t n = std::min<std::uint32_t>(block, lo - s + 1);
            const int r = ask(s, n);
            if (r != 0) whole = false;
            report(0.15 + 0.25 * static_cast<double>(s) / std::max<std::uint32_t>(1, lo));
            if (s + block < s) break;       // debordement
        }
        if (whole) {
            found.push_back({0, lo});
            contiguous = true;
            scanFrom = lo + 1;
        }
    }
    // 4. Le balayage par blocs, du reste. Un bloc refuse : quelques cases
    // sondees (un pas de 31 mots, de 125 bits) ; une case lisible : le debut et
    // la fin de sa plage se cherchent par moities. Une plage plus courte que le
    // pas, cachee dans un bloc refuse, peut echapper (c'est dit dans l'aide).
    const auto readable = [&](std::uint32_t a) { return ask(a, 1) == 0; };
    const auto runEnd = [&](std::uint32_t a) {            // a : lisible
        std::uint32_t lo = a, step = 1;
        std::uint64_t hi = kSpace;                         // le premier non lisible connu
        while (!stopped()) {
            const std::uint64_t at = static_cast<std::uint64_t>(lo) + step;
            if (at >= kSpace) break;
            if (readable(static_cast<std::uint32_t>(at))) {
                lo = static_cast<std::uint32_t>(at);
                step = std::min<std::uint32_t>(step * 2, 4096);
            } else {
                hi = at;
                break;
            }
        }
        if (hi == kSpace && !stopped() && lo < kSpace - 1) {
            if (readable(kSpace - 1)) lo = kSpace - 1;
        }
        while (hi - lo > 1 && !stopped()) {
            const std::uint32_t mid = static_cast<std::uint32_t>(lo + (hi - lo) / 2);
            if (readable(mid)) lo = mid;
            else hi = mid;
        }
        return lo;
    };
    const auto runStart = [&](std::uint32_t a, std::uint32_t floor) {   // a : lisible ; pas avant floor
        if (floor < a && readable(floor)) return floor;
        std::uint32_t lo = floor, hi = a;                 // lo : non lisible, hi : lisible
        while (hi - lo > 1 && !stopped()) {
            const std::uint32_t mid = lo + (hi - lo) / 2;
            if (readable(mid)) hi = mid;
            else lo = mid;
        }
        return hi;
    };
    const std::uint32_t stride = isBits(table) ? 125u : 31u;
    std::uint64_t s = scanFrom;
    while (s < kSpace && !stopped()) {
        const std::uint32_t n = static_cast<std::uint32_t>(std::min<std::uint64_t>(block, kSpace - s));
        const auto at = static_cast<std::uint32_t>(s);
        const int r = ask(at, n);
        if (r < 0) break;
        if (r == 0) {
            found.push_back({at, at + n - 1});
            s += n;
            report(0.4 + 0.6 * static_cast<double>(s) / kSpace);
            continue;
        }
        bool hit = false;
        for (std::uint32_t k = 0; k < n && !stopped(); k += stride) {
            const std::uint32_t a = at + std::min(k, n - 1);
            if (!readable(a)) continue;
            const std::uint32_t first = runStart(a, at);
            const std::uint32_t last = runEnd(a);
            found.push_back({first, last});
            hit = true;
            s = static_cast<std::uint64_t>(last) + 1;
            break;
        }
        if (!hit) s += n;
        report(0.4 + 0.6 * static_cast<double>(std::min<std::uint64_t>(s, kSpace)) / kSpace);
    }
    normalize(found);
    out.ranges = found;
    if (out.ranges.size() == 1 && out.ranges.front().first == 0 && out.ranges.front().last == kSpace - 1) {
        out.everywhere = true;
        out.comment = "r\xC3\xA9pond partout (0 \xC3\xA0 65535) : il ne dit pas o\xC3\xB9 s'arr\xC3\xAAte sa m\xC3\xA9moire";
    } else if (out.ranges.empty()) {
        out.comment = out.failed ? std::string("plus de r\xC3\xA9ponse") : std::string("aucune adresse lisible");
    } else if (out.ranges.size() == 1) {
        out.comment = contiguous ? std::string("d'un seul tenant depuis 0") : std::string("une plage");
    } else {
        out.comment = num(static_cast<std::uint32_t>(out.ranges.size())) + " plages" + (contiguous ? " (la premi\xC3\xA8re depuis 0)" : "");
    }
    if (out.cut) out.comment += " - arr\xC3\xAAt\xC3\xA9 au bout de " + num(static_cast<std::uint32_t>(out.requests)) + " requ\xC3\xAAtes";
    if (out.failed && !out.ranges.empty()) out.comment += " - puis plus de r\xC3\xA9ponse";
    report(1.0);
    return out;
}

Detector::~Detector() {
    stop();
}

void Detector::start(Target target) {
    stop();
    target_ = std::move(target);
    stop_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_ = Progress{};
        progress_.running = true;
        report_.reset();
    }
    thread_ = std::thread([this] { run(); });
}

void Detector::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

bool Detector::running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return progress_.running;
}

Detector::Progress Detector::progress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}

std::optional<DetectReport> Detector::report() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return report_;
}

void Detector::run() {
    const auto t0 = std::chrono::steady_clock::now();
    const auto seconds = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    DetectReport rep;
    modbus::Client client({target_.host, target_.port, target_.unit, target_.timeoutMs});
    const auto c = client.connect();
    if (c.ok) modbus::labelClient(client.localName(), "D\xC3\xA9tection des zones");
    if (!c.ok) {
        rep.failed = true;
        rep.why = "connexion \xC3\xA0 " + target_.host + ":" + std::to_string(target_.port) + " impossible : " + c.why;
    } else {
        int total = 0;
        const Reader reader = [&](MemTable t, std::uint32_t first, std::uint32_t count) -> int {
            modbus::Outcome o;
            if (!client.connected()) {
                const auto again = client.connect();
                if (!again.ok) return -1;
            }
            if (isBits(t)) {
                std::vector<bool> bits;
                o = client.readBits(t == MemTable::DiscreteInputs, static_cast<std::uint16_t>(first), static_cast<std::uint16_t>(count), bits);
            } else {
                std::vector<std::uint16_t> words;
                o = client.readRegisters(t == MemTable::InputRegisters, static_cast<std::uint16_t>(first), static_cast<std::uint16_t>(count), words);
            }
            ++total;
            if (o.ok) return 0;
            if (o.exception) return o.exception;
            if (o.lost) client.disconnect();
            return -1;
        };
        const MemTable order[] = {MemTable::Coils, MemTable::DiscreteInputs, MemTable::InputRegisters, MemTable::Holding};
        for (int i = 0; i < 4 && !stop_.load(); ++i) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                progress_.table = i;
                progress_.line = std::string(tableLabel(order[i]));
            }
            auto t = detectTable(order[i], reader, &stop_, [&](double f) {
                std::lock_guard<std::mutex> lock(mutex_);
                progress_.fraction = (i + f) / 4.0;
                progress_.requests = total;
                progress_.seconds = seconds();
            });
            rep.requests += t.requests;
            if (t.failed && t.ranges.empty() && !t.absent) {
                rep.failed = true;
                rep.why = std::string(tableLabel(order[i])) + " : plus de r\xC3\xA9ponse de " + target_.host;
            }
            rep.tables.push_back(std::move(t));
            if (rep.failed) break;
        }
        client.disconnect();
    }
    rep.stopped = stop_.load();
    rep.seconds = seconds();
    std::lock_guard<std::mutex> lock(mutex_);
    progress_.running = false;
    progress_.done = true;
    progress_.fraction = 1.0;
    progress_.requests = rep.requests;
    progress_.seconds = rep.seconds;
    report_ = std::move(rep);
}

// ----------------------------------------------------------------- le scanner ---
Scanner::~Scanner() {
    stop();
}

void Scanner::start(Target target, const MemZones& zones, const MemZones& fallback) {
    stop();
    target_ = std::move(target);
    zones_ = zones.declared ? zones : fallback;
    stop_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& c : cells_) c.assign(kSpace, CellStat{});
        state_ = State{};
        state_.running = true;
    }
    thread_ = std::thread([this] { run(); });
}

void Scanner::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    state_.running = false;
}

bool Scanner::running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.running;
}

Scanner::State Scanner::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::optional<CellStat> Scanner::stat(MemTable t, std::uint32_t offset) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& c = cells_[static_cast<std::size_t>(t)];
    if (offset >= c.size() || !c[offset].seen) return std::nullopt;
    return c[offset];
}

std::size_t Scanner::activeCells() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t n = 0;
    for (const auto& c : cells_)
        for (const auto& x : c)
            if (x.active()) ++n;
    return n;
}

MemZones Scanner::scanned() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return zones_;
}

void Scanner::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& c : cells_) std::fill(c.begin(), c.end(), CellStat{});
}

void Scanner::run() {
    const auto t0 = std::chrono::steady_clock::now();
    modbus::Client client({target_.host, target_.port, target_.unit, target_.timeoutMs});
    while (!stop_.load()) {
        const auto passStart = std::chrono::steady_clock::now();
        if (!client.connected()) {
            const auto c = client.connect();
            if (!c.ok) {
                std::lock_guard<std::mutex> lock(mutex_);
                state_.why = "connexion \xC3\xA0 " + target_.host + ":" + std::to_string(target_.port) + " impossible : " + c.why;
            } else {
                modbus::labelClient(client.localName(), "Scanner de la carte");
            }
        }
        int requests = 0;
        std::string why;
        for (const auto t : kMemTables) {
            if (stop_.load() || !client.connected()) break;
            const std::uint32_t block = isBits(t) ? 2000u : 125u;
            for (const auto& r : zones_.of(t)) {
                for (std::uint64_t s = r.first; s <= r.last && !stop_.load() && client.connected(); s += block) {
                    const auto n = static_cast<std::uint16_t>(std::min<std::uint64_t>(block, r.last - s + 1));
                    std::vector<std::uint16_t> values;
                    modbus::Outcome o;
                    if (isBits(t)) {
                        std::vector<bool> bits;
                        o = client.readBits(t == MemTable::DiscreteInputs, static_cast<std::uint16_t>(s), n, bits);
                        for (bool b : bits) values.push_back(b ? 1 : 0);
                    } else {
                        o = client.readRegisters(t == MemTable::InputRegisters, static_cast<std::uint16_t>(s), n, values);
                    }
                    ++requests;
                    if (!o.ok) {
                        why = std::string(tableLabel(t)) + " " + modicon(t, static_cast<std::uint32_t>(s)) + " : "
                              + (o.exception ? modbus::exceptionText(o.exception) : o.why);
                        if (o.lost) client.disconnect();
                        continue;
                    }
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto& cells = cells_[static_cast<std::size_t>(t)];
                    for (std::size_t i = 0; i < values.size() && s + i < kSpace; ++i) {
                        auto& c = cells[s + i];
                        const std::uint16_t v = values[i];
                        if (!c.seen) {
                            c.seen = true;
                            c.value = c.min = c.max = v;
                        } else {
                            if (v != c.value) ++c.changes;
                            c.value = v;
                            c.min = std::min(c.min, v);
                            c.max = std::max(c.max, v);
                        }
                    }
                }
            }
        }
        const double passMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - passStart).count();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++state_.passes;
            state_.requestsPerPass = requests;
            state_.passMs = passMs;
            state_.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (!why.empty()) state_.why = why;
        }
        // La passe suivante, a la periode (par petits pas : Arreter repond vite).
        const int wait = std::max(50, target_.periodMs - static_cast<int>(passMs));
        for (int waited = 0; waited < wait && !stop_.load(); waited += 20) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    client.disconnect();
}

// ------------------------------------------------------------------- la carte ---
std::vector<std::string> writersOf(const Project& p, std::string_view path) {
    const std::string leaf = keyOf(path);
    std::vector<std::string> out;
    std::set<std::string> seen;
    const auto add = [&](std::string s) {
        if (seen.insert(s).second) out.push_back(std::move(s));
    };
    const auto codeWrites = [&](std::string_view body) -> int {
        for (const auto& [written, line] : assignedPaths(body))
            if (covers(keyOf(written), leaf)) return std::max(1, line);
        return 0;
    };
    for (const auto& v : p.views) {
        for (const auto& o : v.objects) {
            if (kindWritesVariable(o.kind))
                for (const auto& prop : o.props)
                    if ((prop.key == "variable" || prop.key == "output") && covers(keyOf(prop.value), leaf))
                        add(o.name + " (" + v.name + ", " + std::string(kindLabel(o.kind)) + ")");
            for (const auto& a : o.actions) {
                if (operationWritesVariable(a.operation) && covers(keyOf(a.target), leaf))
                    add(o.name + " (" + v.name + ", action " + std::string(operationLabel(a.operation)) + ")");
                if (a.operation == Operation::RunScript)
                    if (const int line = codeWrites(a.value)) add(o.name + " (" + v.name + ", script de l'action, ligne " + std::to_string(line) + ")");
            }
        }
        for (const auto& a : v.actions) {
            if (operationWritesVariable(a.operation) && covers(keyOf(a.target), leaf))
                add(v.name + " (action de la vue, " + std::string(operationLabel(a.operation)) + ")");
            if (a.operation == Operation::RunScript)
                if (const int line = codeWrites(a.value)) add(v.name + " (script d'une action de la vue, ligne " + std::to_string(line) + ")");
        }
        for (const auto& sc : v.scripts)
            if (sc.lang == ScriptLang::ST)
                if (const int line = codeWrites(sc.body)) add("script " + sc.name + " de " + v.name + " (ligne " + std::to_string(line) + ")");
    }
    for (const auto& sc : p.programs.scripts)
        if (sc.lang == ScriptLang::ST)
            if (const int line = codeWrites(sc.body)) add("script " + sc.name + " (ligne " + std::to_string(line) + ")");
    for (const auto& f : p.programs.functions)
        if (const int line = codeWrites(f.body)) add("fonction " + f.name + " (ligne " + std::to_string(line) + ")");
    for (const auto& r : p.recipes)
        for (const auto& f : r.fields)
            if (covers(keyOf(f.variable), leaf)) add("la recette " + r.name);
    return out;
}

// ------------------------------------------------- l'index des ecritures (1.11.2) ---
namespace {
// La racine d'un chemin : avant le premier '.' ou '['. covers(w, f) exige la meme
// racine (hors des crochets, les caracteres doivent etre egaux).
std::string rootOf(const std::string& key) {
    const auto k = key.find_first_of(".[");
    return k == std::string::npos ? key : key.substr(0, k);
}
} // namespace

WriteIndex::WriteIndex(const Project& p) {
    // Le meme parcours que writersOf(projet, chemin), dans le meme ordre ; un endroit
    // (Site) par appel possible de add(), une entree par chemin qu'il ecrit.
    const auto site = [&](std::string label, std::string tail, bool code) {
        sites_.push_back({std::move(label), std::move(tail), code});
        return static_cast<std::uint32_t>(sites_.size() - 1);
    };
    const auto entry = [&](std::string key, std::uint32_t s, int line) {
        auto root = rootOf(key);
        roots_[std::move(root)].push_back({std::move(key), s, line});
        ++entries_;
    };
    const auto plain = [&](std::string_view written, const std::function<std::string()>& label) {
        std::string key = keyOf(written);
        if (key.empty()) return;                     // covers("", ...) est faux
        entry(std::move(key), site(label(), {}, false), 0);
    };
    const auto code = [&](std::string_view body, const std::function<std::string()>& label, std::string tail) {
        const auto paths = assignedPaths(body);      // une seule analyse par script
        if (paths.empty()) return;
        const auto s = site(label(), std::move(tail), true);
        for (const auto& [written, line] : paths) {
            std::string key = keyOf(written);
            if (!key.empty()) entry(std::move(key), s, std::max(1, line));
        }
    };
    for (const auto& v : p.views) {
        for (const auto& o : v.objects) {
            if (kindWritesVariable(o.kind))
                for (const auto& prop : o.props)
                    if (prop.key == "variable" || prop.key == "output")
                        plain(prop.value, [&] { return o.name + " (" + v.name + ", " + std::string(kindLabel(o.kind)) + ")"; });
            for (const auto& a : o.actions) {
                if (operationWritesVariable(a.operation))
                    plain(a.target, [&] { return o.name + " (" + v.name + ", action " + std::string(operationLabel(a.operation)) + ")"; });
                if (a.operation == Operation::RunScript)
                    code(a.value, [&] { return o.name + " (" + v.name + ", script de l'action, ligne "; }, ")");
            }
        }
        for (const auto& a : v.actions) {
            if (operationWritesVariable(a.operation))
                plain(a.target, [&] { return v.name + " (action de la vue, " + std::string(operationLabel(a.operation)) + ")"; });
            if (a.operation == Operation::RunScript)
                code(a.value, [&] { return v.name + " (script d'une action de la vue, ligne "; }, ")");
        }
        for (const auto& sc : v.scripts)
            if (sc.lang == ScriptLang::ST)
                code(sc.body, [&] { return "script " + sc.name + " de " + v.name + " (ligne "; }, ")");
    }
    for (const auto& sc : p.programs.scripts)
        if (sc.lang == ScriptLang::ST)
            code(sc.body, [&] { return "script " + sc.name + " (ligne "; }, ")");
    for (const auto& f : p.programs.functions)
        code(f.body, [&] { return "fonction " + f.name + " (ligne "; }, ")");
    for (const auto& r : p.recipes)
        for (const auto& f : r.fields)
            plain(f.variable, [&] { return "la recette " + r.name; });
}

std::vector<std::string> WriteIndex::writersOf(std::string_view path) const {
    const std::string leaf = keyOf(path);
    std::vector<std::string> out;
    const auto it = roots_.find(rootOf(leaf));
    if (it == roots_.end()) return out;
    std::set<std::string> seen;
    // Les entrees d'une racine sont dans l'ordre du projet, celles d'un meme endroit
    // a la suite : la premiere qui couvre la case donne la ligne (comme codeWrites).
    std::uint32_t done = std::numeric_limits<std::uint32_t>::max();
    for (const auto& e : it->second) {
        if (e.site == done || !covers(e.key, leaf)) continue;
        done = e.site;
        const auto& s = sites_[e.site];
        std::string text = s.code ? s.label + std::to_string(e.line) + s.tail : s.label;
        if (seen.insert(text).second) out.push_back(std::move(text));
    }
    return out;
}

namespace {
// L'empreinte de ce que lit l'index (FNV-1a, longueurs comprises) : une passe sans
// analyse, bien moins chere que l'index lui-meme.
struct Print {
    std::uint64_t h{1469598103934665603ull};
    void bytes(const void* d, std::size_t n) {
        const auto* c = static_cast<const unsigned char*>(d);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= c[i];
            h *= 1099511628211ull;
        }
    }
    void num(std::uint64_t v) { bytes(&v, sizeof v); }
    void str(std::string_view s) {
        num(s.size());
        bytes(s.data(), s.size());
    }
};
std::uint64_t printOf(const Project& p) {
    Print f;
    f.num(p.views.size());
    for (const auto& v : p.views) {
        f.str(v.name);
        f.num(v.objects.size());
        for (const auto& o : v.objects) {
            f.str(o.name);
            f.num(static_cast<std::uint64_t>(o.kind));
            if (kindWritesVariable(o.kind)) {
                f.num(o.props.size());
                for (const auto& prop : o.props)
                    if (prop.key == "variable" || prop.key == "output") {
                        f.str(prop.key);
                        f.str(prop.value);
                    }
            }
            f.num(o.actions.size());
            for (const auto& a : o.actions) {
                f.num(static_cast<std::uint64_t>(a.operation));
                f.str(a.target);
                f.str(a.value);
            }
        }
        f.num(v.actions.size());
        for (const auto& a : v.actions) {
            f.num(static_cast<std::uint64_t>(a.operation));
            f.str(a.target);
            f.str(a.value);
        }
        f.num(v.scripts.size());
        for (const auto& sc : v.scripts) {
            f.str(sc.name);
            f.num(static_cast<std::uint64_t>(sc.lang));
            f.str(sc.body);
        }
    }
    f.num(p.programs.scripts.size());
    for (const auto& sc : p.programs.scripts) {
        f.str(sc.name);
        f.num(static_cast<std::uint64_t>(sc.lang));
        f.str(sc.body);
    }
    f.num(p.programs.functions.size());
    for (const auto& fn : p.programs.functions) {
        f.str(fn.name);
        f.str(fn.body);
    }
    f.num(p.recipes.size());
    for (const auto& r : p.recipes) {
        f.str(r.name);
        f.num(r.fields.size());
        for (const auto& fl : r.fields) f.str(fl.variable);
    }
    return f.h;
}
std::mutex                        gIndexMutex;
std::uint64_t                     gIndexPrint{0};
std::shared_ptr<const WriteIndex> gIndex;
std::atomic<std::size_t>          gIndexBuilds{0};
} // namespace

std::shared_ptr<const WriteIndex> writeIndexOf(const Project& p) {
    const std::uint64_t print = printOf(p);
    std::lock_guard<std::mutex> lock(gIndexMutex);
    if (gIndex && gIndexPrint == print) return gIndex;
    gIndex = std::make_shared<const WriteIndex>(p);
    gIndexPrint = print;
    ++gIndexBuilds;
    return gIndex;
}

std::size_t writeIndexBuilds() noexcept { return gIndexBuilds.load(); }

std::size_t MemoryMap::count(int severity) const noexcept {
    std::size_t n = 0;
    for (const auto& c : conflicts)
        if (c.severity == severity) ++n;
    return n;
}
std::size_t MemoryMap::outOfZone() const noexcept {
    std::size_t n = 0;
    for (const auto& v : vars)
        if (v.outOfZone) ++n;
    return n;
}
std::vector<std::size_t> MemoryMap::at(MemTable t, std::uint32_t offset) const {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < vars.size(); ++i)
        if (vars[i].table == t && offset >= vars[i].first && offset <= vars[i].last) out.push_back(i);
    return out;
}

namespace {
void judge(MemoryMap& m) {
    std::vector<std::size_t> order(m.vars.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const auto& x = m.vars[a];
        const auto& y = m.vars[b];
        if (x.table != y.table) return x.table < y.table;
        return x.first < y.first;
    });
    for (std::size_t i = 0; i < order.size(); ++i) {
        const auto& a = m.vars[order[i]];
        for (std::size_t j = i + 1; j < order.size(); ++j) {
            const auto& b = m.vars[order[j]];
            if (b.table != a.table || b.first > a.last) break;
            if (a.bit >= 0 && b.bit >= 0 && a.bit != b.bit) continue;    // deux bits d'un meme mot
            MapConflict c;
            c.a = order[i];
            c.b = order[j];
            c.table = a.table;
            c.first = std::max(a.first, b.first);
            c.last = std::min(a.last, b.last);
            c.severity = a.written() && b.written() ? 3 : (a.written() || b.written()) ? 2 : 1;
            m.conflicts.push_back(c);
        }
    }
    for (const auto& c : m.conflicts) {
        m.vars[c.a].verdict = std::max(m.vars[c.a].verdict, c.severity);
        m.vars[c.b].verdict = std::max(m.vars[c.b].verdict, c.severity);
    }
}
} // namespace

MemoryMap buildMap(const Project& p, const Equipment& e) {
    MemoryMap m;
    m.equipment = e.name;
    m.zones = e.zones;
    m.lowWordFirst = e.wordOrder != "fort";
    const auto index = writeIndexOf(p);          // 1.11.2 : une passe, pas une par variable
    for (const auto* top : equip::boundVariables(p, e)) {
        std::set<std::string> corrected;
        for (const auto& pl : top->places) corrected.insert(keyOf(top->name + "." + pl.path));
        for (const auto& v : types::leafVariables(p, *top)) {
            comm::Point pt;
            if (v.address.empty() || !equip::placeEquipmentAddress(v.address, equip::registerType(v), pt)) continue;
            MapVar mv;
            mv.name = v.name;
            mv.root = top->name;
            mv.address = v.address;
            mv.type = v.scaled() && !v.rawType.empty() ? v.rawType : v.type;
            mv.table = tableOf(pt.area);
            mv.first = pt.offset;
            mv.last = pt.offset + (pt.bits() ? 0u : static_cast<std::uint32_t>(std::max<int>(1, pt.size)) - 1u);
            mv.bit = pt.encoding == comm::Encoding::BitOfWord ? pt.bit : -1;
            mv.writable = !v.readOnly && !top->readOnly && e.writes && (mv.table == MemTable::Coils || mv.table == MemTable::Holding);
            mv.corrected = corrected.count(keyOf(v.name)) != 0;
            mv.outOfZone = e.zones.declared && !contains(e.zones.of(mv.table), mv.first, mv.last - mv.first + 1);
            mv.writers = index->writersOf(v.name);
            m.vars.push_back(std::move(mv));
        }
    }
    judge(m);
    return m;
}

MemoryMap buildPlcMap(const Project& p, const comm::Plan& plan, std::uint32_t plcBits, std::uint32_t plcWords) {
    MemoryMap m;
    m.equipment = "automate du projet";
    m.lowWordFirst = p.comm.wordOrder != "fort";
    const auto index = writeIndexOf(p);          // 1.11.2 : une passe, pas une par variable
    if (plcWords > 0) {
        m.zones.declared = true;
        if (plcBits) {
            m.zones.of(MemTable::Coils).push_back({0, plcBits - 1});
            m.zones.of(MemTable::DiscreteInputs).push_back({0, plcBits - 1});
        }
        m.zones.of(MemTable::Holding).push_back({0, plcWords - 1});
        m.zones.of(MemTable::InputRegisters).push_back({0, plcWords - 1});
        m.zones.origin = "configuration de l'automate";
    }
    for (const auto& pt : plan.points()) {
        MapVar mv;
        mv.name = pt.name;
        mv.root = pt.name;
        mv.address = pt.address;
        mv.type = pt.typeName;
        mv.table = tableOf(pt.area);
        mv.first = pt.offset;
        const std::size_t span = std::max<std::size_t>(1, pt.span());
        mv.last = pt.offset + static_cast<std::uint32_t>(span) - 1u;
        mv.bit = pt.encoding == comm::Encoding::BitOfWord ? pt.bit : -1;
        mv.writable = pt.writable && p.comm.writes;
        mv.outOfZone = m.zones.declared && !contains(m.zones.of(mv.table), mv.first, mv.last - mv.first + 1);
        mv.writers = index->writersOf(pt.name);
        m.vars.push_back(std::move(mv));
    }
    judge(m);
    return m;
}

std::string conflictText(const MemoryMap& m, const MapConflict& c) {
    const auto& a = m.vars[c.a];
    const auto& b = m.vars[c.b];
    const auto how = [](const MapVar& v) {
        if (!v.written()) return std::string("lue");
        return "\xC3\xA9" "crite par " + v.writers.front() + (v.writers.size() > 1 ? " (+" + std::to_string(v.writers.size() - 1) + ")" : std::string{});
    };
    const std::string where = c.first == c.last ? modicon(c.table, c.first) : modicon(c.table, c.first) + " \xC3\xA0 " + modicon(c.table, c.last);
    const std::string head = c.severity == 3 ? "deux \xC3\xA9" "critures" : c.severity == 2 ? "une \xC3\xA9" "criture et une lecture" : "partag\xC3\xA9 en lecture";
    return head + " sur " + where + " : " + a.name + " (" + how(a) + ") et " + b.name + " (" + how(b) + ")";
}

std::vector<std::pair<std::string, std::string>> interpretations(std::uint16_t word, std::optional<std::uint16_t> next, bool low) {
    std::vector<std::pair<std::string, std::string>> out;
    char b[64];
    out.push_back({"INT / UINT", std::to_string(static_cast<std::int16_t>(word)) + " / " + std::to_string(word)});
    std::snprintf(b, sizeof b, "0x%04X", word);
    out.push_back({"hexa", b});
    std::string bin;
    for (int i = 15; i >= 0; --i) {
        bin.push_back((word >> i) & 1 ? '1' : '0');
        if (i % 4 == 0 && i) bin.push_back(' ');
    }
    out.push_back({"binaire", bin});
    if (next) {
        const std::uint32_t v = low ? (static_cast<std::uint32_t>(*next) << 16) | word : (static_cast<std::uint32_t>(word) << 16) | *next;
        out.push_back({"avec le suivant : DINT", std::to_string(static_cast<std::int32_t>(v))});
        out.push_back({"avec le suivant : UDINT", std::to_string(v)});
        float f = 0;
        static_assert(sizeof f == sizeof v, "float 32 bits");
        std::memcpy(&f, &v, sizeof f);
        const double d = f;
        if (std::isfinite(d)) {
            std::snprintf(b, sizeof b, "%.6g", d);
            const bool plausible = d == 0.0 || (std::fabs(d) >= 1e-3 && std::fabs(d) <= 1e7);
            out.push_back({"avec le suivant : REAL", std::string(b) + (plausible ? " \xE2\x86\x90 vraisemblable" : "")});
        } else {
            out.push_back({"avec le suivant : REAL", "pas un nombre"});
        }
    }
    return out;
}

} // namespace hmi::zones
