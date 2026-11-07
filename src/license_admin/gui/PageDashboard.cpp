// =============================================================================
//  license_admin/gui/PageDashboard.cpp - 1.33.0 : LE TABLEAU DE BORD
// -----------------------------------------------------------------------------
//  Ce que disent l'archive et le registre des activations de ce poste, hors
//  ligne (Workspace::dashboard) :
//    - les tuiles : clients, licences actives, expirees, revoquees, PC actives,
//      demandes en attente, celles qui finissent dans 30 jours ; les licences
//      flottantes demandent le service en ligne (grisee : aucun chiffre) ;
//      un clic sur une tuile ouvre la page qui en dit plus ;
//    - les dernieres operations (signees, demandes, revocations) ;
//    - la repartition des licences actives par edition (des barres) ;
//    - l'etat du poste : la cle, le registre, la liste de revocation, la synchro ;
//    - les actions rapides : Nouvelle licence... (l'assistant), Noter des
//      demandes... (des .xpgreq recus, pas encore signes), Synchroniser (la page
//      GitHub) ;
//    - le premier lancement (aucune cle) : comment commencer.
//  Des demandes (.xpgreq) glissees sur la page ouvrent l'assistant avec elles.
// =============================================================================
#include "Page.hpp"

#include "../../ui/Icons.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace licdlg {
// LicenceWizard.cpp : l'assistant ouvert prend des demandes glissees (faux : aucun assistant ouvert).
bool wizardTakeDrop(const std::vector<std::string>& paths);
} // namespace licdlg

namespace {

using gfx::Color;
using gfx::FontId;
using gfx::Rect;

constexpr FontId kF28{28}, kF26{26}, kF16{16}, kF15{15}, kF14{14}, kF13{13}, kF12{12};

std::string fit(const gfx::IRenderer& r, const std::string& s, FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

// Un chemin trop long : on garde la fin (le nom du dossier), pas le debut.
std::string fitTail(const gfx::IRenderer& r, const std::string& s, FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::size_t from = 0;
    while (from < s.size() && r.measure("\xE2\x80\xA6" + s.substr(from), f).width > w) {
        ++from;
        while (from < s.size() && (static_cast<unsigned char>(s[from]) & 0xC0) == 0x80) ++from;
    }
    return "\xE2\x80\xA6" + s.substr(from);
}

std::vector<std::string> wrap(const std::string& text, FontId f, float w) {
    std::vector<std::string> out;
    std::string line;
    std::size_t i = 0;
    while (i <= text.size()) {
        const std::size_t sp = text.find(' ', i);
        const std::string word = text.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(tryLine, f) > w) {
            out.push_back(line);
            line = word;
        } else {
            line = tryLine;
        }
        if (sp == std::string::npos) break;
        i = sp + 1;
    }
    if (!line.empty() || out.empty()) out.push_back(line);
    return out;
}

void boldText(gfx::IRenderer& r, gfx::Point p, const std::string& s, FontId f, Color c) {
    r.drawText(p, s, f, c);
    r.drawText({p.x + 0.6f, p.y}, s, f, c);
}

void card(gfx::IRenderer& r, const ui::Theme& th, const Rect& box) {
    r.fillRoundedRect(box, th.brand.cardBorder, 8.f);
    r.fillRoundedRect(box.inset(1.f, 1.f), th.brand.card, 7.f);
}

Color toneColor(const ui::Theme& th, Tone t) {
    switch (t) {
        case Tone::Ok: return th.color.ok;
        case Tone::Warning: return th.color.warning;
        case Tone::Error: return th.color.error;
        case Tone::Info: break;
    }
    return th.color.text;
}

bool isRequest(const std::string& path) {
    std::string ext = std::filesystem::path(std::u8string(path.begin(), path.end())).extension().string();
    for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return ext == ".xpgreq";
}

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// ---- une table a lignes fixes (les dernieres operations) ----
class RowsModel final : public ui::ITableModel {
public:
    RowsModel(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, std::vector<ui::CellStyle> styles,
              std::vector<Seconds> keys, std::vector<std::string> tips)
        : headers_(std::move(headers)), rows_(std::move(rows)), styles_(std::move(styles)), keys_(std::move(keys)), tips_(std::move(tips)) {}
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override { return r < tips_.size() ? tips_[r] : std::string(); }
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string(); }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string();
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        if (r >= styles_.size() || c != 1) return {};
        return styles_[r];
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override {
        if (c == 0 && a < keys_.size() && b < keys_.size()) return keys_[a] < keys_[b];
        return cellText(a, c) < cellText(b, c);
    }

private:
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<ui::CellStyle>            styles_;
    std::vector<Seconds>                  keys_;
    std::vector<std::string>              tips_;
};

// ---- une tuile : un chiffre, ce qu'il compte ; un clic ouvre la page qui en dit plus ----
class StatTile final : public ui::Button {
public:
    StatTile(std::string id, std::string label) : ui::Button(label, std::move(id)), label_(std::move(label)) {}
    void set(std::string value, std::string sub, Tone tone, bool dim) {
        value_ = std::move(value);
        sub_ = std::move(sub);
        tone_ = tone;
        dim_ = dim;
        invalidate();
    }
    [[nodiscard]] const std::string& value() const noexcept { return value_; }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Rect  r = bounds();
        ctx.r.fillRoundedRect(r, hovered() ? c.accent.withAlpha(150) : th.brand.cardBorder, 8.f);
        ctx.r.fillRoundedRect(r.inset(1.f, 1.f), th.brand.card, 7.f);
        const bool  marked = tone_ != Tone::Info && !dim_;
        const Color ink = dim_ ? c.textDisabled : marked ? toneColor(th, tone_) : c.text;
        if (marked) ctx.r.fillRoundedRect({r.x + 1.f, r.y + 10.f, 3.f, r.h - 20.f}, ink, 1.5f);
        const float x = r.x + 16.f;
        ctx.r.drawText({x, r.y + 10.f}, fit(ctx.r, label_, kF13, r.w - 28.f), kF13, dim_ ? c.textDisabled : c.textMuted);
        const FontId vf = r.h < 66.f ? FontId{24} : kF28;
        const float  vy = r.y + 28.f;
        if (value_.empty()) {   // pas de chiffre (le service en ligne) : la phrase, sur deux lignes s'il le faut
            float ly = r.y + 28.f;
            for (const auto& l : wrap(sub_, kF13, r.w - 28.f)) {
                if (ly + 14.f > r.bottom()) break;
                ctx.r.drawText({x, ly}, l, kF13, c.textDisabled);
                ly += 16.f;
            }
            return;
        }
        boldText(ctx.r, {x, vy}, value_, vf, ink);
        const float vw = ctx.r.measure(value_, vf).width + 10.f;
        const float lh = ctx.r.lineHeight(vf);
        ctx.r.drawText({x + vw, vy + lh - ctx.r.lineHeight(kF12) - 3.f}, fit(ctx.r, sub_, kF12, r.w - 28.f - vw), kF12, c.textMuted);
    }

private:
    std::string label_, value_, sub_;
    Tone        tone_{Tone::Info};
    bool        dim_{false};
};

struct PosteLine {
    std::string label, value;
    Tone        tone{Tone::Info};
};

class DashboardPage final : public Page {
public:
    explicit DashboardPage(ShellHost host) : Page("page.tableau", std::move(host)) {
        newLicence_ = addButton("Nouvelle licence\xE2\x80\xA6", "tableau.nouvelle-licence", ui::Button::Style::Primary, ui::Icon::Plus);
        noteRequests_ = addButton("Noter des demandes\xE2\x80\xA6", "tableau.noter-demandes", ui::Button::Style::Default, ui::Icon::Import);
        sync_ = addButton("Synchroniser", "tableau.synchroniser", ui::Button::Style::Default, ui::Icon::Refresh);
        newLicence_->setTooltip("L'assistant : le client, l'\xC3\xA9" "dition, les PC (leurs demandes .xpgreq), la dur\xC3\xA9" "e, puis signer.");
        noteRequests_->setTooltip("Des demandes d'activation re\xC3\xA7ues (.xpgreq), pas encore sign\xC3\xA9" "es : not\xC3\xA9" "es dans le registre des activations.");
        sync_->setTooltip("La page Synchronisation GitHub : recevoir et envoyer l'archive.");
        links_ += newLicence_->clicked->connect([this] { openWizard({}); });
        links_ += noteRequests_->clicked->connect([this] { askRequests(); });
        links_ += sync_->clicked->connect([this] { host_.navigate("github"); });

        createKey_ = addButton("Cr\xC3\xA9" "er la cl\xC3\xA9\xE2\x80\xA6", "tableau.creer-cle", ui::Button::Style::Primary, ui::Icon::Lock);
        restore_ = addButton("Restaurer une sauvegarde\xE2\x80\xA6", "tableau.restaurer", ui::Button::Style::Default, ui::Icon::Open);
        toEditions_ = addButton("Aller aux \xC3\xA9" "ditions", "tableau.aller-editions", ui::Button::Style::Primary, ui::Icon::GoTo);
        links_ += createKey_->clicked->connect([this] { host_.navigate("securite"); });
        links_ += restore_->clicked->connect([this] { host_.navigate("securite"); });
        links_ += toEditions_->clicked->connect([this] { host_.navigate("editions"); });

        const struct {
            const char* id;
            const char* label;
            const char* page;
        } tiles[] = {
            {"clients", "Clients", "clients"},
            {"actives", "Licences actives", "licences"},
            {"expirees", "Expir\xC3\xA9" "es", "licences"},
            {"revoquees", "R\xC3\xA9voqu\xC3\xA9" "es", "revocations"},
            {"pc", "PC activ\xC3\xA9s", "activations"},
            {"demandes", "Demandes en attente", "activations"},
            {"bientot", "Finissent dans 30 jours", "licences"},
            {"flottantes", "Licences flottantes", "flottantes"},
        };
        for (const auto& t : tiles) {
            auto tile = std::make_unique<StatTile>(std::string("tableau.tuile.") + t.id, t.label);
            const std::string page = t.page;
            links_ += tile->clicked->connect([this, page] { host_.navigate(page); });
            tiles_.push_back(&static_cast<StatTile&>(addChild(std::move(tile))));
        }
        tiles_.back()->setTooltip("Les licences flottantes demandent le service de licences en ligne, pas encore fait : aucun chiffre ici.");

        auto table = std::make_unique<ui::TableView>("tableau.operations");
        table->setColumns({{"Date", 132.f}, {"Op\xC3\xA9ration", 140.f}, {"Licence", 140.f}, {"Client", 180.f}, {"PC", 220.f}});
        table->setAlternatingRowColors(true);
        table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
        links_ += table_->activated->connect([this](ui::RowIndex) { host_.navigate("historique"); });
        refresh();
    }

    [[nodiscard]] std::string title() const override { return "Tableau de bord"; }

    void refresh() override {
        const Workspace& w = ws();
        d_ = w.dashboard();
        const auto& k = w.key();
        hint_.clear();
        hintKind_ = 0;
        if (!k.present) {
            hintKind_ = 1;
            hint_ = "Pour commencer : cr\xC3\xA9" "e la cl\xC3\xA9 de l'\xC3\xA9" "diteur (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9 \xE2\x80\xBA Cr\xC3\xA9" "er la cl\xC3\xA9), ou restaure la sauvegarde d'une "
                    "cl\xC3\xA9 existante. Sans cl\xC3\xA9, rien ne se signe.";
        } else if (!w.hasRegistry()) {
            hintKind_ = 2;
            hint_ = "Il manque le registre (les \xC3\xA9" "ditions et les fonctionnalit\xC3\xA9s) : signe-le dans \xC3\x89" "ditions avant la premi\xC3\xA8re licence.";
        }
        createKey_->setVisibility(hintKind_ == 1 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        restore_->setVisibility(hintKind_ == 1 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        toEditions_->setVisibility(hintKind_ == 2 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        newLicence_->setEnabled(k.present && w.hasRegistry());
        newLicence_->setTooltip(k.present && w.hasRegistry()
                                    ? std::string("L'assistant : le client, l'\xC3\xA9" "dition, les PC (leurs demandes .xpgreq), la dur\xC3\xA9" "e, puis signer.")
                                    : std::string("Il faut d'abord la cl\xC3\xA9 de l'\xC3\xA9" "diteur et le registre sign\xC3\xA9."));

        const auto n = [](int v) { return std::to_string(v); };
        tiles_[0]->set(n(d_.clients), "dans l'archive", Tone::Info, false);
        tiles_[1]->set(n(d_.activeLicences), "sur " + n(d_.licences) + " au total", Tone::Info, false);
        tiles_[2]->set(n(d_.expiredLicences), "\xC3\xA0 renouveler", d_.expiredLicences ? Tone::Warning : Tone::Info, false);
        tiles_[3]->set(n(d_.revokedLicences), w.hasRevocations() ? "liste n\xC2\xB0 " + std::to_string(w.revocations().number) : std::string("aucune liste"),
                       d_.revokedLicences ? Tone::Error : Tone::Info, false);
        tiles_[4]->set(n(d_.devices), "li\xC3\xA9s \xC3\xA0 une licence", Tone::Info, false);
        tiles_[5]->set(n(d_.pendingRequests), "pas encore sign\xC3\xA9" "es", d_.pendingRequests ? Tone::Warning : Tone::Info, false);
        tiles_[6]->set(n(d_.expiringSoon), "licences actives", d_.expiringSoon ? Tone::Warning : Tone::Info, false);
        tiles_[7]->set("", "demande le service en ligne", Tone::Info, true);

        rebuildOps();

        // L'etat du poste.
        poste_.clear();
        if (k.present)
            poste_.push_back({"Cl\xC3\xA9", k.id + (k.test ? std::string(" (essai)") : std::string()), k.test ? Tone::Warning : Tone::Ok});
        else
            poste_.push_back({"Cl\xC3\xA9", "aucune sur ce poste", Tone::Error});
        if (k.present) poste_.push_back({"Protection", k.protectedHere ? "chiffr\xC3\xA9" "e pour ce compte (DPAPI)" : "fichier r\xC3\xA9serv\xC3\xA9 \xC3\xA0 ce compte", Tone::Info});
        if (w.hasRegistry())
            poste_.push_back({"Registre", "n\xC2\xB0 " + std::to_string(w.registry().number) + ", du " + dateFr(w.registry().issuedAt), Tone::Info});
        else
            poste_.push_back({"Registre", "pas encore sign\xC3\xA9", Tone::Warning});
        poste_.push_back({"R\xC3\xA9vocations", w.hasRevocations() ? "liste n\xC2\xB0 " + std::to_string(w.revocations().number) + " (" +
                                                                  plural(static_cast<int>(w.revocations().revoked.size()), "entr\xC3\xA9" "e", "entr\xC3\xA9" "es") + ")"
                                                            : std::string("aucune liste sign\xC3\xA9" "e"),
                          Tone::Info});
        poste_.push_back({"Archive", plural(static_cast<int>(w.licences().size()), "licence sign\xC3\xA9" "e", "licences sign\xC3\xA9" "es"), Tone::Info});
        poste_.push_back({"Activations", plural(static_cast<int>(w.records().size()), "ligne", "lignes") + " dans le registre", Tone::Info});
        const auto& g = w.github();
        if (!g.problem.empty()) poste_.push_back({"Synchro", g.problem, Tone::Error});
        else if (!g.configured) poste_.push_back({"Synchro", "GitHub non r\xC3\xA9gl\xC3\xA9", Tone::Info});
        else poste_.push_back({"Synchro", g.config.repo + (g.token ? std::string() : std::string(" (pas de jeton)")), g.token ? Tone::Info : Tone::Warning});
        if (!w.archiveProblems().empty())
            poste_.push_back({"Illisibles", plural(static_cast<int>(w.archiveProblems().size()), "fichier de l'archive", "fichiers de l'archive"), Tone::Warning});
        if (w.skippedLines() > 0)
            poste_.push_back({"Illisibles", plural(w.skippedLines(), "ligne des activations", "lignes des activations"), Tone::Warning});
        poste_.push_back({"Dossier", w.folder(), Tone::Info});
        invalidateLayout();
    }

    bool dropFiles(const std::vector<std::string>& paths) override {
        if (paths.empty() || !std::all_of(paths.begin(), paths.end(), isRequest)) return false;   // la coquille les range
        if (licdlg::wizardTakeDrop(paths)) return true;   // l'assistant deja ouvert les prend
        if (!ws().key().present || !ws().hasRegistry()) {
            host_.report(OpResult::fail(!ws().key().present ? "pas de cl\xC3\xA9 de l'\xC3\xA9" "diteur sur ce poste : cr\xC3\xA9" "e-la d'abord (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9), puis glisse \xC3\xA0 nouveau les demandes"
                                                            : "pas encore de registre sign\xC3\xA9 : signe-le d'abord (\xC3\x89" "ditions), puis glisse \xC3\xA0 nouveau les demandes"));
            return true;
        }
        openWizard(paths);
        return true;
    }

    // Les essais.
    [[nodiscard]] const Dashboard& numbers() const noexcept { return d_; }

protected:
    void onLayout() override {
        const Rect  b = bounds();
        const float pad = b.w < 760.f ? 16.f : 24.f;
        const float x0 = b.x + pad, w = std::max(240.f, b.w - 2 * pad);
        const bool  compact = b.h < 680.f;
        float       y = b.y + 18.f;
        // Le titre et les actions rapides a droite.
        titleY_ = y;
        float bx = x0 + w;
        for (ui::Button* btn : {newLicence_, noteRequests_, sync_}) {
            const float bw = btn->sizeHint().preferred.w;
            bx -= bw;
            btn->setBounds({bx, y, bw, 32.f});
            bx -= 8.f;
        }
        y += 40.f;
        leadY_ = y;
        y += 30.f;

        // Le premier lancement.
        hintRect_ = {};
        hintLines_.clear();
        if (hintKind_ != 0) {
            std::vector<ui::Button*> btns;
            if (hintKind_ == 1) btns = {createKey_, restore_};
            else btns = {toEditions_};
            float bw = 0.f;
            for (auto* btn : btns) bw += btn->sizeHint().preferred.w + 8.f;
            const bool beside = w - bw - 64.f > 320.f;
            hintLines_ = wrap(hint_, kF15, (beside ? w - bw - 72.f : w - 64.f));
            const float textH = static_cast<float>(hintLines_.size()) * 20.f;
            const float h = beside ? std::max(textH, 32.f) + 28.f : textH + 32.f + 28.f + 8.f;
            hintRect_ = {x0, y, w, h};
            float hx = x0 + w - 14.f;
            const float hy = beside ? y + (h - 32.f) * 0.5f : y + 14.f + textH + 8.f;
            if (!beside) hx = x0 + 44.f + bw;
            for (auto it = btns.rbegin(); it != btns.rend(); ++it) {
                const float bw1 = (*it)->sizeHint().preferred.w;
                hx -= bw1;
                (*it)->setBounds({hx, hy, bw1, 32.f});
                hx -= 8.f;
            }
            y += h + 14.f;
        }

        // Les tuiles : quatre par ligne.
        const float tileH = compact ? 62.f : 70.f, gap = 12.f;
        const float tileW = (w - 3 * gap) / 4.f;
        for (std::size_t i = 0; i < tiles_.size(); ++i) {
            const float tx = x0 + static_cast<float>(i % 4) * (tileW + gap);
            const float ty = y + static_cast<float>(i / 4) * (tileH + gap);
            tiles_[i]->setBounds({tx, ty, tileW, tileH});
        }
        y += 2 * tileH + gap + 16.f;

        // En bas : les dernieres operations a gauche ; a droite, les editions puis l'etat du poste.
        const float bottom = b.bottom() - 16.f;
        const float rightW = std::clamp(w * 0.30f, 300.f, 380.f);
        const float leftW = w - rightW - 16.f;
        opsRect_ = {x0, y, leftW, std::max(110.f, bottom - y)};
        table_->setBounds({opsRect_.x + 1.f, opsRect_.y + 40.f, opsRect_.w - 2.f, std::max(40.f, opsRect_.h - 41.f)});
        // Les colonnes suivent la largeur ; etroite, la date perd l'heure.
        const float tw = opsRect_.w - 2.f - 14.f;
        if (std::abs(tw - columnsFor_) > 1.f) {
            columnsFor_ = tw;
            const bool narrow = tw < 700.f;
            const float date = narrow ? 100.f : 138.f, op = 156.f, lic = 142.f;
            const float rest = std::max(240.f, tw - date - op - lic);
            table_->setColumns({{"Date", date}, {"Op\xC3\xA9ration", op}, {"Licence", lic}, {"Client", std::floor(rest * 0.5f)}, {"PC", std::floor(rest * 0.5f)}});
            if (narrow != narrow_) {
                narrow_ = narrow;
                rebuildOps();
            }
        }
        // A droite : les editions (a leur hauteur), puis l'etat du poste jusqu'en bas.
        const std::size_t bars = std::max<std::size_t>(d_.byEdition.size(), 2);
        barH_ = compact ? 22.f : 24.f;
        lineH_ = compact ? 19.f : 21.f;
        const float wantEd = 44.f + static_cast<float>(std::min<std::size_t>(bars, 8)) * barH_ + 10.f;
        const float wantPoste = 44.f + static_cast<float>(poste_.size()) * lineH_ + 8.f;
        const float colH = std::max(200.f, bottom - y);
        const float edH = std::clamp(colH - wantPoste - 16.f, 92.f, wantEd);
        editionsRect_ = {x0 + leftW + 16.f, y, rightW, edH};
        posteRect_ = {x0 + leftW + 16.f, y + edH + 16.f, rightW, std::max(60.f, colH - edH - 16.f)};
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Rect  b = bounds();
        ctx.r.fillRect(b, c.windowBg);
        const float pad = b.w < 760.f ? 16.f : 24.f;
        const float x0 = b.x + pad;
        boldText(ctx.r, {x0, titleY_}, "Tableau de bord", kF26, c.text);
        ctx.r.drawText({x0, leadY_}, fit(ctx.r, "Ce que disent l'archive et le registre des activations de ce poste, hors ligne. Un clic sur un chiffre ouvre sa page.", kF15, b.w - 2 * pad),
                       kF15, c.textMuted);

        if (!hintRect_.empty()) {
            ctx.r.fillRoundedRect(hintRect_, c.accent.withAlpha(110), 8.f);
            ctx.r.fillRoundedRect(hintRect_.inset(1.f, 1.f), c.panelBg, 7.f);
            ctx.r.fillRoundedRect(hintRect_.inset(1.f, 1.f), c.accent.withAlpha(16), 7.f);
            ui::drawIcon(ctx.r, hintKind_ == 1 ? ui::Icon::Lock : ui::Icon::Info, {hintRect_.x + 16.f, hintRect_.y + 14.f, 18.f, 18.f}, c.accent);
            float y = hintRect_.y + 14.f;
            for (const auto& l : hintLines_) {
                ctx.r.drawText({hintRect_.x + 44.f, y}, l, kF15, c.text);
                y += 20.f;
            }
        }

        // Les dernieres operations.
        card(ctx.r, th, opsRect_);
        boldText(ctx.r, {opsRect_.x + 16.f, opsRect_.y + 12.f}, "Derni\xC3\xA8res op\xC3\xA9rations", kF16, c.text);
        {
            const std::string n = d_.recent.empty() ? std::string() : d_.recent.size() >= 12 ? std::string("les 12 plus r\xC3\xA9" "centes") : plural(static_cast<int>(d_.recent.size()), "op\xC3\xA9ration", "op\xC3\xA9rations");
            const float nw = ctx.r.measure(n, kF13).width;
            ctx.r.drawText({opsRect_.right() - 16.f - nw, opsRect_.y + 14.f}, n, kF13, c.textMuted);
        }

        // La repartition par edition.
        card(ctx.r, th, editionsRect_);
        boldText(ctx.r, {editionsRect_.x + 16.f, editionsRect_.y + 12.f}, "R\xC3\xA9partition par \xC3\xA9" "dition", kF16, c.text);
        {
            const std::string sub = "licences actives";
            const float sw = ctx.r.measure(sub, kF13).width;
            ctx.r.drawText({editionsRect_.right() - 16.f - sw, editionsRect_.y + 14.f}, sub, kF13, c.textMuted);
            float y = editionsRect_.y + 44.f;
            const float rowH = barH_;
            if (d_.byEdition.empty()) {
                ctx.r.drawText({editionsRect_.x + 16.f, y}, "Aucune licence active.", kF14, c.textMuted);
            } else {
                int most = 1;
                for (const auto& [name, count] : d_.byEdition) most = std::max(most, count);
                const float labelW = std::min(150.f, editionsRect_.w * 0.4f);
                const float countW = 36.f;
                const float barX = editionsRect_.x + 16.f + labelW + 8.f;
                const float barW = std::max(20.f, editionsRect_.right() - 16.f - countW - barX);
                const std::size_t fits = static_cast<std::size_t>(std::max(1.f, (editionsRect_.bottom() - 8.f - y) / rowH));
                for (std::size_t i = 0; i < d_.byEdition.size() && i < fits; ++i) {
                    const auto& [name, count] = d_.byEdition[i];
                    if (i + 1 == fits && d_.byEdition.size() > fits) {
                        ctx.r.drawText({editionsRect_.x + 16.f, y + 3.f}, "+ " + std::to_string(d_.byEdition.size() - i) + " autres \xC3\xA9" "ditions", kF13, c.textMuted);
                        break;
                    }
                    ctx.r.drawText({editionsRect_.x + 16.f, y + 3.f}, fit(ctx.r, name, kF14, labelW), kF14, c.text);
                    const Rect track{barX, y + 6.f, barW, 10.f};
                    ctx.r.fillRoundedRect(track, c.gridLine, 5.f);
                    const float fw = std::max(10.f, barW * static_cast<float>(count) / static_cast<float>(most));
                    ctx.r.fillRoundedRect({track.x, track.y, fw, track.h}, c.accent, 5.f);
                    const std::string cnt = std::to_string(count);
                    const float cw = ctx.r.measure(cnt, kF14).width;
                    boldText(ctx.r, {editionsRect_.right() - 16.f - cw, y + 3.f}, cnt, kF14, c.text);
                    y += rowH;
                }
            }
        }

        // L'etat du poste.
        card(ctx.r, th, posteRect_);
        boldText(ctx.r, {posteRect_.x + 16.f, posteRect_.y + 12.f}, "\xC3\x89tat du poste", kF16, c.text);
        {
            float y = posteRect_.y + 44.f;
            const float labelW = 86.f;
            for (const auto& l : poste_) {
                if (y + 18.f > posteRect_.bottom()) break;
                ctx.r.drawText({posteRect_.x + 16.f, y}, l.label, kF13, c.textMuted);
                const float vx = posteRect_.x + 16.f + labelW, vw = posteRect_.right() - 16.f - vx;
                const std::string v = l.label == "Dossier" ? fitTail(ctx.r, l.value, kF14, vw) : fit(ctx.r, l.value, kF14, vw);
                ctx.r.drawText({vx, y - 1.f}, v, kF14, l.tone == Tone::Info ? c.text : toneColor(th, l.tone));
                y += lineH_;
            }
        }
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (!d_.recent.empty()) return;
        const auto& c = ctx.theme.color;
        const Rect  t = table_->bounds();
        ctx.r.drawText({t.x + 16.f, t.y + 40.f}, fit(ctx.r, "Aucune op\xC3\xA9ration pour l'instant : signe une licence ou note une demande.", kF14, t.w - 32.f), kF14,
                       c.textMuted);
    }

private:
    // Les dernieres operations (etroite : la date sans l'heure).
    void rebuildOps() {
        std::vector<std::vector<std::string>> rows;
        std::vector<ui::CellStyle>            styles;
        std::vector<Seconds>                  keys;
        std::vector<std::string>              tips;
        for (const auto& a : d_.recent) {
            std::string what = a.kind;
            ui::CellStyle s;
            if (a.kind == "licence") {
                what = a.extension ? "Licence \xC3\xA9tendue" : "Licence sign\xC3\xA9" "e";
                s.icon = ui::Icon::Ok;
                s.iconTone = ui::Tone::Ok;
            } else if (a.kind == "demande") {
                what = "Demande re\xC3\xA7ue";
                s.icon = ui::Icon::Mail;
                s.iconTone = ui::Tone::Info;
            } else if (a.kind == "revocation") {
                what = a.deviceKey.empty() ? "Licence r\xC3\xA9voqu\xC3\xA9" "e" : "PC r\xC3\xA9voqu\xC3\xA9";
                s.icon = ui::Icon::Close;
                s.iconTone = ui::Tone::Error;
            }
            const std::string pc = a.kind == "revocation" && a.deviceKey.empty() ? std::string("toute la licence")
                                   : a.deviceName.empty()                         ? std::string("(sans nom)")
                                                                                  : a.deviceName;
            // Une revocation ne note pas le client : celui de la licence de l'archive.
            std::string customer = a.customer;
            if (customer.empty() && !a.licenseId.empty())
                if (const LicenceEntry* l = ws().licence(a.licenseId)) customer = l->licence.customer;
            rows.push_back({narrow_ ? dateFr(a.at) : dateTimeFr(a.at), what, a.licenseId.empty() ? std::string("-") : a.licenseId, customer, pc});
            styles.push_back(s);
            keys.push_back(a.at);
            tips.push_back(a.fingerprint.empty() ? std::string() : "Empreinte de la cl\xC3\xA9 du PC : " + a.fingerprint);
        }
        model_ = std::make_shared<RowsModel>(std::vector<std::string>{"Date", "Op\xC3\xA9ration", "Licence", "Client", "PC"}, std::move(rows), std::move(styles),
                                             std::move(keys), std::move(tips));
        table_->setModel(model_);
    }

    ui::Button* addButton(const char* text, const char* id, ui::Button::Style style, ui::Icon icon) {
        auto b = std::make_unique<ui::Button>(text, id);
        b->setStyle(style);
        b->setIcon(icon);
        return &static_cast<ui::Button&>(addChild(std::move(b)));
    }

    void openWizard(std::vector<std::string> requests) {
        if (!ws().key().present || !ws().hasRegistry()) {
            host_.report(OpResult::fail("il faut d'abord la cl\xC3\xA9 de l'\xC3\xA9" "diteur et le registre sign\xC3\xA9"));
            return;
        }
        host_.openDialog(makeLicenceWizard(host_, std::move(requests)));
    }

    void askRequests() {
        if (host_.busy()) {
            host_.report(OpResult::fail("un travail lent tourne : attends sa fin pour noter des demandes"));
            return;
        }
        ui::FilePick pick;
        pick.title = "Choisis la demande d'activation re\xC3\xA7ue (.xpgreq)";
        pick.filters = {{"Demande d'activation (.xpgreq)", "xpgreq"}};
        host_.pickFile(pick, [this](std::string path) {
            if (path.empty()) return;
            if (host_.busy()) {
                host_.report(OpResult::fail("un travail lent tourne : attends sa fin pour noter des demandes"));
                return;
            }
            host_.report(ws().recordRequests({path}));
        });
    }

    Dashboard                    d_;
    std::vector<PosteLine>       poste_;
    std::string                  hint_;
    int                          hintKind_{0};   // 1 : pas de cle ; 2 : pas de registre
    std::vector<std::string>     hintLines_;
    ui::Button*                  newLicence_{nullptr};
    ui::Button*                  noteRequests_{nullptr};
    ui::Button*                  sync_{nullptr};
    ui::Button*                  createKey_{nullptr};
    ui::Button*                  restore_{nullptr};
    ui::Button*                  toEditions_{nullptr};
    std::vector<StatTile*>       tiles_;
    ui::TableView*               table_{nullptr};
    std::shared_ptr<RowsModel>   model_;
    Rect                         hintRect_{}, opsRect_{}, editionsRect_{}, posteRect_{};
    float                        columnsFor_{-1.f};
    float                        barH_{24.f}, lineH_{21.f};
    bool                         narrow_{false};
    float                        titleY_{0.f}, leadY_{0.f};
    core::ConnectionScope        links_;
};

} // namespace

PagePtr makeDashboardPage(ShellHost host) { return std::make_unique<DashboardPage>(std::move(host)); }

} // namespace license_admin::gui
