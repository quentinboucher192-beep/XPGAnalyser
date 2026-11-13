// =============================================================================
//  license_admin/gui/PcPicker.hpp - 1.51.0 : CHERCHER LE PC D'UN FUTUR ADMIN
//  (<< Ajouter un admin >>, << Dans les notifications de son Studio >>)
// -----------------------------------------------------------------------------
//  La demande (1.51.0) : << ajoute un systeme de recherche pour chercher le PC plus
//  efficace >>. A la place de la liste deroulante : un champ de recherche et, dessous,
//  la liste des PC, toujours visible.
//   - la recherche (la meme que partout : ui::SearchQuery - les mots ET, "une
//     phrase", -mot exclu ; ni la casse ni les accents ne comptent) porte sur le nom
//     du PC, son identifiant, le client, la licence, le Studio et sa version, la
//     derniere activite (<< aujourd'hui >>, << hier >>, << 12/11/2026 >>) ;
//   - les plus recemment actifs d'abord ; ce qui correspond est surligne ;
//   - le compte : << 12 PC >> sans recherche, << 3 sur 240 PC >> avec ;
//   - au clavier, dans le champ : Haut / Bas changent le PC choisi (la liste le suit),
//     Page haut / Page bas d'une page, Entree le prend (picked), Echap efface la
//     recherche (puis quitte le champ) ; a la souris : un clic le choisit, un
//     double-clic le prend, la molette fait defiler ;
//   - des centaines de PC : les textes de chaque PC sont plies UNE fois (a l'arrivee
//     des PC), une recherche ne fait que des find() ; seules les lignes visibles sont
//     dessinees.
//  Les PC sont ceux que connait ce poste (Workspace::pcsForMessages : les activations,
//  les demandes, les licences en ligne).
// =============================================================================
#pragma once

#include "Workspace.hpp"

#include "../../core/Signal.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <string>
#include <utility>
#include <vector>

namespace license_admin::gui {

class PcPicker final : public ui::Widget {
public:
    // `id` : celui du widget ; le champ de recherche : id + ".recherche" (les scripts le tapent).
    explicit PcPicker(std::string id);

    // Les PC (dans n'importe quel ordre) et l'heure : la liste se trie (les plus recemment actifs d'abord) et
    // choisit le premier. La recherche tapee reste.
    void setPcs(std::vector<MessagePc> pcs, Seconds now);
    [[nodiscard]] const std::vector<MessagePc>& pcs() const noexcept { return pcs_; }

    // La recherche (ce qui est tape dans le champ ; les essais, les scripts).
    void setQuery(const std::string& text);
    [[nodiscard]] const std::string& query() const noexcept;
    [[nodiscard]] ui::InputText& field() noexcept { return *field_; }

    // Les PC montres, dans l'ordre de la liste (des rangs dans pcs()).
    [[nodiscard]] const std::vector<int>& shown() const noexcept { return shown_; }
    // Le PC choisi (nul : aucun - rien ne correspond, ou aucun PC) ; sa valeur : son identifiant sur le service
    // (sinon sa cle), celle que prend Workspace::sendAdminInstaller.
    [[nodiscard]] const MessagePc* selected() const;
    [[nodiscard]] std::string selectedValue() const;
    bool select(const std::string& value);   // par identifiant ou cle ; faux : pas montre
    bool selectShown(int row);                // la ligne `row` (0 : la premiere) de la liste montree ; faux : pas de ligne
    void moveSelection(int delta);           // Haut / Bas (+-1), Page haut / bas (+-une page)
    void pick();                              // Entree, double-clic : `picked`

    // << 12 PC >>, << 3 sur 240 PC >>, << aucun PC sur 240 >>, << aucun PC connu sur ce poste >>.
    [[nodiscard]] std::string countText() const;
    // La derniere activite d'un PC, comme la liste la dit (et la cherche) : << aujourd'hui >>, << hier >>, << il y a
    // 3 jours >>, << le 12/11/2026 >>, << jamais vu >>.
    [[nodiscard]] std::string lastSeenText(const MessagePc& pc) const;
    // Le libelle d'un PC (son nom, sinon le debut de son identifiant) et ce que la recherche y surligne : des plages
    // [debut, fin) d'octets du libelle.
    [[nodiscard]] static std::string labelOf(const MessagePc& pc);
    [[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> highlights(const std::string& text) const;
    // La liste : la premiere ligne montree, combien en tiennent.
    [[nodiscard]] int firstVisible() const noexcept { return first_; }
    [[nodiscard]] int visibleRows() const noexcept;

    const core::SignalPtr<> selectionChanged = core::Signal<>::create();
    const core::SignalPtr<> picked = core::Signal<>::create();

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;

private:
    void refilter();
    void ensureVisible();
    [[nodiscard]] int rowAt(gfx::Point p) const;

    ui::InputText*                   field_{nullptr};
    std::vector<MessagePc>           pcs_;
    std::vector<std::string>         folded_;   // par PC : ses textes de recherche plies, separes par \x1F
    std::vector<int>                 order_;    // tous les PC, les plus recemment actifs d'abord
    std::vector<int>                 shown_;    // ceux qui correspondent, dans cet ordre
    int                              current_{-1};   // un rang dans shown_ ; -1 : aucun
    int                              first_{0};
    Seconds                          now_{0};
    gfx::Rect                        listRect_{};
    float                            rowH_{46.f};
    core::ConnectionScope            links_;
};

} // namespace license_admin::gui
