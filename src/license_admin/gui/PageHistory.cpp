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
// =============================================================================
#include "Page.hpp"

#include "../../ui/TextSearch.hpp"
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
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace license_admin::gui {

// Le CSV de l'historique (ce que montre la page) - l'export, et les essais.
std::string historyCsv(const std::vector<ActivationRecord>& records);

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

// Le genre d'une ligne, a l'ecran : Demande, Licence, Extension, Revocation ; 1.44.0 : Client supprime, Client restaure.
std::string typeOf(const ActivationRecord& a) {
    if (a.kind == "demande") return "Demande";
    if (a.kind == "licence") return a.extension ? "Extension" : "Licence";
    if (a.kind == "revocation") return "R\xC3\xA9vocation";
    if (a.kind == "client") return a.action == "restaure" ? "Client restaur\xC3\xA9" : "Client supprim\xC3\xA9";
    return a.kind;
}

// Le filtre : "", "demande", "licence", "extension", "revocation", "client" (1.44.0).
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
        type_ = add(*this, std::make_unique<ui::DropDown>("historique.type"));
        type_->setItems({{"Tout", ""}, {"Demandes", "demande"}, {"Licences", "licence"}, {"Extensions", "extension"}, {"R\xC3\xA9vocations", "revocation"}, {"Clients", "client"}});
        type_->setSelectedIndex(0);
        search_ = add(*this, std::make_unique<ui::InputText>("historique.recherche"));
        search_->setPlaceholder("Chercher une licence, un client, un PC\xE2\x80\xA6");
        search_->setEscapeClears(true);
        export_ = add(*this, std::make_unique<ui::Button>("Exporter\xE2\x80\xA6", "historique.exporter"));
        export_->setIcon(ui::Icon::Export);
        export_->setTooltip("Les lignes montr\xC3\xA9" "es (filtre et recherche compris), en CSV pour Excel.");
        table_ = add(*this, std::make_unique<ui::TableView>("historique.table"));
        table_->setColumns({{"Date", 150.f}, {"Type", 116.f}, {"Licence", 140.f}, {"Client", 170.f}, {"PC", 160.f}, {"D\xC3\xA9tail", 520.f}});
        table_->setSelectionMode(ui::SelectionMode::Extended);
        model_ = std::make_shared<Rows>(std::vector<std::string>{"Date", "Type", "Licence", "Client", "PC", "D\xC3\xA9tail"});
        table_->setModel(model_);
        links_ += type_->selectionChanged->connect([this](int) { rebuild(); });
        links_ += search_->textChanged->connect([this](const std::string&) { rebuild(); });
        links_ += export_->clicked->connect([this] { exportCsv(); });
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
        const float tw = 170.f, sw = std::min(340.f, w * 0.36f), ew = export_->sizeHint().preferred.w;
        type_->setBounds({x, y + 2.f, tw, kFieldH});
        search_->setBounds({x + tw + kGap, y + 2.f, sw, kFieldH});
        export_->setBounds({x + w - ew, y, ew, kButtonH});
        countRect_ = {x + tw + sw + 2.f * kGap + 4.f, y, std::max(0.f, w - tw - sw - ew - 3.f * kGap - 4.f), kButtonH};
        y += kButtonH + kGap;
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
        if (shown_.size() != total_) n += " sur " + std::to_string(total_);
        if (hidden_) n += " - " + count(hidden_, "r\xC3\xA9vocation r\xC3\xA9p\xC3\xA9t\xC3\xA9" "e masqu\xC3\xA9" "e", "r\xC3\xA9vocations r\xC3\xA9p\xC3\xA9t\xC3\xA9" "es masqu\xC3\xA9" "es") + " (la liste re-sign\xC3\xA9" "e enti\xC3\xA8re)";
        r.drawText({countRect_.x, countRect_.y + (kButtonH - r.lineHeight(ctx.theme.font.ui)) * 0.5f}, fit(r, n, ctx.theme.font.ui, countRect_.w), ctx.theme.font.ui,
                   c.textMuted);
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (!shown_.empty()) return;
        auto& r = ctx.r;
        const gfx::Rect t = table_->bounds();
        const std::string line = records_.empty() ? "Rien encore : l'historique se remplit avec les demandes not\xC3\xA9" "es, les licences sign\xC3\xA9" "es et les r\xC3\xA9vocations."
                                                  : "Aucune ligne ne correspond au filtre ou \xC3\xA0 la recherche.";
        r.drawText({t.x + 16.f, emptyY_}, fit(r, line, ctx.theme.font.ui, t.w - 32.f), ctx.theme.font.ui, ctx.theme.color.textMuted);
    }

private:
    static constexpr const char* kLead =
        "Tout ce que ce poste a not\xC3\xA9 dans le registre des activations, le plus r\xC3\xA9" "cent d'abord : demandes re\xC3\xA7ues, licences sign\xC3\xA9" "es "
        "(une ligne par PC li\xC3\xA9), extensions, r\xC3\xA9vocations, clients supprim\xC3\xA9s ou restaur\xC3\xA9s. Rien n'en est jamais effac\xC3\xA9.";

    // La ligne telle que la page la montre (le client et le nom du PC completes).
    [[nodiscard]] ActivationRecord completed(const ActivationRecord& a) const {
        ActivationRecord x = a;
        if (x.customer.empty())
            if (auto it = customerOf_.find(x.licenseId); it != customerOf_.end()) x.customer = it->second;
        if (x.deviceName.empty() && !x.deviceKey.empty())
            if (auto it = nameOf_.find(x.deviceKey); it != nameOf_.end()) x.deviceName = it->second;
        return x;
    }

    void rebuild() {
        const auto* item = type_->selectedItem();
        const std::string want = item ? item->value : std::string();
        const ui::SearchQuery q(search_->text());
        const auto ordered = newestFirst(records_, &hidden_);
        total_ = ordered.size();
        shown_.clear();
        for (const ActivationRecord* p : ordered) {
            if (!want.empty() && filterKey(*p) != want) continue;
            ActivationRecord a = completed(*p);
            if (!q.empty() && !q.matches({a.licenseId, a.customer, a.deviceName, a.fingerprint, a.edition, a.software, a.reason, typeOf(a), detailOf(a)})) continue;
            shown_.push_back(std::move(a));
        }
        std::vector<std::vector<std::string>> rows, keys;
        std::vector<std::string> kinds;
        for (std::size_t i = 0; i < shown_.size(); ++i) {
            const auto& a = shown_[i];
            rows.push_back({dateTimeFr(a.at), typeOf(a), a.licenseId, a.customer, pcOf(a), detailOf(a)});
            // A la meme heure, l'ordre de la page (le plus recent d'abord) : la cle porte le rang.
            keys.push_back({padKey(a.at) + padKey(static_cast<long long>(shown_.size() - i)), "", "", "", "", ""});
            kinds.push_back(filterKey(a));
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
        invalidate();
    }

    void exportCsv() {
        if (host_.busy()) {
            if (host_.flash) host_.flash(kBusy, Tone::Warning);
            return;
        }
        if (shown_.empty()) {
            if (host_.flash) host_.flash("Aucune ligne \xC3\xA0 exporter (le filtre ou la recherche n'en montre aucune).", Tone::Warning);
            return;
        }
        ui::FilePick pick;
        pick.title = "Exporter l'historique pour Excel";
        pick.filters = {{"Tableau pour Excel (.csv)", "csv"}};
        pick.start = "historique.csv";
        pick.save = true;
        const std::vector<ActivationRecord> list = shown_;
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

    core::ConnectionScope              links_;
    ui::DropDown*                      type_{nullptr};
    ui::InputText*                     search_{nullptr};
    ui::Button*                        export_{nullptr};
    ui::TableView*                     table_{nullptr};
    std::shared_ptr<Rows>              model_;
    std::vector<ActivationRecord>      records_, shown_;
    std::map<std::string, std::string> customerOf_, nameOf_;
    std::size_t                        total_{0}, hidden_{0};
    std::vector<std::string>           leadLines_;
    float                              titleY_{0}, leadY_{0}, emptyY_{0};
    gfx::Rect                          countRect_{};
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
