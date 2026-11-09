// =============================================================================
//  session/SessionSync.cpp - 1.43.0 : voir SessionSync.hpp
// =============================================================================
#include "SessionSync.hpp"

#include "../deploy/Crypto.hpp"
#include "../hmi/HmiStore.hpp"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace session {

namespace {

using Bytes = std::vector<std::uint8_t>;
using BytesPtr = std::shared_ptr<const Bytes>;

BytesPtr bytesOf(std::string_view s) { return std::make_shared<const Bytes>(s.begin(), s.end()); }
std::string textOf(const BytesPtr& b) { return b ? std::string(b->begin(), b->end()) : std::string(); }

bool sameBytes(const BytesPtr& a, const BytesPtr& b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return *a == *b;
}

// Un fichier de texte (une difference de lignes) ou des octets (entier) ?
bool isText(const std::string& path) { return path.rfind("ressources/", 0) != 0; }

std::vector<std::string> splitLines(std::string_view s) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < s.size()) {
        const auto eol = s.find('\n', pos);
        if (eol == std::string_view::npos) {
            out.emplace_back(s.substr(pos));
            break;
        }
        out.emplace_back(s.substr(pos, eol - pos + 1));   // la fin de ligne comprise : rien ne se perd
        pos = eol + 1;
    }
    return out;
}

std::string joinLines(const std::vector<std::string>& lines) {
    std::string out;
    std::size_t n = 0;
    for (const auto& l : lines) n += l.size();
    out.reserve(n);
    for (const auto& l : lines) out += l;
    return out;
}

core::json::Value strings(const std::vector<std::string>& v) {
    core::json::Value a = core::json::Value::array();
    for (const auto& s : v) a.push(s);
    return a;
}

bool stringsOf(const core::json::Value& v, std::vector<std::string>& out) {
    out.clear();
    if (v.isNull()) return true;
    if (!v.isArray()) return false;
    for (const auto& s : v.items()) {
        if (!s.isString()) return false;
        out.push_back(s.asString());
    }
    return true;
}

core::json::Value ids(const std::vector<hmi::Id>& v) {
    core::json::Value a = core::json::Value::array();
    for (const auto id : v) a.push(static_cast<std::int64_t>(id));
    return a;
}

bool idsOf(const core::json::Value& v, std::vector<hmi::Id>& out) {
    out.clear();
    if (v.isNull()) return true;
    if (!v.isArray()) return false;
    for (const auto& x : v.items()) {
        const auto n = x.asInt(-1);
        if (n <= 0 || n > 0xFFFFFFFFll) return false;
        out.push_back(static_cast<hmi::Id>(n));
    }
    return true;
}

// La vue reduite a sa ligne de l'index (ce qui fait viewFileName et le dossier).
hmi::View stubOf(const hmi::View& v) {
    hmi::View s;
    s.id = v.id;
    s.name = v.name;
    s.folder = v.folder;
    s.role = v.role;
    return s;
}

// L'identifiant au debut d'un nom de fichier de vue ("vues/0003-Synoptique.vue" -> 3).
hmi::Id viewIdOfPath(const std::string& path) {
    const std::size_t at = path.rfind('/');
    const std::size_t k = at == std::string::npos ? 0 : at + 1;
    std::uint64_t v = 0;
    std::size_t i = k;
    while (i < path.size() && path[i] >= '0' && path[i] <= '9' && v < 0x100000000ull) {
        v = v * 10 + static_cast<std::uint64_t>(path[i] - '0');
        ++i;
    }
    if (i == k || i >= path.size() || path[i] != '-' || v == 0 || v >= 0x100000000ull) return hmi::kNoId;
    return static_cast<hmi::Id>(v);
}

// La place de `lines` dans `text` a partir de `from` ; la plus proche de `near` ; npos : nulle part.
std::size_t findRun(const std::vector<std::string>& text, const std::vector<std::string>& lines, std::size_t near) {
    if (lines.empty()) return std::min(near, text.size());
    if (lines.size() > text.size()) return std::string::npos;
    std::size_t best = std::string::npos, bestDist = std::string::npos;
    for (std::size_t i = 0; i + lines.size() <= text.size(); ++i) {
        bool ok = true;
        for (std::size_t k = 0; k < lines.size() && ok; ++k) ok = text[i + k] == lines[k];
        if (!ok) continue;
        const std::size_t dist = i > near ? i - near : near - i;
        if (dist < bestDist) { best = i; bestDist = dist; }
        if (dist == 0) break;
    }
    return best;
}

} // namespace

// ================================================================================================ ViewDelta ===
bool ViewDelta::empty() const noexcept { return !full && !shellChanged && objects.empty() && removed.empty() && !orderChanged; }

std::vector<hmi::Id> ViewDelta::touched() const {
    std::vector<hmi::Id> out;
    out.reserve(objects.size() + removed.size());
    for (const auto& o : objects) out.push_back(o.id);
    for (const auto id : removed) out.push_back(id);
    return out;
}

void encodeView(const ViewDelta& d, core::json::Value& header, std::string& data) {
    header = core::json::Value::object();
    header.set("op", "VUE");
    header.set("vue", static_cast<std::int64_t>(d.view));
    if (d.full) header.set("entiere", true);
    if (d.shellChanged) header.set("coque", true);
    if (!d.removed.empty()) header.set("supprimes", ids(d.removed));
    if (d.orderChanged) header.set("ordre", ids(d.order));
    // Le texte d'une vue : la vue elle-meme (entiere si ses proprietes changent ; sinon son nom et ses calques
    // seulement, pour que les objets se relisent a l'identique) et les seuls objets du changement.
    hmi::View mini;
    if (d.full || d.shellChanged) {
        mini = d.shell;
    } else {
        mini.id = d.view;
        mini.name = d.shell.name;
        mini.layers = d.shell.layers;
        mini.activeLayer = d.shell.activeLayer;
    }
    mini.id = d.view;
    mini.objects = d.objects;
    data = hmi::serializeView(mini);
}

bool decodeView(const core::json::Value& header, std::string_view data, ViewDelta& d, std::string* why) {
    const auto fail = [&](std::string w) { if (why) *why = std::move(w); return false; };
    d = ViewDelta{};
    const auto v = header.integer("vue", 0);
    if (v <= 0 || v > 0xFFFFFFFFll) return fail("VUE : la vue manque");
    d.view = static_cast<hmi::Id>(v);
    d.full = header.flag("entiere");
    d.shellChanged = header.flag("coque");
    if (!idsOf(header.at("supprimes"), d.removed)) return fail("VUE : supprimes illisible");
    d.orderChanged = header.has("ordre");
    if (!idsOf(header.at("ordre"), d.order)) return fail("VUE : ordre illisible");
    auto parsed = hmi::parseView(data);
    if (!parsed) return fail("VUE : " + parsed.error().message());
    hmi::View view = std::move(parsed.value());
    if (view.id != d.view) return fail("VUE : la vue du texte n'est pas celle de l'en-t\xC3\xAAte");
    d.objects = std::move(view.objects);
    view.objects.clear();
    d.shell = std::move(view);
    return true;
}

// ============================================================================================ les lignes ===
std::vector<LineHunk> diffLines(std::string_view base, std::string_view now) {
    const auto a = splitLines(base), b = splitLines(now);
    std::vector<LineHunk> out;
    // Le debut et la fin communs : un changement est presque toujours petit.
    std::size_t pre = 0;
    while (pre < a.size() && pre < b.size() && a[pre] == b[pre]) ++pre;
    std::size_t suf = 0;
    while (suf < a.size() - pre && suf < b.size() - pre && a[a.size() - 1 - suf] == b[b.size() - 1 - suf]) ++suf;
    const std::size_t n = a.size() - pre - suf, m = b.size() - pre - suf;
    if (n == 0 && m == 0) return out;
    // Le milieu : la plus longue suite commune (programmation dynamique) s'il reste raisonnable, sinon un seul
    // morceau (tout le milieu retire, tout le nouveau milieu ajoute).
    std::vector<std::pair<int, int>> ops;   // (0 egal, 1 retire, 2 ajoute ; l'indice dans a ou b)
    if (n * m <= 4'000'000) {
        std::vector<std::vector<std::uint32_t>> L(n + 1, std::vector<std::uint32_t>(m + 1, 0));
        for (std::size_t i = n; i-- > 0;)
            for (std::size_t j = m; j-- > 0;)
                L[i][j] = a[pre + i] == b[pre + j] ? L[i + 1][j + 1] + 1 : std::max(L[i + 1][j], L[i][j + 1]);
        std::size_t i = 0, j = 0;
        while (i < n || j < m) {
            if (i < n && j < m && a[pre + i] == b[pre + j]) { ops.emplace_back(0, static_cast<int>(i)); ++i; ++j; }
            else if (j < m && (i == n || L[i][j + 1] >= L[i + 1][j])) { ops.emplace_back(2, static_cast<int>(j)); ++j; }
            else { ops.emplace_back(1, static_cast<int>(i)); ++i; }
        }
    } else {
        for (std::size_t i = 0; i < n; ++i) ops.emplace_back(1, static_cast<int>(i));
        for (std::size_t j = 0; j < m; ++j) ops.emplace_back(2, static_cast<int>(j));
    }
    // Les morceaux : des suites de retraits et d'ajouts, avec 2 lignes de contexte de chaque cote.
    std::size_t ai = pre;   // la ligne courante dans a
    std::size_t k = 0;
    while (k < ops.size()) {
        if (ops[k].first == 0) { ++ai; ++k; continue; }
        LineHunk h;
        h.at = ai;
        for (std::size_t c = ai >= 2 ? ai - 2 : 0; c < ai; ++c) h.before.push_back(a[c]);
        while (k < ops.size() && ops[k].first != 0) {
            if (ops[k].first == 1) { h.removed.push_back(a[pre + static_cast<std::size_t>(ops[k].second)]); ++ai; }
            else h.added.push_back(b[pre + static_cast<std::size_t>(ops[k].second)]);
            ++k;
        }
        for (std::size_t c = ai; c < a.size() && c < ai + 2; ++c) h.after.push_back(a[c]);
        out.push_back(std::move(h));
    }
    return out;
}

bool patchLines(std::string& text, const std::vector<LineHunk>& hunks) {
    auto lines = splitLines(text);
    long shift = 0;   // ce que les morceaux deja poses ont ajoute ou retire avant le suivant
    for (const auto& h : hunks) {
        const std::size_t expected = static_cast<std::size_t>(std::max<long>(0, static_cast<long>(h.at) + shift));
        // D'abord le contexte entier, puis sans le contexte (des lignes voisines ont pu changer), puis le
        // contexte d'un seul cote pour un ajout seul.
        std::vector<std::string> full = h.before;
        full.insert(full.end(), h.removed.begin(), h.removed.end());
        full.insert(full.end(), h.after.begin(), h.after.end());
        std::size_t at = std::string::npos;
        if (const auto p = findRun(lines, full, expected >= h.before.size() ? expected - h.before.size() : 0); p != std::string::npos)
            at = p + h.before.size();
        if (at == std::string::npos && !h.removed.empty()) {
            at = findRun(lines, h.removed, expected);
        }
        if (at == std::string::npos && h.removed.empty()) {
            if (!h.before.empty()) {
                if (const auto p = findRun(lines, h.before, expected >= h.before.size() ? expected - h.before.size() : 0); p != std::string::npos)
                    at = p + h.before.size();
            }
            if (at == std::string::npos && !h.after.empty()) at = findRun(lines, h.after, expected);
            if (at == std::string::npos && h.before.empty() && h.after.empty()) at = std::min(expected, lines.size());
        }
        if (at == std::string::npos || at > lines.size()) return false;
        if (h.removed.size() > lines.size() - at) return false;
        for (std::size_t k = 0; k < h.removed.size(); ++k)
            if (lines[at + k] != h.removed[k]) return false;
        lines.erase(lines.begin() + static_cast<long>(at), lines.begin() + static_cast<long>(at + h.removed.size()));
        lines.insert(lines.begin() + static_cast<long>(at), h.added.begin(), h.added.end());
        shift = static_cast<long>(at) - static_cast<long>(h.at) + static_cast<long>(h.added.size()) - static_cast<long>(h.removed.size());
    }
    text = joinLines(lines);
    return true;
}

// ============================================================================================ le projet ===
void encodeProject(const ProjectDelta& d, core::json::Value& header, std::string& data) {
    header = core::json::Value::object();
    header.set("op", "FICHIERS");
    data.clear();
    core::json::Value files = core::json::Value::array();
    for (const auto& f : d.files) {
        core::json::Value x = core::json::Value::object();
        x.set("chemin", f.path);
        if (f.kind == FileDelta::Kind::Removed) {
            x.set("genre", "supprime");
        } else if (f.kind == FileDelta::Kind::Whole) {
            x.set("genre", "entier");
            x.set("octets", static_cast<std::int64_t>(f.whole.size()));
            data += f.whole;
        } else {
            x.set("genre", "lignes");
            core::json::Value hs = core::json::Value::array();
            for (const auto& h : f.hunks) {
                core::json::Value y = core::json::Value::object();
                y.set("a", static_cast<std::int64_t>(h.at));
                y.set("avant", strings(h.before));
                y.set("retire", strings(h.removed));
                y.set("ajoute", strings(h.added));
                y.set("apres", strings(h.after));
                hs.push(std::move(y));
            }
            x.set("morceaux", std::move(hs));
        }
        files.push(std::move(x));
    }
    header.set("fichiers", std::move(files));
    core::json::Value views = core::json::Value::array();
    for (const auto& [id, text] : d.newViews) {
        core::json::Value x = core::json::Value::object();
        x.set("vue", static_cast<std::int64_t>(id));
        x.set("octets", static_cast<std::int64_t>(text.size()));
        data += text;
        views.push(std::move(x));
    }
    header.set("vues", std::move(views));
}

bool decodeProject(const core::json::Value& header, std::string_view data, ProjectDelta& d, std::string* why) {
    const auto fail = [&](std::string w) { if (why) *why = "FICHIERS : " + std::move(w); return false; };
    d = ProjectDelta{};
    std::size_t pos = 0;
    const auto take = [&](std::int64_t n, std::string& out) {
        if (n < 0 || static_cast<std::uint64_t>(n) > data.size() - pos) return false;
        out.assign(data.substr(pos, static_cast<std::size_t>(n)));
        pos += static_cast<std::size_t>(n);
        return true;
    };
    for (const auto& x : header.at("fichiers").items()) {
        FileDelta f;
        f.path = x.str("chemin");
        // Un chemin du projet : relatif, sans remonter, hors des vues.
        if (f.path.empty() || f.path.size() > 260 || f.path.front() == '/' || f.path.find("..") != std::string::npos
            || f.path.find('\\') != std::string::npos || f.path.find(':') != std::string::npos || f.path.rfind("vues/", 0) == 0)
            return fail("chemin refus\xC3\xA9 : " + f.path);
        const std::string kind = x.str("genre");
        if (kind == "supprime") {
            f.kind = FileDelta::Kind::Removed;
        } else if (kind == "entier") {
            f.kind = FileDelta::Kind::Whole;
            if (!take(x.integer("octets", -1), f.whole)) return fail("octets manquants pour " + f.path);
        } else if (kind == "lignes") {
            f.kind = FileDelta::Kind::Patch;
            for (const auto& y : x.at("morceaux").items()) {
                LineHunk h;
                const auto at = y.integer("a", -1);
                if (at < 0) return fail("morceau illisible dans " + f.path);
                h.at = static_cast<std::size_t>(at);
                if (!stringsOf(y.at("avant"), h.before) || !stringsOf(y.at("retire"), h.removed) || !stringsOf(y.at("ajoute"), h.added)
                    || !stringsOf(y.at("apres"), h.after))
                    return fail("morceau illisible dans " + f.path);
                f.hunks.push_back(std::move(h));
            }
        } else {
            return fail("genre inconnu pour " + f.path);
        }
        d.files.push_back(std::move(f));
    }
    for (const auto& x : header.at("vues").items()) {
        const auto id = x.integer("vue", 0);
        if (id <= 0 || id > 0xFFFFFFFFll) return fail("vue neuve sans identifiant");
        std::string text;
        if (!take(x.integer("octets", -1), text)) return fail("octets manquants pour une vue neuve");
        d.newViews.emplace_back(static_cast<hmi::Id>(id), std::move(text));
    }
    if (pos != data.size()) return fail("octets en trop");
    return true;
}

std::map<std::string, BytesPtr> shellFiles(hmi::Project& p) {
    // Les vues reduites a leur ligne de l'index, le temps d'ecrire le reste : le projet n'est pas copie.
    std::vector<hmi::View> views;
    views.swap(p.views);
    p.views.reserve(views.size());
    for (const auto& v : views) p.views.push_back(stubOf(v));
    const hmi::Id next = p.nextId;
    p.nextId = 1;   // le compteur est a chacun (son bloc) : il ne passe pas
    std::vector<hmi::ProjectFile> files;
    try {
        files = hmi::serializeProject(p);
    } catch (...) {
        p.views.swap(views);
        p.nextId = next;
        throw;
    }
    p.views.swap(views);
    p.nextId = next;
    std::map<std::string, BytesPtr> out;
    for (auto& f : files) {
        if (f.path.rfind("vues/", 0) == 0) continue;
        if (f.path == "ihm.txt" && f.data) {
            // L'en-tete au format courant : des vues reduites ne doivent pas faire baisser le format ecrit.
            std::string text = textOf(f.data);
            const auto eol = text.find('\n');
            const auto at = text.find("(format ");
            if (at != std::string::npos && (eol == std::string::npos || at < eol)) {
                const auto close = text.find(')', at);
                if (close != std::string::npos) text.replace(at, close - at + 1, "(format " + std::to_string(hmi::kFormatVersion) + ")");
            }
            out[f.path] = bytesOf(text);
            continue;
        }
        out[f.path] = f.data;
    }
    return out;
}

// Relire le projet depuis ses fichiers hors des vues : les vues de `p` (entieres) a l'ordre et dans le dossier de
// l'index lu, les neuves (`fresh`) telles quelles ; le compteur, la corbeille et le cache des bibliotheques de `p`.
static bool rebuildFrom(hmi::Project& p, const std::map<std::string, BytesPtr>& files, const std::map<hmi::Id, const std::string*>& fresh,
                 ProjectApplied& done, std::string* why) {
    const auto reader = [&](const std::string& path, std::string& content) -> bool {
        if (path.rfind("vues/", 0) == 0) {
            const hmi::Id id = viewIdOfPath(path);
            if (const auto it = fresh.find(id); it != fresh.end()) { content = *it->second; return true; }
            if (const auto* v = p.view(id)) { content = hmi::serializeView(stubOf(*v)); return true; }
            if (id == hmi::kNoId) return false;
            hmi::View unknown;   // citee par l'index, pas encore ici : retiree apres la lecture, le rattrapage la donnera
            unknown.id = id;
            unknown.name = "Vue";
            content = hmi::serializeView(unknown);
            return true;
        }
        const auto it = files.find(path);
        if (it == files.end() || !it->second) return false;
        content = textOf(it->second);
        return true;
    };
    auto parsed = hmi::parseProject(reader);
    if (!parsed) {
        if (why) *why = "le projet re\xC3\xA7u ne se relit pas : " + parsed.error().message();
        return false;
    }
    hmi::Project np = std::move(parsed.value());
    std::unordered_map<hmi::Id, std::size_t> mine;
    for (std::size_t i = 0; i < p.views.size(); ++i) mine[p.views[i].id] = i;
    std::vector<bool> taken(p.views.size(), false);
    for (auto& v : np.views) {
        if (fresh.count(v.id) && !mine.count(v.id)) {
            done.addedViews.push_back(v.id);
            continue;
        }
        const auto it = mine.find(v.id);
        if (it == mine.end()) {
            v.id = hmi::kNoId;
            continue;
        }
        std::string folder = v.folder;
        v = std::move(p.views[it->second]);
        taken[it->second] = true;
        v.folder = std::move(folder);
    }
    np.views.erase(std::remove_if(np.views.begin(), np.views.end(), [](const hmi::View& v) { return v.id == hmi::kNoId; }),
                   np.views.end());
    for (std::size_t i = 0; i < p.views.size(); ++i) {
        if (taken[i]) continue;
        done.removedViews.push_back(p.views[i].id);
        done.removed.push_back(std::move(p.views[i]));
    }
    np.nextId = p.nextId;
    np.trash = std::move(p.trash);
    np.libraryCache = std::move(p.libraryCache);
    p = std::move(np);
    return true;
}

// ============================================================================================== le miroir ===
Mirror::ViewSnap Mirror::snap(const hmi::View& v) {
    ViewSnap s;
    s.shell = v;
    s.shell.objects.clear();
    s.order.reserve(v.objects.size());
    for (const auto& o : v.objects) {
        s.order.push_back(o.id);
        s.objects.emplace(o.id, o);
    }
    return s;
}

void Mirror::reset(hmi::Project& p) {
    views_.clear();
    for (const auto& v : p.views) views_[v.id] = snap(v);
    files_ = shellFiles(p);
}

bool Mirror::hasView(hmi::Id id) const { return views_.count(id) != 0; }

void Mirror::collect(hmi::Project& p, const std::set<hmi::Id>& touched, bool projectTouched,
                     std::vector<ViewDelta>& out, ProjectDelta& project) {
    collect(p, touched, projectTouched, out, project, nullptr);
}

// 1.45.0 : `previous` (non nul) recoit ce que les changements remplacent (le journal de la session).
void Mirror::collect(hmi::Project& p, const std::set<hmi::Id>& touched, bool projectTouched,
                     std::vector<ViewDelta>& out, ProjectDelta& project, Previous* previous) {
    if (previous) {
        for (const auto& [id, s] : views_) previous->viewNames[id] = s.shell.name;
        for (const auto& [path, data] : files_) {
            (void)data;
            previous->files.insert(path);
        }
    }
    // Des vues apparues ou disparues : le reste du projet (l'index) change aussi.
    std::set<hmi::Id> present;
    for (const auto& v : p.views) present.insert(v.id);
    for (const auto& [id, s] : views_) {
        (void)s;
        if (!present.count(id)) projectTouched = true;
    }
    for (const auto id : present)
        if (!views_.count(id)) projectTouched = true;

    for (const auto id : touched) {
        const auto* v = p.view(id);
        auto it = views_.find(id);
        if (!v || it == views_.end()) continue;
        ViewSnap& s = it->second;
        ViewDelta d;
        d.view = id;
        hmi::View shell = *v;
        shell.objects.clear();
        if (!(shell == s.shell)) {
            d.shellChanged = true;
            d.shell = shell;
        } else {
            d.shell.name = s.shell.name;
            d.shell.layers = s.shell.layers;
            d.shell.activeLayer = s.shell.activeLayer;
        }
        std::vector<hmi::Id> order;
        order.reserve(v->objects.size());
        std::unordered_set<hmi::Id> now;
        for (const auto& o : v->objects) {
            order.push_back(o.id);
            now.insert(o.id);
            const auto k = s.objects.find(o.id);
            if (k == s.objects.end() || !(k->second == o)) d.objects.push_back(o);
        }
        for (const auto& [oid, o] : s.objects) {
            (void)o;
            if (!now.count(oid)) d.removed.push_back(oid);
        }
        if (order != s.order) {
            d.orderChanged = true;
            d.order = order;
        }
        if (d.empty()) continue;
        if (previous) {   // 1.45.0 : la vue d'avant, avec les seuls objets touches
            hmi::View was = s.shell;
            was.objects.clear();
            for (const auto& o : d.objects)
                if (const auto k = s.objects.find(o.id); k != s.objects.end()) was.objects.push_back(k->second);
            for (const auto oid : d.removed)
                if (const auto k = s.objects.find(oid); k != s.objects.end()) was.objects.push_back(k->second);
            previous->views[id] = std::move(was);
        }
        if (!d.shellChanged) {
            // Les calques du miroir peuvent etre vieux d'un changement : ceux d'aujourd'hui relisent les objets.
            d.shell.layers = shell.layers;
            d.shell.activeLayer = shell.activeLayer;
        }
        s.shell = std::move(shell);
        s.order = std::move(order);
        for (const auto& o : d.objects) s.objects[o.id] = o;
        for (const auto oid : d.removed) s.objects.erase(oid);
        out.push_back(std::move(d));
    }

    if (!projectTouched) return;
    auto files = shellFiles(p);
    for (const auto& [path, data] : files) {
        const auto old = files_.find(path);
        if (old != files_.end() && sameBytes(old->second, data)) continue;
        FileDelta f;
        f.path = path;
        if (old != files_.end() && isText(path)) {
            f.kind = FileDelta::Kind::Patch;
            f.hunks = diffLines(textOf(old->second), textOf(data));
            if (f.hunks.empty()) continue;
        } else {
            f.kind = FileDelta::Kind::Whole;
            f.whole = textOf(data);
        }
        project.files.push_back(std::move(f));
    }
    for (const auto& [path, data] : files_) {
        (void)data;
        if (!files.count(path)) {
            FileDelta f;
            f.path = path;
            f.kind = FileDelta::Kind::Removed;
            project.files.push_back(std::move(f));
        }
    }
    files_ = std::move(files);
    for (const auto& v : p.views) {
        if (views_.count(v.id)) continue;
        project.newViews.emplace_back(v.id, hmi::serializeView(v));
        views_[v.id] = snap(v);
    }
    for (auto it = views_.begin(); it != views_.end();) it = present.count(it->first) ? std::next(it) : views_.erase(it);
}

bool Mirror::applyView(hmi::Project& p, const ViewDelta& d, std::string* why) {
    hmi::View* v = p.view(d.view);
    if (d.full) {
        hmi::View whole = d.shell;
        whole.id = d.view;
        whole.objects = d.objects;
        if (v) *v = whole;
        else p.views.push_back(whole);
        views_[d.view] = snap(whole);
        return true;
    }
    if (!v) {
        if (why) *why = "la vue " + std::to_string(d.view) + " n'existe pas ici";
        return false;
    }
    auto& s = views_[d.view];
    if (d.shellChanged) {
        std::vector<hmi::Object> objects;
        objects.swap(v->objects);
        *v = d.shell;
        v->id = d.view;
        v->objects.swap(objects);
        s.shell = d.shell;
        s.shell.objects.clear();
        s.shell.id = d.view;
    }
    // Les objets : remplaces, ajoutes, retires.
    std::unordered_map<hmi::Id, std::size_t> index;
    for (std::size_t i = 0; i < v->objects.size(); ++i) index[v->objects[i].id] = i;
    for (const auto& o : d.objects) {
        if (const auto it = index.find(o.id); it != index.end()) v->objects[it->second] = o;
        else {
            index[o.id] = v->objects.size();
            v->objects.push_back(o);
        }
        s.objects[o.id] = o;
    }
    if (!d.removed.empty()) {
        const std::unordered_set<hmi::Id> gone(d.removed.begin(), d.removed.end());
        v->objects.erase(std::remove_if(v->objects.begin(), v->objects.end(), [&](const hmi::Object& o) { return gone.count(o.id) != 0; }),
                         v->objects.end());
        for (const auto id : d.removed) s.objects.erase(id);
    }
    // L'ordre : celui de l'envoyeur pour les objets qu'il connait ; les autres (crees ici, pas encore partis)
    // restent derriere leur voisin d'avant.
    if (d.orderChanged) {
        std::unordered_map<hmi::Id, std::size_t> rank;
        for (std::size_t i = 0; i < d.order.size(); ++i) rank[d.order[i]] = i;
        std::vector<hmi::Object> sorted;
        sorted.reserve(v->objects.size());
        std::vector<hmi::Object> known, unknown;
        std::vector<hmi::Id> unknownAfter;   // le voisin d'avant (connu) de chaque inconnu
        hmi::Id lastKnown = hmi::kNoId;
        for (auto& o : v->objects) {
            if (rank.count(o.id)) { lastKnown = o.id; known.push_back(std::move(o)); }
            else { unknownAfter.push_back(lastKnown); unknown.push_back(std::move(o)); }
        }
        std::stable_sort(known.begin(), known.end(), [&](const hmi::Object& a, const hmi::Object& b) { return rank[a.id] < rank[b.id]; });
        std::size_t u = 0;
        // Les inconnus sans voisin connu d'abord.
        for (std::size_t i = 0; i < unknown.size(); ++i)
            if (unknownAfter[i] == hmi::kNoId) sorted.push_back(std::move(unknown[i]));
        for (auto& o : known) {
            const hmi::Id id = o.id;
            sorted.push_back(std::move(o));
            for (std::size_t i = 0; i < unknown.size(); ++i)
                if (unknownAfter[i] == id) sorted.push_back(std::move(unknown[i]));
        }
        (void)u;
        v->objects = std::move(sorted);
        s.order = d.order;
    } else {
        // L'ordre du miroir suit les ajouts et les retraits.
        std::vector<hmi::Id> order;
        for (const auto id : s.order)
            if (s.objects.count(id)) order.push_back(id);
        for (const auto& o : d.objects)
            if (std::find(order.begin(), order.end(), o.id) == order.end()) order.push_back(o.id);
        s.order = std::move(order);
    }
    return true;
}

bool Mirror::applyProject(hmi::Project& p, const ProjectDelta& d, ProjectApplied& done, std::string* why) {
    done = ProjectApplied{};
    auto files = shellFiles(p);
    for (const auto& f : d.files) {
        switch (f.kind) {
            case FileDelta::Kind::Removed:
                files.erase(f.path);
                files_.erase(f.path);
                break;
            case FileDelta::Kind::Whole: {
                auto b = bytesOf(f.whole);
                files[f.path] = b;
                files_[f.path] = b;
                break;
            }
            case FileDelta::Kind::Patch: {
                std::string now = textOf(files[f.path]);
                if (!patchLines(now, f.hunks)) {
                    done.conflicts.push_back(f.path);
                    break;
                }
                files[f.path] = bytesOf(now);
                std::string base = textOf(files_[f.path]);
                if (patchLines(base, f.hunks)) files_[f.path] = bytesOf(base);
                else files_[f.path] = files[f.path];   // le miroir s'aligne : les empreintes rattraperont l'ecart
                break;
            }
        }
    }
    std::map<hmi::Id, const std::string*> fresh;
    for (const auto& [id, text] : d.newViews) fresh[id] = &text;
    if (!rebuildFrom(p, files, fresh, done, why)) return false;
    for (const auto id : done.addedViews)
        if (const auto* v = p.view(id)) views_[id] = snap(*v);
    for (const auto id : done.removedViews) views_.erase(id);
    for (auto& [id, sn] : views_)
        if (const auto* v = p.view(id)) sn.shell.folder = v->folder;
    return true;
}

ViewDelta Mirror::fullView(const hmi::View& v) {
    ViewDelta d;
    d.view = v.id;
    d.full = true;
    d.shell = v;
    d.shell.objects.clear();
    d.objects = v.objects;
    d.orderChanged = true;
    for (const auto& o : v.objects) d.order.push_back(o.id);
    return d;
}

ViewDelta Mirror::objectsOf(const hmi::View& v, const std::vector<hmi::Id>& wanted) {
    ViewDelta d;
    d.view = v.id;
    d.shell.name = v.name;
    d.shell.layers = v.layers;
    d.shell.activeLayer = v.activeLayer;
    for (const auto id : wanted) {
        if (const auto* o = v.object(id)) d.objects.push_back(*o);
        else d.removed.push_back(id);
    }
    d.orderChanged = true;
    for (const auto& o : v.objects) d.order.push_back(o.id);
    return d;
}

bool Mirror::fileOf(hmi::Project& p, const std::string& path, FileDelta& out) {
    auto files = shellFiles(p);
    const auto it = files.find(path);
    out = FileDelta{};
    out.path = path;
    if (it == files.end()) {
        out.kind = FileDelta::Kind::Removed;
        return false;
    }
    out.kind = FileDelta::Kind::Whole;
    out.whole = textOf(it->second);
    return true;
}

bool mergeShell(hmi::Project& current, const hmi::Project& from, const hmi::Project& to) {
    hmi::Project f = from, t = to;
    f.views.clear();
    t.views.clear();
    for (const auto& v : current.views) {
        f.views.push_back(stubOf(v));
        t.views.push_back(stubOf(v));
    }
    const auto ff = shellFiles(f), tf = shellFiles(t);
    auto cf = shellFiles(current);
    std::set<std::string> paths;
    for (const auto& [k, v] : ff) { (void)v; paths.insert(k); }
    for (const auto& [k, v] : tf) { (void)v; paths.insert(k); }
    for (const auto& path : paths) {
        const auto a = ff.find(path), b = tf.find(path);
        const BytesPtr pa = a == ff.end() ? nullptr : a->second, pb = b == tf.end() ? nullptr : b->second;
        if (sameBytes(pa, pb)) continue;
        if (!pb) { cf.erase(path); continue; }
        if (!pa || !isText(path)) { cf[path] = pb; continue; }
        std::string text = textOf(cf[path]);
        if (!patchLines(text, diffLines(textOf(pa), textOf(pb)))) return false;
        cf[path] = bytesOf(text);
    }
    ProjectApplied done;
    std::string why;
    return rebuildFrom(current, cf, {}, done, &why);   // une lecture qui echoue ne touche a rien
}

std::map<std::string, std::string> Mirror::hashes(hmi::Project& p) {
    std::map<std::string, std::string> out;
    for (const auto& v : p.views) out["v:" + std::to_string(v.id)] = deploy::crypto::sha256Hex(hmi::serializeView(v));
    for (const auto& [path, data] : shellFiles(p)) {
        const std::string text = textOf(data);
        out["f:" + path] = deploy::crypto::sha256Hex(text);
    }
    return out;
}

void Mirror::adoptView(const hmi::View& v) { views_[v.id] = snap(v); }

void Mirror::adoptFile(hmi::Project& p, const std::string& path) {
    auto files = shellFiles(p);
    if (const auto it = files.find(path); it != files.end()) files_[path] = it->second;
    else files_.erase(path);
}

} // namespace session
