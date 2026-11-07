// =============================================================================
//  license_admin/gui/PageClients.cpp - 1.33.0 : LA PAGE Clients
// -----------------------------------------------------------------------------
//  Pas encore de fichier clients a part : un client, c'est le nom ecrit dans ses
//  licences signees (et les demandes qui nomment une de ses licences) -
//  Workspace::clients. La page les liste (licences, actives, PC, demandes en
//  attente, derniere activite) ; le client choisi : ses licences et ses PC.
//  << Nouvelle licence pour ce client... >> ouvre l'assistant de licence, le
//  client prerempli ; << Voir ses licences >> va a la page Licences.
// =============================================================================
#include "Page.hpp"

#include "../../ui/TextSearch.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace {

using RowIndex = ui::RowIndex;

const ui::Fonts kFonts{};
constexpr float kPad = 16.f;
constexpr float kButtonH = 32.f;
constexpr float kFieldH = 28.f;
constexpr float kGap = 8.f;
constexpr float kLineH = 20.f;

// ---------------------------------------------------------------------------------------------- les aides
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

std::string count(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

std::string padKey(long long v) {
    char b[32];
    std::snprintf(b, sizeof b, "%012lld", v);
    return b;
}

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string deviceState(const std::string& s) {
    if (s == "revoque") return "r\xC3\xA9voqu\xC3\xA9" "e";
    if (s == "expire") return "expir\xC3\xA9" "e";
    return s;   // "demande", "active"
}

// =================================================================================== la page
class ClientsPage final : public Page {
public:
    explicit ClientsPage(ShellHost host) : Page("page.clients", std::move(host)) {
        search_ = add(*this, std::make_unique<ui::InputText>("clients.recherche"));
        search_->setPlaceholder("Chercher un client\xE2\x80\xA6");
        search_->setEscapeClears(true);
        newLicence_ = add(*this, std::make_unique<ui::Button>("Nouvelle licence pour ce client\xE2\x80\xA6", "clients.nouvelle-licence"));
        newLicence_->setStyle(ui::Button::Style::Primary);
        newLicence_->setIcon(ui::Icon::Plus);
        seeLicences_ = add(*this, std::make_unique<ui::Button>("Voir ses licences", "clients.voir-licences"));
        seeLicences_->setIcon(ui::Icon::GoTo);
        table_ = add(*this, std::make_unique<ui::TableView>("clients.table"));
        table_->setColumns({{"Client", 260.f}, {"Licences", 92.f}, {"Actives", 84.f}, {"PC", 56.f}, {"Demandes en attente", 176.f}, {"Derni\xC3\xA8re activit\xC3\xA9", 156.f}});
        table_->setSelectionMode(ui::SelectionMode::Single);
        model_ = std::make_shared<Rows>(std::vector<std::string>{"Client", "Licences", "Actives", "PC", "Demandes en attente", "Derni\xC3\xA8re activit\xC3\xA9"});
        table_->setModel(model_);
        licences_ = add(*this, std::make_unique<ui::TableView>("clients.licences"));
        licences_->setColumns({{"Licence", 140.f}, {"\xC3\x89" "dition", 120.f}, {"\xC3\x89tat", 150.f}, {"Fin", 106.f}});
        licences_->setSelectionMode(ui::SelectionMode::Single);
        licencesModel_ = std::make_shared<Rows>(std::vector<std::string>{"Licence", "\xC3\x89" "dition", "\xC3\x89tat", "Fin"});
        licences_->setModel(licencesModel_);
        devices_ = add(*this, std::make_unique<ui::TableView>("clients.pc"));
        devices_->setColumns({{"PC", 150.f}, {"Licence", 140.f}, {"\xC3\x89tat", 90.f}, {"Empreinte", 200.f}});
        devices_->setSelectionMode(ui::SelectionMode::Single);
        devicesModel_ = std::make_shared<Rows>(std::vector<std::string>{"PC", "Licence", "\xC3\x89tat", "Empreinte"});
        devices_->setModel(devicesModel_);

        links_ += search_->textChanged->connect([this](const std::string&) { rebuild(); });
        links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>& rows) {
            if (loading_ || rows.empty() || rows.front() >= shown_.size()) return;
            selected_ = shown_[rows.front()].name;
            rebuildDetail();
        });
        links_ += newLicence_->clicked->connect([this] {
            const ClientEntry* c = current();
            if (!c || !host_.openDialog) return;
            host_.openDialog(makeLicenceWizard(host_, {}, {}, c->name));
        });
        links_ += seeLicences_->clicked->connect([this] {
            if (current() && host_.navigate) host_.navigate("licences");
        });
        refresh();
    }

    [[nodiscard]] std::string title() const override { return "Clients"; }

    void refresh() override {
        all_ = ws().clients();
        devicesAll_ = ws().devices();
        rebuild();
        invalidateLayout();
        invalidate();
    }

    // Les essais : le client choisi ("" : aucun).
    [[nodiscard]] const std::string& selected() const noexcept { return selected_; }

protected:
    void onLayout() override {
        const gfx::Rect b = bounds();
        const float x = b.x + kPad, w = std::max(200.f, b.w - 2.f * kPad);
        float y = b.y + kPad;
        titleY_ = y;
        y += 34.f;
        leadLines_ = wrap(kLead, kFonts.ui, w, 3);
        leadY_ = y;
        y += static_cast<float>(leadLines_.size()) * kLineH + 10.f;
        search_->setBounds({x, y + 2.f, std::min(320.f, w * 0.35f), kFieldH});
        const float sw = seeLicences_->sizeHint().preferred.w, nw = newLicence_->sizeHint().preferred.w;
        seeLicences_->setBounds({x + w - sw, y, sw, kButtonH});
        newLicence_->setBounds({x + w - sw - kGap - nw, y, nw, kButtonH});
        countRect_ = {x + std::min(320.f, w * 0.35f) + 12.f, y, std::max(0.f, w - std::min(320.f, w * 0.35f) - sw - nw - kGap - 24.f), kButtonH};
        y += kButtonH + kGap;
        const gfx::Rect area{x, y, w, std::max(60.f, b.bottom() - kPad - y)};
        // En haut les clients ; en bas le client choisi : ses licences et ses PC, cote a cote.
        const float th = std::max(120.f, std::floor(area.h * 0.42f));
        table_->setBounds({area.x, area.y, area.w, th});
        panel_ = {area.x, area.y + th + 12.f, area.w, std::max(120.f, area.bottom() - area.y - th - 12.f)};
        float py = panel_.y + 32.f + 8.f;
        summaryY_ = py;
        py += kLineH + 6.f;
        licencesHeadY_ = devicesHeadY_ = py;
        py += 22.f;
        const float half = std::floor((panel_.w - 36.f) * 0.5f);
        const float th2 = std::max(48.f, panel_.bottom() - 10.f - py);
        licences_->setBounds({panel_.x + 12.f, py, half, th2});
        devices_->setBounds({panel_.x + 24.f + half, py, panel_.w - 36.f - half, th2});
        devicesX_ = panel_.x + 24.f + half;
        emptyY_ = area.y + 40.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const gfx::Rect b = bounds();
        r.fillRect(b, c.panelBg);
        const float x = b.x + kPad, w = std::max(200.f, b.w - 2.f * kPad);
        r.drawText({x, titleY_}, "Clients", ctx.theme.font.title, c.text);
        for (std::size_t i = 0; i < leadLines_.size(); ++i)
            r.drawText({x, leadY_ + static_cast<float>(i) * kLineH}, fit(r, leadLines_[i], ctx.theme.font.ui, w), ctx.theme.font.ui, c.textMuted);
        std::string n = count(shown_.size(), "client", "clients");
        if (shown_.size() != all_.size()) n += " (sur " + std::to_string(all_.size()) + ")";
        r.drawText({countRect_.x, countRect_.y + (kButtonH - r.lineHeight(ctx.theme.font.ui)) * 0.5f}, fit(r, n, ctx.theme.font.ui, countRect_.w), ctx.theme.font.ui,
                   c.textMuted);
        // Le panneau du client choisi.
        r.fillRect(panel_, c.windowBg);
        r.strokeRect(panel_, c.border, 1.f);
        r.fillRect({panel_.x, panel_.y, panel_.w, 32.f}, c.headerBg);
        const ClientEntry* cl = current();
        const std::string head = cl ? cl->name : std::string("Aucun client choisi");
        r.drawText({panel_.x + 12.f, panel_.y + (32.f - r.lineHeight(ctx.theme.font.heading)) * 0.5f}, fit(r, head, ctx.theme.font.heading, panel_.w - 24.f),
                   ctx.theme.font.heading, cl ? c.text : c.textMuted);
        if (!cl) return;
        std::string summary = count(cl->licenceIds.size(), "licence", "licences") + " (" + count(static_cast<std::size_t>(cl->activeLicences), "active", "actives")
                              + "), " + count(static_cast<std::size_t>(cl->devices), "PC li\xC3\xA9", "PC li\xC3\xA9s");
        if (cl->pendingRequests > 0) summary += ", " + count(static_cast<std::size_t>(cl->pendingRequests), "demande en attente", "demandes en attente");
        if (cl->lastActivity) summary += " - derni\xC3\xA8re activit\xC3\xA9 le " + dateFr(cl->lastActivity);
        r.drawText({panel_.x + 12.f, summaryY_}, fit(r, summary, ctx.theme.font.ui, panel_.w - 24.f), ctx.theme.font.ui, c.text);
        r.drawText({panel_.x + 12.f, licencesHeadY_}, "Ses licences", ctx.theme.font.ui, c.textMuted);
        r.drawText({devicesX_, devicesHeadY_}, "Ses PC (le registre des activations)", ctx.theme.font.ui, c.textMuted);
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (!shown_.empty()) return;
        auto& r = ctx.r;
        const gfx::Rect t = table_->bounds();
        const std::string line = all_.empty() ? "Pas encore de client : un client appara\xC3\xAEt avec sa premi\xC3\xA8re licence sign\xC3\xA9" "e."
                                              : "Aucun client ne correspond \xC3\xA0 la recherche.";
        r.drawText({t.x + 16.f, emptyY_}, fit(r, line, ctx.theme.font.ui, t.w - 32.f), ctx.theme.font.ui, ctx.theme.color.textMuted);
    }

private:
    static constexpr const char* kLead =
        "Les clients sont tir\xC3\xA9s des licences sign\xC3\xA9" "es et des demandes re\xC3\xA7ues : il n'y a pas encore de fichier clients \xC3\xA0 part. "
        "Un client, c'est le nom \xC3\xA9" "crit dans ses licences (sans tenir compte des majuscules).";

    [[nodiscard]] const ClientEntry* current() const {
        for (const auto& c : shown_)
            if (c.name == selected_) return &c;
        return nullptr;
    }

    void rebuild() {
        const ui::SearchQuery q(search_->text());
        shown_.clear();
        for (const auto& c : all_) {
            std::vector<std::string> texts{c.name};
            texts.insert(texts.end(), c.licenceIds.begin(), c.licenceIds.end());
            if (!q.empty() && !q.matches(texts)) continue;
            shown_.push_back(c);
        }
        if (!current()) selected_ = shown_.empty() ? std::string() : shown_.front().name;
        std::vector<std::vector<std::string>> rows, keys;
        std::vector<int> pending;
        for (const auto& c : shown_) {
            rows.push_back({c.name, std::to_string(c.licenceIds.size()), std::to_string(c.activeLicences), std::to_string(c.devices), std::to_string(c.pendingRequests),
                            dateFr(c.lastActivity)});
            keys.push_back({lowerAscii(c.name), padKey(static_cast<long long>(c.licenceIds.size())), padKey(c.activeLicences), padKey(c.devices),
                            padKey(c.pendingRequests), padKey(c.lastActivity)});
            pending.push_back(c.pendingRequests);
        }
        loading_ = true;
        model_->set(std::move(rows), std::move(keys), [pending](RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (c == 0) s.bold = true;
            if (c == 4 && r < pending.size() && pending[r] > 0) s.fgTone = ui::Tone::Info;
            return s;
        });
        for (std::size_t i = 0; i < shown_.size(); ++i)
            if (shown_[i].name == selected_) table_->selectModelRows({static_cast<RowIndex>(i)}, false);
        loading_ = false;
        rebuildDetail();
    }

    void rebuildDetail() {
        const ClientEntry* c = current();
        std::vector<std::vector<std::string>> lrows, lkeys, drows;
        std::vector<int> ltone;
        std::vector<std::string> dstates;
        if (c) {
            for (const auto& id : c->licenceIds) {
                if (const LicenceEntry* l = ws().licence(id)) {
                    lrows.push_back({id, l->licence.edition, l->stateLabel(), l->licence.expiresAt ? dateFr(l->licence.expiresAt) : std::string("sans fin")});
                    lkeys.push_back({"", "", "", padKey(l->licence.expiresAt ? l->licence.expiresAt : 99999999999LL)});
                    ltone.push_back(l->revoked ? 3 : l->expired ? 2 : 1);
                } else {
                    lrows.push_back({id, "", "pas dans l'archive", ""});
                    lkeys.push_back({});
                    ltone.push_back(0);
                }
            }
            const std::string me = lowerAscii(c->name);
            for (const auto& d : devicesAll_) {
                const bool mine = (!d.customer.empty() && lowerAscii(d.customer) == me)
                                  || (!d.licenseId.empty() && std::find(c->licenceIds.begin(), c->licenceIds.end(), d.licenseId) != c->licenceIds.end());
                if (!mine) continue;
                drows.push_back({d.name.empty() ? std::string("(sans nom)") : d.name, d.licenseId, deviceState(d.state), d.fingerprint});
                dstates.push_back(d.state);
            }
        }
        licencesModel_->set(std::move(lrows), std::move(lkeys), [ltone](RowIndex r, std::size_t col) {
            ui::CellStyle s;
            if (col == 0) s.monospace = true;
            if (col == 2 && r < ltone.size())
                s.fgTone = ltone[r] == 1 ? ui::Tone::Ok : ltone[r] == 2 ? ui::Tone::Warning : ltone[r] == 3 ? ui::Tone::Error : ui::Tone::Muted;
            return s;
        });
        devicesModel_->set(std::move(drows), {}, [dstates](RowIndex r, std::size_t col) {
            ui::CellStyle s;
            if (col == 3) s.monospace = true;
            if (col == 2 && r < dstates.size())
                s.fgTone = dstates[r] == "active" ? ui::Tone::Ok : dstates[r] == "demande" ? ui::Tone::Info : dstates[r] == "revoque" ? ui::Tone::Error : ui::Tone::Warning;
            return s;
        });
        newLicence_->setEnabled(c != nullptr);
        seeLicences_->setEnabled(c != nullptr);
        invalidate();
    }

    core::ConnectionScope    links_;
    ui::InputText*           search_{nullptr};
    ui::Button*              newLicence_{nullptr};
    ui::Button*              seeLicences_{nullptr};
    ui::TableView*           table_{nullptr};
    ui::TableView*           licences_{nullptr};
    ui::TableView*           devices_{nullptr};
    std::shared_ptr<Rows>    model_, licencesModel_, devicesModel_;
    std::vector<ClientEntry> all_, shown_;
    std::vector<KnownDevice> devicesAll_;
    std::string              selected_;
    std::vector<std::string> leadLines_;
    bool                     loading_{false};
    float                    titleY_{0}, leadY_{0}, summaryY_{0}, licencesHeadY_{0}, devicesHeadY_{0}, devicesX_{0}, emptyY_{0};
    gfx::Rect                panel_{}, countRect_{};
};

} // namespace

PagePtr makeClientsPage(ShellHost host) { return std::make_unique<ClientsPage>(std::move(host)); }

} // namespace license_admin::gui
