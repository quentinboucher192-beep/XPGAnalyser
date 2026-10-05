// =============================================================================
//  app/VariablePicker.hpp - lot API 3 : choisir des variables pour une table
// -----------------------------------------------------------------------------
//  « + Variable IHM » (les variables de l'IHM, 72 dans Armoire_Gaz) et
//  « + Variable API » (celles de l'automate) : chercher, filtrer par genre,
//  cocher, « Ajouter N variables ». Une variable deja dans la table reste
//  visible, grisee, et ne se coche pas.
//
//  Le dialogue ne touche pas au projet : il rend les noms choisis (dans
//  DialogResult::payload, un par ligne) et l'ecran en fait UNE commande - un
//  seul Ctrl+Z les retire toutes.
// =============================================================================
#pragma once

#include "../menu/IMenu.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace app {

class VariablePicker final : public menu::WidgetMenu {
public:
    struct Candidate {
        std::string name, type, detail;   // detail : la description, le commentaire ou l'adresse
        bool        linked{false};        // IHM : liee a l'automate ; API : situee (%M, %I...)
        bool        present{false};       // deja dans la table
    };
    struct Spec {
        std::string            title;     // « Ajouter des variables IHM a PURGE »
        bool                   hmi{false};
        std::vector<Candidate> candidates;
        std::string            note;      // en bas a gauche
    };

    explicit VariablePicker(Spec spec);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return spec_.title; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // Les noms choisis, dans l'ordre de la liste.
    [[nodiscard]] static std::vector<std::string> parse(const std::string& payload);

    // Pour les scripts et les tests : cocher / decocher une ligne par son nom,
    // choisir un filtre par le debut de son libelle (« BOOL », « Li »), chercher.
    bool toggle(std::string_view name);
    bool chooseFilter(std::string_view label);
    void setSearch(const std::string& text);
    [[nodiscard]] std::size_t checkedCount() const;
    [[nodiscard]] std::size_t shownCount() const;
    void finish(bool ok);

protected:
    core::Status buildUi() override;

private:
    class Body;
    Spec  spec_;
    Body* body_{nullptr};
    bool  done_{false};
};

} // namespace app
