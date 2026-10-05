#include "HmiFolders.hpp"

#include "HmiTypes.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <set>

namespace hmi::fold {

namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

// "A/B" sous "A" ; "A/B/C" sous "A/B" -> le reste du chemin ("B", "C").
std::string replacePrefix(const std::string& folder, const std::string& from, const std::string& to) {
    if (sameFolder(folder, from)) return to;
    const std::string rest = folder.substr(from.size() + 1);
    return to.empty() ? rest : to + "/" + rest;
}

// Chaque element d'une liste, modifiable : `f(id, folder&)`.
template <class F>
void forEachFolder(Project& p, List list, F&& f) {
    switch (list) {
        case List::Views: case List::Popups: case List::Templates: case List::Headers: case List::Footers: case List::Symbols:
            for (auto& v : p.views)
                if (listOfView(v) == list) f(v.id, v.folder);
            break;
        case List::Scripts:   for (auto& s : p.programs.scripts) f(s.id, s.folder); break;
        case List::Types:     for (auto& t : p.programs.types) f(t.id, t.folder); break;
        case List::Styles:    for (auto& s : p.styles) f(s.id, s.folder); break;
        case List::Resources: for (auto& r : p.assets.resources) f(r.id, r.folder); break;
        case List::Count:     break;
    }
}

// Reordonner dans un vecteur : `ids` (dans l'ordre ou ils sont) avant ou apres `anchor`.
template <class T>
bool moveNearIn(std::vector<T>& vec, const std::vector<Id>& ids, Id anchor, bool after, const std::string& folder) {
    const auto moved = [&](const T& e) { return std::find(ids.begin(), ids.end(), e.id) != ids.end(); };
    if (std::find(ids.begin(), ids.end(), anchor) != ids.end()) return false;
    if (std::none_of(vec.begin(), vec.end(), [&](const T& e) { return e.id == anchor; })) return false;
    std::vector<T> moving, rest;
    for (auto& e : vec) (moved(e) ? moving : rest).push_back(std::move(e));
    auto it = std::find_if(rest.begin(), rest.end(), [&](const T& e) { return e.id == anchor; });
    if (after) ++it;
    for (auto& m : moving) m.folder = folder;
    rest.insert(it, std::make_move_iterator(moving.begin()), std::make_move_iterator(moving.end()));
    vec = std::move(rest);
    return true;
}

} // namespace

std::string_view key(List l) noexcept {
    switch (l) {
        case List::Views:     return "vues";
        case List::Popups:    return "popups";
        case List::Templates: return "ecrans_modeles";
        case List::Headers:   return "entetes";
        case List::Footers:   return "pieds";
        case List::Symbols:   return "symboles";
        case List::Scripts:   return "scripts";
        case List::Types:     return "types";
        case List::Styles:    return "styles";
        case List::Resources: return "ressources";
        case List::Count:     break;
    }
    return {};
}

std::optional<List> fromKey(std::string_view k) noexcept {
    for (int i = 0; i < static_cast<int>(List::Count); ++i)
        if (key(static_cast<List>(i)) == k) return static_cast<List>(i);
    return std::nullopt;
}

std::string_view label(List l) noexcept {
    switch (l) {
        case List::Views:     return "Vues";
        case List::Popups:    return "Popups";
        case List::Templates: return "\xC3\x89" "crans mod\xC3\xA8les";
        case List::Headers:   return "Mod\xC3\xA8les d'en-t\xC3\xAAte";
        case List::Footers:   return "Mod\xC3\xA8les de pied de page";
        case List::Symbols:   return "Symboles";
        case List::Scripts:   return "Scripts g\xC3\xA9n\xC3\xA9raux";
        case List::Types:     return "Types IHM";
        case List::Styles:    return "Styles";
        case List::Resources: return "Ressources";
        case List::Count:     break;
    }
    return {};
}

std::string noun(List l, std::size_t n) {
    const bool many = n > 1;
    switch (l) {
        case List::Views:     return many ? "vues" : "vue";
        case List::Popups:    return many ? "popups" : "popup";
        case List::Templates: return many ? "\xC3\xA9" "crans mod\xC3\xA8les" : "\xC3\xA9" "cran mod\xC3\xA8le";
        case List::Headers:   return many ? "en-t\xC3\xAAtes" : "en-t\xC3\xAAte";
        case List::Footers:   return many ? "pieds de page" : "pied de page";
        case List::Symbols:   return many ? "symboles" : "symbole";
        case List::Scripts:   return many ? "scripts" : "script";
        case List::Types:     return many ? "types" : "type";
        case List::Styles:    return many ? "styles" : "style";
        case List::Resources: return many ? "ressources" : "ressource";
        case List::Count:     break;
    }
    return many ? "\xC3\xA9l\xC3\xA9ments" : "\xC3\xA9l\xC3\xA9ment";
}

std::string agreed(List l, std::size_t n, std::string_view participle) {
    std::string s(participle);
    if (l == List::Views || l == List::Popups || l == List::Resources) s += 'e';   // les feminins
    if (n > 1) s += 's';
    return s;
}

List listOfView(const View& v) noexcept {
    if (v.role == "popup") return List::Popups;
    if (v.role == "modele") return List::Templates;
    if (v.role == "entete") return List::Headers;
    if (v.role == "pied") return List::Footers;
    if (v.role == "symbole") return List::Symbols;
    return List::Views;
}

bool sameFolder(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

bool inside(std::string_view folder, std::string_view parent) noexcept {
    if (parent.empty()) return true;
    if (sameFolder(folder, parent)) return true;
    return folder.size() > parent.size() && folder[parent.size()] == '/' && sameFolder(folder.substr(0, parent.size()), parent);
}

std::vector<Item> items(const Project& p, List list) {
    std::vector<Item> out;
    switch (list) {
        case List::Views: case List::Popups: case List::Templates: case List::Headers: case List::Footers: case List::Symbols:
            for (const auto& v : p.views)
                if (listOfView(v) == list) out.push_back({v.id, v.name, v.folder});
            break;
        case List::Scripts:   for (const auto& s : p.programs.scripts) out.push_back({s.id, s.name, s.folder}); break;
        case List::Types:     for (const auto& t : p.programs.types) out.push_back({t.id, t.name, t.folder}); break;
        case List::Styles:    for (const auto& s : p.styles) out.push_back({s.id, s.name, s.folder}); break;
        case List::Resources: for (const auto& r : p.assets.resources) out.push_back({r.id, r.name, r.folder}); break;
        case List::Count:     break;
    }
    return out;
}

bool contains(const Project& p, List list, Id id) {
    const auto all = items(p, list);
    return std::any_of(all.begin(), all.end(), [&](const Item& i) { return i.id == id; });
}

std::string folderOf(const Project& p, List list, Id id) {
    for (const auto& i : items(p, list))
        if (i.id == id) return i.folder;
    return {};
}

std::vector<std::string> allFolders(const Project& p, List list) {
    std::set<std::string> seen;
    std::vector<std::string> out;
    const auto add = [&](std::string_view f) {
        for (const auto& c : types::folderChain(f))
            if (seen.insert(upper(c)).second) out.push_back(c);
    };
    if (const auto it = p.listFolders.find(std::string(key(list))); it != p.listFolders.end())
        for (const auto& f : it->second) add(f);
    for (const auto& i : items(p, list)) add(i.folder);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return upper(a) < upper(b); });
    return out;
}

std::size_t countIn(const Project& p, List list, std::string_view folder, bool deep) {
    std::size_t n = 0;
    for (const auto& i : items(p, list))
        if (deep ? inside(i.folder, folder) : sameFolder(i.folder, folder)) ++n;
    return n;
}

bool addFolder(Project& p, List list, const std::string& path, std::string* why) {
    const std::string f = trimmed(path);
    if (!types::validFolder(f, why)) return false;
    for (const auto& existing : allFolders(p, list))
        if (sameFolder(existing, f)) return fail(why, "le dossier \xC2\xAB " + f + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0");
    p.listFolders[std::string(key(list))].push_back(f);
    return true;
}

bool renameFolder(Project& p, List list, const std::string& from, const std::string& to, std::string* why) {
    const std::string target = trimmed(to);
    if (from.empty()) return fail(why, "la racine ne se renomme pas");
    if (!types::validFolder(target, why)) return false;
    bool found = false;
    for (const auto& existing : allFolders(p, list)) {
        if (sameFolder(existing, from)) found = true;
        else if (sameFolder(existing, target)) return fail(why, "le dossier \xC2\xAB " + target + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0");
    }
    if (!found) return fail(why, "dossier introuvable : " + from);
    if (inside(target, from) && !sameFolder(target, from)) return fail(why, "un dossier ne va pas dans lui-m\xC3\xAAme");
    forEachFolder(p, list, [&](Id, std::string& folder) {
        if (inside(folder, from) && !folder.empty()) folder = replacePrefix(folder, from, target);
    });
    auto& explicitList = p.listFolders[std::string(key(list))];
    for (auto& f : explicitList)
        if (inside(f, from)) f = replacePrefix(f, from, target);
    if (std::none_of(explicitList.begin(), explicitList.end(), [&](const std::string& f) { return sameFolder(f, target); }))
        explicitList.push_back(target);          // le dossier renomme reste, meme vide
    return true;
}

bool removeFolder(Project& p, List list, const std::string& path) {
    if (path.empty()) return false;
    const std::string parent = types::folderParent(path);
    bool changed = false;
    forEachFolder(p, list, [&](Id, std::string& folder) {
        if (folder.empty() || !inside(folder, path)) return;
        folder = replacePrefix(folder, path, parent);
        changed = true;
    });
    const std::string k(key(list));
    if (auto it = p.listFolders.find(k); it != p.listFolders.end()) {
        auto& fl = it->second;
        std::vector<std::string> kept;
        for (const auto& f : fl) {
            if (sameFolder(f, path)) { changed = true; continue; }
            if (inside(f, path)) {
                kept.push_back(replacePrefix(f, path, parent));
                changed = true;
            } else {
                kept.push_back(f);
            }
        }
        fl = std::move(kept);
        if (fl.empty()) p.listFolders.erase(it);
    }
    return changed;
}

std::size_t moveToFolder(Project& p, List list, const std::vector<Id>& ids, const std::string& folder) {
    std::size_t n = 0;
    forEachFolder(p, list, [&](Id id, std::string& f) {
        if (std::find(ids.begin(), ids.end(), id) == ids.end() || sameFolder(f, folder)) return;
        f = folder;
        ++n;
    });
    return n;
}

bool moveFolder(Project& p, List list, const std::string& path, const std::string& parent, std::string* why) {
    if (path.empty()) return fail(why, "la racine ne se d\xC3\xA9place pas");
    if (inside(parent, path)) return fail(why, "un dossier ne va pas dans lui-m\xC3\xAAme");
    const std::string target = parent.empty() ? types::folderLeaf(path) : parent + "/" + types::folderLeaf(path);
    if (sameFolder(target, path)) return fail(why, "le dossier y est d\xC3\xA9j\xC3\xA0");
    return renameFolder(p, list, path, target, why);
}

bool moveNear(Project& p, List list, const std::vector<Id>& ids, Id anchor, bool after) {
    if (ids.empty() || !contains(p, list, anchor)) return false;
    for (const auto id : ids)
        if (!contains(p, list, id)) return false;
    const std::string folder = folderOf(p, list, anchor);
    switch (list) {
        case List::Views: case List::Popups: case List::Templates: case List::Headers: case List::Footers: case List::Symbols:
            return moveNearIn(p.views, ids, anchor, after, folder);
        case List::Scripts:   return moveNearIn(p.programs.scripts, ids, anchor, after, folder);
        case List::Types:     return moveNearIn(p.programs.types, ids, anchor, after, folder);
        case List::Styles:    return moveNearIn(p.styles, ids, anchor, after, folder);
        case List::Resources: return moveNearIn(p.assets.resources, ids, anchor, after, folder);
        case List::Count:     break;
    }
    return false;
}

} // namespace hmi::fold
