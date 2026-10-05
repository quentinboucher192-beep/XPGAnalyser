// =============================================================================
//  app/GrafcetCodePanel.hpp - 1.10, chantier R2 (decision 11 bis) : le volet Code
// -----------------------------------------------------------------------------
//  Sous le dessin du grafcet, le code de l'element choisi : la receptivite d'une
//  transition, le corps d'une action. Le MEME editeur que les sections ST du
//  programme (ui::MultiLineText : coloration, aide a la saisie, signatures, valeur
//  au survol en simulation) - l'ecran lui donne les memes fournisseurs que ceux du
//  DocumentPane, plus les raccourcis du grafcet (X3, A2, FIN(A2), ACTIF(X3),
//  DUREE(X1) >= T#5s, FINI(Autre)).
//
//  Le volet ne modifie rien lui-meme : Ctrl+Entree, Appliquer, ou le quitter avec
//  un changement emet applyRequested ; l'ecran lance la commande annulable
//  (GrafcetEdit : SetTransitionConditionCommand, SetActionBodyCommand), relit le
//  grafcet et les onglets ST ouverts, et dit ici ce qui s'est passe.
// =============================================================================
#pragma once

#include "../ui/widgets/Containers.hpp"
#include "../ui/widgets/Controls.hpp"

#include <string>

namespace app {

    // L'editeur des sections, avec Ctrl+Entree pour appliquer (la touche Entree
    // seule reste un saut de ligne, comme dans une section).
    class GrafcetCodeEditor final : public ui::MultiLineText {
    public:
        explicit GrafcetCodeEditor(std::string id) : ui::MultiLineText(std::move(id)) {}
        const core::SignalPtr<> submitted = core::Signal<>::create();

    protected:
        ui::EventResult onEvent(const ui::InputEvent& ev) override;
    };

    class GrafcetCodePanel final : public ui::Widget {
    public:
        enum class Target : std::uint8_t { None, Transition, Action };

        explicit GrafcetCodePanel(std::string id);

        // Montrer le code d'un element. `title` : la ligne du haut ("Receptivite de
        // T2 - SFC_DetoxalA, ligne 69") ; `text` : le code tel qu'il est dans le
        // programme ; `hint` : la ligne du bas.
        void show(Target target, int id, std::string title, std::string text, std::string hint);
        // Rien de choisi (une etape, le fond) : le volet le dit, l'editeur se vide.
        void showNothing(std::string hint);

        [[nodiscard]] Target target() const noexcept { return target_; }
        [[nodiscard]] int    targetId() const noexcept { return targetId_; }
        [[nodiscard]] std::string text() const;
        // Le texte a change depuis show() ou le dernier markApplied().
        [[nodiscard]] bool dirty() const;
        // Le texte applique devient la reference (le volet n'est plus "modifie").
        void markApplied(std::string text);
        // Remettre le texte du programme (le bouton Retablir).
        void revert();

        void setStatus(std::string message, ui::StatusBar::Severity severity = ui::StatusBar::Severity::None);

        [[nodiscard]] GrafcetCodeEditor& editor() const { return *editor_; }

        // Ctrl+Entree, Appliquer, ou le volet quitte avec un changement.
        const core::SignalPtr<> applyRequested = core::Signal<>::create();

    protected:
        void onLayout() override;

    private:
        void refreshTitle();

        ui::StatusBar*     title_{ nullptr };
        ui::Button*        apply_{ nullptr };
        ui::Button*        revert_{ nullptr };
        GrafcetCodeEditor* editor_{ nullptr };
        ui::StatusBar*     status_{ nullptr };
        Target             target_{ Target::None };
        int                targetId_{ -1 };
        std::string        titleText_;
        std::string        baseline_;     // le texte du programme (ou le dernier applique)
        core::ConnectionScope links_;
    };

} // namespace app
