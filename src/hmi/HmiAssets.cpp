#include "HmiAssets.hpp"
#include "HmiGuide.hpp"         // 1.14.1 (lot N6) : fold (la recherche de la bibliotheque d'images)
#include "HmiMedia.hpp"
#include "HmiStore.hpp"
#include "HmiWidgets.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <system_error>

namespace fs = std::filesystem;

namespace hmi {

namespace {

std::string fileNameOf(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// 'nom' ou "nom" dans une expression : c'est ainsi qu'une expression choisit une
// image (SEL(Ouvert, 'vanne_fermee.png', 'vanne_ouverte.png')).
bool quotes(std::string_view expr, std::string_view name) {
    if (name.empty()) return false;
    for (char q : {'\'', '"'}) {
        std::string needle;
        needle += q;
        needle += name;
        needle += q;
        if (expr.find(needle) != std::string_view::npos) return true;
    }
    return false;
}

std::size_t replaceQuoted(std::string& expr, std::string_view from, std::string_view to) {
    std::size_t n = 0;
    for (char q : {'\'', '"'}) {
        const std::string a = std::string(1, q) + std::string(from) + std::string(1, q);
        const std::string b = std::string(1, q) + std::string(to) + std::string(1, q);
        for (std::size_t at = expr.find(a); at != std::string::npos; at = expr.find(a, at + b.size())) {
            expr.replace(at, a.size(), b);
            ++n;
        }
    }
    return n;
}

} // namespace

// ------------------------------------------------------------------ ressources ---
void describeResource(Resource& r) {
    if (!r.data) return;
    const MediaInfo m = inspectMedia(*r.data, r.name);
    // ---- Lot API 8 : glisser de fichiers, 2e partie ----
    //  Ni un media lu dans ses octets, ni une extension de media : un document
    //  (PDF, DOCX, ZIP...), garde tel quel ; son extension est son format.
    if (m.kind == MediaKind::Unknown && m.format.empty() && formatFromExtension(r.name).empty()) {
        r.format = documentFormat(r.name);
        r.bytes = r.data->size();
        r.width = r.height = 0;
        r.seconds = 0;
        r.detail.clear();
        return;
    }
    // ---- fin Lot API 8 : glisser de fichiers, 2e partie ----
    r.format = m.format.empty() ? formatFromExtension(r.name) : m.format;
    r.bytes = r.data->size();
    r.width = m.width;
    r.height = m.height;
    r.seconds = m.seconds;
    std::string detail;
    auto add = [&](const std::string& part) {
        if (part.empty()) return;
        if (!detail.empty()) detail += ", ";
        detail += part;
    };
    switch (m.kind) {
        case MediaKind::Image:
            if (m.format == "GIF" && m.images > 1) {
                // Lot 16 : un GIF anime - ses images, un tour, sa boucle.
                char s[32];
                std::snprintf(s, sizeof s, "%.2f s", m.seconds);
                std::string secs(s);
                for (auto& ch : secs) if (ch == '.') ch = ',';
                add("GIF anim\xC3\xA9 : " + std::to_string(m.images) + " images, " + secs + " par tour");
                add(m.loop == 0 ? std::string("boucle sans fin") : m.loop > 0 ? "boucle " + std::to_string(m.loop) + " fois" : std::string("une fois"));
            } else if (m.images > 1) {
                add(std::to_string(m.images) + " images");
            }
            break;
        case MediaKind::Sound:
            add(m.codec);
            if (m.sampleRate) add(std::to_string(m.sampleRate) + " Hz");
            if (m.channels) add(m.channels == 1 ? "mono" : m.channels == 2 ? "st\xC3\xA9r\xC3\xA9o" : std::to_string(m.channels) + " canaux");
            if (m.bitsPerSample) add(std::to_string(m.bitsPerSample) + " bits");
            else if (m.bitrateKbps) add(std::to_string(m.bitrateKbps) + " kbit/s");
            break;
        case MediaKind::Video:
            add(m.codec);
            break;
        case MediaKind::Font:
            add(m.family + (m.style.empty() ? "" : " " + m.style));
            if (m.glyphs) add(std::to_string(m.glyphs) + " glyphes");
            break;
        case MediaKind::Document:                        // lot API 8 : traite plus haut
        case MediaKind::Unknown: break;
    }
    if (!m.ok()) add("ERREUR : " + m.error);
    r.detail = detail;
}

Resource makeResource(Project& p, std::string name, BlobPtr data, std::string origin) {
    Resource r;
    r.id = p.allocate();
    r.name = uniqueResourceName(p, name);
    r.origin = std::move(origin);
    r.added = nowStamp();
    r.data = std::move(data);
    describeResource(r);
    return r;
}

core::Result<Resource> readResource(Project& p, const std::string& path, std::string wantedName) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec))
        return core::fail(core::ErrorCode::FileNotFound, "fichier introuvable : " + path);
    const auto size = fs::file_size(path, ec);
    if (ec) return core::fail(core::ErrorCode::FileUnreadable, "taille illisible : " + path);
    // Une ressource est gardee en memoire et copiee dans le projet : au-dela,
    // c'est un fichier externe, qu'on cite sans l'embarquer.
    constexpr std::uintmax_t kMax = 256ull * 1024 * 1024;
    if (size > kMax)
        return core::fail(core::ErrorCode::InvalidArgument,
                          "fichier trop lourd pour \xC3\xAAtre embarqu\xC3\xA9 (" + formatBytes(size) + ", 256 Mo au plus)");
    std::ifstream in(path, std::ios::binary);
    if (!in) return core::fail(core::ErrorCode::FileUnreadable, "lecture impossible : " + path);
    auto bytes = std::make_shared<Bytes>(static_cast<std::size_t>(size));
    if (size > 0) in.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(size));
    if (!in) return core::fail(core::ErrorCode::FileUnreadable, "lecture incompl\xC3\xA8te : " + path);
    const std::string name = wantedName.empty() ? fileNameOf(path) : std::move(wantedName);
    // Lot API 8 : un fichier qui n'est pas un media n'est plus refuse : c'est un
    // document (describeResource), garde tel quel.
    Resource r = makeResource(p, name, std::move(bytes), path);
    if (r.kind() == MediaKind::Unknown)
        return core::fail(core::ErrorCode::InvalidArgument, r.name + " : " + r.detail);
    return r;
}

bool citesResource(std::string_view key) noexcept {
    return key == "image" || key == "video" || key == "poster" || key == "font" || key == "document";   // 1.12.30 : la visionneuse
}

std::vector<Citation> citations(const Project& p, std::string_view name) {
    std::vector<Citation> out;
    if (name.empty()) return out;
    // Lot 6 : les images d'un etat d'image animee, le son d'une action, un
    // IHM_SON('...') dans un script comptent aussi.
    const auto inStates = [&](const std::string& text) {
        for (const auto& st : parseImageStates(text))
            if (std::find(st.images.begin(), st.images.end(), name) != st.images.end()) return true;
        return false;
    };
    const auto actionCites = [&](const Action& a) {
        return (a.operation == Operation::PlaySound && a.target == name)
            || ((a.operation == Operation::RunScript || a.operation == Operation::Assign) && quotes(a.value, name));
    };
    for (const auto& v : p.views) {
        for (const auto& o : v.objects) {
            for (const auto& prop : o.props) {
                const bool byValue = (citesResource(prop.key) && prop.value == name) || (prop.key == "states" && inStates(prop.value));
                const bool byExpr = !prop.expr.empty() && quotes(prop.expr, name);
                if (byValue || byExpr) out.push_back({v.id, o.id, v.name + "/" + o.name + "." + prop.key, !byValue});
            }
            for (const auto& a : o.actions)
                if (actionCites(a)) out.push_back({v.id, o.id, v.name + "/" + o.name + " (action)", false});
        }
        for (const auto& a : v.actions)
            if (actionCites(a)) out.push_back({v.id, kNoId, v.name + " (action de vue)", false});
        for (const auto& sc : v.scripts)
            if (quotes(sc.body, name)) out.push_back({v.id, kNoId, v.name + "." + sc.event, true});
    }
    for (const auto& sc : p.programs.scripts)
        if (quotes(sc.body, name)) out.push_back({kNoId, kNoId, "script " + sc.name, true});
    // Lot 11 : le son d'une priorite d'alarme (Configuration > Alarmes).
    for (std::size_t k = 0; k < p.alarmSettings.sounds.size(); ++k)
        if (p.alarmSettings.sounds[k] == name)
            out.push_back({kNoId, kNoId, "alarmes : son de la priorit\xC3\xA9 " + std::to_string(k + 1), false});
    return out;
}

std::vector<const Resource*> unusedResources(const Project& p) {
    std::vector<const Resource*> out;
    // Lot API 8 : un document accompagne le projet (une notice, un plan) : aucun
    // objet ne le cite, il n'est pas "inutilise" (ni retire avec les inutilisees).
    for (const auto& r : p.assets.resources)
        if (r.kind() != MediaKind::Document && citations(p, r.name).empty()) out.push_back(&r);
    return out;
}

bool renameResource(Project& p, Id id, const std::string& newName, std::size_t* updated, std::string* why) {
    auto* r = p.resource(id);
    if (!r) { if (why) *why = "ressource introuvable"; return false; }
    if (newName.empty()) { if (why) *why = "une ressource a un nom"; return false; }
    if (newName == r->name) { if (updated) *updated = 0; return true; }
    if (p.resourceByName(newName)) { if (why) *why = "'" + newName + "' existe d\xC3\xA9j\xC3\xA0"; return false; }
    const std::string old = r->name;
    std::size_t n = 0;
    const auto renameIn = [&](Action& a) {
        if (a.operation == Operation::PlaySound && a.target == old) { a.target = newName; ++n; }
        if (a.operation == Operation::RunScript || a.operation == Operation::Assign) n += replaceQuoted(a.value, old, newName);
    };
    for (auto& v : p.views) {
        for (auto& o : v.objects) {
            for (auto& prop : o.props) {
                if (citesResource(prop.key) && prop.value == old) { prop.value = newName; ++n; }
                if (!prop.expr.empty()) n += replaceQuoted(prop.expr, old, newName);
                // Lot 6 : les images des etats d'une image animee.
                if (prop.key == "states" && !prop.value.empty()) {
                    auto states = parseImageStates(prop.value);
                    bool changed = false;
                    for (auto& st : states)
                        for (auto& img : st.images)
                            if (img == old) { img = newName; changed = true; ++n; }
                    if (changed) prop.value = formatImageStates(states);
                }
            }
            for (auto& a : o.actions) renameIn(a);
        }
        for (auto& a : v.actions) renameIn(a);
        for (auto& sc : v.scripts) n += replaceQuoted(sc.body, old, newName);
    }
    for (auto& sc : p.programs.scripts) n += replaceQuoted(sc.body, old, newName);
    for (auto& snd : p.alarmSettings.sounds)          // lot 11 : les sons des alarmes suivent
        if (snd == old) { snd = newName; ++n; }
    r->name = newName;
    if (updated) *updated = n;
    return true;
}

// ------------------------------------------------------------ fichiers externes ---
std::string_view externalStatusLabel(ExternalStatus s) noexcept {
    switch (s) {
        case ExternalStatus::Present:      return "pr\xC3\xA9sent";
        case ExternalStatus::Modified:     return "modifi\xC3\xA9";
        case ExternalStatus::Missing:      return "absent";
        case ExternalStatus::Unverifiable: return "non v\xC3\xA9rifiable";
    }
    return "absent";
}

std::string fileStamp(const std::string& path) {
    std::error_code ec;
    const auto ft = fs::last_write_time(path, ec);
    if (ec) return {};
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(ft);
    const std::time_t t = std::chrono::system_clock::to_time_t(sys);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[64];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

std::string resolveExternalPath(const ExternalFile& f, const std::string& projectFolder) {
    if (f.kind == ExternalKind::Database) return f.path;
    fs::path p(f.path);
    if (p.is_absolute() || projectFolder.empty()) return p.string();
    return (fs::path(projectFolder) / p).lexically_normal().string();
}

ExternalState externalState(const ExternalFile& f, const std::string& projectFolder) {
    ExternalState st;
    st.resolvedPath = resolveExternalPath(f, projectFolder);
    if (f.kind == ExternalKind::Database) { st.status = ExternalStatus::Unverifiable; return st; }
    std::error_code ec;
    if (!fs::is_regular_file(st.resolvedPath, ec)) { st.status = ExternalStatus::Missing; return st; }
    st.bytes = static_cast<std::uint64_t>(fs::file_size(st.resolvedPath, ec));
    st.modified = fileStamp(st.resolvedPath);
    st.status = (st.bytes == f.bytes && st.modified == f.modified) ? ExternalStatus::Present : ExternalStatus::Modified;
    return st;
}

void relinkExternal(ExternalFile& f, const std::string& projectFolder) {
    const auto st = externalState(f, projectFolder);
    f.bytes = st.bytes;
    f.modified = st.modified;
    f.linked = nowStamp();
}

ExternalFile linkExternal(Project& p, std::string name, ExternalKind kind, std::string path, std::string part,
                          const std::string& projectFolder) {
    ExternalFile f;
    f.id = p.allocate();
    if (name.empty()) {
        name = kind == ExternalKind::Database ? std::string("Base") : fileNameOf(path);
        if (const auto dot = name.rfind('.'); dot != std::string::npos && dot > 0) name.resize(dot);
    }
    f.name = uniqueExternalName(p, name);
    f.kind = kind;
    f.path = std::move(path);
    f.part = std::move(part);
    // Un fichier DANS le dossier du projet est garde en relatif : le projet
    // copie ailleurs avec ses donnees les retrouve. Hors du dossier : absolu.
    if (kind != ExternalKind::Database && !projectFolder.empty()) {
        std::error_code ec;
        const fs::path target(f.path);
        const fs::path base = fs::absolute(fs::path(projectFolder), ec).lexically_normal();
        if (!ec && target.is_absolute()) {
            const auto rel = target.lexically_normal().lexically_relative(base);
            if (!rel.empty() && *rel.begin() != fs::path(".."))
                f.path = rel.generic_string();
        }
    }
    relinkExternal(f, projectFolder);
    return f;
}

std::vector<Citation> externalCitations(const Project& p, std::string_view name) {
    std::vector<Citation> out;
    if (name.empty()) return out;
    for (const auto& v : p.views)
        for (const auto& o : v.objects)
            for (const auto& prop : o.props)
                if ((prop.key == "source" && o.kind == Kind::Table && prop.value == name)
                    || (!prop.expr.empty() && quotes(prop.expr, name)))
                    out.push_back({v.id, o.id, v.name + "/" + o.name + "." + prop.key, !prop.expr.empty()});
    return out;
}

// =============================================== 1.14.1 (lot N6) : la bibliotheque d'images ===
namespace imagelib {

namespace {

// Un chemin en UTF-8 <-> std::filesystem (sous Windows, std::string serait lu dans la page de code du systeme).
fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string& folderRef() {
    static std::string f;
    return f;
}

bool readAll(const fs::path& path, Bytes& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::string hex6(std::uint32_t rgb) {
    char b[8];
    std::snprintf(b, sizeof b, "#%06X", static_cast<unsigned>(rgb & 0xFFFFFFu));
    return b;
}

// "Le nom.PNG" -> ("Le nom", ".PNG").
std::pair<std::string, std::string> stemExt(const std::string& name) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return {name, {}};
    return {name.substr(0, dot), name.substr(dot)};
}

// Un nom de fichier sur partout (Windows compris).
std::string safeFileName(std::string name) {
    for (auto& ch : name)
        if (static_cast<unsigned char>(ch) < 0x20 || std::string_view("<>:\"/\\\\|?*").find(ch) != std::string_view::npos) ch = '_';
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    return name.empty() ? std::string("image") : name;
}

} // namespace

const std::vector<Image>& builtIn() {
    static const std::vector<Image> all = [] {
        std::vector<Image> out;
        out.reserve(builtInData().size());
        for (const auto& b : builtInData()) {
            Image i;
            i.id = b.id;
            i.label = b.label;
            i.category = b.category;
            i.words = b.words;
            i.ink = b.ink;
            i.mono = b.mono;
            i.width = b.width;
            i.height = b.height;
            i.format = "SVG";
            const std::string_view svg(b.svg);
            i.data = std::make_shared<const Bytes>(svg.begin(), svg.end());
            out.push_back(std::move(i));
        }
        return out;
    }();
    return all;
}

const std::vector<std::string>& builtInCategories() {
    static const std::vector<std::string> cats = [] {
        std::vector<std::string> out;
        for (const auto& i : builtIn())
            if (out.empty() || out.back() != i.category) out.push_back(i.category);
        return out;
    }();
    return cats;
}

const Image* findBuiltIn(std::string_view id) {
    for (const auto& i : builtIn())
        if (i.id == id) return &i;
    return nullptr;
}

void setUserFolder(std::string folder) { folderRef() = std::move(folder); }
const std::string& userFolder() { return folderRef(); }

std::vector<Image> userImages() {
    std::vector<Image> out;
    const std::string& folder = folderRef();
    if (folder.empty()) return out;
    std::error_code ec;
    std::vector<fs::path> files;
    for (fs::directory_iterator it(P(folder), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const std::string name = U(it->path().filename());
        if (kindOfFormat(formatFromExtension(name)) != MediaKind::Image) continue;
        files.push_back(it->path());
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        return guide::fold(U(a.filename())) < guide::fold(U(b.filename()));
    });
    for (const auto& f : files) {
        auto bytes = std::make_shared<Bytes>();
        if (!readAll(f, *bytes) || bytes->empty()) continue;
        const std::string name = U(f.filename());
        const MediaInfo m = inspectMedia(*bytes, name);
        if (m.kind != MediaKind::Image || !m.ok()) continue;
        Image i;
        i.id = "mes/" + name;
        i.label = stemExt(name).first;
        i.category = std::string(kMine);
        i.words = name;
        i.width = m.width;
        i.height = m.height;
        i.format = m.format;
        i.file = U(f);
        i.data = std::move(bytes);
        out.push_back(std::move(i));
    }
    return out;
}

bool addUserImage(const std::string& name, const Bytes& data, std::string* stored, bool* existed, std::string* why) {
    const auto fail = [why](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    if (existed) *existed = false;
    const std::string& folder = folderRef();
    if (folder.empty()) return fail("pas de dossier pour Mes images");
    const std::string clean = safeFileName(name);
    const MediaInfo m = inspectMedia(data, clean);
    if (m.kind != MediaKind::Image || !m.ok())
        return fail(clean + " n'est pas une image (PNG, JPEG, BMP, GIF, SVG ou ICO)" + (m.ok() ? std::string{} : " : " + m.error));
    std::error_code ec;
    fs::create_directories(P(folder), ec);
    // Le meme contenu deja la : rien a ecrire.
    for (const auto& img : userImages())
        if (img.data && *img.data == data) {
            if (existed) *existed = true;
            if (stored) *stored = U(P(img.file).filename());
            return true;
        }
    auto [stem, ext] = stemExt(clean);
    if (ext.empty()) {
        ext = "." + m.format;
        for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    std::string file = stem + ext;
    for (int n = 2; n < 1000 && fs::exists(P(folder) / P(file), ec); ++n) file = stem + " (" + std::to_string(n) + ")" + ext;
    const fs::path target = P(folder) / P(file);
    {
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) return fail("\xC3\xA9" "criture impossible : " + U(target));
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) return fail("\xC3\xA9" "criture incompl\xC3\xA8te : " + U(target));
    }
    if (stored) *stored = file;
    return true;
}

bool addUserFile(const std::string& path, std::string* stored, bool* existed, std::string* why) {
    Bytes data;
    if (!readAll(P(path), data) || data.empty()) {
        if (why) *why = "fichier illisible : " + path;
        return false;
    }
    return addUserImage(U(P(path).filename()), data, stored, existed, why);
}

bool removeUserImage(const Image& img, std::string* why) {
    if (!img.mine()) {
        if (why) *why = "une image de la biblioth\xC3\xA8que se garde (seules Mes images se retirent)";
        return false;
    }
    std::error_code ec;
    if (!fs::remove(P(img.file), ec) || ec) {
        if (why) *why = "suppression impossible : " + img.file;
        return false;
    }
    return true;
}

bool matches(const Image& img, std::string_view query) {
    const std::string q = guide::fold(query);
    const std::string hay = guide::fold(img.label + " " + img.category + " " + img.words + " " + img.id);
    std::size_t at = 0;
    bool any = false;
    while (at < q.size()) {
        while (at < q.size() && (q[at] == ' ' || q[at] == ',')) ++at;
        std::size_t end = at;
        while (end < q.size() && q[end] != ' ' && q[end] != ',') ++end;
        if (end == at) break;
        const std::string word = q.substr(at, end - at);
        at = end;
        any = true;
        bool found = false;
        for (std::size_t pos = hay.find(word); pos != std::string::npos && !found; pos = hay.find(word, pos + 1))
            found = pos == 0 || !std::isalnum(static_cast<unsigned char>(hay[pos - 1]));
        if (!found) return false;
    }
    return any;
}

const std::vector<Swatch>& swatches() {
    static const std::vector<Swatch> k = {
        {"clair", "Clair", kInk},        {"sombre", "Sombre", kDarkInk},  {"noir", "Noir", 0x111111},
        {"gris", "Gris", 0x8A94A6},       {"bleu", "Bleu", 0x2F6FD6},      {"vert", "Vert", 0x1E9A4A},
        {"jaune", "Jaune", 0xF5C400},     {"orange", "Orange", 0xEE8A1F},  {"rouge", "Rouge", 0xD93A2B},
    };
    return k;
}

std::uint32_t autoInk(std::string_view background) {
    unsigned r = 0x20, g = 0x24, b = 0x2B;
    if (background.size() >= 7 && background[0] == '#') {
        const auto h = [&](std::size_t at) { return static_cast<unsigned>(std::stoul(std::string(background.substr(at, 2)), nullptr, 16)); };
        try {
            r = h(1);
            g = h(3);
            b = h(5);
        } catch (...) {
        }
    }
    return (r * 299 + g * 587 + b * 114) / 1000 < 140 ? kInk : kDarkInk;
}

BlobPtr contentFor(const Image& img, std::uint32_t rgb) {
    if (!img.data || !img.ink || (rgb & 0xFFFFFFu) == kInk) return img.data;
    static std::mutex m;
    static std::map<std::pair<const void*, std::uint32_t>, BlobPtr> cache;
    const std::lock_guard<std::mutex> lock(m);
    auto& slot = cache[{img.data.get(), rgb & 0xFFFFFFu}];
    if (slot) return slot;
    std::string text(img.data->begin(), img.data->end());
    const std::string to = hex6(rgb);
    for (std::size_t at = text.find(kInkHex); at != std::string::npos; at = text.find(kInkHex, at + to.size()))
        text.replace(at, kInkHex.size(), to);
    slot = std::make_shared<const Bytes>(text.begin(), text.end());
    return slot;
}

std::string resourceName(const Image& img, std::uint32_t rgb) {
    if (img.mine()) return U(P(img.file).filename());
    std::string name = img.id;
    rgb &= 0xFFFFFFu;
    if (img.ink && rgb != kInk) {
        std::string key;
        for (const auto& s : swatches())
            if (s.rgb == rgb) key = std::string(s.key);
        if (key.empty()) {
            key = hex6(rgb).substr(1);
            for (auto& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        name += "-" + key;
    }
    return name + ".svg";
}

Imported importImage(Project& p, const Image& img, std::uint32_t rgb) {
    const BlobPtr blob = contentFor(img, rgb);
    Imported out;
    if (!blob) return out;
    for (const auto& r : p.assets.resources)
        if (r.data && (r.data == blob || *r.data == *blob)) {
            out.id = r.id;
            out.name = r.name;
            out.reused = true;
            return out;
        }
    Resource r = makeResource(p, resourceName(img, rgb), blob,
                              img.mine() ? img.file : "Biblioth\xC3\xA8que d'images \xE2\x80\xBA " + img.category + " \xE2\x80\xBA " + img.label);
    out.id = r.id;
    out.name = r.name;
    p.assets.resources.push_back(std::move(r));
    return out;
}

std::string sizeText(const Image& img) {
    const std::string wh = std::to_string(img.width) + " x " + std::to_string(img.height);
    return img.format == "SVG" ? wh + ", vectorielle (nette \xC3\xA0 toute taille)" : wh + " px";
}

} // namespace imagelib

} // namespace hmi
