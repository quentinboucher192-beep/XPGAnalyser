// =============================================================================
//  ui/widgets/ListMenu.cpp - 1.47.0 (planche 06) : le clic droit, le meme ordre
//  dans toutes les listes (voir ListMenu.hpp)
// =============================================================================
#include "ListMenu.hpp"

#include "DataViews.hpp"

#include <algorithm>
#include <memory>

namespace ui {

namespace {

std::function<void(const std::string&, const std::string&)> gCopied;

const char* const kEllipsis = "\xE2\x80\xA6";

std::string withCount(std::string text, std::size_t n) {
    const std::string num = std::to_string(n);
    for (std::size_t p = text.find("%n"); p != std::string::npos; p = text.find("%n", p + num.size())) text.replace(p, 2, num);
    return text;
}

bool endsWithEllipsis(const std::string& s) {
    return (s.size() >= 3 && s.compare(s.size() - 3, 3, kEllipsis) == 0) || (s.size() >= 3 && s.compare(s.size() - 3, 3, "...") == 0);
}

std::string upperAscii(std::string s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c >= 'a' && c <= 'z') s[i] = static_cast<char>(c - 'a' + 'A');
        // 1.47.0 : les lettres accentuees du francais (UTF-8 C3 A0..BE, sauf C3 B7) : << 2 REVOCATIONS >> avec son E accent aigu
        else if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            if (d >= 0xA0 && d <= 0xBE && d != 0xB7) s[i + 1] = static_cast<char>(d - 0x20);
            ++i;
        }
    }
    return s;
}

// La raison d'une entree grisee tient sur une ligne du menu (le menu ne passe pas a la ligne) : au-dela de
// kNoteMax caracteres, ce qui precede << : >> (<< Une licence en cours (XPG-2026-00031, jusqu'au 06/11/2027) >>), sinon
// les premiers mots et << ... >>. La raison entiere reste ou elle est ecrite (le bouton, la fiche).
constexpr std::size_t kNoteMax = 64;
std::size_t glyphs(const std::string& s, std::size_t end) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < end && i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
    return n;
}
std::string shortNote(const std::string& s) {
    if (glyphs(s, s.size()) <= kNoteMax) return s;
    const auto colon = s.find(" : ");
    if (colon != std::string::npos && colon > 8 && glyphs(s, colon) <= kNoteMax) return s.substr(0, colon);
    std::size_t cut = 0;
    for (std::size_t p = s.find(' '); p != std::string::npos && glyphs(s, p) <= kNoteMax - 1; p = s.find(' ', p + 1)) cut = p;
    if (cut == 0) return s;
    std::string out = s.substr(0, cut);
    while (!out.empty() && (out.back() == ',' || out.back() == ':' || out.back() == ';' || out.back() == ' ' || out.back() == '(')) out.pop_back();
    return out + kEllipsis;
}

} // namespace

// ------------------------------------------------------------------------------------------- ajouter
ListMenu& ListMenu::open(std::string label, std::function<void()> run, std::string why, bool enterOpens) {
    Entry e;
    e.label = std::move(label);
    e.run = std::move(run);
    e.why = std::move(why);
    e.icon = Icon::Open;
    if (enterOpens) e.shortcut = "Entr\xC3\xA9" "e";
    added_.push_back({Sec::Open, std::move(e)});
    return *this;
}

ListMenu& ListMenu::unfold(bool unfolded, std::function<void()> run) {
    Entry e;
    e.label = unfolded ? "Replier" : "D\xC3\xA9plier";
    e.many = unfolded ? "Replier les %n" : "D\xC3\xA9plier les %n";
    e.icon = unfolded ? Icon::Collapse : Icon::Expand;
    e.shortcut = unfolded ? "\xE2\x86\x90" : "\xE2\x86\x92";
    e.run = std::move(run);
    added_.push_back({Sec::Unfold, std::move(e)});
    return *this;
}

ListMenu& ListMenu::unfold(TableView& table) {
    if (!table.unfoldEnabled()) return *this;
    const auto rows = table.selectedModelRows();
    if (rows.empty()) return *this;
    bool allOpen = true;
    for (const RowIndex r : rows) allOpen = allOpen && table.isUnfolded(r);
    TableView* t = &table;
    return unfold(allOpen, [t, rows, allOpen] {
        for (const RowIndex r : rows) (void)t->setUnfolded(r, !allOpen);
    });
}

ListMenu& ListMenu::action(Entry e) {
    added_.push_back({Sec::Actions, std::move(e)});
    return *this;
}

ListMenu& ListMenu::goTo(Entry e) {
    added_.push_back({Sec::GoTo, std::move(e)});
    return *this;
}

ListMenu& ListMenu::filterOn(Entry e) {
    added_.push_back({Sec::FilterOn, std::move(e)});
    return *this;
}

ListMenu& ListMenu::copy(std::string label, std::string text, Icon icon, std::string many, std::string manyText) {
    Entry e;
    e.label = label;
    e.many = std::move(many);
    e.icon = icon;
    const std::string what = std::move(label);
    std::string one = std::move(text);
    std::string several = std::move(manyText);
    const bool plural = sel_.count > 1;
    e.run = [what, one, several, plural] {
        const std::string& t = plural ? several : one;
        setClipboardText(t);
        if (gCopied) gCopied(what, t);
    };
    if (!plural && one.empty()) e.why = "rien \xC3\xA0 copier";
    added_.push_back({Sec::Copy, std::move(e)});
    return *this;
}

ListMenu& ListMenu::copyEntry(Entry e) {
    added_.push_back({Sec::Copy, std::move(e)});
    return *this;
}

ListMenu& ListMenu::exportRows(std::function<void()> run, std::string why) {
    Entry e;
    e.label = "Exporter la ligne (CSV)";
    e.many = "Exporter %n lignes (CSV)";
    e.icon = Icon::Export;
    e.run = std::move(run);
    e.why = std::move(why);
    added_.push_back({Sec::Export, std::move(e)});
    return *this;
}

ListMenu& ListMenu::exportEntry(Entry e) {
    added_.push_back({Sec::Export, std::move(e)});
    return *this;
}

ListMenu& ListMenu::danger(Entry e) {
    if (!e.label.empty() && !endsWithEllipsis(e.label)) e.label += kEllipsis;
    if (!e.many.empty() && !endsWithEllipsis(e.many)) e.many += kEllipsis;
    added_.push_back({Sec::Danger, std::move(e)});
    return *this;
}

ListMenu& ListMenu::note(std::string text, std::string sub) {
    Entry e;
    e.label = std::move(text);
    e.why = sub.empty() ? std::string(" ") : std::move(sub);   // grisee : une ligne a lire, pas a choisir
    e.icon = Icon::Info;
    e.many = e.label;
    added_.push_back({Sec::Note, std::move(e)});
    return *this;
}

// --------------------------------------------------------------------------------------------- le menu
std::string ListMenu::labelOf(const Entry& e) const { return many() ? withCount(e.many, sel_.count) : withCount(e.label, sel_.count); }

bool ListMenu::shown(const Added& a) const {
    if (!many()) return !a.e.label.empty();
    // Plusieurs lignes : ni Ouvrir, ni Aller a, ni Filtrer sur ; le reste s'il sait dire << les 3 >>.
    if (a.sec == Sec::GoTo || a.sec == Sec::FilterOn) return false;
    if (a.sec == Sec::Open) return false;
    return !a.e.many.empty();
}

bool ListMenu::empty() const noexcept {
    for (const auto& a : added_)
        if (shown(a)) return false;
    return true;
}

std::vector<PopupMenu::Item> ListMenu::items(int firstId) const {
    firstId_ = firstId;
    runs_.clear();
    std::vector<PopupMenu::Item> out;
    const auto addRun = [this, firstId](const std::function<void()>& fn) {
        runs_.push_back(fn);
        return firstId + static_cast<int>(runs_.size()) - 1;
    };
    // Une entree du menu (et ses enfants).
    std::function<PopupMenu::Item(const Entry&, bool)> make = [&](const Entry& e, bool top) {
        PopupMenu::Item it;
        it.label = top ? labelOf(e) : withCount(e.label, sel_.count);
        it.icon = e.icon;
        it.radio = e.radio;
        it.checked = e.checked;
        it.shortcut = e.shortcut;
        it.enabled = e.why.empty();
        // La raison sous le libelle (planche 06 : << droit retire le 02/11 - demande-le a Camille Essai >>).
        it.note = shortNote(e.why.empty() ? e.note : (e.why == " " ? std::string() : e.why));
        for (const auto& c : e.children) it.children.push_back(make(c, false));
        it.id = e.children.empty() && it.enabled && e.run ? addRun(e.run) : -1;
        return it;
    };
    const auto rule = [&] {
        if (out.empty() || out.back().separator || out.back().heading) return;
        PopupMenu::Item s;
        s.separator = true;
        out.push_back(std::move(s));
    };
    if (many()) {
        PopupMenu::Item head;
        head.heading = true;
        head.label = upperAscii(std::to_string(sel_.count) + " " + sel_.noun + (sel_.feminine ? " choisies" : " choisis"));
        out.push_back(std::move(head));
    }
    // Les groupes de la regle : Ouvrir | actions | Aller a, Filtrer sur | Copier, Exporter | danger | note.
    const auto section = [&](Sec s) {
        for (const auto& a : added_)
            if (a.sec == s && shown(a)) {
                PopupMenu::Item it = make(a.e, true);
                if (s == Sec::Danger) it.danger = true;
                out.push_back(std::move(it));
            }
    };
    const auto submenu = [&](Sec s, const char* title, Icon icon) {
        Entry parent;
        for (const auto& a : added_)
            if (a.sec == s && shown(a)) parent.children.push_back(a.e);
        if (parent.children.empty()) return;
        PopupMenu::Item it;
        it.label = title;
        it.icon = icon;
        for (const auto& c : parent.children) it.children.push_back(make(c, false));
        out.push_back(std::move(it));
    };
    section(Sec::Open);
    section(Sec::Unfold);
    rule();
    section(Sec::Actions);
    rule();
    submenu(Sec::GoTo, kGoTo, Icon::GoTo);
    submenu(Sec::FilterOn, kFilterOn, Icon::Filter);
    rule();
    // Copier : un sous-menu pour une ligne ; plusieurs lignes, ses entrees << pour toutes >> a plat (<< Copier les 3
    // identifiants >>).
    if (many()) section(Sec::Copy);
    else submenu(Sec::Copy, kCopy, Icon::Copy);
    section(Sec::Export);
    rule();
    section(Sec::Danger);
    rule();
    section(Sec::Note);
    while (!out.empty() && out.back().separator) out.pop_back();
    return out;
}

bool ListMenu::run(int id) const {
    const int i = id - firstId_;
    if (i < 0 || static_cast<std::size_t>(i) >= runs_.size() || !runs_[static_cast<std::size_t>(i)]) return false;
    const auto fn = runs_[static_cast<std::size_t>(i)];   // une copie : l'action peut refaire le menu
    fn();
    return true;
}

std::vector<std::string> ListMenu::labels() const {
    std::vector<std::string> out;
    std::function<void(const std::vector<PopupMenu::Item>&, const std::string&)> walk = [&](const std::vector<PopupMenu::Item>& v, const std::string& prefix) {
        for (const auto& it : v) {
            if (it.separator) continue;
            out.push_back(prefix + it.label);
            if (!it.children.empty()) walk(it.children, prefix + it.label + " > ");
        }
    };
    walk(items(firstId_), {});
    return out;
}

bool ListMenu::runLabel(const std::string& label) const {
    const auto all = items(firstId_);
    std::function<int(const std::vector<PopupMenu::Item>&, const std::string&)> find = [&](const std::vector<PopupMenu::Item>& v, const std::string& prefix) -> int {
        for (const auto& it : v) {
            if (it.separator) continue;
            if (prefix + it.label == label) return it.enabled && it.children.empty() ? it.id : -1;
            if (!it.children.empty())
                if (const int id = find(it.children, prefix + it.label + " > "); id >= 0) return id;
        }
        return -1;
    };
    const int id = find(all, {});
    return id >= 0 && run(id);
}

core::Connection ListMenu::bind(TableView& table, std::function<ListMenu()> build) {
    auto holder = std::make_shared<ListMenu>();
    table.setExtraContextOnly(true);
    table.setExtraContextItems([holder, build] {
        *holder = build ? build() : ListMenu();
        return holder->items(1000);
    });
    return table.contextAction->connect([holder](int id) {
        const ListMenu m = *holder;   // une copie : l'action peut rouvrir le menu
        (void)m.run(id);
    });
}

void ListMenu::setCopiedHook(std::function<void(const std::string&, const std::string&)> hook) { gCopied = std::move(hook); }

} // namespace ui
