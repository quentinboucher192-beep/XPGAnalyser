// =============================================================================
//  export/ProgramBook.cpp - 1.8.0 : voir ProgramBook.hpp (le contenu, le texte)
// =============================================================================
#include "ProgramBook.hpp"

#include "../core/CodeIcons.hpp"
#include "../domain/ExecutionOrder.hpp"
#include "../project/ApiChecks.hpp"
#include "../project/CodeIconKeys.hpp"
#include "DocKit.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <unordered_map>

namespace exporter::book {

namespace {

std::string lower(std::string_view s) {
    std::string o(s);
    for (auto& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Les tabulations aux taquets de 4 : le code garde son alignement.
std::string expandTabs(std::string_view line) {
    std::string out;
    out.reserve(line.size() + 8);
    std::size_t col = 0;
    for (const char c : line) {
        if (c == '\t') {
            const std::size_t n = 4 - (col % 4);
            out.append(n, ' ');
            col += n;
        } else if (c != '\r') {
            out += c;
            // Une suite UTF-8 ne compte qu'une colonne.
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++col;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::vector<std::string> linesOf(std::string_view body) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= body.size()) {
        const auto nl = body.find('\n', from);
        out.push_back(expandTabs(body.substr(from, (nl == std::string_view::npos ? body.size() : nl) - from)));
        if (nl == std::string_view::npos) break;
        from = nl + 1;
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

// Le premier commentaire qui dit quelque chose (pas une ligne de ====).
std::string firstComment(std::string_view code) {
    std::size_t at = 0;
    while ((at = code.find("(*", at)) != std::string_view::npos) {
        const auto end = code.find("*)", at + 2);
        const auto inner = code.substr(at + 2, (end == std::string_view::npos ? code.size() : end) - at - 2);
        at = end == std::string_view::npos ? code.size() : end + 2;
        std::string t;
        for (const char c : inner) t += (c == '\r' || c == '\n' || c == '\t') ? ' ' : c;
        // Les suites de 3 decorations ou plus disparaissent.
        std::string clean;
        for (std::size_t i = 0; i < t.size();) {
            const char c = t[i];
            if (c == '=' || c == '_' || c == '-' || c == '*' || c == '#' || c == '~') {
                std::size_t j = i;
                while (j < t.size() && t[j] == c) ++j;
                if (j - i >= 3) { clean += ' '; i = j; continue; }
            }
            clean += c;
            ++i;
        }
        std::string collapsed;
        for (const char c : clean) {
            if (c == ' ' && (collapsed.empty() || collapsed.back() == ' ')) continue;
            collapsed += c;
        }
        while (!collapsed.empty() && (collapsed.back() == ' ' || collapsed.back() == ':' || collapsed.back() == '.' || collapsed.back() == '-')) collapsed.pop_back();
        while (!collapsed.empty() && (collapsed.front() == ' ' || collapsed.front() == ':' || collapsed.front() == '-')) collapsed.erase(collapsed.begin());
        bool letter = false;
        for (const char c : collapsed) letter = letter || std::isalpha(static_cast<unsigned char>(c));
        if (collapsed.size() >= 4 && letter) {
            if (collapsed.size() > 120) {
                collapsed.resize(117);
                collapsed += "...";
            }
            return collapsed;
        }
    }
    return {};
}

// Le code sans commentaires ni chaines (les appels se cherchent la).
std::string stripped(std::string_view code) {
    std::string out;
    out.reserve(code.size());
    for (std::size_t i = 0; i < code.size();) {
        if (code.compare(i, 2, "(*") == 0) {
            const auto e = code.find("*)", i + 2);
            i = e == std::string_view::npos ? code.size() : e + 2;
            out += ' ';
            continue;
        }
        if (code[i] == '\'') {
            const auto e = code.find('\'', i + 1);
            i = e == std::string_view::npos ? code.size() : e + 1;
            out += ' ';
            continue;
        }
        out += code[i++];
    }
    return out;
}

std::string text(const domain::Project& p, domain::SymbolId id) { return std::string(p.strings.text(id)); }

std::string attribute(const domain::Variable& v, std::string_view key) {
    for (const auto& [k, value] : v.attributes)
        if (k == key) return value;
    return {};
}

std::string direction(domain::VariableScope s) {
    switch (s) {
        case domain::VariableScope::Input:  return "Entr\xC3\xA9" "e";
        case domain::VariableScope::Output: return "Sortie";
        case domain::VariableScope::InOut:  return "E/S";
        default:                            return "Locale";
    }
}

Param paramOf(const domain::Project& p, domain::Index v) {
    Param r;
    if (v >= p.variables.size()) return r;
    const auto& var = p.variables[v];
    r.direction = direction(var.scope);
    r.name = text(p, var.name);
    r.type = text(p, var.type.name);
    r.receives = attribute(var, "EffectiveParameter");
    r.comment = text(p, var.comment);
    return r;
}

// Les blocs qu'une section appelle : un nom de variable de type DFB (ou bloc
// standard) suivi d'une parenthese : "Rtc(Enable := TRUE)".
std::vector<std::string> callsOf(const domain::Project& p, const domain::Section& sec,
                                 const std::unordered_map<std::string, domain::Index>& globals) {
    std::unordered_map<std::string, domain::Index> locals;
    if (sec.owner < p.pous.size()) {
        const auto& pou = p.pous[sec.owner];
        for (const auto list : {&pou.locals, &pou.parameters})
            for (const auto v : *list)
                if (v < p.variables.size()) locals[lower(p.strings.text(p.variables[v].name))] = v;
    }
    const auto code = stripped(sec.body);
    std::set<std::string> seen;
    std::vector<std::string> out;
    for (std::size_t i = 0; i < code.size();) {
        if (!(std::isalpha(static_cast<unsigned char>(code[i])) || code[i] == '_') || (i > 0 && (identChar(code[i - 1]) || code[i - 1] == '.' || code[i - 1] == '%'))) {
            ++i;
            continue;
        }
        std::size_t e = i;
        while (e < code.size() && identChar(code[e])) ++e;
        std::size_t k = e;
        while (k < code.size() && (code[k] == ' ' || code[k] == '\t')) ++k;
        if (k < code.size() && code[k] == '(') {
            const auto key = lower(std::string_view(code).substr(i, e - i));
            domain::Index v = domain::kNoIndex;
            if (const auto it = locals.find(key); it != locals.end()) v = it->second;
            else if (const auto g = globals.find(key); g != globals.end()) v = g->second;
            if (v != domain::kNoIndex) {
                const auto& var = p.variables[v];
                if (var.type.klass == domain::TypeClass::FunctionBlock && seen.insert(key).second)
                    out.push_back(text(p, var.name) + " (" + text(p, var.type.name) + ")");
            }
        }
        i = e;
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
    return out;
}

std::string taskKind(const domain::Task& t) {
    if (t.type == "cyclic") return "cyclique";
    if (t.type == "periodic") return "p\xC3\xA9riodique";
    return t.type;
}

// Les taches dans l'ordre de Control Expert : MAST, FAST, AUX0..3, puis les autres.
int taskRank(std::string_view name) {
    const auto n = lower(name);
    if (n == "mast") return 0;
    if (n == "fast") return 1;
    if (n.rfind("aux", 0) == 0) return 2;
    return 3;
}

std::string sanitize(std::string_view s) {
    std::string out;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '_') out += c;
        else if (u >= 0x80) continue;        // un accent : sa lettre suffit a peu pres
        else out += '_';
    }
    while (out.find("__") != std::string::npos) out.replace(out.find("__"), 2, "_");
    if (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? std::string("programme") : out;
}

} // namespace

std::string roleName(int icon) { return icon >= 0 ? std::string(core::codeicons::info(static_cast<std::size_t>(icon)).name) : std::string{}; }

Book build(const domain::Project& p, const Options& o) {
    namespace ci = project::codeicons;
    Book b;
    b.project = !o.projectName.empty() ? o.projectName : !p.header.projectName.empty() ? p.header.projectName : std::string("Projet");
    b.version = p.header.projectVersion;
    b.cpu = p.hardware.cpuReference + (p.hardware.cpuFirmware.empty() ? std::string{} : " " + p.hardware.cpuFirmware);
    b.product = p.header.product;
    b.source = p.header.sourceFile.empty() ? std::string{} : std::filesystem::path(p.header.sourceFile).filename().string();
    b.exportedAt = o.exportedAt;
    b.app = o.appName + (o.appVersion.empty() ? std::string{} : " " + o.appVersion);
    for (const auto& pou : p.pous) b.programUnits += pou.kind == domain::PouKind::ProgramUnit ? 1u : 0u;
    b.derivedTypes = p.derivedTypes.size();

    std::unordered_map<std::string, domain::Index> globals;
    for (domain::Index v = 0; v < p.variables.size(); ++v)
        if (p.variables[v].scope == domain::VariableScope::Global) globals[lower(p.strings.text(p.variables[v].name))] = v;

    const std::set<domain::Index> chosen(o.sections.begin(), o.sections.end());
    std::vector<std::size_t> taskOrder(p.tasks.size());
    for (std::size_t i = 0; i < taskOrder.size(); ++i) taskOrder[i] = i;
    std::stable_sort(taskOrder.begin(), taskOrder.end(),
                     [&](std::size_t a, std::size_t c) { return taskRank(p.strings.text(p.tasks[a].name)) < taskRank(p.strings.text(p.tasks[c].name)); });

    std::set<domain::Index> placed;
    std::size_t rank = 0;
    const auto makeSection = [&](domain::Index s, const std::string& owner, const std::string& task, bool inUnit) {
        const auto& sec = p.sections[s];
        Section x;
        x.rank = ++rank;
        x.index = s;
        x.name = text(p, sec.name);
        x.owner = owner;
        x.task = task;
        x.inUnit = inUnit;
        x.condition = text(p, sec.activationCondition);
        x.comment = firstComment(sec.body);
        x.lines = linesOf(sec.body);
        x.lineCount = x.lines.size();
        if (o.access) {
            const auto acc = project::api::accessOfSection(p, s);
            x.writes = acc.writes;
            x.reads = acc.reads;
            x.calls = callsOf(p, sec, globals);
        }
        x.icon = o.roles ? ci::sectionIcon(p, s) : -1;
        b.totalLines += x.lineCount;
        placed.insert(s);
        return x;
    };
    const auto wanted = [&](const Section& x, const std::string& unitName) {
        switch (o.scope) {
            case Options::Scope::All: return true;
            case Options::Scope::Unit: return !unitName.empty() && lower(unitName) == lower(o.unit);
            case Options::Scope::Sections: return chosen.count(x.index) > 0;
        }
        return true;
    };
    for (const auto ti : taskOrder) {
        const auto& task = p.tasks[ti];
        const std::string taskName = text(p, task.name);
        TaskInfo info;
        info.name = taskName;
        info.kind = taskKind(task);
        info.period = task.period;
        info.watchdog = task.watchdog;
        const auto taskItem = b.order.size();
        bool taskShown = false;
        for (const auto& entry : domain::executionEntries(p, taskName)) {
            if (entry.unit && entry.pou < p.pous.size()) {
                const auto& pou = p.pous[entry.pou];
                Unit u;
                u.name = text(p, pou.name);
                u.task = taskName;
                u.rank = pou.order;
                if (o.unitParams)
                    for (const auto v : pou.parameters) u.params.push_back(paramOf(p, v));
                u.locals = pou.locals.size();
                u.icon = o.roles ? ci::pouIcon(p, entry.pou) : -1;
                const auto unitIndex = b.units.size();
                bool unitShown = false;
                for (const auto s : entry.sections) {
                    if (s >= p.sections.size()) continue;
                    auto x = makeSection(s, u.name, taskName, true);
                    ++u.sections;
                    u.lines += x.lineCount;
                    ++info.sections;
                    info.lines += x.lineCount;
                    if (!wanted(x, u.name)) continue;
                    if (!unitShown && o.scope != Options::Scope::Sections) {
                        b.order.push_back({Item::Unit, unitIndex});
                        unitShown = true;
                    }
                    b.scopeLines += x.lineCount;
                    b.order.push_back({Item::Section, b.sections.size()});
                    b.sections.push_back(std::move(x));
                    taskShown = true;
                }
                b.units.push_back(std::move(u));
                continue;
            }
            for (const auto s : entry.sections) {
                if (s >= p.sections.size()) continue;
                auto x = makeSection(s, taskName, taskName, false);
                ++info.sections;
                info.lines += x.lineCount;
                if (!wanted(x, {})) continue;
                b.scopeLines += x.lineCount;
                b.order.push_back({Item::Section, b.sections.size()});
                b.sections.push_back(std::move(x));
                taskShown = true;
            }
        }
        b.tasks.push_back(info);
        // Le titre d'une tache : quand il y en a plusieurs dans le document.
        if (taskShown && p.tasks.size() > 1)
            b.order.insert(b.order.begin() + static_cast<std::ptrdiff_t>(taskItem), Item{Item::Task, b.tasks.size() - 1});
    }
    // Les sections qu'aucune tache n'execute d'elle-meme (les sous-routines) : a la fin.
    {
        TaskInfo info;
        info.name = "Sous-routines";
        info.kind = "appel\xC3\xA9" "es par le code";
        const auto taskItem = b.order.size();
        bool any = false;
        for (domain::Index s = 0; s < p.sections.size(); ++s) {
            if (placed.count(s)) continue;
            const auto& sec = p.sections[s];
            if (sec.owner < p.pous.size() && p.pous[sec.owner].kind == domain::PouKind::FunctionBlockType) continue;
            auto x = makeSection(s, text(p, sec.task).empty() ? std::string("(sans t\xC3\xA2" "che)") : text(p, sec.task), info.name, false);
            ++info.sections;
            info.lines += x.lineCount;
            if (!wanted(x, {})) continue;
            b.scopeLines += x.lineCount;
            b.order.push_back({Item::Section, b.sections.size()});
            b.sections.push_back(std::move(x));
            any = true;
        }
        if (any) {
            b.tasks.push_back(info);
            b.order.insert(b.order.begin() + static_cast<std::ptrdiff_t>(taskItem), Item{Item::Task, b.tasks.size() - 1});
        }
    }
    b.totalSections = rank;

    switch (o.scope) {
        case Options::Scope::All: b.scopeLabel = "Tout le programme"; break;
        case Options::Scope::Unit: b.scopeLabel = "L'unit\xC3\xA9 de programme " + o.unit; break;
        case Options::Scope::Sections:
            b.scopeLabel = b.sections.size() == 1 ? "La section " + (b.sections.empty() ? std::string{} : b.sections.front().name)
                                                  : std::to_string(b.sections.size()) + " sections choisies";
            break;
    }

    // Les annexes. Une partie du programme : seulement ce qu'elle emploie.
    std::set<std::string> usedTypes, usedVars;
    for (const auto& s : b.sections) {
        for (const auto& c : s.calls) {
            const auto open = c.find(" (");
            if (open != std::string::npos) usedTypes.insert(lower(c.substr(open + 2, c.size() - open - 3)));
        }
        for (const auto& w : s.writes) usedVars.insert(lower(w));
        for (const auto& r : s.reads) usedVars.insert(lower(r));
    }
    const bool all = o.scope == Options::Scope::All;
    if (o.dfbAppendix)
        for (domain::Index i = 0; i < p.pous.size(); ++i) {
            const auto& pou = p.pous[i];
            if (pou.kind != domain::PouKind::FunctionBlockType || !pou.userDefined) continue;
            Dfb d;
            d.name = text(p, pou.name);
            if (!all && !usedTypes.count(lower(d.name))) continue;
            d.version = pou.version;
            for (const auto v : pou.parameters) {
                d.params.push_back(paramOf(p, v));
                if (v < p.variables.size()) {
                    const auto sc = p.variables[v].scope;
                    d.inputs += sc == domain::VariableScope::Input ? 1u : 0u;
                    d.outputs += sc == domain::VariableScope::Output ? 1u : 0u;
                    d.inOuts += sc == domain::VariableScope::InOut ? 1u : 0u;
                }
            }
            for (const auto s : pou.sections) {
                if (s >= p.sections.size()) continue;
                Dfb::Body body;
                body.name = text(p, p.sections[s].name);
                body.lines = linesOf(p.sections[s].body);
                body.icon = o.roles ? ci::sectionIcon(p, s) : -1;
                d.lines += body.lines.size();
                d.bodies.push_back(std::move(body));
            }
            d.icon = o.roles ? ci::pouIcon(p, i) : -1;
            b.dfbs.push_back(std::move(d));
        }
    if (o.variablesAppendix)
        for (const auto& v : p.variables) {
            if (v.scope != domain::VariableScope::Global) continue;
            Variable x;
            x.name = text(p, v.name);
            if (!all && !usedVars.count(lower(x.name))) continue;
            x.type = text(p, v.type.name);
            x.address = v.address.raw;
            x.initial = text(p, v.initValue);
            x.comment = text(p, v.comment);
            b.variables.push_back(std::move(x));
        }
    return b;
}

std::string fileStem(const Book& b, const Options& o, std::string_view isoDate) {
    std::string stem = "Programme_" + sanitize(b.project);
    switch (o.scope) {
        case Options::Scope::All: stem += "_" + std::string(isoDate); break;
        case Options::Scope::Unit: stem += "_" + sanitize(o.unit); break;
        case Options::Scope::Sections: {
            std::size_t n = 0;
            for (const auto& s : b.sections) {
                if (n == 3) {
                    stem += "_etc";
                    break;
                }
                stem += "_" + sanitize(s.name);
                ++n;
            }
            break;
        }
    }
    return stem;
}

// --------------------------------------------------------------- la couleur ---
std::vector<Run> colorize(std::string_view line, bool& inComment) {
    static const std::set<std::string> kKeywords{"if", "then", "else", "elsif", "end_if", "for", "to", "by", "do", "end_for", "while",
                                                 "end_while", "repeat", "until", "end_repeat", "case", "of", "end_case", "and", "or",
                                                 "xor", "not", "mod", "true", "false", "return", "exit"};
    static const std::set<std::string> kTypes{"bool", "ebool", "int", "dint", "uint", "udint", "word", "dword", "real", "time",
                                              "string", "byte", "array"};
    std::vector<Run> out;
    const auto push = [&out](std::string_view t, Run::Kind k) {
        if (t.empty()) return;
        if (!out.empty() && out.back().kind == k) out.back().text += t;
        else out.push_back({std::string(t), k});
    };
    std::size_t i = 0;
    const std::size_t n = line.size();
    while (i < n) {
        if (inComment) {
            const auto e = line.find("*)", i);
            const std::size_t j = e == std::string_view::npos ? n : e + 2;
            push(line.substr(i, j - i), Run::Comment);
            if (e != std::string_view::npos) inComment = false;
            i = j;
            continue;
        }
        const char c = line[i];
        if (c == '(' && i + 1 < n && line[i + 1] == '*') {
            inComment = true;
            continue;
        }
        if (c == '\'') {
            const auto e = line.find('\'', i + 1);
            const std::size_t j = e == std::string_view::npos ? n : e + 1;
            push(line.substr(i, j - i), Run::String);
            i = j;
            continue;
        }
        if (c == '%') {
            std::size_t j = i + 1;
            while (j < n && std::isalpha(static_cast<unsigned char>(line[j]))) ++j;
            while (j < n && (std::isdigit(static_cast<unsigned char>(line[j])) || line[j] == '.' || line[j] == ':')) ++j;
            if (j > i + 1) {
                push(line.substr(i, j - i), Run::Address);
                i = j;
                continue;
            }
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < n && identChar(line[j])) ++j;
            if (j < n && line[j] == '#') {      // t#1s, DINT#1
                ++j;
                while (j < n && (identChar(line[j]) || line[j] == '.')) ++j;
                push(line.substr(i, j - i), Run::Number);
                i = j;
                continue;
            }
            const auto word = line.substr(i, j - i);
            const auto low = lower(word);
            if (kKeywords.count(low)) push(word, Run::Keyword);
            else if (kTypes.count(low)) push(word, Run::Type);
            else if (low.find("_to_") != std::string::npos || low == "shl" || low == "shr" || low == "abs" || low == "sqrt" || low == "min"
                     || low == "max" || low == "limit" || low == "sel")
                push(word, Run::Function);
            else push(word, Run::Plain);
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) && (i == 0 || !identChar(line[i - 1]))) {
            std::size_t j = i;
            while (j < n && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '.' || line[j] == '_')) ++j;
            push(line.substr(i, j - i), Run::Number);
            i = j;
            continue;
        }
        push(line.substr(i, 1), Run::Plain);
        ++i;
    }
    return out;
}

// ---------------------------------------------------------------- le texte ---
namespace {

constexpr std::size_t kWidth = 100;

std::size_t columns(std::string_view s) {
    std::size_t n = 0;
    for (const char c : s) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1u : 0u;
    return n;
}

std::string pad(std::string s, std::size_t width) {
    // Coupe (en colonnes) puis complete d'espaces.
    std::size_t cols = 0, cut = s.size();
    for (std::size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) continue;
        if (cols == width) {
            cut = i;
            break;
        }
        ++cols;
    }
    s.resize(cut);
    if (cols < width) s.append(width - cols, ' ');
    return s;
}

std::string leftRight(const std::string& left, const std::string& right) {
    const auto l = columns(left), r = columns(right);
    return left + std::string(l + r + 1 < kWidth ? kWidth - l - r : 1, ' ') + right;
}

// Une liste de noms sur plusieurs lignes, alignee apres son libelle.
void listLines(std::vector<std::string>& out, const std::string& label, const std::vector<std::string>& items) {
    if (items.empty()) {
        out.push_back(label + "\xE2\x80\x94");
        return;
    }
    const std::string indent(columns(label), ' ');
    std::string cur = label;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const std::string piece = items[i] + (i + 1 < items.size() ? ", " : "");
        if (columns(cur) + columns(piece) > kWidth && cur != label) {
            while (!cur.empty() && cur.back() == ' ') cur.pop_back();
            out.push_back(cur);
            cur = indent;
        }
        cur += piece;
    }
    out.push_back(cur);
}

} // namespace

std::string toText(const Book& b, const Options& o) {
    using exporter::doc::thousands;
    std::vector<std::string> L;
    const std::string eq(kWidth, '='), dash(kWidth, '-'), hash(kWidth, '#');
    const std::string mainTask = b.tasks.empty() ? std::string("MAST") : b.tasks.front().name;
    L.push_back(eq);
    L.push_back("PROGRAMME DE L'AUTOMATE \xE2\x80\x94 " + b.project);
    L.push_back(o.scope == Options::Scope::All ? "Lu dans l'ordre d'ex\xC3\xA9" "cution de la t\xC3\xA2" "che " + mainTask
                                               : "Extrait : " + b.scopeLabel + ", dans l'ordre d'ex\xC3\xA9" "cution");
    L.push_back(eq);
    L.push_back({});
    L.push_back("Projet      : " + b.project + (b.version.empty() ? std::string{} : " (version " + b.version + ")"));
    if (!b.cpu.empty() || !b.product.empty()) L.push_back("Automate    : " + b.cpu + (b.product.empty() ? std::string{} : " \xE2\x80\x94 " + b.product));
    for (const auto& t : b.tasks) {
        if (t.name == "Sous-routines") continue;
        std::string line = "T\xC3\xA2" "che       : " + t.name + ", " + t.kind;
        if (t.period > 0) line += ", p\xC3\xA9riode " + std::to_string(t.period) + " ms";
        if (t.watchdog > 0) line += ", chien de garde " + std::to_string(t.watchdog) + " ms";
        L.push_back(line);
    }
    L.push_back("Contenu     : " + std::to_string(b.totalSections) + " sections, " + thousands(b.totalLines) + " lignes de ST ; "
                + std::to_string(b.programUnits) + (b.programUnits > 1 ? " unit\xC3\xA9s" : " unit\xC3\xA9") + " de programme ; "
                + std::to_string(b.dfbs.size()) + " blocs DFB");
    if (o.scope != Options::Scope::All)
        L.push_back("Export\xC3\xA9     : " + b.scopeLabel + " (" + std::to_string(b.sections.size()) + " sections, " + thousands(b.scopeLines) + " lignes)");
    L.push_back("Export\xC3\xA9 le  : " + (b.exportedAt.empty() ? std::string("?") : b.exportedAt) + ", par " + b.app
                + (b.source.empty() ? std::string{} : " (depuis " + b.source + ")"));
    L.push_back({});
    if (o.guide) {
        L.push_back("COMMENT LIRE CE FICHIER");
        L.push_back("-----------------------");
        L.push_back("L'automate ex\xC3\xA9" "cute la t\xC3\xA2" "che " + mainTask + " en boucle. \xC3\x80 chaque cycle, il ex\xC3\xA9" "cute les sections dans l'ordre");
        L.push_back("de ce fichier, de 1 \xC3\xA0 " + std::to_string(b.totalSections) + ", puis recommence.");
        L.push_back(" - \xC2\xAB Condition : X \xC2\xBB : la section ne s'ex\xC3\xA9" "cute que pendant les cycles o\xC3\xB9 X est vrai.");
        L.push_back(" - Une unit\xC3\xA9 de programme regroupe des sections qui s'ex\xC3\xA9" "cutent en bloc, \xC3\xA0 son rang. Ses");
        L.push_back("   param\xC3\xA8tres relient ses noms \xC3\xA0 ceux du projet (armoires -> Armoires).");
        L.push_back(" - Le code est du texte structur\xC3\xA9 (ST, norme CEI 61131-3) : := affecte une valeur, (* ... *) est");
        L.push_back("   un commentaire, IF ... THEN ... END_IF une condition.");
        if (o.access) L.push_back(" - \xC2\xAB \xC3\x89" "crit \xC2\xBB et \xC2\xAB Lit \xC2\xBB : les variables du projet que la section modifie et celles qu'elle consulte.");
        if (o.roles) L.push_back(" - [R\xC3\xB4le] : l'ic\xC3\xB4ne choisie pour la section dans " + o.appName + ".");
        L.push_back({});
        L.push_back("SOMMAIRE \xE2\x80\x94 ordre d'ex\xC3\xA9" "cution");
        L.push_back(std::string(30, '-'));
        L.push_back("  N\xC2\xB0  " + pad("Section", 30) + " " + pad("Unit\xC3\xA9 / t\xC3\xA2" "che", 18) + " " + pad("Condition", 34) + " " + "Lignes");
        L.push_back("  --  " + std::string(30, '-') + " " + std::string(18, '-') + " " + std::string(34, '-') + " " + std::string(6, '-'));
        for (const auto& it : b.order) {
            if (it.kind == Item::Task) {
                const auto& t = b.tasks[it.index];
                L.push_back("      == T\xC3\xA2" "che " + t.name + " : " + std::to_string(t.sections) + " sections, " + thousands(t.lines) + " lignes");
                continue;
            }
            if (it.kind == Item::Unit) {
                const auto& u = b.units[it.index];
                L.push_back("      >> Unit\xC3\xA9 de programme " + u.name + " (rang " + std::to_string(u.rank) + ") : " + std::to_string(u.sections)
                            + " sections, " + thousands(u.lines) + " lignes, " + std::to_string(u.params.size()) + " param\xC3\xA8tres");
                continue;
            }
            const auto& s = b.sections[it.index];
            std::string num = std::to_string(s.rank);
            if (num.size() < 2) num = " " + num;
            std::string lines = thousands(s.lineCount);
            if (lines.size() < 6) lines = std::string(6 - lines.size(), ' ') + lines;
            L.push_back("  " + num + "  " + pad(s.name, 30) + " " + pad(s.owner, 18) + " " + pad(s.condition, 34) + " " + lines);
        }
        std::string annexes;
        if (o.dfbAppendix && !b.dfbs.empty()) annexes += "A. Blocs DFB (" + std::to_string(b.dfbs.size()) + ")";
        if (o.variablesAppendix && !b.variables.empty())
            annexes += std::string(annexes.empty() ? "" : "   ") + (annexes.empty() ? "A" : "B") + ". Variables globales (" + std::to_string(b.variables.size()) + ")";
        if (!annexes.empty()) {
            L.push_back({});
            L.push_back("Annexes : " + annexes);
        }
        L.push_back({});
    }
    for (const auto& it : b.order) {
        if (it.kind == Item::Task) {
            const auto& t = b.tasks[it.index];
            L.push_back({});
            L.push_back(hash);
            L.push_back(leftRight("T\xC3\xA2" "CHE " + t.name, t.kind + " \xC2\xB7 " + std::to_string(t.sections) + " sections"));
            L.push_back(hash);
            continue;
        }
        if (it.kind == Item::Unit) {
            const auto& u = b.units[it.index];
            L.push_back({});
            L.push_back(hash);
            L.push_back(leftRight("UNIT\xC3\x89 DE PROGRAMME  " + u.name + (u.icon >= 0 && o.roles ? "  [" + roleName(u.icon) + "]" : std::string{}),
                                  "rang " + std::to_string(u.rank) + " dans " + u.task + " \xC2\xB7 " + std::to_string(u.sections) + " sections \xC2\xB7 "
                                      + thousands(u.lines) + " lignes"));
            L.push_back(hash);
            if (o.unitParams) {
                L.push_back("Param\xC3\xA8tres (" + std::to_string(u.params.size()) + ") :");
                L.push_back("  " + pad("Sens", 8) + " " + pad("Nom", 24) + " " + pad("Type", 26) + " Re\xC3\xA7oit (variable du projet)");
                for (const auto& prm : u.params)
                    L.push_back("  " + pad(prm.direction, 8) + " " + pad(prm.name, 24) + " " + pad(prm.type, 26) + " " + prm.receives);
            }
            L.push_back("Variables locales : " + std::to_string(u.locals));
            continue;
        }
        const auto& s = b.sections[it.index];
        L.push_back({});
        L.push_back(eq);
        std::string num = std::to_string(s.rank);
        if (num.size() < 2) num = " " + num;
        L.push_back(leftRight("[" + num + "/" + std::to_string(b.totalSections) + "]  " + s.name
                                  + (s.icon >= 0 && o.roles ? "  [" + roleName(s.icon) + "]" : std::string{}),
                              s.owner + " \xC2\xB7 " + thousands(s.lineCount) + " lignes"));
        L.push_back(eq);
        L.push_back("Condition   : " + (s.condition.empty() ? std::string("aucune (\xC3\xA0 chaque cycle)") : s.condition));
        if (!s.comment.empty()) L.push_back("Commentaire : " + s.comment);
        if (o.access) {
            listLines(L, "\xC3\x89" "crit (" + std::to_string(s.writes.size()) + ")" + std::string(s.writes.size() < 10 ? 2 : 1, ' ') + ": ", s.writes);
            listLines(L, "Lit (" + std::to_string(s.reads.size()) + ")" + std::string(s.reads.size() < 10 ? 4 : 3, ' ') + ": ", s.reads);
            if (!s.calls.empty()) listLines(L, "Appelle     : ", s.calls);
        }
        L.push_back(dash);
        for (std::size_t i = 0; i < s.lines.size(); ++i) {
            if (o.lineNumbers) {
                std::string n = std::to_string(i + 1);
                L.push_back(std::string(n.size() < 5 ? 5 - n.size() : 0, ' ') + n + "  " + s.lines[i]);
            } else {
                L.push_back(s.lines[i]);
            }
        }
    }
    char letter = 'A';
    if (o.dfbAppendix && !b.dfbs.empty()) {
        L.push_back({});
        L.push_back(hash);
        L.push_back(std::string("ANNEXE ") + letter++ + " \xE2\x80\x94 LES BLOCS DFB (" + std::to_string(b.dfbs.size()) + ")");
        L.push_back(hash);
        for (const auto& d : b.dfbs) {
            L.push_back({});
            L.push_back(eq);
            L.push_back(leftRight(d.name + (d.version.empty() ? std::string{} : "  version " + d.version)
                                      + (d.icon >= 0 && o.roles ? "  [" + roleName(d.icon) + "]" : std::string{}),
                                  std::to_string(d.inputs) + " entr\xC3\xA9" "es \xC2\xB7 " + std::to_string(d.outputs) + " sorties \xC2\xB7 "
                                      + std::to_string(d.inOuts) + " E/S \xC2\xB7 " + thousands(d.lines) + " lignes"));
            L.push_back(eq);
            for (const auto& prm : d.params)
                L.push_back("  " + pad(prm.direction, 8) + " " + pad(prm.name, 24) + " " + pad(prm.type, 26) + (prm.comment.empty() ? std::string{} : " " + prm.comment));
            for (const auto& body : d.bodies) {
                L.push_back(dash);
                L.push_back("Section " + body.name + " (" + thousands(body.lines.size()) + " lignes)");
                L.push_back(dash);
                for (std::size_t i = 0; i < body.lines.size(); ++i) {
                    std::string n = std::to_string(i + 1);
                    L.push_back(o.lineNumbers ? std::string(n.size() < 5 ? 5 - n.size() : 0, ' ') + n + "  " + body.lines[i] : body.lines[i]);
                }
            }
        }
    }
    if (o.variablesAppendix && !b.variables.empty()) {
        L.push_back({});
        L.push_back(hash);
        L.push_back(std::string("ANNEXE ") + letter + " \xE2\x80\x94 LES VARIABLES GLOBALES (" + std::to_string(b.variables.size()) + ")");
        L.push_back(hash);
        L.push_back(pad("Nom", 32) + " " + pad("Type", 28) + " " + pad("Adresse", 10) + " " + pad("Valeur init.", 12) + " Commentaire");
        for (const auto& v : b.variables)
            L.push_back(pad(v.name, 32) + " " + pad(v.type, 28) + " " + pad(v.address, 10) + " " + pad(v.initial, 12) + " " + v.comment);
    }
    L.push_back({});
    L.push_back("-- fin du fichier --");
    std::string out = "\xEF\xBB\xBF";
    for (const auto& l : L) {
        out += l;
        out += "\r\n";
    }
    return out;
}

} // namespace exporter::book
