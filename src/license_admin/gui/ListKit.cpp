// =============================================================================
//  license_admin/gui/ListKit.cpp - 1.47.0 : ce que les listes de l'Admin
//  partagent (voir ListKit.hpp)
// =============================================================================
#include "ListKit.hpp"

#include "AdminsKit.hpp"
#include "ClassicView.hpp"   // 1.52.0 : XPG Admin sans derogation (pas de lien vers Administrateurs)

#include "../../deploy/Crypto.hpp"
#include "../../ui/widgets/FilterMemory.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>

namespace license_admin::gui::listkit {

namespace {

namespace fs = std::filesystem;

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// Le fichier des listes, lu une fois, reecrit a chaque changement (quelques lignes : << cle=valeur >>, les valeurs deja
// echappees par ui::FilterMemory - jamais de retour a la ligne dedans).
struct Memory {
    std::string                        file;
    std::map<std::string, std::string> values;
    const void*                        owner{nullptr};
};
std::shared_ptr<Memory> gMemory;

void writeMemory(const Memory& m) {
    std::error_code ec;
    const fs::path target = P(m.file);
    if (m.values.empty()) {
        fs::remove(target, ec);
        return;
    }
    std::string text = "# XPG License Manager Admin : les filtres des listes (sur ce PC ; 1.47.0)\n";
    for (const auto& [k, v] : m.values) text += k + "=" + v + "\n";
    const fs::path tmp = target.parent_path() / (target.filename().u8string() + u8".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << text;
        if (!f) return;
    }
    fs::rename(tmp, target, ec);
    if (ec) fs::remove(tmp, ec);
}

bool isPlusKey(const Workspace& w, const std::string& key) {
    for (const auto& k : w.editorKeys())
        if (deploy::crypto::hex(k) == key) return true;
    return false;
}

const ui::Theme& refTheme() {
    static const ui::Theme t = ui::Theme::byName("XPG");
    return t;
}

std::string csvCell(const std::string& s) {
    std::string t = s;
    // Une formule neutralisee (Excel) : =, +, -, @ en tete.
    if (!t.empty() && (t[0] == '=' || t[0] == '+' || t[0] == '-' || t[0] == '@')) t = "'" + t;
    if (t.find_first_of(";\"\n\r") == std::string::npos) return t;
    std::string o = "\"";
    for (char c : t) o += c == '"' ? std::string("\"\"") : std::string(1, c);
    return o + "\"";
}

} // namespace

// ------------------------------------------------------------------------------------------ la memoire
std::string memoryFile(const std::string& folder) {
    const auto u = (P(folder) / "listes-filtres.txt").u8string();
    return std::string(u.begin(), u.end());
}

void installMemory(const std::string& folder, const void* owner) {
    if (folder.empty()) return;
    auto m = std::make_shared<Memory>();
    m->file = memoryFile(folder);
    m->owner = owner;
    std::ifstream f(P(m->file), std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        m->values[line.substr(0, eq)] = line.substr(eq + 1);
    }
    gMemory = m;
    ui::FilterMemory::Store s;
    std::weak_ptr<Memory> weak = m;
    s.read = [weak](const std::string& key) {
        const auto mm = weak.lock();
        if (!mm) return std::string();
        const auto it = mm->values.find(key);
        return it == mm->values.end() ? std::string() : it->second;
    };
    s.write = [weak](const std::string& key, const std::string& value) {
        const auto mm = weak.lock();
        if (!mm || key.empty() || key.find_first_of("=\r\n") != std::string::npos || value.find_first_of("\r\n") != std::string::npos) return;
        const auto it = mm->values.find(key);
        if (value.empty()) {
            if (it == mm->values.end()) return;
            mm->values.erase(it);
        } else {
            if (it != mm->values.end() && it->second == value) return;
            mm->values[key] = value;
        }
        writeMemory(*mm);
    };
    ui::FilterMemory::install(std::move(s));
}

void uninstallMemory(const void* owner) {
    if (!gMemory || gMemory->owner != owner) return;
    gMemory.reset();
    ui::FilterMemory::install({});
}

// ------------------------------------------------------------------------------------------ les admins
int adminColor(const Workspace& w, const std::string& key) {
    if (key.empty()) return -1;
    if (isPlusKey(w, key)) return 0;   // l'admin+ : le violet de l'Admin
    if (const auto& list = w.adminList()) {
        int i = 1;
        for (const auto& a : list->admins) {
            if (a.key == key) return a.active() ? i : -1;
            if (a.active()) ++i;
        }
    }
    return -1;
}

std::string adminName(const Workspace& w, const std::string& key) {
    if (const auto& list = w.adminList())
        if (const auto* a = list->byKey(key)) return a->name;
    if (isPlusKey(w, key)) return w.key().name.empty() ? std::string("admin+") : w.key().name;
    return {};
}

ui::UnfoldCard::Person person(const ShellHost& host, const std::string& key, const std::string& name, std::string count) {
    ui::UnfoldCard::Person p;
    p.name = name;
    p.count = std::move(count);
    int color = -1;
    if (host.workspace) color = adminColor(host.workspace(), key);
    p.color = kit::avatarColor(refTheme(), color);
    p.muted = color < 0;
    // 1.52.0 : XPG Admin sans derogation n'a pas Administrateurs - la personne se lit, sans lien
    if (!key.empty() && host.navigate && !(host.workspace && classic::managementOnly(host.workspace()))) {
        const ShellHost h = host;
        p.open = [h, key] {
            requestPageFocus("admins", "admin:" + key);
            h.navigate("admins");
        };
    }
    return p;
}

// ------------------------------------------------------------------------------------------ qui a fait quoi
void WhoDid::load() const {
    if (loaded_) return;
    loaded_ = true;
    lines_.clear();
    const JournalView v = w_.journalLines();
    const std::string plusName = w_.key().name.empty() ? std::string("admin+") : w_.key().name;
    for (const auto& r : v.rows) {
        Did d;
        d.key = r.line.key;
        d.who = r.who == "admin+" ? plusName : r.who == "le service" ? std::string("Le service") : r.who;
        d.when = r.line.parsed.when ? r.line.parsed.when : r.line.received;
        d.action = r.line.parsed.action;
        d.summary = r.line.parsed.summary.empty() ? r.line.parsed.object : r.line.parsed.summary;
        d.number = r.line.number;
        // l'objet, range dans le resume pour la recherche : on garde l'objet seul a part
        d.summary = r.line.parsed.object + "\t" + d.summary;
        lines_.push_back(std::move(d));
    }
}

std::optional<Did> WhoDid::last(const std::vector<std::string>& actions, const std::string& object) const {
    load();
    for (auto it = lines_.rbegin(); it != lines_.rend(); ++it) {
        const auto tab = it->summary.find('\t');
        const std::string obj = it->summary.substr(0, tab);
        if (obj != object) continue;
        if (!actions.empty() && std::find(actions.begin(), actions.end(), it->action) == actions.end()) continue;
        Did d = *it;
        d.summary = tab == std::string::npos ? std::string() : it->summary.substr(tab + 1);
        return d;
    }
    return std::nullopt;
}

std::vector<Did> WhoDid::history(const std::string& object) const {
    load();
    std::vector<Did> out;
    for (const auto& l : lines_) {
        const auto tab = l.summary.find('\t');
        if (l.summary.substr(0, tab) != object) continue;
        Did d = l;
        d.summary = tab == std::string::npos ? std::string() : l.summary.substr(tab + 1);
        out.push_back(std::move(d));
    }
    return out;
}

// ------------------------------------------------------------------------------------------ une erreur du service
std::string shortError(const std::string& full) {
    std::string s = full;
    for (const char* p : {"En direct : ", "Refus\xC3\xA9 : ", "Service : "})
        if (s.rfind(p, 0) == 0) s = s.substr(std::string(p).size());
    std::size_t glyphs = 0;
    for (const char ch : s) glyphs += (static_cast<unsigned char>(ch) & 0xC0) != 0x80 ? 1 : 0;
    if (glyphs <= 56) return s;
    struct Rule {
        const char* needle;
        const char* say;
    };
    static const Rule kRules[] = {
        {"pas de r\xC3\xA9ponse du service", "Le service ne r\xC3\xA9pond pas"},
        {"refuse la signature", "Le service refuse la signature de l'appel"},
        {"ne reconna\xC3\xAEt pas la cl\xC3\xA9", "Le service ne reconna\xC3\xAEt pas la cl\xC3\xA9 de ce poste"},
        {"droits d'admin ne permettent pas", "Refus\xC3\xA9 : droit manquant"},
        {"manque le droit", "Refus\xC3\xA9 : droit manquant"},
        {"acc\xC3\xA8s d'admin a \xC3\xA9t\xC3\xA9 retir\xC3\xA9", "Ton acc\xC3\xA8s d'admin est retir\xC3\xA9"},
        {"limite les appels", "Le service limite les appels pour l'instant"},
        {"a eu une erreur", "Erreur du service"},
        {"n'est pas r\xC3\xA9gl\xC3\xA9", "Le service n'est pas r\xC3\xA9gl\xC3\xA9"},
        {"introuvable sur le service", "Introuvable sur le service"},
        {"trop gros pour le service", "Trop gros pour le service"},
        {"conflit sur le service", "Conflit sur le service"},
    };
    for (const auto& r : kRules)
        if (s.find(r.needle) != std::string::npos) return r.say;
    // Autre chose : sa premiere proposition (avant << : >> ou << ( >>), sinon les premiers mots.
    for (const char* stop : {" : ", " ("})
        if (const auto at = s.find(stop); at != std::string::npos && at > 12 && at < 80) return s.substr(0, at);
    std::string out;
    for (std::size_t p = s.find(' '); p != std::string::npos && p < 70; p = s.find(' ', p + 1)) out = s.substr(0, p);
    return (out.empty() ? s.substr(0, 60) : out) + "\xE2\x80\xA6";
}

// ------------------------------------------------------------------------------------------ exporter
void exportRows(const ShellHost& host, const ui::TableView& table, std::vector<ui::RowIndex> rows, const std::string& fileName, const std::string& what) {
    const auto& m = table.model();
    if (!m || rows.empty() || !host.pickFile) return;
    std::string csv = "\xEF\xBB\xBF";
    bool first = true;
    const auto& cols = table.columns();
    for (std::size_t c = 0; c < cols.size(); ++c) {
        if (!cols[c].visible) continue;
        csv += (first ? "" : ";") + csvCell(cols[c].title.empty() ? m->headerText(c) : cols[c].title);
        first = false;
    }
    csv += "\r\n";
    for (const ui::RowIndex r : rows) {
        if (r >= m->rowCount()) continue;
        first = true;
        for (std::size_t c = 0; c < cols.size(); ++c) {
            if (!cols[c].visible) continue;
            csv += (first ? "" : ";") + csvCell(m->cellText(r, c));
            first = false;
        }
        csv += "\r\n";
    }
    ui::FilePick pick;
    pick.title = "Exporter " + std::to_string(rows.size()) + (rows.size() > 1 ? " lignes" : " ligne") + " pour Excel";
    pick.filters = {{"Tableau pour Excel (.csv)", "csv"}};
    pick.start = fileName;
    pick.save = true;
    const std::size_t n = rows.size();
    const ShellHost h = host;
    host.pickFile(pick, [h, csv, n, what](std::string path) {
        if (path.empty()) return;
        if (path.size() < 4 || path.compare(path.size() - 4, 4, ".csv") != 0) path += ".csv";
        std::ofstream f(P(path), std::ios::binary | std::ios::trunc);
        f << csv;
        if (!f) {
            if (h.report) h.report(OpResult::fail("impossible d'\xC3\xA9" "crire " + path));
            return;
        }
        OpResult r = OpResult::done(std::to_string(n) + " " + what + (n > 1 ? " export\xC3\xA9" "es" : " export\xC3\xA9" "e") + " (CSV pour Excel).");
        r.output = path;
        if (h.report) h.report(r);
    });
}

} // namespace license_admin::gui::listkit
