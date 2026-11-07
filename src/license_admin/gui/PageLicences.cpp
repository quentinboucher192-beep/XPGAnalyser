// =============================================================================
//  license_admin/gui/PageLicences.cpp - 1.33.0 : LA PAGE << LICENCES >>
// -----------------------------------------------------------------------------
//  Chaque licence de l'archive (sa derniere emission signee) : une recherche
//  (identifiant, client, edition), des filtres (toutes, actives, expirees,
//  revoquees), le tableau, et le detail de celle choisie : tous ses champs, ses
//  fonctionnalites, ses PC (nom, empreinte, date d'activation ; les PC revoques
//  marques). Les gestes :
//    Nouvelle licence...          l'assistant (LicenceWizard.cpp) ;
//    Ajouter un PC...             l'assistant sur la licence choisie ;
//    Exporter le fichier...       le .xpglic a renvoyer au client ;
//    Revoquer...                  un dialogue : toute la licence ou un PC ;
//    Ouvrir le dossier de l'archive.
//  Des demandes (.xpgreq) glissees sur la page vont a l'assistant (ouvert, ou
//  ouvert pour elles) ; un .xpglic n'est pas pour elle (la coquille le range).
// =============================================================================
#include "Page.hpp"

#include "../GitHubSync.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace licdlg {
// Definis dans LicenceWizard.cpp.
menu::MenuPtr makeRevokeDialog(ShellHost host, std::string licenseId);
bool wizardTakeDrop(const std::vector<std::string>& paths);
std::string fingerprintOfKey(const std::string& hex);
std::string startFor(const std::string& fileName);
void rememberFolder(const std::string& filePath);
} // namespace licdlg

namespace {

namespace fs = std::filesystem;
using licensing::LicenseType;
using PG = ui::PropertyGrid;

constexpr float kPad = 16.f;
constexpr Seconds kDaySeconds = 86400;

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string extOf(const std::string& path) { return lower(U(P(path).extension())); }

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

std::string typeLabel(LicenseType t) {
    switch (t) {
        case LicenseType::Perpetual: return "perp\xC3\xA9tuelle";
        case LicenseType::Temporary: return "temporaire";
        case LicenseType::Evaluation: return "\xC3\xA9valuation";
        case LicenseType::Floating: return "flottante";
    }
    return {};
}

class Rows final : public ui::ITableModel {
public:
    using Style = std::function<ui::CellStyle(ui::RowIndex, std::size_t)>;
    Rows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, Style style = {}, std::vector<std::string> tips = {})
        : headers_(std::move(headers)), rows_(std::move(rows)), style_(std::move(style)), tips_(std::move(tips)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string(); }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string();
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override { return style_ ? style_(r, c) : ui::CellStyle{}; }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override { return r < tips_.size() ? tips_[r] : std::string(); }

private:
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_;
    Style                                 style_;
    std::vector<std::string>              tips_;
};

PG::Property prop(std::string name, std::string value, std::string description = {}) {
    PG::Property p;
    p.name = std::move(name);
    p.value = std::move(value);
    p.type = PG::ValueType::ReadOnly;
    p.description = std::move(description);
    return p;
}

template <class T>
T& adopt(ui::Widget& parent, std::unique_ptr<T> w) {
    return static_cast<T&>(parent.addChild(std::move(w)));
}

ui::Button& button(ui::Widget& parent, std::string text, std::string id, ui::Button::Style style = ui::Button::Style::Default) {
    auto& b = adopt(parent, std::make_unique<ui::Button>(std::move(text), std::move(id)));
    b.setStyle(style);
    return b;
}

float buttonWidth(ui::Widget& b, float minimum) { return std::max(minimum, b.sizeHint().preferred.w); }

enum class Filter : int { All = 0, Active = 1, Expired = 2, Revoked = 3 };

bool inFilter(const LicenceEntry& l, Filter f) {
    switch (f) {
        case Filter::All: return true;
        case Filter::Active: return !l.revoked && !l.expired;
        case Filter::Expired: return l.expired && !l.revoked;
        case Filter::Revoked: return l.revoked;
    }
    return true;
}

// La revocation qui touche ce PC de cette emission (nullptr : aucune).
const licensing::Revocation* deviceRevocation(const Workspace& ws, const licensing::License& l, const std::string& key) {
    for (const auto& r : ws.revocations().revoked)
        if (r.licenseId == l.licenseId && r.device == key && (r.issuedBefore == 0 || l.issuedAt < r.issuedBefore)) return &r;
    return nullptr;
}

class LicencesPage final : public Page {
public:
    explicit LicencesPage(ShellHost host) : Page("licences", std::move(host)) {
        new_ = &button(*this, "Nouvelle licence\xE2\x80\xA6", "licences.nouvelle", ui::Button::Style::Primary);
        new_->setTooltip("L'assistant : les demandes des PC, le client, l'\xC3\xA9" "dition, les fonctionnalit\xC3\xA9s, puis signer");
        addPc_ = &button(*this, "Ajouter un PC\xE2\x80\xA6", "licences.ajouter-pc");
        addPc_->setTooltip("Lier de nouveaux PC \xC3\xA0 la licence choisie (ses PC li\xC3\xA9s sont gard\xC3\xA9s) ; la r\xC3\xA9-\xC3\xA9mettre");
        export_ = &button(*this, "Exporter le fichier\xE2\x80\xA6", "licences.exporter");
        export_->setTooltip("Le .xpglic de la licence choisie, \xC3\xA0 renvoyer au client");
        revoke_ = &button(*this, "R\xC3\xA9voquer\xE2\x80\xA6", "licences.revoquer", ui::Button::Style::Danger);
        revoke_->setTooltip("Toute la licence choisie, ou un de ses PC");
        folder_ = &button(*this, "Ouvrir le dossier de l'archive", "licences.dossier");
        search_ = &adopt(*this, std::make_unique<ui::InputText>("licences.recherche"));
        search_->setPlaceholder("Chercher : identifiant, client, \xC3\xA9" "dition");
        search_->setEscapeClears(true);
        const char* chipIds[4] = {"licences.filtre.toutes", "licences.filtre.actives", "licences.filtre.expirees", "licences.filtre.revoquees"};
        for (int i = 0; i < 4; ++i) {
            chips_[i] = &button(*this, "", chipIds[i]);
            links_ += chips_[i]->clicked->connect([this, i] { setFilter(static_cast<Filter>(i)); });
        }

        auto split = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, "licences.separation");
        auto table = std::make_unique<ui::TableView>("licences.table");
        table->setSelectionMode(ui::SelectionMode::Single);
        table->setAlternatingRowColors(true);
        table->setColumns({{"ID", 140.f}, {"Client", 190.f}, {"\xC3\x89" "dition", 130.f}, {"Type", 120.f}, {"PC", 70.f}, {"\xC3\x89mise le", 105.f},
                           {"Fin", 105.f}, {"\xC3\x89tat", 220.f}});
        table_ = table.get();
        auto bottom = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, "licences.detail");
        auto details = std::make_unique<ui::PropertyGrid>("licences.details");
        details->setShowDescriptionPane(false);
        details_ = details.get();
        auto pcs = std::make_unique<ui::TableView>("licences.pcs");
        pcs->setSelectionMode(ui::SelectionMode::Single);
        pcs->setColumns({{"PC", 160.f}, {"\xC3\x89tat", 200.f}, {"Empreinte", 250.f}, {"Activ\xC3\xA9 le", 105.f}});
        pcs_ = pcs.get();
        bottom->addPane(std::move(details), 0.45f, 220.f);
        bottom->addPane(std::move(pcs), 0.55f, 200.f);
        split->addPane(std::move(table), 0.55f, 110.f);
        split->addPane(std::move(bottom), 0.45f, 140.f);
        split_ = &adopt(*this, std::move(split));

        links_ += new_->clicked->connect([this] { host_.openDialog(makeLicenceWizard(host_, {})); });
        links_ += addPc_->clicked->connect([this] {
            if (const LicenceEntry* l = selected()) host_.openDialog(makeLicenceWizard(host_, {}, l->licence.licenseId));
        });
        links_ += export_->clicked->connect([this] { exportSelected(); });
        links_ += revoke_->clicked->connect([this] {
            if (const LicenceEntry* l = selected()) host_.openDialog(licdlg::makeRevokeDialog(host_, l->licence.licenseId));
        });
        links_ += folder_->clicked->connect([this] { openArchive(); });
        links_ += search_->textChanged->connect([this](const std::string&) { rebuild(); });
        links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& sel) {
            if (syncing_) return;
            selectedId_ = !sel.empty() && sel.front() < shownIds_.size() ? shownIds_[sel.front()] : std::string();
            showDetails();
        });
        refresh();
    }
    ~LicencesPage() override { *alive_ = false; }

    [[nodiscard]] std::string title() const override { return "Licences"; }

    void refresh() override {
        // Les compteurs des filtres.
        int counts[4] = {0, 0, 0, 0};
        for (const auto& l : ws().licences())
            for (int f = 0; f < 4; ++f)
                if (inFilter(l, static_cast<Filter>(f))) ++counts[f];
        const char* names[4] = {"Toutes", "Actives", "Expir\xC3\xA9" "es", "R\xC3\xA9voqu\xC3\xA9" "es"};
        for (int f = 0; f < 4; ++f) {
            chips_[f]->setText(std::string(names[f]) + " (" + std::to_string(counts[f]) + ")");
            chips_[f]->setStyle(static_cast<int>(filter_) == f ? ui::Button::Style::Primary : ui::Button::Style::Default);
        }
        invalidateLayout();
        rebuild();
    }

    bool dropFiles(const std::vector<std::string>& paths) override {
        if (paths.empty()) return false;
        for (const auto& p : paths)
            if (extOf(p) != ".xpgreq") return false;   // un .xpglic, un .xpgreg... : la coquille les range
        if (licdlg::wizardTakeDrop(paths)) return true;   // l'assistant ouvert les prend
        host_.openDialog(makeLicenceWizard(host_, paths));
        return true;
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float x = r.x + kPad, w = std::max(200.f, r.w - 2.f * kPad);
        actionsY_ = r.y + kPad + 64.f;
        float bx = x;
        for (ui::Button* b : {new_, addPc_, export_, revoke_, folder_}) {
            const float bw = buttonWidth(*b, 96.f);
            b->setBounds({bx, actionsY_, bw, 32.f});
            bx += bw + 8.f;
        }
        filterY_ = actionsY_ + 44.f;
        const float sw = std::min(360.f, std::max(180.f, w * 0.35f));
        search_->setBounds({x, filterY_, sw, 30.f});
        float cx = x + sw + 12.f;
        for (auto* c : chips_) {
            const float cw = buttonWidth(*c, 70.f);
            c->setBounds({cx, filterY_, cw, 30.f});
            cx += cw + 6.f;
        }
        const float top = filterY_ + 42.f;
        split_->setBounds({x, top, w, std::max(160.f, r.bottom() - kPad - top)});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        r.fillRect(b, c.panelBg);
        const float x = b.x + kPad, w = b.w - 2.f * kPad;
        r.drawText({x, b.y + kPad}, title(), ctx.theme.font.title, c.text);
        r.drawText({x, b.y + kPad + 36.f},
                   fit(r, "Chaque licence sign\xC3\xA9" "e, rang\xC3\xA9" "e dans l'archive. Choisis-en une pour voir ses PC ; Nouvelle licence\xE2\x80\xA6 pour en signer une.",
                       ctx.theme.font.ui, w),
                   ctx.theme.font.ui, c.textMuted);
        const auto& list = ws().licences();
        const std::string count = std::to_string(shownIds_.size()) + (shownIds_.size() == list.size() ? "" : " sur " + std::to_string(list.size()))
                                  + (list.size() > 1 ? " licences" : " licence");
        const float cw = r.measure(count, ctx.theme.font.ui).width;
        const float after = chips_[3]->bounds().right() + 12.f;
        if (b.right() - kPad - cw > after)
            r.drawText({b.right() - kPad - cw, filterY_ + (30.f - r.lineHeight(ctx.theme.font.ui)) * 0.5f}, count, ctx.theme.font.ui, c.textMuted);
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const gfx::FontId f = ctx.theme.font.ui;
        if (shownIds_.empty()) {
            const auto t = table_->bounds();
            const std::string text = ws().licences().empty()
                                         ? std::string("Aucune licence dans l'archive : Nouvelle licence\xE2\x80\xA6 pour signer la premi\xC3\xA8re (ou glisse ici les demandes .xpgreq d'un client).")
                                         : std::string("Aucune licence ne correspond \xC3\xA0 la recherche ou au filtre.");
            r.drawText({t.x + 16.f, t.y + 44.f}, fit(r, text, f, t.w - 32.f), f, c.textMuted);
        }
        if (!selected()) {
            const auto d = details_->bounds();
            r.drawText({d.x + 16.f, d.y + 16.f}, fit(r, "Choisis une licence pour voir son d\xC3\xA9tail.", f, d.w - 32.f), f, c.textMuted);
        }
    }

private:
    [[nodiscard]] const LicenceEntry* selected() const { return selectedId_.empty() ? nullptr : ws().licence(selectedId_); }

    void setFilter(Filter f) {
        filter_ = f;
        for (int k = 0; k < 4; ++k) chips_[k]->setStyle(static_cast<int>(filter_) == k ? ui::Button::Style::Primary : ui::Button::Style::Default);
        rebuild();
    }

    void rebuild() {
        const std::string q = lower(search_->text());
        shownIds_.clear();
        std::vector<std::vector<std::string>> rows;
        std::vector<int> tones;   // 0 active, 1 a surveiller, 2 expiree, 3 revoquee
        const auto& reg = ws().registry();
        const Seconds now = ws().now();
        for (const auto& e : ws().licences()) {
            if (!inFilter(e, filter_)) continue;
            const auto& l = e.licence;
            const auto* ed = reg.edition(l.edition);
            const std::string edLabel = ed && !ed->label.empty() ? ed->label : l.edition;
            if (!q.empty()) {
                const std::string hay = lower(l.licenseId + "\n" + l.customer + "\n" + l.edition + "\n" + edLabel);
                if (hay.find(q) == std::string::npos) continue;
            }
            std::string state = e.stateLabel();
            int tone = 0;
            if (e.revoked) tone = 3;
            else if (e.expired) tone = 2;
            else if (e.revokedDevices > 0) tone = 1;
            if (!e.revoked && !e.expired && l.expiresAt != 0 && l.expiresAt - now <= 30 * kDaySeconds) {
                state += " \xE2\x80\x94 finit dans " + std::to_string(std::max<Seconds>(0, (l.expiresAt - now) / kDaySeconds)) + " j";
                tone = std::max(tone, 1);
            }
            shownIds_.push_back(l.licenseId);
            rows.push_back({l.licenseId, l.customer, edLabel, typeLabel(l.type), std::to_string(l.devices.size()) + " / " + std::to_string(l.maxDevices),
                            dateFr(l.issuedAt),
                            l.expiresAt ? dateFr(l.expiresAt) : std::string("sans fin"), state});
            tones.push_back(tone);
        }
        model_ = std::make_shared<Rows>(
            std::vector<std::string>{"ID", "Client", "\xC3\x89" "dition", "Type", "PC", "\xC3\x89mise le", "Fin", "\xC3\x89tat"}, std::move(rows),
            [tones](ui::RowIndex r, std::size_t c) {
                ui::CellStyle s;
                if (c == 0) s.monospace = true;
                if (c == 7 && r < tones.size()) {
                    static const ui::Tone map[4] = {ui::Tone::Ok, ui::Tone::Warning, ui::Tone::Muted, ui::Tone::Error};
                    static const ui::Icon icons[4] = {ui::Icon::Ok, ui::Icon::Warning, ui::Icon::History, ui::Icon::Error};
                    s.fgTone = map[tones[r]];
                    s.icon = icons[tones[r]];
                    s.iconTone = map[tones[r]];
                }
                return s;
            });
        syncing_ = true;
        table_->setModel(model_);
        const auto it = std::find(shownIds_.begin(), shownIds_.end(), selectedId_);
        if (it != shownIds_.end()) table_->selectModelRows({static_cast<ui::RowIndex>(it - shownIds_.begin())}, false);
        else selectedId_.clear();
        syncing_ = false;
        showDetails();
        invalidate();
    }

    void showDetails() {
        const LicenceEntry* e = selected();
        addPc_->setEnabled(e != nullptr);
        export_->setEnabled(e != nullptr);
        revoke_->setEnabled(e != nullptr);
        std::vector<PG::Category> cats;
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> tips;
        std::vector<bool> revoked;
        if (e) {
            const auto& l = e->licence;
            const auto& reg = ws().registry();
            const auto* ed = reg.edition(l.edition);
            PG::Category a;
            a.name = "Licence " + l.licenseId;
            a.properties.push_back(prop("Identifiant", l.licenseId));
            a.properties.push_back(prop("Client", l.customer));
            a.properties.push_back(prop("\xC3\x89" "dition", ed && !ed->label.empty() && ed->label != l.edition ? ed->label + " (" + l.edition + ")" : l.edition));
            a.properties.push_back(prop("Type", typeLabel(l.type)));
            a.properties.push_back(prop("PC", std::to_string(l.devices.size()) + " li\xC3\xA9(s) sur " + std::to_string(l.maxDevices) + " autoris\xC3\xA9(s)"
                                                  + (e->revokedDevices ? ", " + std::to_string(e->revokedDevices) + " r\xC3\xA9voqu\xC3\xA9(s)" : std::string())));
            a.properties.push_back(prop("\xC3\x89mise le", dateTimeFr(l.issuedAt), "La date de signature de cette \xC3\xA9mission (la derni\xC3\xA8re rang\xC3\xA9" "e dans l'archive)."));
            a.properties.push_back(prop("Fin", l.expiresAt ? dateTimeFr(l.expiresAt) : std::string("sans fin")));
            a.properties.push_back(prop("Gr\xC3\xA2" "ce hors ligne", l.type == LicenseType::Temporary ? std::to_string(l.offlineGraceDays) + " j" : std::string("sans objet"),
                                        "Apr\xC3\xA8s la fin, les fonctionnalit\xC3\xA9s restent ouvertes ces jours-l\xC3\xA0, le temps d'importer le renouvellement."));
            a.properties.push_back(prop("Registre", "n\xC2\xB0 " + std::to_string(l.manifest), "Le registre en vigueur \xC3\xA0 la signature (joint au fichier)."));
            a.properties.push_back(prop("\xC3\x89tat", e->stateLabel()));
            a.properties.push_back(prop("Fichier", e->path, "La copie rang\xC3\xA9" "e dans l'archive (Exporter le fichier\xE2\x80\xA6 pour l'envoyer)."));
            cats.push_back(std::move(a));

            PG::Category f;
            f.name = "Fonctionnalit\xC3\xA9s (" + std::to_string(l.features.size()) + ")";
            std::map<std::string, std::string> byProduct;
            std::vector<std::string> order;
            for (const auto& g : l.features) {
                const auto* def = reg.feature(g.id);
                const std::string product = def && !def->product.empty() ? def->product : std::string("Autres");
                std::string label = def && !def->label.empty() ? def->label : g.id;
                for (const auto& [lid, v] : g.limits) label += " (" + lid + " " + std::to_string(v) + ")";
                if (!byProduct.count(product)) order.push_back(product);
                auto& line = byProduct[product];
                line += (line.empty() ? "" : ", ") + label;
            }
            for (const auto& p : order) f.properties.push_back(prop(p, byProduct[p]));
            if (l.features.empty()) f.properties.push_back(prop("Aucune", "cette licence n'ouvre aucune fonctionnalit\xC3\xA9"));
            cats.push_back(std::move(f));

            PG::Category rv;
            for (const auto& x : ws().revocations().revoked) {
                if (x.licenseId != l.licenseId) continue;
                std::string who = "toute la licence";
                if (!x.device.empty()) {
                    who = "le PC " + licdlg::fingerprintOfKey(x.device);
                    if (const auto* d = l.device(x.device); d && !d->name.empty()) who = "le PC \xC2\xAB " + d->name + " \xC2\xBB";
                }
                rv.properties.push_back(prop(dateFr(x.revokedAt), who + " : " + x.reason + (x.issuedBefore ? " (\xC3\xA9missions d'avant le " + dateFr(x.issuedBefore) + ")" : "")));
            }
            if (!rv.properties.empty()) {
                rv.name = "R\xC3\xA9vocations";
                cats.push_back(std::move(rv));
            }

            for (const auto& d : l.devices) {
                const licensing::Revocation* x = deviceRevocation(ws(), l, d.key);
                const bool all = e->revoked;
                rows.push_back({d.name.empty() ? std::string("(sans nom)") : d.name,
                                x ? "r\xC3\xA9voqu\xC3\xA9 le " + dateFr(x->revokedAt) : (all ? std::string("r\xC3\xA9voqu\xC3\xA9 (la licence)") : std::string("li\xC3\xA9")),
                                licdlg::fingerprintOfKey(d.key), dateFr(d.activatedAt)});
                tips.push_back(x ? "R\xC3\xA9voqu\xC3\xA9 : " + x->reason : std::string());
                revoked.push_back(x || all);
            }
        }
        details_->setCategories(std::move(cats));
        pcsModel_ = std::make_shared<Rows>(std::vector<std::string>{"PC", "\xC3\x89tat", "Empreinte", "Activ\xC3\xA9 le"}, std::move(rows),
                                           [revoked](ui::RowIndex r, std::size_t c) {
                                               ui::CellStyle s;
                                               if (c == 2) s.monospace = true;
                                               if (r < revoked.size() && c == 1) {
                                                   s.fgTone = revoked[r] ? ui::Tone::Error : ui::Tone::Ok;
                                                   s.icon = revoked[r] ? ui::Icon::Error : ui::Icon::Ok;
                                                   s.iconTone = s.fgTone;
                                               }
                                               if (r < revoked.size() && revoked[r] && c == 0) s.fgTone = ui::Tone::Error;
                                               return s;
                                           },
                                           std::move(tips));
        pcs_->setModel(pcsModel_);
        invalidate();
    }

    void exportSelected() {
        const LicenceEntry* e = selected();
        if (!e) return;
        if (host_.busy && host_.busy()) {
            host_.flash("Un travail est en cours : attends qu'il finisse pour exporter.", Tone::Warning);
            return;
        }
        const std::string id = e->licence.licenseId;
        ui::FilePick pick;
        pick.title = "Exporter le fichier de licence " + id;
        pick.filters = {{"Licences XPG (.xpglic)", "xpglic"}};
        pick.save = true;
        pick.start = licdlg::startFor(id + ".xpglic");
        auto alive = alive_;
        host_.pickFile(pick, [this, alive, id](const std::string& chosen) {
            if (!*alive) return;
            if (chosen.empty()) {
                host_.flash("Annul\xC3\xA9 : rien n'a \xC3\xA9t\xC3\xA9 export\xC3\xA9.", Tone::Info);
                return;
            }
            std::string path = chosen;
            if (extOf(path) != ".xpglic") path += ".xpglic";
            const OpResult r = ws().exportLicence(id, path);
            if (r.ok) licdlg::rememberFolder(path);
            host_.report(r);
        });
    }

    void openArchive() {
        const std::string archive = github::archiveFolder(ws().folder());
        const fs::path licences = P(archive) / "licences";
        std::error_code ec;
        host_.openFolder(fs::is_directory(licences, ec) ? U(licences) : archive);
    }

    ui::Button*                 new_{nullptr};
    ui::Button*                 addPc_{nullptr};
    ui::Button*                 export_{nullptr};
    ui::Button*                 revoke_{nullptr};
    ui::Button*                 folder_{nullptr};
    ui::InputText*              search_{nullptr};
    ui::Button*                 chips_[4]{};
    ui::Splitter*               split_{nullptr};
    ui::TableView*              table_{nullptr};
    ui::PropertyGrid*           details_{nullptr};
    ui::TableView*              pcs_{nullptr};
    std::shared_ptr<Rows>       model_, pcsModel_;
    std::vector<std::string>    shownIds_;
    std::string                 selectedId_;
    Filter                      filter_{Filter::All};
    bool                        syncing_{false};
    float                       actionsY_{0.f}, filterY_{0.f};
    std::shared_ptr<bool>       alive_ = std::make_shared<bool>(true);
    core::ConnectionScope       links_;
};

} // namespace

PagePtr makeLicencesPage(ShellHost host) { return std::make_unique<LicencesPage>(std::move(host)); }

} // namespace license_admin::gui
