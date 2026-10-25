// =============================================================================
//  hmi/HmiLibrary.cpp - 1.13.0 (lot J8) : les bibliotheques reliees (voir HmiLibrary.hpp)
// =============================================================================
#include "HmiLibrary.hpp"
#include "HmiExport.hpp"     // zipStored
#include "HmiNames.hpp"
#include "HmiScript.hpp"     // forEachCode, splitDeclarations
#include "HmiStore.hpp"      // serializeProject, parseProject, parseRecord, quote, nowStamp
#include "../core/Version.hpp"
#include "../../third_party/miniz.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>

namespace hmi::biblio {

namespace fs = std::filesystem;

namespace {

unsigned char uc(char c) { return static_cast<unsigned char>(c); }
bool identStart(char c) { return std::isalpha(uc(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(uc(c)) || c == '_'; }
std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(uc(c)));
    return out;
}
bool same(std::string_view a, std::string_view b) { return upperOf(a) == upperOf(b); }
std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(uc(s[a]))) ++a;
    while (b > a && std::isspace(uc(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// Un chemin en UTF-8 (comme le reste du programme).
fs::path pathOf(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
std::string utf8Of(const fs::path& p) {
    const auto s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

// L'espace d'un item publie, sous le nom local du lien : Gaz -> alias, Gaz.Ligne -> alias.Ligne, rien -> alias,
// Autre -> alias.Autre (un item hors de l'espace de tete reste sous la bibliotheque).
std::string remapNs(std::string_view ns, std::string_view published, std::string_view alias) {
    if (ns.empty() || same(ns, published)) return std::string(alias);
    if (ns.size() > published.size() && same(ns.substr(0, published.size()), published) && ns[published.size()] == '.')
        return std::string(alias) + std::string(ns.substr(published.size()));
    return std::string(alias) + "." + std::string(ns);
}

// Le nom relatif a la bibliotheque (T_Vanne, Ligne.T_X) d'un item d'espace `ns`.
std::string relativeName(std::string_view ns, std::string_view name, std::string_view lib) {
    if (same(ns, lib)) return std::string(name);
    if (ns.size() > lib.size() && same(ns.substr(0, lib.size()), lib) && ns[lib.size()] == '.')
        return std::string(ns.substr(lib.size() + 1)) + "." + std::string(name);
    return qualifiedName(ns, name);
}

// Les mots d'un texte de type qui nomment un type de la bibliotheque, ecrits en entier (vus de `own`).
std::string qualifyTypeText(const names::Index& ix, std::string_view text, std::string_view own) {
    if (text.empty() || ix.empty()) return std::string(text);
    names::Scope scope;
    scope.own = std::string(own);
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (!identStart(text[i]) || (i > 0 && (identChar(text[i - 1]) || text[i - 1] == '.'))) { out += text[i++]; continue; }
        std::size_t j = i;
        while (j < text.size() && identChar(text[j])) ++j;
        if (j < text.size() && text[j] == '.') {                    // deja un nom complet : tel quel
            while (j < text.size() && (identChar(text[j]) || text[j] == '.')) ++j;
            out.append(text.substr(i, j - i));
            i = j;
            continue;
        }
        const std::string word(text.substr(i, j - i));
        const auto r = names::resolveBare(ix, scope, word);
        out += r.full.empty() || r.kind != names::ItemKind::Type ? word : r.full;
        i = j;
    }
    return out;
}

// Le contenu d'une bibliotheque : chaque texte de type ecrit en entier, une fois les espaces remis.
void qualifyContent(Programs& c) {
    Project tmp;
    tmp.programs = c;
    const names::Index ix(tmp);
    if (ix.empty()) return;
    const auto fix = [&](std::string& t, const std::string& own) { t = qualifyTypeText(ix, t, own); };
    for (auto& t : c.types) {
        fix(t.base, t.ns);
        for (auto& i : t.interfaces) fix(i, t.ns);
        for (auto& m : t.members) fix(m.type, t.ns);
        for (auto& m : t.methods) {
            fix(m.returnType, t.ns);
            for (auto& d : m.decls) fix(d.type, t.ns);
        }
        for (auto& o : t.operators) {
            fix(o.left, t.ns);
            fix(o.right, t.ns);
            fix(o.result, t.ns);
        }
    }
    for (auto& f : c.functions) {
        fix(f.returnType, f.ns);
        for (auto& d : f.decls) fix(d.type, f.ns);
    }
    for (auto& s : c.scripts)
        for (auto& d : s.decls) fix(d.type, s.ns);
}

Id libraryId(std::size_t index, Id id) {
    return kLibraryIdBase + static_cast<Id>((index & 0x3FFu) << 20) + (id & 0xFFFFFu);
}

// Un membre du cache : "Position:REAL" ; une enumeration : "Auto=1".
std::vector<std::string> splitList(std::string_view s, char sep) {
    std::vector<std::string> out;
    int depth = 0;
    std::string cur;
    for (const char c : s) {
        if (c == '(' || c == '[' || c == '<') ++depth;
        else if (c == ')' || c == ']' || c == '>') --depth;
        if (c == sep && depth == 0) {
            if (!trimmed(cur).empty()) out.push_back(trimmed(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!trimmed(cur).empty()) out.push_back(trimmed(cur));
    return out;
}

std::mutex& foldersLock() {
    static std::mutex m;
    return m;
}
std::vector<std::string>& foldersSlot() {
    static std::vector<std::string> v;
    return v;
}

void readZip(const Bytes& bytes, std::map<std::string, std::string>& files, bool& zip) {
    mz_zip_archive archive;
    std::memset(&archive, 0, sizeof archive);
    zip = !bytes.empty() && mz_zip_reader_init_mem(&archive, bytes.data(), bytes.size(), 0);
    if (!zip) return;
    const mz_uint n = mz_zip_reader_get_num_files(&archive);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&archive, i, &st) || st.m_is_directory) continue;
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&archive, i, &size, 0);
        if (!data) continue;
        std::string name = st.m_filename;
        for (auto& ch : name) if (ch == '\\') ch = '/';
        files[name].assign(static_cast<const char*>(data), size);
        mz_free(data);
    }
    mz_zip_reader_end(&archive);
}

} // namespace

// --------------------------------------------------------------------- versions ---
std::string Version::text() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

Version parseVersion(std::string_view s) {
    Version v;
    std::string t = trimmed(s);
    if (!t.empty() && (t[0] == 'v' || t[0] == 'V')) t.erase(t.begin());
    int parts[3] = {0, 0, 0};
    std::size_t k = 0, i = 0;
    while (k < 3) {
        if (i >= t.size() || !std::isdigit(uc(t[i]))) return v;
        int n = 0;
        while (i < t.size() && std::isdigit(uc(t[i]))) {
            n = n * 10 + (t[i] - '0');
            if (n > 99999) return v;
            ++i;
        }
        parts[k++] = n;
        if (i >= t.size()) break;
        if (t[i] != '.') return v;
        ++i;
    }
    if (i < t.size()) return v;
    v.major = parts[0];
    v.minor = parts[1];
    v.patch = parts[2];
    v.ok = true;
    return v;
}

int compare(const Version& a, const Version& b) noexcept {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

LibraryState stateOf(std::string_view found, std::string_view recorded, std::string_view floor) {
    const Version f = parseVersion(found);
    if (!f.ok) return LibraryState::Unreadable;
    const Version r = parseVersion(recorded);
    const Version fl = parseVersion(floor.empty() ? recorded : floor);
    if (fl.ok && f.major != fl.major) return LibraryState::Incompatible;
    if (fl.ok && compare(f, fl) < 0) return LibraryState::Older;
    if (r.ok && compare(f, r) > 0) return LibraryState::Newer;
    return LibraryState::Linked;
}

std::string_view stateLabel(LibraryState s) noexcept {
    switch (s) {
        case LibraryState::Linked: return "reli\xC3\xA9" "e";
        case LibraryState::Newer: return "plus r\xC3\xA9" "cente dispo";
        case LibraryState::Older: return "plus ancienne";
        case LibraryState::Incompatible: return "incompatible";
        case LibraryState::Missing: return "introuvable";
        case LibraryState::Unreadable: return "illisible";
    }
    return "?";
}

std::string bumped(std::string_view version, Bump b) {
    Version v = parseVersion(version);
    if (!v.ok) v = Version{1, 0, 0, true};
    switch (b) {
        case Bump::Major: return Version{v.major + 1, 0, 0, true}.text();
        case Bump::Minor: return Version{v.major, v.minor + 1, 0, true}.text();
        case Bump::Patch: return Version{v.major, v.minor, v.patch + 1, true}.text();
    }
    return v.text();
}

// -------------------------------------------------------------------- le fichier ---
Bytes toZip(const Package& pkg) {
    const auto& m = pkg.manifest;
    std::string text = "# IHM Studio - biblioth\xC3\xA8que (format " + std::to_string(kLibraryFormat) + ")\n";
    text += "bibliotheque format=" + std::to_string(kLibraryFormat) + " outil=" + quote(XPG_ANALYZER_VERSION) + " nom=" + quote(m.name)
          + " version=" + quote(m.version) + " description=" + quote(m.description) + " projet=" + quote(m.fromProject)
          + " cree=" + quote(m.created) + "\n";
    for (const auto& [name, floor] : m.dependencies) text += "dependance nom=" + quote(name) + " compat=" + quote(floor) + "\n";
    text += "fin\n";
    std::vector<std::pair<std::string, std::string>> items;
    items.emplace_back(std::string(kManifestFile), text);
    for (const auto& f : serializeProject(pkg.content))
        if (f.data) items.emplace_back("ihm/" + f.path, std::string(f.data->begin(), f.data->end()));
    return zipStored(items);
}

core::Result<Package> fromZip(const Bytes& bytes) {
    std::map<std::string, std::string> files;
    bool zip = false;
    readZip(bytes, files, zip);
    if (!zip) return core::fail(core::ErrorCode::FileUnreadable, "pas une biblioth\xC3\xA8que (le fichier n'est pas un zip)");
    const auto man = files.find(std::string(kManifestFile));
    if (man == files.end())
        return core::fail(core::ErrorCode::FileUnreadable, "pas une biblioth\xC3\xA8que (bibliotheque.txt absent) - un paquet .xpgtypes s'importe, il ne se relie pas");
    Package pkg;
    std::size_t pos = 0;
    const std::string& m = man->second;
    while (pos < m.size()) {
        const std::size_t eol = m.find('\n', pos);
        std::string_view line(m.data() + pos, (eol == std::string::npos ? m.size() : eol) - pos);
        pos = eol == std::string::npos ? m.size() : eol + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line[0] == '#') continue;
        Record r;
        std::string error;
        if (!parseRecord(line, r, error)) return core::fail(core::ErrorCode::XmlMalformed, "bibliotheque.txt : " + error);
        const auto get = [&](const char* key) { const auto* v = r.get(key); return v ? *v : std::string{}; };
        if (r.word == "bibliotheque") {
            const std::string f = get("format");
            pkg.manifest.format = f.empty() ? 1 : std::atoi(f.c_str());
            pkg.manifest.writer = get("outil");
            pkg.manifest.name = get("nom");
            pkg.manifest.version = get("version");
            pkg.manifest.description = get("description");
            pkg.manifest.fromProject = get("projet");
            pkg.manifest.created = get("cree");
        } else if (r.word == "dependance") {
            pkg.manifest.dependencies.emplace_back(get("nom"), get("compat"));
        }
    }
    // Une bibliotheque d'une version plus recente de l'outil : refusee en le disant (comme un paquet).
    const auto newer = [&](const std::string& what) {
        return core::fail(core::ErrorCode::XmlUnsupportedDtd,
                          "cette biblioth\xC3\xA8que vient d'une version plus r\xC3\xA9" "cente d'IHM Studio"
                              + (pkg.manifest.writer.empty() ? std::string{} : " (" + pkg.manifest.writer + ")") + " : cette version ("
                              + std::string(XPG_ANALYZER_VERSION) + ") ne sait pas la lire (" + what + ")");
    };
    if (pkg.manifest.format > kLibraryFormat)
        return newer("biblioth\xC3\xA8que au format " + std::to_string(pkg.manifest.format) + ", elle lit jusqu'au format "
                     + std::to_string(kLibraryFormat));
    if (pkg.manifest.name.empty() || !isNamespaceName(pkg.manifest.name))
        return core::fail(core::ErrorCode::XmlMalformed, "biblioth\xC3\xA8que sans nom lisible (bibliotheque.txt : nom=)");
    if (!parseVersion(pkg.manifest.version).ok)
        return core::fail(core::ErrorCode::XmlMalformed, "biblioth\xC3\xA8que " + pkg.manifest.name + " : version illisible \xC2\xAB "
                                                             + pkg.manifest.version + " \xC2\xBB (1.4.0)");
    const auto read = [&files](const std::string& path, std::string& content) {
        const auto it = files.find("ihm/" + path);
        if (it == files.end()) return false;
        content = it->second;
        return true;
    };
    auto project = parseProject(read);
    if (!project && project.error().code == core::ErrorCode::XmlUnsupportedDtd) return newer(project.error().context);
    if (!project) return core::fail(project.error().code, "biblioth\xC3\xA8que : " + project.error().context);
    pkg.content = std::move(*project);
    return pkg;
}

core::Status writeFile(const Package& pkg, const std::string& path) {
    const Bytes data = toZip(pkg);
    std::error_code ec;
    if (const auto parent = pathOf(path).parent_path(); !parent.empty()) fs::create_directories(parent, ec);
    std::ofstream out(pathOf(path), std::ios::binary | std::ios::trunc);
    if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture impossible : " + path);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    out.flush();
    if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture incompl\xC3\xA8" "te : " + path);
    return core::ok();
}

core::Result<Package> readFile(const std::string& path) {
    std::ifstream in(pathOf(path), std::ios::binary);
    if (!in) return core::fail(core::ErrorCode::FileNotFound, "fichier introuvable : " + path);
    const Bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return fromZip(data);
}

std::string contentsText(const Project& c) {
    std::vector<std::string> parts;
    if (!c.programs.types.empty()) parts.push_back(plural(c.programs.types.size(), "type", "types"));
    if (!c.programs.functions.empty()) parts.push_back(plural(c.programs.functions.size(), "fonction", "fonctions"));
    if (!c.programs.scripts.empty()) parts.push_back(plural(c.programs.scripts.size(), "script", "scripts"));
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i ? ", " : "") + parts[i];
    return out.empty() ? std::string("vide") : out;
}

// ------------------------------------------------------------------------ publier ---
PublishPlan collect(const Project& p, const PublishSpec& spec) {
    PublishPlan plan;
    auto& m = plan.package.manifest;
    m.name = trimmed(spec.name);
    m.version = trimmed(spec.version);
    m.description = spec.description;
    m.fromProject = p.config.name;
    m.created = nowStamp();
    m.writer = XPG_ANALYZER_VERSION;
    m.dependencies = spec.dependencies;
    if (!isNamespaceName(m.name)) {
        plan.refusal = "donne un nom de biblioth\xC3\xA8que lisible (Gaz, Util) : ce sera son espace de noms";
        return plan;
    }
    if (!parseVersion(m.version).ok) {
        plan.refusal = "version illisible \xC2\xAB " + m.version + " \xC2\xBB : majeur.mineur.correctif (1.4.0)";
        return plan;
    }
    std::vector<std::string> spaces = spec.spaces;
    if (spaces.empty()) spaces.push_back(m.name);
    const auto inSpaces = [&](std::string_view ns) {
        for (const auto& s : spaces)
            if (same(ns, s) || (ns.size() > s.size() && same(ns.substr(0, s.size()), s) && ns[s.size()] == '.')) return true;
        return false;
    };
    const auto chosen = [&](Id id) { return std::find(spec.items.begin(), spec.items.end(), id) != spec.items.end(); };
    Project& c = plan.package.content;
    c.config.name = m.name;
    c.config.unitFamilies = p.config.unitFamilies;     // 1.13.0 (lot J6) : les unites du projet voyagent avec ses types
    c.config.units = p.config.units;
    for (const auto& t : p.programs.types) if ((!t.ns.empty() && inSpaces(t.ns)) || chosen(t.id)) c.programs.types.push_back(t);
    for (const auto& f : p.programs.functions) if ((!f.ns.empty() && inSpaces(f.ns)) || chosen(f.id)) c.programs.functions.push_back(f);
    for (const auto& s : p.programs.scripts) if ((!s.ns.empty() && inSpaces(s.ns)) || chosen(s.id)) c.programs.scripts.push_back(s);
    // Un item choisi hors de l'espace de tete passe dessous (sa place dans la bibliotheque).
    const auto under = [&](std::string& ns) { if (ns.empty()) ns = m.name; };
    for (auto& t : c.programs.types) under(t.ns);
    for (auto& f : c.programs.functions) under(f.ns);
    for (auto& s : c.programs.scripts) under(s.ns);
    // Les dossiers d'un item ne voyagent pas (ils rangent le projet d'origine).
    for (auto& t : c.programs.types) t.folder.clear();
    for (auto& s : c.programs.scripts) s.folder.clear();
    if (c.programs.types.empty() && c.programs.functions.empty() && c.programs.scripts.empty()) {
        plan.refusal = "rien \xC3\xA0 publier : l'espace " + m.name + " n'a ni type, ni fonction, ni script (choisis des \xC3\xA9l\xC3\xA9ments)";
        return plan;
    }
    Id highest = 0;
    for (const auto& t : c.programs.types) highest = std::max(highest, t.id);
    for (const auto& f : c.programs.functions) highest = std::max(highest, f.id);
    for (const auto& s : c.programs.scripts) highest = std::max(highest, s.id);
    c.nextId = highest + 1;
    // Ce qui manquerait : ce que le code emploie et qui ne part pas - une variable IHM, un item de la racine.
    std::set<std::string> said;
    const names::Index all(p);
    const auto note = [&](const std::string& w) { if (said.insert(upperOf(w)).second) plan.missing.push_back(w); };
    const auto look = [&](std::string_view code) {
        std::size_t i = 0;
        while (i < code.size()) {
            if (code[i] == '\'' || code[i] == '"') {
                const char q = code[i];
                std::size_t j = i + 1;
                while (j < code.size() && code[j] != q) j += code[j] == '$' ? 2 : 1;
                i = j + 1;
                continue;
            }
            if (code[i] == '(' && i + 1 < code.size() && code[i + 1] == '*') {
                const auto e = code.find("*)", i + 2);
                i = e == std::string_view::npos ? code.size() : e + 2;
                continue;
            }
            if (code[i] == '/' && i + 1 < code.size() && code[i + 1] == '/') {
                const auto e = code.find('\n', i);
                i = e == std::string_view::npos ? code.size() : e;
                continue;
            }
            if (!identStart(code[i]) || (i > 0 && (identChar(code[i - 1]) || code[i - 1] == '.' || code[i - 1] == '#'))) { ++i; continue; }
            std::size_t j = i;
            while (j < code.size() && identChar(code[j])) ++j;
            const std::string w(code.substr(i, j - i));
            if (const Variable* v = p.variable(w)) note("la variable IHM " + v->name + " (une variable ne voyage pas : passe-la en param\xC3\xA8tre)");
            else if (const HmiFunction* f = p.functionByName(w); f && f->ns.empty() && !inSpaces(f->ns) && !chosen(f->id))
                note("la fonction " + f->name + " (de la racine : mets-la dans l'espace " + m.name + " ou choisis-la)");
            else if (const HmiType* t = p.hmiTypeByName(w); t && t->ns.empty() && !chosen(t->id) && !isLibraryId(t->id))
                note("le type " + t->name + " (de la racine : mets-le dans l'espace " + m.name + " ou choisis-le)");
            i = j;
        }
    };
    for (const auto& t : c.programs.types) {
        for (const auto& mm : t.members) look(mm.type);
        for (const auto& mt : t.methods) look(mt.body);
        look(t.base);
    }
    for (const auto& f : c.programs.functions) {
        look(f.body);
        look(f.returnType);
        for (const auto& d : f.decls) look(d.type);
    }
    for (const auto& s : c.programs.scripts) look(s.body);
    // Un espace qui vient d'une bibliotheque reliee : une dependance (elle doit etre reliee aussi).
    for (const auto& lib : p.libraries) {
        bool uses = false;
        const auto mentions = [&](std::string_view code) {
            const std::string u = upperOf(code), key = upperOf(lib.name) + ".";
            return u.find(key) != std::string::npos;
        };
        for (const auto& f : c.programs.functions) uses = uses || mentions(f.body);
        for (const auto& s : c.programs.scripts) uses = uses || mentions(s.body);
        for (const auto& t : c.programs.types) for (const auto& mm : t.members) uses = uses || mentions(mm.type);
        if (uses && std::none_of(m.dependencies.begin(), m.dependencies.end(), [&](const auto& d) { return same(d.first, lib.name); }))
            m.dependencies.emplace_back(lib.publishedName(), lib.compat.empty() ? lib.version : lib.compat);
    }
    return plan;
}

// ------------------------------------------------------------------- l'interface ---
std::string signatureOf(const HmiFunction& f) {
    std::string s;
    if (!f.generics.empty()) s += "<" + genericsText(f.generics) + ">";
    s += "(";
    bool first = true;
    for (const auto& d : f.decls) {
        if (d.kind != DeclKind::Parameter) continue;
        s += first ? "" : "; ";
        if (d.mode == PassMode::InOut) s += "VAR_IN_OUT ";
        else if (d.mode == PassMode::Out) s += "VAR_OUTPUT ";
        s += d.name + " : " + d.type;
        first = false;
    }
    // Les parametres ecrits dans le corps (VAR_INPUT ... END_VAR) : comme le moteur les lit.
    if (first) {
        const auto parts = splitDeclarations(f.body, true, [](std::string_view) { return true; });
        for (const auto* in : parts.parameters()) {
            s += first ? "" : "; ";
            if (in->section == LocalVar::Section::InOut) s += "VAR_IN_OUT ";
            else if (in->section == LocalVar::Section::Output) s += "VAR_OUTPUT ";
            s += in->name + " : " + in->type;
            first = false;
        }
    }
    s += ")";
    if (!f.returnType.empty()) s += " : " + f.returnType;
    return s;
}

void interfaceOf(const Project& content, std::string_view lib, std::vector<LibraryTypeSig>& types,
                 std::vector<LibraryFunctionSig>& functions, std::vector<std::string>& scripts) {
    types.clear();
    functions.clear();
    scripts.clear();
    for (const auto& t : content.programs.types) {
        LibraryTypeSig sig;
        sig.name = relativeName(t.ns, t.name, lib);
        switch (t.kind) {
            case HmiTypeKind::Enumeration:
                sig.kind = "enumeration";
                for (const auto& v : t.values) sig.members += (sig.members.empty() ? "" : ",") + v.name + "=" + std::to_string(v.value);
                break;
            case HmiTypeKind::Interface:
                sig.kind = "interface";
                for (const auto& m : t.methods) sig.members += (sig.members.empty() ? "" : ",") + m.name + signatureOf(m);
                break;
            case HmiTypeKind::Callback:
                sig.kind = "rappel";
                sig.members = t.signature;
                break;
            case HmiTypeKind::Structure:
                for (const auto& m : t.members)
                    if (m.visibility == Visibility::Public) sig.members += (sig.members.empty() ? "" : ",") + m.name + ":" + m.type;
                break;
        }
        types.push_back(std::move(sig));
    }
    for (const auto& f : content.programs.functions) functions.push_back({relativeName(f.ns, f.name, lib), signatureOf(f)});
    for (const auto& s : content.programs.scripts) scripts.push_back(relativeName(s.ns, s.name, lib));
}

// ---------------------------------------------------------------------- relier ---
void setSharedFolders(std::vector<std::string> folders) {
    const std::lock_guard<std::mutex> hold(foldersLock());
    foldersSlot() = std::move(folders);
}

std::vector<std::string> sharedFolders() {
    std::vector<std::string> out;
    {
        const std::lock_guard<std::mutex> hold(foldersLock());
        out = foldersSlot();
    }
    if (const char* env = std::getenv("XPG_BIBLIOTHEQUES_IHM"); env && *env) out.emplace_back(env);
    return out;
}

std::string findFile(const Library& lib, const std::string& projectFolder) {
    if (lib.path.empty()) return {};
    std::error_code ec;
    const fs::path given = pathOf(lib.path);
    if (given.is_relative() && !projectFolder.empty()) {
        const fs::path in = pathOf(projectFolder) / given;
        if (fs::is_regular_file(in, ec)) return utf8Of(fs::absolute(in, ec).lexically_normal());
    }
    for (const auto& folder : sharedFolders()) {
        for (const fs::path& candidate : {pathOf(folder) / given.filename(), pathOf(folder) / given})
            if (fs::is_regular_file(candidate, ec)) return utf8Of(fs::absolute(candidate, ec).lexically_normal());
    }
    if (given.is_absolute() && fs::is_regular_file(given, ec)) return utf8Of(given.lexically_normal());
    return {};
}

std::string pathToRecord(const std::string& file, const std::string& projectFolder) {
    std::error_code ec;
    const fs::path f = fs::absolute(pathOf(file), ec).lexically_normal();
    if (!projectFolder.empty()) {
        const fs::path root = fs::absolute(pathOf(projectFolder), ec).lexically_normal();
        const fs::path rel = f.lexically_relative(root);
        if (!rel.empty() && rel.native().find(fs::path("..").native()) != 0 && !rel.is_absolute()) {
            const std::string r = utf8Of(rel);
            if (r.rfind("..", 0) != 0) return r;
        }
    }
    return utf8Of(f);
}

LoadedLibrary loadContent(const Library& link, const Package& pkg, std::size_t index) {
    LoadedLibrary out;
    out.name = link.name;
    out.fromCache = false;
    out.foundVersion = pkg.manifest.version;
    const std::string published = pkg.manifest.name.empty() ? link.publishedName() : pkg.manifest.name;
    const std::string& alias = link.name;
    const bool renamed = !same(published, alias);
    Programs& c = out.content;
    c.types = pkg.content.programs.types;
    c.functions = pkg.content.programs.functions;
    c.scripts = pkg.content.programs.scripts;
    const auto code = [&](std::string& t) { if (renamed) t = names::renameInCode(t, published, alias); };
    const auto type = [&](std::string& t) { if (renamed) t = names::renameInType(t, published, alias); };
    for (auto& t : c.types) {
        t.ns = remapNs(t.ns, published, alias);
        t.id = libraryId(index, t.id);
        t.folder.clear();
        type(t.base);
        for (auto& i : t.interfaces) type(i);
        for (auto& m : t.members) type(m.type);
        for (auto& m : t.methods) {
            m.id = libraryId(index, m.id);
            code(m.body);
            type(m.returnType);
            for (auto& d : m.decls) type(d.type);
        }
        for (auto& o : t.operators) {
            o.id = libraryId(index, o.id);
            code(o.body);
        }
    }
    for (auto& f : c.functions) {
        f.ns = remapNs(f.ns, published, alias);
        f.id = libraryId(index, f.id);
        code(f.body);
        type(f.returnType);
        for (auto& d : f.decls) type(d.type);
        f.tests.clear();                        // ses tests restent a la bibliotheque (ils se jouent la-bas)
    }
    for (auto& s : c.scripts) {
        s.ns = remapNs(s.ns, published, alias);
        s.id = libraryId(index, s.id);
        s.folder.clear();
        code(s.body);
        code(s.watch);
    }
    qualifyContent(c);
    return out;
}

LoadedLibrary fromCache(const Library& link, std::size_t index) {
    LoadedLibrary out;
    out.name = link.name;
    out.fromCache = true;
    out.foundVersion.clear();
    Id next = 1;
    const auto place = [&](std::string_view relative, std::string& ns, std::string& name) {
        const auto [sub, last] = splitQualified(relative);
        ns = sub.empty() ? link.name : link.name + "." + sub;
        name = last;
    };
    for (const auto& sig : link.types) {
        HmiType t;
        place(sig.name, t.ns, t.name);
        t.id = libraryId(index, next++);
        if (sig.kind == "enumeration") {
            t.kind = HmiTypeKind::Enumeration;
            for (const auto& v : splitList(sig.members, ',')) {
                HmiEnumValue ev;
                const auto eq = v.find('=');
                ev.name = trimmed(v.substr(0, eq));
                if (eq != std::string::npos) ev.value = std::atoll(v.c_str() + eq + 1);
                t.values.push_back(std::move(ev));
            }
        } else if (sig.kind == "rappel") {
            t.kind = HmiTypeKind::Callback;
            t.signature = sig.members;
        } else if (sig.kind == "interface") {
            t.kind = HmiTypeKind::Interface;
        } else {
            for (const auto& m : splitList(sig.members, ',')) {
                TypeMember tm;
                const auto colon = m.find(':');
                tm.name = trimmed(m.substr(0, colon));
                tm.type = colon == std::string::npos ? std::string("INT") : trimmed(m.substr(colon + 1));
                t.members.push_back(std::move(tm));
            }
        }
        out.content.types.push_back(std::move(t));
    }
    for (const auto& sig : link.functions) {
        HmiFunction f;
        place(sig.name, f.ns, f.name);
        f.id = libraryId(index, next++);
        std::string s = trimmed(sig.signature);
        if (!s.empty() && s.front() == '<') {                       // <T : ANY_NUM>(a : T) : T
            const auto close = s.find('>');
            std::vector<GenericParam> g;
            if (close != std::string::npos && parseGenerics(s.substr(1, close - 1), g)) f.generics = std::move(g);
            s = trimmed(close == std::string::npos ? std::string_view{} : std::string_view(s).substr(close + 1));
        }
        const auto open = s.find('('), close = s.rfind(')');
        if (open != std::string::npos && close != std::string::npos && close > open) {
            for (const auto& prm : splitList(s.substr(open + 1, close - open - 1), ';')) {
                Declaration d;
                d.kind = DeclKind::Parameter;
                std::string text = prm;
                if (upperOf(text).rfind("VAR_IN_OUT ", 0) == 0) { d.mode = PassMode::InOut; text = trimmed(text.substr(11)); }
                else if (upperOf(text).rfind("VAR_OUTPUT ", 0) == 0) { d.mode = PassMode::Out; text = trimmed(text.substr(11)); }
                const auto colon = text.find(':');
                d.name = trimmed(text.substr(0, colon));
                d.type = colon == std::string::npos ? std::string("INT") : trimmed(text.substr(colon + 1));
                d.id = next++;
                d.visibility = Visibility::Public;
                if (!d.name.empty()) f.decls.push_back(std::move(d));
            }
            const auto colon = s.find(':', close);
            if (colon != std::string::npos) f.returnType = trimmed(s.substr(colon + 1));
        }
        f.description = "biblioth\xC3\xA8que " + link.name + " (interface seule : le fichier n'est pas charg\xC3\xA9)";
        out.content.functions.push_back(std::move(f));
    }
    for (const auto& name : link.scripts) {
        Script sc;
        place(name, sc.ns, sc.name);
        sc.id = libraryId(index, next++);
        sc.event = "Appel";
        out.content.scripts.push_back(std::move(sc));
    }
    qualifyContent(out.content);
    return out;
}

void useCache(Project& p) {
    if (p.libraries.empty()) {
        p.libraryCache.loaded.reset();
        return;
    }
    auto loaded = std::make_shared<std::vector<LoadedLibrary>>();
    for (std::size_t i = 0; i < p.libraries.size(); ++i) {
        LoadedLibrary l = fromCache(p.libraries[i], i);
        l.state = LibraryState::Missing;
        l.message = "pas encore cherch\xC3\xA9" "e sur le disque (" + p.libraries[i].path + ")";
        loaded->push_back(std::move(l));
    }
    p.libraryCache.loaded = std::move(loaded);
}

void resolve(Project& p, const std::string& projectFolder) {
    if (p.libraries.empty()) {
        p.libraryCache.loaded.reset();
        return;
    }
    auto loaded = std::make_shared<std::vector<LoadedLibrary>>();
    for (std::size_t i = 0; i < p.libraries.size(); ++i) {
        const Library& link = p.libraries[i];
        const std::string file = findFile(link, projectFolder);
        const std::string floor = link.compat.empty() ? link.version : link.compat;
        LoadedLibrary l;
        if (file.empty()) {
            l = fromCache(link, i);
            l.state = LibraryState::Missing;
            l.message = link.name + " introuvable (" + link.path + ") : localise-la (le projet garde son interface pour v\xC3\xA9rifier)";
            loaded->push_back(std::move(l));
            continue;
        }
        auto pkg = readFile(file);
        if (!pkg) {
            l = fromCache(link, i);
            l.state = LibraryState::Unreadable;
            l.foundPath = file;
            l.message = link.name + " illisible : " + (pkg.error().context.empty() ? pkg.error().message() : pkg.error().context);
            loaded->push_back(std::move(l));
            continue;
        }
        const LibraryState st = stateOf(pkg->manifest.version, link.version, floor);
        const Version f = parseVersion(pkg->manifest.version), fl = parseVersion(floor);
        if (st == LibraryState::Incompatible || st == LibraryState::Unreadable) {
            l = fromCache(link, i);
            l.state = st;
            l.foundPath = file;
            l.foundVersion = pkg->manifest.version;
            l.message = st == LibraryState::Incompatible
                            ? link.name + " incompatible (" + pkg->manifest.version + ", le projet demande la " + std::to_string(fl.major) + ".x)"
                            : link.name + " : version illisible (" + pkg->manifest.version + ")";
            loaded->push_back(std::move(l));
            continue;
        }
        l = loadContent(link, *pkg, i);
        l.state = st;
        l.foundPath = file;
        l.foundVersion = pkg->manifest.version;
        if (st == LibraryState::Newer)
            l.message = link.name + " : " + link.version + " \xE2\x86\x92 " + pkg->manifest.version + " (compatible) - Mettre \xC3\xA0 jour fige la "
                      + pkg->manifest.version;
        else if (st == LibraryState::Older)
            l.message = link.name + " " + pkg->manifest.version + " est plus ancienne que demand\xC3\xA9" "e (" + floor
                      + ") : un symbole r\xC3\xA9" "cent manquera";
        else
            l.message = link.name + " " + pkg->manifest.version + " reli\xC3\xA9" "e";
        (void)f;
        loaded->push_back(std::move(l));
    }
    p.libraryCache.loaded = std::move(loaded);
}

LinkPlan planLink(const Project& p, const std::string& file, const std::string& projectFolder, std::string_view alias) {
    LinkPlan plan;
    std::error_code ec;
    plan.file = utf8Of(fs::absolute(pathOf(file), ec).lexically_normal());
    auto pkg = readFile(file);
    if (!pkg) {
        plan.refusal = pkg.error().context.empty() ? pkg.error().message() : pkg.error().context;
        return plan;
    }
    plan.package = std::move(*pkg);
    const std::string published = plan.package.manifest.name;
    const std::string name = trimmed(alias).empty() ? published : trimmed(alias);
    if (!isNamespaceName(name)) {
        plan.refusal = "\xC2\xAB " + name + " \xC2\xBB n'est pas un nom d'espace lisible (Gaz, Util)";
        return plan;
    }
    if (p.libraryByName(name)) {
        plan.refusal = "une biblioth\xC3\xA8que " + name + " est d\xC3\xA9j\xC3\xA0 reli\xC3\xA9" "e : relie celle-ci sous un autre nom (Relier sous le nom...)";
        return plan;
    }
    // Un espace du projet du meme nom : ses noms se meleraient a ceux de la bibliotheque.
    const auto projectSpace = [&](std::string_view ns) {
        return !ns.empty() && (same(ns, name) || (ns.size() > name.size() && same(ns.substr(0, name.size()), name) && ns[name.size()] == '.'));
    };
    const bool clash = std::any_of(p.programs.types.begin(), p.programs.types.end(), [&](const HmiType& t) { return projectSpace(t.ns); })
                    || std::any_of(p.programs.functions.begin(), p.programs.functions.end(), [&](const HmiFunction& f) { return projectSpace(f.ns); })
                    || std::any_of(p.programs.scripts.begin(), p.programs.scripts.end(), [&](const Script& s) { return projectSpace(s.ns); });
    if (clash) {
        plan.refusal = "l'espace " + name + " existe d\xC3\xA9j\xC3\xA0 dans le projet : relie la biblioth\xC3\xA8que sous un autre nom (Relier sous le nom...)";
        return plan;
    }
    if (const std::string r = names::reservedClash(p, name); !r.empty()) {
        plan.refusal = r + " : relie la biblioth\xC3\xA8que sous un autre nom";
        return plan;
    }
    Library& l = plan.link;
    l.name = name;
    if (!same(name, published)) l.published = published;
    l.version = plan.package.manifest.version;
    l.compat = l.version;
    l.path = pathToRecord(plan.file, projectFolder);
    const LoadedLibrary content = loadContent(l, plan.package, p.libraries.size());
    Project shown;
    shown.programs = content.content;
    interfaceOf(shown, name, l.types, l.functions, l.scripts);
    if (fs::path(pathOf(l.path)).is_absolute())
        plan.warnings.push_back("le fichier est hors du projet : le lien garde son chemin absolu (copie-le dans libs/ du projet pour qu'il voyage avec lui)");
    for (const auto& [dep, floor] : plan.package.manifest.dependencies)
        if (!p.libraryByName(dep))
            plan.warnings.push_back("elle emploie la biblioth\xC3\xA8que " + dep + (floor.empty() ? std::string{} : " (" + floor + " ou plus)")
                                    + " : relie-la aussi");
    if (!same(name, published)) plan.warnings.push_back("reli\xC3\xA9" "e sous le nom " + name + " (publi\xC3\xA9" "e sous " + published + ")");
    return plan;
}

bool link(Project& p, const LinkPlan& plan, const std::string& projectFolder) {
    if (!plan.ok() || p.libraryByName(plan.link.name)) return false;
    p.libraries.push_back(plan.link);
    resolve(p, projectFolder);
    return true;
}

bool unlink(Project& p, std::string_view name) {
    const auto it = std::find_if(p.libraries.begin(), p.libraries.end(), [&](const Library& l) { return same(l.name, name); });
    if (it == p.libraries.end()) return false;
    p.libraries.erase(it);
    if (p.libraryCache.loaded) {
        auto next = std::make_shared<std::vector<LoadedLibrary>>();
        for (const auto& l : *p.libraryCache.loaded)
            if (!same(l.name, name)) next->push_back(l);
        p.libraryCache.loaded = next->empty() ? nullptr : std::shared_ptr<const std::vector<LoadedLibrary>>(next);
    }
    return true;
}

bool relocate(Project& p, std::string_view name, const std::string& file, const std::string& projectFolder, std::string* why) {
    auto it = std::find_if(p.libraries.begin(), p.libraries.end(), [&](const Library& l) { return same(l.name, name); });
    if (it == p.libraries.end()) {
        if (why) *why = "aucune biblioth\xC3\xA8que " + std::string(name);
        return false;
    }
    auto pkg = readFile(file);
    if (!pkg) {
        if (why) *why = pkg.error().context.empty() ? pkg.error().message() : pkg.error().context;
        return false;
    }
    if (!same(pkg->manifest.name, it->publishedName())) {
        if (why) *why = "ce fichier est la biblioth\xC3\xA8que " + pkg->manifest.name + ", pas " + it->publishedName();
        return false;
    }
    it->path = pathToRecord(file, projectFolder);
    resolve(p, projectFolder);
    return true;
}

// ------------------------------------------------------------------ mettre a jour ---
std::size_t UpdatePlan::breakCount() const {
    return static_cast<std::size_t>(std::count_if(changes.begin(), changes.end(), [](const InterfaceChange& c) { return c.breaks(); }));
}

UpdatePlan planUpdate(const Project& p, std::string_view name, const std::string& projectFolder, const std::string& file) {
    UpdatePlan plan;
    const Library* l = p.libraryByName(name);
    if (!l) {
        plan.refusal = "aucune biblioth\xC3\xA8que " + std::string(name) + " n'est reli\xC3\xA9" "e";
        return plan;
    }
    plan.name = l->name;
    plan.from = l->version;
    plan.file = file.empty() ? findFile(*l, projectFolder) : file;
    if (plan.file.empty()) {
        plan.refusal = l->name + " introuvable (" + l->path + ") : localise-la d'abord";
        return plan;
    }
    auto pkg = readFile(plan.file);
    if (!pkg) {
        plan.refusal = pkg.error().context.empty() ? pkg.error().message() : pkg.error().context;
        return plan;
    }
    if (!same(pkg->manifest.name, l->publishedName())) {
        plan.refusal = "ce fichier est la biblioth\xC3\xA8que " + pkg->manifest.name + ", pas " + l->publishedName();
        return plan;
    }
    plan.package = std::move(*pkg);
    plan.to = plan.package.manifest.version;
    // L'interface nouvelle, sous le nom local du lien.
    Library next = *l;
    const LoadedLibrary content = loadContent(next, plan.package, 0);
    Project shown;
    shown.programs = content.content;
    std::vector<LibraryTypeSig> types;
    std::vector<LibraryFunctionSig> functions;
    std::vector<std::string> scripts;
    interfaceOf(shown, l->name, types, functions, scripts);
    const auto full = [&](const std::string& rel) { return qualifiedName(l->name, rel); };
    using K = InterfaceChange::Kind;
    const auto add = [&](K k, const std::string& rel, const char* what, std::string before, std::string after) {
        InterfaceChange c;
        c.kind = k;
        c.name = full(rel);
        c.what = what;
        c.before = std::move(before);
        c.after = std::move(after);
        if (k != K::Added) c.usedAt = usesOf(p, c.name);
        plan.changes.push_back(std::move(c));
    };
    for (const auto& t : types) {
        const auto old = std::find_if(l->types.begin(), l->types.end(), [&](const LibraryTypeSig& o) { return same(o.name, t.name); });
        if (old == l->types.end()) add(K::Added, t.name, "type", {}, t.members);
        else if (!(old->members == t.members && old->kind == t.kind)) add(K::Changed, t.name, "type", old->members, t.members);
    }
    for (const auto& o : l->types)
        if (std::none_of(types.begin(), types.end(), [&](const LibraryTypeSig& t) { return same(o.name, t.name); }))
            add(K::Removed, o.name, "type", o.members, {});
    // Les fonctions : par nom et signature (une surcharge qui change compte comme retiree + ajoutee).
    for (const auto& f : functions) {
        const bool sameSig = std::any_of(l->functions.begin(), l->functions.end(),
                                         [&](const LibraryFunctionSig& o) { return same(o.name, f.name) && o.signature == f.signature; });
        if (sameSig) continue;
        const auto old = std::find_if(l->functions.begin(), l->functions.end(), [&](const LibraryFunctionSig& o) { return same(o.name, f.name); });
        const bool oldStill = old != l->functions.end()
                           && std::any_of(functions.begin(), functions.end(), [&](const LibraryFunctionSig& g) { return same(g.name, old->name) && g.signature == old->signature; });
        if (old == l->functions.end() || oldStill) add(K::Added, f.name, "fonction", {}, f.signature);
        else add(K::Changed, f.name, "fonction", old->signature, f.signature);
    }
    for (const auto& o : l->functions) {
        const bool kept = std::any_of(functions.begin(), functions.end(), [&](const LibraryFunctionSig& f) { return same(o.name, f.name); });
        if (!kept) add(K::Removed, o.name, "fonction", o.signature, {});
    }
    for (const auto& s : scripts)
        if (std::none_of(l->scripts.begin(), l->scripts.end(), [&](const std::string& o) { return same(o, s); })) add(K::Added, s, "script", {}, {});
    for (const auto& o : l->scripts)
        if (std::none_of(scripts.begin(), scripts.end(), [&](const std::string& s) { return same(o, s); })) add(K::Removed, o, "script", {}, {});
    return plan;
}

bool applyUpdate(Project& p, const UpdatePlan& plan, const std::string& projectFolder) {
    if (!plan.refusal.empty()) return false;
    auto it = std::find_if(p.libraries.begin(), p.libraries.end(), [&](const Library& l) { return same(l.name, plan.name); });
    if (it == p.libraries.end()) return false;
    const Version to = parseVersion(plan.to);
    const Version floor = parseVersion(it->compat.empty() ? it->version : it->compat);
    it->version = plan.to;
    // Un nouveau majeur : le plancher le suit (le projet accepte la rupture) ; sinon il reste ce qu'il etait.
    if (to.ok && (!floor.ok || to.major != floor.major)) it->compat = plan.to;
    if (it->compat.empty()) it->compat = plan.to;
    it->path = pathToRecord(plan.file, projectFolder);
    const LoadedLibrary content = loadContent(*it, plan.package, 0);
    Project shown;
    shown.programs = content.content;
    interfaceOf(shown, it->name, it->types, it->functions, it->scripts);
    resolve(p, projectFolder);
    return true;
}

std::vector<std::string> usesOf(const Project& p, std::string_view full) {
    std::vector<std::string> out;
    const auto [ns, shortName] = splitQualified(full);
    if (shortName.empty()) return out;
    const std::string uFull = upperOf(full), uShort = upperOf(shortName), uNs = upperOf(ns);
    const auto wordAt = [](const std::string& u, std::size_t at, std::size_t len) {
        return (at == 0 || !(identChar(u[at - 1]) || u[at - 1] == '.')) && (at + len >= u.size() || !identChar(u[at + len]));
    };
    const auto mentions = [&](std::string_view text, bool code) {
        const std::string u = upperOf(text);
        for (std::size_t at = u.find(uFull); at != std::string::npos; at = u.find(uFull, at + 1))
            if (wordAt(u, at, uFull.size())) return true;
        if (!code) return false;
        // un nom court sous USING de l'espace
        bool imports = false;
        for (const auto& us : names::usings(text)) imports = imports || upperOf(us.ns) == uNs;
        if (!imports) return false;
        for (std::size_t at = u.find(uShort); at != std::string::npos; at = u.find(uShort, at + 1))
            if (wordAt(u, at, uShort.size())) return true;
        return false;
    };
    std::set<std::string> said;
    const auto note = [&](const std::string& w) { if (said.insert(w).second) out.push_back(w); };
    forEachCode(p, [&](const std::string& text, CodeForm form, const CodeSite& site) {
        if (!text.empty() && mentions(text, form == CodeForm::Code)) note(site.where);
    });
    for (const auto& v : p.programs.variables) if (mentions(v.type, false)) note("variable " + v.name);
    for (const auto& t : p.programs.types)
        for (const auto& m : t.members) if (mentions(m.type, false)) note("type " + fullName(t) + " (membre " + m.name + ")");
    forEachDeclarations(p, [&](const std::vector<Declaration>& list) {
        for (const auto& d : list) if (mentions(d.type, false)) note("d\xC3\xA9" "claration " + d.name);
    });
    return out;
}

std::vector<LinkIssue> linkIssues(const Project& p) {
    std::vector<LinkIssue> out;
    std::set<std::string> seen;
    for (const auto& l : p.libraries) {
        if (!seen.insert(upperOf(l.name)).second) {
            out.push_back({l.name, "deux biblioth\xC3\xA8ques s'appellent " + l.name + " : relie l'une sous un autre nom", true, false});
            continue;
        }
        if (const std::string r = names::reservedClash(p, l.name); !r.empty())
            out.push_back({l.name, r + " (la biblioth\xC3\xA8que " + l.name + ") : renomme l'un ou relie-la sous un autre nom", true, false});
        const LoadedLibrary* ld = p.loadedLibrary(l.name);
        if (ld) {
            switch (ld->state) {
                case LibraryState::Missing: out.push_back({l.name, "biblioth\xC3\xA8que " + ld->message, true, true}); break;
                case LibraryState::Unreadable: out.push_back({l.name, "biblioth\xC3\xA8que " + ld->message, true, true}); break;
                case LibraryState::Incompatible:
                    out.push_back({l.name, "biblioth\xC3\xA8que " + ld->message + " : remets une " + l.name
                                               + " compatible, ou mets-la \xC3\xA0 jour (ses ruptures te seront montr\xC3\xA9" "es)", true, true});
                    break;
                case LibraryState::Older: out.push_back({l.name, "biblioth\xC3\xA8que " + ld->message, false, true}); break;
                case LibraryState::Newer: out.push_back({l.name, "biblioth\xC3\xA8que " + ld->message, false, false}); break;
                case LibraryState::Linked: break;
            }
        }
        if (!l.path.empty() && pathOf(l.path).is_absolute())
            out.push_back({l.name, "la biblioth\xC3\xA8que " + l.name + " est reli\xC3\xA9" "e par un chemin absolu (" + l.path
                                       + ") : copie-la dans libs/ du projet pour qu'il voyage avec elle", false, false});
        // Un espace du projet qui porte le nom de la bibliotheque : leurs noms se meleraient.
        const auto mine = [&](std::string_view ns) {
            return !ns.empty() && (same(ns, l.name) || (ns.size() > l.name.size() && same(ns.substr(0, l.name.size()), l.name) && ns[l.name.size()] == '.'));
        };
        const bool clash = std::any_of(p.programs.types.begin(), p.programs.types.end(), [&](const HmiType& t) { return mine(t.ns); })
                        || std::any_of(p.programs.functions.begin(), p.programs.functions.end(), [&](const HmiFunction& f) { return mine(f.ns); })
                        || std::any_of(p.programs.scripts.begin(), p.programs.scripts.end(), [&](const Script& s) { return mine(s.ns); });
        if (clash)
            out.push_back({l.name, "l'espace " + l.name + " est \xC3\xA0 la fois une biblioth\xC3\xA8que reli\xC3\xA9" "e et un espace du projet : renomme l'espace du projet",
                           true, false});
    }
    return out;
}

} // namespace hmi::biblio
