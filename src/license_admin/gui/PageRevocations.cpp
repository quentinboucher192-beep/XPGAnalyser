// =============================================================================
//  license_admin/gui/PageRevocations.cpp - 1.33.0 : LA PAGE << REVOCATIONS >>
// -----------------------------------------------------------------------------
//  La liste de revocation signee de l'archive (son numero, sa date) et chacune
//  de ses entrees : la licence, son client, le PC (ou toute la licence), la
//  raison, la date, les emissions touchees (toutes, ou celles d'avant une date).
//  Les gestes :
//    Revoquer...                 le dialogue de la page Licences, la licence
//                                choisie dans une liste ;
//    Retirer la revocation       apres confirmation : une nouvelle liste sans elle ;
//    Exporter la liste...        le .xpgrev a envoyer aux clients.
//  Sans service en ligne, une revocation n'arrive chez un client qu'avec un
//  fichier qu'il importe : la page le dit.
//
//  1.47.0 (planches 06, 07, 12) : la barre << Filtrer et chercher >> (les puces
//  Raison, Par qui, Periode), le clic droit dans l'ordre de la regle (Ouvrir la
//  licence, Deplier ; Aller a ; Filtrer sur ; Copier ; Exporter ; en rouge :
//  Retirer la revocation..., Revoquer toute la licence...), la ligne depliee
//  (la licence, ce qui est revoque et par qui, son histoire). << Par >> : la
//  ligne du journal des admins qui l'a revoquee (vide : le journal ne la
//  connait pas). Plusieurs lignes se choisissent (Exporter, Copier) ; Retirer
//  vise une ligne.
// =============================================================================
#include "AdminsKit.hpp"   // 1.47.0 : les dates (<< hier 17:20 >>)
#include "ListKit.hpp"     // 1.47.0 : la barre de filtres, le clic droit, le depliage
#include "Page.hpp"

#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace licdlg {
// Definis dans LicenceWizard.cpp.
menu::MenuPtr makeRevokeDialog(ShellHost host, std::string licenseId);
menu::MenuPtr makeConfirmDialog(std::string id, std::string title, std::string text, std::string okLabel, bool danger,
                                std::function<void()> onOk);
std::string fingerprintOfKey(const std::string& hex);
std::string startFor(const std::string& fileName);
void rememberFolder(const std::string& filePath);
} // namespace licdlg

namespace {

namespace fs = std::filesystem;

constexpr float kPad = 16.f;

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string extOf(const std::string& path) {
    const auto u = fs::path(std::u8string(path.begin(), path.end())).extension().u8string();
    return lower(std::string(u.begin(), u.end()));
}

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

// Couper un texte en lignes de `width` pixels au plus.
std::vector<std::string> wrapText(const std::string& text, gfx::FontId f, float width) {
    std::vector<std::string> out;
    std::string line;
    std::size_t i = 0;
    while (i <= text.size()) {
        std::size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        const std::string word = text.substr(i, j - i);
        const std::string tried = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(tried, f) > width) {
            out.push_back(line);
            line = word;
        } else {
            line = tried;
        }
        i = j + 1;
    }
    out.push_back(line);
    return out;
}

std::string padKey(long long v) {
    char b[32];
    std::snprintf(b, sizeof b, "%012lld", v);
    return b;
}

// 1.47.0 : le modele refait en place (la barre filtre, la selection et les lignes depliees restent).
class Rows final : public ui::ITableModel {
public:
    using Style = std::function<ui::CellStyle(ui::RowIndex, std::size_t)>;
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
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string();
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override { return style_ ? style_(r, c) : ui::CellStyle{}; }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return key(a, c) < key(b, c); }

private:
    [[nodiscard]] std::string key(ui::RowIndex r, std::size_t c) const {
        if (r < keys_.size() && c < keys_[r].size() && !keys_[r][c].empty()) return keys_[r][c];
        return cellText(r, c);
    }
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_, keys_;
    Style                                 style_;
};

template <class T>
T& adopt(ui::Widget& parent, std::unique_ptr<T> w) {
    return static_cast<T&>(parent.addChild(std::move(w)));
}

ui::Button& button(ui::Widget& parent, std::string text, std::string id, ui::Button::Style style = ui::Button::Style::Default) {
    auto& b = adopt(parent, std::make_unique<ui::Button>(std::move(text), std::move(id)));
    b.setStyle(style);
    return b;
}

const char* const kExplain =
    "Une r\xC3\xA9vocation n'arrive chez un client qu'avec un fichier qu'il importe : sa prochaine licence (la liste y est jointe), ou la liste "
    "export\xC3\xA9" "e ici. Il n'y a pas de service en ligne : rien n'est envoy\xC3\xA9 tout seul.";
constexpr const char* kBusy = "Un travail est en cours : attends qu'il finisse.";

// Les colonnes du modele : les 7 montrees, puis la cle d'une ligne (le depliage la suit ; une licence peut avoir
// plusieurs PC revoques).
constexpr std::size_t kKeyColumn = 7;

class RevocationsPage final : public Page {
public:
    explicit RevocationsPage(ShellHost host) : Page("revocations", std::move(host)) {
        revoke_ = &button(*this, "R\xC3\xA9voquer\xE2\x80\xA6", "revocations.revoquer", ui::Button::Style::Danger);
        revoke_->setTooltip("R\xC3\xA9voquer une licence de l'archive, ou un de ses PC");
        remove_ = &button(*this, "Retirer la r\xC3\xA9vocation", "revocations.retirer");
        remove_->setTooltip("Une nouvelle liste, sign\xC3\xA9" "e, sans la r\xC3\xA9vocation choisie");
        export_ = &button(*this, "Exporter la liste\xE2\x80\xA6", "revocations.exporter");
        export_->setTooltip("La liste sign\xC3\xA9" "e (.xpgrev) \xC3\xA0 envoyer aux clients : ils l'importent comme une licence");
        table_ = &adopt(*this, std::make_unique<ui::TableView>("revocations.table"));
        table_->setSelectionMode(ui::SelectionMode::Extended);   // 1.47.0 : plusieurs (Exporter, Copier)
        table_->setAlternatingRowColors(true);
        // 1.47.0 : << Par >> (qui l'a revoquee) apres la raison - les puces dans l'ordre de la planche 12 (Raison, Par qui, Periode).
        table_->setColumns({{"Licence", 130.f}, {"Client", 165.f}, {"PC", 220.f}, {"Raison", 180.f}, {"Par", 150.f}, {"R\xC3\xA9voqu\xC3\xA9" "e le", 105.f},
                            {"\xC3\x89missions", 150.f}});
        model_ = std::make_shared<Rows>(std::vector<std::string>{"Licence", "Client", "PC", "Raison", "Par", "R\xC3\xA9voqu\xC3\xA9" "e le", "\xC3\x89missions", "Cl\xC3\xA9"});
        table_->setModel(model_);
        // 1.47.0 (planche 12) : LA BARRE - chercher (revocations.recherche), la raison, par qui, la periode.
        bar_ = &adopt(*this, std::make_unique<ui::FilterBar>("revocations.barre", barFields(), "revocations.recherche"));
        bar_->setNoun("r\xC3\xA9vocations");
        bar_->setClock([this] { return listkit::seconds(ws().now()); });
        {
            ui::FilterBar::Source src;
            src.rows = [this] { return all_.size(); };
            src.text = [this](std::size_t r, std::size_t f) { return r < texts_.size() && f < texts_[r].size() ? texts_[r][f] : std::string(); };
            src.time = [this](std::size_t r, std::size_t f) -> std::int64_t {
                return r < all_.size() && f == 5 ? listkit::seconds(all_[r].x.revokedAt) : 0;
            };
            src.extra = [this](std::size_t r) { return r < all_.size() ? all_[r].x.device + " " + all_[r].fingerprint : std::string(); };
            bar_->setSource(std::move(src));
        }
        bar_->linkTable(*table_);
        bar_->setMemoryKey("revocations");
        links_ += bar_->changed->connect([this] { rebuild(); });
        links_ += ui::ListMenu::bind(*table_, [this] { return menuFor(); });                // 1.47.0 (planche 06)
        table_->setUnfold([this](ui::RowIndex r) { return unfoldFor(r); }, static_cast<int>(kKeyColumn));   // (planche 07)
        table_->setEnterActivates(true);
        links_ += table_->activated->connect([this](ui::RowIndex r) {   // Entree, le double-clic : la licence
            if (const Item* it = itemAt(r); it && it->inArchive) openLicence(it->x.licenseId);
        });

        links_ += revoke_->clicked->connect([this] {
            if (ws().licences().empty()) {
                host_.flash("Aucune licence dans l'archive \xC3\xA0 r\xC3\xA9voquer.", Tone::Warning);
                return;
            }
            host_.openDialog(licdlg::makeRevokeDialog(host_, {}));
        });
        links_ += remove_->clicked->connect([this] { askRemove(); });
        links_ += export_->clicked->connect([this] { exportList(); });
        links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) { updateButtons(); });
        refresh();
    }
    ~RevocationsPage() override { *alive_ = false; }

    [[nodiscard]] std::string title() const override { return "R\xC3\xA9vocations"; }

    void refresh() override {
        const Workspace& w = ws();
        const auto& list = w.revocations().revoked;
        who_.reset();
        all_.clear();
        texts_.clear();
        for (std::size_t i = 0; i < list.size(); ++i) {
            Item it;
            it.index = i;
            it.x = list[i];
            const LicenceEntry* l = w.licence(it.x.licenseId);
            it.inArchive = l != nullptr;
            it.customer = l ? l->licence.customer : std::string();
            it.pc = "toute la licence";
            if (!it.x.device.empty()) {
                it.fingerprint = licdlg::fingerprintOfKey(it.x.device);
                it.pc = it.fingerprint;
                if (l)
                    if (const auto* d = l->licence.device(it.x.device); d && !d->name.empty()) {
                        it.pcName = d->name;
                        it.pc = d->name + " \xE2\x80\x94 " + it.fingerprint;
                    }
            }
            it.emissions = it.x.issuedBefore ? "avant le " + dateTimeFr(it.x.issuedBefore) : std::string("toutes");
            it.by = revokedBy(it.x);
            texts_.push_back({it.x.licenseId, l ? it.customer : std::string("(pas dans l'archive)"), it.pc, it.x.reason, it.by ? it.by->who : std::string(),
                              dateFr(it.x.revokedAt), it.emissions});
            all_.push_back(std::move(it));
        }
        bar_->refreshCounts();
        // 1.47.0 : << La revocation >> d'une autre page (l'Historique) : la barre filtree sur sa licence.
        if (const std::string f = takePageFocus("revocations"); f.rfind("licence:", 0) == 0) {
            bar_->clearAll();
            bar_->filterOn(0, f.substr(8));
        }
        rebuild();
        invalidateLayout();
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float x = r.x + kPad, w = std::max(200.f, r.w - 2.f * kPad);
        explainLines_ = std::min<std::size_t>(3, wrapText(kExplain, gfx::FontId{16}, w).size());
        statusY_ = r.y + kPad + 38.f + 20.f * static_cast<float>(explainLines_) + 6.f;
        actionsY_ = statusY_ + 30.f;
        float bx = x;
        for (ui::Button* b : {revoke_, remove_, export_}) {
            const float bw = std::max(110.f, b->sizeHint().preferred.w);
            b->setBounds({bx, actionsY_, bw, 32.f});
            bx += bw + 8.f;
        }
        float top = actionsY_ + 42.f;
        const float bh = bar_->heightFor(w);   // 1.47.0 : la barre de filtres, au-dessus de la table
        bar_->setBounds({x, top, w, bh});
        top += bh + 8.f;
        table_->setBounds({x, top, w, std::max(120.f, r.bottom() - kPad - top)});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        r.fillRect(b, c.panelBg);
        const float x = b.x + kPad, w = b.w - 2.f * kPad;
        r.drawText({x, b.y + kPad}, title(), ctx.theme.font.title, c.text);
        const auto lines = wrapText(kExplain, ctx.theme.font.ui, w);
        for (std::size_t i = 0; i < lines.size() && i < 3; ++i)
            r.drawText({x, b.y + kPad + 38.f + 20.f * static_cast<float>(i)}, fit(r, lines[i], ctx.theme.font.ui, w), ctx.theme.font.ui, c.textMuted);
        const Workspace& ws0 = ws();
        std::string status;
        if (!ws0.hasRevocations())
            status = "Aucune liste de r\xC3\xA9vocation sign\xC3\xA9" "e pour l'instant.";
        else {
            const auto& list = ws0.revocations();
            const auto n = list.revoked.size();
            status = "Liste n\xC2\xB0 " + std::to_string(list.number) + ", sign\xC3\xA9" "e le " + dateTimeFr(list.issuedAt) + " \xE2\x80\x94 "
                     + (n == 0 ? std::string("aucune r\xC3\xA9vocation") : std::to_string(n) + (n == 1 ? " r\xC3\xA9vocation" : " r\xC3\xA9vocations")) + ".";
        }
        r.drawText({x, statusY_}, fit(r, status, ctx.theme.font.ui, w), ctx.theme.font.ui, c.text);
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (!shown_.empty()) return;
        const auto t = table_->bounds();
        const std::string text = ws().revocations().revoked.empty()
                                     ? "Rien n'est r\xC3\xA9voqu\xC3\xA9. R\xC3\xA9voquer\xE2\x80\xA6 pour retirer une licence ou un PC (fin de contrat, PC remplac\xC3\xA9\xE2\x80\xA6)."
                                     : "Aucune r\xC3\xA9vocation ne correspond \xC3\xA0 la recherche ou aux filtres.";
        ctx.r.drawText({t.x + 16.f, t.y + 44.f}, fit(ctx.r, text, ctx.theme.font.ui, t.w - 32.f), ctx.theme.font.ui, ctx.theme.color.textMuted);
    }

private:
    struct Item {
        std::size_t                 index{0};      // sa place dans la liste signee (Retirer la vise)
        licensing::Revocation       x;
        std::string                 customer, pc, pcName, fingerprint, emissions;
        std::optional<listkit::Did> by;            // la ligne du journal des admins qui l'a revoquee
        bool                        inArchive{false};
    };

    // 1.47.0 (planche 12) : les colonnes vues par la barre (dans l'ordre du modele).
    static std::vector<ui::FilterBar::Field> barFields() {
        using K = ui::FilterBar::Kind;
        std::vector<ui::FilterBar::Field> f(7);
        f[0] = {.title = "Licence", .noun = "une licence"};
        f[1] = {.title = "Client", .noun = "un client"};
        f[2] = {.title = "PC", .kind = K::Text};
        f[3] = {.title = "Raison", .chip = true, .noun = "une raison"};
        f[4] = {.title = "Par", .chip = true, .chipLabel = "Par qui", .noun = "un admin"};
        f[5] = {.title = "R\xC3\xA9voqu\xC3\xA9" "e le", .kind = K::Period, .chip = true, .chipLabel = "P\xC3\xA9riode"};
        f[6] = {.title = "\xC3\x89missions", .noun = "des \xC3\xA9missions"};
        return f;
    }

    // La ligne du journal des admins de cette revocation : << a revoque (un PC de) la licence ... >>, la plus proche de sa date.
    [[nodiscard]] std::optional<listkit::Did> revokedBy(const licensing::Revocation& x) const {
        std::optional<listkit::Did> best;
        Seconds gap = 0;
        for (const auto& d : who_.history(x.licenseId)) {
            if (d.action != "licence_revoquer" || d.summary.rfind("a r\xC3\xA9voqu\xC3\xA9", 0) != 0) continue;   // pas << a retire la revocation >>
            if ((d.summary.find("un PC") != std::string::npos) != !x.device.empty()) continue;
            const Seconds g = d.when > x.revokedAt ? d.when - x.revokedAt : x.revokedAt - d.when;
            if (!best || g < gap) {
                best = d;
                gap = g;
            }
        }
        return best;
    }

    [[nodiscard]] const Item* itemAt(ui::RowIndex r) const { return r < shown_.size() && shown_[r] < all_.size() ? &all_[shown_[r]] : nullptr; }

    [[nodiscard]] static std::string keyOf(const Item& it) {
        return it.x.licenseId + "|" + it.x.device + "|" + std::to_string(static_cast<long long>(it.x.revokedAt));
    }

    void rebuild() {
        std::vector<std::string> keep;   // la selection, retrouvee par sa cle
        for (const ui::RowIndex r : table_->selectedModelRows())
            if (const Item* it = itemAt(r)) keep.push_back(keyOf(*it));
        shown_.clear();
        for (std::size_t i = 0; i < all_.size(); ++i)
            if (bar_->accepts(i)) shown_.push_back(i);
        std::vector<std::vector<std::string>> rows, keys;
        std::vector<bool> whole;
        std::vector<ui::RowIndex> again;
        for (std::size_t k = 0; k < shown_.size(); ++k) {
            const Item& it = all_[shown_[k]];
            auto cells = texts_[shown_[k]];
            cells.push_back(keyOf(it));
            rows.push_back(std::move(cells));
            keys.push_back({"", "", "", "", "", padKey(it.x.revokedAt), "", ""});
            whole.push_back(it.x.device.empty());
            if (std::find(keep.begin(), keep.end(), keyOf(it)) != keep.end()) again.push_back(static_cast<ui::RowIndex>(k));
        }
        model_->set(std::move(rows), std::move(keys), [whole](ui::RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (c == 0) s.monospace = true;
            if (c == 2 && r < whole.size()) {
                s.icon = whole[r] ? ui::Icon::Lock : ui::Icon::Screen;
                s.iconTone = ui::Tone::Error;
            }
            return s;
        });
        table_->selectModelRows(again, false);
        updateButtons();
        invalidate();
    }

    void updateButtons() {
        remove_->setEnabled(table_->selectedModelRows().size() == 1);   // 1.47.0 : une ligne (plusieurs : Exporter, Copier)
        export_->setEnabled(ws().hasRevocations());
        // 1.44.0 : revoquer (et retirer une revocation) : le droit << licence_revoquer >> ; le bouton dit pourquoi.
        if (!ws().can("licence_revoquer")) {
            revoke_->setEnabled(false);
            remove_->setEnabled(false);
            revoke_->setTooltip(ws().whyNot("licence_revoquer"));
            remove_->setTooltip(ws().whyNot("licence_revoquer"));
        } else {
            revoke_->setEnabled(true);
        }
        invalidate();
    }

    // Pourquoi Retirer / Revoquer ne se peut pas (vide : il se peut).
    [[nodiscard]] std::string revokeWhy() const {
        if (!ws().can("licence_revoquer")) return ws().whyNot("licence_revoquer");
        if (host_.busy && host_.busy()) return "un travail est en cours";
        return {};
    }

    void openLicence(const std::string& id) {
        requestPageFocus("licences", id);
        host_.navigate("licences");
    }
    void goTo(const std::string& page, const std::string& focus) {
        requestPageFocus(page, focus);
        host_.navigate(page);
    }

    // ---- 1.47.0 (planche 06) : le clic droit, dans l'ordre de la regle ----
    ui::ListMenu menuFor() {
        const auto rows = table_->selectedModelRows();
        ui::ListMenu m({rows.size(), "r\xC3\xA9vocations", true});
        if (rows.empty()) return m;
        const Item* it = itemAt(rows.front());
        if (rows.size() == 1 && it) {
            const std::string id = it->x.licenseId;
            if (it->inArchive) m.open("Ouvrir la licence", [this, id] { openLicence(id); });
        }
        m.unfold(*table_);
        if (rows.size() == 1 && it) {
            const Item x = *it;
            const std::string id = x.x.licenseId;
            if (x.inArchive) m.goTo({.label = "La licence " + id, .icon = ui::Icon::Document, .run = [this, id] { openLicence(id); }});
            if (!x.customer.empty())
                m.goTo({.label = "Le client : " + x.customer, .icon = ui::Icon::User, .run = [this, c = x.customer] { goTo("clients", "client:" + c); }});
            m.goTo({.label = "Ses PC (Activations)", .icon = ui::Icon::Station, .run = [this, id] { goTo("activations", "licence:" + id); }});
            if (x.by && !x.by->key.empty())
                m.goTo({.label = "R\xC3\xA9voqu\xC3\xA9" "e par " + x.by->who, .icon = ui::Icon::User, .run = [this, k = x.by->key] { goTo("admins", "admin:" + k); }});
            if (x.by && x.by->number && ws().role() == Role::Plus)   // le journal : l'admin+ seulement
                m.goTo({.label = "Sa ligne du journal (n\xC2\xB0 " + std::to_string(x.by->number) + ")", .icon = ui::Icon::Task,
                        .run = [this, n = x.by->number] { goTo("journal", "ligne:" + std::to_string(n)); }});
            m.goTo({.label = "Son historique", .icon = ui::Icon::History, .run = [this, id] { goTo("historique", "licence:" + id); }});
            const auto& t = texts_[shown_[rows.front()]];
            const auto on = [&](std::size_t f, const std::string& label) {
                if (f >= t.size() || t[f].empty()) return;
                const std::string v = t[f];
                m.filterOn({.label = label + " : " + v, .run = [this, f, v] { bar_->filterOn(f, v); }});
            };
            on(3, "Cette raison");
            on(1, "Ce client");
            on(0, "Cette licence");
            on(4, "R\xC3\xA9voqu\xC3\xA9" "e par");
            if (x.x.revokedAt > 0) {
                const std::int64_t when = listkit::seconds(x.x.revokedAt);
                m.filterOn({.label = "Ce jour : " + dateFr(x.x.revokedAt), .run = [this, when] { bar_->filterOnDay(5, when); }});
            }
            m.copy("La licence", id, ui::Icon::Document);
            if (!x.x.device.empty()) {
                m.copy("L'empreinte du PC", x.fingerprint, ui::Icon::Copy);
                m.copy("La cl\xC3\xA9 du PC", x.x.device, ui::Icon::Lock);
            }
        }
        std::string lines;
        for (const ui::RowIndex r : rows) {
            if (r >= shown_.size()) continue;
            const auto& t = texts_[shown_[r]];
            std::string line;
            for (std::size_t c = 0; c < t.size(); ++c) line += (c ? "\t" : "") + t[c];
            lines += line + "\n";
        }
        m.copy("La ligne", lines, ui::Icon::Copy, "Copier les %n lignes", lines);
        m.exportRows([this, rows] { listkit::exportRows(host_, *table_, rows, "revocations.csv", "r\xC3\xA9vocations"); });
        m.exportEntry({.label = "Exporter la liste sign\xC3\xA9" "e (.xpgrev)\xE2\x80\xA6", .many = "Exporter la liste sign\xC3\xA9" "e (.xpgrev)\xE2\x80\xA6", .icon = ui::Icon::Export,
                       .why = ws().hasRevocations() ? std::string() : std::string("aucune liste sign\xC3\xA9" "e"), .run = [this] { exportList(); }});
        if (rows.size() == 1 && it) {
            const std::size_t index = it->index;
            m.danger({.label = "Retirer la r\xC3\xA9vocation\xE2\x80\xA6", .icon = ui::Icon::Undo, .why = revokeWhy(), .run = [this, index] { askRemoveAt(index); }});
            const LicenceEntry* l = ws().licence(it->x.licenseId);
            if (l && !l->revoked && !it->x.device.empty()) {   // un PC revoque : la licence entiere reste a revoquer
                const std::string id = it->x.licenseId;
                m.danger({.label = "R\xC3\xA9voquer toute la licence\xE2\x80\xA6", .icon = ui::Icon::Close, .why = revokeWhy(),
                          .run = [this, id] { host_.openDialog(licdlg::makeRevokeDialog(host_, id)); }});
            }
        }
        return m;
    }

    // ---- 1.47.0 (planche 07) : la ligne depliee - la licence, ce qui est revoque et par qui, son histoire ----
    ui::WidgetPtr unfoldFor(ui::RowIndex r) {
        const Item* p = itemAt(r);
        if (!p) return nullptr;
        const Item it = *p;
        const Seconds now = ws().now();
        const std::string id = it.x.licenseId;
        auto card = std::make_unique<ui::UnfoldCard>("revocations.depliage");
        ui::UnfoldCard::Box lic;
        lic.title = "LA LICENCE";
        if (const LicenceEntry* e = ws().licence(id)) {
            const auto& l = e->licence;
            lic.lines.push_back({.icon = ui::Icon::Document, .text = "**" + id + "** \xC2\xB7 " + l.edition,
                                 .link = [this, id] { openLicence(id); }});
            lic.lines.push_back({.icon = ui::Icon::User, .text = l.customer.empty() ? std::string("(client inconnu)") : l.customer,
                                 .link = l.customer.empty() ? std::function<void()>() : std::function<void()>([this, c = l.customer] { goTo("clients", "client:" + c); })});
            lic.lines.push_back({.icon = ui::Icon::Station, .text = "PC : **" + std::to_string(l.devices.size()) + "** sur " + std::to_string(l.maxDevices)
                                                                     + (l.expiresAt ? " \xC2\xB7 fin le " + dateFr(l.expiresAt) : std::string(" \xC2\xB7 sans fin"))});
            lic.lines.push_back({.icon = ui::Icon::Info, .text = "\xC3\xA9tat : **" + e->stateLabel() + "**",
                                 .tone = e->revoked ? ui::Tone::Error : e->expired ? ui::Tone::Warning : ui::Tone::Ok});
        } else {
            lic.lines.push_back({.icon = ui::Icon::Document, .text = "**" + id + "**"});
            lic.lines.push_back({.icon = ui::Icon::Warning, .text = "pas dans l'archive de ce poste", .tone = ui::Tone::Warning});
        }
        card->addBox(std::move(lic));

        ui::UnfoldCard::Box what;
        what.title = "CE QUI EST R\xC3\x89VOQU\xC3\x89";
        if (it.x.device.empty())
            what.lines.push_back({.icon = ui::Icon::Lock, .text = "**toute la licence**", .tone = ui::Tone::Error});
        else
            what.lines.push_back({.icon = ui::Icon::Screen, .text = "le PC **" + (it.pcName.empty() ? it.fingerprint : it.pcName) + "**"
                                                                    + (it.pcName.empty() ? std::string() : " (" + it.fingerprint + ")"),
                                  .tone = ui::Tone::Error});
        what.lines.push_back({.icon = ui::Icon::Comment, .text = "raison : **" + (it.x.reason.empty() ? std::string("(aucune)") : it.x.reason) + "**"});
        what.lines.push_back({.icon = ui::Icon::Layers,
                              .text = it.x.issuedBefore ? "les \xC3\xA9missions sign\xC3\xA9" "es avant le " + dateTimeFr(it.x.issuedBefore) : std::string("toutes ses \xC3\xA9missions")});
        ui::UnfoldCard::Line when{.icon = ui::Icon::History, .text = "r\xC3\xA9voqu\xC3\xA9" "e " + kit::whenFr(now, it.x.revokedAt)};
        if (it.by) {
            when.text += ", par";
            when.person = listkit::person(host_, it.by->key, it.by->who);
        }
        what.lines.push_back(std::move(when));
        card->addBox(std::move(what));

        ui::UnfoldCard::Box story;
        story.title = "SON HISTOIRE";
        story.timeline = true;
        story.weight = 1.3f;
        const auto history = who_.history(id);
        const std::size_t from = history.size() > 4 ? history.size() - 4 : 0;
        for (std::size_t k = from; k < history.size(); ++k) {
            const auto& d = history[k];
            ui::UnfoldCard::Line line{.time = kit::whenFr(now, d.when), .text = d.summary};
            if (!d.key.empty()) line.person = listkit::person(host_, d.key, d.who);
            story.lines.push_back(std::move(line));
        }
        if (!story.lines.empty()) card->addBox(std::move(story));

        if (it.inArchive) card->addButton("Ouvrir la licence", "revocations.depliage.ouvrir", ui::Icon::GoTo, [this, id] { openLicence(id); });
        const std::size_t index = it.index;
        card->addButton("Retirer la r\xC3\xA9vocation\xE2\x80\xA6", "revocations.depliage.retirer", ui::Icon::Undo, [this, index] { askRemoveAt(index); }, revokeWhy());
        card->addText("clic droit : le menu", true);
        return card;
    }

    void askRemove() {
        const auto sel = table_->selectedModelRows();
        if (sel.size() != 1) return;
        if (const Item* it = itemAt(sel.front())) askRemoveAt(it->index);
    }

    void askRemoveAt(std::size_t index) {
        const auto& list = ws().revocations();
        if (index >= list.revoked.size()) return;
        if (!ws().can("licence_revoquer")) {
            host_.flash(ws().whyNot("licence_revoquer"), Tone::Warning);
            return;
        }
        if (host_.busy && host_.busy()) {
            host_.flash(kBusy, Tone::Warning);
            return;
        }
        const licensing::Revocation x = list.revoked[index];
        const std::uint64_t number = list.number;
        std::string what = "la licence " + x.licenseId;
        if (!x.device.empty()) {
            what = "un PC (" + licdlg::fingerprintOfKey(x.device) + ") de la licence " + x.licenseId;
            if (const LicenceEntry* l = ws().licence(x.licenseId))
                if (const auto* d = l->licence.device(x.device); d && !d->name.empty()) what = "le PC \xC2\xAB " + d->name + " \xC2\xBB de la licence " + x.licenseId;
        }
        auto alive = alive_;
        host_.openDialog(licdlg::makeConfirmDialog(
            "retirer-revocation", "Retirer la r\xC3\xA9vocation",
            "Retirer la r\xC3\xA9vocation de " + what + " (\xC2\xAB " + x.reason + " \xC2\xBB) ?\n\nUne nouvelle liste est sign\xC3\xA9" "e sans elle. Un client qui a "
            "d\xC3\xA9j\xC3\xA0 import\xC3\xA9 l'ancienne liste la garde jusqu'\xC3\xA0 ce qu'il re\xC3\xA7oive la nouvelle (sa prochaine licence, ou la liste export\xC3\xA9" "e).",
            "Retirer", false, [this, alive, index, number, x] {
                if (!*alive) return;
                const auto& now = ws().revocations();
                if (now.number != number || index >= now.revoked.size() || now.revoked[index].licenseId != x.licenseId || now.revoked[index].device != x.device) {
                    host_.flash("La liste a chang\xC3\xA9 entre-temps : rien n'a \xC3\xA9t\xC3\xA9 retir\xC3\xA9 ; choisis de nouveau.", Tone::Warning);
                    return;
                }
                if (host_.busy && host_.busy()) {
                    host_.flash(kBusy, Tone::Warning);
                    return;
                }
                const OpResult r = ws().unrevoke(index);
                host_.report(r);
                if (r.ok) (void)publishToService(host_);   // 1.35.0 : le service regle (la liste deposee, les PC lies prevenus)
            }));
    }

    void exportList() {
        if (!ws().hasRevocations()) return;
        if (host_.busy && host_.busy()) {
            host_.flash("Un travail est en cours : attends qu'il finisse pour exporter.", Tone::Warning);
            return;
        }
        ui::FilePick pick;
        pick.title = "Exporter la liste de r\xC3\xA9vocation";
        pick.filters = {{"Listes de r\xC3\xA9vocation XPG (.xpgrev)", "xpgrev"}};
        pick.save = true;
        pick.start = licdlg::startFor("revocations-" + std::to_string(ws().revocations().number) + ".xpgrev");
        auto alive = alive_;
        host_.pickFile(pick, [this, alive](const std::string& chosen) {
            if (!*alive) return;
            if (chosen.empty()) {
                host_.flash("Annul\xC3\xA9 : rien n'a \xC3\xA9t\xC3\xA9 export\xC3\xA9.", Tone::Info);
                return;
            }
            std::string path = chosen;
            if (extOf(path) != ".xpgrev") path += ".xpgrev";
            const OpResult r = ws().exportRevocations(path);
            if (r.ok) licdlg::rememberFolder(path);
            host_.report(r);
        });
    }

    ui::Button*                           revoke_{nullptr};
    ui::Button*                           remove_{nullptr};
    ui::Button*                           export_{nullptr};
    ui::TableView*                        table_{nullptr};
    ui::FilterBar*                        bar_{nullptr};            // 1.47.0
    std::shared_ptr<Rows>                 model_;
    std::vector<Item>                     all_;                     // 1.47.0 : toutes les revocations (la barre les lit)
    std::vector<std::vector<std::string>> texts_;                   // ... leurs cases
    std::vector<std::size_t>              shown_;                   // ... celles que la barre garde (ligne du modele -> all_)
    listkit::WhoDid                       who_{host_.workspace()};  // 1.47.0 : qui l'a revoquee
    std::size_t                           explainLines_{1};
    float                                 statusY_{0.f}, actionsY_{0.f};
    std::shared_ptr<bool>                 alive_ = std::make_shared<bool>(true);
    core::ConnectionScope                 links_;
};

} // namespace

PagePtr makeRevocationsPage(ShellHost host) { return std::make_unique<RevocationsPage>(std::move(host)); }

} // namespace license_admin::gui
