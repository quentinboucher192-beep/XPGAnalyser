#include "ProjectStore.hpp"
#include "ProjectIcon.hpp"

#include "../export/XpgWriter.hpp"
#include "../import/ProjectParser.hpp"
#include "../import/XmlReader.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>
#include <cstdlib>

namespace project {

using namespace domain;
namespace fs = std::filesystem;

namespace {

// --- a tiny semicolon table -------------------------------------------------
// Values are escaped so a comment containing a semicolon or a newline survives
// the trip. Chosen over CSV quoting because it stays readable in a diff.
std::string encodeField(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case ';':  out += "\\s"; break;
            case '\n': out += "\\n"; break;
            case '\r': break;
            case '\\': out += "\\\\"; break;
            default:   out.push_back(c);
        }
    }
    return out;
}

std::string decodeField(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { out.push_back(s[i]); continue; }
        switch (s[++i]) {
            case 's':  out.push_back(';'); break;
            case 'n':  out.push_back('\n'); break;
            case '\\': out.push_back('\\'); break;
            default:   out.push_back(s[i]);
        }
    }
    return out;
}

std::vector<std::string> splitFields(std::string_view line) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (true) {
        const auto sep = line.find(';', from);
        auto piece = line.substr(from, (sep == std::string_view::npos ? line.size() : sep) - from);
        while (!piece.empty() && (piece.front() == ' ' || piece.front() == '\t')) piece.remove_prefix(1);
        while (!piece.empty() && (piece.back() == ' ' || piece.back() == '\t' || piece.back() == '\r'))
            piece.remove_suffix(1);
        out.emplace_back(decodeField(piece));
        if (sep == std::string_view::npos) break;
        from = sep + 1;
    }
    return out;
}

std::string field(const std::vector<std::string>& f, std::size_t i) {
    return i < f.size() ? f[i] : std::string{};
}

core::Status writeText(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return core::fail(core::ErrorCode::FileUnreadable, "cannot write", path.string());
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out ? core::ok()
               : core::fail(core::ErrorCode::FileUnreadable, "short write", path.string());
}

core::Result<std::string> readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return core::fail(core::ErrorCode::FileNotFound, path.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void eachLine(std::string_view text, const std::function<void(std::string_view)>& fn) {
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto nl = text.find('\n', from);
        auto line = text.substr(from, (nl == std::string_view::npos ? text.size() : nl) - from);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty() && line.front() != '#') fn(line);
        if (nl == std::string_view::npos) break;
        from = nl + 1;
    }
}

std::string nowStamp() {
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[80];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

// A name that is safe as a file name on Windows and readable on disk.
std::string safeName(std::string_view name) {
    std::string out;
    for (char c : name) {
        const bool bad = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?'
                      || c == '"' || c == '<' || c == '>' || c == '|';
        out.push_back(bad ? '_' : c);
    }
    if (out.empty()) out = "unnamed";
    return out;
}

std::string_view scopeName(VariableScope s) { return toString(s); }

VariableScope scopeFromName(std::string_view s) {
    if (s == "Local")    return VariableScope::Local;
    if (s == "Public")   return VariableScope::Public;
    if (s == "Input")    return VariableScope::Input;
    if (s == "Output")   return VariableScope::Output;
    if (s == "InOut")    return VariableScope::InOut;
    if (s == "Constant") return VariableScope::Constant;
    if (s == "Member")   return VariableScope::DerivedMember;
    return VariableScope::Global;
}

std::string_view languageName(PouLanguage l) { return toString(l); }

PouLanguage languageFromName(std::string_view s) {
    if (s == "IL")  return PouLanguage::IL;
    if (s == "LD")  return PouLanguage::LD;
    if (s == "FBD") return PouLanguage::FBD;
    if (s == "SFC") return PouLanguage::SFC;
    if (s == "ST")  return PouLanguage::ST;
    return PouLanguage::Unknown;
}

// --- one variable, one line -------------------------------------------------
constexpr const char* kVariableHeader =
    "# name ; type ; address ; scope ; init ; comment ; attributes ; element comments\n";

std::string encodeVariable(const Project& p, const Variable& v) {
    std::string attrs;
    for (const auto& [n, val] : v.attributes) {
        if (!attrs.empty()) attrs += ',';
        attrs += n + '=' + val;
    }
    std::string elements;
    for (const auto& e : v.instanceElements) {
        if (!elements.empty()) elements += ',';
        elements += e.name + '=' + std::string(p.strings.text(e.comment));
    }
    std::string line;
    line += encodeField(p.strings.text(v.name));            line += " ; ";
    line += encodeField(p.strings.text(v.type.name));       line += " ; ";
    line += encodeField(v.address.raw);                     line += " ; ";
    line += encodeField(scopeName(v.scope));                line += " ; ";
    line += encodeField(p.strings.text(v.initValue));       line += " ; ";
    line += encodeField(p.strings.text(v.comment));         line += " ; ";
    line += encodeField(attrs);                             line += " ; ";
    line += encodeField(elements);
    return line;
}

Variable decodeVariable(Project& p, const std::vector<std::string>& f) {
    Variable v;
    v.name  = p.strings.intern(field(f, 0));
    v.scope = scopeFromName(field(f, 3));
    // LE MEME CLASSEMENT QUE L'IMPORT DU .XPG. Le nom seul ne suffit pas : un
    // "ARRAY[0..1] OF armoire" relu sans ses bornes ni son type d'element
    // pesait 0 octet, les DDT qui contiennent des tableaux etaient sous-estimes,
    // et le simulateur ne savait plus indexer Armoires[i] dans un projet
    // rouvert depuis son dossier (alors qu'il le savait juste apres l'import).
    v.type = importer::classifyTypeName(field(f, 1), p.strings);
    if (const auto addr = field(f, 2); !addr.empty()) {
        v.address = Address::parse(addr);
        v.located = v.address.valid();
    }
    v.initValue = p.strings.intern(field(f, 4));
    v.comment   = p.strings.intern(field(f, 5));

    auto splitPairs = [](std::string_view s, char sep) {
        std::vector<std::pair<std::string, std::string>> out;
        std::size_t from = 0;
        while (from < s.size()) {
            const auto next = s.find(sep, from);
            const auto item = s.substr(from, (next == std::string_view::npos ? s.size() : next) - from);
            const auto eq = item.find('=');
            if (eq != std::string_view::npos)
                out.emplace_back(std::string(item.substr(0, eq)), std::string(item.substr(eq + 1)));
            if (next == std::string_view::npos) break;
            from = next + 1;
        }
        return out;
    };
    for (auto& [n, val] : splitPairs(field(f, 6), ',')) v.attributes.emplace_back(n, val);
    for (auto& [n, val] : splitPairs(field(f, 7), ','))
        v.instanceElements.push_back(InstanceElement{n, p.strings.intern(val)});
    return v;
}

} // namespace

// ===========================================================================
std::shared_ptr<Project> ProjectStore::createEmpty(std::string name, std::string cpuReference) {
    auto p = std::make_shared<Project>();
    p->header.company        = "Schneider Automation";
    p->header.product        = "XpgAnalyzer 1.0";
    p->header.dtdVersion     = "41";
    p->header.projectName    = std::move(name);
    p->header.projectVersion = "0.0.1";

    p->hardware.cpuReference = cpuReference.empty() ? "BMXP342020" : std::move(cpuReference);
    p->hardware.resourceName = "Micro Basic";
    p->hardware.family       = "Modicon M340";
    p->hardware.inferred     = true;

    // Every M340 project has a MAST task; a project without one cannot run.
    Task mast;
    mast.name     = p->strings.intern("MAST");
    mast.type     = "cyclic";
    mast.watchdog = 250;
    p->tasks.push_back(std::move(mast));

    p->buildIndices();
    return p;
}

// --------------------------------------------------------------------- save --
core::Status ProjectStore::save(const Project& p, const Manifest& manifest,
                                const std::string& folder, const LockRecord& lock) {
    const fs::path root(folder);
    std::error_code ec;
    fs::create_directories(root, ec);

    // ---- manifest ---------------------------------------------------------
    {
        std::ostringstream m;
        m << "# XpgAnalyzer project. Safe to read; edit with care.\n";
        m << "formatVersion = " << manifest.formatVersion << '\n';
        m << "name = "     << manifest.name << '\n';
        m << "version = "  << manifest.version << '\n';
        m << "state = "    << toString(manifest.state) << '\n';
        m << "created = "  << (manifest.created.empty() ? nowStamp() : manifest.created) << '\n';
        m << "modified = " << nowStamp() << '\n';
        m << "author = "   << manifest.author << '\n';
        m << "cpu = "      << (manifest.cpuReference.empty() ? p.hardware.cpuReference
                                                             : manifest.cpuReference) << '\n';
        m << "firmware = " << (manifest.cpuFirmware.empty() ? p.hardware.cpuFirmware
                                                            : manifest.cpuFirmware) << '\n';
        m << "comment = "  << manifest.comment << '\n';
        m << "company = "  << (manifest.company.empty() ? p.header.company : manifest.company) << '\n';
        m << "product = "  << (manifest.product.empty() ? p.header.product : manifest.product) << '\n';
        m << "dtd = "      << (manifest.dtdVersion.empty() ? p.header.dtdVersion
                                                           : manifest.dtdVersion) << '\n';
        // Lot API 5 : le reste de l'en-tete du fichier d'origine (« multiprogramme »,
        // la date du contentHeader, les fins de ligne). Sans eux, l'export d'un
        // projet range en dossier ne rendait plus les memes octets qu'un export direct.
        m << "content = "      << (manifest.contentKind.empty() ? p.header.contentKind : manifest.contentKind) << '\n';
        m << "content-date = " << (manifest.contentDateTime.empty() ? p.header.contentDateTime : manifest.contentDateTime) << '\n';
        m << "crlf = "         << (p.header.crlf ? "oui" : "non") << '\n';
        if (auto r = writeText(root / "project.xpgproj", m.str()); !r) return r;
    }

    // ---- lock -------------------------------------------------------------
    if (manifest.state == State::Lock && lock.valid()) {
        std::ostringstream l;
        l << "# Access control for this project. Removing this file does not\n"
             "# decrypt anything - the sources were never encrypted - it only\n"
             "# removes the prompt. Treat it as a gate, not as a safe.\n";
        l << "salt = "       << lock.salt << '\n';
        l << "iterations = " << lock.iterations << '\n';
        l << "password = "   << lock.passwordHash << '\n';
        l << "master = "     << lock.masterHash << '\n';
        if (auto r = writeText(root / "project.lock", l.str()); !r) return r;
    } else {
        fs::remove(root / "project.lock", ec);
    }

    // ---- hardware ---------------------------------------------------------
    {
        std::ostringstream h;
        h << "# PLC configuration.\n";
        h << "cpu = "        << p.hardware.cpuReference << '\n';
        h << "firmware = "   << p.hardware.cpuFirmware << '\n';
        h << "family = "     << p.hardware.family << '\n';
        h << "resource = "   << p.hardware.resourceName << '\n';
        h << "bus = "        << p.hardware.busName << '\n';
        h << "internalBits = "   << p.hardware.memory.internalBits << '\n';
        h << "internalWords = "  << p.hardware.memory.internalWords << '\n';
        h << "constantWords = "  << p.hardware.memory.constantWords << '\n';
        h << "autoRun = "        << (p.hardware.memory.autoRun ? "true" : "false") << '\n';
        h << "initialiseWords = "<< (p.hardware.memory.initialiseWords ? "true" : "false") << '\n';
        h << "\n# rack ; number ; reference ; topological address\n";
        h << "# module ; rack ; slot ; reference ; family ; firmware ; kind ; in ; out ; topo ; guid\n";
        h << "# channel ; rack ; slot ; number ; direction ; role ; task ; code ; version ; iob ; KW ; KPW\n";
        for (const auto& rack : p.hardware.racks) {
            h << "rack ; " << rack.number << " ; " << encodeField(rack.reference)
              << " ; " << encodeField(rack.topologicalAddress) << '\n';
            for (const auto& m : rack.modules) {
                h << "module ; " << rack.number << " ; " << m.slot << " ; "
                  << encodeField(m.reference) << " ; " << encodeField(m.family) << " ; "
                  << encodeField(m.firmware) << " ; " << static_cast<int>(m.kind) << " ; "
                  << m.inputPoints << " ; " << m.outputPoints << " ; "
                  << encodeField(m.topologicalAddress) << " ; " << encodeField(m.nodeGuid) << '\n';
                for (const auto& c : m.channels) {
                    h << "channel ; " << rack.number << " ; " << m.slot << " ; " << c.number
                      << " ; " << static_cast<int>(c.direction) << " ; " << encodeField(c.role)
                      << " ; " << encodeField(c.task) << " ; " << c.functionCode
                      << " ; " << c.functionVersion << " ; " << encodeField(c.iobFile) << " ; ";
                    for (std::size_t i = 0; i < c.paramKW.size(); ++i)
                        h << (i ? "," : "") << c.paramKW[i];
                    h << " ; ";
                    for (std::size_t i = 0; i < c.paramKPW.size(); ++i)
                        h << (i ? "," : "") << c.paramKPW[i];
                    h << '\n';
                }
            }
        }
        if (auto r = writeText(root / "config" / "hardware.txt", h.str()); !r) return r;
    }

    // ---- global dictionary -------------------------------------------------
    {
        std::ostringstream g;
        g << "# Global variables.\n" << kVariableHeader;
        for (const auto& v : p.variables)
            if (v.scope == VariableScope::Global || v.scope == VariableScope::Constant)
                g << encodeVariable(p, v) << '\n';
        if (auto r = writeText(root / "vars" / "globals.txt", g.str()); !r) return r;
    }

    // ---- derived types -----------------------------------------------------
    fs::remove_all(root / "ddt", ec);
    {
        // Declaration order matters: Control Expert emits types in the order it
        // holds them, and an alphabetical reload would export a different file
        // for the same project. The index records the order the folder cannot.
        std::ostringstream idx;
        idx << "# Derived types, in declaration order.\n";
        for (const auto& d : p.derivedTypes) idx << p.strings.text(d.name) << '\n';
        if (auto r = writeText(root / "ddt" / "index.txt", idx.str()); !r) return r;
    }
    for (const auto& d : p.derivedTypes) {
        std::ostringstream t;
        t << "# Derived data type.\n";
        t << "name = "     << p.strings.text(d.name) << '\n';
        t << "version = "  << d.version << '\n';
        t << "# checksum is recomputed by Control Expert on import; not stored.\n\n";
        t << kVariableHeader;
        for (auto fi : d.fields) t << encodeVariable(p, p.variables[fi]) << '\n';
        const auto file = root / "ddt" / (safeName(p.strings.text(d.name)) + ".ddt");
        if (auto r = writeText(file, t.str()); !r) return r;
    }

    // ---- POUs --------------------------------------------------------------
    fs::remove_all(root / "dfb", ec);
    fs::remove_all(root / "units", ec);
    {
        std::ostringstream dfbIdx, unitIdx;
        dfbIdx  << "# DFB types, in declaration order.\n";
        unitIdx << "# Program units, in declaration order.\n";
        for (const auto& pou : p.pous) {
            if (pou.kind == PouKind::FunctionBlockType) dfbIdx  << p.strings.text(pou.name) << '\n';
            if (pou.kind == PouKind::ProgramUnit)       unitIdx << p.strings.text(pou.name) << '\n';
        }
        if (auto r = writeText(root / "dfb" / "index.txt", dfbIdx.str()); !r) return r;
        if (auto r = writeText(root / "units" / "index.txt", unitIdx.str()); !r) return r;
    }
    for (const auto& pou : p.pous) {
        const bool isDfb  = pou.kind == PouKind::FunctionBlockType;
        const bool isUnit = pou.kind == PouKind::ProgramUnit;
        if (!isDfb && !isUnit) continue;

        const auto dir = root / (isDfb ? "dfb" : "units") / safeName(p.strings.text(pou.name));

        std::ostringstream i;
        i << "# " << (isDfb ? "DFB type" : "Program unit") << " interface.\n";
        i << "name = "    << p.strings.text(pou.name) << '\n';
        i << "version = " << pou.version << '\n';
        if (isUnit) {
            i << "task = "  << p.strings.text(pou.task) << '\n';
            i << "order = " << pou.order << '\n';
        }
        for (const auto& [n, v] : pou.attributes) i << "attribute = " << n << " = " << v << '\n';
        i << '\n' << kVariableHeader;
        for (auto vi : pou.parameters) i << encodeVariable(p, p.variables[vi]) << '\n';
        for (auto vi : pou.locals)     i << encodeVariable(p, p.variables[vi]) << '\n';

        i << "\n# section ; name ; language ; order ; activation ; logic ; file\n";
        for (auto si : pou.sections) {
            const auto& s = p.sections[si];
            const auto file = safeName(p.strings.text(s.name)) + ".st";
            i << "section ; " << encodeField(p.strings.text(s.name)) << " ; "
              << languageName(s.language) << " ; " << s.order << " ; "
              << encodeField(p.strings.text(s.activationCondition)) << " ; "
              << encodeField(p.strings.text(s.logicCondition)) << " ; " << file << '\n';
            if (auto r = writeText(dir / "code" / file, s.body); !r) return r;
        }
        if (auto r = writeText(dir / "interface.txt", i.str()); !r) return r;
    }

    // ---- task sections -----------------------------------------------------
    fs::remove_all(root / "sections", ec);
    {
        std::ostringstream idx;
        idx << "# Task sections, in execution order.\n";
        idx << "# name ; task ; language ; order ; activation ; logic ; file\n";
        for (const auto& s : p.sections) {
            if (s.owner != kNoIndex && s.owner < p.pous.size()) {
                const auto k = p.pous[s.owner].kind;
                if (k == PouKind::FunctionBlockType || k == PouKind::ProgramUnit) continue;
            }
            const auto file = safeName(p.strings.text(s.name)) + ".st";
            idx << encodeField(p.strings.text(s.name)) << " ; "
                << encodeField(p.strings.text(s.task)) << " ; " << languageName(s.language)
                << " ; " << s.order << " ; "
                << encodeField(p.strings.text(s.activationCondition)) << " ; "
                << encodeField(p.strings.text(s.logicCondition)) << " ; " << file << '\n';
            if (auto r = writeText(root / "sections" / file, s.body); !r) return r;
        }
        if (auto r = writeText(root / "sections" / "index.txt", idx.str()); !r) return r;
    }

    // ---- animation tables ---------------------------------------------------
    {
        std::ostringstream a;
        // Lot API 3 : une ligne "ihm ; nom" pour une variable de l'IHM. Un ancien
        // fichier (que des "entry") se lit tel quel ; un ancien programme ignore
        // les lignes "ihm" (il ne connait que "table" et "entry").
        a << "# Watch tables.\n# table ; name ; owning POU\n# entry ; symbol   (une variable de l'automate)\n"
             "# ihm ; name      (une variable de l'IHM : jamais exportee vers Control Expert)\n";
        for (const auto& table : p.animationTables) {
            a << "table ; " << encodeField(p.strings.text(table.name)) << " ; "
              << encodeField(p.strings.text(table.owner)) << '\n';
            for (const auto& e : table.entries)
                a << (e.hmi ? "ihm ; " : "entry ; ") << encodeField(p.strings.text(e.name)) << '\n';
        }
        if (auto r = writeText(root / "tables" / "animation.txt", a.str()); !r) return r;
    }

    // ---- tasks -------------------------------------------------------------
    {
        std::ostringstream t;
        // Lot API 4 : la periode (ms) en quatrieme champ - une tache periodique
        // (FAST, AUX) en a une ; un ancien fichier (trois champs) se lit tel quel.
        t << "# name ; type ; watchdog (ms) ; period (ms, periodic only)\n";
        for (const auto& task : p.tasks)
            t << encodeField(p.strings.text(task.name)) << " ; " << encodeField(task.type)
              << " ; " << task.watchdog << " ; " << task.period << '\n';
        if (auto r = writeText(root / "config" / "tasks.txt", t.str()); !r) return r;
    }

    // ---- lot API 5 : les bornes de lecture du plan memoire ------------------
    {
        std::ostringstream m;
        m << "# zone ; debut ; fin  (les cellules lues, bornes comprises ; absente : toute la zone)\n";
        static const char* const kZones[] = {"M", "MW", "KW"};
        for (std::size_t z = 0; z < p.memoryWindows.size(); ++z)
            if (p.memoryWindows[z].set) m << kZones[z] << " ; " << p.memoryWindows[z].from << " ; " << p.memoryWindows[z].to << '\n';
        if (auto r = writeText(root / "config" / "memoire.txt", m.str()); !r) return r;
    }

    // ---- 1.8.0 : les icones au choix de ce qui porte du code (aucune : pas de fichier)
    {
        const auto path = root / "config" / "icones-code.txt";
        if (!p.codeIcons.empty()) {
            std::ostringstream o;
            o << "# 1.8.0 : les icones choisies. element ; icone (core/CodeIcons.hpp)\n";
            for (const auto& [key, value] : p.codeIcons)
                if (!key.empty() && !value.empty()) o << key << " ; " << value << '\n';
            if (auto r = writeText(path, o.str()); !r) return r;
        } else {
            std::error_code gone;
            fs::remove(path, gone);
        }
    }

    // ---- lot API 6 : l'icone du projet (pas d'icone : pas de fichier) --------
    {
        const auto path = root / "config" / "icone.txt";
        if (!p.icon.empty()) {
            if (auto r = writeText(path, icon::toText(p.icon)); !r) return r;
        } else {
            std::error_code gone;
            fs::remove(path, gone);
        }
    }

    return core::ok();
}

// --------------------------------------------------------------------- open --
core::Result<Manifest> ProjectStore::readManifest(const std::string& folder) {
    auto text = readText(fs::path(folder) / "project.xpgproj");
    if (!text) return core::fail(core::ErrorCode::FileNotFound,
                                 "not a project folder: no project.xpgproj", folder);
    Manifest m;
    eachLine(*text, [&](std::string_view line) {
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) return;
        auto key = line.substr(0, eq);
        auto val = line.substr(eq + 1);
        auto trim = [](std::string_view s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
            return std::string(s);
        };
        const auto k = trim(key), v = trim(val);
        if      (k == "formatVersion") m.formatVersion = v;
        else if (k == "name")          m.name = v;
        else if (k == "version")       m.version = v;
        else if (k == "state")         m.state = stateFromString(v);
        else if (k == "created")       m.created = v;
        else if (k == "modified")      m.modified = v;
        else if (k == "author")        m.author = v;
        else if (k == "cpu")           m.cpuReference = v;
        else if (k == "firmware")      m.cpuFirmware = v;
        else if (k == "comment")       m.comment = v;
        else if (k == "company")       m.company = v;
        else if (k == "product")       m.product = v;
        else if (k == "dtd")           m.dtdVersion = v;
        else if (k == "content")       m.contentKind = v;
        else if (k == "content-date")  m.contentDateTime = v;
        else if (k == "crlf")          m.crlf = v != "non" && v != "0";
    });
    return m;
}

bool ProjectStore::isProjectFolder(const std::string& folder) {
    std::error_code ec;
    return fs::exists(fs::path(folder) / "project.xpgproj", ec);
}

namespace {

LockRecord readLock(const fs::path& root) {
    LockRecord r;
    auto text = readText(root / "project.lock");
    if (!text) return r;
    eachLine(*text, [&](std::string_view line) {
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) return;
        auto trim = [](std::string_view s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
            return std::string(s);
        };
        const auto k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if      (k == "salt")       r.salt = v;
        else if (k == "password")   r.passwordHash = v;
        else if (k == "master")     r.masterHash = v;
        else if (k == "iterations") r.iterations = std::max(1, std::atoi(v.c_str()));
    });
    return r;
}

core::Status loadModel(const fs::path& root, Project& p) {
    // ---- tasks -------------------------------------------------------------
    if (auto text = readText(root / "config" / "tasks.txt"))
        eachLine(*text, [&](std::string_view line) {
            const auto f = splitFields(line);
            Task t;
            t.name     = p.strings.intern(field(f, 0));
            t.type     = field(f, 1);
            t.watchdog = static_cast<std::uint32_t>(std::atoi(field(f, 2).c_str()));
            t.period   = static_cast<std::uint32_t>(std::atoi(field(f, 3).c_str()));
            p.tasks.push_back(std::move(t));
        });

    // ---- lot API 5 : les bornes de lecture du plan memoire ------------------
    if (auto text = readText(root / "config" / "memoire.txt"))
        eachLine(*text, [&](std::string_view line) {
            const auto f = splitFields(line);
            const auto zone = field(f, 0);
            const int z = zone == "M" ? 0 : zone == "MW" ? 1 : zone == "KW" ? 2 : -1;
            if (z < 0 || f.size() < 3) return;
            auto& w = p.memoryWindows[static_cast<std::size_t>(z)];
            w.set = true;
            w.from = static_cast<std::uint32_t>(std::max(0, std::atoi(field(f, 1).c_str())));
            w.to = static_cast<std::uint32_t>(std::max(0, std::atoi(field(f, 2).c_str())));
            if (w.to < w.from) std::swap(w.from, w.to);
        });

    // ---- lot API 6 : l'icone du projet -----------------------------------------
    // Illisible : pas d'icone (le logo revient) plutot qu'un projet qui ne s'ouvre pas.
    if (auto text = readText(root / "config" / "icone.txt"))
        if (auto ic = icon::fromText(*text)) p.icon = std::move(*ic);

    // ---- 1.8.0 : les icones au choix -------------------------------------------
    // Une ligne illisible est sautee : une icone de moins, jamais un projet qui ne s'ouvre pas.
    if (auto text = readText(root / "config" / "icones-code.txt"))
        eachLine(*text, [&](std::string_view line) {
            if (line.empty() || line.front() == '#') return;
            const auto semi = line.rfind(';');
            if (semi == std::string_view::npos) return;
            const auto trim = [](std::string_view s) {
                while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
                while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
                return s;
            };
            const auto key = trim(line.substr(0, semi));
            const auto value = trim(line.substr(semi + 1));
            if (!key.empty() && !value.empty()) p.codeIcons[std::string(key)] = std::string(value);
        });

    // ---- hardware -----------------------------------------------------------
    if (auto text = readText(root / "config" / "hardware.txt")) {
        Rack* rack = nullptr;
        Module* module = nullptr;
        eachLine(*text, [&](std::string_view line) {
            const auto eq = line.find('=');
            if (eq != std::string_view::npos && line.find(';') == std::string_view::npos) {
                auto trim = [](std::string_view s) {
                    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
                    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
                    return std::string(s);
                };
                const auto k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
                if      (k == "cpu")           p.hardware.cpuReference = v;
                else if (k == "firmware")      p.hardware.cpuFirmware = v;
                else if (k == "family")        p.hardware.family = v;
                else if (k == "resource")      p.hardware.resourceName = v;
                else if (k == "bus")           p.hardware.busName = v;
                else if (k == "internalBits")  p.hardware.memory.internalBits = static_cast<std::uint32_t>(std::atoi(v.c_str()));
                else if (k == "internalWords") p.hardware.memory.internalWords = static_cast<std::uint32_t>(std::atoi(v.c_str()));
                else if (k == "constantWords") p.hardware.memory.constantWords = static_cast<std::uint32_t>(std::atoi(v.c_str()));
                else if (k == "autoRun")       p.hardware.memory.autoRun = v == "true";
                else if (k == "initialiseWords") p.hardware.memory.initialiseWords = v == "true";
                if (p.hardware.memory.internalWords) p.hardware.memory.declared = true;
                return;
            }
            const auto f = splitFields(line);
            if (f.empty()) return;

            if (f[0] == "rack") {
                Rack r;
                r.number             = static_cast<std::uint16_t>(std::atoi(field(f, 1).c_str()));
                r.reference          = field(f, 2);
                r.topologicalAddress = field(f, 3);
                p.hardware.racks.push_back(std::move(r));
                rack = &p.hardware.racks.back();
                module = nullptr;
            } else if (f[0] == "module" && rack) {
                Module m;
                m.rack               = static_cast<std::uint16_t>(std::atoi(field(f, 1).c_str()));
                m.slot               = static_cast<std::int16_t>(std::atoi(field(f, 2).c_str()));
                m.reference          = field(f, 3);
                m.family             = field(f, 4);
                m.firmware           = field(f, 5);
                m.kind               = static_cast<ModuleKind>(std::atoi(field(f, 6).c_str()));
                m.inputPoints        = static_cast<std::uint16_t>(std::atoi(field(f, 7).c_str()));
                m.outputPoints       = static_cast<std::uint16_t>(std::atoi(field(f, 8).c_str()));
                m.topologicalAddress = field(f, 9);
                m.nodeGuid           = field(f, 10);
                m.isCpu              = m.kind == ModuleKind::Cpu;
                rack->modules.push_back(std::move(m));
                module = &rack->modules.back();
            } else if (f[0] == "channel" && module) {
                Channel c;
                c.number          = static_cast<std::uint16_t>(std::atoi(field(f, 3).c_str()));
                c.direction       = static_cast<ChannelDirection>(std::atoi(field(f, 4).c_str()));
                c.role            = field(f, 5);
                c.task            = field(f, 6);
                c.functionCode    = static_cast<std::uint16_t>(std::atoi(field(f, 7).c_str()));
                c.functionVersion = static_cast<std::uint16_t>(std::atoi(field(f, 8).c_str()));
                c.iobFile         = field(f, 9);
                auto numbers = [](std::string_view s, std::vector<std::uint32_t>& out) {
                    std::size_t from = 0;
                    while (from < s.size()) {
                        const auto next = s.find(',', from);
                        const auto piece = s.substr(from, (next == std::string_view::npos ? s.size() : next) - from);
                        out.push_back(static_cast<std::uint32_t>(std::strtoul(std::string(piece).c_str(), nullptr, 10)));
                        if (next == std::string_view::npos) break;
                        from = next + 1;
                    }
                };
                numbers(field(f, 10), c.paramKW);
                numbers(field(f, 11), c.paramKPW);
                module->channels.push_back(std::move(c));
            }
        });
        if (!p.hardware.racks.empty()) p.hardware.inferred = false;
    }

    // ---- globals ------------------------------------------------------------
    if (auto text = readText(root / "vars" / "globals.txt"))
        eachLine(*text, [&](std::string_view line) {
            p.variables.push_back(decodeVariable(p, splitFields(line)));
        });

    // ---- derived types -------------------------------------------------------
    std::error_code ec;
    if (fs::exists(root / "ddt", ec)) {
        std::vector<fs::path> files;
        if (auto idx = readText(root / "ddt" / "index.txt"))
            eachLine(*idx, [&](std::string_view name) {
                files.push_back(root / "ddt" / (safeName(name) + ".ddt"));
            });
        if (files.empty()) {                            // no index: fall back
            for (const auto& e : fs::directory_iterator(root / "ddt", ec))
                if (e.is_regular_file() && e.path().extension() == ".ddt") files.push_back(e.path());
            std::sort(files.begin(), files.end());
        }

        for (const auto& file : files) {
            auto text = readText(file);
            if (!text) continue;
            DerivedType d;
            d.name = p.strings.intern(file.stem().string());
            eachLine(*text, [&](std::string_view line) {
                const auto eq = line.find('=');
                if (eq != std::string_view::npos && line.find(';') == std::string_view::npos) {
                    auto trim = [](std::string_view s) {
                        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
                        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
                        return std::string(s);
                    };
                    const auto k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
                    if      (k == "name")    d.name = p.strings.intern(v);
                    else if (k == "version") d.version = v;
                    return;
                }
                auto v = decodeVariable(p, splitFields(line));
                v.scope = VariableScope::DerivedMember;
                // Le type qui porte le champ : celui qu'on lit (il sera range a cet indice).
                // Sans lui, un champ ne savait plus de quel type il est : « utilise par »
                // (lot API 5), la suppression d'un type, le renommage d'un champ se trompaient.
                v.owner = static_cast<Index>(p.derivedTypes.size());
                p.variables.push_back(std::move(v));
                d.fields.push_back(static_cast<Index>(p.variables.size() - 1));
            });
            p.derivedTypes.push_back(std::move(d));
        }
    }

    // ---- POUs ---------------------------------------------------------------
    for (const char* area : {"dfb", "units"}) {
        const auto dir = root / area;
        if (!fs::exists(dir, ec)) continue;
        std::vector<fs::path> pous;
        if (auto idx = readText(dir / "index.txt"))
            eachLine(*idx, [&](std::string_view name) { pous.push_back(dir / safeName(name)); });
        if (pous.empty()) {
            for (const auto& e : fs::directory_iterator(dir, ec))
                if (e.is_directory()) pous.push_back(e.path());
            std::sort(pous.begin(), pous.end());
        }

        for (const auto& pouDir : pous) {
            auto text = readText(pouDir / "interface.txt");
            if (!text) continue;

            Pou pou;
            pou.kind = (std::string(area) == "dfb") ? PouKind::FunctionBlockType
                                                    : PouKind::ProgramUnit;
            pou.name = p.strings.intern(pouDir.filename().string());

            struct PendingSection { std::string name, language, activation, logic, file; std::uint32_t order; };
            std::vector<PendingSection> sections;
            std::vector<Variable>       declarations;

            eachLine(*text, [&](std::string_view line) {
                if (line.rfind("section ;", 0) == 0) {
                    const auto f = splitFields(line);
                    sections.push_back(PendingSection{
                        field(f, 1), field(f, 2), field(f, 4), field(f, 5), field(f, 6),
                        static_cast<std::uint32_t>(std::atoi(field(f, 3).c_str()))});
                    return;
                }
                const auto eq = line.find('=');
                if (eq != std::string_view::npos && line.find(';') == std::string_view::npos) {
                    auto trim = [](std::string_view s) {
                        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
                        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
                        return std::string(s);
                    };
                    const auto k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
                    if      (k == "name")    pou.name = p.strings.intern(v);
                    else if (k == "version") pou.version = v;
                    else if (k == "task")    pou.task = p.strings.intern(v);
                    else if (k == "order")   pou.order = static_cast<std::uint32_t>(std::atoi(v.c_str()));
                    else if (k == "attribute") {
                        const auto eq2 = v.find('=');
                        if (eq2 != std::string::npos)
                            pou.attributes.emplace_back(trim(v.substr(0, eq2)), trim(v.substr(eq2 + 1)));
                    }
                    return;
                }
                declarations.push_back(decodeVariable(p, splitFields(line)));
            });

            const auto pouIndex = static_cast<Index>(p.pous.size());
            for (auto& v : declarations) {
                v.owner = pouIndex;
                p.variables.push_back(std::move(v));
                const auto vi = static_cast<Index>(p.variables.size() - 1);
                const auto scope = p.variables[vi].scope;
                if (scope == VariableScope::Input || scope == VariableScope::Output
                    || scope == VariableScope::InOut)
                    pou.parameters.push_back(vi);
                else
                    pou.locals.push_back(vi);
            }

            p.pous.push_back(std::move(pou));
            for (const auto& s : sections) {
                Section sec;
                sec.name                = p.strings.intern(s.name);
                sec.language            = languageFromName(s.language);
                sec.order               = s.order;
                sec.activationCondition = p.strings.intern(s.activation);
                sec.logicCondition      = p.strings.intern(s.logic);
                sec.owner               = pouIndex;
                if (auto body = readText(pouDir / "code" / s.file)) sec.body = *body;
                sec.lineCount = sec.body.empty()
                    ? 0u : static_cast<std::uint32_t>(std::count(sec.body.begin(), sec.body.end(), '\n')) + 1;
                p.sections.push_back(std::move(sec));
                p.pous[pouIndex].sections.push_back(static_cast<Index>(p.sections.size() - 1));
            }
        }
    }

    // ---- animation tables -----------------------------------------------------
    if (auto text = readText(root / "tables" / "animation.txt"))
        eachLine(*text, [&](std::string_view line) {
            const auto f = splitFields(line);
            if (f.empty()) return;
            if (f[0] == "table") {
                AnimationTable t;
                t.name  = p.strings.intern(field(f, 1));
                t.owner = p.strings.intern(field(f, 2));
                p.animationTables.push_back(std::move(t));
            } else if ((f[0] == "entry" || f[0] == "ihm") && !p.animationTables.empty()) {
                p.animationTables.back().entries.push_back({p.strings.intern(field(f, 1)), f[0] == "ihm"});
            }
        });

    // ---- task sections --------------------------------------------------------
    if (auto text = readText(root / "sections" / "index.txt"))
        eachLine(*text, [&](std::string_view line) {
            const auto f = splitFields(line);
            if (f.size() < 7) return;
            Section s;
            s.name                = p.strings.intern(field(f, 0));
            s.task                = p.strings.intern(field(f, 1));
            s.language            = languageFromName(field(f, 2));
            s.order               = static_cast<std::uint32_t>(std::atoi(field(f, 3).c_str()));
            s.activationCondition = p.strings.intern(field(f, 4));
            s.logicCondition      = p.strings.intern(field(f, 5));
            if (auto body = readText(root / "sections" / field(f, 6))) s.body = *body;
            s.lineCount = s.body.empty()
                ? 0u : static_cast<std::uint32_t>(std::count(s.body.begin(), s.body.end(), '\n')) + 1;

            Pou own;
            own.name = s.name;
            own.kind = PouKind::Section;
            p.pous.push_back(std::move(own));
            s.owner = static_cast<Index>(p.pous.size() - 1);
            p.sections.push_back(std::move(s));
            p.pous.back().sections.push_back(static_cast<Index>(p.sections.size() - 1));
        });

    p.buildIndices();

    // Resolve type references, exactly as the XPG parser's link step does.
    for (auto& v : p.variables) {
        const auto elem = v.type.elementType ? v.type.elementType : v.type.name;
        if (auto it = p.typeByName.find(elem); it != p.typeByName.end()) {
            v.type.derivedIndex = it->second;
            // Un tableau de DDT reste un tableau (comme Project::linkTypes).
            if (v.type.klass == TypeClass::Unknown) v.type.klass = TypeClass::Derived;
            p.derivedTypes[it->second].instanceCount++;
        } else if (auto ip = p.pouByName.find(elem); ip != p.pouByName.end()
                   && p.pous[ip->second].kind == PouKind::FunctionBlockType) {
            v.type.fbTypeIndex = ip->second;
            v.type.klass = TypeClass::FunctionBlock;
            p.pous[ip->second].instanceCount++;
        }
    }
    for (auto& t : p.tasks) t.sections.clear();
    for (Index i = 0; i < p.sections.size(); ++i) {
        if (p.sections[i].task == 0) continue;
        for (auto& t : p.tasks)
            if (t.name == p.sections[i].task) { t.sections.push_back(i); break; }
    }
    for (auto& t : p.tasks)
        std::stable_sort(t.sections.begin(), t.sections.end(),
                         [&](Index a, Index b) { return p.sections[a].order < p.sections[b].order; });
    return core::ok();
}

} // namespace

core::Result<OpenResult> ProjectStore::open(const std::string& folder) {
    auto manifest = readManifest(folder);
    if (!manifest) return core::Err<core::Error>(manifest.error());

    OpenResult out;
    out.manifest = *manifest;
    out.lock     = readLock(fs::path(folder));

    if (out.manifest.state == State::Lock && out.lock.valid()) {
        out.needsPassword = true;       // no model until the caller authenticates
        return out;
    }

    out.project = std::make_shared<Project>();
    out.project->header.projectName    = out.manifest.name;
    out.project->header.projectVersion = out.manifest.version;
    out.project->header.sourceFile     = folder;
    out.project->header.company        = out.manifest.company;
    out.project->header.product        = out.manifest.product;
    out.project->header.dtdVersion     = out.manifest.dtdVersion;
    out.project->header.contentKind    = out.manifest.contentKind;
    out.project->header.contentDateTime = out.manifest.contentDateTime;
    out.project->header.crlf           = out.manifest.crlf;
    if (auto r = loadModel(fs::path(folder), *out.project); !r)
        return core::Err<core::Error>(r.error());
    return out;
}

core::Result<OpenResult> ProjectStore::open(const std::string& folder,
                                            std::string_view password,
                                            std::string_view masterKey) {
    auto first = open(folder);
    if (!first) return first;
    if (!first->needsPassword) return first;

    if (!checkPassword(first->lock, password) && !checkMaster(first->lock, masterKey))
        return core::fail(core::ErrorCode::Cancelled,
                          "the project is locked and the password does not match", folder);

    OpenResult out;
    out.manifest = first->manifest;
    out.lock     = first->lock;
    out.project  = std::make_shared<Project>();
    out.project->header.projectName    = out.manifest.name;
    out.project->header.projectVersion = out.manifest.version;
    out.project->header.sourceFile     = folder;
    out.project->header.company        = out.manifest.company;
    out.project->header.product        = out.manifest.product;
    out.project->header.dtdVersion     = out.manifest.dtdVersion;
    out.project->header.contentKind    = out.manifest.contentKind;
    out.project->header.contentDateTime = out.manifest.contentDateTime;
    out.project->header.crlf           = out.manifest.crlf;
    if (auto r = loadModel(fs::path(folder), *out.project); !r)
        return core::Err<core::Error>(r.error());
    return out;
}

// ---------------------------------------------------------------- duplicate --
core::Status ProjectStore::duplicate(const std::string& from, const std::string& to,
                                     std::string newName) {
    std::error_code ec;
    if (!isProjectFolder(from))
        return core::fail(core::ErrorCode::FileNotFound, "not a project folder", from);
    if (fs::exists(to, ec))
        return core::fail(core::ErrorCode::InvalidArgument, "destination already exists", to);

    fs::copy(from, to, fs::copy_options::recursive, ec);
    if (ec) return core::fail(core::ErrorCode::FileUnreadable, ec.message(), to);

    // A copy is a new project: it goes back to DEV, loses the lock, and drops
    // the generated sources. Carrying a LOCK across a copy would be a way to
    // launder one, and carrying stale src/ would be a way to ship the wrong file.
    fs::remove(fs::path(to) / "project.lock", ec);
    fs::remove_all(fs::path(to) / "src", ec);

    auto manifest = readManifest(to);
    if (!manifest) return core::Err<core::Error>(manifest.error());
    manifest->name     = newName.empty() ? manifest->name + " (copy)" : std::move(newName);
    manifest->state    = State::Dev;
    manifest->created  = nowStamp();
    manifest->modified = nowStamp();

    std::ostringstream m;
    m << "# XpgAnalyzer project. Safe to read; edit with care.\n";
    m << "formatVersion = " << manifest->formatVersion << '\n';
    m << "name = "     << manifest->name << '\n';
    m << "version = "  << manifest->version << '\n';
    m << "state = "    << toString(manifest->state) << '\n';
    m << "created = "  << manifest->created << '\n';
    m << "modified = " << manifest->modified << '\n';
    m << "author = "   << manifest->author << '\n';
    m << "cpu = "      << manifest->cpuReference << '\n';
    m << "firmware = " << manifest->cpuFirmware << '\n';
    m << "comment = "  << manifest->comment << '\n';
    return writeText(fs::path(to) / "project.xpgproj", m.str());
}

// ------------------------------------------------------------------ export --
core::Result<std::vector<std::string>> ProjectStore::exportSources(const Project& p,
                                                                   const std::string& folder) {
    std::vector<std::string> written;
    const auto src = fs::path(folder) / "src";

    const auto xpg = (src / "MAST.XPG").string();
    if (auto r = exporter::writeXpgFile(p, xpg); !r) return core::Err<core::Error>(r.error());
    written.push_back(xpg);

    if (!p.hardware.racks.empty()) {
        const auto xhw = (src / "CONFIG.XHW").string();
        if (auto r = exporter::writeXhwFile(p, xhw); !r) return core::Err<core::Error>(r.error());
        written.push_back(xhw);
    }
    return written;
}

} // namespace project
