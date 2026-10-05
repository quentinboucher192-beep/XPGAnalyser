// =============================================================================
//  app/ApiListKit.hpp - lot API 5 : ce que les listes de l'API partagent
// -----------------------------------------------------------------------------
//  Les onglets Types derives, Blocs DFB, Unites, Variables et Sous-routines
//  montrent chacun une liste, avec au-dessus la meme ligne : un champ de
//  recherche et des filtres en pastilles (« Tous 28 », « Du projet 18 »...),
//  comme le choix des variables d'une table d'animation. Un clic sur une
//  pastille la choisit ; le texte tape filtre en plus.
//
//  Et quelques aides de texte : sans la casse, les milliers, l'etat face a la
//  bibliotheque (« projet », « a jour », « 1.3 disponible »).
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../project/SharedLibrary.hpp"
#include "../domain/ProjectModel.hpp"
#include "../ui/Widget.hpp"
#include "../ui/widgets/FilterMemory.hpp"     // lot API 8 : la recherche retenue

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ui { class InputText; }

namespace app {

class ApiFilterBar final : public ui::Widget {
public:
    ApiFilterBar(std::string id, std::string placeholder);
    // Les pastilles : (libelle, nombre). La choisie reste la meme si elle existe encore.
    void setChips(std::vector<std::pair<std::string, std::size_t>> chips);
    void setCurrent(std::size_t index);
    [[nodiscard]] std::size_t current() const noexcept { return current_; }
    // La pastille dont le libelle commence par `label` (sans la casse) ; faux : aucune.
    bool chooseChip(std::string_view label);
    [[nodiscard]] std::string search() const;
    void setSearch(const std::string& text);
    [[nodiscard]] gfx::Rect chipRect(std::size_t index) const;
    [[nodiscard]] std::size_t chipCount() const noexcept { return chips_.size(); }
    [[nodiscard]] ui::SizeHint sizeHint() const override;
    // La recherche ou la pastille a change.
    const core::SignalPtr<> changed = core::Signal<>::create();

    // ---- Lot API 8 : retenue d'une seance a l'autre, et le compte ----
    //  La recherche tapee et la pastille choisie sont retenues (ui::FilterMemory,
    //  par projet, sous l'id de la barre) : recall() les relit et les pose - le
    //  volet l'appelle une fois `changed` branche ; sinon, au premier placement
    //  (un geste fait avant, une recherche posee par programme, l'emporte). Une
    //  pastille retenue arrive avec les pastilles (setChips) si elles n'y sont
    //  pas encore.
    void recall();
    // "12 sur 40", a droite de la barre, tant qu'une recherche est tapee
    // (setCount jamais appele : rien, comme avant).
    void setCount(std::size_t shown, std::size_t total);
    [[nodiscard]] std::string countText() const;
    [[nodiscard]] ui::InputText& field() noexcept { return *field_; }
    // Le libelle de la pastille choisie ("" : aucune pastille) - le journal des scripts.
    [[nodiscard]] std::string currentChip() const { return current_ < chips_.size() ? chips_[current_].first : std::string{}; }
    // ---- fin Lot API 8 ----
protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    ui::InputText*                                  field_{nullptr};
    std::vector<std::pair<std::string, std::size_t>> chips_;
    mutable std::vector<gfx::Rect>                  rects_;
    std::size_t                                     current_{0};
    int                                             hover_{-1};
    core::ConnectionScope                           links_;
    // ---- Lot API 8 ----
    void applyRemembered(const ui::SearchMemory::State& state);
    ui::SearchMemory                                memory_;
    bool                                            recalled_{false};
    std::string                                     pendingChip_;     // retenue, avant que les pastilles n'arrivent
    std::size_t                                     countShown_{0}, countTotal_{0};
    bool                                            counted_{false};
    // ---- fin Lot API 8 ----
};

namespace apikit {

[[nodiscard]] std::string lower(std::string_view);
[[nodiscard]] bool containsNoCase(std::string_view hay, std::string_view needle);
// Lot recherche : LA recherche des listes (ui::SearchQuery) - chaque mot dans
// l'un des textes (ET), "une phrase", -mot exclu ; sans casse ni accents.
// Pour beaucoup de lignes, garder un ui::SearchQuery plutot que le relire ici.
[[nodiscard]] bool matches(std::initializer_list<std::string_view> texts, std::string_view query);
[[nodiscard]] std::string thousands(std::size_t);
[[nodiscard]] std::string plural(std::size_t n, std::string_view one, std::string_view many);

// Un type ou un bloc face a la bibliotheque partagee.
struct LibState {
    enum Kind : std::uint8_t { Absent, UpToDate, Different } kind{Absent};
    std::string version;     // celle de la bibliotheque
    [[nodiscard]] std::string label() const;     // « projet », « a jour », « 1.3 disponible »
};
[[nodiscard]] LibState libraryState(const project::SharedLibrary*, std::string_view name, std::string_view projectVersion,
                                    project::LibraryItemKind kind);

// Lot API 6 : COLLER DEPUIS EXCEL. Un type que Control Expert connaitrait :
// elementaire (BOOL, INT, REAL, STRING[32]...), un DDT ou un DFB du projet,
// ARRAY[a..b] OF l'un d'eux (pas de tableau de DFB). Faux : `why` dit pourquoi.
[[nodiscard]] bool validPlcType(const domain::Project& p, std::string_view type, std::string* why);
// Une adresse lisible (%MW10, %I0.3...) ; vide : permise (pas situee).
[[nodiscard]] bool validAddress(std::string_view address, std::string* why);
// Un nom libre parmi `taken` (sans casse) : "Nom", "Nom_2", "Nom_3"...
[[nodiscard]] std::string freeName(std::string_view wanted, const std::vector<std::string>& taken);

// ---- Lot API 8 : chercher dans les listes qui n'avaient pas de recherche ----
// Le commentaire en tete d'un code ST - (* ... *), ou des lignes // - sur une
// ligne, les blancs resserres, `max` octets au plus (coupe sur un debut de
// caractere, suivi de "...") : la description d'une sous-routine. "" : aucun.
[[nodiscard]] std::string leadingComment(std::string_view code, std::size_t max = 160);
// UNE RECHERCHE SUR DEUX NIVEAUX (une tache et ce qu'elle execute) : un enfant
// est garde quand chaque terme est dans ses textes OU dans ceux de son parent
// (MAST garde toutes ses entrees ; "MAST Gestion", les entrees Gestion de
// MAST) ; le parent, quand ses textes suffisent ou qu'il garde un enfant. Une
// recherche vide garde tout.
struct TwoLevelMatch {
    bool              parent{true};
    std::vector<bool> children;
};
[[nodiscard]] TwoLevelMatch searchTwoLevels(const ui::SearchQuery& query, const std::vector<std::string>& parentTexts,
                                            const std::vector<std::vector<std::string>>& childTexts);
// ---- fin Lot API 8 ----

} // namespace apikit
} // namespace app
