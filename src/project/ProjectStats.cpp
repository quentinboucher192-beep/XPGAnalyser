// =============================================================================
//  project/ProjectStats.cpp - lot API 7 : les statistiques du projet ouvert
// =============================================================================
#include "ProjectStats.hpp"

#include "BlockLibrary.hpp"
#include "MemberTree.hpp"
#include "TypeUsage.hpp"
#include "../domain/ExecutionOrder.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <optional>
#include <unordered_set>

namespace project::stats {

using namespace domain;

namespace {

constexpr std::size_t kNpos = static_cast<std::size_t>(-1);
constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

// Des tests ASCII faits main, comme l'analyseur : std::isalpha depend de la
// locale, et un identificateur CEI 61131-3 n'en depend pas.
constexpr char asciiLower(char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
constexpr char asciiUpper(char c) noexcept { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }
constexpr bool identStart(char c) noexcept { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
constexpr bool identChar(char c) noexcept { return identStart(c) || (c >= '0' && c <= '9'); }
constexpr bool blank(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = asciiLower(c);
    return out;
}

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = asciiUpper(c);
    return out;
}

std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && blank(s.front())) s.remove_prefix(1);
    while (!s.empty() && blank(s.back())) s.remove_suffix(1);
    return s;
}

bool equalNoCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (asciiLower(a[i]) != asciiLower(b[i])) return false;
    return true;
}

bool startsNoCase(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() && equalNoCase(s.substr(0, prefix.size()), prefix);
}

std::string text(const Project& p, SymbolId id) { return std::string(p.strings.text(id)); }

// Un produit qui ne deborde pas : un tableau de 10^15 elements (members::parseArray
// plafonne la) d'un type de mille octets depasse 64 bits.
std::uint64_t mulSat(std::uint64_t a, std::uint64_t b) noexcept {
    if (a != 0 && b > kMax / a) return kMax;
    return a * b;
}
std::uint64_t addSat(std::uint64_t a, std::uint64_t b) noexcept { return b > kMax - a ? kMax : a + b; }

std::size_t langIndex(Language l) noexcept { return static_cast<std::size_t>(l); }
std::size_t scopeIndex(Scope s) noexcept { return static_cast<std::size_t>(s); }

struct Elementary { std::string_view name; std::uint64_t bytes; };
constexpr Elementary kElementary[] = {
    {"BOOL", 1}, {"EBOOL", 1}, {"BYTE", 1}, {"SINT", 1}, {"USINT", 1},
    {"INT", 2}, {"UINT", 2}, {"WORD", 2},
    {"DINT", 4}, {"UDINT", 4}, {"DWORD", 4}, {"REAL", 4}, {"TIME", 4}, {"DATE", 4}, {"TOD", 4}, {"TIME_OF_DAY", 4},
    {"LINT", 8}, {"ULINT", 8}, {"LWORD", 8}, {"LREAL", 8}, {"DT", 8}, {"DATE_AND_TIME", 8},
};

// La racine d'un chemin : "manu_ouv[0]" -> "manu_ouv", "a.b" -> "a".
std::string_view rootOf(std::string_view path) noexcept {
    path = trim(path);
    std::size_t end = 0;
    while (end < path.size() && identChar(path[end])) ++end;
    return path.substr(0, end);
}

bool inputOutput(MemoryArea a) noexcept {
    switch (a) {
        case MemoryArea::Input: case MemoryArea::InputWord: case MemoryArea::Output: case MemoryArea::OutputWord:
        case MemoryArea::Topological: return true;
        default: return false;
    }
}

std::optional<Scope> scopeOf(VariableScope s) noexcept {
    switch (s) {
        case VariableScope::Global: case VariableScope::Constant: return Scope::Global;
        case VariableScope::Local: case VariableScope::Public: return Scope::Local;
        case VariableScope::Input: case VariableScope::Output: case VariableScope::InOut: return Scope::Parameter;
        case VariableScope::DerivedMember: break;
    }
    return std::nullopt;
}

// ============================================================ la lecture du code ==
//  Qui est lu, qui est ecrit : une passe par section, sans copie du texte.
//  Les noms sont compares SANS la casse et seuls ceux qu'on cherche (les
//  globales, et dans une unite ses parametres lies a une globale) comptent.
struct Use {
    bool read{false}, written{false};
};

// Les mots du ST qui ne sont pas des noms. Apres THEN, ELSE, DO, REPEAT et FOR,
// une instruction commence (FOR i := 0 : i est ecrit).
bool keyword(const std::string& w) {
    static const std::unordered_set<std::string> k{
        "if", "then", "else", "elsif", "end_if", "case", "of", "end_case", "for", "to", "by", "do", "end_for",
        "while", "end_while", "repeat", "until", "end_repeat", "return", "exit", "and", "or", "xor", "not", "mod",
        "true", "false"};
    return k.count(w) != 0;
}
bool opensStatement(const std::string& w) { return w == "then" || w == "else" || w == "do" || w == "repeat" || w == "for"; }
bool writesItsArgument(const std::string& w) { return w == "set" || w == "reset" || w == "inc" || w == "dec"; }

// Apres un nom : ses membres et ses indices (.a[3].b), et les blancs.
std::size_t afterPath(std::string_view s, std::size_t k) noexcept {
    const auto n = s.size();
    for (;;) {
        while (k < n && blank(s[k])) ++k;
        if (k < n && s[k] == '.') {
            ++k;
            while (k < n && blank(s[k])) ++k;
            while (k < n && identChar(s[k])) ++k;
            continue;
        }
        if (k < n && s[k] == '[') {
            int depth = 0;
            while (k < n) {
                if (s[k] == '[') ++depth;
                else if (s[k] == ']' && --depth == 0) { ++k; break; }
                ++k;
            }
            continue;
        }
        return k;
    }
}

class Scanner {
public:
    Scanner(const std::unordered_map<std::string, std::size_t>& globals, const std::unordered_set<std::string>& inouts,
            std::vector<Use>& uses)
        : globals_(globals), inouts_(inouts), uses_(uses) {}

    // Les noms propres au POU dont on lit le code : ses parametres et ses
    // variables CACHENT une globale du meme nom (kNpos), sauf un parametre
    // d'unite lie a une globale (EffectiveParameter), qui EST cette globale.
    // Nul : une section de la tache, rien ne cache rien.
    void setScope(const std::unordered_map<std::string, std::size_t>* names) noexcept { names_ = names; }

    // Du ST, instruction par instruction.
    void statements(std::string_view s) {
        bool start = true;        // en tete d'instruction
        bool arrow = false;       // "=>" : le nom qui suit est ecrit (une sortie liee)
        bool writeArg = false;    // SET( RESET( INC( DEC( : leur argument est ecrit
        bool inoutArg = false;    // "P :=" ou P est une entree-sortie d'un bloc
        char prev = ';';          // le dernier caractere significatif
        const auto n = s.size();
        std::size_t i = 0;
        while (i < n) {
            const char c = s[i];
            if (c == '(' && i + 1 < n && s[i + 1] == '*') { i = skipTo(s, i + 2, "*)"); continue; }
            if (c == '/' && i + 1 < n && s[i + 1] == '/') { i = skipTo(s, i + 2, "\n"); continue; }
            if (c == '\'' || c == '"') {
                // Une chaine : '$'' y est une apostrophe, pas sa fin.
                std::size_t k = i + 1;
                while (k < n && s[k] != c) k += (s[k] == '$' && k + 1 < n) ? 2 : 1;
                i = k < n ? k + 1 : n;
                prev = c;
                start = false;
                continue;
            }
            if (identStart(c) && (i == 0 || !identChar(s[i - 1]))) {
                std::size_t j = i + 1;
                while (j < n && identChar(s[j])) ++j;
                const char before = prev;
                prev = 'a';
                const auto word = s.substr(i, j - i);
                i = j;
                // Un membre (a.b), une adresse (%MW10), un litteral (16#FF, T#5s).
                if (before == '.' || before == '%' || before == '#') { start = false; continue; }
                key_.assign(word);
                for (auto& ch : key_) ch = asciiLower(ch);
                if (keyword(key_)) {
                    start = opensStatement(key_);
                    arrow = writeArg = inoutArg = false;
                    continue;
                }
                const auto k = afterPath(s, j);
                const bool assign = k + 1 < n && s[k] == ':' && s[k + 1] == '=';
                const bool output = k + 1 < n && s[k] == '=' && s[k + 1] == '>';
                if ((assign && !start) || output) {
                    // Le nom d'un parametre formel dans un appel : Tempo(IN := x, Q => y).
                    inoutArg = assign && inouts_.count(key_) != 0;
                    start = false;
                    continue;
                }
                if (k < n && s[k] == '(' && writesItsArgument(key_)) {
                    writeArg = true;
                    start = false;
                    continue;
                }
                if (Use* u = find()) {
                    if (assign || arrow || writeArg) u->written = true;
                    else if (inoutArg) u->read = u->written = true;
                    else u->read = true;
                }
                arrow = writeArg = inoutArg = false;
                start = false;
                continue;
            }
            if (c == ';') {
                start = true;
                arrow = writeArg = inoutArg = false;
                prev = c;
                ++i;
                continue;
            }
            if (c == ':' && i + 1 < n && s[i + 1] == '=') { prev = '='; start = false; i += 2; continue; }
            if (c == ':') { start = true; prev = c; ++i; continue; }        // un libelle de CASE : une instruction suit
            if (c == '=' && i + 1 < n && s[i + 1] == '>') { arrow = true; prev = '>'; start = false; i += 2; continue; }
            if (!blank(c)) {
                prev = c;
                start = false;
                if (c == ',' || c == ')') arrow = writeArg = inoutArg = false;
            }
            ++i;
        }
    }

    // LD, FBD, SFC, IL : le sens ne se lit pas ici. Chaque nom, ou qu'il soit
    // (le texte d'un schema porte ses noms entre guillemets), est lu ET ecrit :
    // jamais une fausse alerte « jamais lue » ou « jamais ecrite ».
    void anyDirection(std::string_view s) {
        eachName(s, [](Use& u) { u.read = u.written = true; });
    }

    // Une condition d'activation : des lectures.
    void readsOnly(std::string_view s) {
        eachName(s, [](Use& u) { u.read = true; });
    }

private:
    static std::size_t skipTo(std::string_view s, std::size_t from, std::string_view end) noexcept {
        const auto at = s.find(end, from);
        return at == std::string_view::npos ? s.size() : at + end.size();
    }

    template <typename F>
    void eachName(std::string_view s, F&& mark) {
        const auto n = s.size();
        std::size_t i = 0;
        char prev = 0;
        while (i < n) {
            const char c = s[i];
            if (identStart(c) && (i == 0 || !identChar(s[i - 1]))) {
                std::size_t j = i + 1;
                while (j < n && identChar(s[j])) ++j;
                if (prev != '.' && prev != '%' && prev != '#') {
                    key_.assign(s.substr(i, j - i));
                    for (auto& ch : key_) ch = asciiLower(ch);
                    if (Use* u = find()) mark(*u);
                }
                prev = 'a';
                i = j;
                continue;
            }
            if (!blank(c)) prev = c;
            ++i;
        }
    }

    Use* find() {
        if (names_) {
            const auto a = names_->find(key_);
            if (a != names_->end()) return a->second == kNpos ? nullptr : &uses_[a->second];
        }
        const auto g = globals_.find(key_);
        return g == globals_.end() ? nullptr : &uses_[g->second];
    }

    const std::unordered_map<std::string, std::size_t>& globals_;
    const std::unordered_set<std::string>&              inouts_;
    const std::unordered_map<std::string, std::size_t>* names_{nullptr};
    std::vector<Use>&                                   uses_;
    std::string                                         key_;   // le nom en minuscules, sans allocation a chaque fois
};

// ------------------------------------------------------------ le code ----
std::string mainTask(const Project& p) {
    for (const auto& t : p.tasks)
        if (equalNoCase(p.strings.text(t.name), "MAST")) return std::string(p.strings.text(t.name));
    return p.tasks.empty() ? std::string("MAST") : std::string(p.strings.text(p.tasks.front().name));
}

void codeStats(const Project& p, const std::string& task, CodeStats& c) {
    c.task = task;
    // Les entrees de l'onglet Ordre d'execution, telles quelles (noms, rangs) ;
    // leurs lignes se recomptent plus bas, section par section, pour que
    // chacune ait son langage.
    const auto entries = api::entriesOf(p, task);
    c.entries.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        EntryLines l;
        l.name = e.name;
        l.unit = e.unit;
        l.rank = i + 1;
        l.section = e.section;
        l.unitIndex = e.unitIndex;
        c.entries.push_back(std::move(l));
    }
    // Le MEME parcours que entriesOf, pas a pas : une section de la tache est
    // l'entree suivante (meme une section que l'ordre citerait deux fois), une
    // unite l'est a sa premiere section et garde les suivantes. Retrouver
    // l'entree par l'indice de sa section donnait deux fois les lignes a la
    // premiere et aucune a la seconde.
    std::vector<char> inTask(p.sections.size(), 0);
    std::unordered_map<Index, std::size_t> unitAt;
    std::size_t next = 0;
    for (const auto& step : executionOrder(p, task)) {
        if (step.section >= p.sections.size()) continue;
        std::size_t at = 0;
        if (step.fromProgramUnit && step.unit < p.pous.size()) {
            const auto [it, first] = unitAt.try_emplace(step.unit, next);
            if (first) ++next;
            at = it->second;
        } else {
            at = next++;
        }
        if (at >= c.entries.size()) continue;       // jamais : les deux parcours sont les memes
        const auto& sec = p.sections[step.section];
        const auto lang = languageOf(sec.language);
        auto& e = c.entries[at];
        e.byLanguage[langIndex(lang)] += sec.lineCount;
        e.lines += sec.lineCount;
        ++e.sections;
        c.byLanguage[langIndex(lang)] += sec.lineCount;
        c.lines += sec.lineCount;
        SectionLines sl;
        sl.name = text(p, sec.name);
        sl.unit = e.unit ? e.name : std::string{};
        sl.section = step.section;
        sl.owner = e.unit ? e.unitIndex : kNoIndex;
        sl.language = lang;
        sl.lines = sec.lineCount;
        c.sections.push_back(std::move(sl));
        inTask[step.section] = 1;
    }
    for (const auto& e : c.entries) {
        if (e.unit) {
            ++c.units;
            c.unitSections += e.sections;
        } else {
            ++c.taskSections;
        }
    }
    // Hors de la tache : le corps des DFB, les sous-routines, le reste.
    std::vector<char> dfbSeen(p.pous.size(), 0);
    for (std::size_t i = 0; i < p.sections.size(); ++i) {
        if (inTask[i]) continue;
        const auto& sec = p.sections[i];
        if (sec.owner < p.pous.size() && p.pous[sec.owner].kind == PouKind::FunctionBlockType) {
            c.dfbLines += sec.lineCount;
            if (!dfbSeen[sec.owner]) {
                dfbSeen[sec.owner] = 1;
                ++c.dfbs;
            }
        } else if (sec.isSubroutine) {
            c.subroutineLines += sec.lineCount;
            ++c.subroutines;
        } else {
            c.otherLines += sec.lineCount;
        }
    }
}

// ------------------------------------------------------ les variables ----
struct Global {
    Index         index{kNoIndex};
    std::string   name;
    usage::Genre  genre{usage::Genre::Other};
    bool          located{false};
    bool          constant{false};
    bool          initialised{false};
};

NamedVariable named(const Global& g, const Project& p) {
    NamedVariable n;
    n.name = g.name;
    n.index = g.index;
    n.detail = g.index < p.variables.size() ? p.variables[g.index].address.raw : std::string{};
    return n;
}

void variableStats(const Project& p, const std::map<std::string, ReadInfo>* hmiReads, ProjectStats& s) {
    auto& vs = s.variables;
    auto& mem = s.memory;
    SizeModel sizes(p);

    // usage::genreOf ne depend que du type de base, du genre de TypeRef et de
    // l'adresse : il se calcule une fois par combinaison.
    std::unordered_map<std::string, usage::Genre> genres;
    const auto genreOf = [&](const Variable& v) {
        std::string key = lower(usage::baseTypeOf(p, v));
        key += v.type.klass == TypeClass::FunctionBlock ? "|b" : "|-";
        key += (v.located || v.address.valid()) ? 'l' : '-';
        if (const auto it = genres.find(key); it != genres.end()) return it->second;
        const auto g = usage::genreOf(p, v);
        genres.emplace(std::move(key), g);
        return g;
    };

    std::vector<Global> globals;
    std::unordered_map<std::string, std::size_t> globalByName;   // en minuscules -> globals
    std::map<std::string, DuplicateAddress> byAddress;           // en majuscules
    std::unordered_map<std::string, std::size_t> rowOf;          // le nom du type (SizeModel), en minuscules -> mem.types

    for (Index i = 0; i < p.variables.size(); ++i) {
        const auto& v = p.variables[i];
        const auto scope = scopeOf(v.scope);
        if (!scope) continue;                              // un champ de DDT : dans son type
        const auto si = scopeIndex(*scope);
        ++vs.count[si];
        const auto genre = genreOf(v);
        ++vs.byGenre[si][static_cast<std::size_t>(genre)];
        const bool commented = !trim(p.strings.text(v.comment)).empty();
        const bool located = v.located || v.address.valid();
        if (commented) ++vs.commented;
        if (located) ++vs.located;
        const std::string name = text(p, v.name);

        if (*scope == Scope::Global) {
            Global g;
            g.index = i;
            g.name = name;
            g.genre = genre;
            g.located = located;
            g.constant = v.scope == VariableScope::Constant;
            g.initialised = !trim(p.strings.text(v.initValue)).empty();
            globalByName.emplace(lower(name), globals.size());
            if (!commented) {
                ++vs.globalsWithoutComment;
                if (located) {
                    ++vs.locatedWithoutComment;
                    if (vs.firstLocatedWithoutComment.empty()) vs.firstLocatedWithoutComment = v.address.raw;
                }
                if (located && inputOutput(v.address.area)) vs.ioWithoutComment.push_back(named(g, p));
            }
            globals.push_back(std::move(g));
        }
        if (located && !trim(v.address.raw).empty()) {
            auto& d = byAddress[upper(trim(v.address.raw))];
            if (d.address.empty()) d.address = std::string(trim(v.address.raw));
            NamedVariable n;
            n.name = name;
            n.index = i;
            n.detail = d.address;
            d.variables.push_back(std::move(n));
        }

        // ---- la memoire : la ou elle est declaree, une fois (voir MemoryStats).
        if (v.scope == VariableScope::InOut) continue;
        if (*scope != Scope::Global && v.owner < p.pous.size() && p.pous[v.owner].kind == PouKind::FunctionBlockType) continue;
        // Une borne nommee (ARRAY[0..N]) : sa dimension compte pour un et la
        // declaration est dite incomplete. Les bornes de l'import n'aident pas :
        // il lit « 0..N » comme 0..0, un element sur, ce qui serait faux.
        const auto flat = SizeModel::flatten(p.strings.text(v.type.name));
        const auto info = sizes.of(flat.base);
        const auto bytes = mulSat(info.bytes, flat.count);
        mem.total = addSat(mem.total, bytes);
        mem.byScope[si] = addSat(mem.byScope[si], bytes);
        const bool resolved = info.resolved && flat.ok;
        // Une ligne par type, sous le nom que le modele lui donne : STRING[32] et
        // « string [32] » font la meme ligne.
        const auto key = lower(!info.name.empty() ? std::string_view(info.name) : trim(flat.base));
        auto row = rowOf.find(key);
        if (row == rowOf.end()) {
            TypeRow r;
            r.name = !info.name.empty() ? info.name : !trim(flat.base).empty() ? std::string(trim(flat.base)) : std::string("?");
            r.genre = info.genre;
            r.unitBytes = info.bytes;
            row = rowOf.emplace(key, mem.types.size()).first;
            mem.types.push_back(std::move(r));
        }
        auto& r = mem.types[row->second];
        ++r.declared;
        r.instances = addSat(r.instances, flat.count);
        r.totalBytes = addSat(r.totalBytes, bytes);
        r.resolved = r.resolved && resolved;
        r.holders.emplace_back(name, flat.count);
        if (!resolved) ++mem.unresolved;
    }
    // Les types inconnus eux-memes, meme au fond d'un DFB (Display_NTPC), pas
    // le DFB connu qui les contient.
    mem.unresolvedTypes = sizes.unknownTypes();

    // ---- les types : le plus lourd d'abord, leur part ---------------------------
    std::stable_sort(mem.types.begin(), mem.types.end(), [](const TypeRow& a, const TypeRow& b) {
        if (a.totalBytes != b.totalBytes) return a.totalBytes > b.totalBytes;
        if (a.declared != b.declared) return a.declared > b.declared;
        return lower(a.name) < lower(b.name);
    });
    for (auto& r : mem.types) {
        r.share = mem.total ? static_cast<double>(r.totalBytes) / static_cast<double>(mem.total) : 0.0;
        std::stable_sort(r.holders.begin(), r.holders.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        if (r.holders.size() > 4) r.holders.resize(4);
    }

    // ---- les adresses en double --------------------------------------------------
    for (auto& [key, d] : byAddress)
        if (d.variables.size() > 1) vs.duplicates.push_back(std::move(d));

    // ---- qui lit, qui ecrit : une passe sur tout le code --------------------------
    // Les entrees-sorties des blocs (DFB du projet, blocs de la bibliotheque) : ce
    // qu'on leur passe peut etre ecrit par le bloc.
    std::unordered_set<std::string> inouts;
    for (const auto& pou : p.pous) {
        if (pou.kind != PouKind::FunctionBlockType) continue;
        for (const auto vi : pou.parameters)
            if (vi < p.variables.size() && p.variables[vi].scope == VariableScope::InOut) inouts.insert(lower(p.strings.text(p.variables[vi].name)));
    }
    for (const auto& block : BlockLibrary::shared().all())
        for (const auto& pin : block.parameters)
            if (pin.direction == BlockParameter::Direction::InOut) inouts.insert(lower(pin.name));

    // Dans le code d'un POU, ses propres variables cachent les globales du meme
    // nom. Mais les parametres d'une unite de programme sont LIES a une globale
    // (EffectiveParameter="ConfigGazUtilisee", "manu_ouv[0]") : les nommer,
    // c'est nommer la globale. Une entree la lit a chaque cycle, une sortie
    // l'ecrit, une entree-sortie suit ce que le code en fait.
    std::vector<Use> uses(globals.size());
    std::unordered_map<Index, std::unordered_map<std::string, std::size_t>> own;   // POU -> nom -> globale (kNpos : a lui)
    std::vector<char> bound(globals.size(), 0);
    for (Index pi = 0; pi < p.pous.size(); ++pi) {
        const auto& pou = p.pous[pi];
        if (pou.sections.empty() || (pou.parameters.empty() && pou.locals.empty())) continue;
        auto& names = own[pi];
        for (const auto* list : {&pou.parameters, &pou.locals})
            for (const auto vi : *list) {
                if (vi >= p.variables.size()) continue;
                const auto& v = p.variables[vi];
                std::size_t target = kNpos;
                if (pou.kind == PouKind::ProgramUnit)
                    for (const auto& [attr, value] : v.attributes) {
                        if (attr != "EffectiveParameter") continue;
                        if (const auto g = globalByName.find(lower(rootOf(value))); g != globalByName.end()) target = g->second;
                    }
                names.emplace(lower(p.strings.text(v.name)), target);
                if (target == kNpos) continue;
                bound[target] = 1;
                if (v.scope == VariableScope::Input) uses[target].read = true;
                if (v.scope == VariableScope::Output) uses[target].written = true;
            }
    }
    Scanner scan(globalByName, inouts, uses);
    for (const auto& sec : p.sections) {
        const auto names = sec.owner < p.pous.size() ? own.find(sec.owner) : own.end();
        scan.setScope(names == own.end() ? nullptr : &names->second);
        if (sec.language == PouLanguage::ST) scan.statements(sec.body);
        else scan.anyDirection(sec.body);
        scan.readsOnly(p.strings.text(sec.activationCondition));
        scan.readsOnly(p.strings.text(sec.logicCondition));
        scan.setScope(nullptr);
    }

    // ---- les compteurs -----------------------------------------------------------
    // « Pas utilisee » : aucun code ne la lit ni ne l'ecrit (une condition
    // d'activation compte) et aucune unite ne la recoit en parametre - etre liee
    // a une unite, c'est servir. usage::Where (la pastille de l'onglet
    // Variables) ne voit ni les liaisons, ni les conditions, ni une locale qui
    // cache une globale du meme nom : sur le projet d'essai, les deux comptes
    // sont les memes.
    for (std::size_t k = 0; k < globals.size(); ++k) {
        const auto& g = globals[k];
        const auto& u = uses[k];
        const bool unused = !u.read && !u.written && !bound[k];
        const bool hmi = hmiReads && hmiReads->count(lower(g.name)) != 0;
        if (unused) vs.unused.push_back(named(g, p));
        if (hmi) vs.readByHmi.push_back(named(g, p));
        // Une instance de bloc s'ecrit en l'appelant : ni lue ni ecrite au sens
        // d'une donnee. Ce que rien ne nomme est deja « pas utilise ».
        const bool block = g.genre == usage::Genre::DfbInstance || g.genre == usage::Genre::StandardBlock;
        if (unused || block) continue;
        // Situee : elle est la pour qu'on la lise dehors (l'IHM, un superviseur).
        if (u.written && !u.read && !hmi && !g.located) vs.writtenNeverRead.push_back(named(g, p));
        // Une valeur initiale en fait une constante. Situee, quelqu'un l'ecrit
        // peut-etre dehors : le materiel (%I), le systeme (%S), l'IHM ou un
        // superviseur (%MW, par Modbus) ; liee a l'IHM, l'IHM peut l'ecrire.
        // Le code ne le dit pas : pas d'alerte (%MW2050, sur le projet d'essai).
        if (u.read && !u.written && !hmi && !g.located && !g.constant && !g.initialised) vs.readNeverWritten.push_back(named(g, p));
    }
}

// ------------------------------------------------------------ a regarder ----
std::string joinNames(const std::vector<NamedVariable>& v, std::size_t max) {
    std::string out;
    for (std::size_t i = 0; i < v.size() && i < max; ++i) out += (i ? ", " : "") + v[i].name;
    if (v.size() > max) out += "\xE2\x80\xA6";
    return out;
}

std::string plural(std::size_t n, std::string_view one, std::string_view many) {
    return thousands(n) + " " + std::string(n == 1 ? one : many);
}

std::vector<Finding> findingsOf(const ProjectStats& s) {
    std::vector<Finding> out;
    const auto& vs = s.variables;
    // 1. Deux variables a la meme adresse : l'une ecrase l'autre.
    if (!vs.duplicates.empty()) {
        Finding f;
        f.tone = Finding::Tone::Error;
        f.id = "doublons";
        f.title = plural(vs.duplicates.size(), "adresse d\xC3\xA9" "clar\xC3\xA9" "e deux fois", "adresses d\xC3\xA9" "clar\xC3\xA9" "es deux fois");
        for (std::size_t i = 0; i < vs.duplicates.size() && i < 3; ++i) {
            const auto& d = vs.duplicates[i];
            if (!f.detail.empty()) f.detail += " \xC2\xB7 ";
            f.detail += d.address + " : " + joinNames(d.variables, 3);
        }
        if (vs.duplicates.size() > 3) f.detail += " \xE2\x80\xA6";
        f.detail += " \xE2\x80\x94 l'une \xC3\xA9" "crase l'autre";
        f.button = "Variables";
        f.request = vs.duplicates.size() == 1 ? "variables:" + vs.duplicates.front().address : std::string("variables");
        out.push_back(std::move(f));
    }
    // 2. Les unites trop longues pour se relire.
    {
        std::vector<const EntryLines*> longUnits;
        for (const auto& e : s.code.entries)
            if (e.unit && e.lines > kLongUnitLines) longUnits.push_back(&e);
        std::stable_sort(longUnits.begin(), longUnits.end(), [](const EntryLines* a, const EntryLines* b) { return a->lines > b->lines; });
        if (!longUnits.empty()) {
            Finding f;
            f.tone = Finding::Tone::Warning;
            f.id = "unites-longues";
            f.title = plural(longUnits.size(), "unit\xC3\xA9", "unit\xC3\xA9s") + " de plus de " + thousands(kLongUnitLines) + " lignes";
            for (std::size_t i = 0; i < longUnits.size() && i < 4; ++i)
                f.detail += (i ? ", " : "") + longUnits[i]->name + " (" + thousands(longUnits[i]->lines) + ")";
            if (longUnits.size() > 4) f.detail += "\xE2\x80\xA6";
            f.detail += longUnits.size() > 1 ? " : les d\xC3\xA9" "couper les rendrait lisibles" : " : la d\xC3\xA9" "couper la rendrait lisible";
            f.button = "Voir";
            f.request = "unites";
            out.push_back(std::move(f));
        }
    }
    // 3. Les lectures avant l'ecriture, dans l'ordre de la tache.
    if (!s.late.empty()) {
        Finding f;
        f.tone = Finding::Tone::Warning;
        f.id = "lectures-avant";
        f.title = plural(s.late.size(), "lecture", "lectures") + " avant l'\xC3\xA9" "criture dans l'ordre de " + s.code.task;
        for (std::size_t i = 0; i < s.late.size() && i < 2; ++i) {
            const auto& l = s.late[i];
            if (!f.detail.empty()) f.detail += " \xC2\xB7 ";
            f.detail += l.reader + " lit " + l.variable + ", \xC3\xA9" "crite plus loin par " + l.writer;
        }
        if (s.late.size() > 2) f.detail += " \xE2\x80\xA6";
        f.button = "Ordre";
        // L'onglet Ordre d'execution, sur la premiere entree qui lit trop tot.
        f.request = "ordre:" + s.late.front().reader;
        out.push_back(std::move(f));
    }
    // 4. Les globales sans commentaire.
    if (vs.globalsWithoutComment > 0) {
        Finding f;
        f.tone = Finding::Tone::Warning;
        f.id = "sans-commentaire";
        f.title = vs.globalsWithoutComment == 1 ? std::string("1 variable globale sans commentaire")
                                                : thousands(vs.globalsWithoutComment) + " variables globales sans commentaire";
        if (vs.locatedWithoutComment > 0)
            f.detail = "dont " + thousands(vs.locatedWithoutComment) + (vs.locatedWithoutComment == 1 ? " situ\xC3\xA9" "e" : " situ\xC3\xA9" "es")
                     + (vs.firstLocatedWithoutComment.empty() ? std::string{} : " (" + vs.firstLocatedWithoutComment + "\xE2\x80\xA6)") + " : ";
        f.detail += "Coller depuis Excel les commente d'un coup";
        f.button = "Variables";
        f.request = "variables";
        out.push_back(std::move(f));
    }
    // 5. Les variables que le code ne nomme pas.
    if (!vs.unused.empty()) {
        Finding f;
        f.tone = Finding::Tone::Warning;
        f.id = "pas-utilisees";
        f.title = vs.unused.size() == 1 ? std::string("1 variable globale ne sert pas au programme")
                                        : thousands(vs.unused.size()) + " variables globales ne servent pas au programme";
        f.detail = joinNames(vs.unused, 4) + " : pas utilis\xC3\xA9" "e ne veut pas dire \xC3\xA0 supprimer, l'IHM peut la lire";
        f.button = "Voir";
        f.request = "variables-inutilisees";
        out.push_back(std::move(f));
    }
    // 6. Ce que la bibliotheque a de plus recent.
    if (!s.outdated.empty()) {
        Finding f;
        f.tone = Finding::Tone::Info;
        f.id = "bibliotheque";
        f.title = s.outdated.size() == 1 ? std::string("1 \xC3\xA9l\xC3\xA9ment plus r\xC3\xA9" "cent en biblioth\xC3\xA8que")
                                         : thousands(s.outdated.size()) + " \xC3\xA9l\xC3\xA9ments plus r\xC3\xA9" "cents en biblioth\xC3\xA8que";
        for (std::size_t i = 0; i < s.outdated.size() && i < 5; ++i) {
            const auto& n = s.outdated[i];
            if (!f.detail.empty()) f.detail += " \xC2\xB7 ";
            f.detail += n.name;
            if (i == 0) f.detail += " " + n.projectVersion + " \xE2\x86\x92 " + n.libraryVersion;
        }
        if (s.outdated.size() > 5) f.detail += " \xE2\x80\xA6";
        f.button = "Mettre \xC3\xA0 jour\xE2\x80\xA6";
        f.request = "bibliotheque";
        out.push_back(std::move(f));
    }
    if (out.empty()) {
        Finding f;
        f.tone = Finding::Tone::Ok;
        f.id = "rien";
        f.title = "Rien \xC3\xA0 regarder";
        f.detail = "Pas d'adresse en double, pas de lecture avant l'\xC3\xA9" "criture, les globales sont comment\xC3\xA9" "es.";
        out.push_back(std::move(f));
    }
    return out;
}

// ------------------------------------------------------------------ le CSV ----
std::string csvCell(std::string_view v) {
    if (v.find_first_of(";\"\n\r") == std::string_view::npos) return std::string(v);
    std::string out = "\"";
    for (const char c : v) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

std::string csvShare(double fraction) { return decimal(fraction * 100.0, 1); }

} // namespace

// ================================================================ les langages ==
Language languageOf(PouLanguage l) noexcept {
    switch (l) {
        case PouLanguage::ST:  return Language::ST;
        case PouLanguage::SFC: return Language::SFC;
        case PouLanguage::LD:  return Language::LD;
        case PouLanguage::FBD: return Language::FBD;
        case PouLanguage::IL:  return Language::IL;
        case PouLanguage::Unknown: break;
    }
    return Language::Other;
}

std::string_view languageName(Language l) noexcept {
    switch (l) {
        case Language::ST:  return "ST";
        case Language::SFC: return "SFC";
        case Language::LD:  return "LD";
        case Language::FBD: return "FBD";
        case Language::IL:  return "IL";
        case Language::Other: break;
    }
    return "autre";
}

std::string_view languageLabel(Language l) noexcept {
    switch (l) {
        case Language::ST:  return "ST (texte structur\xC3\xA9)";
        case Language::SFC: return "SFC (grafcets)";
        case Language::LD:  return "LD (contacts)";
        case Language::FBD: return "FBD (blocs fonctions)";
        case Language::IL:  return "IL (liste d'instructions)";
        case Language::Other: break;
    }
    return "autre langage";
}

std::string_view typeGenreLabel(TypeGenre g) noexcept {
    switch (g) {
        case TypeGenre::Elementary: return "\xC3\xA9l\xC3\xA9mentaire";
        case TypeGenre::Derived:    return "type d\xC3\xA9riv\xC3\xA9 (DDT)";
        case TypeGenre::Dfb:        return "bloc DFB";
        case TypeGenre::Standard:   return "bloc standard";
        case TypeGenre::Unknown:    break;
    }
    return "inconnu";
}

// ============================================================ le modele de taille ==
std::uint64_t elementaryBytes(std::string_view type) noexcept {
    const auto t = trim(type);
    if (startsNoCase(t, "STRING")) {
        auto rest = trim(t.substr(6));
        if (rest.empty()) return 17;                        // 16 caracteres et le zero final
        if (rest.front() != '[' || rest.back() != ']') return 0;
        rest = trim(rest.substr(1, rest.size() - 2));
        if (rest.empty() || rest.size() > 9) return 0;
        std::uint64_t n = 0;
        for (const char c : rest) {
            if (c < '0' || c > '9') return 0;              // une longueur nommee : pas lue
            n = n * 10 + static_cast<std::uint64_t>(c - '0');
        }
        return n + 1;
    }
    for (const auto& e : kElementary)
        if (equalNoCase(t, e.name)) return e.bytes;
    return 0;
}

SizeModel::SizeModel(const Project& p) : p_(p) {
    for (Index i = 0; i < p.derivedTypes.size(); ++i) ddt_.emplace(lower(p.strings.text(p.derivedTypes[i].name)), i);
    for (Index i = 0; i < p.pous.size(); ++i)
        if (p.pous[i].kind == PouKind::FunctionBlockType) dfb_.emplace(lower(p.strings.text(p.pous[i].name)), i);
}

SizeModel::Flat SizeModel::flatten(std::string_view type) {
    Flat f;
    std::string current(trim(type));
    // Des tableaux de tableaux : l'element d'un tableau peut en etre un.
    for (int depth = 0; depth < 8; ++depth) {
        const auto shape = members::parseArray(current);
        if (!shape.valid()) {
            // "ARRAY[0..N] OF INT" : un tableau quand meme, de taille inconnue.
            if (startsNoCase(current, "ARRAY")) {
                const auto of = upper(current).find(" OF ");
                if (of != std::string::npos) {
                    f.ok = false;
                    current = std::string(trim(std::string_view(current).substr(of + 4)));
                    continue;
                }
            }
            break;
        }
        const auto n = shape.count();
        f.count = mulSat(f.count, n > 0 ? static_cast<std::uint64_t>(n) : 0u);
        current = shape.element;
    }
    f.base = std::move(current);
    return f;
}

SizeModel::Info SizeModel::of(std::string_view type) {
    const auto t = trim(type);
    auto key = lower(t);
    if (const auto it = cache_.find(key); it != cache_.end()) return it->second;
    // Un type qui se contiendrait lui-meme (Control Expert l'interdit, un fichier
    // n'est pas oblige d'etre valide) : 0 ici, au lieu de boucler - et rien de
    // retenu, le calcul du dehors pose la vraie valeur.
    if (std::find(busy_.begin(), busy_.end(), key) != busy_.end()) {
        Info loop;
        loop.name = std::string(t);
        return loop;
    }
    auto info = compute(key, t);
    cache_.insert_or_assign(std::move(key), info);
    return info;
}

SizeModel::Info SizeModel::compute(const std::string& key, std::string_view type) {
    Info info;
    info.name = std::string(type);
    if (type.empty()) return info;
    if (startsNoCase(type, "ARRAY")) {
        const auto flat = flatten(type);
        const auto element = of(flat.base);
        info.bytes = mulSat(element.bytes, flat.count);
        info.genre = element.genre;
        info.resolved = element.resolved && flat.ok;
        return info;
    }
    if (const auto bytes = elementaryBytes(type); bytes != 0) {
        info.bytes = bytes;
        info.genre = TypeGenre::Elementary;
        info.resolved = true;
        info.name.clear();
        for (const char c : type) if (!blank(c)) info.name += asciiUpper(c);   // "string [ 100 ]" -> "STRING[100]"
        return info;
    }
    busy_.push_back(key);
    if (const auto d = ddt_.find(key); d != ddt_.end()) {
        const auto& dt = p_.derivedTypes[d->second];
        info.name = std::string(p_.strings.text(dt.name));
        info.genre = TypeGenre::Derived;
        info.resolved = true;
        for (const auto f : dt.fields) {
            if (f >= p_.variables.size()) continue;
            const auto part = of(p_.strings.text(p_.variables[f].type.name));
            info.bytes = addSat(info.bytes, part.bytes);
            info.resolved = info.resolved && part.resolved;
        }
    } else if (const auto b = dfb_.find(key); b != dfb_.end()) {
        const auto& pou = p_.pous[b->second];
        info.name = std::string(p_.strings.text(pou.name));
        info.genre = pou.userDefined ? TypeGenre::Dfb : TypeGenre::Standard;
        info.resolved = true;
        for (const auto* list : {&pou.parameters, &pou.locals})
            for (const auto vi : *list) {
                if (vi >= p_.variables.size()) continue;
                const auto& v = p_.variables[vi];
                if (v.scope == VariableScope::InOut) continue;   // elle designe la variable qu'on lui passe
                const auto part = of(p_.strings.text(v.type.name));
                info.bytes = addSat(info.bytes, part.bytes);
                info.resolved = info.resolved && part.resolved;
            }
    } else if (const auto* block = BlockLibrary::shared().find(type); block && block->kind == BlockKind::FunctionBlock) {
        // Un bloc fonction (EFB) seulement : une fonction ne s'instancie pas.
        info.name = block->name;
        info.genre = TypeGenre::Standard;
        info.resolved = true;
        // Une broche generique (ANY_NUM...) n'a pas de taille : elle compte 0, le
        // bloc reste connu - et ce n'est pas un « type inconnu » du projet.
        const auto known = unknown_.size();
        for (const auto& pin : block->parameters) {
            if (pin.direction == BlockParameter::Direction::InOut) continue;
            info.bytes = addSat(info.bytes, of(pin.type).bytes);
        }
        unknown_.erase(unknown_.begin() + static_cast<std::ptrdiff_t>(known), unknown_.end());
    } else if (std::find(unknown_.begin(), unknown_.end(), info.name) == unknown_.end()) {
        unknown_.push_back(info.name);
    }
    busy_.pop_back();
    return info;
}

// ===================================================================== le tout ==
ProjectStats compute(const Project& p, const SharedLibrary* library, const std::map<std::string, ReadInfo>* hmiReads) {
    const auto t0 = std::chrono::steady_clock::now();
    ProjectStats s;
    // Verifie avant de lire le moindre element, comme l'analyseur : une
    // construction incoherente doit donner un ecran vide, pas une lecture hors
    // des tableaux.
    if (!p.layoutMatches()) return s;
    s.valid = true;
    s.project = p.header.projectName;
    const auto task = mainTask(p);
    codeStats(p, task, s.code);
    variableStats(p, hmiReads, s);
    s.late = api::lateReads(p, task);
    if (library) s.outdated = library->outdated(p);
    s.findings = findingsOf(s);
    s.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return s;
}

std::string toCsv(const ProjectStats& s) {
    const auto& c = s.code;
    const auto& vs = s.variables;
    const auto& m = s.memory;
    const auto n = [](std::uint64_t v) { return std::to_string(v); };
    std::string out;
    out += "Statistiques;" + csvCell(s.project) + "\n\n";

    out += "Indicateur;Valeur\n";
    out += "Lignes de code (" + csvCell(c.task) + ");" + n(c.lines) + "\n";
    out += "Entr\xC3\xA9" "es de " + csvCell(c.task) + ";" + n(c.entries.size()) + "\n";
    out += "Sections de la t\xC3\xA2" "che;" + n(c.taskSections) + "\n";
    out += "Unit\xC3\xA9s de programme;" + n(c.units) + "\n";
    out += "Sections des unit\xC3\xA9s;" + n(c.unitSections) + "\n";
    out += "Lignes des blocs DFB;" + n(c.dfbLines) + "\n";
    out += "Lignes des sous-routines;" + n(c.subroutineLines) + "\n";
    out += "Autres lignes;" + n(c.otherLines) + "\n";
    out += "Variables;" + n(vs.total()) + "\n";
    out += "Globales;" + n(vs.count[0]) + "\n";
    out += "Locales;" + n(vs.count[1]) + "\n";
    out += "Param\xC3\xA8tres;" + n(vs.count[2]) + "\n";
    out += "Situ\xC3\xA9" "es;" + n(vs.located) + "\n";
    out += "Comment\xC3\xA9" "es;" + n(vs.commented) + "\n";
    out += "Part comment\xC3\xA9" "e (%);" + csvShare(vs.commentedShare()) + "\n";
    out += "Globales sans commentaire;" + n(vs.globalsWithoutComment) + "\n";
    out += "Pas utilis\xC3\xA9" "es;" + n(vs.unused.size()) + "\n";
    out += "Lues par l'IHM;" + n(vs.readByHmi.size()) + "\n";
    out += "\xC3\x89" "crites, jamais lues;" + n(vs.writtenNeverRead.size()) + "\n";
    out += "Jamais \xC3\xA9" "crites;" + n(vs.readNeverWritten.size()) + "\n";
    out += "Adresses en double;" + n(vs.duplicates.size()) + "\n";
    out += "E/S sans commentaire;" + n(vs.ioWithoutComment.size()) + "\n";
    out += "M\xC3\xA9moire d\xC3\xA9" "clar\xC3\xA9" "e (octets);" + n(m.total) + "\n";
    out += "dont globales (octets);" + n(m.byScope[0]) + "\n";
    out += "dont locales (octets);" + n(m.byScope[1]) + "\n";
    out += "dont param\xC3\xA8tres (octets);" + n(m.byScope[2]) + "\n";
    out += "D\xC3\xA9" "clarations d'un type inconnu;" + n(m.unresolved) + "\n";
    out += "Lectures avant l'\xC3\xA9" "criture;" + n(s.late.size()) + "\n";
    out += "Plus r\xC3\xA9" "cents en biblioth\xC3\xA8que;" + n(s.outdated.size()) + "\n";

    out += "\nRang;Entr\xC3\xA9" "e de " + csvCell(c.task) + ";Genre;Sections;Lignes";
    for (std::size_t l = 0; l < kLanguageCount; ++l) out += ";" + std::string(languageName(static_cast<Language>(l)));
    out += "\n";
    for (const auto& e : c.entries) {
        out += n(e.rank) + ";" + csvCell(e.name) + ";" + (e.unit ? "unit\xC3\xA9" : "section") + ";" + n(e.sections) + ";" + n(e.lines);
        for (const auto v : e.byLanguage) out += ";" + n(v);
        out += "\n";
    }

    out += "\nSection;Unit\xC3\xA9;Langage;Lignes\n";
    for (const auto& sec : c.sections)
        out += csvCell(sec.name) + ";" + csvCell(sec.unit) + ";" + std::string(languageName(sec.language)) + ";" + n(sec.lines) + "\n";

    out += "\nType;Genre;D\xC3\xA9" "clar\xC3\xA9" "es;Instances;Taille (octets);Total (octets);Part (%)\n";
    for (const auto& r : m.types)
        out += csvCell(r.name) + ";" + std::string(typeGenreLabel(r.genre)) + (r.resolved ? "" : " (taille inconnue)") + ";" + n(r.declared) + ";"
             + n(r.instances) + ";" + n(r.unitBytes) + ";" + n(r.totalBytes) + ";" + csvShare(r.share) + "\n";

    out += "\n\xC3\x80 regarder;D\xC3\xA9tail\n";
    for (const auto& f : s.findings) out += csvCell(f.title) + ";" + csvCell(f.detail) + "\n";
    return out;
}

// ================================================================ les nombres ==
std::string thousands(std::uint64_t n) {
    const auto d = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += d[i];
    }
    return out;
}

std::string decimal(double value, int decimals) {
    if (!std::isfinite(value)) return "\xE2\x80\x94";
    decimals = std::clamp(decimals, 0, 6);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, value);
    std::string raw(buf);
    // La locale du C peut deja ecrire une virgule : les deux se lisent.
    std::size_t sep = raw.find_first_of(".,");
    std::string intPart = raw.substr(0, sep == std::string::npos ? raw.size() : sep);
    const std::string frac = sep == std::string::npos ? std::string{} : raw.substr(sep + 1);
    std::string sign;
    if (!intPart.empty() && intPart.front() == '-') {
        sign = "-";
        intPart.erase(0, 1);
    }
    std::string grouped;
    for (std::size_t i = 0; i < intPart.size(); ++i) {
        if (i && (intPart.size() - i) % 3 == 0) grouped += "\xE2\x80\xAF";
        grouped += intPart[i];
    }
    return sign + grouped + (frac.empty() ? std::string{} : "," + frac);
}

std::string bytesText(std::uint64_t bytes) {
    if (bytes < 1024) return thousands(bytes) + " o";
    const double ko = static_cast<double>(bytes) / 1024.0;
    if (ko < 1024.0) return decimal(ko, ko < 100.0 ? 1 : 0) + " Ko";
    const double mo = ko / 1024.0;
    return decimal(mo, mo < 100.0 ? 1 : 0) + " Mo";
}

std::string percentText(double fraction) {
    if (!(fraction > 0.0)) return "0 %";
    if (fraction < 0.005) return "< 1 %";
    return std::to_string(static_cast<long long>(std::lround(fraction * 100.0))) + " %";
}

} // namespace project::stats
