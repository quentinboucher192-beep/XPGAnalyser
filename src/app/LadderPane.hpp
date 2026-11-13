// =============================================================================
//  app/LadderPane.hpp - 1.51.0 : l'editeur graphique d'une section LD (ladder)
// -----------------------------------------------------------------------------
//  Le volet "Sch\xC3\xA9ma" d'une section LD (le volet "Texte" garde la lecture en
//  texte, project/GraphListing) :
//
//    en haut     la palette : contacts (ouvert, ferme, fronts), bobines (normale,
//                inverse, S, R), bloc (TON, ADD, un DFB...), comparaison,
//                operation, fil, liaison verticale, branche en parallele (OU),
//                supprimer, ligne neuve / supprimer la ligne, commentaire, zoom ;
//    au centre   le dessin : les rails, les rangees, les symboles, les blocs et
//                leurs broches, les commentaires ; un clic choisit, le double-clic
//                (ou Entree) modifie la variable - la liste propose les variables
//                du projet ; les fleches deplacent le choix, Alt + fleches
//                deplacent l'element ;
//    en bas      ce qui est choisi, et en simulation sa valeur.
//
//  Chaque geste ecrit le XML de la section (project::ladder::write : les reseaux
//  non touches restent octet pour octet) par UNE commande (LadderEditCommand) :
//  Ctrl+Z le defait. En simulation, le dessin lit le runtime a chaque image
//  (sim::Runtime::ladderLive) : le courant en vert, les valeurs des variables et
//  des broches ; 1 / 0 forcent la variable choisie, L la libere.
//
//  Les touches (le dessin a le clavier) : C contact, Maj+C contact ferme, P / N
//  fronts, B bobine, Maj+B inverse, S / R mise a 1 / a 0, F bloc, K comparaison,
//  O operation, H fil, V liaison verticale, U branche en parallele, I inverser,
//  T changer de type, Suppr effacer, Inser ligne neuve, Maj+Suppr supprimer la ligne.
// =============================================================================
#pragma once

#include "../project/Ladder.hpp"
#include "../project/LadderEdit.hpp"
#include "../ui/Widget.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui { class ToolBar; class StatusBar; }
namespace sim { class Runtime; }

namespace app {

    class App;
    class LadderCanvas;
    class LadderField;

    class LadderPane final : public ui::Widget {
    public:
        LadderPane(App& app, domain::Index section, std::string paneId);
        ~LadderPane() override;

        // Relit la section (apres Ctrl+Z, une modification ailleurs) ; garde le choix s'il existe encore.
        void reload();

        [[nodiscard]] const project::ladder::Diagram& diagram() const noexcept { return diagram_; }
        [[nodiscard]] domain::Index section() const noexcept { return section_; }
        [[nodiscard]] bool readable() const noexcept { return readable_; }

        // ---- le choix ---------------------------------------------------------------
        [[nodiscard]] project::ladder::Pos selection() const noexcept { return sel_; }
        bool select(project::ladder::Pos p);
        // Une ligne du texte (le volet Texte, une reference croisee) : la rangee qu'elle dit.
        bool revealListingLine(std::size_t line);

        // ---- les gestes (la palette, les touches, les scripts) -------------------------
        // "ld.contact", "ld.closed", "ld.rising", "ld.falling", "ld.coil", "ld.notCoil",
        // "ld.set", "ld.reset", "ld.block", "ld.compare", "ld.operate", "ld.wire", "ld.vlink",
        // "ld.parallel", "ld.invert", "ld.type", "ld.erase", "ld.rung", "ld.delRung",
        // "ld.row", "ld.delRow", "ld.comment", "ld.edit", "ld.zoomIn", "ld.zoomOut", "ld.fit",
        // "ld.force1", "ld.force0", "ld.unforce".
        void command(const std::string& id);
        // Ouvre la saisie sur le choix (le nom, le texte, le type d'un bloc neuf...).
        bool openEditor();
        [[nodiscard]] bool editing() const noexcept { return editMode_ != 0; }
        // Le texte de la saisie ouverte, pour un script : tape `text` (la liste se met
        // a jour) ; `accept` : Entree.
        bool typeInEditor(const std::string& text, bool accept);
        [[nodiscard]] std::vector<std::string> editorSuggestions() const;
        // Pose directement la variable / le texte de l'element choisi (un pas de Ctrl+Z).
        bool setSelectionText(const std::string& text);
        // Pose un bloc de ce type a la case choisie (instance creee si c'est un FB).
        bool placeBlockOfType(const std::string& type);

        // L'etat pour un script (ladder-etat) : le choix, le nombre de reseaux, de rangees, en simulation le courant.
        [[nodiscard]] std::string describe() const;
        [[nodiscard]] const std::string& lastMessage() const noexcept { return lastMessage_; }

        // Le corps de la section a change (par cet editeur) : le volet Texte suit.
        const core::SignalPtr<> edited = core::Signal<>::create();

        // ---- pour le dessin -------------------------------------------------------------
        [[nodiscard]] sim::Runtime* runtime() const;
        [[nodiscard]] std::string runtimeSection() const;
        void say(const std::string& text, bool error = false);
        void keyCommand(const std::string& id) { command(id); }

    protected:
        void onLayout() override;
        void onPaint(const ui::PaintContext&) override;

    private:
        friend class LadderCanvas;
        bool change(const std::function<bool(project::ladder::Diagram&)>& op, const std::string& label);
        void commitEditor(const std::string& text);
        void closeEditor();
        void placeField();
        void showSelection();
        bool forceSelected(int value);   // 1, 0 ; -1 : liberer
        [[nodiscard]] std::string selectedVariable() const;

        App&                  app_;
        domain::Index         section_;
        std::shared_ptr<bool> alive_;
        project::ladder::Diagram diagram_;
        bool                  readable_{ false };
        std::string           body_;              // le corps lu (pour savoir s'il a change)
        project::ladder::Pos  sel_{};
        int                   editMode_{ 0 };     // 0 : pas de saisie (voir LadderPane.cpp)
        int                   editPin_{ -1 };     // la broche en saisie
        std::string           lastMessage_;
        std::string           statusLine_;

        ui::ToolBar*          bar_{ nullptr };
        LadderCanvas*         canvas_{ nullptr };
        LadderField*          field_{ nullptr };
        ui::StatusBar*        status_{ nullptr };
        core::ConnectionScope links_;
    };

    // Les commandes ladder-... d'un script (ScriptRunner) sur le volet ouvert :
    //   ladder-choisir R C [N]        la case (rangee R, colonne C, reseau N ; 1 = la premiere)
    //   ladder-outil ld.contact|...   un bouton de la palette (un element neuf ouvre sa saisie)
    //   ladder-taper "texte" [entree] tape dans la saisie ouverte (la liste se met a jour) ; entree : valider
    //   ladder-nommer "texte"         la variable (le texte) de l'element choisi, d'un coup
    //   ladder-bloc TYPE              un bloc de ce type a la case choisie
    //   ladder-broche K "valeur"      le parametre de la broche K (1 = la premiere) du bloc choisi
    //   ladder-forcer VAR 1|0|non     en simulation : forcer, liberer
    //   ladder-annuler                Ctrl+Z
    //   ladder-etat ["attendu"]       l'etat au journal ; attendu : doit y figurer
    bool runLadderScript(App& app, LadderPane& pane, const std::vector<std::string>& words, std::string& said, std::string& error);

    // "Aller a" une ligne d'une section (references croisees, recherche) : si son
    // ladder est ouvert en schema, la rangee de cette ligne y est choisie.
    void revealLadderLine(ui::Widget* root, domain::Index section, std::size_t line);

} // namespace app
