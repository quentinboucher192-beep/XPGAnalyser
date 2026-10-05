#include "HmiAssets.hpp"
#include "HmiStore.hpp"
#include "HmiWidgets.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
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
    return key == "image" || key == "video" || key == "poster" || key == "font";
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

} // namespace hmi
