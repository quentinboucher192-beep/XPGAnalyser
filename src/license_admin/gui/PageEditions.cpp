// =============================================================================
//  license_admin/gui/PageEditions.cpp - 1.33.0 : LES PAGES Editions ET
//  Fonctionnalites (le registre signe, modifiable)
// -----------------------------------------------------------------------------
//  Le registre (les editions, les fonctionnalites, l'edition sans licence) est
//  un document signe et numerote. Les deux pages le modifient dans UNE copie de
//  travail partagee (le brouillon : une seule par poste, gardee quand on passe
//  d'une page a l'autre), puis << Signer le registre n. N+1 >> le signe et le
//  range dans l'archive (Workspace::signRegistry). Rien n'est supprime : une
//  edition ou une fonctionnalite se RETIRE (des clients l'ont dans leur
//  licence). Un changement arrive chez un client avec sa prochaine licence : le
//  registre voyage dans chaque fichier de licence.
//
//  Sans registre : le premier se signe depuis un registre.json ecrit a la main
//  (le format : docs/LICENCES.md ; l'exemple : tests/fixtures/licences/
//  registre-exemple.json).
//
//  Le brouillon part du registre de l'archive. Signe ailleurs entre-temps (la
//  console, un autre poste par la synchro) : le brouillon est perime, la
//  signature est refusee (elle effacerait l'autre) ; << Annuler les
//  modifications >> repart du nouveau.
// =============================================================================
#include "ListKit.hpp"
#include "Page.hpp"

#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Charts.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/DatePicker.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace licdlg {
// Defini dans LicenceWizard.cpp.
menu::MenuPtr makeConfirmDialog(std::string id, std::string title, std::string text, std::string okLabel, bool danger,
                                std::function<void()> onOk);
} // namespace licdlg

namespace {

namespace lic = licensing;
using RowIndex = ui::RowIndex;

const ui::Fonts kFonts{};
constexpr float kPad = 16.f;
constexpr float kButtonH = 32.f;
constexpr float kFieldH = 28.f;
constexpr float kGap = 8.f;
constexpr float kLineH = 20.f;

// ---------------------------------------------------------------------------------------------- les aides
// Une table : des lignes de texte, une cle de tri facultative par case, un style.
class Rows final : public ui::ITableModel {
public:
    using Style = std::function<ui::CellStyle(RowIndex, std::size_t)>;
    explicit Rows(std::vector<std::string> headers) : headers_(std::move(headers)) {}
    void set(std::vector<std::vector<std::string>> rows, std::vector<std::vector<std::string>> keys, Style style) {
        rows_ = std::move(rows);
        keys_ = std::move(keys);
        style_ = std::move(style);
        modelReset->emit();
    }
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string(); }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string();
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override { return style_ ? style_(r, c) : ui::CellStyle{}; }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t c) const override { return key(a, c) < key(b, c); }

private:
    [[nodiscard]] std::string key(RowIndex r, std::size_t c) const {
        if (r < keys_.size() && c < keys_[r].size() && !keys_[r][c].empty()) return keys_[r][c];
        return cellText(r, c);
    }
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_, keys_;
    Style                                 style_;
};

template <class T>
T* add(ui::Widget& parent, std::unique_ptr<T> w) {
    T* p = w.get();
    parent.addChild(std::move(w));
    return p;
}

// Le texte coupe a la largeur (avec ...), mesure par le renderer.
std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();   // 1.44.0 : un caractere entier
        if (!t.empty()) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

// Le texte en lignes (mots entiers), mesure sans renderer (la mise en page) ; trop long : la derniere finit par ...
std::vector<std::string> wrap(const std::string& text, gfx::FontId f, float w, std::size_t maxLines) {
    std::vector<std::string> words;
    for (std::size_t i = 0; i <= text.size();) {
        std::size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        if (j > i) words.push_back(text.substr(i, j - i));
        i = j + 1;
    }
    std::vector<std::string> lines;
    std::string line;
    for (const auto& word : words) {
        const std::string t = line.empty() ? word : line + " " + word;
        if (line.empty() || ui::measureWidth(t, f) <= w) {
            line = t;
            continue;
        }
        lines.push_back(line);
        line = word;
        if (lines.size() == maxLines) {
            lines.back() += "\xE2\x80\xA6";
            return lines;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

bool plain(const std::string& s) {
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7F) return false;
    return true;
}

// Un entier tape (des chiffres seulement) dans [lo, hi].
bool parseCount(const std::string& raw, int lo, int hi, int& out) {
    const std::string s = trim(raw);
    if (s.empty() || s.size() > 9) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    const long v = std::stol(s);
    if (v < lo || v > hi) return false;
    out = static_cast<int>(v);
    return true;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (const auto& x : v) out += (out.empty() ? "" : sep) + x;
    return out;
}

std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

std::string padKey(long long v) {
    char b[32];
    std::snprintf(b, sizeof b, "%012lld", v);
    return b;
}

std::string daysText(int d) { return d > 0 ? std::to_string(d) + " j" : std::string("sans fin"); }

bool grants(const lic::EditionDef& e, const std::string& feature) {
    return std::any_of(e.features.begin(), e.features.end(), [&](const lic::FeatureGrant& g) { return g.id == feature; });
}

bool sameLimits(const std::vector<lic::LimitDef>& a, const std::vector<lic::LimitDef>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || a[i].label != b[i].label) return false;
    return true;
}

// Les produits du registre, dans l'ordre de leur premiere fonctionnalite.
std::vector<std::string> productsOf(const lic::Registry& r) {
    std::vector<std::string> out;
    for (const auto& f : r.features)
        if (std::find(out.begin(), out.end(), f.product) == out.end()) out.push_back(f.product);
    return out;
}

constexpr const char* kBusy = "Un travail est en cours : attends qu'il finisse.";

// ------------------------------------------------------------------------------ le brouillon partage
//  UNE copie de travail par poste (le dossier du Workspace) : les deux pages la partagent, elle survit au
//  passage d'une page a l'autre. Propre : elle suit le registre de l'archive a chaque relecture.
struct Draft {
    const Workspace* owner{nullptr};
    std::string      folder;
    std::uint64_t    base{0};    // le numero du registre dont elle part
    lic::Registry    reg;
    bool             dirty{false};
    std::set<std::string> announce;   // 1.38.0 : les editions dont la reduction est annoncee aux clients a la signature
};

Draft& theDraft() {
    static Draft d;
    return d;
}

void loadDraft(Draft& d, const Workspace& ws) {
    d.reg = ws.hasRegistry() ? ws.registry() : lic::Registry{};
    d.base = ws.hasRegistry() ? ws.registry().number : 0;
    d.dirty = false;
    d.announce.clear();
}

// Le brouillon de ce poste (un autre Workspace : un brouillon neuf). Il n'est relu que par syncDraft : un pointeur
// dans ses editions reste valable pendant une modification.
Draft& draftOf(Workspace& ws) {
    Draft& d = theDraft();
    if (d.owner != &ws || d.folder != ws.folder()) {
        d = Draft{};
        d.owner = &ws;
        d.folder = ws.folder();
        loadDraft(d, ws);
    }
    return d;
}

// Relire : un brouillon sans modification suit le registre de l'archive.
Draft& syncDraft(Workspace& ws) {
    Draft& d = draftOf(ws);
    if (!d.dirty) loadDraft(d, ws);
    return d;
}

// Le registre a-t-il ete signe ailleurs depuis le debut des modifications ?
bool stale(const Draft& d, const Workspace& ws) { return d.dirty && (!ws.hasRegistry() || ws.registry().number != d.base); }

// Ce qui change d'un registre a l'autre, une ligne par chose.
std::vector<std::string> changesOf(const lic::Registry& a, const lic::Registry& b) {
    std::vector<std::string> out;
    if (a.unlicensedEdition != b.unlicensedEdition) out.push_back("\xC3\xA9" "dition sans licence : " + a.unlicensedEdition + " devient " + b.unlicensedEdition);
    for (const auto& f : b.features) {
        const lic::FeatureDef* o = a.feature(f.id);
        if (!o) {
            out.push_back("fonctionnalit\xC3\xA9 " + f.id + " ajout\xC3\xA9" "e");
            continue;
        }
        std::vector<std::string> what;
        if (o->label != f.label) what.push_back("libell\xC3\xA9 \xC2\xAB " + f.label + " \xC2\xBB");
        if (o->product != f.product) what.push_back("produit \xC2\xAB " + f.product + " \xC2\xBB");
        if (!sameLimits(o->limits, f.limits)) what.push_back("limites");
        if (o->retired != f.retired) what.push_back(f.retired ? "retir\xC3\xA9" "e" : "remise en service");
        if (!what.empty()) out.push_back("fonctionnalit\xC3\xA9 " + f.id + " : " + join(what, ", "));
    }
    for (const auto& e : b.editions) {
        const lic::EditionDef* o = a.edition(e.id);
        if (!o) {
            out.push_back("\xC3\xA9" "dition " + e.id + " ajout\xC3\xA9" "e");
            continue;
        }
        std::vector<std::string> what;
        if (o->label != e.label) what.push_back("libell\xC3\xA9 \xC2\xAB " + e.label + " \xC2\xBB");
        if (o->type != e.type) what.push_back("type " + lic::licenseTypeLabel(e.type));
        if (o->maxDevices != e.maxDevices) what.push_back(std::to_string(e.maxDevices) + " PC");
        if (o->durationDays != e.durationDays) what.push_back("dur\xC3\xA9" "e " + daysText(e.durationDays));
        if (o->offlineGraceDays != e.offlineGraceDays) what.push_back("gr\xC3\xA2" "ce " + std::to_string(e.offlineGraceDays) + " j");
        if (o->price != e.price) what.push_back("prix " + (e.price.empty() ? std::string("retir\xC3\xA9") : e.price));   // 1.37.0
        if (o->description != e.description) what.push_back("description");
        if (o->promoPrice != e.promoPrice || o->promoUntil != e.promoUntil)   // 1.38.0 : une reduction
            what.push_back(e.promoPrice.empty() ? std::string("r\xC3\xA9" "duction retir\xC3\xA9" "e")
                                                : "r\xC3\xA9" "duction " + e.promoPrice + " jusqu'au " + lic::isoDate(e.promoUntil));
        std::vector<std::string> plus, minus;
        for (const auto& g : e.features)
            if (!grants(*o, g.id)) plus.push_back("+" + g.id);
        for (const auto& g : o->features)
            if (!grants(e, g.id)) minus.push_back("-" + g.id);
        plus.insert(plus.end(), minus.begin(), minus.end());
        if (!plus.empty()) what.push_back("fonctionnalit\xC3\xA9s " + join(plus, " "));
        if (o->retired != e.retired) what.push_back(e.retired ? "retir\xC3\xA9" "e" : "remise en service");
        if (o->saleFrom != e.saleFrom || o->saleUntil != e.saleUntil)   // 1.42.0 : une offre temporaire
            what.push_back(!e.temporary() ? std::string("en vente toujours")
                                          : "en vente " + (e.saleFrom ? "du " + dateFr(e.saleFrom) : std::string()) +
                                                (e.saleUntil ? " au " + dateFr(e.saleUntil - lic::kDay) : std::string(" sans fin")));
        if (!what.empty()) out.push_back("\xC3\xA9" "dition " + e.id + " : " + join(what, ", "));
    }
    for (const auto& o : a.editions)   // 1.42.0 : une offre supprimee
        if (!b.edition(o.id)) out.push_back("offre " + o.id + " supprim\xC3\xA9" "e");
    for (const auto& op : b.options) {   // 1.42.0 : les options
        const lic::OptionDef* o = a.option(op.id);
        if (!o) out.push_back("option " + op.id + " ajout\xC3\xA9" "e");
        else if (!(*o == op)) out.push_back("option " + op.id + " modifi\xC3\xA9" "e");
    }
    for (const auto& o : a.options)
        if (!b.option(o.id)) out.push_back("option " + o.id + " supprim\xC3\xA9" "e");
    return out;
}

// Apres une modification : encore differente du registre signe ?
void settle(Draft& d, const Workspace& ws) {
    if (!d.dirty || stale(d, ws)) return;
    if (changesOf(ws.registry(), d.reg).empty()) d.dirty = false;
}

// Le brouillon se signe-t-il ? (le meme controle que la lecture stricte, dit plus simplement)
bool checkDraft(const lic::Registry& r, std::string* why) {
    const auto no = [&](std::string s) {
        *why = std::move(s);
        return false;
    };
    for (const auto& f : r.features) {
        if (f.label.empty()) return no("la fonctionnalit\xC3\xA9 " + f.id + " n'a pas de libell\xC3\xA9");
        if (f.product.empty()) return no("la fonctionnalit\xC3\xA9 " + f.id + " n'a pas de produit");
        if (f.label.size() > 200 || !plain(f.label)) return no("le libell\xC3\xA9 de " + f.id + " est trop long ou contient des caract\xC3\xA8res interdits");
        if (f.product.size() > 64 || !plain(f.product)) return no("le produit de " + f.id + " est trop long ou contient des caract\xC3\xA8res interdits");
    }
    for (const auto& e : r.editions) {   // 1.38.0 : une reduction a une fin, pas encore passee
        if (!e.promoPrice.empty() && e.promoUntil == 0) return no("la r\xC3\xA9" "duction de " + e.id + " n'a pas de fin (R\xC3\xA9" "duction jusqu'au)");
        if (!e.promoPrice.empty() && e.promoUntil <= lic::systemNow())
            return no("la r\xC3\xA9" "duction de " + e.id + " est d\xC3\xA9j\xC3\xA0 finie : retire son prix r\xC3\xA9" "duit ou change sa fin");
    }
    for (const auto& e : r.editions) {
        if (e.label.empty()) return no("l'\xC3\xA9" "dition " + e.id + " n'a pas de libell\xC3\xA9");
        if (e.label.size() > 200 || !plain(e.label)) return no("le libell\xC3\xA9 de " + e.id + " est trop long ou contient des caract\xC3\xA8res interdits");
        for (const auto& g : e.features) {
            const lic::FeatureDef* d = r.feature(g.id);
            if (!d) return no("l'\xC3\xA9" "dition " + e.id + " accorde " + g.id + ", qui n'est pas d\xC3\xA9" "clar\xC3\xA9" "e");
            for (const auto& [name, v] : g.limits) {
                (void)v;
                if (std::none_of(d->limits.begin(), d->limits.end(), [&](const lic::LimitDef& l) { return l.id == name; }))
                    return no("l'\xC3\xA9" "dition " + e.id + " fixe la limite " + name + " de " + g.id + ", qui n'est plus d\xC3\xA9" "clar\xC3\xA9" "e");
            }
        }
    }
    for (const auto& e : r.editions)   // 1.42.0 : une vente qui finit apres avoir commence
        if (e.saleFrom && e.saleUntil && e.saleUntil <= e.saleFrom) return no("la vente de " + e.id + " finit avant de commencer (Au)");
    for (const auto& o : r.options) {   // 1.42.0 : les options
        if (o.label.empty()) return no("l'option " + o.id + " n'a pas de nom");
        if (o.label.size() > 200 || !plain(o.label)) return no("le nom de l'option " + o.id + " est trop long ou contient des caract\xC3\xA8res interdits");
        if (o.features.empty()) return no("l'option " + o.label + " n'ouvre aucune fonctionnalit\xC3\xA9 (coche-en une)");
        if (o.durations.empty()) return no("l'option " + o.label + " n'a aucune dur\xC3\xA9" "e (coche-en une)");
        for (const auto& g : o.features)
            if (!r.feature(g.id)) return no("l'option " + o.label + " ouvre " + g.id + ", qui n'est pas d\xC3\xA9" "clar\xC3\xA9" "e");
        for (const auto& e : o.editions)
            if (!r.edition(e)) return no("l'option " + o.label + " est propos\xC3\xA9" "e \xC3\xA0 " + e + ", qui n'est plus dans le registre");
        for (const auto& d : o.durations)
            if (d.price.size() > 60 || !plain(d.price)) return no("un prix de l'option " + o.label + " est trop long (60 octets au plus)");
    }
    const lic::EditionDef* free = r.edition(r.unlicensedEdition);
    if (!free) return no("l'\xC3\xA9" "dition sans licence " + r.unlicensedEdition + " n'est pas dans le registre");
    if (free->retired) return no("l'\xC3\xA9" "dition sans licence " + r.unlicensedEdition + " ne peut pas \xC3\xAAtre retir\xC3\xA9" "e");
    return true;
}

// ======================================================================= la base des deux pages
class RegistryPage : public Page {
public:
    RegistryPage(std::string id, ShellHost host, std::string prefix, std::string pageTitle, std::string lead, std::string newText)
        : Page(std::move(id), std::move(host)), prefix_(std::move(prefix)), title_(std::move(pageTitle)), lead_(std::move(lead)) {
        newBtn_ = add(*this, std::make_unique<ui::Button>(std::move(newText), prefix_ + ".nouvelle"));
        newBtn_->setIcon(ui::Icon::Plus);
        cancelBtn_ = add(*this, std::make_unique<ui::Button>("Annuler les modifications", prefix_ + ".annuler"));
        cancelBtn_->setIcon(ui::Icon::Undo);
        signBtn_ = add(*this, std::make_unique<ui::Button>("Signer le registre", prefix_ + ".signer"));
        signBtn_->setStyle(ui::Button::Style::Primary);
        firstBtn_ = add(*this, std::make_unique<ui::Button>("Signer le premier registre depuis un fichier\xE2\x80\xA6", prefix_ + ".premier"));
        firstBtn_->setStyle(ui::Button::Style::Primary);
        createBtn_ = add(*this, std::make_unique<ui::Button>("Cr\xC3\xA9" "er", prefix_ + ".nouvelle.creer"));
        createBtn_->setStyle(ui::Button::Style::Primary);
        closeNewBtn_ = add(*this, std::make_unique<ui::Button>("Fermer", prefix_ + ".nouvelle.fermer"));
        links_ += newBtn_->clicked->connect([this] { openNew(true); });
        links_ += closeNewBtn_->clicked->connect([this] { openNew(false); });
        links_ += createBtn_->clicked->connect([this] { createNew(); });
        links_ += cancelBtn_->clicked->connect([this] { discard(); });
        links_ += signBtn_->clicked->connect([this] { sign(); });
        links_ += firstBtn_->clicked->connect([this] { signFirst(); });
    }
    ~RegistryPage() override { *life_ = false; }

    [[nodiscard]] std::string title() const override { return title_; }

    void refresh() override {
        syncDraft(ws());
        who_.reset();
        hasRegistry_ = ws().hasRegistry();
        if (!hasRegistry_) newOpen_ = false;
        updateBar();
        const auto vis = hasRegistry_ ? ui::Visibility::Visible : ui::Visibility::Collapsed;
        for (ui::Widget* w : {static_cast<ui::Widget*>(newBtn_), static_cast<ui::Widget*>(cancelBtn_), static_cast<ui::Widget*>(signBtn_)}) w->setVisibility(vis);
        firstBtn_->setVisibility(hasRegistry_ ? ui::Visibility::Collapsed : ui::Visibility::Visible);
        for (ui::Widget* w : newWidgets()) w->setVisibility(newOpen_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        createBtn_->setVisibility(newOpen_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        closeNewBtn_->setVisibility(newOpen_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        showContent(hasRegistry_);
        if (bar_) bar_->setVisibility(vis);
        if (hasRegistry_) reloadContent();
        // 1.47.0 : une autre liste y mene (<< Aller a > Ses options >>...) : la barre filtree sur ce qu'elle demande.
        if (const std::string f = takePageFocus(prefix_); !f.empty() && hasRegistry_) applyFocus(f);
        invalidateLayout();
        invalidate();
    }

protected:
    // ---- 1.47.0 (planches 06, 07, 12) : la barre, le clic droit, le depliage - ce que les trois listes partagent ----
    //  La barre lit texts_ : TOUTES les lignes du brouillon (les colonnes de la table, puis les champs en plus) ; la
    //  page ne met dans sa table que celles que bar_->accepts(i) garde.
    virtual void applyFocus(const std::string& what) { (void)what; }
    void setupBar(ui::TableView& table, std::vector<ui::FilterBar::Field> fields, const std::string& memoryKey, const std::string& noun,
                  std::function<void()> rebuild) {
        bar_ = add(*this, std::make_unique<ui::FilterBar>(prefix_, std::move(fields)));
        bar_->setNoun(noun);
        bar_->setClock([this] { return listkit::seconds(ws().now()); });
        ui::FilterBar::Source src;
        src.rows = [this] { return texts_.size(); };
        src.text = [this](std::size_t r, std::size_t f) { return r < texts_.size() && f < texts_[r].size() ? texts_[r][f] : std::string(); };
        bar_->setSource(std::move(src));
        bar_->linkTable(table);
        bar_->setMemoryKey(memoryKey);
        links_ += bar_->changed->connect(std::move(rebuild));
    }
    // Une autre page, filtree sur `focus` (requestPageFocus).
    void goTo(const std::string& page, const std::string& focus) {
        requestPageFocus(page, focus);
        if (host_.navigate) host_.navigate(page);
    }
    // Les lignes du brouillon : << signee >>, << modifiee >>, << nouvelle >> (0, 1, 2).
    static std::string markText(int mark) { return mark == 2 ? "nouvelle" : mark == 1 ? "modifi\xC3\xA9" "e" : "sign\xC3\xA9" "e"; }
    // La ligne d'une table, ses cases separees par des tabulations (Copier > La ligne).
    static std::string lineOf(const ui::TableView& t, RowIndex r) {
        std::string line;
        const auto& m = t.model();
        for (std::size_t c = 0; m && c < m->columnCount(); ++c) line += (c ? "\t" : "") + m->cellText(r, c);
        return line;
    }
    // Les licences de l'archive (hors revoquees) en cours chaque semaine des 12 derniers mois, d'apres leurs dates : la
    // petite courbe d'une ligne depliee. `has` : celle-ci compte-t-elle a cette heure ?
    std::vector<double> weekly(const std::function<bool(const LicenceEntry&, Seconds)>& has) const {
        std::vector<double> out;
        const Seconds now = ws().now();
        for (int k = 52; k >= 0; --k) {
            const Seconds t = now - static_cast<Seconds>(k) * 7 * lic::kDay;
            double n = 0;
            for (const auto& l : ws().licences())
                if (!l.revoked && l.licence.issuedAt <= t && (l.licence.expiresAt == 0 || t < l.licence.expiresAt) && has(l, t)) n += 1;
            out.push_back(n);
        }
        return out;
    }
    void addCurve(ui::UnfoldCard& card, std::string title, std::vector<double> values) {
        card.setCurve(std::move(title), [values](const ui::PaintContext& ctx, gfx::Rect box) {
            ui::charts::paintSparkline(ctx.r, ctx.theme, box.inset(4.f, 6.f), values);
            const std::string last = std::to_string(static_cast<long long>(values.empty() ? 0 : values.back()));
            ctx.r.drawText({box.right() - 4.f - ctx.r.measure(last, ctx.theme.font.uiBold).width, box.y - 2.f}, last, ctx.theme.font.uiBold, ctx.theme.color.text);
        });
    }
    // Qui a signe le registre en vigueur (le journal des admins : << registre N >>), au bas d'une ligne depliee.
    void addSigner(ui::UnfoldCard& card) {
        if (!ws().hasRegistry()) return;
        const std::uint64_t n = ws().registry().number;
        const auto d = who_.last({"offres", "options", "annonces"}, "registre " + std::to_string(n));
        card.addText("registre n\xC2\xB0 " + std::to_string(n) + " sign\xC3\xA9 le " + dateFr(ws().registry().issuedAt) + (d ? " par" : ""), true);
        if (d) card.addPerson(listkit::person(host_, d->key, d->who), prefix_ + ".depliage.signe", true);
    }

    ui::FilterBar*                        bar_{nullptr};    // 1.47.0
    std::vector<std::vector<std::string>> texts_;           // ce que la barre lit : toutes les lignes du brouillon
    listkit::WhoDid                       who_{host_.workspace()};
    // ---- ce que chaque page fournit ----
    virtual void reloadContent() = 0;                        // relire le brouillon (tables ET champs du panneau)
    virtual void contentEdited() = 0;                        // apres une modification : les tables (pas les champs tapes)
    virtual void layoutContent(const gfx::Rect& area) = 0;   // la table et le panneau
    virtual void paintContent(const ui::PaintContext& ctx) = 0;
    virtual void showContent(bool on) = 0;
    virtual std::vector<ui::Widget*> newWidgets() = 0;       // les champs de << Nouvelle... >>
    virtual void layoutNew(const gfx::Rect& row) = 0;
    virtual void paintNew(const ui::PaintContext& ctx) = 0;
    virtual std::string newHint() const = 0;
    virtual bool create(std::string* why) = 0;               // ajouter au brouillon ; faux : pourquoi
    virtual void clearNew() = 0;
    // Un champ du panneau refuse ce qui est tape (rien n'est signe tant qu'il en reste).
    [[nodiscard]] virtual std::string fieldProblem() const = 0;

    Draft& draft() const { return draftOf(ws()); }

    // Le brouillon a change.
    void markDirty() {
        Draft& d = draft();
        d.dirty = true;
        settle(d, ws());
    }
    // Une modification tapee dans le panneau : la barre et les tables suivent ; les champs restent tels quels.
    void edited() {
        markDirty();
        problem_.clear();
        updateBar();
        contentEdited();
        invalidateLayout();
        invalidate();
    }
    void setProblem(std::string p) {
        problem_ = std::move(p);
        invalidateLayout();
        invalidate();
    }
    void say(const std::string& text, Tone tone) {
        if (host_.flash) host_.flash(text, tone);
    }
    [[nodiscard]] std::weak_ptr<bool> alive() const { return life_; }
    // 1.42.0 : le brouillon se sert d'un champ de la 1.42 (une offre temporaire, une option) et le registre signe pas
    // encore : dit une fois (les Studios d'avant la 1.42 ne liront plus le registre).
    void warnFormat142() {
        const bool now142 = draft().reg.usesFormat142();
        const bool signed142 = ws().hasRegistry() && ws().registry().usesFormat142();
        if (now142 && !signed142 && !warned142_) {
            warned142_ = true;
            say("Offre temporaire ou option : le prochain registre sign\xC3\xA9 ne se lira qu'avec la 1.42 ou plus. Mets d'abord les Studios "
                "de tes clients en 1.42.",
                Tone::Warning);
        }
    }
    bool warned142_{false};

    void onLayout() final {
        const gfx::Rect b = bounds();
        const float x = b.x + kPad, w = std::max(200.f, b.w - 2.f * kPad);
        float y = b.y + kPad;
        titleY_ = y;
        // L'etat du registre, a droite du titre.
        const float tw = ui::measureWidth(title_, kFonts.title) + 24.f;
        statusRect_ = {x + tw, y + 4.f, std::max(0.f, w - tw), 22.f};
        y += 34.f;
        leadLines_ = wrap(lead_, kFonts.ui, w, 3);
        leadY_ = y;
        y += static_cast<float>(leadLines_.size()) * kLineH + 10.f;
        if (!hasRegistry_) {
            // Pas encore de registre : l'explication, puis le bouton du premier.
            content_ = {x, y + 8.f, w, std::max(60.f, b.bottom() - kPad - y - 8.f)};
            emptyLines_ = wrap(kFirstRegistry, kFonts.ui, w, 8);
            const float fw = firstBtn_->sizeHint().preferred.w;
            firstBtn_->setBounds({x, content_.y + 30.f + static_cast<float>(emptyLines_.size()) * kLineH + 14.f, fw, kButtonH});
            return;
        }
        // La barre : Nouvelle..., Annuler ; Signer a droite.
        float bx = x;
        for (ui::Button* btn : {newBtn_, cancelBtn_}) {
            const float bw = btn->sizeHint().preferred.w;
            btn->setBounds({bx, y, bw, kButtonH});
            bx += bw + kGap;
        }
        const float sw = signBtn_->sizeHint().preferred.w;
        signBtn_->setBounds({std::max(bx, x + w - sw), y, sw, kButtonH});
        y += kButtonH + kGap;
        newHintLines_.clear();
        if (newOpen_ && hasRegistry_) {
            newRow_ = {x, y, w, kFieldH};
            const float cw = createBtn_->sizeHint().preferred.w, fw = closeNewBtn_->sizeHint().preferred.w;
            closeNewBtn_->setBounds({x + w - fw, y - 2.f, fw, kButtonH});
            createBtn_->setBounds({x + w - fw - kGap - cw, y - 2.f, cw, kButtonH});
            layoutNew({x, y, w - fw - cw - 3.f * kGap, kFieldH});
            y += kFieldH + 6.f;
            newHintY_ = y;
            newHintLines_ = wrap(newHint(), kFonts.caption, w, 2);
            y += static_cast<float>(newHintLines_.size()) * 17.f + kGap;
        }
        problemY_ = y;
        if (!problemText().empty()) y += kLineH + 4.f;
        if (bar_ && bar_->visible()) {   // 1.47.0 : la barre << Filtrer et chercher >>, au-dessus de la table et de la fiche
            const float bh = bar_->heightFor(w);
            bar_->setBounds({x, y, w, bh});
            y += bh + kGap;
        }
        const gfx::Rect content{x, y, w, std::max(60.f, b.bottom() - kPad - y)};
        content_ = content;
        layoutContent(content);
    }

    // La ligne rouge : un refus, un champ refuse, ou le brouillon perime.
    [[nodiscard]] std::string problemText() const {
        if (!problem_.empty()) return problem_;
        if (std::string f = fieldProblem(); !f.empty()) return f;
        if (hasRegistry_ && stale(draft(), ws()))
            return "Le registre n\xC2\xB0 " + std::to_string(ws().registry().number) + " a \xC3\xA9t\xC3\xA9 sign\xC3\xA9 depuis le d\xC3\xA9" "but de tes modifications (faites sur le n\xC2\xB0 "
                   + std::to_string(draft().base) + ") : annule-les pour repartir de lui.";
        return {};
    }

    void onPaint(const ui::PaintContext& ctx) final {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const gfx::Rect b = bounds();
        r.fillRect(b, c.panelBg);
        const float x = b.x + kPad, w = std::max(200.f, b.w - 2.f * kPad);
        r.drawText({x, titleY_}, fit(r, title_, ctx.theme.font.title, w), ctx.theme.font.title, c.text);
        for (std::size_t i = 0; i < leadLines_.size(); ++i)
            r.drawText({x, leadY_ + static_cast<float>(i) * kLineH}, fit(r, leadLines_[i], ctx.theme.font.ui, w), ctx.theme.font.ui, c.textMuted);
        const gfx::Color warn = ctx.theme.tone(ui::Tone::Warning, c.text), bad = ctx.theme.tone(ui::Tone::Error, c.text);
        if (hasRegistry_) {
            // L'etat du registre et du brouillon, a droite du titre.
            const Draft& d = draft();
            std::string status = "Registre n\xC2\xB0 " + std::to_string(ws().registry().number) + " sign\xC3\xA9 le " + dateFr(ws().registry().issuedAt);
            gfx::Color ink = c.textMuted;
            if (stale(d, ws())) {
                status += " \xC2\xB7 modifications p\xC3\xA9rim\xC3\xA9" "es";
                ink = bad;
            } else if (d.dirty) {
                const auto ch = changesOf(ws().registry(), d.reg);
                status += " \xC2\xB7 " + plural(ch.size(), "modification non sign\xC3\xA9" "e", "modifications non sign\xC3\xA9" "es") + (ch.empty() ? "" : " : " + join(ch, " ; "));
                ink = warn;
            } else {
                status += " \xC2\xB7 \xC3\xA0 jour";
            }
            const std::string shownStatus = fit(r, status, ctx.theme.font.ui, statusRect_.w);
            const float sw = r.measure(shownStatus, ctx.theme.font.ui).width;
            r.drawText({statusRect_.right() - sw, statusRect_.y + (statusRect_.h - r.lineHeight(ctx.theme.font.ui)) * 0.5f}, shownStatus, ctx.theme.font.ui, ink);
            if (newOpen_) {
                paintNew(ctx);
                for (std::size_t i = 0; i < newHintLines_.size(); ++i)
                    r.drawText({newRow_.x, newHintY_ + static_cast<float>(i) * 17.f}, fit(r, newHintLines_[i], ctx.theme.font.caption, newRow_.w),
                               ctx.theme.font.caption, c.textMuted);
            }
            if (const std::string p = problemText(); !p.empty()) r.drawText({x, problemY_}, fit(r, p, ctx.theme.font.ui, w), ctx.theme.font.ui, bad);
            paintContent(ctx);
            return;
        }
        // Pas encore de registre.
        r.drawText({x, content_.y}, "Pas encore de registre", ctx.theme.font.lead, c.text);
        for (std::size_t i = 0; i < emptyLines_.size(); ++i)
            r.drawText({x, content_.y + 30.f + static_cast<float>(i) * kLineH}, fit(r, emptyLines_[i], ctx.theme.font.ui, w), ctx.theme.font.ui, c.textMuted);
        std::string note;
        if (ws().role() == Role::None) note = "Il faut d'abord la cl\xC3\xA9 de l'\xC3\xA9" "diteur : Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9.";
        else if (!ws().can("offres")) note = ws().whyNot("offres");   // 1.44.0
        for (const auto& p : ws().archiveProblems())
            if (p.rfind("registre :", 0) == 0) note = "Le registre de l'archive ne se lit pas (" + p + ").";
        if (!note.empty()) r.drawText({x, firstBtn_->bounds().bottom() + 12.f}, fit(r, note, ctx.theme.font.ui, w), ctx.theme.font.ui, warn);
        if (!problem_.empty()) r.drawText({x, firstBtn_->bounds().bottom() + 36.f}, fit(r, problem_, ctx.theme.font.ui, w), ctx.theme.font.ui, bad);
    }

    std::string                prefix_;
    std::shared_ptr<bool>      life_{std::make_shared<bool>(true)};
    core::ConnectionScope      links_;
    bool                       hasRegistry_{false};
    bool                       newOpen_{false};
    gfx::Rect                  newRow_{}, content_{};

private:
    void updateBar() {
        const Draft& d = draft();
        const std::uint64_t next = (hasRegistry_ ? ws().registry().number : d.base) + 1;
        signBtn_->setText("Signer le registre n\xC2\xB0 " + std::to_string(next));
        if (d.dirty && hasRegistry_ && !stale(d, ws())) {
            const auto ch = changesOf(ws().registry(), d.reg);
            signBtn_->setTooltip("\xC3\x80 signer :\n" + join(ch, "\n"));
        } else {
            signBtn_->setTooltip("Rien \xC3\xA0 signer : aucune modification.");
        }
        // 1.44.0 : un admin signe le registre s'il a le droit de la partie qui change (offres, options, annonces : le
        // Workspace verifie la partie exacte) ; sans aucun des trois, le bouton dit pourquoi.
        const bool mayRegistry = ws().can("offres") || ws().can("options") || ws().can("annonces");
        signBtn_->setEnabled(d.dirty && !host_.busy() && mayRegistry);
        if (!mayRegistry) signBtn_->setTooltip(ws().whyNot("offres"));
        cancelBtn_->setEnabled(d.dirty);
        firstBtn_->setEnabled(ws().can("offres") && !host_.busy());
        if (!ws().can("offres")) firstBtn_->setTooltip(ws().whyNot("offres"));
    }

    void openNew(bool on) {
        newOpen_ = on && hasRegistry_;
        if (!on) clearNew();
        problem_.clear();
        refresh();
    }

    void createNew() {
        std::string why;
        if (!create(&why)) {
            setProblem("Refus\xC3\xA9 : " + why);
            return;
        }
        clearNew();
        newOpen_ = false;
        markDirty();
        problem_.clear();
        refresh();
    }

    void discard() {
        Draft& d = draft();
        if (!d.dirty) return;
        d.dirty = false;
        syncDraft(ws());
        problem_.clear();
        refresh();
        say("Modifications du registre annul\xC3\xA9" "es.", Tone::Info);
    }

    void sign() {
        if (host_.busy()) {
            say(kBusy, Tone::Warning);
            return;
        }
        Draft& d = draft();
        if (!d.dirty) {
            setProblem("Rien \xC3\xA0 signer : aucune modification.");
            return;
        }
        if (stale(d, ws())) {
            host_.report(OpResult::fail("le registre n\xC2\xB0 " + std::to_string(ws().hasRegistry() ? ws().registry().number : 0)
                                        + " a \xC3\xA9t\xC3\xA9 sign\xC3\xA9 depuis le d\xC3\xA9" "but de tes modifications : annule-les et refais-les sur lui"));
            return;
        }
        if (const std::string p = fieldProblem(); !p.empty()) {
            setProblem("Corrige d'abord : " + p);
            return;
        }
        std::string why;
        if (!checkDraft(d.reg, &why)) {
            host_.report(OpResult::fail(why));
            return;
        }
        const auto changes = changesOf(ws().registry(), d.reg);
        // 1.38.0 : les annonces aux clients - une finie part ; << Prevenir mes clients >> : une notification dans leurs
        // Studios (jamais un mail), a leur prochaine connexion.
        lic::Registry signedReg = d.reg;
        const lic::Seconds at = lic::systemNow();
        std::erase_if(signedReg.announcements, [at](const lic::Announcement& a) { return a.until != 0 && a.until <= at; });
        std::vector<std::string> told;
        const std::string next = std::to_string(ws().hasRegistry() ? ws().registry().number + 1 : 1);
        for (const auto& id : d.announce) {
            const lic::EditionDef* e = signedReg.edition(id);
            if (!e || e->promoPrice.empty() || e->retired) continue;
            std::erase_if(signedReg.announcements, [&](const lic::Announcement& a) { return a.edition == id; });   // remplacee
            lic::Announcement a;
            a.id = ("R" + next + "-" + id).substr(0, 40);
            const std::string name = e->label.empty() ? e->id : e->label;
            a.title = ("R\xC3\xA9" "duction sur " + name + " : " + e->promoPrice).substr(0, 120);
            // La fin : chaque Studio l'ajoute a sa facon (<< jusqu'au 31/12/2026 >>) ; ici, l'ancien prix et la phrase.
            std::string text = e->price.empty() ? std::string() : "Au lieu de " + e->price + ".";
            if (!e->description.empty()) text += (text.empty() ? "" : " ") + e->description;
            if (text.empty()) text = name + ".";
            if (text.size() > 600) text.resize(600);
            a.text = text;
            a.edition = id;
            a.at = at;
            a.until = e->promoUntil;
            signedReg.announcements.push_back(std::move(a));
            told.push_back(name);
        }
        if (signedReg.announcements.size() > 20)   // les plus anciennes partent
            signedReg.announcements.erase(signedReg.announcements.begin(), signedReg.announcements.end() - 20);
        OpResult r = ws().signRegistry(signedReg);
        if (r.ok) {
            d.dirty = false;
            d.announce.clear();
            if (!told.empty())
                r.details.push_back("Clients pr\xC3\xA9venus (" + join(told, ", ") + ") : une notification dans leurs Studios \xC3\xA0 leur prochaine connexion, jamais un mail.");
            syncDraft(ws());
            std::vector<std::string> lines;
            for (const auto& ch : changes) lines.push_back("Chang\xC3\xA9 : " + ch + ".");
            r.details.insert(r.details.begin(), lines.begin(), lines.end());
        }
        problem_.clear();
        host_.report(r);
        if (r.ok) (void)publishToService(host_);   // 1.35.0 : le service regle, le registre y part
        refresh();
    }

    void signFirst() {
        if (host_.busy()) {
            say(kBusy, Tone::Warning);
            return;
        }
        if (!ws().can("offres")) {   // 1.44.0 : le droit << offres >> (l'admin+ l'a toujours)
            host_.report(OpResult::fail(ws().role() == Role::None ? (ws().key().problem.empty() ? std::string("pas de cl\xC3\xA9 de l'\xC3\xA9" "diteur sur ce poste") : ws().key().problem)
                                                                  : ws().whyNot("offres")));
            return;
        }
        ui::FilePick pick;
        pick.title = "Choisis le registre.json \xC3\xA0 signer";
        pick.filters = {{"Registre \xC3\xA9" "crit \xC3\xA0 la main (.json)", "json"}, {"Tous les fichiers", "*"}};
        std::weak_ptr<bool> alive = life_;
        ShellHost host = host_;
        host_.pickFile(pick, [this, alive, host](std::string path) {
            if (path.empty()) return;
            if (host.busy()) {
                if (host.flash) host.flash(kBusy, Tone::Warning);
                return;
            }
            const OpResult r = host.workspace().signRegistryJson(path);
            host.report(r);
            if (r.ok) (void)publishToService(host);   // 1.35.0 : le service regle, le registre y part
            if (!alive.expired()) refresh();
        });
    }

    std::string              title_, lead_, problem_;
    std::vector<std::string> leadLines_;
    static constexpr const char* kFirstRegistry =
        "Le registre dit quelles \xC3\xA9" "ditions et fonctionnalit\xC3\xA9s existent ; sign\xC3\xA9 avec ta cl\xC3\xA9, il part avec chaque licence. Le premier se signe "
        "depuis un fichier registre.json \xC3\xA9" "crit \xC3\xA0 la main : son format est d\xC3\xA9" "crit dans docs/LICENCES.md, et un exemple complet est dans "
        "tests/fixtures/licences/registre-exemple.json (21 fonctionnalit\xC3\xA9s, 6 \xC3\xA9" "ditions) : copie-le, adapte-le, signe-le. Ensuite, tout se "
        "modifie ici, puis \xC2\xAB Signer le registre n\xC2\xB0 N+1 \xC2\xBB.";
    std::vector<std::string> newHintLines_, emptyLines_;
    float                    titleY_{0}, leadY_{0}, newHintY_{0}, problemY_{0};
    gfx::Rect                statusRect_{};
    ui::Button*              newBtn_{nullptr};
    ui::Button*              cancelBtn_{nullptr};
    ui::Button*              signBtn_{nullptr};
    ui::Button*              firstBtn_{nullptr};
    ui::Button*              createBtn_{nullptr};
    ui::Button*              closeNewBtn_{nullptr};
};

// Un champ a gauche de son libelle, dans le panneau.
struct FieldRow {
    std::string label;
    gfx::Rect   at{};
};

void paintCard(const ui::PaintContext& ctx, const gfx::Rect& card, const std::string& heading, const std::string& sub) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    r.fillRect(card, c.windowBg);
    r.strokeRect(card, c.border, 1.f);
    r.fillRect({card.x, card.y, card.w, 32.f}, c.headerBg);
    float subW = 0.f;
    if (!sub.empty()) {
        subW = std::min(card.w * 0.45f - 16.f, r.measure(sub, ctx.theme.font.caption).width);
        r.drawText({card.right() - 12.f - subW, card.y + (32.f - r.lineHeight(ctx.theme.font.caption)) * 0.5f}, fit(r, sub, ctx.theme.font.caption, subW + 1.f),
                   ctx.theme.font.caption, ctx.theme.tone(ui::Tone::Info, c.text));
    }
    r.drawText({card.x + 12.f, card.y + (32.f - r.lineHeight(ctx.theme.font.heading)) * 0.5f},
               fit(r, heading, ctx.theme.font.heading, card.w - 36.f - (subW > 0.f ? subW + 12.f : 0.f)), ctx.theme.font.heading, c.text);
}

// ============================================================================ la page Editions
class EditionsPage final : public RegistryPage {
public:
    explicit EditionsPage(ShellHost host)
        : RegistryPage("page.editions", std::move(host), "editions", "Offres",
                       "Ce qu'ouvre chaque offre (une \xC3\xA9" "dition), ses valeurs pour une licence neuve, sa p\xC3\xA9riode de vente. Un changement arrive "
                       "chez un client avec sa prochaine licence : le registre sign\xC3\xA9 voyage dans chaque fichier de licence.",
                       "Nouvelle offre\xE2\x80\xA6") {
        table_ = add(*this, std::make_unique<ui::TableView>("editions.table"));
        table_->setColumns({{"Identifiant", 150.f}, {"Libell\xC3\xA9", 150.f}, {"Type", 100.f}, {"PC", 50.f}, {"Dur\xC3\xA9" "e", 80.f}, {"Gr\xC3\xA2" "ce", 66.f},
                            {"Fonctionnalit\xC3\xA9s", 110.f}, {"En vente", 150.f}, {"Licences", 76.f}});
        table_->setSelectionMode(ui::SelectionMode::Single);
        model_ = std::make_shared<Rows>(std::vector<std::string>{"Identifiant", "Libell\xC3\xA9", "Type", "PC", "Dur\xC3\xA9" "e", "Gr\xC3\xA2" "ce", "Fonctionnalit\xC3\xA9s", "En vente", "Licences"});
        table_->setModel(model_);
        links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>& rows) {
            if (loading_ || rows.empty() || rows.front() >= ids_.size()) return;
            select(ids_[rows.front()]);
        });
        // 1.47.0 (planches 06, 07, 12) : la barre (le type, l'etat de vente, ce qu'elle ouvre...), le clic droit, le depliage.
        setupBar(*table_, barFields(), "offres", "offres", [this] { rebuildTable(); });
        links_ += ui::ListMenu::bind(*table_, [this] { return menuFor(); });
        table_->setUnfold([this](RowIndex r) { return unfoldFor(r); });

        label_ = add(*this, std::make_unique<ui::InputText>("editions.libelle"));
        type_ = add(*this, std::make_unique<ui::DropDown>("editions.type"));
        pcs_ = add(*this, std::make_unique<ui::InputText>("editions.pc"));
        days_ = add(*this, std::make_unique<ui::InputText>("editions.duree"));
        days_->setPlaceholder("0 : sans fin");
        pcs_->setTooltip("Le nombre de PC qu'une licence neuve de cette \xC3\xA9" "dition peut lier (de 1 \xC3\xA0 100 000).");
        days_->setTooltip("La dur\xC3\xA9" "e d'une licence neuve, en jours ; 0 : sans fin (une licence perp\xC3\xA9tuelle).");
        // 1.37.0 : la << boutique >> des Studios - le prix (tel que tu l'ecris) et une phrase, montres au client qui choisit
        price_ = add(*this, std::make_unique<ui::InputText>("editions.prix"));
        price_->setPlaceholder("490 \xE2\x82\xAC HT / an (vide : pas de prix montr\xC3\xA9)");
        price_->setMaxLength(60);
        price_->setTooltip("Le prix montr\xC3\xA9 au client dans la boutique de sa demande de licence (Studio \xE2\x80\xBA Demander ma licence), tel que tu "
                           "l'\xC3\xA9" "cris. Aucun paiement en ligne : il se r\xC3\xA8gle avec toi.");
        description_ = add(*this, std::make_unique<ui::InputText>("editions.description"));
        description_->setPlaceholder("Pour un bureau d'\xC3\xA9tudes (facultatif)");
        description_->setMaxLength(200);
        // 1.38.0 : une reduction - le nouveau prix et sa fin ; la carte de la boutique barre l'ancien. << Prevenir mes
        // clients >> : une notification dans leurs Studios a la signature (jamais un mail).
        promo_ = add(*this, std::make_unique<ui::InputText>("editions.prix-reduit"));
        promo_->setPlaceholder("390 \xE2\x82\xAC HT / an (vide : pas de r\xC3\xA9" "duction)");
        promo_->setMaxLength(60);
        promo_->setTooltip("Le prix r\xC3\xA9" "duit : dans la boutique des Studios, l'ancien prix est barr\xC3\xA9, le nouveau montr\xC3\xA9 avec sa fin ; apr\xC3\xA8s cette "
                           "date, la carte reprend le prix normal d'elle-m\xC3\xAAme.");
        // 1.42.0 : la fin de la reduction dans un mini calendrier (avant : une date AAAA-MM-JJ tapee)
        promoEnd_ = add(*this, std::make_unique<ui::DatePicker>("editions.reduction-fin"));
        promoEnd_->setPlaceholder("jusqu'au\xE2\x80\xA6");
        promoEnd_->setClearable(true);
        promoEnd_->setShortcuts({{"1 semaine", 7, 0, false}, {"1 mois", 0, 1, false}, {"Fin du mois", 0, 0, true}});
        promoEnd_->setTooltip("Le dernier jour de la r\xC3\xA9" "duction (compris) : apr\xC3\xA8s, la boutique reprend le prix normal d'elle-m\xC3\xAAme.");
        // 1.42.0 : UNE OFFRE TEMPORAIRE - en vente pendant une periode (du ... au ..., compris), puis hors de la boutique seule
        sale_ = add(*this, std::make_unique<ui::DropDown>("editions.vente"));
        sale_->setItems({{"Toujours", "toujours"}, {"Pendant une p\xC3\xA9riode", "periode"}});
        sale_->setTooltip("Une offre temporaire : la boutique des Studios ne la montre que pendant sa p\xC3\xA9riode, puis elle en sort seule. Les "
                          "licences d\xC3\xA9j\xC3\xA0 vendues vont jusqu'\xC3\xA0 leur fin.");
        saleFrom_ = add(*this, std::make_unique<ui::DatePicker>("editions.vente-debut"));
        saleFrom_->setShortcuts({{"Aujourd'hui", 1, 0, false}});
        saleTo_ = add(*this, std::make_unique<ui::DatePicker>("editions.vente-fin"));
        saleTo_->setPlaceholder("sans fin");
        saleTo_->setClearable(true);
        saleTo_->setShortcuts({{"1 semaine", 7, 0, false}, {"2 semaines", 14, 0, false}, {"1 mois", 0, 1, false}, {"Fin du mois", 0, 0, true}});
        deleteBtn_ = add(*this, std::make_unique<ui::Button>("Supprimer l'offre\xE2\x80\xA6", "editions.supprimer"));
        deleteBtn_->setStyle(ui::Button::Style::Danger);
        deleteBtn_->setTooltip("Une offre que plus aucune licence en cours n'utilise se supprime ; une offre encore utilis\xC3\xA9" "e se retire de la vente "
                               "(sa suppression couperait les licences de ces clients).");
        announce_ = add(*this, std::make_unique<ui::Checkbox>("Pr\xC3\xA9venir mes clients", "editions.prevenir"));
        announce_->setTooltip("La r\xC3\xA9" "duction annonc\xC3\xA9" "e \xC3\xA0 tes clients : \xC3\xA0 la signature du registre, une annonce part avec lui ; chaque "
                              "Studio la montre une fois dans ses notifications (jamais un mail), m\xC3\xAA" "me un client hors connexion (\xC3\xA0 sa prochaine connexion).");
        grace_ = add(*this, std::make_unique<ui::InputText>("editions.grace"));
        grace_->setTooltip("Apr\xC3\xA8s la fin, les fonctionnalit\xC3\xA9s restent ouvertes ces jours-l\xC3\xA0, le temps d'importer le renouvellement.");
        retired_ = add(*this, std::make_unique<ui::Checkbox>("Retir\xC3\xA9" "e (plus propos\xC3\xA9" "e)", "editions.retiree"));
        retired_->setTooltip("Une \xC3\xA9" "dition ne se supprime jamais : des clients l'ont dans leur licence et la gardent. Retir\xC3\xA9" "e, elle n'est plus propos\xC3\xA9" "e.");
        feats_ = add(*this, std::make_unique<ui::TableView>("editions.fonctionnalites"));
        feats_->setColumns({{"Fonctionnalit\xC3\xA9", 170.f}, {"Identifiant", 150.f}, {"Limites", 120.f}});
        feats_->setSelectionMode(ui::SelectionMode::Single);
        featsModel_ = std::make_shared<Rows>(std::vector<std::string>{"Fonctionnalit\xC3\xA9", "Identifiant", "Limites"});
        feats_->setModel(featsModel_);

        links_ += label_->textChanged->connect([this](const std::string& t) {
            if (loading_) return;
            const std::string v = trim(t);
            if (v.empty()) return fieldError("libelle", "le libell\xC3\xA9 de l'\xC3\xA9" "dition ne peut pas \xC3\xAAtre vide");
            if (v.size() > 200 || !plain(v)) return fieldError("libelle", "le libell\xC3\xA9 est trop long (200 octets au plus) ou contient un caract\xC3\xA8re interdit");
            fieldError("libelle", {});
            if (auto* e = current(); e && e->label != v) {
                e->label = v;
                edited();
            }
        });
        const auto textField = [this](ui::InputText* field, const char* key, std::size_t maxBytes, std::string what, std::string lic::EditionDef::*member) {
            links_ += field->textChanged->connect([this, key, maxBytes, what, member](const std::string& t) {
                if (loading_) return;
                const std::string v = trim(t);
                if (v.size() > maxBytes || !plain(v)) return fieldError(key, what + " : " + std::to_string(maxBytes) + " octets au plus, sans caract\xC3\xA8re de contr\xC3\xB4le");
                fieldError(key, {});
                if (auto* e = current(); e && e->*member != v) {
                    e->*member = v;
                    edited();
                }
            });
        };
        textField(price_, "prix", 60, "le prix", &lic::EditionDef::price);
        textField(description_, "description", 200, "la description", &lic::EditionDef::description);
        links_ += promo_->textChanged->connect([this](const std::string& t) {   // 1.38.0
            if (loading_) return;
            const std::string v = trim(t);
            if (v.size() > 60 || !plain(v)) return fieldError("prix-reduit", "le prix r\xC3\xA9" "duit : 60 octets au plus, sans caract\xC3\xA8re de contr\xC3\xB4le");
            fieldError("prix-reduit", {});
            if (auto* e = current(); e && e->promoPrice != v) {
                e->promoPrice = v;
                if (v.empty()) draft().announce.erase(e->id);
                checkPromo();
                edited();
            }
        });
        links_ += promoEnd_->changed->connect([this](std::int64_t day) {   // 1.42.0 : le dernier jour, compris (23:59:59)
            if (loading_) return;
            const lic::Seconds when = day ? day + lic::kDay - 1 : 0;
            fieldError("reduction-fin", {});
            if (auto* e = current(); e && e->promoUntil != when) {
                e->promoUntil = when;
                checkPromo();
                edited();
            }
        });
        links_ += sale_->selectionChanged->connect([this](int i) {   // 1.42.0
            if (loading_ || i < 0) return;
            auto* e = current();
            if (!e) return;
            const bool period = i == 1;
            if (period && !e->temporary()) {
                const lic::Seconds today = lic::Seconds(ui::DatePicker::dayOf(ws().now()));
                e->saleFrom = today;
                e->saleUntil = today + 7 * lic::kDay;   // une semaine : du jour au jour + 6, compris
            } else if (!period) {
                e->saleFrom = e->saleUntil = 0;
            }
            loadSale();
            edited();
            warnFormat142();
        });
        links_ += saleFrom_->changed->connect([this](std::int64_t day) {
            if (loading_) return;
            auto* e = current();
            if (!e) return;
            e->saleFrom = day;
            if (e->saleUntil && e->saleUntil <= e->saleFrom) e->saleUntil = e->saleFrom + 7 * lic::kDay;
            if (!e->temporary()) e->saleUntil = 0;
            loadSale();
            edited();
            warnFormat142();
        });
        links_ += saleTo_->changed->connect([this](std::int64_t day) {
            if (loading_) return;
            auto* e = current();
            if (!e) return;
            e->saleUntil = day ? day + lic::kDay : 0;   // le dernier jour compris : la vente finit le lendemain a 00:00
            fieldError("vente", e->saleFrom && e->saleUntil && e->saleUntil <= e->saleFrom ? std::string("la vente finit avant de commencer (Au)") : std::string());
            loadSale();
            edited();
            warnFormat142();
        });
        links_ += deleteBtn_->clicked->connect([this] { deleteOffer(); });
        links_ += announce_->stateChanged->connect([this](ui::Checkbox::State st) {
            if (loading_) return;
            auto* e = current();
            if (!e) return;
            if (st == ui::Checkbox::State::Checked) draft().announce.insert(e->id);
            else draft().announce.erase(e->id);
            if (!draft().dirty && st == ui::Checkbox::State::Checked) edited();   // une annonce seule : un registre a signer
        });
        links_ += type_->selectionChanged->connect([this](int i) {
            if (loading_ || i < 0) return;
            const auto* item = type_->selectedItem();
            lic::LicenseType t{};
            if (!item || !lic::parseLicenseType(item->value, t)) return;
            if (auto* e = current(); e && e->type != t) {
                e->type = t;
                edited();
            }
        });
        const auto number = [this](ui::InputText* field, const char* key, int lo, int hi, std::string what, int lic::EditionDef::*member) {
            links_ += field->textChanged->connect([this, key, lo, hi, what, member](const std::string& t) {
                if (loading_) return;
                int v = 0;
                if (!parseCount(t, lo, hi, v)) return fieldError(key, what + " : un nombre entier de " + std::to_string(lo) + " \xC3\xA0 " + std::to_string(hi));
                fieldError(key, {});
                if (auto* e = current(); e && e->*member != v) {
                    e->*member = v;
                    edited();
                }
            });
        };
        number(pcs_, "pc", 1, 100000, "PC au plus", &lic::EditionDef::maxDevices);
        number(days_, "duree", 0, 36600, "la dur\xC3\xA9" "e (jours ; 0 : sans fin)", &lic::EditionDef::durationDays);
        number(grace_, "grace", 0, 3660, "la gr\xC3\xA2" "ce hors ligne (jours)", &lic::EditionDef::offlineGraceDays);
        links_ += retired_->stateChanged->connect([this](ui::Checkbox::State s) {
            if (loading_) return;
            auto* e = current();
            if (!e) return;
            const bool on = s == ui::Checkbox::State::Checked;
            if (on && e->id == draft().reg.unlicensedEdition) {
                loading_ = true;
                retired_->setState(ui::Checkbox::State::Unchecked);
                loading_ = false;
                setProblem("Refus\xC3\xA9 : l'\xC3\xA9" "dition sans licence (" + e->id + ") ne se retire pas ; c'est ce qu'ouvre le logiciel sans licence valide.");
                return;
            }
            if (e->retired != on) {
                e->retired = on;
                if (!on) (void)ws().setDeleteWhenUnused(e->id, false);   // 1.42.0 : remise en vente, elle reste
                edited();
            }
        });
        links_ += feats_->checkClicked->connect([this](RowIndex row) { toggleFeature(row); });

        newId_ = add(*this, std::make_unique<ui::InputText>("editions.nouvelle.id"));
        newId_->setPlaceholder("PRO_PLUS");
        newId_->setMaxLength(32);
        newLabel_ = add(*this, std::make_unique<ui::InputText>("editions.nouvelle.libelle"));
        newLabel_->setPlaceholder("Professionnel +");
        refresh();
    }

protected:
    void reloadContent() override {
        autoDelete();
        const lic::Registry& reg = draft().reg;
        if (selected_.empty() || !reg.edition(selected_)) selected_ = reg.editions.empty() ? std::string() : reg.editions.front().id;
        rebuildTable();
        loadPanel();
    }

    void contentEdited() override { rebuildTable(); }

    void showContent(bool on) override {
        const auto v = on ? ui::Visibility::Visible : ui::Visibility::Collapsed;
        for (ui::Widget* w : panelWidgets()) w->setVisibility(v);
        table_->setVisibility(v);
    }

    void layoutContent(const gfx::Rect& area) override {
        const float pw = std::clamp(area.w * 0.36f, 360.f, 500.f);
        table_->setBounds({area.x, area.y, std::max(120.f, area.w - pw - 12.f), area.h});
        panel_ = {area.right() - pw, area.y, pw, area.h};
        const float px = panel_.x + 12.f, pw2 = panel_.w - 24.f, lw = 128.f;
        // 1.42.0 : une petite fenetre - les lignes plus serrees (la liste des fonctionnalites garde de la place).
        const bool compact = panel_.h < 520.f;
        const float g = compact ? 3.f : kGap;
        float y = panel_.y + 32.f + (compact ? 4.f : 10.f);
        rows_.clear();
        const auto row = [&](const char* text, ui::Widget* w) {
            rows_.push_back({text, {px, y, lw, kFieldH}});
            w->setBounds({px + lw, y, pw2 - lw, kFieldH});
            y += kFieldH + g;
        };
        row("Libell\xC3\xA9", label_);
        row("Type par d\xC3\xA9" "faut", type_);
        row("Prix (boutique)", price_);          // 1.37.0
        row("Description", description_);
        // 1.38.0 : la reduction - le prix reduit et sa fin sur une ligne (comme Prix) ; 1.42.0 : la fin au calendrier, et
        // << Prevenir mes clients >> dessous.
        {
            const float room = pw2 - lw, wa = std::floor((room - kGap) * 0.5f), wb = room - kGap - wa;
            rows_.push_back({"R\xC3\xA9" "duction", {px, y, lw, kFieldH}});
            promo_->setBounds({px + lw, y, wa, kFieldH});
            promoEnd_->setBounds({px + lw + wa + kGap, y, wb, kFieldH});
            y += kFieldH + g;
        }
        // 1.42.0 : la periode de vente
        {
            rows_.push_back({"En vente", {px, y, lw, kFieldH}});
            const lic::EditionDef* e = draft().reg.edition(selected_);
            const bool period = e && e->temporary();
            sale_->setBounds({px + lw, y, std::min(pw2 - lw, 220.f), kFieldH});
            y += kFieldH + (compact ? 2.f : 4.f);
            const auto v = period ? ui::Visibility::Visible : ui::Visibility::Collapsed;
            saleFrom_->setVisibility(v);
            saleTo_->setVisibility(v);
            if (period) {
                const float room = pw2 - lw, w = std::floor((room - 22.f) * 0.5f);
                saleFrom_->setBounds({px + lw, y, w, kFieldH});
                saleTo_->setBounds({px + lw + w + 22.f, y, room - w - 22.f, kFieldH});
                saleArrowAt_ = {px + lw + w + 6.f, y + kFieldH * 0.5f};
                y += kFieldH + 2.f;
            }
            saleNoteY_ = y;
            y += saleNote_.empty() ? (compact ? 0.f : 2.f) : 18.f + g;
        }
        // PC, duree, grace : une ligne, leur libelle au-dessus.
        const float inner = pw2 - 2.f * kGap;
        const float w1 = std::floor(inner * 0.26f), w2 = std::floor(inner * 0.37f), w3 = inner - w1 - w2;
        const float cy = y, fy = y + 17.f;
        rows_.push_back({"PC au plus", {px, cy, w1, 16.f}});
        rows_.push_back({"Dur\xC3\xA9" "e (jours)", {px + w1 + kGap, cy, w2, 16.f}});
        rows_.push_back({"Gr\xC3\xA2" "ce (jours)", {px + w1 + w2 + 2.f * kGap, cy, w3, 16.f}});
        pcs_->setBounds({px, fy, w1, kFieldH});
        days_->setBounds({px + w1 + kGap, fy, w2, kFieldH});
        grace_->setBounds({px + w1 + w2 + 2.f * kGap, fy, w3, kFieldH});
        y = fy + kFieldH + g;
        // Retiree, et 1.38.0 : Prevenir mes clients (la reduction), sur la meme ligne ; 1.42.0 : Supprimer l'offre... en tete de
        // la carte, a droite.
        retired_->setBounds({px, y, std::floor(pw2 * 0.52f), 24.f});
        announce_->setBounds({px + std::floor(pw2 * 0.52f) + kGap, y, pw2 - std::floor(pw2 * 0.52f) - kGap, 24.f});
        y += 24.f + (compact ? 0.f : 2.f);
        deleteNoteY_ = y;
        y += deleteNote_.empty() ? g : 18.f + g;
        {
            const float bw = std::ceil(std::max(140.f, deleteBtn_->sizeHint().preferred.w));
            deleteBtn_->setBounds({panel_.right() - 12.f - bw, panel_.y + 4.f, bw, 26.f});
        }
        featsHeadY_ = y;
        y += 22.f;
        feats_->setBounds({px, y, pw2, std::max(48.f, panel_.bottom() - 10.f - y)});
    }

    void paintContent(const ui::PaintContext& ctx) override {
        const lic::Registry& reg = draft().reg;
        const lic::EditionDef* e = reg.edition(selected_);
        if (!e) return;
        const bool free = e->id == reg.unlicensedEdition;
        paintCard(ctx, panel_, "Offre " + e->id, free ? "l'offre sans licence" : (e->retired ? "retir\xC3\xA9" "e" : (e->temporary() ? "temporaire" : "")));
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        for (const auto& fr : rows_)
            r.drawText({fr.at.x, fr.at.y + (fr.at.h - r.lineHeight(ctx.theme.font.caption)) * 0.5f}, fit(r, fr.label, ctx.theme.font.caption, fr.at.w - 4.f),
                       ctx.theme.font.caption, c.textMuted);
        std::size_t on = 0, offered = 0;
        for (const auto& f : reg.features) {
            if (!f.retired) ++offered;
            if (grants(*e, f.id)) ++on;
        }
        if (saleFrom_->visible()) {   // 1.42.0 : la fleche entre Du et Au
            const gfx::Point a = saleArrowAt_;
            r.line({a.x, a.y}, {a.x + 9.f, a.y}, c.textMuted, 1.4f);
            r.line({a.x + 5.f, a.y - 4.f}, {a.x + 9.f, a.y}, c.textMuted, 1.4f);
            r.line({a.x + 5.f, a.y + 4.f}, {a.x + 9.f, a.y}, c.textMuted, 1.4f);
        }
        if (!saleNote_.empty())
            r.drawText({panel_.x + 12.f + 128.f, saleNoteY_}, fit(r, saleNote_, ctx.theme.font.caption, panel_.w - 24.f - 128.f), ctx.theme.font.caption, c.textMuted);
        if (!deleteNote_.empty())
            r.drawText({panel_.x + 12.f, deleteNoteY_}, fit(r, deleteNote_, ctx.theme.font.caption, panel_.w - 24.f), ctx.theme.font.caption, c.textMuted);
        const std::string head = "Fonctionnalit\xC3\xA9s accord\xC3\xA9" "es : " + std::to_string(on) + " sur " + std::to_string(offered) + (free ? " (sans licence)" : "");
        r.drawText({panel_.x + 12.f, featsHeadY_}, fit(r, head, ctx.theme.font.ui, panel_.w - 24.f), ctx.theme.font.ui, c.text);
    }

    std::vector<ui::Widget*> newWidgets() override { return {newId_, newLabel_}; }

    void layoutNew(const gfx::Rect& row) override {
        const float idW = std::min(220.f, row.w * 0.35f);
        newLabels_ = {{"Identifiant", {row.x, row.y, 86.f, kFieldH}}, {"Libell\xC3\xA9", {row.x + 86.f + idW + 12.f, row.y, 60.f, kFieldH}}};
        newId_->setBounds({row.x + 86.f, row.y, idW, kFieldH});
        const float lx = row.x + 86.f + idW + 12.f + 60.f;
        newLabel_->setBounds({lx, row.y, std::max(80.f, row.right() - lx), kFieldH});
    }

    void paintNew(const ui::PaintContext& ctx) override {
        for (const auto& fr : newLabels_)
            ctx.r.drawText({fr.at.x, fr.at.y + (kFieldH - ctx.r.lineHeight(ctx.theme.font.caption)) * 0.5f}, fr.label, ctx.theme.font.caption,
                           ctx.theme.color.textMuted);
    }

    std::string newHint() const override {
        return "Identifiant : des MAJUSCULES, des chiffres et _, commen\xC3\xA7" "ant par une lettre (32 au plus) ; il ne change plus une fois sign\xC3\xA9. "
               "Elle part vide (1 PC, 365 jours, 14 jours de gr\xC3\xA2" "ce) : coche ses fonctionnalit\xC3\xA9s ensuite.";
    }

    bool create(std::string* why) override {
        lic::Registry& reg = draft().reg;
        const std::string id = trim(newId_->text());
        std::string label = trim(newLabel_->text());
        if (id.empty()) return *why = "donne un identifiant \xC3\xA0 la nouvelle \xC3\xA9" "dition (PRO_PLUS)", false;
        if (!lic::isEditionId(id))
            return *why = "\xC2\xAB " + id + " \xC2\xBB n'est pas un identifiant d'\xC3\xA9" "dition : des MAJUSCULES, des chiffres et _, commen\xC3\xA7" "ant par une lettre (32 au plus)", false;
        if (reg.edition(id)) return *why = "l'\xC3\xA9" "dition " + id + " existe d\xC3\xA9j\xC3\xA0 (une \xC3\xA9" "dition retir\xC3\xA9" "e garde son identifiant)", false;
        if (label.empty()) label = id;
        if (label.size() > 200 || !plain(label)) return *why = "le libell\xC3\xA9 est trop long ou contient un caract\xC3\xA8re interdit", false;
        lic::EditionDef e;
        e.id = id;
        e.label = label;
        e.type = lic::LicenseType::Temporary;
        e.maxDevices = 1;
        e.durationDays = 365;
        e.offlineGraceDays = 14;
        reg.editions.push_back(e);
        selected_ = id;
        errors_.clear();
        return true;
    }

    void clearNew() override {
        newId_->setText({});
        newLabel_->setText({});
    }

    std::string fieldProblem() const override {
        for (const auto& [k, v] : errors_) {
            (void)k;
            if (!v.empty()) return v;
        }
        return {};
    }

private:
    std::vector<ui::Widget*> panelWidgets() {
        return {label_, type_, price_, description_, promo_, promoEnd_, announce_, sale_, saleFrom_, saleTo_, pcs_, days_, grace_, retired_, deleteBtn_, feats_};
    }

    // 1.42.0 : << 10/11 -> 16/11 >> (la fin est le lendemain du dernier jour, a 00:00).
    static std::string ddmm(lic::Seconds t) {
        std::string d = lic::isoDate(t);   // AAAA-MM-JJ
        return d.size() == 10 ? d.substr(8, 2) + "/" + d.substr(5, 2) : d;
    }
    // L'etat de vente d'une offre : la case du tableau, et la phrase du panneau.
    std::string saleCell(const lic::EditionDef& e) const {
        if (e.retired) return "retir\xC3\xA9" "e";
        if (!e.temporary()) return "toujours";
        const lic::Seconds t = ws().now();
        if (e.saleUntil && t >= e.saleUntil) return "vente finie le " + ddmm(e.saleUntil - lic::kDay);
        const std::string range = (e.saleFrom ? ddmm(e.saleFrom) : std::string("\xE2\x80\xA6")) + " \xE2\x86\x92 " + (e.saleUntil ? ddmm(e.saleUntil - lic::kDay) : std::string("sans fin"));
        if (e.saleFrom && t < e.saleFrom) return range + " (\xC3\xA0 venir)";
        if (e.saleUntil) {
            const long long left = (e.saleUntil - t + lic::kDay - 1) / lic::kDay;
            return range + " \xC2\xB7 encore " + std::to_string(left) + " j";
        }
        return range;
    }
    std::string saleSentence(const lic::EditionDef& e) const {
        if (!e.temporary()) return {};
        const lic::Seconds t = ws().now();
        if (e.saleUntil && t >= e.saleUntil)
            return "Vente finie le " + dateFr(e.saleUntil - lic::kDay) + " : elle n'est plus dans la boutique (les licences vendues continuent).";
        std::string s;
        if (e.saleFrom && t < e.saleFrom) s = "Elle entre dans la boutique le " + dateFr(e.saleFrom) + ". ";
        if (e.saleUntil) s += "Le " + dateFr(e.saleUntil - lic::kDay) + " au soir, elle en sort seule. ";
        return s + "Les licences vendues vont jusqu'\xC3\xA0 leur fin.";
    }
    // Les champs de la vente, depuis le brouillon.
    void loadSale() {
        const lic::EditionDef* e = draft().reg.edition(selected_);
        const bool was = loading_;
        loading_ = true;
        const lic::Seconds today = ui::DatePicker::dayOf(ws().now());
        for (ui::DatePicker* p : {promoEnd_, saleFrom_, saleTo_}) p->setToday(today);
        if (e) {
            sale_->setSelectedIndex(e->temporary() ? 1 : 0);
            saleFrom_->setDate(e->saleFrom);
            saleTo_->setDate(e->saleUntil ? e->saleUntil - lic::kDay : 0);
            saleTo_->setRangeStart(e->saleFrom ? e->saleFrom : today);
            saleNote_ = saleSentence(*e);
            const auto list = ws().offersToDelete();
            deleteNote_.clear();
            if (e->retired && std::find(list.begin(), list.end(), e->id) != list.end()) {
                const lic::Seconds last = ws().lastLicenceEnd(e->id);
                deleteNote_ = last > 0 ? "Supprim\xC3\xA9" "e d'elle-m\xC3\xAAme apr\xC3\xA8s sa derni\xC3\xA8re licence (le " + dateFr(last) + ")."
                                       : std::string("Supprim\xC3\xA9" "e d'elle-m\xC3\xAAme quand plus aucune licence ne l'utilisera.");
            }
        }
        loading_ = was;
        invalidateLayout();
        invalidate();
    }

    // 1.42.0 : SUPPRIMER une offre - jamais l'offre sans licence ; une offre encore utilisee (une licence en cours) se retire de
    // la vente a la place (et se supprime d'elle-meme apres la derniere).
    void deleteOffer() {
        const lic::EditionDef* e = draft().reg.edition(selected_);
        if (!e) return;
        const std::string id = e->id;
        if (id == draft().reg.unlicensedEdition)
            return setProblem("Refus\xC3\xA9 : l'offre sans licence (" + id + ") ne se supprime pas ; c'est ce qu'ouvre le logiciel sans licence valide.");
        std::weak_ptr<bool> life = alive();
        const auto using_ = ws().licencesInForce(id);
        if (!using_.empty()) {
            const lic::Seconds last = ws().lastLicenceEnd(id);
            const std::string n = std::to_string(using_.size()) + (using_.size() > 1 ? " licences en cours l'utilisent" : " licence en cours l'utilise");
            std::string text = n + " (" + using_.front() + (using_.size() > 1 ? "\xE2\x80\xA6" : "") + "). Les Studios de ces clients lisent leur offre dans "
                               "le registre : la supprimer couperait leurs licences.\n\n\xC3\x80 la place : la retirer de la vente (plus vendue, plus montr\xC3\xA9" "e dans la "
                               "boutique ; les licences continuent jusqu'\xC3\xA0 leur fin), et la supprimer d'elle-m\xC3\xAAme apr\xC3\xA8s la derni\xC3\xA8re" +
                               (last > 0 ? " (le " + dateFr(last) + ")." : last < 0 ? " (une licence est sans fin : elle reste alors retir\xC3\xA9" "e)." : ".");
            host_.openDialog(licdlg::makeConfirmDialog("dialog.offre-utilisee", id + " ne peut pas \xC3\xAAtre supprim\xC3\xA9" "e maintenant", text,
                                                       "Retirer de la vente", false, [this, life, id] {
                                                           if (life.expired()) return;
                                                           for (auto& x : draft().reg.editions)
                                                               if (x.id == id) x.retired = true;
                                                           (void)ws().setDeleteWhenUnused(id, true);
                                                           edited();
                                                           loadPanel();
                                                           say("Offre " + id + " retir\xC3\xA9" "e de la vente (\xC3\xA0 signer) ; supprim\xC3\xA9" "e d'elle-m\xC3\xAAme apr\xC3\xA8s sa derni\xC3\xA8re licence.", Tone::Info);
                                                       }));
            return;
        }
        const std::string text = "Plus aucune licence en cours ne l'utilise. Elle quitte le catalogue avec le prochain registre sign\xC3\xA9 ; les licences "
                                 "finies restent lisibles dans l'archive, et les anciens registres la gardent (rien n'est r\xC3\xA9\xC3\xA9" "crit).\n\nTant que le "
                                 "registre n'est pas sign\xC3\xA9, \xC2\xAB Annuler les modifications \xC2\xBB la remet.";
        host_.openDialog(licdlg::makeConfirmDialog("dialog.supprimer-offre", "Supprimer l'offre " + id + " ?", text, "Supprimer l'offre", true, [this, life, id] {
            if (life.expired()) return;
            removeOffer(id);
            say("Offre " + id + " supprim\xC3\xA9" "e du brouillon : signe le registre pour qu'elle quitte le catalogue.", Tone::Info);
        }));
    }

    // Oter une offre du brouillon : ses annonces partent avec elle ; une option qui n'etait proposee qu'a elle est retiree.
    void removeOffer(const std::string& id) {
        lic::Registry& reg = draft().reg;
        std::erase_if(reg.editions, [&](const lic::EditionDef& e) { return e.id == id; });
        std::erase_if(reg.announcements, [&](const lic::Announcement& a) { return a.edition == id; });
        draft().announce.erase(id);
        for (auto& o : reg.options) {
            const bool had = std::find(o.editions.begin(), o.editions.end(), id) != o.editions.end();
            std::erase(o.editions, id);
            if (had && o.editions.empty()) o.retired = true;   // vide voudrait dire << toutes >> : retiree plutot
        }
        (void)ws().setDeleteWhenUnused(id, false);
        if (selected_ == id) selected_ = reg.editions.empty() ? std::string() : reg.editions.front().id;
        errors_.clear();
        edited();
        loadPanel();
    }

    // 1.42.0 : les offres retirees a supprimer d'elles-memes (offres-a-supprimer.json) dont la derniere licence est finie.
    void autoDelete() {
        for (const auto& id : ws().offersToDelete()) {
            const lic::EditionDef* e = draft().reg.edition(id);
            if (!e) {
                if (!(ws().hasRegistry() && ws().registry().edition(id))) (void)ws().setDeleteWhenUnused(id, false);   // deja partie
                continue;
            }
            if (!e->retired || !ws().licencesInForce(id).empty() || id == draft().reg.unlicensedEdition) continue;
            removeOffer(id);
            say("L'offre " + id + " n'a plus de licence en cours : supprim\xC3\xA9" "e d'elle-m\xC3\xAAme (\xC3\xA0 signer).", Tone::Info);
        }
    }

    // 1.38.0 : un prix reduit sans fin - dit tout de suite ; la case << Prevenir >> suit le prix reduit.
    void checkPromo() {
        const auto* e = current();
        if (!e) return;
        fieldError("reduction", !e->promoPrice.empty() && e->promoUntil == 0 ? std::string("donne la fin de la r\xC3\xA9" "duction (R\xC3\xA9" "duction jusqu'au)")
                                                                              : std::string());
        announce_->setEnabled(!e->promoPrice.empty());
    }

    lic::EditionDef* current() {
        for (auto& e : draft().reg.editions)
            if (e.id == selected_) return &e;
        return nullptr;
    }

    void fieldError(const std::string& key, std::string text) {
        bool changed = false;
        auto it = std::find_if(errors_.begin(), errors_.end(), [&](const auto& p) { return p.first == key; });
        if (text.empty()) {
            if (it != errors_.end()) {
                errors_.erase(it);
                changed = true;
            }
        } else if (it == errors_.end()) {
            errors_.emplace_back(key, std::move(text));
            changed = true;
        } else if (it->second != text) {
            it->second = std::move(text);
            changed = true;
        }
        if (changed) {
            invalidateLayout();
            invalidate();
        }
    }

    void select(const std::string& id) {
        if (id == selected_) return;
        selected_ = id;
        errors_.clear();
        loadPanel();
        rebuildFeatures();
        invalidateLayout();
        invalidate();
    }

    void rebuildTable() {
        const lic::Registry& reg = draft().reg;
        std::vector<std::vector<std::string>> rows, keys;
        std::vector<int> marks;   // 0 pareille, 1 modifiee, 2 nouvelle
        std::vector<bool> retired, temporary;
        ids_.clear();
        // 1.47.0 : toutes les offres pour la barre (texts_), puis la table : celles qu'elle garde.
        texts_.clear();
        std::vector<int> allMarks;
        for (const auto& e : reg.editions) {
            const int mark = markOf(e);
            auto t = cellsOf(e);
            t.push_back(stateOf(e));
            t.push_back(opensOf(e));
            t.push_back(markText(mark));
            texts_.push_back(std::move(t));
            allMarks.push_back(mark);
        }
        bar_->refreshCounts();
        for (std::size_t i = 0; i < reg.editions.size(); ++i) {
            if (!bar_->accepts(i)) continue;
            const lic::EditionDef& e = reg.editions[i];
            ids_.push_back(e.id);
            const long long used = static_cast<long long>(ws().licencesInForce(e.id).size());   // 1.42.0
            rows.push_back(cellsOf(e));
            keys.push_back({"", "", "", padKey(e.maxDevices), padKey(e.durationDays > 0 ? e.durationDays : 999999), padKey(e.offlineGraceDays),
                            padKey(static_cast<long long>(e.features.size())), "", padKey(used)});
            marks.push_back(allMarks[i]);
            retired.push_back(e.retired);
            temporary.push_back(e.temporary() && !e.retired);
        }
        const std::string free = reg.unlicensedEdition;
        const std::vector<std::string> idsCopy = ids_;
        const float scroll = table_->scrollOffset();
        loading_ = true;
        model_->set(std::move(rows), std::move(keys), [idsCopy, free, marks, retired, temporary](RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (r >= idsCopy.size()) return s;
            if (c == 0) {
                s.monospace = true;
                if (idsCopy[r] == free) {
                    s.badge = "sans licence";
                    s.badgeTone = ui::Tone::Info;
                }
            }
            if (c == 1 && marks[r]) {
                s.badge = marks[r] == 2 ? "nouvelle" : "modifi\xC3\xA9" "e";
                s.badgeTone = ui::Tone::Warning;
            }
            if (c == 7 && temporary[r]) {   // 1.42.0 : une offre temporaire
                s.badge = "temporaire";
                s.badgeTone = ui::Tone::Info;
            }
            if (retired[r]) s.fgTone = ui::Tone::Muted;
            return s;
        });
        for (std::size_t i = 0; i < ids_.size(); ++i)
            if (ids_[i] == selected_) table_->selectModelRows({static_cast<RowIndex>(i)}, false);
        table_->setScrollOffset(scroll);
        loading_ = false;
        rebuildFeatures();
    }

    // ---- 1.47.0 : la barre, le clic droit, le depliage ----
    std::vector<std::string> cellsOf(const lic::EditionDef& e) const {
        const long long used = static_cast<long long>(ws().licencesInForce(e.id).size());
        return {e.id, e.label, lic::licenseTypeLabel(e.type), std::to_string(e.maxDevices), daysText(e.durationDays), std::to_string(e.offlineGraceDays) + " j",
                std::to_string(e.features.size()), saleCell(e), std::to_string(used)};
    }
    // 0 pareille que le registre signe, 1 modifiee, 2 nouvelle.
    int markOf(const lic::EditionDef& e) const {
        const lic::EditionDef* o = ws().hasRegistry() ? ws().registry().edition(e.id) : nullptr;
        if (!o) return 2;
        lic::Registry a, b;
        a.editions = {*o};
        b.editions = {e};
        return changesOf(a, b).empty() ? 0 : 1;
    }
    // L'etat de vente (la puce Etat).
    std::string stateOf(const lic::EditionDef& e) const {
        if (e.retired) return "retir\xC3\xA9" "e";
        if (!e.temporary()) return "en vente";
        const lic::Seconds t = ws().now();
        if (e.saleUntil && t >= e.saleUntil) return "vente finie";
        if (e.saleFrom && t < e.saleFrom) return "\xC3\xA0 venir";
        return "temporaire";
    }
    // Ce qu'elle ouvre (les libelles, un par ligne : Field::split).
    std::string opensOf(const lic::EditionDef& e) const {
        std::string out;
        for (const auto& g : e.features) {
            const lic::FeatureDef* f = draft().reg.feature(g.id);
            out += (out.empty() ? "" : "\n") + (f && !f->label.empty() ? f->label : g.id);
        }
        return out;
    }
    static std::vector<ui::FilterBar::Field> barFields() {
        using K = ui::FilterBar::Kind;
        std::vector<ui::FilterBar::Field> f(12);
        f[0] = {.title = "Identifiant", .kind = K::Text};
        f[1] = {.title = "Libell\xC3\xA9", .kind = K::Text};
        f[2] = {.title = "Type", .chip = true, .noun = "un type"};
        f[3] = {.title = "PC", .noun = "un nombre de PC"};
        f[4] = {.title = "Dur\xC3\xA9" "e", .noun = "une dur\xC3\xA9" "e"};
        f[5] = {.title = "Gr\xC3\xA2" "ce", .noun = "une gr\xC3\xA2" "ce"};
        f[6] = {.title = "Fonctionnalit\xC3\xA9s", .kind = K::Text};
        f[7] = {.title = "En vente", .kind = K::Text};
        f[8] = {.title = "Licences", .kind = K::Text};
        f[9] = {.title = "\xC3\x89tat", .chip = true, .noun = "un \xC3\xA9tat",
                .order = {"en vente", "temporaire", "\xC3\xA0 venir", "vente finie", "retir\xC3\xA9" "e"}};
        f[10] = {.title = "Ouvre", .chip = true, .noun = "une fonctionnalit\xC3\xA9", .searchable = false, .split = "\n"};
        f[11] = {.title = "Brouillon", .noun = "un \xC3\xA9tat", .order = {"sign\xC3\xA9" "e", "modifi\xC3\xA9" "e", "nouvelle"}};
        return f;
    }
    void applyFocus(const std::string& what) override {
        if (what.rfind("fonction:", 0) == 0) {   // << Les offres qui l'accordent >> (Fonctionnalites)
            bar_->clearAll();
            bar_->filterOn(10, what.substr(9));
        }
    }
    // Retiree / remise en vente : le brouillon (comme la case << Retiree >>).
    void setRetired(const std::string& id, bool on) {
        for (auto& e : draft().reg.editions)
            if (e.id == id && e.retired != on) {
                e.retired = on;
                if (!on) (void)ws().setDeleteWhenUnused(id, false);
                edited();
                if (id == selected_) loadPanel();
            }
    }

    ui::ListMenu menuFor() {
        const auto sel = table_->selectedModelRows();
        ui::ListMenu m({sel.size(), "offres", true});
        if (sel.empty() || sel.front() >= ids_.size()) return m;
        const RowIndex row = sel.front();
        const std::string id = ids_[row];
        const lic::Registry& reg = draft().reg;
        const lic::EditionDef* e = reg.edition(id);
        if (!e) return m;
        const bool free = id == reg.unlicensedEdition;
        m.unfold(*table_);
        if (e->retired)
            m.action({.label = "Remettre en vente", .icon = ui::Icon::Undo, .run = [this, id] { setRetired(id, false); }, .note = "dans le brouillon, \xC3\xA0 signer"});
        else
            m.action({.label = "Retirer de la vente", .icon = ui::Icon::Close, .why = free ? std::string("l'offre sans licence ne se retire pas") : std::string(),
                      .run = [this, id] { setRetired(id, true); }, .note = "dans le brouillon, \xC3\xA0 signer"});
        std::size_t all = 0;
        for (const auto& l : ws().licences()) all += l.licence.edition == id ? 1 : 0;
        const std::size_t inForce = ws().licencesInForce(id).size();
        m.goTo({.label = "Ses licences (" + std::to_string(inForce) + " en cours)", .icon = ui::Icon::Document,
                .why = all ? std::string() : std::string("aucune licence de cette offre"), .run = [this, id] { goTo("licences", "offre:" + id); }});
        m.goTo({.label = "Ses PC (Activations)", .icon = ui::Icon::Station, .why = all ? std::string() : std::string("aucune licence de cette offre"),
                .run = [this, id] { goTo("activations", "offre:" + id); }});
        m.goTo({.label = "Ses fonctionnalit\xC3\xA9s (" + std::to_string(e->features.size()) + ")", .icon = ui::Icon::Module,
                .why = e->features.empty() ? std::string("elle n'en ouvre aucune") : std::string(), .run = [this, id] { goTo("fonctionnalites", "offre:" + id); }});
        std::size_t options = 0;
        for (const auto& o : reg.options) options += o.offeredTo(id) && !o.retired ? 1 : 0;
        m.goTo({.label = "Ses options (" + std::to_string(options) + ")", .icon = ui::Icon::Star,
                .why = options ? std::string() : std::string("aucune option propos\xC3\xA9" "e"), .run = [this, id] { goTo("options", "offre:" + id); }});
        const auto on = [&](std::size_t f, const std::string& label, const std::string& v) {
            if (!v.empty()) m.filterOn({.label = label + " : " + v, .run = [this, f, v] { bar_->filterOn(f, v); }});
        };
        on(2, "Ce type", lic::licenseTypeLabel(e->type));
        on(9, "Cet \xC3\xA9tat", stateOf(*e));
        on(3, "Ces PC", std::to_string(e->maxDevices));
        on(4, "Cette dur\xC3\xA9" "e", daysText(e->durationDays));
        if (const int mark = markOf(*e)) on(11, "Le brouillon", markText(mark));
        m.copy("L'identifiant", id, ui::Icon::Copy);
        m.copy("Le libell\xC3\xA9", e->label, ui::Icon::Copy);
        m.copy("La ligne", lineOf(*table_, row), ui::Icon::Copy);
        m.exportRows([this, sel] { listkit::exportRows(host_, *table_, sel, "offres.csv", "offres"); });
        m.danger({.label = "Supprimer l'offre", .icon = ui::Icon::Delete, .why = free ? std::string("l'offre sans licence ne se supprime pas") : std::string(),
                  .run = [this, id] {
                      select(id);
                      deleteOffer();
                  }});
        return m;
    }

    // La ligne depliee : ce qu'elle ouvre, sa vente, ses licences (et leur courbe), le brouillon, qui a signe le registre.
    ui::WidgetPtr unfoldFor(RowIndex r) {
        if (r >= ids_.size()) return nullptr;
        const lic::Registry& reg = draft().reg;
        const lic::EditionDef* e = reg.edition(ids_[r]);
        if (!e) return nullptr;
        const std::string id = e->id;
        auto card = std::make_unique<ui::UnfoldCard>("editions.depliage");
        {
            ui::UnfoldCard::Box b{.title = "CE QU'ELLE OUVRE"};
            for (const auto& product : productsOf(reg)) {
                std::vector<std::string> labels;
                for (const auto& f : reg.features)
                    if (f.product == product && grants(*e, f.id)) labels.push_back(f.label.empty() ? f.id : f.label);
                if (labels.empty()) continue;
                std::string t = "**" + product + "** (" + std::to_string(labels.size()) + ") : " + join(labels, ", ");
                b.lines.push_back({.icon = ui::Icon::Module, .text = t});
                if (b.lines.size() == 4) break;
            }
            if (b.lines.empty()) b.lines.push_back({.icon = ui::Icon::Info, .text = "rien : coche ses fonctionnalit\xC3\xA9s dans la fiche"});
            card->addBox(std::move(b));
        }
        {
            ui::UnfoldCard::Box b{.title = "SA VENTE"};
            b.lines.push_back({.icon = ui::Icon::Document, .text = "**" + e->price + "**", .tone = ui::Tone::None});
            if (e->price.empty()) b.lines.back().text = "pas de prix montr\xC3\xA9";
            if (!e->promoPrice.empty())
                b.lines.push_back({.icon = ui::Icon::Star, .text = "r\xC3\xA9" "duction **" + e->promoPrice + "**" + (e->promoUntil ? " jusqu'au " + dateFr(e->promoUntil) : std::string())});
            b.lines.push_back({.icon = ui::Icon::History, .text = "en vente : **" + saleCell(*e) + "**", .tone = e->retired ? ui::Tone::Muted : ui::Tone::None});
            b.lines.push_back({.icon = ui::Icon::Settings, .text = "**" + std::to_string(e->maxDevices) + "** PC \xC2\xB7 " + daysText(e->durationDays) + " \xC2\xB7 gr\xC3\xA2" "ce " +
                                                                std::to_string(e->offlineGraceDays) + " j \xC2\xB7 " + lic::licenseTypeLabel(e->type)});
            card->addBox(std::move(b));
        }
        {
            ui::UnfoldCard::Box b{.title = "SES LICENCES"};
            const auto used = ws().licencesInForce(id);
            std::set<std::string> clients;
            for (const auto& lid : used)
                if (const LicenceEntry* l = ws().licence(lid)) clients.insert(l->licence.customer);
            ui::UnfoldCard::Line l{.icon = ui::Icon::Document,
                                   .text = "**" + std::to_string(used.size()) + "** en cours chez **" + std::to_string(clients.size()) + "** client" + (clients.size() > 1 ? "s" : "")};
            l.link = [this, id] { goTo("licences", "offre:" + id); };
            b.lines.push_back(std::move(l));
            const lic::Seconds last = ws().lastLicenceEnd(id);
            if (last > 0) b.lines.push_back({.icon = ui::Icon::History, .text = "la derni\xC3\xA8re finit le " + dateFr(last)});
            else if (last < 0) b.lines.push_back({.icon = ui::Icon::History, .text = "une licence est sans fin"});
            std::size_t options = 0;
            for (const auto& o : reg.options) options += o.offeredTo(id) && !o.retired ? 1 : 0;
            ui::UnfoldCard::Line o{.icon = ui::Icon::Star, .text = "**" + std::to_string(options) + "** option" + (options > 1 ? "s" : "") + " propos\xC3\xA9" "e" + (options > 1 ? "s" : "")};
            if (options) o.link = [this, id] { goTo("options", "offre:" + id); };
            b.lines.push_back(std::move(o));
            card->addBox(std::move(b));
        }
        if (const lic::EditionDef* o = ws().hasRegistry() ? ws().registry().edition(id) : nullptr; o && markOf(*e) == 1) {
            ui::UnfoldCard::Box b{.title = "TON BROUILLON (PAS ENCORE SIGN\xC3\x89)", .head = {"ce qui change", "sign\xC3\xA9", "brouillon"}, .weight = 1.3f};
            const auto pair = [&](const char* what, const std::string& a, const std::string& z) {
                if (a != z) b.table.push_back({what, a.empty() ? std::string("\xE2\x80\x94") : a, z.empty() ? std::string("\xE2\x80\x94") : z});
            };
            pair("Libell\xC3\xA9", o->label, e->label);
            pair("Prix", o->price, e->price);
            pair("R\xC3\xA9" "duction", o->promoPrice, e->promoPrice);
            pair("En vente", saleCell(*o), saleCell(*e));
            pair("PC", std::to_string(o->maxDevices), std::to_string(e->maxDevices));
            pair("Dur\xC3\xA9" "e", daysText(o->durationDays), daysText(e->durationDays));
            pair("Gr\xC3\xA2" "ce", std::to_string(o->offlineGraceDays) + " j", std::to_string(e->offlineGraceDays) + " j");
            pair("Fonctionnalit\xC3\xA9s", std::to_string(o->features.size()), std::to_string(e->features.size()));
            pair("Retir\xC3\xA9" "e", o->retired ? "oui" : "non", e->retired ? "oui" : "non");
            if (!b.table.empty()) card->addBox(std::move(b));
        }
        addCurve(*card, "EN COURS, 12 MOIS", weekly([id](const LicenceEntry& l, Seconds) { return l.licence.edition == id; }));
        card->addButton("Voir ses licences", "editions.depliage.licences", ui::Icon::GoTo, [this, id] { goTo("licences", "offre:" + id); });
        addSigner(*card);
        return card;
    }

    void rebuildFeatures() {
        const lic::Registry& reg = draft().reg;
        const lic::EditionDef* e = reg.edition(selected_);
        std::vector<std::vector<std::string>> rows;
        std::vector<int> kind;   // -1 titre ; 0 non accordee ; 1 accordee
        std::vector<bool> retired;
        featRows_.clear();
        for (const auto& product : productsOf(reg)) {
            rows.push_back({product, "", ""});
            kind.push_back(-1);
            retired.push_back(false);
            featRows_.push_back({});
            for (const auto& f : reg.features) {
                if (f.product != product) continue;
                std::string limits;
                for (const auto& l : f.limits) limits += (limits.empty() ? "" : ", ") + (l.label.empty() ? l.id : l.label);
                rows.push_back({f.label + (f.retired ? " (retir\xC3\xA9" "e)" : ""), f.id, limits});
                kind.push_back(e && grants(*e, f.id) ? 1 : 0);
                retired.push_back(f.retired);
                featRows_.push_back(f.id);
            }
        }
        const float scroll = feats_->scrollOffset();
        featsModel_->set(std::move(rows), {}, [kind, retired](RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (r >= kind.size()) return s;
            if (kind[r] < 0) {
                if (c == 0) s.spanRow = true;
                s.bold = true;
                return s;
            }
            if (c == 0) s.check = static_cast<std::int8_t>(kind[r]);
            if (c == 1) s.monospace = true;
            if (retired[r]) s.fgTone = ui::Tone::Muted;
            return s;
        });
        feats_->setScrollOffset(scroll);
    }

    void loadPanel() {
        const lic::Registry& reg = draft().reg;
        const lic::EditionDef* e = reg.edition(selected_);
        errors_.clear();   // les champs reprennent les valeurs du brouillon
        loading_ = true;
        if (e) {
            label_->setText(e->label);
            std::vector<ui::DropDown::Item> items;
            for (auto t : {lic::LicenseType::Perpetual, lic::LicenseType::Temporary, lic::LicenseType::Evaluation})
                items.push_back({lic::licenseTypeLabel(t), lic::licenseTypeKey(t)});
            if (e->type == lic::LicenseType::Floating) items.push_back({"flottante (pas en version 1)", lic::licenseTypeKey(lic::LicenseType::Floating)});
            type_->setItems(items);
            for (std::size_t i = 0; i < items.size(); ++i)
                if (items[i].value == lic::licenseTypeKey(e->type)) type_->setSelectedIndex(static_cast<int>(i));
            pcs_->setText(std::to_string(e->maxDevices));
            days_->setText(std::to_string(e->durationDays));
            grace_->setText(std::to_string(e->offlineGraceDays));
            price_->setText(e->price);               // 1.37.0
            description_->setText(e->description);
            promo_->setText(e->promoPrice);          // 1.38.0
            promoEnd_->setToday(ui::DatePicker::dayOf(ws().now()));
            promoEnd_->setDate(e->promoUntil);   // 1.42.0 : le dernier jour (23:59:59 ramene a son jour)
            announce_->setState(draft().announce.count(e->id) ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
            retired_->setState(e->retired ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        }
        for (ui::Widget* w : panelWidgets()) w->setEnabled(e != nullptr);
        if (e) deleteBtn_->setEnabled(e->id != draft().reg.unlicensedEdition);
        loading_ = false;
        loadSale();   // 1.42.0
    }

    void toggleFeature(RowIndex row) {
        if (row >= featRows_.size() || featRows_[row].empty()) return;
        auto* e = current();
        if (!e) return;
        const std::string& id = featRows_[row];
        auto it = std::find_if(e->features.begin(), e->features.end(), [&](const lic::FeatureGrant& g) { return g.id == id; });
        if (it != e->features.end()) {
            e->features.erase(it);
        } else {
            const lic::FeatureDef* f = draft().reg.feature(id);
            if (f && f->retired) {
                setProblem("Refus\xC3\xA9 : la fonctionnalit\xC3\xA9 " + id + " est retir\xC3\xA9" "e ; remets-la en service (page Fonctionnalit\xC3\xA9s) pour l'accorder.");
                return;
            }
            lic::FeatureGrant g;
            g.id = id;
            e->features.push_back(g);
            lic::normalizeGrants(e->features);
        }
        edited();
    }

    ui::TableView*        table_{nullptr};
    std::shared_ptr<Rows> model_;
    std::vector<std::string> ids_;
    std::string           selected_;
    gfx::Rect             panel_{};
    std::vector<FieldRow> rows_, newLabels_;
    float                 featsHeadY_{0};
    ui::InputText*        label_{nullptr};
    ui::DropDown*         type_{nullptr};
    ui::InputText*        pcs_{nullptr};
    ui::InputText*        days_{nullptr};
    ui::InputText*        grace_{nullptr};
    ui::InputText*        price_{nullptr};         // 1.37.0
    ui::InputText*        promo_{nullptr};         // 1.38.0
    ui::DatePicker*       promoEnd_{nullptr};      // 1.42.0 : un mini calendrier
    ui::Checkbox*         announce_{nullptr};
    ui::DropDown*         sale_{nullptr};          // 1.42.0 : la periode de vente
    ui::DatePicker*       saleFrom_{nullptr};
    ui::DatePicker*       saleTo_{nullptr};
    ui::Button*           deleteBtn_{nullptr};
    std::string           saleNote_, deleteNote_;
    float                 saleNoteY_{0}, deleteNoteY_{0};
    gfx::Point            saleArrowAt_{};
    ui::InputText*        description_{nullptr};
    ui::Checkbox*         retired_{nullptr};
    ui::TableView*        feats_{nullptr};
    std::shared_ptr<Rows> featsModel_;
    std::vector<std::string> featRows_;   // l'identifiant de chaque ligne ("" : un titre de produit)
    ui::InputText*        newId_{nullptr};
    ui::InputText*        newLabel_{nullptr};
    std::vector<std::pair<std::string, std::string>> errors_;
    bool                  loading_{false};
};

// ====================================================================== la page Fonctionnalites
class FeaturesPage final : public RegistryPage {
public:
    explicit FeaturesPage(ShellHost host)
        : RegistryPage("page.fonctionnalites", std::move(host), "fonctionnalites", "Fonctionnalit\xC3\xA9s",
                       "Ce que les \xC3\xA9" "ditions accordent, produit par produit, et leurs limites nomm\xC3\xA9" "es. Un changement arrive chez un client "
                       "avec sa prochaine licence : le registre sign\xC3\xA9 voyage dans chaque fichier de licence.",
                       "Nouvelle fonctionnalit\xC3\xA9\xE2\x80\xA6") {
        table_ = add(*this, std::make_unique<ui::TableView>("fonctionnalites.table"));
        table_->setColumns({{"Identifiant", 170.f}, {"Produit", 104.f}, {"Libell\xC3\xA9", 150.f}, {"Limites", 110.f}, {"Retir\xC3\xA9" "e", 90.f}, {"\xC3\x89" "ditions", 96.f}});
        table_->setSelectionMode(ui::SelectionMode::Single);
        model_ = std::make_shared<Rows>(std::vector<std::string>{"Identifiant", "Produit", "Libell\xC3\xA9", "Limites", "Retir\xC3\xA9" "e", "\xC3\x89" "ditions"});
        table_->setModel(model_);
        links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>& rows) {
            if (loading_ || rows.empty() || rows.front() >= ids_.size()) return;
            select(ids_[rows.front()]);
        });
        // 1.47.0 (planches 06, 07, 12) : la barre (le produit, l'etat, l'offre qui l'accorde...), le clic droit, le depliage.
        setupBar(*table_, barFields(), "fonctionnalites", "fonctionnalit\xC3\xA9s", [this] { rebuildTable(); });
        links_ += ui::ListMenu::bind(*table_, [this] { return menuFor(); });
        table_->setUnfold([this](RowIndex r) { return unfoldFor(r); });

        label_ = add(*this, std::make_unique<ui::InputText>("fonctionnalites.libelle"));
        product_ = add(*this, std::make_unique<ui::InputText>("fonctionnalites.produit"));
        product_->setAssist([this](std::string_view before, std::size_t& from, std::vector<ui::InputText::Suggestion>& out) { suggestProducts(before, from, out); });
        retired_ = add(*this, std::make_unique<ui::Checkbox>("Retir\xC3\xA9" "e (plus accord\xC3\xA9" "e)", "fonctionnalites.retiree"));
        retired_->setTooltip("Une fonctionnalit\xC3\xA9 ne se supprime jamais : des clients l'ont dans leur licence et la gardent. Retir\xC3\xA9" "e, aucune \xC3\xA9" "dition ne l'accorde plus.");
        limits_ = add(*this, std::make_unique<ui::TableView>("fonctionnalites.limites"));
        limits_->setColumns({{"Nom", 170.f}, {"Libell\xC3\xA9", 220.f}});
        limits_->setSelectionMode(ui::SelectionMode::Single);
        limitsModel_ = std::make_shared<Rows>(std::vector<std::string>{"Nom", "Libell\xC3\xA9"});
        limits_->setModel(limitsModel_);
        limitId_ = add(*this, std::make_unique<ui::InputText>("fonctionnalites.limite.nom"));
        limitId_->setPlaceholder("max_views");
        limitLabel_ = add(*this, std::make_unique<ui::InputText>("fonctionnalites.limite.libelle"));
        limitLabel_->setPlaceholder("Vues au plus");
        limitAdd_ = add(*this, std::make_unique<ui::Button>("Ajouter ou renommer", "fonctionnalites.limite.ajouter"));
        limitAdd_->setTooltip("Ajoute la limite nomm\xC3\xA9" "e \xC3\xA0 gauche ; si elle existe, change son libell\xC3\xA9.");
        limitRemove_ = add(*this, std::make_unique<ui::Button>("Retirer la limite", "fonctionnalites.limite.retirer"));

        links_ += label_->textChanged->connect([this](const std::string& t) {
            if (loading_) return;
            const std::string v = trim(t);
            if (v.empty()) return fieldError("libelle", "le libell\xC3\xA9 de la fonctionnalit\xC3\xA9 ne peut pas \xC3\xAAtre vide");
            if (v.size() > 200 || !plain(v)) return fieldError("libelle", "le libell\xC3\xA9 est trop long (200 octets au plus) ou contient un caract\xC3\xA8re interdit");
            fieldError("libelle", {});
            if (auto* f = current(); f && f->label != v) {
                f->label = v;
                edited();
            }
        });
        links_ += product_->textChanged->connect([this](const std::string& t) {
            if (loading_) return;
            const std::string v = trim(t);
            if (v.empty()) return fieldError("produit", "le produit ne peut pas \xC3\xAAtre vide (API Studio, IHM Studio, IA, Autres...)");
            if (v.size() > 64 || !plain(v)) return fieldError("produit", "le produit est trop long (64 octets au plus) ou contient un caract\xC3\xA8re interdit");
            fieldError("produit", {});
            if (auto* f = current(); f && f->product != v) {
                f->product = v;
                edited();
            }
        });
        links_ += retired_->stateChanged->connect([this](ui::Checkbox::State s) {
            if (loading_) return;
            auto* f = current();
            const bool on = s == ui::Checkbox::State::Checked;
            if (f && f->retired != on) {
                f->retired = on;
                edited();
            }
        });
        links_ += limits_->selectionChanged->connect([this](const std::vector<RowIndex>& rows) {
            const auto* f = current();
            if (loading_ || !f || rows.empty() || rows.front() >= f->limits.size()) return;
            limitId_->setText(f->limits[rows.front()].id);
            limitLabel_->setText(f->limits[rows.front()].label);
        });
        links_ += limitAdd_->clicked->connect([this] { addLimit(); });
        links_ += limitRemove_->clicked->connect([this] { removeLimit(); });

        newId_ = add(*this, std::make_unique<ui::InputText>("fonctionnalites.nouvelle.id"));
        newId_->setPlaceholder("hmi_reports");
        newId_->setMaxLength(64);
        newProduct_ = add(*this, std::make_unique<ui::InputText>("fonctionnalites.nouvelle.produit"));
        newProduct_->setPlaceholder("IHM Studio");
        newProduct_->setAssist([this](std::string_view before, std::size_t& from, std::vector<ui::InputText::Suggestion>& out) { suggestProducts(before, from, out); });
        newLabel_ = add(*this, std::make_unique<ui::InputText>("fonctionnalites.nouvelle.libelle"));
        newLabel_->setPlaceholder("Rapports");
        refresh();
    }

protected:
    void reloadContent() override {
        const lic::Registry& reg = draft().reg;
        if (selected_.empty() || !reg.feature(selected_)) selected_ = reg.features.empty() ? std::string() : reg.features.front().id;
        rebuildTable();
        loadPanel();
    }

    void contentEdited() override {
        rebuildTable();
        rebuildLimits();
    }

    void showContent(bool on) override {
        const auto v = on ? ui::Visibility::Visible : ui::Visibility::Collapsed;
        for (ui::Widget* w : panelWidgets()) w->setVisibility(v);
        table_->setVisibility(v);
    }

    void layoutContent(const gfx::Rect& area) override {
        const float pw = std::clamp(area.w * 0.36f, 360.f, 500.f);
        table_->setBounds({area.x, area.y, std::max(120.f, area.w - pw - 12.f), area.h});
        panel_ = {area.right() - pw, area.y, pw, area.h};
        const float px = panel_.x + 12.f, pw2 = panel_.w - 24.f, lw = 110.f;
        float y = panel_.y + 32.f + 10.f;
        rows_.clear();
        const auto row = [&](const char* text, ui::Widget* w) {
            rows_.push_back({text, {px, y, lw, kFieldH}});
            w->setBounds({px + lw, y, pw2 - lw, kFieldH});
            y += kFieldH + kGap;
        };
        row("Libell\xC3\xA9", label_);
        row("Produit", product_);
        retired_->setBounds({px, y, pw2, 24.f});
        y += 24.f + kGap;
        usedY_ = y;
        usedLines_ = wrap(usedText(), kFonts.caption, pw2, 2);
        y += static_cast<float>(std::max<std::size_t>(1, usedLines_.size())) * 17.f + kGap;
        limitsHeadY_ = y;
        y += 22.f;
        const float bottomBlock = kFieldH + kGap + kButtonH + 10.f;
        limits_->setBounds({px, y, pw2, std::max(48.f, panel_.bottom() - bottomBlock - kGap - y)});
        y = panel_.bottom() - bottomBlock;
        const float idW = std::min(170.f, pw2 * 0.4f);
        limitId_->setBounds({px, y, idW, kFieldH});
        limitLabel_->setBounds({px + idW + kGap, y, pw2 - idW - kGap, kFieldH});
        y += kFieldH + kGap;
        const float aw = limitAdd_->sizeHint().preferred.w, rw = limitRemove_->sizeHint().preferred.w;
        limitAdd_->setBounds({px, y, std::min(aw, pw2 - rw - kGap), kButtonH});
        limitRemove_->setBounds({px + std::min(aw, pw2 - rw - kGap) + kGap, y, rw, kButtonH});
    }

    void paintContent(const ui::PaintContext& ctx) override {
        const lic::FeatureDef* f = draft().reg.feature(selected_);
        if (!f) return;
        paintCard(ctx, panel_, "Fonctionnalit\xC3\xA9 " + f->id, f->retired ? "retir\xC3\xA9" "e" : f->product);
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        for (const auto& fr : rows_)
            r.drawText({fr.at.x, fr.at.y + (kFieldH - r.lineHeight(ctx.theme.font.caption)) * 0.5f}, fr.label, ctx.theme.font.caption, c.textMuted);
        for (std::size_t i = 0; i < usedLines_.size(); ++i)
            r.drawText({panel_.x + 12.f, usedY_ + static_cast<float>(i) * 17.f}, usedLines_[i], ctx.theme.font.caption, c.textMuted);
        r.drawText({panel_.x + 12.f, limitsHeadY_}, fit(r, "Limites (des entiers nomm\xC3\xA9s)", ctx.theme.font.ui, panel_.w - 24.f), ctx.theme.font.ui,
                   c.text);
    }

    std::vector<ui::Widget*> newWidgets() override { return {newId_, newProduct_, newLabel_}; }

    void layoutNew(const gfx::Rect& row) override {
        const float idW = std::min(200.f, row.w * 0.3f), prW = std::min(150.f, row.w * 0.22f);
        float x = row.x;
        newLabels_.clear();
        newLabels_.push_back({"Identifiant", {x, row.y, 86.f, kFieldH}});
        x += 86.f;
        newId_->setBounds({x, row.y, idW, kFieldH});
        x += idW + 12.f;
        newLabels_.push_back({"Produit", {x, row.y, 60.f, kFieldH}});
        x += 60.f;
        newProduct_->setBounds({x, row.y, prW, kFieldH});
        x += prW + 12.f;
        newLabels_.push_back({"Libell\xC3\xA9", {x, row.y, 60.f, kFieldH}});
        x += 60.f;
        newLabel_->setBounds({x, row.y, std::max(80.f, row.right() - x), kFieldH});
    }

    void paintNew(const ui::PaintContext& ctx) override {
        for (const auto& fr : newLabels_)
            ctx.r.drawText({fr.at.x, fr.at.y + (kFieldH - ctx.r.lineHeight(ctx.theme.font.caption)) * 0.5f}, fr.label, ctx.theme.font.caption,
                           ctx.theme.color.textMuted);
    }

    std::string newHint() const override {
        return "Identifiant : des minuscules, des chiffres et _, commen\xC3\xA7" "ant par une lettre (64 au plus) ; il ne change plus une fois sign\xC3\xA9. "
               "Elle n'est accord\xC3\xA9" "e par aucune \xC3\xA9" "dition tant que tu ne la coches pas (page \xC3\x89" "ditions).";
    }

    bool create(std::string* why) override {
        lic::Registry& reg = draft().reg;
        const std::string id = trim(newId_->text());
        const std::string product = trim(newProduct_->text());
        const std::string label = trim(newLabel_->text());
        if (id.empty()) return *why = "donne un identifiant \xC3\xA0 la nouvelle fonctionnalit\xC3\xA9 (hmi_reports)", false;
        if (!lic::isFeatureId(id))
            return *why = "\xC2\xAB " + id + " \xC2\xBB n'est pas un identifiant de fonctionnalit\xC3\xA9 : des minuscules, des chiffres et _, commen\xC3\xA7" "ant par une lettre (64 au plus)",
                   false;
        if (reg.feature(id)) return *why = "la fonctionnalit\xC3\xA9 " + id + " existe d\xC3\xA9j\xC3\xA0 (une fonctionnalit\xC3\xA9 retir\xC3\xA9" "e garde son identifiant)", false;
        if (product.empty()) return *why = "donne son produit (API Studio, IHM Studio, IA, Autres...)", false;
        if (label.empty()) return *why = "donne son libell\xC3\xA9 (ce que lit le client)", false;
        if (product.size() > 64 || !plain(product) || label.size() > 200 || !plain(label))
            return *why = "le produit (64 octets) ou le libell\xC3\xA9 (200 octets) est trop long, ou contient un caract\xC3\xA8re interdit", false;
        lic::FeatureDef f;
        f.id = id;
        f.product = product;
        f.label = label;
        reg.features.push_back(f);
        selected_ = id;
        errors_.clear();
        return true;
    }

    void clearNew() override {
        newId_->setText({});
        newProduct_->setText({});
        newLabel_->setText({});
    }

    std::string fieldProblem() const override {
        for (const auto& [k, v] : errors_) {
            (void)k;
            if (!v.empty()) return v;
        }
        return {};
    }

private:
    std::vector<ui::Widget*> panelWidgets() { return {label_, product_, retired_, limits_, limitId_, limitLabel_, limitAdd_, limitRemove_}; }

    lic::FeatureDef* current() {
        for (auto& f : draft().reg.features)
            if (f.id == selected_) return &f;
        return nullptr;
    }

    void suggestProducts(std::string_view before, std::size_t& from, std::vector<ui::InputText::Suggestion>& out) {
        from = 0;
        std::vector<std::string> products = productsOf(draft().reg);
        for (const char* p : {"API Studio", "IHM Studio", "IA", "Autres"})
            if (std::find(products.begin(), products.end(), p) == products.end()) products.push_back(p);
        const auto same = [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); };
        for (const auto& p : products) {
            if (p.size() < before.size() || !std::equal(before.begin(), before.end(), p.begin(), same)) continue;
            ui::InputText::Suggestion sug;
            sug.text = p;
            sug.detail = "produit";
            out.push_back(std::move(sug));
        }
    }

    void fieldError(const std::string& key, std::string text) {
        auto it = std::find_if(errors_.begin(), errors_.end(), [&](const auto& p) { return p.first == key; });
        if (text.empty()) {
            if (it != errors_.end()) errors_.erase(it);
        } else if (it == errors_.end()) {
            errors_.emplace_back(key, std::move(text));
        } else {
            it->second = std::move(text);
        }
        invalidateLayout();
        invalidate();
    }

    // Les editions qui l'accordent.
    [[nodiscard]] std::string usedText() const {
        const lic::Registry& reg = draft().reg;
        std::vector<std::string> by;
        for (const auto& e : reg.editions)
            if (grants(e, selected_)) by.push_back(e.id + (e.retired ? " (retir\xC3\xA9" "e)" : ""));
        if (by.empty()) return "Accord\xC3\xA9" "e par aucune \xC3\xA9" "dition : coche-la sur la page \xC3\x89" "ditions.";
        return "Accord\xC3\xA9" "e par " + plural(by.size(), "\xC3\xA9" "dition", "\xC3\xA9" "ditions") + " : " + join(by, ", ") + ".";
    }

    void select(const std::string& id) {
        if (id == selected_) return;
        selected_ = id;
        errors_.clear();
        limitId_->setText({});
        limitLabel_->setText({});
        loadPanel();
        invalidateLayout();
        invalidate();
    }

    void rebuildTable() {
        const lic::Registry& reg = draft().reg;
        std::vector<std::vector<std::string>> rows, keys;
        std::vector<int> marks;
        std::vector<bool> retired;
        ids_.clear();
        // 1.47.0 : toutes les fonctionnalites pour la barre (texts_), puis la table : celles qu'elle garde.
        texts_.clear();
        for (const auto& f : reg.features) {
            auto t = cellsOf(f);
            t.push_back(f.retired ? "retir\xC3\xA9" "e" : "en service");
            std::string by, in;
            for (const auto& e : reg.editions)
                if (grants(e, f.id)) by += (by.empty() ? "" : "\n") + e.id;
            for (const auto& o : reg.options)
                if (std::any_of(o.features.begin(), o.features.end(), [&](const lic::FeatureGrant& g) { return g.id == f.id; }))
                    in += (in.empty() ? "" : "\n") + (o.label.empty() ? o.id : o.label);
            t.push_back(by);
            t.push_back(in);
            t.push_back(markText(markOf(f)));
            texts_.push_back(std::move(t));
        }
        bar_->refreshCounts();
        for (std::size_t i = 0; i < reg.features.size(); ++i) {
            if (!bar_->accepts(i)) continue;
            const lic::FeatureDef& f = reg.features[i];
            ids_.push_back(f.id);
            rows.push_back(cellsOf(f));
            keys.push_back({"", "", "", "", "", padKey(static_cast<long long>(usedBy(f.id)))});
            marks.push_back(markOf(f));
            retired.push_back(f.retired);
        }
        const float scroll = table_->scrollOffset();
        loading_ = true;
        model_->set(std::move(rows), std::move(keys), [marks, retired](RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (r >= marks.size()) return s;
            if (c == 0) s.monospace = true;
            if (c == 2 && marks[r]) {
                s.badge = marks[r] == 2 ? "nouvelle" : "modifi\xC3\xA9" "e";
                s.badgeTone = ui::Tone::Warning;
            }
            if (retired[r]) s.fgTone = ui::Tone::Muted;
            return s;
        });
        for (std::size_t i = 0; i < ids_.size(); ++i)
            if (ids_[i] == selected_) table_->selectModelRows({static_cast<RowIndex>(i)}, false);
        table_->setScrollOffset(scroll);
        loading_ = false;
    }

    // ---- 1.47.0 : la barre, le clic droit, le depliage ----
    std::size_t usedBy(const std::string& id) const {
        std::size_t n = 0;
        for (const auto& e : draft().reg.editions) n += grants(e, id) ? 1 : 0;
        return n;
    }
    std::vector<std::string> cellsOf(const lic::FeatureDef& f) const {
        std::string limits;
        for (const auto& l : f.limits) limits += (limits.empty() ? "" : ", ") + l.id;
        return {f.id, f.product, f.label, limits, f.retired ? "retir\xC3\xA9" "e" : "", std::to_string(usedBy(f.id))};
    }
    int markOf(const lic::FeatureDef& f) const {
        const lic::FeatureDef* o = ws().hasRegistry() ? ws().registry().feature(f.id) : nullptr;
        if (!o) return 2;
        return o->label != f.label || o->product != f.product || o->retired != f.retired || !sameLimits(o->limits, f.limits) ? 1 : 0;
    }
    static std::vector<ui::FilterBar::Field> barFields() {
        using K = ui::FilterBar::Kind;
        std::vector<ui::FilterBar::Field> f(10);
        f[0] = {.title = "Identifiant", .kind = K::Text};
        f[1] = {.title = "Produit", .chip = true, .noun = "un produit"};
        f[2] = {.title = "Libell\xC3\xA9", .kind = K::Text};
        f[3] = {.title = "Limites", .kind = K::Text};
        f[4] = {.title = "Retir\xC3\xA9" "e", .kind = K::Text};
        f[5] = {.title = "\xC3\x89" "ditions", .kind = K::Text};
        f[6] = {.title = "\xC3\x89tat", .chip = true, .noun = "un \xC3\xA9tat", .searchable = false, .order = {"en service", "retir\xC3\xA9" "e"}};
        f[7] = {.title = "Accord\xC3\xA9" "e par", .chip = true, .chipLabel = "Offre", .noun = "une offre", .searchable = false, .split = "\n"};
        f[8] = {.title = "Dans les options", .noun = "une option", .searchable = false, .split = "\n"};
        f[9] = {.title = "Brouillon", .noun = "un \xC3\xA9tat", .searchable = false, .order = {"sign\xC3\xA9" "e", "modifi\xC3\xA9" "e", "nouvelle"}};
        return f;
    }
    void applyFocus(const std::string& what) override {
        if (what.rfind("offre:", 0) == 0) {   // << Ses fonctionnalites >> (Offres)
            bar_->clearAll();
            bar_->filterOn(7, what.substr(6));
        } else if (what.rfind("option:", 0) == 0) {   // << Ce qu'elle ouvre >> (Options)
            bar_->clearAll();
            bar_->filterOn(8, what.substr(7));
        }
    }
    void setRetired(const std::string& id, bool on) {
        for (auto& f : draft().reg.features)
            if (f.id == id && f.retired != on) {
                f.retired = on;
                edited();
                if (id == selected_) loadPanel();
            }
    }

    ui::ListMenu menuFor() {
        const auto sel = table_->selectedModelRows();
        ui::ListMenu m({sel.size(), "fonctionnalit\xC3\xA9s", true});
        if (sel.empty() || sel.front() >= ids_.size()) return m;
        const RowIndex row = sel.front();
        const lic::Registry& reg = draft().reg;
        const lic::FeatureDef* f = reg.feature(ids_[row]);
        if (!f) return m;
        const std::string id = f->id, label = f->label.empty() ? f->id : f->label;
        m.unfold(*table_);
        if (f->retired)
            m.action({.label = "Remettre en service", .icon = ui::Icon::Undo, .run = [this, id] { setRetired(id, false); }, .note = "dans le brouillon, \xC3\xA0 signer"});
        else
            m.action({.label = "Retirer (plus accord\xC3\xA9" "e)", .icon = ui::Icon::Close, .run = [this, id] { setRetired(id, true); },
                      .note = "dans le brouillon, \xC3\xA0 signer ; les licences d\xC3\xA9j\xC3\xA0 sign\xC3\xA9" "es la gardent"});
        std::vector<std::string> by;
        for (const auto& e : reg.editions)
            if (grants(e, id)) by.push_back(e.id);
        std::size_t options = 0;
        for (const auto& o : reg.options)
            options += std::any_of(o.features.begin(), o.features.end(), [&](const lic::FeatureGrant& g) { return g.id == id; }) ? 1 : 0;
        m.goTo({.label = "Les offres qui l'accordent (" + std::to_string(by.size()) + ")", .icon = ui::Icon::Layers,
                .why = by.empty() ? std::string("aucune offre ne l'accorde") : std::string(), .run = [this, label] { goTo("editions", "fonction:" + label); }});
        m.goTo({.label = "Les options qui l'ouvrent (" + std::to_string(options) + ")", .icon = ui::Icon::Star,
                .why = options ? std::string() : std::string("aucune option ne l'ouvre"), .run = [this, label] { goTo("options", "fonction:" + label); }});
        m.filterOn({.label = "Ce produit : " + f->product, .run = [this, p = f->product] { bar_->filterOn(1, p); }});
        m.filterOn({.label = std::string("Cet \xC3\xA9tat : ") + (f->retired ? "retir\xC3\xA9" "e" : "en service"),
                    .run = [this, st = std::string(f->retired ? "retir\xC3\xA9" "e" : "en service")] { bar_->filterOn(6, st); }});
        for (std::size_t k = 0; k < by.size() && k < 6; ++k)
            m.filterOn({.label = "Accord\xC3\xA9" "e par : " + by[k], .run = [this, v = by[k]] { bar_->filterOn(7, v); }});
        m.copy("L'identifiant", id, ui::Icon::Copy);
        m.copy("Le libell\xC3\xA9", f->label, ui::Icon::Copy);
        m.copy("La ligne", lineOf(*table_, row), ui::Icon::Copy);
        m.exportRows([this, sel] { listkit::exportRows(host_, *table_, sel, "fonctionnalites.csv", "fonctionnalit\xC3\xA9s"); });
        m.note("Rien ne se supprime", "une fonctionnalit\xC3\xA9 se retire : des licences l'ont");
        return m;
    }

    // La ligne depliee : ce qu'elle est (ses limites), qui l'accorde (les offres, avec leurs limites ; les options), chez les
    // clients (les licences en cours qui l'ont).
    ui::WidgetPtr unfoldFor(RowIndex r) {
        if (r >= ids_.size()) return nullptr;
        const lic::Registry& reg = draft().reg;
        const lic::FeatureDef* f = reg.feature(ids_[r]);
        if (!f) return nullptr;
        const std::string id = f->id;
        auto card = std::make_unique<ui::UnfoldCard>("fonctionnalites.depliage");
        {
            ui::UnfoldCard::Box b{.title = "CE QU'ELLE EST"};
            b.lines.push_back({.icon = ui::Icon::Module, .text = "**" + f->label + "** \xC2\xB7 " + f->product, .tone = f->retired ? ui::Tone::Muted : ui::Tone::None});
            b.lines.push_back({.icon = ui::Icon::Code, .text = id, .mono = true});
            for (const auto& l : f->limits) b.lines.push_back({.icon = ui::Icon::Settings, .text = "limite **" + l.id + "** : " + l.label});
            if (f->limits.empty()) b.lines.push_back({.icon = ui::Icon::Settings, .text = "sans limite nomm\xC3\xA9" "e"});
            card->addBox(std::move(b));
        }
        {
            ui::UnfoldCard::Box b{.title = "QUI L'ACCORDE"};
            for (const auto& e : reg.editions)
                for (const auto& g : e.features)
                    if (g.id == id && b.lines.size() < 5) {
                        std::string t = "offre **" + e.id + "**";
                        for (const auto& [name, v] : g.limits) t += " \xC2\xB7 " + name + " = " + std::to_string(v);
                        ui::UnfoldCard::Line l{.icon = ui::Icon::Layers, .text = t, .tone = e.retired ? ui::Tone::Muted : ui::Tone::None};
                        const std::string label = f->label.empty() ? id : f->label;
                        l.link = [this, label] { goTo("editions", "fonction:" + label); };
                        b.lines.push_back(std::move(l));
                    }
            for (const auto& o : reg.options)
                if (b.lines.size() < 6 && std::any_of(o.features.begin(), o.features.end(), [&](const lic::FeatureGrant& g) { return g.id == id; }))
                    b.lines.push_back({.icon = ui::Icon::Star, .text = "option **" + (o.label.empty() ? o.id : o.label) + "**"});
            if (b.lines.empty()) b.lines.push_back({.icon = ui::Icon::Info, .text = "personne : coche-la sur une offre (page Offres)"});
            card->addBox(std::move(b));
        }
        {
            ui::UnfoldCard::Box b{.title = "CHEZ LES CLIENTS"};
            std::size_t n = 0;
            std::set<std::string> clients;
            for (const auto& l : ws().licences())
                if (Workspace::inForce(l) && l.licence.grant(id)) {
                    ++n;
                    clients.insert(l.licence.customer);
                }
            b.lines.push_back({.icon = ui::Icon::Document,
                               .text = "**" + std::to_string(n) + "** licence" + (n > 1 ? "s" : "") + " en cours l'" + (n > 1 ? "ont" : "a") + ", chez **" +
                                       std::to_string(clients.size()) + "** client" + (clients.size() > 1 ? "s" : "")});
            if (f->retired) b.lines.push_back({.icon = ui::Icon::Info, .text = "retir\xC3\xA9" "e : les licences sign\xC3\xA9" "es la gardent jusqu'\xC3\xA0 leur fin", .tone = ui::Tone::Muted});
            card->addBox(std::move(b));
        }
        addCurve(*card, "LICENCES QUI L'ONT, 12 MOIS", weekly([id](const LicenceEntry& l, Seconds) { return l.licence.grant(id) != nullptr; }));
        addSigner(*card);
        return card;
    }

    void rebuildLimits() {
        const lic::FeatureDef* f = draft().reg.feature(selected_);
        std::vector<std::vector<std::string>> rows;
        if (f)
            for (const auto& l : f->limits) rows.push_back({l.id, l.label});
        limitsModel_->set(std::move(rows), {}, [](RowIndex, std::size_t c) {
            ui::CellStyle s;
            if (c == 0) s.monospace = true;
            return s;
        });
    }

    void loadPanel() {
        const lic::FeatureDef* f = draft().reg.feature(selected_);
        errors_.clear();   // les champs reprennent les valeurs du brouillon
        loading_ = true;
        if (f) {
            label_->setText(f->label);
            product_->setText(f->product);
            retired_->setState(f->retired ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        }
        for (ui::Widget* w : panelWidgets()) w->setEnabled(f != nullptr);
        rebuildLimits();
        loading_ = false;
    }

    void addLimit() {
        auto* f = current();
        if (!f) return;
        const std::string id = trim(limitId_->text()), label = trim(limitLabel_->text());
        if (!lic::isFeatureId(id))
            return setProblem("Refus\xC3\xA9 : \xC2\xAB " + id + " \xC2\xBB n'est pas un nom de limite : des minuscules, des chiffres et _, commen\xC3\xA7" "ant par une lettre (max_views)");
        if (label.empty()) return setProblem("Refus\xC3\xA9 : donne un libell\xC3\xA9 \xC3\xA0 la limite (\xC2\xAB Vues au plus \xC2\xBB)");
        if (label.size() > 200 || !plain(label)) return setProblem("Refus\xC3\xA9 : le libell\xC3\xA9 de la limite est trop long ou contient un caract\xC3\xA8re interdit");
        auto it = std::find_if(f->limits.begin(), f->limits.end(), [&](const lic::LimitDef& l) { return l.id == id; });
        if (it != f->limits.end()) {
            if (it->label == label) return;
            it->label = label;
        } else {
            if (f->limits.size() >= 32) return setProblem("Refus\xC3\xA9 : 32 limites au plus par fonctionnalit\xC3\xA9");
            f->limits.push_back({id, label});
        }
        limitId_->setText({});
        limitLabel_->setText({});
        edited();
    }

    void removeLimit() {
        auto* f = current();
        if (!f) return;
        const auto rows = limits_->selectedModelRows();
        std::string id = trim(limitId_->text());
        if (!rows.empty() && rows.front() < f->limits.size()) id = f->limits[rows.front()].id;
        auto it = std::find_if(f->limits.begin(), f->limits.end(), [&](const lic::LimitDef& l) { return l.id == id; });
        if (it == f->limits.end()) return setProblem("Choisis d'abord la limite \xC3\xA0 retirer.");
        for (const auto& e : draft().reg.editions)
            for (const auto& g : e.features)
                if (g.id == f->id)
                    for (const auto& [name, v] : g.limits) {
                        (void)v;
                        if (name == id) return setProblem("Refus\xC3\xA9 : l'\xC3\xA9" "dition " + e.id + " fixe la limite " + id + " ; elle reste d\xC3\xA9" "clar\xC3\xA9" "e.");
                    }
        for (const auto& l : ws().licences())
            if (const auto* g = l.licence.grant(f->id))
                for (const auto& [name, v] : g->limits) {
                    (void)v;
                    if (name == id)
                        return setProblem("Refus\xC3\xA9 : la licence " + l.licence.licenseId + " (sign\xC3\xA9" "e) fixe la limite " + id + " ; elle reste d\xC3\xA9" "clar\xC3\xA9" "e.");
                }
        f->limits.erase(it);
        limitId_->setText({});
        limitLabel_->setText({});
        edited();
    }

    ui::TableView*        table_{nullptr};
    std::shared_ptr<Rows> model_;
    std::vector<std::string> ids_;
    std::string           selected_;
    gfx::Rect             panel_{};
    std::vector<FieldRow> rows_, newLabels_;
    std::vector<std::string> usedLines_;
    float                 usedY_{0}, limitsHeadY_{0};
    ui::InputText*        label_{nullptr};
    ui::InputText*        product_{nullptr};
    ui::Checkbox*         retired_{nullptr};
    ui::TableView*        limits_{nullptr};
    std::shared_ptr<Rows> limitsModel_;
    ui::InputText*        limitId_{nullptr};
    ui::InputText*        limitLabel_{nullptr};
    ui::Button*           limitAdd_{nullptr};
    ui::Button*           limitRemove_{nullptr};
    ui::InputText*        newId_{nullptr};
    ui::InputText*        newProduct_{nullptr};
    ui::InputText*        newLabel_{nullptr};
    std::vector<std::pair<std::string, std::string>> errors_;
    bool                  loading_{false};
};

// ====================================================================== 1.42.0 : la page Options
//  Le catalogue des OPTIONS (Registry::options) : des fonctionnalites vendues a part, pour un temps, a ajouter a une licence
//  dont l'offre ne les a pas (Licences > Ajouter une option...). Chaque option : ce qu'elle ouvre, ses durees (1 semaine,
//  1 mois, jusqu'a la fin de la licence, une autre) et leurs prix, les offres a qui elle est proposee, la boutique des
//  Studios, un essai offert. Le meme brouillon que les offres : << Signer le registre >> les signe ensemble.
class OptionsPage final : public RegistryPage {
public:
    explicit OptionsPage(ShellHost host)
        : RegistryPage("page.options", std::move(host), "options", "Options",
                       "Des fonctionnalit\xC3\xA9s vendues \xC3\xA0 part, pour un temps, m\xC3\xAA" "me sur une offre qui ne les a pas. Elles s'ajoutent \xC3\xA0 une "
                       "licence depuis Licences \xE2\x80\xBA Ajouter une option\xE2\x80\xA6 ; \xC3\xA0 leur date, elles s'arr\xC3\xAA" "tent seules.",
                       "Nouvelle option\xE2\x80\xA6") {
        table_ = add(*this, std::make_unique<ui::TableView>("options.table"));
        table_->setColumns({{"Identifiant", 150.f}, {"Nom", 170.f}, {"Ouvre", 170.f}, {"Dur\xC3\xA9" "es et prix", 230.f}, {"Propos\xC3\xA9" "e \xC3\xA0", 150.f},
                            {"En cours", 76.f}});
        table_->setSelectionMode(ui::SelectionMode::Single);
        model_ = std::make_shared<Rows>(std::vector<std::string>{"Identifiant", "Nom", "Ouvre", "Dur\xC3\xA9" "es et prix", "Propos\xC3\xA9" "e \xC3\xA0", "En cours"});
        table_->setModel(model_);
        links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>& rows) {
            if (loading_ || rows.empty() || rows.front() >= ids_.size()) return;
            select(ids_[rows.front()]);
        });
        // 1.47.0 (planches 06, 07, 12) : la barre (ce qu'elle ouvre, les offres, l'etat...), le clic droit, le depliage.
        setupBar(*table_, barFields(), "options", "options", [this] { rebuildTable(); });
        links_ += ui::ListMenu::bind(*table_, [this] { return menuFor(); });
        table_->setUnfold([this](RowIndex r) { return unfoldFor(r); });
        label_ = add(*this, std::make_unique<ui::InputText>("options.nom"));
        description_ = add(*this, std::make_unique<ui::InputText>("options.description"));
        description_->setPlaceholder("PDF et Excel multi-pages (facultatif)");
        description_->setMaxLength(200);
        for (int k = 0; k < 4; ++k) {
            static const char* const labels[] = {"1 semaine", "1 mois", "Jusqu'\xC3\xA0 la fin de la licence", "Autre (jours)"};
            static const char* const keys[] = {"7", "30", "0", "autre"};
            slot_[k].on = add(*this, std::make_unique<ui::Checkbox>(labels[k], std::string("options.duree-") + keys[k]));
            slot_[k].price = add(*this, std::make_unique<ui::InputText>(std::string("options.prix-") + keys[k]));
            slot_[k].price->setPlaceholder(k == 2 ? "290 \xE2\x82\xAC HT / an" : k == 1 ? "49 \xE2\x82\xAC HT" : "19 \xE2\x82\xAC HT");
            slot_[k].price->setMaxLength(60);
            links_ += slot_[k].on->stateChanged->connect([this, k](ui::Checkbox::State st) { toggleSlot(k, st == ui::Checkbox::State::Checked); });
            links_ += slot_[k].price->textChanged->connect([this, k](const std::string& t) { slotPrice(k, t); });
        }
        otherDays_ = add(*this, std::make_unique<ui::InputText>("options.jours-autre"));
        otherDays_->setPlaceholder("14");
        links_ += otherDays_->textChanged->connect([this](const std::string& t) {
            if (loading_) return;
            int v = 0;
            if (!parseCount(t, 1, 3660, v)) return fieldError("autre", "l'autre dur\xC3\xA9" "e : un nombre de jours de 1 \xC3\xA0 3660");
            fieldError("autre", {});
            auto* o = current();
            if (!o) return;
            for (auto& d : o->durations)
                if (isOther(d.days)) {
                    if (d.days != v && std::none_of(o->durations.begin(), o->durations.end(), [&](const lic::OptionDuration& x) { return x.days == v; })) {
                        d.days = v;
                        edited();
                    }
                    return;
                }
        });
        shop_ = add(*this, std::make_unique<ui::Checkbox>("Dans la boutique des Studios", "options.boutique"));
        shop_->setTooltip("Montr\xC3\xA9" "e au client dans Licence \xE2\x80\xBA Options, et propos\xC3\xA9" "e quand une action qu'elle ouvre est ferm\xC3\xA9" "e.");
        trial_ = add(*this, std::make_unique<ui::InputText>("options.essai"));
        trial_->setTooltip("Des jours offerts, une fois par client (0 : pas d'essai).");
        retired_ = add(*this, std::make_unique<ui::Checkbox>("Retir\xC3\xA9" "e (plus propos\xC3\xA9" "e)", "options.retiree"));
        deleteBtn_ = add(*this, std::make_unique<ui::Button>("Supprimer l'option\xE2\x80\xA6", "options.supprimer"));
        deleteBtn_->setStyle(ui::Button::Style::Danger);
        feats_ = add(*this, std::make_unique<ui::TableView>("options.fonctionnalites"));
        feats_->setColumns({{"Ce qu'elle ouvre", 200.f}, {"Identifiant", 150.f}});
        feats_->setSelectionMode(ui::SelectionMode::Single);
        featsModel_ = std::make_shared<Rows>(std::vector<std::string>{"Ce qu'elle ouvre", "Identifiant"});
        feats_->setModel(featsModel_);
        eds_ = add(*this, std::make_unique<ui::TableView>("options.offres"));
        eds_->setColumns({{"Propos\xC3\xA9" "e aux offres", 200.f}, {"", 150.f}});
        eds_->setSelectionMode(ui::SelectionMode::Single);
        edsModel_ = std::make_shared<Rows>(std::vector<std::string>{"Propos\xC3\xA9" "e aux offres", ""});
        eds_->setModel(edsModel_);

        links_ += label_->textChanged->connect([this](const std::string& t) {
            if (loading_) return;
            const std::string v = trim(t);
            if (v.empty()) return fieldError("nom", "le nom de l'option ne peut pas \xC3\xAAtre vide");
            if (v.size() > 200 || !plain(v)) return fieldError("nom", "le nom est trop long (200 octets au plus) ou contient un caract\xC3\xA8re interdit");
            fieldError("nom", {});
            if (auto* o = current(); o && o->label != v) {
                o->label = v;
                edited();
            }
        });
        links_ += description_->textChanged->connect([this](const std::string& t) {
            if (loading_) return;
            const std::string v = trim(t);
            if (v.size() > 200 || !plain(v)) return fieldError("description", "la description : 200 octets au plus");
            fieldError("description", {});
            if (auto* o = current(); o && o->description != v) {
                o->description = v;
                edited();
            }
        });
        links_ += trial_->textChanged->connect([this](const std::string& t) {
            if (loading_) return;
            int v = 0;
            if (!parseCount(t, 0, 366, v)) return fieldError("essai", "l'essai : un nombre de jours de 0 \xC3\xA0 366");
            fieldError("essai", {});
            if (auto* o = current(); o && o->trialDays != v) {
                o->trialDays = v;
                edited();
            }
        });
        links_ += shop_->stateChanged->connect([this](ui::Checkbox::State st) {
            if (loading_) return;
            if (auto* o = current(); o && o->shop != (st == ui::Checkbox::State::Checked)) {
                o->shop = st == ui::Checkbox::State::Checked;
                edited();
            }
        });
        links_ += retired_->stateChanged->connect([this](ui::Checkbox::State st) {
            if (loading_) return;
            if (auto* o = current(); o && o->retired != (st == ui::Checkbox::State::Checked)) {
                o->retired = st == ui::Checkbox::State::Checked;
                edited();
            }
        });
        links_ += feats_->checkClicked->connect([this](RowIndex row) { toggleFeature(row); });
        links_ += eds_->checkClicked->connect([this](RowIndex row) { toggleEdition(row); });
        links_ += deleteBtn_->clicked->connect([this] { deleteOption(); });

        newId_ = add(*this, std::make_unique<ui::InputText>("options.nouvelle.id"));
        newId_->setPlaceholder("exports_avances");
        newId_->setMaxLength(64);
        newLabel_ = add(*this, std::make_unique<ui::InputText>("options.nouvelle.nom"));
        newLabel_->setPlaceholder("Exports avanc\xC3\xA9s");
        refresh();
    }

protected:
    void reloadContent() override {
        const lic::Registry& reg = draft().reg;
        if (selected_.empty() || !reg.option(selected_)) selected_ = reg.options.empty() ? std::string() : reg.options.front().id;
        rebuildTable();
        loadPanel();
    }

    void contentEdited() override { rebuildTable(); }

    void showContent(bool on) override {
        const auto v = on ? ui::Visibility::Visible : ui::Visibility::Collapsed;
        for (ui::Widget* w : panelWidgets()) w->setVisibility(v);
        table_->setVisibility(v);
    }

    void layoutContent(const gfx::Rect& area) override {
        const float pw = std::clamp(area.w * 0.38f, 380.f, 520.f);
        table_->setBounds({area.x, area.y, std::max(120.f, area.w - pw - 12.f), area.h});
        panel_ = {area.right() - pw, area.y, pw, area.h};
        const float px = panel_.x + 12.f, pw2 = panel_.w - 24.f, lw = 110.f;
        float y = panel_.y + 32.f + 10.f;
        rows_.clear();
        rows_.push_back({"Nom", {px, y, lw, kFieldH}});
        label_->setBounds({px + lw, y, pw2 - lw, kFieldH});
        y += kFieldH + kGap;
        rows_.push_back({"Description", {px, y, lw, kFieldH}});
        description_->setBounds({px + lw, y, pw2 - lw, kFieldH});
        y += kFieldH + kGap;
        durationsY_ = y;
        y += 18.f;
        const float priceW = std::min(150.f, pw2 * 0.34f);
        for (int k = 0; k < 4; ++k) {
            float cw = pw2 - priceW - kGap;
            if (k == 3) {
                slot_[k].on->setBounds({px, y + 2.f, 120.f, 24.f});
                otherDays_->setBounds({px + 124.f, y, std::max(50.f, cw - 124.f), kFieldH});
            } else {
                slot_[k].on->setBounds({px, y + 2.f, cw, 24.f});
            }
            slot_[k].price->setBounds({px + pw2 - priceW, y, priceW, kFieldH});
            y += kFieldH + 4.f;
        }
        y += kGap;
        shop_->setBounds({px, y + 2.f, pw2 * 0.6f, 24.f});
        trialLabel_ = {px + pw2 * 0.6f + kGap, y, 60.f, kFieldH};
        trial_->setBounds({trialLabel_.right(), y, std::max(40.f, pw2 - (trialLabel_.right() - px)), kFieldH});
        y += kFieldH + kGap;
        const float bw = std::ceil(std::max(150.f, deleteBtn_->sizeHint().preferred.w));
        retired_->setBounds({px, y + 2.f, std::max(80.f, pw2 - bw - kGap), 24.f});
        deleteBtn_->setBounds({px + pw2 - bw, y, bw, kFieldH});
        y += kFieldH + kGap;
        const float rest = std::max(96.f, panel_.bottom() - 10.f - y);
        const float h1 = std::floor(rest * 0.55f);
        feats_->setBounds({px, y, pw2, h1 - 4.f});
        eds_->setBounds({px, y + h1 + 4.f, pw2, rest - h1 - 4.f});
    }

    void paintContent(const ui::PaintContext& ctx) override {
        const lic::OptionDef* o = draft().reg.option(selected_);
        if (!o) return;
        paintCard(ctx, panel_, "Option " + o->label, o->retired ? "retir\xC3\xA9" "e" : "");
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        for (const auto& fr : rows_)
            r.drawText({fr.at.x, fr.at.y + (fr.at.h - r.lineHeight(ctx.theme.font.caption)) * 0.5f}, fit(r, fr.label, ctx.theme.font.caption, fr.at.w - 4.f),
                       ctx.theme.font.caption, c.textMuted);
        r.drawText({panel_.x + 12.f, durationsY_}, "Dur\xC3\xA9" "es et prix (HT) : le client choisit", ctx.theme.font.caption, c.textMuted);
        r.drawText({trialLabel_.x, trialLabel_.y + (trialLabel_.h - r.lineHeight(ctx.theme.font.caption)) * 0.5f}, "Essai (j)", ctx.theme.font.caption, c.textMuted);
    }

    std::vector<ui::Widget*> newWidgets() override { return {newId_, newLabel_}; }

    void layoutNew(const gfx::Rect& row) override {
        const float idW = std::min(220.f, row.w * 0.35f);
        newLabels_ = {{"Identifiant", {row.x, row.y, 86.f, kFieldH}}, {"Nom", {row.x + 86.f + idW + 12.f, row.y, 40.f, kFieldH}}};
        newId_->setBounds({row.x + 86.f, row.y, idW, kFieldH});
        const float lx = row.x + 86.f + idW + 12.f + 40.f;
        newLabel_->setBounds({lx, row.y, std::max(80.f, row.right() - lx), kFieldH});
    }

    void paintNew(const ui::PaintContext& ctx) override {
        for (const auto& fr : newLabels_)
            ctx.r.drawText({fr.at.x, fr.at.y + (kFieldH - ctx.r.lineHeight(ctx.theme.font.caption)) * 0.5f}, fr.label, ctx.theme.font.caption,
                           ctx.theme.color.textMuted);
    }

    std::string newHint() const override {
        return "Identifiant : des minuscules, des chiffres et _, commen\xC3\xA7" "ant par une lettre (64 au plus). Elle part avec une dur\xC3\xA9" "e d'une semaine : "
               "coche ce qu'elle ouvre, ses dur\xC3\xA9" "es et leurs prix ensuite.";
    }

    bool create(std::string* why) override {
        lic::Registry& reg = draft().reg;
        const std::string id = trim(newId_->text());
        std::string label = trim(newLabel_->text());
        if (id.empty()) return *why = "donne un identifiant \xC3\xA0 la nouvelle option (exports_avances)", false;
        if (!lic::isFeatureId(id))
            return *why = "\xC2\xAB " + id + " \xC2\xBB n'est pas un identifiant d'option : des minuscules, des chiffres et _, commen\xC3\xA7" "ant par une lettre", false;
        if (reg.option(id)) return *why = "l'option " + id + " existe d\xC3\xA9j\xC3\xA0", false;
        if (reg.options.size() >= 64) return *why = "64 options au plus", false;
        if (label.empty()) label = id;
        if (label.size() > 200 || !plain(label)) return *why = "le nom est trop long ou contient un caract\xC3\xA8re interdit", false;
        lic::OptionDef o;
        o.id = id;
        o.label = label;
        o.durations = {{7, ""}};
        reg.options.push_back(o);
        selected_ = id;
        errors_.clear();
        warnFormat142();
        return true;
    }

    void clearNew() override {
        newId_->setText({});
        newLabel_->setText({});
    }

    std::string fieldProblem() const override {
        for (const auto& [k, v] : errors_) {
            (void)k;
            if (!v.empty()) return v;
        }
        return {};
    }

private:
    struct Slot {
        ui::Checkbox*  on{nullptr};
        ui::InputText* price{nullptr};
    };
    static constexpr int kSlotDays[3] = {7, 30, 0};
    static bool isOther(int days) { return days != 7 && days != 30 && days != 0; }

    std::vector<ui::Widget*> panelWidgets() {
        std::vector<ui::Widget*> v = {label_, description_, otherDays_, shop_, trial_, retired_, deleteBtn_, feats_, eds_};
        for (const auto& s : slot_) {
            v.push_back(s.on);
            v.push_back(s.price);
        }
        return v;
    }

    lic::OptionDef* current() {
        for (auto& o : draft().reg.options)
            if (o.id == selected_) return &o;
        return nullptr;
    }

    void fieldError(const std::string& key, std::string text) {
        auto it = std::find_if(errors_.begin(), errors_.end(), [&](const auto& p) { return p.first == key; });
        if (text.empty()) {
            if (it != errors_.end()) errors_.erase(it);
        } else if (it == errors_.end()) {
            errors_.emplace_back(key, std::move(text));
        } else {
            it->second = std::move(text);
        }
        invalidateLayout();
        invalidate();
    }

    void select(const std::string& id) {
        if (id == selected_) return;
        selected_ = id;
        errors_.clear();
        loadPanel();
        invalidateLayout();
        invalidate();
    }

    lic::OptionDuration* slotDuration(lic::OptionDef& o, int k) {
        for (auto& d : o.durations)
            if (k < 3 ? d.days == kSlotDays[k] : isOther(d.days)) return &d;
        return nullptr;
    }

    void toggleSlot(int k, bool on) {
        if (loading_) return;
        auto* o = current();
        if (!o) return;
        lic::OptionDuration* d = slotDuration(*o, k);
        if (on && !d) {
            lic::OptionDuration x;
            int days = 14;
            if (k < 3) days = kSlotDays[k];
            else if (int v = 0; parseCount(otherDays_->text(), 1, 3660, v) && !(v == 7 || v == 30)) days = v;
            x.days = days;
            x.price = trim(slot_[k].price->text());
            if (o->durations.size() >= 8) return setProblem("Refus\xC3\xA9 : 8 dur\xC3\xA9" "es au plus.");
            o->durations.push_back(x);
            std::sort(o->durations.begin(), o->durations.end(), [](const lic::OptionDuration& a, const lic::OptionDuration& b) {
                return (a.days == 0 ? 1000000 : a.days) < (b.days == 0 ? 1000000 : b.days);
            });
            edited();
            loadPanel();
        } else if (!on && d) {
            const int days = d->days;
            std::erase_if(o->durations, [&](const lic::OptionDuration& x) { return x.days == days; });
            edited();
            loadPanel();
        }
    }

    void slotPrice(int k, const std::string& t) {
        if (loading_) return;
        auto* o = current();
        if (!o) return;
        const std::string v = trim(t);
        if (v.size() > 60 || !plain(v)) return fieldError("prix", "un prix : 60 octets au plus");
        fieldError("prix", {});
        if (lic::OptionDuration* d = slotDuration(*o, k); d && d->price != v) {
            d->price = v;
            edited();
        }
    }

    void toggleFeature(RowIndex row) {
        if (row >= featRows_.size() || featRows_[row].empty()) return;
        auto* o = current();
        if (!o) return;
        const std::string& id = featRows_[row];
        auto it = std::find_if(o->features.begin(), o->features.end(), [&](const lic::FeatureGrant& g) { return g.id == id; });
        if (it != o->features.end()) o->features.erase(it);
        else {
            lic::FeatureGrant g;
            g.id = id;
            o->features.push_back(g);
            lic::normalizeGrants(o->features);
        }
        edited();
        rebuildChecks();
    }

    // Les offres : une liste vide veut dire << toutes >> ; decocher l'une d'elles garde toutes les autres.
    void toggleEdition(RowIndex row) {
        if (row >= edRows_.size()) return;
        auto* o = current();
        if (!o) return;
        const std::string& id = edRows_[row];
        if (o->editions.empty()) {
            for (const auto& e : draft().reg.editions)
                if (e.id != id) o->editions.push_back(e.id);
        } else if (std::find(o->editions.begin(), o->editions.end(), id) != o->editions.end()) {
            std::erase(o->editions, id);
            if (o->editions.empty()) return setProblem("Refus\xC3\xA9 : une option est propos\xC3\xA9" "e \xC3\xA0 au moins une offre (retire-la plut\xC3\xB4t).");
        } else {
            o->editions.push_back(id);
            if (o->editions.size() == draft().reg.editions.size()) o->editions.clear();   // toutes
        }
        edited();
        rebuildChecks();
    }

    void deleteOption() {
        const lic::OptionDef* o = draft().reg.option(selected_);
        if (!o) return;
        const std::string id = o->id, label = o->label;
        std::weak_ptr<bool> life = alive();
        const std::string text = "Elle quitte le catalogue avec le prochain registre sign\xC3\xA9 : plus propos\xC3\xA9" "e, plus montr\xC3\xA9" "e dans la boutique. "
                                 "Les options d\xC3\xA9j\xC3\xA0 ajout\xC3\xA9" "es \xC3\xA0 des licences y restent jusqu'\xC3\xA0 leur fin (chaque licence en garde la copie).";
        host_.openDialog(licdlg::makeConfirmDialog("dialog.supprimer-option", "Supprimer l'option " + label + " ?", text, "Supprimer l'option", true, [this, life, id] {
            if (life.expired()) return;
            std::erase_if(draft().reg.options, [&](const lic::OptionDef& x) { return x.id == id; });
            selected_.clear();
            errors_.clear();
            edited();
            reloadContent();
            say("Option supprim\xC3\xA9" "e du brouillon : signe le registre.", Tone::Info);
        }));
    }

    // Les licences en cours qui ont cette option en ce moment.
    int inUse(const std::string& id) const {
        int n = 0;
        const lic::Seconds t = ws().now();
        for (const auto& l : ws().licences())
            if (Workspace::inForce(l))
                for (const auto& o : l.licence.options)
                    if (o.id == id && o.activeAt(t, l.licence.expiresAt)) {
                        ++n;
                        break;
                    }
        return n;
    }

    static std::string durationLabel(int days) {
        if (days == 0) return "fin de licence";
        if (days == 7) return "la semaine";
        if (days == 30) return "le mois";
        return std::to_string(days) + " j";
    }

    void rebuildTable() {
        const lic::Registry& reg = draft().reg;
        std::vector<std::vector<std::string>> rows, keys;
        std::vector<int> marks;
        std::vector<bool> retired;
        ids_.clear();
        // 1.47.0 : toutes les options pour la barre (texts_ : ce qu'elle ouvre et ses offres une par ligne), puis la table.
        texts_.clear();
        for (const auto& o : reg.options) {
            auto t = cellsOf(o);
            t[2] = join(featureLabels(o), "\n");
            std::vector<std::string> eds;
            for (const auto& e : reg.editions)
                if (o.offeredTo(e.id)) eds.push_back(e.id);
            t[4] = join(eds, "\n");
            t.push_back(o.retired ? "retir\xC3\xA9" "e" : "propos\xC3\xA9" "e");
            t.push_back(o.shop ? "dans la boutique" : "hors boutique");
            t.push_back(o.trialDays > 0 ? "essai " + std::to_string(o.trialDays) + " j" : std::string("sans essai"));
            t.push_back(markText(markOf(o)));
            texts_.push_back(std::move(t));
        }
        bar_->refreshCounts();
        for (std::size_t i = 0; i < reg.options.size(); ++i) {
            if (!bar_->accepts(i)) continue;
            const lic::OptionDef& o = reg.options[i];
            ids_.push_back(o.id);
            rows.push_back(cellsOf(o));
            keys.push_back({"", "", "", "", "", padKey(inUse(o.id))});
            marks.push_back(markOf(o));
            retired.push_back(o.retired);
        }
        const float scroll = table_->scrollOffset();
        loading_ = true;
        model_->set(std::move(rows), std::move(keys), [marks, retired](RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (r >= marks.size()) return s;
            if (c == 0) s.monospace = true;
            if (c == 1 && marks[r]) {
                s.badge = marks[r] == 2 ? "nouvelle" : "modifi\xC3\xA9" "e";
                s.badgeTone = ui::Tone::Warning;
            }
            if (retired[r]) s.fgTone = ui::Tone::Muted;
            return s;
        });
        for (std::size_t i = 0; i < ids_.size(); ++i)
            if (ids_[i] == selected_) table_->selectModelRows({static_cast<RowIndex>(i)}, false);
        table_->setScrollOffset(scroll);
        loading_ = false;
        rebuildChecks();
    }

    // ---- 1.47.0 : la barre, le clic droit, le depliage ----
    std::vector<std::string> featureLabels(const lic::OptionDef& o) const {
        std::vector<std::string> out;
        for (const auto& g : o.features) {
            const lic::FeatureDef* f = draft().reg.feature(g.id);
            out.push_back(f ? f->label : g.id);
        }
        return out;
    }
    std::vector<std::string> cellsOf(const lic::OptionDef& o) const {
        std::vector<std::string> dl;
        for (const auto& d : o.durations) dl.push_back((d.price.empty() ? std::string("sur demande") : d.price) + " " + durationLabel(d.days));
        return {o.id, o.label, join(featureLabels(o), ", "), join(dl, " \xC2\xB7 "), o.editions.empty() ? std::string("toutes") : join(o.editions, ", "),
                std::to_string(inUse(o.id))};
    }
    int markOf(const lic::OptionDef& o) const {
        const lic::OptionDef* s = ws().hasRegistry() ? ws().registry().option(o.id) : nullptr;
        return !s ? 2 : (*s == o ? 0 : 1);
    }
    static std::vector<ui::FilterBar::Field> barFields() {
        using K = ui::FilterBar::Kind;
        std::vector<ui::FilterBar::Field> f(10);
        f[0] = {.title = "Identifiant", .kind = K::Text};
        f[1] = {.title = "Nom", .kind = K::Text};
        f[2] = {.title = "Ouvre", .chip = true, .noun = "une fonctionnalit\xC3\xA9", .split = "\n"};
        f[3] = {.title = "Dur\xC3\xA9" "es et prix", .kind = K::Text};
        f[4] = {.title = "Propos\xC3\xA9" "e \xC3\xA0", .chip = true, .chipLabel = "Offre", .noun = "une offre", .split = "\n"};
        f[5] = {.title = "En cours", .kind = K::Text};
        f[6] = {.title = "\xC3\x89tat", .chip = true, .noun = "un \xC3\xA9tat", .searchable = false, .order = {"propos\xC3\xA9" "e", "retir\xC3\xA9" "e"}};
        f[7] = {.title = "Boutique", .noun = "la boutique", .searchable = false};
        f[8] = {.title = "Essai", .noun = "un essai", .searchable = false};
        f[9] = {.title = "Brouillon", .noun = "un \xC3\xA9tat", .searchable = false, .order = {"sign\xC3\xA9" "e", "modifi\xC3\xA9" "e", "nouvelle"}};
        return f;
    }
    void applyFocus(const std::string& what) override {
        if (what.rfind("offre:", 0) == 0) {   // << Ses options >> (Offres)
            bar_->clearAll();
            bar_->filterOn(4, what.substr(6));
        } else if (what.rfind("fonction:", 0) == 0) {   // << Les options qui l'ouvrent >> (Fonctionnalites)
            bar_->clearAll();
            bar_->filterOn(2, what.substr(9));
        }
    }
    // Une case de la fiche, depuis le clic droit (le brouillon, comme la case elle-meme).
    void setFlag(const std::string& id, bool lic::OptionDef::*member, bool on) {
        for (auto& o : draft().reg.options)
            if (o.id == id && o.*member != on) {
                o.*member = on;
                edited();
                if (id == selected_) loadPanel();
            }
    }

    ui::ListMenu menuFor() {
        const auto sel = table_->selectedModelRows();
        ui::ListMenu m({sel.size(), "options", true});
        if (sel.empty() || sel.front() >= ids_.size()) return m;
        const RowIndex row = sel.front();
        const lic::Registry& reg = draft().reg;
        const lic::OptionDef* o = reg.option(ids_[row]);
        if (!o) return m;
        const std::string id = o->id, label = o->label.empty() ? o->id : o->label;
        m.unfold(*table_);
        const std::string draftNote = "dans le brouillon, \xC3\xA0 signer";
        if (o->shop) m.action({.label = "\xC3\x94ter de la boutique des Studios", .icon = ui::Icon::Close, .run = [this, id] { setFlag(id, &lic::OptionDef::shop, false); }, .note = draftNote});
        else m.action({.label = "Mettre dans la boutique des Studios", .icon = ui::Icon::Globe, .run = [this, id] { setFlag(id, &lic::OptionDef::shop, true); }, .note = draftNote});
        if (o->retired) m.action({.label = "Remettre en service", .icon = ui::Icon::Undo, .run = [this, id] { setFlag(id, &lic::OptionDef::retired, false); }, .note = draftNote});
        else m.action({.label = "Retirer (plus propos\xC3\xA9" "e)", .icon = ui::Icon::Close, .run = [this, id] { setFlag(id, &lic::OptionDef::retired, true); },
                       .note = draftNote + " ; les licences qui l'ont la gardent"});
        const int n = inUse(id);
        m.goTo({.label = "Les licences qui l'ont (" + std::to_string(n) + ")", .icon = ui::Icon::Document,
                .why = n ? std::string() : std::string("aucune licence en cours ne l'a"), .run = [this, id] { goTo("licences", "option:" + id); }});
        m.goTo({.label = "Ce qu'elle ouvre (Fonctionnalit\xC3\xA9s)", .icon = ui::Icon::Module, .why = o->features.empty() ? std::string("elle n'ouvre rien") : std::string(),
                .run = [this, label] { goTo("fonctionnalites", "option:" + label); }});
        const auto fl = featureLabels(*o);
        for (std::size_t k = 0; k < fl.size() && k < 5; ++k) m.filterOn({.label = "Ouvre : " + fl[k], .run = [this, v = fl[k]] { bar_->filterOn(2, v); }});
        for (std::size_t k = 0; k < o->editions.size() && k < 5; ++k)
            m.filterOn({.label = "Propos\xC3\xA9" "e \xC3\xA0 : " + o->editions[k], .run = [this, v = o->editions[k]] { bar_->filterOn(4, v); }});
        const std::string st = o->retired ? "retir\xC3\xA9" "e" : "propos\xC3\xA9" "e";
        m.filterOn({.label = "Cet \xC3\xA9tat : " + st, .run = [this, st] { bar_->filterOn(6, st); }});
        m.copy("L'identifiant", id, ui::Icon::Copy);
        m.copy("Le nom", o->label, ui::Icon::Copy);
        m.copy("La ligne", lineOf(*table_, row), ui::Icon::Copy);
        m.exportRows([this, sel] { listkit::exportRows(host_, *table_, sel, "options.csv", "options"); });
        m.danger({.label = "Supprimer l'option", .icon = ui::Icon::Delete, .run = [this, id] {
                      select(id);
                      deleteOption();
                  }});
        return m;
    }

    // La ligne depliee : ce qu'elle ouvre, ses durees et prix, ou elle est proposee, les licences qui l'ont (et leur courbe).
    ui::WidgetPtr unfoldFor(RowIndex r) {
        if (r >= ids_.size()) return nullptr;
        const lic::Registry& reg = draft().reg;
        const lic::OptionDef* o = reg.option(ids_[r]);
        if (!o) return nullptr;
        const std::string id = o->id;
        auto card = std::make_unique<ui::UnfoldCard>("options.depliage");
        {
            ui::UnfoldCard::Box b{.title = "CE QU'ELLE OUVRE"};
            for (const auto& g : o->features) {
                const lic::FeatureDef* f = reg.feature(g.id);
                if (b.lines.size() == 4) {
                    b.lines.push_back({.icon = ui::Icon::Module, .text = "et " + std::to_string(o->features.size() - 4) + " autre(s)"});
                    break;
                }
                b.lines.push_back({.icon = ui::Icon::Module, .text = "**" + (f ? f->label : g.id) + "**" + (f ? " \xC2\xB7 " + f->product : std::string())});
            }
            if (b.lines.empty()) b.lines.push_back({.icon = ui::Icon::Info, .text = "rien : coche ce qu'elle ouvre dans la fiche", .tone = ui::Tone::Warning});
            card->addBox(std::move(b));
        }
        {
            ui::UnfoldCard::Box b{.title = "SES DUR\xC3\x89" "ES ET PRIX"};
            for (const auto& d : o->durations)
                b.lines.push_back({.icon = ui::Icon::History, .text = "**" + (d.price.empty() ? std::string("sur demande") : d.price) + "** \xC2\xB7 " + durationLabel(d.days)});
            b.lines.push_back({.icon = ui::Icon::Star, .text = o->trialDays > 0 ? "essai offert : **" + std::to_string(o->trialDays) + " j** (une fois par client)" : std::string("pas d'essai")});
            card->addBox(std::move(b));
        }
        {
            ui::UnfoldCard::Box b{.title = "O\xC3\x99 ELLE EST PROPOS\xC3\x89" "E"};
            b.lines.push_back({.icon = ui::Icon::Layers, .text = "aux offres : **" + (o->editions.empty() ? std::string("toutes") : join(o->editions, ", ")) + "**"});
            b.lines.push_back({.icon = ui::Icon::Globe, .text = o->shop ? std::string("dans la boutique des Studios") : std::string("hors de la boutique des Studios"),
                               .check = o->shop});
            const int n = inUse(id);
            ui::UnfoldCard::Line l{.icon = ui::Icon::Document, .text = "**" + std::to_string(n) + "** licence" + (n > 1 ? "s l'ont" : " l'a") + " en ce moment"};
            if (n) l.link = [this, id] { goTo("licences", "option:" + id); };
            b.lines.push_back(std::move(l));
            if (o->retired) b.lines.push_back({.icon = ui::Icon::Info, .text = "retir\xC3\xA9" "e : plus propos\xC3\xA9" "e", .tone = ui::Tone::Muted});
            card->addBox(std::move(b));
        }
        addCurve(*card, "LICENCES AVEC L'OPTION, 12 MOIS", weekly([id](const LicenceEntry& l, Seconds t) {
                     for (const auto& x : l.licence.options)
                         if (x.id == id && x.activeAt(t, l.licence.expiresAt)) return true;
                     return false;
                 }));
        if (inUse(id) > 0) card->addButton("Voir les licences", "options.depliage.licences", ui::Icon::GoTo, [this, id] { goTo("licences", "option:" + id); });
        addSigner(*card);
        return card;
    }

    void rebuildChecks() {
        const lic::Registry& reg = draft().reg;
        const lic::OptionDef* o = reg.option(selected_);
        {
            std::vector<std::vector<std::string>> rows;
            std::vector<int> kind;
            featRows_.clear();
            for (const auto& product : productsOf(reg)) {
                rows.push_back({product, ""});
                kind.push_back(-1);
                featRows_.push_back({});
                for (const auto& f : reg.features) {
                    if (f.product != product || f.retired) continue;
                    const bool on = o && std::any_of(o->features.begin(), o->features.end(), [&](const lic::FeatureGrant& g) { return g.id == f.id; });
                    rows.push_back({f.label, f.id});
                    kind.push_back(on ? 1 : 0);
                    featRows_.push_back(f.id);
                }
            }
            const float scroll = feats_->scrollOffset();
            featsModel_->set(std::move(rows), {}, [kind](RowIndex r, std::size_t c) {
                ui::CellStyle s;
                if (r >= kind.size()) return s;
                if (kind[r] < 0) {
                    if (c == 0) s.spanRow = true;
                    s.bold = true;
                    return s;
                }
                if (c == 0) s.check = static_cast<std::int8_t>(kind[r]);
                if (c == 1) s.monospace = true;
                return s;
            });
            feats_->setScrollOffset(scroll);
        }
        {
            std::vector<std::vector<std::string>> rows;
            std::vector<int> kind;
            std::vector<bool> retired;
            edRows_.clear();
            for (const auto& e : reg.editions) {
                const bool on = o && o->offeredTo(e.id);
                bool has = o && !o->features.empty();   // l'offre ouvre deja tout ce que l'option ouvre
                if (has)
                    for (const auto& g : o->features)
                        if (!grants(e, g.id)) has = false;
                rows.push_back({e.label.empty() ? e.id : e.label, has ? std::string("l'a d\xC3\xA9j\xC3\xA0") : std::string()});
                kind.push_back(on ? 1 : 0);
                retired.push_back(e.retired);
                edRows_.push_back(e.id);
            }
            edsModel_->set(std::move(rows), {}, [kind, retired](RowIndex r, std::size_t c) {
                ui::CellStyle s;
                if (r >= kind.size()) return s;
                if (c == 0) s.check = static_cast<std::int8_t>(kind[r]);
                if (c == 1 || retired[r]) s.fgTone = ui::Tone::Muted;
                return s;
            });
        }
    }

    void loadPanel() {
        lic::OptionDef* o = current();
        errors_.clear();
        loading_ = true;
        if (o) {
            label_->setText(o->label);
            description_->setText(o->description);
            for (int k = 0; k < 4; ++k) {
                const lic::OptionDuration* d = slotDuration(*o, k);
                slot_[k].on->setState(d ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
                slot_[k].price->setText(d ? d->price : std::string());
                slot_[k].price->setEnabled(d != nullptr);
                if (k == 3) otherDays_->setText(d ? std::to_string(d->days) : std::string());
            }
            shop_->setState(o->shop ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
            trial_->setText(std::to_string(o->trialDays));
            retired_->setState(o->retired ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        }
        for (ui::Widget* w : panelWidgets()) w->setEnabled(o != nullptr);
        if (o)
            for (int k = 0; k < 4; ++k) slot_[k].price->setEnabled(slotDuration(*o, k) != nullptr);
        loading_ = false;
        rebuildChecks();
    }

    ui::TableView*        table_{nullptr};
    std::shared_ptr<Rows> model_;
    std::vector<std::string> ids_;
    std::string           selected_;
    gfx::Rect             panel_{}, trialLabel_{};
    std::vector<FieldRow> rows_, newLabels_;
    float                 durationsY_{0};
    ui::InputText*        label_{nullptr};
    ui::InputText*        description_{nullptr};
    Slot                  slot_[4];
    ui::InputText*        otherDays_{nullptr};
    ui::Checkbox*         shop_{nullptr};
    ui::InputText*        trial_{nullptr};
    ui::Checkbox*         retired_{nullptr};
    ui::Button*           deleteBtn_{nullptr};
    ui::TableView*        feats_{nullptr};
    std::shared_ptr<Rows> featsModel_;
    std::vector<std::string> featRows_;
    ui::TableView*        eds_{nullptr};
    std::shared_ptr<Rows> edsModel_;
    std::vector<std::string> edRows_;
    ui::InputText*        newId_{nullptr};
    ui::InputText*        newLabel_{nullptr};
    std::vector<std::pair<std::string, std::string>> errors_;
    bool                  loading_{false};
};

} // namespace

PagePtr makeEditionsPage(ShellHost host) { return std::make_unique<EditionsPage>(std::move(host)); }
PagePtr makeOptionsPage(ShellHost host) { return std::make_unique<OptionsPage>(std::move(host)); }   // 1.42.0
PagePtr makeFeaturesPage(ShellHost host) { return std::make_unique<FeaturesPage>(std::move(host)); }

} // namespace license_admin::gui
