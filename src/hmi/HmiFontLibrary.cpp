// =============================================================================
//  hmi/HmiFontLibrary.cpp - 1.26.0 : la bibliotheque de polices (voir HmiFontLibrary.hpp)
// =============================================================================
#include "HmiFontLibrary.hpp"

#include "HmiAssets.hpp"
#include "HmiGuide.hpp"         // fold (la recherche)

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <system_error>

namespace fs = std::filesystem;

namespace hmi::fontlib {

namespace {

const char* const kSans = "Sans empattement";
const char* const kSerif = "Avec empattement";
const char* const kMono = "Chasse fixe";
const char* const kOfl = "SIL Open Font License 1.1";
const char* const kVera = "Licence Bitstream Vera (DejaVu : domaine public)";

// ---- les fournies : une entree par fichier, dans l'ordre de la bibliotheque -------------------------------------------
struct Spec {
    const char* id;
    const char* file;
    const char* family;
    const char* style;
    const char* category;
    const char* words;
    const char* description;
    const char* license;
    const char* licenseFile;
};

const std::vector<Spec>& specs() {
    static const std::vector<Spec> all = {
        // ---- Sans empattement ----
        {"liberation-sans", "LiberationSans-Regular.ttf", "Liberation Sans", "Normal", kSans, "arial helvetica arimo liberation",
         "Les m\xC3\xAAmes chasses qu'Arial : un texte pr\xC3\xA9vu en Arial garde sa place.", kOfl, "fonts-liberation.txt"},
        {"liberation-sans-gras", "LiberationSans-Bold.ttf", "Liberation Sans", "Gras", kSans, "arial helvetica arimo liberation gras bold",
         "Les m\xC3\xAAmes chasses qu'Arial : un texte pr\xC3\xA9vu en Arial garde sa place.", kOfl, "fonts-liberation.txt"},
        {"liberation-sans-italique", "LiberationSans-Italic.ttf", "Liberation Sans", "Italique", kSans,
         "arial helvetica arimo liberation italique italic",
         "Les m\xC3\xAAmes chasses qu'Arial : un texte pr\xC3\xA9vu en Arial garde sa place.", kOfl, "fonts-liberation.txt"},
        {"dejavu-sans", "DejaVuSans.ttf", "DejaVu Sans", "Normal", kSans, "dejavu verdana vera accents grec cyrillique symboles",
         "Tr\xC3\xA8s compl\xC3\xA8te : les accents, le grec, le cyrillique, les symboles et les fl\xC3\xA8" "ches.", kVera,
         "fonts-dejavu-core.txt"},
        {"dejavu-sans-gras", "DejaVuSans-Bold.ttf", "DejaVu Sans", "Gras", kSans, "dejavu verdana vera accents grec cyrillique symboles gras bold",
         "Tr\xC3\xA8s compl\xC3\xA8te : les accents, le grec, le cyrillique, les symboles et les fl\xC3\xA8" "ches.", kVera,
         "fonts-dejavu-core.txt"},
        {"carlito", "Carlito-Regular.ttf", "Carlito", "Normal", kSans, "calibri office word excel carlito",
         "Les m\xC3\xAAmes chasses que Calibri (la police d'Office).", kOfl, "fonts-crosextra-carlito.txt"},
        {"carlito-gras", "Carlito-Bold.ttf", "Carlito", "Gras", kSans, "calibri office word excel carlito gras bold",
         "Les m\xC3\xAAmes chasses que Calibri (la police d'Office).", kOfl, "fonts-crosextra-carlito.txt"},
        // ---- Avec empattement ----
        {"liberation-serif", "LiberationSerif-Regular.ttf", "Liberation Serif", "Normal", kSerif, "times new roman tinos liberation serif",
         "Les m\xC3\xAAmes chasses que Times New Roman.", kOfl, "fonts-liberation.txt"},
        {"liberation-serif-gras", "LiberationSerif-Bold.ttf", "Liberation Serif", "Gras", kSerif,
         "times new roman tinos liberation serif gras bold", "Les m\xC3\xAAmes chasses que Times New Roman.", kOfl, "fonts-liberation.txt"},
        {"dejavu-serif", "DejaVuSerif.ttf", "DejaVu Serif", "Normal", kSerif, "dejavu vera serif accents grec cyrillique symboles",
         "Tr\xC3\xA8s compl\xC3\xA8te, avec empattement : les accents, le grec, le cyrillique, les symboles.", kVera,
         "fonts-dejavu-core.txt"},
        {"caladea", "Caladea-Regular.ttf", "Caladea", "Normal", kSerif, "cambria office word caladea serif",
         "Les m\xC3\xAAmes chasses que Cambria (la police d'Office avec empattement).", kOfl, "fonts-crosextra-caladea.txt"},
        // ---- Chasse fixe ----
        {"liberation-mono", "LiberationMono-Regular.ttf", "Liberation Mono", "Normal", kMono, "courier new cousine liberation mono code",
         "Les m\xC3\xAAmes chasses que Courier New.", kOfl, "fonts-liberation.txt"},
        {"liberation-mono-gras", "LiberationMono-Bold.ttf", "Liberation Mono", "Gras", kMono, "courier new cousine liberation mono code gras bold",
         "Les m\xC3\xAAmes chasses que Courier New.", kOfl, "fonts-liberation.txt"},
        {"dejavu-sans-mono", "DejaVuSansMono.ttf", "DejaVu Sans Mono", "Normal", kMono, "dejavu vera mono code accents grec cyrillique",
         "Tr\xC3\xA8s compl\xC3\xA8te, \xC3\xA0 chasse fixe : les accents, le grec, le cyrillique, les symboles.", kVera,
         "fonts-dejavu-core.txt"},
        {"jetbrains-mono", "JetBrainsMono-Regular.ttf", "JetBrains Mono", "Normal", kMono, "jetbrains mono code valeurs chiffres",
         "Les chiffres et les valeurs bien align\xC3\xA9s (une police faite pour le code).", kOfl, "fonts-jetbrains-mono.txt"},
        {"jetbrains-mono-gras", "JetBrainsMono-Bold.ttf", "JetBrains Mono", "Gras", kMono, "jetbrains mono code valeurs chiffres gras bold",
         "Les chiffres et les valeurs bien align\xC3\xA9s (une police faite pour le code).", kOfl, "fonts-jetbrains-mono.txt"},
    };
    return all;
}

// ---- les fichiers ----
fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
bool readAll(const fs::path& path, Bytes& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}
std::pair<std::string, std::string> stemExt(const std::string& name) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return {name, {}};
    return {name.substr(0, dot), name.substr(dot)};
}
std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string safeFileName(std::string name) {
    for (auto& ch : name)
        if (static_cast<unsigned char>(ch) < 0x20 || std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos) ch = '_';
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    return name.empty() ? std::string("police") : name;
}
std::string leafOf(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// La table des tables d'une police : a-t-elle `tag` ?
bool hasTable(const Bytes& d, const char* tag) {
    if (d.size() < 12) return false;
    const auto be16 = [&](std::size_t at) { return at + 2 <= d.size() ? (static_cast<unsigned>(d[at]) << 8) | d[at + 1] : 0u; };
    const unsigned tables = be16(4);
    for (unsigned i = 0; i < tables; ++i) {
        const std::size_t rec = 12 + static_cast<std::size_t>(i) * 16;
        if (rec + 16 > d.size()) return false;
        if (std::equal(tag, tag + 4, d.begin() + static_cast<std::ptrdiff_t>(rec))) return true;
    }
    return false;
}

// L'ordre des styles dans une famille.
int styleRank(std::string_view s) {
    if (s == "Normal") return 0;
    if (s == "Gras") return 1;
    if (s == "Italique") return 2;
    if (s == "Gras italique") return 3;
    return 4;
}

std::atomic<std::uint64_t>& revisionRef() {
    static std::atomic<std::uint64_t> r{1};
    return r;
}

// ---- les fournies, lues une fois par dossier ----
struct NativeState {
    std::mutex        m;
    std::string       folder;       // le dossier donne (ou le defaut)
    std::string       loadedFrom;   // celui des polices lues
    bool              loaded{false};
    std::vector<Font> fonts;
};
NativeState& native() {
    static NativeState s;
    return s;
}

// ---- Mes polices : le dossier relu a chaque fois, un fichier relu seulement s'il a change ----
struct UserEntry {
    std::uintmax_t     size{0};
    fs::file_time_type time{};
    bool               ok{false};
    Font               font;
};
struct UserState {
    std::recursive_mutex             m;
    std::string                      folder;
    std::map<std::string, UserEntry> files;   // par chemin
    std::vector<Font>                last;    // la derniere liste (findFont, fromChoice)
};
UserState& user() {
    static UserState s;
    return s;
}

bool searchHit(const std::string& hayRaw, std::string_view query) {
    const std::string hay = " " + guide::fold(hayRaw) + " ";
    std::string word;
    const std::string q = guide::fold(query);
    for (std::size_t i = 0; i <= q.size(); ++i) {
        if (i == q.size() || q[i] == ' ') {
            if (!word.empty()) {
                // Chaque mot cherche commence un mot (apres un blanc, un tiret, une parenthese).
                bool found = false;
                for (std::size_t at = hay.find(word); at != std::string::npos && !found; at = hay.find(word, at + 1)) {
                    const char before = at == 0 ? ' ' : hay[at - 1];
                    found = before == ' ' || before == '-' || before == '(' || before == '/' || before == '\'';
                }
                if (!found) return false;
            }
            word.clear();
        } else {
            word += q[i];
        }
    }
    return true;
}

std::vector<Font> sortedStyles(std::vector<Font> fonts) {
    std::stable_sort(fonts.begin(), fonts.end(), [](const Font& a, const Font& b) { return styleRank(a.style) < styleRank(b.style); });
    return fonts;
}

const char* const kIhmFont = "celle de l'IHM";

} // namespace

std::string Font::fullName() const { return style.empty() || style == "Normal" ? label : label + " " + style; }

// ================================================================== lire une police ===
bool readable(const Bytes& data, std::string* family, bool* editable, std::string* why) {
    const auto fail = [why](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    if (editable) *editable = false;
    if (data.size() < 12) return fail("fichier trop court pour une police");
    if (data[0] == 't' && data[1] == 't' && data[2] == 'c' && data[3] == 'f')
        return fail("une collection TTC (plusieurs polices dans un fichier) : enregistre-en une seule, en TTF ou OTF");
    const MediaInfo m = inspectMedia(data, {});
    if (m.kind != MediaKind::Font) return fail("pas une police TrueType ou OpenType");
    if (!m.ok()) return fail(m.error);
    if (family) *family = m.family;
    if (editable) *editable = hasTable(data, "glyf") && hasTable(data, "loca");
    return true;
}

std::string frenchStyle(std::string_view subfamily) {
    const std::string s = lowerAscii(std::string(subfamily));
    const bool bold = s.find("bold") != std::string::npos || s.find("gras") != std::string::npos;
    const bool italic = s.find("italic") != std::string::npos || s.find("oblique") != std::string::npos || s.find("italique") != std::string::npos;
    if (bold && italic) return "Gras italique";
    if (bold) return "Gras";
    if (italic) return "Italique";
    if (s.empty() || s == "regular" || s == "book" || s == "normal" || s == "roman" || s == "medium") return "Normal";
    return std::string(subfamily);
}

std::string displayName(const Bytes& data) {
    const MediaInfo m = inspectMedia(data, {});
    if (m.kind != MediaKind::Font || m.family.empty()) return {};
    const std::string st = frenchStyle(m.style);
    return st == "Normal" ? m.family : m.family + " " + st;
}

// ================================================================== les fournies ===
void setNativeFolder(std::string folder) {
    auto& st = native();
    std::lock_guard<std::mutex> lock(st.m);
    st.folder = std::move(folder);
}

const std::string& nativeFolder() {
    auto& st = native();
    std::lock_guard<std::mutex> lock(st.m);
    if (st.folder.empty()) {
        std::error_code ec;
        st.folder = U(fs::absolute(fs::path("resources") / "polices", ec));
    }
    return st.folder;
}

const std::vector<Font>& builtIn() {
    const std::string folder = nativeFolder();
    auto& st = native();
    std::lock_guard<std::mutex> lock(st.m);
    if (st.loaded && st.loadedFrom == folder) return st.fonts;
    st.fonts.clear();
    st.loaded = true;
    st.loadedFrom = folder;
    for (const auto& s : specs()) {
        const fs::path path = P(folder) / s.file;
        auto bytes = std::make_shared<Bytes>();
        if (!readAll(path, *bytes) || bytes->empty()) continue;   // absente : pas listee
        bool editable = false;
        if (!readable(*bytes, nullptr, &editable)) continue;
        const MediaInfo m = inspectMedia(*bytes, s.file);
        Font f;
        f.id = s.id;
        f.label = s.family;
        f.category = s.category;
        f.style = s.style;
        f.words = s.words;
        f.description = s.description;
        f.format = m.format.empty() ? std::string("TTF") : m.format;
        f.file = U(path);
        f.editable = editable;
        f.license = s.license;
        f.licenseFile = U(P(folder) / "licences" / s.licenseFile);
        f.glyphs = m.glyphs;
        f.data = std::move(bytes);
        st.fonts.push_back(std::move(f));
    }
    return st.fonts;
}

const std::vector<std::string>& builtInCategories() {
    static const std::vector<std::string> all = {kSans, kSerif, kMono};
    return all;
}

const Font* findBuiltIn(std::string_view id) {
    for (const auto& f : builtIn())
        if (f.id == id) return &f;
    return nullptr;
}

// ================================================================== Mes polices ===
void setUserFolder(std::string folder) {
    auto& st = user();
    {
        std::lock_guard<std::recursive_mutex> lock(st.m);
        st.folder = std::move(folder);
        st.files.clear();
        st.last.clear();
    }
    ++revisionRef();
}

const std::string& userFolder() {
    auto& st = user();
    std::lock_guard<std::recursive_mutex> lock(st.m);
    return st.folder;
}

std::uint64_t userRevision() noexcept { return revisionRef().load(); }

std::vector<Font> userFonts() {
    auto& st = user();
    std::lock_guard<std::recursive_mutex> lock(st.m);
    std::vector<Font> out;
    if (st.folder.empty()) {
        st.last.clear();
        return out;
    }
    std::error_code ec;
    std::map<std::string, UserEntry> seen;
    for (fs::directory_iterator it(P(st.folder), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const std::string leaf = U(it->path().filename());
        const std::string ext = lowerAscii(stemExt(leaf).second);
        if (ext != ".ttf" && ext != ".otf") continue;
        const std::string key = U(it->path());
        const auto size = it->file_size(e2);
        const auto time = it->last_write_time(e2);
        if (const auto c = st.files.find(key); c != st.files.end() && c->second.size == size && c->second.time == time) {
            seen.emplace(key, c->second);
            continue;
        }
        UserEntry e;
        e.size = size;
        e.time = time;
        auto bytes = std::make_shared<Bytes>();
        std::string family;
        bool editable = false;
        if (readAll(it->path(), *bytes) && !bytes->empty() && readable(*bytes, &family, &editable)) {
            const MediaInfo m = inspectMedia(*bytes, leaf);
            Font& f = e.font;
            f.id = "mes/" + leaf;
            f.label = family.empty() ? stemExt(leaf).first : family;
            f.category = std::string(kMine);
            f.style = frenchStyle(m.style);
            f.words = stemExt(leaf).first;
            f.format = m.format.empty() ? std::string("TTF") : m.format;
            f.file = key;
            f.editable = editable;
            f.glyphs = m.glyphs;
            f.description = "Une de tes polices (" + leaf + ")" + (editable ? std::string(".") : std::string(" : contours CFF, aper\xC3\xA7u et emploi."));
            f.data = std::move(bytes);
            e.ok = true;
        }
        seen.emplace(key, std::move(e));
    }
    st.files = std::move(seen);
    for (const auto& [path, e] : st.files)
        if (e.ok) out.push_back(e.font);
    // Par famille (sans casse ni accents), puis par style, puis par fichier.
    std::sort(out.begin(), out.end(), [](const Font& a, const Font& b) {
        const std::string fa = guide::fold(a.label), fb = guide::fold(b.label);
        if (fa != fb) return fa < fb;
        if (styleRank(a.style) != styleRank(b.style)) return styleRank(a.style) < styleRank(b.style);
        return guide::fold(a.file) < guide::fold(b.file);
    });
    st.last = out;
    return out;
}

bool addUserFont(const std::string& name, const Bytes& data, std::string* stored, bool* existed, std::string* why) {
    const auto fail = [why](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    if (existed) *existed = false;
    const std::string folder = userFolder();
    if (folder.empty()) return fail("pas de dossier pour Mes polices");
    const std::string clean = safeFileName(name);
    std::string bad;
    if (!readable(data, nullptr, nullptr, &bad)) return fail(clean + " n'est pas une police TrueType ou OpenType : " + bad);
    std::error_code ec;
    fs::create_directories(P(folder), ec);
    for (const auto& f : userFonts())
        if (f.data && *f.data == data) {
            if (existed) *existed = true;
            if (stored) *stored = leafOf(f.file);
            return true;
        }
    auto [stem, ext] = stemExt(clean);
    if (const std::string low = lowerAscii(ext); low != ".ttf" && low != ".otf") {
        if (!ext.empty()) stem = clean;   // "ma.police" : le point fait partie du nom
        ext = inspectMedia(data, {}).format == "OTF" ? ".otf" : ".ttf";
    }
    std::string leaf = stem + ext;
    for (int k = 2; fs::exists(P(folder) / P(leaf), ec) && k < 1000; ++k) leaf = stem + " (" + std::to_string(k) + ")" + ext;
    std::ofstream out(P(folder) / P(leaf), std::ios::binary);
    if (!out) return fail("\xC3\xA9" "criture impossible dans " + folder);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) return fail("\xC3\xA9" "criture impossible dans " + folder);
    out.close();
    ++revisionRef();
    if (stored) *stored = leaf;
    return true;
}

bool addUserFile(const std::string& path, std::string* stored, bool* existed, std::string* why) {
    Bytes data;
    if (!readAll(P(path), data) || data.empty()) {
        if (why) *why = "fichier illisible : " + path;
        return false;
    }
    return addUserFont(U(P(path).filename()), data, stored, existed, why);
}

bool replaceUserFont(const Font& f, const Bytes& data, std::string* why) {
    const auto fail = [why](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    if (!f.mine() || f.file.empty()) return fail("une police fournie se garde : duplique-la dans Mes polices pour la modifier");
    std::string bad;
    if (!readable(data, nullptr, nullptr, &bad)) return fail("pas une police TrueType ou OpenType : " + bad);
    std::ofstream out(P(f.file), std::ios::binary | std::ios::trunc);
    if (!out) return fail("\xC3\xA9" "criture impossible : " + f.file);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) return fail("\xC3\xA9" "criture impossible : " + f.file);
    out.close();
    ++revisionRef();
    return true;
}

bool removeUserFont(const Font& f, std::string* why) {
    if (!f.mine()) {
        if (why) *why = "une police fournie se garde (seules Mes polices se retirent)";
        return false;
    }
    std::error_code ec;
    if (!fs::remove(P(f.file), ec) || ec) {
        if (why) *why = "suppression impossible : " + f.file;
        return false;
    }
    ++revisionRef();
    return true;
}

// ================================================================== les familles ===
const Font& Family::regular() const {
    for (const auto& f : fonts)
        if (f.style == "Normal") return f;
    return fonts.front();
}

const Font* Family::style(std::string_view s) const {
    for (const auto& f : fonts)
        if (f.style == s) return &f;
    return nullptr;
}

std::string Family::stylesText() const {
    std::string out;
    for (const auto& f : fonts) {
        if (out.find(f.style) != std::string::npos) continue;
        out += (out.empty() ? "" : ", ") + f.style;
    }
    return out;
}

std::vector<std::string> categories() {
    std::vector<std::string> out{std::string(kMine)};
    for (const auto& c : builtInCategories()) out.push_back(c);
    return out;
}

std::vector<Family> families() {
    std::vector<Family> out;
    // Une famille par nom ; deux fichiers du meme nom ET du meme style (deux copies de Carlito dans Mes polices) : deux
    // familles, chacune son fichier.
    const auto addTo = [&out](const Font& f) {
        for (auto& fam : out)
            if (fam.category == f.category && fam.label == f.label && !fam.style(f.style)) {
                fam.fonts.push_back(f);
                return;
            }
        out.push_back(Family{f.label, f.category, {f}});
    };
    for (const auto& f : userFonts()) addTo(f);
    for (const auto& f : builtIn()) addTo(f);
    for (auto& fam : out) fam.fonts = sortedStyles(std::move(fam.fonts));
    return out;
}

std::vector<Family> familiesOf(std::string_view category) {
    std::vector<Family> out;
    for (auto& fam : families())
        if (fam.category == category) out.push_back(std::move(fam));
    return out;
}

const Font* findFont(std::string_view id) {
    if (id.rfind("mes/", 0) != 0) return findBuiltIn(id);
    auto& st = user();
    std::lock_guard<std::recursive_mutex> lock(st.m);
    (void)userFonts();
    for (const auto& f : st.last)
        if (f.id == id) return &f;
    return nullptr;
}

// ================================================================== chercher, importer ===
bool matches(const Font& f, std::string_view query) {
    return searchHit(f.label + " " + f.style + " " + f.category + " " + f.words + " " + f.description + " " + f.id + " " + f.format + " "
                         + leafOf(f.file),
                     query);
}

bool matches(const Family& fam, std::string_view query) {
    return std::any_of(fam.fonts.begin(), fam.fonts.end(), [&](const Font& f) { return matches(f, query); });
}

std::string resourceName(const Font& f) { return f.file.empty() ? f.id + ".ttf" : leafOf(f.file); }

const Resource* importedAs(const Project& p, const Font& f) {
    if (!f.data) return nullptr;
    for (const auto& r : p.assets.resources)
        if (r.data && (r.data == f.data || (r.data->size() == f.data->size() && *r.data == *f.data))) return &r;
    return nullptr;
}

Imported importFont(Project& p, const Font& f) {
    Imported out;
    if (!f.data) return out;
    if (const Resource* r = importedAs(p, f)) {
        out.id = r->id;
        out.name = r->name;
        out.reused = true;
        return out;
    }
    Resource r = makeResource(p, resourceName(f), f.data,
                              f.mine() ? f.file : "Biblioth\xC3\xA8que de polices \xE2\x80\xBA " + f.category + " \xE2\x80\xBA " + f.fullName());
    out.id = r.id;
    out.name = r.name;
    p.assets.resources.push_back(std::move(r));
    return out;
}

const Resource* projectFont(const Project& p, std::string_view name) {
    if (name.empty() || name == "Sans" || name == "Mono") return nullptr;
    for (const auto& r : p.assets.resources)
        if (r.name == name) return r.kind() == MediaKind::Font && r.data ? &r : nullptr;
    return nullptr;
}

std::vector<const Resource*> projectFonts(const Project& p) {
    std::vector<const Resource*> out;
    for (const auto& r : p.assets.resources)
        if (r.kind() == MediaKind::Font) out.push_back(&r);
    return out;
}

std::string choiceLabel(const Font& f) {
    // Une des tiennes : le nom de son fichier (deux copies d'une meme famille restent distinctes).
    if (f.mine()) return std::string(kMine) + " \xE2\x80\xBA " + stemExt(leafOf(f.file)).first + (f.format == "OTF" ? " (OTF)" : "");
    return "Biblioth\xC3\xA8que \xE2\x80\xBA " + f.category + " \xE2\x80\xBA " + f.fullName();
}

std::vector<std::string> choices(const Project& p) {
    std::vector<std::string> out{"Sans", "Mono"};
    for (const auto* r : projectFonts(p)) out.push_back(r->name);
    for (const auto& f : userFonts())
        if (!importedAs(p, f)) out.push_back(choiceLabel(f));
    for (const auto& f : builtIn())
        if (!importedAs(p, f)) out.push_back(choiceLabel(f));
    return out;
}

const Font* fromChoice(std::string_view choice) {
    for (const auto& f : builtIn())
        if (choiceLabel(f) == choice) return &f;
    if (choice.rfind(kMine, 0) != 0) return nullptr;
    auto& st = user();
    std::lock_guard<std::recursive_mutex> lock(st.m);
    (void)userFonts();
    for (const auto& f : st.last)
        if (choiceLabel(f) == choice) return &f;
    return nullptr;
}

// ================================================================== la case Police ===
std::string themeFontName(const Project& p, bool values) {
    const std::string& name = values ? p.config.valueFont : p.config.defaultFont;
    if (name.empty()) return {};
    if (const Resource* r = projectFont(p, name)) {
        const std::string shown = displayName(*r->data);
        if (!shown.empty()) return shown;
    }
    return name;
}

std::vector<Choice> fieldChoices(const Project& p, bool theme) {
    std::vector<Choice> out;
    const std::string kTheme = "Th\xC3\xA8me";
    if (theme) {
        Choice c{"Celle de l'IHM", "", "", ""};
        c.shortLabel = c.label;
        out.push_back(std::move(c));
    } else {
        const std::string def = themeFontName(p, false), val = themeFontName(p, true);
        Choice d{"Police par d\xC3\xA9" "faut (" + (def.empty() ? std::string(kIhmFont) : def) + ")", "Sans", "", ""};
        Choice m{"Police des valeurs (" + (val.empty() ? std::string("chasse fixe de l'IHM") : val) + ")", "Mono", "", ""};
        for (Choice* c : {&d, &m}) {
            c->section = kTheme;
            c->shortLabel = c->label;
            if (const Resource* r = resolve(p, c->value)) { c->data = r->data; c->format = r->format; }
        }
        out.push_back(std::move(d));
        out.push_back(std::move(m));
    }
    // Les polices du projet, par leur famille ; deux qui se confondent : leur nom de fichier. 1.26.1 : rangees par
    // famille (une ligne, ses styles en pastilles), avec ce qui les emploie.
    const auto fonts = projectFonts(p);
    std::vector<std::string> labels;
    for (const auto* r : fonts) labels.push_back(r->data ? displayName(*r->data) : std::string{});
    std::vector<Choice> mine;
    for (std::size_t i = 0; i < fonts.size(); ++i) {
        std::string label = labels[i];
        const bool twin = std::count(labels.begin(), labels.end(), label) > 1;
        if (label.empty() || twin || label == "Sans" || label == "Mono") label = fonts[i]->name;
        Choice c{label, fonts[i]->name, "", "Du projet"};
        c.section = "Du projet";
        c.shortLabel = label;
        c.data = fonts[i]->data;
        c.format = fonts[i]->format;
        std::string family;
        if (!twin && fonts[i]->data && readable(*fonts[i]->data, &family) && !family.empty()) {
            c.family = family;
            const std::string& shown = labels[i];
            c.style = shown.size() > family.size() + 1 && shown.compare(0, family.size() + 1, family + " ") == 0
                          ? shown.substr(family.size() + 1) : std::string("Normal");
        }
        if (const Usage u = usageOf(p, fonts[i]->name); u.any()) c.note = usageText(u);
        mine.push_back(std::move(c));
    }
    // Une famille ensemble (dans l'ordre ou elle apparait), Normal d'abord.
    const auto styleRank = [](const std::string& st) { return st == "Normal" ? 0 : st == "Gras" ? 1 : st == "Italique" ? 2 : 3; };
    std::vector<std::string> order;
    for (const auto& c : mine)
        if (std::find(order.begin(), order.end(), c.family) == order.end()) order.push_back(c.family);
    std::stable_sort(mine.begin(), mine.end(), [&](const Choice& x, const Choice& y) {
        const auto ix = std::find(order.begin(), order.end(), x.family) - order.begin();
        const auto iy = std::find(order.begin(), order.end(), y.family) - order.begin();
        if (ix != iy) return ix < iy;
        if (x.family.empty()) return false;
        return styleRank(x.style) < styleRank(y.style);
    });
    for (auto& c : mine) out.push_back(std::move(c));
    // La bibliotheque : Mes polices, puis les fournies, pas encore dans le projet.
    const auto library = [&](const Font& f) {
        Choice c{choiceLabel(f), "", f.id, "Biblioth\xC3\xA8que"};
        c.section = f.mine() ? std::string(kMine) : "Biblioth\xC3\xA8que \xE2\x80\xBA " + f.category;
        c.family = f.label;
        c.style = f.style.empty() ? std::string("Normal") : f.style;
        c.shortLabel = f.mine() ? stemExt(leafOf(f.file)).first : f.fullName();
        c.data = f.data;
        c.format = f.format;
        return c;
    };
    for (const auto& f : userFonts())
        if (!importedAs(p, f)) out.push_back(library(f));
    for (const auto& f : builtIn())
        if (!importedAs(p, f)) out.push_back(library(f));
    // Deux choix d'une meme section avec la meme famille ET le meme style (deux copies d'une police) : chacun sa ligne.
    std::vector<bool> alone(out.size(), false);
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i].family.empty()) continue;
        for (std::size_t j = i + 1; j < out.size(); ++j)
            if (out[j].section == out[i].section && out[j].family == out[i].family && out[j].style == out[i].style)
                alone[i] = alone[j] = true;
    }
    for (std::size_t i = 0; i < out.size(); ++i)
        if (alone[i]) out[i].family.clear();
    return out;
}

std::string fieldLabel(const Project& p, std::string_view value, bool theme) {
    const std::string_view v = !theme && value.empty() ? std::string_view("Sans") : value;
    for (const auto& c : fieldChoices(p, theme))
        if (c.libraryId.empty() && c.value == v) return c.label;
    return std::string(value);
}

std::optional<Choice> fromLabel(const Project& p, std::string_view label, bool theme) {
    for (const auto& c : fieldChoices(p, theme))
        if (c.label == label) return c;
    if (const Font* f = fromChoice(label)) return Choice{std::string(label), "", f->id, "Biblioth\xC3\xA8que"};
    if (theme && label.empty()) return Choice{"Celle de l'IHM", "", "", ""};
    if (!theme && (label == "Sans" || label == "Mono" || label.empty()))
        return Choice{fieldLabel(p, label, false), label.empty() ? std::string("Sans") : std::string(label), "", ""};
    if (projectFont(p, label)) return Choice{std::string(label), std::string(label), "", "Du projet"};
    return std::nullopt;
}

// ================================================================== la police partout ===
namespace {
// 1.26.1 : le nom lisible d'une police ("carlito gras") et sa famille ("carlito"), plies - lus une fois par contenu.
struct FoldedNames { std::string full, family; };
FoldedNames foldedNamesOf(const BlobPtr& data) {
    static std::mutex m;
    static std::map<const Bytes*, std::pair<std::size_t, FoldedNames>> cache;
    if (!data) return {};
    std::lock_guard<std::mutex> lock(m);
    if (const auto it = cache.find(data.get()); it != cache.end() && it->second.first == data->size()) return it->second.second;
    FoldedNames n;
    std::string family;
    if (readable(*data, &family)) {
        n.full = guide::fold(displayName(*data));
        n.family = guide::fold(family);
    }
    if (cache.size() > 512) cache.clear();
    cache[data.get()] = {data->size(), n};
    return n;
}
bool startsWithFolded(const std::string& folded, std::string_view prefix) {
    const std::string p = guide::fold(prefix);
    return !p.empty() && folded.compare(0, p.size(), p) == 0;
}
} // namespace

const Resource* resolve(const Assets& a, const Config* cfg, std::string_view value) {
    std::string_view name = value;
    // 1.26.1 (LA POLICE EN FORMULE) : une formule ecrit souvent ce qu'elle lit dans la liste - << Police par defaut >>,
    // << Police des valeurs (JetBrains Mono) >> - et non Sans, Mono.
    const std::string folded = guide::fold(value);
    if (name.empty() || name == "Sans" || startsWithFolded(folded, "Police par d\xC3\xA9" "faut"))
        name = cfg ? std::string_view(cfg->defaultFont) : std::string_view{};
    else if (name == "Mono" || startsWithFolded(folded, "Police des valeurs"))
        name = cfg ? std::string_view(cfg->valueFont) : std::string_view{};
    if (name.empty()) return nullptr;
    for (const auto& r : a.resources)
        if (r.name == name) return r.kind() == MediaKind::Font && r.data ? &r : nullptr;
    // 1.26.1 : sinon, sans casse ni accents - le nom de la ressource, ou sans son extension (<< Carlito-Bold >>), ou le nom
    // de la police (<< Carlito Gras >>), ou sa famille (<< Carlito >> : son style Normal d'abord).
    const std::string want = guide::fold(name);
    const Resource* byFamily = nullptr;
    bool familyRegular = false;
    for (const auto& r : a.resources) {
        if (r.kind() != MediaKind::Font || !r.data) continue;
        const std::string rn = guide::fold(r.name);
        if (rn == want || guide::fold(stemExt(r.name).first) == want) return &r;
        const FoldedNames n = foldedNamesOf(r.data);
        if (!n.full.empty() && n.full == want) return &r;
        if (!n.family.empty() && n.family == want) {
            const bool regular = n.full == n.family;
            if (!byFamily || (regular && !familyRegular)) { byFamily = &r; familyRegular = regular; }
        }
    }
    return byFamily;
}

std::vector<std::string> quotedNames(std::string_view e) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < e.size(); ++i) {
        const char q = e[i];
        if (q != '\'' && q != '"') continue;
        std::string lit;
        std::size_t j = i + 1;
        for (; j < e.size(); ++j) {
            if (e[j] == '$' && j + 1 < e.size()) { lit += e[++j]; continue; }   // $' $$ : le caractere lui-meme
            if (e[j] == q) break;
            lit += e[j];
        }
        if (j >= e.size()) break;
        std::size_t b = 0, f = lit.size();
        while (b < f && lit[b] == ' ') ++b;
        while (f > b && lit[f - 1] == ' ') --f;
        std::string name = lit.substr(b, f - b);
        if (!name.empty() && std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
        i = j;
    }
    return out;
}

std::string formulaFontProblem(const Project& p, std::string_view name) {
    if (resolve(p, name)) return {};
    const std::string folded = guide::fold(name);
    if (name == "Sans" || name == "Mono" || startsWithFolded(folded, "Police par d\xC3\xA9" "faut") || startsWithFolded(folded, "Police des valeurs"))
        return {};   // une police du theme pas choisie : celle de l'IHM, comme une case sans formule
    // Dans la bibliotheque, pas dans le projet : le poste n'aura que les polices du projet.
    for (const auto& fam : families())
        for (const auto& f : fam.fonts)
            if (guide::fold(f.fullName()) == folded || guide::fold(f.label) == folded || guide::fold(resourceName(f)) == folded
                || guide::fold(choiceLabel(f)) == folded)
                return "'" + std::string(name) + "' est dans la biblioth\xC3\xA8que de polices, pas dans le projet : importe-la "
                       "(IHM \xE2\x80\xBA Ressources \xE2\x80\xBA Biblioth\xC3\xA8que de polices) - sinon le poste prend la police par d\xC3\xA9" "faut";
    return "'" + std::string(name) + "' : aucune police du projet ne porte ce nom (ni sa ressource, ni sa famille) - le poste prendra la "
           "police par d\xC3\xA9" "faut";
}

const Resource* resolve(const Project& p, std::string_view value) { return resolve(p.assets, &p.config, value); }

Usage usageOf(const Project& p, std::string_view name) {
    Usage u;
    if (name.empty()) return u;
    u.defaultFont = p.config.defaultFont == name;
    u.valueFont = p.config.valueFont == name;
    for (const auto& v : p.views)
        for (const auto& o : v.objects) {
            const Prop* prop = o.find("font");
            if (!prop) continue;
            const std::string& f = prop->value;
            if (f == name || ((f.empty() || f == "Sans") && u.defaultFont) || (f == "Mono" && u.valueFont)) ++u.objects;
        }
    const auto cites = [&](const std::vector<Prop>& props) {
        return std::any_of(props.begin(), props.end(), [&](const Prop& x) { return x.key == "font" && x.value == name; });
    };
    for (const auto& st : p.styles) {
        bool c = cites(st.props);
        for (const auto& t : st.targets) c = c || cites(t.props);
        u.styles += c ? 1u : 0u;
    }
    return u;
}

std::string usageText(const Usage& u) {
    std::string out;
    const auto add = [&out](const std::string& s) { out += (out.empty() ? "" : ", ") + s; };
    if (u.objects) add(std::to_string(u.objects) + (u.objects > 1 ? " objets" : " objet"));
    if (u.styles) add(std::to_string(u.styles) + (u.styles > 1 ? " styles" : " style"));
    if (u.defaultFont) add("police par d\xC3\xA9" "faut");
    if (u.valueFont) add("police des valeurs");
    return out.empty() ? std::string("aucun objet") : out;
}

std::string_view sampleText() {
    return "Pompe P1 en marche \xE2\x80\x94 12,4 bar \xE2\x80\x94 \xC3\xA0\xC3\xA9\xC3\xA8\xC3\xA7\xC3\xB9 \xE2\x82\xAC";
}

} // namespace hmi::fontlib
