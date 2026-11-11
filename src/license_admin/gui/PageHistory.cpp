// =============================================================================
//  license_admin/gui/PageHistory.cpp - 1.33.0 : LA PAGE Historique
// -----------------------------------------------------------------------------
//  Chaque ligne du registre des activations (activations.jsonl : ajoute a la
//  fin, jamais reecrit), la plus recente d'abord : les demandes recues, les
//  licences signees (une ligne par PC lie), les extensions, les revocations.
//  Filtre par genre, recherche, compte ; << Exporter... >> ecrit ce qui est
//  montre en CSV pour Excel (UTF-8 avec BOM, ; et CRLF ; une case qui commence
//  par = + - @ tabulation ou retour prend une apostrophe devant : Excel la lit
//  comme du texte, jamais comme une formule - la regle de devicesCsv).
//
//  UNE LISTE DE REVOCATION SE RESIGNE ENTIERE : chaque signature note une ligne
//  par revocation qu'elle contient, les anciennes comprises. La page montre une
//  revocation une fois (sa premiere ligne) et dit combien de repetitions sont
//  masquees.
//
//  1.44.0 : un client SUPPRIME ou RESTAURE (Clients) a aussi sa ligne (genre
//  << client >>) : << Client supprime : <nom> (2 licences) >>, et qui l'a fait.
//
//  1.47.0 (planches 06, 07, 12) : la barre << Filtrer et chercher >> remplace la
//  liste des genres et le champ de recherche (historique.recherche reste) : les
//  puces Type, Qui, Periode ; le clic droit dans l'ordre de la regle (Ouvrir la
//  licence, Deplier ; Aller a ; Filtrer sur ; Copier ; Exporter ; rien en rouge :
//  rien ne s'efface de l'historique) ; la ligne depliee (ce qu'elle dit, qui
//  l'a fait, les autres lignes de la meme licence). << Qui >> : un client
//  supprime ou restaure le porte ; une licence, une extension, une revocation :
//  la ligne du journal des admins de la meme action, a la meme heure (une
//  heure d'ecart au plus) ; une demande : personne (le client l'envoie).
// =============================================================================
#include "AdminsKit.hpp"   // 1.47.0 : les dates (<< hier 17:20 >>)
#include "ListKit.hpp"     // 1.47.0 : la barre de filtres, le clic droit, le depliage
#include "Page.hpp"

#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace license_admin::gui {

// Le CSV de l'historique (ce que montre la page) - l'export, et les essais.
std::string historyCsv(const std::vector<ActivationRecord>& records);

namespace {

// 1.48.1 : pourquoi une ligne de l'Historique ne se supprime pas (le bouton grise, le clic droit).
constexpr const char* kNoDelete = "l'Historique ne s'efface pas : c'est la trace de ce qui a \xC3\xA9t\xC3\xA9 sign\xC3\xA9, demand\xC3\xA9, r\xC3\xA9voqu\xC3\xA9 - supprime plut\xC3\xB4t la licence, le PC ou le client";

using RowIndex = ui::RowIndex;

const ui::Fonts kFonts{};
constexpr float kPad = 16.f;
constexpr float kButtonH = 32.f;
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

// Le genre d'une ligne, a l'ecran : Demande, Licence, Extension, Revocation ; 1.44.0 : Client supprime, Client restaure.
std::string typeOf(const ActivationRecord& a) {
    if (a.kind == "demande") return "Demande";
    if (a.kind == "licence") return a.extension ? "Extension" : "Licence";
    if (a.kind == "revocation") return "R\xC3\xA9vocation";
    if (a.kind == "client") return a.action == "restaure" ? "Client restaur\xC3\xA9" : "Client supprim\xC3\xA9";
    return a.kind;
}

// Le genre, pour les couleurs : "demande", "licence", "extension", "revocation", "client" (1.44.0).
std::string filterKey(const ActivationRecord& a) {
    if (a.kind == "licence") return a.extension ? "extension" : "licence";
    return a.kind;
}

std::string pcOf(const ActivationRecord& a) {
    if (!a.deviceName.empty()) return a.deviceName;
    if (a.kind == "revocation" && a.deviceKey.empty()) return "toute la licence";
    if (!a.fingerprint.empty()) return "(" + a.fingerprint + ")";
    return {};
}

std::string detailOf(const ActivationRecord& a) {
    std::vector<std::string> parts;
    if (a.kind == "demande") {
        if (!a.software.empty()) parts.push_back(a.software);
        if (!a.licenseId.empty()) parts.push_back("licence demand\xC3\xA9" "e " + a.licenseId);
        if (!a.fingerprint.empty()) parts.push_back("empreinte " + a.fingerprint);
        if (a.requestedAt) parts.push_back("faite le " + dateFr(a.requestedAt));
    } else if (a.kind == "licence") {
        std::string ed = a.edition.empty() ? std::string() : "\xC3\xA9" "dition " + a.edition;
        licensing::LicenseType t{};
        if (!a.type.empty() && licensing::parseLicenseType(a.type, t)) ed += (ed.empty() ? "" : " ") + std::string("(") + licensing::licenseTypeLabel(t) + ")";
        if (!ed.empty()) parts.push_back(ed);
        parts.push_back(a.expiresAt ? "fin le " + dateFr(a.expiresAt) : std::string("sans fin"));
        if (a.activatedAt) parts.push_back("PC activ\xC3\xA9 le " + dateFr(a.activatedAt));
        if (!a.software.empty()) parts.push_back(a.software);
    } else if (a.kind == "client") {   // 1.44.0
        std::string head = (a.action == "restaure" ? "Client restaur\xC3\xA9 : " : "Client supprim\xC3\xA9 : ") + a.customer;
        if (!a.licences.empty()) head += " (" + count(a.licences.size(), "licence", "licences") + ")";
        parts.push_back(head);
        if (!a.licences.empty()) {
            std::string ids;
            for (const auto& id : a.licences) ids += (ids.empty() ? "" : ", ") + id;
            parts.push_back(ids);
        }
        if (!a.by.empty()) parts.push_back("par " + a.by);
    } else if (a.kind == "revocation") {
        if (!a.deviceKey.empty()) parts.push_back("un PC" + (a.fingerprint.empty() ? std::string() : " (" + a.fingerprint + ")"));
        if (!a.reason.empty()) parts.push_back("raison : " + a.reason);
        if (a.issuedBefore) parts.push_back("les \xC3\xA9missions sign\xC3\xA9" "es avant le " + dateFr(a.issuedBefore));
        if (a.revokedAt && a.revokedAt != a.at) parts.push_back("r\xC3\xA9voqu\xC3\xA9" "e le " + dateFr(a.revokedAt));
    }
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : ", ") + p;
    return out;
}

// Une cellule du CSV (la regle de devicesCsv : une formule devient du texte ; ; " et les retours entre guillemets).
std::string csvField(const std::string& raw) {
    const std::string x = licensing::looksLikeFormula(raw) && raw != "-" ? "'" + raw : raw;
    if (x.find_first_of(";\"\r\n") == std::string::npos) return x;
    std::string q = "\"";
    for (char c : x) q += c == '"' ? std::string("\"\"") : std::string(1, c);
    return q + "\"";
}

// Les lignes, la plus recente d'abord (a la meme heure : la derniere du fichier d'abord) ; une revocation repetee
// (la liste re-signee entiere) n'est gardee qu'une fois - sa premiere ligne. `hidden` : combien sont masquees.
std::vector<const ActivationRecord*> newestFirst(const std::vector<ActivationRecord>& records, std::size_t* hidden) {
    std::vector<std::size_t> order(records.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return records[x].at < records[y].at; });
    std::set<std::tuple<std::string, std::string, licensing::Seconds, std::string, licensing::Seconds>> seen;
    std::vector<const ActivationRecord*> kept;
    std::size_t masked = 0;
    for (std::size_t i : order) {   // du plus ancien au plus recent : la premiere ligne d'une revocation est gardee
        const ActivationRecord& a = records[i];
        if (a.kind == "revocation" && !seen.insert({a.licenseId, a.deviceKey, a.revokedAt, a.reason, a.issuedBefore}).second) {
            ++masked;
            continue;
        }
        kept.push_back(&a);
    }
    std::reverse(kept.begin(), kept.end());
    if (hidden) *hidden = masked;
    return kept;
}

// =================================================================================== la page
class HistoryPage final : public Page {
public:
    explicit HistoryPage(ShellHost host) : Page("page.historique", std::move(host)) {
        export_ = add(*this, std::make_unique<ui::Button>("Exporter\xE2\x80\xA6", "historique.exporter"));
        export_->setIcon(ui::Icon::Export);
        export_->setTooltip("Les lignes montr\xC3\xA9" "es (filtres et recherche compris), en CSV pour Excel.");
        // 1.48.1 : Supprimer est la, grise (la raison dans son infobulle) ; Actualiser relit l'archive et le registre.
        delete_ = add(*this, std::make_unique<ui::Button>("Supprimer\xE2\x80\xA6", "historique.supprimer"));
        delete_->setStyle(ui::Button::Style::Danger);
        delete_->setIcon(ui::Icon::Delete);
        delete_->setEnabled(false);
        delete_->setTooltip(kNoDelete);
        reload_ = add(*this, std::make_unique<ui::Button>("Actualiser", "historique.actualiser"));
        reload_->setIcon(ui::Icon::Refresh);
        reload_->setTooltip("Relit tout de suite le registre des activations et l'archive (F5)");
        table_ = add(*this, std::make_unique<ui::TableView>("historique.table"));
        table_->setColumns({{"Date", 150.f}, {"Type", 116.f}, {"Licence", 140.f}, {"Client", 170.f}, {"PC", 160.f}, {"D\xC3\xA9tail", 520.f}});
        table_->setSelectionMode(ui::SelectionMode::Extended);
        // La 7e colonne du modele (pas montree) : la place de la ligne dans le registre - le depliage la suit.
        model_ = std::make_shared<Rows>(std::vector<std::string>{"Date", "Type", "Licence", "Client", "PC", "D\xC3\xA9tail", "Ligne"});
        table_->setModel(model_);
        // 1.47.0 (planche 12) : LA BARRE - chercher (historique.recherche, comme avant), le type, qui, la periode.
        bar_ = add(*this, std::make_unique<ui::FilterBar>("historique", barFields()));
        bar_->setNoun("lignes");
        bar_->setChipOrder({1, 6, 0});   // 1.47.0 (planches) : Type, Qui, Periode - l'ordre des puces, pas celui des colonnes
        bar_->setClock([this] { return listkit::seconds(ws().now()); });
        {
            ui::FilterBar::Source src;
            src.rows = [this] { return all_.size(); };
            src.text = [this](std::size_t r, std::size_t f) { return r < texts_.size() && f < texts_[r].size() ? texts_[r][f] : std::string(); };
            src.time = [this](std::size_t r, std::size_t f) -> std::int64_t { return r < all_.size() && f == 0 ? listkit::seconds(all_[r].a.at) : 0; };
            src.extra = [this](std::size_t r) {
                if (r >= all_.size()) return std::string();
                const auto& a = all_[r].a;
                return a.fingerprint + " " + a.edition + " " + a.software + " " + a.reason + " " + a.deviceKey;
            };
            bar_->setSource(std::move(src));
        }
        bar_->linkTable(*table_);
        bar_->setMemoryKey("historique");
        links_ += bar_->changed->connect([this] { rebuild(); });
        links_ += ui::ListMenu::bind(*table_, [this] { return menuFor(); });                       // 1.47.0 (planche 06)
        table_->setUnfold([this](RowIndex r) { return unfoldFor(r); }, 6);                          // (planche 07)
        table_->setEnterActivates(true);
        links_ += table_->activated->connect([this](RowIndex r) {   // Entree, le double-clic : la licence (un client : sa fiche)
            if (const Line* l = lineAt(r)) openOf(l->a);
        });
        links_ += export_->clicked->connect([this] { exportCsv(); });
        links_ += reload_->clicked->connect([this] {
            if (host_.report) host_.report(OpResult::done("Historique relu : le registre des activations et l'archive."));
            else {
                ws().reload();
                refresh();
            }
        });
        refresh();
    }

    [[nodiscard]] std::string title() const override { return "Historique"; }

    void refresh() override {
        records_ = ws().records();
        // Le client d'une ligne qui ne le porte pas (une demande, une revocation) : celui de sa licence.
        customerOf_.clear();
        for (const auto& l : ws().licences()) customerOf_[l.licence.licenseId] = l.licence.customer;
        for (const auto& a : records_)
            if (!a.customer.empty() && !a.licenseId.empty() && !customerOf_.count(a.licenseId)) customerOf_[a.licenseId] = a.customer;
        // Le nom d'un PC qu'une ligne ne nomme pas (une revocation d'un PC) : celui que le registre lui connait.
        nameOf_.clear();
        for (const auto& d : ws().devices())
            if (!d.name.empty()) nameOf_[d.deviceKey] = d.name;
        who_.reset();
        all_.clear();
        texts_.clear();
        for (const ActivationRecord* p : newestFirst(records_, &hidden_)) {
            Line l;
            l.at = static_cast<std::size_t>(p - records_.data());
            l.a = completed(*p);
            l.who = whoDid(l.a);
            std::string who = l.who ? l.who->who : std::string();
            if (who.empty() && l.a.kind == "client") who = l.a.by == "admin+" ? plusName() : l.a.by;
            texts_.push_back({dateTimeFr(l.a.at), typeOf(l.a), l.a.licenseId, l.a.customer, pcOf(l.a), detailOf(l.a), who});
            all_.push_back(std::move(l));
        }
        bar_->refreshCounts();
        // 1.47.0 : une autre page demande une licence ou un client (<< Son historique >>) : la barre filtree sur lui.
        if (const std::string f = takePageFocus("historique"); f.rfind("licence:", 0) == 0) {
            bar_->clearAll();
            bar_->filterOn(2, f.substr(8));
        } else if (f.rfind("client:", 0) == 0) {
            bar_->clearAll();
            bar_->filterOn(3, f.substr(7));
        }
        rebuild();
        invalidateLayout();
        invalidate();
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
        const float ew = export_->sizeHint().preferred.w;
        export_->setBounds({x, y, ew, kButtonH});
        const float dw = delete_->sizeHint().preferred.w;   // 1.48.1
        delete_->setBounds({x + ew + kGap, y, dw, kButtonH});
        const float rw = reload_->sizeHint().preferred.w + 8.f;
        reload_->setBounds({x + w - rw, y, rw, kButtonH});
        const float cx = x + ew + dw + 3.f * kGap;
        countRect_ = {cx, y, std::max(0.f, x + w - rw - kGap - cx), kButtonH};
        y += kButtonH + kGap;
        const float bh = bar_->heightFor(w);   // 1.47.0 : la barre de filtres
        bar_->setBounds({x, y + 2.f, w, bh});
        y += bh + kGap + 2.f;
        table_->setBounds({x, y, w, std::max(60.f, b.bottom() - kPad - y)});
        emptyY_ = y + 40.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const gfx::Rect b = bounds();
        r.fillRect(b, c.panelBg);
        const float x = b.x + kPad, w = std::max(200.f, b.w - 2.f * kPad);
        r.drawText({x, titleY_}, "Historique", ctx.theme.font.title, c.text);
        for (std::size_t i = 0; i < leadLines_.size(); ++i)
            r.drawText({x, leadY_ + static_cast<float>(i) * kLineH}, fit(r, leadLines_[i], ctx.theme.font.ui, w), ctx.theme.font.ui, c.textMuted);
        std::string n = count(shown_.size(), "ligne", "lignes");
        if (shown_.size() != all_.size()) n += " sur " + std::to_string(all_.size());
        if (hidden_) n += " - " + count(hidden_, "r\xC3\xA9vocation r\xC3\xA9p\xC3\xA9t\xC3\xA9" "e masqu\xC3\xA9" "e", "r\xC3\xA9vocations r\xC3\xA9p\xC3\xA9t\xC3\xA9" "es masqu\xC3\xA9" "es") + " (la liste re-sign\xC3\xA9" "e enti\xC3\xA8re)";
        r.drawText({countRect_.x, countRect_.y + (kButtonH - r.lineHeight(ctx.theme.font.ui)) * 0.5f}, fit(r, n, ctx.theme.font.ui, countRect_.w), ctx.theme.font.ui,
                   c.textMuted);
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (!shown_.empty()) return;
        auto& r = ctx.r;
        const gfx::Rect t = table_->bounds();
        const std::string line = records_.empty() ? "Rien encore : l'historique se remplit avec les demandes not\xC3\xA9" "es, les licences sign\xC3\xA9" "es et les r\xC3\xA9vocations."
                                                  : "Aucune ligne ne correspond aux filtres ou \xC3\xA0 la recherche.";
        r.drawText({t.x + 16.f, emptyY_}, fit(r, line, ctx.theme.font.ui, t.w - 32.f), ctx.theme.font.ui, ctx.theme.color.textMuted);
    }

private:
    static constexpr const char* kLead =
        "Tout ce que ce poste a not\xC3\xA9 dans le registre des activations, le plus r\xC3\xA9" "cent d'abord : demandes re\xC3\xA7ues, licences sign\xC3\xA9" "es "
        "(une ligne par PC li\xC3\xA9), extensions, r\xC3\xA9vocations, clients supprim\xC3\xA9s ou restaur\xC3\xA9s. Rien n'en est jamais effac\xC3\xA9.";

    struct Line {
        std::size_t                 at{0};   // sa place dans le registre (la cle du depliage, de la selection)
        ActivationRecord            a;       // completee (le client, le nom du PC)
        std::optional<listkit::Did> who;     // la ligne du journal des admins de la meme action
    };

    // 1.47.0 (planche 12) : les colonnes vues par la barre (les 6 du modele), puis qui.
    static std::vector<ui::FilterBar::Field> barFields() {
        using K = ui::FilterBar::Kind;
        std::vector<ui::FilterBar::Field> f(7);
        f[0] = {.title = "Date", .kind = K::Period, .chip = true, .chipLabel = "P\xC3\xA9riode"};
        f[1] = {.title = "Type", .chip = true, .noun = "un type",
                .order = {"Demande", "Licence", "Extension", "R\xC3\xA9vocation", "Client supprim\xC3\xA9", "Client restaur\xC3\xA9"}};
        f[2] = {.title = "Licence", .noun = "une licence"};
        f[3] = {.title = "Client", .noun = "un client"};
        f[4] = {.title = "PC", .kind = K::Text};
        f[5] = {.title = "D\xC3\xA9tail", .kind = K::Text};
        f[6] = {.title = "Qui", .chip = true, .noun = "un admin"};
        return f;
    }

    [[nodiscard]] std::string plusName() const { return ws().key().name.empty() ? std::string("admin+") : ws().key().name; }

    // La ligne telle que la page la montre (le client et le nom du PC completes).
    [[nodiscard]] ActivationRecord completed(const ActivationRecord& a) const {
        ActivationRecord x = a;
        if (x.customer.empty())
            if (auto it = customerOf_.find(x.licenseId); it != customerOf_.end()) x.customer = it->second;
        if (x.deviceName.empty() && !x.deviceKey.empty())
            if (auto it = nameOf_.find(x.deviceKey); it != nameOf_.end()) x.deviceName = it->second;
        return x;
    }

    // Qui l'a fait : la ligne du journal des admins de la meme action sur le meme objet, la plus proche (une heure au plus).
    [[nodiscard]] std::optional<listkit::Did> whoDid(const ActivationRecord& a) const {
        std::vector<std::string> actions;
        std::string object = a.licenseId, start;
        Seconds when = a.at;
        if (a.kind == "licence") {
            actions = a.extension ? std::vector<std::string>{"licence_prolonger", "licence_option", "poste_retirer", "licence_creer"}
                                  : std::vector<std::string>{"licence_creer"};
        } else if (a.kind == "revocation") {
            actions = {"licence_revoquer"};
            start = "a r\xC3\xA9voqu\xC3\xA9";
            if (a.revokedAt) when = a.revokedAt;
        } else if (a.kind == "client") {
            actions = {a.action == "restaure" ? "client_restaurer" : "client_supprimer"};
            object = a.customer;
        } else {
            return std::nullopt;   // une demande : le client l'envoie
        }
        if (object.empty()) return std::nullopt;
        std::optional<listkit::Did> best;
        Seconds gap = 0;
        for (const auto& d : who_.history(object)) {
            if (std::find(actions.begin(), actions.end(), d.action) == actions.end()) continue;
            if (!start.empty() && d.summary.rfind(start, 0) != 0) continue;
            const Seconds g = d.when > when ? d.when - when : when - d.when;
            if (g > 3600 || (best && g >= gap)) continue;
            best = d;
            gap = g;
        }
        return best;
    }

    [[nodiscard]] const Line* lineAt(RowIndex r) const { return r < shown_.size() && shown_[r] < all_.size() ? &all_[shown_[r]] : nullptr; }

    void rebuild() {
        std::vector<std::size_t> keep;   // la selection, retrouvee par la place des lignes dans le registre
        for (const RowIndex r : table_->selectedModelRows())
            if (const Line* l = lineAt(r)) keep.push_back(l->at);
        shown_.clear();
        for (std::size_t i = 0; i < all_.size(); ++i)
            if (bar_->accepts(i)) shown_.push_back(i);
        std::vector<std::vector<std::string>> rows, keys;
        std::vector<std::string> kinds;
        std::vector<RowIndex> again;
        for (std::size_t i = 0; i < shown_.size(); ++i) {
            const Line& l = all_[shown_[i]];
            auto cells = texts_[shown_[i]];
            cells.resize(6);
            cells.push_back("r" + std::to_string(l.at));
            rows.push_back(std::move(cells));
            // A la meme heure, l'ordre de la page (le plus recent d'abord) : la cle porte le rang.
            keys.push_back({padKey(l.a.at) + padKey(static_cast<long long>(shown_.size() - i)), "", "", "", "", "", ""});
            kinds.push_back(filterKey(l.a));
            if (std::find(keep.begin(), keep.end(), l.at) != keep.end()) again.push_back(static_cast<RowIndex>(i));
        }
        model_->set(std::move(rows), std::move(keys), [kinds](RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (c == 1 && r < kinds.size()) {
                s.bold = true;
                s.fgTone = kinds[r] == "demande" ? ui::Tone::Info : kinds[r] == "revocation" || kinds[r] == "client" ? ui::Tone::Error : kinds[r] == "extension" ? ui::Tone::Accent : ui::Tone::Ok;
            }
            if (c == 2) s.monospace = true;
            return s;
        });
        table_->selectModelRows(again, false);
        invalidate();
    }

    void goTo(const std::string& page, const std::string& focus) {
        requestPageFocus(page, focus);
        host_.navigate(page);
    }
    // Ouvrir : la licence de la ligne (dans l'archive), sinon le client (une ligne << client >>).
    void openOf(const ActivationRecord& a) {
        if (!a.licenseId.empty() && ws().licence(a.licenseId)) goTo("licences", a.licenseId);
        else if (a.kind == "client" && !a.customer.empty()) goTo("clients", "client:" + a.customer);
    }

    // ---- 1.47.0 (planche 06) : le clic droit, dans l'ordre de la regle ----
    ui::ListMenu menuFor() {
        const auto rows = table_->selectedModelRows();
        ui::ListMenu m({rows.size(), "lignes", true});
        if (rows.empty()) return m;
        const Line* p = lineAt(rows.front());
        if (rows.size() == 1 && p) {
            const ActivationRecord a = p->a;
            if (!a.licenseId.empty() && ws().licence(a.licenseId)) m.open("Ouvrir la licence", [this, a] { openOf(a); });
            else if (a.kind == "client" && !a.customer.empty()) m.open("Ouvrir le client", [this, a] { openOf(a); });
        }
        m.unfold(*table_);
        if (rows.size() == 1 && p) {
            const Line l = *p;
            const ActivationRecord& a = l.a;
            const std::string id = a.licenseId;
            if (!id.empty() && ws().licence(id)) m.goTo({.label = "La licence " + id, .icon = ui::Icon::Document, .run = [this, id] { goTo("licences", id); }});
            if (!a.customer.empty())
                m.goTo({.label = "Le client : " + a.customer, .icon = ui::Icon::User, .run = [this, c = a.customer] { goTo("clients", "client:" + c); }});
            if (!id.empty()) m.goTo({.label = "Ses PC (Activations)", .icon = ui::Icon::Station, .run = [this, id] { goTo("activations", "licence:" + id); }});
            if (a.kind == "revocation" && !id.empty())
                m.goTo({.label = "La r\xC3\xA9vocation (R\xC3\xA9vocations)", .icon = ui::Icon::Close, .run = [this, id] { goTo("revocations", "licence:" + id); }});
            if (l.who && !l.who->key.empty())
                m.goTo({.label = "Par " + l.who->who, .icon = ui::Icon::User, .run = [this, k = l.who->key] { goTo("admins", "admin:" + k); }});
            if (l.who && l.who->number && ws().role() == Role::Plus)   // le journal : l'admin+ seulement
                m.goTo({.label = "Sa ligne du journal (n\xC2\xB0 " + std::to_string(l.who->number) + ")", .icon = ui::Icon::Task,
                        .run = [this, n = l.who->number] { goTo("journal", "ligne:" + std::to_string(n)); }});
            const auto& t = texts_[shown_[rows.front()]];
            const auto on = [&](std::size_t f, const std::string& label) {
                if (f >= t.size() || t[f].empty()) return;
                const std::string v = t[f];
                m.filterOn({.label = label + " : " + v, .run = [this, f, v] { bar_->filterOn(f, v); }});
            };
            on(1, "Ce type");
            on(2, "Cette licence");
            on(3, "Ce client");
            on(6, "Qui");
            if (a.at > 0) {
                const std::int64_t when = listkit::seconds(a.at);
                m.filterOn({.label = "Ce jour : " + dateFr(a.at), .run = [this, when] { bar_->filterOnDay(0, when); }});
            }
            if (!id.empty()) m.copy("La licence", id, ui::Icon::Document);
            if (!a.fingerprint.empty()) m.copy("L'empreinte du PC", a.fingerprint, ui::Icon::Copy);
            if (!a.deviceKey.empty()) m.copy("La cl\xC3\xA9 du PC", a.deviceKey, ui::Icon::Lock);
        }
        std::string lines;
        std::vector<ActivationRecord> list;
        for (const RowIndex r : rows) {
            const Line* l = lineAt(r);
            if (!l) continue;
            const auto& t = texts_[shown_[r]];
            std::string line;
            for (std::size_t c = 0; c < t.size(); ++c) line += (c ? "\t" : "") + t[c];
            lines += line + "\n";
            list.push_back(l->a);
        }
        m.copy("La ligne", lines, ui::Icon::Copy, "Copier les %n lignes", lines);
        m.exportRows([this, list] { exportList(list); }, host_.busy && host_.busy() ? std::string("un travail est en cours") : std::string());
        m.danger({.label = "Supprimer\xE2\x80\xA6", .many = "Supprimer les %n lignes\xE2\x80\xA6", .icon = ui::Icon::Delete, .why = kNoDelete});   // 1.48.1 : jamais
        return m;
    }

    // ---- 1.47.0 (planche 07) : la ligne depliee - ce qu'elle dit, qui l'a fait, la meme licence ----
    ui::WidgetPtr unfoldFor(RowIndex r) {
        const Line* p = lineAt(r);
        if (!p) return nullptr;
        const Line l = *p;
        const ActivationRecord& a = l.a;
        const Seconds now = ws().now();
        const std::string id = a.licenseId;
        const bool inArchive = !id.empty() && ws().licence(id) != nullptr;
        auto card = std::make_unique<ui::UnfoldCard>("historique.depliage");
        ui::UnfoldCard::Box box;
        box.title = "LA LIGNE";
        const auto licenceLine = [&](const std::string& prefix) {
            ui::UnfoldCard::Line x{.icon = ui::Icon::Document, .text = prefix + "**" + id + "**" + (a.edition.empty() ? std::string() : " \xC2\xB7 " + a.edition)};
            if (inArchive) x.link = [this, id] { goTo("licences", id); };
            box.lines.push_back(std::move(x));
        };
        const auto customerLine = [&] {
            if (a.customer.empty()) return;
            box.lines.push_back({.icon = ui::Icon::User, .text = a.customer, .link = [this, c = a.customer] { goTo("clients", "client:" + c); }});
        };
        const auto pcLine = [&](const std::string& prefix) {
            if (a.deviceName.empty() && a.fingerprint.empty()) return;
            std::string t = prefix + "**" + (a.deviceName.empty() ? a.fingerprint : a.deviceName) + "**";
            if (!a.deviceName.empty() && !a.fingerprint.empty()) t += " (" + a.fingerprint + ")";
            if (!a.software.empty()) t += " \xC2\xB7 " + a.software;
            box.lines.push_back({.icon = ui::Icon::Screen, .text = t});
        };
        if (a.kind == "demande") {
            pcLine("le PC ");
            customerLine();
            if (!id.empty()) licenceLine("demande la licence ");
            box.lines.push_back({.icon = ui::Icon::History, .text = "faite " + kit::whenFr(now, a.requestedAt ? a.requestedAt : a.at) + ", not\xC3\xA9" "e " + kit::whenFr(now, a.at)});
        } else if (a.kind == "licence") {
            licenceLine(a.extension ? "\xC3\xA9tendue : " : "sign\xC3\xA9" "e : ");
            customerLine();
            pcLine("pour le PC ");
            box.lines.push_back({.icon = ui::Icon::History,
                                 .text = (a.expiresAt ? "fin le " + dateFr(a.expiresAt) : std::string("sans fin")) + (a.activatedAt ? " \xC2\xB7 PC activ\xC3\xA9 le " + dateFr(a.activatedAt) : std::string())});
        } else if (a.kind == "revocation") {
            licenceLine("r\xC3\xA9voqu\xC3\xA9" "e : ");
            customerLine();
            if (a.deviceKey.empty()) box.lines.push_back({.icon = ui::Icon::Lock, .text = "**toute la licence**", .tone = ui::Tone::Error});
            else pcLine("le PC ");
            if (!a.reason.empty()) box.lines.push_back({.icon = ui::Icon::Comment, .text = "raison : **" + a.reason + "**"});
            if (a.issuedBefore) box.lines.push_back({.icon = ui::Icon::Layers, .text = "les \xC3\xA9missions sign\xC3\xA9" "es avant le " + dateTimeFr(a.issuedBefore)});
        } else if (a.kind == "client") {
            box.lines.push_back({.icon = ui::Icon::User, .text = (a.action == "restaure" ? "restaur\xC3\xA9 : **" : "supprim\xC3\xA9 : **") + a.customer + "**",
                                 .tone = a.action == "restaure" ? ui::Tone::Ok : ui::Tone::Error,
                                 .link = [this, c = a.customer] { goTo("clients", "client:" + c); }});
            if (!a.licences.empty()) {
                std::string ids;
                for (const auto& x : a.licences) ids += (ids.empty() ? "" : ", ") + x;
                box.lines.push_back({.icon = ui::Icon::Document, .text = count(a.licences.size(), "licence", "licences") + " : " + ids});
            }
        }
        // Qui : la pastille de l'admin (le journal), sinon le nom que la ligne porte.
        ui::UnfoldCard::Line when{.icon = ui::Icon::History, .text = "not\xC3\xA9" "e le " + dateTimeFr(a.at)};
        if (a.kind == "client" && !a.by.empty() && !l.who) when.text += ", par " + (a.by == "admin+" ? plusName() : a.by);
        else if (a.kind == "demande") when.text += ", envoy\xC3\xA9" "e par le client";
        box.lines.push_back(std::move(when));
        if (l.who)   // qui : sa pastille (> sa fiche), sur sa ligne
            box.lines.push_back({.icon = ui::Icon::User, .text = "par", .person = listkit::person(host_, l.who->key, l.who->who)});
        card->addBox(std::move(box));

        // La meme licence : ses autres lignes du registre, les plus recentes (cinq au plus).
        if (!id.empty()) {
            ui::UnfoldCard::Box story;
            story.title = "LA M\xC3\x8AME LICENCE";
            story.timeline = true;
            story.weight = 1.3f;
            std::size_t more = 0;
            for (const auto& o : all_) {
                if (o.a.licenseId != id || o.at == l.at) continue;
                if (story.lines.size() == 5) {
                    ++more;
                    continue;
                }
                std::string t = typeOf(o.a);
                if (const std::string pc = pcOf(o.a); !pc.empty()) t += " \xC2\xB7 " + pc;
                story.lines.push_back({.time = kit::whenFr(now, o.a.at), .text = t});
            }
            if (more) story.lines.push_back({.text = "et " + count(more, "ligne", "lignes") + " de plus"});
            if (!story.lines.empty()) card->addBox(std::move(story));
        }
        if (inArchive) card->addButton("Ouvrir la licence", "historique.depliage.licence", ui::Icon::GoTo, [this, id] { goTo("licences", id); });
        if (!id.empty()) card->addButton("Ses PC", "historique.depliage.pc", ui::Icon::Station, [this, id] { goTo("activations", "licence:" + id); });
        if (!a.customer.empty())
            card->addButton("Le client", "historique.depliage.client", ui::Icon::User, [this, c = a.customer] { goTo("clients", "client:" + c); });
        card->addText("clic droit : le menu", true);
        return card;
    }

    void exportCsv() {
        if (host_.busy()) {
            if (host_.flash) host_.flash(kBusy, Tone::Warning);
            return;
        }
        if (shown_.empty()) {
            if (host_.flash) host_.flash("Aucune ligne \xC3\xA0 exporter (les filtres ou la recherche n'en montrent aucune).", Tone::Warning);
            return;
        }
        std::vector<ActivationRecord> list;
        for (const std::size_t i : shown_) list.push_back(all_[i].a);
        exportList(list);
    }

    // Ces lignes en CSV pour Excel (l'explorateur, puis le fichier ; le resultat au bandeau).
    void exportList(const std::vector<ActivationRecord>& list) {
        if (list.empty()) return;
        if (host_.busy()) {
            if (host_.flash) host_.flash(kBusy, Tone::Warning);
            return;
        }
        ui::FilePick pick;
        pick.title = "Exporter l'historique pour Excel";
        pick.filters = {{"Tableau pour Excel (.csv)", "csv"}};
        pick.start = "historique.csv";
        pick.save = true;
        ShellHost host = host_;
        host_.pickFile(pick, [list, host](std::string path) {
            if (path.empty()) return;
            if (host.busy()) {
                if (host.flash) host.flash(kBusy, Tone::Warning);
                return;
            }
            const auto dot = path.find_last_of("./\\");
            if (dot == std::string::npos || path[dot] != '.') path += ".csv";
            const std::string text = historyCsv(list);
            std::ofstream out(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary | std::ios::trunc);
            if (out) out.write(text.data(), static_cast<std::streamsize>(text.size()));
            out.close();
            if (!out) {
                host.report(OpResult::fail("impossible d'\xC3\xA9" "crire " + path));
                return;
            }
            OpResult r = OpResult::done("Historique \xC3\xA9" "crit pour Excel : " + path + " (" + count(list.size(), "ligne", "lignes") + ").");
            r.output = path;
            host.report(r);
        });
    }

    core::ConnectionScope                 links_;
    ui::Button*                           export_{nullptr};
    ui::Button*                           delete_{nullptr};   // 1.48.1 : grise (kNoDelete)
    ui::Button*                           reload_{nullptr};   // 1.48.1 : Actualiser
    ui::FilterBar*                        bar_{nullptr};            // 1.47.0
    ui::TableView*                        table_{nullptr};
    std::shared_ptr<Rows>                 model_;
    std::vector<ActivationRecord>         records_;
    std::vector<Line>                     all_;                     // 1.47.0 : les lignes (sans les repetitions), la plus recente d'abord
    std::vector<std::vector<std::string>> texts_;                   // ... leurs cases (la barre), puis qui
    std::vector<std::size_t>              shown_;                   // ... celles que la barre garde (ligne du modele -> all_)
    listkit::WhoDid                       who_{host_.workspace()};  // 1.47.0 : qui l'a fait
    std::map<std::string, std::string>    customerOf_, nameOf_;
    std::size_t                           hidden_{0};
    std::vector<std::string>              leadLines_;
    float                                 titleY_{0}, leadY_{0}, emptyY_{0};
    gfx::Rect                             countRect_{};
};

} // namespace

std::string historyCsv(const std::vector<ActivationRecord>& records) {
    std::string out = "\xEF\xBB\xBF";   // UTF-8 avec BOM : Excel le lit comme tel
    const auto line = [&](const std::vector<std::string>& cells) {
        for (std::size_t i = 0; i < cells.size(); ++i) out += (i ? ";" : "") + csvField(cells[i]);
        out += "\r\n";
    };
    line({"Date", "Type", "Licence", "Client", "PC", "Empreinte", "D\xC3\xA9tail"});
    for (const auto& a : records)
        line({dateTimeFr(a.at), typeOf(a), a.licenseId, a.customer, a.kind == "revocation" && a.deviceKey.empty() ? std::string("toute la licence") : a.deviceName,
              a.fingerprint, detailOf(a)});
    return out;
}

PagePtr makeHistoryPage(ShellHost host) { return std::make_unique<HistoryPage>(std::move(host)); }

} // namespace license_admin::gui
