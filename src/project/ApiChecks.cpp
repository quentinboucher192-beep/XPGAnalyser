// =============================================================================
//  project/ApiChecks.cpp - lot API 2 : le tableau de bord de l'API
// =============================================================================
#include "ApiChecks.hpp"

#include "../domain/ExecutionOrder.hpp"
#include "../import/ProjectAnalyzer.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace project::api {

namespace {

std::string lowerOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Le code sans ses commentaires (* ... *) ni ses chaines '...' : remplaces par
// des blancs, pour garder les positions (et les fins de ligne).
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

const std::unordered_set<std::string>& keywords() {
    static const std::unordered_set<std::string> k{
        "if", "then", "else", "elsif", "end_if", "case", "of", "end_case", "for", "to", "by", "do",
        "end_for", "while", "end_while", "repeat", "until", "end_repeat", "return", "exit", "and",
        "or", "xor", "not", "mod", "true", "false"};
    return k;
}

// Les noms (en minuscules) qu'un code ecrit et lit. `globals` : les noms des
// variables globales, en minuscules - les autres sont ignores.
void scan(std::string_view code, const std::unordered_map<std::string, std::string>& globals,
          std::set<std::string>& writes, std::set<std::string>& reads) {
    const auto text = stripped(code);
    // Instruction par instruction : ';' les separe (les mots-cles THEN, DO,
    // ELSE ouvrent une nouvelle instruction eux aussi).
    std::size_t start = 0;
    const auto handle = [&](std::size_t from, std::size_t to) {
        std::string_view st(text.data() + from, to - from);
        // L'ecriture : un nom (et ses membres, ses indices) avant ':=' en tete.
        std::size_t p = 0;
        while (p < st.size() && std::isspace(static_cast<unsigned char>(st[p]))) ++p;
        std::string target;
        const auto assign = st.find(":=");
        if (assign != std::string_view::npos && p < st.size() && isIdentStart(st[p])) {
            auto q = p;
            while (q < st.size() && isIdentChar(st[q])) ++q;
            const auto word = lowerOf(st.substr(p, q - p));
            // Rien d'autre qu'un chemin (membres, indices) entre le nom et ':='.
            bool path = true;
            int depth = 0;
            for (auto k = q; k < assign; ++k) {
                const char c = st[k];
                if (c == '[') ++depth;
                else if (c == ']') --depth;
                else if (depth == 0 && !(c == '.' || isIdentChar(c) || std::isspace(static_cast<unsigned char>(c)))) { path = false; break; }
            }
            if (path && !keywords().count(word)) target = word;
        }
        // Les lectures : tous les noms, sauf la cible et les membres apres un point.
        std::size_t k = 0;
        bool targetSeen = false;
        while (k < st.size()) {
            if (isIdentStart(st[k]) && (k == 0 || (!isIdentChar(st[k - 1]) && st[k - 1] != '.' && st[k - 1] != '%'))) {
                auto e = k;
                while (e < st.size() && isIdentChar(st[e])) ++e;
                const auto word = lowerOf(st.substr(k, e - k));
                if (!targetSeen && !target.empty() && word == target && k <= assign) {
                    targetSeen = true;
                    if (globals.count(word)) writes.insert(word);
                } else if (globals.count(word) && !keywords().count(word)) {
                    reads.insert(word);
                }
                k = e;
                continue;
            }
            ++k;
        }
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == ';') { handle(start, i); start = i + 1; continue; }
        // THEN / DO / ELSE / REPEAT : la suite est une autre instruction.
        if (isIdentStart(text[i]) && (i == 0 || !isIdentChar(text[i - 1]))) {
            auto e = i;
            while (e < text.size() && isIdentChar(text[e])) ++e;
            const auto word = lowerOf(std::string_view(text).substr(i, e - i));
            if (word == "then" || word == "do" || word == "else" || word == "repeat") {
                handle(start, e);
                start = e;
            }
            i = e - 1;
        }
    }
    if (start < text.size()) handle(start, text.size());
}

std::unordered_map<std::string, std::string> globalNames(const domain::Project& p) {
    std::unordered_map<std::string, std::string> out;
    for (const auto& v : p.variables)
        if (v.scope == domain::VariableScope::Global) {
            const std::string name(p.strings.text(v.name));
            out.emplace(lowerOf(name), name);
        }
    return out;
}

} // namespace

std::vector<Entry> entriesOf(const domain::Project& p, std::string_view task) {
    std::vector<Entry> out;
    std::map<domain::Index, std::size_t> unitAt;
    for (const auto& step : domain::executionOrder(p, task)) {
        if (step.section >= p.sections.size()) continue;
        const auto& sec = p.sections[step.section];
        if (step.fromProgramUnit && step.unit < p.pous.size()) {
            const auto it = unitAt.find(step.unit);
            if (it == unitAt.end()) {
                Entry e;
                e.name = std::string(p.strings.text(p.pous[step.unit].name));
                e.unit = true;
                e.unitIndex = step.unit;
                unitAt[step.unit] = out.size();
                out.push_back(std::move(e));
            }
            auto& e = out[unitAt[step.unit]];
            ++e.sections;
            e.lines += sec.lineCount;
            continue;
        }
        Entry e;
        e.name = std::string(p.strings.text(sec.name));
        e.sections = 1;
        e.lines = sec.lineCount;
        e.section = step.section;
        out.push_back(std::move(e));
    }
    return out;
}

namespace {
void accessSets(const domain::Project& p, const Entry& e, const std::unordered_map<std::string, std::string>& globals,
                std::set<std::string>& writes, std::set<std::string>& reads) {
    if (!e.unit) {
        if (e.section < p.sections.size()) scan(p.sections[e.section].body, globals, writes, reads);
    } else if (e.unitIndex < p.pous.size()) {
        for (const auto s : p.pous[e.unitIndex].sections)
            if (s < p.sections.size()) scan(p.sections[s].body, globals, writes, reads);
    }
    // Ce qu'une entree ecrit, elle ne le « lit pas d'avant » : le retirer des lectures.
    for (const auto& w : writes) reads.erase(w);
}
} // namespace

Access accessOf(const domain::Project& p, const Entry& e) {
    const auto globals = globalNames(p);
    std::set<std::string> w, r;
    accessSets(p, e, globals, w, r);
    Access a;
    for (const auto& x : w) a.writes.push_back(globals.at(x));
    for (const auto& x : r) a.reads.push_back(globals.at(x));
    const auto byName = [](const std::string& a1, const std::string& b1) { return lowerOf(a1) < lowerOf(b1); };
    std::sort(a.writes.begin(), a.writes.end(), byName);
    std::sort(a.reads.begin(), a.reads.end(), byName);
    return a;
}

Access accessOfSection(const domain::Project& p, domain::Index section) {
    Access a;
    if (section >= p.sections.size()) return a;
    auto names = globalNames(p);
    // Les parametres de l'unite : leur nom local vers la variable recue (sa racine).
    const auto owner = p.sections[section].owner;
    if (owner < p.pous.size() && p.pous[owner].kind == domain::PouKind::ProgramUnit)
        for (const auto v : p.pous[owner].parameters) {
            if (v >= p.variables.size()) continue;
            const auto& var = p.variables[v];
            std::string effective;
            for (const auto& [key, value] : var.attributes)
                if (key == "EffectiveParameter") effective = value;
            if (effective.empty()) continue;
            std::size_t end = 0;
            while (end < effective.size() && isIdentChar(effective[end])) ++end;
            const auto root = effective.substr(0, end);
            const auto it = names.find(lowerOf(root));
            names[lowerOf(std::string(p.strings.text(var.name)))] = it != names.end() ? it->second : root;
        }
    std::set<std::string> w, r;
    scan(p.sections[section].body, names, w, r);
    for (const auto& x : w) r.erase(x);
    std::set<std::string> seenW, seenR;
    for (const auto& x : w)
        if (seenW.insert(lowerOf(names.at(x))).second) a.writes.push_back(names.at(x));
    for (const auto& x : r)
        if (seenR.insert(lowerOf(names.at(x))).second && !seenW.count(lowerOf(names.at(x)))) a.reads.push_back(names.at(x));
    const auto byName = [](const std::string& a1, const std::string& b1) { return lowerOf(a1) < lowerOf(b1); };
    std::sort(a.writes.begin(), a.writes.end(), byName);
    std::sort(a.reads.begin(), a.reads.end(), byName);
    return a;
}

std::vector<LateRead> lateReads(const domain::Project& p, std::string_view task) {
    const auto globals = globalNames(p);
    const auto entries = entriesOf(p, task);
    std::vector<std::set<std::string>> writes(entries.size()), reads(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) accessSets(p, entries[i], globals, writes[i], reads[i]);
    std::unordered_map<std::string, std::size_t> firstWriter;
    for (std::size_t i = 0; i < entries.size(); ++i)
        for (const auto& w : writes[i]) firstWriter.emplace(w, i);
    std::vector<LateRead> out;
    for (std::size_t i = 0; i < entries.size(); ++i)
        for (const auto& r : reads[i]) {
            const auto it = firstWriter.find(r);
            if (it == firstWriter.end() || it->second <= i) continue;
            LateRead l;
            l.reader = entries[i].name;
            l.readerRank = i + 1;
            l.variable = globals.at(r);
            l.writer = entries[it->second].name;
            l.writerRank = it->second + 1;
            out.push_back(std::move(l));
        }
    return out;
}

Summary summarize(const domain::Project& p, const SharedLibrary* library) {
    Summary s;
    s.cpu = p.hardware.cpuReference;
    s.family = p.hardware.family;
    s.firmware = p.hardware.cpuFirmware;
    s.resource = p.hardware.resourceName;
    s.product = p.header.product;
    s.hasHardware = !p.hardware.inferred && !p.hardware.racks.empty();
    s.racks = p.hardware.racks.size();
    s.modules = p.hardware.totalModules();
    s.tasks = p.tasks.size();

    if (!p.tasks.empty()) {
        s.mainTask = std::string(p.strings.text(p.tasks.front().name));
        s.order = entriesOf(p, s.mainTask);
        for (const auto& e : s.order) {
            if (e.unit) { ++s.units; s.unitSections += e.sections; }
            else ++s.taskSections;
            s.lines += e.lines;
        }
        s.late = lateReads(p, s.mainTask);
    }
    for (const auto& pou : p.pous) {
        if (pou.kind == domain::PouKind::SubRoutine) ++s.subroutines;
        if (pou.kind == domain::PouKind::FunctionBlockType) {
            ++s.dfb;
            if (library && library->find(p.strings.text(pou.name))) ++s.dfbFromLibrary;
        }
    }
    s.ddt = p.derivedTypes.size();
    if (library) {
        for (const auto& d : p.derivedTypes)
            if (library->find(p.strings.text(d.name))) ++s.ddtFromLibrary;
        s.outdated = library->outdated(p);
    }
    // Les usages : le meme index que la colonne « Utilisations » de l'onglet
    // Variables (referenceCount n'est rempli par personne) - le filtre que le
    // tableau de bord ouvre montre donc exactement les memes variables.
    const auto refs = importer::ProjectAnalyzer::buildReferenceIndex(p);
    for (const auto& v : p.variables) {
        if (v.scope != domain::VariableScope::Global) continue;
        ++s.variables;
        if (v.located) ++s.located;
        if (v.type.klass == domain::TypeClass::FunctionBlock || v.type.fbTypeIndex != domain::kNoIndex) ++s.blockInstances;
        const auto it = refs.find(v.name);
        if (it == refs.end() || it->second == 0) s.unused.emplace_back(p.strings.text(v.name));
    }
    s.animationTables = p.animationTables.size();
    for (const auto& t : p.animationTables) s.animationEntries += t.entries.size();
    return s;
}

} // namespace project::api
