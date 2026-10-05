// =============================================================================
//  app/TablePaste.cpp - coller un tableau d'Excel (lot 20) : voir l'en-tete.
// =============================================================================
#include "TablePaste.hpp"

#include "../core/Command.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace app::paste {

namespace {

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    // Une espace insecable (Excel en met parfois en tete d'un nombre) : UTF-8 C2 A0.
    std::string out(s.substr(a, b - a));
    while (out.size() >= 2 && static_cast<unsigned char>(out[0]) == 0xC2 && static_cast<unsigned char>(out[1]) == 0xA0) out.erase(0, 2);
    while (out.size() >= 2 && static_cast<unsigned char>(out[out.size() - 2]) == 0xC2
           && static_cast<unsigned char>(out.back()) == 0xA0) out.resize(out.size() - 2);
    return out;
}

std::string plural(std::size_t n, const std::string& one, const std::string& many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) out += ", ";
        out += items[i];
    }
    return out;
}

// Les lettres accentuees du francais (UTF-8, deux octets) et leur lettre de base.
char baseLetter(unsigned char lead, unsigned char next) {
    if (lead != 0xC3) return 0;
    switch (next) {
        case 0xA0: case 0xA1: case 0xA2: case 0xA4: case 0x80: case 0x81: case 0x82: case 0x84: return 'a';
        case 0xA7: case 0x87: return 'c';
        case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0x88: case 0x89: case 0x8A: case 0x8B: return 'e';
        case 0xAE: case 0xAF: case 0x8E: case 0x8F: return 'i';
        case 0xB4: case 0xB6: case 0x94: case 0x96: return 'o';
        case 0xB9: case 0xBB: case 0xBC: case 0x99: case 0x9B: case 0x9C: return 'u';
        default: return 0;
    }
}

} // namespace

Grid parseGrid(std::string_view text) {
    Grid grid;
    // Un BOM UTF-8 en tete (un fichier copie depuis le Bloc-notes).
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);
    std::vector<std::string> row;
    std::string cell;
    bool quoted = false, cellStarted = false;
    const auto endCell = [&] {
        row.push_back(cell);
        cell.clear();
        cellStarted = false;
    };
    const auto endRow = [&] {
        endCell();
        grid.push_back(std::move(row));
        row.clear();
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (quoted) {
            if (ch == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') { cell += '"'; ++i; }
                else quoted = false;
            } else {
                cell += ch;
            }
            continue;
        }
        if (ch == '"' && !cellStarted && cell.empty()) { quoted = true; cellStarted = true; continue; }
        if (ch == '\t') { endCell(); continue; }
        if (ch == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            endRow();
            continue;
        }
        if (ch == '\n') { endRow(); continue; }
        cell += ch;
        cellStarted = true;
    }
    if (!cell.empty() || !row.empty() || cellStarted) endRow();
    // Les lignes toutes vides (la fin d'un collage, une ligne sautee) ne comptent pas.
    grid.erase(std::remove_if(grid.begin(), grid.end(), [](const std::vector<std::string>& r) {
                   return std::all_of(r.begin(), r.end(), [](const std::string& c) { return trim(c).empty(); });
               }),
               grid.end());
    return grid;
}

std::string normalizedTitle(std::string_view title) {
    std::string out;
    for (std::size_t i = 0; i < title.size(); ++i) {
        const auto c = static_cast<unsigned char>(title[i]);
        if (c >= 0x80) {
            if (i + 1 < title.size()) {
                if (const char b = baseLetter(c, static_cast<unsigned char>(title[i + 1]))) {
                    out += b;
                    ++i;
                    continue;
                }
            }
            continue;       // une autre lettre non ASCII : ignoree
        }
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    }
    return out;
}

Column column(std::string title, std::vector<std::string> aliases, int tableColumn,
              std::function<bool(const std::string&, const std::string&, std::string*)> set, bool key) {
    Column c;
    c.title = std::move(title);
    c.aliases = std::move(aliases);
    c.tableColumn = tableColumn;
    c.set = std::move(set);
    c.key = key;
    return c;
}

std::vector<std::string> keysFrom(const ui::TableView& table, std::size_t anchorViewRow, std::size_t keyColumn) {
    std::vector<std::string> out;
    const auto& m = table.model();
    if (!m) return out;
    for (std::size_t i = anchorViewRow; i < table.visibleRowCount(); ++i) out.push_back(m->cellText(table.viewRow(i), keyColumn));
    return out;
}

// ---------------------------------------------------------------------- run ----
Report run(const Target& target, std::string_view text, int anchorColumn, bool asNewRows) {
    Report rep;
    const Grid grid = parseGrid(text);
    if (grid.empty()) {
        rep.error = "le presse-papiers est vide";
        return rep;
    }
    const auto& cols = target.columns;
    int keyIndex = -1;
    for (std::size_t i = 0; i < cols.size(); ++i)
        if (cols[i].key) keyIndex = static_cast<int>(i);

    // 1. La premiere ligne est-elle faite de titres ?
    const auto matchTitle = [&](const std::string& cell) -> int {
        const std::string n = normalizedTitle(cell);
        if (n.empty()) return -1;
        for (std::size_t i = 0; i < cols.size(); ++i) {
            if (normalizedTitle(cols[i].title) == n) return static_cast<int>(i);
            for (const auto& a : cols[i].aliases)
                if (normalizedTitle(a) == n) return static_cast<int>(i);
        }
        return -1;
    };
    std::vector<int> mapping;           // case collee -> colonne de la cible (-1 : ignoree)
    {
        const auto& first = grid.front();
        std::vector<int> m;
        std::size_t matches = 0, nonEmpty = 0;
        bool keyMatched = false;
        for (const auto& cell : first) {
            const int c = matchTitle(cell);
            m.push_back(c);
            if (!trim(cell).empty()) ++nonEmpty;
            if (c >= 0) {
                ++matches;
                if (c == keyIndex) keyMatched = true;
            }
        }
        rep.titles = matches >= 2 || (matches == 1 && (keyMatched || matches == nonEmpty) && grid.size() > 1);
        if (rep.titles) {
            mapping = std::move(m);
            for (std::size_t i = 0; i < first.size(); ++i) {
                const std::string t = trim(first[i]);
                if (t.empty()) continue;
                if (mapping[i] < 0) rep.ignored.push_back(t);
                else if (!cols[static_cast<std::size_t>(mapping[i])].set && mapping[i] != keyIndex) rep.computed.push_back(cols[static_cast<std::size_t>(mapping[i])].title);
                else rep.recognized.push_back(cols[static_cast<std::size_t>(mapping[i])].title);
            }
        }
    }
    if (!rep.titles) {
        // 2. Sans titres : a partir de la colonne choisie, dans l'ordre de la table.
        std::vector<int> tableOrder;
        for (std::size_t i = 0; i < cols.size(); ++i)
            if (cols[i].tableColumn >= 0) tableOrder.push_back(static_cast<int>(i));
        std::sort(tableOrder.begin(), tableOrder.end(), [&](int a, int b) {
            return cols[static_cast<std::size_t>(a)].tableColumn < cols[static_cast<std::size_t>(b)].tableColumn;
        });
        std::size_t start = 0;
        if (anchorColumn >= 0)
            for (std::size_t i = 0; i < tableOrder.size(); ++i)
                if (cols[static_cast<std::size_t>(tableOrder[i])].tableColumn == anchorColumn) start = i;
        std::size_t width = 0;
        for (const auto& r : grid) width = std::max(width, r.size());
        for (std::size_t j = 0; j < width; ++j) mapping.push_back(start + j < tableOrder.size() ? tableOrder[start + j] : -1);
        if (start < tableOrder.size()) rep.startColumn = cols[static_cast<std::size_t>(tableOrder[start])].title;
    }
    int keyCell = -1;
    for (std::size_t j = 0; j < mapping.size(); ++j)
        if (mapping[j] == keyIndex && keyIndex >= 0) keyCell = static_cast<int>(j);
    // Les cases s'ecrivent DANS L'ORDRE DE LA CIBLE, pas dans celui d'Excel :
    // l'equipement avant l'adresse, quel que soit l'ordre des colonnes collees.
    std::vector<int> cellOf(cols.size(), -1);
    for (std::size_t j = 0; j < mapping.size(); ++j)
        if (mapping[j] >= 0 && cellOf[static_cast<std::size_t>(mapping[j])] < 0) cellOf[static_cast<std::size_t>(mapping[j])] = static_cast<int>(j);
    const bool keyed = keyCell >= 0;
    if (!keyed && asNewRows) {
        rep.error = "Coller en nouvelles lignes demande la colonne " + (keyIndex >= 0 ? cols[static_cast<std::size_t>(keyIndex)].title : std::string("Nom"));
        return rep;
    }
    if (!keyed && target.keysFromAnchor.empty()) {
        rep.error = "choisis d'abord la ligne o\xC3\xB9 coller (ou copie aussi la colonne "
                    + (keyIndex >= 0 ? cols[static_cast<std::size_t>(keyIndex)].title : std::string("Nom")) + ")";
        return rep;
    }

    // 3. Ligne par ligne.
    const std::size_t firstLine = rep.titles ? 1 : 0;
    for (std::size_t li = firstLine; li < grid.size(); ++li) {
        const auto& row = grid[li];
        const std::size_t lineNo = li + 1;
        ++rep.lines;
        std::string key;
        if (keyed) key = static_cast<std::size_t>(keyCell) < row.size() ? trim(row[static_cast<std::size_t>(keyCell)]) : std::string{};
        else {
            const std::size_t k = li - firstLine;
            if (k >= target.keysFromAnchor.size()) {
                ++rep.skipped;
                continue;
            }
            key = target.keysFromAnchor[k];
        }
        if (key.empty()) {
            ++rep.skipped;
            continue;
        }
        const Column* keyCol = keyIndex >= 0 ? &cols[static_cast<std::size_t>(keyIndex)] : nullptr;
        bool exists = target.exists && target.exists(key);
        if (asNewRows) {
            if (target.freeKey) key = target.freeKey(key);
            exists = false;
        }
        std::set<std::string> used;       // titres (normalises) deja pris en compte
        bool wrote = false;
        if (!exists) {
            if (!keyed || !target.create) {
                rep.refused.push_back({lineNo, key, keyCol ? keyCol->title : std::string{}, key, target.unknown, keyCol ? keyCol->tableColumn : -1});
                continue;
            }
            std::map<std::string, std::string> cells;
            for (std::size_t j = 0; j < mapping.size() && j < row.size(); ++j)
                if (mapping[j] >= 0 && static_cast<int>(j) != keyCell) {
                    const std::string v = trim(row[j]);
                    if (!v.empty()) cells[normalizedTitle(cols[static_cast<std::size_t>(mapping[j])].title)] = v;
                }
            Notes notes;
            std::vector<std::string> took;
            std::string why;
            const std::string made = target.create(key, cells, notes, took, &why);
            if (made.empty()) {
                rep.refused.push_back({lineNo, key, keyCol ? keyCol->title : std::string{}, key, why, keyCol ? keyCol->tableColumn : -1});
                continue;
            }
            key = made;
            ++rep.created;
            rep.createdKeys.push_back(key);
            for (const auto& t : took) used.insert(t);
            for (const auto& [title, reason] : notes) {
                const std::string n = normalizedTitle(title);
                used.insert(n);
                int tc = -1;
                std::string value;
                for (std::size_t j = 0; j < mapping.size() && j < row.size(); ++j)
                    if (mapping[j] >= 0 && normalizedTitle(cols[static_cast<std::size_t>(mapping[j])].title) == n) {
                        tc = cols[static_cast<std::size_t>(mapping[j])].tableColumn;
                        value = trim(row[j]);
                    }
                rep.refused.push_back({lineNo, key, title, value, reason, tc});
            }
        }
        for (std::size_t ci = 0; ci < cols.size(); ++ci) {
            const int cj = cellOf[ci];
            if (cj < 0 || cj == keyCell || static_cast<std::size_t>(cj) >= row.size()) continue;
            const std::size_t j = static_cast<std::size_t>(cj);
            const Column& c = cols[ci];
            if (!c.set) continue;                                  // calculee : jamais ecrite
            if (used.count(normalizedTitle(c.title))) continue;    // la creation l'a deja prise
            const std::string value = trim(row[j]);
            if (value.empty()) continue;                           // une case vide ne change rien
            std::string why;
            if (c.set(key, value, &why)) wrote = true;
            else rep.refused.push_back({lineNo, key, c.title, value, why.empty() ? std::string("refus\xC3\xA9" "e") : why, c.tableColumn});
        }
        if (exists && wrote) {
            ++rep.updated;
            rep.updatedKeys.push_back(key);
        } else if (exists) {
            ++rep.skipped;
        }
    }
    return rep;
}

// ------------------------------------------------------------------ report ----
std::string Report::counts(const Target& t) const {
    const std::string made = t.feminine ? "cr\xC3\xA9\xC3\xA9" "e" : "cr\xC3\xA9\xC3\xA9";
    const std::string madeMany = t.feminine ? "cr\xC3\xA9\xC3\xA9" "es" : "cr\xC3\xA9\xC3\xA9s";
    const std::string upd = t.feminine ? "mise \xC3\xA0 jour" : "mis \xC3\xA0 jour";
    const std::string updMany = t.feminine ? "mises \xC3\xA0 jour" : "mis \xC3\xA0 jour";
    std::vector<std::string> parts;
    parts.push_back(std::to_string(created) + " " + (created == 1 ? t.noun + " " + made : t.nouns + " " + madeMany));
    parts.push_back(std::to_string(updated) + " " + (updated == 1 ? upd : updMany));
    if (!refused.empty()) parts.push_back(plural(refused.size(), "case refus\xC3\xA9" "e", "cases refus\xC3\xA9" "es"));
    if (skipped > 0) parts.push_back(plural(skipped, "ligne sans changement", "lignes sans changement"));
    return joined(parts);
}

std::string Report::banner(const Target& t) const {
    if (!error.empty()) return "Rien n'est coll\xC3\xA9 : " + error;
    // Les comptes d'abord : ils se lisent meme quand le bandeau est coupe.
    std::string out = "Coll\xC3\xA9 depuis Excel : " + plural(lines, "ligne", "lignes") + " \xE2\x80\x94 " + counts(t) + " \xC2\xB7 Ctrl+Z pour tout annuler \xC2\xB7 ";
    if (titles) {
        out += "colonnes reconnues par leur titre : " + joined(recognized);
        if (!ignored.empty()) out += " (ignor\xC3\xA9" + std::string(ignored.size() > 1 ? "es" : "e") + " : " + joined(ignored) + ")";
    } else {
        out += "sans titres, \xC3\xA0 partir de la colonne " + startColumn;
    }
    return out;
}

std::string Report::status(const Target& t) const {
    if (!error.empty()) return "Coller : " + error;
    std::string out = "Coll\xC3\xA9 : " + counts(t);
    if (!refused.empty()) {
        const auto& r = refused.front();
        out += " (ligne " + std::to_string(r.line) + ", " + r.column + " : " + r.why + ")";
    }
    return out + " \xE2\x80\x94 Ctrl+Z pour tout annuler";
}

std::vector<ui::TableView::Mark> Report::marks() const {
    std::vector<ui::TableView::Mark> out;
    for (const auto& k : createdKeys) out.push_back({k, -1, ui::TableView::MarkKind::Created, {}});
    for (const auto& k : updatedKeys) out.push_back({k, -1, ui::TableView::MarkKind::Updated, {}});
    for (const auto& r : refused)
        if (r.tableColumn >= 0) {
            // La raison qui cite deja la valeur ("type << FLOTTANT >> inconnu") ne la repete pas.
            const std::string quoted = "\xC2\xAB " + r.value + " \xC2\xBB";
            const std::string why = r.why.find(quoted) != std::string::npos ? r.why : quoted + " \xE2\x80\x94 " + r.why;
            out.push_back({r.key, r.tableColumn, ui::TableView::MarkKind::Refused,
                           "Ligne " + std::to_string(r.line) + " du collage, " + r.column + " : " + why});
        }
    return out;
}

// ----------------------------------------------------------------- binding ----
namespace {
std::function<void(const std::string&)>& notifier() {
    static std::function<void(const std::string&)> f;
    return f;
}
} // namespace

void setNotifier(std::function<void(const std::string&)> f) { notifier() = std::move(f); }
void notify(const std::string& text) {
    if (notifier()) notifier()(text);
}

void bind(Binding& b) {
    if (!b.table) return;
    Binding* self = &b;
    b.table->setPasteHandler([self](const ui::TableView::PasteRequest& rq) {
        if (!self->target) return;
        const Target target = self->target(rq);
        Report rep;
        {
            self->pasting = true;
            core::CommandGroupScope group("Coller depuis Excel");
            rep = run(target, rq.text, rq.anchorColumn, rq.asNewRows);
            const std::size_t done = rep.created + rep.updated;
            group.setLabel("Coller depuis Excel : " + std::to_string(done) + " "
                           + (done == 1 ? target.noun : target.nouns)
                           + (rep.refused.empty() ? std::string{} : " (" + std::to_string(rep.refused.size()) + " refus)"));
            (void)group.close();
            self->pasting = false;
        }
        if (self->refresh) self->refresh();
        if (rep.error.empty()) self->table->setPasteResult(rep.banner(target), rep.marks(), self->keyColumn);
        else self->table->clearPasteResult();
        if (self->done) self->done(rep, target);
        notify(rep.status(target));
    });
}

void forget(Binding& b) {
    if (b.pasting || !b.table) return;
    b.table->clearPasteResult();
}

} // namespace app::paste
