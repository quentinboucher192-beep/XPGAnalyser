// =============================================================================
//  license_admin/gui/PageLicences.cpp - 1.33.0 : LA PAGE << LICENCES >>
// -----------------------------------------------------------------------------
//  Chaque licence de l'archive (sa derniere emission signee) : une recherche
//  (identifiant, client, edition), des filtres (toutes, actives, expirees,
//  revoquees), le tableau, et le detail de celle choisie : tous ses champs, ses
//  fonctionnalites, ses PC (nom, empreinte, date d'activation ; les PC revoques
//  marques). Les gestes :
//    Nouvelle licence...          l'assistant (LicenceWizard.cpp) ;
//    Nouvelle licence en ligne... 1.35.0 : l'assistant d'une licence EN LIGNE
//                                 (aucun PC : un code d'activation, montre une
//                                 fois, masque, Afficher et Copier) ;
//    Ajouter un PC...             l'assistant sur la licence choisie (en ligne :
//                                 Prolonger..., une emission plus recente) ;
//    Exporter le fichier...       le .xpglic a renvoyer au client ;
//    Revoquer...                  un dialogue : toute la licence ou un PC ;
//    Ouvrir le dossier de l'archive.
//  1.35.0 : une licence EN LIGNE montre ses ACTIVATIONS (le dernier releve du
//  service : le nom du PC, vu le, bail jusqu'au, etat) ; << Retirer ce PC >>
//  (il n'est plus renouvele), << Nouveau code... >> (l'ancien ne marche plus),
//  << Desactiver >> / << Reactiver >>, << Relever >> (lents : runSlow).
//  Des demandes (.xpgreq) glissees sur la page vont a l'assistant (ouvert, ou
//  ouvert pour elles) ; un .xpglic n'est pas pour elle (la coquille le range).
// =============================================================================
#include "Page.hpp"

#include "../ArchiveSync.hpp"
#include "../../ui/Shapes.hpp"   // 1.41.0 : les cadres arrondis
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
menu::MenuPtr makeConfirmDialog(std::string id, std::string title, std::string text, std::string okLabel, bool danger,
                                std::function<void()> onOk);
std::string fingerprintOfKey(const std::string& hex);
std::string startFor(const std::string& fileName);
void rememberFolder(const std::string& filePath);
} // namespace licdlg

namespace {

namespace fs = std::filesystem;
using licensing::LicenseType;
using PG = ui::PropertyGrid;

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

// 1.41.0 : UNE PASTILLE DE FILTRE (la maquette) - << Toutes 3 >> : choisie, l'encre pleine et le texte blanc ; sinon
// blanche, bordee. Son texte reste << Toutes (3) >> (ce que lisent les essais et les lecteurs d'ecran).
class FilterChip final : public ui::Button {
public:
    explicit FilterChip(std::string id) : ui::Button("", std::move(id)) {}
    void setOn(bool on) {
        if (on_ != on) { on_ = on; invalidate(); }
    }
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {ui::measureWidth(text(), gfx::semibold(gfx::FontId{13})) + 26.f, 34.f};
        h.minimum = h.preferred;
        return h;
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const gfx::Rect r = bounds();
        const std::string& t = text();
        const auto open = t.rfind(" (");
        const std::string label = open == std::string::npos ? t : t.substr(0, open);
        const std::string count = open == std::string::npos || t.size() < open + 3 ? std::string() : t.substr(open + 2, t.size() - open - 3);
        const gfx::Color fill = on_ ? c.text : hovered() ? c.rowAltBg : c.panelBg;
        ui::shapes::fillFramedRect(ctx.r, r, fill, on_ ? c.text : c.border, r.h * 0.5f);
        const gfx::FontId fl = gfx::semibold(gfx::FontId{13}), fc = gfx::FontId{12};
        const float lw = ctx.r.measure(label, fl).width, cw = count.empty() ? 0.f : ctx.r.measure(count, fc).width + 6.f;
        float x = r.x + (r.w - lw - cw) * 0.5f;
        ctx.r.drawText({x, r.y + (r.h - ctx.r.lineHeight(fl)) * 0.5f}, label, fl, on_ ? c.textInverted : c.text);
        x += lw + 6.f;
        if (!count.empty())
            ctx.r.drawText({x, r.y + (r.h - ctx.r.lineHeight(fc)) * 0.5f + 1.f}, count, fc, on_ ? c.textInverted.withAlpha(190) : c.textMuted);
    }

private:
    bool on_{false};
};

// 1.41.0 : la carte de la maquette (blanche, coins de 12, un trait, une ombre a peine).
void cardBox(gfx::IRenderer& r, const ui::Theme& th, const gfx::Rect& box) {
    if (box.empty()) return;
    r.fillRoundedRect({box.x, box.y + 1.f, box.w, box.h + 1.f}, th.brand.cardShadow, 12.f);
    r.fillRoundedRect(box, th.brand.cardBorder, 12.f);
    r.fillRoundedRect(box.inset(1.f, 1.f), th.brand.card, 11.f);
}

// Les initiales d'un client (l'avatar de la fiche) : << Atelier Meyer >> -> << AM >>.
std::string initialsOf(const std::string& name) {
    std::string out;
    bool start = true;
    for (std::size_t i = 0; i < name.size() && out.size() < 2; ++i) {
        const unsigned char ch = static_cast<unsigned char>(name[i]);
        if (ch == ' ' || ch == '-' || ch == '\'') { start = true; continue; }
        if (start && std::isalnum(ch)) out += static_cast<char>(std::toupper(ch));
        start = false;
    }
    return out.empty() ? std::string("?") : out;
}

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
        // 1.41.0 : UNE ACTION PRINCIPALE PAR ECRAN (la maquette validee) - << Nouvelle licence >> est en haut de la fenetre ;
        // la fiche de la licence choisie montre trois boutons (Prolonger ou Ajouter un PC, Exporter, le code) ; le reste
        // est dans son menu << ... >> (le dangereux en rouge). Ces boutons-la vivent dans un support jamais dessine : le
        // menu les nomme, montre ceux qui servent et declenche leur clic.
        holder_ = &adopt(*this, std::make_unique<ui::Widget>("licences.menu.boutons"));
        holder_->setVisibility(ui::Visibility::Collapsed);
        new_ = &button(*holder_, "Nouvelle licence\xE2\x80\xA6", "licences.nouvelle", ui::Button::Style::Primary);
        new_->setTooltip("L'assistant : les demandes des PC, le client, l'\xC3\xA9" "dition, les fonctionnalit\xC3\xA9s, puis signer");
        newOnline_ = &button(*this, "Nouvelle licence en ligne\xE2\x80\xA6", "licences.nouvelle-en-ligne");
        newOnline_->setTooltip("Une licence sans PC li\xC3\xA9 : chaque PC l'active avec un code d'activation (gard\xC3\xA9 chiffr\xC3\xA9 sur ce poste) aupr\xC3\xA8s du service des licences");
        refreshOnline_ = &button(*holder_, "Relever", "licences.relever");
        refreshOnline_->setIcon(ui::Icon::Refresh);
        // 1.40.0 : l'application le fait seule (le regard en direct : un PC active ou libere apparait sans clic)
        refreshOnline_->setTooltip("Lit tout de suite sur le service les activations des licences en ligne (les PC, vu le, bail jusqu'au). "
                                   "L'application le fait seule d\xC3\xA8s qu'un PC est activ\xC3\xA9 ou lib\xC3\xA9r\xC3\xA9 (en direct).");
        showCode_ = &button(*this, "Voir le code\xE2\x80\xA6", "licences.voir-code");   // 1.37.0
        showCode_->setTooltip("Le code d'activation de la licence en ligne choisie, gard\xC3\xA9 chiffr\xC3\xA9 sur ce poste (pour ce compte Windows)");
        newCode_ = &button(*holder_, "Nouveau code\xE2\x80\xA6", "licences.nouveau-code");
        newCode_->setTooltip("Un code d'activation neuf pour la licence en ligne choisie : l'ancien ne marche plus (les PC d\xC3\xA9j\xC3\xA0 activ\xC3\xA9s restent)");
        active_ = &button(*holder_, "D\xC3\xA9sactiver\xE2\x80\xA6", "licences.desactiver", ui::Button::Style::Danger);
        active_->setTooltip("La licence en ligne ne s'active plus et ses PC ne sont plus renouvel\xC3\xA9s (chacun s'arr\xC3\xAAte \xC3\xA0 la fin de son bail)");
        addPc_ = &button(*this, "Ajouter un PC\xE2\x80\xA6", "licences.ajouter-pc", ui::Button::Style::Primary);   // 1.41.0 : l'action de la fiche
        addPc_->setTooltip("Lier de nouveaux PC \xC3\xA0 la licence choisie (ses PC li\xC3\xA9s sont gard\xC3\xA9s) ; la r\xC3\xA9-\xC3\xA9mettre");
        export_ = &button(*this, "Exporter le fichier\xE2\x80\xA6", "licences.exporter");
        export_->setTooltip("Le .xpglic de la licence choisie, \xC3\xA0 renvoyer au client");
        revoke_ = &button(*holder_, "R\xC3\xA9voquer\xE2\x80\xA6", "licences.revoquer", ui::Button::Style::Danger);
        revoke_->setTooltip("Toute la licence choisie, ou un de ses PC");
        // 1.38.0 : supprimer une licence revoquee (la corbeille de l'outil ; sa revocation reste).
        delete_ = &button(*holder_, "Supprimer\xE2\x80\xA6", "licences.supprimer", ui::Button::Style::Danger);
        delete_->setTooltip("La licence r\xC3\xA9voqu\xC3\xA9" "e choisie quitte la liste (son fichier va dans la corbeille de l'outil) ; sa r\xC3\xA9vocation reste en vigueur");
        // 1.33.0 : retirer un PC de la licence (un PC remplace ou rendu) : une nouvelle emission sans lui.
        dropPc_ = &button(*holder_, "Retirer un PC\xE2\x80\xA6", "licences.retirer-pc");
        dropPc_->setTooltip("Le PC choisi dans la liste des PC : une nouvelle \xC3\xA9mission de la licence sans lui (m\xC3\xAA" "me fin), \xC3\xA0 renvoyer au client");
        folder_ = &button(*holder_, "Ouvrir le dossier de l'archive", "licences.dossier");
        more_ = &button(*this, "\xE2\x80\xA6", "licences.plus");
        more_->setTooltip("Les autres gestes sur cette licence : nouveau code, retirer un PC, d\xC3\xA9sactiver, r\xC3\xA9voquer, le dossier de l'archive\xE2\x80\xA6");
        menu_ = &adopt(*this, std::make_unique<ui::PopupMenu>("licences.menu"));
        links_ += more_->clicked->connect([this] { openMore(); });
        links_ += menu_->itemChosen->connect([this](int id) {
            if (id >= 0 && static_cast<std::size_t>(id) < menuButtons_.size() && menuButtons_[static_cast<std::size_t>(id)]->enabled())
                menuButtons_[static_cast<std::size_t>(id)]->clicked->emit();
        });
        search_ = &adopt(*this, std::make_unique<ui::InputText>("licences.recherche"));
        search_->setPlaceholder("Identifiant, client, \xC3\xA9" "dition\xE2\x80\xA6");
        search_->setEscapeClears(true);
        const char* chipIds[4] = {"licences.filtre.toutes", "licences.filtre.actives", "licences.filtre.expirees", "licences.filtre.revoquees"};
        for (int i = 0; i < 4; ++i) {
            chips_[i] = &adopt(*this, std::make_unique<FilterChip>(chipIds[i]));
            links_ += chips_[i]->clicked->connect([this, i] { setFilter(static_cast<Filter>(i)); });
        }

        auto table = std::make_unique<ui::TableView>("licences.table");
        table->setRowHeightOverride(40.f);   // 1.41.0 : une liste aeree
        table->setSelectionMode(ui::SelectionMode::Single);
        table->setAlternatingRowColors(true);
        table->setColumns({{"ID", 140.f}, {"Client", 190.f}, {"\xC3\x89" "dition", 130.f}, {"Type", 120.f}, {"PC", 70.f}, {"\xC3\x89mise le", 105.f},
                           {"Fin", 105.f}, {"\xC3\x89tat", 220.f}});
        table_ = table.get();
        auto details = std::make_unique<ui::PropertyGrid>("licences.details");
        details->setShowDescriptionPane(false);
        details_ = details.get();
        auto pcs = std::make_unique<ui::TableView>("licences.pcs");
        pcs->setSelectionMode(ui::SelectionMode::Single);
        pcs->setColumns({{"PC", 160.f}, {"\xC3\x89tat", 230.f}, {"Empreinte", 250.f}, {"Activ\xC3\xA9 le", 120.f}});
        pcs_ = pcs.get();
        pcs->setRowHeightOverride(34.f);
        adopt(*this, std::move(table));
        adopt(*this, std::move(pcs));
        adopt(*this, std::move(details));

        links_ += new_->clicked->connect([this] { host_.openDialog(makeLicenceWizard(host_, {})); });
        links_ += newOnline_->clicked->connect([this] { host_.openDialog(makeLicenceWizard(host_, {}, {}, {}, true)); });
        links_ += refreshOnline_->clicked->connect([this] { fetchOnline(); });
        links_ += newCode_->clicked->connect([this] { askNewCode(); });
        links_ += showCode_->clicked->connect([this] { showCode(); });
        links_ += active_->clicked->connect([this] { askActive(); });
        links_ += addPc_->clicked->connect([this] {
            if (const LicenceEntry* l = selected())
                host_.openDialog(makeLicenceWizard(host_, {}, l->licence.licenseId, {}, Workspace::isOnlineLicence(l->licence)));
        });
        links_ += export_->clicked->connect([this] { exportSelected(); });
        links_ += revoke_->clicked->connect([this] {
            if (const LicenceEntry* l = selected()) host_.openDialog(licdlg::makeRevokeDialog(host_, l->licence.licenseId));
        });
        links_ += folder_->clicked->connect([this] { openArchive(); });
        links_ += delete_->clicked->connect([this] { askDelete(); });   // 1.38.0
        links_ += dropPc_->clicked->connect([this] { dropSelectedPc(); });
        links_ += pcs_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& sel) {
            selectedPc_ = sel.empty() ? -1 : static_cast<int>(sel.front());
        });
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
            chips_[f]->setOn(static_cast<int>(filter_) == f);
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
    // 1.41.0 : LA MISE EN PAGE DE LA MAQUETTE - le titre et sa ligne, la recherche et les pastilles des filtres, la liste
    // a gauche (des lignes aerees, dans une carte), la fiche de la licence choisie a droite (une carte : qui, l'etat,
    // trois boutons et << ... >>, les chiffres, ses PC, le detail).
    void onLayout() override {
        const auto r = bounds();
        const float pad = r.w < 900.f ? 20.f : 28.f;
        const float x = r.x + pad, w = std::max(200.f, r.w - 2.f * pad);
        const bool compact = r.h < 640.f;
        // En tete, a droite : la licence en ligne (la principale est en haut de la fenetre).
        const float nw = buttonWidth(*newOnline_, 120.f) + 8.f;
        newOnline_->setBounds({x + w - nw, r.y + (compact ? 18.f : 26.f) + 4.f, nw, 34.f});
        filterY_ = r.y + (compact ? 84.f : 100.f);
        const float sw = std::min(320.f, std::max(180.f, w * 0.28f));
        search_->setBounds({x, filterY_, sw, 34.f});
        float cx = x + sw + 12.f;
        for (auto* c : chips_) {
            const float cw = c->sizeHint().preferred.w;
            c->setBounds({cx, filterY_, cw, 34.f});
            cx += cw + 8.f;
        }
        const float top = filterY_ + 50.f, bottom = r.bottom() - 18.f;
        const float rightW = std::clamp(w * 0.36f, 340.f, 460.f);
        const float leftW = std::max(260.f, w - rightW - 16.f);
        listRect_ = {x, top, leftW, std::max(120.f, bottom - top)};
        table_->setBounds(listRect_.inset(1.f, 1.f));
        // Les colonnes a la largeur de la liste : le type et l'emission sont dans la fiche ; etroite, l'edition puis la
        // fin s'effacent aussi (le modele garde toutes les colonnes).
        const float listW = listRect_.w - 2.f - 14.f;
        if (std::abs(listW - columnsFor_) > 1.f) {
            columnsFor_ = listW;
            const bool edition = listW >= 520.f, end = listW >= 660.f;
            const float fixed = 124.f + (edition ? 124.f : 0.f) + 92.f + (end ? 96.f : 0.f);
            const float rest = std::max(200.f, listW - fixed);
            std::vector<ui::TableView::Column> cols = {{"ID", 124.f}, {"Client", std::floor(rest * 0.6f)}, {"\xC3\x89" "dition", 124.f}, {"Type", 120.f},
                                                       {"PC", 92.f}, {"\xC3\x89mise le", 105.f}, {"Fin", 96.f}, {"\xC3\x89tat", std::floor(rest * 0.4f)}};
            cols[2].visible = edition;
            cols[3].visible = false;
            cols[5].visible = false;
            cols[6].visible = end;
            table_->setColumns(std::move(cols));
        }
        cardRect_ = {x + leftW + 16.f, top, rightW, std::max(120.f, bottom - top)};
        // La fiche.
        const float cx0 = cardRect_.x + 18.f, cw0 = cardRect_.w - 36.f;
        headY_ = cardRect_.y + 18.f;
        float by = headY_ + 58.f;
        // Les trois boutons de la fiche, a leur largeur ; serres ensemble s'il le faut (jamais sur << ... >>).
        const float moreW = 38.f;
        std::vector<ui::Button*> row;
        std::vector<float> natural;
        float sum = 0.f;
        for (ui::Button* b : {addPc_, export_, showCode_})
            if (b->visibility() != ui::Visibility::Collapsed) {
                row.push_back(b);
                natural.push_back(buttonWidth(*b, 80.f) + 8.f);
                sum += natural.back();
            }
        const float gaps = 8.f * static_cast<float>(row.empty() ? 0 : row.size() - 1);
        const float room = cw0 - moreW - 8.f - gaps;
        const float k = sum > room && sum > 0.f ? room / sum : 1.f;
        float bx = cx0;
        for (std::size_t i = 0; i < row.size(); ++i) {
            const float bw = std::floor(natural[i] * k);
            row[i]->setBounds({bx, by, bw, 34.f});
            bx += bw + 8.f;
        }
        more_->setBounds({cx0 + cw0 - moreW, by, moreW, 34.f});
        menu_->setBounds(r);   // le menu << ... >> se dessine par-dessus la page (son cadre : toute la page)
        statsY_ = by + 50.f;
        const float statsH = compact ? 96.f : 112.f;
        pcsTitleY_ = statsY_ + statsH + 14.f;
        const float avail = cardRect_.bottom() - 14.f - (pcsTitleY_ + 28.f);
        const float pcsH = std::clamp(avail * 0.45f, 70.f, 34.f * 4.f + 30.f);
        pcs_->setBounds({cardRect_.x + 1.f, pcsTitleY_ + 28.f, cardRect_.w - 2.f, pcsH});
        fitPcsColumns();
        detailTitleY_ = pcsTitleY_ + 28.f + pcsH + 12.f;
        details_->setBounds({cardRect_.x + 1.f, detailTitleY_ + 26.f, cardRect_.w - 2.f, std::max(40.f, cardRect_.bottom() - 10.f - (detailTitleY_ + 26.f))});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const auto b = bounds();
        r.fillRect(b, c.windowBg);
        const float pad = b.w < 900.f ? 20.f : 28.f;
        const float x = b.x + pad, w = b.w - 2.f * pad;
        const bool compact = b.h < 640.f;
        const float ty = b.y + (compact ? 18.f : 26.f);
        r.drawText({x, ty}, title(), gfx::semibold(gfx::FontId{28}), c.text);
        // La ligne sous le titre : combien, et les PC.
        const auto& list = ws().licences();
        int linked = 0, places = 0;
        for (const auto& l : list)
            if (!l.revoked && !l.expired && !Workspace::isOnlineLicence(l.licence)) {
                linked += static_cast<int>(l.licence.devices.size());
                places += l.licence.maxDevices;
            }
        std::string lead = std::to_string(list.size()) + (list.size() > 1 ? " licences" : " licence");
        if (places > 0) lead += " \xC2\xB7 " + std::to_string(linked) + " PC li\xC3\xA9s sur " + std::to_string(places) + " places";
        if (shownIds_.size() != list.size()) lead += " \xC2\xB7 " + std::to_string(shownIds_.size()) + " montr\xC3\xA9" "es";
        r.drawText({x, ty + 40.f}, fit(r, lead, gfx::FontId{14}, w - newOnline_->bounds().w - 16.f), gfx::FontId{14}, c.textMuted);

        cardBox(r, th, listRect_);
        cardBox(r, th, cardRect_);
        const LicenceEntry* e = selected();
        if (!e) return;
        const auto& l = e->licence;
        const float cx0 = cardRect_.x + 18.f, cw0 = cardRect_.w - 36.f;
        // Qui : l'avatar (les initiales), le client, l'identifiant ; l'etat en pastille.
        const gfx::Rect av{cx0, headY_, 44.f, 44.f};
        r.fillRoundedRect(av, c.accent, 12.f);
        const std::string ini = initialsOf(l.customer);
        const gfx::FontId fi = gfx::semibold(gfx::FontId{16});
        r.drawText({av.x + (av.w - r.measure(ini, fi).width) * 0.5f, av.y + (av.h - r.lineHeight(fi)) * 0.5f}, ini, fi, c.textInverted);
        // l'etat
        const Seconds now = ws().now();
        std::string state = e->revoked ? std::string("R\xC3\xA9voqu\xC3\xA9" "e") : e->expired ? std::string("Expir\xC3\xA9" "e") : std::string("Active");
        gfx::Color tone = e->revoked ? c.error : e->expired ? c.textMuted : c.ok;
        if (!e->revoked && !e->expired && l.expiresAt != 0 && l.expiresAt - now <= 30 * kDaySeconds) {
            state = "Finit bient\xC3\xB4t";
            tone = c.warning;
        }
        const gfx::FontId fp = gfx::semibold(gfx::FontId{12});
        const float pw = r.measure(state, fp).width + 30.f;
        const gfx::Rect pill{cx0 + cw0 - pw, headY_ + 2.f, pw, 24.f};
        r.fillRoundedRect(pill, tone.withAlpha(26), 12.f);
        r.fillRoundedRect({pill.x + 10.f, pill.y + 8.5f, 7.f, 7.f}, tone, 3.5f);
        r.drawText({pill.x + 22.f, pill.y + (pill.h - r.lineHeight(fp)) * 0.5f}, state, fp, tone);
        const gfx::FontId fn = gfx::semibold(gfx::FontId{17});
        r.drawText({av.right() + 12.f, headY_ + 1.f}, fit(r, l.customer, fn, pill.x - 10.f - (av.right() + 12.f)), fn, c.text);
        r.drawText({av.right() + 12.f, headY_ + 24.f}, l.licenseId, th.font.mono, c.textMuted);

        // Les chiffres : une grille de 3 x 2, des traits fins (la maquette).
        const auto* ed = ws().registry().edition(l.edition);
        const bool onlineLic = Workspace::isOnlineLicence(l);
        const auto* on = onlineLic ? ws().service().snapshot.licence(l.licenseId) : nullptr;
        std::string pcs = onlineLic ? (on ? std::to_string(on->activeCount()) : std::string("?")) + " sur " + std::to_string(l.maxDevices) + " en ligne"
                                    : std::to_string(l.devices.size()) + " sur " + std::to_string(l.maxDevices);
        std::string end = l.expiresAt ? dateFr(l.expiresAt) : std::string("sans fin");
        if (l.expiresAt && !e->expired && !e->revoked) end += "  \xC2\xB7 " + std::to_string(std::max<Seconds>(0, (l.expiresAt - now) / kDaySeconds)) + " j";
        const std::string cells[6][2] = {
            {"\xC3\x89" "dition", ed && !ed->label.empty() ? ed->label : l.edition},
            {"Fin", end},
            {"Type", typeLabel(l.type) + (onlineLic ? std::string(" \xC2\xB7 en ligne") : std::string())},
            {"PC", pcs},
            {"Hors ligne", l.type == LicenseType::Temporary ? std::to_string(l.offlineGraceDays) + " j de gr\xC3\xA2" "ce" : std::string("sans objet")},
            {"Registre", "n\xC2\xB0 " + std::to_string(l.manifest)},
        };
        const float statsH = compact ? 96.f : 112.f;
        const gfx::Rect grid{cardRect_.x + 1.f, statsY_, cardRect_.w - 2.f, statsH};
        r.fillRect({grid.x, grid.y, grid.w, 1.f}, c.gridLine);
        r.fillRect({grid.x, grid.y + grid.h * 0.5f, grid.w, 1.f}, c.gridLine);
        r.fillRect({grid.x, grid.bottom(), grid.w, 1.f}, c.gridLine);
        const float colW = grid.w / 3.f;
        for (int i = 1; i < 3; ++i) r.fillRect({grid.x + colW * static_cast<float>(i), grid.y, 1.f, grid.h}, c.gridLine);
        const gfx::FontId fv = gfx::semibold(gfx::FontId{14});
        for (int i = 0; i < 6; ++i) {
            const float gx = grid.x + colW * static_cast<float>(i % 3) + 16.f, gy = grid.y + (grid.h * 0.5f) * static_cast<float>(i / 3) + (compact ? 8.f : 12.f);
            r.drawText({gx, gy}, cells[i][0], gfx::FontId{12}, c.textMuted);
            r.drawText({gx, gy + 20.f}, fit(r, cells[i][1], fv, colW - 24.f), fv, c.text);
        }
        // Ses PC, puis le detail complet.
        const gfx::FontId fh = gfx::semibold(gfx::FontId{14});
        r.drawText({cx0, pcsTitleY_}, "PC", fh, c.text);
        const std::size_t n = pcsModel_ ? pcsModel_->rowCount() : 0;
        r.drawText({cx0 + r.measure("PC", fh).width + 8.f, pcsTitleY_ + 1.f}, n ? std::to_string(n) + (onlineLic ? " activ\xC3\xA9(s), au dernier relev\xC3\xA9" : " li\xC3\xA9(s)") : std::string("aucun"),
                   gfx::FontId{13}, c.textMuted);
        r.drawText({cx0, detailTitleY_}, "D\xC3\xA9tail", fh, c.text);
    }

    void onPaintOverlay(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const gfx::FontId f{14};
        if (shownIds_.empty()) {
            const auto t = table_->bounds();
            const std::string text = ws().licences().empty()
                                         ? std::string("Aucune licence dans l'archive : Nouvelle licence (en haut) pour signer la premi\xC3\xA8re, ou glisse ici les demandes .xpgreq d'un client.")
                                         : std::string("Aucune licence ne correspond \xC3\xA0 la recherche ou au filtre.");
            r.drawText({t.x + 20.f, t.y + 48.f}, fit(r, text, f, t.w - 40.f), f, c.textMuted);
        }
        if (!selected()) {
            // Une fiche vide : rien dessous, une phrase.
            const auto d = cardRect_.inset(1.f, 1.f);
            r.fillRoundedRect(d, ctx.theme.brand.card, 11.f);
            ui::drawIcon(r, ui::Icon::Document, {d.x + d.w * 0.5f - 14.f, d.y + d.h * 0.38f - 34.f, 28.f, 28.f}, c.textDisabled);
            const std::string t1 = "Choisis une licence pour voir son d\xC3\xA9tail.";
            r.drawText({d.x + (d.w - r.measure(t1, f).width) * 0.5f, d.y + d.h * 0.38f + 4.f}, fit(r, t1, f, d.w - 32.f), f, c.textMuted);
        }
    }

private:
    [[nodiscard]] const LicenceEntry* selected() const { return selectedId_.empty() ? nullptr : ws().licence(selectedId_); }

    // 1.41.0 : les colonnes des PC a la largeur de la fiche - l'empreinte (et la date d'activation d'un PC en ligne) dans
    // l'infobulle et le detail ; le modele garde toutes les colonnes.
    void fitPcsColumns() {
        const float w = pcs_->bounds().w - 14.f;
        if (w <= 40.f || std::abs(w - pcsColumnsFor_) <= 1.f) return;
        pcsColumnsFor_ = w;
        std::vector<ui::TableView::Column> cols;
        if (pcsOnline_) {
            cols = {{"PC", std::floor(w * 0.34f)}, {"\xC3\x89tat", std::floor(w * 0.24f)}, {"Empreinte", 200.f}, {"Activ\xC3\xA9 le", 100.f},
                    {"Vu le", std::floor(w * 0.22f)}, {"Bail jusqu'au", std::floor(w * 0.20f)}};
            cols[2].visible = false;
            cols[3].visible = false;
        } else {
            cols = {{"PC", std::floor(w * 0.28f)}, {"\xC3\x89tat", std::floor(w * 0.50f)}, {"Empreinte", 220.f}, {"Activ\xC3\xA9 le", std::floor(w * 0.22f)}};
            cols[2].visible = false;
        }
        pcs_->setColumns(std::move(cols));
    }

    // 1.41.0 : LE MENU << ... >> - les gestes de la licence choisie qui ne sont pas dans la fiche, puis ceux de la page ;
    // le dangereux en rouge (une icone d'erreur), a la fin. Une entree grisee dit pourquoi (l'infobulle du bouton).
    void openMore() {
        menuButtons_.clear();
        std::vector<ui::PopupMenu::Item> items;
        auto add = [&](ui::Button* b, ui::Icon icon) {
            if (!b || b->visibility() == ui::Visibility::Collapsed) return;
            ui::PopupMenu::Item it;
            it.label = b->text();
            it.icon = icon;
            it.danger = b->style() == ui::Button::Style::Danger;
            it.enabled = b->enabled();
            if (!it.enabled) it.disabledReason = b->tooltip();
            it.id = static_cast<int>(menuButtons_.size());
            menuButtons_.push_back(b);
            items.push_back(std::move(it));
        };
        auto separator = [&] {
            if (items.empty() || items.back().separator) return;
            ui::PopupMenu::Item it;
            it.separator = true;
            items.push_back(std::move(it));
        };
        add(dropPc_, ui::Icon::Release);
        add(newCode_, ui::Icon::Refresh);
        add(refreshOnline_, ui::Icon::Refresh);
        separator();
        add(folder_, ui::Icon::Folder);
        separator();
        add(active_, active_->style() == ui::Button::Style::Danger ? ui::Icon::Warning : ui::Icon::Ok);
        add(revoke_, ui::Icon::Error);
        add(delete_, ui::Icon::Delete);
        if (!items.empty() && items.back().separator) items.pop_back();
        menu_->setItems(std::move(items));
        const gfx::Rect m = more_->bounds();
        menu_->openInWindow({m.right() - 240.f, m.bottom() + 4.f});
    }

    void setFilter(Filter f) {
        filter_ = f;
        for (int k = 0; k < 4; ++k) chips_[k]->setOn(static_cast<int>(filter_) == k);
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
            std::string pcs = std::to_string(l.devices.size()) + " / " + std::to_string(l.maxDevices);
            if (Workspace::isOnlineLicence(l)) {   // en ligne : les PC actives au dernier releve
                const auto* on = ws().service().snapshot.licence(l.licenseId);
                pcs = (on ? std::to_string(on->activeCount()) : std::string("?")) + " / " + std::to_string(l.maxDevices) + " en ligne";
                if (on && !on->active) state += " \xE2\x80\x94 d\xC3\xA9sactiv\xC3\xA9" "e sur le service";
            }
            rows.push_back({l.licenseId, l.customer, edLabel, typeLabel(l.type) + (Workspace::isOnlineLicence(l) ? std::string(" (en ligne)") : std::string()), pcs,
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
        const bool onlineLic = e && Workspace::isOnlineLicence(e->licence);
        const bool ready = ws().service().ready();
        const online::OnlineLicence* on = onlineLic ? ws().service().snapshot.licence(e->licence.licenseId) : nullptr;
        addPc_->setEnabled(e != nullptr);
        addPc_->setText(onlineLic ? std::string("Prolonger\xE2\x80\xA6") : std::string("Ajouter un PC\xE2\x80\xA6"));
        export_->setEnabled(e != nullptr);
        revoke_->setEnabled(e != nullptr);
        delete_->setVisibility(e && e->revoked ? ui::Visibility::Visible : ui::Visibility::Collapsed);   // 1.38.0 : une revoquee seulement
        dropPc_->setText(onlineLic ? std::string("Retirer ce PC\xE2\x80\xA6") : std::string("Retirer un PC\xE2\x80\xA6"));
        dropPc_->setEnabled(e != nullptr && (onlineLic ? on && !on->activations.empty() : e->licence.devices.size() > 1));
        newOnline_->setEnabled(ready);
        refreshOnline_->setVisibility(ready ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        newCode_->setVisibility(onlineLic ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        showCode_->setVisibility(onlineLic ? ui::Visibility::Visible : ui::Visibility::Collapsed);   // 1.37.0
        active_->setVisibility(onlineLic ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        newCode_->setEnabled(onlineLic && ready);
        active_->setEnabled(onlineLic && ready);
        active_->setText(on && !on->active ? std::string("R\xC3\xA9" "activer\xE2\x80\xA6") : std::string("D\xC3\xA9sactiver\xE2\x80\xA6"));
        active_->setStyle(on && !on->active ? ui::Button::Style::Default : ui::Button::Style::Danger);
        invalidateLayout();
        selectedPc_ = -1;
        onlineKeys_.clear();
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
            if (onlineLic) {
                a.properties.push_back(prop("En ligne", l.service, "Une licence sans PC li\xC3\xA9 : chaque PC l'active avec son code aupr\xC3\xA8s de ce service."));
                a.properties.push_back(prop(l.type == LicenseType::Floating ? "Postes en m\xC3\xAAme temps" : "PC au plus", std::to_string(l.maxDevices)));
                if (on) {
                    a.properties.push_back(prop("Sur le service", std::string(on->active ? "active" : "D\xC3\xA9SACTIV\xC3\x89" "E") + ", " + std::to_string(on->activeCount()) +
                                                                      " PC activ\xC3\xA9(s), bail de " + std::to_string(on->leaseDays) + " j" +
                                                                      (on->hasCode ? std::string() : std::string(", sans code")) +
                                                                      (on->issuedAt != l.issuedAt ? std::string(" (une autre \xC3\xA9mission que celle de l'archive)") : std::string())));
                    a.properties.push_back(prop("Relev\xC3\xA9 le", dateTimeFr(ws().service().snapshot.at)));
                } else {
                    a.properties.push_back(prop("Sur le service", ws().service().snapshot.at ? std::string("absente du dernier relev\xC3\xA9 (pas d\xC3\xA9pos\xC3\xA9" "e ?)")
                                                                                              : std::string("jamais relev\xC3\xA9 : Relever")));
                }
            } else {
                a.properties.push_back(prop("PC", std::to_string(l.devices.size()) + " li\xC3\xA9(s) sur " + std::to_string(l.maxDevices) + " autoris\xC3\xA9(s)"
                                                      + (e->revokedDevices ? ", " + std::to_string(e->revokedDevices) + " r\xC3\xA9voqu\xC3\xA9(s)" : std::string())));
            }
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

            if (on)   // en ligne : les activations, au dernier releve
                for (const auto& act : on->activations) {
                    onlineKeys_.push_back(act.deviceKey);
                    rows.push_back({act.name.empty() ? std::string("(sans nom)") : act.name, act.stateLabel(), licdlg::fingerprintOfKey(act.deviceKey),
                                    dateFr(act.activatedAt), dateTimeFr(act.seenAt), dateFr(act.until)});
                    tips.push_back("Vu le " + dateTimeFr(act.seenAt) + " ; son activation vaut jusqu'au " + dateTimeFr(act.until) + " sans nouvelle connexion.");
                    revoked.push_back(act.state != "active");
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
        if (onlineLic != pcsOnline_) {
            pcsOnline_ = onlineLic;
            pcsColumnsFor_ = -1.f;
            fitPcsColumns();
        }
        pcsModel_ = std::make_shared<Rows>(onlineLic ? std::vector<std::string>{"PC", "\xC3\x89tat", "Empreinte", "Activ\xC3\xA9 le", "Vu le", "Bail jusqu'au"}
                                                     : std::vector<std::string>{"PC", "\xC3\x89tat", "Empreinte", "Activ\xC3\xA9 le"},
                                           std::move(rows),
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

    // 1.33.0 : retirer le PC choisi de la licence (une nouvelle emission sans lui, la meme fin). 1.35.0 : en ligne, ce PC
    // n'est plus renouvele par le service.
    void dropSelectedPc() {
        const LicenceEntry* e = selected();
        if (!e) return;
        if (host_.busy && host_.busy()) {
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
            return;
        }
        if (Workspace::isOnlineLicence(e->licence)) {
            retireOnline(e->licence.licenseId);
            return;
        }
        if (selectedPc_ < 0 || selectedPc_ >= static_cast<int>(e->licence.devices.size())) {
            host_.flash("Choisis d'abord le PC \xC3\xA0 retirer, dans la liste des PC de la licence.", Tone::Warning);
            return;
        }
        const std::string id = e->licence.licenseId;
        const auto& dev = e->licence.devices[static_cast<std::size_t>(selectedPc_)];
        const std::string key = dev.key;
        const std::string name = dev.name.empty() ? std::string("(sans nom)") : dev.name;
        auto alive = alive_;
        const std::string text = "Retirer le PC \xC2\xAB " + name + " \xC2\xBB (" + licdlg::fingerprintOfKey(key) + ") de la licence " + id
                                 + " ?\n\nUne nouvelle \xC3\xA9mission de la licence est sign\xC3\xA9" "e sans lui (m\xC3\xAA" "me fin, m\xC3\xAA" "mes fonctionnalit\xC3\xA9s) : "
                                   "tu choisis o\xC3\xB9 l'\xC3\xA9" "crire, puis tu l'envoies au client. Pour que ce PC ne serve plus avec l'ancienne \xC3\xA9mission, "
                                   "r\xC3\xA9voque-le aussi (R\xC3\xA9voquer\xE2\x80\xA6).";
        host_.openDialog(licdlg::makeConfirmDialog("dialog.retirer-pc", "Retirer un PC", text, "Retirer et signer\xE2\x80\xA6", true, [this, alive, id, key] {
            if (!*alive) return;
            ui::FilePick pick;
            pick.title = "La nouvelle \xC3\xA9mission de la licence " + id;
            pick.filters = {{"Licences XPG (.xpglic)", "xpglic"}};
            pick.save = true;
            pick.start = licdlg::startFor(id + ".xpglic");
            host_.pickFile(pick, [this, alive, id, key](const std::string& chosen) {
                if (!*alive) return;
                if (chosen.empty()) {
                    host_.flash("Annul\xC3\xA9 : rien n'a \xC3\xA9t\xC3\xA9 sign\xC3\xA9.", Tone::Info);
                    return;
                }
                std::string path = chosen;
                if (extOf(path) != ".xpglic") path += ".xpglic";
                const OpResult r = ws().removeDevice(id, key, path);
                if (r.ok) licdlg::rememberFolder(path);
                host_.report(r);
                if (r.ok) (void)publishToService(host_);   // 1.35.0 : le service regle, les PC sont prevenus
            });
        }));
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
        const std::string archiveDir = archive::archiveFolder(ws().folder());
        const fs::path licences = P(archiveDir) / "licences";
        std::error_code ec;
        host_.openFolder(fs::is_directory(licences, ec) ? U(licences) : archiveDir);
    }

    // ---- 1.35.0 : les licences en ligne ----
    [[nodiscard]] bool busyNow() {
        if (!(host_.busy && host_.busy())) return false;
        host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
        return true;
    }

    void fetchOnline() {
        if (busyNow()) return;
        auto alive = alive_;
        (void)refreshOnlineLicences(host_, [this, alive](const OpResult&) {
            if (*alive) rebuild();
        });
    }

    void retireOnline(const std::string& id) {
        if (selectedPc_ < 0 || selectedPc_ >= static_cast<int>(onlineKeys_.size())) {
            host_.flash("Choisis d'abord le PC \xC3\xA0 retirer, dans la liste de ses activations (Relever pour la mettre \xC3\xA0 jour).", Tone::Warning);
            return;
        }
        const std::string key = onlineKeys_[static_cast<std::size_t>(selectedPc_)];
        auto alive = alive_;
        ShellHost host = host_;
        host_.openDialog(licdlg::makeConfirmDialog(
            "dialog.retirer-pc-en-ligne", "Retirer ce PC",
            "Retirer le PC " + licdlg::fingerprintOfKey(key) + " de la licence en ligne " + id +
                " ?\n\nIl n'est plus renouvel\xC3\xA9. Son Studio, ouvert et connect\xC3\xA9, le voit dans les 2 minutes (une fen\xC3\xAAtre le lui dit) ; "
                "sinon \xC3\xA0 son prochain lancement connect\xC3\xA9, au plus tard \xC3\xA0 la fin de son bail. Sa place se lib\xC3\xA8re ; le client "
                "peut activer un autre PC avec le m\xC3\xAAme code.",
            "Retirer", true, [this, alive, host, id, key] {
                if (!*alive || (host.busy && host.busy())) return;
                const Workspace* w = &host.workspace();
                (void)host.runSlow("Service : retrait d'un PC de " + id, [w, id, key] { return w->serviceRetire(id, key); },
                                   [this, alive, host](const OpResult& r) {
                                       if (!*alive || !r.ok) return;
                                       (void)refreshOnlineLicences(host, [this, alive](const OpResult&) {
                                           if (*alive) rebuild();
                                       });
                                   });
            }));
    }

    void askNewCode() {
        const LicenceEntry* e = selected();
        if (!e || !Workspace::isOnlineLicence(e->licence) || busyNow()) return;
        const std::string id = e->licence.licenseId;
        auto alive = alive_;
        host_.openDialog(licdlg::makeConfirmDialog(
            "dialog.nouveau-code", "Un nouveau code d'activation",
            "Tirer un nouveau code pour la licence " + id + " ?\n\nL'ancien ne marchera plus d\xC3\xA8s que le nouveau est d\xC3\xA9pos\xC3\xA9 (les PC d\xC3\xA9j\xC3\xA0 "
            "activ\xC3\xA9s restent). Le nouveau s'affiche, et il est gard\xC3\xA9 chiffr\xC3\xA9 sur ce poste.",
            "Tirer un nouveau code", false, [this, alive, id] {
                if (!*alive) return;
                ServiceJob job;
                std::string code;
                const OpResult r = ws().prepareNewCode(id, job, code);
                if (!r.ok) {
                    host_.report(r);
                    return;
                }
                if (!depositOnlineLicence(host_, std::move(job), std::move(code), [this, alive](const OpResult& d) {
                        if (*alive && d.ok) fetchOnline();
                    }))
                    host_.flash("Un travail est en cours : rien n'est parti, le code est oubli\xC3\xA9. Recommence quand il a fini.", Tone::Warning);
            }));
    }

    // 1.37.0 : le code garde chiffre sur ce poste, montre dans la fenetre du code (Afficher, Copier)
    void showCode() {
        const LicenceEntry* e = selected();
        if (!e || !Workspace::isOnlineLicence(e->licence)) return;
        std::string code, why;
        if (!online::readCode(ws().folder(), e->licence.licenseId, code, &why)) {
            host_.report(OpResult::fail(why));
            return;
        }
        host_.openDialog(makeCodeDialog(e->licence.licenseId, code));
        deploy::crypto::wipe(code.data(), code.size());
    }

    // 1.38.0 : supprimer la licence revoquee choisie (une question d'abord).
    void askDelete() {
        const LicenceEntry* e = selected();
        if (!e || !e->revoked || busyNow()) return;
        const std::string id = e->licence.licenseId;
        auto alive = alive_;
        ShellHost host = host_;
        host_.openDialog(licdlg::makeConfirmDialog(
            "dialog.supprimer-licence", "Supprimer la licence",
            "Supprimer la licence r\xC3\xA9voqu\xC3\xA9" "e " + id + " de l'archive ?\n\nElle quitte la liste ; son fichier va dans la corbeille de l'outil "
            "(rien n'est effac\xC3\xA9). Sa r\xC3\xA9vocation reste : les Studios la refusent toujours. La synchro de l'archive ne la ram\xC3\xA8ne pas.",
            "Supprimer", true, [this, alive, host, id] {
                if (!*alive) return;
                const OpResult r = host.workspace().deleteRevokedLicence(id);
                host.report(r);
                if (*alive && r.ok) {
                    selectedId_.clear();
                    refresh();
                }
            }));
    }

    void askActive() {
        const LicenceEntry* e = selected();
        if (!e || !Workspace::isOnlineLicence(e->licence) || busyNow()) return;
        const std::string id = e->licence.licenseId;
        const auto* on = ws().service().snapshot.licence(id);
        const bool activate = on && !on->active;
        auto alive = alive_;
        ShellHost host = host_;
        host_.openDialog(licdlg::makeConfirmDialog(
            activate ? "dialog.reactiver" : "dialog.desactiver", activate ? "R\xC3\xA9" "activer la licence" : "D\xC3\xA9sactiver la licence",
            activate ? "R\xC3\xA9" "activer la licence en ligne " + id + " ?\n\nSon code marche de nouveau et ses PC sont renouvel\xC3\xA9s \xC3\xA0 leur prochaine connexion."
                     : "D\xC3\xA9sactiver la licence en ligne " + id + " ?\n\nSon code ne marche plus ; ses PC ne sont plus renouvel\xC3\xA9s. Un Studio ouvert et "
                       "connect\xC3\xA9 la perd dans les 2 minutes (une fen\xC3\xAAtre le dit au client) ; sinon \xC3\xA0 son prochain lancement connect\xC3\xA9, au "
                       "plus tard \xC3\xA0 la fin de son bail. Pour l'annuler pour de bon, r\xC3\xA9voque-la aussi.",
            activate ? "R\xC3\xA9" "activer" : "D\xC3\xA9sactiver", !activate, [this, alive, host, id, activate] {
                if (!*alive || (host.busy && host.busy())) return;
                const Workspace* w = &host.workspace();
                (void)host.runSlow(std::string(activate ? "Service : r\xC3\xA9" "activation de " : "Service : d\xC3\xA9sactivation de ") + id,
                                   [w, id, activate] { return w->serviceSetActive(id, activate); }, [this, alive, host](const OpResult& r) {
                                       if (!*alive || !r.ok) return;
                                       (void)refreshOnlineLicences(host, [this, alive](const OpResult&) {
                                           if (*alive) rebuild();
                                       });
                                   });
            }));
    }

    ui::Button*                 new_{nullptr};
    ui::Button*                 newOnline_{nullptr};
    ui::Button*                 refreshOnline_{nullptr};
    ui::Button*                 newCode_{nullptr};
    ui::Button*                 showCode_{nullptr};   // 1.37.0
    ui::Button*                 active_{nullptr};
    std::vector<std::string>    onlineKeys_;
    bool                        pcsOnline_{false};
    ui::Button*                 addPc_{nullptr};
    ui::Button*                 export_{nullptr};
    ui::Button*                 revoke_{nullptr};
    ui::Button*                 delete_{nullptr};    // 1.38.0
    ui::Button*                 dropPc_{nullptr};
    int                         selectedPc_{-1};
    ui::Button*                 folder_{nullptr};
    ui::InputText*              search_{nullptr};
    FilterChip*                 chips_[4]{};
    ui::Widget*                 holder_{nullptr};    // 1.41.0 : les boutons du menu << ... >> (jamais dessines)
    ui::Button*                 more_{nullptr};
    ui::PopupMenu*              menu_{nullptr};
    std::vector<ui::Button*>    menuButtons_;
    gfx::Rect                   listRect_{}, cardRect_{};
    float                       headY_{0.f}, statsY_{0.f}, pcsTitleY_{0.f}, detailTitleY_{0.f};
    ui::TableView*              table_{nullptr};
    ui::PropertyGrid*           details_{nullptr};
    ui::TableView*              pcs_{nullptr};
    std::shared_ptr<Rows>       model_, pcsModel_;
    std::vector<std::string>    shownIds_;
    std::string                 selectedId_;
    Filter                      filter_{Filter::All};
    bool                        syncing_{false};
    float                       filterY_{0.f}, columnsFor_{-1.f}, pcsColumnsFor_{-1.f};
    std::shared_ptr<bool>       alive_ = std::make_shared<bool>(true);
    core::ConnectionScope       links_;
};

} // namespace

PagePtr makeLicencesPage(ShellHost host) { return std::make_unique<LicencesPage>(std::move(host)); }

} // namespace license_admin::gui
