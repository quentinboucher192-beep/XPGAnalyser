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
#include "Page.hpp"

#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

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
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
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
        if (!what.empty()) out.push_back("\xC3\xA9" "dition " + e.id + " : " + join(what, ", "));
    }
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
        if (hasRegistry_) reloadContent();
        invalidateLayout();
        invalidate();
    }

protected:
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
        if (!ws().key().present) note = "Il faut d'abord la cl\xC3\xA9 de l'\xC3\xA9" "diteur : Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9.";
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
        signBtn_->setEnabled(d.dirty && !host_.busy());
        cancelBtn_->setEnabled(d.dirty);
        firstBtn_->setEnabled(ws().key().present && !host_.busy());
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
            std::string text = e->price.empty() ? std::string() : "Au lieu de " + e->price + ". ";
            text += "Jusqu'au " + lic::isoDate(e->promoUntil) + ".";
            if (!e->description.empty()) text += " " + e->description;
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
        if (!ws().key().present) {
            host_.report(OpResult::fail(ws().key().problem.empty() ? std::string("pas de cl\xC3\xA9 de l'\xC3\xA9" "diteur sur ce poste") : ws().key().problem));
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
        : RegistryPage("page.editions", std::move(host), "editions", "\xC3\x89" "ditions",
                       "Ce qu'ouvre chaque \xC3\xA9" "dition et ses valeurs par d\xC3\xA9" "faut pour une licence neuve. Un changement arrive chez un client "
                       "avec sa prochaine licence : le registre sign\xC3\xA9 voyage dans chaque fichier de licence.",
                       "Nouvelle \xC3\xA9" "dition\xE2\x80\xA6") {
        table_ = add(*this, std::make_unique<ui::TableView>("editions.table"));
        table_->setColumns({{"Identifiant", 160.f}, {"Libell\xC3\xA9", 160.f}, {"Type", 110.f}, {"PC", 56.f}, {"Dur\xC3\xA9" "e", 84.f}, {"Gr\xC3\xA2" "ce", 76.f},
                            {"Fonctionnalit\xC3\xA9s", 150.f}, {"Retir\xC3\xA9" "e", 90.f}});
        table_->setSelectionMode(ui::SelectionMode::Single);
        model_ = std::make_shared<Rows>(std::vector<std::string>{"Identifiant", "Libell\xC3\xA9", "Type", "PC", "Dur\xC3\xA9" "e", "Gr\xC3\xA2" "ce", "Fonctionnalit\xC3\xA9s", "Retir\xC3\xA9" "e"});
        table_->setModel(model_);
        links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>& rows) {
            if (loading_ || rows.empty() || rows.front() >= ids_.size()) return;
            select(ids_[rows.front()]);
        });

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
        promoEnd_ = add(*this, std::make_unique<ui::InputText>("editions.reduction-fin"));
        promoEnd_->setPlaceholder("jusqu'au AAAA-MM-JJ");
        promoEnd_->setTooltip("Le dernier jour de la r\xC3\xA9" "duction (AAAA-MM-JJ) : apr\xC3\xA8s, la boutique reprend le prix normal d'elle-m\xC3\xAAme.");
        promoEnd_->setMaxLength(10);
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
        links_ += promoEnd_->textChanged->connect([this](const std::string& t) {
            if (loading_) return;
            const std::string v = trim(t);
            lic::Seconds when = 0;
            if (!v.empty() && !lic::parseIsoTime(v + "T23:59:59Z", when))
                return fieldError("reduction-fin", "la fin de la r\xC3\xA9" "duction : une date AAAA-MM-JJ (le dernier jour)");
            fieldError("reduction-fin", {});
            if (auto* e = current(); e && e->promoUntil != when) {
                e->promoUntil = when;
                checkPromo();
                edited();
            }
        });
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
        float y = panel_.y + 32.f + 10.f;
        rows_.clear();
        const auto row = [&](const char* text, ui::Widget* w) {
            rows_.push_back({text, {px, y, lw, kFieldH}});
            w->setBounds({px + lw, y, pw2 - lw, kFieldH});
            y += kFieldH + kGap;
        };
        row("Libell\xC3\xA9", label_);
        row("Type par d\xC3\xA9" "faut", type_);
        row("Prix (boutique)", price_);          // 1.37.0
        row("Description", description_);
        // 1.38.0 : la reduction - le prix reduit et sa fin sur une ligne (comme Prix).
        {
            const float room = pw2 - lw, wa = std::floor((room - kGap) * 0.58f), wb = room - kGap - wa;
            rows_.push_back({"R\xC3\xA9" "duction", {px, y, lw, kFieldH}});
            promo_->setBounds({px + lw, y, wa, kFieldH});
            promoEnd_->setBounds({px + lw + wa + kGap, y, wb, kFieldH});
            y += kFieldH + kGap;
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
        y = fy + kFieldH + kGap;
        // Retiree, et 1.38.0 : Prevenir mes clients (la reduction), sur la meme ligne.
        retired_->setBounds({px, y, std::floor(pw2 * 0.42f), 24.f});
        announce_->setBounds({px + std::floor(pw2 * 0.42f) + kGap, y, pw2 - std::floor(pw2 * 0.42f) - kGap, 24.f});
        y += 24.f + kGap;
        featsHeadY_ = y;
        y += 22.f;
        feats_->setBounds({px, y, pw2, std::max(48.f, panel_.bottom() - 10.f - y)});
    }

    void paintContent(const ui::PaintContext& ctx) override {
        const lic::Registry& reg = draft().reg;
        const lic::EditionDef* e = reg.edition(selected_);
        if (!e) return;
        const bool free = e->id == reg.unlicensedEdition;
        paintCard(ctx, panel_, "\xC3\x89" "dition " + e->id, free ? "\xC3\xA9" "dition sans licence" : (e->retired ? "retir\xC3\xA9" "e" : ""));
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
    std::vector<ui::Widget*> panelWidgets() { return {label_, type_, price_, description_, promo_, promoEnd_, announce_, pcs_, days_, grace_, retired_, feats_}; }

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
        const lic::Registry* signedReg = ws().hasRegistry() ? &ws().registry() : nullptr;
        std::vector<std::vector<std::string>> rows, keys;
        std::vector<int> marks;   // 0 pareille, 1 modifiee, 2 nouvelle
        ids_.clear();
        for (const auto& e : reg.editions) {
            ids_.push_back(e.id);
            rows.push_back({e.id, e.label, lic::licenseTypeLabel(e.type), std::to_string(e.maxDevices), daysText(e.durationDays),
                            std::to_string(e.offlineGraceDays) + " j", std::to_string(e.features.size()), e.retired ? "retir\xC3\xA9" "e" : ""});
            keys.push_back({"", "", "", padKey(e.maxDevices), padKey(e.durationDays > 0 ? e.durationDays : 999999), padKey(e.offlineGraceDays),
                            padKey(static_cast<long long>(e.features.size())), ""});
            const lic::EditionDef* o = signedReg ? signedReg->edition(e.id) : nullptr;
            int mark = 0;
            if (!o) mark = 2;
            else {
                lic::Registry a, b;
                a.editions = {*o};
                b.editions = {e};
                if (!changesOf(a, b).empty()) mark = 1;
            }
            marks.push_back(mark);
        }
        const std::string free = reg.unlicensedEdition;
        const std::vector<std::string> idsCopy = ids_;
        std::vector<bool> retired;
        for (const auto& e : reg.editions) retired.push_back(e.retired);
        const float scroll = table_->scrollOffset();
        loading_ = true;
        model_->set(std::move(rows), std::move(keys), [idsCopy, free, marks, retired](RowIndex r, std::size_t c) {
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
            if (retired[r]) s.fgTone = ui::Tone::Muted;
            return s;
        });
        for (std::size_t i = 0; i < ids_.size(); ++i)
            if (ids_[i] == selected_) table_->selectModelRows({static_cast<RowIndex>(i)}, false);
        table_->setScrollOffset(scroll);
        loading_ = false;
        rebuildFeatures();
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
            promoEnd_->setText(e->promoUntil ? lic::isoDate(e->promoUntil) : std::string());
            announce_->setState(draft().announce.count(e->id) ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
            retired_->setState(e->retired ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        }
        for (ui::Widget* w : panelWidgets()) w->setEnabled(e != nullptr);
        loading_ = false;
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
    ui::InputText*        promoEnd_{nullptr};
    ui::Checkbox*         announce_{nullptr};
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
        const lic::Registry* signedReg = ws().hasRegistry() ? &ws().registry() : nullptr;
        std::vector<std::vector<std::string>> rows, keys;
        std::vector<int> marks;
        std::vector<bool> retired;
        ids_.clear();
        for (const auto& f : reg.features) {
            ids_.push_back(f.id);
            std::string limits;
            for (const auto& l : f.limits) limits += (limits.empty() ? "" : ", ") + l.id;
            std::size_t used = 0;
            for (const auto& e : reg.editions)
                if (grants(e, f.id)) ++used;
            rows.push_back({f.id, f.product, f.label, limits, f.retired ? "retir\xC3\xA9" "e" : "", std::to_string(used)});
            keys.push_back({"", "", "", "", "", padKey(static_cast<long long>(used))});
            const lic::FeatureDef* o = signedReg ? signedReg->feature(f.id) : nullptr;
            int mark = 0;
            if (!o) mark = 2;
            else if (o->label != f.label || o->product != f.product || o->retired != f.retired || !sameLimits(o->limits, f.limits)) mark = 1;
            marks.push_back(mark);
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

} // namespace

PagePtr makeEditionsPage(ShellHost host) { return std::make_unique<EditionsPage>(std::move(host)); }
PagePtr makeFeaturesPage(ShellHost host) { return std::make_unique<FeaturesPage>(std::move(host)); }

} // namespace license_admin::gui
