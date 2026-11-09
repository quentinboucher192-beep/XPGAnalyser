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
//  1.42.0 : LE DEBALLAGE EN ARBRE (la maquette validee) - chaque client se deplie : ses licences (chacune : ses PC, ses
//  options), ses achats, ses demandes en attente ; << Tout deplier >>, << Tout replier >>. << Actualiser >> (F5) relit
//  l'archive, le registre des activations et, le service regle, les licences en ligne. A droite, la fiche du client
//  choisi : Nouvelle licence pour ce client, Voir ses licences, Supprimer le client (ses licences toutes finies : elles
//  vont dans la corbeille de l'outil) ; dessous, la corbeille des clients (Restaurer).
class ClientsPage final : public Page {
public:
    explicit ClientsPage(ShellHost host) : Page("page.clients", std::move(host)) {
        search_ = add(*this, std::make_unique<ui::InputText>("clients.recherche"));
        search_->setPlaceholder("Client, licence, PC\xE2\x80\xA6");
        search_->setEscapeClears(true);
        refreshBtn_ = add(*this, std::make_unique<ui::Button>("Actualiser", "clients.actualiser"));
        refreshBtn_->setIcon(ui::Icon::Refresh);
        refreshBtn_->setTooltip("Relit tout de suite l'archive, le registre des activations et, le service r\xC3\xA9gl\xC3\xA9, les licences en ligne (F5)");
        expandBtn_ = add(*this, std::make_unique<ui::Button>("Tout d\xC3\xA9plier", "clients.deplier"));
        expandBtn_->setStyle(ui::Button::Style::Flat);
        expandBtn_->setIcon(ui::Icon::Expand);
        collapseBtn_ = add(*this, std::make_unique<ui::Button>("Tout replier", "clients.replier"));
        collapseBtn_->setStyle(ui::Button::Style::Flat);
        collapseBtn_->setIcon(ui::Icon::Collapse);
        newLicence_ = add(*this, std::make_unique<ui::Button>("Nouvelle licence pour ce client\xE2\x80\xA6", "clients.nouvelle-licence"));
        newLicence_->setStyle(ui::Button::Style::Primary);
        newLicence_->setIcon(ui::Icon::Plus);
        seeLicences_ = add(*this, std::make_unique<ui::Button>("Voir ses licences", "clients.voir-licences"));
        seeLicences_->setIcon(ui::Icon::GoTo);
        deleteBtn_ = add(*this, std::make_unique<ui::Button>("Supprimer le client\xE2\x80\xA6", "clients.supprimer"));
        deleteBtn_->setStyle(ui::Button::Style::Danger);
        deleteBtn_->setTooltip("Un client dont aucune licence n'est en cours : ses licences vont dans la corbeille de l'outil (rien n'est effac\xC3\xA9 ; "
                               "Restaurer le remet)");
        restoreBtn_ = add(*this, std::make_unique<ui::Button>("Restaurer", "clients.restaurer"));
        restoreBtn_->setIcon(ui::Icon::Undo);
        table_ = add(*this, std::make_unique<ui::TableView>("clients.table"));
        table_->setColumns({{"Client \xC2\xB7 licence \xC2\xB7 PC", 320.f}, {"Offre \xC2\xB7 \xC3\xA9tat", 190.f}, {"Fin", 104.f}, {"PC", 64.f}, {"Activit\xC3\xA9", 140.f}});
        table_->setSelectionMode(ui::SelectionMode::Single);
        table_->setRowHeightOverride(34.f);
        model_ = std::make_shared<Rows>(std::vector<std::string>{"Client \xC2\xB7 licence \xC2\xB7 PC", "Offre \xC2\xB7 \xC3\xA9tat", "Fin", "PC", "Activit\xC3\xA9"});
        table_->setModel(model_);
        trash_ = add(*this, std::make_unique<ui::TableView>("clients.corbeille"));
        trash_->setColumns({{"Client", 160.f}, {"Supprim\xC3\xA9 le", 100.f}, {"Licences", 70.f}});
        trash_->setSelectionMode(ui::SelectionMode::Single);
        trashModel_ = std::make_shared<Rows>(std::vector<std::string>{"Client", "Supprim\xC3\xA9 le", "Licences"});
        trash_->setModel(trashModel_);

        links_ += search_->textChanged->connect([this](const std::string&) { rebuild(); });
        links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>& rows) {
            if (loading_ || rows.empty() || rows.front() >= rows_.size()) return;
            selected_ = rows_[rows.front()].client;
            selectedKey_ = rows_[rows.front()].key;
            updateButtons();
            invalidate();
        });
        links_ += table_->expanderClicked->connect([this](RowIndex r) {
            if (r >= rows_.size()) return;
            toggle(rows_[r].key);
        });
        links_ += table_->activated->connect([this](RowIndex r) {   // un double clic deplie aussi
            if (r < rows_.size() && rows_[r].expandable) toggle(rows_[r].key);
        });
        links_ += expandBtn_->clicked->connect([this] {
            expandAll_ = true;
            open_.clear();
            rebuild();
        });
        links_ += collapseBtn_->clicked->connect([this] {
            expandAll_ = false;
            open_.clear();
            closed_.clear();
            rebuild();
        });
        links_ += refreshBtn_->clicked->connect([this] { reloadNow(); });
        links_ += newLicence_->clicked->connect([this] {
            const ClientEntry* c = current();
            if (!c || !host_.openDialog) return;
            host_.openDialog(makeLicenceWizard(host_, {}, {}, c->name));
        });
        links_ += seeLicences_->clicked->connect([this] {
            if (current() && host_.navigate) host_.navigate("licences");
        });
        links_ += deleteBtn_->clicked->connect([this] { askDelete(); });
        links_ += restoreBtn_->clicked->connect([this] { restoreSelected(); });
        refresh();
    }

    [[nodiscard]] std::string title() const override { return "Clients"; }

    void refresh() override {
        all_ = ws().clients();
        devicesAll_ = ws().devices();
        deleted_ = ws().deletedClients();
        readAt_ = ws().now();
        rebuild();
        rebuildTrash();
        invalidateLayout();
        invalidate();
    }

protected:
    void onLayout() override {
        const gfx::Rect b = bounds();
        const float x = b.x + kPad, w = std::max(200.f, b.w - 2.f * kPad);
        float y = b.y + kPad;
        titleY_ = y;
        // en haut a droite : Actualiser
        const float rw = std::ceil(refreshBtn_->sizeHint().preferred.w) + 8.f;
        refreshBtn_->setBounds({x + w - rw, y, rw, kButtonH});
        y += 34.f;
        leadLines_ = wrap(kLead, kFonts.ui, w, 2);
        leadY_ = y;
        y += static_cast<float>(leadLines_.size()) * kLineH + 10.f;
        search_->setBounds({x, y + 2.f, std::min(300.f, w * 0.32f), kFieldH});
        const float cw = std::ceil(collapseBtn_->sizeHint().preferred.w), ew = std::ceil(expandBtn_->sizeHint().preferred.w);
        const float cardW = std::clamp(w * 0.32f, 300.f, 420.f);
        const float tableRight = x + w - cardW - 12.f;
        collapseBtn_->setBounds({tableRight - cw, y, cw, kButtonH});
        expandBtn_->setBounds({tableRight - cw - 6.f - ew, y, ew, kButtonH});
        const float tw = ui::measureWidth("Clients", kFonts.title) + 24.f;
        countRect_ = {x + tw, titleY_ + 4.f, std::max(0.f, w - tw - rw - 220.f), 22.f};
        y += kButtonH + kGap;
        const gfx::Rect area{x, y, w, std::max(60.f, b.bottom() - kPad - y)};
        table_->setBounds({area.x, area.y, std::max(200.f, area.w - cardW - 12.f), area.h});
        card_ = {area.right() - cardW, area.y, cardW, area.h};
        // la fiche du client choisi
        float cy = card_.y + 32.f + 10.f;
        summaryY_ = cy;
        cy += 4.f * kLineH + 10.f;
        const float bw = card_.w - 24.f;
        newLicence_->setBounds({card_.x + 12.f, cy, bw, kButtonH});
        cy += kButtonH + 6.f;
        const float half = std::floor((bw - 6.f) * 0.5f);
        seeLicences_->setBounds({card_.x + 12.f, cy, half, kButtonH});
        deleteBtn_->setBounds({card_.x + 18.f + half, cy, bw - half - 6.f, kButtonH});
        cy += kButtonH + 8.f;
        deleteNoteY_ = cy;
        cy += 2.f * 17.f + 12.f;
        // la corbeille des clients
        trashHeadY_ = cy;
        cy += 24.f;
        const float rbw = std::ceil(restoreBtn_->sizeHint().preferred.w) + 8.f;
        const float th = std::max(48.f, card_.bottom() - 12.f - kButtonH - 8.f - cy);
        trash_->setBounds({card_.x + 12.f, cy, bw, th});
        // les colonnes de la corbeille a la largeur de la fiche
        const float cDate = 96.f, cLic = 66.f;
        trash_->setColumns({{"Client", std::max(80.f, bw - cDate - cLic - 14.f)}, {"Supprim\xC3\xA9 le", cDate}, {"Licences", cLic}});
        restoreBtn_->setBounds({card_.right() - 12.f - rbw, cy + th + 8.f, rbw, kButtonH});
        emptyY_ = area.y + 48.f;
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
        std::string n = count(shownClients_, "client", "clients");
        if (shownClients_ != all_.size()) n += " (sur " + std::to_string(all_.size()) + ")";
        n += " \xC2\xB7 relu \xC3\xA0 " + hourOf(readAt_);
        const std::string shownCount = fit(r, n, ctx.theme.font.ui, countRect_.w);
        r.drawText({countRect_.x, countRect_.y + (countRect_.h - r.lineHeight(ctx.theme.font.ui)) * 0.5f}, shownCount, ctx.theme.font.ui, c.textMuted);
        // La fiche du client choisi.
        r.fillRect(card_, c.windowBg);
        r.strokeRect(card_, c.border, 1.f);
        r.fillRect({card_.x, card_.y, card_.w, 32.f}, c.headerBg);
        const ClientEntry* cl = current();
        const std::string head = cl ? cl->name : std::string("Aucun client choisi");
        r.drawText({card_.x + 12.f, card_.y + (32.f - r.lineHeight(ctx.theme.font.heading)) * 0.5f}, fit(r, head, ctx.theme.font.heading, card_.w - 24.f),
                   ctx.theme.font.heading, cl ? c.text : c.textMuted);
        if (cl) {
            const auto lines = summaryOf(*cl);
            for (std::size_t i = 0; i < lines.size() && i < 4; ++i)
                r.drawText({card_.x + 12.f, summaryY_ + static_cast<float>(i) * kLineH}, fit(r, lines[i], ctx.theme.font.ui, card_.w - 24.f), ctx.theme.font.ui,
                           i == 0 ? c.text : (i == 3 ? c.warning : c.textMuted));
            if (!deleteNote_.empty()) {
                const auto note = wrap(deleteNote_, ctx.theme.font.caption, card_.w - 24.f, 2);
                for (std::size_t i = 0; i < note.size(); ++i)
                    r.drawText({card_.x + 12.f, deleteNoteY_ + static_cast<float>(i) * 17.f}, note[i], ctx.theme.font.caption, c.textMuted);
            }
        }
        r.fillRect({card_.x + 12.f, trashHeadY_ - 8.f, card_.w - 24.f, 1.f}, c.border);
        r.drawText({card_.x + 12.f, trashHeadY_}, "Corbeille des clients (" + std::to_string(deleted_.size()) + ")", ctx.theme.font.ui, c.textMuted);
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (shownClients_ > 0) return;
        auto& r = ctx.r;
        const gfx::Rect t = table_->bounds();
        const std::string line = all_.empty() ? "Pas encore de client : un client appara\xC3\xAEt avec sa premi\xC3\xA8re licence sign\xC3\xA9" "e."
                                              : "Aucun client ne correspond \xC3\xA0 la recherche.";
        r.drawText({t.x + 16.f, emptyY_}, fit(r, line, ctx.theme.font.ui, t.w - 32.f), ctx.theme.font.ui, ctx.theme.color.textMuted);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::F5 && k->mods.none()) {
            reloadNow();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    static constexpr const char* kLead =
        "Un client, c'est le nom \xC3\xA9" "crit dans ses licences (sans tenir compte des majuscules). D\xC3\xA9plie-le : ses licences, leurs PC et "
        "leurs options, ses achats, ses demandes.";

    // Une ligne de l'arbre : sa cle (ce qui se deplie), son client, ses cases, son retrait et sa fleche.
    struct Line {
        std::string              key, client;
        std::vector<std::string> cells;
        int                      depth{0};
        bool                     expandable{false}, expanded{false};
        int                      tone{0};    // 1 ok, 2 attention, 3 erreur, 4 discret, 5 info
        std::string              badge;
        int                      badgeTone{0};
        bool                     bold{false};
    };

    static std::string hourOf(Seconds t) {
        const std::string s = dateTimeFr(t);   // "JJ/MM/AAAA HH:MM"
        return s.size() >= 5 ? s.substr(s.size() - 5) : s;
    }

    [[nodiscard]] const ClientEntry* current() const {
        for (const auto& c : all_)
            if (c.name == selected_) return &c;
        return nullptr;
    }

    [[nodiscard]] bool isOpen(const std::string& key) const {
        if (open_.count(key)) return true;
        if (closed_.count(key)) return false;
        return expandAll_;
    }
    void toggle(const std::string& key) {
        if (isOpen(key)) {
            open_.erase(key);
            closed_.insert(key);
        } else {
            closed_.erase(key);
            open_.insert(key);
        }
        rebuild();
    }

    [[nodiscard]] std::vector<std::string> summaryOf(const ClientEntry& c) const {
        std::vector<std::string> out;
        std::string a = count(c.licenceIds.size(), "licence", "licences") + " (" + count(static_cast<std::size_t>(c.activeLicences), "en cours", "en cours") + ") \xC2\xB7 " +
                        count(static_cast<std::size_t>(c.devices), "PC li\xC3\xA9", "PC li\xC3\xA9s");
        out.push_back(a);
        const auto hist = ws().purchaseHistory(c.name);
        std::string b = hist.empty() ? std::string("Aucun achat not\xC3\xA9") : count(hist.size(), "achat", "achats") + " depuis le " + dateFr(hist.front().at);
        out.push_back(b);
        out.push_back(c.lastActivity ? "Derni\xC3\xA8re activit\xC3\xA9 le " + dateFr(c.lastActivity) : std::string("Aucune activit\xC3\xA9 not\xC3\xA9" "e"));
        if (c.pendingRequests > 0) out.push_back(count(static_cast<std::size_t>(c.pendingRequests), "demande en attente", "demandes en attente"));
        return out;
    }

    // L'etat d'une licence : son mot et sa couleur.
    std::pair<std::string, int> licenceState(const LicenceEntry& l) const {
        if (l.revoked) return {"r\xC3\xA9voqu\xC3\xA9" "e", 3};
        if (l.expired) return {"expir\xC3\xA9" "e", 4};
        const Seconds t = ws().now();
        if (l.licence.expiresAt && l.licence.expiresAt - t <= 30 * 86400) return {"finit bient\xC3\xB4t", 2};
        return {"active", 1};
    }

    void rebuild() {
        const ui::SearchQuery q(search_->text());
        rows_.clear();
        shownClients_ = 0;
        const Seconds t = ws().now();
        for (const auto& c : all_) {
            // ce que la recherche regarde : le client, ses licences, ses PC
            std::vector<std::string> texts{c.name};
            texts.insert(texts.end(), c.licenceIds.begin(), c.licenceIds.end());
            for (const auto& id : c.licenceIds)
                if (const LicenceEntry* l = ws().licence(id))
                    for (const auto& d : l->licence.devices) texts.push_back(d.name);
            if (!q.empty() && !q.matches(texts)) continue;
            ++shownClients_;
            const std::string ck = "c:" + lowerAscii(c.name);
            // la ligne du client
            Line cl;
            cl.key = ck;
            cl.client = c.name;
            cl.depth = 0;
            cl.bold = true;
            cl.expandable = !c.licenceIds.empty();
            cl.expanded = isOpen(ck);
            Seconds lastEnd = 0;
            bool forever = false;
            for (const auto& id : c.licenceIds)
                if (const LicenceEntry* l = ws().licence(id); l && Workspace::inForce(*l)) {
                    if (!l->licence.expiresAt) forever = true;
                    lastEnd = std::max(lastEnd, l->licence.expiresAt);
                }
            const std::string state = c.activeLicences > 0 ? "active" : std::string("plus rien en cours");
            cl.cells = {c.name, count(c.licenceIds.size(), "licence", "licences") + " \xC2\xB7 " + state,
                        forever ? std::string("sans fin") : (lastEnd ? dateFr(lastEnd) : std::string()), std::to_string(c.devices), dateFr(c.lastActivity)};
            cl.tone = c.activeLicences > 0 ? 0 : 4;
            if (c.pendingRequests > 0) {
                cl.badge = count(static_cast<std::size_t>(c.pendingRequests), "demande", "demandes");
                cl.badgeTone = 2;
            }
            rows_.push_back(cl);
            if (!cl.expanded) continue;
            // ses licences
            for (const auto& id : c.licenceIds) {
                const LicenceEntry* le = ws().licence(id);
                Line ll;
                ll.key = "l:" + id;
                ll.client = c.name;
                ll.depth = 1;
                if (!le) {
                    ll.cells = {id, "pas dans l'archive", "", "", ""};
                    ll.tone = 4;
                    rows_.push_back(ll);
                    continue;
                }
                const auto& lic = le->licence;
                const bool onlineLic = Workspace::isOnlineLicence(lic);
                const auto* on = onlineLic ? ws().service().snapshot.licence(id) : nullptr;
                const auto [word, tone] = licenceState(*le);
                const int pcs = onlineLic ? (on ? on->activeCount() : 0) : static_cast<int>(lic.devices.size());
                ll.cells = {id, lic.edition + " \xC2\xB7 " + word, lic.expiresAt ? dateFr(lic.expiresAt) : std::string("sans fin"),
                            std::to_string(pcs) + " / " + std::to_string(lic.maxDevices), dateFr(lic.issuedAt)};
                ll.tone = tone;
                const bool hasPcs = onlineLic ? (on && !on->activations.empty()) : !lic.devices.empty();
                ll.expandable = hasPcs || !lic.options.empty();
                ll.expanded = ll.expandable && isOpen(ll.key);
                if (onlineLic) {
                    ll.badge = "en ligne";
                    ll.badgeTone = 5;
                }
                rows_.push_back(ll);
                if (!ll.expanded) continue;
                if (hasPcs) {   // le groupe des PC
                    Line g;
                    g.key = "p:" + id;
                    g.client = c.name;
                    g.depth = 2;
                    g.expandable = true;
                    g.expanded = isOpen(g.key);
                    g.cells = {"PC (" + std::to_string(onlineLic && on ? on->activations.size() : lic.devices.size()) + ")", "", "", "", ""};
                    rows_.push_back(g);
                    if (g.expanded) {
                        if (on)
                            for (const auto& a : on->activations) {
                                Line p;
                                p.key = "pc:" + id + ":" + a.deviceKey;
                                p.client = c.name;
                                p.depth = 3;
                                p.cells = {a.name.empty() ? std::string("(sans nom)") : a.name, a.stateLabel(), a.until ? "bail \xE2\x86\x92 " + dateFr(a.until) : std::string(), "",
                                           a.seenAt ? "vu le " + dateFr(a.seenAt) : std::string()};
                                p.tone = a.state == "active" ? 1 : 4;
                                rows_.push_back(p);
                            }
                        for (const auto& d : lic.devices) {
                            Line p;
                            p.key = "pc:" + id + ":" + d.key;
                            p.client = c.name;
                            p.depth = 3;
                            p.cells = {d.name.empty() ? std::string("(sans nom)") : d.name, le->revoked ? std::string("r\xC3\xA9voqu\xC3\xA9") : std::string("li\xC3\xA9"), "", "",
                                       "activ\xC3\xA9 le " + dateFr(d.activatedAt)};
                            p.tone = le->revoked ? 3 : 1;
                            rows_.push_back(p);
                        }
                    }
                }
                if (!lic.options.empty()) {   // le groupe des options
                    Line g;
                    g.key = "o:" + id;
                    g.client = c.name;
                    g.depth = 2;
                    g.expandable = true;
                    g.expanded = isOpen(g.key);
                    g.cells = {"Options (" + std::to_string(lic.options.size()) + ")", "", "", "", ""};
                    rows_.push_back(g);
                    if (g.expanded)
                        for (std::size_t k = 0; k < lic.options.size(); ++k) {
                            const auto& o = lic.options[k];
                            Line p;
                            p.key = "op:" + id + ":" + std::to_string(k);
                            p.client = c.name;
                            p.depth = 3;
                            const Seconds end = o.until ? o.until : lic.expiresAt;
                            std::string st = o.activeAt(t, lic.expiresAt) ? (end ? "encore " + std::to_string(std::max<Seconds>(1, (end - t + 86399) / 86400)) + " j" : std::string("en cours"))
                                                                          : (t < o.from ? std::string("\xC3\xA0 venir") : std::string("finie"));
                            p.cells = {o.label, st, end ? dateFr(end - 1) : std::string("fin de licence"), "", "depuis le " + dateFr(o.from)};
                            p.tone = o.activeAt(t, lic.expiresAt) ? 5 : 4;
                            rows_.push_back(p);
                        }
                }
            }
            // ses achats
            const auto hist = ws().purchaseHistory(c.name);
            if (!hist.empty()) {
                Line g;
                g.key = "a:" + lowerAscii(c.name);
                g.client = c.name;
                g.depth = 1;
                g.expandable = true;
                g.expanded = isOpen(g.key);
                g.cells = {"Achats (" + std::to_string(hist.size()) + ")", "", "", "", ""};
                rows_.push_back(g);
                if (g.expanded)
                    for (auto it = hist.rbegin(); it != hist.rend(); ++it) {
                        Line p;
                        p.key = "ac:" + it->licenseId + ":" + std::to_string(it->at) + ":" + it->label;
                        p.client = c.name;
                        p.depth = 2;
                        const char* kind = it->kind == "option" ? "option" : it->kind == "extension" ? "prolongation" : "offre";
                        p.cells = {it->label, std::string(kind) + (it->price.empty() ? std::string() : " \xC2\xB7 " + it->price),
                                   it->until ? dateFr(it->until) : std::string("sans fin"), "", dateFr(it->at)};
                        p.tone = 4;
                        rows_.push_back(p);
                    }
            }
            // ses demandes en attente
            const std::string me = lowerAscii(c.name);
            for (const auto& d : devicesAll_) {
                if (d.state != "demande") continue;
                const bool mine = (!d.customer.empty() && lowerAscii(d.customer) == me) ||
                                  (!d.licenseId.empty() && std::find(c.licenceIds.begin(), c.licenceIds.end(), d.licenseId) != c.licenceIds.end());
                if (!mine) continue;
                Line p;
                p.key = "d:" + d.deviceKey;
                p.client = c.name;
                p.depth = 1;
                p.cells = {"Demande : " + (d.name.empty() ? std::string("(sans nom)") : d.name), "\xC3\xA0 d\xC3\xA9" "cider", "", "", dateFr(d.requestedAt)};
                p.tone = 2;
                rows_.push_back(p);
            }
        }
        if (!current() || !std::any_of(rows_.begin(), rows_.end(), [&](const Line& l) { return l.client == selected_; })) {
            selected_ = rows_.empty() ? std::string() : rows_.front().client;
            selectedKey_ = rows_.empty() ? std::string() : rows_.front().key;
        }
        std::vector<std::vector<std::string>> cells;
        for (const auto& l : rows_) cells.push_back(l.cells);
        const std::vector<Line> lines = rows_;
        loading_ = true;
        const float scroll = table_->scrollOffset();
        model_->set(std::move(cells), {}, [lines](RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (r >= lines.size()) return s;
            const Line& l = lines[r];
            const auto toneOf = [](int t) {
                switch (t) {
                    case 1: return ui::Tone::Ok;
                    case 2: return ui::Tone::Warning;
                    case 3: return ui::Tone::Error;
                    case 4: return ui::Tone::Muted;
                    case 5: return ui::Tone::Info;
                    default: return ui::Tone::None;
                }
            };
            if (c == 0) {
                s.indent = 20.f * static_cast<float>(l.depth);
                s.expander = l.expandable ? (l.expanded ? 1 : 0) : -1;
                s.bold = l.bold;
                if (!l.badge.empty()) {
                    s.badge = l.badge;
                    s.badgeTone = toneOf(l.badgeTone);
                }
                if (l.depth >= 3 || l.key.rfind("pc:", 0) == 0) s.monospace = l.key.rfind("pc:", 0) == 0;
            }
            if (c == 1 && l.tone) s.fgTone = toneOf(l.tone);
            if (c > 1 && l.tone == 4) s.fgTone = ui::Tone::Muted;
            return s;
        });
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].key == selectedKey_) table_->selectModelRows({static_cast<RowIndex>(i)}, false);
        table_->setScrollOffset(scroll);
        loading_ = false;
        updateButtons();
        invalidate();
    }

    void rebuildTrash() {
        std::vector<std::vector<std::string>> rows;
        for (const auto& d : deleted_) rows.push_back({d.name, dateFr(d.deletedAt), std::to_string(d.licenceIds.size())});
        trashModel_->set(std::move(rows), {}, [](RowIndex, std::size_t) { return ui::CellStyle{}; });
        restoreBtn_->setEnabled(!deleted_.empty());
    }

    void updateButtons() {
        const ClientEntry* c = current();
        newLicence_->setEnabled(c != nullptr);
        seeLicences_->setEnabled(c != nullptr);
        deleteNote_.clear();
        bool canDelete = c != nullptr;
        if (c) {
            for (const auto& id : c->licenceIds)
                if (const LicenceEntry* l = ws().licence(id); l && Workspace::inForce(*l)) {
                    canDelete = false;
                    deleteNote_ = "Une licence en cours (" + id + (l->licence.expiresAt ? ", jusqu'au " + dateFr(l->licence.expiresAt) : std::string()) +
                                  ") : r\xC3\xA9voque-la ou attends sa fin pour le supprimer.";
                    break;
                }
            if (canDelete && c->pendingRequests > 0) {
                canDelete = false;
                deleteNote_ = "Une demande attend ta d\xC3\xA9" "cision (Demandes en ligne).";
            }
        }
        deleteBtn_->setEnabled(canDelete);
    }

    void reloadNow() {
        OpResult r = OpResult::done("Clients relus : l'archive et le registre des activations.");
        if (ws().service().ready()) {
            r.message = "Clients relus : l'archive, le registre des activations ; les licences en ligne sont relev\xC3\xA9" "es sur le service.";
            (void)refreshOnlineLicences(host_);
        }
        if (host_.report) host_.report(r);   // tout est relu, chaque page rafraichie
        else {
            ws().reload();
            refresh();
        }
    }

    void askDelete() {
        const ClientEntry* c = current();
        if (!c) return;
        const std::string name = c->name;
        const std::size_t n = c->licenceIds.size();
        auto alive = alive_;
        const std::string text = "Ses " + count(n, "licence", "licences") + " (toutes finies) partent dans la corbeille de l'outil, avec lui : rien n'est "
                                 "effac\xC3\xA9, et la synchro ne les ram\xC3\xA8ne pas. Sa r\xC3\xA9vocation, s'il en a une, reste dans la liste sign\xC3\xA9" "e.\n\n"
                                 "Pour le remettre : la corbeille des clients, en bas de la fiche, \xE2\x80\xBA Restaurer.";
        host_.openDialog(licdlg::makeConfirmDialog("dialog.supprimer-client", "Supprimer le client " + name + " ?", text, "Supprimer le client", true,
                                                   [this, alive, name] {
                                                       if (!*alive) return;
                                                       host_.report(ws().deleteClient(name));
                                                   }));
    }

    void restoreSelected() {
        const auto sel = trash_->selectedModelRows();
        std::size_t i = sel.empty() ? 0 : static_cast<std::size_t>(sel.front());
        if (i >= deleted_.size()) return;
        host_.report(ws().restoreClient(deleted_[i].name));
    }

    core::ConnectionScope    links_;
    std::shared_ptr<bool>    alive_{std::make_shared<bool>(true)};
    ui::InputText*           search_{nullptr};
    ui::Button*              refreshBtn_{nullptr};
    ui::Button*              expandBtn_{nullptr};
    ui::Button*              collapseBtn_{nullptr};
    ui::Button*              newLicence_{nullptr};
    ui::Button*              seeLicences_{nullptr};
    ui::Button*              deleteBtn_{nullptr};
    ui::Button*              restoreBtn_{nullptr};
    ui::TableView*           table_{nullptr};
    ui::TableView*           trash_{nullptr};
    std::shared_ptr<Rows>    model_, trashModel_;
    std::vector<ClientEntry> all_;
    std::vector<KnownDevice> devicesAll_;
    std::vector<DeletedClient> deleted_;
    std::vector<Line>        rows_;
    std::set<std::string>    open_, closed_;
    bool                     expandAll_{false};
    std::size_t              shownClients_{0};
    std::string              selected_, selectedKey_, deleteNote_;
    std::vector<std::string> leadLines_;
    bool                     loading_{false};
    Seconds                  readAt_{0};
    float                    titleY_{0}, leadY_{0}, summaryY_{0}, deleteNoteY_{0}, trashHeadY_{0}, emptyY_{0};
    gfx::Rect                card_{}, countRect_{};

public:
    ~ClientsPage() override { *alive_ = false; }
};

} // namespace

PagePtr makeClientsPage(ShellHost host) { return std::make_unique<ClientsPage>(std::move(host)); }

} // namespace license_admin::gui
