// =============================================================================
//  app/ai/AiVarFolders.cpp - le dossier des variables creees (voir AiVarFolders.hpp)
// =============================================================================
#include "AiVarFolders.hpp"

#include "../../hmi/HmiTypes.hpp"   // allFolders, folderChain, folderLeaf, folderParent

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace app::ai::varfold {

namespace {

const char* const kOpen = "\xC2\xAB ";
const char* const kClose = " \xC2\xBB";

bool blank(char c) { return c == ' ' || c == '\t'; }

std::string quoted(std::string_view s) { return std::string(kOpen) + std::string(s) + kClose; }

// Un texte montre dans un message : coupe s'il est long (jamais au milieu d'une lettre).
std::string shown(std::string_view s) {
    if (s.size() <= 80) return std::string(s);
    std::size_t cut = 77;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return std::string(s.substr(0, cut)) + "...";
}

// "a, b, c et d" ; au-dela de `max` : "a, b, c, d, ... (N en tout)".
std::string listOf(const std::vector<std::string>& items, std::size_t max = 4) {
    std::string s;
    const std::size_t n = std::min(items.size(), max);
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0) s += (i + 1 == n && n == items.size()) ? " et " : ", ";
        s += items[i];
    }
    if (items.size() > max) s += ", \xE2\x80\xA6 (" + std::to_string(items.size()) + " en tout)";
    return s;
}

std::string variablesCount(std::size_t n) { return std::to_string(n) + (n > 1 ? " variables" : " variable"); }

std::string trimmedText(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && blank(s[a])) ++a;
    while (b > a && blank(s[b - 1])) --b;
    return std::string(s.substr(a, b - a));
}

// Les dossiers connus : leur cle -> leur ecriture (la premiere vue), parents compris.
class Known {
public:
    void add(std::string_view path) {
        for (const auto& c : hmi::types::folderChain(path)) spelled_.emplace(key(c), c);
    }
    [[nodiscard]] bool has(std::string_view path) const { return spelled_.count(key(path)) > 0; }
    // Un chemin normalise, niveau par niveau : un niveau qui existe garde son ecriture, un niveau neuf la sienne.
    [[nodiscard]] std::string spell(std::string_view normalized) const {
        std::string out;
        std::size_t from = 0;
        while (from < normalized.size()) {
            const auto slash = normalized.find('/', from);
            const std::string_view part = normalized.substr(from, slash == std::string_view::npos ? std::string_view::npos : slash - from);
            const std::string candidate = out.empty() ? std::string(part) : out + "/" + std::string(part);
            const auto it = spelled_.find(key(candidate));
            out = it != spelled_.end() ? it->second : candidate;
            if (slash == std::string_view::npos) break;
            from = slash + 1;
        }
        return out;
    }

private:
    std::unordered_map<std::string, std::string> spelled_;
};

} // namespace

bool normalize(std::string_view raw, std::string& out, std::string* why) {
    out.clear();
    const auto refuse = [&](const std::string& text) {
        if (why) *why = "dossier " + quoted(shown(raw)) + " : " + text;
        out.clear();
        return false;
    };
    std::vector<std::string> parts;
    std::string part, tooLong;
    bool space = false;
    const auto flush = [&] {
        if (part.empty()) return;
        if (tooLong.empty() && part.size() > kMaxName) tooLong = part;
        parts.push_back(std::move(part));
        part.clear();
    };
    for (const char c : raw) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '/') {
            flush();
            space = false;
            continue;
        }
        if (blank(c)) {
            space = !part.empty();   // les blancs du debut tombent ; ceux du milieu deviennent un seul
            continue;
        }
        if (u < 0x20 || u == 0x7F) return refuse("un caract\xC3\xA8re de contr\xC3\xB4le n'est pas permis (des noms s\xC3\xA9par\xC3\xA9s par /)");
        if (std::string_view("\\:*?\"<>|").find(c) != std::string_view::npos)
            return refuse("le caract\xC3\xA8re " + quoted(std::string(1, c)) + " n'est pas permis (des noms s\xC3\xA9par\xC3\xA9s par /, sans \\ : * ? \" < > |)");
        if (space) part += ' ';
        space = false;
        part += c;
    }
    flush();
    if (!tooLong.empty()) return refuse("le nom " + quoted(shown(tooLong)) + " a plus de " + std::to_string(kMaxName) + " caract\xC3\xA8res");
    if (parts.size() > kMaxDepth)
        return refuse(std::to_string(kMaxDepth) + " niveaux au plus (il en a " + std::to_string(parts.size()) + ")");
    for (const auto& x : parts) {
        if (!out.empty()) out += '/';
        out += x;
    }
    if (out.size() > kMaxPath) return refuse(std::to_string(kMaxPath) + " caract\xC3\xA8res au plus pour le chemin entier");
    return true;
}

std::string key(std::string_view path) {
    std::string out;
    bool space = false;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const char c = path[i];
        if (blank(c)) {
            space = true;
            continue;
        }
        if (c == '/') {   // les blancs autour d'un / ne comptent pas
            space = false;
            out += '/';
            continue;
        }
        if (space && !out.empty() && out.back() != '/') out += ' ';
        space = false;
        const auto u = static_cast<unsigned char>(c);
        if (u == 0xC3 && i + 1 < path.size()) {
            // une lettre accentuee (U+00C0 a U+00DE, sauf le signe multiplie) : sa minuscule (E aigu -> e aigu)
            auto d = static_cast<unsigned char>(path[i + 1]);
            if (d >= 0x80 && d <= 0x9E && d != 0x97) d = static_cast<unsigned char>(d + 0x20);
            out += c;
            out += static_cast<char>(d);
            ++i;
            continue;
        }
        out += static_cast<char>(std::tolower(u));
    }
    return out;
}

std::string resolve(std::string_view normalized, const std::vector<std::string>& known) {
    Known k;
    for (const auto& f : known) k.add(f);
    return k.spell(normalized);
}

Prefix prefixOf(std::string_view name) {
    std::vector<std::string_view> tokens;
    std::size_t from = 0;
    while (from <= name.size()) {
        const auto us = name.find('_', from);
        const std::string_view t = name.substr(from, us == std::string_view::npos ? std::string_view::npos : us - from);
        if (!t.empty()) tokens.push_back(t);
        if (us == std::string_view::npos) break;
        from = us + 1;
    }
    if (tokens.size() < 2) return {};
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string_view t = tokens[i];
        if (std::none_of(t.begin(), t.end(), digit)) continue;
        if (std::all_of(t.begin(), t.end(), digit)) {
            if (i == 0) continue;   // des chiffres seuls en tete : rien a quoi les rattacher
            return {std::string(tokens[i - 1]) + "_" + std::string(t), true};
        }
        return {std::string(t), true};
    }
    return {std::string(tokens.front()), false};
}

Placement place(const hmi::Project& project, const std::vector<Request>& in) {
    Placement out;
    out.placed.resize(in.size());
    Known dirs;
    for (const auto& f : hmi::types::allFolders(project)) dirs.add(f);
    const auto create = [&](const std::string& path) {
        if (path.empty() || dirs.has(path)) return;
        out.created.push_back(path);
        dirs.add(path);
    };
    // 0. le dossier donne (celui de la variable ou de son operation), ou celui du << ranger >> qui la range.
    std::vector<std::size_t> open;
    for (std::size_t i = 0; i < in.size(); ++i) {
        Placed& at = out.placed[i];
        if (in[i].stored) {
            at.how = Placed::How::Stored;
            at.folder = dirs.spell(in[i].folder);
        } else if (!in[i].folder.empty()) {
            at.how = Placed::How::Given;
            at.folder = dirs.spell(in[i].folder);
            create(at.folder);
        } else {
            open.push_back(i);
        }
    }
    if (!open.empty()) {
        // 1. le dossier ou vivent deja les variables du meme prefixe (le plus garni ; a egalite, le premier rencontre) -
        // celles du projet et celles de cette operation qui ont un dossier donne.
        std::set<std::string> wanted;
        for (const auto i : open)
            if (const Prefix px = prefixOf(in[i].name); !px.text.empty()) wanted.insert(key(px.text));
        struct Home {
            std::size_t count{0}, first{0};
            std::string folder;
        };
        std::unordered_map<std::string, std::map<std::string, Home>> homes;   // prefixe -> dossier (sa cle) -> ...
        std::size_t seen = 0;
        const auto live = [&](std::string_view name, const std::string& folder) {
            const std::string fk = key(folder);
            if (fk.empty()) return;
            const Prefix px = prefixOf(name);
            if (px.text.empty()) return;
            const std::string pk = key(px.text);
            if (!wanted.count(pk)) return;
            Home& h = homes[pk][fk];
            if (h.count++ == 0) {
                h.folder = folder;
                h.first = seen++;
            }
        };
        if (!wanted.empty()) {
            for (const auto& v : project.programs.variables) live(v.name, v.folder);
            for (std::size_t i = 0; i < in.size(); ++i)
                if (out.placed[i].how == Placed::How::Given) live(in[i].name, out.placed[i].folder);
        }
        std::vector<std::size_t> rest;
        for (const auto i : open) {
            const Prefix px = prefixOf(in[i].name);
            const auto it = px.text.empty() ? homes.end() : homes.find(key(px.text));
            if (it == homes.end() || it->second.empty()) {
                rest.push_back(i);
                continue;
            }
            const Home* best = nullptr;
            for (const auto& [fk, h] : it->second)
                if (!best || h.count > best->count || (h.count == best->count && h.first < best->first)) best = &h;
            out.placed[i] = Placed{best->folder, Placed::How::Existing, px.text};
        }
        // 2. un prefixe commun a au moins deux variables creees ensemble (sans dossier) : un dossier a son nom, a la racine.
        std::vector<std::string> order;
        std::unordered_map<std::string, std::vector<std::size_t>> groups;
        for (const auto i : rest) {
            const Prefix px = prefixOf(in[i].name);
            if (px.text.empty() || px.text.size() > kMaxName) continue;
            const std::string pk = key(px.text);
            auto& g = groups[pk];
            if (g.empty()) order.push_back(pk);
            g.push_back(i);
        }
        // 2 bis (1.22.1) : au moins trois equipements d'une meme famille crees ensemble (PompeG_1, PompeG_2, PompeG_3 : la
        // famille PompeG) - un dossier de famille et un sous-dossier par equipement (PompeG/PompeG_1), plutot que des dizaines
        // de dossiers a la racine. Une famille de moins de 3 caracteres (P1, P2 : << P >>) n'en fait pas.
        const auto familyOf = [](std::string_view prefix) {
            std::size_t e = prefix.size();
            while (e > 0 && (std::isdigit(static_cast<unsigned char>(prefix[e - 1])) || prefix[e - 1] == '_' || prefix[e - 1] == '-' || prefix[e - 1] == ' ')) --e;
            return std::string(prefix.substr(0, e));
        };
        std::unordered_map<std::string, std::size_t> families;   // la famille (sa cle) -> ses equipements crees ensemble
        for (const auto& pk : order)
            if (const auto& g = groups[pk]; g.size() >= 2)
                if (const Prefix px = prefixOf(in[g.front()].name); px.equipment)
                    if (const std::string fam = familyOf(px.text); fam.size() >= 3) ++families[key(fam)];
        for (const auto& pk : order) {
            const auto& g = groups[pk];
            if (g.size() < 2) continue;
            const Prefix gpx = prefixOf(in[g.front()].name);
            const std::string text = gpx.text;
            std::string where = text;
            if (const std::string fam = familyOf(text); gpx.equipment && fam.size() >= 3 && families[key(fam)] >= 3) where = fam + "/" + text;
            const std::string folder = dirs.spell(where);
            create(folder);
            for (const auto i : g) out.placed[i] = Placed{folder, Placed::How::Common, text};
        }
        // 3. le reste : la racine (comme avant).
    }

    // Ce qui est deduit est DIT : un avertissement par dossier deduit (les premiers), puis le compte du reste.
    struct Said {
        std::string              folder, prefix;
        Placed::How              how{Placed::How::Root};
        std::vector<std::string> names;
    };
    std::vector<Said> said;
    std::unordered_map<std::string, std::size_t> saidAt;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const Placed& at = out.placed[i];
        if (at.how != Placed::How::Existing && at.how != Placed::How::Common) continue;
        const std::string k = std::string(at.how == Placed::How::Existing ? "1|" : "2|") + key(at.folder) + "|" + key(at.prefix);
        const auto [it, added] = saidAt.emplace(k, said.size());
        if (added) said.push_back(Said{at.folder, at.prefix, at.how, {}});
        said[it->second].names.push_back(in[i].name);
    }
    constexpr std::size_t kMaxSaid = 8;
    for (std::size_t k = 0; k < said.size() && k < kMaxSaid; ++k) {
        const Said& s = said[k];
        out.warnings.push_back(listOf(s.names) + (s.names.size() > 1 ? " : rang\xC3\xA9" "es dans " : " : rang\xC3\xA9" "e dans ") + quoted(s.folder)
                               + " (aucun dossier donn\xC3\xA9 : d\xC3\xA9" "duit du pr\xC3\xA9" "fixe " + s.prefix
                               + (s.how == Placed::How::Existing ? std::string(", l\xC3\xA0 o\xC3\xB9 sont d\xC3\xA9j\xC3\xA0 ses variables).")
                                                                 : ", commun \xC3\xA0 " + std::to_string(s.names.size()) + " variables cr\xC3\xA9\xC3\xA9" "es ensemble)."));
    }
    if (said.size() > kMaxSaid) {
        std::size_t more = 0;
        for (std::size_t k = kMaxSaid; k < said.size(); ++k) more += said[k].names.size();
        out.warnings.push_back("\xE2\x80\xA6 et " + variablesCount(more) + " de plus, rang\xC3\xA9" "es dans " + std::to_string(said.size() - kMaxSaid)
                               + " autres dossiers d\xC3\xA9" "duits du pr\xC3\xA9" "fixe de leur nom.");
    }

    // LE MELANGE : un dossier final (pas la racine) qui recevrait deux equipements ou plus, chacun avec deux variables au
    // moins (celles qui y sont deja comprises), dont une variable creee ici.
    struct Bin {
        std::string                        folder;
        std::vector<std::string>           order;   // les prefixes (leur cle), dans l'ordre de rencontre
        std::map<std::string, std::size_t> count;
        std::map<std::string, std::string> text;
        std::set<std::string>              fresh;   // ceux d'une variable creee ici
    };
    std::vector<Bin> bins;
    std::unordered_map<std::string, std::size_t> binOf;
    const auto drop = [](Bin& b, const Prefix& px) {
        const std::string pk = key(px.text);
        if (b.count[pk]++ == 0) {
            b.order.push_back(pk);
            b.text[pk] = px.text;
        }
        return pk;
    };
    // les dossiers qui recoivent une variable creee (d'un equipement) ; puis ce qui y est deja, puis ce qui arrive
    const auto counted = [&](std::size_t i) { return out.placed[i].how != Placed::How::Stored && !out.placed[i].folder.empty() && prefixOf(in[i].name).equipment; };
    for (std::size_t i = 0; i < in.size(); ++i)
        if (counted(i) && binOf.emplace(key(out.placed[i].folder), bins.size()).second) bins.push_back(Bin{out.placed[i].folder, {}, {}, {}, {}});
    if (!bins.empty())
        for (const auto& v : project.programs.variables) {
            if (v.folder.empty()) continue;
            const auto it = binOf.find(key(v.folder));
            if (it == binOf.end()) continue;
            const Prefix px = prefixOf(v.name);
            if (px.equipment) (void)drop(bins[it->second], px);
        }
    for (std::size_t i = 0; i < in.size(); ++i)
        if (counted(i)) {
            Bin& b = bins[binOf.at(key(out.placed[i].folder))];
            b.fresh.insert(drop(b, prefixOf(in[i].name)));
        }
    for (const auto& b : bins) {
        std::vector<std::string> mixed;
        bool fresh = false;
        for (const auto& pk : b.order)
            if (b.count.at(pk) >= 2) {
                mixed.push_back(pk);
                fresh = fresh || b.fresh.count(pk) > 0;
            }
        if (mixed.size() < 2 || !fresh) continue;
        // la separation proposee : un sous-dossier par equipement (si le dossier porte deja le nom de l'un d'eux : a cote)
        std::string base = b.folder;
        const std::string leaf = key(hmi::types::folderLeaf(b.folder));
        if (std::find(mixed.begin(), mixed.end(), leaf) != mixed.end()) base = hmi::types::folderParent(b.folder);
        std::vector<std::string> names, targets;
        for (const auto& pk : mixed) {
            const std::string& t = b.text.at(pk);
            names.push_back(t);
            targets.push_back(quoted(dirs.spell(base.empty() ? t : base + "/" + t)));
        }
        out.warnings.push_back("Le dossier " + quoted(b.folder) + " m\xC3\xA9langerait " + std::to_string(mixed.size()) + " \xC3\xA9quipements (" + listOf(names)
                               + ") : s\xC3\xA9pare-les, par exemple " + listOf(targets) + ".");
    }
    return out;
}

std::vector<std::string> suggest(const hmi::Project& project, const std::vector<std::string>& names) {
    std::vector<Request> requests;
    requests.reserve(names.size());
    for (const auto& n : names) requests.push_back(Request{n, {}, false});
    const Placement pl = place(project, requests);
    std::vector<std::string> out;
    out.reserve(pl.placed.size());
    for (const auto& at : pl.placed) out.push_back(at.folder);
    return out;
}

std::string treeText(const hmi::Project& project, std::size_t maxBytes, std::size_t* folders, std::size_t* listed) {
    // Les dossiers, chacun une fois (par sa cle), dans l'ordre de l'arbre : un dossier, puis ses sous-dossiers.
    std::vector<std::pair<std::string, std::string>> all;   // (ordre, chemin)
    std::set<std::string> seen;
    for (const auto& f : hmi::types::allFolders(project)) {
        std::string k = key(f);
        if (k.empty() || !seen.insert(k).second) continue;
        for (auto& c : k)
            if (c == '/') c = '\x01';   // le / avant toute lettre : les sous-dossiers suivent leur parent
        all.emplace_back(std::move(k), f);
    }
    std::sort(all.begin(), all.end());
    // Les variables : celles de chaque dossier, et avec ses sous-dossiers ; celles de la racine.
    std::unordered_map<std::string, std::size_t> direct, total;
    std::size_t root = 0;
    for (const auto& v : project.programs.variables) {
        const std::string k = key(v.folder);
        if (k.empty()) {
            ++root;
            continue;
        }
        ++direct[k];
        for (std::size_t at = 0;;) {
            const auto slash = k.find('/', at);
            ++total[k.substr(0, slash)];
            if (slash == std::string::npos) break;
            at = slash + 1;
        }
    }
    if (folders) *folders = all.size();
    if (listed) *listed = 0;
    if (all.empty()) return "Dossiers des variables IHM : aucun (" + variablesCount(root) + " \xC3\xA0 la racine).\n";
    struct Line {
        std::size_t depth{0};
        std::string text;
    };
    std::vector<Line> lines;
    std::size_t deepest = 0;
    for (const auto& [order, f] : all) {
        const std::string k = key(f);
        const std::size_t depth = static_cast<std::size_t>(std::count(k.begin(), k.end(), '/'));
        const auto d = direct.find(k), t = total.find(k);
        const std::size_t nd = d == direct.end() ? 0 : d->second, nt = t == total.end() ? 0 : t->second;
        std::string text(2 * depth, ' ');
        text += trimmedText(hmi::types::folderLeaf(f)) + " (" + std::to_string(nd) + (nt > nd ? ", " + std::to_string(nt) + " en tout" : std::string{}) + ")\n";
        lines.push_back(Line{depth, std::move(text)});
        deepest = std::max(deepest, depth);
    }
    // Le budget : les premiers niveaux d'abord (le haut de l'arbre est toujours la), puis les suivants tant qu'ils tiennent.
    std::vector<bool> chosen(lines.size(), false);
    std::size_t used = 0, count = 0;
    bool full = false;
    for (std::size_t depth = 0; depth <= deepest && !full; ++depth)
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].depth != depth) continue;
            if (used + lines[i].text.size() > maxBytes) {
                full = true;
                break;
            }
            chosen[i] = true;
            used += lines[i].text.size();
            ++count;
        }
    if (listed) *listed = count;
    std::string s = "Dossiers des variables IHM (" + std::to_string(all.size())
                    + ", l'arborescence ; entre parenth\xC3\xA8ses : ses variables, puis le total avec ses sous-dossiers) :\n";
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (chosen[i]) s += lines[i].text;
    if (count < lines.size()) s += "... et " + std::to_string(lines.size() - count) + " autres dossiers (non list\xC3\xA9s : demande si besoin).\n";
    if (root > 0) s += "\xC3\x80 la racine (sans dossier) : " + variablesCount(root) + ".\n";
    return s;
}

} // namespace app::ai::varfold
