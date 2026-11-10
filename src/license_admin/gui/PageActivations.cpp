// =============================================================================
//  license_admin/gui/PageActivations.cpp - 1.33.0 : LA PAGE Activations (les PC
//  connus du poste Admin)
// -----------------------------------------------------------------------------
//  Le registre des activations (activations.jsonl, dans le dossier de la cle) :
//  le dernier etat de chaque PC (Workspace::devices) - une demande recue, une
//  licence signee, une revocation. Seulement ce que les clients envoient : il
//  n'y a pas de service en ligne, rien ne remonte des PC eux-memes, et une
//  demande ne porte rien de leur materiel (une cle publique, un nom choisi, le
//  logiciel). La page cherche, filtre par etat, compte, note des demandes
//  (.xpgreq, choisies ou glissees sur la fenetre) et ecrit le tableau pour
//  Excel (le CSV de la console : UTF-8 avec BOM, ; et CRLF, les formules
//  neutralisees).
// =============================================================================
#include "ListKit.hpp"   // 1.47.0 : la barre de filtres, le clic droit, le depliage
#include "Page.hpp"

#include "../../ui/TextSearch.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
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
constexpr const char* kBusy = "Un travail est en cours : attends qu'il finisse.";

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
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();   // 1.44.0 : un caractere entier
        if (!t.empty()) t.pop_back();
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

// L'etat d'un PC, a l'ecran (KnownDevice::state : "demande", "active", "revoque", "expire").
std::string stateLabel(const std::string& s) {
    if (s == "demande") return "demande";
    if (s == "active") return "active";
    if (s == "revoque") return "r\xC3\xA9voqu\xC3\xA9" "e";
    if (s == "expire") return "expir\xC3\xA9" "e";
    return s;
}

ui::Tone stateTone(const std::string& s) {
    if (s == "active") return ui::Tone::Ok;
    if (s == "demande") return ui::Tone::Info;
    if (s == "revoque") return ui::Tone::Error;
    if (s == "expire") return ui::Tone::Warning;
    return ui::Tone::None;
}

bool isRequestFile(const std::string& path) {
    const std::string l = lowerAscii(path);
    return l.size() > 7 && l.compare(l.size() - 7, 7, ".xpgreq") == 0;
}

// =================================================================================== la page
class ActivationsPage final : public Page {
public:
    explicit ActivationsPage(ShellHost host) : Page("page.activations", std::move(host)) {
        record_ = add(*this, std::make_unique<ui::Button>("Noter des demandes\xE2\x80\xA6", "activations.noter"));
        record_->setStyle(ui::Button::Style::Primary);
        record_->setIcon(ui::Icon::Import);
        record_->setTooltip("Une demande d'activation (.xpgreq) re\xC3\xA7ue d'un client, not\xC3\xA9" "e sans rien signer. Plusieurs : glisse-les sur la fen\xC3\xAAtre.");
        export_ = add(*this, std::make_unique<ui::Button>("Exporter pour Excel\xE2\x80\xA6", "activations.exporter"));
        export_->setIcon(ui::Icon::Export);
        export_->setTooltip("Les PC montr\xC3\xA9s (recherche et filtre compris), en CSV : UTF-8, s\xC3\xA9parateur ; - Excel l'ouvre tel quel.");
        folder_ = add(*this, std::make_unique<ui::Button>("Ouvrir le registre des activations", "activations.dossier"));
        folder_->setIcon(ui::Icon::FolderOpen);
        folder_->setTooltip("Le dossier du poste Admin : activations.jsonl (une ligne par \xC3\xA9v\xC3\xA9nement, jamais r\xC3\xA9\xC3\xA9" "crite).");
        table_ = add(*this, std::make_unique<ui::TableView>("activations.table"));
        table_->setColumns({{"PC", 170.f}, {"Empreinte", 250.f}, {"Client", 190.f}, {"Licence", 140.f}, {"\xC3\x89" "dition", 120.f}, {"Logiciel", 220.f},
                            {"Demand\xC3\xA9" "e le", 128.f}, {"Activ\xC3\xA9" "e le", 120.f}, {"Fin", 110.f}, {"\xC3\x89tat", 96.f}});
        table_->setSelectionMode(ui::SelectionMode::Extended);
        model_ = std::make_shared<Rows>(std::vector<std::string>{"PC", "Empreinte", "Client", "Licence", "\xC3\x89" "dition", "Logiciel", "Demand\xC3\xA9" "e le", "Activ\xC3\xA9" "e le", "Fin", "\xC3\x89tat"});
        table_->setModel(model_);
        // 1.47.0 (planche 12) : LA BARRE - chercher (activations.recherche), le client, l'offre, le logiciel, la periode, l'etat.
        bar_ = add(*this, std::make_unique<ui::FilterBar>("activations", barFields()));
        bar_->setNoun("PC");
        bar_->setClock([this] { return listkit::seconds(ws().now()); });
        {
            ui::FilterBar::Source src;
            src.rows = [this] { return all_.size(); };
            src.text = [this](std::size_t r, std::size_t f) { return r < texts_.size() && f < texts_[r].size() ? texts_[r][f] : std::string(); };
            src.time = [this](std::size_t r, std::size_t f) -> std::int64_t {
                if (r >= all_.size()) return 0;
                return listkit::seconds(f == 6 ? all_[r].requestedAt : f == 7 ? all_[r].activatedAt : 0);
            };
            src.extra = [this](std::size_t r) { return r < all_.size() ? all_[r].deviceKey + " " + stateLabel(all_[r].state) : std::string(); };
            bar_->setSource(std::move(src));
        }
        bar_->linkTable(*table_);
        bar_->setMemoryKey("activations");
        links_ += bar_->changed->connect([this] { rebuild(); });
        links_ += ui::ListMenu::bind(*table_, [this] { return menuFor(); });   // 1.47.0 (planche 06)
        table_->setUnfold([this](RowIndex r) { return unfoldFor(r); }, 1);    // 1.47.0 (planche 07) : reperee par son empreinte

        links_ += record_->clicked->connect([this] { pickRequest(); });
        links_ += export_->clicked->connect([this] { exportCsv(); });
        links_ += folder_->clicked->connect([this] {
            if (host_.openFolder) host_.openFolder(ws().folder());
        });
        refresh();
    }
    ~ActivationsPage() override { *life_ = false; }

    [[nodiscard]] std::string title() const override { return "Activations"; }

    void refresh() override {
        all_ = ws().devices();
        texts_.clear();
        for (const auto& d : all_) texts_.push_back(cellsOf(d));
        who_.reset();
        bar_->refreshCounts();
        // 1.47.0 : << Ses PC >> d'une licence (Licences, le clic droit) : la barre filtree sur elle.
        if (const std::string f = takePageFocus("activations"); f.rfind("licence:", 0) == 0) {
            bar_->clearAll();
            bar_->filterOn(3, f.substr(8));
        } else if (f.rfind("client:", 0) == 0) {   // << Ses PC >> d'un client
            bar_->clearAll();
            bar_->filterOn(2, f.substr(7));
        } else if (f.rfind("offre:", 0) == 0) {    // << Ses PC >> d'une offre (Offres)
            bar_->clearAll();
            bar_->filterOn(4, f.substr(6));
        }
        rebuild();
        invalidateLayout();
        invalidate();
    }

    bool dropFiles(const std::vector<std::string>& paths) override {
        if (paths.empty()) return false;
        for (const auto& p : paths)
            if (!isRequestFile(p)) return false;   // autre chose : la coquille range le tout (importFiles)
        if (host_.busy()) {
            if (host_.flash) host_.flash(kBusy, Tone::Warning);
            return true;
        }
        host_.report(ws().recordRequests(paths));
        refresh();
        return true;
    }

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
        float bx = x;
        for (ui::Button* btn : {record_, export_, folder_}) {
            const float bw = btn->sizeHint().preferred.w;
            btn->setBounds({bx, y, bw, kButtonH});
            bx += bw + kGap;
        }
        y += kButtonH + kGap;
        const float bh = bar_->heightFor(w);   // 1.47.0 : la barre de filtres
        bar_->setBounds({x, y + 2.f, w, bh});
        y += bh + 8.f;
        totalsRect_ = {x, y, w, 22.f};
        y += 22.f + 4.f;
        noteY_ = y;
        if (ws().skippedLines() > 0) y += kLineH + 4.f;
        table_->setBounds({x, y, w, std::max(60.f, b.bottom() - kPad - y)});
        emptyY_ = y + 40.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const gfx::Rect b = bounds();
        r.fillRect(b, c.panelBg);
        const float x = b.x + kPad, w = std::max(200.f, b.w - 2.f * kPad);
        r.drawText({x, titleY_}, "Activations", ctx.theme.font.title, c.text);
        for (std::size_t i = 0; i < leadLines_.size(); ++i)
            r.drawText({x, leadY_ + static_cast<float>(i) * kLineH}, fit(r, leadLines_[i], ctx.theme.font.ui, w), ctx.theme.font.ui, c.textMuted);
        r.drawText({totalsRect_.x, totalsRect_.y + (totalsRect_.h - r.lineHeight(ctx.theme.font.ui)) * 0.5f}, fit(r, totals(), ctx.theme.font.ui, totalsRect_.w),
                   ctx.theme.font.ui, c.text);
        if (ws().skippedLines() > 0)
            r.drawText({x, noteY_},
                       fit(r, count(static_cast<std::size_t>(ws().skippedLines()), "ligne illisible ignor\xC3\xA9" "e", "lignes illisibles ignor\xC3\xA9" "es")
                                  + " dans activations.jsonl (ab\xC3\xAEm\xC3\xA9" "e, ou \xC3\xA9" "crite par une version plus r\xC3\xA9" "cente de l'outil).",
                           ctx.theme.font.ui, w),
                       ctx.theme.font.ui, ctx.theme.tone(ui::Tone::Warning, c.text));
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (!shown_.empty()) return;
        // Rien a montrer : dire pourquoi, sur la table vide.
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const gfx::Rect t = table_->bounds();
        const std::string line = all_.empty() ? "Aucun PC connu pour l'instant : le registre se remplit avec les demandes not\xC3\xA9" "es et les licences sign\xC3\xA9" "es."
                                              : "Aucun PC ne correspond \xC3\xA0 la recherche ou au filtre.";
        r.drawText({t.x + 16.f, emptyY_}, fit(r, line, ctx.theme.font.ui, t.w - 32.f), ctx.theme.font.ui, c.textMuted);
    }

private:
    static constexpr const char* kLead =
        "Les PC connus de ce poste : seulement ce que les clients t'envoient (leurs demandes .xpgreq) et les licences sign\xC3\xA9" "es ici. "
        "Pas de service en ligne : rien ne remonte des PC eux-m\xC3\xAAmes, rien de leur mat\xC3\xA9riel. Glisse des demandes sur la fen\xC3\xAAtre pour les noter.";

    [[nodiscard]] std::string totals() const {
        std::set<std::string> licences, customers;
        std::size_t active = 0, pending = 0, revoked = 0, expired = 0;
        for (const auto& d : shown_) {
            if (!d.licenseId.empty() && d.state != "demande") licences.insert(d.licenseId);
            if (!d.customer.empty()) customers.insert(lowerAscii(d.customer));
            if (d.state == "active") ++active;
            else if (d.state == "demande") ++pending;
            else if (d.state == "revoque") ++revoked;
            else if (d.state == "expire") ++expired;
        }
        std::string t = count(shown_.size(), "PC", "PC") + ", " + count(licences.size(), "licence", "licences") + ", " + count(customers.size(), "client", "clients")
                        + " : " + count(active, "activ\xC3\xA9", "activ\xC3\xA9s") + ", " + std::to_string(pending) + " en attente, "
                        + count(revoked, "r\xC3\xA9voqu\xC3\xA9", "r\xC3\xA9voqu\xC3\xA9s") + ", " + count(expired, "expir\xC3\xA9", "expir\xC3\xA9s");
        if (shown_.size() != all_.size()) t += " (sur " + count(all_.size(), "PC", "PC") + " en tout)";
        return t;
    }

    // Les cases d'un PC (la table, la barre).
    static std::vector<std::string> cellsOf(const KnownDevice& d) {
        return {d.name.empty() ? std::string("(sans nom)") : d.name, d.fingerprint, d.customer, d.licenseId, d.edition, d.software,
                dateFr(d.requestedAt), dateFr(d.activatedAt),
                d.expiresAt ? dateFr(d.expiresAt) : (d.state == "demande" ? std::string() : std::string("sans fin")), stateLabel(d.state)};
    }

    // 1.47.0 (planche 12) : les colonnes vues par la barre (dans l'ordre du modele).
    static std::vector<ui::FilterBar::Field> barFields() {
        using K = ui::FilterBar::Kind;
        std::vector<ui::FilterBar::Field> f(10);
        f[0] = {.title = "PC", .kind = K::Text};
        f[1] = {.title = "Empreinte", .kind = K::Text};
        f[2] = {.title = "Client", .chip = true, .noun = "un client"};
        f[3] = {.title = "Licence", .noun = "une licence"};
        f[4] = {.title = "\xC3\x89" "dition", .chip = true, .chipLabel = "Offre", .noun = "une offre"};
        f[5] = {.title = "Logiciel", .chip = true, .noun = "un logiciel"};
        f[6] = {.title = "Demand\xC3\xA9" "e le", .kind = K::Period};
        f[7] = {.title = "Activ\xC3\xA9" "e le", .kind = K::Period, .chip = true, .chipLabel = "Activ\xC3\xA9" "e"};
        f[8] = {.title = "Fin", .kind = K::Text};
        f[9] = {.title = "\xC3\x89tat", .chip = true, .noun = "un \xC3\xA9tat", .order = {"demande", "active", "r\xC3\xA9voqu\xC3\xA9" "e", "expir\xC3\xA9" "e"}};
        return f;
    }

    void rebuild() {
        shown_.clear();
        for (std::size_t i = 0; i < all_.size(); ++i)
            if (bar_->accepts(i)) shown_.push_back(all_[i]);
        std::vector<std::vector<std::string>> rows, keys;
        std::vector<std::string> states;
        for (const auto& d : shown_) {
            rows.push_back(cellsOf(d));
            keys.push_back({"", "", "", "", "", "", padKey(d.requestedAt), padKey(d.activatedAt), padKey(d.expiresAt ? d.expiresAt : 99999999999LL), ""});
            states.push_back(d.state);
        }
        model_->set(std::move(rows), std::move(keys), [states](RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (r >= states.size()) return s;
            if (c == 1) s.monospace = true;
            if (c == 9) {
                s.fgTone = stateTone(states[r]);
                s.bold = true;
            }
            return s;
        });
        invalidate();
    }

    // ---- 1.47.0 (planche 06) : le clic droit, dans l'ordre de la regle ----
    ui::ListMenu menuFor() {
        const auto rows = table_->selectedModelRows();
        ui::ListMenu m({rows.size(), "PC", false});
        if (rows.empty()) return m;
        m.unfold(*table_);
        const KnownDevice* d = rows.front() < shown_.size() ? &shown_[rows.front()] : nullptr;
        if (rows.size() == 1 && d) {
            if (!d->licenseId.empty() && ws().licence(d->licenseId)) {
                const std::string id = d->licenseId;
                m.goTo({.label = "Sa licence " + id, .icon = ui::Icon::Document, .run = [this, id] {
                            requestPageFocus("licences", id);
                            host_.navigate("licences");
                        }});
            }
            if (!d->customer.empty()) {
                const std::string c = d->customer;
                m.goTo({.label = "Le client : " + c, .icon = ui::Icon::User, .run = [this, c] {
                            requestPageFocus("clients", "client:" + c);
                            host_.navigate("clients");
                        }});
            }
            const auto on = [&](std::size_t f, const std::string& label, const std::string& v) {
                if (!v.empty()) m.filterOn({.label = label + " : " + v, .run = [this, f, v] { bar_->filterOn(f, v); }});
            };
            on(2, "Ce client", d->customer);
            on(3, "Cette licence", d->licenseId);
            on(4, "Cette offre", d->edition);
            on(5, "Ce logiciel", d->software);
            on(9, "Cet \xC3\xA9tat", stateLabel(d->state));
            if (d->activatedAt > 0) {
                const std::int64_t when = listkit::seconds(d->activatedAt);
                m.filterOn({.label = "Ce jour : " + dateFr(d->activatedAt), .run = [this, when] { bar_->filterOnDay(7, when); }});
            }
            m.copy("L'empreinte", d->fingerprint, ui::Icon::Copy);
            m.copy("La cl\xC3\xA9 du PC", d->deviceKey, ui::Icon::Lock);
            if (!d->licenseId.empty()) m.copy("La licence", d->licenseId, ui::Icon::Document);
        }
        std::string lines;
        std::vector<KnownDevice> list;
        for (const RowIndex r : rows) {
            if (r >= shown_.size()) continue;
            std::string line;
            const auto cells = cellsOf(shown_[r]);
            for (std::size_t c = 0; c < cells.size(); ++c) line += (c ? "\t" : "") + cells[c];
            lines += line + "\n";
            list.push_back(shown_[r]);
        }
        m.copy("La ligne", lines, ui::Icon::Copy, "Copier les %n lignes", lines);
        m.exportRows([this, list] { exportList(list); });
        return m;
    }

    // ---- 1.47.0 (planche 07) : la ligne depliee - le client, la licence, qui l'a signee ----
    ui::WidgetPtr unfoldFor(RowIndex r) {
        if (r >= shown_.size()) return nullptr;
        const KnownDevice d = shown_[r];
        auto card = std::make_unique<ui::UnfoldCard>("activations.depliage");
        ui::UnfoldCard::Box b;   // a plat (planche 07 : les Activations)
        std::string head = "**" + (d.customer.empty() ? std::string("(client inconnu)") : d.customer) + "**";
        if (!d.fingerprint.empty()) head += "  " + d.fingerprint;
        if (!d.software.empty()) head += " \xC2\xB7 " + d.software;
        b.lines.push_back({.icon = ui::Icon::User, .text = head});
        const Seconds now = ws().now();
        if (d.requestedAt) b.lines.push_back({.icon = ui::Icon::History, .text = "demand\xC3\xA9 " + whenText(now, d.requestedAt)});
        if (!d.licenseId.empty()) {
            const std::string id = d.licenseId;
            std::string t = (d.activatedAt ? "activ\xC3\xA9 " + whenText(now, d.activatedAt) + ", " : std::string()) + "licence **" + d.licenseId + "**";
            if (!d.edition.empty()) t += " (" + d.edition + ")";
            ui::UnfoldCard::Line l{.icon = ui::Icon::Document, .text = t};
            if (ws().licence(id))
                l.link = [this, id] {
                    requestPageFocus("licences", id);
                    host_.navigate("licences");
                };
            if (const auto who = who_.last({"licence_creer", "licence_prolonger", "licence_option"}, d.licenseId)) {
                l.text += ", sign\xC3\xA9" "e par";
                l.person = listkit::person(host_, who->key, who->who);
            }
            b.lines.push_back(std::move(l));
        }
        std::string st = "\xC3\xA9tat : **" + stateLabel(d.state) + "**";
        if (d.expiresAt) st += " \xC2\xB7 fin le " + dateFr(d.expiresAt);
        if (d.revokedAt) st += " \xC2\xB7 r\xC3\xA9voqu\xC3\xA9 le " + dateFr(d.revokedAt);
        b.lines.push_back({.icon = ui::Icon::Info, .text = st, .tone = stateTone(d.state)});
        card->addBox(std::move(b));
        return card;
    }

    static std::string whenText(Seconds now, Seconds t) {
        if (t <= 0) return {};
        const Seconds dd = now - t;
        if (dd >= 0 && dd < 86400) return "aujourd'hui";
        if (dd >= 86400 && dd < 2 * 86400) return "hier";
        return "le " + dateFr(t);
    }

    void exportList(const std::vector<KnownDevice>& list) {
        if (list.empty() || host_.busy()) return;
        ui::FilePick pick;
        pick.title = "Exporter les activations pour Excel";
        pick.filters = {{"Tableau pour Excel (.csv)", "csv"}};
        pick.start = "activations.csv";
        pick.save = true;
        ShellHost host = host_;
        host_.pickFile(pick, [list, host](std::string path) {
            if (path.empty()) return;
            const auto dot = path.find_last_of("./\\");
            if (dot == std::string::npos || path[dot] != '.') path += ".csv";
            host.report(host.workspace().exportCsv(list, path));
        });
    }

    void pickRequest() {
        if (host_.busy()) {
            if (host_.flash) host_.flash(kBusy, Tone::Warning);
            return;
        }
        ui::FilePick pick;
        pick.title = "Choisis la demande du client (.xpgreq)";
        pick.filters = {{"Demandes d'activation (.xpgreq)", "xpgreq"}, {"Tous les fichiers", "*"}};
        std::weak_ptr<bool> alive = life_;
        ShellHost host = host_;
        host_.pickFile(pick, [this, alive, host](std::string path) {
            if (path.empty()) return;
            if (host.busy()) {
                if (host.flash) host.flash(kBusy, Tone::Warning);
                return;
            }
            host.report(host.workspace().recordRequests({path}));
            if (!alive.expired()) refresh();
        });
    }

    void exportCsv() {
        if (host_.busy()) {
            if (host_.flash) host_.flash(kBusy, Tone::Warning);
            return;
        }
        if (shown_.empty()) {
            if (host_.flash) host_.flash("Aucun PC \xC3\xA0 exporter (la recherche ou le filtre n'en montre aucun).", Tone::Warning);
            return;
        }
        ui::FilePick pick;
        pick.title = "Exporter les activations pour Excel";
        pick.filters = {{"Tableau pour Excel (.csv)", "csv"}};
        pick.start = "activations.csv";
        pick.save = true;
        const std::vector<KnownDevice> list = shown_;   // ce qui est montre au moment du clic
        ShellHost host = host_;
        host_.pickFile(pick, [list, host](std::string path) {
            if (path.empty()) return;
            if (host.busy()) {
                if (host.flash) host.flash(kBusy, Tone::Warning);
                return;
            }
            const auto dot = path.find_last_of("./\\");
            if (dot == std::string::npos || path[dot] != '.') path += ".csv";
            host.report(host.workspace().exportCsv(list, path));
        });
    }

    std::shared_ptr<bool>    life_{std::make_shared<bool>(true)};
    core::ConnectionScope    links_;
    ui::Button*              record_{nullptr};
    ui::Button*              export_{nullptr};
    ui::Button*              folder_{nullptr};
    ui::FilterBar*           bar_{nullptr};                         // 1.47.0
    std::vector<std::vector<std::string>> texts_;                  // 1.47.0 : les cases de chaque PC (la barre)
    listkit::WhoDid          who_{host_.workspace()};               // 1.47.0 : qui a signe la licence
    ui::TableView*           table_{nullptr};
    std::shared_ptr<Rows>    model_;
    std::vector<KnownDevice> all_, shown_;
    std::vector<std::string> leadLines_;
    float                    titleY_{0}, leadY_{0}, noteY_{0}, emptyY_{0};
    gfx::Rect                totalsRect_{};
};

} // namespace

PagePtr makeActivationsPage(ShellHost host) { return std::make_unique<ActivationsPage>(std::move(host)); }

} // namespace license_admin::gui
