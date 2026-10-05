// =============================================================================
//  project/MacroFolders.cpp - les dossiers des macros
// =============================================================================
#include "MacroFolders.hpp"

#include "MacroSpec.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace project::macro {

namespace {

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

std::string folded(std::string_view s) {
    auto out = foldAccents(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::vector<std::string> segments(std::string_view path) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= path.size()) {
        const auto at = path.find('/', from);
        out.push_back(trim(path.substr(from, (at == std::string_view::npos ? path.size() : at) - from)));
        if (at == std::string_view::npos) break;
        from = at + 1;
    }
    if (out.size() == 1 && out.front().empty()) out.clear();
    return out;
}

std::string joinSegments(const std::vector<std::string>& parts, std::size_t from = 0) {
    std::string out;
    for (std::size_t i = from; i < parts.size(); ++i) out += (out.empty() ? "" : "/") + parts[i];
    return out;
}

std::string joinPath(std::string_view parent, std::string_view child) {
    if (parent.empty()) return std::string(child);
    if (child.empty()) return std::string(parent);
    return std::string(parent) + "/" + std::string(child);
}

// `path` rebase : ce qui etait sous `from` passe sous `to`. Faux si `path`
// n'est ni `from` ni en dessous.
bool rebase(std::string& path, std::string_view from, std::string_view to) {
    const auto p = segments(path), f = segments(from);
    if (f.empty() || p.size() < f.size()) return false;
    for (std::size_t i = 0; i < f.size(); ++i)
        if (folded(p[i]) != folded(f[i])) return false;
    path = joinPath(to, joinSegments(p, f.size()));
    return true;
}

bool contains(const std::vector<std::string>& list, std::string_view path) {
    return std::any_of(list.begin(), list.end(), [&](const std::string& x) { return sameFolder(x, path); });
}

void dedupe(std::vector<std::string>& list) {
    std::vector<std::string> out;
    for (auto& p : list)
        if (!p.empty() && !contains(out, p)) out.push_back(std::move(p));
    list = std::move(out);
}

} // namespace

// =================================================================== chemins ==
std::string folderLeaf(std::string_view path) {
    const auto s = segments(path);
    return s.empty() ? std::string{} : s.back();
}

std::string folderParent(std::string_view path) {
    auto s = segments(path);
    if (s.size() <= 1) return {};
    s.pop_back();
    return joinSegments(s);
}

bool sameFolder(std::string_view a, std::string_view b) {
    return folded(cleanFolder(a)) == folded(cleanFolder(b));
}

bool insideFolder(std::string_view folder, std::string_view parent) {
    const auto f = segments(folder), p = segments(parent);
    if (p.empty()) return true;
    if (f.size() < p.size()) return false;
    for (std::size_t i = 0; i < p.size(); ++i)
        if (folded(f[i]) != folded(p[i])) return false;
    return true;
}

std::string cleanFolder(std::string_view path) {
    const auto s = segments(path);
    for (const auto& part : s)
        if (part.empty()) return {};
    return joinSegments(s);
}

// ==================================================================== layout ==
std::vector<std::string> FolderLayout::subfolders(std::string_view parent) const {
    std::vector<std::string> out;
    for (const auto& f : folders)
        if (sameFolder(folderParent(f), parent)) out.push_back(f);
    return out;
}

std::vector<std::size_t> FolderLayout::macrosIn(std::string_view folder) const {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < macros.size(); ++i)
        if (sameFolder(macros[i].folder, folder)) out.push_back(i);
    return out;
}

std::size_t FolderLayout::countIn(std::string_view folder, bool deep) const {
    std::size_t n = 0;
    for (const auto& m : macros)
        if (deep ? insideFolder(m.folder, folder) : sameFolder(m.folder, folder)) ++n;
    return n;
}

std::string FolderLayout::folderOf(std::string_view macro) const {
    for (const auto& m : macros)
        if (folded(m.name) == folded(macro)) return m.folder;
    return {};
}

bool FolderLayout::hasFolder(std::string_view path) const {
    return contains(folders, path);
}

// ================================================================ le fichier ==
bool MacroFolders::load() {
    folders_.clear();
    placements_.clear();
    if (file_.empty()) return true;
    std::ifstream in(file_, std::ios::binary);
    if (!in) return true;
    std::ostringstream ss;
    ss << in.rdbuf();
    parse(ss.str());
    return true;
}

void MacroFolders::parse(std::string_view text) {
    folders_.clear();
    placements_.clear();
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto nl = text.find('\n', from);
        const auto line = trim(text.substr(from, (nl == std::string_view::npos ? text.size() : nl) - from));
        from = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        if (line.empty() || line.front() == '#') continue;
        std::vector<std::string> f;
        std::size_t at = 0;
        while (at <= line.size()) {
            const auto semi = line.find(';', at);
            f.push_back(trim(std::string_view(line).substr(at, (semi == std::string::npos ? line.size() : semi) - at)));
            if (semi == std::string::npos) break;
            at = semi + 1;
        }
        if (f.empty()) continue;
        const auto kind = folded(f[0]);
        if (kind == "dossier" && f.size() >= 2) {
            const auto p = cleanFolder(f[1]);
            if (!p.empty() && !contains(folders_, p)) folders_.push_back(p);
        } else if (kind == "macro" && f.size() >= 2) {
            const std::string folder = f.size() >= 3 ? cleanFolder(f[2]) : std::string{};
            if (placementOf(f[1]) < 0) placements_.emplace_back(f[1], folder);
        }
    }
}

std::string MacroFolders::render() const {
    std::ostringstream out;
    out << "# Les dossiers des macros : l'onglet Macros et l'arbre API > Macros.\n"
           "# Un dossier n'a aucun effet sur les noms ni sur RunMacro : c'est un rangement.\n"
           "#   dossier ; <chemin>              un dossier (meme vide), dans l'ordre d'affichage\n"
           "#   macro ; <nom> ; <chemin>        ou est rangee une macro (vide : a la racine)\n"
           "# Une macro absente d'ici va dans le dossier de sa ligne \"#! categorie\".\n";
    for (const auto& f : folders_) out << "dossier ; " << f << '\n';
    for (const auto& [name, folder] : placements_) out << "macro ; " << name << " ; " << folder << '\n';
    return out.str();
}

core::Status MacroFolders::save() const {
    if (file_.empty()) return core::fail(core::ErrorCode::FileUnreadable, "dossiers.txt : aucun chemin");
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(file_).parent_path(), ec);
    std::ofstream out(file_, std::ios::binary | std::ios::trunc);
    if (!out) return core::fail(core::ErrorCode::FileUnreadable, "ecriture impossible", file_);
    out << render();
    if (!out) return core::fail(core::ErrorCode::FileUnreadable, "ecriture incomplete", file_);
    return core::ok();
}

void MacroFolders::setMacros(std::vector<std::pair<std::string, std::string>> nameAndCategory) {
    macros_ = std::move(nameAndCategory);
}

int MacroFolders::placementOf(std::string_view name) const {
    for (std::size_t i = 0; i < placements_.size(); ++i)
        if (folded(placements_[i].first) == folded(name)) return static_cast<int>(i);
    return -1;
}

FolderLayout MacroFolders::arrange() const {
    FolderLayout layout;
    // 1. Le dossier de chaque macro : sa ligne, sinon sa categorie.
    std::vector<FolderLayout::Entry> placed, others;
    for (const auto& [name, category] : macros_) {
        const int at = placementOf(name);
        FolderLayout::Entry e{name, at >= 0 ? placements_[static_cast<std::size_t>(at)].second : cleanFolder(category)};
        (at >= 0 ? placed : others).push_back(std::move(e));
    }
    // Les rangees d'abord, dans l'ordre du fichier ; les autres par nom.
    std::sort(placed.begin(), placed.end(), [this](const FolderLayout::Entry& a, const FolderLayout::Entry& b) {
        return placementOf(a.name) < placementOf(b.name);
    });
    std::sort(others.begin(), others.end(),
              [](const FolderLayout::Entry& a, const FolderLayout::Entry& b) { return folded(a.name) < folded(b.name); });
    layout.macros = std::move(placed);
    for (auto& e : others) layout.macros.push_back(std::move(e));

    // 2. Les dossiers : ceux du fichier, puis ceux que les macros impliquent,
    //    toujours avec leurs parents avant eux.
    std::vector<std::string> all;
    std::function<void(const std::string&)> add = [&](const std::string& path) {
        if (path.empty() || contains(all, path)) return;
        add(folderParent(path));
        all.push_back(path);
    };
    for (const auto& f : folders_) add(f);
    std::vector<std::string> implied;
    for (const auto& m : layout.macros)
        if (!m.folder.empty() && !contains(all, m.folder) && !contains(implied, m.folder)) implied.push_back(m.folder);
    std::sort(implied.begin(), implied.end(), [](const std::string& a, const std::string& b) { return folded(a) < folded(b); });
    for (const auto& f : implied) add(f);
    layout.folders = std::move(all);
    return layout;
}

void MacroFolders::materialize() {
    const auto layout = arrange();
    folders_ = layout.folders;
    std::vector<std::pair<std::string, std::string>> kept;
    for (const auto& m : layout.macros) kept.emplace_back(m.name, m.folder);
    // Les lignes des macros inconnues (livrees ailleurs, pas encore la) restent.
    for (const auto& p : placements_)
        if (std::none_of(kept.begin(), kept.end(), [&](const auto& k) { return folded(k.first) == folded(p.first); }))
            kept.push_back(p);
    placements_ = std::move(kept);
}

// ================================================================ les gestes ==
bool MacroFolders::addFolder(const std::string& path, std::string* why) {
    const auto p = cleanFolder(path);
    if (p.empty()) {
        if (why) *why = "un nom de dossier ne peut pas etre vide";
        return false;
    }
    if (p.find(';') != std::string::npos) {
        if (why) *why = "pas de ; dans un nom de dossier";
        return false;
    }
    if (arrange().hasFolder(p)) {
        if (why) *why = "le dossier " + p + " existe deja";
        return false;
    }
    materialize();
    folders_.push_back(p);
    dedupe(folders_);
    return true;
}

bool MacroFolders::renameFolder(const std::string& from, const std::string& to, std::string* why) {
    const auto f = cleanFolder(from), t = cleanFolder(to);
    const auto layout = arrange();
    if (f.empty() || !layout.hasFolder(f)) {
        if (why) *why = "le dossier " + from + " n'existe pas";
        return false;
    }
    if (t.empty() || t.find(';') != std::string::npos) {
        if (why) *why = t.empty() ? "un nom de dossier ne peut pas etre vide" : "pas de ; dans un nom de dossier";
        return false;
    }
    if (f == t) return true;
    if (layout.hasFolder(t) && !sameFolder(f, t)) {
        if (why) *why = "le dossier " + t + " existe deja";
        return false;
    }
    if (insideFolder(t, f) && !sameFolder(t, f)) {
        if (why) *why = "un dossier ne va pas dans lui-meme";
        return false;
    }
    materialize();
    for (auto& p : folders_) (void)rebase(p, f, t);
    for (auto& [name, folder] : placements_) (void)rebase(folder, f, t);
    dedupe(folders_);
    return true;
}

bool MacroFolders::removeFolder(const std::string& path, std::string* why) {
    const auto f = cleanFolder(path);
    if (f.empty() || !arrange().hasFolder(f)) {
        if (why) *why = "le dossier " + path + " n'existe pas";
        return false;
    }
    const auto parent = folderParent(f);
    materialize();
    std::vector<std::string> kept;
    for (auto p : folders_) {
        if (sameFolder(p, f)) continue;
        (void)rebase(p, f, parent);
        kept.push_back(std::move(p));
    }
    folders_ = std::move(kept);
    for (auto& [name, folder] : placements_) (void)rebase(folder, f, parent);
    dedupe(folders_);
    return true;
}

bool MacroFolders::moveFolder(const std::string& path, const std::string& parent, std::string* why) {
    const auto f = cleanFolder(path);
    const auto np = cleanFolder(parent);
    if (f.empty() || !arrange().hasFolder(f)) {
        if (why) *why = "le dossier " + path + " n'existe pas";
        return false;
    }
    if (!np.empty() && insideFolder(np, f)) {
        if (why) *why = "un dossier ne va pas dans lui-meme";
        return false;
    }
    const auto target = joinPath(np, folderLeaf(f));
    if (sameFolder(target, f)) return true;
    if (arrange().hasFolder(target)) {
        if (why) *why = "le dossier " + target + " existe deja";
        return false;
    }
    return renameFolder(f, target, why);
}

std::size_t MacroFolders::moveMacros(const std::vector<std::string>& names, const std::string& folder) {
    const auto target = cleanFolder(folder);
    materialize();
    std::size_t moved = 0;
    for (const auto& name : names) {
        const int at = placementOf(name);
        if (at < 0) continue;
        auto entry = placements_[static_cast<std::size_t>(at)];
        if (!sameFolder(entry.second, target)) ++moved;
        placements_.erase(placements_.begin() + at);
        entry.second = target;
        placements_.push_back(std::move(entry));
    }
    if (!target.empty() && !contains(folders_, target)) folders_.push_back(target);
    return moved;
}

bool MacroFolders::placeNear(const std::vector<std::string>& names, const std::string& anchor, bool after) {
    materialize();
    if (placementOf(anchor) < 0) return false;
    std::vector<std::pair<std::string, std::string>> moving;
    for (const auto& name : names) {
        if (folded(name) == folded(anchor)) continue;
        const int at = placementOf(name);
        if (at < 0) continue;
        moving.push_back(placements_[static_cast<std::size_t>(at)]);
        placements_.erase(placements_.begin() + at);
    }
    if (moving.empty()) return false;
    const int a = placementOf(anchor);
    const auto folder = placements_[static_cast<std::size_t>(a)].second;
    for (auto& m : moving) m.second = folder;
    placements_.insert(placements_.begin() + a + (after ? 1 : 0), moving.begin(), moving.end());
    return true;
}

void MacroFolders::place(const std::string& name, const std::string& folder) {
    // La ligne de la macro est posee AVANT d'ecrire le rangement : sans elle,
    // sa categorie ("#! categorie = Mes macros") imposait un dossier, qui
    // restait ensuite vide dans l'onglet (une macro restauree, creee depuis un
    // modele).
    const auto f = cleanFolder(folder);
    if (const int at = placementOf(name); at >= 0) placements_.erase(placements_.begin() + at);
    placements_.emplace_back(name, f);
    materialize();
    // Puis a la fin de son dossier.
    if (const int at = placementOf(name); at >= 0) placements_.erase(placements_.begin() + at);
    placements_.emplace_back(name, f);
    if (!f.empty() && !contains(folders_, f)) folders_.push_back(f);
}

void MacroFolders::forget(const std::string& name) {
    if (const int at = placementOf(name); at >= 0) placements_.erase(placements_.begin() + at);
    std::erase_if(macros_, [&](const auto& m) { return folded(m.first) == folded(name); });
}

void MacroFolders::renameMacro(const std::string& from, const std::string& to) {
    if (const int at = placementOf(from); at >= 0) placements_[static_cast<std::size_t>(at)].first = to;
    for (auto& m : macros_)
        if (folded(m.first) == folded(from)) m.first = to;
}

} // namespace project::macro
