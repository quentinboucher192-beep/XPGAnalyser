// =============================================================================
//  ui/widgets/SearchField.hpp - lot API 8 : le champ de recherche d'un volet
// -----------------------------------------------------------------------------
//  Le champ de toutes les recherches de l'application (ui::SearchQuery : les
//  mots ET, "une phrase", -mot exclu, sans casse ni accents) et, a sa droite,
//  le compte "12 sur 40" tant qu'une recherche est tapee. Le texte tape est
//  retenu d'une seance a l'autre (ui::FilterMemory, sous l'id du champ) : le
//  volet appelle recall() une fois son signal `changed` branche (sinon, au
//  premier placement).
//
//    +--------------------------------------------+
//    | [ Rechercher : nom, description...    ]  3 sur 12 |
//    +--------------------------------------------+
// =============================================================================
#pragma once

#include "FilterMemory.hpp"

#include <cstddef>
#include <string>

namespace ui {

class InputText;

class SearchField final : public Widget {
public:
    // `fieldId` : l'id du champ (hmi.recipes.search) - les scripts le tapent
    // (champ "hmi.recipes.search" "gaz"), la memoire le retient sous lui. Le
    // widget : fieldId + ".zone".
    SearchField(const std::string& fieldId, std::string placeholder, std::string tooltip = {});
    ~SearchField() override;

    [[nodiscard]] const std::string& text() const noexcept;
    [[nodiscard]] const SearchQuery& query() const noexcept { return query_; }
    void setText(const std::string& text);               // `changed` part (s'il change)
    // Le compte : "12 sur 40" a droite du champ, tant qu'une recherche est tapee.
    void setCount(std::size_t shown, std::size_t total);
    [[nodiscard]] std::string countText() const;          // "" : pas de recherche
    [[nodiscard]] InputText& field() noexcept { return *field_; }
    void recall();                                         // la recherche retenue (une fois branche)
    [[nodiscard]] SizeHint sizeHint() const override;

    const core::SignalPtr<> changed = core::Signal<>::create();

protected:
    void onLayout() override;
    void onPaint(const PaintContext&) override;

private:
    InputText*            field_{nullptr};
    SearchQuery           query_;
    std::size_t           shown_{0}, total_{0};
    bool                  counted_{false};
    bool                  recalled_{false};
    SearchMemory          memory_;
    core::ConnectionScope links_;
};

} // namespace ui
