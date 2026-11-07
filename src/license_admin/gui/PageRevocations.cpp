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
// =============================================================================
#include "Page.hpp"

#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
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
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
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

class Rows final : public ui::ITableModel {
public:
    using Style = std::function<ui::CellStyle(ui::RowIndex, std::size_t)>;
    Rows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, Style style = {})
        : headers_(std::move(headers)), rows_(std::move(rows)), style_(std::move(style)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string(); }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string();
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override { return style_ ? style_(r, c) : ui::CellStyle{}; }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }

private:
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_;
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
        table_->setSelectionMode(ui::SelectionMode::Single);
        table_->setAlternatingRowColors(true);
        table_->setColumns({{"Licence", 140.f}, {"Client", 170.f}, {"PC", 260.f}, {"Raison", 220.f}, {"R\xC3\xA9voqu\xC3\xA9" "e le", 115.f},
                            {"\xC3\x89missions", 215.f}});
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
        std::vector<std::vector<std::string>> rows;
        std::vector<bool> whole;
        for (const auto& x : list) {
            const LicenceEntry* l = w.licence(x.licenseId);
            std::string pc = "toute la licence";
            if (!x.device.empty()) {
                pc = licdlg::fingerprintOfKey(x.device);
                if (l)
                    if (const auto* d = l->licence.device(x.device); d && !d->name.empty()) pc = d->name + " \xE2\x80\x94 " + pc;
            }
            rows.push_back({x.licenseId, l ? l->licence.customer : std::string("(pas dans l'archive)"), pc, x.reason, dateFr(x.revokedAt),
                            x.issuedBefore ? "avant le " + dateTimeFr(x.issuedBefore) : std::string("toutes")});
            whole.push_back(x.device.empty());
        }
        model_ = std::make_shared<Rows>(
            std::vector<std::string>{"Licence", "Client", "PC", "Raison", "R\xC3\xA9voqu\xC3\xA9" "e le", "\xC3\x89missions"}, std::move(rows),
            [whole](ui::RowIndex r, std::size_t c) {
                ui::CellStyle s;
                if (c == 0) s.monospace = true;
                if (c == 2 && r < whole.size()) {
                    s.icon = whole[r] ? ui::Icon::Lock : ui::Icon::Screen;
                    s.iconTone = ui::Tone::Error;
                }
                return s;
            });
        const auto keep = table_->selectedModelRows();
        table_->setModel(model_);
        if (!keep.empty() && keep.front() < list.size()) table_->selectModelRows({keep.front()}, false);
        updateButtons();
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
        const float top = actionsY_ + 46.f;
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
        if (!ws().revocations().revoked.empty()) return;
        const auto t = table_->bounds();
        const std::string text = "Rien n'est r\xC3\xA9voqu\xC3\xA9. R\xC3\xA9voquer\xE2\x80\xA6 pour retirer une licence ou un PC (fin de contrat, PC remplac\xC3\xA9\xE2\x80\xA6).";
        ctx.r.drawText({t.x + 16.f, t.y + 44.f}, fit(ctx.r, text, ctx.theme.font.ui, t.w - 32.f), ctx.theme.font.ui, ctx.theme.color.textMuted);
    }

private:
    void updateButtons() {
        remove_->setEnabled(!table_->selectedModelRows().empty());
        export_->setEnabled(ws().hasRevocations());
        invalidate();
    }

    void askRemove() {
        const auto sel = table_->selectedModelRows();
        const auto& list = ws().revocations();
        if (sel.empty() || sel.front() >= list.revoked.size()) return;
        if (host_.busy && host_.busy()) {
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
            return;
        }
        const std::size_t index = sel.front();
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
                    host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
                    return;
                }
                host_.report(ws().unrevoke(index));
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

    ui::Button*           revoke_{nullptr};
    ui::Button*           remove_{nullptr};
    ui::Button*           export_{nullptr};
    ui::TableView*        table_{nullptr};
    std::shared_ptr<Rows> model_;
    std::size_t           explainLines_{1};
    float                 statusY_{0.f}, actionsY_{0.f};
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    core::ConnectionScope links_;
};

} // namespace

PagePtr makeRevocationsPage(ShellHost host) { return std::make_unique<RevocationsPage>(std::move(host)); }

} // namespace license_admin::gui
